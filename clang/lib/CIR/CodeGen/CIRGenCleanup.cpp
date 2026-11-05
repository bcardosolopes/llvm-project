//===--- CIRGenCleanup.cpp - Bookkeeping and code emission for cleanups ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code dealing with the IR generation for cleanups
// and related information.
//
// A "cleanup" is a piece of code which needs to be executed whenever
// control transfers out of a particular scope.  This can be
// conditionalized to occur only on exceptional control flow, only on
// normal control flow, or both.
//
//===----------------------------------------------------------------------===//

#include "CIRGenCleanup.h"
#include "CIRGenFunction.h"

#include "clang/CIR/MissingFeatures.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ScopeExit.h"

#include "mlir/IR/Dominance.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

using namespace clang;
using namespace clang::CIRGen;

//===----------------------------------------------------------------------===//
// CIRGenFunction cleanup related
//===----------------------------------------------------------------------===//

// Forward declaration — defined below popCleanupBlock.
static mlir::Block *createNormalEntry(CIRGenFunction &cgf,
                                      EHCleanupScope &scope);

/// Build a unconditional branch to the lexical scope cleanup block
/// or with the labeled blocked if already solved.
///
/// Track on scope basis, goto's we need to fix later.
cir::BrOp CIRGenFunction::emitBranchThroughCleanup(mlir::Location loc,
                                                   JumpDest dest) {
  // Insert a branch: to the cleanup block (unsolved) or to the already
  // materialized label. Keep track of unsolved goto's.
  assert(dest.getBlock() && "assumes incoming valid dest");
  auto brOp = cir::BrOp::create(builder, loc, dest.getBlock());

  // Calculate the innermost active normal cleanup.
  EHScopeStack::stable_iterator topCleanup =
      ehStack.getInnermostActiveNormalCleanup();

  // If we're not in an active normal cleanup scope, or if the
  // destination scope is within the innermost active normal cleanup
  // scope, we don't need to worry about fixups.
  if (topCleanup == ehStack.stable_end() ||
      topCleanup.encloses(dest.getScopeDepth())) { // works for invalid
    builder.clearInsertionPoint();
    return brOp;
  }

  // If we can't resolve the destination cleanup scope, just add this
  // to the current cleanup scope as a branch fixup.
  if (!dest.getScopeDepth().isValid()) {
    BranchFixup &fixup = ehStack.addBranchFixup();
    fixup.destination = dest.getBlock();
    fixup.destinationIndex = dest.getDestIndex();
    fixup.initialBranch = brOp;
    fixup.optimisticBranchBlock = nullptr;
    builder.clearInsertionPoint();
    return brOp;
  }

  // Otherwise, thread through all the normal cleanups in scope.

  // NOTE: In OGCG, a store to NormalCleanupDestSlot and a branch redirect
  // to the cleanup's entry block happen here. In CIR, we don't redirect
  // branches — LexicalScope::cleanup() handles routing BrOps through
  // cleanup blocks post-facto. We do record branch-afters/throughs on the
  // scope metadata so popCleanupBlock knows about them.
  // CIR doesn't use a dest index slot like OGCG — just record branch-afters.
  // Create the index at the function entry to avoid block structure issues
  // (e.g., inserting after yield in catch handlers).
  mlir::Value index;
  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    auto *entryBlock = &builder.getBlock()->getParent()->front();
    builder.setInsertionPointToStart(entryBlock);
    index = builder.getUInt32(dest.getDestIndex(), brOp.getLoc());
  }

  // Add this destination to all the scopes involved.
  EHScopeStack::stable_iterator i = topCleanup;
  EHScopeStack::stable_iterator e = dest.getScopeDepth();
  if (e.strictlyEncloses(i)) {
    while (true) {
      EHCleanupScope &scope = cast<EHCleanupScope>(*ehStack.find(i));
      assert(scope.isNormalCleanup());
      i = scope.getEnclosingNormalCleanup();

      // If this is the last cleanup we're propagating through, tell it
      // that there's a resolved jump moving through it.
      if (!e.strictlyEncloses(i)) {
        scope.addBranchAfter(index, dest.getBlock());
        break;
      }

      // Otherwise, tell the scope that there's a jump propagating
      // through it.  If this isn't new information, all the rest of
      // the work has been done before.
      if (!scope.addBranchThrough(dest.getBlock()))
        break;
    }
  }

  builder.clearInsertionPoint();
  return brOp;
}

/// Emits all the code to cause the given temporary to be cleaned up.
void CIRGenFunction::emitCXXTemporary(const CXXTemporary *temporary,
                                      QualType tempType, Address ptr) {
  pushDestroy(NormalAndEHCleanup, ptr, tempType, destroyCXXObject,
              /*useEHCleanupForArray=*/true);
}

//===----------------------------------------------------------------------===//
// EHScopeStack
//===----------------------------------------------------------------------===//

void EHScopeStack::Cleanup::anchor() {}

EHScopeStack::stable_iterator
EHScopeStack::getInnermostActiveNormalCleanup() const {
  stable_iterator si = getInnermostNormalCleanup();
  stable_iterator se = stable_end();
  while (si != se) {
    EHCleanupScope &cleanup = llvm::cast<EHCleanupScope>(*find(si));
    if (cleanup.isActive())
      return si;
    si = cleanup.getEnclosingNormalCleanup();
  }
  return stable_end();
}

/// Push an entry of the given size onto this protected-scope stack.
char *EHScopeStack::allocate(size_t size) {
  size = llvm::alignTo(size, ScopeStackAlignment);
  if (!startOfBuffer) {
    unsigned capacity = llvm::PowerOf2Ceil(std::max<size_t>(size, 1024ul));
    startOfBuffer = std::make_unique<char[]>(capacity);
    startOfData = endOfBuffer = startOfBuffer.get() + capacity;
  } else if (static_cast<size_t>(startOfData - startOfBuffer.get()) < size) {
    unsigned currentCapacity = endOfBuffer - startOfBuffer.get();
    unsigned usedCapacity =
        currentCapacity - (startOfData - startOfBuffer.get());
    unsigned requiredCapacity = usedCapacity + size;
    // We know from the 'else if' condition that requiredCapacity is greater
    // than currentCapacity.
    unsigned newCapacity = llvm::PowerOf2Ceil(requiredCapacity);

    std::unique_ptr<char[]> newStartOfBuffer =
        std::make_unique<char[]>(newCapacity);
    char *newEndOfBuffer = newStartOfBuffer.get() + newCapacity;
    char *newStartOfData = newEndOfBuffer - usedCapacity;
    memcpy(newStartOfData, startOfData, usedCapacity);
    startOfBuffer.swap(newStartOfBuffer);
    endOfBuffer = newEndOfBuffer;
    startOfData = newStartOfData;
  }

  assert(startOfBuffer.get() + size <= startOfData);
  startOfData -= size;
  return startOfData;
}

void EHScopeStack::deallocate(size_t size) {
  startOfData += llvm::alignTo(size, ScopeStackAlignment);
}

/// Remove any 'null' fixups on the stack.  However, we can't pop more
/// fixups than the fixup depth on the innermost normal cleanup, or
/// else fixups that we try to add to that cleanup will end up in the
/// wrong place.  We *could* try to shrink fixup depths, but that's
/// actually a lot of work for little benefit.
void EHScopeStack::popNullFixups() {
  // We expect this to only be called when there's still an innermost
  // normal cleanup;  otherwise there really shouldn't be any fixups.
  assert(hasNormalCleanups());

  EHScopeStack::iterator it = find(innermostNormalCleanup);
  unsigned minSize = cast<EHCleanupScope>(*it).getFixupDepth();
  assert(branchFixups.size() >= minSize && "fixup stack out of order");

  while (branchFixups.size() > minSize &&
         branchFixups.back().destination == nullptr)
    branchFixups.pop_back();
}

/// Is this cleanup a candidate for a shared-landing-pad `cir.cleanup.scope`?
///
/// If it is, pushCleanup records where the cleanup's guarded range starts and
/// popCleanupBlock tries to move that range into a `cir.cleanup.scope`, where
/// FlattenCFG wires every throwing call to the scope's ONE landing pad. That
/// reproduces OGCG's shape -- one pad per cleanup depth -- instead of one
/// synthetic `cir.try`, and so one pad, per call.
///
/// This is only a candidacy test: nothing about code generation changes here,
/// and popCleanupBlock is free to decline (the per-call tries emitted in the
/// meantime are correct on their own). Only conditions already known at push
/// time belong here.
///
/// A scope is sound only where its unwind edge is a pure cleanup chain.
/// FlattenCFG chains a cleanup.scope to the enclosing cleanup.scope and
/// otherwise resumes, so an enclosing `cir.try`'s catch handlers would be
/// bypassed. Refuse if anything other than a Cleanup is on the EH stack, or a
/// cir.try encloses us.
static bool shouldOpenCleanupScope(CIRGenFunction &cgf, EHScopeStack &ehStack,
                                   bool isEHCleanup, bool isLifetimeMarker) {
  if (!isEHCleanup || isLifetimeMarker)
    return false;
  if (!cgf.getLangOpts().Exceptions)
    return false;

  // A cleanup pushed while a cleanup region is being emitted belongs to that
  // region; nesting a scope inside it would capture the wrong code.
  if (cgf.isEmittingEHCleanup)
    return false;

  // The initializer_list backing-array path builds its own cleanup.scope and
  // emits the dispatch itself; don't wrap its cleanups a second time.
  if (cgf.emittingSharedCleanupScope)
    return false;

  // Need a real insertion point to mark the range start.
  if (!cgf.getBuilder().getInsertionBlock())
    return false;

  // Catch dispatch must not be bypassed: refuse inside a cir.try.
  if (!cgf.curLexScope || cgf.curLexScope->getClosestTryParent())
    return false;

  // Same check from the EH stack's side -- Catch/Terminate/Filter scopes.
  for (EHScopeStack::stable_iterator si = ehStack.getInnermostEHScope();
       si != ehStack.stable_end();) {
    EHScope &s = *ehStack.find(si);
    if (s.getKind() != EHScope::Cleanup)
      return false;
    si = s.getEnclosingEHScope();
  }
  return true;
}

