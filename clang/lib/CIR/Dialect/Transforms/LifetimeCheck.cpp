//===- Lifetimecheck.cpp - emit diagnostic checks for lifetime violations -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PassDetail.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Mangle.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"

#include "clang/CIR/Interfaces/CIRLoopOpInterface.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/SmallSet.h"

#include <functional>

using namespace mlir;
using namespace cir;

namespace mlir {
#define GEN_PASS_DEF_LIFETIMECHECK
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

namespace {

struct LocOrdering {
  bool operator()(mlir::Location L1, mlir::Location L2) const {
    return std::less<const void *>()(L1.getAsOpaquePointer(),
                                     L2.getAsOpaquePointer());
  }
};

struct LifetimeCheckPass : public impl::LifetimeCheckBase<LifetimeCheckPass> {
  LifetimeCheckPass() = default;
  using LifetimeCheckBase::LifetimeCheckBase;
  void runOnOperation() override;

  void checkOperation(Operation *op);
  void checkFunc(cir::FuncOp fnOp);
  void checkBlock(Block &block);

  void checkRegionWithScope(Region &region);
  void checkRegion(Region &region);

  void checkIf(IfOp op);
  void checkSwitch(SwitchOp op);
  void checkLoop(LoopOpInterface op);
  void checkAlloca(AllocaOp op);
  void checkStore(StoreOp op);
  void checkLoad(LoadOp op);
  void checkCopy(CopyOp copyOp);
  void checkCall(CallOp callOp);
  void checkAwait(AwaitOp awaitOp);
  void checkReturn(ReturnOp retOp);

  void classifyAndInitTypeCategories(mlir::Value addr, mlir::Type t,
                                     mlir::Location loc, unsigned nestLevel);
  void updatePointsTo(mlir::Value addr, mlir::Value data, mlir::Location loc);
  void updatePointsToForConstRecord(mlir::Value addr,
                                    cir::ConstRecordAttr value,
                                    mlir::Location loc);
  void updatePointsToForZeroRecord(mlir::Value addr, RecordType sTy,
                                   mlir::Location loc);

  enum DerefStyle {
    Direct,
    RetLambda,
    CallParam,
    IndirectCallParam,
    UseAfterMove,
  };
  bool checkPointerDeref(mlir::Value addr, mlir::Location loc,
                         DerefStyle derefStyle = DerefStyle::Direct);
  void checkCoroTaskStore(StoreOp storeOp);
  void checkLambdaCaptureStore(StoreOp storeOp);
  // Checks and tracks moved-from values in store operations.
  // Handles two scenarios:
  // 1. Detects use-after-move: Reports diagnostic if storing value loaded from
  // moved-from source
  // 2. Tracks rvalue initialization: Marks source as moved-from (e.g., int
  // b(std::move(a))) Only applies to value types; only marks AllocaOp
  // destinations; skips dereference operations.
  void checkMovedFromValue(StoreOp storeOp);
  void trackCallToCoroutine(CallOp callOp);

  void checkCtor(CallOp callOp, cir::CtorKind ctorKind);
  void checkMoveCtor(CallOp callOp, cir::CtorKind ctorKind);
  void checkMoveAssignment(CallOp callOp, const clang::CXXMethodDecl *m);
  // Checks function call arguments for moves via rvalue references and tracks
  // their moved-from state.
  //
  // For each argument passed to an rvalue reference parameter (T&&), this
  // function:
  // 1. Checks if the argument is currently in a valid state (not already moved)
  // 2. Marks the argument as moved-from after the call
  //
  // This performs both checking (detecting use-after-move) and tracking
  // (updating program state) for function call arguments.
  void checkMoveInCallArgs(CallOp callOp);
  void checkArgForRValueRef(CallOp callOp, unsigned argIdx,
                            const clang::FunctionDecl *funcDecl);
  void checkCopyAssignment(CallOp callOp, const clang::CXXMethodDecl *m);
  void checkNonConstUseOfOwner(mlir::Value ownerAddr, mlir::Location loc);
  void markOwnerAsMovedFrom(mlir::Value addr, mlir::Location loc);
  // Returns true if the value represents a temporary that should be skipped
  // for move tracking purposes.
  //
  // Temporaries are generally skipped because they're destroyed at the end of
  // the full expression and cannot be used after being moved from. However,
  // coroutine task temporaries are an exception - they need lifetime tracking
  // even as temporaries because they may be captured by the coroutine frame.
  //
  // Note: Currently uses "ref.tmp" prefix detection which is not fully
  // reliable. See FIXME comment in implementation for future improvements.
  bool isSkippableTemporary(mlir::Value v);

  // Helper methods for checking value states and tracking moved-from values
  bool isValueType(mlir::Value addr);
  bool hasInvalidState(mlir::Value addr);
  bool isValueTypeMovedFrom(mlir::Value addr);
  void markPointerOrValueTypeAsMovedFrom(mlir::Value addr, mlir::Location loc);

  bool isSmartPointerSafeMethod(llvm::StringRef methodName);
  void checkOperators(CallOp callOp, const clang::CXXMethodDecl *m);
  void checkOtherMethodsAndFunctions(CallOp callOp,
                                     const clang::CXXMethodDecl *m);
  void checkForOwnerAndPointerArguments(CallOp callOp, unsigned firstArgIdx);

  mlir::Value getThisParamPointerCategory(CallOp callOp);
  mlir::Value getThisParamOwnerCategory(CallOp callOp);

  // Tracks current module.
  ModuleOp theModule;
  // Track current function under analysis
  std::optional<FuncOp> currFunc;

  // Common helpers.
  bool isCtorInitPointerFromOwner(CallOp callOp);
  mlir::Value getNonConstUseOfOwner(CallOp callOp,
                                    const clang::CXXMethodDecl *m);
  bool isOwnerOrPointerClassMethod(CallOp callOp,
                                   const clang::CXXMethodDecl *m);

  // Diagnostic helpers.
  void emitInvalidHistory(mlir::InFlightDiagnostic &D, mlir::Value histKey,
                          mlir::Location warningLoc,
                          DerefStyle derefStyle = DerefStyle::Direct);

  // Helper to look up RecordDecl from ASTContext using RecordType name.
  const clang::RecordDecl *getRecordDecl(cir::RecordType ty);
  // Helper to look up CXXMethodDecl for a callee.
  const clang::CXXMethodDecl *getMethodDecl(ModuleOp mod, CallOp callOp);

  ///
  /// Pass options handling
  /// ---------------------

  struct Options {
    enum : unsigned {
      None = 0,
      RemarkPsetInvalid = 1,
      RemarkPsetAlways = 1 << 1,
      RemarkAll = 1 << 2,
      HistoryNull = 1 << 3,
      HistoryInvalid = 1 << 4,
      HistoryAll = 1 << 5,
    };
    unsigned val = None;
    unsigned histLimit = 1;
    bool isOptionsParsed = false;

    void parseOptions(ArrayRef<llvm::StringRef> remarks,
                      ArrayRef<llvm::StringRef> hist, unsigned hist_limit) {
      if (isOptionsParsed)
        return;

      for (auto &remark : remarks) {
        val |= StringSwitch<unsigned>(remark)
                   .Case("pset-invalid", RemarkPsetInvalid)
                   .Case("pset-always", RemarkPsetAlways)
                   .Case("all", RemarkAll)
                   .Default(None);
      }
      for (auto &h : hist) {
        val |= StringSwitch<unsigned>(h)
                   .Case("invalid", HistoryInvalid)
                   .Case("null", HistoryNull)
                   .Case("all", HistoryAll)
                   .Default(None);
      }
      histLimit = hist_limit;
      isOptionsParsed = true;
    }

    void parseOptions(LifetimeCheckPass &pass) {
      llvm::SmallVector<llvm::StringRef, 4> remarks;
      llvm::SmallVector<llvm::StringRef, 4> hists;

      for (auto &r : pass.remarksList)
        remarks.push_back(r);

      for (auto &h : pass.historyList)
        hists.push_back(h);

      parseOptions(remarks, hists, pass.historyLimit);
    }

    bool emitRemarkAll() { return val & RemarkAll; }
    bool emitRemarkPsetInvalid() {
      return emitRemarkAll() || val & RemarkPsetInvalid;
    }
    bool emitRemarkPsetAlways() {
      return emitRemarkAll() || val & RemarkPsetAlways;
    }

    bool emitHistoryAll() { return val & HistoryAll; }
    bool emitHistoryNull() { return emitHistoryAll() || val & HistoryNull; }
    bool emitHistoryInvalid() {
      return emitHistoryAll() || val & HistoryInvalid;
    }
  } opts;

  ///
  /// State
  /// -----

  struct State {
    using DataTy = enum {
      Invalid,
      NullPtr,
      Global,
      OwnedBy,
      LocalValue,
      NumKindsMinusOne = LocalValue
    };
    State() { val.setInt(Invalid); }
    State(DataTy d) { val.setInt(d); }
    State(mlir::Value v, DataTy d = LocalValue) {
      assert((d == LocalValue || d == OwnedBy) && "expected value or owned");
      val.setPointerAndInt(v, d);
    }

    static constexpr int KindBits = 3;
    static_assert((1 << KindBits) > NumKindsMinusOne,
                  "Not enough room for kind!");
    llvm::PointerIntPair<mlir::Value, KindBits> val;

    bool operator<(const State &RHS) const {
      if (hasValue() && RHS.hasValue())
        return val.getPointer().getAsOpaquePointer() <
               RHS.val.getPointer().getAsOpaquePointer();
      return val.getInt() < RHS.val.getInt();
    }
    bool operator==(const State &RHS) const {
      if (hasValue() && RHS.hasValue())
        return val.getPointer() == RHS.val.getPointer();
      return val.getInt() == RHS.val.getInt();
    }

    bool isLocalValue() const { return val.getInt() == LocalValue; }
    bool isOwnedBy() const { return val.getInt() == OwnedBy; }
    bool hasValue() const { return isLocalValue() || isOwnedBy(); }

    mlir::Value getData() const {
      assert(hasValue() && "data type does not hold a mlir::Value");
      return val.getPointer();
    }

    void dump(llvm::raw_ostream &OS = llvm::errs(), int ownedGen = 0);

    static State getInvalid() { return {Invalid}; }
    static State getNullPtr() { return {NullPtr}; }
    static State getLocalValue(mlir::Value v) { return {v, LocalValue}; }
    static State getOwnedBy(mlir::Value v) { return {v, State::OwnedBy}; }
  };

  ///
  /// Invalid and null history tracking
  /// ---------------------------------
  enum InvalidStyle {
    Unknown,
    EndOfScope,
    NotInitialized,
    MovedFrom,
    NonConstUseOfOwner,
  };

  struct InvalidHistEntry {
    InvalidStyle style = Unknown;
    std::optional<mlir::Location> loc;
    std::optional<mlir::Value> val;
    InvalidHistEntry() = default;
    InvalidHistEntry(InvalidStyle s, std::optional<mlir::Location> l,
                     std::optional<mlir::Value> v)
        : style(s), loc(l), val(v) {}
  };

