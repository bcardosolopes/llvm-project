//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements pass that inlines CIR operations regions into the parent
// function region.
//
//===----------------------------------------------------------------------===//

#include "PassDetail.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/RegionGraphTraits.h"
#include "mlir/IR/Threading.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/Support/TimeProfiler.h"

#include <atomic>

using namespace mlir;
using namespace cir;

namespace mlir {
#define GEN_PASS_DEF_CIRFLATTENCFG
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

namespace {

constexpr llvm::StringLiteral kCaseLikelihoodsAttr = "cir.case_likelihoods";

static bool containsNestedOpenMPOps(cir::ScopeOp scopeOp) {
  return scopeOp->walk([&](Operation *nestedOp) {
           if (nestedOp != scopeOp.getOperation() && nestedOp->getDialect() &&
               isa<omp::OpenMPDialect>(nestedOp->getDialect()))
             return WalkResult::interrupt();
           return WalkResult::advance();
         }).wasInterrupted();
}
constexpr llvm::StringLiteral kCatchBeginFnAttr = "cir.catch_begin_fn";
constexpr llvm::StringLiteral kCatchEndFnAttr = "cir.catch_end_fn";
constexpr llvm::StringLiteral kCatchRawHandlerAttr = "cir.raw_handler";
constexpr llvm::StringLiteral kObjCDeadCatchesAttr = "cir.objc_dead_catches";
constexpr llvm::StringLiteral kShareSyntheticCleanupAttr =
    "cir.share_synthetic_cleanup";
constexpr llvm::StringLiteral kPreserveDeadObjCCatchBlockAttr =
    "cir.preserve_dead_objc_catch";
constexpr llvm::StringLiteral kPreserveDeadUBSanUnreachableBlockAttr =
    "cir.preserve_dead_ubsan_unreachable";
// Marks the is_in_ramp-guarded delayed get-return-object conversion cir.if
// emitted by CIRGenFunction::emitCoroutineBody. FlattenCFG uses it to (a) keep
// the original from being flattened prematurely during the greedy pass and
// (b) clone the conversion onto every coroutine suspend-return-to-caller edge
// so a coroutine that suspends (suspend_always) still runs the GRO->return
// converting ctor on the ramp, matching classic CodeGen's placement at
// Coro.SuspendBB. Kept in sync with CIRGenCoroutine.cpp.
constexpr llvm::StringLiteral kCoroGroConvAttr = "cir.coro_gro_conv";

enum class CaseLikelihood { None, Likely, Unlikely };

static CaseLikelihood getCaseLikelihood(cir::CaseOp caseOp, unsigned index) {
  auto likelihoods =
      caseOp->getAttrOfType<mlir::ArrayAttr>(kCaseLikelihoodsAttr);
  if (!likelihoods || index >= likelihoods.size())
    return CaseLikelihood::None;
  auto lh = static_cast<clang::Stmt::Likelihood>(
      mlir::cast<mlir::IntegerAttr>(likelihoods[index]).getInt());
  switch (lh) {
  case clang::Stmt::LH_Likely:
    return CaseLikelihood::Likely;
  case clang::Stmt::LH_Unlikely:
    return CaseLikelihood::Unlikely;
  case clang::Stmt::LH_None:
    return CaseLikelihood::None;
  }
  llvm_unreachable("unexpected likelihood");
}

static std::optional<llvm::SmallVector<int32_t>>
getLikelihoodWeights(llvm::ArrayRef<CaseLikelihood> likelihoods) {
  if (likelihoods.size() <= 1)
    return std::nullopt;

  uint64_t numUnlikely = 0;
  uint64_t numNone = 0;
  uint64_t numLikely = 0;
  for (CaseLikelihood lh : likelihoods) {
    switch (lh) {
    case CaseLikelihood::Unlikely:
      ++numUnlikely;
      break;
    case CaseLikelihood::None:
      ++numNone;
      break;
    case CaseLikelihood::Likely:
      ++numLikely;
      break;
    }
  }

  if (numUnlikely == 0 && numLikely == 0)
    return std::nullopt;

  const uint64_t likely = INT32_MAX / (numLikely + 2);
  const uint64_t none = likely / (numNone + 1);
  const uint64_t unlikely = 0;

  llvm::SmallVector<int32_t> result;
  result.reserve(likelihoods.size());
  for (CaseLikelihood lh : likelihoods) {
    switch (lh) {
    case CaseLikelihood::Unlikely:
      result.push_back(static_cast<int32_t>(unlikely));
      break;
    case CaseLikelihood::None:
      result.push_back(static_cast<int32_t>(none));
      break;
    case CaseLikelihood::Likely:
      result.push_back(static_cast<int32_t>(likely));
      break;
    }
  }

  return result;
}

static mlir::ArrayAttr getBranchWeightsAttr(mlir::MLIRContext *ctx,
                                            llvm::ArrayRef<int32_t> weights) {
  llvm::SmallVector<mlir::Attribute> attrs;
  attrs.reserve(weights.size());
  auto i32Ty = mlir::IntegerType::get(ctx, 32);
  for (int32_t weight : weights)
    attrs.push_back(mlir::IntegerAttr::get(i32Ty, weight));
  return mlir::ArrayAttr::get(ctx, attrs);
}

static bool shouldEmitLikelihoodInfo(mlir::Operation *op) {
  if (mlir::ModuleOp mod = op->getParentOfType<mlir::ModuleOp>()) {
    if (auto optInfoAttr = mlir::cast_if_present<cir::OptInfoAttr>(
            mod->getAttr(cir::CIRDialect::getOptInfoAttrName())))
      return optInfoAttr.getLevel() > 0 || optInfoAttr.getSize() > 0;
  }
  return false;
}

constexpr llvm::StringLiteral kDILocationAtomGroupKey = "llvm.di_atom_group";
constexpr llvm::StringLiteral kDILocationAtomRankKey = "llvm.di_atom_rank";

static uint64_t getAtomGroupFromLoc(mlir::Location loc) {
  if (auto fusedLoc = mlir::dyn_cast<mlir::FusedLoc>(loc))
    if (auto dict =
            mlir::dyn_cast_or_null<mlir::DictionaryAttr>(fusedLoc.getMetadata()))
      if (auto atomGroup = dict.getAs<mlir::IntegerAttr>(kDILocationAtomGroupKey))
        if (atomGroup.getValue().isNonNegative())
          return atomGroup.getValue().getZExtValue();
  return 0;
}

static std::optional<mlir::Location>
getNestedLocWithAtomMetadata(mlir::Location loc) {
  auto fusedLoc = mlir::dyn_cast<mlir::FusedLoc>(loc);
  if (!fusedLoc || fusedLoc.getLocations().empty())
    return std::nullopt;

  mlir::Location nestedLoc = fusedLoc.getLocations().front();
  if (!getAtomGroupFromLoc(nestedLoc))
    return std::nullopt;
  return nestedLoc;
}

static mlir::Location applySourceAtomMetadata(mlir::PatternRewriter &rewriter,
                                              mlir::Location loc,
                                              uint64_t group,
                                              uint8_t rank) {
  if (!group || !rank)
    return loc;

  auto dict = rewriter.getDictionaryAttr(
      {rewriter.getNamedAttr(
           rewriter.getStringAttr(kDILocationAtomGroupKey),
           rewriter.getI64IntegerAttr(static_cast<int64_t>(group))),
       rewriter.getNamedAttr(rewriter.getStringAttr(kDILocationAtomRankKey),
                             rewriter.getI8IntegerAttr(rank))});
  return mlir::FusedLoc::get({loc}, dict, rewriter.getContext());
}

static mlir::Location stripOuterSourceAtomMetadata(mlir::Location loc) {
  auto fusedLoc = mlir::dyn_cast<mlir::FusedLoc>(loc);
  if (!fusedLoc || fusedLoc.getLocations().empty())
    return loc;

  auto dict =
      mlir::dyn_cast_or_null<mlir::DictionaryAttr>(fusedLoc.getMetadata());
  if (!dict || !dict.get(kDILocationAtomGroupKey))
    return loc;

  return fusedLoc.getLocations().front();
}

static mlir::Location getPreferredCondBrLoc(mlir::Value cond,
                                            mlir::Location fallbackLoc) {
  mlir::Location condLoc = stripOuterSourceAtomMetadata(cond.getLoc());
  if (auto fileLoc = condLoc->findInstanceOf<mlir::FileLineColLoc>())
    if (fileLoc.getLine())
      return condLoc;
  return stripOuterSourceAtomMetadata(fallbackLoc);
}

static mlir::Location getFunctionEndLoc(cir::FuncOp fn) {
  if (auto fusedLoc = mlir::dyn_cast<mlir::FusedLoc>(fn.getLoc()))
    if (!fusedLoc.getLocations().empty())
      return fusedLoc.getLocations().back();
  return fn.getLoc();
}

static uint64_t getMaxSourceAtomGroup(cir::FuncOp fn) {
  uint64_t maxGroup = 0;
  fn.walk([&](mlir::Operation *op) {
    maxGroup = std::max(maxGroup, getAtomGroupFromLoc(op->getLoc()));
  });
  return maxGroup;
}

static void eraseDeadBlock(mlir::RewriterBase &rewriter, mlir::Block &block) {
  // Keep greedy rewrite bookkeeping in sync when deleting a dead block that
  // may still contain nested structured ops already queued on the worklist.
  block.dropAllDefinedValueUses();
  rewriter.eraseBlock(&block);
}

// A block inside a catch-handler region can hold a cir.label that is the target
// of a cir.goto elsewhere in the function. Before GotoSolver runs (immediately
// after FlattenCFG) the goto is still symbolic and creates no CFG edge, so such
// a label block looks predecessor-less. It must NOT be erased as dead: dropping
// it would leave the goto with no target and abort GotoSolver ("goto label not
// found"). Mirrors the label-block preservation in CIRSwitchOpFlattening.
static bool blockHoldsLabel(mlir::Block &block) {
  return llvm::any_of(block.getOperations(),
                      [](mlir::Operation &o) { return isa<cir::LabelOp>(o); });
}

// Return true if `gotoOp` targets a `cir.label` defined somewhere within
// `handlerRegion`. Such a goto jumps *within* the catch handler (e.g. a forward
// goto over a later declaration) rather than leaving it, so it must NOT be
// treated as a handler-exit that needs an __cxa_end_catch emitted before it:
// the caught exception object stays alive until control actually leaves the
// handler at its closing brace. Emitting end_catch on an intra-handler goto
// destroys the exception object too early and mis-orders it before the
// catch-body destructors (a silent wrong-dtor-order miscompile).
static bool gotoTargetsLabelInRegion(cir::GotoOp gotoOp,
                                     mlir::Region &handlerRegion) {
  llvm::StringRef target = gotoOp.getLabel();
  return handlerRegion
      .walk([&](cir::LabelOp labelOp) {
        return labelOp.getLabel() == target ? mlir::WalkResult::interrupt()
                                            : mlir::WalkResult::advance();
      })
      .wasInterrupted();
}

static void unifyRetvalMultiReturns(cir::FuncOp fn) {
  auto fnType = fn.getFunctionType();
  if (!fnType.getReturnType())
    return;

  // Compute the next free key-instruction atom group ID (used only when the
  // function already participates in key-instruction debug metadata). When the
  // function has no atom groups (e.g. release builds without -g), this stays
  // at zero and we'll skip the FusedLoc decoration below — but the unification
  // itself still runs, because it has a real codegen-shape benefit beyond DI:
  // a single retval load lets SROA propagate scalar fields of the return slot
  // through every return path. With multiple returns each loading the slot as
  // an aggregate, SROA bails on splitting and downstream LLVM is forced to
  // materialize/destructure {iN, ptr}-style aggregate phis, defeating the
  // SROA-friendly shape that classic codegen produces (e.g. via OGCG's single
  // ReturnBlock + EmitBranchThroughCleanup). This was the root cause of the
  // DuckDB hashjoin_dups_rhs +9% regression on string_t-returning callees.
  uint64_t newAtomGroup = getMaxSourceAtomGroup(fn);

  llvm::SmallVector<cir::ReturnOp> returns;
  fn.walk([&](cir::ReturnOp ret) { returns.push_back(ret); });
  if (returns.size() <= 1)
    return;

  // Strip cir.bitcast on a pointer SSA value to find the underlying address.
  // Each return path may load through its own bitcast of the same __retval
  // alloca (e.g., when the return-by-coercion ABI shape is layered on top of
  // the structurally-typed slot via union/aggregate punning), so comparing
  // raw load addresses would miss the equivalence.
  auto stripPtrBitcasts = [](mlir::Value v) -> mlir::Value {
    while (auto cast = v.getDefiningOp<cir::CastOp>()) {
      if (cast.getKind() != cir::CastKind::bitcast)
        break;
      v = cast.getSrc();
    }
    return v;
  };

  // Track the underlying alloca address (used for equivalence). The actual
  // load may be through a chain of bitcasts (when the ABI return type
  // differs from the alloca type, e.g. record-typed __retval slot loaded as
  // a coerced { iN, ptr } return value); we recreate the bitcast at the
  // unified-return block so the new load matches the function return type.
  mlir::Value retvalAddr = nullptr;
  // Pointer type at the load site for the first return (the type the load
  // expects to read through), used to re-issue the bitcast in the new block.
  mlir::Type firstLoadAddrType = nullptr;
  llvm::SmallVector<cir::LoadOp> returnLoads;
  for (cir::ReturnOp ret : returns) {
    if (ret.getInput().size() != 1)
      return;
    auto load = ret.getInput().front().template getDefiningOp<cir::LoadOp>();
    if (!load)
      return;
    mlir::Value addr = stripPtrBitcasts(load.getAddr());
    if (!retvalAddr) {
      retvalAddr = addr;
      firstLoadAddrType = load.getAddr().getType();
    } else if (addr != retvalAddr) {
      return;
    }
    returnLoads.push_back(load);
  }

  mlir::OpBuilder builder(fn.getContext());
  mlir::Block *retBlock = builder.createBlock(&fn.getBody());
  mlir::Location finalLoc = getFunctionEndLoc(fn);
  if (newAtomGroup)
    ++newAtomGroup;

  builder.setInsertionPointToStart(retBlock);
  // If the original loads went through a chain of bitcasts, recreate a single
  // bitcast at the unified return block so the new load has the right
  // pointer-element type. The unified ReturnOp consumes loads typed as the
  // function's return type.
  mlir::Value loadAddr = retvalAddr;
  if (firstLoadAddrType && firstLoadAddrType != retvalAddr.getType()) {
    loadAddr = cir::CastOp::create(builder, finalLoc, firstLoadAddrType,
                                   cir::CastKind::bitcast, retvalAddr);
  }
  auto finalLoad =
      cir::LoadOp::create(builder,
                          newAtomGroup
                              ? mlir::FusedLoc::get(
                                    {finalLoc},
                                    mlir::DictionaryAttr::get(
                                        fn.getContext(),
                                        {builder.getNamedAttr(
                                             builder.getStringAttr(
                                                 kDILocationAtomGroupKey),
                                             builder.getI64IntegerAttr(
                                                 static_cast<int64_t>(
                                                     newAtomGroup))),
                                         builder.getNamedAttr(
                                             builder.getStringAttr(
                                                 kDILocationAtomRankKey),
                                             builder.getI8IntegerAttr(2))}),
                                    fn.getContext())
                              : finalLoc,
                          fnType.getReturnType(), loadAddr);
  auto finalRetLoc = newAtomGroup
                         ? mlir::FusedLoc::get(
                               {finalLoc},
                               mlir::DictionaryAttr::get(
                                   fn.getContext(),
                                   {builder.getNamedAttr(
                                        builder.getStringAttr(
                                            kDILocationAtomGroupKey),
                                        builder.getI64IntegerAttr(
                                            static_cast<int64_t>(newAtomGroup))),
                                    builder.getNamedAttr(
                                        builder.getStringAttr(
                                            kDILocationAtomRankKey),
                                        builder.getI8IntegerAttr(1))}),
                               fn.getContext())
                         : finalLoc;
  cir::ReturnOp::create(builder, finalRetLoc, finalLoad.getResult());

  for (auto [ret, load] : llvm::zip_equal(returns, returnLoads)) {
    builder.setInsertionPoint(ret);
    cir::BrOp::create(builder, ret.getLoc(), retBlock);
    ret.erase();
    if (load.getResult().use_empty())
      load.erase();
  }
}

/// Lowers operations with the terminator trait that have a single successor.
void lowerTerminator(mlir::Operation *op, mlir::Block *dest,
                     mlir::PatternRewriter &rewriter) {
  assert(op->hasTrait<mlir::OpTrait::IsTerminator>() && "not a terminator");
  mlir::OpBuilder::InsertionGuard guard(rewriter);
  rewriter.setInsertionPoint(op);
  rewriter.replaceOpWithNewOp<cir::BrOp>(op, dest);
}

static bool hasTrivialStepRegion(mlir::Region *stepRegion) {
  if (!stepRegion || !stepRegion->hasOneBlock())
    return false;
  mlir::Block &block = stepRegion->front();
  return block.getOperations().size() == 1 &&
         mlir::isa<cir::YieldOp>(block.front());
}

/// Walks a region while skipping operations of type `Ops`. This ensures the
/// callback is not applied to said operations and its children.
template <typename... Ops>
void walkRegionSkipping(
    mlir::Region &region,
    mlir::function_ref<mlir::WalkResult(mlir::Operation *)> callback) {
  region.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation *op) {
    if (isa<Ops...>(op))
      return mlir::WalkResult::skip();
    return callback(op);
  });
}

/// Determine whether a `cir.break` found while flattening `loop` is owned by a
/// `cir.switch` nested *inside* the loop (which owns the break's exit), rather
/// than by the loop itself. A break is captured by a nested switch only when a
/// `cir.switch` lies strictly between the break and `loop` in the op nesting.
///
/// This must not be answered with `getParentOfType<cir::SwitchOp>()`: that also
/// matches a switch that *encloses* the loop. When such an outer switch is
/// still structured — e.g. CIRSwitchOpFlattening deferred it because a
/// Duff's-device `cir.case` is buried in a scope/loop — the loop's own break
/// would be left unlowered here and later mis-lowered to the outer switch's
/// exit block, skipping the code after the loop (a silent miscompile).
static bool breakCapturedByNestedSwitch(mlir::Operation *breakOp,
                                        mlir::Operation *loop) {
  for (mlir::Operation *parent = breakOp->getParentOp();
       parent && parent != loop; parent = parent->getParentOp())
    if (isa<cir::SwitchOp>(parent))
      return true;
  return false;
}

// A cir.scope "has an escaping branch" if some block in its body branches to a
// block outside the scope's region — e.g. a `break`/`continue`/`goto` already
// lowered to `cir.br <outer-block>`. (A normal value-producing scope only
// branches within its own region and exits via `cir.yield`, so it is not
// matched here — it is left for CIRScopeOpFlattening, which preserves results.)
static bool scopeHasEscapingBranch(cir::ScopeOp scopeOp) {
  mlir::Region &r = scopeOp.getScopeRegion();
  for (mlir::Block &b : r)
    if (mlir::Operation *term = b.getTerminator())
      for (mlir::Block *succ : term->getSuccessors())
        if (succ->getParent() != &r)
          return true;
  return false;
}

// Inline (into the parent region) every dead cir.scope reachable in `region` —
// descending through structured ops other than nested loops/switches, which
// own their exits — that has a branch escaping the scope. Such scopes look
// trivially dead to the greedy driver (a `cir.br` is Pure and cir.scope has
// RecursiveMemoryEffects), so it would erase them: that silently drops the
// branch (miscompile) or, when the branch escapes to an outer region, sends
// RewriterBase::eraseOp's CFG post-order block traversal across the region
// boundary into unbounded recursion (a compiler stack overflow). Iterated
// innermost-out: inlining an inner break-scope exposes the escaping branch in
// its enclosing scope, which is then inlined on the next round.
static void inlineEscapingBranchScopes(mlir::Region &region,
                                       mlir::RewriterBase &rewriter) {
  while (true) {
    cir::ScopeOp toInline = nullptr;
    walkRegionSkipping<cir::LoopOpInterface, cir::SwitchOp>(
        region, [&](mlir::Operation *op) {
          auto scopeOp = mlir::dyn_cast<cir::ScopeOp>(op);
          // Only inline scopes whose results are unused: a scope reached by an
          // escaping branch never falls through to yield a value, so its
          // results (if any) must be dead. This also keeps the unconditional
          // eraseOp below sound (it requires use_empty).
          if (scopeOp && !scopeOp.isEmpty() && scopeOp->use_empty() &&
              scopeHasEscapingBranch(scopeOp)) {
            toInline = scopeOp;
            return mlir::WalkResult::interrupt();
          }
          return mlir::WalkResult::advance();
        });
    if (!toInline)
      return;

    mlir::OpBuilder::InsertionGuard guard(rewriter);
    mlir::Location scopeLoc = toInline.getLoc();
    mlir::Block *scopeBlock = toInline->getBlock();
    mlir::Block *afterScopeBlock =
        rewriter.splitBlock(scopeBlock, mlir::Block::iterator(toInline));
    mlir::Block *beforeBody = &toInline.getScopeRegion().front();
    mlir::Block *afterBody = &toInline.getScopeRegion().back();
    rewriter.inlineRegionBefore(toInline.getScopeRegion(), afterScopeBlock);

    rewriter.setInsertionPointToEnd(scopeBlock);
    cir::BrOp::create(rewriter, scopeLoc, beforeBody);

    rewriter.setInsertionPointToEnd(afterBody);
    if (auto yieldOp = mlir::dyn_cast<cir::YieldOp>(afterBody->getTerminator()))
      rewriter.replaceOpWithNewOp<cir::BrOp>(yieldOp, yieldOp.getArgs(),
                                             afterScopeBlock);

    rewriter.eraseOp(toInline);

    if (afterScopeBlock->empty() && afterScopeBlock->use_empty())
      eraseDeadBlock(rewriter, *afterScopeBlock);
  }
}

struct CIRFlattenCFGPass : public impl::CIRFlattenCFGBase<CIRFlattenCFGPass> {

  CIRFlattenCFGPass() = default;
  void runOnOperation() override;
};

struct CIRIfFlattening : public mlir::OpRewritePattern<cir::IfOp> {
  using OpRewritePattern<IfOp>::OpRewritePattern;

  mlir::LogicalResult
  matchAndRewrite(cir::IfOp ifOp,
                  mlir::PatternRewriter &rewriter) const override {
    // The delayed get-return-object conversion cir.if must survive the greedy
    // pass structured so CIRAwaitOpFlattening can clone it onto each coroutine
    // suspend-return edge. CIRFlattenCFGPass::runOnOperation flattens the
    // surviving original explicitly after the greedy pass (the clones have the
    // marker stripped, so they flatten normally here).
    if (ifOp->hasAttr(kCoroGroConvAttr))
      return mlir::failure();

    mlir::OpBuilder::InsertionGuard guard(rewriter);
    mlir::Location ifLoc = ifOp.getLoc();
    mlir::Location loc = getPreferredCondBrLoc(ifOp.getCondition(), ifLoc);
    bool emptyElse = ifOp.getElseRegion().empty();
    bool ifHasNoSanitize = ifOp->hasAttr("cir.nosanitize");
    mlir::Block *currentBlock = rewriter.getInsertionBlock();
    mlir::Block *remainingOpsBlock =
        rewriter.splitBlock(currentBlock, rewriter.getInsertionPoint());
    mlir::Block *continueBlock;
    if (ifOp->getResults().empty())
      continueBlock = remainingOpsBlock;
    else
      llvm_unreachable("NYI");

    // Inline the region
    mlir::Block *thenBeforeBody = &ifOp.getThenRegion().front();
    rewriter.inlineRegionBefore(ifOp.getThenRegion(), continueBlock);

    // Replace all cir.yield ops in inlined then blocks (not just the last one).
    // Dead intermediate blocks (e.g., from va_arg expansion or early returns)
    // may contain yield terminators that would survive into the parent region.
    for (mlir::Block *block = thenBeforeBody; block != continueBlock;
         block = block->getNextNode()) {
      if (auto yieldOp = dyn_cast<cir::YieldOp>(block->getTerminator())) {
        rewriter.setInsertionPointToEnd(block);
        bool hasNoSanitize = yieldOp->hasAttr("cir.nosanitize");
        auto br = rewriter.replaceOpWithNewOp<cir::BrOp>(yieldOp,
                                                         yieldOp.getArgs(),
                                                         continueBlock);
        if (block->getOperations().size() == 1)
          br->setLoc(stripOuterSourceAtomMetadata(ifOp.getLoc()));
        if (ifHasNoSanitize || hasNoSanitize)
          br->setAttr("cir.nosanitize", rewriter.getUnitAttr());
      }
    }

    rewriter.setInsertionPointToEnd(continueBlock);

    // Has else region: inline it.
    mlir::Block *elseBeforeBody = nullptr;
    mlir::Block *elseAfterBody = nullptr;
    if (!emptyElse) {
      elseBeforeBody = &ifOp.getElseRegion().front();
      elseAfterBody = &ifOp.getElseRegion().back();
      rewriter.inlineRegionBefore(ifOp.getElseRegion(), continueBlock);
    } else {
      elseBeforeBody = elseAfterBody = continueBlock;
    }

    rewriter.setInsertionPointToEnd(currentBlock);
    auto brCondOp =
        cir::BrCondOp::create(rewriter, loc, ifOp.getCondition(),
                              thenBeforeBody, elseBeforeBody);
    if (uint64_t atomGroup = getAtomGroupFromLoc(ifLoc))
      brCondOp->setLoc(
          applySourceAtomMetadata(rewriter, brCondOp.getLoc(), atomGroup, 1));

    // Propagate unpredictable attribute from cir.if to cir.brcond.
    if (ifOp->hasAttr("cir.unpredictable"))
      brCondOp->setAttr("cir.unpredictable",
                        rewriter.getUnitAttr());

    // Propagate branch weights from cir.if to cir.brcond.
    if (auto weights = ifOp->getAttr("cir.branch_weights"))
      brCondOp->setAttr("cir.branch_weights", weights);
    // Propagate HLSL control-flow hint from cir.if to cir.brcond.
    if (auto hlslHint = ifOp->getAttr("cir.hlsl.controlflow.hint"))
      brCondOp->setAttr("cir.hlsl.controlflow.hint", hlslHint);
    // Propagate nosanitize marker from cir.if to cir.brcond.
    if (ifOp->hasAttr("cir.nosanitize"))
      brCondOp->setAttr("cir.nosanitize", rewriter.getUnitAttr());
    // Preserve synthetic dead noreturn-UBSan epilogs until LLVM lowering.
    if (ifOp->hasAttr(kPreserveDeadUBSanUnreachableBlockAttr))
      brCondOp->setAttr(kPreserveDeadUBSanUnreachableBlockAttr,
                        rewriter.getUnitAttr());

    if (!emptyElse) {
      // Replace all cir.yield ops in inlined else blocks.
      for (mlir::Block *block = elseBeforeBody; block != continueBlock;
           block = block->getNextNode()) {
        if (auto yieldOp = dyn_cast<cir::YieldOp>(block->getTerminator())) {
          rewriter.setInsertionPointToEnd(block);
          bool hasNoSanitize = yieldOp->hasAttr("cir.nosanitize");
          auto br = rewriter.replaceOpWithNewOp<cir::BrOp>(yieldOp,
                                                           yieldOp.getArgs(),
                                                           continueBlock);
          if (block->getOperations().size() == 1)
            br->setLoc(stripOuterSourceAtomMetadata(ifOp.getLoc()));
          if (ifHasNoSanitize || hasNoSanitize)
            br->setAttr("cir.nosanitize", rewriter.getUnitAttr());
        }
      }
    }

    rewriter.replaceOp(ifOp, continueBlock->getArguments());
    return mlir::success();
  }
};

class CIRScopeOpFlattening : public mlir::OpRewritePattern<cir::ScopeOp> {
public:
  using OpRewritePattern<cir::ScopeOp>::OpRewritePattern;

  mlir::LogicalResult
  matchAndRewrite(cir::ScopeOp scopeOp,
                  mlir::PatternRewriter &rewriter) const override;
};

static mlir::LogicalResult flattenScopeOp(cir::ScopeOp scopeOp,
                                          mlir::RewriterBase &rewriter) {
  mlir::OpBuilder::InsertionGuard guard(rewriter);
  mlir::Location loc = scopeOp.getLoc();
  bool hasLiveResults = scopeOp.getNumResults() > 0 && !scopeOp->use_empty();

  // Empty scope: just remove it.
  // TODO: Remove this logic once CIR uses MLIR infrastructure to remove
  // trivially dead operations. MLIR canonicalizer is too aggressive and we
  // need to either (a) make sure all our ops model all side-effects and/or
  // (b) have more options in the canonicalizer in MLIR to temper
  // aggressiveness level.
  if (scopeOp.isEmpty()) {
    rewriter.eraseOp(scopeOp);
    return mlir::success();
  }

  // Split the current block before the ScopeOp to create the inlining point.
  mlir::Block *currentBlock = scopeOp->getBlock();
  rewriter.setInsertionPoint(scopeOp);
  mlir::Block *continueBlock =
      rewriter.splitBlock(currentBlock, rewriter.getInsertionPoint());
  // Dead scope results do not need continuation block arguments; keeping
  // them forces every inlined yield predecessor to synthesize unused
  // operands and breaks await cleanup edges that have nothing to forward.
  if (hasLiveResults)
    continueBlock->addArguments(scopeOp.getResultTypes(), loc);

  cir::YieldOp trivialContinueYield = nullptr;
  cir::BrOp trivialContinueBranch = nullptr;
  cir::ReturnOp trivialContinueReturn = nullptr;
  if (!hasLiveResults && continueBlock->getNumArguments() == 0 &&
      continueBlock->getOperations().size() == 2 &&
      &continueBlock->front() == scopeOp.getOperation()) {
    mlir::Operation &continueOp = continueBlock->back();
    trivialContinueYield = dyn_cast<cir::YieldOp>(&continueOp);
    if (!trivialContinueYield)
      if (auto br = dyn_cast<cir::BrOp>(&continueOp);
          br && getAtomGroupFromLoc(br.getLoc()))
        trivialContinueBranch = br;
    if (!trivialContinueYield && !trivialContinueBranch)
      if (auto ret = dyn_cast<cir::ReturnOp>(&continueOp);
          ret && ret.getInput().empty())
        trivialContinueReturn = ret;
  }

  // Inline body region.
  mlir::Block *beforeBody = &scopeOp.getScopeRegion().front();
  rewriter.inlineRegionBefore(scopeOp.getScopeRegion(), continueBlock);

  // Save stack and then branch into the body of the region.
  // CIR scopes do not model dynamic stack restore in this flattening pass:
  // stack save/restore for dynamic allocas is emitted explicitly during
  // CIRGen cleanup emission (see CIRGenDecl's pushStackRestore path).
  rewriter.setInsertionPointToEnd(currentBlock);

  // Optimization: when the scope body's entry block doesn't start with a
  // label (needed for goto resolution), merge it into currentBlock to avoid
  // creating an unnecessary unconditional branch and extra block boundary.
  // The inlined block has no predecessors yet, so merging is always safe
  // at this point.
  if (!isa<cir::LabelOp>(beforeBody->front())) {
    rewriter.mergeBlocks(beforeBody, currentBlock);
    // Update beforeBody to point to the merged block for the yield
    // replacement loop below.
    beforeBody = currentBlock;
  } else {
    cir::BrOp::create(rewriter, loc, mlir::ValueRange(), beforeBody);
  }

  // Replace all cir.yield ops in the inlined scope blocks with branches to
  // continueBlock. The original code only handled the yield in the last block
  // (afterBody), but dead blocks (e.g. from va_arg expansion) may contain
  // yield terminators that would otherwise survive into the parent region and
  // trigger a verification failure.
  for (mlir::Block *block = beforeBody; block != continueBlock;
       block = block->getNextNode()) {
    if (auto yieldOp = dyn_cast<cir::YieldOp>(block->getTerminator())) {
      rewriter.setInsertionPointToEnd(block);
      if (trivialContinueYield) {
        auto newYield = rewriter.replaceOpWithNewOp<cir::YieldOp>(
            yieldOp, trivialContinueYield.getOperands());
        newYield->setLoc(trivialContinueYield.getLoc());
      } else if (trivialContinueBranch) {
        auto newBr = rewriter.replaceOpWithNewOp<cir::BrOp>(
            yieldOp, trivialContinueBranch.getDestOperands(),
            trivialContinueBranch.getDest());
        newBr->setLoc(trivialContinueBranch.getLoc());
        newBr->setAttrs(trivialContinueBranch->getAttrs());
      } else if (trivialContinueReturn) {
        auto newRet = rewriter.replaceOpWithNewOp<cir::ReturnOp>(yieldOp);
        newRet->setLoc(trivialContinueReturn.getLoc());
      } else {
        mlir::ValueRange yieldArgs =
            hasLiveResults ? yieldOp.getArgs() : mlir::ValueRange();
        rewriter.replaceOpWithNewOp<cir::BrOp>(yieldOp, yieldArgs,
                                               continueBlock);
      }
    }
  }

  if (continueBlock->getNumArguments() != 0) {
    // Nested flattening can already have introduced plain branches that bypass
    // the scope result and jump straight to the scope continue block, for
    // example coroutine destroy/cleanup exits. Once the scope gains block
    // arguments for yielded values, those exits still need placeholder
    // operands to keep the CFG well-formed.
    llvm::SmallVector<mlir::Block *> predecessors(continueBlock->pred_begin(),
                                                  continueBlock->pred_end());
    for (mlir::Block *pred : predecessors) {
      auto brOp = dyn_cast<cir::BrOp>(pred->getTerminator());
      if (!brOp || brOp.getDest() != continueBlock ||
          !brOp.getDestOperands().empty())
        continue;

      llvm::SmallVector<mlir::Value> undefArgs;
      undefArgs.reserve(continueBlock->getNumArguments());
      rewriter.setInsertionPoint(brOp);
      for (mlir::BlockArgument arg : continueBlock->getArguments()) {
        auto undef = cir::ConstantOp::create(rewriter, brOp.getLoc(),
                                             cir::UndefAttr::get(arg.getType()));
        undefArgs.push_back(undef.getRes());
      }

      auto attrs = brOp->getAttrs();
      auto brLoc = brOp.getLoc();
      auto newBr =
          rewriter.replaceOpWithNewOp<cir::BrOp>(brOp, undefArgs, continueBlock);
      newBr->setAttrs(attrs);
      newBr->setLoc(brLoc);
    }
  }

  // Labels may temporarily end up mid-block after scope inlining. That is
  // expected here: LabelOp verification is intentionally relaxed during
  // FlattenCFG, and GotoSolver runs immediately after this pass to split
  // blocks at label positions while preserving the label block itself as the
  // eventual goto target.

  // Replace the op with values return from the body region.
  if (scopeOp.getNumResults() > 0 && scopeOp->use_empty())
    rewriter.eraseOp(scopeOp);
  else
    rewriter.replaceOp(scopeOp, continueBlock->getArguments());
  if ((trivialContinueYield || trivialContinueBranch ||
       trivialContinueReturn) &&
      continueBlock->use_empty())
    eraseDeadBlock(rewriter, *continueBlock);

  return mlir::success();
}

