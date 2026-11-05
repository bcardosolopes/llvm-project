#include "LowerCall.h"
#include "CIRToCIRArgMapping.h"
#include "LowerFunctionInfo.h"
#include "LowerModule.h"
#include "LowerTypes.h"
#include "clang/CIR/Dialect/IR/CIRDataLayout.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/FnInfoOpts.h"
#include "clang/CIR/MissingFeatures.h"
#include "clang/AST/DeclCXX.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/Support/ErrorHandling.h"

using namespace mlir;
using namespace cir;

using ABIArgInfo = cir::ABIArgInfo;
using FnInfoOpts = cir::FnInfoOpts;
using MissingFeatures = cir::MissingFeatures;

namespace {

/// Check if a CIR type has floating-point representation, matching OGCG's
/// QualType::hasFloatingRepresentation().
static bool hasFloatingRepresentation(mlir::Type ty) {
  if (isAnyFloatingPointType(ty))
    return true;
  if (auto complexTy = mlir::dyn_cast<cir::ComplexType>(ty))
    return isAnyFloatingPointType(complexTy.getElementType());
  if (auto vectorTy = mlir::dyn_cast<cir::VectorType>(ty))
    return isAnyFloatingPointType(vectorTy.getElementType());
  return false;
}

/// Mirror LLVM's nofpclass type legality check for lowered CIR types.
static bool isNoFPClassCompatibleType(mlir::Type ty) {
  if (isAnyFloatingPointType(ty))
    return true;
  if (auto complexTy = mlir::dyn_cast<cir::ComplexType>(ty))
    return isAnyFloatingPointType(complexTy.getElementType());
  if (auto vectorTy = mlir::dyn_cast<cir::VectorType>(ty))
    return isAnyFloatingPointType(vectorTy.getElementType());
  if (auto recordTy = mlir::dyn_cast<cir::RecordType>(ty)) {
    auto members = recordTy.getMembers();
    return !members.empty() &&
           llvm::all_of(members, [](mlir::Type memberTy) {
             return isNoFPClassCompatibleType(memberTy);
           });
  }
  return false;
}

/// Match OGCG's nofpclass gating: the source type must carry floating
/// semantics, and the ABI-lowered storage type must be legal for nofpclass.
static bool canApplyNoFPClass(const ABIArgInfo &AI, mlir::Type ty) {
  if (!hasFloatingRepresentation(ty))
    return false;

  mlir::Type loweredTy =
      AI.canHaveCoerceToType() ? AI.getCoerceToType() : nullptr;
  if (!loweredTy)
    loweredTy = ty;
  return isNoFPClassCompatibleType(loweredTy);
}

using TypeProvenance = LowerFunctionInfoArgInfo::TypeProvenance;

static TypeProvenance getTypeProvenance(bool isBitInt, bool isMatrix) {
  TypeProvenance provenance = TypeProvenance::None;
  if (isBitInt)
    provenance |= TypeProvenance::BitInt;
  if (isMatrix)
    provenance |= TypeProvenance::Matrix;
  return provenance;
}

static bool shouldPropagateCallsiteNonnull(mlir::FunctionOpInterface calleeFn,
                                           unsigned argNo) {
  auto cirFn = mlir::dyn_cast<cir::FuncOp>(calleeFn.getOperation());
  if (!cirFn)
    return false;

  auto ast = cirFn.getAstAttr();
  if (!ast)
    return false;

  const clang::FunctionDecl *fnDecl = nullptr;
  const clang::CXXMethodDecl *methodDecl = nullptr;

  if (auto methodAst = mlir::dyn_cast<cir::ASTCXXMethodDeclAttr>(ast)) {
    methodDecl = methodAst.getAst();
    fnDecl = methodDecl;
  } else if (auto ctorAst = mlir::dyn_cast<cir::ASTCXXConstructorDeclAttr>(
                 ast)) {
    methodDecl = ctorAst.getAst();
    fnDecl = methodDecl;
  } else if (auto convAst = mlir::dyn_cast<cir::ASTCXXConversionDeclAttr>(
                 ast)) {
    methodDecl = convAst.getAst();
    fnDecl = methodDecl;
  } else if (auto dtorAst = mlir::dyn_cast<cir::ASTCXXDestructorDeclAttr>(
                 ast)) {
    methodDecl = dtorAst.getAst();
    fnDecl = methodDecl;
  } else if (auto fnAst = mlir::dyn_cast<cir::ASTFunctionDeclAttr>(ast)) {
    fnDecl = fnAst.getAst();
  } else {
    return false;
  }

  if (methodDecl && methodDecl->isImplicitObjectMemberFunction() && argNo == 0)
    return true;

  if (!fnDecl)
    return false;

  unsigned numParams = fnDecl->getNumParams();
  unsigned numArgs = calleeFn.getNumArguments();
  if (numArgs < numParams)
    return false;

  // CIR function arguments can include an implicit prefix such as `this` and,
  // for some structors, a VTT parameter. Map the lowered CIR argument index
  // back to the corresponding source-level ParmVarDecl before deciding whether
  // OGCG would propagate call-site nonnull.
  unsigned implicitPrefixArgs = numArgs - numParams;
  if (argNo < implicitPrefixArgs)
    return false;

  unsigned paramIdx = argNo - implicitPrefixArgs;
  if (paramIdx >= numParams)
    return false;

  return fnDecl->getParamDecl(paramIdx)->getType()->isReferenceType();
}

static bool isCXXInstanceMethod(mlir::Attribute ast) {
  if (!ast)
    return false;

  if (auto methodAst = mlir::dyn_cast<cir::ASTCXXMethodDeclAttr>(ast))
    return methodAst.getAst()->isImplicitObjectMemberFunction();

  return mlir::isa<cir::ASTCXXConstructorDeclAttr,
                   cir::ASTCXXConversionDeclAttr,
                   cir::ASTCXXDestructorDeclAttr>(ast);
}

/// Arrange a call as unto a free function, except possibly with an
/// additional number of formal parameters considered required.
const LowerFunctionInfo &
arrangeFreeFunctionLikeCall(LowerTypes &LT, LowerModule &LM,
                            mlir::ValueRange args, const FuncType fnType,
                            unsigned numExtraRequiredArgs, bool chainCall,
                            bool instanceMethod,
                            TypeProvenance resultProvenance,
                            llvm::ArrayRef<TypeProvenance> argProvenances,
                            cir::CallingConv callingConv, bool allowCache,
                            bool hasRegParm, unsigned regParm) {
  cir_cconv_assert(args.size() >= numExtraRequiredArgs);

  // In most cases, there are no optional arguments.
  RequiredArgs required = RequiredArgs::All;

  // If we have a variadic prototype, the required arguments are the
  // extra prefix plus the arguments in the prototype.
  // FIXME(cir): Properly check if function is no-proto.
  if (/*IsPrototypedFunction=*/true) {
    if (fnType.isVarArg())
      required = RequiredArgs::forPrototypePlus(fnType, numExtraRequiredArgs);
    // pass_object_size implicit parameters are already materialized in
    // CIRGen (via emitCallArgs / maybeEmitImplicitObjectSize) and
    // reflected in FuncType, so no extra parameter adjustment needed here.
  }

  // TODO(cir): There's some CC stuff related to no-proto functions here, but
  // its skipped here since it requires CodeGen info. Maybe this information
  // could be embbed  in the FuncOp during CIRGen.

  FnInfoOpts opts = FnInfoOpts::None;
  if (chainCall)
    opts = opts | FnInfoOpts::IsChainCall;
  if (instanceMethod)
    opts = opts | FnInfoOpts::IsInstanceMethod;

  // Fast path: when call operands exactly match the callee prototype inputs,
  // avoid per-call materialization of a temporary arg type vector.
  auto fnInputs = fnType.getInputs();
  bool argsMatchPrototype = fnInputs.size() == args.size();
  if (argsMatchPrototype) {
    for (auto [idx, arg] : llvm::enumerate(args)) {
      if (arg.getType() != fnInputs[idx]) {
        argsMatchPrototype = false;
        break;
      }
    }
  }
  if (argsMatchPrototype) {
    return LT.arrangeLLVMFunctionInfo(fnType.getReturnType(), opts, fnInputs,
                                      resultProvenance, argProvenances,
                                      required, allowCache,
                                      callingConv, hasRegParm, regParm);
  }

  // Slow path: keep small calls allocation-free and reuse LowerTypes scratch
  // storage to avoid per-call heap churn in hot mismatch paths.
  auto &lastSigs = LT.getLastSlowPathCallSignatures();
  unsigned cacheSize = LT.getSlowPathCallSignatureCacheSize();
  auto tryMatchSlot = [&](unsigned slot) -> const LowerFunctionInfo * {
    auto &lastSig = lastSigs[slot];
    if (lastSig.valid && lastSig.FI &&
        lastSig.resultType == fnType.getReturnType() &&
        lastSig.resultProvenance == resultProvenance &&
        lastSig.opts == opts &&
        lastSig.callingConv == callingConv &&
        lastSig.hasRegParm == hasRegParm && lastSig.regParm == regParm &&
        lastSig.allowsOptionalArgs == required.allowsOptionalArgs()) {
      if (!lastSig.allowsOptionalArgs ||
          lastSig.numRequiredArgs == required.getNumRequiredArgs()) {
        if (lastSig.argTypes.size() == args.size() &&
            lastSig.argProvenances == argProvenances) {
          bool sameArgTypes = true;
          for (auto [idx, arg] : llvm::enumerate(args)) {
            if (lastSig.argTypes[idx] != arg.getType()) {
              sameArgTypes = false;
              break;
            }
          }
          if (sameArgTypes) {
            LT.setPreferredSlowPathCallSignatureSlot(slot);
            return lastSig.FI;
          }
        }
      }
    }
    return nullptr;
  };
  if (allowCache) {
    unsigned preferredSlot = LT.getPreferredSlowPathCallSignatureSlot();
    if (preferredSlot < cacheSize)
      if (const LowerFunctionInfo *fi = tryMatchSlot(preferredSlot))
        return *fi;
    for (unsigned slot = 0; slot < cacheSize; ++slot) {
      if (slot == preferredSlot)
        continue;
      if (const LowerFunctionInfo *fi = tryMatchSlot(slot))
        return *fi;
    }
  }
  auto &argTypes = LT.getCallArgTypesScratch();
  argTypes.clear();
  argTypes.reserve(args.size());
  for (const auto &a : args)
    argTypes.emplace_back(a.getType());
  const LowerFunctionInfo &fi =
      LT.arrangeLLVMFunctionInfo(fnType.getReturnType(), opts, argTypes,
                                 resultProvenance, argProvenances,
                                 required, allowCache, callingConv, hasRegParm,
                                 regParm);
  if (allowCache) {
    auto &lastSig = lastSigs[LT.getNextSlowPathCallSignatureSlot()];
    lastSig.valid = true;
    lastSig.resultType = fnType.getReturnType();
    lastSig.resultProvenance = resultProvenance;
    lastSig.opts = opts;
    lastSig.callingConv = callingConv;
    lastSig.hasRegParm = hasRegParm;
    lastSig.regParm = regParm;
    lastSig.allowsOptionalArgs = required.allowsOptionalArgs();
    lastSig.numRequiredArgs =
        required.allowsOptionalArgs() ? required.getNumRequiredArgs() : 0;
    lastSig.argTypes.assign(argTypes.begin(), argTypes.end());
    lastSig.argProvenances.assign(argProvenances.begin(), argProvenances.end());
    lastSig.FI = &fi;
    LT.setPreferredSlowPathCallSignatureSlot(
        LT.getNextSlowPathCallSignatureSlot());
    LT.advanceSlowPathCallSignatureSlot();
  }
  return fi;
}

} // namespace

