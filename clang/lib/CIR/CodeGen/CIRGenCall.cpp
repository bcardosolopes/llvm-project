//===--- CIRGenCall.cpp - Encapsulate calling convention details ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These classes wrap the information about a call or function definition used
// to handle ABI compliancy.
//
//===----------------------------------------------------------------------===//

#include "CIRGenCall.h"
#include "CIRGenCXXABI.h"
#include "CIRGenFunction.h"
#include "CIRGenFunctionInfo.h"
#include "CIRGenSanitizer.h"
#include "mlir/IR/Dominance.h"
#include "clang/AST/Attr.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/Mangle.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/NoSanitizeList.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"
#include <functional>

using namespace clang;
using namespace clang::CIRGen;

static void emitWritebacks(CIRGenFunction &cgf, const CallArgList &args);

namespace {

constexpr llvm::StringLiteral kShareSyntheticCleanupAttr =
    "cir.share_synthetic_cleanup";

static const NonNullAttr *getNonNullAttr(const Decl *fd, const ParmVarDecl *pvd,
                                         QualType argType, unsigned argNo) {
  if (!argType->isAnyPointerType() && !argType->isBlockPointerType())
    return nullptr;
  if (pvd)
    if (auto parmNNAttr = pvd->getAttr<NonNullAttr>())
      return parmNNAttr;
  if (!fd)
    return nullptr;
  for (const auto *nnAttr : fd->specific_attrs<NonNullAttr>()) {
    if (nnAttr->isNonNull(argNo))
      return nnAttr;
  }
  return nullptr;
}

} // namespace

static void eraseNoReturnAttr(cir::FuncOp fn) {
  auto extraAttrs = fn.getExtraAttrs();
  if (!extraAttrs || !extraAttrs->getElements().get("noreturn"))
    return;

  mlir::NamedAttrList attrs{extraAttrs->getElements().getValue()};
  attrs.erase("noreturn");
  fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
      attrs.getDictionary(fn.getContext())));
}

static void setCUDAKernelCallingConvention(CanQualType &FTy, CIRGenModule &CGM,
                                           const FunctionDecl *FD) {
  if (FD->hasAttr<CUDAGlobalAttr>()) {
    const FunctionType *FT = FTy->getAs<FunctionType>();
    CGM.getTargetCIRGenInfo().setCUDAKernelCallingConvention(FT);
    FTy = FT->getCanonicalTypeUnqualified();
  }
}

CIRGenFunctionInfo *
CIRGenFunctionInfo::create(CanQualType resultType,
                           llvm::ArrayRef<CanQualType> argTypes,
                           RequiredArgs required,
                           FunctionType::ExtInfo extInfo,
                           bool instanceMethod, bool chainCall,
                           bool delegateCall) {
  // The first slot allocated for arg type slot is for the return value.
  void *buffer = operator new(
      totalSizeToAlloc<CanQualType>(argTypes.size() + 1));

  CIRGenFunctionInfo *fi = new (buffer) CIRGenFunctionInfo();

  fi->required = required;
  fi->numArgs = argTypes.size();
  fi->extInfo = extInfo;
  fi->instanceMethod = instanceMethod;
  fi->chainCall = chainCall;
  fi->delegateCall = delegateCall;

  fi->getArgTypes()[0] = resultType;
  std::copy(argTypes.begin(), argTypes.end(), fi->argTypesBegin());

  return fi;
}

cir::FuncType CIRGenTypes::getFunctionType(GlobalDecl gd) {
  const CIRGenFunctionInfo &fi = arrangeGlobalDeclaration(gd);
  return getFunctionType(fi);
}

cir::FuncType CIRGenTypes::getFunctionType(const CIRGenFunctionInfo &info) {
  auto it = funcTypeCache.find(&info);
  if (it != funcTypeCache.end())
    return it->second;

  mlir::Type resultType = convertType(info.getReturnType());
  SmallVector<mlir::Type, 8> argTypes;
  argTypes.reserve(info.getNumRequiredArgs());

  bool hasUnconvertibleArg = false;
  for (const CanQualType &argType : info.requiredArguments()) {
    mlir::Type converted = convertType(argType);
    // convertType returns a default-constructed (null) mlir::Type from its
    // NYI fallthrough path. Pushing a null arg type into cir::FuncType::get
    // segfaults inside MLIR's TypeUniquer (the null Type's getContext() is
    // dereferenced when constructing the storage), masking the underlying
    // NYI as an unrelated cc1 SEGV. Detect the null here, leave a real NYI
    // diagnostic with the offending parameter type, and emit a placeholder
    // FuncType so the frontend can finish reporting and exit cleanly.
    if (!converted) {
      cgm.errorNYI(SourceLocation(), "function parameter type lowering",
                   QualType(argType));
      hasUnconvertibleArg = true;
      break;
    }
    argTypes.push_back(converted);
  }

  // Mirror the same defence for the return type. The existing ternary
  // (resultType ? resultType : builder.getVoidTy()) silently swallowed the
  // null, which is correct for a true 'void' return but hides genuine
  // conversion failures of non-void returns. We surface the latter as an NYI.
  if (!hasUnconvertibleArg && !resultType &&
      !info.getReturnType()->isVoidType()) {
    cgm.errorNYI(SourceLocation(), "function return type lowering",
                 QualType(info.getReturnType()));
    hasUnconvertibleArg = true;
  }

  if (hasUnconvertibleArg) {
    // Placeholder matches the pattern in CIRGenTypes::convertFunctionType
    // when the function type involves an incomplete record (line 165).
    cir::FuncType placeholder =
        cir::FuncType::get(SmallVector<mlir::Type, 1>{}, builder.getVoidTy());
    funcTypeCache[&info] = placeholder;
    return placeholder;
  }

  cir::FuncType funcType = cir::FuncType::get(
      argTypes, (resultType ? resultType : builder.getVoidTy()),
      info.isVariadic());
  funcTypeCache[&info] = funcType;
  return funcType;
}

cir::FuncType CIRGenTypes::getFunctionTypeForVTable(GlobalDecl gd) {
  const CXXMethodDecl *md = cast<CXXMethodDecl>(gd.getDecl());
  const FunctionProtoType *fpt = md->getType()->getAs<FunctionProtoType>();

  if (!isFuncTypeConvertible(fpt)) {
    // When a vtable function type involves incomplete types, return a
    // placeholder void() type. This is safe because vtable slots store
    // function pointers cast to i8*, and the actual function type is
    // only needed when the function is called (by which time the types
    // will be complete). Matches OGCG which returns a placeholder
    // llvm::StructType here.
    return cir::FuncType::get({}, builder.getVoidTy());
  }

  return getFunctionType(gd);
}

CIRGenCallee CIRGenCallee::prepareConcreteCallee(CIRGenFunction &cgf) const {
  if (isVirtual()) {
    const CallExpr *ce = getVirtualCallExpr();
    return cgf.cgm.getCXXABI().getVirtualFunctionPointer(
        cgf, getVirtualMethodDecl(), getThisAddress(), getVirtualFunctionType(),
        ce ? ce->getBeginLoc() : SourceLocation());
  }
  return *this;
}

void CIRGenFunction::emitAggregateStore(mlir::Value value, Address dest,
                                         bool destIsVolatile) {
  // In classic codegen:
  // Function to store a first-class aggregate into memory. We prefer to
  // store the elements rather than the aggregate to be more friendly to
  // fast-isel.
  // In CIR codegen:
  // Emit the most simple cir.store possible (e.g. a store for a whole
  // record), which can later be broken down in other CIR levels (or prior
  // to dialect codegen).

  // Stored result for the callers of this function expected to be in the same
  // scope as the value, don't make assumptions about current insertion point.
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointAfter(value.getDefiningOp());
  auto storeOp = builder.createStore(*currSrcLoc, value, dest, destIsVolatile);
  addOpToCurrentSourceAtom(storeOp, value);
}

static void addAttributesFromFunctionProtoType(CIRGenBuilderTy &builder,
                                               mlir::NamedAttrList &attrs,
                                               const FunctionProtoType *fpt) {
  if (!fpt)
    return;

  if (!isUnresolvedExceptionSpec(fpt->getExceptionSpecType()) &&
      fpt->isNothrow()) {
    auto attr = cir::NoThrowAttr::get(builder.getContext());
    attrs.set(attr.getMnemonic(), attr);
  }

  // AArch64 SME attributes from the FunctionProtoType.
  // Matches OGCG's AddAttributesFromFunctionProtoType (CGCall.cpp:1840-1877).
  unsigned smeBits = fpt->getAArch64SMEAttributes();
  if (smeBits & FunctionType::SME_PStateSMEnabledMask)
    attrs.set("aarch64_pstate_sm_enabled",
              mlir::UnitAttr::get(builder.getContext()));
  if (smeBits & FunctionType::SME_PStateSMCompatibleMask)
    attrs.set("aarch64_pstate_sm_compatible",
              mlir::UnitAttr::get(builder.getContext()));
  if (smeBits & FunctionType::SME_AgnosticZAStateMask)
    attrs.set("aarch64_za_state_agnostic",
              mlir::UnitAttr::get(builder.getContext()));

  // ZA state
  if (FunctionType::getArmZAState(smeBits) == FunctionType::ARM_Preserves)
    attrs.set("aarch64_preserves_za",
              mlir::UnitAttr::get(builder.getContext()));
  if (FunctionType::getArmZAState(smeBits) == FunctionType::ARM_In)
    attrs.set("aarch64_in_za", mlir::UnitAttr::get(builder.getContext()));
  if (FunctionType::getArmZAState(smeBits) == FunctionType::ARM_Out)
    attrs.set("aarch64_out_za", mlir::UnitAttr::get(builder.getContext()));
  if (FunctionType::getArmZAState(smeBits) == FunctionType::ARM_InOut)
    attrs.set("aarch64_inout_za", mlir::UnitAttr::get(builder.getContext()));

  // ZT0 state
  if (FunctionType::getArmZT0State(smeBits) == FunctionType::ARM_Preserves)
    attrs.set("aarch64_preserves_zt0",
              mlir::UnitAttr::get(builder.getContext()));
  if (FunctionType::getArmZT0State(smeBits) == FunctionType::ARM_In)
    attrs.set("aarch64_in_zt0", mlir::UnitAttr::get(builder.getContext()));
  if (FunctionType::getArmZT0State(smeBits) == FunctionType::ARM_Out)
    attrs.set("aarch64_out_zt0", mlir::UnitAttr::get(builder.getContext()));
  if (FunctionType::getArmZT0State(smeBits) == FunctionType::ARM_InOut)
    attrs.set("aarch64_inout_zt0",
              mlir::UnitAttr::get(builder.getContext()));
}

/// Apply CodeGenOpts-driven default function attributes. These are attributes
/// that are essentially part of the global target configuration (but can be
/// overridden per-function). Matches OGCG's getTrivialDefaultFunctionAttributes
/// in CGCall.cpp.
static void
getTrivialDefaultFunctionAttributes(llvm::StringRef name, bool hasOptnone,
                                    CIRGenModule &cgm,
                                    const CodeGenOptions &codeGenOpts,
                                    const LangOptions &langOpts,
                                    bool attrOnCallSite,
                                    mlir::MLIRContext *ctx,
                                    mlir::NamedAttrList &funcAttrs) {
  // OptimizeNoneAttr takes precedence over -Os or -Oz.
  if (!hasOptnone) {
    if (codeGenOpts.OptimizeSize)
      funcAttrs.set("optsize", mlir::UnitAttr::get(ctx));
    if (codeGenOpts.OptimizeSize == 2)
      funcAttrs.set("minsize", mlir::UnitAttr::get(ctx));
  }

  if (codeGenOpts.DisableRedZone)
    funcAttrs.set("noredzone", mlir::UnitAttr::get(ctx));
  if (codeGenOpts.IndirectTlsSegRefs)
    funcAttrs.set("indirect-tls-seg-refs", mlir::UnitAttr::get(ctx));
  if (codeGenOpts.NoImplicitFloat)
    funcAttrs.set("noimplicitfloat", mlir::UnitAttr::get(ctx));

  if (attrOnCallSite) {
    // Attributes that should go on the call site only.
    if (!codeGenOpts.SimplifyLibCalls || langOpts.isNoBuiltinFunc(name))
      funcAttrs.set("nobuiltin", mlir::UnitAttr::get(ctx));
    if (!codeGenOpts.TrapFuncName.empty())
      funcAttrs.set("trap-func-name",
                    cgm.getStringAttrCached(codeGenOpts.TrapFuncName));
  } else {
    auto framePointerKindName = [&]() -> const char * {
      switch (codeGenOpts.getFramePointer()) {
      case CodeGenOptions::FramePointerKind::None:
        return "none";
      case CodeGenOptions::FramePointerKind::Reserved:
        return "reserved";
      case CodeGenOptions::FramePointerKind::NonLeafNoReserve:
        return "non-leaf-no-reserve";
      case CodeGenOptions::FramePointerKind::NonLeaf:
        return "non-leaf";
      case CodeGenOptions::FramePointerKind::All:
        return "all";
      }
      llvm_unreachable("invalid FramePointerKind");
    };

    switch (codeGenOpts.getFramePointer()) {
    case CodeGenOptions::FramePointerKind::None:
      // This is the default behavior.
      break;
    case CodeGenOptions::FramePointerKind::Reserved:
    case CodeGenOptions::FramePointerKind::NonLeafNoReserve:
    case CodeGenOptions::FramePointerKind::NonLeaf:
    case CodeGenOptions::FramePointerKind::All:
      funcAttrs.set("frame-pointer",
                    cgm.getStringAttrCached(framePointerKindName()));
    }

    if (codeGenOpts.LessPreciseFPMAD)
      funcAttrs.set("less-precise-fpmad",
                    cgm.getStringAttrCached("true"));

    if (codeGenOpts.NullPointerIsValid)
      funcAttrs.set("null-pointer-is-valid",
                    cgm.getStringAttrCached("true"));

    if (langOpts.getDefaultExceptionMode() == LangOptions::FPE_Ignore)
      funcAttrs.set("no-trapping-math", cgm.getStringAttrCached("true"));

    if (langOpts.NoHonorInfs)
      funcAttrs.set("no-infs-fp-math", cgm.getStringAttrCached("true"));
    if (langOpts.NoHonorNaNs)
      funcAttrs.set("no-nans-fp-math", cgm.getStringAttrCached("true"));
    if (codeGenOpts.SoftFloat)
      funcAttrs.set("use-soft-float", cgm.getStringAttrCached("true"));
    funcAttrs.set("stack-protector-buffer-size",
                  cgm.getUIntStringAttrCached(codeGenOpts.SSPBufferSize));
    if (langOpts.NoSignedZero)
      funcAttrs.set("no-signed-zeros-fp-math",
                    cgm.getStringAttrCached("true"));

    const std::vector<std::string> &recips = codeGenOpts.Reciprocals;
    if (!recips.empty())
      funcAttrs.set("reciprocal-estimates",
                    cgm.getStringAttrCached(llvm::join(recips, ",")));

    if (!codeGenOpts.PreferVectorWidth.empty() &&
        codeGenOpts.PreferVectorWidth != "none")
      funcAttrs.set("prefer-vector-width",
                    cgm.getStringAttrCached(codeGenOpts.PreferVectorWidth));

    if (codeGenOpts.StackRealignment)
      funcAttrs.set("stackrealign", mlir::UnitAttr::get(ctx));
    if (codeGenOpts.Backchain)
      funcAttrs.set("backchain", mlir::UnitAttr::get(ctx));
    if (codeGenOpts.EnableSegmentedStacks)
      funcAttrs.set("split-stack", mlir::UnitAttr::get(ctx));

    if (codeGenOpts.SpeculativeLoadHardening)
      funcAttrs.set("speculative-load-hardening", mlir::UnitAttr::get(ctx));

    // Add zero-call-used-regs attribute.
    switch (codeGenOpts.getZeroCallUsedRegs()) {
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::Skip:
      break;
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::UsedGPRArg:
      funcAttrs.set("zero-call-used-regs",
                    cgm.getStringAttrCached("used-gpr-arg"));
      break;
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::UsedGPR:
      funcAttrs.set("zero-call-used-regs",
                    cgm.getStringAttrCached("used-gpr"));
      break;
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::UsedArg:
      funcAttrs.set("zero-call-used-regs",
                    cgm.getStringAttrCached("used-arg"));
      break;
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::Used:
      funcAttrs.set("zero-call-used-regs",
                    cgm.getStringAttrCached("used"));
      break;
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::AllGPRArg:
      funcAttrs.set("zero-call-used-regs",
                    cgm.getStringAttrCached("all-gpr-arg"));
      break;
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::AllGPR:
      funcAttrs.set("zero-call-used-regs",
                    cgm.getStringAttrCached("all-gpr"));
      break;
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::AllArg:
      funcAttrs.set("zero-call-used-regs",
                    cgm.getStringAttrCached("all-arg"));
      break;
    case llvm::ZeroCallUsedRegs::ZeroCallUsedRegsKind::All:
      funcAttrs.set("zero-call-used-regs",
                    cgm.getStringAttrCached("all"));
      break;
    }
  }

  // Convergent and NoUnwind for CUDA/OpenCL/SYCL are handled earlier in
  // constructAttributeList — don't duplicate them here.

  if (codeGenOpts.SaveRegParams && !attrOnCallSite)
    funcAttrs.set("save-reg-params", mlir::UnitAttr::get(ctx));

  for (llvm::StringRef attr : codeGenOpts.DefaultFunctionAttrs) {
    auto [var, value] = attr.split('=');
    funcAttrs.set(var, cgm.getStringAttrCached(value));
  }

  // Branch protection attributes.
  TargetInfo::BranchProtectionInfo BPI(langOpts);
  if (BPI.SignReturnAddr != LangOptions::SignReturnAddressScopeKind::None) {
    funcAttrs.set("sign-return-address",
                  cgm.getStringAttrCached(BPI.getSignReturnAddrStr()));
    funcAttrs.set("sign-return-address-key",
                  cgm.getStringAttrCached(BPI.getSignKeyStr()));
  }
  if (BPI.BranchTargetEnforcement)
    funcAttrs.set("branch-target-enforcement", mlir::UnitAttr::get(ctx));
  if (BPI.BranchProtectionPAuthLR)
    funcAttrs.set("branch-protection-pauth-lr", mlir::UnitAttr::get(ctx));
  if (BPI.GuardedControlStack)
    funcAttrs.set("guarded-control-stack", mlir::UnitAttr::get(ctx));

  if (!attrOnCallSite) {
    const auto &PAO = codeGenOpts.PointerAuth;
    if (PAO.ReturnAddresses)
      funcAttrs.set("ptrauth-returns", mlir::UnitAttr::get(ctx));
    if (PAO.FunctionPointers)
      funcAttrs.set("ptrauth-calls", mlir::UnitAttr::get(ctx));
    if (PAO.AuthTraps)
      funcAttrs.set("ptrauth-auth-traps", mlir::UnitAttr::get(ctx));
    if (PAO.IndirectGotos)
      funcAttrs.set("ptrauth-indirect-gotos", mlir::UnitAttr::get(ctx));
    if (PAO.AArch64JumpTableHardening)
      funcAttrs.set("aarch64-jump-table-hardening",
                    mlir::UnitAttr::get(ctx));
  }
}

void CIRGenModule::getDefaultFunctionAttributes(
    llvm::StringRef name, bool hasOptnone, bool attrOnCallSite,
    mlir::NamedAttrList &funcAttrs) {
  // Fast path: for common cases (no name, no optnone), reuse a cached
  // DictionaryAttr to avoid ~20 StringAttr::get + UnitAttr::get calls
  // and a DictionaryAttr::get (sort + hash + uniquify) per invocation.
  if (name.empty() && !hasOptnone) {
    auto &cached =
        attrOnCallSite ? cachedDefaultCallSiteAttrs : cachedDefaultFuncDefAttrs;
    if (!cached) {
      mlir::NamedAttrList tmp;
      getTrivialDefaultFunctionAttributes(name, hasOptnone, *this,
                                          getCodeGenOpts(),
                                          getLangOpts(), attrOnCallSite,
                                          &getMLIRContext(), tmp);
          auto &ctx = getMLIRContext();
      llvm::DenormalMode FPDenormalMode = getCodeGenOpts().FPDenormalMode;
      llvm::DenormalMode FP32DenormalMode = getCodeGenOpts().FP32DenormalMode;
      if (FPDenormalMode != llvm::DenormalMode::getDefault())
        tmp.set("denormal-fp-math",
                getStringAttrCached(FPDenormalMode.str()));
      if (FP32DenormalMode != FPDenormalMode && FP32DenormalMode.isValid())
        tmp.set("denormal-fp-math-f32",
                getStringAttrCached(FP32DenormalMode.str()));
      cached = tmp.getDictionary(&ctx);
    }
    // Merge cached attrs into the output list.
    for (auto attr : cached)
      funcAttrs.set(attr.getName(), attr.getValue());
    return;
  }

  getTrivialDefaultFunctionAttributes(name, hasOptnone, *this,
                                      getCodeGenOpts(),
                                      getLangOpts(), attrOnCallSite,
                                      &getMLIRContext(), funcAttrs);

  // Pointer auth function attributes are not yet supported.

  // Add denormal-fp-math and denormal-fp-math-f32 attributes based on
  // the requested denormal behavior (CGCall.cpp:1941-1948, 1957).
  {
    llvm::DenormalMode FPDenormalMode = getCodeGenOpts().FPDenormalMode;
    llvm::DenormalMode FP32DenormalMode = getCodeGenOpts().FP32DenormalMode;
    if (FPDenormalMode != llvm::DenormalMode::getDefault())
      funcAttrs.set("denormal-fp-math",
                    getStringAttrCached(FPDenormalMode.str()));
    if (FP32DenormalMode != FPDenormalMode && FP32DenormalMode.isValid())
      funcAttrs.set("denormal-fp-math-f32",
                    getStringAttrCached(FP32DenormalMode.str()));
  }
}