mlir::LogicalResult
CIRScopeOpFlattening::matchAndRewrite(cir::ScopeOp scopeOp,
                                      mlir::PatternRewriter &rewriter) const {
  return flattenScopeOp(scopeOp, rewriter);
}

class CIRSwitchOpFlattening : public mlir::OpRewritePattern<cir::SwitchOp> {
public:
  using OpRewritePattern<cir::SwitchOp>::OpRewritePattern;

  inline void rewriteYieldOp(mlir::PatternRewriter &rewriter,
                             cir::YieldOp yieldOp,
                             mlir::Block *destination) const {
    rewriter.setInsertionPoint(yieldOp);
    rewriter.replaceOpWithNewOp<cir::BrOp>(yieldOp, yieldOp.getOperands(),
                                           destination);
  }

  // Return the new defaultDestination block.
  Block *condBrToRangeDestination(cir::SwitchOp op,
                                  mlir::PatternRewriter &rewriter,
                                  mlir::Block *rangeDestination,
                                  mlir::Block *defaultDestination,
                                  bool emitLikelihoodInfo,
                                  CaseLikelihood likelihood,
                                  const APInt &lowerBound,
                                  const APInt &upperBound) const {
    // Use signedness-aware comparison for the assert since unsigned
    // case ranges can have high-bit-set values.
    cir::IntType condIntType =
        mlir::cast<cir::IntType>(op.getCondition().getType());
    assert((condIntType.isSigned() ? lowerBound.sle(upperBound)
                                   : lowerBound.ule(upperBound)) &&
           "Invalid range");
    mlir::Block *resBlock = rewriter.createBlock(defaultDestination);
    // Derive integer types from the switch condition to handle all widths
    // (i8, i16, i32, i64, etc.) correctly.
    cir::IntType sIntType =
        mlir::cast<cir::IntType>(op.getCondition().getType());
    cir::IntType uIntType =
        cir::IntType::get(op.getContext(), sIntType.getWidth(), false);

    cir::ConstantOp rangeLength = cir::ConstantOp::create(
        rewriter, op.getLoc(),
        cir::IntAttr::get(sIntType, upperBound - lowerBound));

    cir::ConstantOp lowerBoundValue = cir::ConstantOp::create(
        rewriter, op.getLoc(), cir::IntAttr::get(sIntType, lowerBound));
    cir::BinOp diffValue =
        cir::BinOp::create(rewriter, op.getLoc(), sIntType, cir::BinOpKind::Sub,
                           op.getCondition(), lowerBoundValue,
                           {}, {}, {}, {}, {}, {});

    // Use unsigned comparison to check if the condition is in the range.
    cir::CastOp uDiffValue = cir::CastOp::create(
        rewriter, op.getLoc(), uIntType, CastKind::integral, diffValue);
    cir::CastOp uRangeLength = cir::CastOp::create(
        rewriter, op.getLoc(), uIntType, CastKind::integral, rangeLength);

    mlir::Value cond = cir::CmpOp::create(
        rewriter, op.getLoc(), cir::CmpOpKind::le, uDiffValue, uRangeLength);
    if (emitLikelihoodInfo && likelihood != CaseLikelihood::None) {
      auto boolTy = cir::BoolType::get(op.getContext());
      auto expected = cir::ConstantOp::create(
          rewriter, op.getLoc(), boolTy,
          cir::BoolAttr::get(op.getContext(), boolTy,
                             likelihood == CaseLikelihood::Likely));
      cond = cir::ExpectOp::create(rewriter, op.getLoc(), boolTy, cond,
                                   expected, /*prob=*/mlir::FloatAttr{});
    }
    cir::BrCondOp::create(rewriter, op.getLoc(), cond, rangeDestination,
                          defaultDestination);
    return resBlock;
  }

  mlir::LogicalResult
  matchAndRewrite(cir::SwitchOp op,
                  mlir::PatternRewriter &rewriter) const override {
    bool emitLikelihoodInfo = shouldEmitLikelihoodInfo(op);
    llvm::SmallVector<CaseOp> cases;
    op.collectCases(cases);

    // Empty switch statement: just erase it.
    if (cases.empty()) {
      rewriter.eraseOp(op);
      return mlir::success();
    }

    // Duff's device / cross-scope case labels: if any case op is nested
    // inside an intermediate scope or loop (i.e., not directly in the
    // switch body or a case body), defer this rewrite so the greedy
    // driver can flatten those intermediate ops first.  On retry the
    // case ops will be at the right nesting level.
    for (cir::CaseOp caseOp : cases) {
      for (mlir::Operation *parent = caseOp->getParentOp();
           parent != op.getOperation(); parent = parent->getParentOp()) {
        if (isa<cir::ScopeOp, cir::LoopOpInterface>(parent))
          return mlir::failure();
      }
    }

    // Create exit block from the next node of cir.switch op.
    mlir::Block *exitBlock = rewriter.splitBlock(
        rewriter.getBlock(), op->getNextNode()->getIterator());

    // We lower cir.switch op in the following process:
    // 1. Inline the region from the switch op after switch op.
    // 2. Traverse each cir.case op:
    //    a. Record the entry block, block arguments and condition for every
    //    case. b. Inline the case region after the case op.
    // 3. Replace the empty cir.switch.op with the new cir.switchflat op by the
    //    recorded block and conditions.

    // inline everything from switch body between the switch op and the exit
    // block.
    // Map from preserved dead label blocks to the CaseOp that
    // follows them in source order. Used after case inlining to redirect
    // label block yields to the correct case entry.
    llvm::DenseMap<mlir::Block *, cir::CaseOp> labelBlockToNextCase;
    {
      cir::YieldOp switchYield = nullptr;
      llvm::SmallVector<cir::BreakOp> bodyBreaks;

      // Resolve goto/label pairs local to the switch body BEFORE
      // the dead-block erasure. GotoSolver runs after FlattenCFG, so
      // cir.goto/cir.label in the switch body are still symbolic with no
      // CFG edges. Without resolution, label blocks appear to have no
      // predecessors and get erased, destroying ops (like cir.cast for
      // nested switch conditions) that are still referenced.
      {
        llvm::StringMap<mlir::Block *> localLabels;
        llvm::SmallVector<cir::GotoOp, 4> localGotos;

        for (mlir::Block &block : op.getBody().getBlocks()) {
          for (mlir::Operation &innerOp : block.getOperations()) {
            if (auto lab = dyn_cast<cir::LabelOp>(&innerOp))
              localLabels.try_emplace(lab.getLabel(), lab->getBlock());
            else if (auto goTo = dyn_cast<cir::GotoOp>(&innerOp))
              localGotos.push_back(goTo);
          }
        }

        for (cir::GotoOp goTo : localGotos) {
          auto it = localLabels.find(goTo.getLabel());
          if (it == localLabels.end())
            continue; // target outside switch body; leave for GotoSolver
          mlir::Block *dest = it->second;
          rewriter.setInsertionPoint(goTo);
          cir::BrOp::create(rewriter, goTo.getLoc(), dest);
          rewriter.eraseOp(goTo);
        }
        // Don't erase labels here — they may be targeted by gotos outside
        // the switch body. GotoSolver will clean them up later.
      }

      // Scan body blocks for yield and break terminators.
      // Skip dead blocks (no predecessors, not the entry block) that can
      // appear from labels before case labels in the source.
      // Preserve blocks containing cir.case ops — they are dead in the CFG
      // but reachable via switch dispatch. Also preserve blocks with
      // cir.label (goto targets not yet resolved).
      mlir::Block &entryBlock = op.getBody().front();
      llvm::SmallVector<mlir::Block *, 4> preservedDeadBlocks;
      for (mlir::Block &block :
           llvm::make_early_inc_range(op.getBody().getBlocks())) {
        if (&block != &entryBlock && block.hasNoPredecessors()) {
          bool preserve = llvm::any_of(block.getOperations(),
              [](mlir::Operation &o) {
                return isa<cir::CaseOp, cir::LabelOp>(o);
              });
          if (!preserve) {
            eraseDeadBlock(rewriter, block);
            continue;
          }
          // Preserved dead blocks must NOT participate in the
          // yield/break scan — their cir.yield is a structural artifact,
          // not the switch's actual yield. We fix their terminators after
          // inlining below.
          preservedDeadBlocks.push_back(&block);
          continue;
        }
        if (auto yieldOp = dyn_cast<cir::YieldOp>(block.getTerminator()))
          switchYield = yieldOp;
        else if (auto breakOp = dyn_cast<cir::BreakOp>(block.getTerminator()))
          bodyBreaks.push_back(breakOp);
      }

      assert(!op.getBody().empty());

      // Before inlining the switch body, map preserved dead
      // label blocks to the CaseOp they should fall through to.
      // In source code like `a: case 0: goto a;`, label `a:` is in
      // a dead block and its yield should fall through to `case 0:`.
      // We use the first CaseOp in source order as the target.
      if (!preservedDeadBlocks.empty() && !cases.empty()) {
        for (mlir::Block *block : preservedDeadBlocks) {
          if (isa<cir::YieldOp>(block->getTerminator()))
            labelBlockToNextCase[block] = cases.front();
        }
      }

      mlir::Block *originalBlock = op->getBlock();
      mlir::Block *swopBlock =
          rewriter.splitBlock(originalBlock, op->getIterator());
      rewriter.inlineRegionBefore(op.getBody(), exitBlock);

      for (mlir::Block *block : preservedDeadBlocks) {
        mlir::Operation *term = block->getTerminator();
        if (isa<cir::BreakOp>(term)) {
          rewriter.setInsertionPoint(term);
          cir::BrOp::create(rewriter, term->getLoc(), exitBlock);
          rewriter.eraseOp(term);
        } else if (isa<cir::YieldOp>(term)) {
          // Replace yield with br to exitBlock for now; if this label
          // block has a following CaseOp, it will be redirected after
          // case inlining (below).
          rewriter.setInsertionPoint(term);
          cir::BrOp::create(rewriter, term->getLoc(), exitBlock);
          rewriter.eraseOp(term);
        }
      }

      if (switchYield)
        rewriteYieldOp(rewriter, switchYield, exitBlock);

      // Handle break ops at the top level of the switch body (not inside
      // any case region). These are break statements that exit the switch
      // but fall outside the per-case region walks below.
      for (cir::BreakOp breakOp : bodyBreaks)
        lowerTerminator(breakOp, exitBlock, rewriter);

      rewriter.setInsertionPointToEnd(originalBlock);
      cir::BrOp::create(rewriter, op.getLoc(), swopBlock);
    }

    // Allocate required data structures (disconsider default case in
    // vectors).
    llvm::SmallVector<mlir::APInt, 8> caseValues;
    llvm::SmallVector<mlir::Block *, 8> caseDestinations;
    llvm::SmallVector<mlir::ValueRange, 8> caseOperands;
    llvm::SmallVector<CaseLikelihood, 8> switchLikelihoods;

    llvm::SmallVector<std::pair<APInt, APInt>> rangeValues;
    llvm::SmallVector<mlir::Block *> rangeDestinations;
    llvm::SmallVector<mlir::ValueRange> rangeOperands;
    llvm::SmallVector<CaseLikelihood> rangeLikelihoods;

    // Initialize default case as optional.
    mlir::Block *defaultDestination = exitBlock;
    mlir::ValueRange defaultOperands = exitBlock->getArguments();
    switchLikelihoods.push_back(CaseLikelihood::None);

    // Digest the case statements values and bodies.
    for (cir::CaseOp caseOp : cases) {
      mlir::Region &region = caseOp.getCaseRegion();
      // Found default case: save destination and operands.
      switch (caseOp.getKind()) {
      case cir::CaseOpKind::Default:
        defaultDestination = &region.front();
        defaultOperands = defaultDestination->getArguments();
        switchLikelihoods.front() = getCaseLikelihood(caseOp, 0);
        break;
      case cir::CaseOpKind::Range:
        assert(caseOp.getValue().size() == 2 &&
               "Case range should have 2 case value");
        rangeValues.push_back(
            {cast<cir::IntAttr>(caseOp.getValue()[0]).getValue(),
             cast<cir::IntAttr>(caseOp.getValue()[1]).getValue()});
        rangeDestinations.push_back(&region.front());
        rangeOperands.push_back(rangeDestinations.back()->getArguments());
        rangeLikelihoods.push_back(getCaseLikelihood(caseOp, 0));
        break;
      case cir::CaseOpKind::Anyof:
      case cir::CaseOpKind::Equal:
        // AnyOf cases kind can have multiple values, hence the loop below.
        for (auto [index, value] : llvm::enumerate(caseOp.getValue())) {
          caseValues.push_back(cast<cir::IntAttr>(value).getValue());
          caseDestinations.push_back(&region.front());
          caseOperands.push_back(caseDestinations.back()->getArguments());
          switchLikelihoods.push_back(getCaseLikelihood(caseOp, index));
        }
        break;
      }

      // Handle break statements.
      walkRegionSkipping<cir::LoopOpInterface, cir::SwitchOp>(
          region, [&](mlir::Operation *op) {
            if (!isa<cir::BreakOp>(op))
              return mlir::WalkResult::advance();

            lowerTerminator(op, exitBlock, rewriter);
            return mlir::WalkResult::skip();
          });

      // After break lowering, ScopeOps that wrapped the BreakOp now carry a
      // `cir.br exitBlock` that escapes the scope's region. Since cir.br is
      // Pure and ScopeOp has RecursiveMemoryEffects, the greedy driver would
      // consider these ScopeOps trivially dead and erase them — silently
      // losing the break branch, or (when the break is wrapped in more than
      // one scope, so the branch escapes to an outer region) driving
      // RewriterBase::eraseOp's CFG post-order traversal across the region
      // boundary into an unbounded recursion. Inline every such scope now,
      // innermost-out, so the branch survives and stays region-local.
      inlineEscapingBranchScopes(region, rewriter);

      // Track fallthrough in cases.
      for (mlir::Block &blk : region.getBlocks()) {
        if (blk.getNumSuccessors())
          continue;

        if (auto yieldOp = dyn_cast<cir::YieldOp>(blk.getTerminator())) {
          mlir::Operation *nextOp = caseOp->getNextNode();
          assert(nextOp && "caseOp is not expected to be the last op");
          mlir::Block *oldBlock = nextOp->getBlock();
          mlir::Block *newBlock =
              rewriter.splitBlock(oldBlock, nextOp->getIterator());
          rewriter.setInsertionPointToEnd(oldBlock);
          cir::BrOp::create(rewriter, nextOp->getLoc(), mlir::ValueRange(),
                            newBlock);
          rewriteYieldOp(rewriter, yieldOp, newBlock);
        }
      }

      mlir::Block *oldBlock = caseOp->getBlock();
      mlir::Block *newBlock =
          rewriter.splitBlock(oldBlock, caseOp->getIterator());

      mlir::Block &entryBlock = caseOp.getCaseRegion().front();
      rewriter.inlineRegionBefore(caseOp.getCaseRegion(), newBlock);

      // PGO skipcount, mirroring classic
      // CodeGenFunction::EmitBlockWithFallThrough: when instrumenting, a case's
      // region entry begins with its profile-counter increment
      // (cir.call @__cir_instrprof_increment[_step]) and is the switch-dispatch
      // destination (region.front()), so direct dispatch counts it once.
      // Textual fall-through from the previous case reaches the body through
      // the branch created just below; that path must NOT re-run the counter,
      // or a fallen-into case body is counted once per dispatch plus once per
      // fall-through. Split the counter into its own block so only the dispatch
      // edge includes it and the fall-in branch targets the body past it. For a
      // case reached only by dispatch (break before it, or the first case) the
      // fall-in branch is dead, so the redirect is harmless.
      mlir::Block *fallInTarget = &entryBlock;
      // The counter increment is emitted at the top of the case region,
      // preceded only by the constant operands it consumes (null ptr, hash,
      // counter count, index). Scan past those to find it.
      cir::CallOp counter;
      for (mlir::Operation &entryOp : entryBlock) {
        if (auto call = dyn_cast<cir::CallOp>(&entryOp)) {
          std::optional<llvm::StringRef> callee = call.getCallee();
          if (callee && callee->starts_with("__cir_instrprof_increment"))
            counter = call;
          break;
        }
        if (!isa<cir::ConstantOp>(entryOp))
          break;
      }
      if (counter) {
        mlir::Block *bodyBlock =
            rewriter.splitBlock(&entryBlock, std::next(counter->getIterator()));
        rewriter.setInsertionPointToEnd(&entryBlock);
        cir::BrOp::create(rewriter, counter.getLoc(), bodyBlock);
        fallInTarget = bodyBlock;
      }

      // Create a branch to the entry of the inlined region (textual fall-in).
      rewriter.setInsertionPointToEnd(oldBlock);
      cir::BrOp::create(rewriter, caseOp.getLoc(), fallInTarget);

      // Fix up label blocks that should fall through to this case.
      // If a preserved dead label block's yield was replaced with br to
      // exitBlock, but the label originally preceded this case in the
      // source, redirect it to the case's entry block instead.
      for (auto &[labelBlock, nextCase] : labelBlockToNextCase) {
        if (nextCase != caseOp)
          continue;
        // The label block's terminator is a BrOp to exitBlock.
        // Replace it with a branch to the case entry block.
        if (auto brOp = dyn_cast<cir::BrOp>(labelBlock->getTerminator())) {
          if (brOp.getDest() == exitBlock) {
            rewriter.setInsertionPoint(brOp);
            cir::BrOp::create(rewriter, brOp.getLoc(), &entryBlock);
            rewriter.eraseOp(brOp);
          }
        }
      }
    }

    // Remove all cases since we've inlined the regions.
    for (cir::CaseOp caseOp : cases) {
      mlir::Block *caseBlock = caseOp->getBlock();
      // Erase the block with no predecessors here to make the generated code
      // simpler a little bit.
      if (caseBlock->hasNoPredecessors())
        rewriter.eraseBlock(caseBlock);
      else
        rewriter.eraseOp(caseOp);
    }

    for (auto [rangeVal, operand, destination, likelihood] :
         llvm::zip(rangeValues, rangeOperands, rangeDestinations,
                   rangeLikelihoods)) {
      APInt lowerBound = rangeVal.first;
      APInt upperBound = rangeVal.second;

      // The case range is unreachable, skip it.
      bool isSigned = mlir::cast<cir::IntType>(
                          op.getCondition().getType())
                          .isSigned();
      if (isSigned ? lowerBound.sgt(upperBound)
                   : lowerBound.ugt(upperBound))
        continue;

      // If range is small, add multiple switch instruction cases.
      // This magical number is from the original CGStmt code.
      constexpr int kSmallRangeThreshold = 64;
      if ((upperBound - lowerBound)
              .ult(llvm::APInt(lowerBound.getBitWidth(),
                               kSmallRangeThreshold))) {
        for (APInt iValue = lowerBound;
             isSigned ? iValue.sle(upperBound) : iValue.ule(upperBound);
             ++iValue) {
          caseValues.push_back(iValue);
          caseOperands.push_back(operand);
          caseDestinations.push_back(destination);
          switchLikelihoods.push_back(likelihood);
        }
        continue;
      }

      defaultDestination =
          condBrToRangeDestination(op, rewriter, destination,
                                   defaultDestination, emitLikelihoodInfo,
                                   likelihood, lowerBound, upperBound);
      defaultOperands = operand;
    }

    // Set switch op to branch to the newly created blocks.
    rewriter.setInsertionPoint(op);
    bool isUnpredictable = op->hasAttr("cir.unpredictable");
    // Save HLSL hint before replaceOpWithNewOp erases op.
    auto hlslHint = op->getAttr("cir.hlsl.controlflow.hint");
    auto switchFlatOp = rewriter.replaceOpWithNewOp<cir::SwitchFlatOp>(
        op, op.getCondition(), defaultDestination, defaultOperands, caseValues,
        caseDestinations, caseOperands);

    // Propagate unpredictable attribute from cir.switch to cir.switch.flat.
    if (isUnpredictable)
      switchFlatOp->setAttr("cir.unpredictable",
                            rewriter.getUnitAttr());

    if (emitLikelihoodInfo)
      if (auto branchWeights = getLikelihoodWeights(switchLikelihoods))
      switchFlatOp->setAttr("cir.branch_weights",
                            getBranchWeightsAttr(rewriter.getContext(),
                                                 *branchWeights));
    // Propagate HLSL control-flow hint from cir.switch to cir.switch.flat.
    if (hlslHint)
      switchFlatOp->setAttr("cir.hlsl.controlflow.hint", hlslHint);

    return mlir::success();
  }
};

class CIRLoopOpInterfaceFlattening
    : public mlir::OpInterfaceRewritePattern<cir::LoopOpInterface> {
public:
  using mlir::OpInterfaceRewritePattern<
      cir::LoopOpInterface>::OpInterfaceRewritePattern;

  inline void lowerConditionOp(cir::ConditionOp op, mlir::Block *body,
                               mlir::Block *exit,
                               mlir::PatternRewriter &rewriter) const {
    mlir::OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPoint(op);
    if (mlir::Operation *condDef = op.getCondition().getDefiningOp()) {
      if (!getAtomGroupFromLoc(condDef->getLoc()))
        if (auto nestedLoc = getNestedLocWithAtomMetadata(op.getLoc()))
          condDef->setLoc(*nestedLoc);
    }
    // Save location before replaceOpWithNewOp erases op.
    mlir::Location opLoc = op.getLoc();
    auto brCondOp =
        rewriter.replaceOpWithNewOp<cir::BrCondOp>(op, op.getCondition(), body,
                                                   exit);
    if (uint64_t atomGroup = getAtomGroupFromLoc(opLoc))
      brCondOp->setLoc(
          applySourceAtomMetadata(rewriter, brCondOp.getLoc(), atomGroup, 1));
  }

  mlir::LogicalResult
  matchAndRewrite(cir::LoopOpInterface op,
                  mlir::PatternRewriter &rewriter) const final {
    // Read loop hint before the loop op is erased.
    auto loopHint =
        op->getAttrOfType<mlir::DictionaryAttr>("loopHint");

    // Setup CFG blocks.
    mlir::Block *entry = rewriter.getInsertionBlock();
    mlir::Block *exit =
        rewriter.splitBlock(entry, rewriter.getInsertionPoint());
    mlir::Block *cond = &op.getCond().front();
    mlir::Block *body = &op.getBody().front();
    mlir::Block *step =
        (op.maybeGetStep() ? &op.maybeGetStep()->front() : nullptr);

    // Setup loop entry branch.
    rewriter.setInsertionPointToEnd(entry);
    cir::BrOp::create(rewriter, op.getLoc(), &op.getEntry().front());

    // Branch from condition region to body or exit.
    // When a potentially-throwing function call appears in the loop
    // condition inside a try block, TryOp flattening (which runs first)
    // splits the condition block and replaces the call with a TryCallOp.
    // This means the ConditionOp may no longer be in the first block of
    // the condition region — it could be in any successor block. Search
    // all blocks in the condition region to find it.
    cir::ConditionOp conditionOp = nullptr;
    for (mlir::Block &blk : op.getCond()) {
      if (auto c = dyn_cast<cir::ConditionOp>(blk.getTerminator())) {
        conditionOp = c;
        break;
      }
    }
    assert(conditionOp && "condition region must contain a cir.condition op");
    mlir::Block *condBlock = conditionOp->getBlock();
    lowerConditionOp(conditionOp, body, exit, rewriter);
    // For do-while loops, the condition→body branch is the back-edge.
    if (loopHint && isa<cir::DoWhileOp>(op.getOperation()))
      condBlock->getTerminator()->setAttr("loopHint", loopHint);

    // Lower continue/break statements in the body region with a single walk.
    // Keep continue-in-switch behavior (continue targets loop), and preserve
    // break-in-switch behavior (break targets switch).
    bool trivialStep = hasTrivialStepRegion(op.maybeGetStep());
    mlir::Block *dest = (step ? step : cond);
    mlir::Operation *loopOp = op.getOperation();
    op.walkBodySkippingNestedLoops([&](mlir::Operation *op) {
      if (isa<cir::ContinueOp>(op)) {
        mlir::Block *continueBlock = op->getBlock();
        lowerTerminator(op, trivialStep ? cond : dest, rewriter);
        if (trivialStep && loopHint)
          continueBlock->getTerminator()->setAttr("loopHint", loopHint);
        return mlir::WalkResult::skip();
      }
      if (isa<cir::BreakOp>(op) && !breakCapturedByNestedSwitch(op, loopOp)) {
        lowerTerminator(op, exit, rewriter);
        return mlir::WalkResult::skip();
      }
      return mlir::WalkResult::advance();
    });

    // Lower continue statements and non-switch breaks in step and cond
    // regions. GCC statement expressions can contain break/continue in these
    // positions.
    for (mlir::Region *region :
         {&op.getCond(), static_cast<mlir::Region *>(op.maybeGetStep())}) {
      if (!region)
        continue;
      walkRegionSkipping<cir::LoopOpInterface>(
          *region, [&](mlir::Operation *innerOp) {
            if (isa<cir::ContinueOp>(innerOp)) {
              mlir::Block *continueBlock = innerOp->getBlock();
              lowerTerminator(innerOp, trivialStep ? cond : dest, rewriter);
              if (trivialStep && loopHint)
                continueBlock->getTerminator()->setAttr("loopHint", loopHint);
              return mlir::WalkResult::skip();
            }
            if (isa<cir::BreakOp>(innerOp) &&
                !breakCapturedByNestedSwitch(innerOp, loopOp)) {
              lowerTerminator(innerOp, exit, rewriter);
              return mlir::WalkResult::skip();
            }
            return mlir::WalkResult::advance();
          });
    }

    // After break lowering, ScopeOps that contained BreakOps now have
    // BrOp terminators (targeting exit) instead of BreakOps. Since BrOp
    // has no memory effects and ScopeOp has RecursiveMemoryEffects, the
    // greedy driver would consider these ScopeOps trivially dead and
    // erase them — silently losing the break branch. Inline them now.
    // Search body, step, and cond regions for such scopes. (This direct-child
    // scan also inlines scopes whose body exits via a non-yield terminator with
    // no CFG successor, e.g. `cir.return`/`cir.unreachable`.)
    llvm::SmallVector<cir::ScopeOp> scopesToInline;
    for (mlir::Region *region :
         {&op.getBody(), &op.getCond(),
          static_cast<mlir::Region *>(op.maybeGetStep())}) {
      if (!region)
        continue;
      for (mlir::Block &blk : *region) {
        for (mlir::Operation &bodyOp : blk) {
          if (auto scopeOp = dyn_cast<cir::ScopeOp>(&bodyOp)) {
            if (!scopeOp.isEmpty() &&
                !isa<cir::YieldOp>(
                    scopeOp.getScopeRegion().back().getTerminator()))
              scopesToInline.push_back(scopeOp);
          }
        }
      }
    }
    for (auto scopeOp : scopesToInline) {
      mlir::OpBuilder::InsertionGuard guard(rewriter);
      mlir::Location scopeLoc = scopeOp.getLoc();
      mlir::Block *scopeBlock = scopeOp->getBlock();
      mlir::Block *afterScopeBlock =
          rewriter.splitBlock(scopeBlock, mlir::Block::iterator(scopeOp));
      mlir::Block *beforeBody = &scopeOp.getScopeRegion().front();
      mlir::Block *afterBody = &scopeOp.getScopeRegion().back();
      rewriter.inlineRegionBefore(scopeOp.getScopeRegion(), afterScopeBlock);

      rewriter.setInsertionPointToEnd(scopeBlock);
      cir::BrOp::create(rewriter, scopeLoc, beforeBody);

      rewriter.setInsertionPointToEnd(afterBody);
      if (auto yieldOp =
              dyn_cast<cir::YieldOp>(afterBody->getTerminator())) {
        rewriter.replaceOpWithNewOp<cir::BrOp>(yieldOp, yieldOp.getArgs(),
                                               afterScopeBlock);
      }

      rewriter.eraseOp(scopeOp);

      // After erasing the scope op, afterScopeBlock may be empty if the
      // scope was the only operation in its portion of the block. An empty
      // block without a terminator violates CIR invariants.
      if (afterScopeBlock->empty() && afterScopeBlock->use_empty()) {
        eraseDeadBlock(rewriter, *afterScopeBlock);
      }
    }

    // The direct-child scan above only sees scopes that are immediate children
    // of the loop regions and only when their own back-terminator escapes. A
    // break/continue buried inside a further nested cir.scope — or inside a
    // cir.try that was flattened first, so the lowered `cir.br` (to exit/step/
    // cond) now sits inside an enclosing scope whose own terminator is still a
    // plain yield — is missed. Such a scope looks trivially dead to the greedy
    // driver (its escaping cir.br is Pure and ScopeOp has RecursiveMemoryEffects)
    // and gets erased; RewriterBase::eraseOp's CFG post-order block traversal
    // then follows the escaping branch across the region boundary back to the
    // scope op itself, an unbounded recursion (a compiler stack overflow).
    // inlineEscapingBranchScopes walks each region recursively and inlines every
    // scope with an escaping branch, innermost-out, keeping the branch
    // region-local.
    for (mlir::Region *region :
         {&op.getBody(), &op.getCond(),
          static_cast<mlir::Region *>(op.maybeGetStep())}) {
      if (region)
        inlineEscapingBranchScopes(*region, rewriter);
    }

    // Lower optional body region yield.
    for (mlir::Block &blk : op.getBody().getBlocks()) {
      auto bodyYield = dyn_cast<cir::YieldOp>(blk.getTerminator());
      if (bodyYield) {
        mlir::Block *yieldBlock = bodyYield->getBlock();
        lowerTerminator(bodyYield, trivialStep ? cond : (step ? step : cond),
                        rewriter);
        // For while loops (no step), body→cond is the back-edge.
        if ((!step || trivialStep) && loopHint)
          yieldBlock->getTerminator()->setAttr("loopHint", loopHint);
      }
    }

    // Lower mandatory step region yield.
    // Like the condition region, TryOp processing may have split the step
    // block, so search all blocks for the YieldOp.
    if (step && !trivialStep) {
      cir::YieldOp stepYield = nullptr;
      for (mlir::Block &blk : *op.maybeGetStep()) {
        if (auto y = dyn_cast<cir::YieldOp>(blk.getTerminator())) {
          stepYield = y;
          break;
        }
      }
      assert(stepYield && "step region must contain a cir.yield op");
      mlir::Block *stepBlock = stepYield->getBlock();
      lowerTerminator(stepYield, cond, rewriter);
      // For for-loops, step→cond is the back-edge.
      if (loopHint)
        stepBlock->getTerminator()->setAttr("loopHint", loopHint);
    }

    // Move region contents out of the loop op.
    rewriter.inlineRegionBefore(op.getCond(), exit);
    rewriter.inlineRegionBefore(op.getBody(), exit);
    if (step && !trivialStep)
      rewriter.inlineRegionBefore(*op.maybeGetStep(), exit);

    rewriter.eraseOp(op);
    return mlir::success();
  }
};

