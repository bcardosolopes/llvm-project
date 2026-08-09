//===--- SemaDoExpr.cpp - Semantic Analysis for do-expressions ----------*-===//
//
// Copyright 2026 Jump Trading, LLC
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Implements semantic analysis for do-expressions:
//  `do [init-capture-seq[opt]] -> Type { ... }`, with `do_return` yielding
//  the value. Each `[name = init]` capture introduces a `decltype((init))`
//  local (the value category of the initializer is preserved).
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Analysis/Analyses/ReachableCode.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/BitVector.h"

using namespace clang;

static bool doExprHasDeducedResultType(const Sema::DoExprStackEntry &Entry) {
  return !Entry.TypeIsExplicit || Entry.ExplicitType->getContainedAutoType();
}

static bool isParsingExpansionStmtPattern(Sema &S) {
  if (S.isSynthesizingExpansionStmt())
    return false;

  for (DeclContext *DC = S.CurContext; DC; DC = DC->getParent())
    if (isa<ExpansionStmtDecl>(DC))
      return true;
  return false;
}

static VarDecl *findFirstVarDecl(Stmt *S) {
  if (!S)
    return nullptr;

  if (auto *DS = dyn_cast<DeclStmt>(S)) {
    for (Decl *D : DS->decls())
      if (auto *VD = dyn_cast<VarDecl>(D))
        return VD;
    return nullptr;
  }

  for (Stmt *Sub : S->children())
    if (auto *VD = findFirstVarDecl(Sub))
      return VD;

  return nullptr;
}

enum class ConstevalIfKind {
  AlwaysConstantEvaluated,
  NeverConstantEvaluated,
  PotentiallyConstantEvaluated,
};

static ConstevalIfKind getConstevalIfKind(Sema &S) {
  if (S.isConstantEvaluatedContext() ||
      S.currentEvaluationContext().isImmediateFunctionContext())
    return ConstevalIfKind::AlwaysConstantEvaluated;

  if (const auto *FD = dyn_cast<FunctionDecl>(S.CurContext)) {
    if (FD->isConsteval())
      return ConstevalIfKind::AlwaysConstantEvaluated;
    if (FD->isConstexpr())
      return ConstevalIfKind::PotentiallyConstantEvaluated;
  }

  return ConstevalIfKind::NeverConstantEvaluated;
}

static CFG::BuildOptions::ConstevalConditionKind
toConstevalConditionKind(ConstevalIfKind Kind) {
  switch (Kind) {
  case ConstevalIfKind::AlwaysConstantEvaluated:
    return CFG::BuildOptions::CCK_AlwaysConstant;
  case ConstevalIfKind::NeverConstantEvaluated:
    return CFG::BuildOptions::CCK_NeverConstant;
  case ConstevalIfKind::PotentiallyConstantEvaluated:
    return CFG::BuildOptions::CCK_Unknown;
  }
  llvm_unreachable("unhandled ConstevalIfKind");
}