/// Determine whether a function parameter or return value of the given CIR
/// type should be marked `noundef`. This is a CIR-level equivalent of the
/// classic CodeGen's DetermineNoUndef in CGCall.cpp.
///
/// The rules mirror OGCG:
///  - Indirect / IndirectAliased: the pointer itself is always well-defined.
///  - Extend: the extended value is well-defined.
///  - Direct: well-defined for scalar types (int, bool, float, pointer,
///    vector); conservatively false for aggregates (may have padding bits).
///  - Ignore: no parameter to annotate.
bool cir::determineNoUndef(mlir::Type ty, const ABIArgInfo &ai,
                           const CIRDataLayout &DL, bool CheckCoerce) {
  switch (ai.getKind()) {
  case ABIArgInfo::Indirect:
  case ABIArgInfo::IndirectAliased:
    // The pointer that replaces an indirect arg is always well-defined.
    return true;
  case ABIArgInfo::Extend:
    // Sign/zero-extended values are always well-defined.
    return true;
  case ABIArgInfo::Direct:
    // Unsized source CIR types such as OpenCL event_t-like opaques can lower
    // through target-specific direct forms, but they have no meaningful source
    // store size. Conservatively skip noundef instead of querying data layout.
    if (!cir::isSized(ty))
      return false;
    // If we're coercing to a type with a greater store size than the original,
    // we're introducing new undef bits.  Match OGCG DetermineNoUndef.
    // Use store size (byte-aligned) to match OGCG's ConvertTypeForMem
    // semantics, which rounds sub-byte types (e.g. <1 x i1>) up to
    // their memory representation (i8).
    // Scalar types have no padding bits -> noundef.
    // Check these BEFORE the coerce size check because CIR integer types
    // (like _BitInt(65)) have raw bit widths that differ from OGCG's
    // ConvertTypeForMem representation (which pads to power-of-2). The
    // coerce size check would incorrectly fail for these types.
    // Matches OGCG DetermineNoUndef: isBitIntType() returns true before
    // reaching the aggregate check.
    if (mlir::isa<IntType, BoolType, SingleType, DoubleType, FP16Type,
                  BF16Type, FP80Type, FP128Type, LongDoubleType,
                  PointerType>(ty))
      return true;
    if (CheckCoerce && ai.canHaveCoerceToType()) {
      mlir::Type coerceTy = ai.getCoerceToType();
      if (coerceTy &&
          llvm::TypeSize::isKnownGT(DL.getTypeStoreSize(coerceTy),
                                    DL.getTypeStoreSize(ty)))
        return false;
    }
    // Fixed-size vectors are noundef; scalable vectors have unknown size at
    // compile time and should not get noundef (matches OGCG which returns
    // false via the typeSizeEqualsStoreSize check for scalable types).
    if (auto vecTy = mlir::dyn_cast<cir::VectorType>(ty))
      return !vecTy.getIsScalable();
    // Complex types are noundef if their element type is noundef.
    if (auto complexTy = mlir::dyn_cast<cir::ComplexType>(ty))
      return cir::determineNoUndef(complexTy.getElementType(), ai, DL, false);
    return false;
  default:
    return false;
  }
}

