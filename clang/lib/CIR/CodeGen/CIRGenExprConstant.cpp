//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Constant Expr nodes as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "Address.h"
#include "CIRGenCXXABI.h"
#include "CIRGenConstantEmitter.h"
#include "CIRGenModule.h"
#include "CIRGenRecordLayout.h"
#include "TargetInfo.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinAttributeInterfaces.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "clang/AST/APValue.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/Builtins.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ErrorHandling.h"
#include <functional>
#include <iterator>

using namespace clang;
using namespace clang::CIRGen;

//===----------------------------------------------------------------------===//
//                            ConstantAggregateBuilder
//===----------------------------------------------------------------------===//

namespace {
class ConstExprEmitter;

static bool isAMDGPUNamedBarrierType(QualType t) {
  if (const auto *builtinTy = t->getAs<BuiltinType>()) {
    switch (builtinTy->getKind()) {
#define AMDGPU_OPAQUE_PTR_TYPE(Name, Id, SingletonId, Width, Align, AS)
#define AMDGPU_NAMED_BARRIER_TYPE(Name, Id, SingletonId, Width, Align, Scope)  \
    case BuiltinType::Id:                                                       \
      return true;
#include "clang/Basic/AMDGPUTypes.def"
    default:
      break;
    }
  }
  return false;
}

static mlir::TypedAttr computePadding(CIRGenModule &cgm, CharUnits size) {
  // C mode: zero-initialize padding (Linux kernel relies on this).
  // C++ mode: use undef for padding (matches OGCG CGExprConstant.cpp:45-53).
  bool useZero = !cgm.getLangOpts().CPlusPlus;
  mlir::Type eltTy = cgm.uCharTy;
  clang::CharUnits::QuantityType arSize = size.getQuantity();
  CIRGenBuilderTy &bld = cgm.getBuilder();
  if (size > CharUnits::One()) {
    auto arrTy = cir::ArrayType::get(eltTy, arSize);
    if (useZero) {
      SmallVector<mlir::Attribute> elts(arSize, cir::ZeroAttr::get(eltTy));
      return bld.getConstArray(mlir::ArrayAttr::get(bld.getContext(), elts),
                               arrTy);
    }
    return cir::UndefAttr::get(arrTy);
  }

  return useZero ? mlir::cast<mlir::TypedAttr>(cir::ZeroAttr::get(eltTy))
                 : mlir::cast<mlir::TypedAttr>(cir::UndefAttr::get(eltTy));
}

static mlir::TypedAttr
getConstantArrayFromObjCEncodeExpr(CIRGenModule &cgm, const ObjCEncodeExpr *e,
                                   QualType t) {
  std::string str;
  cgm.getASTContext().getObjCEncodingForType(e->getEncodedType(), str);

  const ConstantArrayType *cat = cgm.getASTContext().getAsConstantArrayType(t);
  assert(cat && "ObjC @encode data not of constant array type");

  uint64_t finalSize = cat->getZExtSize();
  str.resize(finalSize, '\0');
  mlir::Type eltTy = cgm.convertType(cat->getElementType());
  return llvm::cast<mlir::TypedAttr>(
      cgm.getBuilder().getString(str, eltTy, finalSize));
}

static bool shouldSignConstantPointer(PointerAuthenticationMode authMode) {
  return authMode == PointerAuthenticationMode::SignAndStrip ||
         authMode == PointerAuthenticationMode::SignAndAuth;
}

static mlir::Attribute maybeAddGlobalConstantPtrAuthFixup(
    ConstantEmitter &emitter, mlir::Attribute init, QualType destType,
    uint64_t byteOffset) {
  if (auto explicitPtrAuth = mlir::dyn_cast<cir::ConstPtrAuthAttr>(init)) {
    emitter.addGlobalConstantPtrAuthFixup(
        byteOffset, explicitPtrAuth.getKey(),
        explicitPtrAuth.getDiscriminator(),
        explicitPtrAuth.getAddressDiscriminated());
    return explicitPtrAuth.getUnsignedPointer();
  }

  auto &builder = emitter.cgm.getBuilder();

  auto isPointerLikeConstant = [&] {
    return mlir::isa<cir::GlobalViewAttr, cir::ConstPtrAttr>(init);
  };

  if (PointerAuthQualifier pointerAuth = destType.getPointerAuth()) {
    if (pointerAuth.hasKeyNone() ||
        !shouldSignConstantPointer(pointerAuth.getAuthenticationMode()))
      return init;
    if (builder.isNullValue(init) && !pointerAuth.authenticatesNullValues())
      return init;
    if (!builder.isNullValue(init) && !isPointerLikeConstant())
      return init;
    emitter.addGlobalConstantPtrAuthFixup(
        byteOffset, pointerAuth.getKey(), pointerAuth.getExtraDiscriminator(),
        pointerAuth.isAddressDiscriminated());
    return init;
  }

  if (auto authInfo = emitter.cgm.getFunctionPointerAuthInfo(destType)) {
    if (!shouldSignConstantPointer(authInfo->authenticationMode))
      return init;
    if (builder.isNullValue(init) && !authInfo->authenticatesNullValues)
      return init;
    if (!builder.isNullValue(init) && !isPointerLikeConstant())
      return init;
    emitter.addGlobalConstantPtrAuthFixup(
        byteOffset, authInfo->key, authInfo->discriminator,
        /*addressDiscriminated=*/false);
  }

  return init;
}

static mlir::TypedAttr castPointerLikeConstantToType(CIRGenModule &cgm,
                                                     mlir::TypedAttr attr,
                                                     mlir::Type destTy) {
  if (attr.getType() == destTy)
    return attr;

  if (auto gv = mlir::dyn_cast<cir::GlobalViewAttr>(attr))
    return cir::GlobalViewAttr::get(
        destTy, gv.getSymbol(), gv.getIndices(), gv.getRawByteOffset(),
        gv.getInRange(), gv.getInbounds(), gv.getAddrAdjustment(),
        gv.getNoCFI());

  if (auto ptr = mlir::dyn_cast<cir::ConstPtrAttr>(attr))
    return cir::ConstPtrAttr::get(destTy, ptr.getValue());

  cgm.errorNYI("pointer-auth constant cast requires pointer-like constant");
  return {};
}

// Preserve explicit ptrauth sign_constant payload as a typed wrapper until the
// enclosing global initializer can record the final fixup location.
static mlir::TypedAttr
emitConstantPtrAuthUnsignedPointer(ConstantEmitter &emitter, const Expr *e) {
  CIRGenModule &cgm = emitter.cgm;
  Expr::EvalResult result;
  bool succeeded = e->EvaluateAsRValue(result, cgm.getASTContext());
  assert(succeeded);
  (void)succeeded;
  assert(result.Val.isLValue() && "ptrauth constant pointer must be an lvalue");

  return mlir::cast<mlir::TypedAttr>(
      ConstantEmitter(cgm, emitter.cgf)
          .emitAbstractWithoutDestPtrAuth(e->getExprLoc(), result.Val,
                                          e->getType()));
}

static unsigned emitConstantPtrAuthKey(CIRGenModule &cgm, const Expr *e) {
  return e->EvaluateKnownConstInt(cgm.getASTContext()).getZExtValue();
}

static std::pair<bool, uint64_t>
emitConstantPtrAuthDiscriminator(CIRGenModule &cgm, const Expr *e) {
  e = e->IgnoreParens();
  if (const auto *call = dyn_cast<CallExpr>(e)) {
    if (call->getBuiltinCallee() ==
        Builtin::BI__builtin_ptrauth_blend_discriminator) {
      return {
          true,
          call->getArg(1)->EvaluateKnownConstInt(cgm.getASTContext())
              .getZExtValue(),
      };
    }
  }

  if (e->getType()->isPointerType())
    return {true, 0};

  return {false, e->EvaluateKnownConstInt(cgm.getASTContext()).getZExtValue()};
}

static cir::ConstPtrAuthAttr emitConstantPtrAuth(ConstantEmitter &emitter,
                                                 const CallExpr *e) {
  mlir::TypedAttr unsignedPointer =
      emitConstantPtrAuthUnsignedPointer(emitter, e->getArg(0));
  if (!unsignedPointer)
    return {};

  auto [addressDiscriminated, discriminator] =
      emitConstantPtrAuthDiscriminator(emitter.cgm, e->getArg(2));
  return cir::ConstPtrAuthAttr::get(
      mlir::cast<cir::PointerType>(unsignedPointer.getType()), unsignedPointer,
      emitConstantPtrAuthKey(emitter.cgm, e->getArg(1)), discriminator,
      addressDiscriminated);
}

static mlir::Attribute
emitArrayConstant(CIRGenModule &cgm, mlir::Type desiredType,
                  mlir::Type commonElementType, uint64_t arrayBound,
                  SmallVectorImpl<mlir::TypedAttr> &elements,
                  mlir::TypedAttr filler);

struct ConstantAggregateBuilderUtils {
  CIRGenModule &cgm;
  cir::CIRDataLayout dataLayout;

  ConstantAggregateBuilderUtils(CIRGenModule &cgm)
      : cgm(cgm), dataLayout{cgm.getModule()} {}

  CharUnits getAlignment(const mlir::TypedAttr c) const {
    return CharUnits::fromQuantity(
        dataLayout.getAlignment(c.getType(), /*useABIAlign=*/true));
  }

  CharUnits getSize(mlir::Type ty) const {
    return CharUnits::fromQuantity(dataLayout.getTypeAllocSize(ty));
  }

  CharUnits getSize(const mlir::TypedAttr c) const {
    return getSize(c.getType());
  }

  mlir::TypedAttr getPadding(CharUnits size) const {
    return computePadding(cgm, size);
  }

  /// Return an explicit zero constant of the given size. Unlike getPadding(),
  /// this always produces zeros (never undef), matching OGCG's getZeroes().
  /// Use this when splitting a ZeroAttr so both halves stay zero.
  mlir::TypedAttr getZeroes(CharUnits size) const {
    mlir::Type eltTy = cgm.uCharTy;
    clang::CharUnits::QuantityType arSize = size.getQuantity();
    CIRGenBuilderTy &bld = cgm.getBuilder();
    if (size > CharUnits::One()) {
      auto arrTy = cir::ArrayType::get(eltTy, arSize);
      SmallVector<mlir::Attribute> elts(arSize, cir::ZeroAttr::get(eltTy));
      return bld.getConstArray(mlir::ArrayAttr::get(bld.getContext(), elts),
                               arrTy);
    }
    return cir::ZeroAttr::get(eltTy);
  }
};

/// Incremental builder for an mlir::TypedAttr holding a record or array
/// constant.
class ConstantAggregateBuilder : private ConstantAggregateBuilderUtils {
  struct Element {
    Element(mlir::TypedAttr element, CharUnits offset)
        : element(element), offset(offset) {}

    mlir::TypedAttr element;
    /// Describes the offset of `element` within the constant.
    CharUnits offset;
  };
  /// The elements of the constant. The elements are kept in increasing offset
  /// order, and we ensure that there is no overlap:
  /// elements.offset[i+1] >= elements.offset[i] + getSize(elements.element[i])
  ///
  /// This may contain explicit padding elements (in order to create a
  /// natural layout), but need not. Gaps between elements are implicitly
  /// considered to be filled with undef.
  llvm::SmallVector<Element, 32> elements;

  /// The size of the constant (the maximum end offset of any added element).
  /// May be larger than the end of elems.back() if we split the last element
  /// and removed some trailing undefs.
  CharUnits size = CharUnits::Zero();

  /// This is true only if laying out elems in order as the elements of a
  /// non-packed LLVM struct will give the correct layout.
  bool naturalLayout = true;

  bool split(size_t index, CharUnits hint);
  std::optional<size_t> splitAt(CharUnits pos);

  static mlir::Attribute buildFrom(CIRGenModule &cgm, ArrayRef<Element> elems,
                                   CharUnits startOffset, CharUnits size,
                                   bool naturalLayout, mlir::Type desiredTy,
                                   bool allowOversized);

public:
  ConstantAggregateBuilder(CIRGenModule &cgm)
      : ConstantAggregateBuilderUtils(cgm) {}

  /// Update or overwrite the value starting at \p offset with \c c.
  ///
  /// \param allowOverwrite If \c true, this constant might overwrite (part of)
  ///        a constant that has already been added. This flag is only used to
  ///        detect bugs.
  bool add(mlir::TypedAttr typedAttr, CharUnits offset, bool allowOverwrite);

  /// Update or overwrite the bits starting at \p offsetInBits with \p bits.
  bool addBits(llvm::APInt bits, uint64_t offsetInBits, bool allowOverwrite);

  /// Attempt to condense the value starting at \p offset to a constant of type
  /// \p desiredTy.
  void condense(CharUnits offset, mlir::Type desiredTy);