  struct InvalidHist {
    llvm::SmallVector<InvalidHistEntry, 8> entries;
    void add(mlir::Value ptr, InvalidStyle invalidStyle, mlir::Location loc,
             std::optional<mlir::Value> val = {}) {
      entries.emplace_back(InvalidHistEntry(invalidStyle, loc, val));
    }
  };

  llvm::DenseMap<mlir::Value, InvalidHist> invalidHist;

  using PMapNullHistType =
      llvm::DenseMap<mlir::Value, std::optional<mlir::Location>>;
  PMapNullHistType pmapNullHist;

  llvm::SmallSet<mlir::Location, 8, LocOrdering> emittedDiagnostics;

  ///
  /// Pointer Map and Pointer Set
  /// ---------------------------

  using PSetType = llvm::SmallSet<State, 4>;
  using PMapType = llvm::DenseMap<mlir::Value, PSetType>;

  PMapType *currPmap = nullptr;
  PMapType &getPmap() { return *currPmap; }
  void markPsetInvalid(mlir::Value ptr, InvalidStyle invalidStyle,
                       mlir::Location loc,
                       std::optional<mlir::Value> extraVal = {}) {
    auto &pset = getPmap()[ptr];

    if (pset.count(State::getInvalid()))
      return;

    pset.insert(State::getInvalid());
    invalidHist[ptr].add(ptr, invalidStyle, loc, extraVal);
  }

  void markPsetNull(mlir::Value addr, mlir::Location loc,
                    InvalidStyle invalidStyle = InvalidStyle::Unknown) {
    getPmap()[addr].clear();
    getPmap()[addr].insert(State::getNullPtr());
    pmapNullHist[addr] = loc;
    // Also record in invalidHist for diagnostic history
    if (invalidStyle != InvalidStyle::Unknown)
      invalidHist[addr].add(addr, invalidStyle, loc);
  }

  void joinPmaps(SmallVectorImpl<PMapType> &pmaps);

  void kill(const State &s, InvalidStyle invalidStyle, mlir::Location loc);
  void killInPset(mlir::Value ptrKey, const State &s, InvalidStyle invalidStyle,
                  mlir::Location loc, std::optional<mlir::Value> extraVal);

  // Local pointers
  SmallPtrSet<mlir::Value, 8> ptrs;

  DenseMap<mlir::Value, unsigned> owners;
  void addOwner(mlir::Value o) {
    // In loop bodies, the same alloca can be re-visited during
    // the fixed-point iteration in checkLoop.
    owners[o] = 0;
  }
  void incOwner(mlir::Value o) {
    assert(owners.count(o) && "entry expected");
    owners[o]++;
  }

  // Aggregates and exploded fields.
  using ExplodedFieldsTy = llvm::SmallVector<mlir::Value, 4>;
  DenseMap<mlir::Value, ExplodedFieldsTy> aggregates;
  void addAggregate(mlir::Value a, SmallVectorImpl<mlir::Value> &fields) {
    // In loop bodies, the same alloca can be re-visited during
    // the fixed-point iteration in checkLoop.
    aggregates[a].swap(fields);
  }

  void printPset(PSetType &pset, llvm::raw_ostream &OS = llvm::errs());
  LLVM_DUMP_METHOD void dumpPmap(PMapType &pmap);
  LLVM_DUMP_METHOD void dumpCurrentPmap();

  ///
  /// Coroutine tasks (promise_type)
  /// ------------------------------
  llvm::DenseMap<mlir::Type, bool> IsTaskTyCache;
  bool isTaskType(mlir::Value taskVal);
  bool isTaskType(mlir::Type ty);
  SmallPtrSet<mlir::Value, 8> tasks;

  ///
  /// Lambdas
  /// -------
  llvm::DenseMap<mlir::Type, bool> IsLambdaTyCache;
  // Track types we already know to be smart pointers
  llvm::DenseMap<mlir::Type, bool> IsSmartPointerTyCache;
  bool isLambdaType(mlir::Type ty);
  mlir::Value getLambdaFromMemberAccess(mlir::Value addr);

  ///
  /// Scope, context and guards
  /// -------------------------

  struct LexicalScopeContext {
    unsigned Depth = 0;
    LexicalScopeContext() = delete;

    llvm::PointerUnion<mlir::Region *, mlir::Operation *> parent;
    LexicalScopeContext(mlir::Region *R) : parent(R) {}
    LexicalScopeContext(mlir::Operation *Op) : parent(Op) {}
    ~LexicalScopeContext() = default;

    SmallPtrSet<mlir::Value, 4> localValues;
    SmallPtrSet<mlir::Value, 2> localTempTasks;
    DenseMap<mlir::Value, mlir::Location> localRetLambdas;

    LLVM_DUMP_METHOD void dumpLocalValues();
  };

  class LexicalScopeGuard {
    LifetimeCheckPass &Pass;
    LexicalScopeContext *OldVal = nullptr;

  public:
    LexicalScopeGuard(LifetimeCheckPass &p, LexicalScopeContext *L) : Pass(p) {
      if (Pass.currScope) {
        OldVal = Pass.currScope;
        L->Depth++;
      }
      Pass.currScope = L;
    }

    LexicalScopeGuard(const LexicalScopeGuard &) = delete;
    LexicalScopeGuard &operator=(const LexicalScopeGuard &) = delete;
    LexicalScopeGuard &operator=(LexicalScopeGuard &&other) = delete;

    void cleanup();
    void restore() { Pass.currScope = OldVal; }
    ~LexicalScopeGuard() {
      cleanup();
      restore();
    }
  };

  class PmapGuard {
    LifetimeCheckPass &Pass;
    PMapType *OldVal = nullptr;

  public:
    PmapGuard(LifetimeCheckPass &lcp, PMapType *L) : Pass(lcp) {
      if (Pass.currPmap) {
        OldVal = Pass.currPmap;
      }
      Pass.currPmap = L;
    }

    PmapGuard(const PmapGuard &) = delete;
    PmapGuard &operator=(const PmapGuard &) = delete;
    PmapGuard &operator=(PmapGuard &&other) = delete;

    void restore() { Pass.currPmap = OldVal; }
    ~PmapGuard() { restore(); }
  };

  LexicalScopeContext *currScope = nullptr;

  ///
  /// AST related
  /// -----------

  std::optional<clang::ASTContext *> astCtx;
  void setASTContext(clang::ASTContext *c) { astCtx = c; }

  // Map from CIR record type name to AST RecordDecl
  llvm::DenseMap<mlir::StringAttr, const clang::RecordDecl *> recordDeclMap;
  void buildRecordDeclMap();
  bool recordDeclMapBuilt = false;

