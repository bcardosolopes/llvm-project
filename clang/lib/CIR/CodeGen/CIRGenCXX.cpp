//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with C++ code generation.
//
//===----------------------------------------------------------------------===//

#include "CIRGenCXXABI.h"
#include "CIRGenFunction.h"
#include "CIRGenModule.h"

#include "clang/AST/GlobalDecl.h"
#include "clang/AST/VTableBuilder.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace clang;
using namespace clang::CIRGen;

static mlir::Value getTLSRegistrationAddress(CIRGenBuilderTy &builder,
                                             mlir::Value addr) {
  if (auto getGlobal = addr.getDefiningOp<cir::GetGlobalOp>()) {
    if (!getGlobal.getTls())
      return addr;
    return cir::GetGlobalOp::create(builder, getGlobal.getLoc(),
                                    getGlobal.getType(), getGlobal.getName());
  }

  auto cast = addr.getDefiningOp<cir::CastOp>();
  if (!cast || cast.getKind() != cir::CastKind::bitcast)
    return addr;

  mlir::Value src = getTLSRegistrationAddress(builder, cast.getSrc());
  if (src == cast.getSrc())
    return addr;
  return builder.createBitcast(cast.getLoc(), src, cast.getType());
}

cir::FuncOp CIRGenFunction::generateDestroyHelper(Address addr, QualType type,
                                                  Destroyer *destroyer,
                                                  bool useEHCleanupForArray,
                                                  const VarDecl *vd) {
  SourceLocation helperSourceLoc = vd ? vd->getLocation() : SourceLocation();
  mlir::Location loc =
      helperSourceLoc.isValid() ? getLoc(helperSourceLoc) : builder.getUnknownLoc();

  QualType helperArgTy = getContext().VoidPtrTy;
  mlir::Type helperArgIRTy = convertType(helperArgTy);
  const IdentifierInfo *helperII =
      &getContext().Idents.get("__cxx_global_array_dtor");
  QualType helperFnTy =
      getContext().getFunctionType(getContext().VoidTy, {helperArgTy}, {});
  FunctionDecl *helperDecl = FunctionDecl::Create(
      getContext(), getContext().getTranslationUnitDecl(), helperSourceLoc,
      helperSourceLoc, helperII, helperFnTy, /*TInfo=*/nullptr, SC_Static,
      /*UsesFPIntrin=*/false, /*isInlineSpecified=*/false,
      /*hasWrittenPrototype=*/false);
  ParmVarDecl *helperParam = ParmVarDecl::Create(
      getContext(), helperDecl, helperSourceLoc, helperSourceLoc,
      /*Id=*/nullptr, helperArgTy,
      getContext().getTrivialTypeSourceInfo(helperArgTy, helperSourceLoc),
      SC_None, /*DefArg=*/nullptr);
  ParmVarDecl *helperParams[] = {helperParam};
  helperDecl->setParams(helperParams);

  cir::FuncOp fn = cgm.createCIRFunction(
      loc, cgm.getUniqueGlobalName("__cxx_global_array_dtor"),
      builder.getFuncType({helperArgIRTy}, builder.getVoidTy()), nullptr);
  fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
  fn.setSymVisibility("private");
  cgm.setCIRFunctionAttributesForDefinition(GlobalDecl{}, fn);
  if (const char *section =
          getContext().getTargetInfo().getStaticInitSectionSpecifier())
    fn.setSectionAttr(builder.getStringAttr(section));

  mlir::Value basePtr = addr.getPointer();
  cir::GetGlobalOp getGlobalOp = basePtr.getDefiningOp<cir::GetGlobalOp>();
  if (!getGlobalOp) {
    auto castOp = basePtr.getDefiningOp<cir::CastOp>();
    if (castOp && castOp.getKind() == cir::CastKind::bitcast)
      getGlobalOp = castOp.getSrc().getDefiningOp<cir::GetGlobalOp>();
  }
  assert(getGlobalOp && "expected GetGlobalOp for destroy helper target");
  auto globalOp = mlir::dyn_cast_or_null<cir::GlobalOp>(
      cgm.getGlobalValue(getGlobalOp.getName()));
  assert(globalOp && "expected GlobalOp for destroy helper target");

  mlir::OpBuilder::InsertionGuard guard(builder);
  CIRGenFunction helperCGF(cgm, builder, /*suppressNewContext=*/true);
  helperCGF.curGD = GlobalDecl{};
  SourceLocRAIIObject helperLoc(helperCGF, loc);
  mlir::Block *entry = fn.addEntryBlock();

  FunctionArgList args;
  args.push_back(helperParam);

  SymTableScopeTy varScope(helperCGF.symbolTable);
  {
    LexicalScope fnScope(helperCGF, loc, entry);
    helperCGF.startFunction(GlobalDecl{}, getContext().VoidTy, fn,
                            fn.getFunctionType(), args, helperSourceLoc,
                            helperSourceLoc);

    auto &helperBuilder = helperCGF.getBuilder();
    mlir::Value globalVal = helperBuilder.createGetGlobal(globalOp);
    if (globalVal.getType() != addr.getPointer().getType())
      globalVal = helperBuilder.createBitcast(loc, globalVal,
                                              addr.getPointer().getType());
    Address helperAddr(globalVal, addr.getElementType(), addr.getAlignment());
    helperCGF.emitDestroy(helperAddr, type, destroyer, useEHCleanupForArray);
    cir::ReturnOp::create(helperBuilder, loc);
    if (mlir::failed(fn.verifyBody()))
      return {};
    helperCGF.finishFunction(SourceLocation());
  }

  return fn;
}

