//===----- CGCoroutine.cpp - Emit CIR Code for C++ coroutines -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with C++ code generation of coroutines.
//
//===----------------------------------------------------------------------===//

#include "CIRGenFunction.h"
#include "mlir/Support/LLVM.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"

using namespace clang;
using namespace clang::CIRGen;

#ifndef NDEBUG
static QualType
getCoroutineSuspendExprReturnType(const ASTContext &ctx,
                                  const CoroutineSuspendExpr *e) {
  const auto *re = e->getResumeExpr();
  assert(isa<CallExpr>(re) && "unexpected suspend expression type");
  return cast<CallExpr>(re)->getCallReturnType(ctx);
}
#endif

struct clang::CIRGen::CGCoroData {
  // What is the current await expression kind and how many
  // await/yield expressions were encountered so far.
  // These are used to generate pretty labels for await expressions in LLVM IR.
  cir::AwaitKind currentAwaitKind = cir::AwaitKind::Init;
  unsigned awaitNum = 0;
  unsigned yieldNum = 0;
  // Stores the __builtin_coro_id emitted in the function so that we can supply
  // it as the first argument to other builtins.
  cir::CallOp coroId = nullptr;

  // Stores the result of __builtin_coro_begin call.
  mlir::Value coroBegin = nullptr;

  // The deallocate expression from CoroutineBodyStmt, used to emit
  // coroutine frame deallocation (coro.free + operator delete).
  Stmt *deallocate = nullptr;

  // Stores the cleanup depth of the function-level final suspend entry.
  // co_return emits early-exit cleanups down to this depth before jumping to
  // the symbolic final-suspend label.
  // Reused for the per-suspend destroy edge too: each non-final suspend's
  // destroy edge runs the body-local destructors down to this same depth. The
  // depth sits ABOVE the promise/GRO/frame cleanups (which the coroutine's own
  // destroy machinery already handles), so the destroy edge only adds the
  // body-local cleanups that were previously missing -- it must NOT re-run the
  // promise/GRO, or non-trivially-destructible promises get double-destroyed.
  std::optional<EHScopeStack::stable_iterator> finalSuspendCleanupDepth;

  // Function-local symbolic label that marks the entry to final_suspend.
  // Using cir.goto/cir.label lets co_return escape nested structured regions
  // without illegal cross-region branches.
  std::string finalSuspendLabel;

  // Function-local symbolic label that marks the start of the destroy cleanup
  // path after final_suspend has been bypassed or completed.
  std::string destroyCleanupLabel;

  // How many co_return statements are in the coroutine. Used to decide whether
  // we need to add co_return; equivalent at the end of the user authored body.
  unsigned coreturnCount = 0;

  // All top-level returns funnel through a shared cleanup block and then a
  // shared coro.ret block. This keeps frame deallocation on the destroy path
  // while still ensuring there is exactly one fallthrough coro.end.
  mlir::Block *cleanupRetBlock = nullptr;
  mlir::Block *coroRetBlock = nullptr;

  // The promise type's 'unhandled_exception' handler, if it defines one.
  Stmt *exceptionHandler = nullptr;

  // Tracks whether init-suspend await_resume threw and the coroutine body
  // must be skipped in favor of final_suspend.
  mlir::Value resumeEHVar = nullptr;

};

// Defining these here allows to keep CGCoroData private to this file.
CIRGenFunction::CGCoroInfo::CGCoroInfo() {}
CIRGenFunction::CGCoroInfo::~CGCoroInfo() {}

namespace {
// FIXME: both GetParamRef and ParamReferenceReplacerRAII are good template
// candidates to be shared among LLVM / CIR codegen.

// Hunts for the parameter reference in the parameter copy/move declaration.
struct GetParamRef : public StmtVisitor<GetParamRef> {
public:
  DeclRefExpr *expr = nullptr;
  GetParamRef() {}
  void VisitDeclRefExpr(DeclRefExpr *e) {
    assert(expr == nullptr && "multilple declref in param move");
    expr = e;
  }
  void VisitStmt(Stmt *s) {
    for (Stmt *c : s->children()) {
      if (c)
        Visit(c);
    }
  }
};

// This class replaces references to parameters to their copies by changing
// the addresses in CGF.LocalDeclMap and restoring back the original values in
// its destructor.
struct ParamReferenceReplacerRAII {
  CIRGenFunction::DeclMapTy savedLocals;
  CIRGenFunction::DeclMapTy &localDeclMap;

  ParamReferenceReplacerRAII(CIRGenFunction::DeclMapTy &localDeclMap)
      : localDeclMap(localDeclMap) {}

  void addCopy(const DeclStmt *pm) {
    // Figure out what param it refers to.

    assert(pm->isSingleDecl());
    const VarDecl *vd = static_cast<const VarDecl *>(pm->getSingleDecl());
    const Expr *initExpr = vd->getInit();
    GetParamRef visitor;
    visitor.Visit(const_cast<Expr *>(initExpr));
    assert(visitor.expr);
    DeclRefExpr *dreOrig = visitor.expr;
    auto *pd = dreOrig->getDecl();

    auto it = localDeclMap.find(pd);
    assert(it != localDeclMap.end() && "parameter is not found");
    savedLocals.insert({pd, it->second});

    auto copyIt = localDeclMap.find(vd);
    assert(copyIt != localDeclMap.end() && "parameter copy is not found");
    it->second = copyIt->getSecond();
  }

  ~ParamReferenceReplacerRAII() {
    for (auto &&savedLocal : savedLocals) {
      localDeclMap.insert({savedLocal.first, savedLocal.second});
    }
  }
};

struct CallCoroDelete final : EHScopeStack::Cleanup {
  Stmt *deallocate;

  explicit CallCoroDelete(Stmt *deallocate) : deallocate(deallocate) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    (void)flags;
    if (deallocate)
      cgf.emitCoroutineDeallocationStmt(deallocate);
  }
};

struct CallCoroEnd final : EHScopeStack::Cleanup {
  void emit(CIRGenFunction &cgf, Flags flags) override {
    assert(flags.isForEHCleanup() &&
           "coro.end unwind cleanup must only run during EH cleanup emission");

    auto &builder = cgf.getBuilder();
    mlir::Location loc = cgf.currSrcLoc
                             ? *cgf.currSrcLoc
                             : mlir::UnknownLoc::get(builder.getContext());
    cir::ConstantOp nullPtr = builder.getNullPtr(cgf.voidPtrTy, loc);
    cgf.emitCoroEndBuiltinCall(loc, nullPtr, /*isUnwind=*/true);
    cir::CallOp isInRamp = cgf.emitCoroIsInRampBuiltinCall(loc);

    mlir::Block *curBlock = builder.getBlock();
    mlir::Region *parentRegion = curBlock->getParent();
    auto *cleanupContBlock = new mlir::Block();
    auto *skipCleanupsBlock = new mlir::Block();
    parentRegion->push_back(cleanupContBlock);
    parentRegion->push_back(skipCleanupsBlock);

    builder.setInsertionPointToEnd(curBlock);
    cir::BrCondOp::create(builder, loc, isInRamp.getResult(), cleanupContBlock,
                          skipCleanupsBlock);

    {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToStart(skipCleanupsBlock);
      if (cgf.callWithExceptionCtx)
        cir::YieldOp::create(builder, loc);
      else
        cir::ResumeOp::create(builder, loc);
    }

    builder.setInsertionPointToStart(cleanupContBlock);
  }
};
} // namespace

mlir::Value CIRGenFunction::getCoroIdResult() const {
  if (curCoro.data && curCoro.data->coroId)
    return curCoro.data->coroId.getResult();
  // Matching OGCG: emit error when coro_alloc/coro_begin/coro_free is used
  // without a prior __builtin_coro_id call.
  cgm.errorNYI("coroutine builtin used without prior __builtin_coro_id");
  return {};
}

void CIRGenFunction::initCoroData(cir::CallOp coroId,
                                  const CallExpr *coroIdExpr) {
  if (curCoro.data) {
    // Already in a coroutine context — either a C++ coroutine body or a
    // duplicate __builtin_coro_id call. Matching OGCG diagnostics.
    if (coroIdExpr)
      cgm.errorNYI(coroIdExpr->getSourceRange(),
                    "__builtin_coro_id used in a function that is already a "
                    "coroutine");
    return;
  }
  curCoro.data = std::make_unique<CGCoroData>();
  curCoro.data->coroId = coroId;
  // Plain C builtin experimentation needs CoroData for sibling builtins, but
  // it must not mark the function as a coroutine body.
}

void CIRGenFunction::setCoroBeginValue(mlir::Value coroBegin) {
  if (curCoro.data)
    curCoro.data->coroBegin = coroBegin;
}

