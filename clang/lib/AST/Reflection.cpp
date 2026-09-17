//===--- Reflection.cpp - Classes for representing reflection ---*- C++ -*-===//
//
// Copyright 2024 Bloomberg Finance L.P.
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the ReflectionValue class.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Reflection.h"
#include "clang/AST/ASTContext.h"
#include "clang/Lex/Token.h"

namespace clang {

TokenSequenceData CreateTokenSequenceData(ASTContext &Ctx,
                                          ArrayRef<Token> Tokens) {
  Token *StoredTokens = nullptr;
  if (!Tokens.empty()) {
    StoredTokens = new (Ctx) Token[Tokens.size()];
    std::copy(Tokens.begin(), Tokens.end(), StoredTokens);
  }

  return TokenSequenceData(StoredTokens, Tokens.size());
}

TokenSequenceData CreateTokenSequenceData(ASTContext &Ctx,
                                          ArrayRef<Token> Tokens1,
                                          ArrayRef<Token> Tokens2) {

  Token *StoredTokens = nullptr;
  size_t Size = Tokens1.size() + Tokens2.size();
  if (Size != 0) {
    StoredTokens = new (Ctx) Token[Size];
    Token *Next = std::copy(Tokens1.begin(), Tokens1.end(), StoredTokens);
    std::copy(Tokens2.begin(), Tokens2.end(), Next);
  }

  return TokenSequenceData(StoredTokens, Size);
}

bool TagDataMemberSpec::operator==(TagDataMemberSpec const &Rhs) const {
  return (Ty == Rhs.Ty &&
          Alignment == Rhs.Alignment &&
          BitWidth == Rhs.BitWidth &&
          Name == Rhs.Name);
}

bool TagDataMemberSpec::operator!=(TagDataMemberSpec const &Rhs) const {
  return !(*this == Rhs);
}

bool FunctionDeclSpec::operator==(FunctionDeclSpec const &Rhs) const {
  return (Source == Rhs.Source &&
          Name == Rhs.Name &&
          TemplateParameterPrefix == Rhs.TemplateParameterPrefix &&
          ParameterPrefix == Rhs.ParameterPrefix &&
          MarkOverride == Rhs.MarkOverride &&
          MarkNoexcept == Rhs.MarkNoexcept);
}

bool FunctionDeclSpec::operator!=(FunctionDeclSpec const &Rhs) const {
  return !(*this == Rhs);
}

}  // end namespace clang

namespace clang {
static constexpr OverloadedOperatorKind MetaOperatorOrder[] = {
    OO_None, OO_New, OO_Delete, OO_Array_New, OO_Array_Delete, OO_Coawait,
    OO_Call, OO_Subscript, OO_Arrow, OO_ArrowStar, OO_Tilde, OO_Exclaim,
    OO_Plus, OO_Minus, OO_Star, OO_Slash, OO_Percent, OO_Caret, OO_Amp,
    OO_Pipe, OO_Equal, OO_PlusEqual, OO_MinusEqual, OO_StarEqual,
    OO_SlashEqual, OO_PercentEqual, OO_CaretEqual, OO_AmpEqual, OO_PipeEqual,
    OO_EqualEqual, OO_ExclaimEqual, OO_Less, OO_Greater, OO_LessEqual,
    OO_GreaterEqual, OO_Spaceship, OO_AmpAmp, OO_PipePipe, OO_LessLess,
    OO_GreaterGreater, OO_LessLessEqual, OO_GreaterGreaterEqual, OO_PlusPlus,
    OO_MinusMinus, OO_Comma,
};

OverloadedOperatorKind getOverloadedOperatorForMetaIndex(unsigned Index) {
  return Index < std::size(MetaOperatorOrder) ? MetaOperatorOrder[Index]
                                              : OO_None;
}

QualType stripDeducedTypeSugar(const ASTContext &C, QualType T) {
  if (const auto *DT = dyn_cast<DeducedType>(T)) {
    if (!DT->isDeduced())
      return T;
    return C.getQualifiedType(DT->getDeducedType(), T.getLocalQualifiers());
  }
  // A placeholder buried under a reference or pointer ('auto&&' deduced as
  // 'int&'): only the canonical type is free of it.
  if (const DeducedType *DT = T->getContainedDeducedType();
      DT && DT->isDeduced())
    return C.getCanonicalType(T);
  return T;
}

OverloadedOperatorKind getOverloadedOperatorForTokenKind(tok::TokenKind Kind) {
  switch (Kind) {
#define OVERLOADED_OPERATOR(Name, Spelling, Token, Unary, Binary, MemberOnly)  \
  case tok::Token:                                                             \
    return OO_##Name;
#define OVERLOADED_OPERATOR_MULTI(Name, Spelling, Unary, Binary, MemberOnly)
#include "clang/Basic/OperatorKinds.def"
  default:
    return OO_None;
  }
}

unsigned getMetaIndexForOverloadedOperator(OverloadedOperatorKind OO) {
  const auto *It = llvm::find(MetaOperatorOrder, OO);
  return It == std::end(MetaOperatorOrder) ? 0 : It - MetaOperatorOrder;
}

} // namespace clang