/// Try to emit a base destructor as an alias to its primary
/// base-class destructor.
bool CIRGenModule::tryEmitBaseDestructorAsAlias(const CXXDestructorDecl *d) {
  if (!getCodeGenOpts().CXXCtorDtorAliases)
    return true;

  // Producing an alias to a base class ctor/dtor can degrade debug quality
  // as the debugger cannot tell them apart.
  if (getCodeGenOpts().OptimizationLevel == 0)
    return true;

  // OGCG disables this optimization for ARM64EC due to symbol table
  // complications (CGCXX.cpp:42-44).
  if (astContext.getTargetInfo().getTriple().isWindowsArm64EC())
    return true;

  // If sanitizing memory to check for use-after-dtor, do not emit as
  // an alias, unless this class owns no members. The actual sanitizer
  // callbacks are emitted in enterDtorCleanups.
  if (getCodeGenOpts().SanitizeMemoryUseAfterDtor &&
      !d->getParent()->field_empty())
    return true;

  // If the destructor doesn't have a trivial body, we have to emit it
  // separately.
  if (!d->hasTrivialBody())
    return true;

  const CXXRecordDecl *klass = d->getParent();

  // We are going to instrument this destructor, so give up even if it is
  // currently empty.
  if (klass->mayInsertExtraPadding())
    return true;

  // If we need to manipulate a VTT parameter, give up.
  if (klass->getNumVBases()) {
    // Extra Credit:  passing extra parameters is perfectly safe
    // in many calling conventions, so only bail out if the ctor's
    // calling convention is nonstandard.
    return true;
  }

  // If any field has a non-trivial destructor, we have to emit the
  // destructor separately.
  for (const auto *i : klass->fields())
    if (i->getType().isDestructedType())
      return true;

  // Try to find a unique base class with a non-trivial destructor.
  const CXXRecordDecl *uniqueBase = nullptr;
  for (const auto &i : klass->bases()) {

    // We're in the base destructor, so skip virtual bases.
    if (i.isVirtual())
      continue;

    // Skip base classes with trivial destructors.
    const auto *base = i.getType()->castAsCXXRecordDecl();
    if (base->hasTrivialDestructor())
      continue;

    // If we've already found a base class with a non-trivial
    // destructor, give up.
    if (uniqueBase)
      return true;
    uniqueBase = base;
  }

  // If we didn't find any bases with a non-trivial destructor, then
  // the base destructor is actually effectively trivial, which can
  // happen if it was needlessly user-defined or if there are virtual
  // bases with non-trivial destructors.
  if (!uniqueBase)
    return true;

  // If the base is at a non-zero offset, give up.
  const ASTRecordLayout &classLayout = astContext.getASTRecordLayout(klass);
  if (!classLayout.getBaseClassOffset(uniqueBase).isZero())
    return true;

  // Give up if the calling conventions don't match. We could update the call,
  // but it is probably not worth it.
  const CXXDestructorDecl *baseD = uniqueBase->getDestructor();
  if (baseD->getType()->castAs<FunctionType>()->getCallConv() !=
      d->getType()->castAs<FunctionType>()->getCallConv())
    return true;

  GlobalDecl aliasDecl(d, Dtor_Base);
  GlobalDecl targetDecl(baseD, Dtor_Base);

  // The alias will use the linkage of the referent.  If we can't
  // support aliases with that linkage, fail.
  auto linkage = getFunctionLinkage(aliasDecl);

  // We can't use an alias if the linkage is not valid for one.
  if (!cir::isValidLinkage(linkage))
    return true;

  auto targetLinkage = getFunctionLinkage(targetDecl);

  // Check if we have it already.
  StringRef mangledName = getMangledName(aliasDecl);
  auto entry = getGlobalValue(mangledName);
  auto globalValue = dyn_cast_or_null<cir::CIRGlobalValueInterface>(entry);
  if (entry && globalValue && !globalValue.isDeclaration())
    return false;
  if (replacements.count(mangledName))
    return false;

  // Find the referent once and reuse it in all downstream checks/creation to
  // avoid repeated symbol lookup/creation work on this hot alias path.
  auto aliaseeOp = getAddrOfGlobal(targetDecl);
  StringRef targetName = getMangledName(targetDecl);
  auto aliaseeGV = dyn_cast_or_null<cir::CIRGlobalValueInterface>(aliaseeOp);

  // Instead of creating as alias to a linkonce_odr, replace all of the uses
  // of the aliasee.
  if (cir::isDiscardableIfUnused(linkage) &&
      !(targetLinkage == cir::GlobalLinkageKind::AvailableExternallyLinkage &&
        targetDecl.getDecl()->hasAttr<AlwaysInlineAttr>())) {
    // FIXME: An extern template instantiation will create functions with
    // linkage "AvailableExternally". In libc++, some classes also define
    // members with attribute "AlwaysInline" and expect no reference to
    // be generated. It is desirable to reenable this optimisation after
    // corresponding LLVM changes.
    addReplacement(mangledName, targetName);
    return false;
  }

  // If we have a weak, non-discardable alias (weak, weak_odr), like an
  // extern template instantiation or a dllexported class, avoid forming it on
  // COFF. A COFF weak external alias cannot satisfy a normal undefined
  // symbol reference from another TU. The other TU must also mark the
  // referenced symbol as weak, which we cannot rely on.
  if (cir::isWeakForLinker(linkage) && getTriple().isOSBinFormatCOFF()) {
    llvm_unreachable("please sent a PR with a test and remove this.\n");
    return true;
  }

  // If we don't have a definition for the destructor yet or the definition
  // is available_externally, don't emit an alias.  We can't emit aliases to
  // declarations; that's just not how aliases work.
  if (aliaseeGV && aliaseeGV.isDeclarationForLinker())
    return true;

  // Don't create an alias to a linker weak symbol. This avoids producing
  // different COMDATs in different TUs. Another option would be to
  // output the alias both for weak_odr and linkonce_odr, but that
  // requires explicit comdat support in the IL.
  if (cir::isWeakForLinker(targetLinkage))
    return true;

  // Create the alias with no name.
  auto aliasee = cast<cir::FuncOp>(aliaseeOp);
  emitAliasForGlobal(mangledName, entry, aliasDecl, aliasee, linkage);
  return false;
}