  /// Produce a constant representing the entire accumulated value, ideally of
  /// the specified type. If \p allowOversized, the constant might be larger
  /// than implied by \p desiredTy (eg, if there is a flexible array member).
  /// Otherwise, the constant will be of exactly the same size as \p desiredTy
  /// even if we can't represent it as that type.
  mlir::Attribute build(mlir::Type desiredTy, bool allowOversized) const {
    return buildFrom(cgm, elements, CharUnits::Zero(), size, naturalLayout,
                     desiredTy, allowOversized);
  }
};

template <typename Container, typename Range = std::initializer_list<
                                  typename Container::value_type>>
static void replace(Container &c, size_t beginOff, size_t endOff, Range vals) {
  assert(beginOff <= endOff && "invalid replacement range");
  llvm::replace(c, c.begin() + beginOff, c.begin() + endOff, vals);
}

bool ConstantAggregateBuilder::add(mlir::TypedAttr typedAttr, CharUnits offset,
                                   bool allowOverwrite) {
  // Common case: appending to a layout.
  if (offset >= size) {
    CharUnits align = getAlignment(typedAttr);
    CharUnits alignedSize = size.alignTo(align);
    if (alignedSize > offset || offset.alignTo(align) != offset) {
      naturalLayout = false;
    } else if (alignedSize < offset) {
      elements.emplace_back(getPadding(offset - size), size);
    }
    elements.emplace_back(typedAttr, offset);
    size = offset + getSize(typedAttr);
    return true;
  }

  // Uncommon case: constant overlaps what we've already created.
  std::optional<size_t> firstElemToReplace = splitAt(offset);
  if (!firstElemToReplace)
    return false;

  CharUnits cSize = getSize(typedAttr);
  std::optional<size_t> lastElemToReplace = splitAt(offset + cSize);
  if (!lastElemToReplace)
    return false;

  assert((firstElemToReplace == lastElemToReplace || allowOverwrite) &&
         "unexpectedly overwriting field");

  Element newElt(typedAttr, offset);
  replace(elements, *firstElemToReplace, *lastElemToReplace, {newElt});
  size = std::max(size, offset + cSize);
  naturalLayout = false;
  return true;
}

bool ConstantAggregateBuilder::addBits(llvm::APInt bits, uint64_t offsetInBits,
                                       bool allowOverwrite) {
  const ASTContext &astContext = cgm.getASTContext();
  const uint64_t charWidth = astContext.getCharWidth();
  mlir::Type charTy = cgm.getBuilder().getUIntNTy(charWidth);

  // Offset of where we want the first bit to go within the bits of the
  // current char.
  unsigned offsetWithinChar = offsetInBits % charWidth;

  // We split bit-fields up into individual bytes. Walk over the bytes and
  // update them.
  for (CharUnits offsetInChars =
           astContext.toCharUnitsFromBits(offsetInBits - offsetWithinChar);
       /**/; ++offsetInChars) {
    // Number of bits we want to fill in this char.
    unsigned wantedBits =
        std::min((uint64_t)bits.getBitWidth(), charWidth - offsetWithinChar);

    // Get a char containing the bits we want in the right places. The other
    // bits have unspecified values.
    llvm::APInt bitsThisChar = bits;
    if (bitsThisChar.getBitWidth() < charWidth)
      bitsThisChar = bitsThisChar.zext(charWidth);
    if (cgm.getDataLayout().isBigEndian()) {
      // Figure out how much to shift by. We may need to left-shift if we have
      // less than one byte of Bits left.
      int shift = bits.getBitWidth() - charWidth + offsetWithinChar;
      if (shift > 0)
        bitsThisChar.lshrInPlace(shift);
      else if (shift < 0)
        bitsThisChar = bitsThisChar.shl(-shift);
    } else {
      bitsThisChar = bitsThisChar.shl(offsetWithinChar);
    }
    if (bitsThisChar.getBitWidth() > charWidth)
      bitsThisChar = bitsThisChar.trunc(charWidth);

    if (wantedBits == charWidth) {
      // Got a full byte: just add it directly.
      add(cir::IntAttr::get(charTy, bitsThisChar), offsetInChars,
          allowOverwrite);
    } else {
      // Partial byte: update the existing integer if there is one. If we
      // can't split out a 1-CharUnit range to update, then we can't add
      // these bits and fail the entire constant emission.
      std::optional<size_t> firstElemToUpdate = splitAt(offsetInChars);
      if (!firstElemToUpdate)
        return false;
      std::optional<size_t> lastElemToUpdate =
          splitAt(offsetInChars + CharUnits::One());
      if (!lastElemToUpdate)
        return false;
      assert(*lastElemToUpdate - *firstElemToUpdate < 2 &&
             "should have at most one element covering one byte");

      // Figure out which bits we want and discard the rest.
      llvm::APInt updateMask(charWidth, 0);
      if (cgm.getDataLayout().isBigEndian())
        updateMask.setBits(charWidth - offsetWithinChar - wantedBits,
                           charWidth - offsetWithinChar);
      else
        updateMask.setBits(offsetWithinChar, offsetWithinChar + wantedBits);
      bitsThisChar &= updateMask;
      bool isNullOrUndef = false;
      if (*firstElemToUpdate < elements.size()) {
        auto elt = elements[*firstElemToUpdate].element;
        if (mlir::isa<cir::UndefAttr>(elt) || mlir::isa<cir::ZeroAttr>(elt)) {
          isNullOrUndef = true;
        } else if (auto intAttr = mlir::dyn_cast<cir::IntAttr>(elt)) {
          isNullOrUndef = intAttr.isNullValue();
        }
      }

      if (*firstElemToUpdate == *lastElemToUpdate || isNullOrUndef) {
        // All existing bits are either zero or undef.
        add(cir::IntAttr::get(charTy, bitsThisChar), offsetInChars,
            /*allowOverwrite*/ true);
      } else {
        cir::IntAttr ci =
            mlir::dyn_cast<cir::IntAttr>(elements[*firstElemToUpdate].element);
        // In order to perform a partial update, we need the existing bitwise
        // value, which we can only extract for a constant int.
        if (!ci)
          return false;
        // Because this is a 1-CharUnit range, the constant occupying it must
        // be exactly one CharUnit wide.
        assert(ci.getBitWidth() == charWidth && "splitAt failed");
        assert((!(ci.getValue() & updateMask) || allowOverwrite) &&
               "unexpectedly overwriting bitfield");
        bitsThisChar |= (ci.getValue() & ~updateMask);
        elements[*firstElemToUpdate].element =
            cir::IntAttr::get(charTy, bitsThisChar);
      }
    }

    // Stop if we've added all the bits.
    if (wantedBits == bits.getBitWidth())
      break;

    // Remove the consumed bits from Bits.
    if (!cgm.getDataLayout().isBigEndian())
      bits.lshrInPlace(wantedBits);
    bits = bits.trunc(bits.getBitWidth() - wantedBits);

    // The remaining bits go at the start of the following bytes.
    offsetWithinChar = 0;
  }

  return true;
}

/// Returns a position within elements such that all elements
/// before the returned index end before pos and all elements at or after
/// the returned index begin at or after pos. Splits elements as necessary
/// to ensure this. Returns std::nullopt if we find something we can't split.
std::optional<size_t> ConstantAggregateBuilder::splitAt(CharUnits pos) {
  if (pos >= size)
    return elements.size();

  while (true) {
    // Find the first element that starts after pos.
    Element *iter =
        llvm::upper_bound(elements, pos, [](CharUnits pos, const Element &elt) {
          return pos < elt.offset;
        });

    if (iter == elements.begin())
      return 0;

    size_t index = iter - elements.begin() - 1;
    const Element &elt = elements[index];

    // If we already have an element starting at pos, we're done.
    if (elt.offset == pos)
      return index;

    // Check for overlap with the element that starts before pos.
    CharUnits eltEnd = elt.offset + getSize(elt.element);
    if (eltEnd <= pos)
      return index + 1;

    // Try to decompose it into smaller constants.
    if (!split(index, pos))
      return std::nullopt;
  }
}

/// Split the constant at index, if possible. Return true if we did.
/// Hint indicates the location at which we'd like to split, but may be
/// ignored.
bool ConstantAggregateBuilder::split(size_t index, CharUnits hint) {
  naturalLayout = false;
  mlir::TypedAttr c = elements[index].element;
  CharUnits offset = elements[index].offset;

  // Expand a ConstRecordAttr (struct aggregate) into its members.
  if (auto recordAttr = mlir::dyn_cast<cir::ConstRecordAttr>(c)) {
    auto recordTy = mlir::cast<cir::RecordType>(recordAttr.getType());
    auto memberTypes = recordTy.getMembers();
    auto memberAttrs = recordAttr.getMembers();
    bool packed = recordTy.getPacked();

    // Compute member offsets using the same algorithm as
    // RecordType::computeStructSize.
    SmallVector<Element> newElems;
    uint64_t memberOffset = 0;
    for (unsigned i = 0; i < memberAttrs.size(); ++i) {
      uint64_t tyAlign =
          packed ? 1 : dataLayout.layout.getTypeABIAlignment(memberTypes[i]);
      memberOffset = llvm::alignTo(memberOffset, tyAlign);
      newElems.emplace_back(
          mlir::cast<mlir::TypedAttr>(memberAttrs[i]),
          offset + CharUnits::fromQuantity(memberOffset));
      memberOffset += dataLayout.getTypeAllocSize(memberTypes[i]);
    }
    replace(elements, index, index + 1, newElems);
    return true;
  }

  // Expand a ConstArrayAttr into its individual elements.
  if (auto arrayAttr = mlir::dyn_cast<cir::ConstArrayAttr>(c)) {
    auto arrayTy = mlir::cast<cir::ArrayType>(arrayAttr.getType());
    mlir::Type eltTy = arrayTy.getElementType();
    CharUnits eltSize =
        CharUnits::fromQuantity(dataLayout.getTypeAllocSize(eltTy));

    SmallVector<Element> newElems;
    if (auto elts = mlir::dyn_cast<mlir::ArrayAttr>(arrayAttr.getElts())) {
      for (unsigned i = 0; i < elts.size(); ++i) {
        newElems.emplace_back(mlir::cast<mlir::TypedAttr>(elts[i]),
                              offset + i * eltSize);
      }
      // Trailing zeros from the array.
      for (int64_t i = 0; i < arrayAttr.getTrailingZerosNum(); ++i) {
        newElems.emplace_back(
            mlir::cast<mlir::TypedAttr>(cir::ZeroAttr::get(eltTy)),
            offset + (elts.size() + i) * eltSize);
      }
    } else {
      // String data — expand each byte.
      auto strAttr = mlir::cast<mlir::StringAttr>(arrayAttr.getElts());
      for (unsigned i = 0; i < strAttr.size(); ++i) {
        auto byteVal = llvm::APInt(8, static_cast<uint8_t>(strAttr.data()[i]));
        newElems.emplace_back(cir::IntAttr::get(eltTy, byteVal),
                              offset + i * eltSize);
      }
      for (int64_t i = 0; i < arrayAttr.getTrailingZerosNum(); ++i) {
        newElems.emplace_back(
            mlir::cast<mlir::TypedAttr>(cir::ZeroAttr::get(eltTy)),
            offset + (strAttr.size() + i) * eltSize);
      }
    }
    replace(elements, index, index + 1, newElems);
    return true;
  }

  // Split a ZeroAttr into two zeros at the hinted offset. Use getZeroes()
  // (not getPadding()) so both halves are explicit zeros — getPadding()
  // returns undef in C++ mode, which would incorrectly turn a zero into
  // undef.
  if (mlir::isa<cir::ZeroAttr>(c)) {
    CharUnits elemSize = getSize(c);
    assert(hint > offset && hint < offset + elemSize && "nothing to split");
    SmallVector<Element> newElems;
    newElems.emplace_back(getZeroes(hint - offset), offset);
    newElems.emplace_back(getZeroes(offset + elemSize - hint), hint);
    replace(elements, index, index + 1, newElems);
    return true;
  }

  // Drop UndefAttr; it doesn't contribute to the final layout.
  if (mlir::isa<cir::UndefAttr>(c)) {
    replace(elements, index, index + 1, SmallVector<Element>{});
    return true;
  }

  return false;
}

void ConstantAggregateBuilder::condense(CharUnits offset,
                                        mlir::Type desiredTy) {
  CharUnits desiredSize = getSize(desiredTy);

  std::optional<size_t> firstElemToReplace = splitAt(offset);
  if (!firstElemToReplace)
    return;
  size_t first = *firstElemToReplace;

  std::optional<size_t> lastElemToReplace = splitAt(offset + desiredSize);
  if (!lastElemToReplace)
    return;
  size_t last = *lastElemToReplace;

  size_t length = last - first;
  if (length == 0)
    return;

  if (length == 1 && elements[first].offset == offset &&
      getSize(elements[first].element) == desiredSize) {
    // OGCG re-wraps a single element into a single-element struct when the
    // desired type is a struct with exactly one element whose type matches.
    assert(!cir::MissingFeatures::constAggBuilderCondenseSingleElem());
    llvm_unreachable("NYI");
    return;
  }

  // Build a new constant from the elements in the range.
  SmallVector<Element> subElems(elements.begin() + first,
                                elements.begin() + last);
  mlir::Attribute replacement =
      buildFrom(cgm, subElems, offset, desiredSize,
                /*naturalLayout=*/false, desiredTy, false);
  if (!replacement)
    return;

  // Replace the range with the condensed constant.
  Element newElt(mlir::cast<mlir::TypedAttr>(replacement), offset);
  replace(elements, first, last, {newElt});
}

mlir::Attribute
ConstantAggregateBuilder::buildFrom(CIRGenModule &cgm, ArrayRef<Element> elems,
                                    CharUnits startOffset, CharUnits size,
                                    bool naturalLayout, mlir::Type desiredTy,
                                    bool allowOversized) {
  ConstantAggregateBuilderUtils utils(cgm);

  if (elems.empty())
    return cir::UndefAttr::get(desiredTy);

  // Element offsets are absolute (relative to the enclosing constant), but the
  // constant we are building starts at `startOffset`; make offsets relative to
  // it. Mirrors OGCG CGExprConstant.cpp's `Offset` lambda in buildFrom.
  auto offsetFromStart = [&](size_t i) {
    return elems[i].offset - startOffset;
  };

  // If we want an array type, see if all the elements are the same type and
  // appropriately spaced.
  if (mlir::isa<cir::ArrayType>(desiredTy)) {
    assert(!allowOversized && "oversized array emission not supported");

    auto arrayTy = mlir::cast<cir::ArrayType>(desiredTy);
    mlir::Type eltTy = arrayTy.getElementType();
    CharUnits eltSize = utils.getSize(eltTy);

    bool canBuildArray = (eltSize > CharUnits::Zero());
    mlir::Type commonType = elems[0].element.getType();
    mlir::TypedAttr filler =
        mlir::cast<mlir::TypedAttr>(cir::ZeroAttr::get(commonType));
    SmallVector<mlir::TypedAttr> arrayElts;
    for (size_t i = 0; i != elems.size(); ++i) {
      // Skip zeroes; we'll use a zero value as our array filler.
      if (cgm.getBuilder().isNullValue(elems[i].element))
        continue;

      // All remaining elements must be the same type.
      if (elems[i].element.getType() != commonType ||
          !offsetFromStart(i).isMultipleOf(eltSize)) {
        canBuildArray = false;
        break;
      }
      arrayElts.resize(offsetFromStart(i) / eltSize + 1, filler);
      arrayElts.back() = elems[i].element;
    }

    if (canBuildArray) {
      return emitArrayConstant(cgm, desiredTy, commonType,
                               arrayTy.getSize(), arrayElts, filler);
    }
    // Fall through to struct-based packing if array layout doesn't match.
  }

  // The size of the constant we plan to generate. This is usually just the size
  // of the initialized type, but in AllowOversized mode (i.e. flexible array
  // init), it can be larger.
  CharUnits desiredSize = utils.getSize(desiredTy);
  if (size > desiredSize) {
    assert(allowOversized && "elems are oversized");
    desiredSize = size;
  }

  // The natural alignment of an unpacked CIR record with the given elements.
  CharUnits align = CharUnits::One();
  for (auto [e, offset] : elems)
    align = std::max(align, utils.getAlignment(e));

  // The natural size of an unpacked LLVM struct with the given elements.
  CharUnits alignedSize = size.alignTo(align);

  bool packed = false;
  bool padded = false;

  llvm::SmallVector<mlir::Attribute, 32> unpackedElems;
  if (desiredSize < alignedSize || desiredSize.alignTo(align) != desiredSize) {
    naturalLayout = false;
    packed = true;
  } else {
    // The natural layout would be too small. Add padding to fix it. (This
    // is ignored if we choose a packed layout.)
    unpackedElems.reserve(elems.size() + 1);
    llvm::transform(elems, std::back_inserter(unpackedElems),
                    std::mem_fn(&Element::element));
    if (desiredSize > alignedSize) {
      unpackedElems.push_back(utils.getPadding(desiredSize - size));
      padded = true;
    }
  }

  // If we don't have a natural layout, insert padding as necessary.
  // As we go, double-check to see if we can actually just emit Elems
  // as a non-packed record and do so opportunistically if possible.
  llvm::SmallVector<mlir::Attribute, 32> packedElems;
  packedElems.reserve(elems.size());
  if (!naturalLayout) {
    CharUnits sizeSoFar = CharUnits::Zero();
    for (auto [element, offset] : elems) {
      CharUnits align = utils.getAlignment(element);
      CharUnits naturalOffset = sizeSoFar.alignTo(align);
      CharUnits desiredOffset = offset - startOffset;
      assert(desiredOffset >= sizeSoFar && "elements out of order");

      if (desiredOffset != naturalOffset)
        packed = true;
      if (desiredOffset != sizeSoFar)
        packedElems.push_back(utils.getPadding(desiredOffset - sizeSoFar));
      packedElems.push_back(element);
      sizeSoFar = desiredOffset + utils.getSize(element);
    }
    // If we're using the packed layout, pad it out to the desired size if
    // necessary.
    if (packed) {
      assert(sizeSoFar <= desiredSize &&
             "requested size is too small for contents");

      if (sizeSoFar < desiredSize)
        packedElems.push_back(utils.getPadding(desiredSize - sizeSoFar));
    }
  }

  CIRGenBuilderTy &builder = cgm.getBuilder();
  auto arrAttr = mlir::ArrayAttr::get(builder.getContext(),
                                      packed ? packedElems : unpackedElems);

  cir::RecordType recordType = builder.getCompleteRecordType(arrAttr, packed);
  if (auto desired = mlir::dyn_cast<cir::RecordType>(desiredTy))
    if (desired.isLayoutIdentical(recordType))
      recordType = desired;

  return builder.getConstRecordOrZeroAttr(arrAttr, packed, padded, recordType);
}

//===----------------------------------------------------------------------===//
//                            ConstRecordBuilder
//===----------------------------------------------------------------------===//

class ConstRecordBuilder {
  CIRGenModule &cgm;
  ConstantEmitter &emitter;
  ConstantAggregateBuilder &builder;
  CharUnits startOffset;

public:
  static mlir::Attribute buildRecord(ConstantEmitter &emitter,
                                     InitListExpr *ile, QualType valTy);
  static mlir::Attribute buildRecord(ConstantEmitter &emitter,
                                     const APValue &value, QualType valTy);
  static bool updateRecord(ConstantEmitter &emitter,
                           ConstantAggregateBuilder &constant, CharUnits offset,
                           InitListExpr *updater);

private:
  ConstRecordBuilder(ConstantEmitter &emitter,
                     ConstantAggregateBuilder &builder, CharUnits startOffset)
      : cgm(emitter.cgm), emitter(emitter), builder(builder),
        startOffset(startOffset) {}

  bool appendField(const FieldDecl *field, uint64_t fieldOffset,
                   mlir::TypedAttr initCst, bool allowOverwrite = false);

  bool appendBytes(CharUnits fieldOffsetInChars, mlir::TypedAttr initCst,
                   bool allowOverwrite = false);

  bool appendBitField(const FieldDecl *field, uint64_t fieldOffset,
                      cir::IntAttr ci, bool allowOverwrite = false);

  /// Applies zero-initialization to padding bytes before and within a field.
  /// \param layout The record layout containing field offset information.
  /// \param fieldNo The field index in the record.
  /// \param field The field declaration.
  /// \param allowOverwrite Whether to allow overwriting existing values.
  /// \param sizeSoFar The current size processed, updated by this function.
  /// \param zeroFieldSize Set to true if the field has zero size.
  /// \returns true on success, false if padding could not be applied.
  bool applyZeroInitPadding(const ASTRecordLayout &layout, unsigned fieldNo,
                            const FieldDecl &field, bool allowOverwrite,
                            CharUnits &sizeSoFar, bool &zeroFieldSize);

  /// Applies zero-initialization to trailing padding bytes in a record.
  /// \param layout The record layout containing size information.
  /// \param allowOverwrite Whether to allow overwriting existing values.
  /// \param sizeSoFar The current size processed.
  /// \returns true on success, false if padding could not be applied.
  bool applyZeroInitPadding(const ASTRecordLayout &layout, bool allowOverwrite,
                            CharUnits &sizeSoFar);

  bool build(InitListExpr *ile, bool allowOverwrite);
  bool build(const APValue &val, const RecordDecl *rd, bool isPrimaryBase,
             const CXXRecordDecl *vTableClass, CharUnits baseOffset);

