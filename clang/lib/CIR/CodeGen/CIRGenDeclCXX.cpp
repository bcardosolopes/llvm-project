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

#include "CIRGenCXXABI.h"
#include "CIRGenFunction.h"
#include "CIRGenModule.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Linkage.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/Support/SaveAndRestore.h"

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

  // Check if we've already initialized this decl (OGCG uses ~0U sentinel).
  auto delayedIt = delayedCXXInitPosition.find(vd);
  if (delayedIt != delayedCXXInitPosition.end() && delayedIt->second == ~0U)
    return;

  // Determine the init order for this variable. If it was deferred, use the
  // reserved position. Otherwise, assign the next available position. This
  // ensures _GLOBAL__sub_I_ calls init functions in TU declaration order.
  unsigned initOrder;
  if (delayedIt != delayedCXXInitPosition.end()) {
    initOrder = delayedIt->second;
  } else {
    initOrder = cxxGlobalInitOrder++;
  }

  // Store the init order on the GlobalOp so LoweringPrepare can sort
  // the init functions when building _GLOBAL__sub_I_.
  addr->setAttr("cir.global_init_order",
                mlir::IntegerAttr::get(
                    mlir::IntegerType::get(addr.getContext(), 32), initOrder));

  // Mark this decl as already initialized.
  delayedCXXInitPosition[vd] = ~0U;

  // Handle MSVC init_seg pragma by mapping known segment names to the same
  // global ctor priorities used by OGCG.
  //
  // OGCG also supports arbitrary init_seg sections by emitting explicit
  // pointers to init functions in those sections. CIR currently supports the
  // common priority-mapped segments.
  if (const auto *isa = vd->getAttr<InitSegAttr>()) {
    int priority = -1;
    if (isa->getSection() == ".CRT$XCC")
      priority = 200;
    else if (isa->getSection() == ".CRT$XCL")
      priority = 400;

    if (priority != -1)
      addr.setInitPriorityAttr(builder.getI32IntegerAttr(priority));
  }
  // OGCG tracks TLS variables in CXXThreadLocalInits/CXXThreadLocalInitVars
  // for thread-local initialization function generation. CIR tracks dynamic
  // TLS vars in cxxThreadLocals; wrappers are emitted in CIRGen and patched in
  // LoweringPrepare to call _ZTH aliases / __tls_init on first access.
  // OGCG's EmitCXXModuleInitFunc handles C++20 module initialization.
  // CIR defers module init function emission to LoweringPrepare.

  // Compute COMDAT key for vague-linkage globals. If the target supports
  // COMDAT and the variable is externally visible, the init function and
  // global_ctors entry are associated with the variable's COMDAT group.
  bool hasCOMDATKey =
      supportsCOMDAT() && vd->isExternallyVisible();

  // OGCG checks if the global has weak/linkonce linkage or is a dynamic TLS
  // variable with template instantiation, and routes to EmitCXXGuardedInit
  // instead of EmitCXXGlobalVarDeclInit. This prevents double initialization
  // when the same linkonce_odr variable appears in multiple TUs.
  auto linkage = addr.getLinkage();
  bool needsGuardedInit =
      cir::isWeakLinkage(linkage) || cir::isLinkOnceLinkage(linkage);
  bool isTLSTemplateInstantiation =
      vd->getTLSKind() == VarDecl::TLS_Dynamic &&
      isTemplateInstantiation(vd->getTemplateSpecializationKind());
  if (!needsGuardedInit && isTLSTemplateInstantiation)
    needsGuardedInit = true;

  if (needsGuardedInit) {
    // Guard against double emission (same guard as emitDeclInit).
    if (!addr.getCtorRegion().empty())
      return;

    // Set up CIRGenFunction context for emitting into the ctorRegion.
    CIRGenFunction cgf{*this, builder, true};
    llvm::SaveAndRestore<CIRGenFunction *> savedCGF(curCGF, &cgf);
    curCGF->curFn = addr;
    CIRGenFunction::SourceLocRAIIObject fnLoc{cgf,
                                              getLoc(vd->getLocation())};

    // Create the ctorRegion block. emitGuardedInit will emit guard variable
    // check + init + dtor registration into this block.
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *block = builder.createBlock(&addr.getCtorRegion());
    CIRGenFunction::LexicalScope lexScope{cgf, addr.getLoc(),
                                          builder.getInsertionBlock()};
    lexScope.setAsGlobalInit();
    builder.setInsertionPointToStart(block);

    getCXXABI().emitGuardedInit(cgf, *vd, addr, performInit);

    // Terminate the ctorRegion block. emitGuardedInit emits guard code +
    // IfOps, but doesn't add the final YieldOp for the ctorRegion.
    builder.setInsertionPointToEnd(block);
    cir::YieldOp::create(builder, addr.getLoc());

    // Mark dynamic TLS template instantiations so LoweringPrepare can classify
    // them as unordered, matching OGCG's UnorderedInits handling even when the
    // variable has internal linkage and therefore no COMDAT key.
    if (isTLSTemplateInstantiation)
      addr->setAttr("cir.tls_template_instantiation",
                    mlir::UnitAttr::get(addr.getContext()));

    // For template/vague-linkage globals: associate the init function with
    // the variable's COMDAT group and add to @llvm.used to prevent linker GC.
    if (hasCOMDATKey &&
        (isTemplateInstantiation(vd->getTemplateSpecializationKind()) ||
         !isUniqueGVALinkage(astContext.GetGVALinkageForVariable(vd)) ||
         vd->hasAttr<SelectAnyAttr>())) {
      // TLS init functions go through __tls_init, not global_ctors, so they
      // don't need comdat on the init function or @llvm.used (matching OGCG
      // which adds TLS inits to CXXThreadLocalInits and returns early before
      // the comdat/AddGlobalCtor/addUsedGlobal logic).
      if (!vd->getTLSKind()) {
        // Store the comdat group name on the GlobalOp so LoweringPrepare can
        // set it on the init function.
        addr->setAttr("comdat_name", getStringAttrCached(addr.getSymName()));
        // Add to @llvm.used on ELF/MSVC to prevent linker GC.
        if (getTriple().isOSBinFormatELF() ||
            getTarget().getCXXABI().isMicrosoft())
          addUsedGlobal(addr);
      }
    }
    return;
  }

  // For template instantiations, non-unique GVA linkage, or SelectAny
  // variables that don't need guarded init (e.g. anonymous namespace templates
  // with internal linkage), register as a separate unordered global_ctors
  // entry (with ptr null key) instead of bundling into _GLOBAL__sub_I_.
  // This matches OGCG's AddGlobalCtor call at CGDeclCXX.cpp:639.
  if (isTemplateInstantiation(vd->getTemplateSpecializationKind()) ||
      !isUniqueGVALinkage(astContext.GetGVALinkageForVariable(vd)) ||
      vd->hasAttr<SelectAnyAttr>()) {
    addr->setAttr("cir.unordered_init",
                   mlir::UnitAttr::get(addr.getContext()));
  }

  emitCXXGlobalVarDeclInit(vd, addr, performInit);
}

