//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides C++ code generation targeting the Itanium C++ ABI.  The class
// in this file generates structures that follow the Itanium C++ ABI, which is
// documented at:
//  https://itanium-cxx-abi.github.io/cxx-abi/abi.html
//  https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html
//
// It also supports the closely-related ARM ABI, documented at:
// https://developer.arm.com/documentation/ihi0041/g/
//
//===----------------------------------------------------------------------===//

#include "CIRGenCXXABI.h"
#include "CIRGenFunction.h"

#include "clang/AST/ExprCXX.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/AST/VTableBuilder.h"
#include "clang/Basic/Linkage.h"
#include "clang/CIR/MissingFeatures.h"
#include "mlir/Dialect/DLTI/DLTI.h"
#include "llvm/Support/SipHash.h"
#include "llvm/Support/ErrorHandling.h"

using namespace clang;
using namespace clang::CIRGen;

namespace {

static mlir::ptr::MemorySpaceAttrInterface
getItaniumRTTIAddressSpace(CIRGenModule &cgm) {
  return cgm.getDefaultGlobalsAddressSpace();
}

static cir::PointerType
getItaniumRTTIPointerType(CIRGenBuilderTy &builder,
                          mlir::ptr::MemorySpaceAttrInterface addrSpace) {
  return builder.getPointerTo(builder.getUInt8Ty(), addrSpace);
}

static mlir::ptr::MemorySpaceAttrInterface
getExplicitDefaultAddressSpace(CIRGenModule &cgm) {
  unsigned defaultAS =
      cgm.getASTContext().getTargetAddressSpace(LangAS::Default);
  if (!defaultAS)
    return {};
  return cir::TargetAddressSpaceAttr::get(&cgm.getMLIRContext(), defaultAS);
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

class CIRGenItaniumCXXABI : public CIRGenCXXABI {
protected:
  /// All the vtables which have been defined.
  llvm::DenseMap<const CXXRecordDecl *, cir::GlobalOp> vtables;

  bool useARMMethodPtrABI;
  bool useARMGuardVarABI;

  bool isVTableHidden(const CXXRecordDecl *rd) const {
    const auto &vtableLayout =
        cgm.getItaniumVTableContext().getVTableLayout(rd);

    for (const auto &vtableComponent : vtableLayout.vtable_components()) {
      if (vtableComponent.isRTTIKind()) {
        const CXXRecordDecl *rttiDecl = vtableComponent.getRTTIDecl();
        if (rttiDecl->getVisibility() == Visibility::HiddenVisibility)
          return true;
      } else if (vtableComponent.isUsedFunctionPointerKind()) {
        const CXXMethodDecl *method = vtableComponent.getFunctionDecl();
        if (method->getVisibility() == Visibility::HiddenVisibility &&
            !method->isDefined())
          return true;
      }
    }
    return false;
  }

  bool hasAnyUnusedVirtualInlineFunction(const CXXRecordDecl *rd) const {
    const auto &vtableLayout =
        cgm.getItaniumVTableContext().getVTableLayout(rd);

    for (const auto &vtableComponent : vtableLayout.vtable_components()) {
      // Skip empty slot.
      if (!vtableComponent.isUsedFunctionPointerKind())
        continue;

      const CXXMethodDecl *method = vtableComponent.getFunctionDecl();
      const FunctionDecl *fd = method->getDefinition();
      const bool isInlined =
          method->getCanonicalDecl()->isInlined() || (fd && fd->isInlined());
      if (!isInlined)
        continue;

      StringRef name = cgm.getMangledName(vtableComponent.getGlobalDecl(
          cgm.getASTContext().getTargetInfo().emitVectorDeletingDtors(
              cgm.getASTContext().getLangOpts())));
      auto *op = cgm.getGlobalValue(name);

      // This checks if virtual inline function has already been emitted.
      // Note that it is possible that this inline function would be emitted
      // after trying to emit vtable speculatively. Because of this we do
      // an extra pass after emitting all deferred vtables to find and emit
      // these vtables opportunistically.
      if (!op)
        return true;
      if (auto funcOp = dyn_cast<cir::FuncOp>(op)) {
        if (funcOp.isDeclaration())
          return true;
      }
    }
    return false;
  }

  bool canSpeculativelyEmitVTableAsBaseClass(const CXXRecordDecl *rd) const {
    // We don't emit available_externally vtables if we are in -fapple-kext mode
    // because kext mode does not permit devirtualization.
    if (cgm.getLangOpts().AppleKext)
      return false;

    // If the vtable is hidden then it is not safe to emit an
    // available_externally copy of vtable.
    if (isVTableHidden(rd))
      return false;

    if (cgm.getCodeGenOpts().ForceEmitVTables)
      return true;

    // If we don't have any not emitted inline virtual function then we are safe
    // to emit an available_externally copy of vtable.
    // FIXME we can still emit a copy of the vtable if we
    // can emit definition of the inline functions.
    if (hasAnyUnusedVirtualInlineFunction(rd))
      return false;

    // For a class with virtual bases, we must also be able to speculatively
    // emit the VTT, because CodeGen doesn't have separate notions of "can emit
    // the vtable" and "can emit the VTT". For a base subobject, this means we
    // need to be able to emit non-virtual base vtables.
    if (rd->getNumVBases()) {
      for (const auto &b : rd->bases()) {
        auto *brd = b.getType()->getAsCXXRecordDecl();
        assert(brd && "no class for base specifier");
        if (b.isVirtual() || !brd->isDynamicClass())
          continue;
        if (!canSpeculativelyEmitVTableAsBaseClass(brd))
          return false;
      }
    }

    return true;
  }

public:
  CIRGenItaniumCXXABI(CIRGenModule &cgm, bool useARMMethodPtrABI = false,
                      bool useARMGuardVarABI = false)
      : CIRGenCXXABI(cgm), useARMMethodPtrABI(useARMMethodPtrABI),
        useARMGuardVarABI(useARMGuardVarABI) {}

  AddedStructorArgs getImplicitConstructorArgs(CIRGenFunction &cgf,
                                               const CXXConstructorDecl *d,
                                               CXXCtorType type,
                                               bool forVirtualBase,
                                               bool delegating) override;

  bool isThisCompleteObject(clang::GlobalDecl gd) const override;

  bool needsVTTParameter(clang::GlobalDecl gd) override;

  bool isZeroInitializable(const MemberPointerType *mpt) override;

  cir::MethodAttr
  emitMemberFunctionPointer(const CXXMethodDecl *md, clang::QualType memPtrTy,
                            cir::MethodType ty,
                            CharUnits thisAdjustment) override;

  cir::FuncOp
  getOrCreateVirtualFunctionPointerThunk(const CXXMethodDecl *md);

  AddedStructorArgCounts
  buildStructorSignature(GlobalDecl gd,
                         llvm::SmallVectorImpl<CanQualType> &argTys) override;

  void emitInstanceFunctionProlog(SourceLocation loc,
                                  CIRGenFunction &cgf) override;

  void addImplicitStructorParams(CIRGenFunction &cgf, QualType &resTy,
                                 FunctionArgList &params) override;
  mlir::Value getCXXDestructorImplicitParam(CIRGenFunction &cgf,
                                            const CXXDestructorDecl *dd,
                                            CXXDtorType type,
                                            bool forVirtualBase,
                                            bool delegating) override;
  void emitCXXConstructors(const clang::CXXConstructorDecl *d) override;
  void emitCXXDestructors(const clang::CXXDestructorDecl *d) override;
  void emitCXXStructor(clang::GlobalDecl gd) override;

  void emitDestructorCall(CIRGenFunction &cgf, const CXXDestructorDecl *dd,
                          CXXDtorType type, bool forVirtualBase,
                          bool delegating, Address thisAddr,
                          QualType thisTy) override;
  void registerGlobalDtor(const VarDecl *vd, cir::FuncOp dtor,
                          mlir::Value addr) override;
  void emitGuardedInit(CIRGenFunction &cgf, const VarDecl &d,
                       cir::GlobalOp var, bool shouldPerformInit) override;
  void emitCXXThreadLocalInitFuncs() override;
  void emitVirtualObjectDelete(CIRGenFunction &cgf, const CXXDeleteExpr *de,
                               Address ptr, QualType elementType,
                               const CXXDestructorDecl *dtor) override;

  void emitRethrow(CIRGenFunction &cgf, bool isNoReturn) override;
  void emitThrow(CIRGenFunction &cgf, const CXXThrowExpr *e) override;

  void emitBeginCatch(CIRGenFunction &cgf,
                      const CXXCatchStmt *catchStmt) override;

  bool useThunkForDtorVariant(const CXXDestructorDecl *dtor,
                              CXXDtorType dt) const override {
    // Itanium does not emit any destructor variant as an inline thunk.
    // Delegating may occur as an optimization, but all variants are either
    // emitted with external linkage or as linkonce if they are inline and used.
    return false;
  }

  bool isVirtualOffsetNeededForVTableField(CIRGenFunction &cgf,
                                           CIRGenFunction::VPtr vptr) override;

  cir::GlobalOp getAddrOfVTable(const CXXRecordDecl *rd,
                                CharUnits vptrOffset) override;
  CIRGenCallee getVirtualFunctionPointer(CIRGenFunction &cgf,
                                         clang::GlobalDecl gd, Address thisAddr,
                                         mlir::Type ty,
                                         SourceLocation loc) override;
  mlir::Value emitVirtualDestructorCall(CIRGenFunction &cgf,
                                        const CXXDestructorDecl *dtor,
                                        CXXDtorType dtorType, Address thisAddr,
                                        DeleteOrMemberCallExpr e) override;
  mlir::Value getVTableAddressPoint(BaseSubobject base,
                                    const CXXRecordDecl *vtableClass) override;
  mlir::Value getVTableAddressPointInStructorWithVTT(
      CIRGenFunction &cgf, const CXXRecordDecl *vtableClass, BaseSubobject base,
      const CXXRecordDecl *nearestVBase);

  mlir::Value getVTableAddressPointInStructor(
      CIRGenFunction &cgf, const clang::CXXRecordDecl *vtableClass,
      clang::BaseSubobject base,
      const clang::CXXRecordDecl *nearestVBase) override;
  void emitVTableDefinitions(CIRGenVTables &cgvt,
                             const CXXRecordDecl *rd) override;
  void emitVirtualInheritanceTables(const CXXRecordDecl *rd) override;

  bool canSpeculativelyEmitVTable(const CXXRecordDecl *rd) const override;

  void setThunkLinkage(cir::FuncOp thunkFn, bool forVTable, GlobalDecl gd,
                       bool returnAdjustment) override {
    if (forVTable && !thunkFn.hasLocalLinkage())
      thunkFn.setLinkage(cir::GlobalLinkageKind::AvailableExternallyLinkage);
    const auto *nd = cast<NamedDecl>(gd.getDecl());
    cgm.setGVProperties(thunkFn, nd);
  }

  bool exportThunk() override { return true; }

  mlir::Value performThisAdjustment(CIRGenFunction &cgf, Address thisAddr,
                                    const CXXRecordDecl *unadjustedClass,
                                    const ThunkInfo &ti) override;

  mlir::Value performReturnAdjustment(CIRGenFunction &cgf, Address ret,
                                      const CXXRecordDecl *unadjustedClass,
                                      const ReturnAdjustment &ra) override;

  mlir::Attribute getAddrOfRTTIDescriptor(mlir::Location loc,
                                          QualType ty) override;
  CatchTypeInfo
  getAddrOfCXXCatchHandlerType(mlir::Location loc, QualType ty,
                               QualType catchHandlerType) override {
    auto rtti =
        dyn_cast_if_present<cir::GlobalViewAttr>(getAddrOfRTTIDescriptor(loc, ty));
    assert(rtti && "expected GlobalViewAttr");
    return CatchTypeInfo{rtti, 0};
  }

  bool doStructorsInitializeVPtrs(const CXXRecordDecl *vtableClass) override {
    return true;
  }

  size_t getSrcArgforCopyCtor(const CXXConstructorDecl *,
                              FunctionArgList &args) const override {
    assert(!args.empty() && "expected the arglist to not be empty!");
    return args.size() - 1;
  }

  void emitBadCastCall(CIRGenFunction &cgf, mlir::Location loc) override;

  mlir::Value
  getVirtualBaseClassOffset(mlir::Location loc, CIRGenFunction &cgf,
                            Address thisAddr, const CXXRecordDecl *classDecl,
                            const CXXRecordDecl *baseClassDecl) override;

  // The traditional clang CodeGen emits calls to `__dynamic_cast` directly into
  // LLVM in the `emitDynamicCastCall` function. In CIR, `dynamic_cast`
  // expressions are lowered to `cir.dyn_cast` ops instead of calls to runtime
  // functions. So during CIRGen we don't need the `emitDynamicCastCall`
  // function that clang CodeGen has.
  mlir::Value emitDynamicCast(CIRGenFunction &cgf, mlir::Location loc,
                              QualType srcRecordTy, QualType destRecordTy,
                              cir::PointerType destCIRTy, bool isRefCast,
                              Address src) override;

  Address initializeArrayCookie(CIRGenFunction &cgf, Address newPtr,
                                mlir::Value numElements, const CXXNewExpr *e,
                                QualType elementType) override;
  mlir::Value readArrayCookieImpl(CIRGenFunction &cgf, Address allocPtr,
                                  CharUnits cookieSize) override;

protected:
  CharUnits getArrayCookieSizeImpl(QualType elementType) override;

  /**************************** RTTI Uniqueness ******************************/
  /// Returns true if the ABI requires RTTI type_info objects to be unique
  /// across a program.
  virtual bool shouldRTTIBeUnique() const { return true; }

public:
  /// What sort of unique-RTTI behavior should we use?
  enum RTTIUniquenessKind {
    /// We are guaranteeing, or need to guarantee, that the RTTI string
    /// is unique.
    RUK_Unique,

    /// We are not guaranteeing uniqueness for the RTTI string, so we
    /// can demote to hidden visibility but must use string comparisons.
    RUK_NonUniqueHidden,

    /// We are not guaranteeing uniqueness for the RTTI string, so we
    /// have to use string comparisons, but we also have to emit it with
    /// non-hidden visibility.
    RUK_NonUniqueVisible
  };

  /// Return the required visibility status for the given type and linkage in
  /// the current ABI.
  RTTIUniquenessKind
  classifyRTTIUniqueness(QualType canTy, cir::GlobalLinkageKind linkage) const;

  /// Emit RTTI descriptors for all fundamental C++ types.
  void emitFundamentalRTTIDescriptors(const CXXRecordDecl *rd);
};

class CIRGenARMCXXABI : public CIRGenItaniumCXXABI {
public:
  CIRGenARMCXXABI(CIRGenModule &cgm)
      : CIRGenItaniumCXXABI(cgm, /*useARMMethodPtrABI=*/true,
                             /*useARMGuardVarABI=*/true) {
    // OGCG uses AppleARM64CXXABI (inheriting from ARMCXXABI) for Apple
    // ARM64 targets. Apple ARM64 is not a supported CIR target.
  }

  bool constructorsAndDestructorsReturnThis() const override { return true; }

  CharUnits getArrayCookieSizeImpl(QualType elementType) override;
  Address initializeArrayCookie(CIRGenFunction &cgf, Address newPtr,
                                mlir::Value numElements, const CXXNewExpr *e,
                                QualType elementType) override;
  mlir::Value readArrayCookieImpl(CIRGenFunction &cgf, Address ptr,
                                  CharUnits cookieSize) override;
};

class CIRGenFuchsiaCXXABI final : public CIRGenItaniumCXXABI {
public:
  explicit CIRGenFuchsiaCXXABI(CIRGenModule &cgm)
      : CIRGenItaniumCXXABI(cgm) {}

private:
  bool constructorsAndDestructorsReturnThis() const override { return true; }
};

class CIRGenAppleARM64CXXABI : public CIRGenARMCXXABI {
public:
  CIRGenAppleARM64CXXABI(CIRGenModule &cgm) : CIRGenARMCXXABI(cgm) {
    // OGCG's AppleARM64CXXABI sets Use32BitVTableOffsetABI = true for
    // relative vtable layout on Apple ARM64. Apple ARM64 is not a primary
    // CIR target; member pointer resolution in createGetMethod does not
    // yet truncate vtable offsets to 32 bits.
  }

  // ARM64 libraries are prepared for non-unique RTTI.
  bool shouldRTTIBeUnique() const override { return false; }
};

class CIRGenMicrosoftCXXABI final : public CIRGenItaniumCXXABI {
  [[noreturn]] void unsupportedFeature(llvm::StringRef feature) const {
    cgm.errorNYI(feature);
    llvm_unreachable("unsupported Microsoft C++ ABI feature in CIRGen");
  }

  [[noreturn]] void unsupportedFeature(SourceLocation loc,
                                       llvm::StringRef feature) const {
    cgm.errorNYI(loc, feature);
    llvm_unreachable("unsupported Microsoft C++ ABI feature in CIRGen");
  }

  MicrosoftVTableContext &getMicrosoftVTableContext() const {
    return cast<MicrosoftVTableContext>(*cgm.getASTContext().getVTableContext());
  }

  CharUnits getVirtualFunctionPrologueThisAdjustment(GlobalDecl gd) const;

public:
  explicit CIRGenMicrosoftCXXABI(CIRGenModule &cgm)
      : CIRGenItaniumCXXABI(cgm) {}

  void initializeHiddenVirtualInheritanceMembers(CIRGenFunction &cgf,
                                                 const CXXRecordDecl *rd)
      override {
    if (rd->getNumVBases())
      unsupportedFeature(rd->getLocation(),
                         "Microsoft virtual inheritance members");
  }

  mlir::Value getVirtualBaseClassOffset(mlir::Location loc,
                                        CIRGenFunction &cgf, Address thisAddr,
                                        const CXXRecordDecl *classDecl,
                                        const CXXRecordDecl *baseClassDecl)
      override {
    unsupportedFeature("Microsoft virtual base offsets");
  }

  mlir::Value emitDynamicCast(CIRGenFunction &cgf, mlir::Location loc,
                              QualType srcRecordTy, QualType destRecordTy,
                              cir::PointerType destCIRTy, bool isRefCast,
                              Address src) override {
    unsupportedFeature("Microsoft dynamic_cast");
  }

  bool hasThisReturn(GlobalDecl gd) const override;
  bool hasMostDerivedReturn(GlobalDecl gd) const override;
  bool isThisCompleteObject(GlobalDecl gd) const override;

  const CXXRecordDecl *getThisArgumentTypeForMethod(GlobalDecl gd) override;

  bool needsVTTParameter(GlobalDecl gd) override {
    const auto *md = cast<CXXMethodDecl>(gd.getDecl());
    if (md->getParent()->getNumVBases())
      unsupportedFeature(md->getLocation(), "Microsoft VTT parameters");
    return false;
  }

  Address adjustThisArgumentForVirtualFunctionCall(CIRGenFunction &cgf,
                                                   GlobalDecl gd,
                                                   Address thisPtr,
                                                   bool virtualCall) override {
    unsupportedFeature("Microsoft virtual this adjustment");
  }

  size_t getSrcArgforCopyCtor(const CXXConstructorDecl *cd,
                              FunctionArgList &args) const override;

  AddedStructorArgCounts
  buildStructorSignature(GlobalDecl gd,
                         llvm::SmallVectorImpl<CanQualType> &argTys) override;

  void addImplicitStructorParams(CIRGenFunction &cgf, QualType &resTy,
                                 FunctionArgList &params) override;

  void emitInstanceFunctionProlog(SourceLocation loc,
                                  CIRGenFunction &cgf) override;

  void emitRethrow(CIRGenFunction &cgf, bool isNoReturn) override {
    unsupportedFeature("Microsoft C++ rethrow");
  }

  void emitThrow(CIRGenFunction &cgf, const CXXThrowExpr *e) override {
    unsupportedFeature(e->getThrowLoc(), "Microsoft C++ throw");
  }

  void emitBadCastCall(CIRGenFunction &cgf, mlir::Location loc) override {
    unsupportedFeature("Microsoft bad_cast");
  }

  void emitBeginCatch(CIRGenFunction &cgf,
                      const CXXCatchStmt *catchStmt) override {
    unsupportedFeature(catchStmt->getBeginLoc(), "Microsoft begin catch");
  }

  mlir::Attribute getAddrOfRTTIDescriptor(mlir::Location loc,
                                          QualType ty) override {
    unsupportedFeature("Microsoft RTTI descriptors");
  }

  CatchTypeInfo getAddrOfCXXCatchHandlerType(mlir::Location loc, QualType ty,
                                             QualType catchHandlerType)
      override {
    unsupportedFeature("Microsoft catch handler types");
  }

  AddedStructorArgs getImplicitConstructorArgs(CIRGenFunction &cgf,
                                               const CXXConstructorDecl *d,
                                               CXXCtorType type,
                                               bool forVirtualBase,
                                               bool delegating) override;

  void emitCXXConstructors(const CXXConstructorDecl *d) override;
  void emitCXXDestructors(const CXXDestructorDecl *d) override;
  void emitCXXStructor(GlobalDecl gd) override;

  mlir::Value getCXXDestructorImplicitParam(CIRGenFunction &cgf,
                                            const CXXDestructorDecl *dd,
                                            CXXDtorType type,
                                            bool forVirtualBase,
                                            bool delegating) override;

  void emitDestructorCall(CIRGenFunction &cgf, const CXXDestructorDecl *dd,
                          CXXDtorType type, bool forVirtualBase,
                          bool delegating, Address thisAddr,
                          QualType thisTy) override;

  void registerGlobalDtor(const VarDecl *vd, cir::FuncOp dtor,
                          mlir::Value addr) override {
    unsupportedFeature(vd->getLocation(), "Microsoft global destructors");
  }

  void emitGuardedInit(CIRGenFunction &cgf, const VarDecl &d, cir::GlobalOp var,
                       bool shouldPerformInit) override {
    unsupportedFeature(d.getLocation(), "Microsoft guarded initialization");
  }

  void emitCXXThreadLocalInitFuncs() override {
    if (!cgm.getCXXThreadLocals().empty())
      unsupportedFeature("Microsoft TLS init wrappers");
  }

  void emitVirtualObjectDelete(CIRGenFunction &cgf, const CXXDeleteExpr *de,
                               Address ptr, QualType elementType,
                               const CXXDestructorDecl *dtor) override {
    unsupportedFeature("Microsoft virtual object delete");
  }

  cir::MethodAttr emitMemberFunctionPointer(const CXXMethodDecl *md,
                                            QualType memPtrTy,
                                            cir::MethodType ty,
                                            CharUnits thisAdjustment) override {
    unsupportedFeature(md->getLocation(), "Microsoft member function pointers");
  }

  bool isZeroInitializable(const MemberPointerType *mpt) override {
    unsupportedFeature("Microsoft member pointer zero initialization");
  }

  bool isVirtualOffsetNeededForVTableField(CIRGenFunction &cgf,
                                           CIRGenFunction::VPtr vptr) override {
    unsupportedFeature("Microsoft vtable virtual offsets");
  }

  bool canSpeculativelyEmitVTable(const CXXRecordDecl *rd) const override {
    unsupportedFeature("Microsoft speculative vtable emission");
  }

  void emitVTableDefinitions(CIRGenVTables &cgvt,
                             const CXXRecordDecl *rd) override {
    unsupportedFeature(rd->getLocation(), "Microsoft vtable definitions");
  }

  mlir::Value emitVirtualDestructorCall(CIRGenFunction &cgf,
                                        const CXXDestructorDecl *dtor,
                                        CXXDtorType dtorType, Address thisAddr,
                                        DeleteOrMemberCallExpr e) override {
    unsupportedFeature(dtor->getLocation(),
                       "Microsoft virtual destructor calls");
  }

  void emitVirtualInheritanceTables(const CXXRecordDecl *rd) override {
    unsupportedFeature(rd->getLocation(),
                       "Microsoft virtual inheritance tables");
  }

  bool exportThunk() override {
    unsupportedFeature("Microsoft C++ thunks");
  }

  void setThunkLinkage(cir::FuncOp thunkFn, bool forVTable, GlobalDecl gd,
                       bool returnAdjustment) override {
    unsupportedFeature("Microsoft thunk linkage");
  }

  mlir::Value performThisAdjustment(CIRGenFunction &cgf, Address thisAddr,
                                    const CXXRecordDecl *unadjustedClass,
                                    const ThunkInfo &ti) override {
    unsupportedFeature("Microsoft thunk this adjustment");
  }

  mlir::Value performReturnAdjustment(CIRGenFunction &cgf, Address ret,
                                      const CXXRecordDecl *unadjustedClass,
                                      const ReturnAdjustment &ra) override {
    unsupportedFeature("Microsoft thunk return adjustment");
  }

  bool useThunkForDtorVariant(const CXXDestructorDecl *dtor,
                              CXXDtorType dt) const override {
    return dt != Dtor_Base;
  }

  cir::GlobalLinkageKind
  getCXXDestructorLinkage(GVALinkage linkage, const CXXDestructorDecl *dtor,
                          CXXDtorType dt) const override {
    if (linkage == GVA_Internal)
      return cir::GlobalLinkageKind::InternalLinkage;

    switch (dt) {
    case Dtor_Base:
      return cgm.getCIRLinkageForDeclarator(dtor, linkage,
                                            /*isConstantVariable=*/false);
    case Dtor_Complete:
      if (dtor->hasAttr<DLLExportAttr>())
        return cir::GlobalLinkageKind::LinkOnceODRLinkage;
      if (dtor->hasAttr<DLLImportAttr>())
        return cir::GlobalLinkageKind::AvailableExternallyLinkage;
      return cir::GlobalLinkageKind::LinkOnceODRLinkage;
    case Dtor_Deleting:
      return cir::GlobalLinkageKind::LinkOnceODRLinkage;
    case Dtor_VectorDeleting:
      return cir::GlobalLinkageKind::WeakAnyLinkage;
    case Dtor_Unified:
      llvm_unreachable("MS C++ ABI does not support unified dtors");
    case Dtor_Comdat:
      llvm_unreachable("MS C++ ABI does not support comdat dtors");
    }
    llvm_unreachable("invalid dtor type");
  }

  cir::GlobalOp getAddrOfVTable(const CXXRecordDecl *rd,
                                CharUnits vptrOffset) override {
    unsupportedFeature(rd->getLocation(), "Microsoft vtable addresses");
  }

  CIRGenCallee getVirtualFunctionPointer(CIRGenFunction &cgf, GlobalDecl gd,
                                         Address thisAddr, mlir::Type ty,
                                         SourceLocation loc) override {
    unsupportedFeature(loc, "Microsoft virtual function pointers");
  }

  mlir::Value getVTableAddressPoint(BaseSubobject base,
                                    const CXXRecordDecl *vtableClass)
      override {
    unsupportedFeature(vtableClass->getLocation(),
                       "Microsoft vtable address points");
  }

  mlir::Value getVTableAddressPointInStructor(CIRGenFunction &cgf,
                                              const CXXRecordDecl *vtableClass,
                                              BaseSubobject base,
                                              const CXXRecordDecl *nearestVBase)
      override {
    unsupportedFeature(vtableClass->getLocation(),
                       "Microsoft structor vtable address points");
  }

  bool doStructorsInitializeVPtrs(const CXXRecordDecl *vtableClass) override {
    unsupportedFeature(vtableClass->getLocation(),
                       "Microsoft vptr initialization");
  }

protected:
  bool requiresArrayCookie(const CXXNewExpr *e) override {
    unsupportedFeature(e->getExprLoc(), "Microsoft array cookies");
  }

public:
  bool requiresArrayCookie(const CXXDeleteExpr *e,
                           QualType elementType) override {
    unsupportedFeature(e->getExprLoc(), "Microsoft array cookies");
  }

  Address initializeArrayCookie(CIRGenFunction &cgf, Address newPtr,
                                mlir::Value numElements, const CXXNewExpr *e,
                                QualType elementType) override {
    unsupportedFeature(e->getExprLoc(), "Microsoft array cookies");
  }

  CharUnits getArrayCookieSizeImpl(QualType elementType) override {
    unsupportedFeature("Microsoft array cookies");
  }

  mlir::Value readArrayCookieImpl(CIRGenFunction &cgf, Address ptr,
                                  CharUnits cookieSize) override {
    unsupportedFeature("Microsoft array cookies");
  }
};

} // namespace

void CIRGenItaniumCXXABI::emitInstanceFunctionProlog(SourceLocation loc,
                                                     CIRGenFunction &cgf) {
  // Naked functions have no prolog.
  if (cgf.curFuncDecl && cgf.curFuncDecl->hasAttr<NakedAttr>())
    return;

  /// Initialize the 'this' slot. In the Itanium C++ ABI, no prologue
  /// adjustments are required, because they are all handled by thunks.
  setCXXABIThisValue(cgf, loadIncomingCXXThis(cgf));

  /// Initialize the 'vtt' slot if needed.
  if (getStructorImplicitParamDecl(cgf)) {
    cir::LoadOp val = cgf.getBuilder().createLoad(
        cgf.getLoc(loc),
        cgf.getAddrOfLocalVar(getStructorImplicitParamDecl(cgf)));
    setStructorImplicitParamValue(cgf, val);
  }

  /// If this is a function that the ABI specifies returns 'this', initialize
  /// the return slot to this' at the start of the function.
  ///
  /// Unlike the setting of return types, this is done within the ABI
  /// implementation instead of by clients of CIRGenCXXBI because:
  /// 1) getThisValue is currently protected
  /// 2) in theory, an ABI could implement 'this' returns some other way;
  ///    HasThisReturn only specifies a contract, not the implementation
  if (hasThisReturn(cgf.curGD)) {
    cgf.getBuilder().CIRBaseBuilderTy::createStore(
        cgf.getLoc(loc), cgf.cxxabiThisValue,
        cgf.returnValue.emitRawPointer());
  }
}

static bool isDeletingDtor(GlobalDecl gd) {
  return isa<CXXDestructorDecl>(gd.getDecl()) &&
         (gd.getDtorType() == Dtor_Deleting ||
          gd.getDtorType() == Dtor_VectorDeleting);
}

bool CIRGenMicrosoftCXXABI::hasThisReturn(GlobalDecl gd) const {
  return isa<CXXConstructorDecl>(gd.getDecl());
}

bool CIRGenMicrosoftCXXABI::hasMostDerivedReturn(GlobalDecl gd) const {
  return isDeletingDtor(gd);
}

bool CIRGenMicrosoftCXXABI::isThisCompleteObject(GlobalDecl gd) const {
  if (!isa<CXXDestructorDecl>(gd.getDecl()))
    return false;

  switch (gd.getDtorType()) {
  case Dtor_Complete:
  case Dtor_Deleting:
  case Dtor_VectorDeleting:
    return true;
  case Dtor_Base:
    return false;
  case Dtor_Comdat:
    llvm_unreachable("emitting dtor comdat as function?");
  case Dtor_Unified:
    llvm_unreachable("unexpected unified dtor type");
  }
  llvm_unreachable("bad dtor kind");
}

const CXXRecordDecl *
CIRGenMicrosoftCXXABI::getThisArgumentTypeForMethod(GlobalDecl gd) {
  auto *md = cast<CXXMethodDecl>(gd.getDecl());

  if (md->isVirtual()) {
    GlobalDecl lookupGD = gd;
    if (const auto *dd = dyn_cast<CXXDestructorDecl>(md)) {
      if (gd.getDtorType() == Dtor_Complete)
        return md->getParent();

      lookupGD = GlobalDecl(dd, cgm.getASTContext()
                                    .getTargetInfo()
                                    .emitVectorDeletingDtors(
                                        cgm.getASTContext().getLangOpts())
                                ? Dtor_VectorDeleting
                                : Dtor_Deleting);
    }

    MethodVFTableLocation ml =
        getMicrosoftVTableContext().getMethodVFTableLocation(lookupGD);
    if (ml.VBase || !ml.VFPtrOffset.isZero())
      return nullptr;
  }

  return md->getParent();
}

size_t CIRGenMicrosoftCXXABI::getSrcArgforCopyCtor(const CXXConstructorDecl *cd,
                                                   FunctionArgList &args) const {
  assert(args.size() >= 2 && "expected the arglist to have at least two args");
  if (cd->getParent()->getNumVBases() > 0 &&
      cd->getType()->castAs<FunctionProtoType>()->isVariadic())
    return 2;
  return 1;
}

CharUnits CIRGenMicrosoftCXXABI::getVirtualFunctionPrologueThisAdjustment(
    GlobalDecl gd) const {
  const auto *md = cast<CXXMethodDecl>(gd.getDecl());

  if (const auto *dd = dyn_cast<CXXDestructorDecl>(md)) {
    if (gd.getDtorType() == Dtor_Complete)
      return CharUnits::Zero();

    gd = GlobalDecl(dd, cgm.getASTContext()
                            .getTargetInfo()
                            .emitVectorDeletingDtors(
                                cgm.getASTContext().getLangOpts())
                            ? Dtor_VectorDeleting
                            : Dtor_Deleting);
  }

  MethodVFTableLocation ml =
      getMicrosoftVTableContext().getMethodVFTableLocation(gd);
  CharUnits adjustment = ml.VFPtrOffset;

  if (isa<CXXDestructorDecl>(md))
    adjustment = CharUnits::Zero();

  if (ml.VBase) {
    const ASTRecordLayout &layout =
        cgm.getASTContext().getASTRecordLayout(md->getParent());
    adjustment += layout.getVBaseClassOffset(ml.VBase);
  }

  return adjustment;
}

CIRGenCXXABI::AddedStructorArgCounts
CIRGenMicrosoftCXXABI::buildStructorSignature(
    GlobalDecl gd, llvm::SmallVectorImpl<CanQualType> &argTys) {
  AddedStructorArgCounts added;
  if (isa<CXXDestructorDecl>(gd.getDecl()) && isDeletingDtor(gd)) {
    argTys.push_back(cgm.getASTContext().IntTy);
    ++added.suffix;
  }

  const auto *cd = dyn_cast<CXXConstructorDecl>(gd.getDecl());
  if (!cd)
    return added;

  const CXXRecordDecl *klass = cd->getParent();
  const auto *fpt = cd->getType()->castAs<FunctionProtoType>();
  if (!klass->getNumVBases())
    return added;

  if (fpt->isVariadic()) {
    argTys.insert(argTys.begin() + 1, cgm.getASTContext().IntTy);
    ++added.prefix;
  } else {
    argTys.push_back(cgm.getASTContext().IntTy);
    ++added.suffix;
  }
  return added;
}

void CIRGenMicrosoftCXXABI::addImplicitStructorParams(CIRGenFunction &cgf,
                                                      QualType &resTy,
                                                      FunctionArgList &params) {
  const auto *md = cast<CXXMethodDecl>(cgf.curGD.getDecl());
  assert((isa<CXXConstructorDecl>(md) || isa<CXXDestructorDecl>(md)) &&
         "expected constructor or destructor");

  ASTContext &astContext = cgm.getASTContext();
  if (isa<CXXConstructorDecl>(md) && md->getParent()->getNumVBases()) {
    auto *isMostDerived = ImplicitParamDecl::Create(
        astContext, /*DC=*/nullptr, md->getLocation(),
        &astContext.Idents.get("is_most_derived"), astContext.IntTy,
        ImplicitParamKind::Other);
    const auto *fpt = md->getType()->castAs<FunctionProtoType>();
    if (fpt->isVariadic())
      params.insert(params.begin() + 1, isMostDerived);
    else
      params.push_back(isMostDerived);
    getStructorImplicitParamDecl(cgf) = isMostDerived;
    return;
  }

  if (isDeletingDtor(cgf.curGD)) {
    auto *shouldDelete = ImplicitParamDecl::Create(
        astContext, /*DC=*/nullptr, md->getLocation(),
        &astContext.Idents.get("should_call_delete"), astContext.IntTy,
        ImplicitParamKind::Other);
    params.push_back(shouldDelete);
    getStructorImplicitParamDecl(cgf) = shouldDelete;
    resTy = astContext.VoidPtrTy;
  }
}

void CIRGenMicrosoftCXXABI::emitInstanceFunctionProlog(SourceLocation loc,
                                                       CIRGenFunction &cgf) {
  if (cgf.curFuncDecl && cgf.curFuncDecl->hasAttr<NakedAttr>())
    return;

  mlir::Value thisValue = loadIncomingCXXThis(cgf);
  const auto *md = cast<CXXMethodDecl>(cgf.curGD.getDecl());
  if (!cgf.curFuncIsThunk && md->isVirtual()) {
    CharUnits adjustment = getVirtualFunctionPrologueThisAdjustment(cgf.curGD);
    if (!adjustment.isZero()) {
      mlir::Value offset = cgf.getBuilder().getSignedInt(
          cgf.getLoc(loc), -adjustment.getQuantity(), /*width=*/64);
      thisValue = cgf.getBuilder().createPtrStride(cgf.getLoc(loc), thisValue,
                                                   offset);
    }
  }
  setCXXABIThisValue(cgf, thisValue);

  if (hasThisReturn(cgf.curGD) || hasMostDerivedReturn(cgf.curGD)) {
    cgf.getBuilder().CIRBaseBuilderTy::createStore(
        cgf.getLoc(loc), cgf.cxxabiThisValue,
        cgf.returnValue.emitRawPointer());
  }

  if (!getStructorImplicitParamDecl(cgf))
    return;

  cir::LoadOp val = cgf.getBuilder().createLoad(
      cgf.getLoc(loc), cgf.getAddrOfLocalVar(getStructorImplicitParamDecl(cgf)));
  setStructorImplicitParamValue(cgf, val);
}

CIRGenCXXABI::AddedStructorArgs
CIRGenMicrosoftCXXABI::getImplicitConstructorArgs(
    CIRGenFunction &cgf, const CXXConstructorDecl *d, CXXCtorType type,
    bool forVirtualBase, bool delegating) {
  assert((type == Ctor_Complete || type == Ctor_Base) &&
         "unexpected constructor kind for Microsoft ABI");
  if (!d->getParent()->getNumVBases())
    return AddedStructorArgs{};

  mlir::Value mostDerivedArg;
  if (delegating) {
    mostDerivedArg = getStructorImplicitParamValue(cgf);
  } else {
    mostDerivedArg = cgf.getBuilder().getConstInt(
        cgf.getLoc(d->getLocation()), cgf.getBuilder().getSInt32Ty(),
        type == Ctor_Complete ? 1 : 0);
  }

  if (d->getType()->castAs<FunctionProtoType>()->isVariadic())
    return AddedStructorArgs::withPrefix(
        {{mostDerivedArg, cgm.getASTContext().IntTy}});
  return AddedStructorArgs::withSuffix(
      {{mostDerivedArg, cgm.getASTContext().IntTy}});
}

void CIRGenMicrosoftCXXABI::emitCXXConstructors(const CXXConstructorDecl *d) {
  cgm.emitGlobal(GlobalDecl(d, Ctor_Complete));
}

void CIRGenMicrosoftCXXABI::emitCXXDestructors(const CXXDestructorDecl *d) {
  // The TU defining a Microsoft ABI destructor only guarantees the base
  // destructor definition. Equivalent complete destructors are redirected to
  // the base variant by CIRGenModule.
  cgm.emitGlobal(GlobalDecl(d, Dtor_Base));

  if (d->getParent()->getNumVBases() > 0 && d->hasAttr<DLLExportAttr>())
    cgm.emitGlobal(GlobalDecl(d, Dtor_Complete));
}

void CIRGenMicrosoftCXXABI::emitCXXStructor(GlobalDecl gd) {
  if (const auto *cd = dyn_cast<CXXConstructorDecl>(gd.getDecl())) {
    if (cd->getParent()->getNumVBases())
      unsupportedFeature(cd->getLocation(),
                         "Microsoft constructors with virtual bases");

    cir::FuncOp fn = cgm.codegenCXXStructor(GlobalDecl(cd, Ctor_Complete));
    cgm.maybeSetTrivialComdat(*cd, fn);
    return;
  }

  const auto *dd = cast<CXXDestructorDecl>(gd.getDecl());

  if (gd.getDtorType() == Dtor_Complete && dd->getParent()->getNumVBases() == 0)
    gd = gd.getWithDtorType(Dtor_Base);

  if (gd.getDtorType() == Dtor_Base && !cgm.tryEmitBaseDestructorAsAlias(dd))
    return;

  if (isDeletingDtor(gd))
    unsupportedFeature(dd->getLocation(), "Microsoft deleting destructors");

  cir::FuncOp fn = cgm.codegenCXXStructor(gd);
  cgm.maybeSetTrivialComdat(*dd, fn);
}

mlir::Value CIRGenMicrosoftCXXABI::getCXXDestructorImplicitParam(
    CIRGenFunction &cgf, const CXXDestructorDecl *dd, CXXDtorType type,
    bool forVirtualBase, bool delegating) {
  return nullptr;
}

void CIRGenMicrosoftCXXABI::emitDestructorCall(CIRGenFunction &cgf,
                                               const CXXDestructorDecl *dd,
                                               CXXDtorType type,
                                               bool forVirtualBase,
                                               bool delegating,
                                               Address thisAddr,
                                               QualType thisTy) {
  if (type == Dtor_Complete && dd->getParent()->getNumVBases() == 0)
    type = Dtor_Base;

  if (forVirtualBase)
    unsupportedFeature(dd->getLocation(),
                       "Microsoft virtual-base destructor calls");

  if (dd->isVirtual())
    unsupportedFeature(dd->getLocation(), "Microsoft virtual destructor calls");

  GlobalDecl gd(dd, type);
  CIRGenCallee callee = CIRGenCallee::forDirect(cgm.getAddrOfCXXStructor(gd), gd);
  cgf.emitCXXDestructorCall(gd, callee, thisAddr.getPointer(), thisTy, nullptr,
                            QualType(), nullptr);
}

CIRGenCXXABI::AddedStructorArgCounts
CIRGenItaniumCXXABI::buildStructorSignature(
    GlobalDecl gd, llvm::SmallVectorImpl<CanQualType> &argTys) {
  clang::ASTContext &astContext = cgm.getASTContext();

  // All parameters are already in place except VTT, which goes after 'this'.
  // These are clang types, so we don't need to worry about sret yet.

  // Check if we need to add a VTT parameter (which has type void **).
  if ((isa<CXXConstructorDecl>(gd.getDecl()) ? gd.getCtorType() == Ctor_Base
                                             : gd.getDtorType() == Dtor_Base) &&
      cast<CXXMethodDecl>(gd.getDecl())->getParent()->getNumVBases() != 0) {
    LangAS as = cgm.getGlobalVarAddressSpace(nullptr);
    QualType vttComponentTy =
        astContext.getAddrSpaceQualType(astContext.VoidPtrTy, as);
    argTys.insert(argTys.begin() + 1,
                  astContext.getPointerType(
                      CanQualType::CreateUnsafe(vttComponentTy)));
    return AddedStructorArgCounts::withPrefix(1);
  }

  return AddedStructorArgCounts{};
}

// Find out how to cirgen the complete destructor and constructor
namespace {
enum class StructorCIRGen { Emit, RAUW, DedupLate, Alias, COMDAT };
}

static StructorCIRGen getCIRGenToUse(CIRGenModule &cgm,
                                     const CXXMethodDecl *md) {
  if (!cgm.getCodeGenOpts().CXXCtorDtorAliases)
    return StructorCIRGen::Emit;

  // The complete and base structors are not equivalent if there are any virtual
  // bases, so emit separate functions.
  if (md->getParent()->getNumVBases())
    return StructorCIRGen::Emit;

  GlobalDecl aliasDecl;
  if (const auto *dd = dyn_cast<CXXDestructorDecl>(md)) {
    aliasDecl = GlobalDecl(dd, Dtor_Complete);
  } else {
    const auto *cd = cast<CXXConstructorDecl>(md);
    aliasDecl = GlobalDecl(cd, Ctor_Complete);
  }

  cir::GlobalLinkageKind linkage = cgm.getFunctionLinkage(aliasDecl);

  // OGCG uses RAUW here: it drops the complete-object structor entirely and
  // rewrites every reference to the base-object one. CIR cannot do that during
  // CIRGen, because CIRGenModule::applyReplacements() runs at release() time,
  // before CXXABILowering and LoweringPrepare — any C1/D1 reference those
  // passes materialize afterwards would be left dangling. Instead emit the
  // delegating stub and tag it, so LoweringPrepare can fold it away once the
  // module is fully formed. See CIR_StructorDedupAttr.
  if (cir::isDiscardableIfUnused(linkage))
    return StructorCIRGen::DedupLate;

  // FIXME: Should we allow available_externally aliases?
  if (!cir::isValidLinkage(linkage))
    return StructorCIRGen::DedupLate;

  if (cir::isWeakForLinker(linkage)) {
    // Only ELF and wasm support COMDATs with arbitrary names (C5/D5).
    if (cgm.getTarget().getTriple().isOSBinFormatELF() ||
        cgm.getTarget().getTriple().isOSBinFormatWasm())
      return StructorCIRGen::COMDAT;
    return StructorCIRGen::Emit;
  }

  return StructorCIRGen::Alias;
}

static void emitConstructorDestructorAlias(CIRGenModule &cgm,
                                           GlobalDecl aliasDecl,
                                           GlobalDecl targetDecl) {
  cir::GlobalLinkageKind linkage = cgm.getFunctionLinkage(aliasDecl);

  // Does this function alias already exists?
  StringRef mangledName = cgm.getMangledName(aliasDecl);
  auto entryOp = cgm.getGlobalValue(mangledName);
  auto globalValue = dyn_cast_or_null<cir::CIRGlobalValueInterface>(entryOp);
  if (globalValue && !globalValue.isDeclaration())
    return;

  auto entry = cast_or_null<cir::FuncOp>(entryOp);

  // Retrieve aliasee info.
  auto aliasee = cast<cir::FuncOp>(cgm.getAddrOfGlobal(targetDecl));

  // Populate actual alias.
  cgm.emitAliasForGlobal(mangledName, entry, aliasDecl, aliasee, linkage);
}

void CIRGenItaniumCXXABI::emitCXXStructor(GlobalDecl gd) {
  auto *md = cast<CXXMethodDecl>(gd.getDecl());
  StructorCIRGen cirGenType = getCIRGenToUse(cgm, md);
  const auto *cd = dyn_cast<CXXConstructorDecl>(md);
  const CXXDestructorDecl *dd = cd ? nullptr : cast<CXXDestructorDecl>(md);

  // Non-empty when this complete-object structor is emitted as a delegating
  // stub that LoweringPrepare should later fold into the named base variant.
  std::string dedupBaseName;

  if (cd ? gd.getCtorType() == Ctor_Complete
         : gd.getDtorType() == Dtor_Complete) {
    GlobalDecl baseDecl =
        cd ? gd.getWithCtorType(Ctor_Base) : gd.getWithDtorType(Dtor_Base);

    if (cirGenType == StructorCIRGen::Alias ||
        cirGenType == StructorCIRGen::COMDAT) {
      emitConstructorDestructorAlias(cgm, gd, baseDecl);
      return;
    }

    if (cirGenType == StructorCIRGen::RAUW) {
      StringRef mangledName = cgm.getMangledName(gd);
      // Ensure the base function exists in the module before we store
      // its name for replacement. Without this, applyReplacements()
      // may fail to find it via getGlobalValue().
      cgm.getAddrOfGlobal(baseDecl);
      // Replacement to the base symbol can happen before the base variant is
      // directly referenced in CIR (e.g. system-header inline ctors/dtors
      // under -mconstructor-aliases). Queue it for deferred emission now so
      // final IR never keeps a dangling declaration after replacements.
      cgm.addDeferredDeclToEmit(baseDecl);
      StringRef aliaseeName = cgm.getMangledName(baseDecl);
      cgm.addReplacement(mangledName, aliaseeName);
      return;
    }

    if (cirGenType == StructorCIRGen::DedupLate) {
      // Like the RAUW arm, but the replacement happens in LoweringPrepare
      // rather than here. Make sure the base variant really gets emitted,
      // since after the fold every reference will point at it.
      cgm.getAddrOfGlobal(baseDecl);
      cgm.addDeferredDeclToEmit(baseDecl);
      dedupBaseName = cgm.getMangledName(baseDecl).str();
    }
  }

  // The base destructor is equivalent to the base destructor of its base class
  // if there is exactly one non-virtual base class with a non-trivial
  // destructor, there are no fields with a non-trivial destructor, and the body
  // of the destructor is trivial.
  if (dd && gd.getDtorType() == Dtor_Base &&
      cirGenType != StructorCIRGen::COMDAT &&
      !cgm.tryEmitBaseDestructorAsAlias(dd))
    return;

  auto fn = cgm.codegenCXXStructor(gd);

  if (!dedupBaseName.empty()) {
    mlir::MLIRContext *ctx = &cgm.getMLIRContext();
    fn->setAttr(cir::StructorDedupAttr::getAttrName(),
                cir::StructorDedupAttr::get(
                    ctx, mlir::FlatSymbolRefAttr::get(ctx, dedupBaseName)));
  }

  if (cirGenType == StructorCIRGen::COMDAT) {
    // Place the base structor in a COMDAT group keyed by the special C5/D5
    // unified-structor name (e.g. _ZN1SC5Ev), not by the base structor's own
    // C2/D2 symbol name. The Itanium ABI mandates this so the whole structor
    // family (the C2/D2 body plus the C1/D1 alias that points into it) is
    // treated as a single unit by the linker. Without it, the group is named
    // after C2/D2 and a non-prevailing copy can drop the C1/D1 weak alias from
    // the output, breaking downstream links. This mirrors OGCG's COMDAT path
    // in ItaniumCXXABI.cpp.
    SmallString<256> buffer;
    llvm::raw_svector_ostream out(buffer);
    auto &mangleCtx = cast<ItaniumMangleContext>(getMangleContext());
    if (dd)
      mangleCtx.mangleCXXDtorComdat(dd, out);
    else
      mangleCtx.mangleCXXCtorComdat(cd, out);
    fn.setComdat(true);
    fn->setAttr("comdat_name",
                mlir::StringAttr::get(&cgm.getMLIRContext(), out.str()));
  } else {
    cgm.maybeSetTrivialComdat(*md, fn);
  }
}

void CIRGenItaniumCXXABI::addImplicitStructorParams(CIRGenFunction &cgf,
                                                    QualType &resTy,
                                                    FunctionArgList &params) {
  const auto *md = cast<CXXMethodDecl>(cgf.curGD.getDecl());
  assert(isa<CXXConstructorDecl>(md) || isa<CXXDestructorDecl>(md));

  // Check if we need a VTT parameter as well.
  if (needsVTTParameter(cgf.curGD)) {
    ASTContext &astContext = cgm.getASTContext();

    // FIXME: avoid the fake decl
    LangAS as = cgm.getGlobalVarAddressSpace(nullptr);
    QualType vttComponentTy =
        astContext.getAddrSpaceQualType(astContext.VoidPtrTy, as);
    QualType t = astContext.getPointerType(vttComponentTy);
    auto *vttDecl = ImplicitParamDecl::Create(
        astContext, /*DC=*/nullptr, md->getLocation(),
        &astContext.Idents.get("vtt"), t, ImplicitParamKind::CXXVTT);
    params.insert(params.begin() + 1, vttDecl);
    getStructorImplicitParamDecl(cgf) = vttDecl;
  }
}

void CIRGenItaniumCXXABI::emitCXXConstructors(const CXXConstructorDecl *d) {
  // Just make sure we're in sync with TargetCXXABI.
  assert(cgm.getTarget().getCXXABI().hasConstructorVariants());

  // The constructor used for constructing this as a base class;
  // ignores virtual bases.
  cgm.emitGlobal(GlobalDecl(d, Ctor_Base));

  // The constructor used for constructing this as a complete class;
  // constructs the virtual bases, then calls the base constructor.
  if (!d->getParent()->isAbstract()) {
    // We don't need to emit the complete ctro if the class is abstract.
    cgm.emitGlobal(GlobalDecl(d, Ctor_Complete));
  }
}

void CIRGenItaniumCXXABI::emitCXXDestructors(const CXXDestructorDecl *d) {
  // The destructor used for destructing this as a base class; ignores
  // virtual bases.
  cgm.emitGlobal(GlobalDecl(d, Dtor_Base));

  // The destructor used for destructing this as a most-derived class;
  // call the base destructor and then destructs any virtual bases.
  cgm.emitGlobal(GlobalDecl(d, Dtor_Complete));

  // The destructor in a virtual table is always a 'deleting'
  // destructor, which calls the complete destructor and then uses the
  // appropriate operator delete.
  if (d->isVirtual())
    cgm.emitGlobal(GlobalDecl(d, Dtor_Deleting));
}

CIRGenCXXABI::AddedStructorArgs CIRGenItaniumCXXABI::getImplicitConstructorArgs(
    CIRGenFunction &cgf, const CXXConstructorDecl *d, CXXCtorType type,
    bool forVirtualBase, bool delegating) {
  if (!needsVTTParameter(GlobalDecl(d, type)))
    return AddedStructorArgs{};

  // Insert the implicit 'vtt' argument as the second argument. Make sure to
  // correctly reflect its address space, which can differ from generic on
  // some targets.
  mlir::Value vtt =
      cgf.getVTTParameter(GlobalDecl(d, type), forVirtualBase, delegating);
  LangAS as = cgm.getGlobalVarAddressSpace(nullptr);
  QualType vttComponentTy =
      cgm.getASTContext().getAddrSpaceQualType(cgm.getASTContext().VoidPtrTy,
                                               as);
  QualType vttTy = cgm.getASTContext().getPointerType(vttComponentTy);
  return AddedStructorArgs::withPrefix({{vtt, vttTy}});
}

/// Determine whether 'this' is a complete object within the given function.
/// The Itanium ABI has separate complete-object vs. base-object variants of
/// both constructors and destructors.
bool CIRGenItaniumCXXABI::isThisCompleteObject(GlobalDecl gd) const {
  // Check destructors.
  if (isa<CXXDestructorDecl>(gd.getDecl())) {
    switch (gd.getDtorType()) {
    case Dtor_Complete:
    case Dtor_Deleting:
      return true;
    case Dtor_Base:
      return false;
    case Dtor_Comdat:
      llvm_unreachable("emitting dtor comdat as function?");
    case Dtor_Unified:
      llvm_unreachable("emitting unified dtor as function?");
    case Dtor_VectorDeleting:
      llvm_unreachable("unexpected dtor kind for this ABI");
    }
    llvm_unreachable("bad dtor kind");
  }
  // Check constructors.
  if (isa<CXXConstructorDecl>(gd.getDecl())) {
    switch (gd.getCtorType()) {
    case Ctor_Complete:
      return true;
    case Ctor_Base:
      return false;
    case Ctor_CopyingClosure:
    case Ctor_DefaultClosure:
      llvm_unreachable("closure ctors in Itanium ABI?");
    case Ctor_Comdat:
      llvm_unreachable("emitting ctor comdat as function?");
    case Ctor_Unified:
      llvm_unreachable("emitting unified ctor as function?");
    }
    llvm_unreachable("bad ctor kind");
  }
  // No other kinds.
  return false;
}

/// Return whether the given global decl needs a VTT (virtual table table)
/// parameter, which it does if it's a base constructor or destructor with
/// virtual bases.
bool CIRGenItaniumCXXABI::needsVTTParameter(GlobalDecl gd) {
  auto *md = cast<CXXMethodDecl>(gd.getDecl());

  // We don't have any virtual bases, just return early.
  if (!md->getParent()->getNumVBases())
    return false;

  // Check if we have a base constructor.
  if (isa<CXXConstructorDecl>(md) && gd.getCtorType() == Ctor_Base)
    return true;

  // Check if we have a base destructor.
  if (isa<CXXDestructorDecl>(md) && gd.getDtorType() == Dtor_Base)
    return true;

  return false;
}

/// The Itanium ABI requires non-zero initialization only for data
/// member pointers, for which '0' is a valid offset.
bool CIRGenItaniumCXXABI::isZeroInitializable(const MemberPointerType *mpt) {
  return mpt->isMemberFunctionPointer();
}

cir::MethodAttr
CIRGenItaniumCXXABI::emitMemberFunctionPointer(const CXXMethodDecl *md,
                                               clang::QualType memPtrTy,
                                               cir::MethodType ty,
                                               CharUnits thisAdjustment) {
  assert(md->isInstance() && "Member function must not be static!");

  int64_t adj = thisAdjustment.getQuantity();

  // The MethodType `ty` may have a stale placeholder function type if the
  // return type involved an incomplete record when convertFunctionTypeInternal
  // was first called (it returns FuncType::get({}, voidTy) as a placeholder).
  // Re-derive the correct MethodType from the actual method declaration's type,
  // which is fully resolved by this point.
  const CIRGenFunctionInfo &fnInfo =
      cgm.getTypes().arrangeCXXMethodDeclaration(md);
  cir::FuncType fullFnTy = cgm.getTypes().getFunctionType(fnInfo);
  // fullFnTy includes 'this' as the first parameter. Strip it to get the
  // method's function type (member pointer representation excludes 'this').
  auto params = fullFnTy.getInputs();
  assert(!params.empty() && "method type must have at least 'this'");
  auto methodParams = params.drop_front(1); // skip 'this'
  auto methodFuncTy = cir::FuncType::get(
      llvm::SmallVector<mlir::Type>(methodParams.begin(), methodParams.end()),
      fullFnTy.getReturnType(), fullFnTy.isVarArg());
  auto correctedTy = cir::MethodType::get(methodFuncTy, ty.getClassTy());
  bool noCFI = md->getType()->isCFIUncheckedCalleeFunctionType();

  if (md->isVirtual()) {
    uint64_t index = cgm.getItaniumVTableContext().getMethodVTableIndex(md);
    uint64_t vtableOffset;
    if (cgm.getItaniumVTableContext().isRelativeLayout()) {
      // Multiply by 4-byte relative offsets.
      vtableOffset = index * 4;
    } else {
      const ASTContext &context = cgm.getASTContext();
      CharUnits pointerWidth = context.toCharUnitsFromBits(
          context.getTargetInfo().getPointerWidth(LangAS::Default));
      vtableOffset = index * pointerWidth.getQuantity();
    }

    const auto &schema = cgm.getCodeGenOpts().PointerAuth.CXXMemberFunctionPointers;
    if (useARMMethodPtrABI && schema) {
      const CXXMethodDecl *origMD =
          cast<CXXMethodDecl>(cgm.getItaniumVTableContext()
                                  .findOriginalMethod(md->getCanonicalDecl())
                                  .getDecl());
      cir::FuncOp thunkFn = getOrCreateVirtualFunctionPointerThunk(origMD);
      auto symbol = mlir::FlatSymbolRefAttr::get(thunkFn.getSymNameAttr());
      if (auto authInfo = cgm.getMemberFunctionPointerAuthInfo(memPtrTy))
        return cir::MethodAttr::get(correctedTy, symbol, adj,
                                    authInfo->discriminator, noCFI);
      return cir::MethodAttr::get(correctedTy, symbol, adj, noCFI);
    }
    return cir::MethodAttr::get(correctedTy, vtableOffset, adj);
  }

  // Check whether the function has a computable type. If a parameter type
  // is incomplete (e.g., forward-declared struct passed by value), we cannot
  // lower the full function type. Use an empty placeholder instead, matching
  // OGCG's isFuncTypeConvertible check (ItaniumCXXABI.cpp:1225).
  const FunctionProtoType *fpt = md->getType()->castAs<FunctionProtoType>();
  cir::FuncType fnTyForDecl = fullFnTy;
  if (!cgm.getTypes().isFuncTypeConvertible(fpt))
    fnTyForDecl = cir::FuncType::get({}, cgm.getBuilder().getVoidTy());

  cir::FuncOp methodFuncOp = cgm.getAddrOfFunction(md, fnTyForDecl);
  // OGCG signs the non-virtual member function pointer via
  // CGM.getMemberFunctionPointer() under the member function pointer
  // authentication schema. CIR defers encoding to lowering.
  auto symbol = mlir::FlatSymbolRefAttr::get(methodFuncOp.getSymNameAttr());
  if (auto authInfo = cgm.getMemberFunctionPointerAuthInfo(memPtrTy))
    return cir::MethodAttr::get(correctedTy, symbol, adj,
                                authInfo->discriminator, noCFI);
  return cir::MethodAttr::get(correctedTy, symbol, adj, noCFI);
}

cir::FuncOp CIRGenItaniumCXXABI::getOrCreateVirtualFunctionPointerThunk(
    const CXXMethodDecl *md) {
  SmallString<256> thunkName;
  llvm::raw_svector_ostream out(thunkName);
  getMangleContext().mangleCXXName(md, out);
  thunkName += "_vfpthunk_";

  const CIRGenFunctionInfo &fnInfo =
      cgm.getTypes().arrangeCXXMethodDeclaration(md);
  cir::FuncType thunkTy = cgm.getTypes().getFunctionType(fnInfo);
  GlobalDecl gd(md);
  cir::FuncOp thunkFn =
      cgm.getOrCreateCIRFunction(thunkName, thunkTy, gd, /*forVTable=*/true,
                                 /*dontDefer=*/true, /*isThunk=*/true);

  cir::GlobalLinkageKind linkage =
      md->isExternallyVisible() ? cir::GlobalLinkageKind::LinkOnceODRLinkage
                                : cir::GlobalLinkageKind::InternalLinkage;
  thunkFn.setLinkage(linkage);
  if (linkage == cir::GlobalLinkageKind::LinkOnceODRLinkage) {
    thunkFn.setGlobalVisibility(cir::VisibilityKind::Hidden);
    if (cgm.supportsCOMDAT())
      thunkFn.setComdat(true);
  }

  if (!thunkFn.isDeclaration())
    return thunkFn;

  cgm.setCIRFunctionAttributesForDefinition(gd, thunkFn, /*isThunk=*/true);
  if (auto extraAttrs = thunkFn.getExtraAttrs()) {
    mlir::NamedAttrList attrs{extraAttrs->getElements().getValue()};
    attrs.erase("ssp");
    attrs.erase("sspstrong");
    attrs.erase("sspreq");
    thunkFn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(&cgm.getMLIRContext())));
  }

  CIRGenBuilderTy &builder = cgm.getBuilder();
  mlir::OpBuilder::InsertionGuard guard(builder);
  CIRGenFunction cgf(cgm, builder, /*suppressNewContext=*/true);
  mlir::Block *entryBB = thunkFn.addEntryBlock();
  cgf.getBuilder().setInsertionPointToStart(entryBB);

  CIRGenFunction::SymTableScopeTy varScope(cgf.symbolTable);
  auto thunkLoc = thunkFn.getLoc();
  CIRGenFunction::LexicalScope lexScope{cgf, thunkLoc, entryBB};
  CIRGenFunction::SourceLocRAIIObject scopedLoc{cgf, thunkLoc};

  cgf.startThunk(thunkFn, gd, fnInfo, /*isUnprototyped=*/false);
  mlir::Value thisVal = cgm.getCXXABI().loadIncomingCXXThis(cgf);
  cgm.getCXXABI().setCXXABIThisValue(cgf, thisVal);

  CallArgList callArgs;
  callArgs.add(RValue::get(thisVal), md->getThisType());
  for (const ParmVarDecl *param : md->parameters())
    cgf.emitDelegateCallArg(callArgs, param, SourceLocation());

  const FunctionProtoType *fpt = md->getType()->castAs<FunctionProtoType>();
  RequiredArgs required =
      RequiredArgs::getFromProtoWithExtraSlots(fpt, /*additional=*/1);
  const CIRGenFunctionInfo &callInfo =
      cgm.getTypes().arrangeCXXMethodCall(callArgs, fpt, required, 0);
  CIRGenCallee callee =
      CIRGenCallee::forVirtual(nullptr, gd, cgf.loadCXXThisAddress(), thunkTy);

  cir::CIRCallOpInterface callOrTryCall;
  (void)cgf.emitCall(callInfo, callee, ReturnValueSlot(), callArgs,
                     &callOrTryCall, thunkLoc, /*isMustTail=*/true);
  cgf.finishThunk();
  return thunkFn;
}

