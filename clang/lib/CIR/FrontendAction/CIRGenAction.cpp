//===--- CIRGenAction.cpp - LLVM Code generation Frontend Action ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/FrontendAction/CIRGenAction.h"
#include "../../CodeGen/BackendConsumer.h"
#include "../../CodeGen/CoverageMappingGen.h"
#include "mlir/Bytecode/BytecodeWriter.h"
#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "clang/Basic/DiagnosticDriver.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/LangStandard.h"
#include "clang/CIR/CIRGenerator.h"
#include "clang/CIR/CIRToCIRPasses.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRStage.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/LowerToLLVM.h"
#include "clang/CIR/Passes.h"
#include "clang/CodeGen/BackendUtil.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Frontend/Debug/Options.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMRemarkStreamer.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

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

namespace {

/// Strip the `personality` attribute from a function when no exception-handling
/// instruction survives in its body.
///
/// CIR-to-LLVM lowering eagerly attaches `personality` to any function that
/// contains a `cir.try` (and the landing-pad lowering also installs a fallback
/// personality). After downstream LLVM optimisations (inliner, simplifycfg,
/// DCE) eliminate every `invoke`/`landingpad`/funclet instruction, the
/// `personality` reference becomes stale: the function is provably non-
/// unwinding, yet the IR still pulls in `__gxx_personality_v0` and signals
/// to consumers that the function may throw. AST CodeGen exhibits the same
/// pattern less often because its EH lowering produces fewer easily-DCE'd
/// `invoke`s. Stripping the stale attribute brings CIR closer to AST-CodeGen
/// parity, removes an unused symbol reference, and avoids confusing downstream
/// passes that gate on `hasPersonalityFn()`.
///
/// Invariant: only strip when the function body contains *no* EH instruction
/// (no `invoke`, `landingpad`, `resume`, or funclet pad). Functions with
/// surviving EH legitimately need the personality and are left untouched.
static bool stripStalePersonality(llvm::Function &F) {
  if (!F.hasPersonalityFn() || F.isDeclaration())
    return false;
  for (const llvm::BasicBlock &BB : F) {
    for (const llvm::Instruction &I : BB) {
      if (llvm::isa<llvm::InvokeInst>(I) ||
          llvm::isa<llvm::LandingPadInst>(I) ||
          llvm::isa<llvm::ResumeInst>(I) ||
          llvm::isa<llvm::CatchSwitchInst>(I) ||
          llvm::isa<llvm::CatchPadInst>(I) ||
          llvm::isa<llvm::CleanupPadInst>(I))
        return false;
    }
  }
  F.setPersonalityFn(nullptr);
  return true;
}

/// Module pass that runs `stripStalePersonality` over every function and, if
/// the resulting `__gxx_personality_v0` (or related personality) declaration
/// has no remaining users, erases the dead declaration.
class CIRStripStalePersonalityPass
    : public llvm::PassInfoMixin<CIRStripStalePersonalityPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &) {
    llvm::SmallPtrSet<llvm::Constant *, 4> touchedPersonalities;
    bool changed = false;
    for (llvm::Function &F : M) {
      if (!F.hasPersonalityFn() || F.isDeclaration())
        continue;
      llvm::Constant *pers = F.getPersonalityFn();
      if (stripStalePersonality(F)) {
        changed = true;
        touchedPersonalities.insert(pers);
      }
    }
    // Best-effort cleanup of now-unused personality declarations. Strip the
    // ConstantExpr cast (if any) to find the underlying GlobalValue.
    for (llvm::Constant *pers : touchedPersonalities) {
      llvm::Value *stripped = pers->stripPointerCasts();
      if (auto *GV = llvm::dyn_cast<llvm::GlobalValue>(stripped)) {
        if (GV->use_empty() && GV->isDeclaration())
          GV->eraseFromParent();
      }
    }
    return changed ? llvm::PreservedAnalyses::none()
                   : llvm::PreservedAnalyses::all();
  }
};

/// Append a CIR-only PassBuilder callback to `CGO.PassBuilderCallbacks` that
/// runs `CIRStripStalePersonalityPass` at OptimizerLast (i.e. after the LLVM
/// optimisation pipeline has had a chance to eliminate `invoke`s).
static void registerCIRPersonalityCleanup(clang::CodeGenOptions &CGO) {
  CGO.PassBuilderCallbacks.push_back([](llvm::PassBuilder &PB) {
    PB.registerOptimizerLastEPCallback([](llvm::ModulePassManager &MPM,
                                          llvm::OptimizationLevel,
                                          llvm::ThinOrFullLTOPhase) {
      MPM.addPass(CIRStripStalePersonalityPass());
    });
  });
}

} // namespace

