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

}  // end namespace clang
