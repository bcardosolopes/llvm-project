//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Emit Stmt nodes as CIR code.
//
//===----------------------------------------------------------------------===//

#include "CIRGenBuilder.h"
#include "CIRGenCleanup.h"
#include "CIRGenFunction.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/Location.h"
#include "mlir/Support/LLVM.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/StmtOpenACC.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/PrettyStackTrace.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace clang;
using namespace clang::CIRGen;
using namespace cir;

static constexpr llvm::StringLiteral kCaseLikelihoodsAttr =
    "cir.case_likelihoods";

// Returns the raw (pre-getPGOFuncName) instrument-profiling name for an OpenMP
// outlined region, with a per-enclosing-function `.N` disambiguator. The first
// region of a function keeps the bare name (e.g. "f.omp_outlined") to match the
// name LLVM's symbol table would assign; the second and later regions get
// ".1", ".2", ... This mirrors classic clang, where the LLVM module symbol
// table auto-uniquifies the colliding outlined-helper functions and PGO derives
// its name from the already-uniquified function name. CIR computes the PGO name
// eagerly from the AST before any symbol-table uniquification happens, so the
// disambiguator must be applied explicitly here; without it every region in one
// enclosing function shares a single FuncNameVar/counter and the per-region
// profile data is corrupted.
static std::string getOpenMPOutlinedPGOHelperName(CIRGenModule &cgm,
                                                  llvm::StringRef parentName,
                                                  unsigned idx) {
  const bool isGPU = cgm.getTarget().getTriple().isGPU();
  std::string base = (parentName + (isGPU ? "_" : ".") + "omp_outlined").str();
  if (idx == 0)
    return base;
  return (base + "." + llvm::Twine(idx)).str();
}

CIRGenPGO::RegionCountersScope
CIRGenFunction::enterOpenMPHelperRegionCounters(const CapturedStmt &s) {
  auto parentFn = mlir::dyn_cast<cir::FuncOp>(curFn);
  assert(parentFn &&
         "OpenMP captured helper requires an enclosing CIR function");

  llvm::StringRef parentName = parentFn.getSymName();
  if (auto attr =
          parentFn->getAttrOfType<mlir::StringAttr>("cir.frontend_pgo_name"))
    parentName = attr.getValue();

  // The PGO name attribute is normally set (via getOpenMPHelperPGOName) before
  // this region's counters are entered, allocating the index for this region.
  // Look it up; if for some path it was not allocated yet, allocate it now so
  // the counter name still gets a unique disambiguator.
  auto it = openMPOutlinedRegionIndex.find(&s);
  unsigned idx;
  if (it != openMPOutlinedRegionIndex.end()) {
    idx = it->second;
  } else {
    idx = openMPOutlinedPGOCounter[parentFn.getOperation()]++;
    openMPOutlinedRegionIndex[&s] = idx;
  }

  return PGO->enterRegionCounters(
      GlobalDecl(s.getCapturedDecl()),
      getOpenMPOutlinedPGOHelperName(cgm, parentName, idx),
      llvm::GlobalValue::InternalLinkage);
}

std::string CIRGenFunction::getOpenMPHelperPGOName(const CapturedStmt &s) {
  auto parentFn = mlir::dyn_cast<cir::FuncOp>(curFn);
  assert(parentFn &&
         "OpenMP captured helper requires an enclosing CIR function");

  llvm::StringRef parentName = parentFn.getSymName();
  if (auto attr =
          parentFn->getAttrOfType<mlir::StringAttr>("cir.frontend_pgo_name"))
    parentName = attr.getValue();

  // Allocate-or-fetch the per-region index for this enclosing function. This is
  // the first of the two helpers invoked per region (the attribute is set at
  // omp.ParallelOp creation, before the region body's counters are entered), so
  // it owns allocation; enterOpenMPHelperRegionCounters reuses the same index.
  auto [it, inserted] = openMPOutlinedRegionIndex.try_emplace(&s, 0);
  if (inserted)
    it->second = openMPOutlinedPGOCounter[parentFn.getOperation()]++;
  unsigned idx = it->second;

  return llvm::getPGOFuncName(
      getOpenMPOutlinedPGOHelperName(cgm, parentName, idx),
      llvm::GlobalValue::InternalLinkage, cgm.getCodeGenOpts().MainFileName,
      llvm::IndexedInstrProf::Version);
}

static std::optional<int32_t>
getHLSLControlFlowHint(llvm::ArrayRef<const Attr *> attrs) {
  std::optional<int32_t> hint;
  for (const Attr *attr : attrs) {
    if (attr->getKind() != attr::HLSLControlFlowHint)
      continue;
    switch (cast<HLSLControlFlowHintAttr>(attr)->getSemanticSpelling()) {
    case HLSLControlFlowHintAttr::Spelling::Microsoft_branch:
      hint = 1;
      break;
    case HLSLControlFlowHintAttr::Spelling::Microsoft_flatten:
      hint = 2;
      break;
    case HLSLControlFlowHintAttr::SpellingNotCalculated:
      break;
    }
  }
  return hint;
}

static mlir::IntegerAttr getCaseLikelihoodAttr(mlir::MLIRContext *ctx,
                                               Stmt::Likelihood lh) {
  return mlir::IntegerAttr::get(mlir::IntegerType::get(ctx, 32),
                                static_cast<int32_t>(lh));
}

static mlir::ArrayAttr
getCaseLikelihoodsAttr(mlir::MLIRContext *ctx,
                       llvm::ArrayRef<Stmt::Likelihood> likelihoods) {
  llvm::SmallVector<mlir::Attribute> attrs;
  attrs.reserve(likelihoods.size());
  for (Stmt::Likelihood lh : likelihoods)
    attrs.push_back(getCaseLikelihoodAttr(ctx, lh));
  return mlir::ArrayAttr::get(ctx, attrs);
}

static void terminateBody(CIRGenBuilderTy &builder, mlir::Region &r,
                          mlir::Location loc);
static cir::YieldOp getSingleYieldTerminator(mlir::Region &region);

static mlir::Value castPointerForReturn(CIRGenBuilderTy &builder,
                                        mlir::Location loc,
                                        mlir::Value value,
                                        mlir::Type targetType) {
  if (value.getType() == targetType)
    return value;

  auto srcPtrTy = mlir::dyn_cast<cir::PointerType>(value.getType());
  auto dstPtrTy = mlir::dyn_cast<cir::PointerType>(targetType);
  if (!srcPtrTy || !dstPtrTy)
    return value;

  if (srcPtrTy.getAddrSpace() != dstPtrTy.getAddrSpace()) {
    auto samePointeeDstAS =
        builder.getPointerTo(srcPtrTy.getPointee(), dstPtrTy.getAddrSpace());
    value = builder.createAddrSpaceCast(loc, value, samePointeeDstAS);
    if (value.getType() == targetType)
      return value;
  }

  return builder.createBitcast(loc, value, targetType);
}

namespace {

static mlir::Type getObjCFragileExceptionDataTy(CIRGenFunction &cgf) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
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

static cir::FuncOp getObjCFragileSetJmpFn(CIRGenFunction &cgf) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
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

struct ObjCBareRethrowInfo {
  llvm::StringRef fnName;
  bool takesObject;
  bool returnsObject;
};

static ObjCBareRethrowInfo getObjCBareRethrowInfo(CIRGenFunction &cgf) {
  const ObjCRuntime runtime = cgf.getLangOpts().ObjCRuntime;
  const llvm::Triple &triple = cgf.getTarget().getTriple();

  switch (runtime.getKind()) {
  case ObjCRuntime::FragileMacOSX:
    llvm_unreachable("fragile Objective-C runtime uses separate EH lowering");
  case ObjCRuntime::MacOSX:
  case ObjCRuntime::iOS:
  case ObjCRuntime::WatchOS:
    return {"objc_exception_rethrow", /*takesObject=*/false,
            /*returnsObject=*/false};
  case ObjCRuntime::GNUstep:
    if (triple.isOSCygMing() && runtime.getVersion() >= VersionTuple(2))
      return {"__cxa_rethrow", /*takesObject=*/false,
              /*returnsObject=*/true};
    if (triple.isWindowsMSVCEnvironment())
      return {"objc_exception_rethrow", /*takesObject=*/false,
              /*returnsObject=*/false};
    return {"objc_exception_throw", /*takesObject=*/true,
            /*returnsObject=*/false};
  case ObjCRuntime::GCC:
  case ObjCRuntime::ObjFW:
    return {"objc_exception_throw", /*takesObject=*/true,
            /*returnsObject=*/false};
  }

  llvm_unreachable("bad Objective-C runtime kind");
}

static bool isCurrentIPKnownUnreachable(CIRGenFunction &cgf) {
  mlir::Block *block = cgf.getBuilder().getInsertionBlock();
  if (!block)
    return true;
  return block->hasNoPredecessors() && block != &block->getParent()->front() &&
         block->empty();
}

static void emitFragileHazardAsm(CIRGenFunction &cgf, mlir::Location loc,
                                 llvm::ArrayRef<mlir::Value> locals,
                                 llvm::StringRef memConstraint,
                                 bool outputs) {
  if (locals.empty())
    return;

  CIRGenBuilderTy &builder = cgf.getBuilder();
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
  explicit FragileObjCHazards(CIRGenFunction &cgf) : cgf(cgf) {
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

  CIRGenFunction &cgf;
  llvm::SmallVector<mlir::Value, 8> locals;
  llvm::SmallPtrSet<mlir::Block *, 8> blocksBeforeTry;
};

struct CallFragileSyncExit final : EHScopeStack::Cleanup {
  mlir::Location loc;
  cir::FuncOp syncExitFn;
  cir::FuncOp tryExitFn;
  Address syncArgAddr;
  Address callTryExitAddr;
  Address exceptionDataAddr;
  bool releaseLock;

  CallFragileSyncExit(mlir::Location loc, cir::FuncOp syncExitFn,
                      cir::FuncOp tryExitFn, Address syncArgAddr,
                      Address callTryExitAddr, Address exceptionDataAddr,
                      bool releaseLock)
      : loc(loc), syncExitFn(syncExitFn), tryExitFn(tryExitFn),
        syncArgAddr(syncArgAddr), callTryExitAddr(callTryExitAddr),
        exceptionDataAddr(exceptionDataAddr), releaseLock(releaseLock) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    (void)flags;
    CIRGenBuilderTy &builder = cgf.getBuilder();
    mlir::Value shouldCallTryExit = builder.createLoad(loc, callTryExitAddr);

    mlir::Region *region = builder.getBlock()->getParent();
    auto *callExitBlock = new mlir::Block();
    auto *contBlock = new mlir::Block();
    region->push_back(callExitBlock);
    region->push_back(contBlock);

    cir::BrCondOp::create(builder, loc, shouldCallTryExit, callExitBlock,
                          contBlock);

    builder.setInsertionPointToStart(callExitBlock);
    mlir::Value exceptionData = exceptionDataAddr.getPointer();
    if (exceptionData.getType() != builder.getVoidPtrTy())
      exceptionData = builder.createBitcast(exceptionData, builder.getVoidPtrTy());
    cir::CallOp tryExitCall = builder.createCallOp(loc, tryExitFn, {exceptionData});
    tryExitCall.setNothrow(true);
    cir::BrOp::create(builder, loc, contBlock);

    builder.setInsertionPointToStart(contBlock);
    mlir::Value syncArg = builder.createLoad(loc, syncArgAddr);
    cir::CallOp syncExitCall = builder.createCallOp(loc, syncExitFn, {syncArg});
    syncExitCall.setNothrow(true);
    if (releaseLock)
      cgf.emitARCRelease(syncArg, loc);
  }
};

static const CallExpr *
getSimpleDeferredCleanupDispatchCandidateCall(const Stmt *body) {
  const Stmt *simpleBody = body;
  while (const auto *compound = dyn_cast<CompoundStmt>(simpleBody)) {
    if (compound->size() != 1)
      return nullptr;
    simpleBody = *compound->body_begin();
  }

  const auto *expr = dyn_cast<Expr>(simpleBody);
  if (!expr)
    return nullptr;

  for (;;) {
    expr = expr->IgnoreImplicitAsWritten();
    if (const auto *cleanups = dyn_cast<ExprWithCleanups>(expr)) {
      expr = cleanups->getSubExpr();
      continue;
    }
    break;
  }

  const auto *call = dyn_cast<CallExpr>(expr->IgnoreParenImpCasts());
  if (!call || !call->getType()->isVoidType() || call->getNumArgs() > 1)
    return nullptr;

  const FunctionDecl *callee = call->getDirectCallee();
  if (!callee || callee->getBuiltinID())
    return nullptr;

  if (!isa<DeclRefExpr>(call->getCallee()->IgnoreParenImpCasts()))
    return nullptr;

  return call;
}

struct EmitDeferredStatement final : EHScopeStack::Cleanup {
  const DeferStmt &stmt;

  explicit EmitDeferredStatement(const DeferStmt *stmt) : stmt(*stmt) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    (void)flags;

    struct DeferredCleanupDispatchCandidateScope {
      CIRGenFunction &cgf;
      const CallExpr *call;

      explicit DeferredCleanupDispatchCandidateScope(CIRGenFunction &cgf,
                                                     const CallExpr *call)
          : cgf(cgf), call(call) {
        if (call)
          cgf.pushDeferredCleanupDispatchCandidate(call);
      }
      ~DeferredCleanupDispatchCandidateScope() {
        if (call)
          cgf.popDeferredCleanupDispatchCandidate(call);
      }
    };

    mlir::Region *parentRegion = nullptr;
    if (mlir::Block *block = cgf.getBuilder().getInsertionBlock())
      parentRegion = block->getParent();

    // Deferred cleanups are re-emitted along multiple exit edges. Restore the
    // enclosing VLA-size cache after each emission so VLA locals inside the
    // deferred body recompute their bounds on the next cleanup path.
    CIRGenFunction::VlaSizeMapRevertingRAII vlaSizeMapGuard(cgf);
    llvm::SmallVector<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>, 8>
        declMapGuards;
    llvm::SmallVector<const Stmt *, 8> worklist;
    worklist.push_back(stmt.getBody());
    while (!worklist.empty()) {
      const Stmt *cur = worklist.pop_back_val();
      if (!cur)
        continue;
      if (const auto *declStmt = dyn_cast<DeclStmt>(cur))
        for (const Decl *decl : declStmt->decls())
          if (const auto *varDecl = dyn_cast<VarDecl>(decl))
            declMapGuards.push_back(
                std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(cgf,
                                                                       varDecl));
      for (const Stmt *child : cur->children())
        worklist.push_back(child);
    }

    DeferredCleanupDispatchCandidateScope candidateScope(
        cgf, getSimpleDeferredCleanupDispatchCandidateCall(stmt.getBody()));
    if (cgf.emitStmt(stmt.getBody(), /*useCurrentScope=*/false).failed()) {
      cgf.getCIRGenModule().errorNYI(stmt.getSourceRange(),
                                    "emit deferred cleanup body");
      if (!cgf.haveInsertPoint() && parentRegion)
        cgf.getBuilder().createBlock(parentRegion);
      return;
    }

    if (!cgf.haveInsertPoint() && parentRegion)
      cgf.getBuilder().createBlock(parentRegion);
  }
};

} // namespace

static bool previousBlockEndedWithExitLikeTerminator(CIRGenFunction &cgf) {
  mlir::Block *block = cgf.getBuilder().getInsertionBlock();
  if (!block || block->isEntryBlock() || !block->hasNoPredecessors())
    return false;

  mlir::Region *region = block->getParent();
  if (!region)
    return false;

  auto it = block->getIterator();
  if (it == region->begin())
    return false;

  mlir::Block *prevBlock = &*std::prev(it);
  if (prevBlock->empty())
    return false;

  return mlir::isa<cir::BreakOp, cir::ContinueOp, cir::GotoOp, cir::ReturnOp,
                   cir::UnreachableOp>(prevBlock->back());
}

static bool sourceLocationWithinRange(const SourceManager &sm,
                                      SourceLocation loc, SourceRange range) {
  return range.isValid() &&
         !sm.isBeforeInTranslationUnit(loc, range.getBegin()) &&
         sm.isBeforeInTranslationUnit(loc, range.getEnd());
}

static bool normalExitLeavesTryBody(
    const CIRGenFunction::LexicalScope *currentScope,
    const CIRGenFunction::LexicalScope *targetScope,
    const CIRGenFunction::LexicalScope *tryBodyScope) {
  for (const CIRGenFunction::LexicalScope *scope = currentScope;
       scope && scope != targetScope; scope = scope->getParentScope()) {
    if (scope == tryBodyScope)
      return true;
  }

  return false;
}

static Stmt::Likelihood
getFirstCaseLikelihood(llvm::ArrayRef<const Attr *> attrs, const Stmt *subStmt) {
  Stmt::Likelihood fromAttrs = Stmt::getLikelihood(attrs);
  if (fromAttrs != Stmt::LH_None)
    return fromAttrs;
  return Stmt::LH_None;
}

static mlir::LogicalResult emitStmtWithResult(CIRGenFunction &cgf,
                                              const Stmt *exprResult,
                                              AggValueSlot slot,
                                              Address *lastValue) {
  // We have to special case labels here. They are statements, but when put
  // at the end of a statement expression, they yield the value of their
  // subexpression. Handle this by walking through all labels we encounter,
  // emitting them before we evaluate the subexpr.
  // Similar issues arise for attributed statements.
  while (!isa<Expr>(exprResult)) {
    if (const auto *ls = dyn_cast<LabelStmt>(exprResult)) {
      if (cgf.emitLabel(*ls->getDecl()).failed())
        return mlir::failure();
      exprResult = ls->getSubStmt();
    } else if (const auto *as = dyn_cast<AttributedStmt>(exprResult)) {
      // FIXME: Update this if we ever have attributes that affect the
      // semantics of an expression.
      exprResult = as->getSubStmt();
    } else {
      llvm_unreachable("Unknown value statement");
    }
  }

  // OGCG calls EnsureInsertPoint() here before evaluating the last
  // expression. CIR labels and scopes keep a valid insertion point.

  const Expr *e = cast<Expr>(exprResult);
  QualType exprTy = e->getType();
  if (cgf.hasAggregateEvaluationKind(exprTy)) {
    cgf.emitAggExpr(e, slot);
  } else {
    // We can't return an RValue here because there might be cleanups at
    // the end of the StmtExpr.  Because of that, we have to emit the result
    // here into a temporary alloca.
    cgf.emitAnyExprToMem(e, *lastValue, Qualifiers(),
                         /*IsInit*/ false);
  }

  return mlir::success();
}

void CIRGenFunction::pushActiveObjCFinallyScope(const Stmt *body,
                                                SourceRange tryBodyRange,
                                                LexicalScope *tryBodyScope) {
  activeObjCFinallyScopes.push_back({body, tryBodyRange, tryBodyScope});
}

void CIRGenFunction::popActiveObjCFinallyScope() {
  assert(!activeObjCFinallyScopes.empty() &&
         "missing active Objective-C @finally scope");
  activeObjCFinallyScopes.pop_back();
}

void CIRGenFunction::popActiveObjCFinallyScopeIfPresent(
    const Stmt *body, const LexicalScope *tryBodyScope) {
  if (activeObjCFinallyScopes.empty())
    return;

  const ActiveObjCFinallyScope &activeFinally = activeObjCFinallyScopes.back();
  if (activeFinally.body == body &&
      activeFinally.tryBodyScope == tryBodyScope)
    activeObjCFinallyScopes.pop_back();
}

mlir::LogicalResult
CIRGenFunction::emitActiveObjCFinallyBodyForNormalExit(unsigned index,
                                                       bool &overrodeExit) {
  assert(index < activeObjCFinallyScopes.size() &&
         "invalid active Objective-C @finally index");

  ActiveObjCFinallyScope activeFinally = activeObjCFinallyScopes[index];
  activeObjCFinallyScopes.erase(activeObjCFinallyScopes.begin() + index);

  if (emitObjCFinallyBody(activeFinally.body, /*useCurrentScope=*/false)
          .failed())
    return mlir::failure();

  overrodeExit =
      !haveInsertPoint() || previousBlockEndedWithExitLikeTerminator(*this);
  return mlir::success();
}

mlir::LogicalResult
CIRGenFunction::emitActiveObjCFinallyBodiesForReturn(bool &overrodeExit) {
  overrodeExit = false;
  for (int i = static_cast<int>(activeObjCFinallyScopes.size()) - 1; i >= 0;
       --i) {
    if (emitActiveObjCFinallyBodyForNormalExit(i, overrodeExit).failed())
      return mlir::failure();
    if (overrodeExit)
      return mlir::success();
  }

  return mlir::success();
}

mlir::LogicalResult
CIRGenFunction::emitActiveObjCFinallyBodiesForGoto(const LabelDecl *label,
                                                   bool &overrodeExit) {
  overrodeExit = false;
  SourceLocation labelLoc = label->getLocation();
  const SourceManager &sm = getContext().getSourceManager();

  for (int i = static_cast<int>(activeObjCFinallyScopes.size()) - 1; i >= 0;
       --i) {
    if (sourceLocationWithinRange(sm, labelLoc,
                                  activeObjCFinallyScopes[i].tryBodyRange))
      continue;

    if (emitActiveObjCFinallyBodyForNormalExit(i, overrodeExit).failed())
      return mlir::failure();
    if (overrodeExit)
      return mlir::success();
  }

  return mlir::success();
}

mlir::LogicalResult CIRGenFunction::emitActiveObjCFinallyBodiesForBreakContinue(
    const LexicalScope *targetScope, bool &overrodeExit) {
  overrodeExit = false;
  if (!targetScope)
    return mlir::success();

  for (int i = static_cast<int>(activeObjCFinallyScopes.size()) - 1; i >= 0;
       --i) {
    if (!normalExitLeavesTryBody(curLexScope, targetScope,
                                 activeObjCFinallyScopes[i].tryBodyScope))
      continue;

    if (emitActiveObjCFinallyBodyForNormalExit(i, overrodeExit).failed())
      return mlir::failure();
    if (overrodeExit)
      return mlir::success();
  }

  return mlir::success();
}

std::string CIRGenFunction::createNamedLoopControlLabel(llvm::StringRef kind) {
  auto fn = cast<cir::FuncOp>(curFn);
  std::string label = fn.getSymName().str();
  label += ".__named_";
  label += kind.str();
  label += ".";
  label += std::to_string(nextNamedLoopControlLabel++);
  return label;
}

CIRGenFunction::NamedLoopControlTarget *
CIRGenFunction::getNamedLoopControlTarget(const clang::Stmt *loopOrSwitch) {
  for (auto it = namedLoopControlStack.rbegin();
       it != namedLoopControlStack.rend(); ++it) {
    if (it->loopOrSwitch == loopOrSwitch)
      return &*it;
  }

  llvm_unreachable("named break/continue target not found");
}

bool CIRGenFunction::isActiveNamedLoopTarget(const clang::Stmt *stmt) const {
  for (const clang::Stmt *target : activeNamedLoopTargetStack) {
    if (target == stmt)
      return true;
  }
  return false;
}

void CIRGenFunction::emitSyntheticLabel(mlir::Location loc,
                                        llvm::StringRef labelName) {
  cir::LabelOp::create(builder, loc, labelName);
}

mlir::LogicalResult CIRGenFunction::emitGotoWithCleanup(
    mlir::Location loc, llvm::StringRef labelName,
    EHScopeStack::stable_iterator targetDepth,
    const LexicalScope *targetScope) {
  if (targetDepth.isValid() && targetDepth != ehStack.stable_begin())
    emitCleanupsForEarlyExit(targetDepth);

  bool finallyOverrodeGoto = false;
  if (emitActiveObjCFinallyBodiesForBreakContinue(targetScope,
                                                  finallyOverrodeGoto)
          .failed())
    return mlir::failure();
  if (finallyOverrodeGoto)
    return mlir::success();

  auto gotoOp = cir::GotoOp::create(builder, loc, labelName);
  addOpToNewSourceAtom(gotoOp);
  builder.createBlock(builder.getBlock()->getParent());
  return mlir::success();
}

/// Return true when a statement is simple enough to be emitted in-place inside
/// a declaration-free compound block without creating a dedicated `cir.scope`.
/// Keep this conservative: statements that can branch, split blocks, or carry
/// nested lexical scopes should keep the explicit scope op.
static bool isScopeElidableStmt(const Stmt *stmt) {
  auto containsExprKind = [](const Stmt *root, auto &&predicate) -> bool {
    if (!root)
      return false;
    llvm::SmallVector<const Stmt *, 8> worklist{root};
    while (!worklist.empty()) {
      const Stmt *cur = worklist.pop_back_val();
      if (!cur)
        continue;
      if (const auto *expr = dyn_cast<Expr>(cur))
        if (predicate(expr))
          return true;
      for (const Stmt *child : cur->children())
        worklist.push_back(child);
    }
    return false;
  };

  if (isa<Expr, NullStmt>(stmt))
    return !containsExprKind(stmt, [](const Expr *expr) {
      return isa<StmtExpr, VAArgExpr>(expr);
    });

  if (const auto *attrStmt = dyn_cast<AttributedStmt>(stmt))
    return isScopeElidableStmt(attrStmt->getSubStmt());

  return false;
}

/// Return whether this compound statement requires a dedicated cir.scope op.
/// We can only elide the scope for declaration-free blocks containing
/// expression-like statements that do not alter control flow shape.
static bool needsCIRScope(const CompoundStmt &s) {
  return llvm::any_of(s.body(), [](const Stmt *stmt) {
    return isa<DeclStmt>(stmt) || !isScopeElidableStmt(stmt);
  });
}

mlir::LogicalResult CIRGenFunction::emitCompoundStmtWithoutScope(
    const CompoundStmt &s, Address *lastValue, AggValueSlot slot) {
  mlir::LogicalResult result = mlir::success();
  const Stmt *exprResult = s.body_back();
  assert((!lastValue || (lastValue && exprResult)) &&
         "If lastValue is not null then the CompoundStmt must have a "
         "StmtExprResult");

  bool reachedUnreachable = false;
  for (const Stmt *curStmt : s.body()) {
    // Skip unreachable statements after a return/break/continue/goto unless
    // the statement contains a label (goto target from reachable code).
    // Matches OGCG's HaveInsertPoint() check. Prevents dead code emission
    // for patterns like: return; static A k;
    if (reachedUnreachable) {
      if (!containsLabel(curStmt))
        continue;
      reachedUnreachable = false;
    }

    const bool saveResult = lastValue && exprResult == curStmt;
    if (saveResult) {
      if (emitStmtWithResult(*this, exprResult, slot, lastValue).failed())
        result = mlir::failure();
    } else {
      if (emitStmt(curStmt, /*useCurrentScope=*/false).failed())
        result = mlir::failure();
    }

    // Check if the statement we just emitted terminated the current execution
    // path. This catches return/break/continue/goto/throw as well as noreturn
    // calls (__builtin_trap, __builtin_unreachable, [[noreturn]] functions).
    // We detect this by checking if the insertion block changed to one with
    // no predecessors (a dead continuation block created after a terminator).
    //
    // Also unwrap compound statements: { { break; } } terminates just like
    // a bare break. This is needed for MCF-style guard macros that expand
    // to: if (check_fails) { { return; } _GuardFallback(); }
    // Without unwrapping, _GuardFallback() would be emitted as reachable.
    const Stmt *termCheck = curStmt;
    while (const auto *cs = dyn_cast<CompoundStmt>(termCheck)) {
      if (cs->body_empty())
        break;
      termCheck = cs->body_back();
    }
    if (isa<ReturnStmt>(termCheck) || isa<BreakStmt>(termCheck) ||
        isa<ContinueStmt>(termCheck) || isa<GotoStmt>(termCheck) ||
        isa<CXXThrowExpr>(termCheck) || isa<ObjCAtThrowStmt>(termCheck))
      reachedUnreachable = true;
    // For if statements where all branches terminate (return, abort,
    // throw, etc.), subsequent code is dead. CIR's structured regions don't
    // create dead merge blocks like OGCG's CFG, so we check the AST.
    else if (const auto *ifStmt = dyn_cast<IfStmt>(curStmt)) {
      if (ifStmt->getElse()) {
        // Helper to check if a branch definitely terminates.
        auto branchTerminates = [](const Stmt *s) -> bool {
          // Unwrap compound statements.
          if (const auto *cs = dyn_cast<CompoundStmt>(s)) {
            if (cs->body_empty())
              return false;
            s = cs->body_back();
          }
          if (isa<ReturnStmt>(s) || isa<CXXThrowExpr>(s) ||
              isa<ObjCAtThrowStmt>(s) ||
              isa<GotoStmt>(s) || isa<BreakStmt>(s) ||
              isa<ContinueStmt>(s))
            return true;
          // Check for calls to noreturn functions (e.g., abort()).
          if (const auto *es = dyn_cast<Expr>(s)) {
            es = es->IgnoreParenImpCasts();
            if (const auto *ce = dyn_cast<CallExpr>(es))
              if (const auto *fd = ce->getDirectCallee())
                if (fd->isNoReturn())
                  return true;
          }
          return false;
        };
        if (branchTerminates(ifStmt->getThen()) &&
            branchTerminates(ifStmt->getElse()))
          reachedUnreachable = true;
      }
    } else if (auto *curBlock = builder.getInsertionBlock()) {
      // For expression-level terminators (noreturn calls, builtins), check
      // if the block has a terminator already. CIR emits cir.unreachable
      // after noreturn calls, which terminates the block. A subsequent
      // createBlock creates a dead continuation block with no predecessors.
      //
      // Skip this check for asm goto: the fallthrough block created by
      // cir.asm.goto has no predecessors at CIRGen time because GotoSolver
      // wires the successors later. It IS reachable. Peel any enclosing
      // label/attributed wrappers first — `back: asm goto(...)` presents as a
      // LabelStmt here, and missing the exemption would wrongly drop the
      // reachable fallthrough continuation (e.g. the following return).
      const Stmt *asmCheck = curStmt;
      while (true) {
        if (const auto *ls = dyn_cast<LabelStmt>(asmCheck))
          asmCheck = ls->getSubStmt();
        else if (const auto *as = dyn_cast<AttributedStmt>(asmCheck))
          asmCheck = as->getSubStmt();
        else
          break;
      }
      bool isAsmGoto = false;
      if (const auto *gs = dyn_cast<GCCAsmStmt>(asmCheck))
        isAsmGoto = gs->isAsmGoto();
      if (!isAsmGoto && curBlock->hasNoPredecessors() &&
          curBlock != &curBlock->getParent()->front() &&
          curBlock->empty())
        reachedUnreachable = true;
    }
  }
  return result;
}