void *EHScopeStack::pushCleanup(CleanupKind kind, size_t size) {
  char *buffer = allocate(EHCleanupScope::getSizeForCleanupSize(size));
  bool isNormalCleanup = kind & NormalCleanup;
  bool isEHCleanup = kind & EHCleanup;
  bool isLifetimeMarker = kind & LifetimeMarker;

  // Mark where this cleanup's guarded range starts, so popCleanupBlock can try
  // to move the range into a cir.cleanup.scope. Nothing else changes here: code
  // in the range is emitted exactly as before, which is what lets
  // popCleanupBlock decline without leaving anything half-done.
  bool wantsCleanupScope =
      shouldOpenCleanupScope(*cgf, *this, isEHCleanup, isLifetimeMarker);
  mlir::Block *rangeStartBlock = nullptr;
  mlir::Operation *rangeStartAfter = nullptr;
  if (wantsCleanupScope) {
    CIRGenBuilderTy &builder = cgf->getBuilder();
    rangeStartBlock = builder.getInsertionBlock();
    mlir::Block::iterator ip = builder.getInsertionPoint();
    rangeStartAfter =
        ip == rangeStartBlock->begin() ? nullptr : &*std::prev(ip);
  }

  // OGCG skips EH cleanup if the innermost EH scope is Terminate, per
  // C++ [except.terminate] which says it's implementation-defined whether
  // cleanups are called before std::terminate. CIR uses the conservative
  // behavior of always running cleanups.

  EHCleanupScope *scope = new (buffer)
      EHCleanupScope(isNormalCleanup, isEHCleanup, size, branchFixups.size(),
                     innermostNormalCleanup, innermostEHScope);
  if (isEHCleanup) {
    scope->setCleanupId(++cgf->ehCleanupIdCounter);
    scope->setScopeSeqAtPush(cgf->cleanupScopeSeqCounter);
    scope->setSharedScopeChunkId(cgf->currentSharedScopeChunkId);
  }
  if (wantsCleanupScope)
    scope->setCleanupScopeRange(rangeStartBlock, rangeStartAfter);

  if (isNormalCleanup)
    innermostNormalCleanup = stable_begin();

  if (isEHCleanup)
    innermostEHScope = stable_begin();

  if (isLifetimeMarker) {
    scope->setLifetimeMarker();
  }

  // With Windows -EHa, Invoke llvm.seh.scope.begin() for EHCleanup
  if (cgf->getLangOpts().EHAsynch && isEHCleanup && !isLifetimeMarker &&
      cgf->getTarget().getCXXABI().isMicrosoft()) {
    cgf->cgm.errorNYI("push seh cleanup");
  }

  return scope->getCleanupBuffer();
}

void EHScopeStack::popCleanup() {
  assert(!empty() && "popping exception stack when not empty");

  assert(isa<EHCleanupScope>(*begin()));
  EHCleanupScope &cleanup = cast<EHCleanupScope>(*begin());
  innermostNormalCleanup = cleanup.getEnclosingNormalCleanup();
  innermostEHScope = cleanup.getEnclosingEHScope();
  deallocate(cleanup.getAllocatedSize());

  // Destroy the cleanup.
  cleanup.destroy();

  // Check whether we can shrink the branch-fixups stack.
  if (!branchFixups.empty()) {
    // If we no longer have any normal cleanups, all the fixups are
    // complete.
    if (!hasNormalCleanups()) {
      branchFixups.clear();
    } else {
      // Otherwise we can still trim out unnecessary nulls.
      popNullFixups();
    }
  }
}

bool EHScopeStack::requiresCatchOrCleanup() const {
  for (stable_iterator si = getInnermostEHScope(); si != stable_end();) {
    // Skip cleanup scopes that are inert for future calls.
    if (auto *cleanup = dyn_cast<EHCleanupScope>(&*find(si))) {
      si = cleanup->getEnclosingEHScope();
      // Lifetime markers do not need invoke-style lowering, and deactivated
      // cleanups should not keep forcing later calls down exception-call paths.
      if (cleanup->isLifetimeMarker() || !cleanup->isActive())
        continue;
    }
    return true;
  }
  return false;
}

EHCatchScope *EHScopeStack::pushCatch(unsigned numHandlers) {
  char *buffer = allocate(EHCatchScope::getSizeForNumHandlers(numHandlers));
  EHCatchScope *scope =
      new (buffer) EHCatchScope(numHandlers, innermostEHScope);
  innermostEHScope = stable_begin();
  return scope;
}

static void emitCleanup(CIRGenFunction &cgf, EHScopeStack::Cleanup *cleanup,
                        EHScopeStack::Cleanup::Flags flags,
                        Address activeFlag) {
  // If there's an active flag, load it and guard the cleanup behind a cir.if.
  if (activeFlag.isValid()) {
    auto &builder = cgf.getBuilder();
    auto loc = cgf.currSrcLoc ? *cgf.currSrcLoc
                              : mlir::UnknownLoc::get(builder.getContext());
    mlir::Value isActive = builder.createLoad(loc, activeFlag);
    cir::IfOp::create(
        builder, loc, isActive, /*withElseRegion=*/false,
        /*thenBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          cleanup->emit(cgf, flags);
          // The object is gone, so the flag has to say so. It is set where the
          // object is built and the alloca is in the entry block, so anything
          // that runs the construction more than once -- a full-expression
          // temporary in a loop -- would otherwise carry the previous
          // iteration's true into the next one. That matters because a shared
          // cir.cleanup.scope reads the flag on every unwind edge in its
          // range, including edges taken before the object was rebuilt, and
          // would destroy the previous iteration's object a second time.
          builder.createStore(loc, builder.getFalse(loc), activeFlag);
          cir::YieldOp::create(b, loc);
        },
        /*elseBuilder=*/nullptr);
    return;
  }
  // Ask the cleanup to emit itself.
  assert(cgf.haveInsertPoint() && "expected insertion point");
  cleanup->emit(cgf, flags);
  assert(cgf.haveInsertPoint() && "cleanup ended with no insertion point?");
}

static mlir::Block *createNormalEntry(CIRGenFunction &cgf,
                                      EHCleanupScope &scope) {
  assert(scope.isNormalCleanup());
  mlir::Block *entry = scope.getNormalBlock();
  if (!entry) {
    mlir::OpBuilder::InsertionGuard guard(cgf.getBuilder());
    entry = cgf.curLexScope->getOrCreateCleanupBlock(cgf.getBuilder());
    scope.setNormalBlock(entry);
  }
  return entry;
}

void CIRGenFunction::closeActiveSyntheticTry() {
  activeSyntheticTry = nullptr;
  activeSyntheticTryScope = EHScopeStack::stable_end();
}

/// Would deactivating `scope` without an active flag lose its destructor from a
/// shared unwind edge? Only cleanups that can end up on one need the flag: the
/// cleanup may still form its own cir.cleanup.scope covering the calls before
/// this point, or an already-formed scope overlapping its range will have this
/// destructor appended at pop, when the pop-time active bit is long gone.
static bool cleanupNeedsEHDeactivationFlag(CIRGenFunction &cgf,
                                           EHCleanupScope &scope) {
  if (!scope.isEHCleanup() || scope.isLifetimeMarker())
    return false;
  if (!scope.getRangeStartBlock())
    return false;
  if (scope.wantsCleanupScopeOp())
    return true;
  return llvm::any_of(
      cgf.pendingSharedCleanupScopes,
      [&](const CIRGenFunction::PendingSharedCleanupScope &pending) {
        return pending.formationSeq > scope.getScopeSeqAtPush();
      });
}

unsigned CIRGenFunction::getInnermostEHCleanupId() {
  // Mirror the walk that fills a synthetic try's per-call cleanup regions:
  // lifetime markers contribute nothing on the EH path and are skipped there,
  // so the destructor prefix a try carries starts at the first cleanup past
  // them -- that is the cleanup whose shared dispatch could replace it.
  for (EHScopeStack::stable_iterator si = ehStack.getInnermostEHScope();
       si != ehStack.stable_end();) {
    auto *cleanup = dyn_cast<EHCleanupScope>(&*ehStack.find(si));
    if (!cleanup)
      return 0;
    if (cleanup->isEHCleanup() && !cleanup->isLifetimeMarker())
      return cleanup->getCleanupId();
    si = cleanup->getEnclosingEHScope();
  }
  return 0;
}