/// Construct the CIR attribute list of a function or call.
void CIRGenModule::constructAttributeList(llvm::StringRef name,
                                          const CIRGenFunctionInfo &info,
                                          CIRGenCalleeInfo calleeInfo,
                                          mlir::NamedAttrList &attrs,
                                          cir::CallingConv &callingConv,
                                          cir::SideEffect &sideEffect,
                                          bool attrOnCallSite, bool isThunk) {
  // Use the effective calling convention, matching OGCG. Some targets (for
  // example ARM when the ABI default differs from LLVM's inferred default)
  // must upgrade plain CC_C functions to a target-specific ABI CC.
  callingConv =
      getTypes().ClangCallConvToCIRCallConv(info.getASTCallingConvention());
  if (callingConv == cir::CallingConv::C) {
    cir::CallingConv effectiveCC =
        getTargetCIRGenInfo().getRuntimeCallingConv(*this);
    if (effectiveCC != cir::CallingConv::C)
      callingConv = effectiveCC;
  }

  // OGCG checks FI.isCmseNSCall() to add CMSE NS call attributes.
  // CMSE is ARM Cortex-M only, not AArch64. Not a CIR target.

  sideEffect = cir::SideEffect::All;

  // Set noreturn from the function info (captures _Noreturn / [[noreturn]]
  // on the function type itself).
  if (info.isNoReturn()) {
    auto attr = cir::NoReturnAttr::get(&getMLIRContext());
    attrs.set(attr.getMnemonic(), attr);
  }

  // Add default function attributes based on language options.
  const LangOptions &langOpts = getLangOpts();

  if (langOpts.assumeFunctionsAreConvergent()) {
    // Conservatively, mark all functions and calls in CUDA and OpenCL as
    // convergent (meaning, they may call an intrinsically convergent op, such
    // as __syncthreads() / barrier(), and so can't have certain optimizations
    // applied around them). LLVM will remove this attribute where it safely
    // can.
    auto convgt = cir::ConvergentAttr::get(&getMLIRContext());
    attrs.set(convgt.getMnemonic(), convgt);
  }

  if (langOpts.OpenCL ||
      (langOpts.CUDA && langOpts.CUDAIsDevice) || langOpts.SYCLIsDevice) {
    auto noThrow = cir::NoThrowAttr::get(&getMLIRContext());
    attrs.set(noThrow.getMnemonic(), noThrow);
  }

  addAttributesFromFunctionProtoType(getBuilder(), attrs,
                                     calleeInfo.getCalleeFunctionProtoType());

  const Decl *targetDecl = calleeInfo.getCalleeDecl().getDecl();

  // Propagate OpenMP assumption attributes (OGCG CGCall.cpp:2426-2428).
  // [[omp::assume(...)]] works even without -fopenmp.
  if (targetDecl) {
    llvm::SmallVector<llvm::StringRef, 4> assumptions;
    for (const auto *aa : targetDecl->specific_attrs<OMPAssumeAttr>())
      aa->getAssumption().split(assumptions, ",");
    if (!assumptions.empty())
      attrs.set("llvm.assume",
                getStringAttrCached(llvm::join(assumptions, ",")));
  }

  bool hasOptnone = false;
  const NoBuiltinAttr *nba = nullptr;

  if (targetDecl) {
    if (targetDecl->hasAttr<ReturnsTwiceAttr>()) {
      auto attr = cir::ReturnsTwiceAttr::get(&getMLIRContext());
      attrs.set(attr.getMnemonic(), attr);
    }
    if (targetDecl->hasAttr<NoThrowAttr>()) {
      auto attr = cir::NoThrowAttr::get(&getMLIRContext());
      attrs.set(attr.getMnemonic(), attr);
    }

    if (targetDecl->hasAttr<NoReturnAttr>()) {
      auto attr = cir::NoReturnAttr::get(&getMLIRContext());
      attrs.set(attr.getMnemonic(), attr);
    }

    if (const FunctionDecl *func = dyn_cast<FunctionDecl>(targetDecl)) {
      addAttributesFromFunctionProtoType(
          getBuilder(), attrs, func->getType()->getAs<FunctionProtoType>());
      // Extract NoBuiltinAttr from the callee FunctionDecl for use in
      // addNoBuiltinAttributes below (matches OGCG CGCall.cpp:2482).
      nba = func->getAttr<NoBuiltinAttr>();

      const auto *md = dyn_cast<CXXMethodDecl>(func);
      const bool isVirtualCall = md && md->isVirtual();
      // Don't use [[noreturn]], _Noreturn or [[no_builtin]] for a call to a
      // virtual function. These attributes are not inherited by overrides.
      if (!(attrOnCallSite && isVirtualCall)) {
        if (func->isNoReturn()) {
          auto attr = cir::NoReturnAttr::get(&getMLIRContext());
          attrs.set(attr.getMnemonic(), attr);
        }
      } else {
        nba = nullptr;
      }

      // OGCG adds NoAlias to operator new return values when
      // AssumeSaneOperatorNew is set (CGCall.cpp:2468-2473). This is a
      // call-site return attribute (not on the declaration).
      if (attrOnCallSite && func->isReplaceableGlobalAllocationFunction()) {
        auto kind = func->getDeclName().getCXXOverloadedOperator();
        if (getCodeGenOpts().AssumeSaneOperatorNew &&
            (kind == OO_New || kind == OO_Array_New))
          attrs.set("cir.noalias_return",
                    mlir::UnitAttr::get(&getMLIRContext()));
      }
    }

    // NoAlias for RestrictAttr (__attribute__((malloc))) is handled in
    // setFunctionAttributes via cir.noalias result attribute.
    // ReturnsNonNullAttr is handled in setFunctionAttributes.

    // OGCG handles Cold/Hot/NoDuplicate/Convergent/NoMerge attrs on the
    // target decl (CGCall.cpp:2449-2574).
    if (targetDecl->hasAttr<ColdAttr>())
      attrs.set("cold", mlir::UnitAttr::get(&getMLIRContext()));
    if (targetDecl->hasAttr<HotAttr>()) {
      auto attr = cir::HotAttr::get(&getMLIRContext());
      attrs.set(attr.getMnemonic(), attr);
    }
    if (targetDecl->hasAttr<NoDuplicateAttr>())
      attrs.set("noduplicate", mlir::UnitAttr::get(&getMLIRContext()));
    if (targetDecl->hasAttr<ConvergentAttr>()) {
      auto attr = cir::ConvergentAttr::get(&getMLIRContext());
      attrs.set(attr.getMnemonic(), attr);
    }
    // Only place nomerge attribute on call sites, never on function
    // definitions. This allows it to work on indirect virtual function calls.
    if (attrOnCallSite && targetDecl->hasAttr<NoMergeAttr>())
      attrs.set("nomerge", mlir::UnitAttr::get(&getMLIRContext()));

    // OGCG sets up AddPotentialArgAccess lambda and MemAttrForPtrArgs here
    // (CGCall.cpp:2436-2444) for memory-effects tracking when ABI lowering
    // introduces indirect/inalloca argument accesses. CIR defers this to the
    // ABI lowering pass along with all per-parameter ABI attributes.
    assert(!cir::MissingFeatures::callAttrABIArgLowering());

    // 'const', 'pure' and 'noalias' attributed functions are also nounwind.
    if (targetDecl->hasAttr<ConstAttr>()) {
      sideEffect = cir::SideEffect::Const;
      auto noThrow = cir::NoThrowAttr::get(&getMLIRContext());
      attrs.set(noThrow.getMnemonic(), noThrow);
      // gcc specifies that 'const' functions have greater restrictions than
      // 'pure' functions, so they also cannot have infinite loops.
      // willreturn is handled during lowering via the side_effect attribute.
    } else if (targetDecl->hasAttr<PureAttr>()) {
      sideEffect = cir::SideEffect::Pure;
      auto noThrow = cir::NoThrowAttr::get(&getMLIRContext());
      attrs.set(noThrow.getMnemonic(), noThrow);
      // gcc specifies that 'pure' functions cannot have infinite loops.
      // willreturn is handled during lowering via the side_effect attribute.
    } else if (targetDecl->hasAttr<NoAliasAttr>()) {
      auto noThrow = cir::NoThrowAttr::get(&getMLIRContext());
      attrs.set(noThrow.getMnemonic(), noThrow);
    }

    if (targetDecl->hasAttr<DeviceKernelAttr>() &&
        DeviceKernelAttr::isOpenCLSpelling(
            targetDecl->getAttr<DeviceKernelAttr>())) {
      // Check callingConv to distinguish kernel entry points from stubs.
      // OGCG CGCall.cpp:2537-2538 checks CallingConv != CC_C (LLVM CC 0).
      // OGCG also checks CallingConv != CC_SpirFunction but compares a
      // clang enum value against an LLVM CC, so that check never matches
      // (clang CC_SpirFunction=12 vs LLVM SPIR_FUNC=75). We replicate
      // OGCG's actual behavior: only exclude CC_C stubs.
      if (callingConv != cir::CallingConv::C) {
        auto cirKernelAttr = cir::OpenCLKernelAttr::get(&getMLIRContext());
        attrs.set(cirKernelAttr.getMnemonic(), cirKernelAttr);

        auto uniformAttr =
            cir::OpenCLKernelUniformWorkGroupSizeAttr::get(&getMLIRContext());
        if (getLangOpts().OpenCLVersion <= 120) {
          // OpenCL v1.2 Work groups are always uniform
          attrs.set(uniformAttr.getMnemonic(), uniformAttr);
        } else {
          // OpenCL v2.0 Work groups may be whether uniform or not.
          // '-cl-uniform-work-group-size' compile option gets a hint
          // to the compiler that the global work-size be a multiple of
          // the work-group size specified to clEnqueueNDRangeKernel
          // (i.e. work groups are uniform).
          if (getLangOpts().OffloadUniformBlock) {
            attrs.set(uniformAttr.getMnemonic(), uniformAttr);
          }
        }
      }
    }

    if (!attrOnCallSite && targetDecl->hasAttr<ClspvLibclcBuiltinAttr>())
      attrs.set("clspv_libclc_builtin",
                mlir::UnitAttr::get(&getMLIRContext()));

    if (!attrOnCallSite) {
      if (const auto *callbackAttr = targetDecl->getAttr<CallbackAttr>()) {
        mlir::Builder builder(&getMLIRContext());
        SmallVector<mlir::Attribute, 4> callbackEncoding;
        for (int encodedIndex : callbackAttr->encoding()) {
          callbackEncoding.push_back(
              builder.getI64IntegerAttr(static_cast<int64_t>(encodedIndex)));
        }
        attrs.set("callback",
                  mlir::ArrayAttr::get(&getMLIRContext(), callbackEncoding));
      }
    }

    if (langOpts.CUDA && !langOpts.CUDAIsDevice &&
        targetDecl->hasAttr<CUDAGlobalAttr>()) {
      GlobalDecl kernel(calleeInfo.getCalleeDecl());
      llvm::StringRef kernelName = getMangledName(
          kernel.getWithKernelReferenceKind(KernelReferenceKind::Kernel));
      auto attr =
          cir::CUDAKernelNameAttr::get(&getMLIRContext(), kernelName.str());
      attrs.set(attr.getMnemonic(), attr);
    }

    // OGCG handles many target-specific attributes here (CmseNSCall,
    // AnyX86NoCfCheck, Leaf, BPFFastCall,
    // ArmLocallyStreaming, SpeculativeLoadHardening, NoSplitStack, etc.)
    assert(!cir::MissingFeatures::callAttrTargetSpecific());

    // no_caller_saved_registers from __attribute__((no_caller_saved_registers)).
    // Matches OGCG CGCall.cpp:2517-2518.
    if (targetDecl->hasAttr<AnyX86NoCallerSavedRegistersAttr>())
      attrs.set("no_caller_saved_registers",
                mlir::UnitAttr::get(&getMLIRContext()));

    // nocallback from __attribute__((leaf)).
    // Matches OGCG CGCall.cpp:2521-2522.
    if (targetDecl->hasAttr<LeafAttr>())
      attrs.set("nocallback", mlir::UnitAttr::get(&getMLIRContext()));

    // aarch64_pstate_sm_body from __arm_locally_streaming.
    // Matches OGCG CGCall.cpp:2560-2561.
    if (targetDecl->hasAttr<ArmLocallyStreamingAttr>())
      attrs.set("aarch64_pstate_sm_body",
                mlir::UnitAttr::get(&getMLIRContext()));

    // Attach modular-format metadata when present, matching OGCG
    // ConstructAttributeList (CGCall.cpp:2563-2573).
    if (auto *modularFormat = targetDecl->getAttr<ModularFormatAttr>()) {
      const auto *format = targetDecl->getAttr<FormatAttr>();
      assert(format && "modular_format attribute expects format metadata");
      StringRef type = format->getType()->getName();
      std::string formatIdx = std::to_string(format->getFormatIdx());
      std::string firstArg = std::to_string(format->getFirstArg());
      SmallVector<StringRef> args = {
          type, formatIdx, firstArg, modularFormat->getModularImplFn()->getName(),
          modularFormat->getImplName()};
      llvm::append_range(args, modularFormat->aspects());
      attrs.set("modular-format",
                getStringAttrCached(llvm::join(args, ",")));
    }

    // Add allocsize attribute if the function has AllocSizeAttr.
    // Encodes as: uint64(ElemSizeParamIndex) << 32 | NumElemsParamIndex
    // where NumElemsParamIndex is UINT32_MAX when not present.
    // This matches OGCG's ConstructAttributeList (CGCall.cpp).
    if (auto *allocSizeAttr = targetDecl->getAttr<AllocSizeAttr>()) {
      unsigned elemSizeArg = allocSizeAttr->getElemSizeParam().getLLVMIndex();
      uint64_t rawRepr = (uint64_t(elemSizeArg) << 32) | 0xFFFFFFFFu;
      if (allocSizeAttr->getNumElemsParam().isValid()) {
        rawRepr = (uint64_t(elemSizeArg) << 32) |
                  allocSizeAttr->getNumElemsParam().getLLVMIndex();
      }
      attrs.set("allocsize", getStringAttrCached(std::to_string(rawRepr)));
    }

    hasOptnone = targetDecl->hasAttr<OptimizeNoneAttr>();

    // __attribute__((error("msg"))) / __attribute__((warning("msg")))
    // Emit dontcall-error/dontcall-warn on the function declaration.
    // Matches OGCG CodeGenModule.cpp:3304-3309.
    if (const auto *ea = targetDecl->getAttr<ErrorAttr>()) {
      auto attr = cir::DontCallAttr::get(
          getStringAttrCached(ea->getUserDiagnostic()),
          ea->isError());
      attrs.set(attr.getMnemonic(), attr);
    }
  }

  if (!attrOnCallSite) {
    // OGCG adds "cmse_nonsecure_entry" for CmseNSEntryAttr on function
    // definitions (CGCall.cpp:2636-2637). CMSE is for ARM Cortex-M (32-bit)
    // embedded processors, not AArch64. CIR targets arm64 (AArch64).
    // These functions require the returns_twice attribute for correct codegen,
    // but the attribute may not be added if -fno-builtin is specified. We
    // explicitly add that attribute here (CGCall.cpp:2663-2670).
    {
      static const llvm::StringSet<> ReturnsTwiceFn{
          "_setjmpex", "setjmp",      "_setjmp",      "vfork",
          "sigsetjmp", "__sigsetjmp", "savectx", "getcontext"};
      if (ReturnsTwiceFn.contains(name))
        attrs.set("returns_twice", mlir::UnitAttr::get(&getMLIRContext()));
    }
    // OGCG handles DisableTailCallsAttr, AnyX86InterruptAttr, and
    // -fdisable-tail-calls to set the "disable-tail-calls" attr
    // (CGCall.cpp:2640-2661).
    {
      auto shouldDisableTailCalls = [&] {
        if (getCodeGenOpts().DisableTailCalls)
          return true;
        if (!targetDecl)
          return false;
        if (targetDecl->hasAttr<DisableTailCallsAttr>() ||
            targetDecl->hasAttr<AnyX86InterruptAttr>())
          return true;
        if (getCodeGenOpts().NoEscapingBlockTailCalls) {
          if (const auto *bd = dyn_cast<BlockDecl>(targetDecl))
            if (!bd->doesNotEscape())
              return true;
        }
        return false;
      };
      if (shouldDisableTailCalls())
        attrs.set("disable-tail-calls",
                  getStringAttrCached("true"));
    }
    // CPU/feature target attributes on function declarations/definitions.
    // Matches OGCG's GetCPUAndFeaturesAttributes (CGCall.cpp:2672-2674).
    {
      llvm::StringRef targetCPU = getTarget().getTargetOpts().CPU;
      llvm::StringRef tuneCPU = getTarget().getTargetOpts().TuneCPU;
      std::vector<std::string> features;

      const FunctionDecl *fd = nullptr;
      if (targetDecl)
        fd = dyn_cast<FunctionDecl>(targetDecl);
      fd = fd ? fd->getMostRecentDecl() : nullptr;
      const auto *td = fd ? fd->getAttr<TargetAttr>() : nullptr;
      const auto *tv = fd ? fd->getAttr<TargetVersionAttr>() : nullptr;
      const auto *sd = fd ? fd->getAttr<CPUSpecificAttr>() : nullptr;
      const auto *tc = fd ? fd->getAttr<TargetClonesAttr>() : nullptr;

      if (td || tv || sd || tc) {
        llvm::StringMap<bool> featureMap;
        GlobalDecl featureGD = calleeInfo.getCalleeDecl().getWithDecl(fd);
        astContext.getFunctionFeatureMap(featureMap, featureGD);
        if (td) {
          ParsedTargetAttr parsedAttr =
              getTarget().parseTargetAttr(td->getFeaturesStr());
          if (!parsedAttr.CPU.empty() &&
              getTarget().isValidCPUName(parsedAttr.CPU)) {
            targetCPU = parsedAttr.CPU;
            tuneCPU = "";
          }
          if (!parsedAttr.Tune.empty() &&
              getTarget().isValidCPUName(parsedAttr.Tune))
            tuneCPU = parsedAttr.Tune;
        }
        if (sd)
          tuneCPU = sd->getCPUName(featureGD.getMultiVersionIndex())->getName();
        for (const auto &entry : featureMap)
          features.push_back((entry.getValue() ? "+" : "-") +
                             entry.getKey().str());

        if (getTarget().getTriple().isAArch64()) {
          llvm::SmallVector<StringRef, 8> feats;
          bool isDefault = false;
          if (tv) {
            isDefault = tv->isDefaultVersion();
            tv->getFeatures(feats);
          } else if (tc) {
            isDefault = tc->isDefaultVersion(featureGD.getMultiVersionIndex());
            tc->getFeatures(feats, featureGD.getMultiVersionIndex());
          }
          if (isDefault) {
            attrs.set("fmv-features", mlir::UnitAttr::get(&getMLIRContext()));
          } else if (!feats.empty()) {
            std::set<StringRef> orderedFeats(feats.begin(), feats.end());
            std::string fmvFeatures;
            for (StringRef feat : orderedFeats)
              fmvFeatures.append("," + feat.str());
            attrs.set("fmv-features",
                      getStringAttrCached(fmvFeatures.substr(1)));
          }
        }
      } else {
        features = getTarget().getTargetOpts().Features;
      }

      if (!targetCPU.empty())
        attrs.set("target-cpu", getStringAttrCached(targetCPU));
      if (!tuneCPU.empty())
        attrs.set("tune-cpu", getStringAttrCached(tuneCPU));
      if (!features.empty()) {
        // In the common case (no per-function target attributes) the feature
        // set is module-global, so the filter+sort+join (O(F log F) over
        // 100-200 features) yields the identical string for every function.
        // Compute it once and reuse.
        bool commonCase = !(td || tv || sd || tc);
        mlir::StringAttr featuresAttr;
        if (commonCase && cachedDefaultTargetFeaturesComputed) {
          featuresAttr = cachedDefaultTargetFeaturesAttr;
        } else {
          llvm::erase_if(features, [&](const std::string &f) {
            return getTarget().isReadOnlyFeature(f.substr(1));
          });
          llvm::sort(features);
          featuresAttr = getStringAttrCached(llvm::join(features, ","));
          if (commonCase) {
            cachedDefaultTargetFeaturesComputed = true;
            cachedDefaultTargetFeaturesAttr = featuresAttr;
          }
        }
        attrs.set("target-features", featuresAttr);
      }
    }
    // OGCG adds "marked_for_windows_hot_patching" for functions in the
    // MSHotPatchFunctions list (CGCall.cpp:2677-2681). Windows is not a
    // supported CIR target.
  }

  // Add no-builtin attributes. Matches OGCG's addNoBuiltinAttributes
  // (CGCall.cpp:2237-2271, called at 2583).
  {
    auto *ctx = &getMLIRContext();
    // -fno-builtin disables all builtins.
    if (langOpts.NoBuiltin) {
      attrs.set("no-builtins", mlir::UnitAttr::get(ctx));
    } else {
      // -fno-builtin-<name> disables specific builtins.
      for (const auto &builtinName : langOpts.NoBuiltinFuncs)
        attrs.set(("no-builtin-" + builtinName), mlir::UnitAttr::get(ctx));
    }
    // Per-function __attribute__((no_builtin(...))) on the callee.
    if (nba) {
      if (llvm::is_contained(nba->builtinNames(), "*")) {
        attrs.set("no-builtins", mlir::UnitAttr::get(ctx));
      } else {
        for (const auto &builtinName : nba->builtinNames())
          attrs.set(("no-builtin-" + builtinName.str()),
                    mlir::UnitAttr::get(ctx));
      }
    }
  }

  // Apply CodeGenOpts-driven default function attributes (frame-pointer,
  // stack-protector, no-trapping-math, null-pointer-is-valid, etc.).
  // Matches OGCG's getDefaultFunctionAttributes() call in ConstructAttributeList.
  getDefaultFunctionAttributes(name, hasOptnone, attrOnCallSite, attrs);

  // Per-function attribute overrides on the target decl.
  // Matches OGCG CGCall.cpp:2590-2621.
  if (targetDecl) {
    // Per-function __attribute__((zero_call_used_regs(...))) overrides the
    // command-line -fzero-call-used-regs flag (CGCall.cpp:2597-2604).
    if (targetDecl->hasAttr<ZeroCallUsedRegsAttr>()) {
      auto kind =
          targetDecl->getAttr<ZeroCallUsedRegsAttr>()->getZeroCallUsedRegs();
      // Remove the global default first, then set the per-function value.
      attrs.erase(getStringAttrCached("zero-call-used-regs"));
      attrs.set(
          "zero-call-used-regs",
          getStringAttrCached(
              ZeroCallUsedRegsAttr::ConvertZeroCallUsedRegsKindToStr(kind)));
    }
  }

  // OGCG overrides default function attributes based on decl-specific
  // attributes like NoSpeculativeLoadHardening, SpeculativeLoadHardening,
  // and NoSplitStack (CGCall.cpp:2590-2621).
  if (targetDecl) {
    if (targetDecl->hasAttr<NoSpeculativeLoadHardeningAttr>())
      attrs.erase(getStringAttrCached("speculative-load-hardening"));
    if (targetDecl->hasAttr<SpeculativeLoadHardeningAttr>())
      attrs.set("speculative-load-hardening",
                mlir::UnitAttr::get(&getMLIRContext()));
    if (targetDecl->hasAttr<NoSplitStackAttr>())
      attrs.erase(getStringAttrCached("split-stack"));
  }

  // Add NonLazyBind attribute to function declarations when -fno-plt is
  // used. Matches OGCG CGCall.cpp:2607-2617.
  if (getCodeGenOpts().NoPLT) {
    if (auto *fn = dyn_cast_or_null<FunctionDecl>(targetDecl)) {
      if (!fn->isDefined() && !attrOnCallSite) {
        attrs.set("nonlazybind",
                  mlir::UnitAttr::get(&getMLIRContext()));
      }
    }
  }

  // OGCG removes the Convergent attribute when NoConvergentAttr is present
  // on the decl (CGCall.cpp:2619). This is used in OpenCL/CUDA where all
  // functions are convergent by default, but specific functions can opt out.
  if (targetDecl && targetDecl->hasAttr<NoConvergentAttr>()) {
    auto convgt = cir::ConvergentAttr::get(&getMLIRContext());
    attrs.erase(convgt.getMnemonic());
  }

  // OGCG adds "sample-profile-suffix-elision-policy" for internal linkage
  // functions with -funique-internal-linkage-names (CGCall.cpp:2625-2631).
  if (targetDecl && getCodeGenOpts().UniqueInternalLinkageNames) {
    if (const auto *fn = dyn_cast<FunctionDecl>(targetDecl)) {
      if (!fn->isExternallyVisible())
        attrs.set("sample-profile-suffix-elision-policy",
                  getStringAttrCached("selected"));
    }
  }

  // Mark functions that are replaceable by the loader. Matches OGCG
  // CGCall.cpp:2685-2686.
  if (getCodeGenOpts().isLoaderReplaceableFunctionName(name))
    attrs.set("loader-replaceable", mlir::UnitAttr::get(&getMLIRContext()));

  // OGCG processes ABIArgInfo for all parameters and the return value here,
  // adding sret, byval, inreg, sext/zext, noundef, nofpclass,
  // dereferenceable, alignment, noalias, dead_on_return, etc.
  // CIR defers ABI lowering to a later pass.
  assert(!cir::MissingFeatures::callAttrABIArgLowering());

  // OGCG calls getDefaultFunctionAttributes (CGCall.cpp:2586) for default
  // function attributes (frame-pointer, stack-protector, etc.) and then
  // processes sanitizer attributes. CIR handles default attrs through its
  // own mechanism and defers sanitizers.
  (void)0; // Sanitizer-specific CIR parity work remains NYI in this path.
  // Note: side_effect is returned via output parameter and set as a property
  // on CallOp, not as part of extra_attrs.
}

