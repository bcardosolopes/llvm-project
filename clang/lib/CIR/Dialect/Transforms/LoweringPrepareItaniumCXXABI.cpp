//===--------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with
// LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===--------------------------------------------------------------------===//
//
// This file provides Itanium C++ ABI specific code
// that is used during LLVMIR lowering prepare.
//
//===--------------------------------------------------------------------===//

#include "LoweringPrepareItaniumCXXABI.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Value.h"
#include "mlir/IR/ValueRange.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CIR/Dialect/Builder/CIRBaseBuilder.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDataLayout.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/MissingFeatures.h"

cir::LoweringPrepareCXXABI *cir::LoweringPrepareCXXABI::createItaniumABI() {
  return new LoweringPrepareItaniumCXXABI();
}

static void buildBadCastCall(cir::CIRBaseBuilderTy &builder, mlir::Location loc,
                             mlir::FlatSymbolRefAttr badCastFuncRef,
                             bool insideTryRegion,
                             mlir::Region *cleanupToTransfer) {
  auto callOp = builder.createCallOp(loc, badCastFuncRef, cir::VoidType(),
                                     mlir::ValueRange{});
  // When the failed reference dynamic_cast is inside a try block, mark the
  // __cxa_bad_cast() call as an exception-throwing call so FlattenCFG converts
  // it to an invoke with a landing pad routing to the catch handler.
  if (insideTryRegion)
    callOp.setExceptionAttr(mlir::UnitAttr::get(builder.getContext()));
  // CIRGen stashes the destructor bodies of the locals live between the cast
  // and the enclosing try in the cir.dyn_cast op's cleanup region. Move them
  // onto the __cxa_bad_cast call's per-call cleanup region so FlattenCFG runs
  // them on the unwind edge (matching a normal throwing call under a user try).
  if (cleanupToTransfer && !cleanupToTransfer->empty())
    callOp.getCleanup().takeBody(*cleanupToTransfer);
  // __cxa_bad_cast never returns — set noreturn on the call-site attributes
  // so the LLVM lowering propagates it. Matches OGCG's call-site attrs.
  mlir::NamedAttrList badCastAttrs;
  auto noReturn = cir::NoReturnAttr::get(builder.getContext());
  badCastAttrs.set(noReturn.getMnemonic(), noReturn);
  callOp->setAttr("extra_attrs",
                   cir::ExtraFuncAttributesAttr::get(
                       badCastAttrs.getDictionary(builder.getContext())));
  cir::UnreachableOp::create(builder, loc);
  builder.clearInsertionPoint();
}

static mlir::ptr::MemorySpaceAttrInterface
getDefaultGlobalsAddressSpace(mlir::ModuleOp module) {
  if (mlir::DataLayoutSpecInterface spec = module.getDataLayoutSpec()) {
    auto key = spec.getGlobalMemorySpaceIdentifier(module.getContext());
    if (mlir::DataLayoutEntryInterface entry = spec.getSpecForIdentifier(key)) {
      if (auto globalAS = mlir::dyn_cast<mlir::IntegerAttr>(entry.getValue())) {
        unsigned addrSpace = globalAS.getValue().getZExtValue();
        if (addrSpace)
          return cir::TargetAddressSpaceAttr::get(module.getContext(),
                                                  addrSpace);
      }
    }
  }
  return {};
}

static mlir::Value castPointerValue(cir::CIRBaseBuilderTy &builder,
                                    mlir::Location loc, mlir::Value value,
                                    mlir::Type targetType) {
  if (value.getType() == targetType)
    return value;

  auto srcPtrTy = mlir::cast<cir::PointerType>(value.getType());
  auto dstPtrTy = mlir::cast<cir::PointerType>(targetType);
  if (srcPtrTy.getAddrSpace() != dstPtrTy.getAddrSpace()) {
    auto samePointeeDstAS =
        builder.getPointerTo(srcPtrTy.getPointee(), dstPtrTy.getAddrSpace());
    value = builder.createAddrSpaceCast(loc, value, samePointeeDstAS);
    if (value.getType() == targetType)
      return value;
  }

  return builder.createBitcast(loc, value, targetType);
}

