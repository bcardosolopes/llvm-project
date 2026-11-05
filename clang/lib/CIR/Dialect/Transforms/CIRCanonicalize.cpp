//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements pass that canonicalizes CIR operations, eliminating
// redundant branches, empty scopes, and other unnecessary operations.
//
//===----------------------------------------------------------------------===//

#include "PassDetail.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/Region.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/MissingFeatures.h"

using namespace mlir;
using namespace cir;

namespace mlir {
#define GEN_PASS_DEF_CIRCANONICALIZE
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

namespace {

static bool containsNestedOpenMPOps(cir::ScopeOp scopeOp) {
  return scopeOp->walk([&](Operation *nestedOp) {
           if (nestedOp != scopeOp.getOperation() && nestedOp->getDialect() &&
               isa<omp::OpenMPDialect>(nestedOp->getDialect()))
             return WalkResult::interrupt();
           return WalkResult::advance();
         }).wasInterrupted();
}

/// Removes branches between two blocks if it is the only branch.
///
/// From:
///   ^bb0:
///     cir.br ^bb1
///   ^bb1:  // pred: ^bb0
///     cir.return
///
/// To:
///   ^bb0:
///     cir.return
struct RemoveRedundantBranches : public OpRewritePattern<BrOp> {
  using OpRewritePattern<BrOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(BrOp op,
                                PatternRewriter &rewriter) const final {
    Block *block = op.getOperation()->getBlock();
    Block *dest = op.getDest();

    if (isa<cir::LabelOp, cir::IndirectBrOp>(dest->front()))
      return failure();
    // Single edge between blocks: merge it. The branch's operands take the
    // place of the destination's arguments, which flat CIR after mem2reg has.
    if (block->getNumSuccessors() == 1 && dest != block &&
        dest->getSinglePredecessor() == block) {
      llvm::SmallVector<mlir::Value> destArgs(op.getDestOperands());
      rewriter.eraseOp(op);
      rewriter.mergeBlocks(dest, block, destArgs);
      return success();
    }

    return failure();
  }
};

//===----------------------------------------------------------------------===//
// CIRCanonicalizePass
//===----------------------------------------------------------------------===//

struct CIRCanonicalizePass
    : public impl::CIRCanonicalizeBase<CIRCanonicalizePass> {
  using CIRCanonicalizeBase::CIRCanonicalizeBase;

  // The same operation rewriting done here could have been performed
  // by CanonicalizerPass (adding hasCanonicalizer for target Ops and
  // implementing the same from above in CIRDialects.cpp). However, it's
  // currently too aggressive for static analysis purposes, since it might
  // remove things where a diagnostic can be generated.
  //
  // FIXME: perhaps we can add one more mode to GreedyRewriteConfig to
  // disable this behavior.
  void runOnOperation() override;
};

void populateCIRCanonicalizePatterns(RewritePatternSet &patterns) {
  // clang-format off
  patterns.add<
    RemoveRedundantBranches
  >(patterns.getContext());
  // clang-format on
}

void CIRCanonicalizePass::runOnOperation() {
  // Collect rewrite patterns.
  RewritePatternSet patterns(&getContext());
  populateCIRCanonicalizePatterns(patterns);

  // Collect operations to apply patterns.
  llvm::SmallVector<Operation *, 16> ops;
  getOperation()->walk([&](Operation *op) {
    assert(!cir::MissingFeatures::callOp());

    // Many operations are here to perform a manual `fold` in
    // applyOpPatternsGreedily.
    if (auto castOp = dyn_cast<CastOp>(op)) {
      // Keep discarded floating-point classification/comparison builtins alive
      // in O0 codegen parity paths: these lower to cir.cmp/cir.is_fp_class
      // followed by bool_to_int whose result is intentionally unused.
      // Running canonicalization roots from such bool_to_int casts lets the
      // greedy driver erase the entire chain as trivially dead.
      if (castOp.getKind() == CastKind::bool_to_int &&
          castOp.getResult().use_empty() &&
          isa_and_present<CmpOp, IsFPClassOp, SignBitOp>(
              castOp.getSrc().getDefiningOp()))
        return;
      ops.push_back(op);
      return;
    }

    if (auto selectOp = dyn_cast<SelectOp>(op)) {
      // Keep discarded select chains alive for builtin lowering patterns that
      // intentionally materialize integer results from FP classification/sign
      // predicates (e.g. __builtin_isinf_sign). Canonicalization roots from
      // the dead select result would otherwise erase the full chain.
      if (selectOp->getNumResults() == 1 && selectOp.getResult().use_empty() &&
          isa_and_present<IsFPClassOp, SignBitOp>(
              selectOp.getCondition().getDefiningOp()))
        return;
      ops.push_back(op);
      return;
    }

    if (auto scopeOp = dyn_cast<ScopeOp>(op)) {
      // Preserve empty OpenMP constructs that carry launch semantics, such as
      // omp.parallel if/num_threads nested under omp.target host_eval(...).
      // Rooting greedy canonicalization on the surrounding cir.scope lets MLIR
      // erase the empty OpenMP op as trivially dead, which changes target
      // launch width and host fallback behavior.
      if (!containsNestedOpenMPOps(scopeOp))
        ops.push_back(op);
      return;
    }

    if (isa<BrOp, BrCondOp, SwitchOp, UnaryOp, ComplexCreateOp,
            ComplexImagOp, ComplexRealOp, VecCmpOp, VecCreateOp, VecExtractOp,
            VecShuffleOp, VecShuffleDynamicOp, VecTernaryOp, BitClrsbOp,
            BitClzOp, BitCtzOp, BitFfsOp, BitParityOp, BitPopcountOp,
            BitReverseOp, ByteSwapOp, RotateOp, ConstantOp>(op))
      ops.push_back(op);
  });

  // Apply patterns.
  if (applyOpPatternsGreedily(ops, std::move(patterns)).failed())
    signalPassFailure();
}

} // namespace

std::unique_ptr<Pass> mlir::createCIRCanonicalizePass() {
  return std::make_unique<CIRCanonicalizePass>();
}