namespace cir {

static BackendAction
getBackendActionFromOutputType(CIRGenAction::OutputType Action) {
  switch (Action) {
  case CIRGenAction::OutputType::EmitCIR:
  case CIRGenAction::OutputType::EmitMLIR:
    llvm_unreachable(
        "Unsupported output type for getBackendActionFromOutputType!");
  case CIRGenAction::OutputType::EmitAssembly:
    return BackendAction::Backend_EmitAssembly;
  case CIRGenAction::OutputType::EmitBC:
    return BackendAction::Backend_EmitBC;
  case CIRGenAction::OutputType::EmitLLVM:
    return BackendAction::Backend_EmitLL;
  case CIRGenAction::OutputType::EmitObj:
    return BackendAction::Backend_EmitObj;
  }
  // We should only get here if a non-enum value is passed in
  llvm_unreachable("Unsupported output type!");
}

static std::unique_ptr<llvm::Module> lowerFromCIRToLLVMIR(
    mlir::ModuleOp MLIRModule, llvm::LLVMContext &LLVMCtx,
    bool DisableDebugInfo, bool EnableVerifier, bool RunCallConvLowering = true,
    bool RunMem2RegAfterDebugInfo = false, bool RunMem2RegInLowering = false,
    const std::function<void(llvm::StringRef, llvm::StringRef)>
        &reportProfileReadError = {},
    cir::InlinerOptions Inliner = {}) {
  return direct::lowerDirectlyFromCIRToLLVMIR(
      MLIRModule, LLVMCtx, DisableDebugInfo, EnableVerifier,
      RunCallConvLowering, RunMem2RegAfterDebugInfo, RunMem2RegInLowering,
      reportProfileReadError, Inliner);
}

namespace {

class CIRDiagnosticHandler final : public llvm::DiagnosticHandler {
public:
  CIRDiagnosticHandler(const CodeGenOptions &CGOpts, BackendConsumer *BCon)
      : CodeGenOpts(CGOpts), BackendCon(BCon) {}

  bool handleDiagnostics(const llvm::DiagnosticInfo &DI) override {
    BackendCon->DiagnosticHandlerImpl(DI);
    return true;
  }

  bool isAnalysisRemarkEnabled(llvm::StringRef PassName) const override {
    return CodeGenOpts.OptimizationRemarkAnalysis.patternMatches(PassName);
  }
  bool isMissedOptRemarkEnabled(llvm::StringRef PassName) const override {
    return CodeGenOpts.OptimizationRemarkMissed.patternMatches(PassName);
  }
  bool isPassedOptRemarkEnabled(llvm::StringRef PassName) const override {
    return CodeGenOpts.OptimizationRemark.patternMatches(PassName);
  }