static bool emitSynthesizedCoroutineDeallocCall(CIRGenFunction &cgf,
                                                const CallExpr *callExpr) {
  // CoroutineBodyStmt synthesizes frame deallocation as a plain
  // operator delete(coro.free(...), coro.size(), ...) call, so we need
  // to rebuild OGCG's coroutine-specific null guard around the hoisted
  // coro.free result before lowering the delete call.
  const auto *callee = dyn_cast<DeclRefExpr>(
      callExpr->getCallee()->IgnoreParenImpCasts());
  const auto *deleteFD =
      callee ? dyn_cast<FunctionDecl>(callee->getDecl()) : nullptr;
  if (!deleteFD || deleteFD->getOverloadedOperator() != OO_Delete)
    return false;

  if (callExpr->getNumArgs() == 0)
    return false;

  const auto *coroFreeCall =
      dyn_cast<CallExpr>(callExpr->getArg(0)->IgnoreParenImpCasts());
  const auto *coroFreeCallee =
      coroFreeCall ? dyn_cast<DeclRefExpr>(
                         coroFreeCall->getCallee()->IgnoreParenImpCasts())
                   : nullptr;
  if (!coroFreeCallee)
    return false;

  const auto *coroFreeDecl =
      dyn_cast<FunctionDecl>(coroFreeCallee->getDecl());
  if (!coroFreeDecl ||
      coroFreeDecl->getBuiltinID() != Builtin::BI__builtin_coro_free)
    return false;

  mlir::Location loc = cgf.getLoc(callExpr->getSourceRange());
  CIRGenFunction::SourceLocRAIIObject callLoc(cgf, loc);

  mlir::Value coroFree = cgf.emitScalarExpr(callExpr->getArg(0));
  Address coroFreeSlot = cgf.createMemTemp(
      callExpr->getArg(0)->getType(), loc, "coro.free");
  cgf.getBuilder().createStore(loc, coroFree, coroFreeSlot);

  mlir::Value isNotNull = cgf.getBuilder().createCompare(
      loc, cir::CmpOpKind::ne, coroFree,
      cgf.getBuilder().getNullPtr(coroFree.getType(), loc));

  mlir::Block *curBlock = cgf.getBuilder().getBlock();
  mlir::Region *parentRegion = curBlock->getParent();
  auto *deleteBlock = new mlir::Block();
  auto *contBlock = new mlir::Block();
  parentRegion->push_back(deleteBlock);
  parentRegion->push_back(contBlock);
  cgf.getBuilder().setInsertionPointToEnd(curBlock);
  cir::BrCondOp::create(cgf.getBuilder(), loc, isNotNull, deleteBlock,
                        contBlock);

  cgf.getBuilder().setInsertionPointToStart(deleteBlock);
  mlir::Value loadedCoroFree = cgf.getBuilder().createLoad(loc, coroFreeSlot);

  CallArgList deleteArgs;
  deleteArgs.add(RValue::get(loadedCoroFree), callExpr->getArg(0)->getType());
  cgf.emitCallArgs(deleteArgs,
                   deleteFD->getType()->castAs<FunctionProtoType>(),
                   llvm::make_range(std::next(callExpr->arg_begin()),
                                    callExpr->arg_end()),
                   deleteFD, /*paramsToSkip=*/1);

  cir::CIRCallOpInterface callOrTryCall;
  cir::FuncOp calleePtr = cgf.cgm.getAddrOfFunction(deleteFD);
  CIRGenCallee deleteCallee =
      CIRGenCallee::forDirect(calleePtr, GlobalDecl(deleteFD));
  (void)cgf.emitCall(
      cgf.cgm.getTypes().arrangeFreeFunctionCall(
          deleteArgs, deleteFD->getType()->castAs<FunctionProtoType>()),
      deleteCallee, ReturnValueSlot(), deleteArgs, &callOrTryCall, loc);
  cir::BrOp::create(cgf.getBuilder(), loc, contBlock);
  cgf.getBuilder().setInsertionPointToStart(contBlock);
  return true;
}

void CIRGenFunction::emitCoroutineDeallocationStmt(const Stmt *stmt) {
  if (!stmt)
    return;

  if (const auto *callExpr = dyn_cast<CallExpr>(stmt))
    if (emitSynthesizedCoroutineDeallocCall(*this, callExpr))
      return;

  (void)emitStmt(stmt, /*useCurrentScope=*/true);
}

RValue CIRGenFunction::emitCoroutineFrame() {
  // When generating an await_suspend wrapper, __builtin_coro_frame() must
  // return the wrapper's frame parameter, not the outer coro.begin.
  if (curAwaitSuspendWrapper.framePtr) {
    return RValue::get(curAwaitSuspendWrapper.framePtr);
  }
  if (curCoro.data && curCoro.data->coroBegin) {
    return RValue::get(curCoro.data->coroBegin);
  }
  cgm.errorNYI("emitCoroutineFrame outside coroutine context");
  return RValue::get(nullptr);
}

static void createCoroData(CIRGenFunction &cgf,
                           CIRGenFunction::CGCoroInfo &curCoro,
                           cir::CallOp coroId) {
  assert(!curCoro.data && "EmitCoroutineBodyStatement called twice?");

  curCoro.data = std::make_unique<CGCoroData>();
  curCoro.data->coroId = coroId;
}

static llvm::StringRef getOrCreateFinalSuspendLabel(CGCoroData &coro,
                                                    cir::FuncOp fn) {
  if (coro.finalSuspendLabel.empty())
    coro.finalSuspendLabel = (fn.getSymName() + ".__coro_final").str();
  return coro.finalSuspendLabel;
}

static llvm::StringRef getOrCreateDestroyCleanupLabel(CGCoroData &coro,
                                                      cir::FuncOp fn) {
  if (coro.destroyCleanupLabel.empty())
    coro.destroyCleanupLabel = (fn.getSymName() + ".__coro_destroy").str();
  return coro.destroyCleanupLabel;
}

static mlir::LogicalResult
emitBodyAndFallthrough(CIRGenFunction &cgf, const CoroutineBodyStmt &s,
                       Stmt *body,
                       const CIRGenFunction::LexicalScope *currLexScope) {
  if (cgf.emitStmt(body, /*useCurrentScope=*/true).failed())
    return mlir::failure();
  // Note that classic codegen checks CanFallthrough by looking into the
  // availability of the insert block which is kinda brittle and unintuitive,
  // seems to be related with how landing pads are handled.
  //
  // CIRGen handles this by checking pre-existing co_returns in the current
  // scope instead.

  // From LLVM IR Gen: const bool CanFallthrough = Builder.GetInsertBlock();
  const bool canFallthrough = !currLexScope->hasCoreturn();
  if (canFallthrough)
    if (Stmt *onFallthrough = s.getFallthroughHandler()) {
      // The implicit fall-off handler is an implicit CoreturnStmt (return_void)
      // for a return_void promise, or a NullStmt in the pathological
      // no-return_void/no-return_value case. Route the CoreturnStmt through the
      // fall-off ordering (destroy body-scope locals before return_void);
      // anything else keeps the ordinary emission.
      if (const auto *coret = dyn_cast<CoreturnStmt>(onFallthrough)) {
        if (cgf.emitCoreturnStmt(*coret, /*isFallthrough=*/true).failed())
          return mlir::failure();
      } else if (cgf.emitStmt(onFallthrough, /*useCurrentScope=*/true).failed())
        return mlir::failure();
    }

  return mlir::success();
}

cir::CallOp CIRGenFunction::emitCoroIDBuiltinCall(mlir::Location loc,
                                                  mlir::Value nullPtr) {
  cir::IntType int32Ty = builder.getUInt32Ty();

  const TargetInfo &ti = cgm.getASTContext().getTargetInfo();
  unsigned newAlign = ti.getNewAlign() / ti.getCharWidth();

  mlir::Operation *builtin = cgm.getGlobalValue(cgm.builtinCoroId);

  cir::FuncOp fnOp;
  if (!builtin) {
    fnOp = cgm.createCIRBuiltinFunction(
        loc, cgm.builtinCoroId,
        cir::FuncType::get({int32Ty, voidPtrTy, voidPtrTy, voidPtrTy}, int32Ty),
        /*FD=*/nullptr);
    assert(fnOp && "should always succeed");
  } else {
    fnOp = cast<cir::FuncOp>(builtin);
  }

  return builder.createCallOp(loc, fnOp,
                              mlir::ValueRange{builder.getUInt32(newAlign, loc),
                                               nullPtr, nullPtr, nullPtr});
}

cir::CallOp CIRGenFunction::emitCoroAllocBuiltinCall(mlir::Location loc) {
  cir::BoolType boolTy = builder.getBoolTy();

  mlir::Operation *builtin = cgm.getGlobalValue(cgm.builtinCoroAlloc);

  cir::FuncOp fnOp;
  if (!builtin) {
    fnOp = cgm.createCIRBuiltinFunction(loc, cgm.builtinCoroAlloc,
                                        cir::FuncType::get({uInt32Ty}, boolTy),
                                        /*fd=*/nullptr);
    assert(fnOp && "should always succeed");
  } else {
    fnOp = cast<cir::FuncOp>(builtin);
  }

  return builder.createCallOp(
      loc, fnOp, mlir::ValueRange{curCoro.data->coroId.getResult()});
}

