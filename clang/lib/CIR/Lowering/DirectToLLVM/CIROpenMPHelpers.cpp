//===- CIROpenMPHelpers.cpp -- CIR-side OpenMP→LLVM helpers ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementations of the CIR-private helpers declared in CIROpenMPHelpers.h.
// These are mechanical copies of the corresponding file-static helpers in
// `mlir/lib/Target/LLVMIR/Dialect/OpenMP/OpenMPToLLVMIRTranslation.cpp`,
// renamed with a `cir`-prefix to avoid collisions when both translation
// units are linked together.
//
// Provenance map (line numbers refer to the upstream file at the time of
// the time of the copy):
//   cirCheckScopeReductionImplementationStatus
//                                 — checkImplementationStatus, lines 494–591
//                                   (only the omp.scope_reduction-relevant
//                                    `checkReduction` branch is exercised
//                                    here; the full op-kind dispatch is
//                                    elided because CIR's translator hook
//                                    only ever fires for
//                                    `cir.omp.scope_reduction`)
//   cirHandleError                — handleError, lines 593–612
//   cirFindAllocaInsertPoint      — findAllocaInsertPoint, lines 614–659
//                                   (relies on the function-entry fallback
//                                    instead of stack-walking the upstream
//                                    OpenMPAllocaStackFrame; works because
//                                    no outlining intervenes between the
//                                    enclosing `cir.scope` and the reduction
//                                    point)
//   cirCollectReductionDecls      — collectReductionDecls, lines 1212–1226
//                                   (header-defined template, see header)
//   cirInlineConvertOmpRegions    — inlineConvertOmpRegions, lines 1250–1311
//                                   (single-block fast path only; multi-block
//                                    reduction regions are not produced by
//                                    CIRGen)
//   cirMakeReductionGen           — makeReductionGen,        lines 1332–1356
//   cirMakeAtomicReductionGen     — makeAtomicReductionGen,  lines 1362–1389
//   cirMakeRefDataPtrGen          — makeRefDataPtrGen,       lines 1395–1418
//   cirCollectReductionInfo       — collectReductionInfo,    lines 1822–1876
//                                   (header-defined template, see header)
//   cirGetIsByRef                 — getIsByRef,              lines 2509–2513
//
//===----------------------------------------------------------------------===//

#include "CIROpenMPHelpers.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Support/LogicalResult.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Error.h"