  bool isAnyRemarkEnabled() const override {
    return CodeGenOpts.OptimizationRemarkAnalysis.hasValidPattern() ||
           CodeGenOpts.OptimizationRemarkMissed.hasValidPattern() ||
           CodeGenOpts.OptimizationRemark.hasValidPattern();
  }

private:
  const CodeGenOptions &CodeGenOpts;
  BackendConsumer *BackendCon;
};

static void reportOptRecordError(llvm::Error E, DiagnosticsEngine &Diags,
                                 const CodeGenOptions &CodeGenOpts) {
  llvm::handleAllErrors(
      std::move(E),
      [&](const llvm::LLVMRemarkSetupFileError &E) {
        Diags.Report(diag::err_cannot_open_file)
            << CodeGenOpts.OptRecordFile << E.message();
      },
      [&](const llvm::LLVMRemarkSetupPatternError &E) {
        Diags.Report(diag::err_drv_optimization_remark_pattern)
            << E.message() << CodeGenOpts.OptRecordPasses;
      },
      [&](const llvm::LLVMRemarkSetupFormatError &E) {
        Diags.Report(diag::err_drv_optimization_remark_format)
            << CodeGenOpts.OptRecordFormat;
      });
}

static bool emitBackendOutputWithDiagnosticHandler(
    CompilerInstance &CI, CodeGenOptions &CodeGenOpts, StringRef DataLayout,
    llvm::Module *LLVMModule, BackendAction BEAction,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
    std::unique_ptr<raw_pwrite_stream> OS, ASTContext *Context = nullptr,
    CIRGenerator *Gen = nullptr) {
  llvm::LLVMContext &LLVMCtx = LLVMModule->getContext();
  using DiagnosticConsumerPtr =
      std::unique_ptr<BackendConsumer, void (*)(BackendConsumer *)>;
  struct RestoreDiagnosticHandler {
    llvm::LLVMContext &Ctx;
    std::unique_ptr<llvm::DiagnosticHandler> PrevHandler =
        Ctx.getDiagnosticHandler();
    ~RestoreDiagnosticHandler() {
      Ctx.setDiagnosticHandler(std::move(PrevHandler));
    }
  } Restore{LLVMCtx};

  DiagnosticConsumerPtr DiagConsumer(
      CreateBackendConsumer(CI, BEAction, FS, LLVMCtx, "", nullptr, nullptr,
                            Context ? nullptr : LLVMModule),
      DestroyBackendConsumer);
  if (Context) {
    DiagConsumer->Initialize(*Context);
    if (Gen)
      Gen->populateFunctionSourceLocations(*DiagConsumer);
  }

  LLVMCtx.setDiagnosticHandler(
      std::make_unique<CIRDiagnosticHandler>(CodeGenOpts, DiagConsumer.get()));
  LLVMCtx.setDefaultTargetCPU(CI.getTargetOpts().CPU);
  LLVMCtx.setDefaultTargetFeatures(llvm::join(CI.getTargetOpts().Features, ","));

  llvm::Expected<llvm::LLVMRemarkFileHandle> OptRecordFileOrErr =
      llvm::setupLLVMOptimizationRemarks(
          LLVMCtx, CodeGenOpts.OptRecordFile, CodeGenOpts.OptRecordPasses,
          CodeGenOpts.OptRecordFormat, CodeGenOpts.DiagnosticsWithHotness,
          CodeGenOpts.DiagnosticsHotnessThreshold);
  if (llvm::Error E = OptRecordFileOrErr.takeError()) {
    reportOptRecordError(std::move(E), CI.getDiagnostics(), CodeGenOpts);
    return false;
  }

  llvm::LLVMRemarkFileHandle OptRecordFile = std::move(*OptRecordFileOrErr);
  if (OptRecordFile && CodeGenOpts.getProfileUse() !=
                           llvm::driver::ProfileInstrKind::ProfileNone)
    LLVMCtx.setDiagnosticsHotnessRequested(true);
  if (CodeGenOpts.MisExpect)
    LLVMCtx.setMisExpectWarningRequested(true);
  if (CodeGenOpts.DiagnosticsMisExpectTolerance)
    LLVMCtx.setDiagnosticsMisExpectTolerance(
        CodeGenOpts.DiagnosticsMisExpectTolerance);

  // Multiple paths can emit the same LLVM module flag into the lowered module
  // (for example the i386 NumRegisterParameters flag is emitted both by CIR
  // lowering and again when profile/coverage instrumentation augments the
  // module), which fails verification because module flag identifiers must be
  // unique. Drop duplicate flag keys, keeping the first occurrence, before the
  // backend runs the verifier.
  if (auto *FlagsMD = LLVMModule->getModuleFlagsMetadata()) {
    llvm::SmallVector<llvm::MDNode *, 8> UniqueFlags;
    llvm::SmallPtrSet<llvm::MDString *, 8> SeenKeys;
    bool FoundDuplicate = false;
    for (llvm::MDNode *Flag : FlagsMD->operands()) {
      auto *Key = Flag->getNumOperands() >= 2
                      ? llvm::dyn_cast<llvm::MDString>(Flag->getOperand(1))
                      : nullptr;
      if (Key && !SeenKeys.insert(Key).second) {
        FoundDuplicate = true;
        continue;
      }
      UniqueFlags.push_back(Flag);
    }
    if (FoundDuplicate) {
      LLVMModule->eraseNamedMetadata(FlagsMD);
      auto *NewFlags = LLVMModule->getOrInsertModuleFlagsMetadata();
      for (llvm::MDNode *Flag : UniqueFlags)
        NewFlags->addOperand(Flag);
    }
  }

  emitBackendOutput(CI, CodeGenOpts, DataLayout, LLVMModule, BEAction, FS,
                    std::move(OS), DiagConsumer.get());
  if (OptRecordFile)
    OptRecordFile->keep();
  return true;
}

} // namespace

// The AST attributes on cir.func/cir.global wrap raw clang::Decl pointers.
// They are valid only inside the cc1 invocation that produced them, they have
// no serialized form, and a reader hits "cannot be parsed" the moment one
// shows up in a bytecode file. Strip them before writing a fat object.
//
// Every consumer in the lowering path reaches these through dyn_cast_or_null
// and copes with their absence. Whether "copes" also means "emits the same
// code" is a question for measurement, not for reading the sources: lowering
// the stripped module and lowering the original produce byte-identical LLVM
// IR across the cir-link sweep (virtual dispatch, exceptions, member
// pointers, RTTI, multiple inheritance, lambdas, templates, std::function).
// So the strip is free on everything exercised so far -- which is not the
// same as free in general, since these attributes do feed call-site
// attribute inference and special-member handling.
/// Record, for the calling convention, what the AST knows about passing each
/// record that is passed by value, before serialization takes the AST away.
///
/// The calling convention of a C++ record depends on more than its layout: one
/// with a non-trivial copy, move or destructor is passed by invisible reference
/// and returned through sret, and the caller of one with a non-trivial
/// destructor destroys it, so its memory is not dead when the call returns.
/// Target lowering reads that off the record's AST, which the CIR a fat object
/// carries does not have -- and whoever lowers it (cir-link) got registers and
/// dead_on_return for std::string and every other such class. The facts go
/// into cir.record_abi, record name to RecordABIFactBits, for the records that
/// appear in function signatures and calls: the only ones the calling
/// convention asks about. Trivially passable records are left out.
static void recordABIFactsForSerialization(mlir::ModuleOp Mod) {
  llvm::MapVector<mlir::StringAttr, unsigned> Facts;
  auto Note = [&](mlir::Type Ty) {
    auto Rec = mlir::dyn_cast<cir::RecordType>(Ty);
    if (!Rec || !Rec.getName())
      return;
    auto Ast = Rec.getAst();
    if (!Ast || !Ast.getRawDecl())
      return;
    const clang::RecordDecl *RD = Ast.getRawDecl();
    unsigned Bits = 0;
    if (RD->getArgPassingRestrictions() !=
        clang::RecordArgPassingKind::CanPassInRegs)
      Bits |= 1u << 0;
    if (const auto *CXXRD = llvm::dyn_cast<clang::CXXRecordDecl>(RD))
      if (!CXXRD->hasTrivialDestructor())
        Bits |= 1u << 1;
    if (RD->isParamDestroyedInCallee())
      Bits |= 1u << 2;
    if (Bits)
      Facts[Rec.getName()] = Bits;
  };
  Mod.walk([&](mlir::Operation *Op) {
    if (auto Fn = mlir::dyn_cast<cir::FuncOp>(Op)) {
      cir::FuncType FnTy = Fn.getFunctionType();
      for (mlir::Type In : FnTy.getInputs())
        Note(In);
      Note(FnTy.getReturnType());
    } else if (mlir::isa<cir::CallOp, cir::TryCallOp>(Op)) {
      for (mlir::Value Operand : Op->getOperands())
        Note(Operand.getType());
      for (mlir::Type Result : Op->getResultTypes())
        Note(Result);
    }
  });
  if (Facts.empty())
    return;
  mlir::MLIRContext *Ctx = Mod.getContext();
  llvm::SmallVector<mlir::NamedAttribute> Entries;
  for (auto &[Name, Bits] : Facts)
    Entries.emplace_back(Name, mlir::IntegerAttr::get(
                                   mlir::IntegerType::get(Ctx, 32), Bits));
  Mod->setAttr(cir::CIRDialect::getRecordABIFactsAttrName(),
               mlir::DictionaryAttr::get(Ctx, Entries));
}

static void stripASTAttrsForSerialization(mlir::ModuleOp Mod) {
  Mod.walk([](mlir::Operation *Op) {
    Op->removeAttr("ast");
    Op->removeAttr("cir.debug.var_decl");
  });
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
                 CodeGenOptions &CGO, std::unique_ptr<raw_pwrite_stream> OS,
                 CoverageSourceInfo *CoverageInfo)
      : Action(Action), CI(CI), OutputStream(std::move(OS)),
        FS(&CI.getVirtualFileSystem()),
        Gen(std::make_unique<CIRGenerator>(
            CI.getDiagnostics(), std::move(FS), CI.getCodeGenOpts(),
            Action == CIRGenAction::OutputType::EmitLLVM ||
                Action == CIRGenAction::OutputType::EmitBC ||
                Action == CIRGenAction::OutputType::EmitObj ||
                Action == CIRGenAction::OutputType::EmitAssembly,
            CoverageInfo)),
        FEOptions(CI.getFrontendOpts()), CGO(CGO) {}