class CIRTernaryOpFlattening : public mlir::OpRewritePattern<cir::TernaryOp> {
public:
  using OpRewritePattern<cir::TernaryOp>::OpRewritePattern;

  mlir::LogicalResult
  matchAndRewrite(cir::TernaryOp op,
                  mlir::PatternRewriter &rewriter) const override {
    Location loc = op->getLoc();
    Block *condBlock = rewriter.getInsertionBlock();
    Block::iterator opPosition = rewriter.getInsertionPoint();
    Block *remainingOpsBlock = rewriter.splitBlock(condBlock, opPosition);
    llvm::SmallVector<mlir::Location, 2> locs;
    // Ternary result is optional, make sure to populate the location only
    // when relevant.
    if (op->getResultTypes().size())
      locs.push_back(loc);

    // Add block arguments for the ternary result directly to the remaining
    // ops block, so that true/false branches go directly there without an
    // intermediate trivial-br block. This avoids generating one extra basic
    // block per ternary, which matters for very large switch tables (e.g.,
    // 50k+ extra blocks in SPEC 502.gcc_r's insn-attrtab.c).
    for (auto [type, l] : llvm::zip(op->getResultTypes(), locs))
      remainingOpsBlock->addArgument(type, l);

    Region &trueRegion = op.getTrueRegion();
    Block *trueBlock = &trueRegion.front();
    mlir::Operation *trueTerminator = trueRegion.back().getTerminator();
    rewriter.setInsertionPointToEnd(&trueRegion.back());

    // Handle both yield and unreachable terminators (throw expressions)
    bool hasNoSanitize = op->hasAttr("cir.nosanitize");
    if (auto trueYieldOp = dyn_cast<cir::YieldOp>(trueTerminator)) {
      auto trueBr = rewriter.replaceOpWithNewOp<cir::BrOp>(
          trueYieldOp, trueYieldOp.getArgs(), remainingOpsBlock);
      if (hasNoSanitize)
        trueBr->setAttr("cir.nosanitize",
                         mlir::UnitAttr::get(rewriter.getContext()));
    } else if (isa<cir::UnreachableOp>(trueTerminator)) {
      // Terminator is unreachable (e.g., from throw), just keep it
    } else {
      trueTerminator->emitError("unexpected terminator in ternary true region, "
                                "expected yield or unreachable, got: ")
          << trueTerminator->getName();
      return mlir::failure();
    }
    rewriter.inlineRegionBefore(trueRegion, remainingOpsBlock);

    Block *falseBlock = &op.getFalseRegion().front();
    Region &falseRegion = op.getFalseRegion();

    mlir::Operation *falseTerminator = falseRegion.back().getTerminator();
    rewriter.setInsertionPointToEnd(&falseRegion.back());

    // Handle both yield and unreachable terminators (throw expressions)
    if (auto falseYieldOp = dyn_cast<cir::YieldOp>(falseTerminator)) {
      rewriter.replaceOpWithNewOp<cir::BrOp>(
          falseYieldOp, falseYieldOp.getArgs(), remainingOpsBlock);
    } else if (isa<cir::UnreachableOp>(falseTerminator)) {
      // Terminator is unreachable (e.g., from throw), just keep it
    } else {
      falseTerminator->emitError("unexpected terminator in ternary false "
                                 "region, expected yield or unreachable, got: ")
          << falseTerminator->getName();
      return mlir::failure();
    }
    rewriter.inlineRegionBefore(falseRegion, remainingOpsBlock);

    rewriter.setInsertionPointToEnd(condBlock);
    auto brCondOp =
        cir::BrCondOp::create(rewriter, loc, op.getCond(), trueBlock, falseBlock);

    // Propagate nosanitize from cir.ternary to cir.brcond.
    if (auto nosanitize = op->getAttr("cir.nosanitize"))
      brCondOp->setAttr("cir.nosanitize", nosanitize);

    // Propagate branch weights from cir.ternary to cir.brcond.
    if (auto weights = op->getAttr("cir.branch_weights"))
      brCondOp->setAttr("cir.branch_weights", weights);

    // Note: a ternary's fast-math flags have no sink on this control-flow
    // lowering path — the result flows through block arguments that become an
    // LLVM phi, which carries no fast-math flags. FP min/max ternaries that
    // would benefit from fmf (for the llvm.maxnum/minnum fold) are folded to a
    // cir.select by CIRSimplify (which carries the flags) before this pass
    // runs, so nothing is lost here.

    rewriter.replaceOp(op, remainingOpsBlock->getArguments());

    // Ok, we're done!
    return mlir::success();
  }
};

class CIRAwaitOpFlattening : public mlir::OpRewritePattern<cir::AwaitOp> {
  mlir::SymbolTable &symTable;

public:
  CIRAwaitOpFlattening(mlir::MLIRContext *ctx, mlir::SymbolTable &symTable)
      : OpRewritePattern<cir::AwaitOp>(ctx), symTable(symTable) {}

  static cir::CallOp getCoroutineEndCall(mlir::Block &block) {
    if (block.empty() || !mlir::isa<cir::ReturnOp>(block.back()))
      return {};

    for (mlir::Operation &op : block) {
      auto callOp = mlir::dyn_cast<cir::CallOp>(op);
      if (!callOp || !callOp.getCalleeAttr())
        continue;
      if (callOp.getCalleeAttr().getValue() == "__builtin_coro_end")
        return callOp;
    }
    return {};
  }

  static bool isPureCoroutineSuspendRetBlock(mlir::Block &block,
                                             cir::CallOp coroEndCall) {
    for (mlir::Operation &op : block) {
      if (mlir::isa<cir::ConstantOp>(op))
        continue;
      if (&op == coroEndCall.getOperation())
        return true;
      if (!mlir::isa<cir::ReturnOp>(op))
        return false;
    }
    return false;
  }

  static mlir::Block *getCoroutineDestroyCleanupBlock(cir::FuncOp funcOp) {
    std::string destroyLabel = (funcOp.getSymName() + ".__coro_destroy").str();
    for (mlir::Block &block : funcOp.getBody().getBlocks()) {
      if (block.empty())
        continue;
      auto labelOp = mlir::dyn_cast<cir::LabelOp>(block.front());
      if (labelOp && labelOp.getLabel() == destroyLabel)
        return &block;
    }
    return nullptr;
  }

  // The is_in_ramp-guarded delayed get-return-object conversion cir.if
  // (tagged by CIRGen with kCoroGroConvAttr) lives in the __coro_destroy
  // block, which is only on the fallthrough-completion path. A walk finds it
  // regardless of how far flattening has un-nested the surrounding structured
  // ops (CIRIfFlattening leaves this tagged op alone until after the greedy
  // pass, so it is still a structured cir.if while awaits are processed).
  static cir::IfOp findDelayedGroConvIf(cir::FuncOp funcOp) {
    cir::IfOp result;
    funcOp.walk([&](cir::IfOp ifOp) {
      if (ifOp->hasAttr(kCoroGroConvAttr)) {
        result = ifOp;
        return mlir::WalkResult::interrupt();
      }
      return mlir::WalkResult::advance();
    });
    return result;
  }

  static bool
  hasCoroDestroyOnlyWhenCompleteAttr(cir::FuncOp funcOp) {
    if (auto extraAttrs = funcOp.getExtraAttrs())
      return extraAttrs->getElements().get("coro_only_destroy_when_complete") !=
             nullptr;
    return false;
  }

  static bool isCoroutineDestroyCleanupOp(mlir::Operation &op,
                                          mlir::SymbolTable &symTable) {
    if (mlir::isa<cir::LifetimeEndOp>(op))
      return true;

    auto callOp = mlir::dyn_cast<cir::CallOp>(op);
    if (!callOp || callOp.isIndirect())
      return false;

    if (cir::FuncOp callee = callOp.getDirectCallee(symTable))
      return callee.isCxxDestructor();

    return false;
  }

  static void collectCoroutineDestroyCleanupOps(
      mlir::Block &srcBlock, mlir::SymbolTable &symTable,
      llvm::SmallVectorImpl<mlir::Operation *> &cleanupOps) {
    llvm::SmallVector<mlir::Operation *> trailingCleanupOps;
    auto appendIfNew = [&](mlir::Operation *op) {
      for (mlir::Operation *existing : cleanupOps) {
        if (existing == op)
          return;
      }
      cleanupOps.push_back(op);
    };

    for (mlir::Operation &op : srcBlock) {
      if (!mlir::isa<cir::LifetimeEndOp>(op))
        break;
      appendIfNew(&op);
    }

    for (mlir::Operation *op = srcBlock.getTerminator()->getPrevNode(); op;
         op = op->getPrevNode()) {
      if (!isCoroutineDestroyCleanupOp(*op, symTable))
        break;
      trailingCleanupOps.push_back(op);
    }
    for (auto it = trailingCleanupOps.rbegin(); it != trailingCleanupOps.rend();
         ++it)
      appendIfNew(*it);
  }

  static bool hasCoroutineDestroyCleanupOps(mlir::Block &srcBlock,
                                            mlir::SymbolTable &symTable) {
    llvm::SmallVector<mlir::Operation *> cleanupOps;
    collectCoroutineDestroyCleanupOps(srcBlock, symTable, cleanupOps);
    return !cleanupOps.empty();
  }

  static bool isTrivialCoroutineDestroyCleanupBridge(mlir::Block &block) {
    for (mlir::Operation &op : block) {
      if (&op == block.getTerminator())
        break;
      if (!mlir::isa<cir::LabelOp>(op))
        return false;
    }

    auto brOp = mlir::dyn_cast<cir::BrOp>(block.getTerminator());
    return brOp && brOp.getDestOperands().empty();
  }

  static mlir::Block *
  findCoroutineDestroyCleanupSourceBlock(mlir::Block &srcBlock,
                                         mlir::SymbolTable &symTable) {
    if (hasCoroutineDestroyCleanupOps(srcBlock, symTable))
      return &srcBlock;

    llvm::SmallPtrSet<mlir::Block *, 4> visited;
    mlir::Block *block = &srcBlock;
    while (visited.insert(block).second) {
      auto brOp = mlir::dyn_cast<cir::BrOp>(block->getTerminator());
      if (!brOp || !brOp.getDestOperands().empty())
        return nullptr;

      mlir::Block *succ = brOp.getDest();
      if (!succ || succ->getSinglePredecessor() != block)
        return nullptr;

      if (hasCoroutineDestroyCleanupOps(*succ, symTable))
        return succ;
      if (!isTrivialCoroutineDestroyCleanupBridge(*succ))
        return nullptr;
      block = succ;
    }

    return nullptr;
  }

  static void cloneCoroutineDestroyCleanupOps(mlir::PatternRewriter &rewriter,
                                              mlir::Block &srcBlock,
                                              mlir::Block *dstBlock,
                                              mlir::SymbolTable &symTable) {
    llvm::SmallVector<mlir::Operation *> cleanupOps;
    collectCoroutineDestroyCleanupOps(srcBlock, symTable, cleanupOps);

    mlir::OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPointToEnd(dstBlock);
    for (mlir::Operation *op : cleanupOps)
      rewriter.clone(*op);
  }

  static mlir::Operation *
  findCompletedCoroutineDestroyTailStart(mlir::Block &srcBlock,
                                         mlir::SymbolTable &symTable) {
    for (mlir::Operation &op : srcBlock) {
      auto callOp = mlir::dyn_cast<cir::CallOp>(op);
      if (!callOp || callOp.isIndirect())
        continue;

      if (cir::FuncOp callee = callOp.getDirectCallee(symTable);
          callee && callee.getSymName() == "__builtin_coro_free") {
        mlir::Operation *start = &op;
        if (mlir::Operation *prev = op.getPrevNode();
            prev && mlir::isa<cir::LifetimeEndOp>(prev))
          start = prev;
        return start;
      }
    }

    return nullptr;
  }

  static mlir::Block *
  findCompletedCoroutineDestroyTailSourceBlock(mlir::Block &srcBlock,
                                               mlir::SymbolTable &symTable) {
    llvm::SmallPtrSet<mlir::Block *, 4> visited;
    mlir::Block *block = &srcBlock;
    while (visited.insert(block).second) {
      if (findCompletedCoroutineDestroyTailStart(*block, symTable))
        return block;

      auto brOp = mlir::dyn_cast<cir::BrOp>(block->getTerminator());
      if (!brOp || !brOp.getDestOperands().empty())
        return nullptr;
      block = brOp.getDest();
      if (!block)
        return nullptr;
    }

    return nullptr;
  }

  static mlir::Block *cloneCompletedCoroutineDestroyTail(
      mlir::PatternRewriter &rewriter, mlir::Block &srcBlock,
      mlir::Block *insertBefore, mlir::SymbolTable &symTable) {
    mlir::Operation *start =
        findCompletedCoroutineDestroyTailStart(srcBlock, symTable);
    if (!start)
      return nullptr;

    mlir::Block *dstBlock = rewriter.createBlock(insertBefore);
    mlir::OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPointToEnd(dstBlock);
    mlir::IRMapping mapper;
    for (mlir::Operation *op = start; op; op = op->getNextNode())
      rewriter.clone(*op, mapper);
    return dstBlock;
  }

  static bool cloneCleanupTailBeforeYield(mlir::PatternRewriter &rewriter,
                                          mlir::Block &srcBlock,
                                          mlir::Block *dstBlock) {
    auto yieldOp = mlir::dyn_cast<cir::YieldOp>(srcBlock.getTerminator());
    if (!yieldOp)
      return false;

    mlir::OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPointToEnd(dstBlock);
    mlir::IRMapping mapper;
    for (mlir::Operation &op : llvm::make_early_inc_range(srcBlock)) {
      if (&op == yieldOp.getOperation())
        break;
      rewriter.clone(op, mapper);
    }
    return true;
  }

  static llvm::SmallVector<mlir::Value>
  getBranchOperandsOrUndef(mlir::PatternRewriter &rewriter, mlir::Location loc,
                           mlir::Block *dest,
                           mlir::ValueRange operands = mlir::ValueRange{}) {
    llvm::SmallVector<mlir::Value> branchOperands(operands.begin(),
                                                  operands.end());
    if (!dest || !branchOperands.empty() || dest->getNumArguments() == 0)
      return branchOperands;

    branchOperands.reserve(dest->getNumArguments());
    for (mlir::BlockArgument arg : dest->getArguments()) {
      auto undef =
          cir::ConstantOp::create(rewriter, loc, cir::UndefAttr::get(arg.getType()));
      branchOperands.push_back(undef.getRes());
    }
    return branchOperands;
  }

  /// Find or create a builtin function declaration in the module.
  cir::FuncOp getOrCreateBuiltin(mlir::PatternRewriter &rewriter,
                                 mlir::ModuleOp mod,
                                 mlir::SymbolTable &symTable,
                                 mlir::Location loc, llvm::StringRef name,
                                 cir::FuncType fnTy) const {
    if (auto existing = symTable.lookup<cir::FuncOp>(name))
      return existing;
    mlir::OpBuilder::InsertionGuard modGuard(rewriter);
    rewriter.setInsertionPointToStart(mod.getBody());
    auto fn = cir::FuncOp::create(rewriter, loc, name, fnTy);
    fn.setPrivate();
    fn.setBuiltin(true);
    symTable.insert(fn);
    return fn;
  }

  void populateCoroutineSuspendRetBlock(mlir::PatternRewriter &rewriter,
                                        cir::FuncOp funcOp,
                                        mlir::ModuleOp modOp,
                                        mlir::SymbolTable &symTable,
                                        mlir::Location loc,
                                        mlir::Block *coroRetBlock,
                                        cir::PointerType ptrToVoid,
                                        cir::BoolType boolTy) const {
    mlir::OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPointToStart(coroRetBlock);

    auto ctx = rewriter.getContext();
    auto coroEndNull = cir::ConstantOp::create(
        rewriter, loc,
        cir::ConstPtrAttr::get(ptrToVoid, rewriter.getI64IntegerAttr(0)));
    auto coroEndFalse = cir::ConstantOp::create(
        rewriter, loc, boolTy, cir::BoolAttr::get(ctx, boolTy, false));
    (void)getOrCreateBuiltin(rewriter, modOp, symTable, loc, "__builtin_coro_end",
                             cir::FuncType::get({ptrToVoid, boolTy}, boolTy));
    cir::CallOp::create(rewriter, loc,
                        mlir::SymbolRefAttr::get(ctx, "__builtin_coro_end"),
                        boolTy,
                        mlir::ValueRange{coroEndNull, coroEndFalse});
    if (funcOp.getFunctionType().hasVoidReturn()) {
      cir::ReturnOp::create(rewriter, loc);
    } else {
      mlir::Type retTy = funcOp.getFunctionType().getReturnType();
      auto undef =
          cir::ConstantOp::create(rewriter, loc, cir::UndefAttr::get(retTy));
      cir::ReturnOp::create(rewriter, loc, undef.getRes());
    }
  }

  mlir::Block *
  getOrCreateCoroutineSuspendRetBlock(mlir::PatternRewriter &rewriter,
                                      cir::FuncOp funcOp, mlir::ModuleOp modOp,
                                      mlir::SymbolTable &symTable,
                                      mlir::Location loc,
                                      cir::PointerType ptrToVoid,
                                      cir::BoolType boolTy) const {
    for (mlir::Block &block : funcOp.getBody().getBlocks()) {
      cir::CallOp coroEndCall = getCoroutineEndCall(block);
      if (!coroEndCall)
        continue;
      if (isPureCoroutineSuspendRetBlock(block, coroEndCall))
        return &block;

      mlir::OpBuilder::InsertionGuard guard(rewriter);
      auto splitIt = mlir::Block::iterator(coroEndCall);
      while (splitIt != block.begin()) {
        auto prevIt = std::prev(splitIt);
        if (!mlir::isa<cir::ConstantOp>(*prevIt))
          break;
        splitIt = prevIt;
      }

      mlir::Block *coroRetBlock = rewriter.splitBlock(&block, splitIt);
      rewriter.setInsertionPointToEnd(&block);
      cir::BrOp::create(rewriter, loc, coroRetBlock);

      // Preserve the existing return-value tail after coro.end (for example,
      // the by-value coroutine handle load/return), but keep any local
      // constants feeding coro.end in the shared tail block so suspend-return
      // predecessors stay dominance-correct when they branch to it.
      return coroRetBlock;
    }

    auto *coroRetBlock = new mlir::Block();
    funcOp.getBody().push_back(coroRetBlock);

    populateCoroutineSuspendRetBlock(rewriter, funcOp, modOp, symTable, loc,
                                     coroRetBlock, ptrToVoid, boolTy);
    return coroRetBlock;
  }

  mlir::LogicalResult
  matchAndRewrite(cir::AwaitOp op,
                  mlir::PatternRewriter &rewriter) const override {
    mlir::OpBuilder::InsertionGuard guard(rewriter);
    mlir::Location loc = op.getLoc();
    mlir::MLIRContext *ctx = op.getContext();

    mlir::Block *currentBlock = rewriter.getInsertionBlock();
    mlir::Block *afterAwait =
        rewriter.splitBlock(currentBlock, rewriter.getInsertionPoint());

    // Check if we're in a coroutine function — only inject coro.save/suspend
    // for coroutine functions.
    auto funcOp = op->getParentOfType<cir::FuncOp>();
    bool isCoroutine = funcOp && funcOp.getCoroutine();
    bool isFinalSuspend = (op.getKind() == cir::AwaitKind::Final);

    // --- Resume region ---
    // Inline resume region before afterAwait. Replace its yield terminator
    // with a branch to afterAwait.
    mlir::Block *resumeEntry = &op.getResume().front();
    mlir::Block *resumeExit = &op.getResume().back();
    rewriter.inlineRegionBefore(op.getResume(), afterAwait);

    rewriter.setInsertionPointToEnd(resumeExit);
    if (auto yieldOp = dyn_cast<cir::YieldOp>(resumeExit->getTerminator()))
      rewriter.replaceOpWithNewOp<cir::BrOp>(
          yieldOp, getBranchOperandsOrUndef(rewriter, yieldOp.getLoc(),
                                            afterAwait, yieldOp.getArgs()),
          afterAwait);

    // --- Cleanup/destroy region (optional) ---
    // The await may carry a 4th region holding the destructors of locals live
    // at this suspend point. Capture and inline it now, BEFORE the op is erased
    // below, so the non-final coroutine destroy edge can route through it. The
    // region is non-empty only for non-final suspends with live local cleanups.
    mlir::Block *cleanupRegionEntry = nullptr;
    mlir::Block *cleanupRegionExit = nullptr;
    if (!op.getCleanup().empty()) {
      cleanupRegionEntry = &op.getCleanup().front();
      cleanupRegionExit = &op.getCleanup().back();
      rewriter.inlineRegionBefore(op.getCleanup(), afterAwait);
    }

    // --- Suspend region ---
    // Inline suspend region before resume.
    mlir::Block *suspendEntry = &op.getSuspend().front();
    mlir::Block *suspendExit = &op.getSuspend().back();
    rewriter.inlineRegionBefore(op.getSuspend(), resumeEntry);
    mlir::Block *cleanupBlock = nullptr;
    mlir::Block *suspRetBlock = nullptr;

    if (isCoroutine) {
      // Inject coro.save at the start of the suspend block and
      // coro.suspend at the end, with a three-way switch on the result.
      auto modOp = op->getParentOfType<mlir::ModuleOp>();
      auto voidTy = cir::VoidType::get(ctx);
      auto ptrToVoid = cir::PointerType::get(voidTy);
      auto u32Ty = cir::IntType::get(ctx, 32, /*isSigned=*/false);
      auto u8Ty = cir::IntType::get(ctx, 8, /*isSigned=*/false);
      auto boolTy = cir::BoolType::get(ctx);

      // __builtin_coro_save(ptr<void>) -> u32i (token in LLVM)
      (void)getOrCreateBuiltin(rewriter, modOp, symTable, loc,
                               "__builtin_coro_save",
                               cir::FuncType::get({ptrToVoid}, u32Ty));

      // __builtin_coro_suspend(u32i, bool) -> u8i
      (void)getOrCreateBuiltin(rewriter, modOp, symTable, loc,
                               "__builtin_coro_suspend",
                               cir::FuncType::get({u32Ty, boolTy}, u8Ty));

      // Insert coro.save(null) at the beginning of the suspend block.
      rewriter.setInsertionPointToStart(suspendEntry);
      auto nullPtr = cir::ConstantOp::create(
          rewriter, loc,
          cir::ConstPtrAttr::get(ptrToVoid,
                                 rewriter.getI64IntegerAttr(0)));
      auto saveCall = cir::CallOp::create(
          rewriter, loc,
          mlir::SymbolRefAttr::get(ctx, "__builtin_coro_save"),
          u32Ty, mlir::ValueRange{nullPtr});

      // Check if the suspend region ends with a ConditionOp (veto case:
      // bool-returning await_suspend) or YieldOp (normal case).
      auto suspendCond =
          dyn_cast<cir::ConditionOp>(suspendExit->getTerminator());
      auto suspendYield =
          dyn_cast<cir::YieldOp>(suspendExit->getTerminator());
      assert((suspendCond || suspendYield) &&
             "suspend region must end with cir.yield or cir.condition");

      // Create all auxiliary blocks upfront.
      // cleanup: coroutine destroyed — branch to afterAwait.
      cleanupBlock = rewriter.createBlock(resumeEntry);
      // suspend-return: actual suspension — branch to the shared coro.ret
      // block after the surrounding function epilogue has been split.
      suspRetBlock = rewriter.createBlock(resumeEntry);
      // For veto case: a separate block for coro.suspend (only entered when
      // await_suspend returns true, i.e. suspension is not vetoed).
      mlir::Block *coroSuspendBlock =
          suspendCond ? rewriter.createBlock(resumeEntry) : nullptr;

      // Emit coro.suspend + resume check into the appropriate block.
      if (suspendCond) {
        rewriter.setInsertionPointToEnd(coroSuspendBlock);
      } else {
        rewriter.setInsertionPoint(suspendYield);
      }

      // coro.suspend(save_token, is_final) -> u8
      auto isFinalVal = cir::ConstantOp::create(
          rewriter, loc, boolTy,
          cir::BoolAttr::get(ctx, boolTy, isFinalSuspend));
      auto suspCall = cir::CallOp::create(
          rewriter, loc,
          mlir::SymbolRefAttr::get(ctx, "__builtin_coro_suspend"),
          u8Ty, mlir::ValueRange{saveCall.getResult(), isFinalVal});
      mlir::Value suspResult = suspCall.getResult();

      // Three-way dispatch on coro.suspend result using cir.switch.flat,
      // matching OGCG's 'switch i8 %result, label %coro.ret [i8 0, %resume;
      // i8 1, %cleanup]'. CoroSplit requires this switch pattern.
      llvm::SmallVector<llvm::APInt, 2> caseValues = {
          llvm::APInt(8, 0), // 0 = resume
          llvm::APInt(8, 1), // 1 = cleanup
      };
      llvm::SmallVector<mlir::Block *, 2> caseDests = {
          resumeEntry, cleanupBlock};
      llvm::SmallVector<mlir::ValueRange, 2> caseOps = {{}, {}};

      if (suspendCond) {
        // Veto case: coroSuspendBlock gets the switch dispatch.
        cir::SwitchFlatOp::create(
            rewriter, loc, suspResult, suspRetBlock, mlir::ValueRange{},
            caseValues, caseDests, caseOps);

        // Replace ConditionOp with veto branch:
        // true -> coroSuspendBlock (do suspend),
        // false -> resumeEntry (veto: skip suspend, go to resume).
        rewriter.setInsertionPoint(suspendCond);
        rewriter.replaceOpWithNewOp<cir::BrCondOp>(
            suspendCond, suspendCond.getCondition(),
            coroSuspendBlock, resumeEntry);
      } else {
        // Normal case: replace yield with switch dispatch.
        rewriter.replaceOpWithNewOp<cir::SwitchFlatOp>(
            suspendYield, suspResult, suspRetBlock, mlir::ValueRange{},
            caseValues, caseDests, caseOps);
      }
    } else {
      // Not a coroutine — simple branch from suspend to resume.
      rewriter.setInsertionPointToEnd(suspendExit);
      if (auto suspendYield =
              dyn_cast<cir::YieldOp>(suspendExit->getTerminator()))
        rewriter.replaceOpWithNewOp<cir::BrOp>(suspendYield, resumeEntry);
    }

    // --- Ready region ---
    // Inline ready region before suspend. Replace the cir.condition terminator
    // with a conditional branch: true -> resume, false -> suspend.
    mlir::Block *readyEntry = &op.getReady().front();
    mlir::Block *readyExit = &op.getReady().back();
    rewriter.inlineRegionBefore(op.getReady(), suspendEntry);

    auto condOp = dyn_cast<cir::ConditionOp>(readyExit->getTerminator());
    assert(condOp && "ready region must end with cir.condition");
    rewriter.setInsertionPointToEnd(readyExit);
    // cir.condition: true means await_ready() returned true -> skip to resume.
    // false means not ready -> go through suspend.
    rewriter.replaceOpWithNewOp<cir::BrCondOp>(condOp, condOp.getCondition(),
                                                resumeEntry, suspendEntry);

    // Branch from current block into the ready region.
    rewriter.setInsertionPointToEnd(currentBlock);
    cir::BrOp::create(rewriter, loc, readyEntry);

    rewriter.eraseOp(op);
    if (isCoroutine) {
      auto modOp = funcOp->getParentOfType<mlir::ModuleOp>();
      auto ptrToVoid = cir::PointerType::get(cir::VoidType::get(ctx));
      auto boolTy = cir::BoolType::get(ctx);
      mlir::Block *coroRetBlock = getOrCreateCoroutineSuspendRetBlock(
          rewriter, funcOp, modOp, symTable, loc, ptrToVoid, boolTy);
      rewriter.setInsertionPointToStart(suspRetBlock);
      // Delayed get-return-object conversion on the suspend-return-to-caller
      // path. The is_in_ramp-guarded conversion cir.if that CIRGen emits
      // (tagged kCoroGroConvAttr) lives in the __coro_destroy block, which is
      // only on the FALLTHROUGH-completion path; a coroutine that suspends at
      // initial/final suspend returns to the caller through this switch default
      // and would otherwise skip the conversion (leaving the return value
      // unconstructed and swallowing a throwing converting ctor). Clone the
      // guarded conversion here so it runs on every ramp suspend-return too,
      // matching classic CodeGen's EmitGroConv at Coro.SuspendBB
      // (CGCoroutine.cpp:815-856). The clone keeps the is_in_ramp guard, so it
      // is a no-op on the resume/destroy clones; the ramp takes exactly one
      // exit (the first suspend that suspends, or fallthrough completion) and
      // the guard is true only on the ramp, so the converting ctor runs
      // EXACTLY once. CIRAwaitOpFlattening runs per cir.await, so this covers
      // initial, final, and user suspends. The tagged cir.if is self-contained
      // (its converting-ctor cir.try carries its own throw cleanup -- frame
      // free + coro.end + resume -- entirely inside the then-region, with all
      // operands being function-entry allocas / coro tokens that dominate every
      // suspend point), so cloning it via IRMapping introduces no dangling
      // cross-block edges and a throwing converting ctor propagates correctly
      // from the suspend-return edge as well. The marker is stripped from the
      // clone so the greedy driver flattens it normally in this same run.
      if (cir::IfOp groConv = findDelayedGroConvIf(funcOp)) {
        // The conversion cir.if's condition is a __builtin_coro_is_in_ramp
        // call defined in the __coro_destroy (fallthrough) block, which does
        // not dominate this suspend-return block. Emit a fresh is_in_ramp call
        // here and map it in so the clone is guarded by a dominating value.
        (void)getOrCreateBuiltin(rewriter, modOp, symTable, loc,
                                 "__builtin_coro_is_in_ramp",
                                 cir::FuncType::get({}, boolTy));
        auto isInRamp = cir::CallOp::create(
            rewriter, loc,
            mlir::SymbolRefAttr::get(ctx, "__builtin_coro_is_in_ramp"), boolTy,
            mlir::ValueRange{});
        mlir::IRMapping mapper;
        mapper.map(groConv.getCondition(), isInRamp.getResult());
        mlir::Operation *cloned =
            rewriter.clone(*groConv.getOperation(), mapper);
        cloned->removeAttr(kCoroGroConvAttr);
      }
      rewriter.setInsertionPointToEnd(suspRetBlock);
      cir::BrOp::create(rewriter, loc,
                        getBranchOperandsOrUndef(rewriter, loc, coroRetBlock),
                        coroRetBlock);

      if (isFinalSuspend &&
          hasCoroDestroyOnlyWhenCompleteAttr(funcOp)) {
        auto afterAwaitBr = mlir::dyn_cast<cir::BrOp>(afterAwait->getTerminator());
        if (afterAwaitBr && afterAwaitBr.getDestOperands().empty())
          if (mlir::Block *tailSrc =
                  findCompletedCoroutineDestroyTailSourceBlock(*afterAwait,
                                                               symTable))
            if (mlir::Block *destroyTail = cloneCompletedCoroutineDestroyTail(
                    rewriter, *tailSrc, coroRetBlock, symTable)) {
              rewriter.setInsertionPoint(afterAwaitBr);
              rewriter.replaceOpWithNewOp<cir::BrOp>(afterAwaitBr, destroyTail);
              rewriter.setInsertionPointToStart(cleanupBlock);
              cir::BrOp::create(rewriter, loc, destroyTail);
              return mlir::success();
            }
      }

      rewriter.setInsertionPointToStart(cleanupBlock);
      if (mlir::Block *destroyCleanupBlock =
              getCoroutineDestroyCleanupBlock(funcOp)) {
        if (isFinalSuspend) {
          if (cloneCleanupTailBeforeYield(rewriter, *afterAwait,
                                          cleanupBlock)) {
            cir::BrOp::create(
                rewriter, loc,
                getBranchOperandsOrUndef(rewriter, loc, destroyCleanupBlock),
                destroyCleanupBlock);
            return mlir::success();
          }
        } else {
          if (cleanupRegionEntry) {
            // Route the i8==1 destroy edge AND the resume continuation through a
            // shared dest-slot SELECTOR block, matching classic CodeGen's
            // cleanup.dest.slot model. This is required for symmetric-transfer
            // (SuspendHandle) co_yields consumed via co_nothrow: if the destroy
            // edge instead runs the body-local destructor in its own block and
            // branches straight to the frame-dealloc tail (a destroy-only
            // side-path), then after the trivial destructor is inlined, LLVM
            // simplifycfg merges that copy with the coroutine's EH-unwind
            // destructor copies into blocks reachable from several suspend
            // switches; LLVM CoroSplit cannot then partition that structure
            // cleanly into the .resume/.destroy clones, so the awaiting Task's
            // resume chain never completes -> blockingWait baton never posts ->
            // hang. Classic avoids this by funnelling the destroy edge into the
            // post-resume merge block via an integer selector (a phi), so the
            // destructor block is dominated by a block that is also on the
            // resume path and stays uniform/collapsible per clone.
            //
            // Topology produced (mirrors classic %50/%45 -> %51 switch -> %64):
            //   resumeExit:   ... -> br selBlock(0)        ; selector = normal
            //   cleanupBlock: -> br selBlock(1)            ; selector = destroy
            //   selBlock(%sel: u8):
            //       switch %sel [ 0 -> afterAwait ; default -> dtorEntry ]
            //   dtorEntry .. dtorExit: <body-local dtors> -> destroyCleanupBlock
            // CoroSplit folds %sel to a constant per clone (0 in .resume, 1 in
            // .destroy), so the destructor is DCE'd from .resume and kept in
            // .destroy, with no destroy-only mergeable side-path.
            // The dest-slot routing requires a clean `br afterAwait` (no
            // operands) at the resume region exit so we can thread it through
            // the selector. This holds for the body-local-cleanup suspends we
            // target. If the resume exit is shaped differently, fall back to
            // the legacy inline-destructor placement below.
            //
            // LOAD-BEARING INVARIANT: for non-final symmetric-transfer
            // (SuspendHandle) and conditional (SuspendBool) suspends carrying a
            // cleanup region, canDestSlot MUST hold -- the legacy fallback
            // re-creates the destroy-only side-path that LLVM CoroSplit
            // mis-lowers into the co_nothrow deadlock this transform exists to
            // fix. It holds today because await_resume's result is materialized
            // through a temporary alloca, never an `afterAwait` block argument,
            // so the resume exit is always an operand-less `br afterAwait`. A
            // future change that threads a resume value through afterAwait block
            // args would silently flip canDestSlot to false and reintroduce the
            // hang with no compile-time signal. coroutine-destroy-cleanup-
            // symmetric-transfer.cpp pins the selector topology to catch that.
            auto resumeBr = dyn_cast<cir::BrOp>(resumeExit->getTerminator());
            bool canDestSlot = resumeBr && resumeBr.getDest() == afterAwait &&
                               resumeBr.getDestOperands().empty();
            if (canDestSlot) {
              auto u8Ty = cir::IntType::get(ctx, 8, /*isSigned=*/false);
              mlir::Block *selBlock = rewriter.createBlock(
                  afterAwait, mlir::TypeRange{u8Ty}, {loc});

              // Redirect the resume continuation to `br selBlock(0)`.
              rewriter.setInsertionPoint(resumeBr);
              auto zero = cir::ConstantOp::create(
                  rewriter, loc, u8Ty, cir::IntAttr::get(u8Ty, 0));
              rewriter.replaceOpWithNewOp<cir::BrOp>(
                  resumeBr, mlir::ValueRange{zero.getResult()}, selBlock);

              // The i8==1 destroy switch-case block branches to selBlock(1)
              // with NO inline destructor of its own.
              rewriter.setInsertionPointToEnd(cleanupBlock);
              auto one = cir::ConstantOp::create(rewriter, loc, u8Ty,
                                                 cir::IntAttr::get(u8Ty, 1));
              cir::BrOp::create(rewriter, loc,
                                mlir::ValueRange{one.getResult()}, selBlock);

              // The cleanup region (already inlined) is the destructor block(s);
              // rewrite its terminating yield to branch to the frame-dealloc
              // destroy tail.
              auto cleanupYield =
                  cast<cir::YieldOp>(cleanupRegionExit->getTerminator());
              rewriter.setInsertionPoint(cleanupYield);
              rewriter.replaceOpWithNewOp<cir::BrOp>(
                  cleanupYield,
                  getBranchOperandsOrUndef(rewriter, loc, destroyCleanupBlock),
                  destroyCleanupBlock);

              // The selector switch: 0 -> normal continuation (afterAwait);
              // default (the destroy selector 1) -> the destructor entry.
              rewriter.setInsertionPointToEnd(selBlock);
              llvm::SmallVector<llvm::APInt, 1> selCaseValues = {
                  llvm::APInt(8, 0)};
              llvm::SmallVector<mlir::Block *, 1> selCaseDests = {afterAwait};
              llvm::SmallVector<mlir::ValueRange, 1> selCaseOps = {{}};
              cir::SwitchFlatOp::create(
                  rewriter, loc, selBlock->getArgument(0),
                  /*defaultDest=*/cleanupRegionEntry,
                  /*defaultOperands=*/mlir::ValueRange{}, selCaseValues,
                  selCaseDests, selCaseOps);
              return mlir::success();
            }

            // Legacy fallback: move the cleanup region's destructor ops into
            // the i8==1 destroy switch-case block and branch to the destroy
            // tail. Used when the resume exit cannot be cleanly threaded.
            if (cleanupRegionEntry == cleanupRegionExit) {
              rewriter.mergeBlocks(cleanupRegionEntry, cleanupBlock);
              auto cleanupYield =
                  cast<cir::YieldOp>(cleanupBlock->getTerminator());
              rewriter.setInsertionPoint(cleanupYield);
              rewriter.replaceOpWithNewOp<cir::BrOp>(
                  cleanupYield,
                  getBranchOperandsOrUndef(rewriter, loc, destroyCleanupBlock),
                  destroyCleanupBlock);
            } else {
              cir::BrOp::create(
                  rewriter, loc,
                  getBranchOperandsOrUndef(rewriter, loc, cleanupRegionEntry),
                  cleanupRegionEntry);
              auto cleanupYield =
                  cast<cir::YieldOp>(cleanupRegionExit->getTerminator());
              rewriter.setInsertionPoint(cleanupYield);
              rewriter.replaceOpWithNewOp<cir::BrOp>(
                  cleanupYield,
                  getBranchOperandsOrUndef(rewriter, loc, destroyCleanupBlock),
                  destroyCleanupBlock);
            }
            return mlir::success();
          }
          // Fallback (no explicit cleanup region): the legacy syntactic scrape
          // of trailing destructor ops from the resume continuation block.
          if (mlir::Block *cleanupSrc =
                  findCoroutineDestroyCleanupSourceBlock(*afterAwait, symTable))
            cloneCoroutineDestroyCleanupOps(rewriter, *cleanupSrc, cleanupBlock,
                                            symTable);
          cir::BrOp::create(
              rewriter, loc,
              getBranchOperandsOrUndef(rewriter, loc, destroyCleanupBlock),
              destroyCleanupBlock);
          return mlir::success();
        }
      }
      cir::BrOp::create(
          rewriter, loc, getBranchOperandsOrUndef(rewriter, loc, afterAwait),
          afterAwait);
    }
    return mlir::success();
  }
};