void CIRGenItaniumCXXABI::emitVTableDefinitions(CIRGenVTables &cgvt,
                                                const CXXRecordDecl *rd) {
  cir::GlobalOp vtable = getAddrOfVTable(rd, CharUnits());
  if (vtable.hasInitializer())
    return;

  ItaniumVTableContext &vtContext = cgm.getItaniumVTableContext();
  const VTableLayout &vtLayout = vtContext.getVTableLayout(rd);
  cir::GlobalLinkageKind linkage = cgm.getVTableLinkage(rd);
  mlir::Attribute rtti =
      cgm.getAddrOfRTTIDescriptor(cgm.getLoc(rd->getBeginLoc()),
                                  cgm.getASTContext().getCanonicalTagType(rd));

  // Classic codegen uses ConstantInitBuilder here, which is a very general
  // and feature-rich class to generate initializers for global values.
  // For now, this is using a simpler approach to create the initializer in CIR.
  cgvt.createVTableInitializer(vtable, vtLayout, rtti,
                               cir::isLocalLinkage(linkage));

  // Set the correct linkage.
  vtable.setLinkage(linkage);

  if (cgm.supportsCOMDAT() && cir::isWeakForLinker(linkage))
    vtable.setComdat(true);

  // Set the right visibility.
  cgm.setGVProperties(vtable, rd);

  // OGCG calls setVTableSelectiveDLLImportExport for PS4/PS5 targets with
  // DLL import/export support. PS4/PS5 is not a supported CIR target.

  // Carry vtable type metadata information for the same cases as OGCG:
  // LTO, IR instrumentation, or speculative devirtualization, with the
  // available_externally widening for WPD/specdevirt.
  bool shouldAttachVTableTypeMetadata =
      cgm.getCodeGenOpts().LTOUnit ||
      cgm.getCodeGenOpts().hasProfileIRInstr() ||
      cgm.getCodeGenOpts().DevirtualizeSpeculatively;
  bool shouldEmitForThisVTable =
      !vtable.isDeclarationForLinker() ||
      cgm.getCodeGenOpts().WholeProgramVTables ||
      cgm.getCodeGenOpts().DevirtualizeSpeculatively;
  if (shouldAttachVTableTypeMetadata && shouldEmitForThisVTable) {
    SmallVector<mlir::Attribute, 8> typeEntries;
    const auto &layout = vtLayout;
    const auto &addressPoints = layout.getAddressPoints();
    unsigned componentSize =
        cgm.getASTContext().getTargetInfo().getPointerWidth(
            LangAS::Default) /
        8;

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
            comps[i].getFunctionDecl()->getType(),
            std::nullopt, ap.base->getCanonicalDecl());
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

  // If this is the magic class __cxxabiv1::__fundamental_type_info,
  // we will emit the typeinfo for the fundamental types. This is the
  // same behaviour as GCC.
  const DeclContext *DC = rd->getDeclContext();
  if (rd->getIdentifier() &&
      rd->getIdentifier()->isStr("__fundamental_type_info") &&
      isa<NamespaceDecl>(DC) && cast<NamespaceDecl>(DC)->getIdentifier() &&
      cast<NamespaceDecl>(DC)->getIdentifier()->isStr("__cxxabiv1") &&
      DC->getParent()->isTranslationUnit()) {
    emitFundamentalRTTIDescriptors(rd);
  }

  [[maybe_unused]] auto vtableAsGlobalValue =
      dyn_cast<cir::CIRGlobalValueInterface>(*vtable);
  assert(vtableAsGlobalValue && "VTable must support CIRGlobalValueInterface");
  // For available_externally definitions, preserve the vtable through
  // whole-program analysis just like OGCG.
  if (vtable.isDeclarationForLinker() &&
      (cgm.getCodeGenOpts().WholeProgramVTables ||
       cgm.getCodeGenOpts().DevirtualizeSpeculatively))
    cgm.addCompilerUsedGlobal(vtable);

  if (vtContext.isRelativeLayout()) {
    if (cgm.getLangOpts().Sanitize.has(SanitizerKind::HWAddress))
      vtable->setAttr("cir.no_sanitize_hwaddress",
                      mlir::UnitAttr::get(vtable->getContext()));
    if (!vtable.getDsoLocal() || vtable.getComdat())
      cgvt.generateRelativeVTableAlias(vtable, vtable.getSymName());
  }

  assert(!cir::MissingFeatures::debugInfoCallSiteInfo());
}

