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
#include "clang/CIR/LowerToLLVM.h"

#include <memory>

namespace llvm {
class LLVMContext;
class Module;
} // namespace llvm

namespace cir {
/// Create a pass for lowering from MLIR builtin dialects such as `Affine` and
/// `Std`, to the LLVM dialect for codegen.
std::unique_ptr<mlir::Pass> createConvertMLIRToLLVMPass();

/// Create a pass that fully lowers CIR to the MLIR in-tree dialects.
std::unique_ptr<mlir::Pass> createConvertCIRToMLIRPass();

/// Lower CIR to MLIR standard dialects.
mlir::ModuleOp lowerFromCIRToMLIR(mlir::ModuleOp theModule,
                                  mlir::MLIRContext *mlirCtx);

/// Lower CIR through MLIR to LLVM dialect.
mlir::ModuleOp lowerFromCIRToMLIRToLLVMDialect(mlir::ModuleOp theModule,
                                               mlir::MLIRContext *mlirCtx);

/// Lower CIR through MLIR to LLVM IR.
std::unique_ptr<llvm::Module>
lowerFromCIRToMLIRToLLVMIR(mlir::ModuleOp theModule,
                           std::unique_ptr<mlir::MLIRContext> mlirCtx,
                           llvm::LLVMContext &llvmCtx);

namespace direct {
/// Create a pass that fully lowers CIR to the LLVMIR dialect.
std::unique_ptr<mlir::Pass> createConvertCIRToLLVMPass();

/// Adds the passes that lower a module at stage \p from to the LLVMIR dialect.
/// Only what the module has not been through yet is scheduled; a module with
/// no stage (CIR written by hand) gets every step. In order:
///  - CXXABILowering, before cir-prepare;
///  - CallConvLowering, before cir-abi, when \p runCallConvLowering is true;
///    this is the only place it runs;
///  - HoistAllocas, FlattenCFG and GotoSolver, before cir-flat;
///  - TailMerge, then mem2reg when \p runMem2Reg is true. mem2reg has to follow
///    call-conv lowering: run earlier, it promotes a by-value aggregate return
///    slot that call-conv lowering then rewrites into an sret store, and the
///    return value is dropped;
///  - the CIR inliner when \p inliner enables it, followed by mem2reg again
///    when that is on, for the allocas the inlined bodies bring in;
///  - static-chain call lowering;
///  - the conversion to LLVM dialect.
/// With \p stopAt, the list ends once the module is at that stage, for callers
/// that want the CIR rather than LLVM dialect; see CIRStage. With
/// \p stopBeforeConversion, it ends after the inliner, before static-chain
/// call lowering: the CIR-to-CIR part a partitioned lowering runs on the whole
/// module. With \p startAtConversion, it is only the last two, for each piece
/// of that module.
void populateCIRToLLVMPasses(mlir::OpPassManager &pm,
                             std::optional<CIRStage> from,
                             bool runCallConvLowering, bool runMem2Reg = false,
                             std::optional<CIRStage> stopAt = std::nullopt,
                             InlinerOptions inliner = {},
                             bool stopBeforeConversion = false,
                             bool startAtConversion = false);

} // namespace direct
} // end namespace cir

#endif // CLANG_CIR_PASSES_H
