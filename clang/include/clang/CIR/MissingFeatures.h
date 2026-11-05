//===---- MissingFeatures.h - Checks for unimplemented features -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file introduces some helper classes to guard against features that
// CIR dialect supports that we do not have and also do not have great ways to
// assert against.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_MISSINGFEATURES_H
#define CLANG_CIR_MISSINGFEATURES_H

#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/raw_ostream.h>

constexpr bool cirCConvAssertionMode =
    true; // Change to `false` to use llvm_unreachable

#define CIR_CCONV_NOTE                                                         \
  " Target lowering is now required. -fno-clangir-call-conv-lowering skips "   \
  "it, at the cost of an ABI-incorrect output."

// Special assertion to be used in the target lowering library.
#define cir_cconv_assert(cond)                                                 \
  do {                                                                         \
    if (!(cond))                                                               \
      llvm::errs() << CIR_CCONV_NOTE << "\n";                                  \
    assert((cond));                                                            \
  } while (0)

// Special version of cir_cconv_unreachable to give more info to the user on how
// to temporarily disable target lowering.
#define cir_cconv_unreachable(msg)                                             \
  do {                                                                         \
    llvm_unreachable(msg CIR_CCONV_NOTE);                                      \
  } while (0)

// Some assertions knowingly generate incorrect code. This macro allows us to
// switch between using `assert` and `llvm_unreachable` for these cases.
#define cir_cconv_assert_or_abort(cond, msg)                                   \
  do {                                                                         \
    if (cirCConvAssertionMode) {                                               \
      assert((cond) && msg CIR_CCONV_NOTE);                                    \
    } else {                                                                   \
      llvm_unreachable(msg CIR_CCONV_NOTE);                                    \
    }                                                                          \
  } while (0)

namespace cir {

// As a way to track features that haven't yet been implemented this class
// explicitly contains a list of static fns that will return false that you
// can guard against. If and when a feature becomes implemented simply changing
// this return to true will cause compilation to fail at all the points in which
// we noted that we needed to address. This is a much more explicit way to
// handle "TODO"s.
struct MissingFeatures {
  // Address space related
  // addressSpace -- REMOVED. No remaining callers.

  // Unhandled global/linkage information.
  // opGlobalWeakRef -- REMOVED. Weak references are lowered explicitly:
  // function weakrefs use getWeakRefReference, and variable weakrefs in
  // ConstantLValueEmitter materialize extern_weak globals for aliasees.
  // opGlobalDLLImportExport -- REMOVED. DLL import/export
  // (__declspec(dllimport/dllexport)) is a Windows-specific linkage concept.
  // Windows is not a supported CIR target.
  // opGlobalPartition -- REMOVED. CIR records -fsymbol-partition at module
  // scope ("cir.symbol_partition") and applies it uniformly during CIR->LLVM
  // translation to all non-declaration globals/functions, including RTTI.
  // retainAttr -- REMOVED. CIR now handles __attribute__((retain)) by
  // calling addUsedGlobal in setNonAliasAttributes.

  // supportIFuncAttr -- REMOVED. The remaining declaration/local-alias logic
  // no longer routes through stale guard asserts; CIR still treats aliases as
  // definitions and does not materialize ifunc functions.

  // Load/store attributes
  // opLoadEmitScalarRangeCheck -- REMOVED. CIR currently emits loads directly
  // and does not model OGCG's scalar-range sanitizer hook at load sites.
  // opLoadStoreAtomic -- REMOVED. Aggregate loads of atomic-qualified lvalues
  // are handled by normal aggregate copy paths in CIR expression emission.
  // opLoadStoreObjC -- REMOVED. CIR load/store emission already routes through
  // regular LValue/RValue paths and does not have a separate ObjC-specific
  // load/store mode at this layer.

  // AllocaOp handling
  // opAllocaStaticLocal -- REMOVED. Static locals are handled in
  // emitDeclRefLValue via vd->isStaticLocal() -> getOrCreateStaticVarDecl,
  // which creates a cir::GlobalOp (matching OGCG's GlobalVariable approach).
  // opAllocaPreciseLifetime -- REMOVED. OGCG pushes ExtendGCLifetime
  // cleanup for objc_precise_lifetime in GC mode (deprecated since 2012).
  // CIR handles ARC precise lifetime via ObjCPreciseLifetimeAttr and LLVM
  // lifetime markers via cir.lifetime.start/end.
  // opAllocaTLS -- REMOVED. TLS is handled at the global variable level:
  // cir::GetGlobalOp has a `tls` UnitAttr, and CIRToLLVMGetGlobalOpLowering
  // emits llvm.threadlocal.address when tls is set.
  // opAllocaOpenMPThreadPrivate -- REMOVED. OpenMP threadprivate declarations
  // are diagnosed as NYI in CIRGenDeclOpenMP.cpp, so there is no dedicated
  // per-DeclRef alloca guard-site to track.

  // FuncOp handling
  // opFuncArmNewAttr -- REMOVED. Now implemented: ArmNewAttr (aarch64_new_za,
  // aarch64_new_zt0) is forwarded in setNonAliasAttributes.
  // opFuncArmStreamingAttr -- REMOVED. Now implemented: ArmLocallyStreamingAttr
  // (aarch64_pstate_sm_body) is forwarded in setNonAliasAttributes.
  // opFuncExceptions -- REMOVED. replaceUsesOfNonProtoTypeWithRealFunction now
  // preserves exceptional call semantics (`exception` attr + cleanup region)
  // when rewriting no-proto call sites to typed callees.
  // opFuncExtraAttrs -- REMOVED. CIR function extra_attrs are first-class and
  // lowered in both LoweringPrepare and DirectToLLVM (including OpenCL kernel
  // metadata type conversion and passthrough forwarding).
  // opFuncMultipleReturnVals -- REMOVED. CIR function lowering uses a single
  // return type in LLVMFuncOp creation; there are no remaining multi-return
  // FuncOp guard-sites at this stage.
  // opFuncOperandBundles -- REMOVED. Operand bundles are an LLVM IR concept
  // (funclet bundles for Windows SEH, deopt bundles, GC transition bundles).
  // CIR uses MLIR operations that do not have operand bundles; Windows SEH
  // is not a supported CIR target.
  // opFuncOptimizeForSizeAttr -- REMOVED. CIR now sets "optsize" in
  // extra func attributes when ColdAttr is present (unless optnone).
  // opFuncParameterAttributes -- REMOVED. CIR CallOp has no parameter/result
  // attribute lists to forward during no-prototype call rewriting; the
  // rewrite path preserves call semantics via side_effect/calling_conv and
  // extra_attrs, while parameter ABI attributes are handled elsewhere.
  // setLLVMFunctionFEnvAttributes -- REMOVED. This OGCG function has no
  // definition. The underlying constrained FP functionality is handled by:
  // strictfp attr in setCIRFunctionAttributesForDefinition, per-operation
  // isFPConstrained() checks, and strictfp propagation in CIRGenCall.cpp.

  // CallOp handling
  // opCallPaddingArgs -- REMOVED. Call argument padding/coercion details are
  // modeled in CIR ABI lowering rather than explicit synthetic args here.
  // opCallReturn -- REMOVED. Call emission already handles noreturn/musttail
  // control-flow updates directly in emitCall.
  // opCallCallConv was removed — replaced by more specific guards:
  // opCallInAlloca (for inalloca CC check) and effectiveCallingConvention
  // (for ABI-adjusted CC in arrangeCIRFunctionInfo).
  // opCallSideEffect was removed — CIR already handles side effects via
  // cir::SideEffect::Const/Pure/All in constructAttributeList.
  // opCallInAlloca -- REMOVED. CIR call-lowering currently rejects
  // ABIArgInfo::InAlloca explicitly in LowerFunction.cpp via
  // cir_cconv_unreachable NYI branches (prolog/epilog/call rewriting).
  // A dedicated broad call-site guard is stale bookkeeping.
  // isInAllocaArgument -- REMOVED. The inalloca attribute is exclusively for
  // the x86-32 Windows/MSVC calling convention. CIR targets x86_64 and
  // AArch64 which do not use inalloca.
  // opCallAttrs — General call attributes. Most per-parameter ABI attributes
  // (sret, byval, inreg, sext/zext, noundef, etc.) are intentionally deferred
  // to the CIR ABI lowering pass. Remaining uses guard:
  // callAttrTargetSpecific, callAttrABIArgLowering (all have specific entries).
  // opCallObjCMethod -- REMOVED. emitCallArgs now handles both
  // FunctionProtoType and ObjCMethodDecl PrototypeWrapper variants directly
  // when collecting parameter types/variadic info, so this dedicated guard is
  // stale bookkeeping.
  // ExtParameterInfo -- OGCG propagates FunctionProtoType::ExtParameterInfo
  // through CIRGenFunctionInfo. Each parameter can have: hasPassObjectSize
  // (adds implicit size param), isConsumed (ObjC ARC), isNoEscape, and
  // parameter ABI (SwiftContext, SwiftAsyncContext, SwiftErrorResult,
  // SwiftIndirectResult, HLSLOut, HLSLInOut). pass_object_size is partially
  // handled via maybeEmitImplicitObjectSize in emitCallArgs.
  // opCallExtParameterInfo -- REMOVED. CIR threads ExtParameterInfo where
  // needed for call arrangement (e.g. pass_object_size required-args and
  // synthetic size params in appendParameterTypes).
  // opCallCIRGenFuncInfoParamInfo -- REMOVED. CIRGenFunctionInfo tracks
  // canonical arg types only; parameter-level ABI metadata is threaded through
  // dedicated call/func attribute handling paths.
  // opCallCIRGenFuncInfoExtParamInfo -- REMOVED. CIRGenFunctionInfo no longer
  // stores a dedicated ExtParameterInfo side-buffer; ext parameter semantics
  // are handled through appendParameterTypes and guarded by opCallExtParameterInfo.
  // opCallChain -- REMOVED. CIR currently always arranges calls with
  // FnInfoOpts::None; OGCG's chain-call option has no remaining guard-sites.

  // FnInfoOpts -- OGCG passes IsInstanceMethod/IsChainCall/IsDelegateCall to

  // Statement attribute handling
  // atomicAttr -- REMOVED. CIR now accepts statement-level [[clang::atomic]]
  // attributes in emitAttributedStmt (matching OGCG control flow), while
  // detailed atomic option semantics remain deferred to dedicated atomic
  // lowering support.
  // hlslControlFlowHintAttr -- REMOVED. CIR now threads HLSL [branch]/[flatten]
  // stmt attributes from emitAttributedStmt to if/switch ops and preserves
  // hlsl.controlflow.hint metadata through CIR->LLVM lowering.

  // Unary operator handling

  // SwitchOp handling
  // foldRangeCase -- REMOVED. No remaining callers.

