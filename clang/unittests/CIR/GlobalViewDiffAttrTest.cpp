//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for GlobalViewDiffAttr's verifier.
//
// The verifier guards a state that cannot be reached from text. The assembly
// format keeps the operands' types alive -- without it they were dropped and
// re-parsed as null, and a null type is what MLIR's alias walk and bytecode
// numbering dereference -- but get() can always be handed a null directly, and
// printing the result would crash rather than diagnose.
//
// So the checks live in C++ and are tested in C++. Only the untyped-operand
// case is also reachable from text; see
// clang/test/CIR/IR/invalid-global-view-diff.cir.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/MLIRContext.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "gtest/gtest.h"

using namespace mlir;
using namespace cir;

namespace {

class GlobalViewDiffAttrTest : public ::testing::Test {
protected:
  GlobalViewDiffAttrTest() {
    context.loadDialect<cir::CIRDialect>();
    // getChecked reports through the diagnostic engine rather than returning
    // the message, so it is captured here.
    context.getDiagEngine().registerHandler([this](Diagnostic &d) {
      lastError = d.str();
      return success();
    });
  }

  // A global_view with a real pointer type, i.e. a well-formed operand.
  GlobalViewAttr typedView() {
    auto u8 = cir::IntType::get(&context, 8, /*isSigned=*/false);
    return GlobalViewAttr::get(cir::PointerType::get(u8),
                               FlatSymbolRefAttr::get(&context, "g"));
  }

  // The same, with no type -- what a round trip used to produce before the
  // operands started carrying their types.
  GlobalViewAttr untypedView() {
    return GlobalViewAttr::get(&context, /*type=*/{},
                               FlatSymbolRefAttr::get(&context, "g"), {}, {},
                               {}, {}, /*noCFI=*/false, /*inbounds=*/false);
  }

  Type diffTy() { return cir::IntType::get(&context, 32, /*isSigned=*/true); }

  std::string lastError;
  MLIRContext context;
};

// The well-formed case, so the negatives below are known to be rejected for
// their own reason rather than because everything is.
TEST_F(GlobalViewDiffAttrTest, TypedOperandsAccepted) {
  auto attr = GlobalViewDiffAttr::getChecked(
      [&] { return emitError(UnknownLoc::get(&context)); }, &context, diffTy(),
      typedView(), typedView());
  EXPECT_TRUE(attr);
}

// A null operand. Checked before the operands are asked for their types,
// because getType() on a null attribute dereferences -- the verifier would
// otherwise crash on exactly the input it is meant to reject.
TEST_F(GlobalViewDiffAttrTest, NullLhsRejected) {
  auto attr = GlobalViewDiffAttr::getChecked(
      [&] { return emitError(UnknownLoc::get(&context)); }, &context, diffTy(),
      GlobalViewAttr(), typedView());
  EXPECT_FALSE(attr);
  EXPECT_NE(lastError.find("requires both operands"), std::string::npos);
}

TEST_F(GlobalViewDiffAttrTest, NullRhsRejected) {
  auto attr = GlobalViewDiffAttr::getChecked(
      [&] { return emitError(UnknownLoc::get(&context)); }, &context, diffTy(),
      typedView(), GlobalViewAttr());
  EXPECT_FALSE(attr);
  EXPECT_NE(lastError.find("requires both operands"), std::string::npos);
}

// A null self type. Not reachable from text either: written without a trailing
// `: <type>` the attribute simply parses and prints back the same way.
TEST_F(GlobalViewDiffAttrTest, NullTypeRejected) {
  auto attr = GlobalViewDiffAttr::getChecked(
      [&] { return emitError(UnknownLoc::get(&context)); }, &context,
      /*type=*/{}, typedView(), typedView());
  EXPECT_FALSE(attr);
  EXPECT_NE(lastError.find("requires a type"), std::string::npos);
}

// An operand with no type of its own. This one is also reachable from text.
TEST_F(GlobalViewDiffAttrTest, UntypedLhsRejected) {
  auto attr = GlobalViewDiffAttr::getChecked(
      [&] { return emitError(UnknownLoc::get(&context)); }, &context, diffTy(),
      untypedView(), typedView());
  EXPECT_FALSE(attr);
  EXPECT_NE(lastError.find("operands must each carry a type"),
            std::string::npos);
}

TEST_F(GlobalViewDiffAttrTest, UntypedRhsRejected) {
  auto attr = GlobalViewDiffAttr::getChecked(
      [&] { return emitError(UnknownLoc::get(&context)); }, &context, diffTy(),
      typedView(), untypedView());
  EXPECT_FALSE(attr);
  EXPECT_NE(lastError.find("operands must each carry a type"),
            std::string::npos);
}

} // namespace