/// Splice `tryOp`'s body into its parent block in place of the op and strip the
/// per-call cleanup regions from the calls it held: the enclosing
/// cir.cleanup.scope's shared dispatch now provides them.
static void unwrapSyntheticTry(cir::TryOp tryOp) {
  mlir::Block *body = &tryOp.getTryRegion().front();
  if (body->mightHaveTerminator())
    body->getTerminator()->erase();

  // Only the ops that came out of the body -- everything from `firstMoved` up
  // to the try -- may have their cleanups stripped.
  mlir::Block *parent = tryOp->getBlock();
  mlir::Operation *before = tryOp->getPrevNode();
  parent->getOperations().splice(tryOp->getIterator(), body->getOperations());

  for (mlir::Operation *op = tryOp->getPrevNode(); op && op != before;
       op = op->getPrevNode()) {
    auto callOp = mlir::dyn_cast<cir::CallOp>(op);
    if (!callOp || !callOp.getException())
      continue;
    callOp.getCleanup().getBlocks().clear();
    callOp->removeAttr("cir.share_synthetic_cleanup");
  }
  tryOp->erase();
}

/// Does every value the cleanup region of `scopeOp` names still dominate it?
/// Values the region defines itself are fine; anything else has to come from
/// the block the scope op sits in, ahead of it, or from a block enclosing that
/// one. A definition in a region the scope op does not sit inside -- a sibling
/// cir.if, or the scope's own body region -- does not reach the cleanup.
static bool dominatesCleanupScope(cir::CleanupScopeOp scopeOp) {
  mlir::Region &cleanup = scopeOp.getCleanup();
  // The blocks enclosing the scope op, outermost last. The walk goes through
  // parent *ops* and stops where one has no block of its own: mid-CIRGen the
  // region being built is often not attached to anything yet, so this chain can
  // end well short of the function.
  llvm::SmallVector<mlir::Operation *, 8> enclosing;
  for (mlir::Operation *op = scopeOp; op && op->getBlock();
       op = op->getParentOp())
    enclosing.push_back(op);
  mlir::Region *outermost = enclosing.back()->getBlock()->getParent();

  // Is `block` somewhere under `region`? Walked by hand for the same reason.
  auto isUnder = [](mlir::Block *block, mlir::Region *region) {
    for (mlir::Block *b = block; b;) {
      if (b->getParent() == region)
        return true;
      mlir::Operation *parent = b->getParent()->getParentOp();
      b = parent ? parent->getBlock() : nullptr;
    }
    return false;
  };

  // Only built if the walk out reaches a block other than the definition's in
  // the same region, which takes a label or some other statement-level branch
  // between the two.
  std::optional<mlir::DominanceInfo> dom;

  auto reaches = [&](mlir::Value v) {
    mlir::Block *defBlock;
    mlir::Operation *def = v.getDefiningOp();
    if (def)
      defBlock = def->getBlock();
    else
      defBlock = mlir::cast<mlir::BlockArgument>(v).getOwner();
    // Defined by the cleanup itself. Note this is not the same as being
    // anywhere under the scope op: the body region is a sibling of the cleanup
    // one and reaches it no better than a sibling cir.if does.
    if (isUnder(defBlock, &cleanup))
      return true;
    // On the way out from the scope op: the definition has to sit ahead of
    // whichever op on that path shares its block, or ahead of the block that
    // op is in when the definition is in another block of the same region --
    // a label starts a new block, so everything declared before one is defined
    // in a block that dominates rather than holds the scope op.
    for (mlir::Operation *op : enclosing) {
      if (op->getBlock() == defBlock)
        return !def || def->isBeforeInBlock(op);
      if (op->getBlock()->getParent() == defBlock->getParent()) {
        if (!dom)
          dom.emplace();
        return dom->properlyDominates(defBlock, op->getBlock());
      }
    }
    // Off that path but still under the outermost region reached: a sibling,
    // which does not dominate. Anything further out does -- the walk only ended
    // there because CIRGen has not attached the region yet, and everything it
    // holds from outside was emitted before it started building this one.
    return !isUnder(defBlock, outermost);
  };

  bool ok = true;
  scopeOp.getCleanup().walk([&](mlir::Operation *op) {
    if (llvm::all_of(op->getOperands(), reaches))
      return mlir::WalkResult::advance();
    ok = false;
    return mlir::WalkResult::interrupt();
  });
  return ok;
}

/// Splice `scopeOp`'s body back into the block it sits in and erase it. The
/// synthetic tries inside come back with it untouched -- they are only
/// unwrapped once a scope can no longer be undone -- so the range is left with
/// exactly the IR it had before the scope was formed.
static void undoCleanupScopeOp(cir::CleanupScopeOp scopeOp) {
  mlir::Block *bodyBlock = &scopeOp.getBody().back();
  mlir::Block *block = scopeOp->getBlock();
  block->getOperations().splice(scopeOp->getIterator(),
                                bodyBlock->getOperations(), bodyBlock->begin(),
                                std::prev(bodyBlock->end()));
  scopeOp->erase();
}

void CIRGenFunction::rollbackCleanupScope(cir::CleanupScopeOp op) {
  // The scopes nested inside this one stopped taking appends of their own when
  // it formed, and it has taken some in their stead since. Those cleanups are
  // popped and gone, so the nested scopes cannot be handed them after the fact:
  // they come apart together with this one. Their tries are still in place --
  // nothing pending has been unwrapped -- so undoing them is just as free.
  //
  // One that cannot be undone stays and starts taking appends again instead:
  // the cleanups it missed while covered are gone, but that is what it would
  // have had anyway before it was ever enrolled here, and it still carries the
  // pad its body's calls depend on.
  llvm::SmallVector<cir::CleanupScopeOp, 2> covered;
  for (PendingSharedCleanupScope &pending : pendingSharedCleanupScopes)
    if (pending.coveredBy == op) {
      if (pending.undoable)
        covered.push_back(pending.op);
      else
        pending.coveredBy = {};
    }
  for (cir::CleanupScopeOp inner : covered)
    rollbackCleanupScope(inner);

  llvm::erase_if(pendingSharedCleanupScopes,
                 [&](const PendingSharedCleanupScope &pending) {
                   return pending.op == op;
                 });
  undoCleanupScopeOp(op);
}

void CIRGenFunction::sweepFinalizedCleanupScopes() {
  // Every append still to come is from a cleanup currently on the stack, and
  // only ones pushed before the scope formed reach it (see
  // appendCleanupToPendingSharedScopes). With none of those left the scope is
  // final, and the tries its shared pad has made redundant can go -- which is
  // the whole point of forming it.
  auto canStillTakeAppends = [&](unsigned formationSeq) {
    for (EHScopeStack::iterator it = ehStack.begin(); it != ehStack.end();
         ++it) {
      auto *cleanup = dyn_cast<EHCleanupScope>(&*it);
      if (!cleanup || !cleanup->isEHCleanup() || cleanup->isLifetimeMarker())
        continue;
      if (cleanup->getScopeSeqAtPush() < formationSeq)
        return true;
    }
    return false;
  };

  llvm::erase_if(pendingSharedCleanupScopes,
                 [&](const PendingSharedCleanupScope &pending) {
                   if (canStillTakeAppends(pending.formationSeq))
                     return false;
                   for (cir::TryOp tryOp : pending.unwrappable)
                     unwrapSyntheticTry(tryOp);
                   return true;
                 });
}

