//===- LoweringPrepare.cpp - pareparation work for LLVM lowering ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LoweringPrepareCXXABI.h"
#include "PassDetail.h"
#include "mlir/IR/AttrTypeSubElements.h"
#include "mlir/IR/Attributes.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/Cuda.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CIR/Dialect/Builder/CIRBaseBuilder.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDataLayout.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/IR/CIRStage.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/MissingFeatures.h"
#include "clang/CIR/Target/AArch64.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

#include <memory>

using namespace mlir;
using namespace cir;

static std::string getCUDAPrefix(clang::ASTContext *astCtx) {
  if (astCtx->getLangOpts().HIP)
    return "hip";
  return "cuda";
}

static std::string addUnderscoredPrefix(llvm::StringRef cudaPrefix,
                                        llvm::StringRef cudaFunctionName) {
  return ("__" + cudaPrefix + cudaFunctionName).str();
}

namespace mlir {
#define GEN_PASS_DEF_LOWERINGPREPARE
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

static SmallString<128> getTransformedFileName(mlir::ModuleOp mlirModule) {
  SmallString<128> fileName;

  if (mlirModule.getSymName())
    fileName = llvm::sys::path::filename(mlirModule.getSymName()->str());

  if (fileName.empty())
    fileName = "<null>";

  for (size_t i = 0; i < fileName.size(); ++i) {
    // Replace everything that's not [a-zA-Z0-9._] with a _. This set happens
    // to be the set of C preprocessing numbers.
    if (!clang::isPreprocessingNumberBody(fileName[i]))
      fileName[i] = '_';
  }

  return fileName;
}

/// Return the FuncOp called by `callOp`.
static cir::FuncOp getCalledFunction(cir::CallOp callOp,
                                     mlir::SymbolTable *symTable) {
  if (!symTable)
    return nullptr;
  auto callee = callOp.getCallee();
  if (!callee)
    return nullptr;
  return symTable->lookup<cir::FuncOp>(*callee);
}

static mlir::ArrayAttr getAtExitCallbackPtrauthAttr(mlir::ModuleOp mlirModule) {
  return mlirModule->getAttrOfType<mlir::ArrayAttr>(
      "cir.atexit_callback_ptrauth");
}

static std::string makeUniqueHelperName(llvm::StringSet<> &existingSymbols,
                                        llvm::StringRef base) {
  std::string candidate = base.str();
  if (!existingSymbols.contains(candidate)) {
    existingSymbols.insert(candidate);
    return candidate;
  }

  for (unsigned n = 1;; ++n) {
    std::string suffixed = candidate + "." + std::to_string(n);
    if (!existingSymbols.contains(suffixed)) {
      existingSymbols.insert(suffixed);
      return suffixed;
    }
  }
}

static mlir::Value getTLSRegistrationAddress(CIRBaseBuilderTy &builder,
                                             mlir::Value addr) {
  if (auto getGlobal = addr.getDefiningOp<cir::GetGlobalOp>()) {
    if (!getGlobal.getTls())
      return addr;
    return cir::GetGlobalOp::create(builder, getGlobal.getLoc(),
                                    getGlobal.getType(), getGlobal.getName());
  }

  auto cast = addr.getDefiningOp<cir::CastOp>();
  if (!cast || cast.getKind() != cir::CastKind::bitcast)
    return addr;

  mlir::Value src = getTLSRegistrationAddress(builder, cast.getSrc());
  if (src == cast.getSrc())
    return addr;
  return cir::CastOp::create(builder, cast.getLoc(), cast.getType(),
                             cir::CastKind::bitcast, src);
}

namespace {

// Keep Microsoft C++ lowering explicit: allow target-specific va_arg support
// where we have it, but continue to hard-fail Microsoft-only C++ operations
// instead of silently using Itanium semantics.
class LoweringPrepareMicrosoftCXXABI final : public cir::LoweringPrepareCXXABI {
public:
  explicit LoweringPrepareMicrosoftCXXABI(const clang::TargetInfo &target) {
    switch (target.getTriple().getArch()) {
    case llvm::Triple::aarch64:
      helper.reset(cir::LoweringPrepareCXXABI::createAArch64ABI(
          cir::AArch64ABIKind::Win64));
      break;
    case llvm::Triple::x86_64:
      helper.reset(cir::LoweringPrepareCXXABI::createX86ABI(/*is64Bit=*/true));
      break;
    default:
      // Keep unsupported Microsoft C++ targets explicit at the operation
      // sites that actually need ABI help instead of crashing on any TU that
      // merely uses the Microsoft ABI.
      break;
    }
  }

  mlir::Value lowerDynamicCast(cir::CIRBaseBuilderTy &, clang::ASTContext &,
                               cir::DynamicCastOp) override {
    llvm_unreachable("Microsoft C++ dynamic_cast lowering NYI");
  }

  mlir::Value lowerVAArg(cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
                         const cir::CIRDataLayout &datalayout) override {
    if (!helper)
      llvm_unreachable("Microsoft C++ va_arg lowering NYI for this target");
    return helper->lowerVAArg(builder, op, datalayout);
  }

private:
  std::unique_ptr<cir::LoweringPrepareCXXABI> helper;
};

struct LoweringPreparePass
    : public impl::LoweringPrepareBase<LoweringPreparePass> {
  LoweringPreparePass() = default;
  void runOnOperation() override;

  void runOnOp(mlir::Operation *op);
  void lowerCastOp(cir::CastOp op);
  void lowerComplexAddOp(cir::ComplexAddOp op);
  void lowerComplexSubOp(cir::ComplexSubOp op);
  void lowerComplexDivOp(cir::ComplexDivOp op);
  void lowerComplexMulOp(cir::ComplexMulOp op);
  void lowerUnaryOp(cir::UnaryOp op);
  void lowerGlobalOp(cir::GlobalOp op);
  void lowerDynamicCastOp(cir::DynamicCastOp op);
  void lowerArrayDtor(cir::ArrayDtor op);
  void lowerArrayCtor(cir::ArrayCtor op);
  void lowerTrivialCopyCall(cir::CallOp op);
  void lowerStdFindOp(cir::StdFindOp op);
  void lowerStrLenOp(cir::StrLenOp op);
  void lowerIterBeginOp(cir::IterBeginOp op);
  void lowerIterEndOp(cir::IterEndOp op);
  void lowerThreeWayCmpOp(cir::CmpThreeWayOp op);
  void lowerThrowOp(cir::ThrowOp op);
  void lowerVAArgOp(cir::VAArgOp op);

  /// Build the function that initializes the specified global
  cir::FuncOp buildCXXGlobalVarDeclInitFunc(cir::GlobalOp op);

  /// Handle the dtor region by registering destructor with __cxa_atexit
  cir::FuncOp getOrCreateDtorFunc(CIRBaseBuilderTy &builder, cir::GlobalOp op,
                                  mlir::Region &dtorRegion,
                                  cir::CallOp &dtorCall,
                                  bool &createdHelper);

  /// Build a module init function that calls all the dynamic initializers.
  void buildCXXGlobalInitFunc();

  /// Build the __tls_init function with __tls_guard for lazy TLS init.
  void buildCXXTLSInitFunc();

  /// Materialize global ctor/dtor list
  void buildGlobalCtorDtorList();
  /// Build __GLOBAL_init_<priority> helpers for dtors that must be
  /// registered through __cxa_atexit / atexit.
  void buildRegisterGlobalDtorsWithAtExit();

  /// Collect annotations from a global or function into the module-level list
  void addGlobalAnnotations(mlir::Operation *op, mlir::ArrayAttr annotations);

  /// Materialize the global annotations attribute on the module
  void buildGlobalAnnotationValues();

  /// Fold complete-object structors tagged with `#cir.structor_dedup` into
  /// their base-object variant, the way OGCG's StructorCodegen::RAUW does.
  void foldDedupableStructors();

  ///
  /// CUDA related
  /// ------------

  // Maps CUDA kernel name to device stub function.
  llvm::StringMap<cir::FuncOp> cudaKernelMap;
  // Maps CUDA device-side variable name to host-side (shadow) GlobalOp.
  llvm::StringMap<cir::GlobalOp> cudaVarMap;

  void buildCUDAModuleCtor();
  std::optional<cir::FuncOp> buildCUDAModuleDtor();
  std::optional<cir::FuncOp> buildHIPModuleDtor();
  std::optional<cir::FuncOp> buildCUDARegisterGlobals();

  void buildCUDARegisterGlobalFunctions(cir::CIRBaseBuilderTy &builder,
                                        cir::FuncOp regGlobalFunc);
  void buildCUDARegisterVars(cir::CIRBaseBuilderTy &builder,
                             cir::FuncOp regGlobalFunc);

  cir::FuncOp buildRuntimeFunction(
      mlir::OpBuilder &builder, llvm::StringRef name, mlir::Location loc,
      cir::FuncType type,
      cir::GlobalLinkageKind linkage = cir::GlobalLinkageKind::ExternalLinkage);

  cir::GlobalOp buildRuntimeVariable(
      mlir::OpBuilder &builder, llvm::StringRef name, mlir::Location loc,
      mlir::Type type,
      cir::GlobalLinkageKind linkage = cir::GlobalLinkageKind::ExternalLinkage,
      cir::VisibilityKind visibility = cir::VisibilityKind::Default);

  ///
  /// AST related
  /// -----------

  clang::ASTContext *astCtx = nullptr;

  // Helper for lowering C++ ABI specific operations.
  std::shared_ptr<cir::LoweringPrepareCXXABI> cxxABI;

  /// Tracks current module.
  mlir::ModuleOp mlirModule;

  /// Cached symbol table for O(1) lookups.
  mlir::SymbolTable *symTable = nullptr;

  /// Data layout for type size queries.
  std::optional<cir::CIRDataLayout> datalayout;
  bool encounteredLoweringError = false;

  llvm::SmallVector<cir::FuncOp> dynamicInitializers;

  /// Tracks TLS dynamic initializers (separate from non-TLS).
  /// Only contains ordered (non-template) TLS initializers that go into
  /// __tls_init.
  llvm::SmallVector<cir::FuncOp> tlsDynamicInitializers;

  /// Tracks unordered (template/comdat) TLS initializers. These have their own
  /// individual init functions and _ZTH aliases point directly to them, not to
  /// __tls_init. Maps variable name → init function.
  llvm::StringMap<cir::FuncOp> tlsUnorderedInitMap;

  /// TLS model for the __tls_guard variable. Set from the first TLS variable
  /// with dynamic initialization. Defaults to GeneralDynamic but should match
  /// the -ftls-model setting.
  cir::TLS_Model tlsGuardModel = cir::TLS_Model::GeneralDynamic;
  bool tlsGuardModelSet = false;

  /// Tracks TLS variable names that need dynamic initialization. Used to
  /// determine which _ZTW* wrappers need to call the init function.
  llvm::StringSet<> tlsDynamicInitVarNames;

  /// List of ctors and their priorities to be called before main()
  /// The third element is the comdat key (associated global name), empty if
  /// none.
  llvm::SmallVector<std::tuple<std::string, uint32_t, std::string>, 4>
      globalCtorList;
  /// Parallel to globalCtorList: the lexical order of each entry, used to
  /// stable-sort the comdat entries to match OGCG's dependency order.
  llvm::SmallVector<unsigned, 4> globalCtorLexOrder;
  /// List of dtors and their priorities to be called when unloading module.
  llvm::SmallVector<std::tuple<std::string, uint32_t, std::string>, 4>
      globalDtorList;
  /// Dtors that should be registered through __cxa_atexit / atexit, grouped
  /// by priority while preserving insertion order.
  llvm::MapVector<uint32_t, llvm::SmallVector<std::string, 4>> dtorsUsingAtExit;
  /// List of annotations in the module
  llvm::SmallVector<mlir::Attribute, 4> globalAnnotations;
  /// All symbols already present in the module. Used to avoid collisions when
  /// synthesizing helper function names.
  llvm::StringSet<> existingSymbols;

  void setASTContext(clang::ASTContext *c) {
    astCtx = c;
    const clang::TargetInfo &target = c->getTargetInfo();
    switch (c->getCXXABIKind()) {
    case clang::TargetCXXABI::GenericItanium:
    case clang::TargetCXXABI::Fuchsia:
    case clang::TargetCXXABI::GenericMIPS:
    case clang::TargetCXXABI::WebAssembly:
    case clang::TargetCXXABI::XL:
      if (target.getTriple().getArch() == llvm::Triple::x86_64) {
        cxxABI.reset(cir::LoweringPrepareCXXABI::createX86ABI(/*is64bit=*/true));
        return;
      }
      if (target.getTriple().getArch() == llvm::Triple::x86) {
        cxxABI.reset(
            cir::LoweringPrepareCXXABI::createX86ABI(/*is64bit=*/false));
        return;
      }
      cxxABI.reset(cir::LoweringPrepareCXXABI::createItaniumABI());
      return;
    case clang::TargetCXXABI::GenericARM: {
      llvm::Triple::ArchType arch = target.getTriple().getArch();
      if (arch == llvm::Triple::arm || arch == llvm::Triple::thumb ||
          arch == llvm::Triple::armeb || arch == llvm::Triple::thumbeb) {
        cxxABI.reset(cir::LoweringPrepareCXXABI::createARMABI());
        return;
      }
      cxxABI.reset(cir::LoweringPrepareCXXABI::createItaniumABI());
      return;
    }
    case clang::TargetCXXABI::iOS:
    case clang::TargetCXXABI::WatchOS:
      cxxABI.reset(cir::LoweringPrepareCXXABI::createItaniumABI());
      return;
    case clang::TargetCXXABI::GenericAArch64:
      cxxABI.reset(cir::LoweringPrepareCXXABI::createAArch64ABI(
          target.getABI() == "aapcs-soft" ? cir::AArch64ABIKind::AAPCSSoft
                                           : cir::AArch64ABIKind::AAPCS));
      return;
    case clang::TargetCXXABI::AppleARM64:
      cxxABI.reset(cir::LoweringPrepareCXXABI::createAArch64ABI(
          cir::AArch64ABIKind::DarwinPCS));
      return;
    case clang::TargetCXXABI::Microsoft:
      // CIR has no full Microsoft-specific C++ lowering yet. Keep unsupported
      // Microsoft-only C++ operations explicit, but still allow target-specific
      // helpers needed by otherwise ABI-agnostic CIR (e.g. va_arg on Win64).
      if (!c->getLangOpts().CPlusPlus) {
        switch (target.getTriple().getArch()) {
        case llvm::Triple::aarch64:
          cxxABI.reset(cir::LoweringPrepareCXXABI::createAArch64ABI(
              cir::AArch64ABIKind::Win64));
          break;
        case llvm::Triple::x86_64:
          cxxABI.reset(
              cir::LoweringPrepareCXXABI::createX86ABI(/*is64Bit=*/true));
          break;
        default:
          cxxABI.reset(cir::LoweringPrepareCXXABI::createItaniumABI());
          break;
        }
        return;
      }
      cxxABI = std::make_unique<LoweringPrepareMicrosoftCXXABI>(target);
      return;
    }

    llvm_unreachable("NYI");
  }
};

} // namespace

cir::GlobalOp LoweringPreparePass::buildRuntimeVariable(
    mlir::OpBuilder &builder, llvm::StringRef name, mlir::Location loc,
    mlir::Type type, cir::GlobalLinkageKind linkage,
    cir::VisibilityKind visibility) {
  cir::GlobalOp g =
      symTable ? symTable->lookup<cir::GlobalOp>(name) : nullptr;
  if (!g) {
    g = cir::GlobalOp::create(builder, loc, name, type);
    g.setLinkageAttr(
        cir::GlobalLinkageKindAttr::get(builder.getContext(), linkage));
    mlir::SymbolTable::setSymbolVisibility(
        g, mlir::SymbolTable::Visibility::Private);
    g.setGlobalVisibilityAttr(
        cir::VisibilityAttr::get(builder.getContext(), visibility));
    if (symTable)
      symTable->insert(g);
  }
  return g;
}

cir::FuncOp LoweringPreparePass::buildRuntimeFunction(
    mlir::OpBuilder &builder, llvm::StringRef name, mlir::Location loc,
    cir::FuncType type, cir::GlobalLinkageKind linkage) {
  cir::FuncOp f = symTable ? symTable->lookup<cir::FuncOp>(name) : nullptr;
  if (!f) {
    f = cir::FuncOp::create(builder, loc, name, type);
    f.setLinkageAttr(
        cir::GlobalLinkageKindAttr::get(builder.getContext(), linkage));
    mlir::SymbolTable::setSymbolVisibility(
        f, mlir::SymbolTable::Visibility::Private);
    // Only mark *external* runtime function declarations so the
    // CallConvLowering pass skips noundef/signext/zeroext attribute emission.
    // Internal helpers (e.g. __cxx_global_array_dtor, __cxx_global_var_init)
    // are CIR-generated definitions that should still get normal attributes.
    // This matches OGCG's `if (D) SetFunctionAttributes(...)` in
    // GetOrCreateLLVMFunction.
    if (linkage == cir::GlobalLinkageKind::ExternalLinkage)
      f->setAttr("cir.no_proto_attrs",
                 mlir::UnitAttr::get(builder.getContext()));

    if (symTable)
      symTable->insert(f);
  }
  return f;
}

static void applyDefaultInternalFunctionAttrs(mlir::ModuleOp module,
                                              cir::FuncOp fn) {
  auto defaultAttrs = module->getAttrOfType<cir::ExtraFuncAttributesAttr>(
      "cir.default_internal_func_attrs");
  if (!defaultAttrs)
    return;

  fn.setExtraAttrsAttr(defaultAttrs);

  if (defaultAttrs.getElements().get("noinline"))
    fn.setInlineKind(cir::InlineKind::NoInline);
}

static mlir::Value lowerScalarToComplexCast(mlir::MLIRContext &ctx,
                                            cir::CastOp op) {
  cir::CIRBaseBuilderTy builder(ctx);
  builder.setInsertionPoint(op);

  mlir::Value src = op.getSrc();
  mlir::Value imag = builder.getNullValue(src.getType(), op.getLoc());
  return builder.createComplexCreate(op.getLoc(), src, imag);
}

static mlir::Value lowerComplexToScalarCast(mlir::MLIRContext &ctx,
                                            cir::CastOp op,
                                            cir::CastKind elemToBoolKind) {
  cir::CIRBaseBuilderTy builder(ctx);
  builder.setInsertionPoint(op);

  mlir::Value src = op.getSrc();
  if (!mlir::isa<cir::BoolType>(op.getType()))
    return builder.createComplexReal(op.getLoc(), src);

  // Complex cast to bool: (bool)(a+bi) => (bool)a || (bool)b
  mlir::Value srcReal = builder.createComplexReal(op.getLoc(), src);
  mlir::Value srcImag = builder.createComplexImag(op.getLoc(), src);

  cir::BoolType boolTy = builder.getBoolTy();
  mlir::Value srcRealToBool =
      builder.createCast(op.getLoc(), elemToBoolKind, srcReal, boolTy);
  mlir::Value srcImagToBool =
      builder.createCast(op.getLoc(), elemToBoolKind, srcImag, boolTy);
  return builder.createLogicalOr(op.getLoc(), srcRealToBool, srcImagToBool);
}

static mlir::Value lowerComplexToComplexCast(mlir::MLIRContext &ctx,
                                             cir::CastOp op,
                                             cir::CastKind scalarCastKind) {
  CIRBaseBuilderTy builder(ctx);
  builder.setInsertionPoint(op);

  mlir::Value src = op.getSrc();
  auto dstComplexElemTy =
      mlir::cast<cir::ComplexType>(op.getType()).getElementType();

  mlir::Value srcReal = builder.createComplexReal(op.getLoc(), src);
  mlir::Value srcImag = builder.createComplexImag(op.getLoc(), src);

  mlir::Value dstReal = builder.createCast(op.getLoc(), scalarCastKind, srcReal,
                                           dstComplexElemTy);
  mlir::Value dstImag = builder.createCast(op.getLoc(), scalarCastKind, srcImag,
                                           dstComplexElemTy);

  // Forward FP constraint attributes from the original complex cast to the
  // decomposed scalar casts so that constrained fpext/fptrunc are emitted.
  for (mlir::Value v : {dstReal, dstImag}) {
    if (auto castOp = v.getDefiningOp<cir::CastOp>()) {
      if (auto fpExcept = op->getAttr("cir.fp_exception"))
        castOp->setAttr("cir.fp_exception", fpExcept);
      if (auto roundMode = op->getAttr("cir.rounding_mode"))
        castOp->setAttr("cir.rounding_mode", roundMode);
    }
  }

  return builder.createComplexCreate(op.getLoc(), dstReal, dstImag);
}

void LoweringPreparePass::lowerCastOp(cir::CastOp op) {
  mlir::MLIRContext &ctx = getContext();
  mlir::Value loweredValue = [&]() -> mlir::Value {
    switch (op.getKind()) {
    case cir::CastKind::float_to_complex:
    case cir::CastKind::int_to_complex:
      return lowerScalarToComplexCast(ctx, op);
    case cir::CastKind::float_complex_to_real:
    case cir::CastKind::int_complex_to_real:
      return lowerComplexToScalarCast(ctx, op, op.getKind());
    case cir::CastKind::float_complex_to_bool:
      return lowerComplexToScalarCast(ctx, op, cir::CastKind::float_to_bool);
    case cir::CastKind::int_complex_to_bool:
      return lowerComplexToScalarCast(ctx, op, cir::CastKind::int_to_bool);
    case cir::CastKind::float_complex:
      return lowerComplexToComplexCast(ctx, op, cir::CastKind::floating);
    case cir::CastKind::float_complex_to_int_complex:
      return lowerComplexToComplexCast(ctx, op, cir::CastKind::float_to_int);
    case cir::CastKind::int_complex:
      return lowerComplexToComplexCast(ctx, op, cir::CastKind::integral);
    case cir::CastKind::int_complex_to_float_complex:
      return lowerComplexToComplexCast(ctx, op, cir::CastKind::int_to_float);
    default:
      return nullptr;
    }
  }();

  if (loweredValue) {
    op.replaceAllUsesWith(loweredValue);
    op.erase();
  }
}

void LoweringPreparePass::lowerComplexAddOp(cir::ComplexAddOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op);
  mlir::Location loc = op.getLoc();
  mlir::Value lhs = op.getLhs();
  mlir::Value rhs = op.getRhs();
  cir::FastMathFlags fmf = op.getFastMathFlags();
  mlir::Value lhsReal = builder.createComplexReal(loc, lhs);
  mlir::Value lhsImag = builder.createComplexImag(loc, lhs);
  mlir::Value rhsReal = builder.createComplexReal(loc, rhs);
  mlir::Value rhsImag = builder.createComplexImag(loc, rhs);
  mlir::Value resultReal =
      builder.createBinop(loc, lhsReal, cir::BinOpKind::Add, rhsReal, fmf);
  mlir::Value resultImag =
      builder.createBinop(loc, lhsImag, cir::BinOpKind::Add, rhsImag, fmf);
  mlir::Value result = builder.createComplexCreate(loc, resultReal, resultImag);
  op.replaceAllUsesWith(result);
  op.erase();
}