  mlir::Attribute finalize(QualType ty);
};

bool ConstRecordBuilder::appendField(const FieldDecl *field,
                                     uint64_t fieldOffset,
                                     mlir::TypedAttr initCst,
                                     bool allowOverwrite) {
  const ASTContext &astContext = cgm.getASTContext();

  CharUnits fieldOffsetInChars = astContext.toCharUnitsFromBits(fieldOffset);
  initCst = mlir::cast<mlir::TypedAttr>(maybeAddGlobalConstantPtrAuthFixup(
      emitter, initCst, field->getType(),
      (startOffset + fieldOffsetInChars).getQuantity()));

  return appendBytes(fieldOffsetInChars, initCst, allowOverwrite);
}

bool ConstRecordBuilder::appendBytes(CharUnits fieldOffsetInChars,
                                     mlir::TypedAttr initCst,
                                     bool allowOverwrite) {
  return builder.add(initCst, startOffset + fieldOffsetInChars, allowOverwrite);
}

bool ConstRecordBuilder::appendBitField(const FieldDecl *field,
                                        uint64_t fieldOffset, cir::IntAttr ci,
                                        bool allowOverwrite) {
  const CIRGenRecordLayout &rl =
      cgm.getTypes().getCIRGenRecordLayout(field->getParent());
  const CIRGenBitFieldInfo &info = rl.getBitFieldInfo(field);
  llvm::APInt fieldValue = ci.getValue();

  // Promote the size of FieldValue if necessary
  // FIXME: This should never occur, but currently it can because initializer
  // constants are cast to bool, and because clang is not enforcing bitfield
  // width limits.
  if (info.size > fieldValue.getBitWidth())
    fieldValue = fieldValue.zext(info.size);

  // Truncate the size of FieldValue to the bit field size.
  if (info.size < fieldValue.getBitWidth())
    fieldValue = fieldValue.trunc(info.size);

  return builder.addBits(fieldValue,
                         cgm.getASTContext().toBits(startOffset) + fieldOffset,
                         allowOverwrite);
}

bool ConstRecordBuilder::applyZeroInitPadding(
    const ASTRecordLayout &layout, unsigned fieldNo, const FieldDecl &field,
    bool allowOverwrite, CharUnits &sizeSoFar, bool &zeroFieldSize) {
  uint64_t startBitOffset = layout.getFieldOffset(fieldNo);
  CharUnits startOffset =
      cgm.getASTContext().toCharUnitsFromBits(startBitOffset);
  if (sizeSoFar < startOffset) {
    if (!appendBytes(sizeSoFar, computePadding(cgm, startOffset - sizeSoFar),
                     allowOverwrite))
      return false;
  }

  if (!field.isBitField()) {
    CharUnits fieldSize =
        cgm.getASTContext().getTypeSizeInChars(field.getType());
    sizeSoFar = startOffset + fieldSize;
    zeroFieldSize = fieldSize.isZero();
  } else {
    const CIRGenRecordLayout &rl =
        cgm.getTypes().getCIRGenRecordLayout(field.getParent());
    const CIRGenBitFieldInfo &info = rl.getBitFieldInfo(&field);
    uint64_t endBitOffset = startBitOffset + info.size;
    sizeSoFar = cgm.getASTContext().toCharUnitsFromBits(endBitOffset);
    if (endBitOffset % cgm.getASTContext().getCharWidth() != 0)
      sizeSoFar++;
    zeroFieldSize = info.size == 0;
  }
  return true;
}

bool ConstRecordBuilder::applyZeroInitPadding(const ASTRecordLayout &layout,
                                              bool allowOverwrite,
                                              CharUnits &sizeSoFar) {
  CharUnits totalSize = layout.getSize();
  if (sizeSoFar < totalSize) {
    if (!appendBytes(sizeSoFar, computePadding(cgm, totalSize - sizeSoFar),
                     allowOverwrite))
      return false;
  }
  sizeSoFar = totalSize;
  return true;
}

/// In-place port of classic CodeGen's EmitDesignatedInitUpdater
/// (clang/lib/CodeGen/CGExprConstant.cpp): apply the `updater` InitListExpr of
/// a DesignatedInitUpdateExpr on top of the constant bytes already present in
/// `builder` at `offset`. Record subobjects are patched via
/// ConstRecordBuilder::updateRecord (which correctly handles bitfields); array
/// subobjects are patched element by element. NoInitExpr entries mean "keep the
/// existing value".
static bool emitDesignatedInitUpdater(ConstantEmitter &emitter,
                                      ConstantAggregateBuilder &builder,
                                      CharUnits offset, QualType type,
                                      const InitListExpr *updater) {
  CIRGenModule &cgm = emitter.cgm;

  if (type->isRecordType())
    return ConstRecordBuilder::updateRecord(
        emitter, builder, offset, const_cast<InitListExpr *>(updater));

  auto *cat = cgm.getASTContext().getAsConstantArrayType(type);
  if (!cat)
    return false;
  QualType elemType = cat->getElementType();
  CharUnits elemSize = cgm.getASTContext().getTypeSizeInChars(elemType);
  mlir::Type elemTy = cgm.convertType(elemType);

  mlir::Attribute fillC;
  if (const Expr *filler = updater->getArrayFiller()) {
    if (!isa<NoInitExpr>(filler)) {
      fillC = emitter.tryEmitPrivateForMemory(filler, elemType);
      if (!fillC)
        return false;
    }
  }

  unsigned numElementsToUpdate =
      fillC ? cat->getZExtSize() : updater->getNumInits();
  for (unsigned i = 0; i != numElementsToUpdate; ++i, offset += elemSize) {
    const Expr *init = nullptr;
    if (i < updater->getNumInits())
      init = updater->getInit(i);

    if (!init && fillC) {
      if (!builder.add(mlir::cast<mlir::TypedAttr>(fillC), offset,
                       /*allowOverwrite=*/true))
        return false;
    } else if (!init || isa<NoInitExpr>(init)) {
      continue;
    } else if (const auto *childILE = dyn_cast<InitListExpr>(init)) {
      if (!emitDesignatedInitUpdater(emitter, builder, offset, elemType,
                                     childILE))
        return false;
      // Attempt to reduce the array element to a single constant if necessary.
      builder.condense(offset, elemTy);
    } else {
      mlir::Attribute val = emitter.tryEmitPrivateForMemory(init, elemType);
      if (!val)
        return false;
      if (!builder.add(mlir::cast<mlir::TypedAttr>(val), offset,
                       /*allowOverwrite=*/true))
        return false;
    }
  }

  return true;
}

bool ConstRecordBuilder::build(InitListExpr *ile, bool allowOverwrite) {
  RecordDecl *rd = ile->getType()->castAsRecordDecl();
  const ASTRecordLayout &layout = cgm.getASTContext().getASTRecordLayout(rd);

  // Bail out if we have base classes. We could support these, but they only
  // arise in C++1z where we will have already constant folded most interesting
  // cases. FIXME: There are still a few more cases we can handle this way.
  if (auto *cxxrd = dyn_cast<CXXRecordDecl>(rd))
    if (cxxrd->getNumBases())
      return false;

  const bool zeroInitPadding = cgm.shouldZeroInitPadding();
  bool zeroFieldSize = false;
  CharUnits sizeSoFar = CharUnits::Zero();

  unsigned elementNo = 0;
  for (auto [index, field] : llvm::enumerate(rd->fields())) {

    // If this is a union, skip all the fields that aren't being initialized.
    if (rd->isUnion() &&
        !declaresSameEntity(ile->getInitializedFieldInUnion(), field))
      continue;

    // Don't emit anonymous bitfields.
    if (field->isUnnamedBitField())
      continue;

    // Get the initializer.  A record can include fields without initializers,
    // we just use explicit null values for them.
    Expr *init = nullptr;
    if (elementNo < ile->getNumInits())
      init = ile->getInit(elementNo++);
    if (isa_and_nonnull<NoInitExpr>(init)) {
      if (zeroInitPadding &&
          !applyZeroInitPadding(layout, index, *field, allowOverwrite,
                                sizeSoFar, zeroFieldSize))
        return false;
      continue;
    }

    // Zero-sized fields are not emitted, but their initializers may still
    // prevent emission of this record as a constant.
    if (isEmptyFieldForLayout(cgm.getASTContext(), field)) {
      if (init && init->HasSideEffects(cgm.getASTContext()))
        return false;
      continue;
    }

    if (zeroInitPadding &&
        !applyZeroInitPadding(layout, index, *field, allowOverwrite, sizeSoFar,
                              zeroFieldSize))
      return false;

    // When emitting a DesignatedInitUpdateExpr, a nested InitListExpr
    // represents additional overwriting of our current constant value, and not
    // a new constant to emit independently. Recursively merge the update into
    // the field bytes already present in the builder (mirrors classic
    // CodeGen's ConstStructBuilder::Build / EmitDesignatedInitUpdater).
    // Non-InitListExpr inits fall through to the normal emit path below.
    if (allowOverwrite &&
        (field->getType()->isArrayType() || field->getType()->isRecordType())) {
      if (auto *subILE = dyn_cast<InitListExpr>(init)) {
        CharUnits fieldOffsetInChars = cgm.getASTContext().toCharUnitsFromBits(
            layout.getFieldOffset(index));
        if (!emitDesignatedInitUpdater(emitter, builder,
                                       startOffset + fieldOffsetInChars,
                                       field->getType(), subILE))
          return false;
        // If we split apart the field's value, try to collapse it down to a
        // single value now.
        builder.condense(startOffset + fieldOffsetInChars,
                         cgm.convertType(field->getType()));
        continue;
      }
    }

    mlir::Attribute eltInitAttr =
        init ? emitter.tryEmitPrivateForMemory(init, field->getType())
             : emitter.emitNullForMemory(cgm.getLoc(ile->getSourceRange()),
                                         field->getType());
    if (!eltInitAttr)
      return false;

    mlir::TypedAttr eltInit = mlir::cast<mlir::TypedAttr>(eltInitAttr);

    // When a field has zero AST size but the emitted constant has non-zero
    // IR size, adjust SizeSoFar to account for the actual emitted size.
    // This matches OGCG's CGExprConstant.cpp Build(InitListExpr) adjustment.
    if (zeroInitPadding && zeroFieldSize) {
      cir::CIRDataLayout dl{cgm.getModule()};
      sizeSoFar +=
          CharUnits::fromQuantity(dl.getTypeAllocSize(eltInit.getType()));
    }

    if (!field->isBitField()) {
      // Handle non-bitfield members.
      if (!appendField(field, layout.getFieldOffset(index), eltInit,
                       allowOverwrite))
        return false;
      // After emitting a non-empty field with [[no_unique_address]], we may
      // need to overwrite its tail padding.
      if (field->hasAttr<NoUniqueAddressAttr>())
        allowOverwrite = true;
    } else {
      // Otherwise we have a bitfield.
      if (auto constInt = dyn_cast<cir::IntAttr>(eltInit)) {
        if (!appendBitField(field, layout.getFieldOffset(index), constInt,
                            allowOverwrite))
          return false;
      } else if (auto constBool = dyn_cast<cir::BoolAttr>(eltInit)) {
        // _Bool bitfield initializers come through as BoolAttr.
        // Convert to a 1-bit IntAttr for appendBitField.
        auto intTy = cir::IntType::get(cgm.getBuilder().getContext(),
                                       /*width=*/1, /*isSigned=*/false);
        auto intVal = cir::IntAttr::get(
            intTy, llvm::APInt(1, constBool.getValue() ? 1 : 0));
        if (!appendBitField(field, layout.getFieldOffset(index), intVal,
                            allowOverwrite))
          return false;
      } else {
        // OGCG tries to fold non-ConstantInt bitfield values (which arise
        // from large _BitInt types) back into an integer via
        // ConstantFoldLoadFromConst. CIR constant emission for this path
        // already materializes integer constants for bitfield initializers,
        // including long _BitInt values, so this remains a non-constant case.
        return false;
      }
    }
  }

  return !zeroInitPadding ||
         applyZeroInitPadding(layout, allowOverwrite, sizeSoFar);
}

namespace {
struct BaseInfo {
  BaseInfo(const CXXRecordDecl *decl, CharUnits offset, unsigned index)
      : decl(decl), offset(offset), index(index) {}

  const CXXRecordDecl *decl;
  CharUnits offset;
  unsigned index;