bool cir::determineSwiftCoerceAndExpandNoUndef(mlir::Type ty,
                                               const ABIArgInfo &ai,
                                               const CIRDataLayout &DL) {
  if (!ai.isCoerceAndExpand() || !cir::isSized(ty))
    return false;

  auto vecTy = mlir::dyn_cast<cir::VectorType>(ty);
  if (!vecTy || vecTy.getIsScalable())
    return false;

  auto typeSize = DL.getTypeSizeInBits(ty);
  mlir::Type coerceTy = ai.getCoerceToType();
  if (coerceTy && cir::isSized(coerceTy) &&
      llvm::TypeSize::isKnownGT(DL.getTypeSizeInBits(coerceTy), typeSize))
    return false;

  return cir::determineNoUndef(vecTy.getElementType(),
                               ABIArgInfo::getDirect(), DL, false);
}

/// Update function with ABI-specific attributes.
///
/// NOTE(cir): Partially copies CodeGenModule::ConstructAttributeList, but
/// focuses on ABI/Target-related attributes.
void LowerModule::constructAttributeList(llvm::StringRef Name,
                                         const LowerFunctionInfo &FI,
                                         FuncOp CalleeInfo, FuncOp newFn,
                                         unsigned &CallingConv,
                                         bool AttrOnCallSite, bool IsThunk) {
  // Collect function IR attributes from the CC lowering.
  // We'll collect the paramete and result attributes later.
  // FIXME(cir): Codegen differentiates between CallConv and EffectiveCallConv,
  // but I don't think we need to do this here.
  CallingConv = FI.getCallingConvention();
  // OGCG sets noreturn here during calling convention lowering. CIR handles
  // noreturn ABI-agnostically — the attribute is set in CIRGen on FuncOp and
  // propagated to LLVM IR during LowerToLLVM lowering.
  // OGCG checks for CMSE non-secure calls here. CMSE is ARM Cortex-M
  // only (not AArch64), not a CIR target.

  // TODO(cir): Implement AddAttributesFromFunctionProtoType here.
  // TODO(cir): Implement AddAttributesFromOMPAssumes here.

  // TODO(cir): Skipping a bunch of AST queries here. We will need to partially
  // implement some of them as this section sets target-specific attributes
  // too.
  // if (TargetDecl) {
  //   [...]
  // }

  // NOTE(cir): The original code adds default and no-builtin attributes here as
  // well. These are ABI/Target-agnostic, so it would be better handled in
  // CIRGen.

  // Override some default IR attributes based on declaration-specific
  // information.
  // NOTE(cir): Skipping another set of AST queries here.

  // Collect attributes from arguments and return values.
  CIRToCIRArgMapping IRFunctionArgs(getContext(), FI);

  const ABIArgInfo &RetAI = FI.getReturnInfo();

  // Check if noundef analysis is disabled (-no-enable-noundef-analysis).
  // When disabled, skip all noundef attribute emission.
  bool enableNoundef = !getModule()->hasAttr("cir.no_noundef_analysis");

  // Mark return value as noundef when appropriate. The return type must not
  // be void and must not be returned indirectly (sret). Match OGCG's
  // HasStrictReturn() (CGCall.cpp:1906-1936): MSan forces strict returns even
  // in C / extern "C" mode to avoid caller-callee noundef mismatches.
  const auto &langOpts = getContext().getLangOpts();
  bool hasStrictReturn = langOpts.Sanitize.has(clang::SanitizerKind::Memory);
  if (!hasStrictReturn)
    hasStrictReturn = langOpts.CPlusPlus;
  // extern "C" functions follow C semantics unless MSan forces strict
  // returns above.
  if (hasStrictReturn &&
      !langOpts.Sanitize.has(clang::SanitizerKind::Memory) &&
      CalleeInfo && CalleeInfo->hasAttr("cir.extern_c"))
    hasStrictReturn = false;
  // OGCG also checks VarDecl extern "C" for indirect calls through
  // extern "C" function pointer variables (CGCall.cpp:1923-1926).
  // This is handled in LowerFunction.cpp's indirect call path via the
  // cir.callee_extern_c attribute set by CIRGen on the CallOp.
  // OGCG final check (CGCall.cpp:1930-1935): when -fno-strict-return is used,
  // only force noundef if the return type can't be safely dropped
  // (non-trivially copyable / non-trivial destructor) or the Return sanitizer
  // is active.
  if (hasStrictReturn && !getContext().getCodeGenOpts().StrictReturn) {
    // MayDropFunctionReturn: check if the return type can be safely dropped.
    bool mayDrop = true;
    mlir::Type retTy = FI.getReturnType();
    if (auto recordTy = mlir::dyn_cast<cir::RecordType>(retTy))
      mayDrop = !getRecordABIFacts(recordTy).nonTrivialDestructor;
    // If the return value can be safely dropped and no Return sanitizer is
    // active, skip noundef.
    if (mayDrop && !langOpts.Sanitize.has(clang::SanitizerKind::Return))
      hasStrictReturn = false;
  }
  // Check for cir.no_noundef_return — set by CIRGen for member pointer
  // returns where noundef would be incorrect.
  bool noNoundefReturn = false;
  if (CalleeInfo)
    noNoundefReturn = CalleeInfo->hasAttr("cir.no_noundef_return");
  if (enableNoundef && hasStrictReturn && !noNoundefReturn &&
      !RetAI.isIndirect() &&
      determineNoUndef(FI.getReturnType(), RetAI, getDataLayout()))
    newFn.setResultAttr(0, "cir.noundef", rewriter.getUnitAttr());

  switch (RetAI.getKind()) {
  case ABIArgInfo::Extend:
    if (RetAI.isSignExt())
      newFn.setResultAttr(0, "cir.signext", rewriter.getUnitAttr());
    else
      // FIXME(cir): Add a proper abstraction to create attributes.
      newFn.setResultAttr(0, "cir.zeroext", rewriter.getUnitAttr());
    [[fallthrough]];
  case ABIArgInfo::Direct:
    // If the return type was marked promotable by CIR codegen (e.g. wchar_t,
    // char32_t) but classified as Direct (not Extend), add signext/zeroext
    // only on targets that extend 32-bit C-promotable types (x86/x86_64).
    if (CalleeInfo && CalleeInfo.getNumResults() > 0 &&
        CalleeInfo.getResultAttr(0, "cir.promotable")) {
      if (auto intTy = mlir::dyn_cast<cir::IntType>(FI.getReturnType())) {
        const llvm::Triple &triple = getTarget().getTriple();
        if (triple.isX86()) {
          if (intTy.isSigned())
            newFn.setResultAttr(0, "cir.signext", rewriter.getUnitAttr());
          else
            newFn.setResultAttr(0, "cir.zeroext", rewriter.getUnitAttr());
        }
      }
    }
    if (RetAI.getInReg())
      cir_cconv_unreachable("InReg return attribute is NYI");
    // Add nofpclass for return value when fast-math flags disable NaN/Inf.
    // Matches OGCG CGCall.cpp:2717-2718. LangOpts may not have fast-math
    // flags propagated, so also read from the function's extra_attrs which
    // carry "no-infs-fp-math" and "no-nans-fp-math".
    {
      bool noInfs = false, noNans = false;
      // Check LangOpts first (may not be populated).
      const auto &langOpts = getContext().getLangOpts();
      noInfs = langOpts.NoHonorInfs;
      noNans = langOpts.NoHonorNaNs;
      // Also check function extra_attrs as fallback.
      if (!noInfs || !noNans) {
        if (CalleeInfo) {
          if (auto extraAttrs = CalleeInfo.getExtraAttrs()) {
            auto elems = extraAttrs->getElements();
            if (!noInfs) {
              if (auto a = mlir::dyn_cast_or_null<mlir::StringAttr>(
                      elems.get("no-infs-fp-math")))
                noInfs = a.getValue() == "true";
            }
            if (!noNans) {
              if (auto a = mlir::dyn_cast_or_null<mlir::StringAttr>(
                      elems.get("no-nans-fp-math")))
                noNans = a.getValue() == "true";
            }
          }
        }
      }
      if ((noInfs || noNans) && !RetAI.isIndirect()) {
        mlir::Type retTy = FI.getReturnType();
        if (canApplyNoFPClass(RetAI, retTy)) {
          unsigned mask = 0;
          if (noInfs) mask |= 0x204; // fcInf
          if (noNans) mask |= 0x3;   // fcNan
          newFn.setResultAttr(0, "cir.nofpclass",
                              rewriter.getI32IntegerAttr(mask));
        }
      }
    }
    break;
  case ABIArgInfo::Ignore:
  case ABIArgInfo::Indirect:
  case ABIArgInfo::CoerceAndExpand:
    break;
  default:
    cir_cconv_unreachable("Missing ABIArgInfo::Kind");
  }

  if (!IsThunk) {
    // Propagate reference-return attributes (nonnull, dereferenceable, align)
    // from the original FuncOp. CIRGen sets these where QualType info is
    // available; we forward them here so they survive FuncOp replacement.
    if (CalleeInfo && CalleeInfo.getNumResults() > 0) {
      if (CalleeInfo.getResultAttr(0, "cir.nonnull"))
        newFn.setResultAttr(0, "cir.nonnull", rewriter.getUnitAttr());
      if (auto derefAttr =
              CalleeInfo.getResultAttr(0, "cir.dereferenceable"))
        newFn.setResultAttr(0, "cir.dereferenceable", derefAttr);
      if (auto derefOrNullAttr =
              CalleeInfo.getResultAttr(0, "cir.dereferenceable_or_null"))
        newFn.setResultAttr(0, "cir.dereferenceable_or_null",
                            derefOrNullAttr);
      if (auto alignAttr = CalleeInfo.getResultAttr(0, "cir.align"))
        newFn.setResultAttr(0, "cir.align", alignAttr);
    }
  }

  // Attach attributes to sret parameter.
  if (RetAI.isIndirect()) {
    unsigned sretArgNo = IRFunctionArgs.getSRetArgNo();
    // Mark as sret with the return type so DirectToLLVM can emit sret(T).
    mlir::Type retTy = FI.getReturnType();
    newFn.setArgAttr(sretArgNo, "cir.sret", mlir::TypeAttr::get(retTy));
    if (RetAI.getInReg())
      newFn.setArgAttr(sretArgNo, "cir.inreg", rewriter.getUnitAttr());
    // The sret pointer does not alias any other pointer.
    newFn.setArgAttr(sretArgNo, "cir.noalias", rewriter.getUnitAttr());
    // The sret memory is writable and dead on unwind (matches OGCG
    // CGCall.cpp:2764-2765).
    newFn.setArgAttr(sretArgNo, "cir.writable", rewriter.getUnitAttr());
    newFn.setArgAttr(sretArgNo, "cir.dead_on_unwind", rewriter.getUnitAttr());
    // Record the required alignment in bytes. All callers of
    // ABIArgInfo::getIndirect now pass bytes (matching OGCG's CharUnits).
    unsigned alignBytes = RetAI.getIndirectAlign();
    newFn.setArgAttr(sretArgNo, "cir.align",
                     rewriter.getI64IntegerAttr(alignBytes));
  }

  // Apply `nonnull`, `dereferencable(N)` and `align N` to the `this` argument,
  // unless this is a thunk function.
  // FIXME: fix this properly, https://reviews.llvm.org/D100388
  unsigned ArgNo = 0;
  bool hasUsedSwiftIndirectResultSRet = RetAI.isIndirect();
  for (LowerFunctionInfo::const_arg_iterator I = FI.arg_begin(),
                                             E = FI.arg_end();
       I != E; ++I, ++ArgNo) {
    // Type ParamType = I->type;
    const ABIArgInfo &AI = I->info;
    llvm::SmallVector<NamedAttribute> Attrs;

    // Add attribute for padding argument, if necessary.
    if (IRFunctionArgs.hasPaddingArg(ArgNo) && AI.getInReg())
      newFn.setArgAttr(IRFunctionArgs.getPaddingArgNo(ArgNo), "cir.inreg",
                       rewriter.getUnitAttr());

    // Propagate 'noalias' from the original function (set by CIRGen for
    // __restrict__ parameters) to the new ABI-lowered function.
    if (CalleeInfo && ArgNo < CalleeInfo.getNumArguments() &&
        CalleeInfo.getArgAttr(ArgNo, "cir.noalias"))
      Attrs.push_back(
          rewriter.getNamedAttr("cir.noalias", rewriter.getUnitAttr()));

    // Propagate Swift parameter ABI attributes from the original function
    // to the new ABI-lowered function.
    if (CalleeInfo && ArgNo < CalleeInfo.getNumArguments()) {
      if (CalleeInfo.getArgAttr(ArgNo, "cir.swiftasync"))
        Attrs.push_back(
            rewriter.getNamedAttr("cir.swiftasync", rewriter.getUnitAttr()));
      else if (CalleeInfo.getArgAttr(ArgNo, "cir.swiftself"))
        Attrs.push_back(
            rewriter.getNamedAttr("cir.swiftself", rewriter.getUnitAttr()));
      else if (CalleeInfo.getArgAttr(ArgNo, "cir.swifterror"))
        Attrs.push_back(
            rewriter.getNamedAttr("cir.swifterror", rewriter.getUnitAttr()));
      if (CalleeInfo.getArgAttr(ArgNo, "cir.noescape"))
        Attrs.push_back(
            rewriter.getNamedAttr("cir.noescape", rewriter.getUnitAttr()));
      bool isSwiftIndirectResult =
          static_cast<bool>(CalleeInfo.getArgAttr(ArgNo,
                                                  "cir.swift_indirect_result"));
      if (isSwiftIndirectResult)
        Attrs.push_back(rewriter.getNamedAttr("cir.swift_indirect_result",
                                              rewriter.getUnitAttr()));
      if (isSwiftIndirectResult && !hasUsedSwiftIndirectResultSRet &&
          mlir::isa<cir::VoidType>(FI.getReturnType())) {
        auto ptrTy = mlir::dyn_cast<cir::PointerType>(I->type);
        cir_cconv_assert(ptrTy &&
                         "swift_indirect_result must lower through a pointer");
        Attrs.push_back(rewriter.getNamedAttr(
            "cir.sret", mlir::TypeAttr::get(I->type)));
        hasUsedSwiftIndirectResultSRet = true;
      }
    }

    // Mark argument as noundef when appropriate.
    // Skip if cir.no_noundef is set (e.g., for member pointer params),
    // UNLESS the param was converted to Indirect (byval) — byval pointers
    // are always well-defined regardless of the original type.
    bool noNoundefParam = false;
    if (CalleeInfo && ArgNo < CalleeInfo.getNumArguments() &&
        !AI.isIndirect() && !AI.isIndirectAliased())
      noNoundefParam =
          CalleeInfo.getArgAttr(ArgNo, "cir.no_noundef") != nullptr;
    bool addNoUndef = determineNoUndef(I->type, AI, getDataLayout());
    if (!addNoUndef &&
        (CallingConv == llvm::CallingConv::Swift ||
         CallingConv == llvm::CallingConv::SwiftTail))
      addNoUndef =
          determineSwiftCoerceAndExpandNoUndef(I->type, AI, getDataLayout());
    if (enableNoundef && !noNoundefParam && addNoUndef)
      Attrs.push_back(
          rewriter.getNamedAttr("cir.noundef", rewriter.getUnitAttr()));

    // 'restrict' -> 'noalias' is done in EmitFunctionProlog when we
    // have the corresponding parameter variable.  It doesn't make
    // sense to do it here because parameters are so messed up.
    switch (AI.getKind()) {
    case ABIArgInfo::Extend:
      if (AI.isSignExt())
        Attrs.push_back(
            rewriter.getNamedAttr("cir.signext", rewriter.getUnitAttr()));
      else
        // FIXME(cir): Add a proper abstraction to create attributes.
        Attrs.push_back(
            rewriter.getNamedAttr("cir.zeroext", rewriter.getUnitAttr()));
      [[fallthrough]];
    case ABIArgInfo::Direct:
      // If CIR codegen marked this parameter as C-promotable (e.g. wchar_t,
      // char32_t — types >= 32 bits that CIR's type system cannot distinguish
      // from plain int), and the ABI classified it as Direct, add extension
      // only on targets that extend all C-promotable integers regardless of
      // width. x86/x86_64 extends all C-promotable ints; aarch64 only
      // extends types < 32 bits (Darwin is handled by the ABI returning
      // Extend directly).
      if (CalleeInfo && ArgNo < CalleeInfo.getNumArguments() &&
          CalleeInfo.getArgAttr(ArgNo, "cir.promotable")) {
        if (auto intTy = mlir::dyn_cast<cir::IntType>(I->type)) {
          const llvm::Triple &triple = getTarget().getTriple();
          if (triple.isX86()) {
            if (intTy.isSigned())
              Attrs.push_back(rewriter.getNamedAttr("cir.signext",
                                                     rewriter.getUnitAttr()));
            else
              Attrs.push_back(rewriter.getNamedAttr("cir.zeroext",
                                                     rewriter.getUnitAttr()));
          }
        }
      }
      if (ArgNo == 0 && FI.isChainCall())
        Attrs.push_back(
            rewriter.getNamedAttr("cir.nest", rewriter.getUnitAttr()));
      else if (AI.getInReg() && !IRFunctionArgs.hasPaddingArg(ArgNo))
        Attrs.push_back(
            rewriter.getNamedAttr("cir.inreg", rewriter.getUnitAttr()));
      if (unsigned align = AI.getDirectAlign())
        Attrs.push_back(rewriter.getNamedAttr(
            "cir.alignstack",
            rewriter.getIntegerAttr(rewriter.getI64Type(), align)));
      // Add nofpclass for FP parameters when fast-math flags disable NaN/Inf.
      // Matches OGCG CGCall.cpp:2741-2742. Read from function extra_attrs
      // since LangOpts may not be populated.
      {
        bool noInfs = false, noNans = false;
        const auto &langOpts = getContext().getLangOpts();
        noInfs = langOpts.NoHonorInfs;
        noNans = langOpts.NoHonorNaNs;
        if (!noInfs || !noNans) {
          if (CalleeInfo) {
            if (auto extraAttrs = CalleeInfo.getExtraAttrs()) {
              auto elems = extraAttrs->getElements();
              if (!noInfs) {
                if (auto a = mlir::dyn_cast_or_null<mlir::StringAttr>(
                        elems.get("no-infs-fp-math")))
                  noInfs = a.getValue() == "true";
              }
              if (!noNans) {
                if (auto a = mlir::dyn_cast_or_null<mlir::StringAttr>(
                        elems.get("no-nans-fp-math")))
                  noNans = a.getValue() == "true";
              }
            }
          }
        }
        if ((noInfs || noNans) && canApplyNoFPClass(AI, I->type)) {
          unsigned mask = 0;
          if (noInfs) mask |= 0x204; // fcInf
          if (noNans) mask |= 0x3;   // fcNan
          Attrs.push_back(rewriter.getNamedAttr(
              "cir.nofpclass", rewriter.getI32IntegerAttr(mask)));
        }
      }
      break;
    case ABIArgInfo::Indirect: {
      if (AI.getInReg())
        Attrs.push_back(
            rewriter.getNamedAttr("cir.inreg", rewriter.getUnitAttr()));

      if (AI.getIndirectByVal()) {
        Attrs.push_back(rewriter.getNamedAttr(
            "cir.byval", mlir::TypeAttr::get(I->type)));
        unsigned align = AI.getIndirectAlign();
        cir_cconv_assert(align != 0);
        Attrs.push_back(rewriter.getNamedAttr(
            "cir.align",
            rewriter.getI64IntegerAttr(align)));
      } else {
        // Non-byval indirect params: add dead_on_return when the object's
        // lifetime ends in the callee. Matches OGCG CGCall.cpp:2883-2887:
        //   if (!ParamType.isDestructedType() || !ParamType->isRecordType()
        //       || ParamType->castAsRecordDecl()->isParamDestroyedInCallee())
        // For record types with non-trivial destructors on Itanium ABI, the
        // CALLER is responsible for destruction, so the memory is NOT dead
        // after the call returns.
        bool addDeadOnReturn = true;
        if (auto recordTy = mlir::dyn_cast<cir::RecordType>(I->type)) {
          // C++ record with non-trivial destructor: only add dead_on_return
          // if the callee destroys the parameter.
          RecordABIFacts facts = getRecordABIFacts(recordTy);
          if (facts.nonTrivialDestructor && !facts.destroyedInCallee)
            addDeadOnReturn = false;
        }
        if (addDeadOnReturn)
          Attrs.push_back(rewriter.getNamedAttr(
              "cir.dead_on_return", rewriter.getUnitAttr()));
      }

      // Add noalias when -fpass-by-value-is-noalias is set and the record
      // type can be passed in registers. Matches OGCG CGCall.cpp:2891-2897.
      // For indirect parameters, I->type may be a pointer wrapping the record
      // type (CIRGen wraps indirect params as pointers), so look through it.
      if (getContext().getCodeGenOpts().PassByValueIsNoAlias) {
        mlir::Type paramTy = I->type;
        if (auto ptrTy = mlir::dyn_cast<cir::PointerType>(paramTy))
          paramTy = ptrTy.getPointee();
        if (auto recordTy = mlir::dyn_cast<cir::RecordType>(paramTy)) {
          if (auto ast = recordTy.getAst()) {
            const clang::RecordDecl *rd = ast.getRawDecl();
            if (rd && rd->getArgPassingRestrictions() ==
                          clang::RecordArgPassingKind::CanPassInRegs)
              Attrs.push_back(rewriter.getNamedAttr("cir.noalias",
                                                    rewriter.getUnitAttr()));
          }
        }
      }

      break;
    }
    case ABIArgInfo::IndirectAliased: {
      // byref attribute: the callee receives a pointer to read-only memory
      // (e.g., AMDGPU kernel args in constant address space).
      Attrs.push_back(rewriter.getNamedAttr(
          "cir.byref", mlir::TypeAttr::get(I->type)));
      unsigned align = AI.getIndirectAlign();
      if (align)
        Attrs.push_back(rewriter.getNamedAttr(
            "cir.align",
            rewriter.getI64IntegerAttr(align)));
      break;
    }
    case ABIArgInfo::Ignore:
      // Nothing to do for ignored arguments.
      break;
    case ABIArgInfo::CoerceAndExpand:
      // CoerceAndExpand arguments are expanded into individual IR args.
      // No special ABI attributes needed on each expanded arg.
      break;
    default:
      cir_cconv_unreachable("Missing ABIArgInfo::Kind");
    }

    // Propagate reference-parameter attributes (nonnull, dereferenceable,
    // align) from the original function. CIRGen sets these on the original
    // FuncOp where QualType information is available; we forward them here
    // so they survive the FuncOp replacement during target lowering.
    if (CalleeInfo && ArgNo < CalleeInfo.getNumArguments()) {
      if (CalleeInfo.getArgAttr(ArgNo, "cir.nonnull"))
        Attrs.push_back(
            rewriter.getNamedAttr("cir.nonnull", rewriter.getUnitAttr()));
      if (CalleeInfo.getArgAttr(ArgNo, "cir.nonnull") &&
          shouldPropagateCallsiteNonnull(CalleeInfo, ArgNo))
        Attrs.push_back(
            rewriter.getNamedAttr("cir.callsite_nonnull",
                                  rewriter.getUnitAttr()));
      if (auto derefAttr = CalleeInfo.getArgAttr(ArgNo, "cir.dereferenceable"))
        Attrs.push_back(rewriter.getNamedAttr("cir.dereferenceable", derefAttr));
      if (auto derefOrNullAttr =
              CalleeInfo.getArgAttr(ArgNo, "cir.dereferenceable_or_null"))
        Attrs.push_back(rewriter.getNamedAttr("cir.dereferenceable_or_null",
                                              derefOrNullAttr));
      if (auto alignAttr = CalleeInfo.getArgAttr(ArgNo, "cir.align"))
        Attrs.push_back(rewriter.getNamedAttr("cir.align", alignAttr));
      // Propagate target-set cir.byval (e.g. x86_intrcc interrupt handlers)
      // unless the ABI already classified this arg as indirect-byval.
      if (!AI.isIndirect() || !AI.getIndirectByVal()) {
        if (auto byvalAttr = CalleeInfo.getArgAttr(ArgNo, "cir.byval"))
          Attrs.push_back(rewriter.getNamedAttr("cir.byval", byvalAttr));
      }
    }

    // Swift error result handling at the call site is also needed for Swift
    // interop.
    assert(!cir::MissingFeatures::swiftErrorResult());

    if (!Attrs.empty()) {
      unsigned FirstIRArg, NumIRArgs;
      std::tie(FirstIRArg, NumIRArgs) = IRFunctionArgs.getIRArgs(ArgNo);
      for (unsigned i = 0; i < NumIRArgs; i++)
        newFn.setArgAttrs(FirstIRArg + i, Attrs);
    }
  }
  cir_cconv_assert(ArgNo == FI.arg_size());
}

