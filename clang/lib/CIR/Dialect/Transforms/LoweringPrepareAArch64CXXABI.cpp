//====- LoweringPrepareAArch64CXXABI.cpp - AArch64 ABI specific code -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides AArch64 C++ ABI specific code that is used during LLVMIR
// lowering prepare.
//
//===----------------------------------------------------------------------===//

#include "LoweringPrepareItaniumCXXABI.h"
#include "TargetLowering/ABIInfoImpl.h"
#include "clang/AST/CharUnits.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRDataLayout.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"
#include "clang/CIR/Target/AArch64.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/Triple.h"

using cir::AArch64ABIKind;

/// Check if a CIR type is a valid HFA/HVA base type for AArch64.
/// Valid base types: any floating-point type, or 64/128-bit non-SVE vectors.
static bool isHFABaseType(mlir::Type ty, const cir::CIRDataLayout &dl) {
  if (mlir::isa<cir::SingleType, cir::DoubleType, cir::LongDoubleType,
                cir::FP16Type, cir::BF16Type>(ty))
    return true;
  if (auto vecTy = mlir::dyn_cast<cir::VectorType>(ty)) {
    if (vecTy.getIsScalable() || vecTy.getIsSveFixedLength())
      return false;
    uint64_t vecSize = dl.getTypeAllocSize(vecTy) * 8;
    return vecSize == 64 || vecSize == 128;
  }
  return false;
}

/// Check if two types are equivalent HFA/HVA base types.
/// Vector bases are equivalent if they have the same total size.
static bool sameHFABase(mlir::Type a, mlir::Type b,
                        const cir::CIRDataLayout &dl) {
  if (a == b)
    return true;
  if (auto va = mlir::dyn_cast<cir::VectorType>(a))
    if (auto vb = mlir::dyn_cast<cir::VectorType>(b))
      return dl.getTypeAllocSize(va) == dl.getTypeAllocSize(vb);
  return false;
}

/// Recursively check if a CIR type is a Homogeneous Floating-point/Vector
/// Aggregate (HFA/HVA). Sets base to the element type and members to the leaf
/// count. Handles nested records, arrays, and validates no internal padding.
/// Matches the logic in CallConvLowering's isHomogeneousAggregate.
static bool isHomogeneousAggregate(mlir::Type ty, mlir::Type &base,
                                   uint64_t &members,
                                   const cir::CIRDataLayout &dl) {
  // A _Complex T is treated as a 2-element HFA when T is a valid HFA base
  // (floating-point). Matches CallConvLowering's isHomogeneousAggregate
  // (TargetLowering/Targets/AArch64.cpp) and OGCG ABIInfo::isHomogeneousAggregate.
  if (auto complexTy = mlir::dyn_cast<cir::ComplexType>(ty)) {
    mlir::Type eltTy = complexTy.getElementType();
    if (!isHFABaseType(eltTy, dl))
      return false;
    if (!base)
      base = eltTy;
    else if (!sameHFABase(base, eltTy, dl))
      return false;
    members = 2;
    return true;
  }

  if (isHFABaseType(ty, dl)) {
    if (!base)
      base = ty;
    else if (!sameHFABase(base, ty, dl))
      return false;
    members = 1;
    return true;
  }

  if (auto arrTy = mlir::dyn_cast<cir::ArrayType>(ty)) {
    uint64_t numElements = arrTy.getSize();
    if (numElements == 0)
      return false;
    uint64_t eltMembers = 0;
    if (!isHomogeneousAggregate(arrTy.getElementType(), base, eltMembers, dl))
      return false;
    members = eltMembers * numElements;
    return true;
  }

  if (auto recordTy = mlir::dyn_cast<cir::RecordType>(ty)) {
    auto recordMembers = recordTy.getMembers();
    if (recordMembers.empty())
      return false;
    bool isUnion = recordTy.isUnion();
    members = 0;
    for (auto member : recordMembers) {
      uint64_t fldMembers = 0;
      if (!isHomogeneousAggregate(member, base, fldMembers, dl))
        return false;
      members = isUnion ? std::max(members, fldMembers) : members + fldMembers;
    }
    if (!base || members == 0)
      return false;
    // Padding check: base size * members must equal struct size.
    // This ensures no internal or trailing padding within the aggregate.
    uint64_t baseSize = dl.getTypeAllocSize(base) * 8;
    uint64_t structSize = dl.getTypeAllocSize(ty) * 8;
    if (baseSize * members != structSize)
      return false;
    return true;
  }

  return false;
}

/// Detect if a CIR RecordType is an HFA/HVA with at most 4 members.
/// Returns true and sets baseTy and numMembers on success.
static bool detectHFA(mlir::Type ty, mlir::Type &baseTy, uint64_t &numMembers,
                      const cir::CIRDataLayout &dl) {
  if (!mlir::isa<cir::RecordType, cir::ComplexType>(ty))
    return false;
  baseTy = {};
  numMembers = 0;
  // isHomogeneousAggregate accumulates a partial member count into numMembers
  // before a later non-FP field makes it return false, so the out-params are
  // only meaningful when it succeeds. Reset them here too, otherwise a leaked
  // count (e.g. 2 for {float,float,int}) would misclassify a non-HFA in
  // emitAAPCSVAArgBlocks.
  if (!isHomogeneousAggregate(ty, baseTy, numMembers, dl)) {
    baseTy = {};
    numMembers = 0;
    return false;
  }
  if (numMembers >= 1 && numMembers <= 4)
    return true;
  baseTy = {};
  numMembers = 0;
  return false;
}

/// Match AArch64 illegal-vector detection used by target ABI lowering.
static bool isIllegalVectorTypeForAArch64ABI(mlir::Type ty,
                                             const cir::CIRDataLayout &dl,
                                             const llvm::Triple &triple) {
  auto vecTy = mlir::dyn_cast<cir::VectorType>(ty);
  if (!vecTy)
    return false;
  if (vecTy.getIsScalable())
    return false;
  if (vecTy.getIsSveFixedLength())
    return true;

  unsigned numElements = vecTy.getSize();
  uint64_t size = dl.getTypeSizeInBits(ty);
  if (!llvm::isPowerOf2_32(numElements))
    return true;

  if (triple.getArch() == llvm::Triple::aarch64_32 &&
      triple.isOSBinFormatMachO())
    return size <= 32;

  return size != 64 && (size != 128 || numElements == 1);
}

