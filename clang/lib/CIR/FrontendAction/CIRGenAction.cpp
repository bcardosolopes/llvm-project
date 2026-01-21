//===--- CIRGenAction.cpp - LLVM Code generation Frontend Action ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/FrontendAction/CIRGenAction.h"
#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/LangStandard.h"
#include "clang/CIR/CIRGenerator.h"
#include "clang/CIR/CIRToCIRPasses.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/LowerToLLVM.h"
#include "clang/CIR/Passes.h"
#include "clang/CodeGen/BackendUtil.h"
#include "clang/Frontend/CompilerInstance.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Frontend/Debug/Options.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"

using namespace cir;
using namespace clang;

static std::string sanitizePassOptions(llvm::StringRef O) {
  if (O.empty())
    return "";
  std::string Opts{O};
  // MLIR pass options are space separated, but we use ';' in clang since
  // space aren't well supported, switch it back.
  for (unsigned I = 0, E = Opts.size(); I < E; ++I)
    if (Opts[I] == ';')
      Opts[I] = ' ';
  // If arguments are surrounded with '"', trim them off
  return llvm::StringRef(Opts).trim('"').str();
}

namespace cir {

static BackendAction
getBackendActionFromOutputType(CIRGenAction::OutputType Action) {
  switch (Action) {
  case CIRGenAction::OutputType::EmitCIR:
  case CIRGenAction::OutputType::EmitCIRFlat:
  case CIRGenAction::OutputType::EmitMLIR:
    assert(false &&
           "Unsupported output type for getBackendActionFromOutputType!");
    break; // Unreachable, but fall through to report that
  case CIRGenAction::OutputType::EmitAssembly:
    return BackendAction::Backend_EmitAssembly;
  case CIRGenAction::OutputType::EmitBC:
    return BackendAction::Backend_EmitBC;
  case CIRGenAction::OutputType::EmitLLVM:
    return BackendAction::Backend_EmitLL;
  case CIRGenAction::OutputType::EmitObj:
    return BackendAction::Backend_EmitObj;
  }
  // We should only get here if a non-enum value is passed in or we went through
  // the assert(false) case above
  llvm_unreachable("Unsupported output type!");
}

static std::unique_ptr<llvm::Module>
lowerFromCIRToLLVMIR(mlir::ModuleOp MLIRModule, llvm::LLVMContext &LLVMCtx,
                     bool DisableDebugInfo) {
  return direct::lowerDirectlyFromCIRToLLVMIR(MLIRModule, LLVMCtx,
                                              DisableDebugInfo);
}

class CIRGenConsumer : public clang::ASTConsumer {

  virtual void anchor();

  CIRGenAction::OutputType Action;

  CompilerInstance &CI;

  std::unique_ptr<raw_pwrite_stream> OutputStream;

  ASTContext *Context{nullptr};
  IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS;
  std::unique_ptr<CIRGenerator> Gen;
  const FrontendOptions &FEOptions;
  CodeGenOptions &CGO;

  /// Info about module to link into a module we're generating.
  struct LinkModule {
    /// The module to link in.
    std::unique_ptr<llvm::Module> Module;

    /// If true, we set attributes on Module's functions according to our
    /// CodeGenOptions and LangOptions, as though we were generating the
    /// function ourselves.
    bool PropagateAttrs;

    /// If true, we use LLVM module internalizer.
    bool Internalize;

    /// Bitwise combination of llvm::LinkerFlags used when we link the module.
    unsigned LinkFlags;
  };
  /// Bitcode modules to link in to our module.
  SmallVector<LinkModule, 4> LinkModules;

public:
  CIRGenConsumer(CIRGenAction::OutputType Action, CompilerInstance &CI,
                 CodeGenOptions &CGO, std::unique_ptr<raw_pwrite_stream> OS)
      : Action(Action), CI(CI), OutputStream(std::move(OS)),
        FS(&CI.getVirtualFileSystem()),
        Gen(std::make_unique<CIRGenerator>(CI.getDiagnostics(), std::move(FS),
                                           CI.getCodeGenOpts())),
        FEOptions(CI.getFrontendOpts()), CGO(CGO) {}

  void Initialize(ASTContext &Ctx) override {
    assert(!Context && "initialized multiple times");
    Context = &Ctx;
    Gen->Initialize(Ctx);
  }