bool CIRGenItaniumCXXABI::canSpeculativelyEmitVTable(
    const CXXRecordDecl *rd) const {
  if (!canSpeculativelyEmitVTableAsBaseClass(rd))
    return false;

  if (rd->shouldEmitInExternalSource())
    return false;

  // For a complete-object vtable (or more specifically, for the VTT), we need
  // to be able to speculatively emit the vtables of all dynamic virtual bases.
  for (const auto &b : rd->vbases()) {
    auto *brd = b.getType()->getAsCXXRecordDecl();
    assert(brd && "no class for base specifier");
    if (!brd->isDynamicClass())
      continue;
    if (!canSpeculativelyEmitVTableAsBaseClass(brd))
      return false;
  }

  return true;
}

mlir::Value CIRGenItaniumCXXABI::emitVirtualDestructorCall(
    CIRGenFunction &cgf, const CXXDestructorDecl *dtor, CXXDtorType dtorType,
    Address thisAddr, DeleteOrMemberCallExpr expr) {
  auto *callExpr = dyn_cast<const CXXMemberCallExpr *>(expr);
  auto *delExpr = dyn_cast<const CXXDeleteExpr *>(expr);
  assert((callExpr != nullptr) ^ (delExpr != nullptr));
  assert(callExpr == nullptr || callExpr->arg_begin() == callExpr->arg_end());
  assert(dtorType == Dtor_Deleting || dtorType == Dtor_Complete);

  GlobalDecl globalDecl(dtor, dtorType);
  const CIRGenFunctionInfo *fnInfo =
      &cgm.getTypes().arrangeCXXStructorDeclaration(globalDecl);
  const cir::FuncType &fnTy = cgm.getTypes().getFunctionType(*fnInfo);
  auto callee = CIRGenCallee::forVirtual(callExpr, globalDecl, thisAddr, fnTy);

  QualType thisTy =
      callExpr ? callExpr->getObjectType() : delExpr->getDestroyedType();

  cgf.emitCXXDestructorCall(globalDecl, callee, thisAddr.emitRawPointer(),
                            thisTy, nullptr, QualType(), nullptr);
  return nullptr;
}

void CIRGenItaniumCXXABI::emitVirtualInheritanceTables(
    const CXXRecordDecl *rd) {
  CIRGenVTables &vtables = cgm.getVTables();
  cir::GlobalOp vtt = vtables.getAddrOfVTT(rd);
  vtables.emitVTTDefinition(vtt, cgm.getVTableLinkage(rd), rd);
}

namespace {
class CIRGenItaniumRTTIBuilder {
  CIRGenModule &cgm;                 // Per-module state.
  const CIRGenItaniumCXXABI &cxxABI; // Per-module state.

  /// The fields of the RTTI descriptor currently being built.
  SmallVector<mlir::Attribute, 16> fields;

  // Returns the mangled type name of the given type.
  cir::GlobalOp getAddrOfTypeName(mlir::Location loc, QualType ty,
                                  cir::GlobalLinkageKind linkage);

  /// descriptor of the given type.
  mlir::Attribute getAddrOfExternalRTTIDescriptor(mlir::Location loc,
                                                  QualType ty);

  /// Build the vtable pointer for the given type.
  void buildVTablePointer(mlir::Location loc, const Type *ty);

  /// Build an abi::__si_class_type_info, used for single inheritance, according
  /// to the Itanium C++ ABI, 2.9.5p6b.
  void buildSIClassTypeInfo(mlir::Location loc, const CXXRecordDecl *rd);

  /// Build the RTTI payload for an Objective-C object or interface type.
  void buildObjCObjectTypeInfo(mlir::Location loc, const ObjCObjectType *ty);

  /// Build an abi::__pointer_type_info struct, used for pointer types.
  void buildPointerTypeInfo(mlir::Location loc, QualType pointeeTy);

  /// Build an abi::__pointer_to_member_type_info struct.
  void buildPointerToMemberTypeInfo(mlir::Location loc,
                                     const MemberPointerType *ty);

  /// Build an abi::__vmi_class_type_info, used for
  /// classes with bases that do not satisfy the abi::__si_class_type_info
  /// constraints, according ti the Itanium C++ ABI, 2.9.5p5c.
  void buildVMIClassTypeInfo(mlir::Location loc, const CXXRecordDecl *rd);

public:
  CIRGenItaniumRTTIBuilder(const CIRGenItaniumCXXABI &abi, CIRGenModule &cgm)
      : cgm(cgm), cxxABI(abi) {}

  /// Build the RTTI type info struct for the given type, or
  /// link to an existing RTTI descriptor if one already exists.
  mlir::Attribute buildTypeInfo(mlir::Location loc, QualType ty);

  /// Build the RTTI type info struct for the given type.
  mlir::Attribute buildTypeInfo(mlir::Location loc, QualType ty,
                                cir::GlobalLinkageKind linkage,
                                mlir::SymbolTable::Visibility visibility);
};
} // namespace

// TODO(cir): Will be removed after sharing them with the classical codegen
namespace {

// Pointer type info flags.
enum {
  /// PTI_Const - Type has const qualifier.
  PTI_Const = 0x1,

  /// PTI_Volatile - Type has volatile qualifier.
  PTI_Volatile = 0x2,

  /// PTI_Restrict - Type has restrict qualifier.
  PTI_Restrict = 0x4,

  /// PTI_Incomplete - Type is incomplete.
  PTI_Incomplete = 0x8,

  /// PTI_ContainingClassIncomplete - Containing class is incomplete.
  /// (in pointer to member).
  PTI_ContainingClassIncomplete = 0x10,

  /// PTI_TransactionSafe - Pointee is transaction_safe function (C++ TM TS).
  // PTI_TransactionSafe = 0x20,

  /// PTI_Noexcept - Pointee is noexcept function (C++1z).
  PTI_Noexcept = 0x40,
};

// VMI type info flags.
enum {
  /// VMI_NonDiamondRepeat - Class has non-diamond repeated inheritance.
  VMI_NonDiamondRepeat = 0x1,

  /// VMI_DiamondShaped - Class is diamond shaped.
  VMI_DiamondShaped = 0x2
};

// Base class type info flags.
enum {
  /// BCTI_Virtual - Base class is virtual.
  BCTI_Virtual = 0x1,

  /// BCTI_Public - Base class is public.
  BCTI_Public = 0x2
};

/// Given a builtin type, returns whether the type
/// info for that type is defined in the standard library.
/// TODO(cir): this can unified with LLVM codegen
static bool typeInfoIsInStandardLibrary(const BuiltinType *ty) {
  // Itanium C++ ABI 2.9.2:
  //   Basic type information (e.g. for "int", "bool", etc.) will be kept in
  //   the run-time support library. Specifically, the run-time support
  //   library should contain type_info objects for the types X, X* and
  //   X const*, for every X in: void, std::nullptr_t, bool, wchar_t, char,
  //   unsigned char, signed char, short, unsigned short, int, unsigned int,
  //   long, unsigned long, long long, unsigned long long, float, double,
  //   long double, char16_t, char32_t, and the IEEE 754r decimal and
  //   half-precision floating point types.
  //
  // GCC also emits RTTI for __int128.
  // FIXME: We do not emit RTTI information for decimal types here.

  // Types added here must also be added to emitFundamentalRTTIDescriptors.
  switch (ty->getKind()) {
  case BuiltinType::WasmExternRef:
  case BuiltinType::HLSLResource:
    llvm_unreachable("NYI");
  case BuiltinType::Void:
  case BuiltinType::NullPtr:
  case BuiltinType::Bool:
  case BuiltinType::WChar_S:
  case BuiltinType::WChar_U:
  case BuiltinType::Char_U:
  case BuiltinType::Char_S:
  case BuiltinType::UChar:
  case BuiltinType::SChar:
  case BuiltinType::Short:
  case BuiltinType::UShort:
  case BuiltinType::Int:
  case BuiltinType::UInt:
  case BuiltinType::Long:
  case BuiltinType::ULong:
  case BuiltinType::LongLong:
  case BuiltinType::ULongLong:
  case BuiltinType::Half:
  case BuiltinType::Float:
  case BuiltinType::Double:
  case BuiltinType::LongDouble:
  case BuiltinType::Float16:
  case BuiltinType::Float128:
  case BuiltinType::Ibm128:
  case BuiltinType::Char8:
  case BuiltinType::Char16:
  case BuiltinType::Char32:
  case BuiltinType::Int128:
  case BuiltinType::UInt128:
    return true;

#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix)                   \
  case BuiltinType::Id:
#include "clang/Basic/OpenCLImageTypes.def"
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) case BuiltinType::Id:
#include "clang/Basic/OpenCLExtensionTypes.def"
  case BuiltinType::OCLSampler:
  case BuiltinType::OCLEvent:
  case BuiltinType::OCLClkEvent:
  case BuiltinType::OCLQueue:
  case BuiltinType::OCLReserveID:
#define SVE_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/AArch64ACLETypes.def"
#define PPC_VECTOR_TYPE(Name, Id, Size) case BuiltinType::Id:
#include "clang/Basic/PPCTypes.def"
#define RVV_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/RISCVVTypes.def"
#define AMDGPU_TYPE(Name, Id, SingletonId, Width, Align) case BuiltinType::Id:
#include "clang/Basic/AMDGPUTypes.def"
  case BuiltinType::ShortAccum:
  case BuiltinType::Accum:
  case BuiltinType::LongAccum:
  case BuiltinType::UShortAccum:
  case BuiltinType::UAccum:
  case BuiltinType::ULongAccum:
  case BuiltinType::ShortFract:
  case BuiltinType::Fract:
  case BuiltinType::LongFract:
  case BuiltinType::UShortFract:
  case BuiltinType::UFract:
  case BuiltinType::ULongFract:
  case BuiltinType::SatShortAccum:
  case BuiltinType::SatAccum:
  case BuiltinType::SatLongAccum:
  case BuiltinType::SatUShortAccum:
  case BuiltinType::SatUAccum:
  case BuiltinType::SatULongAccum:
  case BuiltinType::SatShortFract:
  case BuiltinType::SatFract:
  case BuiltinType::SatLongFract:
  case BuiltinType::SatUShortFract:
  case BuiltinType::SatUFract:
  case BuiltinType::SatULongFract:
  case BuiltinType::BFloat16:
    return false;

  case BuiltinType::Dependent:
#define BUILTIN_TYPE(Id, SingletonId)
#define PLACEHOLDER_TYPE(Id, SingletonId) case BuiltinType::Id:
#include "clang/AST/BuiltinTypes.def"
    llvm_unreachable("asking for RRTI for a placeholder type!");

  case BuiltinType::ObjCId:
  case BuiltinType::ObjCClass:
  case BuiltinType::ObjCSel:
    llvm_unreachable("FIXME: Objective-C types are unsupported!");
  }

  llvm_unreachable("Invalid BuiltinType Kind!");
}

static bool typeInfoIsInStandardLibrary(const PointerType *pointerTy) {
  QualType pointeeTy = pointerTy->getPointeeType();
  const auto *builtinTy = dyn_cast<BuiltinType>(pointeeTy);
  if (!builtinTy)
    return false;

  // Check the qualifiers.
  Qualifiers quals = pointeeTy.getQualifiers();
  quals.removeConst();

  if (!quals.empty())
    return false;

  return typeInfoIsInStandardLibrary(builtinTy);
}

/// IsStandardLibraryRTTIDescriptor - Returns whether the type
/// information for the given type exists in the standard library.
static bool isStandardLibraryRttiDescriptor(QualType ty) {
  // Type info for builtin types is defined in the standard library.
  if (const auto *builtinTy = dyn_cast<BuiltinType>(ty))
    return typeInfoIsInStandardLibrary(builtinTy);

  // Type info for some pointer types to builtin types is defined in the
  // standard library.
  if (const auto *pointerTy = dyn_cast<PointerType>(ty))
    return typeInfoIsInStandardLibrary(pointerTy);

  return false;
}

/// ShouldUseExternalRTTIDescriptor - Returns whether the type information for
/// the given type exists somewhere else, and that we should not emit the type
/// information in this translation unit.  Assumes that it is not a
/// standard-library type.
static bool shouldUseExternalRttiDescriptor(CIRGenModule &cgm, QualType ty) {
  ASTContext &context = cgm.getASTContext();

  // If RTTI is disabled, assume it might be disabled in the
  // translation unit that defines any potential key function, too.
  if (!context.getLangOpts().RTTI)
    return false;

  if (const auto *recordTy = dyn_cast<RecordType>(ty)) {
    const auto *rd =
        cast<CXXRecordDecl>(recordTy->getDecl())->getDefinitionOrSelf();
    if (!rd->hasDefinition())
      return false;

    if (!rd->isDynamicClass())
      return false;

    // FIXME: this may need to be reconsidered if the key function
    // changes.
    // N.B. We must always emit the RTTI data ourselves if there exists a key
    // function.
    bool isDLLImport = rd->hasAttr<DLLImportAttr>();

    // Don't import the RTTI but emit it locally.
    if (cgm.getTriple().isOSCygMing())
      return false;

    if (cgm.getVTables().isVTableExternal(rd)) {
      if (cgm.getTarget().hasPS4DLLImportExport())
        return true;

      return !isDLLImport || cgm.getTriple().isWindowsItaniumEnvironment();
    }

    if (isDLLImport)
      return true;
  }

  return false;
}

/// Contains virtual and non-virtual bases seen when traversing a class
/// hierarchy.
struct SeenBases {
  llvm::SmallPtrSet<const CXXRecordDecl *, 16> nonVirtualBases;
  llvm::SmallPtrSet<const CXXRecordDecl *, 16> virtualBases;
};

/// Compute the value of the flags member in abi::__vmi_class_type_info.
///
static unsigned computeVmiClassTypeInfoFlags(const CXXBaseSpecifier *base,
                                             SeenBases &bases) {

  unsigned flags = 0;
  auto *baseDecl = base->getType()->castAsCXXRecordDecl();

  if (base->isVirtual()) {
    // Mark the virtual base as seen.
    if (!bases.virtualBases.insert(baseDecl).second) {
      // If this virtual base has been seen before, then the class is diamond
      // shaped.
      flags |= VMI_DiamondShaped;
    } else {
      if (bases.nonVirtualBases.count(baseDecl))
        flags |= VMI_NonDiamondRepeat;
    }
  } else {
    // Mark the non-virtual base as seen.
    if (!bases.nonVirtualBases.insert(baseDecl).second) {
      // If this non-virtual base has been seen before, then the class has non-
      // diamond shaped repeated inheritance.
      flags |= VMI_NonDiamondRepeat;
    } else {
      if (bases.virtualBases.count(baseDecl))
        flags |= VMI_NonDiamondRepeat;
    }
  }

  // Walk all bases.
  for (const auto &bs : baseDecl->bases())
    flags |= computeVmiClassTypeInfoFlags(&bs, bases);

  return flags;
}

static unsigned computeVmiClassTypeInfoFlags(const CXXRecordDecl *rd) {
  unsigned flags = 0;
  SeenBases bases;

  // Walk all bases.
  for (const auto &bs : rd->bases())
    flags |= computeVmiClassTypeInfoFlags(&bs, bases);

  return flags;
}

// Return whether the given record decl has a "single,
// public, non-virtual base at offset zero (i.e. the derived class is dynamic
// iff the base is)", according to Itanium C++ ABI, 2.95p6b.
// TODO(cir): this can unified with LLVM codegen
static bool canUseSingleInheritance(const CXXRecordDecl *rd) {
  // Check the number of bases.
  if (rd->getNumBases() != 1)
    return false;

  // Get the base.
  CXXRecordDecl::base_class_const_iterator base = rd->bases_begin();

  // Check that the base is not virtual.
  if (base->isVirtual())
    return false;

  // Check that the base is public.
  if (base->getAccessSpecifier() != AS_public)
    return false;

  // Check that the class is dynamic iff the base is.
  auto *baseDecl = base->getType()->castAsCXXRecordDecl();
  return baseDecl->isEmpty() ||
         baseDecl->isDynamicClass() == rd->isDynamicClass();
}

/// IsIncompleteClassType - Returns whether the given record type is incomplete.
static bool isIncompleteClassType(const RecordType *recordTy) {
  return !recordTy->getDecl()->getDefinitionOrSelf()->isCompleteDefinition();
}

/// Returns whether the given type contains an
/// incomplete class type. This is true if
///
///   * The given type is an incomplete class type.
///   * The given type is a pointer type whose pointee type contains an
///     incomplete class type.
///   * The given type is a member pointer type whose class is an incomplete
///     class type.
///   * The given type is a member pointer type whoise pointee type contains an
///     incomplete class type.
/// is an indirect or direct pointer to an incomplete class type.
static bool containsIncompleteClassType(QualType ty) {
  if (const auto *recordTy = dyn_cast<RecordType>(ty)) {
    if (isIncompleteClassType(recordTy))
      return true;
  }

  if (const auto *pointerTy = dyn_cast<PointerType>(ty))
    return containsIncompleteClassType(pointerTy->getPointeeType());

  if (const auto *memberPointerTy = dyn_cast<MemberPointerType>(ty)) {
    // Check if the class type is incomplete.
    if (!memberPointerTy->getMostRecentCXXRecordDecl()->hasDefinition())
      return true;

    return containsIncompleteClassType(memberPointerTy->getPointeeType());
  }

  return false;
}

const char *vTableClassNameForType(const CIRGenModule &cgm, const Type *ty) {
  // abi::__class_type_info.
  static const char *const classTypeInfo =
      "_ZTVN10__cxxabiv117__class_type_infoE";
  // abi::__si_class_type_info.
  static const char *const siClassTypeInfo =
      "_ZTVN10__cxxabiv120__si_class_type_infoE";
  // abi::__vmi_class_type_info.
  static const char *const vmiClassTypeInfo =
      "_ZTVN10__cxxabiv121__vmi_class_type_infoE";

  switch (ty->getTypeClass()) {
#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_TYPE(Class, Base) case Type::Class:
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#include "clang/AST/TypeNodes.inc"
    llvm_unreachable("Non-canonical and dependent types shouldn't get here");

  case Type::LValueReference:
  case Type::RValueReference:
    llvm_unreachable("References shouldn't get here");

  case Type::Auto:
  case Type::DeducedTemplateSpecialization:
    llvm_unreachable("Undeduced type shouldn't get here");

  case Type::Pipe:
    llvm_unreachable("Pipe types shouldn't get here");

  case Type::ArrayParameter:
    llvm_unreachable("Array Parameter types should not get here.");

  case Type::Builtin:
  case Type::BitInt:
  // GCC treats vector and complex types as fundamental types.
  case Type::Vector:
  case Type::ExtVector:
  case Type::ConstantMatrix:
  case Type::Complex:
  case Type::Atomic:
  // FIXME: GCC treats block pointers as fundamental types?!
  case Type::BlockPointer:
    return "_ZTVN10__cxxabiv123__fundamental_type_infoE";
  case Type::ConstantArray:
  case Type::IncompleteArray:
  case Type::VariableArray:
    // abi::__array_type_info.
    return "_ZTVN10__cxxabiv117__array_type_infoE";

  case Type::FunctionNoProto:
  case Type::FunctionProto:
    // abi::__function_type_info.
    return "_ZTVN10__cxxabiv120__function_type_infoE";
    break;

  case Type::Enum:
    return "_ZTVN10__cxxabiv116__enum_type_infoE";

  case Type::Record: {
    const auto *rd = cast<CXXRecordDecl>(cast<RecordType>(ty)->getDecl())
                         ->getDefinitionOrSelf();

    if (!rd->hasDefinition() || !rd->getNumBases()) {
      return classTypeInfo;
    }

    if (canUseSingleInheritance(rd)) {
      return siClassTypeInfo;
    }

    return vmiClassTypeInfo;
  }

  case Type::ObjCObject:
    ty = cast<ObjCObjectType>(ty)->getBaseType().getTypePtr();
    if (isa<BuiltinType>(ty))
      return classTypeInfo;
    assert(isa<ObjCInterfaceType>(ty));
    [[fallthrough]];

  case Type::ObjCInterface:
    if (cast<ObjCInterfaceType>(ty)->getDecl()->getSuperClass())
      return siClassTypeInfo;
    return classTypeInfo;

  case Type::ObjCObjectPointer:
  case Type::Pointer:
    // abi::__pointer_type_info.
    return "_ZTVN10__cxxabiv119__pointer_type_infoE";

  case Type::MemberPointer:
    // abi::__pointer_to_member_type_info.
    return "_ZTVN10__cxxabiv129__pointer_to_member_type_infoE";

  case Type::HLSLAttributedResource:
  case Type::HLSLInlineSpirv:
    llvm_unreachable("HLSL doesn't support virtual functions");
  }

  return nullptr;
}
} // namespace

/// Return the linkage that the type info and type info name constants
/// should have for the given type.
static cir::GlobalLinkageKind getTypeInfoLinkage(CIRGenModule &cgm,
                                                 QualType ty) {
  //   In addition, it and all of the intermediate abi::__pointer_type_info
  //   structs in the chain down to the abi::__class_type_info for the
  //   incomplete class type must be prevented from resolving to the
  //   corresponding type_info structs for the complete class type, possibly
  //   by making them local static objects. Finally, a dummy class RTTI is
  //   generated for the incomplete type that will not resolve to the final
  //   complete class RTTI (because the latter need not exist), possibly by
  //   making it a local static object.
  if (containsIncompleteClassType(ty))
    return cir::GlobalLinkageKind::InternalLinkage;

  switch (ty->getLinkage()) {
  case Linkage::Invalid:
    llvm_unreachable("Linkage hasn't been computed!");

  case Linkage::None:
  case Linkage::Internal:
  case Linkage::UniqueExternal:
    return cir::GlobalLinkageKind::InternalLinkage;

  case Linkage::VisibleNone:
  case Linkage::Module:
  case Linkage::External:
    // RTTI is not enabled, which means that this type info struct is going
    // to be used for exception handling. Give it linkonce_odr linkage.
    if (!cgm.getLangOpts().RTTI)
      return cir::GlobalLinkageKind::LinkOnceODRLinkage;

    if (const RecordType *record = dyn_cast<RecordType>(ty)) {
      const auto *rd =
          cast<CXXRecordDecl>(record->getDecl())->getDefinitionOrSelf();
      if (rd->hasAttr<WeakAttr>())
        return cir::GlobalLinkageKind::WeakODRLinkage;

      if (cgm.getTriple().isWindowsItaniumEnvironment())
        if (rd->hasAttr<DLLImportAttr>() &&
            shouldUseExternalRttiDescriptor(cgm, ty))
          return cir::GlobalLinkageKind::ExternalLinkage;

      // MinGW always uses LinkOnceODRLinkage for type info.
      if (rd->isDynamicClass() && !cgm.getASTContext()
                                       .getTargetInfo()
                                       .getTriple()
                                       .isWindowsGNUEnvironment())
        return cgm.getVTableLinkage(rd);
    }

    return cir::GlobalLinkageKind::LinkOnceODRLinkage;
  }

  llvm_unreachable("Invalid linkage!");
}

cir::GlobalOp
CIRGenItaniumRTTIBuilder::getAddrOfTypeName(mlir::Location loc, QualType ty,
                                            cir::GlobalLinkageKind linkage) {
  CIRGenBuilderTy &builder = cgm.getBuilder();
  auto rttiAddrSpace = getItaniumRTTIAddressSpace(cgm);
  SmallString<256> name;
  llvm::raw_svector_ostream out(name);
  cgm.getCXXABI().getMangleContext().mangleCXXRTTIName(ty, out);

  // We know that the mangled name of the type starts at index 4 of the
  // mangled name of the typename, so we can just index into it in order to
  // get the mangled name of the type.
  // The name string must be null-terminated for the runtime's strcmp.
  llvm::StringRef typeName = name.substr(4);
  std::string typeNameNullTerminated(typeName);
  typeNameNullTerminated.push_back('\0');
  mlir::Attribute init = builder.getString(
      typeNameNullTerminated, cgm.convertType(cgm.getASTContext().CharTy),
      std::nullopt);

  CharUnits align =
      cgm.getASTContext().getTypeAlignInChars(cgm.getASTContext().CharTy);

  // builder.getString can return a #cir.zero if the string given to it only
  // contains null bytes. However, type names cannot be full of null bytes.
  // So cast Init to a ConstArrayAttr should be safe.
  auto initStr = cast<cir::ConstArrayAttr>(init);

  cir::GlobalOp gv = cgm.createOrReplaceCXXRuntimeVariable(
      loc, name, initStr.getType(), linkage, align, rttiAddrSpace);
  // Type name strings are immutable — mark as constant to match OGCG and
  // allow the linker to merge duplicate constant strings.
  gv.setConstant(true);
  CIRGenModule::setInitializer(gv, init);
  return gv;
}

