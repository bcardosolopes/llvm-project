//===--- LowerFunction.cpp - Lower CIR Function Code ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file partially mimics clang/lib/CodeGen/CodeGenFunction.cpp. The queries
// are adapted to operate on the CIR dialect, however.
//
//===----------------------------------------------------------------------===//
#include "LowerFunction.h"
#include "CIRToCIRArgMapping.h"
#include "LowerCall.h"
#include "LowerFunctionInfo.h"
#include "LowerModule.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecordLayout.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Support/LogicalResult.h"
#include "clang/AST/DeclCXX.h"
#include "clang/CIR/ABIArgInfo.h"
#include "clang/CIR/Dialect/Builder/CIRBaseBuilder.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"
#include "clang/CIR/TypeEvaluationKind.h"
#include "clang/AST/CharUnits.h"
#include "mlir/IR/Dominance.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <numeric>

using ABIArgInfo = cir::ABIArgInfo;

namespace cir {

namespace {

constexpr llvm::StringLiteral kDILocationAtomGroupKey = "llvm.di_atom_group";
constexpr llvm::StringLiteral kDILocationAtomRankKey = "llvm.di_atom_rank";

/// Advance \p builder's insertion point past the latest defining op (living in
/// the current insertion block) of any value in \p args, so that an op created
/// at the resulting point has all of these operands dominating it. Never moves
/// the insertion point backward over earlier ops. This mirrors the
/// terminator-safe logic already used when rewriting try_call, and is needed
/// for plain calls whose sret return slot reuses a destination pointer defined
/// after the original call (e.g. `a[i] = f()` where the element address is
/// loaded post-call) on sret-return targets such as i386 and 32-bit ARM.
static void advanceInsertionPastArgs(mlir::OpBuilder &builder,
                                     llvm::ArrayRef<mlir::Value> args) {
  mlir::Block *ipBlock = builder.getInsertionBlock();
  if (!ipBlock)
    return;
  mlir::Block::iterator ipPoint = builder.getInsertionPoint();
  mlir::Operation *ipOp = ipPoint == ipBlock->end() ? nullptr : &*ipPoint;
  mlir::Operation *latestArg = nullptr;
  for (mlir::Value arg : args) {
    mlir::Operation *def = arg.getDefiningOp();
    if (!def || def->getBlock() != ipBlock)
      continue;
    if (ipOp && def != ipOp && def->isBeforeInBlock(ipOp))
      continue;
    if (!latestArg || latestArg->isBeforeInBlock(def))
      latestArg = def;
  }
  if (latestArg)
    builder.setInsertionPointAfter(latestArg);
}

/// Check if a CIR type has floating-point representation, matching OGCG's
/// QualType::hasFloatingRepresentation().
static bool hasFloatingRepresentation(mlir::Type ty) {
  if (isAnyFloatingPointType(ty))
    return true;
  if (auto complexTy = mlir::dyn_cast<cir::ComplexType>(ty))
    return isAnyFloatingPointType(complexTy.getElementType());
  if (auto vectorTy = mlir::dyn_cast<cir::VectorType>(ty))
    return isAnyFloatingPointType(vectorTy.getElementType());
  return false;
}

/// Mirror LLVM's nofpclass type legality check for lowered CIR types.
static bool isNoFPClassCompatibleType(mlir::Type ty) {
  if (isAnyFloatingPointType(ty))
    return true;
  if (auto complexTy = mlir::dyn_cast<cir::ComplexType>(ty))
    return isAnyFloatingPointType(complexTy.getElementType());
  if (auto vectorTy = mlir::dyn_cast<cir::VectorType>(ty))
    return isAnyFloatingPointType(vectorTy.getElementType());
  if (auto recordTy = mlir::dyn_cast<cir::RecordType>(ty)) {
    auto members = recordTy.getMembers();
    return !members.empty() &&
           llvm::all_of(members, [](mlir::Type memberTy) {
             return isNoFPClassCompatibleType(memberTy);
           });
  }
  return false;
}

/// Match OGCG's nofpclass gating: the source type must carry floating
/// semantics, and the ABI-lowered storage type must be legal for nofpclass.
static bool canApplyNoFPClass(const ABIArgInfo &AI, mlir::Type ty) {
  if (!hasFloatingRepresentation(ty))
    return false;

  mlir::Type loweredTy =
      AI.canHaveCoerceToType() ? AI.getCoerceToType() : nullptr;
  if (!loweredTy)
    loweredTy = ty;
  return isNoFPClassCompatibleType(loweredTy);
}

static cir::LoadOp findLoadLike(mlir::Value v);

static bool hasSingleUseLoadChain(mlir::Value value, cir::LoadOp &load) {
  while (auto cast = value.getDefiningOp<cir::CastOp>()) {
    if (!cast.getResult().hasOneUse())
      return false;
    value = cast.getSrc();
  }

  load = value.getDefiningOp<cir::LoadOp>();
  return load && load.getResult().hasOneUse();
}

static cir::IntAttr getConstantIndexOrNull(mlir::Value idx) {
  if (auto constantOp = idx.getDefiningOp<cir::ConstantOp>())
    return constantOp.getValueAttr<cir::IntAttr>();
  return {};
}

static void updateAlignmentIfStronger(std::optional<uint64_t> currentAlign,
                                      uint64_t newAlign,
                                      llvm::function_ref<void(uint64_t)> set) {
  if (!currentAlign || *currentAlign < newAlign)
    set(newAlign);
}

static void updateCopyAlignmentIfStronger(
    mlir::IntegerAttr currentAlign, uint64_t newAlign,
    llvm::function_ref<void(mlir::IntegerAttr)> set,
    mlir::PatternRewriter &rewriter) {
  if (!currentAlign || currentAlign.getInt() < (int64_t)newAlign)
    set(rewriter.getI64IntegerAttr(newAlign));
}

static void propagateByValDerivedAlignment(LowerFunction &LF, mlir::Value addr,
                                           clang::CharUnits alignment) {
  auto &rewriter = LF.getRewriter();
  auto &layout = LF.LM.getDataLayout();

  for (mlir::OpOperand &use : llvm::make_early_inc_range(addr.getUses())) {
    mlir::Operation *user = use.getOwner();

    if (auto copyOp = mlir::dyn_cast<CopyOp>(user)) {
      if (copyOp.getSrc() == addr)
        updateCopyAlignmentIfStronger(copyOp.getSrcAlignmentAttr(),
                                      alignment.getQuantity(),
                                      [&](mlir::IntegerAttr attr) {
                                        copyOp.setSrcAlignmentAttr(attr);
                                      },
                                      rewriter);
      if (copyOp.getDst() == addr)
        updateCopyAlignmentIfStronger(copyOp.getDstAlignmentAttr(),
                                      alignment.getQuantity(),
                                      [&](mlir::IntegerAttr attr) {
                                        copyOp.setDstAlignmentAttr(attr);
                                      },
                                      rewriter);
      continue;
    }

    if (auto loadOp = mlir::dyn_cast<LoadOp>(user)) {
      if (loadOp.getAddr() != addr)
        continue;
      updateAlignmentIfStronger(loadOp.getAlignment(), alignment.getQuantity(),
                                [&](uint64_t align) {
                                  loadOp.setAlignment(align);
                                });
      continue;
    }

    if (auto storeOp = mlir::dyn_cast<StoreOp>(user)) {
      if (storeOp.getAddr() != addr)
        continue;
      updateAlignmentIfStronger(storeOp.getAlignment(), alignment.getQuantity(),
                                [&](uint64_t align) {
                                  storeOp.setAlignment(align);
                                });
      continue;
    }

    if (auto castOp = mlir::dyn_cast<CastOp>(user)) {
      if (castOp.getSrc() != addr)
        continue;
      if (castOp.getKind() == cir::CastKind::bitcast ||
          castOp.getKind() == cir::CastKind::address_space)
        propagateByValDerivedAlignment(LF, castOp.getResult(), alignment);
      continue;
    }

    if (auto memberOp = mlir::dyn_cast<GetMemberOp>(user)) {
      if (memberOp.getAddr() != addr)
        continue;
      auto recordPtrTy = mlir::dyn_cast<PointerType>(addr.getType());
      auto recordTy =
          recordPtrTy ? mlir::dyn_cast<RecordType>(recordPtrTy.getPointee())
                      : RecordType();
      if (!recordTy)
        continue;
      clang::CharUnits offset = clang::CharUnits::fromQuantity(
          recordTy.getElementOffset(layout.layout, memberOp.getIndex()));
      propagateByValDerivedAlignment(LF, memberOp.getResult(),
                                     alignment.alignmentAtOffset(offset));
      continue;
    }

    if (auto elementOp = mlir::dyn_cast<GetElementOp>(user)) {
      if (elementOp.getBase() != addr)
        continue;
      auto arrayPtrTy = mlir::dyn_cast<PointerType>(addr.getType());
      auto arrayTy =
          arrayPtrTy ? mlir::dyn_cast<ArrayType>(arrayPtrTy.getPointee())
                     : ArrayType();
      if (!arrayTy)
        continue;

      clang::CharUnits elementSize = clang::CharUnits::fromQuantity(
          layout.getTypeAllocSize(arrayTy.getElementType()).getFixedValue());
      clang::CharUnits elementAlignment =
          alignment.alignmentOfArrayElement(elementSize);
      if (cir::IntAttr constantIdx = getConstantIndexOrNull(elementOp.getIndex()))
        elementAlignment = alignment.alignmentAtOffset(
            constantIdx.getValue().getZExtValue() * elementSize);
      propagateByValDerivedAlignment(LF, elementOp.getResult(), elementAlignment);
      continue;
    }

    if (auto strideOp = mlir::dyn_cast<PtrStrideOp>(user)) {
      if (strideOp.getBase() != addr)
        continue;
      clang::CharUnits elementSize = clang::CharUnits::fromQuantity(
          layout.getTypeAllocSize(strideOp.getElementType()).getFixedValue());
      clang::CharUnits derivedAlignment =
          alignment.alignmentOfArrayElement(elementSize);
      if (cir::IntAttr constantIdx = getConstantIndexOrNull(strideOp.getStride()))
        derivedAlignment = alignment.alignmentAtOffset(
            constantIdx.getValue().getZExtValue() * elementSize);
      propagateByValDerivedAlignment(LF, strideOp.getResult(), derivedAlignment);
      continue;
    }
  }
}

/// Erase the defining op of \p addr, chasing through address_space casts.
/// On AMDGCN, CIRGen inserts private→flat address_space casts between allocas
/// and stores. When erasing a dead store's target address, we must erase the
/// cast AND the underlying alloca (if it becomes dead).
void eraseDefOpThroughCast(mlir::PatternRewriter &rewriter, mlir::Value addr) {
  auto *addrDef = addr.getDefiningOp();
  if (!addrDef)
    return;
  // Only erase ops this helper's contract owns: address-producing allocas
  // (and the address_space casts wrapping them). A CallOp / GetGlobalOp /
  // PtrStrideOp may be referenced by other passes (e.g. tracked in
  // CallConvLowering's users->directCalls); erasing them yields UAF.
  if (auto castOp = mlir::dyn_cast<CastOp>(addrDef)) {
    auto castSrc = castOp.getSrc();
    rewriter.eraseOp(castOp);
    if (castSrc.use_empty())
      if (auto *srcDef = castSrc.getDefiningOp())
        if (mlir::isa<AllocaOp, CastOp>(srcDef))
          rewriter.eraseOp(srcDef);
    return;
  }
  if (mlir::isa<AllocaOp>(addrDef))
    rewriter.eraseOp(addrDef);
}

struct ArgInitStoreChain {
  cir::StoreOp store;
  cir::CastOp preStoreCast;

  ArgInitStoreChain() = default;
  ArgInitStoreChain(cir::StoreOp store, cir::CastOp preStoreCast = {})
      : store(store), preStoreCast(preStoreCast) {}

  explicit operator bool() const { return static_cast<bool>(store); }
};

static ArgInitStoreChain findArgInitStoreChain(mlir::Value arg) {
  if (!arg.hasOneUse())
    return {};

  mlir::Operation *user = *arg.user_begin();
  if (auto store = mlir::dyn_cast<cir::StoreOp>(user)) {
    if (store.getValue() == arg)
      return ArgInitStoreChain(store);
    return {};
  }

  auto cast = mlir::dyn_cast<cir::CastOp>(user);
  if (!cast || cast.getKind() != cir::CastKind::integral ||
      cast.getSrc() != arg || !cast.getResult().hasOneUse())
    return {};

  auto store = mlir::dyn_cast<cir::StoreOp>(*cast.getResult().user_begin());
  if (!store || store.getValue() != cast.getResult())
    return {};

  return ArgInitStoreChain(store, cast);
}

static void eraseArgInitStoreChain(mlir::PatternRewriter &rewriter,
                                   ArgInitStoreChain chain,
                                   mlir::Value addr) {
  rewriter.eraseOp(chain.store);
  if (chain.preStoreCast && chain.preStoreCast.getResult().use_empty())
    rewriter.eraseOp(chain.preStoreCast);
  eraseDefOpThroughCast(rewriter, addr);
}

/// Erase dead operations that only serve writes to an ignored return slot.
///
/// LowerFunction's ignored-return ABI rewrite drops return operands but the
/// source-level `__retval` traffic can still remain. Walk through address-space
/// casts from the return slot and erase dead stores/copies/loads that become
/// pointless once the ABI treats the function as returning void.
static void eraseDeadIgnoredRetSlotUsers(mlir::PatternRewriter &rewriter,
                                         mlir::Value addr) {
  for (mlir::OpOperand &use : llvm::make_early_inc_range(addr.getUses())) {
    mlir::Operation *user = use.getOwner();

    if (auto castOp = mlir::dyn_cast<CastOp>(user)) {
      eraseDeadIgnoredRetSlotUsers(rewriter, castOp.getResult());
      if (castOp.getResult().use_empty())
        rewriter.eraseOp(castOp);
      continue;
    }

    if (auto storeOp = mlir::dyn_cast<StoreOp>(user)) {
      if (storeOp.getAddr() != addr)
        continue;
      auto deadLoad = findLoadLike(storeOp.getValue());
      rewriter.eraseOp(storeOp);
      if (deadLoad && deadLoad.getResult().use_empty())
        rewriter.eraseOp(deadLoad);
      continue;
    }

    if (auto copyOp = mlir::dyn_cast<CopyOp>(user)) {
      if (copyOp.getDst() != addr)
        continue;
      rewriter.eraseOp(copyOp);
      continue;
    }

    if (auto loadOp = mlir::dyn_cast<LoadOp>(user)) {
      if (loadOp.getResult().use_empty())
        rewriter.eraseOp(loadOp);
    }
  }
}

static std::pair<uint64_t, uint8_t>
extractSourceAtomMetadata(mlir::Location loc) {
  if (auto fusedLoc = mlir::dyn_cast<mlir::FusedLoc>(loc)) {
    if (auto metadata =
            mlir::dyn_cast_or_null<mlir::DictionaryAttr>(fusedLoc.getMetadata())) {
      uint64_t atomGroup = 0;
      uint8_t atomRank = 0;
      if (auto atomGroupAttr =
              metadata.getAs<mlir::IntegerAttr>(kDILocationAtomGroupKey))
        if (atomGroupAttr.getValue().isNonNegative())
          atomGroup = atomGroupAttr.getValue().getZExtValue();
      if (auto atomRankAttr =
              metadata.getAs<mlir::IntegerAttr>(kDILocationAtomRankKey))
        if (atomRankAttr.getValue().isNonNegative() &&
            atomRankAttr.getValue().ule(7))
          atomRank = atomRankAttr.getValue().getZExtValue();
      return {atomGroup, atomRank};
    }
  }

  return {0, 0};
}

static mlir::Location applySourceAtomMetadata(mlir::Location loc, uint64_t group,
                                              uint8_t rank) {
  if (!group || !rank)
    return loc;

  auto [existingGroup, existingRank] = extractSourceAtomMetadata(loc);
  if (existingGroup && existingRank && existingRank < rank)
    return loc;

  auto *ctx = loc.getContext();
  auto i64Ty = mlir::IntegerType::get(ctx, 64);
  auto i8Ty = mlir::IntegerType::get(ctx, 8);
  auto dict = mlir::DictionaryAttr::get(
      ctx,
      {mlir::NamedAttribute(
           mlir::StringAttr::get(ctx, kDILocationAtomGroupKey),
           mlir::IntegerAttr::get(i64Ty, group)),
       mlir::NamedAttribute(
           mlir::StringAttr::get(ctx, kDILocationAtomRankKey),
           mlir::IntegerAttr::get(i8Ty, rank))});
  return mlir::FusedLoc::get({loc}, dict, ctx);
}

static cir::TBAAAttr
deriveIndirectParamPointerStoreTBAA(cir::StoreOp originalStore,
                                    mlir::Type storedValueType,
                                    mlir::MLIRContext *ctx) {
  if (!mlir::isa<cir::PointerType>(storedValueType))
    return {};

  std::optional<cir::TBAAAttr> originalTBAA = originalStore.getTbaa();
  if (!originalTBAA)
    return {};

  cir::TBAAStructAttr structTBAA;
  if (auto tag = mlir::dyn_cast<cir::TBAATagAttr>(*originalTBAA))
    structTBAA = mlir::dyn_cast<cir::TBAAStructAttr>(tag.getBase());
  else
    structTBAA = mlir::dyn_cast<cir::TBAAStructAttr>(*originalTBAA);

  if (!structTBAA)
    return {};

  auto anyPtr =
      cir::TBAAScalarAttr::get(ctx, "any pointer", storedValueType);
  llvm::SmallString<256> pointerID("p1 ");
  pointerID += structTBAA.getId();
  return cir::TBAAScalarAttr::get(ctx, pointerID, storedValueType, anyPtr,
                                  /*size=*/0);
}

static cir::TBAAAttr deriveFlattenedFieldStoreTBAA(cir::StoreOp originalStore) {
  std::optional<cir::TBAAAttr> originalTBAA = originalStore.getTbaa();
  if (!originalTBAA)
    return {};

  if (mlir::isa<cir::TBAAOmnipotentCharAttr, cir::TBAAScalarAttr,
                cir::TBAAVTablePointerAttr>(*originalTBAA))
    return *originalTBAA;

  if (auto tag = mlir::dyn_cast<cir::TBAATagAttr>(*originalTBAA))
    if (mlir::isa<cir::TBAAOmnipotentCharAttr, cir::TBAAScalarAttr,
                  cir::TBAAVTablePointerAttr>(tag.getAccess()))
      return *originalTBAA;

  return {};
}

static bool typeHasMayAlias(clang::QualType qty) {
  if (auto *td = qty->getAsTagDecl())
    if (td->hasAttr<clang::MayAliasAttr>())
      return true;

  if (auto *arrayTy = qty->getAsArrayTypeUnsafe())
    if (typeHasMayAlias(arrayTy->getElementType()))
      return true;

  while (auto *tt = qty->getAs<clang::TypedefType>()) {
    if (tt->getDecl()->hasAttr<clang::MayAliasAttr>())
      return true;
    qty = tt->desugar();
  }
  return false;
}

class AggregateCopyTBAABuilder {
public:
  explicit AggregateCopyTBAABuilder(LowerFunction &LF)
      : mlirContext(LF.getRewriter().getContext()),
        codeGenOpts(LF.LM.getContext().getCodeGenOpts()),
        langOpts(LF.LM.getContext().getLangOpts()),
        newStructPathTBAA(LF.LM.getModule()->hasAttr(
            cir::CIRDialect::getNewStructPathTBAAAttrName())) {}

  mlir::ArrayAttr getTBAAStructInfo(cir::RecordType recordTy) {
    if (codeGenOpts.OptimizationLevel == 0 || codeGenOpts.RelaxedAliasing)
      return {};

    auto recordAst = recordTy.getAst();
    if (!recordAst)
      return {};

    const clang::RecordDecl *recordDecl = recordAst.getRawDecl();
    if (!recordDecl)
      return {};
    recordDecl = recordDecl->getDefinition();
    if (!recordDecl || recordDecl->hasFlexibleArrayMember())
      return {};

    auto &astContext = recordDecl->getASTContext();

    struct FieldInfo {
      uint64_t offset;
      uint64_t size;
      cir::TBAAAttr tag;
    };
    llvm::SmallVector<FieldInfo, 8> fields;

    std::function<bool(uint64_t, clang::QualType, bool)> collectFields =
        [&](uint64_t baseOffset, clang::QualType qTy, bool mayAlias) -> bool {
      if (const auto *recordType = qTy->getAsCanonical<clang::RecordType>()) {
        if (recordType->isUnionType()) {
          uint64_t size = astContext.getTypeSizeInChars(qTy).getQuantity();
          auto tag = getAccessTagInfo(getChar(), size);
          if (!tag)
            return false;
          fields.push_back({baseOffset, size, tag});
          return true;
        }

        const clang::RecordDecl *rd = recordType->getDecl()->getDefinition();
        if (!rd || rd->hasFlexibleArrayMember())
          return false;

        if (const auto *cxxRD = llvm::dyn_cast<clang::CXXRecordDecl>(rd))
          if (!cxxRD->bases().empty())
            return false;

        const clang::ASTRecordLayout &layout =
            astContext.getASTRecordLayout(rd);
        unsigned idx = 0;
        for (auto fieldIt = rd->field_begin(), fieldEnd = rd->field_end();
             fieldIt != fieldEnd; ++fieldIt, ++idx) {
          clang::FieldDecl *field = *fieldIt;
          if (field->isZeroSize(astContext) || field->isUnnamedBitField())
            continue;

          if (field->isBitField())
            return false;

          uint64_t offset =
              baseOffset + layout.getFieldOffset(idx) / astContext.getCharWidth();
          clang::QualType fieldType = field->getType();
          if (!collectFields(offset, fieldType,
                             mayAlias || typeHasMayAlias(fieldType)))
            return false;
        }
        return true;
      }

      uint64_t size = astContext.getTypeSizeInChars(qTy).getQuantity();
      cir::TBAAAttr type = mayAlias ? getChar() : getTypeInfo(astContext, qTy);
      if (!type)
        return false;

      auto tag = getAccessTagInfo(type, size);
      if (!tag)
        return false;
      fields.push_back({baseOffset, size, tag});
      return true;
    };

    clang::QualType recordQty = astContext.getCanonicalTagType(recordDecl);
    if (!collectFields(/*baseOffset=*/0, recordQty,
                       typeHasMayAlias(recordQty)))
      return {};

    auto int64Ty = mlir::IntegerType::get(mlirContext, 64);
    auto mkI64 = [&](uint64_t value) -> mlir::IntegerAttr {
      return mlir::IntegerAttr::get(int64Ty, value);
    };

    llvm::SmallVector<mlir::Attribute, 8> fieldAttrs;
    fieldAttrs.reserve(fields.size());
    for (const auto &field : fields) {
      fieldAttrs.push_back(mlir::ArrayAttr::get(
          mlirContext, {mkI64(field.offset), mkI64(field.size), field.tag}));
    }
    return mlir::ArrayAttr::get(mlirContext, fieldAttrs);
  }

  // Access tag for the store that spills a non-byval indirect record argument
  // into its local pointer slot. OGCG derives it from the *pointer* type
  // (`EmitParmDecl` in CGDecl.cpp), never from the record's own access info,
  // which CIRGen deliberately leaves empty for aggregates.
  cir::TBAAAttr getIndirectParamPointerTag(cir::RecordType recordTy) {
    auto recordAst = recordTy.getAst();
    if (!recordAst)
      return {};

    const clang::RecordDecl *recordDecl = recordAst.getRawDecl();
    if (!recordDecl)
      return {};

    clang::ASTContext &astContext = recordDecl->getASTContext();
    clang::QualType ptrQty =
        astContext.getPointerType(astContext.getCanonicalTagType(recordDecl));
    return getAccessTagInfo(
        getTypeInfo(astContext, ptrQty),
        astContext.getTypeSizeInChars(ptrQty).getQuantity());
  }

private:
  cir::TBAAAttr getChar() const {
    return cir::TBAAOmnipotentCharAttr::get(mlirContext);
  }

  cir::TBAAScalarAttr getAnyPtr(clang::ASTContext &astContext,
                                unsigned ptrDepth) {
    if (anyPtrs.size() < ptrDepth) {
      anyPtrs.reserve(ptrDepth);
      uint64_t ptrSize = newStructPathTBAA
                             ? astContext.getTypeSizeInChars(astContext.VoidPtrTy)
                                   .getQuantity()
                             : 0;
      auto ptrTy =
          cir::PointerType::get(mlirContext, cir::VoidType::get(mlirContext));
      if (anyPtrs.empty())
        anyPtrs.push_back(cir::TBAAScalarAttr::get(
            mlirContext, "any pointer", ptrTy, /*parent=*/nullptr, ptrSize));
      for (size_t idx = anyPtrs.size(); idx < ptrDepth; ++idx) {
        auto name = ("any p" + llvm::Twine(idx + 1) + " pointer").str();
        anyPtrs.push_back(cir::TBAAScalarAttr::get(
            mlirContext, name, ptrTy, anyPtrs[idx - 1], ptrSize));
      }
    }
    return anyPtrs[ptrDepth - 1];
  }

  cir::TBAAScalarAttr getScalarTypeInfo(clang::ASTContext &astContext,
                                        clang::QualType qty) const {
    const clang::Type *type = astContext.getCanonicalType(qty).getTypePtr();
    const auto *builtinType = llvm::cast<clang::BuiltinType>(type);
    uint64_t size =
        newStructPathTBAA ? astContext.getTypeSizeInChars(qty).getQuantity() : 0;
    return cir::TBAAScalarAttr::get(mlirContext, builtinType->getName(langOpts),
                                    cir::VoidType::get(mlirContext),
                                    /*parent=*/nullptr, size);
  }

  std::optional<llvm::SmallString<256>>
  mangleCanonicalTypeName(clang::ASTContext &astContext,
                          clang::QualType qty) const {
    if (!langOpts.CPlusPlus)
      return std::nullopt;

    auto mangler = std::unique_ptr<clang::MangleContext>(
        astContext.createMangleContext());
    if (!mangler)
      return std::nullopt;

    llvm::SmallString<256> outName;
    llvm::raw_svector_ostream out(outName);
    mangler->mangleCanonicalTypeName(qty, out);
    return outName;
  }

  cir::TBAAAttr getTypeInfo(clang::ASTContext &astContext,
                            clang::QualType qty) {
    if (codeGenOpts.OptimizationLevel == 0 || codeGenOpts.RelaxedAliasing)
      return {};

    if (typeHasMayAlias(qty))
      return getChar();

    return getTypeInfoHelper(astContext, qty);
  }

  cir::TBAAAttr getTypeInfoHelper(clang::ASTContext &astContext,
                                  clang::QualType qty) {
    const clang::Type *type = astContext.getCanonicalType(qty).getTypePtr();

    if (const auto *builtinType = llvm::dyn_cast<clang::BuiltinType>(type)) {
      switch (builtinType->getKind()) {
      case clang::BuiltinType::Char_U:
      case clang::BuiltinType::Char_S:
      case clang::BuiltinType::UChar:
      case clang::BuiltinType::SChar:
        return getChar();
      case clang::BuiltinType::UShort:
        return getScalarTypeInfo(astContext, astContext.ShortTy);
      case clang::BuiltinType::UInt:
        return getScalarTypeInfo(astContext, astContext.IntTy);
      case clang::BuiltinType::ULong:
        return getScalarTypeInfo(astContext, astContext.LongTy);
      case clang::BuiltinType::ULongLong:
        return getScalarTypeInfo(astContext, astContext.LongLongTy);
      case clang::BuiltinType::UInt128:
        return getScalarTypeInfo(astContext, astContext.Int128Ty);
      case clang::BuiltinType::UShortFract:
        return getScalarTypeInfo(astContext, astContext.ShortFractTy);
      case clang::BuiltinType::UFract:
        return getScalarTypeInfo(astContext, astContext.FractTy);
      case clang::BuiltinType::ULongFract:
        return getScalarTypeInfo(astContext, astContext.LongFractTy);
      case clang::BuiltinType::SatUShortFract:
        return getScalarTypeInfo(astContext, astContext.SatShortFractTy);
      case clang::BuiltinType::SatUFract:
        return getScalarTypeInfo(astContext, astContext.SatFractTy);
      case clang::BuiltinType::SatULongFract:
        return getScalarTypeInfo(astContext, astContext.SatLongFractTy);
      case clang::BuiltinType::UShortAccum:
        return getScalarTypeInfo(astContext, astContext.ShortAccumTy);
      case clang::BuiltinType::UAccum:
        return getScalarTypeInfo(astContext, astContext.AccumTy);
      case clang::BuiltinType::ULongAccum:
        return getScalarTypeInfo(astContext, astContext.LongAccumTy);
      case clang::BuiltinType::SatUShortAccum:
        return getScalarTypeInfo(astContext, astContext.SatShortAccumTy);
      case clang::BuiltinType::SatUAccum:
        return getScalarTypeInfo(astContext, astContext.SatAccumTy);
      case clang::BuiltinType::SatULongAccum:
        return getScalarTypeInfo(astContext, astContext.SatLongAccumTy);
      default:
        return getScalarTypeInfo(astContext, qty);
      }
    }

    if (type->isStdByteType())
      return getChar();

    if (type->isPointerType() || type->isReferenceType()) {
      if (!codeGenOpts.PointerTBAA)
        return getAnyPtr(astContext, 1);

      unsigned ptrDepth = 0;
      do {
        ++ptrDepth;
        type = type->getPointeeType()->getBaseElementTypeUnsafe();
      } while (type->isPointerType());

      if (type->isVoidType())
        return getAnyPtr(astContext, ptrDepth);

      llvm::SmallString<256> typeName;
      if (llvm::isa<clang::BuiltinType>(type)) {
        auto pointeeTBAA = getTypeInfoHelper(astContext, clang::QualType(type, 0));
        if (auto scalarAttr = mlir::dyn_cast<cir::TBAAScalarAttr>(pointeeTBAA))
          typeName = scalarAttr.getId();
        else if (mlir::isa<cir::TBAAOmnipotentCharAttr>(pointeeTBAA))
          typeName = "omnipotent char";
        else
          return getAnyPtr(astContext, ptrDepth);
      } else {
        const auto *recordType = type->getAs<clang::RecordType>();
        if (!recordType || !recordType->getDecl()->getDeclName())
          return getAnyPtr(astContext, ptrDepth);

        auto mangled =
            mangleCanonicalTypeName(astContext, clang::QualType(type, 0));
        if (!mangled)
          return getAnyPtr(astContext, ptrDepth);
        typeName = *mangled;
      }

      uint64_t ptrSize =
          newStructPathTBAA ? astContext.getTypeSizeInChars(qty).getQuantity() : 0;
      llvm::SmallString<256> outName("p");
      outName += std::to_string(ptrDepth);
      outName += " ";
      outName += typeName;
      return cir::TBAAScalarAttr::get(
          mlirContext, outName,
          cir::PointerType::get(mlirContext, cir::VoidType::get(mlirContext)),
          getAnyPtr(astContext, ptrDepth), ptrSize);
    }

    if (newStructPathTBAA && type->isArrayType())
      return getTypeInfo(astContext, llvm::cast<clang::ArrayType>(type)
                                         ->getElementType());

    if (const auto *enumType = llvm::dyn_cast<clang::EnumType>(type)) {
      const clang::EnumDecl *enumDecl =
          enumType->getDecl()->getDefinitionOrSelf();
      if (!langOpts.CPlusPlus) {
        clang::QualType underlyingType = enumDecl->getIntegerType();
        if (underlyingType.isNull())
          return getChar();
        return getTypeInfo(astContext, underlyingType);
      }

      if (!enumDecl->isExternallyVisible())
        return getChar();

      auto mangled =
          mangleCanonicalTypeName(astContext, clang::QualType(enumType, 0));
      if (!mangled)
        return getChar();

      uint64_t enumSize =
          newStructPathTBAA ? astContext.getTypeSizeInChars(qty).getQuantity() : 0;
      return cir::TBAAScalarAttr::get(mlirContext, *mangled,
                                      cir::VoidType::get(mlirContext),
                                      /*parent=*/nullptr, enumSize);
    }

    if (const auto *bitIntType = llvm::dyn_cast<clang::BitIntType>(type)) {
      llvm::SmallString<32> outName;
      llvm::raw_svector_ostream out(outName);
      out << "_BitInt(" << bitIntType->getNumBits() << ')';
      uint64_t size =
          newStructPathTBAA ? astContext.getTypeSizeInChars(qty).getQuantity() : 0;
      return cir::TBAAScalarAttr::get(mlirContext, outName,
                                      cir::VoidType::get(mlirContext),
                                      /*parent=*/nullptr, size);
    }

    return getChar();
  }

  cir::TBAAAttr getAccessTagInfo(cir::TBAAAttr accessType, uint64_t size) const {
    if (!accessType)
      return {};

    if (newStructPathTBAA)
      return cir::TBAATagAttr::get(mlirContext, accessType, accessType,
                                   /*offset=*/0, size);
    return accessType;
  }

