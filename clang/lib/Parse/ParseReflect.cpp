//===--- ParseReflect.cpp - C++2c Reflection Parsing (P2996) --------------===//
//
// Copyright 2024 Bloomberg Finance L.P.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements parsing for reflection facilities.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/LocInfoType.h"
#include "clang/AST/Reflection.h"
#include "clang/Basic/DiagnosticParse.h"
#include "clang/Lex/Token.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Template.h"
#include "llvm/ADT/ScopeExit.h"
using namespace clang;

ExprResult Parser::ParseCXXReflectExpression(SourceLocation OpLoc) {
  SourceLocation OperandLoc = Tok.getLocation();

  // Handle token sequence: ^^{ balanced tokens }
  if (Tok.is(tok::l_brace)) {
    SourceLocation LBraceLoc = ConsumeBrace();

    auto IsBackslash = [&](const Token &T) {
      return T.is(tok::unknown) && T.getLength() == 1 &&
             *PP.getSourceManager().getCharacterData(T.getLocation()) == '\\';
    };

    SmallVector<Token, 16> Tokens;
    unsigned BraceDepth = 1;
    // The brace depths at which nested ^^{ } literals opened, innermost
    // last. An interpolation binds to the innermost literal enclosing it;
    // each additional backslash reaches one literal further out (as with
    // nested backquotes). With D nested literals open, `\(e)` written with
    // K backslashes is this literal's when K == D + 1; with fewer it belongs
    // to a nested literal and is kept as tokens for when that literal is
    // parsed in turn; with more it escapes past this literal.
    SmallVector<unsigned, 2> NestedLiterals;
    while (BraceDepth > 0 && Tok.isNot(tok::eof)) {
      // A nested literal: keep its '^^{' and remember where it closes.
      if (Tok.is(tok::caretcaret) && NextToken().is(tok::l_brace)) {
        Tokens.push_back(Tok);
        ConsumeToken();
        ++BraceDepth;
        NestedLiterals.push_back(BraceDepth);
        Tokens.push_back(Tok);
        ConsumeBrace();
        continue;
      }
      if (Tok.is(tok::l_brace))
        ++BraceDepth;
      else if (Tok.is(tok::r_brace)) {
        if (!NestedLiterals.empty() && NestedLiterals.back() == BraceDepth)
          NestedLiterals.pop_back();
        --BraceDepth;
        if (BraceDepth == 0)
          break;
      }

      // Check for interpolation: \(expr), with one backslash per level.
      unsigned Backslashes = 0;
      if (IsBackslash(Tok)) {
        Backslashes = 1;
        while (IsBackslash(GetLookAheadToken(Backslashes)))
          ++Backslashes;
        if (GetLookAheadToken(Backslashes).isNot(tok::l_paren))
          Backslashes = 0;
      }
      unsigned Level = NestedLiterals.size() + 1;
      if (Backslashes > Level) {
        Diag(Tok, diag::err_interpolation_escapes_token_sequence)
            << Backslashes << Level;
        Backslashes = Level; // recover as this literal's
      }
      if (Backslashes == Level) {
        SourceLocation BackslashLoc = Tok.getLocation();
        // Preserve leading space from the backslash token so that stringize
        // can correctly reproduce whitespace (e.g., "int \(id("x"))" should
        // become "int x", not "intx").
        bool HadLeadingSpace = Tok.hasLeadingSpace();
        while (IsBackslash(Tok))
          ConsumeToken();  // consume the '\'s
        ConsumeParen();  // consume '('

        ExprResult Expr = ParseAssignmentExpression();
        if (Expr.isInvalid()) {
          SkipUntil(tok::r_paren, StopAtSemi | StopBeforeMatch);
          if (Tok.is(tok::r_paren))
            ConsumeParen();
          continue;
        }

        SourceLocation RParenLoc = Tok.getLocation();
        if (ExpectAndConsume(tok::r_paren)) {
          continue;
        }

        ExprResult CE = Actions.ActOnTokenSequenceInterpolation(Expr.get());
        if (CE.isInvalid())
          continue;

        Token AnnTok;
        AnnTok.startToken();
        AnnTok.setKind(tok::annot_token_seq_expr);
        AnnTok.setLocation(BackslashLoc);
        AnnTok.setAnnotationEndLoc(RParenLoc);
        if (HadLeadingSpace)
          AnnTok.setFlag(Token::LeadingSpace);
        setExprAnnotation(AnnTok, CE);
        Tokens.push_back(AnnTok);
        continue;
      }

      Tokens.push_back(Tok);
      ConsumeAnyToken();
    }

    if (Tok.isNot(tok::r_brace)) {
      Diag(LBraceLoc, diag::err_expected) << tok::r_brace;
      return ExprError();
    }
    SourceLocation RBraceLoc = Tok.getLocation();
    ConsumeBrace();

    // Mark any identifiers in the token sequence that refer to local variables
    // or parameters as referenced, to suppress -Wunused-parameter and
    // -Wunused-variable warnings. The token sequence will use them when
    // injected. Lookup happens in the capture scope — for the common case
    // where capture and injection share a scope this is correct; for
    // cross-scope cases this is a heuristic and may match unrelated names of
    // the same spelling. Use \(expr) to interpolate when precision matters.
    for (const Token &T : Tokens) {
      if (T.is(tok::identifier)) {
        if (IdentifierInfo *II = T.getIdentifierInfo()) {
          LookupResult R(Actions, II, T.getLocation(),
                         Sema::LookupOrdinaryName);
          if (Actions.LookupName(R, getCurScope(),
                                 /*AllowBuiltinCreation=*/false)) {
            if (auto *VD = R.getAsSingle<VarDecl>())
              VD->setReferenced();
          }
        }
      }
    }

    SourceRange OperandRange(LBraceLoc, RBraceLoc);
    return Actions.ActOnCXXTokenSequenceReflection(getCurScope(), OpLoc,
                                                   OperandRange, Tokens);
  }

  Sema::ConstevalOnlyRecorder RecordConstevalOnly(Actions);
  EnterExpressionEvaluationContext EvalContext(
        Actions, Sema::ExpressionEvaluationContext::ReflectionContext);

  // Parse a leading nested-name-specifier, e.g.,
  //
  CXXScopeSpec SS;
  if (ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/nullptr,
                                     /*ObjectHasErrors=*/false,
                                     /*EnteringContext=*/false)) {
    SkipUntil(tok::semi, StopAtSemi | StopBeforeMatch);
    return ExprError();
  }

  // Start the tentative parse: This will be reverted if the operand is found
  // to be a type (or rather: a type whose name is more complicated than a
  // single identifier).
  //
  TentativeParsingAction TentativeAction(*this);

  // Next, check for an unqualified-id.
  if (Tok.isOneOf(tok::identifier, tok::kw_operator, tok::kw_template,
                  tok::tilde, tok::annot_template_id)) {
    // Try parsing the operand name as an 'unqualified-id'.

    SourceLocation TemplateKWLoc;
    UnqualifiedId UnqualName;
    if (!ParseUnqualifiedId(SS, ParsedType{}, /*ObjectHadError=*/false,
                            /*EnteringContext=*/false,
                            /*AllowDestructorName=*/true,
                            /*AllowConstructorName=*/false,
                            /*AllowDeductionGuide=*/false,
                            SS.isSet() ? &TemplateKWLoc : nullptr,
                            UnqualName)) {
      bool AssumeType = false;
      if (UnqualName.getKind() == UnqualifiedIdKind::IK_TemplateId &&
          UnqualName.TemplateId->Kind == TNK_Type_template)
        AssumeType = true;
      else if (Tok.isOneOf(tok::l_square, tok::l_paren, tok::star, tok::amp,
                           tok::ampamp, tok::kw_const, tok::kw_volatile,
                           tok::kw_restrict))
        AssumeType = true;

      if (!AssumeType) {
        TentativeAction.Commit();
        return RecordConstevalOnly.RecordAndReturn(
                Actions.ActOnCXXReflectExpr(OpLoc, TemplateKWLoc, SS,
                                            UnqualName));
      }
    }
  } else if (SS.isValid() &&
             SS.getScopeRep().getKind() == NestedNameSpecifier::Kind::Global) {
    // Check for '^::'.
    TentativeAction.Commit();

    Decl *TUDecl = Actions.getASTContext().getTranslationUnitDecl();
    return RecordConstevalOnly.RecordAndReturn(
            Actions.ActOnCXXReflectExpr(OpLoc, SourceLocation(), TUDecl));
  }
  TentativeAction.Revert();

  if (SS.isSet() &&
      TryAnnotateTypeOrScopeTokenAfterScopeSpec(SS, true,
                                                ImplicitTypenameContext::No)) {
    SkipUntil(tok::semi, StopAtSemi | StopBeforeMatch);
    return ExprError();
  }

  // Anything else must be a type-id (e.g., 'const int', 'Cls(*)(int)'.
  if (isCXXTypeId(TentativeCXXTypeIdContext::AsReflectionOperand)) {
    TypeResult TR = ParseTypeName(nullptr, DeclaratorContext::ReflectOperator);
    if (TR.isInvalid())
      return ExprError();

    std::string refKind;
    if (QualType QT = cast<LocInfoType>(TR.get().get())->getType();
        QT->isLValueReferenceType()) {
      refKind = "&";
    } else if (QT->isRValueReferenceType()) {
      refKind = "&&";
    } else if (auto *FPT = dyn_cast<FunctionProtoType>(QT)) {
      if (FPT->getRefQualifier() == RQ_LValue)
        refKind = "&";
      else if (FPT->getRefQualifier() == RQ_RValue)
        refKind = "&&";
    }

    if (!refKind.empty() &&
        !Tok.isOneOf(tok::r_paren, tok::greater, tok::greatergreater,
                     tok::comma, tok::r_brace, tok::r_square, tok::r_splice,
                     tok::semi, tok::ellipsis, tok::colon, tok::question)) {
      TypeLoc TL = cast<LocInfoType>(TR.get().get())
          ->getTypeSourceInfo()->getTypeLoc();

      Diag(OperandLoc, diag::warn_meant_parenthesize_reflection)
        << refKind << TL.getSourceRange();
    }

    return RecordConstevalOnly.RecordAndReturn(
            Actions.ActOnCXXReflectExpr(OpLoc, TR));
  }

  Diag(OperandLoc, diag::err_cannot_reflect_operand);
  return ExprError();
}