mlir::LogicalResult CIRGenFunction::emitCompoundStmt(const CompoundStmt &s,
                                                     Address *lastValue,
                                                     AggValueSlot slot) {
  PrettyStackTraceLoc crashInfo(getContext().getSourceManager(), s.getLBracLoc(),
                                "CIR generation of compound statement ('{}')");

  // Add local scope to track new declared variables.
  mlir::Location scopeLoc = getLoc(s.getSourceRange());
  SymTableScopeTy varScope(symbolTable);

  // For declaration-free lexical blocks, keep cleanup/source-range tracking via
  // a cleanup-only lexical scope but skip creating a cir.scope op. This avoids
  // emitting one op/region/block per brace scope, reducing scope op pressure in
  // template-heavy translation units.
  bool insideStructuredControlScope =
      curLexScope &&
      (curLexScope->isLoop() || curLexScope->isSwitch() || curLexScope->isTry());
  if (!lastValue && !insideStructuredControlScope && !needsCIRScope(s)) {
    LexicalScope lexScope(*this, scopeLoc, builder.getInsertionBlock());
    lexScope.setClangSourceRange(s.getSourceRange());
    lexScope.setCleanupOnly();
    return emitCompoundStmtWithoutScope(s, lastValue, slot);
  }

  mlir::OpBuilder::InsertPoint scopeInsPt;
  cir::ScopeOp::create(
      builder, scopeLoc,
      [&](mlir::OpBuilder &b, mlir::Type &type, mlir::Location loc) {
        scopeInsPt = b.saveInsertionPoint();
      });
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.restoreInsertionPoint(scopeInsPt);
  LexicalScope lexScope(*this, scopeLoc, builder.getInsertionBlock());
  lexScope.setClangSourceRange(s.getSourceRange());
  return emitCompoundStmtWithoutScope(s, lastValue, slot);
}

void CIRGenFunction::emitStopPoint(const Stmt *s) {
  // OGCG calls DI->EmitLocation(Builder, Loc) here to set the builder's
  // current debug location. CIR handles this differently: every CIR op
  // carries its own mlir::Location (file/line/col), so explicit stop-points
  // are unnecessary. The DIScopeForLLVMFuncOpPass converts these locations
  // to DILocations during lowering.
}

static bool isSimpleStmt(const Stmt *s) {
  switch (s->getStmtClass()) {
  case Stmt::DeclStmtClass:
  case Stmt::CompoundStmtClass:
  case Stmt::GotoStmtClass:
  case Stmt::ContinueStmtClass:
  case Stmt::NullStmtClass:
  case Stmt::LabelStmtClass:
  case Stmt::CaseStmtClass:
  case Stmt::DefaultStmtClass:
  case Stmt::BreakStmtClass:
  case Stmt::ReturnStmtClass:
    return true;
  default:
    return false;
  }
}

// Build CIR for a statement. useCurrentScope should be true if no new scopes
// need to be created when finding a compound statement.
mlir::LogicalResult CIRGenFunction::emitStmt(const Stmt *s,
                                             bool useCurrentScope,
                                             ArrayRef<const Attr *> attrs) {
  // OGCG calls PGO->setCurrentStmt(S) here to track PGO counters per
  // statement. CIR does not currently thread OGCG's PGO state object here.
  // OGCG checks for OpenMPSimd mode to dispatch
  // EmitSimpleOMPExecutableDirective before the main switch.
  // CIR handles OpenMP directives through emitSimpleStmt / stmt dispatch.

  // OGCG calls EmitStopPoint(S) here before dispatching to set the debug
  // stop point for every statement. CIR doesn't need this because every
  // CIR op carries its own mlir::Location. DIScopeForLLVMFuncOpPass
  // converts these locations to DILocations during lowering.

  if (isSimpleStmt(s))
    return emitSimpleStmt(s, useCurrentScope, attrs);

  // OGCG checks !HaveInsertPoint() here and skips unreachable code unless
  // it contains a label. CIR checks for unreachable code in
  // emitCompoundStmtWithoutScope instead, since DeclStmts (handled by
  // emitSimpleStmt above) also need skipping for static variable guards.

  // Mark the dynamic extent of OpenMP directives that OpenMPIRBuilder outlines
  // via llvm::CodeExtractor, so nested emission (e.g. of a typed catch clause,
  // which the outliner cannot handle) can detect it. The guard stays live
  // across the recursive emission of the directive body below.
  std::optional<OutlinedOpenMPRegionRAII> outlinedOpenMPGuard;
  if (const auto *ompDir = dyn_cast<OMPExecutableDirective>(s)) {
    OpenMPDirectiveKind dirKind = ompDir->getDirectiveKind();
    if (isOpenMPParallelDirective(dirKind) ||
        isOpenMPTaskingDirective(dirKind) ||
        isOpenMPTargetExecutionDirective(dirKind) ||
        isOpenMPTeamsDirective(dirKind))
      outlinedOpenMPGuard.emplace(*this);
  }

  switch (s->getStmtClass()) {
  case Stmt::NoStmtClass:
  case Stmt::CXXCatchStmtClass:
  case Stmt::SEHExceptStmtClass:
  case Stmt::SEHFinallyStmtClass:
  case Stmt::MSDependentExistsStmtClass:
    llvm_unreachable("invalid statement class to emit generically");
  case Stmt::BreakStmtClass:
  case Stmt::NullStmtClass:
  case Stmt::CompoundStmtClass:
  case Stmt::ContinueStmtClass:
  case Stmt::DeclStmtClass:
  case Stmt::ReturnStmtClass:
    llvm_unreachable("should have emitted these statements as simple");

#define STMT(Type, Base)
#define ABSTRACT_STMT(Op)
#define EXPR(Type, Base) case Stmt::Type##Class:
#include "clang/AST/StmtNodes.inc"
    {
      assert(builder.getInsertionBlock() &&
             "expression emission must have an insertion point");

      emitIgnoredExpr(cast<Expr>(s));

      // Classic codegen has a check here to see if the emitter created a new
      // block that isn't used (comparing the incoming and outgoing insertion
      // points) and deletes the outgoing block if it's not used. In CIR, we
      // will handle that during the cir.canonicalize pass.
      return mlir::success();
    }
  case Stmt::IfStmtClass:
    return emitIfStmt(cast<IfStmt>(*s), attrs);
  case Stmt::SwitchStmtClass:
    return emitSwitchStmt(cast<SwitchStmt>(*s), attrs);
  case Stmt::ForStmtClass:
    return emitForStmt(cast<ForStmt>(*s), attrs);
  case Stmt::WhileStmtClass:
    return emitWhileStmt(cast<WhileStmt>(*s), attrs);
  case Stmt::DoStmtClass:
    return emitDoStmt(cast<DoStmt>(*s), attrs);
  case Stmt::ObjCForCollectionStmtClass:
    return emitObjCForCollectionStmt(cast<ObjCForCollectionStmt>(*s));
  case Stmt::CXXTryStmtClass:
    return emitCXXTryStmt(cast<CXXTryStmt>(*s));
  case Stmt::CXXForRangeStmtClass:
    return emitCXXForRangeStmt(cast<CXXForRangeStmt>(*s), attrs);
  case Stmt::CoroutineBodyStmtClass:
    return emitCoroutineBody(cast<CoroutineBodyStmt>(*s));
  case Stmt::IndirectGotoStmtClass:
    return emitIndirectGotoStmt(cast<IndirectGotoStmt>(*s));
  case Stmt::CoreturnStmtClass:
    return emitCoreturnStmt(cast<CoreturnStmt>(*s));
  case Stmt::OpenACCComputeConstructClass:
    return emitOpenACCComputeConstruct(cast<OpenACCComputeConstruct>(*s));
  case Stmt::OpenACCLoopConstructClass:
    return emitOpenACCLoopConstruct(cast<OpenACCLoopConstruct>(*s));
  case Stmt::OpenACCCombinedConstructClass:
    return emitOpenACCCombinedConstruct(cast<OpenACCCombinedConstruct>(*s));
  case Stmt::OpenACCDataConstructClass:
    return emitOpenACCDataConstruct(cast<OpenACCDataConstruct>(*s));
  case Stmt::OpenACCEnterDataConstructClass:
    return emitOpenACCEnterDataConstruct(cast<OpenACCEnterDataConstruct>(*s));
  case Stmt::OpenACCExitDataConstructClass:
    return emitOpenACCExitDataConstruct(cast<OpenACCExitDataConstruct>(*s));
  case Stmt::OpenACCHostDataConstructClass:
    return emitOpenACCHostDataConstruct(cast<OpenACCHostDataConstruct>(*s));
  case Stmt::OpenACCWaitConstructClass:
    return emitOpenACCWaitConstruct(cast<OpenACCWaitConstruct>(*s));
  case Stmt::OpenACCInitConstructClass:
    return emitOpenACCInitConstruct(cast<OpenACCInitConstruct>(*s));
  case Stmt::OpenACCShutdownConstructClass:
    return emitOpenACCShutdownConstruct(cast<OpenACCShutdownConstruct>(*s));
  case Stmt::OpenACCSetConstructClass:
    return emitOpenACCSetConstruct(cast<OpenACCSetConstruct>(*s));
  case Stmt::OpenACCUpdateConstructClass:
    return emitOpenACCUpdateConstruct(cast<OpenACCUpdateConstruct>(*s));
  case Stmt::OpenACCCacheConstructClass:
    return emitOpenACCCacheConstruct(cast<OpenACCCacheConstruct>(*s));
  case Stmt::OpenACCAtomicConstructClass:
    return emitOpenACCAtomicConstruct(cast<OpenACCAtomicConstruct>(*s));
  case Stmt::GCCAsmStmtClass:
  case Stmt::MSAsmStmtClass:
    return emitAsmStmt(cast<AsmStmt>(*s));
  case Stmt::OMPScopeDirectiveClass:
    return emitOMPScopeDirective(cast<OMPScopeDirective>(*s));
  case Stmt::OMPErrorDirectiveClass:
    return emitOMPErrorDirective(cast<OMPErrorDirective>(*s));
  case Stmt::OMPParallelDirectiveClass:
    return emitOMPParallelDirective(cast<OMPParallelDirective>(*s));
  case Stmt::OMPTaskwaitDirectiveClass:
    return emitOMPTaskwaitDirective(cast<OMPTaskwaitDirective>(*s));
  case Stmt::OMPTaskyieldDirectiveClass:
    return emitOMPTaskyieldDirective(cast<OMPTaskyieldDirective>(*s));
  case Stmt::OMPBarrierDirectiveClass:
    return emitOMPBarrierDirective(cast<OMPBarrierDirective>(*s));
  case Stmt::OMPMetaDirectiveClass:
    return emitOMPMetaDirective(cast<OMPMetaDirective>(*s));
  case Stmt::OMPCanonicalLoopClass:
    return emitOMPCanonicalLoop(cast<OMPCanonicalLoop>(*s));
  case Stmt::OMPSimdDirectiveClass:
    return emitOMPSimdDirective(cast<OMPSimdDirective>(*s));
  case Stmt::OMPTileDirectiveClass:
    return emitOMPTileDirective(cast<OMPTileDirective>(*s));
  case Stmt::OMPUnrollDirectiveClass:
    return emitOMPUnrollDirective(cast<OMPUnrollDirective>(*s));
  case Stmt::OMPFuseDirectiveClass:
    return emitOMPFuseDirective(cast<OMPFuseDirective>(*s));
  case Stmt::OMPForDirectiveClass:
    return emitOMPForDirective(cast<OMPForDirective>(*s));
  case Stmt::OMPForSimdDirectiveClass:
    return emitOMPForSimdDirective(cast<OMPForSimdDirective>(*s));
  case Stmt::OMPSectionsDirectiveClass:
    return emitOMPSectionsDirective(cast<OMPSectionsDirective>(*s));
  case Stmt::OMPSectionDirectiveClass:
    return emitOMPSectionDirective(cast<OMPSectionDirective>(*s));
  case Stmt::OMPSingleDirectiveClass:
    return emitOMPSingleDirective(cast<OMPSingleDirective>(*s));
  case Stmt::OMPMasterDirectiveClass:
    return emitOMPMasterDirective(cast<OMPMasterDirective>(*s));
  case Stmt::OMPCriticalDirectiveClass:
    return emitOMPCriticalDirective(cast<OMPCriticalDirective>(*s));
  case Stmt::OMPParallelForDirectiveClass:
    return emitOMPParallelForDirective(cast<OMPParallelForDirective>(*s));
  case Stmt::OMPParallelForSimdDirectiveClass:
    return emitOMPParallelForSimdDirective(
        cast<OMPParallelForSimdDirective>(*s));
  case Stmt::OMPParallelMasterDirectiveClass:
    return emitOMPParallelMasterDirective(cast<OMPParallelMasterDirective>(*s));
  case Stmt::OMPParallelSectionsDirectiveClass:
    return emitOMPParallelSectionsDirective(
        cast<OMPParallelSectionsDirective>(*s));
  case Stmt::OMPTaskDirectiveClass:
    return emitOMPTaskDirective(cast<OMPTaskDirective>(*s));
  case Stmt::OMPTaskgroupDirectiveClass:
    return emitOMPTaskgroupDirective(cast<OMPTaskgroupDirective>(*s));
  case Stmt::OMPFlushDirectiveClass:
    return emitOMPFlushDirective(cast<OMPFlushDirective>(*s));
  case Stmt::OMPDepobjDirectiveClass:
    return emitOMPDepobjDirective(cast<OMPDepobjDirective>(*s));
  case Stmt::OMPScanDirectiveClass:
    return emitOMPScanDirective(cast<OMPScanDirective>(*s));
  case Stmt::OMPOrderedDirectiveClass:
    return emitOMPOrderedDirective(cast<OMPOrderedDirective>(*s));
  case Stmt::OMPAtomicDirectiveClass:
    return emitOMPAtomicDirective(cast<OMPAtomicDirective>(*s));
  case Stmt::OMPTargetDirectiveClass:
    return emitOMPTargetDirective(cast<OMPTargetDirective>(*s));
  case Stmt::OMPTeamsDirectiveClass:
    return emitOMPTeamsDirective(cast<OMPTeamsDirective>(*s));
  case Stmt::OMPCancellationPointDirectiveClass:
    return emitOMPCancellationPointDirective(
        cast<OMPCancellationPointDirective>(*s));
  case Stmt::OMPCancelDirectiveClass:
    return emitOMPCancelDirective(cast<OMPCancelDirective>(*s));
  case Stmt::OMPTargetDataDirectiveClass:
    return emitOMPTargetDataDirective(cast<OMPTargetDataDirective>(*s));
  case Stmt::OMPTargetEnterDataDirectiveClass:
    return emitOMPTargetEnterDataDirective(
        cast<OMPTargetEnterDataDirective>(*s));
  case Stmt::OMPTargetExitDataDirectiveClass:
    return emitOMPTargetExitDataDirective(cast<OMPTargetExitDataDirective>(*s));
  case Stmt::OMPTargetParallelDirectiveClass:
    return emitOMPTargetParallelDirective(cast<OMPTargetParallelDirective>(*s));
  case Stmt::OMPTargetParallelForDirectiveClass:
    return emitOMPTargetParallelForDirective(
        cast<OMPTargetParallelForDirective>(*s));
  case Stmt::OMPTaskLoopDirectiveClass:
    return emitOMPTaskLoopDirective(cast<OMPTaskLoopDirective>(*s));
  case Stmt::OMPTaskLoopSimdDirectiveClass:
    return emitOMPTaskLoopSimdDirective(cast<OMPTaskLoopSimdDirective>(*s));
  case Stmt::OMPMaskedTaskLoopDirectiveClass:
    return emitOMPMaskedTaskLoopDirective(cast<OMPMaskedTaskLoopDirective>(*s));
  case Stmt::OMPMaskedTaskLoopSimdDirectiveClass:
    return emitOMPMaskedTaskLoopSimdDirective(
        cast<OMPMaskedTaskLoopSimdDirective>(*s));
  case Stmt::OMPMasterTaskLoopDirectiveClass:
    return emitOMPMasterTaskLoopDirective(cast<OMPMasterTaskLoopDirective>(*s));
  case Stmt::OMPMasterTaskLoopSimdDirectiveClass:
    return emitOMPMasterTaskLoopSimdDirective(
        cast<OMPMasterTaskLoopSimdDirective>(*s));
  case Stmt::OMPParallelGenericLoopDirectiveClass:
    return emitOMPParallelGenericLoopDirective(
        cast<OMPParallelGenericLoopDirective>(*s));
  case Stmt::OMPParallelMaskedDirectiveClass:
    return emitOMPParallelMaskedDirective(cast<OMPParallelMaskedDirective>(*s));
  case Stmt::OMPParallelMaskedTaskLoopDirectiveClass:
    return emitOMPParallelMaskedTaskLoopDirective(
        cast<OMPParallelMaskedTaskLoopDirective>(*s));
  case Stmt::OMPParallelMaskedTaskLoopSimdDirectiveClass:
    return emitOMPParallelMaskedTaskLoopSimdDirective(
        cast<OMPParallelMaskedTaskLoopSimdDirective>(*s));
  case Stmt::OMPParallelMasterTaskLoopDirectiveClass:
    return emitOMPParallelMasterTaskLoopDirective(
        cast<OMPParallelMasterTaskLoopDirective>(*s));
  case Stmt::OMPParallelMasterTaskLoopSimdDirectiveClass:
    return emitOMPParallelMasterTaskLoopSimdDirective(
        cast<OMPParallelMasterTaskLoopSimdDirective>(*s));
  case Stmt::OMPDistributeDirectiveClass:
    return emitOMPDistributeDirective(cast<OMPDistributeDirective>(*s));
  case Stmt::OMPDistributeParallelForDirectiveClass:
    return emitOMPDistributeParallelForDirective(
        cast<OMPDistributeParallelForDirective>(*s));
  case Stmt::OMPDistributeParallelForSimdDirectiveClass:
    return emitOMPDistributeParallelForSimdDirective(
        cast<OMPDistributeParallelForSimdDirective>(*s));
  case Stmt::OMPDistributeSimdDirectiveClass:
    return emitOMPDistributeSimdDirective(cast<OMPDistributeSimdDirective>(*s));
  case Stmt::OMPTargetParallelGenericLoopDirectiveClass:
    return emitOMPTargetParallelGenericLoopDirective(
        cast<OMPTargetParallelGenericLoopDirective>(*s));
  case Stmt::OMPTargetParallelForSimdDirectiveClass:
    return emitOMPTargetParallelForSimdDirective(
        cast<OMPTargetParallelForSimdDirective>(*s));
  case Stmt::OMPTargetSimdDirectiveClass:
    return emitOMPTargetSimdDirective(cast<OMPTargetSimdDirective>(*s));
  case Stmt::OMPTargetTeamsGenericLoopDirectiveClass:
    return emitOMPTargetTeamsGenericLoopDirective(
        cast<OMPTargetTeamsGenericLoopDirective>(*s));
  case Stmt::OMPTargetUpdateDirectiveClass:
    return emitOMPTargetUpdateDirective(cast<OMPTargetUpdateDirective>(*s));
  case Stmt::OMPTeamsDistributeDirectiveClass:
    return emitOMPTeamsDistributeDirective(
        cast<OMPTeamsDistributeDirective>(*s));
  case Stmt::OMPTeamsDistributeSimdDirectiveClass:
    return emitOMPTeamsDistributeSimdDirective(
        cast<OMPTeamsDistributeSimdDirective>(*s));
  case Stmt::OMPTeamsDistributeParallelForSimdDirectiveClass:
    return emitOMPTeamsDistributeParallelForSimdDirective(
        cast<OMPTeamsDistributeParallelForSimdDirective>(*s));
  case Stmt::OMPTeamsDistributeParallelForDirectiveClass:
    return emitOMPTeamsDistributeParallelForDirective(
        cast<OMPTeamsDistributeParallelForDirective>(*s));
  case Stmt::OMPTeamsGenericLoopDirectiveClass:
    return emitOMPTeamsGenericLoopDirective(
        cast<OMPTeamsGenericLoopDirective>(*s));
  case Stmt::OMPTargetTeamsDirectiveClass:
    return emitOMPTargetTeamsDirective(cast<OMPTargetTeamsDirective>(*s));
  case Stmt::OMPTargetTeamsDistributeDirectiveClass:
    return emitOMPTargetTeamsDistributeDirective(
        cast<OMPTargetTeamsDistributeDirective>(*s));
  case Stmt::OMPTargetTeamsDistributeParallelForDirectiveClass:
    return emitOMPTargetTeamsDistributeParallelForDirective(
        cast<OMPTargetTeamsDistributeParallelForDirective>(*s));
  case Stmt::OMPTargetTeamsDistributeParallelForSimdDirectiveClass:
    return emitOMPTargetTeamsDistributeParallelForSimdDirective(
        cast<OMPTargetTeamsDistributeParallelForSimdDirective>(*s));
  case Stmt::OMPTargetTeamsDistributeSimdDirectiveClass:
    return emitOMPTargetTeamsDistributeSimdDirective(
        cast<OMPTargetTeamsDistributeSimdDirective>(*s));
  case Stmt::OMPInteropDirectiveClass:
    return emitOMPInteropDirective(cast<OMPInteropDirective>(*s));
  case Stmt::OMPDispatchDirectiveClass:
    return emitOMPDispatchDirective(cast<OMPDispatchDirective>(*s));
  case Stmt::OMPGenericLoopDirectiveClass:
    return emitOMPGenericLoopDirective(cast<OMPGenericLoopDirective>(*s));
  case Stmt::OMPReverseDirectiveClass:
    return emitOMPReverseDirective(cast<OMPReverseDirective>(*s));
  case Stmt::OMPInterchangeDirectiveClass:
    return emitOMPInterchangeDirective(cast<OMPInterchangeDirective>(*s));
  case Stmt::OMPAssumeDirectiveClass:
    return emitOMPAssumeDirective(cast<OMPAssumeDirective>(*s));
  case Stmt::OMPMaskedDirectiveClass:
    return emitOMPMaskedDirective(cast<OMPMaskedDirective>(*s));
  case Stmt::OMPStripeDirectiveClass:
    return emitOMPStripeDirective(cast<OMPStripeDirective>(*s));
  case Stmt::AttributedStmtClass:
    return emitAttributedStmt(cast<AttributedStmt>(*s));
  case Stmt::ObjCAutoreleasePoolStmtClass:
    return emitObjCAutoreleasePoolStmt(cast<ObjCAutoreleasePoolStmt>(*s));
  case Stmt::ObjCAtTryStmtClass:
    return emitObjCAtTryStmt(cast<ObjCAtTryStmt>(*s));
  case Stmt::ObjCAtThrowStmtClass:
    return emitObjCAtThrowStmt(cast<ObjCAtThrowStmt>(*s));
  case Stmt::ObjCAtSynchronizedStmtClass:
    return emitObjCAtSynchronizedStmt(cast<ObjCAtSynchronizedStmt>(*s));
  case Stmt::DeferStmtClass:
    return emitDeferStmt(cast<DeferStmt>(*s));
  case Stmt::CapturedStmtClass: {
    const CapturedStmt &cs = cast<CapturedStmt>(*s);
    return emitCapturedStmt(cs, cs.getCapturedRegionKind());
  }
  case Stmt::LabelStmtClass:
  case Stmt::GotoStmtClass:
  case Stmt::DefaultStmtClass:
  case Stmt::CaseStmtClass:
  case Stmt::SEHLeaveStmtClass:
  case Stmt::SYCLKernelCallStmtClass:
  case Stmt::SEHTryStmtClass:
  case Stmt::ObjCAtCatchStmtClass:
  case Stmt::ObjCAtFinallyStmtClass:
    cgm.errorNYI(s->getSourceRange(),
                 std::string("emitStmt: ") + s->getStmtClassName());
    return mlir::failure();
  }

  llvm_unreachable("Unexpected statement class");
}

mlir::LogicalResult CIRGenFunction::emitSimpleStmt(
    const Stmt *s, bool useCurrentScope, llvm::ArrayRef<const Attr *> attrs) {
  switch (s->getStmtClass()) {
  default:
    return mlir::failure();
  case Stmt::DeclStmtClass:
    return emitDeclStmt(cast<DeclStmt>(*s));
  case Stmt::CompoundStmtClass:
    if (useCurrentScope)
      return emitCompoundStmtWithoutScope(cast<CompoundStmt>(*s));
    return emitCompoundStmt(cast<CompoundStmt>(*s));
  case Stmt::GotoStmtClass:
    return emitGotoStmt(cast<GotoStmt>(*s));
  case Stmt::ContinueStmtClass:
    return emitContinueStmt(cast<ContinueStmt>(*s));

  // NullStmt doesn't need any handling, but we need to say we handled it.
  case Stmt::NullStmtClass:
    break;

  case Stmt::LabelStmtClass:
    return emitLabelStmt(cast<LabelStmt>(*s));
  case Stmt::CaseStmtClass:
  case Stmt::DefaultStmtClass:
    // If we reached here, we must not handling a switch case in the top level.
    return emitSwitchCase(cast<SwitchCase>(*s),
                          /*buildingTopLevelCase=*/false, attrs);

  case Stmt::BreakStmtClass:
    return emitBreakStmt(cast<BreakStmt>(*s));
  case Stmt::ReturnStmtClass:
    return emitReturnStmt(cast<ReturnStmt>(*s));
  }

  return mlir::success();
}