/// Returns the canonical formal type of the given C++ method.
static CanQual<FunctionProtoType> getFormalType(const CXXMethodDecl *md) {
  return md->getType()
      ->getCanonicalTypeUnqualified()
      .getAs<FunctionProtoType>();
}

/// Adds the formal parameters in FPT to the given prefix. If any parameter in
/// FPT has pass_object_size_attrs, then we'll add parameters for those, too.
/// TODO(cir): this should be shared with LLVM codegen
static void appendParameterTypes(const CIRGenTypes &cgt,
                                 SmallVectorImpl<CanQualType> &prefix,
                                 CanQual<FunctionProtoType> fpt) {
  // Fast path: don't touch param info if we don't need to.
  if (!fpt->hasExtParameterInfos()) {
    prefix.append(fpt->param_type_begin(), fpt->param_type_end());
    return;
  }

  // In the vast majority of cases, we'll have precisely fpt->getNumParams()
  // parameters; the only thing that can change this is the presence of
  // pass_object_size. So, we preallocate for the common case.
  prefix.reserve(prefix.size() + fpt->getNumParams());

  auto extInfos = fpt->getExtParameterInfos();
  assert(extInfos.size() == fpt->getNumParams());
  for (unsigned i = 0, e = fpt->getNumParams(); i != e; ++i) {
    prefix.push_back(fpt->getParamType(i));
    if (extInfos[i].hasPassObjectSize())
      prefix.push_back(cgt.getASTContext().getCanonicalSizeType());
  }
}

const CIRGenFunctionInfo &
CIRGenTypes::arrangeCXXStructorDeclaration(GlobalDecl gd) {
  auto *md = cast<CXXMethodDecl>(gd.getDecl());

  llvm::SmallVector<CanQualType, 16> argTypes;
  argTypes.push_back(deriveThisType(theCXXABI.getThisArgumentTypeForMethod(gd),
                                    md));

  bool passParams = true;

  if (auto *cd = dyn_cast<CXXConstructorDecl>(md)) {
    // A base class inheriting constructor doesn't get forwarded arguments
    // needed to construct a virtual base (or base class thereof)
    if (auto inherited = cd->getInheritedConstructor())
      passParams = inheritingCtorHasParams(inherited, gd.getCtorType());
  }

  CanQual<FunctionProtoType> fpt = getFormalType(md);

  if (passParams)
    appendParameterTypes(*this, argTypes, fpt);

  // The structor signature may include implicit parameters.
  [[maybe_unused]] CIRGenCXXABI::AddedStructorArgCounts addedArgs =
      theCXXABI.buildStructorSignature(gd, argTypes);

  RequiredArgs required =
      (passParams && md->isVariadic() ? RequiredArgs(argTypes.size())
                                      : RequiredArgs::All);

  CanQualType resultType = theCXXABI.hasThisReturn(gd) ? argTypes.front()
                           : theCXXABI.hasMostDerivedReturn(gd)
                               ? astContext.VoidPtrTy
                               : astContext.VoidTy;

  return arrangeCIRFunctionInfo(resultType, FnInfoOpts::IsInstanceMethod,
                                argTypes, required, fpt->getExtInfo());
}

/// Derives the 'this' type for CIRGen purposes, i.e. ignoring method CVR
/// qualification. Either or both of `rd` and `md` may be null. A null `rd`
/// indicates that there is no meaningful 'this' type, and a null `md` can occur
/// when calling a method pointer.
CanQualType CIRGenTypes::deriveThisType(const CXXRecordDecl *rd,
                                        const CXXMethodDecl *md) {
  CanQualType recTy;
  if (rd) {
    recTy = getASTContext().getCanonicalTagType(rd);
  } else {
    // This can happen with the MS ABI when there is no record decl.
    // MS ABI is Windows-only; CIR uses Itanium ABI.
    cgm.errorNYI("deriveThisType with no record decl (MS ABI)");
    return {};
  }

  if (md)
    recTy = CanQualType::CreateUnsafe(getASTContext().getAddrSpaceQualType(
        recTy, md->getMethodQualifiers().getAddressSpace()));
  return getASTContext().getPointerType(recTy);
}

/// Arrange the CIR function layout for a value of the given function type, on
/// top of any implicit parameters already stored.
static const CIRGenFunctionInfo &
arrangeCIRFunctionInfo(CIRGenTypes &cgt, bool instanceMethod,
                       SmallVectorImpl<CanQualType> &prefix,
                       CanQual<FunctionProtoType> fpt) {
  RequiredArgs required =
      RequiredArgs::getFromProtoWithExtraSlots(fpt, prefix.size());
  appendParameterTypes(cgt, prefix, fpt);
  CanQualType resultType = fpt->getReturnType().getUnqualifiedType();
  FnInfoOpts opts =
      instanceMethod ? FnInfoOpts::IsInstanceMethod : FnInfoOpts::None;
  return cgt.arrangeCIRFunctionInfo(resultType, opts, prefix, required,
                                    fpt->getExtInfo());
}

void CIRGenFunction::emitDelegateCallArg(CallArgList &args,
                                         const VarDecl *param,
                                         SourceLocation loc) {
  // StartFunction converted the ABI-lowered parameter(s) into a local alloca.
  // We need to turn that into an r-value suitable for emitCall
  Address local = getAddrOfLocalVar(param);

  QualType type = param->getType();

  // OGCG handles inalloca argument passing here. inalloca is x86-32 MSVC
  // ABI only, not applicable to CIR targets.

  // GetAddrOfLocalVar returns a pointer-to-pointer for references, but the
  // argument needs to be the original pointer.
  if (type->isReferenceType()) {
    args.add(
        RValue::get(builder.createLoad(getLoc(param->getSourceRange()), local)),
        type);
  } else if (getLangOpts().ObjCAutoRefCount &&
             param->hasAttr<NSConsumedAttr>() && type->isObjCRetainableType()) {
    mlir::Location paramLoc = getLoc(param->getSourceRange());
    mlir::Value ptr = builder.createLoad(paramLoc, local);
    builder.createStore(paramLoc,
                        builder.getNullValue(local.getElementType(), paramLoc),
                        local);
    args.add(RValue::get(ptr), type);
  } else {
    // Match OGCG: only consumed retainable ARC params need move-out handling.
    // Other delegate-call args are loaded normally from the local alloca.
    args.add(convertTempToRValue(local, type, loc), type);
  }

  // Deactivate the cleanup for the callee-destructed param that was pushed.
  if (type->isRecordType() && !curFuncIsThunk &&
      type->castAsRecordDecl()->isParamDestroyedInCallee() &&
      param->needsDestruction(getContext())) {
    auto *parmVar = cast<ParmVarDecl>(param);
    EHScopeStack::stable_iterator cleanup =
        calleeDestructedParamCleanups.lookup(parmVar);
    assert(cleanup.isValid() &&
           "cleanup for callee-destructed param not recorded");
    args.addArgCleanupDeactivation(cleanup, nullptr);
  }
}

static const CIRGenFunctionInfo &
arrangeFreeFunctionLikeCall(CIRGenTypes &cgt, CIRGenModule &cgm,
                            const CallArgList &args,
                            const FunctionType *fnType) {

  RequiredArgs required = RequiredArgs::All;

  if (const auto *proto = dyn_cast<FunctionProtoType>(fnType)) {
    if (proto->isVariadic())
      required = RequiredArgs::getFromProtoWithExtraSlots(proto, 0);
  } else if (cgm.getTargetCIRGenInfo().isNoProtoCallVariadic(
                 cast<FunctionNoProtoType>(fnType))) {
    required = RequiredArgs(args.size());
  }

  SmallVector<CanQualType, 16> argTypes;
  for (const CallArg &arg : args)
    argTypes.push_back(cgt.getASTContext().getCanonicalParamType(arg.ty));

  CanQualType retType = fnType->getReturnType()->getCanonicalTypeUnqualified();

  return cgt.arrangeCIRFunctionInfo(retType, FnInfoOpts::None, argTypes,
                                    required, fnType->getExtInfo());
}

/// Arrange a call to a C++ method, passing the given arguments.
///
/// extraPrefixArgs is the number of ABI-specific args passed after the `this`
/// parameter.
/// passProtoArgs indicates whether `args` has args for the parameters in the
/// given CXXConstructorDecl.
const CIRGenFunctionInfo &CIRGenTypes::arrangeCXXConstructorCall(
    const CallArgList &args, const CXXConstructorDecl *d, CXXCtorType ctorKind,
    unsigned extraPrefixArgs, unsigned extraSuffixArgs, bool passProtoArgs) {

  // FIXME: Kill copy.
  llvm::SmallVector<CanQualType, 16> argTypes;
  for (const auto &arg : args)
    argTypes.push_back(astContext.getCanonicalParamType(arg.ty));

  // +1 for implicit this, which should always be args[0]
  unsigned totalPrefixArgs = 1 + extraPrefixArgs;

  CanQual<FunctionProtoType> fpt = getFormalType(d);
  RequiredArgs required = passProtoArgs
                              ? RequiredArgs::getFromProtoWithExtraSlots(
                                    fpt, totalPrefixArgs + extraSuffixArgs)
                              : RequiredArgs::All;

  GlobalDecl gd(d, ctorKind);
  CanQualType resultType = theCXXABI.hasThisReturn(gd)   ? argTypes.front()
                           : theCXXABI.hasMostDerivedReturn(gd)
                               ? astContext.VoidPtrTy
                               : astContext.VoidTy;

  return arrangeCIRFunctionInfo(resultType, FnInfoOpts::IsInstanceMethod,
                                argTypes, required, fpt->getExtInfo());
}

/// Arrange a call to a C++ method, passing the given arguments.
///
/// numPrefixArgs is the number of the ABI-specific prefix arguments we have. It
/// does not count `this`.
const CIRGenFunctionInfo &CIRGenTypes::arrangeCXXMethodCall(
    const CallArgList &args, const FunctionProtoType *proto,
    RequiredArgs required, unsigned numPrefixArgs) {
  assert(numPrefixArgs + 1 <= args.size() &&
         "Emitting a call with less args than the required prefix?");

  // FIXME: Kill copy.
  llvm::SmallVector<CanQualType, 16> argTypes;
  for (const CallArg &arg : args)
    argTypes.push_back(astContext.getCanonicalParamType(arg.ty));

  return arrangeCIRFunctionInfo(
      proto->getReturnType()->getCanonicalTypeUnqualified(),
      FnInfoOpts::IsInstanceMethod, argTypes, required, proto->getExtInfo());
}

const CIRGenFunctionInfo &
CIRGenTypes::arrangeFreeFunctionCall(const CallArgList &args,
                                     const FunctionType *fnType) {
  return arrangeFreeFunctionLikeCall(*this, cgm, args, fnType);
}

const CIRGenFunctionInfo &
CIRGenTypes::arrangeBlockFunctionCall(const CallArgList &args,
                                      const FunctionType *fnType) {
  RequiredArgs required = RequiredArgs::All;
  if (const auto *proto = dyn_cast<FunctionProtoType>(fnType)) {
    if (proto->isVariadic())
      required = RequiredArgs::getFromProtoWithExtraSlots(proto, 1);
  }

  llvm::SmallVector<CanQualType, 16> argTypes;
  for (const CallArg &arg : args)
    argTypes.push_back(astContext.getCanonicalParamType(arg.ty));

  return arrangeCIRFunctionInfo(
      fnType->getReturnType()->getCanonicalTypeUnqualified(),
      FnInfoOpts::None, argTypes, required, fnType->getExtInfo());
}

/// Arrange the argument and result information for a declaration or definition
/// of the given C++ non-static member function. The member function must be an
/// ordinary function, i.e. not a constructor or destructor.
const CIRGenFunctionInfo &
CIRGenTypes::arrangeCXXMethodDeclaration(const CXXMethodDecl *md) {
  assert(!isa<CXXConstructorDecl>(md) && "wrong method for constructors!");
  assert(!isa<CXXDestructorDecl>(md) && "wrong method for destructors!");

  CanQualType ft = getFormalType(md).getAs<Type>();
  setCUDAKernelCallingConvention(ft, cgm, md);
  auto prototype = ft.getAs<FunctionProtoType>();

  if (md->isImplicitObjectMemberFunction()) {
    // The abstract case is perfectly fine.
    auto *thisType = theCXXABI.getThisArgumentTypeForMethod(md);
    return arrangeCXXMethodType(thisType, prototype.getTypePtr(), md);
  }

  return arrangeFreeFunctionType(prototype);
}

/// Arrange the argument and result information for a call to an unknown C++
/// non-static member function of the given abstract type. (A null RD means we
/// don't have any meaningful "this" argument type, so fall back to a generic
/// pointer type). The member fucntion must be an ordinary function, i.e. not a
/// constructor or destructor.
const CIRGenFunctionInfo &
CIRGenTypes::arrangeCXXMethodType(const CXXRecordDecl *rd,
                                  const FunctionProtoType *fpt,
                                  const CXXMethodDecl *md) {
  llvm::SmallVector<CanQualType, 16> argTypes;

  // Add the 'this' pointer.
  argTypes.push_back(deriveThisType(rd, md));

  return ::arrangeCIRFunctionInfo(
      *this, /*instanceMethod=*/true, argTypes,
      fpt->getCanonicalTypeUnqualified().getAs<FunctionProtoType>());
}

/// Arrange the argument and result information for the declaration or
/// definition of the given function.
const CIRGenFunctionInfo &
CIRGenTypes::arrangeFunctionDeclaration(const GlobalDecl gd) {
  const auto *fd = cast<FunctionDecl>(gd.getDecl());

  if (const auto *md = dyn_cast<CXXMethodDecl>(fd))
    if (md->isImplicitObjectMemberFunction())
      return arrangeCXXMethodDeclaration(md);

  CanQualType funcTy = fd->getType()->getCanonicalTypeUnqualified();

  assert(isa<FunctionType>(funcTy));
  setCUDAKernelCallingConvention(funcTy, cgm, fd);

  // OpenCL kernel stubs use a target-dependent non-kernel CC (e.g. CC_C for
  // AMDGPU, CC_SpirFunction for SPIR/SPIRV).
  if (DeviceKernelAttr::isOpenCLSpelling(fd->getAttr<DeviceKernelAttr>()) &&
      gd.getKernelReferenceKind() == KernelReferenceKind::Stub) {
    const FunctionType *ft = funcTy->getAs<FunctionType>();
    cgm.getTargetCIRGenInfo().setOCLKernelStubCallingConvention(ft);
    funcTy = ft->getCanonicalTypeUnqualified();
  }

  // When declaring a function without a prototype, always use a non-variadic
  // type.
  if (CanQual<FunctionNoProtoType> noProto =
          funcTy.getAs<FunctionNoProtoType>()) {
    return arrangeCIRFunctionInfo(noProto->getReturnType(), FnInfoOpts::None,
                                  {}, RequiredArgs::All,
                                  noProto->getExtInfo());
  }

  return arrangeFreeFunctionType(funcTy.castAs<FunctionProtoType>());
}

/// When a call result is inside a synthetic try region, it cannot be directly
/// used outside that region. Instead, store the result to a temporary alloca
/// (inside the try region, right after the call) and load it from outside.
static RValue getRValueThroughMemory(mlir::Location loc,
                                     CIRGenBuilderTy &builder, mlir::Value val,
                                     Address addr) {
  auto ip = builder.saveInsertionPoint();
  builder.setInsertionPointAfterValue(val);
  builder.createStore(loc, val, addr);
  builder.restoreInsertionPoint(ip);
  auto load = builder.createLoad(loc, addr);
  return RValue::get(load);
}