  bool operator<(const BaseInfo &o) const { return offset < o.offset; }
};
} // namespace

bool ConstRecordBuilder::build(const APValue &val, const RecordDecl *rd,
                               bool isPrimaryBase,
                               const CXXRecordDecl *vTableClass,
                               CharUnits offset) {
  const ASTRecordLayout &layout = cgm.getASTContext().getASTRecordLayout(rd);
  if (const CXXRecordDecl *cd = dyn_cast<CXXRecordDecl>(rd)) {
    // Add a vtable pointer, if we need one and it hasn't already been added.
    if (layout.hasOwnVFPtr()) {
      CIRGenBuilderTy &builder = cgm.getBuilder();
      cir::GlobalOp vtable =
          cgm.getCXXABI().getAddrOfVTable(vTableClass, CharUnits());
      clang::VTableLayout::AddressPointLocation addressPoint =
          cgm.getItaniumVTableContext()
              .getVTableLayout(vTableClass)
              .getAddressPoint(BaseSubobject(cd, offset));
      mlir::ArrayAttr indices = builder.getArrayAttr({
          builder.getI32IntegerAttr(addressPoint.VTableIndex),
          builder.getI32IntegerAttr(addressPoint.AddressPointIndex),
      });

      // Compute inrange bounds for the vtable GEP, matching OGCG's
      // ItaniumCXXABI::getVTableAddressPoint (ItaniumCXXABI.cpp:2187-2194).
      // The inrange bounds limit pointer arithmetic to the sub-vtable array,
      // enabling better devirtualization optimizations.
      const VTableLayout &vtableLayout =
          cgm.getItaniumVTableContext().getVTableLayout(vTableClass);
      unsigned componentSize =
          cgm.getASTContext()
              .getTypeSizeInChars(cgm.getASTContext().VoidPtrTy)
              .getQuantity();
      unsigned vtableSize =
          componentSize *
          vtableLayout.getVTableSize(addressPoint.VTableIndex);
      unsigned apOffset = componentSize * addressPoint.AddressPointIndex;
      int32_t inRangeStart = -(int32_t)apOffset;
      int32_t inRangeEnd = (int32_t)(vtableSize - apOffset);
      auto inRangeAttr = mlir::DenseI32ArrayAttr::get(
          builder.getContext(), {inRangeStart, inRangeEnd});

      auto symbol = mlir::FlatSymbolRefAttr::get(vtable.getSymNameAttr());
      cir::PointerType ptrTy =
          builder.getPointerTo(vtable.getSymType(),
                               vtable.getAddrSpaceAttr());
      cir::GlobalViewAttr vtableInit = cir::GlobalViewAttr::get(
          ptrTy, symbol, indices, mlir::IntegerAttr{}, inRangeAttr,
          /*inbounds=*/true);
      if (auto auth = cgm.computeVTPointerAuthentication(cd))
        emitter.addGlobalConstantPtrAuthFixup(
            (startOffset + offset).getQuantity(), auth->getKey(),
            auth->getExtraDiscriminator(),
            auth->isAddressDiscriminated());
      if (!appendBytes(offset, vtableInit))
        return false;
    }

    // Accumulate and sort bases, in order to visit them in address order, which
    // may not be the same as declaration order.
    SmallVector<BaseInfo> bases;
    bases.reserve(cd->getNumBases());
    for (auto [index, base] : llvm::enumerate(cd->bases())) {
      assert(!base.isVirtual() && "should not have virtual bases here");
      const CXXRecordDecl *bd = base.getType()->getAsCXXRecordDecl();
      CharUnits baseOffset = layout.getBaseClassOffset(bd);
      bases.push_back(BaseInfo(bd, baseOffset, index));
    }
    llvm::stable_sort(bases);

    for (BaseInfo &base : bases) {
      bool isPrimaryBase = layout.getPrimaryBase() == base.decl;
      if (!build(val.getStructBase(base.index), base.decl, isPrimaryBase,
                 vTableClass, offset + base.offset))
        return false;
    }
  }

  uint64_t offsetBits = cgm.getASTContext().toBits(offset);

  const bool zeroInitPadding = cgm.shouldZeroInitPadding();
  bool zeroFieldSize = false;
  CharUnits sizeSoFar = CharUnits::Zero();

  bool allowOverwrite = false;
  for (auto [index, field] : llvm::enumerate(rd->fields())) {
    // If this is a union, skip all the fields that aren't being initialized.
    if (rd->isUnion() && !declaresSameEntity(val.getUnionField(), field))
      continue;

    // Don't emit anonymous bitfields or zero-sized fields.
    if (field->isUnnamedBitField() ||
        isEmptyFieldForLayout(cgm.getASTContext(), field))
      continue;

    if (zeroInitPadding &&
        !applyZeroInitPadding(layout, index, *field, allowOverwrite, sizeSoFar,
                              zeroFieldSize))
      return false;

    // Emit the value of the initializer.
    const APValue &fieldValue =
        rd->isUnion() ? val.getUnionValue() : val.getStructField(index);
    mlir::TypedAttr eltInit = mlir::dyn_cast_if_present<mlir::TypedAttr>(
        emitter.tryEmitPrivateForMemory(fieldValue, field->getType()));
    if (!eltInit)
      return false;

    // When a field has zero AST size but the emitted constant has non-zero
    // IR size, adjust SizeSoFar to account for the actual emitted size.
    // This matches OGCG's CGExprConstant.cpp Build(APValue) adjustment.
    if (zeroInitPadding && zeroFieldSize) {
      cir::CIRDataLayout dl{cgm.getModule()};
      sizeSoFar +=
          CharUnits::fromQuantity(dl.getTypeAllocSize(eltInit.getType()));
    }

    if (!field->isBitField()) {
      // Handle non-bitfield members.
      if (!appendField(field, layout.getFieldOffset(index) + offsetBits,
                       eltInit, allowOverwrite))
        return false;
      // After emitting a non-empty field with [[no_unique_address]], we may
      // need to overwrite its tail padding.
      if (field->hasAttr<NoUniqueAddressAttr>())
        allowOverwrite = true;
    } else {
      // Otherwise we have a bitfield.
      if (auto constInt = dyn_cast<cir::IntAttr>(eltInit)) {
        if (!appendBitField(field, layout.getFieldOffset(index) + offsetBits,
                            constInt, allowOverwrite))
          return false;
      } else if (auto constBool = dyn_cast<cir::BoolAttr>(eltInit)) {
        // _Bool bitfield initializers come through as BoolAttr.
        // Convert to a 1-bit IntAttr for appendBitField.
        auto intTy = cir::IntType::get(cgm.getBuilder().getContext(),
                                       /*width=*/1, /*isSigned=*/false);
        auto intVal = cir::IntAttr::get(
            intTy, llvm::APInt(1, constBool.getValue() ? 1 : 0));
        if (!appendBitField(field, layout.getFieldOffset(index) + offsetBits,
                            intVal, allowOverwrite))
          return false;
      } else {
        // OGCG tries to fold non-ConstantInt bitfield values (which arise
        // from large _BitInt types) back into an integer via
        // ConstantFoldLoadFromConst. CIR constant emission for this path
        // already materializes integer constants for bitfield initializers,
        // including long _BitInt values, so this remains a non-constant case.
        return false;
      }
    }
  }

  return !zeroInitPadding ||
         applyZeroInitPadding(layout, allowOverwrite, sizeSoFar);
}

mlir::Attribute ConstRecordBuilder::finalize(QualType type) {
  type = type.getNonReferenceType();
  RecordDecl *rd =
      type->castAs<clang::RecordType>()->getDecl()->getDefinitionOrSelf();
  mlir::Type valTy = cgm.convertType(type);
  return builder.build(valTy, rd->hasFlexibleArrayMember());
}

mlir::Attribute ConstRecordBuilder::buildRecord(ConstantEmitter &emitter,
                                                InitListExpr *ile,
                                                QualType valTy) {
  ConstantAggregateBuilder constant(emitter.cgm);
  ConstRecordBuilder builder(emitter, constant, CharUnits::Zero());

  if (!builder.build(ile, /*allowOverwrite*/ false))
    return nullptr;

  return builder.finalize(valTy);
}

mlir::Attribute ConstRecordBuilder::buildRecord(ConstantEmitter &emitter,
                                                const APValue &val,
                                                QualType valTy) {
  ConstantAggregateBuilder constant(emitter.cgm);
  ConstRecordBuilder builder(emitter, constant, CharUnits::Zero());

  const RecordDecl *rd =
      valTy->castAs<clang::RecordType>()->getDecl()->getDefinitionOrSelf();
  const CXXRecordDecl *cd = dyn_cast<CXXRecordDecl>(rd);
  if (!builder.build(val, rd, false, cd, CharUnits::Zero()))
    return nullptr;

  return builder.finalize(valTy);
}

bool ConstRecordBuilder::updateRecord(ConstantEmitter &emitter,
                                      ConstantAggregateBuilder &constant,
                                      CharUnits offset, InitListExpr *updater) {
  return ConstRecordBuilder(emitter, constant, offset)
      .build(updater, /*allowOverwrite*/ true);
}

//===----------------------------------------------------------------------===//
//                             ConstExprEmitter
//===----------------------------------------------------------------------===//

// This class only needs to handle arrays, structs and unions.
//
// In LLVM codegen, when outside C++11 mode, those types are not constant
// folded, while all other types are handled by constant folding.
//
// In CIR codegen, instead of folding things here, we should defer that work
// to MLIR: do not attempt to do much here.
class ConstExprEmitter
    : public StmtVisitor<ConstExprEmitter, mlir::Attribute, QualType> {
  CIRGenModule &cgm;
  [[maybe_unused]] ConstantEmitter &emitter;

  /// Apply a designated init updater to an existing base constant.
  /// This handles the core update logic for DesignatedInitUpdateExpr,
  /// merging the updater's modifications into the base constant.
  /// NoInitExpr entries in the updater mean "keep the original value".
  mlir::Attribute applyDesignatedInitUpdater(mlir::Attribute c,
                                             const InitListExpr *updater,
                                             QualType destType);

public:
  ConstExprEmitter(ConstantEmitter &emitter)
      : cgm(emitter.cgm), emitter(emitter) {}

  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//

  mlir::Attribute VisitStmt(Stmt *s, QualType t) { return {}; }

  mlir::Attribute VisitConstantExpr(ConstantExpr *ce, QualType t) {
    if (mlir::Attribute result = emitter.tryEmitConstantExpr(ce))
      return result;
    return Visit(ce->getSubExpr(), t);
  }

  mlir::Attribute VisitParenExpr(ParenExpr *pe, QualType t) {
    return Visit(pe->getSubExpr(), t);
  }

  mlir::Attribute
  VisitSubstNonTypeTemplateParmExpr(SubstNonTypeTemplateParmExpr *pe,
                                    QualType t) {
    return Visit(pe->getReplacement(), t);
  }

  mlir::Attribute VisitGenericSelectionExpr(GenericSelectionExpr *ge,
                                            QualType t) {
    return Visit(ge->getResultExpr(), t);
  }

  mlir::Attribute VisitChooseExpr(ChooseExpr *ce, QualType t) {
    return Visit(ce->getChosenSubExpr(), t);
  }

  mlir::Attribute VisitCompoundLiteralExpr(CompoundLiteralExpr *e, QualType t) {
    return Visit(e->getInitializer(), t);
  }

  mlir::Attribute VisitCallExpr(CallExpr *e, QualType t) {
    unsigned builtin = e->getBuiltinCallee();
    if (builtin == Builtin::BI__builtin_ptrauth_sign_constant)
      return emitConstantPtrAuth(emitter, e);

    if (builtin != Builtin::BI__builtin___CFStringMakeConstantString &&
        builtin != Builtin::BI__builtin___NSStringMakeConstantString)
      return {};

    const auto *literal = cast<StringLiteral>(e->getArg(0)->IgnoreParenCasts());
    if (builtin == Builtin::BI__builtin___NSStringMakeConstantString)
      return cgm.getAddrOfConstantObjCString(literal);
    return cgm.getAddrOfConstantCFString(literal);
  }

  mlir::Attribute VisitCastExpr(CastExpr *e, QualType destType) {
    if (const auto *ece = dyn_cast<ExplicitCastExpr>(e))
      cgm.emitExplicitCastExprType(ece,
                                   const_cast<CIRGenFunction *>(emitter.cgf));

    Expr *subExpr = e->getSubExpr();

    switch (e->getCastKind()) {
    case CK_ToUnion: {
      // GCC cast to union extension.
      assert(e->getType()->isUnionType() &&
             "Destination type is not union type!");

      auto *field = e->getTargetUnionField();

      mlir::Attribute c =
          emitter.tryEmitPrivateForMemory(subExpr, field->getType());
      if (!c)
        return {};

      // Build the union constant using ConstantAggregateBuilder. The field
      // value goes at offset 0, and the builder handles padding to fill the
      // union's full size.
      mlir::TypedAttr fieldInit = mlir::cast<mlir::TypedAttr>(c);
      ConstantAggregateBuilder aggBuilder(cgm);
      if (!aggBuilder.add(fieldInit, CharUnits::Zero(),
                          /*allowOverwrite=*/false))
        return {};

      RecordDecl *rd =
          destType->castAs<clang::RecordType>()->getDecl()->getDefinitionOrSelf();
      mlir::Type valTy = cgm.convertType(destType);
      return aggBuilder.build(valTy, rd->hasFlexibleArrayMember());
    }

    case CK_AddressSpaceConversion:
      // OGCG implements CK_AddressSpaceConversion via performAddrSpaceCast().
      cgm.errorNYI(e->getBeginLoc(), "ConstExprEmitter::VisitCastExpr");
      return {};

    case CK_ReinterpretMemberPointer: {
      // Under the Itanium ABI, reinterpret casts of member function pointers
      // are no-ops (the {ptr_or_vtable_offset, this_adjustment} representation
      // is the same regardless of the member function type). Emit the
      // subexpression constant (which may fall through to the EvaluateAsRValue
      // path), then re-type the MethodAttr with the destination type.
      mlir::TypedAttr c =
          emitter.tryEmitPrivate(subExpr, subExpr->getType());
      if (!c)
        return {};
      auto methodAttr = mlir::dyn_cast<cir::MethodAttr>(c);
      if (!methodAttr)
        return {};
      auto destMethodTy =
          mlir::cast<cir::MethodType>(cgm.convertType(destType));
      std::optional<uint64_t> destPtrauthDisc;
      if (auto authInfo = cgm.getMemberFunctionPointerAuthInfo(destType))
        destPtrauthDisc = authInfo->discriminator;
      // Reconstruct with the destination type, preserving symbol/vtable_offset.
      if (methodAttr.getSymbol()) {
        if (destPtrauthDisc)
          return cir::MethodAttr::get(destMethodTy, *methodAttr.getSymbol(),
                                      methodAttr.getThisAdjustment(),
                                      *destPtrauthDisc,
                                      methodAttr.getNoCFI());
        return cir::MethodAttr::get(destMethodTy, *methodAttr.getSymbol(),
                                    methodAttr.getThisAdjustment(),
                                    methodAttr.getNoCFI());
      }
      if (methodAttr.getVtableOffset())
        return cir::MethodAttr::get(destMethodTy, *methodAttr.getVtableOffset(),
                                    methodAttr.getThisAdjustment());
      // Null member pointer.
      return cir::MethodAttr::get(destMethodTy);
    }
    case CK_DerivedToBaseMemberPointer:
    case CK_BaseToDerivedMemberPointer: {
      // Constant-fold member function pointer casts by adjusting the
      // this-adjustment field. Matches OGCG's
      // ItaniumCXXABI::EmitMemberPointerConversion.
      // Data member pointers fall through to the Evaluate() path which
      // handles them correctly via APValue.
      if (!destType->isMemberFunctionPointerType())
        return {};

      mlir::TypedAttr c =
          emitter.tryEmitPrivate(subExpr, subExpr->getType());
      if (!c)
        return {};
      auto methodAttr = mlir::dyn_cast<cir::MethodAttr>(c);
      if (!methodAttr)
        return {};

      // Compute the non-virtual base class offset for the cast path.
      QualType derivedTy =
          e->getCastKind() == CK_DerivedToBaseMemberPointer
              ? subExpr->getType()
              : destType;
      const auto *mpType = derivedTy->castAs<MemberPointerType>();
      // Use getMostRecentCXXRecordDecl() to obtain the class from the
      // canonical member-pointer type, matching OGCG's
      // CGCXXABI::getMemberPointerAdjustment.
      const CXXRecordDecl *derivedClass = mpType->getMostRecentCXXRecordDecl();
      CharUnits offset =
          cgm.computeNonVirtualBaseClassOffset(derivedClass, e->path());

      auto destMethodTy =
          mlir::cast<cir::MethodType>(cgm.convertType(destType));
      std::optional<uint64_t> destPtrauthDisc;
      if (auto authInfo = cgm.getMemberFunctionPointerAuthInfo(destType))
        destPtrauthDisc = authInfo->discriminator;

      // Null member pointer: casting null stays null regardless of path.
      if (!methodAttr.getSymbol() && !methodAttr.getVtableOffset())
        return cir::MethodAttr::get(destMethodTy);

      if (offset.isZero()) {
        // Trivial adjustment — just retype.
        if (methodAttr.getVtableOffset())
          return cir::MethodAttr::get(destMethodTy,
                                      *methodAttr.getVtableOffset(),
                                      methodAttr.getThisAdjustment());
        if (destPtrauthDisc)
          return cir::MethodAttr::get(destMethodTy, *methodAttr.getSymbol(),
                                      methodAttr.getThisAdjustment(),
                                      *destPtrauthDisc,
                                      methodAttr.getNoCFI());
        return cir::MethodAttr::get(destMethodTy, *methodAttr.getSymbol(),
                                    methodAttr.getThisAdjustment(),
                                    methodAttr.getNoCFI());
      }

      bool isDerivedToBase =
          (e->getCastKind() == CK_DerivedToBaseMemberPointer);
      int64_t adj = offset.getQuantity();
      int64_t srcAdj = methodAttr.getThisAdjustment();
      int64_t dstAdj = isDerivedToBase ? (srcAdj - adj) : (srcAdj + adj);

      if (methodAttr.getVtableOffset())
        return cir::MethodAttr::get(destMethodTy,
                                    *methodAttr.getVtableOffset(), dstAdj);
      if (destPtrauthDisc)
        return cir::MethodAttr::get(destMethodTy, *methodAttr.getSymbol(),
                                    dstAdj, *destPtrauthDisc,
                                    methodAttr.getNoCFI());
      return cir::MethodAttr::get(destMethodTy, *methodAttr.getSymbol(),
                                  dstAdj, methodAttr.getNoCFI());
    }

    case CK_LValueToRValue: {
      // We don't really support doing lvalue-to-rvalue conversions here; any
      // interesting conversions should be done in Evaluate(). But as a
      // special case, allow compound literals to support the gcc extension
      // allowing "struct x {int x;} x = (struct x) {};".
      if (auto *e = dyn_cast<CompoundLiteralExpr>(subExpr->IgnoreParens()))
        return Visit(e->getInitializer(), destType);
      return {};
    }
    case CK_AtomicToNonAtomic:
    case CK_NonAtomicToAtomic:
    case CK_NoOp:
    case CK_ConstructorConversion:
      return Visit(subExpr, destType);

    case CK_IntToOCLSampler:
      llvm_unreachable("global sampler variables are not generated");

    case CK_Dependent:
      llvm_unreachable("saw dependent cast!");

    case CK_BuiltinFnToFnPtr:
      llvm_unreachable("builtin functions are handled elsewhere");

    // These will never be supported.
    case CK_ObjCObjectLValueCast:
    case CK_ARCProduceObject:
    case CK_ARCConsumeObject:
    case CK_ARCReclaimReturnedObject:
    case CK_ARCExtendBlockObject:
    case CK_CopyAndAutoreleaseBlockObject:
      return {};

    // OGCG handles these cast kinds with actual constant-folding
    // implementations (e.g. ProduceIntToIntCast for CK_IntegralCast,
    // GetAddrOfConstantStringFromLiteral for CK_ArrayToPointerDecay with
    // StringLiteral, EmitNullConstant for CK_NullToPointer). CIR falls back
    // to Evaluate() for all of these.
    case CK_ArrayToPointerDecay:
    case CK_NullToPointer:
    case CK_IntegralCast:
      assert(!cir::MissingFeatures::constExprEmitterIntToIntCast());
      return {};

    // These don't need to be handled here because Evaluate knows how to
    // evaluate them in the cases where they can be folded.
    case CK_BitCast: {
      mlir::Attribute c = Visit(subExpr, subExpr->getType());
      auto ptrauth = mlir::dyn_cast_or_null<cir::ConstPtrAuthAttr>(c);
      if (!ptrauth)
        return {};

      mlir::Type destTy = cgm.convertType(destType);
      if (!mlir::isa<cir::PointerType>(destTy))
        return {};

      mlir::TypedAttr unsignedPointer =
          castPointerLikeConstantToType(cgm, ptrauth.getUnsignedPointer(),
                                        destTy);
      if (!unsignedPointer)
        return {};

      return cir::ConstPtrAuthAttr::get(
          mlir::cast<cir::PointerType>(destTy), unsignedPointer,
          ptrauth.getKey(), ptrauth.getDiscriminator(),
          ptrauth.getAddressDiscriminated());
    }
    case CK_ToVoid:
    case CK_Dynamic:
    case CK_LValueBitCast:
    case CK_LValueToRValueBitCast:
    case CK_NullToMemberPointer:
    case CK_UserDefinedConversion:
    case CK_CPointerToObjCPointerCast:
    case CK_BlockPointerToObjCPointerCast:
    case CK_AnyPointerToBlockPointerCast:
    case CK_FunctionToPointerDecay:
    case CK_BaseToDerived:
    case CK_DerivedToBase:
    case CK_UncheckedDerivedToBase:
    case CK_MemberPointerToBoolean:
    case CK_VectorSplat:
    case CK_FloatingRealToComplex:
    case CK_FloatingComplexToReal:
    case CK_FloatingComplexToBoolean:
    case CK_FloatingComplexCast:
    case CK_FloatingComplexToIntegralComplex:
    case CK_IntegralRealToComplex:
    case CK_IntegralComplexToReal:
    case CK_IntegralComplexToBoolean:
    case CK_IntegralComplexCast:
    case CK_IntegralComplexToFloatingComplex:
    case CK_PointerToIntegral:
    case CK_PointerToBoolean:
    case CK_BooleanToSignedIntegral:
    case CK_IntegralToPointer:
    case CK_IntegralToBoolean:
    case CK_IntegralToFloating:
    case CK_FloatingToIntegral:
    case CK_FloatingToBoolean:
    case CK_FloatingCast:
    case CK_FloatingToFixedPoint:
    case CK_FixedPointToFloating:
    case CK_FixedPointCast:
    case CK_FixedPointToBoolean:
    case CK_FixedPointToIntegral:
    case CK_IntegralToFixedPoint:
    case CK_ZeroToOCLOpaqueType:
    case CK_MatrixCast:
    case CK_HLSLArrayRValue:
    case CK_HLSLVectorTruncation:
    case CK_HLSLMatrixTruncation:
    case CK_HLSLElementwiseCast:
    case CK_HLSLAggregateSplatCast:
      return {};
    }
    llvm_unreachable("Invalid CastKind");
  }

  mlir::Attribute VisitCXXDefaultInitExpr(CXXDefaultInitExpr *die, QualType t) {
    // No need for a DefaultInitExprScope: we don't handle 'this' in a
    // constant expression.
    return Visit(die->getExpr(), t);
  }

  mlir::Attribute VisitExprWithCleanups(ExprWithCleanups *e, QualType t) {
    // Since this about constant emission no need to wrap this under a scope.
    return Visit(e->getSubExpr(), t);
  }

  mlir::Attribute VisitMaterializeTemporaryExpr(MaterializeTemporaryExpr *e,
                                                QualType t) {
    return Visit(e->getSubExpr(), t);
  }

  mlir::Attribute VisitImplicitValueInitExpr(ImplicitValueInitExpr *e,
                                             QualType t) {
    return cgm.getBuilder().getZeroInitAttr(cgm.convertType(t));
  }

  mlir::Attribute VisitInitListExpr(InitListExpr *ile, QualType t) {
    if (ile->isTransparent())
      return Visit(ile->getInit(0), t);

    if (ile->getType()->isArrayType()) {
      auto *cat = cgm.getASTContext().getAsConstantArrayType(ile->getType());
      assert(cat && "can't emit array init for non-constant-bound array");
      const uint64_t numElements = cat->getZExtSize();
      uint64_t numInitElements = ile->getNumInits();

      // Adjust element count for EmbedExpr (C23 #embed). Each EmbedExpr
      // can represent multiple array elements (one per embedded byte).
      for (const auto *init : ile->inits()) {
        if (const auto *embed =
                dyn_cast<EmbedExpr>(init->IgnoreParenImpCasts())) {
          numInitElements += embed->getDataElementCount() - 1;
          if (numInitElements > numElements) {
            numInitElements = numElements;
            break;
          }
        }
      }

      uint64_t numInitableElts =
          std::min<uint64_t>(numInitElements, numElements);
      QualType eltType = cat->getElementType();
      mlir::Type desiredArrayCIRTy = cgm.convertType(ile->getType());

      // Handle array filler for uninitialized trailing elements.
      mlir::TypedAttr filler;
      if (const Expr *fillerExpr = ile->getArrayFiller()) {
        // OGCG uses tryEmitAbstractForMemory here; CIR uses
        // tryEmitPrivateForMemory without abstract wrapping.
        auto fillerAttr =
            emitter.tryEmitPrivateForMemory(fillerExpr, eltType);
        if (!fillerAttr)
          return {};
        filler = mlir::cast<mlir::TypedAttr>(fillerAttr);
      }

      // Emit each initializer element.
      SmallVector<mlir::TypedAttr, 16> elements;
      if (filler && cgm.getBuilder().isNullValue(filler))
        elements.reserve(numInitableElts + 1);
      else
        elements.reserve(numElements);

      mlir::Type commonElementType;
      QualType destTy = cat->getElementType();
      uint64_t elementSize =
          cgm.getASTContext().getTypeSizeInChars(eltType).getQuantity();
      unsigned arrayIndex = 0;
      for (unsigned i = 0; i < ile->getNumInits(); ++i) {
        const Expr *init = ile->getInit(i);
        if (auto *embedS =
                dyn_cast<EmbedExpr>(init->IgnoreParenImpCasts())) {
          // EmbedExpr: emit each embedded byte as a separate array element.
          StringLiteral *sl = embedS->getDataStringLiteral();
          for (unsigned j = embedS->getStartingElementPos(),
                        n = embedS->getDataElementCount();
               j != embedS->getStartingElementPos() + n; ++j) {
            if (arrayIndex >= numElements)
              break;
            uint32_t byteVal = sl->getCodeUnit(j);
            // Create an integer constant for this byte value, widened to the
            // element type (e.g., i32 for int arrays).
            auto eltAttr = emitter.tryEmitPrivateForMemory(
                IntegerLiteral::Create(cgm.getASTContext(),
                                       llvm::APInt(cgm.getASTContext()
                                                       .getTypeSize(destTy),
                                                   byteVal),
                                       destTy, SourceLocation()),
                eltType);
            if (!eltAttr)
              return {};
            mlir::TypedAttr elt = mlir::cast<mlir::TypedAttr>(eltAttr);
            if (arrayIndex == 0)
              commonElementType = elt.getType();
            else if (elt.getType() != commonElementType)
              commonElementType = {};
            elt = mlir::cast<mlir::TypedAttr>(maybeAddGlobalConstantPtrAuthFixup(
                emitter, elt, eltType, arrayIndex * elementSize));
            elements.push_back(elt);
            arrayIndex++;
          }
        } else {
          auto eltAttr = emitter.tryEmitPrivateForMemory(init, eltType);
          if (!eltAttr)
            return {};
          mlir::TypedAttr elt = mlir::cast<mlir::TypedAttr>(eltAttr);
          if (arrayIndex == 0)
            commonElementType = elt.getType();
          else if (elt.getType() != commonElementType)
            commonElementType = {};
          elt = mlir::cast<mlir::TypedAttr>(maybeAddGlobalConstantPtrAuthFixup(
              emitter, elt, eltType, arrayIndex * elementSize));
          elements.push_back(elt);
          arrayIndex++;
        }
      }

      return emitArrayConstant(cgm, desiredArrayCIRTy, commonElementType,
                               numElements, elements, filler);
    }

    if (ile->getType()->isRecordType()) {
      return ConstRecordBuilder::buildRecord(emitter, ile, t);
    }

    if (ile->getType()->isVectorType()) {
      // If we return null here, the non-constant initializer will take care of
      // it, but we would prefer to handle it here.
      assert(!cir::MissingFeatures::constEmitterVectorILE());
      return {};
    }

    return {};
  }

  mlir::Attribute VisitDesignatedInitUpdateExpr(DesignatedInitUpdateExpr *e,
                                                QualType destType) {
    // Evaluate the base expression (the original aggregate constant).
    mlir::Attribute c = Visit(e->getBase(), destType);
    if (!c)
      return {};

    // Apply the updater's modifications to the base constant.
    return applyDesignatedInitUpdater(c, e->getUpdater(), destType);
  }

  mlir::Attribute VisitCXXConstructExpr(CXXConstructExpr *e, QualType ty) {
    if (!e->getConstructor()->isTrivial())
      return {};

    // Only default and copy/move constructors can be trivial.
    if (e->getNumArgs()) {
      assert(e->getNumArgs() == 1 && "trivial ctor with > 1 argument");
      assert(e->getConstructor()->isCopyOrMoveConstructor() &&
             "trivial ctor has argument but isn't a copy/move ctor");

      Expr *arg = e->getArg(0);
      assert(cgm.getASTContext().hasSameUnqualifiedType(ty, arg->getType()) &&
             "argument to copy ctor is of wrong type");

      // Look through the temporary; it's just converting the value to an lvalue
      // to pass it to the constructor.
      if (auto const *mte = dyn_cast<MaterializeTemporaryExpr>(arg))
        return Visit(mte->getSubExpr(), ty);

      // Don't try to support arbitrary lvalue-to-rvalue conversions for now.
      // OGCG has the same limitation (returns nullptr here).
      return {};
    }

    return cgm.getBuilder().getZeroInitAttr(cgm.convertType(ty));
  }

  mlir::Attribute VisitStringLiteral(StringLiteral *e, QualType t) {
    // This is a string literal initializing an array in an initializer.
    return cgm.getConstantArrayFromStringLiteral(e);
  }

  mlir::Attribute VisitObjCEncodeExpr(ObjCEncodeExpr *e, QualType t) {
    // Match OGCG by emitting the encoding bytes inline for array
    // initializers, resizing or truncating to the destination array type.
    return getConstantArrayFromObjCEncodeExpr(cgm, e, t);
  }

  mlir::Attribute VisitUnaryExtension(const UnaryOperator *e, QualType t) {
    return Visit(e->getSubExpr(), t);
  }

  mlir::Attribute VisitPackIndexingExpr(PackIndexingExpr *e, QualType t) {
    return Visit(e->getSelectedExpr(), t);
  }

  // Utility methods
  mlir::Type convertType(QualType t) { return cgm.convertType(t); }
};

mlir::Attribute ConstExprEmitter::applyDesignatedInitUpdater(
    mlir::Attribute c, const InitListExpr *updater, QualType destType) {
  // If the base is a ZeroAttr (opaque zero constant), expand it into a
  // concrete ConstRecordAttr so we can patch individual fields below.
  // For arrays, we handle ZeroAttr directly in the array patching code
  // to avoid O(N) expansion for large arrays.
  if (mlir::isa<cir::ZeroAttr>(c)) {
    auto typedC = mlir::cast<mlir::TypedAttr>(c);
    if (auto recTy = mlir::dyn_cast<cir::RecordType>(typedC.getType())) {
      unsigned n = recTy.getNumElements();
      llvm::SmallVector<mlir::Attribute> elts(n);
      for (unsigned i = 0; i < n; ++i)
        elts[i] = cgm.getBuilder().getZeroInitAttr(recTy.getElementType(i));
      c = cir::ConstRecordAttr::get(
          recTy, mlir::ArrayAttr::get(cgm.getBuilder().getContext(), elts));
    }
    // Arrays with ZeroAttr base are handled below in the array patching
    // section — we avoid expanding here to prevent O(N) memory usage
    // for large sparse arrays like `int arr[1000000] = {{}, .arr[5] = 42}`.
  }

  if (auto constArr = mlir::dyn_cast<cir::ConstArrayAttr>(c)) {
    // Array case: patch individual array elements.
    // The elements may be stored as ArrayAttr (wide strings, integer arrays)
    // or StringAttr (narrow strings). For StringAttr, expand to individual
    // integer elements first so we can patch them.
    llvm::SmallVector<mlir::Attribute> elts;
    auto arrTy = mlir::cast<cir::ArrayType>(constArr.getType());
    if (auto arrAttr = mlir::dyn_cast<mlir::ArrayAttr>(constArr.getElts())) {
      elts.assign(arrAttr.begin(), arrAttr.end());
      // The ArrayAttr may be truncated (trailing zeros omitted by
      // emitArrayConstant). Pad to the full array size so that
      // patching at any valid index is safe.
      if (elts.size() < arrTy.getSize()) {
        auto eltTy = arrTy.getElementType();
        auto zero = cgm.getBuilder().getZeroInitAttr(eltTy);
        elts.resize(arrTy.getSize(), zero);
      }
    } else if (auto strAttr =
                   mlir::dyn_cast<mlir::StringAttr>(constArr.getElts())) {
      // Expand string representation to individual IntAttr elements.
      // Note: getString() truncates trailing zeros, so we must also
      // account for getTrailingZerosNum() to get the full array.
      auto eltTy = arrTy.getElementType();
      llvm::StringRef data = strAttr.getValue();
      uint64_t totalSize = arrTy.getSize();
      elts.reserve(totalSize);
      for (unsigned char ch : data)
        elts.push_back(cir::IntAttr::get(eltTy, ch));
      // Pad with zeros for any trailing zeros that were truncated.
      for (uint64_t i = data.size(); i < totalSize; ++i)
        elts.push_back(cir::IntAttr::get(eltTy, 0));
    } else {
      return {};
    }
    auto *cat = cgm.getASTContext().getAsConstantArrayType(destType);
    QualType elemType = cat->getElementType();

    // Check for an array filler (e.g. ImplicitValueInitExpr for zero-fill).
    // When the updater has a filler, ALL elements beyond getNumInits() must be
    // filled — this handles cases like `.arr = { [1] = 'x' }` where the
    // filler zeroes out elements not explicitly initialized.
    // Matches OGCG CGExprConstant.cpp EmitDesignatedInitUpdater.
    mlir::Attribute fillVal;
    if (const Expr *filler = updater->getArrayFiller()) {
      if (!isa<NoInitExpr>(filler)) {
        fillVal = emitter.tryEmitPrivateForMemory(filler, elemType);
        if (!fillVal)
          return {};
      }
    }
    unsigned numElementsToUpdate =
        fillVal ? arrTy.getSize() : updater->getNumInits();

    for (unsigned i = 0; i < numElementsToUpdate; ++i) {
      const Expr *init = nullptr;
      if (i < updater->getNumInits())
        init = updater->getInit(i);

      if (!init && fillVal) {
        elts[i] = fillVal;
        continue;
      }
      if (!init || isa<NoInitExpr>(init))
        continue;
      // When emitting a DesignatedInitUpdateExpr, a nested InitListExpr
      // represents additional overwriting of our current constant value, and
      // not a new constant to emit independently. Recursively merge the
      // update with the existing element value.
      if (auto *subILE = dyn_cast<InitListExpr>(init)) {
        if (elemType->isArrayType() ||
            (elemType->isRecordType() && !elemType->isUnionType())) {
          mlir::Attribute updated =
              applyDesignatedInitUpdater(elts[i], subILE, elemType);
          if (!updated)
            return {};
          elts[i] = updated;
          continue;
        }
      }
      mlir::Attribute val = emitter.tryEmitPrivateForMemory(init, elemType);
      if (!val)
        return {};
      elts[i] = val;
    }
    // Use emitArrayConstant to handle both homogeneous arrays (ConstArrayAttr)
    // and heterogeneous arrays (packed struct) — e.g., when patching a union
    // array element changes its CIR type (union{int} → union{float}).
    auto expectedEltTy = arrTy.getElementType();
    mlir::Type commonEltTy = expectedEltTy;
    SmallVector<mlir::TypedAttr> typedElts;
    typedElts.reserve(elts.size());
    for (auto &elt : elts) {
      auto typed = mlir::cast<mlir::TypedAttr>(elt);
      typedElts.push_back(typed);
      if (typed.getType() != commonEltTy)
        commonEltTy = {};
    }
    auto filler = cgm.getBuilder().getZeroInitAttr(expectedEltTy);
    return emitArrayConstant(cgm, arrTy, commonEltTy, arrTy.getSize(),
                             typedElts, filler);
  }

  // Handle ZeroAttr base for array types — build a sparse ConstArrayAttr
  // with only the modified prefix, using emitArrayConstant for trailing
  // zeros optimization.
  if (mlir::isa<cir::ZeroAttr>(c)) {
    auto typedC = mlir::cast<mlir::TypedAttr>(c);
    if (auto arrTy = mlir::dyn_cast<cir::ArrayType>(typedC.getType())) {
      unsigned numInits = updater->getNumInits();

      // For large sparse arrays (e.g., {.arr[500000] = 42}), bail out
      // to the APValue evaluator which handles sparse arrays efficiently
      // via its filler mechanism. Element-by-element patching is O(N)
      // and would hang for large arrays.
      if (numInits > 1024)
        return {};

      auto *cat = cgm.getASTContext().getAsConstantArrayType(destType);
      QualType elemType = cat->getElementType();
      uint64_t numElements = arrTy.getSize();
      mlir::Type eltCIRTy = arrTy.getElementType();
      mlir::TypedAttr zeroElt = cgm.getBuilder().getZeroInitAttr(eltCIRTy);

      SmallVector<mlir::TypedAttr, 16> elements;
      elements.reserve(numInits);
      mlir::Type commonElementType;
      for (unsigned i = 0; i < numInits; ++i) {
        const Expr *init = updater->getInit(i);
        if (isa<NoInitExpr>(init)) {
          elements.push_back(zeroElt);
        } else {
          // For nested sub-aggregate InitListExprs, recursively merge.
          if (auto *subILE = dyn_cast<InitListExpr>(init)) {
            if (elemType->isArrayType() ||
                (elemType->isRecordType() && !elemType->isUnionType())) {
              mlir::Attribute updated =
                  applyDesignatedInitUpdater(zeroElt, subILE, elemType);
              if (!updated)
                return {};
              elements.push_back(mlir::cast<mlir::TypedAttr>(updated));
              if (i == 0)
                commonElementType = elements.back().getType();
              else if (elements.back().getType() != commonElementType)
                commonElementType = {};
              continue;
            }
          }
          auto val = emitter.tryEmitPrivateForMemory(init, elemType);
          if (!val)
            return {};
          elements.push_back(mlir::cast<mlir::TypedAttr>(val));
        }
        if (i == 0)
          commonElementType = elements.back().getType();
        else if (elements.back().getType() != commonElementType)
          commonElementType = {};
      }

      return emitArrayConstant(cgm, arrTy, commonElementType, numElements,
                               elements, zeroElt);
    }
  }

  if (auto constRec = mlir::dyn_cast<cir::ConstRecordAttr>(c)) {
    // Record case: patch individual fields.
    auto *rd = destType->getAsRecordDecl();

    // A ConstRecordAttr can appear for non-record types when CIR packs
    // heterogeneous array elements (e.g., unions with different active
    // members) into a packed struct. Treat it as an array and patch elements.
    if (!rd) {
      auto *cat = cgm.getASTContext().getAsConstantArrayType(destType);
      if (!cat)
        return {};
      QualType elemType = cat->getElementType();
      auto members = constRec.getMembers();
      llvm::SmallVector<mlir::Attribute> elts(members.begin(), members.end());
      uint64_t arrayBound = cat->getZExtSize();
      // Pad if needed (packed struct may be shorter than full array).
      if (elts.size() < arrayBound) {
        auto eltCIRTy = cgm.convertType(elemType);
        auto zero = cgm.getBuilder().getZeroInitAttr(eltCIRTy);
        elts.resize(arrayBound, zero);
      }
      // Check for array filler — see ConstArrayAttr case above.
      mlir::Attribute fillValRec;
      if (const Expr *filler = updater->getArrayFiller()) {
        if (!isa<NoInitExpr>(filler)) {
          fillValRec = emitter.tryEmitPrivateForMemory(filler, elemType);
          if (!fillValRec)
            return {};
        }
      }
      unsigned numToUpdateRec =
          fillValRec ? arrayBound : updater->getNumInits();
      for (unsigned i = 0; i < numToUpdateRec; ++i) {
        const Expr *init = nullptr;
        if (i < updater->getNumInits())
          init = updater->getInit(i);

        if (!init && fillValRec) {
          elts[i] = fillValRec;
          continue;
        }
        if (!init || isa<NoInitExpr>(init))
          continue;
        if (auto *subILE = dyn_cast<InitListExpr>(init)) {
          if (elemType->isArrayType() ||
              (elemType->isRecordType() && !elemType->isUnionType())) {
            mlir::Attribute updated =
                applyDesignatedInitUpdater(elts[i], subILE, elemType);
            if (!updated)
              return {};
            elts[i] = updated;
            continue;
          }
        }
        mlir::Attribute val = emitter.tryEmitPrivateForMemory(init, elemType);
        if (!val)
          return {};
        elts[i] = val;
      }
      // Re-emit via emitArrayConstant to handle type heterogeneity.
      auto eltCIRTy = cgm.convertType(elemType);
      SmallVector<mlir::TypedAttr> typedElts;
      typedElts.reserve(elts.size());
      mlir::Type commonEltTy = eltCIRTy;
      for (auto &elt : elts) {
        auto typed = mlir::cast<mlir::TypedAttr>(elt);
        typedElts.push_back(typed);
        if (typed.getType() != commonEltTy)
          commonEltTy = {};
      }
      auto filler = cgm.getBuilder().getZeroInitAttr(eltCIRTy);
      return emitArrayConstant(cgm, cir::ArrayType::get(eltCIRTy, arrayBound),
                               commonEltTy, arrayBound, typedElts, filler);
    }

    // Records with bitfield members need byte-level manipulation because
    // CIR packs bitfields into byte-sized storage elements. Use
    // ConstRecordBuilder::updateRecord which handles bitfield byte packing
    // via ConstantAggregateBuilder::addBits.
    bool hasBitFields = false;
    for (auto *field : rd->fields()) {
      if (field->isBitField()) {
        hasBitFields = true;
        break;
      }
    }
    if (hasBitFields) {
      // Reconstruct a ConstantAggregateBuilder from the base ConstRecordAttr
      // elements, then apply the updater on top.
      ConstantAggregateBuilder constant(cgm);
      auto recTy = mlir::cast<cir::RecordType>(constRec.getType());
      auto members = constRec.getMembers();
      cir::CIRDataLayout dl{cgm.getModule()};
      for (unsigned i = 0; i < recTy.getNumElements(); ++i) {
        CharUnits offset =
            CharUnits::fromQuantity(recTy.getElementOffset(dl.layout, i));
        constant.add(mlir::cast<mlir::TypedAttr>(members[i]), offset, false);
      }
      if (!ConstRecordBuilder::updateRecord(
              emitter, constant, CharUnits::Zero(),
              const_cast<InitListExpr *>(updater)))
        return {};
      mlir::Type cirType = cgm.convertType(destType);
      return constant.build(cirType, /*allowOversized=*/false);
    }

    auto members = constRec.getMembers();
    llvm::SmallVector<mlir::Attribute> elts(members.begin(), members.end());
    const auto &layout = cgm.getTypes().getCIRGenRecordLayout(rd);

    unsigned fieldIdx = 0;
    for (auto *field : rd->fields()) {
      if (fieldIdx >= updater->getNumInits())
        break;
      const Expr *init = updater->getInit(fieldIdx);
      if (!isa<NoInitExpr>(init) &&
          !isEmptyFieldForLayout(cgm.getASTContext(), field)) {
        unsigned cirFieldNo = layout.getCIRFieldNo(field);
        // When emitting a DesignatedInitUpdateExpr, a nested InitListExpr
        // represents additional overwriting of our current constant value,
        // and not a new constant to emit independently. Recursively merge
        // the update with the existing field value.
        if (auto *subILE = dyn_cast<InitListExpr>(init)) {
          if (field->getType()->isArrayType() ||
              (field->getType()->isRecordType() &&
               !field->getType()->isUnionType())) {
            mlir::Attribute updated = applyDesignatedInitUpdater(
                elts[cirFieldNo], subILE, field->getType());
            if (!updated)
              return {};
            elts[cirFieldNo] = updated;
            ++fieldIdx;
            continue;
          }
        }
        mlir::Attribute val =
            emitter.tryEmitPrivateForMemory(init, field->getType());
        if (!val)
          return {};
        elts[cirFieldNo] = val;
      }
      ++fieldIdx;
    }
    auto newMembers =
        mlir::ArrayAttr::get(cgm.getBuilder().getContext(), elts);
    // If any element's type changed (e.g., a union array became a packed
    // struct), create a new anonymous record type to match.
    auto recTy = mlir::cast<cir::RecordType>(constRec.getType());
    bool typeChanged = false;
    for (unsigned i = 0; i < elts.size(); ++i) {
      auto eltTy = mlir::cast<mlir::TypedAttr>(elts[i]).getType();
      if (eltTy != recTy.getElementType(i)) {
        typeChanged = true;
        break;
      }
    }
    if (typeChanged)
      return cgm.getBuilder().getAnonConstRecord(newMembers, recTy.getPacked(),
                                                 recTy.getPadded());
    return cir::ConstRecordAttr::get(recTy, newMembers);
  }

  // Unsupported aggregate type — fall through to APValue path.
  return {};
}

// TODO(cir): this can be shared with LLVM's codegen
static QualType getNonMemoryType(CIRGenModule &cgm, QualType type) {
  if (const auto *at = type->getAs<AtomicType>()) {
    return cgm.getASTContext().getQualifiedType(at->getValueType(),
                                                type.getQualifiers());
  }
  return type;
}

static mlir::Attribute
emitArrayConstant(CIRGenModule &cgm, mlir::Type desiredType,
                  mlir::Type commonElementType, uint64_t arrayBound,
                  SmallVectorImpl<mlir::TypedAttr> &elements,
                  mlir::TypedAttr filler) {
  CIRGenBuilderTy &builder = cgm.getBuilder();
  constexpr uint64_t LargeByteArrayStringThreshold = 1024;

  uint64_t nonzeroLength = arrayBound;
  if (elements.size() < nonzeroLength && builder.isNullValue(filler))
    nonzeroLength = elements.size();

  if (nonzeroLength == elements.size()) {
    while (nonzeroLength > 0 &&
           builder.isNullValue(elements[nonzeroLength - 1]))
      --nonzeroLength;
  }

  if (nonzeroLength == 0)
    return cir::ZeroAttr::get(desiredType);

  const uint64_t trailingZeroes = arrayBound - nonzeroLength;

  // Add a zeroinitializer array filler if we have lots of trailing zeroes.
  if (trailingZeroes >= 8) {
    assert(elements.size() >= nonzeroLength &&
           "missing initializer for non-zero element");

    if (commonElementType) {
      // Truncate to only the nonzero prefix. The ConstArrayAttr builder
      // automatically computes trailingZerosNum from the difference between
      // the array type size and the element count, matching OGCG's
      // EmitArrayConstant which emits zeroinitializer for trailing zeros.
      SmallVector<mlir::Attribute> eles;
      eles.reserve(nonzeroLength);
      for (uint64_t i = 0; i < nonzeroLength; ++i)
        eles.push_back(elements[i]);

      return cir::ConstArrayAttr::get(
          cir::ArrayType::get(commonElementType, arrayBound),
          mlir::ArrayAttr::get(builder.getContext(), eles));
    }
    // Fall through to packed-struct path if commonElementType is null
    // (heterogeneous element types). Must pad elements to full array size.
    elements.resize(arrayBound, filler);
  } else if (elements.size() != arrayBound) {
    elements.resize(arrayBound, filler);

    if (filler.getType() != commonElementType)
      commonElementType = {};
  }

  if (commonElementType) {
    // For large homogeneous byte arrays, use StringAttr-backed
    // ConstArrayAttr instead of an ArrayAttr with one IntAttr per element.
    // This avoids creating huge MLIR attribute lists for large blobs.
    if (arrayBound >= LargeByteArrayStringThreshold) {
      if (auto intTy = mlir::dyn_cast<cir::IntType>(commonElementType);
          intTy && intTy.getWidth() == 8) {
        std::string bytes;
        bytes.reserve(elements.size());
        bool allBytes = true;
        for (const auto &element : elements) {
          if (auto intAttr = mlir::dyn_cast<cir::IntAttr>(element)) {
            bytes.push_back(
                static_cast<char>(intAttr.getValue().getZExtValue()));
            continue;
          }
          if (mlir::isa<cir::ZeroAttr>(element)) {
            bytes.push_back('\0');
            continue;
          }
          allBytes = false;
          break;
        }

        if (allBytes) {
          return cir::ConstArrayAttr::get(
              cir::ArrayType::get(commonElementType, arrayBound),
              cgm.getStringAttrCached(bytes));
        }
      }
    }

    SmallVector<mlir::Attribute> eles;
    eles.reserve(elements.size());

    for (const auto &element : elements)
      eles.push_back(element);

    return cir::ConstArrayAttr::get(
        cir::ArrayType::get(commonElementType, arrayBound),
        mlir::ArrayAttr::get(builder.getContext(), eles));
  }

  SmallVector<mlir::Attribute> eles;
  eles.reserve(elements.size());
  for (auto const &element : elements)
    eles.push_back(element);

  auto arrAttr = mlir::ArrayAttr::get(builder.getContext(), eles);
  return builder.getAnonConstRecord(arrAttr, /*packed=*/true);
}

} // namespace