mlir::LogicalResult CIRGenFunction::emitLabelStmt(const clang::LabelStmt &s) {
  if (emitLabel(*s.getDecl()).failed())
    return mlir::failure();

  incrementProfileCounter(s.getDecl()->getStmt());

  const Stmt *namedTarget = s.getInnermostLabeledStmt();
  bool tracksNamedLoopTarget =
      isa<WhileStmt, DoStmt, ForStmt, CXXForRangeStmt, ObjCForCollectionStmt,
          SwitchStmt>(namedTarget);
  if (tracksNamedLoopTarget)
    activeNamedLoopTargetStack.push_back(namedTarget);

  if (getContext().getLangOpts().EHAsynch && s.isSideEntry()) {
    getCIRGenModule().errorNYI(s.getSourceRange(), "IsEHa: not implemented.");
    if (tracksNamedLoopTarget)
      activeNamedLoopTargetStack.pop_back();
    return mlir::failure();
  }

  // When the label's sub-statement is a CompoundStmt, create a cleanup-only
  // LexicalScope to properly track RAII object lifetimes. Without this, the
  // compound stmt shares the parent scope (useCurrentScope=true), and RAII
  // objects inside it are never cleaned up when a goto jumps out. The
  // cleanup-only scope provides RunCleanupsScope tracking and appears in the
  // LexicalScope chain (for goto source range analysis), but doesn't emit
  // cir.scope or cir.yield ops.
  if (const auto *cs = dyn_cast<CompoundStmt>(s.getSubStmt())) {
    mlir::Location scopeLoc = getLoc(cs->getSourceRange());
    LexicalScope lexScope(*this, scopeLoc, builder.getInsertionBlock());
    lexScope.setClangSourceRange(cs->getSourceRange());
    lexScope.setCleanupOnly();
    auto res = emitCompoundStmtWithoutScope(*cs);
    if (tracksNamedLoopTarget)
      activeNamedLoopTargetStack.pop_back();
    return res;
  }

  auto res = emitStmt(s.getSubStmt(), /*useCurrentScope*/ true);
  if (tracksNamedLoopTarget)
    activeNamedLoopTargetStack.pop_back();
  return res;
}

mlir::LogicalResult
CIRGenFunction::emitObjCAutoreleasePoolStmt(const ObjCAutoreleasePoolStmt &s) {
  const auto &body = cast<CompoundStmt>(*s.getSubStmt());
  RunCleanupsScope scope(*this);
  mlir::Value token = emitObjCAutoreleasePoolPush(getLoc(s.getBeginLoc()));
  emitObjCAutoreleasePoolCleanup(token);
  return emitCompoundStmt(body);
}

mlir::LogicalResult CIRGenFunction::emitDeferStmt(const clang::DeferStmt &s) {
  ehStack.pushCleanup<EmitDeferredStatement>(NormalAndEHCleanup, &s);
  return mlir::success();
}

mlir::LogicalResult CIRGenFunction::emitObjCRethrow(mlir::Location loc) {
  auto voidTy = cir::VoidType::get(&getMLIRContext());
  auto rethrowFnTy = cir::FuncType::get({}, voidTy);
  cir::FuncOp rethrowFn =
      cgm.createRuntimeFunction(rethrowFnTy, "objc_exception_rethrow");

  cir::CallOp callOp = builder.createCallOp(loc, rethrowFn, {});
  if (isCatchOrCleanupRequired())
    callOp.setExceptionAttr(mlir::UnitAttr::get(builder.getContext()));

  mlir::NamedAttrList callAttrs;
  auto noReturn = cir::NoReturnAttr::get(&getMLIRContext());
  callAttrs.set(noReturn.getMnemonic(), noReturn);
  callOp->setAttr("extra_attrs", cir::ExtraFuncAttributesAttr::get(
                                     callAttrs.getDictionary(
                                         &getMLIRContext())));

  if (callOp.getExceptionAttr()) {
    cir::TryOp enclosingTryOp =
        curLexScope ? curLexScope->getClosestTryParent() : cir::TryOp();
    if (enclosingTryOp) {
      callWithExceptionCtx = callOp;
      populateCatchHandlersIfRequired(enclosingTryOp);
      callWithExceptionCtx = nullptr;

      EHScope &innermostScope = *ehStack.find(ehStack.getInnermostEHScope());
      if (innermostScope.getKind() == EHScope::Cleanup &&
          cast<EHCleanupScope>(innermostScope).isEHCleanup())
        enclosingTryOp.setCleanup(true);
    }
  }

  cir::UnreachableOp::create(builder, loc);
  builder.clearInsertionPoint();
  return mlir::success();
}

mlir::Value CIRGenFunction::emitMayThrowOpViaSyntheticCleanupTry(
    mlir::Location loc, mlir::Type resultTy,
    llvm::function_ref<mlir::Value()> emitOp) {
  // A throwing op with a non-call representation (e.g. cir.dyn_cast ref, whose
  // __cxa_bad_cast is synthesized later in LoweringPrepare) is not routed
  // through emitCallLikeOp's synthetic-try machinery, so with active EH cleanups
  // but no user try it would drop the local destructors on the throw's unwind
  // edge. Wrap it in a synthetic cleanup-only TryOp whose handler eagerly emits
  // the active EH cleanup bodies (RAII/ARC destructors); LoweringPrepare then
  // sees the op inside a TryOp and emits the abort call as an exception, and
  // FlattenCFG routes it through the cleanup landing pad. Mirrors the cleanup
  // path of emitObjCThrowViaSyntheticTry / Case 1 of emitCXXThrowExpr.

  // Allocate the result slot before the try so it dominates the post-try load;
  // the op's result cannot escape the try region directly.
  Address resultSlot =
      resultTy ? createTempAlloca(resultTy, getPointerAlign(), loc,
                                  "tmp.try.dyncast.res")
               : Address::invalid();

  cir::TryOp syntheticTryOp = cir::TryOp::create(
      builder, loc,
      /*tryBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location l) {},
      /*handlersBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location l, mlir::OperationState &result) {
        auto *r = result.addRegion();
        builder.createBlock(r);
        bool savedIsEmittingEHCleanup = isEmittingEHCleanup;
        isEmittingEHCleanup = true;
        for (EHScopeStack::stable_iterator si = ehStack.getInnermostEHScope();
             si != ehStack.stable_end();
             si = ehStack.find(si)->getEnclosingEHScope()) {
          EHScope &sc = *ehStack.find(si);
          if (sc.getKind() != EHScope::Cleanup)
            break;
          EHCleanupScope &cs = cast<EHCleanupScope>(sc);
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
      });
  syntheticTryOp.setSynthetic(true);
  syntheticTryOp.setCleanup(true);
  syntheticTryOp.setHandlerTypesAttr(mlir::ArrayAttr::get(
      &getMLIRContext(), {cir::UnwindAttr::get(&getMLIRContext())}));

  mlir::OpBuilder::InsertPoint savedIP = builder.saveInsertionPoint();
  builder.setInsertionPointToStart(&syntheticTryOp.getTryRegion().back());
  mlir::Value opResult;
  {
    LexicalScope tryScope{*this, loc, builder.getInsertionBlock()};
    tryScope.setAsTry(syntheticTryOp);
    opResult = emitOp();
    if (resultTy && opResult)
      builder.createStore(loc, opResult, resultSlot);
  }
  if (builder.getInsertionBlock() &&
      !builder.getInsertionBlock()->mightHaveTerminator())
    cir::YieldOp::create(builder, syntheticTryOp.getLoc());
  builder.restoreInsertionPoint(savedIP);

  if (resultTy)
    return builder.createLoad(loc, resultSlot);
  return opResult;
}

// Wrap an @throw that has active EH cleanups but no user @try in a synthetic
// cleanup-only TryOp, then re-emit the throw inside it. This mirrors the Case 1
// (no enclosing try) path in emitCXXThrowExpr: the handler eagerly emits the
// active EH cleanup bodies (RAII/ARC destructors), and ehCleanupWalkBoundary is
// set so the throw's own per-call cleanup does not re-emit them (avoiding a
// double-destroy). FlattenCFG turns the wrapped objc_exception_throw into an
// invoke with a landing pad that runs those cleanups then resumes.
mlir::LogicalResult
CIRGenFunction::emitObjCThrowViaSyntheticTry(const ObjCAtThrowStmt &s) {
  mlir::Location loc = getLoc(s.getSourceRange());

  // Determine whether a Terminate or Filter scope governs the throw (e.g. a
  // noexcept function or a dynamic exception-spec). Check both direct scopes
  // and scopes sitting behind Cleanup scopes, matching emitCXXThrowExpr.
  bool isTerminateScope = false;
  bool isFilterScope = false;
  EHFilterScope *filterScope = nullptr;
  for (EHScopeStack::stable_iterator si = ehStack.getInnermostEHScope();
       si != ehStack.stable_end();
       si = ehStack.find(si)->getEnclosingEHScope()) {
    EHScope &sc = *ehStack.find(si);
    if (sc.getKind() == EHScope::Terminate) {
      isTerminateScope = true;
      break;
    }
    if (sc.getKind() == EHScope::Filter) {
      isFilterScope = true;
      filterScope = &cast<EHFilterScope>(sc);
      break;
    }
    if (sc.getKind() != EHScope::Cleanup)
      break;
  }

  cir::TryOp syntheticTryOp = cir::TryOp::create(
      builder, loc,
      /*tryBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location l) {},
      /*handlersBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location l, mlir::OperationState &result) {
        auto *r = result.addRegion();
        builder.createBlock(r);
        if (isTerminateScope || isFilterScope) {
          // Terminate/Filter: empty handler region, populated below by
          // emitTerminateHandlerBody/emitFilterHandlerBody.
          cir::ResumeOp::create(builder, loc);
        } else {
          // Cleanup: eagerly emit the destructor calls for all active EH
          // cleanup scopes. These run during unwinding when the throw's
          // landing pad is reached.
          bool savedIsEmittingEHCleanup = isEmittingEHCleanup;
          isEmittingEHCleanup = true;
          for (EHScopeStack::stable_iterator si =
                   ehStack.getInnermostEHScope();
               si != ehStack.stable_end();
               si = ehStack.find(si)->getEnclosingEHScope()) {
            EHScope &sc = *ehStack.find(si);
            if (sc.getKind() != EHScope::Cleanup)
              break;
            EHCleanupScope &cs = cast<EHCleanupScope>(sc);
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
    syntheticTryOp.setHandlerTypesAttr(mlir::ArrayAttr::get(
        &getMLIRContext(), {cir::CatchAllAttr::get(&getMLIRContext())}));
    emitTerminateHandlerBody(syntheticTryOp);
  } else if (isFilterScope) {
    syntheticTryOp.setHandlerTypesAttr(mlir::ArrayAttr::get(
        &getMLIRContext(), {cir::CatchAllAttr::get(&getMLIRContext())}));
    llvm::SmallVector<mlir::Attribute> filterTypes;
    for (unsigned i = 0; i < filterScope->getNumFilters(); ++i)
      filterTypes.push_back(filterScope->getFilter(i));
    syntheticTryOp->setAttr(
        "cir.filter_types",
        mlir::ArrayAttr::get(&getMLIRContext(), filterTypes));
    emitFilterHandlerBody(syntheticTryOp);
  } else {
    syntheticTryOp.setCleanup(true);
    syntheticTryOp.setHandlerTypesAttr(mlir::ArrayAttr::get(
        &getMLIRContext(), {cir::UnwindAttr::get(&getMLIRContext())}));
  }

  mlir::OpBuilder::InsertPoint savedIP = builder.saveInsertionPoint();
  builder.setInsertionPointToStart(&syntheticTryOp.getTryRegion().back());

  // The pre-existing RAII/ARC cleanups are already emitted in the synthetic
  // handler above, so bound the throw's own per-call cleanup walk here to avoid
  // emitting them a second time (double-destroy).
  ehCleanupWalkBoundary = ehStack.getInnermostEHScope();

  // Re-emit the throw. getClosestTryParent() now finds this synthetic TryOp, so
  // the per-path wiring routes objc_exception_throw through the landing pad.
  // Scope the LexicalScope in an inner block so it is torn down before the tail
  // fixups below, matching emitCXXThrowExpr.
  mlir::LogicalResult emitResult = mlir::success();
  {
    LexicalScope tryScope{*this, loc, builder.getInsertionBlock()};
    tryScope.setAsTry(syntheticTryOp);
    emitResult = emitObjCAtThrowStmt(s);
  }

  ehCleanupWalkBoundary = EHScopeStack::stable_end();

  // Give the dead continuation block created by the throw its yield terminator.
  if (builder.getInsertionBlock() &&
      !builder.getInsertionBlock()->mightHaveTerminator())
    cir::YieldOp::create(builder, syntheticTryOp.getLoc());
  builder.restoreInsertionPoint(savedIP);

  return emitResult;
}

mlir::LogicalResult
CIRGenFunction::emitObjCAtThrowStmt(const ObjCAtThrowStmt &s) {
  mlir::Location loc = getLoc(s.getSourceRange());
  const Expr *throwExpr = s.getThrowExpr();

  // When an @throw appears in a scope with active EH cleanup (e.g. an
  // enclosing C++ RAII object with a non-trivial destructor, or an ARC
  // __strong local) but there is no user @try/try, the objc_exception_throw
  // call must still run those cleanups on its unwind edge. Create a synthetic
  // cleanup-only TryOp wrapping the throw, then re-emit the throw inside it so
  // getClosestTryParent() finds this try and the per-path wiring below routes
  // the call through the cleanup landing pad. This mirrors the Case 1 path in
  // emitCXXThrowExpr for a plain C++ throw with active cleanups; without it the
  // call is emitted as a non-unwinding cir.call and the enclosing destructors
  // are dropped during unwinding.
  cir::TryOp userTry =
      curLexScope ? curLexScope->getClosestTryParent() : cir::TryOp();

  // An ObjC @throw inside an outlined OpenMP region whose active unwind
  // destination is an EH scope pushed *outside* the region (e.g. a try wrapping
  // the region) acquires an unwind edge crossing the region boundary. Because
  // @throw lowers to objc_exception_throw here rather than through
  // emitCXXThrowExpr, it bypasses that function's OpenMP-escape gate; mirror it.
  // Left unchecked, CIRGen ties the throw to the enclosing (out-of-region) TryOp
  // and FlattenCFG produces a malformed cross-region block successor that
  // crashes collectUnreachable's block-BFS during CIR-to-LLVM lowering.
  // exceptionEscapesOpenMPRegion walks the EH chain so the gate still fires when
  // in-region cleanups (RAII/ARC) sit between the throw and the out-of-region
  // handler. The gate is scoped to throws wired to a *real* enclosing TryOp:
  // with none -- or only the in-region synthetic cleanup TryOp that the
  // emitObjCThrowViaSyntheticTry path below creates and re-emits the throw under
  // -- the throw's landing pad and cir.resume stay entirely in-region and never
  // build the cross-region edge, even when an out-of-region ARC/RAII cleanup is
  // active. When the gate fires, diagnose NYI and emit a plain terminating throw
  // with no unwind edge (suppress the exception attribute), the
  // objc_exception_throw analogue of the plain-call gate in emitCall.
  bool escapesOpenMPRegion = userTry && !userTry.getSynthetic() &&
                             isCatchOrCleanupRequired() &&
                             exceptionEscapesOpenMPRegion();
  if (escapesOpenMPRegion)
    cgm.errorNYI(s.getSourceRange(), "exception escaping an OpenMP region");

  if (!userTry && isCatchOrCleanupRequired())
    return emitObjCThrowViaSyntheticTry(s);

  if (getLangOpts().ObjCRuntime.getKind() == ObjCRuntime::FragileMacOSX) {
    mlir::Value exceptionObject;
    if (throwExpr) {
      exceptionObject = emitObjCThrowOperand(throwExpr);
    } else {
      assert(!objCEHValueStack.empty() && objCEHValueStack.back() &&
             "Unexpected rethrow outside @catch block");
      exceptionObject = objCEHValueStack.back();
    }

    auto objPtrTy = builder.getVoidPtrTy();
    if (exceptionObject.getType() != objPtrTy)
      exceptionObject = builder.createBitcast(exceptionObject, objPtrTy);

    auto voidTy = cir::VoidType::get(&getMLIRContext());
    auto throwFnTy = cir::FuncType::get({objPtrTy}, voidTy);
    cir::FuncOp throwFn =
        cgm.createRuntimeFunction(throwFnTy, "objc_exception_throw");

    cir::CallOp callOp = builder.createCallOp(loc, throwFn, {exceptionObject});
    if (isCatchOrCleanupRequired() && !escapesOpenMPRegion)
      callOp.setExceptionAttr(mlir::UnitAttr::get(builder.getContext()));
    else
      callOp.setNothrow(true);

    mlir::NamedAttrList callAttrs;
    auto noReturn = cir::NoReturnAttr::get(&getMLIRContext());
    callAttrs.set(noReturn.getMnemonic(), noReturn);
    callOp->setAttr("extra_attrs", cir::ExtraFuncAttributesAttr::get(
                                       callAttrs.getDictionary(
                                           &getMLIRContext())));

    if (callOp.getExceptionAttr()) {
      cir::TryOp enclosingTryOp =
          curLexScope ? curLexScope->getClosestTryParent() : cir::TryOp();
      if (enclosingTryOp) {
        callWithExceptionCtx = callOp;
        populateCatchHandlersIfRequired(enclosingTryOp);
        callWithExceptionCtx = nullptr;

        EHScope &innermostScope = *ehStack.find(ehStack.getInnermostEHScope());
        if (innermostScope.getKind() == EHScope::Cleanup &&
            cast<EHCleanupScope>(innermostScope).isEHCleanup())
          enclosingTryOp.setCleanup(true);
      }
    }

    cir::UnreachableOp::create(builder, loc);
    builder.clearInsertionPoint();
    return mlir::success();
  }

  if (!throwExpr) {
    assert(!objCEHValueStack.empty() && objCEHValueStack.back() &&
           "Unexpected rethrow outside @catch block");

    const ObjCBareRethrowInfo rethrowInfo = getObjCBareRethrowInfo(*this);
    llvm::SmallVector<mlir::Value, 1> args;
    cir::FuncOp rethrowFn;

    if (rethrowInfo.takesObject) {
      mlir::Value exceptionObject = objCEHValueStack.back();
      auto objPtrTy = builder.getVoidPtrTy();
      if (exceptionObject.getType() != objPtrTy)
        exceptionObject = builder.createBitcast(exceptionObject, objPtrTy);
      args.push_back(exceptionObject);

      auto voidTy = cir::VoidType::get(&getMLIRContext());
      rethrowFn = cgm.createRuntimeFunction(
          cir::FuncType::get({objPtrTy}, voidTy), rethrowInfo.fnName);
    } else {
      mlir::Type resultTy = rethrowInfo.returnsObject
                                ? static_cast<mlir::Type>(builder.getVoidPtrTy())
                                : static_cast<mlir::Type>(
                                      cir::VoidType::get(&getMLIRContext()));
      rethrowFn = cgm.createRuntimeFunction(
          cir::FuncType::get({}, resultTy), rethrowInfo.fnName);
    }

    cir::CallOp callOp = builder.createCallOp(loc, rethrowFn, args);
    if (isCatchOrCleanupRequired() && !escapesOpenMPRegion)
      callOp.setExceptionAttr(mlir::UnitAttr::get(builder.getContext()));

    mlir::NamedAttrList callAttrs;
    auto noReturn = cir::NoReturnAttr::get(&getMLIRContext());
    callAttrs.set(noReturn.getMnemonic(), noReturn);
    callOp->setAttr("extra_attrs", cir::ExtraFuncAttributesAttr::get(
                                       callAttrs.getDictionary(
                                           &getMLIRContext())));

    if (callOp.getExceptionAttr()) {
      cir::TryOp enclosingTryOp =
          curLexScope ? curLexScope->getClosestTryParent() : cir::TryOp();
      if (enclosingTryOp) {
        callWithExceptionCtx = callOp;
        populateCatchHandlersIfRequired(enclosingTryOp);
        callWithExceptionCtx = nullptr;

        EHScope &innermostScope = *ehStack.find(ehStack.getInnermostEHScope());
        if (innermostScope.getKind() == EHScope::Cleanup &&
            cast<EHCleanupScope>(innermostScope).isEHCleanup())
          enclosingTryOp.setCleanup(true);
      }
    }

    cir::UnreachableOp::create(builder, loc);
    builder.clearInsertionPoint();
    return mlir::success();
  }

  mlir::Value exceptionObject = emitObjCThrowOperand(throwExpr);
  auto objPtrTy = builder.getVoidPtrTy();
  if (exceptionObject.getType() != objPtrTy)
    exceptionObject = builder.createBitcast(exceptionObject, objPtrTy);

  auto voidTy = cir::VoidType::get(&getMLIRContext());
  auto throwFnTy = cir::FuncType::get({objPtrTy}, voidTy);
  cir::FuncOp throwFn = cgm.createRuntimeFunction(throwFnTy,
                                                  "objc_exception_throw");

  cir::CallOp callOp = builder.createCallOp(loc, throwFn, {exceptionObject});
  if (isCatchOrCleanupRequired() && !escapesOpenMPRegion)
    callOp.setExceptionAttr(mlir::UnitAttr::get(builder.getContext()));

  mlir::NamedAttrList callAttrs;
  auto noReturn = cir::NoReturnAttr::get(&getMLIRContext());
  callAttrs.set(noReturn.getMnemonic(), noReturn);
  callOp->setAttr("extra_attrs", cir::ExtraFuncAttributesAttr::get(
                                     callAttrs.getDictionary(
                                         &getMLIRContext())));

  if (callOp.getExceptionAttr()) {
    cir::TryOp enclosingTryOp =
        curLexScope ? curLexScope->getClosestTryParent() : cir::TryOp();
    if (enclosingTryOp) {
      callWithExceptionCtx = callOp;
      populateCatchHandlersIfRequired(enclosingTryOp);
      callWithExceptionCtx = nullptr;

      EHScope &innermostScope = *ehStack.find(ehStack.getInnermostEHScope());
      if (innermostScope.getKind() == EHScope::Cleanup &&
          cast<EHCleanupScope>(innermostScope).isEHCleanup())
        enclosingTryOp.setCleanup(true);
    }
  }

  cir::UnreachableOp::create(builder, loc);
  builder.clearInsertionPoint();
  return mlir::success();
}

mlir::LogicalResult
CIRGenFunction::emitObjCAtSynchronizedStmt(const ObjCAtSynchronizedStmt &s) {
  struct CallObjCSyncExit final : EHScopeStack::Cleanup {
    cir::FuncOp syncExitFn;
    Address syncArgAddr;

    CallObjCSyncExit(cir::FuncOp syncExitFn, Address syncArgAddr)
        : syncExitFn(syncExitFn), syncArgAddr(syncArgAddr) {}

    void emit(CIRGenFunction &cgf, Flags flags) override {
      auto loc = cgf.curFn->getLoc();
      mlir::Value syncArg = cgf.builder.createLoad(loc, syncArgAddr);
      cir::CallOp call = cgf.builder.createCallOp(loc, syncExitFn, {syncArg});
      call.setNothrow(true);
    }
  };

  mlir::Location loc = getLoc(s.getSourceRange());
  const Expr *lockExpr = s.getSynchExpr();
  mlir::Value lockValue;
  bool releaseLock = false;
  if (getLangOpts().ObjCAutoRefCount) {
    lockValue = emitARCRetainScalarExpr(lockExpr);
    releaseLock = true;
  } else {
    lockValue = emitScalarExpr(lockExpr);
  }

  if (isCurrentIPKnownUnreachable(*this))
    return mlir::success();

  auto objPtrTy = builder.getVoidPtrTy();
  if (lockValue.getType() != objPtrTy)
    lockValue = builder.createBitcast(lockValue, objPtrTy);

  Address syncArgAddr =
      createTempAlloca(objPtrTy, getPointerAlign(), loc, "sync.arg");
  builder.createStore(loc, lockValue, syncArgAddr);

  auto intTy = builder.getSInt32Ty();
  auto syncFnTy = cir::FuncType::get({objPtrTy}, intTy);
  cir::FuncOp syncEnterFn = cgm.createRuntimeFunction(syncFnTy,
                                                      "objc_sync_enter");
  cir::FuncOp syncExitFn = cgm.createRuntimeFunction(syncFnTy,
                                                     "objc_sync_exit");

  cir::CallOp enterCall = builder.createCallOp(loc, syncEnterFn, {lockValue});
  enterCall.setNothrow(true);

  if (getLangOpts().ObjCRuntime.getKind() == ObjCRuntime::FragileMacOSX) {
    auto voidTy = cir::VoidType::get(&getMLIRContext());
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
    cir::FuncOp setJmpFn = getObjCFragileSetJmpFn(*this);

    mlir::Type exceptionDataTy = getObjCFragileExceptionDataTy(*this);
    Address exceptionDataAddr = createTempAlloca(exceptionDataTy,
                                                 getPointerAlign(), loc,
                                                 "exceptiondata.ptr");

    FragileObjCHazards hazards(*this);

    Address callTryExitAddr =
        createTempAlloca(builder.getBoolTy(), CharUnits::One(), loc,
                         "_call_try_exit");
    builder.createStore(loc, builder.getFalse(loc), callTryExitAddr);

    ehStack.pushCleanup<CallFragileSyncExit>(NormalAndEHCleanup, loc, syncExitFn,
                                             tryExitFn, syncArgAddr,
                                             callTryExitAddr, exceptionDataAddr,
                                             releaseLock);

    mlir::Value exceptionData = exceptionDataAddr.getPointer();
    if (exceptionData.getType() != objPtrTy)
      exceptionData = builder.createBitcast(exceptionData, objPtrTy);

    cir::CallOp tryEnterCall =
        builder.createCallOp(loc, tryEnterFn, {exceptionData});
    tryEnterCall.setNothrow(true);

    auto setJmpBufferTy = cir::ArrayType::get(builder.getSInt32Ty(), 18);
    mlir::Value setJmpBuffer = builder.createGetMember(
        loc, builder.getPointerTo(setJmpBufferTy), exceptionDataAddr.getPointer(),
        "", 0);
    if (setJmpBuffer.getType() != objPtrTy)
      setJmpBuffer = builder.createBitcast(setJmpBuffer, objPtrTy);

    cir::CallOp setJmpCall = builder.createCallOp(loc, setJmpFn, {setJmpBuffer});
    setJmpCall.setNothrow(true);
    markReturnsTwice(setJmpCall, getMLIRContext());

    mlir::Value setJmpResult = setJmpCall.getResult();
    mlir::Value didEnterTry = cir::CmpOp::create(
        builder, loc, cir::CmpOpKind::eq, setJmpResult,
        builder.getSInt32(0, loc));

    mlir::Region *region = builder.getBlock()->getParent();
    auto *tryBlock = new mlir::Block();
    auto *handlerBlock = new mlir::Block();
    region->push_back(tryBlock);
    region->push_back(handlerBlock);
    cir::BrCondOp::create(builder, loc, didEnterTry, tryBlock, handlerBlock);

    builder.setInsertionPointToStart(tryBlock);
    builder.createStore(loc, builder.getTrue(loc), callTryExitAddr);
    if (mlir::failed(emitStmt(s.getSynchBody(), /*useCurrentScope=*/false)))
      return mlir::failure();

    mlir::OpBuilder::InsertPoint tryFallthroughIP = builder.saveInsertionPoint();
    hazards.emitReadHazardsInNewBlocks();
    if (tryFallthroughIP.isSet())
      builder.restoreInsertionPoint(tryFallthroughIP);
    popCleanupBlock();

    mlir::OpBuilder::InsertPoint normalIP = builder.saveInsertionPoint();

    builder.setInsertionPointToStart(handlerBlock);
    hazards.emitWriteHazard(loc);

    mlir::Value syncArg = builder.createLoad(loc, syncArgAddr);
    cir::CallOp syncExitCall =
        builder.createCallOp(loc, syncExitFn, {syncArg});
    syncExitCall.setNothrow(true);
    if (releaseLock)
      emitARCRelease(syncArg, loc);

    cir::CallOp extractCall =
        builder.createCallOp(loc, extractFn, {exceptionData});
    extractCall.setNothrow(true);

    cir::CallOp throwCall =
        builder.createCallOp(loc, throwFn, {extractCall.getResult()});
    throwCall.setNothrow(true);
    markNoReturnCall(throwCall, getMLIRContext());
    cir::UnreachableOp::create(builder, loc);
    builder.clearInsertionPoint();

    if (normalIP.isSet())
      builder.restoreInsertionPoint(normalIP);
    return mlir::success();
  }

  RunCleanupsScope scope(*this);
  if (releaseLock)
    emitARCConsumeObject(lockValue, getLoc(lockExpr->getExprLoc()));

  ehStack.pushCleanup<CallObjCSyncExit>(NormalAndEHCleanup, syncExitFn,
                                        syncArgAddr);
  return emitStmt(s.getSynchBody(), /*useCurrentScope=*/false);
}

