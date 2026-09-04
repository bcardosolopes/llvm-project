//===--- UseDoExprCheck.cpp - clang-tidy ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "UseDoExprCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Analysis/CFG.h"
#include "clang/Lex/Lexer.h"

using namespace clang::ast_matchers;

namespace clang::tidy::modernize {

namespace {

/// The classification of one immediately-invoked lambda. Exactly one of these
/// is reported per lambda; the order of the enumerators is the order in which
/// they are tested, so a lambda that is both generic and mutable is reported
/// as generic.
enum class Category {
  // Reasons a `do` expression cannot express the lambda. Tested first: a
  // rewrite is impossible regardless of what the body looks like.
  InMacro,
  InUnevaluatedContext,
  Coroutine,
  Generic,
  HasParams,
  Mutable,
  CaptureHeavy,
  ExplicitSpecifier,
  LabelOrGoto,
  FallsOffEnd,
  Unanalyzable,
  // Rewritable, most interesting first.
  ControlFlowWorkaround,
  ConstInit,
  Simple,
};

StringRef name(Category C) {
  switch (C) {
  case Category::InMacro:
    return "in-macro";
  case Category::InUnevaluatedContext:
    return "in-unevaluated-context";
  case Category::Coroutine:
    return "coroutine";
  case Category::Generic:
    return "generic";
  case Category::HasParams:
    return "has-params";
  case Category::Mutable:
    return "mutable";
  case Category::CaptureHeavy:
    return "capture-heavy";
  case Category::ExplicitSpecifier:
    return "explicit-specifier";
  case Category::LabelOrGoto:
    return "label-or-goto";
  case Category::FallsOffEnd:
    return "falls-off-end";
  case Category::Unanalyzable:
    return "unanalyzable";
  case Category::ControlFlowWorkaround:
    return "control-flow-workaround";
  case Category::ConstInit:
    return "const-init";
  case Category::Simple:
    return "simple";
  }
  llvm_unreachable("unhandled category");
}

bool isRewritable(Category C) {
  return C == Category::ControlFlowWorkaround || C == Category::ConstInit ||
         C == Category::Simple;
}

/// Collects the control-flow shape of a lambda body. It deliberately does not
/// descend into anything with a control flow of its own -- a nested lambda,
/// function or local class -- because those `return`s belong to that nested
/// thing, and rewriting them would be wrong.
class BodyScanner : public RecursiveASTVisitor<BodyScanner> {
public:
  bool TraverseLambdaExpr(LambdaExpr *) { return true; }
  bool TraverseDecl(Decl *D) {
    if (isa_and_nonnull<FunctionDecl, TagDecl>(D))
      return true;
    return RecursiveASTVisitor::TraverseDecl(D);
  }
  bool VisitSwitchStmt(SwitchStmt *) {
    HasSwitch = true;
    return true;
  }
  bool VisitIfStmt(IfStmt *S) {
    if (S->getElse())
      HasElse = true;
    return true;
  }
  bool VisitReturnStmt(ReturnStmt *S) {
    Returns.push_back(S);
    return true;
  }
  // A label is function-scoped, so moving one out of a lambda and into the
  // enclosing function can collide with a label already there, and a `goto`
  // that used to be confined to the lambda would suddenly be able to leave.
  bool VisitLabelStmt(LabelStmt *) {
    HasLabelOrGoto = true;
    return true;
  }
  bool VisitGotoStmt(GotoStmt *) {
    HasLabelOrGoto = true;
    return true;
  }
  bool VisitIndirectGotoStmt(IndirectGotoStmt *) {
    HasLabelOrGoto = true;
    return true;
  }

