//===--- UseDoExprCheck.h - clang-tidy --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MODERNIZE_USEDOEXPRCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MODERNIZE_USEDOEXPRCHECK_H

#include "../ClangTidyCheck.h"
#include "llvm/ADT/DenseSet.h"

namespace clang::tidy::modernize {

/// Finds immediately-invoked lambda expressions and classifies each one by
/// whether -- and why -- it could be spelled as a `do` expression instead.
///
/// The check only reports; it never rewrites. Every immediately-invoked lambda
/// lands in exactly one category, refusals included, so that the diagnostics
/// can be aggregated into a census.
///
/// For the user-facing documentation see:
/// http://clang.llvm.org/extra/clang-tidy/checks/modernize/use-do-expr.html
class UseDoExprCheck : public ClangTidyCheck {
public:
  UseDoExprCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context) {}
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus11;
  }
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onEndOfTranslationUnit() override { ReportedLambdas.clear(); }

private:
  /// Classifies and diagnoses one call, if it invokes a lambda written at the
  /// call site. \p ForceUnevaluated short-circuits the context walk for calls
  /// dug out of a place that is unevaluated by construction.
  void report(const CallExpr *Call, ASTContext &Ctx, const SourceManager &SM,
              bool ForceUnevaluated);

  /// Attaches the fixits that turn \p Call into a `do` expression. Returns
  /// false, having attached nothing, if any part of the rewrite cannot be
  /// spelled from the available source locations.
  bool buildRewrite(const CallExpr *Call, const LambdaExpr *Lambda,
                    ArrayRef<const ReturnStmt *> Returns, ASTContext &Ctx,
                    const SourceManager &SM, DiagnosticBuilder &Diag);

  /// Spelling locations of the lambdas already reported in this translation
  /// unit. A lambda in a template is matched once per instantiation and once
  /// for the pattern; without this every count would be inflated by however
  /// many times the enclosing template happens to be used.
  llvm::DenseSet<unsigned> ReportedLambdas;
};

} // namespace clang::tidy::modernize

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_MODERNIZE_USEDOEXPRCHECK_H
