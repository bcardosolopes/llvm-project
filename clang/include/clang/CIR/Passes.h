//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file exposes the entry points to create compiler passes for ClangIR.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_PASSES_H
#define CLANG_CIR_PASSES_H

#include "mlir/Pass/Pass.h"
#include "clang/CIR/Dialect/IR/CIRStage.h"

#include <memory>
#include <optional>

namespace cir {
namespace direct {
/// Create a pass that fully lowers CIR to the LLVMIR dialect.
std::unique_ptr<mlir::Pass> createConvertCIRToLLVMPass();

/// Adds the pre-lowering passes and the CIR to LLVMIR conversion, plus the
/// OpenMP passes when enableOpenMP is set (-fopenmp). Only passes past the
/// recorded \p from stage run; with \p stopAt the list ends at that stage.
void populateCIRToLLVMPasses(mlir::OpPassManager &pm,
                             std::optional<CIRStage> from, bool enableOpenMP,
                             std::optional<CIRStage> stopAt = std::nullopt);

} // namespace direct
} // end namespace cir

#endif // CLANG_CIR_PASSES_H