ExprResult Parser::ParseCXXMetafunctionExpression() {
  assert(Tok.is(tok::kw___metafunction) && "expected '___metafunction'");
  SourceLocation KwLoc = ConsumeToken();

  // Balance any number of arguments in parens.
  BalancedDelimiterTracker Parens(*this, tok::l_paren);
  if (Parens.expectAndConsume())
    return ExprError();

  SmallVector<Expr *, 2> Args;
  do {
    ExprResult Expr = ParseConstantExpression();
    if (Expr.isInvalid()) {
      Parens.skipToEnd();
      return ExprError();
    }
    Args.push_back(Expr.get());
  } while (TryConsumeToken(tok::comma));

  if (Parens.consumeClose())
    return ExprError();

  SourceLocation LPLoc = Parens.getOpenLocation();
  SourceLocation RPLoc = Parens.getCloseLocation();
  return Actions.ActOnCXXMetafunction(KwLoc, LPLoc, Args, RPLoc);
}

bool Parser::ParseSpliceSpecifier(bool TryParseSpecialization) {
  assert(Tok.is(tok::l_splice) && "expected '[:'");

  BalancedDelimiterTracker SpliceTokens(*this, tok::l_splice);
  if (SpliceTokens.expectAndConsume())
    return true;

  ExprResult ER;
  if (Tok.isOneOf(tok::annot_typename, tok::annot_template_name,
                  tok::annot_splice) &&
      NextToken().is(tok::r_splice)) {
    // An interpolated type, template, or namespace (from a token sequence)
    // used as the whole splice operand: splice what it designates.
    SourceLocation Loc = Tok.getLocation();
    if (Tok.is(tok::annot_splice)) {
      SpliceResult Inner = getSpliceAnnotation(Tok);
      ConsumeAnnotationToken();
      ER = Inner.isInvalid() ? ExprError() : Inner.get()->getOperand();
    } else if (Tok.is(tok::annot_typename)) {
      // The annotation carries a bare type with no location information.
      TypeResult T = getTypeAnnotation(Tok);
      ConsumeAnnotationToken();
      ER = T.isInvalid() ? ExprError()
                         : Actions.BuildCXXReflectExpr(
                               Loc, Loc, Sema::GetTypeFromParser(T.get()));
    } else {
      TemplateName Template =
          TemplateName::getFromVoidPointer(Tok.getAnnotationValue());
      ConsumeAnnotationToken();
      ER = Actions.BuildCXXReflectExpr(Loc, Loc, Template);
    }
  } else {
    ER = ParseConstantExpression();
  }
  if (ER.isInvalid() || ER.get()->containsErrors()) {
    SpliceTokens.skipToEnd();
    return true;
  }
  Expr *Operand = ER.get();

  Token end = Tok;
  if (SpliceTokens.consumeClose())
    return true;

  SourceLocation LSplice = SpliceTokens.getOpenLocation();
  SourceLocation RSplice = SpliceTokens.getCloseLocation();

  SpliceResult SR;
  if (TryParseSpecialization && Tok.is(tok::less)) {
    ASTTemplateArgsPtr TArgsPtr;
    SourceLocation LAngleLoc, RAngleLoc;
    {
      TemplateArgList TArgs;
      if (ParseTemplateIdAfterTemplateName(/*ConsumeLastToken=*/false,
                                           LAngleLoc, TArgs, RAngleLoc,
                                           /*Template=*/nullptr))
        return true;

      TArgsPtr = ASTTemplateArgsPtr(TArgs.data(), TArgs.size());
      end = Tok;
      ConsumeToken();
    }
    SR = Actions.ActOnSpliceSpecifier(LSplice, Operand, RSplice, LAngleLoc,
                                      TArgsPtr, RAngleLoc);
  } else {
    SR = Actions.ActOnSpliceSpecifier(LSplice, Operand, RSplice);
  }
  if (SR.isInvalid())
    return true;
  SpliceSpecifier *Splice = SR.get();

  UnconsumeToken(end);
  Tok.setKind(tok::annot_splice);
  setSpliceAnnotation(Tok, Splice);
  Tok.setLocation(Splice->getBeginLoc());
  Tok.setAnnotationEndLoc(Splice->getEndLoc());
  PP.AnnotateCachedTokens(Tok);

  return false;
}