/// Match AArch64 illegal-vector coercions used by target ABI lowering.
static mlir::Type coerceIllegalVectorTypeForAArch64ABI(
    mlir::Type ty, mlir::MLIRContext *ctx, const cir::CIRDataLayout &dl,
    const llvm::Triple &triple) {
  uint64_t size = dl.getTypeSizeInBits(ty);
  if ((triple.isAndroid() || triple.isOHOSFamily()) && size <= 16)
    return cir::IntType::get(ctx, 16, false);
  if (size <= 32)
    return cir::IntType::get(ctx, 32, false);
  if (size == 64) {
    auto i32Ty = cir::IntType::get(ctx, 32, false);
    return cir::VectorType::get(i32Ty, 2);
  }
  if (size == 128) {
    auto i32Ty = cir::IntType::get(ctx, 32, false);
    return cir::VectorType::get(i32Ty, 4);
  }
  return {};
}

namespace {
class LoweringPrepareAArch64CXXABI : public LoweringPrepareItaniumCXXABI {
public:
  LoweringPrepareAArch64CXXABI(AArch64ABIKind k) : Kind(k) {}
  mlir::Value lowerVAArg(cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
                         const cir::CIRDataLayout &datalayout) override;

private:
  AArch64ABIKind Kind;
  mlir::Value lowerAAPCSVAArg(cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
                              const cir::CIRDataLayout &datalayout);
  mlir::Value lowerDarwinVAArg(cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
                               const cir::CIRDataLayout &datalayout);
  bool isDarwinPCS() const { return Kind == AArch64ABIKind::DarwinPCS; }
  bool isSoftFloat() const { return Kind == AArch64ABIKind::AAPCSSoft; }
};
} // namespace

cir::LoweringPrepareCXXABI *
cir::LoweringPrepareCXXABI::createAArch64ABI(AArch64ABIKind k) {
  return new LoweringPrepareAArch64CXXABI(k);
}

// Helper to compute result type for GetMemberOp from a record pointer.
static mlir::Type getGetMemberResultTy(mlir::Value recordPtr, unsigned idx) {
  auto ptrTy = mlir::cast<cir::PointerType>(recordPtr.getType());
  auto recordTy = mlir::cast<cir::RecordType>(ptrTy.getPointee());
  auto fldTy = recordTy.getMembers()[idx];
  return cir::PointerType::get(fldTy);
}

// Check if the va_arg op is inside a single-block constrained region
// (like the condition region of a do-while, while, or for loop).
// The AAPCS va_arg expansion creates multiple basic blocks, which violates
// the single-block constraint. When this returns true, the expansion must
// be wrapped in a cir.scope to contain the multi-block control flow.
static bool isInSingleBlockConstrainedRegion(cir::VAArgOp op) {
  auto *parentOp = op->getParentOp();
  auto *parentRegion = op->getParentRegion();
  if (auto doOp = mlir::dyn_cast<cir::DoWhileOp>(parentOp))
    return parentRegion == &doOp.getCond();
  if (auto whileOp = mlir::dyn_cast<cir::WhileOp>(parentOp))
    return parentRegion == &whileOp.getCond();
  if (auto forOp = mlir::dyn_cast<cir::ForOp>(parentOp))
    return parentRegion == &forOp.getCond();
  return false;
}

static bool tryLowerRecordVAArgMemcpyPattern(cir::CIRBaseBuilderTy &builder,
                                             cir::VAArgOp op,
                                             mlir::Value srcAddr,
                                             uint64_t memcpyLen,
                                             uint64_t dstAlignment,
                                             uint64_t srcAlignment) {
  if (!op->hasOneUse())
    return false;

  auto store = mlir::dyn_cast<cir::StoreOp>(*op->user_begin());
  if (!store || store.getValue() != op.getResult())
    return false;

  mlir::Value tmpAddr = store.getAddr();
  cir::CopyOp copy;
  for (mlir::Operation *user : tmpAddr.getUsers()) {
    if (user == store.getOperation())
      continue;

    auto currentCopy = mlir::dyn_cast<cir::CopyOp>(user);
    if (!currentCopy || currentCopy.getSrc() != tmpAddr || copy)
      return false;
    copy = currentCopy;
  }

  if (!copy)
    return false;

  auto loc = op.getLoc();
  mlir::Value dstVoid = builder.createBitcast(copy.getDst(),
                                              builder.getVoidPtrTy());
  mlir::Value len = builder.getUnsignedInt(loc, memcpyLen, /*numBits=*/64);
  auto memcpy = cir::MemCpyOp::create(builder, loc, dstVoid, srcAddr, len);

  uint64_t actualDstAlign = dstAlignment;
  if (auto attr = copy.getDstAlignmentAttr())
    actualDstAlign = attr.getInt();
  memcpy->setAttr("cir.dst_align", builder.getI64IntegerAttr(actualDstAlign));
  memcpy->setAttr("cir.src_align", builder.getI64IntegerAttr(srcAlignment));
  if (auto tbaaStruct = op->getAttrOfType<mlir::ArrayAttr>("cir.tbaa_struct"))
    memcpy->setAttr("cir.tbaa_struct", tbaaStruct);

  copy.erase();
  store.erase();
  if (auto alloca = tmpAddr.getDefiningOp<cir::AllocaOp>();
      alloca && tmpAddr.use_empty())
    alloca.erase();
  return true;
}

static bool hasRecordLeafAlignmentAbove(mlir::Type ty, uint64_t alignment,
                                        const cir::CIRDataLayout &datalayout) {
  if (auto recordTy = mlir::dyn_cast<cir::RecordType>(ty))
    return llvm::any_of(recordTy.getMembers(), [&](mlir::Type memberTy) {
      return hasRecordLeafAlignmentAbove(memberTy, alignment, datalayout);
    });

  if (auto arrayTy = mlir::dyn_cast<cir::ArrayType>(ty))
    return hasRecordLeafAlignmentAbove(arrayTy.getElementType(), alignment,
                                       datalayout);

  if (auto complexTy = mlir::dyn_cast<cir::ComplexType>(ty))
    return hasRecordLeafAlignmentAbove(complexTy.getElementType(), alignment,
                                       datalayout);

  return datalayout.getABITypeAlign(ty).value() > alignment;
}