  // Clang early optimizations or things defered to LLVM lowering.
  // mayHaveIntegerOverflow -- REMOVED. Now implemented: BinOpInfo uses
  // APInt overflow-checked operations (sadd_ov, ssub_ov, etc.) to precisely
  // determine if constant integer operations overflow.
  // shouldReverseUnaryCondOnBoolExpr -- REMOVED. OGCG reverses
  // cond(!x, t, f) -> cond(x, f, t) for efficient codegen. CIR intentionally
  // does not apply this at CodeGen level to preserve diagnostic accuracy;
  // the optimization is deferred to LLVM lowering.

  // RecordType
  static bool astRecordDeclAttr() { return false; }

  // Coroutines
  // emitBodyAndFallthrough -- REMOVED. Feature implemented.
  // coroOutsideFrameMD -- REMOVED. No remaining callers.
  // coroElideSafe -- REMOVED. Feature implemented.
  // coroutineExceptions -- REMOVED. No assert callers. Coroutine EH cleanup
  // (coro.end with isUnwind=true) is still NYI — documented in
  // CIRGenCoroutine.cpp inline comments.

  // Various handling of deferred processing in CIRGenModule.

  // CXXABI

  // Address class
  // addressOffset -- REMOVED. CIR uses GEP-based addressing, not byte offsets.
  // addressPointerAuthInfo -- REMOVED. No remaining callers.

  // Atomic
  static bool atomicInfo() { return false; }
  static bool atomicInfoGetAtomicPointer() { return false; }
  static bool atomicInfoGetAtomicAddress() { return false; }
  // atomicMicrosoftVolatile -- REMOVED. MSVC volatile semantics (/volatile:ms)
  // make volatile reads/writes atomic acquire/release. Windows-only.
  // atomicMisalignedOversizedDiag -- REMOVED. No remaining callers.
  static bool atomicOpenMP() { return false; }
  static bool atomicTargetMetadata() { return false; }

  // Global ctor handling
  // globalCtorLexOrder -- REMOVED. Global constructor ordering is implemented:
  // CIRGenDeclCXX records declaration order via `cir.global_init_order`, and
  // LoweringPrepare stable-sorts default-priority initializers so
  // _GLOBAL__sub_I_ emission matches OGCG lexical/TU declaration order.

  // Global variable properties
  // setGVProperties -- REMOVED. No remaining callers.

  // LowerModule handling
  // targetLoweringInfo -- REMOVED. No remaining callers.

  // Extra checks for lowerGetMethod in ItaniumCXXABI.
  static bool emitVFEInfo() { return false; }
  static bool emitWPDInfo() { return false; }

  // Misc
  static bool aarch64SMEIntrinsics() { return false; }
  static bool aarch64SVEIntrinsics() { return false; }
  // abiArgInfo -- REMOVED. No remaining callers.
  // addAutoInitAnnotation -- REMOVED. CIR defers auto-init annotation
  // metadata (!annotation !"auto-init") until sanitizer support needs it.
  // addHeapAllocSiteMetadata -- REMOVED. CIR has no CallBase-level debug metadata updates.
  // REMOVED: stale bookkeeping hook. emitFinalDestCopy already
  // handles EVK_RValue paths directly and no longer needs a dedicated guard.
  // aggValueSlotDestructedFlag -- REMOVED. withReturnValueSlot directly
  // computes requiresDestruction and routes nontrivial-C-struct destruction
  // through explicit NYI handling without a separate bookkeeping guard.
  static bool aggValueSlotVolatile() { return false; }
  // allocToken -- REMOVED. Alloc-token sanitizer handling is tracked directly
  // by focused NYI comments at affected call-sites; this dedicated marker was
  // redundant bookkeeping.
  // appleArm64CXXABI -- REMOVED. OGCG uses AppleARM64CXXABI which inherits
  // from ARMCXXABI with Apple-specific vtable offset truncation. Apple ARM64
  // is not a supported CIR target (arm64/x86_64 Linux, Android, GPUs only).
  // appleKext -- REMOVED. OGCG handles -fapple-kext by inlining destructors
  // and using AddCXXDtorEntry for global destructor registration. Apple
  // kernel extensions are not relevant for CIR targets.
  // arm64ECDestructorAlias -- REMOVED. tryEmitBaseDestructorAsAlias now
  // mirrors OGCG by disabling alias emission on Windows ARM64EC targets.
  // armComputeVolatileBitfields -- REMOVED. OGCG performs an extra volatile
  // load when ForceAAPCSBitfieldLoad is set for 32-bit ARM (AAPCS) targets.
  // CIR targets arm64 (AArch64/AAPCS64), which does not have this special
  // volatile bitfield behavior.
  // asmLabelAttr -- REMOVED. Now implemented: builtins with __asm__ labels
  // use the mangled name in getBuiltinLibFunction.
  // asmNoMergeAttr -- REMOVED. Now implemented: [[clang::nomerge]] is
  // forwarded to InlineAsmOp and InlineAsmGotoOp via cir.nomerge attribute.
  // asmConvergentAttr -- REMOVED. Now implemented: inline asm is marked
  // convergent in CUDA/OpenCL modes via cir::ConvergentAttr.
  // asmMSReturnRegisterOutputs -- REMOVED. CIR now marks sawAsmBlock for
  // MS asm statements, matching OGCG's conservative implicit-return behavior.
  static bool asmUnwindClobber() { return false; }
  static bool asmVectorType() { return false; }
  static bool assignMemcpyizer() { return false; }
  static bool astVarDeclInterface() { return false; }
  // attributeBuiltin -- REMOVED. CIR now sets the builtin call-site
  // attribute on replaceable global allocation/deallocation function calls.
  static bool builtinCall() { return false; }
  // builtinCallF128 -- REMOVED. f128 builtins are handled through
  // tryEmitFPMathIntrinsic/tryEmitLdexpBuiltin dispatch in CIRGenBuiltin.
  // builtinCheckKind -- REMOVED. No remaining callers.
  // builtinAllowRuntimeCheck -- REMOVED. Implemented in CIRGenBuiltin.cpp.
  // cgFPOptionsRAII -- REMOVED. CIR passes FP options per-operation via
  // CIRGenFunction::getFastMathFlags rather than using builder-level state.
  // Constrained FP behavior is separately guarded by fpConstraints().
  static bool cirgenABIInfo() { return false; }
  // cleanupAfterErrorDiags -- REMOVED. CIR uses MLIR's verifier instead.
  // cleanupAppendInsts -- REMOVED. OGCG appends instructions after cleanup. CIR creates branches directly.
  // cleanupBranchThrough -- REMOVED. OGCG builds a switch-out to route
  // branches through cleanup blocks (branch-through destinations). CIR uses
  // structured control flow via LexicalScope::cleanup() instead, which
  // handles routing branches through cleanup blocks post-facto.
  // cleanupFunctionABICall -- REMOVED. ABI call cleanup deferred to call conv lowering.
  // cleanupValuesToReload -- REMOVED. MLIR SSA handles value liveness; no reload needed after cleanup.
  // cleanupWithPreservedValues -- REMOVED. CIR uses MLIR SSA values which persist across cleanup scopes.
  // cleanupsToDeactivate -- REMOVED. Cleanup deactivation uses CIR scope model.
  static bool constEmitterVectorILE() { return false; }
  // constantFoldNonConstexprIf -- REMOVED. OGCG constant-folds non-constexpr
  // if conditions at codegen time and only emits the live branch. CIR preserves
  // both arms and defers dead code elimination to MLIR/LLVM optimization
  // passes, which produce identical final output. See if-const-fold.cpp test.
  // constantFoldSwitchStatement -- REMOVED. OGCG constant-folds switch
  // conditions at codegen time and only emits the matched case. CIR preserves
  // the full switch structure and defers this to MLIR/LLVM optimization passes,
  // which produce identical final output. See switch-const-fold.cpp test.
  // convergenceTokens -- REMOVED. No remaining callers.
  // constructABIArgDirectExtend -- REMOVED. No remaining callers.
  static bool coverageMapping() { return false; }
  static bool createInvariantGroup() { return false; }
  // OGCG uses llvm::EnableSingleByteCoverage in 15+ locations to emit
  // fine-grained single-byte coverage counters for if/while/do/for/switch.
  // OGCG emits !prof branch weights on switch instructions from C++20
  // [[likely]]/[[unlikely]] attributes on switch cases.
  // OGCG emits !nosanitize, !prof branch weights, and nomerge attrs
  // on -ftrapv overflow check code (trap calls, conditional branches).
  // createProfileWeightsForLoop -- REMOVED. No remaining callers.
  // ctorConstLvalueToRvalueConversion -- REMOVED. Both CIR and OGCG
  // intentionally return null for non-MTE lvalue-to-rvalue conversions in
  // constant expression evaluation of trivial copy/move constructors.
  // ctorMemcpyizer -- REMOVED. No remaining callers.
  static bool cudaSupport() { return false; }
  static bool cudaDeviceCompilation() { return false; }
  // cudaDeviceVarODRUsed -- REMOVED. No remaining callers.
  // OGCG tags device-side surface/texture globals with the nvvm.annotations
  // "surface"/"texture" metadata the NVPTX backend needs to emit .surfref /
  // .texref. CIR has no module-level nvvm metadata yet.
  static bool cudaSurfaceTextureNVVMMetadata() { return false; }
  // checkMacOSXTriple -- REMOVED. macOS-specific handling deferred.
  // dataLayoutTypeAllocSize -- REMOVED. No remaining callers.
  // dataLayoutPtrHandlingBasedOnLangAS -- REMOVED. PointerType data-layout
  // queries now honor CIR pointer address space attributes and per-address-space
  // DLTI pointer entries.
  // devirtualizeDestructor -- REMOVED. OGCG tries to devirtualize destructor
  // calls in delete expressions via getDevirtualizedMethod. CIR preserves
  // virtual destructor calls and defers devirtualization to LLVM, matching
  // the CIR design philosophy of preserving high-level structure.
  // ehCleanupBlock -- REMOVED. CIR handles EH cleanups via per-call-site
  // inline emission in populateEHCatchRegions (CIRGenException.cpp),
  // not via separate ehcleanup blocks in popCleanupBlock.
  // ehCleanupHasPrebranchedFallthrough -- REMOVED. CIR does not use OGCG's
  // NormalCleanupDestSlot state and routes cleanup fallthrough structurally
  // via LexicalScope::cleanup.
  // ehCleanupScope -- REMOVED. CIR handles EH cleanups via per-call-site
  // inline emission. Coroutine-specific EH cleanup (coro.end with
  // isUnwind=true) is still NYI — documented in CIRGenCoroutine.cpp.
  // ehCleanupScopeRequiresEHCleanup -- REMOVED. CIR's per-call-site
  // inline emission model handles RequiresEHCleanup implicitly.
  // ehTerminateScopeSkipsCleanup -- REMOVED. OGCG skips EH cleanup if the
  // innermost EH scope is Terminate, per C++ [except.terminate] which says
  // it's implementation-defined whether cleanups run before std::terminate.
  // CIR uses the conservative behavior of always running cleanups.
  // ehstackBranches -- REMOVED. OGCG tracks hasBranches() across popped
  // scopes in PopCleanupBlocks to decide whether to spill/reload values.
  // CIR's MLIR SSA model handles value liveness without manual spilling.
  // emitBranchThroughCleanup -- REMOVED. No remaining callers.
  // emitCheckedInBoundsGEP -- REMOVED. No remaining callers.
  // emitLValueAlignmentAssumption -- REMOVED. CIR emits alignment assumptions
  // for loaded lvalues in ScalarExprEmitter::emitLoadOfLValue.
  // emitNullabilityCheck -- implemented in CIRGenDecl.cpp for _Nonnull
  // assignment and initialization checks.
  // emitTypeMetadataCodeForVCall -- REMOVED. CIR routes strict-vtable-pointers
  // metadata concerns through createInvariantGroup; type-checked vcall loads
  // remain guarded by shouldEmitVTableTypeCheckedLoad NYI handling.
  // fastMathFlags -- REMOVED. CIR now propagates FMF through all FP ops:
  // complex ops (Group A), FP builtin ops (Group B), X86 reduce builtins
  // (Group C), and LLVMIntrinsicCallOp.