class CIRTryOpFlattening : public mlir::OpRewritePattern<cir::TryOp> {
  mlir::SymbolTable &symTable;

public:
  CIRTryOpFlattening(mlir::MLIRContext *ctx, mlir::SymbolTable &symTable)
      : OpRewritePattern<cir::TryOp>(ctx), symTable(symTable) {}

  void promoteEnclosingEHEndCatchOps(cir::TryOp tryOp,
                                     mlir::PatternRewriter &rewriter) const {
    llvm::SmallVector<cir::CatchParamOp, 2> endCatchOpsToPromote;
    // Walk both try and catch regions for end_catch ops that need
    // invoke treatment. Try region catches promoted ops from inner
    // flattened tries. Catch regions have ops from early exit paths
    // where the enclosing scope has active cleanups.
    auto collectEndCatchOps = [&](cir::CatchParamOp catchParam) {
      if (catchParam->getParentOfType<cir::TryOp>() != tryOp)
        return;
      if (!catchParam.isEnd() ||
          !catchParam->hasAttr("cir.end_catch_enclosing_eh"))
        return;
      endCatchOpsToPromote.push_back(catchParam);
    };
    tryOp.getTryRegion().walk(collectEndCatchOps);
    for (auto &catchRegion : tryOp.getCatchRegions())
      catchRegion.walk(collectEndCatchOps);

    auto *ctx = rewriter.getContext();
    auto voidTy = cir::VoidType::get(ctx);
    auto endCatchFnTy = cir::FuncType::get({}, voidTy);

    for (cir::CatchParamOp endCatchOp : endCatchOpsToPromote) {
      auto catchEndFnAttr =
          endCatchOp->getAttrOfType<StringAttr>(kCatchEndFnAttr);
      StringRef fnName =
          catchEndFnAttr ? catchEndFnAttr.getValue() : "__cxa_end_catch";

      auto modOp = endCatchOp->getParentOfType<mlir::ModuleOp>();
      if (auto catchEndFn = symTable.lookup<cir::FuncOp>(fnName)) {
        if (catchEndFn.getFunctionType() != endCatchFnTy)
          catchEndFn.setFunctionType(endCatchFnTy);
      } else {
        mlir::OpBuilder::InsertionGuard modGuard(rewriter);
        rewriter.setInsertionPointToStart(modOp.getBody());
        auto newCatchEndFn =
            cir::FuncOp::create(rewriter, endCatchOp.getLoc(), fnName,
                                endCatchFnTy);
        newCatchEndFn.setPrivate();
        symTable.insert(newCatchEndFn);
      }

      rewriter.setInsertionPoint(endCatchOp);
      auto callOp = cir::CallOp::create(
          rewriter, endCatchOp.getLoc(), mlir::SymbolRefAttr::get(ctx, fnName),
          /*resType=*/mlir::Type{}, mlir::ValueRange{}, cir::CallingConv::C,
          cir::SideEffect::All, rewriter.getUnitAttr());

      mlir::Region &cleanup = callOp.getCleanup();
      mlir::Block *cleanupBlock = rewriter.createBlock(&cleanup);
      rewriter.setInsertionPointToEnd(cleanupBlock);
      cir::YieldOp::create(rewriter, endCatchOp.getLoc());

      rewriter.eraseOp(endCatchOp);
    }
  }

  static void collectExceptionCallsInBlocks(
      mlir::Block *beginBlock, mlir::Block *endBlock,
      SmallVectorImpl<std::pair<cir::CallOp, bool>> &handlerCallsToRewrite,
      bool endCatchNothrow, bool rawHandler) {
    auto collectNestedCalls = [&](auto &&self, mlir::Operation &op) -> void {
      if (mlir::isa<cir::TryOp>(op))
        return;
      if (auto callOp = mlir::dyn_cast<cir::CallOp>(op);
          callOp && callOp.getException()) {
        if (rawHandler)
          callOp->setAttr(kCatchRawHandlerAttr,
                          mlir::UnitAttr::get(callOp.getContext()));
        handlerCallsToRewrite.push_back({callOp, endCatchNothrow});
      }
      for (mlir::Region &nestedRegion : op.getRegions())
        for (mlir::Block &nestedBlock : nestedRegion)
          for (mlir::Operation &nestedOp : nestedBlock)
            self(self, nestedOp);
    };

    for (auto it = beginBlock->getIterator(); &*it != endBlock; ++it)
      for (mlir::Operation &op : *it)
        collectNestedCalls(collectNestedCalls, op);
  }

  mlir::Block *buildTypeCase(mlir::PatternRewriter &rewriter, mlir::Region &r,
                             mlir::Block *afterTry,
                             mlir::Type exceptionPtrTy,
                             bool hasEnclosingEH, bool isNoexceptFn,
                             SmallVectorImpl<std::pair<cir::CallOp, bool>>
                                 &handlerCallsToRewrite) const {
    // Erase dead blocks in the catch handler region. co_return inside a catch
    // handler in a coroutine generates unreachable blocks with stale yields
    // that would trigger the single-yield assertion below.
    for (auto it = std::next(r.begin()), e = r.end(); it != e;) {
      mlir::Block &block = *it++;
      if (block.hasNoPredecessors() && !blockHoldsLabel(block)) {
        eraseDeadBlock(rewriter, block);
      }
    }

    YieldOp yieldOp;
    llvm::SmallVector<CatchParamOp, 2> paramOps;
    // Collect non-yield exit terminators that leave the catch handler without
    // going through the yield path. Each needs __cxa_end_catch inserted before
    // it. Walk recursively but skip nested TryOp regions (those manage their
    // own exception state).
    //
    // ReturnOp and GotoOp always exit the catch handler regardless of nesting.
    // BreakOp and ContinueOp only exit the catch handler if they are NOT inside
    // a SwitchOp or loop within the catch — otherwise they exit the switch/loop,
    // not the catch handler.
    llvm::SmallVector<mlir::Operation *, 2> exitOps;
    for (auto &block : r)
      for (auto &op : block) {
        if (auto y = mlir::dyn_cast<YieldOp>(op)) {
          assert(!yieldOp && "expect to only find one");
          yieldOp = y;
        }
      }
    walkRegionSkipping<cir::TryOp>(r, [&](mlir::Operation *op) {
      if (mlir::isa<cir::ReturnOp, cir::InlineAsmGotoOp>(op))
        exitOps.push_back(op);
      else if (auto gotoOp = mlir::dyn_cast<cir::GotoOp>(op)) {
        // A goto whose target label lives inside this catch handler stays
        // within the handler; only a goto leaving the handler needs
        // __cxa_end_catch inserted before it.
        if (!gotoTargetsLabelInRegion(gotoOp, r))
          exitOps.push_back(op);
      }
      return mlir::WalkResult::advance();
    });
    walkRegionSkipping<cir::TryOp, cir::SwitchOp, cir::LoopOpInterface>(
        r, [&](mlir::Operation *op) {
          if (mlir::isa<cir::BreakOp, cir::ContinueOp>(op))
            exitOps.push_back(op);
          return mlir::WalkResult::advance();
        });
    for (auto &block : r)
      for (auto &op : block)
        if (auto p = mlir::dyn_cast<CatchParamOp>(op))
          // Skip CatchParamOps that already have a kind attribute — these
          // belong to flattened inner TryOps (e.g., synthetic terminate
          // handlers) and must not be rewritten with this catch handler's
          // exception pointer.
          if (!p.getKind())
            paramOps.push_back(p);
    assert(!paramOps.empty() && "expected at least one catch_param");

    // Check if the catch handler's __cxa_end_catch is nothrow (non-record
    // type like int/pointer). Propagate to End ops so LowerToLLVM emits a
    // plain call instead of invoke-to-terminate.
    bool endCatchNothrow = llvm::any_of(paramOps, [](CatchParamOp p) {
      return p->hasAttr("cir.end_catch_nothrow");
    });
    bool endCatchNormalCleanup = llvm::any_of(paramOps, [](CatchParamOp p) {
      return p->hasAttr("cir.end_catch_normal_cleanup");
    });
    bool rawHandler = llvm::any_of(paramOps, [](CatchParamOp p) {
      return p->hasAttr(kCatchRawHandlerAttr);
    });

    rewriter.inlineRegionBefore(r, afterTry);

    // The first CatchParamOp is the entry point for the catch handler.
    // It defines the entry block and receives the exception pointer argument.
    CatchParamOp firstParam = paramOps[0];

    // Rewrite `cir.catch_param` to be scope aware and instead generate:
    // ```
    //   cir.catch_param begin %exception_ptr
    //   ...
    //   cir.catch_param end
    //   cir.br ...
    mlir::Value catchResult = firstParam.getParam();
    assert(catchResult && "expected to be available");
    rewriter.setInsertionPointAfterValue(catchResult);
    mlir::Block *entryBlock = firstParam->getBlock();
    mlir::Location catchLoc = firstParam.getLoc();
    auto catchBeginFn = firstParam->getAttrOfType<StringAttr>(kCatchBeginFnAttr);
    auto catchEndFn = firstParam->getAttrOfType<StringAttr>(kCatchEndFnAttr);
    // Catch handler only gets the exception pointer (selection not needed).
    mlir::Value exceptionPtr =
        entryBlock->addArgument(exceptionPtrTy, firstParam.getLoc());

    collectExceptionCallsInBlocks(entryBlock, afterTry, handlerCallsToRewrite,
                                  endCatchNothrow, rawHandler);

    if (rawHandler) {
      assert(paramOps.size() == 1 && "raw catch handler should use one param");
      mlir::Value rawException = exceptionPtr;
      if (rawException.getType() != catchResult.getType()) {
        rewriter.setInsertionPoint(firstParam);
        rawException = cir::CastOp::create(rewriter, catchLoc,
                                           catchResult.getType(),
                                           cir::CastKind::bitcast, rawException);
      }
      rewriter.replaceOp(firstParam, rawException);
      if (!yieldOp)
        return entryBlock;

      rewriter.setInsertionPointToEnd(yieldOp->getBlock());
      rewriter.replaceOpWithNewOp<cir::BrOp>(yieldOp, afterTry);
      return entryBlock;
    }

    // When there are two CatchParamOps (catch-by-value with non-trivial copy
    // ctor), the first is __cxa_get_exception_ptr and the second is
    // __cxa_begin_catch. When there's only one, it's __cxa_begin_catch.
    if (paramOps.size() == 1) {
      auto catchType = catchResult.getType();
      auto beginOp = rewriter.replaceOpWithNewOp<cir::CatchParamOp>(
          firstParam, catchType, exceptionPtr,
          cir::CatchParamKindAttr::get(rewriter.getContext(),
                                       cir::CatchParamKind::Begin));
      if (catchBeginFn)
        beginOp->setAttr(kCatchBeginFnAttr, catchBeginFn);
      if (catchEndFn)
        beginOp->setAttr(kCatchEndFnAttr, catchEndFn);
    } else {
      // First param: __cxa_get_exception_ptr
      auto catchType = catchResult.getType();
      auto exnPtrOp = rewriter.replaceOpWithNewOp<cir::CatchParamOp>(
          firstParam, catchType, exceptionPtr,
          cir::CatchParamKindAttr::get(rewriter.getContext(),
                                       cir::CatchParamKind::GetExceptionPtr));
      if (catchBeginFn)
        exnPtrOp->setAttr(kCatchBeginFnAttr, catchBeginFn);
      if (catchEndFn)
        exnPtrOp->setAttr(kCatchEndFnAttr, catchEndFn);
      // Subsequent params: __cxa_begin_catch
      for (unsigned i = 1; i < paramOps.size(); ++i) {
        CatchParamOp p = paramOps[i];
        auto pType = p.getParam() ? p.getParam().getType() : mlir::Type{};
        auto pCatchBeginFn = p->getAttrOfType<StringAttr>(kCatchBeginFnAttr);
        auto pCatchEndFn = p->getAttrOfType<StringAttr>(kCatchEndFnAttr);
        rewriter.setInsertionPoint(p);
        auto beginOp = rewriter.replaceOpWithNewOp<cir::CatchParamOp>(
            p, pType, exceptionPtr,
            cir::CatchParamKindAttr::get(rewriter.getContext(),
                                         cir::CatchParamKind::Begin));
        if (pCatchBeginFn)
          beginOp->setAttr(kCatchBeginFnAttr, pCatchBeginFn);
        if (pCatchEndFn)
          beginOp->setAttr(kCatchEndFnAttr, pCatchEndFn);
      }
    }

    // Terminate handlers or catch handlers that unconditionally rethrow end
    // with UnreachableOp instead of YieldOp. Skip __cxa_end_catch and branch
    // emission — the handler already ends with unreachable.
    if (!yieldOp)
      return entryBlock;

    rewriter.setInsertionPoint(yieldOp);
    auto endOp = cir::CatchParamOp::create(
        rewriter, catchLoc, mlir::Type{}, nullptr,
        cir::CatchParamKindAttr::get(rewriter.getContext(),
                                     cir::CatchParamKind::End));
    if (catchEndFn)
      endOp->setAttr(kCatchEndFnAttr, catchEndFn);
    if (endCatchNothrow)
      endOp->setAttr("cir.end_catch_nothrow",
                      mlir::UnitAttr::get(rewriter.getContext()));
    else if (endCatchNormalCleanup)
      endOp->setAttr("cir.end_catch_normal_cleanup",
                     mlir::UnitAttr::get(rewriter.getContext()));
    else if (hasEnclosingEH)
      endOp->setAttr("cir.end_catch_enclosing_eh",
                      mlir::UnitAttr::get(rewriter.getContext()));
    else if (isNoexceptFn)
      endOp->setAttr("cir.end_catch_eh_cleanup",
                      mlir::UnitAttr::get(rewriter.getContext()));

    // Insert __cxa_end_catch before exit ops that leave the catch handler.
    // These exit paths must call __cxa_end_catch to balance __cxa_begin_catch
    // and destroy the exception object. BreakOp/ContinueOp inside switch/loop
    // within the catch are excluded — they exit the switch/loop, not the catch
    // handler.
    for (mlir::Operation *exitOp : exitOps) {
      // Skip if CIRGen already emitted a CatchParamOp::End before this exit
      // op (e.g., from CallEndCatch cleanup during early exit). This avoids
      // duplicate __cxa_end_catch calls.
      bool alreadyHasEnd = false;
      for (auto *op = exitOp->getPrevNode(); op; op = op->getPrevNode()) {
        if (auto cp = mlir::dyn_cast<cir::CatchParamOp>(op);
            cp && cp.isEnd()) {
          alreadyHasEnd = true;
          break;
        }
      }
      if (alreadyHasEnd)
        continue;

      rewriter.setInsertionPoint(exitOp);
      auto exitEndOp = cir::CatchParamOp::create(
          rewriter, catchLoc, mlir::Type{}, nullptr,
          cir::CatchParamKindAttr::get(rewriter.getContext(),
                                       cir::CatchParamKind::End));
      if (catchEndFn)
        exitEndOp->setAttr(kCatchEndFnAttr, catchEndFn);
      if (endCatchNothrow)
        exitEndOp->setAttr("cir.end_catch_nothrow",
                           mlir::UnitAttr::get(rewriter.getContext()));
      else if (endCatchNormalCleanup)
        exitEndOp->setAttr("cir.end_catch_normal_cleanup",
                           mlir::UnitAttr::get(rewriter.getContext()));
      else if (hasEnclosingEH)
        exitEndOp->setAttr("cir.end_catch_enclosing_eh",
                           mlir::UnitAttr::get(rewriter.getContext()));
      else if (isNoexceptFn)
        exitEndOp->setAttr("cir.end_catch_eh_cleanup",
                           mlir::UnitAttr::get(rewriter.getContext()));
    }

    rewriter.setInsertionPointToEnd(yieldOp->getBlock());
    rewriter.replaceOpWithNewOp<cir::BrOp>(yieldOp, afterTry);
    return entryBlock;
  }

  void buildUnwindCase(mlir::PatternRewriter &rewriter, mlir::Region &r,
                       mlir::Block *unwindBlock, mlir::Block *afterTry) const {
    // The unwind region is usually one block ending in `cir.resume`, but a
    // cleanup that cannot fall through (a `noreturn` destructor, say) makes
    // CIRGen end the entry block in `cir.unreachable` and strand the
    // `cir.resume` in a block with no predecessors. Inline whatever shape we
    // get, the same way buildAllCase does.
    llvm::SmallVector<mlir::Block *> regionBlocks;
    for (mlir::Block &b : r)
      regionBlocks.push_back(&b);
    mlir::Block *unwindStartBB = regionBlocks.front();

    rewriter.inlineRegionBefore(r, afterTry);
    rewriter.mergeBlocks(unwindStartBB, unwindBlock);
    // The entry block's ops live in `unwindBlock` now, and `unwindStartBB` is
    // gone.
    regionBlocks[0] = unwindBlock;

    // Drop the stranded blocks before rewriting: a `cir.resume_flat` there
    // would use `unwindBlock`'s arguments from code nothing can branch to.
    llvm::SmallVector<mlir::Block *> liveBlocks{unwindBlock};
    for (mlir::Block *b : llvm::drop_begin(regionBlocks)) {
      if (b->hasNoPredecessors())
        rewriter.eraseBlock(b);
      else
        liveBlocks.push_back(b);
    }

    // Whatever resumes are left are reachable, so each needs the flat form.
    // There may be none: an entry block ending in `cir.unreachable` never
    // resumes.
    for (mlir::Block *b : liveBlocks) {
      auto resume = dyn_cast<cir::ResumeOp>(b->getTerminator());
      if (!resume)
        continue;
      rewriter.setInsertionPointToEnd(b);
      rewriter.replaceOpWithNewOp<cir::ResumeFlatOp>(
          resume, unwindBlock->getArgument(0), unwindBlock->getArgument(1));
    }
  }

  void buildAllCase(mlir::PatternRewriter &rewriter, mlir::Region &r,
                    mlir::Block *afterTry, mlir::Block *catchAllBlock,
                    mlir::Value exceptionPtr, mlir::Value selector,
                    bool hasEnclosingEH, bool isNoexceptFn,
                    SmallVectorImpl<std::pair<cir::CallOp, bool>>
                        &handlerCallsToRewrite) const {
    // Erase dead blocks in catch-all handler region.
    for (auto it = std::next(r.begin()), e = r.end(); it != e;) {
      mlir::Block &block = *it++;
      if (block.hasNoPredecessors() && !blockHoldsLabel(block)) {
        eraseDeadBlock(rewriter, block);
      }
    }

    YieldOp yieldOp;
    llvm::SmallVector<CatchParamOp, 2> paramOps;
    llvm::SmallVector<mlir::Operation *, 2> exitOps;
    for (auto &block : r)
      for (auto &op : block) {
        if (auto y = mlir::dyn_cast<YieldOp>(op)) {
          assert(!yieldOp && "expect to only find one");
          yieldOp = y;
        }
      }
    walkRegionSkipping<cir::TryOp>(r, [&](mlir::Operation *op) {
      if (mlir::isa<cir::ReturnOp, cir::InlineAsmGotoOp>(op))
        exitOps.push_back(op);
      else if (auto gotoOp = mlir::dyn_cast<cir::GotoOp>(op)) {
        // A goto whose target label lives inside this catch handler stays
        // within the handler; only a goto leaving the handler needs
        // __cxa_end_catch inserted before it.
        if (!gotoTargetsLabelInRegion(gotoOp, r))
          exitOps.push_back(op);
      }
      return mlir::WalkResult::advance();
    });
    walkRegionSkipping<cir::TryOp, cir::SwitchOp, cir::LoopOpInterface>(
        r, [&](mlir::Operation *op) {
          if (mlir::isa<cir::BreakOp, cir::ContinueOp>(op))
            exitOps.push_back(op);
          return mlir::WalkResult::advance();
        });
    for (auto &block : r)
      for (auto &op : block)
        if (auto p = mlir::dyn_cast<CatchParamOp>(op))
          // Skip CatchParamOps from flattened inner TryOps.
          if (!p.getKind())
            paramOps.push_back(p);

    // Check if the catch handler's __cxa_end_catch is nothrow.
    bool endCatchNothrow = llvm::any_of(paramOps, [](CatchParamOp p) {
      return p->hasAttr("cir.end_catch_nothrow");
    });
    bool endCatchNormalCleanup = llvm::any_of(paramOps, [](CatchParamOp p) {
      return p->hasAttr("cir.end_catch_normal_cleanup");
    });
    bool rawHandler = llvm::any_of(paramOps, [](CatchParamOp p) {
      return p->hasAttr(kCatchRawHandlerAttr);
    });

    // When the catch-all comes from an outer try scope (not the try's own
    // handler), the region has no catch_param — it's just a forwarding stub.
    // Resume unwinding so the outer try's landing pad can handle it.
    if (paramOps.empty()) {
      assert(yieldOp && "expected yield in forwarding catch-all region");
      assert(selector && "forwarding catch-all needs selector for resume_flat");
      rewriter.inlineRegionBefore(r, afterTry);
      rewriter.mergeBlocks(&*std::prev(afterTry->getIterator()), catchAllBlock);
      rewriter.setInsertionPointToEnd(catchAllBlock);
      rewriter.replaceOpWithNewOp<cir::ResumeFlatOp>(yieldOp, exceptionPtr,
                                                     selector);
      return;
    }

    CatchParamOp firstParam = paramOps[0];
    mlir::Block *catchAllStartBB = &r.front();
    rewriter.inlineRegionBefore(r, afterTry);
    rewriter.mergeBlocks(catchAllStartBB, catchAllBlock);

    // Rewrite `cir.catch_param` to be scope aware and instead generate:
    // ```
    //   cir.catch_param begin %exception_ptr
    //   ...
    //   cir.catch_param end
    //   cir.br ...
    mlir::Value catchResult = firstParam.getParam();
    assert(catchResult && "expected to be available");
    rewriter.setInsertionPointAfterValue(catchResult);
    mlir::Location catchLoc = firstParam.getLoc();
    auto catchBeginFn = firstParam->getAttrOfType<StringAttr>(kCatchBeginFnAttr);
    auto catchEndFn = firstParam->getAttrOfType<StringAttr>(kCatchEndFnAttr);

    collectExceptionCallsInBlocks(catchAllBlock, afterTry,
                                  handlerCallsToRewrite, endCatchNothrow,
                                  rawHandler);

    if (rawHandler) {
      assert(paramOps.size() == 1 && "raw catch handler should use one param");
      mlir::Value rawException = exceptionPtr;
      if (rawException.getType() != catchResult.getType()) {
        rewriter.setInsertionPoint(firstParam);
        rawException = cir::CastOp::create(rewriter, catchLoc,
                                           catchResult.getType(),
                                           cir::CastKind::bitcast, rawException);
      }
      rewriter.replaceOp(firstParam, rawException);
      if (!yieldOp)
        return;

      rewriter.setInsertionPointToEnd(yieldOp->getBlock());
      rewriter.replaceOpWithNewOp<cir::BrOp>(yieldOp, afterTry);
      return;
    }

    // When there are two CatchParamOps (catch-by-value with non-trivial copy
    // ctor), the first is __cxa_get_exception_ptr and the second is
    // __cxa_begin_catch. When there's only one, it's __cxa_begin_catch.
    if (paramOps.size() == 1) {
      auto catchType = catchResult.getType();
      auto beginOp = rewriter.replaceOpWithNewOp<cir::CatchParamOp>(
          firstParam, catchType, exceptionPtr,
          cir::CatchParamKindAttr::get(rewriter.getContext(),
                                       cir::CatchParamKind::Begin));
      if (catchBeginFn)
        beginOp->setAttr(kCatchBeginFnAttr, catchBeginFn);
      if (catchEndFn)
        beginOp->setAttr(kCatchEndFnAttr, catchEndFn);
    } else {
      auto catchType = catchResult.getType();
      auto exnPtrOp = rewriter.replaceOpWithNewOp<cir::CatchParamOp>(
          firstParam, catchType, exceptionPtr,
          cir::CatchParamKindAttr::get(rewriter.getContext(),
                                       cir::CatchParamKind::GetExceptionPtr));
      if (catchBeginFn)
        exnPtrOp->setAttr(kCatchBeginFnAttr, catchBeginFn);
      if (catchEndFn)
        exnPtrOp->setAttr(kCatchEndFnAttr, catchEndFn);
      for (unsigned i = 1; i < paramOps.size(); ++i) {
        CatchParamOp p = paramOps[i];
        auto pType = p.getParam() ? p.getParam().getType() : mlir::Type{};
        auto pCatchBeginFn = p->getAttrOfType<StringAttr>(kCatchBeginFnAttr);
        auto pCatchEndFn = p->getAttrOfType<StringAttr>(kCatchEndFnAttr);
        rewriter.setInsertionPoint(p);
        auto beginOp = rewriter.replaceOpWithNewOp<cir::CatchParamOp>(
            p, pType, exceptionPtr,
            cir::CatchParamKindAttr::get(rewriter.getContext(),
                                         cir::CatchParamKind::Begin));
        if (pCatchBeginFn)
          beginOp->setAttr(kCatchBeginFnAttr, pCatchBeginFn);
        if (pCatchEndFn)
          beginOp->setAttr(kCatchEndFnAttr, pCatchEndFn);
      }
    }

    // Terminate handlers (noexcept functions) end with UnreachableOp instead
    // of YieldOp because std::terminate() never returns. Skip __cxa_end_catch
    // and branch emission — the handler already ends with unreachable.
    if (!yieldOp)
      return;

    rewriter.setInsertionPoint(yieldOp);
    auto endOp = cir::CatchParamOp::create(
        rewriter, catchLoc, mlir::Type{}, nullptr,
        cir::CatchParamKindAttr::get(rewriter.getContext(),
                                     cir::CatchParamKind::End));
    if (catchEndFn)
      endOp->setAttr(kCatchEndFnAttr, catchEndFn);
    if (endCatchNothrow)
      endOp->setAttr("cir.end_catch_nothrow",
                      mlir::UnitAttr::get(rewriter.getContext()));
    else if (endCatchNormalCleanup)
      endOp->setAttr("cir.end_catch_normal_cleanup",
                     mlir::UnitAttr::get(rewriter.getContext()));
    else if (hasEnclosingEH)
      endOp->setAttr("cir.end_catch_enclosing_eh",
                     mlir::UnitAttr::get(rewriter.getContext()));
    else if (isNoexceptFn)
      endOp->setAttr("cir.end_catch_eh_cleanup",
                     mlir::UnitAttr::get(rewriter.getContext()));

    // Insert __cxa_end_catch before exit ops that leave the catch handler.
    for (mlir::Operation *exitOp : exitOps) {
      // Skip if CIRGen already emitted a CatchParamOp::End before this exit
      // op (e.g., from CallEndCatch cleanup during early exit).
      bool alreadyHasEnd = false;
      for (auto *op = exitOp->getPrevNode(); op; op = op->getPrevNode()) {
        if (auto cp = mlir::dyn_cast<cir::CatchParamOp>(op);
            cp && cp.isEnd()) {
          alreadyHasEnd = true;
          break;
        }
      }
      if (alreadyHasEnd)
        continue;

      rewriter.setInsertionPoint(exitOp);
      auto exitEndOp = cir::CatchParamOp::create(
          rewriter, catchLoc, mlir::Type{}, nullptr,
          cir::CatchParamKindAttr::get(rewriter.getContext(),
                                       cir::CatchParamKind::End));
      if (catchEndFn)
        exitEndOp->setAttr(kCatchEndFnAttr, catchEndFn);
      if (endCatchNothrow)
        exitEndOp->setAttr("cir.end_catch_nothrow",
                           mlir::UnitAttr::get(rewriter.getContext()));
      else if (endCatchNormalCleanup)
        exitEndOp->setAttr("cir.end_catch_normal_cleanup",
                           mlir::UnitAttr::get(rewriter.getContext()));
      else if (hasEnclosingEH)
        exitEndOp->setAttr("cir.end_catch_enclosing_eh",
                           mlir::UnitAttr::get(rewriter.getContext()));
      else if (isNoexceptFn)
        exitEndOp->setAttr("cir.end_catch_eh_cleanup",
                           mlir::UnitAttr::get(rewriter.getContext()));
    }

    rewriter.setInsertionPointToEnd(yieldOp->getBlock());
    rewriter.replaceOpWithNewOp<cir::BrOp>(yieldOp, afterTry);
  }

