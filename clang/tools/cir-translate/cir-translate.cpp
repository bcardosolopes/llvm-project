//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Converts CIR directly to LLVM IR, similar to mlir-translate or LLVM llc.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/InitAllTranslations.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Target/LLVMIR/Dialect/All.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "mlir/Target/LLVMIR/Import.h"
#include "mlir/Tools/mlir-translate/MlirTranslateMain.h"
#include "mlir/Tools/mlir-translate/Translation.h"

#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/DebugProgramInstruction.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Transforms/Instrumentation/SanitizerBinaryMetadata.h"

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/LowerToLLVM.h"
#include "clang/CIR/MissingFeatures.h"

/// The tool's own --target, passed into the lowering library rather than
/// registered by it: a cl::opt in a library is registered by every binary that
/// links it, which for the lowering library includes clang.
static llvm::cl::opt<std::string> targetTripleOption(
    "target",
    llvm::cl::desc(
        "Specify a default target triple when it's not available in the "
        "module"),
    llvm::cl::init(""));

namespace cir {
namespace direct {
extern void registerCIRDialectTranslation(mlir::DialectRegistry &registry);
} // namespace direct

namespace {

} // namespace
} // namespace cir

/// Write LLVM bitcode rather than textual LLVM IR.
///
/// Not inferred from the input's format, though that is the rule everywhere
/// else in this pipeline, because this tool cannot see its own input: the
/// filename is a function-local static cl::Positional inside mlirTranslateMain,
/// so it is not reachable from here and, being positional, is not in the named
/// option registry either. The caller knows what it wrote -- cir-link-wrapper
/// hands this tool bytecode and asks for bitcode back -- so the decision is
/// made there instead of guessed here.
static llvm::cl::opt<cir::LTOBitcodeOptions::Kind> lto(
    "lto",
    llvm::cl::desc("With -emit-llvm-bitcode, the kind of LTO link the module "
                   "is joining, so it is written the way clang writes bitcode "
                   "for that kind of link"),
    llvm::cl::init(cir::LTOBitcodeOptions::Kind::None),
    llvm::cl::values(
        clEnumValN(cir::LTOBitcodeOptions::Kind::Full, "full", "regular LTO"),
        clEnumValN(cir::LTOBitcodeOptions::Kind::Thin, "thin", "ThinLTO")));

static llvm::cl::opt<bool> splitLTOUnit(
    "split-lto-unit",
    llvm::cl::desc("clang's -fsplit-lto-unit. Only affects -lto=thin"),
    llvm::cl::init(false));

static llvm::cl::opt<bool>
    emitLLVMBitcode("emit-llvm-bitcode",
                    llvm::cl::desc("Write LLVM bitcode instead of textual "
                                   "LLVM IR"),
                    llvm::cl::init(false));

// clang's -fclangir-inline, for this lowering; cir-link-wrapper passes it
// when it hands cir-translate the lowering half of a split link.
static llvm::cl::opt<bool>
    runInliner("inline",
               llvm::cl::desc("Run the CIR inliner while lowering "
                              "(-fclangir-inline)"),
               llvm::cl::init(false));
static llvm::cl::opt<unsigned> inlineThreshold(
    "cir-inline-threshold",
    llvm::cl::desc("Threshold for --inline (-fclangir-inline-threshold=); "
                   "the inliner's own when not given"));

static cir::InlinerOptions getInlinerOptions() {
  cir::InlinerOptions options;
  options.enabled = runInliner;
  if (inlineThreshold.getNumOccurrences())
    options.threshold = inlineThreshold;
  return options;
}

void registerToLLVMTranslation() {
  static llvm::cl::opt<bool> disableCCLowering(
      "disable-cc-lowering",
      llvm::cl::desc("Disable calling convention lowering pass"),
      llvm::cl::init(false));

  mlir::TranslateFromMLIRRegistration registration(
      "cir-to-llvmir", "Translate CIR to LLVMIR",
      [](mlir::Operation *op, mlir::raw_ostream &output) {
        auto cirModule = llvm::dyn_cast<mlir::ModuleOp>(op);
        (void)disableCCLowering;

        std::optional<llvm::StringRef> requestedTriple;
        if (targetTripleOption.getNumOccurrences() > 0)
          requestedTriple = llvm::StringRef(targetTripleOption);

        llvm::LLVMContext llvmContext;
        std::unique_ptr<llvm::Module> llvmModule = cir::lowerCIRModuleToLLVMIR(
            cirModule, llvmContext, requestedTriple, getInlinerOptions());
        if (!llvmModule)
          return mlir::failure();
        if (emitLLVMBitcode)
          cir::writeLLVMModuleBitcode(
              *llvmModule, output,
              {/*kind=*/lto, /*splitLTOUnit=*/splitLTOUnit});
        else
          llvmModule->print(output, nullptr);
        return mlir::success();
      },
      cir::registerCIRToLLVMIRTranslations);
}

// Register the standard --mlir-to-llvmir translation with CIR dialect support.
// This allows cir-translate to be used as a drop-in replacement for
// mlir-translate when CIR attributes survive lowering to the LLVM dialect.
void registerMLIRToLLVMIRTranslation() {
  mlir::TranslateFromMLIRRegistration registration(
      "mlir-to-llvmir", "Translate MLIR to LLVMIR (with CIR dialect support)",
      [](mlir::Operation *op, mlir::raw_ostream &output) {
        llvm::LLVMContext llvmContext;
        auto llvmModule = mlir::translateModuleToLLVMIR(op, llvmContext);
        if (!llvmModule)
          return mlir::failure();
        llvmModule->removeDebugIntrinsicDeclarations();
        llvmModule->print(output, nullptr);
        return mlir::success();
      },
      [](mlir::DialectRegistry &registry) {
        registry.insert<mlir::DLTIDialect, mlir::func::FuncDialect>();
        mlir::registerAllToLLVMIRTranslations(registry);
        cir::direct::registerCIRDialectTranslation(registry);
      });
}

int main(int argc, char **argv) {
  registerToLLVMTranslation();
  registerMLIRToLLVMIRTranslation();
  return failed(mlir::mlirTranslateMain(argc, argv, "CIR Translation Tool"));
}