ExprResult Parser::ParseCXXSpliceAsExpr(SourceLocation TemplateKWLoc,
                                        bool AllowMemberReference) {
  assert(Tok.is(tok::annot_splice) && "expected a splice annotation");

  SpliceResult SR = getSpliceAnnotation(Tok);
  if (SR.isInvalid())
    return ExprError();
  SpliceSpecifier *Splice = SR.get();

  assert((!Splice->isSpecialization() || TemplateKWLoc.isValid()) &&
         "splice-specialization-specifier required leading 'template'");
  ConsumeAnnotationToken();

  return Actions.ActOnCXXSpliceExpression(TemplateKWLoc, Splice,
                                          AllowMemberReference);
}

TypeResult Parser::ParseCXXSpliceAsType(SourceLocation TypenameKWLoc,
                                        bool AllowDependent, bool Complain) {
  assert(Tok.is(tok::annot_splice) && "expected a splice annotation");

  SpliceResult SR = getSpliceAnnotation(Tok);
  if (SR.isInvalid())
    return TypeError();
  SpliceSpecifier *Splice = SR.get();

  TypeResult Result = Actions.ActOnCXXSpliceTypeSpecifier(TypenameKWLoc,
                                                          Splice, Complain);
  if (!Result.isInvalid())
    ConsumeAnnotationToken();

  return Result;
}

DeclResult Parser::ParseCXXSpliceAsNamespace() {
  assert(Tok.is(tok::annot_splice) && "expected annot_splice");

  SpliceResult SR = getSpliceAnnotation(Tok);
  if (SR.isInvalid())
    return DeclError();
  SpliceSpecifier *Splice = SR.get();

  assert(!Splice->isSpecialization() &&
         "splice-specialization-specifier cannot represent a namespace");
  ConsumeAnnotationToken();

  return Actions.ActOnCXXSpliceExpectingNamespace(Splice);
}

//===----------------------------------------------------------------------===//
// Expression macros: name!(args), name!{args}, name![args]
//===----------------------------------------------------------------------===//

static void relocateExpansionTokens(SourceManager &SM,
                                    SmallVectorImpl<Token> &Toks,
                                    SourceRange Invocation);

/// Parse the argument list of an expression-macro invocation and hand it to
/// Sema. The macro name has already been consumed; the current token is '!',
/// followed by whichever bracket the invocation chose.
ExprResult Parser::ParseMacroInvocation(CXXScopeSpec &SS,
                                        const IdentifierInfo *II,
                                        SourceLocation NameLoc) {
  assert(isMacroInvocationExclaim());

  // The macro's parameter shape decides how each argument is parsed, so the
  // macro has to be found before the arguments are read. (With a dependent
  // qualifier it cannot be, and the arguments wait for instantiation.)
  SmallVector<bool, 4> RawParams;
  bool ShapeUnknown = false;
  bool ShapeError = Actions.GetQualifiedMacroParameterShape(
      getCurScope(), SS, II, NameLoc, RawParams, ShapeUnknown);

  SourceLocation ExclaimLoc = ConsumeToken();
  BalancedDelimiterTracker T(*this, Tok.getKind());
  T.consumeOpen();

  if (ShapeError) {
    T.skipToEnd();
    return ExprError();
  }

  ExprVector Args;
  if (ParseMacroArguments(RawParams, ShapeUnknown, T, Args))
    return ExprError();
  if (T.consumeClose())
    return ExprError();

  return Actions.ActOnMacroInvocation(getCurScope(), SS, II, NameLoc,
                                      ExclaimLoc, T.getOpenLocation(), Args,
                                      T.getCloseLocation(), ShapeUnknown);
}