mlir::LogicalResult
CIRGenFunction::emitObjCForCollectionStmt(const ObjCForCollectionStmt &s) {
  const ObjCRuntime runtime = getLangOpts().ObjCRuntime;
  const bool hasAppleObjCRuntime =
      runtime.isNeXTFamily() &&
      (runtime.isNonFragile() ||
       (getTarget().getTriple().isOSDarwin() &&
        getTarget().getTriple().isOSBinFormatMachO()));
  const bool hasGNUObjCRuntime =
      runtime.getKind() == ObjCRuntime::GCC ||
      runtime.getKind() == ObjCRuntime::GNUstep ||
      runtime.getKind() == ObjCRuntime::ObjFW;
  if (!hasAppleObjCRuntime && !hasGNUObjCRuntime) {
    cgm.errorNYI(s.getSourceRange(), "ObjC fast enumeration on unsupported runtime");
    return mlir::failure();
  }

  mlir::Location loc = getLoc(s.getSourceRange());
  RunCleanupsScope forScope(*this);

  AutoVarEmission variable = AutoVarEmission::invalid();
  if (const auto *declStmt = dyn_cast<DeclStmt>(s.getElement()))
    variable = emitAutoVarAlloca(*cast<VarDecl>(declStmt->getSingleDecl()));

  QualType stateTy = cgm.getObjCFastEnumerationStateType();
  Address statePtr = createMemTemp(stateTy, loc, "state.ptr");
  emitNullInitialization(loc, statePtr, stateTy);

  constexpr unsigned numItems = 16;
  QualType itemsTy = getContext().getConstantArrayType(
      getContext().getObjCIdType(), llvm::APInt(32, numItems), nullptr,
      ArraySizeModifier::Normal, 0);
  Address itemsPtr = createMemTemp(itemsTy, loc, "items.ptr");

  mlir::Type objCIdTy = convertType(getContext().getObjCIdType());
  mlir::Type nsUIntTy = convertType(getContext().getNSUIntegerType());
  mlir::Type unsignedLongTy = convertType(getContext().UnsignedLongTy);
  CharUnits nsUIntAlign =
      getContext().getTypeAlignInChars(getContext().getNSUIntegerType());
  CharUnits unsignedLongAlign =
      getContext().getTypeAlignInChars(getContext().UnsignedLongTy);
  CharUnits boolAlign = getContext().getTypeAlignInChars(getContext().BoolTy);

  mlir::Value zero = builder.getConstInt(loc, nsUIntTy, 0);
  mlir::Value one = builder.getConstInt(loc, nsUIntTy, 1);
  mlir::Value bufferCount = builder.getConstInt(loc, nsUIntTy, numItems);

  Address countAddr =
      createTempAlloca(nsUIntTy, nsUIntAlign, loc, "forcoll.count");
  Address indexAddr =
      createTempAlloca(nsUIntTy, nsUIntAlign, loc, "forcoll.index");
  Address needsRefillAddr = createTempAlloca(builder.getBoolTy(), boolAlign,
                                             loc, "forcoll.refill");
  builder.createStore(loc, zero, indexAddr);
  builder.createStore(loc, builder.getFalse(loc), needsRefillAddr);

  mlir::Value collection;
  if (getLangOpts().ObjCAutoRefCount) {
    collection = emitARCRetainScalarExpr(s.getCollection());
    emitARCConsumeObject(collection, getLoc(s.getCollection()->getExprLoc()));
  } else {
    collection = emitScalarExpr(s.getCollection());
  }

  const IdentifierInfo *selectorIds[] = {
      &getContext().Idents.get("countByEnumeratingWithState"),
      &getContext().Idents.get("objects"),
      &getContext().Idents.get("count")};
  Selector fastEnumSel =
      getContext().Selectors.getSelector(std::size(selectorIds), selectorIds);

  const RecordDecl *stateRecord = stateTy->getAsRecordDecl();
  assert(stateRecord && "fast-enumeration state should be a record type");
  auto getStateField = [&](unsigned fieldIndex) -> const FieldDecl * {
    auto fieldIt = stateRecord->field_begin();
    std::advance(fieldIt, fieldIndex);
    return *fieldIt;
  };
  Address stateItemsPtrAddr =
      emitAddrOfFieldStorage(statePtr, getStateField(1), "", 1);
  Address stateMutationsPtrAddr =
      emitAddrOfFieldStorage(statePtr, getStateField(2), "", 2);

  auto emitCountByEnumerating = [&](mlir::Location callLoc) -> mlir::Value {
    mlir::Value itemsBuffer = builder.getArrayElement(
        callLoc, callLoc, itemsPtr.getPointer(), itemsPtr.getElementType(),
        zero, /*shouldDecay=*/true);

    llvm::SmallVector<CallArg, 3> args;
    args.emplace_back(RValue::get(statePtr.getPointer()),
                      getContext().getPointerType(stateTy));
    args.emplace_back(
        RValue::get(itemsBuffer),
        getContext().getPointerType(getContext().getObjCIdType()));
    args.emplace_back(RValue::get(bufferCount), getContext().getNSUIntegerType());
    if (hasAppleObjCRuntime)
      return emitObjCMsgSend(*this, callLoc, getContext().getNSUIntegerType(),
                             collection, fastEnumSel, args)
          .getValue();

    QualType objCIdTy = getContext().getObjCIdType();
    QualType objCSelTy = getContext().getObjCSelType();

    llvm::SmallVector<QualType, 4> argTypes;
    argTypes.push_back(objCIdTy);
    argTypes.push_back(objCSelTy);
    for (const CallArg &arg : args)
      argTypes.push_back(arg.ty);

    QualType msgFnTy = getContext().getFunctionType(
        getContext().getNSUIntegerType(), argTypes,
        FunctionProtoType::ExtProtoInfo());
    auto msgProto = CanQual<FunctionProtoType>::CreateUnsafe(
        getContext().getCanonicalType(msgFnTy));
    const CIRGenFunctionInfo &funcInfo =
        getTypes().arrangeFreeFunctionType(msgProto);
    cir::FuncType exactFnTy = getTypes().getFunctionType(funcInfo);

    CallArgList actualArgs;
    mlir::Type idTy = convertType(objCIdTy);
    mlir::Value receiver = collection;
    if (receiver.getType() != idTy)
      receiver = builder.createBitcast(callLoc, receiver, idTy);
    actualArgs.add(RValue::get(receiver), objCIdTy);

    mlir::Value selector = cgm.getObjCSelector(*this, fastEnumSel, callLoc);
    actualArgs.add(RValue::get(selector), objCSelTy);
    for (const CallArg &arg : args) {
      assert(!arg.hasLValue() && "fast-enum helper expects scalar args");
      actualArgs.add(arg.getKnownRValue(), arg.ty);
    }

    auto attachGNUObjCMessageSendAttrs = [&](mlir::Operation *op) {
      op->setAttr(
          "cir.gnu_objc_message_send",
          mlir::ArrayAttr::get(
              &cgm.getMLIRContext(),
              {cgm.getStringAttrCached(fastEnumSel.getAsString()),
               cgm.getStringAttrCached(""),
               cgm.getBoolAttrCached(false)}));
    };

    const bool useGNUNonLegacyObjCRuntime =
        cgm.getCodeGenOpts().getObjCDispatchMethod() != CodeGenOptions::Legacy;
    if (useGNUNonLegacyObjCRuntime) {
      cir::FuncOp msgSendFn = cgm.getObjCMsgSendFunc();
      mlir::Value msgSendAddr = cir::GetGlobalOp::create(
          builder, callLoc, builder.getPointerTo(msgSendFn.getFunctionType()),
          msgSendFn.getSymNameAttr());
      mlir::Value msgSendPtr =
          builder.createBitcast(callLoc, msgSendAddr,
                                builder.getPointerTo(exactFnTy));

      cir::CIRCallOpInterface callOp;
      RValue result = emitCall(
          funcInfo, CIRGenCallee::forDirect(msgSendPtr.getDefiningOp()),
          ReturnValueSlot(), actualArgs, &callOp, callLoc);
      attachGNUObjCMessageSendAttrs(callOp);
      return result.getValue();
    }

    mlir::Value imp;
    switch (runtime.getKind()) {
    case ObjCRuntime::GCC:
    case ObjCRuntime::ObjFW: {
      auto lookupCall = builder.createCallOp(
          callLoc, cgm.getObjCMsgLookupFunc(),
          {actualArgs[0].getKnownRValue().getValue(), selector});
      attachGNUObjCMessageSendAttrs(lookupCall);
      imp = lookupCall.getResult();
      break;
    }
    case ObjCRuntime::GNUstep: {
      Address receiverSlot = createTempAlloca(cgm.voidPtrTy, getPointerAlign(),
                                              callLoc, "objc.receiver");
      mlir::Value originalReceiver = actualArgs[0].getKnownRValue().getValue();
      if (originalReceiver.getType() != cgm.voidPtrTy)
        originalReceiver =
            builder.createBitcast(callLoc, originalReceiver, cgm.voidPtrTy);
      builder.createStore(callLoc, originalReceiver, receiverSlot);

      mlir::Value sender = builder.getNullValue(cgm.voidPtrTy, callLoc);
      if (const auto *currentMethod =
              dyn_cast_or_null<ObjCMethodDecl>(curGD.getDecl())) {
        Address selfAddr = getAddrOfLocalVar(currentMethod->getSelfDecl());
        sender = builder.createLoad(callLoc, selfAddr);
        if (sender.getType() != cgm.voidPtrTy)
          sender = builder.createBitcast(callLoc, sender, cgm.voidPtrTy);
      }

      auto lookupSenderCall = builder.createCallOp(
          callLoc, cgm.getObjCMsgLookupSenderFunc(),
          {receiverSlot.getPointer(), selector, sender});
      attachGNUObjCMessageSendAttrs(lookupSenderCall);
      mlir::Value slot = lookupSenderCall.getResult();
      mlir::Value impAddr = builder.createGetMember(
          callLoc, builder.getPointerTo(cgm.voidPtrTy), slot, "", 4);
      imp = builder.createLoad(
          callLoc, Address(impAddr, cgm.voidPtrTy, getPointerAlign()));

      mlir::Value rewrittenReceiver = builder.createLoad(callLoc, receiverSlot);
      actualArgs[0] = CallArg(RValue::get(rewrittenReceiver), objCIdTy);
      break;
    }
    default:
      cgm.errorNYI("ObjC fast enumeration GNU runtime kind");
      return mlir::Value();
    }

    mlir::Type exactPtrTy = builder.getPointerTo(exactFnTy);
    if (imp.getType() != exactPtrTy)
      imp = builder.createBitcast(callLoc, imp, exactPtrTy);

    cir::CIRCallOpInterface callOp;
    RValue result = emitCall(
        funcInfo, CIRGenCallee::forDirect(imp.getDefiningOp()),
        ReturnValueSlot(), actualArgs, &callOp, callLoc);
    attachGNUObjCMessageSendAttrs(callOp);
    return result.getValue();
  };

  auto emitElementStore = [&](LValue elementLValue, mlir::Value value,
                              bool isInit) {
    mlir::Location storeLoc = value.getLoc();
    Qualifiers::ObjCLifetime lifetime =
        elementLValue.getQuals().getObjCLifetime();
    auto retainForLifetime = [&](mlir::Value storedValue) -> mlir::Value {
      if (elementLValue.getType()->isBlockPointerType())
        return emitARCRetainBlock(storedValue, storeLoc, /*mandatory=*/false);
      return emitARCRetainNonBlock(storedValue, storeLoc);
    };

    switch (lifetime) {
    case Qualifiers::OCL_Strong: {
      value = retainForLifetime(value);
      if (!isInit) {
        mlir::Value oldValue = emitLoadOfScalar(elementLValue, SourceLocation());
        emitStoreThroughLValue(RValue::get(value), elementLValue);
        emitARCRelease(oldValue, storeLoc);
        return;
      }
      emitStoreThroughLValue(RValue::get(value), elementLValue,
                             /*isInit=*/true);
      return;
    }
    case Qualifiers::OCL_Autoreleasing:
      if (elementLValue.getType()->isBlockPointerType()) {
        value = emitARCRetainBlock(value, storeLoc, /*mandatory=*/true);
        value = emitARCAutorelease(value, storeLoc);
      } else {
        value = emitARCRetainAutorelease(value, storeLoc);
      }
      emitStoreThroughLValue(RValue::get(value), elementLValue, isInit);
      return;
    case Qualifiers::OCL_Weak:
      if (isInit) {
        emitARCInitWeak(elementLValue.getAddress(), value);
      } else {
        (void)emitARCStoreWeak(elementLValue.getAddress(), value,
                               /*ignored=*/true);
      }
      return;
    case Qualifiers::OCL_None:
    case Qualifiers::OCL_ExplicitNone:
      emitStoreThroughLValue(RValue::get(value), elementLValue, isInit);
      return;
    }
    llvm_unreachable("unexpected ARC lifetime");
  };

  mlir::Value initialCount = emitCountByEnumerating(loc);
  builder.createStore(loc, initialCount, countAddr);

  mlir::LogicalResult loopRes = mlir::success();
  cir::ForOp forOp;
  NamedLoopControlTarget *namedTarget = nullptr;
  std::string namedBreakLabel;
  bool hasNamedTarget = isActiveNamedLoopTarget(&s);
  mlir::Location scopeLoc = getLoc(s.getSourceRange());
  cir::ScopeOp::create(builder, scopeLoc, [&](mlir::OpBuilder &,
                                              mlir::Location loopScopeLoc) {
    LexicalScope lexScope{*this, loopScopeLoc, builder.getInsertionBlock()};
    lexScope.setAsLoop();
    if (hasNamedTarget) {
      NamedLoopControlTarget target;
      target.loopOrSwitch = &s;
      target.scope = &lexScope;
      target.breakTargetDepth = forScope.getCleanupStackDepth();
      target.continueTargetDepth = lexScope.getCleanupStackDepth();
      target.breakLabel = createNamedLoopControlLabel("break");
      target.continueLabel = createNamedLoopControlLabel("continue");
      namedLoopControlStack.push_back(std::move(target));
      namedTarget = &namedLoopControlStack.back();
      namedBreakLabel = namedTarget->breakLabel;
    }

    mlir::Value hasInitialItems =
        cir::CmpOp::create(builder, loc, cir::CmpOpKind::ne, initialCount, zero);
    cir::IfOp::create(
        builder, loc, hasInitialItems, /*withElseRegion=*/false,
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          mlir::Value stateMutationsPtr =
              builder.createLoad(ifLoc, stateMutationsPtrAddr);
          mlir::Value initialMutations = builder.createLoad(
              ifLoc, Address(stateMutationsPtr, unsignedLongTy, unsignedLongAlign));

          forOp = builder.createFor(
              loc,
              /*condBuilder=*/
              [&](mlir::OpBuilder &, mlir::Location condLoc) {
                mlir::Value needsRefill =
                    builder.createLoad(condLoc, needsRefillAddr);
                cir::IfOp::create(
                    builder, condLoc, needsRefill, /*withElseRegion=*/false,
                    [&](mlir::OpBuilder &, mlir::Location refillLoc) {
                      mlir::Value refetchCount = emitCountByEnumerating(refillLoc);
                      builder.createStore(refillLoc, refetchCount, countAddr);
                      builder.createStore(refillLoc, zero, indexAddr);
                      builder.createStore(refillLoc, builder.getFalse(refillLoc),
                                          needsRefillAddr);
                      builder.createYield(refillLoc);
                    });

                mlir::Value count = builder.createLoad(condLoc, countAddr);
                mlir::Value hasItems =
                    cir::CmpOp::create(builder, condLoc, cir::CmpOpKind::ne,
                                       count, zero);
                builder.createCondition(hasItems);
              },
              /*bodyBuilder=*/
              [&](mlir::OpBuilder &, mlir::Location bodyLoc) {
                incrementProfileCounter(&s);

                mlir::Value stateMutationsPtr =
                    builder.createLoad(bodyLoc, stateMutationsPtrAddr);
                mlir::Value currentMutations = builder.createLoad(
                    bodyLoc,
                    Address(stateMutationsPtr, unsignedLongTy, unsignedLongAlign));
                mlir::Value wasMutated =
                    cir::CmpOp::create(builder, bodyLoc, cir::CmpOpKind::ne,
                                       currentMutations, initialMutations);
                cir::IfOp::create(
                    builder, bodyLoc, wasMutated, /*withElseRegion=*/false,
                    [&](mlir::OpBuilder &, mlir::Location mutationLoc) {
                      mlir::Value mutationReceiver = collection;
                      if (mutationReceiver.getType() != builder.getVoidPtrTy()) {
                        mutationReceiver = builder.createBitcast(
                            mutationLoc, mutationReceiver, builder.getVoidPtrTy());
                      }
                      SmallVector<mlir::Value, 1> mutationArgs{mutationReceiver};
                      emitRuntimeCall(mutationLoc,
                                      cgm.getObjCEnumerationMutationFunc(),
                                      mutationArgs);
                      builder.createYield(mutationLoc);
                    });

                RunCleanupsScope elementVariableScope(*this);
                bool elementIsVariable = false;
                LValue elementLValue;
                QualType elementType;

                if (const auto *declStmt = dyn_cast<DeclStmt>(s.getElement())) {
                  emitAutoVarInit(variable);

                  const auto *elementDecl =
                      cast<VarDecl>(declStmt->getSingleDecl());
                  DeclRefExpr elementRef(getContext(),
                                         const_cast<VarDecl *>(elementDecl),
                                         /*RefersToEnclosingVariableOrCapture=*/
                                         false,
                                         elementDecl->getType(), VK_LValue,
                                         SourceLocation());
                  elementLValue = emitLValue(&elementRef);
                  elementType = elementDecl->getType();
                  elementIsVariable = true;

                  if (elementDecl->isARCPseudoStrong())
                    elementLValue.getQuals().setObjCLifetime(
                        Qualifiers::OCL_ExplicitNone);
                } else {
                  elementType = cast<Expr>(s.getElement())->getType();
                }

                mlir::Value enumStateItems =
                    builder.createLoad(bodyLoc, stateItemsPtrAddr);
                mlir::Value index = builder.createLoad(bodyLoc, indexAddr);
                mlir::Value currentItemPtr = builder.getArrayElement(
                    bodyLoc, bodyLoc, enumStateItems, objCIdTy, index,
                    /*shouldDecay=*/true);
                mlir::Value currentItem = builder.createLoad(
                    bodyLoc, Address(currentItemPtr, objCIdTy, getPointerAlign()));
                mlir::Type convertedElementType = convertType(elementType);
                if (currentItem.getType() != convertedElementType)
                  currentItem = builder.createBitcast(bodyLoc, currentItem,
                                                      convertedElementType);

                if (!elementIsVariable)
                  elementLValue = emitLValue(cast<Expr>(s.getElement()));
                emitElementStore(elementLValue, currentItem,
                                 /*isInit=*/elementIsVariable);

                if (elementIsVariable)
                  emitAutoVarCleanups(variable);

                if (emitStmt(s.getBody(), /*useCurrentScope=*/false).failed())
                  loopRes = mlir::failure();
                emitStopPoint(&s);

                elementVariableScope.forceCleanup();
              },
              /*stepBuilder=*/
              [&](mlir::OpBuilder &, mlir::Location stepLoc) {
                if (namedTarget)
                  emitSyntheticLabel(stepLoc, namedTarget->continueLabel);
                mlir::Value index = builder.createLoad(stepLoc, indexAddr);
                mlir::Value nextIndex = builder.createNUWAdd(stepLoc, index, one);
                builder.createStore(stepLoc, nextIndex, indexAddr);

                mlir::Value count = builder.createLoad(stepLoc, countAddr);
                mlir::Value needsRefill =
                    cir::CmpOp::create(builder, stepLoc, cir::CmpOpKind::eq,
                                       nextIndex, count);
                builder.createStore(stepLoc, needsRefill, needsRefillAddr);
                builder.createYield(stepLoc);
              });

          if (auto yieldOp = getSingleYieldTerminator(forOp.getStep())) {
            yieldOp->setLoc(getLoc(s.getForLoc()));
            addOpToNewSourceAtom(yieldOp);
          }
          builder.createYield(ifLoc);
        });
    if (hasNamedTarget) {
      namedLoopControlStack.pop_back();
      namedTarget = nullptr;
    }
  });

  if (forOp) {
    terminateBody(builder, forOp.getBody(), getLoc(s.getEndLoc()));
    if (auto yieldOp = getSingleYieldTerminator(forOp.getBody())) {
      yieldOp->setLoc(getLoc(s.getForLoc()));
      addOpToNewSourceAtom(yieldOp);
    }
  }

  if (!isa<DeclStmt>(s.getElement())) {
    LValue elementLValue = emitLValue(cast<Expr>(s.getElement()));
    mlir::Value nullElement = builder.getNullValue(
        convertType(elementLValue.getType()), getLoc(s.getEndLoc()));
    emitElementStore(elementLValue, nullElement, /*isInit=*/false);
  }

  if (hasNamedTarget)
    emitSyntheticLabel(getLoc(s.getEndLoc()), namedBreakLabel);

  return loopRes;
}

mlir::LogicalResult
CIRGenFunction::emitAttributedStmt(const AttributedStmt &s) {
  bool nomerge = false;
  bool noinline = false;
  bool alwaysinline = false;
  bool noconvergent = false;
  const CallExpr *musttail = nullptr;

  for (const auto *a : s.getAttrs()) {
    switch (a->getKind()) {
    case attr::NoMerge:
      nomerge = true;
      break;
    case attr::NoInline:
      noinline = true;
      break;
    case attr::AlwaysInline:
      alwaysinline = true;
      break;
    case attr::MustTail: {
      const Stmt *sub = s.getSubStmt();
      const ReturnStmt *r = cast<ReturnStmt>(sub);
      musttail = cast<CallExpr>(r->getRetValue()->IgnoreParens());
    } break;
    case attr::CXXAssume: {
      const Expr *assumption = cast<CXXAssumeAttr>(a)->getAssumption();
      // OGCG also checks Builder.GetInsertBlock() before emitting the assume.
      assert(builder.getInsertionBlock() &&
             "expected insertion block before [[assume]] emission");
      if (getLangOpts().CXXAssumptions &&
          !assumption->HasSideEffects(getContext())) {
        mlir::Value argValue = emitCheckedArgForAssume(assumption);
        cir::AssumeOp::create(builder, getLoc(s.getSourceRange()), argValue);
      }
      break;
    }
    case attr::NoConvergent:
      noconvergent = true;
      break;
    case attr::Atomic:
      // OGCG creates CGAtomicOptionsRAII to override atomic codegen options.
      // CIR does not yet model a per-statement atomic options RAII object, and
      // there are currently no CIR call sites that consume AtomicAttr options.
      // Keep parity by accepting the attribute as a no-op for now.
      break;
    case attr::HLSLControlFlowHint:
      // Consumed by emitIfStmt/emitSwitchStmt via attrs forwarded to emitStmt.
      break;
    default:
      // For fallthrough and other attributes that don't need special handling,
      // just continue to emit the substatement.
      break;
    }
  }

  llvm::SaveAndRestore saveNoMerge(inNoMergeAttributedStmt, nomerge);
  llvm::SaveAndRestore saveNoInline(inNoInlineAttributedStmt, noinline);
  llvm::SaveAndRestore saveAlwaysInline(inAlwaysInlineAttributedStmt,
                                        alwaysinline);
  llvm::SaveAndRestore saveNoConvergent(inNoConvergentAttributedStmt,
                                         noconvergent);
  llvm::SaveAndRestore saveMusttail(mustTailCall, musttail);

  return emitStmt(s.getSubStmt(), /*useCurrentScope*/ true, s.getAttrs());
}

// Add a terminating yield on a body region if no other terminators are used.
static void terminateBody(CIRGenBuilderTy &builder, mlir::Region &r,
                          mlir::Location loc) {
  if (r.empty())
    return;

  SmallVector<mlir::Block *, 4> eraseBlocks;
  unsigned numBlocks = r.getBlocks().size();
  for (auto &block : r.getBlocks()) {
    // Already cleanup after return operations, which might create
    // empty blocks if emitted as last stmt.
    if (numBlocks != 1 && block.empty() && block.hasNoPredecessors() &&
        block.hasNoSuccessors())
      eraseBlocks.push_back(&block);

    if (block.empty() ||
        !block.back().hasTrait<mlir::OpTrait::IsTerminator>()) {
      mlir::OpBuilder::InsertionGuard guardCase(builder);
      builder.setInsertionPointToEnd(&block);
      builder.createYield(loc);
    }
  }

  for (auto *b : eraseBlocks)
    b->erase();
}

static cir::YieldOp getSingleYieldTerminator(mlir::Region &region) {
  if (!region.hasOneBlock())
    return nullptr;
  return mlir::dyn_cast<cir::YieldOp>(region.front().getTerminator());
}

mlir::LogicalResult CIRGenFunction::emitIfStmt(
    const IfStmt &s, llvm::ArrayRef<const Attr *> attrs) {
  mlir::LogicalResult res = mlir::success();
  std::optional<int32_t> hlslControlFlowHint = getHLSLControlFlowHint(attrs);
  // The else branch of a consteval if statement is always the only branch
  // that can be runtime evaluated.
  const Stmt *constevalExecuted;
  if (s.isConsteval()) {
    constevalExecuted = s.isNegatedConsteval() ? s.getThen() : s.getElse();
    if (!constevalExecuted) {
      // No runtime code execution required
      return res;
    }
  }

  // C99 6.8.4.1: The first substatement is executed if the expression
  // compares unequal to 0.  The condition must be a scalar type.
  auto ifStmtBuilder = [&]() -> mlir::LogicalResult {
    if (s.isConsteval()) {
      // OGCG wraps the executed consteval branch in a RunCleanupsScope.
      // CIR emits the branch in the current scope/cleanup model directly.
      return emitStmt(constevalExecuted, /*useCurrentScope=*/true);
    }

    if (s.getInit())
      if (emitStmt(s.getInit(), /*useCurrentScope=*/true).failed())
        return mlir::failure();

    if (s.getConditionVariable()) {
      ApplySourceAtomGroup condDeclAtom(*this);
      emitDecl(*s.getConditionVariable());
    }

    // Match OGCG: if the condition folds to a constant and the skipped arm has
    // no labels, emit only the runtime-executed arm.
    bool condConstant;
    if (constantFoldsToBool(s.getCond(), condConstant, s.isConstexpr())) {
      const Stmt *executed = s.getThen();
      const Stmt *skipped = s.getElse();
      if (!condConstant)
        std::swap(executed, skipped);

      if (s.isConstexpr() || !containsLabel(skipped)) {
        if (condConstant)
          incrementProfileCounter(&s);
        if (executed) {
          maybeEmitDeferredVarDeclInit(s.getConditionVariable());
          return emitStmt(executed, /*useCurrentScope=*/true);
        }
        return mlir::success();
      }
    }

    // Compute condition likelihood from [[likely]]/[[unlikely]] attributes
    // on then/else branches. Only applied when PGO weights are not available.
    Stmt::Likelihood LH = Stmt::getLikelihood(s.getThen(), s.getElse());

    // OGCG has a separate path for MC/DC coverage that evaluates the condition
    // as a bool before branching, to update the test vector bitmap.
    assert(!cir::MissingFeatures::mcdcCoverageFolding());

    // For C++26 structured bindings as condition variables (P0963R3), the
    // holding vars (get<I>() results) must be emitted AFTER the condition
    // is evaluated (operator bool / operator int) but BEFORE the if body.
    // OGCG does this via ConditionalDecl parameter to EmitBranchOnBoolExpr.
    if (const VarDecl *condVar = s.getConditionVariable()) {
      mlir::Location thenLoc = getLoc(s.getThen()->getSourceRange());
      std::optional<mlir::Location> elseLoc;
      if (s.getElse())
        elseLoc = getLoc(s.getElse()->getSourceRange());

      SmallVector<mlir::Location, 2> ifLocs{thenLoc};
      if (elseLoc)
        ifLocs.push_back(*elseLoc);
      mlir::Location loc = mlir::FusedLoc::get(&getMLIRContext(), ifLocs);

      mlir::Value condV = emitOpOnBoolExpr(loc, s.getCond());
      condV = emitCondLikelihoodViaExpectIntrinsic(condV, LH);
      closeActiveSyntheticTry();
      maybeEmitDeferredVarDeclInit(condVar);

      mlir::LogicalResult resThen = mlir::success();
      mlir::LogicalResult resElse = mlir::success();
      uint64_t branchAtom = reserveSourceAtomGroup();
      auto ifOp = cir::IfOp::create(
          builder, loc, condV, elseLoc.has_value(),
          /*thenBuilder=*/
          [&](mlir::OpBuilder &, mlir::Location) {
            LexicalScope lexScope{*this, thenLoc,
                                  builder.getInsertionBlock()};
            // Record the then-branch source range so that a goto to a label
            // inside this branch is recognized as staying within the scope.
            // Without this, emitGotoStmt's forward-goto walk sees an invalid
            // source range and assumes the goto exits this scope, incorrectly
            // emitting a branch-local's cleanup and double-destroying it on the
            // goto path. Sibling of the fix in emitIfOnBoolExpr.
            lexScope.setClangSourceRange(s.getThen()->getSourceRange());
            incrementProfileCounter(&s);
            resThen = emitStmt(s.getThen(), /*useCurrentScope=*/true);
          },
          /*elseBuilder=*/
          [&](mlir::OpBuilder &, mlir::Location) {
            assert(elseLoc && "Invalid location for elseS.");
            LexicalScope lexScope{*this, *elseLoc,
                                  builder.getInsertionBlock()};
            lexScope.setClangSourceRange(s.getElse()->getSourceRange());
            resElse = emitStmt(s.getElse(), /*useCurrentScope=*/true);
          });
      addOpToSpecificSourceAtom(ifOp, condV, branchAtom);
      if (hlslControlFlowHint)
        ifOp->setAttr(
            "cir.hlsl.controlflow.hint",
            mlir::IntegerAttr::get(mlir::IntegerType::get(&getMLIRContext(), 32),
                                   *hlslControlFlowHint));
      return mlir::LogicalResult::success(resThen.succeeded() &&
                                          resElse.succeeded());
    }

    return emitIfOnBoolExpr(s.getCond(), s.getThen(), s.getElse(), LH,
                            hlslControlFlowHint, /*pgoCounterStmt=*/&s);
  };

  // TODO: Add a new scoped symbol table.
  // LexicalScope ConditionScope(*this, S.getCond()->getSourceRange());
  // The if scope contains the full source range for IfStmt.
  mlir::Location scopeLoc = getLoc(s.getSourceRange());

  // Skip the cir.scope wrapper for simple if-statements that don't need
  // scoping (no init-statement, no condition variable, not consteval).
  // This avoids creating a ScopeOp + Region + Block per if-statement,
  // reducing MLIR operation count and downstream pass overhead (~6%).
  if (!s.getInit() && !s.getConditionVariable() && !s.isConsteval()) {
    LexicalScope lexScope{*this, scopeLoc, builder.getInsertionBlock()};
    lexScope.setCleanupOnly();
    lexScope.setClangSourceRange(s.getSourceRange());
    res = ifStmtBuilder();
  } else {
    cir::ScopeOp::create(builder, scopeLoc, /*scopeBuilder=*/
                         [&](mlir::OpBuilder &b, mlir::Location loc) {
                           LexicalScope lexScope{*this, scopeLoc,
                                                 builder.getInsertionBlock()};
                           // Record the IfStmt's source range so that a goto to
                           // a label inside the if-statement (e.g. in the else
                           // branch) is recognized as staying within this
                           // scope. Without this, emitGotoStmt's forward-goto
                           // walk sees an invalid source range and assumes the
                           // goto exits this scope, incorrectly emitting the
                           // condition variable's cleanup and double-destroying
                           // it on the goto path.
                           lexScope.setClangSourceRange(s.getSourceRange());
                           res = ifStmtBuilder();
                         });
  }

  return res;
}

