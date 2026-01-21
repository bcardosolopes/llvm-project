//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with code generation of C++ declarations
//
//===----------------------------------------------------------------------===//

#include "CIRGenModule.h"
#include "clang/AST/Attr.h"
#include "clang/Basic/LangOptions.h"

using namespace clang;
using namespace clang::CIRGen;

void CIRGenModule::emitCXXGlobalVarDeclInitFunc(const VarDecl *vd,
                                                cir::GlobalOp addr,
                                                bool performInit) {
  // According to E.2.3.1 in CUDA-7.5 Programming guide: __device__,
  // __constant__ and __shared__ variables defined in namespace scope,
  // that are of class type, cannot have a non-empty constructor. All
  // the checks have been done in Sema by now. Whatever initializers
  // are allowed are empty and we just need to ignore them here.
  if (getLangOpts().CUDAIsDevice && !getLangOpts().GPUAllowDeviceInit &&
      (vd->hasAttr<CUDADeviceAttr>() || vd->hasAttr<CUDAConstantAttr>() ||
       vd->hasAttr<CUDASharedAttr>()))
    return;

  assert(!cir::MissingFeatures::deferredCXXGlobalInit());

  emitCXXGlobalVarDeclInit(vd, addr, performInit);
}