mlir::Attribute
CIRGenItaniumRTTIBuilder::getAddrOfExternalRTTIDescriptor(mlir::Location loc,
                                                          QualType ty) {
  // Mangle the RTTI name.
  SmallString<256> name;
  llvm::raw_svector_ostream out(name);
  cgm.getCXXABI().getMangleContext().mangleCXXRTTI(ty, out);
  CIRGenBuilderTy &builder = cgm.getBuilder();
  auto rttiAddrSpace = getItaniumRTTIAddressSpace(cgm);
  auto rttiPtrTy = getItaniumRTTIPointerType(builder, rttiAddrSpace);

  // Look for an existing global.
  cir::GlobalOp gv = dyn_cast_or_null<cir::GlobalOp>(
      cgm.getGlobalValue(name));

  if (!gv) {
    // Create a new global variable.
    // From LLVM codegen => Note for the future: If we would ever like to do
    // deferred emission of RTTI, check if emitting vtables opportunistically
    // need any adjustment.
    gv = CIRGenModule::createGlobalOp(cgm, loc, name, rttiPtrTy,
                                      /*isConstant=*/true, rttiAddrSpace);
    const CXXRecordDecl *rd = ty->getAsCXXRecordDecl();
    cgm.setGVProperties(gv, rd);

    // Import the typeinfo symbol when all non-inline virtual methods are
    // imported.
    if (cgm.getTarget().hasPS4DLLImportExport()) {
      cgm.errorNYI("getAddrOfExternalRTTIDescriptor: hasPS4DLLImportExport");
      return {};
    }
  }

  return builder.getGlobalViewAttr(rttiPtrTy, gv);
}

void CIRGenItaniumRTTIBuilder::buildVTablePointer(mlir::Location loc,
                                                  const Type *ty) {
  CIRGenBuilderTy &builder = cgm.getBuilder();
  auto rttiAddrSpace = getItaniumRTTIAddressSpace(cgm);
  auto rttiPtrTy = getItaniumRTTIPointerType(builder, rttiAddrSpace);
  const char *vTableName = vTableClassNameForType(cgm, ty);
  if (!vTableName)
    return; // errorNYI already called inside vTableClassNameForType

  mlir::FlatSymbolRefAttr vTableSym;
  bool isRelativeLayout = cgm.getItaniumVTableContext().isRelativeLayout();
  if (mlir::Operation *existing = cgm.getGlobalValue(vTableName)) {
    if (auto vTable = dyn_cast<cir::GlobalOp>(existing))
      vTableSym = mlir::FlatSymbolRefAttr::get(vTable.getSymNameAttr());
    else if (auto alias = dyn_cast<cir::AliasOp>(existing))
      vTableSym = mlir::FlatSymbolRefAttr::get(alias.getSymNameAttr());
  }

  if (!vTableSym) {
    // OGCG uses getOrInsertGlobal here, so the fallback declaration is always
    // an external zero-length array with no explicit alignment.
    mlir::Type eltTy = isRelativeLayout ? static_cast<mlir::Type>(builder.getUInt8Ty())
                                        : static_cast<mlir::Type>(rttiPtrTy);
    mlir::Type vtableGlobalTy = cir::ArrayType::get(eltTy, 0);
    auto vTable = cgm.createOrReplaceCXXRuntimeVariable(
        loc, vTableName, vtableGlobalTy,
        cir::GlobalLinkageKind::ExternalLinkage, CharUnits::Zero(),
        rttiAddrSpace);
    // getOrInsertGlobal defaults to a mutable declaration.
    vTable.setConstant(false);
    vTableSym = mlir::FlatSymbolRefAttr::get(vTable.getSymNameAttr());
  }

  // The vtable address point is 2.
  mlir::Attribute field{};
  if (isRelativeLayout) {
    // Relative-layout RTTI vtables use a byte-granularity address point:
    // 4 bytes for offset-to-top and 4 bytes for the relative RTTI entry.
    field = cir::GlobalViewAttr::get(rttiPtrTy, vTableSym,
                                     /*indices=*/mlir::ArrayAttr{},
                                     builder.getI64IntegerAttr(8));
  } else {
    SmallVector<mlir::Attribute, 4> offsets{
        cgm.getBuilder().getI32IntegerAttr(2)};
    auto indices = mlir::ArrayAttr::get(builder.getContext(), offsets);
    field = cir::GlobalViewAttr::get(rttiPtrTy, vTableSym,
                                     indices, /*inbounds=*/true);
  }

  assert(field && "expected attribute");
  fields.push_back(field);
}

/// Build an abi::__si_class_type_info, used for single inheritance, according
/// to the Itanium C++ ABI, 2.95p6b.
void CIRGenItaniumRTTIBuilder::buildSIClassTypeInfo(mlir::Location loc,
                                                    const CXXRecordDecl *rd) {
  // Itanium C++ ABI 2.9.5p6b:
  // It adds to abi::__class_type_info a single member pointing to the
  // type_info structure for the base type,
  mlir::Attribute baseTypeInfo =
      CIRGenItaniumRTTIBuilder(cxxABI, cgm)
          .buildTypeInfo(loc, rd->bases_begin()->getType());
  fields.push_back(baseTypeInfo);
}

void CIRGenItaniumRTTIBuilder::buildObjCObjectTypeInfo(
    mlir::Location loc, const ObjCObjectType *objTy) {
  const Type *ty = objTy->getBaseType().getTypePtr();
  assert((isa<BuiltinType>(ty) || isa<ObjCInterfaceType>(ty)) &&
         "expected builtin id/Class or Objective-C interface");

  // The builtin id/Class forms are plain __class_type_info with no payload.
  if (isa<BuiltinType>(ty))
    return;

  ObjCInterfaceDecl *classDecl = cast<ObjCInterfaceType>(ty)->getDecl();
  ObjCInterfaceDecl *superDecl = classDecl->getSuperClass();

  // Root Objective-C interfaces are also plain __class_type_info.
  if (!superDecl)
    return;

  QualType superTy = cgm.getASTContext().getObjCInterfaceType(superDecl);
  fields.push_back(
      CIRGenItaniumRTTIBuilder(cxxABI, cgm).buildTypeInfo(loc, superTy));
}

/// Extract the pbase flags from a pointer-like type.
static unsigned extractPBaseFlags(ASTContext &ctx, QualType &ty) {
  unsigned flags = 0;

  if (ty.isConstQualified())
    flags |= PTI_Const;
  if (ty.isVolatileQualified())
    flags |= PTI_Volatile;
  if (ty.isRestrictQualified())
    flags |= PTI_Restrict;
  ty = ty.getUnqualifiedType();

  if (containsIncompleteClassType(ty))
    flags |= PTI_Incomplete;

  if (auto *proto = ty->getAs<FunctionProtoType>()) {
    if (proto->isNothrow()) {
      flags |= PTI_Noexcept;
      ty = ctx.getFunctionTypeWithExceptionSpec(ty, EST_None);
    }
  }

  return flags;
}

/// Build an abi::__pointer_type_info struct, used for pointer types.
void CIRGenItaniumRTTIBuilder::buildPointerTypeInfo(mlir::Location loc,
                                                     QualType pointeeTy) {
  mlir::Type unsignedIntLTy =
      cgm.convertType(cgm.getASTContext().UnsignedIntTy);

  // Itanium C++ ABI 2.9.5p7:
  //   __flags is a flag word describing the cv-qualification and other
  //   attributes of the type pointed to.
  unsigned flags = extractPBaseFlags(cgm.getASTContext(), pointeeTy);
  fields.push_back(cir::IntAttr::get(unsignedIntLTy, flags));

  // Itanium C++ ABI 2.9.5p7:
  //   __pointee is a pointer to the std::type_info derivation for the
  //   unqualified type being pointed to.
  mlir::Attribute pointeeTypeInfo =
      CIRGenItaniumRTTIBuilder(cxxABI, cgm).buildTypeInfo(loc, pointeeTy);
  fields.push_back(pointeeTypeInfo);
}

/// Build an abi::__pointer_to_member_type_info struct, used for member
/// pointer types.
void CIRGenItaniumRTTIBuilder::buildPointerToMemberTypeInfo(
    mlir::Location loc, const MemberPointerType *ty) {
  QualType pointeeTy = ty->getPointeeType();

  // Itanium C++ ABI 2.9.5p7:
  //   __flags is a flag word describing the cv-qualification and other
  //   attributes of the type pointed to.
  unsigned flags = extractPBaseFlags(cgm.getASTContext(), pointeeTy);

  const auto *rd = ty->getMostRecentCXXRecordDecl();
  if (!rd->hasDefinition())
    flags |= PTI_ContainingClassIncomplete;

  mlir::Type unsignedIntLTy =
      cgm.convertType(cgm.getASTContext().UnsignedIntTy);
  fields.push_back(cir::IntAttr::get(unsignedIntLTy, flags));

  // Itanium C++ ABI 2.9.5p7:
  //   __pointee is a pointer to the std::type_info derivation for the
  //   unqualified type being pointed to.
  mlir::Attribute pointeeTypeInfo =
      CIRGenItaniumRTTIBuilder(cxxABI, cgm).buildTypeInfo(loc, pointeeTy);
  fields.push_back(pointeeTypeInfo);

  // Itanium C++ ABI 2.9.5p9:
  //   __context is a pointer to an abi::__class_type_info corresponding to
  //   the class type containing the member pointed to.
  CanQualType classType =
      cgm.getASTContext().getCanonicalTagType(rd);
  fields.push_back(
      CIRGenItaniumRTTIBuilder(cxxABI, cgm).buildTypeInfo(loc, classType));
}

/// Build an abi::__vmi_class_type_info, used for
/// classes with bases that do not satisfy the abi::__si_class_type_info
/// constraints, according to the Itanium C++ ABI, 2.9.5p5c.
void CIRGenItaniumRTTIBuilder::buildVMIClassTypeInfo(mlir::Location loc,
                                                     const CXXRecordDecl *rd) {
  mlir::Type unsignedIntLTy =
      cgm.convertType(cgm.getASTContext().UnsignedIntTy);

  // Itanium C++ ABI 2.9.5p6c:
  //   __flags is a word with flags describing details about the class
  //   structure, which may be referenced by using the __flags_masks
  //   enumeration. These flags refer to both direct and indirect bases.
  unsigned flags = computeVmiClassTypeInfoFlags(rd);
  fields.push_back(cir::IntAttr::get(unsignedIntLTy, flags));

  // Itanium C++ ABI 2.9.5p6c:
  //   __base_count is a word with the number of direct proper base class
  //   descriptions that follow.
  fields.push_back(cir::IntAttr::get(unsignedIntLTy, rd->getNumBases()));

  if (!rd->getNumBases())
    return;

  // Now add the base class descriptions.

  // Itanium C++ ABI 2.9.5p6c:
  //   __base_info[] is an array of base class descriptions -- one for every
  //   direct proper base. Each description is of the type:
  //
  //   struct abi::__base_class_type_info {
  //   public:
  //     const __class_type_info *__base_type;
  //     long __offset_flags;
  //
  //     enum __offset_flags_masks {
  //       __virtual_mask = 0x1,
  //       __public_mask = 0x2,
  //       __offset_shift = 8
  //     };
  //   };

  // If we're in mingw and 'long' isn't wide enough for a pointer, use 'long
  // long' instead of 'long' for __offset_flags. libstdc++abi uses long long on
  // LLP64 platforms.
  // FIXME: Consider updating libc++abi to match, and extend this logic to all
  // LLP64 platforms.
  QualType offsetFlagsTy = cgm.getASTContext().LongTy;
  const TargetInfo &ti = cgm.getASTContext().getTargetInfo();
  if (ti.getTriple().isOSCygMing() &&
      ti.getPointerWidth(LangAS::Default) > ti.getLongWidth())
    offsetFlagsTy = cgm.getASTContext().LongLongTy;
  mlir::Type offsetFlagsLTy = cgm.convertType(offsetFlagsTy);

  for (const CXXBaseSpecifier &base : rd->bases()) {
    // The __base_type member points to the RTTI for the base type.
    fields.push_back(CIRGenItaniumRTTIBuilder(cxxABI, cgm)
                         .buildTypeInfo(loc, base.getType()));

    CXXRecordDecl *baseDecl = base.getType()->castAsCXXRecordDecl();
    int64_t offsetFlags = 0;

    // All but the lower 8 bits of __offset_flags are a signed offset.
    // For a non-virtual base, this is the offset in the object of the base
    // subobject. For a virtual base, this is the offset in the virtual table of
    // the virtual base offset for the virtual base referenced (negative).
    CharUnits offset;
    if (base.isVirtual())
      offset = cgm.getItaniumVTableContext().getVirtualBaseOffsetOffset(
          rd, baseDecl);
    else {
      const ASTRecordLayout &layout =
          cgm.getASTContext().getASTRecordLayout(rd);
      offset = layout.getBaseClassOffset(baseDecl);
    }
    offsetFlags = uint64_t(offset.getQuantity()) << 8;

    // The low-order byte of __offset_flags contains flags, as given by the
    // masks from the enumeration __offset_flags_masks.
    if (base.isVirtual())
      offsetFlags |= BCTI_Virtual;
    if (base.getAccessSpecifier() == AS_public)
      offsetFlags |= BCTI_Public;

    fields.push_back(cir::IntAttr::get(offsetFlagsLTy, offsetFlags));
  }
}

mlir::Attribute CIRGenItaniumRTTIBuilder::buildTypeInfo(mlir::Location loc,
                                                        QualType ty) {
  CIRGenBuilderTy &builder = cgm.getBuilder();
  auto rttiAddrSpace = getItaniumRTTIAddressSpace(cgm);
  auto rttiPtrTy = getItaniumRTTIPointerType(builder, rttiAddrSpace);

  // We want to operate on the canonical type.
  ty = ty.getCanonicalType();

  // RTTI is always for the unqualified type. Strip cv-qualifiers so that
  // mangleCXXRTTI doesn't assert on qualified types (e.g., from
  // dynamic_cast<const Derived*>).
  ty = ty.getUnqualifiedType();

  // Check if we've already emitted an RTTI descriptor for this type.
  SmallString<256> name;
  llvm::raw_svector_ostream out(name);
  cgm.getCXXABI().getMangleContext().mangleCXXRTTI(ty, out);

  auto oldGV = dyn_cast_or_null<cir::GlobalOp>(
      cgm.getGlobalValue(name));

  if (oldGV && !oldGV.isDeclaration()) {
    assert(!oldGV.hasAvailableExternallyLinkage() &&
           "available_externally typeinfos not yet implemented");
    return builder.getGlobalViewAttr(rttiPtrTy, oldGV);
  }

  // Check if there is already an external RTTI descriptor for this type.
  if (isStandardLibraryRttiDescriptor(ty) ||
      shouldUseExternalRttiDescriptor(cgm, ty))
    return getAddrOfExternalRTTIDescriptor(loc, ty);

  // Emit the standard library with external linkage.
  cir::GlobalLinkageKind linkage = getTypeInfoLinkage(cgm, ty);

  // Give the type_info object and name the formal visibility of the
  // type itself.

  mlir::SymbolTable::Visibility symVisibility;
  if (cir::isLocalLinkage(linkage))
    // If the linkage is local, only default visibility makes sense.
    symVisibility = mlir::SymbolTable::Visibility::Public;
  else if (cxxABI.classifyRTTIUniqueness(ty, linkage) ==
           CIRGenItaniumCXXABI::RUK_NonUniqueHidden) {
    symVisibility = mlir::SymbolTable::Visibility::Private;
  } else
    symVisibility = CIRGenModule::getMLIRVisibility(ty->getVisibility());

  return buildTypeInfo(loc, ty, linkage, symVisibility);
}

mlir::Attribute CIRGenItaniumRTTIBuilder::buildTypeInfo(
    mlir::Location loc, QualType ty, cir::GlobalLinkageKind linkage,
    mlir::SymbolTable::Visibility visibility) {
  CIRGenBuilderTy &builder = cgm.getBuilder();
  auto rttiAddrSpace = getItaniumRTTIAddressSpace(cgm);
  auto rttiPtrTy = getItaniumRTTIPointerType(builder, rttiAddrSpace);

  // OGCG sets DLL storage class on type info here. Windows-only, skipped.

  // Add the vtable pointer.
  buildVTablePointer(loc, cast<Type>(ty));

  // And the name.
  cir::GlobalOp typeName = getAddrOfTypeName(loc, ty, linkage);
  mlir::Attribute typeNameField;

  // If we're supposed to demote the visibility, be sure to set a flag
  // to use a string comparison for type_info comparisons.
  CIRGenItaniumCXXABI::RTTIUniquenessKind rttiUniqueness =
      cxxABI.classifyRTTIUniqueness(ty, linkage);
  typeNameField = builder.getGlobalViewAttr(rttiPtrTy, typeName);
  if (rttiUniqueness != CIRGenItaniumCXXABI::RUK_Unique) {
    // The flag is the sign bit, which on ARM64 is defined to be clear
    // for global pointers. This is very ARM64-specific.
    llvm::APInt signBit(/*numBits=*/64, 1);
    signBit <<= 63;
    auto signBitAttr = mlir::IntegerAttr::get(
        mlir::IntegerType::get(builder.getContext(), 64), signBit);
    typeNameField = cir::GlobalViewAttr::get(
        rttiPtrTy,
        mlir::FlatSymbolRefAttr::get(typeName.getSymNameAttr()),
        /*indices=*/{}, /*rawByteOffset=*/{}, /*inRange=*/{},
        /*inbounds=*/false,
        signBitAttr);
  }

  fields.push_back(typeNameField);

  switch (ty->getTypeClass()) {
#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_TYPE(Class, Base) case Type::Class:
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#include "clang/AST/TypeNodes.inc"
    llvm_unreachable("Non-canonical and dependent types shouldn't get here");

  // GCC treats vector types as fundamental types.
  case Type::Builtin:
  case Type::Vector:
  case Type::ExtVector:
  case Type::ConstantMatrix:
  case Type::Complex:
  case Type::BlockPointer:
    // Itanium C++ ABI 2.9.5p4:
    // abi::__fundamental_type_info adds no data members to std::type_info.
    break;

  case Type::LValueReference:
  case Type::RValueReference:
    llvm_unreachable("References shouldn't get here");

  case Type::Auto:
  case Type::DeducedTemplateSpecialization:
    llvm_unreachable("Undeduced type shouldn't get here");

  case Type::Pipe:
    break;

  case Type::BitInt:
    break;

  case Type::ConstantArray:
  case Type::IncompleteArray:
  case Type::VariableArray:
  case Type::ArrayParameter:
    // Itanium C++ ABI 2.9.5p5:
    // abi::__array_type_info adds no data members to std::type_info.
    break;

  case Type::FunctionNoProto:
  case Type::FunctionProto:
    // Itanium C++ ABI 2.9.5p5:
    // abi::__function_type_info adds no data members to std::type_info.
    break;

  case Type::Enum:
    // Itanium C++ ABI 2.9.5p5:
    // abi::__enum_type_info adds no data members to std::type_info.
    break;

  case Type::Record: {
    const auto *rd = cast<CXXRecordDecl>(cast<RecordType>(ty)->getDecl())
                         ->getDefinitionOrSelf();
    if (!rd->hasDefinition() || !rd->getNumBases()) {
      // We don't need to emit any fields.
      break;
    }

    if (canUseSingleInheritance(rd)) {
      buildSIClassTypeInfo(loc, rd);
    } else {
      buildVMIClassTypeInfo(loc, rd);
    }

    break;
  }

  case Type::ObjCObject:
  case Type::ObjCInterface:
    buildObjCObjectTypeInfo(loc, cast<ObjCObjectType>(ty));
    break;

  case Type::ObjCObjectPointer:
    buildPointerTypeInfo(loc, cast<ObjCObjectPointerType>(ty)->getPointeeType());
    break;

  case Type::Pointer:
    buildPointerTypeInfo(loc, cast<PointerType>(ty)->getPointeeType());
    break;

  case Type::MemberPointer:
    buildPointerToMemberTypeInfo(loc, cast<MemberPointerType>(ty));
    break;

  case Type::Atomic:
    // No fields, at least for the moment.
    break;

  case Type::HLSLAttributedResource:
  case Type::HLSLInlineSpirv:
    llvm_unreachable("HLSL doesn't support RTTI");
  }

  // OGCG sets DLL storage class for RTTI globals. Windows-only.
  cir::TypeInfoAttr init = builder.getTypeInfo(builder.getArrayAttr(fields));

  SmallString<256> name;
  llvm::raw_svector_ostream out(name);
  cgm.getCXXABI().getMangleContext().mangleCXXRTTI(ty, out);

  // Create new global and search for an existing global.
  auto oldGV = dyn_cast_or_null<cir::GlobalOp>(
      cgm.getGlobalValue(name));

  cir::GlobalOp gv =
      CIRGenModule::createGlobalOp(cgm, loc, name, init.getType(),
                                   /*isConstant=*/true, rttiAddrSpace);

  if (const auto &schema =
          cgm.getCodeGenOpts().PointerAuth.CXXTypeInfoVTablePointer) {
    auto authMode = schema.getAuthenticationMode();
    if (authMode == PointerAuthenticationMode::SignAndStrip ||
        authMode == PointerAuthenticationMode::SignAndAuth) {
      uint64_t otherDiscriminator = 0;
      switch (schema.getOtherDiscrimination()) {
      case PointerAuthSchema::Discrimination::None:
        break;
      case PointerAuthSchema::Discrimination::Constant:
        otherDiscriminator = schema.getConstantDiscrimination();
        break;
      case PointerAuthSchema::Discrimination::Type:
        otherDiscriminator =
            cgm.getASTContext().getPointerAuthTypeDiscriminator(ty);
        break;
      case PointerAuthSchema::Discrimination::Decl:
        llvm_unreachable(
            "RTTI typeinfo vtable pointer schema must not use decl "
            "discrimination");
      }

      mlir::Type i64Ty = mlir::IntegerType::get(builder.getContext(), 64);
      gv->setAttr("cir.rtti_vtable_ptrauth",
                  builder.getArrayAttr({
                      builder.getI32IntegerAttr(schema.getKey()),
                      mlir::IntegerAttr::get(i64Ty, otherDiscriminator),
                      builder.getBoolAttr(schema.isAddressDiscriminated()),
                  }));
    }
  }

  // Export the typeinfo in the same circumstances as the vtable is
  // exported.
  if (cgm.getTarget().hasPS4DLLImportExport()) {
    cgm.errorNYI("buildTypeInfo: target hasPS4DLLImportExport");
    return {};
  }

  // If there's already an old global variable, replace it with the new one.
  if (oldGV) {
    // Replace occurrences of the old variable if needed.
    gv.setName(oldGV.getName());

    // The old global may have been declared with a different type (e.g.,
    // !cir.ptr<!u8i> from getAddrOfExternalRTTIDescriptor) while the new
    // definition uses the full TypeInfoAttr record type. Update all users
    // of the old global to reference the new type.
    if (oldGV.getSymType() != gv.getSymType()) {
      auto newGlobalPtrTy =
          cir::PointerType::get(gv.getSymType(), gv.getAddrSpaceAttr());
      // Update all GetGlobalOp users of the old symbol to the new pointer type.
      // Use the symbol-user index (completed + in-flight function users + top-
      // level initializer users) rather than walking every op in the module per
      // descriptor, which is O(typeinfos x module-size) on polymorphic/generated
      // TUs (the same quadratic shape fixed for relative-vtable aliases).
      cgm.retypeGetGlobalUsers(oldGV.getSymNameAttr(), newGlobalPtrTy);
    }
    oldGV->erase();
  }

  // Set the linkage before checking comdat — the global needs linkage to
  // determine if it's weak-for-linker. OGCG passes linkage to the
  // GlobalVariable constructor; CIR's createGlobalOp doesn't take linkage,
  // so we set it explicitly here.
  gv.setLinkage(linkage);
  mlir::SymbolTable::setSymbolVisibility(gv, CIRGenModule::getMLIRVisibility(gv));
  gv.setGlobalVisibility(CIRGenModule::getCIRVisibilityKind(
      visibility == mlir::SymbolTable::Visibility::Private &&
              !cir::isLocalLinkage(linkage)
          ? HiddenVisibility
          : ty->getVisibility()));

  if (cgm.supportsCOMDAT() && cir::isWeakForLinker(gv.getLinkage()))
    gv.setComdat(true);

  CharUnits align = cgm.getASTContext().toCharUnitsFromBits(
      cgm.getTarget().getPointerAlign(LangAS::Default));
  gv.setAlignmentAttr(cgm.getSize(align));

  // The Itanium ABI specifies that type_info objects must be globally
  // unique, with one exception: if the type is an incomplete class
  // type or a (possibly indirect) pointer to one.  That exception
  // affects the general case of comparing type_info objects produced
  // by the typeid operator, which is why the comparison operators on
  // std::type_info generally use the type_info name pointers instead
  // of the object addresses.  However, the language's built-in uses
  // of RTTI generally require class types to be complete, even when
  // manipulating pointers to those class types.  This allows the
  // implementation of dynamic_cast to rely on address equality tests,
  // which is much faster.

  // All of this is to say that it's important that both the type_info
  // object and the type_info name be uniqued when weakly emitted.

  mlir::SymbolTable::setSymbolVisibility(
      typeName, CIRGenModule::getMLIRVisibility(typeName));
  typeName.setGlobalVisibility(CIRGenModule::getCIRVisibilityKind(
      visibility == mlir::SymbolTable::Visibility::Private &&
              !cir::isLocalLinkage(linkage)
          ? HiddenVisibility
          : ty->getVisibility()));
  // OGCG sets DLL storage class on typeName and GV here. Windows-only, skipped.
  // LLVM global `partition` is not represented in CIR global emission.
  cgm.setDSOLocal(static_cast<mlir::Operation *>(typeName.getOperation()));

  CIRGenModule::setInitializer(gv, init);

  // setDSOLocal must be called AFTER setInitializer because
  // shouldAssumeDSOLocal checks isDeclarationForLinker(), which returns true
  // for globals without an initial value. Without the initializer set,
  // the global looks like a declaration and doesn't get dso_local.
  cgm.setDSOLocal(static_cast<mlir::Operation *>(gv.getOperation()));
  return builder.getGlobalViewAttr(rttiPtrTy, gv);
}

mlir::Attribute CIRGenItaniumCXXABI::getAddrOfRTTIDescriptor(mlir::Location loc,
                                                             QualType ty) {
  return CIRGenItaniumRTTIBuilder(*this, cgm).buildTypeInfo(loc, ty);
}

/// What sort of uniqueness rules should we use for the RTTI for the
/// given type?
CIRGenItaniumCXXABI::RTTIUniquenessKind
CIRGenItaniumCXXABI::classifyRTTIUniqueness(
    QualType canTy, cir::GlobalLinkageKind linkage) const {
  if (shouldRTTIBeUnique())
    return RUK_Unique;

  // It's only necessary for linkonce_odr or weak_odr linkage.
  if (linkage != cir::GlobalLinkageKind::LinkOnceODRLinkage &&
      linkage != cir::GlobalLinkageKind::WeakODRLinkage)
    return RUK_Unique;

  // It's only necessary with default visibility.
  if (canTy->getVisibility() != DefaultVisibility)
    return RUK_Unique;

  // If we're not required to publish this symbol, hide it.
  if (linkage == cir::GlobalLinkageKind::LinkOnceODRLinkage)
    return RUK_NonUniqueHidden;

  // If we're required to publish this symbol, as we might be under an
  // explicit instantiation, leave it with default visibility but
  // enable string-comparisons.
  assert(linkage == cir::GlobalLinkageKind::WeakODRLinkage);
  return RUK_NonUniqueVisible;
}

void CIRGenItaniumCXXABI::emitFundamentalRTTIDescriptors(
    const CXXRecordDecl *rd) {
  // Types added here must also be added to typeInfoIsInStandardLibrary.
  ASTContext &ctx = cgm.getASTContext();
  // OGCG sets DLL storage class for fundamental type infos. Windows-only.
  mlir::SymbolTable::Visibility visibility =
      CIRGenModule::getMLIRVisibility(rd->getVisibility());
  mlir::Location loc = cgm.getLoc(rd->getSourceRange());
  auto emitFundamentalTypeInfo = [&](QualType type) {
    CIRGenItaniumRTTIBuilder(*this, cgm)
        .buildTypeInfo(loc, type, cir::GlobalLinkageKind::ExternalLinkage,
                       visibility);
  };
  auto emitFundamentalTypeTriplet = [&](QualType fundamentalType) {
    emitFundamentalTypeInfo(fundamentalType);
    emitFundamentalTypeInfo(ctx.getPointerType(fundamentalType));
    emitFundamentalTypeInfo(ctx.getPointerType(fundamentalType.withConst()));
  };
  // Keep the emission order spelled out. The self-hosted clangir compiler
  // currently miscompiles the local QualType-array form of this routine.
  emitFundamentalTypeTriplet(ctx.VoidTy);
  emitFundamentalTypeTriplet(ctx.NullPtrTy);
  emitFundamentalTypeTriplet(ctx.BoolTy);
  emitFundamentalTypeTriplet(ctx.WCharTy);
  emitFundamentalTypeTriplet(ctx.CharTy);
  emitFundamentalTypeTriplet(ctx.UnsignedCharTy);
  emitFundamentalTypeTriplet(ctx.SignedCharTy);
  emitFundamentalTypeTriplet(ctx.ShortTy);
  emitFundamentalTypeTriplet(ctx.UnsignedShortTy);
  emitFundamentalTypeTriplet(ctx.IntTy);
  emitFundamentalTypeTriplet(ctx.UnsignedIntTy);
  emitFundamentalTypeTriplet(ctx.LongTy);
  emitFundamentalTypeTriplet(ctx.UnsignedLongTy);
  emitFundamentalTypeTriplet(ctx.LongLongTy);
  emitFundamentalTypeTriplet(ctx.UnsignedLongLongTy);
  emitFundamentalTypeTriplet(ctx.Int128Ty);
  emitFundamentalTypeTriplet(ctx.UnsignedInt128Ty);
  emitFundamentalTypeTriplet(ctx.HalfTy);
  emitFundamentalTypeTriplet(ctx.FloatTy);
  emitFundamentalTypeTriplet(ctx.DoubleTy);
  emitFundamentalTypeTriplet(ctx.LongDoubleTy);
  emitFundamentalTypeTriplet(ctx.Float128Ty);
  emitFundamentalTypeTriplet(ctx.Char8Ty);
  emitFundamentalTypeTriplet(ctx.Char16Ty);
  emitFundamentalTypeTriplet(ctx.Char32Ty);
}

void CIRGenItaniumCXXABI::emitDestructorCall(
    CIRGenFunction &cgf, const CXXDestructorDecl *dd, CXXDtorType type,
    bool forVirtualBase, bool delegating, Address thisAddr, QualType thisTy) {
  GlobalDecl gd(dd, type);
  mlir::Value vtt =
      getCXXDestructorImplicitParam(cgf, dd, type, forVirtualBase, delegating);
  ASTContext &astContext = cgm.getASTContext();
  LangAS as = cgm.getGlobalVarAddressSpace(nullptr);
  QualType vttComponentTy =
      astContext.getAddrSpaceQualType(astContext.VoidPtrTy, as);
  QualType vttTy = astContext.getPointerType(vttComponentTy);
  CIRGenCallee callee;
  if (astContext.getLangOpts().AppleKext && type != Dtor_Base &&
      dd->isVirtual())
    callee =
        cgf.buildAppleKextVirtualDestructorCall(dd, type, dd->getParent());
  else
    callee = CIRGenCallee::forDirect(cgm.getAddrOfCXXStructor(gd), gd);

  cgf.emitCXXDestructorCall(gd, callee, thisAddr.getPointer(), thisTy, vtt,
                            vttTy, nullptr);
}