mlir::LogicalResult CIRGenFunction::emitDeclStmt(const DeclStmt &s) {
  assert(builder.getInsertionBlock() && "expected valid insertion point");
  // OGCG checks HaveInsertPoint() and calls EmitStopPoint here.

  for (const Decl *i : s.decls())
    emitDecl(*i, /*evaluateConditionDecl=*/true);

  return mlir::success();
}

/// Determine if the given call uses the swiftasync calling convention.
static bool isSwiftAsyncCallee(const CallExpr *CE) {
  auto calleeQualType = CE->getCallee()->getType();
  const FunctionType *calleeType = nullptr;
  if (calleeQualType->isFunctionPointerType() ||
      calleeQualType->isFunctionReferenceType() ||
      calleeQualType->isBlockPointerType() ||
      calleeQualType->isMemberFunctionPointerType()) {
    calleeType = calleeQualType->getPointeeType()->castAs<FunctionType>();
  } else if (auto *ty = dyn_cast<FunctionType>(calleeQualType)) {
    calleeType = ty;
  } else if (auto CMCE = dyn_cast<CXXMemberCallExpr>(CE)) {
    if (auto methodDecl = CMCE->getMethodDecl()) {
      calleeType = methodDecl->getType()->castAs<FunctionType>();
    } else {
      return false;
    }
  } else {
    return false;
  }
  return calleeType->getCallConv() == clang::CallingConv::CC_SwiftAsync;
}

mlir::LogicalResult CIRGenFunction::emitReturnStmt(const ReturnStmt &s) {
  uint64_t returnAtom = reserveSourceAtomGroup();
  mlir::Location loc = getLoc(s.getSourceRange());
  mlir::Location retLoc = getLoc(s.getBeginLoc());
  const Expr *rv = s.getRetValue();
  const auto *curFD = dyn_cast_or_null<FunctionDecl>(curCodeDecl);
  QualType curReturnType = curFnRetTy;

  if (requiresReturnValueCheck())
    setReturnValueCheckSourceLocation(s.getBeginLoc());
  // OGCG emits unreachable for returns from outlined SEH helpers.
  // Windows SEH is not a supported CIR target.
  // OGCG uses SaveRetExprRAII for ObjC block capture lifetime management.
  // CIR tracks this directly in the return-value emission/cleanup model.
  // returnValueValidCheck: OGCG checks ReturnValue.isValid() and emits for
  // side effects only when it's not valid (void return). CIR handles this via
  // AST-level type checks (!rv and rv->getType()->isVoidType()) in
  // handleReturnVal below, which is structurally different but semantically
  // equivalent — void functions never allocate fnRetAlloca.

  // Record the result expression of the return statement (before unwrapping any
  // ExprWithCleanups). It is consulted by emitBlockLiteral to decide whether a
  // block capture's lifetime should end at the end of the full expression as
  // opposed to the end of the scope enclosing the block expression. Mirrors
  // OGCG's SaveRetExprRAII (CGStmt.cpp).
  llvm::SaveAndRestore<const Expr *> saveRetExpr(retExpr, rv);

  RunCleanupsScope cleanupScope(*this);
  bool createNewScope = false;
  if (const auto *ewc = dyn_cast_or_null<ExprWithCleanups>(rv)) {
    rv = ewc->getSubExpr();
    createNewScope = true;
  }

  // If we're in a swiftasynccall function, and the return expression is a
  // call to a swiftasynccall function, mark the call as the musttail call.
  // This matches OGCG's behavior in EmitReturnStmt (CGStmt.cpp).
  std::optional<llvm::SaveAndRestore<const CallExpr *>> saveMustTail;
  if (rv && curFD) {
    const auto *curFnType = curFD->getType()->castAs<FunctionType>();
    if (curFnType->getCallConv() == clang::CallingConv::CC_SwiftAsync) {
      if (const auto *ce = dyn_cast<CallExpr>(rv)) {
        if (isSwiftAsyncCallee(ce))
          saveMustTail.emplace(mustTailCall, ce);
      }
    }
  }

  // For simple top-level returns, we can return the computed SSA value
  // directly and avoid the synthetic __retval spill/reload sequence.
  // Keep this conservative: only when the current lexical scope is the
  // function body and there are no enclosing cleanups to thread through.
  const auto *retCall = rv ? dyn_cast<CallExpr>(rv->IgnoreParenImpCasts())
                           : nullptr;
  bool canTryDirectReturn =
      rv && curFD && fnRetAlloca && !createNewScope && !mustTailCall &&
      !isCoroutine() && activeObjCFinallyScopes.empty() && curLexScope &&
      curLexScope->depth == 0 && !curLexScope->getParentScope() &&
      !ehStack.hasNormalCleanups();
  bool canTryDirectReferenceReturn =
      canTryDirectReturn && !curReturnType.isNull() &&
      curReturnType->isReferenceType();
  bool canTryDirectScalarReturn =
      canTryDirectReturn && !curFD->getReturnType()->isReferenceType() &&
      rv->getType()->isIntegerType() && retCall && retCall->getNumArgs() == 0 &&
      retCall->getDirectCallee() && !retCall->getBuiltinCallee() &&
      !retCall->getDirectCallee()->getPrimaryTemplate() &&
      CIRGenFunction::getEvaluationKind(rv->getType()) == cir::TEK_Scalar;
  mlir::Value directRetValue = nullptr;

  bool mustTailHandled = false;
  auto handleReturnVal = [&]() {
    if (getContext().getLangOpts().ElideConstructors && s.getNRVOCandidate() &&
        s.getNRVOCandidate()->isNRVOVariable()) {
      // Apply the named return value optimization for this return statement,
      // which means doing nothing: the appropriate result has already been
      // constructed into the NRVO variable.

      // If there is an NRVO flag for this variable, set it to 1 into indicate
      // that the cleanup code should not destroy the variable.
      if (auto nrvoFlag = nrvoFlags[s.getNRVOCandidate()])
        builder.createFlagStore(loc, true, nrvoFlag);
    } else if (!rv) {
      // No return expression. Do nothing.
    } else if (rv->getType()->isVoidType()) {
      // Make sure not to return anything, but evaluate the expression
      // for side effects.
      emitAnyExpr(rv);
    } else if (!curReturnType.isNull() && curReturnType->isReferenceType()) {
      // If this function returns a reference, take the address of the
      // expression rather than the value.
      RValue result = emitReferenceBindingToExpr(rv);
      mlir::Value refVal = result.getValue();
      mlir::Type fnRetTy =
          cast<cir::FuncOp>(curFn).getFunctionType().getReturnType();
      if (canTryDirectReferenceReturn && refVal.getType() != fnRetTy) {
        bool canReturnDirect = true;
        if (refVal.getType() != fnRetTy &&
            mlir::isa<cir::PointerType>(refVal.getType()) &&
            mlir::isa<cir::PointerType>(fnRetTy))
          refVal = castPointerForReturn(builder, loc, refVal, fnRetTy);
        else if (refVal.getType() != fnRetTy)
          canReturnDirect = false;

        if (canReturnDirect)
          directRetValue = refVal;
      }

      if (directRetValue)
        return;

      // CIR uses typed pointers so we may need a bitcast when the
      // reference value's pointer type doesn't match the return alloca's
      // pointee type.  In LLVM IR this is implicit (opaque ptr).
      mlir::Type retPointeeTy =
          mlir::cast<cir::PointerType>(fnRetAlloca->getType()).getPointee();
      if (refVal.getType() != retPointeeTy &&
          mlir::isa<cir::PointerType>(refVal.getType()) &&
          mlir::isa<cir::PointerType>(retPointeeTy))
        refVal = castPointerForReturn(builder, loc, refVal, retPointeeTy);
      auto storeOp =
          builder.CIRBaseBuilderTy::createStore(loc, refVal, *fnRetAlloca);
      cgm.decorateOperationWithTBAA(storeOp,
                                    cgm.getTBAAAccessInfo(curReturnType));
      addOpToSpecificSourceAtom(storeOp, refVal, returnAtom);
    } else {
      mlir::Value value = nullptr;
      switch (CIRGenFunction::getEvaluationKind(rv->getType())) {
      case cir::TEK_Scalar:
        value = emitScalarExpr(rv);
        if (value) { // Change this to an assert once emitScalarExpr is complete
          if (canTryDirectScalarReturn) {
            mlir::Type fnRetTy =
                cast<cir::FuncOp>(curFn).getFunctionType().getReturnType();
            bool canReturnDirect = true;
            if (value.getType() != fnRetTy &&
                mlir::isa<cir::PointerType>(value.getType()) &&
                mlir::isa<cir::PointerType>(fnRetTy))
              value = castPointerForReturn(builder, loc, value, fnRetTy);
            else if (value.getType() != fnRetTy)
              canReturnDirect = false;

            if (canReturnDirect)
              directRetValue = value;
          }

          if (!directRetValue) {
            // CIR uses typed pointers — when the scalar value's pointer type
            // doesn't match the return alloca's pointee type, insert a
            // bitcast. In LLVM IR this is implicit (opaque ptr).
            mlir::Type retPointeeTy =
                mlir::cast<cir::PointerType>(fnRetAlloca->getType())
                    .getPointee();
            if (value.getType() != retPointeeTy &&
                mlir::isa<cir::PointerType>(value.getType()) &&
                mlir::isa<cir::PointerType>(retPointeeTy))
              value = castPointerForReturn(builder, loc, value, retPointeeTy);
            auto storeOp =
                builder.CIRBaseBuilderTy::createStore(loc, value, *fnRetAlloca);
            cgm.decorateOperationWithTBAA(storeOp,
                                          cgm.getTBAAAccessInfo(rv->getType()));
            addOpToSpecificSourceAtom(storeOp, value, returnAtom);
          }
        }
        break;
      case cir::TEK_Complex:
        emitComplexExprIntoLValue(rv,
                                  makeAddrLValue(returnValue, rv->getType()),
                                  /*isInit=*/true);
        break;
      case cir::TEK_Aggregate:
        {
          llvm::SaveAndRestore<uint64_t> saveCurrentAtom(
              sourceAtomInfo.currentAtom);
          std::optional<SourceLocRAIIObject> returnExprLoc;
          if (returnAtom)
            returnExprLoc.emplace(*this, getLoc(rv->getBeginLoc()));
          sourceAtomInfo.currentAtom = returnAtom;
          emitAggExpr(rv, AggValueSlot::forAddr(returnValue, Qualifiers(),
                                                AggValueSlot::IsDestructed,
                                                AggValueSlot::IsNotAliased,
                                                getOverlapForReturnValue()));
        }
        break;
      }
      // For musttail calls, emitCall already emitted cir.return directly
      // after the call and created a dead block. This handles all return
      // types uniformly (scalar, complex, aggregate). The dead code emitted
      // above (stores to __retval) is harmless and gets cleaned up.
      if (mustTailCall) {
        mustTailHandled = true;
        return;
      }
    }
  };

  if (!createNewScope) {
    handleReturnVal();
  } else {
    mlir::Location scopeLoc =
        getLoc(rv ? rv->getSourceRange() : s.getSourceRange());
    // First create cir.scope and later emit it's body. Otherwise all CIRGen
    // dispatched by `handleReturnVal()` might needs to manipulate blocks and
    // look into parents, which are all unlinked.
    mlir::OpBuilder::InsertPoint scopeBody;
    cir::ScopeOp::create(builder, scopeLoc, /*scopeBuilder=*/
                         [&](mlir::OpBuilder &b, mlir::Location loc) {
                           scopeBody = b.saveInsertionPoint();
                         });
    {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.restoreInsertionPoint(scopeBody);
      CIRGenFunction::LexicalScope lexScope{*this, scopeLoc,
                                            builder.getInsertionBlock()};
      handleReturnVal();
    }
  }

  // Musttail already emitted cir.return directly — skip the rest.
  if (mustTailHandled)
    return mlir::success();

  // If the return expression unexpectedly introduced cleanups, fall back to
  // the legacy __retval path so the value survives across cleanup emission.
  if (directRetValue &&
      ehStack.stable_begin() != cleanupScope.getCleanupStackDepth()) {
    mlir::Type retPointeeTy =
        mlir::cast<cir::PointerType>(fnRetAlloca->getType()).getPointee();
    mlir::Value spillVal = directRetValue;
    if (spillVal.getType() != retPointeeTy &&
        mlir::isa<cir::PointerType>(spillVal.getType()) &&
        mlir::isa<cir::PointerType>(retPointeeTy))
      spillVal = castPointerForReturn(builder, loc, spillVal, retPointeeTy);
    auto storeOp =
        builder.CIRBaseBuilderTy::createStore(loc, spillVal, *fnRetAlloca);
    addOpToSpecificSourceAtom(storeOp, spillVal, returnAtom);
    directRetValue = nullptr;
  }

  cleanupScope.forceCleanup();

  if (!activeObjCFinallyScopes.empty()) {
    bool finallyOverrodeReturn = false;
    if (emitActiveObjCFinallyBodiesForReturn(finallyOverrodeReturn).failed())
      return mlir::failure();
    if (finallyOverrodeReturn)
      return mlir::success();
  }

  if (directRetValue)
    emitReturnValueCheck(retLoc, directRetValue);

  if (directRetValue && autoreleaseResult)
    directRetValue = emitARCAutoreleaseReturnValue(directRetValue, retLoc);

  if (directRetValue) {
    auto retOp =
        cir::ReturnOp::create(builder, retLoc, llvm::ArrayRef(directRetValue));
    addOpToSpecificSourceAtom(retOp, directRetValue, returnAtom);
    builder.createBlock(builder.getBlock()->getParent());
    return mlir::success();
  }

  // Emit cleanups for RAII objects in scope before returning from a nested
  // scope. When returning from depth > 0, we emit ALL cleanup destructors
  // inline in correct LIFO order (innermost first) and emit cir.return
  // directly — bypassing the retBlock/cleanup-block-threading mechanism
  // which would cause double emission.
  bool emittedEarlyCleanups = false;
  if (curLexScope && curLexScope->depth > 0 &&
      curLexScope->getParentScope()) {
    EHScopeStack::stable_iterator targetDepth =
        curLexScope->getCleanupStackDepth();
    for (LexicalScope *scope = curLexScope->getParentScope(); scope;
         scope = scope->getParentScope()) {
      auto scopeDepth = scope->getCleanupStackDepth();
      if (scopeDepth != targetDepth)
        targetDepth = scopeDepth;
    }
    // Emit cleanups if the ehStack has any normal cleanups between the
    // innermost scope and the target depth. This covers both:
    // - Parent scope cleanups (targetDepth differs from curLexScope depth)
    // - Current scope cleanups pushed after scope creation (e.g., RAII
    // objects in catch handlers —)
    if (ehStack.stable_begin() != targetDepth) {
      emitCleanupsForEarlyExit(targetDepth);
      emittedEarlyCleanups = true;
    }
  }

  if (emittedEarlyCleanups) {
    // All cleanups emitted inline in LIFO order. Emit cir.return directly
    // instead of using a retBlock — LexicalScope::cleanup() would thread
    // the retBlock through cleanup blocks, causing double emission of
    // current-scope destructors.
    auto fn = cast<cir::FuncOp>(curFn);
    if (!fn.getFunctionType().hasVoidReturn()) {
      auto value = cir::LoadOp::create(
          builder, retLoc, fn.getFunctionType().getReturnType(), *fnRetAlloca);
      if (requiresReturnValueCheck())
        emitReturnValueCheck(retLoc, value.getResult());
      mlir::Value retValue = value.getResult();
      if (autoreleaseResult)
        retValue = emitARCAutoreleaseReturnValue(retValue, retLoc);
      cir::ReturnOp::create(builder, retLoc, llvm::ArrayRef(retValue));
    } else {
      cir::ReturnOp::create(builder, retLoc);
    }
  } else {
    // In CIR we might have returns in different scopes. Walk past cleanup-only
    // scopes (label sub-statements) to create the retBlock in the nearest
    // scope that processes retBlocks in its cleanup. Cleanup-only scopes don't
    // emit cir.return into retBlocks, leaving them empty and unterminated.
    LexicalScope *retScope = curLexScope;
    while (retScope->isCleanupOnly() && retScope->getParentScope())
      retScope = retScope->getParentScope();
    auto *retBlock =
        retScope->getOrCreateRetBlock(*this, applySourceAtomMetadata(
                                                 retLoc, returnAtom, 1));
    // Save the parent region before emitBranchThroughCleanup clears the
    // insertion point (matching OGCG's Builder.ClearInsertionPoint()).
    mlir::Region *parentRegion = builder.getBlock()->getParent();
    emitBranchThroughCleanup(retLoc, returnBlock(retBlock));
    // Insert the new block to continue codegen after branch to ret block.
    builder.createBlock(parentRegion);
  }

  // Insert the new block to continue codegen after the return/branch.
  // For the early cleanup path, we already have a terminator (cir.return)
  // and need a new block. For the branch path, createBlock was called above.
  if (emittedEarlyCleanups)
    builder.createBlock(builder.getBlock()->getParent());

  return mlir::success();
}

mlir::LogicalResult CIRGenFunction::emitGotoStmt(const clang::GotoStmt &s) {
  // FIXME: LLVM codegen inserts emit a stop point here for debug info
  // sake when the insertion point is available, but doesn't do
  // anything special when there isn't. We haven't implemented debug
  // info support just yet, look at this again once we have it.

  // Emit cleanups only for scopes the goto actually exits. A same-scope
  // goto (both goto and label in the same scope) should not trigger any
  // cleanup; a scope-exiting goto should destroy RAII objects in all
  // scopes being exited.
  const clang::LabelDecl *labelDecl = s.getLabel();
  EHScopeStack::stable_iterator targetDepth = ehStack.stable_begin();

  JumpDest dest = getJumpDestForLabel(labelDecl);
  if (dest.getScopeDepth().isValid()) {
    // Backward goto: label already emitted, use its recorded scope depth.
    targetDepth = dest.getScopeDepth();
  } else {
    // Forward goto: label not yet emitted. Walk the LexicalScope chain
    // and use source ranges to determine which scopes the goto exits.
    // A scope whose source range contains the label is NOT exited.
    // A scope whose source range does NOT contain the label (or has no
    // source range info) IS exited by the goto.
    //
    // NOTE: This source-range approach diverges from OGCG, which uses
    // EmitBranchThroughCleanup(getJumpDestForLabel(S.getLabel())) to
    // defer cleanup emission entirely. CIR cannot fully defer because
    // GotoOp is name-based (cross-region) and cleanups are emitted inline.
    // CIR now has structural tracking (labelMap, rescopeLabels) but still
    // uses this heuristic for actual cleanup determination.
    assert(!cir::MissingFeatures::forwardGotoDeferredCleanup());
    clang::SourceLocation labelLoc = labelDecl->getLocation();
    const clang::SourceManager &sm = getContext().getSourceManager();

    for (LexicalScope *scope = curLexScope; scope;
         scope = scope->getParentScope()) {
      clang::SourceRange sr = scope->getClangSourceRange();
      if (sr.isValid()) {
        // Check if the label is within this scope's source range.
        if (!sm.isBeforeInTranslationUnit(labelLoc, sr.getBegin()) &&
            sm.isBeforeInTranslationUnit(labelLoc, sr.getEnd())) {
          // Label is inside this scope — goto does not exit it.
          break;
        }
      }
      // Label is not in this scope (or scope has no source range info).
      // The goto exits this scope — emit cleanups to scope's entry depth.
      targetDepth = scope->getCleanupStackDepth();
    }
  }

  if (targetDepth != ehStack.stable_begin())
    emitCleanupsForEarlyExit(targetDepth);

  bool finallyOverrodeGoto = false;
  if (emitActiveObjCFinallyBodiesForGoto(labelDecl, finallyOverrodeGoto)
          .failed())
    return mlir::failure();
  if (finallyOverrodeGoto)
    return mlir::success();

  auto gotoLabelName = cgm.getBlockAddressLabelName(*s.getLabel());
  auto gotoOp =
      cir::GotoOp::create(builder, getLoc(s.getSourceRange()), gotoLabelName);
  addOpToNewSourceAtom(gotoOp);

  // A goto marks the end of a block, create a new one for codegen after
  // emitGotoStmt can resume building in that block.
  // Insert the new block to continue codegen after goto.
  builder.createBlock(builder.getBlock()->getParent());

  return mlir::success();
}

mlir::LogicalResult
CIRGenFunction::emitIndirectGotoStmt(const IndirectGotoStmt &s) {
  // OGCG optimizes indirect goto with a statically known target
  // (s.getConstantTarget()) into a direct branch through cleanup.
  // Match that optimization: when the target is a constant label
  // (e.g. goto *&&label), emit a direct cir::GotoOp instead of
  // cir::IndirectGotoOp.
  if (const LabelDecl *target = s.getConstantTarget()) {
    auto gotoLabelName = cgm.getBlockAddressLabelName(*target);
    auto gotoOp = cir::GotoOp::create(builder, getLoc(s.getSourceRange()),
                                      gotoLabelName);
    addOpToNewSourceAtom(gotoOp);
    builder.createBlock(builder.getBlock()->getParent());
    return mlir::success();
  }

  mlir::Value val = emitScalarExpr(s.getTarget());
  instantiateIndirectGotoBlock();
  assert(indirectGotoBlock &&
         "indirect goto block should have been created");
  hasIndirectGoto = true;
  auto indirectGotoOp =
      cir::IndirectGotoOp::create(builder, getLoc(s.getSourceRange()), val);
  addOpToNewSourceAtom(indirectGotoOp);
  builder.createBlock(builder.getBlock()->getParent());
  return mlir::success();
}

mlir::LogicalResult
CIRGenFunction::emitContinueStmt(const clang::ContinueStmt &s) {
  // OGCG checks HaveInsertPoint() and calls EmitStopPoint here.
  if (s.hasLabelTarget()) {
    auto *target = getNamedLoopControlTarget(s.getNamedLoopOrSwitch());
    assert(!target->continueLabel.empty() && "named continue target missing");
    return emitGotoWithCleanup(getLoc(s.getSourceRange()),
                               target->continueLabel,
                               target->continueTargetDepth, target->scope);
  }

  // Emit cleanups for RAII objects in scope before continuing the loop.
  // Walk up to the innermost loop scope — continue stays within the loop,
  // so we only destroy objects constructed inside the loop body.
  //
  // For for-loops with a condition variable (e.g. for (; R r = R(v--); )),
  // use continueTargetDepth to exclude the condition variable's cleanup.
  // Continue goes to the step region, which already has the dtor.
  // Without this, the condition variable is destroyed twice.
  EHScopeStack::stable_iterator targetDepth = ehStack.stable_begin();
  LexicalScope *targetScope = nullptr;
  for (LexicalScope *scope = curLexScope; scope;
       scope = scope->getParentScope()) {
    if (scope->isLoop()) {
      targetScope = scope;
      targetDepth = scope->getContinueTargetDepth().value_or(
          scope->getCleanupStackDepth());
      break;
    }
  }
  if (targetDepth != ehStack.stable_begin())
    emitCleanupsForEarlyExit(targetDepth);

  bool finallyOverrodeContinue = false;
  if (emitActiveObjCFinallyBodiesForBreakContinue(targetScope,
                                                  finallyOverrodeContinue)
          .failed())
    return mlir::failure();
  if (finallyOverrodeContinue)
    return mlir::success();

  auto continueOp = builder.createContinue(getLoc(s.getKwLoc()));
  addOpToNewSourceAtom(continueOp);

  // Insert the new block to continue codegen after the continue statement.
  builder.createBlock(builder.getBlock()->getParent());

  return mlir::success();
}