/// Parse 'obj.name!(args)' / 'obj->name!(args)'. The member name has been
/// consumed; the current token is '!'. The object expression binds to the
/// macro's explicit object parameter.
ExprResult Parser::ParseMemberMacroInvocation(Expr *Base, SourceLocation OpLoc,
                                              tok::TokenKind OpKind,
                                              const IdentifierInfo *II,
                                              SourceLocation NameLoc) {
  assert(isMacroInvocationExclaim());

  // If the object's class is not known yet (a dependent object expression),
  // the arguments wait for instantiation.
  SmallVector<bool, 4> RawParams;
  bool ShapeUnknown = false;
  bool ShapeError = Actions.GetMemberMacroParameterShape(
      Base, OpKind, II, NameLoc, RawParams, ShapeUnknown);

  SourceLocation ExclaimLoc = ConsumeToken();
  BalancedDelimiterTracker T(*this, Tok.getKind());
  T.consumeOpen();

  if (ShapeError) {
    T.skipToEnd();
    return ExprError();
  }

  ExprVector Args;
  if (ParseMacroArguments(RawParams, ShapeUnknown, T, Args))
    return ExprError();
  if (T.consumeClose())
    return ExprError();

  return Actions.ActOnMemberMacroInvocation(
      getCurScope(), Base, OpLoc, OpKind, II, NameLoc, ExclaimLoc,
      T.getOpenLocation(), Args, T.getCloseLocation(), ShapeUnknown);
}

/// Parse a declaration-position macro invocation, 'name!(args);', at
/// namespace or class scope, then parse the macro's expansion as a sequence
/// of declarations in place. There is no deferral: the invocation context
/// must not be dependent (a dependent context uses a consteval block with
/// queue_injection).
Parser::DeclGroupPtrTy Parser::ParseDeclMacroInvocation(AccessSpecifier AS,
                                                        Decl *TagDecl) {
  assert(isStartOfDeclMacroInvocation());

  IdentifierInfo *II = Tok.getIdentifierInfo();
  SourceLocation NameLoc = ConsumeToken();

  // The arguments are constant expressions (or raw tokens).
  EnterExpressionEvaluationContext ConstantEvaluated(
      Actions, Sema::ExpressionEvaluationContext::ConstantEvaluated);

  // The macro's parameter shape decides how each argument is parsed.
  SmallVector<bool, 4> RawParams;
  bool ShapeError;
  {
    LookupResult R(Actions, II, NameLoc, Sema::LookupOrdinaryName);
    Actions.LookupParsedName(R, getCurScope(), /*SS=*/nullptr,
                             /*ObjectType=*/QualType());
    ShapeError = Actions.GetMacroParameterShape(R, RawParams);
  }

  SourceLocation ExclaimLoc = ConsumeToken();
  BalancedDelimiterTracker T(*this, Tok.getKind());
  T.consumeOpen();

  if (ShapeError) {
    T.skipToEnd();
    TryConsumeToken(tok::semi);
    return nullptr;
  }

  ExprVector Args;
  if (ParseMacroArguments(RawParams, /*ShapeUnknown=*/false, T, Args)) {
    TryConsumeToken(tok::semi);
    return nullptr;
  }
  if (T.consumeClose())
    return nullptr;
  ExpectAndConsumeSemi(diag::err_expected_semi_declaration);

  TokenSequenceData Expansion;
  if (Actions.ActOnDeclMacroInvocation(getCurScope(), II, NameLoc, ExclaimLoc,
                                       T.getOpenLocation(), Args,
                                       T.getCloseLocation(), Expansion))
    return nullptr;

  // Parse the expansion as declarations at the current position, delimited
  // by its own eof.
  SmallVector<Token, 16> Toks(Expansion.begin(), Expansion.end());
  relocateExpansionTokens(PP.getSourceManager(), Toks,
                          SourceRange(NameLoc, T.getCloseLocation()));
  Token Eof;
  Eof.startToken();
  Eof.setKind(tok::eof);
  Eof.setLocation(T.getCloseLocation());
  Toks.push_back(Eof);

  Token SavedTok = Tok;
  PP.EnterTokenStream(Toks, /*DisableMacroExpansion=*/true,
                      /*IsReinject=*/true);
  ConsumeAnyToken();

  SmallVector<Decl *, 4> Decls;
  if (Actions.CurContext->isRecord()) {
    // Members of the class being parsed, under the current access specifier
    // (the expansion may change it; the change does not leak out).
    ParseTokensAsClassMembers(AS, TagDecl);
  } else {
    while (Tok.isNot(tok::eof)) {
      SourceLocation Before = Tok.getLocation();
      ParsedAttributes DeclAttrs(AttrFactory);
      ParsedAttributes DeclSpecAttrs(AttrFactory);
      DeclGroupPtrTy G = ParseExternalDeclaration(DeclAttrs, DeclSpecAttrs);
      if (G)
        for (Decl *D : G.get())
          Decls.push_back(D);
      // Guarantee progress on malformed tokens. (Location equality alone is
      // not proof: repeated evaluations of one token sequence share
      // locations, so only a parse that also produced nothing counts.)
      if (!G && Tok.isNot(tok::eof) && Tok.getLocation() == Before) {
        Diag(Tok, diag::err_unexpected_token_in_injected_members)
            << Tok.getKind();
        ConsumeAnyToken();
      }
    }
  }
  Tok = SavedTok;

  if (Decls.empty())
    return nullptr;
  return DeclGroupPtrTy::make(
      DeclGroupRef::Create(Actions.Context, Decls.data(), Decls.size()));
}

