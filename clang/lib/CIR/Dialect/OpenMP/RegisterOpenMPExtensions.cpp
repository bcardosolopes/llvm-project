//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Registration for OpenMP extensions as applied to CIR dialect.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/OpenMP/RegisterOpenMPExtensions.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/Dialect/OpenMP/CIROpenMPTypeInterfaces.h"

namespace cir::omp {

void registerOpenMPExtensions(mlir::DialectRegistry &registry) {
  registry.addExtension(+[](mlir::MLIRContext *ctx, cir::CIRDialect *dialect) {
    cir::PointerType::attachInterface<
        OpenMPPointerLikeModel<cir::PointerType>>(*ctx);
  });
}

} // namespace cir::omp