  void Initialize(ASTContext &Ctx) override {
    assert(!Context && "initialized multiple times");
    Context = &Ctx;
    Gen->Initialize(Ctx);
    // Make Sema available during top-level emission so on-demand implicit
    // template instantiation can happen while declarations are created.
    if (CI.hasSema())
      Gen->setSema(CI.getSema());
  }

  bool HandleTopLevelDecl(DeclGroupRef D) override {
    if (CI.hasSema())
      Gen->setSema(CI.getSema());
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
    llvm::TimeTraceScope cirGenScope("CIRGen AST Consumer");

    // Pass Sema to CIRGenModule for on-demand template instantiation.
    // This must happen before release() so that implicit template
    // instantiations (e.g. std::forward<int>) can have their bodies
    // instantiated on demand during deferred emission.
    if (CI.hasSema())
      Gen->setSema(CI.getSema());
    Gen->HandleTranslationUnit(C);
    llvm::scope_exit releaseOnExit([this] { Gen->releaseModule(); });

    // CIRGen may already have diagnosed an unsupported codegen mode. Stop
    // before verifier/passes/backend work so we don't continue past a known
    // fatal frontend error.
    if (CI.getDiagnostics().hasErrorOccurred())
      return;

    if (!FEOptions.ClangIRDisableCIRVerifier) {
      if (!Gen->verifyModule()) {
        CI.getDiagnostics().Report(
            diag::err_cir_verification_failed_pre_passes);
        return;
      }
    }

    mlir::ModuleOp MlirModule = Gen->getModule();
    mlir::MLIRContext &MlirCtx = Gen->getMLIRContext();
    bool runMem2RegAfterDebugInfo = false;
    // CIR mem2reg runs in the direct-to-LLVM pipeline, after CallConvLowering:
    // run any earlier and it promotes a by-value aggregate return slot that
    // CallConvLowering is about to turn into an sret store, and the return
    // value is lost.
    bool runMem2RegInLowering = false;

    auto SetupCirPipelineAndExecute = [&] {
      llvm::TimeTraceScope cirPipelineScope("CIR Pipeline Driver");

      std::string LifetimeOpts, IdiomRecognizerOpts, LibOptOpts;
      if (FEOptions.ClangIRLifetimeCheck)
        LifetimeOpts = sanitizePassOptions(FEOptions.ClangIRLifetimeCheckOpts);
      if (FEOptions.ClangIRIdiomRecognizer)
        IdiomRecognizerOpts =
            sanitizePassOptions(FEOptions.ClangIRIdiomRecognizerOpts);
      if (FEOptions.ClangIRLibOpt)
        LibOptOpts = sanitizePassOptions(FEOptions.ClangIRLibOptOpts);

      // CIR local/parameter variable debug info is currently
      // materialized from surviving LLVM allocas during direct lowering.
      // Running CIR mem2reg before conversion erases those source allocas and
      // drops variable debug info entirely. Keep mem2reg enabled for normal
      // codegen, but move it past debug-info materialization whenever variable
      // debug info is requested until the CIR mem2reg path can preserve or
      // rebuild it.
      bool enableCIRMem2Reg = FEOptions.ClangIREnableMem2Reg;
      bool lowersToLLVMIR = Action == CIRGenAction::OutputType::EmitLLVM ||
                            Action == CIRGenAction::OutputType::EmitBC ||
                            Action == CIRGenAction::OutputType::EmitObj ||
                            Action == CIRGenAction::OutputType::EmitAssembly;
      if (enableCIRMem2Reg && lowersToLLVMIR) {
        switch (CGO.getDebugInfo()) {
        case llvm::codegenoptions::NoDebugInfo:
        case llvm::codegenoptions::LocTrackingOnly:
        case llvm::codegenoptions::DebugDirectivesOnly:
        case llvm::codegenoptions::DebugLineTablesOnly:
          break;
        case llvm::codegenoptions::DebugInfoConstructor:
        case llvm::codegenoptions::LimitedDebugInfo:
        case llvm::codegenoptions::FullDebugInfo:
        case llvm::codegenoptions::UnusedTypeInfo:
          enableCIRMem2Reg = false;
          runMem2RegAfterDebugInfo = true;
          break;
        }
      }

      runMem2RegInLowering = enableCIRMem2Reg;

      std::string PassOptParsingFailure;
      if (runCIRToCIRPasses(
              MlirModule, MlirCtx, C, !FEOptions.ClangIRDisableCIRVerifier,
              FEOptions.ClangIRLifetimeCheck, LifetimeOpts,
              CGO.OptimizationLevel > 0, FEOptions.ClangIREnableMoveOpt,
              FEOptions.ClangIRIdiomRecognizer, IdiomRecognizerOpts,
              FEOptions.ClangIRLibOpt, LibOptOpts, PassOptParsingFailure)
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
          // Report the verification failure through the diagnostics engine
          // so the compilation is considered failed. Previously this called
          // exit(1) directly, which crashed under -fintegrated-cc1 because
          // CrashRecoveryContext::HandleExit intercepted the exit.
          CI.getDiagnostics().Report(diag::err_cir_to_cir_transform_failed);
        }
      } else {
        mlir::SourceMgrDiagnosticHandler SourceMgrHandler(MlirSourceMgr,
                                                          &MlirCtx);
        SetupCirPipelineAndExecute();
      }
    }