/// Parse the arguments of a macro invocation up to (not including) the
/// closing bracket (the one matching the invocation's opener); raw parameters
/// take their arguments as token sequences. Skips to the closing bracket and
/// returns true on error.
bool Parser::ParseMacroArguments(ArrayRef<bool> RawParams, bool ShapeUnknown,
                                 BalancedDelimiterTracker &T,
                                 ExprVector &Args) {
  tok::TokenKind Close = T.getCloseKind();
  if (ShapeUnknown) {
    ExprResult Blob = ParseMacroRawArgument(Close, /*Greedy=*/true);
    if (Blob.isInvalid()) {
      T.skipToEnd();
      return true;
    }
    Args.push_back(Blob.get());
    return false;
  }
  if (ParseMacroArgumentList(RawParams, Close, Close == tok::r_brace, Args)) {
    T.skipToEnd();
    return true;
  }
  return false;
}

bool Parser::ParseMacroArgumentList(ArrayRef<bool> RawParams,
                                    tok::TokenKind Close, bool Braced,
                                    ExprVector &Args) {
  if (Tok.isNot(Close)) {
    while (true) {
      unsigned Idx = Args.size();
      bool Raw = Idx < RawParams.size() && RawParams[Idx];
      ExprResult Arg;
      if (Raw)
        Arg = ParseMacroRawArgument(Close,
                                    /*Greedy=*/Idx + 1 == RawParams.size());
      else if (Tok.is(tok::l_brace))
        Arg = ParseBraceInitializer();
      else
        Arg = ParseAssignmentExpression();
      if (Arg.isInvalid())
        return true;
      Args.push_back(Arg.get());
      if (!TryConsumeToken(tok::comma))
        break;
      // A braced argument list permits a trailing comma, as a braced
      // initializer list does; the call-like brackets do not, as a call does
      // not. (Requires at least one argument: 'f!{,}' is not an empty list.
      // And a *greedy* raw parameter never reaches here -- it swallows
      // top-level commas, so its trailing comma is tokens, not sugar.)
      if (Braced && Tok.is(Close))
        break;
    }
  }
  // name!() is an empty argument list, never a single empty token sequence;
  // a raw parameter that wants to permit an empty invocation declares a
  // default argument.
  return false;
}

/// Capture the tokens of a raw (token_sequence) macro argument: balanced
/// parentheses, brackets and braces, ending at a top-level comma (unless the
/// parameter is the last one and therefore greedy) or at \p Close, the
/// bracket that closes the invocation's argument list.
ExprResult Parser::ParseMacroRawArgument(tok::TokenKind Close, bool Greedy) {
  SmallVector<Token, 16> Tokens;
  SourceLocation StartLoc = Tok.getLocation();
  SourceLocation EndLoc = StartLoc;
  SmallVector<tok::TokenKind, 4> Closers;
  while (true) {
    // (\p Close is eof when re-parsing a captured argument list.)
    if (Closers.empty() && (Tok.is(Close) || (Tok.is(tok::comma) && !Greedy)))
      break;
    if (Tok.is(tok::eof)) {
      Diag(Tok, diag::err_expected)
          << (Closers.empty() ? Close : Closers.back());
      return ExprError();
    }
    if (Tok.is(tok::l_paren)) {
      Closers.push_back(tok::r_paren);
    } else if (Tok.is(tok::l_square)) {
      Closers.push_back(tok::r_square);
    } else if (Tok.is(tok::l_brace)) {
      Closers.push_back(tok::r_brace);
    } else if (Tok.isOneOf(tok::r_paren, tok::r_square, tok::r_brace)) {
      // An unmatched closer of another kind is the wrong bracket for this
      // invocation: 'id!(1]'.
      if (Closers.empty() || Tok.isNot(Closers.back())) {
        Diag(Tok, diag::err_expected)
            << (Closers.empty() ? Close : Closers.back());
        return ExprError();
      }
      Closers.pop_back();
    }
    Tokens.push_back(Tok);
    EndLoc = Tok.getLocation();
    ConsumeAnyToken();
  }
  return Actions.ActOnCXXTokenSequenceReflection(
      getCurScope(), StartLoc, SourceRange(StartLoc, EndLoc), Tokens);
}

