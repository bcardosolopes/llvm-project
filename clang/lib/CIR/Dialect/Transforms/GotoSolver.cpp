//====- GotoSolver.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "PassDetail.h"
#include "mlir/IR/Threading.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRStage.h"
#include "clang/CIR/Dialect/Passes.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Support/TimeProfiler.h"
#include <memory>

using namespace mlir;
using namespace cir;

namespace mlir {
#define GEN_PASS_DEF_GOTOSOLVER
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

namespace {

struct GotoSolverPass : public impl::GotoSolverBase<GotoSolverPass> {
  GotoSolverPass() = default;
  void runOnOperation() override;
};

/// Map from function name → set of block address labels referenced in global
/// initializers (e.g., jump tables like `void *labels[] = {&&a, &&b}`).
using GlobalBlockAddrMap = llvm::StringMap<llvm::SmallSet<StringRef, 4>>;

static void collectBlockAddrInfosFromAttr(
    mlir::Attribute rootAttr,
    llvm::function_ref<void(cir::BlockAddrInfoAttr)> collectBlockAddrInfo) {
  llvm::SmallVector<mlir::Attribute, 8> worklist;
  worklist.push_back(rootAttr);
  while (!worklist.empty()) {
    mlir::Attribute attr = worklist.pop_back_val();
    if (auto ba = dyn_cast<cir::BlockAddrAttr>(attr)) {
      collectBlockAddrInfo(ba.getInfo());
    } else if (auto bad = dyn_cast<cir::BlockAddrDiffAttr>(attr)) {
      collectBlockAddrInfo(bad.getLhs());
      collectBlockAddrInfo(bad.getRhs());
    } else if (auto ca = dyn_cast<cir::ConstArrayAttr>(attr)) {
      if (auto elts = dyn_cast_if_present<mlir::ArrayAttr>(ca.getElts())) {
        for (mlir::Attribute elt : elts)
          worklist.push_back(elt);
      }
    } else if (auto cr = dyn_cast<cir::ConstRecordAttr>(attr)) {
      for (mlir::Attribute member : cr.getMembers())
        worklist.push_back(member);
    }
  }
}

/// Pre-build a map of all block address labels referenced in global
/// initializers. This is done once at the module level to avoid an O(F*G)
/// quadratic scan (walking all globals for every function).
static GlobalBlockAddrMap buildGlobalBlockAddrMap(mlir::ModuleOp moduleOp) {
  GlobalBlockAddrMap result;
  // No global is nested in a function, so the walk skips function bodies.
  moduleOp.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation *op) {
    if (isa<cir::FuncOp>(op))
      return mlir::WalkResult::skip();
    if (auto globalOp = dyn_cast<cir::GlobalOp>(op))
      if (auto init = globalOp.getInitialValue())
        collectBlockAddrInfosFromAttr(*init, [&](cir::BlockAddrInfoAttr info) {
          result[info.getFunc().getValue()].insert(info.getLabel());
        });
    return mlir::WalkResult::advance();
  });
  return result;
}