/// Arrange the argument and result information for the declaration or
/// definition of the given function.
const LowerFunctionInfo &LowerTypes::arrangeFunctionDeclaration(FuncOp fnOp) {
  // qualifiedTypes is no longer tracked as a MissingFeatures guard.
  FuncType FTy = fnOp.getFunctionType();
  auto resultProvenance = getTypeProvenance(fnOp->hasAttr("cir.ret.bitint"),
                                            fnOp->hasAttr("cir.ret.matrix"));
  llvm::SmallVector<TypeProvenance, 8> argProvenances;
  argProvenances.reserve(fnOp.getNumArguments());
  auto bitIntArgs =
      fnOp->getAttrOfType<mlir::DenseBoolArrayAttr>("cir.bitint_args");
  auto matrixArgs =
      fnOp->getAttrOfType<mlir::DenseBoolArrayAttr>("cir.matrix_args");
  for (unsigned i = 0; i < fnOp.getNumArguments(); ++i) {
    bool isBitInt = bitIntArgs && i < bitIntArgs.size() && bitIntArgs[i];
    bool isMatrix = matrixArgs && i < matrixArgs.size() && matrixArgs[i];
    argProvenances.push_back(getTypeProvenance(isBitInt, isMatrix));
  }

  RequiredArgs required = RequiredArgs::forPrototypePlus(FTy, /*additional=*/0);
  FnInfoOpts opts =
      isCXXInstanceMethod(fnOp.getAstAttr()) ? FnInfoOpts::IsInstanceMethod
                                             : FnInfoOpts::None;
  bool hasRegParm = false;
  unsigned regParm = 0;
  if (auto regParmAttr = fnOp->getAttrOfType<mlir::IntegerAttr>("cir.regparm")) {
    hasRegParm = true;
    regParm = regParmAttr.getValue().getZExtValue();
  }
  return arrangeLLVMFunctionInfo(FTy.getReturnType(), opts, FTy.getInputs(),
                                 resultProvenance, argProvenances, required,
                                 /*allowCache=*/true, fnOp.getCallingConv(),
                                 hasRegParm, regParm);
}