  bool HandleTopLevelDecl(DeclGroupRef D) override {
    Gen->HandleTopLevelDecl(D);
    return true;
  }

  void HandleCXXStaticMemberVarInstantiation(clang::VarDecl *VD) override {
    Gen->HandleCXXStaticMemberVarInstantiation(VD);
  }

  void HandleOpenACCRoutineReference(const FunctionDecl *FD,
                                     const OpenACCRoutineDecl *RD) override {
    Gen->HandleOpenACCRoutineReference(FD, RD);
  }

  void HandleInlineFunctionDefinition(FunctionDecl *D) override {
    Gen->HandleInlineFunctionDefinition(D);
  }

  void HandleTranslationUnit(ASTContext &C) override {
    Gen->HandleTranslationUnit(C);

    if (!FEOptions.ClangIRDisableCIRVerifier) {
      if (!Gen->verifyModule()) {
        CI.getDiagnostics().Report(
            diag::err_cir_verification_failed_pre_passes);
        llvm::report_fatal_error(
            "CIR codegen: module verification error before running CIR passes");
        return;
      }
    }

    mlir::ModuleOp MlirModule = Gen->getModule();
    mlir::MLIRContext &MlirCtx = Gen->getMLIRContext();

    auto SetupCirPipelineAndExecute = [&] {
      std::string LifetimeOpts, IdiomRecognizerOpts, LibOptOpts;
      if (FEOptions.ClangIRLifetimeCheck)
        LifetimeOpts = sanitizePassOptions(FEOptions.ClangIRLifetimeCheckOpts);
      if (FEOptions.ClangIRIdiomRecognizer)
        IdiomRecognizerOpts =
            sanitizePassOptions(FEOptions.ClangIRIdiomRecognizerOpts);
      if (FEOptions.ClangIRLibOpt)
        LibOptOpts = sanitizePassOptions(FEOptions.ClangIRLibOptOpts);

      std::string PassOptParsingFailure;
      if (runCIRToCIRPasses(
              MlirModule, MlirCtx, C, !FEOptions.ClangIRDisableCIRVerifier,
              FEOptions.ClangIRLifetimeCheck, LifetimeOpts,
              CGO.OptimizationLevel > 0, FEOptions.ClangIREnableMoveOpt,
              FEOptions.ClangIRCallConvLowering,
              FEOptions.ClangIRIdiomRecognizer, IdiomRecognizerOpts,
              FEOptions.ClangIRLibOpt, LibOptOpts, PassOptParsingFailure,
              FEOptions.ClangIREnableMem2Reg)
              .failed()) {
        if (!PassOptParsingFailure.empty()) {
          auto D =
              CI.getDiagnostics().Report(diag::err_drv_cir_pass_opt_parsing);
          D << PassOptParsingFailure;
        } else
          CI.getDiagnostics().Report(diag::err_cir_to_cir_transform_failed);
        return;
      }
    };

    if (!FEOptions.ClangIRDisablePasses) {
      // Handle source manager properly given that lifetime analysis
      // might emit warnings and remarks.
      auto &ClangSourceMgr = C.getSourceManager();
      FileID MainFileID = ClangSourceMgr.getMainFileID();

      std::unique_ptr<llvm::MemoryBuffer> FileBuf =
          llvm::MemoryBuffer::getMemBuffer(
              ClangSourceMgr.getBufferOrFake(MainFileID));

      llvm::SourceMgr MlirSourceMgr;
      MlirSourceMgr.AddNewSourceBuffer(std::move(FileBuf), llvm::SMLoc());

      if (FEOptions.ClangIRVerifyDiags) {
        mlir::SourceMgrDiagnosticVerifierHandler SourceMgrHandler(MlirSourceMgr,
                                                                  &MlirCtx);
        MlirCtx.printOpOnDiagnostic(false);
        SetupCirPipelineAndExecute();

        // Verify the diagnostic handler to make sure that each of the
        // diagnostics matched.
        if (SourceMgrHandler.verify().failed()) {
          // FIXME: we fail ungracefully, there's probably a better way
          // to communicate non-zero return so tests can actually fail.
          llvm::sys::RunInterruptHandlers();
          exit(1);
        }
      } else {
        mlir::SourceMgrDiagnosticHandler SourceMgrHandler(MlirSourceMgr,
                                                          &MlirCtx);
        SetupCirPipelineAndExecute();
      }
    }

    // If -fcir-output=<file> was specified, save CIR to the given file.
    if (!FEOptions.ClangIROutputFile.empty() && MlirModule) {
      std::error_code EC;
      llvm::raw_fd_ostream CIROut(FEOptions.ClangIROutputFile, EC);
      if (EC) {
        CI.getDiagnostics().Report(diag::err_fe_unable_to_open_output)
            << FEOptions.ClangIROutputFile << EC.message();
      } else {
        mlir::OpPrintingFlags Flags;
        Flags.enableDebugInfo(/*enable=*/true, /*prettyForm=*/false);
        MlirModule->print(CIROut, Flags);
      }
    }

    switch (Action) {
    case CIRGenAction::OutputType::EmitCIR:
      if (OutputStream && MlirModule) {
        mlir::OpPrintingFlags Flags;
        Flags.enableDebugInfo(/*enable=*/true, /*prettyForm=*/false);
        MlirModule->print(*OutputStream, Flags);
      }
      break;
    case CIRGenAction::OutputType::EmitCIRFlat:
      if (OutputStream && MlirModule) {
        // Run the pre-lowering passes to flatten the CFG.
        mlir::PassManager pm(&MlirCtx);
        mlir::populateCIRPreLoweringPasses(pm);
        pm.enableVerifier(!FEOptions.ClangIRDisableCIRVerifier);
        (void)mlir::applyPassManagerCLOptions(pm);
        if (pm.run(MlirModule).failed()) {
          CI.getDiagnostics().Report(diag::err_cir_to_cir_transform_failed);
          return;
        }

        mlir::OpPrintingFlags Flags;
        Flags.enableDebugInfo(/*enable=*/true, /*prettyForm=*/false);
        MlirModule->print(*OutputStream, Flags);
      }
      break;
    case CIRGenAction::OutputType::EmitMLIR:
      if (OutputStream && MlirModule) {
        StringRef EmitMLIRKind = FEOptions.ClangIREmitMLIR;
        if (EmitMLIRKind == "cir") {
          // Same as EmitCIR
          mlir::OpPrintingFlags Flags;
          Flags.enableDebugInfo(/*enable=*/true, /*prettyForm=*/false);
          MlirModule->print(*OutputStream, Flags);
        } else if (EmitMLIRKind == "cir-flat") {
          // Same as EmitCIRFlat
          mlir::PassManager pm(&MlirCtx);
          mlir::populateCIRPreLoweringPasses(pm);
          pm.enableVerifier(!FEOptions.ClangIRDisableCIRVerifier);
          (void)mlir::applyPassManagerCLOptions(pm);
          if (pm.run(MlirModule).failed()) {
            CI.getDiagnostics().Report(diag::err_cir_to_cir_transform_failed);
            return;
          }
          mlir::OpPrintingFlags Flags;
          Flags.enableDebugInfo(/*enable=*/true, /*prettyForm=*/false);
          MlirModule->print(*OutputStream, Flags);
        } else if (EmitMLIRKind == "llvm") {
          // Lower to MLIR LLVM dialect and print
          mlir::PassManager pm(&MlirCtx);
          cir::direct::populateCIRToLLVMPasses(pm);
          (void)mlir::applyPassManagerCLOptions(pm);
          if (pm.run(MlirModule).failed()) {
            CI.getDiagnostics().Report(diag::err_cir_to_cir_transform_failed);
            return;
          }
          MlirModule->print(*OutputStream);
        } else if (EmitMLIRKind == "core") {
          if (FEOptions.ClangIRDirectLowering) {
            llvm::report_fatal_error(
                "ClangIR direct lowering is incompatible with "
                "emitting of MLIR standard dialects");
          }
          // Lower CIR to MLIR core dialects and print
          mlir::PassManager pm(&MlirCtx);
          pm.addPass(mlir::createSCFPreparePass());
          pm.addPass(cir::createConvertCIRToMLIRPass());
          (void)mlir::applyPassManagerCLOptions(pm);
          if (pm.run(MlirModule).failed()) {
            CI.getDiagnostics().Report(diag::err_cir_to_cir_transform_failed);
            return;
          }
          MlirModule->print(*OutputStream);
        }
      }
      break;
    case CIRGenAction::OutputType::EmitLLVM:
    case CIRGenAction::OutputType::EmitBC:
    case CIRGenAction::OutputType::EmitObj:
    case CIRGenAction::OutputType::EmitAssembly: {
      llvm::LLVMContext LLVMCtx;
      bool DisableDebugInfo =
          CGO.getDebugInfo() == llvm::codegenoptions::NoDebugInfo;

      LoadLinkModules(LLVMCtx);

      std::unique_ptr<llvm::Module> LLVMModule =
          lowerFromCIRToLLVMIR(MlirModule, LLVMCtx, DisableDebugInfo);

      LinkInModules(*LLVMModule);

      BackendAction BEAction = getBackendActionFromOutputType(Action);
      emitBackendOutput(
          CI, CI.getCodeGenOpts(), C.getTargetInfo().getDataLayoutString(),
          LLVMModule.get(), BEAction, FS, std::move(OutputStream));
      break;
    }
    }
  }

