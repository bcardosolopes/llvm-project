//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the LoweringPrepareItaniumCXXABI class, which is the
// Itanium C++ ABI specific implementation of LoweringPrepareCXXABI.
//
//===----------------------------------------------------------------------===//

#ifndef CIR_DIALECT_TRANSFORMS__LOWERINGPREPAREINTANIUMCXXABI_H
#define CIR_DIALECT_TRANSFORMS__LOWERINGPREPAREINTANIUMCXXABI_H

#include "LoweringPrepareCXXABI.h"
#include "clang/CIR/Dialect/IR/CIRDataLayout.h"

class LoweringPrepareItaniumCXXABI : public cir::LoweringPrepareCXXABI {
public:
  mlir::Value lowerDynamicCast(cir::CIRBaseBuilderTy &builder,
                               clang::ASTContext &astCtx,
                               cir::DynamicCastOp op) override;

  mlir::Value lowerVAArg(cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
                         const cir::CIRDataLayout &datalayout) override {
    // Generic Itanium does not lower va_arg at CIR level.
    return {};
  }
};

#endif // CIR_DIALECT_TRANSFORMS__LOWERINGPREPAREINTANIUMCXXABI_H
