//===- CIRToABIType.h - CIR to LLVM ABI library type bridge -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Maps a CIR type onto the llvm::abi::Type the LLVM ABI Lowering Library
// classifies, so that every CIR pass which needs an ABI answer asks the one
// shared classifier instead of hand-rolling target rules.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_LIB_CIR_DIALECT_TRANSFORMS_CIRTOABITYPE_H
#define CLANG_LIB_CIR_DIALECT_TRANSFORMS_CIRTOABITYPE_H

#include "mlir/ABI/ABITypeMapper.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Types.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "llvm/ABI/Types.h"

namespace cir {

/// Map a CIR type onto the llvm::abi::Type the ABI Lowering Library
/// classifies.  Returns null for a type with no mapping yet (a vector, an
/// incomplete record, or an aggregate one of whose members has no mapping), so
/// a caller that cannot proceed without an ABI answer can report NYI.
///
/// A null result says only that this bridge has no llvm::abi spelling for the
/// type.  Whether the classifier's answer for a type that does map is one a
/// given pass can act on is that pass's own question: CallConvLoweringPass
/// pre-filters with a stricter predicate, because it must also be able to
/// convert the resulting coercion type back to CIR.
const llvm::abi::Type *mapTypeToABIType(mlir::Type type,
                                        mlir::abi::ABITypeMapper &typeMapper,
                                        const mlir::DataLayout &dl,
                                        mlir::ModuleOp modOp);

} // namespace cir

#endif // CLANG_LIB_CIR_DIALECT_TRANSFORMS_CIRTOABITYPE_H
