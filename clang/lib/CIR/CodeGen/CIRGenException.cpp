//===--- CIRGenException.cpp - Emit CIR Code for C++ exceptions -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with C++ exception related code generation.
//
//===----------------------------------------------------------------------===//

#include "CIRGenCXXABI.h"
#include "CIRGenCleanup.h"
#include "CIRGenFunction.h"

#include "clang/CIR/MissingFeatures.h"
#include "clang/AST/StmtObjC.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/SaveAndRestore.h"

namespace {
/// A cleanup to free the exception object if its initialization throws.
struct FreeException final : clang::CIRGen::EHScopeStack::Cleanup {
  mlir::Value exn;
  FreeException(mlir::Value exn) : exn(exn) {}
  void emit(clang::CIRGen::CIRGenFunction &cgf, Flags flags) override {
    auto &builder = cgf.getBuilder();
    auto loc = cgf.currSrcLoc ? *cgf.currSrcLoc : builder.getUnknownLoc();

    // void __cxa_free_exception(void *thrown_exception);
    auto voidTy = cir::VoidType::get(&cgf.getMLIRContext());
    auto ptrTy = builder.getVoidPtrTy(
        mlir::cast<cir::PointerType>(exn.getType()).getAddrSpace());
    auto freeExnFnTy = cir::FuncType::get({ptrTy}, voidTy);
    cir::FuncOp freeExnFn = cgf.cgm.createRuntimeFunction(
        freeExnFnTy, "__cxa_free_exception");

    // Cast the exception pointer to void* if needed.
    mlir::Value exnPtr = exn;
    if (exnPtr.getType() != ptrTy)
      exnPtr = cir::CastOp::create(builder, loc, ptrTy,
                                    cir::CastKind::bitcast, exnPtr);

    // OGCG uses EmitNounwindRuntimeCall for __cxa_free_exception, which
    // marks the call as nounwind. __cxa_free_exception is specified to never
    // throw, so this is semantically correct and allows the optimizer to
    // simplify EH tables.
    auto callOp = builder.createCallOp(loc, freeExnFn, {exnPtr});
    callOp.setNothrow(true);
  }
};

static mlir::Type
getObjCFragileExceptionDataTy(clang::CIRGen::CIRGenFunction &cgf) {
  clang::CIRGen::CIRGenBuilderTy &builder = cgf.getBuilder();
  auto setJmpBufferTy = cir::ArrayType::get(builder.getSInt32Ty(), 18);
  auto stackPtrTy =
      cir::ArrayType::get(builder.getPointerTo(builder.getVoidPtrTy()), 4);
  return builder.getAnonRecordTy({setJmpBufferTy, stackPtrTy},
                                 /*packed=*/false,
                                 /*padded=*/false);
}

static void mergeCallExtraAttr(cir::CallOp call, llvm::StringRef name,
                               mlir::Attribute value) {
  mlir::NamedAttrList attrs;
  if (auto existing = call.getExtraAttrsAttr())
    attrs = mlir::NamedAttrList{existing.getElements().getValue()};
  attrs.set(name, value);
  call->setAttr("extra_attrs", cir::ExtraFuncAttributesAttr::get(
                                   attrs.getDictionary(call.getContext())));
}

static void mergeFuncExtraAttr(cir::FuncOp fn, llvm::StringRef name,
                               mlir::Attribute value) {
  mlir::NamedAttrList attrs;
  if (auto existing = fn.getExtraAttrs())
    attrs = mlir::NamedAttrList{existing->getElements().getValue()};
  attrs.set(name, value);
  fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
      attrs.getDictionary(fn.getContext())));
}

static void markNoReturnCall(cir::CallOp call, mlir::MLIRContext &ctx) {
  auto noReturn = cir::NoReturnAttr::get(&ctx);
  mergeCallExtraAttr(call, noReturn.getMnemonic(), noReturn);
}

static void markReturnsTwice(cir::CallOp call, mlir::MLIRContext &ctx) {
  mergeCallExtraAttr(call, "returns_twice", mlir::UnitAttr::get(&ctx));
}

static cir::FuncOp
getObjCFragileSetJmpFn(clang::CIRGen::CIRGenFunction &cgf) {
  clang::CIRGen::CIRGenBuilderTy &builder = cgf.getBuilder();
  auto voidPtrTy = builder.getVoidPtrTy();
  auto intTy = builder.getSInt32Ty();
  cir::FuncType fnTy = cir::FuncType::get({voidPtrTy}, intTy);
  cir::FuncOp fn = cgf.cgm.createRuntimeFunction(fnTy, "_setjmp");
  mergeFuncExtraAttr(fn, "returns_twice",
                     mlir::UnitAttr::get(&cgf.getMLIRContext()));
  mergeFuncExtraAttr(fn, "nonlazybind",
                     mlir::UnitAttr::get(&cgf.getMLIRContext()));
  return fn;
}

static void emitFragileHazardAsm(clang::CIRGen::CIRGenFunction &cgf,
                                 mlir::Location loc,
                                 llvm::ArrayRef<mlir::Value> locals,
                                 llvm::StringRef memConstraint,
                                 bool outputs) {
  if (locals.empty())
    return;

  clang::CIRGen::CIRGenBuilderTy &builder = cgf.getBuilder();
  std::string constraints;
  for (unsigned i = 0, e = locals.size(); i != e; ++i) {
    if (i)
      constraints += ',';
    constraints += memConstraint;
  }

  std::string_view clobbers = cgf.getTarget().getClobbers();
  if (!clobbers.empty()) {
    if (!constraints.empty())
      constraints += ',';
    constraints += clobbers;
  }

  llvm::SmallVector<mlir::Attribute> operandAttrs;
  operandAttrs.reserve(locals.size());
  for (mlir::Value local : locals) {
    (void)local;
    operandAttrs.push_back(mlir::UnitAttr::get(&cgf.getMLIRContext()));
  }

  llvm::SmallVector<mlir::ValueRange, 3> operands = {
      outputs ? mlir::ValueRange(locals) : mlir::ValueRange{},
      outputs ? mlir::ValueRange{} : mlir::ValueRange(locals),
      mlir::ValueRange{}};

  (void)cir::InlineAsmOp::create(
      builder, loc, mlir::Type{}, operands, /*asmString=*/"",
      constraints, /*sideEffects=*/true, cir::AsmFlavor::x86_att,
      builder.getArrayAttr(operandAttrs));
}

class FragileObjCHazards {
public:
  explicit FragileObjCHazards(clang::CIRGen::CIRGenFunction &cgf) : cgf(cgf) {
    mlir::Block *entry = cgf.getCurFunctionEntryBlock();
    for (mlir::Operation &op : *entry)
      if (auto alloca = mlir::dyn_cast<cir::AllocaOp>(op))
        if (!cgf.fnRetAlloca || alloca.getResult() != *cgf.fnRetAlloca)
          locals.push_back(alloca.getResult());

    visitBlocks(*mlir::dyn_cast<cir::FuncOp>(cgf.curFn).getCallableRegion(),
                [&](mlir::Block &block) { blocksBeforeTry.insert(&block); });
  }

  void emitWriteHazard(mlir::Location loc) {
    emitFragileHazardAsm(cgf, loc, locals, "=*m", /*outputs=*/true);
  }

  void emitReadHazardsInNewBlocks() {
    if (locals.empty())
      return;

    visitBlocks(*mlir::dyn_cast<cir::FuncOp>(cgf.curFn).getCallableRegion(),
                [&](mlir::Block &block) {
                  if (blocksBeforeTry.contains(&block))
                    return;

                  llvm::SmallVector<mlir::Operation *> protectedCalls;
                  for (mlir::Operation &op : block) {
                    if (mlir::isa<cir::LLVMIntrinsicCallOp>(op))
                      continue;

                    auto call = mlir::dyn_cast<cir::CIRCallOpInterface>(op);
                    if (!call || op.hasAttr("nothrow"))
                      continue;

                    protectedCalls.push_back(&op);
                  }

                  for (mlir::Operation *op : protectedCalls) {
                    mlir::OpBuilder::InsertionGuard guard(cgf.getBuilder());
                    cgf.getBuilder().setInsertionPoint(op);
                    emitFragileHazardAsm(cgf, op->getLoc(), locals, "*m",
                                         /*outputs=*/false);
                  }
                });
  }

private:
  template <typename FnTy>
  static void visitBlocks(mlir::Region &region, FnTy &&visit) {
    for (mlir::Block &block : region) {
      visit(block);
      for (mlir::Operation &op : block)
        for (mlir::Region &nestedRegion : op.getRegions())
          visitBlocks(nestedRegion, visit);
    }
  }

  clang::CIRGen::CIRGenFunction &cgf;
  llvm::SmallVector<mlir::Value, 8> locals;
  llvm::SmallPtrSet<mlir::Block *, 8> blocksBeforeTry;
};
} // namespace

using namespace clang;
using namespace clang::CIRGen;

const EHPersonality EHPersonality::GNU_C = {"__gcc_personality_v0", nullptr};
const EHPersonality EHPersonality::GNU_C_SJLJ = {"__gcc_personality_sj0",
                                                 nullptr};
const EHPersonality EHPersonality::GNU_C_SEH = {"__gcc_personality_seh0",
                                                nullptr};
const EHPersonality EHPersonality::NeXT_ObjC = {"__objc_personality_v0",
                                                nullptr};
const EHPersonality EHPersonality::GNU_CPlusPlus = {"__gxx_personality_v0",
                                                    nullptr};
const EHPersonality EHPersonality::GNU_CPlusPlus_SJLJ = {
    "__gxx_personality_sj0", nullptr};
const EHPersonality EHPersonality::GNU_CPlusPlus_SEH = {
    "__gxx_personality_seh0", nullptr};
const EHPersonality EHPersonality::GNU_ObjC = {"__gnu_objc_personality_v0",
                                               "objc_exception_throw"};
const EHPersonality EHPersonality::GNU_ObjC_SJLJ = {
    "__gnu_objc_personality_sj0", "objc_exception_throw"};
const EHPersonality EHPersonality::GNU_ObjC_SEH = {
    "__gnu_objc_personality_seh0", "objc_exception_throw"};
const EHPersonality EHPersonality::GNU_ObjCXX = {
    "__gnustep_objcxx_personality_v0", nullptr};
const EHPersonality EHPersonality::GNUstep_ObjC = {
    "__gnustep_objc_personality_v0", nullptr};
const EHPersonality EHPersonality::MSVC_except_handler = {"_except_handler3",
                                                          nullptr};
const EHPersonality EHPersonality::MSVC_C_specific_handler = {
    "__C_specific_handler", nullptr};
const EHPersonality EHPersonality::MSVC_CxxFrameHandler3 = {
    "__CxxFrameHandler3", nullptr};
const EHPersonality EHPersonality::GNU_Wasm_CPlusPlus = {
    "__gxx_wasm_personality_v0", nullptr};
const EHPersonality EHPersonality::XL_CPlusPlus = {"__xlcxx_personality_v1",
                                                   nullptr};
const EHPersonality EHPersonality::ZOS_CPlusPlus = {"__zos_cxx_personality_v2",
                                                    nullptr};

static const EHPersonality &getCPersonality(const TargetInfo &target,
                                            const CodeGenOptions &cgOpts) {
  const llvm::Triple &triple = target.getTriple();
  if (triple.isWindowsMSVCEnvironment())
    return EHPersonality::MSVC_CxxFrameHandler3;
  if (cgOpts.hasSjLjExceptions())
    return EHPersonality::GNU_C_SJLJ;
  if (cgOpts.hasDWARFExceptions())
    return EHPersonality::GNU_C;
  if (cgOpts.hasSEHExceptions())
    return EHPersonality::GNU_C_SEH;
  return EHPersonality::GNU_C;
}

static const EHPersonality &getObjCPersonality(const TargetInfo &target,
                                               const LangOptions &langOpts,
                                               const CodeGenOptions &cgOpts) {
  const llvm::Triple &triple = target.getTriple();
  if (triple.isWindowsMSVCEnvironment())
    return EHPersonality::MSVC_CxxFrameHandler3;

  switch (langOpts.ObjCRuntime.getKind()) {
  case ObjCRuntime::FragileMacOSX:
    return getCPersonality(target, cgOpts);
  case ObjCRuntime::MacOSX:
  case ObjCRuntime::iOS:
  case ObjCRuntime::WatchOS:
    return EHPersonality::NeXT_ObjC;
  case ObjCRuntime::GNUstep:
    if (triple.isOSCygMing())
      return EHPersonality::GNU_CPlusPlus_SEH;
    if (langOpts.ObjCRuntime.getVersion() >= VersionTuple(1, 7))
      return EHPersonality::GNUstep_ObjC;
    [[fallthrough]];
  case ObjCRuntime::GCC:
  case ObjCRuntime::ObjFW:
    if (cgOpts.hasSjLjExceptions())
      return EHPersonality::GNU_ObjC_SJLJ;
    if (cgOpts.hasSEHExceptions())
      return EHPersonality::GNU_ObjC_SEH;
    return EHPersonality::GNU_ObjC;
  }
  llvm_unreachable("bad runtime kind");
}

static const EHPersonality &getCXXPersonality(const TargetInfo &target,
                                              const CodeGenOptions &cgOpts) {
  const llvm::Triple &triple = target.getTriple();
  if (triple.isWindowsMSVCEnvironment())
    return EHPersonality::MSVC_CxxFrameHandler3;
  if (triple.isOSAIX())
    return EHPersonality::XL_CPlusPlus;
  // OGCG also checks isOSzOS() here and returns ZOS_CPlusPlus. z/OS is not
  // a supported CIR target (arm64/x86_64 Linux, Android, GPUs only).
  if (cgOpts.hasSjLjExceptions())
    return EHPersonality::GNU_CPlusPlus_SJLJ;
  if (cgOpts.hasDWARFExceptions())
    return EHPersonality::GNU_CPlusPlus;
  if (cgOpts.hasSEHExceptions())
    return EHPersonality::GNU_CPlusPlus_SEH;
  if (cgOpts.hasWasmExceptions())
    return EHPersonality::GNU_Wasm_CPlusPlus;
  return EHPersonality::GNU_CPlusPlus;
}

/// Determines the personality function to use when both C++
/// and Objective-C exceptions are being caught.
static const EHPersonality &getObjCXXPersonality(const TargetInfo &target,
                                                 const LangOptions &langOpts,
                                                 const CodeGenOptions &cgOpts) {
  if (target.getTriple().isWindowsMSVCEnvironment())
    return EHPersonality::MSVC_CxxFrameHandler3;

  switch (langOpts.ObjCRuntime.getKind()) {
  // In the fragile ABI, just use C++ exception handling and hope
  // they're not doing crazy exception mixing.
  case ObjCRuntime::FragileMacOSX:
    return getCXXPersonality(target, cgOpts);

  // The ObjC personality defers to the C++ personality for non-ObjC
  // handlers.  Unlike the C++ case, we use the same personality
  // function on targets using (backend-driven) SJLJ EH.
  case ObjCRuntime::MacOSX:
  case ObjCRuntime::iOS:
  case ObjCRuntime::WatchOS:
    return getObjCPersonality(target, langOpts, cgOpts);

  case ObjCRuntime::GNUstep:
    if (target.getTriple().isOSCygMing())
      return EHPersonality::GNU_CPlusPlus_SEH;
    return EHPersonality::GNU_ObjCXX;

  // The GCC runtime's personality function inherently doesn't support
  // mixed EH.  Use the ObjC personality just to avoid returning null.
  case ObjCRuntime::GCC:
  case ObjCRuntime::ObjFW:
    return getObjCPersonality(target, langOpts, cgOpts);
  }
  llvm_unreachable("bad runtime kind");
}

static const EHPersonality &getSEHPersonalityMSVC(const llvm::Triple &triple) {
  return triple.getArch() == llvm::Triple::x86
             ? EHPersonality::MSVC_except_handler
             : EHPersonality::MSVC_C_specific_handler;
}

const EHPersonality &EHPersonality::get(CIRGenModule &cgm,
                                        const FunctionDecl *fd) {
  const llvm::Triple &triple = cgm.getTarget().getTriple();
  const LangOptions &langOpts = cgm.getLangOpts();
  const CodeGenOptions &cgOpts = cgm.getCodeGenOpts();
  const TargetInfo &target = cgm.getTarget();

  // Functions using SEH get an SEH personality.
  if (fd && fd->usesSEHTry())
    return getSEHPersonalityMSVC(triple);

  if (langOpts.ObjC) {
    return langOpts.CPlusPlus ? getObjCXXPersonality(target, langOpts, cgOpts)
                              : getObjCPersonality(target, langOpts, cgOpts);
  }
  return langOpts.CPlusPlus ? getCXXPersonality(target, cgOpts)
                            : getCPersonality(target, cgOpts);
}

const EHPersonality &EHPersonality::get(CIRGenFunction &cgf) {
  const auto *fg = cgf.curCodeDecl;
  // For outlined finallys and filters, use the SEH personality in case they
  // contain more SEH. This mostly only affects finallys. Filters could
  // hypothetically use gnu statement expressions to sneak in nested SEH.
  fg = fg ? fg : cgf.curSEHParent.getDecl();
  return get(cgf.cgm, dyn_cast_or_null<FunctionDecl>(fg));
}

static llvm::StringRef getPersonalityFn(CIRGenModule &cgm,
                                        const EHPersonality &personality) {
  // Create the personality function type: i32 (...)
  mlir::Type i32Ty = cgm.getBuilder().getI32Type();
  auto funcTy = cir::FuncType::get({}, i32Ty, /*isVarArg=*/true);

  cir::FuncOp personalityFn = cgm.createRuntimeFunction(
      funcTy, personality.personalityFn, mlir::ArrayAttr(), /*isLocal=*/true);

  return personalityFn.getSymName();
}

bool CIRGenFunction::exceptionEscapesOpenMPRegion() {
  if (!insideOutlinedOpenMPRegion || openMPRegionEHDepth.empty())
    return false;

  // OpenMP-internal data-sharing codegen (lastprivate copy-back, firstprivate
  // copy-in, reduction combiner) emits compiler-generated copy/combine calls at
  // the region boundary. Those are not user exceptions escaping the region, so
  // do not gate them -- their unwind edge lands on the region's own teardown.
  if (insideOpenMPRegionDataSharingCopy)
    return false;

  // A scope pushed after region entry is strictly enclosed by the snapshot; a
  // scope present at entry is not -- it lives outside the region. Walk outward
  // from the innermost active EH scope: cross in-region cleanups (they always
  // re-raise), stop at any in-region non-cleanup handler (does not escape), and
  // report escape as soon as an out-of-region scope becomes the unwind target.
  EHScopeStack::stable_iterator snapshot = openMPRegionEHDepth.back();
  for (EHScopeStack::stable_iterator si = ehStack.getInnermostEHScope();
       si.isValid() && si != EHScopeStack::stable_end();) {
    if (!snapshot.strictlyEncloses(si))
      return true;
    EHScope &scope = *ehStack.find(si);
    if (scope.getKind() != EHScope::Cleanup)
      return false;
    si = scope.getEnclosingEHScope();
  }
  return false;
}