// OGCG has EmitDefinitionAsAlias (CGCXX.cpp) for unconditionally aliasing
// one global definition to another, used by MSVC ABI for destructor aliasing.
// Itanium destructor aliasing is handled directly in
// tryEmitBaseDestructorAsAlias. MSVC ABI is not a supported CIR target.

/// Emit code to cause the variable at the given address to be considered as
/// constant from this point onwards.
static void emitDeclInvariant(CIRGenFunction &cgf, const VarDecl *d) {
  mlir::Value addr = cgf.cgm.getAddrOfGlobalVar(d);
  cgf.emitInvariantStart(cgf.getContext().getTypeSizeInChars(d->getType()),
                         addr, cgf.getLoc(d->getSourceRange()));
}

void CIRGenFunction::emitInvariantStart(CharUnits size, mlir::Value addr,
                                        mlir::Location loc) {
  // Do not emit the intrinsic if we're not optimizing.
  if (!cgm.getCodeGenOpts().OptimizationLevel)
    return;

  CIRGenBuilderTy &builder = getBuilder();

  // Create the size constant as i64
  uint64_t width = size.getQuantity();
  mlir::Value sizeValue = builder.getConstInt(loc, builder.getSInt64Ty(),
                                              static_cast<int64_t>(width));

  // Create the intrinsic call. The llvm.invariant.start intrinsic returns a
  // token in the default address space regardless of the operand's address
  // space; it is overloaded only on the operand pointer. Using the operand's
  // (possibly non-default) address space for the result type would select a
  // non-existent overload and fail intrinsic translation. We don't capture the
  // result, so use a default-address-space pointer for it.
  cir::LLVMIntrinsicCallOp::create(
      builder, loc, cgm.getStringAttrCached("invariant.start"),
      builder.getVoidPtrTy(), mlir::ValueRange{sizeValue, addr});
}

static void emitDeclInit(CIRGenFunction &cgf, const VarDecl *varDecl,
                         cir::GlobalOp globalOp) {
  assert((varDecl->hasGlobalStorage() ||
          (varDecl->hasLocalStorage() &&
           cgf.getContext().getLangOpts().OpenCLCPlusPlus)) &&
         "VarDecl must have global or local (in the case of OpenCL) storage!");
  assert(!varDecl->getType()->isReferenceType() &&
         "Should not call emitDeclInit on a reference!");

  // Guard against double emission. For template instantiations used multiple
  // times (e.g., `n<> + n<>`), emitGlobalVarDefinition may be called more
  // than once for the same global, each time reaching emitDeclInit. Since CIR
  // attaches init code to the ctorRegion (unlike OGCG which creates a separate
  // init function), adding another block would violate the MaxSizedRegion<1>
  // constraint.
  if (!globalOp.getCtorRegion().empty())
    return;

  CIRGenBuilderTy &builder = cgf.getBuilder();

  // Set up the ctor region.
  mlir::OpBuilder::InsertionGuard guard(builder);
  mlir::Block *block = builder.createBlock(&globalOp.getCtorRegion());
  CIRGenFunction::LexicalScope lexScope{cgf, globalOp.getLoc(),
                                        builder.getInsertionBlock()};
  lexScope.setAsGlobalInit();
  builder.setInsertionPointToStart(block);

  mlir::Value declPtr = cgf.cgm.getAddrOfGlobalVar(varDecl);

  // The address space of a global variable may differ from the expected address
  // space of its type. For example, on AMDGCN, globals live in AS 1 (global
  // memory) but the constructor's `this` pointer expects AS 0 (generic/flat).
  // Insert an addrspacecast if needed (matches OGCG CGDeclCXX.cpp:198-205).
  QualType type = varDecl->getType();
  LangAS expectedAS = type.getAddressSpace();
  auto declPtrTy = mlir::cast<cir::PointerType>(declPtr.getType());
  mlir::ptr::MemorySpaceAttrInterface expectedAddrSpace =
      (expectedAS == LangAS::Default)
          ? mlir::ptr::MemorySpaceAttrInterface{}
          : cir::toCIRLangAddressSpaceAttr(&cgf.cgm.getMLIRContext(),
                                           expectedAS);
  if (declPtrTy.getAddrSpace() != expectedAddrSpace) {
    auto newPtrTy =
        cir::PointerType::get(declPtrTy.getPointee(), expectedAddrSpace);
    declPtr = builder.createAddrSpaceCast(declPtr, newPtrTy);
  }

  Address declAddr(declPtr, cgf.cgm.getASTContext().getDeclAlign(varDecl));
  LValue lv = cgf.makeAddrLValue(declAddr, type);

  const Expr *init = varDecl->getInit();
  switch (CIRGenFunction::getEvaluationKind(type)) {
  case cir::TEK_Scalar:
    cgf.emitScalarInit(init, cgf.getLoc(varDecl->getLocation()), lv, false);
    break;
  case cir::TEK_Complex:
    cgf.emitComplexExprIntoLValue(init, lv, /*isInit=*/true);
    break;
  case cir::TEK_Aggregate:
    cgf.emitAggExpr(init,
                    AggValueSlot::forLValue(lv, AggValueSlot::IsDestructed,
                                            AggValueSlot::IsNotAliased,
                                            AggValueSlot::DoesNotOverlap));
    break;
  }

  // Finish the ctor region.
  builder.setInsertionPointToEnd(block);
  cir::YieldOp::create(builder, globalOp.getLoc());
}