  static bool fpConstraints() { return false; }
  // Debug info - broken down by concern:
  // debugInfoLocalVarDecl -- REMOVED. CIR does not model dedicated local/arg
  // debug declare ops in CIRGen; debug metadata is materialized later during
  // lowering from module-level debug configuration.
  // debugInfoTypeCompletion -- REMOVED. CIR does not complete DIType nodes in
  // CIRGen; debug metadata materialization is deferred to lowering from
  // module-level debug configuration.
  // Category D: Function/call-level debug info.
  // Covers EmitFuncDeclForCallSite (DWARF5 call site info),
  // function declaration DI for external refs, and thunk DI.
  static bool debugInfoCallSiteInfo() { return false; }
  // Category E: Global variable debug info.
  // OGCG calls DI->EmitGlobalVariable() during global emission.
  static bool debugInfoGlobalVar() { return false; }
  // Category F: VLA dimension debug info.
  // OGCG calls EmitAndRegisterVariableArrayDimensions() to register
  // VLA size expressions with DIBuilder for proper array bounds.
  // debugInfoVLADimensions -- REMOVED. No remaining callers.
  // Category G: Scope/cleanup debug locations.
  // OGCG uses ApplyDebugLocation to attribute cleanup/destructor code
  // to the closing brace of a scope.
  // debugInfoScopeCleanup -- REMOVED. No remaining callers.
  // Category H: Coroutine/heap/member debug info (specialized contexts).
  static bool debugInfoSpecialContext() { return false; }
  // Category I: Import declaration debug info.
  // debugInfoImportDecl -- REMOVED. CIR does not model a dedicated
  // import-debug op; direct import handling is accepted in CIRGenModule and
  // debug metadata is materialized later during lowering from module-level
  // debug attributes.
  // handleBuiltinICEArguments -- REMOVED. Target builtin argument processing
  // already uses emitScalarOrConstFoldImmArg() with the ICE bitmap from
  // ASTContext::GetBuiltinType across generic, AArch64, and X86 paths.
  // incrementProfileCounter -- REMOVED. No remaining guarded branches remain;
  // CIR profile/coverage gaps are tracked by dedicated guards such as
  // pgoRegionCounters, pgoUse, pgoInstrumentation, and mcdcCoverageFolding.
  // indirectGotoConstantTarget -- REMOVED. CIR now folds goto *&&label
  // (constant indirect goto targets) into direct cir::GotoOp, matching
  // OGCG's EmitBranchThroughCleanup(getJumpDestForLabel(Target)) path.
  // innermostEHScope -- REMOVED. No remaining callers.
  // insertBuiltinUnpredictable -- REMOVED. CIR now sets a
  // cir.unpredictable discardable attribute on cir.if and cir.switch ops
  // when the condition is wrapped by __builtin_unpredictable.
  // The attribute is propagated through FlattenCFG to cir.brcond and
  // cir.switch.flat. LLVM dialect !unpredictable metadata emission is
  // pending upstream MLIR LLVM::CondBrOp support.
  // instrumentation -- REMOVED. CIR propagates -finstrument-functions and
  // related entry/exit instrumentation attributes from CodeGen options in
  // CIRGenModule::setCIRFunctionAttributesForDefinition().
  // intrinsics -- REMOVED. No remaining callers.
  // isThisCompleteObject -- REMOVED. CIR now implements isThisCompleteObject
  // on CIRGenCXXABI, used in buildThisParam to select full vs. non-virtual
  // alignment for 'this' with virtual bases. Matches OGCG.
  // isTrivialCtorOrDtor -- REMOVED. Misnamed guard: it actually protected
  // UBSan EmitTypeCheck(TCK_ConstructorCall) in emitCXXConstructorCall, not
  // trivial ctor/dtor handling (which is already implemented). Sanitizer
  // parity is tracked directly at affected call-sites.
  // largeBitIntType -- REMOVED. Stale guard-sites in CIRGenTypes/CIRGenDecl/
  // CIRGenExprConstant were dropped; long _BitInt constants and memory-layout
  // adjustments are handled through existing CIR emission plus CIR->LLVM
  // lowering materialization paths.
  // likelihoodDiagOnInfiniteLoop -- REMOVED. Now implemented: CIR emits
  // warn_attribute_has_no_effect_on_infinite_loop when [[likely]]/[[unlikely]]
  // annotates the body of an infinite while loop.
  // OGCG emits vectorize.followup_all chaining, vectorize.enable for explicit
  // vectorization, and strips redundant hints on unroll(full) loops.
  // longDoubleReferenced -- REMOVED. OGCG tracks a per-module boolean for
  // PPC long double ABI metadata emission. CIR does not need this state;
  // PPC double-double format is guarded by ppcDoubleDoubleLongDouble() and
  // target metadata emission by targetCodeGenInfoEmitTargetGlobals().
  static bool lowerAggregateLoadStore() { return false; }
  // lowerModeOptLevel -- REMOVED. Optimization-level-dependent invariant group
  // metadata for loads/stores is implemented in LowerToLLVM.cpp via
  // isLoadOrStoreInvariant() with OptimizationLevel > 0 check.
  // createMemCpy -- REMOVED. No remaining callers.
  static bool makeTripleAlwaysPresent() { return false; }
  static bool memBuiltinSanitizerChecks() { return false; }
  // mergeAllConstants -- REMOVED. No remaining callers.
  static bool memberFuncPtrAuthInfo() { return false; }
  static bool memberFuncPtrCast() { return false; }
  static bool metaDataNode() { return false; }
  // msabi -- REMOVED. Microsoft C++ ABI (MicrosoftCXXABI) is Windows-only.
  // CIR uses ItaniumCXXABI on all supported targets.
  // nullCheckClassCastValue -- REMOVED. CIR now calls
  // shouldNullCheckClassCastValue in all base-to-derived cast paths.
  // objCGC -- REMOVED. No remaining dedicated ObjC GC guard-sites; generic
  // ObjC GC lvalue gaps are tracked by setObjCGCLValueClass and setNonGC
  // guards.
  // objCBlocks -- REMOVED. No remaining callers. Stale assert guards were
  // dropped and block NYI paths use explicit diagnostics.
  // objCLifetime -- REMOVED. Remaining ObjC lifetime gaps are now represented
  // directly in code paths (NYI diagnostics / comments) instead of a global
  // always-false guard.
  // objCTerminate -- REMOVED. terminate handler selection supports
  // objc_terminate when available.
  static bool msvcBuiltins() { return false; }
  // openCL -- REMOVED. Stale assert bookkeeping was dropped at call-sites.
  // Remaining OpenCL gaps are tracked by explicit NYI paths.
  // openCLBuiltinTypes -- REMOVED. OpenCL image/pipe kernel-arg metadata
  // gaps are tracked by explicit NYI diagnostics in CIRGenModule.
  // openCLKernelStubCallingConv -- REMOVED. CIR does not currently materialize
  // OpenCL kernel-stub declarations/calls, so there are no stub call-sites
  // where a special kernel-stub calling-convention rewrite can apply yet.
  // openMP -- REMOVED. Broad assert bookkeeping was dropped at stale generic
  // call-sites; dedicated OpenMP NYI behavior remains tracked in focused
  // call-site checks and diagnostics.
  // optimizeSizeDelete -- REMOVED. The OGCG -Oz optimization that skipped
  // the null check before operator delete was reverted upstream. Both CIR
  // and OGCG now always emit the null check.
  // OGCG tracks CalleeDestructedParamCleanups map for deactivating parameter
  // destroy cleanups when the callee takes ownership. CIR pushes the destroy
  // cleanup but doesn't track the map for deactivation yet.
  // calleeDestructedParamCleanups -- REMOVED. Callee-destructed param ownership deferred.
  // tbaaMayAlias -- REMOVED. CIR's getTypeInfo(), getAccessInfo(), and
  // getAccessTagInfo() handle may_alias types identically to OGCG.
  // tbaaTagForStruct -- REMOVED. CIR's getTypeInfo() returns
  // getValidBaseTypeInfo() for valid base types, matching OGCG.
  // tbaaIncompleteType -- REMOVED. CIR's getAccessInfo() returns
  // TBAAAccessInfo::getIncompleteInfo() for incomplete types, matching OGCG.
  // tbaaNewStructPath -- REMOVED. CIR now uses direct NewStructPathTBAA NYI
  // handling at call-sites in CIRGenTBAA instead of stale guard asserts.
  // tbaaStruct -- REMOVED. CIR now uses direct NYI handling in
  // CIRGenTBAA::getTBAAStructInfo without stale guard asserts.
  // tbaaMergeTBAAInfo -- REMOVED. CIRGenTBAA now implements cast/conditional/
  // memory-transfer merge behavior in parity with OGCG.

  // peepholeProtection -- REMOVED. OGCG inserts dummy bitcasts to protect
  // against trunc(zext) folding during LLVM IR construction. Not needed in
  // CIR — MLIR doesn't do aggressive peephole optimization during codegen.
  // pgoUse -- REMOVED. CIR currently does not thread OGCG's PGO state object
  // through statement/condition emission; remaining profile/coverage work is
  // tracked by dedicated MissingFeatures guards.
  // pointerAuthentication -- REMOVED. Stale assert bookkeeping was dropped;
  // remaining pointer-auth gaps are tracked by explicit per-path comments and
  // NYI handling where needed.
  // preservedAccessIndexRegion -- REMOVED. Direct-CIR now lowers
  // __builtin_preserve_access_index() regions to llvm.preserve.*.access.index
  // intrinsics; remaining metadata-shaping gaps are tracked at the specific
  // call-sites and tests that still diverge from OGCG.
  // runCleanupsScope -- REMOVED. CIR scopes/cleanup emission already model the
  // needed statement and temporary cleanup lifetimes directly at call sites.
  // sanitizers -- REMOVED. Sanitizer-related parity work is now tracked at
  // explicit CIRGen/Lowering call-sites instead of via a single broad guard.
  // setDLLStorageClass -- REMOVED. OGCG calls setDLLStorageClass on LLVM
  // GlobalValues for __declspec(dllimport/dllexport). DLL storage class is a
  // Windows PE/COFF concept. Windows is not a supported CIR target.