void CIRGenFunction::emitCXXThrowExpr(const CXXThrowExpr *e) {
  const llvm::Triple &triple = getTarget().getTriple();
  if (cgm.getLangOpts().OpenMPIsTargetDevice && triple.isGPU()) {
    cgm.errorNYI("emitCXXThrowExpr OpenMP with NVPTX or AMDGCN Triples");
    return;
  }

  // When a throw appears in a scope with active EH cleanup (RAII destructors),
  // we handle two cases:
  //
  // Case 1 (no enclosing try): Create a synthetic TryOp wrapping the throw.
  // FlattenCFG converts the throw to cir.call @__cxa_throw with a landing pad
  // that runs destructors + resume. This mirrors emitCallLikeOp in
  // CIRGenCall.cpp for regular calls without a surrounding try block.
  //
  // Case 2 (enclosing try): Convert the cir.throw to a cir.call exception
  // @__cxa_throw during CIRGen, then populate its per-call cleanup region
  // via callWithExceptionCtx + populateCatchHandlersIfRequired. This way
  // FlattenCFG sees a regular cir.call exception with populated cleanup and
  // creates a SINGLE landing pad with both cleanup (destructors) and catch
  // handlers, matching OGCG's `landingpad cleanup catch ptr @_ZTIi`.
  // Without this, a nested synthetic TryOp would cause FlattenCFG to create
  // TWO separate landing pads (cleanup-only inner, catch-only outer), and
  // the inner resume would bypass the outer catch entirely.
  bool needsCleanup = isCatchOrCleanupRequired();

  // A direct C++ throw inside an outlined OpenMP region whose active unwind
  // destination is an EH scope pushed *outside* the region (e.g. a try wrapping
  // the region) would acquire an unwind edge crossing the omp region boundary.
  // CIRGen builds that edge as a cir.throw tied to the enclosing cir.try, whose
  // unwind region lies outside the omp op; FlattenCFG then produces a malformed
  // cross-region block successor that crashes collectUnreachable's block-BFS
  // during CIR-to-LLVM lowering. This is the throw-site analogue of the
  // throwing-call gate in emitCall (CIRGenCall.cpp): a plain call there is
  // forced to isInvoke=false; here the throw is emitted as a plain terminating
  // throw with no unwind edge. exceptionEscapesOpenMPRegion walks the EH chain
  // so the gate still fires when in-region cleanups (RAII/ARC/cleanup attr) sit
  // between the throw and the out-of-region handler. A throw whose unwind stops
  // at an in-region handler, or one with no enclosing handler at all
  // (needsCleanup=false), is unaffected.
  bool escapesOpenMPRegion = needsCleanup && exceptionEscapesOpenMPRegion();
  if (escapesOpenMPRegion) {
    cgm.errorNYI(e->getSourceRange(), "exception escaping an OpenMP region");
    needsCleanup = false;
  }

  cir::TryOp enclosingTryOp;
  if (needsCleanup)
    enclosingTryOp = curLexScope->getClosestTryParent();

  // Inline conversion is only needed when there are active EH cleanup scopes
  // (RAII destructors) between the throw and the enclosing try. Without cleanup
  // scopes, FlattenCFG handles the throw correctly via its ThrowOp walk.
  bool hasEHCleanupScope = false;
  if (needsCleanup && enclosingTryOp) {
    if (e->getSubExpr()) {
      // Regular throw: any EH cleanup scope means inline conversion needed.
      EHScope &innermost = *ehStack.find(ehStack.getInnermostEHScope());
      hasEHCleanupScope = (innermost.getKind() == EHScope::Cleanup &&
                           cast<EHCleanupScope>(innermost).isEHCleanup());
    } else {
      // Rethrow: __cxa_rethrow handles __cxa_end_catch internally, so we
      // only need inline conversion when there are actual RAII destructor
      // cleanups beyond the __cxa_end_catch cleanup.
      //
      // Walk the EH scope stack from innermost. In a catch handler, the
      // stack looks like: [RAII cleanups...] [__cxa_end_catch] [CatchScope].
      // The __cxa_end_catch cleanup is always immediately above its
      // CatchScope. When a nested try-catch exists inside the catch
      // handler, the stack looks like:
      //   [RAII cleanups...] [inner CatchScope] [__cxa_end_catch] [outer CatchScope]
      // In this case, the RAII cleanups are separated from __cxa_end_catch
      // by the inner CatchScope.
      //
      // Strategy: count EH cleanup scopes before the first CatchScope.
      // Then check if a CatchScope follows. If the first CatchScope
      // encountered is NOT followed by __cxa_end_catch (i.e., it's an
      // inner nested CatchScope), any cleanups found before it are RAII.
      // If the first CatchScope IS paired with __cxa_end_catch (direct
      // catch handler), then 1 cleanup = __cxa_end_catch, 2+ = RAII.
      int ehCleanupCount = 0;
      bool hitCatchScope = false;
      for (EHScopeStack::stable_iterator si = ehStack.getInnermostEHScope();
           si != ehStack.stable_end();
           si = ehStack.find(si)->getEnclosingEHScope()) {
        EHScope &s = *ehStack.find(si);
        if (s.getKind() == EHScope::Catch) {
          hitCatchScope = true;
          break;
        }
        if (s.getKind() != EHScope::Cleanup)
          break;
        if (cast<EHCleanupScope>(s).isEHCleanup())
          ehCleanupCount++;
      }
      // If we hit a CatchScope, any cleanups found before it are RAII —
      // the __cxa_end_catch cleanup is on the other side of the CatchScope.
      // If we didn't hit a CatchScope (just cleanups), then the first
      // cleanup is __cxa_end_catch and we need > 1 for RAII.
      if (hitCatchScope)
        hasEHCleanupScope = (ehCleanupCount > 0);
      else
        hasEHCleanupScope = (ehCleanupCount > 1);
    }
  }

  bool needsSyntheticTry = needsCleanup && !enclosingTryOp;
  bool needsInlineConversion = needsCleanup && enclosingTryOp && hasEHCleanupScope;
  bool emittedObjCThrow = false;

  cir::TryOp syntheticTryOp;
  mlir::OpBuilder::InsertPoint savedIP;

  if (needsSyntheticTry) {
    mlir::Location loc = *currSrcLoc;

    // Check if a Terminate or Filter scope is present.
    // This determines whether the handler should be a cleanup+unwind,
    // a catch-all terminate handler, or a filter handler. Check both
    // direct Terminate/Filter and Terminate/Filter behind Cleanup scopes.
    bool isTerminateScope = false;
    bool isFilterScope = false;
    EHFilterScope *filterScope = nullptr;
    for (EHScopeStack::stable_iterator si = ehStack.getInnermostEHScope();
         si != ehStack.stable_end();
         si = ehStack.find(si)->getEnclosingEHScope()) {
      EHScope &s = *ehStack.find(si);
      if (s.getKind() == EHScope::Terminate) {
        isTerminateScope = true;
        break;
      }
      if (s.getKind() == EHScope::Filter) {
        isFilterScope = true;
        filterScope = &cast<EHFilterScope>(s);
        break;
      }
      if (s.getKind() != EHScope::Cleanup)
        break;
    }

    syntheticTryOp = cir::TryOp::create(
        builder, loc,
        /*tryBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location l) {},
        /*handlersBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location l,
            mlir::OperationState &result) {
          auto *r = result.addRegion();
          builder.createBlock(r);
          if (isTerminateScope || isFilterScope) {
            // For Terminate/Filter scope: empty handler region, will be
            // populated by emitTerminateHandlerBody or
            // emitFilterHandlerBody below.
            cir::ResumeOp::create(builder, loc);
          } else {
            // Emit EH cleanup bodies (destructor calls) for all active cleanup
            // scopes. These execute during stack unwinding when the throw's
            // landing pad is reached.
            bool savedIsEmittingEHCleanup = isEmittingEHCleanup;
            isEmittingEHCleanup = true;
            for (EHScopeStack::stable_iterator si =
                     ehStack.getInnermostEHScope();
                 si != ehStack.stable_end();
                 si = ehStack.find(si)->getEnclosingEHScope()) {
              EHScope &s = *ehStack.find(si);
              if (s.getKind() != EHScope::Cleanup)
                break;
              EHCleanupScope &cs = cast<EHCleanupScope>(s);
              if (!cs.isEHCleanup() || !cs.isActive())
                continue;
              EHScopeStack::Cleanup *cleanup = cs.getCleanup();
              EHScopeStack::Cleanup::Flags flags;
              flags.setIsForEHCleanup();
              flags.setIsEHCleanupKind();
              cleanup->emit(*this, flags);
            }
            isEmittingEHCleanup = savedIsEmittingEHCleanup;
            cir::ResumeOp::create(builder, loc);
          }
        });
    syntheticTryOp.setSynthetic(true);

    if (isTerminateScope) {
      // noexcept: catch-all handler that calls std::terminate.
      syntheticTryOp.setHandlerTypesAttr(
          mlir::ArrayAttr::get(&getMLIRContext(),
                               {cir::CatchAllAttr::get(&getMLIRContext())}));
      emitTerminateHandlerBody(syntheticTryOp);
    } else if (isFilterScope) {
      // Dynamic exception spec: catch-all handler that calls
      // __cxa_call_unexpected. Store filter types as a discardable attr
      // so CFG flattening can generate proper filter clauses.
      syntheticTryOp.setHandlerTypesAttr(
          mlir::ArrayAttr::get(&getMLIRContext(),
                               {cir::CatchAllAttr::get(&getMLIRContext())}));
      // Store filter types for later lowering to LLVM filter clauses.
      llvm::SmallVector<mlir::Attribute> filterTypes;
      for (unsigned i = 0; i < filterScope->getNumFilters(); ++i)
        filterTypes.push_back(filterScope->getFilter(i));
      syntheticTryOp->setAttr(
          "cir.filter_types",
          mlir::ArrayAttr::get(&getMLIRContext(), filterTypes));
      emitFilterHandlerBody(syntheticTryOp);
    } else {
      // Cleanup: cleanup-only, one unwind handler region.
      syntheticTryOp.setCleanup(true);
      syntheticTryOp.setHandlerTypesAttr(
          mlir::ArrayAttr::get(&getMLIRContext(),
                               {cir::UnwindAttr::get(&getMLIRContext())}));
    }

    savedIP = builder.saveInsertionPoint();
    builder.setInsertionPointToStart(&syntheticTryOp.getTryRegion().back());

    // Set the cleanup walk boundary so that per-call cleanup regions for
    // calls within the throw expression (e.g., constructors in
    // `throw Error(X())`) do NOT include the pre-existing RAII scopes
    // that are already handled by this synthetic TryOp's handler. Without
    // this, those scopes would be destroyed both in the per-call cleanup
    // and in the handler, causing double-destroy.
    ehCleanupWalkBoundary = ehStack.getInnermostEHScope();
  }

  if (const Expr *subExpr = e->getSubExpr()) {
    QualType throwType = subExpr->getType();
    if (throwType->isObjCObjectPointerType()) {
      const ObjCAtThrowStmt objCThrowStmt(e->getExprLoc(),
                                          const_cast<Expr *>(subExpr));
      mlir::LogicalResult emitResult = mlir::success();
      if (needsSyntheticTry) {
        LexicalScope tryScope{*this, getLoc(e->getSourceRange()),
                              builder.getInsertionBlock()};
        tryScope.setAsTry(syntheticTryOp);
        emitResult = emitObjCAtThrowStmt(objCThrowStmt);
      } else {
        emitResult = emitObjCAtThrowStmt(objCThrowStmt);
      }
      assert(emitResult.succeeded() &&
             "failed to emit Objective-C object-pointer throw");
      emittedObjCThrow = true;
      needsInlineConversion = false;
    } else {
      cgm.getCXXABI().emitThrow(*this, e);
    }
  } else {
    cgm.getCXXABI().emitRethrow(*this, /*isNoReturn=*/true);
  }

  if (needsSyntheticTry) {
    // Reset the cleanup walk boundary.
    ehCleanupWalkBoundary = EHScopeStack::stable_end();
    // Add yield terminator to the dead continuation block created by
    // insertThrowAndSplit inside the try region.
    if (builder.getInsertionBlock() &&
        !builder.getInsertionBlock()->mightHaveTerminator())
      cir::YieldOp::create(builder, syntheticTryOp.getLoc());
    builder.restoreInsertionPoint(savedIP);
  }

  if (emittedObjCThrow)
    return;

  // Case 2: throw inside an enclosing try with RAII cleanup.
  // Convert the cir.throw to cir.call exception @__cxa_throw so it
  // participates in the enclosing TryOp's landing pad construction.
  if (needsInlineConversion && e->getSubExpr()) {
    // The throw is in the block before the dead continuation block
    // created by insertThrowAndSplit (or emitThrow's cir.call exception path).
    mlir::Block *deadBlock = builder.getInsertionBlock();
    mlir::Block *throwBlock = deadBlock->getPrevNode();
    assert(throwBlock && "expected block before dead continuation block");
    cir::ThrowOp throwOp = nullptr;
    for (auto &op : *throwBlock) {
      if (auto t = dyn_cast<cir::ThrowOp>(&op))
        throwOp = t;
    }
    // If emitThrow already converted the throw to cir.call exception
    // @__cxa_throw (because expression evaluation pushed new EH scopes),
    // there's no ThrowOp to convert. Skip the inline conversion.
    if (!throwOp) {
      // emitThrow already handled the conversion. The per-call cleanup
      // was populated with expression temp cleanup. We still need to
      // ensure the cleanup flag is set on the enclosing TryOp.
      EHScope &innermostScope =
          *ehStack.find(ehStack.getInnermostEHScope());
      if (innermostScope.getKind() == EHScope::Cleanup &&
          cast<EHCleanupScope>(innermostScope).isEHCleanup())
        enclosingTryOp.setCleanup(true);
      builder.setInsertionPointToEnd(deadBlock);
    } else {

    mlir::Location loc = throwOp.getLoc();
    auto *ctx = &getMLIRContext();
    auto voidTy = cir::VoidType::get(ctx);
    auto excPtrTy = mlir::cast<cir::PointerType>(throwOp.getExceptionPtr().getType());
    auto ptrToVoid = builder.getVoidPtrTy(excPtrTy.getAddrSpace());

    // Get or create @__cxa_throw declaration.
    // Use cgm.getModule() instead of getParentOfType<ModuleOp>() because the
    // throw may be inside an IfOp body builder whose parent Op isn't yet
    // inserted into the IR, breaking the parent chain walk.
    auto modOp = cgm.getModule();
    (void)modOp;

    builder.setInsertionPoint(throwOp);

    // Cast exception_ptr to void*.
    mlir::Value excPtr = cir::CastOp::create(builder, loc, ptrToVoid,
                                              cir::CastKind::bitcast,
                                              throwOp.getExceptionPtr());

    // Get type_info address and cast to void*.
    auto typeInfoGlobal = mlir::cast<cir::GlobalOp>(
        cgm.getGlobalValue(throwOp.getTypeInfoAttr().getValue()));
    auto typeInfoGlobalTy = typeInfoGlobal.getSymType();
    auto typeInfoVoidTy =
        builder.getVoidPtrTy(typeInfoGlobal.getAddrSpaceAttr());
    auto ptrToTypeInfo = builder.getPointerTo(typeInfoGlobalTy,
                                              typeInfoGlobal.getAddrSpaceAttr());
    auto throwFnTy =
        cir::FuncType::get({ptrToVoid, typeInfoVoidTy, ptrToVoid}, voidTy);
    cgm.createRuntimeFunction(throwFnTy, "__cxa_throw");
    mlir::Value typeInfoAddr =
        cir::GetGlobalOp::create(builder, loc, ptrToTypeInfo,
                                 throwOp.getTypeInfoAttr())
            .getAddr();
    mlir::Value typeInfo = cir::CastOp::create(
        builder, loc, typeInfoVoidTy, cir::CastKind::bitcast, typeInfoAddr);

    // Dtor arg — use actual dtor symbol if present, null otherwise.
    mlir::Value dtor;
    if (auto dtorAttr = throwOp.getDtorAttr()) {
      auto dtorFunc = mlir::cast<cir::FuncOp>(
          cgm.getGlobalValue(dtorAttr.getValue()));
      auto dtorFnTy = dtorFunc.getFunctionType();
      auto ptrToDtor = builder.getPointerTo(dtorFnTy, excPtrTy.getAddrSpace());
      mlir::Value dtorAddr =
          cir::GetGlobalOp::create(builder, loc, ptrToDtor,
                                   dtorAttr.getValue())
              .getAddr();
      dtor = cir::CastOp::create(builder, loc, ptrToVoid,
                                  cir::CastKind::bitcast, dtorAddr);
    } else {
      dtor = builder.getNullPtr(ptrToVoid, loc);
    }

    // Create cir.call exception @__cxa_throw(...) — empty cleanup region
    // will be populated by populateCatchHandlersIfRequired below.
    auto callOp = cir::CallOp::create(
        builder, loc, mlir::SymbolRefAttr::get(ctx, "__cxa_throw"),
        /*resType=*/mlir::Type{},
        mlir::ValueRange{excPtr, typeInfo, dtor}, cir::CallingConv::C,
        cir::SideEffect::All, builder.getUnitAttr());
    if (auto attr = throwOp->getAttr("cir.throw_dtor_ptrauth"))
      callOp->setAttr("cir.throw_dtor_ptrauth", attr);

    // Add unreachable after the call.
    cir::UnreachableOp::create(builder, loc);

    // Erase the original throw + unreachable.
    if (auto *nextOp = throwOp->getNextNode())
      if (mlir::isa<cir::UnreachableOp>(nextOp))
        nextOp->erase();
    throwOp->erase();

    // Populate the call's per-call cleanup with destructor bodies and set
    // handler_types on the enclosing TryOp, just like emitCallLikeOp does.
    callWithExceptionCtx = callOp;
    populateCatchHandlersIfRequired(enclosingTryOp);
    callWithExceptionCtx = nullptr;

    // Ensure the cleanup flag is set on the enclosing TryOp. This is needed
    // because populateCatchHandlers only sets cleanup=true when it first
    // initializes handler_types. If handler_types were already set by a
    // prior call (e.g., a constructor call before the RAII object entered
    // scope), cleanup scopes added later won't update the flag. Without
    // the cleanup flag, landing pads won't catch non-matching exceptions,
    // so destructors wouldn't run during unwinding for those exceptions.
    EHScope &innermostScope = *ehStack.find(ehStack.getInnermostEHScope());
    if (innermostScope.getKind() == EHScope::Cleanup &&
        cast<EHCleanupScope>(innermostScope).isEHCleanup())
      enclosingTryOp.setCleanup(true);

    // Restore insertion point to the dead continuation block.
    builder.setInsertionPointToEnd(deadBlock);
    } // end else (throwOp found)
  }

  // Case 2b: Rethrow inside an enclosing try with RAII cleanup.
  // Convert cir.throw (rethrow) to cir.call exception @__cxa_rethrow()
  // with cleanup region populated from the EH scope stack, so RAII objects
  // (e.g., guard variables declared in catch handlers) are destroyed when
  // the rethrow propagates. Without this, throw; skips the inline
  // conversion and the cleanup region is never populated.
  if (needsInlineConversion && !e->getSubExpr()) {
    mlir::Block *deadBlock = builder.getInsertionBlock();
    mlir::Block *throwBlock = deadBlock->getPrevNode();
    assert(throwBlock && "expected block before dead continuation block");
    cir::ThrowOp throwOp = nullptr;
    for (auto &op : *throwBlock) {
      if (auto t = dyn_cast<cir::ThrowOp>(&op))
        throwOp = t;
    }
    assert(throwOp && "expected rethrow ThrowOp in block before dead block");

    mlir::Location loc = throwOp.getLoc();
    auto *ctx = &getMLIRContext();
    auto voidTy = cir::VoidType::get(ctx);
    auto rethrowFnTy = cir::FuncType::get({}, voidTy);

    // Get or create @__cxa_rethrow declaration.
    auto modOp = cgm.getModule();
    if (!cgm.getGlobalValue("__cxa_rethrow")) {
      mlir::OpBuilder::InsertionGuard modGuard(builder);
      builder.setInsertionPointToStart(modOp.getBody());
      auto fn =
          cir::FuncOp::create(builder, loc, "__cxa_rethrow", rethrowFnTy);
      cgm.insertGlobalSymbol(fn);
      fn.setPrivate();
    }

    builder.setInsertionPoint(throwOp);

    // Create cir.call exception @__cxa_rethrow() — empty cleanup region
    // will be populated by populateCatchHandlersIfRequired below.
    auto callOp = cir::CallOp::create(
        builder, loc, mlir::SymbolRefAttr::get(ctx, "__cxa_rethrow"),
        /*resType=*/mlir::Type{},
        mlir::ValueRange{}, cir::CallingConv::C,
        cir::SideEffect::All, builder.getUnitAttr());

    // Add unreachable after the call.
    cir::UnreachableOp::create(builder, loc);

    // Erase the original throw + unreachable.
    if (auto *nextOp = throwOp->getNextNode())
      if (mlir::isa<cir::UnreachableOp>(nextOp))
        nextOp->erase();
    throwOp->erase();

    // Populate the call's per-call cleanup with destructor bodies and set
    // handler_types on the enclosing TryOp, just like emitCallLikeOp does.
    callWithExceptionCtx = callOp;
    populateCatchHandlersIfRequired(enclosingTryOp);
    callWithExceptionCtx = nullptr;

    // Ensure the cleanup flag is set on the enclosing TryOp.
    EHScope &innermostScope = *ehStack.find(ehStack.getInnermostEHScope());
    if (innermostScope.getKind() == EHScope::Cleanup &&
        cast<EHCleanupScope>(innermostScope).isEHCleanup())
      enclosingTryOp.setCleanup(true);

    // Restore insertion point to the dead continuation block.
    builder.setInsertionPointToEnd(deadBlock);
  }

  // For throws without inline conversion, populate catch handlers on the
  // enclosing TryOp if present (sets handler_types and mayThrow). Skip this when
  // the throw escapes an outlined OpenMP region: attaching the throw to the
  // enclosing (out-of-region) TryOp is exactly the cross-region unwind edge that
  // crashes lowering, and the case has already been diagnosed as NYI above.
  if (!needsInlineConversion && !escapesOpenMPRegion &&
      isCatchOrCleanupRequired()) {
    if (cir::TryOp tryOp = curLexScope->getClosestTryParent())
      populateCatchHandlersIfRequired(tryOp);
  }
}

