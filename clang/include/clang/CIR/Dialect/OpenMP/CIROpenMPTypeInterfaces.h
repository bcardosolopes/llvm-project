//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains external dialect interfaces for CIR.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_DIALECT_OPENMP_CIROPENMPTYPEINTERFACES_H
#define CLANG_CIR_DIALECT_OPENMP_CIROPENMPTYPEINTERFACES_H

#include "mlir/Dialect/OpenMP/OpenMPDialect.h"

namespace cir::omp {

template <typename T>
struct OpenMPPointerLikeModel
    : public mlir::omp::PointerLikeType::ExternalModel<
          OpenMPPointerLikeModel<T>, T> {
  mlir::Type getElementType(mlir::Type pointer) const {
    return mlir::cast<T>(pointer).getPointee();
  }
};

} // namespace cir::omp

#endif // CLANG_CIR_DIALECT_OPENMP_CIROPENMPTYPEINTERFACES_H