static mlir::Value
buildDynamicCastAfterNullCheck(cir::CIRBaseBuilderTy &builder,
                               cir::DynamicCastOp op) {
  mlir::Location loc = op->getLoc();
  mlir::Value srcValue = op.getSrc();
  cir::DynamicCastInfoAttr castInfo = op.getInfo().value();
  auto srcPtrTy = mlir::cast<cir::PointerType>(srcValue.getType());
  auto srcAS = srcPtrTy.getAddrSpace();

  mlir::Value srcPtr =
      builder.createBitcast(srcValue, builder.getVoidPtrTy(srcAS));
  cir::ConstantOp srcRtti = builder.getConstant(loc, castInfo.getSrcRtti());
  cir::ConstantOp destRtti = builder.getConstant(loc, castInfo.getDestRtti());
  cir::ConstantOp offsetHint =
      builder.getConstant(loc, castInfo.getOffsetHint());

  mlir::FlatSymbolRefAttr dynCastFuncRef = castInfo.getRuntimeFunc();

  // The RTTI constants may have typed pointer types (e.g., ptr to
  // __class_type_info struct) that differ from __dynamic_cast's parameter
  // types. When the source explicitly declares __dynamic_cast, it gets typed
  // parameters; when CIR creates it as a runtime function, it uses u8*.
  // Look up the actual function signature and cast the RTTI pointers to match.
  auto module = op->getParentOfType<mlir::ModuleOp>();
  auto dynCastFunc =
      module.lookupSymbol<cir::FuncOp>(dynCastFuncRef.getValue());
  assert(dynCastFunc && "runtime function __dynamic_cast not found");
  auto fnTy = dynCastFunc.getFunctionType();
  mlir::Value srcRttiVal = srcRtti.getResult();
  mlir::Value destRttiVal = destRtti.getResult();
  if (srcPtr.getType() != fnTy.getInput(0))
    srcPtr = castPointerValue(builder, loc, srcPtr, fnTy.getInput(0));
  if (srcRttiVal.getType() != fnTy.getInput(1))
    srcRttiVal = castPointerValue(builder, loc, srcRttiVal, fnTy.getInput(1));
  if (destRttiVal.getType() != fnTy.getInput(2))
    destRttiVal =
        castPointerValue(builder, loc, destRttiVal, fnTy.getInput(2));
  mlir::Value dynCastFuncArgs[4] = {srcPtr, srcRttiVal, destRttiVal,
                                    offsetHint};

  auto dynCastCallOp = builder.createCallOp(loc, dynCastFuncRef,
                                             fnTy.getReturnType(),
                                             dynCastFuncArgs);
  // __dynamic_cast is nounwind — set nothrow on the call-site to match OGCG.
  dynCastCallOp.setNothrow(true);
  mlir::Value castedPtr = dynCastCallOp.getResult();

  assert(mlir::isa<cir::PointerType>(castedPtr.getType()) &&
         "the return value of __dynamic_cast should be a ptr");

  /// C++ [expr.dynamic.cast]p9:
  ///   A failed cast to reference type throws std::bad_cast
  if (op.isRefCast()) {
    bool insideTryRegion = op->getParentOfType<cir::TryOp>() != nullptr;
    // Emit a cir.if that checks the casted value.
    mlir::Value castedValueIsNull = builder.createPtrIsNull(castedPtr);
    mlir::Region *cleanupToTransfer =
        op.getCleanup().empty() ? nullptr : &op.getCleanup();
    cir::IfOp::create(builder, loc, castedValueIsNull, false,
                      [&](mlir::OpBuilder &, mlir::Location) {
                        buildBadCastCall(builder, loc,
                                         castInfo.getBadCastFunc(),
                                         insideTryRegion, cleanupToTransfer);
                      });
  }

  // Note that castedPtr is a void*. Cast it to a pointer to the destination
  // type before return.
  return castPointerValue(builder, loc, castedPtr, op.getType());
}