static mlir::LogicalResult process(cir::FuncOp func,
                                   const GlobalBlockAddrMap &globalBlockAddrs) {
  mlir::OpBuilder rewriter(func.getContext());
  llvm::StringMap<Block *> labels;
  llvm::SmallVector<cir::GotoOp, 4> gotos;
  llvm::SmallVector<cir::IndirectGotoOp, 4> indirectGotos;
  llvm::SmallSet<StringRef, 4> blockAddrLabel;
  llvm::SmallSet<std::string, 4> preserveLabels;
  llvm::SmallVector<cir::IndirectBrOp, 2> indirectBrs;
  llvm::SmallVector<cir::InlineAsmGotoOp, 2> asmGotos;

  // Collect all ops of interest first, then split blocks at labels in a
  // separate pass. Splitting blocks during the walk invalidates the walk's
  // block/operation iterators (ilist_iterator sentinel crash).
  llvm::SmallVector<cir::LabelOp, 4> labelOps;
  func.getBody().walk([&](mlir::Operation *op) {
    if (auto lab = dyn_cast<cir::LabelOp>(op)) {
      labelOps.push_back(lab);
    } else if (auto goTo = dyn_cast<cir::GotoOp>(op)) {
      gotos.push_back(goTo);
    } else if (auto indGoTo = dyn_cast<cir::IndirectGotoOp>(op)) {
      indirectGotos.push_back(indGoTo);
    } else if (auto blockAddr = dyn_cast<cir::BlockAddressOp>(op)) {
      blockAddrLabel.insert(blockAddr.getBlockAddrInfo().getLabel());
    } else if (auto constant = dyn_cast<cir::ConstantOp>(op)) {
      collectBlockAddrInfosFromAttr(
          constant.getValue(), [&](cir::BlockAddrInfoAttr info) {
            if (info.getFunc().getValue() == func.getSymName())
              blockAddrLabel.insert(info.getLabel());
          });
    } else if (auto indBr = dyn_cast<cir::IndirectBrOp>(op)) {
      indirectBrs.push_back(indBr);
    } else if (auto asmGoto = dyn_cast<cir::InlineAsmGotoOp>(op)) {
      asmGotos.push_back(asmGoto);
    }
  });

  // Now split blocks at labels. This must be done outside the walk to avoid
  // invalidating block/operation iterators. When a block is split, the
  // predecessor block needs a branch to the new block to maintain well-formed IR.
  for (auto lab : labelOps) {
    mlir::Block *block = lab->getBlock();
    if (&block->front() != lab.getOperation()) {
      mlir::Block *predBlock = block;
      block = predBlock->splitBlock(lab);
      // Add a branch from the predecessor block to the new label block.
      mlir::OpBuilder::InsertionGuard guard(rewriter);
      rewriter.setInsertionPointToEnd(predBlock);
      cir::BrOp::create(rewriter, lab.getLoc(), block);
    }
    labels.try_emplace(lab.getLabel(), block);
  }

  // Look up block address references from global initializers using the
  // pre-built map (avoids O(F*G) quadratic scan).
  auto it = globalBlockAddrs.find(func.getSymName());
  if (it != globalBlockAddrs.end()) {
    for (StringRef label : it->second)
      blockAddrLabel.insert(label);
  }

  for (auto goTo : gotos) {
    mlir::OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPoint(goTo);
    auto it = labels.find(goTo.getLabel());
    assert(it != labels.end() && "goto label not found");
    Block *dest = it->second;
    // cir.goto can temporarily reference labels in nested regions before
    // FlattenCFG runs. Avoid introducing cross-region branches in that case
    // and preserve the symbolic goto for a later flattened run.
    if (dest->getParent() != goTo->getBlock()->getParent()) {
      preserveLabels.insert(std::string(goTo.getLabel()));
      continue;
    }
    cir::BrOp::create(rewriter, goTo.getLoc(), dest);
    goTo.erase();
  }

  // Resolve cir.asm.goto label names to block successors.
  // The first successor is the fallthrough block (the next block after the
  // asm.goto), and the remaining successors are the goto label blocks.
  for (auto asmGoto : asmGotos) {
    // The pass pipeline runs GotoSolver more than once. Unlike cir.goto (which
    // is erased once resolved), a resolved asm.goto is rewritten in place and
    // keeps its symbolic label_names attribute, so it is re-collected on the
    // next run — but by then its cir.label ops have been erased and the lookup
    // below would spuriously fail. A resolved asm.goto already has its block
    // successors wired up, so skip it.
    if (asmGoto->getNumSuccessors() > 0)
      continue;

    Block *asmBlock = asmGoto->getBlock();
    Block *fallthroughBlock = asmBlock->getNextNode();
    assert(fallthroughBlock && "asm.goto must have a fallthrough block");

    SmallVector<Block *> allSuccs;
    allSuccs.push_back(fallthroughBlock);
    bool canRewrite = true;
    for (auto labelAttr : asmGoto.getLabelNames()) {
      StringRef labelName = cast<StringAttr>(labelAttr).getValue();
      auto it = labels.find(labelName);
      if (it == labels.end()) {
        asmGoto.emitOpError() << "asm.goto label not found: " << labelName;
        return mlir::failure();
      }
      if (it->second->getParent() != asmBlock->getParent()) {
        preserveLabels.insert(std::string(labelName));
        canRewrite = false;
        break;
      }
      allSuccs.push_back(it->second);
    }
    if (!canRewrite)
      continue;

    // Rebuild the op with successors.
    mlir::OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPoint(asmGoto);

    // Collect the operand ranges.
    SmallVector<ValueRange> operandRanges;
    for (auto range : asmGoto.getAsmOperands())
      operandRanges.push_back(range);

    // Preserve the result type (if the asm goto has register outputs).
    mlir::Type resType;
    if (asmGoto.getRes())
      resType = asmGoto.getRes().getType();

    auto newOp = cir::InlineAsmGotoOp::create(
        rewriter, asmGoto.getLoc(), resType, operandRanges,
        asmGoto.getAsmStringAttr(), asmGoto.getConstraintsAttr(),
        asmGoto.getSideEffectsAttr(), asmGoto.getAsmFlavorAttr(),
        asmGoto.getOperandAttrsAttr(), asmGoto.getLabelNamesAttr(), allSuccs);
    newOp->setAttrs(asmGoto->getAttrs());

    // Replace uses of the old result with the new result.
    if (asmGoto.getRes())
      asmGoto.getRes().replaceAllUsesWith(newOp.getRes());

    asmGoto.erase();
  }

  // Replace cir.indirect_goto with cir.br to the indirect branch dispatch
  // block. The dispatch block is the block containing the cir.indirect_br op.
  //
  // CIRGen creates the dispatch block in the function-body region so it
  // survives FlattenCFG hoisting nested cir.scope blocks into that region. An
  // indirect_goto can, however, live in a region FlattenCFG does not hoist into
  // the function body -- most notably an OpenMP dialect region (the body of
  // omp.parallel / omp.wsloop / omp.loop_nest). MLIR forbids branches across
  // regions, so such a goto cannot target the function-level dispatch block.
  // Give each region that holds indirect_gotos its own dispatch block; the
  // successor-population loop below then wires up that region's labels.
  llvm::SmallVector<mlir::Block *> createdDispatchBlocks;
  if (!indirectGotos.empty()) {
    assert(!indirectBrs.empty() &&
           "indirect_goto without indirect_br dispatch block");
    llvm::DenseMap<mlir::Region *, mlir::Block *> dispatchForRegion;
    for (auto indBr : indirectBrs)
      dispatchForRegion.try_emplace(indBr->getBlock()->getParent(),
                                    indBr->getBlock());
    for (auto indGoTo : indirectGotos) {
      mlir::Region *region = indGoTo->getBlock()->getParent();
      mlir::Block *dispatchBlock = dispatchForRegion.lookup(region);
      if (!dispatchBlock) {
        // No dispatch block exists in this region yet (the goto sits in an
        // OpenMP region). Create one at the region's end with a void-ptr block
        // argument feeding a fresh cir.indirect_br.
        mlir::OpBuilder::InsertionGuard guard(rewriter);
        dispatchBlock = rewriter.createBlock(region, region->end(),
                                             {indGoTo.getAddr().getType()},
                                             {indGoTo.getLoc()});
        cir::IndirectBrOp::create(rewriter, indGoTo.getLoc(),
                                  dispatchBlock->getArgument(0),
                                  /*poison=*/false,
                                  mlir::ArrayRef<mlir::ValueRange>{},
                                  mlir::ArrayRef<mlir::Block *>{});
        indirectBrs.push_back(
            cast<cir::IndirectBrOp>(dispatchBlock->getTerminator()));
        dispatchForRegion[region] = dispatchBlock;
        createdDispatchBlocks.push_back(dispatchBlock);
      }
      mlir::OpBuilder::InsertionGuard guard(rewriter);
      rewriter.setInsertionPoint(indGoTo);
      cir::BrOp::create(rewriter, indGoTo.getLoc(), dispatchBlock,
                         indGoTo.getAddr());
      indGoTo.erase();
    }
  }

  // Populate cir.indirect_br successor lists. At CIR codegen time, the
  // successor list may be empty because block addresses used in constant
  // expressions (e.g., `void *labels[] = {&&a, &&b}`) don't create
  // BlockAddressOps, and cross-region references aren't allowed in structured
  // CIR. Now that FlattenCFG has made all blocks flat, we can wire up the
  // label blocks as successors.
  for (auto indBr : indirectBrs) {
    if (indBr.getPoison())
      continue;

    // Collect existing successors to avoid duplicates.
    llvm::SmallPtrSet<mlir::Block *, 4> existingSuccs;
    for (mlir::Block *succ : indBr.getSuccessors())
      existingSuccs.insert(succ);

    // Find label blocks that should be successors but aren't yet.
    llvm::SmallVector<mlir::Block *> newSuccs;
    llvm::SmallVector<mlir::ValueRange> newOperands;
    for (auto &labelName : blockAddrLabel) {
      auto it = labels.find(labelName);
      if (it == labels.end())
        continue;
      mlir::Block *block = it->second;
      if (block->getParent() != indBr->getBlock()->getParent())
        continue;
      if (!existingSuccs.contains(block)) {
        newSuccs.push_back(block);
        newOperands.push_back(block->getArguments());
      }
    }

    if (newSuccs.empty())
      continue;

    // Collect existing successor info.
    llvm::SmallVector<mlir::Block *> allSuccs(indBr.getSuccessors().begin(),
                                              indBr.getSuccessors().end());
    llvm::SmallVector<mlir::ValueRange> allOperands;
    for (unsigned i = 0; i < indBr.getSuccessors().size(); ++i)
      allOperands.push_back(indBr.getSuccOperands()[i]);

    // Append new successors.
    allSuccs.append(newSuccs.begin(), newSuccs.end());
    allOperands.append(newOperands.begin(), newOperands.end());

    // Replace with a new IndirectBrOp that includes all successors.
    mlir::OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPoint(indBr);
    cir::IndirectBrOp::create(rewriter, indBr.getLoc(), indBr.getAddr(),
                              indBr.getPoison(), allOperands, allSuccs);
    indBr.erase();
  }

  // A dispatch block created for a nested (e.g. OpenMP) region only ever gets
  // same-region labels wired up above; a target label outside that region is a
  // genuine cross-region computed goto (e.g. jumping out of an omp.parallel),
  // which would need the label hoisted or the region outlined first. Reject it
  // cleanly instead of emitting an cir.indirect_br with no successors.
  for (mlir::Block *dispatchBlock : createdDispatchBlocks) {
    auto indBr = cast<cir::IndirectBrOp>(dispatchBlock->getTerminator());
    if (indBr.getSuccessors().empty())
      return indBr.emitOpError()
             << "NYI: computed goto in a nested region (e.g. OpenMP) whose "
                "target label lives outside that region";
  }

  // Erase the function-level dispatch block left dead when every indirect_goto
  // lived in a nested region and got its own dispatch block there: it has no
  // predecessors and, with its labels in the nested region, no successors.
  llvm::SmallVector<cir::IndirectBrOp> deadBrs;
  func.getBody().walk([&](cir::IndirectBrOp indBr) {
    if (indBr->getBlock()->hasNoPredecessors() && indBr.getSuccessors().empty())
      deadBrs.push_back(indBr);
  });
  for (auto indBr : deadBrs) {
    mlir::Block *block = indBr->getBlock();
    indBr.erase();
    block->erase();
  }

  for (auto &lab : labels) {
    StringRef labelName = lab.getKey();
    Block *block = lab.getValue();
    if (!blockAddrLabel.contains(labelName) &&
        !preserveLabels.contains(std::string(labelName))) {
      // Erase the LabelOp inside the block if it wasn't preserved for a later
      // flatten+goto-solver run.
      if (auto lab = dyn_cast<cir::LabelOp>(&block->front()))
        lab.erase();
    }
  }
  return mlir::success();
}