void CIRGenFunction::spillValuesEscapingCleanupScopes() {
  // A cir.cleanup.scope has no results, so a value its body defines is confined
  // to that region -- and $cleanup is a sibling of $body, not a child, so a
  // cleanup naming a body value is out of scope just the same.
  // formCleanupScopeOp keeps out the values it can see a consumer for, but the
  // consumers that matter here do not exist yet when the scope forms, and an
  // in-range use proves nothing, since the same value can be handed elsewhere
  // as well. Two shapes reach this point:
  //
  //  - the enclosing full expression takes it. `h.wlock()->n++`: the load of
  //    `n` feeds the increment inside the range and is *also* the expression's
  //    result, which the enclosing cir.scope yields only when it closes, well
  //    after this cleanup popped;
  //  - the shared cleanup dispatch takes it. A subobject's destructor names the
  //    address the body computed for it, and that dispatch is emitted into
  //    $cleanup as the cleanup pops, after the body is already walled off.
  //
  // Spill them, the way OGCG does for a cleanup naming a value it does not
  // dominate (DominatingValue::save): store into a slot right after the
  // definition, and reload right before each consumer. Reloading at the use --
  // rather than once, up front -- keeps whatever guard already stands between
  // the two, which is what makes the $cleanup case sound: a dispatch destroying
  // a subobject runs under the flag that says the subobject was built, and that
  // same flag says the slot was written. FlattenCFG promotes the slot away
  // again when the store and the one load land in a single block, so the common
  // case leaves nothing behind.
  auto fn = mlir::dyn_cast_if_present<cir::FuncOp>(curFn);
  if (!fn || fn.getBody().empty())
    return;

  // Post-order: an inner scope is spilled first, and the load it leaves behind
  // is then what an enclosing scope sees escaping.
  llvm::SmallVector<cir::CleanupScopeOp, 4> scopes;
  fn.walk([&](cir::CleanupScopeOp op) { scopes.push_back(op); });

  for (cir::CleanupScopeOp op : scopes) {
    // A use the reload cannot reach is not one this can repair: a consumer
    // ahead of the scope op in its own block was already reading a definition
    // that did not dominate it. Leave it for the verifier rather than move the
    // breakage somewhere less obvious. $cleanup is always reachable -- it runs
    // after some prefix of the body, on unwind.
    auto repairable = [&](mlir::Operation *user) {
      if (op.getCleanup().findAncestorOpInRegion(*user))
        return true;
      for (mlir::Operation *o = user; o; o = o->getParentOp())
        if (o->getBlock() == op->getBlock())
          return op->isBeforeInBlock(o);
      return false;
    };
    auto escapes = [&](mlir::OpOperand &use) {
      mlir::Operation *user = use.getOwner();
      return !op.getBody().findAncestorOpInRegion(*user) && repairable(user);
    };

    llvm::SmallVector<mlir::Value, 4> escaping;
    op.getBody().walk([&](mlir::Operation *nested) {
      for (mlir::Value res : nested->getResults())
        if (llvm::any_of(res.getUses(), escapes))
          escaping.push_back(res);
    });
    if (escaping.empty())
      continue;

    for (mlir::Value v : escaping) {
      mlir::Type ty = v.getType();
      mlir::Location loc = v.getLoc();
      CharUnits align = CharUnits::fromQuantity(
          cgm.getDataLayout().getABITypeAlign(ty).value());
      Address slot(emitAlloca("tmp.cleanup.scope.res", ty, loc, align,
                              /*insertIntoFnEntryBlock=*/true),
                   ty, align);
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointAfterValue(v);
      builder.createStore(loc, v, slot);

      // Collect before rewriting: setting an operand walks the use list.
      llvm::SmallVector<mlir::OpOperand *, 4> uses;
      for (mlir::OpOperand &use : v.getUses())
        if (escapes(use))
          uses.push_back(&use);
      for (mlir::OpOperand *use : uses) {
        builder.setInsertionPoint(use->getOwner());
        use->set(builder.createLoad(loc, slot));
      }
    }
  }
}

cir::CleanupScopeOp CIRGenFunction::formCleanupScopeOp(EHCleanupScope &scope) {
  mlir::Block *block = scope.getRangeStartBlock();
  // Only a range that stayed inside one block can be moved into a region. If
  // codegen split the block (goto, switch fallthrough, ...), decline.
  if (!block || builder.getInsertionBlock() != block)
    return {};

  mlir::Operation *after = scope.getRangeStartAfter();
  mlir::Block::iterator first =
      after ? std::next(after->getIterator()) : block->begin();
  mlir::Block::iterator last = builder.getInsertionPoint();

  // Where popCleanupBlock must carry on emitting once the region is built. The
  // range can end short of here, and the normal-path cleanup still belongs at
  // the end of the block -- putting it right after the scope op would run the
  // destructor before the rest of the block.
  mlir::Block::iterator resumeAt = last;

  // A terminator already emitted at the end of the block (the function's
  // cir.return, say) is not part of the guarded range.
  if (last == block->end() && block->mightHaveTerminator())
    last = block->getTerminator()->getIterator();
  if (first == last)
    return {};

  // A value defined in the range and used after it would be trapped inside the
  // new region. Uses emitted so far are visible below, but CIRGen can still be
  // holding a value whose consumer does not exist yet -- the enclosing
  // cir.scope yields the full-expression's result only when that scope closes,
  // well after this cleanup pops, and an in-range use proves nothing, since the
  // same value can be handed to the enclosing expression as well. Two things
  // keep such a value out of the region:
  //
  //  - lifting it above the range, whenever it takes nothing the range defines
  //    and is pure, an alloca, or a base-class adjustment. Moving an allocation
  //    earlier in its block is always valid, and keeping allocas out of the
  //    body also leaves the entry block's alloca order alone;
  //    cir.base_class_addr is a constant-offset adjustment for a *non-virtual*
  //    base that lowers to pointer arithmetic, so its MemRead annotation is
  //    conservative bookkeeping rather than a real dependence. This is done
  //    unconditionally, not just for values nothing uses yet: these address
  //    computations -- the pointer a new-expression yields, a global's address,
  //    a temporary's slot, a base subobject's address -- are exactly what a
  //    full expression hands upwards, and lifting costs nothing, since none of
  //    them throw and so none needs a pad;
  //  - otherwise, ending the range at the first op with a result nothing uses
  //    yet, which is the clearest sign of a value CIRGen has kept for a
  //    consumer it has not emitted. A cir.load cannot be lifted -- a call in
  //    the range may write what it reads -- and truncating still shares one pad
  //    across the prefix, with the remainder keeping the per-call synthetic try
  //    it had before.
  //
  // What stays in the range is then either already dead or feeding a call, and
  // the scan below over the uses that do exist catches the rest.
  llvm::SmallVector<mlir::Operation *, 8> lifted;
  auto definedInRange = [&](mlir::Value v) {
    mlir::Operation *def = v.getDefiningOp();
    return def && def->getBlock() == block && !def->isBeforeInBlock(&*first) &&
           !llvm::is_contained(lifted, def);
  };
  for (mlir::Block::iterator it = first; it != last; ++it) {
    if (it->getNumRegions() == 0 &&
        (mlir::isMemoryEffectFree(&*it) ||
         mlir::isa<cir::AllocaOp, cir::BaseClassAddrOp>(&*it)) &&
        llvm::none_of(it->getOperands(), definedInRange)) {
      lifted.push_back(&*it);
      continue;
    }
    if (llvm::any_of(it->getResults(),
                     [](mlir::Value v) { return v.use_empty(); })) {
      last = it;
      break;
    }
  }
  // Move them out in order, each after the last one moved. Anchoring on the op
  // the range starts after keeps that order even when the op at the front of
  // the range is itself lifted, which would otherwise drag the insertion point
  // along with it and leave a use ahead of its definition.
  mlir::Operation *anchor = after;
  for (mlir::Operation *op : lifted) {
    if (anchor)
      op->moveAfter(anchor);
    else
      op->moveBefore(block, block->begin());
    anchor = op;
  }
  first = anchor ? std::next(anchor->getIterator()) : block->begin();
  if (first == last)
    return {};

  // The ancestor of `op` that sits directly in `block`, or null if `op` is not
  // nested under one.
  //
  // The climb costs one step per level of nesting, and the scan below asks for
  // it once per use of every op in the range. When a cleanup declines to share
  // a pad -- a long brace initializer, say, where each element keeps its own
  // synthetic try -- those tries nest one level per element, so the depth grows
  // with the range and a plain climb per use is quadratic in it. On a
  // 1155-element table that is minutes to tens of minutes of CIRGen for one
  // function. The ancestor is a pure function of the op and consecutive uses
  // share almost all of their path, so memoize it: each op is climbed past
  // once, and every later query along that path is a single lookup.
  llvm::DenseMap<mlir::Operation *, mlir::Operation *> blockLevelAncestor;
  auto ancestorInBlock = [&](mlir::Operation *op) -> mlir::Operation * {
    llvm::SmallVector<mlir::Operation *, 8> climbed;
    while (op && op->getBlock() != block) {
      auto known = blockLevelAncestor.find(op);
      if (known != blockLevelAncestor.end()) {
        op = known->second;
        break;
      }
      climbed.push_back(op);
      op = op->getParentOp();
    }
    for (mlir::Operation *seen : climbed)
      blockLevelAncestor[seen] = op;
    return op;
  };

  // Is `op` (or an ancestor of it) inside the range?
  auto inRange = [&](mlir::Operation *op) {
    op = ancestorInBlock(op);
    if (!op)
      return false;
    if (op->isBeforeInBlock(&*first))
      return false;
    return last == block->end() || op->isBeforeInBlock(&*last);
  };

  // Which synthetic tries in the range can be collapsed into the shared pad?
  // Only ones created while *this* cleanup was innermost: any other recorded
  // scope means the try's per-call cleanup runs destructors the shared dispatch
  // would not, and unwrapping it would drop them.
  unsigned self = scope.getCleanupId();
  llvm::SmallVector<cir::TryOp, 8> unwrappable;
  bool sawUserTry = false;
  bool escapes = false;
  bool tooBigToPayFor = false;

  // Forming the scope is an optimization, and proving it legal costs a walk of
  // every op in the range. popCleanupBlocks pops one cleanup per object the
  // scope holds, so a scope holding N objects runs this scan N times, over a
  // range that itself grows with N. That is quadratic with a full IR walk for a
  // constant, and it is reached by ordinary code: a brace initializer whose
  // elements have non-trivial destructors and which is too long to share one
  // pad leaves a synthetic try per element for the scan to walk. Bound the work
  // instead. Declining is always safe -- it is the same per-call form emitted
  // everywhere else this scan says no -- and a range this large is past the
  // point where one shared pad is the shape worth having.
  constexpr unsigned maxScannedOps = 8192;
  unsigned scannedOps = 0;

  for (mlir::Block::iterator it = first;
       it != last && !sawUserTry && !tooBigToPayFor; ++it) {
    it->walk([&](mlir::Operation *nested) {
      if (++scannedOps > maxScannedOps) {
        tooBigToPayFor = true;
        return mlir::WalkResult::interrupt();
      }
      // A value already used after the range would be trapped inside the new
      // region; decline rather than build IR violating dominance.
      for (mlir::Value res : nested->getResults())
        for (mlir::Operation *user : res.getUsers())
          if (!inRange(user)) {
            escapes = true;
            return mlir::WalkResult::interrupt();
          }
      auto tryOp = mlir::dyn_cast<cir::TryOp>(nested);
      if (!tryOp)
        return mlir::WalkResult::advance();
      if (!tryOp.getSynthetic()) {
        // A user try inside the range would have its catch handlers bypassed:
        // FlattenCFG chains a cleanup.scope's pad outward, past them.
        sawUserTry = true;
        return mlir::WalkResult::interrupt();
      }
      auto id =
          tryOp->getAttrOfType<mlir::IntegerAttr>(kSyntheticTryCleanupIdAttr);
      if (id && static_cast<unsigned>(id.getInt()) == self &&
          tryOp.getTryRegion().hasOneBlock())
        unwrappable.push_back(tryOp);
      return mlir::WalkResult::advance();
    });
  }
  if (escapes || sawUserTry || tooBigToPayFor)
    return {};
  // Nothing to share a pad between: leave the IR alone.
  if (unwrappable.empty())
    return {};

  mlir::Location loc = first->getLoc();
  builder.setInsertionPoint(block, first);
  auto cleanupScopeOp = cir::CleanupScopeOp::create(
      builder, loc,
      /*bodyBuilder=*/[](mlir::OpBuilder &, mlir::Location) {},
      /*cleanupBuilder=*/[](mlir::OpBuilder &, mlir::Location) {});

  // Move the range into the body region, then terminate it.
  mlir::Block *bodyBlock = &cleanupScopeOp.getBody().back();
  bodyBlock->getOperations().splice(bodyBlock->end(), block->getOperations(),
                                    first, last);
  builder.setInsertionPointToEnd(bodyBlock);
  cir::YieldOp::create(builder, loc);

  // No call emitted from here on belongs to the try the last call opened.
  closeActiveSyntheticTry();

  // Emit ONLY this cleanup into the cleanup region: the enclosing cleanups run
  // by pad chaining (or by appendCleanupToPendingSharedScopes when an enclosing
  // cleanup forms no scope), which is what keeps the pad count at one per depth
  // rather than one per depth-prefix.
  builder.setInsertionPointToEnd(&cleanupScopeOp.getCleanup().back());
  emitSharedCleanupScopeDispatch(scope.getEnclosingEHScope());
  if (!builder.getInsertionBlock()->mightHaveTerminator())
    cir::YieldOp::create(builder, loc);

  // A cleanup can name a value the scope op does not dominate. The usual case
  // is a conditional cleanup: a partially built object's members are destroyed
  // through addresses computed inside the cir.if that ran the construction, and
  // the cleanup region is a sibling of that region, not a descendant of it. The
  // range itself is off limits too, since the body region is likewise a sibling
  // of the cleanup one. Nothing about the dispatch is known before it is
  // emitted, so it is emitted first and the scope undone if it does not hold
  // up: the range goes back where it came from, and the calls keep the
  // per-call synthetic tries they still have -- nothing has been unwrapped yet.
  if (!dominatesCleanupScope(cleanupScopeOp)) {
    undoCleanupScopeOp(cleanupScopeOp);
    builder.setInsertionPoint(block, resumeAt);
    return {};
  }

  builder.setInsertionPoint(block, resumeAt);

  // Scopes now nested inside this one are chained to its pad by FlattenCFG and
  // no longer need enclosing destructors appended directly. Marking rather than
  // dropping them: if this scope is undone, they are back to needing their own.
  // The innermost cover wins, so a scope already covered is left alone.
  for (PendingSharedCleanupScope &inner : pendingSharedCleanupScopes)
    if (!inner.coveredBy && cleanupScopeOp->isProperAncestor(inner.op))
      inner.coveredBy = cleanupScopeOp;

  // The calls in the body now unwind to the shared pad, so their per-call tries
  // and the destructor prefixes duplicated in them are redundant -- but they
  // are what the range falls back to if the scope has to be undone, so they are
  // only dropped once that can no longer happen.
  pendingSharedCleanupScopes.push_back(
      {cleanupScopeOp, ++cleanupScopeSeqCounter, std::move(unwrappable),
       /*coveredBy=*/{}});
  return cleanupScopeOp;
}

