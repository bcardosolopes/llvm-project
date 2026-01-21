//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares an interface for running CIR-to-CIR passes.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_CIRTOCIRPASSES_H
#define CLANG_CIR_CIRTOCIRPASSES_H

#include "mlir/Pass/Pass.h"
#include "llvm/ADT/StringRef.h"

#include <memory>

namespace clang {
class ASTContext;
}

namespace mlir {
class MLIRContext;
class ModuleOp;
} // namespace mlir

namespace cir {

// Run set of cleanup/prepare/etc passes CIR <-> CIR.
mlir::LogicalResult runCIRToCIRPasses(
    mlir::ModuleOp theModule, mlir::MLIRContext &mlirCtx,
    clang::ASTContext &astCtx, bool enableVerifier, bool enableLifetime,
    llvm::StringRef lifetimeOpts, bool enableCIRSimplify, bool enableCIRMoveOpt,
    bool enableCallConvLowering, bool enableIdiomRecognizer,
    llvm::StringRef idiomRecognizerOpts, bool enableLibOpt,
    llvm::StringRef libOptOpts, std::string &passOptParsingFailure,
    bool enableMem2Reg = false);

} // namespace cir

#endif // CLANG_CIR_CIRTOCIRPASSES_H_