  mlir::MLIRContext *mlirContext;
  const clang::CodeGenOptions &codeGenOpts;
  const clang::LangOptions &langOpts;
  bool newStructPathTBAA;
  llvm::SmallVector<cir::TBAAScalarAttr, 4> anyPtrs;
};

mlir::Value buildAddressAtOffset(LowerFunction &LF, mlir::Value addr,
                                 const ABIArgInfo &info) {
  if (unsigned offset = info.getDirectOffset()) {
    auto &rewriter = LF.getRewriter();
    auto loc = addr.getLoc();
    // Preserve the address space of the source pointer.
    auto srcPtrTy = mlir::cast<PointerType>(addr.getType());
    auto addrSpace = srcPtrTy.getAddrSpace();
    // Cast to byte pointer.
    auto u8Ty = IntType::get(rewriter.getContext(), 8, /*isSigned=*/false);
    auto u8PtrTy = PointerType::get(u8Ty, addrSpace);
    auto bytePtr = CastOp::create(rewriter, loc, u8PtrTy, CastKind::bitcast,
                                   addr);
    // Advance by offset bytes.
    auto offsetVal = cir::ConstantOp::create(
        rewriter, loc,
        cir::IntAttr::get(IntType::get(rewriter.getContext(), 64, false),
                          offset));
    auto offsetPtr =
        PtrStrideOp::create(rewriter, loc, u8PtrTy, bytePtr, offsetVal,
            cir::GEPNoWrapFlags::inbounds);
    // Cast to the coerce type pointer.
    auto coercePtrTy = PointerType::get(info.getCoerceToType(), addrSpace);
    return CastOp::create(rewriter, loc, coercePtrTy, CastKind::bitcast,
                          offsetPtr);
  }
  return addr;
}

// Forward declaration — defined below.
AllocaOp createTmpAlloca(LowerFunction &LF, mlir::Location loc,
                         mlir::Type ty, unsigned explicitAlign = 0);
cir::AllocaOp findAlloca(mlir::Operation *op);

mlir::Value createCoercedBitcast(mlir::Value Src, mlir::Type DestTy,
                                 LowerFunction &CGF) {
  if (auto load = Src.getDefiningOp<cir::LoadOp>()) {
    // Peek through the load and bitcast the underlying address, preserving
    // its address space. Look through an address-space cast to the underlying
    // pointer first: the address-space cast may later be folded away, and if we
    // built the coerced pointer in its (generic) result address space the
    // bitcast would then disagree with the underlying pointer's real address
    // space.
    mlir::Value addr = load.getAddr();
    if (auto asCast = addr.getDefiningOp<cir::CastOp>())
      if (asCast.getKind() == CastKind::address_space)
        addr = asCast.getSrc();
    auto srcPtrTy = mlir::cast<PointerType>(addr.getType());
    auto destPtrTy = PointerType::get(DestTy, srcPtrTy.getAddrSpace());
    if (srcPtrTy == destPtrTy)
      return addr;
    return CastOp::create(CGF.getRewriter(), Src.getLoc(), destPtrTy,
                          CastKind::bitcast, addr);
  }

  // If the source is a pointer, preserve its address space.
  if (auto srcPtrTy = mlir::dyn_cast<PointerType>(Src.getType())) {
    auto destPtrTy = PointerType::get(DestTy, srcPtrTy.getAddrSpace());
    if (srcPtrTy == destPtrTy)
      return Src;
    return CastOp::create(CGF.getRewriter(), Src.getLoc(), destPtrTy,
                          CastKind::bitcast, Src);
  }

  // The source is a non-pointer, non-load value (e.g. a vector from a
  // shuffle). We cannot bitcast a value directly to a pointer, so
  // materialize it through memory: store to a temp alloca, then
  // bitcast the alloca pointer to the destination type.
  auto tmpAlloca = createTmpAlloca(CGF, Src.getLoc(), Src.getType());
  StoreOp::create(CGF.getRewriter(), Src.getLoc(), Src, tmpAlloca,
                  /*is_volatile=*/false, /*is_nontemporal=*/false,
                  /*alignment=*/tmpAlloca.getAlignmentAttr(),
                  /*sync_scope=*/cir::SyncScopeKindAttr{},
                  /*mem_order=*/cir::MemOrderAttr{},
                  /*tbaa=*/cir::TBAAAttr{});
  auto destPtrTy = PointerType::get(DestTy);
  if (tmpAlloca.getResult().getType() == destPtrTy)
    return tmpAlloca.getResult();
  return CastOp::create(CGF.getRewriter(), Src.getLoc(), destPtrTy,
                        CastKind::bitcast, tmpAlloca.getResult());
}

// FIXME(cir): Create a custom rewriter class to abstract this away.
mlir::Value createBitcast(mlir::Value Src, mlir::Type Ty, LowerFunction &LF) {
  return CastOp::create(LF.getRewriter(), Src.getLoc(), Ty, CastKind::bitcast,
                        Src);
}

static mlir::Value createVectorCoercionIndex(LowerFunction &LF,
                                             mlir::Location loc) {
  auto u64Ty = IntType::get(LF.getRewriter().getContext(), 64,
                            /*isSigned=*/false);
  return ConstantOp::create(LF.getRewriter(), loc, IntAttr::get(u64Ty, 0));
}

static bool containsScalableCoercionElement(mlir::Type ty) {
  if (auto vecTy = mlir::dyn_cast<VectorType>(ty))
    return vecTy.getIsScalable();

  if (auto arrTy = mlir::dyn_cast<ArrayType>(ty))
    return containsScalableCoercionElement(arrTy.getElementType());

  if (auto recTy = mlir::dyn_cast<RecordType>(ty))
    return llvm::any_of(recTy.getMembers(), containsScalableCoercionElement);

  if (auto opaqueTy = mlir::dyn_cast<OpaqueType>(ty))
    return opaqueTy.getTag() == OpaqueType::getAArch64SVCountTag();

  return false;
}

static std::optional<mlir::Value>
coerceFixedToScalableVector(LowerFunction &LF, mlir::Location loc,
                            mlir::Value value, VectorType dstVecTy) {
  auto srcVecTy = mlir::dyn_cast<VectorType>(value.getType());
  if (!srcVecTy || srcVecTy.getIsScalable() || !dstVecTy.getIsScalable())
    return std::nullopt;

  auto srcElemTy = srcVecTy.getElementType();
  auto dstElemTy = dstVecTy.getElementType();
  mlir::Type insertTy = dstVecTy;

  if (mlir::isa<BoolType>(dstElemTy)) {
    auto srcIntTy = mlir::dyn_cast<IntType>(srcElemTy);
    if (!srcIntTy || srcIntTy.getWidth() != 8)
      return std::nullopt;
    insertTy = VectorType::get(srcElemTy,
                               llvm::divideCeil<uint64_t>(dstVecTy.getSize(), 8),
                               /*is_scalable=*/true);
  } else if (srcElemTy != dstElemTy) {
    return std::nullopt;
  }

  auto &rw = LF.getRewriter();
  auto zeroIndex = createVectorCoercionIndex(LF, loc);
  auto poison = ConstantOp::create(rw, loc, PoisonAttr::get(insertTy));
  mlir::Value result = LLVMIntrinsicCallOp::create(
                           rw, loc,
                           mlir::StringAttr::get(rw.getContext(),
                                                 "vector.insert"),
                           insertTy,
                           mlir::ValueRange{poison.getResult(), value,
                                            zeroIndex})
                           .getResult();
  if (result.getType() != dstVecTy)
    result = CastOp::create(rw, loc, dstVecTy, CastKind::bitcast, result);
  return result;
}

static std::pair<mlir::Value, bool>
coerceScalableToFixedVector(LowerFunction &LF, VectorType dstVecTy,
                            VectorType srcVecTy, mlir::Value value) {
  if (!srcVecTy.getIsScalable() || dstVecTy.getIsScalable())
    return {value, false};

  auto &rw = LF.getRewriter();
  auto loc = value.getLoc();
  auto srcElemTy = srcVecTy.getElementType();
  auto dstElemTy = dstVecTy.getElementType();

  if (mlir::isa<BoolType>(srcElemTy)) {
    auto dstIntTy = mlir::dyn_cast<IntType>(dstElemTy);
    if (!dstIntTy || dstIntTy.getWidth() != 8 ||
        srcVecTy.getSize() % 8 != 0)
      return {value, false};

    auto extractSrcTy =
        VectorType::get(dstElemTy, srcVecTy.getSize() / 8,
                        /*is_scalable=*/true);
    value = CastOp::create(rw, loc, extractSrcTy, CastKind::bitcast, value);
    srcVecTy = extractSrcTy;
    srcElemTy = srcVecTy.getElementType();
  }

  if (srcElemTy != dstElemTy)
    return {value, false};

  auto zeroIndex = createVectorCoercionIndex(LF, loc);
  value = LLVMIntrinsicCallOp::create(
              rw, loc,
              mlir::StringAttr::get(rw.getContext(), "vector.extract"),
              dstVecTy, mlir::ValueRange{value, zeroIndex})
              .getResult();
  return {value, true};
}

static std::optional<mlir::Value>
tryCoerceVectorValue(LowerFunction &LF, mlir::Value value,
                     mlir::Type dstTy) {
  auto srcVecTy = mlir::dyn_cast<VectorType>(value.getType());
  auto dstVecTy = mlir::dyn_cast<VectorType>(dstTy);
  if (!srcVecTy || !dstVecTy || value.getType() == dstTy)
    return std::nullopt;

  if (auto coerced = coerceFixedToScalableVector(LF, value.getLoc(), value,
                                                 dstVecTy))
    return coerced;

  auto [coerced, extracted] =
      coerceScalableToFixedVector(LF, dstVecTy, srcVecTy, value);
  if (extracted)
    return coerced;

  return std::nullopt;
}

/// Given a record pointer that we are accessing some number of bytes out of it,
/// try to gep into the record to get at its inner goodness.  Dive as deep as
/// possible without entering an element with an in-memory size smaller than
/// DstSize.
mlir::Value enterRecordPointerForCoercedAccess(mlir::Value SrcPtr,
                                               RecordType SrcSTy,
                                               uint64_t DstSize,
                                               LowerFunction &CGF) {
  // We can't dive into a zero-element record.
  if (SrcSTy.getNumElements() == 0)
    return SrcPtr;

  mlir::Type FirstElt = SrcSTy.getMembers()[0];

  if (SrcSTy.isUnion())
    FirstElt = SrcSTy.getLargestMember(CGF.LM.getDataLayout().layout);

  // If the first elt is at least as large as what we're looking for, or if the
  // first element is the same size as the whole record, we can enter it. The
  // comparison must be made on the store size and not the alloca size. Using
  // the alloca size may overstate the size of the load.
  uint64_t FirstEltSize = CGF.LM.getDataLayout().getTypeStoreSize(FirstElt);
  if (FirstEltSize < DstSize &&
      FirstEltSize < CGF.LM.getDataLayout().getTypeStoreSize(SrcSTy))
    return SrcPtr;

  auto &rw = CGF.getRewriter();
  if (mlir::isa<RecordType>(SrcPtr.getType())) {
    // For non-record leaf fields, keep the original aggregate load so the
    // final coercion can reload directly from the source slot instead of
    // materializing an intermediate typed aggregate load. We return early
    // in two cases:
    // (a) The whole record already covers the destination width — a scalar
    //     leaf load adds no value over the aggregate memcpy/bitcast path.
    // (b) The first element is smaller than the destination AND is not an
    //     integer/pointer type.  In OGCG this is a GEP (no load), but in
    //     CIR we'd emit a typed load whose value cannot be coerced to the
    //     destination via int/ptr extension — the caller will fall through
    //     to the memcpy path and the load becomes dead.
    if (!mlir::isa<RecordType>(FirstElt) &&
        (CGF.LM.getDataLayout().getTypeStoreSize(SrcSTy) >= DstSize ||
         (FirstEltSize < DstSize &&
          !mlir::isa<IntType>(FirstElt) &&
          !mlir::isa<PointerType>(FirstElt))))
      return SrcPtr;

    mlir::Value addr;
    if (auto load = SrcPtr.getDefiningOp<cir::LoadOp>()) {
      addr = load.getAddr();
    } else {
      // The value is a direct RecordType SSA value (e.g., from a call return,
      // extractvalue, or undef constant) — not backed by a pointer. Allocate a
      // temporary, store the value, and use the temporary's address.
      auto tmp = createTmpAlloca(CGF, SrcPtr.getLoc(), SrcPtr.getType());
      StoreOp::create(rw, SrcPtr.getLoc(), SrcPtr, tmp,
                      /*is_volatile=*/false, /*is_nontemporal=*/false,
                      /*alignment=*/tmp.getAlignmentAttr(),
                      /*sync_scope=*/cir::SyncScopeKindAttr{},
                      /*mem_order=*/cir::MemOrderAttr{},
                      /*tbaa=*/cir::TBAAAttr{});
      addr = tmp;
    }
    cir_cconv_assert(mlir::isa<PointerType>(addr.getType()));
    // Preserve the address space of the source pointer when creating the
    // coerced pointer type.
    auto addrSpace = mlir::cast<PointerType>(addr.getType()).getAddrSpace();
    auto ptrTy = PointerType::get(FirstElt, addrSpace);
    // We can not use getMemberOp here since we need a pointer to the first
    // element. And in the case of unions we pick a type of the largest elt,
    // that may or may not be the first one. Thus, getMemberOp verification
    // may fail.
    auto cast = createBitcast(addr, ptrTy, CGF);
    mlir::IntegerAttr alignAttr;
    if (auto alloca = findAlloca(cast.getDefiningOp()))
      alignAttr = alloca.getAlignmentAttr();
    SrcPtr = LoadOp::create(rw, SrcPtr.getLoc(), cast,
                            /*isDeref=*/false, /*is_volatile=*/false,
                            /*is_nontemporal=*/false,
                            /*alignment=*/alignAttr,
                            /*sync_scope=*/cir::SyncScopeKindAttr{},
                            /*mem_order=*/cir::MemOrderAttr{},
                            /*tbaa=*/cir::TBAAAttr{});
  }

  if (auto sty = mlir::dyn_cast<RecordType>(SrcPtr.getType()))
    return enterRecordPointerForCoercedAccess(SrcPtr, sty, DstSize, CGF);

  return SrcPtr;
}

/// Convert a value Val to the specific Ty where both
/// are either integers or pointers.  This does a truncation of the value if it
/// is too large or a zero extension if it is too small.
///
/// This behaves as if the value were coerced through memory, so on big-endian
/// targets the high bits are preserved in a truncation, while little-endian
/// targets preserve the low bits.
static mlir::Value coerceIntOrPtrToIntOrPtr(mlir::Value val, mlir::Type typ,
                                            LowerFunction &CGF) {
  if (val.getType() == typ)
    return val;

  auto &bld = CGF.getRewriter();

  if (mlir::isa<PointerType>(val.getType())) {
    // If this is Pointer->Pointer avoid conversion to and from int.
    if (mlir::isa<PointerType>(typ))
      return CastOp::create(bld, val.getLoc(), typ, CastKind::bitcast, val);

    // Convert the pointer to an integer so we can play with its width.
    val = CastOp::create(bld, val.getLoc(), typ, CastKind::ptr_to_int, val);
  }

  auto dstIntTy = typ;
  if (mlir::isa<PointerType>(dstIntTy))
    dstIntTy = CGF.LM.getDataLayout().getIntPtrType(dstIntTy);

  if (val.getType() != dstIntTy) {
    const auto &layout = CGF.LM.getDataLayout();
    if (layout.isBigEndian()) {
      // Preserve the high bits on big-endian targets.
      // That is what memory coercion does.
      uint64_t srcSize = layout.getTypeSizeInBits(val.getType());
      uint64_t dstSize = layout.getTypeSizeInBits(dstIntTy);
      uint64_t diff = srcSize > dstSize ? srcSize - dstSize : dstSize - srcSize;
      auto loc = val.getLoc();
      if (srcSize > dstSize) {
        auto intAttr = IntAttr::get(val.getType(), diff);
        auto amount = ConstantOp::create(bld, loc, intAttr);
        val = ShiftOp::create(bld, loc, val.getType(), val, amount, false);
        val = CastOp::create(bld, loc, dstIntTy, CastKind::integral, val);
      } else {
        // Widen then shift left to preserve high bits.  OGCG uses
        // CreateZExt unconditionally (CGCall.cpp:1299), so force zext by
        // casting signed sources through their unsigned counterpart.
        if (auto srcIntTy = mlir::dyn_cast<IntType>(val.getType())) {
          if (srcIntTy.isSigned()) {
            auto unsignedTy = IntType::get(
                bld.getContext(), srcIntTy.getWidth(), /*isSigned=*/false);
            val = CastOp::create(bld, loc, unsignedTy, CastKind::integral,
                                 val);
          }
        }
        val = CastOp::create(bld, loc, dstIntTy, CastKind::integral, val);
        auto intAttr = IntAttr::get(val.getType(), diff);
        auto amount = ConstantOp::create(bld, loc, intAttr);
        val = ShiftOp::create(bld, loc, val.getType(), val, amount, true);
      }
    } else {
      // Little-endian targets preserve the low bits. No shifts required.
      // ABI coercion padding bits must be zero-filled, matching OGCG's
      // unconditional CreateZExt.  Force zext by casting signed sources
      // through their unsigned counterpart first.
      if (auto srcIntTy = mlir::dyn_cast<IntType>(val.getType())) {
        if (srcIntTy.isSigned()) {
          auto unsignedTy = IntType::get(bld.getContext(),
                                         srcIntTy.getWidth(), /*isSigned=*/false);
          val = CastOp::create(bld, val.getLoc(), unsignedTy,
                               CastKind::integral, val);
        }
      }
      val =
          CastOp::create(bld, val.getLoc(), dstIntTy, CastKind::integral, val);
    }
  }

  if (mlir::isa<PointerType>(typ))
    val = CastOp::create(bld, val.getLoc(), typ, CastKind::int_to_ptr, val);

  return val;
}

AllocaOp createTmpAlloca(LowerFunction &LF, mlir::Location loc,
                         mlir::Type ty, unsigned explicitAlign) {
  auto &rw = LF.getRewriter();
  mlir::PatternRewriter::InsertionGuard guard(rw);

  // Insert the alloca at the beginning of the current block. This ensures
  // it dominates all its uses within the same block after CFG flattening.
  // The previous approach of inserting at the function entry block failed
  // because after flattening, the alloca and its uses ended up in the same
  // block with the alloca AFTER the uses.
  //
  // However, if the block starts with a cir.label op, we must insert AFTER
  // it — cir.label is required to be the first operation in its block.
  mlir::Block *block = rw.getBlock();
  if (!block->empty() && mlir::isa<cir::LabelOp>(block->front()))
    rw.setInsertionPointAfter(&block->front());
  else
    rw.setInsertionPointToStart(block);

  unsigned alignVal = explicitAlign
                          ? explicitAlign
                          : LF.LM.getDataLayout().getABITypeAlign(ty).value();
  auto alignAttr = rw.getI64IntegerAttr(alignVal);
  auto ptrTy = PointerType::get(ty);
  return AllocaOp::create(rw, loc, ptrTy, ty, "tmp", alignAttr);
}

MemCpyOp createMemCpy(LowerFunction &LF, mlir::Value dst, mlir::Value src,
                      uint64_t len, unsigned dstAlign = 0,
                      unsigned srcAlign = 0,
                      std::optional<mlir::Location> loc = std::nullopt,
                      mlir::Type copiedTy = {}) {
  cir_cconv_assert(mlir::isa<PointerType>(src.getType()));
  cir_cconv_assert(mlir::isa<PointerType>(dst.getType()));

  auto *ctxt = LF.getRewriter().getContext();
  auto &rw = LF.getRewriter();

  // Preserve address space when casting to void pointer — a bitcast must
  // stay within the same address space.
  auto toVoidPtr = [&](mlir::Value v) -> mlir::Value {
    auto ptrTy = mlir::cast<PointerType>(v.getType());
    if (mlir::isa<VoidType>(ptrTy.getPointee()))
      return v;
    auto voidPtr =
        PointerType::get(cir::VoidType::get(ctxt), ptrTy.getAddrSpace());
    return createBitcast(v, voidPtr, LF);
  };

  src = toVoidPtr(src);
  dst = toVoidPtr(dst);

  // The memcpy length must use the target's pointer-sized integer so that the
  // lowered llvm.memcpy picks the matching .i32/.i64 intrinsic variant: i32 on
  // 32-bit targets, i64 on 64-bit targets. This mirrors OGCG, which builds the
  // ABI-coercion memcpy size with CodeGenFunction::IntPtrTy. A hardcoded i64
  // here emits llvm.memcpy.p0.p0.i64 on a 32-bit datalayout, diverging from
  // classic CodeGen.
  auto ptrTy = mlir::cast<PointerType>(dst.getType());
  unsigned ptrWidth = LF.LM.getDataLayout().getPointerTypeSizeInBits(ptrTy);
  auto lenTy = IntType::get(ctxt, ptrWidth, /*isSigned=*/false);
  mlir::Location memcpyLoc = loc.value_or(src.getLoc());
  auto lenVal = ConstantOp::create(rw, memcpyLoc, IntAttr::get(lenTy, len));
  auto memcpyOp = MemCpyOp::create(rw, memcpyLoc, dst, src, lenVal);
  // Forward alignment as discardable attrs for LowerToLLVM to pick up.
  if (dstAlign)
    memcpyOp->setAttr("cir.dst_align",
                       rw.getI64IntegerAttr(dstAlign));
  if (srcAlign)
    memcpyOp->setAttr("cir.src_align",
                       rw.getI64IntegerAttr(srcAlign));
  if (auto recordTy = mlir::dyn_cast_if_present<RecordType>(copiedTy)) {
    AggregateCopyTBAABuilder tbaaBuilder(LF);
    if (auto tbaaStruct = tbaaBuilder.getTBAAStructInfo(recordTy))
      memcpyOp->setAttr("cir.tbaa_struct", tbaaStruct);
  }
  return memcpyOp;
}

/// Extract alignment from a value that may be an AllocaOp result.
static unsigned getAllocaAlign(mlir::Value v) {
  if (auto alloca = v.getDefiningOp<cir::AllocaOp>())
    if (auto alignAttr = alloca.getAlignmentAttr())
      return alignAttr.getInt();
  return 0;
}

/// Match OGCG's getOrEnforceKnownAlignment behavior for local stack slots.
///
/// If \p v is backed by a CIR alloca, try to raise that alloca's explicit
/// alignment to \p prefAlign as long as doing so does not exceed the target's
/// natural stack alignment. This avoids introducing an unnecessary byval copy
/// when the original stack slot can simply be realigned in place.
static unsigned getOrEnforceAllocaAlign(mlir::Value v, unsigned prefAlign,
                                        LowerFunction &LF) {
  while (auto castOp = v.getDefiningOp<cir::CastOp>()) {
    if (!mlir::isa<PointerType>(castOp.getSrc().getType()) ||
        !mlir::isa<PointerType>(castOp.getResult().getType()))
      break;
    v = castOp.getSrc();
  }

  auto alloca = v.getDefiningOp<cir::AllocaOp>();
  if (!alloca)
    return 0;

  unsigned currentAlign = alloca.getAlignmentAttr()
                              ? alloca.getAlignmentAttr().getInt()
                              : 0;
  if (!prefAlign || prefAlign <= currentAlign)
    return currentAlign;

  uint64_t stackAlignBits = LF.LM.getDataLayout().layout.getStackAlignment();
  if (stackAlignBits && 8ull * prefAlign > stackAlignBits)
    return currentAlign;

  alloca.setAlignmentAttr(LF.getRewriter().getI64IntegerAttr(prefAlign));
  return prefAlign;
}

static IntType getBitIntStorageType(LowerFunction &LF, mlir::Type ty,
                                    bool isBitIntType) {
  if (!isBitIntType)
    return {};

  auto intTy = mlir::dyn_cast<IntType>(ty);
  if (!intTy)
    return {};

  unsigned storageBits = LF.getTarget().getBitIntWidth(intTy.getWidth());
  if (storageBits == intTy.getWidth())
    return {};

  return IntType::get(LF.getRewriter().getContext(), storageBits,
                      intTy.isSigned());
}

static unsigned getBitIntStorageAlign(LowerFunction &LF, mlir::Type ty,
                                      bool isBitIntType) {
  if (!isBitIntType)
    return 0;

  auto intTy = mlir::dyn_cast<IntType>(ty);
  if (!intTy)
    return 0;

  return LF.getTarget().getBitIntAlign(intTy.getWidth()) /
         LF.getTarget().getCharWidth();
}

static mlir::Value bitcastAddressForPointeeType(LowerFunction &LF,
                                                mlir::Location loc,
                                                mlir::Value addr,
                                                mlir::Type pointeeTy) {
  auto ptrTy = mlir::cast<PointerType>(addr.getType());
  auto newPtrTy = PointerType::get(pointeeTy, ptrTy.getAddrSpace());
  if (ptrTy == newPtrTy)
    return addr;

  return CastOp::create(LF.getRewriter(), loc, newPtrTy, CastKind::bitcast,
                        addr);
}

static mlir::Value adaptReplacementAddrForOriginalAddr(LowerFunction &LF,
                                                       mlir::Location loc,
                                                       mlir::Value replacement,
                                                       mlir::Value original) {
  auto originalPtrTy = mlir::dyn_cast<PointerType>(original.getType());
  if (!originalPtrTy)
    return replacement;
  return bitcastAddressForPointeeType(LF, loc, replacement,
                                      originalPtrTy.getPointee());
}

/// Fallback for replacing an original function argument when
/// findArgInitStoreChain returns null.
///
/// This handles two scenarios that the standard init-store-chain pattern
/// does not cover:
///   1. The argument has zero uses (unused parameter).
///   2. The argument has multiple uses, or a single use that is not a
///      simple store (e.g. the argument is used directly in a call or
///      is stored via a pattern findArgInitStoreChain doesn't recognise).
///
/// \p replacement is the new alloca (or pointer value) that should take
/// over from the original arg's alloca.  When the original arg was stored
/// to one or more allocas we find them, RAUW their addresses with
/// \p replacement, and erase the dead stores and allocas.  Any remaining
/// non-store uses of the original arg are replaced with an undef so that
/// the "Missing RAUW?" assertion in generateCode is satisfied.
static void replaceArgWithoutInitStoreChain(LowerFunction &LF,
                                            mlir::PatternRewriter &rewriter,
                                            mlir::Location loc,
                                            mlir::Value originalArg,
                                            mlir::Value replacement) {
  if (originalArg.use_empty())
    return;

  // Collect stores of the arg into allocas. There may be zero, one, or
  // several (the last case violates the one-use assumption but does occur
  // in practice).
  llvm::SmallVector<cir::StoreOp, 2> argStores;
  llvm::SmallVector<cir::CastOp, 2> argCasts;
  for (mlir::OpOperand &use : originalArg.getUses()) {
    if (auto store = mlir::dyn_cast<cir::StoreOp>(use.getOwner())) {
      if (store.getValue() == originalArg)
        argStores.push_back(store);
    } else if (auto cast = mlir::dyn_cast<cir::CastOp>(use.getOwner())) {
      argCasts.push_back(cast);
    }
  }

  // Also find stores that go through a cast (cast arg -> store).
  for (auto cast : argCasts) {
    for (mlir::OpOperand &use : cast.getResult().getUses()) {
      if (auto store = mlir::dyn_cast<cir::StoreOp>(use.getOwner())) {
        if (store.getValue() == cast.getResult())
          argStores.push_back(store);
      }
    }
  }

  // For each store, RAUW its target alloca address with the replacement,
  // then erase the dead store (and the alloca if it becomes unused).
  for (auto store : argStores) {
    auto addr = store.getAddr();
    auto replacementAddr =
        adaptReplacementAddrForOriginalAddr(LF, loc, replacement, addr);
    rewriter.replaceAllUsesWith(addr, replacementAddr);
    rewriter.eraseOp(store);
    // The original addr (alloca or cast of alloca) may now be dead.
    if (addr.use_empty())
      eraseDefOpThroughCast(rewriter, addr);
  }

  // Clean up casts whose results are now unused (the stores through them
  // were erased above).
  for (auto cast : argCasts) {
    if (cast.getResult().use_empty())
      rewriter.eraseOp(cast);
  }

  // If the original arg still has remaining uses (e.g. it was passed
  // directly to another op without going through a store), replace those
  // with an undef. The parameter is being ABI-lowered away, so a
  // concrete value cannot survive; undef is the safe conservative choice
  // matching the Ignore-case handling.
  if (!originalArg.use_empty()) {
    auto undef = cir::ConstantOp::create(
        rewriter, loc, cir::UndefAttr::get(originalArg.getType()));
    rewriter.replaceAllUsesWith(originalArg, undef);
    if (undef->use_empty())
      rewriter.eraseOp(undef);
  }
}

static mlir::Value canonicalizeBitIntToStorage(LowerFunction &LF,
                                               mlir::Location loc,
                                               mlir::Value value,
                                               IntType storageTy) {
  if (value.getType() == storageTy)
    return value;

  return CastOp::create(LF.getRewriter(), loc, storageTy, CastKind::integral,
                        value);
}

static mlir::Value canonicalizeBitIntStorageValue(LowerFunction &LF,
                                                  mlir::Location loc,
                                                  mlir::Value value,
                                                  mlir::Type semanticTy,
                                                  IntType storageTy) {
  mlir::Value semanticVal = value;
  if (semanticVal.getType() != semanticTy)
    semanticVal = CastOp::create(LF.getRewriter(), loc, semanticTy,
                                 CastKind::integral, semanticVal);

  return canonicalizeBitIntToStorage(LF, loc, semanticVal, storageTy);
}

static mlir::Value createBitIntStorageLoad(LowerFunction &LF,
                                           mlir::Location loc,
                                           mlir::Value addr,
                                           mlir::Type semanticTy,
                                           IntType storageTy,
                                           unsigned align) {
  auto &rw = LF.getRewriter();
  auto storageAddr = bitcastAddressForPointeeType(LF, loc, addr, storageTy);

  mlir::IntegerAttr alignAttr;
  if (align)
    alignAttr = rw.getI64IntegerAttr(align);
  auto loaded = LoadOp::create(rw, loc, storageAddr,
                               /*isDeref=*/false, /*is_volatile=*/false,
                               /*is_nontemporal=*/false,
                               /*alignment=*/alignAttr,
                               /*sync_scope=*/cir::SyncScopeKindAttr{},
                               /*mem_order=*/cir::MemOrderAttr{},
                               /*tbaa=*/cir::TBAAAttr{});
  if (semanticTy == storageTy)
    return loaded;

  return CastOp::create(rw, loc, semanticTy, CastKind::integral, loaded);
}

static void createBitIntStorageStore(LowerFunction &LF, mlir::Location loc,
                                     mlir::Value value, mlir::Value addr,
                                     mlir::Type semanticTy,
                                     IntType storageTy,
                                     mlir::IntegerAttr alignAttr,
                                     cir::TBAAAttr tbaa = {}) {
  auto &rw = LF.getRewriter();
  auto storageAddr = bitcastAddressForPointeeType(LF, loc, addr, storageTy);
  auto storageVal =
      canonicalizeBitIntStorageValue(LF, loc, value, semanticTy, storageTy);

  StoreOp::create(rw, loc, storageVal, storageAddr,
                  /*is_volatile=*/false, /*is_nontemporal=*/false,
                  /*alignment=*/alignAttr,
                  /*sync_scope=*/cir::SyncScopeKindAttr{},
                  /*mem_order=*/cir::MemOrderAttr{}, /*tbaa=*/tbaa);
}

cir::AllocaOp findAlloca(mlir::Operation *op) {
  if (!op)
    return {};

  if (auto al = mlir::dyn_cast<cir::AllocaOp>(op)) {
    return al;
  } else if (auto ret = mlir::dyn_cast<cir::ReturnOp>(op)) {
    auto vals = ret.getInput();
    if (vals.size() == 1)
      return findAlloca(vals[0].getDefiningOp());
  } else if (auto load = mlir::dyn_cast<cir::LoadOp>(op)) {
    return findAlloca(load.getAddr().getDefiningOp());
  } else if (auto cast = mlir::dyn_cast<cir::CastOp>(op)) {
    return findAlloca(cast.getSrc().getDefiningOp());
  } else if (auto stride = mlir::dyn_cast<cir::PtrStrideOp>(op)) {
    return findAlloca(stride.getBase().getDefiningOp());
  }

  return {};
}

static mlir::IntegerAttr getAddrAlignmentAttr(mlir::Value addr) {
  if (!addr)
    return {};
  if (auto alloca = findAlloca(addr.getDefiningOp()))
    return alloca.getAlignmentAttr();
  return {};
}

mlir::Value findAddr(mlir::Value v) {
  if (mlir::isa<cir::PointerType>(v.getType()))
    return v;

  auto op = v.getDefiningOp();
  if (!op || !mlir::isa<cir::CastOp, cir::LoadOp, cir::ReturnOp>(op))
    return {};

  return findAddr(op->getOperand(0));
}

static cir::LoadOp findLoadLike(mlir::Value v) {
  if (auto load = v.getDefiningOp<cir::LoadOp>())
    return load;

  if (auto cast = v.getDefiningOp<cir::CastOp>())
    return findLoadLike(cast.getSrc());

  return {};
}

static cir::AllocaOp findReturnAlloca(cir::FuncOp fn) {
  cir::AllocaOp retAlloca;
  fn.walk([&](ReturnOp ret) {
    if (!retAlloca)
      retAlloca = findAlloca(ret);
  });

  if (retAlloca)
    return retAlloca;

  fn.walk([&](cir::AllocaOp alloca) {
    if (!retAlloca && alloca.getName() == "__retval")
      retAlloca = alloca;
  });
  return retAlloca;
}

static bool tryElideSingleStoreReturnSlot(mlir::PatternRewriter &rewriter,
                                          cir::FuncOp fn) {
  cir::AllocaOp retAlloca = findReturnAlloca(fn);
  if (!retAlloca || retAlloca.getName() != "__retval")
    return false;

  // Check for unresolved symbolic gotos (cir.goto / cir.label). DominanceInfo
  // is unreliable with gotos because goto targets are symbolic (not CFG edges),
  // making label blocks appear to have "no predecessors" and causing domination
  // checks to be optimistically wrong. We still allow elision when gotos exist
  // IF the single store and all return-loads are in the same block as retAlloca
  // (trivial domination that doesn't rely on cross-block analysis).
  bool hasUnresolvedGotos = false;
  fn.walk([&](cir::GotoOp) { hasUnresolvedGotos = true; });

  llvm::SmallVector<std::pair<ReturnOp, cir::LoadOp>, 4> returnLoads;
  bool canElide = true;
  fn.walk([&](ReturnOp ret) {
    if (!canElide || ret.getInput().empty())
      return;

    cir::LoadOp load;
    if (!hasSingleUseLoadChain(ret.getInput().front(), load) ||
        findAlloca(ret) != retAlloca) {
      canElide = false;
      return;
    }

    returnLoads.emplace_back(ret, load);
  });
  if (!canElide || returnLoads.empty())
    return false;

  cir::StoreOp singleStore;
  unsigned loadUseCount = 0;
  for (mlir::OpOperand &use : retAlloca.getResult().getUses()) {
    mlir::Operation *user = use.getOwner();
    if (auto store = mlir::dyn_cast<cir::StoreOp>(user)) {
      if (store.getAddr() != retAlloca.getResult() || singleStore)
        return false;
      singleStore = store;
      continue;
    }

    auto load = mlir::dyn_cast<cir::LoadOp>(user);
    if (!load)
      return false;

    ++loadUseCount;
    if (!llvm::any_of(returnLoads, [&](const auto &returnLoad) {
          return returnLoad.second == load;
        }))
      return false;
  }
  if (!singleStore || loadUseCount != returnLoads.size())
    return false;

  mlir::Value storedValue = singleStore.getValue();
  if (hasUnresolvedGotos) {
    // With unresolved gotos, only elide when the store and all return-loads
    // are in the same block. This guarantees trivial domination without
    // relying on the unreliable cross-block DominanceInfo.
    mlir::Block *storeBlock = singleStore->getBlock();
    for (auto [ret, load] : returnLoads) {
      if (load->getBlock() != storeBlock || ret->getBlock() != storeBlock)
        return false;
    }
    // Also verify the stored value dominates within the block (defined before
    // the store). For block arguments or ops defined in the same block, check
    // ordering.
    if (auto *defOp = storedValue.getDefiningOp()) {
      if (defOp->getBlock() != storeBlock || !defOp->isBeforeInBlock(singleStore))
        return false;
    }
  } else {
    mlir::DominanceInfo domInfo(fn);
    for (auto [ret, load] : returnLoads) {
      if (!domInfo.dominates(singleStore.getOperation(), load.getOperation()) ||
          !domInfo.dominates(storedValue, ret.getOperation()))
        return false;
    }
  }

  for (auto [ret, load] : returnLoads) {
    mlir::Value retInput = ret.getInput().front();
    if (retInput != load.getResult())
      rewriter.replaceAllUsesWith(load.getResult(), storedValue);

    rewriter.setInsertionPoint(ret);
    rewriter.replaceOpWithNewOp<ReturnOp>(
        ret, retInput == load.getResult() ? storedValue : retInput);

    if (load.getResult().use_empty())
      rewriter.eraseOp(load);
  }

  bool onlyStoreUseRemains = llvm::all_of(
      retAlloca.getResult().getUses(), [&](mlir::OpOperand &use) {
        return use.getOwner() == singleStore.getOperation();
      });
  if (onlyStoreUseRemains)
    rewriter.eraseOp(singleStore);
  if (retAlloca.getResult().use_empty())
    rewriter.eraseOp(retAlloca);

  return true;
}

static bool hasExistingLifetimeMarker(mlir::Value addr) {
  for (mlir::Operation *user : addr.getUsers()) {
    if (mlir::isa<cir::LifetimeStartOp, cir::LifetimeEndOp>(user))
      return true;
    if (auto cast = mlir::dyn_cast<cir::CastOp>(user))
      if (hasExistingLifetimeMarker(cast.getResult()))
        return true;
  }
  return false;
}

static bool indirectArgLifetimeEndsInCallee(LowerModule &LM, mlir::Type argTy,
                                            const ABIArgInfo &argInfo) {
  if (!argInfo.isIndirect() && !argInfo.isIndirectAliased())
    return false;

  if (argInfo.isIndirect() && argInfo.getIndirectByVal())
    return true;

  if (auto recordTy = mlir::dyn_cast<cir::RecordType>(argTy)) {
    RecordABIFacts facts = LM.getRecordABIFacts(recordTy);
    if (facts.nonTrivialDestructor && !facts.destroyedInCallee)
      return false;
  }

  return true;
}

/// Create a store to \param Dst from \param Src where the source and
/// destination may have different types.
///
/// This safely handles the case when the src type is larger than the
/// destination type; the upper bits of the src will be lost.
void createCoercedStore(mlir::Value Src, mlir::Value Dst, bool DstIsVolatile,
                        mlir::Location StoreLoc, LowerFunction &CGF,
                        mlir::IntegerAttr DstAlignOverride = {}) {
  mlir::Type SrcTy = Src.getType();
  mlir::Type DstTy = Dst.getType();

  if (auto dstPtrTy = mlir::dyn_cast<PointerType>(DstTy))
    if (auto coerced = tryCoerceVectorValue(CGF, Src, dstPtrTy.getPointee())) {
      Src = *coerced;
      SrcTy = Src.getType();
    }

  // Get alignment for the destination stores. If a caller provides an explicit
  // alignment override (e.g. from the original store that already accounts for
  // the pointer's offset within an array), use that. Otherwise fall back to the
  // alloca's alignment, matching OGCG's Address alignment behavior.
  mlir::IntegerAttr dstAlignAttr = DstAlignOverride;
  if (!dstAlignAttr)
    if (auto alloca = findAlloca(Dst.getDefiningOp()))
      dstAlignAttr = alloca.getAlignmentAttr();

  if (SrcTy == DstTy) {
    // Types already match (e.g. self-referential struct with a single pointer
    // member: the coerced type is ptr<Node> and the alloca is also ptr<Node>).
    // Bitcast the destination to ptr<SrcTy> and store directly, matching OG
    // codegen's CreateCoercedStore which falls through to a simple store when
    // SrcTy == Dst.getElementType().
    Dst = createCoercedBitcast(Dst, SrcTy, CGF);
    auto &bld = CGF.getRewriter();
    StoreOp::create(bld, Dst.getLoc(), Src, Dst, DstIsVolatile,
                    /*is_nontemporal=*/false,
                    /*alignment=*/dstAlignAttr,
                    /*sync_scope=*/cir::SyncScopeKindAttr{},
                    /*mem_order=*/cir::MemOrderAttr{},
                    /*tbaa=*/cir::TBAAAttr{});
    return;
  }

  llvm::TypeSize SrcSize = CGF.LM.getDataLayout().getTypeAllocSize(SrcTy);
  auto dstPtrTy = mlir::dyn_cast<PointerType>(DstTy);

  if (dstPtrTy)
    if (auto dstSTy = mlir::dyn_cast<RecordType>(dstPtrTy.getPointee()))
      if (SrcTy != dstSTy) {
        Dst = enterRecordPointerForCoercedAccess(Dst, dstSTy,
                                                 SrcSize.getFixedValue(), CGF);
        // Recompute dstPtrTy after struct diving — the destination pointer
        // may now point to a sub-element with a different type and size.
        DstTy = Dst.getType();
        dstPtrTy = mlir::dyn_cast<PointerType>(DstTy);
      }

  auto &layout = CGF.LM.getDataLayout();
  llvm::TypeSize DstSize = dstPtrTy
                               ? layout.getTypeAllocSize(dstPtrTy.getPointee())
                               : layout.getTypeAllocSize(DstTy);

  if (SrcSize.isScalable() || SrcSize <= DstSize) {
    if (mlir::isa<IntType>(SrcTy) && dstPtrTy &&
        mlir::isa<PointerType>(dstPtrTy.getPointee()) &&
        SrcSize == layout.getTypeAllocSize(dstPtrTy.getPointee())) {
      // If the value is supposed to be a pointer, convert it before storing.
      Src = coerceIntOrPtrToIntOrPtr(Src, dstPtrTy.getPointee(), CGF);
      StoreOp::create(CGF.getRewriter(), Dst.getLoc(), Src, Dst, DstIsVolatile,
                      /*is_nontemporal=*/false,
                      /*alignment=*/dstAlignAttr,
                      /*sync_scope=*/cir::SyncScopeKindAttr{},
                      /*mem_order=*/cir::MemOrderAttr{},
                      /*tbaa=*/cir::TBAAAttr{});
    } else if (auto STy = mlir::dyn_cast<RecordType>(SrcTy);
               STy && !STy.isUnion()) {
      // Prefer scalar stores to first-class aggregate stores.
      // Unions are excluded: their members overlap, so they cannot be
      // decomposed member-by-member (cir.extract_member rejects unions).
      // They fall through to the whole-value coerced store below.
      // OGCG uses CreateStructGEP which adjusts alignment based on the
      // element's offset within the struct. We must do the same: compute
      // the base alignment and reduce it for elements at non-zero offsets.
      auto &bld = CGF.getRewriter();
      auto &dataLayout = CGF.LM.getDataLayout();
      auto dstAddr = createCoercedBitcast(Dst, SrcTy, CGF);
      uint64_t baseAlign = dstAlignAttr
                               ? dstAlignAttr.getValue().getZExtValue()
                               : dataLayout.getABITypeAlign(STy).value();
      uint64_t offsetAfterPrevious = 0;
      auto dstAddrSpace =
          mlir::cast<PointerType>(dstAddr.getType()).getAddrSpace();
      for (unsigned i = 0, e = STy.getNumElements(); i != e; ++i) {
        mlir::Type elemTy = STy.getMembers()[i];
        auto eltPtr = GetMemberOp::create(
            bld, Dst.getLoc(), PointerType::get(elemTy, dstAddrSpace),
            dstAddr,
            /*name=*/"", /*index=*/i);
        auto elt = ExtractMemberOp::create(bld, Src.getLoc(), Src,
                                           static_cast<uint64_t>(i));
        // Compute alignment for this element: the effective alignment at
        // (base + offset) is min(baseAlign, 1 << ctz(offset)), matching
        // OGCG's Address::alignmentAtOffset via CreateStructGEP.
        uint64_t elemOffset = 0;
        if (!STy.isUnion()) {
          const uint64_t elemABIAlign =
              STy.getPacked() ? 1 : dataLayout.getABITypeAlign(elemTy).value();
          elemOffset = llvm::alignTo(offsetAfterPrevious, elemABIAlign);
          offsetAfterPrevious = elemOffset + dataLayout.getTypeAllocSize(elemTy);
        }
        uint64_t elemAlign = baseAlign;
        if (elemOffset != 0)
          elemAlign = std::min(baseAlign,
                               (uint64_t)llvm::MinAlign(baseAlign, elemOffset));
        mlir::IntegerAttr elemAlignAttr = mlir::IntegerAttr::get(
            mlir::IntegerType::get(bld.getContext(), 64), elemAlign);
        StoreOp::create(bld, Dst.getLoc(), elt, eltPtr, DstIsVolatile,
                        /*is_nontemporal=*/false,
                        /*alignment=*/elemAlignAttr,
                        /*sync_scope=*/cir::SyncScopeKindAttr{},
                        /*mem_order=*/cir::MemOrderAttr{},
                        /*tbaa=*/cir::TBAAAttr{});
      }
    } else {
      Dst = createCoercedBitcast(Dst, SrcTy, CGF);
      StoreOp::create(CGF.getRewriter(), Src.getLoc(), Src, Dst, DstIsVolatile,
                      /*is_nontemporal=*/false,
                      /*alignment=*/dstAlignAttr,
                      /*sync_scope=*/cir::SyncScopeKindAttr{},
                      /*mem_order=*/cir::MemOrderAttr{},
                      /*tbaa=*/cir::TBAAAttr{});
    }
  } else if (mlir::isa<IntType>(SrcTy)) {
    auto &bld = CGF.getRewriter();
    auto *ctxt = CGF.LM.getMLIRContext();
    auto dstIntTy = IntType::get(ctxt, DstSize.getFixedValue() * 8, false);
    Src = coerceIntOrPtrToIntOrPtr(Src, dstIntTy, CGF);
    // Preserve the address space from the destination pointer.
    auto dstAddrSpace =
        mlir::cast<PointerType>(Dst.getType()).getAddrSpace();
    auto ptrTy = PointerType::get(dstIntTy, dstAddrSpace);
    auto addr =
        CastOp::create(bld, Dst.getLoc(), ptrTy, CastKind::bitcast, Dst);
    StoreOp::create(bld, Dst.getLoc(), Src, addr,
                    /*is_volatile=*/false, /*is_nontemporal=*/false,
                    /*alignment=*/dstAlignAttr,
                    /*sync_scope=*/cir::SyncScopeKindAttr{},
                    /*mem_order=*/cir::MemOrderAttr{},
                    /*tbaa=*/cir::TBAAAttr{});
  } else {
    auto tmp = createTmpAlloca(CGF, StoreLoc, SrcTy);
    StoreOp::create(CGF.getRewriter(), Src.getLoc(), Src, tmp,
                    /*is_volatile=*/false, /*is_nontemporal=*/false,
                    /*alignment=*/tmp.getAlignmentAttr(),
                    /*sync_scope=*/cir::SyncScopeKindAttr{},
                    /*mem_order=*/cir::MemOrderAttr{},
                    /*tbaa=*/cir::TBAAAttr{});
    auto memcpyOp = createMemCpy(CGF, Dst, tmp, DstSize.getFixedValue(),
                                 getAllocaAlign(Dst), getAllocaAlign(tmp),
                                 StoreLoc);
    auto [atomGroup, atomRank] = extractSourceAtomMetadata(StoreLoc);
    if (atomGroup && atomRank == 1) {
      memcpyOp->setLoc(applySourceAtomMetadata(StoreLoc, atomGroup, 1));
      if (auto *srcDef = Src.getDefiningOp())
        srcDef->setLoc(applySourceAtomMetadata(srcDef->getLoc(), atomGroup, 2));
    }
  }
}

/// Coerces a \param Src value to a value of type \param Ty.
///
/// This safely handles the case when the src type is smaller than the
/// destination type; in this situation the values of bits which not present in
/// the src are undefined.
///
/// NOTE(cir): This method has partial parity with CGCall's CreateCoercedLoad.
/// Unlike the original codegen, this function does not emit a coerced load
/// since CIR's type checker wouldn't allow it. Instead, it casts the existing
/// ABI-agnostic value to it's ABI-aware counterpart. Nevertheless, we should
/// try to follow the same logic as the original codegen for correctness.
mlir::Value createCoercedValue(mlir::Value Src, mlir::Type Ty,
                               LowerFunction &CGF) {
  mlir::Type SrcTy = Src.getType();
  mlir::Value SrcAddrForMemCoercion;

  // If SrcTy and Ty are the same, just reuse the exising load.
  if (SrcTy == Ty)
    return Src;

  if (auto coerced = tryCoerceVectorValue(CGF, Src, Ty))
    return *coerced;

  // If it is the special boolean case, use the proper cast kind.
  // OGCG uses CreateZExt/CreateSExt for bool→int, not bitcast.
  if (mlir::isa<BoolType>(SrcTy) && mlir::isa<IntType>(Ty))
    return CastOp::create(CGF.getRewriter(), Src.getLoc(), Ty,
                          CastKind::bool_to_int, Src);
  if (mlir::isa<IntType>(SrcTy) && mlir::isa<BoolType>(Ty))
    return CastOp::create(CGF.getRewriter(), Src.getLoc(), Ty,
                          CastKind::int_to_bool, Src);

  llvm::TypeSize DstSize = CGF.LM.getDataLayout().getTypeAllocSize(Ty);

  if (auto SrcSTy = mlir::dyn_cast<RecordType>(SrcTy)) {
    if (mlir::Value SrcAddr = findAddr(Src)) {
      llvm::TypeSize SrcRecordSize =
          CGF.LM.getDataLayout().getTypeAllocSize(SrcSTy);
      // Match OGCG's address-based coercion: if the whole aggregate is smaller
      // than the destination width, keep copying from the source slot directly
      // instead of first diving into a leaf field and materializing a dead
      // scalar load before falling back to memcpy. Keep the existing leaf-dive
      // path for integer/pointer-ish aggregates, which still needs the scalar
      // coercion semantics (for example aarch64-be zext cases).
      if (!SrcRecordSize.isScalable() && !DstSize.isScalable() &&
          SrcRecordSize.getFixedValue() < DstSize.getFixedValue() &&
          isNoFPClassCompatibleType(SrcSTy) &&
          (mlir::isa<IntType>(Ty) || mlir::isa<PointerType>(Ty))) {
        SrcAddrForMemCoercion = SrcAddr;
      } else {
        Src = enterRecordPointerForCoercedAccess(
            Src, SrcSTy, DstSize.getFixedValue(), CGF);
        SrcTy = Src.getType();
      }
    } else {
      Src =
          enterRecordPointerForCoercedAccess(Src, SrcSTy,
                                             DstSize.getFixedValue(), CGF);
      SrcTy = Src.getType();
    }
  }

  llvm::TypeSize SrcSize = CGF.LM.getDataLayout().getTypeAllocSize(SrcTy);

  // If the source and destination are integer or pointer types, just do an
  // extension or truncation to the desired type.
  if ((mlir::isa<IntType>(Ty) || mlir::isa<PointerType>(Ty)) &&
      (mlir::isa<IntType>(SrcTy) || mlir::isa<PointerType>(SrcTy))) {
    return coerceIntOrPtrToIntOrPtr(Src, Ty, CGF);
  }

  // If load is legal, just bitcast the src pointer.
  if (!SrcSize.isScalable() && !DstSize.isScalable() &&
      SrcSize.getFixedValue() >= DstSize.getFixedValue()) {
    // Generally SrcSize is never greater than DstSize, since this means we are
    // losing bits. However, this can happen in cases where the record has
    // additional padding, for example due to a user specified alignment.
    //
    // FIXME: Assert that we aren't truncating non-padding bits when have access
    // to that information.
    return CGF.buildAggregateBitcast(Src, Ty);
  }

  if (mlir::Value addr = SrcAddrForMemCoercion ? SrcAddrForMemCoercion
                                               : findAddr(Src)) {
    auto tmpAlloca = createTmpAlloca(CGF, addr.getLoc(), Ty);
    createMemCpy(CGF, tmpAlloca, addr, SrcSize.getFixedValue(),
                 getAllocaAlign(tmpAlloca), getAllocaAlign(addr));
    return LoadOp::create(CGF.getRewriter(), addr.getLoc(),
                          tmpAlloca.getResult());
  }

  // Fallback: the source value has no traceable memory address (e.g. a
  // float result that was never loaded from an alloca).  Materialise
  // store-then-memcpy coercion, matching OGCG's CreateCoercedLoad path:
  //   1. Spill the source value into a temporary alloca.
  //   2. Allocate a second temporary of the destination type.
  //   3. memcpy min(SrcSize, DstSize) bytes from source slot to dest slot.
  //   4. Load the coerced value from the destination slot.
  {
    auto srcTmp = createTmpAlloca(CGF, Src.getLoc(), SrcTy);
    StoreOp::create(CGF.getRewriter(), Src.getLoc(), Src, srcTmp,
                    /*is_volatile=*/false, /*is_nontemporal=*/false,
                    /*alignment=*/srcTmp.getAlignmentAttr(),
                    /*sync_scope=*/cir::SyncScopeKindAttr{},
                    /*mem_order=*/cir::MemOrderAttr{},
                    /*tbaa=*/cir::TBAAAttr{});
    auto dstTmp = createTmpAlloca(CGF, Src.getLoc(), Ty);
    uint64_t copySize =
        std::min(SrcSize.getFixedValue(), DstSize.getFixedValue());
    createMemCpy(CGF, dstTmp, srcTmp, copySize, getAllocaAlign(dstTmp),
                 getAllocaAlign(srcTmp));
    return LoadOp::create(CGF.getRewriter(), Src.getLoc(), dstTmp.getResult());
  }
}

/// Creates a coerced value from \param src having a type of \param ty which is
/// a non fundamental integer type
mlir::Value createCoercedNonFundamental(mlir::Value src, mlir::Type ty,
                                        LowerFunction &LF) {
  if (auto load = src.getDefiningOp<cir::LoadOp>()) {
    auto &bld = LF.getRewriter();
    auto addr = load.getAddr();
    auto &dataLayout = LF.LM.getDataLayout();

    // Byte-aligned non-fundamental integers (for example i128 ABI coercions)
    // can be loaded directly from the original slot. Materializing an
    // intermediate coercion alloca here only turns the return into a memcpy
    // round-trip, which diverges from OGCG and bloats the final code.
    auto intTy = mlir::cast<IntType>(ty);
    llvm::TypeSize tyAllocSize = dataLayout.getTypeAllocSize(ty);
    llvm::TypeSize srcAllocSize = dataLayout.getTypeAllocSize(src.getType());
    if (!tyAllocSize.isScalable() && !srcAllocSize.isScalable() &&
        intTy.getWidth() % 8 == 0 &&
        srcAllocSize.getFixedValue() >= tyAllocSize.getFixedValue()) {
      auto castPtr = createCoercedBitcast(src, ty, LF);
      auto newLoad = LoadOp::create(
          bld, src.getLoc(), castPtr,
          /*isDeref=*/false, /*is_volatile=*/false,
          /*is_nontemporal=*/false, /*alignment=*/getAddrAlignmentAttr(addr),
          /*sync_scope=*/cir::SyncScopeKindAttr{},
          /*mem_order=*/cir::MemOrderAttr{},
          /*tbaa=*/cir::TBAAAttr{});
      bld.replaceAllOpUsesWith(load, newLoad);
      return newLoad;
    }

    // Compute alignment for the coercion alloca: use the maximum of the source
    // alignment and the preferred alignment of the coerce type, matching OGCG's
    // CreateTempAllocaForCoercion.
    auto oldAlloca = addr.getDefiningOp<cir::AllocaOp>();
    llvm::Align prefAlign = dataLayout.getPrefTypeAlign(ty);
    llvm::Align coerceAlign = prefAlign;
    if (oldAlloca) {
      if (auto srcAlignAttr = oldAlloca.getAlignmentAttr()) {
        llvm::Align srcAlign(srcAlignAttr.getInt());
        coerceAlign = std::max(srcAlign, prefAlign);
      }
    }
    auto alignAttr =
        mlir::IntegerAttr::get(mlir::IntegerType::get(bld.getContext(), 64),
                               coerceAlign.value());
    auto alloca = AllocaOp::create(
        bld, src.getLoc(), cir::PointerType::get(ty), ty,
        /*name=*/llvm::StringRef(""), alignAttr);

    auto tySize = dataLayout.getTypeStoreSize(ty);
    createMemCpy(LF, alloca, addr, tySize.getFixedValue(),
                 getAllocaAlign(alloca), getAllocaAlign(addr));
    auto newLoad = LoadOp::create(bld, src.getLoc(), alloca.getResult());
    bld.replaceAllOpUsesWith(load, newLoad);

    return newLoad;
  }

  cir_cconv_unreachable("NYI");
}

/// After the calling convention is lowered, an ABI-agnostic type might have to
/// be loaded back to its ABI-aware couterpart so it may be returned. If they
/// differ, we have to do a coerced load. A coerced load, which means to load a
/// type to another despite that they represent the same value. The simplest
/// cases can be solved with a mere bitcast.
///
/// This partially replaces CreateCoercedLoad from the original codegen.
/// However, instead of emitting the load, it emits a cast.
///
/// FIXME(cir): Improve parity with the original codegen.
mlir::Value castReturnValue(mlir::Value Src, mlir::Type Ty, LowerFunction &LF) {
  mlir::Type SrcTy = Src.getType();

  // If SrcTy and Ty are the same, nothing to do.
  if (SrcTy == Ty)
    return Src;

  if (auto coerced = tryCoerceVectorValue(LF, Src, Ty))
    return *coerced;

  // If it is the special boolean case, use the proper cast kind.
  // OGCG uses CreateZExt for bool→int, not bitcast.
  if (mlir::isa<BoolType>(SrcTy) && mlir::isa<IntType>(Ty))
    return CastOp::create(LF.getRewriter(), Src.getLoc(), Ty,
                          CastKind::bool_to_int, Src);

  auto intTy = mlir::dyn_cast<IntType>(Ty);
  if (intTy && !intTy.isFundamental())
    return createCoercedNonFundamental(Src, Ty, LF);

  llvm::TypeSize DstSize = LF.LM.getDataLayout().getTypeAllocSize(Ty);

  // FIXME(cir): Do we need the EnterRecordPointerForCoercedAccess routine here?

  llvm::TypeSize SrcSize = LF.LM.getDataLayout().getTypeAllocSize(SrcTy);

  if ((mlir::isa<IntType>(Ty) || mlir::isa<PointerType>(Ty)) &&
      (mlir::isa<IntType>(SrcTy) || mlir::isa<PointerType>(SrcTy))) {
    return coerceIntOrPtrToIntOrPtr(Src, Ty, LF);
  }

  // If load is legal, just bitcast the src pointer.
  if (!SrcSize.isScalable() && !DstSize.isScalable() &&
      SrcSize.getFixedValue() >= DstSize.getFixedValue()) {
    // Generally SrcSize is never greater than DstSize, since this means we are
    // losing bits. However, this can happen in cases where the record has
    // additional padding, for example due to a user specified alignment.
    //
    // FIXME: Assert that we aren't truncating non-padding bits when have access
    // to that information.
    auto Cast = createCoercedBitcast(Src, Ty, LF);
    // Use the source alloca's alignment to match OGCG.
    mlir::IntegerAttr alignAttr;
    if (auto alloca = findAlloca(Src.getDefiningOp()))
      alignAttr = alloca.getAlignmentAttr();
    return LoadOp::create(LF.getRewriter(), Src.getLoc(), Cast,
                          /*isDeref=*/false, /*is_volatile=*/false,
                          /*is_nontemporal=*/false, /*alignment=*/alignAttr,
                          /*sync_scope=*/cir::SyncScopeKindAttr{},
                          /*mem_order=*/cir::MemOrderAttr{},
                          /*tbaa=*/cir::TBAAAttr{});
  }

  // Otherwise do coercion through memory.
  {
    auto &rewriter = LF.getRewriter();
    AllocaOp addr = findAlloca(Src.getDefiningOp());
    // When the source value does not trace back to an alloca (e.g. it comes
    // from a call result or block argument), materialise it into a temporary
    // so the memcpy-based coercion below has an address to copy from.
    AllocaOp srcTmp;
    if (!addr) {
      srcTmp = createTmpAlloca(LF, Src.getLoc(), SrcTy);
      StoreOp::create(rewriter, Src.getLoc(), Src, srcTmp,
                      /*is_volatile=*/false, /*is_nontemporal=*/false,
                      /*alignment=*/srcTmp.getAlignmentAttr(),
                      /*sync_scope=*/cir::SyncScopeKindAttr{},
                      /*mem_order=*/cir::MemOrderAttr{},
                      /*tbaa=*/cir::TBAAAttr{});
      addr = srcTmp;
    }
    auto tmp = createTmpAlloca(LF, Src.getLoc(), Ty);
    createMemCpy(LF, tmp, addr, SrcSize.getFixedValue(),
                 getAllocaAlign(tmp), getAllocaAlign(addr));
    return LoadOp::create(rewriter, Src.getLoc(), tmp.getResult());
  }
}

} // namespace

// FIXME(cir): Pass SrcFn and NewFn around instead of having then as attributes.
LowerFunction::LowerFunction(LowerModule &LM, mlir::PatternRewriter &rewriter,
                             FuncOp srcFn, FuncOp newFn)
    : Target(LM.getTarget()), rewriter(rewriter), SrcFn(srcFn), NewFn(newFn),
      LM(LM) {}

LowerFunction::LowerFunction(LowerModule &LM, mlir::PatternRewriter &rewriter,
                             FuncOp srcFn, CallOp callOp)
    : Target(LM.getTarget()), rewriter(rewriter), SrcFn(srcFn), callOp(callOp),
      LM(LM) {}

LowerFunction::LowerFunction(LowerModule &LM, mlir::PatternRewriter &rewriter,
                             FuncOp srcFn, TryCallOp tryCallOp)
    : Target(LM.getTarget()), rewriter(rewriter), SrcFn(srcFn),
      tryCallNormalDest(tryCallOp.getNormalDest()),
      tryCallUnwindDest(tryCallOp.getUnwindDest()), LM(LM) {}

/// This method has partial parity with CodeGenFunction::EmitFunctionProlog from
/// the original codegen. However, it focuses on the ABI-specific details. On
/// top of that, it is also responsible for rewriting the original function.
llvm::LogicalResult LowerFunction::buildFunctionProlog(
    const LowerFunctionInfo &FI, FuncOp Fn,
    llvm::MutableArrayRef<mlir::BlockArgument> Args) {
  // NOTE(cir): Skipping naked and implicit-return-zero functions here. These
  // are dealt with in CIRGen.

  CIRToCIRArgMapping IRFunctionArgs(LM.getContext(), FI);
  cir_cconv_assert(Fn.getNumArguments() == IRFunctionArgs.totalIRArgs());

  // Forward anchor for the prologue ops of coerced (Direct) by-value record
  // arguments. The uncoerced alloca + coerced store for such an argument must
  // be placed at the top of the entry block so it dominates every use of the
  // parameter. Relying on the rewriter's incoming insertion point is wrong:
  // some callers leave it at the end of the entry block (e.g. a constructor
  // whose member initializers consume a by-value parameter), and other argument
  // kinds (Indirect) move it mid-loop -- either case would place the alloca
  // after the operations that use it, producing a use-before-def / null
  // operand. Anchoring each Direct argument after the previous one's prologue
  // ops keeps declaration order (matching classic CodeGen).
  mlir::Operation *lastDirectPrologOp = nullptr;

  // Name the record return parameter.

  // Track if we received the parameter as a pointer (indirect, byval, or
  // inalloca). If already have a pointer, EmitParmDecl doesn't need to copy it
  // into a local alloca for us.
  llvm::SmallVector<mlir::Value, 8> ArgVals;
  ArgVals.reserve(Args.size());

  // Create a pointer value for every parameter declaration. This usually
  // entails copying one or more LLVM IR arguments into an alloca. Don't push
  // any cleanups or do anything that might unwind. We do that separately, so
  // we can push the cleanups in the correct order for the ABI.
  if (FI.arg_size() != Args.size())
    cir_cconv_unreachable("NYI: ABI parameter coercion");
  cir_cconv_assert(FI.arg_size() == Args.size());
  unsigned ArgNo = 0;
  LowerFunctionInfo::const_arg_iterator info_it = FI.arg_begin();
  for (llvm::MutableArrayRef<mlir::BlockArgument>::const_iterator
           i = Args.begin(),
           e = Args.end();
       i != e; ++i, ++info_it, ++ArgNo) {
    const mlir::Value Arg = *i;
    const ABIArgInfo &ArgI = info_it->info;

    // OGCG checks isKNRPromoted() here to demote K&R-promoted parameters
    // (e.g. int back to char). CIR doesn't support K&R declarations.
    mlir::Type Ty = Arg.getType();

    unsigned FirstIRArg, NumIRArgs;
    std::tie(FirstIRArg, NumIRArgs) = IRFunctionArgs.getIRArgs(ArgNo);

    switch (ArgI.getKind()) {
    case ABIArgInfo::Extend:
    case ABIArgInfo::Direct: {
      // An empty struct flattened to zero IR args has no corresponding
      // argument in the new function. Handle like Ignore.
      if (NumIRArgs == 0) {
        mlir::Value oldArg = SrcFn.getArgument(ArgNo);
        if (!oldArg.use_empty()) {
          llvm::SmallVector<cir::StoreOp, 2> deadStores;
          for (mlir::OpOperand &use : oldArg.getUses()) {
            if (auto store = mlir::dyn_cast<cir::StoreOp>(use.getOwner()))
              deadStores.push_back(store);
          }
          auto undef = cir::ConstantOp::create(
              rewriter, Fn.getLoc(), cir::UndefAttr::get(oldArg.getType()));
          rewriter.replaceAllUsesWith(oldArg, undef);
          // Erase dead stores and their target allocas (which may be
          // behind an address_space cast).
          for (auto store : deadStores) {
            auto addr = store.getAddr();
            rewriter.eraseOp(store);
            if (addr.use_empty())
              eraseDefOpThroughCast(rewriter, addr);
          }
          if (undef->use_empty())
            rewriter.eraseOp(undef);
        }
        break;
      }
      auto AI = Fn.getArgument(FirstIRArg);
      mlir::Value originalArg = SrcFn.getArgument(ArgNo);
      auto originalInitStore = findArgInitStoreChain(originalArg);
      cir::AllocaOp originalArgAlloca;
      if (originalInitStore)
        originalArgAlloca =
            findAlloca(originalInitStore.store.getAddr().getDefiningOp());

      // OGCG sets pointer parameter attributes (nonnull, noalias, align,
      // dereferenceable for C99 static arrays, AlignValueAttr) here in
      // EmitFunctionProlog. In CIR, most of these are already set during
      // CIRGen (cir.nonnull, cir.noalias, etc.) and lowered in LowerToLLVM.
      // Static array dereferenceable/align attrs are tracked under
      // callAttrABIArgLowering.

      // Prepare the argument value. If we have the trivial case, handle it
      // with no muss and fuss.
      if (!mlir::isa<RecordType>(ArgI.getCoerceToType()) &&
          ArgI.getCoerceToType() == Ty && ArgI.getDirectOffset() == 0) {
        cir_cconv_assert(NumIRArgs == 1);

        // LLVM expects swifterror parameters to be used in very restricted
        // ways. Copy the value into a less-restricted temporary.
        mlir::Value V = AI;
        if (SrcFn.getArgAttr(ArgNo, "cir.swifterror")) {
          // swifterror params can only be the ADDRESS operand of load/store
          // in LLVM. Create a temp alloca, load the current error pointer
          // from the swifterror param, and store it to the temp. The
          // function body uses the temp; buildFunctionEpilog writes back.
          auto ptrTy = mlir::cast<PointerType>(Ty);
          auto innerTy = ptrTy.getPointee();
          auto align = LM.getDataLayout().getABITypeAlign(innerTy);
          auto alignAttr = rewriter.getI64IntegerAttr(align.value());
          auto tmpAlloca = AllocaOp::create(
              rewriter, Fn.getLoc(), PointerType::get(innerTy), innerTy,
              /*name=*/"swifterror.tmp", alignAttr);
          auto loaded = LoadOp::create(rewriter, Fn.getLoc(), AI);
          StoreOp::create(rewriter, Fn.getLoc(), loaded, tmpAlloca,
                          /*is_volatile=*/false, /*is_nontemporal=*/false,
                          /*alignment=*/alignAttr,
                          /*sync_scope=*/cir::SyncScopeKindAttr{},
                          /*mem_order=*/cir::MemOrderAttr{},
                          /*tbaa=*/cir::TBAAAttr{});
          V = tmpAlloca;
          swiftErrorTemps.push_back({tmpAlloca, AI});
        }

        // Ensure the argument is the correct type.
        if (V.getType() != ArgI.getCoerceToType())
          cir_cconv_unreachable("NYI");

        ArgVals.push_back(V);

        // NOTE(cir): Here we have a trivial case, which means we can just
        // replace all uses of the original argument with the new one.
        mlir::Value oldArg = SrcFn.getArgument(ArgNo);
        rewriter.replaceAllUsesWith(oldArg, V);

        break;
      }

      // Allocate original argument to be "uncoerced".
      // FIXME(cir): We should have a alloca op builder that does not required
      // the pointer type to be explicitly passed.
      // Use the original CIRGen alloca alignment when available. It comes
      // from getDeclAlign() which accounts for typedef-level
      // __attribute__((aligned(N))) and other source-level alignment
      // specifiers that aren't reflected in the CIR type system. This
      // matches OGCG's CreateMemTemp(Ty, getDeclAlign(Arg), ...) approach.
      auto align = LM.getDataLayout().getABITypeAlign(Ty);
      if (originalArgAlloca)
        if (auto originalAlign = originalArgAlloca.getAlignmentAttr())
          align = llvm::Align(originalAlign.getInt());
      // Preserve the original alloca's address space (e.g. offload_private on
      // AMDGPU) so that the replacement has the same pointer type. Without
      // this, replaceAllUsesWith would create a type mismatch.
      auto origAS =
          originalArgAlloca
              ? mlir::cast<PointerType>(originalArgAlloca.getType())
                    .getAddrSpace()
              : mlir::ptr::MemorySpaceAttrInterface();

      // Anchor this argument's prologue at the top of the entry block, after
      // any prologue ops already emitted for earlier arguments, so it dominates
      // the parameter's uses while preserving declaration order.
      //
      // NOTE: intentionally NOT an InsertionGuard. The RAUW/erase below
      // (eraseArgInitStoreChain) can free the op that the incoming insertion
      // point refers to; a guard would restore that now-dangling iterator and
      // the next op creation (e.g. the ABIArgInfo::Ignore case's cir.const)
      // would insert against freed memory (heap-use-after-free). Instead the
      // insertion point is re-seated after the erase to the just-captured,
      // guaranteed-live lastDirectPrologOp (see below).
      if (lastDirectPrologOp) {
        rewriter.setInsertionPointAfter(lastDirectPrologOp);
      } else if (!Fn.getBody().empty()) {
        mlir::Block *entryBlock = &Fn.getBody().front();
        // A cir.label, if present, must remain the first op in its block.
        if (!entryBlock->empty() &&
            mlir::isa<cir::LabelOp>(entryBlock->front()))
          rewriter.setInsertionPointAfter(&entryBlock->front());
        else
          rewriter.setInsertionPointToStart(entryBlock);
      }

      auto Alloca =
          AllocaOp::create(rewriter, Fn.getLoc(),
                           cir::PointerType::get(Ty, origAS), Ty,
                           /*name=*/llvm::StringRef(""),
                           /*alignment=*/rewriter.getI64IntegerAttr(
                               align.value()));

      mlir::Value Ptr = buildAddressAtOffset(*this, Alloca.getResult(), ArgI);

      // Fast-isel and the optimizer generally like scalar values better than
      // FCAs, so we flatten them if this is safe to do for this argument.
      RecordType STy = mlir::dyn_cast<RecordType>(ArgI.getCoerceToType());
      if (ArgI.isDirect() && ArgI.getCanBeFlattened() && STy &&
          STy.getNumElements() > 1) {
        auto ptrType = mlir::cast<PointerType>(Ptr.getType());
        llvm::TypeSize recordSize =
            LM.getTypes().getDataLayout().getTypeAllocSize(STy);
        llvm::TypeSize ptrElementSize =
            LM.getTypes().getDataLayout().getTypeAllocSize(
                ptrType.getPointee());
        if (recordSize.isScalable()) {
          cir_cconv_unreachable("NYI");
        } else {
          uint64_t srcSize = recordSize.getFixedValue();
          uint64_t dstSize = ptrElementSize.getFixedValue();

          mlir::Value addrToStoreInto;
          if (srcSize <= dstSize) {
            addrToStoreInto =
                CastOp::create(rewriter, Ptr.getLoc(),
                               PointerType::get(STy, ptrType.getAddrSpace()),
                               CastKind::bitcast, Ptr);
          } else {
            addrToStoreInto = createTmpAlloca(*this, Ptr.getLoc(), STy);
          }

          cir_cconv_assert(STy.getNumElements() == NumIRArgs);
          uint64_t baseAlign = align.value();
          uint64_t offsetAfterPrevious = 0;
          for (unsigned i = 0, e = STy.getNumElements(); i != e; ++i) {
            mlir::Value ai = Fn.getArgument(FirstIRArg + i);
            mlir::Type elementTy = STy.getMembers()[i];
            mlir::Value eltPtr = GetMemberOp::create(
                rewriter, ai.getLoc(),
                PointerType::get(elementTy, ptrType.getAddrSpace()),
                addrToStoreInto,
                /*name=*/"", /*index=*/i);
            // Compute per-element alignment matching OGCG's CreateStructGEP
            // which adjusts alignment based on element offset.
            uint64_t elemOffset = 0;
            if (!STy.isUnion()) {
              const uint64_t elemABIAlign =
                  STy.getPacked() ? 1 : LM.getDataLayout().getABITypeAlign(elementTy).value();
              elemOffset = llvm::alignTo(offsetAfterPrevious, elemABIAlign);
              offsetAfterPrevious = elemOffset +
                                    LM.getDataLayout().getTypeAllocSize(elementTy);
            }
            uint64_t elemAlign = baseAlign;
            if (elemOffset != 0)
              elemAlign = std::min(baseAlign,
                                   (uint64_t)llvm::MinAlign(baseAlign, elemOffset));
            mlir::IntegerAttr elemAlignAttr = mlir::IntegerAttr::get(
                mlir::IntegerType::get(rewriter.getContext(), 64), elemAlign);
            StoreOp::create(
                rewriter, ai.getLoc(), ai, eltPtr,
                /*is_volatile=*/false,
                /*is_nontemporal=*/false,
                /*alignment=*/elemAlignAttr,
                /*sync_scope=*/cir::SyncScopeKindAttr{},
                /*mem_order=*/cir::MemOrderAttr{},
                /*tbaa=*/
                    originalInitStore
                    ? deriveFlattenedFieldStoreTBAA(originalInitStore.store)
                    : cir::TBAAAttr{});
          }

          if (srcSize > dstSize) {
            createMemCpy(*this, Ptr, addrToStoreInto, dstSize,
                         getAllocaAlign(Ptr), getAllocaAlign(addrToStoreInto));
          }
        }
      } else {
        // Simple case, just do a coerced store of the argument into the alloca.
        cir_cconv_assert(NumIRArgs == 1);
        mlir::Value AI = Fn.getArgument(FirstIRArg);
        // TODO(cir): Set argument name in the new function.
        createCoercedStore(AI, Ptr, /*DstIsVolatile=*/false, Ptr.getLoc(),
                           *this);
      }

      // Advance the prologue anchor to the last op emitted for this argument
      // (the coerced store, still at the top of the entry block) so the next
      // coerced argument is placed after it, preserving declaration order.
      // Capture here, before the RAUW/erase below, which can invalidate the
      // insertion-point iterator.
      if (mlir::Block *ipBlock = rewriter.getInsertionBlock()) {
        mlir::Block::iterator ipIt = rewriter.getInsertionPoint();
        lastDirectPrologOp =
            ipIt == ipBlock->begin() ? Alloca.getOperation() : &*std::prev(ipIt);
      } else {
        lastDirectPrologOp = Alloca.getOperation();
      }

      // Match to what EmitParamDecl is expecting for this type.
      // FIXME(cir): Should we have an ParamValue abstraction like in the
      // original codegen?
      ArgVals.push_back(Alloca);

      // NOTE(cir): Once we have uncoerced the argument, we should be able to
      // RAUW the original argument alloca with the new one. This assumes that
      // the argument is used only to be stored in a alloca.
      if (originalInitStore) {
        auto argAlloca = originalInitStore.store.getAddr();
        auto replacementAddr = adaptReplacementAddrForOriginalAddr(
            *this, Fn.getLoc(), Alloca.getResult(), argAlloca);
        rewriter.replaceAllUsesWith(argAlloca, replacementAddr);
        eraseArgInitStoreChain(rewriter, originalInitStore, argAlloca);
      } else {
        // The argument was not stored via the standard init-store-chain
        // pattern (e.g. unused parameter, or multiple uses). Use the
        // fallback to clean up the original arg's uses.
        replaceArgWithoutInitStoreChain(*this, rewriter, Fn.getLoc(),
                                        originalArg, Alloca.getResult());
      }
      // Re-establish a valid insertion point after the RAUW/erase above, which
      // may have freed the op the incoming insertion point referred to.
      // lastDirectPrologOp was captured before the erase and is a live entry-
      // block op, so this keeps subsequent prologue ops in declaration order
      // (preserving the coerced-param dominance intent) without restoring a
      // dangling iterator.
      if (lastDirectPrologOp)
        rewriter.setInsertionPointAfter(lastDirectPrologOp);
      break;
    }
    case ABIArgInfo::IndirectAliased:
    case ABIArgInfo::Indirect: {
      auto AI = Fn.getArgument(FirstIRArg);
      if (!hasScalarEvaluationKind(Ty)) {
        // Aggregates and complex variables are accessed by reference. All we
        // need to do is realign the value, if requested. Also, if the address
        // may be aliased, copy it to ensure that the parameter variable is
        // mutable and has a unique adress, as C requires.
        if (ArgI.isIndirectAliased()) {
          // IndirectAliased: the pointer may alias other references (e.g.,
          // AMDGPU kernel args in constant address space). Copy to a local
          // alloca so the parameter is mutable and has a unique address.
          mlir::Value arg = SrcFn.getArgument(ArgNo);
          auto initStore = findArgInitStoreChain(arg);
          if (!initStore) {
            // Fallback: create the local alloca and memcpy, then use the
            // generic fallback to redirect the original arg's uses.
            auto localAlloca = cir::AllocaOp::create(
                rewriter, AI.getLoc(), cir::PointerType::get(Ty), Ty,
                /*name=*/llvm::StringRef(""),
                /*alignment=*/
                rewriter.getI64IntegerAttr(ArgI.getIndirectAlign()));
            uint64_t typeSize = LM.getDataLayout().getTypeStoreSize(Ty);
            createMemCpy(*this, localAlloca, AI, typeSize,
                         ArgI.getIndirectAlign(), ArgI.getIndirectAlign());
            replaceArgWithoutInitStoreChain(*this, rewriter, AI.getLoc(), arg,
                                            localAlloca.getResult());
          } else {
            auto argAlloca = initStore.store.getAddr();

            // Create a local alloca for the copy.
            // Re-use the original alloca's name so debug info is preserved.
            // Chase through address_space casts to find the actual alloca.
            auto origAlloca = argAlloca.getDefiningOp<cir::AllocaOp>();
            if (!origAlloca) {
              if (auto castOp = argAlloca.getDefiningOp<CastOp>())
                origAlloca = castOp.getSrc().getDefiningOp<cir::AllocaOp>();
            }
            llvm::StringRef allocaName = origAlloca ? origAlloca.getName() : "";
            // Preserve the original alloca's address space so the RAUW below
            // doesn't create a type mismatch.
            auto localAS = origAlloca
                               ? mlir::cast<PointerType>(origAlloca.getType())
                                     .getAddrSpace()
                               : mlir::ptr::MemorySpaceAttrInterface();
            auto localAlloca = cir::AllocaOp::create(
                rewriter, AI.getLoc(), cir::PointerType::get(Ty, localAS), Ty,
                /*name=*/allocaName,
                /*alignment=*/
                rewriter.getI64IntegerAttr(ArgI.getIndirectAlign()));

            // Copy from the kernel arg pointer to the local alloca.
            uint64_t typeSize = LM.getDataLayout().getTypeStoreSize(Ty);
            createMemCpy(*this, localAlloca, AI, typeSize,
                         ArgI.getIndirectAlign(), ArgI.getIndirectAlign());

            // Replace original alloca with the local copy.
            auto replacementAddr = adaptReplacementAddrForOriginalAddr(
                *this, AI.getLoc(), localAlloca.getResult(), argAlloca);
            rewriter.replaceAllUsesWith(argAlloca, replacementAddr);
            eraseArgInitStoreChain(rewriter, initStore, argAlloca);
          }
        } else if (ArgI.getIndirectRealign()) {
          // Match OGCG's EmitFunctionProlog: copy the incoming indirect
          // aggregate to a naturally aligned local temp before exposing the
          // parameter address to the body.
          mlir::Value arg = SrcFn.getArgument(ArgNo);
          auto initStore = findArgInitStoreChain(arg);
          auto align = LM.getDataLayout().getABITypeAlign(Ty);

          if (!initStore) {
            auto localAlloca = cir::AllocaOp::create(
                rewriter, AI.getLoc(), cir::PointerType::get(Ty), Ty,
                /*name=*/llvm::StringRef(""),
                /*alignment=*/rewriter.getI64IntegerAttr(align.value()));
            uint64_t typeSize = LM.getDataLayout().getTypeStoreSize(Ty);
            createMemCpy(*this, localAlloca, AI, typeSize, align.value(),
                         ArgI.getIndirectAlign());
            replaceArgWithoutInitStoreChain(*this, rewriter, AI.getLoc(), arg,
                                            localAlloca.getResult());
            ArgVals.push_back(localAlloca);
          } else {
            auto argAlloca = initStore.store.getAddr();

            auto origAlloca = argAlloca.getDefiningOp<cir::AllocaOp>();
            if (!origAlloca) {
              if (auto castOp = argAlloca.getDefiningOp<CastOp>())
                origAlloca = castOp.getSrc().getDefiningOp<cir::AllocaOp>();
            }

            if (origAlloca)
              if (auto originalAlign = origAlloca.getAlignmentAttr())
                align = llvm::Align(originalAlign.getInt());

            auto origAS = origAlloca
                              ? mlir::cast<PointerType>(origAlloca.getType())
                                    .getAddrSpace()
                              : mlir::ptr::MemorySpaceAttrInterface();
            llvm::StringRef allocaName = origAlloca ? origAlloca.getName() : "";
            auto localAlloca = cir::AllocaOp::create(
                rewriter, AI.getLoc(), cir::PointerType::get(Ty, origAS), Ty,
                /*name=*/allocaName,
                /*alignment=*/rewriter.getI64IntegerAttr(align.value()));

            uint64_t typeSize = LM.getDataLayout().getTypeStoreSize(Ty);
            createMemCpy(*this, localAlloca, AI, typeSize, align.value(),
                         ArgI.getIndirectAlign());

            auto replacementAddr = adaptReplacementAddrForOriginalAddr(
                *this, AI.getLoc(), localAlloca.getResult(), argAlloca);
            rewriter.replaceAllUsesWith(argAlloca, replacementAddr);
            eraseArgInitStoreChain(rewriter, initStore, argAlloca);

            ArgVals.push_back(localAlloca);
          }
        } else if (ArgI.getIndirectByVal()) {
          // For byval arguments, the ABI passes a pointer to a caller-made
          // copy of the struct. The callee owns this copy and can use the
          // pointer directly as the alloca for the parameter.
          mlir::Value arg = SrcFn.getArgument(ArgNo);
          auto initStore = findArgInitStoreChain(arg);
          if (!initStore) {
            replaceArgWithoutInitStoreChain(*this, rewriter, AI.getLoc(), arg,
                                            AI);
          } else {
            auto argAlloca = initStore.store.getAddr();

            // AI is a pointer to the struct copy. Replace the original alloca
            // with AI directly.
            auto replacementAddr = adaptReplacementAddrForOriginalAddr(
                *this, AI.getLoc(), AI, argAlloca);
            rewriter.replaceAllUsesWith(argAlloca, replacementAddr);
            eraseArgInitStoreChain(rewriter, initStore, argAlloca);
          }

          if (unsigned byvalAlign = ArgI.getIndirectAlign())
            propagateByValDerivedAlignment(
                *this, AI, clang::CharUnits::fromQuantity(byvalAlign));

          ArgVals.push_back(AI);
        } else {
          // For non-byval indirect args, create an alloca for the pointer
          // and store the pointer argument into it.
          // Inspired by EmitParamDecl, which is called in the end of
          // EmitFunctionProlog in the original codegen.
          auto ptrTy = cir::PointerType::get(Arg.getType());
          mlir::Value arg = SrcFn.getArgument(ArgNo);
          auto initStore = findArgInitStoreChain(arg);
          auto align = LM.getDataLayout().getABITypeAlign(ptrTy);
          auto alignAttr = rewriter.getI64IntegerAttr(align.value());

          auto recordPointerTBAA = [&]() -> cir::TBAAAttr {
            auto recordTy = mlir::dyn_cast<cir::RecordType>(Ty);
            if (!recordTy)
              return {};
            AggregateCopyTBAABuilder tbaaBuilder(*this);
            return tbaaBuilder.getIndirectParamPointerTag(recordTy);
          };

          if (!initStore) {
            auto newAlloca = AllocaOp::create(
                rewriter, Fn.getLoc(), cir::PointerType::get(ptrTy), ptrTy,
                /*name=*/llvm::StringRef(""),
                /*alignment=*/alignAttr);
            StoreOp::create(rewriter, newAlloca.getLoc(), AI,
                            newAlloca.getResult(),
                            /*is_volatile=*/false,
                            /*is_nontemporal=*/false,
                            /*alignment=*/alignAttr,
                            /*sync_scope=*/cir::SyncScopeKindAttr{},
                            /*mem_order=*/cir::MemOrderAttr{},
                            /*tbaa=*/recordPointerTBAA());
            auto load = LoadOp::create(rewriter, newAlloca.getLoc(),
                                       newAlloca.getResult());
            replaceArgWithoutInitStoreChain(*this, rewriter, Fn.getLoc(), arg,
                                            load.getResult());
          } else {
            auto originalStore = initStore.store;
            auto argAlloca = originalStore.getAddr();
            auto pointerStoreTBAA = deriveIndirectParamPointerStoreTBAA(
                originalStore, AI.getType(), rewriter.getContext());
            if (!pointerStoreTBAA)
              pointerStoreTBAA = recordPointerTBAA();

            rewriter.setInsertionPoint(argAlloca.getDefiningOp());
            auto newAlloca = AllocaOp::create(
                rewriter, Fn.getLoc(), cir::PointerType::get(ptrTy), ptrTy,
                /*name=*/llvm::StringRef(""),
                /*alignment=*/alignAttr);

            StoreOp::create(rewriter, newAlloca.getLoc(), AI,
                            newAlloca.getResult(),
                            /*is_volatile=*/false,
                            /*is_nontemporal=*/false,
                            /*alignment=*/alignAttr,
                            /*sync_scope=*/cir::SyncScopeKindAttr{},
                            /*mem_order=*/cir::MemOrderAttr{},
                            /*tbaa=*/pointerStoreTBAA);
            auto load = LoadOp::create(rewriter, newAlloca.getLoc(),
                                       newAlloca.getResult());

            rewriter.replaceAllUsesWith(argAlloca, load);
            eraseArgInitStoreChain(rewriter, initStore, argAlloca);
          }

          ArgVals.push_back(AI);
        }
      } else {
        // Scalar types passed indirectly (e.g., large _BitInt). The function
        // argument is a pointer to the value.
        if (ArgI.getIndirectByVal()) {
          // With byval, the caller provides a copy. The pointer can be used
          // directly as the local alloca, same as the aggregate byval case.
          mlir::Value arg = SrcFn.getArgument(ArgNo);
          auto initStore = findArgInitStoreChain(arg);
          if (!initStore) {
            replaceArgWithoutInitStoreChain(*this, rewriter, AI.getLoc(), arg,
                                            AI);
          } else {
            auto argAlloca = initStore.store.getAddr();

            auto replacementAddr = adaptReplacementAddrForOriginalAddr(
                *this, AI.getLoc(), AI, argAlloca);
            rewriter.replaceAllUsesWith(argAlloca, replacementAddr);
            eraseArgInitStoreChain(rewriter, initStore, argAlloca);
          }

          if (unsigned byvalAlign = ArgI.getIndirectAlign())
            propagateByValDerivedAlignment(
                *this, AI, clang::CharUnits::fromQuantity(byvalAlign));

          ArgVals.push_back(AI);
        } else {
          // Without byval, the callee receives a pointer to the caller's
          // copy. Load the value into a local alloca so the function body
          // can use it like any other local.
          mlir::Value arg = SrcFn.getArgument(ArgNo);
          auto initStore = findArgInitStoreChain(arg);

          if (!initStore) {
            // Fallback: create a local alloca, load from the indirect
            // pointer, and redirect the original arg's uses.
            unsigned localAllocaAlign =
                LM.getDataLayout().getABITypeAlign(Ty).value();
            if (info_it->isBitIntType()) {
              localAllocaAlign = ArgI.getIndirectAlign();
              if (!localAllocaAlign)
                localAllocaAlign =
                    getBitIntStorageAlign(*this, Ty, info_it->isBitIntType());
            }
            auto localAlloca = AllocaOp::create(
                rewriter, Fn.getLoc(), cir::PointerType::get(Ty), Ty,
                /*name=*/"tmp",
                /*alignment=*/rewriter.getI64IntegerAttr(localAllocaAlign));
            if (auto storageTy =
                    getBitIntStorageType(*this, Ty, info_it->isBitIntType())) {
              auto loadedVal =
                  createBitIntStorageLoad(*this, Fn.getLoc(), AI, Ty, storageTy,
                                          ArgI.getIndirectAlign());
              createBitIntStorageStore(
                  *this, Fn.getLoc(), loadedVal, localAlloca.getResult(), Ty,
                  storageTy, localAlloca.getAlignmentAttr(), cir::TBAAAttr{});
            } else {
              mlir::IntegerAttr indirectAlignAttr;
              if (unsigned indirectAlign = ArgI.getIndirectAlign())
                indirectAlignAttr = rewriter.getI64IntegerAttr(indirectAlign);
              auto loadedVal =
                  LoadOp::create(rewriter, Fn.getLoc(), AI,
                                 /*isDeref=*/false, /*is_volatile=*/false,
                                 /*is_nontemporal=*/false,
                                 /*alignment=*/indirectAlignAttr,
                                 /*sync_scope=*/cir::SyncScopeKindAttr{},
                                 /*mem_order=*/cir::MemOrderAttr{},
                                 /*tbaa=*/cir::TBAAAttr{});
              StoreOp::create(rewriter, Fn.getLoc(), loadedVal,
                              localAlloca.getResult(),
                              /*is_volatile=*/false, /*is_nontemporal=*/false,
                              /*alignment=*/localAlloca.getAlignmentAttr(),
                              /*sync_scope=*/cir::SyncScopeKindAttr{},
                              /*mem_order=*/cir::MemOrderAttr{},
                              /*tbaa=*/cir::TBAAAttr{});
            }
            replaceArgWithoutInitStoreChain(*this, rewriter, Fn.getLoc(), arg,
                                            localAlloca.getResult());
            ArgVals.push_back(localAlloca);
          } else {
            auto originalStore = initStore.store;
            auto argAlloca = originalStore.getAddr();
            auto originalArgAlloca = findAlloca(argAlloca.getDefiningOp());

            // Create a local copy by loading from the indirect pointer.
            // Preserve the original alloca's address space for RAUW
            // consistency.
            auto argAS = mlir::cast<cir::PointerType>(argAlloca.getType())
                             .getAddrSpace();
            unsigned localAllocaAlign =
                LM.getDataLayout().getABITypeAlign(Ty).value();
            if (originalArgAlloca)
              if (auto originalAlign = originalArgAlloca.getAlignmentAttr())
                localAllocaAlign = originalAlign.getInt();
            if (info_it->isBitIntType()) {
              localAllocaAlign = ArgI.getIndirectAlign();
              if (!localAllocaAlign)
                localAllocaAlign =
                    getBitIntStorageAlign(*this, Ty, info_it->isBitIntType());
            }
            auto localAlloca = AllocaOp::create(
                rewriter, Fn.getLoc(), cir::PointerType::get(Ty, argAS), Ty,
                /*name=*/"tmp",
                /*alignment=*/rewriter.getI64IntegerAttr(localAllocaAlign));
            if (auto storageTy =
                    getBitIntStorageType(*this, Ty, info_it->isBitIntType())) {
              auto loadedVal =
                  createBitIntStorageLoad(*this, Fn.getLoc(), AI, Ty, storageTy,
                                          ArgI.getIndirectAlign());
              createBitIntStorageStore(
                  *this, Fn.getLoc(), loadedVal, localAlloca.getResult(), Ty,
                  storageTy, localAlloca.getAlignmentAttr(),
                  originalStore.getTbaa().value_or(cir::TBAAAttr{}));
            } else {
              // The caller-provided slot is only guaranteed the indirect ABI
              // alignment, which can be smaller than the value type's natural
              // alignment for fixed vectors lowered indirectly.
              mlir::IntegerAttr indirectAlignAttr;
              if (unsigned indirectAlign = ArgI.getIndirectAlign())
                indirectAlignAttr = rewriter.getI64IntegerAttr(indirectAlign);
              auto loadedVal =
                  LoadOp::create(rewriter, Fn.getLoc(), AI,
                                 /*isDeref=*/false, /*is_volatile=*/false,
                                 /*is_nontemporal=*/false,
                                 /*alignment=*/indirectAlignAttr,
                                 /*sync_scope=*/cir::SyncScopeKindAttr{},
                                 /*mem_order=*/cir::MemOrderAttr{},
                                 /*tbaa=*/cir::TBAAAttr{});
              StoreOp::create(
                  rewriter, Fn.getLoc(), loadedVal, localAlloca.getResult(),
                  /*is_volatile=*/false, /*is_nontemporal=*/false,
                  /*alignment=*/localAlloca.getAlignmentAttr(),
                  /*sync_scope=*/cir::SyncScopeKindAttr{},
                  /*mem_order=*/cir::MemOrderAttr{},
                  /*tbaa=*/originalStore.getTbaa().value_or(cir::TBAAAttr{}));
            }

            auto replacementAddr = adaptReplacementAddrForOriginalAddr(
                *this, Fn.getLoc(), localAlloca.getResult(), argAlloca);
            rewriter.replaceAllUsesWith(argAlloca, replacementAddr);
            eraseArgInitStoreChain(rewriter, initStore, argAlloca);

            ArgVals.push_back(localAlloca);
          }
        }
      }
      break;
    }
    case ABIArgInfo::Ignore: {
      // Ignored arguments don't have a corresponding IR argument.
      // Replace the old argument's uses with an undef value so that the
      // "Missing RAUW?" assertion in generateCode passes.
      mlir::Value oldArg = SrcFn.getArgument(ArgNo);
      if (!oldArg.use_empty()) {
        // Collect stores that use this arg before replacing — storing undef
        // to a local alloca is pointless and diverges from OGCG.
        llvm::SmallVector<cir::StoreOp, 2> deadStores;
        for (mlir::OpOperand &use : oldArg.getUses()) {
          if (auto store = mlir::dyn_cast<cir::StoreOp>(use.getOwner()))
            deadStores.push_back(store);
        }

        auto undef = cir::ConstantOp::create(
            rewriter, Fn.getLoc(), cir::UndefAttr::get(oldArg.getType()));
        rewriter.replaceAllUsesWith(oldArg, undef);

        // Erase the dead stores and their target allocas (which may be
        // behind an address_space cast).
        for (auto store : deadStores) {
          auto addr = store.getAddr();
          rewriter.eraseOp(store);
          if (addr.use_empty())
            eraseDefOpThroughCast(rewriter, addr);
        }

        // If the undef constant has no remaining uses, erase it too.
        if (undef->use_empty())
          rewriter.eraseOp(undef);
      }
      break;
    }
    case ABIArgInfo::CoerceAndExpand: {
      // Reconstruct the aggregate from expanded arguments into a temporary.
      auto coercionType = ArgI.getCoerceAndExpandType();

      // Find the original CIRGen alloca to preserve its alignment. It comes
      // from getDeclAlign() which accounts for typedef-level alignment attrs.
      mlir::Value originalArg2 = SrcFn.getArgument(ArgNo);
      auto initStore2 = findArgInitStoreChain(originalArg2);
      cir::AllocaOp originalArgAlloca2;
      if (initStore2)
        originalArgAlloca2 =
            findAlloca(initStore2.store.getAddr().getDefiningOp());

      // Use the original CIRGen alloca alignment when available. This matches
      // OGCG's CreateMemTemp(Ty, getDeclAlign(Arg), ...) approach.
      auto align = LM.getDataLayout().getABITypeAlign(Ty);
      if (originalArgAlloca2)
        if (auto originalAlign = originalArgAlloca2.getAlignmentAttr())
          align = llvm::Align(originalAlign.getInt());
      // Preserve address space from the original alloca for RAUW consistency.
      auto origAS2 =
          originalArgAlloca2
              ? mlir::cast<PointerType>(originalArgAlloca2.getType())
                    .getAddrSpace()
              : mlir::ptr::MemorySpaceAttrInterface();
      auto Alloca =
          AllocaOp::create(rewriter, Fn.getLoc(),
                           cir::PointerType::get(Ty, origAS2), Ty,
                           /*name=*/llvm::StringRef(""),
                           /*alignment=*/rewriter.getI64IntegerAttr(
                               align.value()));

      // Bitcast the alloca to the padded coercion struct type.
      auto coercionPtrTy = PointerType::get(coercionType, origAS2);
      auto coercionAddr = CastOp::create(rewriter, Fn.getLoc(), coercionPtrTy,
                                          CastKind::bitcast, Alloca.getResult());

      mlir::Value coercionValue = ConstantOp::create(
          rewriter, Fn.getLoc(), UndefAttr::get(coercionType));

      unsigned argIndex = FirstIRArg;
      for (unsigned i = 0, e = coercionType.getNumElements(); i != e; ++i) {
        mlir::Type eltType = coercionType.getMembers()[i];
        if (ABIArgInfo::isPaddingForCoerceAndExpand(eltType))
          continue;

        mlir::Value elt = Fn.getArgument(argIndex++);

        if (elt.getType() != eltType) {
          if (auto coerced = tryCoerceVectorValue(*this, elt, eltType))
            elt = *coerced;
        }

        if (elt.getType() != eltType) {
          // Coerce via store to tmp + bitcast load if types differ.
          auto tmpAlloca = createTmpAlloca(*this, Fn.getLoc(), elt.getType());
          StoreOp::create(rewriter, Fn.getLoc(), elt, tmpAlloca.getResult(),
                          /*is_volatile=*/false, /*is_nontemporal=*/false,
                          /*alignment=*/tmpAlloca.getAlignmentAttr(),
                          /*sync_scope=*/cir::SyncScopeKindAttr{},
                          /*mem_order=*/cir::MemOrderAttr{},
                          /*tbaa=*/cir::TBAAAttr{});
          auto castPtr = CastOp::create(rewriter, Fn.getLoc(),
                                         PointerType::get(eltType),
                                         CastKind::bitcast,
                                         tmpAlloca.getResult());
          elt = LoadOp::create(rewriter, Fn.getLoc(), castPtr.getResult());
        }

        coercionValue = InsertMemberOp::create(
            rewriter, Fn.getLoc(), coercionValue,
            rewriter.getI64IntegerAttr(i), elt);
      }
      cir_cconv_assert(argIndex == FirstIRArg + NumIRArgs);

      StoreOp::create(rewriter, Fn.getLoc(), coercionValue,
                      coercionAddr.getResult(),
                      /*is_volatile=*/false, /*is_nontemporal=*/false,
                      /*alignment=*/Alloca.getAlignmentAttr(),
                      /*sync_scope=*/cir::SyncScopeKindAttr{},
                      /*mem_order=*/cir::MemOrderAttr{},
                      /*tbaa=*/cir::TBAAAttr{});

      ArgVals.push_back(Alloca);

      // Replace the original arg alloca with our reconstructed one.
      if (initStore2) {
        auto argAlloca2 = initStore2.store.getAddr();
        auto replacementAddr = adaptReplacementAddrForOriginalAddr(
            *this, Fn.getLoc(), Alloca.getResult(), argAlloca2);
        rewriter.replaceAllUsesWith(argAlloca2, replacementAddr);
        eraseArgInitStoreChain(rewriter, initStore2, argAlloca2);
      } else {
        replaceArgWithoutInitStoreChain(*this, rewriter, Fn.getLoc(),
                                        originalArg2, Alloca.getResult());
      }
      break;
    }
    case ABIArgInfo::InAlloca:
      cir_cconv_unreachable("NYI: InAlloca in function prolog");
    }
  }

  // CIRGen has already materialized parameter declarations, cleanups, and
  // source-language evaluation order. Target lowering only rewires ABI entry
  // values into those existing allocas, so there is no additional Microsoft
  // left-to-right callee-destruction work to do here.

  return llvm::success();
}

llvm::LogicalResult
LowerFunction::buildFunctionEpilog(const LowerFunctionInfo &FI) {
  // NOTE(cir): no-return, naked, and no result functions should be handled in
  // CIRGen.

  mlir::Value RV = {};
  mlir::Type RetTy = FI.getReturnType();
  const ABIArgInfo &RetAI = FI.getReturnInfo();

  // Write back swifterror temporaries before each return. LLVM requires the
  // final error value to be stored back to the swifterror parameter so it can
  // be propagated to the caller. This must run on every return path,
  // including the Direct/Extend early-return below, so it is factored out
  // here and invoked from all epilog exits.
  auto emitSwiftErrorWritebacks = [&]() {
    if (swiftErrorTemps.empty())
      return;
    llvm::SmallVector<ReturnOp> returns;
    NewFn->walk([&](ReturnOp ret) { returns.push_back(ret); });
    for (auto ret : returns) {
      mlir::PatternRewriter::InsertionGuard guard(rewriter);
      rewriter.setInsertionPoint(ret);
      for (auto &[tmpAlloca, swifterrorParam] : swiftErrorTemps) {
        auto val = LoadOp::create(rewriter, ret.getLoc(), tmpAlloca);
        StoreOp::create(rewriter, ret.getLoc(), val, swifterrorParam,
                        /*is_volatile=*/false, /*is_nontemporal=*/false,
                        /*alignment=*/mlir::IntegerAttr{},
                        /*sync_scope=*/cir::SyncScopeKindAttr{},
                        /*mem_order=*/cir::MemOrderAttr{},
                        /*tbaa=*/cir::TBAAAttr{});
      }
    }
  };

  switch (RetAI.getKind()) {

  case ABIArgInfo::Ignore:
    // The ABI treats this return as void (for example, ignored empty
    // aggregates). Drop the return operand and clean up the now-dead
    // source-level return-slot traffic that CIRGen left behind.
    {
      cir::AllocaOp retAlloca;
      NewFn->walk([&](ReturnOp ret) {
        if (!retAlloca)
          retAlloca = findAlloca(ret);
      });
      if (!retAlloca) {
        NewFn->walk([&](cir::AllocaOp alloca) {
          if (alloca.getName() == "__retval")
            retAlloca = alloca;
        });
      }

      llvm::SmallVector<ReturnOp> returns;
      NewFn->walk([&](ReturnOp ret) { returns.push_back(ret); });

      for (auto ret : returns) {
        if (ret.getInput().empty())
          continue;

        auto deadLoad = findLoadLike(ret.getInput().front());
        mlir::PatternRewriter::InsertionGuard guard(rewriter);
        rewriter.setInsertionPoint(ret);
        rewriter.replaceOpWithNewOp<ReturnOp>(ret);
        if (deadLoad && deadLoad.getResult().use_empty())
          rewriter.eraseOp(deadLoad);
      }

      if (retAlloca) {
        eraseDeadIgnoredRetSlotUsers(rewriter, retAlloca.getResult());
        if (retAlloca.getResult().use_empty())
          rewriter.eraseOp(retAlloca);
      }
    }
    break;

  case ABIArgInfo::Indirect: {
    mlir::Value RVAddr = {};
    CIRToCIRArgMapping IRFunctionArgs(LM.getContext(), FI, true);
    if (IRFunctionArgs.hasSRetArg()) {
      auto &entry = NewFn.getBody().front();
      RVAddr = entry.getArgument(IRFunctionArgs.getSRetArgNo());
    }

    if (RVAddr) {
      mlir::PatternRewriter::InsertionGuard guard(rewriter);
      if (auto storageTy = getBitIntStorageType(
              *this, RetTy, FI.getReturnArgInfo().isBitIntType())) {
        cir::AllocaOp retAlloca;
        NewFn->walk([&](ReturnOp ret) {
          if (!retAlloca)
            retAlloca = findAlloca(ret);
        });
        if (!retAlloca) {
          NewFn->walk([&](cir::AllocaOp alloca) {
            if (alloca.getName() == "__retval")
              retAlloca = alloca;
          });
        }
        if (retAlloca) {
          unsigned slotAlign = getAllocaAlign(retAlloca.getResult());
          for (mlir::OpOperand &use :
               llvm::make_early_inc_range(retAlloca.getResult().getUses())) {
            mlir::Operation *user = use.getOwner();
            if (auto store = mlir::dyn_cast<StoreOp>(user)) {
              if (store.getAddr() != retAlloca.getResult())
                continue;

              rewriter.setInsertionPoint(store);
              createBitIntStorageStore(
                  *this, store.getLoc(), store.getValue(), retAlloca.getResult(),
                  RetTy, storageTy, retAlloca.getAlignmentAttr(),
                  store.getTbaa().value_or(cir::TBAAAttr{}));
              rewriter.eraseOp(store);
            } else if (auto load = mlir::dyn_cast<LoadOp>(user)) {
              rewriter.setInsertionPoint(load);
              auto newLoad = createBitIntStorageLoad(
                  *this, load.getLoc(), retAlloca.getResult(), load.getType(),
                  storageTy, slotAlign);
              rewriter.replaceAllUsesWith(load.getResult(), newLoad);
              rewriter.eraseOp(load);
            }
          }
        }

        llvm::SmallVector<ReturnOp> returns;
        NewFn->walk([&](ReturnOp ret) { returns.push_back(ret); });

        mlir::IntegerAttr retAlignAttr;
        if (RetAI.getIndirectAlign())
          retAlignAttr = rewriter.getI64IntegerAttr(RetAI.getIndirectAlign());

        for (auto ret : returns) {
          if (ret.getInput().empty())
            continue;

          auto retInputs = ret.getInput();
          cir_cconv_assert(retInputs.size() == 1 &&
                           "return should only have one input");
          auto retValue = retInputs[0];

          // A return that forwards a musttail call result needs no store: the
          // musttail call already writes the result into the sret pointer.
          // Forward the function's own sret argument to the call (replacing the
          // local __retval slot it was given) and emit a bare return — a
          // musttail call must be immediately followed by the return with no
          // intervening ops. Detect the musttail call by scanning the return's
          // block rather than the return value's defining op, because earlier
          // lowering may interpose a load between the call and the return.
          cir::CallOp musttailCall;
          for (mlir::Operation &op : *ret->getBlock()) {
            if (auto c = mlir::dyn_cast<cir::CallOp>(op);
                c && c.getMustTailAttr()) {
              musttailCall = c;
              break;
            }
          }
          if (musttailCall) {
            if (retAlloca)
              rewriter.replaceAllUsesWith(retAlloca.getResult(), RVAddr);
            // Erase any (now dead) result-forwarding ops between the musttail
            // call and the return.
            llvm::SmallVector<mlir::Operation *> between;
            for (mlir::Operation *op = musttailCall->getNextNode();
                 op && op != ret.getOperation(); op = op->getNextNode())
              between.push_back(op);
            for (mlir::Operation *op : llvm::reverse(between))
              if (op->use_empty())
                rewriter.eraseOp(op);
            rewriter.setInsertionPoint(ret);
            rewriter.replaceOpWithNewOp<ReturnOp>(ret);
            continue;
          }

          rewriter.setInsertionPoint(ret);
          createBitIntStorageStore(*this, ret.getLoc(), retValue, RVAddr,
                                   RetTy, storageTy, retAlignAttr);
          rewriter.replaceOpWithNewOp<ReturnOp>(ret);

          if (auto load = retValue.getDefiningOp<cir::LoadOp>();
              load && load.getResult().use_empty())
            rewriter.eraseOp(load);
        }
        if (retAlloca && retAlloca.getResult().use_empty())
          rewriter.eraseOp(retAlloca);
        break;
      }

      // Find the __retval alloca once by inspecting any return op.
      // We must do this before the walk because erasing the alloca during the
      // walk would cause findAlloca to fail for subsequent return ops that
      // share the same alloca.
      cir::AllocaOp retAlloca;
      NewFn->walk([&](ReturnOp ret) {
        if (!retAlloca)
          retAlloca = findAlloca(ret);
      });

      // For musttail calls, the return is void (or carries an undef), so
      // findAlloca won't trace back to __retval. Fall back to scanning for
      // the __retval alloca by name.
      if (!retAlloca) {
        NewFn->walk([&](cir::AllocaOp alloca) {
          if (alloca.getName() == "__retval")
            retAlloca = alloca;
        });
      }

      if (retAlloca) {
        mlir::IntegerAttr retAlignAttr;
        if (RetAI.getIndirectAlign())
          retAlignAttr = rewriter.getI64IntegerAttr(RetAI.getIndirectAlign());
        if (retAlignAttr) {
          for (mlir::OpOperand &use :
               llvm::make_early_inc_range(retAlloca.getResult().getUses())) {
            mlir::Operation *user = use.getOwner();
            if (auto store = mlir::dyn_cast<StoreOp>(user)) {
              if (store.getAddr() == retAlloca.getResult() &&
                  !store.getAlignmentAttr())
                store.setAlignmentAttr(retAlignAttr);
            } else if (auto load = mlir::dyn_cast<LoadOp>(user)) {
              if (load.getAddr() == retAlloca.getResult() &&
                  !load.getAlignmentAttr())
                load.setAlignmentAttr(retAlignAttr);
            }
          }
        }
        // Replace all uses of the alloca with the sret pointer and erase it.
        rewriter.replaceAllUsesWith(retAlloca.getResult(), RVAddr);
        rewriter.eraseOp(retAlloca);
      }

      // Walk all returns and make them void. Collect first since we modify.
      llvm::SmallVector<ReturnOp> returns;
      NewFn->walk([&](ReturnOp ret) { returns.push_back(ret); });

      mlir::IntegerAttr retAlignAttr;
      if (RetAI.getIndirectAlign())
        retAlignAttr = rewriter.getI64IntegerAttr(RetAI.getIndirectAlign());

      for (auto ret : returns) {
        if (ret.getInput().empty())
          continue;
        rewriter.setInsertionPoint(ret);

        auto retInputs = ret.getInput();
        cir_cconv_assert(retInputs.size() == 1 &&
                         "return should only have one input");
        auto retValue = retInputs[0];
        auto load = retValue.getDefiningOp<cir::LoadOp>();

        // A musttail call has already written the result through the sret
        // pointer, and no op may sit between the call and the return.
        cir::CallOp musttailCall;
        for (mlir::Operation &op : *ret->getBlock()) {
          if (auto c = mlir::dyn_cast<cir::CallOp>(op);
              c && c.getMustTailAttr()) {
            musttailCall = c;
            break;
          }
        }

        // Discarding the operand is only correct when the value already lives
        // in the sret slot, i.e. it is a load from RVAddr -- which is what the
        // __retval RAUW above produces. A value still held in an SSA register
        // has no backing store, so making the return void would silently drop
        // it and leave the caller's sret slot uninitialized. mem2reg promotes
        // the single-assignment __retval slot of `return callee();`, so there
        // is no alloca left to redirect and the return operand is the raw
        // cir.call result.
        if (!musttailCall && (!load || load.getAddr() != RVAddr)) {
          mlir::Value dst = RVAddr;
          auto dstPtrTy = mlir::cast<PointerType>(RVAddr.getType());
          if (dstPtrTy.getPointee() != retValue.getType())
            dst = CastOp::create(
                rewriter, ret.getLoc(),
                PointerType::get(retValue.getType(), dstPtrTy.getAddrSpace()),
                CastKind::bitcast, RVAddr);
          StoreOp::create(rewriter, ret.getLoc(), retValue, dst,
                          /*is_volatile=*/false, /*is_nontemporal=*/false,
                          /*alignment=*/retAlignAttr,
                          /*sync_scope=*/cir::SyncScopeKindAttr{},
                          /*mem_order=*/cir::MemOrderAttr{},
                          /*tbaa=*/cir::TBAAAttr{});
        }

        rewriter.replaceOpWithNewOp<ReturnOp>(ret);

        // Clean up the now-unused load.
        if (load && load.getResult().use_empty())
          rewriter.eraseOp(load);
      }
    }
    break;
  }

  case ABIArgInfo::Extend:
  case ABIArgInfo::Direct:
    // FIXME(cir): Should we call ConvertType(RetTy) here?
    if (RetAI.getCoerceToType() == RetTy && RetAI.getDirectOffset() == 0) {
      // The internal return value temp always has pointer-to-return-type type,
      // so the generic path is to return the emitted load directly. Mirror
      // OGCG's single-store elision when the function returns a named
      // __retval slot that is written once and every return reloads from it.
      (void)tryElideSingleStoreReturnSlot(rewriter, NewFn);
      emitSwiftErrorWritebacks();
      return llvm::success();
    } else {
      // NOTE(cir): Unlike the original codegen, CIR may have multiple return
      // statements in the function body. We have to handle this here.
      mlir::PatternRewriter::InsertionGuard guard(rewriter);
      NewFn->walk([&](ReturnOp returnOp) {
        rewriter.setInsertionPoint(returnOp);
        auto retInput = returnOp->getOperand(0);

        // Skip coercion for returns that directly forward a musttail call
        // result. The musttail call will be separately lowered to return
        // the correct coerced type, and adding a coercion here would
        // insert intermediate ops between the musttail call and return,
        // violating LLVM's musttail requirements.
        if (auto callOp = retInput.getDefiningOp<cir::CallOp>()) {
          if (callOp.getMustTailAttr())
            return;
        }

        auto load = retInput.getDefiningOp<cir::LoadOp>();

        // If the value is offset in memory, apply the offset now.
        // This matches OGCG's emitAddressAtOffset call before
        // CreateCoercedLoad.
        if (RetAI.getDirectOffset() != 0 && load) {
          mlir::Value offsetPtr =
              buildAddressAtOffset(*this, load.getAddr(), RetAI);
          auto offsetLoad = LoadOp::create(rewriter, load.getLoc(), offsetPtr);
          mlir::IntegerAttr alignAttr = load.getAlignmentAttr();
          if (!alignAttr)
            alignAttr = getAddrAlignmentAttr(load.getAddr());
          if (alignAttr) {
            uint64_t baseAlign = alignAttr.getInt();
            uint64_t adjustedAlign =
                std::gcd(baseAlign, uint64_t(RetAI.getDirectOffset()));
            offsetLoad.setAlignmentAttr(rewriter.getI64IntegerAttr(
                adjustedAlign ? adjustedAlign : baseAlign));
          }
          RV = offsetLoad;
        } else {
          RV = castReturnValue(retInput, RetAI.getCoerceToType(), *this);
        }

        rewriter.replaceOpWithNewOp<ReturnOp>(returnOp, RV);
        // Clean up the now-unused load.
        if (load && load.getResult().use_empty())
          rewriter.eraseOp(load);
      });
    }

    // OGCG emits emitAutoreleaseOfResult here for ObjC ARC.
    // CIR currently has no dedicated ARC autorelease-result hook in
    // return-value ABI lowering.
    break;

  case ABIArgInfo::CoerceAndExpand: {
    auto coercionType = RetAI.getCoerceAndExpandType();
    auto unpaddedCoercionType = RetAI.getUnpaddedCoerceAndExpandType();
    auto unpaddedRecord = mlir::dyn_cast<RecordType>(unpaddedCoercionType);

    mlir::PatternRewriter::InsertionGuard guard(rewriter);
    NewFn->walk([&](ReturnOp returnOp) {
      rewriter.setInsertionPoint(returnOp);
      auto retInput = returnOp->getOperand(0);

      // Skip coercion for returns that directly forward a musttail call result.
      if (auto callOp = retInput.getDefiningOp<cir::CallOp>()) {
        if (callOp.getMustTailAttr())
          return;
      }

      auto load = retInput.getDefiningOp<cir::LoadOp>();
      mlir::Value retAddr = load ? load.getAddr() : retInput;

      // Bitcast the return value address to the padded coercion struct type.
      auto coercionPtrTy = PointerType::get(coercionType);
      auto coercionAddr = CastOp::create(rewriter, returnOp.getLoc(),
                                          coercionPtrTy, CastKind::bitcast,
                                          retAddr);
      auto coercionAlignAttr = getAddrAlignmentAttr(coercionAddr.getResult());

      mlir::Value coercionValue =
          LoadOp::create(rewriter, returnOp.getLoc(), coercionAddr.getResult(),
                         /*isDeref=*/false, /*is_volatile=*/false,
                         /*is_nontemporal=*/false,
                         /*alignment=*/coercionAlignAttr,
                         /*sync_scope=*/cir::SyncScopeKindAttr{},
                         /*mem_order=*/cir::MemOrderAttr{},
                         /*tbaa=*/cir::TBAAAttr{});

      // Load each non-padding element from the return value.
      llvm::SmallVector<mlir::Value, 4> results;
      unsigned unpaddedIndex = 0;
      for (unsigned i = 0, e = coercionType.getNumElements(); i != e; ++i) {
        auto coercedEltType = coercionType.getMembers()[i];
        if (ABIArgInfo::isPaddingForCoerceAndExpand(coercedEltType))
          continue;

        auto paramType = unpaddedRecord
                             ? unpaddedRecord.getMembers()[unpaddedIndex++]
                             : unpaddedCoercionType;

        mlir::Value elt = ExtractMemberOp::create(rewriter, returnOp.getLoc(),
                                                  coercionValue, i);

        // Coerce element type to match the unpadded type if they differ.
        if (elt.getType() != paramType) {
          if (auto coerced = tryCoerceVectorValue(*this, elt, paramType))
            elt = *coerced;
        }

        if (elt.getType() != paramType) {
          auto tmpAlloca =
              createTmpAlloca(*this, returnOp.getLoc(), elt.getType());
          StoreOp::create(rewriter, returnOp.getLoc(), elt,
                          tmpAlloca.getResult(), /*is_volatile=*/false,
                          /*is_nontemporal=*/false,
                          /*alignment=*/tmpAlloca.getAlignmentAttr(),
                          /*sync_scope=*/cir::SyncScopeKindAttr{},
                          /*mem_order=*/cir::MemOrderAttr{},
                          /*tbaa=*/cir::TBAAAttr{});
          auto castPtr = CastOp::create(rewriter, returnOp.getLoc(),
                                         PointerType::get(paramType),
                                         CastKind::bitcast,
                                         tmpAlloca.getResult());
          elt = LoadOp::create(rewriter, returnOp.getLoc(), castPtr.getResult());
        }

        results.push_back(elt);
      }

      mlir::Value newRV;
      if (results.size() == 1) {
        // Single result — return it directly.
        newRV = results[0];
      } else {
        // Multiple results — pack into the unpadded record type.
        auto unpaddedRecTy = mlir::cast<RecordType>(unpaddedCoercionType);
        newRV = ConstantOp::create(
            rewriter, returnOp.getLoc(), UndefAttr::get(unpaddedRecTy));
        for (unsigned i = 0, e = results.size(); i != e; ++i)
          newRV = InsertMemberOp::create(
              rewriter, returnOp.getLoc(), newRV,
              rewriter.getI64IntegerAttr(i), results[i]);
      }

      rewriter.replaceOpWithNewOp<ReturnOp>(returnOp, newRV);
      // Clean up the now-unused load.
      if (load && load.getResult().use_empty())
        rewriter.eraseOp(load);
    });
    break;
  }

  case ABIArgInfo::InAlloca:
    cir_cconv_unreachable("NYI: InAlloca in function epilog");

  case ABIArgInfo::IndirectAliased:
    cir_cconv_unreachable("Invalid ABIArgInfo for return value");
  }

  // OGCG clears padding bits for CmseNSEntryAttr returns and calls
  // EmitReturnValueCheck for sanitizer-based return value validation.
  // OGCG clears padding bits for CMSE non-secure entry returns.
  // CMSE is ARM Cortex-M only, not AArch64. Not a CIR target.
  // CIR has no dedicated sanitizer return-value validation hook in
  // target-lowering epilog emission yet.

  // Write back swifterror temporaries before each return for the return-ABI
  // kinds that fall through the switch above (the Direct/Extend fast path
  // handles this at its own early return).
  emitSwiftErrorWritebacks();

  return llvm::success();
}

/// Generate code for a function based on the ABI-specific information.
///
/// This method has partial parity with CodeGenFunction::GenerateCode, but it
/// focuses on the ABI-specific details. So a lot of codegen stuff is removed.
llvm::LogicalResult
LowerFunction::generateCode(FuncOp oldFn, FuncOp newFn,
                            const LowerFunctionInfo &FnInfo) {
  cir_cconv_assert(newFn && "generating code for null Function");
  auto Args = oldFn.getArguments();

  // Emit the ABI-specific function prologue.
  cir_cconv_assert(newFn.empty() && "Function already has a body");
  rewriter.setInsertionPointToEnd(newFn.addEntryBlock());
  if (buildFunctionProlog(FnInfo, newFn, oldFn.getArguments()).failed())
    return llvm::failure();

  // Ensure that old ABI-agnostic arguments uses were replaced.
  const auto hasNoUses = [](mlir::Value val) { return val.getUses().empty(); };
  cir_cconv_assert(std::all_of(Args.begin(), Args.end(), hasNoUses) &&
                   "Missing RAUW?");

  // NOTE(cir): While the new function has the ABI-aware parameters, the old
  // function still has the function logic. To complete the migration, we have
  // to move the old function body to the new function.

  // Backup references  to entry blocks.
  mlir::Block *srcBlock = &oldFn.getBody().front();
  mlir::Block *dstBlock = &newFn.getBody().front();

  // Ensure both blocks have the same number of arguments in order to
  // safely merge them.
  CIRToCIRArgMapping IRFunctionArgs(LM.getContext(), FnInfo, true);
  if (IRFunctionArgs.hasSRetArg()) {
    auto dstIndex = IRFunctionArgs.getSRetArgNo();
    auto retArg = dstBlock->getArguments()[dstIndex];
    srcBlock->insertArgument(dstIndex, retArg.getType(), retArg.getLoc());
  }

  // Migrate function body to new ABI-aware function.
  rewriter.inlineRegionBefore(oldFn.getBody(), newFn.getBody(),
                              newFn.getBody().end());

  // The block arguments of srcBlock are the old function's arguments. At this
  // point, all old arguments should be replaced with the lowered values.
  // Thus we could safely remove all the block arguments on srcBlock here.
  srcBlock->eraseArguments(0, srcBlock->getNumArguments());

  // Merge entry blocks to ensure correct branching.
  rewriter.mergeBlocks(srcBlock, dstBlock);

  // FIXME(cir): What about saving parameters for corotines? Should we do
  // something about it in this pass? If the change with the calling
  // convention, we might have to handle this here.

  // Emit the standard function epilogue.
  if (buildFunctionEpilog(FnInfo).failed())
    return llvm::failure();

  return llvm::success();
}

void LowerFunction::buildAggregateStore(mlir::Value Val, mlir::Value Dest,
                                        bool DestIsVolatile) {
  // In LLVM codegen:
  // Function to store a first-class aggregate into memory. We prefer to
  // store the elements rather than the aggregate to be more friendly to
  // fast-isel.
  cir_cconv_assert(mlir::isa<PointerType>(Dest.getType()) &&
                   "Storing in a non-pointer!");

  // Circumvent CIR's type checking.
  mlir::Type pointeeTy = mlir::cast<PointerType>(Dest.getType()).getPointee();
  if (Val.getType() != pointeeTy) {
    // NOTE(cir):  We only bitcast and store if the types have the same size.
    cir_cconv_assert((LM.getDataLayout().getTypeSizeInBits(Val.getType()) ==
                      LM.getDataLayout().getTypeSizeInBits(pointeeTy)) &&
                     "Incompatible types");
    auto loc = Val.getLoc();
    Val = CastOp::create(rewriter, loc, pointeeTy, CastKind::bitcast, Val);
  }

  // Use the destination alloca's alignment, matching OGCG's behavior.
  mlir::IntegerAttr alignAttr;
  if (auto alloca = findAlloca(Dest.getDefiningOp()))
    alignAttr = alloca.getAlignmentAttr();

  StoreOp::create(rewriter, Val.getLoc(), Val, Dest,
                  DestIsVolatile, /*is_nontemporal=*/false,
                  /*alignment=*/alignAttr,
                  /*sync_scope=*/cir::SyncScopeKindAttr{},
                  /*mem_order=*/cir::MemOrderAttr{},
                  /*tbaa=*/cir::TBAAAttr{});
}

mlir::Value LowerFunction::buildAggregateBitcast(mlir::Value Val,
                                                 mlir::Type DestTy) {
  auto Cast = createCoercedBitcast(Val, DestTy, *this);
  // Use the source alloca's alignment to match OGCG, which uses the Address
  // alignment rather than the coerced type's natural alignment.
  mlir::IntegerAttr alignAttr;
  if (auto alloca = findAlloca(Val.getDefiningOp()))
    alignAttr = alloca.getAlignmentAttr();
  return LoadOp::create(rewriter, Val.getLoc(), Cast,
                        /*isDeref=*/false, /*is_volatile=*/false,
                        /*is_nontemporal=*/false, /*alignment=*/alignAttr,
                        /*sync_scope=*/cir::SyncScopeKindAttr{},
                        /*mem_order=*/cir::MemOrderAttr{},
                        /*tbaa=*/cir::TBAAAttr{});
}

/// Rewrite a call operation to abide to the ABI calling convention.
///
/// FIXME(cir): This method has partial parity to CodeGenFunction's
/// EmitCallEpxr method defined in CGExpr.cpp. This could likely be
/// removed in favor of a more direct approach.
llvm::LogicalResult LowerFunction::rewriteCallOp(CallOp op,
                                                 ReturnValueSlot retValSlot) {

  // TODO(cir): Check if BlockCall, CXXMemberCall, CUDAKernelCall, or
  // CXXOperatorMember require special handling here. These should be handled
  // in CIRGen, unless there is call conv or ABI-specific stuff to be handled,
  // them we should do it here.

  // TODO(cir): Also check if Builtin and CXXPeseudoDtor need special handling
  // here. These should be handled in CIRGen, unless there is call conv or
  // ABI-specific stuff to be handled, them we should do it here.

  // NOTE(cir): There is no direct way to fetch the function type from the
  // CallOp, so we fetch it from the source function. This assumes the
  // function definition has not yet been lowered.

  FuncType fnType;
  if (SrcFn) {
    fnType = SrcFn.getFunctionType();
  } else if (op.isIndirect()) {
    if (auto ptrTy =
            mlir::dyn_cast<PointerType>(op.getIndirectCall().getType()))
      fnType = mlir::dyn_cast<FuncType>(ptrTy.getPointee());
  }

  cir_cconv_assert(fnType && "No callee function type");

  mlir::Value staticChain;
  if (op->hasAttr("cir.static_chain")) {
    auto rawArgs = op.getArgs();
    unsigned chainIndexFromBack = op->hasAttr("cir.runtime_ptrauth") ? 2 : 1;
    cir_cconv_assert(rawArgs.size() >= chainIndexFromBack &&
                     "static chain call missing hidden chain operand");
    staticChain = rawArgs[rawArgs.size() - chainIndexFromBack];
  }

  // Rewrite the call operation to abide to the ABI calling convention.
  auto Ret = rewriteCallOp(fnType, SrcFn, op, retValSlot, staticChain);

  // Replace the original call result with the new one.
  bool hasOriginalResult = op.getNumResults() > 0;
  if (Ret) {
    if (hasOriginalResult)
      rewriter.replaceAllUsesWith(op.getResult(), Ret);
  } else if (hasOriginalResult && !op.getResult().use_empty()) {
    // If the original call had a result but the lowered call returns void
    // (e.g., ignored empty struct return), erase dead user operations that
    // just store the (now-void) result. This matches OGCG, which skips the
    // aggregate copy for empty structs in emitAggregateCopy.
    //
    // For any remaining non-store uses, fall back to replacing with undef.
    llvm::SmallVector<mlir::Operation *, 4> usersToErase;
    for (mlir::OpOperand &use : op.getResult().getUses()) {
      if (auto storeOp = mlir::dyn_cast<StoreOp>(use.getOwner())) {
        // Only erase stores where the call result is the source (value being
        // stored), not the destination pointer.
        if (storeOp.getValue() == op.getResult())
          usersToErase.push_back(storeOp);
      }
    }
    for (mlir::Operation *user : usersToErase)
      rewriter.eraseOp(user);

    // If there are still remaining uses after erasing stores, replace them
    // with undef as a fallback.
    if (!op.getResult().use_empty()) {
      // When lowering a try_call (tryCallNormalDest set), the new call is a
      // terminator with successors; its result is only live on the normal
      // path, so the undef placeholder must be created at the start of the
      // normal-dest block, not after the terminator.
      if (tryCallNormalDest) {
        if (!tryCallNormalDest->empty() &&
            mlir::isa<cir::LabelOp>(tryCallNormalDest->front()))
          rewriter.setInsertionPointAfter(&tryCallNormalDest->front());
        else
          rewriter.setInsertionPointToStart(tryCallNormalDest);
      } else {
        rewriter.setInsertionPoint(op);
      }
      auto undef = cir::ConstantOp::create(
          rewriter, op.getLoc(),
          cir::UndefAttr::get(op.getResult().getType()));
      rewriter.replaceAllUsesWith(op.getResult(), undef);
    }
  }
  // Collect the original call's operands before erasing the call. After
  // erasing, some operands (e.g., struct loads for byval args) may become
  // dead because the ABI lowering bypasses them via findAddr() and creates
  // memcpy from the source address directly.
  llvm::SmallVector<mlir::Value, 4> oldOperands(op.getArgOperands());

  // Erase original ABI-agnostic call.
  rewriter.eraseOp(op);

  // Clean up dead operands. When the ABI lowering traces through a LoadOp
  // to the source address (findAddr), the loaded value becomes unused.
  // Erasing it here avoids emitting dead loads in the lowered IR.
  for (mlir::Value operand : oldOperands) {
    if (auto loadOp =
            mlir::dyn_cast_if_present<LoadOp>(operand.getDefiningOp())) {
      if (loadOp.getResult().use_empty())
        rewriter.eraseOp(loadOp);
    }
  }

  return llvm::success();
}

/// Rewrite a try_call operation to abide to the ABI calling convention.
/// This mirrors rewriteCallOp(CallOp) but preserves the TryCallOp's
/// normal/unwind destination blocks.
llvm::LogicalResult LowerFunction::rewriteCallOp(TryCallOp op,
                                                 ReturnValueSlot retValSlot) {
  FuncType fnType;
  if (SrcFn) {
    fnType = SrcFn.getFunctionType();
  } else if (op.isIndirect()) {
    if (auto ptrTy =
            mlir::dyn_cast<PointerType>(op.getIndirectCall().getType()))
      fnType = mlir::dyn_cast<FuncType>(ptrTy.getPointee());
  }

  cir_cconv_assert(fnType && "No callee function type");

  // Store successor blocks so the inner rewriteCallOp creates a TryCallOp.
  tryCallNormalDest = op.getNormalDest();
  tryCallUnwindDest = op.getUnwindDest();

  // Create a temporary CallOp from the TryCallOp's operands/attributes so
  // we can reuse the existing ABI lowering pipeline.
  // An indirect try_call keeps the callee pointer as its first operand, which
  // getArgOperands() deliberately hides. The temporary has to carry it too,
  // otherwise it looks like a direct call to a null symbol and the pipeline
  // never reaches the indirect-call path.
  llvm::SmallVector<mlir::Value, 16> tmpArgs;
  if (op.isIndirect())
    tmpArgs.push_back(op.getIndirectCall());
  llvm::append_range(tmpArgs, op.getArgOperands());

  rewriter.setInsertionPoint(op);
  auto tmpCallOp = CallOp::create(
      rewriter, op.getLoc(), op.getCalleeAttr(),
      op.getNumResults() > 0 ? op.getResult().getType()
                             : cir::VoidType::get(op.getContext()),
      tmpArgs, op.getCallingConv(), op.getSideEffect(),
      /*exception=*/mlir::UnitAttr());

  // Copy discardable attributes.
  for (auto attr : op->getDiscardableAttrs())
    tmpCallOp->setAttr(attr.getName(), attr.getValue());
  // Copy extra_attrs if present.
  if (auto extraAttrs = op.getExtraAttrsAttr())
    tmpCallOp->setAttr("extra_attrs", extraAttrs);

  // Hand the temporary the real uses of the result before lowering it. The
  // ABI lowering inspects the users of the call result -- to pick a direct
  // destination for an indirect (sret) return, and to drop the stores the
  // lowered call has made redundant -- and a result with no users looks to it
  // like a result nobody wants. It would then route the return through a fresh
  // temporary and erase the store that was supposed to publish it, or replace
  // the value with undef, leaving the real destination unwritten. The
  // temporary call sits immediately before the terminator, so it dominates
  // every use the terminator's result had.
  bool hasOriginalResult = op.getNumResults() > 0;
  if (hasOriginalResult)
    rewriter.replaceAllUsesWith(op.getResult(), tmpCallOp.getResult());

  // Rewrite the temporary CallOp using the existing ABI lowering pipeline.
  // The inner rewriteCallOp will detect tryCallNormalDest/tryCallUnwindDest
  // and create a TryCallOp instead of a CallOp.
  auto Ret = rewriteCallOp(fnType, SrcFn, tmpCallOp, retValSlot);

  // Replace the temporary's result with the lowered one.
  if (Ret) {
    if (hasOriginalResult)
      rewriter.replaceAllUsesWith(tmpCallOp.getResult(), Ret);
  } else if (hasOriginalResult && !tmpCallOp.getResult().use_empty()) {
    llvm::SmallVector<mlir::Operation *, 4> usersToErase;
    for (mlir::OpOperand &use : tmpCallOp.getResult().getUses()) {
      if (auto storeOp = mlir::dyn_cast<StoreOp>(use.getOwner())) {
        if (storeOp.getValue() == tmpCallOp.getResult())
          usersToErase.push_back(storeOp);
      }
    }
    for (mlir::Operation *user : usersToErase)
      rewriter.eraseOp(user);
    if (!tmpCallOp.getResult().use_empty()) {
      // The try_call's result is only live on the normal (non-exception) path.
      // A try_call is a terminator with successors, so the undef placeholder
      // must be created at the start of the normal-dest block -- inserting it
      // at the call site would place a non-terminator after the (new) try_call
      // terminator, corrupting the block.
      if (mlir::Block *normalDest = op.getNormalDest()) {
        if (!normalDest->empty() &&
            mlir::isa<cir::LabelOp>(normalDest->front()))
          rewriter.setInsertionPointAfter(&normalDest->front());
        else
          rewriter.setInsertionPointToStart(normalDest);
      } else {
        rewriter.setInsertionPoint(op);
      }
      auto undef = cir::ConstantOp::create(
          rewriter, op.getLoc(),
          cir::UndefAttr::get(tmpCallOp.getResult().getType()));
      rewriter.replaceAllUsesWith(tmpCallOp.getResult(), undef);
    }
  }

  // The inner pipeline created a new TryCallOp but does not erase the
  // temporary CallOp we passed in. Erase it now to avoid leaving a dead
  // op in the IR.
  rewriter.eraseOp(tmpCallOp);

  llvm::SmallVector<mlir::Value, 4> oldOperands(op.getArgOperands());

  // Erase the original TryCallOp.
  rewriter.eraseOp(op);

  // Clean up dead operands.
  for (mlir::Value operand : oldOperands) {
    if (auto loadOp =
            mlir::dyn_cast_if_present<LoadOp>(operand.getDefiningOp())) {
      if (loadOp.getResult().use_empty())
        rewriter.eraseOp(loadOp);
    }
  }

  // Reset successor tracking.
  tryCallNormalDest = nullptr;
  tryCallUnwindDest = nullptr;

  return llvm::success();
}

/// Rewrite a call operation to abide to the ABI calling convention.
///
/// FIXME(cir): This method has partial parity to CodeGenFunction's EmitCall
/// method defined in CGExpr.cpp. This could likely be removed in favor of a
/// more direct approach since most of the code here is exclusively CodeGen.
mlir::Value LowerFunction::rewriteCallOp(FuncType calleeTy, FuncOp origCallee,
                                         CallOp callOp,
                                         ReturnValueSlot retValSlot,
                                         mlir::Value Chain) {
  // NOTE(cir): Skip a bunch of function pointer stuff and AST declaration
  // asserts. Also skip sanitizers, as these should likely be handled at
  // CIRGen.
  CallArgList Args;

  // NOTE(cir): Call args were already emitted in CIRGen. Skip the evaluation
  // order done in CIRGen and just fetch the exiting arguments here.
  Args.append(callOp.getArgOperands().begin(), callOp.getArgOperands().end());
  if (Chain)
    Args.insert(Args.begin(), Chain);
  llvm::SmallVector<char, 8> argLValueFlags(Args.size(), 0);
  if (auto argLValues =
          callOp->getAttrOfType<mlir::DenseBoolArrayAttr>("cir.arg_lvalues")) {
    unsigned argOffset = Chain ? 1u : 0u;
    for (unsigned i = 0; i < argLValues.size() &&
                         i + argOffset < argLValueFlags.size();
         ++i)
      argLValueFlags[i + argOffset] = argLValues[i] ? 1 : 0;
  }

  auto resultProvenance = LowerFunctionInfoArgInfo::TypeProvenance::None;
  llvm::SmallVector<LowerFunctionInfoArgInfo::TypeProvenance, 8>
      argProvenances(Args.size(),
                     LowerFunctionInfoArgInfo::TypeProvenance::None);
  auto inferBitIntProvenanceFromType = [](mlir::Type ty) {
    auto intTy = mlir::dyn_cast<cir::IntType>(ty);
    if (!intTy)
      return LowerFunctionInfoArgInfo::TypeProvenance::None;
    switch (intTy.getWidth()) {
    case 8:
    case 16:
    case 32:
    case 64:
    case 128:
      return LowerFunctionInfoArgInfo::TypeProvenance::None;
    default:
      return LowerFunctionInfoArgInfo::TypeProvenance::BitInt;
    }
  };
  auto addArgProvenances =
      [&](mlir::DenseBoolArrayAttr attrs,
          LowerFunctionInfoArgInfo::TypeProvenance flag) {
        if (!attrs)
          return;
        unsigned argOffset = Chain ? 1u : 0u;
        for (unsigned i = 0; i < attrs.size() &&
                             i + argOffset < argProvenances.size();
             ++i)
          if (attrs[i])
            argProvenances[i + argOffset] |= flag;
      };
  if (origCallee) {
    if (origCallee->hasAttr("cir.ret.bitint"))
      resultProvenance |= LowerFunctionInfoArgInfo::TypeProvenance::BitInt;
    if (origCallee->hasAttr("cir.ret.matrix"))
      resultProvenance |= LowerFunctionInfoArgInfo::TypeProvenance::Matrix;
    addArgProvenances(
        origCallee->getAttrOfType<mlir::DenseBoolArrayAttr>("cir.bitint_args"),
        LowerFunctionInfoArgInfo::TypeProvenance::BitInt);
    addArgProvenances(
        origCallee->getAttrOfType<mlir::DenseBoolArrayAttr>("cir.matrix_args"),
        LowerFunctionInfoArgInfo::TypeProvenance::Matrix);
  }
  for (auto [idx, arg] : llvm::enumerate(callOp.getArgOperands()))
    if (argProvenances[idx + (Chain ? 1u : 0u)] ==
        LowerFunctionInfoArgInfo::TypeProvenance::None)
      argProvenances[idx + (Chain ? 1u : 0u)] =
          inferBitIntProvenanceFromType(arg.getType());

  auto isInstanceMethodCallee = [&](cir::FuncOp callee) {
    auto ast = callee.getAstAttr();
    if (!ast)
      return false;
    if (auto methodAst = mlir::dyn_cast<cir::ASTCXXMethodDeclAttr>(ast))
      return methodAst.getAst()->isImplicitObjectMemberFunction();
    return mlir::isa<cir::ASTCXXConstructorDeclAttr,
                     cir::ASTCXXConversionDeclAttr,
                     cir::ASTCXXDestructorDeclAttr>(ast);
  };
  bool instanceMethod = origCallee && isInstanceMethodCallee(origCallee);

  auto hasMfp8ABIOverride = [&](cir::FuncOp callee) {
    if (!callee)
      return false;
    if (callee->hasAttr("cir.mfp8_ret"))
      return true;
    for (unsigned i = 0; i < callee.getNumArguments(); ++i)
      if (callee.getArgAttr(i, "cir.mfp8"))
        return true;
    return false;
  };
  bool needsMfp8ABIOverride = origCallee && hasMfp8ABIOverride(origCallee);

  // Propagate __attribute__((regparm(N))) from the callee (direct calls) or
  // the call site (indirect calls through a regparm function pointer) so the
  // X86-32 ABI classifier marks the register-passed arguments `inreg`.
  bool hasRegParm = false;
  unsigned regParm = 0;
  if (auto regParmAttr =
          callOp->getAttrOfType<mlir::IntegerAttr>("cir.regparm")) {
    hasRegParm = true;
    regParm = regParmAttr.getValue().getZExtValue();
  } else if (origCallee) {
    if (auto regParmAttr =
            origCallee->getAttrOfType<mlir::IntegerAttr>("cir.regparm")) {
      hasRegParm = true;
      regParm = regParmAttr.getValue().getZExtValue();
    }
  }

  const LowerFunctionInfo &FnInfo = LM.getTypes().arrangeFreeFunctionCall(
      Args, calleeTy, /*chainCall=*/static_cast<bool>(Chain), instanceMethod,
      resultProvenance, argProvenances,
      callOp.getCallingConv(), !needsMfp8ABIOverride, hasRegParm, regParm);
  if (needsMfp8ABIOverride)
    LM.applyMfp8Overrides(origCallee, FnInfo, Chain ? 1u : 0u);

  // C99 6.5.2.2p6:
  //   If the expression that denotes the called function has a type
  //   that does not include a prototype, [the default argument
  //   promotions are performed]. If the number of arguments does not
  //   equal the number of parameters, the behavior is undefined. If
  //   the function is defined with a type that includes a prototype,
  //   and either the prototype ends with an ellipsis (, ...) or the
  //   types of the arguments after promotion are not compatible with
  //   the types of the parameters, the behavior is undefined. If the
  //   function is defined with a type that does not include a
  //   prototype, and the types of the arguments after promotion are
  //   not compatible with those of the parameters after promotion,
  //   the behavior is undefined [except in some trivial cases].
  // That is, in the general case, we should assume that a call
  // through an unprototyped function type works like a *non-variadic*
  // call.  The way we make this work is to cast to the exact type
  // of the promoted arguments.
  //
  // Chain calls use this same code path to add the invisible chain parameter
  // to the function type.
  if ((origCallee && origCallee.getNoProto()) || Chain) {
    // NOTE(cir): For no-proto functions, the original codegen casts arguments
    // to promoted types. In CIR, the call arguments are already properly typed,
    // so we can proceed with normal lowering.
  }

  // TODO(cir): LLVM IR has the concept of "CallBase", which is a base class
  // for all types of calls. Perhaps we should have a CIR interface to mimic
  // this class.
  CallOp CallOrInvoke = {};
  mlir::Value CallResult =
      rewriteCallOp(FnInfo, origCallee, callOp, retValSlot, Args,
                    argLValueFlags, CallOrInvoke, /*isMustTail=*/false,
                    callOp.getLoc());

  // NOTE(cir): Skipping debug stuff here.

  return CallResult;
}

mlir::Value createAlloca(mlir::Location loc, mlir::Type type,
                         LowerFunction &CGF, unsigned explicitAlign = 0) {
  auto align = explicitAlign ? explicitAlign
                             : CGF.LM.getDataLayout().getABITypeAlign(type).value();
  auto alignAttr = CGF.getRewriter().getI64IntegerAttr(align);
  return AllocaOp::create(CGF.getRewriter(), loc, cir::PointerType::get(type),
                          type,
                          /*name=*/llvm::StringRef(""), alignAttr);
}

// NOTE(cir): This method has partial parity to CodeGenFunction's EmitCall
// method in CGCall.cpp. When incrementing it, use the original codegen as a
// reference: add ABI-specific stuff and skip codegen stuff.
mlir::Value LowerFunction::rewriteCallOp(const LowerFunctionInfo &CallInfo,
                                         FuncOp Callee, CallOp Caller,
                                         ReturnValueSlot ReturnValue,
                                         CallArgList &CallArgs,
                                         llvm::ArrayRef<char> ArgLValueFlags,
                                         CallOp CallOrInvoke, bool isMustTail,
                                         mlir::Location loc) {
  // FIXME: We no longer need the types from CallArgs; lift up and simplify.

  // Handle record-return functions by passing a pointer to the
  // location that we would like to return into.
  mlir::Type RetTy = CallInfo.getReturnType(); // ABI-agnostic type.
  const cir::ABIArgInfo &RetAI = CallInfo.getReturnInfo();

  FuncType IRFuncTy = LM.getTypes().getFunctionType(CallInfo);
  // NOTE(cir): Some target/ABI related checks happen here. They are skipped
  // under the assumption that they are handled in CIRGen.

  // 1. Set up the arguments.

  // If we're using inalloca, insert the allocation after the stack save.
  // FIXME: Do this earlier rather than hacking it in here!
  if (RecordType ArgRecord = CallInfo.getArgRecord()) {
    cir_cconv_unreachable("NYI");
  }

  CIRToCIRArgMapping IRFunctionArgs(LM.getContext(), CallInfo);
  llvm::SmallVector<mlir::Value, 16> IRCallArgs(IRFunctionArgs.totalIRArgs());

  mlir::Value sRetPtr;
  bool sRetIsDirectDst = false;
  // If the call returns a temporary with record return, create a temporary
  // alloca to hold the result, unless one is given to us.
  if (RetAI.isIndirect() || RetAI.isCoerceAndExpand() || RetAI.isInAlloca()) {
    if (!ReturnValue.isNull()) {
      sRetPtr = ReturnValue.getValue();
      sRetIsDirectDst = true;
    } else if (Caller.getMustTailAttr() && RetAI.isIndirect()) {
      // For musttail calls with sret return, forward the enclosing function's
      // __retval alloca as the sret pointer. buildFunctionEpilog (Indirect
      // case) will later replace __retval with the function's own sret
      // parameter, ensuring the musttail callee writes directly into the
      // caller's sret destination. This matches OGCG which forwards
      // %agg.result to the musttail callee. Using a fresh alloca would cause
      // mismatched ABI attributes between the function's sret and the call's
      // sret, violating LLVM's musttail compatibility requirements.
      auto enclosingFn =
          Caller->getParentOfType<cir::FuncOp>();
      cir_cconv_assert(enclosingFn && "musttail call must be inside a function");
      cir::AllocaOp retAlloca;
      enclosingFn->walk([&](cir::AllocaOp alloca) {
        if (alloca.getName() == "__retval")
          retAlloca = alloca;
      });
      if (retAlloca) {
        sRetPtr = retAlloca.getResult();
      } else {
        // buildFunctionEpilog may have already replaced __retval with the
        // enclosing function's sret parameter (when the function definition
        // was lowered before this call site). In that case, find the sret
        // argument by looking for the cir.sret attribute on function args.
        mlir::Value sretArg;
        for (unsigned i = 0; i < enclosingFn.getNumArguments(); ++i) {
          if (enclosingFn.getArgAttr(i, "cir.sret")) {
            sretArg = enclosingFn.getBody().front().getArgument(i);
            break;
          }
        }
        cir_cconv_assert(sretArg &&
                         "musttail sret requires enclosing function sret arg");
        sRetPtr = sretArg;
      }
      sRetIsDirectDst = true;
    } else {
      // Check if the call result is stored directly into an alloca.
      // If so, pass that alloca as sret to avoid an intermediate copy.
      // This is critical for types like std::string where bitwise copy
      // of SSO data creates dangling internal pointers.
      mlir::Value directDst;
      if (Caller.getResult()) {
        for (auto *user : Caller.getResult().getUsers()) {
          if (auto storeOp = mlir::dyn_cast<StoreOp>(user)) {
            if (!directDst)
              directDst = storeOp.getAddr();
            else { directDst = nullptr; break; }
          }
        }
      }
      if (directDst) {
        sRetPtr = directDst;
        sRetIsDirectDst = true;
      } else {
        if (auto storageTy = getBitIntStorageType(
                *this, RetTy, CallInfo.getReturnArgInfo().isBitIntType())) {
          unsigned retAlign = RetAI.getIndirectAlign();
          if (!retAlign)
            retAlign = getBitIntStorageAlign(
                *this, RetTy, CallInfo.getReturnArgInfo().isBitIntType());
          auto storageAlloca = createAlloca(loc, storageTy, *this, retAlign);
          auto storagePtrTy = mlir::cast<PointerType>(storageAlloca.getType());
          auto semanticPtrTy =
              PointerType::get(RetTy, storagePtrTy.getAddrSpace());
          sRetPtr = CastOp::create(rewriter, loc, semanticPtrTy,
                                   CastKind::bitcast, storageAlloca);
        } else {
          sRetPtr = createAlloca(loc, RetTy, *this);
        }
      }
    }
    // CoerceAndExpand uses sRetPtr as a local temp for reassembly, not as an
    // sret argument. Only set the sret call arg for Indirect/InAlloca returns.
    if (!RetAI.isCoerceAndExpand()) {
      auto sretArgNo = IRFunctionArgs.getSRetArgNo();
      // If the sret pointer's address space doesn't match the function
      // parameter type (e.g. CIR alloca in default AS vs function expecting
      // target_address_space(5) on AMDGPU), insert an address space cast.
      auto sretPtrTy = mlir::cast<PointerType>(sRetPtr.getType());
      auto expectedPtrTy =
          mlir::cast<PointerType>(IRFuncTy.getInput(sretArgNo));
      if (sretPtrTy.getAddrSpace() != expectedPtrTy.getAddrSpace()) {
        auto newPtrTy = PointerType::get(sretPtrTy.getPointee(),
                                         expectedPtrTy.getAddrSpace());
        sRetPtr = CastOp::create(rewriter, loc, newPtrTy,
                                 CastKind::address_space, sRetPtr);
      }
      IRCallArgs[sretArgNo] = sRetPtr;
    }
  }

  // NOTE(cir): OGCG sets up swifterror writeback slots here. We handle
  // swifterror at each call argument below.

  // NOTE(cir): Skipping lifetime markers here.
  const auto &cgo = LM.getContext().getCodeGenOpts();
  bool emitCallArgLifetimeMarkers =
      !cgo.DisableLifetimeMarkers && cgo.OptimizationLevel != 0;
  llvm::SmallVector<mlir::Value, 4> callArgLifetimeEndAfterCall;

  // Track swifterror alloca/original-location pairs for post-call writeback.
  llvm::SmallVector<std::pair<mlir::Value, mlir::Value>, 1>
      swifterrorCallWritebacks;

  // Translate all of the arguments as necessary to match the IR lowering.
  cir_cconv_assert(CallInfo.arg_size() == CallArgs.size() &&
                   "Mismatch between function signature & arguments.");
  unsigned ArgNo = 0;
  LowerFunctionInfo::const_arg_iterator info_it = CallInfo.arg_begin();
  for (auto I = CallArgs.begin(), E = CallArgs.end(); I != E;
       ++I, ++info_it, ++ArgNo) {
    const ABIArgInfo &ArgInfo = info_it->info;

    if (IRFunctionArgs.hasPaddingArg(ArgNo))
      IRCallArgs[IRFunctionArgs.getPaddingArgNo(ArgNo)] =
          cir::ConstantOp::create(
              rewriter, loc, cir::UndefAttr::get(ArgInfo.getPaddingType()));

    unsigned FirstIRArg, NumIRArgs;
    std::tie(FirstIRArg, NumIRArgs) = IRFunctionArgs.getIRArgs(ArgNo);

    // Check if the callee's parameter has __attribute__((maybe_undef)).
    // Variadic arguments (beyond the required count) never have the attribute.
    // Matches OGCG's IsArgumentMaybeUndef (CGCall.cpp:2323-2342).
    bool ArgHasMaybeUndefAttr = false;
    if (Callee && ArgNo < Callee.getNumArguments() &&
        ArgNo < CallInfo.getNumRequiredArgs()) {
      ArgHasMaybeUndefAttr =
          Callee.getArgAttr(ArgNo, "cir.maybe_undef") != nullptr;
    }

    switch (ArgInfo.getKind()) {
    case ABIArgInfo::Extend:
    case ABIArgInfo::Direct: {
      // Empty struct flattened to zero IR args — nothing to pass.
      if (NumIRArgs == 0)
        break;

      if (mlir::isa<BoolType>(info_it->type)) {
        IRCallArgs[FirstIRArg] = *I;
        break;
      }

      if (!mlir::isa<RecordType>(ArgInfo.getCoerceToType()) &&
          ArgInfo.getCoerceToType() == info_it->type &&
          ArgInfo.getDirectOffset() == 0) {
        cir_cconv_assert(NumIRArgs == 1);
        mlir::Value V;
        if (!mlir::isa<RecordType>(I->getType())) {
          V = *I;
        } else {
          cir_cconv_unreachable("NYI");
        }

        // Handle swifterror arguments at the call site. LLVM requires the
        // swifterror argument to be statically an alloca (with swifterror)
        // or a function parameter (with swifterror), not a loaded value.
        // Create a swifterror-annotated alloca, copy the current error
        // pointer into it, pass the alloca, and copy back after the call.
        if (Callee && ArgNo < Callee.getNumArguments() &&
            Callee.getArgAttr(ArgNo, "cir.swifterror")) {
          mlir::Value origErrorLoc = V;
          auto ptrTy = mlir::cast<PointerType>(V.getType());
          auto innerTy = ptrTy.getPointee();
          auto align = LM.getDataLayout().getABITypeAlign(innerTy);
          auto alignAttr = rewriter.getI64IntegerAttr(align.value());

          // Create the swifterror alloca at the function entry.
          AllocaOp swiftErrorAlloca;
          {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            auto enclosingFn = Caller->getParentOfType<FuncOp>();
            rewriter.setInsertionPointToStart(
                &enclosingFn.getBody().front());
            swiftErrorAlloca = AllocaOp::create(
                rewriter, loc, ptrTy, innerTy,
                /*name=*/"swifterror.call", alignAttr);
            swiftErrorAlloca->setAttr("cir.swifterror",
                                      rewriter.getUnitAttr());
          }

          // Copy current error pointer into the swifterror alloca.
          auto currentError = LoadOp::create(rewriter, loc, V);
          StoreOp::create(rewriter, loc, currentError, swiftErrorAlloca,
                          /*is_volatile=*/false, /*is_nontemporal=*/false,
                          /*alignment=*/alignAttr,
                          /*sync_scope=*/cir::SyncScopeKindAttr{},
                          /*mem_order=*/cir::MemOrderAttr{},
                          /*tbaa=*/cir::TBAAAttr{});

          V = swiftErrorAlloca;
          swifterrorCallWritebacks.push_back(
              {swiftErrorAlloca, origErrorLoc});
        }

        if (ArgInfo.getCoerceToType() != V.getType() &&
            mlir::isa<IntType>(V.getType()))
          cir_cconv_unreachable("NYI");

        if (FirstIRArg < IRFuncTy.getNumInputs() &&
            V.getType() != IRFuncTy.getInput(FirstIRArg))
          cir_cconv_unreachable("NYI");

        if (ArgHasMaybeUndefAttr)
          V = cir::FreezeOp::create(rewriter, loc, V);
        IRCallArgs[FirstIRArg] = V;
        break;
      }

      // FIXME: Avoid the conversion through memory if possible.
      mlir::Value Src = *I;

      // If the value is offset in memory, apply the offset now. In OGCG,
      // Src is always an Address (pointer), but in CIR it's a value. When
      // a non-zero offset is needed, we must first store to memory, apply
      // the byte offset, then load the coerced type from the offset address.
      if (ArgInfo.getDirectOffset()) {
        auto tmpAlloca = createTmpAlloca(*this, loc, Src.getType());
        rewriter.setInsertionPoint(Caller); // Store at call site
        cir::StoreOp::create(rewriter, loc, Src, tmpAlloca,
                             /*is_volatile=*/false,
                             /*is_nontemporal=*/false,
                             /*alignment=*/tmpAlloca.getAlignmentAttr(),
                             /*sync_scope=*/cir::SyncScopeKindAttr{},
                             /*mem_order=*/cir::MemOrderAttr{},
                             /*tbaa=*/cir::TBAAAttr{});
        mlir::Value offsetPtr = buildAddressAtOffset(*this, tmpAlloca, ArgInfo);
        cir_cconv_assert(NumIRArgs == 1);
        auto loadedVal =
            cir::LoadOp::create(rewriter, loc, ArgInfo.getCoerceToType(),
                                offsetPtr);
        IRCallArgs[FirstIRArg] = loadedVal;
        break;
      }

      // Fast-isel and the optimizer generally like scalar values better than
      // FCAs, so we flatten them if this is safe to do for this argument.
      // As an example, if we have SrcTy = struct { i32, i32, i32 }, then the
      // coerced type can be STy = struct { u64, i32 }. Hence a function with
      // a single argument SrcTy will be rewritten to take two arguments,
      // namely u64 and i32.
      RecordType STy = mlir::dyn_cast<RecordType>(ArgInfo.getCoerceToType());
      if (STy && ArgInfo.isDirect() && ArgInfo.getCanBeFlattened()) {
        mlir::Type SrcTy = Src.getType();
        bool srcIsAggregate = mlir::isa<RecordType, ArrayType>(SrcTy);
        mlir::Value SrcAddr = srcIsAggregate ? findAddr(Src) : mlir::Value();
        llvm::TypeSize SrcTypeSize = LM.getDataLayout().getTypeAllocSize(SrcTy);
        llvm::TypeSize DstTypeSize = LM.getDataLayout().getTypeAllocSize(STy);

        if (SrcTypeSize.isScalable()) {
          cir_cconv_unreachable("NYI");
        } else {
          size_t SrcSize = SrcTypeSize.getFixedValue();
          size_t DstSize = DstTypeSize.getFixedValue();

          // Create a new temporary space and copy src in the front bits of it.
          // Other bits will be left untouched.
          // Note in OG, Src is of type Address, while here it is mlir::Value.
          // Here we need to first create another alloca to convert it into a
          // PointerType, so that we can call memcpy.
          if (SrcSize < DstSize) {
            unsigned srcAlign = 0;
            if (auto alignAttr = getAddrAlignmentAttr(SrcAddr))
              srcAlign = alignAttr.getInt();

            auto Alloca = createTmpAlloca(*this, loc, STy, srcAlign);
            if (!SrcAddr) {
              auto SrcAlloca = createTmpAlloca(*this, loc, SrcTy);
              rewriter.setInsertionPoint(Caller); // Store at call site
              cir::StoreOp::create(rewriter, loc, Src, SrcAlloca,
                                   /*is_volatile=*/false,
                                   /*is_nontemporal=*/false,
                                   /*alignment=*/SrcAlloca.getAlignmentAttr(),
                                   /*sync_scope=*/cir::SyncScopeKindAttr{},
                                   /*mem_order=*/cir::MemOrderAttr{},
                                   /*tbaa=*/cir::TBAAAttr{});
              SrcAddr = SrcAlloca;
            }
            createMemCpy(*this, Alloca, SrcAddr, SrcSize, getAllocaAlign(Alloca),
                         getAllocaAlign(SrcAddr));
            Src = Alloca;
          } else {
            if (SrcAddr) {
              auto srcPtrTy = mlir::cast<PointerType>(SrcAddr.getType());
              auto coercedPtrTy = PointerType::get(STy, srcPtrTy.getAddrSpace());
              if (srcPtrTy == coercedPtrTy)
                Src = SrcAddr;
              else
                Src = cir::CastOp::create(rewriter, loc, coercedPtrTy,
                                          cir::CastKind::bitcast, SrcAddr);
            } else {
              // SrcSize >= DstSize: Store src to memory and cast the pointer
              // to the coerced type. This is the CIR equivalent of OG's
              // Src.withElementType(STy).
              // Create the alloca at the call site (not function entry)
              // to avoid domination issues when the call is inside a cir.scope.
              // The alloca only lives for the coercion and doesn't need to be
              // at function entry.
              rewriter.setInsertionPoint(Caller);
              auto align = LM.getDataLayout().getABITypeAlign(SrcTy);
              auto alignAttr = rewriter.getI64IntegerAttr(align.value());
              auto SrcAlloca = AllocaOp::create(
                  rewriter, loc, PointerType::get(SrcTy), SrcTy,
                  /*name=*/llvm::StringRef("tmp"), alignAttr);
              cir::StoreOp::create(rewriter, loc, Src, SrcAlloca,
                                   /*is_volatile=*/false,
                                   /*is_nontemporal=*/false,
                                   /*alignment=*/SrcAlloca.getAlignmentAttr(),
                                   /*sync_scope=*/cir::SyncScopeKindAttr{},
                                   /*mem_order=*/cir::MemOrderAttr{},
                                   /*tbaa=*/cir::TBAAAttr{});
              Src = cir::CastOp::create(rewriter, loc, PointerType::get(STy),
                                        cir::CastKind::bitcast, SrcAlloca);
            }
          }

          uint64_t baseAlign = 0;
          if (auto alignAttr = getAddrAlignmentAttr(Src))
            baseAlign = alignAttr.getInt();
          if (!baseAlign)
            if (auto ptrTy = mlir::dyn_cast<PointerType>(Src.getType()))
              baseAlign =
                  LM.getDataLayout().getABITypeAlign(ptrTy.getPointee()).value();

          assert(NumIRArgs == STy.getNumElements());
          for (unsigned I = 0; I != STy.getNumElements(); ++I) {
            mlir::IntegerAttr fieldAlignAttr;
            if (baseAlign) {
              uint64_t fieldOffset =
                  STy.isUnion()
                      ? 0
                      : STy.getElementOffset(LM.getDataLayout().layout, I);
              uint64_t fieldAlign = baseAlign;
              if (fieldOffset != 0)
                fieldAlign = std::min<uint64_t>(
                    baseAlign,
                    static_cast<uint64_t>(
                        llvm::MinAlign(baseAlign, fieldOffset)));
              fieldAlignAttr = rewriter.getI64IntegerAttr(fieldAlign);
            }
            mlir::Value Member = cir::GetMemberOp::create(
                rewriter, loc, PointerType::get(STy.getMembers()[I]), Src,
                /*name=*/"",
                /*index=*/I);
            mlir::Value Load = cir::LoadOp::create(
                rewriter, loc, Member,
                /*isDeref=*/false, /*is_volatile=*/false,
                /*is_nontemporal=*/false, /*alignment=*/fieldAlignAttr,
                /*sync_scope=*/cir::SyncScopeKindAttr{},
                /*mem_order=*/cir::MemOrderAttr{},
                /*tbaa=*/cir::TBAAAttr{});
            if (ArgHasMaybeUndefAttr)
              Load = cir::FreezeOp::create(rewriter, loc, Load);
            IRCallArgs[FirstIRArg + I] = Load;
          }
        }
      } else {
        // In the simple case, just pass the coerced loaded value.
        cir_cconv_assert(NumIRArgs == 1);
        mlir::Value Load =
            createCoercedValue(Src, ArgInfo.getCoerceToType(), *this);

        // OGCG handles CMSE non-secure call coercion here. CMSE is ARM
        // Cortex-M only (not AArch64), not a CIR target.
        if (ArgHasMaybeUndefAttr)
          Load = cir::FreezeOp::create(rewriter, loc, Load);
        IRCallArgs[FirstIRArg] = Load;
      }

      break;
    }
    case ABIArgInfo::Indirect:
    case ABIArgInfo::IndirectAliased: {
      assert(NumIRArgs == 1);

      // Find the address backing this argument value. We need a pointer
      // to the struct (one level of indirection from the value). Use
      // findAddr which returns the immediate pointer-typed operand:
      //   - Direct variable `ps`:  returns alloca ptr<PackedS>
      //   - Array element `arr[0]`: returns GetElementOp ptr<PackedS>
      //   - Pointer deref `*pp`:   returns loaded ptr<PackedS>
      // We avoid findAlloca here because it traces through multiple
      // LoadOps and may return the wrong-level alloca (e.g., for `*pp`
      // it finds the alloca for `pp` with type ptr<ptr<PackedS>>).
      // Match OGCG's EmitCall: only aggregate indirect arguments may reuse
      // their existing storage. Non-aggregate indirect args, such as large
      // _BitInt values, must be materialized through a fresh ABI temp even
      // when the source expression is backed by an addressable lvalue.
      bool argIsAggregate = mlir::isa<RecordType, ArrayType>(info_it->type);
      bool canReuseSourceAddr = argIsAggregate;
      mlir::Value addr = canReuseSourceAddr ? findAddr(*I) : mlir::Value();
      cir::AllocaOp materializedTemp;
      auto materializeValueToTemp = [&](cir::AllocaOp tempAlloca) {
        cir::TBAAAttr tbaa;
        if (auto srcLoad = findLoadLike(*I))
          tbaa = srcLoad.getTbaa().value_or(cir::TBAAAttr{});

        IntType storageTy;
        if (info_it->isBitIntType()) {
          auto tempPointeeTy =
              mlir::cast<PointerType>(tempAlloca.getType()).getPointee();
          storageTy = mlir::dyn_cast<IntType>(tempPointeeTy);
          if (!storageTy || storageTy == info_it->type) {
            storageTy = getBitIntStorageType(*this, info_it->type,
                                             info_it->isBitIntType());
          }
        }

        if (storageTy) {
          createBitIntStorageStore(
              *this, loc, *I, tempAlloca, info_it->type, storageTy,
              tempAlloca.getAlignmentAttr(), tbaa);
          return;
        }

        StoreOp::create(rewriter, loc, *I, tempAlloca,
                        /*is_volatile=*/false,
                        /*is_nontemporal=*/false,
                        /*alignment=*/tempAlloca.getAlignmentAttr(),
                        /*sync_scope=*/cir::SyncScopeKindAttr{},
                        /*mem_order=*/cir::MemOrderAttr{},
                        /*tbaa=*/tbaa);
      };
      if (!addr && argIsAggregate) {
        // Aggregate indirect rvalues still need an address backing the copy
        // decision. Non-aggregate indirect args are handled below by
        // materializing directly into the final byval temp.
        unsigned tmpAlign = ArgInfo.getIndirectAlign();
        materializedTemp =
            createTmpAlloca(*this, loc, info_it->type, tmpAlign);
        rewriter.setInsertionPoint(Caller); // Store at call site
        if (emitCallArgLifetimeMarkers) {
          LifetimeStartOp::create(rewriter, loc, materializedTemp.getResult());
          callArgLifetimeEndAfterCall.push_back(materializedTemp.getResult());
        }
        materializeValueToTemp(materializedTemp);
        addr = materializedTemp;
      }

      // Determine whether the callee needs its own copy of the argument.
      //
      // Match OGCG's EmitCall logic: non-aggregate indirect args always
      // materialize into an ABI temp, while aggregate args can sometimes
      // reuse their existing storage when alignment and aliasing rules allow
      // it. In particular, caller-owned aggregate rvalues for non-byval
      // indirect params (C++ invisible references) already have a dedicated
      // temporary from CIRGen and must be passed directly instead of being
      // copied again.
      bool needsCopy = !addr;

      unsigned requiredAlign = ArgInfo.getIndirectAlign();
      unsigned srcAlign = 0;
      if (addr) {
        srcAlign = getOrEnforceAllocaAlign(addr, requiredAlign, *this);
        if (!srcAlign) {
          if (auto loadOp =
                  mlir::dyn_cast_if_present<LoadOp>(I->getDefiningOp()))
            srcAlign = loadOp.getAlignment().value_or(0);
        }
      }
      if (!srcAlign)
        srcAlign = LM.getDataLayout().getABITypeAlign(I->getType()).value();
      if (srcAlign < requiredAlign)
        needsCopy = true;

      // Match OGCG: a non-byval indirect lvalue cannot alias caller storage,
      // and even byval/ref lvalues need a copy if their backing address is
      // less aligned than the source type's natural ABI alignment.
      bool argWasLValue = ArgNo < ArgLValueFlags.size() && ArgLValueFlags[ArgNo];
      bool lifetimeEndsInCallee =
          indirectArgLifetimeEndsInCallee(LM, info_it->type, ArgInfo);
      if (!needsCopy && argWasLValue) {
        bool isByValOrRef =
            ArgInfo.isIndirectAliased() || ArgInfo.getIndirectByVal();
        unsigned naturalAlign =
            LM.getDataLayout().getABITypeAlign(I->getType()).value();
        if (!isByValOrRef || srcAlign < naturalAlign)
          needsCopy = true;
      }

      // OGCG always creates a byval-temp for indirect scalar/complex/vector
      // rvalues instead of reusing the traced source address. Reusing the
      // original storage drops required ABI temps and lifetime markers for
      // cases like large _BitInt arguments passed by value.
      if (!needsCopy && !argWasLValue && !argIsAggregate)
        needsCopy = true;

      // If the source address space doesn't match the target indirect
      // address space, we must copy to a local alloca (which will be
      // in the correct alloca address space). This matches OGCG's
      // check at CGCall.cpp:5476-5479.
      if (!needsCopy && addr && ArgInfo.isIndirectAliased()) {
        auto ptrTy = mlir::dyn_cast<PointerType>(addr.getType());
        if (ptrTy) {
          unsigned srcAS = 0;
          if (auto targetAS =
                  mlir::dyn_cast_if_present<cir::TargetAddressSpaceAttr>(
                      ptrTy.getAddrSpace()))
            srcAS = targetAS.getValue();
          else if (mlir::isa_and_present<cir::LangAddressSpaceAttr>(
                       ptrTy.getAddrSpace()))
            srcAS = LM.getTargetLoweringInfo()
                        .getTargetAddrSpaceFromCIRAddrSpace(
                            mlir::cast<cir::LangAddressSpaceAttr>(
                                ptrTy.getAddrSpace())
                                .getValue());
          else if (!ptrTy.getAddrSpace() &&
                   addr.getDefiningOp<cir::AllocaOp>()) {
            // A CIR alloca with no explicit address space will be
            // placed in the target's alloca address space when
            // lowered to LLVM. Use that AS for the comparison so
            // we don't force an unnecessary copy.
            if (auto allocaAS = mlir::cast_if_present<mlir::IntegerAttr>(
                    LM.getDataLayout().layout.getAllocaMemorySpace()))
              srcAS = static_cast<unsigned>(
                  allocaAS.getValue().getZExtValue());
          }
          if (srcAS != ArgInfo.getIndirectAddrSpace())
            needsCopy = true;
        }
      }

      if (needsCopy) {
        // Use the ABI-specified indirect alignment for the temp alloca and
        // memcpy destination. This must not be raised to the LLVM scalar ABI
        // alignment for large _BitInt storage types (e.g. i256 on AArch64),
        // because OGCG still uses the target's _BitInt indirect alignment.
        auto storageTy = getBitIntStorageType(*this, info_it->type,
                                              info_it->isBitIntType());
        mlir::Type tempTy = info_it->type;
        if (storageTy)
          tempTy = storageTy;
        unsigned byvalAlign = ArgInfo.getIndirectAlign();
        if (!byvalAlign)
          byvalAlign = LM.getDataLayout().getABITypeAlign(tempTy).value();
        auto tmpAlloca = createTmpAlloca(*this, loc, tempTy, byvalAlign);
        rewriter.setInsertionPoint(Caller); // Temps live at call site
        if (emitCallArgLifetimeMarkers) {
          LifetimeStartOp::create(rewriter, loc, tmpAlloca.getResult());
          callArgLifetimeEndAfterCall.push_back(tmpAlloca.getResult());
        }

        if (storageTy) {
          materializeValueToTemp(tmpAlloca);
        } else if (!mlir::isa<RecordType, ArrayType>(info_it->type)) {
          materializeValueToTemp(tmpAlloca);
        } else {
          auto tySize = LM.getDataLayout().getTypeAllocSize(info_it->type);

          // Source alignment: use the alignment of the source address.
          // For alloca-backed addresses, use the alloca's alignment.
          // For values traced through a LoadOp, use the load's alignment
          // (e.g., align 1 for packed struct members at non-aligned offsets).
          // Otherwise fall back to the type's ABI alignment.
          createMemCpy(*this, tmpAlloca, addr, tySize.getFixedValue(),
                       byvalAlign, srcAlign, std::nullopt, info_it->type);
        }
        mlir::Value val = tmpAlloca;
        if (ArgHasMaybeUndefAttr)
          val = cir::FreezeOp::create(rewriter, loc, val);
        IRCallArgs[FirstIRArg] = val;
      } else {
        if (emitCallArgLifetimeMarkers && argIsAggregate &&
            lifetimeEndsInCallee)
          if (auto alloca = findAlloca(addr.getDefiningOp());
              alloca && alloca.getName() == "coerce" &&
              !hasExistingLifetimeMarker(alloca.getResult())) {
            mlir::OpBuilder::InsertionGuard guard(rewriter);
            rewriter.setInsertionPointAfter(alloca);
            LifetimeStartOp::create(rewriter, loc, alloca.getResult());
            callArgLifetimeEndAfterCall.push_back(alloca.getResult());
          }

        mlir::Value val = addr;
        if (ArgHasMaybeUndefAttr)
          val = cir::FreezeOp::create(rewriter, loc, val);
        IRCallArgs[FirstIRArg] = val;
      }

      // For Indirect and IndirectAliased, the function parameter may have
      // a specific address space (e.g. addrspace(5) for AMDGPU alloca).
      // The call argument pointer may be in a different address space
      // (e.g. a CIR alloca with no explicit AS, or a global pointer).
      // Cast to match the function parameter type. This mirrors OGCG's
      // performAddrSpaceCast at CGCall.cpp:5492-5493.
      // Skip variadic arguments that are past the function type's fixed
      // params (they don't have a declared parameter type to match).
      if (FirstIRArg < IRFuncTy.getNumInputs()) {
        mlir::Value val = IRCallArgs[FirstIRArg];
        auto valPtrTy = mlir::cast<PointerType>(val.getType());
        auto expectedPtrTy =
            mlir::cast<PointerType>(IRFuncTy.getInput(FirstIRArg));
        if (valPtrTy.getPointee() != expectedPtrTy.getPointee()) {
          auto bitcastTy = PointerType::get(expectedPtrTy.getPointee(),
                                            valPtrTy.getAddrSpace());
          val = CastOp::create(rewriter, loc, bitcastTy, CastKind::bitcast,
                               val);
          valPtrTy = mlir::cast<PointerType>(val.getType());
        }
        if (valPtrTy.getAddrSpace() != expectedPtrTy.getAddrSpace()) {
          auto newPtrTy = PointerType::get(valPtrTy.getPointee(),
                                           expectedPtrTy.getAddrSpace());
          auto cast = CastOp::create(rewriter, loc, newPtrTy,
                                     CastKind::address_space, val);
          IRCallArgs[FirstIRArg] = cast;
        } else {
          IRCallArgs[FirstIRArg] = val;
        }
      }

      // NOTE(cir): Skipping Emissions, lifetime markers.

      break;
    }
    case ABIArgInfo::Ignore:
      // Ignored arguments have no corresponding IR argument.
      assert(NumIRArgs == 0);
      break;
    case ABIArgInfo::CoerceAndExpand: {
      auto coercionType = ArgInfo.getCoerceAndExpandType();
      auto unpaddedCoercionType = ArgInfo.getUnpaddedCoerceAndExpandType();
      auto unpaddedRecord =
          mlir::dyn_cast<RecordType>(unpaddedCoercionType);

      // Get address of the source aggregate.
      mlir::Value addr = findAddr(*I);
      if (!addr) {
        // Scalar: materialize to a temporary.
        auto tmpAlloca = createTmpAlloca(*this, loc, I->getType());
        StoreOp::create(rewriter, loc, *I, tmpAlloca.getResult(),
                        /*is_volatile=*/false, /*is_nontemporal=*/false,
                        /*alignment=*/tmpAlloca.getAlignmentAttr(),
                        /*sync_scope=*/cir::SyncScopeKindAttr{},
                        /*mem_order=*/cir::MemOrderAttr{},
                        /*tbaa=*/cir::TBAAAttr{});
        addr = tmpAlloca.getResult();
      }

      // Bitcast to the padded coercion struct type and load the whole record.
      auto coercionPtrTy = PointerType::get(coercionType);
      auto coercionAddr = CastOp::create(rewriter, loc, coercionPtrTy,
                                         CastKind::bitcast, addr);
      bool useElementWiseLoads =
          !containsScalableCoercionElement(coercionType);
      unsigned baseAlign = 0;
      if (auto srcLoad = findLoadLike(*I))
        baseAlign = srcLoad.getAlignment().value_or(0);
      if (!baseAlign)
        if (auto alignAttr = getAddrAlignmentAttr(coercionAddr.getResult()))
          baseAlign = alignAttr.getInt();
      if (!baseAlign)
        if (auto ptrTy = mlir::dyn_cast<PointerType>(addr.getType()))
          baseAlign =
              LM.getDataLayout().getABITypeAlign(ptrTy.getPointee()).value();
      mlir::Value coercionValue;
      if (!useElementWiseLoads) {
        auto coercionAlignAttr = getAddrAlignmentAttr(coercionAddr.getResult());
        coercionValue =
            LoadOp::create(rewriter, loc, coercionAddr.getResult(),
                           /*isDeref=*/false, /*is_volatile=*/false,
                           /*is_nontemporal=*/false,
                           /*alignment=*/coercionAlignAttr,
                           /*sync_scope=*/cir::SyncScopeKindAttr{},
                           /*mem_order=*/cir::MemOrderAttr{},
                           /*tbaa=*/cir::TBAAAttr{});
      }

      unsigned IRArgPos = FirstIRArg;
      unsigned unpaddedIndex = 0;
      for (unsigned i = 0, e = coercionType.getNumElements(); i != e; ++i) {
        mlir::Type eltType = coercionType.getMembers()[i];
        if (ABIArgInfo::isPaddingForCoerceAndExpand(eltType))
          continue;

        auto paramType = unpaddedRecord
                             ? unpaddedRecord.getMembers()[unpaddedIndex++]
                             : unpaddedCoercionType;

        mlir::Value elt;
        if (useElementWiseLoads) {
          mlir::IntegerAttr fieldAlignAttr;
          if (baseAlign) {
            uint64_t fieldOffset =
                coercionType.getElementOffset(LM.getDataLayout().layout, i);
            uint64_t fieldAlign = baseAlign;
            if (fieldOffset != 0)
              fieldAlign = std::min<uint64_t>(
                  baseAlign,
                  (uint64_t)llvm::MinAlign(baseAlign, fieldOffset));
            fieldAlignAttr = rewriter.getI64IntegerAttr(fieldAlign);
          }
          auto eltPtr = GetMemberOp::create(rewriter, loc,
                                            PointerType::get(eltType),
                                            coercionAddr.getResult(),
                                            /*name=*/"", /*index=*/i);
          elt = LoadOp::create(rewriter, loc, eltPtr.getResult(),
                               /*isDeref=*/false, /*is_volatile=*/false,
                               /*is_nontemporal=*/false,
                               /*alignment=*/fieldAlignAttr,
                               /*sync_scope=*/cir::SyncScopeKindAttr{},
                               /*mem_order=*/cir::MemOrderAttr{},
                               /*tbaa=*/cir::TBAAAttr{});
        } else {
          elt = ExtractMemberOp::create(rewriter, loc, coercionValue, i);
        }

        // Coerce element type if it differs from the unpadded type.
        if (elt.getType() != paramType) {
          if (auto coerced = tryCoerceVectorValue(*this, elt, paramType))
            elt = *coerced;
        }

        if (elt.getType() != paramType) {
          auto tmpAlloca2 = createTmpAlloca(*this, loc, elt.getType());
          StoreOp::create(rewriter, loc, elt, tmpAlloca2.getResult(),
                          /*is_volatile=*/false, /*is_nontemporal=*/false,
                          /*alignment=*/tmpAlloca2.getAlignmentAttr(),
                          /*sync_scope=*/cir::SyncScopeKindAttr{},
                          /*mem_order=*/cir::MemOrderAttr{},
                          /*tbaa=*/cir::TBAAAttr{});
          auto castPtr = CastOp::create(rewriter, loc,
                                         PointerType::get(paramType),
                                         CastKind::bitcast,
                                         tmpAlloca2.getResult());
          elt = LoadOp::create(rewriter, loc, castPtr.getResult());
        }

        if (ArgHasMaybeUndefAttr)
          elt = cir::FreezeOp::create(rewriter, loc, elt);
        IRCallArgs[IRArgPos++] = elt;
      }
      cir_cconv_assert(IRArgPos == FirstIRArg + NumIRArgs);
      break;
    }
    case ABIArgInfo::InAlloca:
      cir_cconv_unreachable("NYI: InAlloca in call arg rewriting");
    }
  }

  // 2. Prepare the function pointer.
  // NOTE(cir): This is not needed for CIR.

  // 3. Perform the actual call.

  // NOTE(cir): CIRGen handle when to "deactive" cleanups. We also skip some
  // debugging stuff here.

  // Compute the calling convention and attributes.

  // NOTE(cir): CIR uses a callee-centric attribute model. ABI attributes
  // (sext, zext, sret, byval, noundef, etc.) are set on the callee FuncOp by
  // constructAttributeList, then propagated to call sites during DirectToLLVM
  // lowering via propagateArgAttrsToCallSite. This covers direct calls.
  // Indirect calls get their attrs from the cir.call_arg_attrs mechanism
  // set by rewriteCallOp below.

  // StrictFP call-site attribute propagation is handled in CIRGenCall.cpp
  // (added to ExtraFuncAttributesAttr) and forwarded through LowerToLLVM.

  // NOTE(cir): Deciding whether to use Call or Invoke is done in CIRGen.

  // Rewrite the actual call operation.
  // NOTE(cir): We don't know if the callee was already lowered, so we only
  // fetch the name from the callee, while the return type is fetch from the
  // lowering types manager.

  CallOp newCallOp;
  TryCallOp newTryCallOp;

  // Preserve call properties from the original call.
  cir::CallingConv callingConv = Caller.getCallingConv();
  cir::SideEffect sideEffect = Caller.getSideEffect();
  mlir::UnitAttr exception = Caller.getExceptionAttr();

  // When rewriting a TryCallOp (indicated by tryCallNormalDest being set),
  // create a TryCallOp instead of a CallOp.
  bool isTryCall = tryCallNormalDest != nullptr;

  if (isTryCall) {
    auto retTy = IRFuncTy.getReturnType();
    if (Caller.isIndirect()) {
      rewriter.setInsertionPoint(Caller);
      auto val = Caller.getIndirectCall();
      auto ptrTy = PointerType::get(IRFuncTy);
      mlir::Value callee = (val.getType() == ptrTy)
                               ? val
                               : CastOp::create(rewriter, val.getLoc(), ptrTy,
                                                CastKind::bitcast, val);
      llvm::SmallVector<mlir::Value, 16> indirectArgs{callee};
      indirectArgs.append(IRCallArgs.begin(), IRCallArgs.end());
      // A try_call is a terminator, so it must follow all of its operands
      // (including a reused sret destination pointer); advance past them.
      advanceInsertionPastArgs(rewriter, indirectArgs);
      newTryCallOp = TryCallOp::create(
          rewriter, loc, mlir::SymbolRefAttr(), retTy, tryCallNormalDest,
          tryCallUnwindDest, indirectArgs, sideEffect, callingConv);
    } else {
      // A try_call is a terminator, so it must follow all of its operands. The
      // argument-coercion loop can leave the insertion point before some of the
      // argument-defining ops; advance past the latest one so every operand
      // dominates the try_call.
      advanceInsertionPastArgs(rewriter, IRCallArgs);
      newTryCallOp = TryCallOp::create(
          rewriter, loc, Caller.getCalleeAttr(), retTy, tryCallNormalDest,
          tryCallUnwindDest, IRCallArgs, sideEffect, callingConv);
    }
    // TryCallOp is a terminator — all post-call operations (lifetime ends,
    // swifterror writebacks, return value coercion) must go in the normal
    // destination block rather than after the terminator in the same block.
    rewriter.setInsertionPointToStart(tryCallNormalDest);
    // Use a dummy newCallOp reference for subsequent attribute-setting code.
    // We'll forward all attributes to newTryCallOp instead.
  } else if (Caller.isIndirect()) {
    rewriter.setInsertionPoint(Caller);
    auto val = Caller.getIndirectCall();
    auto ptrTy = PointerType::get(IRFuncTy);
    // Only create bitcast if types are different
    mlir::Value callee = (val.getType() == ptrTy)
                             ? val
                             : CastOp::create(rewriter, val.getLoc(), ptrTy,
                                              CastKind::bitcast, val);
    // For indirect calls, prepend the callee value to operands
    llvm::SmallVector<mlir::Value, 16> indirectArgs{callee};
    indirectArgs.append(IRCallArgs.begin(), IRCallArgs.end());
    // As in the direct case below, ensure the call follows all of its
    // operand-defining ops so a reused sret destination pointer dominates it.
    advanceInsertionPastArgs(rewriter, indirectArgs);
    newCallOp = CallOp::create(rewriter, loc, mlir::SymbolRefAttr(),
                               IRFuncTy.getReturnType(), indirectArgs,
                               callingConv, sideEffect, exception);
  } else {
    // Ensure the call follows all of its operand-defining ops in the current
    // block. The sret return slot may reuse a destination pointer whose
    // defining op sits after the original call (see directDst above); without
    // this, the sret operand would not dominate the call — a use-before-def
    // that crashes during CIR->LLVM lowering on sret-return targets.
    advanceInsertionPastArgs(rewriter, IRCallArgs);
    newCallOp = CallOp::create(rewriter, loc, Caller.getCalleeAttr(),
                               IRFuncTy.getReturnType(), IRCallArgs,
                               callingConv, sideEffect, exception);
  }

  // Get the new operation for attribute setting. For TryCallOp, use the
  // TryCallOp; for CallOp, use the CallOp.
  mlir::Operation *newOp =
      isTryCall ? newTryCallOp.getOperation() : newCallOp.getOperation();

  for (mlir::Value tempAddr : callArgLifetimeEndAfterCall)
    LifetimeEndOp::create(rewriter, loc, tempAddr);

  // After the call, copy updated swifterror values back to their original
  // error pointer locations. The callee may have modified the swifterror
  // alloca, and the caller's code needs to see the updated error pointer.
  for (auto &[swifterrorAlloca, origErrorLoc] : swifterrorCallWritebacks) {
    auto updated = LoadOp::create(rewriter, loc, swifterrorAlloca);
    StoreOp::create(rewriter, loc, updated, origErrorLoc,
                    /*is_volatile=*/false, /*is_nontemporal=*/false,
                    /*alignment=*/mlir::IntegerAttr{},
                    /*sync_scope=*/cir::SyncScopeKindAttr{},
                    /*mem_order=*/cir::MemOrderAttr{},
                    /*tbaa=*/cir::TBAAAttr{});
  }

  auto addIndirectByValCallArgAttrs =
      [&](mlir::NamedAttrList &attrs, mlir::Type argTy,
          const ABIArgInfo &AI) {
        // Preserve the original CIR pointee type so direct-to-LLVM lowering can
        // recover the concrete byval type when its storage layout matches the
        // ABI object. Keep the byte size as a fallback for cases like _BitInt
        // where the lowered storage must use a byte array to match OGCG's
        // alloc-size rules.
        attrs.set("cir.byval", mlir::TypeAttr::get(argTy));
        auto allocSize =
            LM.getDataLayout().getTypeAllocSize(argTy).getFixedValue();
        attrs.set("cir.byval_size", rewriter.getI64IntegerAttr(allocSize));
        attrs.set("cir.align",
                  rewriter.getI64IntegerAttr(AI.getIndirectAlign()));
      };
  auto addIndirectPassByValueCallArgAttrs =
      [&](mlir::NamedAttrList &attrs, mlir::Type argTy,
          const ABIArgInfo &AI) {
        if (AI.isIndirect() && AI.getIndirectByVal()) {
          addIndirectByValCallArgAttrs(attrs, argTy, AI);
        } else if (AI.isIndirect() || AI.isIndirectAliased()) {
          // Non-byval indirect params: add dead_on_return when the object's
          // lifetime ends in the callee. Match LowerCall.cpp and OGCG's
          // CGCall.cpp logic for indirect pass-by-value arguments.
          if (indirectArgLifetimeEndsInCallee(LM, argTy, AI))
            attrs.set("cir.dead_on_return", rewriter.getUnitAttr());
        }

        if (LM.getContext().getCodeGenOpts().PassByValueIsNoAlias) {
          mlir::Type paramTy = argTy;
          if (auto ptrTy = mlir::dyn_cast<cir::PointerType>(paramTy))
            paramTy = ptrTy.getPointee();
          if (auto recordTy = mlir::dyn_cast<cir::RecordType>(paramTy)) {
            if (auto ast = recordTy.getAst()) {
              const clang::RecordDecl *rd = ast.getRawDecl();
              if (rd && rd->getArgPassingRestrictions() ==
                            clang::RecordArgPassingKind::CanPassInRegs)
                attrs.set("cir.noalias", rewriter.getUnitAttr());
            }
          }
        }
      };

  // Set call-site attrs for variadic indirect pass-by-value args. These are
  // needed because variadic args have no corresponding FuncOp arg for
  // propagateArgAttrsToCallSite to copy from during LowerToLLVM. Required
  // direct-call indirect args still come from the callee FuncOp's arg_attrs;
  // required indirect-call args are handled in the indirect-call block below.
  // We store them as "cir.call_arg_attrs" on the CIR CallOp; LowerToLLVM's
  // mergeCallSiteArgAttrs merges them into the LLVM call's arg_attrs.
  bool isSwiftCall = CallInfo.getCallingConvention() == llvm::CallingConv::Swift ||
                     CallInfo.getCallingConvention() ==
                         llvm::CallingConv::SwiftTail;
  if (CallInfo.isVariadic()) {
    unsigned totalArgs = IRCallArgs.size();
    llvm::SmallVector<mlir::Attribute> callArgAttrs(totalArgs);
    auto *ctx = rewriter.getContext();
    bool hasCallArgAttrs = false;
    unsigned numRequired = CallInfo.getNumRequiredArgs();
    unsigned nofpMask = 0;

    {
      bool noInfs = false, noNans = false;
      const auto &langOpts = LM.getContext().getLangOpts();
      noInfs = langOpts.NoHonorInfs;
      noNans = langOpts.NoHonorNaNs;
      if (!noInfs || !noNans) {
        auto parentFn = Caller->getParentOfType<cir::FuncOp>();
        if (parentFn) {
          if (auto extraAttrs = parentFn.getExtraAttrs()) {
            auto elems = extraAttrs->getElements();
            if (!noInfs) {
              if (auto a = mlir::dyn_cast_or_null<mlir::StringAttr>(
                      elems.get("no-infs-fp-math")))
                noInfs = a.getValue() == "true";
            }
            if (!noNans) {
              if (auto a = mlir::dyn_cast_or_null<mlir::StringAttr>(
                      elems.get("no-nans-fp-math")))
                noNans = a.getValue() == "true";
            }
          }
        }
      }
      if (noInfs)
        nofpMask |= 0x204; // fcInf
      if (noNans)
        nofpMask |= 0x3; // fcNan
    }

    unsigned argIdx = 0;
    auto infoIt = CallInfo.arg_begin();
    for (auto it = CallArgs.begin(), end = CallArgs.end(); it != end;
         ++it, ++infoIt, ++argIdx) {
      // Only add attrs for variadic args (beyond declared params).
      if (argIdx < numRequired)
        continue;

      const ABIArgInfo &AI = infoIt->info;
      unsigned firstIR, numIR;
      std::tie(firstIR, numIR) = IRFunctionArgs.getIRArgs(argIdx);

      if (AI.isIndirect()) {
        mlir::NamedAttrList attrs;
        addIndirectPassByValueCallArgAttrs(attrs, infoIt->type, AI);
        if (determineNoUndef(infoIt->type, AI, LM.getDataLayout()))
          attrs.set("cir.noundef", rewriter.getUnitAttr());
        if (!attrs.empty()) {
          callArgAttrs[firstIR] = mlir::DictionaryAttr::get(ctx, attrs);
          hasCallArgAttrs = true;
        }
      } else if (AI.isDirect() || AI.isExtend()) {
        // Variadic Direct/Extend args: mirror OGCG's per-IR-operand attrs for
        // noundef, signext/zeroext, and stack alignment. Aggregates coerced to
        // registers (e.g. struct {int,int} -> i64) must NOT get noundef
        // because their padding bits may be undefined.
        bool addNoUndef = determineNoUndef(infoIt->type, AI, LM.getDataLayout());
        for (unsigned i = 0; i < numIR; ++i) {
          unsigned idx = firstIR + i;
          if (idx >= totalArgs)
            break;
          mlir::NamedAttrList attrs;
          if (addNoUndef)
            attrs.set("cir.noundef", rewriter.getUnitAttr());
          if (unsigned align = AI.getDirectAlign()) {
            attrs.set("cir.alignstack",
                      rewriter.getIntegerAttr(rewriter.getI64Type(), align));
          }
          if (AI.isExtend()) {
            if (AI.isSignExt())
              attrs.set("cir.signext", rewriter.getUnitAttr());
            else
              attrs.set("cir.zeroext", rewriter.getUnitAttr());
          }
          if (nofpMask && canApplyNoFPClass(AI, infoIt->type))
            attrs.set("cir.nofpclass", rewriter.getI32IntegerAttr(nofpMask));
          if (!attrs.empty()) {
            callArgAttrs[idx] = mlir::DictionaryAttr::get(ctx, attrs);
            hasCallArgAttrs = true;
          }
        }
      }
    }

    if (hasCallArgAttrs) {
      for (auto &attr : callArgAttrs)
        if (!attr)
          attr = mlir::DictionaryAttr::get(ctx, {});
      newOp->setAttr("cir.call_arg_attrs",
                     mlir::ArrayAttr::get(ctx, callArgAttrs));
    }
  }

  if (CallInfo.isChainCall()) {
    auto *ctx = rewriter.getContext();
    auto emptyDict = mlir::DictionaryAttr::get(ctx, {});
    auto existingAttrs =
        newOp->getAttrOfType<mlir::ArrayAttr>("cir.call_arg_attrs");
    unsigned numArgs = IRCallArgs.size();
    if (existingAttrs)
      numArgs = std::max<unsigned>(numArgs, existingAttrs.size());

    llvm::SmallVector<mlir::Attribute> callArgAttrs(numArgs, emptyDict);
    if (existingAttrs)
      for (unsigned i = 0; i < existingAttrs.size() && i < numArgs; ++i)
        callArgAttrs[i] = existingAttrs[i];

    unsigned firstIRArg, numIRArgs;
    std::tie(firstIRArg, numIRArgs) = IRFunctionArgs.getIRArgs(0);
    cir_cconv_assert(numIRArgs && firstIRArg < numArgs &&
                     "chain arg should lower to at least one IR operand");

    mlir::NamedAttrList attrs;
    if (callArgAttrs[firstIRArg])
      for (auto a : mlir::cast<mlir::DictionaryAttr>(callArgAttrs[firstIRArg]))
        attrs.append(a);
    attrs.set("cir.nest", rewriter.getUnitAttr());
    attrs.set("cir.noundef", rewriter.getUnitAttr());
    callArgAttrs[firstIRArg] = mlir::DictionaryAttr::get(ctx, attrs);
    newOp->setAttr("cir.call_arg_attrs",
                   mlir::ArrayAttr::get(ctx, callArgAttrs));
  }

  // For indirect calls, set per-arg attributes (noundef, signext, etc.) on
  // the call op itself, since there's no callee FuncOp for
  // propagateArgAttrsToCallSite to copy from later.
  // This must merge with any existing attrs from the variadic block above.
  if (Caller.isIndirect()) {
    unsigned totalArgs = IRCallArgs.size();
    llvm::SmallVector<mlir::Attribute> callArgAttrs(totalArgs);
    auto *ctx = rewriter.getContext();
    bool hasCallArgAttrs = false;

    // Seed with any existing attrs (e.g. variadic arg attrs set above).
    if (auto existingAttrs =
            newOp->getAttrOfType<mlir::ArrayAttr>("cir.call_arg_attrs")) {
      for (unsigned i = 0; i < existingAttrs.size() && i < totalArgs; ++i)
        callArgAttrs[i] = existingAttrs[i];
      hasCallArgAttrs = true;
    }

    // Compute nofpclass mask from LangOpts + enclosing function's extra_attrs.
    // Matches LowerCall.cpp's nofpclass logic and OGCG CGCall.cpp:2862-2863.
    // For indirect calls, the no-infs/no-nans flags come from the enclosing
    // function definition (not the call op itself).
    bool noInfs = false, noNans = false;
    {
      const auto &langOpts = LM.getContext().getLangOpts();
      noInfs = langOpts.NoHonorInfs;
      noNans = langOpts.NoHonorNaNs;
      if (!noInfs || !noNans) {
        auto parentFn = Caller->getParentOfType<cir::FuncOp>();
        if (parentFn) {
          if (auto extraAttrs = parentFn.getExtraAttrs()) {
            auto elems = extraAttrs->getElements();
            if (!noInfs) {
              if (auto a = mlir::dyn_cast_or_null<mlir::StringAttr>(
                      elems.get("no-infs-fp-math")))
                noInfs = a.getValue() == "true";
            }
            if (!noNans) {
              if (auto a = mlir::dyn_cast_or_null<mlir::StringAttr>(
                      elems.get("no-nans-fp-math")))
                noNans = a.getValue() == "true";
            }
          }
        }
      }
    }
    unsigned nofpMask = 0;
    if (noInfs) nofpMask |= 0x204; // fcInf
    if (noNans) nofpMask |= 0x3;   // fcNan

    unsigned argIdx = 0;
    for (auto I = CallInfo.arg_begin(), E = CallInfo.arg_end();
         I != E; ++I, ++argIdx) {
      const ABIArgInfo &AI = I->info;
      unsigned FirstIRArg, NumIRArgs;
      std::tie(FirstIRArg, NumIRArgs) = IRFunctionArgs.getIRArgs(argIdx);

      for (unsigned i = 0; i < NumIRArgs; ++i) {
        unsigned idx = FirstIRArg + i;
        if (idx >= totalArgs)
          break;
        mlir::NamedAttrList attrs;
        // Preserve any existing attrs for this arg (e.g. from variadic
        // block above).
        if (callArgAttrs[idx])
          for (auto existing :
               mlir::cast<mlir::DictionaryAttr>(callArgAttrs[idx]))
            attrs.append(existing);
        if (AI.isIndirect()) {
          addIndirectPassByValueCallArgAttrs(attrs, I->type, AI);
          hasCallArgAttrs = true;
        }
        // For Direct/Extend args, add noundef when appropriate.
        // Use determineNoUndef to check the original type — unions and
        // other aggregates should NOT get noundef even after coercion.
        // Skip when -no-enable-noundef-analysis is set.
        bool enableNoundef =
            !LM.getModule()->hasAttr("cir.no_noundef_analysis");
        bool addNoUndef = determineNoUndef(I->type, AI, LM.getDataLayout());
        if (!addNoUndef && isSwiftCall)
          addNoUndef = determineSwiftCoerceAndExpandNoUndef(
              I->type, AI, LM.getDataLayout());
        if (enableNoundef && addNoUndef) {
          attrs.set("cir.noundef", rewriter.getUnitAttr());
          hasCallArgAttrs = true;
        }
        // Propagate sign/zero extension from the ABI info. This is
        // independent of noundef: the other Extend sites (LowerCall.cpp
        // direct-call args + the LowerFunction return/prolog paths) and OGCG
        // emit signext/zeroext unconditionally, so -no-enable-noundef-analysis
        // must not drop it for indirect/invoke calls.
        if (AI.getKind() == ABIArgInfo::Extend) {
          if (AI.isSignExt())
            attrs.set("cir.signext", rewriter.getUnitAttr());
          else
            attrs.set("cir.zeroext", rewriter.getUnitAttr());
          hasCallArgAttrs = true;
        }
        // Add nofpclass for FP args when fast-math disables NaN/Inf.
        // Matches LowerCall.cpp and OGCG CGCall.cpp:2862-2863.
        if (nofpMask && (AI.isDirect() || AI.isExtend()) &&
            canApplyNoFPClass(AI, I->type)) {
          attrs.set("cir.nofpclass", rewriter.getI32IntegerAttr(nofpMask));
          hasCallArgAttrs = true;
        }
        if (!attrs.empty()) {
          callArgAttrs[idx] = mlir::DictionaryAttr::get(ctx, attrs);
          hasCallArgAttrs = true;
        }
      }
    }

    // Add nonnull/align/dereferenceable on 'this' pointer for instance
    // method calls. For direct calls, these come from the callee FuncOp
    // via propagateArgAttrsToCallSite. For indirect/virtual calls, we must
    // set them here. Matches OGCG CGCall.cpp:2782-2817.
    if (CallInfo.isInstanceMethod() && CallInfo.arg_size() > 0) {
      unsigned firstIR, numIR;
      std::tie(firstIR, numIR) = IRFunctionArgs.getIRArgs(0);
      if (numIR == 1 && firstIR < totalArgs) {
        mlir::NamedAttrList thisAttrs;
        // Preserve any existing attrs for this position.
        if (callArgAttrs[firstIR])
          for (auto existing :
               mlir::cast<mlir::DictionaryAttr>(callArgAttrs[firstIR]))
            thisAttrs.append(existing);

        thisAttrs.set("cir.nonnull", rewriter.getUnitAttr());
        // Get dereferenceable size and alignment from the 'this' type.
        auto thisArgTy = CallInfo.arg_begin()->type;
        if (auto ptrTy = mlir::dyn_cast<cir::PointerType>(thisArgTy)) {
          auto pointeeTy = ptrTy.getPointee();
          if (auto recTy = mlir::dyn_cast<cir::RecordType>(pointeeTy);
              recTy && recTy.isComplete()) {
            const auto &layout = LM.getDataLayout();
            uint64_t size =
                layout.getTypeAllocSize(pointeeTy).getFixedValue();
            if (size > 0)
              thisAttrs.set("cir.dereferenceable",
                            rewriter.getI64IntegerAttr(size));
            uint64_t align =
                layout.getABITypeAlign(pointeeTy).value();
            if (align > 0)
              thisAttrs.set("cir.align",
                            rewriter.getI64IntegerAttr(align));
          }
        }
        callArgAttrs[firstIR] =
            mlir::DictionaryAttr::get(ctx, thisAttrs);
        hasCallArgAttrs = true;
      }
    }

    // Add sret attributes to the sret parameter for indirect calls.
    // For direct calls, propagateArgAttrsToCallSite copies these from the
    // callee FuncOp. For indirect calls we must set them explicitly so
    // that DirectToLLVM emits `sret(T)`, `writable`, `dead_on_unwind`,
    // and `align N` on the call instruction.
    if (IRFunctionArgs.hasSRetArg() && RetAI.isIndirect()) {
      unsigned sretArgNo = IRFunctionArgs.getSRetArgNo();
      mlir::NamedAttrList sretAttrs;
      // Preserve any existing attrs for this position.
      if (callArgAttrs[sretArgNo])
        for (auto existing :
             mlir::cast<mlir::DictionaryAttr>(callArgAttrs[sretArgNo]))
          sretAttrs.append(existing);
      sretAttrs.set("cir.sret", mlir::TypeAttr::get(RetTy));
      if (RetAI.getInReg())
        sretAttrs.set("cir.inreg", rewriter.getUnitAttr());
      // OGCG only sets noalias on function definitions (EmitFunctionProlog),
      // not at call sites. However, musttail calls require call-site arg attrs
      // to match the caller's definition attrs for LLVM tail-call compatibility.
      if (Caller.getMustTailAttr())
        sretAttrs.set("cir.noalias", rewriter.getUnitAttr());
      sretAttrs.set("cir.writable", rewriter.getUnitAttr());
      sretAttrs.set("cir.dead_on_unwind", rewriter.getUnitAttr());
      unsigned alignBytes = RetAI.getIndirectAlign();
      sretAttrs.set("cir.align", rewriter.getI64IntegerAttr(alignBytes));
      callArgAttrs[sretArgNo] =
          mlir::DictionaryAttr::get(ctx, sretAttrs);
      hasCallArgAttrs = true;
    }

    if (hasCallArgAttrs) {
      for (auto &attr : callArgAttrs)
        if (!attr)
          attr = mlir::DictionaryAttr::get(ctx, {});
      newOp->setAttr("cir.call_arg_attrs",
                     mlir::ArrayAttr::get(ctx, callArgAttrs));
    }
  }

  // For indirect calls, set result (return value) attributes on the call op.
  // For direct calls, propagateArgAttrsToCallSite copies these from the callee
  // FuncOp. For indirect calls, there's no callee FuncOp, so we must compute
  // and store them here. Matches OGCG's ConstructAttributeList for return
  // attrs (CGCall.cpp:2690-2710).
  if (Caller.isIndirect() && !mlir::isa<VoidType>(RetTy) &&
      !RetAI.isIndirect()) {
    auto *ctx = rewriter.getContext();
    mlir::NamedAttrList resAttrs;

    // signext/zeroext are unconditional for Extend returns, matching OGCG
    // CGCall.cpp:2703-2710.
    if (RetAI.getKind() == ABIArgInfo::Extend) {
      if (RetAI.isSignExt())
        resAttrs.set("cir.signext", rewriter.getUnitAttr());
      else
        resAttrs.set("cir.zeroext", rewriter.getUnitAttr());
    }

    // nofpclass on return value for FP types when fast-math disables NaN/Inf.
    // Matches LowerCall.cpp and OGCG CGCall.cpp:2717-2718.
    if (RetAI.isDirect() || RetAI.isExtend()) {
      bool noInfs = false, noNans = false;
      const auto &langOpts = LM.getContext().getLangOpts();
      noInfs = langOpts.NoHonorInfs;
      noNans = langOpts.NoHonorNaNs;
      if (!noInfs || !noNans) {
        auto parentFn = Caller->getParentOfType<cir::FuncOp>();
        if (parentFn) {
          if (auto fnExtraAttrs = parentFn.getExtraAttrs()) {
            auto elems = fnExtraAttrs->getElements();
            if (!noInfs) {
              if (auto a = mlir::dyn_cast_or_null<mlir::StringAttr>(
                      elems.get("no-infs-fp-math")))
                noInfs = a.getValue() == "true";
            }
            if (!noNans) {
              if (auto a = mlir::dyn_cast_or_null<mlir::StringAttr>(
                      elems.get("no-nans-fp-math")))
                noNans = a.getValue() == "true";
            }
          }
        }
      }
      if ((noInfs || noNans) && canApplyNoFPClass(RetAI, RetTy)) {
        unsigned mask = 0;
        if (noInfs) mask |= 0x204; // fcInf
        if (noNans) mask |= 0x3;   // fcNan
        resAttrs.set("cir.nofpclass", rewriter.getI32IntegerAttr(mask));
      }
    }

    // noundef is conditional on enableNoundef && hasStrictReturn. Match
    // OGCG's HasStrictReturn(): MSan forces strict returns even in C /
    // extern "C" mode to avoid caller-callee noundef mismatches.
    bool enableNoundef =
        !LM.getModule()->hasAttr("cir.no_noundef_analysis");
    const auto &langOpts = LM.getContext().getLangOpts();
    bool hasStrictReturn =
        langOpts.Sanitize.has(clang::SanitizerKind::Memory);
    if (!hasStrictReturn)
      hasStrictReturn = langOpts.CPlusPlus;
    // For indirect calls through extern "C" function pointer variables,
    // CIRGen sets cir.callee_extern_c in extra_attrs. Check it here to
    // skip noundef, matching OGCG's HasStrictReturn VarDecl check
    // (CGCall.cpp:1923-1926).
    if (hasStrictReturn &&
        !langOpts.Sanitize.has(clang::SanitizerKind::Memory)) {
      if (auto extraAttrs = Caller.getExtraAttrs())
        if (extraAttrs->getElements().get("cir.callee_extern_c"))
          hasStrictReturn = false;
    }
    // OGCG checks StrictReturn, MayDropFunctionReturn, and Return sanitizer.
    if (hasStrictReturn &&
        !LM.getContext().getCodeGenOpts().StrictReturn) {
      bool mayDrop = true;
      if (auto recordTy = mlir::dyn_cast<cir::RecordType>(RetTy))
        mayDrop = !LM.getRecordABIFacts(recordTy).nonTrivialDestructor;
      if (mayDrop && !langOpts.Sanitize.has(clang::SanitizerKind::Return))
        hasStrictReturn = false;
    }
    if (enableNoundef && hasStrictReturn) {
      // Determine noundef for the return type, matching determineNoUndef()
      // in LowerCall.cpp.
      bool isNoundef = false;
      switch (RetAI.getKind()) {
      case ABIArgInfo::Indirect:
        isNoundef = true;
        break;
      case ABIArgInfo::Extend:
        isNoundef = true;
        break;
      case ABIArgInfo::Direct:
        if (mlir::isa<IntType, BoolType, SingleType, DoubleType, FP16Type,
                      BF16Type, FP80Type, FP128Type, LongDoubleType,
                      PointerType, cir::VectorType>(RetTy))
          isNoundef = true;
        break;
      default:
        break;
      }
      if (isNoundef)
        resAttrs.set("cir.noundef", rewriter.getUnitAttr());
    }

    if (!resAttrs.empty())
      newOp->setAttr("cir.call_res_attrs",
                     mlir::DictionaryAttr::get(ctx, resAttrs));
  }

  // Preserve extra_attrs from the original call, or create empty if none.
  auto extraAttrs = Caller.getExtraAttrsAttr();
  if (!extraAttrs)
    extraAttrs = rewriter.getAttr<ExtraFuncAttributesAttr>(
        rewriter.getDictionaryAttr({}));
  newOp->setAttr("extra_attrs", extraAttrs);

  // Merge any cir.call_arg_attrs from the original call (set during CIRGen
  // for indirect-call reference/this-pointer attrs) into the new call op's
  // attrs. These CIRGen attrs are indexed by the original call argument
  // position, so remap each entry through IRFunctionArgs instead of assuming a
  // 1:1 correspondence with lowered IR operands.
  if (auto origCallArgAttrs =
          Caller.getOperation()->getAttrOfType<mlir::ArrayAttr>(
              "cir.call_arg_attrs")) {
    auto *ctx = rewriter.getContext();
    auto existingAttrs =
        newOp->getAttrOfType<mlir::ArrayAttr>("cir.call_arg_attrs");
    unsigned numArgs = IRCallArgs.size();
    if (existingAttrs)
      numArgs = std::max<unsigned>(numArgs, existingAttrs.size());
    llvm::SmallVector<mlir::Attribute> merged(numArgs);
    // Start with existing attrs from target lowering.
    if (existingAttrs)
      for (unsigned i = 0; i < existingAttrs.size() && i < numArgs; ++i)
        merged[i] = existingAttrs[i];
    // Remap original attrs onto the first lowered IR operand produced for each
    // source argument.
    bool changed = false;
    unsigned argOffset = CallInfo.isChainCall() ? 1 : 0;
    unsigned numSourceArgs =
        CallInfo.arg_size() > argOffset ? CallInfo.arg_size() - argOffset : 0;
    for (unsigned argNo = 0,
                  e = std::min<unsigned>(origCallArgAttrs.size(), numSourceArgs);
         argNo != e; ++argNo) {
      auto origDict =
          mlir::cast<mlir::DictionaryAttr>(origCallArgAttrs[argNo]);
      if (origDict.empty())
        continue;

      unsigned firstIRArg, numIRArgs;
      std::tie(firstIRArg, numIRArgs) =
          IRFunctionArgs.getIRArgs(argNo + argOffset);
      if (!numIRArgs || firstIRArg >= numArgs)
        continue;

      mlir::NamedAttrList attrs;
      if (merged[firstIRArg])
        for (auto a : mlir::cast<mlir::DictionaryAttr>(merged[firstIRArg]))
          attrs.append(a);
      for (auto a : origDict)
        attrs.set(a.getName(), a.getValue());
      merged[firstIRArg] = mlir::DictionaryAttr::get(ctx, attrs);
      changed = true;
    }
    if (changed) {
      for (auto &attr : merged)
        if (!attr)
          attr = mlir::DictionaryAttr::get(ctx, {});
      newOp->setAttr("cir.call_arg_attrs", mlir::ArrayAttr::get(ctx, merged));
    }
  }

  // Preserve nothrow attribute (CallOp only; TryCallOp always can throw).
  if (!isTryCall && Caller.getNothrowAttr())
    newCallOp.setNothrow(true);

  // Preserve must_tail attribute (CallOp only).
  if (!isTryCall && Caller.getMustTailAttr())
    newCallOp.setMustTail(true);

  // Preserve tail attribute (CallOp only).
  if (!isTryCall && Caller.getTailAttr())
    newCallOp.setTail(true);

  // Preserve the cleanup region from the original call (CallOp only;
  // TryCallOp doesn't have a cleanup region).
  if (!isTryCall && !Caller.getCleanup().empty()) {
    mlir::Region &srcCleanup = Caller.getCleanup();
    mlir::Region &dstCleanup = newCallOp.getCleanup();
    rewriter.inlineRegionBefore(srcCleanup, dstCleanup, dstCleanup.end());
  }

  // Forward discardable attributes (e.g. cir.imprecise_release) from the
  // original call to the new call. These are not part of the CallOp's
  // inherent attributes and would otherwise be lost during op replacement.
  // Skip cir.call_arg_attrs since it's already handled above with merging.
  for (auto attr : Caller->getDiscardableAttrs()) {
    if (attr.getName() == "cir.call_arg_attrs")
      continue;
    if (attr.getName() == "cir.static_chain")
      continue;
    if (attr.getName() == "cir.runtime_ptrauth") {
      auto rawArgs = Caller.getArgs();
      assert(!rawArgs.empty() && "runtime ptrauth call missing discriminator");
      mlir::Value disc = rawArgs.back();
      auto uintptrTy = mlir::dyn_cast<cir::IntType>(disc.getType());
      assert(uintptrTy && "runtime ptrauth discriminator must be an integer");
      if (uintptrTy.getWidth() != 64) {
        disc = CastOp::create(rewriter, loc,
                              cir::IntType::get(rewriter.getContext(),
                                                /*width=*/64,
                                                /*isSigned=*/false),
                              cir::CastKind::integral, disc);
      }
      if (isTryCall)
        newTryCallOp.getArgsMutable().append(disc);
      else
        newCallOp.getArgsMutable().append(disc);
    }
    newOp->setAttr(attr.getName(), attr.getValue());
  }

  // NOTE(cir): Skipping some ObjC, tail-call, debug, and attribute stuff
  // here.

  // 4. Finish the call.

  // NOTE(cir): Skipping no-return, isMustTail, swift error handling, and
  // writebacks here. These should be handled in CIRGen, I think.

  // Convert return value from ABI-agnostic to ABI-aware.
  mlir::Value Ret = [&] {
    // For musttail calls, skip return value coercion entirely. LLVM requires
    // musttail call to be immediately followed by ret (with at most a bitcast).
    // Inserting store/load operations for type coercion would violate this
    // invariant. The function epilog (buildFunctionEpilog) handles any
    // necessary return type conversion. This matches OGCG's EmitCall which
    // skips all post-call result processing for musttail (CGCall.cpp).
    if (Caller.getMustTailAttr()) {
      if (newOp->getNumResults() > 0)
        return mlir::Value(newOp->getResult(0));
      // The lowered musttail call has no result (e.g. the return is passed
      // indirectly via sret, so the ABI-lowered function returns void). A
      // musttail call must be immediately followed by the return, so do not
      // materialize a value here; the epilog emits the bare return.
      return mlir::Value();
    }

    // NOTE(cir): CIRGen already handled the emission of the return value. We
    // need only to handle the ABI-specific to ABI-agnostic cast here.
    switch (RetAI.getKind()) {

    case cir::ABIArgInfo::Ignore:
      // If we are ignoring an argument that had a result, make sure to
      // construct the appropriate return value for our caller.
      return getUndefRValue(RetTy);

    case ABIArgInfo::Extend:
    case ABIArgInfo::Direct: {
      mlir::Type RetIRTy = RetTy;
      if (RetAI.getCoerceToType() == RetIRTy && RetAI.getDirectOffset() == 0) {
        switch (getEvaluationKind(RetTy)) {
        case cir::TypeEvaluationKind::TEK_Scalar: {
          // If the argument doesn't match, perform a bitcast to coerce it.
          // This can happen due to trivial type mismatches. NOTE(cir):
          // Perhaps this section should handle CIR's boolean case.
          mlir::Value V = mlir::Value(newOp->getResult(0));
          if (V.getType() != RetIRTy)
            cir_cconv_unreachable("NYI");
          return V;
        }
        case cir::TypeEvaluationKind::TEK_Complex: {
          // No coercion needed - the types match.
          return mlir::Value(newOp->getResult(0));
        }
        case cir::TypeEvaluationKind::TEK_Aggregate:
          // Aggregate identity coercion — fall through to the store-based
          // handling below, matching OGCG's behavior.
          break;
        }
      }

      // FIXME(cir): Use return value slot here.
      // Use the Caller parameter (not the class member callOp) so that this
      // code works for both CallOp and TryCallOp lowering paths. When
      // rewriting a TryCallOp, the class member callOp is null and only the
      // Caller parameter holds the (temporary) CallOp.
      mlir::Value RetVal = Caller.getResult();
      mlir::Value dstPtr;
      bool dstIsVolatile = false;
      mlir::IntegerAttr dstAlignAttr;
      for (auto *user : Caller->getUsers()) {
        if (auto storeOp = mlir::dyn_cast<StoreOp>(user)) {
          assert(!dstPtr && "multiple destinations for the return value");
          dstPtr = storeOp.getAddr();
          dstIsVolatile = storeOp.getIsVolatile();
          dstAlignAttr = storeOp.getAlignmentAttr();
        }
      }

      // NOTE(cir): If the function returns, there should always be a valid
      // return value present. Instead of setting the return value here, we
      // should have the ReturnValueSlot object set it beforehand.
      if (!RetVal) {
        RetVal = Caller.getResult();
      }

      // An empty record can overlap other data (if declared with
      // no_unique_address); omit the store for such types - as there is no
      // actual data to store.
      if (mlir::dyn_cast<RecordType>(RetTy) &&
          mlir::cast<RecordType>(RetTy).getNumElements() != 0) {
        RetVal = mlir::Value(newOp->getResult(0));

        // Set insertion point right before the original store to ensure
        // dstPtr dominates all new ops created by createCoercedStore.
        mlir::Location storeLoc = newOp->getLoc();
        if (dstPtr) {
          for (auto *user : Caller->getUsers()) {
            if (auto storeOp = mlir::dyn_cast<StoreOp>(user)) {
              storeLoc = storeOp.getLoc();
              rewriter.setInsertionPoint(storeOp);
              break;
            }
          }
        } else if (!isTryCall) {
          // For regular CallOp, insert coercion ops right after the call.
          // For TryCallOp, the insertion point is already in the normal
          // destination block (set after TryCallOp creation above).
          // Using setInsertionPointAfter(newOp) for TryCallOp would place
          // ops after the terminator, creating blocks without terminators.
          rewriter.setInsertionPointAfter(newOp);
        }

        if (dstPtr) {
          mlir::Value storePtr = dstPtr;
          if (RetAI.getDirectOffset() != 0)
            storePtr = buildAddressAtOffset(*this, storePtr, RetAI);
          createCoercedStore(RetVal, storePtr, dstIsVolatile, storeLoc, *this,
                            dstAlignAttr);
        } else if (RetVal.getType() != RetTy) {
          // When there's no direct store destination (e.g., the call result
          // feeds a yield in a scope op), coerce the ABI return type back to
          // the original type via store/load through memory. Without this,
          // the yield operand type would mismatch the scope result type.
          auto tmpAddr = createTmpAlloca(*this, loc, RetVal.getType());
          StoreOp::create(rewriter, loc, RetVal, tmpAddr.getResult(),
                          /*is_volatile=*/false, /*is_nontemporal=*/false,
                          /*alignment=*/tmpAddr.getAlignmentAttr(),
                          /*sync_scope=*/cir::SyncScopeKindAttr{},
                          /*mem_order=*/cir::MemOrderAttr{},
                          /*tbaa=*/cir::TBAAAttr{});
          auto castPtr =
              CastOp::create(rewriter, loc, PointerType::get(RetTy),
                             CastKind::bitcast, tmpAddr.getResult());
          RetVal = LoadOp::create(rewriter, loc, castPtr.getResult());
        }

        for (auto *user : llvm::make_early_inc_range(Caller->getUsers()))
          if (auto storeOp = mlir::dyn_cast<StoreOp>(user))
            rewriter.eraseOp(storeOp);
      } else if (mlir::isa<RecordType>(RetTy)) {
        // Empty record: no actual data to store. Erase old stores of the
        // call result and return the new call's result for proper RAUW.
        RetVal = mlir::Value(newOp->getResult(0));
        for (auto *user : llvm::make_early_inc_range(Caller->getUsers()))
          if (auto storeOp = mlir::dyn_cast<StoreOp>(user))
            rewriter.eraseOp(storeOp);
      }

      // Complex types with coercion: store coerced value to tmp alloca,
      // bitcast to complex pointer, load, and store to destination.
      if (mlir::isa<ComplexType>(RetTy)) {
        RetVal = mlir::Value(newOp->getResult(0));
        auto coercedTy = RetVal.getType();

        // Store the complex value to the destination and erase old store.
        if (dstPtr) {
          // Set insertion point right before the original store to ensure
          // dstPtr dominates all new ops.
          for (auto *user : Caller->getUsers()) {
            if (auto storeOp = mlir::dyn_cast<StoreOp>(user)) {
              rewriter.setInsertionPoint(storeOp);
              break;
            }
          }
        } else if (!isTryCall) {
          // For regular CallOp, insert coercion ops right after the call.
          // For TryCallOp, the insertion point is already in the normal
          // destination block (set after TryCallOp creation above).
          rewriter.setInsertionPointAfter(newOp);
        }

        auto tmpAddr = createTmpAlloca(*this, loc, coercedTy);
        StoreOp::create(rewriter, loc, RetVal, tmpAddr.getResult(),
                        /*is_volatile=*/false,
                        /*is_nontemporal=*/false,
                        /*alignment=*/tmpAddr.getAlignmentAttr(),
                        /*sync_scope=*/cir::SyncScopeKindAttr{},
                        /*order=*/cir::MemOrderAttr{},
                        /*tbaa=*/cir::TBAAAttr{});
        auto complexPtrTy = PointerType::get(rewriter.getContext(), RetTy);
        mlir::Value castPtr =
            CastOp::create(rewriter, loc, complexPtrTy, CastKind::bitcast,
                           tmpAddr.getResult());
        mlir::Value complexVal = LoadOp::create(rewriter, loc, castPtr);

        if (dstPtr) {
          StoreOp::create(rewriter, loc, complexVal, dstPtr,
                          /*is_volatile=*/dstIsVolatile,
                          /*is_nontemporal=*/false,
                          /*alignment=*/dstAlignAttr,
                          /*sync_scope=*/cir::SyncScopeKindAttr{},
                          /*order=*/cir::MemOrderAttr{},
                          /*tbaa=*/cir::TBAAAttr{});
          for (auto *user : llvm::make_early_inc_range(Caller->getUsers()))
            if (auto storeOp = mlir::dyn_cast<StoreOp>(user))
              rewriter.eraseOp(storeOp);
        }

        RetVal = complexVal;
      }

      // Scalar types with coercion (e.g. __int128 coerced to {i64, i64}):
      // store coerced value to tmp alloca, bitcast to original type pointer,
      // load, and store to destination.
      if (!mlir::isa<RecordType>(RetTy) && !mlir::isa<ComplexType>(RetTy)) {
        RetVal = mlir::Value(newOp->getResult(0));
        auto coercedTy = RetVal.getType();

        if (dstPtr) {
          for (auto *user : Caller->getUsers()) {
            if (auto storeOp = mlir::dyn_cast<StoreOp>(user)) {
              rewriter.setInsertionPoint(storeOp);
              break;
            }
          }
        } else if (!isTryCall) {
          // For regular CallOp, insert coercion ops right after the call.
          // For TryCallOp, the insertion point is already in the normal
          // destination block (set after TryCallOp creation above).
          rewriter.setInsertionPointAfter(newOp);
        }

        // Store coerced result to temporary, bitcast pointer, load as
        // original type.
        auto tmpAddr = createTmpAlloca(*this, loc, coercedTy);
        StoreOp::create(rewriter, loc, RetVal, tmpAddr.getResult(),
                        /*is_volatile=*/false,
                        /*is_nontemporal=*/false,
                        /*alignment=*/tmpAddr.getAlignmentAttr(),
                        /*sync_scope=*/cir::SyncScopeKindAttr{},
                        /*mem_order=*/cir::MemOrderAttr{},
                        /*tbaa=*/cir::TBAAAttr{});
        auto castPtr =
            CastOp::create(rewriter, loc, PointerType::get(RetTy),
                           CastKind::bitcast, tmpAddr.getResult());
        auto scalarVal = LoadOp::create(rewriter, loc, castPtr.getResult());

        if (dstPtr) {
          StoreOp::create(rewriter, loc, scalarVal, dstPtr,
                          /*is_volatile=*/dstIsVolatile,
                          /*is_nontemporal=*/false,
                          /*alignment=*/dstAlignAttr,
                          /*sync_scope=*/cir::SyncScopeKindAttr{},
                          /*mem_order=*/cir::MemOrderAttr{},
                          /*tbaa=*/cir::TBAAAttr{});
          for (auto *user : llvm::make_early_inc_range(Caller->getUsers()))
            if (auto storeOp = mlir::dyn_cast<StoreOp>(user))
              rewriter.eraseOp(storeOp);
        }

        RetVal = scalarVal;
      }

      // NOTE(cir): No need to convert from a temp to an RValue. This is
      // done in CIRGen
      return RetVal;
    }
    case ABIArgInfo::Indirect: {
      // When sret points directly to the destination, erase the original
      // stores (the function already constructed the value there).
      if (sRetIsDirectDst) {
        for (auto *user : llvm::make_early_inc_range(Caller->getUsers()))
          if (auto storeOp = mlir::dyn_cast<StoreOp>(user))
            rewriter.eraseOp(storeOp);
      }
      // Only create a load if the original call result has remaining uses
      // that need a replacement value. When the callee wrote directly to the
      // destination (sRetIsDirectDst), the stores have been erased above and
      // typically no uses remain, so the load would be dead.
      if (Caller.getNumResults() == 0 || Caller.getResult().use_empty())
        return mlir::Value();
      if (auto storageTy = getBitIntStorageType(
              *this, RetTy, CallInfo.getReturnArgInfo().isBitIntType()))
        return createBitIntStorageLoad(*this, loc, sRetPtr, RetTy, storageTy,
                                       RetAI.getIndirectAlign());
      auto load = LoadOp::create(rewriter, loc, sRetPtr);
      return load.getResult();
    }
    case ABIArgInfo::CoerceAndExpand: {
      auto coercionType = RetAI.getCoerceAndExpandType();

      // Bitcast sRetPtr to the padded coercion struct type.
      auto coercionPtrTy = PointerType::get(coercionType);
      auto coercionAddr = CastOp::create(rewriter, loc, coercionPtrTy,
                                          CastKind::bitcast, sRetPtr);

      bool requiresExtract =
          mlir::isa<RecordType>(mlir::Value(newOp->getResult(0)).getType());

      mlir::Value coercionValue = ConstantOp::create(
          rewriter, loc, UndefAttr::get(coercionType));
      unsigned unpaddedIndex = 0;
      for (unsigned i = 0, e = coercionType.getNumElements(); i != e; ++i) {
        mlir::Type eltType = coercionType.getMembers()[i];
        if (ABIArgInfo::isPaddingForCoerceAndExpand(eltType))
          continue;

        mlir::Value elt = mlir::Value(newOp->getResult(0));
        if (requiresExtract) {
          elt = ExtractMemberOp::create(rewriter, loc, elt, unpaddedIndex);
          unpaddedIndex++;
        } else {
          cir_cconv_assert(unpaddedIndex == 0);
        }

        // Coerce element type if the returned type differs from the padded
        // struct element type.
        if (elt.getType() != eltType) {
          if (auto coerced = tryCoerceVectorValue(*this, elt, eltType))
            elt = *coerced;
        }

        if (elt.getType() != eltType) {
          auto tmpCoerce = createTmpAlloca(*this, loc, elt.getType());
          StoreOp::create(rewriter, loc, elt, tmpCoerce.getResult(),
                          /*is_volatile=*/false, /*is_nontemporal=*/false,
                          /*alignment=*/tmpCoerce.getAlignmentAttr(),
                          /*sync_scope=*/cir::SyncScopeKindAttr{},
                          /*mem_order=*/cir::MemOrderAttr{},
                          /*tbaa=*/cir::TBAAAttr{});
          auto castPtr = CastOp::create(rewriter, loc,
                                         PointerType::get(eltType),
                                         CastKind::bitcast,
                                         tmpCoerce.getResult());
          elt = LoadOp::create(rewriter, loc, castPtr.getResult());
        }

        coercionValue = InsertMemberOp::create(
            rewriter, loc, coercionValue, rewriter.getI64IntegerAttr(i), elt);
      }

      StoreOp::create(rewriter, loc, coercionValue, coercionAddr.getResult(),
                      /*is_volatile=*/false, /*is_nontemporal=*/false,
                      /*alignment=*/getAddrAlignmentAttr(
                          coercionAddr.getResult()),
                      /*sync_scope=*/cir::SyncScopeKindAttr{},
                      /*mem_order=*/cir::MemOrderAttr{},
                      /*tbaa=*/cir::TBAAAttr{});

      // Fall through to Indirect handling — load the reconstructed value
      // from sRetPtr.
      if (sRetIsDirectDst) {
        for (auto *user : llvm::make_early_inc_range(Caller->getUsers()))
          if (auto storeOp = mlir::dyn_cast<StoreOp>(user))
            rewriter.eraseOp(storeOp);
      }
      if (Caller.getNumResults() == 0 || Caller.getResult().use_empty())
        return mlir::Value();
      auto loadResult = LoadOp::create(rewriter, loc, sRetPtr);
      return loadResult.getResult();
    }
    case ABIArgInfo::InAlloca:
      cir_cconv_unreachable("NYI: InAlloca in call return rewriting");
    case ABIArgInfo::IndirectAliased:
      cir_cconv_unreachable("Invalid ABIArgInfo for return value");
    }
  }();

  // NOTE(cir): Skipping Emissions, lifetime markers, and dtors here that
  // should be handled in CIRGen.

  return Ret;
}

// NOTE(cir): This method has partial parity to CodeGenFunction's
// GetUndefRValue defined in CGExpr.cpp.
mlir::Value LowerFunction::getUndefRValue(mlir::Type Ty) {
  if (mlir::isa<VoidType>(Ty))
    return nullptr;

  // For record/aggregate/complex types that are being ignored, return nullptr.
  // The caller handles the null return value appropriately.
  if (mlir::isa<RecordType, ArrayType, ComplexType>(Ty))
    return nullptr;

  llvm::errs() << "Missing undef handler for value type: " << Ty << "\n";
  cir_cconv_unreachable("NYI");
}

cir::TypeEvaluationKind LowerFunction::getEvaluationKind(mlir::Type type) {
  // FIXME(cir): Implement type classes for CIR types.
  if (mlir::isa<RecordType, ArrayType>(type))
    return cir::TypeEvaluationKind::TEK_Aggregate;

  if (mlir::isa<ComplexType>(type))
    return cir::TypeEvaluationKind::TEK_Complex;

  // Scalar types
  if (mlir::isa<BoolType, IntType, SingleType, DoubleType, LongDoubleType,
                VectorType, PointerType, VoidType, FP16Type, FP80Type,
                FP128Type, BF16Type, OpaqueType, DataMemberType,
                MethodType>(type))
    return cir::TypeEvaluationKind::TEK_Scalar;

  // MLIR builtin types (e.g., from exception handling runtime signatures).
  if (mlir::isa<mlir::IntegerType, mlir::FloatType>(type))
    return cir::TypeEvaluationKind::TEK_Scalar;

  cir_cconv_unreachable("NYI");
}

} // namespace cir
