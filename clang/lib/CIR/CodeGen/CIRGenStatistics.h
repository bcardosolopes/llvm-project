//===--- CIRGenStatistics.h - CIR CodeGen Statistics ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CIR_CIRGENSTATISTICS_H
#define LLVM_CLANG_LIB_CIR_CIRGENSTATISTICS_H

#include <string>

#include "Address.h"
#include "mlir/IR/Value.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "llvm/ADT/SmallVector.h"

namespace clang::CIRGen {

/// This class managers statistics gathered during CIR code generation.
/// It is used to print statistics at the end of the compilation.
///
struct CIRGenStatistics {
private:
  /// Array initialization statistics. List of JSON objects as strings.
  llvm::SmallVector<std::string> ArrayInits;

public:
  static CIRGenStatistics Stats;

  CIRGenStatistics() {}
  /// Destructor. Print statistics.
  ~CIRGenStatistics();

  /// Check if the array init statistics are enabled.
  bool isPrintArrayInitsEnabled();
  /// Collect the next value in the array init list.
  void collectArrayInit(llvm::SmallVector<mlir::Value> &InitList, Address Addr);
  /// Record array initialization from constant attributes.
  void recordArrayInit(mlir::Attribute Attrs, cir::ArrayType ArrayTy);
  /// Record array initialization from a list of values.
  void recordArrayInit(llvm::ArrayRef<mlir::Value> InitList,
                       cir::ArrayType ArrayTy);
};

} // namespace clang::CIRGen

#endif