  void buildDeadCatchHandler(mlir::PatternRewriter &rewriter, mlir::Region &r,
                             mlir::Block *afterTry, bool hasEnclosingEH,
                             bool isNoexceptFn) const {
    for (auto it = std::next(r.begin()), e = r.end(); it != e;) {
      mlir::Block &block = *it++;
      if (block.hasNoPredecessors() && !blockHoldsLabel(block)) {
        eraseDeadBlock(rewriter, block);
      }
    }

    YieldOp yieldOp;
    llvm::SmallVector<CatchParamOp, 2> paramOps;
    llvm::SmallVector<mlir::Operation *, 2> exitOps;
    for (auto &block : r)
      for (auto &op : block)
        if (auto y = mlir::dyn_cast<YieldOp>(op)) {
          assert(!yieldOp && "expect to only find one");
          yieldOp = y;
        }

    walkRegionSkipping<cir::TryOp>(r, [&](mlir::Operation *op) {
      if (mlir::isa<cir::ReturnOp, cir::GotoOp, cir::InlineAsmGotoOp>(op))
        exitOps.push_back(op);
      return mlir::WalkResult::advance();
    });
    walkRegionSkipping<cir::TryOp, cir::SwitchOp, cir::LoopOpInterface>(
        r, [&](mlir::Operation *op) {
          if (mlir::isa<cir::BreakOp, cir::ContinueOp>(op))
            exitOps.push_back(op);
          return mlir::WalkResult::advance();
        });
    for (auto &block : r)
      for (auto &op : block)
        if (auto p = mlir::dyn_cast<CatchParamOp>(op))
          if (!p.getKind())
            paramOps.push_back(p);

    if (paramOps.empty())
      return;

    bool endCatchNothrow = llvm::any_of(paramOps, [](CatchParamOp p) {
      return p->hasAttr("cir.end_catch_nothrow");
    });
    bool endCatchNormalCleanup = llvm::any_of(paramOps, [](CatchParamOp p) {
      return p->hasAttr("cir.end_catch_normal_cleanup");
    });
    bool rawHandler = llvm::any_of(paramOps, [](CatchParamOp p) {
      return p->hasAttr(kCatchRawHandlerAttr);
    });

    rewriter.inlineRegionBefore(r, afterTry);

    CatchParamOp firstParam = paramOps[0];
    mlir::Location catchLoc = firstParam.getLoc();
    auto catchBeginFn = firstParam->getAttrOfType<StringAttr>(kCatchBeginFnAttr);
    auto catchEndFn = firstParam->getAttrOfType<StringAttr>(kCatchEndFnAttr);
    auto voidPtrTy = cir::PointerType::get(cir::VoidType::get(getContext()));
    rewriter.setInsertionPoint(firstParam);
    auto undefException = cir::ConstantOp::create(
        rewriter, catchLoc, cir::UndefAttr::get(voidPtrTy));
    undefException->setAttr(kPreserveDeadObjCCatchBlockAttr,
                            mlir::UnitAttr::get(rewriter.getContext()));
    mlir::Value exceptionPtr = undefException.getRes();
    mlir::Value catchResult = firstParam.getParam();
    assert(catchResult && "expected to be available");

    if (rawHandler) {
      assert(paramOps.size() == 1 && "raw catch handler should use one param");
      mlir::Value rawException = exceptionPtr;
      if (rawException.getType() != catchResult.getType()) {
        rawException = cir::CastOp::create(rewriter, catchLoc,
                                           catchResult.getType(),
                                           cir::CastKind::bitcast, rawException);
      }
      rewriter.replaceOp(firstParam, rawException);
      if (!yieldOp)
        return;
    } else if (paramOps.size() == 1) {
      auto beginOp = rewriter.replaceOpWithNewOp<cir::CatchParamOp>(
          firstParam, catchResult.getType(), exceptionPtr,
          cir::CatchParamKindAttr::get(rewriter.getContext(),
                                       cir::CatchParamKind::Begin));
      if (catchBeginFn)
        beginOp->setAttr(kCatchBeginFnAttr, catchBeginFn);
      if (catchEndFn)
        beginOp->setAttr(kCatchEndFnAttr, catchEndFn);
    } else {
      auto exnPtrOp = rewriter.replaceOpWithNewOp<cir::CatchParamOp>(
          firstParam, catchResult.getType(), exceptionPtr,
          cir::CatchParamKindAttr::get(rewriter.getContext(),
                                       cir::CatchParamKind::GetExceptionPtr));
      if (catchBeginFn)
        exnPtrOp->setAttr(kCatchBeginFnAttr, catchBeginFn);
      if (catchEndFn)
        exnPtrOp->setAttr(kCatchEndFnAttr, catchEndFn);
      for (unsigned i = 1; i < paramOps.size(); ++i) {
        CatchParamOp p = paramOps[i];
        auto pType = p.getParam() ? p.getParam().getType() : mlir::Type{};
        auto pCatchBeginFn = p->getAttrOfType<StringAttr>(kCatchBeginFnAttr);
        auto pCatchEndFn = p->getAttrOfType<StringAttr>(kCatchEndFnAttr);
        rewriter.setInsertionPoint(p);
        auto beginOp = rewriter.replaceOpWithNewOp<cir::CatchParamOp>(
            p, pType, exceptionPtr,
            cir::CatchParamKindAttr::get(rewriter.getContext(),
                                         cir::CatchParamKind::Begin));
        if (pCatchBeginFn)
          beginOp->setAttr(kCatchBeginFnAttr, pCatchBeginFn);
        if (pCatchEndFn)
          beginOp->setAttr(kCatchEndFnAttr, pCatchEndFn);
      }
    }

    if (!yieldOp)
      return;

    rewriter.setInsertionPoint(yieldOp);
    auto endOp = cir::CatchParamOp::create(
        rewriter, catchLoc, mlir::Type{}, nullptr,
        cir::CatchParamKindAttr::get(rewriter.getContext(),
                                     cir::CatchParamKind::End));
    if (catchEndFn)
      endOp->setAttr(kCatchEndFnAttr, catchEndFn);
    if (endCatchNothrow)
      endOp->setAttr("cir.end_catch_nothrow",
                     mlir::UnitAttr::get(rewriter.getContext()));
    else if (endCatchNormalCleanup)
      endOp->setAttr("cir.end_catch_normal_cleanup",
                     mlir::UnitAttr::get(rewriter.getContext()));
    else if (hasEnclosingEH)
      endOp->setAttr("cir.end_catch_enclosing_eh",
                     mlir::UnitAttr::get(rewriter.getContext()));
    else if (isNoexceptFn)
      endOp->setAttr("cir.end_catch_eh_cleanup",
                     mlir::UnitAttr::get(rewriter.getContext()));

    for (mlir::Operation *exitOp : exitOps) {
      bool alreadyHasEnd = false;
      for (auto *op = exitOp->getPrevNode(); op; op = op->getPrevNode()) {
        if (auto cp = mlir::dyn_cast<cir::CatchParamOp>(op); cp && cp.isEnd()) {
          alreadyHasEnd = true;
          break;
        }
      }
      if (alreadyHasEnd)
        continue;

      rewriter.setInsertionPoint(exitOp);
      auto exitEndOp = cir::CatchParamOp::create(
          rewriter, catchLoc, mlir::Type{}, nullptr,
          cir::CatchParamKindAttr::get(rewriter.getContext(),
                                       cir::CatchParamKind::End));
      if (catchEndFn)
        exitEndOp->setAttr(kCatchEndFnAttr, catchEndFn);
      if (endCatchNothrow)
        exitEndOp->setAttr("cir.end_catch_nothrow",
                           mlir::UnitAttr::get(rewriter.getContext()));
      else if (endCatchNormalCleanup)
        exitEndOp->setAttr("cir.end_catch_normal_cleanup",
                           mlir::UnitAttr::get(rewriter.getContext()));
      else if (hasEnclosingEH)
        exitEndOp->setAttr("cir.end_catch_enclosing_eh",
                           mlir::UnitAttr::get(rewriter.getContext()));
      else if (isNoexceptFn)
        exitEndOp->setAttr("cir.end_catch_eh_cleanup",
                           mlir::UnitAttr::get(rewriter.getContext()));
    }

    rewriter.setInsertionPointToEnd(yieldOp->getBlock());
    rewriter.replaceOpWithNewOp<cir::BrOp>(yieldOp, afterTry);
  }

  void buildDeadCatchers(cir::TryOp tryOp, mlir::PatternRewriter &rewriter,
                         mlir::Block *afterTry, bool hasEnclosingEH,
                         bool isNoexceptFn) const {
    mlir::ArrayAttr catchAttrList = tryOp.getCatchTypesAttr();
    llvm::MutableArrayRef<mlir::Region> catchRegions = tryOp.getCatchRegions();
    for (const auto [idx, catchAttr] : llvm::enumerate(catchAttrList)) {
      if (mlir::isa<cir::UnwindAttr>(catchAttr))
        continue;
      buildDeadCatchHandler(rewriter, catchRegions[idx], afterTry,
                            hasEnclosingEH, isNoexceptFn);
    }
  }

  mlir::ArrayAttr collectTypeSymbols(cir::TryOp tryOp) const {
    mlir::ArrayAttr caseAttrList = tryOp.getCatchTypesAttr();
    if (!caseAttrList)
      return {};
    llvm::SmallVector<mlir::Attribute, 4> symbolList;

    for (mlir::Attribute caseAttr : caseAttrList) {
      auto typeIdGlobal = dyn_cast<cir::GlobalViewAttr>(caseAttr);
      if (!typeIdGlobal)
        continue;
      symbolList.push_back(typeIdGlobal.getSymbol());
    }

    // Return an empty attribute instead of an empty list...
    if (symbolList.empty())
      return {};
    return mlir::ArrayAttr::get(caseAttrList.getContext(), symbolList);
  }

  bool hasCatchAll(cir::TryOp tryOp) const {
    mlir::ArrayAttr handlerTypes = tryOp.getCatchTypesAttr();
    if (!handlerTypes)
      return false;
    for (mlir::Attribute attr : handlerTypes) {
      if (mlir::isa<cir::CatchAllAttr>(attr))
        return true;
    }
    return false;
  }

  /// Check if this TryOp has dynamic exception specification filter types.
  bool hasFilterTypes(cir::TryOp tryOp) const {
    return tryOp->hasAttr("cir.filter_types");
  }

  /// Collect filter type symbols from the cir.filter_types attribute.
  mlir::ArrayAttr collectFilterTypeSymbols(cir::TryOp tryOp) const {
    auto filterTypesAttr =
        tryOp->getAttrOfType<mlir::ArrayAttr>("cir.filter_types");
    if (!filterTypesAttr)
      return {};
    if (filterTypesAttr.empty())
      return filterTypesAttr;
    llvm::SmallVector<mlir::Attribute, 4> symbolList;
    for (mlir::Attribute attr : filterTypesAttr) {
      auto typeIdGlobal = dyn_cast<cir::GlobalViewAttr>(attr);
      if (!typeIdGlobal)
        continue;
      symbolList.push_back(typeIdGlobal.getSymbol());
    }
    if (symbolList.empty())
      return {};
    return mlir::ArrayAttr::get(filterTypesAttr.getContext(), symbolList);
  }

  static bool cleanupRegionsEquivalent(cir::CallOp lhsCall,
                                       cir::CallOp rhsCall) {
    auto &lhsCleanup = lhsCall.getCleanup();
    auto &rhsCleanup = rhsCall.getCleanup();
    return mlir::OperationEquivalence::isRegionEquivalentTo(
        &lhsCleanup, &rhsCleanup, mlir::OperationEquivalence::IgnoreLocations);
  }

  static bool cleanupRegionContainsEndCatch(cir::CallOp callOp) {
    bool hasEndCatch = false;
    callOp.getCleanup().walk([&](cir::CatchParamOp p) {
      if (p.getKind() && *p.getKind() == cir::CatchParamKind::End)
        hasEndCatch = true;
    });
    return hasEndCatch;
  }

  static void inlineCleanupRegion(cir::CallOp callOp,
                                  mlir::PatternRewriter &rewriter,
                                  mlir::Block *entryBlock,
                                  mlir::Block *continueBlock,
                                  mlir::ValueRange continueArgs) {
    mlir::Region &cleanup = callOp.getCleanup();
    assert(!cleanup.empty() && "expected non-empty cleanup region");

    llvm::SmallVector<cir::YieldOp, 2> cleanupYields;
    for (mlir::Block &block : cleanup)
      if (auto yieldOp = mlir::dyn_cast<cir::YieldOp>(block.getTerminator()))
        cleanupYields.push_back(yieldOp);

    mlir::Block *cleanupEntry = &cleanup.front();
    rewriter.inlineRegionBefore(cleanup, continueBlock);

    rewriter.setInsertionPointToEnd(entryBlock);
    cir::BrOp::create(rewriter, callOp.getLoc(), cleanupEntry);

    for (cir::YieldOp yieldOp : cleanupYields) {
      rewriter.setInsertionPoint(yieldOp);
      rewriter.replaceOpWithNewOp<cir::BrOp>(yieldOp, continueArgs,
                                             continueBlock);
    }
  }

  /// Build the filter dispatch case for dynamic exception specifications.
  /// Unlike buildAllCase (which wraps with __cxa_begin_catch/__cxa_end_catch),
  /// the filter handler uses the raw exception pointer directly and adds a
  /// selector < 0 check with a resume path for allowed exceptions.
  ///
  /// Generated code pattern:
  ///   %fails = icmp slt selector, 0
  ///   br %fails, ^unexpected, ^resume
  /// ^unexpected:
  ///   call @__cxa_call_unexpected(exn) noreturn
  ///   unreachable
  /// ^resume:
  ///   resume {exn, selector}
  void buildFilterCase(mlir::PatternRewriter &rewriter, mlir::Region &r,
                       mlir::Block *afterTry, mlir::Block *catchAllBlock,
                       mlir::Value exceptionPtr,
                       mlir::Value selector,
                       bool hasFilterEntries) const {
    // The handler region should contain:
    //   CatchParamOp {cir.filter_handler} -> raw exn ptr
    //   call @__cxa_call_unexpected(exn)
    //   unreachable
    llvm::SmallVector<CatchParamOp, 1> paramOps;
    for (auto &block : r)
      for (auto &op : block)
        if (auto p = mlir::dyn_cast<CatchParamOp>(op))
          paramOps.push_back(p);

    assert(!paramOps.empty() && "expected CatchParamOp in filter handler");

    CatchParamOp firstParam = paramOps[0];
    mlir::Location loc = firstParam.getLoc();
    mlir::Block *filterStartBB = &r.front();
    rewriter.inlineRegionBefore(r, afterTry);
    rewriter.mergeBlocks(filterStartBB, catchAllBlock);

    // Replace CatchParamOp with the raw exception pointer directly.
    // No __cxa_begin_catch — filter handlers use the raw exn ptr.
    rewriter.replaceOp(firstParam, exceptionPtr);

    // Pre-C++17 throw() lowers through a zero-filter scope. Match OGCG and
    // fall straight into __cxa_call_unexpected without synthesizing the dead
    // selector check / resume path used by typed dynamic exception specs.
    if (!hasFilterEntries)
      return;

    // Create the selector < 0 check and resume path.
    // Split the handler: the unexpected handler body (call + unreachable)
    // is already inline. We need to insert a conditional branch BEFORE
    // the handler body that checks the selector.
    //
    // Create a new block for the unexpected handler body.
    mlir::Block *unexpectedBlock =
        rewriter.splitBlock(catchAllBlock, catchAllBlock->begin());

    // Create the resume block.
    mlir::Block *resumeBlock = rewriter.createBlock(afterTry);
    rewriter.setInsertionPointToEnd(resumeBlock);
    cir::ResumeFlatOp::create(rewriter, loc, exceptionPtr, selector);

    // Build the filter dispatch in the catch-all block.
    // The selector is unsigned i32 in CIR, but we need a signed comparison
    // (slt) because filter matches produce negative selector values.
    // Cast both operands to signed i32 for the comparison.
    rewriter.setInsertionPointToEnd(catchAllBlock);
    auto si32Ty = cir::IntType::get(rewriter.getContext(), 32, /*isSigned=*/true);
    auto selectorSigned = cir::CastOp::create(
        rewriter, loc, si32Ty, cir::CastKind::integral, selector);
    auto zero = cir::ConstantOp::create(
        rewriter, loc, si32Ty, cir::IntAttr::get(si32Ty, 0));
    auto failsFilter = cir::CmpOp::create(
        rewriter, loc,
        cir::BoolType::get(rewriter.getContext()), cir::CmpOpKind::lt,
        selectorSigned, zero);
    cir::BrCondOp::create(rewriter, loc, failsFilter,
                           unexpectedBlock, resumeBlock);
  }

  void buildLandingPad(cir::TryOp tryOp, mlir::PatternRewriter &rewriter,
                       mlir::Block *beforeCatch, mlir::Block *landingPadBlock,
                       mlir::Block *catchDispatcher,
                       SmallVectorImpl<cir::CallOp> &callsToRewrite,
                       unsigned callIdx, bool tryOnlyHasCatchAll,
                       mlir::Type exceptionPtrType,
                       mlir::Type typeIdType) const {
    rewriter.setInsertionPointToEnd(landingPadBlock);
    mlir::ArrayAttr symlist = collectTypeSymbols(tryOp);
    bool catchAll = hasCatchAll(tryOp);
    bool isFilter = hasFilterTypes(tryOp);

    // For filter TryOps, use filter_type_list instead of catch_all.
    // The filter clause catches exceptions NOT in the type list.
    mlir::ArrayAttr filterSymlist;
    if (isFilter) {
      filterSymlist = collectFilterTypeSymbols(tryOp);
      catchAll = false; // Don't add catch ptr null for filter
    }

    auto inflightEh = cir::EhInflightOp::create(
        rewriter, tryOp.getLoc(), exceptionPtrType, typeIdType,
        tryOp.getCleanup() ? mlir::UnitAttr::get(tryOp.getContext()) : nullptr,
        catchAll ? mlir::UnitAttr::get(tryOp.getContext()) : nullptr,
        symlist,
        filterSymlist);
    auto selector = inflightEh.getTypeId();
    auto exceptionPtr = inflightEh.getExceptionPtr();

    // Time to emit cleanup's.
    cir::CallOp callOp = callsToRewrite[callIdx];
    llvm::SmallVector<mlir::Value> dispatcherInitOps = {exceptionPtr};
    if (!tryOnlyHasCatchAll) {
      assert(catchDispatcher->getNumArguments() == 2 &&
             "expected two arguments in place");
      dispatcherInitOps.push_back(selector);
    }

    if (!callOp.getCleanup().empty()) {
      inlineCleanupRegion(callOp, rewriter, landingPadBlock, catchDispatcher,
                          dispatcherInitOps);
      return;
    }

    // Branch out to the catch clauses dispatcher.
    assert(catchDispatcher->getNumArguments() >= 1 &&
           "expected at least one argument in place");
    cir::BrOp::create(rewriter, tryOp.getLoc(), catchDispatcher,
                      dispatcherInitOps);
    return;
  }

  mlir::Block *buildLandingPads(cir::TryOp tryOp,
                                mlir::PatternRewriter &rewriter,
                                mlir::Block *beforeCatch, mlir::Block *afterTry,
                                SmallVectorImpl<cir::CallOp> &callsToRewrite,
                                SmallVectorImpl<mlir::Block *> &landingPads,
                                bool tryOnlyHasCatchAll) const {
    unsigned numCalls = callsToRewrite.size();
    llvm::SmallVector<unsigned, 4> cleanupGroupForCall(numCalls);
    llvm::SmallVector<unsigned, 4> groupRepresentatives;
    groupRepresentatives.reserve(numCalls);

    // Group calls with equivalent cleanup regions so they can share the same
    // landing pad. Calls marked with kShareSyntheticCleanupAttr are the
    // raw-CIR empty-cleanup representatives of a same-depth synthetic try
    // reuse and should reuse the first non-empty cleanup landing pad in the
    // try.
    for (unsigned callIdx = 0; callIdx < numCalls; ++callIdx) {
      std::optional<unsigned> matchedGroup;
      if (callsToRewrite[callIdx]->hasAttr(kShareSyntheticCleanupAttr)) {
        for (unsigned groupIdx = 0; groupIdx < groupRepresentatives.size();
             ++groupIdx) {
          unsigned repIdx = groupRepresentatives[groupIdx];
          if (callsToRewrite[repIdx].getCleanup().empty())
            continue;
          matchedGroup = groupIdx;
          break;
        }
        assert(matchedGroup &&
               "expected prior representative cleanup for shared synthetic try");
      }

      for (unsigned groupIdx = 0; groupIdx < groupRepresentatives.size();
           ++groupIdx) {
        if (matchedGroup)
          break;
        unsigned repIdx = groupRepresentatives[groupIdx];
        if (!cleanupRegionsEquivalent(callsToRewrite[callIdx],
                                      callsToRewrite[repIdx]))
          continue;
        matchedGroup = groupIdx;
        break;
      }

      if (!matchedGroup) {
        cleanupGroupForCall[callIdx] = groupRepresentatives.size();
        groupRepresentatives.push_back(callIdx);
      } else {
        cleanupGroupForCall[callIdx] = *matchedGroup;
      }
    }

    unsigned numGroups = groupRepresentatives.size();

    // Create the first landing pad block and a placeholder for the initial
    // catch dispatcher (which will be the common destination for every new
    // landing pad we create).
    auto *landingPadBlock =
        rewriter.splitBlock(beforeCatch, rewriter.getInsertionPoint());

    // For the dispatcher, already add the block arguments and prepare the
    // proper types the landing pad should use to jump to.
    mlir::Block *dispatcher = rewriter.createBlock(afterTry);
    auto exceptionPtrType =
        cir::PointerType::get(cir::VoidType::get(rewriter.getContext()));
    auto typeIdType = cir::IntType::get(getContext(), 32, false);
    dispatcher->addArgument(exceptionPtrType, tryOp.getLoc());
    if (!tryOnlyHasCatchAll)
      dispatcher->addArgument(typeIdType, tryOp.getLoc());

    llvm::SmallVector<mlir::Block *, 4> landingPadForGroup(numGroups);
    for (unsigned groupIdx = 0; groupIdx != numGroups; ++groupIdx) {
      unsigned callIdx = groupRepresentatives[groupIdx];
      buildLandingPad(tryOp, rewriter, beforeCatch, landingPadBlock, dispatcher,
                      callsToRewrite, callIdx, tryOnlyHasCatchAll,
                      exceptionPtrType, typeIdType);
      landingPadForGroup[groupIdx] = landingPadBlock;
      if (groupIdx < numGroups - 1)
        landingPadBlock = rewriter.createBlock(dispatcher);
    }

    for (unsigned callIdx = 0; callIdx != numCalls; ++callIdx)
      landingPads.push_back(landingPadForGroup[cleanupGroupForCall[callIdx]]);

    return dispatcher;
  }

  mlir::Block *buildCatch(cir::TryOp tryOp, mlir::PatternRewriter &rewriter,
                          mlir::Block *afterTry, mlir::Block *dispatcher,
                          SmallVectorImpl<cir::CallOp> &callsToRewrite,
                          SmallVectorImpl<std::pair<cir::CallOp, bool>>
                              &handlerCallsToRewrite,
                          mlir::Attribute catchAttr,
                          mlir::Attribute nextCatchAttr,
                          mlir::Region &catchRegion,
                          bool hasEnclosingEH,
                          bool isNoexceptFn) const {
    mlir::Location loc = tryOp.getLoc();
    mlir::Block *nextDispatcher = nullptr;
    if (auto typeIdGlobal = dyn_cast<cir::GlobalViewAttr>(catchAttr)) {
      auto *previousDispatcher = dispatcher;
      auto typeId =
          cir::EhTypeIdOp::create(rewriter, loc, typeIdGlobal.getSymbol());
      auto ehPtr = previousDispatcher->getArgument(0);
      auto ehSel = previousDispatcher->getArgument(1);

      auto match = cir::CmpOp::create(rewriter, loc,
                                      cir::BoolType::get(rewriter.getContext()),
                                      cir::CmpOpKind::eq, ehSel, typeId);

      mlir::Block *typeCatchBlock =
          buildTypeCase(rewriter, catchRegion, afterTry, ehPtr.getType(),
                        hasEnclosingEH, isNoexceptFn,
                        handlerCallsToRewrite);
      nextDispatcher = rewriter.createBlock(afterTry);
      rewriter.setInsertionPointToEnd(previousDispatcher);

      // Next dispatcher gets both exception ptr and selector info.
      // Even catch-all dispatchers need the selector for forwarding
      // catch-all handlers (from outer scopes) that do resume_flat.
      nextDispatcher->addArgument(ehPtr.getType(), loc);
      nextDispatcher->addArgument(ehSel.getType(), loc);
      llvm::SmallVector<mlir::Value> nextDispatchOps = {ehPtr, ehSel};

      cir::BrCondOp::create(rewriter, loc, match, typeCatchBlock,
                            nextDispatcher, mlir::ValueRange{ehPtr},
                            nextDispatchOps);
      rewriter.setInsertionPointToEnd(nextDispatcher);
    } else if (auto catchAll = dyn_cast<cir::CatchAllAttr>(catchAttr)) {
      auto ehPtr = dispatcher->getArgument(0);
      // When tryOnlyHasCatchAll, the dispatcher has only 1 arg (ehPtr).
      // When there are typed catches before the catch_all, the dispatcher
      // has 2 args (ehPtr + ehSel). The selector is needed for forwarding
      // catch_all handlers (from outer scopes) that do resume_flat.
      mlir::Value ehSel;
      if (dispatcher->getNumArguments() >= 2)
        ehSel = dispatcher->getArgument(1);

      // Filter TryOps use a different handler dispatch: no __cxa_begin_catch,
      // selector < 0 check for unexpected exceptions, resume path.
      if (hasFilterTypes(tryOp)) {
        assert(ehSel && "filter handler needs selector for dispatch");
        auto filterTypesAttr =
            tryOp->getAttrOfType<mlir::ArrayAttr>("cir.filter_types");
        buildFilterCase(rewriter, catchRegion, afterTry, dispatcher, ehPtr,
                        ehSel, filterTypesAttr && !filterTypesAttr.empty());
      } else {
        buildAllCase(rewriter, catchRegion, afterTry, dispatcher, ehPtr, ehSel,
                     hasEnclosingEH, isNoexceptFn, handlerCallsToRewrite);
      }
      // Do not update `nextDispatcher`, no more business in try/catch
    } else if (auto catchUnwind = dyn_cast<cir::UnwindAttr>(catchAttr)) {
      assert(dispatcher->getArguments().size() == 2 &&
             "expected two block argument");
      buildUnwindCase(rewriter, catchRegion, dispatcher, afterTry);
      // Do not update `nextDispatcher`, no more business in try/catch
    }
    return nextDispatcher;
  }

