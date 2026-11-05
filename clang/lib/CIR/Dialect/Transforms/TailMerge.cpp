//===- TailMerge.cpp - Cross-jumping over a flat CIR CFG ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// CIRGen emits scope cleanups inline at every exit edge: a scope with N early
// returns gets N copies of its destructor/cleanup sequence, because at CIRGen
// time each exit lives in a different region and cannot branch to a shared
// block (see emitCleanupsForEarlyExit in CIRGenCleanup.cpp). Classic codegen
// instead chains one shared cleanup block per scope depth, so it emits one
// copy.
//
// The copies are only reconcilable once the regions are gone, which is what
// this pass is for: after FlattenCFG every exit edge is a plain `cir.br` or a
// `cir.return` in one region, and the duplicated cleanups are identical tails
// of blocks that leave the same way. Merging them back into one block restores
// the classic shape.
//
// The unwind side duplicates for the same reason and gets the same treatment
// here. FlattenCFG already lets calls under one `cir.try` share a landing pad
// when their cleanup regions match, but CIRGen hands it a separate synthetic
// try per call once scopes nest, so the sharing never fires across them and
// every invoke ends up with its own byte-identical pad. Those pads are whole
// duplicated blocks rather than tails, and folding them matters more than the
// blocks they cost: the LSDA cannot coalesce the call-site records of adjacent
// invokes that unwind to different pads, so `.gcc_except_table` grows faster
// than `.text` does.
//
// Doing this in CIR rather than leaving it to LLVM matters because the win is
// not the duplicated bytes, it is the call-site count. A `__attribute__((
// cleanup))` handler or a small destructor reached from one call site gets
// inlined; reached from six it does not at -Oz, so it survives out of line with
// a frame and an FDE each, and the address-taken variable it cleans up stays in
// memory instead of being promoted. LLVM's own tail merging runs far too late
// to feed the inliner, and SimplifyCFG's sinking gives up as soon as one
// predecessor's tail diverges (the shallowest exit runs fewer cleanups, so one
// always does).
//
//===----------------------------------------------------------------------===//

#include "PassDetail.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"

#include "mlir/IR/Block.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/TimeProfiler.h"

#include <memory>
#include <queue>
#include <vector>

using namespace mlir;
using namespace cir;