//===----------------------------------------------------------------------===//
//                          ConstantLValueEmitter
//===----------------------------------------------------------------------===//

namespace {
/// A struct which can be used to peephole certain kinds of finalization
/// that normally happen during l-value emission.
struct ConstantLValue {
  llvm::PointerUnion<mlir::Value, mlir::Attribute> value;
  bool hasOffsetApplied;

  /*implicit*/ ConstantLValue(std::nullptr_t)
      : value(nullptr), hasOffsetApplied(false) {}
  /*implicit*/ ConstantLValue(mlir::Attribute attr)
      : value(attr), hasOffsetApplied(false) {}

  ConstantLValue() : value(nullptr), hasOffsetApplied(false) {}
};

/// A helper class for emitting constant l-values.
class ConstantLValueEmitter
    : public ConstStmtVisitor<ConstantLValueEmitter, ConstantLValue> {
  CIRGenModule &cgm;
  ConstantEmitter &emitter;
  const APValue &value;
  QualType destType;

  // Befriend StmtVisitorBase so that we don't have to expose Visit*.
  friend StmtVisitorBase;

public:
  ConstantLValueEmitter(ConstantEmitter &emitter, const APValue &value,
                        QualType destType)
      : cgm(emitter.cgm), emitter(emitter), value(value), destType(destType) {}

  mlir::Attribute tryEmit();

private:
  mlir::Attribute tryEmitAbsolute(mlir::Type destTy);
  ConstantLValue tryEmitBase(const APValue::LValueBase &base);

  ConstantLValue VisitStmt(const Stmt *s) { return nullptr; }
  ConstantLValue VisitConstantExpr(const ConstantExpr *e);
  ConstantLValue VisitCompoundLiteralExpr(const CompoundLiteralExpr *e);
  ConstantLValue VisitStringLiteral(const StringLiteral *e);
  ConstantLValue VisitObjCBoxedExpr(const ObjCBoxedExpr *e);
  ConstantLValue VisitObjCEncodeExpr(const ObjCEncodeExpr *e);
  ConstantLValue VisitObjCStringLiteral(const ObjCStringLiteral *e);
  ConstantLValue VisitPredefinedExpr(const PredefinedExpr *e);
  ConstantLValue VisitAddrLabelExpr(const AddrLabelExpr *e);
  ConstantLValue VisitCallExpr(const CallExpr *e);
  ConstantLValue VisitBlockExpr(const BlockExpr *e);
  ConstantLValue VisitCXXTypeidExpr(const CXXTypeidExpr *e);
  ConstantLValue
  VisitMaterializeTemporaryExpr(const MaterializeTemporaryExpr *e);
  ConstantLValue emitPointerAuthSignConstant(const CallExpr *e);
  mlir::TypedAttr emitPointerAuthPointer(const Expr *e);
  unsigned emitPointerAuthKey(const Expr *e);
  std::pair<bool, uint64_t> emitPointerAuthDiscriminator(const Expr *e);

  /// Try to decompose a flat byte offset into structured GEP-like indices.
  /// Returns a valid ArrayAttr on success, or null ArrayAttr on failure.
  mlir::ArrayAttr getOffset(mlir::Type ty) {
    int64_t offset = value.getLValueOffset().getQuantity();

    // A *self-referential* global initializer — one that takes the address of
    // a subobject (member or array element) of the very global currently being
    // emitted — must not use structured member indices; it has to fall back to
    // a type-independent raw byte offset.
    //
    // While this global's constant initializer is being built, the constant
    // record builder may still insert *explicit* padding members into the
    // global's record type (e.g. a `[N x i8]` gap before an over-aligned
    // field). That padding is only materialized once the whole initializer is
    // known, which is *after* this address-of-subobject constant is emitted.
    // The member indices computed here are relative to the not-yet-padded
    // type, so each later-inserted padding member shifts the indices of all
    // following members by one, leaving these indices pointing at the wrong
    // member (e.g. at the array that precedes the intended field). That lowers
    // to a wrong byte offset and silently corrupts the constant.
    //
    // A flat byte offset is immune to this member re-indexing. This also
    // matches classic codegen (OGCG), which emits `getelementptr i8, ptr,
    // <offset>` for the address of a member/element. (Non-self-referential
    // views are unaffected: by the time they are emitted the referenced
    // global's type, including any padding, is already final.)
    if (offset != 0 && emitter.initializedVarDecl) {
      const auto *baseVD = dyn_cast_or_null<VarDecl>(
          value.getLValueBase().dyn_cast<const ValueDecl *>());
      if (baseVD && baseVD->getCanonicalDecl() ==
                        emitter.initializedVarDecl->getCanonicalDecl())
        return {}; // Use the raw byte-offset fallback.
    }

    cir::CIRDataLayout layout(cgm.getModule());
    SmallVector<int64_t, 3> idxVec;
    if (!cgm.getBuilder().computeGlobalViewIndicesFromFlatOffset(offset, ty,
                                                                 layout,
                                                                 idxVec))
      return {}; // Decomposition failed — caller should use raw byte offset.

    llvm::SmallVector<mlir::Attribute, 3> indices;
    for (int64_t i : idxVec) {
      mlir::IntegerAttr intAttr = cgm.getBuilder().getI32IntegerAttr(i);
      indices.push_back(intAttr);
    }

    if (indices.empty())
      return {};
    return cgm.getBuilder().getArrayAttr(indices);
  }

  /// Apply the value offset to the given constant.
  ConstantLValue applyOffset(ConstantLValue &c) {
    // Handle attribute constant LValues.
    if (auto attr = mlir::dyn_cast<mlir::Attribute>(c.value)) {
      if (auto gv = mlir::dyn_cast<cir::GlobalViewAttr>(attr)) {
        // Determine the base type for computing GEP-like indices.
        // The GlobalViewAttr's result type may be a pointer to an element
        // type (e.g. !cir.ptr<i8> for a string), but indices must be
        // computed relative to the global's actual storage type (e.g.
        // !cir.array<i8 x 12> for a string literal "hello world").
        mlir::Type baseTy =
            mlir::cast<cir::PointerType>(gv.getType()).getPointee();
        if (auto globalOp = mlir::dyn_cast_or_null<cir::GlobalOp>(
                cgm.getGlobalValue(gv.getSymbol().getValue())))
          baseTy = globalOp.getSymType();
        mlir::Type destTy = cgm.getTypes().convertTypeForMem(destType);
        assert(!gv.getIndices() && "Global view is already indexed");

        // Try structured index decomposition first.
        mlir::ArrayAttr indices = getOffset(baseTy);
        if (indices) {
          return cir::GlobalViewAttr::get(destTy, gv.getSymbol(), indices,
                                          /*inbounds=*/false, gv.getNoCFI());
        }

        // Structured decomposition failed (e.g. offset into tail padding,
        // virtual bases, bitfields). Fall back to a raw byte offset, matching
        // OGCG's approach of emitting `getelementptr i8, ptr, i64 offset`.
        int64_t offset = value.getLValueOffset().getQuantity();
        if (offset == 0) {
          c.hasOffsetApplied = true;
          return c;
        }
        mlir::IntegerAttr rawOffset =
            cgm.getBuilder().getI64IntegerAttr(offset);
        return cir::GlobalViewAttr::get(destTy, gv.getSymbol(),
                                        /*indices=*/{}, rawOffset,
                                        /*inRange=*/mlir::DenseI32ArrayAttr{},
                                        /*inbounds=*/false,
                                        /*addrAdjustment=*/mlir::IntegerAttr{},
                                        gv.getNoCFI());
      }
      // For non-GlobalViewAttr attributes (e.g. BlockAddrInfoAttr), we can
      // only handle the zero-offset case.
      if (value.getLValueOffset().isZero()) {
        c.hasOffsetApplied = true;
        return c;
      }
      llvm_unreachable("Unsupported attribute type to offset");
    }

    cgm.errorNYI("ConstantLValue: non-attribute offset");
    return {};
  }
};

} // namespace