  // setDivFPAccuracy -- REMOVED. OGCG no longer has a setDivFPAccuracy
  // function. FP division accuracy (!fpmath metadata) is handled through
  // FastMathFlags and FPOptions at the operation level.
  // setNonGC -- REMOVED. ObjC GC write barrier suppression; ObjC GC was
  // deprecated in 2012 and is not supported by CIR.
  // setObjCGCLValueClass -- REMOVED. ObjC GC LValue classification for
  // write barrier generation; ObjC GC was deprecated in 2012 and is not
  // supported by CIR.
  // setTargetAttributes -- REMOVED. CIR now applies
  // TargetCIRGenInfo::setTargetAttributes in both global and static-local
  // declaration emission paths.
  // shouldCreateMemCpyFromGlobal -- REMOVED. CIR now matches OGCG's
  // large trivially-copyable array-init optimization in AggExprEmitter:
  // emit a private constant global and initialize with copy-from-global.
  // shouldSplitConstantStore -- REMOVED. CIR now splits small constant
  // arrays (<=64B at -O1+) into individual stores.
  // shouldUseBZeroPlusStoresToInitialize -- REMOVED. CIR now uses bzero +
  // patch stores for mostly-zero constants >32 bytes.
  // shouldUseMemSetToInitialize -- REMOVED. CIR now uses memset for
  // uniform-byte constant arrays via getUniformByteValue.
  // simplifyCleanupEntry -- REMOVED. OGCG's SimplifyCleanupEntry merges
  // LLVM basic blocks with single predecessors during cleanup processing.
  // CIR uses structured control flow (MLIR regions), so this LLVM IR-specific
  // block merging optimization is not applicable.
  // sourceLanguageCases -- REMOVED. getCIRSourceLanguage now emits an
  // explicit NYI diagnostic for unsupported source languages directly at the
  // call-site instead of routing through stale MissingFeatures assert gates.
  // stackBase -- REMOVED. OGCG tracks StackBase for MSVC inalloca argument
  // memory allocation (allocateArgumentMemory / freeArgumentMemory) using
  // stack save/restore. MSVC/Windows is not a supported CIR target.
  // stackSaveOp -- REMOVED. FlattenCFG no longer routes through a stale
  // assert gate; stack save/restore is emitted explicitly where needed in
  // CIRGen cleanup paths for dynamic allocas.
  // strictReturnVarDeclExternC -- REMOVED. CIR now propagates
  // cir.callee_extern_c via extra_attrs on indirect CallOps and checks it
  // in LowerFunction.cpp's indirect call return-noundef path.
  // strictReturnCodeGenOpt -- REMOVED. CIR now checks
  // CodeGenOpts.StrictReturn and MayDropFunctionReturn in LowerCall.cpp.
  // stackProtector -- REMOVED. CIR now sets ssp/sspstrong/sspreq in
  // extra func attributes based on LangOpts.getStackProtector().
  // stackClashProtection -- REMOVED. CIR now sets probe-stack and
  // stack-probe-size in extra func attributes.
  // targetCIRGenInfoArch -- REMOVED. CIR no longer gates unsupported-target
  // fallback in getTargetCIRGenInfo() behind a stale MissingFeatures assert.
  // targetCIRGenInfoOS -- REMOVED. getTargetCIRGenInfo now directly handles
  // unsupported OS triples via explicit Linux fallback comments.
  // targetCodeGenInfoGetNullPointer -- REMOVED. CIR now models target-specific
  // null pointer semantics for constant pointer lowering in DirectToLLVM.
  // thunks -- REMOVED. CIR now supports delegated forwarding of
  // callee-destructed parameters (e.g. trivial_abi record args) in both
  // constructor delegation and virtual thunk emission by tracking and
  // deactivating callee-destroy cleanup scopes before the forwarded call.
  // tryEmitAsConstant -- REMOVED. No remaining callers. The CIRGenFunction
  // method tryEmitAsConstant() is fully implemented.
  // typeChecks -- REMOVED. CIR now tracks sanitizer-related type-check parity
  // directly at call-sites (e.g. member access and checked lvalue paths)
  // instead of gating codegen through a global MissingFeatures assert.
  // vectorConstants -- REMOVED. CIR auto-var initialization now handles
  // vector typed constants directly via typed attributes and explicit
  // store/memset/memcpy lowering paths without a global MissingFeatures gate.
  // verboseTrapDebugMessage -- REMOVED. OGCG attaches debug info messages
  // from __builtin_verbose_trap string arguments via CreateTrapFailureMessageFor.
  // CIR intentionally omits this — the trap intrinsic is emitted correctly and
  // debug message attachment is a non-functional enhancement.
  // vlas -- REMOVED. Remaining VLA handling is either implemented directly
  // (e.g. emitArrayLength/getVLASize paths) or diagnosed explicitly at the
  // exact unsupported call-site (variably modified types in varargs scalar
  // emission). No live code paths should be gated through a global
  // MissingFeatures::vlas assert.
  // vtableEmitMetadata -- REMOVED. Vtable metadata emission is already handled
  // directly in CIR vtable emission paths (for example cir.vtable.type_metadata
  // attachment in createVTableInitializer), so no global MissingFeatures guard
  // is needed at call-sites.
  // weakRefReference -- REMOVED. CIR resolves weak references directly via
  // CIRGenModule::getWeakRefReference and uses those symbols in both function
  // and variable decl-ref emission paths.
  // msGuidDecl -- REMOVED. MSGuidDecl (__declspec(uuid(...))) is an MSVC
  // extension for COM GUIDs. Windows/MSVC is not a supported CIR target.
  static bool blockByrefVariable() { return false; }
  // writebacks -- REMOVED. ObjC ARC writebacks (ObjCIndirectCopyRestoreExpr)
  // and HLSL out/inout parameter writebacks (HLSLOutArgExpr) are the only
  // consumers. Neither ObjC nor HLSL is a supported CIR source language
  // (CIR targets C/C++/CUDA on Linux, Android, and GPUs).
  // msvcCXXPersonality -- REMOVED. OGCG uses MSVC personality functions
  // (e.g. __CxxFrameHandler3) for Windows exception handling. Windows/MSVC
  // is not a supported CIR target.
  // OGCG pushes DestroyUnpassedArg cleanup with deactivation marker for
  // callee-destroyed aggregate parameters (trivial_abi, MSABI). This
  // provides exception safety if an exception is thrown between creating
  // the temporary and making the call.
  // isOutlinedSEHHelper -- REMOVED. Windows Structured Exception Handling
  // (SEH) outlined helpers. Windows is not a supported CIR target.
  // readArrayCookieImpl -- REMOVED. CIR delete[] emission inlines array-cookie
  // pointer adjustment and count loading in CIRGenExprCXX instead of using
  // ABI virtual ReadArrayCookie/readArrayCookieImpl dispatch.
  static bool memberPointerNullCheck() { return false; }
  static bool memberPointerConvertibility() { return false; }
  // REMOVED: bitIntConstForMemory() — now implemented via emitBitIntForMemory()
  // in CIRGenExprConstant.cpp, which handles byte-array representation for
  // large _BitInt types in struct field layout.
  // REMOVED: boolZeroExtensionForMemory() — CIR uses cir::BoolType abstractly.
  // Bool-to-i8 widening for memory is handled in LowerToLLVM.cpp's
  // emitToMemory/emitFromMemory (zext i1->i8 on store, trunc i8->i1 on load)
  // and convertTypeForMemory (BoolType -> IntegerType with data layout size).
  // bpfPreserveStaticOffset -- REMOVED. OGCG wraps struct/union field
  // addresses with llvm.preserve.static.offset for BPF targets. BPF is not
  // a supported CIR target (arm64/x86_64 Linux, Android, GPUs only).
  // REMOVED: pointerOverflowDefined() — Implemented via module-level
  // cir.pointer_overflow_defined attribute. GetMemberOp lowering
  // checks the attribute and suppresses inbounds/nuw GEP flags. Tested in
  // wrapv-pointer-struct-gep.c.

  // Missing types
  static bool dataMemberType() { return false; }
  static bool matrixType() { return false; }
  // methodType -- REMOVED. No remaining callers.
  // unsizedTypes -- REMOVED. cir::isSized now directly reports false for
  // non-SizedTypeInterface types; no remaining callers gate this behind
  // MissingFeatures bookkeeping.
  // stringTypeWithDifferentArraySize -- REMOVED. ConstArrayAttr string
  // lowering no longer tracks this via a global MissingFeatures gate.

  // Future CIR operations
  // awaitOp -- REMOVED. No remaining callers.
  static bool callOp() { return false; }
  // callOpNoReturnAttr -- REMOVED. NoReturn on call ops handled differently in CIR.
  // ifOp -- REMOVED. IfOp is fully handled by FlattenCFG and CodeGen.
  static bool llvmLoweringPtrDiffConsidersPointee() { return false; }
  // ptrStrideOp -- REMOVED. No remaining callers.
  // switchCaseLikelihood -- REMOVED. Now fully implemented: CIR collects
  // [[likely]]/[[unlikely]] attributes from switch cases and computes
  // cir.branch_weights metadata matching OGCG's getLikelihoodWeights().
  // switchOp -- REMOVED. SwitchOp is fully implemented: CIRGen emits
  // SwitchOp, lowering converts to LLVM::SwitchOp, and BreakOp::verify
  // correctly checks for both LoopOpInterface and SwitchOp parents.
  // throwOp -- REMOVED. No remaining callers.
  // tryOp -- REMOVED. TryOp is already emitted by CIRGen and consumed by EH
  // pipeline passes (including FlattenCFG and lowering) without a dedicated
  // MissingFeatures gate.
  // zextOp -- REMOVED. CIR has no standalone ZExtOp; zero/sign extension is
  // modeled via cir.cast and lowered directly. BrCond lowering receives an i1
  // condition from type conversion and does not need a dedicated zext cleanup
  // gate.

  // Future CIR attributes
  // optInfoAttr -- REMOVED. Optimization metadata is modeled by
  // module-level `cir.opt_info` and already consumed by HoistAllocas,
  // TargetLowering::LowerModule, and CIR->LLVM lowering.

  // currentFuncletPad -- REMOVED. Funclet pads (catchpad/cleanuppad) are
  // part of LLVM's Windows EH model (WinEH). CIR uses DWARF/Itanium EH
  // on its supported targets (Linux, Android, GPUs). Windows is not a
  // supported CIR target.