static cir::CIRCallOpInterface
emitCallLikeOp(CIRGenFunction &cgf, mlir::Location callLoc,
               cir::FuncType indirectFuncTy, mlir::Value indirectFuncVal,
               cir::FuncOp directFuncOp,
               const SmallVectorImpl<mlir::Value> &cirCallArgs, bool isInvoke,
               cir::CallingConv callingConv, cir::SideEffect sideEffect,
               cir::ExtraFuncAttributesAttr extraFnAttrs) {
  CIRGenBuilderTy &builder = cgf.getBuilder();

  // Coerce pointer argument types to match the callee's parameter types.
  // CIR uses typed pointers, so a function may have been declared with
  // different pointer types than the arguments emitted here (e.g. char* vs
  // void*). LLVM IR never hits this because it uses opaque pointers.
  SmallVector<mlir::Value, 16> coercedArgs(cirCallArgs.begin(),
                                           cirCallArgs.end());
  if (directFuncOp) {
    auto funcTy = directFuncOp.getFunctionType();
    for (unsigned i = 0; i < funcTy.getNumInputs() && i < coercedArgs.size();
         i++) {
      if (coercedArgs[i].getType() == funcTy.getInput(i))
        continue;
      auto srcPtrTy =
          mlir::dyn_cast<cir::PointerType>(coercedArgs[i].getType());
      auto dstPtrTy =
          mlir::dyn_cast<cir::PointerType>(funcTy.getInput(i));
      if (srcPtrTy && dstPtrTy)
        coercedArgs[i] =
            builder.createBitcast(callLoc, coercedArgs[i], dstPtrTy);
    }
  }

  if (isInvoke) {
    // Inside a cir.cleanup.scope body, emit the throwing call as a bare
    // `cir.call exception` with an empty cleanup region. The enclosing
    // cir.cleanup.scope provides the single shared landing pad, wired up by
    // FlattenCFG's CIRCleanupScopeOpFlattening. This lets all N throwing
    // calls in the scope body share ONE cleanup dispatch instead of each
    // materializing the full active-destructor prefix (the O(N^2) source).
    // The scope's cleanup region is emitted separately by emitArrayInit using
    // only op-dominating values (cursor-relative recompute).
    if (cgf.emittingSharedCleanupScope) {
      cir::CallOp bareCall;
      if (indirectFuncTy)
        bareCall = builder.createIndirectTryCallOp(callLoc, indirectFuncVal,
                                                   indirectFuncTy, coercedArgs,
                                                   callingConv, sideEffect);
      else
        bareCall = builder.createTryCallOp(callLoc, directFuncOp, coercedArgs,
                                           callingConv, sideEffect);
      bareCall->setAttr("extra_attrs", extraFnAttrs);
      return bareCall;
    }

    // This call may throw and requires catch and/or cleanup handling.
    // If this call does not appear within the `try` region of an existing
    // TryOp, we must create a synthetic TryOp to contain the call.

    assert(cgf.curLexScope && "expected scope");
    cir::TryOp tryOp = cgf.curLexScope->getClosestTryParent();

    // If there's a Terminate scope on the EH stack (possibly behind Cleanup
    // scopes), don't reuse the enclosing user try-op. Create a nested
    // synthetic try with catch-all + terminate handler so the terminate
    // behavior is correctly scoped (e.g., during catch parameter init).
    // Without this, the terminate handler would replace the outer try-op's
    // unwind handler.
    if (tryOp && cgf.ehStack.requiresCatchOrCleanup()) {
      for (EHScopeStack::stable_iterator si =
               cgf.ehStack.getInnermostEHScope();
           si != cgf.ehStack.stable_end();
           si = cgf.ehStack.find(si)->getEnclosingEHScope()) {
        EHScope &s = *cgf.ehStack.find(si);
        if (s.getKind() == EHScope::Terminate) {
          tryOp = nullptr;
          break;
        }
        if (s.getKind() != EHScope::Cleanup)
          break;
      }
    }

    if (!tryOp) {
      auto currentScope = cgf.ehStack.getInnermostEHScope();

      // Check if we can reuse a shared synthetic TryOp at the same depth.
      // Calls at the same EH cleanup depth share a TryOp: the first call
      // gets full per-call cleanup emission; subsequent calls get empty
      // cleanup regions and share the first call's landing pad in FlattenCFG.
      if (cgf.activeSyntheticTry &&
          currentScope == cgf.activeSyntheticTryScope &&
          cgf.activeSyntheticTry->getBlock() ==
              builder.getInsertionBlock()) {
        // The TryOp must be in the same block as the current insertion
        // point. If we entered/exited a cir.scope or cir.ternary since
        // creating the TryOp, the blocks differ and we can't reuse it.
        //
        // Also verify all call operands dominate the TryOp. Values
        // defined after the TryOp in the parent block cannot be used
        // inside it.
        //
        // Finally, ensure no side-effecting operations (calls, stores)
        // have been emitted after the TryOp. Inserting a new call inside
        // the TryOp's try body would place it before those operations,
        // reordering program-visible side effects. This prevents the
        // miscompilation where `__str_ = String(s); init_ptrs();` was
        // reordered so that init_ptrs() ran before the assignment.
        bool allDominate = true;
        bool hasSideEffectsAfterTry = false;
        auto *tryBlock = cgf.activeSyntheticTry->getBlock();

        // A partial-array-cleanup cursor store: `cir.store <elt-ptr>,
        // %arrayinit.endOfInit`. emitArrayInit emits one between consecutive
        // element ctors to advance the destroy cursor.
        auto isPartialArrayCursorStore = [](mlir::Operation *op) -> bool {
          auto st = mlir::dyn_cast<cir::StoreOp>(op);
          if (!st)
            return false;
          auto *addrDef = st.getAddr().getDefiningOp();
          auto alloca = mlir::dyn_cast_or_null<cir::AllocaOp>(addrDef);
          return alloca && alloca.getName() == "arrayinit.endOfInit";
        };
        // The set of pure ops that were always safe to hoist above the TryOp.
        auto isBaseHoistablePure = [](mlir::Operation *op) -> bool {
          return mlir::isa<cir::AllocaOp, cir::GetMemberOp, cir::CastOp,
                           cir::ConstantOp, cir::LoadOp, cir::GetGlobalOp>(op);
        };

        // Detect the initializer-list array pattern among the ops emitted after
        // the TryOp: one or more partial-array cursor stores, and otherwise
        // only pure ops (the element-address ptr_stride/const arithmetic). ONLY
        // in this pattern do we relax the otherwise-strict reuse conditions
        // below — every other call shape keeps the original conservative
        // behavior, so its codegen is unchanged. This lets the N element ctors
        // of an initializer-list array share ONE partial-array landing pad
        // instead of each spawning its own synthetic try; the latter makes a
        // store-forwarding pass specialize each cleanup copy's endOfInit load
        // to a constant end pointer, which LLVM then unrolls to O(N^2) cleanup
        // code.
        bool sawCursorStore = false;
        bool onlyPureOrCursorAfterTry = true;
        for (auto it = std::next(cgf.activeSyntheticTry->getIterator()),
                  end = tryBlock->end();
             it != end; ++it) {
          if (isPartialArrayCursorStore(&*it)) {
            sawCursorStore = true;
            continue;
          }
          if (isBaseHoistablePure(&*it) || mlir::isa<cir::PtrStrideOp>(&*it))
            continue;
          onlyPureOrCursorAfterTry = false;
        }
        const bool arrayInitPattern =
            sawCursorStore && onlyPureOrCursorAfterTry;

        // In the array-init pattern, element-address pointer arithmetic
        // (PtrStrideOp) is also hoistable above the TryOp.
        auto isHoistablePure = [&](mlir::Operation *op) -> bool {
          return isBaseHoistablePure(op) ||
                 (arrayInitPattern && mlir::isa<cir::PtrStrideOp>(op));
        };

        // Does `v` dominate the TryOp — either already, or (array-init pattern
        // only) after the hoisting loop below moves it and, transitively, all
        // its operands above the TryOp? This mirrors exactly what that loop can
        // achieve, so we never claim a dominance the hoist won't establish.
        std::function<bool(mlir::Value)> dominatesTry =
            [&](mlir::Value v) -> bool {
          auto *defOp = v.getDefiningOp();
          if (!defOp)
            return true; // block argument
          if (defOp->getBlock() != tryBlock)
            return true; // defined in another block
          if (defOp->isBeforeInBlock(cgf.activeSyntheticTry))
            return true; // already before the TryOp
          // Defined after the TryOp. Outside the array-init pattern we keep the
          // original strict rule (no reuse).
          if (!arrayInitPattern || !isHoistablePure(defOp))
            return false;
          // A tmp.try.call.res load is stored inside the try body and is never
          // hoisted, so it cannot be made to dominate.
          if (auto loadOp = mlir::dyn_cast<cir::LoadOp>(defOp)) {
            auto *addrDef = loadOp.getAddr().getDefiningOp();
            if (auto a = mlir::dyn_cast_or_null<cir::AllocaOp>(addrDef);
                a && a.getName() == "tmp.try.call.res")
              return false;
          }
          for (mlir::Value operand : defOp->getOperands())
            if (!dominatesTry(operand))
              return false;
          return true;
        };
        for (auto arg : coercedArgs)
          if (!dominatesTry(arg))
            allDominate = false;
        if (indirectFuncVal && !dominatesTry(indirectFuncVal))
          allDominate = false;

        // Check for side-effecting ops after the TryOp. Pure ops (hoistable
        // above the try) are always safe; in the array-init pattern the
        // relocatable cursor store is too. Anything else would be reordered by
        // inserting the new call before it.
        for (auto it = std::next(cgf.activeSyntheticTry->getIterator()),
                  end = tryBlock->end();
             it != end; ++it) {
          if (isHoistablePure(&*it))
            continue;
          if (arrayInitPattern && isPartialArrayCursorStore(&*it))
            continue;
          hasSideEffectsAfterTry = true;
          break;
        }

        if (allDominate && !hasSideEffectsAfterTry) {
          // Same depth, operands dominate — insert into existing TryOp.
          tryOp = cgf.activeSyntheticTry;

          mlir::Block &tryBodyForReloc = tryOp.getTryRegion().back();
          mlir::Operation *relocTerm = tryBodyForReloc.getTerminator();

          // Hoist any pure ops (get_member, cast, const, alloca, load,
          // get_global) that were emitted between the TryOp and the
          // current insertion point to before the TryOp. This ensures
          // they dominate uses inside the try body. Without this, the
          // aggregate return store (emitted inside the try by emitCall)
          // could reference a destination address defined after the try,
          // causing a dominance violation.
          //
          // An op can only be hoisted if all of its operands already
          // dominate the TryOp (defined before it, or are block args).
          // In particular:
          //  - Loads from tmp.try.call.res must NOT be hoisted because
          //    the store happens inside the TryOp body.
          //  - Loads/get_member/cast ops whose operands come from a
          //    non-hoisted op (e.g. a tmp.try.call.res load result)
          //    also cannot be hoisted.
          auto operandsDominateTryOp = [&](mlir::Operation *op) {
            for (mlir::Value operand : op->getOperands()) {
              auto *defOp = operand.getDefiningOp();
              if (!defOp)
                continue; // Block arguments always dominate.
              if (defOp->getBlock() != tryBlock)
                continue; // Defined in a different block — dominates.
              if (!defOp->isBeforeInBlock(tryOp))
                return false; // Defined after (or at) the TryOp.
            }
            return true;
          };

          for (auto it = std::next(tryOp->getIterator()), end = tryBlock->end();
               it != end;) {
            mlir::Operation *op = &*it++;
            // Relocate a partial-array cursor store into the try body, right
            // before the terminator (hence before the new call inserted below),
            // so the shared partial-array cleanup observes the advanced end
            // pointer and program order relative to the element ctors is
            // preserved. Its value operand (the element ptr_stride) is hoisted
            // above the TryOp earlier in this same loop, so it dominates here.
            if (arrayInitPattern && isPartialArrayCursorStore(op)) {
              op->moveBefore(relocTerm);
              continue;
            }
            if (!isHoistablePure(op))
              continue;
            // Don't hoist loads from tmp.try.call.res allocas: those
            // temporaries are stored inside the TryOp's try body by
            // getRValueThroughMemory. Hoisting the load before the
            // TryOp would read uninitialized memory.
            if (auto loadOp = mlir::dyn_cast<cir::LoadOp>(op)) {
              auto *addrDef = loadOp.getAddr().getDefiningOp();
              if (auto allocaOp =
                      mlir::dyn_cast_or_null<cir::AllocaOp>(addrDef);
                  allocaOp && allocaOp.getName() == "tmp.try.call.res")
                continue;
            }
            // Only hoist if all operands already dominate the TryOp.
            if (!operandsDominateTryOp(op))
              continue;
            op->moveBefore(tryOp);
          }

          mlir::Block &tryBody = tryOp.getTryRegion().back();
          auto *term = tryBody.getTerminator();
          assert(mlir::isa<cir::YieldOp>(term));

          mlir::OpBuilder::InsertPoint ip = builder.saveInsertionPoint();
          builder.setInsertionPoint(term);

          cir::CallOp callOpWithExceptions;
          if (indirectFuncTy) {
            callOpWithExceptions = builder.createIndirectTryCallOp(
                callLoc, indirectFuncVal, indirectFuncTy, coercedArgs,
                callingConv, sideEffect);
          } else {
            callOpWithExceptions = builder.createTryCallOp(
                callLoc, directFuncOp, coercedArgs, callingConv, sideEffect);
          }
          callOpWithExceptions->setAttr("extra_attrs", extraFnAttrs);
          if (cgf.getLangOpts().ObjCAutoRefCount &&
              cgf.cgm.getCodeGenOpts().OptimizationLevel > 0 &&
              !cgf.cgm.getCodeGenOpts().ObjCAutoRefCountExceptions)
            callOpWithExceptions->setAttr(
                "cir.arc.no_objc_arc_exceptions",
                mlir::UnitAttr::get(builder.getContext()));

          // For shared synthetic cleanup tries, the first call already carries
          // the canonical cleanup region for this depth. Mark later calls to
          // reuse that representative landing pad instead of cloning the same
          // cleanup region again in raw CIR.
          if (tryOp.getCleanup()) {
            callOpWithExceptions->setAttr(
                kShareSyntheticCleanupAttr,
                mlir::UnitAttr::get(builder.getContext()));
          } else {
            cgf.callWithExceptionCtx = callOpWithExceptions;
            cgf.populateCatchHandlersIfRequired(tryOp);
            cgf.callWithExceptionCtx = nullptr;
          }

          builder.restoreInsertionPoint(ip);
          return callOpWithExceptions;
        }
        // Operands don't dominate — can't reuse, fall through to create
        // a new per-call TryOp.
      }

      // Different depth, dominance failure, or no active TryOp.
      // Invalidate old TryOp tracking (depth changed or can't reuse).
      cgf.activeSyntheticTry = nullptr;
      cgf.activeSyntheticTryScope = EHScopeStack::stable_end();

      // Create new synthetic TryOp to wrap this call.
      tryOp = cir::TryOp::create(
          builder, callLoc,
          /*tryBuilder=*/
          [&](mlir::OpBuilder &b, mlir::Location loc) {},
          /*handlersBuilder=*/
          [&](mlir::OpBuilder &b, mlir::Location loc,
              mlir::OperationState &result) {
            auto *r = result.addRegion();
            builder.createBlock(r);
            cir::ResumeOp::create(builder, loc);
          });
      tryOp.setSynthetic(true);

      // Track for sharing with subsequent calls at the same depth.
      cgf.activeSyntheticTryScope = currentScope;
      cgf.activeSyntheticTry = tryOp;
      // Record which cleanup's destructor prefix the per-call cleanup regions
      // below carry, so popCleanupBlock knows whether collapsing this try into
      // a shared cir.cleanup.scope pad would preserve them.
      if (unsigned id = cgf.getInnermostEHCleanupId())
        tryOp->setAttr(CIRGenFunction::kSyntheticTryCleanupIdAttr,
                       builder.getI32IntegerAttr(id));
    }

    // For synthetic TryOps, insert the call inside the try body.
    mlir::OpBuilder::InsertPoint ip = builder.saveInsertionPoint();
    if (tryOp.getSynthetic()) {
      mlir::Block *lastBlock = &tryOp.getTryRegion().back();
      builder.setInsertionPointToStart(lastBlock);
    }

    cir::CallOp callOpWithExceptions;
    if (indirectFuncTy) {
      callOpWithExceptions = builder.createIndirectTryCallOp(
          callLoc, indirectFuncVal, indirectFuncTy, coercedArgs, callingConv,
          sideEffect);
    } else {
      callOpWithExceptions = builder.createTryCallOp(
          callLoc, directFuncOp, coercedArgs, callingConv, sideEffect);
    }
    callOpWithExceptions->setAttr("extra_attrs", extraFnAttrs);
    if (cgf.getLangOpts().ObjCAutoRefCount &&
        cgf.cgm.getCodeGenOpts().OptimizationLevel > 0 &&
        !cgf.cgm.getCodeGenOpts().ObjCAutoRefCountExceptions)
      callOpWithExceptions->setAttr(
          "cir.arc.no_objc_arc_exceptions",
          mlir::UnitAttr::get(builder.getContext()));

    // Set context for cleanup region population during EH scope traversal.
    cgf.callWithExceptionCtx = callOpWithExceptions;
    cgf.populateCatchHandlersIfRequired(tryOp);
    cgf.callWithExceptionCtx = nullptr;

    // For synthetic TryOps, add yield terminator and restore insertion point.
    if (tryOp.getSynthetic()) {
      cir::YieldOp::create(builder, tryOp.getLoc());
      builder.restoreInsertionPoint(ip);
    }

    return callOpWithExceptions;
  }

  assert(builder.getInsertionBlock() && "expected valid basic block");

  cir::CallOp op;
  if (indirectFuncTy) {
    op = builder.createIndirectCallOp(callLoc, indirectFuncVal, indirectFuncTy,
                                      coercedArgs, callingConv, sideEffect,
                                      extraFnAttrs);
  } else {
    op = builder.createCallOp(callLoc, directFuncOp, coercedArgs, callingConv,
                              sideEffect, extraFnAttrs);
  }
  if (cgf.getLangOpts().ObjCAutoRefCount &&
      cgf.cgm.getCodeGenOpts().OptimizationLevel > 0 &&
      !cgf.cgm.getCodeGenOpts().ObjCAutoRefCountExceptions)
    op->setAttr("cir.arc.no_objc_arc_exceptions",
                mlir::UnitAttr::get(builder.getContext()));

  return op;
}

const CIRGenFunctionInfo &
CIRGenTypes::arrangeFreeFunctionType(CanQual<FunctionProtoType> fpt) {
  SmallVector<CanQualType, 16> argTypes;
  return ::arrangeCIRFunctionInfo(*this, /*instanceMethod=*/false, argTypes,
                                  fpt);
}

const CIRGenFunctionInfo &
CIRGenTypes::arrangeBlockFunctionDeclaration(const FunctionProtoType *proto,
                                             const FunctionArgList &params) {
  llvm::SmallVector<CanQualType, 16> argTypes;
  argTypes.reserve(params.size());
  for (const VarDecl *param : params)
    argTypes.push_back(astContext.getCanonicalParamType(param->getType()));

  return arrangeCIRFunctionInfo(
      proto->getReturnType()->getCanonicalTypeUnqualified(), FnInfoOpts::None,
      argTypes, RequiredArgs::getFromProtoWithExtraSlots(proto, 1),
      proto->getExtInfo());
}

const CIRGenFunctionInfo &
CIRGenTypes::arrangeFreeFunctionType(CanQual<FunctionNoProtoType> fnpt) {
  CanQualType resultType = fnpt->getReturnType().getUnqualifiedType();
  return arrangeCIRFunctionInfo(resultType, FnInfoOpts::None, {},
                                RequiredArgs(0), fnpt->getExtInfo());
}

bool CIRGenTypes::inheritingCtorHasParams(const InheritedConstructor &inherited,
                                          CXXCtorType type) {
  // Parameters are unnecessary if we're constructing a base class subobject
  // and the inherited constructor lives in a virtual base.
  return type == Ctor_Complete ||
         !inherited.getShadowDecl()->constructsVirtualBase() ||
         !astContext.getTargetInfo().getCXXABI().hasConstructorVariants();
}

