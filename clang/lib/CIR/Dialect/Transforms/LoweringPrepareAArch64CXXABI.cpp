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
#include "clang/AST/CharUnits.h"
#include "clang/CIR/Dialect/IR/CIRDataLayout.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"
#include "clang/CIR/Target/AArch64.h"

using cir::AArch64ABIKind;

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
  bool isDarwinPCS() const { return Kind == AArch64ABIKind::DarwinPCS; }
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

mlir::Value LoweringPrepareAArch64CXXABI::lowerAAPCSVAArg(
    cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
    const cir::CIRDataLayout &datalayout) {
  auto loc = op->getLoc();
  auto valist = op->getOperand(0);
  auto opResTy = op.getType();

  // Front end should not produce non-scalar type of VAArgOp.
  bool isSupportedType =
      mlir::isa<cir::IntType, cir::SingleType, cir::PointerType, cir::BoolType,
                cir::DoubleType>(opResTy);
  assert(isSupportedType && "unsupported va_arg type for AArch64");
  assert(!cir::MissingFeatures::supportisHomogeneousAggregateQueryForAArch64());

  bool IsFPR = cir::isAnyFloatingPointType(opResTy);

  // The AArch64 va_list type and handling is specified in the Procedure Call
  // Standard, section B.4:
  //
  // struct {
  //   void *__stack;
  //   void *__gr_top;
  //   void *__vr_top;
  //   int __gr_offs;
  //   int __vr_offs;
  // };
  auto curInsertionP = builder.saveInsertionPoint();
  auto currentBlock = builder.getInsertionBlock();
  auto boolTy = builder.getBoolTy();

  auto maybeRegBlock = builder.createBlock(builder.getBlock()->getParent());
  auto inRegBlock = builder.createBlock(builder.getBlock()->getParent());
  auto onStackBlock = builder.createBlock(builder.getBlock()->getParent());

  //=======================================
  // Find out where argument was passed
  //=======================================

  clang::CharUnits tySize =
      clang::CharUnits::fromQuantity(datalayout.getTypeStoreSize(opResTy));

  int regSize = tySize.getQuantity();
  int regTopIndex;

  builder.restoreInsertionPoint(curInsertionP);
  // 3 is the field number of __gr_offs, 4 is the field number of __vr_offs
  mlir::Value regOffsP;
  cir::LoadOp regOffs;
  if (!IsFPR) {
    auto fldTy = getGetMemberResultTy(valist, 3);
    regOffsP = builder.createGetMember(loc, fldTy, valist, "gr_offs", 3);
    regOffs = builder.createLoad(loc, regOffsP);
    regTopIndex = 1;
    regSize = llvm::alignTo(regSize, 8);
  } else {
    auto fldTy = getGetMemberResultTy(valist, 4);
    regOffsP = builder.createGetMember(loc, fldTy, valist, "vr_offs", 4);
    regOffs = builder.createLoad(loc, regOffsP);
    regTopIndex = 2;
    regSize = 16;
  }

  //=======================================
  // Check if argument is in registers
  //=======================================

  // If regOffs >= 0 we're already using the stack for this type of argument.
  auto zeroValue = cir::ConstantOp::create(
      builder, loc, cir::IntAttr::get(regOffs.getType(), 0));
  auto usingStack = cir::CmpOp::create(builder, loc, boolTy, cir::CmpOpKind::ge,
                                       regOffs, zeroValue);
  cir::BrCondOp::create(builder, loc, usingStack, onStackBlock, maybeRegBlock);

  auto contBlock = currentBlock->splitBlock(op);
  // Move contBlock after onStackBlock to maintain proper block ordering.
  auto contBlockIter = contBlock->getIterator();
  contBlock->getParent()->getBlocks().remove(contBlockIter);
  onStackBlock->getParent()->getBlocks().insertAfter(
      mlir::Region::iterator(onStackBlock), contBlock);

  // Otherwise, at least some kind of argument could go in these registers.
  builder.setInsertionPointToEnd(maybeRegBlock);

  // Update the gr_offs/vr_offs pointer for next call to va_arg.
  auto regSizeValue = cir::ConstantOp::create(
      builder, loc, cir::IntAttr::get(regOffs.getType(), regSize));
  auto newOffset =
      cir::BinOp::create(builder, loc, regOffs.getType(), cir::BinOpKind::Add,
                         regOffs, regSizeValue);
  builder.createStore(loc, newOffset, regOffsP);

  // Now we're in a position to decide whether this argument really was in
  // registers or not.
  auto inRegs = cir::CmpOp::create(builder, loc, boolTy, cir::CmpOpKind::le,
                                   newOffset, zeroValue);
  cir::BrCondOp::create(builder, loc, inRegs, inRegBlock, onStackBlock);

  //=======================================
  // Argument was in registers
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
                                             castRegTop, regOffs);

  auto resAsVoidP = builder.createBitcast(resAsInt8P, regTop.getType());

  cir::BrOp::create(builder, loc, contBlock, resAsVoidP);

  //=======================================
  // Argument was on the stack
  //=======================================
  builder.setInsertionPointToEnd(onStackBlock);
  auto stackFldTy = getGetMemberResultTy(valist, 0);
  auto stackP = builder.createGetMember(loc, stackFldTy, valist, "stack", 0);

  auto onStackPtr = builder.createLoad(loc, stackP);
  auto ptrDiffTy =
      cir::IntType::get(builder.getContext(), 64, /*isSigned=*/false);

  // All stack slots are multiples of 8 bytes.
  clang::CharUnits stackSlotSize = clang::CharUnits::fromQuantity(8);
  clang::CharUnits stackSize = tySize.alignTo(stackSlotSize);

  auto stackSizeC = cir::ConstantOp::create(
      builder, loc, cir::IntAttr::get(ptrDiffTy, stackSize.getQuantity()));
  auto castStack = builder.createBitcast(onStackPtr, i8PtrTy);
  // Write the new value of __stack for the next call to va_arg.
  auto newStackAsi8Ptr = cir::PtrStrideOp::create(
      builder, loc, castStack.getType(), castStack, stackSizeC);
  auto newStack = builder.createBitcast(newStackAsi8Ptr, onStackPtr.getType());
  builder.createStore(loc, newStack, stackP);

  cir::BrOp::create(builder, loc, contBlock, mlir::Value(onStackPtr));

  // Generate additional instructions for end block.
  builder.setInsertionPoint(op);
  contBlock->addArgument(onStackPtr.getType(), loc);
  auto resP = contBlock->getArgument(0);
  assert(mlir::isa<cir::PointerType>(resP.getType()));
  auto opResPTy = cir::PointerType::get(opResTy);
  auto castResP = builder.createBitcast(resP, opResPTy);
  auto res = builder.createLoad(loc, castResP);
  return res;
}

mlir::Value
LoweringPrepareAArch64CXXABI::lowerVAArg(cir::CIRBaseBuilderTy &builder,
                                         cir::VAArgOp op,
                                         const cir::CIRDataLayout &datalayout) {
  return lowerAAPCSVAArg(builder, op, datalayout);
}
