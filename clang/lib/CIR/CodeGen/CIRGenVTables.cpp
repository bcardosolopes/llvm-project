//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with C++ code generation of virtual tables.
//
//===----------------------------------------------------------------------===//

#include "CIRGenVTables.h"

#include "CIRGenCXXABI.h"
#include "CIRGenFunction.h"
#include "CIRGenModule.h"
#include "clang/Basic/Linkage.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Types.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/VTTBuilder.h"
#include "clang/AST/VTableBuilder.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

using namespace llvm;
using namespace clang;
using namespace clang::CIRGen;

static cir::GlobalViewAttr
getVTableAddressPointView(CIRGenModule &cgm, mlir::FlatSymbolRefAttr vtableSym,
                          unsigned vtableIndex, unsigned vtableAddressPoint) {
  CIRGenBuilderTy &builder = cgm.getBuilder();
  mlir::Attribute indices[] = {builder.getI32IntegerAttr(vtableIndex),
                               builder.getI32IntegerAttr(vtableAddressPoint)};
  return cir::GlobalViewAttr::get(
      builder.getPointerTo(builder.getSInt32Ty(),
                           cgm.getDefaultGlobalsAddressSpace()),
      vtableSym,
      mlir::ArrayAttr::get(builder.getContext(), indices), /*inbounds=*/true);
}

static cir::GlobalViewDiffAttr
getRelativeVTableEntryAttr(CIRGenModule &cgm, cir::GlobalViewAttr target,
                           mlir::FlatSymbolRefAttr vtableSym,
                           unsigned vtableIndex,
                           unsigned vtableAddressPoint) {
  return cir::GlobalViewDiffAttr::get(
      cgm.getBuilder().getContext(), cgm.getBuilder().getSInt32Ty(), target,
      getVTableAddressPointView(cgm, vtableSym, vtableIndex,
                                vtableAddressPoint));
}

static void markNoSanitizeHWAddress(CIRGenModule &cgm, cir::GlobalOp global) {
  if (!cgm.getLangOpts().Sanitize.has(SanitizerKind::HWAddress))
    return;
  global->setAttr("cir.no_sanitize_hwaddress",
                  mlir::UnitAttr::get(global->getContext()));
}

enum class CIRVCallVisibility : uint64_t {
  Public = 0,
  LinkageUnit = 1,
  TranslationUnit = 2,
};

static CIRVCallVisibility
getCIRVCallVisibilityLevel(CIRGenModule &cgm, const CXXRecordDecl *rd,
                           llvm::DenseSet<const CXXRecordDecl *> &visited) {
  // Match OGCG CodeGenModule::GetVCallVisibilityLevel.
  if (!visited.insert(rd).second)
    return CIRVCallVisibility::TranslationUnit;

  LinkageInfo lv = rd->getLinkageAndVisibility();
  CIRVCallVisibility typeVis;
  if (!isExternallyVisible(lv.getLinkage()))
    typeVis = CIRVCallVisibility::TranslationUnit;
  else if (cgm.hasHiddenLTOVisibility(rd))
    typeVis = CIRVCallVisibility::LinkageUnit;
  else
    typeVis = CIRVCallVisibility::Public;

  for (const auto &base : rd->bases()) {
    const auto *baseRD = base.getType()->getAsCXXRecordDecl();
    if (baseRD && baseRD->isDynamicClass())
      typeVis =
          std::min(typeVis, getCIRVCallVisibilityLevel(cgm, baseRD, visited));
  }

  for (const auto &base : rd->vbases()) {
    const auto *baseRD = base.getType()->getAsCXXRecordDecl();
    if (baseRD && baseRD->isDynamicClass())
      typeVis =
          std::min(typeVis, getCIRVCallVisibilityLevel(cgm, baseRD, visited));
  }

  return typeVis;
}

// Rewrite any symbol reference whose name appears in `renames` to its mapped
// replacement. Accepting a map lets a whole batch of vtable renames be applied
// in a single module walk instead of one walk per rename.
static mlir::Attribute
replaceSymbolInAttr(mlir::Attribute attr,
                    const llvm::StringMap<std::string> &renames) {
  if (!attr)
    return {};

  if (auto ref = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(attr)) {
    auto it = renames.find(ref.getValue());
    if (it != renames.end())
      return mlir::FlatSymbolRefAttr::get(attr.getContext(), it->second);
    return attr;
  }

  if (auto gva = mlir::dyn_cast<cir::GlobalViewAttr>(attr)) {
    auto it = renames.find(gva.getSymbol().getValue());
    if (it != renames.end()) {
      auto newSymbol =
          mlir::FlatSymbolRefAttr::get(attr.getContext(), it->second);
      return cir::GlobalViewAttr::get(gva.getType(), newSymbol,
                                      gva.getIndices(), gva.getRawByteOffset(),
                                      gva.getInRange(), gva.getInbounds(),
                                      gva.getAddrAdjustment());
    }
    return attr;
  }

  if (auto diff = mlir::dyn_cast<cir::GlobalViewDiffAttr>(attr)) {
    auto lhs = mlir::cast<cir::GlobalViewAttr>(
        replaceSymbolInAttr(diff.getLhs(), renames));
    auto rhs = mlir::cast<cir::GlobalViewAttr>(
        replaceSymbolInAttr(diff.getRhs(), renames));
    if (lhs == diff.getLhs() && rhs == diff.getRhs())
      return attr;
    return cir::GlobalViewDiffAttr::get(attr.getContext(), diff.getType(), lhs,
                                        rhs);
  }

  if (auto vtable = mlir::dyn_cast<cir::VTableAttr>(attr)) {
    bool changed = false;
    llvm::SmallVector<mlir::Attribute> newData;
    for (auto datum : vtable.getData()) {
      auto replaced = replaceSymbolInAttr(datum, renames);
      if (replaced != datum)
        changed = true;
      newData.push_back(replaced);
    }
    if (!changed)
      return attr;
    return cir::VTableAttr::get(vtable.getType(),
                                mlir::ArrayAttr::get(attr.getContext(),
                                                     newData));
  }

  if (auto rec = mlir::dyn_cast<cir::ConstRecordAttr>(attr)) {
    bool changed = false;
    llvm::SmallVector<mlir::Attribute> newMembers;
    for (auto member : rec.getMembers()) {
      auto replaced = replaceSymbolInAttr(member, renames);
      if (replaced != member)
        changed = true;
      newMembers.push_back(replaced);
    }
    if (!changed)
      return attr;
    auto membersAttr = mlir::ArrayAttr::get(attr.getContext(), newMembers);
    return cir::ConstRecordAttr::get(mlir::cast<cir::RecordType>(rec.getType()),
                                     membersAttr);
  }

  if (auto arr = mlir::dyn_cast<cir::ConstArrayAttr>(attr)) {
    auto elts = arr.getElts();
    if (auto arrElts = mlir::dyn_cast<mlir::ArrayAttr>(elts)) {
      bool changed = false;
      llvm::SmallVector<mlir::Attribute> newElts;
      for (auto elt : arrElts) {
        auto replaced = replaceSymbolInAttr(elt, renames);
        if (replaced != elt)
          changed = true;
        newElts.push_back(replaced);
      }
      if (!changed)
        return attr;
      auto newEltsAttr = mlir::ArrayAttr::get(attr.getContext(), newElts);
      return cir::ConstArrayAttr::get(mlir::cast<cir::ArrayType>(arr.getType()),
                                      newEltsAttr, arr.getTrailingZerosNum());
    }
    return attr;
  }

  if (auto ti = mlir::dyn_cast<cir::TypeInfoAttr>(attr)) {
    bool changed = false;
    llvm::SmallVector<mlir::Attribute> newData;
    for (auto datum : ti.getData()) {
      auto replaced = replaceSymbolInAttr(datum, renames);
      if (replaced != datum)
        changed = true;
      newData.push_back(replaced);
    }
    if (!changed)
      return attr;
    auto dataAttr = mlir::ArrayAttr::get(attr.getContext(), newData);
    return cir::TypeInfoAttr::get(ti.getType(), dataAttr);
  }

  if (auto arr = mlir::dyn_cast<mlir::ArrayAttr>(attr)) {
    bool changed = false;
    llvm::SmallVector<mlir::Attribute> newElts;
    for (auto elt : arr) {
      auto replaced = replaceSymbolInAttr(elt, renames);
      if (replaced != elt)
        changed = true;
      newElts.push_back(replaced);
    }
    if (!changed)
      return attr;
    return mlir::ArrayAttr::get(attr.getContext(), newElts);
  }

  if (auto dict = mlir::dyn_cast<mlir::DictionaryAttr>(attr)) {
    bool changed = false;
    llvm::SmallVector<mlir::NamedAttribute> newAttrs;
    for (auto namedAttr : dict) {
      auto replaced = replaceSymbolInAttr(namedAttr.getValue(), renames);
      if (replaced != namedAttr.getValue())
        changed = true;
      newAttrs.emplace_back(namedAttr.getName(), replaced);
    }
    if (!changed)
      return attr;
    return mlir::DictionaryAttr::get(attr.getContext(), newAttrs);
  }

  return attr;
}

static void replaceSymbolUsesInModule(CIRGenModule &cgm,
                                      const llvm::StringMap<std::string> &renames) {
  if (renames.empty())
    return;
  cgm.getModule().walk([&](mlir::Operation *op) {
    bool changed = false;
    llvm::SmallVector<mlir::NamedAttribute> newAttrs;
    newAttrs.reserve(op->getAttrs().size());
    for (auto namedAttr : op->getAttrs()) {
      auto replaced = replaceSymbolInAttr(namedAttr.getValue(), renames);
      if (replaced != namedAttr.getValue())
        changed = true;
      newAttrs.emplace_back(namedAttr.getName(), replaced);
    }
    if (changed)
      op->setAttrs(mlir::DictionaryAttr::get(op->getContext(), newAttrs));
  });
}

CIRGenVTables::CIRGenVTables(CIRGenModule &cgm)
    : cgm(cgm), vtContext(cgm.getASTContext().getVTableContext()) {}

mlir::Type CIRGenModule::getVTableComponentType() {
  if (getItaniumVTableContext().isRelativeLayout())
    return builder.getSInt32Ty();
  return getDefaultGlobalsInt8PtrTy();
}

mlir::Type CIRGenVTables::getVTableComponentType() {
  return cgm.getVTableComponentType();
}

