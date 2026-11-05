//===- LoweringPrepareARMCXXABI.cpp - ARM ABI specific code --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides ARM (32-bit, AAPCS) C++ ABI specific code that is used
// during LLVM lowering prepare, currently limited to aggregate va_arg lowering.
// Scalar va_arg is left to the generic LLVM va_arg path (return {}).
//
//===----------------------------------------------------------------------===//

#include "LoweringPrepareItaniumCXXABI.h"
#include "LoweringPrepareVAArgStack.h"
#include "clang/CIR/Dialect/IR/CIRDataLayout.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "llvm/Support/MathExtras.h"

using namespace cir;

namespace {
class LoweringPrepareARMCXXABI : public LoweringPrepareItaniumCXXABI {
public:
  mlir::Value lowerVAArg(cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
                         const cir::CIRDataLayout &datalayout) override;
};
} // namespace

mlir::Value
LoweringPrepareARMCXXABI::lowerVAArg(cir::CIRBaseBuilderTy &builder,
                                     cir::VAArgOp op,
                                     const cir::CIRDataLayout &datalayout) {
  mlir::Type ty = op.getType();

  // Only aggregate va_arg needs CIR-level lowering here. Scalars fall back to
  // the generic LLVM va_arg lowering, matching the pre-existing behavior.
  if (!mlir::isa<cir::RecordType>(ty))
    return {};

  builder.setInsertionPoint(op);

  uint64_t tyStoreSize = datalayout.getTypeStoreSize(ty).getFixedValue();

  // AAPCS bounds the ABI alignment of the slot to [4, 8]. The unadjusted type
  // alignment was recorded on the op at CIRGen time (getTypeUnadjustedAlign),
  // matching classic ARMABIInfo::EmitVAArg's TyAlignForABI computation. Fall
  // back to the data layout ABI alignment if the attribute is absent.
  uint64_t tyAlign;
  if (auto alignAttr = op.getAlignmentAttr())
    tyAlign = alignAttr.getInt();
  else
    tyAlign = datalayout.getABITypeAlign(ty).value();

  constexpr uint64_t slotSize = 4;
  uint64_t directAlign = std::min<uint64_t>(std::max<uint64_t>(tyAlign, 4), 8);

  return vaarg_stack::lowerAggregateStackVAArg(builder, op, tyStoreSize,
                                               directAlign, slotSize,
                                               /*ptrIntBits=*/32);
}

cir::LoweringPrepareCXXABI *cir::LoweringPrepareCXXABI::createARMABI() {
  return new LoweringPrepareARMCXXABI();
}
