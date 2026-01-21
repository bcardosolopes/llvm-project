//===- StdHelpers.cpp - Implementation standard related helpers--*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "StdHelpers.h"

namespace cir {

bool isStdArrayType(mlir::Type t) {
  auto sTy = mlir::dyn_cast<RecordType>(t);
  if (!sTy)
    return false;
  auto recordDecl = sTy.getAst();
  if (!recordDecl || !recordDecl.isInStdNamespace())
    return false;

  if (recordDecl.getName().compare("array") != 0)
    return false;

  return true;
}

} // namespace cir