void LoweringPreparePass::lowerComplexSubOp(cir::ComplexSubOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op);
  mlir::Location loc = op.getLoc();
  mlir::Value lhs = op.getLhs();
  mlir::Value rhs = op.getRhs();
  cir::FastMathFlags fmf = op.getFastMathFlags();
  mlir::Value lhsReal = builder.createComplexReal(loc, lhs);
  mlir::Value lhsImag = builder.createComplexImag(loc, lhs);
  mlir::Value rhsReal = builder.createComplexReal(loc, rhs);
  mlir::Value rhsImag = builder.createComplexImag(loc, rhs);
  mlir::Value resultReal =
      builder.createBinop(loc, lhsReal, cir::BinOpKind::Sub, rhsReal, fmf);
  mlir::Value resultImag =
      builder.createBinop(loc, lhsImag, cir::BinOpKind::Sub, rhsImag, fmf);
  mlir::Value result = builder.createComplexCreate(loc, resultReal, resultImag);
  op.replaceAllUsesWith(result);
  op.erase();
}

static mlir::Value buildComplexBinOpLibCall(
    LoweringPreparePass &pass, CIRBaseBuilderTy &builder,
    llvm::StringRef (*libFuncNameGetter)(llvm::APFloat::Semantics),
    mlir::Location loc, cir::ComplexType ty, mlir::Value lhsReal,
    mlir::Value lhsImag, mlir::Value rhsReal, mlir::Value rhsImag,
    cir::FuncOp parentFunc = nullptr) {
  cir::FPTypeInterface elementTy =
      mlir::cast<cir::FPTypeInterface>(ty.getElementType());

  llvm::StringRef libFuncName = libFuncNameGetter(
      llvm::APFloat::SemanticsToEnum(elementTy.getFloatSemantics()));
  llvm::SmallVector<mlir::Type, 4> libFuncInputTypes(4, elementTy);

  cir::FuncType libFuncTy = cir::FuncType::get(libFuncInputTypes, ty);

  // Insert a declaration for the runtime function to be used in Complex
  // multiplication and division when needed
  cir::FuncOp libFunc;
  {
    mlir::OpBuilder::InsertionGuard ipGuard{builder};
    builder.setInsertionPointToStart(pass.mlirModule.getBody());
    libFunc = pass.buildRuntimeFunction(builder, libFuncName, loc, libFuncTy);
  }

  // OGCG creates a FunctionProtoType with EST_BasicNoexcept and goes through
  // the full EmitCall/ConstructAttributeList path, which adds nounwind on
  // the callsite (from noexcept) and noundef on each parameter (from
  // DetermineNoUndef).  Remove cir.no_proto_attrs so CallConvLowering adds
  // noundef, and set nothrow so the call gets nounwind.
  libFunc->removeAttr("cir.no_proto_attrs");

  cir::CallOp call =
      builder.createCallOp(loc, libFunc, {lhsReal, lhsImag, rhsReal, rhsImag});
  call.setNothrow(true);

  // OGCG propagates strictfp from the enclosing function to the call site
  // (via EmitRuntimeCall → EmitCall). Replicate that here: if the parent
  // function has strictfp, set it on the call's extra attrs so that
  // LowerToLLVM emits the LLVM strictfp call-site attribute.
  if (!parentFunc)
    parentFunc = call->getParentOfType<cir::FuncOp>();
  if (parentFunc) {
    if (auto extraAttrs = parentFunc.getExtraAttrs()) {
      if (extraAttrs->getElements().get("strictfp")) {
        mlir::NamedAttrList callAttrs;
        if (auto existing = call.getExtraAttrs())
          callAttrs = mlir::NamedAttrList{existing->getElements().getValue()};
        callAttrs.set("strictfp",
                      mlir::UnitAttr::get(builder.getContext()));
        call.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
            callAttrs.getDictionary(builder.getContext())));
      }
    }
  }

  return call.getResult();
}

static llvm::StringRef
getComplexDivLibCallName(llvm::APFloat::Semantics semantics) {
  switch (semantics) {
  case llvm::APFloat::S_IEEEhalf:
    return "__divhc3";
  case llvm::APFloat::S_IEEEsingle:
    return "__divsc3";
  case llvm::APFloat::S_IEEEdouble:
    return "__divdc3";
  case llvm::APFloat::S_PPCDoubleDouble:
    return "__divtc3";
  case llvm::APFloat::S_x87DoubleExtended:
    return "__divxc3";
  case llvm::APFloat::S_IEEEquad:
    return "__divtc3";
  default:
    llvm_unreachable("unsupported floating point type");
  }
}

static mlir::Value
buildAlgebraicComplexDiv(CIRBaseBuilderTy &builder, mlir::Location loc,
                         mlir::Value lhsReal, mlir::Value lhsImag,
                         mlir::Value rhsReal, mlir::Value rhsImag,
                         cir::FastMathFlags fmf = cir::FastMathFlags::none) {
  // (a+bi) / (c+di) = ((ac+bd)/(cc+dd)) + ((bc-ad)/(cc+dd))i
  mlir::Value &a = lhsReal;
  mlir::Value &b = lhsImag;
  mlir::Value &c = rhsReal;
  mlir::Value &d = rhsImag;

  mlir::Value ac = builder.createBinop(loc, a, cir::BinOpKind::Mul, c, fmf);
  mlir::Value bd = builder.createBinop(loc, b, cir::BinOpKind::Mul, d, fmf);
  mlir::Value cc = builder.createBinop(loc, c, cir::BinOpKind::Mul, c, fmf);
  mlir::Value dd = builder.createBinop(loc, d, cir::BinOpKind::Mul, d, fmf);
  mlir::Value acbd =
      builder.createBinop(loc, ac, cir::BinOpKind::Add, bd, fmf);
  mlir::Value ccdd =
      builder.createBinop(loc, cc, cir::BinOpKind::Add, dd, fmf);
  mlir::Value resultReal =
      builder.createBinop(loc, acbd, cir::BinOpKind::Div, ccdd, fmf);

  mlir::Value bc = builder.createBinop(loc, b, cir::BinOpKind::Mul, c, fmf);
  mlir::Value ad = builder.createBinop(loc, a, cir::BinOpKind::Mul, d, fmf);
  mlir::Value bcad =
      builder.createBinop(loc, bc, cir::BinOpKind::Sub, ad, fmf);
  mlir::Value resultImag =
      builder.createBinop(loc, bcad, cir::BinOpKind::Div, ccdd, fmf);
  return builder.createComplexCreate(loc, resultReal, resultImag);
}

static mlir::Value
buildRangeReductionComplexDiv(CIRBaseBuilderTy &builder, mlir::Location loc,
                              mlir::Value lhsReal, mlir::Value lhsImag,
                              mlir::Value rhsReal, mlir::Value rhsImag,
                              cir::FastMathFlags fmf = cir::FastMathFlags::none) {
  // Implements Smith's algorithm for complex division.
  // SMITH, R. L. Algorithm 116: Complex division. Commun. ACM 5, 8 (1962).

  // Let:
  //   - lhs := a+bi
  //   - rhs := c+di
  //   - result := lhs / rhs = e+fi
  //
  // The algorithm pseudocode looks like follows:
  //   if fabs(c) >= fabs(d):
  //     r := d / c
  //     tmp := c + r*d
  //     e = (a + b*r) / tmp
  //     f = (b - a*r) / tmp
  //   else:
  //     r := c / d
  //     tmp := d + r*c
  //     e = (a*r + b) / tmp
  //     f = (b*r - a) / tmp

  mlir::Value &a = lhsReal;
  mlir::Value &b = lhsImag;
  mlir::Value &c = rhsReal;
  mlir::Value &d = rhsImag;

  auto trueBranchBuilder = [&](mlir::OpBuilder &, mlir::Location) {
    mlir::Value r =
        builder.createBinop(loc, d, cir::BinOpKind::Div, c, fmf);
    mlir::Value rd =
        builder.createBinop(loc, r, cir::BinOpKind::Mul, d, fmf);
    mlir::Value tmp =
        builder.createBinop(loc, c, cir::BinOpKind::Add, rd, fmf);

    mlir::Value br =
        builder.createBinop(loc, b, cir::BinOpKind::Mul, r, fmf);
    mlir::Value abr =
        builder.createBinop(loc, a, cir::BinOpKind::Add, br, fmf);
    mlir::Value e =
        builder.createBinop(loc, abr, cir::BinOpKind::Div, tmp, fmf);

    mlir::Value ar =
        builder.createBinop(loc, a, cir::BinOpKind::Mul, r, fmf);
    mlir::Value bar =
        builder.createBinop(loc, b, cir::BinOpKind::Sub, ar, fmf);
    mlir::Value f =
        builder.createBinop(loc, bar, cir::BinOpKind::Div, tmp, fmf);

    mlir::Value result = builder.createComplexCreate(loc, e, f);
    builder.createYield(loc, result);
  };

  auto falseBranchBuilder = [&](mlir::OpBuilder &, mlir::Location) {
    mlir::Value r =
        builder.createBinop(loc, c, cir::BinOpKind::Div, d, fmf);
    mlir::Value rc =
        builder.createBinop(loc, r, cir::BinOpKind::Mul, c, fmf);
    mlir::Value tmp =
        builder.createBinop(loc, d, cir::BinOpKind::Add, rc, fmf);

    mlir::Value ar =
        builder.createBinop(loc, a, cir::BinOpKind::Mul, r, fmf);
    mlir::Value arb =
        builder.createBinop(loc, ar, cir::BinOpKind::Add, b, fmf);
    mlir::Value e =
        builder.createBinop(loc, arb, cir::BinOpKind::Div, tmp, fmf);

    mlir::Value br =
        builder.createBinop(loc, b, cir::BinOpKind::Mul, r, fmf);
    mlir::Value bra =
        builder.createBinop(loc, br, cir::BinOpKind::Sub, a, fmf);
    mlir::Value f =
        builder.createBinop(loc, bra, cir::BinOpKind::Div, tmp, fmf);

    mlir::Value result = builder.createComplexCreate(loc, e, f);
    builder.createYield(loc, result);
  };

  auto cFabs = cir::FAbsOp::create(builder, loc, c);
  auto dFabs = cir::FAbsOp::create(builder, loc, d);
  cir::CmpOp cmpResult =
      builder.createCompare(loc, cir::CmpOpKind::fugt, cFabs, dFabs);
  auto ternary = cir::TernaryOp::create(builder, loc, cmpResult,
                                        trueBranchBuilder, falseBranchBuilder);

  return ternary.getResult();
}

static mlir::Type higherPrecisionElementTypeForComplexArithmetic(
    mlir::MLIRContext &context, clang::ASTContext &cc,
    CIRBaseBuilderTy &builder, mlir::Type elementType) {

  auto getHigherPrecisionFPType = [&context, &cc](mlir::Type type) -> mlir::Type {
    if (mlir::isa<cir::FP16Type>(type))
      return cir::SingleType::get(&context);

    if (mlir::isa<cir::SingleType>(type) || mlir::isa<cir::BF16Type>(type))
      return cir::DoubleType::get(&context);

    if (mlir::isa<cir::DoubleType>(type)) {
      // Determine the correct underlying type for LongDoubleType from the
      // target info, since long double format varies by platform (x86_fp80 on
      // x86-64 Linux, double on Windows x64/AArch64, fp128 on some targets).
      const llvm::fltSemantics &ldFormat =
          cc.getTargetInfo().getLongDoubleFormat();
      mlir::Type underlying;
      if (&ldFormat == &llvm::APFloat::IEEEdouble())
        underlying = cir::DoubleType::get(&context);
      else if (&ldFormat == &llvm::APFloat::x87DoubleExtended())
        underlying = cir::FP80Type::get(&context);
      else if (&ldFormat == &llvm::APFloat::IEEEquad())
        underlying = cir::FP128Type::get(&context);
      else
        return {};
      return cir::LongDoubleType::get(&context, underlying);
    }

    return type;
  };

  auto getFloatTypeSemantics =
      [&cc](mlir::Type type) -> const llvm::fltSemantics & {
    const clang::TargetInfo &info = cc.getTargetInfo();
    if (mlir::isa<cir::FP16Type>(type))
      return info.getHalfFormat();

    if (mlir::isa<cir::BF16Type>(type))
      return info.getBFloat16Format();

    if (mlir::isa<cir::SingleType>(type))
      return info.getFloatFormat();

    if (mlir::isa<cir::DoubleType>(type))
      return info.getDoubleFormat();

    if (mlir::isa<cir::LongDoubleType>(type)) {
      if (cc.getLangOpts().OpenMP && cc.getLangOpts().OpenMPIsTargetDevice)
        llvm_unreachable("NYI Float type semantics with OpenMP");
      return info.getLongDoubleFormat();
    }

    if (mlir::isa<cir::FP128Type>(type)) {
      if (cc.getLangOpts().OpenMP && cc.getLangOpts().OpenMPIsTargetDevice)
        llvm_unreachable("NYI Float type semantics with OpenMP");
      return info.getFloat128Format();
    }

    llvm_unreachable("Unsupported float type semantics");
  };

  const mlir::Type higherElementType = getHigherPrecisionFPType(elementType);
  const llvm::fltSemantics &elementTypeSemantics =
      getFloatTypeSemantics(elementType);
  const llvm::fltSemantics &higherElementTypeSemantics =
      getFloatTypeSemantics(higherElementType);

  // Check that the promoted type can handle the intermediate values without
  // overflowing. This can be interpreted as:
  // (SmallerType.LargestFiniteVal * SmallerType.LargestFiniteVal) * 2 <=
  //      LargerType.LargestFiniteVal.
  // In terms of exponent it gives this formula:
  // (SmallerType.LargestFiniteVal * SmallerType.LargestFiniteVal
  // doubles the exponent of SmallerType.LargestFiniteVal)
  if (llvm::APFloat::semanticsMaxExponent(elementTypeSemantics) * 2 + 1 <=
      llvm::APFloat::semanticsMaxExponent(higherElementTypeSemantics)) {
    return higherElementType;
  }

  // The intermediate values can't be represented in the promoted type
  // without overflowing.
  return {};
}

static mlir::Value
lowerComplexDiv(LoweringPreparePass &pass, CIRBaseBuilderTy &builder,
                mlir::Location loc, cir::ComplexDivOp op, mlir::Value lhsReal,
                mlir::Value lhsImag, mlir::Value rhsReal, mlir::Value rhsImag,
                mlir::MLIRContext &mlirCx, clang::ASTContext &cc) {
  cir::ComplexType complexTy = op.getType();
  cir::FastMathFlags fmf = op.getFastMathFlags();
  if (mlir::isa<cir::FPTypeInterface>(complexTy.getElementType())) {
    cir::ComplexRangeKind range = op.getRange();
    if (range == cir::ComplexRangeKind::Improved)
      return buildRangeReductionComplexDiv(builder, loc, lhsReal, lhsImag,
                                           rhsReal, rhsImag, fmf);

    if (range == cir::ComplexRangeKind::Full)
      return buildComplexBinOpLibCall(pass, builder, &getComplexDivLibCallName,
                                      loc, complexTy, lhsReal, lhsImag, rhsReal,
                                      rhsImag);

    if (range == cir::ComplexRangeKind::Promoted) {
      mlir::Type originalElementType = complexTy.getElementType();
      mlir::Type higherPrecisionElementType =
          higherPrecisionElementTypeForComplexArithmetic(mlirCx, cc, builder,
                                                         originalElementType);

      if (!higherPrecisionElementType)
        return buildRangeReductionComplexDiv(builder, loc, lhsReal, lhsImag,
                                             rhsReal, rhsImag, fmf);

      cir::CastKind floatingCastKind = cir::CastKind::floating;
      lhsReal = builder.createCast(floatingCastKind, lhsReal,
                                   higherPrecisionElementType);
      lhsImag = builder.createCast(floatingCastKind, lhsImag,
                                   higherPrecisionElementType);
      rhsReal = builder.createCast(floatingCastKind, rhsReal,
                                   higherPrecisionElementType);
      rhsImag = builder.createCast(floatingCastKind, rhsImag,
                                   higherPrecisionElementType);

      mlir::Value algebraicResult = buildAlgebraicComplexDiv(
          builder, loc, lhsReal, lhsImag, rhsReal, rhsImag, fmf);

      mlir::Value resultReal = builder.createComplexReal(loc, algebraicResult);
      mlir::Value resultImag = builder.createComplexImag(loc, algebraicResult);

      mlir::Value finalReal =
          builder.createCast(floatingCastKind, resultReal, originalElementType);
      mlir::Value finalImag =
          builder.createCast(floatingCastKind, resultImag, originalElementType);
      return builder.createComplexCreate(loc, finalReal, finalImag);
    }
  }

  return buildAlgebraicComplexDiv(builder, loc, lhsReal, lhsImag, rhsReal,
                                  rhsImag, fmf);
}

