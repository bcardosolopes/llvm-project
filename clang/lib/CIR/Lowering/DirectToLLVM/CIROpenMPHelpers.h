//===- CIROpenMPHelpers.h -- CIR-side OpenMP→LLVM helpers -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// CIR-private duplicates of file-static helpers from
// `mlir/lib/Target/LLVMIR/Dialect/OpenMP/OpenMPToLLVMIRTranslation.cpp`.
// These let CIR's own LLVM-IR translation hook for `cir.omp.scope_reduction`
// call `OpenMPIRBuilder::createReductions` directly without depending on the
// upstream `omp.scope_reduction` op + its `convertOmpScopeReduction`
// translator (both since removed).
//
// All helpers are name-prefixed with `cir` to avoid collisions when this
// translation unit is linked alongside the shared OpenMP translator. The
// implementations are mechanical copies — see comments inside the .cpp file
// for the exact upstream provenance of each helper.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_LOWERING_DIRECTTOLLVM_CIROPENMPHELPERS_H
#define CLANG_CIR_LOWERING_DIRECTTOLLVM_CIROPENMPHELPERS_H

#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "mlir/IR/Operation.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Target/LLVMIR/ModuleTranslation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Frontend/OpenMP/OMPIRBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/Error.h"

#include <functional>
#include <optional>