cir::RecordType CIRGenVTables::getVTableType(const VTableLayout &layout) {
  SmallVector<mlir::Type, 4> tys;
  mlir::Type componentType = getVTableComponentType();
  for (unsigned i = 0, e = layout.getNumVTables(); i != e; ++i)
    tys.push_back(cir::ArrayType::get(componentType, layout.getVTableSize(i)));

  // FIXME(cir): should VTableLayout be encoded like we do for some
  // AST nodes?
  return cgm.getBuilder().getAnonRecordTy(tys, /*incomplete=*/false);
}

/// At this point in the translation unit, does it appear that can we
/// rely on the vtable being defined elsewhere in the program?
///
/// The response is really only definitive when called at the end of
/// the translation unit.
///
/// The only semantic restriction here is that the object file should
/// not contain a vtable definition when that vtable is defined
/// strongly elsewhere.  Otherwise, we'd just like to avoid emitting
/// vtables when unnecessary.
/// TODO(cir): this should be merged into common AST helper for codegen.
bool CIRGenVTables::isVTableExternal(const CXXRecordDecl *rd) {
  assert(rd->isDynamicClass() && "Non-dynamic classes have no VTable.");

  // We always synthesize vtables if they are needed in the MS ABI. MSVC doesn't
  // emit them even if there is an explicit template instantiation.
  if (cgm.getTarget().getCXXABI().isMicrosoft())
    return false;

  // If we have an explicit instantiation declaration (and not a
  // definition), the vtable is defined elsewhere.
  TemplateSpecializationKind tsk = rd->getTemplateSpecializationKind();
  if (tsk == TSK_ExplicitInstantiationDeclaration)
    return true;

  // Otherwise, if the class is an instantiated template, the
  // vtable must be defined here.
  if (tsk == TSK_ImplicitInstantiation ||
      tsk == TSK_ExplicitInstantiationDefinition)
    return false;

  // Otherwise, if the class is attached to a module, the tables are uniquely
  // emitted in the object for the module unit in which it is defined.
  if (rd->isInNamedModule())
    return rd->shouldEmitInExternalSource();

  // Otherwise, if the class doesn't have a key function (possibly
  // anymore), the vtable must be defined here.
  const CXXMethodDecl *keyFunction =
      cgm.getASTContext().getCurrentKeyFunction(rd);
  if (!keyFunction)
    return false;

  // Otherwise, if we don't have a definition of the key function, the
  // vtable must be defined somewhere else.
  return !keyFunction->hasBody();
}

/// This is a callback from Sema to tell us that a particular vtable is
/// required to be emitted in this translation unit.
///
/// This is only called for vtables that _must_ be emitted (mainly due to key
/// functions).  For weak vtables, CodeGen tracks when they are needed and
/// emits them as-needed.
void CIRGenModule::emitVTable(const CXXRecordDecl *rd) {
  vtables.generateClassData(rd);
}

static bool shouldEmitAvailableExternallyVTable(const CIRGenModule &cgm,
                                                const CXXRecordDecl *rd) {
  return cgm.getCodeGenOpts().OptimizationLevel > 0 &&
         cgm.getCXXABI().canSpeculativelyEmitVTable(rd);
}

/// Given that we're currently at the end of the translation unit, and
/// we've emitted a reference to the vtable for this class, should
/// we define that vtable?
static bool shouldEmitVTableAtEndOfTranslationUnit(CIRGenModule &cgm,
                                                   const CXXRecordDecl *rd) {
  // If vtable is internal then it has to be done.
  if (!cgm.getVTables().isVTableExternal(rd))
    return true;

  // If it's external then maybe we will need it as available_externally.
  return shouldEmitAvailableExternallyVTable(cgm, rd);
}

/// Given that at some point we emitted a reference to one or more
/// vtables, and that we are now at the end of the translation unit,
/// decide whether we should emit them.
void CIRGenModule::emitDeferredVTables() {
#ifndef NDEBUG
  // Remember the size of deferredVTables, because we're going to assume
  // that this entire operation doesn't modify it.
  size_t savedSize = deferredVTables.size();
#endif

  for (const CXXRecordDecl *rd : deferredVTables)
    if (shouldEmitVTableAtEndOfTranslationUnit(*this, rd))
      vtables.generateClassData(rd);
    else if (shouldOpportunisticallyEmitVTables())
      opportunisticVTables.push_back(rd);

  assert(savedSize == deferredVTables.size() &&
         "deferred extra vtables during vtable emission?");
  deferredVTables.clear();
}

void CIRGenVTables::generateClassData(const CXXRecordDecl *rd) {
  if (rd->getNumVBases())
    cgm.getCXXABI().emitVirtualInheritanceTables(rd);

  cgm.getCXXABI().emitVTableDefinitions(*this, rd);
}