mlir::LogicalResult CIRGenFunction::emitLabel(const clang::LabelDecl &d) {
  auto func = cast<cir::FuncOp>(curFn);
  auto blockAddrLabelName = cgm.getBlockAddressLabelName(d);
  auto blockAddrInfo = cir::BlockAddrInfoAttr::get(
      builder.getContext(), func.getSymNameAttr(),
      cgm.getStringAttrCached(blockAddrLabelName));

  // Labels may be emitted while wiring short-circuit CFG for surrounding
  // boolean conditions (emitIfOnBoolExpr) before control reaches the actual
  // LabelStmt. In that case, emitLabel can be called repeatedly for the same
  // LabelDecl. Match OGCG semantics by treating already-emitted labels as a
  // no-op.
  if (cgm.lookupBlockAddressInfo(blockAddrInfo))
    return mlir::success();

  auto labelIt = labelMap.find(&d);
  if (labelIt != labelMap.end() && labelIt->second.getScopeDepth().isValid())
    return mlir::success();

  // Create a new block to tag with a label and add a branch from
  // the current one to it. If the block is empty just call attach it
  // to this label.
  mlir::Block *currBlock = builder.getBlock();
  mlir::Block *labelBlock = currBlock;

  if (!currBlock->empty() || currBlock->isEntryBlock()) {
    {
      mlir::OpBuilder::InsertionGuard guard(builder);
      labelBlock = builder.createBlock(builder.getBlock()->getParent());
    }
    cir::BrOp::create(builder, getLoc(d.getSourceRange()), labelBlock);
  }

  builder.setInsertionPointToEnd(labelBlock);
  cir::LabelOp label =
      cir::LabelOp::create(builder, getLoc(d.getSourceRange()),
                           blockAddrLabelName);
  builder.setInsertionPointToEnd(labelBlock);
  cgm.mapBlockAddress(blockAddrInfo, label);

  // Record the cleanup depth at this label. For forward-referenced labels
  // (goto appeared before label), update the existing JumpDest with the
  // now-known scope depth. Otherwise create a new entry.
  if (labelIt != labelMap.end()) {
    assert(!labelIt->second.getScopeDepth().isValid() &&
           "label already emitted!");
    labelIt->second.setScopeDepth(ehStack.stable_begin());
  } else {
    labelMap[&d] = getJumpDestInCurrentScope(labelBlock);
  }

  // Add this label to the current lexical scope so hasLabelBeenSeenInCurrentScope
  // can detect backward jumps in C code (OGCG CGStmt.cpp:721-726).
  if (curLexScope)
    curLexScope->addLabel(&d);

  //  FIXME: emit debug info for labels, incrementProfileCounter
  return mlir::success();
}

mlir::LogicalResult CIRGenFunction::emitBreakStmt(const clang::BreakStmt &s) {
  // OGCG checks HaveInsertPoint() and calls EmitStopPoint here.
  if (s.hasLabelTarget()) {
    auto *target = getNamedLoopControlTarget(s.getNamedLoopOrSwitch());
    return emitGotoWithCleanup(getLoc(s.getSourceRange()), target->breakLabel,
                               target->breakTargetDepth,
                               target->scope ? target->scope->getParentScope()
                                             : nullptr);
  }

  // Emit cleanups for RAII objects before breaking. Walk up the LexicalScope
  // chain to find the innermost switch or loop scope — that is the scope the
  // break exits. We must only destroy objects constructed INSIDE that scope's
  // body, not objects in enclosing scopes.
  //
  // If the loop has a condition variable with a non-trivial destructor
  // (e.g. while (R r = R(v--))), use breakTargetDepth to exclude the
  // condition variable's cleanup from inline emission. The scope exit
  // cleanup handles the condition variable on the break path. Without this,
  // the condition variable is destroyed twice.
  EHScopeStack::stable_iterator targetDepth = ehStack.stable_begin();
  LexicalScope *targetScope = nullptr;
  for (LexicalScope *scope = curLexScope; scope;
       scope = scope->getParentScope()) {
    if (scope->isSwitch() || scope->isLoop()) {
      targetScope = scope;
      targetDepth = scope->getBreakTargetDepth().value_or(
          scope->getCleanupStackDepth());
      break;
    }
  }
  if (targetDepth != ehStack.stable_begin())
    emitCleanupsForEarlyExit(targetDepth);

  bool finallyOverrodeBreak = false;
  if (emitActiveObjCFinallyBodiesForBreakContinue(targetScope,
                                                  finallyOverrodeBreak)
          .failed())
    return mlir::failure();
  if (finallyOverrodeBreak)
    return mlir::success();

  auto breakOp = builder.createBreak(getLoc(s.getKwLoc()));
  addOpToNewSourceAtom(breakOp);

  // Insert the new block to continue codegen after the break statement.
  builder.createBlock(builder.getBlock()->getParent());

  return mlir::success();
}

template <typename T>
mlir::LogicalResult
CIRGenFunction::emitCaseDefaultCascade(const T *stmt, mlir::Type condType,
                                       mlir::ArrayAttr value, CaseOpKind kind,
                                       mlir::ArrayAttr likelihoods,
                                       bool buildingTopLevelCase) {

  assert((isa<CaseStmt, DefaultStmt>(stmt)) &&
         "only case or default stmt go here");

  mlir::LogicalResult result = mlir::success();

  mlir::Location loc = getLoc(stmt->getBeginLoc());

  enum class SubStmtKind { Case, Default, Other };
  SubStmtKind subStmtKind = SubStmtKind::Other;
  const Stmt *sub = stmt->getSubStmt();

  mlir::OpBuilder::InsertPoint insertPoint;
  CaseOp::create(builder, loc, value, kind, insertPoint);
  auto caseOp = mlir::cast<CaseOp>(insertPoint.getBlock()->getParentOp());
  caseOp->setAttr(kCaseLikelihoodsAttr, likelihoods);

  {
    mlir::OpBuilder::InsertionGuard guardSwitch(builder);
    builder.restoreInsertionPoint(insertPoint);

    if (isa<DefaultStmt>(sub) && isa<CaseStmt>(stmt)) {
      subStmtKind = SubStmtKind::Default;
      builder.createYield(loc);
    } else if (isa<CaseStmt>(sub) && isa<DefaultStmt, CaseStmt>(stmt)) {
      subStmtKind = SubStmtKind::Case;
      builder.createYield(loc);
    } else {
      incrementProfileCounter(stmt);
      result = emitStmt(sub, /*useCurrentScope=*/!isa<CompoundStmt>(sub));
    }

    insertPoint = builder.saveInsertionPoint();
  }

  // If the substmt is default stmt or case stmt, try to handle the special case
  // to make it into the simple form. e.g.
  //
  //  swtich () {
  //    case 1:
  //    default:
  //      ...
  //  }
  //
  // we prefer generating
  //
  //  cir.switch() {
  //     cir.case(equal, 1) {
  //        cir.yield
  //     }
  //     cir.case(default) {
  //        ...
  //     }
  //  }
  //
  // than
  //
  //  cir.switch() {
  //     cir.case(equal, 1) {
  //       cir.case(default) {
  //         ...
  //       }
  //     }
  //  }
  //
  // We don't need to revert this if we find the current switch can't be in
  // simple form later since the conversion itself should be harmless.
  if (subStmtKind == SubStmtKind::Case) {
    result = emitCaseStmt(*cast<CaseStmt>(sub), condType, buildingTopLevelCase,
                          /*attrs=*/{});
  } else if (subStmtKind == SubStmtKind::Default) {
    result = emitDefaultStmt(*cast<DefaultStmt>(sub), condType,
                             buildingTopLevelCase, /*attrs=*/{});
  } else if (buildingTopLevelCase) {
    // If we're building a top level case, try to restore the insert point to
    // the case we're building, then we can attach more random stmts to the
    // case to make generating `cir.switch` operation to be a simple form.
    builder.restoreInsertionPoint(insertPoint);
  }

  return result;
}

const CaseStmt *CIRGenFunction::foldCaseStmt(const clang::CaseStmt &s,
                                             mlir::Type condType,
                                             mlir::ArrayAttr &value,
                                             mlir::ArrayAttr &likelihoods,
                                             Stmt::Likelihood topLikelihood,
                                             cir::CaseOpKind &kind) {
  const CaseStmt *caseStmt = &s;
  const CaseStmt *lastCase = &s;
  SmallVector<mlir::Attribute, 4> caseEltValueListAttr;
  SmallVector<Stmt::Likelihood, 4> caseLikelihoods;
  Stmt::Likelihood currentLikelihood = topLikelihood;

  // Fold cascading cases whenever possible to simplify codegen a bit.
  while (caseStmt) {
    lastCase = caseStmt;

    auto intVal = caseStmt->getLHS()->EvaluateKnownConstInt(getContext());
    unsigned condWidth = mlir::cast<cir::IntType>(condType).getWidth();

    if (auto *rhs = caseStmt->getRHS()) {
      auto endVal = rhs->EvaluateKnownConstInt(getContext());
      SmallVector<mlir::Attribute, 4> rangeCaseAttr = {
          cir::IntAttr::get(condType, intVal.extOrTrunc(condWidth)),
          cir::IntAttr::get(condType, endVal.extOrTrunc(condWidth))};
      value = builder.getArrayAttr(rangeCaseAttr);
      caseLikelihoods.push_back(currentLikelihood);
      likelihoods = getCaseLikelihoodsAttr(&getMLIRContext(), caseLikelihoods);
      kind = cir::CaseOpKind::Range;

      // We may not be able to fold ranges. Due to we can't present range case
      // with other trivial cases now.
      return caseStmt;
    }

    caseEltValueListAttr.push_back(
        cir::IntAttr::get(condType, intVal.extOrTrunc(condWidth)));
    caseLikelihoods.push_back(currentLikelihood);
    currentLikelihood = Stmt::LH_None;

    caseStmt = dyn_cast_or_null<CaseStmt>(caseStmt->getSubStmt());

    // Break early if we found ranges. We can't fold ranges due to the same
    // reason above.
    if (caseStmt && caseStmt->getRHS())
      break;
  }

  if (!caseEltValueListAttr.empty()) {
    value = builder.getArrayAttr(caseEltValueListAttr);
    likelihoods = getCaseLikelihoodsAttr(&getMLIRContext(), caseLikelihoods);
    kind = caseEltValueListAttr.size() > 1 ? cir::CaseOpKind::Anyof
                                           : cir::CaseOpKind::Equal;
  }

  return lastCase;
}

mlir::LogicalResult CIRGenFunction::emitCaseStmt(const CaseStmt &s,
                                                 mlir::Type condType,
                                                 bool buildingTopLevelCase,
                                                 llvm::ArrayRef<const Attr *>
                                                     attrs) {
  mlir::ArrayAttr value;
  mlir::ArrayAttr likelihoods;
  CaseOpKind kind;
  auto *caseStmt =
      foldCaseStmt(s, condType, value, likelihoods,
                   getFirstCaseLikelihood(attrs, s.getSubStmt()),
                   kind);
  return emitCaseDefaultCascade(caseStmt, condType, value, kind, likelihoods,
                                buildingTopLevelCase);
}

mlir::LogicalResult CIRGenFunction::emitDefaultStmt(const clang::DefaultStmt &s,
                                                    mlir::Type condType,
                                                    bool buildingTopLevelCase,
                                                    llvm::ArrayRef<const Attr *>
                                                        attrs) {
  mlir::ArrayAttr likelihoods =
      getCaseLikelihoodsAttr(&getMLIRContext(), {Stmt::getLikelihood(attrs)});
  return emitCaseDefaultCascade(&s, condType, builder.getArrayAttr({}),
                                cir::CaseOpKind::Default, likelihoods,
                                buildingTopLevelCase);
}

mlir::LogicalResult CIRGenFunction::emitSwitchCase(const SwitchCase &s,
                                                   bool buildingTopLevelCase,
                                                   llvm::ArrayRef<const Attr *>
                                                       attrs) {
  assert(!condTypeStack.empty() &&
         "build switch case without specifying the type of the condition");

  if (s.getStmtClass() == Stmt::CaseStmtClass)
    return emitCaseStmt(cast<CaseStmt>(s), condTypeStack.back(),
                        buildingTopLevelCase, attrs);

  if (s.getStmtClass() == Stmt::DefaultStmtClass)
    return emitDefaultStmt(cast<DefaultStmt>(s), condTypeStack.back(),
                           buildingTopLevelCase, attrs);

  llvm_unreachable("expect case or default stmt");
}

/// Build a DictionaryAttr representing loop hints from the given attributes.
/// This mirrors OGCG's LoopInfoStack::push() logic for processing
/// LoopHintAttr, OpenCLUnrollHintAttr, and CodeAlignAttr.
static mlir::DictionaryAttr
buildLoopHintDict(CIRGenFunction &cgf, mlir::MLIRContext *ctx,
                  clang::ASTContext &astCtx,
                  const clang::CodeGenOptions &cgOpts,
                  CIRGenModule &cgm,
                  llvm::ArrayRef<const clang::Attr *> attrs,
                  bool mustProgress,
                  mlir::DictionaryAttr cachedMustProgressOnly = {}) {
  // Fast path for the overwhelmingly common loop case in large TUs:
  // no loop hint attrs and only mustprogress=true.
  // Do not use this path when O>0 with unrolling disabled, because OGCG
  // additionally emits llvm.loop.unroll.disable in that case.
  bool needsDefaultUnrollDisable =
      cgOpts.OptimizationLevel > 0 && !cgOpts.UnrollLoops;
  if (mustProgress && attrs.empty() && cachedMustProgressOnly &&
      !needsDefaultUnrollDisable)
    return cachedMustProgressOnly;

  llvm::SmallVector<mlir::NamedAttribute> entries;
  auto boolAttr = [&](bool val) { return cgm.getBoolAttrCached(val); };
  auto i32Attr = [&](int val) { return cgf.getI32AttrCached(val); };

  // Track unroll state separately to handle -fno-unroll-loops override.
  enum UnrollState { Unspecified, Enable, Disable, Full };
  UnrollState unrollState = Unspecified;
  unsigned unrollCount = 0;
  bool hasVectorizeEnable = false;
  auto addVectorizeEnableIfMissing = [&]() {
    if (hasVectorizeEnable)
      return;
    entries.push_back(
        {cgm.getStringAttrCached("vectorize_enable"), boolAttr(true)});
    hasVectorizeEnable = true;
  };

  for (const auto *attr : attrs) {
    const clang::LoopHintAttr *LH = dyn_cast<clang::LoopHintAttr>(attr);
    const clang::OpenCLUnrollHintAttr *openCLHint =
        dyn_cast<clang::OpenCLUnrollHintAttr>(attr);

    if (!LH && !openCLHint) {
      // Handle CodeAlignAttr.
      if (const auto *codeAlign = dyn_cast<clang::CodeAlignAttr>(attr)) {
        const auto *CE =
            cast<clang::ConstantExpr>(codeAlign->getAlignment());
        llvm::APSInt argVal = CE->getResultAsAPSInt();
        entries.push_back({cgm.getStringAttrCached("code_align"),
                           i32Attr(argVal.getSExtValue())});
      }
      continue;
    }

    clang::LoopHintAttr::OptionType option = clang::LoopHintAttr::Unroll;
    clang::LoopHintAttr::LoopHintState state = clang::LoopHintAttr::Disable;
    unsigned valueInt = 1;

    if (openCLHint) {
      valueInt = openCLHint->getUnrollHint();
      if (valueInt == 0) {
        state = clang::LoopHintAttr::Enable;
      } else if (valueInt != 1) {
        option = clang::LoopHintAttr::UnrollCount;
        state = clang::LoopHintAttr::Numeric;
      }
    } else if (LH) {
      auto *valueExpr = LH->getValue();
      if (valueExpr) {
        llvm::APSInt valueAPS = valueExpr->EvaluateKnownConstInt(astCtx);
        valueInt = valueAPS.getSExtValue();
      }
      option = LH->getOption();
      state = LH->getState();
    }

    switch (state) {
    case clang::LoopHintAttr::Disable:
      switch (option) {
      case clang::LoopHintAttr::Vectorize:
        entries.push_back(
            {cgm.getStringAttrCached("vectorize_width"), i32Attr(1)});
        break;
      case clang::LoopHintAttr::Interleave:
        entries.push_back(
            {cgm.getStringAttrCached("interleave_count"), i32Attr(1)});
        break;
      case clang::LoopHintAttr::Unroll:
        unrollState = Disable;
        break;
      case clang::LoopHintAttr::UnrollAndJam:
        entries.push_back(
            {cgm.getStringAttrCached("unroll_and_jam_disable"),
             boolAttr(true)});
        break;
      case clang::LoopHintAttr::VectorizePredicate:
        entries.push_back(
            {cgm.getStringAttrCached("vectorize_predicate_disable"),
             boolAttr(true)});
        break;
      case clang::LoopHintAttr::Distribute:
        entries.push_back(
            {cgm.getStringAttrCached("distribute_disable"),
             boolAttr(true)});
        break;
      case clang::LoopHintAttr::PipelineDisabled:
        entries.push_back(
            {cgm.getStringAttrCached("pipeline_disable"), boolAttr(true)});
        break;
      default:
        llvm_unreachable("Options cannot be disabled.");
      }
      break;
    case clang::LoopHintAttr::Enable:
      switch (option) {
      case clang::LoopHintAttr::Vectorize:
      case clang::LoopHintAttr::Interleave:
        addVectorizeEnableIfMissing();
        break;
      case clang::LoopHintAttr::Unroll:
        unrollState = Enable;
        break;
      case clang::LoopHintAttr::UnrollAndJam:
        entries.push_back(
            {cgm.getStringAttrCached("unroll_and_jam_enable"),
             boolAttr(true)});
        break;
      case clang::LoopHintAttr::VectorizePredicate:
        entries.push_back(
            {cgm.getStringAttrCached("vectorize_predicate_enable"),
             boolAttr(true)});
        break;
      case clang::LoopHintAttr::Distribute:
        entries.push_back(
            {cgm.getStringAttrCached("distribute_enable"),
             boolAttr(true)});
        break;
      default:
        llvm_unreachable("Options cannot be enabled.");
      }
      break;
    case clang::LoopHintAttr::AssumeSafety:
      switch (option) {
      case clang::LoopHintAttr::Vectorize:
      case clang::LoopHintAttr::Interleave:
        entries.push_back(
            {cgm.getStringAttrCached("parallel"), boolAttr(true)});
        addVectorizeEnableIfMissing();
        break;
      default:
        llvm_unreachable("Options cannot be used to assume mem safety.");
      }
      break;
    case clang::LoopHintAttr::Full:
      switch (option) {
      case clang::LoopHintAttr::Unroll:
        unrollState = Full;
        break;
      case clang::LoopHintAttr::UnrollAndJam:
        entries.push_back(
            {cgm.getStringAttrCached("unroll_and_jam_full"),
             boolAttr(true)});
        break;
      default:
        llvm_unreachable("Options cannot be used with 'full' hint.");
      }
      break;
    case clang::LoopHintAttr::FixedWidth:
      case clang::LoopHintAttr::ScalableWidth:
      switch (option) {
      case clang::LoopHintAttr::VectorizeWidth:
        {
          const bool isScalable =
              state == clang::LoopHintAttr::ScalableWidth;
          entries.push_back(
              {cgm.getStringAttrCached("vectorize_scalable"),
               boolAttr(isScalable)});
          if (LH && LH->getValue()) {
            entries.push_back(
                {cgm.getStringAttrCached("vectorize_width"),
                 i32Attr(valueInt)});
            // When vectorize_width > 1, also emit vectorize.enable=true to
            // activate the vectorizer. Width=1 doesn't need it since it means
            // scalar width. Matches OGCG's LoopInfoStack::push() behavior.
            // Skip if vectorize_enable was already added (e.g. from an explicit
            // vectorize(enable) pragma) to avoid duplicate dictionary keys.
            if (valueInt > 1)
              addVectorizeEnableIfMissing();
          } else {
            // Match OGCG LoopInfoStack::createMetadata():
            // - vectorize_width(scalable) implies vectorize.enable=true.
            // - vectorize_width(fixed) (without an explicit numeric width)
            //   implies vectorize.enable=true because width remains "unset"
            //   (0) rather than 1.
            if (isScalable ||
                state == clang::LoopHintAttr::FixedWidth)
              addVectorizeEnableIfMissing();
          }
        }
        break;
      default:
        llvm_unreachable("Options cannot be used with 'scalable' hint.");
      }
      break;
    case clang::LoopHintAttr::Numeric:
      switch (option) {
      case clang::LoopHintAttr::InterleaveCount:
        entries.push_back(
            {cgm.getStringAttrCached("interleave_count"),
             i32Attr(valueInt)});
        break;
      case clang::LoopHintAttr::UnrollCount:
        unrollCount = valueInt;
        break;
      case clang::LoopHintAttr::UnrollAndJamCount:
        entries.push_back(
            {cgm.getStringAttrCached("unroll_and_jam_count"),
             i32Attr(valueInt)});
        break;
      case clang::LoopHintAttr::PipelineInitiationInterval:
        entries.push_back(
            {cgm.getStringAttrCached("pipeline_ii"), i32Attr(valueInt)});
        break;
      default:
        llvm_unreachable("Options cannot be assigned a value.");
      }
      break;
    }
  }

  // Apply -fno-unroll-loops override: if optimization is on and unroll-loops
  // is disabled, and no pragma set the unroll state, disable unrolling.
  if (cgOpts.OptimizationLevel > 0 && !cgOpts.UnrollLoops &&
      unrollState == Unspecified && unrollCount == 0)
    unrollState = Disable;

  // Emit unroll entries.
  if (unrollState == Enable)
    entries.push_back(
        {cgm.getStringAttrCached("unroll_enable"), boolAttr(true)});
  else if (unrollState == Disable)
    entries.push_back(
        {cgm.getStringAttrCached("unroll_disable"), boolAttr(true)});
  else if (unrollState == Full)
    entries.push_back(
        {cgm.getStringAttrCached("unroll_full"), boolAttr(true)});
  if (unrollCount)
    entries.push_back(
        {cgm.getStringAttrCached("unroll_count"), i32Attr(unrollCount)});

  if (mustProgress)
    entries.push_back(
        {cgm.getStringAttrCached("mustprogress"), boolAttr(true)});

  // When unroll(full) is specified, strip vectorize/interleave/distribute
  // hints since full unrolling takes precedence. Matches OGCG's
  // LoopInfoStack behavior which only emits the unroll hint.
  if (unrollState == Full) {
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
                        [](const mlir::NamedAttribute &attr) {
                          StringRef name = attr.getName().getValue();
                          return name.starts_with("vectorize") ||
                                 name.starts_with("interleave") ||
                                 name.starts_with("distribute");
                        }),
        entries.end());
  }

  if (entries.empty())
    return {};

  return mlir::DictionaryAttr::get(ctx, entries);
}

static mlir::DictionaryAttr
appendLoopAnnotationLocs(CIRGenFunction &cgf, mlir::DictionaryAttr loopHint,
                         mlir::Location startLoc, mlir::Location endLoc) {
  llvm::SmallVector<mlir::NamedAttribute> entries;
  if (loopHint)
    llvm::append_range(entries, loopHint.getValue());
  auto &cgm = cgf.getCIRGenModule();
  entries.push_back({cgm.getStringAttrCached("loop_start_loc"), startLoc});
  entries.push_back({cgm.getStringAttrCached("loop_end_loc"), endLoc});
  return mlir::DictionaryAttr::get(&cgf.getMLIRContext(), entries);
}

static mlir::DictionaryAttr
mergeLoopHintDicts(CIRGenFunction &cgf, mlir::DictionaryAttr base,
                   mlir::DictionaryAttr override) {
  if (!base)
    return override;
  if (!override)
    return base;

  llvm::SmallVector<mlir::NamedAttribute> entries;
  llvm::append_range(entries, base.getValue());
  for (mlir::NamedAttribute attr : override.getValue()) {
    auto existing = llvm::find_if(entries, [&](mlir::NamedAttribute current) {
      return current.getName() == attr.getName();
    });
    if (existing != entries.end())
      *existing = attr;
    else
      entries.push_back(attr);
  }
  return mlir::DictionaryAttr::get(&cgf.getMLIRContext(), entries);
}

/// Determine whether a loop must make forward progress.
/// Mirrors OGCG's CodeGenFunction::checkIfLoopMustProgress.
static bool checkIfLoopMustProgress(CIRGenFunction &cgf,
                                    const clang::Expr *controllingExpr,
                                    bool hasEmptyBody) {
  const clang::CodeGenOptions &cgOpts = cgf.getCIRGenModule().getCodeGenOpts();

  if (cgOpts.getFiniteLoops() == clang::CodeGenOptions::FiniteLoopsKind::Never)
    return false;

  // Loops with constant conditions do not have to make progress in any C
  // version.
  clang::Expr::EvalResult result;
  bool condIsConstInt =
      !controllingExpr ||
      (controllingExpr->EvaluateAsInt(result, cgf.getContext()) &&
       result.Val.isInt());

  bool condIsTrue =
      condIsConstInt &&
      (!controllingExpr || result.Val.getInt().getBoolValue());

  // Loops with non-constant conditions must make progress in C11 and later.
  if (cgf.getLangOpts().C11 && !condIsConstInt)
    return true;

  if (cgOpts.getFiniteLoops() ==
          clang::CodeGenOptions::FiniteLoopsKind::Always ||
      cgf.getLangOpts().CPlusPlus11) {
    // C++26 trivial infinite loop exception.
    if (hasEmptyBody && condIsTrue) {
      cgf.eraseMustProgressAttr();
      return false;
    }
    return true;
  }
  return false;
}

/// Determine whether a loop body is empty (mirrors OGCG's hasEmptyLoopBody).
template <typename LoopStmt>
static bool hasEmptyLoopBody(const LoopStmt &s) {
  if constexpr (std::is_same_v<LoopStmt, clang::ForStmt>) {
    if (s.getInc())
      return false;
  }
  const clang::Stmt *body = s.getBody();
  if (const auto *compound = dyn_cast<clang::CompoundStmt>(body))
    return compound->body_empty();
  return isa<clang::NullStmt>(body);
}

mlir::LogicalResult
CIRGenFunction::emitCXXForRangeStmt(const CXXForRangeStmt &s,
                                    ArrayRef<const Attr *> forAttrs) {
  cir::ForOp forOp;
  NamedLoopControlTarget *namedTarget = nullptr;
  std::string namedBreakLabel;
  bool hasNamedTarget = isActiveNamedLoopTarget(&s);

  // TODO(cir): pass in array of attributes.
  auto forStmtBuilder = [&]() -> mlir::LogicalResult {
    mlir::LogicalResult loopRes = mlir::success();
    // Evaluate the first pieces before the loop.
    if (s.getInit())
      if (emitStmt(s.getInit(), /*useCurrentScope=*/true).failed())
        return mlir::failure();
    if (emitStmt(s.getRangeStmt(), /*useCurrentScope=*/true).failed())
      return mlir::failure();
    if (emitStmt(s.getBeginStmt(), /*useCurrentScope=*/true).failed())
      return mlir::failure();
    if (emitStmt(s.getEndStmt(), /*useCurrentScope=*/true).failed())
      return mlir::failure();
    if (namedTarget)
      namedTarget->continueTargetDepth = ehStack.stable_begin();

    // The range-init temporary, __range, __begin, and __end may have pushed
    // RAII cleanups. Set breakTargetDepth and continueTargetDepth to AFTER
    // these cleanups so that break/continue inside the loop body do NOT
    // emit them inline — the enclosing scope exit handles them instead.
    // Without this, break/continue emit the range-init destructor inline
    // AND the scope exit emits it again, causing double destruction.
    // Same pattern as for-init and condition variables.
    for (LexicalScope *scope = curLexScope; scope;
         scope = scope->getParentScope()) {
      if (scope->isLoop()) {
        scope->setBreakTargetDepth(ehStack.stable_begin());
        scope->setContinueTargetDepth(ehStack.stable_begin());
        break;
      }
    }

    // Build loop hint dictionary from pragma attributes.
    // OGCG does not pass mustProgress for CXXForRangeStmt.
    mlir::DictionaryAttr loopHint = buildLoopHintDict(
        *this, builder.getContext(), getContext(),
        getCIRGenModule().getCodeGenOpts(), getCIRGenModule(), forAttrs,
        /*mustProgress=*/false);
    loopHint = appendLoopAnnotationLocs(*this, loopHint, getLoc(s.getForLoc()),
                                        getLoc(s.getEndLoc()));
    // OGCG creates an intermediate exit block when there are cleanups between
    // the loop body and the loop-exit scope. CIR's structured regions (cir.for)
    // handle cleanup dispatch through scope nesting, so no extra block is needed.
    // OGCG emits convergence control tokens for some loop forms on SPIR-V.
    // CIR does not currently model token threading at the op level here.

    forOp = builder.createFor(
        // Use a point location (the `for` keyword) rather than the full
        // source range. A fused begin..end range location lowers the loop's
        // entry and back-edge branches to DILocation line 0 (LLVM merges the
        // differing begin/end lines to 0), whereas classic CodeGen attributes
        // those branches to the for-statement line.
        getLoc(s.getBeginLoc()),
        /*condBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          mlir::Value condVal = evaluateExprAsBool(s.getCond());
          condVal = emitCondLikelihoodViaExpectIntrinsic(
              condVal, Stmt::getLikelihood(s.getBody()));
          if (mlir::Operation *condDef = condVal.getDefiningOp())
            addOpToNewSourceAtom(condDef);
          auto condOp = builder.createCondition(condVal);
          addOpToNewSourceAtom(condOp);
        },
        /*bodyBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          incrementProfileCounter(&s);
          // Always create a per-iteration cleanup scope wrapping both the
          // loop variable declaration and body, matching OGCG's LexicalScope.
          // This ensures that any temporaries created by the loop variable's
          // initialization expression (e.g., a temporary from dereferencing
          // an iterator that returns by value, bound via const auto&) are
          // properly destroyed at the end of each iteration, not just at
          // loop exit.
          mlir::Location scopeLoc = getLoc(s.getSourceRange());
          cir::ScopeOp::create(
              builder, scopeLoc,
              [&](mlir::OpBuilder &b2, mlir::Location loc2) {
                LexicalScope iterScope{*this, loc2,
                                       builder.getInsertionBlock()};
                if (emitStmt(s.getLoopVarStmt(), /*useCurrentScope=*/true)
                        .failed())
                  loopRes = mlir::failure();
                if (emitStmt(s.getBody(), /*useCurrentScope=*/false).failed())
                  loopRes = mlir::failure();
              });
          emitStopPoint(&s);
        },
        /*stepBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          if (namedTarget)
            emitSyntheticLabel(loc, namedTarget->continueLabel);
          if (s.getInc())
            if (emitStmt(s.getInc(), /*useCurrentScope=*/true).failed())
              loopRes = mlir::failure();
          builder.createYield(loc);
        });
    if (loopHint)
      forOp.setLoopHintAttr(loopHint);
    return loopRes;
  };

  mlir::LogicalResult res = mlir::success();
  mlir::Location scopeLoc = getLoc(s.getSourceRange());
  cir::ScopeOp::create(builder, scopeLoc, /*scopeBuilder=*/
                       [&](mlir::OpBuilder &b, mlir::Location loc) {
                         LexicalScope lexScope{*this, loc,
                                               builder.getInsertionBlock()};
                         lexScope.setAsLoop();
                         if (hasNamedTarget) {
                           NamedLoopControlTarget target;
                           target.loopOrSwitch = &s;
                           target.scope = &lexScope;
                           target.breakTargetDepth =
                               lexScope.getCleanupStackDepth();
                           target.continueTargetDepth =
                               lexScope.getCleanupStackDepth();
                           target.breakLabel =
                               createNamedLoopControlLabel("break");
                           target.continueLabel =
                               createNamedLoopControlLabel("continue");
                           namedLoopControlStack.push_back(std::move(target));
                           namedTarget = &namedLoopControlStack.back();
                           namedBreakLabel = namedTarget->breakLabel;
                         }
                         res = forStmtBuilder();
                         if (hasNamedTarget) {
                           namedLoopControlStack.pop_back();
                           namedTarget = nullptr;
                         }
                       });

  if (res.failed())
    return res;

  terminateBody(builder, forOp.getBody(), getLoc(s.getEndLoc()));
  if (auto yieldOp = getSingleYieldTerminator(forOp.getBody())) {
    yieldOp->setLoc(getLoc(s.getForLoc()));
    addOpToNewSourceAtom(yieldOp);
  }
  if (hasNamedTarget)
    emitSyntheticLabel(getLoc(s.getEndLoc()), namedBreakLabel);
  return mlir::success();
}