  // Cache from mangled name to CXXMethodDecl
  llvm::StringMap<const clang::CXXMethodDecl *> methodDeclCache;
};
} // namespace

static std::string getVarNameFromValue(mlir::Value v) {

  auto srcOp = v.getDefiningOp();
  if (!srcOp) {
    auto blockArg = cast<BlockArgument>(v);
    assert(blockArg.getOwner()->isEntryBlock() && "random block args NYI");
    llvm::SmallString<128> finalName;
    llvm::raw_svector_ostream Out(finalName);
    Out << "fn_arg:" << blockArg.getArgNumber();
    return Out.str().str();
  }

  if (auto allocaOp = dyn_cast<AllocaOp>(srcOp))
    return allocaOp.getName().str();
  if (auto getElemOp = dyn_cast<GetMemberOp>(srcOp)) {
    auto parent = getElemOp.getAddr().getDefiningOp<cir::AllocaOp>();
    if (parent) {
      llvm::SmallString<128> finalName;
      llvm::raw_svector_ostream Out(finalName);
      Out << parent.getName() << "." << getElemOp.getName();
      return Out.str().str();
    }
  }
  if (auto callOp = dyn_cast<CallOp>(srcOp)) {
    if (callOp.getCallee()) {
      llvm::SmallString<128> finalName;
      llvm::raw_svector_ostream Out(finalName);
      Out << "call:" << callOp.getCallee()->str();
      return Out.str().str();
    }
  }
  assert(0 && "how did it get here?");
  return "";
}

static Location getEndLoc(Location loc, int idx = 1) {
  auto fusedLoc = llvm::dyn_cast<FusedLoc>(loc);
  if (!fusedLoc)
    return loc;
  return fusedLoc.getLocations()[idx];
}

static Location getEndLocForHist(Operation *Op) {
  return getEndLoc(Op->getLoc());
}

static Location getEndLocIf(IfOp ifOp, Region *R) {
  assert(ifOp && "what other regions create their own scope?");
  if (&ifOp.getThenRegion() == R)
    return getEndLoc(ifOp.getLoc());
  return getEndLoc(ifOp.getLoc(), /*idx=*/3);
}

static Location getEndLocForHist(Region *R) {
  auto parentOp = R->getParentOp();
  if (isa<IfOp>(parentOp))
    return getEndLocIf(cast<IfOp>(parentOp), R);
  if (isa<FuncOp>(parentOp))
    return getEndLoc(parentOp->getLoc());
  llvm_unreachable("what other regions create their own scope?");
}

static Location getEndLocForHist(LifetimeCheckPass::LexicalScopeContext &lsc) {
  assert(!lsc.parent.isNull() && "shouldn't be null");
  if (auto r = mlir::dyn_cast<Region *>(lsc.parent))
    return getEndLocForHist(r);
  assert(mlir::isa<Operation *>(lsc.parent) &&
         "Only support operation beyond this point");
  return getEndLocForHist(mlir::cast<Operation *>(lsc.parent));
}

void LifetimeCheckPass::killInPset(mlir::Value ptrKey, const State &s,
                                   InvalidStyle invalidStyle,
                                   mlir::Location loc,
                                   std::optional<mlir::Value> extraVal) {
  auto &pset = getPmap()[ptrKey];
  if (pset.contains(s)) {
    pset.erase(s);
    markPsetInvalid(ptrKey, invalidStyle, loc, extraVal);
  }
}

void LifetimeCheckPass::kill(const State &s, InvalidStyle invalidStyle,
                             mlir::Location loc) {
  assert(s.hasValue() && "does not know how to kill other data types");
  mlir::Value v = s.getData();
  std::optional<mlir::Value> extraVal;
  if (invalidStyle == InvalidStyle::EndOfScope)
    extraVal = v;

  for (auto &mapEntry : getPmap()) {
    auto ptr = mapEntry.first;

    if (v == ptr)
      continue;

    if (s.isLocalValue() && owners.count(v))
      killInPset(ptr, State::getOwnedBy(v), invalidStyle, loc, extraVal);
    killInPset(ptr, s, invalidStyle, loc, extraVal);
  }

  if (invalidStyle == InvalidStyle::EndOfScope) {
    owners.erase(v);
    ptrs.erase(v);
    tasks.erase(v);
  }
}

void LifetimeCheckPass::LexicalScopeGuard::cleanup() {
  auto *localScope = Pass.currScope;
  for (auto pointee : localScope->localValues)
    Pass.kill(State::getLocalValue(pointee), InvalidStyle::EndOfScope,
              getEndLocForHist(*localScope));

  for (auto l : localScope->localRetLambdas)
    Pass.checkPointerDeref(l.first, l.second, DerefStyle::RetLambda);
}

void LifetimeCheckPass::checkBlock(Block &block) {
  for (Operation &op : block.getOperations())
    checkOperation(&op);
}

void LifetimeCheckPass::checkRegion(Region &region) {
  for (Block &block : region.getBlocks())
    checkBlock(block);
}

void LifetimeCheckPass::checkRegionWithScope(Region &region) {
  LexicalScopeContext lexScope{&region};
  LexicalScopeGuard scopeGuard{*this, &lexScope};
  for (Block &block : region.getBlocks())
    checkBlock(block);
}

void LifetimeCheckPass::checkFunc(cir::FuncOp fnOp) {
  currFunc = fnOp;
  if (currPmap)
    getPmap().clear();
  pmapNullHist.clear();
  invalidHist.clear();
  emittedDiagnostics.clear();

  PMapType localPmap{};
  PmapGuard pmapGuard{*this, &localPmap};

  for (Region &region : fnOp->getRegions())
    checkRegionWithScope(region);

  currFunc.reset();
}

void LifetimeCheckPass::joinPmaps(SmallVectorImpl<PMapType> &pmaps) {
  for (auto &mapEntry : getPmap()) {
    auto &val = mapEntry.first;

    PSetType joinPset;
    for (auto &pmapOp : pmaps)
      llvm::set_union(joinPset, pmapOp[val]);

    getPmap()[val] = joinPset;
  }
}

void LifetimeCheckPass::checkLoop(LoopOpInterface loopOp) {
  llvm::SmallVector<PMapType, 4> pmapOps;
  llvm::SmallVector<Region *, 4> regionsToCheck;

  auto setupLoopRegionsToCheck = [&](bool isSubsequentTaken = false) {
    regionsToCheck = loopOp.getRegionsInExecutionOrder();
    if (loopOp.maybeGetStep() && !isSubsequentTaken)
      regionsToCheck.pop_back();
  };

  pmapOps.push_back(getPmap());

  PMapType loopExitPmap;
  {
    loopExitPmap = getPmap();
    PmapGuard pmapGuard{*this, &loopExitPmap};
    setupLoopRegionsToCheck();
    for (auto *r : regionsToCheck)
      checkRegion(*r);
    pmapOps.push_back(loopExitPmap);
  }

  if (getPmap() != loopExitPmap) {
    PMapType otherTakenPmap = loopExitPmap;
    PmapGuard pmapGuard{*this, &otherTakenPmap};
    setupLoopRegionsToCheck(/*isSubsequentTaken=*/true);
    for (auto *r : regionsToCheck)
      checkRegion(*r);
    pmapOps.push_back(otherTakenPmap);
  }

  joinPmaps(pmapOps);
}

void LifetimeCheckPass::checkAwait(AwaitOp awaitOp) {
  llvm::SmallVector<PMapType, 4> pmapOps;

  for (auto r : awaitOp.getRegions()) {
    PMapType regionPmap = getPmap();
    PmapGuard pmapGuard{*this, &regionPmap};
    checkRegion(*r);
    pmapOps.push_back(regionPmap);
  }

  joinPmaps(pmapOps);
}

void LifetimeCheckPass::checkReturn(ReturnOp retOp) {
  if (retOp.getNumOperands() == 0)
    return;

  auto retTy = retOp.getOperand(0).getType();
  if (!isLambdaType(retTy))
    return;

  auto loadOp = retOp.getOperand(0).getDefiningOp<cir::LoadOp>();
  assert(loadOp && "expected cir.load");
  if (!loadOp.getAddr().getDefiningOp<cir::AllocaOp>())
    return;

  assert(!currScope->localRetLambdas.count(loadOp.getAddr()) &&
         "lambda already returned?");
  currScope->localRetLambdas.insert(
      std::make_pair(loadOp.getAddr(), loadOp.getLoc()));
}

void LifetimeCheckPass::checkSwitch(SwitchOp switchOp) {
  llvm::SmallVector<PMapType, 2> pmapOps;

  llvm::SmallVector<CaseOp> cases;
  if (!switchOp.isSimpleForm(cases))
    return;

  auto isCaseFallthroughTerminated = [&](Region &r) {
    assert(r.getBlocks().size() == 1 && "cannot yet handle branches");
    Block &block = r.back();
    assert(!block.empty() && "case regions cannot be empty");

    YieldOp y = dyn_cast<YieldOp>(block.back());
    if (!y)
      return false;
    return true;
  };

  for (unsigned regionCurrent = 0, regionPastEnd = cases.size();
       regionCurrent != regionPastEnd; ++regionCurrent) {
    PMapType locaCasePmap = getPmap();
    PmapGuard pmapGuard{*this, &locaCasePmap};

    unsigned idx = regionCurrent;
    while (idx < regionPastEnd) {
      checkRegion(cases[idx].getRegion());
      if (!isCaseFallthroughTerminated(cases[idx].getRegion()))
        break;
      idx++;
    }
    pmapOps.push_back(locaCasePmap);
  }

  joinPmaps(pmapOps);
}

void LifetimeCheckPass::checkIf(IfOp ifOp) {
  llvm::SmallVector<PMapType, 2> pmapOps;

  {
    PMapType localThenPmap = getPmap();
    PmapGuard pmapGuard{*this, &localThenPmap};
    checkRegionWithScope(ifOp.getThenRegion());
    pmapOps.push_back(localThenPmap);
  }

  if (!ifOp.getElseRegion().empty()) {
    PMapType localElsePmap = getPmap();
    PmapGuard pmapGuard{*this, &localElsePmap};
    checkRegionWithScope(ifOp.getElseRegion());
    pmapOps.push_back(localElsePmap);
  } else {
    pmapOps.push_back(getPmap());
  }

  joinPmaps(pmapOps);
}

// Build a map from CIR record type names to AST RecordDecls by walking
// the AST TranslationUnit.
void LifetimeCheckPass::buildRecordDeclMap() {
  if (recordDeclMapBuilt || !astCtx || !*astCtx || !theModule)
    return;
  recordDeclMapBuilt = true;

  auto &ctx = **astCtx;
  auto *TU = ctx.getTranslationUnitDecl();
  auto &mlirCtx = *theModule.getContext();

  // Helper to get the CIR-style qualified name for a RecordDecl, matching
  // the naming convention used by CIRGenTypes::getRecordTypeName.
  auto getCIRRecordName = [&](const clang::RecordDecl *RD) -> std::string {
    if (!RD->getIdentifier())
      return "";
    clang::PrintingPolicy policy = ctx.getPrintingPolicy();
    policy.SuppressInlineNamespace = llvm::to_underlying(
        clang::PrintingPolicy::SuppressInlineNamespaceMode::None);
    policy.AlwaysIncludeTypeForTemplateArgument = true;
    policy.PrintAsCanonical = true;
    policy.SuppressTagKeyword = true;
    std::string name;
    llvm::raw_string_ostream OS(name);
    clang::QualType(ctx.getCanonicalTagType(RD)).print(OS, policy);
    return name;
  };

  std::function<void(const clang::DeclContext *)> walkDecls =
      [&](const clang::DeclContext *DC) {
        for (auto *D : DC->decls()) {
          if (auto *RD = dyn_cast<clang::RecordDecl>(D)) {
            if (!RD->isCompleteDefinition())
              continue;
            auto name = getCIRRecordName(RD);
            if (!name.empty()) {
              auto nameAttr = mlir::StringAttr::get(&mlirCtx, name);
              recordDeclMap[nameAttr] = RD;
            }
          }
          if (auto *NSD = dyn_cast<clang::NamespaceDecl>(D))
            walkDecls(NSD);
          if (auto *CTD = dyn_cast<clang::ClassTemplateDecl>(D)) {
            for (auto *spec : CTD->specializations()) {
              if (!spec->isCompleteDefinition())
                continue;
              auto specName = getCIRRecordName(spec);
              if (!specName.empty()) {
                auto nameAttr = mlir::StringAttr::get(&mlirCtx, specName);
                recordDeclMap[nameAttr] = spec;
              }
            }
          }
          // Walk into record decl contexts for nested types
          if (auto *RD = dyn_cast<clang::RecordDecl>(D))
            walkDecls(RD);
        }
      };

  walkDecls(TU);
}

// Helper to look up RecordDecl from AST context.
const clang::RecordDecl *LifetimeCheckPass::getRecordDecl(cir::RecordType ty) {
  if (!astCtx || !*astCtx)
    return nullptr;
  auto name = ty.getName();
  if (!name)
    return nullptr;

  buildRecordDeclMap();

  auto it = recordDeclMap.find(name);
  if (it != recordDeclMap.end())
    return it->second;

  // Fallback: direct lookup by identifier name.
  auto nameStr = name.getValue();
  auto &ctx = **astCtx;
  auto &idents = ctx.Idents;
  auto *II = &idents.get(nameStr);
  auto result = ctx.getTranslationUnitDecl()->lookup(II);
  for (auto *D : result) {
    if (auto *RD = dyn_cast<clang::RecordDecl>(D))
      return RD;
  }

  return nullptr;
}

template <class T>
static bool isRecordAndHasAttr(cir::RecordType ty, LifetimeCheckPass *pass) {
  auto *RD = pass->getRecordDecl(ty);
  if (!RD)
    return false;
  return RD->hasAttr<T>();
}

static bool isOwnerType(mlir::Type ty, LifetimeCheckPass *pass) {
  auto recordTy = mlir::dyn_cast<cir::RecordType>(ty);
  if (!recordTy)
    return false;
  return isRecordAndHasAttr<clang::OwnerAttr>(recordTy, pass);
}

static bool isSmartPointerType(mlir::Type ty, LifetimeCheckPass *pass,
                               llvm::DenseMap<mlir::Type, bool> &cache) {
  // Check cache first
  auto originalTy = ty;
  if (cache.count(originalTy))
    return cache[originalTy];

  cache[originalTy] = false;

  // Unwrap pointer type if needed
  if (auto ptrType = mlir::dyn_cast<cir::PointerType>(ty))
    ty = ptrType.getPointee();

  auto recordTy = mlir::dyn_cast<cir::RecordType>(ty);
  if (!recordTy)
    return false;

  auto *RD = pass->getRecordDecl(recordTy);
  if (!RD)
    return false;

  // Check if it's in std namespace and is unique_ptr or shared_ptr
  if (!RD->getDeclContext()->isStdNamespace())
    return false;

  llvm::StringRef name = RD->getName();
  if (name == "unique_ptr" || name == "shared_ptr")
    cache[originalTy] = true;

  return cache[originalTy];
}

bool LifetimeCheckPass::isSkippableTemporary(mlir::Value v) {
  auto allocaOp = v.getDefiningOp<cir::AllocaOp>();
  if (!allocaOp)
    return false;

  auto name = allocaOp.getName();
  // Temporaries have names starting with "ref.tmp"
  // FIXME: "ref.tmp" naming is not reliable. Consider adding a unit attribute
  // is_temporary to AllocaOp that is set by CIRGen to definitively mark
  // temporaries. This would be more robust than string prefix matching.
  if (!name.starts_with("ref.tmp"))
    return false;

  // Don't skip coroutine tasks - they need lifetime tracking even as
  // temporaries since they may be captured by the coroutine frame
  if (isTaskType(v))
    return false;

  return true;
}

static bool containsPointerElts(cir::RecordType s) {
  auto members = s.getMembers();
  return std::any_of(members.begin(), members.end(), [](mlir::Type t) {
    return mlir::isa<cir::PointerType>(t);
  });
}

static bool isAggregateType(LifetimeCheckPass *pass, mlir::Type agg) {
  auto t = mlir::dyn_cast<cir::RecordType>(agg);
  if (!t)
    return false;
  if (pass->isLambdaType(agg))
    return false;
  return containsPointerElts(t);
}

static bool isPointerType(mlir::Type t, LifetimeCheckPass *pass) {
  if (mlir::isa<cir::PointerType>(t))
    return true;
  auto recordTy = mlir::dyn_cast<cir::RecordType>(t);
  if (!recordTy)
    return false;
  return isRecordAndHasAttr<clang::PointerAttr>(recordTy, pass);
}

void LifetimeCheckPass::classifyAndInitTypeCategories(mlir::Value addr,
                                                      mlir::Type t,
                                                      mlir::Location loc,
                                                      unsigned nestLevel) {
  getPmap()[addr] = {};

  enum TypeCategory {
    Unknown = 0,
    SharedOwner = 1,
    Owner = 1 << 2,
    Pointer = 1 << 3,
    Indirection = 1 << 4,
    Aggregate = 1 << 5,
    Value = 1 << 6,
  };

  auto localStyle = [&]() {
    if (isPointerType(t, this))
      return TypeCategory::Pointer;
    if (isOwnerType(t, this))
      return TypeCategory::Owner;
    if (isAggregateType(this, t))
      return TypeCategory::Aggregate;
    return TypeCategory::Value;
  }();

  switch (localStyle) {
  case TypeCategory::Pointer:
    ptrs.insert(addr);
    markPsetInvalid(addr, InvalidStyle::NotInitialized, loc);
    break;
  case TypeCategory::Owner:
    addOwner(addr);
    getPmap()[addr].insert(State::getOwnedBy(addr));
    currScope->localValues.insert(addr);
    break;
  case TypeCategory::Aggregate: {
    if (nestLevel > 1)
      break;

    auto members = mlir::cast<cir::RecordType>(t).getMembers();
    llvm::SmallVector<mlir::Value, 4> fieldVals;
    fieldVals.assign(members.size(), {});

    std::for_each(addr.use_begin(), addr.use_end(), [&](mlir::OpOperand &use) {
      auto op = dyn_cast<cir::GetMemberOp>(use.getOwner());
      if (!op)
        return;

      auto eltAddr = op.getResult();
      if (eltAddr.use_empty())
        return;

      auto eltTy = mlir::cast<cir::PointerType>(eltAddr.getType()).getPointee();

      classifyAndInitTypeCategories(eltAddr, eltTy, loc, ++nestLevel);
      fieldVals[op.getIndex()] = eltAddr;
    });

    addAggregate(addr, fieldVals);

    LLVM_FALLTHROUGH;
  }
  case TypeCategory::Value: {
    getPmap()[addr].insert(State::getLocalValue(addr));
    currScope->localValues.insert(addr);
    break;
  }
  default:
    llvm_unreachable("NYI");
  }
}

void LifetimeCheckPass::checkAlloca(AllocaOp allocaOp) {
  classifyAndInitTypeCategories(allocaOp.getAddr(), allocaOp.getAllocaType(),
                                allocaOp.getLoc(), /*nestLevel=*/0);
}

void LifetimeCheckPass::checkCoroTaskStore(StoreOp storeOp) {
  auto taskTmp = storeOp.getValue();
  auto taskAddr = storeOp.getAddr();

  if (auto call = taskTmp.getDefiningOp<cir::CallOp>()) {
    bool potentialTaintedTask = false;
    for (auto arg : call.getArgOperands()) {
      auto alloca = arg.getDefiningOp<cir::AllocaOp>();
      if (alloca && currScope->localValues.count(alloca)) {
        getPmap()[taskAddr].insert(State::getLocalValue(alloca));
        potentialTaintedTask = true;
      }
    }

    if (potentialTaintedTask)
      tasks.insert(taskAddr);
    return;
  }
  llvm_unreachable("expecting cir.call defining op");
}

mlir::Value LifetimeCheckPass::getLambdaFromMemberAccess(mlir::Value addr) {
  auto op = addr.getDefiningOp<cir::GetMemberOp>();
  if (!op)
    return nullptr;
  auto allocaOp = op->getOperand(0).getDefiningOp<cir::AllocaOp>();
  if (!allocaOp || !isLambdaType(allocaOp.getAllocaType()))
    return nullptr;
  return allocaOp;
}

void LifetimeCheckPass::checkLambdaCaptureStore(StoreOp storeOp) {
  auto localByRefAddr = storeOp.getValue();
  auto lambdaCaptureAddr = storeOp.getAddr();

  if (!localByRefAddr.getDefiningOp<cir::AllocaOp>())
    return;
  auto lambdaAddr = getLambdaFromMemberAccess(lambdaCaptureAddr);
  if (!lambdaAddr)
    return;

  if (currScope->localValues.count(localByRefAddr))
    getPmap()[lambdaAddr].insert(State::getLocalValue(localByRefAddr));
}

void LifetimeCheckPass::updatePointsToForConstRecord(mlir::Value addr,
                                                     cir::ConstRecordAttr value,
                                                     mlir::Location loc) {
  assert(aggregates.count(addr) && "expected association with aggregate");
  int memberIdx = 0;
  for (auto &attr : value.getMembers()) {
    auto ta = mlir::dyn_cast<mlir::TypedAttr>(attr);
    assert(ta && "expected typed attribute");
    auto fieldAddr = aggregates[addr][memberIdx];
    if (fieldAddr && mlir::isa<cir::PointerType>(ta.getType())) {
      assert(mlir::isa<cir::ConstPtrAttr>(ta) &&
             "other than null not implemented");
      markPsetNull(fieldAddr, loc);
    }
    memberIdx++;
  }
}

void LifetimeCheckPass::updatePointsToForZeroRecord(mlir::Value addr,
                                                    RecordType sTy,
                                                    mlir::Location loc) {
  assert(aggregates.count(addr) && "expected association with aggregate");
  int memberIdx = 0;
  for (auto &t : sTy.getMembers()) {
    auto fieldAddr = aggregates[addr][memberIdx];
    if (fieldAddr && mlir::isa<cir::PointerType>(t)) {
      markPsetNull(fieldAddr, loc);
    }
    memberIdx++;
  }
}

static mlir::Operation *ignoreBitcasts(mlir::Operation *op) {
  while (auto bitcast = dyn_cast<CastOp>(op)) {
    if (bitcast.getKind() != CastKind::bitcast)
      return op;
    auto b = bitcast.getSrc().getDefiningOp();
    if (!b)
      return op;
    op = b;
  }
  return op;
}

void LifetimeCheckPass::updatePointsTo(mlir::Value addr, mlir::Value data,
                                       mlir::Location loc) {

  auto getArrayFromSubscript = [&](PtrStrideOp strideOp) -> mlir::Value {
    auto castOp = strideOp.getBase().getDefiningOp<cir::CastOp>();
    if (!castOp)
      return {};
    if (castOp.getKind() != cir::CastKind::array_to_ptrdecay)
      return {};
    return castOp.getSrc();
  };

  auto dataSrcOp = data.getDefiningOp();

  if (!dataSrcOp) {
    auto blockArg = cast<BlockArgument>(data);
    if (!blockArg.getOwner()->isEntryBlock())
      return;
    getPmap()[addr].clear();
    getPmap()[addr].insert(State::getLocalValue(data));
    return;
  }

  dataSrcOp = ignoreBitcasts(dataSrcOp);

  if (auto cstOp = dyn_cast<ConstantOp>(dataSrcOp)) {
    if (aggregates.count(addr)) {
      if (auto constRecord =
              mlir::dyn_cast<cir::ConstRecordAttr>(cstOp.getValue())) {
        updatePointsToForConstRecord(addr, constRecord, loc);
        return;
      }

      if (auto zero = mlir::dyn_cast<cir::ZeroAttr>(cstOp.getValue())) {
        if (auto zeroRecordTy = dyn_cast<RecordType>(zero.getType())) {
          updatePointsToForZeroRecord(addr, zeroRecordTy, loc);
          return;
        }
      }
      return;
    }

    assert(cstOp.isNullPtr() && "other than null not implemented");
    assert(getPmap().count(addr) && "address should always be valid");
    markPsetNull(addr, loc);
    return;
  }

  if (auto allocaOp = dyn_cast<AllocaOp>(dataSrcOp)) {
    getPmap()[addr].clear();
    getPmap()[addr].insert(State::getLocalValue(allocaOp.getAddr()));
    return;
  }

  if (auto ptrStrideOp = dyn_cast<PtrStrideOp>(dataSrcOp)) {
    auto array = getArrayFromSubscript(ptrStrideOp);
    if (array) {
      getPmap()[addr].clear();
      getPmap()[addr].insert(State::getLocalValue(array));
    }
    return;
  }

  if (auto getElemOp = dyn_cast<GetElementOp>(dataSrcOp)) {
    getPmap()[addr].clear();
    getPmap()[addr].insert(State::getLocalValue(getElemOp.getBase()));
    return;
  }

  if (auto callOp = dyn_cast<CallOp>(dataSrcOp)) {
    getPmap()[addr].clear();
    getPmap()[addr].insert(State::getLocalValue(callOp.getResult()));
  }

  if (auto loadOp = dyn_cast<LoadOp>(dataSrcOp)) {
    updatePointsTo(addr, loadOp.getAddr(), loc);
    return;
  }
}

void LifetimeCheckPass::checkStore(StoreOp storeOp) {
  auto addr = storeOp.getAddr();

  if (aggregates.count(addr)) {
    auto data = storeOp.getValue();
    if (data.getDefiningOp<cir::ConstantOp>()) {
      updatePointsTo(addr, data, data.getLoc());
    }
    return;
  }

  // Check if storing a moved-from value
  checkMovedFromValue(storeOp);

  if (!ptrs.count(addr)) {
    if (currScope->localTempTasks.count(storeOp.getValue())) {
      checkCoroTaskStore(storeOp);
    } else {
      // Handle the upstream pattern where a call result (possibly with
      // lowered return type like !u8i) is stored into a task-type alloca.
      // In upstream CIR, small structs may be returned as their element type,
      // so the call returns e.g. !u8i instead of !rec_Task.
      auto callOp = storeOp.getValue().getDefiningOp<cir::CallOp>();
      if (callOp) {
        auto allocaOp = addr.getDefiningOp<cir::AllocaOp>();
        if (allocaOp && isTaskType(allocaOp.getAllocaType())) {
          // The store is writing a call result into a task alloca.
          // Treat this like checkCoroTaskStore.
          bool potentialTaintedTask = false;
          for (auto arg : callOp.getArgOperands()) {
            auto alloca = arg.getDefiningOp<cir::AllocaOp>();
            if (alloca && currScope->localValues.count(alloca)) {
              getPmap()[addr].insert(State::getLocalValue(alloca));
              potentialTaintedTask = true;
            }
          }
          if (potentialTaintedTask)
            tasks.insert(addr);
          return;
        }
      }
      checkLambdaCaptureStore(storeOp);
    }
    return;
  }

  updatePointsTo(addr, storeOp.getValue(), storeOp.getValue().getLoc());
}

void LifetimeCheckPass::checkMovedFromValue(StoreOp storeOp) {
  auto data = storeOp.getValue();
  auto loadOp = data.getDefiningOp<cir::LoadOp>();
  if (!loadOp)
    return;

  auto srcAddr = loadOp.getAddr();

  // Check if source is moved-from
  if (isValueTypeMovedFrom(srcAddr)) {
    checkPointerDeref(srcAddr, storeOp.getLoc());
    return; // Early return for consistency with other checkPointerDeref usages
  }

  // Check for rvalue initialization (e.g., int b(std::move(a)))
  // In upstream CIR, std::move generates a call that returns a pointer,
  // then we load from that pointer. The load's address would be a call
  // result, not an alloca. For plain copies like "int b = a", the load's
  // address IS an alloca. We only want to mark as moved for the rvalue case.
  auto destAddr = storeOp.getAddr();
  auto allocaOp = destAddr.getDefiningOp<cir::AllocaOp>();
  if (!allocaOp)
    return;

  // In upstream CIR, a move-init like "int b(std::move(a))" generates:
  //   %ptr = cir.call @std::move(%a_alloca) -> ptr
  //   %val = cir.load %ptr
  //   cir.store %val, %b_alloca
  // The srcAddr (%ptr) is a call result, NOT an alloca.
  // A plain copy "int b = a" generates:
  //   %val = cir.load %a_alloca
  //   cir.store %val, %b_alloca
  // The srcAddr (%a_alloca) IS an alloca.
  // Only handle the move-init case (srcAddr from call/non-alloca).
  if (srcAddr.getDefiningOp<cir::AllocaOp>())
    return; // Plain copy, not a move

  // Try to trace back to the original alloca through a std::move call
  if (auto callDef = srcAddr.getDefiningOp<cir::CallOp>()) {
    if (callDef.getNumArgOperands() > 0) {
      auto originalAddr = callDef.getArgOperand(0);
      if (!originalAddr.getDefiningOp<cir::AllocaOp>())
        return;
      if (!isValueType(originalAddr) || !getPmap().count(originalAddr))
        return;
      if (!hasInvalidState(originalAddr) && !loadOp.getIsDeref()) {
        markPointerOrValueTypeAsMovedFrom(originalAddr, storeOp.getLoc());
      }
    }
  }
}

void LifetimeCheckPass::checkLoad(LoadOp loadOp) {
  auto addr = loadOp.getAddr();
  // Only interested in checking deference on top of pointer types.
  // Note that usually the use of the invalid address happens at the
  // load or store using the result of this loadOp.
  if (!getPmap().count(addr))
    return;

  // For pointer types, only check on deref
  if (ptrs.count(addr)) {
    if (!loadOp.getIsDeref())
      return;
    checkPointerDeref(addr, loadOp.getLoc());
    return;
  }

  // For value types (not owners, not pointers), check if moved-from
  if (isValueTypeMovedFrom(addr)) {
    checkPointerDeref(addr, loadOp.getLoc());
    return; // Keep checkPointerDeref at end
  }
}

void LifetimeCheckPass::checkCopy(CopyOp copyOp) {
  auto dst = copyOp.getDst();
  if (!aggregates.count(dst))
    return;

  // Check if the source is a cir.get_global pointing to a constant
  // with a known initial value. This handles the upstream pattern where
  // aggregate initialization uses cir.copy from a global constant
  // instead of cir.store with a cir.const.
  auto src = copyOp.getSrc();
  auto getGlobalOp = src.getDefiningOp<cir::GetGlobalOp>();
  if (!getGlobalOp)
    return;

  auto globalOp =
      mlir::SymbolTable::lookupSymbolIn(theModule, getGlobalOp.getName());
  auto global = dyn_cast_or_null<cir::GlobalOp>(globalOp);
  if (!global || !global.getInitialValue())
    return;

  auto constRecord =
      mlir::dyn_cast<cir::ConstRecordAttr>(*global.getInitialValue());
  if (constRecord) {
    updatePointsToForConstRecord(dst, constRecord, copyOp.getLoc());
    return;
  }

  auto zero = mlir::dyn_cast<cir::ZeroAttr>(*global.getInitialValue());
  if (zero) {
    if (auto zeroRecordTy = dyn_cast<RecordType>(zero.getType())) {
      updatePointsToForZeroRecord(dst, zeroRecordTy, copyOp.getLoc());
      return;
    }
  }
}

void LifetimeCheckPass::emitInvalidHistory(mlir::InFlightDiagnostic &D,
                                           mlir::Value histKey,
                                           mlir::Location warningLoc,
                                           DerefStyle derefStyle) {
  assert(invalidHist.count(histKey) && "expected invalid hist");
  auto &hist = invalidHist[histKey];
  unsigned limit = opts.histLimit;

  for (int lastIdx = hist.entries.size() - 1; limit > 0 && lastIdx >= 0;
       lastIdx--, limit--) {
    auto &info = hist.entries[lastIdx];

    switch (info.style) {
    case InvalidStyle::NotInitialized: {
      D.attachNote(info.loc) << "uninitialized here";
      break;
    }
    case InvalidStyle::EndOfScope: {
      if (tasks.count(histKey)) {
        llvm::StringRef resource = "resource";
        if (auto allocaOp = info.val->getDefiningOp<cir::AllocaOp>()) {
          if (isLambdaType(allocaOp.getAllocaType()))
            resource = "lambda";
        }
        D.attachNote((*info.val).getLoc())
            << "coroutine bound to " << resource << " with expired lifetime";
        D.attachNote(info.loc) << "at the end of scope or full-expression";
      } else if (derefStyle == DerefStyle::RetLambda) {
        assert(currFunc && "expected function");
        llvm::StringRef parent = currFunc->getLambda() ? "lambda" : "function";
        D.attachNote(info.val->getLoc())
            << "declared here but invalid after enclosing " << parent
            << " ends";
      } else {
        auto outOfScopeVarName = getVarNameFromValue(*info.val);
        D.attachNote(info.loc) << "pointee '" << outOfScopeVarName
                               << "' invalidated at end of scope";
      }
      break;
    }
    case InvalidStyle::NonConstUseOfOwner: {
      D.attachNote(info.loc) << "invalidated by non-const use of owner type";
      break;
    }
    case InvalidStyle::MovedFrom: {
      D.attachNote(info.loc) << "moved here via std::move or rvalue reference";
      break;
    }
    default:
      llvm_unreachable("unknown history style");
    }
  }
}

bool LifetimeCheckPass::checkPointerDeref(mlir::Value addr, mlir::Location loc,
                                          DerefStyle derefStyle) {
  bool hasInvalid = getPmap()[addr].count(State::getInvalid());
  bool hasNullptr = getPmap()[addr].count(State::getNullPtr());

  auto emitPsetRemark = [&] {
    llvm::SmallString<128> psetStr;
    llvm::raw_svector_ostream Out(psetStr);
    printPset(getPmap()[addr], Out);
    emitRemark(loc) << "pset => " << Out.str();
  };

  bool psetRemarkEmitted = false;
  if (opts.emitRemarkPsetAlways()) {
    emitPsetRemark();
    psetRemarkEmitted = true;
  }

  // Only skip temporaries in use-after-move paths — it's possible this can
  // be generalized, but currently only tested for use-after-move.
  if (derefStyle == DerefStyle::UseAfterMove && isSkippableTemporary(addr))
    return false;

  // 2.4.2 - On every dereference of a Pointer p, enforce that p is valid.
  if (!hasInvalid && !hasNullptr)
    return false; // No error

  if (derefStyle == DerefStyle::IndirectCallParam && !hasInvalid)
    return false; // No error

  // For aggregate pointer members passed by reference (CallParam), a null-only
  // pset is normal (e.g. zero-initialized struct fields). Only warn if the
  // member has invalid state.
  if (derefStyle == DerefStyle::CallParam && !hasInvalid &&
      addr.getDefiningOp<cir::GetMemberOp>())
    return false;

  // Do not emit the same warning twice or more (check AFTER we know there's an
  // error)
  if (emittedDiagnostics.count(loc))
    return true; // Already reported

  auto varName = getVarNameFromValue(addr);
  auto D = emitWarning(loc);
  emittedDiagnostics.insert(loc);

  bool isValueType = this->isValueType(
      addr); // Use helper method, keep lowerCamel style for locals

  if (tasks.count(addr))
    D << "use of coroutine '" << varName << "' with dangling reference";
  else if (derefStyle == DerefStyle::RetLambda)
    D << "returned lambda captures local variable";
  else if (derefStyle == DerefStyle::CallParam ||
           derefStyle == DerefStyle::IndirectCallParam) {
    bool isAgg = addr.getDefiningOp<cir::GetMemberOp>();
    D << "passing ";
    if (!isAgg)
      D << (isValueType ? "moved-from value" : "invalid pointer");
    else
      D << "aggregate containing invalid pointer member";
    D << " '" << varName << "'";
  } else
    D << "use of " << (isValueType ? "moved-from" : "invalid")
      << (isValueType ? " value '" : " pointer '") << varName << "'";

  if (hasInvalid && opts.emitHistoryInvalid())
    emitInvalidHistory(D, addr, loc, derefStyle);

  if (hasNullptr && opts.emitHistoryNull()) {
    assert(pmapNullHist.count(addr) && "expected nullptr hist");
    auto &note = pmapNullHist[addr];
    D.attachNote(*note) << "'nullptr' invalidated here";
  }

  if (!psetRemarkEmitted && opts.emitRemarkPsetInvalid())
    emitPsetRemark();

  return true; // Error was reported
}

const clang::CXXMethodDecl *LifetimeCheckPass::getMethodDecl(ModuleOp mod,
                                                             CallOp callOp) {
  if (!callOp.getCallee())
    return nullptr;
  if (!astCtx || !*astCtx)
    return nullptr;

  llvm::StringRef mangledName = *callOp.getCallee();
  auto calleeFuncOp = callOp.getDirectCallee(mod);
  if (!calleeFuncOp || calleeFuncOp.getBuiltin())
    return nullptr;

  // Check cache first.
  auto cacheIt = methodDeclCache.find(mangledName);
  if (cacheIt != methodDeclCache.end())
    return cacheIt->second;

  // Walk the AST to find the FunctionDecl with matching mangled name.
  auto &ctx = **astCtx;
  std::unique_ptr<clang::MangleContext> mangleCtx(ctx.createMangleContext());

  // Search all decls in the TU for the matching method.
  std::function<const clang::CXXMethodDecl *(const clang::DeclContext *)>
      findMethod =
          [&](const clang::DeclContext *DC) -> const clang::CXXMethodDecl * {
    for (auto *D : DC->decls()) {
      if (auto *MD = dyn_cast<clang::CXXMethodDecl>(D)) {
        if (isa<clang::CXXConstructorDecl>(MD) ||
            isa<clang::CXXDestructorDecl>(MD))
          continue;
        std::string name;
        llvm::raw_string_ostream OS(name);
        mangleCtx->mangleName(clang::GlobalDecl(MD), OS);
        if (name == mangledName) {
          methodDeclCache[mangledName] = MD;
          return MD;
        }
      }
      if (auto *RD = dyn_cast<clang::CXXRecordDecl>(D))
        if (auto *found = findMethod(RD))
          return found;
      if (auto *NSD = dyn_cast<clang::NamespaceDecl>(D))
        if (auto *found = findMethod(NSD))
          return found;
    }
    return nullptr;
  };

  auto *result = findMethod(ctx.getTranslationUnitDecl());
  methodDeclCache[mangledName] = result;
  return result;
}

mlir::Value LifetimeCheckPass::getThisParamPointerCategory(CallOp callOp) {
  auto args = callOp.getArgOperands();
  if (args.empty())
    return {};
  auto thisptr = args[0];
  if (ptrs.count(thisptr))
    return thisptr;
  if (auto loadOp = thisptr.getDefiningOp<cir::LoadOp>()) {
    if (ptrs.count(loadOp.getAddr()))
      return loadOp.getAddr();
  }
  return {};
}

mlir::Value LifetimeCheckPass::getThisParamOwnerCategory(CallOp callOp) {
  auto args = callOp.getArgOperands();
  if (args.empty())
    return {};
  auto thisptr = args[0];
  if (owners.count(thisptr))
    return thisptr;
  if (auto loadOp = thisptr.getDefiningOp<cir::LoadOp>()) {
    if (owners.count(loadOp.getAddr()))
      return loadOp.getAddr();
  }
  return {};
}

void LifetimeCheckPass::checkMoveAssignment(CallOp callOp,
                                            const clang::CXXMethodDecl *m) {
  // Move assignments are already marked via CXXAssignAttr with AssignKind::Move
  // (set during CIRGen). This attribute is part of CIR_CXXSpecialMemberAttr
  // attached to FuncOp, which also includes move constructors (CXXCtorAttr with
  // CtorKind::Move). See checkCall for dispatch logic.

  // MyPointer::operator=(MyPointer&&)(%dst, %src)
  // or
  // MyOwner::operator=(MyOwner&&)(%dst, %src)
  auto dst = getThisParamPointerCategory(callOp);
  auto args = callOp.getArgOperands();
  auto src = args[1];

  if (dst && ptrs.count(src)) {
    getPmap()[dst] = getPmap()[src];

    // 2.4.2 - It is an error to use a moved-from object.
    // To that intent we mark src's pset with invalid.
    markPointerOrValueTypeAsMovedFrom(src, callOp.getLoc());
    return;
  }

  dst = getThisParamOwnerCategory(callOp);
  if (dst && owners.count(src)) {
    checkNonConstUseOfOwner(dst, callOp.getLoc());

    // 2.4.2 - It is an error to use a moved-from object.
    markOwnerAsMovedFrom(src, callOp.getLoc());
  }
}

void LifetimeCheckPass::checkMoveInCallArgs(CallOp callOp) {
  if (!callOp.getCallee())
    return;

  auto calleeFuncOp = callOp.getDirectCallee(theModule);
  if (!calleeFuncOp)
    return;

  if (!astCtx || !*astCtx)
    return;

  // Walk the AST to find the FunctionDecl for this callee
  auto &ctx = **astCtx;
  std::unique_ptr<clang::MangleContext> mangleCtx(ctx.createMangleContext());
  llvm::StringRef mangledName = *callOp.getCallee();

  const clang::FunctionDecl *funcDecl = nullptr;

  // Search all decls in the TU for the matching function
  std::function<const clang::FunctionDecl *(const clang::DeclContext *)>
      findFunc =
          [&](const clang::DeclContext *DC) -> const clang::FunctionDecl * {
    for (auto *D : DC->decls()) {
      if (auto *FD = dyn_cast<clang::FunctionDecl>(D)) {
        if (isa<clang::CXXConstructorDecl>(FD) ||
            isa<clang::CXXDestructorDecl>(FD))
          continue;
        std::string name;
        llvm::raw_string_ostream OS(name);
        mangleCtx->mangleName(clang::GlobalDecl(FD), OS);
        if (name == mangledName)
          return FD;
      }
      if (auto *FTD = dyn_cast<clang::FunctionTemplateDecl>(D)) {
        for (auto *spec : FTD->specializations()) {
          if (isa<clang::CXXConstructorDecl>(spec) ||
              isa<clang::CXXDestructorDecl>(spec))
            continue;
          std::string name;
          llvm::raw_string_ostream OS(name);
          mangleCtx->mangleName(clang::GlobalDecl(spec), OS);
          if (name == mangledName)
            return spec;
        }
      }
      if (auto *RD = dyn_cast<clang::CXXRecordDecl>(D))
        if (auto *found = findFunc(RD))
          return found;
      if (auto *NSD = dyn_cast<clang::NamespaceDecl>(D))
        if (auto *found = findFunc(NSD))
          return found;
    }
    return nullptr;
  };

  funcDecl = findFunc(ctx.getTranslationUnitDecl());
  if (!funcDecl)
    return;

  // Skip CXX methods - they are handled separately by the owner/pointer
  // class method dispatch in checkCall (checkOperators, checkMoveAssignment,
  // etc.). This matches the incubator behavior where checkMoveInCallArgs
  // only processes ASTFunctionDeclAttr (not ASTCXXMethodDeclAttr).
  if (isa<clang::CXXMethodDecl>(funcDecl))
    return;

  unsigned numParams = funcDecl->getNumParams();
  unsigned numArgs = callOp.getNumArgOperands();

  // Note: Number of parameters and arguments may differ due to:
  // 1. Default parameters: Function may have more parameters than provided
  // arguments
  // 2. Variadic functions: Function may accept more arguments than declared
  // parameters We check arguments up to min(numArgs, numParams) to handle both
  // cases safely.

  // Check each argument against its parameter type
  for (unsigned i = 0; i < std::min(numArgs, numParams); ++i) {
    checkArgForRValueRef(callOp, i, funcDecl);
  }
}

void LifetimeCheckPass::checkArgForRValueRef(
    CallOp callOp, unsigned argIdx, const clang::FunctionDecl *funcDecl) {
  // Check if parameter is an rvalue reference
  if (argIdx >= funcDecl->getNumParams())
    return;
  auto *param = funcDecl->getParamDecl(argIdx);
  if (!param->getType()->isRValueReferenceType())
    return;

  auto arg = callOp.getArgOperand(argIdx);

  // Case 1: LoadOp (by-value argument loaded from memory)
  // Detects use of moved-from VALUE TYPES passed by rvalue ref.
  //   FAIL: int a = 42; int b = std::move(a);
  //         foo(std::move(a)); // 'a' is moved-from
  //   OK:   int a = 42;
  //         foo(std::move(a)); // 'a' valid before move
  if (auto loadOp = arg.getDefiningOp<cir::LoadOp>()) {
    auto srcAddr = loadOp.getAddr();
    if (isValueTypeMovedFrom(srcAddr)) {
      checkPointerDeref(srcAddr, callOp.getLoc(), DerefStyle::UseAfterMove);
    }
    return;
  }

  // In upstream CIR, std::move is called as a separate function:
  //   %ptr = cir.call @_ZSt4moveIiEOT_RS0_(%alloca) -> ptr
  //   cir.call @consume_int(%ptr) -> void
  // So the arg to consume_int is the result of std::move, which is a pointer.
  // We need to trace back through std::move to find the original alloca.
  auto traceToOriginalAddr = [](mlir::Value v) -> mlir::Value {
    // If it's already an alloca, use it directly
    if (v.getDefiningOp<cir::AllocaOp>())
      return v;

    // If it's a call result (from std::move), look at the call's first arg
    if (auto callDef = v.getDefiningOp<cir::CallOp>()) {
      if (callDef.getNumArgOperands() > 0) {
        auto firstArg = callDef.getArgOperand(0);
        if (firstArg.getDefiningOp<cir::AllocaOp>())
          return firstArg;
      }
    }

    return {};
  };

  mlir::Value addr = traceToOriginalAddr(arg);
  if (!addr)
    return;

  if (!getPmap().count(addr))
    return;

  // Case 2: Owner types (unique_ptr, vector, string, etc.)
  // Detects use of moved-from OWNER objects.
  if (owners.count(addr)) {
    if (checkPointerDeref(addr, callOp.getLoc(), DerefStyle::UseAfterMove))
      return; // Already reported error
    markOwnerAsMovedFrom(addr, callOp.getLoc());
    return;
  }

  // Case 3: Pointer types (T*, T&, iterators)
  // Detects use of UNINITIALIZED/INVALID pointers (not null).
  if (ptrs.count(addr)) {
    if (getPmap()[addr].count(State::getInvalid())) {
      checkPointerDeref(addr, callOp.getLoc(), DerefStyle::UseAfterMove);
      return;
    }
    markPointerOrValueTypeAsMovedFrom(addr, callOp.getLoc());
    return;
  }

  // Case 4: Value types (structs, classes without Owner semantics)
  // Detects use of INVALID value type objects.
  if (getPmap()[addr].count(State::getInvalid())) {
    checkPointerDeref(addr, callOp.getLoc(), DerefStyle::UseAfterMove);
    return;
  }
  markPointerOrValueTypeAsMovedFrom(addr, callOp.getLoc());
}

void LifetimeCheckPass::checkCopyAssignment(CallOp callOp,
                                            const clang::CXXMethodDecl *m) {
  auto dst = getThisParamOwnerCategory(callOp);
  auto args = callOp.getArgOperands();
  auto src = args[1];

  if (dst && owners.count(src))
    return checkNonConstUseOfOwner(dst, callOp.getLoc());

  dst = getThisParamPointerCategory(callOp);
  if (dst && ptrs.count(src)) {
    getPmap()[dst] = getPmap()[src];
    return;
  }
}

bool LifetimeCheckPass::isCtorInitPointerFromOwner(CallOp callOp) {
  auto args = callOp.getArgOperands();
  if (args.size() < 2)
    return false;

  auto ptr = getThisParamPointerCategory(callOp);
  auto owner = args[1];

  if (ptr && owners.count(owner))
    return true;

  return false;
}

void LifetimeCheckPass::checkCtor(CallOp callOp, cir::CtorKind ctorKind) {
  if (ctorKind == cir::CtorKind::Default) {
    auto addr = getThisParamOwnerCategory(callOp);
    if (addr)
      return;

    addr = getThisParamPointerCategory(callOp);
    if (!addr)
      return;

    if (!addr.getDefiningOp<cir::AllocaOp>())
      return;

    markPsetNull(addr, callOp.getLoc());
    return;
  }

  if (ctorKind == cir::CtorKind::Copy) {
    llvm_unreachable("NYI");
  }

  // Move constructor - mark source as moved-from
  checkMoveCtor(callOp, ctorKind);

  if (isCtorInitPointerFromOwner(callOp)) {
    auto addr = getThisParamPointerCategory(callOp);
    assert(addr && "expected pointer category");
    auto args = callOp.getArgOperands();
    auto owner = args[1];
    getPmap()[addr].clear();
    getPmap()[addr].insert(State::getOwnedBy(owner));
    return;
  }
}

void LifetimeCheckPass::checkOperators(CallOp callOp,
                                       const clang::CXXMethodDecl *m) {
  auto addr = getThisParamOwnerCategory(callOp);

  if (addr) {
    // Smart pointers need special handling even for const methods
    // (operator*, operator-> are const but unsafe when null)
    if (isSmartPointerType(addr.getType(), this, IsSmartPointerTyCache)) {
      // Get declaration name (works for operators too)
      std::string methodName;
      if (m)
        methodName = m->getDeclName().getAsString();

      if (isSmartPointerSafeMethod(methodName))
        return; // Safe operation

      // Unsafe: operator*, operator-> will fall through to deref check
      checkPointerDeref(addr, callOp.getLoc());
      return;
    }

    // const access to the owner is fine (for non-smart-pointers).
    if (m && m->isConst())
      return;

    // TODO: this is a place where we can hook in some idiom recocgnition
    // so we don't need to use actual source code annotation to make assumptions
    // on methods we understand and know to behave nicely.
    return checkNonConstUseOfOwner(addr, callOp.getLoc());
  }

  addr = getThisParamPointerCategory(callOp);
  if (addr) {
    checkPointerDeref(addr, callOp.getLoc());
    return;
  }
}

mlir::Value
LifetimeCheckPass::getNonConstUseOfOwner(CallOp callOp,
                                         const clang::CXXMethodDecl *m) {
  if (m && m->isConst())
    return {};
  return getThisParamOwnerCategory(callOp);
}

void LifetimeCheckPass::checkNonConstUseOfOwner(mlir::Value ownerAddr,
                                                mlir::Location loc) {
  kill(State::getOwnedBy(ownerAddr), InvalidStyle::NonConstUseOfOwner, loc);
  incOwner(ownerAddr);
  return;
}

void LifetimeCheckPass::markOwnerAsMovedFrom(mlir::Value addr,
                                             mlir::Location loc) {
  // Don't mark temporaries as moved-from - they're about to be destroyed
  if (isSkippableTemporary(addr))
    return;

  // All owner types are marked invalid after move. Safe operations on specific
  // owner types (e.g., smart pointer get/reset) are handled separately in
  // isSmartPointerSafeMethod.

  // Kill all pointers that point to this owner (propagate invalidation)
  kill(State::getOwnedBy(addr), InvalidStyle::MovedFrom, loc);

  // Mark the owner itself as invalid
  markPsetInvalid(addr, InvalidStyle::MovedFrom, loc);

  // Increment owner generation so new pointers don't alias the old state
  incOwner(addr);
}

bool LifetimeCheckPass::isValueType(mlir::Value addr) {
  return !owners.count(addr) && !ptrs.count(addr);
}

bool LifetimeCheckPass::hasInvalidState(mlir::Value addr) {
  return getPmap().count(addr) && getPmap()[addr].count(State::getInvalid());
}

bool LifetimeCheckPass::isValueTypeMovedFrom(mlir::Value addr) {
  return isValueType(addr) && hasInvalidState(addr);
}

void LifetimeCheckPass::markPointerOrValueTypeAsMovedFrom(mlir::Value addr,
                                                          mlir::Location loc) {
  // Don't mark temporaries as moved-from
  if (isSkippableTemporary(addr))
    return;

  markPsetInvalid(addr, InvalidStyle::MovedFrom, loc);
}

// Returns true if the method can be safely called on a moved-from (null)
// smart pointer.
//
// Safe methods for std::unique_ptr and std::shared_ptr:
// - get(): Returns the raw pointer (nullptr for moved-from pointer)
// - release(): Releases ownership and returns pointer (nullptr if empty)
// - reset(): Resets to a new pointer (handles nullptr gracefully)
// - operator bool: Checks if pointer is non-null (returns false if moved-from)
//
// Unsafe methods that require valid (non-null) pointers:
// - operator*: Dereferences pointer (undefined behavior if null)
// - operator->: Member access through pointer (undefined behavior if null)
//
// This is specific to std::unique_ptr and std::shared_ptr because they have
// well-defined null-after-move semantics. Other owner types may have different
// contracts for moved-from state.
bool LifetimeCheckPass::isSmartPointerSafeMethod(llvm::StringRef methodName) {
  return methodName == "get" || methodName == "release" ||
         methodName == "reset" || methodName == "operator bool";
}

void LifetimeCheckPass::checkMoveCtor(CallOp callOp,
                                      cir::CtorKind ctorKind) {
  // Move constructors are already marked via CXXCtorAttr with CtorKind::Move
  // (set during CIRGen in CIRGenModule.cpp). This attribute is part of
  // CIR_CXXSpecialMemberAttr attached to FuncOp, which also includes move
  // assignments (CXXAssignAttr with AssignKind::Move). See checkCall for
  // dispatch logic.
  if (ctorKind != cir::CtorKind::Move)
    return;

  // Get the source parameter (second argument after 'this')
  if (callOp.getNumOperands() < 2)
    return;

  auto srcArg = callOp.getArgOperand(1);

  // In upstream CIR, std::move is called as a separate function, so
  // the source arg may be:
  // 1. The result of a std::move call (a pointer to the original)
  // 2. A direct alloca (incubator style, where std::move is folded)
  // 3. A load from an alloca
  // We need to trace back to the original alloca.
  mlir::Value src = srcArg;
  if (auto callDef = srcArg.getDefiningOp<cir::CallOp>()) {
    // Trace through std::move: the first arg is the original address
    if (callDef.getNumArgOperands() > 0)
      src = callDef.getArgOperand(0);
  } else if (auto loadOp = srcArg.getDefiningOp<cir::LoadOp>()) {
    src = loadOp.getAddr();
  }

  // Check if this is an Owner type move constructor
  auto addr = getThisParamOwnerCategory(callOp);
  if (addr && owners.count(src)) {
    markOwnerAsMovedFrom(src, callOp.getLoc());
    return;
  }

  // Check if this is a Pointer type move constructor
  addr = getThisParamPointerCategory(callOp);
  if (addr && ptrs.count(src)) {
    markPointerOrValueTypeAsMovedFrom(src, callOp.getLoc());
    return;
  }
}

void LifetimeCheckPass::checkForOwnerAndPointerArguments(CallOp callOp,
                                                         unsigned firstArgIdx) {
  auto args = callOp.getArgOperands();
  auto numOperands = args.size();
  if (firstArgIdx >= numOperands)
    return;

  llvm::SmallSetVector<mlir::Value, 8> ownersToInvalidate, ptrsToDeref;
  for (unsigned i = firstArgIdx, e = numOperands; i != e; ++i) {
    auto arg = args[i];
    if (owners.count(arg))
      ownersToInvalidate.insert(arg);
    if (ptrs.count(arg))
      ptrsToDeref.insert(arg);
    if (tasks.count(arg))
      ptrsToDeref.insert(arg);
    if (aggregates.count(arg)) {
      int memberIdx = 0;
      auto sTy = mlir::dyn_cast<RecordType>(
          mlir::cast<PointerType>(arg.getType()).getPointee());
      assert(sTy && "expected record type");
      for (auto m : sTy.getMembers()) {
        auto ptrMemberAddr = aggregates[arg][memberIdx];
        if (mlir::isa<PointerType>(m) && ptrMemberAddr) {
          ptrsToDeref.insert(ptrMemberAddr);
        }
        memberIdx++;
      }
    }
  }

  for (auto o : ownersToInvalidate)
    checkNonConstUseOfOwner(o, callOp.getLoc());
  for (auto p : ptrsToDeref)
    checkPointerDeref(p, callOp.getLoc(),
                      callOp.getCallee() ? DerefStyle::CallParam
                                         : DerefStyle::IndirectCallParam);
}

void LifetimeCheckPass::checkOtherMethodsAndFunctions(
    CallOp callOp, const clang::CXXMethodDecl *m) {
  unsigned firstArgIdx = 0;
  auto args = callOp.getArgOperands();

  if (m && !args.empty() && !tasks.count(args[firstArgIdx]))
    firstArgIdx++;
  checkForOwnerAndPointerArguments(callOp, firstArgIdx);
}

bool LifetimeCheckPass::isOwnerOrPointerClassMethod(
    CallOp callOp, const clang::CXXMethodDecl *m) {
  // If we have AST info and it's a static method, it's not a class method.
  // If we don't have AST info (m is null), check if the callee has a
  // cxx_special_member attribute (ctor/dtor/assignment). If not, treat it
  // as a regular function - this avoids false positives on free functions
  // whose first argument happens to be loaded from a tracked pointer.
  if (!m) {
    auto calleeFuncOp = callOp.getDirectCallee(theModule);
    if (!calleeFuncOp || !calleeFuncOp.isCXXSpecialMemberFunction())
      return false;
  } else if (m->isStatic()) {
    return false;
  }
  // Check if first arg is this pointer to an owner or pointer class.
  return getThisParamPointerCategory(callOp) ||
         getThisParamOwnerCategory(callOp);
}

bool LifetimeCheckPass::isLambdaType(mlir::Type ty) {
  if (IsLambdaTyCache.count(ty))
    return IsLambdaTyCache[ty];

  IsLambdaTyCache[ty] = false;
  auto recordTy = mlir::dyn_cast<cir::RecordType>(ty);
  if (!recordTy)
    return false;

  // First check if the name starts with "anon." which is how CIR names
  // lambda types.
  auto name = recordTy.getName();
  if (name && name.getValue().starts_with("anon.")) {
    IsLambdaTyCache[ty] = true;
    return true;
  }

  // Also try to look up the RecordDecl from the AST context.
  if (astCtx && *astCtx) {
    auto *RD = getRecordDecl(recordTy);
    if (RD) {
      if (auto *CRD = dyn_cast<clang::CXXRecordDecl>(RD)) {
        if (CRD->isLambda()) {
          IsLambdaTyCache[ty] = true;
          return true;
        }
      }
    }
  }

  return IsLambdaTyCache[ty];
}

bool LifetimeCheckPass::isTaskType(mlir::Type ty) {
  if (IsTaskTyCache.count(ty))
    return IsTaskTyCache[ty];

  bool result = [&] {
    auto taskTy = mlir::dyn_cast<cir::RecordType>(ty);
    if (!taskTy)
      return false;
    if (!astCtx || !*astCtx)
      return false;
    auto *RD = getRecordDecl(taskTy);
    if (!RD)
      return false;
    auto *CRD = dyn_cast<clang::CXXRecordDecl>(RD);
    if (!CRD)
      return false;
    // Check if the type has a promise_type member.
    for (auto *D : CRD->decls()) {
      if (auto *TD = dyn_cast<clang::TypedefNameDecl>(D)) {
        if (TD->getName() == "promise_type")
          return true;
      }
      if (auto *UD = dyn_cast<clang::UsingDecl>(D)) {
        if (UD->getName() == "promise_type")
          return true;
      }
      // promise_type can also be a nested struct/class.
      if (auto *RD = dyn_cast<clang::RecordDecl>(D)) {
        if (RD->getIdentifier() && RD->getName() == "promise_type")
          return true;
      }
    }
    return false;
  }();

  IsTaskTyCache[ty] = result;
  return result;
}

bool LifetimeCheckPass::isTaskType(mlir::Value taskVal) {
  return isTaskType(taskVal.getType());
}

void LifetimeCheckPass::trackCallToCoroutine(CallOp callOp) {
  if (auto calleeFuncOp = callOp.getDirectCallee(theModule)) {
    if (calleeFuncOp.getCoroutine() ||
        (calleeFuncOp.isDeclaration() && callOp->getNumResults() > 0 &&
         isTaskType(callOp->getResult(0)))) {
      currScope->localTempTasks.insert(callOp->getResult(0));
    }
    return;
  }
  if (callOp->getNumResults() > 0 && isTaskType(callOp->getResult(0))) {
    currScope->localTempTasks.insert(callOp->getResult(0));
  }
}

void LifetimeCheckPass::checkCall(CallOp callOp) {
  auto args = callOp.getArgOperands();
  if (args.empty())
    return;

  trackCallToCoroutine(callOp);

  // Track moves via rvalue reference parameters for Value types
  checkMoveInCallArgs(callOp);

  // Handle smart pointer methods specially. getMethodDecl may not find
  // methods in template class specializations (e.g., std::unique_ptr<int>
  // operator*), so we detect smart pointer methods here using the callee's
  // first argument type and the callee function name.
  if (auto fnName = callOp.getCallee()) {
    auto thisAddr = getThisParamOwnerCategory(callOp);
    if (thisAddr &&
        isSmartPointerType(thisAddr.getType(), this, IsSmartPointerTyCache)) {
      auto calleeFuncOp = callOp.getDirectCallee(theModule);
      // Skip special members (ctors, dtors, assignments) - handled below
      if (!calleeFuncOp || !calleeFuncOp.isCXXSpecialMemberFunction()) {
        // Find the method name by walking ClassTemplateDecl specializations
        std::string methodName;
        if (astCtx && *astCtx) {
          auto &ctx = **astCtx;
          std::unique_ptr<clang::MangleContext> mangleCtx(
              ctx.createMangleContext());
          llvm::StringRef mangledName = *fnName;
          std::function<const clang::CXXMethodDecl *(
              const clang::DeclContext *)>
              findSmartPtrMethod =
                  [&](const clang::DeclContext *DC)
              -> const clang::CXXMethodDecl * {
            for (auto *D : DC->decls()) {
              if (auto *CTD = dyn_cast<clang::ClassTemplateDecl>(D)) {
                for (auto *spec : CTD->specializations()) {
                  for (auto *SD : spec->decls()) {
                    auto *MD = dyn_cast<clang::CXXMethodDecl>(SD);
                    if (!MD || isa<clang::CXXConstructorDecl>(MD) ||
                        isa<clang::CXXDestructorDecl>(MD))
                      continue;
                    std::string name;
                    llvm::raw_string_ostream OS(name);
                    mangleCtx->mangleName(clang::GlobalDecl(MD), OS);
                    if (name == mangledName)
                      return MD;
                  }
                }
              }
              if (auto *NSD = dyn_cast<clang::NamespaceDecl>(D))
                if (auto *found = findSmartPtrMethod(NSD))
                  return found;
            }
            return nullptr;
          };
          if (auto *MD = findSmartPtrMethod(ctx.getTranslationUnitDecl()))
            methodName = MD->getDeclName().getAsString();
        }
        if (isSmartPointerSafeMethod(methodName))
          return; // Safe operation on smart pointer
        // Unsafe operation on smart pointer - check for deref
        checkPointerDeref(thisAddr, callOp.getLoc());
        return;
      }
    }
  }

  const clang::CXXMethodDecl *methodDecl = getMethodDecl(theModule, callOp);
  if (!isOwnerOrPointerClassMethod(callOp, methodDecl))
    return checkOtherMethodsAndFunctions(callOp, methodDecl);

  // From this point on only owner and pointer class methods handling,
  // starting from special methods.
  auto calleeFuncOp = callOp.getDirectCallee(theModule);
  if (calleeFuncOp && calleeFuncOp.isCXXSpecialMemberFunction()) {
    auto cxxSpecialMember = calleeFuncOp.getCxxSpecialMemberAttr();
    if (auto cxxCtor = dyn_cast<cir::CXXCtorAttr>(cxxSpecialMember))
      return checkCtor(callOp, cxxCtor.getCtorKind());

    if (auto cxxAssign = dyn_cast<cir::CXXAssignAttr>(cxxSpecialMember)) {
      switch (cxxAssign.getAssignKind()) {
      case cir::AssignKind::Move:
        return checkMoveAssignment(callOp, methodDecl);
      case cir::AssignKind::Copy:
        return checkCopyAssignment(callOp, methodDecl);
      }
    }
  }
  if (methodDecl && methodDecl->isOverloadedOperator())
    return checkOperators(callOp, methodDecl);

  if (auto owner = getNonConstUseOfOwner(callOp, methodDecl)) {
    return checkNonConstUseOfOwner(owner, callOp.getLoc());
  }

  auto addr = getThisParamPointerCategory(callOp);
  if (addr)
    checkPointerDeref(addr, callOp.getLoc());
}

void LifetimeCheckPass::checkOperation(Operation *op) {
  if (isa<::mlir::ModuleOp>(op)) {
    theModule = cast<::mlir::ModuleOp>(op);
    for (Region &region : op->getRegions())
      checkRegion(region);
    return;
  }

  if (isa<ScopeOp>(op)) {
    LexicalScopeContext lexScope{op};
    LexicalScopeGuard scopeGuard{*this, &lexScope};
    for (Region &region : op->getRegions())
      checkRegion(region);
    return;
  }

  if (auto fnOp = dyn_cast<cir::FuncOp>(op))
    return checkFunc(fnOp);
  if (auto ifOp = dyn_cast<IfOp>(op))
    return checkIf(ifOp);
  if (auto switchOp = dyn_cast<SwitchOp>(op))
    return checkSwitch(switchOp);
  if (auto loopOp = dyn_cast<LoopOpInterface>(op))
    return checkLoop(loopOp);
  if (auto allocaOp = dyn_cast<AllocaOp>(op))
    return checkAlloca(allocaOp);
  if (auto storeOp = dyn_cast<StoreOp>(op))
    return checkStore(storeOp);
  if (auto copyOp = dyn_cast<CopyOp>(op))
    return checkCopy(copyOp);
  if (auto loadOp = dyn_cast<LoadOp>(op))
    return checkLoad(loadOp);
  if (auto callOp = dyn_cast<CallOp>(op))
    return checkCall(callOp);
  if (auto awaitOp = dyn_cast<AwaitOp>(op))
    return checkAwait(awaitOp);
  if (auto returnOp = dyn_cast<ReturnOp>(op))
    return checkReturn(returnOp);
}

void LifetimeCheckPass::runOnOperation() {
  assert(astCtx && "Missing ASTContext, please construct with the right ctor");
  opts.parseOptions(*this);
  Operation *op = getOperation();
  checkOperation(op);
}

std::unique_ptr<Pass> mlir::createLifetimeCheckPass() {
  return std::make_unique<LifetimeCheckPass>();
}

std::unique_ptr<Pass> mlir::createLifetimeCheckPass(clang::ASTContext *astCtx) {
  auto lifetime = std::make_unique<LifetimeCheckPass>();
  lifetime->setASTContext(astCtx);
  return std::move(lifetime);
}

std::unique_ptr<Pass>
mlir::createLifetimeCheckPass(ArrayRef<llvm::StringRef> remark,
                              ArrayRef<llvm::StringRef> hist,
                              unsigned hist_limit, clang::ASTContext *astCtx) {
  auto lifetime = std::make_unique<LifetimeCheckPass>();
  lifetime->setASTContext(astCtx);
  lifetime->opts.parseOptions(remark, hist, hist_limit);
  return std::move(lifetime);
}

//===----------------------------------------------------------------------===//
// Dump & print helpers
//===----------------------------------------------------------------------===//

void LifetimeCheckPass::LexicalScopeContext::dumpLocalValues() {
  llvm::errs() << "Local values: { ";
  for (auto value : localValues) {
    llvm::errs() << getVarNameFromValue(value);
    llvm::errs() << ", ";
  }
  llvm::errs() << "}\n";
}

void LifetimeCheckPass::State::dump(llvm::raw_ostream &OS, int ownedGen) {
  switch (val.getInt()) {
  case Invalid:
    OS << "invalid";
    break;
  case NullPtr:
    OS << "nullptr";
    break;
  case Global:
    OS << "global";
    break;
  case LocalValue:
    OS << getVarNameFromValue(val.getPointer());
    break;
  case OwnedBy:
    ownedGen++;
    OS << getVarNameFromValue(val.getPointer()) << "__" << ownedGen << "'";
    break;
  default:
    llvm_unreachable("Not handled");
  }
}

void LifetimeCheckPass::printPset(PSetType &pset, llvm::raw_ostream &OS) {
  OS << "{ ";
  auto size = pset.size();
  for (auto s : pset) {
    int ownerGen = 0;
    if (s.isOwnedBy())
      ownerGen = owners[s.getData()];
    s.dump(OS, ownerGen);
    size--;
    if (size > 0)
      OS << ", ";
  }
  OS << " }";
}

void LifetimeCheckPass::dumpCurrentPmap() { dumpPmap(*currPmap); }

void LifetimeCheckPass::dumpPmap(PMapType &pmap) {
  llvm::errs() << "pmap {\n";
  int entry = 0;
  for (auto &mapEntry : pmap) {
    llvm::errs() << "  " << entry << ": " << getVarNameFromValue(mapEntry.first)
                 << "  " << "=> ";
    printPset(mapEntry.second);
    llvm::errs() << "\n";
    entry++;
  }
  llvm::errs() << "}\n";
}
