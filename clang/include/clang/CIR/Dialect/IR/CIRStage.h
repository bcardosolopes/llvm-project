//===- CIRStage.h - A module's place in the CIR pipeline --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A module records how far it has come through the CIR pipeline as its
// cir.stage attribute. The passes that complete a stage set it, so whoever
// picks the module up, clang's own lowering or clang -x cir, knows what is
// left to run without being told.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_DIALECT_IR_CIRSTAGE_H
#define CLANG_CIR_DIALECT_IR_CIRSTAGE_H

#include "llvm/ADT/StringRef.h"

#include <optional>

namespace mlir {
class Operation;
} // namespace mlir

namespace cir {

/// The stages, in pipeline order: a module at one stage has been through every
/// earlier one. Apart from cirgen, each is spelled the way -emit-mlir= spells
/// the output there.
enum class CIRStage {
  /// "cirgen": CIRGen's output, before the CIR-to-CIR pipeline.
  CIRGen,
  /// "cir-prepare": after CXXABILowering and LoweringPrepare. What -emit-cir
  /// prints when call-conv lowering is off.
  Prepare,
  /// "cir-abi": after CallConvLowering.
  ABI,
  /// "cir-flat": after HoistAllocas, FlattenCFG and GotoSolver.
  Flat,
};

llvm::StringRef stringifyCIRStage(CIRStage stage);
std::optional<CIRStage> symbolizeCIRStage(llvm::StringRef name);

/// The stage \p op records, or nullopt if it records none: it is not a module,
/// or its CIR was written by hand.
std::optional<CIRStage> getStage(mlir::Operation *op);

/// Record that \p op, if it is a module, has reached \p stage. A module never
/// goes back: a pass run again on a module at a later stage leaves it there.
void advanceStage(mlir::Operation *op, CIRStage stage);

/// Drop the stage, once the module is no longer CIR.
void clearStage(mlir::Operation *op);

} // namespace cir

#endif // CLANG_CIR_DIALECT_IR_CIRSTAGE_H
