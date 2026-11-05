//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../../lib/CIR/CodeGen/CIRGenModule.h"
#include "../../lib/CIR/CodeGen/CIRGenFunction.h"
#include "clang/AST/DeclCXX.h"
#include "clang/Tooling/Tooling.h"
#include "gtest/gtest.h"

#include <type_traits>
#include <utility>

namespace {

const clang::CXXConstructorDecl *findConstructor(const clang::ASTContext &Ctx,
                                                 llvm::StringRef Name) {
  for (const clang::Decl *Decl : Ctx.getTranslationUnitDecl()->decls()) {
    const auto *Record = llvm::dyn_cast<clang::CXXRecordDecl>(Decl);
    if (!Record || !Record->isCompleteDefinition() || !Record->getIdentifier() ||
        Record->getName() != Name)
      continue;
    if (Record->ctor_begin() != Record->ctor_end())
      return *Record->ctor_begin();
  }
  return nullptr;
}

std::unique_ptr<clang::ASTUnit>
buildCtorAST(llvm::StringRef Triple) {
  return clang::tooling::buildASTFromCodeWithArgs(
      "struct S { S(); }; S::S() {}",
      {"-std=c++20", "-target", Triple.str()}, "input.cc");
}

TEST(CIRGenModuleTest, GetDataLayoutReturnsReference) {
  using ModuleRef = const clang::CIRGen::CIRGenModule &;
  using ReturnTy = decltype(std::declval<ModuleRef>().getDataLayout());
  static_assert(std::is_lvalue_reference_v<ReturnTy>,
                "CIRGenModule::getDataLayout must return a reference so the "
                "module data layout can be cached and reused");
  EXPECT_TRUE((std::is_lvalue_reference_v<ReturnTy>));
}

TEST(CIRGenModuleTest, CIRGenFunctionUsesModuleI64AttrCache) {
  using FunctionResultTy = std::invoke_result_t<
      decltype(&clang::CIRGen::CIRGenFunction::getI64AttrCached),
      clang::CIRGen::CIRGenFunction &, int64_t>;
  using ModuleResultTy = std::invoke_result_t<
      decltype(&clang::CIRGen::CIRGenModule::getI64AttrCached),
      clang::CIRGen::CIRGenModule &, int64_t>;

  static_assert(
      std::is_same_v<FunctionResultTy, ModuleResultTy>,
      "CIRGenFunction and CIRGenModule i64 attr cache helpers must stay "
      "type-compatible so function-level lookups can reuse module-level cache");
  EXPECT_TRUE((std::is_same_v<FunctionResultTy, ModuleResultTy>));
}

TEST(CIRGenModuleTest, SymbolLookupCacheUsesStringMap) {
  using SymbolCacheTy = clang::CIRGen::CIRGenModule::SymbolLookupCacheTy;
  static_assert(std::is_same_v<SymbolCacheTy, llvm::StringMap<mlir::Operation *>>,
                "symbolLookupCache should use StringMap to avoid temporary "
                "StringAttr construction in hot getGlobalValue lookups");
  EXPECT_TRUE(
      (std::is_same_v<SymbolCacheTy, llvm::StringMap<mlir::Operation *>>));
}

TEST(CIRGenModuleTest, SymbolLookupCacheSupportsStringRefFind) {
  using SymbolCacheTy = clang::CIRGen::CIRGenModule::SymbolLookupCacheTy;
  using FindResultTy =
      decltype(std::declval<SymbolCacheTy &>().find(llvm::StringRef{}));
  static_assert(std::is_same_v<FindResultTy, SymbolCacheTy::iterator>,
                "StringMap-backed symbol cache must support StringRef key "
                "lookup so CIRGenModule::getGlobalValue can avoid constructing "
                "temporary StringAttr values in hot paths");
  EXPECT_TRUE((std::is_same_v<FindResultTy, SymbolCacheTy::iterator>));
}

TEST(CIRGenModuleTest, CanonicalizeCtorBaseForTargetsWithoutVariants) {
  auto AST = buildCtorAST("x86_64-pc-windows-msvc");
  ASSERT_TRUE(AST);

  const clang::ASTContext &Ctx = AST->getASTContext();
  ASSERT_FALSE(Ctx.getTargetInfo().getCXXABI().hasConstructorVariants());

  const auto *Ctor = findConstructor(Ctx, "S");
  ASSERT_NE(Ctor, nullptr);

  clang::GlobalDecl Canonical =
      clang::CIRGen::CIRGenModule::getCanonicalDeclForMangledName(
          clang::GlobalDecl(Ctor, clang::Ctor_Base), Ctx.getTargetInfo());

  EXPECT_EQ(Canonical.getDecl(), Ctor->getCanonicalDecl());
  EXPECT_EQ(Canonical.getCtorType(), clang::Ctor_Complete);
}

TEST(CIRGenModuleTest, PreserveCtorBaseForTargetsWithVariants) {
  auto AST = buildCtorAST("x86_64-unknown-linux-gnu");
  ASSERT_TRUE(AST);

  const clang::ASTContext &Ctx = AST->getASTContext();
  ASSERT_TRUE(Ctx.getTargetInfo().getCXXABI().hasConstructorVariants());

  const auto *Ctor = findConstructor(Ctx, "S");
  ASSERT_NE(Ctor, nullptr);

  clang::GlobalDecl Canonical =
      clang::CIRGen::CIRGenModule::getCanonicalDeclForMangledName(
          clang::GlobalDecl(Ctor, clang::Ctor_Base), Ctx.getTargetInfo());

  EXPECT_EQ(Canonical.getDecl(), Ctor->getCanonicalDecl());
  EXPECT_EQ(Canonical.getCtorType(), clang::Ctor_Base);
}

} // namespace