void LoweringPreparePass::lowerComplexDivOp(cir::ComplexDivOp op) {
  cir::CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op);
  mlir::Location loc = op.getLoc();
  mlir::TypedValue<cir::ComplexType> lhs = op.getLhs();
  mlir::TypedValue<cir::ComplexType> rhs = op.getRhs();
  mlir::Value lhsReal = builder.createComplexReal(loc, lhs);
  mlir::Value lhsImag = builder.createComplexImag(loc, lhs);
  mlir::Value rhsReal = builder.createComplexReal(loc, rhs);
  mlir::Value rhsImag = builder.createComplexImag(loc, rhs);

  mlir::Value loweredResult =
      lowerComplexDiv(*this, builder, loc, op, lhsReal, lhsImag, rhsReal,
                      rhsImag, getContext(), *astCtx);
  op.replaceAllUsesWith(loweredResult);
  op.erase();
}

static llvm::StringRef
getComplexMulLibCallName(llvm::APFloat::Semantics semantics) {
  switch (semantics) {
  case llvm::APFloat::S_IEEEhalf:
    return "__mulhc3";
  case llvm::APFloat::S_IEEEsingle:
    return "__mulsc3";
  case llvm::APFloat::S_IEEEdouble:
    return "__muldc3";
  case llvm::APFloat::S_PPCDoubleDouble:
    return "__multc3";
  case llvm::APFloat::S_x87DoubleExtended:
    return "__mulxc3";
  case llvm::APFloat::S_IEEEquad:
    return "__multc3";
  default:
    llvm_unreachable("unsupported floating point type");
  }
}

static mlir::Value lowerComplexMul(LoweringPreparePass &pass,
                                   CIRBaseBuilderTy &builder,
                                   mlir::Location loc, cir::ComplexMulOp op,
                                   mlir::Value lhsReal, mlir::Value lhsImag,
                                   mlir::Value rhsReal, mlir::Value rhsImag,
                                   cir::FuncOp parentFunc) {
  cir::FastMathFlags fmf = op.getFastMathFlags();
  // (a+bi) * (c+di) = (ac-bd) + (ad+bc)i
  mlir::Value resultRealLhs =
      builder.createBinop(loc, lhsReal, cir::BinOpKind::Mul, rhsReal, fmf);
  mlir::Value resultRealRhs =
      builder.createBinop(loc, lhsImag, cir::BinOpKind::Mul, rhsImag, fmf);
  mlir::Value resultImagLhs =
      builder.createBinop(loc, lhsReal, cir::BinOpKind::Mul, rhsImag, fmf);
  mlir::Value resultImagRhs =
      builder.createBinop(loc, lhsImag, cir::BinOpKind::Mul, rhsReal, fmf);
  mlir::Value resultReal = builder.createBinop(
      loc, resultRealLhs, cir::BinOpKind::Sub, resultRealRhs, fmf);
  mlir::Value resultImag = builder.createBinop(
      loc, resultImagLhs, cir::BinOpKind::Add, resultImagRhs, fmf);
  mlir::Value algebraicResult =
      builder.createComplexCreate(loc, resultReal, resultImag);

  cir::ComplexType complexTy = op.getType();
  cir::ComplexRangeKind rangeKind = op.getRange();
  if (mlir::isa<cir::IntType>(complexTy.getElementType()) ||
      rangeKind == cir::ComplexRangeKind::Basic ||
      rangeKind == cir::ComplexRangeKind::Improved ||
      rangeKind == cir::ComplexRangeKind::Promoted)
    return algebraicResult;

  mlir::Type elementTy = complexTy.getElementType();
  llvm::Align elementAlign =
      pass.datalayout ? pass.datalayout->getABITypeAlign(elementTy)
                      : llvm::Align(1);
  mlir::IntegerAttr elementAlignAttr = builder.getAlignmentAttr(elementAlign);

  // Match OGCG's scalar control flow instead of returning an aggregate-valued
  // fallback result. This gives later passes scalar values to promote/phi.
  mlir::Value resultRealAddr =
      builder.createAlloca(loc, builder.getPointerTo(elementTy), elementTy,
                           "__complex_mul_real", elementAlignAttr);
  mlir::Value resultImagAddr =
      builder.createAlloca(loc, builder.getPointerTo(elementTy), elementTy,
                           "__complex_mul_imag", elementAlignAttr);
  builder.createStore(loc, resultReal, resultRealAddr, /*isVolatile=*/false,
                      elementAlignAttr);
  builder.createStore(loc, resultImag, resultImagAddr, /*isVolatile=*/false,
                      elementAlignAttr);

  // Check the real part first and only inspect the imaginary part on that cold
  // path, mirroring OGCG's nested NaN guards.
  mlir::Value resultRealIsNaN = builder.createIsNaN(loc, resultReal);
  auto unlikelyBranchWeights = builder.getArrayAttr(
      {builder.getI32IntegerAttr(1), builder.getI32IntegerAttr(1048575)});

  auto outerIf = cir::IfOp::create(
      builder, loc, resultRealIsNaN, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &, mlir::Location) {
        mlir::Value resultImagIsNaN = builder.createIsNaN(loc, resultImag);
        auto innerIf = cir::IfOp::create(
            builder, loc, resultImagIsNaN, /*withElseRegion=*/false,
            [&](mlir::OpBuilder &, mlir::Location) {
              mlir::Value libCallResult = buildComplexBinOpLibCall(
                  pass, builder, &getComplexMulLibCallName, loc, complexTy,
                  lhsReal, lhsImag, rhsReal, rhsImag, parentFunc);
              mlir::Value libCallReal =
                  builder.createComplexReal(loc, libCallResult);
              mlir::Value libCallImag =
                  builder.createComplexImag(loc, libCallResult);
              builder.createStore(loc, libCallReal, resultRealAddr,
                                  /*isVolatile=*/false, elementAlignAttr);
              builder.createStore(loc, libCallImag, resultImagAddr,
                                  /*isVolatile=*/false, elementAlignAttr);
              builder.createYield(loc);
            });
        innerIf->setAttr("cir.branch_weights", unlikelyBranchWeights);
        builder.createYield(loc);
      });
  outerIf->setAttr("cir.branch_weights", unlikelyBranchWeights);

  mlir::Value finalReal =
      builder.createLoad(loc, resultRealAddr, /*isVolatile=*/false,
                         elementAlign.value());
  mlir::Value finalImag =
      builder.createLoad(loc, resultImagAddr, /*isVolatile=*/false,
                         elementAlign.value());
  return builder.createComplexCreate(loc, finalReal, finalImag);
}

void LoweringPreparePass::lowerComplexMulOp(cir::ComplexMulOp op) {
  cir::CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op);
  mlir::Location loc = op.getLoc();
  mlir::TypedValue<cir::ComplexType> lhs = op.getLhs();
  mlir::TypedValue<cir::ComplexType> rhs = op.getRhs();
  mlir::Value lhsReal = builder.createComplexReal(loc, lhs);
  mlir::Value lhsImag = builder.createComplexImag(loc, lhs);
  mlir::Value rhsReal = builder.createComplexReal(loc, rhs);
  mlir::Value rhsImag = builder.createComplexImag(loc, rhs);
  auto parentFunc = op->getParentOfType<cir::FuncOp>();
  mlir::Value loweredResult = lowerComplexMul(*this, builder, loc, op, lhsReal,
                                              lhsImag, rhsReal, rhsImag,
                                              parentFunc);
  op.replaceAllUsesWith(loweredResult);
  op.erase();
}

void LoweringPreparePass::lowerUnaryOp(cir::UnaryOp op) {
  mlir::Type ty = op.getType();
  if (!mlir::isa<cir::ComplexType>(ty))
    return;

  mlir::Location loc = op.getLoc();
  cir::UnaryOpKind opKind = op.getKind();

  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op);

  mlir::Value operand = op.getInput();
  mlir::Value operandReal = builder.createComplexReal(loc, operand);
  mlir::Value operandImag = builder.createComplexImag(loc, operand);

  mlir::Value resultReal;
  mlir::Value resultImag;

  switch (opKind) {
  case cir::UnaryOpKind::Inc:
  case cir::UnaryOpKind::Dec:
    resultReal = builder.createUnaryOp(loc, opKind, operandReal);
    resultImag = operandImag;
    break;

  case cir::UnaryOpKind::Plus:
  case cir::UnaryOpKind::Minus:
    resultReal = builder.createUnaryOp(loc, opKind, operandReal);
    resultImag = builder.createUnaryOp(loc, opKind, operandImag);
    break;

  case cir::UnaryOpKind::Not:
    resultReal = operandReal;
    resultImag =
        builder.createUnaryOp(loc, cir::UnaryOpKind::Minus, operandImag);
    break;
  }

  mlir::Value result = builder.createComplexCreate(loc, resultReal, resultImag);
  op.replaceAllUsesWith(result);
  op.erase();
}

cir::FuncOp LoweringPreparePass::getOrCreateDtorFunc(CIRBaseBuilderTy &builder,
                                                     cir::GlobalOp op,
                                                     mlir::Region &dtorRegion,
                                                     cir::CallOp &dtorCall,
                                                     bool &createdHelper) {
  mlir::OpBuilder::InsertionGuard guard(builder);
  assert(!cir::MissingFeatures::astVarDeclInterface());
  createdHelper = false;

  cir::VoidType voidTy = builder.getVoidTy();
  auto voidPtrTy = cir::PointerType::get(voidTy);

  // Look for operations in dtorBlock
  mlir::Block &dtorBlock = dtorRegion.front();

  // Find the GetGlobalOp in the dtor block. It's usually the first op, but
  // LexicalScope cleanup or other CIRGen patterns may insert ops before it.
  cir::GetGlobalOp ggop;
  for (auto &dtorOp : dtorBlock.getOperations()) {
    if (auto gg = mlir::dyn_cast<cir::GetGlobalOp>(dtorOp)) {
      ggop = gg;
      break;
    }
  }
  if (!ggop) {
    op.emitError("no GetGlobalOp found in dtor region");
    encounteredLoweringError = true;
    return {};
  }

  // The simple case is just a call to a destructor, like this:
  //
  //   %0 = cir.get_global %globalS : !cir.ptr<!rec_S>
  //   cir.call %_ZN1SD1Ev(%0) : (!cir.ptr<!rec_S>) -> ()
  //   (implicit cir.yield)
  //
  // That is, if the second operation is a call that takes the get_global result
  // as its only operand, and the only other operation is a yield, then we can
  // just return the called function.
  //
  // We also handle the variant where a bitcast is needed between the get_global
  // and the call (e.g., when the global's storage type differs from the C++
  // record type due to vtable layout):
  //
  //   %0 = cir.get_global @x : !cir.ptr<!rec_anon_struct>
  //   %1 = cir.cast bitcast %0 : ... -> !cir.ptr<!rec_B>
  //   cir.call %_ZN1BD1Ev(%1) : (!cir.ptr<!rec_B>) -> ()
  //   (implicit cir.yield)
  //
  // In both cases, OGCG passes the destructor directly to __cxa_atexit
  // without a wrapper, since LLVM's opaque pointers make the bitcast a no-op.
  size_t numOps = dtorBlock.getOperations().size();
  if (numOps == 3 || numOps == 4) {
    auto nextIt = ggop->getIterator();
    // Check for optional bitcast between get_global and call.
    cir::CastOp castOp;
    if (numOps == 4) {
      castOp = mlir::dyn_cast<cir::CastOp>(&*(++nextIt));
      if (!castOp || castOp.getKind() != cir::CastKind::bitcast ||
          castOp.getSrc() != ggop.getResult())
        castOp = nullptr;
    }
    if (numOps == 3 || castOp) {
      auto callOp = mlir::dyn_cast<cir::CallOp>(&*(++nextIt));
      auto yieldOp = mlir::dyn_cast<cir::YieldOp>(&*(++nextIt));
      mlir::Value expectedArg = castOp ? castOp.getResult() : ggop.getResult();
      if (yieldOp && callOp && callOp.getNumOperands() == 1 &&
          callOp.getArgOperand(0) == expectedArg) {
        dtorCall = callOp;
        return getCalledFunction(callOp, symTable);
      }
    }
  }

  // Otherwise, we need to create a helper function to replace the dtor region.
  // This name is kind of arbitrary, but it matches the name that classic
  // codegen uses, based on the expected case that gets us here.
  createdHelper = true;
  builder.setInsertionPointAfter(op);
  std::string fnName =
      makeUniqueHelperName(existingSymbols, "__cxx_global_array_dtor");

  // Create the helper function.
  auto fnType = cir::FuncType::get({voidPtrTy}, voidTy);
  cir::FuncOp dtorFunc =
      buildRuntimeFunction(builder, fnName, op.getLoc(), fnType,
                           cir::GlobalLinkageKind::InternalLinkage);

  // Apply default function attributes (noinline, nounwind, target-features,
  // etc.) to match OGCG's CreateGlobalInitOrCleanUpFunction behavior.
  applyDefaultInternalFunctionAttrs(mlirModule, dtorFunc);

  // Set the section to .text.startup to match OGCG, which sets this on all
  // functions created through CreateGlobalInitOrCleanUpFunction.
  if (const char *section =
          astCtx->getTargetInfo().getStaticInitSectionSpecifier())
    dtorFunc.setSectionAttr(builder.getStringAttr(section));

  mlir::Block *entryBB = dtorFunc.addEntryBlock();

  // Move everything from the dtor region into the helper function.
  entryBB->getOperations().splice(entryBB->begin(), dtorBlock.getOperations(),
                                  dtorBlock.begin(), dtorBlock.end());

  // Before erasing this, clone the GetGlobalOp back into the dtor region.
  // After the splice, ggop has been moved into entryBB. Find it there.
  cir::GetGlobalOp dtorGGop;
  for (auto &op : entryBB->getOperations()) {
    if (auto gg = mlir::dyn_cast<cir::GetGlobalOp>(op)) {
      dtorGGop = gg;
      break;
    }
  }
  if (!dtorGGop) {
    op.emitError("GetGlobalOp not found in spliced helper function");
    encounteredLoweringError = true;
    return {};
  }
  builder.setInsertionPointToStart(&dtorBlock);
  builder.clone(*dtorGGop.getOperation());

  // The helper function references the global directly (matching OGCG's
  // generateDestroyHelper), so the void* parameter is unused. OGCG passes
  // null to __cxa_atexit as the second argument in this case.

  // Replace the yield in the final block with a return
  mlir::Block &finalBlock = dtorFunc.getBody().back();
  auto yieldOp = cast<cir::YieldOp>(finalBlock.getTerminator());
  builder.setInsertionPoint(yieldOp);
  cir::ReturnOp::create(builder, yieldOp->getLoc());
  yieldOp->erase();

  // Create a call to the helper function in the dtor region. Pass the
  // get_global result as the argument for type consistency (it will be
  // replaced with null when lowered to __cxa_atexit).
  cir::GetGlobalOp origGGop;
  for (auto &op : dtorBlock.getOperations()) {
    if (auto gg = mlir::dyn_cast<cir::GetGlobalOp>(op)) {
      origGGop = gg;
      break;
    }
  }
  if (!origGGop) {
    op.emitError("GetGlobalOp not found in dtor block after clone");
    encounteredLoweringError = true;
    return {};
  }
  builder.setInsertionPointAfter(origGGop);
  mlir::Value ggopResult = origGGop.getResult();
  dtorCall = builder.createCallOp(op.getLoc(), dtorFunc, ggopResult);

  // Add a yield after the call.
  auto finalYield = cir::YieldOp::create(builder, op.getLoc());

  // Erase everything after the yield.
  dtorBlock.getOperations().erase(std::next(mlir::Block::iterator(finalYield)),
                                  dtorBlock.end());
  dtorRegion.getBlocks().erase(std::next(dtorRegion.begin()), dtorRegion.end());

  return dtorFunc;
}

