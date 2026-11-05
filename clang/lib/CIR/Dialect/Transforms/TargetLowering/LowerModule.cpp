//===--- LowerModule.cpp - Lower CIR Module to a Target -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file partially mimics clang/lib/CodeGen/CodeGenModule.cpp. The queries
// are adapted to operate on the CIR dialect, however.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclCXX.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"

#include "CIRLowerContext.h"
#include "LowerFunction.h"
#include "LowerModule.h"
#include "TargetInfo.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Support/LogicalResult.h"
#include "clang/CIR/Target/AArch64.h"
#include "llvm/Support/ErrorHandling.h"

using MissingFeatures = cir::MissingFeatures;
using AArch64ABIKind = cir::AArch64ABIKind;
using X86AVXABILevel = cir::X86AVXABILevel;

namespace cir {

static bool moduleHasSwiftCallingConventions(mlir::ModuleOp module);
static constexpr llvm::StringLiteral WindowsSwiftCXXABICompatAttrName =
    "cir.windows_swift_cxxabi_compat";

namespace {

static const clang::CXXRecordDecl *getCXXRecordDecl(mlir::Type ty) {
  auto recTy = mlir::dyn_cast<cir::RecordType>(ty);
  if (!recTy)
    return nullptr;

  auto ast = recTy.getAst();
  if (!ast)
    return nullptr;

  return llvm::dyn_cast<clang::CXXRecordDecl>(ast.getRawDecl());
}

static bool isWindowsSwiftCXXABICompatFunction(cir::FuncOp fn) {
  auto cc = fn.getCallingConv();
  const bool isSwiftFn =
      cc == cir::CallingConv::Swift || cc == cir::CallingConv::SwiftAsync;

  if (getCXXRecordDecl(fn.getFunctionType().getReturnType()))
    return false;

  for (mlir::Type ty : fn.getFunctionType().getInputs()) {
    const clang::CXXRecordDecl *cxxRD = getCXXRecordDecl(ty);
    if (!cxxRD)
      continue;

    // The shared Swift lowering path is only safe here when the C++ record is
    // forced indirect without needing Microsoft-specific return or prolog
    // handling.
    if (!isSwiftFn || cxxRD->canPassInRegisters() ||
        cxxRD->hasNonTrivialDestructor())
      return false;
  }

  return true;
}

static bool hasWindowsSwiftCXXABICompatAttr(mlir::ModuleOp module) {
  return module->hasAttr(WindowsSwiftCXXABICompatAttrName);
}

static void setWindowsSwiftCXXABICompatAttr(mlir::ModuleOp module) {
  module->setAttr(WindowsSwiftCXXABICompatAttrName,
                  mlir::UnitAttr::get(module.getContext()));
}

} // namespace

static CIRCXXABI *createCXXABI(LowerModule &CGM) {
  auto langAttr = mlir::dyn_cast_if_present<cir::SourceLanguageAttr>(
      CGM.getModule()->getAttr(cir::CIRDialect::getSourceLanguageAttrName()));
  if (CGM.getTarget().getTriple().isOSWindows() && langAttr &&
      langAttr.getValue() == cir::SourceLanguage::C)
    return CreateCABI(CGM);

  if (CGM.canUseWindowsSwiftCXXABICompat()) {
    // Preserve the module-wide compatibility decision across repeated
    // LowerModule reconstruction after call-conv lowering has rewritten away
    // the original Swift calling convention markers.
    setWindowsSwiftCXXABICompatAttr(CGM.getModule());
    return CreateCABI(CGM);
  }

  switch (CGM.getCXXABIKind()) {
  case clang::TargetCXXABI::AppleARM64:
  case clang::TargetCXXABI::Fuchsia:
  case clang::TargetCXXABI::GenericAArch64:
  case clang::TargetCXXABI::GenericARM:
  case clang::TargetCXXABI::iOS:
  case clang::TargetCXXABI::WatchOS:
  case clang::TargetCXXABI::GenericMIPS:
  case clang::TargetCXXABI::GenericItanium:
  case clang::TargetCXXABI::WebAssembly:
  case clang::TargetCXXABI::XL:
    return CreateItaniumCXXABI(CGM);
  case clang::TargetCXXABI::Microsoft:
    return CreateMicrosoftCXXABI(CGM);
  }

  cir_cconv_unreachable("invalid C++ ABI kind");
}

static std::unique_ptr<TargetLoweringInfo>
createTargetLoweringInfo(LowerModule &LM) {
  const clang::TargetInfo &Target = LM.getTarget();
  const llvm::Triple &Triple = Target.getTriple();

  switch (Triple.getArch()) {
  case llvm::Triple::arm:
  case llvm::Triple::thumb:
    return createARMTargetLoweringInfo(LM);
  case llvm::Triple::aarch64_be:
  case llvm::Triple::aarch64_32:
  case llvm::Triple::aarch64: {
    AArch64ABIKind Kind = AArch64ABIKind::AAPCS;
    if (Triple.isOSWindows())
      Kind = AArch64ABIKind::Win64;
    else if (Target.getABI() == "darwinpcs")
      Kind = AArch64ABIKind::DarwinPCS;
    else if (Target.getABI() == "aapcs-soft")
      Kind = AArch64ABIKind::AAPCSSoft;

    return createAArch64TargetLoweringInfo(LM, Kind);
  }
  case llvm::Triple::amdgcn:
    return createAMDGPUTargetLoweringInfo(LM);
  case llvm::Triple::x86:
    return createX86_32TargetLoweringInfo(LM);
  case llvm::Triple::x86_64: {
    llvm::StringRef ABI = Target.getABI();
    X86AVXABILevel AVXLevel =
        (ABI == "avx512"  ? X86AVXABILevel::AVX512
         : ABI == "avx"   ? X86AVXABILevel::AVX
                          : X86AVXABILevel::None);
    switch (Triple.getOS()) {
    case llvm::Triple::Win32:
      return createX86_64TargetLoweringInfo(LM, AVXLevel);
    default:
      return createX86_64TargetLoweringInfo(LM, AVXLevel);
    }
  }
  case llvm::Triple::spirv64:
    return createSPIRVTargetLoweringInfo(LM);

  case llvm::Triple::bpfeb:
  case llvm::Triple::bpfel:
    return createBPFTargetLoweringInfo(LM);

  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64:
    return createWebAssemblyTargetLoweringInfo(LM);

  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64: {
    llvm::StringRef ABIStr = Target.getABI();
    unsigned XLen = Target.getPointerWidth(clang::LangAS::Default);
    unsigned ABIFLen = 0;
    if (ABIStr.ends_with("f"))
      ABIFLen = 32;
    else if (ABIStr.ends_with("d"))
      ABIFLen = 64;
    bool EABI = ABIStr.ends_with("e");
    return createRISCVTargetLoweringInfo(LM, XLen, ABIFLen, EABI);
  }

  case llvm::Triple::nvptx:
  case llvm::Triple::nvptx64:
    return createNVPTXTargetLoweringInfo(LM);

  default:
    cir_cconv_unreachable("ABI NYI");
  }
}

static bool moduleHasSwiftCallingConventions(mlir::ModuleOp module) {
  return llvm::any_of(module.getOps<cir::FuncOp>(), [](cir::FuncOp fn) {
    auto cc = fn.getCallingConv();
    return cc == cir::CallingConv::Swift ||
           cc == cir::CallingConv::SwiftAsync;
  });
}

bool LowerModule::canUseWindowsSwiftCXXABICompat() {
  const llvm::Triple &triple = getTarget().getTriple();
  if (getCXXABIKind() != clang::TargetCXXABI::Microsoft ||
      triple.getArch() != llvm::Triple::x86_64 ||
      triple.getOS() != llvm::Triple::Win32)
    return false;

  if (hasWindowsSwiftCXXABICompatAttr(module))
    return true;

  if (!moduleHasSwiftCallingConventions(module))
    return false;

  return llvm::all_of(module.getOps<cir::FuncOp>(),
                      isWindowsSwiftCXXABICompatFunction);
}

bool LowerModule::functionCanUseWindowsSwiftCXXABICompat(FuncOp fn) {
  return canUseWindowsSwiftCXXABICompat() &&
         isWindowsSwiftCXXABICompatFunction(fn);
}

LowerModule::LowerModule(clang::LangOptions langOpts,
                         clang::CodeGenOptions codeGenOpts,
                         mlir::ModuleOp &module,
                         std::unique_ptr<clang::TargetInfo> targetInfo,
                         mlir::PatternRewriter &rewriter)
    : context(module, std::move(langOpts), std::move(codeGenOpts)),
      module(module), target(std::move(targetInfo)), abi(createCXXABI(*this)),
      types(*this), rewriter(rewriter) {
  context.initBuiltinTypes(*target);
}

const TargetLoweringInfo &LowerModule::getTargetLoweringInfo() {
  if (!targetLoweringInfo)
    targetLoweringInfo = createTargetLoweringInfo(*this);
  return *targetLoweringInfo;
}

void LowerModule::setCIRFunctionAttributes(FuncOp GD,
                                           const LowerFunctionInfo &Info,
                                           FuncOp F, bool IsThunk) {
  unsigned CallingConv;
  // NOTE(cir): The method below will update the F function in-place with the
  // proper attributes.
  constructAttributeList(GD.getName(), Info, GD, F, CallingConv,
                         /*AttrOnCallSite=*/false, IsThunk);
  // TODO(cir): Set Function's calling convention.
}

/// Override ABI classification for _Atomic aggregate types on targets whose
/// ABI classifier does not unwrap AtomicType.
///
/// CIRGen strips AtomicType during type conversion, losing the info that
/// OGCG's classifier uses. On x86_64, the classify() function works on
/// QualType directly and has no AtomicType case, so _Atomic struct falls
/// through to Memory class → indirect. On AArch64, the classifier uses
/// isAggregateTypeForABI() → getEvaluationKind() which unwraps AtomicType,
/// so _Atomic struct is classified like a normal aggregate (direct for
/// small structs). We replicate this target-specific behavior here.
void LowerModule::applyAtomicOverrides(FuncOp op,
                                       const LowerFunctionInfo &FI) {
  const llvm::Triple &triple = getTarget().getTriple();
  if (triple.getArch() != llvm::Triple::x86_64)
    return;

  if (op->hasAttr("cir.atomic_ret")) {
    // Use natural alignment of the return type (in bytes).
    unsigned alignBytes = getContext().getTypeAlign(FI.getReturnType()) / 8;
    const_cast<LowerFunctionInfo &>(FI).getReturnInfo() =
        ABIArgInfo::getIndirect(alignBytes);
  }
  unsigned argNo = 0;
  for (auto it = FI.arg_begin(), ie = FI.arg_end(); it != ie;
       ++it, ++argNo) {
    if (argNo < op.getNumArguments() && op.getArgAttr(argNo, "cir.atomic")) {
      // Use alignment 8 for atomic struct params on x86_64 (matches OGCG's
      // classify() → Memory → getIndirect(8) for _Atomic aggregates).
      const_cast<LowerFunctionInfoArgInfo &>(*it).info =
          ABIArgInfo::getIndirect(/*alignment=*/8);
    }
  }
}

/// Override ABI classification for __mfp8 params/returns.
/// CIR represents __mfp8 as <1 x i8> which the ABI classifier treats as an
/// illegal vector (coercing to i32). In OGCG, __mfp8 is BuiltinType::MFloat8
/// so the classifier passes it directly as <1 x i8>.
void LowerModule::applyMfp8Overrides(FuncOp op,
                                     const LowerFunctionInfo &FI,
                                     unsigned argOffset) {
  const llvm::Triple &triple = getTarget().getTriple();
  if (triple.getArch() != llvm::Triple::aarch64)
    return;

  if (op->hasAttr("cir.mfp8_ret")) {
    const_cast<LowerFunctionInfo &>(FI).getReturnInfo() =
        ABIArgInfo::getDirect(FI.getReturnType());
  }
  unsigned fiArgNo = 0;
  for (auto it = FI.arg_begin(), ie = FI.arg_end(); it != ie; ++it, ++fiArgNo) {
    if (fiArgNo < argOffset)
      continue;
    unsigned argNo = fiArgNo - argOffset;
    if (argNo < op.getNumArguments() && op.getArgAttr(argNo, "cir.mfp8")) {
      const_cast<LowerFunctionInfoArgInfo &>(*it).info =
          ABIArgInfo::getDirect(it->type);
    }
  }
}

/// Set function attributes for a function declaration.
///
/// This method is based on CodeGenModule::SetFunctionAttributes but it
/// altered to consider only the ABI/Target-related bits.
void LowerModule::setFunctionAttributes(FuncOp oldFn, FuncOp newFn,
                                        bool IsIncompleteFunction,
                                        bool IsThunk) {

  // TODO(cir): There's some special handling from attributes related to LLVM
  // intrinsics. Should we do that here as well?

  // Setup target-specific attributes.
  if (!IsIncompleteFunction) {
    const LowerFunctionInfo &FI = getTypes().arrangeGlobalDeclaration(oldFn);

    // Apply the same _Atomic aggregate override as in
    // rewriteFunctionDefinition. constructAttributeList needs the
    // corrected ABIArgInfo to set byval/align on indirect atomic params.
    applyAtomicOverrides(oldFn, FI);
    applyMfp8Overrides(oldFn, FI);

    setCIRFunctionAttributes(oldFn, FI, newFn, IsThunk);
  }

  // Propagate the 'returned' attribute from CIRGen to the ABI-lowered
  // function. CIRGen sets cir.returned on arg 0 for constructors/destructors
  // that return 'this' (HasThisReturn). The 'this' pointer always remains
  // at arg 0 through ABI lowering.
  if (oldFn.getNumArguments() > 0 && newFn.getNumArguments() > 0 &&
      oldFn.getArgAttr(0, "cir.returned"))
    newFn.setArgAttr(0, "cir.returned",
                     mlir::UnitAttr::get(oldFn.getContext()));

  // NOTE(cir): Skipping some linkage and other global value attributes here as
  // it might be better for CIRGen to handle them.

  // TODO(cir): Skipping section attributes here.

  // TODO(cir): Skipping error attributes here.

  // NOTE(cir) Skipping emissions that depend on codegen options, as well as
  // sanitizers handling here. Do this in CIRGen.

  // NOTE(cir): Skipping more things here that depend on codegen options.
  // ExtParameterInfo-driven attributes are handled via per-argument
  // attributes on FuncOp (set during CIRGen) and propagated in
  // constructAttributeList, not at this level.
}

/// Rewrites an existing function to conform to the ABI.
///
/// This method is based on CodeGenModule::EmitGlobalFunctionDefinition but it
/// considerably simplified as it tries to remove any CodeGen related code.
llvm::LogicalResult LowerModule::rewriteFunctionDefinition(FuncOp op) {
  mlir::OpBuilder::InsertionGuard guard(rewriter);
  rewriter.setInsertionPoint(op);

  // Get ABI/target-specific function information.
  const LowerFunctionInfo &FI = this->getTypes().arrangeGlobalDeclaration(op);

  // Override classification for _Atomic aggregate types.
  applyAtomicOverrides(op, FI);
  applyMfp8Overrides(op, FI);

  // Get ABI/target-specific function type.
  FuncType Ty = this->getTypes().getFunctionType(FI);

  // NOTE(cir): Skipping getAddrOfFunction and getOrCreateCIRFunction methods
  // here, as they are mostly codegen logic.

  // Create a new function with the ABI-specific types.
  FuncOp newFn = mlir::cast<FuncOp>(rewriter.cloneWithoutRegions(op));
  newFn.setType(Ty);

  // The clone above preserves arg_attrs from the original function, indexed
  // by original parameter positions. After setType changes the number of
  // arguments (e.g., struct S expanded into ptr + i64), these stale attrs
  // would be misapplied to the wrong arguments. Clear them so
  // setFunctionAttributes can set them correctly using the ABI mapping.
  newFn.setAllArgAttrs(
      llvm::SmallVector<mlir::DictionaryAttr>(Ty.getNumInputs()));

  // Set up ABI-specific function attributes.
  // Skip for runtime functions (cir.no_proto_attrs): these have no backing
  // AST declaration, matching OGCG's `if (D) SetFunctionAttributes(...)` in
  // GetOrCreateLLVMFunction which skips for runtime functions with null Decl.
  bool isRuntimeFn = op->hasAttr("cir.no_proto_attrs");
  if (!isRuntimeFn)
    setFunctionAttributes(op, newFn, false, /*IsThunk=*/false);
  // Strip the marker so it doesn't leak to later passes.
  newFn->removeAttr("cir.no_proto_attrs");
  // ExtParameterInfo-driven "ExtraAttrs" are propagated via
  // constructAttributeList (called from setFunctionAttributes) using
  // per-argument attributes on the original FuncOp.

  // Is a function definition: handle the body.
  // Alias functions have no body (no block arguments) but are not declarations
  // either, so skip code generation for them.
  if (!op.isDeclaration() && !op.getAliasee()) {
    if (LowerFunction(*this, rewriter, op, newFn)
            .generateCode(op, newFn, FI)
            .failed())
      return llvm::failure();
  }

  // Erase original ABI-agnostic function.
  rewriter.eraseOp(op);
  return llvm::success();
}

llvm::LogicalResult LowerModule::rewriteFunctionCall(CallOp callOp,
                                                     FuncOp funcOp) {
  mlir::OpBuilder::InsertionGuard guard(rewriter);
  rewriter.setInsertionPoint(callOp);

  // Create a new function with the ABI-specific calling convention.
  if (LowerFunction(*this, rewriter, funcOp, callOp)
          .rewriteCallOp(callOp)
          .failed())
    return llvm::failure();

  return llvm::success();
}

llvm::LogicalResult LowerModule::rewriteFunctionCall(TryCallOp tryCallOp,
                                                     FuncOp funcOp) {
  mlir::OpBuilder::InsertionGuard guard(rewriter);
  rewriter.setInsertionPoint(tryCallOp);

  if (LowerFunction(*this, rewriter, funcOp, tryCallOp)
          .rewriteCallOp(tryCallOp)
          .failed())
    return llvm::failure();

  return llvm::success();
}

RecordABIFacts LowerModule::getRecordABIFacts(RecordType rec) {
  RecordABIFacts facts;
  if (auto ast = rec.getAst()) {
    const clang::RecordDecl *rd = ast.getRawDecl();
    if (!rd)
      return facts;
    facts.passIndirectly = rd->getArgPassingRestrictions() !=
                           clang::RecordArgPassingKind::CanPassInRegs;
    if (const auto *cxxRD = llvm::dyn_cast<clang::CXXRecordDecl>(rd))
      facts.nonTrivialDestructor = !cxxRD->hasTrivialDestructor();
    facts.destroyedInCallee = rd->isParamDestroyedInCallee();
    return facts;
  }
  if (!rec.getName())
    return facts;
  auto table = module->getAttrOfType<mlir::DictionaryAttr>(
      cir::CIRDialect::getRecordABIFactsAttrName());
  if (!table)
    return facts;
  auto bits = table.getAs<mlir::IntegerAttr>(rec.getName().getValue());
  if (!bits)
    return facts;
  uint64_t value = bits.getValue().getZExtValue();
  facts.passIndirectly = value & RecordPassedIndirectly;
  facts.nonTrivialDestructor = value & RecordNonTrivialDestructor;
  facts.destroyedInCallee = value & RecordDestroyedInCallee;
  return facts;
}

// TODO: not to create it every time
std::unique_ptr<LowerModule>
createLowerModule(mlir::ModuleOp module, mlir::PatternRewriter &rewriter,
                  LowerModuleRequirement requirement,
                  std::optional<cir::SourceLanguage> language) {
  // If the triple is not present, e.g. CIR modules parsed from text, we
  // cannot init LowerModule properly.
  cir_cconv_assert(!cir::MissingFeatures::makeTripleAlwaysPresent());
  if (!module->hasAttr(cir::CIRDialect::getTripleAttrName()))
    return nullptr;

  // Fetch target information.
  llvm::Triple triple(mlir::cast<mlir::StringAttr>(
                          module->getAttr(cir::CIRDialect::getTripleAttrName()))
                          .getValue());

  // Check if the target architecture is supported for the full target-lowering
  // surface. Return nullptr for unsupported targets so passes like call-conv
  // lowering can skip gracefully instead of crashing with llvm_unreachable.
  //
  // CXX ABI lowering is narrower: it only needs a constructed LowerModule so
  // it can select the correct C++ ABI and data layout. Allow that pass to
  // build a LowerModule even on targets such as Win32 x86 C++ where the wider
  // target-lowering path is still intentionally gated off.
  auto archSupported = [&]() -> bool {
    switch (triple.getArch()) {
    case llvm::Triple::arm:
    case llvm::Triple::thumb:
      // Generic ARM AAPCS (soft-float / softfp) ABI lowering is implemented in
      // ARM.cpp, so allow non-Windows ARM. Hard-float AAPCS-VFP
      // homogeneous-aggregate-in-VFP classification is still pending and falls
      // back to the pre-lowering signature shapes inside ARMABIInfo; Swift
      // calling conventions continue to be handled there as before.
      return triple.getOS() != llvm::Triple::Win32;
    case llvm::Triple::aarch64:
    case llvm::Triple::aarch64_32:
    case llvm::Triple::aarch64_be:
      return true;
    case llvm::Triple::amdgcn:
    case llvm::Triple::bpfeb:
    case llvm::Triple::bpfel:
    case llvm::Triple::spirv64:
    case llvm::Triple::nvptx:
    case llvm::Triple::nvptx64:
    case llvm::Triple::wasm32:
    case llvm::Triple::wasm64:
    case llvm::Triple::riscv32:
    case llvm::Triple::riscv64:
      return true;
    default:
      switch (triple.getArch()) {
      case llvm::Triple::x86:
        if (triple.getOS() != llvm::Triple::Win32)
          return true;

        if (auto langAttr =
                mlir::dyn_cast_if_present<cir::SourceLanguageAttr>(
                    module->getAttr(
                        cir::CIRDialect::getSourceLanguageAttrName()));
            !langAttr || langAttr.getValue() != cir::SourceLanguage::C)
          return false;

        // Win32 x86 C modules can use the generic x86-32 ABI classifier.
        // This preserves OGCG's Win32 small-aggregate, vector, and aligned
        // indirect-passing behavior instead of leaving direct-CIR on the
        // pre-lowering signature shapes.
        return true;
      case llvm::Triple::x86_64:
        return true;
      default:
        return false;
      }
    }
  };
  if (requirement == LowerModuleRequirement::FullTargetLowering &&
      !archSupported())
    return nullptr;

  clang::TargetOptions targetOptions;
  targetOptions.Triple = triple.str();

  // Propagate the target ABI from the CIR module attribute so that
  // TargetInfo::getABI() returns the correct value (e.g. "darwinpcs" for
  // AArch64 Apple targets, "avx"/"avx512" for x86_64).  Without this the
  // reconstructed TargetInfo defaults to the base ABI.
  if (auto abiAttr = module->getAttrOfType<mlir::StringAttr>(
          cir::CIRDialect::getTargetABIAttrName()))
    targetOptions.ABI = abiAttr.getValue().str();

  auto targetInfo = clang::targets::AllocateTarget(triple, targetOptions);

  // Apply the ABI string to the TargetInfo (AllocateTarget only stores the
  // default; setABI is needed to actually switch, matching the flow in
  // TargetInfo::CreateTargetInfo).
  if (!targetOptions.ABI.empty())
    targetInfo->setABI(targetOptions.ABI);

  // Propagate target features from the module to the TargetInfo so that
  // target-specific ABI queries (e.g. TargetInfo::getABI(), which controls
  // X86AVXABILevel for x86_64) return correct results. Without this, the
  // TargetInfo defaults to the base ABI (e.g. no AVX register passing on
  // x86_64). Extract target features from any function's extra_attrs
  // dictionary, since all functions carry the resolved target feature string.
  for (auto funcOp : module.getOps<cir::FuncOp>()) {
    if (auto extraAttrs = funcOp.getExtraAttrs()) {
      auto elements = extraAttrs->getElements();
      if (auto featuresAttr =
              elements.getAs<mlir::StringAttr>("target-features")) {
        llvm::SmallVector<llvm::StringRef> parts;
        featuresAttr.getValue().split(parts, ',', /*MaxSplit=*/-1,
                                      /*KeepEmpty=*/false);
        std::vector<std::string> features;
        for (auto p : parts)
          features.push_back(p.str());

        llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> diagIDs(
            new clang::DiagnosticIDs());
        clang::DiagnosticOptions diagOpts;
        clang::DiagnosticsEngine diags(diagIDs, diagOpts);
        targetInfo->handleTargetFeatures(features, diags);
        break;
      }
    }
  }

  // Initialize language options from the module's CIR module attributes.
  // Only a subset of LangOptions is propagated; fields not yet propagated
  // (e.g. most sanitizer bits) retain their defaults.
  clang::LangOptions langOpts;
  if (!language)
    if (auto langAttr = mlir::dyn_cast_if_present<cir::SourceLanguageAttr>(
            module->getAttr(cir::CIRDialect::getSourceLanguageAttrName())))
      language = langAttr.getValue();
  if (language == cir::SourceLanguage::CXX ||
      language == cir::SourceLanguage::OpenCLC)
    langOpts.CPlusPlus = true;
  if (auto abiCompatAttr =
          module->getAttrOfType<mlir::IntegerAttr>("cir.clang_abi_compat")) {
    langOpts.setClangABICompat(
        static_cast<clang::LangOptions::ClangABI>(abiCompatAttr.getInt()));
  }
  // Restore -fc++-abi= so getCXXABIKind() returns the requested ABI rather than
  // the target default; it selects the method-pointer representation. This
  // mirrors ASTContext::getCXXABIKind, which classic CodeGen uses. Textual CIR
  // is a supported input, so the spelling is screened exactly as
  // CompilerInvocation screens the flag: an unknown one must not become an
  // out-of-range TargetCXXABI::Kind, and one this target does not support must
  // not reach createCXXABI, which has no case for several of them.
  if (auto cxxABIAttr =
          module->getAttrOfType<mlir::StringAttr>("cir.cxx_abi_kind")) {
    llvm::StringRef abiName = cxxABIAttr.getValue();
    if (clang::TargetCXXABI::isABI(abiName)) {
      clang::TargetCXXABI::Kind abiKind = clang::TargetCXXABI::getKind(abiName);
      if (clang::TargetCXXABI::isSupportedCXXABI(triple, abiKind))
        langOpts.CXXABI = abiKind;
    }
  }
  if (module->hasAttr("cir.sanitize_memory"))
    langOpts.Sanitize.set(clang::SanitizerKind::Memory);
  if (module->hasAttr("cir.sanitize_return"))
    langOpts.Sanitize.set(clang::SanitizerKind::Return);
  if (module->hasAttr("cir.relative_cxx_abi_vtables"))
    langOpts.RelativeCXXABIVTables = true;
  // Restore LangOpts.HIP so AMDGPUABIInfo::classifyKernelArgumentType coerces
  // kernel pointer arguments to the global address space only for HIP compiles,
  // matching classic CodeGen. Plain C/C++ amdgcn kernels keep generic pointers.
  if (module->hasAttr("cir.hip"))
    langOpts.HIP = true;

  // Restore -mlong-double-64/-mlong-double-128 so the reconstructed TargetInfo
  // reports the overridden long double format. adjust() (below) turns
  // LongDoubleSize into LongDoubleWidth/Align/Format; without it the target's
  // native format is used and the x86_64 ABI classifier misclassifies a
  // `long double` return (coercing it to x86_fp80 even under
  // -mlong-double-64/-128).
  if (auto ldSizeAttr =
          module->getAttrOfType<mlir::IntegerAttr>("cir.long_double_size"))
    langOpts.LongDoubleSize = ldSizeAttr.getInt();

  // Only a subset of CodeGenOptions is propagated via CIR module attributes;
  // fields not yet propagated (e.g. VirtualFunctionElimination,
  // WholeProgramVTables) retain their defaults.
  clang::CodeGenOptions codeGenOpts;

  if (auto optInfo = mlir::cast_if_present<cir::OptInfoAttr>(
          module->getAttr(cir::CIRDialect::getOptInfoAttrName()))) {
    codeGenOpts.OptimizationLevel = optInfo.getLevel();
    codeGenOpts.OptimizeSize = optInfo.getSize();
  }

  if (module->hasAttr("cir.relaxed_aliasing"))
    codeGenOpts.RelaxedAliasing = true;

  if (module->hasAttr("cir.sanitize_binary_metadata_covered"))
    codeGenOpts.SanitizeBinaryMetadataCovered = true;
  if (module->hasAttr("cir.sanitize_binary_metadata_atomics"))
    codeGenOpts.SanitizeBinaryMetadataAtomics = true;
  if (module->hasAttr("cir.sanitize_binary_metadata_uar"))
    codeGenOpts.SanitizeBinaryMetadataUAR = true;
  if (auto ignorelistAttrs = module->getAttrOfType<mlir::ArrayAttr>(
          "cir.sanitize_binary_metadata_ignorelist_files")) {
    codeGenOpts.SanitizeMetadataIgnorelistFiles.reserve(
        ignorelistAttrs.size());
    for (mlir::Attribute attr : ignorelistAttrs) {
      auto pathAttr = mlir::dyn_cast<mlir::StringAttr>(attr);
      if (!pathAttr)
        continue;
      codeGenOpts.SanitizeMetadataIgnorelistFiles.push_back(
          pathAttr.getValue().str());
    }
  }

  // Propagate -fpass-by-value-is-noalias from the module attribute.
  if (module->hasAttr("cir.pass_by_value_is_noalias"))
    codeGenOpts.PassByValueIsNoAlias = true;

  // Propagate -fno-strict-return from the module attribute.
  if (module->hasAttr("cir.no_strict_return"))
    codeGenOpts.StrictReturn = false;

  // Propagate -fno-pointer-tbaa from the module attribute.
  if (module->hasAttr("cir.no_pointer_tbaa"))
    codeGenOpts.PointerTBAA = false;

  if (auto keyAttr = module->getAttrOfType<mlir::IntegerAttr>(
          "cir.ptrauth_cxx_member_function_pointer_key")) {
    auto authModeAttr = module->getAttrOfType<mlir::IntegerAttr>(
        "cir.ptrauth_cxx_member_function_pointer_auth_mode");
    auto authMode = authModeAttr
                        ? static_cast<clang::PointerAuthenticationMode>(
                              authModeAttr.getInt())
                        : clang::PointerAuthenticationMode::SignAndAuth;
    codeGenOpts.PointerAuth.CXXMemberFunctionPointers = clang::PointerAuthSchema(
        static_cast<clang::PointerAuthSchema::ARM8_3Key>(keyAttr.getInt()),
        /*IsAddressDiscriminated=*/false, authMode,
        clang::PointerAuthSchema::Discrimination::None);
  }

  if (auto keyAttr = module->getAttrOfType<mlir::IntegerAttr>(
          "cir.ptrauth_cxx_vtable_pointer_key")) {
    auto authModeAttr = module->getAttrOfType<mlir::IntegerAttr>(
        "cir.ptrauth_cxx_vtable_pointer_auth_mode");
    auto authMode = authModeAttr
                        ? static_cast<clang::PointerAuthenticationMode>(
                              authModeAttr.getInt())
                        : clang::PointerAuthenticationMode::SignAndAuth;
    auto addrDiscAttr = module->getAttrOfType<mlir::BoolAttr>(
        "cir.ptrauth_cxx_vtable_pointer_addr_disc");
    bool isAddressDiscriminated =
        addrDiscAttr ? addrDiscAttr.getValue() : false;
    auto otherDiscAttr = module->getAttrOfType<mlir::IntegerAttr>(
        "cir.ptrauth_cxx_vtable_pointer_other_disc");
    auto otherDiscrimination =
        otherDiscAttr
            ? static_cast<clang::PointerAuthSchema::Discrimination>(
                  otherDiscAttr.getInt())
            : clang::PointerAuthSchema::Discrimination::None;
    auto constDiscAttr = module->getAttrOfType<mlir::IntegerAttr>(
        "cir.ptrauth_cxx_vtable_pointer_const_disc");
    std::optional<uint16_t> constantDiscriminator;
    if (constDiscAttr)
      constantDiscriminator = static_cast<uint16_t>(constDiscAttr.getInt());

    codeGenOpts.PointerAuth.CXXVTablePointers = clang::PointerAuthSchema(
        static_cast<clang::PointerAuthSchema::ARM8_3Key>(keyAttr.getInt()),
        isAddressDiscriminated, authMode, otherDiscrimination,
        constantDiscriminator);
  }

  // Re-apply the target adjustments that TargetInfo::CreateTargetInfo performs
  // in the driver, so overrides carried in LangOptions (e.g.
  // -mlong-double-64/-128 via LongDoubleSize) are reflected in the
  // reconstructed TargetInfo's LongDoubleWidth/Align/Format before it is used
  // by the ABI classifiers.
  {
    llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> diagIDs(
        new clang::DiagnosticIDs());
    clang::DiagnosticOptions diagOpts;
    clang::DiagnosticsEngine diags(diagIDs, diagOpts);
    targetInfo->adjust(diags, langOpts, /*Aux=*/nullptr);
  }

  return std::make_unique<LowerModule>(std::move(langOpts),
                                       std::move(codeGenOpts), module,
                                       std::move(targetInfo), rewriter);
}

} // namespace cir