void CIRGenItaniumCXXABI::registerGlobalDtor(const VarDecl *vd,
                                             cir::FuncOp dtor,
                                             mlir::Value addr) {
  if (vd->isNoDestroy(cgm.getASTContext()))
    return;

  if (vd->getTLSKind()) {
    // TLS destructor registration is handled in LoweringPrepare using
    // __cxa_thread_atexit. The dtor call was already emitted into the
    // GlobalOp's dtorRegion by emitDeclDestroy.
    return;
  }

  // HLSL doesn't support atexit.
  if (cgm.getLangOpts().HLSL) {
    cgm.errorNYI(vd->getSourceRange(), "registerGlobalDtor: HLSL");
    return;
  }

  // OpenMP offloading supports C++ ctors/dtors on targets where hasAtExit()
  // may be false. Match OGCG: for non-static locals, fall back to
  // llvm.global_dtors instead of __cxa_atexit registration.
  if (!cgm.getLangOpts().hasAtExit() && !vd->isStaticLocal()) {
    cgm.addGlobalDtor(dtor);
    return;
  }
  // OGCG: if (CGM.getLangOpts().AppleKext) return CGM.AddCXXDtorEntry(...)
  // Apple kernel extensions (-fapple-kext) use a different destructor
  // registration mechanism. Apple is not a supported CIR target.

  // The default behavior is to use atexit. This is handled in lowering
  // prepare. Nothing to be done for CIR here.
}

namespace {

/// Set nounwind on a runtime function declaration, matching OGCG's pattern
/// of passing NoUnwind via ExtraAttrs to CreateRuntimeFunction
/// (ItaniumCXXABI.cpp:2656-2684) or calling fn->setDoesNotThrow().
static void setRuntimeFnNothrow(cir::FuncOp fn) {
  mlir::NamedAttrList attrs;
  if (auto existingAttrs = fn.getExtraAttrs())
    attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
  attrs.set("nothrow", mlir::UnitAttr::get(fn->getContext()));
  fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
      fn->getContext(),
      mlir::DictionaryAttr::get(fn->getContext(), attrs)));
}

/// Cleanup to call __cxa_guard_abort on the exceptional path when a guarded
/// static local variable's initializer throws. Matches OGCG's CallGuardAbort.
struct CallGuardAbort final : EHScopeStack::Cleanup {
  cir::GlobalOp guard;
  cir::IntType guardTy;
  CallGuardAbort(cir::GlobalOp guard, cir::IntType guardTy)
      : guard(guard), guardTy(guardTy) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    CIRGenBuilderTy &builder = cgf.getBuilder();
    mlir::Location loc = *cgf.currSrcLoc;
    cir::PointerType guardPtrTy = builder.getPointerTo(guardTy);
    cir::FuncType abortFnTy =
        builder.getFuncType({guardPtrTy}, builder.getVoidTy());
    cir::FuncOp abortFn =
        cgf.cgm.createRuntimeFunction(abortFnTy, "__cxa_guard_abort");
    setRuntimeFnNothrow(abortFn);
    mlir::Value guardPtr = builder.createGetGlobal(guard);
    cgf.emitNounwindRuntimeCall(loc, abortFn, {guardPtr});
  }
};
} // namespace

void CIRGenItaniumCXXABI::emitGuardedInit(CIRGenFunction &cgf,
                                          const VarDecl &d, cir::GlobalOp var,
                                          bool shouldPerformInit) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(d.getSourceRange());

  // Determine whether we need thread-safe statics for this variable.
  // Thread-safe statics are needed for local non-TLS variables and inline
  // non-template variables when the language option is enabled.
  bool nonTemplateInline =
      d.isInline() &&
      !isTemplateInstantiation(d.getTemplateSpecializationKind());
  bool threadsafe = cgm.getASTContext().getLangOpts().ThreadsafeStatics &&
                    (d.isLocalVarDecl() || nonTemplateInline) &&
                    !d.getTLSKind();

  // Guard variable type selection (matching OGCG):
  //   - i8 when thread-safe statics are not needed AND the variable has
  //     internal linkage. This covers TLS local statics (which are inherently
  //     thread-safe per-thread) and non-threadsafe internal statics.
  //   - i64 (generic Itanium ABI) otherwise, for __cxa_guard_acquire.
  bool useInt8GuardVariable = !threadsafe && var.hasInternalLinkage();
  // OGCG uses SizeTy (32-bit on AArch32) when UseARMGuardVarABI is true.
  mlir::Type sizeTy = cgm.sizeTy;
  auto sizeIntTy = cast<cir::IntType>(sizeTy);
  cir::IntType guardTy =
      useInt8GuardVariable ? builder.getUInt8Ty() : sizeIntTy;
  CharUnits guardAlignment =
      useInt8GuardVariable
          ? CharUnits::One()
          : CharUnits::fromQuantity(
                cgm.getDataLayout().getABITypeAlign(guardTy).value());

  // Mangle the guard variable name (_ZGVZ...).
  SmallString<256> guardName;
  {
    llvm::raw_svector_ostream out(guardName);
    getMangleContext().mangleStaticGuardVariable(&d, out);
  }

  // Create the guard GlobalOp if it doesn't already exist (handles
  // double-emission of function bodies).
  cir::GlobalOp guard;
  if (mlir::Operation *existingOp = cgm.getGlobalValue(guardName)) {
    guard = mlir::cast<cir::GlobalOp>(existingOp);
  } else {
    // The guard variable must live in the same address space as the guarded
    // variable (e.g. a global in a non-default address space); otherwise the
    // pointer to the guard disagrees with its definition's address space.
    guard = CIRGenModule::createGlobalOp(cgm, loc, guardName.str(), guardTy,
                                         /*isConstant=*/false,
                                         var.getAddrSpaceAttr());
    guard.setInitialValueAttr(cir::IntAttr::get(guardTy, 0));
    // Absorb linkage from the guarded variable (matches OGCG).
    // For inline statics with weak_odr linkage, the guard must also be
    // weak_odr so that all TUs share the same guard variable.
    guard.setLinkage(var.getLinkage());
    // Absorb visibility from the guarded variable (not derived from
    // guard's own linkage), matching OGCG behavior.
    cgm.setDSOLocal(static_cast<mlir::Operation *>(guard));
    // OGCG sets DLL storage class on the guard variable. Windows-only, skipped.
    mlir::SymbolTable::setSymbolVisibility(
        guard, mlir::SymbolTable::getSymbolVisibility(var));
    guard.setGlobalVisibility(var.getGlobalVisibility());
    guard.setAlignment(guardAlignment.getAsAlign().value());
    // If the variable is thread-local, so is its guard variable.
    if (auto tlsModel = var.getTlsModelAttr())
      guard.setTlsModelAttr(tlsModel);
    // Add COMDAT for weak guard variables on targets that support it.
    // On ELF/Wasm, the guard shares the data variable's COMDAT group
    // (Itanium ABI 3.3.2 recommendation). On other platforms, it gets
    // its own COMDAT group.
    if (cgm.supportsCOMDAT() && cir::isWeakForLinker(guard.getLinkage())) {
      guard.setComdat(true);
      if (!d.isLocalVarDecl() && var.getComdat() &&
          (cgm.getTriple().isOSBinFormatELF() ||
           cgm.getTriple().isOSBinFormatWasm())) {
        // Share the data variable's COMDAT group.
        guard->setAttr("comdat_name",
                       cgm.getStringAttrCached(var.getSymName()));
      }
    }
  }

  // Get pointer to guard variable.
  bool guardIsTLS = guard.getTlsModelAttr() != nullptr;
  mlir::Value guardPtr = builder.createGetGlobal(guard, guardIsTLS);

  // OGCG skips the inline fast-path when MaxAtomicInlineWidth == 0 (no inline
  // atomics). CIR's supported targets (AArch64, x86_64) always have
  // MaxAtomicInlineWidth >= 64, so the inline fast-path is always taken.

  // Load the first byte of the guard variable for the fast inline check.
  // Itanium C++ ABI 3.3.2:
  //   if (obj_guard.first_byte == 0) {
  //     if (__cxa_guard_acquire(&obj_guard)) {
  //       ... initialize ...;
  //       __cxa_guard_release(&obj_guard);
  //     }
  //   }
  cir::IntType i8Ty = builder.getUInt8Ty();
  // When the guard is already i8, no bitcast is needed. For i64 guards,
  // we bitcast to i8* to load just the first byte (fast-path check).
  mlir::Value guardBytePtr =
      useInt8GuardVariable ? guardPtr
                           : builder.createPtrBitcast(guardPtr, i8Ty);
  Address guardByteAddr(guardBytePtr, i8Ty, guardAlignment);

  // Use atomic acquire ordering for the fast-path guard byte load when
  // thread-safe statics are enabled. This ensures that references to the
  // initialized object don't occur before the load of the initialization
  // flag (Itanium C++ ABI 3.3.2). Matches OGCG's LI->setAtomic(Acquire).
  cir::MemOrderAttr memOrder;
  if (threadsafe)
    memOrder = cir::MemOrderAttr::get(&cgf.getMLIRContext(),
                                      cir::MemOrder::Acquire);
  mlir::IntegerAttr alignAttr =
      builder.getAlignmentAttr(guardByteAddr.getAlignment());
  mlir::Value firstByte = cir::LoadOp::create(
      builder, loc, guardByteAddr.getPointer(), /*isDeref=*/false,
      /*is_volatile=*/false, /*is_nontemporal=*/false,
      /*alignment=*/alignAttr,
      /*sync_scope=*/cir::SyncScopeKindAttr{}, memOrder, /*tbaa=*/{});
  mlir::Value zero8 = builder.getConstantInt(loc, i8Ty, 0);
  // OGCG masks the guard byte with & 1 when UseARMGuardVarABI is set
  // (testing only bit 0). Do the same for ARM guard-var ABI.
  if (useARMGuardVarABI) {
    mlir::Value one8 = builder.getConstantInt(loc, i8Ty, 1);
    firstByte = cir::BinOp::create(
        builder, loc, i8Ty, cir::BinOpKind::And, firstByte, one8,
        /*no_unsigned_wrap=*/false,
        /*no_signed_wrap=*/false, /*saturated=*/false,
        cir::FastMathFlags::none, /*fpExceptionBehavior=*/{},
        /*roundingMode=*/{});
  }
  mlir::Value isUninitialized =
      builder.createCompare(loc, cir::CmpOpKind::eq, firstByte, zero8);

  // Outer if: fast check — is the guard byte zero (not yet initialized)?
  // OGCG sets branch weights {1, 1048575} to indicate the initialization
  // path is cold (entered ~once per million invocations for local statics).
  auto guardIf = cir::IfOp::create(
      builder, loc, isUninitialized, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &, mlir::Location) {
        // Helper lambda: emit initialization + destructor registration.
        // Shared by both threadsafe and non-threadsafe paths.
        auto emitInitAndDtor = [&]() {
          mlir::Value varAddr = cgm.getAddrOfGlobalVar(&d);
          CharUnits varAlign = cgm.getASTContext().getDeclAlign(&d);
          mlir::Type varElemTy = cgf.convertTypeForMem(d.getType());
          auto varPtrTy = mlir::cast<cir::PointerType>(varAddr.getType());
          auto expectedVarPtrTy =
              cir::PointerType::get(varElemTy, varPtrTy.getAddrSpace());
          if (varPtrTy != expectedVarPtrTy)
            varAddr = builder.createBitcast(varAddr, expectedVarPtrTy);
          Address declAddr(varAddr, varElemTy, varAlign);
          cgf.emitOMPThreadPrivateVarDefinition(&d, declAddr,
                                                shouldPerformInit);

          if (shouldPerformInit) {
            CIRGenFunction::ApplySourceAtomGroup initAtom(cgf);
            CIRGenFunction::SourceLocRAIIObject initLoc{cgf, loc};
            // Emit the initializer inline into the function body.
            QualType type = d.getType();
            LValue lv = cgf.makeAddrLValue(declAddr, type);
            const Expr *init = d.getInit();
            // Reference types need special handling.
            if (type->isReferenceType()) {
              RValue rv = cgf.emitReferenceBindingToExpr(init);
              cgf.emitStoreThroughLValue(rv, lv);
            } else {
              switch (CIRGenFunction::getEvaluationKind(type)) {
              case cir::TEK_Scalar:
                cgf.emitScalarInit(init, loc, lv, false);
                break;
              case cir::TEK_Complex:
                cgf.emitComplexExprIntoLValue(init, lv,
                                              /*isInit=*/true);
                break;
              case cir::TEK_Aggregate:
                cgf.emitAggExpr(
                    init,
                    AggValueSlot::forLValue(lv, AggValueSlot::IsDestructed,
                                            AggValueSlot::IsNotAliased,
                                            AggValueSlot::DoesNotOverlap));
                break;
              }
            }
          }

          // Check if this variable has constant storage. If so, emit
          // invariant.start instead of registering a destructor, matching
          // OGCG's EmitCXXGlobalVarDeclInit (CGDeclCXX.cpp:219-224).
          // Reference globals are excluded: OGCG does not emit
          // llvm.invariant.start for reference-typed static locals.
          bool needsDtor =
              d.needsDestruction(cgf.getContext()) ==
              QualType::DK_cxx_destructor;
          if (!d.getType()->isReferenceType() &&
              d.getType().isConstantStorage(cgf.getContext(), true,
                                            !needsDtor)) {
            mlir::Value varAddr = cgm.getAddrOfGlobalVar(&d);
            cgf.emitInvariantStart(
                cgf.getContext().getTypeSizeInChars(d.getType()), varAddr,
                cgf.getLoc(d.getSourceRange()));
          } else {

          // Register destructor via __cxa_atexit or
          // __cxa_thread_atexit depending on TLS kind.
          {
            QualType type = d.getType();
            if (d.needsDestruction(cgf.getContext()) ==
                    QualType::DK_cxx_destructor &&
                !d.isNoDestroy(cgm.getASTContext())) {
              const CXXRecordDecl *record = type->getAsCXXRecordDecl();
              bool isTLS = d.getTLSKind() != VarDecl::TLS_None;
              if (record && !record->hasTrivialDestructor()) {
                CXXDestructorDecl *dtor = record->getDestructor();
                cir::FuncOp dtorFn =
                    cgm.getAddrAndTypeOfCXXStructor(
                           GlobalDecl(dtor, Dtor_Complete))
                        .second;
                mlir::Value varAddr = cgm.getAddrOfGlobalVar(&d);

                // Use __cxa_thread_atexit for thread-local
                // variables so they are destroyed on the
                // same LIFO stack as global TLS destructors.
                cgf.emitCXXAtExit(loc, dtorFn, varAddr, isTLS);
              } else {
                // For array types (and other non-record cases), use
                // generateDestroyHelper to create a standalone helper
                // function with a fresh CIRGenFunction (and therefore
                // a clean EH stack). This matches OGCG's approach in
                // EmitCXXGlobalVarDeclInit (CGDeclCXX.cpp:141-144).
                //
                // We cannot emit the destroy code directly into the
                // GlobalOp's dtor region using the enclosing function's
                // cgf because any throwing call would produce a landing
                // pad that walks cgf's EH stack and emits cleanups for
                // the enclosing function's local variables — creating
                // cross-function SSA references that violate MLIR
                // dominance rules.
                mlir::Value varAddr = cgm.getAddrOfGlobalVar(&d);
                mlir::Type expectedElTy = cgf.convertTypeForMem(type);
                auto addrPtrTy =
                    mlir::cast<cir::PointerType>(varAddr.getType());
                if (addrPtrTy.getPointee() != expectedElTy)
                  varAddr = builder.createBitcast(
                      varAddr, cir::PointerType::get(expectedElTy));
                CharUnits alignment = cgf.getContext().getDeclAlign(&d);
                Address addr{varAddr, expectedElTy, alignment};
                cir::FuncOp dtorFn = cgf.generateDestroyHelper(
                    addr, type, CIRGenFunction::destroyCXXObject,
                    cgf.getLangOpts().Exceptions, &d);
                if (dtorFn)
                  cgf.emitCXXAtExit(loc, dtorFn, varAddr, isTLS);
              }
            }
          }
          } // end else (non-constant-storage — register destructor)
        };

        // Helper lambda: store 1 to the guard byte.
        auto storeGuardByte = [&]() {
          mlir::Value one8 = builder.getConstantInt(loc, i8Ty, 1);
          builder.createStore(loc, one8, guardByteAddr);
        };

        if (threadsafe) {
          // Thread-safe path: use __cxa_guard_acquire / release / abort.
          // int __cxa_guard_acquire(__guard *);
          cir::IntType i32Ty = builder.getSInt32Ty();
          cir::PointerType guardPtrTy = builder.getPointerTo(guardTy);
          cir::FuncType acquireFnTy =
              builder.getFuncType({guardPtrTy}, i32Ty);
          cir::FuncOp acquireFn =
              cgm.createRuntimeFunction(acquireFnTy, "__cxa_guard_acquire");
          setRuntimeFnNothrow(acquireFn);
          mlir::Value acquireResult =
              cgf.emitNounwindRuntimeCall(loc, acquireFn, {guardPtr});

          // Inner if: we won the initialization race.
          cir::IntType i32Ty2 = builder.getSInt32Ty();
          mlir::Value zero32 = builder.getConstantInt(loc, i32Ty2, 0);
          mlir::Value isAcquired = builder.createCompare(
              loc, cir::CmpOpKind::ne, acquireResult, zero32);

          cir::IfOp::create(
              builder, loc, isAcquired,
              /*withElseRegion=*/false,
              [&](mlir::OpBuilder &, mlir::Location) {
                // Push __cxa_guard_abort cleanup for the exceptional path.
                cgf.ehStack.pushCleanup<CallGuardAbort>(EHCleanup, guard,
                                                        guardTy);

                // Wrap init in a RunCleanupsScope so that any deferred
                // deactivation cleanups pushed during aggregate
                // initialization (e.g. member destructors for exception
                // safety) are deactivated before we pop the guard-abort
                // cleanup below. Without this scope, those cleanups sit
                // on top of the guard-abort cleanup and popCleanupBlock
                // would pop the wrong one.
                {
                  CIRGenFunction::RunCleanupsScope initScope(cgf);
                  emitInitAndDtor();
                }

                // Pop the guard-abort cleanup (normal path only).
                cgf.popCleanupBlock();

                // void __cxa_guard_release(__guard *);
                cir::PointerType guardPtrTy2 = builder.getPointerTo(guardTy);
                cir::FuncType releaseFnTy =
                    builder.getFuncType({guardPtrTy2}, builder.getVoidTy());
                cir::FuncOp releaseFn = cgm.createRuntimeFunction(
                    releaseFnTy, "__cxa_guard_release");
                setRuntimeFnNothrow(releaseFn);
                cgf.emitNounwindRuntimeCall(loc, releaseFn, {guardPtr});

                builder.createYield(loc);
              });
        } else {
          // Non-threadsafe path: simple byte-store guard pattern.
          // For non-local variables, set guard BEFORE init to prevent
          // recursive re-initialization. For local variables, set guard
          // AFTER init so exception-interrupted init can be retried.
          if (!d.isLocalVarDecl())
            storeGuardByte();

          // Wrap init in a RunCleanupsScope so that any EH cleanups
          // pushed during aggregate initialization (e.g. member
          // destructors for exception safety) are popped before
          // leaving the guard scope. Without this, those cleanups
          // reference values defined inside the guard cir.if but
          // get emitted outside it, causing domination errors.
          {
            CIRGenFunction::RunCleanupsScope initScope(cgf);
            emitInitAndDtor();
          }

          if (d.isLocalVarDecl())
            storeGuardByte();
        }

        builder.createYield(loc);
      });

  // Set branch weights on the guard check. The initialization path is cold
  // (weight 1) vs the fast path (weight NumInits-1). Matches OGCG's
  // EmitCXXGuardedInitBranch (CGDeclCXX.cpp:401-436).
  // For non-local variables (COMDAT), OGCG skips weights because we have no
  // way to know how many DSOs will try to initialize the variable.
  if (d.isLocalVarDecl()) {
    uint64_t numInits = d.getTLSKind() ? 1024 : 1024 * 1024;
    auto trueWeight = builder.getI32IntegerAttr(1);
    auto falseWeight = builder.getI32IntegerAttr(numInits - 1);
    guardIf->setAttr("cir.branch_weights",
                      builder.getArrayAttr({trueWeight, falseWeight}));
  }
}

mlir::Value CIRGenItaniumCXXABI::getCXXDestructorImplicitParam(
    CIRGenFunction &cgf, const CXXDestructorDecl *dd, CXXDtorType type,
    bool forVirtualBase, bool delegating) {
  GlobalDecl gd(dd, type);
  return cgf.getVTTParameter(gd, forVirtualBase, delegating);
}

// The idea here is creating a separate block for the throw with an
// `UnreachableOp` as the terminator. So, we branch from the current block
// to the throw block and create a block for the remaining operations.
static void insertThrowAndSplit(mlir::OpBuilder &builder, mlir::Location loc,
                                mlir::Value exceptionPtr = {},
                                mlir::FlatSymbolRefAttr typeInfo = {},
                                mlir::FlatSymbolRefAttr dtor = {},
                                mlir::ArrayAttr dtorPtrauth = {}) {
  mlir::Block *currentBlock = builder.getInsertionBlock();
  mlir::Region *region = currentBlock->getParent();

  if (currentBlock->empty()) {
    auto throwOp = cir::ThrowOp::create(builder, loc, exceptionPtr, typeInfo,
                                        dtor);
    if (dtorPtrauth)
      throwOp->setAttr("cir.throw_dtor_ptrauth", dtorPtrauth);
    cir::UnreachableOp::create(builder, loc);
  } else {
    mlir::Block *throwBlock = builder.createBlock(region);

    auto throwOp = cir::ThrowOp::create(builder, loc, exceptionPtr, typeInfo,
                                        dtor);
    if (dtorPtrauth)
      throwOp->setAttr("cir.throw_dtor_ptrauth", dtorPtrauth);
    cir::UnreachableOp::create(builder, loc);

    builder.setInsertionPointToEnd(currentBlock);
    cir::BrOp::create(builder, loc, throwBlock);
  }

  (void)builder.createBlock(region);
}

void CIRGenItaniumCXXABI::emitRethrow(CIRGenFunction &cgf, bool isNoReturn) {
  // void __cxa_rethrow();
  // In CIR, cir.throw with no operands represents a rethrow. The isNoReturn
  // distinction (whether __cxa_rethrow is marked noreturn) only affects LLVM
  // optimization — in CIR, throw is always a terminator.
  CIRGenBuilderTy &builder = cgf.getBuilder();
  assert(cgf.currSrcLoc && "expected source location");
  mlir::Location loc = *cgf.currSrcLoc;
  insertThrowAndSplit(builder, loc);
}

void CIRGenItaniumCXXABI::emitThrow(CIRGenFunction &cgf,
                                    const CXXThrowExpr *e) {
  // This differs a bit from LLVM codegen, CIR has native operations for some
  // cxa functions, and defers allocation size computation, always pass the dtor
  // symbol, etc. CIRGen also does not use getAllocateExceptionFn / getThrowFn.

  // Now allocate the exception object.
  CIRGenBuilderTy &builder = cgf.getBuilder();
  QualType clangThrowType = e->getSubExpr()->getType();
  cir::PointerType throwTy = builder.getPointerTo(
      cgf.convertType(clangThrowType), getExplicitDefaultAddressSpace(cgm));
  uint64_t typeSize =
      cgf.getContext().getTypeSizeInChars(clangThrowType).getQuantity();
  mlir::Location subExprLoc = cgf.getLoc(e->getSubExpr()->getSourceRange());

  // Defer computing allocation size to some later lowering pass.
  mlir::TypedValue<cir::PointerType> exceptionPtr =
      cir::AllocExceptionOp::create(builder, subExprLoc, throwTy,
                                    builder.getI64IntegerAttr(typeSize))
          .getAddr();

  // Save EH scope state before expression evaluation. emitAnyExprToExn may
  // push new EH cleanup scopes (e.g., ~X() temp destructors from
  // `throw Error(X())`). We compare before/after to detect new scopes.
  EHScopeStack::stable_iterator priorEHScope = cgf.ehStack.stable_begin();

  // Build expression and store its result into exceptionPtr.
  CharUnits exnAlign = cgf.getContext().getExnObjectAlignment();
  cgf.emitAnyExprToExn(e->getSubExpr(), Address(exceptionPtr, exnAlign));

  // Get the RTTI symbol address.
  auto typeInfo = mlir::cast<cir::GlobalViewAttr>(
      cgm.getAddrOfRTTIDescriptor(subExprLoc, clangThrowType,
                                  /*forEH=*/true));
  assert(!typeInfo.getIndices() && "expected no indirection");

  // The address of the destructor.
  //
  // For records with non-trivial destructors, pass the destructor symbol
  // to __cxa_throw so the runtime can clean up the exception object.
  // For trivial destructors, pass null (no cleanup needed).
  //
  mlir::FlatSymbolRefAttr dtorSymbol;
  mlir::ArrayAttr dtorPtrauth;
  if (const RecordType *recordTy = clangThrowType->getAs<RecordType>()) {
    auto *rec = cast<CXXRecordDecl>(recordTy->getDecl()->getDefinition());
    if (!rec->hasTrivialDestructor()) {
      CXXDestructorDecl *dtorD = rec->getDestructor();
      auto dtorAddr = cgm.getAddrOfCXXStructor(
          GlobalDecl(dtorD, CXXDtorType::Dtor_Complete));
      dtorSymbol = mlir::FlatSymbolRefAttr::get(
          builder.getContext(),
          mlir::cast<cir::FuncOp>(dtorAddr).getSymName());

      const ASTContext &ctx = cgm.getASTContext();
      QualType dtorTy = ctx.getFunctionType(ctx.VoidTy, {ctx.VoidPtrTy},
                                            FunctionProtoType::ExtProtoInfo());
      if (auto pointerAuth = cgm.getFunctionPointerAuthInfo(dtorTy)) {
        dtorPtrauth = builder.getArrayAttr(
            {builder.getI32IntegerAttr(pointerAuth->key),
             builder.getI64IntegerAttr(pointerAuth->discriminator)});
      }
    }
  }

  // Now throw the exception.
  mlir::Location loc = cgf.getLoc(e->getSourceRange());

  // Check if expression evaluation pushed new EH cleanup scopes (e.g.,
  // ~X() temp destructors from `throw Error(X())`). If so, __cxa_throw
  // must be an invoke (not a call) so these temporaries are destroyed
  // during stack unwinding. We emit `cir.call exception @__cxa_throw`
  // with per-call cleanup containing ONLY the new scopes (expression
  // temps). Pre-existing RAII scopes (e.g., ~Guard()) are handled by
  // the enclosing TryOp's handler, avoiding double-destroy.
  // Check if the stack actually grew (new scopes pushed on top of
  // priorEHScope). Use strictlyEncloses rather than != because the stack
  // can also SHRINK during expression evaluation (e.g., a nested
  // ExprWithCleanups whose LexicalScope::forceCleanup() pops temporaries).
  // A shrunk stack means no new cleanups — proceeding would iterate stale
  // stable_iterators and hit the "finding savepoint after pop" assertion.
  bool hasNewCleanups =
      priorEHScope.strictlyEncloses(cgf.ehStack.stable_begin());

  if (!hasNewCleanups) {
    // No new scopes from expression evaluation. Use simple ThrowOp.
    // Pre-existing RAII cleanup (if any) is handled by the enclosing
    // TryOp (synthetic from emitCXXThrowExpr or user try-catch).
    insertThrowAndSplit(builder, loc, exceptionPtr, typeInfo.getSymbol(),
                        dtorSymbol, dtorPtrauth);
    return;
  }

  // Expression evaluation pushed new EH scopes. Emit __cxa_throw as
  // cir.call exception with per-call cleanup for these new scopes.

  // Check if we're inside a TryOp (user try-catch or synthetic from
  // emitCXXThrowExpr). Use the LexicalScope chain rather than walking the
  // MLIR parent-op chain: at this point the throw is typically emitted from
  // inside a scope-builder callback (e.g. the full-expression cir.scope of
  // `throw std::string("oops")`) whose ScopeOp has not yet been inserted
  // into its parent TryOp region, so getParentOp() would miss an enclosing
  // *user* try and wrongly report insideTryOp == false — causing emitThrow
  // to wrap __cxa_throw in its own synthetic cleanup-only TryOp whose
  // resume-only handler drops the enclosing RAII destructors on the
  // propagation edge. getClosestTryParent() walks the reliable LexicalScope
  // chain (matching emitCXXThrowExpr's own enclosingTryOp lookup).
  cir::TryOp enclosingTryOp =
      cgf.curLexScope ? cgf.curLexScope->getClosestTryParent() : nullptr;
  bool insideTryOp = enclosingTryOp != nullptr;
  // A synthetic TryOp (created by emitCXXThrowExpr when there is no user
  // try) emits the pre-existing RAII cleanups in its own handler, so the
  // per-call cleanup below must contain ONLY the new expression-temp scopes
  // (avoids the double-destroy). A user try's handler does NOT
  // contain those RAII cleanups — they are emitted per throwing call — so
  // __cxa_throw's per-call cleanup must include every active EH cleanup up
  // to the catch, exactly like populateEHCatchRegions does for an ordinary
  // throwing call. Otherwise the enclosing local's destructor is skipped on
  // the __cxa_throw propagation edge.
  bool enclosingTryIsUser = insideTryOp && !enclosingTryOp.getSynthetic();

  // Prepare __cxa_throw arguments.
  auto *ctx = builder.getContext();
  auto voidTy = cir::VoidType::get(ctx);
  auto excPtrTy = mlir::cast<cir::PointerType>(exceptionPtr.getType());
  auto ptrToVoid = builder.getVoidPtrTy(excPtrTy.getAddrSpace());
  auto typeInfoPtrTy = mlir::cast<cir::PointerType>(typeInfo.getType());
  auto typeInfoVoidTy = builder.getVoidPtrTy(typeInfoPtrTy.getAddrSpace());
  auto throwFnTy =
      cir::FuncType::get({ptrToVoid, typeInfoVoidTy, ptrToVoid}, voidTy);

  // Get or create @__cxa_throw declaration.
  cgm.createRuntimeFunction(throwFnTy, "__cxa_throw");

  // Cast exception pointer to void*.
  mlir::Value excPtrVoid = cir::CastOp::create(
      builder, loc, ptrToVoid, cir::CastKind::bitcast, exceptionPtr);

  // Get type_info address and cast to void*.
  auto typeInfoGlobal = mlir::cast<cir::GlobalOp>(
      cgm.getGlobalValue(typeInfo.getSymbol().getValue()));
  auto ptrToTypeInfo = builder.getPointerTo(typeInfoGlobal.getSymType(),
                                            typeInfoGlobal.getAddrSpaceAttr());
  mlir::Value typeInfoAddr =
      cir::GetGlobalOp::create(builder, loc, ptrToTypeInfo,
                               typeInfo.getSymbol())
          .getAddr();
  mlir::Value typeInfoVoid = cir::CastOp::create(
      builder, loc, typeInfoVoidTy, cir::CastKind::bitcast, typeInfoAddr);

  // Dtor arg — use actual dtor symbol if present, null otherwise.
  mlir::Value dtorArg;
  if (dtorSymbol) {
    auto dtorFunc = mlir::cast<cir::FuncOp>(
        cgm.getGlobalValue(dtorSymbol.getValue()));
    auto ptrToDtor =
        builder.getPointerTo(dtorFunc.getFunctionType(), excPtrTy.getAddrSpace());
    mlir::Value dtorAddr =
        cir::GetGlobalOp::create(builder, loc, ptrToDtor,
                                 dtorSymbol.getValue())
            .getAddr();
    dtorArg = cir::CastOp::create(builder, loc, ptrToVoid,
                                   cir::CastKind::bitcast, dtorAddr);
  } else {
    dtorArg = builder.getNullPtr(ptrToVoid, loc);
  }

  // Helper lambda: emit per-call cleanup from new EH scopes only
  // (between stack top and priorEHScope). This avoids including
  // pre-existing RAII scopes that are handled by the TryOp's handler.
  auto emitNewScopeCleanup = [&](cir::CallOp callOp) {
    mlir::OpBuilder::InsertionGuard guard(builder);
    assert(callOp.getCleanup().empty() &&
           "expected empty cleanup region");
    builder.createBlock(&callOp.getCleanup());
    bool savedIsEmittingEHCleanup = cgf.isEmittingEHCleanup;
    cgf.isEmittingEHCleanup = true;
    // Walk the EH scope chain from innermost outward, emitting cleanup code
    // for each active EH cleanup scope. The boundary depends on the
    // enclosing try: for a *user* try there is no handler emitting the
    // pre-existing RAII cleanups, so walk the ENTIRE active EH cleanup chain
    // (throw temps AND enclosing RAII objects), stopping only at a
    // non-Cleanup scope (e.g. Catch) or the stack end — exactly like
    // populateEHCatchRegions. For a synthetic try, stop at priorEHScope so
    // only the new expression-temp scopes are emitted here (the synthetic
    // handler emits the pre-existing RAII scopes;). Use
    // strictlyEncloses rather than equality because priorEHScope is a
    // stable_begin() snapshot that may point to a normal-only cleanup not in
    // the EH chain (getEnclosingEHScope() skips non-EH scopes); an equality
    // check would miss the boundary and walk off the end of the stack.
    for (EHScopeStack::stable_iterator si =
             cgf.ehStack.getInnermostEHScope();
         enclosingTryIsUser ? (si != cgf.ehStack.stable_end())
                            : priorEHScope.strictlyEncloses(si);) {
      EHScope &s = *cgf.ehStack.find(si);
      if (s.getKind() != EHScope::Cleanup)
        break;
      EHCleanupScope &cs = cast<EHCleanupScope>(s);
      if (!cs.isEHCleanup() || !cs.isActive()) {
        si = cgf.ehStack.find(si)->getEnclosingEHScope();
        continue;
      }
      EHScopeStack::Cleanup *cleanup = cs.getCleanup();
      EHScopeStack::Cleanup::Flags flags;
      flags.setIsForEHCleanup();
      flags.setIsEHCleanupKind();
      cleanup->emit(cgf, flags);
      // After emit(), the EH scope stack may have been modified (e.g.,
      // DestroyObject pushes/pops partial array cleanups). Guard against
      // stale iterators.
      if (!cgf.ehStack.contains(si))
        break;
      si = cgf.ehStack.find(si)->getEnclosingEHScope();
    }
    cgf.isEmittingEHCleanup = savedIsEmittingEHCleanup;
    cir::YieldOp::create(builder, callOp.getLoc());
  };

  if (!insideTryOp) {
    // Not inside any TryOp. Create a synthetic cleanup-only TryOp with
    // a resume-only handler. The per-call cleanup on the cir.call exception
    // handles the actual cleanup code; the TryOp exists only so FlattenCFG
    // can create invoke + landing pad.
    auto syntheticTryOp = cir::TryOp::create(
        builder, loc,
        /*tryBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location l) {},
        /*handlersBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location l,
            mlir::OperationState &result) {
          auto *r = result.addRegion();
          builder.createBlock(r);
          cir::ResumeOp::create(builder, loc);
        });
    syntheticTryOp.setSynthetic(true);
    syntheticTryOp.setCleanup(true);
    syntheticTryOp.setHandlerTypesAttr(
        mlir::ArrayAttr::get(ctx, {cir::UnwindAttr::get(ctx)}));

    mlir::OpBuilder::InsertPoint outerIP = builder.saveInsertionPoint();
    builder.setInsertionPointToStart(
        &syntheticTryOp.getTryRegion().back());

    // Emit cir.call exception @__cxa_throw inside the synthetic TryOp.
    auto callOp = cir::CallOp::create(
        builder, loc, mlir::SymbolRefAttr::get(ctx, "__cxa_throw"),
        /*resType=*/mlir::Type{},
        mlir::ValueRange{excPtrVoid, typeInfoVoid, dtorArg},
        cir::CallingConv::C, cir::SideEffect::All, builder.getUnitAttr());
    if (dtorPtrauth)
      callOp->setAttr("cir.throw_dtor_ptrauth", dtorPtrauth);

    emitNewScopeCleanup(callOp);

    // __cxa_throw is noreturn.
    cir::UnreachableOp::create(builder, loc);

    // Dead continuation block inside try region (TryOp requires yield).
    builder.createBlock(&syntheticTryOp.getTryRegion());
    cir::YieldOp::create(builder, loc);

    // Restore insertion point to after the TryOp.
    builder.restoreInsertionPoint(outerIP);

    // The block containing the TryOp needs a terminator.
    cir::UnreachableOp::create(builder, loc);

    // Create dead continuation block (matching insertThrowAndSplit).
    mlir::Region *region = builder.getInsertionBlock()->getParent();
    (void)builder.createBlock(region);
  } else {
    // Inside a TryOp (user try-catch or synthetic from emitCXXThrowExpr).
    // Emit cir.call exception @__cxa_throw with per-call cleanup for the
    // new scopes (expression temps). Pre-existing RAII scopes are in the
    // TryOp's handler.
    auto callOp = cir::CallOp::create(
        builder, loc, mlir::SymbolRefAttr::get(ctx, "__cxa_throw"),
        /*resType=*/mlir::Type{},
        mlir::ValueRange{excPtrVoid, typeInfoVoid, dtorArg},
        cir::CallingConv::C, cir::SideEffect::All, builder.getUnitAttr());
    if (dtorPtrauth)
      callOp->setAttr("cir.throw_dtor_ptrauth", dtorPtrauth);

    emitNewScopeCleanup(callOp);

    // __cxa_throw is noreturn.
    cir::UnreachableOp::create(builder, loc);

    // Create dead continuation block (matching insertThrowAndSplit).
    mlir::Region *region = builder.getInsertionBlock()->getParent();
    (void)builder.createBlock(region);
  }
}