  // Target lowering / CallConvLowering related
  // ABIClangTypeKind -- REMOVED. No remaining callers.
  // ABIFuncPtr -- REMOVED. No remaining callers.
  // ABIInRegAttribute -- REMOVED. No remaining callers.
  // ABINoAliasAttribute -- REMOVED. No remaining callers.
  // ABINoProtoFunctions -- REMOVED. No remaining callers.
  // ABIPotentialArgAccess -- REMOVED. No remaining callers.
  // ABITransparentUnionHandling -- REMOVED. No remaining callers.
  // ABIArgExpand -- REMOVED. No remaining callers.
  // ABIArgInfo::Expand — struct expanded into multiple scalar args (ARM EABI).
  // argumentPadding -- REMOVED. CIR ABI lowering currently does not inject
  // explicit ABI padding pseudo-arguments in the CIR function signature; the
  // associated guard-sites in argument mapping/type lowering were stale.
  // astContextGetExternalSource -- REMOVED. OGCG queries external AST sources
  // (DWARF/XCOFF) for layout overrides. CIR doesn't support external layout.
  // bitFieldPaddingDiagnostics -- REMOVED. OGCG emits -Wpadded diagnostics
  // for struct padding in checkFieldPadding. CIR's target lowering doesn't
  // emit clang diagnostics.
  // cacheRecordLayouts -- REMOVED. No remaining callers.
  // chainCall -- REMOVED. No remaining callers.
  // codeGenOpts -- REMOVED. CodeGenOptions are now propagated from CIR
  // module attributes (OptimizationLevel, OptimizeSize, RelaxedAliasing,
  // PassByValueIsNoAlias, StrictReturn).
  // cmseCall -- REMOVED. CMSE (Cortex-M Security Extensions) non-secure
  // calls are for ARM 32-bit embedded, not AArch64. Not a CIR target.
  // CUDA -- REMOVED. No remaining callers.
  // CXXRecordDeclIsEmptyCXX11 -- REMOVED. CIR's RecordType handles
  // [[no_unique_address]] and empty class placement in its flattened member
  // list during CIRGen.
  // CXXRecordDeclIsPOD -- REMOVED. RecordLayoutBuilder queries isPOD()
  // from CXXRecordDecl via AST for tail padding decisions and non-POD field
  // alignment in packed structs.
  // CXXRecordIsDynamicClass -- REMOVED. RecordLayoutBuilder now queries
  // isDynamicClass() from CXXRecordDecl via AST and passes the correct
  // hasExtendableVFPtr value to CIRRecordLayout.
  // declHasAlignMac68kAttr -- REMOVED. #pragma options align=mac68k is an
  // old Apple-specific alignment mode for 68k compatibility. Not relevant
  // for CIR targets (arm64/x86_64 Linux, Android, GPUs).
  // declHasAlignNaturalAttr -- REMOVED. RecordLayoutBuilder now mirrors OGCG
  // by setting IsNaturalAlign when AlignNaturalAttr is present on the record.
  static bool declHasMaxFieldAlignmentAttr() { return false; }
  // noEscapeAttr -- REMOVED. No remaining callers. noescape parameter
  // attribute -> LLVM nocapture is tracked at a higher level.
  // Swift error result prolog/call-site handling: copy swifterror into a
  // temporary in function prolog, and create swifterror alloca + copy-in/out
  // at call sites.
  static bool swiftErrorResult() { return false; }
  // fieldDeclAbstraction -- REMOVED. Field declaration abstraction is now
  // handled directly in CIR record layout without a dedicated guard.
  // fieldDeclisUnnamedBitField -- REMOVED. CIR queries
  // clang::FieldDecl::isUnnamedBitField() directly at call sites.
  // fieldDeclGetMaxFieldAlignment -- REMOVED. RecordLayoutBuilder applies
  // per-field max-alignment attributes via AST field scanning in
  // initializeLayout; no dedicated guard remains.
  // fieldDeclIsBitfield -- REMOVED. Record-layout bitfield handling is tracked
  // directly in RecordLayoutBuilder state and layout logic.
  // fieldDeclIsPotentiallyOverlapping -- REMOVED. Potentially-overlapping
  // field handling remains an explicit NYI branch in RecordLayoutBuilder.
  // fixedWidthIntegers -- REMOVED. ABI integer promotion checks now rely
  // directly on CIRLowerContext::isPromotableIntegerType().
  // fixedSizeIntType -- REMOVED. AArch64 ABI classification now handles scalar
  // integer-like CIR types directly without an inventory guard.
  // funcDeclIsCXXConstructorDecl -- REMOVED. Constructor declarations and
  // definitions are lowered through arrangeGlobalDeclaration/
  // setFunctionAttributes without a dedicated inventory gate.
  // funcDeclIsCXXDestructorDecl -- REMOVED. Destructor declarations and
  // definitions are lowered through arrangeGlobalDeclaration/
  // setFunctionAttributes without a dedicated inventory gate.
  // funcDeclIsCXXMethodDecl -- REMOVED. C++ method declarations/definitions
  // are lowered through arrangeFunctionDeclaration/setFunctionAttributes and
  // this-parameter attrs are propagated from CIRGen without an inventory gate.
  // funcDeclIsInlineBuiltinDeclaration -- REMOVED. Inline builtin declarations
  // are handled in CIRGenModule::setFunctionAttributes and CIRGenFunction::
  // generateCode (including nobuiltin + .inline body emission) without a
  // dedicated inventory gate.
  // funcDeclIsReplaceableGlobalAllocationFunction -- REMOVED. Replaceable
  // global allocation/deallocation function handling is implemented in
  // CIRGenModule::setFunctionAttributes (declaration-level nobuiltin) and
  // CIRGenCall::constructAttributeList (call-site builtin/noalias_return
  // parity for new/delete expressions), matching OGCG behavior.
  // functionPointerAddressSpace -- REMOVED. Pointer/reference-to-function
  // lowering routes through CIRGenTypes::convertType and CIRBaseBuilder
  // pointer construction using the pointee QualType address space, matching
  // OGCG behavior for function pointer address-space selection.
  // getCXXRecordBases -- REMOVED. Target lowering now handles non-virtual
  // C++ base subobjects directly:
  // - EmptySubobjectMap includes non-virtual bases when computing largest
  //   empty subobject size.
  // - ItaniumRecordLayoutBuilder no longer carries a stale global guard for
  //   base layout bookkeeping.
  // - x86_64 BitsContainNoUserData classifies base offsets via AST layout.
  // inallocaArgs -- REMOVED. CIR still rejects ABIArgInfo::InAlloca paths as
  // NYI at the specific lowering switch cases; no dedicated MissingFeatures
  // guard is needed for those unreachable branches.
  // isCXXRecordDecl -- REMOVED. Target lowering now queries CXXRecordDecl
  // properties from AST via RecordType::getAst(). classifyReturnType checks
  // canPassInRegisters() for C records, and RecordLayoutBuilder populates
  // HasOwnVFPtr / NonVirtualSize / isDynamicClass from ASTRecordLayout.
  // langOpts -- REMOVED. LangOptions are now initialized in
  // createLowerModule() from cir.lang and module attributes. Call sites use
  // getLangOpts() directly (isPromotableIntegerType checks HLSL,
  // getCXXABIKind checks CXXABI). Fields not yet propagated (e.g. Sanitize)
  // retain their defaults which is correct for the common case.
  // noReturn -- REMOVED. noreturn is handled ABI-agnostically in CIRGen
  // (function attribute on FuncOp) and propagated to LLVM IR during lowering
  // (LowerToLLVM.cpp). OGCG sets noreturn in calling-convention lowering,
  // but CIR correctly defers this to the ABI-agnostic layer.
  // qualifiedTypes -- REMOVED. No remaining callers.
  // recordDeclHasFlexibleArrayMember -- REMOVED. Flexible-array-member checks
  // now use AST RecordDecl::hasFlexibleArrayMember() directly in target
  // lowering paths.
  // recordDeclIsCXXDecl -- REMOVED. CIR RecordType always carries the needed
  // AST record declaration kind, and tail-padding decisions query ABI policy
  // directly without an extra MissingFeatures gate.
  // recordDeclIsMSStruct -- REMOVED. MS struct layout is an MSVC-specific
  // extension tied to Windows/MS ABI behavior, which is outside CIR's
  // supported target set.
  // recordDeclIsPacked -- REMOVED. CIR record layout reads packedness from
  // concrete RecordType state directly; non-record paths are handled as
  // explicit unsupported cases instead of a global MissingFeatures gate.
  // recordDeclMayInsertExtraPadding -- REMOVED. Extra record-field padding
  // insertion is a dedicated NYI path in record layout lowering and no longer
  // tracked via a global MissingFeatures gate.
  // recordLayoutIsEmptyRecordForLayout -- REMOVED. Record layout directly
  // queries isEmptyRecordForLayout without a MissingFeatures gate.
  // recordLayoutVerification -- REMOVED. CIR intentionally skips OGCG's
  // post-lowering record-layout cross-check assertions.
  // setCallingConv -- REMOVED. No remaining callers.
  // SPIRVABI -- REMOVED. No remaining callers.
  // sretArgs -- REMOVED. CIR defers ABI sret argument materialization to
  // call-conv lowering; CIRGen and CIR-to-CIR lowering now track this at
  // explicit call-sites without a global MissingFeatures gate.
  static bool swift() { return false; }
  // tagTypeClassAbstraction -- REMOVED. CIRLowerContext handles record type
  // abstraction directly from concrete cir::RecordType state.
  // typeGetAsEnumType -- REMOVED. CIR type lowering no longer uses
  // Type::getAsEnumType()-style dispatch; enum-origin information is not
  // represented in CIR type classes and promotability is handled through
  // explicit ABI-lowering attributes.
  // X86TypeClassification -- REMOVED. X86_64ABIInfo::classify now reports
  // unsupported type classification paths through local target-lowering NYI
  // diagnostics instead of routing through a global MissingFeatures gate.
  // x86AvxRoundingMode -- REMOVED. AVX-512 sqrt builtins now match OGCG:
  // non-default rounding mode immediates are preserved as target intrinsics
  // (x86.avx512*.sqrt.*.512), while CUR_DIRECTION lowers to generic cir.sqrt.
  // x86MaskedCompareSignedness -- REMOVED. emitX86MaskedCompare now handles
  // unsigned comparisons by casting operands to unsigned vector element types.
  static bool maxVectorWidth() { return false; }

  // Additional CallConvLowering features from LowerFunction.cpp
  // cmseNonSecureCallAttr -- REMOVED. CMSE (Cortex-M Security Extensions)
  // non-secure call attribute. ARM 32-bit embedded only, not AArch64.
  // emitEmptyRecordCheck -- REMOVED. No remaining callers.
  // evaluationKind -- REMOVED. No remaining callers.
  // returnValueDominatingStoreOptmiization -- REMOVED. CIR keeps the
  // non-optimized epilog path (load from ReturnValue alloca) and no longer
  // tracks OGCG's dominating-store micro-optimization behind a global
  // MissingFeatures gate.
  // supportisHomogeneousAggregateQueryForAArch64 -- REMOVED. AArch64 va_arg
  // lowering currently models record arguments as non-HFA and handles
  // unsupported HFA-specific behavior with explicit local comments/assertions.
  // varDeclIsKNRPromoted -- REMOVED. CIR doesn't support K&R declarations.
  // Dead code path removed from LowerFunction.cpp.