/// Does `op` sit at or after the point where `scope`'s guarded range starts?
/// Only ops this can place *before* the range answer false; anything the two
/// positions cannot be compared for is left to the caller's other tests.
static bool startsBeforeCleanupRange(EHCleanupScope &scope,
                                     mlir::Operation *op) {
  mlir::Block *start = scope.getRangeStartBlock();
  if (!start)
    return false;
  mlir::Operation *after = scope.getRangeStartAfter();
  // The anchor is the op the range starts after; if an earlier scope moved it
  // into a region, the range start moved with it and this comparison no longer
  // says anything.
  if (after && after->getBlock() != start)
    return false;
  for (mlir::Operation *o = op; o; o = o->getParentOp())
    if (o->getBlock() == start)
      return after && !after->isBeforeInBlock(o);
  return false;
}

void CIRGenFunction::registerSharedCleanupScope(
    cir::CleanupScopeOp op, EHScopeStack::stable_iterator dispatchBoundary) {
  // Same bookkeeping formCleanupScopeOp does for a scope it builds itself: the
  // scopes this one now encloses are chained to its pad by FlattenCFG and take
  // no appends of their own.
  for (PendingSharedCleanupScope &inner : pendingSharedCleanupScopes)
    if (!inner.coveredBy && op->isProperAncestor(inner.op))
      inner.coveredBy = op;
  pendingSharedCleanupScopes.push_back({op, ++cleanupScopeSeqCounter,
                                        /*unwrappable=*/{}, /*coveredBy=*/{},
                                        /*undoable=*/false, dispatchBoundary});
}

void CIRGenFunction::appendCleanupToPendingSharedScopes(
    EHCleanupScope &scope, cir::CleanupScopeOp skip) {
  llvm::SmallVector<cir::CleanupScopeOp, 2> broken;
  for (const PendingSharedCleanupScope &pending : pendingSharedCleanupScopes) {
    // The scope this same cleanup just formed already emitted the destructor
    // into its own cleanup region.
    if (pending.op == skip)
      continue;
    // A scope nested inside another one reaches this destructor through its
    // parent's pad, which is getting it below.
    if (pending.coveredBy)
      continue;
    // A scope that already existed when this cleanup was pushed sits entirely
    // before its guarded range: it must not get this destructor, and emitting
    // it there could even reference values defined after the scope op (a
    // dominance violation).
    if (pending.formationSeq <= scope.getScopeSeqAtPush())
      continue;
    // Nor may one that formed later but still sits before the range -- which
    // is what the scope guarding the *construction* of the object this cleanup
    // destroys looks like: it is built when the constructor's argument
    // temporaries pop, after the variable's own cleanup was pushed.
    if (startsBeforeCleanupRange(scope, pending.op))
      continue;
    // A scope that wrote its own cleanup region already ran everything that
    // was on the stack inside the boundary its dispatch stopped at -- the
    // partial-array cleanup an initializer_list pushes for itself, and the
    // per-field ones with it. Those pop like any other and would otherwise be
    // emitted into that same region a second time, destroying the elements
    // twice on the unwind edge. Only what sits outside the boundary, which the
    // dispatch could not see, is missing.
    if (pending.dispatchBoundary.isValid() &&
        pending.dispatchBoundary.strictlyEncloses(ehStack.stable_begin()))
      continue;
    cir::CleanupScopeOp inner = pending.op;
    mlir::Block *cleanupBlock = &inner.getCleanup().back();
    // Where the region ended before this append, so it can be put back if the
    // dispatch turns out not to hold up and the scope itself cannot be undone.
    mlir::Operation *mark = nullptr;
    {
      mlir::OpBuilder::InsertionGuard guard(builder);
      // Run after the destructors already in the region: unwinding destroys
      // innermost-first, and this cleanup encloses all of them. Drop the
      // trailing yield first -- emitSharedCleanupScopeDispatch erases yields as
      // it chains cleanups, so an insertion point anchored before one would
      // dangle.
      if (cleanupBlock->mightHaveTerminator())
        cleanupBlock->getTerminator()->erase();
      if (!cleanupBlock->empty())
        mark = &cleanupBlock->back();
      builder.setInsertionPointToEnd(cleanupBlock);
      emitSharedCleanupScopeDispatch(scope.getEnclosingEHScope());
      if (mlir::Block *end = builder.getInsertionBlock())
        if (!end->mightHaveTerminator())
          cir::YieldOp::create(builder, inner.getLoc());
    }
    // This cleanup may name values the scope op does not dominate: a
    // conditional cleanup destroys a partially built object through addresses
    // computed inside the cir.if that ran the construction, and the scope op is
    // a sibling of that region, not a descendant of it. OGCG spills such
    // addresses (DominatingValue::save); CIR has no equivalent, so the scope is
    // undone instead and the range goes back to the per-call synthetic tries it
    // still has, which do sit inside that region.
    if (dominatesCleanupScope(inner))
      continue;
    if (pending.undoable) {
      broken.push_back(inner);
      continue;
    }
    // A scope with nothing to fall back on keeps its pad and gives up only the
    // dispatch just emitted -- the destructor is lost on that edge either way,
    // and undoing the scope would lose the ones it already carries too. Erased
    // back to front, so nothing outside the appended run can still name a
    // result being dropped.
    while (!cleanupBlock->empty() && &cleanupBlock->back() != mark)
      cleanupBlock->back().erase();
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToEnd(cleanupBlock);
    cir::YieldOp::create(builder, inner.getLoc());
  }
  // Rolled back only once the walk is done: it mutates the list.
  for (cir::CleanupScopeOp op : broken)
    rollbackCleanupScope(op);
}