  bool HasSwitch = false;
  bool HasElse = false;
  bool HasLabelOrGoto = false;
  SmallVector<const ReturnStmt *, 4> Returns;
};

/// Whether control can reach the closing brace of \p Body. A `do` expression
/// whose body can fall off the end is ill-formed when it has to produce a
/// value, so this has to be answered before offering a rewrite -- and the
/// answer is interesting on its own, because in the original lambda the same
/// path is undefined behaviour.
bool canFallOffEnd(const FunctionDecl *CallOp, ASTContext &Ctx) {
  CFG::BuildOptions Opts;
  std::unique_ptr<CFG> G = CFG::buildCFG(CallOp, CallOp->getBody(), &Ctx, Opts);
  if (!G)
    return true; // Cannot tell: assume the worst and refuse.
  for (const CFGBlock *Pred : G->getExit().preds()) {
    if (!Pred)
      continue;
    const Stmt *Last = nullptr;
    for (const CFGElement &Elem : llvm::reverse(*Pred))
      if (std::optional<CFGStmt> S = Elem.getAs<CFGStmt>()) {
        Last = S->getStmt();
        break;
      }
    if (isa_and_nonnull<ReturnStmt, CXXThrowExpr>(Last))
      continue;
    if (Pred->hasNoReturnElement())
      continue;
    return true;
  }
  return false;
}

/// Whether \p Child sits where a statement goes, rather than as part of a
/// larger expression. Both the do-while ambiguity and the discarded-value
/// question turn on this.
bool isStatementPosition(const Stmt *Parent, const Stmt *Child) {
  if (isa<CompoundStmt, LabelStmt, CaseStmt, DefaultStmt, AttributedStmt>(
          Parent))
    return true;
  if (const auto *S = dyn_cast<IfStmt>(Parent))
    return Child == S->getThen() || Child == S->getElse();
  if (const auto *S = dyn_cast<WhileStmt>(Parent))
    return Child == S->getBody();
  if (const auto *S = dyn_cast<ForStmt>(Parent))
    return Child == S->getBody();
  if (const auto *S = dyn_cast<DoStmt>(Parent))
    return Child == S->getBody();
  if (const auto *S = dyn_cast<SwitchStmt>(Parent))
    return Child == S->getBody();
  if (const auto *S = dyn_cast<CXXForRangeStmt>(Parent))
    return Child == S->getBody();
  return false;
}

/// Whether \p Parent leaves \p Child's value unused, so that a value discarded
/// at \p Parent means one discarded at \p Child too.
bool isValueTransparent(const Expr *Parent, const Expr *Child) {
  // A temporary that is bound or materialized is not thereby *used*: the node
  // that reads it, if any, is further up. A discarded call returning a class
  // type is wrapped in both, so leaving them out loses the common case.
  if (isa<ParenExpr, FullExpr, CXXBindTemporaryExpr, MaterializeTemporaryExpr>(
          Parent))
    return true;
  if (const auto *C = dyn_cast<CastExpr>(Parent))
    return C->getCastKind() == CK_ToVoid;
  if (const auto *B = dyn_cast<BinaryOperator>(Parent))
    return B->getOpcode() == BO_Comma && B->getLHS() == Child;
  return false;
}

/// The lambda object a call expression invokes, or null if the callee is not a
/// lambda written right at the call site.
const LambdaExpr *getInvokedLambda(const CallExpr *Call) {
  const Expr *E = nullptr;
  if (const auto *Op = dyn_cast<CXXOperatorCallExpr>(Call)) {
    if (Op->getOperator() != OO_Call || Op->getNumArgs() == 0)
      return nullptr;
    E = Op->getArg(0);
  } else if (const auto *Member = dyn_cast<CXXMemberCallExpr>(Call)) {
    // A captureless lambda converted to a function pointer is a member call on
    // the lambda too -- of the conversion operator, not of `operator()`. In
    // `takes([]{ ... })` the lambda is being passed, not invoked, and there is
    // no call to remove.
    const CXXMethodDecl *Method = Member->getMethodDecl();
    if (!Method || Method->getOverloadedOperator() != OO_Call)
      return nullptr;
    E = Member->getImplicitObjectArgument();
  } else {
    E = Call->getCallee();
  }
  // A lambda reaches the call through a materialized temporary and, in the
  // member-call spelling, a member access; peel until something sticks.
  while (E) {
    const Expr *Stripped = E->IgnoreImplicit()->IgnoreParens();
    if (Stripped == E)
      break;
    E = Stripped;
  }
  return dyn_cast_or_null<LambdaExpr>(E);
}

bool isUnevaluated(const Stmt *S) {
  if (const auto *U = dyn_cast<UnaryExprOrTypeTraitExpr>(S))
    return U->getKind() != UETT_VecStep;
  return isa<CXXNoexceptExpr, CXXTypeidExpr, RequiresExpr>(S);
}

/// Collects the immediately-invoked lambdas in a subtree the AST matchers do
/// not reach on their own.
class InvokedLambdaFinder : public RecursiveASTVisitor<InvokedLambdaFinder> {
public:
  bool VisitCallExpr(CallExpr *Call) {
    if (getInvokedLambda(Call))
      Calls.push_back(Call);
    return true;
  }