static uint64_t getVAArgStoreDestAlignment(cir::StoreOp store,
                                           const cir::CIRDataLayout &datalayout) {
  if (auto alignAttr = store.getAlignmentAttr())
    return alignAttr.getInt();

  if (auto alloca = store.getAddr().getDefiningOp<cir::AllocaOp>())
    if (auto alignAttr = alloca.getAlignmentAttr())
      return alignAttr.getInt();

  auto ptrTy = mlir::dyn_cast<cir::PointerType>(store.getAddr().getType());
  if (!ptrTy)
    return 1;
  return datalayout.getABITypeAlign(ptrTy.getPointee()).value();
}

static bool tryLowerDirectRecordVAArgStorePattern(
    cir::CIRBaseBuilderTy &builder, cir::VAArgOp op, mlir::Value srcAddr,
    uint64_t memcpyLen, uint64_t srcAlignment,
    const cir::CIRDataLayout &datalayout) {
  if (!op->hasOneUse())
    return false;

  auto store = mlir::dyn_cast<cir::StoreOp>(*op->user_begin());
  if (!store || store.getValue() != op.getResult())
    return false;

  mlir::Value dstAddr = store.getAddr();
  uint64_t dstAlignment = getVAArgStoreDestAlignment(store, datalayout);
  cir::CopyOp copy;
  bool canFoldCopy = true;
  for (mlir::Operation *user : store.getAddr().getUsers()) {
    if (user == store.getOperation())
      continue;

    auto currentCopy = mlir::dyn_cast<cir::CopyOp>(user);
    if (!currentCopy || currentCopy.getSrc() != store.getAddr() || copy) {
      canFoldCopy = false;
      break;
    }
    copy = currentCopy;
  }

  if (canFoldCopy && copy) {
    dstAddr = copy.getDst();
    if (auto attr = copy.getDstAlignmentAttr())
      dstAlignment = attr.getInt();
  }

  auto loc = op.getLoc();
  mlir::Value dstVoid = builder.createBitcast(dstAddr, builder.getVoidPtrTy());
  mlir::Value len = builder.getUnsignedInt(loc, memcpyLen, /*numBits=*/64);
  auto memcpy = cir::MemCpyOp::create(builder, loc, dstVoid, srcAddr, len);
  memcpy->setAttr("cir.dst_align", builder.getI64IntegerAttr(dstAlignment));
  memcpy->setAttr("cir.src_align", builder.getI64IntegerAttr(srcAlignment));
  if (auto tbaaStruct = op->getAttrOfType<mlir::ArrayAttr>("cir.tbaa_struct"))
    memcpy->setAttr("cir.tbaa_struct", tbaaStruct);

  if (canFoldCopy && copy)
    copy.erase();
  store.erase();
  return true;
}

/// Lower Win64/MS va_arg using the same pointer-bump algorithm as OGCG's
/// emitVoidPtrVAArg. arm64ec uses a stricter indirect rule than plain
/// Windows AArch64: any argument wider than 8 bytes, or not 1/2/4/8 bytes,
/// is passed by reference.
static mlir::Value lowerMSVAArg(cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
                                const cir::CIRDataLayout &datalayout) {
  auto loc = op.getLoc();
  auto ty = op.getType();
  auto valist = op.getOperand();
  bool resultUnused = op.use_empty() && mlir::isa<cir::RecordType>(ty);

  auto moduleOp = op->getParentOfType<mlir::ModuleOp>();
  auto tripleAttr =
      moduleOp ? moduleOp->getAttrOfType<mlir::StringAttr>("cir.triple")
               : nullptr;
  llvm::Triple triple(tripleAttr ? tripleAttr.getValue() : "");

  bool isIndirect = false;
  uint64_t tyBitWidth = datalayout.getTypeSizeInBits(ty);
  if (triple.isWindowsArm64EC()) {
    isIndirect = tyBitWidth > 64 || !llvm::isPowerOf2_64(tyBitWidth);
  } else if (cir::isAggregateTypeForABI(ty) && tyBitWidth > 128) {
    isIndirect = true;
  }

  clang::CharUnits directSize;
  if (isIndirect) {
    directSize = clang::CharUnits::fromQuantity(8);
  } else {
    directSize =
        clang::CharUnits::fromQuantity(datalayout.getTypeStoreSize(ty));
  }

  constexpr uint64_t slotSize = 8;
  constexpr uint64_t slotAlignment = slotSize;
  mlir::Value argPtr = builder.createLoad(loc, valist);
  mlir::Value addr = argPtr;

  uint64_t fullDirectSize = directSize.alignTo(
      clang::CharUnits::fromQuantity(static_cast<int64_t>(slotSize)))
                               .getQuantity();
  mlir::Value strideVal = builder.getSignedInt(loc, fullDirectSize, 32);
  mlir::Value castedPtr =
      builder.createPtrBitcast(addr, builder.getSIntNTy(8));
  mlir::Value nextPtr = builder.createPtrStride(loc, castedPtr, strideVal);
  nextPtr = builder.createBitcast(nextPtr, builder.getVoidPtrTy());
  builder.createStore(loc, nextPtr, valist);

  if (resultUnused) {
    auto undef = cir::ConstantOp::create(builder, loc,
                                         cir::UndefAttr::get(ty));
    return undef.getRes();
  }

  if (isIndirect) {
    mlir::Value indirectPtr = builder.createAlignedLoad(
        loc, builder.createPtrBitcast(addr, builder.getVoidPtrTy()),
        slotAlignment);

    if (mlir::isa<cir::RecordType>(ty)) {
      uint64_t tyAlign = datalayout.getABITypeAlign(ty).value();
      uint64_t tyAllocSize = datalayout.getTypeAllocSize(ty).getFixedValue();
      if (tryLowerRecordVAArgMemcpyPattern(builder, op, indirectPtr,
                                           tyAllocSize, tyAlign, tyAlign)) {
        auto undef = cir::ConstantOp::create(builder, loc,
                                             cir::UndefAttr::get(ty));
        return undef.getRes();
      }
    }

    return builder.createLoad(loc, builder.createPtrBitcast(indirectPtr, ty));
  }

  if (mlir::isa<cir::RecordType>(ty)) {
    uint64_t recordAlign = datalayout.getABITypeAlign(ty).value();
    uint64_t memcpyLen = datalayout.getTypeAllocSize(ty).getFixedValue();

    if (tryLowerRecordVAArgMemcpyPattern(builder, op, addr, memcpyLen,
                                         recordAlign, slotAlignment)) {
      auto undef = cir::ConstantOp::create(builder, loc,
                                           cir::UndefAttr::get(ty));
      return undef.getRes();
    }

    if (hasRecordLeafAlignmentAbove(ty, slotAlignment, datalayout) &&
        tryLowerDirectRecordVAArgStorePattern(builder, op, addr, memcpyLen,
                                              slotAlignment, datalayout)) {
      auto undef = cir::ConstantOp::create(builder, loc,
                                           cir::UndefAttr::get(ty));
      return undef.getRes();
    }
  }

  return builder.createAlignedLoad(loc, builder.createPtrBitcast(addr, ty),
                                   slotAlignment);
}