RValue CIRGenFunction::emitCall(const CIRGenFunctionInfo &funcInfo,
                                const CIRGenCallee &callee,
                                ReturnValueSlot returnValue,
                                const CallArgList &args,
                                cir::CIRCallOpInterface *callOp,
                                mlir::Location loc, bool isMustTail,
                                mlir::Value staticChain,
                                SourceLocation callSiteLoc) {
  QualType retTy = funcInfo.getReturnType();
  cir::FuncType cirFuncTy = getTypes().getFunctionType(funcInfo);

  SmallVector<mlir::Value, 16> cirCallArgs(args.size());
  llvm::SmallVector<bool, 8> argLValues;
  argLValues.reserve(args.size());
  bool hasLValueArg = false;
  for (const CallArg &arg : args) {
    bool isLValueArg = arg.hasLValue();
    argLValues.push_back(isLValueArg);
    hasLValueArg |= isLValueArg;
  }

  // OGCG emits lifetime.start for the sret temp alloca here when the return
  // value is unused (CGCall.cpp:5332-5334). CIR defers ABI lowering (including
  // sret) to a later pass, so no sret temp is created here.

  // Translate all of the arguments as necessary to match the CIR lowering.
  for (auto [argNo, arg, canQualArgType] :
       llvm::enumerate(args, funcInfo.argTypes())) {
    mlir::Type argType = convertType(canQualArgType);
    if (!mlir::isa<cir::RecordType>(argType) &&
        !mlir::isa<cir::ComplexType>(argType)) {
      mlir::Value v;
      if (arg.isAggregate()) {
        // Load the aggregate value from its address for coerced passing.
        Address addr = arg.hasLValue() ? arg.getKnownLValue().getAddress()
                                       : arg.getKnownRValue()
                                             .getAggregateAddress();
        v = builder.createLoad(loc, addr);
      } else {
        v = arg.getKnownRValue().getValue();
      }

      // We might have to widen integers, but we should never truncate.
      // ABI coercion padding bits should always be zero-filled, matching
      // OGCG's unconditional CreateZExt (CGCall.cpp:5568).
      if (argType != v.getType() && mlir::isa<cir::IntType>(v.getType())) {
        auto srcIntTy = mlir::cast<cir::IntType>(v.getType());
        auto dstIntTy = mlir::dyn_cast<cir::IntType>(argType);
        if (dstIntTy) {
          assert(dstIntTy.getWidth() >= srcIntTy.getWidth() &&
                 "Unexpected integer truncation in call argument");
          // Force zero-extension: cast through an unsigned source type so
          // CastKind::integral produces zext instead of sext for signed args.
          if (srcIntTy.isSigned() &&
              srcIntTy.getWidth() < dstIntTy.getWidth()) {
            auto unsignedSrcTy = cir::IntType::get(
                builder.getContext(), srcIntTy.getWidth(), /*isSigned=*/false);
            v = builder.createCast(loc, cir::CastKind::integral, v,
                                   unsignedSrcTy);
          }
          v = builder.createCast(loc, cir::CastKind::integral, v, argType);
        } else {
          // ABI coercion from integer to non-integer type of the same width
          // (e.g., i32 → float for FP register passing).  Use bitcast.
          v = builder.createBitcast(loc, v, argType);
        }
      }

      // If the argument doesn't match, perform a bitcast to coerce it. This
      // can happen due to trivial type mismatches.
      // TODO(cir): When getFunctionType is added, assert that this isn't
      // needed.
      if (argType != v.getType()) {
        auto srcPtrTy = mlir::dyn_cast<cir::PointerType>(v.getType());
        auto dstPtrTy = mlir::dyn_cast<cir::PointerType>(argType);
        if (srcPtrTy && dstPtrTy &&
            srcPtrTy.getAddrSpace() != dstPtrTy.getAddrSpace()) {
          if (srcPtrTy.getPointee() != dstPtrTy.getPointee()) {
            auto intermediateTy = cir::PointerType::get(
                dstPtrTy.getPointee(), srcPtrTy.getAddrSpace());
            v = builder.createBitcast(loc, v, intermediateTy);
          }
          v = builder.createAddrSpaceCast(loc, v, argType);
        } else if (srcPtrTy && dstPtrTy) {
          // Same address space but different pointee types. This can happen
          // due to trivial type mismatches (e.g. void* vs char*) when CIR's
          // typed pointers differ from what the callee expects. LLVM IR uses
          // opaque pointers so OGCG never hits this.
          v = builder.createBitcast(loc, v, argType);
        } else if (mlir::isa<cir::MethodType>(v.getType()) &&
                   mlir::isa<cir::MethodType>(argType)) {
          // MethodType mismatch from stale placeholder FuncType.
          // Reconcile to the callee parameter type.
          v = builder.createBitcast(loc, v, argType);
        } else if (mlir::isa<cir::RecordType>(v.getType()) ||
                   mlir::isa<cir::RecordType>(argType)) {
          // Record type mismatch from ABI coercion — the types are
          // structurally compatible but named differently. Pass as-is;
          // the lowering will handle the coercion via memcpy.
        } else if (mlir::isa<cir::ArrayType>(v.getType()) ||
                   mlir::isa<cir::ArrayType>(argType)) {
          // Array type mismatch from ABI coercion. Pass as-is.
        } else {
          // For truly unexpected mismatches, bitcast to force the type.
          v = builder.createBitcast(loc, v, argType);
        }
      }
      cirCallArgs[argNo] = v;
    } else {
      // If this is a trivially-copyable empty C++ record type, use undef
      // instead of loading from the aggregate temp. Such records will be
      // classified as ABIArgInfo::Ignore by the target ABI, meaning they won't
      // appear in the lowered function signature. The agg.tmp alloca for a
      // trivially-copyable empty struct is never stored to (there's no data
      // and no non-trivial copy), so loading from it would read uninitialized
      // memory. The undef value produced here will be dropped by the target
      // lowering pass when it processes call arguments.
      //
      // We restrict this to trivially-copyable records because non-trivially-
      // copyable types (e.g. classes with non-trivial destructors) may be
      // passed indirectly (by pointer) even when empty, and their aggregate
      // temporaries participate in cleanup/destruction logic.
      if (const auto *rd = canQualArgType->getAsCXXRecordDecl()) {
        if (rd->isEmpty() && rd->isTriviallyCopyable()) {
          cirCallArgs[argNo] =
              builder.getConstant(loc, cir::UndefAttr::get(argType));
          continue;
        }
      }

      Address src = Address::invalid();
      if (!arg.isAggregate()) {
        src = createMemTemp(arg.ty, loc, "coerce");
        arg.copyInto(*this, src, loc);
      } else {
        src = arg.hasLValue() ? arg.getKnownLValue().getAddress()
                              : arg.getKnownRValue().getAggregateAddress();
      }

      // Fast-isel and the optimizer generally like scalar values better than
      // FCAs, so we flatten them if this is safe to do for this argument.
      mlir::Type srcTy = src.getElementType();
      // FIXME(cir): get proper location for each argument.
      mlir::Location argLoc = loc;

      // If the source type is smaller than the destination type of the
      // coerce-to logic, copy the source value into a temp alloca the size
      // of the destination type to allow loading all of it. The bits past
      // the source value are left undef.
      const cir::CIRDataLayout &layout = cgm.getDataLayout();
      uint64_t srcSize = layout.getTypeAllocSize(srcTy);
      uint64_t dstSize = layout.getTypeAllocSize(argType);
      if (srcSize < dstSize) {
        Address tempAlloca = createTempAlloca(
            argType,
            CharUnits::fromQuantity(layout.getABITypeAlign(argType).value()),
            argLoc, "coerce");
        auto sizeTy = mlir::cast<cir::IntType>(
            convertType(getContext().getSizeType()));
        mlir::Value sizeVal =
            builder.getConstantInt(argLoc, sizeTy, srcSize);
        mlir::Value dstPtr = builder.createPtrBitcast(
            tempAlloca.getPointer(), builder.getVoidTy());
        mlir::Value srcPtr =
            builder.createPtrBitcast(src.getPointer(), builder.getVoidTy());
        cir::MemCpyOp::create(builder, argLoc, dstPtr, srcPtr, sizeVal);
        src = tempAlloca;
      } else if (srcTy != argType) {
        // Source is at least as large as destination — just bitcast.
        src = src.withElementType(builder, argType);
      }

      // assert(NumCIRArgs == STy.getMembers().size());
      // In LLVMGen: Still only pass the struct without any gaps but mark it
      // as such somehow.
      //
      // In CIRGen: Emit a load from the "whole" struct,
      // which shall be broken later by some lowering step into multiple
      // loads.
      assert(!cir::MissingFeatures::lowerAggregateLoadStore());
      cirCallArgs[argNo] = builder.createLoad(argLoc, src);
    }
  }

  const CIRGenCallee &concreteCallee = callee.prepareConcreteCallee(*this);
  mlir::Operation *calleePtr = concreteCallee.getFunctionPointer();

  // A direct callee declaration can be created earlier with a placeholder
  // `void ()` type when parameter record types weren't convertible yet.
  // Later in the same TU, call emission may have a fully-arranged signature
  // (from CIRGenFunctionInfo). If we keep using the stale placeholder decl,
  // `cir.call` gets created with zero operands/results and scalar call paths
  // assert on missing return values. Refine declaration-only placeholders to
  // the concrete call signature before creating the call op.
  if (auto directFn = mlir::dyn_cast<cir::FuncOp>(calleePtr)) {
    auto fnTy = directFn.getFunctionType();
    bool isPlaceholder = fnTy.getNumInputs() == 0 && !fnTy.isVarArg() &&
                         mlir::isa<cir::VoidType>(fnTy.getReturnType());
    if (directFn.isDeclaration() && isPlaceholder && !directFn.getNoProto() &&
        fnTy != cirFuncTy)
      directFn.setFunctionType(cirFuncTy);
  }

  // If the callee is a bitcast of a non-variadic function to have a variadic
  // function pointer type, check to see if we can remove the bitcast. This
  // comes up with unprototyped functions.
  //
  // This makes the IR nicer, but more importantly it ensures that we can
  // inline the function at -O0 if it is marked always_inline.
  if (cirFuncTy.isVarArg()) {
    if (auto castOp = dyn_cast<cir::CastOp>(calleePtr)) {
      if (castOp.getKind() == cir::CastKind::bitcast) {
        mlir::Operation *srcOp = castOp.getSrc().getDefiningOp();
        cir::FuncOp origFn;
        if (auto getGlobalOp = dyn_cast_or_null<cir::GetGlobalOp>(srcOp)) {
          mlir::Operation *globalOp =
              cgm.getGlobalValue(getGlobalOp.getName());
          origFn = dyn_cast_or_null<cir::FuncOp>(globalOp);
        }
        if (origFn) {
          cir::FuncType origFT = origFn.getFunctionType();
          if (!origFT.isVarArg() &&
              origFT.getNumInputs() == cirFuncTy.getNumInputs() &&
              origFT.getReturnType() == cirFuncTy.getReturnType()) {
            bool paramsMatch = true;
            for (unsigned i = 0; i < origFT.getNumInputs(); ++i) {
              if (origFT.getInput(i) != cirFuncTy.getInput(i)) {
                paramsMatch = false;
                break;
              }
            }
            if (paramsMatch) {
              calleePtr = origFn;
              cirFuncTy = origFT;
            }
          }
        }
      }
    }
  }

  // OGCG deactivates callee-destructed argument cleanups right before the call
  // to prevent double-destruction in MSVC ABI (CGCall.cpp:5817-5818).
  if (!args.getCleanupsToDeactivate().empty()) {
    for (const auto &cleanup : llvm::reverse(args.getCleanupsToDeactivate())) {
      deactivateCleanupBlock(cleanup.cleanup, cleanup.isActiveIP);
      // OGCG erases the dominating IP instruction here.
      // CIR uses nullptr for now (deactivateCleanupBlock handles it).
    }
  }

  // OGCG tracks LargestVectorWidth from call arguments and return values to set
  // the min-legal-vector-width function attribute (CGCall.cpp:5821-5823).
  assert(!cir::MissingFeatures::largestVectorWidth());

  mlir::NamedAttrList attrs;
  StringRef funcName;
  if (auto calleeFuncOp = dyn_cast<cir::FuncOp>(calleePtr))
    funcName = calleeFuncOp.getName();

  cir::CallingConv callingConv;
  cir::SideEffect sideEffect;
  cgm.constructAttributeList(funcName, funcInfo, callee.getAbstractInfo(),
                             attrs, callingConv, sideEffect,
                             /*attrOnCallSite=*/true, /*isThunk=*/false);

  // OGCG checks for NotTailCalledAttr and sets TCK_NoTail on the call
  // to prevent tail-call optimization (CGCall.cpp:6013-6015).
  // Set in extra_attrs so it survives op replacements during release().
  const Decl *targetDecl = callee.getAbstractInfo().getCalleeDecl().getDecl();
  const auto *callerDecl = dyn_cast_or_null<FunctionDecl>(curCodeDecl);
  const auto *calleeDecl = dyn_cast_or_null<FunctionDecl>(targetDecl);
  if (targetDecl && targetDecl->hasAttr<NotTailCalledAttr>())
    attrs.set("no_tail", mlir::UnitAttr::get(&cgm.getMLIRContext()));

  // For indirect calls through extern "C" function pointer variables,
  // propagate the extern "C" info so target lowering can skip noundef on the
  // return value (matching OGCG's HasStrictReturn VarDecl check,
  // CGCall.cpp:1923-1926).
  if (targetDecl)
    if (const auto *vd = dyn_cast<VarDecl>(targetDecl))
      if (vd->isExternC())
        attrs.set("cir.callee_extern_c",
                  mlir::UnitAttr::get(&cgm.getMLIRContext()));

  // Check target features: diagnose builtin calls where the callee requires
  // target features the caller doesn't have. Mirrors part of OGCG's
  // checkTargetFeatures (CodeGenFunction.cpp). The always_inline / flatten
  // check is handled separately in checkTargetFeatures() below, which is
  // called from emitCall(QualType, ...) where the CallExpr is available.
  if (targetDecl) {
    if (const auto *fd = dyn_cast<FunctionDecl>(targetDecl)) {
      const FunctionDecl *callerFD =
          dyn_cast_or_null<FunctionDecl>(curCodeDecl);
      if (callerFD) {
        unsigned builtinID = fd->getBuiltinID();
        if (builtinID) {
          llvm::StringRef featureList(
              getContext().BuiltinInfo.getRequiredFeatures(builtinID));
          if (!featureList.empty()) {
            llvm::StringMap<bool> callerFeatures;
            getContext().getFunctionFeatureMap(callerFeatures, callerFD);
            if (!Builtin::evaluateRequiredTargetFeatures(featureList,
                                                         callerFeatures)) {
              cgm.getDiags().Report(fd->getLocation(),
                                    diag::err_builtin_needs_feature)
                  << fd->getDeclName() << featureList;
            }
          }
        }
      }
    }
  }
  cgm.getTargetCIRGenInfo().checkFunctionCallABI(
      cgm, callSiteLoc, callerDecl, calleeDecl, args, retTy);

  cir::FuncType indirectFuncTy;
  mlir::Value indirectFuncVal;
  cir::FuncOp directFuncOp;
  if (auto fnOp = dyn_cast<cir::FuncOp>(calleePtr)) {
    directFuncOp = fnOp;
  } else if (auto getGlobalOp = mlir::dyn_cast<cir::GetGlobalOp>(calleePtr)) {
    // FIXME(cir): This peephole optimization avoids indirect calls for
    // builtins. This should be fixed in the builtin declaration instead by
    // not emitting an unecessary get_global in the first place.
    // However, this is also used for no-prototype functions.
    mlir::Operation *globalOp = cgm.getGlobalValue(getGlobalOp.getName());
    assert(globalOp && "undefined global function");
    directFuncOp = mlir::cast<cir::FuncOp>(globalOp);
  } else {
    [[maybe_unused]] mlir::ValueTypeRange<mlir::ResultRange> resultTypes =
        calleePtr->getResultTypes();
    [[maybe_unused]] auto funcPtrTy =
        mlir::dyn_cast<cir::PointerType>(resultTypes.front());
    assert(funcPtrTy && mlir::isa<cir::FuncType>(funcPtrTy.getPointee()) &&
           "expected pointer to function");

    indirectFuncTy = cirFuncTy;
    indirectFuncVal = calleePtr->getResult(0);
  }

  // OGCG adds MSVC EH personality function bundles here for Windows
  // exception handling. Windows/MSVC is not a supported CIR target.

  // Check if the call-site attributes mark this as nothrow. This is used
  // to set the nothrow attribute on the call operation itself, matching OGCG
  // which only puts nounwind on call-sites when the prototype or declaration
  // explicitly indicates nothrow (not inferred from the function definition).
  bool callSiteNothrow = attrs.getNamed("nothrow").has_value();
  // Also check the target declaration directly for explicit nothrow/noexcept.
  // This handles code paths (e.g. builtins) that bypass constructAttributeList
  // but whose declaration explicitly says nothrow.
  if (!callSiteNothrow && targetDecl) {
    if (targetDecl->hasAttr<NoThrowAttr>())
      callSiteNothrow = true;
    else if (const auto *fd = dyn_cast<FunctionDecl>(targetDecl))
      if (const auto *fpt = fd->getType()->getAs<FunctionProtoType>())
        if (!isUnresolvedExceptionSpec(fpt->getExceptionSpecType()) &&
            fpt->isNothrow())
          callSiteNothrow = true;
  }

  // For the call-vs-invoke decision, also check if the callee function
  // definition itself is nounwind. OGCG does this (CGCall.cpp:5906-5908)
  // to decide call vs invoke, but does NOT propagate it to call-site attrs.
  bool cannotThrow = callSiteNothrow;
  if (!cannotThrow && directFuncOp) {
    if (auto extraAttrs = directFuncOp.getExtraAttrs())
      if (extraAttrs->getElements().get("nothrow"))
        cannotThrow = true;
  }
  // Inside a cir.cleanup.scope body, a throwing call must be an exception call
  // even though no per-call EH cleanup was pushed on the stack: the enclosing
  // cir.cleanup.scope supplies the single shared landing pad. Without this the
  // call would be a plain (non-throwing) call and the shared cleanup would
  // never run on unwind.
  bool isInvoke =
      !cannotThrow && (isCatchOrCleanupRequired() || emittingSharedCleanupScope);

  // A potentially-throwing call inside an outlined OpenMP region whose active
  // unwind destination is an EH scope pushed *outside* that region (e.g. a C++
  // try wrapping the region) would need an unwind edge crossing the omp region
  // boundary. CIRGen builds that as a `cir.call exception ... cleanup` targeting
  // the enclosing cir.try, whose unwind region lies outside the omp op. When
  // FlattenCFG lowers the try, the cross-region successor is malformed and
  // crashes collectUnreachable's block-successor BFS during lowering. The region
  // is fundamentally not extractable this way, so diagnose it as NYI instead of
  // emitting the malformed edge. exceptionEscapesOpenMPRegion walks the EH chain
  // so the gate still fires when in-region cleanups (RAII/ARC/cleanup attr) sit
  // between the call and the out-of-region handler; an escaping throw with no
  // handler is not an invoke and is likewise unaffected.
  if (isInvoke && exceptionEscapesOpenMPRegion()) {
    cgm.errorNYI(callSiteLoc, "exception escaping an OpenMP region");
    isInvoke = false;
  }

  bool wouldViolateInlineABI =
      cgm.getTargetCIRGenInfo().wouldInliningViolateFunctionCallABI(callerDecl,
                                                                    calleeDecl);

  // Apply always_inline to all calls within flatten functions, matching
  // OGCG (CGCall.cpp:5877-5884). The flatten attribute causes the compiler
  // to inline every callee except those marked noinline.
  if (curCodeDecl && curCodeDecl->hasAttr<FlattenAttr>() &&
      !inNoInlineAttributedStmt &&
      !(targetDecl && targetDecl->hasAttr<NoInlineAttr>()) &&
      !wouldViolateInlineABI) {
    attrs.set("alwaysinline", mlir::UnitAttr::get(&cgm.getMLIRContext()));
  }

  // Add call-site noinline attribute from [[clang::noinline]] statement attr.
  if (inNoInlineAttributedStmt)
    attrs.set("noinline", mlir::UnitAttr::get(&cgm.getMLIRContext()));

  // Add call-site always_inline attribute from [[clang::always_inline]]
  // statement attr.
  if (inAlwaysInlineAttributedStmt && !wouldViolateInlineABI) {
    attrs.set("alwaysinline", mlir::UnitAttr::get(&cgm.getMLIRContext()));
  }

  // Add call-site nomerge attribute from [[clang::nomerge]] statement attr.
  if (inNoMergeAttributedStmt)
    attrs.set("nomerge", mlir::UnitAttr::get(&cgm.getMLIRContext()));

  // Remove convergent from call sites within [[clang::noconvergent]] stmt.
  // Matches OGCG CGCall.cpp:5863-5864.
  if (inNoConvergentAttributedStmt) {
    auto convgt = cir::ConvergentAttr::get(&cgm.getMLIRContext());
    attrs.erase(convgt.getMnemonic());
  }

  // Propagate strictfp to call sites when the calling function has strict FP
  // semantics, matching OGCG's CGCall.cpp:5840-5842. All calls within a
  // strictfp function must carry the strictfp attribute.
  if (curFuncDecl && curFuncDecl->hasAttr<StrictFPAttr>())
    attrs.set("strictfp", mlir::UnitAttr::get(&cgm.getMLIRContext()));

  bool callDoesNotReturn = static_cast<bool>(attrs.get("noreturn"));
  bool stripNoReturnForUnreachableUB =
      callDoesNotReturn && sanOpts.has(SanitizerKind::Unreachable);
  if (stripNoReturnForUnreachableUB) {
    attrs.erase("noreturn");
    if (directFuncOp)
      eraseNoReturnAttr(directFuncOp);
  }

  // Create ExtraFuncAttributesAttr from the collected attributes.
  // Note: side_effect, calling convention, and nothrow are also set directly on
  // the call op (nothrow at line 884), but we include them in extra_attrs too
  // for completeness. The lowering passes handle both representations.
  auto extraFnAttrs = cir::ExtraFuncAttributesAttr::get(
      attrs.getDictionary(&cgm.getMLIRContext()));

  mlir::Location callLoc = currCallDebugLocOverride.value_or(loc);
  if (staticChain)
    cirCallArgs.push_back(staticChain);

  bool runtimePtrauth = false;
  if (indirectFuncTy && concreteCallee.isOrdinary())
    if (mlir::Value discriminator =
            concreteCallee.getAbstractInfo().getRuntimePtrauthDiscriminator()) {
      cirCallArgs.push_back(discriminator);
      runtimePtrauth = true;
    }
  cir::CIRCallOpInterface theCall = emitCallLikeOp(
      *this, callLoc, indirectFuncTy, indirectFuncVal, directFuncOp,
      cirCallArgs, isInvoke, callingConv, sideEffect, extraFnAttrs);
  if (hasLValueArg)
    theCall->setAttr("cir.arg_lvalues",
                     mlir::DenseBoolArrayAttr::get(&cgm.getMLIRContext(),
                                                  argLValues));

  if (callSiteNothrow)
    theCall->setAttr("nothrow", mlir::UnitAttr::get(&cgm.getMLIRContext()));

  // Carry __attribute__((regparm(N))) onto the call so the X86-32 ABI lowering
  // pass marks the register-passed call arguments `inreg`, including for
  // indirect calls through a regparm function pointer where no callee FuncOp is
  // available. Only emit when present, keeping regparm(0) distinct from no
  // attribute (ExtInfo's +1 encoding).
  if (funcInfo.getExtInfo().getHasRegParm())
    theCall->setAttr(
        "cir.regparm",
        mlir::IntegerAttr::get(
            mlir::IntegerType::get(&cgm.getMLIRContext(), 32),
            funcInfo.getExtInfo().getRegParm()));

  // When emitting calls during EH cleanup and the call might throw, mark it
  // for invoke-to-terminate lowering. Per C++ [except.terminate], if a
  // destructor throws during stack unwinding, std::terminate() must be called.
  // This matches OGCG's pushTerminate() guard in CGCleanup.cpp:1056-1076.
  if (isEmittingEHCleanup && !cannotThrow)
    theCall->setAttr("cir.terminate_on_throw",
                     mlir::UnitAttr::get(&cgm.getMLIRContext()));

  if (callOp)
    *callOp = theCall;

  if (staticChain) {
    theCall->setAttr("cir.static_chain",
                     mlir::UnitAttr::get(&cgm.getMLIRContext()));
  }

  // Emit virtual-call devirtualization markers for later LLVM IR lowering.
  // This mirrors OGCG's EmitTypeMetadataCodeForVCall / EmitVTableTypeCheckedLoad
  // split: hidden-LTO WPD virtual calls normally lower to llvm.(public.)type.test
  // + llvm.assume, but when virtual-function-elimination is also enabled they
  // instead lower to llvm.type.checked.load.
  if (const auto *md =
          callee.isVirtual()
              ? dyn_cast_or_null<CXXMethodDecl>(
                    callee.getVirtualMethodDecl().getDecl())
              : nullptr) {
    const CXXRecordDecl *rd = md->getParent();
    std::string typeName = rd->getQualifiedNameAsString();
    bool shouldEmitCFIVCallCheck =
        getLangOpts().Sanitize.has(SanitizerKind::CFIVCall) &&
        (cgm.getCodeGenOpts().SanitizeCfiCrossDso ||
         cgm.hasHiddenLTOVisibility(rd)) &&
        !getContext().getNoSanitizeList().containsType(
            SanitizerKind::CFIVCall, typeName);
    bool shouldEmitVirtualTypeMetadata =
        shouldEmitCFIVCallCheck || cgm.getCodeGenOpts().DevirtualizeSpeculatively ||
        (cgm.getCodeGenOpts().WholeProgramVTables &&
         !cgm.alwaysHasLTOVisibilityPublic(rd));
    if (shouldEmitVirtualTypeMetadata) {
      CanQualType ty = cgm.getASTContext().getCanonicalTagType(rd);
      bool virtualTypeIdIsDistinct =
          !isExternallyVisible(rd->getLinkageAndVisibility().getLinkage());
      theCall->setAttr("cir.virtual.typeid",
                       cgm.getStringAttrCached(
                           cgm.createStableMetadataIdentifierKeyForType(ty)));
      if (virtualTypeIdIsDistinct)
        theCall->setAttr("cir.virtual.typeid_distinct",
                         mlir::UnitAttr::get(&cgm.getMLIRContext()));

      bool shouldEmitVirtualTypeCheckedLoad =
          shouldEmitVTableTypeCheckedLoad(rd);
      if (shouldEmitCFIVCallCheck) {
        StringRef handlerKind = "abort";
        if (cgm.getCodeGenOpts().SanitizeTrap.has(SanitizerKind::CFIVCall))
          handlerKind = "trap";
        else if (cgm.getCodeGenOpts().SanitizeRecover.has(
                     SanitizerKind::CFIVCall))
          handlerKind = "recover";

        emitSanitizerStatReportStub(loc, CFISanitizerStatKind::VCall,
                                    theCall.getOperation());
        theCall->setAttr("cir.virtual.cfi_vcall_kind",
                         cgm.getStringAttrCached(handlerKind));
        theCall->setAttr("cir.nosanitize",
                         mlir::UnitAttr::get(&cgm.getMLIRContext()));
        // Use the call expression's begin location (not getExprLoc), matching
        // OGCG's prepareConcreteCallee which passes CE->getBeginLoc().
        // For "s1->f()", getBeginLoc() = column of 's', getExprLoc() = column
        // of 'f'.
        {
          const CallExpr *ce = callee.getVirtualCallExpr();
          mlir::Location beginLoc = ce ? getLoc(ce->getBeginLoc()) : loc;
          setUBSanSourceLocationAttrs(*this, theCall, beginLoc);
        }
        setUBSanTypeNameAttr(*this, theCall, ty);
        setUBSanTypeDescriptorAttrs(*this, theCall, ty);
        if (cgm.getCodeGenOpts().SanitizeMinimalRuntime)
          theCall->setAttr("cir.virtual.cfi_minimal_runtime",
                           mlir::UnitAttr::get(&cgm.getMLIRContext()));
        if (cgm.getCodeGenOpts().SanitizeHandlerPreserveAllRegs)
          theCall->setAttr("cir.virtual.cfi_preserve_all_regs",
                           mlir::UnitAttr::get(&cgm.getMLIRContext()));
      }
      if (shouldEmitVirtualTypeCheckedLoad) {
        // Match OGCG's VFE checked-load path, where the synthesized
        // llvm.type.checked.load carries !nosanitize metadata and the final
        // indirect call does not.
        theCall->setAttr("cir.nosanitize",
                         mlir::UnitAttr::get(&cgm.getMLIRContext()));
        theCall->setAttr("cir.virtual.typecheckedload",
                         mlir::UnitAttr::get(&cgm.getMLIRContext()));
      } else if (!shouldEmitCFIVCallCheck) {
        // Match OGCG: WPD uses llvm.type.test for hidden-LTO-visibility
        // classes and llvm.public.type.test otherwise. Internal/local classes
        // also use hidden type IDs.
        bool usePublicTypeTest =
            rd->isExternallyVisible() && !cgm.hasHiddenLTOVisibility(rd);
        theCall->setAttr(
            "cir.virtual.typetest_kind",
            cgm.getStringAttrCached(usePublicTypeTest ? "public" : "hidden"));
      } else {
        theCall->setAttr("cir.virtual.typetest_kind",
                         cgm.getStringAttrCached("hidden"));
      }
    }
  }


  // Emit KCFI operand bundle attribute for indirect calls when KCFI is enabled
  // and the calling function hasn't opted out via __no_sanitize__("kcfi").
  // Matches OGCG's AddKCFIBundle in CodeGenFunction.cpp:2921-2938.
  if (!directFuncOp &&
      cgm.getLangOpts().Sanitize.has(SanitizerKind::KCFI)) {
    // Check if the current function has KCFI suppressed via no_sanitize.
    bool kcfiSuppressed = false;
    if (curCodeDecl) {
      for (const auto *attr : curCodeDecl->specific_attrs<NoSanitizeAttr>())
        if (attr->getMask() & SanitizerKind::KCFI) {
          kcfiSuppressed = true;
          break;
        }
    }
    if (!kcfiSuppressed) {
      const FunctionProtoType *FP =
          callee.getAbstractInfo().getCalleeFunctionProtoType();
      if (FP) {
        StringRef Salt;
        if (const auto &Info = FP->getExtraAttributeInfo())
          Salt = Info.CFISalt;
        uint32_t TypeId = cgm.createKCFITypeId(FP->desugar(), Salt);
        theCall->setAttr(
            "cir.kcfi",
            mlir::IntegerAttr::get(
                mlir::IntegerType::get(&cgm.getMLIRContext(), 32), TypeId));
      }
    }
  }

  if (!directFuncOp)
    if (concreteCallee.isOrdinary())
      if (auto pointerAuth = concreteCallee.getAbstractInfo().getPointerAuthInfo()) {
        theCall->setAttr(
            "cir.ptrauth",
            mlir::ArrayAttr::get(
                &cgm.getMLIRContext(),
                {mlir::IntegerAttr::get(
                     mlir::IntegerType::get(&cgm.getMLIRContext(), 32),
                     pointerAuth->key),
                 cgm.getI64AttrCached(pointerAuth->discriminator)}));

        if (runtimePtrauth)
          theCall->setAttr("cir.runtime_ptrauth",
                           mlir::UnitAttr::get(&cgm.getMLIRContext()));

        if (callee.isVirtual() ||
            concreteCallee.getAbstractInfo().shouldBlendVTableSlot())
          theCall->setAttr("cir.ptrauth.blend_vtable_slot",
                           mlir::UnitAttr::get(&cgm.getMLIRContext()));
      }

  // Set nonnull/align/dereferenceable/noescape on arguments for indirect
  // calls. For direct calls, these attrs are already available on the callee
  // FuncOp and propagated later during lowering.
  if (!directFuncOp && !funcInfo.argTypes().empty()) {
    unsigned numArgs = funcInfo.argTypes().size();
    auto *ctx = &cgm.getMLIRContext();
    auto emptyDict = mlir::DictionaryAttr::get(ctx);
    auto existingArgAttrs =
        theCall->getAttrOfType<mlir::ArrayAttr>("cir.call_arg_attrs");
    if (existingArgAttrs)
      numArgs = std::max<unsigned>(numArgs, existingArgAttrs.size());

    SmallVector<mlir::Attribute> argAttrs(numArgs, emptyDict);
    if (existingArgAttrs)
      for (unsigned i = 0; i < existingArgAttrs.size() && i < numArgs; ++i)
        argAttrs[i] = existingArgAttrs[i];

    // Get FunctionProtoType for noescape checking on indirect calls.
    const auto *fpt =
        callee.getAbstractInfo().getCalleeFunctionProtoType();
    // For instance methods, the first arg is 'this' (not in FPT params).
    unsigned fptParamOffset = funcInfo.isInstanceMethod() ? 1 : 0;
    bool swiftIndirectResultUsesSRet = funcInfo.getReturnType()->isVoidType();

    bool changed = false;
    unsigned argIdx = 0;
    for (QualType argTy : funcInfo.argTypes()) {
      if (argIdx >= numArgs)
        break;

      mlir::NamedAttrList extraAttrs;
      if (argAttrs[argIdx])
        for (auto existing :
             mlir::cast<mlir::DictionaryAttr>(argAttrs[argIdx]))
          extraAttrs.append(existing);

      // Reference arguments get nonnull/align/dereferenceable.
      if (const auto *refTy = argTy->getAs<ReferenceType>()) {
        QualType pointeeTy = refTy->getPointeeType();

        if (cgm.getASTContext().getTargetAddressSpace(
                pointeeTy.getAddressSpace()) == 0 &&
            !cgm.getCodeGenOpts().NullPointerIsValid)
          extraAttrs.set("cir.nonnull", mlir::UnitAttr::get(ctx));

        extraAttrs.set("cir.noundef", mlir::UnitAttr::get(ctx));

        if (!pointeeTy->isIncompleteType() &&
            pointeeTy->isConstantSizeType()) {
          uint64_t size = cgm.getMinimumObjectSize(pointeeTy).getQuantity();
          if (size > 0)
            extraAttrs.set("cir.dereferenceable", getI64AttrCached(size));
        }

        if (pointeeTy->isObjectType()) {
          unsigned align;
          if (pointeeTy->isIncompleteType()) {
            align = 1;
          } else if (const auto *rd = pointeeTy->getAsCXXRecordDecl()) {
            if (!rd->hasDefinition()) {
              align = 1;
            } else {
              auto &layout = getContext().getASTRecordLayout(rd);
              if (rd->isEffectivelyFinal())
                align = layout.getAlignment().getQuantity();
              else
                align = layout.getNonVirtualAlignment().getQuantity();
            }
          } else {
            align = getContext().getTypeAlignInChars(pointeeTy).getQuantity();
          }
          extraAttrs.set("cir.align", getI64AttrCached(align));
        }
      }

      // Mark noescape parameters. Matches OGCG CGCall.cpp:3014-3015.
      if (fpt && fpt->hasExtParameterInfos() && argIdx >= fptParamOffset) {
        unsigned fptIdx = argIdx - fptParamOffset;
        if (fptIdx < fpt->getNumParams()) {
          auto extInfo = fpt->getExtParameterInfo(fptIdx);
          if (extInfo.isNoEscape())
            extraAttrs.set("cir.noescape", mlir::UnitAttr::get(ctx));

          // swiftself / swiftasync / swifterror from ParameterABI. Matches
          // OGCG's per-parameter loop in ConstructAttributeList (CGCall.cpp),
          // which applies these uniformly to definitions, declarations, and
          // call sites.
          if (extInfo.getABI() == ParameterABI::SwiftContext)
            extraAttrs.set("cir.swiftself", mlir::UnitAttr::get(ctx));
          else if (extInfo.getABI() == ParameterABI::SwiftAsyncContext)
            extraAttrs.set("cir.swiftasync", mlir::UnitAttr::get(ctx));
          else if (extInfo.getABI() == ParameterABI::SwiftErrorResult)
            extraAttrs.set("cir.swifterror", mlir::UnitAttr::get(ctx));

          if (extInfo.getABI() == ParameterABI::SwiftIndirectResult &&
              argTy->isPointerType()) {
            extraAttrs.set("cir.noalias", mlir::UnitAttr::get(ctx));

            QualType pointeeTy = argTy->getPointeeType();
            if (swiftIndirectResultUsesSRet) {
              extraAttrs.set("cir.sret",
                             mlir::TypeAttr::get(convertType(argTy)));
              swiftIndirectResultUsesSRet = false;
            }

            if (!pointeeTy->isIncompleteType() &&
                pointeeTy->isConstantSizeType()) {
              auto info = getContext().getTypeInfoInChars(pointeeTy);
              uint64_t size = info.Width.getQuantity();
              if (size > 0)
                extraAttrs.set("cir.dereferenceable", getI64AttrCached(size));

              uint64_t align = info.Align.getQuantity();
              if (auto existingAlign = mlir::dyn_cast_or_null<mlir::IntegerAttr>(
                      extraAttrs.get("cir.align")))
                align = std::max<uint64_t>(align, existingAlign.getInt());
              extraAttrs.set("cir.align", getI64AttrCached(align));
            }
          }
        }
      }

      if (!extraAttrs.empty()) {
        argAttrs[argIdx] = mlir::DictionaryAttr::get(ctx, extraAttrs);
        changed = true;
      }
      ++argIdx;
    }

    if (changed)
      theCall->setAttr("cir.call_arg_attrs",
                       mlir::ArrayAttr::get(ctx, argAttrs));
  }

  // Set nonnull/align/dereferenceable on 'this' pointer for instance method
  // call sites. For direct calls, these also come from the callee FuncOp.
  // For indirect/virtual calls, there's no FuncOp, so these call-site attrs
  // are the only source. The target lowering preserves them when rewriting
  // the call op. Matches OGCG CGCall.cpp:2782-2817.
  if (funcInfo.isInstanceMethod() &&
      !directFuncOp && !funcInfo.argTypes().empty()) {
    QualType thisTy = funcInfo.argTypesBegin()[0];
    if (thisTy->isPointerType() && !thisTy->isVoidPointerType()) {
      QualType pointeeTy = thisTy->getPointeeType();
      // cir.call_arg_attrs indexes by argument position (excluding indirect
      // callee pointer). Arg 0 is always 'this'.
      unsigned numArgs = funcInfo.argTypes().size();
      auto *ctx = &cgm.getMLIRContext();
      auto emptyDict = mlir::DictionaryAttr::get(ctx);
      auto existingArgAttrs =
          theCall->getAttrOfType<mlir::ArrayAttr>("cir.call_arg_attrs");
      if (existingArgAttrs)
        numArgs = std::max<unsigned>(numArgs, existingArgAttrs.size());
      SmallVector<mlir::Attribute> argAttrs(numArgs, emptyDict);
      if (existingArgAttrs)
        for (unsigned i = 0; i < existingArgAttrs.size() && i < numArgs; ++i)
          argAttrs[i] = existingArgAttrs[i];
      mlir::NamedAttrList thisAttrs;
      if (!argAttrs.empty() && argAttrs[0])
        for (auto existing : mlir::cast<mlir::DictionaryAttr>(argAttrs[0]))
          thisAttrs.append(existing);
      if (!cgm.getCodeGenOpts().NullPointerIsValid &&
          thisTy.getAddressSpace() == LangAS::Default) {
        thisAttrs.set("cir.nonnull", mlir::UnitAttr::get(ctx));
        uint64_t size =
            cgm.getMinimumObjectSize(pointeeTy).getQuantity();
        thisAttrs.set("cir.dereferenceable", getI64AttrCached(size));
      } else if (thisTy.getAddressSpace() == LangAS::Default) {
        // With -fno-delete-null-pointer-checks, use dereferenceable_or_null
        // instead of nonnull + dereferenceable.
        // Matches OGCG CGCall.cpp:2800-2808.
        uint64_t size =
            cgm.getMinimumObjectSize(pointeeTy).getQuantity();
        thisAttrs.set("cir.dereferenceable_or_null", getI64AttrCached(size));
      }
      // For C++ class pointees, use non-virtual alignment to avoid
      // inheriting virtual base alignment. Incomplete pointees still
      // conservatively carry byte alignment, matching OGCG.
      unsigned align;
      if (pointeeTy->isIncompleteType()) {
        align = 1;
      } else if (const auto *rd = pointeeTy->getAsCXXRecordDecl()) {
        if (!rd->hasDefinition()) {
          align = 1;
        } else {
          auto &layout = getContext().getASTRecordLayout(rd);
          if (rd->isEffectivelyFinal())
            align = layout.getAlignment().getQuantity();
          else
            align = layout.getNonVirtualAlignment().getQuantity();
        }
      } else {
        align = getContext().getTypeAlignInChars(pointeeTy).getQuantity();
      }
      thisAttrs.set("cir.align", getI64AttrCached(align));
      if (!thisAttrs.empty()) {
        argAttrs[0] = mlir::DictionaryAttr::get(ctx, thisAttrs);
        theCall->setAttr("cir.call_arg_attrs",
                         mlir::ArrayAttr::get(ctx, argAttrs));
      }
    }
  }

  // Set nonnull/align/dereferenceable on reference results for indirect calls.
  // For direct calls, these attrs are propagated from the callee FuncOp.
  if (!directFuncOp)
    if (const auto *refTy = retTy->getAs<ReferenceType>()) {
      QualType pointeeTy = refTy->getPointeeType();
      auto *ctx = &cgm.getMLIRContext();
      mlir::NamedAttrList resAttrs;
      if (auto existingResAttrs =
              theCall->getAttrOfType<mlir::DictionaryAttr>("cir.call_res_attrs"))
        for (auto existing : existingResAttrs)
          resAttrs.append(existing);

      if (cgm.getASTContext().getTargetAddressSpace(
              pointeeTy.getAddressSpace()) == 0 &&
          !cgm.getCodeGenOpts().NullPointerIsValid)
        resAttrs.set("cir.nonnull", mlir::UnitAttr::get(ctx));

      resAttrs.set("cir.noundef", mlir::UnitAttr::get(ctx));

      if (!pointeeTy->isIncompleteType() && pointeeTy->isConstantSizeType()) {
        uint64_t size = cgm.getMinimumObjectSize(pointeeTy).getQuantity();
        if (size > 0)
          resAttrs.set("cir.dereferenceable", getI64AttrCached(size));
      }

      if (pointeeTy->isObjectType()) {
        unsigned align;
        if (pointeeTy->isIncompleteType()) {
          align = 1;
        } else if (const auto *rd = pointeeTy->getAsCXXRecordDecl()) {
          if (!rd->hasDefinition()) {
            align = 1;
          } else {
            auto &layout = getContext().getASTRecordLayout(rd);
            if (rd->isEffectivelyFinal())
              align = layout.getAlignment().getQuantity();
            else
              align = layout.getNonVirtualAlignment().getQuantity();
          }
        } else {
          align = getContext().getTypeAlignInChars(pointeeTy).getQuantity();
        }
        resAttrs.set("cir.align", getI64AttrCached(align));
      }

      if (!resAttrs.empty())
        theCall->setAttr("cir.call_res_attrs",
                         mlir::DictionaryAttr::get(ctx, resAttrs));
    }

  // If the call doesn't return, emit unreachable after it and create a new
  // block so that subsequent codegen has a valid insertion point.
  // This matches OGCG's EmitUnreachable + EnsureInsertPoint pattern.
  if (callDoesNotReturn) {
    if (stripNoReturnForUnreachableUB &&
        sanOpts.hasOneOf(SanitizerKind::Address |
                         SanitizerKind::KernelAddress)) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPoint(theCall.getOperation());

      auto fnTy = cir::FuncType::get({}, builder.getVoidTy());
      auto fn = cgm.createRuntimeFunction(fnTy, "__asan_handle_no_return");
      auto asanCall = builder.createCallOp(loc, fn, {}, fn.getCallingConv());
      asanCall->setAttr("nothrow", mlir::UnitAttr::get(&cgm.getMLIRContext()));
      asanCall->setAttr("cir.nosanitize",
                        mlir::UnitAttr::get(&cgm.getMLIRContext()));
    }

    SourceLocRAIIObject scope(*this, loc);
    emitUnreachable(callSiteLoc, /*createNewBlock=*/true);
    return getUndefRValue(retTy);
  }
  // Musttail handling: set the attribute and emit cir.return immediately,
  // bypassing all result processing. This matches OGCG (CGCall.cpp:6103-6127)
  // which emits ret immediately after a musttail call. For aggregate returns,
  // this prevents the store→load pattern through __retval that violates LLVM's
  // musttail→ret adjacency constraint after call convention lowering (sret).
  if (isMustTail) {
    for (EHScopeStack::stable_iterator si = currentCleanupStackDepth;
         si != ehStack.stable_end();
         si = ehStack.find(si)->getEnclosingEHScope()) {
      EHScope &scope = *ehStack.find(si);
      if (scope.getKind() != EHScope::Cleanup)
        continue;

      auto &cleanupScope = cast<EHCleanupScope>(scope);
      if (!cleanupScope.getCleanup()->isRedundantBeforeReturn()) {
        cgm.errorUnsupported(mustTailCall, "tail call skipping over cleanups");
        break;
      }
    }

    if (auto op = mlir::dyn_cast<cir::CallOp>(theCall.getOperation()))
      op.setMustTail(true);
    mlir::ResultRange results = theCall->getOpResults();
    if (!results.empty())
      cir::ReturnOp::create(builder, callLoc, results[0]);
    else
      cir::ReturnOp::create(builder, callLoc);
    builder.createBlock(builder.getBlock()->getParent());
    return getUndefRValue(retTy);
  }
  // KCFI bundle is emitted above after theCall creation (cir.kcfi attr).
  // OGCG also emits pointer authentication bundles for indirect calls.
  // OGCG writes back SwiftError after the call (CGCall.cpp:6129-6133).
  assert(!cir::MissingFeatures::swift());
  emitWritebacks(*this, args);
  // OGCG ends per-argument lifetime markers after the call.
  assert(!cir::MissingFeatures::callArgCleanups());

  // Prepare alignment assumption info from AssumeAlignedAttr and/or
  // AllocAlignAttr on the callee declaration. OGCG handles these via
  // AssumeAlignedAttrEmitter/AllocAlignAttrEmitter in CGCall.cpp.
  // Both attributes are processed independently, and when both provide
  // constant alignments, the maximum is used (matching OGCG's
  // maybeRaiseRetAlignmentAttribute).
  const AssumeAlignedAttr *assumeAlignedAttr = nullptr;
  int64_t assumeAlignedVal = 0;
  mlir::Value assumeAlignedOffset;

  const AllocAlignAttr *allocAlignAttr = nullptr;
  int64_t allocAlignConstVal = 0;      // >0 if constant, 0 if dynamic
  mlir::Value allocAlignDynamicValue;   // non-null if dynamic

  if (targetDecl) {
    // Check for __attribute__((assume_aligned(N [, offset]))).
    if (const auto *aa = targetDecl->getAttr<AssumeAlignedAttr>()) {
      assumeAlignedAttr = aa;
      llvm::APSInt alignVal =
          aa->getAlignment()->EvaluateKnownConstInt(cgm.getASTContext());
      assumeAlignedVal = alignVal.getExtValue();
      if (const Expr *offsetExpr = aa->getOffset()) {
        assumeAlignedOffset = emitScalarExpr(offsetExpr);
        // Canonicalize zero offset to no offset.
        if (auto constOp =
                assumeAlignedOffset.getDefiningOp<cir::ConstantOp>()) {
          if (auto intAttr =
                  mlir::dyn_cast<cir::IntAttr>(constOp.getValue())) {
            if (intAttr.getValue() == 0)
              assumeAlignedOffset = nullptr;
          }
        }
      }
    }
    // Check for __attribute__((alloc_align(param_idx))).
    // Always processed, even when AssumeAlignedAttr is present (OGCG
    // processes both independently and takes the max for constant values).
    if (const auto *aa = targetDecl->getAttr<AllocAlignAttr>()) {
      allocAlignAttr = aa;
      unsigned paramIdx = aa->getParamIndex().getLLVMIndex();
      mlir::Value alignArg = args[paramIdx].getKnownRValue().getValue();
      // Try to extract a constant alignment value.
      if (auto constOp = alignArg.getDefiningOp<cir::ConstantOp>()) {
        if (auto intAttr =
                mlir::dyn_cast<cir::IntAttr>(constOp.getValue())) {
          allocAlignConstVal = intAttr.getSInt();
        }
      }
      if (allocAlignConstVal == 0) {
        // Dynamic alignment: will emit AssumeAlignedOp with SSA value.
        allocAlignDynamicValue = alignArg;
      }
    }
  }

  mlir::Type retCIRTy = convertType(retTy);
  if (isa<cir::VoidType>(retCIRTy))
    return getUndefRValue(retTy);

  // Helper to emit alignment assumptions on a scalar return value.
  // Handles both __attribute__((assume_aligned(N [, offset]))) and
  // __attribute__((alloc_align(param_idx))), following OGCG's pattern:
  //
  // OGCG (CGCall.cpp:5935-5939, 6281-6282):
  //   1. TryEmitAsCallSiteAttribute for AssumeAligned
  //      → sets `align N` if constant, power-of-2, no offset; consumes attr
  //   2. TryEmitAsCallSiteAttribute for AllocAlign
  //      → maybeRaiseRetAlignment takes max(existing, new); consumes if constant
  //   3. EmitAsAnAssumption for AssumeAligned (if not consumed: has offset)
  //   4. EmitAsAnAssumption for AllocAlign (if not consumed: dynamic value)
  //
  // CIR emits cir.assume_aligned ops that LowerToLLVM converts to either
  // `align N` on the call (constant, no offset) or `llvm.assume` (otherwise).
  auto applyAssumeAligned = [&](RValue rv) -> RValue {
    if ((!assumeAlignedAttr && !allocAlignAttr) || !rv.isScalar())
      return rv;
    mlir::Value val = rv.getValue();
    if (!mlir::isa<cir::PointerType>(val.getType()))
      return rv;

    // Step 1: Compute the constant alignment for the call-site attribute.
    // This is the max of assume_aligned (if no offset) and alloc_align
    // (if constant), matching OGCG's maybeRaiseRetAlignmentAttribute.
    int64_t callSiteAlign = 0;
    bool assumeAlignedConsumed = false;
    bool allocAlignConsumed = false;

    if (assumeAlignedAttr && !assumeAlignedOffset && assumeAlignedVal > 0 &&
        llvm::isPowerOf2_64(assumeAlignedVal)) {
      callSiteAlign = assumeAlignedVal;
      assumeAlignedConsumed = true;
    }
    if (allocAlignAttr && allocAlignConstVal > 0 &&
        llvm::isPowerOf2_64(allocAlignConstVal)) {
      callSiteAlign = std::max(callSiteAlign, allocAlignConstVal);
      allocAlignConsumed = true;
    }

    // Step 2: Emit constant alignment as cir.assume_aligned (becomes `align N`
    // on the call during lowering).
    if (callSiteAlign > 0) {
      SourceLocation locForAttr =
          assumeAlignedAttr ? assumeAlignedAttr->getLocation()
                            : allocAlignAttr->getLocation();
      val = emitAlignmentAssumption(val, retTy, SourceLocation(), locForAttr,
                                    callSiteAlign, /*offsetValue=*/nullptr);
    }

    // Step 3: If assume_aligned has an offset and wasn't consumed as a
    // call-site attribute, emit it as a separate assumption (becomes
    // llvm.assume during lowering).
    if (assumeAlignedAttr && !assumeAlignedConsumed) {
      val = emitAlignmentAssumption(val, retTy, SourceLocation(),
                                    assumeAlignedAttr->getLocation(),
                                    assumeAlignedVal, assumeAlignedOffset);
    }

    // Step 4: If alloc_align wasn't consumed as a call-site attribute,
    // emit it as an assumption (becomes llvm.assume during lowering).
    // This handles both dynamic values and constant non-power-of-2 values.
    if (allocAlignAttr && !allocAlignConsumed) {
      if (allocAlignDynamicValue) {
        // Dynamic value: use AssumeAlignedOp with SSA alignment_value.
        val = cir::AssumeAlignedOp::create(
            builder, getLoc(allocAlignAttr->getLocation()), val,
            /*alignment=*/mlir::IntegerAttr{}, allocAlignDynamicValue,
            /*offset=*/nullptr);
      } else {
        // Constant but non-power-of-2 (UB land per OGCG): emit as constant
        // alignment assumption. LowerToLLVM will emit as llvm.assume since
        // the value is not a power-of-2.
        val = emitAlignmentAssumption(val, retTy, SourceLocation(),
                                      allocAlignAttr->getLocation(),
                                      allocAlignConstVal,
                                      /*offsetValue=*/nullptr);
      }
    }

    return RValue::get(val);
  };

  switch (getEvaluationKind(retTy)) {
  case cir::TEK_Aggregate: {
    Address destPtr = returnValue.getValue();
    bool destIsVolatile = returnValue.isVolatile();

    if (!destPtr.isValid()) {
      destPtr = createMemTemp(retTy, callLoc, getCounterAggTmpAsString());
      destIsVolatile = false;
    }

    mlir::ResultRange results = theCall->getOpResults();
    assert(results.size() <= 1 && "multiple returns from a call");
    auto emitCallResultTo = [&](Address dest, bool isVolatile) {
      emitAggregateStore(results[0], dest, isVolatile);
    };

    mlir::Region *region = builder.getBlock()->getParent();
    if (region != theCall->getParentRegion()) {
      // The call result is inside a synthetic try region.
      //
      // Prefer storing directly to the final destination when that pointer
      // dominates the call; this avoids introducing an extra alloca and copy.
      // Otherwise keep the temporary+copy bridge to preserve SSA dominance.
      //
      // For non-trivially-copyable aggregates, we must always store directly
      // to the destination. Using memcpy (cir.copy) to bridge between a
      // temporary and the destination is incorrect for such types because it
      // bypasses the copy/move constructor, leaving the destination object
      // with mismatched construction/destruction (e.g., the object is never
      // registered in any tracking data structures that the constructor
      // would have updated). Entry-block allocas always dominate nested
      // regions, so this is safe for the typical case.
      mlir::DominanceInfo domInfo(curFn);
      bool destDominatesCall =
          domInfo.dominates(destPtr.getPointer(), theCall.getOperation());
      if (destDominatesCall || !retTy.isTriviallyCopyableType(getContext())) {
        SourceLocRAIIObject loc{*this, callLoc};
        emitCallResultTo(destPtr, destIsVolatile);
        return RValue::getAggregate(destPtr, destIsVolatile);
      }

      // Fallback: destination doesn't dominate the call region, so bridge via
      // a temporary that is visible from both regions.
      Address tryResultAddr = createMemTemp(retTy, callLoc, "tmp.try.call.res");
      SourceLocRAIIObject loc{*this, callLoc};
      emitCallResultTo(tryResultAddr, /*destIsVolatile=*/false);

      if (tryResultAddr.getPointer() != destPtr.getPointer()) {
        // Use cir.copy (memcpy) instead of loading the whole aggregate and
        // storing it. For large types (e.g. std::array<float, 48000>), a
        // whole-aggregate load + store causes SROA to decompose into N
        // individual element ops, creating an O(N²) InstCombine pathology.
        // Memcpy avoids this: LLVM handles it as a single operation.
        builder.createCopy(destPtr.getPointer(), tryResultAddr.getPointer(),
                           destIsVolatile);
      }
      return RValue::getAggregate(destPtr, destIsVolatile);
    }

    SourceLocRAIIObject loc{*this, callLoc};
    emitCallResultTo(destPtr, destIsVolatile);
    return RValue::getAggregate(destPtr, destIsVolatile);
  }
  case cir::TEK_Scalar: {
    mlir::ResultRange results = theCall->getOpResults();
    assert(results.size() == 1 && "unexpected number of returns");

    // If the argument doesn't match, perform a bitcast to coerce it. This
    // can happen due to trivial type mismatches.
    if (results[0].getType() != retCIRTy) {
      auto srcPtrTy = mlir::dyn_cast<cir::PointerType>(results[0].getType());
      auto dstPtrTy = mlir::dyn_cast<cir::PointerType>(retCIRTy);
      if (srcPtrTy && dstPtrTy) {
        mlir::Value coerced =
            builder.createBitcast(loc, results[0], retCIRTy);
        return applyAssumeAligned(RValue::get(coerced));
      }
      // For non-pointer type mismatches (e.g. integer coercion from ABI),
      // use a bitcast. This matches OGCG's unconditional CreateBitCast.
      mlir::Value coerced =
          builder.createBitcast(loc, results[0], retCIRTy);
      return applyAssumeAligned(RValue::get(coerced));
    }

    mlir::Region *region = builder.getBlock()->getParent();
    if (region != theCall->getParentRegion()) {
      // The call result is inside a synthetic try region. Store it to a
      // temporary and load it from outside.
      Address destPtr = returnValue.getValue();

      if (!destPtr.isValid())
        destPtr = createMemTemp(retTy, callLoc, "tmp.try.call.res");

      return applyAssumeAligned(
          getRValueThroughMemory(callLoc, builder, results[0], destPtr));
    }

    return applyAssumeAligned(RValue::get(results[0]));
  }
  case cir::TEK_Complex: {
    mlir::ResultRange results = theCall->getOpResults();
    assert(!results.empty() &&
           "Expected at least one result for complex rvalue");

    mlir::Region *region = builder.getBlock()->getParent();
    if (region != theCall->getParentRegion()) {
      // The call result is inside a synthetic try region. Store it to a
      // temporary and load it from outside, matching the scalar path.
      Address destPtr = returnValue.getValue();
      if (!destPtr.isValid())
        destPtr = createMemTemp(retTy, callLoc, "tmp.try.call.res");
      // getRValueThroughMemory returns RValue::get (scalar) but we need
      // RValue::getComplex. Extract the loaded value and re-wrap it.
      RValue rv = getRValueThroughMemory(callLoc, builder, results[0], destPtr);
      return RValue::getComplex(rv.getValue());
    }

    return RValue::getComplex(results[0]);
  }
  }
  llvm_unreachable("Invalid evaluation kind");
}