void CIRGenFunction::emitAnyExprToExn(const Expr *e, Address addr) {
  // Make sure the exception object is cleaned up if there's an exception
  // during initialization. This must be done for all throw operand types:
  // while scalar stores cannot throw, evaluating the throw expression itself
  // can (for example, `throw may_throw();`) after `__cxa_allocate_exception`
  // has already allocated memory.
  //
  // Match OGCG by always pushing FreeException here.
  bool needsFreeExnCleanup = true;

  // Save the stable_iterator of the FreeException cleanup so we can find
  // it after emitAnyExprToMem, which may push additional cleanups (e.g.,
  // ~X() temp destructors) on top of FreeException.
  EHScopeStack::stable_iterator freeExnScopeIter;
  if (needsFreeExnCleanup) {
    pushFullExprCleanup<FreeException>(EHCleanup, addr.getPointer());
    freeExnScopeIter = ehStack.stable_begin();
  }

  // __cxa_allocate_exception returns a void*;  we need to cast this
  // to the appropriate type for the object.
  mlir::Type ty = convertTypeForMem(e->getType());
  Address typedAddr = addr.withElementType(builder, ty);

  // From LLVM's codegen:
  // FIXME: this isn't quite right!  If there's a final unelided call
  // to a copy constructor, then according to [except.terminate]p1 we
  // must call std::terminate() if that constructor throws, because
  // technically that copy occurs after the exception expression is
  // evaluated but before the exception is caught.  But the best way
  // to handle that is to teach EmitAggExpr to do the final copy
  // differently if it can't be elided.
  emitAnyExprToMem(e, typedAddr, e->getType().getQualifiers(),
                   /*isInitializer=*/true);

  // Initialization succeeded — the exception object is now owned by
  // __cxa_throw. Deactivate the FreeException cleanup.
  //
  // If emitAnyExprToMem pushed additional cleanups on top of FreeException
  // (e.g., ~X() temp destructors from CXXBindTemporaryExpr in
  // `throw Error(X())`), the stack is [~X(), FreeException]. Blindly
  // popping the top would pop ~X() instead of FreeException, leaving
  // FreeException as the only scope — causing __cxa_throw's cleanup to
  // contain __cxa_free_exception (wrong) instead of ~X() (correct).
  //
  // When extra scopes exist on top: deactivate FreeException by its saved
  // stable_iterator without popping. The deactivated scope is silently
  // popped later by the enclosing LexicalScope (popCleanupBlock skips
  // inactive scopes, and populateEHCatchRegions skips them too).
  //
  // When FreeException is still at the top (no extra scopes): pop it
  // normally. This avoids leaving a deactivated scope on the stack, which
  // would cause requiresCatchOrCleanup() to return true spuriously (it
  // doesn't check isActive), leading to __cxa_throw being emitted as
  // invoke when a plain call is correct.
  if (needsFreeExnCleanup) {
    if (ehStack.stable_begin() == freeExnScopeIter) {
      // FreeException is still at the top — no extra scopes pushed.
      // Pop normally (original behavior). Its guarded range ends right here,
      // so a cir.cleanup.scope formed from that range runs it unconditionally
      // even though it is inactive from now on.
      EHCleanupScope &cleanupScope = cast<EHCleanupScope>(*ehStack.begin());
      cleanupScope.setActive(false);
      cleanupScope.setActiveOverCleanupScopeRange();
      popCleanupBlock();
    } else {
      // Extra scopes on top — deactivate without popping.
      // Go through deactivateCleanupBlock so that, if the range it guards
      // outlives this point, it gets an EH active flag: the shared unwind edge
      // is emitted at pop and would otherwise drop __cxa_free_exception.
      deactivateCleanupBlock(freeExnScopeIter, /*dominatingIP=*/nullptr);
    }
  }
}

void CIRGenFunction::populateUnwindResumeBlock(bool isCleanup,
                                               cir::TryOp tryOp) {
  const EHPersonality &personality = EHPersonality::get(*this);
  // This can always be a call because we necessarily didn't find
  // anything on the EH stack which needs our help.
  const char *rethrowName = personality.catchallRethrowFn;
  if (rethrowName != nullptr && !isCleanup) {
    cgm.errorNYI("populateUnwindResumeBlock CatchallRethrowFn");
    return;
  }

  unsigned regionsNum = tryOp->getNumRegions();
  mlir::Region *unwindRegion = &tryOp->getRegion(regionsNum - 1);
  mlir::Block *unwindResumeBlock = &unwindRegion->front();
  if (!unwindResumeBlock->empty())
    return;

  // Check if there's a Terminate scope enclosing this TryOp's catch scope.
  // If so, unhandled exceptions should call std::terminate() instead of
  // resuming. This handles try-catch inside noexcept functions.
  bool hasEnclosingTerminate = false;
  for (EHScopeStack::stable_iterator si = ehStack.getInnermostEHScope();
       si != ehStack.stable_end();
       si = ehStack.find(si)->getEnclosingEHScope()) {
    EHScope &s = *ehStack.find(si);
    if (s.getKind() == EHScope::Terminate) {
      hasEnclosingTerminate = true;
      break;
    }
  }

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(unwindResumeBlock);

  if (hasEnclosingTerminate) {
    // Replace unwind/resume with catch-all terminate handler.
    // Change the handler type from UnwindAttr to CatchAllAttr.
    mlir::ArrayAttr handlerTypes = tryOp.getHandlerTypesAttr();
    if (handlerTypes && !handlerTypes.empty()) {
      llvm::SmallVector<mlir::Attribute> newTypes(handlerTypes.begin(),
                                                   handlerTypes.end());
      // Replace the last entry (UnwindAttr) with CatchAllAttr.
      if (mlir::isa<cir::UnwindAttr>(newTypes.back()))
        newTypes.back() = cir::CatchAllAttr::get(&getMLIRContext());
      tryOp.setHandlerTypesAttr(
          mlir::ArrayAttr::get(&getMLIRContext(), newTypes));
    }
    // Populate the handler region with CatchParamOp + terminate call.
    emitTerminateHandlerBody(tryOp);
  } else {
    // Normal unwind: emit cir.resume.
    cir::ResumeOp::create(builder, tryOp.getLoc());
  }
}

mlir::LogicalResult CIRGenFunction::emitCXXTryStmt(const CXXTryStmt &s) {
  // CIR optimization: skip the try/catch entirely if the try body is empty,
  // since an empty body cannot throw. OGCG does not have this early return —
  // it always enters/exits the try scope regardless.
  if (s.getTryBlock()->body_empty())
    return mlir::LogicalResult::success();

  return emitCXXTryStmt(s, [&]() -> mlir::LogicalResult {
    return emitStmt(s.getTryBlock(), /*useCurrentScope=*/true);
  });
}

mlir::LogicalResult
CIRGenFunction::emitCXXTryStmt(const CXXTryStmt &s,
                               llvm::function_ref<mlir::LogicalResult()>
                                   emitTryBody) {
  // CIR optimization: skip the try/catch entirely if the try body is empty,
  // since an empty body cannot throw. OGCG does not have this early return —
  // it always enters/exits the try scope regardless.
  if (s.getTryBlock()->body_empty())
    return mlir::LogicalResult::success();

  mlir::Location loc = getLoc(s.getSourceRange());
  // Create a scope to hold try local storage for catch params.

  mlir::OpBuilder::InsertPoint scopeIP;
  cir::ScopeOp::create(
      builder, loc,
      /*scopeBuilder=*/[&](mlir::OpBuilder &b, mlir::Location loc) {
        scopeIP = builder.saveInsertionPoint();
      });

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.restoreInsertionPoint(scopeIP);
  mlir::LogicalResult result = emitCXXTryStmtUnderScope(s, emitTryBody);
  cir::YieldOp::create(builder, loc);
  return result;
}

mlir::LogicalResult
CIRGenFunction::emitCXXTryStmtUnderScope(const CXXTryStmt &s) {
  return emitCXXTryStmtUnderScope(s, [&]() -> mlir::LogicalResult {
    return emitStmt(s.getTryBlock(), /*useCurrentScope=*/true);
  });
}

mlir::LogicalResult CIRGenFunction::emitCXXTryStmtUnderScope(
    const CXXTryStmt &s,
    llvm::function_ref<mlir::LogicalResult()> emitTryBody) {
  const llvm::Triple &t = getTarget().getTriple();
  // If we encounter a try statement on in an OpenMP target region offloaded to
  // a GPU, we treat it as a basic block.
  const bool isTargetDevice =
      (cgm.getLangOpts().OpenMPIsTargetDevice && t.isGPU());
  if (isTargetDevice) {
    cgm.errorNYI(
        "emitCXXTryStmtUnderScope: OpenMP target region offloaded to GPU");
    return mlir::failure();
  }

  // A typed catch clause lowers to llvm.eh.typeid.for. The OpenMP host
  // outlining path (OpenMPIRBuilder + llvm::CodeExtractor) refuses to extract
  // any region containing that intrinsic (CodeExtractor's PR39545 guard), so a
  // typed catch physically emitted inside an outlined OpenMP region would abort
  // in OpenMPIRBuilder::finalize. Diagnose it cleanly instead of crashing.
  // catch-all (`catch(...)`) handlers emit no eh.typeid.for and stay supported.
  if (insideOutlinedOpenMPRegion) {
    for (unsigned i = 0, e = s.getNumHandlers(); i != e; ++i) {
      if (s.getHandler(i)->getExceptionDecl()) {
        cgm.errorNYI(s.getSourceRange(),
                     "typed catch clause inside an OpenMP region");
        return mlir::failure();
      }
    }
  }

  unsigned numHandlers = s.getNumHandlers();
  mlir::Location tryLoc = getLoc(s.getBeginLoc());
  mlir::OpBuilder::InsertPoint beginInsertTryBody;

  bool hasCatchAll = false;
  for (unsigned i = 0; i != numHandlers; ++i) {
    hasCatchAll |= s.getHandler(i)->getExceptionDecl() == nullptr;
    if (hasCatchAll)
      break;
  }

  // Create the scope to represent only the C/C++ `try {}` part. However,
  // don't populate right away. Create regions for the catch handlers,
  // but don't emit the handler bodies yet. For now, only make sure the
  // scope returns the exception information.
  auto tryOp = cir::TryOp::create(
      builder, tryLoc,
      /*tryBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location loc) {
        beginInsertTryBody = builder.saveInsertionPoint();
      },
      /*handlersBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location loc,
          mlir::OperationState &result) {
        mlir::OpBuilder::InsertionGuard guard(b);

        // We create an extra region for an unwind catch handler in case the
        // catch-all handler doesn't exists
        unsigned numRegionsToCreate =
            hasCatchAll ? numHandlers : numHandlers + 1;

        for (unsigned i = 0; i != numRegionsToCreate; ++i) {
          mlir::Region *region = result.addRegion();
          builder.createBlock(region);
        }
      });

  // Finally emit the body for try/catch.
  {
    mlir::Location loc = tryOp.getLoc();
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.restoreInsertionPoint(beginInsertTryBody);
    CIRGenFunction::LexicalScope tryScope{*this, loc,
                                          builder.getInsertionBlock()};

    tryScope.setAsTry(tryOp);

    // Attach the basic blocks for the catch regions.
    enterCXXTryStmt(s, tryOp);

    // Emit the body for the `try {}` part.
    bool tryBodyFailed = false;
    {
      mlir::OpBuilder::InsertionGuard guard(builder);
      CIRGenFunction::LexicalScope tryBodyScope{*this, loc,
                                                builder.getInsertionBlock()};
      // Set the source range so that forward gotos within the try body
      // can determine that the label is inside this scope. Without this,
      // emitGotoStmt's forward goto logic sees an invalid source range and
      // assumes the goto exits this scope, incorrectly emitting RAII
      // cleanups for objects that should remain alive.
      tryBodyScope.setClangSourceRange(s.getTryBlock()->getSourceRange());
      tryBodyFailed = emitTryBody().failed();
    }

    // If try-body emission failed -- e.g. a nested construct hit an NYI such
    // as a typed catch clause inside an outlined OpenMP region, whose failure
    // propagates out here -- bail without emitting the catch clauses.
    // enterCXXTryStmt already pushed an EHCatchScope; exitCXXTryStmt (which
    // pops it) is skipped on this path, so pop it explicitly. Otherwise this
    // try's enclosing LexicalScope cleanup runs popCleanupBlock over the
    // still-live EHCatchScope and asserts "top not a cleanup!".
    if (tryBodyFailed) {
      ehStack.popCatch();
      return mlir::failure();
    }

    // Emit catch clauses.
    exitCXXTryStmt(s);
  }

  incrementProfileCounter(&s);

  return mlir::success();
}

// Legacy attribute name retained for compatibility with the existing
// post-lowering WinMSVC ObjC catch-all placeholder rewrite. It now covers
// both raw-handler and wrapped objc_begin_catch/objc_end_catch placeholders.
static constexpr llvm::StringLiteral kWinMSVCRawObjCCatchAllAttr =
    "cir.win_msvc_raw_objc_catchall";

static bool isWinMSVCRawObjCCatchAllPlaceholderTry(cir::TryOp tryOp) {
  return tryOp && tryOp->hasAttr(kWinMSVCRawObjCCatchAllAttr);
}

static bool shouldUseWinMSVCRawObjCCatchAllPlaceholder(
    CIRGenFunction &cgf, const ObjCAtTryStmt &s,
    const ObjCAtFinallyStmt *normalFinallyStmt, unsigned activeHandlers) {
  if (!cgf.getTarget().getTriple().isWindowsMSVCEnvironment() ||
      normalFinallyStmt || activeHandlers != 1)
    return false;

  bool allowBareRethrow = false;
  switch (cgf.getLangOpts().ObjCRuntime.getKind()) {
  case ObjCRuntime::GNUstep:
  case ObjCRuntime::GCC:
  case ObjCRuntime::ObjFW:
    allowBareRethrow = true;
    break;
  case ObjCRuntime::FragileMacOSX:
  case ObjCRuntime::MacOSX:
  case ObjCRuntime::iOS:
  case ObjCRuntime::WatchOS:
    break;
  }

  const ObjCAtCatchStmt *catchStmt = s.getCatchStmt(0);
  if (catchStmt->getCatchParamDecl())
    return false;

  const auto *catchBody = dyn_cast<CompoundStmt>(catchStmt->getCatchBody());
  if (!catchBody)
    return false;

  if (catchBody->size() == 0)
    return true;

  if (!allowBareRethrow || catchBody->size() != 1)
    return false;

  const auto *rethrow =
      dyn_cast<ObjCAtThrowStmt>(*catchBody->body_begin());
  return rethrow && !rethrow->getThrowExpr();
}

/// Emit the structure of the dispatch block for the given catch scope.
/// It is an invariant that the dispatch block already exists.

static void emitCatchDispatchBlock(CIRGenFunction &cgf,
                                   EHCatchScope &catchScope, cir::TryOp tryOp) {
  if (EHPersonality::get(cgf).isWasmPersonality()) {
    cgf.cgm.errorNYI("emitCatchDispatchBlock: WasmPersonality");
    return;
  }

  if (EHPersonality::get(cgf).usesFuncletPads() &&
      !isWinMSVCRawObjCCatchAllPlaceholderTry(tryOp)) {
    cgf.cgm.errorNYI("emitCatchDispatchBlock: usesFuncletPads");
    return;
  }

  assert(std::find_if(catchScope.begin(), catchScope.end(),
                      [](const auto &handler) {
                        return !handler.type.rtti && handler.type.flags != 0;
                      }) == catchScope.end() &&
         "catch handler without type value or with not supported flags");

  // Propagate mayThrow to the enclosing scope. When the last handler is
  // catch-all, we still need to mark the enclosing scope because the catch
  // handler body may contain throws/rethrows that propagate upward.
  // For non-catch-all, populateEHCatchRegions does full setup including
  // unwind region handling.
  if (!std::prev(catchScope.end())->isCatchAll()) {
    cgf.populateEHCatchRegions(catchScope.getEnclosingEHScope(), tryOp);

    // When nested inside another try, populateEHCatchRegions may not emit
    // anything into this region (callWithExceptionCtx is null), leaving an
    // empty block. FlattenCFG expects a cir.resume terminator in the unwind
    // region (see buildUnwindCase).
    mlir::Region &unwindRegion = tryOp.getHandlerRegions().back();
    if (!unwindRegion.empty()) {
      mlir::Block &unwindBlock = unwindRegion.back();
      if (!unwindBlock.mightHaveTerminator() ||
          !unwindBlock.getTerminator()) {
        mlir::OpBuilder::InsertionGuard guard(cgf.getBuilder());
        cgf.getBuilder().setInsertionPointToEnd(&unwindBlock);

        // Emit outer-scope EH cleanups into the unwind region before
        // cir.resume. When an exception propagates through a non-matching
        // catch handler, RAII objects in enclosing scopes between this
        // catch scope and the next outer catch scope must be destroyed.
        // In OGCG, the `resume` instruction triggers the personality
        // function to unwind through cleanup landing pads. In CIR, we
        // inline the outer-scope cleanup code here so that FlattenCFG's
        // ResumeFlatOp redirect includes these cleanups.
        bool savedIsEmittingEHCleanup = cgf.isEmittingEHCleanup;
        cgf.isEmittingEHCleanup = true;
        for (EHScopeStack::stable_iterator si =
                 catchScope.getEnclosingEHScope();
             si != cgf.ehStack.stable_end();
             si = cgf.ehStack.find(si)->getEnclosingEHScope()) {
          EHScope &s = *cgf.ehStack.find(si);
          if (s.getKind() != EHScope::Cleanup)
            break;
          EHCleanupScope &cs = cast<EHCleanupScope>(s);
          if (!cs.isEHCleanup() || !cs.isActive())
            continue;
          EHScopeStack::Cleanup *cleanup = cs.getCleanup();
          EHScopeStack::Cleanup::Flags flags;
          flags.setIsForEHCleanup();
          flags.setIsEHCleanupKind();
          cleanup->emit(cgf, flags);
        }
        cgf.isEmittingEHCleanup = savedIsEmittingEHCleanup;

        cir::ResumeOp::create(cgf.getBuilder(), tryOp.getLoc());
      }
    }
  } else {
    // Catch-all: no unwind region, but still propagate mayThrow to the
    // enclosing scope so that the outer try's exitCXXTryStmt doesn't
    // incorrectly drop its handler blocks.
    EHScopeStack::stable_iterator enclosingIt =
        catchScope.getEnclosingEHScope();
    if (enclosingIt != cgf.ehStack.stable_end())
      cgf.ehStack.find(enclosingIt)->setMayThrow(true);
  }
}

namespace {
constexpr llvm::StringLiteral kCatchBeginFnAttr = "cir.catch_begin_fn";
constexpr llvm::StringLiteral kCatchEndFnAttr = "cir.catch_end_fn";
constexpr llvm::StringLiteral kCatchRawHandlerAttr = "cir.raw_handler";
constexpr llvm::StringLiteral kObjCDeadCatchesAttr = "cir.objc_dead_catches";

bool usesDarwinNonFragileObjCRuntime(CIRGenFunction &cgf) {
  const ObjCRuntime runtime = cgf.getLangOpts().ObjCRuntime;
  return runtime.isNeXTFamily() && runtime.isNonFragile();
}

enum class ObjCCatchHandlerStyle { Wrapped, Raw };

struct ObjCCatchRuntimeInfo {
  ObjCCatchHandlerStyle style;
  llvm::StringRef beginFn;
  llvm::StringRef endFn;
};

mlir::TypedAttr getObjCDarwinCatchHandlerType(CIRGenFunction &cgf,
                                              mlir::Location loc,
                                              QualType catchType);

ObjCCatchRuntimeInfo getObjCCatchRuntimeInfo(CIRGenFunction &cgf) {
  const ObjCRuntime runtime = cgf.getLangOpts().ObjCRuntime;
  const llvm::Triple &triple = cgf.getTarget().getTriple();
  const bool usesSEHExceptions = triple.isWindowsMSVCEnvironment();
  const bool usesCxxExceptions =
      triple.isOSCygMing() && runtime.getKind() == ObjCRuntime::GNUstep &&
      runtime.getVersion() >= VersionTuple(2);
  switch (runtime.getKind()) {
  case ObjCRuntime::FragileMacOSX:
    llvm_unreachable("fragile Objective-C runtime uses separate EH lowering");
  case ObjCRuntime::MacOSX:
  case ObjCRuntime::iOS:
  case ObjCRuntime::WatchOS:
    return {ObjCCatchHandlerStyle::Wrapped, "objc_begin_catch",
            "objc_end_catch"};
  case ObjCRuntime::GNUstep:
    if (usesCxxExceptions)
      return {ObjCCatchHandlerStyle::Wrapped, "__cxa_begin_catch",
              "__cxa_end_catch"};
    if (usesSEHExceptions)
      return {ObjCCatchHandlerStyle::Raw, "", ""};
    if (cgf.getLangOpts().CPlusPlus)
      return {ObjCCatchHandlerStyle::Wrapped, "__cxa_begin_catch",
              "__cxa_end_catch"};
    if (runtime.getVersion() >= VersionTuple(1, 7))
      return {ObjCCatchHandlerStyle::Wrapped, "objc_begin_catch",
              "objc_end_catch"};
    return {ObjCCatchHandlerStyle::Raw, "", ""};
  case ObjCRuntime::GCC:
  case ObjCRuntime::ObjFW:
    return {ObjCCatchHandlerStyle::Raw, "", ""};
  }

  llvm_unreachable("bad Objective-C runtime kind");
}

static std::string getObjCGNURawEHStringName(llvm::StringRef str) {
  static constexpr char hexDigits[] = "0123456789ABCDEF";

  std::string name = "__objc_eh_str_";
  for (unsigned char c : str.bytes()) {
    const bool isAlnum =
        (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z');
    if (isAlnum) {
      name.push_back(static_cast<char>(c));
      continue;
    }

    name += "_x";
    name.push_back(hexDigits[c >> 4]);
    name.push_back(hexDigits[c & 0xF]);
    name.push_back('_');
  }
  return name;
}

static cir::GlobalOp getObjCGNURawEHStringGlobal(CIRGenModule &cgm,
                                                 mlir::Location loc,
                                                 llvm::StringRef str) {
  std::string symbolName = getObjCGNURawEHStringName(str);
  if (auto gv = mlir::dyn_cast_or_null<cir::GlobalOp>(
          cgm.getGlobalValue(symbolName)))
    return gv;

  auto &builder = cgm.getBuilder();
  std::string bytes = str.str();
  bytes.push_back('\0');
  auto init =
      mlir::cast<mlir::TypedAttr>(builder.getString(bytes, builder.getUInt8Ty(),
                                                    bytes.size()));

  cir::GlobalOp gv = CIRGenModule::createGlobalOp(
      cgm, loc, symbolName, init.getType(), /*isConstant=*/true);
  gv.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
  mlir::SymbolTable::setSymbolVisibility(
      gv, mlir::SymbolTable::Visibility::Private);
  gv.setAlignmentAttr(cgm.getSize(CharUnits::One()));
  gv->setAttr("unnamed_addr", mlir::UnitAttr::get(&cgm.getMLIRContext()));
  CIRGenModule::setInitializer(gv, init);
  cgm.setDSOLocal(static_cast<mlir::Operation *>(gv));
  return gv;
}

