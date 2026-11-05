//==-- SwiftCallingConv.h - CIR Swift ABI lowering ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements Swift calling convention lowering for CIR, ported from
// clang/lib/CodeGen/SwiftCallingConv.cpp to operate on CIR types.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CIR_DIALECT_TRANSFORMS_TARGETLOWERING_SWIFTCALLINGCONV_H
#define LLVM_CLANG_LIB_CIR_DIALECT_TRANSFORMS_TARGETLOWERING_SWIFTCALLINGCONV_H

#include "LowerFunctionInfo.h"
#include "mlir/IR/Types.h"
#include "clang/AST/CharUnits.h"
#include "clang/CIR/ABIArgInfo.h"
#include "llvm/ADT/SmallVector.h"

namespace cir {

class LowerTypes;

namespace swiftcall {

/// Lowering helper for Swift aggregates. Walks aggregate types, collects
/// their scalar components at byte offsets, and produces CoerceAndExpand
/// ABIArgInfo with the correct coercion/unpadded types.
class SwiftAggLowering {
  LowerTypes &LT;

  struct StorageEntry {
    clang::CharUnits Begin;
    clang::CharUnits End;
    mlir::Type Type; // nullptr means opaque

    clang::CharUnits getWidth() const { return End - Begin; }
  };
  llvm::SmallVector<StorageEntry, 4> Entries;
  bool Finished = false;

public:
  SwiftAggLowering(LowerTypes &LT) : LT(LT) {}

  void addOpaqueData(clang::CharUnits begin, clang::CharUnits end) {
    addEntry(nullptr, begin, end);
  }

  /// Add typed data for a CIR type at the given byte offset.
  void addTypedData(mlir::Type type, clang::CharUnits begin);
  void addTypedData(mlir::Type type, clang::CharUnits begin,
                    clang::CharUnits end);

  void finish();

  bool empty() const {
    assert(Finished && "didn't finish lowering before calling empty()");
    return Entries.empty();
  }

  bool shouldPassIndirectly(bool asReturnValue) const;

  /// Return the coercion type (with padding) and the unpadded type.
  std::pair<mlir::Type, mlir::Type> getCoerceAndExpandTypes() const;

private:
  void addLegalTypedData(mlir::Type type, clang::CharUnits begin,
                         clang::CharUnits end);
  void addEntry(mlir::Type type, clang::CharUnits begin,
                clang::CharUnits end);
  void splitVectorEntry(unsigned index);
  static bool shouldMergeEntries(const StorageEntry &first,
                                 const StorageEntry &second,
                                 clang::CharUnits chunkSize);
};

/// Classify a CIR type for swiftcall argument/return lowering.
ABIArgInfo classifyType(LowerTypes &LT, mlir::Type type, bool forReturn);

/// Compute ABI info for all arguments and return value of a swiftcall
/// function.
void computeABIInfo(LowerTypes &LT, LowerFunctionInfo &FI);

/// Return the maximum voluntary integer size for the current target.
clang::CharUnits getMaximumVoluntaryIntegerSize(LowerTypes &LT);

/// Return the Swift CC's notion of the natural alignment of a CIR type.
clang::CharUnits getNaturalAlignment(LowerTypes &LT, mlir::Type type);

/// Is the given integer width "legal" for Swift's perspective?
bool isLegalIntegerType(LowerTypes &LT, unsigned bitWidth);

/// Is the given vector type legal for Swift?
bool isLegalVectorType(LowerTypes &LT, clang::CharUnits vectorSize,
                       mlir::Type eltTy, unsigned numElts);

/// Count registers occupied by a set of scalar types. Returns true if
/// it exceeds maxAllRegisters.
bool occupiesMoreThan(LowerTypes &LT,
                      llvm::ArrayRef<mlir::Type> scalarTypes,
                      unsigned maxAllRegisters);

} // namespace swiftcall
} // namespace cir

#endif // LLVM_CLANG_LIB_CIR_DIALECT_TRANSFORMS_TARGETLOWERING_SWIFTCALLINGCONV_H