mlir::Attribute CIRGenVTables::getVTableComponent(
    const VTableLayout &layout, unsigned componentIndex, mlir::Attribute rtti,
    unsigned &nextVTableThunkIndex, unsigned vtableIndex,
    unsigned componentIndexInVTable, unsigned vtableAddressPoint,
    bool vtableHasLocalLinkage, mlir::FlatSymbolRefAttr vtableSym,
    llvm::SmallVectorImpl<mlir::Attribute> *ptrauthEntries) {
  const VTableComponent &component = layout.vtable_components()[componentIndex];

  CIRGenBuilderTy &builder = cgm.getBuilder();

  bool useRelativeLayout = cgm.getItaniumVTableContext().isRelativeLayout();
  auto globalsInt8PtrTy = cgm.getDefaultGlobalsInt8PtrTy();

  // On targets with a nonzero global address space (e.g., AMDGCN AS1), vtable
  // entries must use that address space to match OGCG behavior. For relative
  // layout, the component type is i32 rather than a pointer.
  auto compType = cgm.getVTableComponentType();
  auto compPtrTy = mlir::dyn_cast<cir::PointerType>(compType);

  switch (component.getKind()) {
  case VTableComponent::CK_UnusedFunctionPointer:
    if (useRelativeLayout)
      return cir::IntAttr::get(builder.getSInt32Ty(), 0);
    return builder.getConstPtrAttr(compPtrTy, 0);

  case VTableComponent::CK_VCallOffset:
    if (useRelativeLayout)
      return cir::IntAttr::get(builder.getSInt32Ty(),
                               component.getVCallOffset().getQuantity());
    return builder.getConstPtrAttr(compPtrTy,
                                   component.getVCallOffset().getQuantity());

  case VTableComponent::CK_VBaseOffset:
    if (useRelativeLayout)
      return cir::IntAttr::get(builder.getSInt32Ty(),
                               component.getVBaseOffset().getQuantity());
    return builder.getConstPtrAttr(compPtrTy,
                                   component.getVBaseOffset().getQuantity());

  case VTableComponent::CK_OffsetToTop:
    if (useRelativeLayout)
      return cir::IntAttr::get(builder.getSInt32Ty(),
                               component.getOffsetToTop().getQuantity());
    return builder.getConstPtrAttr(compPtrTy,
                                   component.getOffsetToTop().getQuantity());

  case VTableComponent::CK_RTTI:
    assert((mlir::isa<cir::GlobalViewAttr>(rtti) ||
            mlir::isa<cir::ConstPtrAttr>(rtti)) &&
           "expected GlobalViewAttr or ConstPtrAttr");
    if (useRelativeLayout) {
      auto rttiView = mlir::dyn_cast<cir::GlobalViewAttr>(rtti);
      if (!rttiView)
        return cir::IntAttr::get(builder.getSInt32Ty(), 0);

      // Always reference the RTTI through a dso_local proxy, matching classic
      // codegen (CodeGenVTables::addRelativeComponent). The RTTI may live in a
      // different linkage unit than the vtable -- e.g. an extern-template base's
      // typeinfo referenced by a construction vtable -- so a direct relative
      // reference would emit an R_*_PREL32 against an external symbol and fail
      // to link a shared object. The proxy is dso_local, so the relative offset
      // targets a local symbol; the linker rewrites it to a GOTPCREL relocation
      // when the real RTTI is preemptible. The proxy's linkage follows the
      // vtable's: internal for local-linkage vtables (such as construction
      // vtables), linkonce_odr otherwise.
      SmallString<64> proxyName(rttiView.getSymbol().getValue());
      proxyName.append(".rtti_proxy");
      auto proxy =
          mlir::dyn_cast_or_null<cir::GlobalOp>(cgm.getGlobalValue(proxyName));
      if (!proxy) {
        mlir::Location loc = mlir::UnknownLoc::get(builder.getContext());
        cir::GlobalLinkageKind linkage =
            vtableHasLocalLinkage ? cir::GlobalLinkageKind::InternalLinkage
                                  : cir::GlobalLinkageKind::LinkOnceODRLinkage;
        llvm::Align align =
            cgm.getDataLayout().getABITypeAlign(globalsInt8PtrTy);
        proxy = cgm.createOrReplaceCXXRuntimeVariable(
            loc, proxyName, globalsInt8PtrTy, linkage,
            CharUnits::fromQuantity(align),
            cgm.getDefaultGlobalsAddressSpace());
        cgm.setInitializer(proxy, rttiView);
        proxy->setAttr("unnamed_addr",
                       mlir::UnitAttr::get(builder.getContext()));
        cgm.setDSOLocal(static_cast<mlir::Operation *>(proxy));
        if (!cir::isLocalLinkage(linkage)) {
          proxy.setGlobalVisibility(cir::VisibilityKind::Hidden);
          if (cgm.supportsCOMDAT())
            proxy.setComdat(true);
        }
      }
      assert(proxy && "relative RTTI proxy must be a cir.global");
      markNoSanitizeHWAddress(cgm, proxy);
      cir::GlobalViewAttr targetView = cir::GlobalViewAttr::get(
          globalsInt8PtrTy,
          mlir::FlatSymbolRefAttr::get(builder.getContext(),
                                       proxy.getSymName()));
      return getRelativeVTableEntryAttr(cgm, targetView, vtableSym, vtableIndex,
                                        vtableAddressPoint);
    }
    // A null RTTI slot (e.g. device code, which has no RTTI) is created in the
    // default address space, but the other vtable entries use the component
    // pointer type. On targets with a nonzero global address space (AMDGCN AS1)
    // these must match, or the vtable array element types mismatch. Rebuild the
    // null pointer with the component pointer type to match OGCG.
    if (auto nullRtti = mlir::dyn_cast<cir::ConstPtrAttr>(rtti))
      if (compPtrTy && nullRtti.getType() != compPtrTy)
        return builder.getConstPtrAttr(compPtrTy,
                                       nullRtti.getValue().getInt());
    return rtti;

  case VTableComponent::CK_FunctionPointer:
  case VTableComponent::CK_CompleteDtorPointer:
  case VTableComponent::CK_DeletingDtorPointer: {
    GlobalDecl gd = component.getGlobalDecl(
        cgm.getASTContext().getTargetInfo().emitVectorDeletingDtors(
            cgm.getASTContext().getLangOpts()));

    // OGCG also checks for OpenMP NVPTX target and emits null pointers
    // instead of runtime function references to avoid unresolved references.
    // CIR currently has no equivalent OpenMP NVPTX vtable runtime hook.

    cir::FuncOp fnPtr;
    if (cast<CXXMethodDecl>(gd.getDecl())->isPureVirtual()) {
      if (useRelativeLayout)
        return cir::IntAttr::get(builder.getSInt32Ty(), 0);
      // For pure virtual functions, emit a reference to __cxa_pure_virtual.
      // OGCG queries CGM.getCXXABI().GetPureVirtualCallName() which returns
      // a different name for the MS ABI (_purecall). CIR uses Itanium ABI.
      if (!pureVirtualFn) {
        cir::FuncType fnTy = builder.getFuncType({}, builder.getVoidTy());
        pureVirtualFn = cgm.createRuntimeFunction(fnTy, "__cxa_pure_virtual");
        // OGCG sets unnamed_addr and does NOT set dso_local on pure/deleted
        // virtual function declarations (CGVTables.cpp:832-833).
        pureVirtualFn.setDSOLocal(false);
        mlir::NamedAttrList attrs;
        if (auto extraAttrs = pureVirtualFn.getExtraAttrs())
          attrs = mlir::NamedAttrList{extraAttrs->getElements().getValue()};
        attrs.set("unnamed_addr",
                  mlir::UnitAttr::get(builder.getContext()));
        pureVirtualFn.setExtraAttrsAttr(
            cir::ExtraFuncAttributesAttr::get(
                builder.getContext(),
                mlir::DictionaryAttr::get(builder.getContext(), attrs)));
      }
      fnPtr = pureVirtualFn;
    } else if (cast<CXXMethodDecl>(gd.getDecl())->isDeleted()) {
      if (useRelativeLayout)
        return cir::IntAttr::get(builder.getSInt32Ty(), 0);
      // For deleted virtual functions, emit a reference to
      // __cxa_deleted_virtual.
      // OGCG queries CGM.getCXXABI().GetDeletedVirtualCallName() which
      // returns a different name for the MS ABI. CIR uses Itanium ABI.
      if (!deletedVirtualFn) {
        cir::FuncType fnTy = builder.getFuncType({}, builder.getVoidTy());
        deletedVirtualFn =
            cgm.createRuntimeFunction(fnTy, "__cxa_deleted_virtual");
        deletedVirtualFn.setDSOLocal(false);
        mlir::NamedAttrList attrs;
        if (auto extraAttrs = deletedVirtualFn.getExtraAttrs())
          attrs = mlir::NamedAttrList{extraAttrs->getElements().getValue()};
        attrs.set("unnamed_addr",
                  mlir::UnitAttr::get(builder.getContext()));
        deletedVirtualFn.setExtraAttrsAttr(
            cir::ExtraFuncAttributesAttr::get(
                builder.getContext(),
                mlir::DictionaryAttr::get(builder.getContext(), attrs)));
      }
      fnPtr = deletedVirtualFn;
    } else if (nextVTableThunkIndex < layout.vtable_thunks().size() &&
               layout.vtable_thunks()[nextVTableThunkIndex].first ==
                   componentIndex) {
      const auto &thunkInfo =
          layout.vtable_thunks()[nextVTableThunkIndex].second;
      nextVTableThunkIndex++;
      fnPtr = maybeEmitThunk(gd, thunkInfo, /*ForVTable=*/true);
      if (cgm.getCodeGenOpts().PointerAuth.CXXVirtualFunctionPointers) {
        assert(thunkInfo.Method && "Method not set");
        gd = gd.getWithDecl(thunkInfo.Method);
      }
    } else {
      // Otherwise we can use the method definition directly.
      cir::FuncType fnTy = cgm.getTypes().getFunctionTypeForVTable(gd);
      fnPtr = cgm.getAddrOfFunction(gd, fnTy, /*ForVTable=*/true);
      if (cgm.getCodeGenOpts().PointerAuth.CXXVirtualFunctionPointers)
        gd = getItaniumVTableContext().findOriginalMethod(gd);
    }

    if (ptrauthEntries) {
      if (const auto &schema =
              cgm.getCodeGenOpts().PointerAuth.CXXVirtualFunctionPointers) {
        auto authMode = schema.getAuthenticationMode();
        if (authMode == PointerAuthenticationMode::SignAndStrip ||
            authMode == PointerAuthenticationMode::SignAndAuth) {
          uint64_t discriminator = 0;
          switch (schema.getOtherDiscrimination()) {
          case PointerAuthSchema::Discrimination::None:
            break;
          case PointerAuthSchema::Discrimination::Type:
            discriminator = cgm.getASTContext().getPointerAuthTypeDiscriminator(
                cast<CXXMethodDecl>(gd.getDecl())->getType());
            break;
          case PointerAuthSchema::Discrimination::Decl:
            discriminator = cgm.getPointerAuthDeclDiscriminator(gd);
            break;
          case PointerAuthSchema::Discrimination::Constant:
            discriminator = schema.getConstantDiscrimination();
            break;
          }

          mlir::Type i64Ty =
              mlir::IntegerType::get(builder.getContext(), 64);
          ptrauthEntries->push_back(builder.getArrayAttr({
              builder.getI32IntegerAttr(vtableIndex),
              builder.getI32IntegerAttr(componentIndexInVTable),
              builder.getI32IntegerAttr(schema.getKey()),
              mlir::IntegerAttr::get(i64Ty, discriminator),
              builder.getBoolAttr(schema.isAddressDiscriminated()),
          }));
        }
      }
    }

    // OGCG sets setUnnamedAddr(Global) on pure/deleted virtual fn decls —
    // handled by setFunctionAttributes for virtual methods.
    // Use the vtable component pointer type (which includes the global address
    // space on targets like AMDGCN). The lowering will emit addrspacecast when
    // the function's native address space differs from the global AS.
    // For relative layout, entries are i32 offsets — use flat pointer for the
    // intermediate GlobalViewAttr since it's converted to a relative offset.
    auto fnViewPtrTy = compPtrTy ? compPtrTy : builder.getUInt8PtrTy();
    auto fnView = cir::GlobalViewAttr::get(
        fnViewPtrTy,
        mlir::FlatSymbolRefAttr::get(fnPtr.getSymNameAttr()));
    if (useRelativeLayout)
      return getRelativeVTableEntryAttr(cgm, fnView, vtableSym, vtableIndex,
                                        vtableAddressPoint);
    return fnView;
  }
  }

  llvm_unreachable("Unexpected vtable component kind");
}

void CIRGenVTables::createVTableInitializer(cir::GlobalOp &vtableOp,
                                            const clang::VTableLayout &layout,
                                            mlir::Attribute rtti,
                                            bool vtableHasLocalLinkage) {
  mlir::Type componentType = getVTableComponentType();

  const llvm::SmallVectorImpl<unsigned> &addressPoints =
      layout.getAddressPointIndices();
  unsigned nextVTableThunkIndex = 0;

  mlir::MLIRContext *mlirContext = &cgm.getMLIRContext();
  SmallVector<mlir::Attribute> ptrauthEntries;

  SmallVector<mlir::Attribute> vtables;
  for (auto [vtableIndex, addressPoint] : llvm::enumerate(addressPoints)) {
    // Build a ConstArrayAttr of the vtable components.
    size_t vtableStart = layout.getVTableOffset(vtableIndex);
    size_t vtableEnd = vtableStart + layout.getVTableSize(vtableIndex);
    llvm::SmallVector<mlir::Attribute> components;
    components.reserve(vtableEnd - vtableStart);
    for (auto [componentIndexInVTable, componentIndex] :
         llvm::enumerate(llvm::seq(vtableStart, vtableEnd)))
      components.push_back(
          getVTableComponent(layout, componentIndex, rtti, nextVTableThunkIndex,
                             vtableIndex, componentIndexInVTable, addressPoint,
                             vtableHasLocalLinkage,
                             mlir::FlatSymbolRefAttr::get(
                                 vtableOp.getSymNameAttr()),
                             &ptrauthEntries));
    // Create a ConstArrayAttr to hold the components.
    auto arr = cir::ConstArrayAttr::get(
        cir::ArrayType::get(componentType, components.size()),
        mlir::ArrayAttr::get(mlirContext, components));
    vtables.push_back(arr);
  }

  // Create a ConstRecordAttr to hold the component array.
  const auto members = mlir::ArrayAttr::get(mlirContext, vtables);
  cir::ConstRecordAttr record = cgm.getBuilder().getAnonConstRecord(members);

  // Create a VTableAttr
  auto vtableAttr = cir::VTableAttr::get(record.getType(), record.getMembers());

  // Add the vtable initializer to the vtable global op.
  cgm.setInitializer(vtableOp, vtableAttr);

  if (!ptrauthEntries.empty())
    vtableOp->setAttr("cir.vtable_vfunc_ptrauth",
                      cgm.getBuilder().getArrayAttr(ptrauthEntries));
}