static mlir::TypedAttr getObjCGNURawCatchHandlerType(CIRGenFunction &cgf,
                                                     mlir::Location loc,
                                                     QualType catchType) {
  auto voidPtrTy = cgf.getBuilder().getVoidPtrTy();

  if (catchType->isObjCIdType() || catchType->isObjCQualifiedIdType()) {
    cir::GlobalOp stringGV = getObjCGNURawEHStringGlobal(cgf.cgm, loc, "@id");
    return cgf.getBuilder().getGlobalViewAttr(voidPtrTy, stringGV);
  }

  const auto *objPtrTy = catchType->getAs<ObjCObjectPointerType>();
  assert(objPtrTy && "expected Objective-C object pointer catch type");

  const ObjCInterfaceType *ifaceTy = objPtrTy->getInterfaceType();
  assert(ifaceTy && "expected Objective-C interface pointer catch type");
  const IdentifierInfo *identifier = ifaceTy->getDecl()->getIdentifier();
  assert(identifier && "expected named Objective-C interface catch type");

  cir::GlobalOp stringGV =
      getObjCGNURawEHStringGlobal(cgf.cgm, loc, identifier->getName());
  return cgf.getBuilder().getGlobalViewAttr(voidPtrTy, stringGV);
}

static cir::GlobalOp getObjCGNUstepIdEHTypeGlobal(CIRGenModule &cgm,
                                                  mlir::Location loc) {
  auto voidPtrTy = cgm.getBuilder().getVoidPtrTy();
  cir::GlobalOp gv = cgm.createOrReplaceCXXRuntimeVariable(
      loc, "__objc_id_type_info", voidPtrTy,
      cir::GlobalLinkageKind::ExternalLinkage, CharUnits::Zero());
  gv.setConstant(false);
  return gv;
}

static cir::GlobalOp getObjCGNUstepTypeNameGlobal(CIRGenModule &cgm,
                                                  mlir::Location loc,
                                                  const ObjCInterfaceDecl *id) {
  id = id->getCanonicalDecl();
  const IdentifierInfo *identifier = id->getIdentifier();
  assert(identifier && "expected named Objective-C interface");

  std::string className = identifier->getName().str();
  std::string symbolName = "__objc_eh_typename_" + className;
  if (auto gv = mlir::dyn_cast_or_null<cir::GlobalOp>(
          cgm.getGlobalValue(symbolName)))
    return gv;

  auto &builder = cgm.getBuilder();
  std::string bytes = className;
  bytes.push_back('\0');
  auto init =
      mlir::cast<mlir::TypedAttr>(builder.getString(bytes, builder.getUInt8Ty(),
                                                    bytes.size()));

  cir::GlobalOp gv = cgm.createOrReplaceCXXRuntimeVariable(
      loc, symbolName, init.getType(), cir::GlobalLinkageKind::LinkOnceODRLinkage,
      CharUnits::One());
  CIRGenModule::setInitializer(gv, init);
  return gv;
}

static cir::GlobalOp
getObjCGNUstepClassTypeInfoVTableGlobal(CIRGenModule &cgm, mlir::Location loc) {
  auto voidPtrTy = cgm.getBuilder().getVoidPtrTy();
  cir::GlobalOp gv = cgm.createOrReplaceCXXRuntimeVariable(
      loc, "_ZTVN7gnustep7libobjc22__objc_class_type_infoE", voidPtrTy,
      cir::GlobalLinkageKind::ExternalLinkage, CharUnits::Zero());
  gv.setConstant(true);
  return gv;
}

static cir::GlobalOp getObjCGNUstepEHTypeGlobal(CIRGenModule &cgm,
                                                mlir::Location loc,
                                                const ObjCInterfaceDecl *id) {
  id = id->getCanonicalDecl();
  const IdentifierInfo *identifier = id->getIdentifier();
  assert(identifier && "expected named Objective-C interface");

  std::string className = identifier->getName().str();
  std::string symbolName = "__objc_eh_typeinfo_" + className;
  auto &builder = cgm.getBuilder();
  auto voidPtrTy = builder.getVoidPtrTy();
  auto typeInfoTy =
      builder.getAnonRecordTy({voidPtrTy, voidPtrTy}, /*packed=*/false,
                              /*padded=*/false);

  cir::GlobalOp gv = cgm.createOrReplaceCXXRuntimeVariable(
      loc, symbolName, typeInfoTy, cir::GlobalLinkageKind::LinkOnceODRLinkage,
      cgm.getPointerAlign());
  gv.setConstant(false);
  if (!gv.isDeclaration())
    return gv;

  cir::GlobalOp vtableGV = getObjCGNUstepClassTypeInfoVTableGlobal(cgm, loc);
  auto vtableSym = mlir::FlatSymbolRefAttr::get(vtableGV.getSymNameAttr());
  auto vtableIndices = builder.getArrayAttr({builder.getI32IntegerAttr(2)});
  mlir::Attribute vtableAttr =
      cir::GlobalViewAttr::get(voidPtrTy, vtableSym, vtableIndices,
                               /*inbounds=*/true);
  mlir::Attribute typeNameAttr = builder.getGlobalViewAttr(
      voidPtrTy, getObjCGNUstepTypeNameGlobal(cgm, loc, id));

  auto init = builder.getAnonConstRecord(
      builder.getArrayAttr({vtableAttr, typeNameAttr}),
      /*packed=*/false, /*padded=*/false, typeInfoTy);
  CIRGenModule::setInitializer(gv, init);
  return gv;
}

static mlir::TypedAttr getObjCGNUstepCatchHandlerType(CIRGenFunction &cgf,
                                                      mlir::Location loc,
                                                      QualType catchType) {
  auto voidPtrTy = cgf.getBuilder().getVoidPtrTy();

  if (catchType->isObjCIdType() || catchType->isObjCQualifiedIdType()) {
    return cgf.getBuilder().getGlobalViewAttr(
        voidPtrTy, getObjCGNUstepIdEHTypeGlobal(cgf.cgm, loc));
  }

  const auto *objPtrTy = catchType->getAs<ObjCObjectPointerType>();
  assert(objPtrTy && "expected Objective-C object pointer catch type");

  const ObjCInterfaceType *ifaceTy = objPtrTy->getInterfaceType();
  assert(ifaceTy && "expected Objective-C interface pointer catch type");

  return cgf.getBuilder().getGlobalViewAttr(
      voidPtrTy, getObjCGNUstepEHTypeGlobal(cgf.cgm, loc, ifaceTy->getDecl()));
}

static mlir::TypedAttr getObjCCatchHandlerType(CIRGenFunction &cgf,
                                               mlir::Location loc,
                                               QualType catchType) {
  switch (cgf.getLangOpts().ObjCRuntime.getKind()) {
  case ObjCRuntime::MacOSX:
  case ObjCRuntime::iOS:
  case ObjCRuntime::WatchOS:
    return getObjCDarwinCatchHandlerType(cgf, loc, catchType);
  case ObjCRuntime::GNUstep:
    if (cgf.cgm.getTriple().isWindowsMSVCEnvironment())
      return mlir::cast<mlir::TypedAttr>(
          cgf.cgm.getAddrOfRTTIDescriptor(loc, catchType, /*forEh=*/true));
    if (cgf.getLangOpts().CPlusPlus)
      return getObjCGNUstepCatchHandlerType(cgf, loc, catchType);
    return getObjCGNURawCatchHandlerType(cgf, loc, catchType);
  case ObjCRuntime::ObjFW:
    return getObjCGNURawCatchHandlerType(cgf, loc, catchType);
  case ObjCRuntime::GCC:
    assert(!(catchType->isObjCIdType() || catchType->isObjCQualifiedIdType()) &&
           "GCC id catches are lowered as catch-all");
    return getObjCGNURawCatchHandlerType(cgf, loc, catchType);
  case ObjCRuntime::FragileMacOSX:
    llvm_unreachable("fragile Objective-C runtime uses separate EH lowering");
  }

  llvm_unreachable("bad Objective-C runtime kind");
}

static bool isObjCGCCIdCatchAll(CIRGenFunction &cgf, QualType catchType) {
  return cgf.getLangOpts().ObjCRuntime.getKind() == ObjCRuntime::GCC &&
         (catchType->isObjCIdType() || catchType->isObjCQualifiedIdType());
}

static bool isObjCCatchAllHandler(CIRGenFunction &cgf,
                                  const VarDecl *catchDecl) {
  if (!catchDecl)
    return true;
  return isObjCGCCIdCatchAll(cgf, catchDecl->getType());
}

static bool hasUnwindExceptions(const LangOptions &langOpts) {
  if (!langOpts.Exceptions)
    return false;

  if (langOpts.CXXExceptions)
    return true;

  if (langOpts.ObjCExceptions)
    return langOpts.ObjCRuntime.hasUnwindExceptions();

  return true;
}

bool hasLiveFallthroughInsertPoint(CIRGenFunction &cgf) {
  if (!cgf.haveInsertPoint())
    return false;

  mlir::Block *block = cgf.getBuilder().getInsertionBlock();
  if (!block || block->isEntryBlock() || !block->hasNoPredecessors())
    return true;

  mlir::Region *region = block->getParent();
  if (!region)
    return true;

  auto it = block->getIterator();
  if (it == region->begin())
    return true;

  mlir::Block *prevBlock = &*std::prev(it);
  if (prevBlock->empty())
    return true;

  return !mlir::isa<cir::BreakOp, cir::ContinueOp, cir::GotoOp,
                    cir::ReturnOp, cir::UnreachableOp>(prevBlock->back());
}

struct CallObjCEndCatch final : EHScopeStack::Cleanup {
  mlir::Location loc;
  bool endCatchMightThrow;
  mlir::StringAttr catchEndFnAttr;

  explicit CallObjCEndCatch(mlir::Location loc, bool endCatchMightThrow,
                            mlir::StringAttr catchEndFnAttr)
      : loc(loc), endCatchMightThrow(endCatchMightThrow),
        catchEndFnAttr(catchEndFnAttr) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    auto emitEndCatch = [&](bool isEHCleanup) {
      auto endOp = cir::CatchParamOp::create(
          cgf.getBuilder(), loc, mlir::Type{}, nullptr,
          cir::CatchParamKindAttr::get(cgf.getBuilder().getContext(),
                                       cir::CatchParamKind::End));
      if (!endCatchMightThrow)
        endOp->setAttr("cir.end_catch_nothrow",
                       mlir::UnitAttr::get(cgf.getBuilder().getContext()));
      if (isEHCleanup)
        endOp->setAttr("cir.end_catch_eh_cleanup",
                       mlir::UnitAttr::get(cgf.getBuilder().getContext()));
      if (catchEndFnAttr)
        endOp->setAttr(kCatchEndFnAttr, catchEndFnAttr);
    };

    // Match classic ObjC codegen: normal early exits from a catch handler
    // still run objc_end_catch before leaving the handler.
    if (flags.isForEarlyExit()) {
      emitEndCatch(/*isEHCleanup=*/false);
      return;
    }

    if (flags.isForEHCleanup()) {
      emitEndCatch(/*isEHCleanup=*/true);
      return;
    }

    if (!cgf.getBuilder().getBlock()->mightHaveTerminator())
      cir::YieldOp::create(cgf.getBuilder(), loc);
  }
};

mlir::TypedAttr getObjCDarwinCatchHandlerType(CIRGenFunction &cgf,
                                              mlir::Location loc,
                                              QualType catchType) {
  auto voidPtrTy = cgf.getBuilder().getVoidPtrTy();

  if (catchType->isObjCIdType() || catchType->isObjCQualifiedIdType())
    return cgf.getBuilder().getGlobalViewAttr(
        voidPtrTy, cgf.cgm.getObjCDarwinIdEHTypeGlobal(loc));

  const auto *objPtrTy = catchType->getAs<ObjCObjectPointerType>();
  assert(objPtrTy && "expected Objective-C object pointer catch type");

  const ObjCInterfaceType *ifaceTy = objPtrTy->getInterfaceType();
  assert(ifaceTy && "expected Objective-C interface catch type");

  return cgf.getBuilder().getGlobalViewAttr(
      voidPtrTy,
      cgf.cgm.getObjCDarwinEHTypeGlobal(loc, ifaceTy->getDecl(),
                                        /*forDefinition=*/false));
}

mlir::Value emitObjCBeginCatch(CIRGenFunction &cgf, mlir::Location loc,
                               mlir::Type paramTy, bool endCatchMightThrow) {
  const ObjCCatchRuntimeInfo catchInfo = getObjCCatchRuntimeInfo(cgf);
  auto catchParam = cir::CatchParamOp::create(
      cgf.getBuilder(), loc, paramTy,
      /*exception_ptr=*/mlir::Value{}, /*kind=*/cir::CatchParamKindAttr{});

  if (catchInfo.style == ObjCCatchHandlerStyle::Raw) {
    catchParam->setAttr(kCatchRawHandlerAttr,
                        mlir::UnitAttr::get(cgf.getBuilder().getContext()));
    return catchParam.getParam();
  }

  // Match OGCG's ObjC EH path by materializing the wrapped catch helpers as
  // runtime declarations during CIRGen instead of inventing bare LLVM decls
  // later in DirectToLLVM. This lets Windows-Itanium import/CC handling reuse
  // the normal runtime-function pipeline.
  auto voidPtrTy = cgf.getBuilder().getVoidPtrTy();
  cgf.cgm.createRuntimeFunction(
      cir::FuncType::get({voidPtrTy}, voidPtrTy), catchInfo.beginFn);
  cgf.cgm.createRuntimeFunction(
      cir::FuncType::get({}, cgf.getBuilder().getVoidTy()), catchInfo.endFn);

  if (!endCatchMightThrow)
    catchParam->setAttr("cir.end_catch_nothrow",
                        mlir::UnitAttr::get(cgf.getBuilder().getContext()));
  auto catchBeginFnAttr = cgf.getBuilder().getStringAttr(catchInfo.beginFn);
  auto catchEndFnAttr = cgf.getBuilder().getStringAttr(catchInfo.endFn);
  catchParam->setAttr(kCatchBeginFnAttr, catchBeginFnAttr);
  catchParam->setAttr(kCatchEndFnAttr, catchEndFnAttr);
  cgf.ehStack.pushCleanup<CallObjCEndCatch>(NormalAndEHCleanup, loc,
                                            endCatchMightThrow,
                                            catchEndFnAttr);
  return catchParam.getParam();
}

void emitInitOfObjCCatchParam(CIRGenFunction &cgf, const VarDecl &catchParam,
                              Address paramAddr, mlir::Value exn,
                              SourceLocation loc) {
  mlir::Location catchLoc = cgf.getLoc(loc);
  mlir::Type paramTy = paramAddr.getElementType();
  if (exn.getType() != paramTy)
    exn = cir::CastOp::create(cgf.getBuilder(), catchLoc, paramTy,
                              cir::CastKind::bitcast, exn);

  switch (catchParam.getType().getQualifiers().getObjCLifetime()) {
  case Qualifiers::OCL_Strong:
    exn = cgf.emitARCRetainNonBlock(exn, catchLoc);
    [[fallthrough]];
  case Qualifiers::OCL_None:
  case Qualifiers::OCL_ExplicitNone:
  case Qualifiers::OCL_Autoreleasing:
    cgf.getBuilder().createStore(catchLoc, exn, paramAddr);
    return;
  case Qualifiers::OCL_Weak:
    cgf.emitARCInitWeak(paramAddr, exn);
    return;
  }

  llvm_unreachable("invalid ownership qualifier");
}

// Collect every local VarDecl declared textually within an Objective-C
// @finally body's statement subtree. Because CIRGen re-emits the whole subtree
// on every edge leaving the @try, VarDecls at any nesting depth (nested
// compound statements, nested @try/@finally, loops, etc.) get re-registered on
// each copy and would collide in the function-wide localDeclMap. Blocks and
// lambdas are emitted in their own CIRGenFunction, so their bodies (whose
// decls never enter this function's localDeclMap) are not descended into.
// Both automatic and static locals are collected: a static local's storage is
// a single shared global, but each re-emitted copy still calls
// setAddrOfLocalVar and would trip "Decl already exists in LocalDeclMap!" on
// the second copy without the per-copy snapshot/clear/restore. Block-scope
// extern (external storage, refers to a global) never enters localDeclMap and
// is excluded.
void collectFinallyLocalVarDecls(
    const Stmt *stmt, llvm::SmallVectorImpl<const VarDecl *> &decls) {
  if (!stmt)
    return;

  if (const auto *ds = dyn_cast<DeclStmt>(stmt)) {
    for (const Decl *d : ds->decls())
      if (const auto *vd = dyn_cast<VarDecl>(d))
        if (vd->hasLocalStorage() || vd->isStaticLocal())
          decls.push_back(vd);
  }

  // Do not descend into nested blocks/lambdas: their bodies are emitted in a
  // separate CIRGenFunction and their locals never touch this localDeclMap.
  if (isa<BlockExpr, LambdaExpr>(stmt))
    return;

  for (const Stmt *child : stmt->children())
    collectFinallyLocalVarDecls(child, decls);
}
} // namespace