/// Return true if control may fall off the end of a do-expression body \p Body
/// (rather than always exiting via `do_return`, an outer-scope
/// `return`/`break`/`continue`/`goto`, a `throw`, or a call to a `[[noreturn]]`
/// function).
///
/// Reachability is computed from a CFG built in do-expression-body mode (so
/// escaping `break`/`continue` and `do_return` are modeled as edges to the
/// exit block, and nested do-expressions are opaque). This reuses the same
/// graph construction that powers -Wreturn-type, so switch coverage, `&&`/`||`,
/// `?:`, constant loop conditions, and `if constexpr`/`if consteval` pruning
/// are all handled accurately.
static bool doExprBodyMayFallThrough(Sema &S, CompoundStmt *Body,
                                     ConstevalIfKind ConstevalKind) {
  CFG::BuildOptions Options;
  Options.DoExpressionBody = true;
  Options.PruneTriviallyFalseEdges = true;
  Options.AddEHEdges = false;
  Options.ConstevalCondition = toConstevalConditionKind(ConstevalKind);

  std::unique_ptr<CFG> Cfg =
      CFG::buildCFG(/*D=*/nullptr, Body, &S.Context, Options);
  if (!Cfg)
    // If we can't build a CFG, conservatively assume control may fall off the
    // end: a spurious diagnostic is safer than silently accepting a body that
    // never yields a value.
    return true;

  // Mark blocks reachable from entry so dead code doesn't create phantom
  // fall-through edges.
  llvm::BitVector Live(Cfg->getNumBlockIDs());
  reachable_code::ScanReachableFromBlock(&Cfg->getEntry(), Live);

  // A do-expression "falls off the end" if a live predecessor of the exit block
  // reaches it via a plain edge — i.e. not through `do_return`, an outer
  // `return`/`break`/`continue`, a `throw`, or a [[noreturn]] call. Edges from
  // pruned (unreachable) branches appear as null predecessors and are dropped
  // by IgnoreNullPredecessors.
  CFGBlock::FilterOptions FO;
  FO.IgnoreDefaultsWithCoveredEnums = 1;

  for (CFGBlock::filtered_pred_iterator I =
           Cfg->getExit().filtered_pred_start_end(FO);
       I.hasMore(); ++I) {
    const CFGBlock &B = **I;
    if (!Live[B.getBlockID()])
      continue;

    // A call that doesn't return doesn't really reach the exit.
    if (B.hasNoReturnElement())
      continue;

    // try/catch boundaries are abnormal edges, not fall-through. (A `break` or
    // `continue` that leaves the do-expression is routed to a separate sink, so
    // a `break`/`continue` terminator that reaches the exit here is an
    // inner-loop break that exits its loop and then falls off the end — a
    // genuine fall-through, handled by the generic logic below.)
    if (const Stmt *Term = B.getTerminatorStmt()) {
      if (isa<CXXTryStmt>(Term))
        continue;
    }

    // Find the last real statement in the block, skipping implicit destructors.
    CFGBlock::const_reverse_iterator RI = B.rbegin(), RE = B.rend();
    for (; RI != RE; ++RI)
      if (RI->getAs<CFGStmt>())
        break;

    if (RI == RE)
      // Empty block reaching exit (e.g. a labeled empty statement): a plain
      // fall-through edge.
      return true;

    const Stmt *Last = RI->castAs<CFGStmt>().getStmt();
    // `do_return`/`return` leave via the do-expression value or the enclosing
    // function; `throw` leaves via an exception.
    if (isa<DoReturnStmt, ReturnStmt, CoreturnStmt>(Last))
      continue;
    if (isa<CXXThrowExpr>(Last))
      continue;

    // An edge to exit that is none of the above is a real fall-through.
    if (!llvm::is_contained(B.succs(), &Cfg->getExit()))
      continue; // abnormal edge, not fall-through
    return true;
  }

  return false;
}

void Sema::ActOnStartDoExpr(SourceLocation DoLoc, QualType ExplicitType,
                            unsigned TemplateDepth) {
  // The body forms a protected target for branch diagnostics: jumps may leave
  // it, but may not enter it from outside.
  setFunctionHasBranchProtectedScope();

  DoExprStackEntry Entry;
  Entry.DoLoc = DoLoc;
  Entry.TypeIsExplicit = !ExplicitType.isNull();
  Entry.ExplicitType = ExplicitType;
  Entry.ExplicitTypeInfo = nullptr;
  Entry.DeducedType = QualType();
  Entry.HasDependentDoReturn = false;
  Entry.TemplateDepth =
      TemplateDepth == ~0U ? getTemplateDepth(getCurScope()) : TemplateDepth;
  // PushCompoundScope (called by ParseCompoundStatementBody) asserts that
  // there is a current function scope. At namespace scope (e.g. a constexpr
  // variable initializer) there is none, so push a synthetic one and remember
  // to pop it on close. This synthetic scope only matters for the few Sema
  // checks that consult the function-scope info during body parsing; the
  // body's `return`/`break`/`continue` lookup is driven by the parser's
  // Scope tree, which we deliberately do not augment.
  if (FunctionScopes.empty()) {
    QualType FnTy = Context.getFunctionNoProtoType(Context.VoidTy);
    auto *SyntheticFD =
        FunctionDecl::Create(Context, CurContext, DoLoc, DoLoc,
                             DeclarationName(), FnTy, /*TInfo=*/nullptr,
                             SC_None);
    SyntheticFD->setImplicit();
    Entry.SavedContext = CurContext;
    CurContext = SyntheticFD;
    PushFunctionScope();
    Entry.PushedSyntheticFunctionScope = true;
  }
  Entry.FunctionScopeDepth = FunctionScopes.size();
  DoExprStack.push_back(Entry);
}

