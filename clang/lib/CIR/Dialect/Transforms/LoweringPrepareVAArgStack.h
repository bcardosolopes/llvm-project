//===- LoweringPrepareVAArgStack.h - shared stack va_arg lowering ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Shared helper for lowering cir.va_arg on stack-based ABIs whose va_list is a
// plain pointer that walks the argument overflow area (ARM/AAPCS and i386
// SysV). Both targets copy an aggregate out of the stack slot at the current
// pointer, then advance the pointer by the size rounded up to the slot size,
// optionally aligning the slot pointer up first. This mirrors classic CodeGen's
// emitVoidPtrDirectVAArg (ABIInfoImpl.cpp).
//
//===----------------------------------------------------------------------===//

#ifndef CIR_DIALECT_TRANSFORMS_LOWERINGPREPAREVAARGSTACK_H
#define CIR_DIALECT_TRANSFORMS_LOWERINGPREPAREVAARGSTACK_H

#include "clang/CIR/Dialect/Builder/CIRBaseBuilder.h"
#include "clang/CIR/Dialect/IR/CIRDataLayout.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "llvm/Support/MathExtras.h"

namespace cir {
namespace vaarg_stack {

/// Extract the TBAA tag for a scalar va_arg result, if present.
inline cir::TBAAAttr getScalarVAArgTBAATag(cir::VAArgOp op) {
  auto tbaaStruct = op->getAttrOfType<mlir::ArrayAttr>("cir.tbaa_struct");
  if (!tbaaStruct || tbaaStruct.size() != 1)
    return {};
  auto triple = mlir::dyn_cast<mlir::ArrayAttr>(tbaaStruct[0]);
  if (!triple || triple.size() != 3)
    return {};
  auto offset = mlir::dyn_cast<mlir::IntegerAttr>(triple[0]);
  if (!offset || offset.getInt() != 0)
    return {};
  return mlir::dyn_cast<cir::TBAAAttr>(triple[2]);
}

inline cir::LoadOp createVAArgResultLoad(cir::CIRBaseBuilderTy &builder,
                                         cir::VAArgOp op, mlir::Location loc,
                                         mlir::Value ptr,
                                         uint64_t alignment = 0) {
  cir::LoadOp load =
      builder.createLoad(loc, ptr, /*isVolatile=*/false, alignment);
  if (auto tbaa = getScalarVAArgTBAATag(op))
    load.setTbaaAttr(tbaa);
  return load;
}

/// Peephole for the common `store %vaarg to %tmp; copy %tmp to %dst` sequence:
/// rewrite it to a single memcpy directly from the stack slot into the final
/// destination. Matches OGCG's EmitAggFinalDestCopy, which memcpy's the
/// type's store size straight into the destination. Returns true on success.
inline bool tryLowerRecordVAArgMemcpyPattern(cir::CIRBaseBuilderTy &builder,
                                             cir::VAArgOp op,
                                             mlir::Value srcAddr,
                                             uint64_t memcpyLen,
                                             uint64_t dstAlignment,
                                             uint64_t srcAlignment,
                                             unsigned lenBits) {
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

  auto loc = store.getLoc();
  mlir::Value dstVoid =
      builder.createBitcast(copy.getDst(), builder.getVoidPtrTy());
  mlir::Value len = builder.getUnsignedInt(loc, memcpyLen, /*numBits=*/lenBits);
  auto memcpy = cir::MemCpyOp::create(builder, loc, dstVoid, srcAddr, len);

  uint64_t actualDstAlign = dstAlignment;
  if (auto attr = copy.getDstAlignmentAttr())
    actualDstAlign = attr.getInt();
  memcpy->setAttr("cir.dst_align", builder.getI64IntegerAttr(actualDstAlign));
  memcpy->setAttr("cir.src_align", builder.getI64IntegerAttr(srcAlignment));

  copy.erase();
  store.erase();
  if (auto alloca = tmpAddr.getDefiningOp<cir::AllocaOp>();
      alloca && tmpAddr.use_empty())
    alloca.erase();
  return true;
}

/// Round a byte pointer up to `align` using the same GEP-then-ptrmask idiom as
/// classic CodeGen's emitRoundPointerUpToAlignment. `ptrIntBits` is the pointer
/// index width for the target (32 for arm32/i386), so the emitted constants
/// match classic's IntPtrTy.
inline mlir::Value alignStackPointerUp(cir::CIRBaseBuilderTy &builder,
                                       mlir::Location loc, mlir::Value ptr,
                                       int64_t align, unsigned ptrIntBits) {
  auto i8Ty = mlir::IntegerType::get(builder.getContext(), 8);
  auto i8PtrTy = cir::PointerType::get(i8Ty);
  auto uIntPtrTy =
      cir::IntType::get(builder.getContext(), ptrIntBits, /*isSigned=*/false);
  auto sIntPtrTy =
      cir::IntType::get(builder.getContext(), ptrIntBits, /*isSigned=*/true);

  auto alignM1 = cir::ConstantOp::create(
      builder, loc, cir::IntAttr::get(uIntPtrTy, align - 1));
  auto castPtr = builder.createBitcast(ptr, i8PtrTy);
  auto roundUp = cir::PtrStrideOp::create(builder, loc, castPtr.getType(),
                                          castPtr, alignM1,
                                          cir::GEPNoWrapFlags::inbounds);
  auto roundUpAsOrig = builder.createBitcast(roundUp, ptr.getType());
  auto negAlignMask = cir::ConstantOp::create(
      builder, loc, cir::IntAttr::get(sIntPtrTy, -align));
  return cir::PtrMaskOp::create(builder, loc, roundUpAsOrig.getType(),
                                roundUpAsOrig, negAlignMask);
}

/// Lower a cir.va_arg producing an aggregate on a stack-based ABI.
///
/// \p tyStoreSize  the type's store size in bytes (memcpy length).
/// \p directAlign  the ABI slot alignment for this argument in bytes. If it
///                 exceeds \p slotSize the slot pointer is rounded up.
/// \p slotSize     the stack slot granularity in bytes (4 for arm32/i386).
/// \p ptrIntBits   pointer index width for the target (32).
///
/// Mirrors classic emitVoidPtrDirectVAArg: load argp.cur, optionally align it
/// up, store back argp.cur + roundUp(size, slotSize), and copy the aggregate
/// out of the (aligned) slot. Returns an undef of the result type on success
/// (the real data movement is folded into a memcpy) or a loaded value if the
/// memcpy peephole did not apply.
inline mlir::Value
lowerAggregateStackVAArg(cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
                         uint64_t tyStoreSize, uint64_t directAlign,
                         uint64_t slotSize, unsigned ptrIntBits) {
  mlir::Location loc = op.getLoc();
  mlir::Value valist = op.getOperand();
  mlir::Type ty = op.getType();

  auto i8Ty = mlir::IntegerType::get(builder.getContext(), 8);
  auto i8PtrTy = cir::PointerType::get(i8Ty);
  auto i8PtrPtrTy = cir::PointerType::get(i8PtrTy);

  // Reinterpret the va_list slot as a char** and load argp.cur (a char*). Some
  // targets model va_list as a plain char* (i386), others as a one-field record
  // whose only member is that char* at offset 0 (arm32 __va_list); casting the
  // slot pointer to char** loads the current argument pointer in both cases,
  // matching classic emitVoidPtrDirectVAArg's VAListAddr.withElementType.
  mlir::Value valistPtr = builder.createBitcast(valist, i8PtrPtrTy);
  mlir::Value curPtr = builder.createLoad(loc, valistPtr);

  // If the CC aligns this type higher than the slot size, round the slot
  // pointer up before reading (classic AllowHigherAlign path).
  mlir::Value dataPtr = curPtr;
  uint64_t srcAlign = slotSize;
  if (directAlign > slotSize) {
    dataPtr = alignStackPointerUp(builder, loc, curPtr, directAlign, ptrIntBits);
    srcAlign = directAlign;
  }

  // Advance argp by size rounded up to the slot size, and store it back.
  uint64_t fullDirectSize = llvm::alignTo(tyStoreSize, slotSize);
  auto castData = builder.createBitcast(dataPtr, i8PtrTy);
  mlir::Value strideVal =
      builder.getSignedInt(loc, fullDirectSize, ptrIntBits);
  auto nextPtr = cir::PtrStrideOp::create(builder, loc, castData.getType(),
                                          castData, strideVal,
                                          cir::GEPNoWrapFlags::inbounds);
  builder.createStore(loc, nextPtr, valistPtr);

  // Fold the aggregate copy into a single memcpy from the slot to the final
  // destination when the standard store-to-tmp/copy pattern is present.
  if (tryLowerRecordVAArgMemcpyPattern(builder, op, castData, tyStoreSize,
                                       directAlign, srcAlign,
                                       /*lenBits=*/ptrIntBits)) {
    auto undef =
        cir::ConstantOp::create(builder, loc, cir::UndefAttr::get(ty));
    return undef.getRes();
  }

  // Fallback: load the aggregate value directly from the slot.
  auto slotPtr = builder.createPtrBitcast(dataPtr, ty);
  return createVAArgResultLoad(builder, op, loc, slotPtr, srcAlign);
}

} // namespace vaarg_stack
} // namespace cir

#endif // CIR_DIALECT_TRANSFORMS_LOWERINGPREPAREVAARGSTACK_H