/// Figure out the rules for calling a function with the given formal
/// type using the given arguments.  The arguments are necessary
/// because the function might be unprototyped, in which case it's
/// target-dependent in crazy ways.
const LowerFunctionInfo &
LowerTypes::arrangeFreeFunctionCall(mlir::ValueRange args,
                                    const FuncType fnType, bool chainCall,
                                    bool instanceMethod,
                                    TypeProvenance resultProvenance,
                                    llvm::ArrayRef<TypeProvenance> argProvenances,
                                    cir::CallingConv callingConv,
                                    bool allowCache, bool hasRegParm,
                                    unsigned regParm) {
  return arrangeFreeFunctionLikeCall(*this, LM, args, fnType,
                                     chainCall ? 1 : 0, chainCall,
                                     instanceMethod,
                                     resultProvenance, argProvenances,
                                     callingConv, allowCache, hasRegParm,
                                     regParm);
}

/// Arrange the argument and result information for the declaration or
/// definition of the given function.
const LowerFunctionInfo &
LowerTypes::arrangeFreeFunctionType(FuncType FTy,
                                    TypeProvenance resultProvenance,
                                    llvm::ArrayRef<TypeProvenance> argProvenances,
                                    cir::CallingConv callingConv) {
  // Free functions have no implicit prefix args. Arrange directly from the
  // FuncType input range so declaration/definition lowering avoids temporary
  // argument-type vector materialization.
  RequiredArgs required = RequiredArgs::forPrototypePlus(FTy, /*additional=*/0);
  return arrangeLLVMFunctionInfo(FTy.getReturnType(), FnInfoOpts::None,
                                 FTy.getInputs(), resultProvenance,
                                 argProvenances, required,
                                 /*allowCache=*/true, callingConv);
}