mlir::LogicalResult
CIRGenFunction::emitObjCFinallyBody(const Stmt *finallyBody,
                                    bool useCurrentScope) {
  llvm::SmallVector<const VarDecl *, 4> finallyLocals;
  collectFinallyLocalVarDecls(finallyBody, finallyLocals);

  // Snapshot and clear any pre-existing localDeclMap entries for the @finally's
  // locals so this re-emission allocates fresh storage, then restore the prior
  // entries (or remove this copy's entries) once the body has been emitted.
  llvm::SmallVector<std::pair<const VarDecl *, Address>, 4> saved;
  saved.reserve(finallyLocals.size());
  for (const VarDecl *vd : finallyLocals) {
    auto it = localDeclMap.find(vd);
    if (it != localDeclMap.end()) {
      saved.push_back({vd, it->second});
      localDeclMap.erase(it);
    } else {
      saved.push_back({vd, Address::invalid()});
    }
  }

  auto restore = llvm::make_scope_exit([&] {
    for (auto &entry : saved) {
      localDeclMap.erase(entry.first);
      if (entry.second.isValid())
        localDeclMap.insert({entry.first, entry.second});
    }
  });

  // symbolTable is a scoped hash table with no per-key erase, so isolate this
  // copy's decls in their own scope. Without this, the handler copy
  // (useCurrentScope=true, which reuses the current symbol-table scope) would
  // leave the @finally's locals bound and a later copy's declare() would trip
  // the "not supposed to be available just yet" assert. This scope pops all of
  // this copy's bindings when it exits, mirroring the fresh scope that
  // emitCompoundStmt already opens for useCurrentScope=false bodies.
  SymTableScopeTy finallySymScope(symbolTable);

  return emitStmt(finallyBody, useCurrentScope);
}

mlir::LogicalResult CIRGenFunction::emitObjCAtTryStmt(const ObjCAtTryStmt &s) {
  if (getLangOpts().ObjCRuntime.getKind() == ObjCRuntime::FragileMacOSX) {
    mlir::Location loc = getLoc(s.getSourceRange());
    auto objPtrTy = builder.getVoidPtrTy();
    auto intTy = builder.getSInt32Ty();
    auto voidTy = cir::VoidType::get(&getMLIRContext());
    const ObjCAtFinallyStmt *finallyStmt = s.getFinallyStmt();
    unsigned numHandlers = s.getNumCatchStmts();

    auto bitcastToVoidPtr = [&](mlir::Value value) {
      if (value.getType() != objPtrTy)
        value = builder.createBitcast(loc, value, objPtrTy);
      return value;
    };

    cir::FuncType exceptionVoidFnTy = cir::FuncType::get({objPtrTy}, voidTy);
    cir::FuncOp tryEnterFn =
        cgm.createRuntimeFunction(exceptionVoidFnTy, "objc_exception_try_enter");
    cir::FuncOp tryExitFn =
        cgm.createRuntimeFunction(exceptionVoidFnTy, "objc_exception_try_exit");
    cir::FuncOp extractFn =
        cgm.createRuntimeFunction(cir::FuncType::get({objPtrTy}, objPtrTy),
                                  "objc_exception_extract");
    cir::FuncOp throwFn =
        cgm.createRuntimeFunction(exceptionVoidFnTy, "objc_exception_throw");
    cir::FuncOp matchFn = cgm.createRuntimeFunction(
        cir::FuncType::get({objPtrTy, objPtrTy}, intTy),
        "objc_exception_match");
    cir::FuncOp setJmpFn = getObjCFragileSetJmpFn(*this);

    mlir::Type exceptionDataTy = getObjCFragileExceptionDataTy(*this);
    Address exceptionDataAddr = createTempAlloca(
        exceptionDataTy, getPointerAlign(), loc, "exceptiondata.ptr");
    mlir::Value exceptionData = bitcastToVoidPtr(exceptionDataAddr.getPointer());

    auto setJmpBufferTy = cir::ArrayType::get(builder.getSInt32Ty(), 18);
    mlir::Value setJmpBuffer = builder.createGetMember(
        loc, builder.getPointerTo(setJmpBufferTy), exceptionDataAddr.getPointer(),
        "", 0);
    setJmpBuffer = bitcastToVoidPtr(setJmpBuffer);

    auto emitTryEnter = [&]() {
      cir::CallOp tryEnterCall =
          builder.createCallOp(loc, tryEnterFn, {exceptionData});
      tryEnterCall.setNothrow(true);
    };

    auto emitTryExit = [&]() {
      cir::CallOp tryExitCall =
          builder.createCallOp(loc, tryExitFn, {exceptionData});
      tryExitCall.setNothrow(true);
    };

    auto emitSetJmp = [&]() -> mlir::Value {
      cir::CallOp setJmpCall =
          builder.createCallOp(loc, setJmpFn, {setJmpBuffer});
      setJmpCall.setNothrow(true);
      markReturnsTwice(setJmpCall, getMLIRContext());
      return setJmpCall.getResult();
    };

    auto emitFragileThrow = [&](mlir::Value exn) -> mlir::LogicalResult {
      exn = bitcastToVoidPtr(exn);
      cir::CallOp throwCall = builder.createCallOp(loc, throwFn, {exn});
      throwCall.setNothrow(true);
      markNoReturnCall(throwCall, getMLIRContext());
      cir::UnreachableOp::create(builder, loc);
      builder.clearInsertionPoint();
      return mlir::success();
    };

    auto emitFinallyDispatch =
        [&](bool callTryExit, mlir::Block *continueBlock,
            mlir::Value rethrowValue = mlir::Value()) -> mlir::LogicalResult {
      if (!hasLiveFallthroughInsertPoint(*this))
        return mlir::success();

      if (callTryExit)
        emitTryExit();

      if (finallyStmt &&
          emitObjCFinallyBody(finallyStmt->getFinallyBody(),
                              /*useCurrentScope=*/false)
              .failed())
        return mlir::failure();

      if (!hasLiveFallthroughInsertPoint(*this))
        return mlir::success();

      if (rethrowValue)
        return emitFragileThrow(rethrowValue);

      if (continueBlock)
        cir::BrOp::create(builder, loc, continueBlock);
      return mlir::success();
    };

    auto emitFragileCatchBody =
        [&](const ObjCAtCatchStmt *catchStmt,
            mlir::Value caught) -> mlir::LogicalResult {
      mlir::Location handlerLoc = getLoc(catchStmt->getSourceRange());
      {
        LexicalScope handlerScope{*this, handlerLoc, builder.getInsertionBlock()};
        handlerScope.setClangSourceRange(catchStmt->getCatchBody()->getSourceRange());
        handlerScope.setCleanupOnly();
        RunCleanupsScope catchVarScope(*this);

        const VarDecl *catchParam = catchStmt->getCatchParamDecl();
        if (catchParam) {
          AutoVarEmission var = emitAutoVarAlloca(*catchParam);
          emitInitOfObjCCatchParam(*this, *catchParam,
                                   var.getObjectAddress(*this), caught,
                                   catchStmt->getBeginLoc());
          emitAutoVarCleanups(var);
        }

        if (finallyStmt)
          pushActiveObjCFinallyScope(finallyStmt->getFinallyBody(),
                                     catchStmt->getCatchBody()->getSourceRange(),
                                     &handlerScope);
        if (emitStmt(catchStmt->getCatchBody(), /*useCurrentScope=*/false)
                .failed())
          return mlir::failure();
        if (finallyStmt)
          popActiveObjCFinallyScopeIfPresent(finallyStmt->getFinallyBody(),
                                             &handlerScope);

        catchVarScope.forceCleanup();
      }

      return mlir::success();
    };

    FragileObjCHazards hazards(*this);
    mlir::Region *region = builder.getBlock()->getParent();
    auto *tryBlock = new mlir::Block();
    auto *tryHandlerBlock = new mlir::Block();
    auto *continueBlock = new mlir::Block();
    region->push_back(tryBlock);
    region->push_back(tryHandlerBlock);
    region->push_back(continueBlock);

    emitTryEnter();
    mlir::Value didEnterTry = cir::CmpOp::create(
        builder, loc, cir::CmpOpKind::eq, emitSetJmp(),
        builder.getSInt32(0, loc));
    cir::BrCondOp::create(builder, loc, didEnterTry, tryBlock,
                          tryHandlerBlock);

    builder.setInsertionPointToStart(tryBlock);
    {
      LexicalScope tryBodyScope{*this, loc, builder.getInsertionBlock()};
      tryBodyScope.setClangSourceRange(s.getTryBody()->getSourceRange());
      tryBodyScope.setCleanupOnly();
      if (finallyStmt)
        pushActiveObjCFinallyScope(finallyStmt->getFinallyBody(),
                                   s.getTryBody()->getSourceRange(),
                                   &tryBodyScope);
      if (emitStmt(s.getTryBody(), /*useCurrentScope=*/false).failed())
        return mlir::failure();
      if (finallyStmt)
        popActiveObjCFinallyScopeIfPresent(finallyStmt->getFinallyBody(),
                                           &tryBodyScope);
    }
    if (emitFinallyDispatch(/*callTryExit=*/true, continueBlock).failed())
      return mlir::failure();

    builder.setInsertionPointToStart(tryHandlerBlock);
    hazards.emitWriteHazard(loc);

    cir::CallOp caughtCall = builder.createCallOp(loc, extractFn, {exceptionData});
    caughtCall.setNothrow(true);
    mlir::Value caught = caughtCall.getResult();

    if (!numHandlers) {
      if (emitFinallyDispatch(/*callTryExit=*/false, /*continueBlock=*/nullptr,
                              caught)
              .failed())
        return mlir::failure();
    } else {
      Address propagatingExnAddr = Address::invalid();
      if (finallyStmt) {
        propagatingExnAddr =
            createTempAlloca(objPtrTy, getPointerAlign(), loc,
                             "propagating_exception");
        builder.createStore(loc, bitcastToVoidPtr(caught), propagatingExnAddr);

        emitTryEnter();
        auto *catchBlock = new mlir::Block();
        auto *catchHandlerBlock = new mlir::Block();
        region->push_back(catchBlock);
        region->push_back(catchHandlerBlock);

        mlir::Value didEnterCatch = cir::CmpOp::create(
            builder, loc, cir::CmpOpKind::eq, emitSetJmp(),
            builder.getSInt32(0, loc));
        cir::BrCondOp::create(builder, loc, didEnterCatch, catchBlock,
                              catchHandlerBlock);
        builder.setInsertionPointToStart(catchBlock);

        objCEHValueStack.push_back(caught);
        auto popCaught = llvm::scope_exit([&] { objCEHValueStack.pop_back(); });

        bool allMatched = false;
        for (const ObjCAtCatchStmt *catchStmt : s.catch_stmts()) {
          const VarDecl *catchParam = catchStmt->getCatchParamDecl();
          QualType catchType = catchParam ? catchParam->getType() : QualType();
          mlir::Location handlerLoc = getLoc(catchStmt->getSourceRange());

          bool isCatchAll =
              !catchParam || catchType->isObjCIdType() ||
              catchType->isObjCQualifiedIdType();
          if (isCatchAll) {
            allMatched = true;
          } else if (!catchType->isObjCObjectPointerType()) {
            cgm.errorNYI(catchStmt->getSourceRange(),
                         "emitObjCAtTryStmt fragile runtime non-ObjC catch "
                         "type");
            return mlir::failure();
          }

          if (allMatched) {
            if (emitFragileCatchBody(catchStmt, caught).failed())
              return mlir::failure();
            if (emitFinallyDispatch(/*callTryExit=*/true, continueBlock).failed())
              return mlir::failure();
            break;
          }

          const ObjCObjectPointerType *opt =
              catchType->getAs<ObjCObjectPointerType>();
          const ObjCObjectType *objTy = opt->getObjectType();
          ObjCInterfaceDecl *interfaceDecl = objTy->getInterface();
          assert(interfaceDecl && "catch parameter must have Objective-C type");

          mlir::Value classRef =
              bitcastToVoidPtr(cgm.getObjCClass(*this, interfaceDecl, handlerLoc));
          mlir::Value exn = bitcastToVoidPtr(caught);
          cir::CallOp matchCall =
              builder.createCallOp(handlerLoc, matchFn, {classRef, exn});
          matchCall.setNothrow(true);

          auto *matchedBlock = new mlir::Block();
          auto *nextCatchBlock = new mlir::Block();
          region->push_back(matchedBlock);
          region->push_back(nextCatchBlock);

          mlir::Value matched = cir::CmpOp::create(
              builder, handlerLoc, cir::CmpOpKind::ne, matchCall.getResult(),
              builder.getSInt32(0, handlerLoc));
          cir::BrCondOp::create(builder, handlerLoc, matched, matchedBlock,
                                nextCatchBlock);

          builder.setInsertionPointToStart(matchedBlock);
          if (emitFragileCatchBody(catchStmt, caught).failed())
            return mlir::failure();
          if (emitFinallyDispatch(/*callTryExit=*/true, continueBlock).failed())
            return mlir::failure();

          builder.setInsertionPointToStart(nextCatchBlock);
        }

        if (!allMatched) {
          mlir::Value exn = builder.createLoad(loc, propagatingExnAddr);
          if (emitFinallyDispatch(/*callTryExit=*/true,
                                  /*continueBlock=*/nullptr, exn)
                  .failed())
            return mlir::failure();
        }

        builder.setInsertionPointToStart(catchHandlerBlock);
        cir::CallOp newCaughtCall =
            builder.createCallOp(loc, extractFn, {exceptionData});
        newCaughtCall.setNothrow(true);
        builder.createStore(loc, newCaughtCall.getResult(), propagatingExnAddr);
        if (emitFinallyDispatch(/*callTryExit=*/false,
                                /*continueBlock=*/nullptr,
                                builder.createLoad(loc, propagatingExnAddr))
                .failed())
          return mlir::failure();
      } else {
        objCEHValueStack.push_back(caught);
        auto popCaught = llvm::scope_exit([&] { objCEHValueStack.pop_back(); });

        bool allMatched = false;
        for (const ObjCAtCatchStmt *catchStmt : s.catch_stmts()) {
          const VarDecl *catchParam = catchStmt->getCatchParamDecl();
          QualType catchType = catchParam ? catchParam->getType() : QualType();
          mlir::Location handlerLoc = getLoc(catchStmt->getSourceRange());

          bool isCatchAll =
              !catchParam || catchType->isObjCIdType() ||
              catchType->isObjCQualifiedIdType();
          if (isCatchAll) {
            allMatched = true;
          } else if (!catchType->isObjCObjectPointerType()) {
            cgm.errorNYI(catchStmt->getSourceRange(),
                         "emitObjCAtTryStmt fragile runtime non-ObjC catch "
                         "type");
            return mlir::failure();
          }

          if (allMatched) {
            if (emitFragileCatchBody(catchStmt, caught).failed())
              return mlir::failure();
            if (hasLiveFallthroughInsertPoint(*this))
              cir::BrOp::create(builder, loc, continueBlock);
            break;
          }

          const ObjCObjectPointerType *opt =
              catchType->getAs<ObjCObjectPointerType>();
          const ObjCObjectType *objTy = opt->getObjectType();
          ObjCInterfaceDecl *interfaceDecl = objTy->getInterface();
          assert(interfaceDecl && "catch parameter must have Objective-C type");

          mlir::Value classRef =
              bitcastToVoidPtr(cgm.getObjCClass(*this, interfaceDecl, handlerLoc));
          mlir::Value exn = bitcastToVoidPtr(caught);
          cir::CallOp matchCall =
              builder.createCallOp(handlerLoc, matchFn, {classRef, exn});
          matchCall.setNothrow(true);

          auto *matchedBlock = new mlir::Block();
          auto *nextCatchBlock = new mlir::Block();
          region->push_back(matchedBlock);
          region->push_back(nextCatchBlock);

          mlir::Value matched = cir::CmpOp::create(
              builder, handlerLoc, cir::CmpOpKind::ne, matchCall.getResult(),
              builder.getSInt32(0, handlerLoc));
          cir::BrCondOp::create(builder, handlerLoc, matched, matchedBlock,
                                nextCatchBlock);

          builder.setInsertionPointToStart(matchedBlock);
          if (emitFragileCatchBody(catchStmt, caught).failed())
            return mlir::failure();
          if (hasLiveFallthroughInsertPoint(*this))
            cir::BrOp::create(builder, loc, continueBlock);

          builder.setInsertionPointToStart(nextCatchBlock);
        }

        if (!allMatched && emitFragileThrow(caught).failed())
          return mlir::failure();
      }
    }

    hazards.emitReadHazardsInNewBlocks();
    builder.setInsertionPointToStart(continueBlock);
    return mlir::success();
  }

  // A typed ObjC @catch (e.g. @catch(id) / @catch(NSException*) on the Darwin
  // runtime, where an `id` catch is NOT a catch-all) lowers to a handler that
  // emits llvm.eh.typeid.for. The OpenMP host outlining path (OpenMPIRBuilder +
  // llvm::CodeExtractor) refuses to extract a region containing that intrinsic,
  // so a typed catch emitted inside an outlined OpenMP region aborts in
  // OMPIRBuilder::finalize (the isEligible assert). This is the ObjC analogue of
  // the C++ typed-catch gate in emitCXXTryStmtUnderScope; @throw and @try bypass
  // the C++ emitCXXThrowExpr/emitCXXTryStmt gates. A catch-all handler emits no
  // eh.typeid.for and stays supported, and a @try with only @finally (no @catch)
  // is unaffected. Diagnose cleanly instead of crashing.
  if (insideOutlinedOpenMPRegion) {
    for (unsigned i = 0, e = s.getNumCatchStmts(); i != e; ++i) {
      const VarDecl *catchDecl = s.getCatchStmt(i)->getCatchParamDecl();
      if (!isObjCCatchAllHandler(*this, catchDecl)) {
        cgm.errorNYI(s.getSourceRange(),
                     "typed catch clause inside an OpenMP region");
        return mlir::failure();
      }
    }
  }

  if (s.getFinallyStmt() && !usesDarwinNonFragileObjCRuntime(*this)) {
    cgm.errorNYI(s.getSourceRange(),
                 "emitObjCAtTryStmt non-NeXT Objective-C runtime @finally");
    return mlir::failure();
  }

  unsigned numHandlers = s.getNumCatchStmts();
  mlir::Location loc = getLoc(s.getSourceRange());

  auto emitObjCTryCatchStmt =
      [&](const ObjCAtFinallyStmt *normalFinallyStmt,
          bool &hasNormalFallthrough) -> mlir::LogicalResult {
    if (!numHandlers) {
      cgm.errorNYI(s.getSourceRange(), "emitObjCAtTryStmt without @catch");
      return mlir::failure();
    }

    bool hasCatchAll = false;
    unsigned activeHandlers = 0;
    for (unsigned i = 0; i != numHandlers; ++i) {
      ++activeHandlers;
      hasCatchAll |=
          isObjCCatchAllHandler(*this, s.getCatchStmt(i)->getCatchParamDecl());
      if (hasCatchAll)
        break;
    }

    const bool emitDeadObjCCatchesWithoutUnwind =
        getLangOpts().ObjCExceptions && !hasUnwindExceptions(getLangOpts());

    struct ObjCCatchHandler {
      const ObjCAtCatchStmt *stmt;
      mlir::Region *region;
    };

    mlir::OpBuilder::InsertPoint scopeIP;
    cir::ScopeOp::create(
        builder, loc,
        /*scopeBuilder=*/[&](mlir::OpBuilder &, mlir::Location) {
          scopeIP = builder.saveInsertionPoint();
        });

    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.restoreInsertionPoint(scopeIP);

    mlir::Location tryLoc = getLoc(s.getBeginLoc());
    mlir::OpBuilder::InsertPoint beginInsertTryBody;
    SmallVector<ObjCCatchHandler, 4> handlers;

    auto tryOp = cir::TryOp::create(
        builder, tryLoc,
        /*tryBuilder=*/[&](mlir::OpBuilder &, mlir::Location) {
          beginInsertTryBody = builder.saveInsertionPoint();
        },
        /*handlersBuilder=*/
        [&](mlir::OpBuilder &, mlir::Location, mlir::OperationState &result) {
          unsigned numRegionsToCreate =
              (hasCatchAll || emitDeadObjCCatchesWithoutUnwind)
                  ? activeHandlers
                  : activeHandlers + 1;
          for (unsigned i = 0; i != numRegionsToCreate; ++i) {
            mlir::Region *region = result.addRegion();
            builder.createBlock(region);
          }
        });
    if (shouldUseWinMSVCRawObjCCatchAllPlaceholder(*this, s, normalFinallyStmt,
                                                   activeHandlers))
      tryOp->setAttr(kWinMSVCRawObjCCatchAllAttr,
                     mlir::UnitAttr::get(&getMLIRContext()));
    if (shouldUseWinMSVCRawObjCCatchAllPlaceholder(*this, s, normalFinallyStmt,
                                                   activeHandlers))
      if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(curFn))
        funcOp->setAttr(kWinMSVCRawObjCCatchAllAttr,
                        mlir::UnitAttr::get(&getMLIRContext()));

    bool tryBodyHasFallthrough = false;
    bool catchBodyHasFallthrough = false;

    {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.restoreInsertionPoint(beginInsertTryBody);
      CIRGenFunction::LexicalScope tryScope{*this, loc,
                                            builder.getInsertionBlock()};
      tryScope.setAsTry(tryOp);

      EHCatchScope *catchScope = ehStack.pushCatch(activeHandlers);
      for (unsigned i = 0; i != activeHandlers; ++i) {
        const ObjCAtCatchStmt *catchStmt = s.getCatchStmt(i);
        const VarDecl *catchDecl = catchStmt->getCatchParamDecl();
        if (isObjCCatchAllHandler(*this, catchDecl)) {
          mlir::Region *handler = &tryOp.getHandlerRegions()[i];
          catchScope->setHandler(i, cgm.getCXXABI().getCatchAllTypeInfo(),
                                 handler, /*stmt=*/nullptr);
          handlers.push_back({catchStmt, handler});
          continue;
        }

        QualType catchType = catchDecl->getType();
        if (!(catchType->isObjCIdType() || catchType->isObjCQualifiedIdType() ||
              catchType->isObjCObjectPointerType())) {
          cgm.errorNYI(catchStmt->getSourceRange(),
                       "emitObjCAtTryStmt non-ObjC catch type");
          ehStack.popCatch();
          return mlir::failure();
        }

        mlir::Region *handler = &tryOp.getHandlerRegions()[i];
        CatchTypeInfo typeInfo = {
            getObjCCatchHandlerType(*this, getLoc(catchStmt->getSourceRange()),
                                    catchType),
            0};
        catchScope->setHandler(i, typeInfo, handler, /*stmt=*/nullptr);
        handlers.push_back({catchStmt, handler});
      }

      {
        mlir::OpBuilder::InsertionGuard guard(builder);
        CIRGenFunction::LexicalScope tryBodyScope{*this, loc,
                                                  builder.getInsertionBlock()};
        tryBodyScope.setClangSourceRange(s.getTryBody()->getSourceRange());
        if (normalFinallyStmt)
          pushActiveObjCFinallyScope(normalFinallyStmt->getFinallyBody(),
                                     s.getTryBody()->getSourceRange(),
                                     &tryBodyScope);
        if (emitStmt(s.getTryBody(), /*useCurrentScope=*/true).failed())
          return mlir::failure();
        if (normalFinallyStmt) {
          popActiveObjCFinallyScopeIfPresent(normalFinallyStmt->getFinallyBody(),
                                             &tryBodyScope);
          tryBodyHasFallthrough = hasLiveFallthroughInsertPoint(*this);
        }
      }

      EHCatchScope &catchScopeRef = cast<EHCatchScope>(*ehStack.begin());
      bool keepDeadObjCCatches =
          !catchScopeRef.mayThrow() && emitDeadObjCCatchesWithoutUnwind;
      if (!catchScopeRef.mayThrow() && !keepDeadObjCCatches) {
        catchScopeRef.clearHandlerBlocks();
        ehStack.popCatch();

        SmallVector<mlir::Block *> eraseBlocks;
        for (mlir::Region &handlerRegion : tryOp.getHandlerRegions()) {
          if (handlerRegion.empty())
            continue;
          for (mlir::Block &b : handlerRegion.getBlocks())
            eraseBlocks.push_back(&b);
        }

        for (mlir::Block *b : eraseBlocks)
          b->erase();
        tryOp.setHandlerTypesAttr({});
      } else {
        llvm::SmallVector<mlir::Attribute> handlerAttrs;
        bool emittedCatchAll = false;
        for (unsigned i = 0; i < catchScopeRef.getNumHandlers(); ++i) {
          const EHCatchScope::Handler &handler = catchScopeRef.getHandler(i);
          if (handler.isCatchAll()) {
            emittedCatchAll = true;
            handlerAttrs.push_back(cir::CatchAllAttr::get(&getMLIRContext()));
            break;
          }
          handlerAttrs.push_back(handler.type.rtti);
        }
        if (!emittedCatchAll && !keepDeadObjCCatches)
          handlerAttrs.push_back(cir::UnwindAttr::get(&getMLIRContext()));
        tryOp.setHandlerTypesAttr(
            mlir::ArrayAttr::get(&getMLIRContext(), handlerAttrs));

        if (keepDeadObjCCatches) {
          tryOp->setAttr(kObjCDeadCatchesAttr,
                         mlir::UnitAttr::get(&getMLIRContext()));
        } else {
          emitCatchDispatchBlock(*this, catchScopeRef, tryOp);
        }
        ehStack.popCatch();

        for (const ObjCCatchHandler &handler : llvm::reverse(handlers)) {
          mlir::OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToStart(&handler.region->front());

          const ObjCAtCatchStmt *catchStmt = handler.stmt;
          mlir::Location handlerLoc = getLoc(catchStmt->getSourceRange());
          LexicalScope handlerScope{*this, handlerLoc,
                                    builder.getInsertionBlock()};
          if (normalFinallyStmt)
            handlerScope.setClangSourceRange(
                catchStmt->getCatchBody()->getSourceRange());
          RunCleanupsScope catchScope(*this);

          const VarDecl *catchParam = catchStmt->getCatchParamDecl();
          mlir::Location atCatchLoc = getLoc(catchStmt->getAtCatchLoc());
          mlir::Value caught;

          if (catchParam) {
            auto getCatchParamAllocaIP = [&]() {
              cir::CIRBaseBuilderTy::InsertPoint currIns =
                  builder.saveInsertionPoint();
              mlir::Operation *currParent = currIns.getBlock()->getParentOp();

              mlir::Block *insertBlock = nullptr;
              if (auto scopeOp = currParent->getParentOfType<cir::ScopeOp>())
                insertBlock = &scopeOp.getScopeRegion().getBlocks().back();
              else if (auto fnOp =
                           currParent->getParentOfType<cir::FuncOp>())
                insertBlock = &fnOp.getRegion().getBlocks().back();
              else
                llvm_unreachable("unknown outermost scope-like parent");
              return builder.getBestAllocaInsertPoint(insertBlock);
            };

            CIRGenFunction::AutoVarEmission var =
                emitAutoVarAlloca(*catchParam, getCatchParamAllocaIP());
            mlir::Type catchTy = convertTypeForMem(catchParam->getType());
            caught = emitObjCBeginCatch(
                *this, atCatchLoc, catchTy,
                /*endCatchMightThrow=*/isObjCCatchAllHandler(*this, catchParam));
            emitInitOfObjCCatchParam(*this, *catchParam,
                                     var.getObjectAddress(*this), caught,
                                     catchStmt->getBeginLoc());
            emitAutoVarCleanups(var);
          } else {
            caught = emitObjCBeginCatch(*this, atCatchLoc,
                                        builder.getVoidPtrTy(),
                                        /*endCatchMightThrow=*/true);
          }

          objCEHValueStack.push_back(caught);
          auto popCaught = llvm::scope_exit([&] { objCEHValueStack.pop_back(); });

          if (normalFinallyStmt)
            pushActiveObjCFinallyScope(
                normalFinallyStmt->getFinallyBody(),
                catchStmt->getCatchBody()->getSourceRange(), &handlerScope);
          if (emitStmt(catchStmt->getCatchBody(), /*useCurrentScope=*/true)
                  .failed())
            return mlir::failure();
          if (normalFinallyStmt) {
            popActiveObjCFinallyScopeIfPresent(
                normalFinallyStmt->getFinallyBody(), &handlerScope);
            catchBodyHasFallthrough |= hasLiveFallthroughInsertPoint(*this);
          }

          catchScope.forceCleanup();
        }
      }
    }

    hasNormalFallthrough = tryBodyHasFallthrough || catchBodyHasFallthrough;
    if (haveInsertPoint())
      cir::YieldOp::create(builder, loc);
    return mlir::success();
  };

  if (const ObjCAtFinallyStmt *finallyStmt = s.getFinallyStmt()) {
    if (numHandlers) {
      mlir::Location tryLoc = getLoc(s.getBeginLoc());
      mlir::Location finallyLoc = getLoc(finallyStmt->getAtFinallyLoc());

      mlir::OpBuilder::InsertPoint scopeIP;
      cir::ScopeOp::create(
          builder, loc,
          /*scopeBuilder=*/[&](mlir::OpBuilder &, mlir::Location) {
            scopeIP = builder.saveInsertionPoint();
          });

      mlir::OpBuilder::InsertionGuard outerGuard(builder);
      builder.restoreInsertionPoint(scopeIP);

      mlir::OpBuilder::InsertPoint beginInsertTryBody;
      auto tryOp = cir::TryOp::create(
          builder, tryLoc,
          /*tryBuilder=*/[&](mlir::OpBuilder &, mlir::Location) {
            beginInsertTryBody = builder.saveInsertionPoint();
          },
          /*handlersBuilder=*/
          [&](mlir::OpBuilder &, mlir::Location,
              mlir::OperationState &result) {
            mlir::Region *handler = result.addRegion();
            builder.createBlock(handler);
          });

      bool protectedBodyHasFallthrough = false;
      mlir::Region *finallyHandler = &tryOp.getHandlerRegions().front();

      {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.restoreInsertionPoint(beginInsertTryBody);
        CIRGenFunction::LexicalScope tryScope{*this, loc,
                                              builder.getInsertionBlock()};
        tryScope.setAsTry(tryOp);

        EHCatchScope *catchScope = ehStack.pushCatch(1);
        catchScope->setHandler(0, cgm.getCXXABI().getCatchAllTypeInfo(),
                               finallyHandler, /*stmt=*/nullptr);

        if (emitObjCTryCatchStmt(finallyStmt, protectedBodyHasFallthrough)
                .failed())
          return mlir::failure();

        EHCatchScope &catchScopeRef = cast<EHCatchScope>(*ehStack.begin());
        if (!catchScopeRef.mayThrow()) {
          catchScopeRef.clearHandlerBlocks();
          ehStack.popCatch();

          for (mlir::Block &b :
               llvm::make_early_inc_range(finallyHandler->getBlocks()))
            b.erase();
          tryOp.setHandlerTypesAttr({});
        } else {
          tryOp.setHandlerTypesAttr(mlir::ArrayAttr::get(
              &getMLIRContext(), {cir::CatchAllAttr::get(&getMLIRContext())}));

          emitCatchDispatchBlock(*this, catchScopeRef, tryOp);
          ehStack.popCatch();

          mlir::OpBuilder::InsertionGuard handlerGuard(builder);
          builder.setInsertionPointToStart(&finallyHandler->front());

          LexicalScope handlerScope{*this, finallyLoc,
                                    builder.getInsertionBlock()};
          handlerScope.setClangSourceRange(
              finallyStmt->getFinallyBody()->getSourceRange());
          RunCleanupsScope finallyScope(*this);

          (void)emitObjCBeginCatch(*this, finallyLoc, builder.getVoidPtrTy(),
                                   /*endCatchMightThrow=*/true);

          if (emitObjCFinallyBody(finallyStmt->getFinallyBody(),
                                  /*useCurrentScope=*/true)
                  .failed())
            return mlir::failure();

          if (haveInsertPoint() && emitObjCRethrow(finallyLoc).failed())
            return mlir::failure();

          finallyScope.forceCleanup();
        }
      }

      if (protectedBodyHasFallthrough) {
        if (emitObjCFinallyBody(finallyStmt->getFinallyBody(),
                                /*useCurrentScope=*/false)
                .failed())
          return mlir::failure();
      }

      if (haveInsertPoint())
        cir::YieldOp::create(builder, loc);
      return mlir::success();
    }

    mlir::Location tryLoc = getLoc(s.getBeginLoc());
    mlir::Location finallyLoc = getLoc(finallyStmt->getAtFinallyLoc());

    mlir::OpBuilder::InsertPoint scopeIP;
    cir::ScopeOp::create(
        builder, loc,
        /*scopeBuilder=*/[&](mlir::OpBuilder &, mlir::Location) {
          scopeIP = builder.saveInsertionPoint();
        });

    mlir::OpBuilder::InsertionGuard outerGuard(builder);
    builder.restoreInsertionPoint(scopeIP);

    mlir::OpBuilder::InsertPoint beginInsertTryBody;
    auto tryOp = cir::TryOp::create(
        builder, tryLoc,
        /*tryBuilder=*/[&](mlir::OpBuilder &, mlir::Location) {
          beginInsertTryBody = builder.saveInsertionPoint();
        },
        /*handlersBuilder=*/
        [&](mlir::OpBuilder &, mlir::Location, mlir::OperationState &result) {
          mlir::Region *handler = result.addRegion();
          builder.createBlock(handler);
        });

    bool tryBodyHasFallthrough = false;
    mlir::Region *finallyHandler = &tryOp.getHandlerRegions().front();

    {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.restoreInsertionPoint(beginInsertTryBody);
      CIRGenFunction::LexicalScope tryScope{*this, loc,
                                            builder.getInsertionBlock()};
      tryScope.setAsTry(tryOp);

      EHCatchScope *catchScope = ehStack.pushCatch(1);
      catchScope->setHandler(0, cgm.getCXXABI().getCatchAllTypeInfo(),
                             finallyHandler, /*stmt=*/nullptr);

      {
        mlir::OpBuilder::InsertionGuard guard(builder);
        CIRGenFunction::LexicalScope tryBodyScope{*this, loc,
                                                  builder.getInsertionBlock()};
        tryBodyScope.setClangSourceRange(s.getTryBody()->getSourceRange());
        pushActiveObjCFinallyScope(finallyStmt->getFinallyBody(),
                                   s.getTryBody()->getSourceRange(),
                                   &tryBodyScope);
        if (emitStmt(s.getTryBody(), /*useCurrentScope=*/true).failed())
          return mlir::failure();
        popActiveObjCFinallyScopeIfPresent(finallyStmt->getFinallyBody(),
                                           &tryBodyScope);
        tryBodyHasFallthrough = hasLiveFallthroughInsertPoint(*this);
      }

      EHCatchScope &catchScopeRef = cast<EHCatchScope>(*ehStack.begin());
      if (!catchScopeRef.mayThrow()) {
        catchScopeRef.clearHandlerBlocks();
        ehStack.popCatch();

        for (mlir::Block &b :
             llvm::make_early_inc_range(finallyHandler->getBlocks()))
          b.erase();
        tryOp.setHandlerTypesAttr({});
      } else {
        tryOp.setHandlerTypesAttr(mlir::ArrayAttr::get(
            &getMLIRContext(), {cir::CatchAllAttr::get(&getMLIRContext())}));

        emitCatchDispatchBlock(*this, catchScopeRef, tryOp);
        ehStack.popCatch();

        mlir::OpBuilder::InsertionGuard handlerGuard(builder);
        builder.setInsertionPointToStart(&finallyHandler->front());

        LexicalScope handlerScope{*this, finallyLoc,
                                  builder.getInsertionBlock()};
        handlerScope.setClangSourceRange(
            finallyStmt->getFinallyBody()->getSourceRange());
        RunCleanupsScope finallyScope(*this);

        (void)emitObjCBeginCatch(*this, finallyLoc, builder.getVoidPtrTy(),
                                 /*endCatchMightThrow=*/true);

        if (emitObjCFinallyBody(finallyStmt->getFinallyBody(),
                                /*useCurrentScope=*/true)
                .failed())
          return mlir::failure();

        if (haveInsertPoint() && emitObjCRethrow(finallyLoc).failed())
          return mlir::failure();

        finallyScope.forceCleanup();
      }
    }

    if (tryBodyHasFallthrough) {
      if (emitObjCFinallyBody(finallyStmt->getFinallyBody(),
                              /*useCurrentScope=*/false)
              .failed())
        return mlir::failure();
    }

    if (haveInsertPoint())
      cir::YieldOp::create(builder, loc);
    return mlir::success();
  }

  bool hasNormalFallthrough = false;
  return emitObjCTryCatchStmt(/*normalFinallyStmt=*/nullptr,
                              hasNormalFallthrough);
}