  void buildCatchers(cir::TryOp tryOp, mlir::PatternRewriter &rewriter,
                     mlir::Block *afterBody, mlir::Block *afterTry,
                     SmallVectorImpl<cir::CallOp> &callsToRewrite,
                     SmallVectorImpl<std::pair<cir::CallOp, bool>>
                         &handlerCallsToRewrite,
                     SmallVectorImpl<mlir::Block *> &landingPads,
                     SmallVectorImpl<cir::ResumeFlatOp> &resumeOpsToRedirect)
      const {
    // Replace the tryOp return with a branch that jumps out of the body.
    rewriter.setInsertionPointToEnd(afterBody);

    mlir::Block *beforeCatch = rewriter.getInsertionBlock();
    rewriter.setInsertionPointToEnd(beforeCatch);

    // Check if the terminator is a YieldOp because there could be another
    // terminator, e.g. unreachable
    if (auto tryBodyYield = dyn_cast<cir::YieldOp>(afterBody->getTerminator()))
      rewriter.replaceOpWithNewOp<cir::BrOp>(tryBodyYield, afterTry);

    mlir::ArrayAttr catches = tryOp.getCatchTypesAttr();

    bool hasEnclosingEH = tryOp->hasAttr("cir.has_enclosing_eh") ||
                          tryOp->getParentOfType<cir::TryOp>() != nullptr;

    // Check if the enclosing function is noexcept. In a noexcept function,
    // __cxa_end_catch must be emitted as invoke-to-terminate when it might
    // throw, matching OGCG's behavior where the noexcept spec pushes a
    // terminate scope on the EH stack.
    bool isNoexceptFn = false;
    if (auto fn = tryOp->getParentOfType<cir::FuncOp>()) {
      if (auto extraAttrs = fn.getExtraAttrs())
        isNoexceptFn = extraAttrs->getElements().get("nothrow") != nullptr;
    }

    auto exceptionPtrType =
        cir::PointerType::get(cir::VoidType::get(rewriter.getContext()));
    auto typeIdType = cir::IntType::get(getContext(), 32, false);

    if (!catches || catches.empty()) {
      if (callsToRewrite.empty() && resumeOpsToRedirect.empty())
        return;

      // Cleanup-only tries (for example ObjC @finally wrappers) still need
      // landing pads when inner EH flattening exposes exception calls here.
      mlir::Block *resumeDispatcher = nullptr;
      if (!callsToRewrite.empty()) {
        resumeDispatcher =
            buildLandingPads(tryOp, rewriter, beforeCatch, afterTry,
                             callsToRewrite, landingPads,
                             /*tryOnlyHasCatchAll=*/false);
      } else {
        resumeDispatcher = rewriter.createBlock(afterTry);
        resumeDispatcher->addArgument(exceptionPtrType, tryOp.getLoc());
        resumeDispatcher->addArgument(typeIdType, tryOp.getLoc());
      }

      for (cir::ResumeFlatOp resumeOp : resumeOpsToRedirect) {
        rewriter.setInsertionPoint(resumeOp);
        cir::BrOp::create(rewriter, resumeOp.getLoc(), resumeDispatcher,
                          mlir::ValueRange{resumeOp.getExceptionPtr(),
                                           resumeOp.getTypeId()});
        rewriter.eraseOp(resumeOp);
      }

      rewriter.setInsertionPointToEnd(resumeDispatcher);
      cir::ResumeFlatOp::create(rewriter, tryOp.getLoc(),
                                resumeDispatcher->getArgument(0),
                                resumeDispatcher->getArgument(1));
      return;
    }

    // Skip building catch dispatch when there's nothing to dispatch, except
    // for bare ObjC exception mode where CIRGen intentionally preserves catch
    // regions so we can materialize the same dead helper blocks as OGCG.
    if (callsToRewrite.empty() && resumeOpsToRedirect.empty()) {
      if (tryOp->hasAttr(kObjCDeadCatchesAttr))
        buildDeadCatchers(tryOp, rewriter, afterTry, hasEnclosingEH,
                          isNoexceptFn);
      return;
    }

    // Filter TryOps use handler_types = [#cir.all] but need the selector
    // for the filter dispatch (icmp slt selector, 0), so force false.
    bool tryOnlyHasCatchAll =
        tryOp.isCatchAllOnly() && !hasFilterTypes(tryOp);

    mlir::Block *nextDispatcher;
    if (!callsToRewrite.empty()) {
      // Normal path: build landing pads and dispatcher for direct calls.
      nextDispatcher =
          buildLandingPads(tryOp, rewriter, beforeCatch, afterTry,
                           callsToRewrite, landingPads, tryOnlyHasCatchAll);
    } else {
      // No direct exception calls, but resume_flat ops from flattened inner
      // tries need to be redirected to this outer try's catch dispatcher.
      // Create just the dispatcher block (no landing pads needed).
      nextDispatcher = rewriter.createBlock(afterTry);
      nextDispatcher->addArgument(exceptionPtrType, tryOp.getLoc());
      if (!tryOnlyHasCatchAll)
        nextDispatcher->addArgument(typeIdType, tryOp.getLoc());
    }

    // Redirect resume_flat ops from flattened inner tries to the dispatcher.
    // These represent exceptions that didn't match the inner try's catch
    // handlers and should be dispatched by this outer try instead of
    // continuing the unwind (which would hit std::terminate).
    for (cir::ResumeFlatOp resumeOp : resumeOpsToRedirect) {
      rewriter.setInsertionPoint(resumeOp);
      llvm::SmallVector<mlir::Value> dispatcherArgs = {
          resumeOp.getExceptionPtr()};
      if (!tryOnlyHasCatchAll)
        dispatcherArgs.push_back(resumeOp.getTypeId());
      cir::BrOp::create(rewriter, resumeOp.getLoc(), nextDispatcher,
                         dispatcherArgs);
      rewriter.eraseOp(resumeOp);
    }

    // Fill in dispatcher to all catch clauses.
    rewriter.setInsertionPointToEnd(nextDispatcher);
    llvm::MutableArrayRef<mlir::Region> catchRegions = tryOp.getCatchRegions();
    unsigned catchIdx = 0;

    // Build control-flow for all catch clauses.
    mlir::ArrayAttr catchAttrList = tryOp.getCatchTypesAttr();
    for (mlir::Attribute catchAttr : catchAttrList) {
      mlir::Attribute nextCatchAttr;
      if (catchIdx + 1 < catchAttrList.size())
        nextCatchAttr = catchAttrList[catchIdx + 1];
      nextDispatcher =
          buildCatch(tryOp, rewriter, afterTry, nextDispatcher, callsToRewrite,
                     handlerCallsToRewrite, catchAttr, nextCatchAttr,
                     catchRegions[catchIdx], hasEnclosingEH, isNoexceptFn);
      catchIdx++;
    }

    assert(!nextDispatcher && "last dispatch expected to be nullptr");
  }

  mlir::Block *buildTryBody(cir::TryOp tryOp,
                            mlir::PatternRewriter &rewriter) const {
    auto loc = tryOp.getLoc();
    // Split the current block before the TryOp to create the inlining
    // point.
    auto *beforeTryScopeBlock = tryOp->getBlock();
    auto tryIt = mlir::Block::iterator(tryOp.getOperation());
    mlir::Block *afterTry = rewriter.splitBlock(beforeTryScopeBlock, tryIt);

    // Inline body region.
    auto *beforeBody = &tryOp.getTryRegion().front();
    rewriter.inlineRegionBefore(tryOp.getTryRegion(), afterTry);

    // Branch into the body of the region.
    rewriter.setInsertionPointToEnd(beforeTryScopeBlock);
    cir::BrOp::create(rewriter, loc, mlir::ValueRange(), beforeBody);
    return afterTry;
  }

  // Flatten ScopeOps inside a TryOp's try region that contain
  // ResumeFlatOps from flattened inner tries. This makes the
  // ResumeFlatOps directly visible in the try region so they can
  // be redirected to the outer try's catch dispatcher.
  void flattenScopesInTryRegion(cir::TryOp tryOp,
                                mlir::PatternRewriter &rewriter) const {
    // Collect ScopeOps that are direct children of the try region
    // and contain ResumeFlatOps. Collect innermost first (PostOrder walk
    // visits children before parents).
    llvm::SmallVector<cir::ScopeOp, 4> scopesToFlatten;
    tryOp.getTryRegion().walk([&](cir::ScopeOp scopeOp) {
      bool hasResumeFlat = false;
      scopeOp.getScopeRegion().walk([&](cir::ResumeFlatOp) {
        hasResumeFlat = true;
      });
      if (hasResumeFlat)
        scopesToFlatten.push_back(scopeOp);
    });

    for (cir::ScopeOp scopeOp : scopesToFlatten) {
      if (scopeOp.isEmpty()) {
        rewriter.eraseOp(scopeOp);
        continue;
      }
      mlir::OpBuilder::InsertionGuard scopeGuard(rewriter);
      mlir::Location loc = scopeOp.getLoc();
      bool hasLiveResults =
          scopeOp.getNumResults() > 0 && !scopeOp->use_empty();
      mlir::Block *currentBlock = scopeOp->getBlock();
      mlir::Block *continueBlock = rewriter.splitBlock(
          currentBlock, mlir::Block::iterator(scopeOp));
      if (hasLiveResults)
        continueBlock->addArguments(scopeOp.getResultTypes(), loc);

      // Inline body region.
      mlir::Block *beforeBody = &scopeOp.getScopeRegion().front();
      mlir::Block *afterBody = &scopeOp.getScopeRegion().back();
      rewriter.inlineRegionBefore(scopeOp.getScopeRegion(), continueBlock);

      // Branch into the body.
      rewriter.setInsertionPointToEnd(currentBlock);
      cir::BrOp::create(rewriter, loc, mlir::ValueRange(), beforeBody);

      // Replace yield with branch to continue block.
      rewriter.setInsertionPointToEnd(afterBody);
      if (auto yieldOp =
              dyn_cast<cir::YieldOp>(afterBody->getTerminator()))
        rewriter.replaceOpWithNewOp<cir::BrOp>(
            yieldOp, hasLiveResults ? yieldOp.getArgs() : mlir::ValueRange(),
            continueBlock);

      // Replace the op with values from the body region.
      if (scopeOp.getNumResults() > 0 && scopeOp->use_empty())
        rewriter.eraseOp(scopeOp);
      else
        rewriter.replaceOp(scopeOp, continueBlock->getArguments());
    }
  }

  mlir::LogicalResult
  matchAndRewrite(cir::TryOp tryOp,
                  mlir::PatternRewriter &rewriter) const override {
    mlir::OpBuilder::InsertionGuard guard(rewriter);
    mlir::Block *tryBlock = tryOp->getBlock();
    if (!tryBlock || !tryBlock->getParent())
      return mlir::failure();

    // Preserve enclosing-EH context for nested TryOps before this rewrite
    // inlines/erases the parent and detaches children from lexical ancestry.
    // Nested TryOps flattened later still need this bit to mark end_catch
    // for invoke lowering.
    bool hasNestedTry = false;
    tryOp->walk([&](cir::TryOp nestedTry) {
      if (nestedTry != tryOp) {
        hasNestedTry = true;
        nestedTry->setAttr("cir.has_enclosing_eh",
                           mlir::UnitAttr::get(rewriter.getContext()));
      }
    });

    // Flatten innermost try regions first so outer rewrites can see the
    // redirected resume_flat edges from nested tries.
    if (hasNestedTry)
      return mlir::failure();

    auto *afterBody = &tryOp.getTryRegion().back();

    // Empty scope: just remove it.
    if (tryOp.getTryRegion().empty()) {
      rewriter.eraseOp(tryOp);
      return mlir::success();
    }

    // Convert ThrowOps inside the try body to CallOp exception so they
    // participate in the try_call rewriting and get proper landing pads.
    // Collect first, then modify, to avoid iterator invalidation.
    llvm::SmallVector<cir::ThrowOp, 2> throwsToConvert;
    tryOp.getTryRegion().walk([&](cir::ThrowOp throwOp) {
      if (throwOp->getParentOfType<cir::TryOp>() != tryOp)
        return;
      if (throwOp.rethrows())
        return;
      throwsToConvert.push_back(throwOp);
    });

    for (cir::ThrowOp throwOp : throwsToConvert) {
      mlir::OpBuilder::InsertionGuard throwGuard(rewriter);
      mlir::Location loc = throwOp.getLoc();
      auto *ctx = rewriter.getContext();

      auto voidTy = cir::VoidType::get(ctx);
      auto excPtrTy = mlir::cast<cir::PointerType>(throwOp.getExceptionPtr().getType());
      auto ptrToVoid = cir::PointerType::get(voidTy, excPtrTy.getAddrSpace());

      // Create @__cxa_throw declaration if needed.
      auto modOp = throwOp->getParentOfType<mlir::ModuleOp>();
      auto typeInfoGlobal =
          symTable.lookup<cir::GlobalOp>(throwOp.getTypeInfoAttr().getValue());
      auto typeInfoVoidTy =
          cir::PointerType::get(voidTy, typeInfoGlobal.getAddrSpaceAttr());
      auto throwFnTy =
          cir::FuncType::get({ptrToVoid, typeInfoVoidTy, ptrToVoid}, voidTy);
      if (auto throwFn = symTable.lookup<cir::FuncOp>("__cxa_throw")) {
        if (throwFn.getFunctionType() != throwFnTy)
          throwFn.setFunctionType(throwFnTy);
      } else {
        mlir::OpBuilder::InsertionGuard modGuard(rewriter);
        rewriter.setInsertionPointToStart(modOp.getBody());
        auto newThrowFn =
            cir::FuncOp::create(rewriter, loc, "__cxa_throw", throwFnTy);
        newThrowFn.setPrivate();
        symTable.insert(newThrowFn);
      }

      rewriter.setInsertionPoint(throwOp);

      // Cast exception_ptr to void*.
      mlir::Value excPtr = cir::CastOp::create(rewriter, loc, ptrToVoid,
                                                cir::CastKind::bitcast,
                                                throwOp.getExceptionPtr());
      // Get type_info address — look up the global's actual type from the
      // module.  Primitive RTTI globals like @_ZTIi have type ptr<!u8i>,
      // while class RTTI globals like @_ZTI5Error have a record type.
      auto typeInfoGlobalTy = typeInfoGlobal.getSymType();
      auto ptrToTypeInfo =
          cir::PointerType::get(typeInfoGlobalTy, typeInfoGlobal.getAddrSpaceAttr());
      mlir::Value typeInfoAddr =
          cir::GetGlobalOp::create(rewriter, loc, ptrToTypeInfo,
                                   throwOp.getTypeInfoAttr())
              .getAddr();
      mlir::Value typeInfo = cir::CastOp::create(
          rewriter, loc, typeInfoVoidTy, cir::CastKind::bitcast, typeInfoAddr);

      // Dtor arg — use actual dtor symbol if present, null otherwise.
      mlir::Value dtor;
      if (auto dtorAttr = throwOp.getDtorAttr()) {
        auto dtorFunc = symTable.lookup<cir::FuncOp>(dtorAttr.getValue());
        auto dtorFnTy = dtorFunc.getFunctionType();
        auto ptrToDtor = cir::PointerType::get(dtorFnTy, excPtrTy.getAddrSpace());
        mlir::Value dtorAddr =
            cir::GetGlobalOp::create(rewriter, loc, ptrToDtor,
                                     dtorAttr.getValue())
                .getAddr();
        dtor = cir::CastOp::create(rewriter, loc, ptrToVoid,
                                   cir::CastKind::bitcast, dtorAddr);
      } else {
        dtor = cir::ConstantOp::create(
            rewriter, loc, ptrToVoid,
            cir::ConstPtrAttr::get(ptrToVoid, rewriter.getI64IntegerAttr(0)));
      }

      // Create cir.call exception @__cxa_throw(...) with empty cleanup.
      auto callOp = cir::CallOp::create(
          rewriter, loc,
          mlir::SymbolRefAttr::get(ctx, "__cxa_throw"),
          /*resType=*/mlir::Type{},
          mlir::ValueRange{excPtr, typeInfo, dtor}, cir::CallingConv::C,
          cir::SideEffect::All, rewriter.getUnitAttr());
      if (auto attr = throwOp->getAttr("cir.throw_dtor_ptrauth"))
        callOp->setAttr("cir.throw_dtor_ptrauth", attr);

      // __cxa_throw never returns — set noreturn on the call-site attributes
      // so the LLVM lowering propagates it.  Matches OGCG's call-site attrs.
      mlir::NamedAttrList callAttrs;
      auto noReturn = cir::NoReturnAttr::get(ctx);
      callAttrs.set(noReturn.getMnemonic(), noReturn);
      callOp->setAttr(
          "extra_attrs",
          cir::ExtraFuncAttributesAttr::get(
              callAttrs.getDictionary(ctx)));

      // Add empty cleanup region.
      mlir::Region &cleanup = callOp.getCleanup();
      mlir::Block *cleanupBlock = rewriter.createBlock(&cleanup);
      rewriter.setInsertionPointToEnd(cleanupBlock);
      cir::YieldOp::create(rewriter, loc);

      // Remove the original throw + unreachable, add unreachable after call.
      if (auto *nextOp = throwOp->getNextNode())
        if (mlir::isa<cir::UnreachableOp>(nextOp))
          rewriter.eraseOp(nextOp);
      rewriter.setInsertionPointAfter(callOp);
      cir::UnreachableOp::create(rewriter, loc);
      rewriter.eraseOp(throwOp);
    }

    // `cir.catch_param end` with `cir.end_catch_enclosing_eh` must participate
    // in outer try flattening as a potentially-throwing operation. Promote it
    // to a synthetic exception call in the current try region so the existing
    // try_call / landing-pad machinery routes it through the enclosing catch.
    promoteEnclosingEHEndCatchOps(tryOp, rewriter);

    // Grab the collection of `cir.call exception`s to rewrite to
    // `cir.try_call`.
    llvm::SmallVector<cir::CallOp, 4> callsToRewrite;
    tryOp.getTryRegion().walk([&](CallOp op) {
      // Only grab calls within immediate closest TryOp scope.
      if (op->getParentOfType<cir::TryOp>() != tryOp)
        return;
      if (!op.getException())
        return;
      callsToRewrite.push_back(op);
    });

    llvm::SmallVector<std::pair<cir::CallOp, bool>, 2> handlerCallsToRewrite;

    // Flatten ScopeOps in the try region that contain ResumeFlatOps from
    // flattened inner tries. This exposes them directly in the try region
    // so they can be redirected to the outer try's catch dispatcher.
    flattenScopesInTryRegion(tryOp, rewriter);
    // Update afterBody after potential scope flattening.
    afterBody = &tryOp.getTryRegion().back();

    // Collect resume_flat ops from flattened inner tries. These represent
    // exceptions that didn't match an inner try's catch handlers and need
    // to be redirected to this outer try's catch dispatcher.
    llvm::SmallVector<cir::ResumeFlatOp, 2> resumeOpsToRedirect;
    tryOp.getTryRegion().walk([&](cir::ResumeFlatOp op) {
      resumeOpsToRedirect.push_back(op);
    });

    // When there are resume_flat ops from flattened inner tries, the inner
    // try's EhInflightOps (which become LLVM landingpad instructions) must
    // also include the outer try's catch types. Without this, the personality
    // function won't stop unwinding at the inner landing pad for exception
    // types that only the outer try handles.
    if (!resumeOpsToRedirect.empty()) {
      mlir::ArrayAttr outerSymbols = collectTypeSymbols(tryOp);
      bool outerHasCatchAll = hasCatchAll(tryOp);

      tryOp.getTryRegion().walk([&](cir::EhInflightOp ehOp) {
        if (outerSymbols) {
          llvm::SmallVector<mlir::Attribute> mergedList;
          if (auto existingList = ehOp.getCatchTypeListAttr())
            mergedList.append(existingList.begin(), existingList.end());
          for (mlir::Attribute sym : outerSymbols)
            if (!llvm::is_contained(mergedList, sym))
              mergedList.push_back(sym);
          ehOp.setCatchTypeListAttr(
              mlir::ArrayAttr::get(ehOp.getContext(), mergedList));
        }
        if (outerHasCatchAll && !ehOp.getCatchAll())
          ehOp.setCatchAllAttr(mlir::UnitAttr::get(ehOp.getContext()));
      });
    }

    // Propagate cleanup flag: if any call in callsToRewrite has a non-trivial
    // cleanup region (more than just YieldOp), the TryOp needs cleanup=true.
    // This handles rethrows from catch handlers whose cleanup regions contain
    // RAII destructor calls — after inner try flattening, these calls end up
    // in the outer try's callsToRewrite, and the outer try needs the cleanup
    // flag for the personality function to stop at the landing pad even when
    // no catch types match.
    if (!tryOp.getCleanup()) {
      for (CallOp callOp : callsToRewrite) {
        if (!callOp.getCleanup().empty()) {
          mlir::Block &cleanupBlock = callOp.getCleanup().front();
          // More than just YieldOp → has actual cleanup code.
          if (cleanupBlock.getOperations().size() > 1) {
            tryOp.setCleanup(true);
            break;
          }
        }
      }
    }

    // Build try body.
    mlir::Block *afterTry = buildTryBody(tryOp, rewriter);

    // Build catchers.
    llvm::SmallVector<mlir::Block *, 4> landingPads;
    buildCatchers(tryOp, rewriter, afterBody, afterTry, callsToRewrite,
                  handlerCallsToRewrite, landingPads, resumeOpsToRedirect);

    // Collapse single-predecessor forwarding blocks: when a landing pad
    // branches to a catch dispatcher that has only one predecessor, merge
    // the dispatcher into the landing pad to avoid the redundant forwarding
    // branch and phi node in LLVM IR. This produces tighter code that
    // matches OGCG's single-block terminate landing pad.
    {
      llvm::SmallPtrSet<mlir::Block *, 4> visited;
      for (mlir::Block *lpad : landingPads) {
        if (!visited.insert(lpad).second)
          continue;
        auto brOp = dyn_cast<cir::BrOp>(lpad->getTerminator());
        if (!brOp)
          continue;
        mlir::Block *dest = brOp->getSuccessor(0);
        if (!dest->getSinglePredecessor())
          continue;
        // Copy operands before erasing the branch.
        llvm::SmallVector<mlir::Value> brOperands(brOp.getDestOperands());
        rewriter.eraseOp(brOp);
        rewriter.mergeBlocks(dest, lpad, brOperands);
      }
    }

    rewriter.eraseOp(tryOp);
    assert((landingPads.size() == callsToRewrite.size()) &&
           "expected matching number of entries");

    auto rewriteExceptionCallAsTryCall =
        [&](CallOp callOp, mlir::Block *callBlock, mlir::Block *cont,
            mlir::Block *unwindDest) {
          llvm::SmallVector<mlir::NamedAttribute, 8> discardableAttrs;
          discardableAttrs.append(callOp->getDiscardableAttrs().begin(),
                                  callOp->getDiscardableAttrs().end());

          std::optional<cir::ExtraFuncAttributesAttr> extraAttrs =
              callOp.getExtraAttrs();
          mlir::FlatSymbolRefAttr symbol;
          if (!callOp.isIndirect())
            symbol = callOp.getCalleeAttr();

          mlir::Type resTy = nullptr;
          if (callOp.getNumResults() > 0)
            resTy = callOp.getResult().getType();

          rewriter.setInsertionPointToEnd(callBlock);
          auto tryCall = cir::TryCallOp::create(
              rewriter, callOp.getLoc(), symbol, resTy, cont, unwindDest,
              callOp.getOperands(), callOp.getSideEffect(),
              callOp.getCallingConv());
          if (extraAttrs)
            tryCall.setExtraAttrsAttr(*extraAttrs);

          // Preserve discardable call-site attrs synthesized before
          // FlattenCFG rewrites cir.call exception ops into cir.try_call ops.
          // This includes indirect-call arg/result attrs, speculative devirt
          // markers, and other lowering-relevant metadata carried directly on
          // the call operation.
          for (auto attr : discardableAttrs)
            tryCall->setAttr(attr.getName(), attr.getValue());

          rewriter.replaceOp(callOp, tryCall.getResults());
          return tryCall;
        };

    // Rewrite calls.
    unsigned callIdx = 0;
    for (CallOp callOp : callsToRewrite) {
      mlir::Block *callBlock = callOp->getBlock();
      mlir::Block *cont =
          rewriter.splitBlock(callBlock, mlir::Block::iterator(callOp));
      rewriteExceptionCallAsTryCall(callOp, callBlock, cont,
                                    landingPads[callIdx]);
      callIdx++;
    }

    // Rewrite exception calls from catch handler regions. These were
    // inlined by buildCatchers and need cleanup-only landing pads that
    // resume unwinding (not catch dispatcher routing). This ensures
    // rethrows from catch handlers are emitted as invokes so function-scope
    // RAII objects are destroyed during unwinding.
    for (auto &[callOp, endCatchNothrow] : handlerCallsToRewrite) {
      mlir::Block *callBlock = callOp->getBlock();
      if (!callBlock || !callBlock->getParent())
        continue;
      mlir::Block *cont =
          rewriter.splitBlock(callBlock, mlir::Block::iterator(callOp));

      auto exceptionPtrType =
          cir::PointerType::get(cir::VoidType::get(rewriter.getContext()));
      auto typeIdType = cir::IntType::get(getContext(), 32, false);

      // Create cleanup-only landing pad block.
      mlir::Block *landingPad = rewriter.createBlock(cont);
      rewriter.setInsertionPointToEnd(landingPad);

      auto inflightEh = cir::EhInflightOp::create(
          rewriter, callOp.getLoc(), exceptionPtrType, typeIdType,
          /*cleanup=*/rewriter.getUnitAttr(),
          /*catchAll=*/nullptr,
          /*catchTypeList=*/nullptr,
          /*filterTypeList=*/nullptr);
      auto exceptionPtr = inflightEh.getExceptionPtr();
      auto selector = inflightEh.getTypeId();

      // Inline the call's cleanup region. CIRGen emits a CatchParamOp::End
      // (__cxa_end_catch) at the correct position in the cleanup region:
      // after the catch parameter dtor (per C++ [except.throw]p4) but
      // before outer RAII destructors (per OGCG ordering).
      bool rawHandler = callOp->hasAttr(kCatchRawHandlerAttr);
      bool hasEndCatchInCleanup =
          !rawHandler && cleanupRegionContainsEndCatch(callOp);

      mlir::Block *resumeBlock = rewriter.createBlock(cont);
      rewriter.setInsertionPointToEnd(resumeBlock);

      // Only add CatchParamOp::End if the cleanup region didn't already
      // contain one (e.g., when the cleanup region was empty).
      if (!hasEndCatchInCleanup && !rawHandler) {
        auto endOp = cir::CatchParamOp::create(
            rewriter, callOp.getLoc(), mlir::Type{}, nullptr,
            cir::CatchParamKindAttr::get(rewriter.getContext(),
                                         cir::CatchParamKind::End));
        if (endCatchNothrow)
          endOp->setAttr("cir.end_catch_nothrow",
                          mlir::UnitAttr::get(rewriter.getContext()));
        // Mark as EH cleanup path so LowerToLLVM emits invoke-to-terminate.
        // Per C++ [except.terminate] p1.4, if __cxa_end_catch throws during
        // unwinding, std::terminate() must be called.
        endOp->setAttr("cir.end_catch_eh_cleanup",
                       mlir::UnitAttr::get(rewriter.getContext()));
      }

      // Resume unwinding after cleanup.
      cir::ResumeFlatOp::create(rewriter, callOp.getLoc(), exceptionPtr,
                                selector);

      if (!callOp.getCleanup().empty())
        inlineCleanupRegion(callOp, rewriter, landingPad, resumeBlock, {});
      else {
        rewriter.setInsertionPointToEnd(landingPad);
        cir::BrOp::create(rewriter, callOp.getLoc(), resumeBlock);
      }

      // Convert call to try_call.
      rewriteExceptionCallAsTryCall(callOp, callBlock, cont, landingPad);
    }

    // Quick block cleanup: no indirection to the post try block.
    auto brOp = dyn_cast<cir::BrOp>(afterTry->getTerminator());
    if (brOp && brOp.getDest()->hasNoPredecessors()) {
      mlir::Block *srcBlock = brOp.getDest();
      rewriter.eraseOp(brOp);
      rewriter.mergeBlocks(srcBlock, afterTry);
    }

    return mlir::success();
  }
};

/// Detect and eliminate phi-of-constant patterns from &&/|| lowering.
///
/// After FlattenCFG lowers TernaryOp(cond, rhs, false/true) for short-circuit
/// boolean operators, the resulting CFG has a redundant merge block with a
/// phi node that is immediately branched on:
///
///   ^pred: brcond %cond, ^true_succ, ^false_succ
///   ^false_succ: %c = cir.const(#false); br ^merge(%c)   // for &&
///   ^true_succ:  ...compute %rhs...; br ^merge(%rhs)
///   ^merge(%phi: !cir.bool): brcond %phi, ^then, ^else
///
/// This is transformed to:
///
///   ^pred: brcond %cond, ^true_succ, ^else
///   ^true_succ: ...compute %rhs...; brcond %rhs, ^then, ^else
///
/// For ||, the pattern has #true in the true-branch and the transformation
/// redirects the true-predecessor directly to ^then.
///
/// The optimization iterates to handle chained &&/|| operators where
/// eliminating one phi exposes the next.
static void eliminateShortCircuitPhis(mlir::Region &region) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (mlir::Block &mergeBlock :
         llvm::make_early_inc_range(region.getBlocks())) {
      // Must have exactly one block argument (the phi).
      if (mergeBlock.getNumArguments() != 1)
        continue;
      auto phiArg = mergeBlock.getArgument(0);
      if (!mlir::isa<cir::BoolType>(phiArg.getType()))
        continue;

      // The merge block must end with brcond using the phi as condition.
      auto *terminator = mergeBlock.getTerminator();
      auto brCondOp = dyn_cast<cir::BrCondOp>(terminator);
      if (!brCondOp || brCondOp.getCond() != phiArg)
        continue;

      // The phi must have no other uses beyond the brcond. If it does,
      // we cannot eliminate the merge block.
      if (!phiArg.hasOneUse())
        continue;

      // The merge block must contain only the brcond terminator and no other
      // operations. Side-effecting ops (e.g. cleanup flag stores) between
      // the phi definition and the brcond must not be dropped.
      if (&mergeBlock.front() != terminator)
        continue;

      // Collect predecessors. Must have exactly 2.
      llvm::SmallVector<mlir::Block *, 4> preds(mergeBlock.getPredecessors());
      if (preds.size() != 2)
        continue;

      // Identify which predecessor yields a constant bool and which yields
      // a computed value.
      mlir::Block *constPred = nullptr;
      mlir::Block *valuePred = nullptr;
      bool constValue = false;
      mlir::Value computedValue;

      for (mlir::Block *pred : preds) {
        auto *predTerm = pred->getTerminator();
        auto predBr = dyn_cast<cir::BrOp>(predTerm);
        if (!predBr || predBr.getDest() != &mergeBlock)
          continue;
        if (predBr.getDestOperands().size() != 1)
          continue;

        mlir::Value yieldedVal = predBr.getDestOperands()[0];
        auto constOp =
            dyn_cast_or_null<cir::ConstantOp>(yieldedVal.getDefiningOp());
        if (constOp) {
          auto boolAttr = mlir::dyn_cast<cir::BoolAttr>(constOp.getValue());
          if (boolAttr) {
            constPred = pred;
            constValue = boolAttr.getValue();
            continue;
          }
        }
        // This predecessor yields a non-constant value.
        valuePred = pred;
        computedValue = yieldedVal;
      }

      if (!constPred || !valuePred || !computedValue)
        continue;

      // Determine the targets from the merge block's brcond.
      mlir::Block *thenDest = brCondOp.getDestTrue();
      mlir::Block *elseDest = brCondOp.getDestFalse();

      // For &&: constant is false => constPred should go to elseDest.
      // For ||: constant is true  => constPred should go to thenDest.
      mlir::Block *constTarget = constValue ? thenDest : elseDest;

      // Rewrite constPred: replace its br-to-merge with br to constTarget.
      {
        auto *constPredTerm = constPred->getTerminator();
        mlir::OpBuilder builder(constPredTerm);
        auto newBr =
            cir::BrOp::create(builder, constPredTerm->getLoc(), constTarget);
        // Propagate nosanitize if present on the merge brcond.
        if (auto nosanitize = brCondOp->getAttr("cir.nosanitize"))
          newBr->setAttr("cir.nosanitize", nosanitize);
        constPredTerm->erase();
      }

      // Rewrite valuePred: replace its br-to-merge with brcond on the
      // computed value, branching directly to then/else.
      {
        auto *valuePredTerm = valuePred->getTerminator();
        mlir::OpBuilder builder(valuePredTerm);
        auto newBrCond =
            cir::BrCondOp::create(builder, valuePredTerm->getLoc(),
                                  computedValue, thenDest, elseDest);
        // Propagate attributes from the original brcond.
        if (auto nosanitize = brCondOp->getAttr("cir.nosanitize"))
          newBrCond->setAttr("cir.nosanitize", nosanitize);
        if (auto weights = brCondOp->getAttr("cir.branch_weights"))
          newBrCond->setAttr("cir.branch_weights", weights);
        valuePredTerm->erase();
      }

      // Clean up dead ops in constPred (e.g. the now-unused cir.const).
      for (auto &op : llvm::make_early_inc_range(
               llvm::reverse(constPred->getOperations()))) {
        if (op.use_empty() && !op.hasTrait<mlir::OpTrait::IsTerminator>() &&
            mlir::isOpTriviallyDead(&op))
          op.erase();
      }

      // If constPred is now a trivial forwarding block (only the br
      // terminator remains), redirect all predecessors to branch directly
      // to constTarget instead, then erase constPred.
      {
        auto constPredBr = dyn_cast<cir::BrOp>(constPred->getTerminator());
        bool isTrivialForwarding =
            constPredBr && constPredBr.getDestOperands().empty() &&
            &constPred->front() == constPred->getTerminator();
        if (isTrivialForwarding) {
          constPred->replaceAllUsesWith(constTarget);
          constPred->dropAllDefinedValueUses();
          constPred->dropAllUses();
          constPred->erase();
          constPred = nullptr;
        }
      }

      // Erase the merge block (it should now have no predecessors).
      if (mergeBlock.hasNoPredecessors()) {
        mergeBlock.dropAllDefinedValueUses();
        mergeBlock.dropAllUses();
        mergeBlock.erase();
      }

      // Also check if constPred is now a trivially dead block
      // (has no predecessors and is not the entry block).
      if (constPred && constPred != &region.front() &&
          constPred->hasNoPredecessors()) {
        constPred->dropAllDefinedValueUses();
        constPred->dropAllUses();
        constPred->erase();
      }

      changed = true;
    }
  }
}