bool Parser::ParseDeferredMacroArguments(ArrayRef<bool> RawParams, bool Braced,
                                         TokenSequenceData TSD,
                                         SourceLocation Loc, DeclContext *Ctx,
                                         ArrayRef<NamedDecl *> TemplateParams,
                                         SmallVectorImpl<Expr *> &Args) {
  // The arguments are parsed where they were written, as part of the
  // template: re-enter the template scopes and contexts around \p Ctx, as
  // for a late-parsed template, so that names bind as they would have there.
  // The chain is rooted at the translation unit's scope, not the current
  // one: whatever is being parsed now is unrelated to the template.
  Scope *SavedScope = Actions.CurScope;
  Scope *Root = SavedScope;
  while (Root->getParent())
    Root = Root->getParent();
  Actions.CurScope = Root;
  auto RestoreScope =
      llvm::make_scope_exit([&] { Actions.CurScope = SavedScope; });

  TemplateParameterDepthRAII DepthTracker(TemplateParameterDepth);
  Sema::ContextRAII SavedContext(Actions,
                                 Actions.Context.getTranslationUnitDecl());
  MultiParseScope Scopes(*this);
  SmallVector<DeclContext *, 4> Contexts;
  for (DeclContext *DC = Ctx; DC && !DC->isTranslationUnit();
       DC = DC->getLexicalParent())
    Contexts.push_back(DC);
  for (DeclContext *DC : llvm::reverse(Contexts)) {
    DepthTracker.addDepth(ReenterTemplateScopes(Scopes, cast<Decl>(DC)));
    Scopes.Enter(Scope::DeclScope);
    // The innermost function gets its own function scope below.
    if (DC != Ctx || !DC->isFunctionOrMethod())
      getCurScope()->setEntity(DC);
    Actions.CurContext = DC;
  }

  // Template parameters of an enclosing template that is not a context (a
  // variable or alias template, say) were not re-entered.
  llvm::SmallPtrSet<Decl *, 8> Visible;
  for (Scope *S = getCurScope(); S; S = S->getParent())
    if (S->isTemplateParamScope())
      Visible.insert(S->decls().begin(), S->decls().end());
  SmallVector<NamedDecl *, 4> Extra;
  for (NamedDecl *P : TemplateParams)
    if (P->getDeclName() && !Visible.count(P))
      Extra.push_back(P);
  Scope *ExtraScope = nullptr;
  if (!Extra.empty()) {
    Scopes.Enter(Scope::TemplateParamScope);
    DepthTracker.addDepth(1);
    ExtraScope = getCurScope();
    for (NamedDecl *P : Extra) {
      ExtraScope->AddDecl(P);
      Actions.IdResolver.AddDecl(P);
    }
  }
  // They are not ours to pop (with the scope) off their identifiers.
  auto RemoveExtra = llvm::make_scope_exit([&] {
    for (NamedDecl *P : Extra) {
      ExtraScope->RemoveDecl(P);
      Actions.IdResolver.RemoveDecl(P);
    }
  });

  // A function scope of its own, for anything (a lambda, say) the
  // arguments contain; and one for each enclosing lambda, for captures of
  // what it encloses. (The captures are redone when the arguments are
  // substituted into; the local instantiation scope keeps what rebuilding
  // the lambdas records out of the one being instantiated.)
  LocalInstantiationScope CaptureScope(Actions,
                                       /*CombineWithOuterScope=*/true);
  Actions.PushFunctionScope();
  unsigned NumFunctionScopes = 1;
  for (DeclContext *DC : llvm::reverse(Contexts))
    if (isLambdaCallOperator(DC)) {
      Actions.CurContext = DC;
      Actions.RebuildLambdaScopeInfo(cast<CXXMethodDecl>(DC));
      ++NumFunctionScopes;
    }
  Actions.CurContext = Ctx;
  auto PopFunctionScopes = llvm::make_scope_exit([&] {
    while (NumFunctionScopes--)
      Actions.PopFunctionScopeInfo();
  });

  // The tokens were captured at the invocation and keep their locations.
  SmallVector<Token, 16> Toks(TSD.begin(), TSD.end());
  Token Eof;
  Eof.startToken();
  Eof.setKind(tok::eof);
  Eof.setLocation(Loc);
  Toks.push_back(Eof);

  Token SavedTok = Tok;
  PP.EnterTokenStream(Toks, /*DisableMacroExpansion=*/true,
                      /*IsReinject=*/true);
  ConsumeAnyToken();

  bool Invalid;
  {
    MacroExpansionLookupScope LookupScope(*this);
    GreaterThanIsOperatorScope G(GreaterThanIsOperator, true);
    ColonProtectionRAIIObject ColonProtection(*this, false);
    ExprVector Parsed;
    Invalid = ParseMacroArgumentList(RawParams, tok::eof, Braced, Parsed);
    if (!Invalid && Tok.isNot(tok::eof)) {
      Diag(Tok, diag::err_expected) << tok::comma;
      Invalid = true;
    }
    Args.append(Parsed.begin(), Parsed.end());
  }
  while (Tok.isNot(tok::eof))
    ConsumeAnyToken();

  Tok = SavedTok;
  return Invalid;
}

bool Parser::DeferredMacroArgumentsCallback(
    void *P, ArrayRef<bool> RawParams, bool Braced, TokenSequenceData TSD,
    SourceLocation Loc, DeclContext *Ctx, ArrayRef<NamedDecl *> TemplateParams,
    SmallVectorImpl<Expr *> &Args) {
  return static_cast<Parser *>(P)->ParseDeferredMacroArguments(
      RawParams, Braced, TSD, Loc, Ctx, TemplateParams, Args);
}

ExprResult Parser::ExpressionMacroExpansionCallback(void *P,
                                                    TokenSequenceData TSD,
                                                    SourceRange Invocation) {
  return static_cast<Parser *>(P)->ParseExpressionMacroExpansion(TSD,
                                                                 Invocation);
}

ExprResult Parser::SpeculativeExpressionCallback(void *P,
                                                 TokenSequenceData TSD,
                                                 SourceRange Invocation) {
  return static_cast<Parser *>(P)->ParseExpressionMacroExpansion(
      TSD, Invocation, /*Speculative=*/true);
}

/// Locate a macro expansion's tokens as expanded at \p Invocation (the
/// macro name through the closing bracket), spelled where the macro wrote
/// them. A diagnostic inside the expansion then points at the invocation,
/// with a "expanded from macro 'name'" note into the body -- exactly the
/// presentation of a preprocessor macro, and via the same SourceManager
/// machinery (the note's name is read from the expansion range's first
/// token, which is why the range starts at the name). Tokens that already
/// lie within the invocation -- a raw token argument's -- and interpolated
/// argument expressions (located at the argument) are left where they are.
static void relocateExpansionTokens(SourceManager &SM,
                                    SmallVectorImpl<Token> &Toks,
                                    SourceRange Invocation) {
  if (Invocation.isInvalid())
    return;
  SourceLocation Begin = Invocation.getBegin(), End = Invocation.getEnd();
  SourceLocation FileBegin = SM.getFileLoc(Begin), FileEnd = SM.getFileLoc(End);

  auto Relocates = [&](const Token &Tok) {
    SourceLocation Loc = Tok.getLocation();
    return Loc.isValid() && Tok.isNot(tok::annot_primary_expr) &&
           !SM.isPointWithin(SM.getFileLoc(Loc), FileBegin, FileEnd);
  };
  // A token's extent within its spelling buffer. (An annotation spans from
  // its location to its end location; a different buffer for the end means
  // it is treated as one character.)
  auto Extent = [&](const Token &Tok) -> std::pair<unsigned, unsigned> {
    auto [FID, Off] = SM.getDecomposedLoc(Tok.getLocation());
    if (!Tok.isAnnotation())
      return {Off, Off + Tok.getLength()};
    if (SourceLocation EndLoc = Tok.getAnnotationEndLoc(); EndLoc.isValid()) {
      auto [EndFID, EndOff] = SM.getDecomposedLoc(EndLoc);
      if (EndFID == FID && EndOff >= Off)
        return {Off, EndOff + 1};
    }
    return {Off, Off + 1};
  };

  // One expansion per spelling buffer, covering every relocated token in it
  // (as the preprocessor makes one expansion of a macro's whole body), so
  // consecutive tokens share an expansion context and each keeps its offset
  // within the body. A token then maps to its spelling by offset.
  struct Span {
    unsigned MinOff = ~0u, MaxEnd = 0;
    SourceLocation Base;
  };
  llvm::SmallDenseMap<FileID, Span, 4> Spans;
  for (const Token &Tok : Toks) {
    if (!Relocates(Tok))
      continue;
    Span &S = Spans[SM.getFileID(Tok.getLocation())];
    auto [Off, EndOff] = Extent(Tok);
    S.MinOff = std::min(S.MinOff, Off);
    S.MaxEnd = std::max(S.MaxEnd, EndOff);
  }
  for (auto &[FID, S] : Spans)
    S.Base = SM.createExpansionLoc(SM.getComposedLoc(FID, S.MinOff), Begin,
                                   End, S.MaxEnd - S.MinOff);
  for (Token &Tok : Toks) {
    if (!Relocates(Tok))
      continue;
    const Span &S = Spans.find(SM.getFileID(Tok.getLocation()))->second;
    auto [Off, EndOff] = Extent(Tok);
    if (Tok.isAnnotation())
      Tok.setAnnotationEndLoc(S.Base.getLocWithOffset(EndOff - 1 - S.MinOff));
    Tok.setLocation(S.Base.getLocWithOffset(Off - S.MinOff));
  }
}