void CallArg::copyInto(CIRGenFunction &cgf, Address addr,
                       mlir::Location loc) const {
  LValue dst = cgf.makeAddrLValue(addr, ty);
  if (!hasLV && rv.isScalar())
    cgf.emitStoreOfScalar(rv.getValue(), dst, /*isInit=*/true);
  else if (!hasLV && rv.isComplex())
    cgf.emitStoreOfComplex(loc, rv.getComplexValue(), dst, /*isInit=*/true);
  else {
    Address srcAddr = hasLV ? lv.getAddress() : rv.getAggregateAddress();
    LValue srcLV = cgf.makeAddrLValue(srcAddr, ty);
    // We assume that call args are never copied into subobjects.
    cgf.emitAggregateCopy(dst, srcLV, ty, AggValueSlot::DoesNotOverlap,
                          hasLV ? lv.isVolatileQualified()
                                : rv.isVolatileQualified());
  }
  isUsed = true;
}

static bool isProvablyNull(CIRGenFunction &cgf, Address addr) {
  if (auto constOp = addr.getBasePointer().getDefiningOp<cir::ConstantOp>())
    return cgf.getBuilder().isNullValue(constOp.getValue());
  return false;
}

static bool isProvablyNonNull(Address addr) { return addr.isKnownNonNull(); }