/// Pops a cleanup block. If the block includes a normal cleanup, the
/// current insertion point is threaded through the cleanup, as are
/// any branch fixups on the cleanup.
void CIRGenFunction::popCleanupBlock() {
  assert(!ehStack.empty() && "cleanup stack is empty!");
  assert(isa<EHCleanupScope>(*ehStack.begin()) && "top not a cleanup!");
  EHCleanupScope &scope = cast<EHCleanupScope>(*ehStack.begin());
  assert(scope.getFixupDepth() <= ehStack.getNumBranchFixups());

  // This pop can be the one that makes a pending cir.cleanup.scope final; the
  // stack has to have been popped for the check to say so, and there are
  // several ways out of here.
  auto sweep = llvm::make_scope_exit([&] { sweepFinalizedCleanupScopes(); });

  // Invalidate the active synthetic TryOp if the scope being popped changes
  // the EH cleanup depth. The TryOp was created for calls at a specific
  // depth; popping a scope means subsequent calls will be at a different
  // depth and need a new TryOp.
  if (activeSyntheticTry &&
      ehStack.getInnermostEHScope() == activeSyntheticTryScope)
    closeActiveSyntheticTry();

  // Wrap this cleanup's guarded range in a cir.cleanup.scope, so every throwing
  // call in it shares one landing pad instead of each opening its own synthetic
  // cir.try. Leaves the insertion point where it found it (just after the new
  // op), so the normal-path cleanup emitted below is placed as before.
  //
  // cir.cleanup.scopes already built inside this cleanup still need its
  // destructor whenever its own pad does not enclose them -- FlattenCFG chains
  // a pad outward only through the scopes it is nested in. That covers the
  // scopes formCleanupScopeOp marked `coveredBy`, and nothing else: forming a
  // scope here does not reach a *sibling* one, which is what a range truncated
  // short of the cleanup's full extent leaves behind. The range ends at the
  // first value nothing consumes yet, so a discarded result -- `a.get();` as a
  // statement, a comma operator's left operand -- splits one cleanup's calls
  // across two scopes, and the second one would resume past this destructor.
  if (!pendingSharedCleanupScopes.empty() || scope.wantsCleanupScopeOp()) {
    cir::CleanupScopeOp formed;
    if (scope.wantsCleanupScopeOp())
      formed = formCleanupScopeOp(scope);
    if (scope.isEHCleanup() && !scope.isLifetimeMarker() &&
        !pendingSharedCleanupScopes.empty())
      appendCleanupToPendingSharedScopes(scope, /*skip=*/formed);
  }

  // Remember activation information.
  bool isActive = scope.isActive();

  // Extract active flags for conditional cleanup guards. These are
  // set by createCleanupActiveFlag() when a cleanup is pushed inside
  // a conditional branch (e.g., ternary expression).
  Address normalActiveFlag =
      scope.shouldTestFlagInNormalCleanup() ? scope.getActiveFlag()
                                            : Address::invalid();
  Address ehActiveFlag [[maybe_unused]] =
      scope.shouldTestFlagInEHCleanup() ? scope.getActiveFlag()
                                        : Address::invalid();

  // - whether there are branch fix-ups through this cleanup
  unsigned fixupDepth = scope.getFixupDepth();
  bool hasFixups = ehStack.getNumBranchFixups() != fixupDepth;

  // - whether there are branch-throughs or branch-afters
  bool hasExistingBranches = scope.hasBranches();

  // - whether there's a fallthrough
  mlir::Block *fallthroughSource = builder.getInsertionBlock();
  // A block created after cir.unreachable (for noreturn functions) is dead
  // code with no predecessors. Emitting cleanups into such blocks produces
  // invalid CIR because they end up after cir.yield (the region terminator
  // placed by ternary/scope builders). Skip cleanup emission for dead blocks
  // that follow an unreachable terminator.
  if (fallthroughSource && fallthroughSource->hasNoPredecessors() &&
      !fallthroughSource->isEntryBlock()) {
    // Check if the predecessor block (in region order) ends with unreachable
    mlir::Region *region = fallthroughSource->getParent();
    if (region) {
      auto it = fallthroughSource->getIterator();
      if (it != region->begin()) {
        mlir::Block *prevBlock = &*std::prev(it);
        if (!prevBlock->empty() &&
            mlir::isa<cir::UnreachableOp>(prevBlock->back()))
          fallthroughSource = nullptr;
      }
    }
  }
  bool hasFallthrough =
      fallthroughSource != nullptr && (isActive || hasExistingBranches);

  bool requiresNormalCleanup =
      scope.isNormalCleanup() &&
      (hasFixups || hasExistingBranches || hasFallthrough);

  // If we don't need the cleanup at all, we're done.
  // OGCG also checks RequiresEHCleanup here. CIR doesn't have EH cleanup
  // block emission yet, but we must not silently skip EH cleanups when they
  // are needed.
  bool requiresEHCleanup = scope.isEHCleanup() && scope.isActive();
  if (!requiresNormalCleanup && !requiresEHCleanup) {
    ehStack.popCleanup();
    return;
  }
  if (!requiresNormalCleanup) {
    // EH cleanup is required but normal cleanup is not. In OGCG, this
    // would emit a dedicated ehcleanup basic block. In CIR, EH cleanups
    // are handled via per-call-site inline emission in
    // populateEHCatchRegions (CIRGenException.cpp) which inlines cleanup
    // bodies into each call's exception handler region. This covers the
    // common EH-only cleanup cases (FreeException, __cxa_end_catch, etc.)
    // and is verified by eh-cleanup-scope.cpp tests.
    ehStack.popCleanup();
    return;
  }

  // Copy the cleanup emission data out.  This uses either a stack
  // array or malloc'd memory, depending on the size, which is
  // behavior that SmallVector would provide, if we could use it
  // here. Unfortunately, if you ask for a SmallVector<char>, the
  // alignment isn't sufficient.
  auto *cleanupSource = reinterpret_cast<char *>(scope.getCleanupBuffer());
  alignas(EHScopeStack::ScopeStackAlignment) char
      cleanupBufferStack[8 * sizeof(void *)];
  std::unique_ptr<char[]> cleanupBufferHeap;
  size_t cleanupSize = scope.getCleanupSize();
  EHScopeStack::Cleanup *cleanup;

  // This is necessary because we are going to deallocate the cleanup
  // (in popCleanup) before we emit it.
  if (cleanupSize <= sizeof(cleanupBufferStack)) {
    memcpy(cleanupBufferStack, cleanupSource, cleanupSize);
    cleanup = reinterpret_cast<EHScopeStack::Cleanup *>(cleanupBufferStack);
  } else {
    cleanupBufferHeap.reset(new char[cleanupSize]);
    memcpy(cleanupBufferHeap.get(), cleanupSource, cleanupSize);
    cleanup =
        reinterpret_cast<EHScopeStack::Cleanup *>(cleanupBufferHeap.get());
  }

  EHScopeStack::Cleanup::Flags cleanupFlags;
  if (scope.isNormalCleanup())
    cleanupFlags.setIsNormalCleanupKind();
  if (scope.isEHCleanup())
    cleanupFlags.setIsEHCleanupKind();

  // If we have a fallthrough and no other need for the cleanup,
  // emit it directly.
  if (hasFallthrough && !hasFixups && !hasExistingBranches) {
    // OGCG checks requiresEHCleanup here to avoid the simple path when
    // an EH cleanup block needs to be emitted separately. CIR doesn't
    // need this check because EH cleanups are inlined at each call site
    // via populateEHCatchRegions, not emitted in popCleanupBlock.
    scope.markEmitted();
    ehStack.popCleanup();
    emitCleanup(*this, cleanup, cleanupFlags, normalActiveFlag);  } else {
    // Otherwise, the best approach is to thread everything through
    // the cleanup block and then try to clean up after ourselves.

    // Force the entry block to exist.
    mlir::Block *normalEntry = createNormalEntry(*this, scope);

    // I.  Set up the fallthrough edge in.
    mlir::OpBuilder::InsertPoint savedInactiveFallthroughIP;

    // If there's a fallthrough, we need to store the cleanup
    // destination index. For fall-throughs this is always zero in OGCG's
    // NormalCleanupDestSlot model. CIR does not model NormalCleanupDestSlot;
    // branch routing is wired structurally by LexicalScope::cleanup().
    if (hasFallthrough) {
    } else if (fallthroughSource) {
      // Otherwise, save and clear the IP if we don't have fallthrough
      // because the cleanup is inactive.
      assert(!isActive && "source without fallthrough for active cleanup");
      savedInactiveFallthroughIP = builder.saveInsertionPoint();
    }

    // II.  Emit the entry block.  This implicitly branches to it if
    // we have fallthrough.  All the fixups and existing branches
    // should already be branched to it.
    builder.setInsertionPointToEnd(normalEntry);

    // intercept normal cleanup to mark SEH scope end
    // OGCG checks requiresEHCleanup for SEH EHAsynch scope end marker
    // (CGCleanup.cpp:786-836). CIR doesn't support SEH async exceptions.

    // III.  Figure out where we're going and build the cleanup
    // epilogue.
    bool hasEnclosingCleanups =
        (scope.getEnclosingNormalCleanup() != ehStack.stable_end());

    // Compute the branch-through dest if we need it:
    //   - if there are branch-throughs threaded through the scope
    //   - if fall-through is a branch-through
    //   - if there are fixups that will be optimistically forwarded
    //     to the enclosing cleanup
    mlir::Block *branchThroughDest = nullptr;
    if (scope.hasBranchThroughs() || (hasFixups && hasEnclosingCleanups)) {
      // OGCG builds a switch-out here to route branches through cleanup
      // blocks. CIR uses LexicalScope::cleanup() to wire branches instead.
    }
    (void)branchThroughDest;

    mlir::Block *fallthroughDest = nullptr;

    // Save the single branch-after destination if applicable, to create
    // a branch after cleanup emission.
    mlir::Block *singleBranchAfterDest = nullptr;

    // If there's exactly one branch-after and no other threads,
    // we can route it without a switch.
    // Skip for SEH, since ExitSwitch is used to generate code to indicate
    // abnormal termination. (SEH: Except _leave and fall-through at
    // the end, all other exits in a _try (return/goto/continue/break)
    // are considered as abnormal terminations, using NormalCleanupDestSlot
    // to indicate abnormal termination)
    if (!scope.hasBranchThroughs() && !hasFixups && !hasFallthrough &&
        scope.getNumBranchAfters() == 1) {
      // Route directly to the single branch-after destination.
      // CIR doesn't use NormalCleanupDestSlot, so no dead-store cleanup needed.
      // The branch will be created after the cleanup is emitted below.
      singleBranchAfterDest = scope.getBranchAfterBlock(0);

      // Build a switch-out if we need it:
      //   - if there are branch-afters threaded through the scope
      //   - if fall-through is a branch-after
      //   - if there are fixups that have nowhere left to go and
      //     so must be immediately resolved
    } else if (scope.getNumBranchAfters() || (hasFallthrough) ||
               (hasFixups && !hasEnclosingCleanups)) {
      // In CIR, LexicalScope::cleanup() handles routing branches through
      // cleanup blocks post-facto, so we don't build a switch-out here.
      // The cleanup code is emitted below and LexicalScope::cleanup() will
      // wire up the branches correctly.
    }
    // OGCG emits the EH cleanup block here (CGCleanup.cpp:1031-1081).
    // CIR handles EH cleanups via per-call-site inline emission in
    // populateEHCatchRegions, so no separate EH block is needed here.

    // IV.  Pop the cleanup and emit it.
    scope.markEmitted();
    ehStack.popCleanup();
    assert(ehStack.hasNormalCleanups() == hasEnclosingCleanups);

    emitCleanup(*this, cleanup, cleanupFlags, normalActiveFlag);
    // Append the branch-after destination if we have one.
    // In OGCG, this is handled by InstsToAppend. In CIR, we create the
    // branch directly after cleanup emission.
    if (singleBranchAfterDest) {
      auto loc = currSrcLoc ? *currSrcLoc
                            : mlir::UnknownLoc::get(builder.getContext());
      cir::BrOp::create(builder, loc, singleBranchAfterDest);
    }
    // cleanupAppendInsts -- OGCG appends instructions after cleanup. CIR creates branches directly.

    // Optimistically hope that any fixups will continue falling through.
    for (unsigned i = fixupDepth, e = ehStack.getNumBranchFixups(); i < e;
         ++i) {
      BranchFixup &fixup = ehStack.getBranchFixup(i);
      if (!fixup.destination)
        continue;
      fixup.optimisticBranchBlock = fixup.initialBranch->getBlock();
    }

    // V.  Set up the fallthrough edge out.

    // Case 1: a fallthrough source exists but doesn't branch to the
    // cleanup because the cleanup is inactive.
    if (!hasFallthrough && fallthroughSource) {
      // Prebranched fallthrough was forwarded earlier.
      // Non-prebranched fallthrough doesn't need to be forwarded.
      // Either way, all we need to do is restore the IP we cleared before.
      assert(!isActive);
      builder.restoreInsertionPoint(savedInactiveFallthroughIP);

      // Case 2: a fallthrough source exists and should branch to the
      // cleanup, but we're not supposed to branch through to the next
      // cleanup.
    } else if (hasFallthrough && fallthroughDest) {
      cgm.errorNYI("cleanup fallthrough destination");

      // Case 3: a fallthrough source exists and should branch to the
      // cleanup and then through to the next.
    } else if (hasFallthrough) {
      // Everything is already set up for this.

      // Case 4: no fallthrough source exists.
    } else {
      builder.clearInsertionPoint();
    }

    // VI.  Assorted cleaning.

    // OGCG's SimplifyCleanupEntry merges NormalEntry into its single
    // predecessor (LLVM basic block optimization) and patches fixup pointers.
    // CIR uses structured control flow (MLIR regions), so block merging is
    // not applicable — MLIR's canonicalization handles this if needed.
  }
}