void CIRGenVTables::generateRelativeVTableAlias(cir::GlobalOp vtable,
                                                llvm::StringRef aliasNameRef) {
  assert(getItaniumVTableContext().isRelativeLayout() &&
         "Can only use this if the relative vtable ABI is used");
  assert((!vtable.getDsoLocal() || vtable.getComdat()) &&
         "This should be called only if the vtable is not guaranteed to be "
         "dso_local or is in a COMDAT group");

  if (vtable.getLinkage() == cir::GlobalLinkageKind::AvailableExternallyLinkage)
    return;

  llvm::SmallString<256> aliasName(aliasNameRef);
  llvm::SmallString<256> localName(aliasName);
  localName.append(".local");

  unsigned suffix = 0;
  while (mlir::Operation *existing = cgm.getGlobalValue(localName)) {
    if (existing == vtable.getOperation())
      break;
    localName = aliasName;
    localName.append(".local.");
    localName.append(llvm::utostr(++suffix));
  }

  llvm::StringRef oldName = vtable.getSymName();
  if (oldName != localName) {
    cgm.eraseGlobalSymbol(vtable);
    vtable.setSymName(localName);
    cgm.insertGlobalSymbol(vtable);
    // Defer rewriting module-wide references to oldName until all vtables are
    // emitted, then apply every rename in a single module walk
    // (applyDeferredRelativeVTableRenames). Walking per alias is
    // O(aliases * module-size); a public alias named oldName forwards to the
    // renamed local symbol in the meantime, so deferral is reference-safe.
    deferredRelativeVTableRenames[oldName] = localName.str().str();
  }

  if (!cgm.getGlobalValue(aliasName)) {
    mlir::OpBuilder::InsertionGuard guard(cgm.getBuilder());
    cgm.getBuilder().setInsertionPointAfter(vtable);
    auto alias = cir::AliasOp::create(
        cgm.getBuilder(), vtable.getLoc(), aliasName, vtable.getSymName(),
        vtable.getSymType(), vtable.getLinkage(), vtable.getGlobalVisibility(),
        /*dso_local=*/false, /*weak=*/false, /*is_ifunc=*/false,
        /*tls_model=*/cir::TLS_ModelAttr{});
    alias->setAttr("unnamed_addr", mlir::UnitAttr::get(alias->getContext()));
    cgm.insertGlobalSymbol(alias);
  }

  if (!vtable.getComdat()) {
    vtable.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    vtable.setGlobalVisibility(cir::VisibilityKind::Default);
  } else {
    vtable.setGlobalVisibility(cir::VisibilityKind::Hidden);
  }
  mlir::SymbolTable::setSymbolVisibility(
      vtable, CIRGenModule::getMLIRVisibility(vtable));
  cgm.setDSOLocal(static_cast<mlir::Operation *>(vtable));
}

void CIRGenVTables::applyDeferredRelativeVTableRenames() {
  replaceSymbolUsesInModule(cgm, deferredRelativeVTableRenames);
  deferredRelativeVTableRenames.clear();
}

cir::GlobalOp CIRGenVTables::generateConstructionVTable(
    const CXXRecordDecl *rd, const BaseSubobject &base, bool baseIsVirtual,
    cir::GlobalLinkageKind linkage, VTableAddressPointsMapTy &addressPoints) {
  std::unique_ptr<VTableLayout> vtLayout(
      getItaniumVTableContext().createConstructionVTableLayout(
          base.getBase(), base.getBaseOffset(), baseIsVirtual, rd));

  // Add the address points.
  addressPoints = vtLayout->getAddressPoints();

  // Get the mangled construction vtable name.
  SmallString<256> outName;
  llvm::raw_svector_ostream out(outName);
  cast<ItaniumMangleContext>(cgm.getCXXABI().getMangleContext())
      .mangleCXXCtorVTable(rd, base.getBaseOffset().getQuantity(),
                           base.getBase(), out);
  SmallString<256> name(outName);

  if (getItaniumVTableContext().isRelativeLayout())
    if (mlir::isa_and_present<cir::AliasOp>(cgm.getGlobalValue(name)))
      name.append(".local");

  cir::RecordType vtType = getVTableType(*vtLayout);

  // Construction vtable symbols are not part of the Itanium ABI, so we cannot
  // guarantee that they actually will be available externally. Instead, when
  // emitting an available_externally VTT, we provide references to an internal
  // linkage construction vtable. The ABI only requires complete-object vtables
  // to be the same for all instances of a type, not construction vtables.
  if (linkage == cir::GlobalLinkageKind::AvailableExternallyLinkage)
    linkage = cir::GlobalLinkageKind::InternalLinkage;

  llvm::Align align = cgm.getDataLayout().getABITypeAlign(vtType);
  mlir::Location loc = cgm.getLoc(rd->getSourceRange());

  // Create the variable that will hold the construction vtable.
  cir::GlobalOp vtable = cgm.createOrReplaceCXXRuntimeVariable(
      loc, name, vtType, linkage, CharUnits::fromQuantity(align),
      cgm.getDefaultGlobalsAddressSpace());

  // V-tables are always unnamed_addr — their address identity is never
  // semantically significant. Match OGCG's generateConstructionVTable behavior.
  vtable->setAttr("unnamed_addr",
                  mlir::UnitAttr::get(vtable->getContext()));

  mlir::Attribute rtti = cgm.getAddrOfRTTIDescriptor(
      loc, cgm.getASTContext().getCanonicalTagType(base.getBase()));

  // Create and set the initializer.
  createVTableInitializer(vtable, *vtLayout, rtti,
                          cir::isLocalLinkage(vtable.getLinkage()));

  // Set properties only after the initializer has been set to ensure that the
  // GV is treated as definition and not declaration.
  assert(!vtable.isDeclaration() && "Shouldn't set properties on declaration");
  cgm.setGVProperties(vtable, rd);

  // Carry vtable type metadata for the same cases as OGCG's
  // EmitVTableTypeMetadata: LTO, IR instrumentation, or speculative
  // devirtualization.
  if (cgm.getCodeGenOpts().LTOUnit ||
      cgm.getCodeGenOpts().hasProfileIRInstr() ||
      cgm.getCodeGenOpts().DevirtualizeSpeculatively) {
    SmallVector<mlir::Attribute, 8> typeEntries;
    const auto &layout = *vtLayout;
    const auto &addressPoints = layout.getAddressPoints();
    unsigned componentSize =
        cgm.getDataLayout().getTypeAllocSize(cgm.getVTableComponentType());

    struct AddressPointInfo {
      const CXXRecordDecl *base;
      size_t offset;
      std::string typeName;
      bool operator<(const AddressPointInfo &rhs) const {
        int cmp = typeName.compare(rhs.typeName);
        return cmp < 0 || (cmp == 0 && offset < rhs.offset);
      }
    };

    SmallVector<AddressPointInfo, 8> sortedAddressPoints;
    sortedAddressPoints.reserve(addressPoints.size());
    for (auto &&ap : addressPoints) {
      AddressPointInfo info{ap.first.getBase(),
                            layout.getVTableOffset(ap.second.VTableIndex) +
                                ap.second.AddressPointIndex,
                            {}};
      llvm::raw_string_ostream nameOut(info.typeName);
      CanQualType baseTy =
          cgm.getASTContext().getCanonicalTagType(info.base);
      cgm.getCXXABI().getMangleContext().mangleCanonicalTypeName(baseTy,
                                                                 nameOut);
      sortedAddressPoints.push_back(std::move(info));
    }
    llvm::sort(sortedAddressPoints);

    auto i64Ty = mlir::IntegerType::get(&cgm.getMLIRContext(), 64);
    bool crossDso = cgm.getCodeGenOpts().SanitizeCfiCrossDso;
    bool allVtables = cgm.needAllVtablesTypeId();
    for (const auto &ap : sortedAddressPoints) {
      bool isPublicTypeId = ap.base->isExternallyVisible();
      auto offAttr =
          mlir::IntegerAttr::get(i64Ty, componentSize * ap.offset);
      auto pair = mlir::ArrayAttr::get(
          &cgm.getMLIRContext(),
          {offAttr, cgm.getStringAttrCached(ap.typeName),
           cgm.getStringAttrCached(isPublicTypeId ? "public" : "hidden")});
      typeEntries.push_back(pair);

      // Match OGCG CodeGenModule::AddVTableTypeMetadata: under cross-DSO CFI,
      // attach the numeric type id (MD5 hash of the mangled type name) at the
      // same address-point offset.
      if (crossDso) {
        auto crossDsoPair = mlir::ArrayAttr::get(
            &cgm.getMLIRContext(),
            {offAttr, cgm.getStringAttrCached(ap.typeName),
             cgm.getStringAttrCached("crossdso")});
        typeEntries.push_back(crossDsoPair);
      }

      // Match OGCG CodeGenModule::AddVTableTypeMetadata: when a non-trapping
      // vtable-based CFI checker is enabled, attach the "all-vtables" bitset
      // membership at each address-point offset.
      if (allVtables) {
        auto allVtPair = mlir::ArrayAttr::get(
            &cgm.getMLIRContext(),
            {offAttr, cgm.getStringAttrCached("all-vtables"),
             cgm.getStringAttrCached("public")});
        typeEntries.push_back(allVtPair);
      }
    }

    ArrayRef<VTableComponent> comps = layout.vtable_components();
    for (const auto &ap : sortedAddressPoints) {
      for (unsigned i = 0, e = comps.size(); i != e; ++i) {
        if (comps[i].getKind() != VTableComponent::CK_FunctionPointer)
          continue;
        QualType memPtrTy = cgm.getASTContext().getMemberPointerType(
            comps[i].getFunctionDecl()->getType(), std::nullopt,
            ap.base->getCanonicalDecl());
        std::string typeName;
        llvm::raw_string_ostream nameOut(typeName);
        cgm.getCXXABI().getMangleContext().mangleCanonicalTypeName(memPtrTy,
                                                                    nameOut);
        nameOut << ".virtual";
        bool isPublicTypeId = ap.base->isExternallyVisible();
        auto pair = mlir::ArrayAttr::get(
            &cgm.getMLIRContext(),
            {mlir::IntegerAttr::get(i64Ty, componentSize * i),
             cgm.getStringAttrCached(typeName),
             cgm.getStringAttrCached(isPublicTypeId ? "public" : "hidden")});
        typeEntries.push_back(pair);
      }
    }

    if (!typeEntries.empty())
      vtable->setAttr("cir.vtable.type_metadata",
                      mlir::ArrayAttr::get(&cgm.getMLIRContext(),
                                           typeEntries));
  }

  if (cgm.getCodeGenOpts().VirtualFunctionElimination ||
      cgm.getCodeGenOpts().WholeProgramVTables) {
    llvm::DenseSet<const CXXRecordDecl *> visited;
    CIRVCallVisibility typeVis =
        getCIRVCallVisibilityLevel(cgm, rd, visited);
    if (typeVis != CIRVCallVisibility::Public) {
      auto i64Ty = mlir::IntegerType::get(&cgm.getMLIRContext(), 64);
      vtable->setAttr(
          "cir.vtable.vcall_visibility",
          mlir::IntegerAttr::get(i64Ty, static_cast<uint64_t>(typeVis)));
    }
  }

  if (getItaniumVTableContext().isRelativeLayout() && !vtable.getDsoLocal())
    generateRelativeVTableAlias(vtable, outName);

  return vtable;
}