static mlir::Value alignPointerUp(cir::CIRBaseBuilderTy &builder,
                                  mlir::Location loc, mlir::Value ptr,
                                  clang::CharUnits align) {
  auto i8Ty = mlir::IntegerType::get(builder.getContext(), 8);
  auto i8PtrTy = cir::PointerType::get(i8Ty);
  auto ptrDiffTy =
      cir::IntType::get(builder.getContext(), 64, /*isSigned=*/false);
  auto signedPtrDiffTy =
      cir::IntType::get(builder.getContext(), 64, /*isSigned=*/true);

  int64_t alignVal = align.getQuantity();
  auto alignM1 = cir::ConstantOp::create(
      builder, loc, cir::IntAttr::get(ptrDiffTy, alignVal - 1));
  auto castPtr = builder.createBitcast(ptr, i8PtrTy);
  auto roundUp = cir::PtrStrideOp::create(builder, loc, castPtr.getType(),
                                          castPtr, alignM1,
                                          cir::GEPNoWrapFlags::inbounds);
  auto roundUpAsOrig = builder.createBitcast(roundUp, ptr.getType());
  auto negAlignMask = cir::ConstantOp::create(
      builder, loc, cir::IntAttr::get(signedPtrDiffTy, -alignVal));
  return cir::PtrMaskOp::create(builder, loc, roundUpAsOrig.getType(),
                                roundUpAsOrig, negAlignMask);
}