void GotoSolverPass::runOnOperation() {
  // Whichever way this returns, the module has been through it.
  auto recordStage = llvm::make_scope_exit(
      [&] { cir::advanceStage(getOperation(), cir::CIRStage::Flat); });
  llvm::TimeTraceScope scope("Goto Solver");
  auto *op = getOperation();
  auto moduleOp = dyn_cast<mlir::ModuleOp>(op);

  // Pre-build a map of block address labels referenced in global initializers
  // once, to avoid O(F*G) quadratic scanning (walking all globals for every
  // function).
  GlobalBlockAddrMap globalBlockAddrs;
  if (moduleOp)
    globalBlockAddrs = buildGlobalBlockAddrMap(moduleOp);

  // Collect all FuncOps first to avoid walk invalidation issues, without
  // walking their bodies: functions don't nest. Each is processed on its own:
  // process() only rewrites the function's body and reads the module through
  // the map above, so functions run in parallel.
  llvm::SmallVector<cir::FuncOp> funcOps;
  op->walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation *nested) {
    if (auto func = dyn_cast<cir::FuncOp>(nested)) {
      funcOps.push_back(func);
      return mlir::WalkResult::skip();
    }
    return mlir::WalkResult::advance();
  });
  if (mlir::failed(mlir::failableParallelForEach(
          &getContext(), funcOps,
          [&](cir::FuncOp func) { return process(func, globalBlockAddrs); })))
    signalPassFailure();
}

} // namespace

std::unique_ptr<Pass> mlir::createGotoSolverPass() {
  return std::make_unique<GotoSolverPass>();
}
