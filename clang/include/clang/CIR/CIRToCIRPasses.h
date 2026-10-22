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
#include "clang/CIR/Dialect/IR/CIRStage.h"

#include <memory>
#include <optional>

namespace mlir {
class MLIRContext;
class ModuleOp;
} // namespace mlir

namespace cir {

// Run the CIR <-> CIR passes the module still needs, from \p from to
// \p stopAt; see CIRStage.
mlir::LogicalResult
runCIRToCIRPasses(mlir::ModuleOp theModule, mlir::MLIRContext &mlirCtx,
                  bool enableVerifier, bool enableIdiomRecognizer,
                  bool enableCIRSimplify, bool enableLibOpt,
                  llvm::StringRef libOptOptions, bool enableCallConvLowering,
                  std::optional<CIRStage> from = std::nullopt,
                  std::optional<CIRStage> stopAt = std::nullopt);

} // namespace cir

#endif // CLANG_CIR_CIRTOCIRPASSES_H_