// Emit the AAPCS va_arg ABI logic as multi-block code. The blocks are
// created in the region where the builder is currently positioned.
// Returns the final loaded value, or an undef placeholder when the va_arg
// result itself is unused and only the va_list side effects matter.
//
// This helper is used both for the regular path (blocks in the parent
// function region) and the scope-wrapped path (blocks inside a cir.scope).
static mlir::Value emitAAPCSVAArgBlocks(
    cir::CIRBaseBuilderTy &builder, cir::VAArgOp op, mlir::Location loc,
    mlir::Value valist, mlir::Type opResTy,
    const cir::CIRDataLayout &datalayout, bool IsIndirect, bool IsFPR,
    clang::CharUnits tySize, clang::CharUnits tyAlign, int regSize,
    int regTopIndex, bool resultUnused, bool isHFA = false,
    uint64_t hfaMembers = 0, mlir::Type hfaBaseTy = {}) {
  auto boolTy = builder.getBoolTy();
  auto *entryBlock = builder.getInsertionBlock();
  auto entryIP = builder.saveInsertionPoint();
  auto *region = entryBlock->getParent();

  auto *maybeRegBlock = builder.createBlock(region);
  auto *inRegBlock = builder.createBlock(region);
  auto *onStackBlock = builder.createBlock(region);
  auto *contBlock = builder.createBlock(region);

  //=======================================
  // Entry block: load offset, check >= 0
  //=======================================
  builder.restoreInsertionPoint(entryIP);

  mlir::Value regOffsP;
  cir::LoadOp regOffs;
  if (!IsFPR) {
    auto fldTy = getGetMemberResultTy(valist, 3);
    regOffsP = builder.createGetMember(loc, fldTy, valist, "gr_offs", 3);
    regOffs = builder.createLoad(loc, regOffsP, /*isVolatile=*/false,
                                 /*alignment=*/8);
  } else {
    auto fldTy = getGetMemberResultTy(valist, 4);
    regOffsP = builder.createGetMember(loc, fldTy, valist, "vr_offs", 4);
    regOffs = builder.createLoad(loc, regOffsP);
  }

  auto zeroValue = cir::ConstantOp::create(
      builder, loc, cir::IntAttr::get(regOffs.getType(), 0));
  auto usingStack = cir::CmpOp::create(builder, loc, boolTy,
                                        cir::CmpOpKind::ge, regOffs, zeroValue);
  cir::BrCondOp::create(builder, loc, usingStack, onStackBlock, maybeRegBlock);

  //=======================================
  // Maybe-register block: update offset, check <= 0
  //=======================================
  builder.setInsertionPointToEnd(maybeRegBlock);

  // AAPCS: for non-FPR, non-indirect args with alignment > 8 bytes (e.g.
  // __int128), align the register save area offset upward. Matches OGCG
  // AArch64.cpp EmitAAPCSVAArg (lines 967-976).
  mlir::Value alignedRegOffs = regOffs;
  if (!IsFPR && !IsIndirect && tyAlign.getQuantity() > 8) {
    int64_t alignVal = tyAlign.getQuantity();
    auto alignM1 = cir::ConstantOp::create(
        builder, loc, cir::IntAttr::get(regOffs.getType(), alignVal - 1));
    auto negAlign = cir::ConstantOp::create(
        builder, loc, cir::IntAttr::get(regOffs.getType(), -alignVal));
    auto added = cir::BinOp::create(builder, loc, regOffs.getType(),
                                     cir::BinOpKind::Add, regOffs, alignM1,
                                     {}, {}, {}, {}, {}, {});
    alignedRegOffs = cir::BinOp::create(builder, loc, regOffs.getType(),
                                         cir::BinOpKind::And, added, negAlign,
                                         {}, {}, {}, {}, {}, {});
  }

  auto regSizeValue = cir::ConstantOp::create(
      builder, loc, cir::IntAttr::get(regOffs.getType(), regSize));
  auto newOffset =
      cir::BinOp::create(builder, loc, regOffs.getType(), cir::BinOpKind::Add,
                         alignedRegOffs, regSizeValue, {}, {}, {}, {}, {}, {});
  if (!IsFPR) {
    builder.createStore(loc, newOffset, regOffsP, /*isVolatile=*/false,
                        builder.getAlignmentAttr(8));
  } else {
    builder.createStore(loc, newOffset, regOffsP);
  }

  auto inRegs = cir::CmpOp::create(builder, loc, boolTy, cir::CmpOpKind::le,
                                    newOffset, zeroValue);
  cir::BrCondOp::create(builder, loc, inRegs, inRegBlock, onStackBlock);

  //=======================================
  // In-register block
  //=======================================
  builder.setInsertionPointToEnd(inRegBlock);
  auto regTopFldTy = getGetMemberResultTy(valist, regTopIndex);
  auto regTopP = builder.createGetMember(
      loc, regTopFldTy, valist, IsFPR ? "vr_top" : "gr_top", regTopIndex);
  auto regTop = builder.createLoad(loc, regTopP);
  auto i8Ty = mlir::IntegerType::get(builder.getContext(), 8);
  auto i8PtrTy = cir::PointerType::get(i8Ty);
  auto castRegTop = builder.createBitcast(regTop, i8PtrTy);
  auto resAsInt8P = cir::PtrStrideOp::create(builder, loc, castRegTop.getType(),
                                             castRegTop, alignedRegOffs,
      cir::GEPNoWrapFlags::inbounds);

  mlir::Value regResult;
  if (!resultUnused && isHFA && hfaMembers > 1) {
    // HFA/HVA reconstruction: elements are stored 16 bytes apart in the VR
    // register save area (each in a separate qN slot). We must copy them into
    // a contiguous temporary to match the struct layout. Matches OGCG
    // AArch64.cpp EmitAAPCSVAArg lines 1021-1051.
    assert(!IsIndirect && "HFA/HVA should never be indirect");
    auto alignVal = datalayout.getABITypeAlign(opResTy).value();
    auto alignAttr = builder.getI64IntegerAttr(alignVal);
    auto allocaPtrTy = cir::PointerType::get(opResTy);
    auto tmpAlloca = cir::AllocaOp::create(builder, loc, allocaPtrTy, opResTy,
                                           /*name=*/"hfa.tmp", alignAttr);

    auto basePtrTy = cir::PointerType::get(hfaBaseTy);
    auto baseStoreSize = datalayout.getTypeAllocSize(hfaBaseTy);
    auto idxTy = regOffs.getType();

    // On big-endian platforms, each element is right-aligned within its
    // 16-byte Q-register slot. Matches OGCG AArch64.cpp lines 1033-1037.
    int64_t beHfaOffset = 0;
    if (datalayout.isBigEndian() && baseStoreSize < 16)
      beHfaOffset = 16 - baseStoreSize;

    constexpr uint64_t vrSlotAlign = 16;
    for (uint64_t i = 0; i < hfaMembers; ++i) {
      // Load element from register save area at 16-byte interval + BE offset.
      uint64_t loadOffset = vrSlotAlign * i + beHfaOffset;
      auto loadOffC = cir::ConstantOp::create(
          builder, loc, cir::IntAttr::get(idxTy, loadOffset));
      auto loadAddr = cir::PtrStrideOp::create(builder, loc, i8PtrTy,
          resAsInt8P, loadOffC, cir::GEPNoWrapFlags::inbounds);
      auto loadAddrCast = builder.createBitcast(loadAddr, basePtrTy);
      auto elem = builder.createAlignedLoad(
          loc, loadAddrCast, llvm::MinAlign(vrSlotAlign, loadOffset));

      // Store element contiguously into the temporary alloca.
      auto storeAsI8 = builder.createBitcast(tmpAlloca, i8PtrTy);
      auto storeOffC = cir::ConstantOp::create(
          builder, loc, cir::IntAttr::get(idxTy, baseStoreSize * i));
      auto storeAddr = cir::PtrStrideOp::create(builder, loc, i8PtrTy,
          storeAsI8, storeOffC, cir::GEPNoWrapFlags::inbounds);
      auto storeAddrCast = builder.createBitcast(storeAddr, basePtrTy);
      builder.createStore(loc, elem, storeAddrCast);
    }

    regResult = builder.createBitcast(tmpAlloca, regTop.getType());
  } else {
    // On big-endian, non-HFA values may be right-aligned in their register
    // slot. Applies to: non-indirect scalars, single-member HFAs, and
    // non-aggregate types. Matches OGCG AArch64.cpp lines 1055-1062.
    mlir::Value regAddr = resAsInt8P;
    bool isAggTy = mlir::isa<cir::RecordType>(opResTy);
    if (datalayout.isBigEndian() && !IsIndirect &&
        (isHFA || !isAggTy) &&
        tySize.getQuantity() < (IsFPR ? 16 : 8)) {
      int64_t beOffset = (IsFPR ? 16 : 8) - tySize.getQuantity();
      auto beOffC = cir::ConstantOp::create(
          builder, loc,
          cir::IntAttr::get(regOffs.getType(), beOffset));
      regAddr = cir::PtrStrideOp::create(builder, loc, i8PtrTy,
          regAddr, beOffC, cir::GEPNoWrapFlags::inbounds);
    }
    regResult = builder.createBitcast(regAddr, regTop.getType());
  }
  cir::BrOp::create(builder, loc, contBlock, regResult);

  //=======================================
  // On-stack block
  //=======================================
  builder.setInsertionPointToEnd(onStackBlock);
  auto stackFldTy = getGetMemberResultTy(valist, 0);
  auto stackP = builder.createGetMember(loc, stackFldTy, valist, "stack", 0);
  auto onStackPtr = builder.createLoad(loc, stackP);
  auto ptrDiffTy =
      cir::IntType::get(builder.getContext(), 64, /*isSigned=*/false);

  // AAPCS: for non-indirect args with alignment > 8 bytes, align the on-stack
  // pointer upward using ptrmask. Matches OGCG's emitRoundPointerUpToAlignment
  // called from AArch64.cpp EmitAAPCSVAArg (lines 1079-1081).
  mlir::Value alignedStackPtr = onStackPtr;
  if (!IsIndirect && tyAlign.getQuantity() > 8) {
    int64_t alignVal = tyAlign.getQuantity();
    auto alignM1 = cir::ConstantOp::create(
        builder, loc, cir::IntAttr::get(ptrDiffTy, alignVal - 1));
    auto castForGEP = builder.createBitcast(onStackPtr, i8PtrTy);
    auto roundUp = cir::PtrStrideOp::create(
        builder, loc, castForGEP.getType(), castForGEP, alignM1,
        cir::GEPNoWrapFlags::inbounds);
    auto roundUpAsOrig = builder.createBitcast(roundUp, onStackPtr.getType());
    auto signedPtrDiffTy =
        cir::IntType::get(builder.getContext(), 64, /*isSigned=*/true);
    auto negAlignMask = cir::ConstantOp::create(
        builder, loc, cir::IntAttr::get(signedPtrDiffTy, -alignVal));
    alignedStackPtr = cir::PtrMaskOp::create(builder, loc,
                                              roundUpAsOrig.getType(),
                                              roundUpAsOrig, negAlignMask);
  }

  clang::CharUnits stackSlotSize = clang::CharUnits::fromQuantity(8);
  clang::CharUnits stackSize;
  if (IsIndirect)
    stackSize = stackSlotSize;
  else
    stackSize = tySize.alignTo(stackSlotSize);

  auto stackSizeC = cir::ConstantOp::create(
      builder, loc, cir::IntAttr::get(ptrDiffTy, stackSize.getQuantity()));
  auto castStack = builder.createBitcast(alignedStackPtr, i8PtrTy);
  auto newStackAsi8Ptr = cir::PtrStrideOp::create(
      builder, loc, castStack.getType(), castStack, stackSizeC,
      cir::GEPNoWrapFlags::inbounds);
  auto newStack = builder.createBitcast(newStackAsi8Ptr, onStackPtr.getType());
  builder.createStore(loc, newStack, stackP);

  // On big-endian, non-aggregate types smaller than the stack slot (8 bytes)
  // are right-aligned. Matches OGCG AArch64.cpp lines 1100-1103.
  mlir::Value onStackResult = alignedStackPtr;
  if (datalayout.isBigEndian() && !mlir::isa<cir::RecordType>(opResTy) &&
      tySize < stackSlotSize) {
    clang::CharUnits beOffset = stackSlotSize - tySize;
    auto beOffC = cir::ConstantOp::create(
        builder, loc, cir::IntAttr::get(ptrDiffTy, beOffset.getQuantity()));
    auto castForBE = builder.createBitcast(alignedStackPtr, i8PtrTy);
    auto adjustedP = cir::PtrStrideOp::create(builder, loc, i8PtrTy,
        castForBE, beOffC, cir::GEPNoWrapFlags::inbounds);
    onStackResult = builder.createBitcast(adjustedP, onStackPtr.getType());
  }
  cir::BrOp::create(builder, loc, contBlock, onStackResult);

  //=======================================
  // Continuation block: load the result
  //=======================================
  contBlock->addArgument(onStackPtr.getType(), loc);
  builder.setInsertionPointToEnd(contBlock);
  auto resP = contBlock->getArgument(0);
  assert(mlir::isa<cir::PointerType>(resP.getType()));

  uint64_t directSrcAlignment = IsFPR ? 16 : 8;

  if (resultUnused) {
    auto undef = cir::ConstantOp::create(builder, loc,
                                         cir::UndefAttr::get(opResTy));
    return undef.getRes();
  }

  if (IsIndirect) {
    auto voidPtrPtrTy = cir::PointerType::get(builder.getVoidPtrTy());
    auto castResP = builder.createBitcast(resP, voidPtrPtrTy);
    auto structPtr = builder.createLoad(loc, castResP);
    if (mlir::isa<cir::RecordType>(opResTy)) {
      uint64_t recordAlign = tyAlign.getQuantity();
      uint64_t tyAllocSize = datalayout.getTypeAllocSize(opResTy).getFixedValue();
      if (tryLowerRecordVAArgMemcpyPattern(builder, op, structPtr, tyAllocSize,
                                           recordAlign, recordAlign)) {
        auto undef = cir::ConstantOp::create(builder, loc,
                                             cir::UndefAttr::get(opResTy));
        return undef.getRes();
      }
    }

    auto opResPTy = cir::PointerType::get(opResTy);
    auto castStructPtr = builder.createBitcast(structPtr, opResPTy);
    return builder.createAlignedLoad(loc, castStructPtr,
                                     tyAlign.getQuantity());
  }

  if (mlir::isa<cir::RecordType>(opResTy) && (!isHFA || hfaMembers <= 1)) {
    uint64_t recordAlign = tyAlign.getQuantity();
    uint64_t memcpyLen = datalayout.getTypeAllocSize(opResTy).getFixedValue();

    // The source address is the phi of the in-reg and on-stack pointers.
    // In-reg: directSrcAlignment (16 for FPR, 8 for GPR).
    // On-stack: max(8, tyAlign) because the stack is at least 8-byte aligned
    // and over-aligned types get ptrmask'd upward.
    // The phi alignment is the minimum of both paths.
    uint64_t onStackSrcAlign =
        std::max(uint64_t{8}, static_cast<uint64_t>(tyAlign.getQuantity()));
    uint64_t srcAlign = std::min(directSrcAlignment, onStackSrcAlign);

    if (tryLowerRecordVAArgMemcpyPattern(builder, op, resP, memcpyLen,
                                         recordAlign, srcAlign)) {
      auto undef = cir::ConstantOp::create(builder, loc,
                                           cir::UndefAttr::get(opResTy));
      return undef.getRes();
    }

    if (hasRecordLeafAlignmentAbove(opResTy, srcAlign, datalayout) &&
        tryLowerDirectRecordVAArgStorePattern(builder, op, resP, memcpyLen,
                                              srcAlign,
                                              datalayout)) {
      auto undef = cir::ConstantOp::create(builder, loc,
                                           cir::UndefAttr::get(opResTy));
      return undef.getRes();
    }
  }

  auto opResPTy = cir::PointerType::get(opResTy);
  auto castResP = builder.createBitcast(resP, opResPTy);
  uint64_t loadAlignment = tyAlign.getQuantity();
  if (!IsFPR &&
      mlir::isa<cir::IntType, cir::PointerType, cir::BoolType>(opResTy)) {
    // Match OGCG's merged Address alignment for direct scalar GPR va_arg
    // results. The selected address still names an 8-byte argument slot even
    // when the source type's own ABI alignment is smaller, such as `int`.
    loadAlignment = directSrcAlignment;
    if (datalayout.isBigEndian() && tySize.getQuantity() < 8)
      loadAlignment = llvm::MinAlign(
          loadAlignment, static_cast<uint64_t>(8 - tySize.getQuantity()));
  } else if (loadAlignment == 0 || loadAlignment > directSrcAlignment) {
    loadAlignment = directSrcAlignment;
  }
  return builder.createAlignedLoad(loc, castResP, loadAlignment);
}