cir::FuncOp
LoweringPreparePass::buildCXXGlobalVarDeclInitFunc(cir::GlobalOp op) {
  if (encounteredLoweringError)
    return {};

  // TODO(cir): Store this in the GlobalOp.
  // This should come from the MangleContext, but for now I'm hardcoding it.
  std::string fnName =
      makeUniqueHelperName(existingSymbols, "__cxx_global_var_init");

  // Create a variable initialization function.
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op);
  cir::VoidType voidTy = builder.getVoidTy();
  auto fnType = cir::FuncType::get({}, voidTy);
  FuncOp f = buildRuntimeFunction(builder, fnName, op.getLoc(), fnType,
                                  cir::GlobalLinkageKind::InternalLinkage);

  // Set the section to .text.startup (or target-specific equivalent) to group
  // initialization code together for better startup performance.
  if (const char *section =
          astCtx->getTargetInfo().getStaticInitSectionSpecifier())
    f.setSectionAttr(builder.getStringAttr(section));

  // OGCG's CreateGlobalInitOrCleanUpFunction calls SetInternalFunctionAttributes
  // which applies the full set of default function attributes (nounwind,
  // sspstrong, target-features, etc.) to compiler-generated init functions.
  // CIRGen stores these defaults as a module attribute since LoweringPrepare
  // doesn't have access to CodeGenOptions.
  applyDefaultInternalFunctionAttrs(mlirModule, f);

  // For template/vague-linkage globals, the init function shares the
  // variable's COMDAT group (on ELF/Wasm). The comdat_name attribute is set
  // during CIRGen on the GlobalOp to indicate this.
  if (auto comdatNameAttr = op->getAttrOfType<mlir::StringAttr>("comdat_name")) {
    f.setComdat(true);
    f->setAttr("comdat_name", comdatNameAttr);
  }

  // Move over the initialzation code of the ctor region.
  mlir::Block *entryBB = f.addEntryBlock();
  if (!op.getCtorRegion().empty()) {
    mlir::Block &block = op.getCtorRegion().front();
    entryBB->getOperations().splice(entryBB->begin(), block.getOperations(),
                                    block.begin(), std::prev(block.end()));
  }

  // Register the destructor call with __cxa_atexit (or atexit if
  // -fno-use-cxa-atexit was passed).
  mlir::Region &dtorRegion = op.getDtorRegion();
  if (!dtorRegion.empty()) {
    assert(!cir::MissingFeatures::astVarDeclInterface());

    // If this is a simple call to a destructor, get the called function.
    // Otherwise, create a helper function for the entire dtor region,
    // replacing the current dtor region body with a call to the helper
    // function.
    cir::CallOp dtorCall;
    bool createdHelper = false;
    cir::FuncOp dtorFunc =
        getOrCreateDtorFunc(builder, op, dtorRegion, dtorCall, createdHelper);
    if (!dtorFunc || !dtorCall || encounteredLoweringError)
      return {};

    bool useCXAAtExit = !mlirModule->hasAttr("cir.no_cxa_atexit");
    // Thread-local variables always use __cxa_thread_atexit regardless.
    bool isTLS = op.getTlsModel().has_value();

    if (useCXAAtExit || isTLS) {
      // __cxa_atexit path (default):
      //   extern "C" int __cxa_atexit(void (*f)(void *), void *p, void *d);
      // Create a variable that binds the atexit to this shared object.
      builder.setInsertionPointToStart(&mlirModule.getBodyRegion().front());
      cir::GlobalOp handle = buildRuntimeVariable(
          builder, "__dso_handle", op.getLoc(), builder.getI8Type(),
          cir::GlobalLinkageKind::ExternalLinkage,
          cir::VisibilityKind::Hidden);

      auto voidPtrTy = cir::PointerType::get(voidTy);
      auto voidFnTy = cir::FuncType::get({voidPtrTy}, voidTy);
      auto voidFnPtrTy = cir::PointerType::get(voidFnTy);
      auto handlePtrTy = cir::PointerType::get(handle.getSymType());
      auto fnAtExitType =
          cir::FuncType::get({voidFnPtrTy, voidPtrTy, handlePtrTy},
                             cir::IntType::get(builder.getContext(), 32, true));
      const char *nameAtExit = "__cxa_atexit";
      if (isTLS) {
        // Darwin uses _tlv_atexit for TLS destructor registration, while
        // other platforms use __cxa_thread_atexit. Matches OGCG's
        // registerGlobalDtorWithTLSAtExit (CGDeclCXX.cpp).
        nameAtExit = astCtx->getTargetInfo().getTriple().isOSDarwin()
                         ? "_tlv_atexit"
                         : "__cxa_thread_atexit";
      }
      cir::FuncOp fnAtExit =
          buildRuntimeFunction(builder, nameAtExit, op.getLoc(), fnAtExitType);
      // __cxa_atexit does not throw. Matches OGCG's
      // fn->setDoesNotThrow() in ItaniumCXXABI.cpp:2955.
      {
        mlir::NamedAttrList attrs;
        if (auto existingAttrs = fnAtExit.getExtraAttrs())
          attrs =
              mlir::NamedAttrList{existingAttrs->getElements().getValue()};
        attrs.set("nothrow", mlir::UnitAttr::get(builder.getContext()));
        fnAtExit.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
            builder.getContext(),
            mlir::DictionaryAttr::get(builder.getContext(), attrs)));
      }

      // Replace the dtor call with:
      //   __cxa_atexit(&dtor, &var, &__dso_handle)
      builder.setInsertionPointAfter(dtorCall);
      mlir::Value args[3];
      auto dtorPtrTy = cir::PointerType::get(dtorFunc.getFunctionType());
      args[0] = cir::GetGlobalOp::create(builder, dtorCall.getLoc(), dtorPtrTy,
                                         dtorFunc.getSymName());
      args[0] = cir::CastOp::create(builder, dtorCall.getLoc(), voidFnPtrTy,
                                    cir::CastKind::bitcast, args[0]);
      if (createdHelper) {
        args[1] = builder.getNullPtr(voidPtrTy, dtorCall.getLoc());
      } else {
        mlir::Value dtorArg = dtorCall.getArgOperand(0);
        if (isTLS)
          dtorArg = getTLSRegistrationAddress(builder, dtorArg);
        args[1] = cir::CastOp::create(builder, dtorCall.getLoc(), voidPtrTy,
                                      cir::CastKind::bitcast, dtorArg);
      }
      args[2] = cir::GetGlobalOp::create(builder, handle.getLoc(), handlePtrTy,
                                         handle.getSymName());
      auto atExitCall = builder.createCallOp(dtorCall.getLoc(), fnAtExit, args);
      // __cxa_atexit does not throw — propagate nothrow to the call site
      // so it gets nounwind in LLVM IR, matching OGCG.
      atExitCall.setNothrow(true);
      if (auto ptrauthAttr = getAtExitCallbackPtrauthAttr(mlirModule))
        atExitCall->setAttr("cir.atexit_callback_ptrauth", ptrauthAttr);
    } else {
      // atexit() path (-fno-use-cxa-atexit):
      // Create a __dtor_<varname> wrapper function that calls the destructor
      // with the object address, then register it with atexit().
      // Matches OGCG's registerGlobalDtorWithAtExit / createAtExitStub.
      std::string stubName = ("__dtor_" + op.getSymName()).str();
      auto noArgVoidFnTy = cir::FuncType::get({}, voidTy);
      cir::FuncOp stub = buildRuntimeFunction(
          builder, stubName, op.getLoc(), noArgVoidFnTy,
          cir::GlobalLinkageKind::InternalLinkage);

      if (const char *section =
              astCtx->getTargetInfo().getStaticInitSectionSpecifier())
        stub.setSectionAttr(builder.getStringAttr(section));
      applyDefaultInternalFunctionAttrs(mlirModule, stub);

      // Build stub body: call dtor(obj)
      mlir::Block *stubBlock = stub.addEntryBlock();
      builder.setInsertionPointToStart(stubBlock);
      auto objPtrTy = cir::PointerType::get(op.getSymType());
      mlir::Value objAddr = cir::GetGlobalOp::create(
          builder, op.getLoc(), objPtrTy, op.getSymName());
      builder.createCallOp(op.getLoc(), dtorFunc, {objAddr});
      cir::ReturnOp::create(builder, op.getLoc());

      // Register with atexit():
      //   extern "C" int atexit(void (*f)(void));
      // Set insertion point to module level for the atexit declaration.
      builder.setInsertionPointToEnd(&mlirModule.getBodyRegion().back());
      auto noArgVoidFnPtrTy = cir::PointerType::get(noArgVoidFnTy);
      auto atexitType = cir::FuncType::get(
          {noArgVoidFnPtrTy},
          cir::IntType::get(builder.getContext(), 32, true));
      cir::FuncOp fnAtExit =
          buildRuntimeFunction(builder, "atexit", op.getLoc(), atexitType);
      {
        mlir::NamedAttrList attrs;
        if (auto existingAttrs = fnAtExit.getExtraAttrs())
          attrs =
              mlir::NamedAttrList{existingAttrs->getElements().getValue()};
        attrs.set("nothrow", mlir::UnitAttr::get(builder.getContext()));
        fnAtExit.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
            builder.getContext(),
            mlir::DictionaryAttr::get(builder.getContext(), attrs)));
      }

      // Insert the atexit call into the init function's entry block, not
      // the dtor region (dtorCall is in the dtor region).
      builder.setInsertionPointToEnd(entryBB);
      auto stubPtrTy = cir::PointerType::get(stub.getFunctionType());
      mlir::Value stubRef = cir::GetGlobalOp::create(
          builder, dtorCall.getLoc(), stubPtrTy, stub.getSymName());
      stubRef = cir::CastOp::create(builder, dtorCall.getLoc(),
                                    noArgVoidFnPtrTy,
                                    cir::CastKind::bitcast, stubRef);
      auto atExitCall =
          builder.createCallOp(dtorCall.getLoc(), fnAtExit, {stubRef});
      atExitCall.setNothrow(true);
      if (auto ptrauthAttr = getAtExitCallbackPtrauthAttr(mlirModule))
        atExitCall->setAttr("cir.atexit_callback_ptrauth", ptrauthAttr);
    }

    dtorCall->erase();
    // In the __cxa_atexit path, splice dtor region operations into the init
    // function (they contain the __cxa_atexit call). In the atexit path, the
    // dtor is handled by the stub function, so only splice if we used
    // __cxa_atexit.
    if (useCXAAtExit || isTLS) {
      mlir::Block &dtorBlock = dtorRegion.front();
      entryBB->getOperations().splice(entryBB->end(),
                                      dtorBlock.getOperations(),
                                      dtorBlock.begin(),
                                      std::prev(dtorBlock.end()));
    }
  }

  // Replace cir.yield with cir.return
  builder.setInsertionPointToEnd(entryBB);
  mlir::Operation *yieldOp = nullptr;
  if (!op.getCtorRegion().empty()) {
    mlir::Block &block = op.getCtorRegion().front();
    yieldOp = &block.getOperations().back();
  } else {
    assert(!dtorRegion.empty());
    mlir::Block &block = dtorRegion.front();
    yieldOp = &block.getOperations().back();
  }

  assert(isa<cir::YieldOp>(*yieldOp));
  cir::ReturnOp::create(builder, yieldOp->getLoc());
  return f;
}

void LoweringPreparePass::lowerGlobalOp(GlobalOp op) {
  if (encounteredLoweringError)
    return;

  mlir::Region &ctorRegion = op.getCtorRegion();
  mlir::Region &dtorRegion = op.getDtorRegion();

  if (!ctorRegion.empty() || !dtorRegion.empty()) {
    // Build a variable initialization function and move the initialzation code
    // in the ctor region over.
    cir::FuncOp f = buildCXXGlobalVarDeclInitFunc(op);
    if (!f || encounteredLoweringError)
      return;

    // Clear the ctor and dtor region
    ctorRegion.getBlocks().clear();
    dtorRegion.getBlocks().clear();

    // If the variable has init_priority, set it on the init function.
    if (auto initPriority = op.getInitPriority())
      f.setGlobalCtorPriority(*initPriority);

    // TLS variables must be lazily initialized via __tls_init, not eagerly
    // via _GLOBAL__sub_I_*.
    if (op.getTlsModelAttr()) {
      tlsDynamicInitVarNames.insert(op.getSymName());

      // Distinguish ordered vs unordered TLS initializers (matching OGCG):
      // - Unordered (template instantiation): have the
      //   cir.tls_template_instantiation attribute. Their _ZTH aliases point
      //   to the individual init function, not __tls_init.
      // - Ordered (everything else, including inline variables with comdat):
      //   go into __tls_init, _ZTH aliases point to __tls_init.
      if (op->hasAttr("cir.tls_template_instantiation")) {
        // Unordered: track init function for direct _ZTH aliasing.
        tlsUnorderedInitMap[op.getSymName()] = f;
      } else {
        // Ordered: add to __tls_init.
        tlsDynamicInitializers.push_back(f);
      }

      // Use the TLS model from the first TLS variable for the __tls_guard
      // variable so it matches -ftls-model.
      if (!tlsGuardModelSet) {
        tlsGuardModel = *op.getTlsModel();
        tlsGuardModelSet = true;
      }
    }
    else {
      // Check if this variable has a comdat key, meaning it's a
      // template/inline/vague-linkage variable that needs its own individual
      // entry in @llvm.global_ctors (with the variable as associated data)
      // rather than being bundled into _GLOBAL__sub_I_.
      auto comdatNameAttr =
          op->getAttrOfType<mlir::StringAttr>("comdat_name");
      if (comdatNameAttr) {
        // Route to individual global_ctors entry with comdat key.
        // The init function already has correct comdat set by
        // buildCXXGlobalVarDeclInitFunc.
        // Track the lex order so we can sort later to match OGCG's
        // dependency-respecting init order.
        unsigned lexOrder = UINT_MAX;
        if (auto initOrderAttr =
                op->getAttrOfType<mlir::IntegerAttr>("cir.global_init_order"))
          lexOrder = initOrderAttr.getInt();
        globalCtorList.emplace_back(
            f.getSymName().str(),
            cir::GlobalCtorAttr::getDefaultPriority(),
            comdatNameAttr.getValue().str());
        globalCtorLexOrder.push_back(lexOrder);
      } else if (op->hasAttr("cir.unordered_init")) {
        // Unordered init without comdat (e.g. anonymous namespace template
        // instantiation with internal linkage). Create a separate
        // global_ctors entry with ptr null key.
        unsigned lexOrder = UINT_MAX;
        if (auto initOrderAttr =
                op->getAttrOfType<mlir::IntegerAttr>("cir.global_init_order"))
          lexOrder = initOrderAttr.getInt();
        globalCtorList.emplace_back(
            f.getSymName().str(),
            cir::GlobalCtorAttr::getDefaultPriority(),
            std::string());
        globalCtorLexOrder.push_back(lexOrder);
      } else {
        // Non-comdat init: bundle into _GLOBAL__sub_I_.
        // Propagate the init order from the GlobalOp to the init FuncOp so
        // buildCXXGlobalInitFunc can sort by declaration order.
        if (auto initOrderAttr =
                op->getAttrOfType<mlir::IntegerAttr>("cir.global_init_order"))
          f->setAttr("cir.global_init_order", initOrderAttr);
        dynamicInitializers.push_back(f);
      }
    }
  }

  if (auto annotations = op.getAnnotationsAttr())
    addGlobalAnnotations(op, annotations);
}

template <typename AttributeTy>
static llvm::SmallVector<mlir::Attribute>
prepareCtorDtorAttrList(
    mlir::MLIRContext *context,
    llvm::ArrayRef<std::tuple<std::string, uint32_t, std::string>> list) {
  llvm::SmallVector<mlir::Attribute> attrs;
  for (const auto &[name, priority, assocData] : list) {
    mlir::StringAttr assocAttr;
    if (!assocData.empty())
      assocAttr = mlir::StringAttr::get(context, assocData);
    attrs.push_back(AttributeTy::get(context, name, priority, assocAttr));
  }
  return attrs;
}

void LoweringPreparePass::buildGlobalCtorDtorList() {
  if (!globalCtorList.empty()) {
    // Sort comdat ctor entries by lexical order to match OGCG's
    // dependency-respecting initialization order. OGCG uses
    // DelayedCXXInitPosition to track the order variables are first
    // referenced, then stable-sorts GlobalCtors by LexOrder. This ensures
    // e.g. Fib<2> is initialized before Fib<5> (which depends on it).
    if (globalCtorLexOrder.size() == globalCtorList.size()) {
      // Build index array and sort by lex order (stable to preserve
      // insertion order for entries with the same lex order).
      llvm::SmallVector<unsigned> indices(globalCtorList.size());
      std::iota(indices.begin(), indices.end(), 0);
      llvm::stable_sort(indices, [&](unsigned a, unsigned b) {
        return globalCtorLexOrder[a] < globalCtorLexOrder[b];
      });
      decltype(globalCtorList) sorted;
      sorted.reserve(globalCtorList.size());
      for (unsigned i : indices)
        sorted.push_back(std::move(globalCtorList[i]));
      globalCtorList = std::move(sorted);
    }

    llvm::SmallVector<mlir::Attribute> globalCtors =
        prepareCtorDtorAttrList<cir::GlobalCtorAttr>(&getContext(),
                                                     globalCtorList);

    mlirModule->setAttr(cir::CIRDialect::getGlobalCtorsAttrName(),
                        mlir::ArrayAttr::get(&getContext(), globalCtors));
  }

  if (!globalDtorList.empty()) {
    llvm::SmallVector<mlir::Attribute> globalDtors =
        prepareCtorDtorAttrList<cir::GlobalDtorAttr>(&getContext(),
                                                     globalDtorList);
    mlirModule->setAttr(cir::CIRDialect::getGlobalDtorsAttrName(),
                        mlir::ArrayAttr::get(&getContext(), globalDtors));
  }
}

void LoweringPreparePass::buildRegisterGlobalDtorsWithAtExit() {
  if (dtorsUsingAtExit.empty())
    return;

  auto loc = mlirModule.getLoc();
  cir::CIRBaseBuilderTy builder(getContext());
  auto voidTy = builder.getVoidTy();
  auto i8Ty = builder.getI8Type();
  auto i8PtrTy = cir::PointerType::get(i8Ty);
  auto intTy = cir::IntType::get(builder.getContext(), 32, true);
  auto noArgVoidFnTy = cir::FuncType::get({}, voidTy);
  auto noArgVoidFnPtrTy = cir::PointerType::get(noArgVoidFnTy);
  bool useCXAAtExit = !mlirModule->hasAttr("cir.no_cxa_atexit");
  mlir::ArrayAttr atExitCallbackPtrauth =
      getAtExitCallbackPtrauthAttr(mlirModule);

  auto makeUniqueName = [&](llvm::StringRef base) {
    std::string candidate = base.str();
    if (!existingSymbols.contains(candidate)) {
      existingSymbols.insert(candidate);
      return candidate;
    }
    for (unsigned n = 1;; ++n) {
      std::string suffixed = (candidate + "." + std::to_string(n));
      if (!existingSymbols.contains(suffixed)) {
        existingSymbols.insert(suffixed);
        return suffixed;
      }
    }
  };

  // Build runtime declarations once.
  cir::FuncOp cxaAtExitFn;
  cir::FuncOp atExitFn;
  cir::GlobalOp dsoHandle;
  if (useCXAAtExit) {
    builder.setInsertionPointToStart(&mlirModule.getBodyRegion().front());
    dsoHandle = buildRuntimeVariable(
        builder, "__dso_handle", loc, i8Ty,
        cir::GlobalLinkageKind::ExternalLinkage, cir::VisibilityKind::Hidden);
    auto handlePtrTy = cir::PointerType::get(dsoHandle.getSymType());
    auto noArgPtrVoidFnTy = cir::FuncType::get({i8PtrTy}, voidTy);
    auto noArgPtrVoidFnPtrTy = cir::PointerType::get(noArgPtrVoidFnTy);
    auto cxaAtExitTy =
        cir::FuncType::get({noArgPtrVoidFnPtrTy, i8PtrTy, handlePtrTy}, intTy);
    cxaAtExitFn =
        buildRuntimeFunction(builder, "__cxa_atexit", loc, cxaAtExitTy);
    mlir::NamedAttrList attrs;
    if (auto existingAttrs = cxaAtExitFn.getExtraAttrs())
      attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
    attrs.set("nothrow", mlir::UnitAttr::get(builder.getContext()));
    cxaAtExitFn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        builder.getContext(),
        mlir::DictionaryAttr::get(builder.getContext(), attrs)));
  } else {
    builder.setInsertionPointToStart(&mlirModule.getBodyRegion().front());
    auto atexitTy = cir::FuncType::get({noArgVoidFnPtrTy}, intTy);
    atExitFn = buildRuntimeFunction(builder, "atexit", loc, atexitTy);
    mlir::NamedAttrList attrs;
    if (auto existingAttrs = atExitFn.getExtraAttrs())
      attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
    attrs.set("nothrow", mlir::UnitAttr::get(builder.getContext()));
    atExitFn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        builder.getContext(),
        mlir::DictionaryAttr::get(builder.getContext(), attrs)));
  }

  for (const auto &[priority, dtors] : dtorsUsingAtExit) {
    std::string initFnName =
        makeUniqueName("__GLOBAL_init_" + std::to_string(priority));
    builder.setInsertionPointToEnd(&mlirModule.getBodyRegion().back());
    cir::FuncOp initFn = buildRuntimeFunction(
        builder, initFnName, loc, noArgVoidFnTy,
        cir::GlobalLinkageKind::InternalLinkage);

    if (const char *section =
            astCtx->getTargetInfo().getStaticInitSectionSpecifier())
      initFn.setSectionAttr(builder.getStringAttr(section));
    applyDefaultInternalFunctionAttrs(mlirModule, initFn);

    mlir::Block *entryBB = initFn.addEntryBlock();
    builder.setInsertionPointToStart(entryBB);
    for (llvm::StringRef dtorName : dtors) {
      auto dtorFn = symTable ? symTable->lookup<cir::FuncOp>(dtorName) : nullptr;
      if (!dtorFn) {
        mlirModule.emitError()
            << "missing global dtor symbol while lowering "
               "registerGlobalDtorsWithAtExit: "
            << dtorName;
        encounteredLoweringError = true;
        return;
      }

      auto dtorPtrTy = cir::PointerType::get(dtorFn.getFunctionType());
      mlir::Value dtorRef =
          cir::GetGlobalOp::create(builder, loc, dtorPtrTy, dtorFn.getSymName());
      if (useCXAAtExit) {
        auto noArgPtrVoidFnTy = cir::FuncType::get({i8PtrTy}, voidTy);
        auto noArgPtrVoidFnPtrTy = cir::PointerType::get(noArgPtrVoidFnTy);
        mlir::Value dtorCast = cir::CastOp::create(
            builder, loc, noArgPtrVoidFnPtrTy, cir::CastKind::bitcast, dtorRef);
        mlir::Value nullPtr = builder.getNullPtr(i8PtrTy, loc);
        auto handlePtrTy = cir::PointerType::get(dsoHandle.getSymType());
        mlir::Value handle = cir::GetGlobalOp::create(
            builder, loc, handlePtrTy, dsoHandle.getSymName());
        auto atExitCall = builder.createCallOp(
            loc, cxaAtExitFn, mlir::ValueRange{dtorCast, nullPtr, handle});
        atExitCall.setNothrow(true);
        if (atExitCallbackPtrauth)
          atExitCall->setAttr("cir.atexit_callback_ptrauth",
                              atExitCallbackPtrauth);
      } else {
        mlir::Value dtorCast = cir::CastOp::create(
            builder, loc, noArgVoidFnPtrTy, cir::CastKind::bitcast, dtorRef);
        auto atExitCall = builder.createCallOp(loc, atExitFn, dtorCast);
        atExitCall.setNothrow(true);
        if (atExitCallbackPtrauth)
          atExitCall->setAttr("cir.atexit_callback_ptrauth",
                              atExitCallbackPtrauth);
      }
    }
    cir::ReturnOp::create(builder, loc);

    globalCtorList.emplace_back(initFnName, priority, std::string{});
    globalCtorLexOrder.push_back(UINT_MAX);
  }
}

void LoweringPreparePass::addGlobalAnnotations(mlir::Operation *op,
                                               mlir::ArrayAttr annotations) {
  auto globalValue = cast<mlir::SymbolOpInterface>(op);
  mlir::StringAttr globalValueName = globalValue.getNameAttr();
  for (auto &annot : annotations) {
    llvm::SmallVector<mlir::Attribute, 2> entryArray = {globalValueName, annot};
    globalAnnotations.push_back(
        mlir::ArrayAttr::get(mlirModule.getContext(), entryArray));
  }
}