void CIRGenFunction::enterCXXTryStmt(const CXXTryStmt &s, cir::TryOp tryOp,
                                     bool isFnTryBlock) {
  unsigned numHandlers = s.getNumHandlers();
  EHCatchScope *catchScope = ehStack.pushCatch(numHandlers);
  for (unsigned i = 0; i != numHandlers; ++i) {
    const CXXCatchStmt *catchStmt = s.getHandler(i);
    mlir::Region *handler = &tryOp.getHandlerRegions()[i];
    if (catchStmt->getExceptionDecl()) {
      // FIXME: Dropping the reference type on the type into makes it
      // impossible to correctly implement catch-by-reference
      // semantics for pointers.  Unfortunately, this is what all
      // existing compilers do, and it's not clear that the standard
      // personality routine is capable of doing this right.  See C++ DR 388:
      //   http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_active.html#388
      Qualifiers caughtTypeQuals;
      QualType caughtType = cgm.getASTContext().getUnqualifiedArrayType(
          catchStmt->getCaughtType().getNonReferenceType(), caughtTypeQuals);
      CatchTypeInfo typeInfo = {nullptr, 0};
      if (caughtType->isObjCObjectPointerType()) {
        if (usesDarwinNonFragileObjCRuntime(*this)) {
          typeInfo = {
              getObjCDarwinCatchHandlerType(*this,
                                            getLoc(catchStmt->getSourceRange()),
                                            caughtType),
              0};
        } else if (getLangOpts().ObjCRuntime.getKind() ==
                   ObjCRuntime::ObjFW) {
          typeInfo = {
              mlir::cast<mlir::TypedAttr>(cgm.getAddrOfRTTIDescriptor(
                  getLoc(catchStmt->getSourceRange()), caughtType,
                  /*forEh=*/true)),
              0};
        } else if (getLangOpts().ObjCRuntime.getKind() ==
                   ObjCRuntime::FragileMacOSX) {
          typeInfo = cgm.getCXXABI().getAddrOfCXXCatchHandlerType(
              getLoc(catchStmt->getSourceRange()), caughtType,
              catchStmt->getCaughtType());
        } else {
          cgm.errorNYI(catchStmt->getSourceRange(),
                       "enterCXXTryStmt ObjC object pointer catch for "
                       "non-Darwin non-fragile or GNU Objective-C runtime");
          ehStack.popCatch();
          return;
        }
      } else
        typeInfo = cgm.getCXXABI().getAddrOfCXXCatchHandlerType(
            getLoc(catchStmt->getSourceRange()), caughtType,
            catchStmt->getCaughtType());
      catchScope->setHandler(i, typeInfo, handler, catchStmt);
    } else {
      // No exception decl indicates '...', a catch-all.
      catchScope->setHandler(i, cgm.getCXXABI().getCatchAllTypeInfo(), handler,
                             s.getHandler(i));
    }

    // Under async exceptions, catch(...) needs to catch HW exception too
    // Mark scope with SehTryBegin as a SEH __try scope
    if (getLangOpts().EHAsynch) {
      cgm.errorNYI("enterCXXTryStmt: EHAsynch");
      ehStack.popCatch();
      return;
    }
  }
}