/// Parse the token sequence produced by an expression macro as a single
/// expression, in place of the invocation. In speculative mode
/// (std::meta::test_expression) all diagnostics are suppressed and validity
/// is reported only through the result.
ExprResult Parser::ParseExpressionMacroExpansion(TokenSequenceData TSD,
                                                 SourceRange Invocation,
                                                 bool Speculative) {
  SourceLocation Loc = Invocation.getEnd();
  // The probe must not commit Sema to anything diagnostic-visible: parser
  // and Sema errors are suppressed wholesale (the SFINAETrap additionally
  // keeps Sema's error bookkeeping balanced), typo correction is disabled
  // (a "corrected" expression is not the expression that was asked about),
  // and failure is reported by an invalid or error-containing result or by
  // the trap having fired.
  std::optional<Sema::SFINAETrap> Trap;
  DiagnosticsEngine &Diags = Actions.getDiagnostics();
  bool OldSuppress = Diags.getSuppressAllDiagnostics();
  bool OldDisableTypo = Actions.DisableTypoCorrection;
  if (Speculative) {
    Trap.emplace(Actions, /*ForValidityCheck=*/true);
    Actions.DisableTypoCorrection = true;
    Diags.setSuppressAllDiagnostics(true);
  }
  auto RestoreDiags = llvm::make_scope_exit([&] {
    if (Speculative) {
      Diags.setSuppressAllDiagnostics(OldSuppress);
      Actions.DisableTypoCorrection = OldDisableTypo;
    }
  });

  SmallVector<Token, 16> Toks(TSD.begin(), TSD.end());
  // A probe (test_expression) has no invocation of its own to be expanded
  // at; its tokens keep their locations.
  if (!Speculative)
    relocateExpansionTokens(PP.getSourceManager(), Toks, Invocation);
  Token Eof;
  Eof.startToken();
  Eof.setKind(tok::eof);
  Eof.setLocation(Loc);
  Toks.push_back(Eof);

  Token SavedTok = Tok;
  PP.EnterTokenStream(Toks, /*DisableMacroExpansion=*/true,
                      /*IsReinject=*/true);
  ConsumeAnyToken();

  MacroExpansionLookupScope LookupScope(*this);

  // The expansion is delimited by its own eof, so it is parsed free of the
  // enclosing context's bracket rules.
  ExprResult Result;
  {
    GreaterThanIsOperatorScope G(GreaterThanIsOperator, true);
    ColonProtectionRAIIObject ColonProtection(*this, false);
    Result = ParseExpression();
  }
  if (!Result.isInvalid() && Tok.isNot(tok::eof)) {
    Diag(Tok, diag::err_macro_expansion_not_single_expression);
    Result = ExprError();
  }
  // Any error the trap absorbed invalidates the probe, even if Sema
  // recovered a superficially usable expression.
  if (Speculative && Trap->hasErrorOccurred())
    Result = ExprError();
  // Drain what is left so the enclosing token stream resumes cleanly.
  while (Tok.isNot(tok::eof))
    ConsumeAnyToken();

  Tok = SavedTok;
  return Result;
}

Parser::MacroExpansionLookupScope::MacroExpansionLookupScope(Parser &P) : P(P) {
  Sema &Actions = P.Actions;
  DeclContext *DC = Actions.CurContext;
  if (!DC->isFunctionOrMethod())
    return;
  for (Scope *S = P.getCurScope(); S; S = S->getParent())
    if (S->getEntity() == DC)
      return;

  FnScope.emplace(&P,
                  Scope::FnScope | Scope::DeclScope | Scope::CompoundStmtScope);
  P.getCurScope()->setEntity(DC);
  if (auto *FD = dyn_cast<FunctionDecl>(DC))
    for (ParmVarDecl *Param : FD->parameters())
      seed(Param, Base);
  for (NamedDecl *D : Actions.InjectedLocalDeclsForLookup)
    seed(D, Base);
  // Each level of locals Sema collected gets its own nested scope, so an
  // inner declaration hides an outer one the way it did at the invocation.
  for (const auto &Level : Actions.MacroExpansionLocalScopes) {
    if (Level.empty())
      continue;
    P.EnterScope(Scope::DeclScope);
    Levels.emplace_back();
    for (NamedDecl *D : Level)
      seed(D, Levels.back());
  }
}

void Parser::MacroExpansionLookupScope::seed(
    NamedDecl *D, SmallVectorImpl<NamedDecl *> &Out) {
  if (!D->getDeclName() || !Seen.insert(D).second)
    return;
  P.getCurScope()->AddDecl(D);
  P.Actions.IdResolver.AddDecl(D);
  Out.push_back(D);
}