mlir::Attribute ConstantLValueEmitter::tryEmit() {
  const APValue::LValueBase &base = value.getLValueBase();

  // The destination type should be a pointer or reference
  // type, but it might also be a cast thereof.
  //
  // FIXME: the chain of casts required should be reflected in the APValue.
  // We need this in order to correctly handle things like a ptrtoint of a
  // non-zero null pointer and addrspace casts that aren't trivially
  // represented in LLVM IR.
  mlir::Type destTy = cgm.getTypes().convertTypeForMem(destType);
  assert(mlir::isa<cir::PointerType>(destTy) ||
         mlir::isa<cir::IntType>(destTy));

  // If there's no base at all, this is a null or absolute pointer,
  // possibly cast back to an integer type.
  if (!base)
    return tryEmitAbsolute(destTy);

  // Otherwise, try to emit the base.
  ConstantLValue result = tryEmitBase(base);

  // If that failed, we're done.
  llvm::PointerUnion<mlir::Value, mlir::Attribute> &value = result.value;
  if (!value)
    return {};

  // Apply the offset if necessary and not already done.
  if (!result.hasOffsetApplied)
    value = applyOffset(result).value;

  // OGCG checks DestType.getPointerAuth() here and calls
  // tryEmitConstantSignedPointer to sign pointer constants.

  // Convert to the appropriate type; this could be an lvalue for
  // an integer. OGCG performs address space cast via getPointerCast here.
  if (mlir::isa<cir::PointerType>(destTy)) {
    if (auto attr = mlir::dyn_cast<mlir::Attribute>(value)) {
      if (auto ptrauth = mlir::dyn_cast<cir::ConstPtrAuthAttr>(attr)) {
        mlir::TypedAttr unsignedPointer = ptrauth.getUnsignedPointer();
        if (unsignedPointer.getType() != destTy) {
          unsignedPointer =
              castPointerLikeConstantToType(cgm, unsignedPointer, destTy);
          if (!unsignedPointer)
            return {};
        }
        return cir::ConstPtrAuthAttr::get(
            mlir::cast<cir::PointerType>(destTy), unsignedPointer,
            ptrauth.getKey(), ptrauth.getDiscriminator(),
            ptrauth.getAddressDiscriminated());
      }

      // If the attribute's type doesn't match the destination pointer type
      // (e.g., void* array element initialized with double*), re-wrap the
      // GlobalViewAttr with the correct destination type. This ensures array
      // initializers with implicit pointer casts produce uniform element types,
      // matching OGCG's behavior where all pointers are opaque ptr.
      if (auto gv = mlir::dyn_cast<cir::GlobalViewAttr>(attr)) {
        if (gv.getType() != destTy)
          return cir::GlobalViewAttr::get(destTy, gv.getSymbol(),
                                          gv.getIndices(),
                                          gv.getRawByteOffset(),
                                          gv.getInRange(), gv.getInbounds(),
                                          gv.getAddrAdjustment(),
                                          gv.getNoCFI());
      }
      if (auto ptr = mlir::dyn_cast<cir::ConstPtrAttr>(attr)) {
        if (ptr.getType() != destTy)
          return cir::ConstPtrAttr::get(destTy, ptr.getValue());
      }
      return attr;
    }
    cgm.errorNYI("ConstantLValueEmitter: non-attribute pointer");
    return {};
  }

  // For integer destination types, use GlobalViewAttr with integer result type.
  // This represents a constant ptrtoint of the global address.
  if (auto attr = mlir::dyn_cast<mlir::Attribute>(value)) {
    if (mlir::isa<cir::ConstPtrAuthAttr>(attr)) {
      cgm.errorNYI("ConstantLValueEmitter: ptrtoint of ptrauth sign constant");
      return {};
    }

    if (auto gv = mlir::dyn_cast<cir::GlobalViewAttr>(attr)) {
      // If applyOffset already set the integer type, return directly.
      if (mlir::isa<cir::IntType>(gv.getType()))
        return gv;
      // Otherwise, re-wrap with the integer dest type.
      return cir::GlobalViewAttr::get(destTy, gv.getSymbol(),
                                      gv.getIndices(),
                                      gv.getRawByteOffset(),
                                      gv.getInRange(), gv.getInbounds(),
                                      gv.getAddrAdjustment(),
                                      gv.getNoCFI());
    }
    return attr;
  }

  cgm.errorNYI("ConstantLValueEmitter: ptrtoint of non-attribute constant");
  return {};
}

/// Try to emit an absolute l-value, such as a null pointer or an integer
/// bitcast to pointer type.
mlir::Attribute ConstantLValueEmitter::tryEmitAbsolute(mlir::Type destTy) {
  // If we're producing a pointer, this is easy.
  // OGCG calls CGM.getNullPointer() which can delegate to target hooks for
  // custom null pointer representations. CIR keeps the pointer literal here
  // and applies target-specific null materialization in DirectToLLVM lowering.
  if (auto destPtrTy = mlir::dyn_cast<cir::PointerType>(destTy)) {
    if (value.isNullPointer())
      return cgm.getBuilder().getConstNullPtrAttr(destPtrTy);
    return cgm.getBuilder().getConstPtrAttr(
        destPtrTy, value.getLValueOffset().getQuantity());
  }

  // For integer destination types (ptrtoint of null/absolute pointer),
  // emit as an integer constant.
  if (auto intTy = mlir::dyn_cast<cir::IntType>(destTy))
    return cir::IntAttr::get(intTy, value.getLValueOffset().getQuantity());

  cgm.errorNYI("ConstantLValueEmitter::tryEmitAbsolute: unsupported dest type");
  return {};
}

ConstantLValue
ConstantLValueEmitter::tryEmitBase(const APValue::LValueBase &base) {
  // Handle values.
  if (const ValueDecl *d = base.dyn_cast<const ValueDecl *>()) {
    // The constant always points to the canonical declaration. We want to look
    // at properties of the most recent declaration at the point of emission.
    d = cast<ValueDecl>(d->getMostRecentDecl());

    if (d->hasAttr<WeakRefAttr>()) {
      if (isa<FunctionDecl>(d))
        return cgm.getAddrOfFunctionAttr(d);

      // OGCG: return CGM.GetWeakRefReference(D).getPointer();
      // Weakrefs resolve to the aliasee with extern_weak linkage.
      mlir::Operation *weakRef = cgm.getWeakRefReference(d);
      CIRGenBuilderTy &builder = cgm.getBuilder();
      if (auto fop = mlir::dyn_cast<cir::FuncOp>(weakRef))
        return cir::GlobalViewAttr::get(
            builder.getPointerTo(fop.getFunctionType()),
            mlir::FlatSymbolRefAttr::get(builder.getContext(),
                                         fop.getSymNameAttr()));

      auto globalOp = mlir::cast<cir::GlobalOp>(weakRef);
      return builder.getGlobalViewAttr(globalOp);
    }

    if (auto *fd = dyn_cast<FunctionDecl>(d)) {
      // OGCG applies pointer-auth signing (both destination and function-type
      // discrimination) and CFI unchecked callee wrapping here.
      return cgm.getAddrOfFunctionAttr(
          fd, fd->getType()->isCFIUncheckedCalleeFunctionType());
    }

    if (auto *vd = dyn_cast<VarDecl>(d)) {
      // We can never refer to a variable with local storage.
      if (!vd->hasLocalStorage()) {
        if (vd->isFileVarDecl() || vd->hasExternalStorage())
          return cgm.getAddrOfGlobalVarAttr(vd);

        if (vd->isLocalVarDecl()) {
          cir::GlobalOp globalOp = cgm.getOrCreateStaticVarDecl(
              *vd, cgm.getCIRLinkageVarDefinition(vd, /*isConstant=*/false));
          cir::PointerType ptrTy =
              cgm.getBuilder().getPointerTo(globalOp.getSymType());
          return cgm.getBuilder().getGlobalViewAttr(ptrTy, globalOp);
        }
      }
    }

    if (auto *gcd = dyn_cast<UnnamedGlobalConstantDecl>(d)) {
      cir::GlobalOp globalOp = cgm.getAddrOfUnnamedGlobalConstantDecl(gcd);
      cir::PointerType ptrTy =
          cgm.getBuilder().getPointerTo(globalOp.getSymType());
      return cgm.getBuilder().getGlobalViewAttr(ptrTy, globalOp);
    }

    if (auto *tpo = dyn_cast<TemplateParamObjectDecl>(d)) {
      cir::GlobalOp globalOp = cgm.getAddrOfTemplateParamObject(tpo);
      cir::PointerType ptrTy =
          cgm.getBuilder().getPointerTo(globalOp.getSymType());
      return cgm.getBuilder().getGlobalViewAttr(ptrTy, globalOp);
    }

    // Classic codegen handles MSGuidDecl, but it can also fall through from
    // VarDecl, in which case it silently returns nullptr. For now, let's emit
    // an error to see what cases we need to handle.
    cgm.errorNYI(d->getSourceRange(),
                 "ConstantLValueEmitter: unhandled value decl");
    return {};
  }

  // Handle typeid(T).
  if (TypeInfoLValue ti = base.dyn_cast<TypeInfoLValue>()) {
    mlir::Attribute rtti = cgm.getAddrOfRTTIDescriptor(
        mlir::UnknownLoc::get(&cgm.getMLIRContext()),
        QualType(ti.getType(), 0));
    return cast<cir::GlobalViewAttr>(rtti);
  }

  // Otherwise, it must be an expression.
  return Visit(base.get<const Expr *>());
}

ConstantLValue ConstantLValueEmitter::VisitConstantExpr(const ConstantExpr *e) {
  if (mlir::Attribute result = emitter.tryEmitConstantExpr(e))
    return result;
  return Visit(e->getSubExpr());
}

static ConstantLValue
tryEmitGlobalCompoundLiteral(ConstantEmitter &emitter,
                             const CompoundLiteralExpr *e) {
  CIRGenModule &cgm = emitter.cgm;
  CharUnits align = cgm.getASTContext().getTypeAlignInChars(e->getType());

  // Check if we've already emitted a global for this compound literal.
  auto it = cgm.emittedCompoundLiterals.find(e);
  if (it != cgm.emittedCompoundLiterals.end())
    return cgm.getBuilder().getGlobalViewAttr(it->second);

  LangAS addressSpace = e->getType().getAddressSpace();
  mlir::Attribute c = emitter.tryEmitForInitializer(e->getInitializer(),
                                                    addressSpace, e->getType());
  if (!c) {
    assert(!e->isFileScope() &&
           "file-scope compound literal did not have constant initializer!");
    return nullptr;
  }

  // Place the compound literal global in the correct address space,
  // matching OGCG which passes getTargetAddressSpace(addressSpace).
  unsigned targetAS =
      cgm.getASTContext().getTargetAddressSpace(addressSpace);
  mlir::ptr::MemorySpaceAttrInterface addrSpaceAttr;
  if (targetAS)
    addrSpaceAttr = cir::TargetAddressSpaceAttr::get(
        &cgm.getMLIRContext(), targetAS);

  // Use the initializer constant's own type as the global's storage type, not
  // convertTypeForMem(e->getType()). A record initializer carries explicit
  // alignment-padding members (e.g. {i32, [4 x u8], i64}) that the bare
  // converted type ({i32, i64}) lacks; if the global is typed without them, a
  // constant address of a subobject (&(compound_literal).member) computes its
  // GlobalViewAttr member index against the unpadded layout and points at the
  // wrong field. This mirrors the named-global path, which types the global from
  // mlir::cast<TypedAttr>(init).getType().
  auto gv = CIRGenModule::createGlobalOp(
      cgm, cgm.getLoc(e->getSourceRange()),
      cgm.createGlobalCompoundLiteralName(),
      mlir::cast<mlir::TypedAttr>(c).getType(),
      e->getType().isConstantStorage(cgm.getASTContext(), true, false),
      addrSpaceAttr);
  gv.setInitialValueAttr(c);
  gv.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
  gv.setAlignment(align.getAsAlign().value());

  emitter.finalize(gv);

  // Cache the global for future references to this compound literal.
  cgm.emittedCompoundLiterals[e] = gv;

  return cgm.getBuilder().getGlobalViewAttr(gv);
}

ConstantLValue
ConstantLValueEmitter::VisitCompoundLiteralExpr(const CompoundLiteralExpr *e) {
  ConstantEmitter compoundLiteralEmitter(cgm, emitter.cgf);
  compoundLiteralEmitter.setInConstantContext(emitter.isInConstantContext());
  return tryEmitGlobalCompoundLiteral(compoundLiteralEmitter, e);
}

ConstantLValue
ConstantLValueEmitter::VisitStringLiteral(const StringLiteral *e) {
  return cgm.getAddrOfConstantStringFromLiteral(e);
}