  void LoadLinkModules(llvm::LLVMContext &LLVMCtx) {
    for (const CodeGenOptions::BitcodeFileToLink &F :
         CI.getCodeGenOpts().LinkBitcodeFiles) {
      auto BCBuf = CI.getFileManager().getBufferForFile(F.Filename);
      if (!BCBuf) {
        CI.getDiagnostics().Report(diag::err_cannot_open_file)
            << F.Filename << BCBuf.getError().message();
        LinkModules.clear();
        return;
      }

      Expected<std::unique_ptr<llvm::Module>> ModuleOrErr =
          getOwningLazyBitcodeModule(std::move(*BCBuf), LLVMCtx);
      if (!ModuleOrErr) {
        handleAllErrors(ModuleOrErr.takeError(), [&](llvm::ErrorInfoBase &EIB) {
          CI.getDiagnostics().Report(diag::err_cannot_open_file)
              << F.Filename << EIB.message();
        });
        LinkModules.clear();
        return;
      }
      LinkModules.push_back({std::move(ModuleOrErr.get()), F.PropagateAttrs,
                             F.Internalize, F.LinkFlags});
    }
  }

  void LinkInModules(llvm::Module &M) {
    for (auto &LM : LinkModules) {
      if (llvm::Linker::linkModules(M, std::move(LM.Module), LM.LinkFlags)) {
        CI.getDiagnostics().Report(diag::err_fe_linking_module)
            << M.getModuleIdentifier();
        return;
      }
    }
  }