/// Compute the required linkage of the vtable for the given class.
///
/// Note that we only call this at the end of the translation unit.
cir::GlobalLinkageKind CIRGenModule::getVTableLinkage(const CXXRecordDecl *rd) {
  if (!rd->isExternallyVisible())
    return cir::GlobalLinkageKind::InternalLinkage;

  // In windows, the linkage of vtable is not related to modules.
  bool isInNamedModule =
      !getTarget().getCXXABI().isMicrosoft() && rd->isInNamedModule();
  // If the CXXRecordDecl is not in a module unit, we need to get
  // its key function. We're at the end of the translation unit, so the current
  // key function is fully correct.
  const CXXMethodDecl *keyFunction =
      isInNamedModule ? nullptr : astContext.getCurrentKeyFunction(rd);
  if (isInNamedModule || (keyFunction && !rd->hasAttr<DLLImportAttr>())) {
    // If this class has a key function, use that to determine the
    // linkage of the vtable.
    const FunctionDecl *def = nullptr;
    if (keyFunction && keyFunction->hasBody(def))
      keyFunction = cast<CXXMethodDecl>(def);

    bool isExternalDefinition =
        isInNamedModule ? rd->shouldEmitInExternalSource() : !def;

    TemplateSpecializationKind kind =
        isInNamedModule ? rd->getTemplateSpecializationKind()
                        : keyFunction->getTemplateSpecializationKind();

    switch (kind) {
    case TSK_Undeclared:
    case TSK_ExplicitSpecialization:
      assert(
          (isInNamedModule || def || codeGenOpts.OptimizationLevel > 0 ||
           codeGenOpts.getDebugInfo() != llvm::codegenoptions::NoDebugInfo) &&
          "Shouldn't query vtable linkage without the class in module units, "
          "key function, optimizations, or debug info");
      if (isExternalDefinition && codeGenOpts.OptimizationLevel > 0)
        return cir::GlobalLinkageKind::AvailableExternallyLinkage;

      if (keyFunction && keyFunction->isInlined())
        return !astContext.getLangOpts().AppleKext
                   ? cir::GlobalLinkageKind::LinkOnceODRLinkage
                   : cir::GlobalLinkageKind::InternalLinkage;
      return cir::GlobalLinkageKind::ExternalLinkage;

    case TSK_ImplicitInstantiation:
      return !astContext.getLangOpts().AppleKext
                 ? cir::GlobalLinkageKind::LinkOnceODRLinkage
                 : cir::GlobalLinkageKind::InternalLinkage;

    case TSK_ExplicitInstantiationDefinition:
      return !astContext.getLangOpts().AppleKext
                 ? cir::GlobalLinkageKind::WeakODRLinkage
                 : cir::GlobalLinkageKind::InternalLinkage;

    case TSK_ExplicitInstantiationDeclaration:
      return isExternalDefinition
                 ? cir::GlobalLinkageKind::AvailableExternallyLinkage
                 : cir::GlobalLinkageKind::ExternalLinkage;
    }
  }
  // -fapple-kext mode does not support weak linkage, so we must use
  // internal linkage.
  if (astContext.getLangOpts().AppleKext)
    return cir::GlobalLinkageKind::InternalLinkage;

  auto discardableODRLinkage = cir::GlobalLinkageKind::LinkOnceODRLinkage;
  auto nonDiscardableODRLinkage = cir::GlobalLinkageKind::WeakODRLinkage;
  if (rd->hasAttr<DLLExportAttr>()) {
    // Cannot discard exported vtables.
    discardableODRLinkage = nonDiscardableODRLinkage;
  } else if (rd->hasAttr<DLLImportAttr>()) {
    // Imported vtables are available externally.
    discardableODRLinkage = cir::GlobalLinkageKind::AvailableExternallyLinkage;
    nonDiscardableODRLinkage =
        cir::GlobalLinkageKind::AvailableExternallyLinkage;
  }

  switch (rd->getTemplateSpecializationKind()) {
  case TSK_Undeclared:
  case TSK_ExplicitSpecialization:
  case TSK_ImplicitInstantiation:
    return discardableODRLinkage;

  case TSK_ExplicitInstantiationDeclaration: {
    // Explicit instantiations in MSVC do not provide vtables, so we must emit
    // our own.
    if (getTarget().getCXXABI().isMicrosoft())
      return discardableODRLinkage;
    return shouldEmitAvailableExternallyVTable(*this, rd)
               ? cir::GlobalLinkageKind::AvailableExternallyLinkage
               : cir::GlobalLinkageKind::ExternalLinkage;
  }

  case TSK_ExplicitInstantiationDefinition:
    return nonDiscardableODRLinkage;
  }

  llvm_unreachable("Invalid TemplateSpecializationKind!");
}

cir::GlobalOp CIRGenVTables::getAddrOfVTT(const CXXRecordDecl *rd) {
  assert(rd->getNumVBases() && "Only classes with virtual bases need a VTT");

  SmallString<256> outName;
  llvm::raw_svector_ostream out(outName);
  cast<ItaniumMangleContext>(cgm.getCXXABI().getMangleContext())
      .mangleCXXVTT(rd, out);
  StringRef name = outName.str();

  // This will also defer the definition of the VTT.
  (void)cgm.getCXXABI().getAddrOfVTable(rd, CharUnits());

  VTTBuilder builder(cgm.getASTContext(), rd, /*GenerateDefinition=*/false);

  auto globalsInt8PtrTy = cgm.getDefaultGlobalsInt8PtrTy();
  auto arrayType =
      cir::ArrayType::get(globalsInt8PtrTy, builder.getVTTComponents().size());
  llvm::Align align = cgm.getDataLayout().getABITypeAlign(globalsInt8PtrTy);
  cir::GlobalOp vtt = cgm.createOrReplaceCXXRuntimeVariable(
      cgm.getLoc(rd->getSourceRange()), name, arrayType,
      cir::GlobalLinkageKind::ExternalLinkage, CharUnits::fromQuantity(align),
      cgm.getDefaultGlobalsAddressSpace());
  // VTTs are always unnamed_addr — matching OGCG's GetAddrOfVTT behavior.
  vtt->setAttr("unnamed_addr", mlir::UnitAttr::get(vtt->getContext()));
  cgm.setGVProperties(vtt, rd);
  return vtt;
}

static cir::GlobalOp
getAddrOfVTTVTable(CIRGenVTables &cgvt, CIRGenModule &cgm,
                   const CXXRecordDecl *mostDerivedClass,
                   const VTTVTable &vtable, cir::GlobalLinkageKind linkage,
                   VTableLayout::AddressPointsMapTy &addressPoints) {
  if (vtable.getBase() == mostDerivedClass) {
    assert(vtable.getBaseOffset().isZero() &&
           "Most derived class vtable must have a zero offset!");
    // This is a regular vtable.
    return cgm.getCXXABI().getAddrOfVTable(mostDerivedClass, CharUnits());
  }
  return cgvt.generateConstructionVTable(
      mostDerivedClass, vtable.getBaseSubobject(), vtable.isVirtual(), linkage,
      addressPoints);
}

/// Emit the definition of the given vtable.
void CIRGenVTables::emitVTTDefinition(cir::GlobalOp vttOp,
                                      cir::GlobalLinkageKind linkage,
                                      const CXXRecordDecl *rd) {
  VTTBuilder builder(cgm.getASTContext(), rd, /*GenerateDefinition=*/true);

  mlir::MLIRContext *mlirContext = &cgm.getMLIRContext();

  auto globalsInt8PtrTy = cgm.getDefaultGlobalsInt8PtrTy();
  auto arrayType =
      cir::ArrayType::get(globalsInt8PtrTy, builder.getVTTComponents().size());
  bool signVTTComponents = false;
  mlir::ArrayAttr vttPtrauthAttr;
  if (const auto &schema =
          cgm.getCodeGenOpts().PointerAuth.CXXVTTVTablePointers) {
    auto authMode = schema.getAuthenticationMode();
    if (authMode == PointerAuthenticationMode::SignAndStrip ||
        authMode == PointerAuthenticationMode::SignAndAuth) {
      uint64_t discriminator = 0;
      switch (schema.getOtherDiscrimination()) {
      case PointerAuthSchema::Discrimination::None:
        break;
      case PointerAuthSchema::Discrimination::Constant:
        discriminator = schema.getConstantDiscrimination();
        break;
      case PointerAuthSchema::Discrimination::Type:
      case PointerAuthSchema::Discrimination::Decl:
        llvm_unreachable(
            "CXXVTTVTablePointers schema requires unsupported "
            "type/decl discrimination in CIR constant emission");
      }

      mlir::Type i64Ty = mlir::IntegerType::get(mlirContext, 64);
      vttPtrauthAttr = cgm.getBuilder().getArrayAttr({
          cgm.getBuilder().getI32IntegerAttr(schema.getKey()),
          mlir::IntegerAttr::get(i64Ty, discriminator),
          cgm.getBuilder().getBoolAttr(schema.isAddressDiscriminated()),
      });
      signVTTComponents = true;
    }
  }

  SmallVector<cir::GlobalOp> vtables;
  SmallVector<VTableAddressPointsMapTy> vtableAddressPoints;
  for (const VTTVTable &vtt : builder.getVTTVTables()) {
    vtableAddressPoints.push_back(VTableAddressPointsMapTy());
    vtables.push_back(getAddrOfVTTVTable(*this, cgm, rd, vtt, linkage,
                                         vtableAddressPoints.back()));
  }

  SmallVector<mlir::Attribute> vttComponents;
  for (const VTTComponent &vttComponent : builder.getVTTComponents()) {
    const VTTVTable &vttVT = builder.getVTTVTables()[vttComponent.VTableIndex];
    cir::GlobalOp vtable = vtables[vttComponent.VTableIndex];
    VTableLayout::AddressPointLocation addressPoint;
    if (vttVT.getBase() == rd) {
      // Just get the address point for the regular vtable.
      addressPoint =
          getItaniumVTableContext().getVTableLayout(rd).getAddressPoint(
              vttComponent.VTableBase);
    } else {
      addressPoint = vtableAddressPoints[vttComponent.VTableIndex].lookup(
          vttComponent.VTableBase);
      assert(addressPoint.AddressPointIndex != 0 &&
             "Did not find ctor vtable address point!");
    }

    mlir::Attribute indices[2] = {
        cgm.getBuilder().getI32IntegerAttr(addressPoint.VTableIndex),
        cgm.getBuilder().getI32IntegerAttr(addressPoint.AddressPointIndex),
    };

    auto indicesAttr = mlir::ArrayAttr::get(mlirContext, indices);

    // Compute inrange bounds for VTT GEPs (matching OGCG).
    // The inrange bounds limit pointer arithmetic to the sub-vtable array,
    // enabling devirtualization.
    unsigned componentSize =
        cgm.getDataLayout().getTypeAllocSize(cgm.getVTableComponentType());
    auto vtableRecordTy =
        mlir::cast<cir::RecordType>(vtable.getSymType());
    auto subVtableArrayTy = mlir::cast<cir::ArrayType>(
        vtableRecordTy.getMembers()[addressPoint.VTableIndex]);
    unsigned vtableSize = componentSize * subVtableArrayTy.getSize();
    unsigned offset = componentSize * addressPoint.AddressPointIndex;
    int32_t inRangeStart = -(int32_t)offset;
    int32_t inRangeEnd = (int32_t)(vtableSize - offset);
    auto inRangeAttr =
        mlir::DenseI32ArrayAttr::get(mlirContext, {inRangeStart, inRangeEnd});

    auto symbol = mlir::FlatSymbolRefAttr::get(vtable.getSymNameAttr());
    cir::GlobalViewAttr init = cir::GlobalViewAttr::get(
        globalsInt8PtrTy, symbol, indicesAttr,
        mlir::IntegerAttr{}, inRangeAttr, /*inbounds=*/true,
        mlir::IntegerAttr{});

    vttComponents.push_back(init);
  }

  auto init = cir::ConstArrayAttr::get(
      arrayType, mlir::ArrayAttr::get(mlirContext, vttComponents));

  vttOp.setInitialValueAttr(init);
  if (signVTTComponents)
    vttOp->setAttr("cir.vtt_ptrauth", vttPtrauthAttr);

  // Set the correct linkage.
  vttOp.setLinkage(linkage);
  mlir::SymbolTable::setSymbolVisibility(
      vttOp, CIRGenModule::getMLIRVisibility(vttOp));

  if (cgm.supportsCOMDAT() && vttOp.isWeakForLinker())
    vttOp.setComdat(true);

  // OGCG calls setGVProperties(VTT, RD) after setting linkage/comdat.
  cgm.setGVProperties(vttOp, rd);
}

