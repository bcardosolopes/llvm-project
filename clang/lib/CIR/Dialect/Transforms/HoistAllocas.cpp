//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PassDetail.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/LowerToLLVM.h"
#include "llvm/Support/TimeProfiler.h"

using namespace mlir;
using namespace cir;

namespace mlir {
#define GEN_PASS_DEF_HOISTALLOCAS
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

namespace {

struct HoistAllocasPass : public impl::HoistAllocasBase<HoistAllocasPass> {

  HoistAllocasPass() = default;
  void runOnOperation() override;
};

static bool isOpInLoop(mlir::Operation *op) {
  return op->getParentOfType<cir::LoopOpInterface>();
}

static bool hasStoreToAllocaInLoopCond(cir::AllocaOp alloca) {
  // This function determines whether the given alloca operation represents
  // a variable defined as a while or for loop's condition.
  //
  // Specifically, C/C++ allows the condition of a while or for loop be a
  // variable declaration:
  //
  //   while (const int x = foo()) { /* body... */ }
  //   for (; const int x = foo(); ) { /* body... */ }
  //
  // CIRGen would emit the following CIR for the above code:
  //
  //   cir.scope {
  //     %x.slot = cir.alloca !s32i [init, const]
  //     cir.while {
  //       %0 = cir.call @foo()
  //       cir.store %0, %x
  //       %1 = cir.load %x
  //       %2 = cir.cast int_to_bool %1
  //       cir.condition(%2)
  //     } do {
  //       // loop body goes here.
  //     }
  //   }
  //
  // Note that %x.slot is emitted outside the cir.while operation. Ideally, the
  // cir.while operation should cover this cir.alloca operation, but currently
  // CIR does not work this way. When hoisting such an alloca operation, one
  // must remove the "const" flag from it, otherwise LLVM lowering code will
  // mistakenly attach invariant group metadata to the load and store operations
  // in the while body, indicating that all loads and stores across all
  // iterations of the loop are constant.

  for (mlir::Operation *user : alloca->getUsers()) {
    if (!mlir::isa<cir::StoreOp>(user))
      continue;

    auto store = mlir::cast<cir::StoreOp>(user);
    mlir::Operation *storeParentOp = store->getParentOp();

    if (auto whileOp = mlir::dyn_cast<cir::WhileOp>(storeParentOp))
      return &whileOp.getCond() == store->getParentRegion();

    if (auto forOp = mlir::dyn_cast<cir::ForOp>(storeParentOp))
      return &forOp.getCond() == store->getParentRegion();
  }

  return false;
}

static bool crossesIsolationBoundary(cir::AllocaOp alloca, cir::FuncOp func) {
  auto *isolatedParent =
      alloca->getParentWithTrait<mlir::OpTrait::IsIsolatedFromAbove>();
  return isolatedParent && isolatedParent != func.getOperation();
}

static bool crossesOpenMPRegionBoundary(cir::AllocaOp alloca, cir::FuncOp func) {
  for (mlir::Operation *parent = alloca->getParentOp();
       parent && parent != func.getOperation(); parent = parent->getParentOp())
    if (parent->getDialect() &&
        mlir::isa<omp::OpenMPDialect>(parent->getDialect()))
      return true;
  return false;
}

static void processConstAlloca(cir::AllocaOp alloca,
                               bool hasUnstructuredControlFlow) {
  // When optimization is enabled, LLVM lowering would start emitting invariant
  // group metadata for loads and stores to alloca-ed objects with "const"
  // attribute. For alloca operations inside loops, we need to use
  // cir.invariant_group to refresh invariant information on each iteration.

  if (isOpInLoop(alloca)) {
    // With goto/label style control flow, FlattenCFG/GotoSolver can
    // build join blocks not dominated by the loop path where this
    // invariant_group would be materialized. Keeping "constant" here would
    // later produce invalid SSA (non-dominating invariant_group uses). Fall
    // back to non-constant semantics for correctness.
    if (hasUnstructuredControlFlow) {
      alloca.setConstant(false);
      return;
    }

    // A loop-local alloca is re-initialized with a (possibly different) value
    // on every iteration, so its storage is NOT invariant across the pointer's
    // lifetime. Drop the "const" flag (like the loop-condition case below and
    // unoptimized builds) instead of trying to "refresh" the invariant.
    //
    // This branch previously materialized a cir.invariant_group, but that lowers
    // to llvm.launder.invariant.group + !invariant.group and reroutes the
    // initializing store and every read through the laundered pointer. Combined
    // with debug-info-pinned by-value argument temporaries, SROA/EarlyCSE then
    // fold the per-iteration reads to undef -> 0 (observed as the Hermes
    // DictPropertyMap::grow rehash miscompile: every re-inserted SymbolID key
    // collapsed to 0 and properties were silently dropped, crashing the app).
    // OGCG never emits invariant.group for local/parameter allocas; match that.
    alloca.setConstant(false);
    return;
  } else if (hasStoreToAllocaInLoopCond(alloca)) {
    // The alloca represents a variable declared as the condition of a while
    // or for loop. We have to remove the constant flag during hoisting,
    // otherwise we would be telling the optimizer that the alloca-ed value
    // is constant across all iterations of the loop.
    alloca.setConstant(false);
  }
}

static void process(mlir::ModuleOp mod, cir::FuncOp func) {
  if (func.getRegion().empty())
    return;

  // Hoist all static allocas to the entry block.
  mlir::Block &entryBlock = func.getRegion().front();
  llvm::SmallVector<cir::AllocaOp> allocas;
  func.getBody().walk([&](cir::AllocaOp alloca) {
    if (alloca->getBlock() == &entryBlock)
      return;
    // Keep allocas inside outlined or otherwise isolated regions. Hoisting
    // them to the function entry would create cross-region references.
    if (crossesIsolationBoundary(alloca, func))
      return;
    // Keep allocas nested under OpenMP ops in place. Hoisting them to the
    // enclosing function entry changes them from region-local storage into
    // outer captures once the OpenMP region is outlined.
    if (crossesOpenMPRegionBoundary(alloca, func))
      return;
    // Don't hoist allocas with dynamic alloca size.
    if (alloca.getDynAllocSize())
      return;
    allocas.push_back(alloca);
  });
  if (allocas.empty())
    return;

  bool hasUnstructuredControlFlow = false;
  func.walk([&](mlir::Operation *op) {
    if (mlir::isa<cir::GotoOp, cir::LabelOp, cir::IndirectGotoOp,
                  cir::IndirectBrOp, cir::InlineAsmGotoOp>(op)) {
      hasUnstructuredControlFlow = true;
      return mlir::WalkResult::interrupt();
    }
    return mlir::WalkResult::advance();
  });

  mlir::Operation *insertPoint = &*entryBlock.begin();
  auto optInfoAttr = mlir::cast_if_present<cir::OptInfoAttr>(
      mod->getAttr(cir::CIRDialect::getOptInfoAttrName()));
  unsigned optLevel = optInfoAttr ? optInfoAttr.getLevel() : 0;

  for (auto alloca : allocas) {
    if (auto module = alloca->getParentOfType<mlir::ModuleOp>())
      cir::direct::noteNestedSourceDebugAlloca(module, alloca);

    if (alloca.getConstant()) {
      if (optLevel == 0) {
        // Under non-optimized builds, just remove the constant flag.
        alloca.setConstant(false);
      } else {
        processConstAlloca(alloca, hasUnstructuredControlFlow);
      }
    }

    alloca->moveBefore(insertPoint);
  }
}

void HoistAllocasPass::runOnOperation() {
  llvm::TimeTraceScope scope("Hoist Allocas");
  llvm::SmallVector<Operation *, 16> ops;

  Operation *op = getOperation();
  auto mod = mlir::dyn_cast<mlir::ModuleOp>(op);
  if (!mod)
    mod = op->getParentOfType<mlir::ModuleOp>();

  // If we ever introduce nested cir.function ops, we'll need to make this
  // walk in post-order and recurse into nested functions.
  getOperation()->walk<mlir::WalkOrder::PreOrder>([&](cir::FuncOp op) {
    process(mod, op);
    return mlir::WalkResult::skip();
  });
}

} // namespace

std::unique_ptr<Pass> mlir::createHoistAllocasPass() {
  return std::make_unique<HoistAllocasPass>();
}
