//===- AttrTypeWalkerTest.cpp - AttrTypeWalker tests ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/AttrTypeSubElements.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/TypeRange.h"
#include "llvm/ADT/SmallVector.h"
#include "gtest/gtest.h"

using namespace mlir;

TEST(AttrTypeWalkerTest, WalksNestedTypeSuccessfully) {
  MLIRContext ctx;
  Type i32 = IntegerType::get(&ctx, 32);

  SmallVector<Type> innerTypes{i32};
  Type innerTuple = TupleType::get(&ctx, TypeRange(innerTypes));
  SmallVector<Type> outerTypes{i32, innerTuple};
  Type nested = TupleType::get(&ctx, TypeRange(outerTypes));

  AttrTypeWalker walker;
  int visits = 0;
  walker.addWalk([&](Type) {
    ++visits;
    return WalkResult::advance();
  });

  EXPECT_FALSE(walker.walk(nested).wasInterrupted());
  EXPECT_GE(visits, 2);
}