static void emitDeclDestroy(CIRGenFunction &cgf, const VarDecl *vd,
                            cir::GlobalOp addr) {
  // Honor __attribute__((no_destroy)) and bail instead of attempting
  // to emit a reference to a possibly nonexistent destructor, which
  // in turn can cause a crash. This will result in a global constructor
  // that isn't balanced out by a destructor call as intended by the
  // attribute. This also checks for -fno-c++-static-destructors and
  // bails even if the attribute is not present.
  QualType::DestructionKind dtorKind = vd->needsDestruction(cgf.getContext());

  // FIXME:  __attribute__((cleanup)) ?

  switch (dtorKind) {
  case QualType::DK_none:
    return;

  case QualType::DK_cxx_destructor:
    // TLS variables with C++ destructors are handled by
    // emitCXXThreadLocalInitFuncs which registers via __cxa_thread_atexit.
    // Non-TLS variables fall through to the __cxa_atexit path below.
    break;

  case QualType::DK_objc_strong_lifetime:
  case QualType::DK_objc_weak_lifetime:
  case QualType::DK_nontrivial_c_struct:
    // We don't care about releasing objects during process teardown.
    assert(!vd->getTLSKind() && "should have rejected this");
    return;
  }

  // If not constant storage we'll emit this regardless of NeedsDtor value.
  CIRGenBuilderTy &builder = cgf.getBuilder();

  // Prepare the dtor region.
  mlir::OpBuilder::InsertionGuard guard(builder);
  mlir::Block *block = builder.createBlock(&addr.getDtorRegion());
  CIRGenFunction::LexicalScope lexScope{cgf, addr.getLoc(),
                                        builder.getInsertionBlock()};
  lexScope.setAsGlobalInit();
  builder.setInsertionPointToStart(block);

  CIRGenModule &cgm = cgf.cgm;
  QualType type = vd->getType();

  // Special-case non-array C++ destructors, if they have the right signature.
  // Under some ABIs, destructors return this instead of void, and cannot be
  // passed directly to __cxa_atexit if the target does not allow this
  // mismatch.
  const CXXRecordDecl *record = type->getAsCXXRecordDecl();
  bool canRegisterDestructor =
      record && (!cgm.getCXXABI().hasThisReturn(
                     GlobalDecl(record->getDestructor(), Dtor_Complete)) ||
                 cgm.getCXXABI().canCallMismatchedFunctionType());

  // If __cxa_atexit is disabled via a flag, a different helper function is
  // generated elsewhere which uses atexit instead, and it takes the destructor
  // directly.
  cir::FuncOp fnOp;
  if (record && (canRegisterDestructor || !cgm.getCodeGenOpts().CXAAtExit)) {
    assert(!record->hasTrivialDestructor());
    CXXDestructorDecl *dtor = record->getDestructor();
    // In LLVM OG codegen this is done in registerGlobalDtor, but CIRGen
    // relies on LoweringPrepare for further decoupling, so build the
    // call right here.
    auto gd = GlobalDecl(dtor, Dtor_Complete);
    fnOp = cgm.getAddrAndTypeOfCXXStructor(gd).second;
    mlir::Value globalAddr = cgm.getAddrOfGlobalVar(vd);
    mlir::Type expectedThisTy = fnOp.getFunctionType().getInput(0);
    if (globalAddr.getType() != expectedThisTy) {
      auto globalPtrTy = mlir::cast<cir::PointerType>(globalAddr.getType());
      auto expectedPtrTy = mlir::cast<cir::PointerType>(expectedThisTy);
      if (globalPtrTy.getAddrSpace() != expectedPtrTy.getAddrSpace())
        globalAddr = builder.createAddrSpaceCast(globalAddr, expectedThisTy);
      else
        globalAddr = builder.createBitcast(cgf.getLoc(vd->getSourceRange()),
                                           globalAddr, expectedThisTy);
    }
    builder.createCallOp(cgf.getLoc(vd->getSourceRange()), fnOp,
                         mlir::ValueRange{globalAddr});
    assert(fnOp && "expected cir.func");
    // TODO(cir): This doesn't do anything but check for unhandled conditions.
    // What it is meant to do should really be happening in LoweringPrepare.
    cgm.getCXXABI().registerGlobalDtor(vd, fnOp, nullptr);
  } else {
    // Otherwise, a custom destroyed is needed. Classic codegen creates a helper
    // function here and emits the destroy into the helper function, which is
    // called from __cxa_atexit.
    // In CIR, we just emit the destroy into the dtor region. It will be moved
    // into a separate function during the LoweringPrepare pass.
    // FIXME(cir): We should create a new operation here to explicitly get the
    // address of the global into whose dtor region we are emiiting the destroy.
    // The same applies to code above where it is calling getAddrOfGlobalVar.
    mlir::Value globalVal = builder.createGetGlobal(addr);
    mlir::Type expectedElTy = cgf.convertTypeForMem(type);
    // The global's sym type may differ from the C-level type when the class
    // has a vtable (the initializer is an anonymous struct with vptr fields).
    // Bitcast the pointer to match the expected element type, preserving the
    // global's address space (a bitcast cannot cross address spaces).
    auto ptrTy = mlir::cast<cir::PointerType>(globalVal.getType());
    if (ptrTy.getPointee() != expectedElTy)
      globalVal = builder.createBitcast(
          globalVal,
          cir::PointerType::get(expectedElTy, ptrTy.getAddrSpace()));
    CharUnits alignment = cgf.getContext().getDeclAlign(vd);
    Address globalAddr{globalVal, expectedElTy, alignment};
    // Do NOT pass useEHCleanupForArray=true here. The dtor region
    // will be extracted into a standalone helper function by
    // LoweringPrepare. Using EH cleanups would cause the landing
    // pad to emit all in-scope EH cleanups from the enclosing
    // function's EH stack (e.g., destructors for local variables,
    // guard-abort calls), creating cross-function SSA references
    // that violate MLIR dominance rules.
    cgf.emitDestroy(globalAddr, type, cgf.getDestroyer(dtorKind),
                    /*useEHCleanupForArray=*/false);
  }

  builder.setInsertionPointToEnd(block);
  if (block->empty()) {
    block->erase();
    // Don't confuse lexical cleanup.
    builder.clearInsertionPoint();
  } else {
    cir::YieldOp::create(builder, addr.getLoc());
  }
}