/// Pops cleanup blocks until the given savepoint is reached.
void CIRGenFunction::popCleanupBlocks(
    EHScopeStack::stable_iterator oldCleanupStackDepth) {
  // OGCG tracks HadBranches across popped scopes to decide whether to
  // spill/reload ValuesToReload. CIR doesn't need this because MLIR SSA
  // handles value liveness without manual spilling.

  // OGCG takes a ValuesToReload parameter that spills/reloads values that
  // need to survive across cleanup blocks. CIR doesn't need this because
  // MLIR's SSA form and region-based scoping handle value liveness
  // differently from LLVM IR's flat basic blocks.
  // cleanupValuesToReload -- MLIR SSA handles value liveness; no reload needed after cleanup.

  // Pop cleanup blocks until we reach the base stack depth for the
  // current scope.
  // OGCG computes FallthroughIsBranchThrough per cleanup via
  // Old.strictlyEncloses(Scope.getEnclosingNormalCleanup()) and passes it to
  // PopCleanupBlock. This affects fallthrough dest-slot stores and
  // branch-through routing. CIR's popCleanupBlock takes no parameters.
  // cleanupFallthroughIsBranchThrough -- CIR popCleanupBlock uses LexicalScope, not dest-slot routing.

  while (ehStack.stable_begin() != oldCleanupStackDepth) {
    popCleanupBlock();
  }
}

/// Pops cleanup blocks until the given savepoint is reached, then add the
/// cleanups from the given savepoint in the lifetime-extended cleanups stack.
void CIRGenFunction::popCleanupBlocks(
    EHScopeStack::stable_iterator oldCleanupStackDepth,
    size_t oldLifetimeExtendedSize) {
  popCleanupBlocks(oldCleanupStackDepth);

  // Move our deferred cleanups onto the EH stack.
  for (size_t i = oldLifetimeExtendedSize,
              e = lifetimeExtendedCleanupStack.size();
       i != e; /**/) {
    // Alignment should be guaranteed by the vptrs in the individual cleanups.
    assert((i % alignof(LifetimeExtendedCleanupHeader) == 0) &&
           "misaligned cleanup stack entry");

    LifetimeExtendedCleanupHeader &header =
        reinterpret_cast<LifetimeExtendedCleanupHeader &>(
            lifetimeExtendedCleanupStack[i]);
    i += sizeof(header);

    ehStack.pushCopyOfCleanup(header.getKind(),
                              &lifetimeExtendedCleanupStack[i], header.size);
    i += header.size;

    if (header.isConditional) {
      Address activeFlag =
          reinterpret_cast<Address &>(lifetimeExtendedCleanupStack[i]);
      initFullExprCleanupWithFlag(activeFlag);
      i += sizeof(Address);
    }
  }
  lifetimeExtendedCleanupStack.resize(oldLifetimeExtendedSize);
}