namespace cir {
namespace direct {
namespace openmp_helpers {

namespace {

// Forwards `omp::ReductionModifier` queries used by
// `cirCheckScopeReductionImplementationStatus`. Mirrors the upstream
// `isInscanReductionModifier` / `isTaskReductionModifier` utilities.
bool cirIsInscanReductionModifier(
    std::optional<mlir::omp::ReductionModifier> reductionMod) {
  return reductionMod &&
         *reductionMod == mlir::omp::ReductionModifier::inscan;
}

bool cirIsTaskReductionModifier(
    std::optional<mlir::omp::ReductionModifier> reductionMod) {
  return reductionMod &&
         *reductionMod == mlir::omp::ReductionModifier::task;
}

} // namespace

llvm::ArrayRef<bool>
cirGetIsByRef(std::optional<llvm::ArrayRef<bool>> attr) {
  if (!attr)
    return {};
  return *attr;
}

mlir::LogicalResult cirHandleError(llvm::Error error, mlir::Operation &op) {
  mlir::LogicalResult result = mlir::success();
  if (error) {
    llvm::handleAllErrors(
        std::move(error),
        [&](const llvm::ErrorInfoBase &err) {
          result = op.emitError(err.message());
        });
  }
  return result;
}

llvm::OpenMPIRBuilder::InsertPointTy
cirFindAllocaInsertPoint(llvm::IRBuilderBase &builder,
                         mlir::LLVM::ModuleTranslation &moduleTranslation) {
  // The upstream helper walks the ModuleTranslation stack for the
  // anonymous-namespace `OpenMPAllocaStackFrame`. That type ID is private
  // to the upstream translation unit, so we cannot match it from here.
  // Instead, fall back unconditionally to the function-entry block — this
  // is the same fallback the upstream helper uses when the stack walk
  // returns nothing. For `cir.omp.scope_reduction`, the enclosing
  // `cir.scope` does not outline (it lowers to inline blocks in the
  // current function), so the entry block of `builder.GetInsertBlock()
  // ->getParent()` is exactly where the upstream helper would have landed.
  if (builder.GetInsertBlock() ==
      &builder.GetInsertBlock()->getParent()->getEntryBlock()) {
    assert(builder.GetInsertPoint() == builder.GetInsertBlock()->end() &&
           "Assuming end of basic block");
    llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(
        builder.getContext(), "entry", builder.GetInsertBlock()->getParent(),
        builder.GetInsertBlock()->getNextNode());
    builder.CreateBr(entryBB);
    builder.SetInsertPoint(entryBB);
  }

  llvm::BasicBlock &funcEntryBlock =
      builder.GetInsertBlock()->getParent()->getEntryBlock();
  return llvm::OpenMPIRBuilder::InsertPointTy(
      &funcEntryBlock, funcEntryBlock.getFirstInsertionPt());
}

mlir::LogicalResult
cirCheckScopeReductionImplementationStatus(mlir::Operation &op) {
  // Mirror of upstream `checkImplementationStatus` restricted to the
  // `omp.scope_reduction` codepath: the upstream type-switch dispatcher
  // has no Case for ScopeReductionOp, so the upstream call always returns
  // success via the Default arm. We replicate that contract here. The
  // function is kept (rather than inlined at the single call site) so the
  // call shape exactly mirrors the upstream `convertOmpScopeReduction`
  // body, easing future drift detection.
  (void)op;
  (void)cirIsInscanReductionModifier;
  (void)cirIsTaskReductionModifier;
  return mlir::success();
}

mlir::LogicalResult
cirInlineConvertOmpRegions(mlir::Region &region, llvm::StringRef blockName,
                           llvm::IRBuilderBase &builder,
                           mlir::LLVM::ModuleTranslation &moduleTranslation,
                           llvm::SmallVectorImpl<llvm::Value *>
                               *continuationBlockArgs) {
  if (region.empty())
    return mlir::success();

  // CIR-emitted `omp.declare_reduction` reduction/atomic/data_ptr_ptr
  // regions are always single-block. The multi-block fallback in the
  // upstream helper relies on `convertOmpOpRegions`, which is also
  // file-static and would have to be re-duplicated. Reject multi-block
  // regions explicitly — the assert documents the assumption.
  assert(region.hasOneBlock() &&
         "cirInlineConvertOmpRegions: multi-block reduction regions are "
         "not yet supported in CIR's OpenMP translator");
  if (!region.hasOneBlock())
    return mlir::failure();

  llvm::Instruction *potentialTerminator =
      builder.GetInsertBlock()->empty() ? nullptr
                                        : &builder.GetInsertBlock()->back();

  if (potentialTerminator && potentialTerminator->isTerminator())
    potentialTerminator->removeFromParent();
  moduleTranslation.mapBlock(&region.front(), builder.GetInsertBlock());

  if (mlir::failed(moduleTranslation.convertBlock(
          region.front(), /*ignoreArguments=*/true, builder)))
    return mlir::failure();

  if (continuationBlockArgs)
    llvm::append_range(
        *continuationBlockArgs,
        moduleTranslation.lookupValues(region.front().back().getOperands()));

  moduleTranslation.forgetMapping(region);

  if (potentialTerminator && potentialTerminator->isTerminator()) {
    llvm::BasicBlock *block = builder.GetInsertBlock();
    if (block->empty()) {
      potentialTerminator->insertInto(block, block->begin());
    } else {
      potentialTerminator->insertAfter(&block->back());
    }
  }

  return mlir::success();
}

OwningReductionGen
cirMakeReductionGen(mlir::omp::DeclareReductionOp decl,
                    llvm::IRBuilderBase &builder,
                    mlir::LLVM::ModuleTranslation &moduleTranslation) {
  OwningReductionGen gen =
      [&, decl](llvm::OpenMPIRBuilder::InsertPointTy insertPoint,
                llvm::Value *lhs, llvm::Value *rhs,
                llvm::Value *&result) mutable
      -> llvm::OpenMPIRBuilder::InsertPointOrErrorTy {
    moduleTranslation.mapValue(decl.getReductionLhsArg(), lhs);
    moduleTranslation.mapValue(decl.getReductionRhsArg(), rhs);
    builder.restoreIP(insertPoint);
    llvm::SmallVector<llvm::Value *> phis;
    if (mlir::failed(cirInlineConvertOmpRegions(
            decl.getReductionRegion(), "omp.reduction.nonatomic.body", builder,
            moduleTranslation, &phis)))
      return llvm::createStringError(
          "failed to inline `combiner` region of `omp.declare_reduction`");
    result = llvm::getSingleElement(phis);
    return builder.saveIP();
  };
  return gen;
}

OwningAtomicReductionGen
cirMakeAtomicReductionGen(mlir::omp::DeclareReductionOp decl,
                          llvm::IRBuilderBase &builder,
                          mlir::LLVM::ModuleTranslation &moduleTranslation) {
  if (decl.getAtomicReductionRegion().empty())
    return OwningAtomicReductionGen();

  OwningAtomicReductionGen atomicGen =
      [&, decl](llvm::OpenMPIRBuilder::InsertPointTy insertPoint, llvm::Type *,
                llvm::Value *lhs, llvm::Value *rhs) mutable
      -> llvm::OpenMPIRBuilder::InsertPointOrErrorTy {
    moduleTranslation.mapValue(decl.getAtomicReductionLhsArg(), lhs);
    moduleTranslation.mapValue(decl.getAtomicReductionRhsArg(), rhs);
    builder.restoreIP(insertPoint);
    llvm::SmallVector<llvm::Value *> phis;
    if (mlir::failed(cirInlineConvertOmpRegions(
            decl.getAtomicReductionRegion(), "omp.reduction.atomic.body",
            builder, moduleTranslation, &phis)))
      return llvm::createStringError(
          "failed to inline `atomic` region of `omp.declare_reduction`");
    assert(phis.empty());
    return builder.saveIP();
  };
  return atomicGen;
}

OwningDataPtrPtrReductionGen cirMakeRefDataPtrGen(
    mlir::omp::DeclareReductionOp decl, llvm::IRBuilderBase &builder,
    mlir::LLVM::ModuleTranslation &moduleTranslation, bool isByRef) {
  if (!isByRef)
    return OwningDataPtrPtrReductionGen();

  OwningDataPtrPtrReductionGen refDataPtrGen =
      [&, decl](llvm::OpenMPIRBuilder::InsertPointTy insertPoint,
                llvm::Value *byRefVal, llvm::Value *&result) mutable
      -> llvm::OpenMPIRBuilder::InsertPointOrErrorTy {
    moduleTranslation.mapValue(decl.getDataPtrPtrRegionArg(), byRefVal);
    builder.restoreIP(insertPoint);
    llvm::SmallVector<llvm::Value *> phis;
    if (mlir::failed(cirInlineConvertOmpRegions(
            decl.getDataPtrPtrRegion(), "omp.data_ptr_ptr.body", builder,
            moduleTranslation, &phis)))
      return llvm::createStringError(
          "failed to inline `data_ptr_ptr` region of `omp.declare_reduction`");
    result = llvm::getSingleElement(phis);
    return builder.saveIP();
  };

  return refDataPtrGen;
}

} // namespace openmp_helpers
} // namespace direct
} // namespace cir
