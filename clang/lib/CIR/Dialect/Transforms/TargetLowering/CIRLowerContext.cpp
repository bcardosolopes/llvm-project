//===- CIRLowerContext.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file partially mimics clang/lib/AST/ASTContext.cpp. The queries are
// adapted to operate on the CIR dialect, however.
//
//===----------------------------------------------------------------------===//

#include "CIRLowerContext.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Basic/AddressSpaces.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <cmath>

namespace cir {

/// Map a CIR pointer type's address space to a clang::LangAS for use with
/// TargetInfo queries like getPointerWidth. For LangAddressSpaceAttr, we use
/// the OpenCL representative (same pointer width as CUDA equivalents). For
/// TargetAddressSpaceAttr, we convert to clang's target address space encoding.
static clang::LangAS getLangASForPointer(PointerType ptrTy) {
  auto addrSpace = ptrTy.getAddrSpace();
  if (!addrSpace)
    return clang::LangAS::Default;

  if (auto langAS = mlir::dyn_cast<LangAddressSpaceAttr>(addrSpace)) {
    switch (langAS.getValue()) {
    case LangAddressSpace::Default:
      return clang::LangAS::Default;
    case LangAddressSpace::OffloadPrivate:
      return clang::LangAS::opencl_private;
    case LangAddressSpace::OffloadLocal:
      return clang::LangAS::opencl_local;
    case LangAddressSpace::OffloadGlobal:
      return clang::LangAS::opencl_global;
    case LangAddressSpace::OffloadConstant:
      return clang::LangAS::opencl_constant;
    case LangAddressSpace::OffloadGeneric:
      return clang::LangAS::opencl_generic;
    }
    llvm_unreachable("unknown CIR lang address space");
  }

  if (auto targetAS = mlir::dyn_cast<TargetAddressSpaceAttr>(addrSpace))
    return clang::getLangASFromTargetAS(targetAS.getValue());

  return clang::LangAS::Default;
}

CIRLowerContext::CIRLowerContext(mlir::ModuleOp module,
                                 clang::LangOptions LOpts,
                                 clang::CodeGenOptions CGOpts)
    : MLIRCtx(module.getContext()), LangOpts(std::move(LOpts)),
      CodeGenOpts(std::move(CGOpts)) {}

CIRLowerContext::~CIRLowerContext() {
  // The context owns every CIRRecordLayout it created and cached, mirroring
  // how clang's ASTContext owns its ASTRecordLayouts. Free them here; the
  // friend relationship grants access to CIRRecordLayout's private destructor.
  for (auto &Entry : CIRRecordLayoutMap)
    delete Entry.second;
}

clang::TypeInfo CIRLowerContext::getTypeInfo(mlir::Type T) const {
  // TODO(cir): Memoize type info.

  clang::TypeInfo TI = getTypeInfoImpl(T);
  return TI;
}

unsigned CIRLowerContext::getTypeUnadjustedAlign(mlir::Type T) const {
  if (const auto RT = mlir::dyn_cast<cir::RecordType>(T)) {
    // Prefer the AST value when available. CIR record types don't fully
    // capture C-level packing: the CIR 'packed' flag reflects structural
    // packing (whether field offsets require a packed LLVM struct), not
    // C-level __attribute__((packed)). A packed struct whose single member
    // is at offset 0 won't have the CIR packed flag set, causing the CIR
    // layout builder to compute the member's natural alignment instead of
    // the packed alignment of 1. Using the AST directly matches OGCG's
    // ASTContext::getTypeUnadjustedAlign which correctly respects packing.
    if (auto ast = RT.getAst()) {
      const clang::RecordDecl *RD = ast.getRawDecl();
      if (const auto *Def = RD->getDefinition()) {
        const clang::ASTRecordLayout &ASTLayout =
            Def->getASTContext().getASTRecordLayout(Def);
        return toBits(ASTLayout.getUnadjustedAlignment());
      }
    }
    // Fallback to CIR layout when AST info is not available.
    if (RT.isComplete()) {
      const CIRRecordLayout &Layout = getCIRRecordLayout(RT);
      return toBits(Layout.getUnadjustedAlignment());
    }
  }
  // For non-record types, unadjusted align is the same as regular align.
  return getTypeAlign(T);
}

/// getTypeInfoImpl - Return the size of the specified type, in bits.  This
/// method does not work on incomplete types.
///
/// FIXME: Pointers into different addr spaces could have different sizes and
/// alignment requirements: getPointerInfo should take an AddrSpace, this
/// should take a QualType, &c.
clang::TypeInfo CIRLowerContext::getTypeInfoImpl(const mlir::Type T) const {
  uint64_t Width = 0;
  unsigned Align = 8;
  clang::AlignRequirementKind AlignRequirement =
      clang::AlignRequirementKind::None;

  // Handle ArrayType early, similar to getTypeInfoInChars.
  if (auto arrTy = mlir::dyn_cast<ArrayType>(T)) {
    clang::TypeInfo EltInfo = getTypeInfo(arrTy.getElementType());
    uint64_t Size = arrTy.getSize();
    Width = EltInfo.Width * Size;
    Align = EltInfo.Align;
    return clang::TypeInfo(Width, Align, AlignRequirement);
  }

  // TODO(cir): We should implement a better way to identify type kinds and use
  // builting data layout interface for this.
  auto typeKind = clang::Type::Builtin;
  if (mlir::isa<IntType, SingleType, DoubleType, BoolType, FP16Type, BF16Type,
                FP80Type, FP128Type, LongDoubleType, mlir::IntegerType,
                mlir::FloatType>(T)) {
    typeKind = clang::Type::Builtin;
  } else if (mlir::isa<RecordType>(T)) {
    typeKind = clang::Type::Record;
  } else if (mlir::isa<VectorType>(T)) {
    typeKind = clang::Type::Vector;
  } else if (mlir::isa<ComplexType>(T)) {
    typeKind = clang::Type::Complex;
  } else {
    // FIXME(cir): Completely wrong. Just here to make it non-blocking.
    typeKind = clang::Type::Builtin;
  }

  // FIXME(cir): Here we fetch the width and alignment of a type considering the
  // current target. We can likely improve this using MLIR's data layout, or
  // some other interface, to abstract this away (e.g. type.getWidth() &
  // type.getAlign()). Verify if data layout suffices because this would involve
  // some other types such as vectors and complex numbers.
  // FIXME(cir): In the original codegen, this receives an AST type, meaning it
  // differs chars from integers, something that is not possible with the
  // current level of CIR.
  switch (typeKind) {
  case clang::Type::Builtin: {
    if (auto intTy = mlir::dyn_cast<IntType>(T)) {
      // NOTE(cir): IntType does not carry enough provenance to distinguish
      // __int128 from _BitInt(128), so preserve the dedicated __int128 rule
      // for the 128-bit case. For every other non-standard integer width, use
      // the target's _BitInt layout rules instead of capping at LongLongAlign.
      // This matches OGCG for widths such as _BitInt(65), _BitInt(129), and
      // _BitInt(257) on targets like AArch64 that permit larger _BitInt
      // alignment than plain long long.
      unsigned IntWidth = intTy.getWidth();
      bool useBitIntLayout =
          IntWidth != Target->getCharWidth() &&
          IntWidth != Target->getShortWidth() &&
          IntWidth != Target->getIntWidth() &&
          IntWidth != Target->getLongWidth() &&
          IntWidth != Target->getLongLongWidth() && IntWidth != 128;
      if (useBitIntLayout) {
        Width = Target->getBitIntWidth(IntWidth);
        Align = Target->getBitIntAlign(IntWidth);
        break;
      }

      Width = IntWidth;
      if (IntWidth == 128)
        Align = Target->getInt128Align();
      else
        Align = std::min(IntWidth, (unsigned)Target->getLongLongAlign());
      break;
    }
    if (auto boolTy = mlir::dyn_cast<BoolType>(T)) {
      Width = Target->getBoolWidth();
      Align = Target->getBoolAlign();
      break;
    }
    if (auto floatTy = mlir::dyn_cast<SingleType>(T)) {
      Width = Target->getFloatWidth();
      Align = Target->getFloatAlign();
      break;
    }
    if (auto doubleTy = mlir::dyn_cast<DoubleType>(T)) {
      Width = Target->getDoubleWidth();
      Align = Target->getDoubleAlign();
      break;
    }
    if (mlir::isa<FP16Type>(T)) {
      Width = Target->getHalfWidth();
      Align = Target->getHalfAlign();
      break;
    }
    if (mlir::isa<BF16Type>(T)) {
      Width = Target->getBFloat16Width();
      Align = Target->getBFloat16Align();
      break;
    }
    if (mlir::isa<LongDoubleType>(T)) {
      Width = Target->getLongDoubleWidth();
      Align = Target->getLongDoubleAlign();
      break;
    }
    if (mlir::isa<FP80Type>(T)) {
      // x87 extended precision - use long double metrics since FP80
      // is the underlying type for long double on x86.
      Width = 80;
      Align = Target->getLongDoubleAlign();
      break;
    }
    if (mlir::isa<FP128Type>(T)) {
      Width = Target->getFloat128Width();
      Align = Target->getFloat128Align();
      break;
    }
    if (auto ptrTy = mlir::dyn_cast<PointerType>(T)) {
      clang::LangAS langAS = getLangASForPointer(ptrTy);
      Width = Target->getPointerWidth(langAS);
      Align = Target->getPointerAlign(langAS);
      break;
    }
    if (auto opaqueTy = mlir::dyn_cast<OpaqueType>(T)) {
      if (opaqueTy.getTag() == OpaqueType::getAArch64SVCountTag()) {
        Width = 16;
        Align = 16;
        break;
      }
      if (opaqueTy.getTag() == OpaqueType::getAMDGCNNamedBarrierTag()) {
        Width = 128;
        Align = 32;
        break;
      }
      // OpenCL opaque types (event_t, etc.) are pointer-sized on most
      // targets.
      Width = Target->getPointerWidth(clang::LangAS::Default);
      Align = Target->getPointerAlign(clang::LangAS::Default);
      break;
    }
    // VPtrType is a vtable pointer, which has pointer size and alignment.
    if (mlir::isa<VPtrType>(T)) {
      Width = Target->getPointerWidth(clang::LangAS::Default);
      Align = Target->getPointerAlign(clang::LangAS::Default);
      break;
    }
    // MethodType is a C++ pointer-to-member-function. In the Itanium ABI
    // it is {fnptr_t, ptrdiff_t}, i.e. two pointer-sized values.
    if (mlir::isa<MethodType>(T)) {
      unsigned PtrWidth = Target->getPointerWidth(clang::LangAS::Default);
      Width = 2 * PtrWidth;
      Align = Target->getPointerAlign(clang::LangAS::Default);
      break;
    }
    // Handle MLIR builtin integer types (e.g., from exception handling
    // runtime function signatures).
    if (auto mlirIntTy = mlir::dyn_cast<mlir::IntegerType>(T)) {
      Width = mlirIntTy.getWidth();
      Align = std::min(Width, (uint64_t)Target->getLongLongAlign());
      break;
    }
    // Handle MLIR builtin float types.
    if (auto mlirFloatTy = mlir::dyn_cast<mlir::FloatType>(T)) {
      Width = mlirFloatTy.getWidth();
      Align = Width; // Reasonable default for float types.
      break;
    }
    cir_cconv_unreachable("Unknown builtin type!");
    break;
  }
  case clang::Type::Record: {
    const auto RT = mlir::dyn_cast<RecordType>(T);
    cir_cconv_assert(RT && "expected cir::RecordType for record abstraction");

    // NOTE(cir): Clang does some hanlding of invalid tagged declarations here.
    // Not sure if this is necessary in CIR.

    // Incomplete record types have zero size. This can happen when a function
    // is declared with an incomplete parameter type (e.g., a member function
    // pointer referencing a forward-declared struct). Match OGCG behavior
    // which returns zero size for incomplete types.
    if (!RT.isComplete())
      return clang::TypeInfo(0, 8, AlignRequirement);

    // CIR does not yet build Microsoft C++ record layouts in target lowering.
    // When AST provenance is available, query Clang's record layout directly so
    // ABI classification can still use the correct size and alignment.
    if (Target->getCXXABI().isMicrosoft()) {
      if (auto ast = RT.getAst()) {
        const clang::RecordDecl *RD = ast.getRawDecl();
        if (const auto *Def = RD->getDefinition()) {
          if (llvm::isa<clang::CXXRecordDecl>(Def)) {
            const clang::ASTRecordLayout &ASTLayout =
                Def->getASTContext().getASTRecordLayout(Def);
            Width = toBits(ASTLayout.getSize());
            Align = ASTLayout.getAlignment().getQuantity() * 8;
            if (Def->hasAttr<clang::AlignedAttr>())
              AlignRequirement = clang::AlignRequirementKind::RequiredByRecord;
            return clang::TypeInfo(Width, Align, AlignRequirement);
          }
        }
      }
    }

    const CIRRecordLayout &Layout = getCIRRecordLayout(RT);
    Width = toBits(Layout.getSize());
    Align = toBits(Layout.getAlignment());
    // When AST info is available, use the AST's alignment which correctly
    // handles all alignment attributes (__aligned__, alignas, etc.) including
    // typedef-level overrides that reduce alignment below the natural type
    // alignment (e.g., __aligned__(16) on a <4 x double> vector type).
    // CIR types lose typedef attributes, so the CIR layout builder may
    // compute a higher natural alignment than the AST declares.
    if (auto ast = RT.getAst()) {
      const clang::RecordDecl *RD = ast.getRawDecl();
      if (const auto *Def = RD->getDefinition()) {
        const clang::ASTRecordLayout &ASTLayout =
            Def->getASTContext().getASTRecordLayout(Def);
        // Match OGCG's AST-backed sizing for source-level records. CIR record
        // bodies may contain explicit tail-padding members, so recomputing the
        // width from the lowered member list can overcount padded wrappers and
        // perturb ABI classification.
        Width = toBits(ASTLayout.getSize());
        Align = ASTLayout.getAlignment().getQuantity() * 8; // chars to bits
        // Match OGCG: set AlignRequirement when the record has an explicit
        // alignment attribute (__aligned__, alignas, etc.).
        if (Def->hasAttr<clang::AlignedAttr>())
          AlignRequirement = clang::AlignRequirementKind::RequiredByRecord;
      }
    }
    break;
  }
  case clang::Type::Vector: {
    const auto VT = mlir::cast<VectorType>(T);
    // Get the element type info
    clang::TypeInfo EltInfo = getTypeInfo(VT.getElementType());
    uint64_t EltSize = EltInfo.Width;
    uint64_t NumElements = VT.getSize();
    // Packed boolean vectors use 1 bit per element, matching
    // ASTContext::getTypeInfoImpl's isPackedVectorBoolType check.
    if (mlir::isa<BoolType>(VT.getElementType()))
      Width = NumElements;
    else
      Width = EltSize * NumElements;
    // Enforce at least byte size and alignment, matching ASTContext.
    Width = std::max<unsigned>(8, Width);
    Align = std::max<unsigned>(8, Width);
    // If the alignment is not a power of 2, round up to the next power of 2.
    // This happens for non-power-of-2 length vectors (e.g. 3-element).
    if (Align & (Align - 1)) {
      Align = llvm::bit_ceil(Align);
      Width = llvm::alignTo(Width, Align);
    }
    // Adjust the alignment based on the target max, matching
    // ASTContext::getTypeInfoImpl behavior.
    uint64_t targetVectorAlign = Target->getMaxVectorAlign();
    if (targetVectorAlign && targetVectorAlign < Align)
      Align = targetVectorAlign;
    break;
  }
  case clang::Type::Complex: {
    // Complex types have two elements of the same type.
    const auto CT = mlir::cast<ComplexType>(T);
    clang::TypeInfo EltInfo = getTypeInfo(CT.getElementType());
    Width = EltInfo.Width * 2;
    Align = EltInfo.Align;
    break;
  }
  default:
    cir_cconv_unreachable("Unhandled type class");
  }

  cir_cconv_assert(llvm::isPowerOf2_32(Align) &&
                   "Alignment must be power of 2");
  return clang::TypeInfo(Width, Align, AlignRequirement);
}

mlir::Type CIRLowerContext::initBuiltinType(clang::BuiltinType::Kind K) {
  mlir::Type Ty;

  // NOTE(cir): Clang does more stuff here. Not sure if we need to do the same.
  // qualifiedTypes is no longer tracked as a MissingFeatures guard.
  switch (K) {
  case clang::BuiltinType::Char_S:
    Ty = IntType::get(getMLIRContext(), 8, true);
    break;
  default:
    cir_cconv_unreachable("NYI");
  }

  Types.push_back(Ty);
  return Ty;
}

void CIRLowerContext::initBuiltinTypes(const clang::TargetInfo &Target,
                                       const clang::TargetInfo *AuxTarget) {
  cir_cconv_assert((!this->Target || this->Target == &Target) &&
                   "Incorrect target reinitialization");
  this->Target = &Target;
  this->AuxTarget = AuxTarget;

  // C99 6.2.5p3.
  if (LangOpts.CharIsSigned)
    CharTy = initBuiltinType(clang::BuiltinType::Char_S);
  else
    cir_cconv_unreachable("NYI");
}

/// Convert a size in bits to a size in characters.
clang::CharUnits CIRLowerContext::toCharUnitsFromBits(int64_t BitSize) const {
  return clang::CharUnits::fromQuantity(BitSize / getCharWidth());
}

/// Convert a size in characters to a size in characters.
int64_t CIRLowerContext::toBits(clang::CharUnits CharSize) const {
  return CharSize.getQuantity() * getCharWidth();
}

/// Performing the computation in CharUnits
/// instead of in bits prevents overflowing the uint64_t for some large arrays.
clang::TypeInfoChars getConstantArrayInfoInChars(const CIRLowerContext &ctx,
                                                 cir::ArrayType arrTy) {
  clang::TypeInfoChars eltInfo = ctx.getTypeInfoInChars(arrTy.getElementType());
  uint64_t tySize = arrTy.getSize();
  assert((tySize == 0 || static_cast<uint64_t>(eltInfo.Width.getQuantity()) <=
                             (uint64_t)(-1) / tySize) &&
         "Overflow in array type char size evaluation");
  uint64_t width = eltInfo.Width.getQuantity() * tySize;
  unsigned align = eltInfo.Align.getQuantity();
  if (!ctx.getTargetInfo().getCXXABI().isMicrosoft() ||
      ctx.getTargetInfo().getPointerWidth(clang::LangAS::Default) == 64)
    width = llvm::alignTo(width, align);
  return clang::TypeInfoChars(clang::CharUnits::fromQuantity(width),
                              clang::CharUnits::fromQuantity(align),
                              eltInfo.AlignRequirement);
}

clang::TypeInfoChars CIRLowerContext::getTypeInfoInChars(mlir::Type T) const {
  if (auto arrTy = mlir::dyn_cast<ArrayType>(T))
    return getConstantArrayInfoInChars(*this, arrTy);
  clang::TypeInfo Info = getTypeInfo(T);
  return clang::TypeInfoChars(toCharUnitsFromBits(Info.Width),
                              toCharUnitsFromBits(Info.Align),
                              Info.AlignRequirement);
}

bool CIRLowerContext::isPromotableIntegerType(mlir::Type T) const {
  // HLSL doesn't promote all small integer types to int, it
  // just uses the rank-based promotion rules for all types.
  if (getLangOpts().HLSL)
    return false;

  // FIXME(cir): CIR does not distinguish between char, short, etc. So we just
  // assume it is promotable if smaller than 32 bits. This is wrong since, for
  // example, Char32 is promotable. Improve CIR or add an AST query here.
  if (auto intTy = mlir::dyn_cast<IntType>(T)) {
    return mlir::cast<IntType>(T).getWidth() < 32;
  }

  // Bool are also handled here for codegen parity.
  if (auto boolTy = mlir::dyn_cast<BoolType>(T)) {
    return true;
  }

  return false;
}

} // namespace cir