void LoweringPreparePass::buildGlobalAnnotationValues() {
  if (globalAnnotations.empty())
    return;
  mlir::ArrayAttr annotationValueArray =
      mlir::ArrayAttr::get(mlirModule.getContext(), globalAnnotations);
  mlirModule->setAttr(
      cir::CIRDialect::getGlobalAnnotationsAttrName(),
      cir::GlobalAnnotationValuesAttr::get(annotationValueArray));
}

void LoweringPreparePass::buildCXXGlobalInitFunc() {
  // Collect imported module initializer names from the CIR module attribute.
  SmallVector<StringRef> importedModuleInits;
  if (auto attr = mlirModule->getAttrOfType<mlir::ArrayAttr>(
          "cir.module_init_imports")) {
    for (auto a : attr)
      importedModuleInits.push_back(mlir::cast<mlir::StringAttr>(a).getValue());
    mlirModule->removeAttr("cir.module_init_imports");
  }

  // Determine if this is a C++20 module interface/partition.
  bool isModuleInit = astCtx->getCurrentNamedModule() &&
                      !astCtx->getCurrentNamedModule()->isModuleImplementation();

  // For C++20 module interfaces/partitions, OGCG always creates the module
  // init function even if empty (CGDeclCXX.cpp:703). Only skip when this is
  // NOT a module init.
  if (dynamicInitializers.empty() && importedModuleInits.empty() &&
      !isModuleInit)
    return;

  // Separate initializers with custom priority from those without.
  // Initializers with init_priority are grouped by priority into wrapper
  // functions (_GLOBAL__I_NNNNNN), matching OGCG's behavior.
  llvm::SmallVector<cir::FuncOp> defaultPriorityInitializers;
  llvm::SmallVector<std::pair<unsigned, cir::FuncOp>> prioritizedInitializers;
  for (cir::FuncOp &f : dynamicInitializers) {
    if (auto priority = f.getGlobalCtorPriority()) {
      prioritizedInitializers.emplace_back(*priority, f);
    } else {
      defaultPriorityInitializers.push_back(f);
    }
  }

  // Sort prioritized initializers by priority value, then group same-priority
  // inits into _GLOBAL__I_NNNNNN wrapper functions. This matches OGCG's
  // EmitCXXGlobalInitFunc (CGDeclCXX.cpp:860-931).
  if (!prioritizedInitializers.empty()) {
    llvm::sort(prioritizedInitializers,
               [](const auto &a, const auto &b) { return a.first < b.first; });

    CIRBaseBuilderTy builder(getContext());
    builder.setInsertionPointToEnd(&mlirModule.getBodyRegion().back());
    auto fnType = cir::FuncType::get({}, builder.getVoidTy());

    for (size_t i = 0; i < prioritizedInitializers.size();) {
      unsigned priority = prioritizedInitializers[i].first;

      // Find the range of same-priority initializers.
      size_t j = i + 1;
      while (j < prioritizedInitializers.size() &&
             prioritizedInitializers[j].first == priority)
        ++j;

      // Create _GLOBAL__I_NNNNNN wrapper function for this priority group.
      std::string prioritySuffix = llvm::utostr(priority);
      prioritySuffix =
          std::string(6 - prioritySuffix.size(), '0') + prioritySuffix;
      std::string wrapperName = "_GLOBAL__I_" + prioritySuffix;

      cir::FuncOp wrapper =
          buildRuntimeFunction(builder, wrapperName, mlirModule.getLoc(),
                               fnType, cir::GlobalLinkageKind::InternalLinkage);

      applyDefaultInternalFunctionAttrs(mlirModule, wrapper);

      if (const char *section =
              astCtx->getTargetInfo().getStaticInitSectionSpecifier())
        wrapper.setSectionAttr(builder.getStringAttr(section));

      builder.setInsertionPointToStart(wrapper.addEntryBlock());
      for (size_t k = i; k < j; ++k)
        builder.createCallOp(prioritizedInitializers[k].second.getLoc(),
                             prioritizedInitializers[k].second, {});
      cir::ReturnOp::create(builder, wrapper.getLoc());

      globalCtorList.emplace_back(wrapperName, priority, std::string{});
      globalCtorLexOrder.push_back(UINT_MAX);
      builder.setInsertionPointToEnd(&mlirModule.getBodyRegion().back());
      i = j;
    }
  }

  // Sort default priority initializers by their declaration order
  // (cir.global_init_order attribute) to match OGCG's behavior. Deferred
  // variables may be emitted out of order, but they must be initialized in
  // TU declaration order. Initializers without the attribute are placed at
  // the end in their original order (stable sort).
  llvm::stable_sort(defaultPriorityInitializers,
                    [](const cir::FuncOp &a, const cir::FuncOp &b) {
                      auto aOrder = a->getAttrOfType<mlir::IntegerAttr>(
                          "cir.global_init_order");
                      auto bOrder = b->getAttrOfType<mlir::IntegerAttr>(
                          "cir.global_init_order");
                      // Both have order: sort by order.
                      if (aOrder && bOrder)
                        return aOrder.getInt() < bOrder.getInt();
                      // Only a has order: a comes first.
                      if (aOrder)
                        return true;
                      // Only b has order: b comes first.
                      if (bOrder)
                        return false;
                      // Neither has order: preserve original order.
                      return false;
                    });

  // If there are no default priority initializers and no imported module
  // inits, we're done (unless this is a module interface, which always gets
  // an init function even if empty).
  if (defaultPriorityInitializers.empty() && importedModuleInits.empty() &&
      !isModuleInit)
    return;

  SmallString<256> fnName;
  // Include the filename in the symbol name. Including "sub_" matches gcc
  // and makes sure these symbols appear lexicographically behind the symbols
  // with priority (TBD).  Module implementation units behave the same
  // way as a non-modular TU with imports.
  if (isModuleInit) {
    llvm::raw_svector_ostream out(fnName);
    std::unique_ptr<clang::MangleContext> mangleCtx(
        astCtx->createMangleContext());
    cast<clang::ItaniumMangleContext>(*mangleCtx)
        .mangleModuleInitializer(astCtx->getCurrentNamedModule(), out);
  } else {
    fnName += "_GLOBAL__sub_I_";
    fnName += getTransformedFileName(mlirModule);
  }

  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointToEnd(&mlirModule.getBodyRegion().back());
  auto fnType = cir::FuncType::get({}, builder.getVoidTy());

  // Module init functions must have external linkage so importers can call
  // them. Regular TU init functions use internal linkage.
  auto linkage = isModuleInit
                     ? cir::GlobalLinkageKind::ExternalLinkage
                     : cir::GlobalLinkageKind::InternalLinkage;
  cir::FuncOp f =
      buildRuntimeFunction(builder, fnName, mlirModule.getLoc(), fnType,
                           linkage);

  // Set the section to .text.startup (or target-specific equivalent).
  if (const char *section =
          astCtx->getTargetInfo().getStaticInitSectionSpecifier())
    f.setSectionAttr(builder.getStringAttr(section));

  // Apply default function attributes from CIRGen (nounwind, sspstrong,
  // target-features, etc.). Matches OGCG's SetInternalFunctionAttributes.
  applyDefaultInternalFunctionAttrs(mlirModule, f);

  // Declare imported module init functions at module level before building
  // the init function body (they must be module-level symbols, not nested
  // inside another function).
  SmallVector<cir::FuncOp> importedInitFns;
  if (!importedModuleInits.empty()) {
    mlir::OpBuilder::InsertionGuard moduleGuard(builder);
    builder.setInsertionPointToEnd(&mlirModule.getBodyRegion().back());
    for (StringRef importedInit : importedModuleInits) {
      cir::FuncOp importedFn = buildRuntimeFunction(
          builder, importedInit, mlirModule.getLoc(), fnType,
          cir::GlobalLinkageKind::ExternalLinkage);
      importedInitFns.push_back(importedFn);
    }
  }

  // Collect all init functions to call: first imported module inits, then
  // local inits.
  bool hasInits = !importedInitFns.empty() ||
                  !defaultPriorityInitializers.empty();

  if (isModuleInit && hasInits) {
    // For module init functions, add a guard variable to ensure the init body
    // runs only once (a module might be imported multiple times via nested use).
    auto i8Ty = cir::IntType::get(&getContext(), 8, /*isSigned=*/false);
    SmallString<256> guardName(fnName);
    guardName += "__in_chrg";
    builder.setInsertionPointToStart(&mlirModule.getBodyRegion().front());
    cir::GlobalOp guard = buildRuntimeVariable(
        builder, guardName, mlirModule.getLoc(), i8Ty,
        cir::GlobalLinkageKind::InternalLinkage);
    guard.setInitialValueAttr(cir::IntAttr::get(i8Ty, 0));
    guard.setAlignment(1);

    // Build function body with guard check pattern:
    //   entry:
    //     %guard = load @<fnName>__in_chrg
    //     %uninit = cmp eq %guard, 0
    //     brcond %uninit, ^init, ^exit
    //   ^init:
    //     store 1, @<fnName>__in_chrg
    //     call imported module inits...
    //     call local inits...
    //     br ^exit
    //   ^exit:
    //     return
    mlir::Block *entryBB = f.addEntryBlock();
    builder.setInsertionPointToStart(entryBB);
    mlir::Location loc = mlirModule.getLoc();

    auto guardPtrTy = cir::PointerType::get(i8Ty);
    mlir::Value guardAddr =
        cir::GetGlobalOp::create(builder, loc, guardPtrTy, guardName);
    mlir::Value guardVal =
        cir::LoadOp::create(builder, loc, i8Ty, guardAddr);
    mlir::Value zero = builder.getConstantInt(loc, i8Ty, 0);
    mlir::Value uninit =
        cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq, guardVal, zero);

    mlir::Block *initBB =
        builder.createBlock(&f.getBody(), f.getBody().end());
    mlir::Block *exitBB =
        builder.createBlock(&f.getBody(), f.getBody().end());

    builder.setInsertionPointToEnd(entryBB);
    auto brCond = cir::BrCondOp::create(builder, loc, uninit, initBB, exitBB);
    // Match OGCG's GenerateCXXGlobalInitFunc(..., GuardKind::TlsGuard):
    // module initializers may be entered repeatedly via imports, but the
    // guarded init path is still cold relative to the already-initialized
    // fast path, so preserve the 1:1023 branch weights on the guard branch.
    brCond->setAttr(
        "cir.branch_weights",
        builder.getArrayAttr({builder.getI32IntegerAttr(1),
                              builder.getI32IntegerAttr(1023)}));

    // ^init block: set guard, call all inits.
    builder.setInsertionPointToStart(initBB);
    mlir::Value one = builder.getConstantInt(loc, i8Ty, 1);
    builder.createStore(loc, one, guardAddr);

    // The guard variable can't ever change again. Emit invariant.start to
    // tell LLVM the guard is invariant post-initialization, matching OGCG's
    // EmitInvariantStart call in CGDeclCXX.cpp. OGCG skips this at -O0.
    if (mlirModule->hasAttr(cir::CIRDialect::getOptInfoAttrName())) {
      auto ptrTy = cir::PointerType::get(i8Ty);
      auto s64Ty =
          cir::IntType::get(builder.getContext(), 64, /*isSigned=*/true);
      mlir::Value guardSize = builder.getConstantInt(loc, s64Ty, 1);
      cir::LLVMIntrinsicCallOp::create(
          builder, loc,
          mlir::StringAttr::get(builder.getContext(), "invariant.start"),
          ptrTy, mlir::ValueRange{guardSize, guardAddr});
    }

    for (cir::FuncOp &importedFn : importedInitFns)
      builder.createCallOp(loc, importedFn, {});
    for (cir::FuncOp &init : defaultPriorityInitializers)
      builder.createCallOp(init.getLoc(), init, {});
    cir::BrOp::create(builder, loc, exitBB);

    // ^exit block: return.
    builder.setInsertionPointToStart(exitBB);
    cir::ReturnOp::create(builder, loc);
  } else {
    // Simple case: no guard needed. Just call all inits sequentially.
    builder.setInsertionPointToStart(f.addEntryBlock());
    for (cir::FuncOp &importedFn : importedInitFns)
      builder.createCallOp(mlirModule.getLoc(), importedFn, {});
    for (cir::FuncOp &init : defaultPriorityInitializers)
      builder.createCallOp(init.getLoc(), init, {});
    cir::ReturnOp::create(builder, f.getLoc());
  }

  // Add the global init function (not the individual ctor functions) to the
  // global ctor list.
  globalCtorList.emplace_back(fnName,
                              cir::GlobalCtorAttr::getDefaultPriority(),
                              std::string{});
  globalCtorLexOrder.push_back(UINT_MAX);
}

void LoweringPreparePass::buildCXXTLSInitFunc() {
  if (tlsDynamicInitializers.empty() && tlsUnorderedInitMap.empty())
    return;

  CIRBaseBuilderTy builder(getContext());

  // On Darwin, TLS init/wrapper functions use cxx_fast_tlscc.
  bool isDarwin = astCtx && astCtx->getTargetInfo().getTriple().isOSDarwin();

  // CIR uses its own integer type, not MLIR's builtin IntegerType.
  auto i8Ty = cir::IntType::get(&getContext(), 8, /*isSigned=*/false);

  // Only create __tls_guard and __tls_init if there are ordered
  // (non-template) TLS initializers.
  cir::FuncOp initFunc;
  if (!tlsDynamicInitializers.empty()) {

  // Build __tls_init body with guard check pattern:
  //   entry:
  //     %guard_addr = cir.get_global @__tls_guard
  //     %guard_val = cir.load %guard_addr
  //     %zero = cir.const 0
  //     %uninit = cir.cmp eq %guard_val, %zero
  //     cir.brcond %uninit, ^init, ^exit
  //   ^init:
  //     cir.store 1, %guard_addr
  //     cir.call @__cxx_global_var_init()   // for each ordered TLS init
  //     ...
  //     cir.br ^exit
  //   ^exit:
  //     cir.return

  // Create __tls_guard: internal thread_local i8, initialized to 0.
  builder.setInsertionPointToStart(&mlirModule.getBodyRegion().front());
  cir::GlobalOp guard = buildRuntimeVariable(
      builder, "__tls_guard", mlirModule.getLoc(), i8Ty,
      cir::GlobalLinkageKind::InternalLinkage);
  guard.setTlsModel(tlsGuardModel);
  guard.setInitialValueAttr(cir::IntAttr::get(i8Ty, 0));
  guard.setAlignment(1);

  // Create __tls_init: internal void function.
  builder.setInsertionPointToEnd(&mlirModule.getBodyRegion().back());
  auto fnType = cir::FuncType::get({}, builder.getVoidTy());
  initFunc =
      buildRuntimeFunction(builder, "__tls_init", mlirModule.getLoc(), fnType,
                           cir::GlobalLinkageKind::InternalLinkage);

  // On Darwin, use cxx_fast_tlscc to enable the TLS access fastpath.
  if (isDarwin)
    initFunc.setCallingConv(cir::CallingConv::CXXFastTLS);

  // Apply default function attributes.
  applyDefaultInternalFunctionAttrs(mlirModule, initFunc);

  mlir::Block *entryBB = initFunc.addEntryBlock();
  builder.setInsertionPointToStart(entryBB);

  mlir::Location loc = mlirModule.getLoc();

  // Load the guard.
  auto guardPtrTy = cir::PointerType::get(i8Ty);
  mlir::Value guardAddr =
      cir::GetGlobalOp::create(builder, loc, guardPtrTy, "__tls_guard");
  mlir::Value guardVal =
      cir::LoadOp::create(builder, loc, i8Ty, guardAddr);

  // Compare to zero.
  mlir::Value zero = builder.getConstantInt(loc, i8Ty, 0);
  mlir::Value uninit =
      cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq, guardVal, zero);

  // Create init and exit blocks.
  mlir::Block *initBB =
      builder.createBlock(&initFunc.getBody(), initFunc.getBody().end());
  mlir::Block *exitBB =
      builder.createBlock(&initFunc.getBody(), initFunc.getBody().end());

  // Branch: if uninitialized goto ^init, else goto ^exit.
  builder.setInsertionPointToEnd(entryBB);
  auto brCond = cir::BrCondOp::create(builder, loc, uninit, initBB, exitBB);
  // TLS init is entered ~once per 1024 thread starts. Set branch weights
  // to hint that the init path is unlikely, matching OGCG's
  // EmitCXXGuardedInitBranch with GuardKind::TlsGuard.
  brCond->setAttr(
      "cir.branch_weights",
      builder.getArrayAttr({builder.getI32IntegerAttr(1),
                            builder.getI32IntegerAttr(1023)}));

  // ^init block: set guard to 1, call only ordered TLS init functions.
  builder.setInsertionPointToStart(initBB);
  mlir::Value one = builder.getConstantInt(loc, i8Ty, 1);
  builder.createStore(loc, one, guardAddr);

  // The guard variable can't ever change again. Emit invariant.start to
  // tell LLVM the guard is invariant post-initialization, matching OGCG's
  // EmitInvariantStart call in CGDeclCXX.cpp after the TLS guard store.
  // OGCG skips this at -O0.
  if (mlirModule->hasAttr(cir::CIRDialect::getOptInfoAttrName())) {
    auto ptrTy = cir::PointerType::get(i8Ty);
    auto s64Ty = cir::IntType::get(builder.getContext(), 64, /*isSigned=*/true);
    mlir::Value guardSize = builder.getConstantInt(loc, s64Ty, 1);
    cir::LLVMIntrinsicCallOp::create(
        builder, loc,
        mlir::StringAttr::get(builder.getContext(), "invariant.start"),
        ptrTy, mlir::ValueRange{guardSize, guardAddr});
  }

  for (cir::FuncOp &init : tlsDynamicInitializers)
    builder.createCallOp(init.getLoc(), init, {});
  cir::BrOp::create(builder, loc, exitBB);

  // ^exit block: return.
  builder.setInsertionPointToStart(exitBB);
  cir::ReturnOp::create(builder, loc);
  } // end if (!tlsDynamicInitializers.empty())

  // Patch _ZTW* wrapper functions to call the init function before accessing
  // TLS. For ordered TLS variables, _ZTH aliases point to __tls_init.
  // For unordered (template/comdat) TLS variables, _ZTH aliases point directly
  // to the individual init function.
  // Trivially-initialized TLS variables (e.g., thread_local int x = 100) don't
  // need an init call — their wrappers just return the TLS address.
  mlirModule.walk([&](cir::FuncOp fn) {
    if (!fn.getSymName().starts_with("_ZTW"))
      return;
    if (fn.isDeclaration())
      return;

    // Find the GetGlobalOp in the wrapper to determine which variable it wraps.
    mlir::Block &entry = fn.getBody().front();
    cir::GetGlobalOp ggop;
    for (auto &op : entry.getOperations()) {
      if (auto gg = dyn_cast<cir::GetGlobalOp>(op)) {
        ggop = gg;
        break;
      }
    }
    if (!ggop)
      return;

    // Only add init call if this variable needs dynamic initialization.
    if (!tlsDynamicInitVarNames.contains(ggop.getName()))
      return;

    // Create _ZTH<var> alias: mangle the init function name from the wrapper
    // name by replacing _ZTW with _ZTH.
    SmallString<256> initAliasName("_ZTH");
    initAliasName += fn.getSymName().drop_front(4); // drop "_ZTW"

    // Look up the GlobalOp to get its linkage for the alias.
    cir::GlobalOp gv;
    if (symTable)
      gv = symTable->lookup<cir::GlobalOp>(ggop.getName());

    // Determine alias target: unordered inits alias to their individual init
    // function; ordered inits alias to __tls_init.
    auto it = tlsUnorderedInitMap.find(ggop.getName());
    bool isUnordered = (it != tlsUnorderedInitMap.end());
    StringRef aliasTarget =
        isUnordered ? it->second.getSymName() : StringRef("__tls_init");

    builder.setInsertionPointToStart(&mlirModule.getBodyRegion().front());
    auto initAliasFn = buildRuntimeFunction(
        builder, initAliasName, fn.getLoc(),
        cir::FuncType::get({}, builder.getVoidTy()),
        gv ? gv.getLinkage() : cir::GlobalLinkageKind::WeakODRLinkage);
    initAliasFn.setAliasee(aliasTarget);
    if (gv) {
      initAliasFn.setGlobalVisibility(gv.getGlobalVisibility());
      // Inherit dso_local from the variable, matching OGCG's
      // Init->setDSOLocal(Var->isDSOLocal()) in ItaniumCXXABI.cpp:3319.
      initAliasFn.setDsoLocal(gv.getDsoLocal());
    }
    if (isDarwin)
      initAliasFn.setCallingConv(cir::CallingConv::CXXFastTLS);

    // Insert the init call at the start of the wrapper.
    builder.setInsertionPointToStart(&entry);
    builder.createCallOp(fn.getLoc(), initAliasFn, {},
                         isDarwin ? cir::CallingConv::CXXFastTLS
                                 : cir::CallingConv::C);
  });
}