mlir::Value LoweringPrepareAArch64CXXABI::lowerAAPCSVAArg(
    cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
    const cir::CIRDataLayout &datalayout) {
  auto loc = op->getLoc();
  auto valist = op->getOperand(0);
  auto opResTy = op.getType();
  bool resultUnused = op.use_empty() && mlir::isa<cir::RecordType>(opResTy);
  auto vectorTy = mlir::dyn_cast<cir::VectorType>(opResTy);
  bool isSupportedVectorTy =
      vectorTy && !vectorTy.getIsScalable() && !vectorTy.getIsSveFixedLength();
  auto moduleOp = op->getParentOfType<mlir::ModuleOp>();
  auto tripleAttr =
      moduleOp ? moduleOp->getAttrOfType<mlir::StringAttr>("cir.triple")
               : nullptr;
  llvm::Triple triple(tripleAttr ? tripleAttr.getValue() : "");

  // If we encounter an unsupported type, fall back to LLVM's native va_arg
  // handling rather than crashing.
  bool isRecordType = mlir::isa<cir::RecordType>(opResTy);
  bool isComplexType = mlir::isa<cir::ComplexType>(opResTy);
  bool isSupportedType =
      isRecordType || isComplexType || isSupportedVectorTy ||
      mlir::isa<cir::IntType, cir::SingleType, cir::PointerType, cir::BoolType,
                cir::DoubleType, cir::LongDoubleType, cir::FP16Type,
                cir::BF16Type, cir::FP128Type>(opResTy);
  if (!isSupportedType)
    return {};

  // Detect Homogeneous Floating-point/Vector Aggregates (HFA/HVA).
  // HFAs use the FP/SIMD register save area and are never indirect. A
  // floating-point _Complex is a 2-element HFA (matches OGCG EmitAAPCSVAArg,
  // which routes _Complex through isHomogeneousAggregate).
  mlir::Type hfaBaseTy;
  uint64_t hfaMembers = 0;
  bool isHFA = !isSoftFloat() && (isRecordType || isComplexType) &&
               detectHFA(opResTy, hfaBaseTy, hfaMembers, datalayout);
  bool isIllegalVectorTy =
      isSupportedVectorTy &&
      isIllegalVectorTypeForAArch64ABI(opResTy, datalayout, triple);
  mlir::Type coercedVectorTy;
  if (isIllegalVectorTy)
    coercedVectorTy = coerceIllegalVectorTypeForAArch64ABI(
        opResTy, builder.getContext(), datalayout, triple);

  // AAPCS: non-HFA structs > 16 bytes are passed indirectly (by pointer).
  // Direct vector types > 16 bytes are likewise passed indirectly. HFA/HVA
  // records are always direct regardless of size.
  bool IsIndirect = false;
  if (isIllegalVectorTy) {
    IsIndirect = !coercedVectorTy;
  } else if ((isRecordType || isSupportedVectorTy) && !isHFA) {
    auto structSize = datalayout.getTypeStoreSize(opResTy);
    if (structSize > 16)
      IsIndirect = true;
  }

  // Match OGCG: determine the register class from the ABI-coerced type, not
  // from the source va_arg result type. Small illegal vectors on AArch64 use
  // the integer vararg area once coerced to i16/i32.
  mlir::Type baseTy = opResTy;
  uint64_t numRegs = 1;
  if (IsIndirect) {
    baseTy = cir::PointerType::get(builder.getVoidTy());
  } else if (isHFA) {
    baseTy = hfaBaseTy;
    numRegs = hfaMembers;
  } else if (coercedVectorTy) {
    baseTy = coercedVectorTy;
  }
  bool IsFPR = !isSoftFloat() &&
               (mlir::isa<cir::VectorType>(baseTy) ||
                cir::isAnyFloatingPointType(baseTy));

  clang::CharUnits tySize =
      clang::CharUnits::fromQuantity(datalayout.getTypeStoreSize(opResTy));
  // Use the unadjusted alignment from the va_arg op if available, which was
  // set at CIRGen time from the AST's getTypeUnadjustedAlignInChars. This is
  // needed because packed/aligned attributes on structs affect alignment in
  // ways that the CIR data layout cannot reconstruct. Falls back to the data
  // layout's ABI type alignment if the attribute is not present.
  clang::CharUnits tyAlign;
  if (auto alignAttr = op.getAlignmentAttr())
    tyAlign = clang::CharUnits::fromQuantity(alignAttr.getInt());
  else
    tyAlign = clang::CharUnits::fromQuantity(
        datalayout.getABITypeAlign(opResTy).value());

  int regSize = IsIndirect ? 8 : tySize.getQuantity();
  int regTopIndex;
  if (!IsFPR) {
    regTopIndex = 1;
    regSize = llvm::alignTo(regSize, 8);
  } else {
    regTopIndex = 2;
    // Each FP/SIMD register occupies a 16-byte VR slot. For HFA/HVA types,
    // each element uses one slot. Matches OGCG: RegSize = 16 * NumRegs.
    regSize = 16 * numRegs;
  }

  // The AAPCS va_arg expansion creates multiple basic blocks (for the
  // register-vs-stack decision). If we're inside a single-block constrained
  // region (e.g., the condition of a do-while/while/for loop), wrap the
  // expansion in a cir.scope to contain the multi-block control flow.
  if (isInSingleBlockConstrainedRegion(op)) {
    mlir::Value result;
    auto scopeOp = cir::ScopeOp::create(
        builder, loc,
        [&](mlir::OpBuilder &scopeBuilder, mlir::Type &yieldTy,
            mlir::Location scopeLoc) {
          yieldTy = opResTy;
          cir::CIRBaseBuilderTy sb(scopeBuilder);
          result = emitAAPCSVAArgBlocks(sb, op, loc, valist, opResTy,
                                        datalayout, IsIndirect, IsFPR, tySize,
                                        tyAlign, regSize, regTopIndex,
                                        resultUnused, isHFA,
                                        hfaMembers, hfaBaseTy);
          cir::YieldOp::create(sb, scopeLoc, result);
        });
    return scopeOp.getResult(0);
  }

  // Regular path: expand multi-block code directly into the parent region.
  // Split the current block at the va_arg op so that the entry block is clean
  // (no ops after the insertion point) when the helper emits into it.
  auto *currentBlock = builder.getInsertionBlock();
  auto *restBlock = currentBlock->splitBlock(op);

  // Position at the end of the now-clean entry block.
  builder.setInsertionPointToEnd(currentBlock);

  // Create the multi-block expansion.
  auto result = emitAAPCSVAArgBlocks(builder, op, loc, valist, opResTy,
                                     datalayout, IsIndirect, IsFPR, tySize,
                                     tyAlign, regSize, regTopIndex,
                                     resultUnused, isHFA,
                                     hfaMembers, hfaBaseTy);

  // The helper left the builder at the end of contBlock. Move the va_arg op
  // and everything that followed it (now in restBlock) into contBlock.
  auto *contBlock = builder.getInsertionBlock();
  contBlock->getOperations().splice(contBlock->end(),
                                    restBlock->getOperations());
  restBlock->erase();

  // Position the builder just before the va_arg op (which is now in contBlock).
  builder.setInsertionPoint(op);
  return result;
}