CIRGenCXXABI *clang::CIRGen::CreateCIRGenItaniumCXXABI(CIRGenModule &cgm) {
  switch (cgm.getASTContext().getCXXABIKind()) {
  case TargetCXXABI::GenericItanium:
    return new CIRGenItaniumCXXABI(cgm);

  // Note that AArch64 uses the generic CIRGenItaniumCXXABI class since it
  // doesn't include the other 32-bit ARM oddities: constructor/destructor
  // return values and array cookies.
  case TargetCXXABI::GenericAArch64:
    return new CIRGenItaniumCXXABI(cgm, /*useARMMethodPtrABI=*/true,
                                   /*useARMGuardVarABI=*/true);

  case TargetCXXABI::AppleARM64:
    return new CIRGenAppleARM64CXXABI(cgm);

  // For IR-generation purposes, there's no significant difference between the
  // ARM and iOS ABIs. The 32-bit-ARM family requires CIRGenARMCXXABI for the
  // ARM oddities the generic Itanium ABI lacks: constructors/destructors return
  // 'this', and array cookies use the ARM layout.
  case TargetCXXABI::GenericARM:
  case TargetCXXABI::iOS:
  case TargetCXXABI::WatchOS:
    return new CIRGenARMCXXABI(cgm);

  case TargetCXXABI::Fuchsia:
    return new CIRGenFuchsiaCXXABI(cgm);

  case TargetCXXABI::GenericMIPS:
    return new CIRGenItaniumCXXABI(cgm, /*useARMMethodPtrABI=*/true);

  case TargetCXXABI::WebAssembly:
    return new CIRGenItaniumCXXABI(cgm);

  case TargetCXXABI::XL:
    return new CIRGenItaniumCXXABI(cgm);

  case TargetCXXABI::Microsoft:
    // CIR has no Microsoft-specific C++ ABI implementation yet. In C mode we
    // still need a concrete CIRGenCXXABI for mangling/plumbing even though no
    // C++ ABI hooks are exercised. Keep C++ mode blocked until an MS ABI
    // backend exists.
    if (!cgm.getLangOpts().CPlusPlus)
      return new CIRGenItaniumCXXABI(cgm);
    llvm_unreachable("Microsoft ABI is not Itanium-based");
  }
  llvm_unreachable("bad ABI kind");
}

CIRGenCXXABI *clang::CIRGen::CreateCIRGenMicrosoftCXXABI(CIRGenModule &cgm) {
  assert(cgm.getASTContext().getCXXABIKind() == TargetCXXABI::Microsoft &&
         "unexpected ABI kind");
  return new CIRGenMicrosoftCXXABI(cgm);
}

cir::GlobalOp CIRGenItaniumCXXABI::getAddrOfVTable(const CXXRecordDecl *rd,
                                                   CharUnits vptrOffset) {
  assert(vptrOffset.isZero() && "Itanium ABI only supports zero vptr offsets");
  cir::GlobalOp &vtable = vtables[rd];
  if (vtable)
    return vtable;

  // Queue up this vtable for possible deferred emission.
  cgm.addDeferredVTable(rd);

  SmallString<256> name;
  llvm::raw_svector_ostream out(name);
  getMangleContext().mangleCXXVTable(rd, out);

  const VTableLayout &vtLayout =
      cgm.getItaniumVTableContext().getVTableLayout(rd);
  mlir::Type vtableType = cgm.getVTables().getVTableType(vtLayout);

  // Use pointer alignment for the vtable. Otherwise we would align them based
  // on the size of the initializer which doesn't make sense as only single
  // values are read.
  unsigned ptrAlign = cgm.getItaniumVTableContext().isRelativeLayout()
                          ? 32
                          : cgm.getTarget().getPointerAlign(LangAS::Default);

  vtable = cgm.createOrReplaceCXXRuntimeVariable(
      cgm.getLoc(rd->getSourceRange()), name, vtableType,
      cir::GlobalLinkageKind::ExternalLinkage,
      cgm.getASTContext().toCharUnitsFromBits(ptrAlign),
      cgm.getDefaultGlobalsAddressSpace());
  // V-tables are always unnamed_addr — their address identity is never
  // semantically significant. Match OGCG's getAddrOfVTable behavior.
  vtable->setAttr("unnamed_addr",
                  mlir::UnitAttr::get(vtable->getContext()));

  // In MS C++ if you have a class with virtual functions in which you are using
  // selective member import/export, then all virtual functions must be exported
  // unless they are inline, otherwise a link error will result. To match this
  // behavior, for such classes, we dllimport the vtable if it is defined
  // externally and all the non-inline virtual methods are marked dllimport, and
  // we dllexport the vtable if it is defined in this TU and all the non-inline
  // virtual methods are marked dllexport.
  if (cgm.getTarget().hasPS4DLLImportExport()) {
    cgm.errorNYI(rd->getSourceRange(),
                 "getAddrOfVTable: PS4 DLL import/export");
    return {};
  }

  cgm.setGVProperties(vtable, rd);
  return vtable;
}

CIRGenCallee CIRGenItaniumCXXABI::getVirtualFunctionPointer(
    CIRGenFunction &cgf, clang::GlobalDecl gd, Address thisAddr, mlir::Type ty,
    SourceLocation srcLoc) {
  CIRGenBuilderTy &builder = cgm.getBuilder();
  mlir::Location loc = cgf.getLoc(srcLoc);
  auto globalsAS = cgm.getDefaultGlobalsAddressSpace();
  cir::PointerType tyPtr = builder.getPointerTo(ty, globalsAS);
  auto *methodDecl = cast<CXXMethodDecl>(gd.getDecl());
  mlir::Value vtable = cgf.getVTablePtr(loc, thisAddr, methodDecl->getParent());

  uint64_t vtableIndex = cgm.getItaniumVTableContext().getMethodVTableIndex(gd);
  mlir::Value vfunc{};
  mlir::Value vfuncLoad;
  mlir::Value vtableSlotPtr;
  if (cgm.getItaniumVTableContext().isRelativeLayout()) {
    mlir::Value byteOffset = builder.getUInt32(static_cast<uint32_t>(vtableIndex * 4), loc);
    auto loadRelative = cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("load.relative"), tyPtr,
        mlir::ValueRange{vtable, byteOffset});
    loadRelative->setAttr("tail", mlir::UnitAttr::get(builder.getContext()));
    vfuncLoad = loadRelative.getResult();
  } else {
    vtableSlotPtr = cir::VTableGetVirtualFnAddrOp::create(
        builder, loc, builder.getPointerTo(tyPtr, globalsAS), vtable,
        vtableIndex);
    vfuncLoad = builder.createAlignedLoad(loc, tyPtr, vtableSlotPtr,
                                          cgf.getPointerAlign());
  }

  // Add !invariant.load md to virtual function load to indicate that
  // function didn't change inside vtable.
  // It's safe to add it without -fstrict-vtable-pointers, but it would not
  // help in devirtualization because it will only matter if we will have 2
  // the same virtual function loads from the same vtable load, which won't
  // happen without enabled devirtualization with -fstrict-vtable-pointers.
  if (cgm.getCodeGenOpts().OptimizationLevel > 0 &&
      cgm.getCodeGenOpts().StrictVTablePointers) {
    // Mark virtual function slot load for !invariant.load lowering,
    // matching OGCG under -O1+ -fstrict-vtable-pointers.
    if (mlir::Operation *loadOp = vfuncLoad.getDefiningOp())
      loadOp->setAttr("cir.invariant_load",
                      mlir::UnitAttr::get(builder.getContext()));
  }
  vfunc = vfuncLoad;

  // The virtual function pointer is loaded from the vtable, which lives in the
  // globals address space; on targets where that is not the default address
  // space (e.g. AMDGCN), convert it to a default-address-space pointer for the
  // call with an address-space cast. Otherwise downstream call emission would
  // bitcast across address spaces, which is invalid.
  cir::PointerType defaultTyPtr = builder.getPointerTo(ty);
  if (tyPtr != defaultTyPtr)
    vfunc = builder.createAddrSpaceCast(loc, vfunc, defaultTyPtr);

  std::optional<CIRGenPointerAuthInfo> pointerAuthInfo;
  const auto &schema =
      cgm.getCodeGenOpts().PointerAuth.CXXVirtualFunctionPointers;
  if (schema &&
      schema.getAuthenticationMode() != PointerAuthenticationMode::None) {
    GlobalDecl originalGD =
        cgm.getItaniumVTableContext().findOriginalMethod(gd.getCanonicalDecl());

    uint64_t discriminator = 0;
    switch (schema.getOtherDiscrimination()) {
    case PointerAuthSchema::Discrimination::None:
      break;
    case PointerAuthSchema::Discrimination::Type:
      discriminator = cgm.getASTContext().getPointerAuthTypeDiscriminator(
          cast<CXXMethodDecl>(originalGD.getDecl())->getType());
      break;
    case PointerAuthSchema::Discrimination::Decl:
      discriminator =
          llvm::getPointerAuthStableSipHash(cgm.getMangledName(originalGD));
      break;
    case PointerAuthSchema::Discrimination::Constant:
      discriminator = schema.getConstantDiscrimination();
      break;
    }

    pointerAuthInfo = CIRGenPointerAuthInfo{
        /*enabled=*/true,
        /*key=*/schema.getKey(),
        /*discriminator=*/discriminator,
        /*authenticationMode=*/schema.getAuthenticationMode(),
        /*isIsaPointer=*/schema.isIsaPointer(),
        /*authenticatesNullValues=*/schema.authenticatesNullValues(),
    };

    CIRGenCalleeInfo calleeInfo(
        cast<CXXMethodDecl>(gd.getDecl())->getType()->getAs<FunctionProtoType>(),
        originalGD, pointerAuthInfo);
    return CIRGenCallee::forDirect(vfunc.getDefiningOp(), calleeInfo);
  }

  return CIRGenCallee::forDirect(vfunc.getDefiningOp(), CIRGenCalleeInfo(gd));
}

mlir::Value CIRGenItaniumCXXABI::getVTableAddressPointInStructorWithVTT(
    CIRGenFunction &cgf, const CXXRecordDecl *vtableClass, BaseSubobject base,
    const CXXRecordDecl *nearestVBase) {
  assert((base.getBase()->getNumVBases() || nearestVBase != nullptr) &&
         needsVTTParameter(cgf.curGD) && "This class doesn't have VTT");

  // Get the secondary vpointer index.
  uint64_t virtualPointerIndex =
      cgm.getVTables().getSecondaryVirtualPointerIndex(vtableClass, base);

  /// Load the VTT.
  mlir::Value vttPtr = cgf.loadCXXVTT();
  mlir::Location loc = cgf.getLoc(vtableClass->getSourceRange());
  // Calculate the address point from the VTT, and the offset may be zero.
  vttPtr = cgf.getBuilder().createVTTAddrPoint(loc, vttPtr.getType(), vttPtr,
                                               virtualPointerIndex);
  // And load the address point from the VTT.
  auto vptrType = cir::VPtrType::get(cgf.getBuilder().getContext());
  mlir::Value result = cgf.getBuilder()
                           .createAlignedLoad(loc, vptrType, vttPtr,
                                              cgf.getPointerAlign())
                           .getResult();

  if (const auto &schema =
          cgm.getCodeGenOpts().PointerAuth.CXXVTTVTablePointers) {
    CIRGenRuntimePointerAuthInfo pointerAuth{
        schema.getAuthenticationMode(),
        /*isIsaPointer=*/false,
        /*authenticatesNullValues=*/false,
        schema.getKey(),
        mlir::Value{}};

    uint64_t discriminator =
        cgm.getPointerAuthOtherDiscriminator(schema, GlobalDecl(), QualType());
    if (discriminator)
      pointerAuth.discriminator =
          cgf.getBuilder().getConstantInt(loc, cgm.uIntPtrTy, discriminator);

    if (schema.isAddressDiscriminated()) {
      if (pointerAuth.discriminator)
        pointerAuth.discriminator =
            cgf.emitPointerAuthBlendDiscriminator(vttPtr,
                                                  pointerAuth.discriminator);
      else
        pointerAuth.discriminator =
            cgf.getBuilder().createPtrToInt(vttPtr, cgm.uIntPtrTy);
    }

    mlir::Value resultAsPtr = cgf.getBuilder().createBitcast(
        loc, result, cgm.getDefaultGlobalsInt8PtrTy());
    resultAsPtr = cgf.emitPointerAuthAuth(pointerAuth, resultAsPtr);
    result = cgf.getBuilder().createBitcast(loc, resultAsPtr, vptrType);
  }

  return result;
}

mlir::Value
CIRGenItaniumCXXABI::getVTableAddressPoint(BaseSubobject base,
                                           const CXXRecordDecl *vtableClass) {
  cir::GlobalOp vtable = getAddrOfVTable(vtableClass, CharUnits());

  // Find the appropriate vtable within the vtable group, and the address point
  // within that vtable.
  VTableLayout::AddressPointLocation addressPoint =
      cgm.getItaniumVTableContext()
          .getVTableLayout(vtableClass)
          .getAddressPoint(base);

  mlir::OpBuilder &builder = cgm.getBuilder();
  auto vtablePtrTy = cir::VPtrType::get(builder.getContext());

  // Compute inrange values for the vtable GEP, matching OGCG's
  // ItaniumCXXABI.cpp:2187-2194. This tells the optimizer the valid
  // byte range within the vtable sub-array, enabling devirtualization.
  const VTableLayout &layout =
      cgm.getItaniumVTableContext().getVTableLayout(vtableClass);
  // Each vtable component is a pointer in the standard ABI, or i32 in the
  // relative vtable ABI. Use the actual component type size.
  mlir::Type compType = cgm.getVTableComponentType();
  unsigned componentSize =
      cgm.getDataLayout().getTypeAllocSize(compType);
  unsigned vtableSize =
      componentSize * layout.getVTableSize(addressPoint.VTableIndex);
  unsigned offset = componentSize * addressPoint.AddressPointIndex;

  return cir::VTableAddrPointOp::create(
      builder, cgm.getLoc(vtableClass->getSourceRange()), vtablePtrTy,
      mlir::FlatSymbolRefAttr::get(vtable.getSymNameAttr()),
      cir::AddressPointAttr::get(cgm.getBuilder().getContext(),
                                 addressPoint.VTableIndex,
                                 addressPoint.AddressPointIndex),
      builder.getI32IntegerAttr(vtableSize),
      builder.getI32IntegerAttr(offset));
}

mlir::Value CIRGenItaniumCXXABI::getVTableAddressPointInStructor(
    CIRGenFunction &cgf, const clang::CXXRecordDecl *vtableClass,
    clang::BaseSubobject base, const clang::CXXRecordDecl *nearestVBase) {

  if ((base.getBase()->getNumVBases() || nearestVBase != nullptr) &&
      needsVTTParameter(cgf.curGD)) {
    return getVTableAddressPointInStructorWithVTT(cgf, vtableClass, base,
                                                  nearestVBase);
  }
  return getVTableAddressPoint(base, vtableClass);
}

bool CIRGenItaniumCXXABI::isVirtualOffsetNeededForVTableField(
    CIRGenFunction &cgf, CIRGenFunction::VPtr vptr) {
  if (vptr.nearestVBase == nullptr)
    return false;
  return needsVTTParameter(cgf.curGD);
}

mlir::Value CIRGenItaniumCXXABI::getVirtualBaseClassOffset(
    mlir::Location loc, CIRGenFunction &cgf, Address thisAddr,
    const CXXRecordDecl *classDecl, const CXXRecordDecl *baseClassDecl) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Value vtablePtr = cgf.getVTablePtr(loc, thisAddr, classDecl);
  auto globalsAS = cgm.getDefaultGlobalsAddressSpace();
  auto globalsInt8PtrTy = cgm.getDefaultGlobalsInt8PtrTy();
  mlir::Value vtableBytePtr =
      builder.createBitcast(vtablePtr, globalsInt8PtrTy);
  CharUnits vbaseOffsetOffset =
      cgm.getItaniumVTableContext().getVirtualBaseOffsetOffset(classDecl,
                                                               baseClassDecl);
  mlir::Value offsetVal =
      builder.getSInt64(vbaseOffsetOffset.getQuantity(), loc);
  // OGCG uses CreateConstGEP1_64 (no inbounds) here because the vbase offset
  // is stored at a negative index relative to the vtable address point.
  auto vbaseOffsetPtr = cir::PtrStrideOp::create(
      builder, loc, globalsInt8PtrTy, vtableBytePtr, offsetVal,
      cir::GEPNoWrapFlags::none);

  mlir::Value vbaseOffset;
  if (cgm.getItaniumVTableContext().isRelativeLayout()) {
    auto offsetPtr = builder.createBitcast(
        vbaseOffsetPtr, builder.getPointerTo(builder.getSInt32Ty(), globalsAS));
    vbaseOffset = builder.createLoad(
        loc,
        Address(offsetPtr, builder.getSInt32Ty(), CharUnits::fromQuantity(4)));
  } else {
    mlir::Value offsetPtr = builder.createBitcast(
        vbaseOffsetPtr, builder.getPointerTo(cgm.ptrDiffTy, globalsAS));
    vbaseOffset = builder.createLoad(
        loc, Address(offsetPtr, cgm.ptrDiffTy, cgf.getPointerAlign()));
  }
  return vbaseOffset;
}

static cir::FuncOp getBadCastFn(CIRGenFunction &cgf) {
  // Prototype: void __cxa_bad_cast();
  // __cxa_bad_cast() throws std::bad_cast and never returns.

  // Runtime functions use the C calling convention (FuncOp default).
  // OGCG sets RuntimeCC which is C for Itanium ABI.

  cir::FuncType fnTy =
      cgf.getBuilder().getFuncType({}, cgf.getBuilder().getVoidTy());
  return cgf.cgm.createRuntimeFunction(fnTy, "__cxa_bad_cast");
}

static void emitCallToBadCast(CIRGenFunction &cgf, mlir::Location loc) {
  // Runtime functions use the C calling convention (FuncOp default).

  // Use createCallOp directly instead of emitRuntimeCall so we can set the
  // exception attribute. When inside an EH scope (try block), __cxa_bad_cast()
  // must be an invoke so the thrown std::bad_cast routes to the catch handler
  // instead of going to std::terminate.
  cir::CallOp callOp =
      cgf.getBuilder().createCallOp(loc, getBadCastFn(cgf), mlir::ValueRange{});
  if (cgf.isCatchOrCleanupRequired())
    callOp.setExceptionAttr(
        mlir::UnitAttr::get(cgf.getBuilder().getContext()));
  // __cxa_bad_cast never returns — set noreturn on the call-site attributes
  // so the LLVM lowering propagates it. Matches OGCG's call-site attrs.
  mlir::NamedAttrList badCastCallAttrs;
  auto noReturn = cir::NoReturnAttr::get(&cgf.cgm.getMLIRContext());
  badCastCallAttrs.set(noReturn.getMnemonic(), noReturn);
  callOp->setAttr("extra_attrs",
                   cir::ExtraFuncAttributesAttr::get(
                       badCastCallAttrs.getDictionary(
                           &cgf.cgm.getMLIRContext())));
  // This path emits __cxa_bad_cast directly during CIRGen (the always-null and
  // effectively-final dynamic_cast cases, as opposed to the runtime cir.dyn_cast
  // op lowered later). Under a user try, thread the enclosing local destructors
  // onto its unwind edge so they run as std::bad_cast propagates to the catch.
  cgf.threadUserTryCleanupsOntoThrowingCall(callOp);
  cir::UnreachableOp::create(cgf.getBuilder(), loc);
  cgf.getBuilder().clearInsertionPoint();
}

void CIRGenItaniumCXXABI::emitBadCastCall(CIRGenFunction &cgf,
                                          mlir::Location loc) {
  emitCallToBadCast(cgf, loc);
}

// TODO(cir): This could be shared with classic codegen.
static CharUnits computeOffsetHint(ASTContext &astContext,
                                   const CXXRecordDecl *src,
                                   const CXXRecordDecl *dst) {
  CXXBasePaths paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                     /*DetectVirtual=*/false);

  // If Dst is not derived from Src we can skip the whole computation below and
  // return that Src is not a public base of Dst.  Record all inheritance paths.
  if (!dst->isDerivedFrom(src, paths))
    return CharUnits::fromQuantity(-2ULL);

  unsigned numPublicPaths = 0;
  CharUnits offset;

  // Now walk all possible inheritance paths.
  for (const CXXBasePath &path : paths) {
    if (path.Access != AS_public) // Ignore non-public inheritance.
      continue;

    ++numPublicPaths;

    for (const CXXBasePathElement &pathElement : path) {
      // If the path contains a virtual base class we can't give any hint.
      // -1: no hint.
      if (pathElement.Base->isVirtual())
        return CharUnits::fromQuantity(-1ULL);

      if (numPublicPaths > 1) // Won't use offsets, skip computation.
        continue;

      // Accumulate the base class offsets.
      const ASTRecordLayout &L =
          astContext.getASTRecordLayout(pathElement.Class);
      offset += L.getBaseClassOffset(
          pathElement.Base->getType()->getAsCXXRecordDecl());
    }
  }

  // -2: Src is not a public base of Dst.
  if (numPublicPaths == 0)
    return CharUnits::fromQuantity(-2ULL);

  // -3: Src is a multiple public base type but never a virtual base type.
  if (numPublicPaths > 1)
    return CharUnits::fromQuantity(-3ULL);

  // Otherwise, the Src type is a unique public nonvirtual base type of Dst.
  // Return the offset of Src from the origin of Dst.
  return offset;
}

static cir::FuncOp getItaniumDynamicCastFn(CIRGenFunction &cgf) {
  // Prototype:
  // void *__dynamic_cast(const void *sub,
  //                      global_as const abi::__class_type_info *src,
  //                      global_as const abi::__class_type_info *dst,
  //                      std::ptrdiff_t src2dst_offset);

  mlir::Type voidPtrTy = cgf.getBuilder().getVoidPtrTy();
  mlir::Type rttiPtrTy = cgf.cgm.getDefaultGlobalsInt8PtrTy();
  mlir::Type ptrDiffTy = cgf.convertType(cgf.getContext().getPointerDiffType());

  cir::FuncType fnTy = cgf.getBuilder().getFuncType(
      {voidPtrTy, rttiPtrTy, rttiPtrTy, ptrDiffTy}, voidPtrTy);

  // OGCG marks __dynamic_cast as nounwind, willreturn, readonly.
  cir::FuncOp fn = cgf.cgm.createRuntimeFunction(fnTy, "__dynamic_cast");
  mlir::NamedAttrList attrs;
  if (auto existingExtraAttrs = fn.getExtraAttrs())
    attrs = mlir::NamedAttrList{existingExtraAttrs->getElements().getValue()};
  auto noThrow = cir::NoThrowAttr::get(&cgf.cgm.getMLIRContext());
  attrs.set(noThrow.getMnemonic(), noThrow);
  // Mark as pure (readonly + willreturn) matching OGCG.
  attrs.set("side_effect",
            cir::SideEffectAttr::get(&cgf.cgm.getMLIRContext(),
                                     cir::SideEffect::Pure));
  fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
      attrs.getDictionary(&cgf.cgm.getMLIRContext())));

  return fn;
}

static Address emitDynamicCastToVoid(CIRGenFunction &cgf, mlir::Location loc,
                                     QualType srcRecordTy, Address src) {
  bool vtableUsesRelativeLayout =
      cgf.cgm.getItaniumVTableContext().isRelativeLayout();
  mlir::Value ptr = cgf.getBuilder().createDynCastToVoid(
      loc, src.getPointer(), vtableUsesRelativeLayout);
  return Address{ptr, src.getAlignment()};
}