cir::CallOp
CIRGenFunction::emitCoroBeginBuiltinCall(mlir::Location loc,
                                         mlir::Value coroframeAddr) {
  mlir::Operation *builtin = cgm.getGlobalValue(cgm.builtinCoroBegin);

  cir::FuncOp fnOp;
  if (!builtin) {
    fnOp = cgm.createCIRBuiltinFunction(
        loc, cgm.builtinCoroBegin,
        cir::FuncType::get({uInt32Ty, voidPtrTy}, voidPtrTy),
        /*fd=*/nullptr);
    assert(fnOp && "should always succeed");
  } else {
    fnOp = cast<cir::FuncOp>(builtin);
  }

  return builder.createCallOp(
      loc, fnOp,
      mlir::ValueRange{curCoro.data->coroId.getResult(), coroframeAddr});
}

cir::CallOp CIRGenFunction::emitCoroEndBuiltinCall(mlir::Location loc,
                                                   mlir::Value nullPtr,
                                                   bool isUnwind) {
  cir::BoolType boolTy = builder.getBoolTy();
  mlir::Operation *builtin = cgm.getGlobalValue(cgm.builtinCoroEnd);

  cir::FuncOp fnOp;
  if (!builtin) {
    fnOp = cgm.createCIRBuiltinFunction(
        loc, cgm.builtinCoroEnd,
        cir::FuncType::get({voidPtrTy, boolTy}, boolTy),
        /*fd=*/nullptr);
    assert(fnOp && "should always succeed");
  } else {
    fnOp = cast<cir::FuncOp>(builtin);
  }

  return builder.createCallOp(
      loc, fnOp, mlir::ValueRange{nullPtr, builder.getBool(isUnwind, loc)});
}

cir::CallOp CIRGenFunction::emitCoroIsInRampBuiltinCall(mlir::Location loc) {
  cir::BoolType boolTy = builder.getBoolTy();
  mlir::Operation *builtin = cgm.getGlobalValue(cgm.builtinCoroIsInRamp);

  cir::FuncOp fnOp;
  if (!builtin) {
    fnOp = cgm.createCIRBuiltinFunction(
        loc, cgm.builtinCoroIsInRamp, cir::FuncType::get({}, boolTy),
        /*fd=*/nullptr);
    assert(fnOp && "should always succeed");
  } else {
    fnOp = cast<cir::FuncOp>(builtin);
  }

  return builder.createCallOp(loc, fnOp, mlir::ValueRange{});
}

mlir::Block *CIRGenFunction::getOrCreateCoroutineRetBlock(mlir::Location loc) {
  assert(curCoro.data && "creating coroutine return block outside coroutine");

  if (curCoro.data->coroRetBlock)
    return curCoro.data->coroRetBlock;

  auto fn = mlir::cast<cir::FuncOp>(curFn);
  auto *coroRetBlock = new mlir::Block();
  fn.getBody().push_back(coroRetBlock);
  curCoro.data->coroRetBlock = coroRetBlock;

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToEnd(coroRetBlock);

  cir::ConstantOp nullPtr = builder.getNullPtr(voidPtrTy, loc);
  emitCoroEndBuiltinCall(loc, nullPtr, /*isUnwind=*/false);

  if (!fn.getFunctionType().hasVoidReturn()) {
    auto value = cir::LoadOp::create(builder, loc,
                                     fn.getFunctionType().getReturnType(),
                                     *fnRetAlloca);
    emitReturnValueCheck(loc, value);
    mlir::Value retValue = value.getResult();
    if (autoreleaseResult)
      retValue = emitARCAutoreleaseReturnValue(retValue, loc);
    cir::ReturnOp::create(builder, loc, llvm::ArrayRef(retValue));
  } else {
    cir::ReturnOp::create(builder, loc);
  }

  return coroRetBlock;
}

mlir::Block *
CIRGenFunction::getOrCreateCoroutineCleanupRetBlock(mlir::Location loc) {
  assert(curCoro.data &&
         "creating coroutine cleanup return block outside coroutine");

  if (curCoro.data->cleanupRetBlock)
    return curCoro.data->cleanupRetBlock;

  auto fn = mlir::cast<cir::FuncOp>(curFn);
  auto *cleanupRetBlock = new mlir::Block();
  fn.getBody().push_back(cleanupRetBlock);
  curCoro.data->cleanupRetBlock = cleanupRetBlock;

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToEnd(cleanupRetBlock);
  emitCoroutineFrameDealloc(loc);
  cir::BrOp::create(builder, loc, getOrCreateCoroutineRetBlock(loc));

  return cleanupRetBlock;
}

void CIRGenFunction::emitCoroutineFrameDealloc(mlir::Location loc) {
  if (!curCoro.data || !curCoro.data->deallocate)
    return;

  // Emit the deallocation expression from CoroutineBodyStmt.
  // This expression typically contains:
  //   delete(__builtin_coro_free(coroId, coroBegin))
  // The __builtin_coro_free call is handled by the builtin emitter
  // (CIRGenBuiltin.cpp) and returns the frame pointer if deallocation
  // is needed, or null if the frame was elided by the optimizer.
  // emitCoroutineDeallocationStmt() hoists that coro.free result and
  // guards the delete call to match OGCG's CallCoroDelete path.
  emitCoroutineDeallocationStmt(curCoro.data->deallocate);
}

static llvm::SmallString<32> buildSuspendPrefixStr(CGCoroData &coro,
                                                    cir::AwaitKind kind) {
  unsigned no = 0;
  switch (kind) {
  case cir::AwaitKind::Init:
  case cir::AwaitKind::Final:
    break;
  case cir::AwaitKind::User:
    no = ++coro.awaitNum;
    break;
  case cir::AwaitKind::Yield:
    no = ++coro.yieldNum;
    break;
  }
  llvm::SmallString<32> prefix;
  switch (kind) {
  case cir::AwaitKind::Init:
    prefix = "init";
    break;
  case cir::AwaitKind::User:
    prefix = "await";
    break;
  case cir::AwaitKind::Yield:
    prefix = "yield";
    break;
  case cir::AwaitKind::Final:
    prefix = "final";
    break;
  }
  if (no > 1)
    llvm::Twine(no).toVector(prefix);
  return prefix;
}