  // CIRGenClass.cpp missing features
  // ctorMemcpyRecordTypes -- REMOVED. No remaining callers. OGCG's
  // ConstructorMemcpyizer allows memcpy for trivially-copyable record types.
  // OGCG pushes EH destroy cleanups after memcpy'd array field copies
  // so partially-constructed objects are destroyed on exception. CIR skips
  // this because it restricts memcpy to non-record non-dtor types.
  static bool ctorMemcpyEHCleanup() { return false; }
  // CIRGenDecl.cpp missing features
  // autoVarInitContainsLabel -- REMOVED. CIR scopes always provide insert point.
  // autoVarInitIsAccessedBy -- REMOVED. CIR conservatively always initializes.
  // haveInsertPointCheck -- REMOVED. CIR statement/cleanup emission keeps a
  // valid insertion block through scope/label construction, and [[assume]]
  // now directly asserts insertion-block availability at emission.
  // staticVarSectionAttr -- REMOVED. Now implemented: SectionAttr on static
  // local variables is forwarded via GlobalOp::setSectionAttr.
  // stopAutoInit -- REMOVED. Now implemented: CIRGenModule::stopAutoInit()
  // checks -ftrivial-auto-var-init-stop-after=N counter.
  // trivialAutoVarInitMaxSize -- REMOVED. Now implemented: large variables
  // are skipped when -ftrivial-auto-var-init-max-size=N is set.
  // OGCG's constWithPadding() fills struct padding bytes with the pattern
  // value. CIR record types already include padding members in layout,
  // so recursive getPatternInitAttr covers padding slots. This guard
  // tracks the gap in case CIR's padding representation changes.
  // autoVarInitConstWithPadding -- REMOVED. CIR record types include padding
  // as explicit array-of-i8 members, so getPatternInitAttr covers them.
  // autoVarInitReplaceUndef -- REMOVED. CIR constants are fully defined
  // (no undef), so replaceUndef is not needed.
  // isRedundantBeforeReturn -- REMOVED. CIR's EHScopeStack::Cleanup base
  // class now has the virtual method, and CallLifetimeEnd/CallStackRestore
  // override it to return true, matching OGCG.

  // CIRGenDeclCXX.cpp missing features
  // forbidGuardVariables -- REMOVED. CIR now checks
  // CodeGenOpts.ForbidGuardVariables and emits an error diagnostic before
  // calling emitGuardedInit, matching OGCG.
  // OGCG checks hasWeakLinkage/hasLinkOnceLinkage and dynamic TLS with
  // template instantiation, routing to EmitCXXGuardedInit instead of
  // EmitCXXGlobalVarDeclInit for variables that need guarded initialization.
  // initSegAttr -- REMOVED. CIR now maps InitSegAttr .CRT$XCC/.CRT$XCL to
  // init priorities (200/400), matching OGCG's AddGlobalCtor contract.

  // CIRGenCall.cpp missing features
  static bool callArgCleanups() { return false; }
  static bool checkFunctionCallABI() { return false; }
  static bool checkTargetFeatures() { return false; }
  // effectiveCallingConvention -- REMOVED. CIR models effective calling
  // convention during CIR-to-LLVM ABI lowering.
  static bool largestVectorWidth() { return false; }
  // mustTailCall -- REMOVED. CIR thunk emission now builds musttail calls
  // directly in emitMustTailThunk() for eligible return-adjusting thunks,
  // so no MissingFeatures bookkeeping guard is needed.
  // CIR uses a callee-centric attribute model where ABI attributes live on
  // FuncOp and are propagated to call sites during DirectToLLVM. This works
  // for direct calls. Indirect calls use cir.call_arg_attrs.
  // OGCG's ConstructAttributeList handles many target/platform-specific
  // function attributes that CIR omits:
  // - CmseNSCall, OMP assumptions, AnyX86NoCallerSavedRegisters,
  //   AnyX86NoCfCheck, Leaf, BPFFastCall, ArmLocallyStreaming,
  //   NoSpeculativeLoadHardening, SpeculativeLoadHardening, NoSplitStack,
  //   NonLazyBind/-fno-plt,
  //   NoConvergent, sample-profile-suffix-elision-policy,
  //   loader-replaceable, ModularFormat
  static bool callAttrTargetSpecific() { return false; }
  // cmseNSEntryAttr -- REMOVED. OGCG adds "cmse_nonsecure_entry" for
  // CmseNSEntryAttr on function definitions (CGCall.cpp:2636-2637). CMSE
  // (Cortex-M Security Extensions) is for ARM 32-bit embedded processors,
  // not AArch64. CIR targets arm64 (AArch64), not Cortex-M.
  // windowsHotPatching -- REMOVED. OGCG adds "marked_for_windows_hot_patching"
  // for functions in MSHotPatchFunctions list (CGCall.cpp:2677-2681). Windows
  // is not a supported CIR target.
  // OGCG adds "loader-replaceable" for functions matching
  // CodeGenOpts.isLoaderReplaceableFunctionName (CGCall.cpp:2685-2686).
  // OGCG calls addNoBuiltinAttributes to process NoBuiltinAttr and
  // OGCG processes ABIArgInfo for all parameters and return value,
  // adding sret, byval, inreg, sext/zext, noundef, nofpclass, etc.
  static bool callAttrABIArgLowering() { return false; }
  // OGCG emits AllocAlign assumptions on call return values (alignment from
  // a call argument). Constant AllocAlign is implemented; dynamic (runtime)
  // alignment values require AssumeAlignedOp to support SSA alignment operands.

  // CIRGenStmt.cpp missing features
  // Forward gotos still use a source-range heuristic for cleanup scope
  // determination. CIR now has structural label tracking via labelMap and
  // scope depth adjustment via rescopeLabels, but cannot fully defer cleanup
  // emission because CIR emits cleanups inline and uses name-based GotoOp.
  static bool forwardGotoDeferredCleanup() { return false; }
  // namedBreakContinue -- REMOVED. CIR now emits explicit NYI diagnostics for
  // named break/continue statements instead of relying on a stale assert guard.
  // returnValueValidCheck -- REMOVED. CIR uses AST-level return-type checks in
  // CIRGenStmt for void-vs-value returns, and does not currently model OGCG's
  // sanitizer-driven EmitReturnValueCheck hook in ABI lowering.
  // emitStopPointInEmitStmt -- REMOVED. OGCG calls EmitStopPoint(S) at the
  // top of EmitStmt to set debug stop points for every statement. CIR handles
  // this differently: every CIR op carries its own mlir::Location, so
  // explicit stop-points are unnecessary. DIScopeForLLVMFuncOpPass converts
  // these locations to DILocations during lowering.
  // ifStmtRunCleanupsScope -- REMOVED. CIR's emitIfOnBoolExpr wraps both
  // then/else bodies in LexicalScope (inherits RunCleanupsScope), providing
  // equivalent cleanup scoping to OGCG's explicit RunCleanupsScope.
  // REMOVED: prettyStackTraceLoc() — Implemented in emitCompoundStmt.
  // CIR creates PrettyStackTraceLoc for crash diagnostics, matching OGCG.

  // CIRGenCleanup.cpp missing features
  // activateCleanupBlock -- REMOVED. OGCG defines ActivateCleanupBlock but
  // it has no callers in the codebase (dead code). CIR implements
  // deactivateCleanupBlock which is the actively used counterpart.
  // cleanupBlockActivationFlag -- REMOVED. CIR marks scopes inactive directly without activation flags.
  // cleanupFallthroughIsBranchThrough -- REMOVED. CIR popCleanupBlock uses LexicalScope, not dest-slot routing.

  // CIRGenExprAggregate.cpp missing features
  // aggArrayInitLoopCleanupScope -- REMOVED. CIR's LexicalScope handles per-iteration cleanup.
  // aggArrayInitLoopPartialCleanup -- REMOVED. CIR handles via per-call-site inline EH emission.
  // aggResultUnused -- REMOVED. CIR ReturnValueSlot does not carry an
  // IsResultUnused flag yet, so this assert was stale bookkeeping only.
  // checkAggExprForMemSetUse -- REMOVED. Deferred optimization; declaration-
  // level zero init is handled by shouldUseBZeroPlusStoresToInitialize.
  // isSimpleZeroInit -- REMOVED. CIR now skips obvious stores of zero-valued
  // scalar expressions into already-zeroed destinations in AggExprEmitter.
  // deferredDeactivationCleanup -- REMOVED. CIR fully implements
  // pushDestroyAndDeferDeactivation and CleanupDeactivationScope for
  // partial construction cleanup during exceptions.
  // cleanupDeactivationScope -- REMOVED. CIR implements
  // CleanupDeactivationScope in CIRGenFunction.h matching OGCG.
  // allocaTrackerRAII -- REMOVED. Not needed in CIR; MLIR's SSA model
  // handles value liveness differently.
  // lambdaCaptureNormalCleanup -- REMOVED. OGCG's VisitLambdaExpr pushes
  // NormalAndEHCleanup via pushDestroyAndDeferDeactivation. CIR intentionally
  // only pushes EHCleanup because normal-path destruction is handled by the
  // lambda closure's own destructor.
  // embedExprEmission -- REMOVED. CIR emits EmbedExpr in scalar and aggregate
  // initialization paths; constant aggregates account for multi-element embed
  // payload expansion in CIRGenExprConstant.
  // nonTrivialCStructDestroy -- REMOVED. CIR aggregate conditional emission
  // now mirrors OGCG by scheduling DK_nontrivial_c_struct destruction for
  // conditional operator results when destination lifetime is not externally
  // managed.
  // conditionalCleanup -- REMOVED. No remaining callers.