static Address emitDynamicCastToVoidForExactCheck(CIRGenFunction &cgf,
                                                  mlir::Location loc,
                                                  QualType srcRecordTy,
                                                  Address src) {
  if (!cgf.getLangOpts().PointerAuthCalls ||
      cgf.cgm.getItaniumVTableContext().isRelativeLayout())
    return emitDynamicCastToVoid(cgf, loc, srcRecordTy, src);

  CIRGenBuilderTy &builder = cgf.getBuilder();
  const auto *classDecl = srcRecordTy->castAsCXXRecordDecl();
  auto globalsAS = cgf.cgm.getDefaultGlobalsAddressSpace();

  mlir::Value vtable = cgf.getVTablePtr(loc, src, classDecl);
  mlir::Value vtableAsPtr = builder.createBitcast(
      loc, vtable, builder.getPointerTo(cgf.ptrDiffTy, globalsAS));
  mlir::Value minusTwo = builder.getConstInt(loc, cgf.ptrDiffTy, -2);
  mlir::Value offsetToTopPtr = cir::PtrStrideOp::create(
      builder, loc, builder.getPointerTo(cgf.ptrDiffTy, globalsAS),
      vtableAsPtr, minusTwo,
      cir::GEPNoWrapFlags::inbounds);
  mlir::Value offsetToTop =
      builder.createAlignedLoad(loc, cgf.ptrDiffTy, offsetToTopPtr,
                                cgf.getPointerAlign());

  mlir::Type u8PtrTy = builder.getUInt8PtrTy();
  mlir::Value srcU8Ptr = builder.createBitcast(src.getPointer(), u8PtrTy);
  mlir::Value resultU8Ptr = cir::PtrStrideOp::create(
      builder, loc, u8PtrTy, srcU8Ptr, offsetToTop,
      cir::GEPNoWrapFlags::inbounds);
  return Address(resultU8Ptr, src.getAlignment());
}

static mlir::Value emitExactDynamicCast(CIRGenItaniumCXXABI &abi,
                                        CIRGenFunction &cgf, mlir::Location loc,
                                        QualType srcRecordTy,
                                        QualType destRecordTy,
                                        cir::PointerType destCIRTy,
                                        bool isRefCast, Address src) {
  // Find all the inheritance paths from SrcRecordTy to DestRecordTy.
  const CXXRecordDecl *srcDecl = srcRecordTy->getAsCXXRecordDecl();
  const CXXRecordDecl *destDecl = destRecordTy->getAsCXXRecordDecl();
  CXXBasePaths paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                     /*DetectVirtual=*/false);
  (void)destDecl->isDerivedFrom(srcDecl, paths);

  // Find an offset within `destDecl` where a `srcDecl` instance and its vptr
  // might appear.
  bool requiresCastToPrimaryBase = false;
  std::optional<CharUnits> offset;
  for (const CXXBasePath &path : paths) {
    // dynamic_cast only finds public inheritance paths.
    if (path.Access != AS_public)
      continue;

    CharUnits pathOffset;
    for (const CXXBasePathElement &pathElement : path) {
      // Find the offset along this inheritance step.
      const CXXRecordDecl *base =
          pathElement.Base->getType()->getAsCXXRecordDecl();
      if (pathElement.Base->isVirtual()) {
        // For a virtual base class, we know that the derived class is exactly
        // destDecl, so we can use the vbase offset from its layout.
        const ASTRecordLayout &layout =
            cgf.getContext().getASTRecordLayout(destDecl);
        pathOffset = layout.getVBaseClassOffset(base);
      } else {
        const ASTRecordLayout &layout =
            cgf.getContext().getASTRecordLayout(pathElement.Class);
        pathOffset += layout.getBaseClassOffset(base);
      }
    }

    if (!offset) {
      offset = pathOffset;
    } else if (offset != pathOffset) {
      // base appears in at least two different places. Find the most-derived
      // object and see if it's a DestDecl. Note that the most-derived object
      // must be at least as aligned as this base class subobject, and must
      // have a vptr at offset 0.
      offset = CharUnits::Zero();
      requiresCastToPrimaryBase = true;
      break;
    }
  }

  if (requiresCastToPrimaryBase) {
    src = emitDynamicCastToVoidForExactCheck(cgf, loc, srcRecordTy, src);
    srcDecl = destDecl;
  }

  CIRGenBuilderTy &builder = cgf.getBuilder();

  if (!offset) {
    // If there are no public inheritance paths, the cast always fails.
    mlir::Value nullPtrValue = builder.getNullPtr(destCIRTy, loc);
    if (isRefCast) {
      mlir::Region *currentRegion = builder.getBlock()->getParent();
      emitCallToBadCast(cgf, loc);

      // The call to bad_cast will terminate the block. Create a new block to
      // hold any follow up code.
      builder.createBlock(currentRegion, currentRegion->end());
    }

    return nullPtrValue;
  }

  mlir::Value expectedVPtr =
      abi.getVTableAddressPoint(BaseSubobject(srcDecl, *offset), destDecl);
  mlir::Value success;
  if (requiresCastToPrimaryBase) {
    // Match OGCG's exact dynamic_cast fast path for multiply-inherited source
    // bases: dynamic_cast<void*> authenticates the source vptr while finding
    // the complete object, but the exact-type comparison itself uses a
    // stripped load from the most-derived object address before performing a
    // must-trap authentication of the successful adjusted result.
    mlir::Type vptrTy = expectedVPtr.getType();
    mlir::Type vptrPtrTy = builder.getPointerTo(vptrTy);
    Address srcVPtrPtr(builder.createBitcast(src.getPointer(), vptrPtrTy),
                       src.getAlignment());
    cir::LoadOp srcVPtrLoad = builder.createLoad(loc, srcVPtrPtr);
    mlir::Value srcVPtr = srcVPtrLoad;

    cgf.cgm.decorateOperationWithTBAA(
        srcVPtrLoad, cgf.cgm.getTBAAVTablePtrAccessInfo(vptrTy));

    mlir::Value srcVPtrAsPtr = builder.createBitcast(
        loc, srcVPtr, cgf.cgm.getDefaultGlobalsInt8PtrTy());
    mlir::Value strippedSrcVPtr = srcVPtrAsPtr;
    if (cgf.getLangOpts().PointerAuthCalls) {
      CIRGenRuntimePointerAuthInfo stripInfo;
      stripInfo.authenticationMode = PointerAuthenticationMode::Strip;
      strippedSrcVPtr = cgf.emitPointerAuthAuth(stripInfo, srcVPtrAsPtr);
    }

    mlir::Value expectedVPtrAsPtr = builder.createBitcast(
        loc, expectedVPtr, cgf.cgm.getDefaultGlobalsInt8PtrTy());
    success = builder.createCompare(loc, cir::CmpOpKind::eq, strippedSrcVPtr,
                                    expectedVPtrAsPtr);
  } else {
    mlir::Value srcVPtr = cgf.getVTablePtr(loc, src, srcDecl);
    success =
        builder.createCompare(loc, cir::CmpOpKind::eq, srcVPtr, expectedVPtr);
  }

  auto emitCastResult = [&] {
    if (offset->isZero())
      return builder.createBitcast(src.getPointer(), destCIRTy);

    auto srcPtrTy = mlir::cast<cir::PointerType>(src.getPointer().getType());
    mlir::Type u8PtrTy =
        builder.getPointerTo(builder.getUInt8Ty(), srcPtrTy.getAddrSpace());

    mlir::Value strideToApply =
        builder.getConstInt(loc, builder.getUInt64Ty(), -offset->getQuantity());
    mlir::Value srcU8Ptr = builder.createBitcast(src.getPointer(), u8PtrTy);
    mlir::Value resultU8Ptr = cir::PtrStrideOp::create(builder, loc, u8PtrTy,
                                                       srcU8Ptr, strideToApply,
        cir::GEPNoWrapFlags::inbounds);
    return builder.createBitcast(resultU8Ptr, destCIRTy);
  };

  bool performPostCastAuthentication =
      cgf.getLangOpts().PointerAuthCalls &&
      (requiresCastToPrimaryBase || !offset->isZero());

  if (isRefCast) {
    mlir::Value failed = builder.createNot(success);
    cir::IfOp::create(builder, loc, failed, /*withElseRegion=*/false,
                      [&](mlir::OpBuilder &, mlir::Location) {
                        emitCallToBadCast(cgf, loc);
                      });

    mlir::Value castResult = emitCastResult();
    if (performPostCastAuthentication) {
      cir::IfOp::create(builder, loc, success, /*withElseRegion=*/false,
                        [&](mlir::OpBuilder &, mlir::Location) {
                          Address adjustedThis(castResult, cgf.getPointerAlign());
                          (void)cgf.getVTablePtr(
                              loc, adjustedThis, destDecl,
                              CIRGenFunction::VTableAuthMode::MustTrap);
                        });
    }
    return castResult;
  }

  return cir::TernaryOp::create(
             builder, loc, success,
             [&](mlir::OpBuilder &, mlir::Location) {
               auto result = emitCastResult();
               if (performPostCastAuthentication) {
                 Address adjustedThis(result, cgf.getPointerAlign());
                 (void)cgf.getVTablePtr(
                     loc, adjustedThis, destDecl,
                     CIRGenFunction::VTableAuthMode::MustTrap);
               }
               builder.createYield(loc, result);
             },
             [&](mlir::OpBuilder &, mlir::Location) {
               mlir::Value nullPtrValue = builder.getNullPtr(destCIRTy, loc);
               builder.createYield(loc, nullPtrValue);
             })
      .getResult();
}

static cir::DynamicCastInfoAttr emitDynamicCastInfo(CIRGenFunction &cgf,
                                                    mlir::Location loc,
                                                    QualType srcRecordTy,
                                                    QualType destRecordTy) {
  // Strip cv-qualifiers before RTTI mangling — mangleCXXRTTI asserts
  // !Ty.hasQualifiers().
  auto srcRtti = mlir::cast<cir::GlobalViewAttr>(
      cgf.cgm.getAddrOfRTTIDescriptor(loc,
                                       srcRecordTy.getUnqualifiedType()));
  auto destRtti = mlir::cast<cir::GlobalViewAttr>(
      cgf.cgm.getAddrOfRTTIDescriptor(loc,
                                       destRecordTy.getUnqualifiedType()));

  cir::FuncOp runtimeFuncOp = getItaniumDynamicCastFn(cgf);
  cir::FuncOp badCastFuncOp = getBadCastFn(cgf);
  auto runtimeFuncRef = mlir::FlatSymbolRefAttr::get(runtimeFuncOp);
  auto badCastFuncRef = mlir::FlatSymbolRefAttr::get(badCastFuncOp);

  const CXXRecordDecl *srcDecl = srcRecordTy->getAsCXXRecordDecl();
  const CXXRecordDecl *destDecl = destRecordTy->getAsCXXRecordDecl();
  CharUnits offsetHint = computeOffsetHint(cgf.getContext(), srcDecl, destDecl);

  mlir::Type ptrdiffTy = cgf.convertType(cgf.getContext().getPointerDiffType());
  auto offsetHintAttr = cir::IntAttr::get(ptrdiffTy, offsetHint.getQuantity());

  return cir::DynamicCastInfoAttr::get(srcRtti, destRtti, runtimeFuncRef,
                                       badCastFuncRef, offsetHintAttr);
}

mlir::Value CIRGenItaniumCXXABI::emitDynamicCast(CIRGenFunction &cgf,
                                                 mlir::Location loc,
                                                 QualType srcRecordTy,
                                                 QualType destRecordTy,
                                                 cir::PointerType destCIRTy,
                                                 bool isRefCast, Address src) {
  bool isCastToVoid = destRecordTy.isNull();
  assert((!isCastToVoid || !isRefCast) && "cannot cast to void reference");

  if (isCastToVoid)
    return emitDynamicCastToVoid(cgf, loc, srcRecordTy, src).getPointer();

  // If the destination is effectively final, the cast succeeds if and only
  // if the dynamic type of the pointer is exactly the destination type.
  if (destRecordTy->getAsCXXRecordDecl()->isEffectivelyFinal() &&
      cgf.cgm.getCodeGenOpts().OptimizationLevel > 0) {
    CIRGenBuilderTy &builder = cgf.getBuilder();
    // If this isn't a reference cast, check the pointer to see if it's null.
    if (!isRefCast) {
      mlir::Value srcPtrIsNull = builder.createPtrIsNull(src.getPointer());
      return cir::TernaryOp::create(
                 builder, loc, srcPtrIsNull,
                 [&](mlir::OpBuilder, mlir::Location) {
                   builder.createYield(
                       loc, builder.getNullPtr(destCIRTy, loc).getResult());
                 },
                 [&](mlir::OpBuilder &, mlir::Location) {
                   mlir::Value exactCast = emitExactDynamicCast(
                       *this, cgf, loc, srcRecordTy, destRecordTy, destCIRTy,
                       isRefCast, src);
                   builder.createYield(loc, exactCast);
                 })
          .getResult();
    }

    return emitExactDynamicCast(*this, cgf, loc, srcRecordTy, destRecordTy,
                                destCIRTy, isRefCast, src);
  }

  // Under the vtable-pointer authentication ABI (arm64e), authenticate the
  // source object's vptr before handing the pointer to __dynamic_cast. This is
  // a no-op authenticating load that traps early on a forged/corrupt vptr; on
  // targets without pointer authentication it is elided. Mirrors OGCG's
  // ItaniumCXXABI::emitDynamicCastCall MustTrap load. __dynamic_cast itself is
  // unchanged.
  if (cgf.cgm.getCodeGenOpts().PointerAuth.CXXVTablePointers) {
    CIRGenBuilderTy &builder = cgf.getBuilder();
    const CXXRecordDecl *srcDecl = srcRecordTy->getAsCXXRecordDecl();
    if (isRefCast) {
      // A reference source is never null: authenticate unconditionally.
      (void)cgf.getVTablePtr(loc, src, srcDecl,
                             CIRGenFunction::VTableAuthMode::MustTrap);
    } else {
      // A pointer source may be null. __dynamic_cast returns null for a null
      // input without touching the vptr (the null check is materialized when
      // the cir.dyn_cast op is lowered), so guard the authenticating load with
      // the same null check to avoid dereferencing null (mirrors OGCG's
      // dynamic_cast.notnull block).
      mlir::Value srcIsNotNull = builder.createPtrToBoolCast(src.getPointer());
      cir::IfOp::create(builder, loc, srcIsNotNull, /*withElseRegion=*/false,
                        [&](mlir::OpBuilder &, mlir::Location) {
                          (void)cgf.getVTablePtr(
                              loc, src, srcDecl,
                              CIRGenFunction::VTableAuthMode::MustTrap);
                          builder.createYield(loc);
                        });
    }
  }

  cir::DynamicCastInfoAttr castInfo =
      emitDynamicCastInfo(cgf, loc, srcRecordTy, destRecordTy);
  return cgf.getBuilder().createDynCast(loc, src.getPointer(), destCIRTy,
                                        isRefCast, castInfo);
}

/// The Itanium ABI always places an offset to the complete object
/// at entry -2 in the vtable.
void CIRGenItaniumCXXABI::emitVirtualObjectDelete(
    CIRGenFunction &cgf, const CXXDeleteExpr *delExpr, Address ptr,
    QualType elementType, const CXXDestructorDecl *dtor) {
  bool useGlobalDelete = delExpr->isGlobalDelete();
  if (useGlobalDelete) {
    // Derive the complete-object pointer, which is what we need
    // to pass to the deallocation function.

    auto &builder = cgf.getBuilder();
    mlir::Location loc = cgf.getLoc(delExpr->getSourceRange());

    // Grab the vtable pointer as an intptr_t*.
    auto *classDecl = elementType->castAsCXXRecordDecl();
    mlir::Value vtable = cgf.getVTablePtr(loc, ptr, classDecl);

    // Track back to entry -2 and pull out the offset there.
    QualType ptrDiffQT = cgf.getContext().getPointerDiffType();
    mlir::Type ptrDiffTy = cgf.convertType(ptrDiffQT);
    auto ptrDiffPtrTy =
        builder.getPointerTo(ptrDiffTy, cgf.cgm.getDefaultGlobalsAddressSpace());

    auto vtableCast = builder.createBitcast(vtable, ptrDiffPtrTy);
    auto minusTwo = builder.getSInt64(-2, loc);
    auto offsetPtr = cir::PtrStrideOp::create(builder, loc, ptrDiffPtrTy,
                                              vtableCast, minusTwo,
                                              cir::GEPNoWrapFlags::inbounds);
    Address offsetAddr(offsetPtr, cgf.getPointerAlign());
    mlir::Value offset = builder.createLoad(loc, offsetAddr);

    // Apply the offset.
    auto i8PtrTy = builder.getUInt8PtrTy();
    auto rawPtr = builder.createBitcast(ptr.getPointer(), i8PtrTy);
    auto completePtr =
        cir::PtrStrideOp::create(builder, loc, i8PtrTy, rawPtr, offset,
            cir::GEPNoWrapFlags::inbounds);

    // If we're supposed to call the global delete, make sure we do so
    // even if the destructor throws.
    cgf.pushCallObjectDeleteCleanup(delExpr->getOperatorDelete(), completePtr,
                                    elementType);
  }

  CXXDtorType dtorType = useGlobalDelete ? Dtor_Complete : Dtor_Deleting;
  emitVirtualDestructorCall(cgf, dtor, dtorType, ptr, delExpr);

  if (useGlobalDelete)
    cgf.popCleanupBlock();
}

/************************** Array allocation cookies **************************/

CharUnits CIRGenItaniumCXXABI::getArrayCookieSizeImpl(QualType elementType) {
  // The array cookie is a size_t; pad that up to the element alignment.
  // The cookie is actually right-justified in that space.
  return std::max(
      cgm.getSizeSize(),
      cgm.getASTContext().getPreferredTypeAlignInChars(elementType));
}

Address CIRGenItaniumCXXABI::initializeArrayCookie(CIRGenFunction &cgf,
                                                   Address newPtr,
                                                   mlir::Value numElements,
                                                   const CXXNewExpr *e,
                                                   QualType elementType) {
  assert(requiresArrayCookie(e));
  auto &builder = cgf.getBuilder();
  ASTContext &ctx = cgm.getASTContext();
  CharUnits sizeSize = cgf.getSizeSize();
  mlir::Location loc = cgf.getLoc(e->getSourceRange());

  // The size of the cookie.
  CharUnits cookieSize =
      std::max(sizeSize, ctx.getPreferredTypeAlignInChars(elementType));
  assert(cookieSize == getArrayCookieSizeImpl(elementType));

  mlir::Type u8Ty = cgf.getBuilder().getUIntNTy(8);
  mlir::Value baseBytePtr =
      cgf.getBuilder().createPtrBitcast(newPtr.getPointer(), u8Ty);

  // Compute an offset to the cookie.
  CharUnits cookieOffset = cookieSize - sizeSize;
  mlir::Value cookiePtrValue = baseBytePtr;
  if (!cookieOffset.isZero()) {
    mlir::Value offsetOp = cgf.getBuilder().getSignedInt(
        loc, cookieOffset.getQuantity(), /*width=*/32);
    cookiePtrValue =
        cgf.getBuilder().createPtrStride(loc, cookiePtrValue, offsetOp);
  }

  CharUnits baseAlignment = newPtr.getAlignment();
  CharUnits cookiePtrAlignment = baseAlignment.alignmentAtOffset(cookieOffset);
  Address cookiePtr(cookiePtrValue, u8Ty, cookiePtrAlignment);

  // Write the number of elements into the appropriate slot.
  Address numElementsPtr =
      cookiePtr.withElementType(builder, cgf.sizeTy);
  cir::StoreOp store = builder.createStore(loc, numElements, numElementsPtr);

  // Handle the array cookie specially in ASan.
  if (cgm.getLangOpts().Sanitize.has(SanitizerKind::Address) &&
      !newPtr.getAddressSpace() &&
      (e->getOperatorNew()->isReplaceableGlobalAllocationFunction() ||
       cgm.getCodeGenOpts().SanitizeAddressPoisonCustomArrayCookie)) {
    markNoSanitize(store);
    auto poisonCookieTy =
        builder.getFuncType({numElementsPtr.getType()}, builder.getVoidTy());
    cir::FuncOp poisonCookie = cgm.createRuntimeFunction(
        poisonCookieTy, "__asan_poison_cxx_array_cookie");
    cir::CallOp call = builder.createCallOp(
        loc, poisonCookie, mlir::ValueRange{numElementsPtr.getPointer()});
    markNoSanitize(call);
  }

  // Finally, compute a pointer to the actual data buffer by skipping
  // over the cookie completely.
  mlir::Value dataOffset =
      cgf.getBuilder().getSignedInt(loc, cookieSize.getQuantity(),
                                    /*width=*/32);
  mlir::Value dataPtr =
      cgf.getBuilder().createPtrStride(loc, baseBytePtr, dataOffset);
  mlir::Value finalPtr =
      cgf.getBuilder().createPtrBitcast(dataPtr, newPtr.getElementType());
  CharUnits finalAlignment = baseAlignment.alignmentAtOffset(cookieSize);
  return Address(finalPtr, newPtr.getElementType(), finalAlignment);
}

mlir::Value CIRGenItaniumCXXABI::readArrayCookieImpl(CIRGenFunction &cgf,
                                                     Address allocPtr,
                                                     CharUnits cookieSize) {
  auto &builder = cgf.getBuilder();
  mlir::Location loc = cgf.curFn->getLoc();

  // The element count is right-justified in the cookie.
  Address numElementsPtr = allocPtr;
  CharUnits numElementsOffset = cookieSize - cgf.getSizeSize();
  if (!numElementsOffset.isZero()) {
    mlir::Value offset =
        builder.getSignedInt(loc, numElementsOffset.getQuantity(),
                             /*width=*/32);
    mlir::Value cookieFieldPtr =
        builder.createPtrStride(loc, allocPtr.getPointer(), offset);
    numElementsPtr = Address(cookieFieldPtr, allocPtr.getElementType(),
                             allocPtr.getAlignment().alignmentAtOffset(
                                 numElementsOffset),
                             allocPtr.isKnownNonNull());
  }

  numElementsPtr = numElementsPtr.withElementType(builder, cgf.sizeTy);
  if (!cgm.getLangOpts().Sanitize.has(SanitizerKind::Address) ||
      numElementsPtr.getAddressSpace())
    return builder.createLoad(loc, numElementsPtr);

  auto cookiePtrTy = builder.getVoidPtrTy(numElementsPtr.getAddressSpace());
  auto loadCookieTy = builder.getFuncType({cookiePtrTy}, cgf.sizeTy);
  cir::FuncOp loadCookie =
      cgm.createRuntimeFunction(loadCookieTy, "__asan_load_cxx_array_cookie");
  mlir::Value cookiePtr = numElementsPtr.getPointer();
  if (cookiePtr.getType() != cookiePtrTy)
    cookiePtr = builder.createBitcast(loc, cookiePtr, cookiePtrTy);
  cir::CallOp call = builder.createCallOp(loc, loadCookie,
                                          mlir::ValueRange{cookiePtr});
  markNoSanitize(call);
  return call.getResult();
}

CharUnits CIRGenARMCXXABI::getArrayCookieSizeImpl(QualType elementType) {
  // ARM says that the cookie is always:
  //   struct array_cookie {
  //     std::size_t element_size; // element_size != 0
  //     std::size_t element_count;
  //   };
  // But the base ABI doesn't give anything an alignment greater than
  // 8, so we can dismiss this as typical ABI-author blindness to
  // actual language complexity and round up to the element alignment.
  return std::max(CharUnits::fromQuantity(2 * cgm.SizeSizeInBytes),
                  cgm.getASTContext().getTypeAlignInChars(elementType));
}

Address CIRGenARMCXXABI::initializeArrayCookie(CIRGenFunction &cgf,
                                               Address newPtr,
                                               mlir::Value numElements,
                                               const CXXNewExpr *e,
                                               QualType elementType) {
  assert(requiresArrayCookie(e));

  // The cookie is always at the start of the buffer.
  auto cookiePtr =
      cgf.getBuilder().createPtrBitcast(newPtr.getPointer(), cgf.sizeTy);
  Address cookie = Address(cookiePtr, cgf.sizeTy, newPtr.getAlignment());

  ASTContext &ctx = cgm.getASTContext();
  mlir::Location loc = cgf.getLoc(e->getSourceRange());

  // The first element is the element size.
  mlir::Value elementSize = cgf.getBuilder().getConstInt(
      loc, cgf.sizeTy, ctx.getTypeSizeInChars(elementType).getQuantity());
  cgf.getBuilder().createStore(loc, elementSize, cookie);

  // The second element is the element count.
  auto offsetOp = cgf.getBuilder().getSignedInt(loc, 1, /*width=*/32);
  auto dataPtr =
      cgf.getBuilder().createPtrStride(loc, cookie.getPointer(), offsetOp);
  cookie = Address(dataPtr, cgf.sizeTy, newPtr.getAlignment());
  cgf.getBuilder().createStore(loc, numElements, cookie);

  // Finally, compute a pointer to the actual data buffer by skipping
  // over the cookie completely.
  CharUnits cookieSize = CIRGenARMCXXABI::getArrayCookieSizeImpl(elementType);
  offsetOp = cgf.getBuilder().getSignedInt(loc, cookieSize.getQuantity(),
                                           /*width=*/32);
  auto castOp = cgf.getBuilder().createPtrBitcast(
      newPtr.getPointer(), cgf.getBuilder().getUIntNTy(8));
  dataPtr = cgf.getBuilder().createPtrStride(loc, castOp, offsetOp);
  return Address(dataPtr, cgf.getBuilder().getUIntNTy(8),
                 newPtr.getAlignment());
}

mlir::Value CIRGenARMCXXABI::readArrayCookieImpl(CIRGenFunction &cgf,
                                                 Address allocPtr,
                                                 CharUnits cookieSize) {
  auto &builder = cgf.getBuilder();
  mlir::Location loc = cgf.curFn->getLoc();
  mlir::Value countOffset =
      builder.getSignedInt(loc, cgf.getSizeSize().getQuantity(), /*width=*/32);
  mlir::Value countPtr =
      builder.createPtrStride(loc, allocPtr.getPointer(), countOffset);
  Address numElementsPtr(countPtr, allocPtr.getElementType(),
                         allocPtr.getAlignment().alignmentAtOffset(
                             cgf.getSizeSize()),
                         allocPtr.isKnownNonNull());
  numElementsPtr = numElementsPtr.withElementType(builder, cgf.sizeTy);
  return builder.createLoad(loc, numElementsPtr);
}

namespace {
/// From traditional LLVM, useful info for LLVM lowering support:
/// A cleanup to call __cxa_end_catch.  In many cases, the caught
/// exception type lets us state definitively that the thrown exception
/// type does not have a destructor.  In particular:
///   - Catch-alls tell us nothing, so we have to conservatively
///     assume that the thrown exception might have a destructor.
///   - Catches by reference behave according to their base types.
///   - Catches of non-record types will only trigger for exceptions
///     of non-record types, which never have destructors.
///   - Catches of record types can trigger for arbitrary subclasses
///     of the caught type, so we have to assume the actual thrown
///     exception type might have a throwing destructor, even if the
///     caught type's destructor is trivial or nothrow.
struct CallEndCatch final : EHScopeStack::Cleanup {
  CallEndCatch(bool mightThrow, bool hasEnclosingNormalCleanup,
               bool hasEnclosingEH)
      : mightThrow(mightThrow),
        hasEnclosingNormalCleanup(hasEnclosingNormalCleanup),
        hasEnclosingEH(hasEnclosingEH) {}
  bool mightThrow;
  bool hasEnclosingNormalCleanup;
  bool hasEnclosingEH;

  void emit(CIRGenFunction &cgf, Flags flags) override {
    // During early exit (return/break/continue inside catch), emit a
    // CatchParamOp::End marker so __cxa_end_catch runs BEFORE any
    // outer-scope cleanup calls (e.g., RAII destructors declared outside
    // the try block). This matches OGCG's ordering where __cxa_end_catch
    // is called first, then outer scope destructors run. FlattenCFG will
    // skip inserting a duplicate end op for this exit path.
    if (flags.isForEarlyExit()) {
      auto endOp = cir::CatchParamOp::create(
          cgf.getBuilder(), *cgf.currSrcLoc, mlir::Type{}, nullptr,
          cir::CatchParamKindAttr::get(cgf.getBuilder().getContext(),
                                       cir::CatchParamKind::End));
      if (!mightThrow)
        endOp->setAttr("cir.end_catch_nothrow",
                        mlir::UnitAttr::get(cgf.getBuilder().getContext()));
      else if (hasEnclosingEH)
        endOp->setAttr("cir.end_catch_enclosing_eh",
                        mlir::UnitAttr::get(cgf.getBuilder().getContext()));
      else if (hasEnclosingNormalCleanup)
        endOp->setAttr("cir.end_catch_normal_cleanup",
                        mlir::UnitAttr::get(cgf.getBuilder().getContext()));
      return;
    }

    // When emitting for EH cleanup (per-call cleanup region, e.g., throw
    // from inside a catch handler), emit a CatchParamOp::End marker so
    // __cxa_end_catch runs BEFORE outer RAII destructors on the unwind
    // path. This matches OGCG's ordering where __cxa_end_catch is called
    // first, then outer scope destructors run.
    if (flags.isForEHCleanup()) {
      auto endOp = cir::CatchParamOp::create(
          cgf.getBuilder(), *cgf.currSrcLoc, mlir::Type{}, nullptr,
          cir::CatchParamKindAttr::get(cgf.getBuilder().getContext(),
                                       cir::CatchParamKind::End));
      if (!mightThrow)
        endOp->setAttr("cir.end_catch_nothrow",
                        mlir::UnitAttr::get(cgf.getBuilder().getContext()));
      endOp->setAttr("cir.end_catch_eh_cleanup",
                      mlir::UnitAttr::get(cgf.getBuilder().getContext()));
      return;
    }

    // In coroutines, co_return inside a catch handler creates a retBlock
    // with cir.yield (via emitReturn at non-zero depth). The current block
    // is the dead continuation after co_return's BrOp to the retBlock.
    // Emitting a second yield here would violate FlattenCFG's invariant
    // that each catch handler region has exactly one cir.yield. Skip the
    // yield — the retBlock's yield serves as the handler's exit point,
    // and FlattenCFG will insert CatchParamOp::End before it.
    auto *curBlock = cgf.getBuilder().getBlock();
    if (curBlock && curBlock->hasNoPredecessors() &&
        !curBlock->isEntryBlock()) {
      if (auto fn = dyn_cast<cir::FuncOp>(cgf.curFn))
        if (fn.getCoroutine())
          return;
    }

    if (!mightThrow) {
      // Traditional LLVM codegen would emit a call to __cxa_end_catch
      // here. For CIR, just let it pass since the cleanup is going
      // to be emitted on a later pass when lowering the catch region.
      // CGF.EmitNounwindRuntimeCall(getEndCatchFn(CGF.CGM));
      cir::YieldOp::create(cgf.getBuilder(), *cgf.currSrcLoc);
      return;
    }

    // Traditional LLVM codegen would emit a call to __cxa_end_catch
    // here. For CIR, just let it pass since the cleanup is going
    // to be emitted on a later pass when lowering the catch region.
    // CGF.EmitRuntimeCallOrTryCall(getEndCatchFn(CGF.CGM));
    if (!curBlock->mightHaveTerminator())
      cir::YieldOp::create(cgf.getBuilder(), *cgf.currSrcLoc);
  }
};
} // namespace