cir::FuncOp CIRGenFunction::generateAwaitSuspendWrapper(
    mlir::Location loc, llvm::Twine const &coroName,
    llvm::Twine const &suspendPointName, const CoroutineSuspendExpr &s) {
  std::string funcName =
      (coroName + ".__await_suspend_wrapper__" + suspendPointName).str();

  QualType returnTy = s.getSuspendExpr()->getType();

  // Build the CIR function type: (ptr<void>, ptr<void>) -> retTy
  auto voidTy = cir::VoidType::get(builder.getContext());
  auto ptrToVoid = cir::PointerType::get(voidTy);
  mlir::Type cirRetTy;
  bool isVoidReturn = returnTy->isVoidType();
  if (isVoidReturn) {
    cirRetTy = voidTy;
  } else {
    cirRetTy = convertType(returnTy);
  }
  auto funcType = cir::FuncType::get({ptrToVoid, ptrToVoid}, cirRetTy);

  // Create the wrapper function at module scope.
  cir::FuncOp wrapperFn =
      cgm.createCIRFunction(loc, funcName, funcType, /*fd=*/nullptr);
  wrapperFn.setLinkageAttr(cir::GlobalLinkageKindAttr::get(
      &cgm.getMLIRContext(), cir::GlobalLinkageKind::InternalLinkage));

  // Add parameter attributes matching OGCG:
  // param 0 (awaiter): nonnull, noundef
  // param 1 (frame): noundef
  wrapperFn.setArgAttr(0, "cir.nonnull",
                        mlir::UnitAttr::get(&cgm.getMLIRContext()));

  // Set function attributes matching OGCG (CGCoroutine.cpp:448-450).
  // These wrappers are meant to be always inlined.
  wrapperFn.setInlineKind(cir::InlineKind::AlwaysInline);

  // Set mustprogress and sample-profile-suffix-elision-policy extra attrs.
  {
    mlir::NamedAttrList extraAttrs;
    if (auto existingExtraAttrs = wrapperFn.getExtraAttrs())
      extraAttrs =
          mlir::NamedAttrList{existingExtraAttrs->getElements().getValue()};
    extraAttrs.set("mustprogress",
                   mlir::UnitAttr::get(&cgm.getMLIRContext()));
    if (cgm.getTarget().getTriple().isX86())
      extraAttrs.set("min-legal-vector-width",
                     cgm.getStringAttrCached("0"));
    extraAttrs.set("sample-profile-suffix-elision-policy",
                   mlir::StringAttr::get(&cgm.getMLIRContext(), "selected"));
    wrapperFn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        extraAttrs.getDictionary(&cgm.getMLIRContext())));
  }

  // Build the wrapper body. We temporarily redirect the builder's insertion
  // point into the wrapper function, swap curFn, and set the FramePtr override
  // so that __builtin_coro_frame() returns the wrapper's frame parameter.
  {
    mlir::OpBuilder::InsertionGuard insertGuard(builder);

    // Save and swap CIRGenFunction state that must point to the wrapper.
    auto savedCurFn = curFn;
    curFn = wrapperFn;

    // Save and reset the EH stack state. The wrapper is a separate function
    // that must not inherit exception handling from the parent coroutine.
    // OGCG handles this by creating a fresh CodeGenFunction for the wrapper
    // (CGCoroutine.cpp:251); we save/restore the EH state instead.
    // Without this, calls inside the wrapper that can throw would be emitted
    // as cir.try_call with cleanup handlers from the coroutine's EH stack,
    // causing cross-function value references that violate IsolatedFromAbove.
    auto savedInnermostEHScope = ehStack.innermostEHScope;
    auto savedInnermostNormalCleanup = ehStack.innermostNormalCleanup;
    ehStack.innermostEHScope = EHScopeStack::stable_end();
    ehStack.innermostNormalCleanup = EHScopeStack::stable_end();

    // Save and reset the conditional evaluation context. If the coroutine's
    // co_await appears inside a conditional expression (e.g. a ternary),
    // outermostConditional points into the coroutine body. The wrapper is a
    // separate function, so any cleanup flags it creates must not reference
    // the parent's conditional insertion point — that would produce a
    // cross-function SSA use ('cir.store' op using value defined outside the
    // region).
    auto *savedOutermostConditional = outermostConditional;
    outermostConditional = nullptr;

    // Create the wrapper's entry block with two ptr<void> arguments.
    mlir::Block *entryBB = wrapperFn.addEntryBlock();
    builder.setInsertionPointToStart(entryBB);

    mlir::Value awaiterArg = entryBB->getArgument(0);
    mlir::Value frameArg = entryBB->getArgument(1);

    // Set the frame pointer override so __builtin_coro_frame() returns the
    // wrapper's frame parameter instead of coro.begin.
    curAwaitSuspendWrapper.framePtr = frameArg;

    // Create a temporary LexicalScope so that any allocas emitted by
    // emitScalarExpr go into the wrapper function's entry block.
    LexicalScope wrapperScope(*this, loc, entryBB);

    // Create an LValue for the awaiter and bind the OpaqueValue.
    // The awaiter arg is a void* pointer to the awaiter object. We need to
    // cast it to the awaiter's actual type to create the LValue that the
    // suspend expression expects.
    QualType awaiterTy = s.getOpaqueValue()->getType();
    auto awaiterPtrTy = builder.getPointerTo(convertType(awaiterTy));
    mlir::Value awaiterCasted =
        builder.createBitcast(loc, awaiterArg, awaiterPtrTy);
    auto awaiterLVal = makeNaturalAlignAddrLValue(awaiterCasted, awaiterTy);

    // Save the existing OVE mapping (from the outer emitSuspendExpression)
    // and replace it with the wrapper's awaiter LValue. We must use
    // operator[] rather than OpaqueValueMappingData::bind because the outer
    // scope already has a mapping and std::map::insert won't overwrite it.
    const OpaqueValueExpr *ove = s.getOpaqueValue();
    auto savedIt = opaqueLValues.find(ove);
    LValue savedOVELVal;
    bool hadSavedOVELVal = (savedIt != opaqueLValues.end());
    if (hadSavedOVELVal) {
      savedOVELVal = savedIt->second;
      savedIt->second = awaiterLVal;
    } else {
      opaqueLValues.insert({ove, awaiterLVal});
    }

    // Emit the suspend expression (await_suspend call) inside the wrapper.
    mlir::Value suspendRet = emitScalarExpr(s.getSuspendExpr());

    // Restore the outer scope's OVE mapping.
    if (hadSavedOVELVal)
      opaqueLValues[ove] = savedOVELVal;
    else
      opaqueLValues.erase(ove);
    curAwaitSuspendWrapper.framePtr = nullptr;

    // Build the return.
    if (suspendRet) {
      cir::ReturnOp::create(builder, loc,
                             llvm::ArrayRef<mlir::Value>{suspendRet});
    } else {
      cir::ReturnOp::create(builder, loc);
    }

    // Manually pop the scope to avoid cleanup running destructors.
    wrapperScope.restore();

    // Restore EH stack, conditional evaluation context, and curFn.
    ehStack.innermostEHScope = savedInnermostEHScope;
    ehStack.innermostNormalCleanup = savedInnermostNormalCleanup;
    outermostConditional = savedOutermostConditional;
    curFn = savedCurFn;
  }

  return wrapperFn;
}

