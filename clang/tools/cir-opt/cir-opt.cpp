//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Similar to MLIR/LLVM's "opt" tools but also deals with analysis and custom
// arguments. TODO: this is basically a copy from MlirOptMain.cpp, but capable
// of module emission as specified by the user.
//
//===----------------------------------------------------------------------===//

#include "mlir/Conversion/Passes.h"
#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/BuiltinDialect.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassOptions.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"
#include "mlir/Transforms/Passes.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRStage.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Passes.h"

struct CIRToLLVMPipelineOptions
    : public mlir::PassPipelineOptions<CIRToLLVMPipelineOptions> {
  // The pipeline is built before any module is read, so it cannot look at the
  // input's cir.stage; this says where the input is instead.
  Option<std::string> fromStage{
      *this, "from-stage",
      llvm::cl::desc("The stage the input is at (cirgen, cir-prepare, cir-abi, "
                     "cir-flat); by default, CIR written by hand, which gets "
                     "every step"),
      llvm::cl::init("")};
  // Off by default: most inputs are hand-written CIR that was never meant to
  // be ABI-lowered.
  Option<bool> callConvLowering{
      *this, "call-conv-lowering",
      llvm::cl::desc("Run CallConvLowering before lowering to LLVM"),
      llvm::cl::init(false)};
};

int main(int argc, char **argv) {
  // TODO: register needed MLIR passes for CIR?
  mlir::DialectRegistry registry;
  registry
      .insert<mlir::BuiltinDialect, cir::CIRDialect, mlir::arith::ArithDialect,
              mlir::cf::ControlFlowDialect, mlir::func::FuncDialect,
              mlir::memref::MemRefDialect, mlir::scf::SCFDialect,
              mlir::LLVM::LLVMDialect, mlir::DLTIDialect>();

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createCIRCanonicalizePass();
  });
  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createCIRSimplifyPass();
  });

  mlir::PassPipelineRegistration<CIRToLLVMPipelineOptions> pipeline(
      "cir-to-llvm", "",
      [](mlir::OpPassManager &pm, const CIRToLLVMPipelineOptions &options) {
        std::optional<cir::CIRStage> from;
        if (!options.fromStage.empty()) {
          from = cir::symbolizeCIRStage(options.fromStage);
          if (!from)
            llvm::report_fatal_error("cir-to-llvm: unknown from-stage '" +
                                     llvm::Twine(options.fromStage) + "'");
        }
        cir::direct::populateCIRToLLVMPasses(pm, from,
                                             options.callConvLowering);
      });

  struct CIRToMLIRPipelineOptions
      : public mlir::PassPipelineOptions<CIRToMLIRPipelineOptions> {};
  mlir::PassPipelineRegistration<CIRToMLIRPipelineOptions> mlirPipeline(
      "cir-to-mlir", "",
      [](mlir::OpPassManager &pm, const CIRToMLIRPipelineOptions &options) {
        pm.addPass(cir::createConvertCIRToMLIRPass());
      });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return cir::createConvertMLIRToLLVMPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createCIRFlattenCFGPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createHoistAllocasPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createGotoSolverPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createCIRTailMergePass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createCIRInlinerPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createABILoweringPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createPointsToDiagnosticPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createLiveObjectDiagnosticPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createCXXABILoweringPass();
  });
  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createCallConvLoweringPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createSCFPreparePass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createMoveOptPass();
  });

  mlir::registerTransformsPasses();
  mlir::registerConversionPasses();

  return mlir::asMainReturnCode(MlirOptMain(
      argc, argv, "Clang IR analysis and optimization tool\n", registry));
}