Parser::MacroExpansionLookupScope::~MacroExpansionLookupScope() {
  for (unsigned I = Levels.size(); I--;) {
    for (NamedDecl *D : Levels[I]) {
      P.getCurScope()->RemoveDecl(D);
      P.Actions.IdResolver.RemoveDecl(D);
    }
    P.ExitScope();
  }
  for (NamedDecl *D : Base) {
    P.getCurScope()->RemoveDecl(D);
    P.Actions.IdResolver.RemoveDecl(D);
  }
}

//===----------------------------------------------------------------------===//
// Mem-initializer macros: C(...) : name!(args), ... { }
//===----------------------------------------------------------------------===//

bool Parser::ParseMemInitializerOrMacro(
    Decl *ConstructorDecl, SmallVectorImpl<CXXCtorInitializer *> &MemInits) {
  // '::'[opt] nested-name-specifier[opt], shared by both forms.
  CXXScopeSpec SS;
  if (ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/nullptr,
                                     /*ObjectHasErrors=*/false,
                                     /*EnteringContext=*/false))
    return true;

  if (getLangOpts().Reflection && Tok.is(tok::identifier) &&
      NextToken().is(tok::exclaim) &&
      isMacroArgumentListOpener(GetLookAheadToken(2).getKind()))
    return ParseMemInitMacroInvocation(ConstructorDecl, SS, MemInits);

  MemInitResult MemInit = ParseMemInitializer(ConstructorDecl, SS);
  if (MemInit.isInvalid())
    return true;
  MemInits.push_back(MemInit.get());
  return false;
}

bool Parser::ParseMemInitMacroInvocation(
    Decl *ConstructorDecl, CXXScopeSpec &SS,
    SmallVectorImpl<CXXCtorInitializer *> &MemInits) {
  assert(Tok.is(tok::identifier) && NextToken().is(tok::exclaim));

  IdentifierInfo *II = Tok.getIdentifierInfo();
  SourceLocation NameLoc = ConsumeToken();
  SourceLocation StartLoc = SS.isEmpty() ? NameLoc : SS.getBeginLoc();

  // The macro's parameter shape decides how each argument is parsed.
  SmallVector<bool, 4> RawParams;
  bool ShapeUnknown = false;
  // An invalid qualifier has already been diagnosed.
  bool ShapeError = SS.isInvalid() || Actions.GetQualifiedMacroParameterShape(
                                          getCurScope(), SS, II, NameLoc,
                                          RawParams, ShapeUnknown);

  SourceLocation ExclaimLoc = ConsumeToken();
  BalancedDelimiterTracker T(*this, Tok.getKind());
  T.consumeOpen();

  if (ShapeError) {
    T.skipToEnd();
    return true;
  }

  ExprVector Args;
  if (ParseMacroArguments(RawParams, ShapeUnknown, T, Args))
    return true;
  if (T.consumeClose())
    return true;

  TokenSequenceData Expansion;
  bool Deferred = false;
  if (Actions.ActOnMemInitMacroInvocation(
          getCurScope(), ConstructorDecl, MemInits.size(), SS, II, NameLoc,
          ExclaimLoc, T.getOpenLocation(), Args, T.getCloseLocation(),
          ShapeUnknown, Expansion, Deferred))
    return true;
  if (Deferred)
    return false;

  return ParseMemInitMacroExpansion(ConstructorDecl, Expansion,
                                    SourceRange(StartLoc, T.getCloseLocation()),
                                    MemInits);
}

bool Parser::ParseMemInitializersUntilEof(
    Decl *ConstructorDecl, SmallVectorImpl<CXXCtorInitializer *> &MemInits) {
  // Unlike a written mem-initializer-list, an expansion may be empty: the
  // invocation itself is what the source wrote.
  if (Tok.is(tok::eof))
    return false;

  while (true) {
    // The rest of a malformed expansion is not worth diagnosing.
    if (ParseMemInitializerOrMacro(ConstructorDecl, MemInits))
      return true;

    if (Tok.is(tok::eof))
      break;
    if (!TryConsumeToken(tok::comma)) {
      Diag(Tok, diag::err_expected) << tok::comma;
      return true;
    }
    // No trailing comma: 'a(x),' is not a mem-initializer-list.
    if (Tok.is(tok::eof)) {
      Diag(Tok, diag::err_expected_member_or_base_name);
      return true;
    }
  }
  return false;
}

bool Parser::ParseMemInitMacroExpansion(
    Decl *ConstructorDecl, TokenSequenceData TSD, SourceRange Invocation,
    SmallVectorImpl<CXXCtorInitializer *> &Out) {
  SmallVector<Token, 16> Toks(TSD.begin(), TSD.end());
  relocateExpansionTokens(PP.getSourceManager(), Toks, Invocation);
  Token Eof;
  Eof.startToken();
  Eof.setKind(tok::eof);
  Eof.setLocation(Invocation.getEnd());
  Toks.push_back(Eof);

  Token SavedTok = Tok;
  PP.EnterTokenStream(Toks, /*DisableMacroExpansion=*/true,
                      /*IsReinject=*/true);
  ConsumeAnyToken();

  bool Invalid;
  {
    MacroExpansionLookupScope LookupScope(*this);
    GreaterThanIsOperatorScope G(GreaterThanIsOperator, true);
    ColonProtectionRAIIObject ColonProtection(*this, false);
    PoisonSEHIdentifiersRAIIObject PoisonSEHIdentifiers(*this, true);
    Invalid = ParseMemInitializersUntilEof(ConstructorDecl, Out);
  }
  // Drain what is left so the enclosing token stream resumes cleanly.
  while (Tok.isNot(tok::eof))
    ConsumeAnyToken();

  Tok = SavedTok;
  return Invalid;
}

bool Parser::MemInitMacroExpansionCallback(
    void *P, Decl *ConstructorDecl, TokenSequenceData TSD,
    SourceRange Invocation, SmallVectorImpl<CXXCtorInitializer *> &Out) {
  return static_cast<Parser *>(P)->ParseMemInitMacroExpansion(
      ConstructorDecl, TSD, Invocation, Out);
}