/// Arrange the argument and result information for the declaration or
/// definition of the given function.
const LowerFunctionInfo &LowerTypes::arrangeGlobalDeclaration(FuncOp fnOp) {
  return arrangeFunctionDeclaration(fnOp);
}

/// Arrange the argument and result information for an abstract value
/// of a given function type.  This is the method which all of the
/// above functions ultimately defer to.
///
/// \param resultType - ABI-agnostic CIR result type.
/// \param opts - Options to control the arrangement.
/// \param argTypes - ABI-agnostic CIR argument types.
/// \param required - Information about required/optional arguments.
const LowerFunctionInfo &LowerTypes::arrangeLLVMFunctionInfo(
    Type resultType, FnInfoOpts opts, ArrayRef<Type> argTypes,
    TypeProvenance resultProvenance, ArrayRef<TypeProvenance> argProvenances,
    RequiredArgs required, bool allowCache, cir::CallingConv callingConv,
    bool hasRegParm, unsigned regParm) {
  // qualifiedTypes is no longer tracked as a MissingFeatures guard.

  uint64_t key = 0;
  llvm::SmallVector<FunctionInfoCacheEntry, 1> *bucket = nullptr;
  if (allowCache) {
    key = static_cast<uint64_t>(llvm::hash_combine(
        resultType, static_cast<unsigned>(resultProvenance),
        static_cast<unsigned>(opts), required.allowsOptionalArgs(),
        required.allowsOptionalArgs() ? required.getNumRequiredArgs() : 0u,
        static_cast<unsigned>(callingConv), hasRegParm, regParm, argTypes.size(),
        llvm::hash_combine_range(argTypes.begin(), argTypes.end()),
        llvm::hash_combine_range(argProvenances.begin(),
                                 argProvenances.end())));
    bucket = &functionInfos[key];
    for (const FunctionInfoCacheEntry &entry : *bucket) {
      if (entry.resultType != resultType ||
          entry.resultProvenance != resultProvenance || entry.opts != opts ||
          entry.callingConv != callingConv || entry.hasRegParm != hasRegParm ||
          entry.regParm != regParm || entry.argTypes != argTypes ||
          entry.argProvenances != argProvenances ||
          entry.allowsOptionalArgs != required.allowsOptionalArgs())
        continue;
      if (entry.allowsOptionalArgs &&
          entry.numRequiredArgs != required.getNumRequiredArgs())
        continue;
      return *entry.FI;
    }
  }

  LowerFunctionInfo *FI = nullptr;

  unsigned CC = cirCallConvToLLVMCallConv(callingConv);

  // Construct the function info. We co-allocate the ArgInfos.
  // NOTE(cir): This initial function info might hold incorrect data.
  bool isInstanceMethod =
      (opts & FnInfoOpts::IsInstanceMethod) == FnInfoOpts::IsInstanceMethod;
  bool isChainCall =
      (opts & FnInfoOpts::IsChainCall) == FnInfoOpts::IsChainCall;
  bool isDelegateCall =
      (opts & FnInfoOpts::IsDelegateCall) == FnInfoOpts::IsDelegateCall;
  FI = LowerFunctionInfo::create(CC, isInstanceMethod, isChainCall,
                                 isDelegateCall, resultType, argTypes,
                                 argProvenances, required, hasRegParm, regParm);
  FI->getReturnArgInfo().provenance = resultProvenance;

  // Compute ABI information.
  // NOTE(cir): This corrects the initial function info data.
  getABIInfo().computeInfo(*FI); // FIXME(cir): Args should be set to null.

  // Loop over all of the computed argument and return value info. If any of
  // them are direct or extend without a specified coerce type, specify the
  // default now.
  cir::ABIArgInfo &retInfo = FI->getReturnInfo();
  if (retInfo.canHaveCoerceToType() && retInfo.getCoerceToType() == nullptr)
    retInfo.setCoerceToType(convertType(FI->getReturnType()));

  for (auto &I : FI->arguments())
    if (I.info.canHaveCoerceToType() && I.info.getCoerceToType() == nullptr)
      I.info.setCoerceToType(convertType(I.type));

  // Transparent unions are classified based on their first field type, but
  // the default coerce type is set from the full union type above. This would
  // either (a) attach signext/zeroext to a non-integer type for Extend, or
  // (b) flatten the union to multiple parameters for Direct. Fix the coerce
  // type to the first field type for transparent union arguments.
  for (auto &I : FI->arguments()) {
    if (!I.info.canHaveCoerceToType())
      continue;
    auto recTy = mlir::dyn_cast<cir::RecordType>(I.type);
    if (!recTy || !recTy.isUnion())
      continue;
    auto ast = recTy.getAst();
    if (!ast)
      continue;
    const clang::RecordDecl *rd = ast.getRawDecl();
    if (!rd || !rd->hasAttr<clang::TransparentUnionAttr>())
      continue;
    cir_cconv_assert(!recTy.getMembers().empty() &&
                     "sema created an empty transparent union");
    I.info.setCoerceToType(recTy.getMembers().front());
  }

  if (allowCache) {
    FunctionInfoCacheEntry entry;
    entry.FI.reset(FI);
    entry.resultType = resultType;
    entry.resultProvenance = resultProvenance;
    entry.opts = opts;
    entry.argTypes.assign(argTypes.begin(), argTypes.end());
    entry.argProvenances.assign(argProvenances.begin(), argProvenances.end());
    entry.allowsOptionalArgs = required.allowsOptionalArgs();
    if (entry.allowsOptionalArgs)
      entry.numRequiredArgs = required.getNumRequiredArgs();
    entry.callingConv = callingConv;
    entry.hasRegParm = hasRegParm;
    entry.regParm = regParm;
    cir_cconv_assert(bucket);
    bucket->push_back(std::move(entry));
  } else {
    // Nothing else takes ownership on this path, and the reference below
    // outlives this call, so park it for the lifetime of this LowerTypes.
    uncachedFunctionInfos.emplace_back(FI);
  }

  return *FI;
}