mlir::LogicalResult
CIRGenFunction::emitCoroutineBody(const CoroutineBodyStmt &s) {
  mlir::Location openCurlyLoc = getLoc(s.getBeginLoc());
  cir::ConstantOp nullPtrCst = builder.getNullPtr(voidPtrTy, openCurlyLoc);

  auto fn = mlir::cast<cir::FuncOp>(curFn);
  fn.setCoroutine(true);
  cir::CallOp coroId = emitCoroIDBuiltinCall(openCurlyLoc, nullPtrCst);
  createCoroData(*this, curCoro, coroId);

  // Backend is allowed to elide memory allocations, to help it, emit
  // auto mem = coro.alloc() ? 0 : ... allocation code ...;
  cir::CallOp coroAlloc = emitCoroAllocBuiltinCall(openCurlyLoc);

  // Initialize address of coroutine frame to null
  CanQualType astVoidPtrTy = cgm.getASTContext().VoidPtrTy;
  mlir::Type allocaTy = convertTypeForMem(astVoidPtrTy);
  Address coroFrame =
      createTempAlloca(allocaTy, getContext().getTypeAlignInChars(astVoidPtrTy),
                       openCurlyLoc, "__coro_frame_addr",
                       /*ArraySize=*/nullptr);

  mlir::Value storeAddr = coroFrame.getPointer();
  builder.CIRBaseBuilderTy::createStore(openCurlyLoc, nullPtrCst, storeAddr);
  auto *retOnAllocFailure = s.getReturnStmtOnAllocFailure();
  cir::IfOp::create(
      builder, openCurlyLoc, coroAlloc.getResult(),
      /*withElseRegion=*/false,
      /*thenBuilder=*/[&](mlir::OpBuilder &b, mlir::Location loc) {
        mlir::Value allocResult = emitScalarExpr(s.getAllocate());

        // Handle allocation failure if 'ReturnStmtOnAllocFailure' was provided.
        // If the promise type defines get_return_object_on_allocation_failure(),
        // we check if the allocation returned null and return early if so.
        if (retOnAllocFailure) {
          mlir::Value isNull =
              builder.createPtrIsNull(allocResult);
          cir::IfOp::create(
              builder, loc, isNull,
              /*withElseRegion=*/false,
              /*thenBuilder=*/[&](mlir::OpBuilder &b, mlir::Location loc) {
                // On allocation failure, return directly without coroutine
                // cleanup (the coroutine was never started — no coro.begin,
                // so no coro.end/coro.free/delete needed). We emit the return
                // value and cir.return directly instead of using emitStmt/
                // emitReturnStmt, which would thread through the coroutine
                // cleanup return block referencing values not yet available.
                const Expr *retExpr =
                    cast<ReturnStmt>(retOnAllocFailure)->getRetValue();
                if (retExpr) {
                  switch (getEvaluationKind(retExpr->getType())) {
                  case cir::TEK_Scalar: {
                    mlir::Value value = emitScalarExpr(retExpr);
                    if (value)
                      builder.CIRBaseBuilderTy::createStore(loc, value,
                                                            *fnRetAlloca);
                    break;
                  }
                  case cir::TEK_Aggregate:
                    emitAggExpr(retExpr,
                                AggValueSlot::forAddr(
                                    returnValue, Qualifiers(),
                                    AggValueSlot::IsDestructed,
                                    AggValueSlot::IsNotAliased,
                                    getOverlapForReturnValue()));
                    break;
                  case cir::TEK_Complex:
                    emitComplexExprIntoLValue(
                        retExpr,
                        makeAddrLValue(returnValue, retExpr->getType()),
                        /*isInit=*/true);
                    break;
                  }
                }
                auto fn = cast<cir::FuncOp>(curFn);
                if (!fn.getFunctionType().hasVoidReturn()) {
                  auto value = cir::LoadOp::create(
                      builder, loc, fn.getFunctionType().getReturnType(),
                      *fnRetAlloca);
                  cir::ReturnOp::create(builder, loc,
                                        llvm::ArrayRef(value.getResult()));
                } else {
                  cir::ReturnOp::create(builder, loc);
                }
                // cir.return terminates this block. Create a new unreachable
                // block with cir.yield to satisfy IfOp region requirements.
                builder.createBlock(builder.getBlock()->getParent());
                cir::YieldOp::create(builder, loc);
              });
        }

        builder.CIRBaseBuilderTy::createStore(loc, allocResult, storeAddr);
        cir::YieldOp::create(builder, loc);
      });
  curCoro.data->coroBegin =
      emitCoroBeginBuiltinCall(
          openCurlyLoc,
          cir::LoadOp::create(builder, openCurlyLoc, allocaTy, storeAddr))
          .getResult();

  // Store the deallocate expression for later emission during return.
  curCoro.data->deallocate = s.getDeallocate();

  {
    // TODO(cir): Add coroutine-specific debug metadata for parameter-copy
    // remapping once CIR debug info support is available.
    ParamReferenceReplacerRAII paramReplacer(localDeclMap);
    // Keep the coroutine frame deallocation cleanup active until GRO setup
    // finishes so throwing setup calls (parameter copies, promise ctor,
    // get_return_object) lower as exception-capable calls with frame cleanup.
    ehStack.pushCleanup<CallCoroDelete>(EHCleanup, s.getDeallocate());
    // Create mapping between parameters and copy-params for coroutine
    // function.
    llvm::ArrayRef<const Stmt *> paramMoves = s.getParamMoves();
    assert((paramMoves.size() == 0 || (paramMoves.size() == fnArgs.size())) &&
           "ParamMoves and FnArgs should be the same size for coroutine "
           "function");
    // TODO(cir): Zip coroutine arg mapping into debug info once CIR debug
    // info support is available.

    // Create parameter copies. We do it before creating a promise, since an
    // evolution of coroutine TS may allow promise constructor to observe
    // parameter copies.
    // OGCG marks original parameter allocas with coro_outside_frame metadata
    // so they stay on the stack and are not included in the coroutine frame
    // (CGCoroutine.cpp:951-963), and also marks param-move copy allocas
    // (CGCoroutine.cpp:732-733).
    for (auto *pm : paramMoves) {
      if (emitStmt(pm, /*useCurrentScope=*/true).failed())
        return mlir::failure();
      paramReplacer.addCopy(cast<DeclStmt>(pm));
    }

    if (emitStmt(s.getPromiseDeclStmt(), /*useCurrentScope=*/true).failed())
      return mlir::failure();

    // Update CoroId to refer to the promise. We could not do it earlier
    // because the promise local variable was not emitted yet. The promise
    // alloca was placed before the coro.id call by getBestAllocaInsertPoint,
    // so SSA dominance is satisfied.
    {
      Address promiseAddr = getAddrOfLocalVar(s.getPromiseDecl());
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPoint(curCoro.data->coroId);
      auto promisePtrTy =
          mlir::cast<cir::PointerType>(promiseAddr.getPointer().getType());
      auto promiseVoidPtrTy = cir::PointerType::get(
          builder.getContext(), cir::VoidType::get(builder.getContext()),
          promisePtrTy.getAddrSpace());
      mlir::Value promiseCast =
          builder.createBitcast(promiseAddr.getPointer(), promiseVoidPtrTy);
      curCoro.data->coroId.setOperand(1, promiseCast);
    }

    // returnValue should be valid as long as the coroutine's return type
    // is not void. The assertion could help us to reduce the check later.
    assert(returnValue.isValid() == (bool)s.getReturnStmt());
    // Match OGCG's direct-vs-delayed GRO split. If get_return_object() has the
    // same type as the coroutine return type, we can initialize the result
    // slot eagerly. Otherwise we must materialize the synthetic result-decl
    // temporary, let the coroutine body populate it, and convert it back to
    // the real return type on ramp-only exit paths.
    bool directEmit = true;
    if (returnValue.isValid()) {
      auto *rvi = s.getReturnValueInit();
      assert(rvi && "expected ReturnValueInit");
      QualType groType = rvi->getType();
      // Match OGCG, which compares against CGF.FnRetTy (the arranged return
      // type of the function being emitted) rather than
      // CurFuncDecl->getReturnType(). For a coroutine-lambda's operator(),
      // curFuncDecl is set to getNonClosureContext() (the enclosing function),
      // whose return type is unrelated to the coroutine's. Using its return
      // type here spuriously forces the delayed-GRO path, which spills the
      // sret return slot into the coroutine frame and corrupts the returned
      // object once the frame outlives the ramp.
      directEmit = getContext().hasSameType(groType, curFnRetTy);
    }

    if (directEmit) {
      // We need to emit `get_return_object` before initial_suspend.
      if (returnValue.isValid())
        emitAnyExprToMem(s.getReturnValue(), returnValue,
                         s.getReturnValue()->getType().getQualifiers(),
                         /*isInit*/ true);
    } else if (auto *resultDecl = s.getResultDecl()) {
      if (emitStmt(resultDecl, /*useCurrentScope=*/true).failed())
        return mlir::failure();

      // Keep the GRO temporary on the ramp stack so CoroSplit does not fold it
      // into the coroutine frame. pr59221.cpp relies on this for frame-size
      // parity, and the delayed conversion may still read it after frame
      // teardown paths branch out of the coroutine body.
      if (auto *groVarDecl =
              dyn_cast<VarDecl>(cast<DeclStmt>(resultDecl)->getSingleDecl())) {
        Address groAddr = getAddrOfLocalVar(groVarDecl);
        if (auto allocaOp =
                groAddr.getPointer().getDefiningOp<cir::AllocaOp>()) {
          allocaOp->setAttr("cir.coro_outside_frame", builder.getUnitAttr());
        }
      }
    }

    ehStack.pushCleanup<CallCoroEnd>(EHCleanup);

    curCoro.data->currentAwaitKind = cir::AwaitKind::Init;
    curCoro.data->exceptionHandler = s.getExceptionHandler();
    if (emitStmt(s.getInitSuspendStmt(), /*useCurrentScope=*/true).failed())
      return mlir::failure();

    curCoro.data->finalSuspendCleanupDepth = ehStack.getInnermostNormalCleanup();

    curCoro.data->currentAwaitKind = cir::AwaitKind::User;
    auto *coroBodyScope = curLexScope;

    auto emitCoroutineBodyWithExceptionHandler =
        [&]() -> mlir::LogicalResult {
      auto loc = s.getBeginLoc();
      CXXCatchStmt catchStmt(loc, /*exDecl=*/nullptr,
                             s.getExceptionHandler());
      auto *tryStmt =
          CXXTryStmt::Create(getContext(), loc, s.getBody(), &catchStmt);
      return emitCXXTryStmt(*tryStmt, [&]() -> mlir::LogicalResult {
        return emitBodyAndFallthrough(*this, s, s.getBody(), coroBodyScope);
      });
    };

    if (s.getExceptionHandler()) {
      if (curCoro.data->resumeEHVar) {
        mlir::Location ifLoc = getLoc(s.getBeginLoc());
        auto resumeThrew = cir::LoadOp::create(builder, ifLoc,
                                               builder.getBoolTy(),
                                               curCoro.data->resumeEHVar);
        mlir::LogicalResult bodyResult = mlir::success();
        cir::IfOp::create(
            builder, ifLoc, resumeThrew.getResult(), /*withElseRegion=*/true,
            [&](mlir::OpBuilder &, mlir::Location skipLoc) {
              LexicalScope skipScope{*this, skipLoc,
                                     builder.getInsertionBlock()};
              skipScope.setCleanupOnly();
              cir::YieldOp::create(builder, skipLoc);
            },
            [&](mlir::OpBuilder &, mlir::Location bodyLoc) {
              LexicalScope bodyScope{*this, bodyLoc,
                                     builder.getInsertionBlock()};
              bodyResult = emitCoroutineBodyWithExceptionHandler();
            });
        if (bodyResult.failed())
          return mlir::failure();
      } else if (emitCoroutineBodyWithExceptionHandler().failed()) {
        return mlir::failure();
      }
    } else {
      if (emitBodyAndFallthrough(*this, s, s.getBody(), coroBodyScope)
              .failed())
        return mlir::failure();
    }

    // Note that LLVM checks CanFallthrough by looking into the availability
    // of the insert block which is kinda brittle and unintuitive, seems to be
    // related with how landing pads are handled.
    //
    // OGCG: const bool CanFallthrough = Builder.GetInsertBlock();
    // In CIR, the builder always has an insertion block (structured regions).
    // canFallthrough is true when the body doesn't unconditionally terminate.
    const bool canFallthrough = builder.getInsertionBlock() != nullptr;
    const bool hasCoreturns = curCoro.data->coreturnCount > 0;
    if (canFallthrough || hasCoreturns) {
      curCoro.data->currentAwaitKind = cir::AwaitKind::Final;
      mlir::Location loc = getLoc(s.getSourceRange());
      mlir::Block *retBlock = curLexScope->getOrCreateRetBlock(*this, loc);
      auto brOp = cir::BrOp::create(builder, loc, retBlock);
      builder.createBlock(builder.getBlock()->getParent());
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPoint(brOp);
      if (hasCoreturns) {
        auto fn = cast<cir::FuncOp>(curFn);
        cir::LabelOp::create(builder, loc,
                             getOrCreateFinalSuspendLabel(*curCoro.data, fn));
      }
      if (emitStmt(s.getFinalSuspendStmt(), /*useCurrentScope=*/true).failed())
        return mlir::failure();
      auto fn = cast<cir::FuncOp>(curFn);
      cir::LabelOp::create(builder, loc,
                           getOrCreateDestroyCleanupLabel(*curCoro.data, fn));

      // Delayed get-return-object conversion. This must be emitted HERE, in
      // the __coro_destroy block BEFORE the coroutine-body scope's GRO/promise
      // cleanups pop, so that on the normal exit path the conversion runs
      // before the GRO temporary + promise are destroyed, and on the throwing
      // path (a throwing GRO->return converting ctor) the still-active EH
      // cleanups run exactly once. All coroutine exits (fallthrough and every
      // co_return, which cir.goto's to the __coro_final label) converge on
      // this block, so a single emission covers them. Mirrors OGCG's
      // GetReturnObjectManager::EmitGroConv, which is emitted before the
      // RunCleanupsScope cleanups pop (CGCoroutine.cpp:1041-1042); emitting it
      // after the cleanups (the old per-retBlock loop) double-destroyed the
      // GRO temporary and promise when the converting ctor threw.
      if (!directEmit && returnValue.isValid()) {
        cir::CallOp isInRamp = emitCoroIsInRampBuiltinCall(openCurlyLoc);
        auto convIf = cir::IfOp::create(
            builder, openCurlyLoc, isInRamp.getResult(),
            /*withElseRegion=*/false,
            [&](mlir::OpBuilder &, mlir::Location groLoc) {
              emitAnyExprToMem(s.getReturnValue(), returnValue,
                               s.getReturnValue()->getType().getQualifiers(),
                               /*isInit*/ true);
              cir::YieldOp::create(builder, groLoc);
            });
        // Mark this is_in_ramp-guarded delayed-GRO conversion so FlattenCFG can
        // locate it and additionally run it on every suspend-return-to-caller
        // edge. As emitted here it sits in the __coro_destroy block on the
        // FALLTHROUGH-completion path only; a coroutine that suspends at
        // initial/final suspend (suspend_always) returns to the caller through
        // the coro.suspend switch default and never reaches this block, so the
        // conversion (and any observable side effect / throw of the
        // GRO->return converting ctor) would otherwise be skipped. Mirrors
        // classic CodeGen, whose EmitGroConv places this at Coro.SuspendBB --
        // the shared suspend-switch default -- so it runs for all suspend
        // shapes (CGCoroutine.cpp:329,815-856).
        convIf->setAttr("cir.coro_gro_conv", builder.getUnitAttr());
      }
    }

  }

  // OGCG checks CoroOnlyDestroyWhenCompleteAttr on the return type's record
  // and calls setCoroDestroyOnlyWhenComplete() to optimize destroy codegen
  // (CGCoroutine.cpp:1066-1068).
  if (const auto *fd = dyn_cast<FunctionDecl>(curGD.getDecl())) {
    QualType retTy = fd->getReturnType();
    if (CXXRecordDecl *rd = retTy->getAsCXXRecordDecl();
        rd && rd->hasAttr<CoroOnlyDestroyWhenCompleteAttr>()) {
      auto attrs = fn.getExtraAttrs();
      mlir::NamedAttrList attrList;
      if (attrs)
        for (auto entry : attrs->getElements())
          attrList.append(entry.getName(), entry.getValue());
      attrList.set("coro_only_destroy_when_complete",
                   mlir::UnitAttr::get(fn.getContext()));
      fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
          attrList.getDictionary(fn.getContext())));
    }
  }

  return mlir::success();
}