  void HandleTagDeclDefinition(TagDecl *D) override {
    PrettyStackTraceDecl CrashInfo(D, SourceLocation(),
                                   Context->getSourceManager(),
                                   "CIR generation of declaration");
    Gen->HandleTagDeclDefinition(D);
  }

  void HandleTagDeclRequiredDefinition(const TagDecl *D) override {
    Gen->HandleTagDeclRequiredDefinition(D);
  }

  void CompleteTentativeDefinition(VarDecl *D) override {
    Gen->CompleteTentativeDefinition(D);
  }

  void HandleVTable(CXXRecordDecl *RD) override { Gen->HandleVTable(RD); }
};
} // namespace cir

void CIRGenConsumer::anchor() {}

CIRGenAction::CIRGenAction(OutputType Act, mlir::MLIRContext *MLIRCtx)
    : MLIRCtx(MLIRCtx ? MLIRCtx : new mlir::MLIRContext), Action(Act) {}

CIRGenAction::~CIRGenAction() { MLIRMod.release(); }

static std::unique_ptr<raw_pwrite_stream>
getOutputStream(CompilerInstance &CI, StringRef InFile,
                CIRGenAction::OutputType Action) {
  switch (Action) {
  case CIRGenAction::OutputType::EmitAssembly:
    return CI.createDefaultOutputFile(false, InFile, "s");
  case CIRGenAction::OutputType::EmitCIR:
    return CI.createDefaultOutputFile(false, InFile, "cir");
  case CIRGenAction::OutputType::EmitCIRFlat:
    return CI.createDefaultOutputFile(false, InFile, "cir");
  case CIRGenAction::OutputType::EmitMLIR:
    return CI.createDefaultOutputFile(false, InFile, "mlir");
  case CIRGenAction::OutputType::EmitLLVM:
    return CI.createDefaultOutputFile(false, InFile, "ll");
  case CIRGenAction::OutputType::EmitBC:
    return CI.createDefaultOutputFile(true, InFile, "bc");
  case CIRGenAction::OutputType::EmitObj:
    return CI.createDefaultOutputFile(true, InFile, "o");
  }
  llvm_unreachable("Invalid CIRGenAction::OutputType");
}