void CIRGenFunction::exitCXXTryStmt(const CXXTryStmt &s, bool isFnTryBlock) {
  unsigned numHandlers = s.getNumHandlers();
  EHCatchScope &catchScope = cast<EHCatchScope>(*ehStack.begin());
  assert(catchScope.getNumHandlers() == numHandlers);
  cir::TryOp tryOp = curLexScope->getTry();

  // If the catch was not required, bail out now.
  // CIR tracks whether an enclosing EH scope can throw using scopeMayThrow.
  // This mirrors OGCG's hasEHBranches-driven early bailout logic here: when no
  // throw path reaches this catch scope, handler blocks can be dropped.
  if (!catchScope.mayThrow()) {
    catchScope.clearHandlerBlocks();
    ehStack.popCatch();

    // Drop all basic block from all catch regions.
    SmallVector<mlir::Block *> eraseBlocks;
    for (mlir::Region &handlerRegion : tryOp.getHandlerRegions()) {
      if (handlerRegion.empty())
        continue;

      for (mlir::Block &b : handlerRegion.getBlocks())
        eraseBlocks.push_back(&b);
    }

    for (mlir::Block *b : eraseBlocks)
      b->erase();

    tryOp.setHandlerTypesAttr({});
    return;
  }

  // Ensure the TryOp has handler types set. When all throws/calls in the
  // try body are inside nested TryOps, populateCatchHandlers (which sets
  // handler types) is only called for the inner TryOps — not this one.
  // Set handler types from the CatchScope's handlers so FlattenCFG knows
  // what types to dispatch.
  mlir::ArrayAttr existingTypes = tryOp.getHandlerTypesAttr();
  if (!existingTypes || existingTypes.empty()) {
    bool hasCatchAll = false;
    bool hasEnclosingTerminate = false;
    llvm::SmallVector<mlir::Attribute> handlerAttrs;
    for (unsigned i = 0; i < catchScope.getNumHandlers(); ++i) {
      const EHCatchScope::Handler &handler = catchScope.getHandler(i);
      if (handler.isCatchAll()) {
        hasCatchAll = true;
        handlerAttrs.push_back(cir::CatchAllAttr::get(&getMLIRContext()));
        break;
      }
      handlerAttrs.push_back(handler.type.rtti);
    }
    if (!hasCatchAll) {
      // Check if there's a Terminate scope enclosing this try-catch.
      // If so, unhandled exceptions should terminate, not resume.
      for (EHScopeStack::stable_iterator si =
               catchScope.getEnclosingEHScope();
           si != ehStack.stable_end();
           si = ehStack.find(si)->getEnclosingEHScope()) {
        EHScope &s = *ehStack.find(si);
        if (s.getKind() == EHScope::Terminate) {
          hasEnclosingTerminate = true;
          break;
        }
      }
      if (hasEnclosingTerminate)
        handlerAttrs.push_back(cir::CatchAllAttr::get(&getMLIRContext()));
      else
        handlerAttrs.push_back(cir::UnwindAttr::get(&getMLIRContext()));
    }
    tryOp.setHandlerTypesAttr(
        mlir::ArrayAttr::get(&getMLIRContext(), handlerAttrs));

    // If the unwind handler was replaced with a terminate handler,
    // populate the handler region with std::terminate call.
    if (!hasCatchAll && hasEnclosingTerminate)
      emitTerminateHandlerBody(tryOp);
  }

  // Emit the structure of the EH dispatch for this catch.
  emitCatchDispatchBlock(*this, catchScope, tryOp);

  // Copy the handler blocks off before we pop the EH stack.  Emitting
  // the handlers might scribble on this memory.
  SmallVector<EHCatchScope::Handler> handlers(catchScope.begin(),
                                              catchScope.begin() + numHandlers);

  ehStack.popCatch();

  // Determine if we need an implicit rethrow for all these catch handlers;
  // see the comment below.
  bool doImplicitRethrow =
      isFnTryBlock && isa<CXXDestructorDecl, CXXConstructorDecl>(curCodeDecl);

  // Wasm uses Windows-style EH instructions, but merges all catch clauses into
  // one big catchpad. So we save the old funclet pad here before we traverse
  // each catch handler.
  if (EHPersonality::get(*this).isWasmPersonality()) {
    cgm.errorNYI("exitCXXTryStmt: WASM personality");
    return;
  }

  bool hasCatchAll = false;
  for (auto &handler : llvm::reverse(handlers)) {
    hasCatchAll |= handler.isCatchAll();
    mlir::Region *catchRegion = handler.region;
    const CXXCatchStmt *catchStmt = handler.stmt;

    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(&catchRegion->front());

    // Use a LexicalScope for the catch handler body to properly handle
    // control flow (returns, breaks) and clean up unreachable blocks.
    // Without this, return statements inside catch handlers leave empty
    // continuation blocks that fail MLIR verification.
    mlir::Location handlerLoc = getLoc(catchStmt->getSourceRange());
    LexicalScope handlerScope{*this, handlerLoc,
                              builder.getInsertionBlock()};
    // Record the handler block's source range so emitGotoStmt's forward-goto
    // walk recognizes a goto whose label lives inside this handler as NOT
    // exiting the handler scope. Without this the walk sees no source range,
    // assumes the goto escapes, and emits the handler cleanups (catch-variable
    // dtor + __cxa_end_catch) on the goto path — running them early/twice for a
    // goto that stays within the handler. Mirrors the try-body fix above.
    handlerScope.setClangSourceRange(
        catchStmt->getHandlerBlock()->getSourceRange());

    // Enter a cleanup scope, including the catch variable and the
    // end-catch.
    RunCleanupsScope catchScope(*this);

    // Initialize the catch variable and set up the cleanups.
    // OGCG passes CurrentFuncletPad for Windows SEH funclet-based EH.
    // CIR uses DWARF/Itanium EH; Windows is not a supported target.
    cgm.getCXXABI().emitBeginCatch(*this, catchStmt);

    // Emit the PGO counter increment.
    incrementProfileCounter(catchStmt);

    // Perform the body of the catch.
    [[maybe_unused]] mlir::LogicalResult emitResult =
        emitStmt(catchStmt->getHandlerBlock(), /*useCurrentScope=*/true);
    assert(emitResult.succeeded() && "failed to emit catch handler block");

    // [except.handle]p11:
    //   The currently handled exception is rethrown if control
    //   reaches the end of a handler of the function-try-block of a
    //   constructor or destructor.

    // It is important that we only do this on fallthrough and not on
    // return.  Note that it's illegal to put a return in a
    // constructor function-try-block's catch handler (p14), so this
    // really only applies to destructors.
    if (doImplicitRethrow && haveInsertPoint()) {
      cgm.getCXXABI().emitRethrow(*this, /*isNoReturn=*/false);
      // Note: OGCG does Builder.CreateUnreachable() +
      // Builder.ClearInsertionPoint() here. In CIR, emitRethrow() calls
      // insertThrowAndSplit() which already emits UnreachableOp and creates a
      // continuation block. The continuation block receives dead code from
      // catchScope.forceCleanup() but this is harmless — dead blocks are
      // eliminated during later passes.
    }

    // Fall out through the catch cleanups.
    catchScope.forceCleanup();
  }

  // Because in wasm we merge all catch clauses into one big catchpad, in case
  // none of the types in catch handlers matches after we test against each of
  // them, we should unwind to the next EH enclosing scope. We generate a call
  // to rethrow function here to do that.
  if (EHPersonality::get(*this).isWasmPersonality() && !hasCatchAll) {
    cgm.errorNYI("exitCXXTryStmt: WASM personality without catch all");
    return;
  }
}

/// Populate the catch-all handler region of a TryOp with a call to
/// std::terminate. Used for noexcept function terminate scopes.
void CIRGenFunction::emitTerminateHandlerBody(cir::TryOp tryOp) {
  // OGCG calls CGM.getCXXABI().emitTerminateForUnexpectedException() which
  // dispatches through the ABI layer. CIR performs ABI-based symbol selection
  // directly below and relies on CatchParamOp + FlattenCFG to achieve the same
  // effect.
  mlir::Region &handlerRegion = tryOp.getHandlerRegions().back();
  if (handlerRegion.empty())
    return;

  mlir::Block &handlerBlock = handlerRegion.front();
  mlir::OpBuilder::InsertionGuard guard(builder);
  handlerBlock.clear();
  builder.setInsertionPointToStart(&handlerBlock);

  llvm::StringRef terminateFnName;
  if (getLangOpts().CPlusPlus && getTarget().getCXXABI().isItaniumFamily())
    terminateFnName = "_ZSt9terminatev";
  else if (getLangOpts().CPlusPlus && getTarget().getCXXABI().isMicrosoft()) {
    if (getLangOpts().isCompatibleWithMSVC(LangOptions::MSVC2015))
      terminateFnName = "__std_terminate";
    else
      terminateFnName = "?terminate@@YAXXZ";
  } else {
    // OGCG also checks getLangOpts().ObjC &&
    // getLangOpts().ObjCRuntime.hasTerminate() and uses "objc_terminate"
    // for Objective-C runtimes that support it.
    if (getLangOpts().ObjC && getLangOpts().ObjCRuntime.hasTerminate())
      terminateFnName = "objc_terminate";
    else
      terminateFnName = "abort";
  }

  auto *ctx = &getMLIRContext();
  auto voidTy = cir::VoidType::get(ctx);
  auto terminateFnTy = cir::FuncType::get({}, voidTy);
  auto modOp = cgm.getModule();
  if (!cgm.getGlobalValue(terminateFnName)) {
    mlir::OpBuilder::InsertionGuard modGuard(builder);
    builder.setInsertionPointToStart(modOp.getBody());
    auto fn = cir::FuncOp::create(builder, tryOp.getLoc(), terminateFnName,
                                   terminateFnTy);
    cgm.insertGlobalSymbol(fn);
    fn.setPrivate();
  }

  // Emit CatchParamOp + call std::terminate + YieldOp.
  // FlattenCFG's buildAllCase rewrites CatchParamOp to __cxa_begin_catch(exn),
  // then inlines the handler body, matching OGCG's __clang_call_terminate.
  auto ptrToVoid = cir::PointerType::get(voidTy);
  cir::CatchParamOp::create(builder, tryOp.getLoc(), ptrToVoid,
                             /*exception_ptr=*/mlir::Value{},
                             /*kind=*/cir::CatchParamKindAttr{});
  auto terminateCall = cir::CallOp::create(
      builder, tryOp.getLoc(),
      mlir::SymbolRefAttr::get(ctx, terminateFnName),
      /*resType=*/mlir::Type{}, mlir::ValueRange{});
  terminateCall.setNothrow(true);
  // OGCG also sets doesNotReturn on the terminate call. CIR CallOp does not
  // have a noreturn attribute yet, but the following UnreachableOp serves the
  // same semantic purpose.
  // callOpNoReturnAttr -- NoReturn on call ops handled differently in CIR.
  // std::terminate() never returns — emit unreachable instead of yield.
  // FlattenCFG detects this and skips __cxa_end_catch + branch emission,
  // matching OGCG's __clang_call_terminate pattern.
  cir::UnreachableOp::create(builder, tryOp.getLoc());
}

/// Populate a TryOp's catch-all handler region with a call to
/// __cxa_call_unexpected. Used for dynamic exception specifications
/// (e.g., void foo() throw(int)). Unlike the terminate handler, the filter
/// handler does NOT call __cxa_begin_catch — it passes the raw exception
/// pointer directly to __cxa_call_unexpected. FlattenCFG handles the
/// selector < 0 check and resume path.
void CIRGenFunction::emitFilterHandlerBody(cir::TryOp tryOp) {
  mlir::Region &handlerRegion = tryOp.getHandlerRegions().back();
  if (handlerRegion.empty())
    return;

  mlir::Block &handlerBlock = handlerRegion.front();
  mlir::OpBuilder::InsertionGuard guard(builder);
  handlerBlock.clear();
  builder.setInsertionPointToStart(&handlerBlock);

  auto *ctx = &getMLIRContext();
  auto voidTy = cir::VoidType::get(ctx);
  auto ptrToVoid = cir::PointerType::get(voidTy);

  // Declare __cxa_call_unexpected(void *thrown_exception).
  // This function is specified to never return.
  auto unexpectedFnTy = cir::FuncType::get({ptrToVoid}, voidTy);
  cgm.createRuntimeFunction(unexpectedFnTy, "__cxa_call_unexpected");

  // Get the raw exception pointer. We use CatchParamOp without a kind here
  // as a placeholder. FlattenCFG will handle this differently for filter
  // handlers: it passes the raw exn ptr directly instead of wrapping with
  // __cxa_begin_catch. The cir.filter_handler discardable attribute on this
  // op signals this intent to FlattenCFG.
  auto exnPtr = cir::CatchParamOp::create(builder, tryOp.getLoc(), ptrToVoid,
                                           /*exception_ptr=*/mlir::Value{},
                                           /*kind=*/cir::CatchParamKindAttr{});
  exnPtr->setAttr("cir.filter_handler",
                   mlir::UnitAttr::get(ctx));

  // Call __cxa_call_unexpected(exn). This doesn't need to be an invoke
  // because __cxa_call_unexpected magically filters exceptions according
  // to the last landing pad the exception was thrown into (per OGCG comment).
  auto callOp = cir::CallOp::create(
      builder, tryOp.getLoc(),
      mlir::SymbolRefAttr::get(ctx, "__cxa_call_unexpected"),
      /*resType=*/mlir::Type{}, mlir::ValueRange{exnPtr.getParam()});
  callOp.setNothrow(true);

  // __cxa_call_unexpected never returns.
  cir::UnreachableOp::create(builder, tryOp.getLoc());
}

void CIRGenFunction::populateCatchHandlers(cir::TryOp tryOp) {
  assert(ehStack.requiresCatchOrCleanup());
  assert(!cgm.getLangOpts().IgnoreExceptions &&
         "LandingPad should not be emitted when -fignore-exceptions are in "
         "effect.");

  EHScope &innermostEHScope = *ehStack.find(ehStack.getInnermostEHScope());
  switch (innermostEHScope.getKind()) {
  case EHScope::Terminate: {
    // noexcept function — set up a catch-all that calls std::terminate.
    mlir::ArrayAttr handlerTypesAttr = tryOp.getHandlerTypesAttr();
    if (!handlerTypesAttr || handlerTypesAttr.empty()) {
      tryOp.setHandlerTypesAttr(mlir::ArrayAttr::get(
          &getMLIRContext(),
          {cir::CatchAllAttr::get(&getMLIRContext())}));
    }
    innermostEHScope.setMayThrow(true);

    // Populate the per-call cleanup region.
    if (callWithExceptionCtx) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      assert(callWithExceptionCtx.getCleanup().empty() &&
             "one per call: expected empty region at this point");
      builder.createBlock(&callWithExceptionCtx.getCleanup());
      cir::YieldOp::create(builder, callWithExceptionCtx.getLoc());
    }

    emitTerminateHandlerBody(tryOp);
    return;
  }

  case EHScope::Filter: {
    // Dynamic exception spec — set up a catch-all handler that calls
    // __cxa_call_unexpected, with filter types for the landing pad.
    EHFilterScope &filterScope = cast<EHFilterScope>(innermostEHScope);
    mlir::ArrayAttr handlerTypesAttr = tryOp.getHandlerTypesAttr();
    if (!handlerTypesAttr || handlerTypesAttr.empty()) {
      tryOp.setHandlerTypesAttr(mlir::ArrayAttr::get(
          &getMLIRContext(),
          {cir::CatchAllAttr::get(&getMLIRContext())}));
    }
    innermostEHScope.setMayThrow(true);

    // Store filter types for FlattenCFG → LLVM filter clause generation.
    llvm::SmallVector<mlir::Attribute> filterTypes;
    for (unsigned i = 0; i < filterScope.getNumFilters(); ++i)
      filterTypes.push_back(filterScope.getFilter(i));
    tryOp->setAttr("cir.filter_types",
                    mlir::ArrayAttr::get(&getMLIRContext(), filterTypes));

    // Populate the per-call cleanup region.
    if (callWithExceptionCtx) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      assert(callWithExceptionCtx.getCleanup().empty() &&
             "one per call: expected empty region at this point");
      builder.createBlock(&callWithExceptionCtx.getCleanup());
      cir::YieldOp::create(builder, callWithExceptionCtx.getLoc());
    }

    emitFilterHandlerBody(tryOp);
    return;
  }

  case EHScope::Catch:
  case EHScope::Cleanup:
    // CIR does not cache landing pads.
    break;
  }

  // If there's an existing TryOp, it means we got a `cir.try` scope
  // that leads to this "landing pad" creation site. Otherwise, exceptions
  // are enabled but a throwing function is called anyways (common pattern
  // with function local static initializers).
  mlir::ArrayAttr handlerTypesAttr = tryOp.getHandlerTypesAttr();
  if (!handlerTypesAttr || handlerTypesAttr.empty()) {
    // Accumulate all the handlers in scope.
    bool hasCatchAll = false;
    bool hasCleanup = false;
    bool terminateCatchAll = false;
    bool filterCatchAll = false;
    EHFilterScope *outerFilterScope = nullptr;
    llvm::SmallPtrSet<mlir::Attribute, 4> catchTypes;
    llvm::SmallVector<mlir::Attribute> handlerAttrs;
    for (EHScopeStack::iterator i = ehStack.begin(), e = ehStack.end(); i != e;
         ++i) {
      switch (i->getKind()) {
      case EHScope::Cleanup:
        hasCleanup = (hasCleanup || cast<EHCleanupScope>(*i).isEHCleanup());
        continue;

      case EHScope::Filter:
        // Filter scope acts as catch-all (catches unexpected exceptions).
        hasCatchAll = true;
        filterCatchAll = true;
        outerFilterScope = &cast<EHFilterScope>(*i);
        goto doneAccumulating;

      case EHScope::Terminate:
        // Terminate scope means catch-all — stop accumulating handlers.
        hasCatchAll = true;
        terminateCatchAll = true;
        goto doneAccumulating;

      case EHScope::Catch:
        break;
      } // end switch

      EHCatchScope &catchScope = cast<EHCatchScope>(*i);
      for (const EHCatchScope::Handler &handler :
           llvm::make_range(catchScope.begin(), catchScope.end())) {
        assert(handler.type.flags == 0 &&
               "landingpads do not support catch handler flags");

        // If this is a catch-all, register that and abort.
        if (handler.isCatchAll()) {
          assert(!hasCatchAll);
          hasCatchAll = true;
          break;
        }

        // Check whether we already have a handler for this type.
        // If not, keep track to later add to catch op.
        if (catchTypes.insert(handler.type.rtti).second)
          handlerAttrs.push_back(handler.type.rtti);
      }

      // Only process the innermost catch scope — enclosing catch scopes
      // belong to outer TryOps and should not affect this TryOp's handler
      // attributes. Without this break, nested try-catch produces incorrect
      // CatchAllAttr (from the outer catch(...)) on the inner TryOp's
      // unwind region, causing FlattenCFG to crash in buildAllCase.
      //
      // However, after processing the Catch scope, walk enclosing scopes to
      // check for:
      // - Terminate scope (noexcept function): unhandled exceptions should
      //   terminate, not resume.
      // - Cleanup scopes: locals declared outside the try block
      //   need destruction when the exception type doesn't match any catch
      //   handler. The landing pad needs a `cleanup` clause so the
      //   personality function enters it for non-matching exceptions.
      if (!hasCatchAll) {
        for (EHScopeStack::stable_iterator si = i->getEnclosingEHScope();
             si != ehStack.stable_end();
             si = ehStack.find(si)->getEnclosingEHScope()) {
          EHScope &s = *ehStack.find(si);
          if (s.getKind() == EHScope::Terminate) {
            hasCatchAll = true;
            terminateCatchAll = true;
            break;
          }
          if (s.getKind() == EHScope::Filter) {
            hasCatchAll = true;
            filterCatchAll = true;
            outerFilterScope = &cast<EHFilterScope>(s);
            break;
          }
          if (s.getKind() == EHScope::Cleanup) {
            hasCleanup =
                (hasCleanup || cast<EHCleanupScope>(s).isEHCleanup());
            continue;
          }
          // Stop at another Catch scope (belongs to outer TryOp).
          if (s.getKind() == EHScope::Catch)
            break;
        }
      }
      break;
    }
    doneAccumulating:

    // When the catch-all comes from a Terminate or Filter scope,
    // DON'T set the cleanup flag — these scopes act as hard boundaries.
    // Destructors should NOT run before std::terminate() / __cxa_call_unexpected.
    if (hasCleanup && !terminateCatchAll && !filterCatchAll)
      tryOp.setCleanup(true);

    if (hasCatchAll)
      handlerAttrs.push_back(cir::CatchAllAttr::get(&getMLIRContext()));

    assert((!handlerAttrs.empty() || hasCleanup) && "no catch clauses!");

    // If there's no catch_all, attach the unwind region. This needs to be the
    // last region in the TryOp catch list.
    if (!hasCatchAll)
      handlerAttrs.push_back(cir::UnwindAttr::get(&getMLIRContext()));

    // Add final array of clauses into TryOp.
    tryOp.setHandlerTypesAttr(
        mlir::ArrayAttr::get(&getMLIRContext(), handlerAttrs));

    // If the catch-all came from a Terminate scope, populate the handler
    // region with std::terminate(). This handles the noexcept + RAII case
    // where Cleanup scopes are innermost but Terminate is outer.
    if (terminateCatchAll)
      emitTerminateHandlerBody(tryOp);

    // If the catch-all came from a Filter scope, populate the handler
    // region with __cxa_call_unexpected and store filter types.
    if (filterCatchAll && outerFilterScope) {
      llvm::SmallVector<mlir::Attribute> filterTypes;
      for (unsigned i = 0; i < outerFilterScope->getNumFilters(); ++i)
        filterTypes.push_back(outerFilterScope->getFilter(i));
      tryOp->setAttr("cir.filter_types",
                      mlir::ArrayAttr::get(&getMLIRContext(), filterTypes));
      emitFilterHandlerBody(tryOp);
    }
  }

  // In traditional LLVM codegen. this tells the backend how to generate the
  // landing pad by generating a branch to the dispatch block. In CIR,
  // this is used to populate blocks for later filing during
  // cleanup handling.
  populateEHCatchRegions(ehStack.getInnermostEHScope(), tryOp);
}