  SmallVector<const CallExpr *, 2> Calls;
};

} // namespace

void UseDoExprCheck::registerMatchers(MatchFinder *Finder) {
  // Three spellings reach the same place. `l()` on a concrete lambda is an
  // operator call; the same source inside an uninstantiated template is a
  // plain (dependent) call; and `l.operator()()` is a member call.
  Finder->addMatcher(
      cxxOperatorCallExpr(hasOverloadedOperatorName("()"),
                          hasArgument(0, ignoringParenImpCasts(lambdaExpr())))
          .bind("call"),
      this);
  Finder->addMatcher(
      cxxMemberCallExpr(on(ignoringParenImpCasts(lambdaExpr())),
                        callee(cxxMethodDecl(hasOverloadedOperatorName("()"))))
          .bind("call"),
      this);
  Finder->addMatcher(callExpr(unless(cxxOperatorCallExpr()),
                              unless(cxxMemberCallExpr()),
                              callee(expr(ignoringParenImpCasts(lambdaExpr()))))
                         .bind("call"),
                     this);
  // The matcher traversal does not descend into the operand of a `decltype`,
  // so the calls in there are dug out by hand below. A census that quietly
  // dropped them would understate the unevaluated bucket.
  Finder->addMatcher(decltypeType().bind("decltype"), this);
}

void UseDoExprCheck::check(const MatchFinder::MatchResult &Result) {
  if (const auto *DT = Result.Nodes.getNodeAs<DecltypeType>("decltype")) {
    InvokedLambdaFinder Finder;
    Finder.TraverseStmt(const_cast<Expr *>(DT->getUnderlyingExpr()));
    for (const CallExpr *C : Finder.Calls)
      report(C, *Result.Context, *Result.SourceManager,
             /*ForceUnevaluated=*/true);
    return;
  }
  report(Result.Nodes.getNodeAs<CallExpr>("call"), *Result.Context,
         *Result.SourceManager, /*ForceUnevaluated=*/false);
}

void UseDoExprCheck::report(const CallExpr *Call, ASTContext &Ctx,
                            const SourceManager &SM, bool ForceUnevaluated) {
  const LambdaExpr *Lambda = Call ? getInvokedLambda(Call) : nullptr;
  if (!Lambda)
    return;

  const SourceLocation Loc = Lambda->getBeginLoc();

  // The same lambda is matched once for a template pattern and again for each
  // instantiation of the enclosing template, all sharing this location.
  if (!ReportedLambdas.insert(SM.getFileLoc(Loc).getRawEncoding()).second)
    return;

  // Walk out of the call to answer two questions that the lambda alone cannot:
  // is this an unevaluated operand, and is it the initializer of a constant?
  bool Unevaluated = ForceUnevaluated;
  bool ConstInit = false;
  if (!Unevaluated) {
    DynTypedNode Node = DynTypedNode::create(*Call);
    for (unsigned Depth = 0; Depth != 32; ++Depth) {
      DynTypedNodeList Parents = Ctx.getParents(Node);
      if (Parents.empty())
        break;
      Node = Parents[0];
      if (const auto *S = Node.get<Stmt>()) {
        if (isUnevaluated(S)) {
          Unevaluated = true;
          break;
        }
        // Keep climbing only through the wrappers a value can be nested in;
        // anything else means the initializer question is already answered.
        if (!isa<Expr>(S) && !isa<DeclStmt>(S))
          break;
        continue;
      }
      if (Node.get<TypeLoc>())
        break;
      if (const auto *VD = Node.get<VarDecl>()) {
        ConstInit = VD->isConstexpr() || VD->getType().isConstQualified();
        break;
      }
      if (const auto *FD = Node.get<FieldDecl>()) {
        ConstInit = FD->getType().isConstQualified();
        break;
      }
      break;
    }
  }

  const CXXMethodDecl *CallOp = Lambda->getCallOperator();
  BodyScanner Scanner;
  Scanner.TraverseStmt(const_cast<Stmt *>(Lambda->getBody()));

  const Category Cat = [&] {
    if (Loc.isMacroID() || Call->getBeginLoc().isMacroID())
      return Category::InMacro;
    if (Unevaluated)
      return Category::InUnevaluatedContext;
    if (!CallOp)
      return Category::Unanalyzable;
    if (isa_and_nonnull<CoroutineBodyStmt>(CallOp->getBody()))
      return Category::Coroutine;
    if (Lambda->isGenericLambda())
      return Category::Generic;
    if (CallOp->getNumParams() != 0)
      return Category::HasParams;
    if (Lambda->isMutable())
      return Category::Mutable;
    // A `do` expression names the enclosing locals directly, so it can stand
    // in for by-reference captures but never for a copy.
    if (Lambda->getCaptureDefault() == LCD_ByCopy)
      return Category::CaptureHeavy;
    for (const LambdaCapture &C : Lambda->captures()) {
      if (C.capturesVariable() && C.getCaptureKind() == LCK_ByCopy)
        return Category::CaptureHeavy;
      if (C.capturesVLAType() || C.getCaptureKind() == LCK_StarThis)
        return Category::CaptureHeavy;
    }
    // Anything the `do` expression grammar has no place to put. An explicit
    // `noexcept` is the one that matters: dropping it turns a call to
    // `std::terminate` into a propagating exception.
    // Not `constexpr`: since C++17 a qualifying lambda gets that implicitly,
    // and a `do` expression is usable in a constant expression anyway.
    if (CallOp->isConsteval() || CallOp->isStatic() ||
        CallOp->getType()
                ->castAs<FunctionProtoType>()
                ->getExceptionSpecType() != EST_None)
      return Category::ExplicitSpecifier;
    if (Scanner.HasLabelOrGoto)
      return Category::LabelOrGoto;
    if (!CallOp->getReturnType()->isVoidType() &&
        !CallOp->getReturnType()->isDependentType() &&
        canFallOffEnd(CallOp, Ctx))
      return Category::FallsOffEnd;
    if (Scanner.HasSwitch || Scanner.HasElse || Scanner.Returns.size() > 1)
      return Category::ControlFlowWorkaround;
    if (ConstInit)
      return Category::ConstInit;
    return Category::Simple;
  }();

  if (!isRewritable(Cat)) {
    diag(Loc, "immediately-invoked lambda cannot be replaced by a 'do' "
              "expression (category: %0)")
        << name(Cat);
    return;
  }

  auto Diag = diag(Loc, "immediately-invoked lambda can be replaced by a 'do' "
                        "expression (category: %0)")
              << name(Cat);
  buildRewrite(Call, Lambda, Scanner.Returns, Ctx, SM, Diag);
}

/// Emits the fixits that turn \p Call into a `do` expression, or nothing at all
/// if any piece of the rewrite cannot be spelled from the available source
/// locations. Returns false when no fixit was attached.
bool UseDoExprCheck::buildRewrite(const CallExpr *Call,
                                  const LambdaExpr *Lambda,
                                  ArrayRef<const ReturnStmt *> Returns,
                                  ASTContext &Ctx, const SourceManager &SM,
                                  DiagnosticBuilder &Diag) {
  const LangOptions &LO = getLangOpts();
  const auto *Body = cast<CompoundStmt>(Lambda->getBody());

  // Bare `do {` at the start of a statement is a do-while loop, so a rewrite
  // in statement position has to be parenthesized.
  bool NeedParens = false;
  bool Discarded = false;
  {
    DynTypedNode Node = DynTypedNode::create(*Call);
    const Expr *Top = Call;
    // Whether this call's own value is still unused by the time the climb
    // reaches `Top`. The climb does not stop where the value gets consumed,
    // because the do-while ambiguity is a question about where the *outermost*
    // expression starts -- `l().m();` needs parentheses even though `.m` uses
    // the result. The (void) cast is the one that needs the value to be
    // genuinely discarded: `takes(l())` sits in statement position too, and
    // rewriting it to `takes((void)(do { ... }))` does not compile.
    bool ValueUnused = true;
    bool AlreadyCastToVoid = false;
    for (unsigned Depth = 0; Depth != 32; ++Depth) {
      DynTypedNodeList Parents = Ctx.getParents(Node);
      if (Parents.empty())
        break;
      Node = Parents[0];
      const auto *S = Node.get<Stmt>();
      if (!S)
        break;
      if (const auto *E = dyn_cast<Expr>(S)) {
        ValueUnused = ValueUnused && isValueTransparent(E, Top);
        // Somebody already discarded the value in the source; adding a second
        // cast would only produce `(void)((void)(do { ... }))`.
        if (ValueUnused)
          if (const auto *C = dyn_cast<ExplicitCastExpr>(E))
            AlreadyCastToVoid |= C->getCastKind() == CK_ToVoid;
        Top = E;
        continue;
      }
      if (AlreadyCastToVoid)
        ValueUnused = false;
      const bool StatementPosition = isStatementPosition(S, Top);
      Discarded = StatementPosition && ValueUnused;
      NeedParens =
          StatementPosition && Top->getBeginLoc() == Lambda->getBeginLoc();
      break;
    }
  }

  // A discarded call warns about nothing; a discarded do-expression with a
  // value trips -Wunused-value, which would be new noise on every rewritten
  // site.
  const bool NeedVoidCast =
      Discarded && !Lambda->getCallOperator()->getReturnType()->isVoidType();
  std::string Head = NeedVoidCast ? "(void)(do " : NeedParens ? "(do " : "do ";
  if (Lambda->hasExplicitResultType()) {
    // A trailing return type carries over as `do -> T`. Take it from the
    // function's own TypeLoc: `getReturnTypeSourceRange()` is empty for a
    // lambda, whose declarator has no leading return type to point at.
    const auto FTL = Lambda->getCallOperator()
                         ->getTypeSourceInfo()
                         ->getTypeLoc()
                         .getAsAdjusted<FunctionProtoTypeLoc>();
    if (!FTL)
      return false;
    const StringRef Ret = Lexer::getSourceText(
        CharSourceRange::getTokenRange(FTL.getReturnLoc().getSourceRange()), SM,
        LO);
    if (Ret.empty())
      return false;
    Head += ("-> " + Ret + " ").str();
  }

  const SourceLocation AfterBody =
      Lexer::getLocForEndOfToken(Body->getRBracLoc(), 0, SM, LO);
  if (AfterBody.isInvalid() || Call->getEndLoc().isInvalid())
    return false;

  // Everything from the introducer up to the body's `{` is the lambda-specific
  // syntax; the trailing `()` -- and, in the `.operator()()` spelling, the
  // member access as well -- goes away entirely.
  Diag << FixItHint::CreateReplacement(
      CharSourceRange::getCharRange(Lambda->getIntroducerRange().getBegin(),
                                    Body->getLBracLoc()),
      Head);
  Diag << FixItHint::CreateReplacement(
      CharSourceRange::getTokenRange(AfterBody, Call->getEndLoc()),
      NeedParens || NeedVoidCast ? ")" : "");
  for (const ReturnStmt *R : Returns)
    Diag << FixItHint::CreateReplacement(
        CharSourceRange::getTokenRange(R->getReturnLoc(), R->getReturnLoc()),
        "do_return");
  return true;
}

} // namespace clang::tidy::modernize