// Lower cir.cleanup.scope to a shared exception-cleanup landing pad. Every
// throwing body call is rewritten to a cir.try_call whose unwind edge targets an
// `ehHeader` (eh.inflight) that branches to a `cleanupBody` block. The
// cleanupBody runs this scope's guarded destructors, then either resumes
// (outermost scope) or BRANCHES to the enclosing cir.cleanup.scope's cleanupBody,
// threading the exception token as block arguments (OGCG cleanup-dispatch chain).
// Chaining — rather than each scope re-emitting the enclosing cleanups inline —
// keeps NESTED initializer_list construction O(N) instead of O(N*depth).
// `enclosingMap` maps each cleanup.scope to its nearest enclosing one;
// `cleanupBodyMap` records each flattened scope's cleanupBody (filled
// outermost-first so the enclosing entry exists when an inner scope chains).
static mlir::LogicalResult flattenCleanupScopeOp(
    cir::CleanupScopeOp op, mlir::IRRewriter &rewriter,
    const llvm::DenseMap<mlir::Operation *, mlir::Operation *> &enclosingMap,
    llvm::DenseMap<mlir::Operation *, mlir::Block *> &cleanupBodyMap) {
  mlir::Location loc = op.getLoc();
  mlir::MLIRContext *ctx = op.getContext();
  mlir::Region &bodyRegion = op.getBody();
  mlir::Region &cleanupRegion = op.getCleanup();
  if (bodyRegion.empty() || cleanupRegion.empty())
    return mlir::failure();

  // Collect the throwing calls in the body up-front (skipping nested scopes /
  // tries, which own their own cleanups). Op handles survive region inlining.
  llvm::SmallVector<cir::CallOp> exceptionCalls;
  bodyRegion.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation *nested) {
    if (nested != op.getOperation() &&
        isa<cir::CleanupScopeOp, cir::TryOp>(nested))
      return mlir::WalkResult::skip();
    if (auto call = dyn_cast<cir::CallOp>(nested))
      if (call.getException())
        exceptionCalls.push_back(call);
    return mlir::WalkResult::advance();
  });

  auto exceptionPtrType = cir::PointerType::get(cir::VoidType::get(ctx));
  auto typeIdType = cir::IntType::get(ctx, 32, false);

  // Split at the op; `continueBlock` holds everything after the scope.
  mlir::Block *currentBlock = op->getBlock();
  rewriter.setInsertionPoint(op);
  mlir::Block *continueBlock =
      rewriter.splitBlock(currentBlock, rewriter.getInsertionPoint());

  // Inline the cleanup region; its entry becomes `cleanupBody`, which takes the
  // exception-token block arguments (ptr, type-id) threaded from whichever
  // landing pad reached it.
  mlir::Block *cleanupBody = &cleanupRegion.front();
  rewriter.inlineRegionBefore(cleanupRegion, continueBlock);
  mlir::Value tokPtr = cleanupBody->addArgument(exceptionPtrType, loc);
  mlir::Value tokId = cleanupBody->addArgument(typeIdType, loc);

  // Chain target: the enclosing cleanup.scope's cleanupBody (recorded when it
  // was flattened, outermost-first), if any.
  mlir::Block *enclosingBody = nullptr;
  if (auto encIt = enclosingMap.find(op.getOperation());
      encIt != enclosingMap.end()) {
    if (auto bIt = cleanupBodyMap.find(encIt->second);
        bIt != cleanupBodyMap.end())
      enclosingBody = bIt->second;
  }

  // Terminate cleanupBody: resume (outermost) or branch to the enclosing
  // cleanupBody, forwarding the exception token.
  for (mlir::Block &b : llvm::make_range(cleanupBody->getIterator(),
                                         continueBlock->getIterator())) {
    if (auto y = dyn_cast_or_null<cir::YieldOp>(b.getTerminator())) {
      rewriter.setInsertionPoint(y);
      if (enclosingBody)
        cir::BrOp::create(rewriter, y.getLoc(),
                          mlir::ValueRange{tokPtr, tokId}, enclosingBody);
      else
        cir::ResumeFlatOp::create(rewriter, y.getLoc(), tokPtr, tokId);
      rewriter.eraseOp(y);
    }
  }

  // ehHeader (per-scope landing pad for direct unwinds): eh.inflight, then
  // branch into cleanupBody forwarding the freshly-caught token.
  mlir::Block *ehHeader = rewriter.createBlock(cleanupBody);
  rewriter.setInsertionPointToEnd(ehHeader);
  auto inflightEh = cir::EhInflightOp::create(
      rewriter, loc, exceptionPtrType, typeIdType,
      /*cleanup=*/mlir::UnitAttr::get(ctx), /*catch_all=*/nullptr,
      /*sym_type_list=*/nullptr, /*filter_type_list=*/nullptr);
  cir::BrOp::create(
      rewriter, loc,
      mlir::ValueRange{inflightEh.getExceptionPtr(), inflightEh.getTypeId()},
      cleanupBody);

  // Inline the body before the landing pad; branch into it and turn its
  // terminating yields into branches to the continuation.
  mlir::Block *bodyEntry = &bodyRegion.front();
  rewriter.inlineRegionBefore(bodyRegion, ehHeader);
  rewriter.setInsertionPointToEnd(currentBlock);
  cir::BrOp::create(rewriter, loc, mlir::ValueRange(), bodyEntry);
  for (mlir::Block &b :
       llvm::make_range(bodyEntry->getIterator(), ehHeader->getIterator())) {
    if (auto y = dyn_cast_or_null<cir::YieldOp>(b.getTerminator())) {
      rewriter.setInsertionPoint(y);
      cir::BrOp::create(rewriter, y.getLoc(), mlir::ValueRange(), continueBlock);
      rewriter.eraseOp(y);
    }
  }

  // Route every throwing body call to this scope's ehHeader.
  for (cir::CallOp call : exceptionCalls) {
    mlir::Block *callBlock = call->getBlock();
    mlir::Block *cont =
        rewriter.splitBlock(callBlock, std::next(call->getIterator()));
    mlir::FlatSymbolRefAttr symbol;
    if (!call.isIndirect())
      symbol = call.getCalleeAttr();
    mlir::Type resTy =
        call.getNumResults() > 0 ? call.getResult().getType() : mlir::Type();
    llvm::SmallVector<mlir::NamedAttribute, 8> discardableAttrs(
        call->getDiscardableAttrs().begin(), call->getDiscardableAttrs().end());
    rewriter.setInsertionPointToEnd(callBlock);
    auto tryCall = cir::TryCallOp::create(
        rewriter, call.getLoc(), symbol, resTy, cont, ehHeader,
        call.getOperands(), call.getSideEffect(), call.getCallingConv());
    if (auto extra = call.getExtraAttrs())
      tryCall.setExtraAttrsAttr(*extra);
    // Carry over the call-site attributes synthesized before flattening. For
    // an indirect call these hold the whole ABI contract (cir.sret, byval,
    // sign/zero extension, noundef): there is no callee symbol to recover them
    // from later, and dropping cir.sret alone moves the return slot out of the
    // AArch64 indirect-result register and shifts every argument by one.
    for (auto attr : discardableAttrs)
      tryCall->setAttr(attr.getName(), attr.getValue());
    rewriter.replaceOp(call, tryCall.getResults());
  }

  cleanupBodyMap[op.getOperation()] = cleanupBody;
  rewriter.eraseOp(op);
  return mlir::success();
}

