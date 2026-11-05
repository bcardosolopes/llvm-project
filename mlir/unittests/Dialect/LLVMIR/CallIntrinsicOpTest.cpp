//===- CallIntrinsicOpTest.cpp - Tests for CallIntrinsicOp ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LLVMTestBase.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Diagnostics.h"

using namespace mlir;
using namespace mlir::LLVM;

// A void return must be modeled as ZERO results, not a single `!llvm.void`
// result (which prints as `-> !llvm.void` and fails to re-parse). The typed
// builder folds a void result type to the no-result form.
TEST_F(LLVMIRTest, CallIntrinsicVoidResultTypeYieldsZeroResults) {
  OpBuilder builder(&context);
  auto op = builder.create<CallIntrinsicOp>(
      builder.getUnknownLoc(), LLVMVoidType::get(&context),
      builder.getStringAttr("llvm.donothing"), ValueRange{});
  EXPECT_EQ(op.getNumResults(), 0u);
  EXPECT_TRUE(succeeded(op.verify()));
  op->erase();
}

// A concrete (non-void) result type is preserved as a single result.
TEST_F(LLVMIRTest, CallIntrinsicConcreteResultTypeYieldsOneResult) {
  OpBuilder builder(&context);
  auto op = builder.create<CallIntrinsicOp>(
      builder.getUnknownLoc(), builder.getI32Type(),
      builder.getStringAttr("llvm.donothing"), ValueRange{});
  EXPECT_EQ(op.getNumResults(), 1u);
  op->erase();
}

// The verifier is the single chokepoint for the "void => zero results"
// invariant: a `!llvm.void` result (e.g. constructed via the TypeRange builder,
// which does not fold) must be rejected regardless of which builder produced it.
TEST_F(LLVMIRTest, CallIntrinsicVerifierRejectsVoidResult) {
  OpBuilder builder(&context);
  auto op = builder.create<CallIntrinsicOp>(
      builder.getUnknownLoc(), TypeRange{LLVMVoidType::get(&context)},
      builder.getStringAttr("llvm.donothing"), ValueRange{},
      FastmathFlagsAttr{});
  EXPECT_EQ(op.getNumResults(), 1u);
  // Swallow the expected op-error diagnostic.
  context.getDiagEngine().registerHandler(
      [](Diagnostic &) { return success(); });
  EXPECT_TRUE(failed(op.verify()));
  op->erase();
}
