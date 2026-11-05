//===- LLVMTypeTest.cpp - Tests for LLVM types ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LLVMTestBase.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/IR/AttrTypeSubElements.h"

#include <atomic>
#include <string>
#include <thread>

using namespace mlir;
using namespace mlir::LLVM;

TEST_F(LLVMIRTest, IsStructTypeMutable) {
  auto structTy = LLVMStructType::getIdentified(&context, "foo");
  ASSERT_TRUE(bool(structTy));
  ASSERT_TRUE(structTy.hasTrait<TypeTrait::IsMutable>());
}

TEST_F(LLVMIRTest, FrozenStructUniquing) {
  Type i32 = IntegerType::get(&context, 32);
  Type i64 = IntegerType::get(&context, 64);
  auto frozen = LLVMStructType::getFrozenIdentified(&context, "foo", {i32});
  EXPECT_EQ(frozen,
            LLVMStructType::getFrozenIdentified(&context, "foo", {i32}));
  EXPECT_NE(frozen,
            LLVMStructType::getFrozenIdentified(&context, "foo", {i64}));
  EXPECT_NE(frozen, LLVMStructType::getFrozenIdentified(&context, "foo", {i32},
                                                        /*isPacked=*/true));

  auto frozenOpaque = LLVMStructType::getFrozenOpaque(&context, "foo");
  EXPECT_TRUE(frozenOpaque.isOpaque());
  EXPECT_NE(frozenOpaque, frozen);
  EXPECT_NE(frozenOpaque, LLVMStructType::getFrozenIdentified(&context, "foo",
                                                              {}));

  // The C++ API keeps uniquing by name only, apart from frozen structs.
  auto identified = LLVMStructType::getIdentified(&context, "foo");
  EXPECT_EQ(identified, LLVMStructType::getOpaque("foo", &context));
  EXPECT_NE(identified, frozen);
  EXPECT_NE(identified, frozenOpaque);
}

TEST_F(LLVMIRTest, FrozenStructBodyIsFixed) {
  Type i32 = IntegerType::get(&context, 32);
  Type i64 = IntegerType::get(&context, 64);
  auto frozen = LLVMStructType::getFrozenIdentified(&context, "foo", {i32});
  EXPECT_TRUE(frozen.isInitialized());
  EXPECT_FALSE(frozen.isOpaque());
  EXPECT_EQ(frozen.getBody(), ArrayRef<Type>{i32});
  EXPECT_TRUE(succeeded(frozen.setBody({i32}, /*isPacked=*/false)));
  EXPECT_TRUE(failed(frozen.setBody({i64}, /*isPacked=*/false)));
  EXPECT_TRUE(failed(LLVMStructType::getFrozenOpaque(&context, "foo")
                         .setBody({i32}, /*isPacked=*/false)));
}

TEST_F(LLVMIRTest, FrozenStructSubElementReplacement) {
  Type i32 = IntegerType::get(&context, 32);
  Type i64 = IntegerType::get(&context, 64);
  auto frozen = LLVMStructType::getFrozenIdentified(&context, "foo", {i32});
  auto literal = LLVMStructType::getLiteral(&context, {frozen});
  AttrTypeReplacer replacer;
  replacer.addReplacement([&](IntegerType type) -> Type {
    return type == i32 ? i64 : type;
  });
  Type replaced = replacer.replace(literal);
  auto expected = LLVMStructType::getLiteral(
      &context, {LLVMStructType::getFrozenIdentified(&context, "foo", {i64})});
  EXPECT_EQ(replaced, expected);
}

TEST_F(LLVMIRTest, StructBodyIsPublishedWithInitializedBit) {
  Type i32 = IntegerType::get(&context, 32);
  Type i64 = IntegerType::get(&context, 64);
  SmallVector<Type> body = {i32, i64, i32, i64};
  for (int round = 0; round < 100; ++round) {
    auto type = LLVMStructType::getIdentified(&context,
                                              "foo" + std::to_string(round));
    std::atomic<bool> start = false;
    std::atomic<bool> sawBadBody = false;
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
      readers.emplace_back([&] {
        while (!start)
          ;
        for (int j = 0; j < 1000; ++j) {
          bool initialized = type.isInitialized();
          ArrayRef<Type> seen = type.getBody();
          if (initialized && seen != ArrayRef<Type>(body))
            sawBadBody = true;
        }
      });
    }
    start = true;
    EXPECT_TRUE(succeeded(type.setBody(body, /*isPacked=*/false)));
    for (std::thread &reader : readers)
      reader.join();
    EXPECT_FALSE(sawBadBody);
  }
}