static mlir::Value
buildDynamicCastToVoidAfterNullCheck(cir::CIRBaseBuilderTy &builder,
                                     clang::ASTContext &astCtx,
                                     cir::DynamicCastOp op) {
  mlir::Location loc = op.getLoc();
  bool vtableUsesRelativeLayout = op.getRelativeLayout();

  // Get the address space from the source pointer to preserve it through
  // intermediate operations.
  auto srcPtrTy = mlir::cast<cir::PointerType>(op.getSrc().getType());
  auto srcAS = srcPtrTy.getAddrSpace();
  auto globalsAS =
      getDefaultGlobalsAddressSpace(op->getParentOfType<mlir::ModuleOp>());

  mlir::Type vtableElemTy;
  uint64_t vtableElemAlign;
  if (vtableUsesRelativeLayout) {
    vtableElemTy = builder.getSIntNTy(32);
    vtableElemAlign = 4;
  } else {
    const auto &targetInfo = astCtx.getTargetInfo();
    auto ptrdiffTy = targetInfo.getPtrDiffType(clang::LangAS::Default);
    bool ptrdiffTyIsSigned = clang::TargetInfo::isTypeSigned(ptrdiffTy);
    uint64_t ptrdiffTyWidth = targetInfo.getTypeWidth(ptrdiffTy);

    vtableElemTy = cir::IntType::get(builder.getContext(), ptrdiffTyWidth,
                                     ptrdiffTyIsSigned);
    vtableElemAlign =
        llvm::divideCeil(targetInfo.getPointerAlign(clang::LangAS::Default), 8);
  }

  // Access vtable to get the offset from the given object to its containing
  // complete object.
  // TODO: Add a specialized operation to get the object offset?
  auto vptrPtrTy =
      cir::PointerType::get(cir::VPtrType::get(builder.getContext()), srcAS);
  auto vptrPtr =
      cir::VTableGetVPtrOp::create(builder, loc, vptrPtrTy, op.getSrc());
  mlir::Value vptr = builder.createLoad(loc, vptrPtr);
  mlir::Value elementPtr =
      builder.createBitcast(vptr, builder.getPointerTo(vtableElemTy, globalsAS));
  mlir::Value minusTwo = builder.getSignedInt(loc, -2, 64);
  auto offsetToTopSlotPtr = cir::PtrStrideOp::create(
      builder, loc, builder.getPointerTo(vtableElemTy, globalsAS), elementPtr,
      minusTwo, cir::GEPNoWrapFlags::inbounds);
  mlir::Value offsetToTop =
      builder.createAlignedLoad(loc, offsetToTopSlotPtr, vtableElemAlign);

  // Add the offset to the given pointer to get the cast result.
  // Cast the input pointer to a uint8_t* to allow pointer arithmetic.
  // Preserve the source address space through these casts.
  cir::PointerType u8PtrTy = builder.getPointerTo(builder.getUIntNTy(8), srcAS);
  mlir::Value srcBytePtr = builder.createBitcast(op.getSrc(), u8PtrTy);
  auto dstBytePtr =
      cir::PtrStrideOp::create(builder, loc, u8PtrTy, srcBytePtr, offsetToTop,
          cir::GEPNoWrapFlags::inbounds);
  // Cast the result to a void*.
  return builder.createBitcast(dstBytePtr, builder.getVoidPtrTy(srcAS));
}

mlir::Value
LoweringPrepareItaniumCXXABI::lowerDynamicCast(cir::CIRBaseBuilderTy &builder,
                                               clang::ASTContext &astCtx,
                                               cir::DynamicCastOp op) {
  mlir::Location loc = op->getLoc();
  mlir::Value srcValue = op.getSrc();

  if (op.isRefCast())
    return buildDynamicCastAfterNullCheck(builder, op);

  mlir::Value srcValueIsNotNull = builder.createPtrToBoolCast(srcValue);
  return cir::TernaryOp::create(
             builder, loc, srcValueIsNotNull,
             [&](mlir::OpBuilder &, mlir::Location) {
               mlir::Value castedValue =
                   op.isCastToVoid()
                       ? buildDynamicCastToVoidAfterNullCheck(builder, astCtx,
                                                              op)
                       : buildDynamicCastAfterNullCheck(builder, op);
               builder.createYield(loc, castedValue);
             },
             [&](mlir::OpBuilder &, mlir::Location) {
               builder.createYield(
                   loc, builder.getNullPtr(op.getType(), loc).getResult());
             })
      .getResult();
}
