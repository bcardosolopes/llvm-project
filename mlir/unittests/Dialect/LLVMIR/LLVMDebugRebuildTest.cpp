//===- LLVMDebugRebuildTest.cpp - Tests for nameless debug attr rebuild --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LLVMTestBase.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/AttrTypeSubElements.h"
#include "llvm/BinaryFormat/Dwarf.h"

using namespace mlir;
using namespace mlir::LLVM;

TEST_F(LLVMIRTest, NamelessLocalVariableRebuildKeepsNullName) {
  DIFileAttr file = DIFileAttr::get(&context, "test.c", "/src");
  DIFileAttr newFile = DIFileAttr::get(&context, "other.c", "/src");
  DIBasicTypeAttr intTy = DIBasicTypeAttr::get(
      &context, llvm::dwarf::DW_TAG_base_type, "int", 32,
      llvm::dwarf::DW_ATE_signed);
  DILocalVariableAttr var = DILocalVariableAttr::get(
      &context, file, StringAttr(), file, /*line=*/1, /*arg=*/1,
      /*alignInBits=*/0, intTy, DIFlags::Zero);
  ASSERT_TRUE(var);
  EXPECT_FALSE(var.getName());

  AttrTypeReplacer replacer;
  replacer.addReplacement([&](DIFileAttr attr) -> std::optional<Attribute> {
    if (attr == file)
      return Attribute(newFile);
    return std::nullopt;
  });
  auto newVar = cast<DILocalVariableAttr>(replacer.replace(var));
  EXPECT_FALSE(newVar.getName());
  EXPECT_EQ(newVar.getFile(), newFile);
  EXPECT_EQ(newVar.getLine(), 1u);
}

TEST_F(LLVMIRTest, NamelessLabelRebuildKeepsNullName) {
  DIFileAttr file = DIFileAttr::get(&context, "test.c", "/src");
  DIFileAttr newFile = DIFileAttr::get(&context, "other.c", "/src");
  DILabelAttr label =
      DILabelAttr::get(&context, file, StringAttr(), file, /*line=*/7);
  ASSERT_TRUE(label);
  EXPECT_FALSE(label.getName());

  AttrTypeReplacer replacer;
  replacer.addReplacement([&](DIFileAttr attr) -> std::optional<Attribute> {
    if (attr == file)
      return Attribute(newFile);
    return std::nullopt;
  });
  auto newLabel = cast<DILabelAttr>(replacer.replace(label));
  EXPECT_FALSE(newLabel.getName());
  EXPECT_EQ(newLabel.getFile(), newFile);
  EXPECT_EQ(newLabel.getLine(), 7u);
}