mlir::LogicalResult CIRGenFunction::emitForStmt(const ForStmt &s,
                                                 ArrayRef<const Attr *> forAttrs) {
  cir::ForOp forOp;
  EHScopeStack::stable_iterator condVarCleanupDepth;
  NamedLoopControlTarget *namedTarget = nullptr;
  std::string namedBreakLabel;
  bool hasNamedTarget = isActiveNamedLoopTarget(&s);
  bool continueTargetsStep = s.getInc() || s.getConditionVariable();

  // In C89, for-loop init-declared variables have the scope of the enclosing
  // block, not the for statement (OGCG CGStmt.cpp:1298-1300). Track this so
  // the init can be emitted outside the for scope.
  bool initInEnclosingScope =
      !getLangOpts().C99 && !getLangOpts().CPlusPlus && s.getInit();

  auto forStmtBuilder = [&]() -> mlir::LogicalResult {
    mlir::LogicalResult loopRes = mlir::success();
    // Evaluate the first part before the loop.
    // In C89 mode, the init was already emitted outside the scope (above).
    if (s.getInit() && !initInEnclosingScope) {
      if (emitStmt(s.getInit(), /*useCurrentScope=*/true).failed())
        return mlir::failure();
      if (namedTarget)
        namedTarget->continueTargetDepth = ehStack.stable_begin();
      // If the init-statement pushed RAII cleanups (e.g. for (G g; ...)),
      // set breakTargetDepth and continueTargetDepth to AFTER the init
      // cleanups. This prevents break/continue from emitting the init
      // variable's destructor inline — the enclosing scope exit handles
      // it instead. Without this, break destroys the init variable inline
      // AND the scope exit destroys it again (double-destroy).
      // If a condition variable also has a destructor, it will override
      // these depths later (lines below), correctly excluding both.
      for (LexicalScope *scope = curLexScope; scope;
           scope = scope->getParentScope()) {
        if (scope->isLoop()) {
          scope->setBreakTargetDepth(ehStack.stable_begin());
          scope->setContinueTargetDepth(ehStack.stable_begin());
          break;
        }
      }
    }
    // Build loop hint dictionary from pragma attributes.
    bool mp = checkIfLoopMustProgress(*this, s.getCond(),
                                      hasEmptyLoopBody(s));
    mlir::DictionaryAttr loopHint = buildLoopHintDict(
        *this, builder.getContext(), getContext(),
        getCIRGenModule().getCodeGenOpts(), getCIRGenModule(), forAttrs, mp,
        mp ? getCIRGenModule().getMustProgressLoopHintAttr()
           : mlir::DictionaryAttr{});
    loopHint = mergeLoopHintDicts(*this, loopHint, consumePendingLoopHintAttr());
    // OGCG creates an intermediate exit block when there are cleanups between
    // the loop body and the loop-exit scope. CIR's structured regions (cir.for)
    // handle cleanup dispatch through scope nesting, so no extra block is needed.
    // OGCG emits convergence control tokens for some loop forms on SPIR-V.
    // CIR does not currently model token threading at the op level here.

    forOp = builder.createFor(
        // Use a point location (the `for` keyword) rather than the full
        // source range. A fused begin..end range location lowers the loop's
        // entry and back-edge branches to DILocation line 0 (LLVM merges the
        // differing begin/end lines to 0), whereas classic CodeGen attributes
        // those branches to the for-statement line.
        getLoc(s.getBeginLoc()),
        /*condBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          if (namedTarget && !continueTargetsStep)
            emitSyntheticLabel(loc, namedTarget->continueLabel);
          mlir::Value condVal;
          if (s.getCond()) {
            // If the for statement has a condition scope,
            // emit the local variable declaration.
            if (const VarDecl *condVar = s.getConditionVariable()) {
              // Remember the cleanup depth *before* entering the condition
              // variable scope. Emitting cleanups to this depth destroys the
              // per-iteration condition object (including lifetime markers)
              // while preserving for-init cleanups.
              condVarCleanupDepth = ehStack.stable_begin();
              ApplySourceAtomGroup condDeclAtom(*this);
              emitDecl(*condVar);
              if (namedTarget)
                namedTarget->continueTargetDepth = ehStack.stable_begin();
              // Set both breakTargetDepth and continueTargetDepth to AFTER
              // the condition-variable cleanups. Break exits the loop scope,
              // which runs the final condition cleanup; continue runs through
              // the step region, where we emit the per-iteration cleanup.
              //
              // This must apply to trivial types as well: when lifetime
              // markers are enabled, trivial condition variables still push a
              // cleanup (llvm.lifetime.end). Keeping continue/break targets
              // before the condition scope causes double-cleanup on break and
              // misses per-iteration cleanup sequencing in the step region.
              for (LexicalScope *scope = curLexScope; scope;
                   scope = scope->getParentScope()) {
                if (scope->isLoop()) {
                  scope->setBreakTargetDepth(ehStack.stable_begin());
                  scope->setContinueTargetDepth(ehStack.stable_begin());
                  break;
                }
              }
            }
            // C99 6.8.5p2/p4: The first substatement is executed if the
            // expression compares unequal to 0. The condition must be a
            // scalar type.
            mlir::Block *condEntryBlock = builder.getInsertionBlock();
            condVal = evaluateExprAsBool(s.getCond());
            // If a noreturn call in the condition created extra blocks,
            // clean them up to maintain the SizedRegion<1> constraint.
            if (builder.getInsertionBlock() != condEntryBlock) {
              builder.getInsertionBlock()->erase();
              condEntryBlock->back().erase(); // erase UnreachableOp
              builder.setInsertionPointToEnd(condEntryBlock);
              condVal = cir::ConstantOp::create(builder, loc,
                                                builder.getFalseAttr());
            }
            maybeEmitDeferredVarDeclInit(s.getConditionVariable());
          } else {
            condVal = cir::ConstantOp::create(b, loc, builder.getTrueAttr());
          }
          condVal = emitCondLikelihoodViaExpectIntrinsic(
              condVal, Stmt::getLikelihood(s.getBody()));
          if (s.getCond()) {
            if (mlir::Operation *condDef = condVal.getDefiningOp())
              addOpToNewSourceAtom(condDef);
          }
          auto condOp = builder.createCondition(condVal);
          if (s.getCond())
            addOpToNewSourceAtom(condOp);
        },
        /*bodyBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          incrementProfileCounter(&s);
          // Create a cleanup scope for the body, in case it is not a compound
          // statement. A CompoundStmt body creates its own scope via
          // emitCompoundStmt, but a single-statement body (e.g. DeclStmt)
          // needs an explicit scope so that destructors run at the end of each
          // iteration rather than once after the loop. Matches OGCG's
          // RunCleanupsScope in CGStmt.cpp EmitForStmt.
          if (isa<CompoundStmt>(s.getBody())) {
            if (emitStmt(s.getBody(), /*useCurrentScope=*/false).failed())
              loopRes = mlir::failure();
          } else {
            mlir::Location bodyLoc = getLoc(s.getBody()->getSourceRange());
            mlir::OpBuilder::InsertPoint scopeInsPt;
            cir::ScopeOp::create(
                builder, bodyLoc,
                [&](mlir::OpBuilder &sb, mlir::Type &type, mlir::Location sl) {
                  scopeInsPt = sb.saveInsertionPoint();
                });
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.restoreInsertionPoint(scopeInsPt);
            LexicalScope lexScope(*this, bodyLoc, builder.getInsertionBlock());
            if (emitStmt(s.getBody(), /*useCurrentScope=*/true).failed())
              loopRes = mlir::failure();
          }
          emitStopPoint(&s);
        },
        /*stepBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          if (namedTarget && continueTargetsStep)
            emitSyntheticLabel(loc, namedTarget->continueLabel);
          if (s.getInc())
            if (emitStmt(s.getInc(), /*useCurrentScope=*/true).failed())
              loopRes = mlir::failure();
          // End the current condition-variable iteration after the step
          // expression but before looping back to the condition.
          // This emits both destructor and lifetime-end cleanups as needed.
          if (s.getConditionVariable()) {
            assert(condVarCleanupDepth.isValid() &&
                   "cond var cleanup depth must be recorded in condBuilder");
            emitCleanupsForEarlyExit(condVarCleanupDepth);
          }
          builder.createYield(loc);
        });
    if (loopHint)
      forOp.setLoopHintAttr(loopHint);
    return loopRes;
  };

  auto res = mlir::success();
  auto scopeLoc = getLoc(s.getSourceRange());

  // In C89 mode, emit the init BEFORE the scope so its cleanup belongs
  // to the enclosing block scope, matching OGCG (CGStmt.cpp:1298-1304).
  if (initInEnclosingScope) {
    if (emitStmt(s.getInit(), /*useCurrentScope=*/true).failed())
      return mlir::failure();
  }

  cir::ScopeOp::create(builder, scopeLoc, /*scopeBuilder=*/
                       [&](mlir::OpBuilder &b, mlir::Location loc) {
                         LexicalScope lexScope{*this, loc,
                                               builder.getInsertionBlock()};
                         lexScope.setAsLoop();
                         if (hasNamedTarget) {
                           NamedLoopControlTarget target;
                           target.loopOrSwitch = &s;
                           target.scope = &lexScope;
                           target.breakTargetDepth =
                               lexScope.getCleanupStackDepth();
                           target.continueTargetDepth =
                               lexScope.getCleanupStackDepth();
                           target.breakLabel =
                               createNamedLoopControlLabel("break");
                           target.continueLabel =
                               createNamedLoopControlLabel("continue");
                           namedLoopControlStack.push_back(std::move(target));
                           namedTarget = &namedLoopControlStack.back();
                           namedBreakLabel = namedTarget->breakLabel;
                         }
                         res = forStmtBuilder();
                         if (hasNamedTarget) {
                           namedLoopControlStack.pop_back();
                           namedTarget = nullptr;
                         }
                       });

  if (res.failed())
    return res;

  terminateBody(builder, forOp.getBody(), getLoc(s.getEndLoc()));
  if (auto yieldOp = getSingleYieldTerminator(forOp.getBody())) {
    if (s.getInc()) {
      addOpToNewSourceAtom(yieldOp);
    } else if (s.getCond() && !s.getConditionVariable()) {
      yieldOp->setLoc(getLoc(s.getForLoc()));
      addOpToNewSourceAtom(yieldOp);
    }
  }
  if (!s.getInc() && !s.getCond() && hasEmptyLoopBody(s)) {
    if (auto yieldOp = getSingleYieldTerminator(*forOp.maybeGetStep())) {
      yieldOp->setLoc(getLoc(s.getForLoc()));
      addOpToNewSourceAtom(yieldOp);
    }
  }
  if (hasNamedTarget)
    emitSyntheticLabel(getLoc(s.getEndLoc()), namedBreakLabel);
  return mlir::success();
}

mlir::LogicalResult CIRGenFunction::emitDoStmt(const DoStmt &s,
                                                ArrayRef<const Attr *> doAttrs) {
  cir::DoWhileOp doWhileOp;
  NamedLoopControlTarget *namedTarget = nullptr;
  std::string namedBreakLabel;
  bool hasNamedTarget = isActiveNamedLoopTarget(&s);

  auto doStmtBuilder = [&]() -> mlir::LogicalResult {
    mlir::LogicalResult loopRes = mlir::success();
    // Build loop hint dictionary from pragma attributes.
    bool mp = checkIfLoopMustProgress(*this, s.getCond(),
                                      hasEmptyLoopBody(s));
    mlir::DictionaryAttr loopHint = buildLoopHintDict(
        *this, builder.getContext(), getContext(),
        getCIRGenModule().getCodeGenOpts(), getCIRGenModule(), doAttrs, mp,
        mp ? getCIRGenModule().getMustProgressLoopHintAttr()
           : mlir::DictionaryAttr{});
    // OGCG creates an intermediate exit block when there are cleanups between
    // the loop body and the loop-exit scope. CIR's structured regions (cir.do)
    // handle cleanup dispatch through scope nesting, so no extra block is needed.
    // OGCG emits convergence control tokens for some loop forms on SPIR-V.
    // CIR does not currently model token threading at the op level here.

    doWhileOp = builder.createDoWhile(
        // Point location (the `do` keyword); see the for-loop note about
        // fused range locations lowering loop branches to line 0.
        getLoc(s.getBeginLoc()),
        /*condBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          if (namedTarget)
            emitSyntheticLabel(loc, namedTarget->continueLabel);
          // C99 6.8.5p2/p4: The first substatement is executed if the
          // expression compares unequal to 0. The condition must be a
          // scalar type.
          mlir::Block *condEntryBlock = builder.getInsertionBlock();
          mlir::Value condVal = evaluateExprAsBool(s.getCond());
          // If a noreturn call in the condition created extra blocks,
          // clean them up to maintain the SizedRegion<1> constraint.
          if (builder.getInsertionBlock() != condEntryBlock) {
            builder.getInsertionBlock()->erase();
            condEntryBlock->back().erase(); // erase UnreachableOp
            builder.setInsertionPointToEnd(condEntryBlock);
            condVal =
                cir::ConstantOp::create(builder, loc, builder.getFalseAttr());
          }
          // OGCG does not apply emitCondLikelihoodViaExpectIntrinsic for
          // do-while loops (it only applies for while/for/if conditions).
          mlir::Operation *condDef = condVal.getDefiningOp();
          while (auto castOp = mlir::dyn_cast_or_null<cir::CastOp>(condDef))
            condDef = castOp.getSrc().getDefiningOp();
          if (condDef)
            addOpToNewSourceAtom(condDef);
          // Instrument the do-while region counter here, in the condition,
          // stepping by the (boolean) condition value so the counter holds the
          // loop's backedge count. Classic CodeGen achieves the same via
          // EmitBlockWithFallThrough, which skips the counter on the initial
          // fall-through entry; the structured cir.do body cannot distinguish
          // the fall-through from the backedge, so count condition-true edges
          // instead. The body execution count is then backedge + parent entry,
          // matching what the profile reader reconstructs.
          incrementProfileCounter(&s, condVal);
          auto condOp = builder.createCondition(condVal);
          condOp->setLoc(getLoc(s.getBody()->getEndLoc()));
          addOpToNewSourceAtom(condOp);
        },
        /*bodyBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          // Create a cleanup scope for the body, in case it is not a compound
          // statement. Matches OGCG's RunCleanupsScope in CGStmt.cpp
          // EmitDoStmt.
          if (isa<CompoundStmt>(s.getBody())) {
            if (emitStmt(s.getBody(), /*useCurrentScope=*/false).failed())
              loopRes = mlir::failure();
          } else {
            mlir::Location bodyLoc = getLoc(s.getBody()->getSourceRange());
            mlir::OpBuilder::InsertPoint scopeInsPt;
            cir::ScopeOp::create(
                builder, bodyLoc,
                [&](mlir::OpBuilder &sb, mlir::Type &type, mlir::Location sl) {
                  scopeInsPt = sb.saveInsertionPoint();
                });
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.restoreInsertionPoint(scopeInsPt);
            LexicalScope lexScope(*this, bodyLoc, builder.getInsertionBlock());
            if (emitStmt(s.getBody(), /*useCurrentScope=*/true).failed())
              loopRes = mlir::failure();
          }
          emitStopPoint(&s);
        });
    if (loopHint)
      doWhileOp.setLoopHintAttr(loopHint);
    return loopRes;
  };

  mlir::LogicalResult res = mlir::success();
  mlir::Location scopeLoc = getLoc(s.getSourceRange());
  cir::ScopeOp::create(builder, scopeLoc, /*scopeBuilder=*/
                       [&](mlir::OpBuilder &b, mlir::Location loc) {
                         LexicalScope lexScope{*this, loc,
                                               builder.getInsertionBlock()};
                         lexScope.setAsLoop();
                         if (hasNamedTarget) {
                           NamedLoopControlTarget target;
                           target.loopOrSwitch = &s;
                           target.scope = &lexScope;
                           target.breakTargetDepth =
                               lexScope.getCleanupStackDepth();
                           target.continueTargetDepth =
                               lexScope.getCleanupStackDepth();
                           target.breakLabel =
                               createNamedLoopControlLabel("break");
                           target.continueLabel =
                               createNamedLoopControlLabel("continue");
                           namedLoopControlStack.push_back(std::move(target));
                           namedTarget = &namedLoopControlStack.back();
                           namedBreakLabel = namedTarget->breakLabel;
                         }
                         res = doStmtBuilder();
                         if (hasNamedTarget) {
                           namedLoopControlStack.pop_back();
                           namedTarget = nullptr;
                         }
                       });

  if (res.failed())
    return res;

  terminateBody(builder, doWhileOp.getBody(), getLoc(s.getEndLoc()));
  if (auto yieldOp = getSingleYieldTerminator(doWhileOp.getBody()))
    yieldOp->setLoc(getLoc(s.getBody()->getEndLoc()));
  if (hasNamedTarget)
    emitSyntheticLabel(getLoc(s.getEndLoc()), namedBreakLabel);
  return mlir::success();
}

mlir::LogicalResult CIRGenFunction::emitWhileStmt(const WhileStmt &s,
                                                   ArrayRef<const Attr *> whileAttrs) {
  cir::WhileOp whileOp;
  EHScopeStack::stable_iterator condVarCleanupDepth;
  NamedLoopControlTarget *namedTarget = nullptr;
  std::string namedBreakLabel;
  bool hasNamedTarget = isActiveNamedLoopTarget(&s);

  auto whileStmtBuilder = [&]() -> mlir::LogicalResult {
    mlir::LogicalResult loopRes = mlir::success();
    // Build loop hint dictionary from pragma attributes.
    bool mp = checkIfLoopMustProgress(*this, s.getCond(),
                                      hasEmptyLoopBody(s));
    mlir::DictionaryAttr loopHint = buildLoopHintDict(
        *this, builder.getContext(), getContext(),
        getCIRGenModule().getCodeGenOpts(), getCIRGenModule(), whileAttrs, mp,
        mp ? getCIRGenModule().getMustProgressLoopHintAttr()
           : mlir::DictionaryAttr{});
    // OGCG creates an intermediate exit block when there are cleanups between
    // the loop body and the loop-exit scope. CIR's structured regions (cir.while)
    // handle cleanup dispatch through scope nesting, so no extra block is needed.
    // OGCG emits convergence control tokens for some loop forms on SPIR-V.
    // CIR does not currently model token threading at the op level here.
    // Emit a diagnostic when an infinite while loop has a likelihood attribute
    // ([[likely]]/[[unlikely]]) on the body, since the attribute has no effect
    // on an infinite loop. Matches OGCG CGStmt.cpp:1155-1163.
    Expr::EvalResult result;
    bool condIsConstTrue =
        s.getCond()->EvaluateAsInt(result, getContext()) && result.Val.isInt() &&
        result.Val.getInt().getBoolValue();
    if (condIsConstTrue) {
      if (const Attr *a = Stmt::getLikelihoodAttr(s.getBody())) {
        cgm.getDiags().Report(
            a->getLocation(), diag::warn_attribute_has_no_effect_on_infinite_loop)
            << a << a->getRange();
        cgm.getDiags().Report(
            s.getWhileLoc(), diag::note_attribute_has_no_effect_on_infinite_loop_here)
            << SourceRange(s.getWhileLoc(), s.getRParenLoc());
      }
    }

    whileOp = builder.createWhile(
        // Point location (the `while` keyword); see the for-loop note about
        // fused range locations lowering loop branches to line 0.
        getLoc(s.getBeginLoc()),
        /*condBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          if (namedTarget)
            emitSyntheticLabel(loc, namedTarget->continueLabel);
          mlir::Value condVal;
          // If the for statement has a condition scope,
          // emit the local variable declaration.
          if (const VarDecl *condVar = s.getConditionVariable()) {
            // Remember the cleanup depth *before* entering the condition
            // variable scope. Emitting cleanups to this depth destroys the
            // per-iteration condition object (including lifetime markers).
            condVarCleanupDepth = ehStack.stable_begin();
            ApplySourceAtomGroup condDeclAtom(*this);
            emitDecl(*condVar);
            // Set breakTargetDepth to AFTER the condition-variable cleanups so
            // break does not destroy the condition variable inline — the loop
            // scope exit cleanup handles it instead.
            //
            // This must also apply to trivial types under lifetime markers:
            // otherwise break emits llvm.lifetime.end inline and scope exit
            // emits it again (double end).
            for (LexicalScope *scope = curLexScope; scope;
                 scope = scope->getParentScope()) {
              if (scope->isLoop()) {
                scope->setBreakTargetDepth(ehStack.stable_begin());
                break;
              }
            }
          }
          // C99 6.8.5p2/p4: The first substatement is executed if the
          // expression compares unequal to 0. The condition must be a
          // scalar type.
          mlir::Block *condEntryBlock = builder.getInsertionBlock();
          condVal = evaluateExprAsBool(s.getCond());
          // If a noreturn call in the condition created extra blocks (the
          // noreturn handling emits UnreachableOp + creates a new dead
          // block), clean them up to maintain the SizedRegion<1> constraint
          // on the while condition region. Replace the unreachable with a
          // dummy false condition — the noreturn attribute on the call
          // already conveys unreachability to downstream passes.
          if (builder.getInsertionBlock() != condEntryBlock) {
            builder.getInsertionBlock()->erase();
            condEntryBlock->back().erase(); // erase UnreachableOp
            builder.setInsertionPointToEnd(condEntryBlock);
            condVal =
                cir::ConstantOp::create(builder, loc, builder.getFalseAttr());
          }
          maybeEmitDeferredVarDeclInit(s.getConditionVariable());
          if (!condIsConstTrue)
            condVal = emitCondLikelihoodViaExpectIntrinsic(
                condVal, Stmt::getLikelihood(s.getBody()));
          if (mlir::Operation *condDef = condVal.getDefiningOp())
            addOpToNewSourceAtom(condDef);
          auto condOp = builder.createCondition(condVal);
          addOpToNewSourceAtom(condOp);
        },
        /*bodyBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          incrementProfileCounter(&s);
          // Create a cleanup scope for the body, in case it is not a compound
          // statement. Matches OGCG's RunCleanupsScope in CGStmt.cpp
          // EmitWhileStmt.
          if (isa<CompoundStmt>(s.getBody())) {
            if (emitStmt(s.getBody(), /*useCurrentScope=*/false).failed())
              loopRes = mlir::failure();
          } else {
            mlir::Location bodyLoc = getLoc(s.getBody()->getSourceRange());
            mlir::OpBuilder::InsertPoint scopeInsPt;
            cir::ScopeOp::create(
                builder, bodyLoc,
                [&](mlir::OpBuilder &sb, mlir::Type &type, mlir::Location sl) {
                  scopeInsPt = sb.saveInsertionPoint();
                });
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.restoreInsertionPoint(scopeInsPt);
            LexicalScope lexScope(*this, bodyLoc, builder.getInsertionBlock());
            if (emitStmt(s.getBody(), /*useCurrentScope=*/true).failed())
              loopRes = mlir::failure();
          }
          // End the current condition-variable iteration at the loop
          // back-edge so it is re-created each iteration. The final
          // iteration's cleanup is performed by the enclosing loop scope exit.
          if (s.getConditionVariable()) {
            assert(condVarCleanupDepth.isValid() &&
                   "cond var cleanup depth must be recorded in condBuilder");
            emitCleanupsForEarlyExit(condVarCleanupDepth);
          }
          emitStopPoint(&s);
        });
    if (loopHint)
      whileOp.setLoopHintAttr(loopHint);
    return loopRes;
  };

  mlir::LogicalResult res = mlir::success();
  mlir::Location scopeLoc = getLoc(s.getSourceRange());
  cir::ScopeOp::create(builder, scopeLoc, /*scopeBuilder=*/
                       [&](mlir::OpBuilder &b, mlir::Location loc) {
                         LexicalScope lexScope{*this, loc,
                                               builder.getInsertionBlock()};
                         lexScope.setAsLoop();
                         if (hasNamedTarget) {
                           NamedLoopControlTarget target;
                           target.loopOrSwitch = &s;
                           target.scope = &lexScope;
                           target.breakTargetDepth =
                               lexScope.getCleanupStackDepth();
                           target.continueTargetDepth =
                               lexScope.getCleanupStackDepth();
                           target.breakLabel =
                               createNamedLoopControlLabel("break");
                           target.continueLabel =
                               createNamedLoopControlLabel("continue");
                           namedLoopControlStack.push_back(std::move(target));
                           namedTarget = &namedLoopControlStack.back();
                           namedBreakLabel = namedTarget->breakLabel;
                         }
                         res = whileStmtBuilder();
                         if (hasNamedTarget) {
                           namedLoopControlStack.pop_back();
                           namedTarget = nullptr;
                         }
                       });

  if (res.failed())
    return res;

  terminateBody(builder, whileOp.getBody(), getLoc(s.getEndLoc()));
  if (hasNamedTarget)
    emitSyntheticLabel(getLoc(s.getEndLoc()), namedBreakLabel);
  return mlir::success();
}