mlir::Value LoweringPrepareAArch64CXXABI::lowerDarwinVAArg(
    cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
    const cir::CIRDataLayout &datalayout) {
  // Darwin va_list is a simple char* pointer. The algorithm:
  // 1. Load the current pointer from va_list
  // 2. Advance the pointer by the slot size (type size rounded up to 8 bytes)
  // 3. Store the advanced pointer back to va_list
  // 4. Load the value from the original pointer
  // For indirect types (structs > 16 bytes non-HFA), double-dereference.
  auto loc = op->getLoc();
  auto valist = op->getOperand(0);
  auto opResTy = op.getType();
  bool resultUnused = op.use_empty() && mlir::isa<cir::RecordType>(opResTy);
  auto vectorTy = mlir::dyn_cast<cir::VectorType>(opResTy);
  bool isSupportedVectorTy =
      vectorTy && !vectorTy.getIsScalable() && !vectorTy.getIsSveFixedLength();

  clang::CharUnits tySize =
      clang::CharUnits::fromQuantity(datalayout.getTypeStoreSize(opResTy));
  clang::CharUnits tyAlign;
  if (auto alignAttr = op.getAlignmentAttr())
    tyAlign = clang::CharUnits::fromQuantity(alignAttr.getInt());
  else
    tyAlign = clang::CharUnits::fromQuantity(
        datalayout.getABITypeAlign(opResTy).value());

  // Determine if the argument is passed indirectly (by pointer).
  // On Darwin, non-HFA structs > 16 bytes are indirect.
  // Direct vector types > 16 bytes are also indirect. HFA/HVA structs are
  // always direct regardless of size.
  bool isRecordType = mlir::isa<cir::RecordType>(opResTy);
  mlir::Type darwinHfaBase;
  uint64_t darwinHfaMembers = 0;
  bool darwinIsHFA = isRecordType && detectHFA(opResTy, darwinHfaBase,
                                                darwinHfaMembers, datalayout);
  bool IsIndirect = false;
  if ((isRecordType || isSupportedVectorTy) && !darwinIsHFA) {
    if (tySize.getQuantity() > 16)
      IsIndirect = true;
  }

  // For indirect, we're loading a pointer (8 bytes) from the stack.
  clang::CharUnits slotSize = clang::CharUnits::fromQuantity(8);
  clang::CharUnits directSize =
      IsIndirect ? slotSize : tySize.alignTo(slotSize);
  clang::CharUnits directAlign = IsIndirect ? slotSize : tyAlign;

  // Load current va_list pointer.
  auto curPtr = builder.createLoad(loc, valist);

  auto i8Ty = mlir::IntegerType::get(builder.getContext(), 8);
  auto i8PtrTy = cir::PointerType::get(i8Ty);
  auto ptrDiffTy =
      cir::IntType::get(builder.getContext(), 64, /*isSigned=*/false);

  mlir::Value dataPtr = curPtr;
  if (directAlign > slotSize)
    dataPtr = alignPointerUp(builder, loc, curPtr, directAlign);

  // Advance the pointer by slot size.
  auto castPtr = builder.createBitcast(dataPtr, i8PtrTy);
  auto slotSizeC = cir::ConstantOp::create(
      builder, loc, cir::IntAttr::get(ptrDiffTy, directSize.getQuantity()));
  auto nextPtr = cir::PtrStrideOp::create(builder, loc, castPtr.getType(),
                                          castPtr, slotSizeC,
      cir::GEPNoWrapFlags::inbounds);
  auto nextPtrCast = builder.createBitcast(nextPtr, curPtr.getType());

  // Store the advanced pointer back.
  builder.createStore(loc, nextPtrCast, valist);

  if (resultUnused) {
    auto undef = cir::ConstantOp::create(builder, loc,
                                         cir::UndefAttr::get(opResTy));
    return undef.getRes();
  }

  // Load the value from the original pointer.
  if (IsIndirect) {
    // Indirect: load the pointer-to-struct, then load the struct.
    auto voidPtrPtrTy = cir::PointerType::get(builder.getVoidPtrTy());
    auto castCurPtr = builder.createBitcast(dataPtr, voidPtrPtrTy);
    auto structPtr = builder.createLoad(loc, castCurPtr);
    auto opResPTy = cir::PointerType::get(opResTy);
    auto castStructPtr = builder.createBitcast(structPtr, opResPTy);
    return builder.createAlignedLoad(loc, castStructPtr,
                                     tyAlign.getQuantity());
  }

  auto opResPTy = cir::PointerType::get(opResTy);
  auto castCurPtr = builder.createBitcast(dataPtr, opResPTy);
  return builder.createLoad(loc, castCurPtr);
}