uint64_t CIRGenVTables::getSubVTTIndex(const CXXRecordDecl *rd,
                                       BaseSubobject base) {
  BaseSubobjectPairTy classSubobjectPair(rd, base);

  SubVTTIndiciesMapTy::iterator it = subVTTIndicies.find(classSubobjectPair);
  if (it != subVTTIndicies.end())
    return it->second;

  VTTBuilder builder(cgm.getASTContext(), rd, /*GenerateDefinition=*/false);

  for (const auto &entry : builder.getSubVTTIndices()) {
    // Insert all indices.
    BaseSubobjectPairTy subclassSubobjectPair(rd, entry.first);

    subVTTIndicies.insert(std::make_pair(subclassSubobjectPair, entry.second));
  }

  it = subVTTIndicies.find(classSubobjectPair);
  assert(it != subVTTIndicies.end() && "Did not find index!");

  return it->second;
}

uint64_t CIRGenVTables::getSecondaryVirtualPointerIndex(const CXXRecordDecl *rd,
                                                        BaseSubobject base) {
  auto it = secondaryVirtualPointerIndices.find(std::make_pair(rd, base));

  if (it != secondaryVirtualPointerIndices.end())
    return it->second;

  VTTBuilder builder(cgm.getASTContext(), rd, /*GenerateDefinition=*/false);

  // Insert all secondary vpointer indices.
  for (const auto &entry : builder.getSecondaryVirtualPointerIndices()) {
    std::pair<const CXXRecordDecl *, BaseSubobject> pair =
        std::make_pair(rd, entry.first);

    secondaryVirtualPointerIndices.insert(std::make_pair(pair, entry.second));
  }

  it = secondaryVirtualPointerIndices.find(std::make_pair(rd, base));
  assert(it != secondaryVirtualPointerIndices.end() && "Did not find index!");

  return it->second;
}

void CIRGenVTables::emitThunks(GlobalDecl gd) {
  const CXXMethodDecl *md =
      cast<CXXMethodDecl>(gd.getDecl())->getCanonicalDecl();

  // We don't need to generate thunks for the base destructor.
  if (isa<CXXDestructorDecl>(md) && gd.getDtorType() == Dtor_Base)
    return;

  const VTableContextBase::ThunkInfoVectorTy *thunkInfoVector =
      vtContext->getThunkInfo(gd);

  if (!thunkInfoVector)
    return;

  for (const ThunkInfo &thunk : *thunkInfoVector)
    maybeEmitThunk(gd, thunk, /*ForVTable=*/false);
}

static void setThunkProperties(CIRGenModule &cgm, const ThunkInfo &thunk,
                               cir::FuncOp thunkFn, bool forVTable,
                               GlobalDecl gd) {
  cgm.setFunctionLinkage(gd, thunkFn);
  cgm.getCXXABI().setThunkLinkage(thunkFn, forVTable, gd,
                                  !thunk.Return.isEmpty());

  // Set the right visibility.
  cgm.setGVProperties(thunkFn, cast<NamedDecl>(gd.getDecl()));

  if (!cgm.getCXXABI().exportThunk()) {
    // OGCG resets DLL storage class for non-exported thunks. Windows-only.
    cgm.setDSOLocal(static_cast<mlir::Operation *>(thunkFn));
  }

  if (cgm.supportsCOMDAT() && thunkFn.isWeakForLinker())
    thunkFn.setComdat(true);
}

static bool shouldEmitVTableThunk(CIRGenModule &cgm, const CXXMethodDecl *md,
                                  bool isUnprototyped, bool forVTable) {
  // Always emit thunks in the MS C++ ABI. We cannot rely on other TUs to
  // provide thunks for us.
  if (cgm.getTarget().getCXXABI().isMicrosoft())
    return true;

  // In the Itanium C++ ABI, vtable thunks are provided by TUs that provide
  // definitions of the main method. Therefore, emitting thunks with the vtable
  // is purely an optimization. Emit the thunk if optimizations are enabled and
  // all of the parameter types are complete.
  if (forVTable)
    return cgm.getCodeGenOpts().OptimizationLevel && !isUnprototyped;

  // Always emit thunks along with the method definition.
  return true;
}

static RValue performReturnAdjustment(CIRGenFunction &cgf, QualType resultType,
                                      RValue rv, const ThunkInfo &thunk) {
  // Emit the return adjustment.
  bool nullCheckValue = !resultType->isReferenceType();

  mlir::Value returnValue = rv.getValue();

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = returnValue.getLoc();

  if (nullCheckValue) {
    // For pointer returns, skip the adjustment if the return value is null.
    // Applying a byte offset to null is undefined behavior.
    mlir::Value isNull = builder.createPtrIsNull(returnValue);
    returnValue = cir::TernaryOp::create(
        builder, loc, isNull,
        [&](mlir::OpBuilder &, mlir::Location) {
          // Null case: return null without adjustment.
          builder.createYield(loc, returnValue);
        },
        [&](mlir::OpBuilder &, mlir::Location) {
          // Non-null case: perform the return adjustment.
          CXXRecordDecl *classDecl =
              resultType->getPointeeType()->getAsCXXRecordDecl();
          clang::CharUnits classAlign =
              cgf.cgm.getClassPointerAlignment(classDecl);
          mlir::Type pointeeType =
              cgf.convertTypeForMem(resultType->getPointeeType());
          mlir::Value adjusted = cgf.cgm.getCXXABI().performReturnAdjustment(
              cgf, Address(returnValue, pointeeType, classAlign), classDecl,
              thunk.Return);
          builder.createYield(loc, adjusted);
        }).getResult();
  } else {
    CXXRecordDecl *classDecl =
        resultType->getPointeeType()->getAsCXXRecordDecl();
    clang::CharUnits classAlign =
        cgf.cgm.getClassPointerAlignment(classDecl);
    mlir::Type pointeeType =
        cgf.convertTypeForMem(resultType->getPointeeType());
    returnValue = cgf.cgm.getCXXABI().performReturnAdjustment(
        cgf, Address(returnValue, pointeeType, classAlign), classDecl,
        thunk.Return);
  }

  return RValue::get(returnValue);
}