    // If the CIR-to-CIR pipeline already emitted errors, stop here instead of
    // continuing into later emission/lowering stages. This avoids cascading
    // diagnostics (and potential crashes in unsupported backends) after a
    // known pass failure.
    if (CI.getDiagnostics().hasErrorOccurred())
      return;

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

    cir::InlinerOptions Inliner;
    Inliner.enabled = FEOptions.ClangIREnableInline;
    if (FEOptions.ClangIRInlineThreshold != ~0u)
      Inliner.threshold = FEOptions.ClangIRInlineThreshold;

    // The pass list real lowering runs, with the arguments it runs with. The
    // emit kinds that show CIR at a later stage take a prefix of it.
    auto PopulateLowering = [&](mlir::OpPassManager &pm,
                                std::optional<cir::CIRStage> StopAt) {
      cir::direct::populateCIRToLLVMPasses(
          pm, cir::getStage(MlirModule),
          /*runCallConvLowering=*/FEOptions.ClangIRCallConvLowering,
          /*runMem2Reg=*/runMem2RegInLowering, StopAt, Inliner);
    };
    // Print the CIR as lowering has it at Stage.
    auto EmitCIRAt = [&](cir::CIRStage Stage) {
      mlir::PassManager pm(&MlirCtx);
      PopulateLowering(pm, Stage);
      // Like lowering itself, which verifies once before its passes rather
      // than after each one.
      pm.enableVerifier(false);
      (void)mlir::applyPassManagerCLOptions(pm);
      if (pm.run(MlirModule).failed()) {
        CI.getDiagnostics().Report(diag::err_cir_to_cir_transform_failed);
        return;
      }
      mlir::OpPrintingFlags Flags;
      Flags.enableDebugInfo(/*enable=*/true, /*prettyForm=*/false);
      MlirModule->print(*OutputStream, Flags);
    };