void LoweringPreparePass::lowerDynamicCastOp(DynamicCastOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op);

  assert(astCtx && "AST context is not available during lowering prepare");
  auto loweredValue = cxxABI->lowerDynamicCast(builder, *astCtx, op);

  op.replaceAllUsesWith(loweredValue);
  op.erase();
}

void LoweringPreparePass::lowerVAArgOp(VAArgOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPoint(op);

  auto res = cxxABI->lowerVAArg(builder, op, *datalayout);
  if (res) {
    op.replaceAllUsesWith(res);
    op.erase();
  }
}

static void lowerArrayDtorCtorIntoLoop(cir::CIRBaseBuilderTy &builder,
                                       clang::ASTContext *astCtx,
                                       mlir::Operation *op, mlir::Type eltTy,
                                       mlir::Value arrayAddr, uint64_t arrayLen,
                                       bool isCtor) {
  // Generate loop to call into ctor/dtor for every element.
  mlir::Location loc = op->getLoc();

  // Guard against zero-length arrays. CIRGen should not emit ArrayCtor/
  // ArrayDtor for zero-length arrays, but the do-while loop below would
  // execute at least once regardless, causing out-of-bounds access.
  if (arrayLen == 0) {
    op->erase();
    return;
  }

  // TODO: instead of getting the size from the AST context, create alias for
  // PtrDiffTy and unify with CIRGen stuff.
  const unsigned sizeTypeSize =
      astCtx->getTypeSize(astCtx->getSignedSizeType());
  // For construction, we iterate [begin, begin+arrayLen).
  // For destruction, we iterate backward from begin+arrayLen down to begin,
  // matching OGCG: start at past-the-end, decrement first, then destroy.
  mlir::Value endOffsetVal =
      builder.getUnsignedInt(loc, arrayLen, sizeTypeSize);

  auto begin = cir::CastOp::create(builder, loc, eltTy,
                                   cir::CastKind::array_to_ptrdecay, arrayAddr);
  mlir::Value end =
      cir::PtrStrideOp::create(builder, loc, eltTy, begin, endOffsetVal,
          cir::GEPNoWrapFlags::inbounds);
  mlir::Value start = isCtor ? begin : end;
  mlir::Value stop = isCtor ? end : begin;

  mlir::Value tmpAddr = builder.createAlloca(
      loc, /*addr type*/ builder.getPointerTo(eltTy),
      /*var type*/ eltTy, "__array_idx", builder.getAlignmentAttr(1));
  builder.createStore(loc, start, tmpAddr);

  cir::DoWhileOp loop = builder.createDoWhile(
      loc,
      /*condBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location loc) {
        auto currentElement = cir::LoadOp::create(b, loc, eltTy, tmpAddr);
        auto cmp = cir::CmpOp::create(builder, loc, cir::CmpOpKind::ne,
                                      currentElement, stop);
        builder.createCondition(cmp);
      },
      /*bodyBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location loc) {
        auto currentElement = cir::LoadOp::create(b, loc, eltTy, tmpAddr);

        // Array elements get constructed in order but destructed in reverse.
        mlir::Value stride;
        if (isCtor)
          stride = builder.getUnsignedInt(loc, 1, sizeTypeSize);
        else
          stride = builder.getSignedInt(loc, -1, sizeTypeSize);

        // For destruction, we decrement first (to go from past-the-end to
        // the last element), then call the destructor. This matches OGCG's
        // pattern and ensures all elements including index 0 are destroyed.
        // For construction, we call the constructor first, then advance.
        mlir::Value elementToProcess;
        mlir::Value nextElement;
        if (isCtor) {
          elementToProcess = currentElement;
          nextElement = cir::PtrStrideOp::create(builder, loc, eltTy,
                                                 currentElement, stride,
              cir::GEPNoWrapFlags::inbounds);
        } else {
          nextElement = cir::PtrStrideOp::create(builder, loc, eltTy,
                                                 currentElement, stride,
              cir::GEPNoWrapFlags::inbounds);
          elementToProcess = nextElement;
        }

        // Move all non-terminator ops from the original region into the loop
        // body. We must move all ops (not just the CallOp) because the call
        // may reference values defined by other ops in the region (e.g.
        // cir.const for default constructor arguments).
        mlir::Block &srcBlock = op->getRegion(0).front();
        srcBlock.getArgument(0).replaceAllUsesWith(elementToProcess);
        for (auto &bodyOp :
             llvm::make_early_inc_range(srcBlock.without_terminator())) {
          bodyOp.moveBefore(builder.getInsertionBlock(),
                            builder.getInsertionPoint());
        }

        // Store the element pointer to the temporary variable
        builder.createStore(loc, nextElement, tmpAddr);
        builder.createYield(loc);
      });

  op->replaceAllUsesWith(loop);
  op->erase();
}

void LoweringPreparePass::lowerArrayDtor(cir::ArrayDtor op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op.getOperation());

  mlir::Type eltTy = op->getRegion(0).getArgument(0).getType();
  // ArrayDtor always carries a pointer-to-cir.array<T x N> (static extent).
  // Dynamic/VLA destruction is lowered earlier through emitArrayDestroy's
  // pointer-cursor loop path and never reaches this op.
  auto arrayLen =
      mlir::cast<cir::ArrayType>(op.getAddr().getType().getPointee()).getSize();
  lowerArrayDtorCtorIntoLoop(builder, astCtx, op, eltTy, op.getAddr(), arrayLen,
                             false);
}

void LoweringPreparePass::lowerArrayCtor(cir::ArrayCtor op) {
  cir::CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op.getOperation());

  mlir::Type eltTy = op->getRegion(0).getArgument(0).getType();
  // ArrayCtor always carries a pointer-to-cir.array<T x N> (static extent).
  // Dynamic/VLA construction is emitted directly as a pointer-cursor loop in
  // CIRGenClass and does not use ArrayCtor.
  auto arrayLen =
      mlir::cast<cir::ArrayType>(op.getAddr().getType().getPointee()).getSize();
  lowerArrayDtorCtorIntoLoop(builder, astCtx, op, eltTy, op.getAddr(), arrayLen,
                             true);
}

void LoweringPreparePass::lowerTrivialCopyCall(cir::CallOp op) {
  cir::FuncOp funcOp = getCalledFunction(op, symTable);
  if (!funcOp)
    return;

  std::optional<cir::CtorKind> ctorKind = funcOp.getCxxConstructorKind();
  if (ctorKind && *ctorKind == cir::CtorKind::Copy) {
    // Only replace trivial copy constructor calls with cir.copy. Non-trivial
    // (user-defined) copy constructors must be called to execute their logic.
    if (!funcOp.isCxxTrivialMemberFunction())
      return;
    // Safety checks: constructor calls should have no return value and exactly
    // two operands (dest and src).
    if (op.getNumResults() > 0)
      return;
    mlir::ValueRange operands = op.getOperands();
    if (operands.size() != 2)
      return;
    // Replace the copy constructor call with a `CopyOp`
    CIRBaseBuilderTy builder(getContext());
    mlir::Value dest = operands[0];
    mlir::Value src = operands[1];
    builder.setInsertionPoint(op);
    builder.createCopy(dest, src);
    op.erase();
  }
}

void LoweringPreparePass::lowerStdFindOp(StdFindOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op.getOperation());
  auto call = builder.createCallOp(
      op.getLoc(), op.getOriginalFnAttr(), op.getType(),
      mlir::ValueRange{op.getOperand(0), op.getOperand(1), op.getOperand(2)});

  op.replaceAllUsesWith(call);
  op.erase();
}

void LoweringPreparePass::lowerStrLenOp(StrLenOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op.getOperation());
  auto call = builder.createCallOp(op.getLoc(), op.getOriginalFnAttr(),
                                   op.getType(), op.getOperand());

  op.replaceAllUsesWith(call);
  op.erase();
}

void LoweringPreparePass::lowerIterBeginOp(IterBeginOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op.getOperation());
  auto call = builder.createCallOp(op.getLoc(), op.getOriginalFnAttr(),
                                   op.getType(), op.getOperand());

  op.replaceAllUsesWith(call);
  op.erase();
}

void LoweringPreparePass::lowerIterEndOp(IterEndOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op.getOperation());
  auto call = builder.createCallOp(op.getLoc(), op.getOriginalFnAttr(),
                                   op.getType(), op.getOperand());

  op.replaceAllUsesWith(call);
  op.erase();
}

static void canonicalizeIntrinsicThreeWayCmp(CIRBaseBuilderTy &builder,
                                             cir::CmpThreeWayOp op) {
  auto loc = op->getLoc();
  auto cmpInfo = op.getInfo();

  if (cmpInfo.getLt() == -1 && cmpInfo.getEq() == 0 && cmpInfo.getGt() == 1) {
    // The comparison is already in canonicalized form.
    return;
  }

  auto canonicalizedCmpInfo =
      cir::CmpThreeWayInfoAttr::get(builder.getContext(), -1, 0, 1);
  mlir::Value result =
      cir::CmpThreeWayOp::create(builder, loc, op.getType(), op.getLhs(),
                                 op.getRhs(), canonicalizedCmpInfo)
          .getResult();

  auto compareAndYield = [&](mlir::Value input, int64_t test,
                             int64_t yield) -> mlir::Value {
    // Create a conditional branch that tests whether `input` is equal to
    // `test`. If `input` is equal to `test`, yield `yield`. Otherwise, yield
    // `input` as is.
    auto testValue =
        builder.getConstant(loc, cir::IntAttr::get(input.getType(), test));
    auto yieldValue =
        builder.getConstant(loc, cir::IntAttr::get(input.getType(), yield));
    auto eqToTest =
        builder.createCompare(loc, cir::CmpOpKind::eq, input, testValue);
    return builder.createSelect(loc, eqToTest, yieldValue, input);
  };

  if (cmpInfo.getLt() != -1)
    result = compareAndYield(result, -1, cmpInfo.getLt());

  if (cmpInfo.getEq() != 0)
    result = compareAndYield(result, 0, cmpInfo.getEq());

  if (cmpInfo.getGt() != 1)
    result = compareAndYield(result, 1, cmpInfo.getGt());

  op.replaceAllUsesWith(result);
  op.erase();
}

void LoweringPreparePass::lowerThreeWayCmpOp(cir::CmpThreeWayOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op);

  if (op.isIntegralComparison() && op.isStrongOrdering()) {
    // For three-way comparisons on integral operands that produce strong
    // ordering, we can generate potentially better code with the `llvm.scmp.*`
    // and `llvm.ucmp.*` intrinsics. Thus we don't replace these comparisons
    // here. They will be lowered directly to LLVMIR during the LLVM lowering
    // pass.
    //
    // But we still need to take a step here. `llvm.scmp.*` and `llvm.ucmp.*`
    // returns -1, 0, or 1 to represent lt, eq, and gt, which are the
    // "canonicalized" result values of three-way comparisons. However,
    // `cir.cmp3way` may not produce canonicalized result. We need to
    // canonicalize the comparison if necessary. This is what we're doing in
    // this special branch.
    canonicalizeIntrinsicThreeWayCmp(builder, op);
    return;
  }

  auto loc = op->getLoc();
  auto cmpInfo = op.getInfo();

  auto buildCmpRes = [&](int64_t value) -> mlir::Value {
    return cir::ConstantOp::create(builder, loc,
                                   cir::IntAttr::get(op.getType(), value));
  };
  auto ltRes = buildCmpRes(cmpInfo.getLt());
  auto eqRes = buildCmpRes(cmpInfo.getEq());
  auto gtRes = buildCmpRes(cmpInfo.getGt());

  auto buildCmp = [&](CmpOpKind kind) -> mlir::Value {
    auto ty = BoolType::get(&getContext());
    return cir::CmpOp::create(builder, loc, ty, kind, op.getLhs(), op.getRhs());
  };
  auto buildSelect = [&](mlir::Value condition, mlir::Value trueResult,
                         mlir::Value falseResult) -> mlir::Value {
    return builder.createSelect(loc, condition, trueResult, falseResult);
  };

  mlir::Value transformedResult;
  if (cmpInfo.getOrdering() == CmpOrdering::Strong) {
    // Strong ordering.
    auto lt = buildCmp(CmpOpKind::lt);
    auto eq = buildCmp(CmpOpKind::eq);
    auto selectOnEq = buildSelect(eq, eqRes, gtRes);
    transformedResult = buildSelect(lt, ltRes, selectOnEq);
  } else {
    // Partial ordering.
    auto unorderedRes = buildCmpRes(*cmpInfo.getUnordered());
    auto lt = buildCmp(CmpOpKind::lt);
    auto eq = buildCmp(CmpOpKind::eq);
    auto gt = buildCmp(CmpOpKind::gt);
    auto selectOnEq = buildSelect(eq, eqRes, unorderedRes);
    auto selectOnGt = buildSelect(gt, gtRes, selectOnEq);
    transformedResult = buildSelect(lt, ltRes, selectOnGt);
  }

  op.replaceAllUsesWith(transformedResult);
  op.erase();
}

void LoweringPreparePass::lowerThrowOp(ThrowOp op) {
  CIRBaseBuilderTy builder(getContext());

  if (op.rethrows()) {
    auto voidTy = cir::VoidType::get(builder.getContext());
    auto fnType = cir::FuncType::get({}, voidTy);
    auto fnName = "__cxa_rethrow";

    builder.setInsertionPointToStart(&mlirModule.getBodyRegion().front());
    FuncOp f = buildRuntimeFunction(builder, fnName, op.getLoc(), fnType);

    builder.setInsertionPointAfter(op.getOperation());
    auto call = builder.createTryCallOp(op.getLoc(), f, {});

    // __cxa_rethrow never returns — set noreturn on the call-site attributes
    // so the LLVM lowering propagates it.  Matches OGCG's call-site attrs.
    mlir::NamedAttrList callAttrs;
    auto noReturn = cir::NoReturnAttr::get(builder.getContext());
    callAttrs.set(noReturn.getMnemonic(), noReturn);
    call->setAttr(
        "extra_attrs",
        cir::ExtraFuncAttributesAttr::get(
            callAttrs.getDictionary(builder.getContext())));

    op->replaceAllUsesWith(call);
    op->erase();
  }
}

//===----------------------------------------------------------------------===//
// CUDA/HIP Module Registration Infrastructure
//===----------------------------------------------------------------------===//

void LoweringPreparePass::buildCUDAModuleCtor() {
  if (astCtx->getLangOpts().GPURelocatableDeviceCode)
    llvm_unreachable("NYI");

  // For CUDA without -fgpu-rdc, it's safe to stop generating ctor
  // if there's nothing to register.
  if (cudaKernelMap.empty() && cudaVarMap.empty())
    return;

  // There's no device-side binary, so no need to proceed for CUDA.
  auto cudaBinaryHandleAttr =
      mlirModule->getAttr(cir::CIRDialect::getCUDABinaryHandleAttrName());
  if (!cudaBinaryHandleAttr) {
    return;
  }
  std::string cudaGPUBinaryName =
      cast<cir::CUDABinaryHandleAttr>(cudaBinaryHandleAttr).getName();

  constexpr unsigned cudaFatMagic = 0x466243b1;
  constexpr unsigned hipFatMagic = 0x48495046; // "HIPF"

  auto cudaPrefix = getCUDAPrefix(astCtx);

  const unsigned fatMagic =
      astCtx->getLangOpts().HIP ? hipFatMagic : cudaFatMagic;

  // checkMacOSXTriple -- macOS-specific handling deferred.

  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointToStart(mlirModule.getBody());

  mlir::Location loc = mlirModule.getLoc();

  auto voidTy = VoidType::get(&getContext());
  auto voidPtrTy = PointerType::get(voidTy);
  auto voidPtrPtrTy = PointerType::get(voidPtrTy);
  auto intTy = IntType::get(&getContext(), astCtx->getTypeSize(astCtx->IntTy),
                            astCtx->IntTy->isSignedIntegerType());
  auto charTy = IntType::get(&getContext(), astCtx->getCharWidth(), false);

  // Read the GPU binary and create a constant array for it.
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> cudaGPUBinaryOrErr =
      llvm::MemoryBuffer::getFile(cudaGPUBinaryName);
  if (std::error_code ec = cudaGPUBinaryOrErr.getError()) {
    mlirModule->emitError("cannot open file: " + cudaGPUBinaryName +
                          ec.message());
    return;
  }
  std::unique_ptr<llvm::MemoryBuffer> cudaGPUBinary =
      std::move(cudaGPUBinaryOrErr.get());

  // The section names are different for MAC OS X.
  llvm::StringRef fatbinConstName =
      astCtx->getLangOpts().HIP ? ".hip_fatbin" : ".nv_fatbin";

  llvm::StringRef fatbinSectionName =
      astCtx->getLangOpts().HIP ? ".hipFatBinSegment" : ".nvFatBinSegment";

  // Create a global variable with the contents of GPU binary.
  auto fatbinType =
      ArrayType::get(&getContext(), charTy, cudaGPUBinary->getBuffer().size());

  std::string fatbinStrName = addUnderscoredPrefix(cudaPrefix, "_fatbin_str");
  GlobalOp fatbinStr = GlobalOp::create(
      builder, loc, fatbinStrName, fatbinType, /*isConstant=*/true,
      /*linkage=*/cir::GlobalLinkageKind::PrivateLinkage);
  fatbinStr.setAlignment(8);
  fatbinStr.setInitialValueAttr(cir::ConstArrayAttr::get(
      fatbinType, builder.getStringAttr(cudaGPUBinary->getBuffer())));
  fatbinStr.setSection(fatbinConstName);
  fatbinStr.setPrivate();

  // Create a record FatbinWrapper, pointing to the GPU binary.
  // Record layout:
  //    struct { int magicNum; int version; void *fatbin; void *unused; };
  auto fatbinWrapperType = RecordType::get(
      &getContext(), {intTy, intTy, voidPtrTy, voidPtrTy}, /*packed=*/false,
      /*padded=*/false, RecordType::RecordKind::Struct);

  std::string fatbinWrapperName =
      addUnderscoredPrefix(cudaPrefix, "_fatbin_wrapper");
  GlobalOp fatbinWrapper = GlobalOp::create(
      builder, loc, fatbinWrapperName, fatbinWrapperType, /*isConstant=*/true,
      /*linkage=*/cir::GlobalLinkageKind::InternalLinkage);
  fatbinWrapper.setPrivate();
  fatbinWrapper.setSection(fatbinSectionName);

  auto magicInit = IntAttr::get(intTy, fatMagic);
  auto versionInit = IntAttr::get(intTy, 1);
  auto fatbinStrSymbol =
      mlir::FlatSymbolRefAttr::get(fatbinStr.getSymNameAttr());
  auto fatbinInit = GlobalViewAttr::get(voidPtrTy, fatbinStrSymbol);
  auto unusedInit = builder.getConstNullPtrAttr(voidPtrTy);
  fatbinWrapper.setInitialValueAttr(cir::ConstRecordAttr::get(
      fatbinWrapperType,
      ArrayAttr::get(&getContext(),
                     {magicInit, versionInit, fatbinInit, unusedInit})));

  // GPU fat binary handle is also a global variable in OG.
  std::string gpubinHandleName =
      addUnderscoredPrefix(cudaPrefix, "_gpubin_handle");
  auto gpubinHandle = GlobalOp::create(
      builder, loc, gpubinHandleName, voidPtrPtrTy,
      /*isConstant=*/false, /*linkage=*/GlobalLinkageKind::InternalLinkage);
  gpubinHandle.setInitialValueAttr(builder.getConstNullPtrAttr(voidPtrPtrTy));
  gpubinHandle.setPrivate();

  // Declare this function:
  //    void **__{cuda|hip}RegisterFatBinary(void *);
  std::string regFuncName =
      addUnderscoredPrefix(cudaPrefix, "RegisterFatBinary");
  auto regFuncType = FuncType::get({voidPtrTy}, voidPtrPtrTy);
  auto regFunc = buildRuntimeFunction(builder, regFuncName, loc, regFuncType);

  // Create the module constructor.
  std::string moduleCtorName = addUnderscoredPrefix(cudaPrefix, "_module_ctor");
  auto moduleCtor = buildRuntimeFunction(builder, moduleCtorName, loc,
                                         FuncType::get({}, voidTy),
                                         GlobalLinkageKind::InternalLinkage);
  globalCtorList.emplace_back(moduleCtorName, /*priority=*/65535,
                              std::string{});
  globalCtorLexOrder.push_back(UINT_MAX);
  builder.setInsertionPointToStart(moduleCtor.addEntryBlock());
  if (astCtx->getLangOpts().HIP) {
    auto *entryBlock = builder.getInsertionBlock();
    auto *parent = builder.getInsertionBlock()->getParent();
    auto *ifBlock = builder.createBlock(parent);
    auto *exitBlock = builder.createBlock(parent);
    {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToEnd(entryBlock);
      mlir::Value handle =
          builder.createLoad(loc, builder.createGetGlobal(gpubinHandle));
      auto handlePtrTy = llvm::cast<cir::PointerType>(handle.getType());
      mlir::Value nullPtr = builder.getNullPtr(handlePtrTy, loc);
      auto isNull =
          builder.createCompare(loc, cir::CmpOpKind::eq, handle, nullPtr);
      cir::BrCondOp::create(builder, loc, isNull, ifBlock, exitBlock);
    }
    {
      // When handle is null we need to load the fatbin and register it
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToStart(ifBlock);
      auto wrapper = builder.createGetGlobal(fatbinWrapper);
      auto fatbinVoidPtr = builder.createBitcast(wrapper, voidPtrTy);
      auto gpuBinaryHandleCall =
          builder.createCallOp(loc, regFunc, fatbinVoidPtr);
      auto gpuBinaryHandle = gpuBinaryHandleCall.getResult();
      auto gpuBinaryHandleGlobal = builder.createGetGlobal(gpubinHandle);
      builder.createStore(loc, gpuBinaryHandle, gpuBinaryHandleGlobal);
      cir::BrOp::create(builder, loc, exitBlock);
    }
    {
      // Exit block
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToStart(exitBlock);
      mlir::Value gHandle =
          builder.createLoad(loc, builder.createGetGlobal(gpubinHandle));

      std::optional<FuncOp> regGlobal = buildCUDARegisterGlobals();
      if (regGlobal) {
        builder.createCallOp(loc, *regGlobal, gHandle);
      }

      if (auto dtor = buildHIPModuleDtor()) {
        cir::CIRBaseBuilderTy globalBuilder(getContext());
        globalBuilder.setInsertionPointToStart(mlirModule.getBody());
        FuncOp atexit = buildRuntimeFunction(
            globalBuilder, "atexit", loc,
            FuncType::get(PointerType::get(dtor->getFunctionType()), intTy));
        {
          mlir::NamedAttrList attrs;
          if (auto existingAttrs = atexit.getExtraAttrs())
            attrs =
                mlir::NamedAttrList{existingAttrs->getElements().getValue()};
          attrs.set("nothrow", mlir::UnitAttr::get(builder.getContext()));
          atexit.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
              builder.getContext(),
              mlir::DictionaryAttr::get(builder.getContext(), attrs)));
        }

        mlir::Value dtorFunc = GetGlobalOp::create(
            builder, loc, PointerType::get(dtor->getFunctionType()),
            mlir::FlatSymbolRefAttr::get(dtor->getSymNameAttr()));
        auto call = builder.createCallOp(loc, atexit, dtorFunc);
        call.setNothrow(true);
      }
      cir::ReturnOp::create(builder, loc);
    }
    return;
  }
  // CUDA CTOR-DTOR generations
  auto wrapper = builder.createGetGlobal(fatbinWrapper);
  auto fatbinVoidPtr = builder.createBitcast(wrapper, voidPtrTy);
  auto gpuBinaryHandleCall = builder.createCallOp(loc, regFunc, fatbinVoidPtr);
  auto gpuBinaryHandle = gpuBinaryHandleCall.getResult();
  auto gpuBinaryHandleGlobal = builder.createGetGlobal(gpubinHandle);
  builder.createStore(loc, gpuBinaryHandle, gpuBinaryHandleGlobal);

  // Generate __cuda_register_globals and call it.
  std::optional<FuncOp> regGlobal = buildCUDARegisterGlobals();
  if (regGlobal) {
    builder.createCallOp(loc, *regGlobal, gpuBinaryHandle);
  }

  // From CUDA 10.1 onwards, we must call this function to end registration:
  //      void __cudaRegisterFatBinaryEnd(void **fatbinHandle);
  if (clang::CudaFeatureEnabled(
          astCtx->getTargetInfo().getSDKVersion(),
          clang::CudaFeature::CUDA_USES_FATBIN_REGISTER_END)) {
    cir::CIRBaseBuilderTy globalBuilder(getContext());
    globalBuilder.setInsertionPointToStart(mlirModule.getBody());
    FuncOp endFunc =
        buildRuntimeFunction(globalBuilder, "__cudaRegisterFatBinaryEnd", loc,
                             FuncType::get({voidPtrPtrTy}, voidTy));
    builder.createCallOp(loc, endFunc, gpuBinaryHandle);
  }

  // Create destructor and register it with atexit().
  if (auto dtor = buildCUDAModuleDtor()) {
    cir::CIRBaseBuilderTy globalBuilder(getContext());
    globalBuilder.setInsertionPointToStart(mlirModule.getBody());
    FuncOp atexit = buildRuntimeFunction(
        globalBuilder, "atexit", loc,
        FuncType::get(PointerType::get(dtor->getFunctionType()), intTy));
    {
      mlir::NamedAttrList attrs;
      if (auto existingAttrs = atexit.getExtraAttrs())
        attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
      attrs.set("nothrow", mlir::UnitAttr::get(builder.getContext()));
      atexit.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
          builder.getContext(),
          mlir::DictionaryAttr::get(builder.getContext(), attrs)));
    }

    mlir::Value dtorFunc = GetGlobalOp::create(
        builder, loc, PointerType::get(dtor->getFunctionType()),
        mlir::FlatSymbolRefAttr::get(dtor->getSymNameAttr()));
    auto call = builder.createCallOp(loc, atexit, dtorFunc);
    call.setNothrow(true);
  }

  cir::ReturnOp::create(builder, loc);
}

