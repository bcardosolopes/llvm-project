//====- CIROpInterfaces.cpp - Interface to AST Attributes ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the interface to CIR operations.
//
//===----------------------------------------------------------------------===//
#include "clang/CIR/Interfaces/CIROpInterfaces.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"

using namespace cir;

/// Include the generated type qualifiers interfaces.
#include "clang/CIR/Interfaces/CIROpInterfaces.cpp.inc"

bool CIRGlobalValueInterface::hasDefaultVisibility() {
  if (auto globalOp = mlir::dyn_cast<cir::GlobalOp>(getOperation()))
    return globalOp.getGlobalVisibility() == cir::VisibilityKind::Default;
  if (auto funcOp = mlir::dyn_cast<cir::FuncOp>(getOperation()))
    return funcOp.getGlobalVisibility() == cir::VisibilityKind::Default;
  return isPublic() || isPrivate();
}

bool CIRGlobalValueInterface::canBenefitFromLocalAlias() {
  // hasComdat here should be isDeduplicateComdat, but as far as clang codegen
  // is concerned, there is no case for Comdat::NoDeduplicate as all comdat
  // would be Comdat::Any or Comdat::Largest (in the case of MS ABI). And CIRGen
  // wouldn't even generate Comdat::Largest comdat as it tries to leave ABI
  // specifics to LLVM lowering stage, thus here we don't need test Comdat
  // selectionKind.
  return hasDefaultVisibility() && hasExternalLinkage() && !isDeclaration() &&
         !hasComdat();
}