mlir::Value
LoweringPrepareAArch64CXXABI::lowerVAArg(cir::CIRBaseBuilderTy &builder,
                                         cir::VAArgOp op,
                                         const cir::CIRDataLayout &datalayout) {
  auto opResTy = op.getType();
  // emitVAArg annotates the cases OGCG ignores entirely before touching the
  // va_list. This keeps Darwin empty-record va_arg semantics distinct from
  // zero-sized-but-nonempty wrappers that still consume a zero-byte access.
  if (op->hasAttr("cir.aarch64_va_abi_ignored"))
    return builder.getNullValue(opResTy, op->getLoc());

  // Match OGCG's early AArch64 EmitVAArg guard: pure scalable SVE vectors are
  // not supported in variadic calls, so fail instead of inventing a bogus
  // direct-CIR va_arg lowering path for them.
  if (auto vecTy = mlir::dyn_cast<cir::VectorType>(opResTy);
      vecTy && vecTy.getIsScalable()) {
    llvm::report_fatal_error(
        "Passing SVE types to variadic functions is currently not supported");
  }

  if (Kind == AArch64ABIKind::Win64 || op->hasAttr("cir.ms_abi"))
    return lowerMSVAArg(builder, op, datalayout);

  if (isDarwinPCS())
    return lowerDarwinVAArg(builder, op, datalayout);
  return lowerAAPCSVAArg(builder, op, datalayout);
}