void Sema::ActOnDoExprExplicitType(TypeSourceInfo *ExplicitType) {
  assert(!DoExprStack.empty() &&
         "ActOnDoExprExplicitType without ActOnStartDoExpr");
  DoExprStackEntry &Entry = DoExprStack.back();
  Entry.TypeIsExplicit = ExplicitType;
  Entry.ExplicitType = ExplicitType ? ExplicitType->getType() : QualType();
  Entry.ExplicitTypeInfo = ExplicitType;
}

DeclResult Sema::ActOnDoExprInitCapture(Scope *S, IdentifierInfo *Id,
                                        SourceLocation IdLoc,
                                        SourceLocation EqLoc, Expr *Init) {
  assert(Init && "do-expression init-capture without an initializer");

  // `[name = init]` has `decltype((auto))` semantics: the capture's type is
  // `decltype((init))`, i.e. the value category of the initializer is preserved
  // (lvalue -> T&, xvalue -> T&&, prvalue -> T), exactly as if the initializer
  // were a parenthesized expression. ASTContext::getReferenceQualifiedType
  // implements that rule directly for non-dependent initializers; for dependent
  // ones we form a `decltype` type over the parenthesized initializer so the
  // same rule is re-applied at instantiation.
  QualType T;
  if (Init->isTypeDependent() || Init->isValueDependent()) {
    Expr *Paren =
        new (Context) ParenExpr(Init->getBeginLoc(), Init->getEndLoc(), Init);
    T = BuildDecltypeType(Paren, /*AsUnevaluated=*/false);
  } else {
    T = Context.getReferenceQualifiedType(Init);
  }

  TypeSourceInfo *TSI = Context.getTrivialTypeSourceInfo(T, IdLoc);
  VarDecl *VD =
      VarDecl::Create(Context, CurContext, IdLoc, IdLoc, Id, T, TSI, SC_None);
  // Mark this as a do-expression init-capture. Its lifetime extends to the end
  // of the enclosing full-expression, so the lifetime analysis must not treat a
  // reference into it like a reference into an ordinary block-scoped local.
  VD->setDoExprInitCapture(true);

  // Register the variable before initializing it (and before the next capture is
  // parsed) so later captures and the body can name it.
  PushOnScopeChains(VD, S);

  AddInitializerToDecl(VD, Init, /*DirectInit=*/false);
  FinalizeDeclaration(VD);

  if (VD->isInvalidDecl())
    return DeclResult(true);
  return VD;
}

StmtResult Sema::ActOnDoExprInitStmt(SourceLocation LParenLoc,
                                     SourceLocation RParenLoc,
                                     ArrayRef<Stmt *> InitStmts,
                                     ArrayRef<MaterializeTemporaryExpr *>
                                         LifetimeExtendTemps) {
  if (InitStmts.empty())
    return StmtEmpty();

  Stmt *InitStmt = CompoundStmt::Create(Context, InitStmts, FPOptionsOverride(),
                                        LParenLoc, RParenLoc);
  ActOnDoExprInitStmt(InitStmt, LifetimeExtendTemps);
  return InitStmt;
}

void Sema::ActOnDoExprInitStmt(
    Stmt *InitStmt, ArrayRef<MaterializeTemporaryExpr *> LifetimeExtendTemps) {
  if (InitStmt)
    setFunctionHasBranchProtectedScope();

  if (!LifetimeExtendTemps.empty()) {
    if (VarDecl *ExtendingVar = findFirstVarDecl(InitStmt)) {
      InitializedEntity Entity =
          InitializedEntity::InitializeVariable(ExtendingVar);
      for (auto *MTE : LifetimeExtendTemps)
        MTE->setExtendingDecl(ExtendingVar, Entity.allocateManglingNumber());
    }
  }

  if (InitStmt || !LifetimeExtendTemps.empty())
    Cleanup.setExprNeedsCleanups(true);
}

