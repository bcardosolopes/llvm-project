//===- StdHelpers.h - Helpers for standard types/functions ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef DIALECT_CIR_TRANSFORMS_STDHELPERS_H_
#define DIALECT_CIR_TRANSFORMS_STDHELPERS_H_

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Interfaces/ASTAttrInterfaces.h"

namespace cir {

bool isStdArrayType(mlir::Type t);

} // namespace cir

#endif