namespace mlir {
#define GEN_PASS_DEF_CIRTAILMERGE
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

namespace {

/// The two ways out of a block whose tail this pass can move: an unconditional
/// `cir.br`, and a `cir.return`. A conditional or multi-way terminator would
/// have to be split before its tail could go anywhere, which buys nothing here.
static mlir::Operation *getMergeableExit(mlir::Block *block) {
  if (block->empty())
    return nullptr;
  mlir::Operation *term = &block->back();
  return mlir::isa<cir::BrOp, cir::ReturnOp>(term) ? term : nullptr;
}

/// The operations of \p block that are candidates for merging: everything but
/// the terminator, innermost-last.
static llvm::iterator_range<mlir::Block::iterator> bodyOps(mlir::Block *block) {
  return {block->begin(), std::prev(block->end())};
}

static bool hasOnlyEmptyRegions(mlir::Operation *op) {
  return llvm::all_of(op->getRegions(),
                      [](mlir::Region &region) { return region.empty(); });
}

/// `_Defer` bodies are duplicated per exit edge the same way cleanups are, but
/// that feature already folds its own copies back together during LLVM
/// lowering, using a dispatch block and a `cleanup.dest` switch driven by this
/// tag. Leave those calls alone rather than racing its lowering to the same
/// result.
static bool isDeferDispatchCandidate(mlir::Operation *op) {
  return op->hasAttr("cir.defer_cleanup_dispatch_candidate");
}

/// Walk two blocks backwards from their terminators and return how many
/// trailing operations compute the same thing.
///
/// Two operations pair up when they are structurally equal and their operands
/// are either the exact same SSA value or the results of another paired
/// operation. Requiring the *same* value for anything defined outside the
/// suffix is what makes the move safe: a value both blocks already use has to
/// dominate both of them, so it also dominates the merged block, whose only
/// predecessors are those two blocks.
///
/// The suffix additionally has to be self-contained. An operation whose result
/// escapes -- used by the terminator, or by any block the predecessor
/// dominates -- cannot move, because the merged block does not dominate the
/// successor (which keeps its other predecessors).
static size_t commonTailLength(mlir::Block *lhs, mlir::Block *rhs) {
  mlir::Operation *lhsExit = getMergeableExit(lhs);
  mlir::Operation *rhsExit = getMergeableExit(rhs);
  assert(lhsExit && rhsExit && "callers filter for mergeable exits");

  // The merged block keeps lhs's terminator and drops rhs's, so the two have to
  // be interchangeable down to their attributes. Operands are exempt: they
  // become block arguments the predecessors forward.
  auto anyOperandsMatch = [](mlir::Value, mlir::Value) {
    return mlir::success();
  };
  if (!mlir::OperationEquivalence::isEquivalentTo(
          lhsExit, rhsExit, anyOperandsMatch, /*markEquivalent=*/nullptr,
          mlir::OperationEquivalence::IgnoreLocations))
    return 0;

  // Phase 1: how far back do the blocks agree on *what* they do, ignoring
  // which values they do it to. Operands cannot be judged during this walk:
  // going backwards reaches a use before the definition it refers to, and
  // pairing that definition up is exactly what a later step of the walk does.
  llvm::SmallVector<std::pair<mlir::Operation *, mlir::Operation *>, 8> pairs;
  auto lhsIt = bodyOps(lhs).end();
  auto rhsIt = bodyOps(rhs).end();
  while (lhsIt != bodyOps(lhs).begin() && rhsIt != bodyOps(rhs).begin()) {
    mlir::Operation *lhsOp = &*std::prev(lhsIt);
    mlir::Operation *rhsOp = &*std::prev(rhsIt);

    // `cir.call` always carries a `cleanup` region, empty on the normal path,
    // so regions cannot be rejected outright. A populated one is a different
    // matter: phase 2 below only validates the operands of the operations it
    // walks, not of anything nested inside them.
    if (!hasOnlyEmptyRegions(lhsOp) || !hasOnlyEmptyRegions(rhsOp))
      break;

    if (isDeferDispatchCandidate(lhsOp) || isDeferDispatchCandidate(rhsOp))
      break;

    if (!mlir::OperationEquivalence::isEquivalentTo(
            lhsOp, rhsOp, anyOperandsMatch, /*markEquivalent=*/nullptr,
            mlir::OperationEquivalence::IgnoreLocations))
      break;

    pairs.emplace_back(lhsOp, rhsOp);
    lhsIt = std::prev(lhsIt);
    rhsIt = std::prev(rhsIt);
  }

  // Whatever the terminators forward -- successor arguments, or the returned
  // value -- stays behind in the predecessors, so nothing it depends on may
  // move.
  llvm::DenseSet<mlir::Value> pinned;
  pinned.insert(lhsExit->operand_begin(), lhsExit->operand_end());
  pinned.insert(rhsExit->operand_begin(), rhsExit->operand_end());

  // Phase 2: shrink the candidate until the operands agree and nothing leaks
  // out. `pairs[0]` is the last operation, so an operation that fails at index
  // i leaves the i operations behind it -- dropping it can invalidate whatever
  // consumed its result, hence the re-check.
  size_t length = pairs.size();
  bool hasCall = false;
  bool allMarkers = false;
  while (length) {
    llvm::DenseSet<mlir::Operation *> suffix;
    // rhs value -> the lhs value it pairs with.
    llvm::DenseMap<mlir::Value, mlir::Value> pairing;
    for (size_t i = 0; i != length; ++i) {
      suffix.insert(pairs[i].first);
      suffix.insert(pairs[i].second);
      for (auto [lhsResult, rhsResult] : llvm::zip(
               pairs[i].first->getResults(), pairs[i].second->getResults()))
        pairing[rhsResult] = lhsResult;
    }

    // Both sides have to be free to leave: lhs's operations are spliced into
    // the merged block, rhs's are deleted outright.
    auto escapes = [&](mlir::Operation *op) {
      for (mlir::Value result : op->getResults()) {
        if (pinned.contains(result))
          return true;
        for (mlir::Operation *user : result.getUsers())
          if (!suffix.contains(user))
            return true;
      }
      return false;
    };

    size_t rejected = length;
    hasCall = false;
    allMarkers = true;
    for (size_t i = 0; i != length; ++i) {
      auto [lhsOp, rhsOp] = pairs[i];
      bool ok = !escapes(lhsOp) && !escapes(rhsOp);
      if (ok)
        for (auto [lhsVal, rhsVal] :
             llvm::zip(lhsOp->getOperands(), rhsOp->getOperands()))
          if (lhsVal != rhsVal && pairing.lookup(rhsVal) != lhsVal) {
            ok = false;
            break;
          }
      if (!ok) {
        rejected = i;
        break;
      }
      hasCall |= mlir::isa<mlir::CallOpInterface>(lhsOp);
      allMarkers &= mlir::isa<cir::LifetimeStartOp, cir::LifetimeEndOp>(lhsOp);
    }
    if (rejected == length)
      break;
    length = rejected;
  }

  // Only merge tails that carry a call, or that are nothing but lifetime
  // markers. The point of the pass is to get a cleanup handler back down to
  // one call site so the inliner takes it; a tail of pure arithmetic is
  // already free, and merging it just churns the CFG for LLVM to undo.
  //
  // Markers are the other case worth taking, for the opposite reason: they
  // cost nothing themselves, but a trivially-destructed scope with several
  // early exits leaves one copy on each, and a `lifetime.end` sitting between
  // a branch and its destination is enough to stop SimplifyCFG from folding
  // two exits that go to the same place into a `select`. Sinking is what would
  // normally clear them out of the way, and it gives up here: the successor
  // also has predecessors from outside the scope, which end no lifetime at
  // all. Pairwise merging is not all-or-nothing, so it gets the exits that do
  // agree down to one block and lets the fold happen.
  return hasCall || allMarkers ? length : 0;
}

/// Whether \p block now holds nothing but an argument-less branch, which is
/// what a block whose entire body moved into a merge is left with.
static bool isBareForwarder(mlir::Block *block) {
  if (block->isEntryBlock() || !block->getArguments().empty())
    return false;
  if (!llvm::hasSingleElement(*block))
    return false;
  auto br = mlir::dyn_cast<cir::BrOp>(block->back());
  return br && br.getDestOperands().empty();
}

/// Move the common tail of \p lhs and \p rhs into a fresh block that both then
/// branch to. The merged block inherits lhs's terminator, so it leaves the
/// function exactly the way its two predecessors used to; whatever they
/// forwarded through that terminator becomes a block argument.
///
/// Blocks left holding nothing but the branch to the merge are appended to
/// \p emptied for the caller to fold away once the merging has settled.
/// Returns the fresh block the tail moved into.
static mlir::Block *mergeTail(mlir::Block *lhs, mlir::Block *rhs, size_t length,
                              llvm::SmallVectorImpl<mlir::Block *> &emptied) {
  mlir::Operation *lhsExit = getMergeableExit(lhs);
  mlir::Operation *rhsExit = getMergeableExit(rhs);
  llvm::SmallVector<mlir::Value, 4> lhsArgs(lhsExit->getOperands());
  llvm::SmallVector<mlir::Value, 4> rhsArgs(rhsExit->getOperands());
  mlir::Location lhsLoc = lhsExit->getLoc();
  mlir::Location rhsLoc = rhsExit->getLoc();

  auto *merged = new mlir::Block();
  for (mlir::Value arg : lhsArgs)
    merged->addArgument(arg.getType(), arg.getLoc());
  // Sit next to the block being cloned from, which keeps the cleanup chain
  // reading in the order it runs.
  lhs->getParent()->getBlocks().insert(std::next(lhs->getIterator()), merged);

  // lhs donates the operations; rhs's copies are dead once both branch here.
  // Splice rather than clone so uses inside the tail stay wired up.
  merged->getOperations().splice(merged->end(), lhs->getOperations(),
                                 std::prev(bodyOps(lhs).end(), length),
                                 bodyOps(lhs).end());

  // Drop rhs's copies before rewiring, so the terminator swap below does not
  // have to reason about what is still sitting in front of it. Reverse order
  // keeps every remaining operation's operands defined as we go.
  llvm::SmallVector<mlir::Operation *, 8> dead;
  for (mlir::Operation &op : llvm::make_range(
           std::prev(bodyOps(rhs).end(), length), bodyOps(rhs).end()))
    dead.push_back(&op);
  for (mlir::Operation *op : llvm::reverse(dead)) {
    assert(op->use_empty() && "tail operations were checked for escapes");
    op->erase();
  }

  lhsExit->moveBefore(merged, merged->end());
  lhsExit->setOperands(merged->getArguments());

  mlir::OpBuilder builder(merged->getParent()->getContext());
  builder.setInsertionPointToEnd(lhs);
  cir::BrOp::create(builder, lhsLoc, merged, lhsArgs);

  builder.setInsertionPoint(rhsExit);
  cir::BrOp::create(builder, rhsLoc, merged, rhsArgs);
  rhsExit->erase();

  for (mlir::Block *block : {lhs, rhs})
    if (isBareForwarder(block))
      emptied.push_back(block);
  return merged;
}

/// Splice \p emptied out of the CFG by pointing their predecessors straight at
/// where they branch. Later merges can refill a block, and a chain of them can
/// close into a loop (`for (;;) {}` flattens to exactly that), so resolve each
/// one to a destination that is staying put and skip anything cyclic.
static void foldForwarders(llvm::ArrayRef<mlir::Block *> emptied) {
  // A SetVector, not a DenseSet: the order the forwarders are redirected in is
  // the order their predecessors join the destination's use list, which later
  // merging and the phis of the LLVM translation read. A pointer-hashed order
  // follows the heap, and made the output differ run to run.
  llvm::SetVector<mlir::Block *> forwarders;
  for (mlir::Block *block : emptied)
    if (isBareForwarder(block))
      forwarders.insert(block);

  llvm::SmallVector<std::pair<mlir::Block *, mlir::Block *>, 8> resolved;
  for (mlir::Block *block : forwarders) {
    llvm::SmallPtrSet<mlir::Block *, 8> seen;
    mlir::Block *dest = block;
    while (forwarders.contains(dest)) {
      if (!seen.insert(dest).second) {
        dest = nullptr;
        break;
      }
      dest = mlir::cast<cir::BrOp>(dest->back()).getDest();
    }
    if (dest)
      resolved.emplace_back(block, dest);
  }

  // Redirect everything first. That also rewrites the forwarders' own branches
  // (a forwarder pointing at a forwarder is one of its uses), so by the time
  // anything is erased no block refers to a block that is going away.
  for (auto [block, dest] : resolved)
    block->replaceAllUsesWith(dest);
  for (auto [block, dest] : resolved)
    block->erase();
}

/// The blocks that leave \p body the same way: the predecessors that branch
/// unconditionally to \p successor, or -- when \p successor is null -- every
/// block that returns.
static void collectExitGroup(mlir::Region &body, mlir::Block *successor,
                             llvm::SmallVectorImpl<mlir::Block *> &group) {
  if (!successor) {
    for (mlir::Block &block : body)
      if (mlir::isa_and_nonnull<cir::ReturnOp>(getMergeableExit(&block)))
        group.push_back(&block);
    return;
  }
  for (mlir::Block *pred : successor->getPredecessors()) {
    if (pred == successor)
      continue;
    mlir::Operation *exit = getMergeableExit(pred);
    auto br = exit ? mlir::dyn_cast<cir::BrOp>(exit) : cir::BrOp{};
    if (br && br.getDest() == successor)
      group.push_back(pred);
  }
}

/// Merge the tails within one exit group until nothing more pairs up.
///
/// Candidates come off a longest-first worklist rather than the first
/// available pair: a shorter merge done early would fix the shared tail in
/// place and block the longer one.
///
/// Each pair is measured once, up front. Merging two members only moves and
/// deletes operations of those two blocks -- every other member keeps its
/// operations, its terminator, and the users of its results, so the
/// common-tail length of every other pair is exactly what it was when first
/// measured. Each merge then only adds the pairs involving the fresh block.
/// Re-scanning all pairs after every merge is cubic in the group size, and a
/// function whose switch breaks a thousand cases to one block spends minutes
/// re-measuring pairs whose answer cannot have changed.
///
/// Ties break by collection order so the pass stays deterministic. A merged
/// block ranks after every block collected before it, which can order
/// equal-length merges differently than a round re-scan would; either order
/// merges the same tails, only which copy survives -- and hence locations --
/// can differ.
static bool mergeExitGroup(mlir::Region &body, mlir::Block *successor,
                           llvm::SmallVectorImpl<mlir::Block *> &emptied) {
  llvm::SmallVector<mlir::Block *, 8> group;
  collectExitGroup(body, successor, group);
  if (group.size() < 2)
    return false;

  llvm::DenseMap<mlir::Block *, size_t> rank;
  for (size_t i = 0; i != group.size(); ++i)
    rank[group[i]] = i;

  struct Candidate {
    size_t length;
    size_t first; // Lower collection rank: the scan-order tie-break.
    size_t second;
    mlir::Block *lhs; // Lower rank, matching the (i, j) scan with i < j.
    mlir::Block *rhs;
  };
  struct LongestFirst {
    bool operator()(const Candidate &a, const Candidate &b) const {
      if (a.length != b.length)
        return a.length < b.length;
      if (a.first != b.first)
        return a.first > b.first;
      return a.second > b.second;
    }
  };
  std::priority_queue<Candidate, std::vector<Candidate>, LongestFirst> worklist;
  for (size_t i = 0; i != group.size(); ++i)
    for (size_t j = i + 1; j != group.size(); ++j)
      if (size_t length = commonTailLength(group[i], group[j]))
        worklist.push({length, i, j, group[i], group[j]});

  llvm::DenseSet<mlir::Block *> live;
  live.insert(group.begin(), group.end());
  size_t nextRank = group.size();
  bool changed = false;
  while (!worklist.empty()) {
    Candidate next = worklist.top();
    worklist.pop();
    if (!live.contains(next.lhs) || !live.contains(next.rhs))
      continue;
    mlir::Block *merged = mergeTail(next.lhs, next.rhs, next.length, emptied);
    live.erase(next.lhs);
    live.erase(next.rhs);
    live.insert(merged);
    size_t mergedRank = nextRank++;
    rank[merged] = mergedRank;
    // The merged block always ranks last, so the surviving member donates
    // its operations and the merged block's copies are the ones dropped.
    for (mlir::Block *other : live) {
      if (other == merged)
        continue;
      size_t otherRank = rank[other];
      if (size_t length = commonTailLength(other, merged))
        worklist.push({length, otherRank, mergedRank, other, merged});
    }
    changed = true;
  }
  return changed;
}

/// Whether \p block is an unwind destination. `cir.eh.inflight_exception` is
/// required to be the first operation of one, and a pad is branched to only by
/// the unwind edge of a `cir.try_call`, so it never takes arguments.
static bool isLandingPad(mlir::Block *block) {
  return !block->empty() && block->getArguments().empty() &&
         mlir::isa<cir::EhInflightOp>(block->front());
}

/// Whether \p lhs and \p rhs are the same block written out twice, so that
/// redirecting rhs's predecessors at lhs changes nothing.
///
/// Unlike a tail merge this can walk forwards, which makes the operand check
/// a single pass: a definition is always seen before its uses, so by the time
/// an operand is examined it has either already been paired with its opposite
/// number or it comes from outside both blocks. Coming from outside, it has to
/// be the *same* value on both sides -- which then also settles dominance,
/// since a value both blocks use dominates every predecessor either of them
/// has, and those are exactly the predecessors lhs ends up with.
static bool blocksAreIdentical(mlir::Block *lhs, mlir::Block *rhs) {
  // rhs value -> the lhs value it pairs with.
  llvm::DenseMap<mlir::Value, mlir::Value> pairing;
  auto checkPaired = [&](mlir::Value lhsVal, mlir::Value rhsVal) {
    return mlir::success(pairing.lookup(rhsVal) == lhsVal);
  };

  auto lhsIt = lhs->begin();
  auto rhsIt = rhs->begin();
  for (; lhsIt != lhs->end() && rhsIt != rhs->end(); ++lhsIt, ++rhsIt) {
    mlir::Operation *lhsOp = &*lhsIt;
    mlir::Operation *rhsOp = &*rhsIt;

    // Nothing nested is inspected below, so a populated region -- a `cir.call`
    // carrying cleanup code, say -- is out of scope.
    if (!hasOnlyEmptyRegions(lhsOp) || !hasOnlyEmptyRegions(rhsOp))
      return false;

    if (isDeferDispatchCandidate(lhsOp) || isDeferDispatchCandidate(rhsOp))
      return false;

    if (!mlir::OperationEquivalence::isEquivalentTo(
            lhsOp, rhsOp, checkPaired, /*markEquivalent=*/nullptr,
            mlir::OperationEquivalence::IgnoreLocations))
      return false;

    // Structural equivalence counts an operation's successors without looking
    // at which blocks they are, and leaving the same way is the whole point.
    for (auto [lhsSucc, rhsSucc] :
         llvm::zip(lhsOp->getSuccessors(), rhsOp->getSuccessors()))
      if (lhsSucc != rhsSucc)
        return false;

    // rhs is deleted whole, so anything it defines has to die with it.
    for (mlir::Value result : rhsOp->getResults())
      for (mlir::Operation *user : result.getUsers())
        if (user->getBlock() != rhs)
          return false;

    for (auto [lhsResult, rhsResult] :
         llvm::zip(lhsOp->getResults(), rhsOp->getResults()))
      pairing[rhsResult] = lhsResult;
  }
  return lhsIt == lhs->end() && rhsIt == rhs->end();
}

/// Point every invoke that unwinds to a duplicate landing pad at the first pad
/// that says the same thing.
///
/// The duplicates are replaced outright instead of being split off into a
/// shared block the way a tail merge is: an unwind destination has to *begin*
/// with its landing pad, so leaving behind a block that only branches to the
/// merged one would not be legal until the fold cleaned it up.
static bool mergeLandingPads(mlir::Region &body) {
  llvm::SmallVector<mlir::Block *, 8> pads;
  for (mlir::Block &block : body)
    if (isLandingPad(&block))
      pads.push_back(&block);

  bool changed = false;
  for (size_t i = 0; i != pads.size(); ++i) {
    if (!pads[i])
      continue;
    for (size_t j = i + 1; j != pads.size(); ++j) {
      if (!pads[j] || !blocksAreIdentical(pads[i], pads[j]))
        continue;
      pads[j]->replaceAllUsesWith(pads[i]);
      pads[j]->erase();
      pads[j] = nullptr;
      changed = true;
    }
  }
  return changed;
}

struct CIRTailMergePass : public impl::CIRTailMergeBase<CIRTailMergePass> {
  CIRTailMergePass() = default;
  void runOnOperation() override;
};

void CIRTailMergePass::runOnOperation() {
  llvm::TimeTraceScope scope("CIR Tail Merge");

  getOperation()->walk([&](cir::FuncOp func) {
    mlir::Region &body = func.getBody();
    if (body.empty())
      return;

    // A merge introduces a block that is itself a candidate within the same
    // group, and mergeExitGroup already loops on that. What it cannot see is a
    // tail that only becomes common once a *different* group collapses -- the
    // nested-scope case, where merging the inner cleanup is what leaves the
    // outer one identical -- so iterate the whole function to a fixed point.
    llvm::SmallVector<mlir::Block *, 8> emptied;
    bool changed = true;
    while (changed) {
      changed = false;
      // Ahead of the tail merging, because collapsing the pads is what leaves
      // their successors with the same set of predecessors to merge across.
      changed |= mergeLandingPads(body);
      llvm::SmallVector<mlir::Block *, 32> successors;
      // Null stands for the group of returning blocks, which have no successor
      // to key off.
      successors.push_back(nullptr);
      for (mlir::Block &block : body)
        successors.push_back(&block);
      for (mlir::Block *successor : successors)
        changed |= mergeExitGroup(body, successor, emptied);
    }
    foldForwarders(emptied);
  });
}

} // namespace

std::unique_ptr<Pass> mlir::createCIRTailMergePass() {
  return std::make_unique<CIRTailMergePass>();
}