    switch (Action) {
    case CIRGenAction::OutputType::EmitCIR:
      if (OutputStream && MlirModule) {
        mlir::OpPrintingFlags Flags;
        Flags.enableDebugInfo(/*enable=*/true, /*prettyForm=*/false);
        MlirModule->print(*OutputStream, Flags);
      }
      break;
    case CIRGenAction::OutputType::EmitMLIR:
      if (OutputStream && MlirModule) {
        StringRef EmitMLIRKind = FEOptions.ClangIREmitMLIR;
        // "cir" is the older spelling of "cir-prepare".
        if (EmitMLIRKind == "cir" || EmitMLIRKind == "cir-prepare") {
          // Same as EmitCIR
          mlir::OpPrintingFlags Flags;
          Flags.enableDebugInfo(/*enable=*/true, /*prettyForm=*/false);
          MlirModule->print(*OutputStream, Flags);
        } else if (EmitMLIRKind == "cir-abi") {
          EmitCIRAt(cir::CIRStage::ABI);
        } else if (EmitMLIRKind == "cir-flat") {
          EmitCIRAt(cir::CIRStage::Flat);
        } else if (EmitMLIRKind == "llvm") {
          // Lower to MLIR LLVM dialect and print
          mlir::PassManager pm(&MlirCtx);
          PopulateLowering(pm, /*StopAt=*/std::nullopt);
          (void)mlir::applyPassManagerCLOptions(pm);
          if (pm.run(MlirModule).failed()) {
            CI.getDiagnostics().Report(diag::err_cir_to_cir_transform_failed);
            return;
          }
          MlirModule->print(*OutputStream);
        } else if (EmitMLIRKind == "core") {
          if (FEOptions.ClangIRDirectLowering) {
            CI.getDiagnostics().Report(diag::err_drv_incompatible_options)
                << "-fclangir-direct-lowering" << "-emit-mlir=core";
            return;
          }
          // Lower CIR to MLIR core dialects and print
          mlir::PassManager pm(&MlirCtx);
          if (FEOptions.ClangIRCallConvLowering)
            pm.addPass(mlir::createCallConvLoweringPass());
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
      bool needsGCOVDebugInfo = !CGO.CoverageNotesFile.empty() ||
                                !CGO.CoverageDataFile.empty();
      bool DisableDebugInfo =
          CGO.getDebugInfo() == llvm::codegenoptions::NoDebugInfo &&
          !needsGCOVDebugInfo;

      // Set default target CPU and features on the LLVMContext so that
      // LLVM passes that create new functions (e.g., ASan's asan.module_ctor
      // via Function::createWithDefaultAttr) inherit the correct target
      // attributes. Matches OGCG's CodeGenAction.cpp setup.
      const auto &TargetOpts = CI.getTargetOpts();
      LLVMCtx.setDefaultTargetCPU(TargetOpts.CPU);
      LLVMCtx.setDefaultTargetFeatures(
          llvm::join(TargetOpts.Features, ","));

      LoadLinkModules(LLVMCtx);

      std::unique_ptr<llvm::Module> LLVMModule;
      // -fclangir-fat-objects: capture the CIR *before* lowering, because
      // lowering mutates the module in place and the code just below frees
      // it outright. Bytecode rather than text -- a container is read by
      // tooling, not people, and the text form of a real TU runs to
      // megabytes.
      std::string EmbeddedCIR;
      // And the bitcode of that CIR lowered as cir-link lowers it, without the
      // CIR inliner, for the link steps that read bitcode rather than CIR (see
      // below).
      std::string FatBitcode;
      // CallConvLowering only ever runs here, in lowering; the flag says
      // whether it runs at all.
      bool runCallConvLowering = FEOptions.ClangIRCallConvLowering;
      if (FEOptions.ClangIRFatObjects) {
        // On a clone, because the AST attributes have to come off before
        // serializing and the module below is still going to be lowered with
        // them in place.
        mlir::OwningOpRef<mlir::ModuleOp> FatModule(MlirModule.clone());
        recordABIFactsForSerialization(*FatModule);
        Gen->addRecordLocations(*FatModule);
        // The exception mode changes what inline code does -- libc++'s
        // __throw_* helpers throw, or abort under -fno-exceptions, under one
        // name -- so whoever merges this with other TUs needs to know it.
        if (CI.getLangOpts().CPlusPlus && !CI.getLangOpts().CXXExceptions)
          (*FatModule)
              ->setAttr(cir::CIRDialect::getNoCXXExceptionsAttrName(),
                        mlir::UnitAttr::get(FatModule->getContext()));
        // Likewise RTTI: a vtable or std::function policy built without it
        // has a null where a copy built with it points at a type_info.
        if (CI.getLangOpts().CPlusPlus && !CI.getLangOpts().RTTIData)
          (*FatModule)
              ->setAttr(cir::CIRDialect::getNoRTTIAttrName(),
                        mlir::UnitAttr::get(FatModule->getContext()));
        stripASTAttrsForSerialization(*FatModule);
        llvm::raw_string_ostream CIROS(EmbeddedCIR);
        if (mlir::failed(mlir::writeBytecodeToFile(*FatModule, CIROS))) {
          CI.getDiagnostics().Report(diag::err_fe_error_backend)
              << "could not serialize ClangIR for -fclangir-fat-objects";
          return;
        }
        // A cross-library optimizer can decide what a library keeps exported
        // from the bitcode of its fat objects: what their definitions and the
        // available_externally bodies they call reference. A relink takes this
        // object's CIR and inlines it its own way, so the bitcode has to be
        // that CIR's, before any inlining: the object's own lowering below runs
        // the CIR inliner, and a call it inlines here may stay out of line in
        // the relink, which then needs a symbol the optimizer stopped
        // exporting. The section is not .llvm.lto, which LLD's
        // --fat-lto-objects would link in place of the native code. A context
        // of its own keeps the object's type names as they were.
        llvm::TimeTraceScope bitcodeScope("Lower fat-object CIR to bitcode");
        llvm::LLVMContext FatLLVMCtx;
        std::unique_ptr<llvm::Module> FatLLVMModule = lowerFromCIRToLLVMIR(
            *FatModule, FatLLVMCtx, /*DisableDebugInfo=*/true,
            /*EnableVerifier=*/false, runCallConvLowering);
        // Without it Middle would read the object's .llvm.lto instead, so fail
        // the compile rather than produce an object it misreads.
        if (!FatLLVMModule) {
          CI.getDiagnostics().Report(diag::err_fe_error_backend)
              << "could not lower ClangIR to bitcode for -fclangir-fat-objects";
          return;
        }
        llvm::raw_string_ostream BCOS(FatBitcode);
        llvm::WriteBitcodeToFile(*FatLLVMModule, BCOS);
      }

      {
        llvm::TimeTraceScope cirToLLVMScope("CIR To LLVM Lowering");
        auto reportProfileReadError = [&](llvm::StringRef profilePath,
                                          llvm::StringRef reason) {
          CI.getDiagnostics().Report(diag::err_reading_profile)
              << profilePath << reason;
        };
        LLVMModule = lowerFromCIRToLLVMIR(
            MlirModule, LLVMCtx, DisableDebugInfo,
            !FEOptions.ClangIRDisableCIRVerifier, runCallConvLowering,
            runMem2RegAfterDebugInfo, runMem2RegInLowering,
            reportProfileReadError, Inliner);
      }
      if (!LLVMModule) {
        if (!CI.getDiagnostics().hasErrorOccurred())
          CI.getDiagnostics().Report(diag::err_cir_to_cir_transform_failed);
        return;
      }

      // Free the CIR module now — after lowering it is dead and can be very
      // large. The top-level ModuleOp is created via mlir::ModuleOp::create and
      // is not held by any OwningOpRef, so merely dropping the local handle
      // frees nothing; the operations must be erased explicitly. Without this,
      // the CIR stays resident through backend emission (where the global RSS
      // peak occurs), inflating peak memory. Nothing past this point reads the
      // CIR: emitCoverageMapping and populateFunctionSourceLocations use only
      // AST-side data and the LLVM module.
      Gen->releaseModule();
      MlirModule = {};

      LinkInModules(*LLVMModule);

      // Embed the -fstd-lib-stats=embed report (if any) in a dedicated
      // section of the output object as an @llvm.embedded.object global, so
      // that it can be harvested from the per-TU .o files of a whole-app
      // build.
      //
      // NOTE: embedBufferInModule tags the global with !exclude, so the
      // section comes out SHF_EXCLUDE and the linker drops it. That keeps the
      // linked artifact clean, but it also means the report never reaches the
      // .so: harvest it from the per-TU .o files.
      // Same mechanism, and the same caveat as the stats section above: the
      // global is tagged !exclude, so the section is SHF_EXCLUDE and the
      // linker drops it. That is the intent -- the CIR is harvested from the
      // per-TU .o files, which is exactly what a fat-object link-time
      // pipeline consumes, and the shipped .so stays clean.
      if (!EmbeddedCIR.empty())
        llvm::embedBufferInModule(*LLVMModule,
                                  llvm::MemoryBufferRef(EmbeddedCIR, "ClangIR"),
                                  ".cir");
      if (!FatBitcode.empty())
        llvm::embedBufferInModule(
            *LLVMModule, llvm::MemoryBufferRef(FatBitcode, "ClangIR-bitcode"),
            ".llvm.cir.bitcode");

      if (llvm::StringRef Report = CI.getStdLibStatsReport(); !Report.empty())
        llvm::embedBufferInModule(
            *LLVMModule, llvm::MemoryBufferRef(Report, "stdlib.stats"),
            ".stdlib_stats");

      if (!Gen->emitCoverageMapping(CI.getHeaderSearchOpts(),
                                    CI.getPreprocessorOpts(), *LLVMModule))
        return;

      BackendAction BEAction = getBackendActionFromOutputType(Action);
      {
        llvm::TimeTraceScope llvmBackendScope("LLVM Backend Emission");
        // Schedule the stale-personality stripper at OptimizerLast — after
        // LLVM EH simplification has had a chance to remove invokes.
        registerCIRPersonalityCleanup(CI.getCodeGenOpts());
        if (!emitBackendOutputWithDiagnosticHandler(
                CI, CI.getCodeGenOpts(),
                C.getTargetInfo().getDataLayoutString(), LLVMModule.get(),
                BEAction, FS, std::move(OutputStream), &C, Gen.get()))
          return;
      }
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
    : OwnedMLIRCtx(MLIRCtx ? nullptr : std::make_unique<mlir::MLIRContext>()),
      MLIRCtx(MLIRCtx ? MLIRCtx : OwnedMLIRCtx.get()), Action(Act) {}

CIRGenAction::~CIRGenAction() { MLIRMod.release(); }

static std::unique_ptr<raw_pwrite_stream>
getOutputStream(CompilerInstance &CI, StringRef InFile,
                CIRGenAction::OutputType Action) {
  switch (Action) {
  case CIRGenAction::OutputType::EmitAssembly:
    return CI.createDefaultOutputFile(false, InFile, "s");
  case CIRGenAction::OutputType::EmitCIR:
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

  CoverageSourceInfo *CoverageInfo = nullptr;
  if (CI.getCodeGenOpts().CoverageMapping)
    CoverageInfo =
        CodeGen::CoverageMappingModuleGen::setUpCoverageCallbacks(
            CI.getPreprocessor());

  auto Result = std::make_unique<cir::CIRGenConsumer>(
      Action, CI, CI.getCodeGenOpts(), std::move(Out), CoverageInfo);

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

  if (CodeGenOpts.CoverageMapping) {
    unsigned diagID = CI.getDiagnostics().getCustomDiagID(
        DiagnosticsEngine::Error,
        "ClangIR code gen Not Yet Implemented: %0");
    CI.getDiagnostics().Report(diagID)
        << "source-based coverage mapping (-fcoverage-mapping) for .cir input";
    return;
  }

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
  bool needsGCOVDebugInfo = !CodeGenOpts.CoverageNotesFile.empty() ||
                            !CodeGenOpts.CoverageDataFile.empty();
  bool DisableDebugInfo =
      CodeGenOpts.getDebugInfo() == llvm::codegenoptions::NoDebugInfo &&
      !needsGCOVDebugInfo;

  // Set default target CPU and features on the LLVMContext so that
  // LLVM passes that create new functions (e.g., ASan's asan.module_ctor
  // via Function::createWithDefaultAttr) inherit the correct target
  // attributes. Matches OGCG's CodeGenAction.cpp setup.
  const auto &TargetOpts = CI.getTargetOpts();
  LLVMCtx.setDefaultTargetCPU(TargetOpts.CPU);
  LLVMCtx.setDefaultTargetFeatures(
      llvm::join(TargetOpts.Features, ","));

  std::unique_ptr<llvm::Module> LLVMModule;
  {
    llvm::TimeTraceScope cirToLLVMScope("CIR To LLVM Lowering");
    auto reportProfileReadError = [&](llvm::StringRef profilePath,
                                      llvm::StringRef reason) {
      CI.getDiagnostics().Report(diag::err_reading_profile)
          << profilePath << reason;
    };
    // The pipeline schedules what the input still needs by the stage it
    // records; CIR written by hand records none and gets every step.
    LLVMModule = lowerFromCIRToLLVMIR(
        *MlirModule, LLVMCtx, DisableDebugInfo,
        !CI.getFrontendOpts().ClangIRDisableCIRVerifier,
        CI.getFrontendOpts().ClangIRCallConvLowering,
        /*RunMem2RegAfterDebugInfo=*/false,
        /*RunMem2RegInLowering=*/false, reportProfileReadError);
  }
  if (!LLVMModule)
    return;

  BackendAction BEAction = getBackendActionFromOutputType(Action);
  {
    llvm::TimeTraceScope llvmBackendScope("LLVM Backend Emission");
    // Schedule the stale-personality stripper at OptimizerLast — after LLVM
    // EH simplification has had a chance to remove invokes.
    registerCIRPersonalityCleanup(CI.getCodeGenOpts());
    if (!emitBackendOutputWithDiagnosticHandler(
            CI, CI.getCodeGenOpts(), CI.getTarget().getDataLayoutString(),
            LLVMModule.get(), BEAction, CI.getVirtualFileSystemPtr(),
            std::move(OS)))
      return;
  }
}

void cir::EmitAssemblyAction::anchor() {}
cir::EmitAssemblyAction::EmitAssemblyAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitAssembly, MLIRCtx) {}

void cir::EmitCIRAction::anchor() {}
cir::EmitCIRAction::EmitCIRAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitCIR, MLIRCtx) {}

void cir::EmitMLIRAction::anchor() {}
cir::EmitMLIRAction::EmitMLIRAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitMLIR, MLIRCtx) {}

void cir::EmitLLVMAction::anchor() {}
cir::EmitLLVMAction::EmitLLVMAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitLLVM, MLIRCtx) {}

void cir::EmitBCAction::anchor() {}
cir::EmitBCAction::EmitBCAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitBC, MLIRCtx) {}

void cir::EmitObjAction::anchor() {}
cir::EmitObjAction::EmitObjAction(mlir::MLIRContext *MLIRCtx)
    : CIRGenAction(OutputType::EmitObj, MLIRCtx) {}