ConstantLValue
ConstantLValueEmitter::VisitObjCEncodeExpr(const ObjCEncodeExpr *e) {
  return cgm.getAddrOfConstantStringFromObjCEncode(e);
}

ConstantLValue
ConstantLValueEmitter::VisitObjCStringLiteral(const ObjCStringLiteral *e) {
  return cgm.getAddrOfConstantObjCString(e->getString());
}

ConstantLValue
ConstantLValueEmitter::VisitObjCBoxedExpr(const ObjCBoxedExpr *e) {
  assert(e->isExpressibleAsConstantInitializer() &&
         "this boxed expression can't be emitted as a compile-time constant");
  const auto *literal = cast<StringLiteral>(e->getSubExpr()->IgnoreParenCasts());
  return cgm.getAddrOfConstantCFString(literal);
}

ConstantLValue
ConstantLValueEmitter::VisitPredefinedExpr(const PredefinedExpr *e) {
  return cgm.getAddrOfConstantStringFromLiteral(e->getFunctionName());
}

ConstantLValue
ConstantLValueEmitter::VisitAddrLabelExpr(const AddrLabelExpr *e) {
  assert(emitter.cgf && "Invalid address of label expression outside function");
  auto func = mlir::cast<cir::FuncOp>(emitter.cgf->curFn);
  auto blockAddrLabelName =
      cgm.getBlockAddressLabelName(*e->getLabel());
  cir::BlockAddrInfoAttr blockInfoAttr = cir::BlockAddrInfoAttr::get(
      &cgm.getMLIRContext(), func.getSymName(), blockAddrLabelName);
  mlir::Type ptrTy = cgm.getTypes().convertTypeForMem(e->getType());
  cir::BlockAddrAttr blockAddrAttr =
      cir::BlockAddrAttr::get(&cgm.getMLIRContext(), ptrTy, blockInfoAttr);
  ConstantLValue result;
  result.value = blockAddrAttr;
  result.hasOffsetApplied = true;
  return result;
}

ConstantLValue ConstantLValueEmitter::VisitCallExpr(const CallExpr *e) {
  unsigned builtin = e->getBuiltinCallee();
  if (builtin == Builtin::BI__builtin_ptrauth_sign_constant)
    return emitPointerAuthSignConstant(e);
  if (builtin == Builtin::BI__builtin_function_start) {
    const ValueDecl *decl =
        e->getArg(0)->getAsBuiltinConstantDeclRef(cgm.getASTContext());
    if (!decl || !isa<FunctionDecl>(decl))
      return nullptr;
    return cgm.getAddrOfFunctionAttr(decl, /*noCFI=*/true);
  }

  if (builtin != Builtin::BI__builtin___CFStringMakeConstantString &&
      builtin != Builtin::BI__builtin___NSStringMakeConstantString)
    return nullptr;

  const auto *literal = cast<StringLiteral>(e->getArg(0)->IgnoreParenCasts());
  if (builtin == Builtin::BI__builtin___NSStringMakeConstantString)
    return cgm.getAddrOfConstantObjCString(literal);
  return cgm.getAddrOfConstantCFString(literal);
}

ConstantLValue
ConstantLValueEmitter::emitPointerAuthSignConstant(const CallExpr *e) {
  return emitConstantPtrAuth(emitter, e);
}

mlir::TypedAttr ConstantLValueEmitter::emitPointerAuthPointer(const Expr *e) {
  return emitConstantPtrAuthUnsignedPointer(emitter, e);
}

unsigned ConstantLValueEmitter::emitPointerAuthKey(const Expr *e) {
  return emitConstantPtrAuthKey(cgm, e);
}

std::pair<bool, uint64_t>
ConstantLValueEmitter::emitPointerAuthDiscriminator(const Expr *e) {
  return emitConstantPtrAuthDiscriminator(cgm, e);
}

ConstantLValue ConstantLValueEmitter::VisitBlockExpr(const BlockExpr *e) {
  GlobalDecl gd;
  if (emitter.cgf)
    gd = emitter.cgf->curGD;
  return cgm.getAddrOfGlobalBlockLiteral(cgm.getLoc(e->getSourceRange()), gd,
                                         e, emitter.initializedVarDecl);
}

ConstantLValue
ConstantLValueEmitter::VisitCXXTypeidExpr(const CXXTypeidExpr *e) {
  QualType t;
  if (e->isTypeOperand())
    t = e->getTypeOperand(cgm.getASTContext());
  else
    t = e->getExprOperand()->getType();
  mlir::Attribute rtti =
      cgm.getAddrOfRTTIDescriptor(cgm.getLoc(e->getSourceRange()), t);
  return cast<cir::GlobalViewAttr>(rtti);
}

ConstantLValue ConstantLValueEmitter::VisitMaterializeTemporaryExpr(
    const MaterializeTemporaryExpr *e) {
  assert(e->getStorageDuration() == SD_Static);
  const Expr *inner = e->getSubExpr()->skipRValueSubobjectAdjustments();
  auto *globalOp = cgm.getAddrOfGlobalTemporary(e, inner);
  auto gv = mlir::cast<cir::GlobalOp>(globalOp);
  return cgm.getBuilder().getGlobalViewAttr(gv);
}

//===----------------------------------------------------------------------===//
//                             ConstantEmitter
//===----------------------------------------------------------------------===//

mlir::Attribute ConstantEmitter::tryEmitForInitializer(const VarDecl &d) {
  initializeNonAbstract();
  const VarDecl *oldInitializedVarDecl = initializedVarDecl;
  initializedVarDecl = &d;
  mlir::Attribute init = markIfFailed(tryEmitPrivateForVarInit(d));
  initializedVarDecl = oldInitializedVarDecl;
  if (init)
    init = maybeAddGlobalConstantPtrAuthFixup(*this, init, d.getType(),
                                              /*byteOffset=*/0);
  return init;
}

mlir::Attribute ConstantEmitter::tryEmitForInitializer(const Expr *e,
                                                       LangAS destAddrSpace,
                                                       QualType destType) {
  initializeNonAbstract();
  mlir::Attribute init = markIfFailed(tryEmitPrivateForMemory(e, destType));
  if (init)
    init = maybeAddGlobalConstantPtrAuthFixup(*this, init, destType,
                                              /*byteOffset=*/0);
  return init;
}

mlir::Attribute ConstantEmitter::emitForInitializer(const APValue &value,
                                                    QualType destType) {
  initializeNonAbstract();
  auto c = tryEmitPrivateForMemory(value, destType);
  assert(c && "couldn't emit constant value non-abstractly?");
  c = maybeAddGlobalConstantPtrAuthFixup(*this, c, destType,
                                         /*byteOffset=*/0);
  return c;
}

void ConstantEmitter::finalize(cir::GlobalOp gv) {
  assert(initializedNonAbstract &&
         "finalizing emitter that was used for abstract emission?");
  assert(!finalized && "finalizing emitter multiple times");
  assert(!gv.isDeclaration());
  // OGCG has a full placeholder/self-referential constant mechanism here that
  // replaces placeholder globals with GEP-based addresses into the finalized
  // global's initializer. CIR does not support self-referential constant
  // initializers yet.
  assert(!cir::MissingFeatures::constEmitterPlaceholderSelfRef());
  if (!globalConstantPtrAuthFixups.empty()) {
    mlir::Builder builder(gv.getContext());
    llvm::SmallVector<mlir::Attribute, 4> fixups;
    fixups.reserve(globalConstantPtrAuthFixups.size());
    for (const GlobalConstantPtrAuthFixup &fixup : globalConstantPtrAuthFixups) {
      fixups.push_back(builder.getArrayAttr({
          builder.getI64IntegerAttr(fixup.byteOffset),
          builder.getI32IntegerAttr(fixup.key),
          builder.getI64IntegerAttr(fixup.discriminator),
          builder.getBoolAttr(fixup.addressDiscriminated),
      }));
    }
    gv->setAttr("cir.global_const_ptrauth", builder.getArrayAttr(fixups));
  }
#ifndef NDEBUG
  // Note that we might also be Failed.
  finalized = true;
#endif // NDEBUG
}

mlir::Attribute
ConstantEmitter::tryEmitAbstractForInitializer(const VarDecl &d) {
  AbstractStateRAII state(*this, true);
  const VarDecl *oldInitializedVarDecl = initializedVarDecl;
  initializedVarDecl = &d;
  mlir::Attribute init = tryEmitPrivateForVarInit(d);
  initializedVarDecl = oldInitializedVarDecl;
  return init;
}

ConstantEmitter::~ConstantEmitter() {
  assert((!initializedNonAbstract || finalized || failed) &&
         "not finalized after being initialized for non-abstract emission");
}

static mlir::TypedAttr emitNullConstantForBase(CIRGenModule &cgm,
                                               mlir::Type baseType,
                                               const CXXRecordDecl *baseDecl);

static mlir::TypedAttr emitNullConstant(CIRGenModule &cgm, const RecordDecl *rd,
                                        bool asCompleteObject) {
  const CIRGenRecordLayout &layout = cgm.getTypes().getCIRGenRecordLayout(rd);
  mlir::Type ty = (asCompleteObject ? layout.getCIRType()
                                    : layout.getBaseSubobjectCIRType());
  auto recordTy = mlir::cast<cir::RecordType>(ty);

  unsigned numElements = recordTy.getNumElements();
  SmallVector<mlir::Attribute> elements(numElements);

  auto *cxxrd = dyn_cast<CXXRecordDecl>(rd);
  // Fill in all the bases.
  if (cxxrd) {
    for (const CXXBaseSpecifier &base : cxxrd->bases()) {
      if (base.isVirtual()) {
        // Ignore virtual bases; if we're laying out for a complete
        // object, we'll lay these out later.
        continue;
      }

      const auto *baseDecl = base.getType()->castAsCXXRecordDecl();
      // Ignore empty bases.
      if (isEmptyRecordForLayout(cgm.getASTContext(), base.getType()) ||
          cgm.getASTContext()
              .getASTRecordLayout(baseDecl)
              .getNonVirtualSize()
              .isZero())
        continue;

      unsigned fieldIndex = layout.getNonVirtualBaseCIRFieldNo(baseDecl);
      mlir::Type baseType = recordTy.getElementType(fieldIndex);
      elements[fieldIndex] = emitNullConstantForBase(cgm, baseType, baseDecl);
    }
  }

  // Fill in all the fields.
  for (const FieldDecl *field : rd->fields()) {
    // Fill in non-bitfields. (Bitfields always use a zero pattern, which we
    // will fill in later.)
    if (!field->isBitField() &&
        !isEmptyFieldForLayout(cgm.getASTContext(), field)) {
      unsigned fieldIndex = layout.getCIRFieldNo(field);
      elements[fieldIndex] = cgm.emitNullConstantAttr(field->getType());
    }

    // For unions, stop after the first named field.
    if (rd->isUnion()) {
      if (field->getIdentifier())
        break;
      if (const auto *fieldRD = field->getType()->getAsRecordDecl())
        if (fieldRD->findFirstNamedDataMember())
          break;
    }
  }

  // Fill in the virtual bases, if we're working with the complete object.
  if (cxxrd && asCompleteObject) {
    for (const CXXBaseSpecifier &vbase : cxxrd->vbases()) {
      const auto *baseDecl = vbase.getType()->castAsCXXRecordDecl();
      // Ignore empty bases.
      if (isEmptyRecordForLayout(cgm.getASTContext(), vbase.getType()))
        continue;

      unsigned fieldIndex = layout.getVirtualBaseIndex(baseDecl);

      // We might have already laid this field out.
      if (elements[fieldIndex])
        continue;

      mlir::Type baseType = recordTy.getElementType(fieldIndex);
      elements[fieldIndex] = emitNullConstantForBase(cgm, baseType, baseDecl);
    }
  }

  // Now go through all other fields and zero them out.
  for (unsigned i = 0; i != numElements; ++i) {
    if (!elements[i]) {
      elements[i] =
          cgm.getBuilder().getZeroInitAttr(recordTy.getElementType(i));
    }
  }

  mlir::MLIRContext *mlirContext = recordTy.getContext();
  return cir::ConstRecordAttr::get(recordTy,
                                   mlir::ArrayAttr::get(mlirContext, elements));
}

/// Emit the null constant for a base subobject.
static mlir::TypedAttr emitNullConstantForBase(CIRGenModule &cgm,
                                               mlir::Type baseType,
                                               const CXXRecordDecl *baseDecl) {
  const CIRGenRecordLayout &baseLayout =
      cgm.getTypes().getCIRGenRecordLayout(baseDecl);

  // Just zero out bases that don't have any pointer to data members.
  if (baseLayout.isZeroInitializableAsBase())
    return cgm.getBuilder().getZeroInitAttr(baseType);

  // Otherwise, we can just use its null constant.
  return emitNullConstant(cgm, baseDecl, /*asCompleteObject=*/false);
}

mlir::Attribute ConstantEmitter::tryEmitPrivateForVarInit(const VarDecl &d) {
  // Make a quick check if variable can be default NULL initialized
  // and avoid going through rest of code which may do, for c++11,
  // initialization of memory to all NULLs.
  if (!d.hasLocalStorage()) {
    QualType ty = cgm.getASTContext().getBaseElementType(d.getType());
    if (ty->isRecordType()) {
      if (const auto *e = dyn_cast_or_null<CXXConstructExpr>(d.getInit())) {
        const CXXConstructorDecl *cd = e->getConstructor();
        // FIXME: we should probably model this more closely to C++ than
        // just emitting a global with zero init (mimic what we do for trivial
        // assignments and whatnots). Since this is for globals shouldn't
        // be a problem for the near future.
        if (cd->isTrivial() && cd->isDefaultConstructor())
          return cgm.emitNullConstantAttr(d.getType());
      }
    }
  }
  inConstantContext = d.hasConstantInitialization();

  const Expr *e = d.getInit();
  assert(e && "No initializer to emit");

  QualType destType = d.getType();

  if (!destType->isReferenceType()) {
    QualType nonMemoryDestType = getNonMemoryType(cgm, destType);
    if (mlir::Attribute c = ConstExprEmitter(*this).Visit(const_cast<Expr *>(e),
                                                          nonMemoryDestType))
      return emitForMemory(c, destType);
  }

  // Try to emit the initializer.  Note that this can allow some things that
  // are not allowed by tryEmitPrivateForMemory alone.
  if (APValue *value = d.evaluateValue()) {
    // OGCG asserts: !value->allowConstexprUnknown()
    assert(!value->allowConstexprUnknown() &&
           "constexpr-unknown values should not reach codegen");
    return tryEmitPrivateForMemory(*value, destType);
  }

  return {};
}

mlir::Attribute ConstantEmitter::tryEmitConstantExpr(const ConstantExpr *ce) {
  if (!ce->hasAPValueResult())
    return {};

  QualType retType = ce->getType();
  if (ce->isGLValue())
    retType = cgm.getASTContext().getLValueReferenceType(retType);

  return tryEmitAbstract(ce->getAPValueResult(), retType);
}

mlir::Attribute ConstantEmitter::tryEmitPrivateForMemory(const Expr *e,
                                                         QualType destType) {
  QualType nonMemoryDestType = getNonMemoryType(cgm, destType);
  mlir::TypedAttr c = tryEmitPrivate(e, nonMemoryDestType);
  if (c) {
    mlir::Attribute attr = emitForMemory(c, destType);
    return mlir::cast<mlir::TypedAttr>(attr);
  }
  return nullptr;
}

mlir::Attribute ConstantEmitter::tryEmitPrivateForMemory(const APValue &value,
                                                         QualType destType) {
  QualType nonMemoryDestType = getNonMemoryType(cgm, destType);
  mlir::Attribute c = tryEmitPrivate(value, nonMemoryDestType);
  return (c ? emitForMemory(c, destType) : nullptr);
}

mlir::Attribute ConstantEmitter::tryEmitAbstract(const Expr *e,
                                                 QualType destType) {
  AbstractStateRAII state{*this, true};
  return tryEmitPrivate(e, destType);
}

mlir::Attribute ConstantEmitter::emitAbstract(const Expr *e,
                                              QualType destType) {
  mlir::Attribute c = tryEmitAbstract(e, destType);
  if (!c) {
    cgm.errorNYI(e->getSourceRange(),
                 "emitAbstract failed, emit null constant");
    return cir::UndefAttr::get(cgm.getTypes().convertTypeForMem(destType));
  }
  return c;
}

mlir::Attribute ConstantEmitter::emitAbstract(SourceLocation loc,
                                              const APValue &value,
                                              QualType destType) {
  AbstractStateRAII state(*this, true);
  mlir::Attribute c = tryEmitPrivate(value, destType);
  if (!c) {
    cgm.errorNYI(loc, "emitAbstract failed, emit null constant");
    return cir::UndefAttr::get(cgm.getTypes().convertTypeForMem(destType));
  }
  return c;
}

mlir::Attribute
ConstantEmitter::emitAbstractWithoutDestPtrAuth(SourceLocation loc,
                                                const APValue &value,
                                                QualType destType) {
  // Same as emitAbstract but without applying destination pointer auth.
  // CIR currently does not support pointer auth destination rewrites,
  // so this is equivalent to emitAbstract.
  return emitAbstract(loc, value, destType);
}

mlir::Attribute ConstantEmitter::tryEmitAbstract(const APValue &value,
                                                 QualType destType) {
  AbstractStateRAII state(*this, true);
  return tryEmitPrivate(value, destType);
}

mlir::Attribute ConstantEmitter::emitNullForMemory(mlir::Location loc,
                                                   CIRGenModule &cgm,
                                                   QualType t) {
  mlir::TypedAttr nullAttr = cgm.emitNullConstantAttr(t);
  if (!nullAttr)
    nullAttr = cir::UndefAttr::get(cgm.getTypes().convertTypeForMem(t));
  return emitForMemory(cgm, nullAttr, t);
}

/// Convert a _BitInt IntAttr constant to a byte-array ConstArrayAttr when the
/// type requires a byte-array representation in struct field layout. This
/// matches OGCG's emitForMemory for _BitInt, which splits the integer value
/// into individual bytes stored as [N x i8].
static mlir::Attribute emitBitIntForMemory(CIRGenModule &cgm,
                                           mlir::Attribute c,
                                           QualType destType) {
  if (!cgm.getTypes().typeRequiresSplitIntoByteArray(destType))
    return c;

  auto intAttr = mlir::dyn_cast<cir::IntAttr>(c);
  if (!intAttr)
    return c;

  // Get the byte-array field type.
  mlir::Type fieldTy = cgm.getTypes().convertTypeForMemForField(destType);
  auto arrayTy = mlir::cast<cir::ArrayType>(fieldTy);

  // Sign/zero-extend to the full storage width and split into bytes.
  unsigned storageBits =
      static_cast<unsigned>(cgm.getASTContext().getTypeSize(destType));
  llvm::APInt value = intAttr.getValue();
  if (value.getBitWidth() < storageBits) {
    value = destType->isSignedIntegerOrEnumerationType()
                ? value.sext(storageBits)
                : value.zext(storageBits);
  }

  unsigned numBytes = arrayTy.getSize();
  auto *ctx = &cgm.getMLIRContext();
  auto u8Ty = cir::IntType::get(ctx, 8, /*isSigned=*/false);

  // Extract all bytes, stripping trailing zeros so ConstArrayAttr can
  // represent them compactly via trailingZerosNum.
  SmallVector<mlir::Attribute, 64> bytes;
  bytes.reserve(numBytes);
  for (unsigned i = 0; i < numBytes; ++i) {
    uint8_t byte = value.extractBitsAsZExtValue(8, i * 8);
    bytes.push_back(cir::IntAttr::get(u8Ty, llvm::APInt(8, byte)));
  }
  // Strip trailing zero bytes.
  while (!bytes.empty()) {
    auto lastByte = mlir::cast<cir::IntAttr>(bytes.back());
    if (!lastByte.getValue().isZero())
      break;
    bytes.pop_back();
  }

  auto members = mlir::ArrayAttr::get(ctx, bytes);
  return cir::ConstArrayAttr::get(arrayTy, members);
}

