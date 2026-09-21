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
#include "clang/Sema/Lookup.h"
#include "llvm/ADT/ScopeExit.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
using namespace clang;

ExprResult Parser::ParseCXXReflectExpression(SourceLocation OpLoc) {
  SourceLocation OperandLoc = Tok.getLocation();

  // Handle token sequence: ^^{ balanced tokens }
  if (Tok.is(tok::l_brace)) {
    SourceLocation LBraceLoc = ConsumeBrace();

    SmallVector<Token, 16> Tokens;
    unsigned BraceDepth = 1;
    while (BraceDepth > 0 && Tok.isNot(tok::eof)) {
      if (Tok.is(tok::l_brace))
        ++BraceDepth;
      else if (Tok.is(tok::r_brace)) {
        --BraceDepth;
        if (BraceDepth == 0)
          break;
      }

      // Check for interpolation: \(expr)
      if (Tok.is(tok::unknown) && Tok.getLength() == 1 &&
          *PP.getSourceManager().getCharacterData(Tok.getLocation()) == '\\' &&
          NextToken().is(tok::l_paren)) {
        SourceLocation BackslashLoc = Tok.getLocation();
        // Preserve leading space from the backslash token so that stringize
        // can correctly reproduce whitespace (e.g., "int \(id("x"))" should
        // become "int x", not "intx").
        bool HadLeadingSpace = Tok.hasLeadingSpace();
        ConsumeToken();  // consume '\'
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
    return Actions.ActOnCXXTokenSequenceReflection(OpLoc, OperandRange,
                                                   Tokens);
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

/// Parse the argument list of an expression-macro invocation and hand it to
/// Sema. The macro name has already been consumed; the current token is '!',
/// followed by whichever bracket the invocation chose.
ExprResult Parser::ParseMacroInvocation(CXXScopeSpec &SS,
                                        const IdentifierInfo *II,
                                        SourceLocation NameLoc) {
  assert(isMacroInvocationExclaim());

  // The macro's parameter shape decides how each argument is parsed, so the
  // macro has to be found before the arguments are read.
  SmallVector<bool, 4> RawParams;
  bool ShapeError;
  {
    LookupResult R(Actions, II, NameLoc, Sema::LookupOrdinaryName);
    Actions.LookupParsedName(R, getCurScope(), &SS, /*ObjectType=*/QualType());
    ShapeError = Actions.GetMacroParameterShape(R, RawParams);
  }

  SourceLocation ExclaimLoc = ConsumeToken();
  BalancedDelimiterTracker T(*this, Tok.getKind());
  T.consumeOpen();

  if (ShapeError) {
    T.skipToEnd();
    return ExprError();
  }

  ExprVector Args;
  if (ParseMacroArguments(RawParams, T, Args))
    return ExprError();
  if (T.consumeClose())
    return ExprError();

  return Actions.ActOnMacroInvocation(getCurScope(), SS, II, NameLoc,
                                      ExclaimLoc, T.getOpenLocation(), Args,
                                      T.getCloseLocation());
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
  // every argument is parsed as an expression.
  SmallVector<bool, 4> RawParams;
  bool ShapeError =
      Actions.GetMemberMacroParameterShape(Base, OpKind, II, NameLoc, RawParams);

  SourceLocation ExclaimLoc = ConsumeToken();
  BalancedDelimiterTracker T(*this, Tok.getKind());
  T.consumeOpen();

  if (ShapeError) {
    T.skipToEnd();
    return ExprError();
  }

  ExprVector Args;
  if (ParseMacroArguments(RawParams, T, Args))
    return ExprError();
  if (T.consumeClose())
    return ExprError();

  return Actions.ActOnMemberMacroInvocation(
      getCurScope(), Base, OpLoc, OpKind, II, NameLoc, ExclaimLoc,
      T.getOpenLocation(), Args, T.getCloseLocation());
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
  if (ParseMacroArguments(RawParams, T, Args)) {
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
bool Parser::ParseMacroArguments(ArrayRef<bool> RawParams,
                                 BalancedDelimiterTracker &T,
                                 ExprVector &Args) {
  tok::TokenKind Close = T.getCloseKind();
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
      if (Arg.isInvalid()) {
        T.skipToEnd();
        return true;
      }
      Args.push_back(Arg.get());
      if (!TryConsumeToken(tok::comma))
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
    if (Tok.is(tok::eof)) {
      Diag(Tok, diag::err_expected)
          << (Closers.empty() ? Close : Closers.back());
      return ExprError();
    }
    if (Closers.empty() &&
        (Tok.is(Close) || (Tok.is(tok::comma) && !Greedy)))
      break;
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
      StartLoc, SourceRange(StartLoc, EndLoc), Tokens);
}

ExprResult Parser::ExpressionMacroExpansionCallback(void *P,
                                                    TokenSequenceData TSD,
                                                    SourceLocation Loc) {
  return static_cast<Parser *>(P)->ParseExpressionMacroExpansion(TSD, Loc);
}

ExprResult Parser::SpeculativeExpressionCallback(void *P,
                                                 TokenSequenceData TSD,
                                                 SourceLocation Loc) {
  return static_cast<Parser *>(P)->ParseExpressionMacroExpansion(
      TSD, Loc, /*Speculative=*/true);
}

/// Parse the token sequence produced by an expression macro as a single
/// expression, in place of the invocation. In speculative mode
/// (std::meta::test_expression) all diagnostics are suppressed and validity
/// is reported only through the result.
ExprResult Parser::ParseExpressionMacroExpansion(TokenSequenceData TSD,
                                                 SourceLocation Loc,
                                                 bool Speculative) {
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
  Token Eof;
  Eof.startToken();
  Eof.setKind(tok::eof);
  Eof.setLocation(Loc);
  Toks.push_back(Eof);

  Token SavedTok = Tok;
  PP.EnterTokenStream(Toks, /*DisableMacroExpansion=*/true,
                      /*IsReinject=*/true);
  ConsumeAnyToken();

  // During template instantiation the parser is not positioned inside the
  // function being instantiated. Give the expansion a function scope with the
  // instantiated parameters and the locals Sema collected as visible at the
  // invocation, so unqualified names resolve as they would have in the
  // template.
  auto HasScopeFor = [&](DeclContext *DC) {
    for (Scope *S = getCurScope(); S; S = S->getParent())
      if (S->getEntity() == DC)
        return true;
    return false;
  };
  std::optional<ParseScope> FnScope;
  llvm::SmallPtrSet<NamedDecl *, 16> SeenSeeded;
  SmallVector<NamedDecl *, 8> SeededBase;
  SmallVector<SmallVector<NamedDecl *, 4>, 4> SeededLevels;
  unsigned NumLevelScopes = 0;
  if (Actions.CurContext->isFunctionOrMethod() &&
      !HasScopeFor(Actions.CurContext)) {
    FnScope.emplace(this, Scope::FnScope | Scope::DeclScope |
                              Scope::CompoundStmtScope);
    getCurScope()->setEntity(Actions.CurContext);
    auto SeedInto = [&](NamedDecl *D, SmallVectorImpl<NamedDecl *> &Out) {
      if (!D->getDeclName() || !SeenSeeded.insert(D).second)
        return;
      getCurScope()->AddDecl(D);
      Actions.IdResolver.AddDecl(D);
      Out.push_back(D);
    };
    if (auto *FD = dyn_cast<FunctionDecl>(Actions.CurContext))
      for (ParmVarDecl *P : FD->parameters())
        SeedInto(P, SeededBase);
    for (NamedDecl *D : Actions.InjectedLocalDeclsForLookup)
      SeedInto(D, SeededBase);
    // Each level of locals Sema collected gets its own nested scope, so an
    // inner declaration hides an outer one the way it did at the invocation.
    for (const auto &Level : Actions.MacroExpansionLocalScopes) {
      if (Level.empty())
        continue;
      EnterScope(Scope::DeclScope);
      ++NumLevelScopes;
      SeededLevels.emplace_back();
      for (NamedDecl *D : Level)
        SeedInto(D, SeededLevels.back());
    }
  }
  auto Unseed = llvm::make_scope_exit([&] {
    for (unsigned I = NumLevelScopes; I--;) {
      for (NamedDecl *D : SeededLevels[I]) {
        getCurScope()->RemoveDecl(D);
        Actions.IdResolver.RemoveDecl(D);
      }
      ExitScope();
    }
    for (NamedDecl *D : SeededBase) {
      getCurScope()->RemoveDecl(D);
      Actions.IdResolver.RemoveDecl(D);
    }
  });

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