static bool functionCanThrow(const FunctionDecl *d) {
  const auto *proto = d->getType()->getAs<FunctionProtoType>();
  if (!proto)
    return true;
  return !isNoexceptExceptionSpec(proto->getExceptionSpecType()) ||
         proto->canThrow() != CT_Cannot;
}

static bool statementCanThrow(const Stmt *s) {
  if (!s)
    return false;

  if (const auto *call = dyn_cast<CallExpr>(s)) {
    const auto *callee = call->getDirectCallee();
    if (!callee || functionCanThrow(callee))
      return true;
  }

  if (const auto *bind = dyn_cast<CXXBindTemporaryExpr>(s)) {
    const auto *dtor = bind->getTemporary()->getDestructor();
    if (functionCanThrow(dtor))
      return true;
  }

  for (const Stmt *child : s->children())
    if (statementCanThrow(child))
      return true;

  return false;
}

// Given a suspend expression which roughly looks like:
//
//   auto && x = CommonExpr();
//   if (!x.await_ready()) {
//      x.await_suspend(...); (*)
//   }
//   x.await_resume();
//
// where the result of the entire expression is the result of x.await_resume()
//
//   (*) If x.await_suspend return type is bool, it allows to veto a suspend:
//      if (x.await_suspend(...))
//        llvm_coro_suspend();
//
// This is more higher level than LLVM codegen, for that one see llvm's
// docs/Coroutines.rst for more details.
namespace {
struct LValueOrRValue {
  LValue lv;
  RValue rv;
};
} // namespace