void CIRGenFunction::startThunk(cir::FuncOp fn, GlobalDecl gd,
                                const CIRGenFunctionInfo &fnInfo,
                                bool isUnprototyped) {
  assert(!curGD.getDecl() && "curGD was already set!");
  curGD = gd;
  curFuncIsThunk = true;

  // Build FunctionArgs.
  const CXXMethodDecl *md = cast<CXXMethodDecl>(gd.getDecl());
  QualType thisType = md->getThisType();
  QualType resultType;
  if (isUnprototyped)
    resultType = cgm.getASTContext().VoidTy;
  else if (cgm.getCXXABI().hasThisReturn(gd))
    resultType = thisType;
  else if (cgm.getCXXABI().hasMostDerivedReturn(gd))
    resultType = cgm.getASTContext().VoidPtrTy;
  else
    resultType = md->getType()->castAs<FunctionProtoType>()->getReturnType();
  FunctionArgList functionArgs;

  // Create the implicit 'this' parameter declaration.
  cgm.getCXXABI().buildThisParam(*this, functionArgs);

  // Add the rest of the parameters, if we have a prototype to work with.
  if (!isUnprototyped) {
    functionArgs.append(md->param_begin(), md->param_end());

    if (isa<CXXDestructorDecl>(md))
      cgm.getCXXABI().addImplicitStructorParams(*this, resultType,
                                                functionArgs);
  }

  // Start defining the function.
  // Match OGCG: pass an empty GlobalDecl to avoid method-specific StartFunction
  // codepaths such as the per-function 'this' UBSan prolog. Thunks are
  // forwarding trampolines and should not get their own entry checks.
  //
  // startFunction must still see curFuncIsThunk=true so prologue parameter
  // emission skips callee-destroyed parameter cleanups for thunk parameters.
  // Those parameters are owned by the eventual non-thunk callee.
  auto funcType = mlir::cast<cir::FuncType>(fn.getFunctionType());
  startFunction(GlobalDecl(), resultType, fn, funcType, functionArgs,
                md->getLocation(), md->getLocation());

  // Mark this function as a thunk for debug info purposes. The DISubprogram
  // rewrite in LowerToLLVM uses this to add DIFlagArtificial | DIFlagThunk
  // and move the mangled name from 'name' to 'linkageName'.
  fn->setAttr("cir.thunk", mlir::UnitAttr::get(fn.getContext()));

  // Thunks with scalar returns normally forward the callee result directly via
  // emitReturnFromThunk and do not need startFunction's synthesized __retval
  // alloca/return slot. Preserve that slot for returned-'this' thunks: the
  // ABI prolog still seeds it with the incoming 'this' value.
  if (!resultType->isVoidType() && !hasAggregateEvaluationKind(resultType) &&
      fnRetAlloca && !cgm.getCXXABI().hasThisReturn(gd)) {
    if (auto allocaOp = fnRetAlloca->getDefiningOp<cir::AllocaOp>())
      if (allocaOp->use_empty())
        allocaOp->erase();
    fnRetAlloca.reset();
    returnValue = Address::invalid();
  }

  cgm.getCXXABI().emitInstanceFunctionProlog(md->getLocation(), *this);
  cxxThisValue = cxxabiThisValue;
  curCodeDecl = md;
  curFuncDecl = md;
}

void CIRGenFunction::finishThunk() {
  // Clear these to restore the invariants expected by
  // startFunction/finishFunction.
  curCodeDecl = nullptr;
  curFuncDecl = nullptr;
  curFuncIsThunk = false;

  finishFunction(SourceLocation());
}

void CIRGenFunction::emitCallAndReturnForThunk(cir::FuncOp callee,
                                               const ThunkInfo *thunk,
                                               bool isUnprototyped) {
  assert(isa<CXXMethodDecl>(curGD.getDecl()) &&
         "Please use a new CGF for this thunk");
  const CXXMethodDecl *md = cast<CXXMethodDecl>(curGD.getDecl());

  // Determine the this pointer class (may differ from MD's class for thunks)
  const CXXRecordDecl *thisValueClass =
      md->getThisType()->getPointeeCXXRecordDecl();
  if (thunk)
    thisValueClass = thunk->ThisType->getPointeeCXXRecordDecl();

  mlir::Value adjustedThisPtr =
      thunk ? cgm.getCXXABI().performThisAdjustment(*this, loadCXXThisAddress(),
                                                    thisValueClass, *thunk)
            : loadCXXThis();

  // If perfect forwarding is required a variadic method, a method using
  // inalloca, or an unprototyped thunk, use musttail. Emit an error if this
  // thunk requires a return adjustment, since that is impossible with musttail.
  //
  // OGCG also checks CurFnInfo->usesInAlloca() here. inalloca is x86-32
  // MSVC ABI only, not applicable to CIR targets (x86_64, AArch64).
  const CIRGenFunctionInfo &fnInfo =
      cgm.getTypes().arrangeGlobalDeclaration(curGD);
  if (fnInfo.isVariadic() || isUnprototyped) {
    if (thunk && !thunk->Return.isEmpty()) {
      if (isUnprototyped) {
        cgm.errorNYI(md->getSourceRange(),
                     "return-adjusting thunk with incomplete parameter type");
        return;
      } else if (fnInfo.isVariadic()) {
        llvm_unreachable("shouldn't try to emit musttail return-adjusting "
                         "thunks for variadic functions");
      }
    }
    emitMustTailThunk(curGD, adjustedThisPtr, callee);
    return;
  }

  // Build the call argument list
  CallArgList callArgs;
  QualType thisType = md->getThisType();
  callArgs.add(RValue::get(adjustedThisPtr), thisType);

  if (isa<CXXDestructorDecl>(md))
    cgm.getCXXABI().adjustCallArgsForDestructorThunk(*this, curGD, callArgs);

  // Add the rest of the method parameters
  for (const ParmVarDecl *pd : md->parameters())
    emitDelegateCallArg(callArgs, pd, SourceLocation());

  const FunctionProtoType *fpt = md->getType()->castAs<FunctionProtoType>();

  // Determine whether we have a return value slot to use.
  QualType resultType = cgm.getCXXABI().hasThisReturn(curGD) ? thisType
                        : cgm.getCXXABI().hasMostDerivedReturn(curGD)
                            ? cgm.getASTContext().VoidPtrTy
                            : fpt->getReturnType();

  // OGCG also creates a ReturnValueSlot when ABIArgInfo is Indirect (sret).
  // CIR defers ABI-indirect return handling to the CallConvLowering pass,
  // so the hasAggregateEvaluationKind check is sufficient at CIRGen time.
  // OGCG also passes IsExternallyDestructed=true to the ReturnValueSlot
  // constructor. CIRGen's ReturnValueSlot doesn't model this; thunk return
  // cleanup is handled at the target lowering level (LowerCall.h).
  ReturnValueSlot slot;
  if (!resultType->isVoidType() && hasAggregateEvaluationKind(resultType))
    slot = ReturnValueSlot(returnValue, resultType.isVolatileQualified());

  // Now emit our call.
  CIRGenCallee cirCallee = CIRGenCallee::forDirect(callee, curGD);
  auto loc = curFn->getLoc();
  cir::CIRCallOpInterface callOrTryCall;
  RValue rv = emitCall(fnInfo, cirCallee, slot, callArgs,
                       &callOrTryCall, loc);

  // Consider return adjustment if we have ThunkInfo.
  if (thunk && !thunk->Return.isEmpty())
    rv = performReturnAdjustment(*this, resultType, rv, *thunk);
  // OGCG marks non-return-adjusting thunk calls as tail calls (TCK_Tail)
  // for better codegen — thunks should be zero-overhead forwarding stubs.
  else if (callOrTryCall) {
    if (auto callOp = mlir::dyn_cast<cir::CallOp>(
            callOrTryCall.getOperation()))
      callOp.setTail(true);
  }

  // OGCG sets AutoreleaseResult = false to disable ARC autorelease in thunks.
  // CIR currently has no dedicated autorelease-result toggling here.

  // Emit return.
  if (!resultType->isVoidType() && slot.isNull())
    cgm.getCXXABI().emitReturnFromThunk(*this, rv, resultType);
  else if (!resultType->isVoidType() && !slot.isNull()) {
    // For aggregate returns, the inner call wrote the result into __retval
    // via sret. Emit a cir.return loading from __retval so that the
    // CallConvLowering pass can properly lower the aggregate return.
    auto retLoc = curFn->getLoc();
    mlir::Value retVal = builder.createLoad(retLoc, returnValue);
    cir::ReturnOp::create(builder, retLoc, retVal);
  }

  finishThunk();
}

void CIRGenFunction::emitMustTailThunk(GlobalDecl gd,
                                       mlir::Value adjustedThisPtr,
                                       cir::FuncOp callee) {
  // Emitting a musttail call thunk doesn't use any of the CGCall machinery
  // to translate AST arguments into CIR arguments. For thunks, the caller
  // prototype matches the callee prototype except for 'this'.

  // Collect the function's entry block arguments (raw parameters).
  auto funcOp = mlir::cast<cir::FuncOp>(curFn);
  auto &entryBlock = funcOp.getBody().front();
  SmallVector<mlir::Value, 8> args(entryBlock.getArguments());

  // Set the adjusted 'this' pointer. At CIR level, 'this' is always the
  // first parameter (sret is an ABI lowering concern handled later).
  assert(!args.empty() &&
         "thunk function must have at least a 'this' parameter");
  args[0] = adjustedThisPtr;

  // Build the musttail call.
  auto loc = curFn->getLoc();
  auto calleeSymRef =
      mlir::SymbolRefAttr::get(&getMLIRContext(), callee.getSymName());
  auto resultType = funcOp.getFunctionType().getReturnType();
  auto callOp =
      cir::CallOp::create(builder, loc, calleeSymRef, resultType, args);
  callOp.setMustTail(true);

  // Return the result directly. With musttail, the return value is forwarded
  // from the callee without modification.
  if (callOp.getNumResults() == 0)
    cir::ReturnOp::create(builder, loc);
  else
    cir::ReturnOp::create(builder, loc, callOp.getResult());

  finishThunk();
}