static const Expr *maybeGetUnaryAddrOfOperand(const Expr *e) {
  if (const auto *uop = dyn_cast<UnaryOperator>(e->IgnoreParens()))
    if (uop->getOpcode() == UO_AddrOf)
      return uop->getSubExpr();
  return nullptr;
}

static void emitARCIntrinsicUse(CIRGenFunction &cgf, mlir::Value value,
                                mlir::Location loc) {
  auto voidTy = cir::VoidType::get(cgf.getBuilder().getContext());
  auto funcTy = cir::FuncType::get({}, voidTy, /*isVarArg=*/true);
  cir::FuncOp useFn;
  if (auto *existing = cgf.cgm.getGlobalValue("llvm.objc.clang.arc.use"))
    useFn = mlir::cast<cir::FuncOp>(existing);
  else
    useFn = cgf.cgm.createCIRFunction(
        mlir::UnknownLoc::get(&cgf.cgm.getMLIRContext()),
        "llvm.objc.clang.arc.use", funcTy, nullptr);
  auto use = cgf.getBuilder().createCallOp(loc, useFn, {value});
  use->setAttr("nothrow", mlir::UnitAttr::get(cgf.getBuilder().getContext()));
}

static void emitWriteback(CIRGenFunction &cgf,
                          const CallArgList::Writeback &writeback) {
  const LValue &srcLV = writeback.source;
  Address srcAddr = srcLV.getAddress();
  assert(!isProvablyNull(cgf, srcAddr) &&
         "shouldn't have writeback for provably null argument");

  mlir::Location loc = srcAddr.getPointer().getLoc();

  if (writeback.writebackExpr) {
    cgf.emitIgnoredExpr(writeback.writebackExpr);
    cgf.getBuilder().createLifetimeEnd(loc, writeback.temporary.getPointer());
    return;
  }

  auto emitStoreBack = [&]() {
    mlir::Value value =
        cgf.getBuilder().createLoad(loc, writeback.temporary, /*isVolatile=*/false);
    if (value.getType() != srcAddr.getElementType())
      value = cgf.getBuilder().createBitcast(loc, value, srcAddr.getElementType());

    Qualifiers::ObjCLifetime lifetime = srcLV.getQuals().getObjCLifetime();
    if (writeback.toUse) {
      assert(lifetime == Qualifiers::OCL_Strong);
      value = cgf.emitARCRetainNonBlock(value, loc);
      emitARCIntrinsicUse(cgf, writeback.toUse, loc);
      mlir::Value oldValue = cgf.emitLoadOfScalar(srcLV, SourceLocation());
      cgf.emitStoreThroughLValue(RValue::get(value), srcLV);
      cgf.emitARCRelease(oldValue, loc);
      return;
    }

    if (lifetime == Qualifiers::OCL_Strong) {
      mlir::Value oldValue = cgf.emitLoadOfScalar(srcLV, SourceLocation());
      value = cgf.emitARCRetainNonBlock(value, loc);
      cgf.emitStoreThroughLValue(RValue::get(value), srcLV);
      cgf.emitARCRelease(oldValue, loc);
      return;
    }

    if (lifetime == Qualifiers::OCL_Weak) {
      cgf.emitARCStoreWeak(srcAddr, value, /*ignored=*/true);
      return;
    }

    cgf.emitStoreThroughLValue(RValue::get(value), srcLV);
  };

  if (isProvablyNonNull(srcAddr)) {
    emitStoreBack();
    return;
  }

  mlir::Value nullPtr =
      cgf.getBuilder().getNullValue(srcAddr.getPointer().getType(), loc);
  mlir::Value isNull = cir::CmpOp::create(
      cgf.getBuilder(), loc, cir::CmpOpKind::eq, srcAddr.getPointer(), nullPtr);
  cir::IfOp::create(
      cgf.getBuilder(), loc, isNull, /*withElseRegion=*/true,
      [&](mlir::OpBuilder &, mlir::Location) { cgf.getBuilder().createYield(loc); },
      [&](mlir::OpBuilder &, mlir::Location) {
        emitStoreBack();
        cgf.getBuilder().createYield(loc);
      });
}

static void emitWritebacks(CIRGenFunction &cgf, const CallArgList &args) {
  for (const auto &writeback : args.getWritebacks())
    emitWriteback(cgf, writeback);
}

static void emitWritebackArg(CIRGenFunction &cgf, CallArgList &args,
                             const ObjCIndirectCopyRestoreExpr *cre) {
  LValue srcLV;
  if (const Expr *lvExpr = maybeGetUnaryAddrOfOperand(cre->getSubExpr()))
    srcLV = cgf.emitLValue(lvExpr);
  else {
    Address srcAddr = cgf.emitPointerWithAlignment(cre->getSubExpr());
    QualType srcAddrType =
        cre->getSubExpr()->getType()->castAs<PointerType>()->getPointeeType();
    srcLV = cgf.makeAddrLValue(srcAddr, srcAddrType);
  }

  Address srcAddr = srcLV.getAddress();
  mlir::Location loc = cgf.getLoc(cre->getExprLoc());
  auto destPtrTy = mlir::cast<cir::PointerType>(cgf.convertType(cre->getType()));
  mlir::Type destElemTy = cgf.convertTypeForMem(cre->getType()->getPointeeType());

  if (isProvablyNull(cgf, srcAddr)) {
    args.add(RValue::get(cgf.getBuilder().getNullValue(destPtrTy, loc)),
             cre->getType());
    return;
  }

  Address temp = cgf.createTempAlloca(destElemTy, cgf.getPointerAlign(), loc,
                                      "icr.temp");
  mlir::Value finalArgument = temp.getPointer();
  if (finalArgument.getType() != destPtrTy)
    finalArgument = cgf.getBuilder().createBitcast(loc, finalArgument, destPtrTy);

  mlir::Value valueToUse;
  bool shouldCopy = cre->shouldCopy();
  if (!shouldCopy) {
    cgf.getBuilder().createStore(
        loc, cgf.getBuilder().getNullValue(destElemTy, loc), temp);
  } else if (isProvablyNonNull(srcAddr)) {
    RValue srcRV = cgf.emitLoadOfLValue(srcLV, SourceLocation());
    assert(srcRV.isScalar());
    mlir::Value src = srcRV.getValue();
    if (src.getType() != destElemTy)
      src = cgf.getBuilder().createBitcast(loc, src, destElemTy);
    cgf.getBuilder().createStore(loc, src, temp);
    if (cgf.cgm.getCodeGenOpts().OptimizationLevel != 0 &&
        srcLV.getQuals().getObjCLifetime() == Qualifiers::OCL_Strong)
      valueToUse = src;
  } else {
    mlir::Value srcNull =
        cgf.getBuilder().getNullValue(srcAddr.getPointer().getType(), loc);
    mlir::Value isNull = cir::CmpOp::create(
        cgf.getBuilder(), loc, cir::CmpOpKind::eq, srcAddr.getPointer(), srcNull);
    finalArgument = cgf.getBuilder().createSelect(
        loc, isNull, cgf.getBuilder().getNullValue(destPtrTy, loc), finalArgument);

    CIRGenFunction::ConditionalEvaluation condEval(cgf);
    cir::IfOp::create(
        cgf.getBuilder(), loc, isNull, /*withElseRegion=*/true,
        [&](mlir::OpBuilder &, mlir::Location) {
          cgf.getBuilder().createYield(loc);
        },
        [&](mlir::OpBuilder &, mlir::Location) {
          condEval.beginEvaluation();
          RValue srcRV = cgf.emitLoadOfLValue(srcLV, SourceLocation());
          assert(srcRV.isScalar());
          mlir::Value src = srcRV.getValue();
          if (src.getType() != destElemTy)
            src = cgf.getBuilder().createBitcast(loc, src, destElemTy);
          cgf.getBuilder().createStore(loc, src, temp);
          condEval.endEvaluation();
          cgf.getBuilder().createYield(loc);
        });
  }

  args.addWriteback(srcLV, temp, valueToUse);
  args.add(RValue::get(finalArgument), cre->getType());
}

cir::CallOp CIRGenFunction::emitRuntimeCallOp(mlir::Location loc,
                                              cir::FuncOp callee,
                                              ArrayRef<mlir::Value> args) {
  // OGCG may attach convergence control tokens for convergent operations.
  // CIR runtime call emission does not model explicit token threading here.
  cir::CallOp call =
      builder.createCallOp(loc, callee, args, callee.getCallingConv());
  if (isSanitizerScope)
    call->setAttr("cir.nosanitize", mlir::UnitAttr::get(&cgm.getMLIRContext()));
  assert(call->getNumResults() <= 1 &&
         "runtime functions have at most 1 result");

  return call;
}

mlir::Value CIRGenFunction::emitRuntimeCall(mlir::Location loc,
                                            cir::FuncOp callee,
                                            ArrayRef<mlir::Value> args) {
  cir::CallOp call = emitRuntimeCallOp(loc, callee, args);

  if (call->getNumResults() == 0)
    return nullptr;

  return call->getResult(0);
}

cir::CIRCallOpInterface
CIRGenFunction::emitRuntimeCallOrInvoke(mlir::Location loc, cir::FuncOp callee,
                                        ArrayRef<mlir::Value> args) {
  SmallVector<mlir::Value, 8> runtimeArgs(args.begin(), args.end());
  cir::CIRCallOpInterface call = emitCallLikeOp(
      *this, loc,
      /*indirectFuncTy=*/{}, /*indirectFuncVal=*/{}, callee, runtimeArgs,
      /*isInvoke=*/isCatchOrCleanupRequired(), callee.getCallingConv(),
      cir::SideEffect::All, cir::ExtraFuncAttributesAttr{});
  if (isSanitizerScope)
    call->setAttr("cir.nosanitize", mlir::UnitAttr::get(&cgm.getMLIRContext()));
  if (isEmittingEHCleanup)
    call->setAttr("cir.terminate_on_throw",
                  mlir::UnitAttr::get(&cgm.getMLIRContext()));
  assert(call->getNumResults() <= 1 &&
         "runtime functions have at most 1 result");
  return call;
}

mlir::Value CIRGenFunction::emitNounwindRuntimeCall(mlir::Location loc,
                                                    cir::FuncOp callee,
                                                    ArrayRef<mlir::Value> args) {
  cir::CallOp call =
      builder.createCallOp(loc, callee, args, callee.getCallingConv());
  call->setAttr("nothrow", mlir::UnitAttr::get(&cgm.getMLIRContext()));
  if (isSanitizerScope)
    call->setAttr("cir.nosanitize", mlir::UnitAttr::get(&cgm.getMLIRContext()));
  assert(call->getNumResults() <= 1 &&
         "runtime functions have at most 1 result");

  if (call->getNumResults() == 0)
    return nullptr;

  return call->getResult(0);
}

namespace {
/// Cleanup to destroy a callee-destructed temporary if the call never happens
/// (e.g., a subsequent argument evaluation throws). Matches OGCG's
/// DestroyUnpassedArg (CGCall.cpp).
struct DestroyUnpassedArg final : EHScopeStack::Cleanup {
  Address addr;
  QualType type;

  DestroyUnpassedArg(Address addr, QualType type) : addr(addr), type(type) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    QualType::DestructionKind dtorKind = type.isDestructedType();
    if (dtorKind == QualType::DK_cxx_destructor) {
      const CXXDestructorDecl *dtor =
          type->getAsCXXRecordDecl()->getDestructor();
      assert(!dtor->isTrivial());
      cgf.emitCXXDestructorCall(dtor, Dtor_Complete, /*forVirtualBase=*/false,
                                /*delegating=*/false, addr, type);
    } else {
      cgf.cgm.errorNYI(addr.getPointer().getLoc(),
                       "DestroyUnpassedArg: non-CXX destructor");
    }
  }
};
} // namespace

void CIRGenFunction::emitCallArg(CallArgList &args, const clang::Expr *e,
                                 clang::QualType argType,
                                 bool *sawDefaultArg) {
  const bool isDefaultArg = isa<CXXDefaultArgExpr>(e);
  if (sawDefaultArg && isDefaultArg)
    *sawDefaultArg = true;

  std::optional<CallDebugLocRAIIObject> callDebugLoc;
  if (isDefaultArg && currSrcLoc)
    callDebugLoc.emplace(*this, *currSrcLoc);

  if (const auto *cre = dyn_cast<ObjCIndirectCopyRestoreExpr>(e)) {
    assert(getLangOpts().ObjCAutoRefCount);
    return emitWritebackArg(*this, args, cre);
  }

  assert(argType->isReferenceType() == e->isGLValue() &&
         "reference binding to unmaterialized r-value!");

  if (e->isGLValue()) {
    assert(e->getObjectKind() == OK_Ordinary);
    return args.add(emitReferenceBindingToExpr(e), argType);
  }

  bool hasAggregateEvalKind = hasAggregateEvaluationKind(argType);

  // In the Microsoft C++ ABI, aggregate arguments are destructed by the callee.
  // In Itanium ABI, this also applies to trivial_abi types that pass in
  // registers but have non-trivial destructors.
  // We still have to push an EH-only cleanup in case we unwind before
  // we make it to the call.
  if (argType->isRecordType() &&
      argType->castAsRecordDecl()->isParamDestroyedInCallee()) {
    AggValueSlot slot =
        createAggTemp(argType, getLoc(e->getSourceRange()), "agg.tmp");

    bool destroyedInCallee = true, needsCleanup = true;
    if (const auto *rd = argType->getAsCXXRecordDecl())
      destroyedInCallee = rd->hasNonTrivialDestructor();
    else
      needsCleanup = argType.isDestructedType();

    if (destroyedInCallee)
      slot.setExternallyDestructed();

    emitAggExpr(e, slot);
    RValue rv = slot.asRValue();
    args.add(rv, argType);

    if (destroyedInCallee && needsCleanup) {
      // Push a cleanup to destroy this temporary if an exception is thrown
      // during subsequent argument evaluation. The callee takes ownership
      // on successful call, so we deactivate the cleanup before the call.
      pushFullExprCleanup<DestroyUnpassedArg>(NormalAndEHCleanup,
                                              slot.getAddress(), argType);
      // Use nullptr for the isActive marker — CIR's deactivateCleanupBlock
      // handles nullptr by simply popping the cleanup.
      args.addArgCleanupDeactivation(ehStack.stable_begin(), nullptr);
    }

    return;
  }

  if (hasAggregateEvalKind && isa<ImplicitCastExpr>(e) &&
      cast<CastExpr>(e)->getCastKind() == CK_LValueToRValue &&
      !argType->isArrayParameterType() &&
      !argType.isNonTrivialToPrimitiveCopy()) {
    LValue lv = emitLValue(cast<CastExpr>(e)->getSubExpr());
    assert(lv.isSimple());
    args.addUncopiedAggregate(lv, argType);
    return;
  }

  args.add(emitAnyExprToTemp(e), argType);
}