void CIRGenFunction::emitCXXAtExit(mlir::Location loc, cir::FuncOp dtorFn,
                                   mlir::Value varAddr, bool isTLS) {
  auto &builder = getBuilder();
  auto voidTy = builder.getVoidTy();
  auto voidPtrTy = cir::PointerType::get(voidTy);

  // When -fno-use-cxa-atexit is set (and not TLS), use atexit() with a
  // wrapper function instead of __cxa_atexit. Matches OGCG's
  // registerGlobalDtorWithAtExit in ItaniumCXXABI.cpp.
  if (!cgm.getCodeGenOpts().CXAAtExit && !isTLS) {
    // Create a wrapper stub: void __dtor_<varname>() { dtor(&var); }
    std::string stubName;
    {
      llvm::raw_string_ostream os(stubName);
      os << "__dtor_" << dtorFn.getSymName();
      // Use the variable address to make unique names for static locals.
      // The varAddr is a get_global op — extract the symbol name.
      if (auto getGlobal = varAddr.getDefiningOp<cir::GetGlobalOp>())
        stubName = ("__dtor_" + getGlobal.getName()).str();
    }
    auto noArgVoidFnTy = builder.getFuncType({}, voidTy);
    cir::FuncOp stub = cgm.createRuntimeFunction(noArgVoidFnTy, stubName,
                                                  /*extraAttrs=*/{},
                                                  /*isLocal=*/true);
    stub.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    // Set section from target if specified. Matches OGCG
    // CreateGlobalInitOrCleanUpFunction which only sets section from
    // getStaticInitSectionSpecifier(), not unconditionally.
    if (const char *section =
            getContext().getTargetInfo().getStaticInitSectionSpecifier())
      stub.setSectionAttr(builder.getStringAttr(section));
    // Match OGCG's CreateGlobalInitOrCleanUpFunction path for atexit stubs:
    // these compiler-generated helpers carry the full internal-helper attr
    // bundle, including noinline/nounwind and option-driven attrs like
    // no-builtins.
    {
      mlir::NamedAttrList attrs;
      if (auto existingExtraAttrs = stub.getExtraAttrs())
        attrs = mlir::NamedAttrList{
            existingExtraAttrs->getElements().getValue()};
      cgm.getDefaultInternalFunctionAttributes(attrs);
      stub.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
          attrs.getDictionary(&cgm.getMLIRContext())));
      if (attrs.get("noinline"))
        stub.setInlineKind(cir::InlineKind::NoInline);
    }

    // Build stub body: call dtor(&var) or dtor(nullptr) for callbacks that
    // capture the global directly and ignore the registration pointer.
    cir::GetGlobalOp getGlobalOp;
    if (varAddr) {
      getGlobalOp = varAddr.getDefiningOp<cir::GetGlobalOp>();
      if (!getGlobalOp) {
        auto castOp = varAddr.getDefiningOp<cir::CastOp>();
        if (castOp && castOp.getKind() == cir::CastKind::bitcast)
          getGlobalOp = castOp.getSrc().getDefiningOp<cir::GetGlobalOp>();
      }
      assert(getGlobalOp &&
             "expected get_global or bitcast thereof for atexit registration");
    }

    if (stub.empty()) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      mlir::Block *stubBlock = stub.addEntryBlock();
      builder.setInsertionPointToStart(stubBlock);
      mlir::Type expectedThisTy = dtorFn.getFunctionType().getInput(0);
      mlir::Value objAddr;
      if (getGlobalOp) {
        objAddr = cir::GetGlobalOp::create(builder, loc, getGlobalOp.getType(),
                                           getGlobalOp.getName());
        if (objAddr.getType() != varAddr.getType())
          objAddr = builder.createBitcast(loc, objAddr, varAddr.getType());
        if (objAddr.getType() != expectedThisTy)
          objAddr = builder.createBitcast(loc, objAddr, expectedThisTy);
      } else {
        objAddr = builder.getNullPtr(expectedThisTy, loc);
      }
      builder.createCallOp(loc, dtorFn, {objAddr});
      cir::ReturnOp::create(builder, loc);
    }

    // Register with atexit():
    //   extern "C" int atexit(void (*f)(void));
    auto noArgVoidFnPtrTy = cir::PointerType::get(noArgVoidFnTy);
    auto atexitType = builder.getFuncType(
        {noArgVoidFnPtrTy}, builder.getSInt32Ty());
    cir::FuncOp fnAtExit = cgm.createRuntimeFunction(atexitType, "atexit");
    // atexit does not throw. Matches OGCG CGDeclCXX.cpp.
    {
      mlir::NamedAttrList attrs;
      if (auto existingAttrs = fnAtExit.getExtraAttrs())
        attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
      attrs.set("nothrow", mlir::UnitAttr::get(builder.getContext()));
      fnAtExit.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
          builder.getContext(),
          mlir::DictionaryAttr::get(builder.getContext(), attrs)));
    }

    auto stubPtrTy = cir::PointerType::get(stub.getFunctionType());
    mlir::Value stubRef = cir::GetGlobalOp::create(
        builder, loc, stubPtrTy, stub.getSymNameAttr());
    stubRef = builder.createBitcast(loc, stubRef, noArgVoidFnPtrTy);
    auto call = builder.createCallOp(loc, fnAtExit, {stubRef});
    call.setNothrow(true);
    return;
  }

  auto i8Ty = builder.getUInt8Ty();
  auto i8PtrTy = cir::PointerType::get(i8Ty);

  // Declare __dso_handle as an external hidden global if not already present.
  cir::GlobalOp dsoHandleGlobal;
  {
    auto *existingSym = cgm.getGlobalValue("__dso_handle");
    if (existingSym) {
      dsoHandleGlobal = mlir::cast<cir::GlobalOp>(existingSym);
    } else {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToStart(
          &cgm.getModule().getBodyRegion().front());
      dsoHandleGlobal = cir::GlobalOp::create(
          builder, loc, "__dso_handle", i8Ty,
          /*isConstant=*/false,
          cir::GlobalLinkageKind::ExternalLinkage);
      cgm.insertGlobalSymbol(dsoHandleGlobal);
      // __dso_handle has hidden visibility (defined in the runtime but
      // not exported from the DSO), matching OGCG.
      dsoHandleGlobal.setSymVisibility("private");
      dsoHandleGlobal.setGlobalVisibility(cir::VisibilityKind::Hidden);
      cgm.setDSOLocal(static_cast<mlir::Operation *>(dsoHandleGlobal));
    }
  }
  mlir::Value dsoHandle = builder.createGetGlobal(loc, dsoHandleGlobal);

  // Select the right atexit function: __cxa_thread_atexit for TLS variables,
  // __cxa_atexit for regular static variables. On Darwin, TLS uses
  // _tlv_atexit instead.
  const char *atExitName = "__cxa_atexit";
  if (isTLS) {
    const llvm::Triple &T = getTarget().getTriple();
    atExitName = T.isOSDarwin() ? "_tlv_atexit" : "__cxa_thread_atexit";
  }

  // Declare the atexit function:
  //   extern "C" int __cxa_atexit(void (*f)(void *), void *p, void *d);
  //   extern "C" int __cxa_thread_atexit(void (*f)(void *), void *p, void *d);
  auto voidFnTy = builder.getFuncType({voidPtrTy}, voidTy);
  auto voidFnPtrTy = cir::PointerType::get(voidFnTy);
  auto atExitFnTy =
      builder.getFuncType({voidFnPtrTy, voidPtrTy, i8PtrTy},
                          builder.getSInt32Ty());
  cir::FuncOp atExitFn =
      cgm.createRuntimeFunction(atExitFnTy, atExitName);
  // __cxa_atexit does not throw. Matches OGCG's
  // fn->setDoesNotThrow() in ItaniumCXXABI.cpp:2955.
  {
    mlir::NamedAttrList attrs;
    if (auto existingAttrs = atExitFn.getExtraAttrs())
      attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
    attrs.set("nothrow", mlir::UnitAttr::get(builder.getContext()));
    atExitFn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        builder.getContext(),
        mlir::DictionaryAttr::get(builder.getContext(), attrs)));
  }

  // Get destructor function address and cast to void(*)(void*).
  auto dtorPtrTy = cir::PointerType::get(dtorFn.getFunctionType());
  mlir::Value dtorRef = cir::GetGlobalOp::create(
      builder, loc, dtorPtrTy, dtorFn.getSymNameAttr());
  mlir::Value dtorCast = builder.createBitcast(loc, dtorRef, voidFnPtrTy);

  // Match OGCG: some helper callbacks capture the target global directly and
  // ignore the registration pointer, so they are registered with nullptr.
  mlir::Value varCast;
  if (varAddr) {
    if (isTLS)
      varAddr = getTLSRegistrationAddress(builder, varAddr);
    // __cxa_atexit takes a generic (default-address-space) pointer; a global in
    // a non-default address space must be address-space cast, not bitcast (a
    // bitcast cannot cross address spaces).
    auto varPtrTy = mlir::cast<cir::PointerType>(varAddr.getType());
    if (varPtrTy.getAddrSpace() != voidPtrTy.getAddrSpace()) {
      if (varPtrTy.getPointee() != voidPtrTy.getPointee())
        varAddr = builder.createBitcast(
            loc, varAddr,
            cir::PointerType::get(voidPtrTy.getPointee(),
                                  varPtrTy.getAddrSpace()));
      varCast = builder.createAddrSpaceCast(loc, varAddr, voidPtrTy);
    } else {
      varCast = builder.createBitcast(loc, varAddr, voidPtrTy);
    }
  } else {
    varCast = builder.getNullPtr(voidPtrTy, loc);
  }

  auto call = builder.createCallOp(loc, atExitFn, {dtorCast, varCast, dsoHandle},
                                   atExitFn.getCallingConv());
  call->setAttr("nothrow", mlir::UnitAttr::get(&cgm.getMLIRContext()));

  FunctionProtoType::ExtProtoInfo EPI(
      cgm.getASTContext().getDefaultCallingConvention(/*IsVariadic=*/false,
                                                      /*IsCXXMethod=*/false));
  QualType atExitCallbackTy = cgm.getASTContext().getFunctionType(
      cgm.getASTContext().VoidTy, {cgm.getASTContext().VoidPtrTy}, EPI);
  if (auto pointerAuth = cgm.getFunctionPointerAuthInfo(atExitCallbackTy)) {
    call->setAttr(
        "cir.atexit_callback_ptrauth",
        builder.getArrayAttr(
            {builder.getI32IntegerAttr(pointerAuth->key),
             builder.getI64IntegerAttr(pointerAuth->discriminator)}));
  }
}