namespace cir {
namespace direct {
namespace openmp_helpers {

/// Owning equivalents of OpenMPIRBuilder::(Atomic)ReductionGen that store
/// lambdas with capture. Mirrors the unnamed-namespace typedefs in the
/// upstream translator.
using OwningReductionGen =
    std::function<llvm::OpenMPIRBuilder::InsertPointOrErrorTy(
        llvm::OpenMPIRBuilder::InsertPointTy, llvm::Value *, llvm::Value *,
        llvm::Value *&)>;
using OwningAtomicReductionGen =
    std::function<llvm::OpenMPIRBuilder::InsertPointOrErrorTy(
        llvm::OpenMPIRBuilder::InsertPointTy, llvm::Type *, llvm::Value *,
        llvm::Value *)>;
using OwningDataPtrPtrReductionGen =
    std::function<llvm::OpenMPIRBuilder::InsertPointOrErrorTy(
        llvm::OpenMPIRBuilder::InsertPointTy, llvm::Value *, llvm::Value *&)>;

/// Match the upstream `getIsByRef` helper: unwrap an optional ArrayRef<bool>.
llvm::ArrayRef<bool>
cirGetIsByRef(std::optional<llvm::ArrayRef<bool>> attr);

/// Find the alloca insertion point for the given builder, matching the
/// upstream `findAllocaInsertPoint`. Walks the ModuleTranslation stack for
/// any frame the upstream OpenMP translator has pushed; falls back to the
/// surrounding function entry block. (For `cir.omp.scope_reduction`, no
/// outlining intervenes between the enclosing `cir.scope` and the reduction
/// emission, so the fallback always lands in the right function entry.)
llvm::OpenMPIRBuilder::InsertPointTy
cirFindAllocaInsertPoint(llvm::IRBuilderBase &builder,
                         mlir::LLVM::ModuleTranslation &moduleTranslation);

/// Map an `llvm::Error` to a `LogicalResult`, matching the upstream
/// `handleError` overload pair.
mlir::LogicalResult cirHandleError(llvm::Error error, mlir::Operation &op);

template <typename T>
mlir::LogicalResult cirHandleError(llvm::Expected<T> &result,
                                   mlir::Operation &op) {
  if (!result)
    return cirHandleError(result.takeError(), op);
  return mlir::success();
}

/// Mirror of upstream `checkImplementationStatus` restricted to the
/// `omp.scope_reduction`-relevant subset (clauses on the reduction op
/// itself). Other op kinds are not consulted from CIR's translator.
mlir::LogicalResult
cirCheckScopeReductionImplementationStatus(mlir::Operation &op);

/// Look up reduction declarations referenced by symbol from `op`.
/// Mirror of upstream `collectReductionDecls<T>`.
template <typename T>
void cirCollectReductionDecls(
    T op,
    llvm::SmallVectorImpl<mlir::omp::DeclareReductionOp> &reductions) {
  std::optional<mlir::ArrayAttr> attr = op.getReductionSyms();
  if (!attr)
    return;

  reductions.reserve(reductions.size() + op.getNumReductionVars());
  for (auto symbolRef : attr->template getAsRange<mlir::SymbolRefAttr>()) {
    reductions.push_back(
        mlir::SymbolTable::lookupNearestSymbolFrom<mlir::omp::DeclareReductionOp>(
            op, symbolRef));
  }
}

/// Translate the body of an `omp.declare_reduction` region (single-block
/// fast path, matching the upstream helper). Mirror of upstream
/// `inlineConvertOmpRegions`. Restricted to the single-block path because
/// `omp.declare_reduction` reduction/atomic/data_ptr_ptr regions are
/// always single-block in CIR-emitted IR.
mlir::LogicalResult
cirInlineConvertOmpRegions(mlir::Region &region, llvm::StringRef blockName,
                           llvm::IRBuilderBase &builder,
                           mlir::LLVM::ModuleTranslation &moduleTranslation,
                           llvm::SmallVectorImpl<llvm::Value *>
                               *continuationBlockArgs = nullptr);

/// Build OwningReductionGen for a declare_reduction op (combiner region).
OwningReductionGen
cirMakeReductionGen(mlir::omp::DeclareReductionOp decl,
                    llvm::IRBuilderBase &builder,
                    mlir::LLVM::ModuleTranslation &moduleTranslation);

/// Build OwningAtomicReductionGen if the declare_reduction op has an atomic
/// region; otherwise return an empty std::function.
OwningAtomicReductionGen
cirMakeAtomicReductionGen(mlir::omp::DeclareReductionOp decl,
                          llvm::IRBuilderBase &builder,
                          mlir::LLVM::ModuleTranslation &moduleTranslation);

/// Build OwningDataPtrPtrReductionGen for byref reductions; empty otherwise.
OwningDataPtrPtrReductionGen cirMakeRefDataPtrGen(
    mlir::omp::DeclareReductionOp decl, llvm::IRBuilderBase &builder,
    mlir::LLVM::ModuleTranslation &moduleTranslation, bool isByRef);

/// Populate `reductionInfos` with `OpenMPIRBuilder::ReductionInfo` entries
/// for each reduction variable on `op`. Mirror of upstream
/// `collectReductionInfo<T>`.
template <typename T>
void cirCollectReductionInfo(
    T op, llvm::IRBuilderBase &builder,
    mlir::LLVM::ModuleTranslation &moduleTranslation,
    llvm::SmallVectorImpl<mlir::omp::DeclareReductionOp> &reductionDecls,
    llvm::SmallVectorImpl<OwningReductionGen> &owningReductionGens,
    llvm::SmallVectorImpl<OwningAtomicReductionGen> &owningAtomicReductionGens,
    llvm::SmallVectorImpl<OwningDataPtrPtrReductionGen>
        &owningDataPtrPtrReductionGens,
    const llvm::ArrayRef<llvm::Value *> privateReductionVariables,
    llvm::SmallVectorImpl<llvm::OpenMPIRBuilder::ReductionInfo> &reductionInfos,
    llvm::ArrayRef<bool> isByRef) {
  unsigned numReductions = op.getNumReductionVars();

  for (unsigned i = 0; i < numReductions; ++i) {
    owningReductionGens.push_back(
        cirMakeReductionGen(reductionDecls[i], builder, moduleTranslation));
    owningAtomicReductionGens.push_back(cirMakeAtomicReductionGen(
        reductionDecls[i], builder, moduleTranslation));
    owningDataPtrPtrReductionGens.push_back(cirMakeRefDataPtrGen(
        reductionDecls[i], builder, moduleTranslation, isByRef[i]));
  }

  reductionInfos.reserve(numReductions);
  for (unsigned i = 0; i < numReductions; ++i) {
    llvm::OpenMPIRBuilder::ReductionGenAtomicCBTy atomicGen = nullptr;
    if (owningAtomicReductionGens[i])
      atomicGen = owningAtomicReductionGens[i];
    llvm::Value *variable =
        moduleTranslation.lookupValue(op.getReductionVars()[i]);
    mlir::Type allocatedType;
    reductionDecls[i].getAllocRegion().walk([&](mlir::Operation *innerOp) {
      if (auto alloca =
              mlir::dyn_cast<mlir::LLVM::AllocaOp>(innerOp)) {
        allocatedType = alloca.getElemType();
        return mlir::WalkResult::interrupt();
      }
      return mlir::WalkResult::advance();
    });

    reductionInfos.push_back(
        {moduleTranslation.convertType(reductionDecls[i].getType()), variable,
         privateReductionVariables[i],
         /*EvaluationKind=*/llvm::OpenMPIRBuilder::EvalKind::Scalar,
         owningReductionGens[i],
         /*ReductionGenClang=*/nullptr, atomicGen,
         owningDataPtrPtrReductionGens[i],
         allocatedType ? moduleTranslation.convertType(allocatedType) : nullptr,
         reductionDecls[i].getByrefElementType()
             ? moduleTranslation.convertType(
                   *reductionDecls[i].getByrefElementType())
             : nullptr});
  }
}

} // namespace openmp_helpers
} // namespace direct
} // namespace cir

#endif // CLANG_CIR_LOWERING_DIRECTTOLLVM_CIROPENMPHELPERS_H