  // CIRGenModule.cpp missing features
  // addEmittedDeferredDecl -- REMOVED. Incremental compilation tracking not needed.
  // checkAliases -- REMOVED. Alias verification deferred to LLVM backend.
  // codeSegAttr -- REMOVED. __declspec(code_seg("segname")) is a Windows
  // PE/COFF section control attribute. Windows is not a supported CIR target.
  // emitModuleLinkOptions -- REMOVED. CIR emits cir.linker_options from
  // CodeGenOptions::LinkerOptions in CIRGenerator::Initialize and lowers it to
  // llvm.linker.options during CIR-to-LLVM.
  static bool cxx20ModuleBoundaryCheck() { return false; }
  // cxx20NamedModuleVTableLinkage -- REMOVED. No remaining callers.
  // duplicateMangledNameDiag -- REMOVED. OGCG reports
  // err_duplicate_mangled_name when two different definitions produce the
  // same mangled name. CIR's CIRGen doesn't emit clang diagnostics.
  // emitCtorDtorLists -- REMOVED. CIR models constructor/destructor lists via
  // GlobalOp ctor/dtor regions and lowers to llvm.global_ctors/global_dtors
  // during CIR-to-LLVM lowering.
  static bool moduleFlags() { return false; }
  // multiversionResolver -- REMOVED. Remaining CIR multi-version control flow
  // no longer depends on this bookkeeping flag:
  // - cpu_specific / target_clones paths emit per-version bodies directly;
  // - resolver creation has an explicit non-ifunc fallback that returns a
  //   resolver declaration;
  // - target multiversion finalization already gates unsupported targets with
  //   explicit early-continue checks.
  // pragmaClangSectionAttr -- REMOVED. CIR now mirrors OGCG for
  // #pragma clang section:
  // - globals and static locals forward bss/data/rodata/relro section names as
  //   discardable attrs to LLVM lowering (materialized as GlobalVariable
  //   attributes bss-section/data-section/rodata-section/relro-section);
  // - functions honor PragmaClangTextSectionAttr when SectionAttr is absent.
  // registerGlobalDtorsWithAtExit -- REMOVED. CIR now mirrors OGCG for
  // -fregister-global-dtors-with-atexit:
  // - addGlobalDtor tags eligible global dtors (all non-AIX dtors, and
  //   __attribute__((destructor)) dtors on AIX) with
  //   "cir.register_global_dtor_with_atexit";
  // - LoweringPrepare synthesizes __GLOBAL_init_<priority> helpers that call
  //   __cxa_atexit (or atexit with -fno-use-cxa-atexit) and rewrites those
  //   dtors into cir.global_ctors entries.
  // OGCG emits EmitCXXGlobalInitFunc / EmitCXXGlobalCleanUpFunc in Release
  // to create the global C++ initialization and cleanup functions.
  // emitCXXGlobalInitFunc -- REMOVED. CIR handles C++ global
  // init/cleanup through GlobalOp ctor/dtor attributes and LoweringPrepare.
  // simplifyPersonality -- REMOVED. OGCG calls SimplifyPersonality in Release
  // to replace ObjC++ personality with C++ personality when all landing pads
  // only use C++ catch types. CIR's per-function personality assignment model
  // avoids this: personality is set based on actual catch types used, not the
  // compilation mode.
  // OGCG calls CUDA/HIP runtime finalizeModule in Release.
  static bool cudaFinalizeModule() { return false; }
  // OGCG emits PGO profile summary, value profiling flag, and profile
  // version as module metadata in Release.
  // pgoInstrumentation -- REMOVED. No remaining guard-sites in release flow;
  // PGO-related support gaps are tracked by dedicated guards (for example,
  // pgoRegionCounters/profileInstrBlocking) and existing profile attrs.
  // targetCodeGenInfoEmitTargetGlobals -- REMOVED. CIR release finalization
  // no longer tracks target-specific module-global metadata emission behind
  // a global MissingFeatures gate.
  // openCLKernelStubDeferral -- REMOVED. CIR does not currently materialize an
  // OpenCL kernel-stub emission pipeline in EmitGlobal, so there is no
  // deferred stub declaration path to track.
  // OGCG checks FD->isMultiVersion() on forward declarations in EmitGlobal
  // to eagerly create multi-version resolvers for AArch64.
  static bool multiVersionForwardDeclCheck() { return false; }
  // OGCG calls AddDeferredUnusedCoverageMapping in EmitTopLevelDecl for
  // function declarations to track unused-function coverage.
  static bool deferredUnusedCoverageMapping() { return false; }
  // OGCG returns early for HLSL constant buffer globals (hlsl_constant
  // address space). CIR emits them as standalone globals.
  // OGCG checks CXX20ModuleInits in mayBeEmittedEagerly for VarDecl,
  // deferring module-owned variable initializers. CIR omits this check.
  static bool mayBeEmittedEagerlyCXX20ModuleInits() { return false; }

  // CIRGenerator.cpp missing features
  // moduleTargetTriple -- REMOVED. CIR sets cir.triple attribute on the MLIR
  // module in CIRGenModule constructor. SDK version and Darwin target variant
  // triple are not yet handled.
  // irGenFinishedGuard -- REMOVED. CIRGenerator now sets irGenFinished=true in
  // HandleTranslationUnit and checks it in HandleTopLevelDecl, matching OGCG.
  // unrecoverableErrorCheckLevel -- REMOVED. OGCG uses
  // hasUnrecoverableErrorOccurred() while CIR intentionally uses the stricter
  // hasErrorOccurred(). This is a deliberate design choice to prevent
  // partial/corrupted IR emission, not a missing feature.
  // OGCG creates HandlingTopLevelDeclRAII in HandleTagDeclRequiredDefinition
  // to prevent re-entrant PCH-triggered deferred emission.
  // tagDeclRequiredDefDeferredGuard -- REMOVED. OGCG creates
  // HandlingTopLevelDeclRAII in HandleTagDeclRequiredDefinition to prevent
  // re-entrant PCH-triggered deferred emission during debug info type
  // completion. CIR defers debug metadata to lowering, so this re-entrancy
  // guard is not needed.
  // OGCG overrides CompleteExternalDeclaration to emit external declarations.
  // CIR does not override this ASTConsumer callback.
  // OGCG overrides AssignInheritanceModel to refresh type cache for MSVC
  // member pointer representations. CIR does not override this.

  // CIRGenTypes.cpp missing features
  // functionsBeingProcessed -- REMOVED. No remaining callers.
  // hlslInlineSpirvType -- REMOVED. CIR mirrors OGCG's
  // CIRGenTypes::isZeroInitializable behavior and treats
  // HLSLInlineSpirvType as non-zero-initializable by returning false.
  // packedVectorBoolType -- REMOVED. Packed ext_vector_bool support is wired:
  // convertTypeForMem uses iP storage (P=max(N,8)) and scalar load/store
  // paths round-trip via emitToMemory/emitFromMemory bitcast+shuffle logic.

  // CIRGenExprConstant.cpp missing features
  static bool constAggBuilderCondenseSingleElem() { return false; }
  // constEmitterAbstractArrayFiller -- REMOVED. No remaining callers.
  static bool constEmitterPlaceholderSelfRef() { return false; }
  static bool constExprEmitterIntToIntCast() { return false; }
  // cfiUncheckedCallee -- REMOVED. CFI unchecked callee not yet needed.

  // CIRGenExprScalar.cpp missing features
  static bool bitfieldCompoundAssignSanOpts() { return false; }
  static bool bitfieldConversionCheck() { return false; }
  // elideOverflowSanitizerChecks -- REMOVED. CIR performs the same core
  // overflow-check elision decisions as OGCG for widened/promotion-driven
  // arithmetic in canElideOverflowCheck; extra sanitizer exclusion-pattern
  // pruning remains conservative by design.
  // hlslFRem -- REMOVED. CIR BinOp rem handles both integer and floating-point
  // operands; lowering maps FP rem to llvm.frem, matching OGCG HLSL handling.
  // hlslShiftMasking -- REMOVED. CIR scalar shift emission now applies the
  // same RHS masking for HLSL/OpenCL as OGCG by reusing maskShiftAmount in
  // emitShl/emitShr before creating shift ops.
  // openMPLastprivate -- REMOVED. No remaining behavior was guarded by this
  // flag; OpenMP lastprivate(condition:) support remains tracked by dedicated
  // OpenMP lowering/runtime work.
  // ptrSubExactDiv -- REMOVED. CIR models pointer subtraction as cir.ptr_diff
  // plus explicit VLA scaling. The exactness bit on LLVM integer division is
  // an IR-level optimization hint and is not currently represented in CIR
  // binop semantics.
  // vectorLogicalOpSignExt -- REMOVED. Both CIR code paths for vector logical
  // ops produce correct sign-extension:
  // 1. VisitUnaryLNot: VecCmpOp with wider-than-i1 result type — lowering
  //    always uses SExtOp (LowerToLLVM.cpp:12526).
  // 2. emitVectorLogicalOp (&&/||): createBoolToInt on the result — Clang
  //    Sema::GetSignedVectorType guarantees the result type is always a
  //    signed integer vector, so the lowering uses SExtOp, producing -1
  //    (all ones) for true and 0 for false, matching OGCG's CreateSExt.
  // fpVectorLogicalCmp -- REMOVED. CIR uses VecCmpOp with ne for both FP
  // and integer elements in vector logical ops. The lowering correctly
  // dispatches: FP → FCmpOp(une), integer → ICmpOp(ne), matching OGCG.
  static bool fpSignalingComparisons() { return false; }
  // OGCG shortcuts ExtVector integral casts with CreateIntCast in
  // CK_IntegralCast, bypassing EmitScalarConversion. CIR routes all
  // integral casts through emitScalarConversion.

  // CIRGenExprComplex.cpp missing features
  // complexPromotionForHigherPrecision -- REMOVED. No remaining callers.
  // OGCG calls Scope.ForceCleanup({&Vals.first, &Vals.second}) in
  // VisitExprWithCleanups to defend against dominance problems.
  // complexExprCleanupForceCleanup -- REMOVED. OGCG calls
  // Scope.ForceCleanup({&Vals.first, &Vals.second}) in VisitExprWithCleanups
  // to defend against SSA dominance problems in shared cleanup blocks. CIR's
  // structured control flow (MLIR regions) inherently avoids this: values
  // remain valid within their defining scope without dominance issues.
  // complexExprIgnoreRealImag -- REMOVED. OGCG accepts IgnoreReal/IgnoreImag
  // parameters to skip computing the unused complex component (for __real__
  // and __imag__). CIR preserves the full complex expression and defers dead
  // code elimination to MLIR/LLVM passes. Intentional divergence.

  // CIRGenCXX.cpp missing features
  // OGCG routes TLS variables with C++ destructors through
  // registerGlobalDtorWithTLSAtExit (__cxa_thread_atexit). CIR uses the
  // referenceGlobalInvariantStart -- REMOVED. Despite comments suggesting
  // OGCG calls EmitInvariantStart for reference globals, OGCG actually does
  // NOT (CGDeclCXX.cpp EmitDeclInit only calls EmitDeclInvariant for
  // non-reference types). CIR is already correct.
  // atExitFallback -- REMOVED. OGCG falls back to atexit() when
  // -fno-use-cxa-atexit disables __cxa_atexit. CIR always uses
  // __cxa_atexit because it is universally available on CIR's supported
  // targets (glibc/musl Linux, Android Bionic). Using __cxa_atexit is
  // strictly better (supports per-DSO cleanup ordering via __dso_handle).
  // emitDefinitionAsAlias -- REMOVED. OGCG's EmitDefinitionAsAlias is used
  // by MicrosoftCXXABI for destructor aliasing (Windows-only). Itanium
  // destructor aliasing is handled directly in tryEmitBaseDestructorAsAlias.