void CIRFlattenCFGPass::runOnOperation() {
  llvm::TimeTraceScope scope("CIR FlattenCFG");
  // One walk for the cleanup scopes and for whether there is any OpenMP op at
  // all: asking each cir.scope whether it holds one walks the scope's whole
  // subtree, and most modules have none.
  llvm::SmallVector<cir::CleanupScopeOp> cleanupScopes;
  llvm::DenseMap<mlir::Operation *, mlir::Operation *> enclosingMap;
  bool hasOpenMPOps = false;
  auto collectCleanupScopes = [&] {
    cleanupScopes.clear();
    enclosingMap.clear();
    getOperation()->walk<mlir::WalkOrder::PreOrder>([&](Operation *op) {
      if (auto cs = dyn_cast<cir::CleanupScopeOp>(op)) {
        cleanupScopes.push_back(cs);
        if (auto enc = cs->getParentOfType<cir::CleanupScopeOp>())
          enclosingMap[cs.getOperation()] = enc.getOperation();
      } else if (op->getDialect() &&
                 isa<omp::OpenMPDialect>(op->getDialect())) {
        hasOpenMPOps = true;
      }
    });
  };
  collectCleanupScopes();

  if (hasOpenMPOps) {
    llvm::SmallVector<cir::ScopeOp> protectedOpenMPScopeOps;
    getOperation()->walk<mlir::WalkOrder::PostOrder>([&](cir::ScopeOp scopeOp) {
      if (containsNestedOpenMPOps(scopeOp))
        protectedOpenMPScopeOps.push_back(scopeOp);
    });
    if (!protectedOpenMPScopeOps.empty()) {
      mlir::IRRewriter rewriter(&getContext());
      for (cir::ScopeOp scopeOp : protectedOpenMPScopeOps) {
        if (!scopeOp->getBlock())
          continue;
        if (flattenScopeOp(scopeOp, rewriter).failed()) {
          signalPassFailure();
          return;
        }
      }
      // Flattening those scopes moved what they held.
      collectCleanupScopes();
    }
  }

  // Flatten cir.cleanup.scope ops manually, OUTERMOST-first, so a nested scope
  // chains its cleanup dispatch to the enclosing scope's cleanupBody (recorded
  // by the time we reach the nested one). Done before the greedy patterns so the
  // guarded destructors (cir.if / cir.do) emitted into the cleanupBody get
  // flattened by the normal patterns afterward.
  if (!cleanupScopes.empty()) {
    mlir::IRRewriter rewriter(&getContext());
    llvm::DenseMap<mlir::Operation *, mlir::Block *> cleanupBodyMap;
    // PreOrder walk order is outermost-first.
    for (cir::CleanupScopeOp cs : cleanupScopes) {
      if (!cs->getBlock())
        continue;
      if (flattenCleanupScopeOp(cs, rewriter, enclosingMap, cleanupBodyMap)
              .failed()) {
        signalPassFailure();
        return;
      }
    }
  }

  // Collect operations to apply patterns.
  llvm::SmallVector<Operation *, 16> ops;
  llvm::SmallVector<Operation *, 8> tryOps;
  bool hasAwaitOp = false;
  bool hasIfOp = false;
  bool hasLoopOp = false;
  bool hasScopeOp = false;
  bool hasSwitchOp = false;
  bool hasTernaryOp = false;
  bool hasTryOp = false;
  getOperation()->walk<mlir::WalkOrder::PostOrder>([&](Operation *op) {
    if (isa<TryOp>(op)) {
      hasTryOp = true;
      tryOps.push_back(op);
    } else if (isa<AwaitOp>(op)) {
      hasAwaitOp = true;
      ops.push_back(op);
    } else if (isa<IfOp>(op)) {
      hasIfOp = true;
      ops.push_back(op);
    } else if (auto scopeOp = dyn_cast<ScopeOp>(op)) {
      if (hasOpenMPOps && containsNestedOpenMPOps(scopeOp))
        return;
      hasScopeOp = true;
      ops.push_back(op);
    } else if (isa<SwitchOp>(op)) {
      hasSwitchOp = true;
      ops.push_back(op);
    } else if (isa<LoopOpInterface>(op)) {
      hasLoopOp = true;
      ops.push_back(op);
    } else if (isa<TernaryOp>(op)) {
      hasTernaryOp = true;
      ops.push_back(op);
    }
  });

  // Fast path for already-flat modules: avoid pattern construction and greedy
  // driver setup when there is nothing to rewrite.
  if (ops.empty() && tryOps.empty())
    return;

  // Build the SymbolTable once and share it across sub-passes that need it
  // (CIRAwaitOpFlattening, CIRTryOpFlattening). Each construction walks the
  // entire module, so sharing avoids redundant O(N) walks.
  mlir::SymbolTable symTable(getOperation());

  RewritePatternSet patterns(&getContext());
  auto *ctx = patterns.getContext();
  if (hasAwaitOp)
    patterns.add<CIRAwaitOpFlattening>(ctx, symTable);
  if (hasIfOp)
    patterns.add<CIRIfFlattening>(ctx);
  if (hasLoopOp)
    patterns.add<CIRLoopOpInterfaceFlattening>(ctx);
  if (hasScopeOp)
    patterns.add<CIRScopeOpFlattening>(ctx);
  if (hasSwitchOp)
    patterns.add<CIRSwitchOpFlattening>(ctx);
  if (hasTernaryOp)
    patterns.add<CIRTernaryOpFlattening>(ctx);
  if (hasTryOp)
    patterns.add<CIRTryOpFlattening>(ctx, symTable);

  // TryOps must be processed innermost-first so that when an inner TryOp
  // is flattened, its catch handler code (including rethrows and throws)
  // becomes visible as flat blocks in the outer TryOp's try region.
  // The outer TryOp can then properly wire landing pads for those calls.
  //
  // PostOrder walk yields [innermost, ..., outermost]. The greedy driver
  // uses a LIFO worklist (last element processed first), so we reverse
  // the TryOps to get [outermost, ..., innermost] — LIFO then processes
  // innermost first.
  std::reverse(tryOps.begin(), tryOps.end());
  ops.append(tryOps.begin(), tryOps.end());

  // Apply patterns. They rewrite an op within its own function, except try
  // and await flattening, which use the module's symbol table. So the
  // functions holding neither are flattened in parallel, each with its ops in
  // the order above, and the rest after them as one list, as before. No
  // function's rewrite sees another's, and each function's ops keep their
  // relative order on the driver's worklist, so the result is the same.
  mlir::FrozenRewritePatternSet frozenPatterns(std::move(patterns));
  llvm::DenseSet<mlir::Operation *> serialFunctions;
  for (Operation *op : ops)
    if (isa<TryOp, AwaitOp>(op))
      serialFunctions.insert(op->getParentOfType<cir::FuncOp>());
  llvm::MapVector<mlir::Operation *, llvm::SmallVector<Operation *, 16>>
      opsByFunction;
  llvm::SmallVector<Operation *, 16> serialOps;
  for (Operation *op : ops) {
    Operation *func = op->getParentOfType<cir::FuncOp>();
    if (!func || serialFunctions.contains(func))
      serialOps.push_back(op);
    else
      opsByFunction[func].push_back(op);
  }
  std::atomic<bool> flattenFailed = false;
  mlir::parallelForEach(
      &getContext(), opsByFunction.takeVector(), [&](auto &entry) {
        if (applyOpPatternsGreedily(entry.second, frozenPatterns).failed())
          flattenFailed = true;
      });
  if (!serialOps.empty() &&
      applyOpPatternsGreedily(serialOps, frozenPatterns).failed())
    flattenFailed = true;
  if (flattenFailed)
    signalPassFailure();

  // Flatten the delayed get-return-object conversion cir.if that
  // CIRIfFlattening intentionally left structured (tagged kCoroGroConvAttr) so
  // CIRAwaitOpFlattening could clone it onto each suspend-return edge. Its
  // clones already flattened above (their marker was stripped on clone). Now
  // strip the marker from the surviving originals and flatten them (and the
  // converting-ctor cir.try they contain) in a dedicated greedy pass. Only a
  // coroutine has one, and a coroutine has cir.await ops.
  if (hasAwaitOp) {
    llvm::SmallVector<mlir::Operation *, 4> groConvOps;
    llvm::SmallVector<mlir::Operation *, 2> groConvTryOps;
    getOperation()->walk([&](cir::IfOp ifOp) {
      if (ifOp->hasAttr(kCoroGroConvAttr)) {
        ifOp->removeAttr(kCoroGroConvAttr);
        groConvOps.push_back(ifOp);
        // The conversion cir.if contains the converting-ctor cir.try; enqueue
        // it explicitly so the greedy pass flattens it (it is relocated, not
        // freshly inserted, when the enclosing cir.if flattens, so it would
        // not otherwise be picked up by the AnyOp worklist).
        ifOp.walk([&](cir::TryOp tryOp) { groConvTryOps.push_back(tryOp); });
      }
    });
    if (!groConvOps.empty()) {
      // Process innermost cir.try first (LIFO worklist, matching the main
      // pass), then the cir.if wrappers.
      std::reverse(groConvTryOps.begin(), groConvTryOps.end());
      groConvOps.append(groConvTryOps.begin(), groConvTryOps.end());
      RewritePatternSet groConvPatterns(&getContext());
      groConvPatterns.add<CIRIfFlattening>(ctx);
      groConvPatterns.add<CIRTryOpFlattening>(ctx, symTable);
      if (applyOpPatternsGreedily(groConvOps, std::move(groConvPatterns))
              .failed())
        signalPassFailure();
    }
  }

  // The post-flattening cleanups below each work inside one function, so they
  // run over the functions in parallel when the context is multithreaded
  // (cir-link's is).
  llvm::SmallVector<cir::FuncOp> funcs;
  getOperation()->walk([&](cir::FuncOp func) { funcs.push_back(func); });
  auto forEachFunction = [&](auto &&fn) {
    mlir::parallelForEach(&getContext(), funcs, fn);
  };

  // Post-flattening: eliminate phi-of-constant patterns from short-circuit
  // boolean operators (&&/||). After TernaryOp flattening, these create
  // redundant merge blocks with phi nodes that are immediately branched on.
  // Thread the branches directly to eliminate ~2 blocks per operator.
  if (hasTernaryOp) {
    forEachFunction([](cir::FuncOp func) {
      if (func.isDeclaration())
        return;
      mlir::Region &body = func.getBody();
      if (body.empty())
        return;
      eliminateShortCircuitPhis(body);
    });
  }

  // [ ] Post-flattening cleanup: merge forwarding blocks and promote
  // single-store/single-load try-call-res allocas.
  //
  // After TryOp flattening, buildTryBody creates forwarding blocks (br-only
  // blocks) between the try body continuation and afterTry. These forwarding
  // blocks prevent store/load promotion because the store (in the try_call
  // continuation) and load (in afterTry) are in different blocks. Merging
  // the forwarding blocks brings them together, enabling promotion of the
  // unnecessary tmp.try.call.res alloca introduced by CIRGen to bridge
  // scalar call results across MLIR region boundaries.
  if (hasTryOp) {
    forEachFunction([](cir::FuncOp func) {
      if (func.isDeclaration())
        return;
      mlir::Region &body = func.getBody();
      if (body.empty())
        return;

      auto mergeSinglePredecessorForwardingBlocks = [&]() {
        bool changed = true;
        while (changed) {
          changed = false;
          for (mlir::Block &block : llvm::make_early_inc_range(body.getBlocks())) {
            if (&block == &body.front())
              continue;
            mlir::Block *pred = block.getSinglePredecessor();
            if (!pred)
              continue;
            auto brOp = dyn_cast<cir::BrOp>(pred->getTerminator());
            if (!brOp)
              continue;
            // Don't merge blocks that contain labels or indirect branches —
            // GotoSolver needs these blocks to remain separate so it can
            // wire gotos to the correct block entry points.
            if (llvm::any_of(block.getOperations(), [](mlir::Operation &op) {
                  return isa<cir::LabelOp, cir::IndirectBrOp>(op);
                }))
              continue;
            // Don't merge blocks that are fallthrough targets of asm goto.
            if (mlir::Block *prev = block.getPrevNode()) {
              if (prev->getTerminator() &&
                  isa<cir::InlineAsmGotoOp>(prev->getTerminator()))
                continue;
            }
            // Replace block arguments with branch operands.
            for (auto [arg, operand] :
                 llvm::zip(block.getArguments(), brOp.getDestOperands()))
              arg.replaceAllUsesWith(operand);
            brOp.erase();
            pred->getOperations().splice(pred->end(), block.getOperations());
            block.erase();
            changed = true;
          }
        }
      };

      // Step 1: Merge single-predecessor forwarding blocks. When a block
      // has exactly one predecessor and that predecessor's terminator is an
      // unconditional cir.br to it, merge the block into the predecessor.
      mergeSinglePredecessorForwardingBlocks();

      // Step 2: Deduplicate terminate-only landing pads. When a noexcept
      // function has multiple calls under different control flow paths,
      // each gets its own identical terminate landing pad block. OGCG
      // shares a single landing pad for all of them. Merge duplicates
      // so the CFG matches OGCG.
      {
        mlir::Block *canonicalTerminatePad = nullptr;
        llvm::SmallVector<mlir::Block *, 4> duplicateTerminatePads;

        for (mlir::Block &block : body.getBlocks()) {
          // Match terminate-only landing pads:
          //   eh.inflight_exception catch_all
          //   catch_param begin
          //   call @_ZSt9terminatev() nothrow
          //   unreachable
          auto it = block.begin();
          if (it == block.end())
            continue;
          auto ehOp = dyn_cast<cir::EhInflightOp>(&*it);
          if (!ehOp || !ehOp.getCatchAll())
            continue;
          ++it;
          if (it == block.end())
            continue;
          auto catchParam = dyn_cast<cir::CatchParamOp>(&*it);
          if (!catchParam || !catchParam.getKind() ||
              *catchParam.getKind() != cir::CatchParamKind::Begin)
            continue;
          ++it;
          if (it == block.end())
            continue;
          auto callOp = dyn_cast<cir::CallOp>(&*it);
          if (!callOp || !callOp.getCallee() ||
              callOp.getCallee()->str() != "_ZSt9terminatev")
            continue;
          ++it;
          if (it == block.end())
            continue;
          if (!isa<cir::UnreachableOp>(&*it))
            continue;
          ++it;
          if (it != block.end())
            continue; // Extra ops — not a simple terminate pad.

          if (!canonicalTerminatePad)
            canonicalTerminatePad = &block;
          else
            duplicateTerminatePads.push_back(&block);
        }

        for (mlir::Block *dup : duplicateTerminatePads) {
          for (mlir::BlockOperand &use :
               llvm::make_early_inc_range(dup->getUses()))
            use.set(canonicalTerminatePad);
          dup->dropAllDefinedValueUses();
          dup->dropAllUses();
          dup->erase();
        }
      }

      // Step 3: Share suffixes across cleanup-only landing pads. Constructor
      // cleanup landing pads often come from different synthetic try ops, so
      // they only coexist after flattening. Match the flat pattern
      //   eh.inflight_exception cleanup
      //   <cleanup code>
      //   resume.flat
      // and factor common cleanup suffixes into shared blocks.
      //
      // CIRGen emits, at every unwind edge, an inline copy of every cleanup
      // that is in scope there, so the raw cleanup code is quadratic in the
      // number of locals in a scope. Recovering OGCG's linear chain of landing
      // pads means splitting each pad into "cleanup units" -- one per object
      // destroyed -- and sharing the common tail of that sequence.
      //
      // A unit is delimited by "cut points": program points inside the pad
      // where the only values still live are the in-flight exception pair,
      // which every shared block takes as arguments. A unit is therefore
      // either a single side-effecting op (a scalar destructor call) or a whole
      // single-entry/single-exit segment of blocks (the destroy loop that
      // LoweringPrepare expands cir.array.dtor into, which is why array locals
      // need the block-level form).
      {
        // A program point inside a landing pad.
        struct CutPoint {
          mlir::Block *block;
          mlir::Block::iterator pos;
        };
        // The code between two consecutive cut points. `blocks` holds the
        // blocks the unit owns in DFS order, starting with `begin.block`;
        // control leaves the unit only by branching to `end.block`.
        struct CleanupUnit {
          CutPoint begin;
          CutPoint end;
          llvm::SmallVector<mlir::Block *, 2> blocks;
          // Allocas the unit uses and nothing else does.
          llvm::SmallVector<mlir::Value, 2> scratch;
          // Shape summary, so units that cannot match are rejected without
          // walking them. Equal units always have equal fingerprints.
          llvm::hash_code fingerprint;
        };
        // A cleanup region and its decomposition. The region is rooted either
        // at a landing pad -- `ehOp` is the pad's inflight op -- or at a
        // cleanup chain head, a block CIRGen shares between the unwind edges
        // of one construction and which takes the in-flight exception pair as
        // its arguments. It leaves either through a `resume.flat` of its own
        // or through a single edge to `continuation`; `exitBlock` holds the
        // last cut point either way.
        struct CleanupLandingPad {
          mlir::Block *block;
          cir::EhInflightOp ehOp;
          mlir::Value exceptionPtr;
          mlir::Value typeId;
          cir::ResumeFlatOp resumeOp;
          mlir::Block *continuation;
          mlir::Block *exitBlock;
          llvm::SmallVector<mlir::Block *, 4> padBlocks;
          llvm::SmallVector<CleanupUnit, 4> units;
        };
        // A suffix of some pad's unit sequence that has been materialized into
        // shared blocks; `block` is the entry of the shared chain.
        struct CleanupSuffixNode {
          unsigned infoIdx;
          unsigned unitIdx;
          mlir::Block *block;
        };

        // Iteration range of the ops a unit owns in `block`.
        auto unitOpRange = [](const CleanupUnit &unit, mlir::Block *block) {
          mlir::Block::iterator first =
              block == unit.begin.block ? unit.begin.pos : block->begin();
          mlir::Block::iterator last =
              block == unit.end.block ? unit.end.pos : block->end();
          return llvm::make_range(first, last);
        };

        // Counting an alloca's users is O(uses), and the object a cleanup
        // destroys is used from every inline copy of that cleanup, so the
        // count is asked for over and over. Memoize it.
        llvm::DenseMap<mlir::Value, unsigned> allocaUserCounts;
        auto allocaUserCount = [&](mlir::Value alloca) {
          auto [it, inserted] = allocaUserCounts.try_emplace(alloca, 0);
          if (inserted)
            it->second = llvm::range_size(alloca.getUsers());
          return it->second;
        };

        // Is `op` one of the ops `unit` owns?
        auto unitOwns = [&](const CleanupUnit &unit, mlir::Operation *op) {
          mlir::Block *block = op->getBlock();
          if (!llvm::is_contained(unit.blocks, block))
            return false;
          return llvm::any_of(
              unitOpRange(unit, block),
              [&](mlir::Operation &other) { return &other == op; });
        };

        // Structural equivalence of two units, up to a consistent renaming of
        // the values and blocks they define. Values defined outside a unit
        // (allocas, globals) must be the same SSA value in both.
        auto unitsEquivalent = [&](const CleanupUnit &lhs,
                                   const CleanupUnit &rhs) {
          if (lhs.fingerprint != rhs.fingerprint)
            return false;
          if (lhs.blocks.size() != rhs.blocks.size())
            return false;

          llvm::DenseMap<mlir::Value, mlir::Value> valueMap;
          llvm::DenseMap<mlir::Block *, mlir::Block *> blockMap;
          for (auto [lhsBlock, rhsBlock] :
               llvm::zip_equal(lhs.blocks, rhs.blocks)) {
            if (lhsBlock->getNumArguments() != rhsBlock->getNumArguments())
              return false;
            for (auto [lhsArg, rhsArg] : llvm::zip_equal(
                     lhsBlock->getArguments(), rhsBlock->getArguments())) {
              if (lhsArg.getType() != rhsArg.getType())
                return false;
              valueMap[lhsArg] = rhsArg;
            }
            blockMap[lhsBlock] = rhsBlock;
          }
          // The exit edges of two equivalent units both leave the unit.
          blockMap[lhs.end.block] = rhs.end.block;

          auto checkEquivalent = [&](mlir::Value lhsVal, mlir::Value rhsVal) {
            auto it = valueMap.find(lhsVal);
            if (it != valueMap.end())
              return mlir::success(it->second == rhsVal);
            // A value neither unit defines -- the object being destroyed, a
            // global -- has to be literally the same in both.
            if (lhsVal == rhsVal)
              return mlir::success();
            // CIRGen gives every inline copy of an array destroy its own
            // __array_idx slot, so two otherwise identical units differ in
            // which scratch alloca they use. That is a renaming like any
            // other: the surviving copy's slot serves both.
            auto lhsAlloca = lhsVal.getDefiningOp<cir::AllocaOp>();
            auto rhsAlloca = rhsVal.getDefiningOp<cir::AllocaOp>();
            if (!lhsAlloca || !rhsAlloca ||
                lhsAlloca.getAllocaType() != rhsAlloca.getAllocaType() ||
                lhsAlloca.getType() != rhsAlloca.getType() ||
                lhsAlloca.getAlignment() != rhsAlloca.getAlignment() ||
                !llvm::is_contained(lhs.scratch, lhsVal) ||
                !llvm::is_contained(rhs.scratch, rhsVal))
              return mlir::failure();
            valueMap[lhsVal] = rhsVal;
            return mlir::success();
          };
          auto markEquivalent = [&](mlir::Value lhsVal, mlir::Value rhsVal) {
            valueMap[lhsVal] = rhsVal;
          };

          for (auto [lhsBlock, rhsBlock] :
               llvm::zip_equal(lhs.blocks, rhs.blocks)) {
            auto lhsRange = unitOpRange(lhs, lhsBlock);
            auto rhsRange = unitOpRange(rhs, rhsBlock);
            auto lhsIt = lhsRange.begin(), lhsEnd = lhsRange.end();
            auto rhsIt = rhsRange.begin(), rhsEnd = rhsRange.end();
            for (; lhsIt != lhsEnd && rhsIt != rhsEnd; ++lhsIt, ++rhsIt) {
              if (!mlir::OperationEquivalence::isEquivalentTo(
                      &*lhsIt, &*rhsIt, checkEquivalent, markEquivalent,
                      mlir::OperationEquivalence::IgnoreLocations))
                return false;
              // isEquivalentTo compares the successor count but not the
              // successors themselves; branch targets have to agree under the
              // block renaming for the two control-flow shapes to match.
              for (unsigned i = 0, e = lhsIt->getNumSuccessors(); i != e; ++i) {
                auto mapped = blockMap.find(lhsIt->getSuccessor(i));
                if (mapped == blockMap.end() ||
                    mapped->second != rhsIt->getSuccessor(i))
                  return false;
              }
            }
            if (lhsIt != lhsEnd || rhsIt != rhsEnd)
              return false;
          }
          return true;
        };

        auto suffixesEquivalent = [&](CleanupLandingPad &lhsPad,
                                      unsigned lhsStart,
                                      CleanupLandingPad &rhsPad,
                                      unsigned rhsStart) {
          if (lhsPad.units.size() - lhsStart != rhsPad.units.size() - rhsStart)
            return false;
          if (lhsPad.exceptionPtr.getType() != rhsPad.exceptionPtr.getType() ||
              lhsPad.typeId.getType() != rhsPad.typeId.getType())
            return false;
          // A suffix always runs to the end of both regions, so the two must
          // also leave the same way -- resume, or the same continuation.
          if (lhsPad.continuation != rhsPad.continuation)
            return false;
          for (unsigned i = 0, e = lhsPad.units.size() - lhsStart; i != e; ++i)
            if (!unitsEquivalent(lhsPad.units[lhsStart + i],
                                 rhsPad.units[rhsStart + i]))
              return false;
          return true;
        };

        // Where a cleanup region starts, and what its in-flight exception pair
        // is. Two shapes qualify. A landing pad holds the pair in its inflight
        // op's results. A *cleanup chain head* takes the pair as its block
        // arguments: CIRGen emits one for a braced init-list of elements that
        // hold nested objects, where the two unwind edges a single element
        // generates both branch to one block carrying that element's cleanup,
        // so the cleanup lives in neither pad and the pads are not private.
        struct CleanupRoot {
          cir::EhInflightOp ehOp; // Null for a chain head.
          mlir::Value exceptionPtr;
          mlir::Value typeId;
        };
        auto isCleanupInflight = [](cir::EhInflightOp ehOp) {
          return ehOp && ehOp.getCleanup() && !ehOp.getCatchAll() &&
                 !ehOp.getCatchTypeListAttr() && !ehOp.getFilterTypeListAttr();
        };
        auto getCleanupRoot =
            [&](mlir::Block &entry) -> std::optional<CleanupRoot> {
          if (entry.empty())
            return std::nullopt;
          if (entry.getArguments().empty()) {
            auto ehOp = dyn_cast<cir::EhInflightOp>(&entry.front());
            if (!isCleanupInflight(ehOp))
              return std::nullopt;
            return CleanupRoot{ehOp, ehOp.getExceptionPtr(), ehOp.getTypeId()};
          }
          // A chain head carries the exception pair and nothing else. What
          // makes those two arguments an exception pair is that every
          // predecessor hands over exactly the pair its own cleanup pad
          // caught.
          if (entry.getNumArguments() != 2 || entry.hasNoPredecessors())
            return std::nullopt;
          for (mlir::Block *pred : entry.getPredecessors()) {
            if (!pred->mightHaveTerminator())
              return std::nullopt;
            auto brOp = dyn_cast<cir::BrOp>(pred->getTerminator());
            if (!brOp || brOp.getDestOperands().size() != 2)
              return std::nullopt;
            auto predEh = dyn_cast<cir::EhInflightOp>(&pred->front());
            if (!isCleanupInflight(predEh) ||
                brOp.getDestOperands()[0] != predEh.getExceptionPtr() ||
                brOp.getDestOperands()[1] != predEh.getTypeId())
              return std::nullopt;
          }
          return CleanupRoot{nullptr, entry.getArgument(0),
                             entry.getArgument(1)};
        };

        // Gather the region rooted at `entry`: every block the root dominates,
        // which makes the region private for free. It must leave in exactly
        // one way -- its own `resume.flat`, or edges to a single continuation
        // block carrying nothing but the exception pair. `exits` lists the
        // blocks holding those edges.
        auto findCleanupRegion =
            [&](mlir::DominanceInfo &domInfo, mlir::Block &entry,
                const CleanupRoot &root,
                llvm::SmallVectorImpl<mlir::Block *> &blocks,
                llvm::SmallVectorImpl<mlir::Block *> &exits,
                mlir::Block *&continuation, cir::ResumeFlatOp &resumeOp) {
              llvm::SmallPtrSet<mlir::Block *, 8> seen{&entry};
              llvm::SmallVector<mlir::Block *, 8> worklist{&entry};
              continuation = nullptr;
              resumeOp = nullptr;
              while (!worklist.empty()) {
                mlir::Block *block = worklist.pop_back_val();
                if (!block->mightHaveTerminator())
                  return false;
                blocks.push_back(block);
                if (auto candidate =
                        dyn_cast<cir::ResumeFlatOp>(block->getTerminator())) {
                  if (resumeOp)
                    return false; // More than one exit.
                  if (candidate.getExceptionPtr() != root.exceptionPtr ||
                      candidate.getTypeId() != root.typeId)
                    return false;
                  resumeOp = candidate;
                  continue;
                }
                auto branch =
                    dyn_cast<mlir::BranchOpInterface>(block->getTerminator());
                for (unsigned i = 0, e = block->getNumSuccessors(); i != e;
                     ++i) {
                  mlir::Block *succ = block->getSuccessor(i);
                  if (succ == &entry)
                    return false; // A loop back to the root is not a cleanup.
                  if (domInfo.dominates(&entry, succ)) {
                    if (seen.insert(succ).second)
                      worklist.push_back(succ);
                    continue;
                  }
                  if (!branch)
                    return false;
                  mlir::SuccessorOperands operands =
                      branch.getSuccessorOperands(i);
                  if (operands.size() != 2 ||
                      operands[0] != root.exceptionPtr ||
                      operands[1] != root.typeId)
                    return false;
                  if (continuation && continuation != succ)
                    return false;
                  continuation = succ;
                  if (!llvm::is_contained(exits, block))
                    exits.push_back(block);
                }
              }
              // Exactly one way out: resume, or a continuation, never both.
              return !resumeOp != !continuation;
            };

        // A region leaving through several edges has no single last cut point.
        // Funnel those edges through one block first, so every region has the
        // same shape as the resume case. A funnel that ends up unshared has a
        // single predecessor and step 3.5 folds it back.
        {
          mlir::DominanceInfo funnelDomInfo(func);
          mlir::OpBuilder funnelBuilder(func.getContext());
          llvm::SmallVector<mlir::Block *, 8> roots;
          for (mlir::Block &entry : body.getBlocks())
            roots.push_back(&entry);
          for (mlir::Block *entry : roots) {
            std::optional<CleanupRoot> root = getCleanupRoot(*entry);
            if (!root)
              continue;
            llvm::SmallVector<mlir::Block *, 8> blocks;
            llvm::SmallVector<mlir::Block *, 4> exits;
            mlir::Block *continuation = nullptr;
            cir::ResumeFlatOp resumeOp;
            if (!findCleanupRegion(funnelDomInfo, *entry, *root, blocks, exits,
                                   continuation, resumeOp))
              continue;
            if (exits.size() < 2)
              continue;

            mlir::Location loc = exits.front()->getTerminator()->getLoc();
            mlir::Block *funnel = funnelBuilder.createBlock(&body);
            funnelBuilder.setInsertionPointToEnd(funnel);
            cir::BrOp::create(funnelBuilder, loc, continuation,
                              llvm::SmallVector<mlir::Value, 2>{
                                  root->exceptionPtr, root->typeId});
            for (mlir::Block *exit : exits) {
              auto branch =
                  cast<mlir::BranchOpInterface>(exit->getTerminator());
              for (unsigned i = 0, e = exit->getNumSuccessors(); i != e; ++i) {
                if (exit->getSuccessor(i) != continuation)
                  continue;
                mlir::SuccessorOperands operands =
                    branch.getSuccessorOperands(i);
                operands.erase(0, operands.size());
                exit->getTerminator()->setSuccessor(funnel, i);
              }
            }
          }
        }

        // Dominance is used to find the regions and their cut blocks. Step 3
        // only rewrites the CFG after every region has been collected, so
        // computing it once here is safe.
        mlir::DominanceInfo padDomInfo(func);
        mlir::PostDominanceInfo padPostDomInfo(func);

        auto collectCleanupLandingPad = [&](mlir::Block &entry,
                                            CleanupLandingPad &info) {
          std::optional<CleanupRoot> root = getCleanupRoot(entry);
          if (!root)
            return false;

          llvm::SmallVector<mlir::Block *, 4> exits;
          mlir::Block *continuation = nullptr;
          cir::ResumeFlatOp resumeOp;
          if (!findCleanupRegion(padDomInfo, entry, *root, info.padBlocks,
                                 exits, continuation, resumeOp))
            return false;
          // The funnel pass above left every region with one exiting block.
          if (!resumeOp && exits.size() != 1)
            return false;

          info.block = &entry;
          info.ehOp = root->ehOp;
          info.exceptionPtr = root->exceptionPtr;
          info.typeId = root->typeId;
          info.resumeOp = resumeOp;
          info.continuation = continuation;
          info.exitBlock = resumeOp ? resumeOp->getBlock() : exits.front();

          llvm::SmallPtrSet<mlir::Block *, 8> padSet(info.padBlocks.begin(),
                                                     info.padBlocks.end());
          auto definingBlock = [](mlir::Value value) {
            mlir::Operation *defOp = value.getDefiningOp();
            return defOp ? defOp->getBlock()
                         : cast<mlir::BlockArgument>(value).getOwner();
          };
          auto definedInPad = [&](mlir::Value value) {
            return padSet.contains(definingBlock(value));
          };
          // The exception pair is threaded through as block arguments, so it
          // is available everywhere and never keeps a cut point from forming.
          auto isExceptionValue = [&](mlir::Value value) {
            return value == info.exceptionPtr || value == info.typeId;
          };

          // The pad decomposes into units at its cut blocks: program points
          // where the only values still live are the in-flight exception
          // pair, so a shared block starting there needs no other input.
          // Structurally those are the blocks that dominate the resume and
          // post-dominate the entry -- the boundaries of the pad's
          // single-entry/single-exit segments -- with two live-range filters
          // on top, since a segment boundary is only a cut point if nothing
          // else crosses it.
          mlir::Block *exitBlock = info.exitBlock;
          // Paired with the size of the block's tail, which is what puts the
          // cut blocks in order below.
          llvm::SmallVector<std::pair<mlir::Block *, unsigned>, 4> cutBlocks;
          for (mlir::Block *block : info.padBlocks) {
            // A block taking any argument beyond the exception pair receives
            // something a shared block could not supply. The root is the one
            // exception: a chain head's arguments *are* the exception pair.
            if (block != &entry && !block->getArguments().empty())
              continue;
            if (!padDomInfo.dominates(block, exitBlock) ||
                !padPostDomInfo.postDominates(block, &entry))
              continue;
            // Nothing the pad defined earlier may still be read from here on.
            // A destroy loop's body qualifies structurally but reads the end
            // pointer its setup block computed, so it is rejected here and
            // stays inside its unit -- which is exactly why an array local
            // needs the block-level form of a unit.
            llvm::SmallPtrSet<mlir::Block *, 8> tail{block};
            llvm::SmallVector<mlir::Block *, 8> tailWorklist{block};
            while (!tailWorklist.empty())
              for (mlir::Block *succ :
                   tailWorklist.pop_back_val()->getSuccessors())
                if (padSet.contains(succ) && tail.insert(succ).second)
                  tailWorklist.push_back(succ);
            bool crossed = false;
            for (mlir::Block *tailBlock : tail)
              for (mlir::Operation &op : *tailBlock)
                for (mlir::Value operand : op.getOperands())
                  if (definedInPad(operand) && !isExceptionValue(operand) &&
                      !tail.contains(definingBlock(operand)))
                    crossed = true;
            if (crossed)
              continue;
            cutBlocks.push_back({block, tail.size()});
          }
          // Order them from the entry towards the exit. Dominance is only a
          // partial order, so it cannot be the sort predicate -- an unreachable
          // pad block compares equal to everything and the comparator stops
          // being a strict weak ordering. The tail size is a total order that
          // agrees with dominance on a chain: an earlier cut block reaches
          // everything a later one does, plus itself. If two of them tie the
          // cut blocks are not a chain, so the pad does not decompose.
          llvm::sort(cutBlocks, [](const std::pair<mlir::Block *, unsigned> &lhs,
                                   const std::pair<mlir::Block *, unsigned>
                                       &rhs) {
            return lhs.second > rhs.second;
          });
          for (size_t i = 1, e = cutBlocks.size(); i < e; ++i)
            if (cutBlocks[i - 1].second == cutBlocks[i].second)
              return false;
          if (cutBlocks.empty() || cutBlocks.front().first != &entry ||
              cutBlocks.back().first != exitBlock)
            return false;

          llvm::SmallVector<CutPoint, 8> cutPoints;
          for (const std::pair<mlir::Block *, unsigned> &cut : cutBlocks) {
            mlir::Block *cutBlock = cut.first;
            mlir::Block::iterator start =
                cutBlock == &entry && info.ehOp
                    ? std::next(info.ehOp->getIterator())
                    : cutBlock->begin();
            cutPoints.push_back({cutBlock, start});

            // Subdivide the straight-line prefix of the cut block. A position
            // is a cut point once nothing defined after `start` is still live
            // there: the code before it is then self-contained and can move to
            // a shared block on its own. That covers a lone scalar destructor
            // call, and equally a cast feeding one -- which is the shape a
            // destructor reached through a base-class subobject takes.
            llvm::DenseMap<mlir::Value, unsigned> pendingUses;
            unsigned liveDefs = 0;
            for (mlir::Block::iterator it = start; it != cutBlock->end();
                 ++it) {
              if (it->hasTrait<mlir::OpTrait::IsTerminator>())
                break;
              if (llvm::any_of(it->getRegions(), [](mlir::Region &region) {
                    return !region.empty();
                  }))
                break;
              bool readsAcross = false;
              for (mlir::Value operand : it->getOperands()) {
                auto pending = pendingUses.find(operand);
                if (pending != pendingUses.end()) {
                  if (--pending->second == 0)
                    --liveDefs;
                  continue;
                }
                // Defined in the region but before this prefix: the code here
                // is not self-contained and cannot be split off.
                if (definedInPad(operand) && !isExceptionValue(operand))
                  readsAcross = true;
              }
              if (readsAcross)
                break;
              for (mlir::Value result : it->getResults()) {
                unsigned uses = llvm::range_size(result.getUsers());
                if (!uses)
                  continue;
                pendingUses[result] = uses;
                ++liveDefs;
              }
              // A value used from a later block never drops to zero here, so
              // this also stops the prefix at anything that outlives it.
              if (liveDefs)
                continue;
              // Don't split off a unit holding nothing but a branch.
              mlir::Block::iterator next = std::next(it);
              if (next == cutBlock->end())
                break;
              if (next->hasTrait<mlir::OpTrait::IsTerminator>() &&
                  !isa<cir::ResumeFlatOp>(*next))
                break;
              cutPoints.push_back({cutBlock, next});
            }
          }
          if (cutPoints.size() < 2)
            return false;

          for (unsigned i = 0, e = cutPoints.size() - 1; i != e; ++i) {
            CleanupUnit unit;
            unit.begin = cutPoints[i];
            unit.end = cutPoints[i + 1];
            unit.blocks.push_back(unit.begin.block);
            if (unit.begin.block != unit.end.block) {
              // Everything reachable from the unit's first block without
              // passing through the next cut point belongs to the unit.
              llvm::SmallPtrSet<mlir::Block *, 4> unitSet{unit.begin.block};
              for (unsigned idx = 0; idx != unit.blocks.size(); ++idx)
                for (mlir::Block *succ : unit.blocks[idx]->getSuccessors())
                  if (succ != unit.end.block && unitSet.insert(succ).second)
                    unit.blocks.push_back(succ);
              if (unitSet.contains(unit.end.block))
                return false;
            }
            // A unit must be self-contained: it may read values defined
            // outside the pad, but not values the pad defines elsewhere.
            for (mlir::Block *block : unit.blocks) {
              for (mlir::Operation &op : unitOpRange(unit, block)) {
                for (mlir::Value operand : op.getOperands()) {
                  if (!definedInPad(operand) || isExceptionValue(operand))
                    continue;
                  mlir::Block *defBlock = definingBlock(operand);
                  if (!llvm::is_contained(unit.blocks, defBlock))
                    return false;
                }
              }
            }
            // Scratch storage the cleanup allocates for itself -- an alloca
            // hoisted to the function entry that nothing outside this unit
            // ever touches -- is private, so two units may differ in which
            // slot they use and still be equivalent. Record it now: sharing
            // a unit adds users to the surviving copy's alloca, which would
            // make the property look false on any later comparison.
            unsigned unitOpCount = 0;
            for (mlir::Block *block : unit.blocks)
              unitOpCount += llvm::range_size(unitOpRange(unit, block));
            for (mlir::Block *block : unit.blocks)
              for (mlir::Operation &op : unitOpRange(unit, block))
                for (mlir::Value operand : op.getOperands()) {
                  if (!isa_and_nonnull<cir::AllocaOp>(
                          operand.getDefiningOp()) ||
                      llvm::is_contained(unit.scratch, operand))
                    continue;
                  // The object being destroyed is read from every inline copy
                  // of the cleanup, so it has far more users than any one unit
                  // has ops. Rejecting on the count first keeps this off the
                  // quadratic path: without it, every unit walks that alloca's
                  // whole use list, once per operand mentioning it.
                  if (allocaUserCount(operand) > unitOpCount)
                    continue;
                  if (llvm::all_of(operand.getUsers(),
                                   [&](mlir::Operation *user) {
                                     return unitOwns(unit, user);
                                   }))
                    unit.scratch.push_back(operand);
                }
            // Only properties equivalent units necessarily agree on: the
            // block and op counts, and the sequence of op names.
            unit.fingerprint =
                llvm::hash_combine(unit.blocks.size(), unitOpCount);
            for (mlir::Block *block : unit.blocks)
              for (mlir::Operation &op : unitOpRange(unit, block))
                unit.fingerprint =
                    llvm::hash_combine(unit.fingerprint, op.getName());
            info.units.push_back(std::move(unit));
          }
          return !info.units.empty();
        };

        llvm::SmallVector<CleanupLandingPad, 8> landingPadInfos;
        // Rewriting a region erases its blocks, so no block may belong to two
        // of them. Regions are private, but a landing pad could in principle
        // contain a chain head of its own; first claim wins.
        llvm::DenseSet<mlir::Block *> claimedBlocks;
        for (mlir::Block &block : body.getBlocks()) {
          CleanupLandingPad info{};
          if (!collectCleanupLandingPad(block, info))
            continue;
          if (llvm::any_of(info.padBlocks, [&](mlir::Block *padBlock) {
                return claimedBlocks.contains(padBlock);
              }))
            continue;
          claimedBlocks.insert(info.padBlocks.begin(), info.padBlocks.end());
          landingPadInfos.push_back(std::move(info));
        }

        llvm::SmallVector<bool, 8> sharesCleanupSuffix(landingPadInfos.size(),
                                                       false);
        for (unsigned lhsIdx = 0; lhsIdx != landingPadInfos.size(); ++lhsIdx) {
          for (unsigned rhsIdx = lhsIdx + 1; rhsIdx != landingPadInfos.size();
               ++rhsIdx) {
            // Both already known to share with something: comparing them again
            // cannot change the outcome, and the search below is the pass's
            // dominant cost on a function with many unwind edges.
            if (sharesCleanupSuffix[lhsIdx] && sharesCleanupSuffix[rhsIdx])
              continue;
            // A suffix pair can only match if both sides have the same number
            // of units left, so the start on one side determines the other.
            // Longest first.
            unsigned lhsUnits = landingPadInfos[lhsIdx].units.size();
            unsigned rhsUnits = landingPadInfos[rhsIdx].units.size();
            unsigned longest = std::min(lhsUnits, rhsUnits);
            for (unsigned length = longest; length != 0; --length) {
              if (!suffixesEquivalent(
                      landingPadInfos[lhsIdx], lhsUnits - length,
                      landingPadInfos[rhsIdx], rhsUnits - length))
                continue;
              sharesCleanupSuffix[lhsIdx] = true;
              sharesCleanupSuffix[rhsIdx] = true;
              break;
            }
          }
        }

        if (llvm::is_contained(sharesCleanupSuffix, true)) {
          mlir::OpBuilder builder(func.getContext());
          llvm::SmallVector<CleanupSuffixNode, 8> suffixNodes;
          llvm::SmallVector<mlir::Block *, 8> suffixEntryForInfo(
              landingPadInfos.size(), nullptr);
          llvm::DenseMap<std::tuple<mlir::Type, mlir::Type, mlir::Block *>,
                         mlir::Block *>
              sharedTailBlocks;

          auto padLoc = [](CleanupLandingPad &pad) {
            return pad.exitBlock->getTerminator()->getLoc();
          };

          // The tail of every shared chain: a block that leaves the way the
          // regions feeding it left -- resuming the exception pair it is
          // handed, or passing it on to their common continuation.
          auto getSharedTailBlock = [&](CleanupLandingPad &pad) {
            std::tuple<mlir::Type, mlir::Type, mlir::Block *> key{
                pad.exceptionPtr.getType(), pad.typeId.getType(),
                pad.continuation};
            mlir::Block *&tailBlock = sharedTailBlocks[key];
            if (!tailBlock) {
              mlir::Location loc = padLoc(pad);
              tailBlock = builder.createBlock(&body);
              tailBlock->addArgument(std::get<0>(key), loc);
              tailBlock->addArgument(std::get<1>(key), loc);
              builder.setInsertionPointToEnd(tailBlock);
              llvm::SmallVector<mlir::Value, 2> args(tailBlock->getArguments());
              if (pad.continuation)
                cir::BrOp::create(builder, loc, pad.continuation, args);
              else
                cir::ResumeFlatOp::create(builder, loc, args[0], args[1]);
            }
            return tailBlock;
          };

          auto buildCleanupSuffix = [&](auto &&self, unsigned infoIdx,
                                        unsigned unitIdx) -> mlir::Block * {
            CleanupLandingPad &pad = landingPadInfos[infoIdx];
            for (const CleanupSuffixNode &node : suffixNodes)
              if (suffixesEquivalent(pad, unitIdx,
                                     landingPadInfos[node.infoIdx],
                                     node.unitIdx))
                return node.block;

            const CleanupUnit &unit = pad.units[unitIdx];
            bool isLastUnit = unitIdx + 1 == pad.units.size();

            mlir::Location loc = padLoc(pad);
            auto *suffixBlock = builder.createBlock(&body);
            suffixBlock->addArgument(pad.exceptionPtr.getType(), loc);
            suffixBlock->addArgument(pad.typeId.getType(), loc);
            suffixNodes.push_back({infoIdx, unitIdx, suffixBlock});

            // An op-only unit carries no terminator of its own, so the
            // continuation is spelled out here; otherwise the unit's own exit
            // edges are retargeted at it below.
            bool unitIsOpOnly = unit.begin.block == unit.end.block;
            mlir::Block *continuation =
                isLastUnit && unitIsOpOnly && !pad.continuation
                    ? nullptr
                    : (isLastUnit ? getSharedTailBlock(pad)
                                  : self(self, infoIdx, unitIdx + 1));

            mlir::IRMapping mapping;
            mapping.map(unit.blocks.front(), suffixBlock);
            // Inside a shared block the exception pair is the block's own
            // arguments, whatever the region it came from called them.
            mapping.map(pad.exceptionPtr, suffixBlock->getArgument(0));
            mapping.map(pad.typeId, suffixBlock->getArgument(1));
            for (mlir::Block *block : llvm::drop_begin(unit.blocks)) {
              mlir::Block *cloned = builder.createBlock(&body);
              for (mlir::BlockArgument arg : block->getArguments())
                mapping.map(arg,
                            cloned->addArgument(arg.getType(), arg.getLoc()));
              mapping.map(block, cloned);
            }
            // An op-only unit ends in the middle of its own block, so there is
            // no exit edge to redirect -- and end.block is begin.block, which
            // is already mapped to the suffix block.
            if (continuation && !unitIsOpOnly)
              mapping.map(unit.end.block, continuation);

            for (mlir::Block *block : unit.blocks) {
              builder.setInsertionPointToEnd(mapping.lookup(block));
              for (mlir::Operation &op : unitOpRange(unit, block))
                builder.clone(op, mapping);
            }

            llvm::SmallVector<mlir::Value, 2> exceptionArgs(
                suffixBlock->getArguments());
            if (unitIsOpOnly) {
              builder.setInsertionPointToEnd(suffixBlock);
              if (!continuation)
                cir::ResumeFlatOp::create(builder, loc, exceptionArgs[0],
                                          exceptionArgs[1]);
              else
                cir::BrOp::create(builder, loc, continuation, exceptionArgs);
            } else {
              // Hand the exception pair to the continuation along every edge
              // that leaves the unit. suffixBlock dominates the whole unit, so
              // its arguments are in scope at each of them.
              for (mlir::Block *block : unit.blocks) {
                auto branch = dyn_cast<mlir::BranchOpInterface>(
                    mapping.lookup(block)->getTerminator());
                if (!branch)
                  continue;
                for (unsigned i = 0, e = branch->getNumSuccessors(); i != e;
                     ++i)
                  if (branch->getSuccessor(i) == continuation)
                    branch.getSuccessorOperands(i).append(exceptionArgs);
              }
            }

            return suffixBlock;
          };

          for (unsigned infoIdx = 0; infoIdx != landingPadInfos.size();
               ++infoIdx) {
            if (!sharesCleanupSuffix[infoIdx])
              continue;
            suffixEntryForInfo[infoIdx] =
                buildCleanupSuffix(buildCleanupSuffix, infoIdx, 0);
          }

          for (unsigned infoIdx = 0; infoIdx != landingPadInfos.size();
               ++infoIdx) {
            if (!sharesCleanupSuffix[infoIdx])
              continue;
            CleanupLandingPad &pad = landingPadInfos[infoIdx];

            // A pad keeps its inflight op and jumps on from there; a chain
            // head keeps nothing but its arguments.
            auto insertPt = pad.ehOp ? std::next(pad.ehOp->getIterator())
                                     : pad.block->begin();
            builder.setInsertionPoint(pad.block, insertPt);
            auto brOp = cir::BrOp::create(builder, padLoc(pad),
                                          suffixEntryForInfo[infoIdx],
                                          llvm::SmallVector<mlir::Value, 2>{
                                              pad.exceptionPtr, pad.typeId});

            // The root is now just "catch the exception, jump to the shared
            // chain"; drop the copy of the cleanup code it used to hold.
            llvm::SmallVector<mlir::Operation *, 16> deadOps;
            for (mlir::Operation &op : llvm::make_range(
                     std::next(brOp->getIterator()), pad.block->end()))
              deadOps.push_back(&op);
            for (mlir::Block *block : pad.padBlocks) {
              if (block == pad.block)
                continue;
              for (mlir::Operation &op : *block)
                deadOps.push_back(&op);
            }
            for (mlir::Operation *op : deadOps)
              op->dropAllReferences();
            for (mlir::Operation *op : deadOps)
              op->dropAllUses();
            for (mlir::Operation *op : llvm::reverse(deadOps))
              op->erase();
            for (mlir::Block *block : pad.padBlocks) {
              if (block == pad.block)
                continue;
              block->dropAllDefinedValueUses();
              block->dropAllUses();
              block->erase();
            }
          }
        }
      }

      // Step 3.4: Deduplicate whole cleanup landing pads. Step 3 shares the
      // cleanup *code*, but leaves every unwind edge its own pad branching
      // into the shared chain. Each surviving pad becomes a separate
      // `landingpad` in LLVM IR, and the LSDA gets one call-site entry per
      // pad; OGCG instead points consecutive invokes at a single pad, so its
      // call-site table merges them into one range. Pads that forward the
      // exception pair to the same shared block do identical work, so keep
      // one and repoint the rest at it.
      {
        llvm::DenseMap<mlir::Block *, mlir::Block *> canonicalCleanupPad;
        llvm::SmallVector<std::pair<mlir::Block *, mlir::Block *>, 4>
            duplicateCleanupPads;

        for (mlir::Block &block : body.getBlocks()) {
          // Match "catch the exception, jump to the shared chain":
          //   eh.inflight_exception cleanup
          //   cir.br ^shared(%exn, %tid)
          if (!block.getArguments().empty() || block.empty())
            continue;
          auto ehOp = dyn_cast<cir::EhInflightOp>(&block.front());
          if (!ehOp || !ehOp.getCleanup() || ehOp.getCatchAll() ||
              ehOp.getCatchTypeListAttr() || ehOp.getFilterTypeListAttr())
            continue;
          if (!block.mightHaveTerminator())
            continue;
          auto brOp = dyn_cast<cir::BrOp>(block.getTerminator());
          if (!brOp || &*std::next(ehOp->getIterator()) != brOp.getOperation())
            continue;
          // Nothing but the exception pair may be handed on: anything else
          // could differ between two pads that look alike here.
          if (brOp.getDestOperands().size() != 2 ||
              brOp.getDestOperands()[0] != ehOp.getExceptionPtr() ||
              brOp.getDestOperands()[1] != ehOp.getTypeId())
            continue;

          mlir::Block *&canonical = canonicalCleanupPad[brOp.getDest()];
          if (!canonical)
            canonical = &block;
          else
            duplicateCleanupPads.push_back({&block, canonical});
        }

        for (auto [dup, canonical] : duplicateCleanupPads) {
          for (mlir::BlockOperand &use :
               llvm::make_early_inc_range(dup->getUses()))
            use.set(canonical);
          dup->dropAllDefinedValueUses();
          dup->dropAllUses();
          dup->erase();
        }
      }

      // Step 3.5: Fold any now-unique cleanup suffix heads back into their
      // landing-pad predecessors. Shared suffix blocks keep multiple preds and
      // remain separate; unique heads should collapse back to match OGCG.
      mergeSinglePredecessorForwardingBlocks();

      // Step 4: Promote the single-store/single-load allocas CIRGen used to
      // carry a value across a region boundary -- a call result out of a
      // cir.try body, or a value out of a cir.cleanup.scope one. Now that the
      // regions are flat the store and load are typically in the same block,
      // so the load can read the stored value directly and both ops and the
      // alloca can go.
      mlir::DominanceInfo domInfo(func);
      llvm::SmallVector<cir::AllocaOp, 4> allocasToPromote;
      func.walk([&](cir::AllocaOp alloca) {
        if (alloca.getName() == "tmp.try.call.res" ||
            alloca.getName() == "tmp.cleanup.scope.res")
          allocasToPromote.push_back(alloca);
      });

      for (cir::AllocaOp alloca : allocasToPromote) {
        cir::StoreOp singleStore = nullptr;
        cir::LoadOp singleLoad = nullptr;
        bool canPromote = true;

        for (mlir::OpOperand &use : alloca.getResult().getUses()) {
          mlir::Operation *user = use.getOwner();
          if (auto store = dyn_cast<cir::StoreOp>(user)) {
            if (store.getAddr() == alloca.getResult()) {
              if (singleStore) {
                canPromote = false;
                break;
              }
              singleStore = store;
            } else {
              canPromote = false;
              break;
            }
          } else if (auto load = dyn_cast<cir::LoadOp>(user)) {
            if (singleLoad) {
              canPromote = false;
              break;
            }
            singleLoad = load;
          } else {
            canPromote = false;
            break;
          }
        }

        if (!canPromote || !singleStore || !singleLoad)
          continue;

        // Verify the store dominates the load.
        if (!domInfo.dominates(singleStore.getOperation(),
                               singleLoad.getOperation()))
          continue;

        // Forward the stored value to the load.
        singleLoad.getResult().replaceAllUsesWith(singleStore.getValue());
        singleLoad.erase();
        singleStore.erase();
        alloca.erase();
      }
    });
  }

  // Post-flattening dead block elimination: after flattening structured
  // regions (scope, try, if, etc.), continuation blocks may become
  // unreachable when all paths through the region terminate via return,
  // throw, or unreachable. Remove these dead blocks to avoid spurious
  // trap/unreachable blocks in the lowered LLVM IR.
  forEachFunction([](cir::FuncOp func) {
    if (func.isDeclaration())
      return;
    mlir::Region &body = func.getBody();
    if (body.empty())
      return;

    // Mark reachable blocks from entry via depth-first traversal.
    llvm::df_iterator_default_set<mlir::Block *, 16> reachable;
    for (mlir::Block *block :
         llvm::depth_first_ext(&body.front(), reachable))
      (void)block;

    // Also mark blocks reachable from label blocks. Label blocks are
    // goto targets that have no CFG predecessors yet (cir.goto uses
    // string names, not block references). GotoSolver wires them up
    // in a subsequent pass, so we must preserve them and everything
    // reachable from them.
    // Similarly, preserve blocks containing cir.indirect_br — the
    // indirect-branch dispatch block may have no CFG predecessors yet
    // (cir.indirect_goto is resolved by GotoSolver).
    // Also preserve blocks that are fallthrough targets of
    // cir.asm.goto — before GotoSolver, asm goto ops have no
    // successor blocks (they use string label names), so their
    // fallthrough block appears to have no predecessors.
    for (mlir::Block &block : body.getBlocks()) {
      if (reachable.count(&block))
        continue;
      bool preserve = false;
      // Preserve blocks containing labels, indirect branches, or dead roots
      // that must survive until later lowering.
      if (llvm::any_of(block.getOperations(), [](mlir::Operation &op) {
            return isa<cir::LabelOp, cir::IndirectBrOp>(op) ||
                   op.hasAttr(kPreserveDeadObjCCatchBlockAttr) ||
                   op.hasAttr(kPreserveDeadUBSanUnreachableBlockAttr);
          }))
        preserve = true;
      // Preserve blocks that are the fallthrough of an asm goto.
      // The previous block's terminator is an InlineAsmGotoOp and
      // this block immediately follows it in the region.
      if (!preserve) {
        if (mlir::Block *prev = block.getPrevNode()) {
          if (prev->getTerminator() &&
              isa<cir::InlineAsmGotoOp>(prev->getTerminator()))
            preserve = true;
        }
      }
      if (preserve) {
        for (mlir::Block *b :
             llvm::depth_first_ext(&block, reachable))
          (void)b;
      }
    }

    // Erase unreachable blocks. Be careful not to erase blocks whose
    // values are used by reachable blocks — this can happen when
    // coroutine/EH flattening creates cleanup blocks that lose their
    // predecessor edges but still define values used downstream.
    // Such blocks must be preserved (and marked reachable) along with
    // everything reachable from them.
    bool changed = true;
    while (changed) {
      changed = false;
      for (mlir::Block &block : body.getBlocks()) {
        if (reachable.count(&block))
          continue;
        // Check if any value defined in this block is used by a
        // reachable block.
        bool hasReachableUser = false;
        for (mlir::Operation &op : block.getOperations()) {
          for (mlir::Value result : op.getResults()) {
            for (mlir::OpOperand &use : result.getUses()) {
              if (reachable.count(use.getOwner()->getBlock())) {
                hasReachableUser = true;
                break;
              }
            }
            if (hasReachableUser)
              break;
          }
          if (hasReachableUser)
            break;
        }
        if (hasReachableUser) {
          // Preserve this block and everything reachable from it.
          for (mlir::Block *b :
               llvm::depth_first_ext(&block, reachable))
            (void)b;
          changed = true;
        }
      }
    }

    for (mlir::Block &block :
         llvm::make_early_inc_range(body.getBlocks())) {
      if (reachable.count(&block))
        continue;
      block.dropAllDefinedValueUses();
      block.dropAllUses();
      block.erase();
    }
  });

  forEachFunction([](cir::FuncOp fn) { unifyRetvalMultiReturns(fn); });
}

} // namespace

namespace mlir {

std::unique_ptr<Pass> createCIRFlattenCFGPass() {
  return std::make_unique<CIRFlattenCFGPass>();
}

} // namespace mlir