cir::FuncOp CIRGenModule::codegenCXXStructor(GlobalDecl gd) {
  const CIRGenFunctionInfo &fnInfo =
      getTypes().arrangeCXXStructorDeclaration(gd);
  cir::FuncType funcType = getTypes().getFunctionType(fnInfo);
  cir::FuncOp fn = getAddrOfCXXStructor(gd, &fnInfo, /*FnType=*/nullptr,
                                        /*DontDefer=*/true, ForDefinition);
  setFunctionLinkage(gd, fn);
  CIRGenFunction cgf{*this, builder};
  curCGF = &cgf;
  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    fn = cgf.generateCode(gd, fn, funcType);
  }
  curCGF = nullptr;

  if (!fn)
    return nullptr;

  setNonAliasAttributes(gd, fn);
  setCIRFunctionAttributesForDefinition(gd, fn);
  // setGVProperties is called transitively via
  // setNonAliasAttributes -> setCommonAttributes -> setGVProperties.
  maybeHandleStaticInExternC(cast<FunctionDecl>(gd.getDecl()), fn.getSymName());
  maybeSetTrivialComdat(*gd.getDecl(), fn);
  return fn;
}

// Global variables requiring non-trivial initialization are handled
// differently in CIR than in classic codegen. Classic codegen emits
// a global init function (__cxx_global_var_init) and inserts
// initialization for each global there. In CIR, we attach a ctor
// region to the global variable and insert the initialization code
// into the ctor region. This will be moved into the
// __cxx_global_var_init function during the LoweringPrepare pass.
void CIRGenModule::emitCXXGlobalVarDeclInit(const VarDecl *varDecl,
                                            cir::GlobalOp addr,
                                            bool performInit) {
  QualType ty = varDecl->getType();

  // TODO: handle address space
  // The address space of a static local variable (addr) may be different
  // from the address space of the "this" argument of the constructor. In that
  // case, we need an addrspacecast before calling the constructor.
  //
  // struct StructWithCtor {
  //   __device__ StructWithCtor() {...}
  // };
  // __device__ void foo() {
  //   __shared__ StructWithCtor s;
  //   ...
  // }
  //
  // For example, in the above CUDA code, the static local variable s has a
  // "shared" address space qualifier, but the constructor of StructWithCtor
  // expects "this" in the "generic" address space.
  // Address space casting is handled in emitDeclInit.
  // Create a CIRGenFunction to emit the initializer. While this isn't a true
  // function, the handling works the same way.
  CIRGenFunction cgf{*this, builder, true};
  llvm::SaveAndRestore<CIRGenFunction *> savedCGF(curCGF, &cgf);
  curCGF->curFn = addr;

  // Initialize sanitizer options for global init context. In OGCG this happens
  // via StartFunction in GenerateCXXGlobalVarDeclInitFunc. Since CIR emits
  // global init code into GlobalOp ctor regions without calling startFunction,
  // we must set sanOpts here so that emitTypeCheck (for reference binding,
  // object-size checks, etc.) works correctly.
  cgf.sanOpts.set(getLangOpts().Sanitize.Mask);

  CIRGenFunction::SourceLocRAIIObject fnLoc{cgf,
                                            getLoc(varDecl->getLocation())};

  assert(!cir::MissingFeatures::astVarDeclInterface());

  // Set init_priority if the variable has the attribute.
  if (const auto *ipa = varDecl->getAttr<InitPriorityAttr>())
    addr.setInitPriorityAttr(builder.getI32IntegerAttr(ipa->getPriority()));

  auto emitOMPThreadPrivateRegistration = [&](mlir::Type valueTy) {
    if (!varDecl->hasAttr<OMPThreadPrivateDeclAttr>())
      return;

    if (addr.getCtorRegion().empty()) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      mlir::Block *block = builder.createBlock(&addr.getCtorRegion());
      CIRGenFunction::LexicalScope lexScope{cgf, addr.getLoc(),
                                            builder.getInsertionBlock()};
      lexScope.setAsGlobalInit();
      builder.setInsertionPointToStart(block);
      cir::YieldOp::create(builder, addr.getLoc());
    }

    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(&addr.getCtorRegion().front());

    mlir::Value declPtr = cgf.cgm.getAddrOfGlobalVar(varDecl);
    auto declPtrTy = mlir::cast<cir::PointerType>(declPtr.getType());
    auto expectedPtrTy =
        cir::PointerType::get(valueTy, declPtrTy.getAddrSpace());
    if (declPtrTy != expectedPtrTy)
      declPtr = builder.createBitcast(declPtr, expectedPtrTy);

    Address declAddr(declPtr, valueTy, getASTContext().getDeclAlign(varDecl));
    cgf.emitOMPThreadPrivateVarDefinition(varDecl, declAddr, performInit);
  };

  if (!ty->isReferenceType()) {
    mlir::Type valueTy = cgf.convertTypeForMem(ty);

    bool needsDtor = varDecl->needsDestruction(getASTContext()) ==
                     QualType::DK_cxx_destructor;
    bool isConstantStorage =
        varDecl->getType().isConstantStorage(getASTContext(), true, !needsDtor);
    // PerformInit, constant store invariant / destroy handled below.
    if (performInit) {
      emitDeclInit(cgf, varDecl, addr);
      // For constant storage, emit invariant.start in the ctor region after
      // initialization but before the yield.
      if (isConstantStorage) {
        CIRGenBuilderTy &builder = cgf.getBuilder();
        mlir::OpBuilder::InsertionGuard guard(builder);
        // Set insertion point to end of ctor region (before yield)
        if (!addr.getCtorRegion().empty()) {
          mlir::Block *block = &addr.getCtorRegion().back();
          // Find the yield op and insert before it
          mlir::Operation *yieldOp = block->getTerminator();
          if (yieldOp) {
            builder.setInsertionPoint(yieldOp);
            emitDeclInvariant(cgf, varDecl);
          }
        }
      }
    } else if (isConstantStorage) {
      emitDeclInvariant(cgf, varDecl);
    }

    emitOMPThreadPrivateRegistration(valueTy);

    if (!isConstantStorage)
      emitDeclDestroy(cgf, varDecl, addr);
    return;
  }

  // Reference type global variable initialization.
  // Note: OGCG does NOT call EmitInvariantStart for reference globals
  // (CGDeclCXX.cpp EmitDeclInit only calls EmitDeclInvariant for non-reference
  // types), so CIR is correct in not emitting invariant.start here.
  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *block = builder.createBlock(&addr.getCtorRegion());
    CIRGenFunction::LexicalScope lexScope{cgf, addr.getLoc(),
                                          builder.getInsertionBlock()};
    lexScope.setAsGlobalInit();
    builder.setInsertionPointToStart(block);
    auto getGlobal =
        builder.createGetGlobal(addr, varDecl->getTLSKind() != VarDecl::TLS_None);

    Address declAddr(getGlobal, getASTContext().getDeclAlign(varDecl));
    assert(performInit && "cannot have constant initializer which needs "
                          "destruction for reference");
    const Expr *init = varDecl->getInit();
    RValue rv = cgf.emitReferenceBindingToExpr(init);
    {
      mlir::OpBuilder::InsertionGuard innerGuard(builder);
      mlir::Operation *rvalueDefOp = rv.getValue().getDefiningOp();
      if (rvalueDefOp && rvalueDefOp->getBlock()) {
        mlir::Block *rvalSrcBlock = rvalueDefOp->getBlock();
        if (!rvalSrcBlock->empty() && isa<cir::YieldOp>(rvalSrcBlock->back())) {
          auto &front = rvalSrcBlock->front();
          getGlobal.getDefiningOp()->moveBefore(&front);
          auto yield = cast<cir::YieldOp>(rvalSrcBlock->back());
          builder.setInsertionPoint(yield);
        }
      }
      LValue lv = cgf.makeAddrLValue(declAddr, ty);
      cgf.emitStoreOfScalar(rv.getValue(), lv, /*isInit=*/true);
    }
    builder.setInsertionPointToEnd(block);
    cir::YieldOp::create(builder, addr->getLoc());
  }

  emitOMPThreadPrivateRegistration(cgf.convertTypeForMem(ty));
}