static mlir::Value callBeginCatch(CIRGenFunction &cgf, mlir::Location loc,
                                  mlir::Type paramTy, bool endMightThrow) {
  bool hasEnclosingNormalCleanup = cgf.ehStack.hasNormalCleanups();
  // Check if there's an enclosing EH scope (outer try/catch) that can
  // catch exceptions thrown by __cxa_end_catch. This happens when the
  // caught exception object has a throwing destructor and there's an
  // outer catch handler.
  bool hasEnclosingEH = cgf.isCatchOrCleanupRequired();
  auto catchParam = cir::CatchParamOp::create(
      cgf.getBuilder(), loc, paramTy,
      /*exception_ptr=*/mlir::Value{}, /*kind=*/cir::CatchParamKindAttr{});

  bool mightThrow =
      endMightThrow && !cgf.cgm.getLangOpts().AssumeNothrowExceptionDtor;
  cgf.ehStack.pushCleanup<CallEndCatch>(NormalAndEHCleanup, mightThrow,
                                        hasEnclosingNormalCleanup,
                                        hasEnclosingEH);

  // Mark the catch_param begin op so that FlattenCFG can propagate this to
  // the corresponding catch_param end ops. When nothrow, __cxa_end_catch
  // is emitted as a plain call (not invoke-to-terminate). This matches OGCG
  // which only uses invoke for record-type/catch-all exceptions.
  if (!mightThrow)
    catchParam->setAttr("cir.end_catch_nothrow",
                        mlir::UnitAttr::get(cgf.getBuilder().getContext()));
  else if (hasEnclosingNormalCleanup)
    catchParam->setAttr("cir.end_catch_normal_cleanup",
                        mlir::UnitAttr::get(cgf.getBuilder().getContext()));

  return catchParam.getParam();
}

/// A "special initializer" callback for initializing a catch
/// parameter during catch initialization.
static void initCatchParam(CIRGenFunction &cgf, const VarDecl &catchParam,
                           Address paramAddr, SourceLocation loc) {
  CanQualType catchType =
      cgf.cgm.getASTContext().getCanonicalType(catchParam.getType());
  mlir::Type cirCatchTy = cgf.convertTypeForMem(catchType);
  mlir::Location catchLoc = cgf.getLoc(loc);
  CIRGenFunction::SourceLocRAIIObject catchLocRAII{cgf, catchLoc};

  // If we're catching by reference, we can just cast the object
  // pointer to the appropriate pointer.
  if (isa<ReferenceType>(catchType)) {
    QualType caughtType = cast<ReferenceType>(catchType)->getPointeeType();
    bool endCatchMightThrow = caughtType->isRecordType();

    // __cxa_begin_catch returns the adjusted object pointer.
    mlir::Value adjustedExn =
        callBeginCatch(cgf, catchLoc, cirCatchTy, endCatchMightThrow);

    // We have no way to tell the personality function that we're
    // catching by reference, so if we're catching a pointer,
    // __cxa_begin_catch will actually return that pointer by value.
    if (isa<PointerType>(caughtType)) {
      // We have no way to tell the personality function that we're
      // catching by reference, so __cxa_begin_catch returns the pointer
      // by value. Store it into a temporary alloca and bind the reference
      // to that temporary.
      mlir::Type caughtTy = cgf.convertTypeForMem(caughtType);
      Address exnPtrTmp = cgf.createTempAlloca(
          caughtTy, cgf.getPointerAlign(), catchLoc, "exn.byref.tmp");
      cgf.getBuilder().createStore(catchLoc, adjustedExn, exnPtrTmp);
      adjustedExn = exnPtrTmp.getPointer();
    }

    // Store the adjusted pointer into the reference variable.
    cgf.getBuilder().createStore(catchLoc, adjustedExn, paramAddr);
    return;
  }

  // Scalars and complexes.
  cir::TypeEvaluationKind tek = cgf.getEvaluationKind(catchType);
  if (tek != cir::TEK_Aggregate) {
    // Notes for LLVM lowering:
    // If the catch type is a pointer type, __cxa_begin_catch returns
    // the pointer by value.
    if (catchType->hasPointerRepresentation()) {
      mlir::Value catchParam =
          callBeginCatch(cgf, catchLoc, cirCatchTy, /*endMightThrow=*/false);
      if (catchParam.getType() != paramAddr.getElementType())
        catchParam = cir::CastOp::create(cgf.getBuilder(), catchLoc,
                                         paramAddr.getElementType(),
                                         cir::CastKind::bitcast, catchParam);

      switch (catchType.getQualifiers().getObjCLifetime()) {
      case Qualifiers::OCL_Strong:
        catchParam = cgf.emitARCRetainNonBlock(catchParam, catchLoc);
        [[fallthrough]];
      case Qualifiers::OCL_None:
      case Qualifiers::OCL_ExplicitNone:
      case Qualifiers::OCL_Autoreleasing:
        cgf.getBuilder().createStore(catchLoc, catchParam, paramAddr);
        return;

      case Qualifiers::OCL_Weak:
        cgf.emitARCInitWeak(paramAddr, catchParam);
        return;
      }

      llvm_unreachable("bad ownership qualifier!");
    }

    // Otherwise, it returns a pointer into the exception object.
    CIRGenFunction::ApplySourceAtomGroup catchMaterializationAtom(cgf);
    mlir::Type cirCatchTy = cgf.convertTypeForMem(catchType);
    mlir::Value catchParam =
        callBeginCatch(cgf, catchLoc, cgf.getBuilder().getPointerTo(cirCatchTy),
                       /*endMightThrow=*/false);
    LValue srcLV = cgf.makeNaturalAlignAddrLValue(catchParam, catchType);
    LValue destLV = cgf.makeAddrLValue(paramAddr, catchType);
    switch (tek) {
    case cir::TEK_Complex: {
      mlir::Value load = cgf.emitLoadOfComplex(srcLV, loc);
      cgf.emitStoreOfComplex(cgf.getLoc(loc), load, destLV, /*isInit=*/true);
      return;
    }
    case cir::TEK_Scalar: {
      mlir::Value exnLoad = cgf.emitLoadOfScalar(srcLV, loc);
      cgf.emitStoreOfScalar(exnLoad, destLV, /*isInit=*/true);
      return;
    }
    case cir::TEK_Aggregate:
      llvm_unreachable("evaluation kind filtered out!");
    }

    llvm_unreachable("bad evaluation kind");
  }

  assert(isa<RecordType>(catchType) && "unexpected catch type!");
  auto *catchRD = catchType->getAsCXXRecordDecl();
  CharUnits caughtExnAlignment = cgf.cgm.getClassPointerAlignment(catchRD);

  // Check for a copy expression. If we don't have a copy expression,
  // that means a trivial copy is okay.
  const Expr *copyExpr = catchParam.getInit();
  if (!copyExpr) {
    mlir::Value rawAdjustedExn = callBeginCatch(
        cgf, catchLoc, cgf.getBuilder().getPointerTo(cirCatchTy),
        /*endMightThrow=*/true);
    Address adjustedExn(rawAdjustedExn, cirCatchTy, caughtExnAlignment);
    LValue dest = cgf.makeAddrLValue(paramAddr, catchType);
    LValue src = cgf.makeAddrLValue(adjustedExn, catchType);
    cgf.emitAggregateCopy(dest, src, catchType, AggValueSlot::DoesNotOverlap);
    return;
  }

  // We have to call __cxa_get_exception_ptr to get the adjusted
  // pointer before copying.
  auto catchParamOp = cir::CatchParamOp::create(
      cgf.getBuilder(), catchLoc,
      cgf.getBuilder().getPointerTo(cirCatchTy),
      /*exception_ptr=*/mlir::Value{}, /*kind=*/cir::CatchParamKindAttr{});
  mlir::Value rawAdjustedExn = catchParamOp.getParam();

  Address adjustedExn(rawAdjustedExn, cirCatchTy, caughtExnAlignment);

  // The copy expression is defined in terms of an OpaqueValueExpr.
  // Find it and map it to the adjusted expression.
  CIRGenFunction::OpaqueValueMapping opaque(
      cgf, OpaqueValueExpr::findInCopyConstruct(copyExpr),
      cgf.makeAddrLValue(adjustedExn, catchParam.getType()));

  // Call the copy ctor in a terminate scope. Per C++ [except.handle]/7,
  // if the copy/move constructor of the catch variable throws during
  // initialization, std::terminate() must be called.
  cgf.ehStack.pushTerminate();

  // Perform the copy construction.
  cgf.emitAggExpr(
      copyExpr, AggValueSlot::forAddr(
                    paramAddr, Qualifiers(), AggValueSlot::IsNotDestructed,
                    AggValueSlot::IsNotAliased, AggValueSlot::DoesNotOverlap));

  // Leave the terminate scope.
  cgf.ehStack.popTerminate();

  // Undo the opaque value mapping.
  opaque.pop();

  // Finally, call __cxa_begin_catch to mark the exception as handled.
  callBeginCatch(cgf, catchLoc, cgf.getBuilder().getVoidPtrTy(),
                 /*endMightThrow=*/true);
}

/// Begins a catch statement by initializing the catch variable and
/// calling __cxa_begin_catch.
void CIRGenItaniumCXXABI::emitBeginCatch(CIRGenFunction &cgf,
                                         const CXXCatchStmt *catchStmt) {
  // We have to be very careful with the ordering of cleanups here:
  //   C++ [except.throw]p4:
  //     The destruction [of the exception temporary] occurs
  //     immediately after the destruction of the object declared in
  //     the exception-declaration in the handler.
  //
  // So the precise ordering is:
  //   1.  Construct catch variable.
  //   2.  __cxa_begin_catch
  //   3.  Enter __cxa_end_catch cleanup
  //   4.  Enter dtor cleanup
  //
  // We do this by using a slightly abnormal initialization process.
  // Delegation sequence:
  //   - ExitCXXTryStmt opens a RunCleanupsScope
  //     - EmitAutoVarAlloca creates the variable and debug info
  //       - InitCatchParam initializes the variable from the exception
  //       - CallBeginCatch calls __cxa_begin_catch
  //       - CallBeginCatch enters the __cxa_end_catch cleanup
  //     - EmitAutoVarCleanups enters the variable destructor cleanup
  //   - EmitCXXTryStmt emits the code for the catch body
  //   - EmitCXXTryStmt close the RunCleanupsScope

  VarDecl *catchParam = catchStmt->getExceptionDecl();
  if (!catchParam) {
    callBeginCatch(cgf, cgf.getLoc(catchStmt->getCatchLoc()),
                   cgf.getBuilder().getVoidPtrTy(),
                   /*endMightThrow=*/true);
    return;
  }

  auto getCatchParamAllocaIP = [&]() {
    cir::CIRBaseBuilderTy::InsertPoint currIns =
        cgf.getBuilder().saveInsertionPoint();
    mlir::Operation *currParent = currIns.getBlock()->getParentOp();

    mlir::Block *insertBlock = nullptr;
    if (auto scopeOp = currParent->getParentOfType<cir::ScopeOp>()) {
      insertBlock = &scopeOp.getScopeRegion().getBlocks().back();
    } else if (auto fnOp = currParent->getParentOfType<cir::FuncOp>()) {
      insertBlock = &fnOp.getRegion().getBlocks().back();
    } else {
      llvm_unreachable("unknown outermost scope-like parent");
    }
    return cgf.getBuilder().getBestAllocaInsertPoint(insertBlock);
  };

  // Emit the local. Make sure the alloca's superseed the current scope, since
  // these are going to be consumed by `cir.catch`, which is not within the
  // current scope.

  CIRGenFunction::AutoVarEmission var =
      cgf.emitAutoVarAlloca(*catchParam, getCatchParamAllocaIP());
  initCatchParam(cgf, *catchParam, var.getObjectAddress(cgf),
                 catchStmt->getBeginLoc());
  cgf.emitAutoVarCleanups(var);
}

static mlir::Value performTypeAdjustment(CIRGenFunction &cgf,
                                         Address initialPtr,
                                         const CXXRecordDecl *unadjustedClass,
                                         int64_t nonVirtualAdjustment,
                                         int64_t virtualAdjustment,
                                         bool isReturnAdjustment) {
  if (!nonVirtualAdjustment && !virtualAdjustment)
    return initialPtr.getPointer();

  auto &builder = cgf.getBuilder();
  auto loc = cgf.curFn->getLoc();
  auto initialPtrTy =
      mlir::cast<cir::PointerType>(initialPtr.getPointer().getType());
  auto objectI8PtrTy =
      builder.getPointerTo(builder.getUInt8Ty(), initialPtrTy.getAddrSpace());
  auto globalsAS = cgf.cgm.getDefaultGlobalsAddressSpace();
  auto globalsI8PtrTy = cgf.cgm.getDefaultGlobalsInt8PtrTy();
  mlir::Value v = builder.createBitcast(initialPtr.getPointer(), objectI8PtrTy);

  // In a base-to-derived cast, the non-virtual adjustment is applied first.
  if (nonVirtualAdjustment && !isReturnAdjustment) {
    auto offsetConst = builder.getSInt64(nonVirtualAdjustment, loc);
    v = cir::PtrStrideOp::create(builder, loc, objectI8PtrTy, v, offsetConst,
                                 cir::GEPNoWrapFlags::inbounds);
  }

  // Perform the virtual adjustment if we have one.
  mlir::Value resultPtr;
  if (virtualAdjustment) {
    // For the virtual adjustment, we need to use the original typed pointer
    // to load the vtable pointer, then do byte-level arithmetic.
    Address typedAddr = initialPtr;
    if (nonVirtualAdjustment && !isReturnAdjustment) {
      // If we already applied a non-virtual adjustment, we need to cast
      // the adjusted pointer back to the class type for getVTablePtr.
      auto adjustedTyped =
          builder.createBitcast(v, initialPtr.getPointer().getType());
      typedAddr = Address(adjustedTyped, initialPtr.getAlignment());
    }

    // Load the vtable pointer from the object.
    mlir::Value vtablePtr =
        cgf.getVTablePtr(loc, typedAddr, unadjustedClass);

    // Cast vptr to i8* for byte-level indexing into the vtable.
    auto vtablePtrAsI8 = builder.createBitcast(vtablePtr, globalsI8PtrTy);

    // Index into the vtable by virtualAdjustment bytes to find the offset.
    auto offsetConst = builder.getSInt64(virtualAdjustment, loc);
    auto offsetPtr = cir::PtrStrideOp::create(
        builder, loc, globalsI8PtrTy, vtablePtrAsI8, offsetConst,
        cir::GEPNoWrapFlags::inbounds);

    // Load the adjustment offset from the vtable slot.
    if (cgf.cgm.getItaniumVTableContext().isRelativeLayout()) {
      auto offsetPtrCast = builder.createBitcast(
          offsetPtr, builder.getPointerTo(builder.getSInt32Ty(), globalsAS));
      Address offsetAddr(offsetPtrCast, builder.getSInt32Ty(),
                         CharUnits::fromQuantity(4));
      mlir::Value offset = builder.createLoad(loc, offsetAddr);
      resultPtr = cir::PtrStrideOp::create(
          builder, loc, objectI8PtrTy, v, offset,
          cir::GEPNoWrapFlags::inbounds);
    } else {
      QualType ptrDiffQT = cgf.getContext().getPointerDiffType();
      mlir::Type ptrDiffTy = cgf.convertType(ptrDiffQT);
      auto offsetPtrCast = builder.createBitcast(
          offsetPtr, builder.getPointerTo(ptrDiffTy, globalsAS));
      Address offsetAddr(offsetPtrCast, cgf.getPointerAlign());
      mlir::Value offset = builder.createLoad(loc, offsetAddr);

      // Apply the loaded offset to the current pointer.
      resultPtr = cir::PtrStrideOp::create(
          builder, loc, objectI8PtrTy, v, offset,
          cir::GEPNoWrapFlags::inbounds);
    }
  } else {
    resultPtr = v;
  }

  // In a derived-to-base conversion, the non-virtual adjustment is
  // applied second.
  if (nonVirtualAdjustment && isReturnAdjustment) {
    auto offsetConst = builder.getSInt64(nonVirtualAdjustment, loc);
    resultPtr = cir::PtrStrideOp::create(builder, loc, objectI8PtrTy, resultPtr,
                                         offsetConst,
                                         cir::GEPNoWrapFlags::inbounds);
  }

  // Cast back to original pointer type
  return builder.createBitcast(resultPtr, initialPtr.getType());
}

mlir::Value CIRGenItaniumCXXABI::performThisAdjustment(
    CIRGenFunction &cgf, Address thisAddr, const CXXRecordDecl *unadjustedClass,
    const ThunkInfo &ti) {
  return performTypeAdjustment(cgf, thisAddr, unadjustedClass,
                               ti.This.NonVirtual,
                               ti.This.Virtual.Itanium.VCallOffsetOffset,
                               /*IsReturnAdjustment=*/false);
}

mlir::Value CIRGenItaniumCXXABI::performReturnAdjustment(
    CIRGenFunction &cgf, Address ret, const CXXRecordDecl *unadjustedClass,
    const ReturnAdjustment &ra) {
  return performTypeAdjustment(cgf, ret, unadjustedClass, ra.NonVirtual,
                               ra.Virtual.Itanium.VBaseOffsetOffset,
                               /*IsReturnAdjustment=*/true);
}

/// Check if a variable may need destruction (mirrors
/// CGCXXABI::mayNeedDestruction).
void CIRGenItaniumCXXABI::emitCXXThreadLocalInitFuncs() {
  // OGCG generates a __tls_init function with a __tls_guard guard variable
  // that calls all ordered thread-local initializers. Wrappers then call
  // __tls_init (or per-variable init functions) before returning the address.
  // CIR emits wrappers here and LoweringPrepare materializes __tls_init /
  // __tls_guard plus _ZTH aliases, then injects wrapper init calls.

  // Generate a TLS wrapper function for each TLS_Dynamic thread-local variable
  // defined in this translation unit.  The Itanium C++ ABI requires a wrapper
  // function named _ZTW<mangled_var_name> that returns a pointer to the
  // per-thread instance of the variable.  For constant-initialized variables
  // (the common case) the wrapper simply returns the thread-local address.

  // CIR may instantiate the Itanium ABI helper as a fallback for non-C++
  // Microsoft targets. In that mode the mangle context is Microsoft and
  // Itanium TLS wrappers are not applicable.
  if (cgm.getCXXABI().getMangleContext().getKind() !=
      MangleContext::MK_Itanium)
    return;

  auto &mangleCtx =
      cast<ItaniumMangleContext>(cgm.getCXXABI().getMangleContext());
  CIRGenBuilderTy &builder = cgm.getBuilder();

  for (const VarDecl *vd : cgm.getCXXThreadLocals()) {
    // Skip constant-initialized variables without definitions in this TU
    // that don't need a wrapper. OGCG uses a lazy ThreadWrappers map —
    // wrappers are only emitted for variables that were actually referenced
    // via the wrapper call path. Since CIR iterates all TLS variables, we
    // must explicitly skip those that don't need wrappers. Variables with
    // definitions always get wrappers (other TUs may need them).
    if (!vd->hasDefinition() &&
        !usesThreadWrapperFunction(vd, cgm.getASTContext()))
      continue;
    // Mangle the wrapper name: _ZTW<mangled_variable_name>
    SmallString<256> wrapperName;
    {
      llvm::raw_svector_ostream out(wrapperName);
      mangleCtx.mangleItaniumThreadLocalWrapper(vd, out);
    }

    // Skip if already emitted with a body (e.g., declared extern in a header).
    // But if a forward declaration exists (from emitGlobalVarDeclLValue), we
    // need to fill in the body.
    cir::FuncOp wrapperFn;
    if (auto *existing = cgm.getGlobalValue(wrapperName)) {
      auto existingFn = dyn_cast<cir::FuncOp>(existing);
      if (existingFn && !existingFn.isDeclaration())
        continue;
      if (existingFn)
        wrapperFn = existingFn;
    }

    // OGCG eagerly creates wrappers only for non-discardable definitions
    // (ItaniumCXXABI.cpp:3245-3251). Variables with discardable GVA linkage
    // (internal, available_externally, discardable_odr) only get wrappers if
    // they were actually referenced through the wrapper call path — i.e., a
    // forward declaration was already emitted from emitGlobalVarDeclLValue.
    // Without this filter, CIR emits spurious wrappers and _ZTH aliases for
    // internal-linkage variables that are never accessed (e.g., file-scope
    // `static thread_local int d = g()`).
    if (vd->hasDefinition() && !wrapperFn &&
        isDiscardableGVALinkage(
            cgm.getASTContext().GetGVALinkageForVariable(vd)))
      continue;

    // Find the GlobalOp for the variable.
    StringRef varName = cgm.getMangledName(vd);
    auto *globalOp = cgm.getGlobalValue(varName);
    if (!globalOp)
      continue;
    auto gv = dyn_cast<cir::GlobalOp>(globalOp);
    if (!gv)
      continue;

    // Build wrapper function type: () -> !cir.ptr<T>
    mlir::MLIRContext &mlirCtx = cgm.getMLIRContext();
    QualType wrapperResultQT =
        vd->getType()->isReferenceType() ? vd->getType()->getPointeeType()
                                         : vd->getType();
    mlir::Type wrapperResultTy =
        cgm.getTypes().convertTypeForMem(wrapperResultQT);
    mlir::Type ptrTy = builder.getPointerTo(wrapperResultTy);
    auto wrapperFuncTy = cir::FuncType::get({}, ptrTy);

    mlir::Location loc = cgm.getLoc(vd->getSourceRange());

    // Create the function declaration if it doesn't already exist.
    if (!wrapperFn) {
      wrapperFn = cgm.createCIRFunction(loc, wrapperName, wrapperFuncTy,
                                         nullptr);
    }

    // On Darwin, TLS_Dynamic wrappers use cxx_fast_tlscc calling convention
    // to enable the TLS access fastpath (reduces register saves/restores).
    // This matches OGCG's isThreadWrapperReplaceable check.
    bool isThreadWrapperReplaceable =
        vd->getTLSKind() == VarDecl::TLS_Dynamic &&
        cgm.getTarget().getTriple().isOSDarwin();
    if (isThreadWrapperReplaceable) {
      wrapperFn.setCallingConv(cir::CallingConv::CXXFastTLS);
      // Also add nounwind — these wrappers should never throw.
      mlir::NamedAttrList attrs;
      if (auto existingExtraAttrs = wrapperFn.getExtraAttrs())
        attrs = mlir::NamedAttrList{
            existingExtraAttrs->getElements().getValue()};
      auto noThrow = cir::NoThrowAttr::get(&cgm.getMLIRContext());
      attrs.set(noThrow.getMnemonic(), noThrow);
      wrapperFn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
          attrs.getDictionary(&cgm.getMLIRContext())));
    }

    // Compute wrapper linkage following OGCG's getThreadLocalWrapperLinkage:
    // internal-linkage variables get internal-linkage wrappers. Otherwise, the
    // wrapper is usually emitted weak_odr so every TU can provide a mergeable
    // copy, except for Darwin's replaceable TLS_Dynamic wrappers, which keep
    // the variable's non-ODR linkage when that linkage is already stronger
    // than linkonce/weak_odr.
    cir::GlobalLinkageKind varLinkage =
        cgm.getCIRLinkageVarDefinition(vd, /*isConstant=*/false);
    cir::GlobalLinkageKind wrapperLinkage;
    if (cir::isLocalLinkage(varLinkage)) {
      wrapperLinkage = varLinkage;
    } else if (vd->hasDefinition() && isThreadWrapperReplaceable &&
               varLinkage != cir::GlobalLinkageKind::LinkOnceODRLinkage &&
               varLinkage != cir::GlobalLinkageKind::WeakODRLinkage) {
      wrapperLinkage = varLinkage;
    } else {
      wrapperLinkage = cir::GlobalLinkageKind::WeakODRLinkage;
    }

    wrapperFn.setLinkageAttr(cir::GlobalLinkageKindAttr::get(
        &mlirCtx, wrapperLinkage));

    // Match OGCG: non-local wrappers are usually hidden so references resolve
    // at link time, but Darwin's replaceable wrappers keep default visibility
    // unless the wrapper linkage is itself ODR-mergeable or the source
    // variable already has hidden visibility.
    if (!cir::isLocalLinkage(wrapperLinkage) &&
        (!isThreadWrapperReplaceable ||
         wrapperLinkage == cir::GlobalLinkageKind::LinkOnceODRLinkage ||
         wrapperLinkage == cir::GlobalLinkageKind::WeakODRLinkage ||
         vd->getVisibility() == HiddenVisibility))
      wrapperFn.setGlobalVisibility(cir::VisibilityKind::Hidden);
    if (cgm.supportsCOMDAT() && cir::isWeakForLinker(wrapperLinkage))
      wrapperFn.setComdat(true);

    // Apply function attributes matching OGCG's SetLLVMFunctionAttributes +
    // SetLLVMFunctionAttributesForDefinition(nullptr, Wrapper). TLS wrappers
    // are compiler-generated (no Decl), so they use the D=nullptr path.
    cgm.setDefaultFunctionAttributesForCompilerGenerated(wrapperFn);
    cgm.setCIRFunctionAttributesForDefinition(GlobalDecl{}, wrapperFn);

    // OGCG downgrades weak_odr to linkonce_odr for variables not defined in
    // this TU: the wrapper is discardable since the defining TU will provide
    // a non-discardable copy.
    if (!vd->hasDefinition() &&
        wrapperLinkage == cir::GlobalLinkageKind::WeakODRLinkage) {
      // On Darwin with TLS_Dynamic, the wrapper is "replaceable" — the defining
      // TU provides it, so extern TLS variables just get an external declaration
      // (no body). This matches OGCG's isThreadWrapperReplaceable check at
      // ItaniumCXXABI.cpp:3263-3266.
      if (vd->getTLSKind() == VarDecl::TLS_Dynamic &&
          cgm.getTarget().getTriple().isOSDarwin()) {
        wrapperFn.setLinkageAttr(cir::GlobalLinkageKindAttr::get(
            &mlirCtx, cir::GlobalLinkageKind::ExternalLinkage));
        wrapperFn.setGlobalVisibility(cir::VisibilityKind::Default);
        continue; // Leave as declaration — no body.
      }
      wrapperFn.setLinkageAttr(cir::GlobalLinkageKindAttr::get(
          &mlirCtx, cir::GlobalLinkageKind::LinkOnceODRLinkage));
    }

    // For extern TLS variables (no definition in this TU), declare the
    // extern_weak init function _ZTH<var> at module level so the wrapper
    // can null-check and call it.
    cir::FuncOp initFn;
    if (!vd->hasDefinition()) {
      SmallString<256> initName;
      {
        llvm::raw_svector_ostream out(initName);
        mangleCtx.mangleItaniumThreadLocalInit(vd, out);
      }
      auto initFnTy = cir::FuncType::get({}, builder.getVoidTy());
      initFn = cgm.createCIRFunction(loc, initName, initFnTy, nullptr);
      initFn.setLinkageAttr(cir::GlobalLinkageKindAttr::get(
          &mlirCtx, cir::GlobalLinkageKind::ExternalWeakLinkage));
      // Apply default target function attributes (target-features,
      // no-trapping-math, stack-protector-buffer-size) to the extern_weak
      // declaration. Matches OGCG's SetLLVMFunctionAttributes call
      // (ItaniumCXXABI.cpp:3311-3312).
      {
        mlir::NamedAttrList attrs;
        cgm.getDefaultFunctionAttributes("", /*hasOptnone=*/false,
                                         /*attrOnCallSite=*/false, attrs);
        // Add target-features from TargetOpts (matches OGCG's
        // GetCPUAndFeaturesAttributes for functions without a FunctionDecl).
        auto features = cgm.getTarget().getTargetOpts().Features;
        if (!features.empty()) {
          llvm::erase_if(features, [&](const std::string &f) {
            return cgm.getTarget().isReadOnlyFeature(f.substr(1));
          });
          llvm::sort(features);
          attrs.set("target-features",
                    mlir::StringAttr::get(&mlirCtx, llvm::join(features, ",")));
        }
        initFn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
            attrs.getDictionary(&mlirCtx)));
      }
      // On Darwin, init functions also use cxx_fast_tlscc.
      if (isThreadWrapperReplaceable)
        initFn.setCallingConv(cir::CallingConv::CXXFastTLS);
    }

    // Build function body: return thread-local address of the variable.
    // For extern TLS variables (no definition in this TU), emit a null-check
    // of the extern_weak init function before accessing the TLS address.
    // Use an InsertionGuard so we restore the builder position after emitting
    // the body, ensuring subsequent createCIRFunction calls are at module level.
    {
      mlir::OpBuilder::InsertionGuard guard(builder);
      mlir::Block *entryBb = wrapperFn.addEntryBlock();
      builder.setInsertionPointToStart(entryBb);

      // For variables without a definition, null-check and call the
      // extern_weak TLS init function. OGCG emits:
      //   %p = icmp ne ptr @_ZTH<var>, null
      //   br i1 %p, label %call, label %skip
      //   call: call void @_ZTH<var>()
      //   skip: <access TLS>
      // Matches OGCG's EmitThreadLocalInitFuncs (ItaniumCXXABI.cpp:3327-3395).
      if (initFn) {
        mlir::Value fnAddr = cir::GetGlobalOp::create(
            builder, loc, builder.getPointerTo(initFn.getFunctionType()),
            initFn.getSymName());
        mlir::Value null =
            builder.getNullValue(fnAddr.getType(), loc);
        mlir::Value isNonNull = builder.createCompare(
            loc, cir::CmpOpKind::ne, fnAddr, null);
        cir::IfOp::create(
            builder, loc, isNonNull, /*withElseRegion=*/false,
            [&](mlir::OpBuilder &, mlir::Location) {
              cir::CallingConv cc = isThreadWrapperReplaceable
                                        ? cir::CallingConv::CXXFastTLS
                                        : cir::CallingConv::C;
              builder.createCallOp(loc, initFn, {}, cc);
              cir::YieldOp::create(builder, loc);
            });
      }

      mlir::Value addr =
          builder.createGetGlobal(loc, gv, /*threadLocal=*/true);

      // For a reference, the result of the wrapper function is a pointer to
      // the referenced object. The TLS variable stores a pointer, so we need
      // to load through it. Matches OGCG ItaniumCXXABI.cpp:3391-3394.
      if (vd->getType()->isReferenceType()) {
        CharUnits align = cgm.getASTContext().getDeclAlign(vd);
        addr = builder.createAlignedLoad(loc, gv.getSymType(), addr, align);
      }

      // The global's sym type may differ from the declared C++ type when
      // the variable is a union initialized via a non-first member (the
      // global is retyped to match the active member's initializer).
      // Bitcast the pointer to match the wrapper's return type.
      if (addr.getType() != ptrTy) {
        auto srcPtrTy = mlir::cast<cir::PointerType>(addr.getType());
        auto dstPtrTy = mlir::cast<cir::PointerType>(ptrTy);
        if (srcPtrTy.getAddrSpace() != dstPtrTy.getAddrSpace())
          addr = builder.createAddrSpaceCast(loc, addr, ptrTy);
        else
          addr = builder.createBitcast(loc, addr, ptrTy);
      }
      cir::ReturnOp::create(builder, loc, addr);
    }
  }
}