static LValueOrRValue
emitSuspendExpression(CIRGenFunction &cgf, CGCoroData &coro,
                      CoroutineSuspendExpr const &s, cir::AwaitKind kind,
                      AggValueSlot aggSlot, bool ignoreResult,
                      mlir::Block *scopeParentBlock,
                      mlir::Value &tmpResumeRValAddr, bool forLValue) {
  [[maybe_unused]] mlir::LogicalResult awaitBuild = mlir::success();
  LValueOrRValue awaitRes;

  CIRGenFunction::OpaqueValueMapping binder =
      CIRGenFunction::OpaqueValueMapping(cgf, s.getOpaqueValue());
  CIRGenBuilderTy &builder = cgf.getBuilder();

  // For non-final suspends that have live BODY-LOCAL (normal) cleanups above
  // finalSuspendCleanupDepth, emit a cleanup region holding those locals'
  // destructors. FlattenCFG routes the coroutine destroy edge (handle.destroy()
  // while suspended at this point) through that region so the live body locals
  // are destroyed before the frame is freed -- this is exactly the body-local
  // set that co_return already destroys via the same depth. We stop AT
  // finalSuspendCleanupDepth (above the promise/GRO/frame cleanups) on purpose:
  // the coroutine's own destroy machinery already destroys the promise/GRO and
  // frees the frame, so re-running them here would double-destroy a
  // non-trivially-destructible promise. When there are no live body locals (or
  // this is the final suspend) the region is omitted, preserving prior IR.
  // Coroutines whose return type is marked [[clang::coro_only_destroy_when_
  // complete]] (e.g. folly::coro::Task) are never destroyed while suspended at
  // an intermediate suspend point -- they are only ever destroyed after reaching
  // the final suspend. Emitting a destroy-edge cleanup for their intermediate
  // suspends is therefore unnecessary, and worse: it makes LLVM CoroSplit (which
  // generates a SIMPLIFIED .destroy clone for these types) mis-generate the
  // .resume clone, breaking normal completion (the symmetric-transfer chain never
  // resumes -> blockingWait baton never posts -> hang). Classic CodeGen emits no
  // such cleanups for these coroutines; match that.
  bool destroyOnlyWhenComplete = false;
  if (const auto *fd = dyn_cast_or_null<FunctionDecl>(cgf.curGD.getDecl())) {
    if (CXXRecordDecl *rd = fd->getReturnType()->getAsCXXRecordDecl();
        rd && rd->hasAttr<CoroOnlyDestroyWhenCompleteAttr>())
      destroyOnlyWhenComplete = true;
  }
  // Emit the destroy-edge cleanup for every non-final suspend with live
  // body-local cleanups, regardless of the suspend return type. For
  // symmetric-transfer (SuspendHandle, e.g. folly::coro::AsyncGenerator's
  // co_yield) and conditional (SuspendBool) suspends the suspend region lowers
  // to an invoke with an EH unwind edge plus a tail-call symmetric transfer.
  // FlattenCFG's CIRAwaitOpFlattening routes BOTH the i8==1 destroy edge and
  // the resume continuation of such suspends through a shared dest-slot
  // SELECTOR block (matching classic CodeGen's cleanup.dest.slot model), so the
  // body-local destructor lives in a block dominated by a block that is also on
  // the resume path. That keeps the structure uniform and collapsible per
  // CoroSplit clone (the selector const-folds to 0 in .resume, 1 in .destroy),
  // which avoids the destroy-only mergeable side-path that previously made LLVM
  // simplifycfg + CoroSplit mis-lower the awaiting coroutine under co_nothrow
  // (resume chain never completes -> blockingWait baton never posts -> hang).
  bool needsDestroyCleanup =
      kind != cir::AwaitKind::Final && !destroyOnlyWhenComplete &&
      coro.finalSuspendCleanupDepth.has_value() &&
      cgf.ehStack.getInnermostActiveNormalCleanup() !=
          *coro.finalSuspendCleanupDepth;
  auto cleanupRegionBuilder = [&](mlir::OpBuilder &, mlir::Location cleanupLoc) {
    // Emit the destructors of the active body-local normal cleanups live at this
    // suspend point, walking down to finalSuspendCleanupDepth (exclusive of the
    // promise/GRO/frame, which the coroutine's own destroy machinery handles).
    // Same body-local set co_return destroys via emitCleanupsForEarlyExit.
    cgf.emitCleanupsForEarlyExit(*coro.finalSuspendCleanupDepth);
    cir::YieldOp::create(builder, cleanupLoc);
  };

  [[maybe_unused]] cir::AwaitOp awaitOp = cir::AwaitOp::create(
      builder, cgf.getLoc(s.getSourceRange()), kind,
      /*readyBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location loc) {
        Expr *condExpr = s.getReadyExpr()->IgnoreParens();
        builder.createCondition(cgf.evaluateExprAsBool(condExpr));
      },
      /*suspendBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location loc) {
        // Generate an await_suspend wrapper function and emit the
        // __builtin_coro_await_suspend_* intrinsic call. This matches OGCG's
        // approach of isolating the await_suspend call in a wrapper to enable
        // proper coroutine splitting and optimization.
        auto voidTy = cir::VoidType::get(builder.getContext());
        auto ptrToVoid = cir::PointerType::get(voidTy);

        // Get the awaiter pointer BEFORE generating the wrapper, since the
        // wrapper temporarily swaps the OVE mapping in the shared opaqueLValues
        // map. Cast to void* for the intrinsic signature.
        mlir::Value awaiterPtr =
            builder.createBitcast(loc,
                cgf.getOrCreateOpaqueLValueMapping(s.getOpaqueValue())
                    .getPointer(),
                ptrToVoid);

        // Get the coroutine frame pointer (coro.begin result).
        assert(coro.coroBegin && "expected coro.begin result");
        mlir::Value framePtr = coro.coroBegin;

        auto prefix = buildSuspendPrefixStr(coro, kind);
        auto curFnOp = mlir::cast<cir::FuncOp>(cgf.curFn);
        auto wrapperFn = cgf.generateAwaitSuspendWrapper(
            loc, curFnOp.getName(), prefix, s);

        // Get a reference to the wrapper function for the intrinsic call.
        // GetGlobalOp requires the result type's pointee to match the global's
        // type, so we use a function pointer type and then cast to void*.
        auto wrapperPtrTy =
            cir::PointerType::get(wrapperFn.getFunctionType());
        mlir::Value wrapperFnPtr = cir::GetGlobalOp::create(
            builder, loc, wrapperPtrTy, wrapperFn.getName());
        mlir::Value wrapperRef =
            builder.createBitcast(loc, wrapperFnPtr, ptrToVoid);

        // Select the appropriate builtin based on the return type.
        const char *builtinName = nullptr;
        const auto suspendReturnType = s.getSuspendReturnType();
        switch (suspendReturnType) {
        case CoroutineSuspendExpr::SuspendReturnType::SuspendVoid:
          builtinName = cgf.cgm.builtinCoroAwaitSuspendVoid;
          break;
        case CoroutineSuspendExpr::SuspendReturnType::SuspendBool:
          builtinName = cgf.cgm.builtinCoroAwaitSuspendBool;
          break;
        case CoroutineSuspendExpr::SuspendReturnType::SuspendHandle:
          builtinName = cgf.cgm.builtinCoroAwaitSuspendHandle;
          break;
        }

        // Determine the CIR return type for the builtin.
        mlir::Type builtinRetTy;
        switch (suspendReturnType) {
        case CoroutineSuspendExpr::SuspendReturnType::SuspendVoid:
        case CoroutineSuspendExpr::SuspendReturnType::SuspendHandle:
          builtinRetTy = voidTy;
          break;
        case CoroutineSuspendExpr::SuspendReturnType::SuspendBool:
          builtinRetTy = builder.getBoolTy();
          break;
        }

        // Create or get the builtin function declaration.
        auto builtinFuncType =
            cir::FuncType::get({ptrToVoid, ptrToVoid, ptrToVoid}, builtinRetTy);
        cir::FuncOp builtinFn;
        auto *existing = cgf.cgm.getGlobalValue(builtinName);
        if (!existing) {
          builtinFn = cgf.cgm.createCIRBuiltinFunction(loc, builtinName,
                                                        builtinFuncType,
                                                        /*fd=*/nullptr);
        } else {
          builtinFn = cast<cir::FuncOp>(existing);
        }

        const bool awaitSuspendCanThrow =
            suspendReturnType ==
                CoroutineSuspendExpr::SuspendReturnType::SuspendHandle ||
            statementCanThrow(s.getSuspendExpr());

        SmallVector<mlir::Value, 3> suspendArgs{awaiterPtr, framePtr,
                                                wrapperRef};
        mlir::Value suspendRet;
        cir::CIRCallOpInterface suspendCall;
        if (awaitSuspendCanThrow) {
          suspendCall = cgf.emitRuntimeCallOrInvoke(loc, builtinFn, suspendArgs);
          if (suspendReturnType ==
              CoroutineSuspendExpr::SuspendReturnType::SuspendBool) {
            suspendRet = suspendCall->getResult(0);

            if (builder.getBlock()->getParent() !=
                suspendCall->getParentRegion()) {
              Address suspendRetAddr =
                  cgf.createMemTemp(cgf.getContext().BoolTy, loc,
                                    "tmp.await.suspend.bool");
              auto ip = builder.saveInsertionPoint();
              builder.setInsertionPointAfterValue(suspendRet);
              builder.createStore(loc, suspendRet, suspendRetAddr);
              builder.restoreInsertionPoint(ip);
              suspendRet = builder.createLoad(loc, suspendRetAddr);
            }
          }
        } else {
          suspendRet = cgf.emitNounwindRuntimeCall(loc, builtinFn, suspendArgs);
        }

        // Handle the return value based on suspend type.
        switch (suspendReturnType) {
        case CoroutineSuspendExpr::SuspendReturnType::SuspendVoid:
          // No return value, fall through to YieldOp.
          break;
        case CoroutineSuspendExpr::SuspendReturnType::SuspendBool:
          // Veto suspension if requested by bool returning await_suspend.
          builder.createCondition(suspendRet);
          return;
        case CoroutineSuspendExpr::SuspendReturnType::SuspendHandle:
          // Symmetric transfer: the intrinsic handles resuming the returned
          // coroutine handle internally (via CoroSplit). No explicit resume
          // call needed here.
          break;
        }

        // Signals the parent that execution flows to next region.
        cir::YieldOp::create(builder, loc);
      },
      /*resumeBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location loc) {
        // Exception handling requires additional IR. If the 'await_resume'
        // function is marked as 'noexcept', we avoid generating this additional
        // IR.
        if (coro.exceptionHandler && kind == cir::AwaitKind::Init &&
            statementCanThrow(s.getResumeExpr())) {
          if (!coro.resumeEHVar) {
            // The flag is loaded after the init-suspend ExprWithCleanups scope
            // has ended, so it must live in function-entry storage.
            coro.resumeEHVar = cgf.emitAlloca(
                "initresume.eh", builder.getBoolTy(), loc, CharUnits::One(),
                /*insertIntoFnEntryBlock=*/true);
          }

          builder.CIRBaseBuilderTy::createStore(
              loc, builder.getBool(true, loc), coro.resumeEHVar);

          auto astLoc = s.getResumeExpr()->getExprLoc();
          auto *catchStmt = new (cgf.getContext())
              CXXCatchStmt(astLoc, /*exDecl=*/nullptr, coro.exceptionHandler);
          auto *tryStmt =
              CXXTryStmt::Create(cgf.getContext(), astLoc,
                                 CompoundStmt::Create(cgf.getContext(),
                                                      s.getResumeExpr(),
                                                      FPOptionsOverride(),
                                                      astLoc, astLoc),
                                 catchStmt);

          mlir::OpBuilder::InsertPoint beginInsertTryBody;
          auto tryOp = cir::TryOp::create(
              builder, loc,
              /*tryBuilder=*/
              [&](mlir::OpBuilder &, mlir::Location) {
                beginInsertTryBody = builder.saveInsertionPoint();
              },
              /*handlersBuilder=*/
              [&](mlir::OpBuilder &, mlir::Location,
                  mlir::OperationState &result) {
                auto *region = result.addRegion();
                builder.createBlock(region);
              });
          mlir::OpBuilder::InsertPoint afterTryIP = builder.saveInsertionPoint();

          {
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.restoreInsertionPoint(beginInsertTryBody);

            CIRGenFunction::LexicalScope tryScope{cgf, loc,
                                                  builder.getInsertionBlock()};
            tryScope.setAsTry(tryOp);
            cgf.enterCXXTryStmt(*tryStmt, tryOp);

            (void)cgf.emitStmt(tryStmt->getTryBlock(), /*useCurrentScope=*/false);
            builder.CIRBaseBuilderTy::createStore(
                loc, builder.getBool(false, loc), coro.resumeEHVar);
            cir::YieldOp::create(builder, loc);

            cgf.exitCXXTryStmt(*tryStmt);
          }

          builder.restoreInsertionPoint(afterTryIP);
          awaitRes.rv = RValue::getIgnored();
          cir::YieldOp::create(builder, loc);
          return;
        }

        CIRGenFunction::ConditionalEvaluation conditionalResume(cgf);
        conditionalResume.beginEvaluation();

        // FIXME(cir): the alloca for the resume expr should be placed in the
        // enclosing cir.scope instead.
        if (forLValue) {
          awaitRes.lv = cgf.emitLValue(s.getResumeExpr());
          // For lvalue results, the address is defined inside the resume
          // region. Store it to a tmp alloca so we can reload it after the
          // cir.await op completes (same pattern as scalar rvalues).
          tmpResumeRValAddr = cgf.emitAlloca(
              "__coawait_resume_lval", awaitRes.lv.getPointer().getType(), loc,
              CharUnits::fromQuantity(8),
              builder.getBestAllocaInsertPoint(scopeParentBlock));
          builder.CIRBaseBuilderTy::createStore(loc, awaitRes.lv.getPointer(),
                                                tmpResumeRValAddr);
        } else {
          awaitRes.rv =
              cgf.emitAnyExpr(s.getResumeExpr(), aggSlot, ignoreResult);
          // For scalar results, store to a tmp alloca so we can reload after
          // the cir.await region completes. Aggregate results are written
          // directly to aggSlot and don't need store/reload.
          if (!awaitRes.rv.isIgnored() &&
              (awaitRes.rv.isScalar() || awaitRes.rv.isComplex())) {
            // Create the alloca in the block before the scope wrapping
            // cir.await.  Both scalar and complex values are single MLIR
            // values that need store/reload across the cir.await boundary.
            mlir::Value val = awaitRes.rv.isScalar()
                                  ? awaitRes.rv.getValue()
                                  : awaitRes.rv.getComplexValue();
            tmpResumeRValAddr = cgf.emitAlloca(
                "__coawait_resume_rval", val.getType(), loc,
                CharUnits::One(),
                builder.getBestAllocaInsertPoint(scopeParentBlock));
            // Store the rvalue so we can reload it before the promise call.
            builder.CIRBaseBuilderTy::createStore(loc, val,
                                                  tmpResumeRValAddr);
          }
        }
        conditionalResume.endEvaluation();

        // Returns control back to parent.
        cir::YieldOp::create(builder, loc);
      },
      /*cleanupBuilder=*/
      needsDestroyCleanup ? BuilderCallbackRef(cleanupRegionBuilder)
                          : BuilderCallbackRef(nullptr));

  assert(awaitBuild.succeeded() && "Should know how to codegen");
  return awaitRes;
}