/// Build an apple-kext virtual call by indexing directly into the vtable
/// global. This is used under -fapple-kext when a qualified virtual call is
/// made (e.g. Base::method()) and bypasses the normal virtual dispatch
/// through the object's vptr.
static CIRGenCallee buildAppleKextVirtualCall(CIRGenFunction &cgf,
                                              GlobalDecl gd, mlir::Type ty,
                                              const CXXRecordDecl *rd) {
  assert(!cgf.cgm.getTarget().getCXXABI().isMicrosoft() &&
         "No kext in Microsoft ABI");
  CIRGenModule &cgm = cgf.cgm;
  CIRGenBuilderTy &builder = cgf.getBuilder();

  // Get the vtable global for the class.
  cir::GlobalOp vtableGlobal = cgm.getCXXABI().getAddrOfVTable(rd, CharUnits());
  assert(vtableGlobal && "BuildVirtualCall = kext vtbl pointer is null");

  // Compute the flat index into the vtable: method index + address point.
  uint64_t vtableIndex =
      cgm.getItaniumVTableContext().getMethodVTableIndex(gd);
  const VTableLayout &vtLayout =
      cgm.getItaniumVTableContext().getVTableLayout(rd);
  VTableLayout::AddressPointLocation addressPoint =
      vtLayout.getAddressPoint(BaseSubobject(rd, CharUnits::Zero()));
  vtableIndex += vtLayout.getVTableOffset(addressPoint.VTableIndex) +
                 addressPoint.AddressPointIndex;

  // Get a pointer to the vtable global.
  mlir::Location loc = cgf.getLoc(rd->getSourceRange());
  auto ptrTy = builder.getPointerTo(ty);
  auto ptrPtrTy = builder.getPointerTo(ptrTy);
  mlir::Value vtablePtr = cir::GetGlobalOp::create(
      builder, loc, cir::PointerType::get(vtableGlobal.getSymType()),
      vtableGlobal.getSymNameAttr());

  // Cast to pointer-to-function-pointer for flat indexing.
  mlir::Value castVtable = builder.createBitcast(vtablePtr, ptrPtrTy);

  // GEP to the function pointer slot.
  mlir::Value index =
      builder.getConstInt(loc, builder.getSInt64Ty(), vtableIndex);
  mlir::Value vfuncPtr = cir::PtrStrideOp::create(
      builder, loc, ptrPtrTy, castVtable, index,
      cir::GEPNoWrapFlags::inbounds);

  // Load the function pointer.
  mlir::Value vfunc =
      builder.createAlignedLoad(loc, ptrTy, vfuncPtr, cgf.getPointerAlign());

  std::optional<CIRGenPointerAuthInfo> pointerAuthInfo;
  if (const auto &schema =
          cgm.getCodeGenOpts().PointerAuth.CXXVirtualFunctionPointers) {
    GlobalDecl origGD =
        cgm.getItaniumVTableContext().findOriginalMethod(gd.getCanonicalDecl());

    uint64_t discriminator = 0;
    switch (schema.getOtherDiscrimination()) {
    case PointerAuthSchema::Discrimination::None:
      break;
    case PointerAuthSchema::Discrimination::Type:
      discriminator = cgm.getASTContext().getPointerAuthTypeDiscriminator(
          cast<CXXMethodDecl>(origGD.getDecl())->getType());
      break;
    case PointerAuthSchema::Discrimination::Decl:
      discriminator = cgm.getPointerAuthDeclDiscriminator(origGD);
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
  }

  const auto *fpt =
      cast<CXXMethodDecl>(gd.getDecl())->getType()->getAs<FunctionProtoType>();
  CIRGenCallee callee(
      CIRGenCalleeInfo(fpt, gd, pointerAuthInfo,
                       /*blendVTableSlot=*/true),
      vfunc.getDefiningOp());
  return callee;
}

CIRGenCallee
CIRGenFunction::buildAppleKextVirtualCall(const CXXMethodDecl *md,
                                          NestedNameSpecifier qual,
                                          mlir::Type ty) {
  const CXXRecordDecl *rd = qual.getAsRecordDecl();
  assert(rd && "BuildAppleKextVirtualCall - Qual must be record");
  if (const auto *dd = dyn_cast<CXXDestructorDecl>(md))
    return buildAppleKextVirtualDestructorCall(dd, Dtor_Complete, rd);

  return ::buildAppleKextVirtualCall(*this, md, ty, rd);
}

CIRGenCallee CIRGenFunction::buildAppleKextVirtualDestructorCall(
    const CXXDestructorDecl *dd, CXXDtorType type,
    const CXXRecordDecl *rd) {
  assert(dd->isVirtual() && type != Dtor_Base);
  // Compute the function type for the destructor.
  const CIRGenFunctionInfo &fInfo =
      cgm.getTypes().arrangeCXXStructorDeclaration(
          GlobalDecl(dd, Dtor_Complete));
  mlir::Type ty = cgm.getTypes().getFunctionType(fInfo);
  return ::buildAppleKextVirtualCall(*this, GlobalDecl(dd, type), ty, rd);
}