// Emit the active EH cleanup chain ONCE into the current insertion point, for a
// cir.cleanup.scope's shared cleanup region. Mirrors the per-call cleanup walk
// in populateEHCatchRegions (below), but runs a single time and is shared by
// every throwing call in the scope body — turning the per-call O(N^2)
// materialization into O(N). Callers must ensure the cleanups' addresses
// dominate the enclosing cir.cleanup.scope op.
void CIRGenFunction::emitSharedCleanupScopeDispatch(
    EHScopeStack::stable_iterator boundary, unsigned sharedScopeChunkFilter) {
  bool savedIsEmittingEHCleanup = isEmittingEHCleanup;
  isEmittingEHCleanup = true;
  mlir::Value savedCXXThisValue = cxxThisValue;
  CharUnits savedCXXThisAlignment = cxxThisAlignment;
  if (cxxabiThisValue && cxxabiThisValue != cxxThisValue) {
    cxxThisValue = cxxabiThisValue;
    cxxThisAlignment = cxxabiThisAlignment;
  }
  for (EHScopeStack::stable_iterator si = ehStack.getInnermostEHScope();
       si != ehStack.stable_end() && si != boundary;
       si = ehStack.find(si)->getEnclosingEHScope()) {
    EHScope &s = *ehStack.find(si);
    if (s.getKind() != EHScope::Cleanup)
      break;
    EHCleanupScope &cs = cast<EHCleanupScope>(s);
    if (!cs.isEHCleanup())
      continue;
    // A chunked dispatch emits only its own chunk's cleanups. Other chunks'
    // cleanups are above this walk's reach on their own nested pad, which
    // FlattenCFG chains outward to this one; emitting them here too would put
    // every flag in every pad, which is the quadratic PHI web chunking exists
    // to avoid. Unchunked cleanups (id 0) never match a nonzero filter.
    if (sharedScopeChunkFilter != 0 &&
        cs.getSharedScopeChunkId() != sharedScopeChunkFilter)
      continue;
    // Unlike the per-call walk below, this runs at pop time -- after a
    // partial-construction cleanup has been deactivated. A cleanup carrying an
    // EH active flag was live somewhere inside the scope body, so it still
    // belongs on the shared unwind edge, guarded by that flag (this is what
    // OGCG's PopCleanupBlock does: it emits the EH path regardless of the
    // pop-time active bit whenever there is a flag to test).
    if (!cs.isActive() && !cs.shouldTestFlagInEHCleanup() &&
        !cs.isActiveOverCleanupScopeRange())
      continue;
    if (cs.isLifetimeMarker())
      continue;
    EHScopeStack::Cleanup *cleanup = cs.getCleanup();
    EHScopeStack::Cleanup::Flags flags;
    flags.setIsForEHCleanup();
    flags.setIsEHCleanupKind();
    Address ehActiveFlag = cs.shouldTestFlagInEHCleanup() ? cs.getActiveFlag()
                                                          : Address::invalid();
    if (ehActiveFlag.isValid()) {
      auto loc = currSrcLoc ? *currSrcLoc
                            : mlir::UnknownLoc::get(builder.getContext());
      mlir::Value isActive = builder.createLoad(loc, ehActiveFlag);
      cir::IfOp::create(builder, loc, isActive, /*withElseRegion=*/false,
                        /*thenBuilder=*/
                        [&](mlir::OpBuilder &b, mlir::Location l) {
                          cleanup->emit(*this, flags);
                          // This cleanup has now run on this unwind path. The
                          // region FlattenCFG chains this pad into tests the
                          // same flag -- an enclosing cir.cleanup.scope covers
                          // this cleanup too whenever its guarded range starts
                          // before the cleanup was pushed, which is what a
                          // nested initializer_list looks like -- and would
                          // destroy the object a second time. Clearing the
                          // flag is sound here and nowhere else: this code is
                          // reached only while unwinding, so the chained
                          // region is the one reader left. Clearing rather
                          // than leaving the cleanup out of the inner region
                          // also keeps destruction reverse-of-construction.
                          builder.createStore(l, builder.getFalse(l),
                                              ehActiveFlag);
                          cir::YieldOp::create(b, l);
                        },
                        /*elseBuilder=*/nullptr);
    } else {
      cleanup->emit(*this, flags);
    }
    if (builder.getInsertionBlock()->mightHaveTerminator()) {
      auto *terminator = builder.getInsertionBlock()->getTerminator();
      if (mlir::isa<cir::YieldOp>(terminator))
        terminator->erase();
      else
        break;
    }
  }
  isEmittingEHCleanup = savedIsEmittingEHCleanup;
  cxxThisValue = savedCXXThisValue;
  cxxThisAlignment = savedCXXThisAlignment;
}

// Differently from LLVM traditional codegen, there are no dispatch blocks
// to look at given cir.try_call does not jump to blocks like invoke does.
// However.
void CIRGenFunction::populateEHCatchRegions(EHScopeStack::stable_iterator scope,
                                            cir::TryOp tryOp) {
  if (EHPersonality::get(*this).usesFuncletPads() &&
      !isWinMSVCRawObjCCatchAllPlaceholderTry(tryOp)) {
    cgm.errorNYI("getEHDispatchBlock: usesFuncletPads");
    return;
  }

  // The dispatch block for the end of the scope chain is a block that
  // just resumes unwinding.
  if (scope == ehStack.stable_end()) {
    populateUnwindResumeBlock(/*isCleanup=*/true, tryOp);
    return;
  }

  // Otherwise, we should look at the actual scope.
  EHScope &ehScope = *ehStack.find(scope);
  bool mayThrow = ehScope.mayThrow();

  // Unlike the incubator which caches dispatch blocks per-scope, we populate
  // cleanup regions per-call using callWithExceptionCtx. For each call within
  // a try block, we populate its cleanup region independently.
  // The incubator's "mayThrow && tryOp" check was about a cached dispatch
  // block from a different TryOp - since we don't cache, we skip that check.

  switch (ehScope.getKind()) {
  case EHScope::Catch: {
    mayThrow = true;

    // LLVM does some optimization with branches here, CIR just keep track of
    // the corresponding calls.
    EHCatchScope &catchScope = cast<EHCatchScope>(ehScope);
    if (catchScope.getNumHandlers() == 1 &&
        catchScope.getHandler(0).isCatchAll()) {
      break;
    }

    // Populate cleanup region for this call with a YieldOp.
    if (callWithExceptionCtx) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      assert(callWithExceptionCtx.getCleanup().empty() &&
             "one per call: expected empty region at this point");
      builder.createBlock(&callWithExceptionCtx.getCleanup());
      cir::YieldOp::create(builder, callWithExceptionCtx.getLoc());
    }
    break;
  }
  case EHScope::Cleanup: {
    mayThrow = true;

    // Check if there's a Terminate scope beyond the cleanup scopes.
    // If so, the Terminate scope is a hard boundary — destructors should
    // NOT run before std::terminate(). Skip cleanup body emission entirely.
    // This matches OGCG which uses a simple `catch ptr null` landing pad
    // without cleanup for noexcept functions.
    bool hasOuterTerminate = false;
    for (EHScopeStack::stable_iterator si = scope;
         si != ehStack.stable_end();
         si = ehStack.find(si)->getEnclosingEHScope()) {
      EHScope &s = *ehStack.find(si);
      if (s.getKind() == EHScope::Terminate) {
        hasOuterTerminate = true;
        break;
      }
      if (s.getKind() != EHScope::Cleanup)
        break;
    }

    if (hasOuterTerminate) {
      // Terminate scope found — skip cleanup bodies. Just emit empty
      // cleanup region with a yield.
      if (callWithExceptionCtx) {
        mlir::OpBuilder::InsertionGuard guard(builder);
        assert(callWithExceptionCtx.getCleanup().empty() &&
               "one per call: expected empty region at this point");
        builder.createBlock(&callWithExceptionCtx.getCleanup());
        cir::YieldOp::create(builder, callWithExceptionCtx.getLoc());
      }
    } else {
    // Populate cleanup region for this call with ALL in-scope EH cleanup
    // bodies. When a call throws, all active destructors must run from
    // innermost to outermost — not just the innermost. Walk the EH scope
    // stack from the current scope outward, emitting each cleanup body,
    // until we reach a non-Cleanup scope (e.g., Catch) or the stack end.
    //
    // This mirrors OGCG's chained landing pads where each landing pad
    // branches to the next cleanup before resuming. In CIR we inline all
    // cleanup bodies into a single cleanup region instead.
    if (callWithExceptionCtx) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      assert(callWithExceptionCtx.getCleanup().empty() &&
             "one per call: expected empty region at this point");
      builder.createBlock(&callWithExceptionCtx.getCleanup());
      // CIR emits cleanup bodies eagerly at each call site, unlike OGCG
      // which emits them at scope exit. If we're inside a
      // CXXDefaultInitExprScope (e.g., nested aggregate default member
      // initializers), cxxThisValue has been temporarily modified to point
      // to the member field. EH cleanups like CallBaseDtor and DestroyField
      // call loadCXXThisAddress() and expect the constructor's original
      // 'this'. Restore it from cxxabiThisValue for the duration of
      // cleanup emission.
      mlir::Value savedCXXThisValue = cxxThisValue;
      CharUnits savedCXXThisAlignment = cxxThisAlignment;
      if (cxxabiThisValue && cxxabiThisValue != cxxThisValue) {
        cxxThisValue = cxxabiThisValue;
        cxxThisAlignment = cxxabiThisAlignment;
      }
      // Set isEmittingEHCleanup to prevent sub-calls from creating new
      // TryOps, which would cause infinite recursion with noexcept(false)
      // destructors. Per C++ [except.terminate], throwing during cleanup
      // calls std::terminate — matching OGCG's pushTerminate guard.
      bool savedIsEmittingEHCleanup = isEmittingEHCleanup;
      isEmittingEHCleanup = true;
      for (EHScopeStack::stable_iterator si = scope;
           si != ehStack.stable_end();
           si = ehStack.find(si)->getEnclosingEHScope()) {
        // Stop before reaching scopes handled by an enclosing synthetic
        // TryOp's handler. Without this, pre-existing RAII scopes
        // (e.g., ~Guard()) would be emitted both here and in the enclosing
        // handler, causing double-destroy during unwinding.
        if (si == ehCleanupWalkBoundary)
          break;
        EHScope &s = *ehStack.find(si);
        if (s.getKind() != EHScope::Cleanup)
          break;
        EHCleanupScope &cs = cast<EHCleanupScope>(s);
        if (!cs.isEHCleanup() || !cs.isActive())
          continue;
        // Lifetime markers (llvm.lifetime.end) are optimization hints, not
        // required for unwind correctness. Because this walk inlines every
        // active cleanup body into each throwing call's EH region, including
        // them makes the emitted IR O(N^2) in the number of live temporaries
        // in a single full-expression (e.g. a large braced-init-list, as in
        // generated Unicode tables). Skip them on the EH path; the normal-path
        // lifetime.end is still emitted once per temporary via popCleanupBlock.
        if (cs.isLifetimeMarker())
          continue;
        EHScopeStack::Cleanup *cleanup = cs.getCleanup();
        EHScopeStack::Cleanup::Flags flags;
        flags.setIsForEHCleanup();
        flags.setIsEHCleanupKind();

        // If this cleanup has an EH active flag (set by
        // pushDestroyAndDeferDeactivation), guard the emission with a
        // cir.if that loads the flag. This matches OGCG's landing pad
        // pattern where partial-construction cleanups are conditional on
        // whether the constructor actually ran.
        Address ehActiveFlag = cs.shouldTestFlagInEHCleanup()
                                   ? cs.getActiveFlag()
                                   : Address::invalid();
        if (ehActiveFlag.isValid()) {
          auto loc = currSrcLoc
                         ? *currSrcLoc
                         : mlir::UnknownLoc::get(builder.getContext());
          mlir::Value isActive = builder.createLoad(loc, ehActiveFlag);
          cir::IfOp::create(
              builder, loc, isActive, /*withElseRegion=*/false,
              /*thenBuilder=*/
              [&](mlir::OpBuilder &b, mlir::Location loc) {
                cleanup->emit(*this, flags);
                cir::YieldOp::create(b, loc);
              },
              /*elseBuilder=*/nullptr);
        } else {
          cleanup->emit(*this, flags);
        }
        // If the cleanup emitted a terminator, check whether to continue.
        // CallEndCatch emits a yield to mark the end of catch-scope cleanup,
        // but when throwing from inside a catch handler, we must continue
        // past it to emit outer-scope RAII destructors (e.g., ~R_r). Erase
        // the intermediate yield so subsequent cleanups can be emitted.
        // A fresh yield is added after the walk if needed (see below).
        // For non-yield terminators (e.g., unreachable), stop the walk.
        if (builder.getInsertionBlock()->mightHaveTerminator()) {
          auto *terminator = builder.getInsertionBlock()->getTerminator();
          if (mlir::isa<cir::YieldOp>(terminator))
            terminator->erase();
          else
            break;
        }
      }
      isEmittingEHCleanup = savedIsEmittingEHCleanup;
      cxxThisValue = savedCXXThisValue;
      cxxThisAlignment = savedCXXThisAlignment;
      // Add a yield terminator if one isn't already present.
      if (!builder.getInsertionBlock()->mightHaveTerminator())
        cir::YieldOp::create(builder, callWithExceptionCtx.getLoc());
    }
    } // end else (!hasOuterTerminate)
    // Propagate mayThrow to all enclosing scopes up to and including
    // the first catch scope. Without this, cleanup scopes between
    // the call and the catch scope would prevent the catch scope
    // from being marked as mayThrow, causing exitCXXTryStmt to
    // incorrectly clear the handler types.
    for (EHScopeStack::stable_iterator si = ehScope.getEnclosingEHScope();
         si != ehStack.stable_end();
         si = ehStack.find(si)->getEnclosingEHScope()) {
      EHScope &enclosing = *ehStack.find(si);
      enclosing.setMayThrow(true);
      if (enclosing.getKind() == EHScope::Catch)
        break;
    }
    break;
  }
  case EHScope::Filter: {
    mayThrow = true;
    // The Filter scope is handled in populateCatchHandlers which sets up
    // the catch-all handler with __cxa_call_unexpected. Nothing additional
    // needed here — analogous to Terminate scope handling.
    break;
  }
  case EHScope::Terminate: {
    mayThrow = true;
    // The Terminate scope is handled in populateCatchHandlers which sets up
    // the catch-all handler with std::terminate. Nothing additional needed here.
    break;
  }
  }

  ehScope.setMayThrow(mayThrow);
}

// in classic codegen this function is mapping to `isInvokeDest` previously and
// currently it's mapping to the conditions that performs early returns in
// `getInvokeDestImpl`, in CIR we need the condition to know if the EH scope may
// throw exception or now.
bool CIRGenFunction::isCatchOrCleanupRequired() {
  // Per C++ [except.terminate], if a cleanup itself throws during unwinding,
  // std::terminate is called. When we're already emitting an EH cleanup body,
  // sub-calls should not be wrapped in new TryOps — this prevents infinite
  // recursion with noexcept(false) destructors and matches OGCG's behavior
  // of pushing a terminate scope around EH cleanup emission.
  if (isEmittingEHCleanup)
    return false;

  // If exceptions are disabled/ignored and SEH is not in use, then there is no
  // invoke destination. SEH "works" even if exceptions are off. In practice,
  // this means that C++ destructors and other EH cleanups don't run, which is
  // consistent with MSVC's behavior, except in the presence of -EHa
  const LangOptions &lo = cgm.getLangOpts();
  if (!lo.Exceptions || lo.IgnoreExceptions) {
    if (!lo.Borland && !lo.MicrosoftExt)
      return false;
    // SEH try is Windows-only (not targeted by CIR).
    return false;
  }

  // CUDA device code doesn't have exceptions.
  if (lo.CUDA && lo.CUDAIsDevice)
    return false;

  return ehStack.requiresCatchOrCleanup();
}

void CIRGenFunction::emitStartEHSpec(const Decl *d) {
  if (!cgm.getLangOpts().CXXExceptions)
    return;

  const FunctionDecl *fd = dyn_cast_or_null<FunctionDecl>(d);
  if (!fd) {
    // Check if CapturedDecl is nothrow and create terminate scope for it.
    if (const CapturedDecl *cd = dyn_cast_or_null<CapturedDecl>(d)) {
      if (cd->isNothrow())
        ehStack.pushTerminate();
    }
    return;
  }
  const FunctionProtoType *proto = fd->getType()->getAs<FunctionProtoType>();
  if (!proto)
    return;


  // MSVC ABI does not use EH specs — return early (OGCG CGException.cpp:501).
  if (getTarget().getCXXABI().isMicrosoft())
    return;
  ExceptionSpecificationType est = proto->getExceptionSpecType();
  if (est == EST_Dynamic ||
      (est == EST_DynamicNone && !getLangOpts().CPlusPlus17)) {
    // Pre-C++17 throw() participates in the same filter-based lowering as
    // throw(T...), using a zero-filter EHFilterScope that still dispatches to
    // __cxa_call_unexpected on unwind.
    unsigned numExceptions = proto->getNumExceptions();
    EHFilterScope *filter = ehStack.pushFilter(numExceptions);
    for (unsigned i = 0; i < numExceptions; ++i) {
      QualType ty = proto->getExceptionType(i);
      QualType exceptType = ty.getNonReferenceType().getUnqualifiedType();
      mlir::Attribute ehType = cgm.getAddrOfRTTIDescriptor(
          cgm.getLoc(fd->getBeginLoc()), exceptType, /*forEH=*/true);
      filter->setFilter(i, ehType);
    }
    return;
  }

  if (proto->canThrow() == CT_Cannot) {
    // noexcept functions are simple terminate scopes.
    if (!getLangOpts().EHAsynch) // -EHa: HW exception still can occur
      ehStack.pushTerminate();
  }
}

void CIRGenFunction::emitEndEHSpec(const Decl *d) {
  if (!cgm.getLangOpts().CXXExceptions)
    return;

  const FunctionDecl *fd = dyn_cast_or_null<FunctionDecl>(d);
  if (!fd) {
    if (const CapturedDecl *cd = dyn_cast_or_null<CapturedDecl>(d)) {
      if (cd->isNothrow() && !ehStack.empty())
        ehStack.popTerminate();
    }
    return;
  }
  const FunctionProtoType *proto = fd->getType()->getAs<FunctionProtoType>();
  if (!proto)
    return;

  ExceptionSpecificationType est = proto->getExceptionSpecType();
  if (est == EST_Dynamic ||
      (est == EST_DynamicNone && !getLangOpts().CPlusPlus17)) {
    if (!ehStack.empty())
      ehStack.popFilter();
    return;
  }

  if (proto->canThrow() == CT_Cannot) {
    if (!getLangOpts().EHAsynch && !ehStack.empty())
      ehStack.popTerminate();
  }
}

// In classic codegen this function is equivalent to `getInvokeDestImpl`, in
// ClangIR we don't need to return to return any landing pad, we just need to
// populate the catch handlers if they are required
void CIRGenFunction::threadUserTryCleanupsOntoThrowingCall(cir::CallOp callOp) {
  if (!callOp || !isCatchOrCleanupRequired())
    return;
  cir::TryOp userTry =
      curLexScope ? curLexScope->getClosestTryParent() : cir::TryOp();
  // Only real user tries need per-call cleanup threading. For a synthetic
  // cleanup-only try the destructors are emitted eagerly in its handler, so
  // populating a per-call cleanup as well would run them twice.
  if (!userTry || userTry.getSynthetic())
    return;
  callWithExceptionCtx = callOp;
  populateCatchHandlersIfRequired(userTry);
  callWithExceptionCtx = nullptr;
}

void CIRGenFunction::populateCatchHandlersIfRequired(cir::TryOp tryOp) {
  assert(ehStack.requiresCatchOrCleanup());
  assert(!ehStack.empty());

  const EHPersonality &personality = EHPersonality::get(*this);

  // Set personality function if not already set. Skip if we're in a global
  // initializer context (GlobalOp) since the personality will be set later
  // when the init function is lowered.
  if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(curFn)) {
    if (!funcOp.getPersonality())
      funcOp.setPersonality(getPersonalityFn(cgm, personality));
  }

  // CIR does not cache landing pads.
  if (personality.usesFuncletPads() &&
      !isWinMSVCRawObjCCatchAllPlaceholderTry(tryOp)) {
    cgm.errorNYI("getInvokeDestImpl: usesFuncletPads");
    return;
  } else {
    populateCatchHandlers(tryOp);
  }
}