std::optional<FuncOp> LoweringPreparePass::buildCUDARegisterGlobals() {
  // There is nothing to register.
  if (cudaKernelMap.empty() && cudaVarMap.empty())
    return {};

  cir::CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointToStart(mlirModule.getBody());

  auto loc = mlirModule.getLoc();
  auto cudaPrefix = getCUDAPrefix(astCtx);

  auto voidTy = VoidType::get(&getContext());
  auto voidPtrTy = PointerType::get(voidTy);
  auto voidPtrPtrTy = PointerType::get(voidPtrTy);

  // Create the function:
  //      void __cuda_register_globals(void **fatbinHandle)
  std::string regGlobalFuncName =
      addUnderscoredPrefix(cudaPrefix, "_register_globals");
  auto regGlobalFuncTy = FuncType::get({voidPtrPtrTy}, voidTy);
  FuncOp regGlobalFunc =
      buildRuntimeFunction(builder, regGlobalFuncName, loc, regGlobalFuncTy,
                           /*linkage=*/GlobalLinkageKind::InternalLinkage);
  builder.setInsertionPointToStart(regGlobalFunc.addEntryBlock());

  buildCUDARegisterGlobalFunctions(builder, regGlobalFunc);
  buildCUDARegisterVars(builder, regGlobalFunc);

  ReturnOp::create(builder, loc);
  return regGlobalFunc;
}

void LoweringPreparePass::buildCUDARegisterGlobalFunctions(
    cir::CIRBaseBuilderTy &builder, FuncOp regGlobalFunc) {
  if (cudaKernelMap.empty())
    return;

  auto loc = mlirModule.getLoc();
  auto cudaPrefix = getCUDAPrefix(astCtx);

  auto voidTy = VoidType::get(&getContext());
  auto voidPtrTy = PointerType::get(voidTy);
  auto voidPtrPtrTy = PointerType::get(voidPtrTy);
  auto intTy = IntType::get(&getContext(), astCtx->getTypeSize(astCtx->IntTy),
                            astCtx->IntTy->isSignedIntegerType());
  auto charTy = IntType::get(&getContext(), astCtx->getCharWidth(), false);

  mlir::Value fatbinHandle = *regGlobalFunc.args_begin();

  cir::CIRBaseBuilderTy globalBuilder(getContext());
  globalBuilder.setInsertionPointToStart(mlirModule.getBody());

  FuncOp cudaRegisterFunction = buildRuntimeFunction(
      globalBuilder, addUnderscoredPrefix(cudaPrefix, "RegisterFunction"), loc,
      FuncType::get({voidPtrPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, intTy,
                     voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy},
                    intTy));

  auto makeConstantString = [&](llvm::StringRef str) -> GlobalOp {
    auto strType = ArrayType::get(&getContext(), charTy, 1 + str.size());

    auto tmpString =
        GlobalOp::create(globalBuilder, loc, (".str" + str).str(), strType,
                         /*isConstant=*/true,
                         /*linkage=*/cir::GlobalLinkageKind::PrivateLinkage);

    tmpString.setInitialValueAttr(ConstArrayAttr::get(
        strType, StringAttr::get(&getContext(), str + "\0")));
    tmpString.setPrivate();
    return tmpString;
  };

  auto cirNullPtr = builder.getNullPtr(voidPtrTy, loc);
  for (auto kernelName : cudaKernelMap.keys()) {
    FuncOp deviceStub = cudaKernelMap[kernelName];
    GlobalOp deviceFuncStr = makeConstantString(kernelName);
    mlir::Value deviceFunc = builder.createBitcast(
        builder.createGetGlobal(deviceFuncStr), voidPtrTy);
    if (astCtx->getLangOpts().HIP) {
      auto funcHandle = cast<GlobalOp>(mlirModule.lookupSymbol(kernelName));
      mlir::Value hostFunc =
          builder.createBitcast(builder.createGetGlobal(funcHandle), voidPtrTy);
      builder.createCallOp(
          loc, cudaRegisterFunction,
          {fatbinHandle, hostFunc, deviceFunc, deviceFunc,
           ConstantOp::create(builder, loc, IntAttr::get(intTy, -1)),
           cirNullPtr, cirNullPtr, cirNullPtr, cirNullPtr, cirNullPtr});
    } else {
      mlir::Value hostFunc = builder.createBitcast(
          GetGlobalOp::create(
              builder, loc, PointerType::get(deviceStub.getFunctionType()),
              mlir::FlatSymbolRefAttr::get(deviceStub.getSymNameAttr())),
          voidPtrTy);
      builder.createCallOp(
          loc, cudaRegisterFunction,
          {fatbinHandle, hostFunc, deviceFunc, deviceFunc,
           ConstantOp::create(builder, loc, IntAttr::get(intTy, -1)),
           cirNullPtr, cirNullPtr, cirNullPtr, cirNullPtr, cirNullPtr});
    }
  }
}

void LoweringPreparePass::buildCUDARegisterVars(cir::CIRBaseBuilderTy &builder,
                                                FuncOp regGlobalFunc) {
  if (cudaVarMap.empty())
    return;

  auto loc = mlirModule.getLoc();
  auto cudaPrefix = getCUDAPrefix(astCtx);

  auto voidTy = VoidType::get(&getContext());
  auto voidPtrTy = PointerType::get(voidTy);
  auto voidPtrPtrTy = PointerType::get(voidPtrTy);
  auto intTy = IntType::get(&getContext(), astCtx->getTypeSize(astCtx->IntTy),
                            astCtx->IntTy->isSignedIntegerType());
  auto charTy = IntType::get(&getContext(), astCtx->getCharWidth(), false);
  unsigned sizeTypeSize = astCtx->getTypeSize(astCtx->getSignedSizeType());
  auto sizeTy = IntType::get(&getContext(), sizeTypeSize, false);

  mlir::Value fatbinHandle = *regGlobalFunc.args_begin();

  cir::CIRBaseBuilderTy globalBuilder(getContext());
  globalBuilder.setInsertionPointToStart(mlirModule.getBody());

  FuncOp cudaRegisterVar = buildRuntimeFunction(
      globalBuilder, addUnderscoredPrefix(cudaPrefix, "RegisterVar"), loc,
      FuncType::get({voidPtrPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, intTy,
                     sizeTy, intTy, intTy},
                    voidTy));

  unsigned int count = 0;
  auto makeConstantString = [&](llvm::StringRef str) -> GlobalOp {
    auto strType = ArrayType::get(&getContext(), charTy, 1 + str.size());

    auto tmpString = GlobalOp::create(
        globalBuilder, loc, (".str" + str + std::to_string(count++)).str(),
        strType, /*isConstant=*/true,
        /*linkage=*/cir::GlobalLinkageKind::PrivateLinkage);

    tmpString.setInitialValueAttr(ConstArrayAttr::get(
        strType, StringAttr::get(&getContext(), str + "\0")));
    tmpString.setPrivate();
    return tmpString;
  };

  for (auto &[deviceSideName, global] : cudaVarMap) {
    GlobalOp varNameStr = makeConstantString(deviceSideName);
    mlir::Value varNameValue =
        builder.createBitcast(builder.createGetGlobal(varNameStr), voidPtrTy);

    auto globalVarValue =
        builder.createBitcast(builder.createGetGlobal(global), voidPtrTy);

    auto deviceRegistrationAttr =
        global->getAttrOfType<cir::CUDAVarRegistrationInfoAttr>(
            cir::CUDAVarRegistrationInfoAttr::getMnemonic());

    bool isExternFlag =
        deviceRegistrationAttr ? deviceRegistrationAttr.getIsExtern() : false;
    bool isConstantFlag = deviceRegistrationAttr
                              ? deviceRegistrationAttr.getIsConstant()
                              : global.getConstant();
    bool isManagedFlag =
        deviceRegistrationAttr ? deviceRegistrationAttr.getIsManaged() : false;

    auto kind = deviceRegistrationAttr ? deviceRegistrationAttr.getKind()
                                       : cir::CUDADeviceVarKind::Variable;

    switch (kind) {
    case cir::CUDADeviceVarKind::Variable: {
      if (isManagedFlag)
        llvm_unreachable("Managed Variables NYI");

      auto isExtern =
          ConstantOp::create(builder, loc, IntAttr::get(intTy, isExternFlag));
      // Get type size via the data layout, matching OGCG's
      // getTypeAllocSize(GV->getValueType()) in makeRegisterGlobalsFn.
      auto globalTy = global.getSymType();
      uint64_t typeSizeInBytes = datalayout->getTypeAllocSize(globalTy);
      auto varSize = ConstantOp::create(
          builder, loc, IntAttr::get(sizeTy, typeSizeInBytes));
      auto isConstant =
          ConstantOp::create(builder, loc, IntAttr::get(intTy, isConstantFlag));
      auto zero = ConstantOp::create(builder, loc, IntAttr::get(intTy, 0));
      builder.createCallOp(loc, cudaRegisterVar,
                           {fatbinHandle, globalVarValue, varNameValue,
                            varNameValue, isExtern, varSize, isConstant, zero});
    } break;
    case cir::CUDADeviceVarKind::Surface: {
      // void __cudaRegisterSurface(void **, const struct surfaceReference *,
      //                            const void **, const char *, int, int);
      // Declared here rather than next to __cudaRegisterVar so a module with
      // no surfaces does not carry an unused declaration.
      FuncOp cudaRegisterSurf = buildRuntimeFunction(
          globalBuilder, addUnderscoredPrefix(cudaPrefix, "RegisterSurface"),
          loc,
          FuncType::get(
              {voidPtrPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, intTy, intTy},
              voidTy));
      auto surfType = ConstantOp::create(
          builder, loc,
          IntAttr::get(intTy, deviceRegistrationAttr.getSurfTexType()));
      auto isExtern =
          ConstantOp::create(builder, loc, IntAttr::get(intTy, isExternFlag));
      builder.createCallOp(loc, cudaRegisterSurf,
                           {fatbinHandle, globalVarValue, varNameValue,
                            varNameValue, surfType, isExtern});
    } break;
    case cir::CUDADeviceVarKind::Texture: {
      // void __cudaRegisterTexture(void **, const struct textureReference *,
      //                            const void **, const char *, int, int, int);
      FuncOp cudaRegisterTex = buildRuntimeFunction(
          globalBuilder, addUnderscoredPrefix(cudaPrefix, "RegisterTexture"),
          loc,
          FuncType::get({voidPtrPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, intTy,
                         intTy, intTy},
                        voidTy));
      auto texType = ConstantOp::create(
          builder, loc,
          IntAttr::get(intTy, deviceRegistrationAttr.getSurfTexType()));
      auto isNormalized = ConstantOp::create(
          builder, loc,
          IntAttr::get(intTy, deviceRegistrationAttr.getIsNormalized()));
      auto isExtern =
          ConstantOp::create(builder, loc, IntAttr::get(intTy, isExternFlag));
      builder.createCallOp(loc, cudaRegisterTex,
                           {fatbinHandle, globalVarValue, varNameValue,
                            varNameValue, texType, isNormalized, isExtern});
    } break;
    }
  }
}

std::optional<FuncOp> LoweringPreparePass::buildHIPModuleDtor() {
  if (!mlirModule->getAttr(cir::CIRDialect::getCUDABinaryHandleAttrName()))
    return {};

  std::string prefix = getCUDAPrefix(astCtx);

  auto voidTy = VoidType::get(&getContext());
  auto voidPtrPtrTy = PointerType::get(PointerType::get(voidTy));

  auto loc = mlirModule.getLoc();

  cir::CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointToStart(mlirModule.getBody());

  std::string unregisterFuncName =
      addUnderscoredPrefix(prefix, "UnregisterFatBinary");
  FuncOp unregisterFunc = buildRuntimeFunction(
      builder, unregisterFuncName, loc, FuncType::get({voidPtrPtrTy}, voidTy));

  std::string dtorName = addUnderscoredPrefix(prefix, "_module_dtor");
  FuncOp dtor =
      buildRuntimeFunction(builder, dtorName, loc, FuncType::get({}, voidTy),
                           GlobalLinkageKind::InternalLinkage);

  std::string gpubinName = addUnderscoredPrefix(prefix, "_gpubin_handle");
  auto gpuBinGlobal = cast<GlobalOp>(mlirModule.lookupSymbol(gpubinName));
  auto *entryBlock = dtor.addEntryBlock();
  auto *ifBlock = builder.createBlock(&dtor.getBody());
  auto *exitBlock = builder.createBlock(&dtor.getBody());
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToEnd(entryBlock);
  mlir::Value handle =
      builder.createLoad(loc, builder.createGetGlobal(gpuBinGlobal));
  auto handlePtrTy = llvm::cast<cir::PointerType>(handle.getType());
  mlir::Value nullPtr = builder.getNullPtr(handlePtrTy, loc);
  auto isNull = builder.createCompare(loc, cir::CmpOpKind::ne, handle, nullPtr);
  cir::BrCondOp::create(builder, loc, isNull, ifBlock, exitBlock);
  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(ifBlock);
    builder.createCallOp(loc, unregisterFunc, handle);
    builder.createStore(loc, nullPtr, builder.createGetGlobal(gpuBinGlobal));
    cir::BrOp::create(builder, loc, exitBlock);
  }
  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(exitBlock);
    cir::ReturnOp::create(builder, loc);
  }
  return dtor;
}