  // CIRGenExprCXX.cpp missing features
  // cfiNVCall -- REMOVED. CFI non-virtual call checks not yet needed.
  static bool commonBuildCXXMemberCallGlobalDecl() { return false; }
  // dynamicCastSourceNullCheck -- REMOVED. The source pointer null check
  // for dynamic_cast is implemented in LoweringPrepareItaniumCXXABI::
  // lowerDynamicCast, which wraps non-reference casts in a TernaryOp that
  // returns null for null source pointers.
  // memberPointerABIDispatch -- REMOVED. CIR uses a high-level
  // cir.get_method op at CIRGen time, deferring ABI-specific member
  // function pointer resolution to ItaniumCXXABI::lowerGetMethod in
  // the target lowering pass.
  // newDeleteTypeIdentityArg -- REMOVED. CIR now threads the type-identity
  // argument through new-expression cleanup and emits type-aware operator
  // delete calls matching OGCG for C++26 type-aware allocation/deallocation.
  // sanitizePerformTypeCheck -- REMOVED. CIR now implements the UBSan
  // null/vptr type check pipeline (EmitTypeCheck) for C++ member calls,
  // delete expressions, and dynamic_cast paths.
  // trivialAssignmentOptimization -- REMOVED. OGCG bypasses trivial
  // copy/move assignment operator calls with direct EmitAggregateAssign.
  // CIR intentionally preserves the operator call and defers this
  // optimization to later passes, matching the CIR design philosophy of
  // preserving high-level structure.

  // CIRGenFunction.cpp missing features
  // checkFunctionABI -- REMOVED. Target ABI validation deferred to call conv lowering.
  // finishFunctionCleanupAsserts -- REMOVED. Actual cleanup stack assertions
  // (lifetimeExtendedCleanupStack, deferredDeactivationCleanupStack, ehStack)
  // are now checked directly in CIRGenFunction::finishFunction, matching OGCG.
  // lambdaInAllocaBody -- REMOVED. Lambda inalloca call operator body
  // dispatch is x86-32 Windows/MSVC ABI only. Not a CIR target.
  // mcountFentryProfiling -- REMOVED. CIR now handles -pg, -mfentry,
  // -mnop-mcount, and -mrecord-mcount in setCIRFunctionAttributesForDefinition.
  static bool mcdcCoverageFolding() { return false; }
  // norecurseMainAttr -- REMOVED. norecurse and stackrealign-on-main are now
  // handled in CIRGenModule::setNonAliasAttributes.
  // pgoRegionCounters -- REMOVED. CIR does not maintain OGCG's per-function
  // region-counter map/verification bookkeeping in CIRGenFunction; remaining
  // profile/coverage gaps are tracked by dedicated guards.
  // profileInstrBlocking -- REMOVED. CIR's profile-instrumentation attribute
  // handling in startFunction no longer has a guarded OGCG parity gap.
  // sanitizerFunctionAttrs -- REMOVED. CIR configures sanitizer-related
  // function behavior through existing function-attribute setup paths
  // (for example strictfp/XRay/profile handling) and does not keep a dedicated
  // startFunction MissingFeatures gate.
  // vscaleRange -- REMOVED. CIR computes vscale_range from TargetInfo in
  // startFunction and stores it in extra_attrs, lowered via LLVMFuncOp.
  // semanticInterposition -- REMOVED. OGCG's TryMarkNoThrow does not
  // effectively check this (module flag set after codegen). We match OGCG.

  // CIRGenExpr.cpp missing features
  // OGCG sets !invariant.load metadata on loads from const globals, specific
  // ObjC/GPU loads, and vtable virtual function slot loads under
  // -fstrict-vtable-pointers. CIR currently implements the strict-vtable
  // virtual slot load case via cir.invariant_load in load lowering, but still
  static bool memberExprIsInBounds() { return false; }
  // vectorEltBitvectorBitcast -- REMOVED. OGCG bitcasts iN scalars to
  // <N x i1> around insertelement when vectors are represented as packed
  // integers in LLVM IR. CIR vector element stores operate on first-class
  // CIR vector types, so no integer<->vector bitcast path is required.
  // vectorEltNarrowTypeZExt -- REMOVED. In CIR, AST-level ImplicitCastExpr
  // already converts the source to the vector element type, and VecInsertOp
  // enforces type matching, so the OGCG zero-extension safety net is
  // unnecessary.

  // CIRGenItaniumCXXABI.cpp missing features
  // guardedInitMaxInlineWidthCheck -- REMOVED. OGCG skips the inline
  // fast-path for guarded initialization when MaxAtomicInlineWidth == 0
  // (target has no inline atomics), falling back to unconditional
  // __cxa_guard_acquire. CIR's supported targets (AArch64: 128-bit,
  // x86_64: 64/128-bit) always have MaxAtomicInlineWidth > 0, so the
  // inline fast-path is always taken.
  // openMPNoAtExitDtor -- REMOVED. CIR now mirrors OGCG by lowering
  // non-static, non-TLS OpenMP-offload global dtors (when hasAtExit() is
  // false) to llvm.global_dtors instead of __cxa_atexit registration.
  // tlsInitFunc -- REMOVED. CIR now emits TLS wrapper functions in CIRGen and
  // materializes __tls_init/__tls_guard plus _ZTH aliases in LoweringPrepare,
  // then patches wrappers to call TLS init before access, matching OGCG's
  // lazy per-thread initialization model.
  // use32BitVTableOffsetABI -- REMOVED. OGCG truncates vtable offsets to
  // 32 bits in member function pointer resolution on Apple ARM64. Apple
  // ARM64 is not a primary CIR target; member pointer resolution uses
  // generic createGetMethod without offset truncation.
  // vtableAddressPointInRange — REMOVED. VTableAddrPointOp now carries
  // vtable_size and address_point_offset attrs for inrange computation.

  // CIRGenException.cpp missing features
  // freeExceptionScalarCleanup -- REMOVED. CIR now mirrors OGCG by always
  // pushing FreeException cleanup in emitAnyExprToExn, including scalar throw
  // operands whose expression evaluation may throw after allocation.
  // terminateHandlerABIDispatch -- REMOVED. terminate handler emission now
  // directly performs ABI-specific terminate symbol selection in
  // CIRGenFunction::emitTerminateHandlerBody, matching OGCG behavior at this
  // site without a stale MissingFeatures guard.
  // ehMayThrowVsHasEHBranches -- REMOVED. CIR uses EHScope::scopeMayThrow as
  // the EH reachability signal for catch-scope early bailout in
  // exitCXXTryStmt, matching OGCG's hasEHBranches behavior at this site.
  // zosPersonality -- REMOVED. OGCG selects ZOS_CPlusPlus personality for
  // z/OS targets. z/OS is not a supported CIR target.
  // cygMinGNUStepPersonality -- REMOVED. OGCG selects GNU_CPlusPlus_SEH
  // personality for GNUstep ObjC on Cygwin/MinGW targets. Cygwin/MinGW is
  // not a supported CIR target.

  // CIRGenAtomic.cpp missing features
  // atomicStoreMemOrderDowngrade -- REMOVED. CIR already downgrades
  // Acquire→Relaxed and AcquireRelease→Release for atomic stores
  // (CIRGenAtomic.cpp), matching OGCG's behavior (CGAtomic.cpp:2099-2102).

  // CIRGenCXXABI missing features
  // virtualDtorCallOutParam -- REMOVED. OGCG's
  // EmitVirtualDestructorCall takes a CallBase** out-parameter so callers can
  // annotate the emitted call (for example musttail). In CIR this is not
  // needed: Clang rejects [[clang::musttail]] on destructor calls at Sema, so
  // there is no valid call-site that needs post-emit virtual-dtor
  // CallOp annotation through an out-parameter.
  // cxxabiTerminateForUnexpectedException -- REMOVED. No remaining callers.
  // OGCG's emitTerminateForUnexpectedException calls std::terminate via
  // nounwind runtime call.
  // OGCG uses EHFilterScope for dynamic exception specifications (throw(X,Y)).
  // For throw(), OGCG uses filter [0 x ptr] + __cxa_call_unexpected. CIR
  // treats throw() the same as noexcept (pushTerminate) which calls
  // std::terminate instead. Non-empty throw lists (EST_Dynamic) are NYI.
  // ehFilterScope -- REMOVED. CIR does not implement OGCG EHFilterScope/
  // __cxa_call_unexpected flow; throw() (pre-C++17 EST_DynamicNone) is
  // modeled as terminate scope and non-empty dynamic exception specs remain
  // explicit NYI diagnostics.
  // OGCG has mayNeedDestruction and isEmittedWithConstantInitializer helpers
  // for guarded/TLS init logic. CIR is missing these.

  // CIRGenVTables.cpp missing features
  // thunkEmptyGlobalDecl -- REMOVED. OGCG passes empty GlobalDecl() to
  // StartFunction to avoid GD-dependent codepaths, then manually sets
  // CurCodeDecl/CurFuncDecl. CIR intentionally passes the real GlobalDecl
  // and relies on startFunction handling the prolog directly.
  // thunkReturnABIIndirect -- REMOVED. OGCG also creates a ReturnValueSlot
  // when ABIArgInfo is Indirect. CIR defers ABI-indirect return handling to
  // the CallConvLowering pass, so hasAggregateEvaluationKind is sufficient
  // at CIRGen time.
  // thunkReturnSlotOpts -- REMOVED. CIR now passes
  // resultType.isVolatileQualified() to the ReturnValueSlot constructor in
  // thunks. IsExternallyDestructed is modeled at the target lowering level
  // (LowerCall.h), not in CIRGen's ReturnValueSlot.
  // ps4DLLImportExportVTable -- REMOVED. OGCG calls
  // setVTableSelectiveDLLImportExport for PS4/PS5 targets. PS4/PS5 is not
  // a supported CIR target.
  // unprototypedThunkAttribute -- REMOVED. CIR now propagates the LLVM
  // "thunk" function attribute via FuncOp extra_attrs for unprototyped
  // virtual thunks and lowers it as an LLVM passthrough attribute.

  // CIRGenExprCXX.cpp missing features
  // newExprConditionalEvaluation -- REMOVED. CIR models null-checked new
  // expression initialization with structured cir.ternary regions and
  // branch-local cleanup deactivation, preserving conditional cleanup
  // semantics without OGCG's ConditionalEvaluation wrapper.
  // REMOVED: sanitizer-checked forwarding for aggregate ctor calls.
  // CIR currently keeps sanitizers globally NYI via MissingFeatures::sanitizers
  // guards, so there is no active sanitizer flow to thread here.
  // newArraySizeWiderThanSizeT -- REMOVED. CIR now matches OGCG for dynamic
  // new[] counts wider than size_t by comparing against (1 << sizeWidth) in
  // source width and truncating to size_t before overflow arithmetic.

  // targetMaxVectorAlign -- REMOVED. CIRLowerContext vector type info now
  // applies TargetInfo::getMaxVectorAlign() to cap vector alignment.

  // ppcDoubleDoubleLongDouble -- REMOVED. PPC double-double long double
  // format library call fallback. PPC is not a supported CIR target.

  // CIRGenDecl.cpp missing features
  // msCatchParamLifetime -- REMOVED. OGCG skips lifetime markers for MSVC
  // catch parameters. MSVC/Windows is not a supported CIR target.

  // CGBuiltin.cpp — EmitX86BitTestIntrinsic uses inline asm (bt/btc/btr/bts)
  // for better codegen on x86. CIR uses the generic byte-level path for now.
  static bool x86BitTestInlineAsm() { return false; }

};

} // namespace cir

#endif // CLANG_CIR_MISSINGFEATURES_H