void CIRGenFunction::generateVarArgsThunk(cir::FuncOp fn,
                                          const CIRGenFunctionInfo &fnInfo,
                                          GlobalDecl gd,
                                          const ThunkInfo &thunk) {
  // This function does roughly the same thing as generateThunk, but by cloning
  // the base function body so that va_start and va_end work correctly.
  // OGCG: CodeGenFunction::GenerateVarArgsThunk (CGVTables.cpp)
  const CXXMethodDecl *md = cast<CXXMethodDecl>(gd.getDecl());
  const FunctionProtoType *fpt = md->getType()->castAs<FunctionProtoType>();
  QualType resultType = fpt->getReturnType();

  // Get the original function.
  assert(fnInfo.isVariadic());
  auto ty = cgm.getTypes().getFunctionType(fnInfo);
  cir::FuncOp baseFn = cgm.getAddrOfFunction(gd, ty, /*ForVTable=*/true);

  // Cloning can't work if we don't have a definition.
  if (!md->isDefined()) {
    cgm.errorNYI(md->getSourceRange(),
                 "return-adjusting thunk with variadic arguments");
    return;
  }
  assert(!baseFn.isDeclaration() && "cannot clone undefined variadic method");

  // Clone the base function's body into the thunk function.
  // OGCG uses llvm::CloneFunction which copies both the body and all function/
  // argument attributes. cloneInto only copies ops, so we must also copy the
  // argument and function-level attributes explicitly.
  mlir::IRMapping mapper;
  baseFn.getBody().cloneInto(&fn.getBody(), mapper);

  // Copy argument attributes from the base function to the thunk.
  for (unsigned i = 0, e = baseFn.getNumArguments(); i < e; ++i) {
    if (auto attrs = baseFn.getArgAttrDict(i))
      fn.setArgAttrs(i, attrs);
  }

  // Copy function-level extra attributes (mustprogress, min-legal-vector-width,
  // etc.) from the base function. OGCG's llvm::CloneFunction copies all
  // function attributes from the source; we must do the same here.
  if (auto extraAttrs = baseFn.getExtraAttrs())
    fn.setExtraAttrsAttr(*extraAttrs);

  // "Initialize" CGF minimally (matching OGCG's minimal initialization).
  curFn = fn;

  // Get the "this" parameter (first block argument of the entry block).
  auto &entryBlock = fn.getBody().front();
  mlir::Value thisArg = entryBlock.getArgument(0);

  // Find the first store of "this" to its alloca in the entry block.
  cir::StoreOp thisStore;
  for (auto &op : entryBlock.getOperations()) {
    if (auto storeOp = mlir::dyn_cast<cir::StoreOp>(op)) {
      if (storeOp.getValue() == thisArg) {
        thisStore = storeOp;
        break;
      }
    }
  }
  assert(thisStore && "Store of this should be in entry block");

  // Insert this adjustment before the store.
  builder.setInsertionPoint(thisStore);

  const CXXRecordDecl *thisValueClass =
      thunk.ThisType->getPointeeCXXRecordDecl();
  Address thisAddr(thisArg,
                   cgm.getClassPointerAlignment(md->getParent()));
  mlir::Value adjustedThisPtr = cgm.getCXXABI().performThisAdjustment(
      *this, thisAddr, thisValueClass, thunk);

  // Cast to match the original this type if needed.
  if (adjustedThisPtr.getType() != thisArg.getType())
    adjustedThisPtr =
        builder.createBitcast(adjustedThisPtr, thisArg.getType());

  // Replace the stored value with the adjusted this pointer.
  thisStore.getValueMutable().assign(adjustedThisPtr);

  // Fix up the returned value, if necessary.
  if (!thunk.Return.isEmpty()) {
    // Collect all return ops first (to avoid modifying while walking).
    SmallVector<cir::ReturnOp, 4> returnOps;
    fn.walk([&](cir::ReturnOp retOp) { returnOps.push_back(retOp); });

    for (auto retOp : returnOps) {
      if (retOp.getNumOperands() > 0) {
        builder.setInsertionPoint(retOp);
        RValue rv = RValue::get(retOp.getOperand(0));
        rv = performReturnAdjustment(*this, resultType, rv, thunk);
        retOp.setOperand(0, rv.getValue());
      }
    }
  }
}

void CIRGenFunction::generateThunk(cir::FuncOp fn,
                                   const CIRGenFunctionInfo &fnInfo,
                                   GlobalDecl gd, const ThunkInfo &thunk,
                                   bool isUnprototyped) {
  // Create entry block and set up the builder's insertion point.
  assert(fn.isDeclaration() && "Function already has body?");
  mlir::Block *entryBb = fn.addEntryBlock();
  builder.setInsertionPointToStart(entryBb);

  // Create a scope in the symbol table to hold variable declarations.
  SymTableScopeTy varScope(symbolTable);

  // Create lexical scope.
  // Use the function's location for thunk body operations. Thunks are
  // compiler-generated, but when debug info is enabled the function gets a
  // DISubprogram, so all operations inside must carry a valid debug location
  // (LLVM verifier requires it for inlinable calls).
  auto thunkLoc = fn.getLoc();
  LexicalScope lexScope{*this, thunkLoc, entryBb};

  // Set a synthetic location so that getLoc() doesn't assert when called with
  // invalid SourceLocations (e.g., from emitDelegateCallArg).
  SourceLocRAIIObject scopedLoc{*this, thunkLoc};

  startThunk(fn, gd, fnInfo, isUnprototyped);

  // Get our callee. Use a placeholder type if this method is unprototyped so
  // that CIRGenModule doesn't try to set attributes.
  mlir::Type ty;
  if (isUnprototyped) {
    cgm.errorNYI("unprototyped thunk placeholder type");
    return;
  } else
    ty = cgm.getTypes().getFunctionType(fnInfo);

  cir::FuncOp callee = cgm.getAddrOfFunction(gd, ty, /*ForVTable=*/true);

  // Make the call and return the result.
  emitCallAndReturnForThunk(callee, &thunk, isUnprototyped);
}

cir::FuncOp CIRGenVTables::maybeEmitThunk(GlobalDecl gd,
                                          const ThunkInfo &thunkAdjustments,
                                          bool forVTable) {
  const CXXMethodDecl *md = cast<CXXMethodDecl>(gd.getDecl());
  SmallString<256> name;
  MangleContext &mCtx = cgm.getCXXABI().getMangleContext();

  llvm::raw_svector_ostream out(name);
  if (const CXXDestructorDecl *dd = dyn_cast<CXXDestructorDecl>(md)) {
    mCtx.mangleCXXDtorThunk(dd, gd.getDtorType(), thunkAdjustments,
                            /* elideOverrideInfo */ false, out);
  } else
    mCtx.mangleThunk(md, thunkAdjustments, /* elideOverrideInfo */ false, out);

  if (cgm.getASTContext().useAbbreviatedThunkName(gd, name.str())) {
    name = "";
    if (const CXXDestructorDecl *dd = dyn_cast<CXXDestructorDecl>(md))
      mCtx.mangleCXXDtorThunk(dd, gd.getDtorType(), thunkAdjustments,
                              /* elideOverrideInfo */ true, out);
    else
      mCtx.mangleThunk(md, thunkAdjustments, /* elideOverrideInfo */ true, out);
  }

  cir::FuncType thunkVTableTy = cgm.getTypes().getFunctionTypeForVTable(gd);
  cir::FuncOp thunk = cgm.getAddrOfThunk(name, thunkVTableTy, gd);

  // If we don't need to emit a definition, return this declaration as is.
  bool isUnprototyped = !cgm.getTypes().isFuncTypeConvertible(
      md->getType()->castAs<FunctionType>());
  if (!shouldEmitVTableThunk(cgm, md, isUnprototyped, forVTable))
    return thunk;

  // Arrange a function prototype appropriate for a function definition. In some
  // cases in the MS ABI, we may need to build an unprototyped musttail thunk.
  const CIRGenFunctionInfo &fnInfo =
      cgm.getTypes().arrangeGlobalDeclaration(gd);
  cir::FuncType thunkFnTy = cgm.getTypes().getFunctionType(fnInfo);

  cir::FuncOp thunkFn = thunk;
  if (thunk.getFunctionType() != thunkFnTy) {
    cir::FuncOp oldThunkFn = thunkFn;

    assert(oldThunkFn.isDeclaration() && "Shouldn't replace non-declaration");

    // Remove the name from the old thunk function and get a new thunk.
    // The builder's insertion point may be invalid at this point (e.g., after
    // emitting a function definition), so we insert the new thunk into the
    // module explicitly rather than relying on the builder's insertion point.
    oldThunkFn.setName(StringRef());
    thunkFn =
        cir::FuncOp::create(cgm.getBuilder(), thunk->getLoc(), name.str(),
                            thunkFnTy, cir::GlobalLinkageKind::ExternalLinkage);
    cgm.insertGlobalSymbol(thunkFn);
    // Ensure the new thunk is inserted into the module. FuncOp::create may
    // fail to insert when the builder has no valid insertion point.
    if (!thunkFn->getParentOp())
      cgm.getModule().push_back(thunkFn);
    cgm.setCIRFunctionAttributes(gd, fnInfo, thunkFn,
                                 /*IsThunk=*/false);

    if (!oldThunkFn->use_empty())
      oldThunkFn->replaceAllUsesWith(thunkFn);

    // Remove the old thunk.
    oldThunkFn->erase();
  }

  bool abiHasKeyFunctions = cgm.getTarget().getCXXABI().hasKeyFunctions();
  bool useAvailableExternallyLinkage = forVTable && abiHasKeyFunctions;

  if (!thunkFn.isDeclaration()) {
    if (!abiHasKeyFunctions || useAvailableExternallyLinkage) {
      // There is already a thunk emitted for this function, do nothing.
      // There is already a thunk emitted for this function, do nothing.
      return thunkFn;
    }

    setThunkProperties(cgm, thunkAdjustments, thunkFn, forVTable, gd);
    return thunkFn;
  }

  cgm.setCIRFunctionAttributesForDefinition(gd, thunkFn, /*isThunk=*/true);

  // OGCG adds a "thunk" function attribute on unprototyped thunks to tell
  // LLVM that the return type is meaningless.
  if (isUnprototyped) {
    mlir::NamedAttrList attrs;
    if (auto existingAttrs = thunkFn.getExtraAttrs())
      attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
    attrs.set("thunk", mlir::UnitAttr::get(&cgm.getMLIRContext()));
    thunkFn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(&cgm.getMLIRContext())));
  }

  // Thunks for variadic methods are special because in general variadic
  // arguments cannot be perfectly forwarded.
  bool shouldCloneVarArgs = false;
  if (!isUnprototyped && thunkFn.getFunctionType().isVarArg()) {
    shouldCloneVarArgs = true;
    if (thunkAdjustments.Return.isEmpty()) {
      switch (cgm.getTriple().getArch()) {
      case llvm::Triple::x86_64:
      case llvm::Triple::x86:
      case llvm::Triple::aarch64:
        shouldCloneVarArgs = false;
        break;
      default:
        break;
      }
    }
  }

  if (shouldCloneVarArgs) {
    if (useAvailableExternallyLinkage)
      return thunkFn;
    CIRGenFunction cgf(cgm, cgm.getBuilder());
    cgm.curCGF = &cgf;
    {
      mlir::OpBuilder::InsertionGuard guard(cgm.getBuilder());
      cgf.generateVarArgsThunk(thunkFn, fnInfo, gd, thunkAdjustments);
    }
    cgm.curCGF = nullptr;
  } else {
    // Normal thunk body generation.
    CIRGenFunction cgf(cgm, cgm.getBuilder());
    cgm.curCGF = &cgf;
    {
      mlir::OpBuilder::InsertionGuard guard(cgm.getBuilder());
      cgf.generateThunk(thunkFn, fnInfo, gd, thunkAdjustments, isUnprototyped);
    }
    cgm.curCGF = nullptr;
  }

  setThunkProperties(cgm, thunkAdjustments, thunkFn, forVTable, gd);

  return thunkFn;
}