mlir::Attribute ConstantEmitter::emitForMemory(mlir::Attribute c,
                                               QualType destType) {
  // boolZeroExtensionForMemory: CIR uses cir::BoolType abstractly for
  // constants; bool->i8 widening is handled during lowering.

  // Handle _BitInt types that require byte-array memory representation.
  c = emitBitIntForMemory(cgm, c, destType);

  // For an _Atomic-qualified constant, we may need to add tail padding.
  if (auto at = destType->getAs<AtomicType>()) {
    QualType destValueType = at->getValueType();
    c = emitForMemory(c, destValueType);

    uint64_t innerSize = cgm.getASTContext().getTypeSize(destValueType);
    uint64_t outerSize = cgm.getASTContext().getTypeSize(destType);
    if (innerSize == outerSize)
      return c;

    assert(innerSize < outerSize && "emitted over-large constant for atomic");

    // If sizes differ, we need to pad. Create a record with the value and
    // zero-initialized padding.
    auto innerAttr = mlir::cast<mlir::TypedAttr>(c);
    auto innerTy = innerAttr.getType();
    auto paddingSize = (outerSize - innerSize) / 8;
    auto *ctx = &cgm.getMLIRContext();
    auto u8Ty = cir::IntType::get(ctx, 8, /*isSigned=*/false);
    auto paddingArrayTy = cir::ArrayType::get(u8Ty, paddingSize);
    auto paddingAttr = cir::ZeroAttr::get(paddingArrayTy);

    // The struct includes explicit tail padding, so mark it padded.
    auto anonRecordTy = cir::RecordType::get(ctx, {innerTy, paddingArrayTy},
                                             /*packed=*/false, /*padded=*/true,
                                             cir::RecordType::Struct);

    auto members = mlir::ArrayAttr::get(ctx, {innerAttr, paddingAttr});
    return cir::ConstRecordAttr::get(anonRecordTy, members);
  }

  return c;
}

mlir::Attribute ConstantEmitter::emitForMemory(CIRGenModule &cgm,
                                               mlir::Attribute c,
                                               QualType destType) {
  // boolZeroExtensionForMemory: CIR uses cir::BoolType abstractly for
  // constants; bool->i8 widening is handled during lowering.

  // Handle _BitInt types that require byte-array memory representation.
  c = emitBitIntForMemory(cgm, c, destType);
  // For an _Atomic-qualified constant, we may need to add tail padding.
  if (auto at = destType->getAs<AtomicType>()) {
    QualType destValueType = at->getValueType();
    c = emitForMemory(cgm, c, destValueType);

    uint64_t innerSize = cgm.getASTContext().getTypeSize(destValueType);
    uint64_t outerSize = cgm.getASTContext().getTypeSize(destType);
    if (innerSize == outerSize)
      return c;

    assert(innerSize < outerSize && "emitted over-large constant for atomic");

    // If sizes differ, we need to pad. Create a record with the value and
    // zero-initialized padding.
    auto innerAttr = mlir::cast<mlir::TypedAttr>(c);
    auto innerTy = innerAttr.getType();
    auto paddingSize = (outerSize - innerSize) / 8;
    auto *ctx = &cgm.getMLIRContext();
    auto u8Ty = cir::IntType::get(ctx, 8, /*isSigned=*/false);
    auto paddingArrayTy = cir::ArrayType::get(u8Ty, paddingSize);
    auto paddingAttr = cir::ZeroAttr::get(paddingArrayTy);

    // The struct includes explicit tail padding, so mark it padded.
    auto anonRecordTy = cir::RecordType::get(ctx, {innerTy, paddingArrayTy},
                                             /*packed=*/false, /*padded=*/true,
                                             cir::RecordType::Struct);

    auto members = mlir::ArrayAttr::get(ctx, {innerAttr, paddingAttr});
    return cir::ConstRecordAttr::get(anonRecordTy, members);
  }

  return c;
}

mlir::TypedAttr ConstantEmitter::tryEmitPrivate(const Expr *e,
                                                QualType destType) {
  assert(!destType->isVoidType() && "can't emit a void constant");

  if (!destType->isReferenceType())
    if (mlir::Attribute c =
            ConstExprEmitter(*this).Visit(const_cast<Expr *>(e), destType))
      return llvm::dyn_cast<mlir::TypedAttr>(c);

  Expr::EvalResult result;

  bool success = false;

  if (destType->isReferenceType())
    success = e->EvaluateAsLValue(result, cgm.getASTContext());
  else
    success =
        e->EvaluateAsRValue(result, cgm.getASTContext(), inConstantContext);

  if (success && !result.hasSideEffects()) {
    mlir::Attribute c = tryEmitPrivate(result.Val, destType);
    return llvm::dyn_cast<mlir::TypedAttr>(c);
  }

  return nullptr;
}

mlir::Attribute ConstantEmitter::tryEmitPrivate(const APValue &value,
                                                QualType destType) {
  auto &builder = cgm.getBuilder();
  switch (value.getKind()) {
  case APValue::None:
  case APValue::Indeterminate:
    // Out-of-lifetime and indeterminate values can be modeled as 'undef'.
    return cir::UndefAttr::get(cgm.convertType(destType));
  case APValue::Int: {
    // OGCG checks DestType.getPointerAuth() here and returns nullptr for
    // non-zero authenticated pointer values.
      mlir::Type ty = cgm.convertType(destType);
    if (mlir::isa<cir::BoolType>(ty))
      return builder.getCIRBoolAttr(value.getInt().getBoolValue());
    assert(mlir::isa<cir::IntType>(ty) && "expected integral type");
    return cir::IntAttr::get(ty, value.getInt());
  }
  case APValue::Float: {
    const llvm::APFloat &init = value.getFloat();
    if (&init.getSemantics() == &llvm::APFloat::IEEEhalf() &&
        !cgm.getASTContext().getLangOpts().NativeHalfType &&
        cgm.getASTContext().getTargetInfo().useFP16ConversionIntrinsics()) {
      cgm.errorNYI("ConstExprEmitter::tryEmitPrivate half");
      return {};
    }

    mlir::Type ty = cgm.convertType(destType);
    assert(mlir::isa<cir::FPTypeInterface>(ty) &&
           "expected floating-point type");
    return cir::FPAttr::get(ty, init);
  }
  case APValue::Array: {
    const auto *arrayTy = cgm.getASTContext().getAsArrayType(destType);
    assert(arrayTy && "expected array type for APValue::Array");
    const QualType arrayElementTy = arrayTy->getElementType();
    const mlir::Type desiredElementCIRTy = cgm.convertType(arrayElementTy);
    const unsigned numElements = value.getArraySize();
    const unsigned numInitElts = value.getArrayInitializedElts();

    mlir::Attribute filler;
    if (value.hasArrayFiller()) {
      // OGCG uses tryEmitAbstractForMemory here which wraps in abstract state,
      // ensuring no side-effecting operations happen during filler emission.
      // CIR uses tryEmitPrivateForMemory without abstract wrapping.
      filler = tryEmitPrivateForMemory(value.getArrayFiller(),
                                       arrayElementTy);
      if (!filler)
        return {};
    }

    SmallVector<mlir::TypedAttr, 16> elements;
    if (filler && builder.isNullValue(filler))
      elements.reserve(numInitElts + 1);
    else
      elements.reserve(numInitElts);

    mlir::Type commonElementType;
    uint64_t elementSize =
        cgm.getASTContext().getTypeSizeInChars(arrayElementTy).getQuantity();
    for (unsigned i = 0; i < numInitElts; ++i) {
      const APValue &arrayElement = value.getArrayInitializedElt(i);
      const mlir::Attribute element =
          tryEmitPrivateForMemory(arrayElement, arrayElementTy);
      if (!element)
        return {};

      mlir::TypedAttr elementTyped = mlir::cast<mlir::TypedAttr>(element);
      if (i == 0)
        commonElementType = (elementTyped.getType() == desiredElementCIRTy)
                                ? desiredElementCIRTy
                                : elementTyped.getType();
      else if (elementTyped.getType() != commonElementType) {
        commonElementType = {};
      }

      elementTyped = mlir::cast<mlir::TypedAttr>(
          maybeAddGlobalConstantPtrAuthFixup(*this, elementTyped,
                                             arrayElementTy, i * elementSize));
      elements.push_back(elementTyped);
    }

    mlir::TypedAttr typedFiller = llvm::cast_or_null<mlir::TypedAttr>(filler);
    if (filler && !typedFiller) {
      cgm.errorNYI("array filler should always be typed");
      return {};
    }

    mlir::Type desiredType = cgm.convertType(destType);

    // Fix the type of incomplete arrays if the initializer isn't empty.
    if (destType->isIncompleteArrayType() && !elements.empty()) {
      auto arrTy = mlir::cast<cir::ArrayType>(desiredType);
      desiredType = cir::ArrayType::get(arrTy.getElementType(), elements.size());
    }

    return emitArrayConstant(cgm, desiredType, commonElementType, numElements,
                             elements, typedFiller);
  }
  case APValue::Vector: {
    const QualType elementType =
        destType->castAs<VectorType>()->getElementType();
    const unsigned numElements = value.getVectorLength();

    SmallVector<mlir::Attribute, 16> elements;
    elements.reserve(numElements);

    for (unsigned i = 0; i < numElements; ++i) {
      const mlir::Attribute element =
          tryEmitPrivateForMemory(value.getVectorElt(i), elementType);
      if (!element)
        return {};
      elements.push_back(element);
    }

    const auto desiredVecTy =
        mlir::cast<cir::VectorType>(cgm.convertType(destType));

    return cir::ConstVectorAttr::get(
        desiredVecTy,
        mlir::ArrayAttr::get(cgm.getBuilder().getContext(), elements));
  }
  case APValue::MemberPointer: {
    const ValueDecl *memberDecl = value.getMemberPointerDecl();

    // Null member pointer.
    if (!memberDecl) {
      const MemberPointerType *mpt = destType->getAs<MemberPointerType>();
      if (mpt->isMemberFunctionPointerType()) {
        auto ty = mlir::cast<cir::MethodType>(cgm.convertType(destType));
        return builder.getNullMethodAttr(ty);
      }
      auto ty = mlir::cast<cir::DataMemberType>(cgm.convertType(destType));
      return builder.getNullDataMemberAttr(ty);
    }

    // Member function pointers.
    if (const auto *methodDecl = dyn_cast<CXXMethodDecl>(memberDecl)) {
      CharUnits thisAdj = CharUnits::Zero();
      if (!value.getMemberPointerPath().empty())
        thisAdj = cgm.getASTContext().getMemberPointerPathAdjustment(value);
      auto ty = mlir::cast<cir::MethodType>(cgm.convertType(destType));
      return cgm.getCXXABI().emitMemberFunctionPointer(methodDecl, destType, ty,
                                                       thisAdj);
    }

    auto cirTy = mlir::cast<cir::DataMemberType>(cgm.convertType(destType));

    // Member pointer with a conversion path (from CK_BaseToDerivedMemberPointer
    // or CK_DerivedToBaseMemberPointer). Compute the total byte offset like
    // OGCG's ItaniumCXXABI::EmitMemberPointer().
    if (!value.getMemberPointerPath().empty()) {
      CharUnits thisAdjustment =
          cgm.getASTContext().getMemberPointerPathAdjustment(value);
      CharUnits fieldOffset = cgm.getASTContext().toCharUnitsFromBits(
          cgm.getASTContext().getFieldOffset(memberDecl));
      int64_t totalOffsetQuantity =
          (thisAdjustment + fieldOffset).getQuantity();
      assert(totalOffsetQuantity >= 0 &&
             "member pointer offset should be non-negative");
      unsigned totalOffset = static_cast<unsigned>(totalOffsetQuantity);

      return cir::DataMemberAttr::get(cirTy.getContext(), cirTy, std::nullopt,
                                      totalOffset);
    }

    // Simple case: no conversion path.
    // Handle IndirectFieldDecl (anonymous union/struct members) by resolving
    // through the chain to get the first FieldDecl in the parent record.
    const FieldDecl *fieldDecl;
    if (const auto *ifd = dyn_cast<IndirectFieldDecl>(memberDecl))
      fieldDecl = cast<FieldDecl>(ifd->chain().front());
    else
      fieldDecl = cast<FieldDecl>(memberDecl);

    // Empty [[no_unique_address]] fields have no storage in the CIR record
    // type and thus no entry in fieldIdxMap. Use byte offset like OGCG does.
    if (isEmptyFieldForLayout(cgm.getASTContext(), fieldDecl)) {
      CharUnits fieldOffset = cgm.getASTContext().toCharUnitsFromBits(
          cgm.getASTContext().getFieldOffset(fieldDecl));
      return cir::DataMemberAttr::get(cirTy.getContext(), cirTy, std::nullopt,
                                      static_cast<unsigned>(
                                          fieldOffset.getQuantity()));
    }

    // In a union, all members overlap at offset 0. The CIR field-index path
    // can't be used because DataMemberAttr::verify checks that the field type
    // at the given index matches the member type, but all union fields map to
    // index 0 regardless of their type. Use byte offset (always 0) like OGCG.
    if (fieldDecl->getParent()->isUnion()) {
      return cir::DataMemberAttr::get(cirTy.getContext(), cirTy, std::nullopt,
                                      /*offsetBytes=*/0u);
    }

    // Use the CIR record layout to get the correct field index, since the
    // CIR RecordType includes base class subobjects (e.g., for B : A, the
    // CIR layout is {A_base, field...}) while fieldDecl->getFieldIndex()
    // only counts declared fields in the class.
    const CIRGenRecordLayout &layout =
        cgm.getTypes().getCIRGenRecordLayout(fieldDecl->getParent());
    return builder.getDataMemberAttr(cirTy, layout.getCIRFieldNo(fieldDecl));
  }
  case APValue::LValue:
    return ConstantLValueEmitter(*this, value, destType).tryEmit();
  case APValue::Struct:
  case APValue::Union:
    return ConstRecordBuilder::buildRecord(*this, value, destType);
  case APValue::ComplexInt:
  case APValue::ComplexFloat: {
    mlir::Type desiredType = cgm.convertType(destType);
    auto complexType = mlir::cast<cir::ComplexType>(desiredType);

    mlir::Type complexElemTy = complexType.getElementType();
    if (isa<cir::IntType>(complexElemTy)) {
      const llvm::APSInt &real = value.getComplexIntReal();
      const llvm::APSInt &imag = value.getComplexIntImag();
      return cir::ConstComplexAttr::get(builder.getContext(), complexType,
                                        cir::IntAttr::get(complexElemTy, real),
                                        cir::IntAttr::get(complexElemTy, imag));
    }

    assert(isa<cir::FPTypeInterface>(complexElemTy) &&
           "expected floating-point type");
    const llvm::APFloat &real = value.getComplexFloatReal();
    const llvm::APFloat &imag = value.getComplexFloatImag();
    return cir::ConstComplexAttr::get(builder.getContext(), complexType,
                                      cir::FPAttr::get(complexElemTy, real),
                                      cir::FPAttr::get(complexElemTy, imag));
  }
  case APValue::FixedPoint: {
    // Fixed-point types lower to their backing integer (see getFixedPointIntType
    // / VisitFixedPointLiteral in CIRGenExprScalar). Emit the scaled backing
    // integer, mirroring classic CGExprConstant which returns a ConstantInt of
    // value.getFixedPoint().getValue().
    mlir::Type ty = cgm.convertType(destType);
    return cir::IntAttr::get(ty, value.getFixedPoint().getValue());
  }
  case APValue::AddrLabelDiff: {
    assert(cgf && "AddrLabelDiff requires function context");
    auto func = mlir::cast<cir::FuncOp>(cgf->curFn);
    const AddrLabelExpr *lhsExpr = value.getAddrLabelDiffLHS();
    const AddrLabelExpr *rhsExpr = value.getAddrLabelDiffRHS();
    auto lhsLabel = cgm.getBlockAddressLabelName(*lhsExpr->getLabel());
    auto rhsLabel = cgm.getBlockAddressLabelName(*rhsExpr->getLabel());
    cir::BlockAddrInfoAttr lhs =
        cir::BlockAddrInfoAttr::get(builder.getContext(), func.getSymName(),
                                    lhsLabel);
    cir::BlockAddrInfoAttr rhs =
        cir::BlockAddrInfoAttr::get(builder.getContext(), func.getSymName(),
                                    rhsLabel);
    mlir::Type resultType = cgm.convertType(destType);
    return cir::BlockAddrDiffAttr::get(builder.getContext(), resultType, lhs,
                                       rhs);
  }
  }
  llvm_unreachable("Unknown APValue kind");
}

mlir::Value CIRGenModule::emitNullConstant(QualType t, mlir::Location loc) {
  mlir::TypedAttr attr = emitNullConstantAttr(t);
  if (!attr)
    return builder.getConstant(loc,
                               cir::UndefAttr::get(getTypes().convertTypeForMem(t)));
  return builder.getConstant(loc, attr);
}

mlir::TypedAttr CIRGenModule::emitNullConstantAttr(QualType t) {
  if (t->getAs<PointerType>()) {
    // OGCG routes through target hooks for null pointers. CIR emits a pointer
    // null literal here and preserves target-specific materialization
    // semantics in DirectToLLVM lowering.
    return builder.getConstNullPtrAttr(getTypes().convertTypeForMem(t));
  }

  if (isAMDGPUNamedBarrierType(t))
    return cir::UndefAttr::get(getTypes().convertTypeForMem(t));

  if (getTypes().isZeroInitializable(t))
    return builder.getZeroInitAttr(getTypes().convertTypeForMem(t));

  if (const auto *cat = getASTContext().getAsConstantArrayType(t)) {
    QualType elementTy = cat->getElementType();
    mlir::TypedAttr element = emitNullConstantAttr(elementTy);
    if (!element)
      return {};
    uint64_t numElements = cat->getZExtSize();
    auto arrayTy = mlir::cast<cir::ArrayType>(getTypes().convertTypeForMem(t));
    // For null arrays, ZeroAttr is more efficient than materializing all
    // elements (avoids OOM on huge arrays like char[4GB]).
    if (mlir::isa<cir::ZeroAttr>(element))
      return cir::ZeroAttr::get(arrayTy);
    llvm::SmallVector<mlir::Attribute, 8> elements(numElements, element);
    return cir::ConstArrayAttr::get(
        arrayTy, mlir::ArrayAttr::get(builder.getContext(), elements));
  }

  if (const RecordType *rt = t->getAs<RecordType>())
    return ::emitNullConstant(*this, rt->getDecl(), /*asCompleteObject=*/true);

  assert(t->isMemberDataPointerType() &&
         "Should only see pointers to data members here!");

  // OGCG delegates to getCXXABI().EmitNullMemberPointer() which may produce
  // a non-trivial null representation for some ABIs.
  return builder.getNullDataMemberAttr(
      mlir::cast<cir::DataMemberType>(getTypes().convertTypeForMem(t)));
}

mlir::TypedAttr
CIRGenModule::emitNullConstantForBase(const CXXRecordDecl *record) {
  return ::emitNullConstant(*this, record, false);
}