std::optional<FuncOp> LoweringPreparePass::buildCUDAModuleDtor() {
  if (!mlirModule->getAttr(cir::CIRDialect::getCUDABinaryHandleAttrName()))
    return {};

  std::string prefix = getCUDAPrefix(astCtx);

  auto voidTy = VoidType::get(&getContext());
  auto voidPtrPtrTy = PointerType::get(PointerType::get(voidTy));

  auto loc = mlirModule.getLoc();

  cir::CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointToStart(mlirModule.getBody());

  std::string unregisterFuncName =
      addUnderscoredPrefix(prefix, "UnregisterFatBinary");
  FuncOp unregisterFunc = buildRuntimeFunction(
      builder, unregisterFuncName, loc, FuncType::get({voidPtrPtrTy}, voidTy));

  std::string dtorName = addUnderscoredPrefix(prefix, "_module_dtor");
  FuncOp dtor =
      buildRuntimeFunction(builder, dtorName, loc, FuncType::get({}, voidTy),
                           GlobalLinkageKind::InternalLinkage);

  std::string gpubinName = addUnderscoredPrefix(prefix, "_gpubin_handle");
  auto gpuBinGlobal = cast<GlobalOp>(mlirModule.lookupSymbol(gpubinName));
  builder.setInsertionPointToStart(dtor.addEntryBlock());
  mlir::Value handle =
      builder.createLoad(loc, builder.createGetGlobal(gpuBinGlobal));
  builder.createCallOp(loc, unregisterFunc, handle);
  cir::ReturnOp::create(builder, loc);
  return dtor;
}

void LoweringPreparePass::runOnOp(mlir::Operation *op) {
  if (auto symOp = mlir::dyn_cast<mlir::SymbolOpInterface>(op))
    existingSymbols.insert(symOp.getName());

  if (auto arrayCtor = dyn_cast<cir::ArrayCtor>(op)) {
    lowerArrayCtor(arrayCtor);
  } else if (auto arrayDtor = dyn_cast<cir::ArrayDtor>(op)) {
    lowerArrayDtor(arrayDtor);
  } else if (auto cast = mlir::dyn_cast<cir::CastOp>(op)) {
    lowerCastOp(cast);
  } else if (auto complexAdd = mlir::dyn_cast<cir::ComplexAddOp>(op)) {
    lowerComplexAddOp(complexAdd);
  } else if (auto complexSub = mlir::dyn_cast<cir::ComplexSubOp>(op)) {
    lowerComplexSubOp(complexSub);
  } else if (auto complexDiv = mlir::dyn_cast<cir::ComplexDivOp>(op)) {
    lowerComplexDivOp(complexDiv);
  } else if (auto complexMul = mlir::dyn_cast<cir::ComplexMulOp>(op)) {
    lowerComplexMulOp(complexMul);
  } else if (auto glob = mlir::dyn_cast<cir::GlobalOp>(op)) {
    lowerGlobalOp(glob);
    if (auto attr = op->getAttr(cir::CUDAShadowNameAttr::getMnemonic())) {
      auto shadowNameAttr = dyn_cast<cir::CUDAShadowNameAttr>(attr);
      std::string deviceSideName = shadowNameAttr.getDeviceSideName();
      cudaVarMap[deviceSideName] = glob;
    }
  } else if (auto dynamicCast = mlir::dyn_cast<cir::DynamicCastOp>(op)) {
    lowerDynamicCastOp(dynamicCast);
  } else if (auto unary = mlir::dyn_cast<cir::UnaryOp>(op)) {
    lowerUnaryOp(unary);
  } else if (auto callOp = dyn_cast<cir::CallOp>(op)) {
    lowerTrivialCopyCall(callOp);
  } else if (auto stdFind = dyn_cast<cir::StdFindOp>(op)) {
    lowerStdFindOp(stdFind);
  } else if (auto strLen = dyn_cast<cir::StrLenOp>(op)) {
    lowerStrLenOp(strLen);
  } else if (auto iterBegin = dyn_cast<cir::IterBeginOp>(op)) {
    lowerIterBeginOp(iterBegin);
  } else if (auto iterEnd = dyn_cast<cir::IterEndOp>(op)) {
    lowerIterEndOp(iterEnd);
  } else if (auto threeWayCmp = dyn_cast<cir::CmpThreeWayOp>(op)) {
    lowerThreeWayCmpOp(threeWayCmp);
  } else if (auto throwOp = dyn_cast<cir::ThrowOp>(op)) {
    lowerThrowOp(throwOp);
  } else if (auto vaArgOp = dyn_cast<cir::VAArgOp>(op)) {
    lowerVAArgOp(vaArgOp);
  } else if (auto fnOp = dyn_cast<cir::FuncOp>(op)) {
    if (auto globalCtor = fnOp.getGlobalCtorPriority()) {
      globalCtorList.emplace_back(fnOp.getName(), globalCtor.value(),
                                  std::string{});
      globalCtorLexOrder.push_back(UINT_MAX);
    } else if (auto globalDtor = fnOp.getGlobalDtorPriority()) {
      if (fnOp->hasAttr("cir.register_global_dtor_with_atexit"))
        dtorsUsingAtExit[globalDtor.value()].push_back(fnOp.getName().str());
      else
        globalDtorList.emplace_back(fnOp.getName(), globalDtor.value(),
                                    std::string{});
    }
    if (auto annotations = fnOp.getAnnotationsAttr())
      addGlobalAnnotations(fnOp, annotations);
    if (auto extraAttrs = fnOp.getExtraAttrs()) {
      if (auto attr = extraAttrs->getElements().get(
              cir::CUDAKernelNameAttr::getMnemonic())) {
        auto cudaKernelAttr = dyn_cast<cir::CUDAKernelNameAttr>(attr);
        std::string kernelName = cudaKernelAttr.getKernelName();
        cudaKernelMap[kernelName] = fnOp;
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// Complete-object structor deduplication
//===----------------------------------------------------------------------===//

/// Return true if `fn` does nothing but forward its own arguments, in order, to
/// a single call of `base` and return. Both shapes CIRGen can produce are
/// accepted: the unoptimized one, where every argument is spilled to an alloca
/// and reloaded, and the optimized one, where arguments are used directly.
///
/// A pointer bitcast of an argument counts as forwarding it. That is what a
/// delegation looks like once the base variant has itself been replaced by one
/// belonging to a different class -- `D1` delegating to `Derived::D2`, which
/// `tryEmitBaseDestructorAsAlias` then redirects to `Base::D2` -- because
/// CIRGenModule::replacePointerTypeArgs retypes the `this` argument at the call
/// to match the new callee. A bitcast is only ever a retype: an inheritance
/// step that actually moves the pointer is a `cir.base_class_addr`, which is
/// not accepted here, and OGCG's `TryEmitBaseDestructorAsAlias` refuses the
/// aliasing outright unless the base sits at offset zero.
static bool isPureDelegationTo(cir::FuncOp fn, llvm::StringRef base) {
  mlir::Region &body = fn.getBody();
  if (!body.hasOneBlock())
    return false;
  mlir::Block &entry = body.front();

  // Values known to hold function argument #i.
  llvm::DenseMap<mlir::Value, unsigned> argOf;
  for (auto [idx, arg] : llvm::enumerate(entry.getArguments()))
    argOf[arg] = idx;
  // Stack slots known to hold function argument #i. A slot is only entered
  // here once its (single) store has been walked over, so a load that finds
  // its slot is guaranteed to be dominated by that store.
  llvm::DenseMap<mlir::Value, unsigned> slotOf;

  bool sawCall = false;
  for (mlir::Operation &op : entry) {
    if (mlir::isa<cir::AllocaOp>(op))
      continue;

    if (auto store = mlir::dyn_cast<cir::StoreOp>(op)) {
      auto arg = argOf.find(store.getValue());
      if (arg == argOf.end())
        return false;
      if (!store.getAddr().getDefiningOp<cir::AllocaOp>())
        return false;
      // At most one store per slot, so `slotOf` can never go stale.
      if (!slotOf.insert({store.getAddr(), arg->second}).second)
        return false;
      continue;
    }

    if (auto load = mlir::dyn_cast<cir::LoadOp>(op)) {
      auto slot = slotOf.find(load.getAddr());
      if (slot == slotOf.end())
        return false;
      argOf[load.getResult()] = slot->second;
      continue;
    }

    if (auto cast = mlir::dyn_cast<cir::CastOp>(op)) {
      if (cast.getKind() != cir::CastKind::bitcast)
        return false;
      if (!mlir::isa<cir::PointerType>(cast.getSrc().getType()) ||
          !mlir::isa<cir::PointerType>(cast.getType()))
        return false;
      auto arg = argOf.find(cast.getSrc());
      if (arg == argOf.end())
        return false;
      argOf[cast.getResult()] = arg->second;
      continue;
    }

    if (auto call = mlir::dyn_cast<cir::CallOp>(op)) {
      if (sawCall || call.getNumResults() != 0)
        return false;
      sawCall = true;
      std::optional<llvm::StringRef> callee = call.getCallee();
      if (!callee || *callee != base)
        return false;
      // A call with a cleanup region is part of an EH edge, not a plain tail
      // delegation.
      if (!call.getCleanup().empty())
        return false;
      mlir::OperandRange args = call.getArgOperands();
      if (args.size() != entry.getNumArguments())
        return false;
      for (auto [idx, arg] : llvm::enumerate(args)) {
        auto known = argOf.find(arg);
        if (known == argOf.end() || known->second != idx)
          return false;
      }
      continue;
    }

    if (auto ret = mlir::dyn_cast<cir::ReturnOp>(op)) {
      if (ret.getNumOperands() != 0)
        return false;
      continue;
    }

    return false;
  }

  return sawCall;
}

void LoweringPreparePass::foldDedupableStructors() {
  if (!mlirModule || !symTable)
    return;

  // Symbol names that the module records as plain strings rather than as
  // symbol references. AttrTypeReplacer cannot see those, so a structor
  // mentioned in one of them must not be folded away.
  llvm::StringSet<> opaqueRefs;
  auto collectCtorDtorList = [&](llvm::StringRef attrName) {
    auto list = mlirModule->getAttrOfType<mlir::ArrayAttr>(attrName);
    if (!list)
      return;
    for (mlir::Attribute entry : list) {
      mlir::StringAttr name, associated;
      if (auto ctor = mlir::dyn_cast<cir::GlobalCtorAttr>(entry)) {
        name = ctor.getName();
        associated = ctor.getAssociatedData();
      } else if (auto dtor = mlir::dyn_cast<cir::GlobalDtorAttr>(entry)) {
        name = dtor.getName();
        associated = dtor.getAssociatedData();
      }
      if (name)
        opaqueRefs.insert(name.getValue());
      if (associated)
        opaqueRefs.insert(associated.getValue());
    }
  };
  collectCtorDtorList(cir::CIRDialect::getGlobalCtorsAttrName());
  collectCtorDtorList(cir::CIRDialect::getGlobalDtorsAttrName());
  if (auto annotations =
          mlirModule->getAttrOfType<cir::GlobalAnnotationValuesAttr>(
              cir::CIRDialect::getGlobalAnnotationsAttrName())) {
    for (mlir::Attribute entry : annotations.getAnnotations()) {
      auto pair = mlir::dyn_cast<mlir::ArrayAttr>(entry);
      if (pair && !pair.empty())
        if (auto name = mlir::dyn_cast<mlir::StringAttr>(pair[0]))
          opaqueRefs.insert(name.getValue());
    }
  }

  // Gather the complete-object structors that can be folded into their base
  // variant.
  llvm::SmallVector<cir::FuncOp> tagged;
  llvm::SmallVector<std::pair<cir::FuncOp, mlir::StringAttr>> folds;
  mlirModule.walk([&](cir::FuncOp fn) {
    auto tag = fn->getAttrOfType<cir::StructorDedupAttr>(
        cir::StructorDedupAttr::getAttrName());
    if (!tag)
      return;
    tagged.push_back(fn);

    // The tag is only a statement of intent from CIRGen; everything that makes
    // the fold sound is re-checked here.
    if (fn.isDeclaration() || fn.getAliasee())
      return;
    if (!cir::isDiscardableIfUnused(fn.getLinkage()) &&
        fn.getLinkage() != cir::GlobalLinkageKind::AvailableExternallyLinkage)
      return;
    if (opaqueRefs.contains(fn.getSymName()))
      return;

    // The base variant has to be something this module actually provides,
    // since after the fold every reference lands on it.
    mlir::StringAttr base = tag.getBase().getAttr();
    mlir::Operation *baseOp = symTable->lookup(base);
    if (!baseOp)
      return;
    if (auto baseFn = mlir::dyn_cast<cir::FuncOp>(baseOp)) {
      if (baseFn.isDeclaration())
        return;
    } else if (!mlir::isa<cir::AliasOp>(baseOp)) {
      return;
    }

    if (!isPureDelegationTo(fn, base.getValue()))
      return;

    folds.emplace_back(fn, base);
  });

  // The tag has served its purpose either way; it must not reach the emitted
  // CIR. Erased functions take theirs with them.
  auto dropTags = [&]() {
    for (cir::FuncOp fn : tagged)
      if (fn)
        fn->removeAttr(cir::StructorDedupAttr::getAttrName());
  };

  if (folds.empty()) {
    dropTags();
    return;
  }

  dropTags();

  llvm::DenseMap<mlir::StringAttr, mlir::StringAttr> nameMap;
  for (auto &[fn, base] : folds)
    nameMap[fn.getSymNameAttr()] = base;

  // Rewrite every flat symbol reference in the module in a single walk. This
  // is the same mechanism CIRGenModule::applyReplacements() uses, and it
  // covers call callees, cir.get_global names, alias targets, cir.throw dtor
  // refs, vtable and member-pointer initializers, and anything else that
  // stores a FlatSymbolRefAttr.
  {
    mlir::AttrTypeReplacer replacer;
    replacer.addReplacement(
        [&](mlir::SymbolRefAttr attr)
            -> std::optional<std::pair<mlir::Attribute, mlir::WalkResult>> {
          if (!attr.getNestedReferences().empty())
            return std::nullopt;
          auto it = nameMap.find(attr.getRootReference());
          if (it == nameMap.end())
            return std::nullopt;
          return std::make_pair(
              mlir::Attribute(mlir::FlatSymbolRefAttr::get(it->second)),
              mlir::WalkResult::skip());
        });
    replacer.recursivelyReplaceElementsIn(mlirModule);
  }

  // Fail closed: only erase a structor once the module demonstrably has no
  // remaining reference to it. A survivor means some carrier of symbol names
  // was missed above; leaving the stub in place keeps the output correct, and
  // the assertion makes the gap visible in an asserts build.
  llvm::StringSet<> stillUsed;
  if (std::optional<mlir::SymbolTable::UseRange> uses =
          mlir::SymbolTable::getSymbolUses(mlirModule)) {
    for (const mlir::SymbolTable::SymbolUse &use : *uses) {
      auto ref = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(use.getSymbolRef());
      if (!ref)
        continue;
      if (nameMap.contains(ref.getAttr()))
        stillUsed.insert(ref.getValue());
    }
  } else {
    // Symbol uses could not be enumerated; do not erase anything.
    for (auto &[fn, base] : folds)
      stillUsed.insert(fn.getSymName());
  }

  for (auto &[fn, base] : folds) {
    if (stillUsed.contains(fn.getSymName())) {
      assert(false && "structor dedup: symbol reference survived rewriting");
      continue;
    }
    symTable->erase(fn);
  }
}

void LoweringPreparePass::runOnOperation() {
  // Whichever way this returns, the module has been through it.
  auto recordStage = llvm::make_scope_exit(
      [&] { cir::advanceStage(getOperation(), cir::CIRStage::Prepare); });
  mlir::Operation *op = getOperation();
  if (isa<::mlir::ModuleOp>(op)) {
    mlirModule = cast<::mlir::ModuleOp>(op);
    datalayout.emplace(mlirModule);
  }

  if (mlirModule)
    datalayout.emplace(mlirModule);

  // Build a symbol table once for O(1) lookups.
  std::optional<mlir::SymbolTable> symTableStorage;
  if (mlirModule) {
    symTableStorage.emplace(mlirModule);
    symTable = &*symTableStorage;
    mlirModule.walk([&](mlir::Operation *nestedOp) {
      if (auto symOp = mlir::dyn_cast<mlir::SymbolOpInterface>(nestedOp))
        existingSymbols.insert(symOp.getName());
    });
  }

  llvm::SmallVector<mlir::Operation *> opsToTransform;

  op->walk([&](mlir::Operation *op) {
    if (mlir::isa<cir::ArrayCtor, cir::ArrayDtor, cir::CastOp,
                  cir::ComplexAddOp, cir::ComplexSubOp, cir::ComplexMulOp,
                  cir::ComplexDivOp, cir::CmpThreeWayOp, cir::VAArgOp,
                  cir::DynamicCastOp, cir::FuncOp, cir::CallOp, cir::GlobalOp,
                  cir::UnaryOp, cir::StdFindOp, cir::StrLenOp, cir::IterEndOp,
                  cir::IterBeginOp, cir::ThrowOp>(op))
      opsToTransform.push_back(op);
  });

  for (mlir::Operation *o : opsToTransform)
    runOnOp(o);

  if (encounteredLoweringError) {
    signalPassFailure();
    return;
  }

  if (astCtx->getLangOpts().CUDA && !astCtx->getLangOpts().CUDAIsDevice) {
    buildCUDAModuleCtor();
  }

  buildCXXGlobalInitFunc();
  buildRegisterGlobalDtorsWithAtExit();
  if (encounteredLoweringError) {
    signalPassFailure();
    return;
  }
  buildCXXTLSInitFunc();
  buildGlobalCtorDtorList();
  buildGlobalAnnotationValues();

  // Apply default internal function attrs to CIRGen-created helper functions
  // that were emitted before the module-level default attr bundle existed.
  // This covers .text.startup helpers and local-static atexit wrappers
  // (__dtor_*), including targets like plain x86_64 that do not assign a
  // startup section to those wrappers.
  if (mlirModule) {
    auto defaultAttrs = mlirModule->getAttrOfType<cir::ExtraFuncAttributesAttr>(
        "cir.default_internal_func_attrs");
    if (defaultAttrs) {
      mlirModule.walk([&](cir::FuncOp fn) {
        if (fn.getLinkage() != cir::GlobalLinkageKind::InternalLinkage)
          return;
        if (fn.getExtraAttrsAttr() &&
            !fn.getExtraAttrsAttr().getElements().empty())
          return;

        bool inStartupSection =
            fn.getSectionAttr() && fn.getSectionAttr().getValue() == ".text.startup";
        bool isCIRGenAtExitDtorStub = fn.getName().starts_with("__dtor_");
        if (inStartupSection || isCIRGenAtExitDtorStub)
          applyDefaultInternalFunctionAttrs(mlirModule, fn);
      });
    }
  }

  // Every helper that can introduce a reference to a complete-object structor
  // has run by now, so the module is finally stable enough to drop the
  // redundant ones.
  foldDedupableStructors();

  // Remove the module-level default attrs after consumption. This attribute
  // was only needed to pass CodeGenOptions-derived defaults from CIRGen to
  // LoweringPrepare; it shouldn't appear in the final CIR output.
  if (mlirModule)
    mlirModule->removeAttr("cir.default_internal_func_attrs");
}

std::unique_ptr<Pass> mlir::createLoweringPreparePass() {
  return std::make_unique<LoweringPreparePass>();
}

std::unique_ptr<Pass>
mlir::createLoweringPreparePass(clang::ASTContext *astCtx) {
  auto pass = std::make_unique<LoweringPreparePass>();
  pass->setASTContext(astCtx);
  return std::move(pass);
}