static RValue emitSuspendExpr(CIRGenFunction &cgf,
                              const CoroutineSuspendExpr &e,
                              cir::AwaitKind kind, AggValueSlot aggSlot,
                              bool ignoreResult) {
  RValue rval;
  mlir::Location scopeLoc = cgf.getLoc(e.getSourceRange());

  // Since we model suspend / resume as an inner region, we must store
  // resume scalar results in a tmp alloca, and load it after we build the
  // suspend expression. An alternative way to do this would be to make
  // every region return a value when promise.return_value() is used, but
  // it's a bit awkward given that resume is the only region that actually
  // returns a value.
  mlir::Block *currEntryBlock = cgf.curLexScope->getEntryBlock();
  [[maybe_unused]] mlir::Value tmpResumeRValAddr;

  // No need to explicitly wrap this into a scope since the AST already uses a
  // ExprWithCleanups, which will wrap this into a cir.scope anyways.
  rval = emitSuspendExpression(cgf, *cgf.curCoro.data, e, kind, aggSlot,
                               ignoreResult, currEntryBlock, tmpResumeRValAddr,
                               /*forLValue*/ false)
             .rv;

  if (ignoreResult || rval.isIgnored())
    return rval;

  if (rval.isScalar()) {
    rval = RValue::get(cir::LoadOp::create(cgf.getBuilder(), scopeLoc,
                                           rval.getValue().getType(),
                                           tmpResumeRValAddr));
  } else if (rval.isAggregate()) {
    // Aggregate result was written directly to aggSlot in the resume builder.
    // No reload needed — the data is already at the destination address.
  } else {
    // Complex values use the same store/reload pattern as scalars — they
    // are single MLIR values in CIR (not address-based like aggregates).
    assert(rval.isComplex() && "Expected complex RValue");
    rval = RValue::getComplex(cir::LoadOp::create(
        cgf.getBuilder(), scopeLoc, rval.getComplexValue().getType(),
        tmpResumeRValAddr));
  }
  return rval;
}

RValue CIRGenFunction::emitCoawaitExpr(const CoawaitExpr &e,
                                       AggValueSlot aggSlot,
                                       bool ignoreResult) {
  return emitSuspendExpr(*this, e, curCoro.data->currentAwaitKind, aggSlot,
                         ignoreResult);
}

LValue CIRGenFunction::emitCoawaitLValue(const CoawaitExpr &e) {
  assert(getCoroutineSuspendExprReturnType(getContext(), &e)
             ->isReferenceType() &&
         "Can't have a scalar return unless the return type is a "
         "reference type!");
  mlir::Block *currEntryBlock = curLexScope->getEntryBlock();
  mlir::Value tmpResumeRValAddr;
  LValueOrRValue res =
      emitSuspendExpression(*this, *curCoro.data, e,
                            curCoro.data->currentAwaitKind,
                            AggValueSlot::ignored(), /*ignoreResult=*/false,
                            currEntryBlock, tmpResumeRValAddr,
                            /*forLValue=*/true);
  // Reload the lvalue address from the temp alloca (it was stored inside
  // the resume region to cross the region boundary).
  mlir::Location loc = getLoc(e.getSourceRange());
  mlir::Value addr = cir::LoadOp::create(
      builder, loc, res.lv.getPointer().getType(), tmpResumeRValAddr);
  return LValue::makeAddr(Address(addr, res.lv.getAddress().getElementType(),
                                  res.lv.getAlignment()),
                          e.getType(), res.lv.getBaseInfo());
}

RValue CIRGenFunction::emitCoyieldExpr(const CoyieldExpr &e,
                                       AggValueSlot aggSlot,
                                       bool ignoreResult) {
  return emitSuspendExpr(*this, e, cir::AwaitKind::Yield, aggSlot,
                         ignoreResult);
}

LValue CIRGenFunction::emitCoyieldLValue(const CoyieldExpr &e) {
  assert(getCoroutineSuspendExprReturnType(getContext(), &e)
             ->isReferenceType() &&
         "Can't have a scalar return unless the return type is a "
         "reference type!");
  mlir::Block *currEntryBlock = curLexScope->getEntryBlock();
  mlir::Value tmpResumeRValAddr;
  LValueOrRValue res =
      emitSuspendExpression(*this, *curCoro.data, e, cir::AwaitKind::Yield,
                            AggValueSlot::ignored(), /*ignoreResult=*/false,
                            currEntryBlock, tmpResumeRValAddr,
                            /*forLValue=*/true);
  // Reload the lvalue address from the temp alloca (it was stored inside
  // the resume region to cross the region boundary).
  mlir::Location loc = getLoc(e.getSourceRange());
  mlir::Value addr = cir::LoadOp::create(
      builder, loc, res.lv.getPointer().getType(), tmpResumeRValAddr);
  return LValue::makeAddr(Address(addr, res.lv.getAddress().getElementType(),
                                  res.lv.getAlignment()),
                          e.getType(), res.lv.getBaseInfo());
}

mlir::LogicalResult CIRGenFunction::emitCoreturnStmt(CoreturnStmt const &s,
                                                     bool isFallthrough) {
  ++curCoro.data->coreturnCount;
  curLexScope->setCoreturn();

  const Expr *rv = s.getOperand();
  if (rv && rv->getType()->isVoidType() && !isa<InitListExpr>(rv)) {
    // Make sure to evaluate the non initlist expression of a co_return
    // with a void expression for side effects.
    RunCleanupsScope cleanupScope(*this);
    emitIgnoredExpr(rv);
  }

  assert(curCoro.data->finalSuspendCleanupDepth &&
         "co_return requires a final-suspend cleanup depth");

  // On the implicit fall-off path the coroutine body's block scope has already
  // exited, so its automatic locals must be destroyed BEFORE the synthesized
  // promise.return_void() runs ([stmt.return.coroutine], [dcl.fct.def.coroutine]
  // p6). For an explicit co_return the locals are still in scope at the
  // statement, so the promise call runs first and the locals are destroyed as
  // the scope exits afterwards. Only the body-local cleanups above
  // finalSuspendCleanupDepth run here; the promise/GRO/frame sit below that
  // depth and are left live for the return_void() call. Matches classic
  // CodeGen, whose fall-off destroys the body scope inside EmitStmt(Body)
  // before EmitCoreturnStmt emits return_void (CGCoroutine.cpp:860-867,392-403).
  if (isFallthrough) {
    if (ehStack.stable_begin() != *curCoro.data->finalSuspendCleanupDepth)
      emitCleanupsForEarlyExit(*curCoro.data->finalSuspendCleanupDepth);
    if (emitStmt(s.getPromiseCall(), /*useCurrentScope=*/true).failed())
      return mlir::failure();
  } else {
    if (emitStmt(s.getPromiseCall(), /*useCurrentScope=*/true).failed())
      return mlir::failure();
    if (ehStack.stable_begin() != *curCoro.data->finalSuspendCleanupDepth)
      emitCleanupsForEarlyExit(*curCoro.data->finalSuspendCleanupDepth);
  }

  // Use symbolic goto to escape nested structured regions after cleanup
  // threading. GotoSolver resolves the target once FlattenCFG has made the
  // final-suspend label reachable in the function body region.
  mlir::Location loc = getLoc(s.getSourceRange());
  auto fn = cast<cir::FuncOp>(curFn);
  auto gotoOp = cir::GotoOp::create(
      builder, loc, getOrCreateFinalSuspendLabel(*curCoro.data, fn));
  addOpToNewSourceAtom(gotoOp);
  builder.createBlock(builder.getBlock()->getParent());

  return mlir::success();
}