/// Emit cleanup code inline for all active normal cleanups between the
/// current ehStack top and \p depth, WITHOUT popping them off the stack.
/// This is used for early exits (break/continue/goto) in CIR's structured
/// control flow model, where we can't thread branches through cleanup blocks
/// across region boundaries.
void CIRGenFunction::emitCleanupsForEarlyExit(
    EHScopeStack::stable_iterator depth,
    EHScopeStack::stable_iterator start) {
  // Walk the ehStack from start to depth, emitting each normal cleanup inline.
  EHScopeStack::stable_iterator startIt =
      start.isValid() ? start : ehStack.stable_begin();
  for (EHScopeStack::stable_iterator it = startIt; it != depth;) {
    // Guard: if the chain walk has passed below the target depth, stop.
    // This happens when the normal cleanup chain skips non-cleanup scopes
    // (e.g., Terminate scopes pushed by emitStartEHSpec for noexcept
    // functions). The normal chain links only through EHCleanupScopes,
    // so it can jump from a cleanup directly to stable_end(), bypassing
    // an intervening Terminate scope where depth is positioned. Without
    // this guard, we'd dereference past the end of the EH scope stack
    // buffer, reading garbage memory.
    if (!depth.encloses(it))
      break;

    EHScope &scope = *ehStack.find(it);
    if (scope.getKind() != EHScope::Cleanup) {
      it = scope.getEnclosingEHScope();
      continue;
    }

    EHCleanupScope &cleanupScope = cast<EHCleanupScope>(scope);

    // Advance using the normal cleanup chain, not the EH scope chain.
    // The targetDepth is a stable_iterator from the normal cleanup chain
    // (captured by RunCleanupsScope/LexicalScope), so we must walk the
    // same chain to reach it. Using getEnclosingEHScope() would follow
    // a different linked list that may never hit targetDepth, causing
    // an infinite loop and eventual assertion failure.
    it = cleanupScope.getEnclosingNormalCleanup();

    if (!cleanupScope.isNormalCleanup() || !cleanupScope.isActive())
      continue;

    // Emit the cleanup body inline at the current insertion point.
    // If the cleanup has an active flag (e.g. for variables whose
    // initialization is bypassed by switch cases), guard the emission
    // behind a flag check so the destructor only runs on paths where
    // the constructor actually executed.
    EHScopeStack::Cleanup *cleanup = cleanupScope.getCleanup();
    EHScopeStack::Cleanup::Flags flags;
    flags.setIsNormalCleanupKind();
    flags.setIsForEarlyExit();
    Address activeFlag =
        cleanupScope.shouldTestFlagInNormalCleanup()
            ? cleanupScope.getActiveFlag()
            : Address::invalid();
    emitCleanup(*this, cleanup, flags, activeFlag);

    // After emitting a guarded cleanup inline for an early exit, clear the
    // active flag so the scope-exit cleanup (which shares the same flag)
    // does not run the destructor again. In CIR's structured scope model,
    // scope-exit cleanup code is shared by ALL exit paths. Without this,
    // a break/continue/return that emits the cleanup inline would be
    // followed by the scope-exit cleanup also running it — double
    // destruction.
    //
    // This is safe because the store is emitted at the early-exit point
    // (e.g., inside a cir.if's then-region for a conditional break), so
    // it only affects the early-exit path. The normal (fallthrough) path
    // does not execute this store, leaving the flag true for the
    // scope-exit cleanup to handle.
    if (activeFlag.isValid()) {
      auto loc = currSrcLoc ? *currSrcLoc
                            : mlir::UnknownLoc::get(builder.getContext());
      builder.createStore(loc, builder.getFalse(loc), activeFlag);
    }
  }
}

void CIRGenFunction::deactivateCleanupBlock(EHScopeStack::stable_iterator c,
                                            mlir::Operation *dominatingIP) {
  assert(c != ehStack.stable_end() && "deactivating bottom of stack?");
  EHCleanupScope &scope = cast<EHCleanupScope>(*ehStack.find(c));
  assert(scope.isActive() && "double deactivation");

  // If it's the top of the stack, just pop it, but do so only if it belongs
  // to the current RunCleanupsScope.
  if (c == ehStack.stable_begin() &&
      currentCleanupStackDepth.strictlyEncloses(c)) {
    // If it's a normal cleanup, we need to pretend that the
    // fallthrough is unreachable.
    if (!scope.isNormalCleanup() && getLangOpts().EHAsynch) {
      cgm.errorNYI("deactivateCleanupBlock: EHAsynch");
    } else {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.clearInsertionPoint();
      popCleanupBlock();
    }
    return;
  }

  // Otherwise, follow the general case.
  // For non-top-of-stack cleanups with an active flag, store false into the
  // flag to deactivate the cleanup at runtime. The EH cleanup body was already
  // emitted (wrapped in a cir.if that loads this flag), so setting the flag
  // to false prevents it from firing on the success path. The normal cleanup
  // body (emitted by popCleanupBlock) is also guarded by this flag.
  //
  // This matches OGCG's SetupCleanupBlockActivation which stores false at the
  // deactivation point.
  // A shared cir.cleanup.scope emits its cleanups once, at pop time -- which is
  // after this point. Without a flag to test, an unwind edge taken from inside
  // the range this cleanup guards would lose the destructor entirely, so
  // materialize one now: true from the push point, false from here.
  if (!scope.hasActiveFlag() && cleanupNeedsEHDeactivationFlag(*this, scope))
    giveCleanupEHDeactivationFlag(scope);

  if (scope.hasActiveFlag()) {
    auto loc =
        currSrcLoc ? *currSrcLoc : mlir::UnknownLoc::get(builder.getContext());
    builder.createStore(loc, builder.getFalse(loc), scope.getActiveFlag());
  }
  scope.setActive(false);
}

void CIRGenFunction::giveCleanupEHDeactivationFlag(EHCleanupScope &scope) {
  auto loc =
      currSrcLoc ? *currSrcLoc : mlir::UnknownLoc::get(builder.getContext());
  mlir::Type boolTy = builder.getBoolTy();
  cir::AllocaOp allocaOp = createTempAlloca(boolTy, loc, "cleanup.cond",
                                            /*arraySize=*/nullptr,
                                            /*insertIntoFnEntryBlock=*/true);
  allocaOp.setAlignmentAttr(cgm.getSize(CharUnits::One()));
  Address active(allocaOp, boolTy, CharUnits::One());

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointAfter(allocaOp);
  builder.createStore(loc, builder.getFalse(loc), active);

  // The cleanup was live from its push point on; that is also where its
  // cir.cleanup.scope range starts, so keep the store just outside the range
  // rather than letting it be swept into the scope body.
  mlir::Operation *rangeStartAfter = scope.getRangeStartAfter();
  if (rangeStartAfter)
    builder.setInsertionPointAfter(rangeStartAfter);
  else
    builder.setInsertionPointToStart(scope.getRangeStartBlock());
  mlir::Operation *activation =
      builder.createStore(loc, builder.getTrue(loc), active);
  scope.setCleanupScopeRange(scope.getRangeStartBlock(), activation);

  scope.setActiveFlag(active);
  scope.setTestFlagInEHCleanup();
}

Address CIRGenFunction::createCleanupActiveFlag(mlir::Block *initBlock) {
  auto loc =
      currSrcLoc ? *currSrcLoc : mlir::UnknownLoc::get(builder.getContext());

  // Create an i1 (bool) alloca for the cleanup guard. Must be in the
  // function entry block so it dominates all uses (the flag is read
  // outside the conditional region when popping the cleanup).
  auto boolTy = builder.getBoolTy();
  cir::AllocaOp allocaOp = createTempAlloca(boolTy, loc, "cleanup.cond",
                                            /*arraySize=*/nullptr,
                                            /*insertIntoFnEntryBlock=*/true);
  allocaOp.setAlignmentAttr(cgm.getSize(CharUnits::One()));
  Address active(allocaOp, boolTy, CharUnits::One());

  // Give the flag a defined value at function entry. A cleanup reached only
  // conditionally still has its flag *read* unconditionally: a shared
  // cir.cleanup.scope emits every cleanup pushed anywhere in its range into
  // one region, so an unwind edge taken before the conditional was even
  // evaluated -- an earlier element of the same initializer_list throwing,
  // say -- still runs that region and tests this flag. Initializing only at
  // the point the conditional starts leaves the flag undefined on those
  // edges, and the cleanup then destroys an object whose constructor never
  // ran.
  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointAfter(allocaOp);
    builder.createStore(loc, builder.getFalse(loc), active);
  }

  // The entry-block store runs once, so anything that can be evaluated more
  // than once needs the flag reset on each entry as well; otherwise it stays
  // true from the evaluation that did construct the object.
  mlir::OpBuilder::InsertPoint reinit;
  if (isInConditionalBranch()) {
    // Before each evaluation of the outermost conditional expression.
    reinit = outermostConditional->getInsertPoint();
  } else if (initBlock && initBlock != allocaOp->getBlock()) {
    // The guarded scope is entered more than once (it is nested in a loop).
    reinit = mlir::OpBuilder::InsertPoint(initBlock, initBlock->begin());
  }
  if (reinit.isSet()) {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.restoreInsertionPoint(reinit);
    builder.createStore(loc, builder.getFalse(loc), active);
  }

  // Set to true at the current point — the cleanup was just pushed,
  // meaning the temporary was actually constructed.
  builder.createStore(loc, builder.getTrue(loc), active);

  return active;
}

void CIRGenFunction::initFullExprCleanupWithFlag(Address activeFlag) {
  // Get the topmost cleanup scope and bind the active flag to it.
  EHCleanupScope &cleanup = cast<EHCleanupScope>(*ehStack.begin());
  assert(!cleanup.hasActiveFlag() && "cleanup already has active flag?");
  cleanup.setActiveFlag(activeFlag);
  if (cleanup.isNormalCleanup())
    cleanup.setTestFlagInNormalCleanup();
  if (cleanup.isEHCleanup())
    cleanup.setTestFlagInEHCleanup();
}