std::unique_ptr<ASTConsumer>
CIRGenAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  std::unique_ptr<llvm::raw_pwrite_stream> Out = CI.takeOutputStream();

  if (!Out)
    Out = getOutputStream(CI, InFile, Action);

  auto Result = std::make_unique<cir::CIRGenConsumer>(
      Action, CI, CI.getCodeGenOpts(), std::move(Out));

  return Result;
}

void CIRGenAction::ExecuteAction() {
  if (getCurrentFileKind().getLanguage() != Language::CIR) {
    this->ASTFrontendAction::ExecuteAction();
    return;
  }

  // If this is a CIR file, we have to treat it specially.
  CompilerInstance &CI = getCompilerInstance();
  auto &CodeGenOpts = CI.getCodeGenOpts();

  // Get the output stream.
  std::unique_ptr<raw_pwrite_stream> OS = CI.takeOutputStream();
  if (!OS)
    OS = getOutputStream(CI, getCurrentFileOrBufferName(), Action);
  if (!OS)
    return;

  // Read the input file.
  SourceManager &SM = CI.getSourceManager();
  FileID FID = SM.getMainFileID();
  std::optional<llvm::MemoryBufferRef> MainFile = SM.getBufferOrNone(FID);
  if (!MainFile)
    return;

  // Set up the MLIR context and register CIR dialect.
  MLIRCtx->loadDialect<cir::CIRDialect>();
  MLIRCtx->loadDialect<mlir::DLTIDialect>();

  // Create a source manager for the MLIR parser.
  llvm::SourceMgr SrcMgr;
  SrcMgr.AddNewSourceBuffer(
      llvm::MemoryBuffer::getMemBuffer(MainFile->getBuffer(),
                                       MainFile->getBufferIdentifier()),
      llvm::SMLoc());

  // Parse the CIR module.
  mlir::ParserConfig Config(MLIRCtx);
  mlir::OwningOpRef<mlir::ModuleOp> MlirModule =
      mlir::parseSourceFile<mlir::ModuleOp>(SrcMgr, Config);
  if (!MlirModule) {
    CI.getDiagnostics().Report(diag::err_fe_unable_to_open_output)
        << getCurrentFileOrBufferName() << "failed to parse CIR";
    return;
  }

  // Lower from CIR to LLVM IR.
  llvm::LLVMContext LLVMCtx;
  bool DisableDebugInfo =
      CodeGenOpts.getDebugInfo() == llvm::codegenoptions::NoDebugInfo;
  std::unique_ptr<llvm::Module> LLVMModule =
      lowerFromCIRToLLVMIR(*MlirModule, LLVMCtx, DisableDebugInfo);
  if (!LLVMModule)
    return;

  BackendAction BEAction = getBackendActionFromOutputType(Action);
  emitBackendOutput(CI, CI.getCodeGenOpts(),
                    CI.getTarget().getDataLayoutString(), LLVMModule.get(),
                    BEAction, CI.getVirtualFileSystemPtr(), std::move(OS));
}

void EmitAssemblyAction::anchor() {}
EmitAssemblyAction::EmitAssemblyAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitAssembly, MLIRCtx) {}

void EmitCIRAction::anchor() {}
EmitCIRAction::EmitCIRAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitCIR, MLIRCtx) {}

void EmitCIRFlatAction::anchor() {}
EmitCIRFlatAction::EmitCIRFlatAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitCIRFlat, MLIRCtx) {}

void EmitMLIRAction::anchor() {}
EmitMLIRAction::EmitMLIRAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitMLIR, MLIRCtx) {}

void EmitLLVMAction::anchor() {}
EmitLLVMAction::EmitLLVMAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitLLVM, MLIRCtx) {}

void EmitBCAction::anchor() {}
EmitBCAction::EmitBCAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitBC, MLIRCtx) {}

void EmitObjAction::anchor() {}
EmitObjAction::EmitObjAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitObj, MLIRCtx) {}