void Sema::ActOnDoExprError() {
  if (DoExprStack.empty())
    return;
  bool PoppedSynthetic = DoExprStack.back().PushedSyntheticFunctionScope;
  DeclContext *SavedContext = DoExprStack.back().SavedContext;
  DoExprStack.pop_back();
  if (PoppedSynthetic)
    PopFunctionScopeInfo();
  if (SavedContext)
    CurContext = SavedContext;
}

ExprResult Sema::ActOnDoExpr(SourceLocation DoLoc, SourceLocation LBraceLoc,
                             Stmt *InitStmt, Stmt *Body,
                             SourceLocation RBraceLoc,
                             TypeSourceInfo *ExplicitType) {
  return BuildDoExpr(DoLoc, LBraceLoc, InitStmt, Body, RBraceLoc, ExplicitType,
                     getTemplateDepth(getCurScope()));
}

ExprResult Sema::BuildDoExpr(SourceLocation DoLoc, SourceLocation LBraceLoc,
                             Stmt *InitStmt, Stmt *Body,
                             SourceLocation RBraceLoc,
                             TypeSourceInfo *ExplicitType,
                             unsigned TemplateDepth) {
  assert(!DoExprStack.empty() && "BuildDoExpr without ActOnStartDoExpr");
  DoExprStackEntry Entry = DoExprStack.pop_back_val();
  if (Entry.PushedSyntheticFunctionScope)
    PopFunctionScopeInfo();
  if (Entry.SavedContext)
    CurContext = Entry.SavedContext;

  auto *Compound = cast<CompoundStmt>(Body);

  // A do_return already produced an unrecoverable deduction error; don't
  // pile on with a misleading "no do_return statements" diagnostic.
  if (Entry.DeductionFailed)
    return ExprError();

  QualType ResultType;
  if (Entry.TypeIsExplicit &&
      !Entry.ExplicitType->getContainedAutoType()) {
    ResultType = Entry.ExplicitType;
  } else if (!Entry.DeducedType.isNull()) {
    ResultType = Entry.DeducedType;
  } else if (TemplateDepth > 0 || Entry.HasDependentDoReturn) {
    // Inside a template, defer: no do_return yet seen with a non-dependent
    // operand. Use a dependent placeholder.
    ResultType = Context.DependentTy;
  } else if (Entry.TypeIsExplicit &&
             Entry.ExplicitType->getContainedAutoType()) {
    // No do_return statements: deduce against void, as if the body ended in
    // `do_return;` (mirrors the no-operand case in BuildDoReturnStmt). As
    // with functions, the type as written must be 'auto' or
    // 'decltype(auto)', possibly cv-qualified or constrained: 'auto*' and
    // the like cannot deduce from void.
    if (!Entry.ExplicitType->getAs<AutoType>()) {
      Diag(DoLoc, diag::err_do_expr_no_return_but_not_auto)
          << Entry.ExplicitType;
      return ExprError();
    }
    CXXScalarValueInitExpr VoidVal(Context.VoidTy, nullptr, DoLoc);
    sema::TemplateDeductionInfo Info(DoLoc);
    TemplateDeductionResult Res =
        DeduceAutoType(Entry.ExplicitTypeInfo->getTypeLoc(), &VoidVal,
                       Entry.DeducedType, Info);
    if (Res != TemplateDeductionResult::Success)
      return ExprError();
    ResultType = Entry.DeducedType;
  } else {
    // No do_return statements and no explicit type: the result is void, like
    // a function with a deduced return type and no return statements.
    ResultType = Context.VoidTy;
  }

  // Reachability: a do-expression with a non-void result type is ill-formed
  // if control can fall off the end of the body without exiting via
  // `do_return`, an outer-scope return/break/continue, a throw, or a
  // [[noreturn]] call. (Void do-expressions are like void functions:
  // falling off the end is fine.) Skip the check for dependent types so the
  // analysis runs on each instantiation rather than on the template.
  if (!ResultType->isVoidType() && !ResultType->isDependentType() &&
      doExprBodyMayFallThrough(*this, Compound, getConstevalIfKind(*this))) {
    Diag(RBraceLoc, diag::err_do_expr_falls_off_end) << ResultType;
    return ExprError();
  }

  // Expressions can't have reference type. If the user wrote
  // `do -> T &` / `T &&`, the do-expression itself is a glvalue of T:
  // strip the reference for the stored type and pick the right value
  // category.
  ExprValueKind VK = VK_PRValue;
  if (const auto *Ref = ResultType->getAs<ReferenceType>()) {
    VK = isa<RValueReferenceType>(Ref) ? VK_XValue : VK_LValue;
    ResultType = Ref->getPointeeType();
  } else {
    // A prvalue of non-class, non-array type never has cv-qualified type
    // ([expr.type]p2) — e.g. `do -> const auto { do_return 42; }` is a
    // prvalue of type 'int', just like calling a function returning
    // 'const int'.
    ResultType = ResultType.getNonLValueExprType(Context);
  }

  if (InitStmt)
    Cleanup.setExprNeedsCleanups(true);

  return new (Context) DoExpr(InitStmt, Compound, ResultType, VK, ExplicitType,
                              DoLoc, LBraceLoc, RBraceLoc, TemplateDepth);
}