QualType CIRGenFunction::getVarArgType(const Expr *arg) {
  // System headers on Windows define NULL to 0 instead of 0LL on Win64. MSVC
  // implicitly widens null pointer constants that are arguments to varargs
  // functions to pointer-sized ints.
  if (!getTarget().getTriple().isOSWindows())
    return arg->getType();

  if (arg->getType()->isIntegerType() &&
      getContext().getTypeSize(arg->getType()) <
          getContext().getTargetInfo().getPointerWidth(LangAS::Default) &&
      arg->isNullPointerConstant(getContext(),
                                 Expr::NPC_ValueDependentIsNotNull))
    return getContext().getIntPtrType();

  return arg->getType();
}

/// Similar to emitAnyExpr(), however, the result will always be accessible
/// even if no aggregate location is provided.
RValue CIRGenFunction::emitAnyExprToTemp(const Expr *e) {
  AggValueSlot aggSlot = AggValueSlot::ignored();

  if (hasAggregateEvaluationKind(e->getType())) {
    // When inside a conditional branch (ternary/if), create the aggregate
    // temporary in the FUNCTION entry block so that cleanup destructors
    // (which may fire outside the branch via deferCleanupsToOuterScope)
    // can access the temporary's address. Without this, the alloca would
    // be inside the branch region, and its address wouldn't dominate the
    // deferred cleanup code.
    if (isInConditionalBranch()) {
      mlir::Location loc = getLoc(e->getSourceRange());
      Address addr = createMemTemp(e->getType(), loc,
                                   getCounterAggTmpAsString(), nullptr,
                                   builder.getBestAllocaInsertPoint(
                                       getCurFunctionEntryBlock()));
      aggSlot = AggValueSlot::forAddr(
          addr, e->getType().getQualifiers(),
          AggValueSlot::IsNotDestructed, AggValueSlot::IsNotAliased,
          AggValueSlot::DoesNotOverlap);
    } else {
      aggSlot = createAggTemp(e->getType(), getLoc(e->getSourceRange()),
                              getCounterAggTmpAsString());
    }
  }

  return emitAnyExpr(e, aggSlot);
}

void CIRGenFunction::emitCallArgs(
    CallArgList &args, PrototypeWrapper prototype,
    llvm::iterator_range<clang::CallExpr::const_arg_iterator> argRange,
    AbstractCallee callee, unsigned paramsToSkip, EvaluationOrder order,
    bool *sawDefaultArg) {
  llvm::SmallVector<QualType, 16> argTypes;

  // OGCG extracts ExplicitCC from the prototype to determine whether inalloca
  // argument memory needs to be pre-allocated (MSVC ABI only). CIR does not
  // support inalloca yet, and lower-call rewriting rejects InAlloca
  // ABIArgInfo explicitly (NYI) in LowerFunction.cpp.

  // First, if a prototype was provided, use those argument types.
  bool isVariadic = false;
  if (prototype.p) {
    if (const auto *fpt =
            llvm::dyn_cast<const FunctionProtoType *>(prototype.p)) {
      isVariadic = fpt->isVariadic();
      // OGCG uses ExplicitCC here for hasInAllocaArgs check (MSVC ABI). CIR
      // defers this to target-lowering NYI handling for InAlloca ABI args.
      argTypes.assign(fpt->param_type_begin() + paramsToSkip,
                      fpt->param_type_end());
    } else {
      const auto *method = llvm::cast<const ObjCMethodDecl *>(prototype.p);
      // ObjC method call prototype information is carried by ObjCMethodDecl
      // rather than FunctionProtoType.
      isVariadic = method->isVariadic();
      auto params = method->parameters();
      assert(paramsToSkip <= params.size() &&
             "cannot skip more params than ObjC method has");
      for (const ParmVarDecl *param : params.drop_front(paramsToSkip))
        argTypes.push_back(param->getType());
    }
  }

  // If we still have any arguments, emit them using the type of the argument.
  for (const clang::Expr *a : llvm::drop_begin(argRange, argTypes.size()))
    argTypes.push_back(isVariadic ? getVarArgType(a) : a->getType());
  assert(argTypes.size() == (size_t)(argRange.end() - argRange.begin()));

  // We must evaluate arguments from right to left in the MS C++ ABI, because
  // arguments are destroyed left to right in the callee. As a special case,
  // there are certain language constructs that require left-to-right
  // evaluation, and in those cases we consider the evaluation order requirement
  // to trump the "destruction order is reverse construction order" guarantee.
  bool leftToRight =
      getTarget().getCXXABI().areArgsDestroyedLeftToRightInCallee()
          ? order == EvaluationOrder::ForceLeftToRight
          : order != EvaluationOrder::ForceRightToLeft;

  auto maybeEmitImplicitObjectSize = [&](size_t i, const Expr *arg,
                                         RValue emittedArg) {
    if (!callee.hasFunctionDecl() || i >= callee.getNumParams())
      return;
    auto *ps = callee.getParamDecl(i)->getAttr<PassObjectSizeAttr>();
    if (!ps)
      return;

    const clang::ASTContext &astContext = getContext();
    auto sizeTy = astContext.getSizeType();
    auto t = getBuilder().getUIntNTy(astContext.getTypeSize(sizeTy));
    assert(emittedArg.getValue() && "We emitted nothing for the arg?");
    auto v = evaluateOrEmitBuiltinObjectSize(
        arg, ps->getType(), t, emittedArg.getValue(), ps->isDynamic());
    args.add(RValue::get(v), sizeTy);
    // If we're emitting args in reverse, be sure to do so with
    // pass_object_size, as well.
    if (!leftToRight)
      std::swap(args.back(), *(&args.back() - 1));
  };

  // Evaluate each argument in the appropriate order.
  size_t callArgsStart = args.size();
  for (size_t i = 0; i != argTypes.size(); ++i) {
    size_t idx = leftToRight ? i : argTypes.size() - i - 1;
    CallExpr::const_arg_iterator currentArg = argRange.begin() + idx;
    size_t initialArgSize = args.size();

    emitCallArg(args, *currentArg, argTypes[idx], sawDefaultArg);

    // In particular, we depend on it being the last arg in Args, and the
    // objectsize bits depend on there only being one arg if !LeftToRight.
    assert(initialArgSize + 1 == args.size() &&
           "The code below depends on only adding one arg per emitCallArg");
    (void)initialArgSize;

    // Since pointer argument are never emitted as LValue, it is safe to emit
    // non-null argument check for r-value only.
    if (!args.back().hasLValue()) {
      RValue rvArg = args.back().getKnownRValue();
      emitNonNullArgCheck(rvArg, argTypes[idx], (*currentArg)->getExprLoc(),
                          callee, paramsToSkip + idx);
      maybeEmitImplicitObjectSize(idx, *currentArg, rvArg);
    }

  }

  if (!leftToRight) {
    // Un-reverse the arguments we just evaluated so they match up with the
    // IR function.
    std::reverse(args.begin() + callArgsStart, args.end());
    // OGCG also reverses the writebacks list here. Writebacks are ObjC/HLSL-only.
  }
}

void CIRGenFunction::emitNonNullArgCheck(RValue rv, QualType argType,
                                         SourceLocation argLoc,
                                         AbstractCallee callee,
                                         unsigned parmNum) {
  if (!callee.getDecl() ||
      !(sanOpts.has(SanitizerKind::NonnullAttribute) ||
        sanOpts.has(SanitizerKind::NullabilityArg)))
    return;

  const auto *pvd =
      parmNum < callee.getNumParams() ? callee.getParamDecl(parmNum) : nullptr;
  unsigned argNo = pvd ? pvd->getFunctionScopeIndex() : parmNum;

  const NonNullAttr *nnAttr = nullptr;
  if (sanOpts.has(SanitizerKind::NonnullAttribute))
    nnAttr = getNonNullAttr(callee.getDecl(), pvd, argType, argNo);

  bool canCheckNullability = false;
  if (sanOpts.has(SanitizerKind::NullabilityArg) && !nnAttr && pvd &&
      !pvd->getType()->isRecordType()) {
    auto nullability = pvd->getType()->getNullability();
    canCheckNullability =
        nullability && *nullability == NullabilityKind::NonNull &&
        pvd->getTypeSourceInfo();
  }

  if (!nnAttr && !canCheckNullability)
    return;

  SourceLocation attrLoc;
  llvm::StringRef handlerName;
  SanitizerMask sanitizerKind;
  uint8_t trapHandler = 16; // SanitizerHandler::NonnullArg
  if (nnAttr) {
    attrLoc = nnAttr->getLocation();
    handlerName = "nonnull_arg";
    sanitizerKind = SanitizerKind::NonnullAttribute;
  } else {
    attrLoc = pvd->getTypeSourceInfo()->getTypeLoc().findNullabilityLoc();
    handlerName = "nullability_arg";
    sanitizerKind = SanitizerKind::NullabilityArg;
    trapHandler = 14; // SanitizerHandler::NullabilityArg
  }

  mlir::Location loc = getLoc(argLoc);
  mlir::Value cond = emitNonNullRValueCheck(rv, argType);
  cond.getDefiningOp()->setAttr("cir.nosanitize",
                                mlir::UnitAttr::get(&getMLIRContext()));

  auto ifOp = cir::IfOp::create(
      builder, loc, cond, /*withElseRegion=*/true,
      [&](mlir::OpBuilder &, mlir::Location ifLoc) { builder.createYield(ifLoc); },
      [&](mlir::OpBuilder &, mlir::Location ifLoc) {
        if (cgm.getCodeGenOpts().SanitizeTrap.has(sanitizerKind)) {
          auto trap = cir::TrapOp::create(builder, ifLoc);
          trap->setAttr(
              "cir.ubsantrap_handler",
              mlir::IntegerAttr::get(
                  mlir::IntegerType::get(builder.getContext(), 8),
                  trapHandler));
          trap->setAttr("cir.nosanitize",
                        mlir::UnitAttr::get(&getMLIRContext()));
          return;
        }

        auto srcLocTy = builder.getAnonRecordTy(
            {builder.getUInt8PtrTy(), builder.getSInt32Ty(),
             builder.getSInt32Ty()});
        SmallVector<mlir::Attribute, 3> callLocFields;
        PresumedLoc callPLoc =
            getContext().getSourceManager().getPresumedLoc(argLoc);
        if (callPLoc.isValid()) {
          StringRef filename(callPLoc.getFilename());
          auto fileGV = cgm.getGlobalForConstantCString(ifLoc, filename, ".src");
          callLocFields.push_back(
              builder.getGlobalViewAttr(builder.getUInt8PtrTy(), fileGV));
          callLocFields.push_back(
              cir::IntAttr::get(builder.getSInt32Ty(), callPLoc.getLine()));
          callLocFields.push_back(
              cir::IntAttr::get(builder.getSInt32Ty(), callPLoc.getColumn()));
        } else {
          callLocFields.push_back(
              builder.getConstNullPtrAttr(builder.getUInt8PtrTy()));
          callLocFields.push_back(cir::IntAttr::get(builder.getSInt32Ty(), 0));
          callLocFields.push_back(cir::IntAttr::get(builder.getSInt32Ty(), 0));
        }

        SmallVector<mlir::Attribute, 3> attrLocFields;
        PresumedLoc attrPLoc =
            getContext().getSourceManager().getPresumedLoc(attrLoc);
        if (attrPLoc.isValid()) {
          StringRef filename(attrPLoc.getFilename());
          auto fileGV = cgm.getGlobalForConstantCString(ifLoc, filename, ".src");
          attrLocFields.push_back(
              builder.getGlobalViewAttr(builder.getUInt8PtrTy(), fileGV));
          attrLocFields.push_back(
              cir::IntAttr::get(builder.getSInt32Ty(), attrPLoc.getLine()));
          attrLocFields.push_back(
              cir::IntAttr::get(builder.getSInt32Ty(), attrPLoc.getColumn()));
        } else {
          attrLocFields.push_back(
              builder.getConstNullPtrAttr(builder.getUInt8PtrTy()));
          attrLocFields.push_back(cir::IntAttr::get(builder.getSInt32Ty(), 0));
          attrLocFields.push_back(cir::IntAttr::get(builder.getSInt32Ty(), 0));
        }

        auto nonNullArgTy = builder.getAnonRecordTy(
            {srcLocTy, srcLocTy, builder.getSInt32Ty()});
        auto init = cir::ConstRecordAttr::get(
            nonNullArgTy,
            mlir::ArrayAttr::get(
                &getMLIRContext(),
                {cir::ConstRecordAttr::get(
                     srcLocTy, mlir::ArrayAttr::get(&getMLIRContext(),
                                                    callLocFields)),
                 cir::ConstRecordAttr::get(
                     srcLocTy, mlir::ArrayAttr::get(&getMLIRContext(),
                                                    attrLocFields)),
                 cir::IntAttr::get(builder.getSInt32Ty(), argNo + 1)}));
        auto infoGV = CIRGenModule::createGlobalOp(
            cgm, ifLoc, cgm.getUniqueGlobalName(".nonnull_arg"), nonNullArgTy,
            /*isConstant=*/false);
        infoGV.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
        mlir::SymbolTable::setSymbolVisibility(infoGV,
                                               mlir::SymbolTable::Visibility::Private);
        infoGV->setAttr("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));
        infoGV.setAlignmentAttr(cgm.getSize(getPointerAlign()));
        CIRGenModule::setInitializer(infoGV, init);

        auto stubFnTy =
            cir::FuncType::get({builder.getPointerTo(nonNullArgTy)},
                               builder.getVoidTy());
        auto stubFn = cgm.createRuntimeFunction(
            stubFnTy, "__cir_ubsan_nonnull_arg_check", /*attrs=*/{},
            /*isLocal=*/false, /*assumeConvergent=*/false);
        auto infoPtr = builder.createGetGlobal(ifLoc, infoGV);
        auto call = builder.createCallOp(ifLoc, stubFn, {infoPtr});
        call->setDiscardableAttr("cir.ubsan.handler_name",
                                 builder.getStringAttr(handlerName));
        call->setDiscardableAttr(
            "cir.ubsan.handler_kind",
            builder.getStringAttr(cgm.getCodeGenOpts().SanitizeRecover.has(
                                          sanitizerKind)
                                      ? "recover"
                                      : "abort"));
        call->setDiscardableAttr("cir.nosanitize",
                                 mlir::UnitAttr::get(&getMLIRContext()));
        if (cgm.getCodeGenOpts().SanitizeRecover.has(sanitizerKind)) {
          auto yield = cir::YieldOp::create(builder, ifLoc);
          yield->setAttr("cir.nosanitize",
                         mlir::UnitAttr::get(&getMLIRContext()));
        } else {
          auto unreachable = cir::UnreachableOp::create(builder, ifLoc);
          unreachable->setAttr("cir.nosanitize",
                               mlir::UnitAttr::get(&getMLIRContext()));
        }
      });
  ifOp->setAttr(
      "cir.branch_weights",
      builder.getArrayAttr({builder.getI32IntegerAttr(1024 * 1024 - 1),
                            builder.getI32IntegerAttr(1)}));
  ifOp->setAttr("cir.nosanitize", mlir::UnitAttr::get(&getMLIRContext()));
}

// Emits an error if we don't have a valid set of target features for the
// called function. Mirrors OGCG's CodeGenFunction::checkTargetFeatures
// (CodeGenFunction.cpp). This overload handles builtin target-feature
// diagnostics, then falls back to the SourceLocation-based overload for the
// always_inline / flatten checks.
void CIRGenFunction::checkTargetFeatures(const CallExpr *e,
                                         const FunctionDecl *targetDecl) {
  if (!targetDecl)
    return;

  // SemaChecking cannot handle below x86 builtins because they have different
  // parameter ranges with different TargetAttribute of caller.
  if (getContext().getTargetInfo().getTriple().isX86()) {
    unsigned builtinID = targetDecl->getBuiltinID();
    if (builtinID == X86::BI__builtin_ia32_cmpps ||
        builtinID == X86::BI__builtin_ia32_cmpss ||
        builtinID == X86::BI__builtin_ia32_cmppd ||
        builtinID == X86::BI__builtin_ia32_cmpsd) {
      const FunctionDecl *fd = dyn_cast_or_null<FunctionDecl>(curCodeDecl);
      llvm::StringMap<bool> targetFeatureMap;
      getContext().getFunctionFeatureMap(targetFeatureMap, fd);
      llvm::APSInt result =
          *(e->getArg(2)->getIntegerConstantExpr(getContext()));
      if (result.getSExtValue() > 7 && !targetFeatureMap.lookup("avx"))
        cgm.getDiags().Report(e->getBeginLoc(), diag::err_builtin_needs_feature)
            << targetDecl->getDeclName() << "avx";
    }
  }

  unsigned builtinID = targetDecl->getBuiltinID();
  if (builtinID) {
    const FunctionDecl *fd = dyn_cast_or_null<FunctionDecl>(curCodeDecl);
    if (!fd)
      return;

    llvm::StringMap<bool> callerFeatureMap;
    getContext().getFunctionFeatureMap(callerFeatureMap, fd);

    bool isHipStdPar = getLangOpts().HIPStdPar && getLangOpts().CUDAIsDevice;
    StringRef featureList(getContext().BuiltinInfo.getRequiredFeatures(builtinID));
    if (!Builtin::evaluateRequiredTargetFeatures(featureList,
                                                 callerFeatureMap) &&
        !isHipStdPar) {
      cgm.getDiags().Report(e->getBeginLoc(), diag::err_builtin_needs_feature)
          << targetDecl->getDeclName() << featureList;
    }
    return;
  }

  checkTargetFeatures(e->getBeginLoc(), targetDecl);
}

// Emits an error if we don't have a valid set of target features for the
// called function. Mirrors OGCG's CodeGenFunction::checkTargetFeatures
// (CodeGenFunction.cpp). The builtin path is handled separately in
// emitCall(CIRGenFunctionInfo, ...) above and by the CallExpr overload above;
// this covers the always_inline / flatten paths that only need a
// SourceLocation.
void CIRGenFunction::checkTargetFeatures(SourceLocation loc,
                                         const FunctionDecl *targetDecl) {
  // Early exit if this is an indirect call.
  if (!targetDecl)
    return;

  // Get the current enclosing function if it exists. If it doesn't
  // we can't check the target features anyhow.
  const FunctionDecl *fd = dyn_cast_or_null<FunctionDecl>(curCodeDecl);
  if (!fd)
    return;

  bool isAlwaysInline = targetDecl->hasAttr<AlwaysInlineAttr>();
  bool isFlatten = fd->hasAttr<FlattenAttr>();

  // Only proceed if the callee is always_inline or the caller has flatten.
  if (!isAlwaysInline && !isFlatten)
    return;

  // Skip builtins — those are already checked in emitCall(CIRGenFunctionInfo).
  if (targetDecl->getBuiltinID())
    return;

  llvm::StringMap<bool> callerFeatureMap;
  getContext().getFunctionFeatureMap(callerFeatureMap, fd);

  // When compiling in HipStdPar mode we have to be conservative in rejecting
  // target specific features in the FE.
  bool isHipStdPar = getLangOpts().HIPStdPar && getLangOpts().CUDAIsDevice;

  if (!targetDecl->isMultiVersion() && targetDecl->hasAttr<TargetAttr>()) {
    // Get the required features for the callee.
    const TargetAttr *td = targetDecl->getAttr<TargetAttr>();
    ParsedTargetAttr parsedAttr =
        getContext().filterFunctionTargetAttrs(td);

    SmallVector<StringRef, 1> reqFeatures;
    llvm::StringMap<bool> calleeFeatureMap;
    getContext().getFunctionFeatureMap(calleeFeatureMap, targetDecl);

    for (const auto &f : parsedAttr.Features) {
      if (f[0] == '+' && calleeFeatureMap.lookup(f.substr(1)))
        reqFeatures.push_back(StringRef(f).substr(1));
    }

    for (const auto &f : calleeFeatureMap) {
      // Only positive features are "required".
      if (f.getValue())
        reqFeatures.push_back(f.getKey());
    }

    std::string missingFeature;
    if (!llvm::all_of(reqFeatures,
                      [&](StringRef feature) {
                        if (!callerFeatureMap.lookup(feature)) {
                          missingFeature = feature.str();
                          return false;
                        }
                        return true;
                      }) &&
        !isHipStdPar) {
      if (isAlwaysInline)
        cgm.getDiags().Report(loc, diag::err_function_needs_feature)
            << fd->getDeclName() << targetDecl->getDeclName()
            << missingFeature;
      else if (isFlatten)
        cgm.getDiags().Report(loc, diag::err_flatten_function_needs_feature)
            << fd->getDeclName() << targetDecl->getDeclName()
            << missingFeature;
    }
  } else if (!fd->isMultiVersion() && fd->hasAttr<TargetAttr>()) {
    llvm::StringMap<bool> calleeFeatureMap;
    getContext().getFunctionFeatureMap(calleeFeatureMap, targetDecl);

    for (const auto &f : calleeFeatureMap) {
      if (f.getValue() &&
          (!callerFeatureMap.lookup(f.getKey()) ||
           !callerFeatureMap.find(f.getKey())->getValue()) &&
          !isHipStdPar) {
        if (isAlwaysInline)
          cgm.getDiags().Report(loc, diag::err_function_needs_feature)
              << fd->getDeclName() << targetDecl->getDeclName() << f.getKey();
        else if (isFlatten)
          cgm.getDiags().Report(loc, diag::err_flatten_function_needs_feature)
              << fd->getDeclName() << targetDecl->getDeclName() << f.getKey();
      }
    }
  }
}
