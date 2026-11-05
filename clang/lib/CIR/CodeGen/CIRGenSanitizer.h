//===--- CIRGenSanitizer.h - Shared CIRGen sanitizer helpers ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_LIB_CIR_CODEGEN_CIRGENSANITIZER_H
#define CLANG_LIB_CIR_CODEGEN_CIRGENSANITIZER_H

#include "CIRGenFunction.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "llvm/Support/MathExtras.h"

#include <cstdint>

namespace clang::CIRGen {

inline const CXXRecordDecl *
leastDerivedClassWithSameLayout(const CXXRecordDecl *rd) {
  if (!rd->field_empty())
    return rd;

  if (rd->getNumVBases() != 0)
    return rd;

  if (rd->getNumBases() != 1)
    return rd;

  for (const CXXMethodDecl *md : rd->methods()) {
    if (md->isVirtual()) {
      if (isa<CXXDestructorDecl>(md) && md->isImplicit())
        continue;
      return rd;
    }
  }

  return leastDerivedClassWithSameLayout(
      rd->bases_begin()->getType()->getAsCXXRecordDecl());
}

inline void setUBSanSourceLocationAttrs(CIRGenFunction &cgf,
                                        cir::CIRCallOpInterface call,
                                        mlir::Location loc) {
  auto fileLoc = mlir::dyn_cast<mlir::FileLineColLoc>(loc);
  if (!fileLoc)
    fileLoc = loc->findInstanceOf<mlir::FileLineColLoc>();
  if (!fileLoc)
    return;

  CIRGenBuilderTy &builder = cgf.getBuilder();
  call->setDiscardableAttr("cir.ubsan.source_file", fileLoc.getFilename());
  call->setDiscardableAttr("cir.ubsan.source_line",
                           builder.getI32IntegerAttr(fileLoc.getLine()));
  call->setDiscardableAttr("cir.ubsan.source_column",
                           builder.getI32IntegerAttr(fileLoc.getColumn()));
}

inline void setUBSanSourceLocationAttrs(CIRGenFunction &cgf,
                                        cir::CIRCallOpInterface call,
                                        SourceLocation loc) {
  if (!loc.isValid())
    return;
  setUBSanSourceLocationAttrs(cgf, call, cgf.getLoc(loc));
}

inline void setUBSanTypeNameAttr(CIRGenFunction &cgf,
                                 cir::CIRCallOpInterface call, QualType ty) {
  SmallString<32> typeName;
  cgf.cgm.getDiags().ConvertArgToString(
      DiagnosticsEngine::ak_qualtype, (intptr_t)ty.getAsOpaquePtr(),
      StringRef(), StringRef(), {}, typeName, {});
  call->setDiscardableAttr("cir.ubsan.type_name",
                           cgf.getBuilder().getStringAttr(typeName));
}

inline void setUBSanTypeDescriptorAttrs(CIRGenFunction &cgf,
                                        cir::CIRCallOpInterface call,
                                        QualType ty) {
  uint16_t typeKind = 0xFFFF;
  uint16_t typeInfo = 0;

  if (ty->isIntegerType()) {
    typeKind = 0;
    typeInfo = (llvm::Log2_32(cgf.getContext().getTypeSize(ty)) << 1) |
               (ty->isSignedIntegerType() ? 1 : 0);
  } else if (ty->isFloatingType()) {
    typeKind = 1;
    typeInfo = cgf.getContext().getTypeSize(ty);
  }

  auto *ctx = cgf.getBuilder().getContext();
  auto i16Ty = mlir::IntegerType::get(ctx, 16);
  call->setDiscardableAttr("cir.ubsan.type_kind",
                           mlir::IntegerAttr::get(i16Ty, typeKind));
  call->setDiscardableAttr("cir.ubsan.type_info",
                           mlir::IntegerAttr::get(i16Ty, typeInfo));
}

} // namespace clang::CIRGen

#endif