StmtResult Sema::ActOnDoReturnStmt(SourceLocation DoReturnLoc, Expr *Operand,
                                   Scope *CurScope) {
  return BuildDoReturnStmt(DoReturnLoc, Operand);
}

StmtResult Sema::BuildDoReturnStmt(SourceLocation DoReturnLoc, Expr *Operand) {
  if (DoExprStack.empty()) {
    Diag(DoReturnLoc, diag::err_do_return_outside_do_expr);
    return StmtError();
  }

  DoExprStackEntry &Entry = DoExprStack.back();

  // Reject `do_return` that crossed a lambda / nested function-scope boundary.
  if (FunctionScopes.size() != Entry.FunctionScopeDepth) {
    Diag(DoReturnLoc, diag::err_do_return_crosses_function_scope);
    return StmtError();
  }

  // C++17: do_return statements in discarded statements are not considered when
  // deducing a do-expression's result type.
  if (ExprEvalContexts.back().isDiscardedStatementContext()) {
    if (Operand) {
      ExprResult ER =
          ActOnFinishFullExpr(Operand, DoReturnLoc, /*DiscardedValue=*/false);
      if (ER.isInvalid())
        return StmtError();
      Operand = ER.get();
    }
    return new (Context) DoReturnStmt(DoReturnLoc, Operand);
  }

  // Match function return type deduction: in a dependent context, do not
  // deduce the result type yet, even from non-dependent operands. Instantiation
  // will rebuild the live do_return statements and deduce from those.
  if ((Entry.TemplateDepth > 0 || isParsingExpansionStmtPattern(*this)) &&
      doExprHasDeducedResultType(Entry)) {
    Entry.HasDependentDoReturn = true;
    return new (Context) DoReturnStmt(DoReturnLoc, Operand);
  }

  if (!Operand) {
    // `do_return;` with no expression: only valid for void result.
    QualType ResultType =
        Entry.TypeIsExplicit && !Entry.ExplicitType->getContainedAutoType()
            ? Entry.ExplicitType
            : Entry.DeducedType;
    if (!ResultType.isNull() && !ResultType->isDependentType() &&
        !ResultType->isVoidType()) {
      Diag(DoReturnLoc, diag::err_do_return_missing_value);
      return StmtError();
    }
    if (Entry.TypeIsExplicit && Entry.ExplicitType->getContainedAutoType()) {
      // As with `return;` in a function with a deduced return type, the type
      // as written must be 'auto' or 'decltype(auto)', possibly cv-qualified
      // or constrained: 'auto*' and the like cannot deduce from void.
      if (!Entry.ExplicitType->getAs<AutoType>()) {
        Diag(DoReturnLoc, diag::err_do_return_void_but_not_auto)
            << Entry.ExplicitType;
        Entry.DeductionFailed = true;
        return StmtError();
      }
      CXXScalarValueInitExpr VoidVal(Context.VoidTy, nullptr, DoReturnLoc);
      sema::TemplateDeductionInfo Info(DoReturnLoc);
      TemplateDeductionResult Res =
          DeduceAutoType(Entry.ExplicitTypeInfo->getTypeLoc(), &VoidVal,
                         Entry.DeducedType, Info);
      if (Res != TemplateDeductionResult::Success)
        return StmtError();
    } else if (!Entry.TypeIsExplicit && Entry.DeducedType.isNull()) {
      Entry.DeducedType = Context.VoidTy;
    }
    return new (Context) DoReturnStmt(DoReturnLoc, nullptr);
  }

  // Defer typing for dependent operands; they'll be re-checked at template
  // instantiation via TreeTransform.
  if (Operand->isTypeDependent()) {
    Entry.HasDependentDoReturn = true;
    return new (Context) DoReturnStmt(DoReturnLoc, Operand);
  }

  // Determine move-eligibility BEFORE applying any conversions, so that a
  // named local variable in `do_return r;` gets the same implicit-move
  // treatment as in `return r;` (C++20 [class.copy.elision]p3).
  NamedReturnInfo NRInfo = getNamedReturnInfo(Operand);

  // Compute the result type. For deduced types, this is the operand's value
  // type after lvalue-to-rvalue/array-to-pointer/function-to-pointer
  // conversions on scalar operands; for class operands, the operand stays an
  // lvalue and the deduced type is its non-reference type.
  QualType ResultType;
  if (Entry.TypeIsExplicit && Entry.ExplicitType->getContainedAutoType()) {
    sema::TemplateDeductionInfo Info(Operand->getExprLoc());
    TemplateDeductionResult Res =
        DeduceAutoType(Entry.ExplicitTypeInfo->getTypeLoc(), Operand,
                       Entry.DeducedType, Info);
    switch (Res) {
    case TemplateDeductionResult::Success:
      break;
    case TemplateDeductionResult::AlreadyDiagnosed:
      return StmtError();
    case TemplateDeductionResult::Inconsistent:
      Diag(DoReturnLoc, diag::err_do_return_type_mismatch)
          << Info.SecondArg << Info.FirstArg;
      Entry.DeductionFailed = true;
      return StmtError();
    default:
      Diag(Operand->getExprLoc(), diag::err_auto_fn_deduction_failure)
          << Entry.ExplicitType << Operand->getType();
      return StmtError();
    }
    ResultType = Entry.DeducedType;
  } else if (Entry.TypeIsExplicit) {
    ResultType = Entry.ExplicitType;
  } else {
    QualType OperandType;
    if (Operand->getType()->isRecordType()) {
      // Don't apply lvalue-to-rvalue to class types — that would force a
      // copy/move and defeat NRVO/move-eligibility. Use the value type.
      OperandType = Operand->getType().getNonReferenceType();
    } else {
      ExprResult Conv = DefaultFunctionArrayLvalueConversion(Operand);
      if (Conv.isInvalid())
        return StmtError();
      Operand = Conv.get();
      OperandType = Operand->getType();
    }

    if (Entry.DeducedType.isNull()) {
      Entry.DeducedType = OperandType;
    } else if (!Context.hasSameType(Entry.DeducedType, OperandType)) {
      Diag(DoReturnLoc, diag::err_do_return_type_mismatch)
          << OperandType << Entry.DeducedType;
      Entry.DeductionFailed = true;
      return StmtError();
    }
    ResultType = Entry.DeducedType;
  }

  // Initialize the do-expression's result from the operand. For reference
  // result types, this triggers the same lifetime checks as a function
  // returning a reference (so e.g. `do_return prvalue();` for a
  // reference-typed do-expression diagnoses the same dangling reference
  // that the equivalent IIFE would).
  InitializedEntity InitEntity =
      InitializedEntity::InitializeResult(DoReturnLoc, ResultType);
  ExprResult Init =
      PerformMoveOrCopyInitialization(InitEntity, NRInfo, Operand);
  if (Init.isInvalid())
    return StmtError();
  Operand = Init.get();

  return new (Context) DoReturnStmt(DoReturnLoc, Operand);
}