mlir::LogicalResult CIRGenFunction::emitSwitchBody(const Stmt *s) {
  // It is rare but legal if the switch body is not a compound stmt. e.g.,
  //
  //  switch(a)
  //    while(...) {
  //      case1
  //      ...
  //      case2
  //      ...
  //    }
  if (!isa<CompoundStmt>(s))
    return emitStmt(s, /*useCurrentScope=*/true);

  auto *compoundStmt = cast<CompoundStmt>(s);

  mlir::Block *swtichBlock = builder.getBlock();
  for (auto *c : compoundStmt->body()) {
    // Unwrap AttributedStmt (e.g. [[unlikely]] case 1: ...) and LabelStmt
    // (e.g. L: case 1: ...) chains to find the underlying SwitchCase.
    const Stmt *inner = c;
    llvm::ArrayRef<const Attr *> attrs;
    SmallVector<const LabelStmt *, 2> labelStmts;
    for (;;) {
      if (const auto *as = dyn_cast<AttributedStmt>(inner)) {
        inner = as->getSubStmt();
        attrs = as->getAttrs();
      } else if (const auto *ls = dyn_cast<LabelStmt>(inner)) {
        labelStmts.push_back(ls);
        inner = ls->getSubStmt();
      } else {
        break;
      }
    }

    if (auto *switchCase = dyn_cast<SwitchCase>(inner)) {
      // If the switch body block already ends with a terminator (e.g., from a
      // dead `break;` before the first case label, as in the macro pattern
      // `switch(x) { break; case X: ... }`), erase it. Code before the first
      // case label in a switch is unreachable — switch execution jumps directly
      // to case labels. OGCG handles this by clearing the insertion point after
      // terminators and checking HaveInsertPoint() to skip dead code.
      if (swtichBlock->mightHaveTerminator())
        if (auto *term = swtichBlock->getTerminator())
          term->erase();
      builder.setInsertionPointToEnd(swtichBlock);
      // Emit any labels that wrap the SwitchCase (e.g. "L: case 1:").
      // These labels are goto targets that happen to precede a case label
      // in the source. They need to be emitted in the switch block so that
      // gotos can branch to them.
      for (const auto *ls : labelStmts) {
        if (emitLabel(*ls->getDecl()).failed())
          return mlir::failure();
      }
      // If emitting labels created new blocks, update swtichBlock to the
      // current block. This ensures that subsequent cases are placed after
      // the label block, preserving source order for fallthrough semantics.
      // Without this, later cases would be inserted back in the original
      // switch body entry block, before the label block, causing
      // fallthrough to skip over label+case combinations.
      swtichBlock = builder.getBlock();
      // Reset insert point automatically, so that we can attach following
      // random stmt to the region of previous built case op to try to make
      // the being generated `cir.switch` to be in simple form.
      if (mlir::failed(
              emitSwitchCase(*switchCase, /*buildingTopLevelCase=*/true,
                             attrs)))
        return mlir::failure();

      continue;
    }

    // Otherwise, just build the statements in the nearest case region.
    if (mlir::failed(emitStmt(c, /*useCurrentScope=*/!isa<CompoundStmt>(c))))
      return mlir::failure();
  }

  return mlir::success();
}

namespace {
enum CSFCResult { CSFCFailure, CSFCFallThrough, CSFCSuccess };

static bool containsSwitchCaseStmt(const Stmt *s) {
  if (!s)
    return false;
  if (isa<SwitchCase>(s))
    return true;
  return llvm::any_of(s->children(),
                      [](const Stmt *child) { return containsSwitchCaseStmt(child); });
}

/// Find the emitted statements for a constant-folded switch case.
/// Mirrors OGCG CollectStatementsForCase logic.
static CSFCResult
collectStatementsForCase(CIRGenFunction &cgf, const Stmt *s,
                         const SwitchCase *targetCase, bool &foundCase,
                         SmallVectorImpl<const Stmt *> &resultStmts) {
  if (!s)
    return targetCase ? CSFCSuccess : CSFCFallThrough;

  if (const auto *sc = dyn_cast<SwitchCase>(s)) {
    if (s == targetCase) {
      foundCase = true;
      return collectStatementsForCase(cgf, sc->getSubStmt(), nullptr, foundCase,
                                      resultStmts);
    }
    return collectStatementsForCase(cgf, sc->getSubStmt(), targetCase,
                                    foundCase, resultStmts);
  }

  if (!targetCase && isa<BreakStmt>(s))
    return CSFCSuccess;

  if (const auto *cs = dyn_cast<CompoundStmt>(s)) {
    auto i = cs->body_begin();
    auto e = cs->body_end();
    bool startedInLiveCode = foundCase;
    unsigned startSize = resultStmts.size();

    if (targetCase) {
      bool hadSkippedDecl = false;
      for (; targetCase && i != e; ++i) {
        hadSkippedDecl |= cgf.mightAddDeclToScope(*i);
        switch (collectStatementsForCase(cgf, *i, targetCase, foundCase,
                                         resultStmts)) {
        case CSFCFailure:
          return CSFCFailure;
        case CSFCSuccess:
          if (foundCase) {
            if (hadSkippedDecl)
              return CSFCFailure;
            for (++i; i != e; ++i)
              if (cgf.containsLabel(*i, /*ignoreCaseStmts=*/true))
                return CSFCFailure;
            return CSFCSuccess;
          }
          break;
        case CSFCFallThrough:
          assert(foundCase && "didn't find case but returned fallthrough");
          targetCase = nullptr;
          if (hadSkippedDecl)
            return CSFCFailure;
          break;
        }
      }

      if (!foundCase)
        return CSFCSuccess;
    }

    bool anyDecls = false;
    for (; i != e; ++i) {
      anyDecls |= cgf.mightAddDeclToScope(*i);
      switch (
          collectStatementsForCase(cgf, *i, nullptr, foundCase, resultStmts)) {
      case CSFCFailure:
        return CSFCFailure;
      case CSFCFallThrough:
        break;
      case CSFCSuccess:
        for (++i; i != e; ++i)
          if (cgf.containsLabel(*i, /*ignoreCaseStmts=*/true))
            return CSFCFailure;
        return CSFCSuccess;
      }
    }

    if (anyDecls) {
      if (startedInLiveCode && !cgf.containsBreak(s)) {
        resultStmts.resize(startSize);
        resultStmts.push_back(s);
      } else {
        return CSFCFailure;
      }
    }

    return CSFCFallThrough;
  }

  if (targetCase) {
    if (cgf.containsLabel(s, /*ignoreCaseStmts=*/true))
      return CSFCFailure;
    return CSFCSuccess;
  }

  if (cgf.containsBreak(s))
    return CSFCFailure;

  resultStmts.push_back(s);
  return CSFCFallThrough;
}

/// Find the case statements to emit for a switch with a constant condition.
/// Mirrors OGCG FindCaseStatementsForValue logic.
static bool
findCaseStatementsForValue(CIRGenFunction &cgf, const SwitchStmt &s,
                           const llvm::APSInt &constantCondValue,
                           SmallVectorImpl<const Stmt *> &resultStmts,
                           const SwitchCase *&resultCase) {
  const SwitchCase *sc = s.getSwitchCaseList();
  const DefaultStmt *defaultCase = nullptr;

  for (; sc; sc = sc->getNextSwitchCase()) {
    if (const auto *ds = dyn_cast<DefaultStmt>(sc)) {
      defaultCase = ds;
      continue;
    }

    const auto *cs = cast<CaseStmt>(sc);
    if (cs->getRHS())
      return false;
    if (cs->getLHS()->EvaluateKnownConstInt(cgf.getContext()) ==
        constantCondValue)
      break;
  }

  if (!sc) {
    if (!defaultCase)
      return !cgf.containsLabel(&s);
    sc = defaultCase;
  }

  bool foundCase = false;
  resultCase = sc;
  return collectStatementsForCase(cgf, s.getBody(), sc, foundCase,
                                  resultStmts) != CSFCFailure &&
         foundCase;
}
} // namespace

mlir::LogicalResult CIRGenFunction::emitSwitchStmt(
    const clang::SwitchStmt &s, llvm::ArrayRef<const Attr *> attrs) {
  SwitchOp swop;
  bool emittedStructuredSwitch = false;
  NamedLoopControlTarget *namedTarget = nullptr;
  std::string namedBreakLabel;
  bool hasNamedTarget = isActiveNamedLoopTarget(&s);
  std::optional<int32_t> hlslControlFlowHint = getHLSLControlFlowHint(attrs);
  auto switchStmtBuilder = [&]() -> mlir::LogicalResult {
    // Match OGCG: fold switch conditions that evaluate to a constant and emit
    // only the live case statements when it is safe.
    llvm::APSInt constantCondValue;
    if (constantFoldsToSimpleInteger(s.getCond(), constantCondValue)) {
      SmallVector<const Stmt *, 4> caseStmts;
      const SwitchCase *targetCase = nullptr;
      if (findCaseStatementsForValue(*this, s, constantCondValue, caseStmts,
                                     targetCase)) {
        // CIR emits switch labels through emitSwitchBody/emitSwitchCase with a
        // tracked condition type. If the selected live statements still carry
        // nested case/default labels (e.g. Duff's device), keep the structured
        // switch path to preserve those invariants.
        if (llvm::any_of(caseStmts, [](const Stmt *stmt) {
              return containsSwitchCaseStmt(stmt);
            })) {
          // Fall back to structured switch emission below.
        } else {
          if (s.getInit())
            if (emitStmt(s.getInit(), /*useCurrentScope=*/true).failed())
              return mlir::failure();
          if (s.getConditionVariable())
            emitDecl(*s.getConditionVariable());
          maybeEmitDeferredVarDeclInit(s.getConditionVariable());

          // A nested compound-statement block must open its own cleanup
          // LexicalScope so its locals are destroyed at the block's closing
          // brace, exactly as outside a switch. Forcing useCurrentScope here
          // would route it through emitCompoundStmtWithoutScope and defer the
          // destructors to the enclosing case/switch scope.
          for (const Stmt *liveStmt : caseStmts)
            if (emitStmt(liveStmt,
                         /*useCurrentScope=*/!isa<CompoundStmt>(liveStmt))
                    .failed())
              return mlir::failure();
          return mlir::success();
        }
      }
    }

    if (s.getInit()) {
      if (emitStmt(s.getInit(), /*useCurrentScope=*/true).failed())
        return mlir::failure();
      // If the init-statement pushed RAII cleanups (e.g.
      // switch (G g; expr)), set breakTargetDepth on the switch scope
      // to exclude the init variable's destructor from break's inline
      // emission. The enclosing scope exit handles it instead.
      // Same pattern as for-init.
      //
      // Note: setAsSwitch() hasn't been called yet (it's called inside
      // SwitchOp::create below), but curLexScope is the outer LexicalScope
      // that emitBreakStmt will find when it walks the scope chain. We need
      // to wait until after setAsSwitch() to do the scope walk, OR we can
      // just set it directly on curLexScope since that's the scope that will
      // be marked as switch later.
      curLexScope->setBreakTargetDepth(ehStack.stable_begin());
    }

    if (s.getConditionVariable()) {
      emitDecl(*s.getConditionVariable());
      // If the condition variable has a non-trivial destructor, set
      // breakTargetDepth to exclude its cleanup from break's inline
      // emission. The enclosing LexicalScope exit handles it instead.
      // Without this, break emits the destructor inline AND the scope
      // exit emits it again, causing double destruction.
      // Same pattern as for-loop condition variables.
      QualType type = s.getConditionVariable()->getType();
      if (type.isDestructedType())
        curLexScope->setBreakTargetDepth(ehStack.stable_begin());
    }

    mlir::Value condV = emitScalarExpr(s.getCond());
    closeActiveSyntheticTry();

    // CIR SwitchOp requires an IntType condition. If the condition is a
    // BoolType (e.g. from enum class : bool), promote it to IntType via a
    // bool_to_int cast, matching how C++ integral promotion works for
    // switch conditions.
    if (mlir::isa<cir::BoolType>(condV.getType())) {
      auto intTy = cir::IntType::get(builder.getContext(), 8, false);
      condV = builder.createCast(getLoc(s.getCond()->getExprLoc()),
                                 cir::CastKind::bool_to_int, condV, intTy);
    }

    maybeEmitDeferredVarDeclInit(s.getConditionVariable());
    // TODO: PGO and likelihood (e.g. PGO.haveRegionCounts())
    // Switch case likelihood ([[likely]]/[[unlikely]]) is implemented below
    // (cir.branch_weights metadata computation).

    // If the switch has a condition wrapped by __builtin_unpredictable,
    // mark the switch op so lowering can emit !unpredictable metadata.
    // Matches OGCG CGStmt.cpp:2520-2531.
    bool isUnpredictable = false;
    if (cgm.getCodeGenOpts().OptimizationLevel != 0) {
      if (auto *call = dyn_cast<CallExpr>(s.getCond())) {
        if (auto *fd = dyn_cast_or_null<FunctionDecl>(call->getCalleeDecl())) {
          if (fd->getBuiltinID() == Builtin::BI__builtin_unpredictable)
            isUnpredictable = true;
        }
      }
    }

    mlir::LogicalResult res = mlir::success();
    uint64_t switchAtom = reserveSourceAtomGroup();
    swop = SwitchOp::create(
        builder, getLoc(s.getBeginLoc()), condV,
        /*switchBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc, mlir::OperationState &os) {
          curLexScope->setAsSwitch();

          condTypeStack.push_back(condV.getType());

          res = emitSwitchBody(s.getBody());

          condTypeStack.pop_back();
        });
    emittedStructuredSwitch = true;
    addOpToSpecificSourceAtom(swop, condV, switchAtom);

    if (hlslControlFlowHint)
      swop->setAttr(
          "cir.hlsl.controlflow.hint",
          mlir::IntegerAttr::get(mlir::IntegerType::get(&getMLIRContext(), 32),
                                 *hlslControlFlowHint));

    if (isUnpredictable)
      swop->setAttr("cir.unpredictable",
                    mlir::UnitAttr::get(&getMLIRContext()));

    return res;
  };

  // The switch scope contains the full source range for SwitchStmt.
  mlir::Location scopeLoc = getLoc(s.getSourceRange());
  mlir::LogicalResult res = mlir::success();
  cir::ScopeOp::create(builder, scopeLoc, /*scopeBuilder=*/
                       [&](mlir::OpBuilder &b, mlir::Location loc) {
                         LexicalScope lexScope{*this, loc,
                                               builder.getInsertionBlock()};
                         // Record the SwitchStmt's source range so that a goto
                         // to a label inside the switch is recognized as staying
                         // within this scope. This scope owns the switch
                         // condition-variable / init-statement cleanup. Without
                         // this, emitGotoStmt's forward-goto walk sees an
                         // invalid source range and assumes the goto exits this
                         // scope, incorrectly emitting the condition variable's
                         // cleanup and double-destroying it on the goto path.
                         //
                         lexScope.setClangSourceRange(s.getSourceRange());
                         if (hasNamedTarget) {
                           NamedLoopControlTarget target;
                           target.loopOrSwitch = &s;
                           target.scope = &lexScope;
                           target.breakTargetDepth =
                               lexScope.getCleanupStackDepth();
                           target.breakLabel =
                               createNamedLoopControlLabel("break");
                           namedLoopControlStack.push_back(std::move(target));
                           namedTarget = &namedLoopControlStack.back();
                           namedBreakLabel = namedTarget->breakLabel;
                         }
                         res = switchStmtBuilder();
                         if (hasNamedTarget) {
                           namedLoopControlStack.pop_back();
                           namedTarget = nullptr;
                         }
                       });

  if (!emittedStructuredSwitch)
    if (hasNamedTarget)
      emitSyntheticLabel(getLoc(s.getEndLoc()), namedBreakLabel);
  if (!emittedStructuredSwitch)
    return res;

  llvm::SmallVector<CaseOp> cases;
  swop.collectCases(cases);
  for (auto caseOp : cases)
    terminateBody(builder, caseOp.getCaseRegion(), caseOp.getLoc());
  terminateBody(builder, swop.getBody(), swop.getLoc());

  swop.setAllEnumCasesCovered(s.isAllEnumCasesCovered());

  incrementProfileCounter(&s);

  if (hasNamedTarget)
    emitSyntheticLabel(getLoc(s.getEndLoc()), namedBreakLabel);

  return res;
}

void CIRGenFunction::emitReturnOfRValue(mlir::Location loc, RValue rv,
                                        QualType ty) {
  if (rv.isScalar()) {
    if (requiresReturnValueCheck())
      emitReturnValueCheck(loc, rv.getValue());
    auto storeOp = builder.createStore(loc, rv.getValue(), returnValue);
    cgm.decorateOperationWithTBAA(storeOp, cgm.getTBAAAccessInfo(ty));
    addOpToCurrentSourceAtom(storeOp, rv.getValue());
  } else if (rv.isAggregate()) {
    LValue dest = makeAddrLValue(returnValue, ty);
    LValue src = makeAddrLValue(rv.getAggregateAddress(), ty);
    emitAggregateCopy(dest, src, ty, getOverlapForReturnValue());
  } else {
    emitStoreOfComplex(loc, rv.getComplexValue(),
                       makeAddrLValue(returnValue, ty), /*isInit=*/true);
  }
  mlir::Block *retBlock = curLexScope->getOrCreateRetBlock(*this, loc);
  emitBranchThroughCleanup(loc, returnBlock(retBlock));
}

//===----------------------------------------------------------------------===//
//                         CapturedStmt Emission
//===----------------------------------------------------------------------===//

LValue CIRGenFunction::initCapturedStruct(const CapturedStmt &s) {
  const RecordDecl *rd = s.getCapturedRecordDecl();
  QualType recordTy = getContext().getCanonicalTagType(rd);

  // Initialize the captured struct.
  LValue slotLV = makeAddrLValue(
      createMemTemp(recordTy, getLoc(s.getBeginLoc()), "agg.captured"),
      recordTy);

  RecordDecl::field_iterator curField = rd->field_begin();
  for (CapturedStmt::const_capture_init_iterator i = s.capture_init_begin(),
                                                  e = s.capture_init_end();
       i != e; ++i, ++curField) {
    LValue lv =
        emitLValueForFieldInitialization(slotLV, *curField,
                                         curField->getName());
    if (curField->hasCapturedVLAType()) {
      emitLambdaVLACapture(curField->getCapturedVLAType(), lv);
    } else {
      emitInitializerForField(*curField, lv, *i);
    }
  }

  return slotLV;
}

cir::FuncOp
CIRGenFunction::generateCapturedStmtFunction(const CapturedStmt &s) {
  return generateCapturedStmtFunction(
      s, [](CIRGenFunction &cgf, const CapturedDecl &cd) {
        cgf.incrementProfileCounter(cd.getBody());
        (void)cgf.emitStmt(cd.getBody(), /*useCurrentScope=*/true);
      });
}

cir::FuncOp CIRGenFunction::generateCapturedStmtFunction(
    const CapturedStmt &s,
    llvm::function_ref<void(CIRGenFunction &, const CapturedDecl &)>
        bodyEmitter) {
  assert(capturedStmtInfo &&
         "CapturedStmtInfo should be set when generating the captured "
         "function");
  const CapturedDecl *cd = s.getCapturedDecl();
  const RecordDecl *rd = s.getCapturedRecordDecl();
  SourceLocation loc = s.getBeginLoc();
  assert(cd->hasBody() && "missing CapturedDecl body");

  // Build the argument list.
  FunctionArgList args;
  args.append(cd->param_begin(), cd->param_end());

  // Build the function type: void(ptr_to_captured_struct).
  llvm::SmallVector<mlir::Type> argTypes;
  for (const auto *param : args)
    argTypes.push_back(convertType(param->getType()));
  auto voidTy = cir::VoidType::get(builder.getContext());
  auto funcType = cir::FuncType::get(argTypes, voidTy);

  // Create the function with a unique name. getUniqueGlobalName returns
  // the base name on the first call, then appends .1, .2, etc.
  mlir::Location mlirLoc = getLoc(loc);
  std::string funcName =
      cgm.getUniqueGlobalName(capturedStmtInfo->getHelperName().str());
  cir::FuncOp fn =
      cgm.createCIRFunction(mlirLoc, funcName, funcType, /*fd=*/nullptr);
  fn.setLinkageAttr(cir::GlobalLinkageKindAttr::get(
      &cgm.getMLIRContext(), cir::GlobalLinkageKind::InternalLinkage));
  if (capturedStmtInfo->hasPGOFuncNameOverride()) {
    std::string pgoFuncNameOverride = cgm.getUniqueGlobalName(
        capturedStmtInfo->getPGOFuncNameOverride().str());
    fn->setAttr("cir.frontend_pgo_name",
                cgm.getStringAttrCached(pgoFuncNameOverride));
  }
  cgm.trackCoverageMappingDecl(const_cast<CapturedDecl *>(cd), fn.getSymName(),
                               cgm.getCoverageMappingPGOName(fn));

  // Set definition attributes (noinline, nounwind, optnone, uwtable, ssp).
  // OGCG calls CGM.SetInternalFunctionAttributes(CD, F, FuncInfo) here.
  // The null-fd path handles noinline/uwtable/ssp but not optnone (which
  // requires a Decl). CapturedDecl is not a FunctionDecl, so we handle
  // optnone and nothrow separately.
  //
  // Also apply default target/function attributes (frame-pointer,
  // stack-protector-buffer-size, target-cpu/tune-cpu/target-features, etc.)
  // to match OGCG's internal helper function path.
  {
    mlir::NamedAttrList defaultAttrs;
    if (auto existingExtraAttrs = fn.getExtraAttrs())
      defaultAttrs = mlir::NamedAttrList{
          existingExtraAttrs->getElements().getValue()};

    cgm.getDefaultFunctionAttributes(/*name=*/"", /*hasOptnone=*/false,
                                     /*attrOnCallSite=*/false, defaultAttrs);

    llvm::StringRef targetCPU = cgm.getTarget().getTargetOpts().CPU;
    if (!targetCPU.empty())
      defaultAttrs.set("target-cpu", cgm.getStringAttrCached(targetCPU));
    llvm::StringRef tuneCPU = cgm.getTarget().getTargetOpts().TuneCPU;
    if (!tuneCPU.empty())
      defaultAttrs.set("tune-cpu", cgm.getStringAttrCached(tuneCPU));
    auto features = cgm.getTarget().getTargetOpts().Features;
    if (!features.empty()) {
      llvm::erase_if(features, [&](const std::string &feature) {
        return cgm.getTarget().isReadOnlyFeature(feature.substr(1));
      });
      llvm::sort(features);
      defaultAttrs.set("target-features",
                       cgm.getStringAttrCached(llvm::join(features, ",")));
    }

    fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        defaultAttrs.getDictionary(&cgm.getMLIRContext())));
  }

  cgm.setCIRFunctionAttributesForDefinition(/*gd=*/GlobalDecl{}, fn);

  // Set optnone at -O0 (OGCG SetLLVMFunctionAttributesForDefinition checks
  // D->hasAttr<MinSizeAttr/AlwaysInlineAttr> which are absent on CapturedDecl).
  if (!cgm.getCodeGenOpts().DisableO0ImplyOptNone &&
      cgm.getCodeGenOpts().OptimizationLevel == 0)
    fn.setOptNone(true);

  // Set min-legal-vector-width for X86. OGCG sets this in FinishFunction()
  // via LargestVectorWidth; captured statements go through StartFunction +
  // FinishFunction in OGCG. Since CIR's captured stmt flow doesn't use
  // FinishFunction, set it here with the initial value of 0.
  if (getContext().getTargetInfo().getTriple().isX86()) {
    mlir::NamedAttrList fnAttrs;
    if (auto ea = fn.getExtraAttrs())
      fnAttrs = mlir::NamedAttrList{ea->getElements().getValue()};
    fnAttrs.set("min-legal-vector-width", cgm.getStringAttrCached("0"));
    fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        fnAttrs.getDictionary(&cgm.getMLIRContext())));
  }

  // OGCG adds NoUnwind when CapturedDecl is nothrow (CGStmt.cpp:3371-3372).
  if (cd->isNothrow()) {
    mlir::NamedAttrList attrs;
    if (auto ea = fn.getExtraAttrs())
      attrs = mlir::NamedAttrList{ea->getElements().getValue()};
    attrs.set(cir::NoThrowAttr::getMnemonic(),
              cir::NoThrowAttr::get(&cgm.getMLIRContext()));
    fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(&cgm.getMLIRContext())));
  }

  // Generate the function body.
  {
    mlir::OpBuilder::InsertionGuard insertGuard(builder);

    mlir::Block *entryBB = fn.addEntryBlock();
    builder.setInsertionPointToStart(entryBB);

    mlir::Location fusedLoc = mlir::FusedLoc::get(
        &getMLIRContext(),
        {getLoc(cd->getBody()->getBeginLoc()),
         getLoc(cd->getBody()->getEndLoc())});

    SymTableScopeTy varScope(symbolTable);
    LexicalScope lexScope(*this, fusedLoc, entryBB);

    // Set the source location for the captured function body so that nested
    // emissions (e.g., nested captured statements) have a valid location.
    SourceLocRAIIObject fnLoc{*this, mlirLoc};

    curGD = GlobalDecl(cd);
    curFn = fn;
    curCodeDecl = cd;
    curFuncDecl = nullptr;
    PGO->assignRegionCounters(curGD, fn);

    // Emit parameter handling.
    emitFunctionProlog(args, entryBB, /*fd=*/nullptr, loc);

    // Set the context parameter in CapturedStmtInfo.
    Address declPtr = getAddrOfLocalVar(cd->getContextParam());
    capturedStmtInfo->setContextValue(
        builder.createLoad(getLoc(loc), declPtr));

    // Initialize variable-length arrays.
    QualType tagTy = getContext().getCanonicalTagType(rd);
    LValue base =
        makeNaturalAlignAddrLValue(capturedStmtInfo->getContextValue(), tagTy);
    for (auto *fd : rd->fields()) {
      if (fd->hasCapturedVLAType()) {
        auto exprArg =
            emitLoadOfLValue(emitLValueForField(base, fd), s.getBeginLoc())
                .getValue();
        auto vat = fd->getCapturedVLAType();
        vlaSizeMap[vat->getSizeExpr()] = exprArg;
      }
    }

    // If 'this' is captured, load it into CXXThisValue.
    if (capturedStmtInfo->isCXXThisExprCaptured()) {
      FieldDecl *fd = capturedStmtInfo->getThisFieldDecl();
      LValue thisLValue = emitLValueForField(base, fd);
      cxxThisValue = emitLoadOfLValue(thisLValue, loc).getValue();
    }

    // Emit the body.
    bodyEmitter(*this, *cd);
  }

  return fn;
}

mlir::LogicalResult
CIRGenFunction::emitCapturedStmt(const CapturedStmt &s,
                                 CapturedRegionKind k) {
  LValue capStruct = initCapturedStruct(s);

  // Generate the outlined captured function in a separate CIRGenFunction.
  cir::FuncOp fn;
  {
    CIRGenFunction cgf(cgm, builder, /*suppressNewContext=*/true);
    auto *info = new CGCapturedStmtInfo(s, k);
    if (k == CR_OpenMP) {
      auto parentFn = mlir::dyn_cast<cir::FuncOp>(curFn);
      assert(parentFn &&
             "OpenMP captured helper requires an enclosing CIR function");
      llvm::StringRef parentName = parentFn.getSymName();
      if (auto attr =
              parentFn->getAttrOfType<mlir::StringAttr>("cir.frontend_pgo_name"))
        parentName = attr.getValue();
      // Reuse the per-region disambiguation index that getOpenMPHelperPGOName
      // allocated for this CapturedStmt (set on the omp.ParallelOp attribute
      // before this region body is emitted) so the outlined function's PGO name
      // matches the attribute and region-counter names. Allocate if absent in
      // case some path reaches here without first setting the attribute.
      auto [it, inserted] = openMPOutlinedRegionIndex.try_emplace(&s, 0);
      if (inserted)
        it->second = openMPOutlinedPGOCounter[parentFn.getOperation()]++;
      info->setPGOFuncNameOverride(
          getOpenMPOutlinedPGOHelperName(cgm, parentName, it->second));
    }
    CGCapturedStmtRAII capInfoRAII(cgf, info);
    fn = cgf.generateCapturedStmtFunction(s);
    delete cgf.capturedStmtInfo;
    cgf.capturedStmtInfo = nullptr;
  }

  // Call the outlined function. Use invoke if there are active EH cleanups,
  // matching OGCG's EmitCallOrInvoke (CGStmt.cpp).
  mlir::Value structPtr = capStruct.getPointer();
  emitRuntimeCallOrInvoke(getLoc(s.getBeginLoc()), fn, {structPtr});

  return mlir::success();
}
