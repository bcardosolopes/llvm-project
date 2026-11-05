//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for CIR ConstPtrAttr, verifying that the int64_t builder
// correctly creates null and non-null pointer attributes.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/MLIRContext.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "gtest/gtest.h"

using namespace mlir;
using namespace cir;

class ConstPtrAttrTest : public ::testing::Test {
protected:
  ConstPtrAttrTest() { context.loadDialect<cir::CIRDialect>(); }

  MLIRContext context;
};

// Verify that ConstPtrAttr::get(type, int64_t(0)) creates a valid null
// pointer attribute. Previously, passing a raw 0 would implicitly convert
// to a null IntegerAttr, causing a crash in isNullValue().
TEST_F(ConstPtrAttrTest, Int64BuilderNullPointer) {
  auto voidTy = cir::VoidType::get(&context);
  auto ptrTy = cir::PointerType::get(voidTy);

  auto attr = cir::ConstPtrAttr::get(ptrTy, int64_t(0));
  ASSERT_TRUE(attr);
  EXPECT_TRUE(attr.isNullValue());
  EXPECT_EQ(attr.getType(), ptrTy);
}

// Verify that ConstPtrAttr::get(type, int64_t(N)) for non-zero values
// creates a valid non-null pointer attribute.
TEST_F(ConstPtrAttrTest, Int64BuilderNonNullPointer) {
  auto voidTy = cir::VoidType::get(&context);
  auto ptrTy = cir::PointerType::get(voidTy);

  auto attr = cir::ConstPtrAttr::get(ptrTy, int64_t(0xDEAD));
  ASSERT_TRUE(attr);
  EXPECT_FALSE(attr.isNullValue());
  EXPECT_EQ(attr.getType(), ptrTy);
}

// Verify that the IntegerAttr builder still works correctly.
TEST_F(ConstPtrAttrTest, IntegerAttrBuilder) {
  auto voidTy = cir::VoidType::get(&context);
  auto ptrTy = cir::PointerType::get(voidTy);

  auto intAttr =
      IntegerAttr::get(IntegerType::get(&context, 64), 0);
  auto attr = cir::ConstPtrAttr::get(ptrTy, intAttr);
  ASSERT_TRUE(attr);
  EXPECT_TRUE(attr.isNullValue());
}
