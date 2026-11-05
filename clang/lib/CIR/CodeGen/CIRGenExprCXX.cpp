//===--- CIRGenExprCXX.cpp - Emit CIR Code for C++ expressions ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with code generation of C++ expressions
//
//===----------------------------------------------------------------------===//

#include "CIRGenCXXABI.h"
#include "CIRGenCleanup.h"
#include "CIRGenConstantEmitter.h"
#include "CIRGenFunction.h"
#include "CIRGenSanitizer.h"

#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/Basic/NoSanitizeList.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/MissingFeatures.h"

using namespace clang;
using namespace clang::CIRGen;

static CXXRecordDecl *getCXXRecord(const Expr *E) {
  QualType T = E->getType();
  if (const PointerType *PTy = T->getAs<PointerType>())
    T = PTy->getPointeeType();
  return T->castAsCXXRecordDecl();
}

namespace {
struct MemberCallInfo {
  RequiredArgs reqArgs;
  // Number of prefix arguments for the call. Ignores the `this` pointer.
  unsigned prefixSize;
};

static void maybeAttachCFINVCallAttrs(CIRGenFunction &cgf,
                                      cir::CIRCallOpInterface call,
                                      const CXXRecordDecl *rd,
                                      mlir::Location loc) {
  if (!cgf.sanOpts.has(SanitizerKind::CFINVCall) || !rd ||
      !rd->isCompleteDefinition() || !rd->isDynamicClass())
    return;

  if (!cgf.sanOpts.has(SanitizerKind::CFICastStrict))
    rd = leastDerivedClassWithSameLayout(rd);

  if (!cgf.cgm.getCodeGenOpts().SanitizeCfiCrossDso &&
      !cgf.cgm.hasHiddenLTOVisibility(rd))
    return;

  std::string typeName = rd->getQualifiedNameAsString();
  if (cgf.getContext().getNoSanitizeList().containsType(
          SanitizerKind::CFINVCall, typeName))
    return;

  CanQualType ty = cgf.cgm.getASTContext().getCanonicalTagType(rd);
  std::string outName;
  llvm::raw_string_ostream out(outName);
  cgf.cgm.getCXXABI().getMangleContext().mangleCanonicalTypeName(ty, out);

  StringRef handlerKind = "abort";
  if (cgf.cgm.getCodeGenOpts().SanitizeTrap.has(SanitizerKind::CFINVCall))
    handlerKind = "trap";
  else if (cgf.cgm.getCodeGenOpts().SanitizeRecover.has(
               SanitizerKind::CFINVCall))
    handlerKind = "recover";

  auto &builder = cgf.getBuilder();
  call->setAttr("cir.virtual.typeid", cgf.cgm.getStringAttrCached(out.str()));
  call->setAttr("cir.virtual.typetest_kind",
                cgf.cgm.getStringAttrCached("hidden"));
  call->setAttr("cir.virtual.cfi_vcall_kind",
                cgf.cgm.getStringAttrCached(handlerKind));
  cgf.emitSanitizerStatReportStub(loc, CFISanitizerStatKind::NVCall,
                                  call.getOperation());
  call->setAttr("cir.nosanitize", mlir::UnitAttr::get(builder.getContext()));
  setUBSanSourceLocationAttrs(cgf, call, loc);
  setUBSanTypeNameAttr(cgf, call, ty);
  setUBSanTypeDescriptorAttrs(cgf, call, ty);
  call->setDiscardableAttr("cir.ubsan.type_check_kind",
                           builder.getI8IntegerAttr(1));
  if (cgf.cgm.getCodeGenOpts().SanitizeMinimalRuntime)
    call->setAttr("cir.virtual.cfi_minimal_runtime",
                  mlir::UnitAttr::get(builder.getContext()));
  if (cgf.cgm.getCodeGenOpts().SanitizeHandlerPreserveAllRegs)
    call->setAttr("cir.virtual.cfi_preserve_all_regs",
                  mlir::UnitAttr::get(builder.getContext()));
}
} // namespace

static MemberCallInfo commonBuildCXXMemberOrOperatorCall(
    CIRGenFunction &cgf, const CXXMethodDecl *md, mlir::Value thisPtr,
    mlir::Value implicitParam, QualType implicitParamTy, const CallExpr *ce,
    CallArgList &args, CallArgList *rtlArgs) {
  assert(ce == nullptr || isa<CXXMemberCallExpr>(ce) ||
         isa<CXXOperatorCallExpr>(ce));
  assert(md->isImplicitObjectMemberFunction() &&
         "Trying to emit a member or operator call expr on a static method!");

  // OGCG takes a GlobalDecl here instead of CXXMethodDecl*, preserving
  // ctor/dtor variant info for getThisArgumentTypeForMethod. CIR loses
  // this variant info which can affect ABI-level 'this' type derivation
  // for base-class constructors with virtual bases.
  assert(!cir::MissingFeatures::commonBuildCXXMemberCallGlobalDecl());

  // Push the this ptr.
  const CXXRecordDecl *rd =
      cgf.cgm.getCXXABI().getThisArgumentTypeForMethod(md);
  args.add(RValue::get(thisPtr), cgf.getTypes().deriveThisType(rd, md));

  // If there is an implicit parameter (e.g. VTT), emit it.
  if (implicitParam) {
    args.add(RValue::get(implicitParam), implicitParamTy);
  }

  const auto *fpt = md->getType()->castAs<FunctionProtoType>();
  RequiredArgs required =
      RequiredArgs::getFromProtoWithExtraSlots(fpt, args.size());
  unsigned prefixSize = args.size() - 1;

  // Add the rest of the call args
  if (rtlArgs) {
    // Special case: if the caller emitted the arguments right-to-left already
    // (prior to emitting the *this argument), we're done. This happens for
    // assignment operators.
    args.addFrom(*rtlArgs);
  } else if (ce) {
    // Special case: skip first argument of CXXOperatorCall (it is "this").
    unsigned argsToSkip = 0;
    if (const auto *op = dyn_cast<CXXOperatorCallExpr>(ce)) {
      if (const auto *m = dyn_cast<CXXMethodDecl>(op->getCalleeDecl()))
        argsToSkip =
            static_cast<unsigned>(!m->isExplicitObjectMemberFunction());
    }
    cgf.emitCallArgs(args, fpt, drop_begin(ce->arguments(), argsToSkip),
                     ce->getDirectCallee());
  } else {
    assert(
        fpt->getNumParams() == 0 &&
        "No CallExpr specified for function with non-zero number of arguments");
  }

  //  return {required, prefixSize};
  return {required, prefixSize};
}

RValue
CIRGenFunction::emitCXXMemberPointerCallExpr(const CXXMemberCallExpr *ce,
                                             ReturnValueSlot returnValue) {
  const BinaryOperator *bo =
      cast<BinaryOperator>(ce->getCallee()->IgnoreParens());
  const Expr *baseExpr = bo->getLHS();
  const Expr *memFnExpr = bo->getRHS();

  const auto *mpt = memFnExpr->getType()->castAs<MemberPointerType>();
  const auto *fpt = mpt->getPointeeType()->castAs<FunctionProtoType>();

  // Emit the 'this' pointer.
  Address thisAddr = Address::invalid();
  if (bo->getOpcode() == BO_PtrMemI)
    thisAddr =
        emitPointerWithAlignment(baseExpr, /*baseInfo=*/nullptr,
                                 /*tbaaInfo=*/nullptr, /*isKnownNonNull=*/true);
  else
    thisAddr = emitLValue(baseExpr).getAddress().setKnownNonNull();

  // Get the member function pointer.
  mlir::Value memFnPtr = emitScalarExpr(memFnExpr);

  // Resolve the member function pointer to the actual callee and adjust the
  // "this" pointer for call.
  // OGCG delegates to CGM.getCXXABI().EmitLoadOfMemberFunctionPointer() which
  // is ABI-specific (different for Itanium vs MSVC). CIR uses a high-level
  // cir.get_method op here, deferring ABI-specific resolution to
  // ItaniumCXXABI::lowerGetMethod in the target lowering pass.
  mlir::Location loc = getLoc(ce->getExprLoc());
  auto [/*mlir::Value*/ calleePtr, /*mlir::Value*/ adjustedThis,
        /*mlir::Value*/ ptrauthDiscriminator] =
      builder.createGetMethod(loc, memFnPtr, thisAddr.getPointer(),
                              cgm.uIntPtrTy);
  auto *getMethodOp = calleePtr.getDefiningOp();
  const auto *rd = mpt->getMostRecentCXXRecordDecl();
  CharUnits vtablePtrAlign = cgm.getDynamicOffsetAlignment(
      thisAddr.getAlignment(), rd, getPointerAlign());
  getMethodOp->setAttr("cir.memptr.vtable_ptr_align",
                       getI64AttrCached(vtablePtrAlign.getQuantity()));

  bool shouldEmitVirtualTypeMetadata =
      cgm.getCodeGenOpts().DevirtualizeSpeculatively ||
      (cgm.getCodeGenOpts().WholeProgramVTables &&
       !cgm.alwaysHasLTOVisibilityPublic(rd));
  if (shouldEmitVirtualTypeMetadata) {
    bool virtualTypeIdIsDistinct =
        !isExternallyVisible(QualType(mpt, 0)->getLinkage());
    getMethodOp->setAttr(
        "cir.virtual.typeid",
        cgm.getStringAttrCached(
            cgm.createStableMetadataIdentifierKeyForVirtualMemPtrType(
                QualType(mpt, 0))));
    if (virtualTypeIdIsDistinct)
      getMethodOp->setAttr("cir.virtual.typeid_distinct",
                           mlir::UnitAttr::get(builder.getContext()));

    bool shouldEmitVirtualTypeCheckedLoad =
        cgm.getCodeGenOpts().VirtualFunctionElimination &&
        cgm.hasHiddenLTOVisibility(rd);
    if (shouldEmitVirtualTypeCheckedLoad) {
      getMethodOp->setAttr("cir.virtual.typecheckedload",
                           mlir::UnitAttr::get(builder.getContext()));
    } else {
      bool usePublicTypeTest =
          rd->isExternallyVisible() && !cgm.hasHiddenLTOVisibility(rd);
      getMethodOp->setAttr(
          "cir.virtual.typetest_kind",
          cgm.getStringAttrCached(usePublicTypeTest ? "public" : "hidden"));
    }
  }

  // Check if CFI mfcall is enabled (matching OGCG ItaniumCXXABI.cpp:707-708).
  if (std::optional<CIRGenPointerAuthInfo> pointerAuth =
          cgm.getMemberFunctionPointerAuthInfo(QualType(mpt, 0))) {
    getMethodOp->setAttr(
        "cir.memptr.nonvirtual_ptrauth_discriminator",
        builder.getI64IntegerAttr(pointerAuth->discriminator));
  }
  if (cgm.getLangOpts().Sanitize.has(SanitizerKind::CFIMFCall) &&
      cgm.hasHiddenLTOVisibility(rd)) {
    // Check for CFI-unchecked callee types (matching OGCG
    // ItaniumCXXABI.cpp:711-716).
    bool shouldCheck = true;
    if (bo->isPtrMemOp() &&
        bo->getRHS()
            ->getType()
            ->hasPointeeToCFIUncheckedCalleeFunctionType())
      shouldCheck = false;

    if (shouldCheck) {
      auto *ctx = builder.getContext();
      // Match OGCG's EmitCheckSourceLocation(E->getBeginLoc()), where E is
      // the underlying member-pointer BinaryOperator rather than the full
      // CXXMemberCallExpr. For "(s->*p)()", that means pointing at "s", not
      // the opening "(".
      mlir::Location checkLoc = getLoc(bo->getBeginLoc());
      auto fileLoc = mlir::dyn_cast<mlir::FileLineColLoc>(checkLoc);
      if (!fileLoc)
        fileLoc = checkLoc->findInstanceOf<mlir::FileLineColLoc>();
      if (fileLoc) {
        getMethodOp->setDiscardableAttr("cir.ubsan.source_file",
                                        fileLoc.getFilename());
        getMethodOp->setDiscardableAttr("cir.ubsan.source_line",
                                        builder.getI32IntegerAttr(
                                            fileLoc.getLine()));
        getMethodOp->setDiscardableAttr("cir.ubsan.source_column",
                                        builder.getI32IntegerAttr(
                                            fileLoc.getColumn()));
      }

      SmallString<32> typeName;
      cgm.getDiags().ConvertArgToString(
          DiagnosticsEngine::ak_qualtype, (intptr_t)QualType(mpt, 0).getAsOpaquePtr(),
          StringRef(), StringRef(), {}, typeName, {});
      getMethodOp->setDiscardableAttr("cir.ubsan.type_name",
                                      builder.getStringAttr(typeName));

      uint16_t typeKind = 0xFFFF;
      uint16_t typeInfo = 0;
      auto i16Ty = mlir::IntegerType::get(ctx, 16);
      getMethodOp->setDiscardableAttr("cir.ubsan.type_kind",
                                      mlir::IntegerAttr::get(i16Ty, typeKind));
      getMethodOp->setDiscardableAttr("cir.ubsan.type_info",
                                      mlir::IntegerAttr::get(i16Ty, typeInfo));

      // Virtual type ID:
      // CreateMetadataIdentifierForVirtualMemPtrType(QualType(MPT, 0))
      std::string virtualTypeId =
          cgm.createMetadataIdentifierStringForVirtualMemPtrType(
              QualType(mpt, 0));
      getMethodOp->setAttr("cir.cfi_mfcall_virtual_typeid",
                           builder.getStringAttr(virtualTypeId));

      // OGCG skips the non-virtual member-function-pointer check path for
      // incomplete record types. The virtual memptr type test still applies.
      SmallVector<mlir::Attribute> nvTypeIds;
      if (rd->hasDefinition()) {
        for (const CXXRecordDecl *Base : cgm.getMostBaseClasses(rd)) {
          QualType baseMPT = getContext().getMemberPointerType(
              mpt->getPointeeType(), /*Qualifier=*/std::nullopt,
              Base->getCanonicalDecl());
          nvTypeIds.push_back(builder.getStringAttr(
              cgm.createMetadataIdentifierStringForType(baseMPT)));
        }
      }
      getMethodOp->setAttr("cir.cfi_mfcall_nonvirtual_typeids",
                           builder.getArrayAttr(nvTypeIds));

      // Trap mode flag.
      if (cgm.getCodeGenOpts().SanitizeTrap.has(SanitizerKind::CFIMFCall)) {
        getMethodOp->setAttr("cir.cfi_mfcall_trap",
                             mlir::UnitAttr::get(builder.getContext()));
      } else {
        // Non-trap mode: propagate recover/minimal/preserve state so
        // the target-lowering pass can select the right handler variant
        // without needing CodeGenOpts (which aren't fully reconstructed
        // from module attributes in the CIR -> LLVM pipeline).
        if (cgm.getCodeGenOpts().SanitizeRecover.has(
                SanitizerKind::CFIMFCall))
          getMethodOp->setAttr("cir.cfi_mfcall_recover",
                               mlir::UnitAttr::get(builder.getContext()));
        if (cgm.getCodeGenOpts().SanitizeMinimalRuntime)
          getMethodOp->setAttr("cir.cfi_mfcall_minimal_runtime",
                               mlir::UnitAttr::get(builder.getContext()));
        if (cgm.getCodeGenOpts().SanitizeHandlerPreserveAllRegs)
          getMethodOp->setAttr("cir.cfi_mfcall_preserve_all_regs",
                               mlir::UnitAttr::get(builder.getContext()));
      }
      // Nomerge applies to both trap and non-trap modes.
      if (!cgm.getCodeGenOpts().SanitizeMergeHandlers.has(
              SanitizerKind::SO_CFIMFCall))
        getMethodOp->setAttr("cir.cfi_mfcall_nomerge",
                             mlir::UnitAttr::get(builder.getContext()));
    }
  }

  // Prepare the call arguments.
  // Use the proper class pointer type for 'this' (matching OGCG
  // CGExprCXX.cpp:476-479) so that downstream attribute-setting code can
  // compute dereferenceable size and align from the pointee type.
  CanQualType classType = cgm.getASTContext().getCanonicalTagType(rd);
  QualType thisType = getContext().getPointerType(classType);
  CallArgList argsList;
  argsList.add(RValue::get(adjustedThis), thisType);
  emitCallArgs(argsList, fpt, ce->arguments());

  RequiredArgs required = RequiredArgs::getFromProtoWithExtraSlots(fpt, 1);

  // OGCG checks for null member function pointer before dispatch.
  assert(!cir::MissingFeatures::memberPointerNullCheck());

  // Build the call.
  // OGCG passes MustTailCall flag to EmitCall here (CGExprCXX.cpp:485-488).
  bool isMustTail = (ce == mustTailCall);
  CIRGenCallee callee(
      CIRGenCalleeInfo(fpt, cgm.getMemberFunctionPointerAuthInfo(QualType(mpt, 0))),
      calleePtr.getDefiningOp());
  cir::CIRCallOpInterface callOp;
  RValue result = emitCall(cgm.getTypes().arrangeCXXMethodCall(
                               argsList, fpt, required, /*PrefixSize=*/0),
                           callee, returnValue, argsList, &callOp, loc,
                           isMustTail, /*staticChain=*/{}, ce->getExprLoc());
  if (callOp && callee.getAbstractInfo().getPointerAuthInfo()) {
    if (auto directCall = mlir::dyn_cast<cir::CallOp>(callOp.getOperation()))
      directCall.getArgsMutable().append(ptrauthDiscriminator);
    else if (auto tryCall =
                 mlir::dyn_cast<cir::TryCallOp>(callOp.getOperation()))
      tryCall.getArgsMutable().append(ptrauthDiscriminator);
    else
      llvm_unreachable("unexpected CIR call op kind for runtime ptrauth");
    callOp->setAttr("cir.runtime_ptrauth",
                    mlir::UnitAttr::get(builder.getContext()));
  }
  if (callOp)
    maybeAttachCallGraphSectionAttrs(callOp, QualType(fpt, 0));
  return result;
}

RValue CIRGenFunction::emitCXXMemberOrOperatorMemberCallExpr(
    const CallExpr *ce, const CXXMethodDecl *md, ReturnValueSlot returnValue,
    bool hasQualifier, NestedNameSpecifier qualifier, bool isArrow,
    const Expr *base) {
  assert(isa<CXXMemberCallExpr>(ce) || isa<CXXOperatorCallExpr>(ce));

  // Compute the object pointer.
  bool canUseVirtualCall = md->isVirtual() && !hasQualifier;

  const CXXMethodDecl *devirtualizedMethod = nullptr;
  if (canUseVirtualCall &&
      md->getDevirtualizedMethod(base, getLangOpts().AppleKext)) {
    const CXXRecordDecl *bestDynamicDecl = base->getBestDynamicClassType();
    devirtualizedMethod = md->getCorrespondingMethodInClass(bestDynamicDecl);
    assert(devirtualizedMethod);
    const CXXRecordDecl *devirtualizedClass = devirtualizedMethod->getParent();
    const Expr *inner = base->IgnoreParenBaseCasts();
    if (devirtualizedMethod->getReturnType().getCanonicalType() !=
        md->getReturnType().getCanonicalType())
      // If the return types are not the same, this might be a case where more
      // code needs to run to compensate for it. For example, the derived
      // method might return a type that inherits form from the return
      // type of MD and has a prefix.
      // For now we just avoid devirtualizing these covariant cases.
      devirtualizedMethod = nullptr;
    else if (getCXXRecord(inner) == devirtualizedClass)
      // If the class of the Inner expression is where the dynamic method
      // is defined, build the this pointer from it.
      base = inner;
    else if (getCXXRecord(base) != devirtualizedClass) {
      // If the method is defined in a class that is not the best dynamic
      // one or the one of the full expression, we would have to build
      // a derived-to-base cast to compute the correct this pointer, but
      // we don't have support for that yet, so do a virtual call.
      devirtualizedMethod = nullptr;
    }
  }

  bool trivialForCodegen =
      md->isTrivial() || (md->isDefaulted() && md->getParent()->isUnion());
  bool trivialAssignment =
      trivialForCodegen &&
      (md->isCopyAssignmentOperator() || md->isMoveAssignmentOperator()) &&
      !md->getParent()->mayInsertExtraPadding();

  // C++17 demands that we evaluate the RHS of a (possibly-compound) assignment
  // operator before the LHS.
  CallArgList rtlArgStorage;
  CallArgList *rtlArgs = nullptr;
  LValue trivialAssignmentRHS;
  if (auto *oce = dyn_cast<CXXOperatorCallExpr>(ce)) {
    if (oce->isAssignmentOp()) {
      if (trivialAssignment) {
        trivialAssignmentRHS = emitLValue(ce->getArg(1));
      } else {
        rtlArgs = &rtlArgStorage;
        emitCallArgs(*rtlArgs, md->getType()->castAs<FunctionProtoType>(),
                     drop_begin(ce->arguments(), 1), ce->getDirectCallee(),
                     /*ParamsToSkip*/ 0, EvaluationOrder::ForceRightToLeft);
      }
    }
  }

  LValue thisPtr;
  if (isArrow) {
    LValueBaseInfo baseInfo;
    TBAAAccessInfo tbaaInfo;
    Address thisValue =
        emitPointerWithAlignment(base, &baseInfo, &tbaaInfo);
    thisPtr = makeAddrLValue(thisValue, base->getType()->getPointeeType(),
                                baseInfo, tbaaInfo);
  } else {
    thisPtr = emitLValue(base);
  }

  if (isa<CXXConstructorDecl>(md)) {
    cgm.errorNYI(ce->getSourceRange(),
                 "emitCXXMemberOrOperatorMemberCallExpr: constructor call");
    return RValue::get(nullptr);
  }

  if (trivialForCodegen) {
    if (isa<CXXDestructorDecl>(md))
      return RValue::get(nullptr);

    if (trivialAssignment) {
      LValue rhs = isa<CXXOperatorCallExpr>(ce) ? trivialAssignmentRHS
                                                : emitLValue(*ce->arg_begin());
      bool isVolatile = hasVolatileMember(ce->getType());
      emitAggregateCopy(thisPtr, rhs, ce->getType(), AggValueSlot::MayOverlap,
                        isVolatile);
      return RValue::get(thisPtr.getPointer());
    }

    assert(md->getParent()->mayInsertExtraPadding() &&
           "unknown trivial member function");
  }

  // Emit UBSan type check for member calls, matching OGCG CGExprCXX.cpp:359.
  if (sanitizePerformTypeCheck()) {
    SanitizerSet skippedChecks;
    if (const auto *cmce = dyn_cast<CXXMemberCallExpr>(ce)) {
      auto *ioa = cmce->getImplicitObjectArgument();
      auto isWrappedThis = [](const Expr *obj) {
        const Expr *base = obj;
        while (!isa<CXXThisExpr>(base)) {
          if (isa<CXXDynamicCastExpr>(base))
            return false;

          if (const auto *ce = dyn_cast<CastExpr>(base))
            base = ce->getSubExpr();
          else if (const auto *pe = dyn_cast<ParenExpr>(base))
            base = pe->getSubExpr();
          else if (const auto *uo = dyn_cast<UnaryOperator>(base)) {
            if (uo->getOpcode() != UO_Extension)
              return false;
            base = uo->getSubExpr();
          } else {
            return false;
          }
        }
        return true;
      };
      bool isImplicitObjectCXXThis = isWrappedThis(ioa);
      if (isImplicitObjectCXXThis)
        skippedChecks.set(SanitizerKind::Alignment, true);
      if (isImplicitObjectCXXThis || isa<DeclRefExpr>(ioa))
        skippedChecks.set(SanitizerKind::Null, true);
    }
    // Use the devirtualized method's parent class for the type check hash,
    // not the base class's method. Matches OGCG CGExprCXX.cpp:361 which
    // uses CalleeDecl->getParent() (the derived class for devirtualized
    // calls). Using md->getParent() would always hash to the base class.
    const CXXRecordDecl *typeCheckClass =
        devirtualizedMethod ? devirtualizedMethod->getParent()
                            : md->getParent();
    emitTypeCheck(
        TCK_MemberCall, ce->getExprLoc(), thisPtr.getPointer(),
        getContext().getCanonicalTagType(typeCheckClass),
        /*Alignment=*/CharUnits::Zero(), skippedChecks);
  }

  // Compute the function type we're calling
  const CXXMethodDecl *calleeDecl =
      devirtualizedMethod ? devirtualizedMethod : md;
  const CIRGenFunctionInfo *fInfo = nullptr;
  if (const auto *dtor = dyn_cast<CXXDestructorDecl>(calleeDecl))
    fInfo = &cgm.getTypes().arrangeCXXStructorDeclaration(
        GlobalDecl(dtor, Dtor_Complete));
  else
    fInfo = &cgm.getTypes().arrangeCXXMethodDeclaration(calleeDecl);

  cir::FuncType ty = cgm.getTypes().getFunctionType(*fInfo);

  (void)0; // Sanitizer-specific CIR parity work remains NYI in this path.
  // OGCG checks SanitizerKind::CFINVCall for non-virtual calls to dynamic
  // classes, loading the vtable pointer and emitting VTablePtrCheckForCall.
  // cfiNVCall -- CFI non-virtual call checks not yet needed.

  // C++ [class.virtual]p12:
  //   Explicit qualification with the scope operator (5.1) suppresses the
  //   virtual call mechanism.
  //
  // We also don't emit a virtual call if the base expression has a record type
  // because then we know what the type is.
  bool useVirtualCall = canUseVirtualCall && !devirtualizedMethod;

  if (const auto *dtor = dyn_cast<CXXDestructorDecl>(calleeDecl)) {
    assert(ce->arg_begin() == ce->arg_end() &&
           "Destructor shouldn't have explicit parameters");
    assert(returnValue.isNull() && "Destructor shouldn't have return value");
    if (useVirtualCall) {
      cgm.getCXXABI().emitVirtualDestructorCall(*this, dtor, Dtor_Complete,
                                                thisPtr.getAddress(),
                                                cast<CXXMemberCallExpr>(ce));
    } else {
      GlobalDecl globalDecl(dtor, Dtor_Complete);
      CIRGenCallee callee;
      if (getLangOpts().AppleKext && dtor->isVirtual() && hasQualifier)
        callee = buildAppleKextVirtualCall(dtor, qualifier, ty);
      else if (!devirtualizedMethod) {
        callee = CIRGenCallee::forDirect(
            cgm.getAddrOfCXXStructor(globalDecl, fInfo, ty), globalDecl);
      } else {
        callee = CIRGenCallee::forDirect(
            cgm.getAddrOfFunction(globalDecl, ty), globalDecl);
      }

      QualType thisTy =
          isArrow ? base->getType()->getPointeeType() : base->getType();
      // CIRGen does not pass CallOrInvoke here (different from OG LLVM codegen)
      // because in practice it always null even in OG.
      emitCXXDestructorCall(globalDecl, callee, thisPtr.getPointer(), thisTy,
                            /*implicitParam=*/nullptr,
                            /*implicitParamTy=*/QualType(), ce);
    }
    return RValue::get(nullptr);
  }

  CIRGenCallee callee;
  if (useVirtualCall) {
    callee = CIRGenCallee::forVirtual(ce, md, thisPtr.getAddress(), ty);
  } else {
    (void)0; // Sanitizer-specific CIR parity work remains NYI in this path.
    if (getLangOpts().AppleKext && md->isVirtual() && hasQualifier)
      callee = buildAppleKextVirtualCall(md, qualifier, ty);
    else if (!devirtualizedMethod)
      callee = CIRGenCallee::forDirect(cgm.getAddrOfFunction(calleeDecl, ty),
                                       GlobalDecl(calleeDecl));
    else
      callee = CIRGenCallee::forDirect(
          cgm.getAddrOfFunction(devirtualizedMethod, ty),
          GlobalDecl(devirtualizedMethod));
  }

  if (md->isVirtual()) {
    Address newThisAddr =
        cgm.getCXXABI().adjustThisArgumentForVirtualFunctionCall(
            *this, calleeDecl, thisPtr.getAddress(), useVirtualCall);
    thisPtr.setAddress(newThisAddr);
  }

  cir::CIRCallOpInterface callOp;
  auto result = emitCXXMemberOrOperatorCall(
      calleeDecl, callee, returnValue, thisPtr.getPointer(),
      /*ImplicitParam=*/nullptr, QualType(), ce, rtlArgs, &callOp);
  if (!useVirtualCall && callOp && md->getParent()->isDynamicClass())
    maybeAttachCFINVCallAttrs(*this, callOp, calleeDecl->getParent(),
                              getLoc(ce->getBeginLoc()));
  return result;
}

RValue
CIRGenFunction::emitCXXOperatorMemberCallExpr(const CXXOperatorCallExpr *e,
                                              const CXXMethodDecl *md,
                                              ReturnValueSlot returnValue) {
  assert(md->isImplicitObjectMemberFunction() &&
         "Trying to emit a member call expr on a static method!");
  return emitCXXMemberOrOperatorMemberCallExpr(
      e, md, returnValue, /*HasQualifier=*/false, /*Qualifier=*/std::nullopt,
      /*IsArrow=*/false, e->getArg(0));
}

RValue CIRGenFunction::emitCXXMemberOrOperatorCall(
    const CXXMethodDecl *md, const CIRGenCallee &callee,
    ReturnValueSlot returnValue, mlir::Value thisPtr, mlir::Value implicitParam,
    QualType implicitParamTy, const CallExpr *ce, CallArgList *rtlArgs,
    cir::CIRCallOpInterface *outCallOp) {
  const auto *fpt = md->getType()->castAs<FunctionProtoType>();
  CallArgList args;
  MemberCallInfo callInfo = commonBuildCXXMemberOrOperatorCall(
      *this, md, thisPtr, implicitParam, implicitParamTy, ce, args, rtlArgs);
  auto &fnInfo = cgm.getTypes().arrangeCXXMethodCall(
      args, fpt, callInfo.reqArgs, callInfo.prefixSize);
  assert((ce || currSrcLoc) && "expected source location");
  mlir::Location loc = ce ? getLoc(ce->getExprLoc()) : *currSrcLoc;
  bool isMustTail = (ce && mustTailCall == ce);
  cir::CIRCallOpInterface callOp;
  auto result =
      emitCall(fnInfo, callee, returnValue, args, &callOp, loc, isMustTail,
               /*staticChain=*/{}, ce ? ce->getExprLoc() : SourceLocation());

  // Attach AST CallExpr information for later passes (e.g. IdiomRecognizer).
  if (callOp && ce)
    callOp->setAttr("ast",
                    cir::ASTCallExprAttr::get(&cgm.getMLIRContext(), ce));
  if (outCallOp)
    *outCallOp = callOp;

  return result;
}

static void emitNullBaseClassInitialization(CIRGenFunction &cgf,
                                            Address destPtr,
                                            const CXXRecordDecl *base) {
  if (base->isEmpty())
    return;

  const ASTRecordLayout &layout = cgf.getContext().getASTRecordLayout(base);
  CharUnits nvSize = layout.getNonVirtualSize();

  // We cannot simply zero-initialize the entire base sub-object if vbptrs are
  // present, they are initialized by the most derived class before calling the
  // constructor.
  SmallVector<std::pair<CharUnits, CharUnits>, 1> stores;
  stores.emplace_back(CharUnits::Zero(), nvSize);

  // Each store is split by the existence of a vbptr.
  // vbptr splitting only applies to the MS CXXABI. CIR uses Itanium ABI.
  if (cgf.getTarget().getCXXABI().isMicrosoft()) {
    cgf.cgm.errorNYI("MSVC vbptr splitting in base class initialization");
    return;
  }

  // If the type contains a pointer to data member we can't memset it to zero.
  // Instead, create a null constant and copy it to the destination.
  // TODO: there are other patterns besides zero that we can usefully memset,
  // like -1, which happens to be the pattern used by member-pointers.
  // TODO: isZeroInitializable can be over-conservative in the case where a
  // virtual base contains a member pointer.
  mlir::TypedAttr nullConstantForBase = cgf.cgm.emitNullConstantForBase(base);

  // Memset or store the null constant to initialize the base class.
  // For types containing member pointers, the null constant may be non-zero
  // (e.g., -1 for member-pointers), but CIR's ConstantOp handles this.
  // TODO(cir): When the MS CXXABI is supported, we will need to iterate over
  // `stores` and create a separate memset for each one. For now, we know that
  // there will only be one store and it will begin at offset zero, so that
  // simplifies this code considerably.
  assert(stores.size() == 1 && "Expected only one store");
  assert(stores[0].first == CharUnits::Zero() &&
         "Expected store to begin at offset zero");
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(base->getBeginLoc());

  // The null constant may have the base subobject type (without tail padding),
  // while destPtr has the complete object type. Cast the pointer to match.
  mlir::Type constantType = nullConstantForBase.getType();
  Address storeAddr = destPtr;
  if (destPtr.getElementType() != constantType) {
    auto ptrTy = cir::PointerType::get(constantType);
    storeAddr = Address(builder.createBitcast(loc, destPtr.getPointer(), ptrTy),
                        constantType, destPtr.getAlignment());
  }
  builder.createStore(loc, builder.getConstant(loc, nullConstantForBase),
                      storeAddr);
}

void CIRGenFunction::emitCXXConstructExpr(const CXXConstructExpr *e,
                                          AggValueSlot dest) {
  assert(!dest.isIgnored() && "Must have a destination!");
  const CXXConstructorDecl *cd = e->getConstructor();

  // If we require zero initialization before (or instead of) calling the
  // constructor, as can be the case with a non-user-provided default
  // constructor, emit the zero initialization now, unless destination is
  // already zeroed.
  if (e->requiresZeroInitialization() && !dest.isZeroed()) {
    switch (e->getConstructionKind()) {
    case CXXConstructionKind::Delegating:
    case CXXConstructionKind::Complete:
      emitNullInitialization(getLoc(e->getSourceRange()), dest.getAddress(),
                             e->getType());
      break;
    case CXXConstructionKind::VirtualBase:
    case CXXConstructionKind::NonVirtualBase:
      emitNullBaseClassInitialization(*this, dest.getAddress(),
                                      cd->getParent());
      break;
    }
  }

  // If this is a call to a trivial default constructor, do nothing.
  if (cd->isTrivial() && cd->isDefaultConstructor())
    return;

  // Elide the constructor if we're constructing from a temporary
  if (getLangOpts().ElideConstructors && e->isElidable()) {
    // FIXME: This only handles the simplest case, where the source object is
    //        passed directly as the first argument to the constructor. This
    //        should also handle stepping through implicit casts and conversion
    //        sequences which involve two steps, with a conversion operator
    //        follwed by a converting constructor.
    const Expr *srcObj = e->getArg(0);
    assert(srcObj->isTemporaryObject(getContext(), cd->getParent()));
    assert(
        getContext().hasSameUnqualifiedType(e->getType(), srcObj->getType()));
    emitAggExpr(srcObj, dest);
    return;
  }

  if (const ArrayType *arrayType = getContext().getAsArrayType(e->getType())) {
    emitCXXAggrConstructorCall(cd, arrayType, dest.getAddress(), e,
                               dest.isSanitizerChecked());
  } else {

    clang::CXXCtorType type = Ctor_Complete;
    bool forVirtualBase = false;
    bool delegating = false;

    switch (e->getConstructionKind()) {
    case CXXConstructionKind::Complete:
      type = Ctor_Complete;
      break;
    case CXXConstructionKind::Delegating:
      // We should be emitting a constructor; GlobalDecl will assert this
      type = curGD.getCtorType();
      delegating = true;
      break;
    case CXXConstructionKind::VirtualBase:
      forVirtualBase = true;
      [[fallthrough]];
    case CXXConstructionKind::NonVirtualBase:
      type = Ctor_Base;
      break;
    }

    emitCXXConstructorCall(cd, type, forVirtualBase, delegating, dest, e);
  }
}

static CharUnits calculateCookiePadding(CIRGenFunction &cgf,
                                        const CXXNewExpr *e) {
  if (!e->isArray())
    return CharUnits::Zero();

  // No cookie is required if the operator new[] being used is the
  // reserved placement operator new[].
  if (e->getOperatorNew()->isReservedGlobalPlacementOperator())
    return CharUnits::Zero();

  return cgf.cgm.getCXXABI().getArrayCookieSize(e);
}

static mlir::Value emitCXXNewAllocSize(CIRGenFunction &cgf, const CXXNewExpr *e,
                                       unsigned minElements,
                                       mlir::Value &numElements,
                                       mlir::Value &sizeWithoutCookie) {
  QualType type = e->getAllocatedType();
  mlir::Location loc = cgf.getLoc(e->getSourceRange());

  if (!e->isArray()) {
    CharUnits typeSize = cgf.getContext().getTypeSizeInChars(type);
    sizeWithoutCookie = cgf.getBuilder().getConstant(
        loc, cir::IntAttr::get(cgf.sizeTy, typeSize.getQuantity()));
    return sizeWithoutCookie;
  }

  // The width of size_t.
  unsigned sizeWidth = cgf.cgm.getDataLayout().getTypeSizeInBits(cgf.sizeTy);

  // The number of elements can be have an arbitrary integer type;
  // essentially, we need to multiply it by a constant factor, add a
  // cookie size, and verify that the result is representable as a
  // size_t.  That's just a gloss, though, and it's wrong in one
  // important way: if the count is negative, it's an error even if
  // the cookie size would bring the total size >= 0.
  //
  // If the array size is constant, Sema will have prevented negative
  // values and size overflow.

  // Compute the constant factor.
  llvm::APInt arraySizeMultiplier(sizeWidth, 1);
  while (const ConstantArrayType *cat =
             cgf.getContext().getAsConstantArrayType(type)) {
    type = cat->getElementType();
    arraySizeMultiplier *= cat->getSize();
  }

  CharUnits typeSize = cgf.getContext().getTypeSizeInChars(type);
  llvm::APInt typeSizeMultiplier(sizeWidth, typeSize.getQuantity());
  typeSizeMultiplier *= arraySizeMultiplier;

  // Figure out the cookie size.
  llvm::APInt cookieSize(sizeWidth,
                         calculateCookiePadding(cgf, e).getQuantity());

  // This will be a size_t.
  mlir::Value size;

  // Emit the array size expression.
  // We multiply the size of all dimensions for NumElements.
  // e.g for 'int[2][3]', ElemType is 'int' and NumElements is 6.
  const Expr *arraySize = *e->getArraySize();
  bool isSigned =
      (*e->getArraySize())->getType()->isSignedIntegerOrEnumerationType();
  mlir::Attribute constNumElements =
      ConstantEmitter(cgf.cgm, &cgf)
          .tryEmitAbstract(arraySize, arraySize->getType());
  if (constNumElements) {
    // Get an APInt from the constant
    const llvm::APInt &count =
        mlir::cast<cir::IntAttr>(constNumElements).getValue();

    unsigned numElementsWidth = count.getBitWidth();
    bool hasAnyOverflow = false;

    // If 'count' was a negative number, it's an overflow.
    if (isSigned && count.isNegative())
      hasAnyOverflow = true;

    // We want to do all this arithmetic in size_t. If numElements is
    // wider than that, check whether it's already too big, and if so,
    // overflow.
    else if (numElementsWidth > sizeWidth &&
             numElementsWidth - sizeWidth > count.countl_zero())
      hasAnyOverflow = true;

    // Okay, compute a count at the right width.
    llvm::APInt adjustedCount = count.zextOrTrunc(sizeWidth);

    // If there is a brace-initializer, we cannot allocate fewer elements than
    // there are initializers. If we do, that's treated like an overflow.
    if (adjustedCount.ult(minElements))
      hasAnyOverflow = true;

    // Scale numElements by that.  This might overflow, but we don't
    // care because it only overflows if allocationSize does too, and
    // if that overflows then we shouldn't use this.
    // This emits a constant that may not be used, but we can't tell here
    // whether it will be needed or not.
    numElements =
        cgf.getBuilder().getConstInt(loc, adjustedCount * arraySizeMultiplier);

    // Compute the size before cookie, and track whether it overflowed.
    bool overflow;
    llvm::APInt allocationSize =
        adjustedCount.umul_ov(typeSizeMultiplier, overflow);

    hasAnyOverflow |= overflow;

    // Add in the cookie, and check whether it's overflowed.
    if (cookieSize != 0) {
      // Save the current size without a cookie.  This shouldn't be
      // used if there was overflow
      sizeWithoutCookie = cgf.getBuilder().getConstInt(
          loc, allocationSize.zextOrTrunc(sizeWidth));

      allocationSize = allocationSize.uadd_ov(cookieSize, overflow);
      hasAnyOverflow |= overflow;
    }

    // On overflow, produce a -1 so operator new will fail
    if (hasAnyOverflow) {
      size =
          cgf.getBuilder().getConstInt(loc, llvm::APInt::getAllOnes(sizeWidth));
    } else {
      size = cgf.getBuilder().getConstInt(loc, allocationSize);
    }
  } else {
    // Create a value for the variable number of elements
    numElements = cgf.emitScalarExpr(*e->getArraySize());

    // We might need to check for overflow.
    mlir::Value hasOverflow = nullptr;

    unsigned numElementsWidth =
        mlir::cast<cir::IntType>(numElements.getType()).getWidth();

    // If numElementsWidth > sizeWidth, then one way or another, we're
    // going to have to do a comparison for (2), and this happens to
    // take care of (1), too.
    if (numElementsWidth > sizeWidth) {
      // Match OGCG: compare against 1 << sizeWidth in the original width.
      // This catches values not representable in size_t and, for signed
      // values, also catches negatives via unsigned reinterpretation.
      llvm::APInt threshold =
          llvm::APInt::getOneBitSet(numElementsWidth, sizeWidth);
      mlir::Value thresholdV =
          cgf.getBuilder().getConstInt(loc, threshold, /*isUnsigned=*/true);

      mlir::Value unsignedNumElements = numElements;
      if (isSigned)
        unsignedNumElements = cgf.getBuilder().createIntCast(
            numElements, cgf.getBuilder().getUIntNTy(numElementsWidth));

      hasOverflow = cgf.getBuilder().createCompare(
          loc, cir::CmpOpKind::ge, unsignedNumElements, thresholdV);
      numElements = cgf.getBuilder().createIntCast(numElements, cgf.sizeTy);

      // Otherwise, if we're signed, we want to sext up to size_t.
    } else if (isSigned) {
      if (numElementsWidth < sizeWidth)
        numElements =
            cgf.getBuilder().createIntCast(numElements, cgf.sizeTy);

      // If there's a non-1 type size multiplier, then we can do the
      // signedness check at the same time as we do the multiply
      // because a negative number times anything will cause an
      // unsigned overflow. Otherwise, we have to do it here. But at least
      // in this case, we can subsume the >= minElements check.
      if (typeSizeMultiplier == 1) {
        // Use signed comparison to catch negative values. The constant
        // must match the numElements type for the CIR comparison.
        auto numElTy = numElements.getType();
        mlir::Value minV = cgf.getBuilder().getConstInt(
            loc, numElTy, minElements);
        hasOverflow = cgf.getBuilder().createCompare(
            loc, cir::CmpOpKind::lt, numElements, minV);
      }

      // Cast to unsigned size_t for subsequent arithmetic.
      numElements = cgf.getBuilder().createIntCast(numElements, cgf.sizeTy);

      // Otherwise, zext up to size_t if necessary.
    } else if (numElementsWidth < sizeWidth) {
      numElements =
          cgf.getBuilder().createIntCast(numElements, cgf.sizeTy);
    }

    // There are up to five conditions we need to test for:
    // 1) if isSigned, we need to check whether numElements is negative;
    // 2) if numElementsWidth > sizeWidth, we need to check whether
    //    numElements is larger than something representable in size_t;
    // 3) if minElements > 0, we need to check whether numElements is smaller
    //    than that.
    // 4) we need to compute
    //      sizeWithoutCookie := numElements * typeSizeMultiplier
    //    and check whether it overflows; and
    // 5) if we need a cookie, we need to compute
    //      size := sizeWithoutCookie + cookieSize
    //    and check whether it overflows.

    if (minElements) {
      // Don't allow allocation of fewer elements than we have initializers.
      mlir::Value minElementsV = cgf.getBuilder().getConstInt(
          loc, llvm::APInt(sizeWidth, minElements));
      if (!hasOverflow) {
        hasOverflow = cgf.getBuilder().createCompare(loc, cir::CmpOpKind::lt,
                                                     numElements, minElementsV);
      } else if (numElementsWidth > sizeWidth) {
        // The other existing overflow subsumes this check.
        // We do an unsigned comparison, since any signed value < -1 is
        // taken care of either above or below.
        hasOverflow = cgf.getBuilder().createOr(
            loc, hasOverflow,
            cgf.getBuilder().createCompare(loc, cir::CmpOpKind::lt,
                                           numElements, minElementsV));
      }
    }

    size = numElements;

    // Multiply by the type size if necessary.  This multiplier
    // includes all the factors for nested arrays.
    //
    // This step also causes numElements to be scaled up by the
    // nested-array factor if necessary.  Overflow on this computation
    // can be ignored because the result shouldn't be used if
    // allocation fails.
    if (typeSizeMultiplier != 1) {
      mlir::Value tsmV = cgf.getBuilder().getConstInt(loc, typeSizeMultiplier);
      auto mulResult = cir::BinOpOverflowOp::create(
          cgf.getBuilder(), loc, mlir::cast<cir::IntType>(cgf.sizeTy),
          cir::BinOpOverflowKind::Mul, size, tsmV);

      if (hasOverflow)
        hasOverflow = cgf.getBuilder().createOr(loc, hasOverflow,
                                                mulResult.getOverflow());
      else
        hasOverflow = mulResult.getOverflow();

      size = mulResult.getResult();

      // Also scale up numElements by the array size multiplier.
      if (arraySizeMultiplier != 1) {
        // If the base element type size is 1, then we can re-use the
        // multiply we just did.
        if (typeSize.isOne()) {
          assert(arraySizeMultiplier == typeSizeMultiplier);
          numElements = size;
        } else {
          // Otherwise we need a separate multiply.
          mlir::Value asmV =
              cgf.getBuilder().getConstInt(loc, arraySizeMultiplier);
          numElements = cgf.getBuilder().createMul(loc, numElements, asmV);
        }
      }
    } else {
      // numElements doesn't need to be scaled.
      assert(arraySizeMultiplier == 1);
    }

    // Add in the cookie size if necessary.
    if (cookieSize != 0) {
      sizeWithoutCookie = size;
      mlir::Value cookieSizeV = cgf.getBuilder().getConstInt(loc, cookieSize);
      auto addResult = cir::BinOpOverflowOp::create(
          cgf.getBuilder(), loc, mlir::cast<cir::IntType>(cgf.sizeTy),
          cir::BinOpOverflowKind::Add, size, cookieSizeV);

      if (hasOverflow)
        hasOverflow = cgf.getBuilder().createOr(loc, hasOverflow,
                                                addResult.getOverflow());
      else
        hasOverflow = addResult.getOverflow();

      size = addResult.getResult();
    }

    // If we had any possibility of dynamic overflow, make a select to
    // overwrite 'size' with an all-ones value, which should cause
    // operator new to throw.
    if (hasOverflow) {
      mlir::Value allOnes =
          cgf.getBuilder().getConstInt(loc, llvm::APInt::getAllOnes(sizeWidth));
      size = cgf.getBuilder().createSelect(loc, hasOverflow, allOnes, size);
    }
  }

  if (cookieSize == 0)
    sizeWithoutCookie = size;
  else
    assert(sizeWithoutCookie && "didn't set sizeWithoutCookie?");

  return size;
}

static void storeAnyExprIntoOneUnit(CIRGenFunction &cgf, const Expr *init,
                                    QualType allocType, Address newPtr,
                                    AggValueSlot::Overlap_t mayOverlap,
                                    AggValueSlot::IsSanitizerChecked_t
                                        isSanitizerChecked =
                                            AggValueSlot::IsNotSanitizerChecked,
                                    std::optional<mlir::Location> keyLoc =
                                        std::nullopt) {
  // FIXME: Refactor with emitExprAsInit.
  switch (cgf.getEvaluationKind(allocType)) {
  case cir::TEK_Scalar: {
    if (keyLoc) {
      mlir::Value value = cgf.emitScalarExpr(init);
      CIRGenFunction::SourceLocRAIIObject locRAII{cgf, *keyLoc};
      cgf.emitStoreThroughLValue(RValue::get(value),
                                 cgf.makeAddrLValue(newPtr, allocType), false);
      return;
    }
    cgf.emitScalarInit(init, cgf.getLoc(init->getSourceRange()),
                       cgf.makeAddrLValue(newPtr, allocType), false);
    return;
  }
  case cir::TEK_Complex:
    cgf.emitComplexExprIntoLValue(init, cgf.makeAddrLValue(newPtr, allocType),
                                  /*isInit*/ true);
    return;
  case cir::TEK_Aggregate: {
    AggValueSlot slot = AggValueSlot::forAddr(
        newPtr, allocType.getQualifiers(), AggValueSlot::IsDestructed,
        AggValueSlot::IsNotAliased, mayOverlap, AggValueSlot::IsNotZeroed,
        isSanitizerChecked);
    cgf.emitAggExpr(init, slot);
    return;
  }
  }
  llvm_unreachable("bad evaluation kind");
}

void CIRGenFunction::emitNewArrayInitializer(
    const CXXNewExpr *e, QualType elementType, mlir::Type elementTy,
    Address beginPtr, mlir::Value numElements,
    mlir::Value allocSizeWithoutCookie) {
  // If we have a type with trivial initialization and no initializer,
  // there's nothing to do.
  if (!e->hasInitializer())
    return;

  Address curPtr = beginPtr;

  unsigned initListElements = 0;

  const Expr *init = e->getInitializer();
  Address endOfInit = Address::invalid();
  QualType::DestructionKind dtorKind = elementType.isDestructedType();
  EHScopeStack::stable_iterator partialArrayCleanup;

  CharUnits elementSize = getContext().getTypeSizeInChars(elementType);

  // Attempt to perform zero-initialization using memset.
  auto tryMemsetInitialization = [&]() -> bool {
    auto loc = numElements.getLoc();

    // FIXME: If the type is a pointer-to-data-member under the Itanium ABI,
    // we can initialize with a memset to -1.
    if (!cgm.getTypes().isZeroInitializable(elementType))
      return false;

    // Optimization: since zero initialization will just set the memory
    // to all zeroes, generate a single memset to do it in one shot.

    // Subtract out the size of any elements we've already initialized.
    auto remainingSize = allocSizeWithoutCookie;
    if (initListElements) {
      // We know this can't overflow; we check this when doing the allocation.
      unsigned initializedSize =
          getContext().getTypeSizeInChars(elementType).getQuantity() *
          initListElements;
      auto initSizeOp =
          builder.getConstantInt(loc, remainingSize.getType(), initializedSize);
      remainingSize = builder.createSub(loc, remainingSize, initSizeOp,
                                        cir::OverflowBehavior::None);
    }

    // Create the memset.
    auto castOp =
        builder.createPtrBitcast(curPtr.getPointer(), builder.getVoidTy());
    auto zeroVal = builder.getConstantInt(
        loc, cir::IntType::get(builder.getContext(), 8, false), 0);
    auto intCastVal = builder.createIntCast(
        zeroVal, cir::IntType::get(builder.getContext(), 32, true));
    auto memsetOp =
        cir::MemSetOp::create(builder, loc, castOp, intCastVal, remainingSize);
    // Forward alignment to lowering so the LLVM memset gets align N.
    if (unsigned align = curPtr.getAlignment().getQuantity())
      memsetOp->setAttr("cir.dst_align",
                        builder.getI32IntegerAttr(align));
    return true;
  };

  const InitListExpr *ile = dyn_cast<InitListExpr>(init);
  const CXXParenListInitExpr *cplie = nullptr;
  const StringLiteral *sl = nullptr;
  const ObjCEncodeExpr *ocee = nullptr;
  const Expr *ignoreParen = nullptr;
  if (!ile) {
    ignoreParen = init->IgnoreParenImpCasts();
    cplie = dyn_cast<CXXParenListInitExpr>(ignoreParen);
    sl = dyn_cast<StringLiteral>(ignoreParen);
    ocee = dyn_cast<ObjCEncodeExpr>(ignoreParen);
  }

  // If the initializer is an initializer list, first do the explicit elements.
  if (ile || cplie || sl || ocee) {
    // Initializing from a (braced) string literal is a special case; the init
    // list element does not initialize a (single) array element.
    if ((ile && ile->isStringLiteralInit()) || sl || ocee) {
      // For bare StringLiteral/ObjCEncodeExpr (C++20 parenthesized init),
      // use the init expression directly.
      if (!ile)
        init = ignoreParen;
      // Emit the string literal into the beginning of the array.
      // The string literal has array type (e.g., const char[6]), so we need
      // to use the init expression's type for the aggregate slot.
      const Expr *strInit = ile ? ile->getInit(0) : init;
      mlir::Type strTy = convertTypeForMem(strInit->getType());
      Address strAddr = curPtr.withElementType(builder, strTy);
      AggValueSlot slot = AggValueSlot::forAddr(
          strAddr, elementType.getQualifiers(), AggValueSlot::IsDestructed,
          AggValueSlot::IsNotAliased, AggValueSlot::DoesNotOverlap,
          AggValueSlot::IsNotZeroed, AggValueSlot::IsSanitizerChecked);
      emitAggExpr(strInit, slot);

      // Move past the string literal elements.
      initListElements =
          cast<ConstantArrayType>(
              init->getType()->getAsArrayTypeUnsafe())
              ->getZExtSize();

      auto loc = getLoc(init->getSourceRange());
      auto strideVal =
          builder.getSignedInt(loc, initListElements, /*numBits=*/32);
      auto castOp = builder.createPtrBitcast(curPtr.getPointer(),
                                             curPtr.getElementType());
      auto dataPtr = builder.createPtrStride(loc, castOp, strideVal);
      curPtr = Address(dataPtr, curPtr.getElementType(),
                       curPtr.getAlignment().alignmentAtOffset(
                           elementSize * initListElements));

      // Zero out the rest of the array, if any elements remain.
      if (auto constOp = numElements.getDefiningOp<cir::ConstantOp>()) {
        if (auto constInt = constOp.getValueAttr<cir::IntAttr>()) {
          if (constInt.getUInt() != initListElements) {
            bool ok = tryMemsetInitialization();
            (void)ok;
            assert(ok && "couldn't memset character type?");
          }
        }
      } else {
        bool ok = tryMemsetInitialization();
        (void)ok;
        assert(ok && "couldn't memset character type?");
      }
      return;
    }

    ArrayRef<const Expr *> initExprs =
        ile ? ile->inits() : cplie->getInitExprs();
    initListElements = initExprs.size();

    // If this is a multi-dimensional array new, we will initialize multiple
    // elements with each init list element.
    QualType allocType = e->getAllocatedType();
    if (const ConstantArrayType *cat = dyn_cast_or_null<ConstantArrayType>(
            allocType->getAsArrayTypeUnsafe())) {
      elementTy = convertTypeForMem(allocType);
      curPtr = curPtr.withElementType(builder, elementTy);
      initListElements *= getContext().getConstantArrayElementCount(cat);
    }

    // Enter a partial-destruction Cleanup if necessary.
    // CIR manually deactivates the partial array cleanup at each return
    // point below, rather than using CleanupDeactivationScope (functionally
    // equivalent).
    if (dtorKind) {
      // Create an alloca to track how far initialization has progressed.
      // If a subsequent initializer throws, the cleanup will destroy
      // elements [beginPtr, *endOfInit) in reverse order.
      auto loc = getLoc(e->getSourceRange());
      auto ptrTy = mlir::cast<cir::PointerType>(beginPtr.getPointer().getType());
      endOfInit = createTempAlloca(ptrTy, getPointerAlign(), loc,
                                   "arrayinit.endOfInit");
      builder.createStore(loc, curPtr.getPointer(), endOfInit);
      pushIrregularPartialArrayCleanup(beginPtr.getPointer(), endOfInit,
                                       elementType,
                                       beginPtr.getAlignment()
                                           .alignmentOfArrayElement(elementSize),
                                       getDestroyer(dtorKind));
      partialArrayCleanup = ehStack.stable_begin();
    }

    CharUnits startAlign = curPtr.getAlignment();
    unsigned i = 0;
    for (const Expr *ie : initExprs) {
      // Update the end-of-init tracker before each element init so the
      // cleanup knows how far we've gotten if an init throws.
      if (endOfInit.isValid())
        builder.createStore(getLoc(ie->getSourceRange()), curPtr.getPointer(),
                            endOfInit);
      // FIXME: If the last initializer is an incomplete initializer list for
      // an array, and we have an array filler, we can fold together the two
      // initialization loops.
      storeAnyExprIntoOneUnit(*this, ie, ie->getType(), curPtr,
                              AggValueSlot::DoesNotOverlap,
                              AggValueSlot::IsSanitizerChecked);
      auto loc = getLoc(ie->getExprLoc());
      auto castOp = builder.createPtrBitcast(curPtr.getPointer(),
                                             curPtr.getElementType());
      auto offsetOp = builder.getSignedInt(loc, 1, /*numBits=*/32);
      auto dataPtr = builder.createPtrStride(loc, castOp, offsetOp);
      curPtr = Address(dataPtr, curPtr.getElementType(),
                       startAlign.alignmentAtOffset((++i) * elementSize));
    }

    // The remaining elements are filled with the array filler expression.
    init = ile ? ile->getArrayFiller() : cplie->getArrayFiller();

    // Extract the initializer for the individual array elements by pulling
    // out the array filler from all the nested initializer lists. This avoids
    // generating a nested loop for the initialization.
    while (init && init->getType()->isConstantArrayType()) {
      auto *subILE = dyn_cast<InitListExpr>(init);
      if (!subILE)
        break;
      assert(subILE->getNumInits() == 0 && "explicit inits in array filler?");
      init = subILE->getArrayFiller();
    }

    // Switch back to initializing one base element at a time.
    curPtr = curPtr.withElementType(builder, beginPtr.getElementType());
  }

  // If all elements have already been initialized, skip any further
  // initialization.
  if (auto constOp = numElements.getDefiningOp<cir::ConstantOp>()) {
    if (auto constIntAttr = constOp.getValueAttr<cir::IntAttr>()) {
      // Just skip out if the constant count is zero.
      if (constIntAttr.getUInt() <= initListElements) {
        if (partialArrayCleanup.isValid())
          deactivateCleanupBlock(partialArrayCleanup,
                                 /*dominatingIP=*/nullptr);
        return;
      }
    }
  }

  assert(init && "have trailing elements to initialize but no initializer");

  // If this is a constructor call, try to optimize it out, and failing that
  // emit a single loop to initialize all remaining elements.
  if (const CXXConstructExpr *cce = dyn_cast<CXXConstructExpr>(init)) {
    CXXConstructorDecl *ctor = cce->getConstructor();
    if (ctor->isTrivial()) {
      // If new expression did not specify value-initialization, then there
      // is no initialization.
      if (!cce->requiresZeroInitialization() || ctor->getParent()->isEmpty()) {
        if (partialArrayCleanup.isValid())
          deactivateCleanupBlock(partialArrayCleanup,
                                 /*dominatingIP=*/nullptr);
        return;
      }

      if (tryMemsetInitialization()) {
        if (partialArrayCleanup.isValid())
          deactivateCleanupBlock(partialArrayCleanup,
                                 /*dominatingIP=*/nullptr);
        return;
      }

      // Non-zero-initializable trivial ctor (e.g., member pointers with -1
      // null representation): fall through to the general constructor call
      // loop which handles per-element null initialization via the
      // zeroInitialize flag, matching OGCG behavior.
    }

    // Store the new Cleanup position for irregular Cleanups.
    // If endOfInit is valid, update it with the current position so the
    // partial array cleanup knows where construction reached.
    if (endOfInit.isValid()) {
      auto loc = getLoc(cce->getSourceRange());
      builder.createStore(loc, curPtr.getPointer(), endOfInit);
    }

    // Emit a constructor call loop to initialize the remaining elements.
    if (initListElements) {
      // Compute the remaining element count. If the original numElements is a
      // compile-time constant, emit the difference as a new constant to
      // preserve the constant-count optimization in emitCXXAggrConstructorCall.
      if (auto constOp = numElements.getDefiningOp<cir::ConstantOp>()) {
        if (auto constInt = constOp.getValueAttr<cir::IntAttr>()) {
          uint64_t remaining = constInt.getUInt() - initListElements;
          numElements = builder.getConstantInt(numElements.getLoc(),
                                              numElements.getType(), remaining);
        } else {
          auto initCountOp = builder.getConstantInt(
              numElements.getLoc(), numElements.getType(), initListElements);
          numElements = builder.createSub(numElements.getLoc(), numElements,
                                          initCountOp,
                                          cir::OverflowBehavior::None);
        }
      } else {
        auto initCountOp = builder.getConstantInt(
            numElements.getLoc(), numElements.getType(), initListElements);
        numElements = builder.createSub(numElements.getLoc(), numElements,
                                        initCountOp,
                                        cir::OverflowBehavior::None);
      }
    }
    // Build the array type for emitCXXAggrConstructorCall. The filler
    // CXXConstructExpr type may be the element type (from an init-list
    // array_filler) or the full array type (from a direct new T[N]).
    // emitCXXAggrConstructorCall expects arrayBase to have an ArrayType
    // element type for the static case, or the element type directly for
    // the dynamic case.
    mlir::Type convertedTy = convertType(cce->getType());
    if (auto arrTy = mlir::dyn_cast<cir::ArrayType>(convertedTy);
        arrTy && arrTy.getSize() > 0) {
      curPtr = curPtr.withElementType(builder, convertedTy);
    } else if (arrTy && arrTy.getSize() == 0) {
      // Zero-size array from incomplete type (e.g., A[] in new A[n]):
      // use the innermost element type so emitCXXAggrConstructorCall
      // takes the dynamic pointer-based loop path.
      mlir::Type eltTy = arrTy.getElementType();
      while (auto inner = mlir::dyn_cast<cir::ArrayType>(eltTy))
        eltTy = inner.getElementType();
      curPtr = curPtr.withElementType(builder, eltTy);
    } else if (auto constOp = numElements.getDefiningOp<cir::ConstantOp>()) {
      if (auto constInt = constOp.getValueAttr<cir::IntAttr>()) {
        auto arrayType =
            cir::ArrayType::get(convertedTy, constInt.getUInt());
        curPtr = curPtr.withElementType(builder, arrayType);
      } else {
        // Dynamic remaining count: keep element type for pointer-based loop.
        curPtr = curPtr.withElementType(builder, convertedTy);
      }
    } else {
      // Dynamic remaining count: keep element type for pointer-based loop.
      curPtr = curPtr.withElementType(builder, convertedTy);
    }
    emitCXXAggrConstructorCall(ctor, numElements, curPtr, cce,
                               /*newPointerIsChecked*/ true,
                               cce->requiresZeroInitialization());
    if (partialArrayCleanup.isValid())
      deactivateCleanupBlock(partialArrayCleanup, /*dominatingIP=*/nullptr);
    return;
  }

  // If this is value-initialization, we can usually use memset.
  if (isa<ImplicitValueInitExpr>(init)) {
    if (tryMemsetInitialization()) {
      if (partialArrayCleanup.isValid())
        deactivateCleanupBlock(partialArrayCleanup, /*dominatingIP=*/nullptr);
      return;
    }

    // Non-zero-initializable types (e.g., pointer-to-data-member) need
    // per-element value-initialization. Use emitNullInitialization which
    // handles non-zero null patterns.
    if (auto constOp = numElements.getDefiningOp<cir::ConstantOp>()) {
      if (auto constInt = constOp.getValueAttr<cir::IntAttr>()) {
        uint64_t count = constInt.getUInt();
        if (count <= initListElements) {
          if (partialArrayCleanup.isValid())
            deactivateCleanupBlock(partialArrayCleanup,
                                   /*dominatingIP=*/nullptr);
          return; // All elements already initialized.
        }
        count -= initListElements;
        auto loc = getLoc(init->getSourceRange());
        CharUnits startAlign = curPtr.getAlignment();
        for (uint64_t i = 0; i < count; ++i) {
          emitNullInitialization(loc, curPtr, elementType);
          // Stride by the BASE element type, not the array element type.
          // For multi-dimensional arrays like new T[3][4][5](), curPtr
          // may point to [4 x [5 x T]], but we need to advance by sizeof(T).
          auto baseEltTy = convertTypeForMem(elementType);
          auto castOp = builder.createPtrBitcast(curPtr.getPointer(),
                                                 baseEltTy);
          auto offsetOp = builder.getSignedInt(loc, 1, /*numBits=*/32);
          auto dataPtr = builder.createPtrStride(loc, castOp, offsetOp);
          curPtr = Address(dataPtr, baseEltTy,
                           startAlign.alignmentAtOffset(
                               (initListElements + i + 1) * elementSize));
        }
        if (partialArrayCleanup.isValid())
          deactivateCleanupBlock(partialArrayCleanup,
                                 /*dominatingIP=*/nullptr);
        return;
      }
    }

    // Fall through to the runtime loop below.
  }

  // If we have an empty initializer list, we can usually use memset.
  if (auto *ile2 = dyn_cast<InitListExpr>(init))
    if (ile2->getNumInits() == 0 && tryMemsetInitialization()) {
      if (partialArrayCleanup.isValid())
        deactivateCleanupBlock(partialArrayCleanup, /*dominatingIP=*/nullptr);
      return;
    }

  // If we have a struct whose every field is value-initialized, we can
  // usually use memset.
  if (auto *ile2 = dyn_cast<InitListExpr>(init)) {
    if (const RecordType *rType =
            ile2->getType()->getAsCanonical<RecordType>()) {
      if (rType->getDecl()->isStruct()) {
        const RecordDecl *rd = rType->getDecl()->getDefinitionOrSelf();
        unsigned numFields = 0;
        if (auto *cxxRD = dyn_cast<CXXRecordDecl>(rd))
          numFields = cxxRD->getNumBases();
        for (auto *field : rd->fields())
          if (!field->isUnnamedBitField())
            ++numFields;
        // FIXME: Recurse into nested InitListExprs.
        if (ile2->getNumInits() == numFields)
          for (unsigned i = 0, e2 = ile2->getNumInits(); i != e2; ++i)
            if (!isa<ImplicitValueInitExpr>(ile2->getInit(i)))
              --numFields;
        if (ile2->getNumInits() == numFields && tryMemsetInitialization()) {
          if (partialArrayCleanup.isValid())
            deactivateCleanupBlock(partialArrayCleanup,
                                   /*dominatingIP=*/nullptr);
          return;
        }
      }
    }
  }

  // General runtime loop: emit a pointer-based do-while loop guarded by a
  // zero-remaining-count check. This handles all remaining filler types
  // (ImplicitValueInitExpr with dynamic count, InitListExpr fillers, etc.)
  // using storeAnyExprIntoOneUnit per element, matching OGCG's loop at
  // CGExprCXX.cpp:1254-1314.

  // Enter a partial-destruction cleanup if we haven't already (i.e., no
  // init-list phase pushed one). This protects runtime-loop-initialized
  // elements from leaking if a constructor throws.
  // OGCG uses pushRegularPartialArrayCleanup here (CGExprCXX.cpp:1287-1295),
  // but CIR uses the irregular variant with an alloca because CIR's loop
  // is structured (do-while with alloca cursor) rather than SSA-based (PHI).
  if (!partialArrayCleanup.isValid() && needsEHCleanup(dtorKind)) {
    auto loc = getLoc(e->getSourceRange());
    auto ptrTy =
        mlir::cast<cir::PointerType>(beginPtr.getPointer().getType());
    endOfInit = createTempAlloca(ptrTy, getPointerAlign(), loc,
                                 "arrayinit.endOfInit");
    builder.createStore(loc, curPtr.getPointer(), endOfInit);
    pushIrregularPartialArrayCleanup(
        beginPtr.getPointer(), endOfInit, elementType,
        beginPtr.getAlignment().alignmentOfArrayElement(elementSize),
        getDestroyer(dtorKind));
    partialArrayCleanup = ehStack.stable_begin();
  }

  {
    mlir::Location loc = getLoc(init->getSourceRange());
    // Use the BASE element type for the loop pointer, not the array element
    // type. For multi-dimensional arrays like new T[3][4][5](), elementTy
    // may be [4 x [5 x T]], but the loop must advance by sizeof(T).
    // convertTypeForMem(elementType) gives us the base QualType's CIR type.
    mlir::Type eltTy = convertTypeForMem(elementType);
    cir::PointerType ptrToEltTy = builder.getPointerTo(eltTy);

    // Compute the end pointer: beginPtr + numElements.
    mlir::Value beginPtrVal =
        builder.createPtrBitcast(beginPtr.getPointer(), eltTy);
    mlir::Value endPtr =
        builder.createPtrStride(loc, beginPtrVal, numElements);

    // Guard: skip the loop if no remaining elements (curPtr == endPtr).
    mlir::Value curPtrVal =
        builder.createPtrBitcast(curPtr.getPointer(), eltTy);
    mlir::Value isNotEmpty =
        builder.createCompare(loc, cir::CmpOpKind::ne, curPtrVal, endPtr);

    CharUnits eltAlignment =
        beginPtr.getAlignment().alignmentOfArrayElement(elementSize);

    cir::IfOp::create(
        builder, loc, isNotEmpty, /*withElseRegion=*/false,
        [&](mlir::OpBuilder &, mlir::Location loc) {
          // Alloca for loop cursor.
          auto ptrToPtrTy = builder.getPointerTo(ptrToEltTy);
          mlir::Value curAddrVal = builder.createAlloca(
              loc, ptrToPtrTy, ptrToEltTy, "__new_loop_idx",
              builder.getAlignmentAttr(getPointerAlign().getQuantity()));
          Address curAddr(curAddrVal, ptrToEltTy, getPointerAlign());
          builder.createStore(loc, curPtrVal, curAddr);

          builder.createDoWhile(
              loc,
              /*condBuilder=*/
              [&](mlir::OpBuilder &, mlir::Location loc) {
                mlir::Value cur = builder.createLoad(loc, curAddr);
                mlir::Value cmp = builder.createCompare(
                    loc, cir::CmpOpKind::ne, cur, endPtr);
                builder.createCondition(cmp);
              },
              /*bodyBuilder=*/
              [&](mlir::OpBuilder &, mlir::Location loc) {
                mlir::Value cur = builder.createLoad(loc, curAddr);

                // Update the end-of-init tracker for partial cleanup.
                if (endOfInit.isValid())
                  builder.createStore(loc, cur, endOfInit);

                Address curEltAddr(cur, eltTy, eltAlignment);
                storeAnyExprIntoOneUnit(*this, init, init->getType(),
                                        curEltAddr,
                                        AggValueSlot::DoesNotOverlap,
                                        AggValueSlot::IsSanitizerChecked);

                // Advance cursor.
                mlir::Value one =
                    builder.getSignedInt(loc, 1, /*numBits=*/32);
                mlir::Value next =
                    builder.createPtrStride(loc, cur, one);
                builder.createStore(loc, next, curAddr);
                builder.createYield(loc);
              });
          builder.createYield(loc);
        });

    if (partialArrayCleanup.isValid())
      deactivateCleanupBlock(partialArrayCleanup, /*dominatingIP=*/nullptr);
  }
}

static void emitNewInitializer(CIRGenFunction &cgf, const CXXNewExpr *e,
                               QualType elementType, mlir::Type elementTy,
                               Address newPtr, mlir::Value numElements,
                               mlir::Value allocSizeWithoutCookie) {
  assert(!cir::MissingFeatures::debugInfoSpecialContext());
  if (e->isArray()) {
    cgf.emitNewArrayInitializer(e, elementType, elementTy, newPtr, numElements,
                                allocSizeWithoutCookie);
  } else if (const Expr *init = e->getInitializer()) {
    storeAnyExprIntoOneUnit(cgf, init, e->getAllocatedType(), newPtr,
                            AggValueSlot::DoesNotOverlap,
                            AggValueSlot::IsSanitizerChecked,
                            cgf.getLoc(e->getExprLoc()));
  }
}

/// Emit a call to an operator new or operator delete function, as implicitly
/// created by new-expressions and delete-expressions.
static RValue emitNewDeleteCall(CIRGenFunction &cgf,
                                const FunctionDecl *calleeDecl,
                                const FunctionProtoType *calleeType,
                                const CallArgList &args,
                                cir::CIRCallOpInterface *callOp = nullptr);

namespace {
/// A cleanup to call the given 'operator delete' function upon abnormal
/// exit from a new expression. Templated on a traits type that deals with
/// ensuring that the arguments dominate the cleanup if necessary.
template <typename Traits>
class CallDeleteDuringNew final : public EHScopeStack::Cleanup {
  /// Type used to hold llvm::Value*s.
  typedef typename Traits::ValueTy ValueTy;
  /// Type used to hold RValues.
  typedef typename Traits::RValueTy RValueTy;
  struct PlacementArg {
    RValueTy argValue;
    QualType argType;
  };

  unsigned numPlacementArgs : 31;
  LLVM_PREFERRED_TYPE(bool)
  unsigned passAlignmentToPlacementDelete : 1;
  const FunctionDecl *operatorDelete;
  RValueTy typeIdentity;
  ValueTy ptr;
  ValueTy allocSize;
  CharUnits allocAlign;

  PlacementArg *getPlacementArgs() {
    return reinterpret_cast<PlacementArg *>(this + 1);
  }

public:
  static size_t getExtraSize(size_t numPlacementArgs) {
    return numPlacementArgs * sizeof(PlacementArg);
  }

  CallDeleteDuringNew(size_t numPlacementArgs,
                      const FunctionDecl *operatorDelete, RValueTy typeIdentity,
                      ValueTy ptr, ValueTy allocSize,
                      bool passAlignmentToPlacementDelete, CharUnits allocAlign)
      : numPlacementArgs(numPlacementArgs),
        passAlignmentToPlacementDelete(passAlignmentToPlacementDelete),
        operatorDelete(operatorDelete), typeIdentity(typeIdentity), ptr(ptr),
        allocSize(allocSize), allocAlign(allocAlign) {}

  void setPlacementArg(unsigned i, RValueTy arg, QualType type) {
    assert(i < numPlacementArgs && "index out of range");
    getPlacementArgs()[i] = {arg, type};
  }

  void emit(CIRGenFunction &cgf, Flags flags) override {
    const auto *fpt = operatorDelete->getType()->castAs<FunctionProtoType>();
    CallArgList deleteArgs;
    TypeAwareAllocationMode typeAwareDeallocation = TypeAwareAllocationMode::No;
    if (operatorDelete->isTypeAwareOperatorNewOrDelete())
      typeAwareDeallocation = TypeAwareAllocationMode::Yes;

    // Figure out what parameters we should be implicitly passing.
    UsualDeleteParams params;
    if (numPlacementArgs) {
      // A placement deallocation function is implicitly passed an alignment
      // if the placement allocation function was, but is never passed a size.
      params.Alignment = passAlignmentToPlacementDelete
                             ? AlignedAllocationMode::Yes
                             : AlignedAllocationMode::No;
      params.TypeAwareDelete = typeAwareDeallocation;
      params.Size = isTypeAwareAllocation(params.TypeAwareDelete);
    } else {
      // For a non-placement new-expression, 'operator delete' can take a
      // size and/or an alignment if it has the right parameters.
      params = operatorDelete->getUsualDeleteParams();
    }

    unsigned firstNonTypeArg = 0;
    if (isTypeAwareAllocation(params.TypeAwareDelete)) {
      QualType specializedTypeIdentity = fpt->getParamType(0);
      ++firstNonTypeArg;
      deleteArgs.add(Traits::get(cgf, typeIdentity), specializedTypeIdentity);
    }

    // The first argument after type-identity parameter (if any) is always
    // a void* (or C* for a destroying operator delete for class type C).
    deleteArgs.add(Traits::get(cgf, ptr),
                   fpt->getParamType(firstNonTypeArg));

    assert(!params.DestroyingDelete &&
           "should not call destroying delete in a new-expression");

    // The second argument can be a std::size_t (for non-placement delete).
    if (params.Size)
      deleteArgs.add(Traits::get(cgf, allocSize),
                     cgf.getContext().getSizeType());

    // The next (second or third) argument can be a std::align_val_t, which
    // is an enum whose underlying type is std::size_t.
    // FIXME: Use the right type as the parameter type. Note that in a call
    // to operator delete(size_t, ...), we may not have it available.
    if (isAlignedAllocation(params.Alignment)) {
      QualType sizeType = cgf.getContext().getSizeType();
      mlir::Value alignVal = cgf.getBuilder().getConstInt(
          cgf.getLoc(operatorDelete->getSourceRange()),
          mlir::cast<cir::IntType>(cgf.convertType(sizeType)),
          allocAlign.getQuantity());
      deleteArgs.add(RValue::get(alignVal), sizeType);
    }

    // Pass the rest of the arguments, which must match exactly.
    for (unsigned i = 0; i != numPlacementArgs; ++i) {
      auto arg = getPlacementArgs()[i];
      deleteArgs.add(Traits::get(cgf, arg.argValue), arg.argType);
    }
    assert((fpt->isVariadic()
                ? deleteArgs.size() >= fpt->getNumParams()
                : deleteArgs.size() == fpt->getNumParams()) &&
           "mismatched operator delete argument count");

    // Call 'operator delete'.
    emitNewDeleteCall(cgf, operatorDelete, fpt, deleteArgs);
  }
};
} // namespace

/// Enter a cleanup to call 'operator delete' if the initializer in a
/// new-expression throws.
static void enterNewDeleteCleanup(CIRGenFunction &cgf, const CXXNewExpr *e,
                                  RValue typeIdentity, Address newPtr,
                                  mlir::Value allocSize, CharUnits allocAlign,
                                  const CallArgList &newArgs) {
  unsigned numNonPlacementArgs = e->getNumImplicitArgs();

  // In OGCG, when inside a conditional branch, values must be saved to allocas
  // because LLVM IR uses shared landing pad blocks where SSA values from one
  // branch may not dominate. CIR doesn't have this problem: each call gets its
  // own cleanup region (via callWithExceptionCtx), and MLIR's dominance rules
  // guarantee that values from a parent region dominate nested child regions.
  // Therefore we can always use direct cleanup traits regardless of whether
  // we're in a conditional branch.
  struct DirectCleanupTraits {
    typedef mlir::Value ValueTy;
    typedef RValue RValueTy;
    static RValue get(CIRGenFunction &, ValueTy v) { return RValue::get(v); }
    static RValue get(CIRGenFunction &, RValueTy v) { return v; }
  };

  typedef CallDeleteDuringNew<DirectCleanupTraits> DirectCleanup;

  DirectCleanup *cleanup = cgf.ehStack.pushCleanupWithExtra<DirectCleanup>(
      EHCleanup, e->getNumPlacementArgs(), e->getOperatorDelete(),
      typeIdentity, newPtr.getPointer(), allocSize, e->passAlignment(),
      allocAlign);
  for (unsigned i = 0, n = e->getNumPlacementArgs(); i != n; ++i) {
    auto &arg = newArgs[i + numNonPlacementArgs];
    cleanup->setPlacementArg(i, arg.getKnownRValue(), arg.ty);
  }
}

RValue CIRGenFunction::emitCXXDestructorCall(
    GlobalDecl dtor, const CIRGenCallee &callee, mlir::Value thisVal,
    QualType thisTy, mlir::Value implicitParam, QualType implicitParamTy,
    const CallExpr *ce) {
  const CXXMethodDecl *dtorDecl = cast<CXXMethodDecl>(dtor.getDecl());

  assert(!thisTy.isNull());
  assert(thisTy->getAsCXXRecordDecl() == dtorDecl->getParent() &&
         "Pointer/Object mixup");
  CallArgList args;
  commonBuildCXXMemberOrOperatorCall(*this, dtorDecl, thisVal, implicitParam,
                                     implicitParamTy, ce, args, nullptr);
  assert((ce || dtor.getDecl()) && "expected source location provider");
  // OGCG propagates musttail from CE == MustTailCall here
  // (CGExprCXX.cpp:100-126).
  bool isMustTail = (ce && ce == mustTailCall);
  mlir::Location loc =
      ce ? getLoc(ce->getExprLoc())
         : (currSrcLoc ? *currSrcLoc : getLoc(dtor.getDecl()->getSourceRange()));
  return emitCall(cgm.getTypes().arrangeCXXStructorDeclaration(dtor), callee,
                  ReturnValueSlot(), args, nullptr, loc,
                  isMustTail);
}

RValue CIRGenFunction::emitCXXPseudoDestructorExpr(
    const CXXPseudoDestructorExpr *expr) {
  QualType destroyedType = expr->getDestroyedType();
  if (destroyedType.hasStrongOrWeakObjCLifetime()) {
    cgm.errorNYI(expr->getExprLoc(),
                 "emitCXXPseudoDestructorExpr: Objective-C lifetime is NYI");
    return RValue::get(nullptr);
  } else {
    // C++ [expr.pseudo]p1:
    //   The result shall only be used as the operand for the function call
    //   operator (), and the result of such a call has type void. The only
    //   effect is the evaluation of the postfix-expression before the dot or
    //   arrow.
    emitIgnoredExpr(expr->getBase());
  }

  return RValue::get(nullptr);
}

mlir::Value CIRGenFunction::emitCXXTypeidExpr(const CXXTypeidExpr *e) {
  mlir::Location loc = getLoc(e->getSourceRange());
  auto castTypeInfoPtr = [&](mlir::Value ptr) {
    auto resultPtrTy = builder.getPointerTo(convertType(e->getType()));
    if (ptr.getType() == resultPtrTy)
      return ptr;

    auto srcPtrTy = mlir::cast<cir::PointerType>(ptr.getType());
    auto dstPtrTy = mlir::cast<cir::PointerType>(resultPtrTy);
    if (srcPtrTy.getAddrSpace() != dstPtrTy.getAddrSpace()) {
      if (srcPtrTy.getPointee() != dstPtrTy.getPointee()) {
        auto intermediateTy = cir::PointerType::get(dstPtrTy.getPointee(),
                                                    srcPtrTy.getAddrSpace());
        ptr = builder.createBitcast(loc, ptr, intermediateTy);
      }
      return builder.createAddrSpaceCast(loc, ptr, resultPtrTy);
    }

    return builder.createBitcast(loc, ptr, resultPtrTy);
  };

  auto materializeRTTIValue = [&](QualType operandTy) {
    mlir::Attribute typeInfoAttr =
        cgm.getAddrOfRTTIDescriptor(loc, operandTy);
    return builder.getConstant(loc, mlir::cast<mlir::TypedAttr>(typeInfoAttr));
  };

  if (e->isTypeOperand()) {
    // typeid(Type) — compile-time type info lookup.
    // OGCG handles global variable address space casting here.
    return castTypeInfoPtr(
        materializeRTTIValue(e->getTypeOperand(getContext())));
  }

  // typeid(*expr) — runtime type info from vtable.
  //
  // C++ [expr.typeid]p2:
  //   When typeid is applied to a glvalue expression whose type is a
  //   polymorphic class type, the result refers to a std::type_info object
  //   representing the type of the most derived object.
  // If the operand is already the most derived object, no need to look up
  // the vtable — use the static type's RTTI directly.
  const Expr *operand = e->getExprOperand();
  if (!e->isPotentiallyEvaluated() || e->isMostDerived(getContext())) {
    // OGCG handles global variable address space casting here.
    return castTypeInfoPtr(materializeRTTIValue(operand->getType()));
  }

  // Runtime typeid: load vtable pointer and extract type_info.  // OGCG delegates to getCXXABI().EmitTypeid() and
  // shouldTypeidBeNullChecked() for ABI-specific behavior. CIR hardcodes
  // the Itanium ABI vtable walk and __cxa_bad_typeid.
  // MS ABI uses different typeid dispatch; CIR uses Itanium ABI only.
  Address thisPtr = emitLValue(operand).getAddress();
  QualType srcRecordTy = operand->getType();

  // Null check for typeid(*ptr) — throws std::bad_typeid if null.
  // OGCG delegates null check decision to getCXXABI().shouldTypeidBeNullChecked()
  // and uses ABI-specific bad_typeid (__cxa_bad_typeid for Itanium, __RTtypeid
  // for MSVC). CIR hardcodes Itanium behavior (MS ABI not supported).
  if (e->hasNullCheck()) {
    // Ensure __cxa_bad_typeid declaration exists before building the null
    // check. Use runtime-function creation to match OGCG linkage/DSO-local
    // behavior for runtime declarations.
    auto voidTy = cir::VoidType::get(builder.getContext());
    auto badTypeidFnTy = cir::FuncType::get({}, voidTy);
    cgm.createRuntimeFunction(badTypeidFnTy, "__cxa_bad_typeid");

    mlir::Value isNull = builder.createPtrIsNull(thisPtr.getPointer());
    cir::CallOp badTypeidCall;
    auto emitNullCheck = [&] {
      cir::IfOp::create(
          builder, loc, isNull, /*withElseRegion=*/false,
          [&](mlir::OpBuilder &, mlir::Location) {
            cir::CallOp callOp = cir::CallOp::create(
                builder, loc,
                mlir::SymbolRefAttr::get(builder.getContext(),
                                         "__cxa_bad_typeid"),
                /*resType=*/mlir::Type{}, mlir::ValueRange{});
            // __cxa_bad_typeid throws std::bad_typeid. When inside an EH scope,
            // mark as exception-aware so FlattenCFG converts to invoke.
            if (isCatchOrCleanupRequired())
              callOp.setExceptionAttr(mlir::UnitAttr::get(builder.getContext()));
            // Set noreturn on the call-site attributes so LLVM lowering
            // propagates it. Matches OGCG's call-site attrs.
            mlir::NamedAttrList callAttrs;
            auto noReturn = cir::NoReturnAttr::get(&getMLIRContext());
            callAttrs.set(noReturn.getMnemonic(), noReturn);
            callOp->setAttr("extra_attrs",
                            cir::ExtraFuncAttributesAttr::get(
                                callAttrs.getDictionary(&getMLIRContext())));
            badTypeidCall = callOp;
            cir::UnreachableOp::create(builder, loc);
            builder.clearInsertionPoint();
          });
    };

    cir::TryOp typeidUserTry =
        isCatchOrCleanupRequired() ? curLexScope->getClosestTryParent()
                                   : cir::TryOp();
    if (isCatchOrCleanupRequired() && !typeidUserTry) {
      // Active EH cleanups but no user try: wrap the throwing null check in a
      // synthetic cleanup-only try so the local destructors run as the thrown
      // std::bad_typeid propagates out.
      emitMayThrowOpViaSyntheticCleanupTry(loc, /*resultTy=*/mlir::Type{},
                                           [&]() -> mlir::Value {
                                             emitNullCheck();
                                             return {};
                                           });
    } else {
      emitNullCheck();
      // Populate the catch handler types on the enclosing user TryOp (so
      // FlattenCFG keeps its catch regions for the exception-attributed
      // __cxa_bad_typeid call) and thread the active EH cleanups (RAII
      // destructors between the typeid and the try) onto the call's unwind edge
      // so they run as std::bad_typeid propagates to the catch. Without the
      // latter, only the catch types are set and the destructors are dropped.
      threadUserTryCleanupsOntoThrowingCall(badTypeidCall);
    }
  }

  // Load vtable pointer from the object.
  // OGCG delegates runtime typeid to getCXXABI().EmitTypeid() which is
  // ABI-specific. CIR hardcodes the Itanium ABI vtable walk.
  // MS ABI not supported; CIR uses Itanium ABI only.
  auto *classDecl = srcRecordTy->castAsCXXRecordDecl();
  mlir::Value vtable = getVTablePtr(loc, thisPtr, classDecl);

  auto u8PtrTy = builder.getPointerTo(
      cir::IntType::get(builder.getContext(), 8, false));

  mlir::Value typeInfoPtr;
  if (cgm.getItaniumVTableContext().isRelativeLayout()) {
    // Relative vtable ABI: RTTI is at a 4-byte relative offset at
    // vtable[-4]. Use llvm.load.relative.i32 to resolve it to the RTTI
    // proxy, then load the actual type_info pointer from the proxy.
    mlir::Value offset = builder.getSInt32(-4, loc);
    auto loadRelative = cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("load.relative"), u8PtrTy,
        mlir::ValueRange{vtable, offset});
    mlir::Value rttiProxy = loadRelative.getResult();
    typeInfoPtr =
        builder.createAlignedLoad(loc, u8PtrTy, rttiProxy,
                                  getContext().getTypeAlignInChars(
                                      getContext().VoidPtrTy));
  } else {
    // Traditional vtable ABI: type_info pointer is at index -1
    // (pointer-sized slot before the vtable pointer).
    auto ptrPtrTy = builder.getPointerTo(u8PtrTy);
    mlir::Value vtableCast = cir::CastOp::create(builder, loc, ptrPtrTy,
                                                  cir::CastKind::bitcast,
                                                  vtable);

    auto minusOne = cir::ConstantOp::create(
        builder, loc, cir::IntType::get(builder.getContext(), 64, true),
        cir::IntAttr::get(cir::IntType::get(builder.getContext(), 64, true),
                          -1));
    mlir::Value typeInfoSlot = cir::PtrStrideOp::create(
        builder, loc, ptrPtrTy, vtableCast, minusOne,
        cir::GEPNoWrapFlags::inbounds);

    typeInfoPtr =
        builder.createAlignedLoad(loc, u8PtrTy, typeInfoSlot,
                                  getContext().getTypeAlignInChars(
                                      getContext().VoidPtrTy));
  }

  return castTypeInfoPtr(typeInfoPtr);
}

LValue CIRGenFunction::emitCXXTypeidLValue(const CXXTypeidExpr *e) {
  mlir::Value value = emitCXXTypeidExpr(e);
  return makeNaturalAlignPointeeAddrLValue(value, e->getType());
}

/// Emit a call to an operator new or operator delete function, as implicitly
/// created by new-expressions and delete-expressions.
static RValue emitNewDeleteCall(CIRGenFunction &cgf,
                                const FunctionDecl *calleeDecl,
                                const FunctionProtoType *calleeType,
                                const CallArgList &args,
                                cir::CIRCallOpInterface *outCallOp) {
  cir::CIRCallOpInterface callOrTryCall;
  cir::FuncOp calleePtr = cgf.cgm.getAddrOfFunction(calleeDecl);
  CIRGenCallee callee =
      CIRGenCallee::forDirect(calleePtr, GlobalDecl(calleeDecl));
  RValue rv =
      cgf.emitCall(cgf.cgm.getTypes().arrangeFreeFunctionCall(args, calleeType),
                   callee, ReturnValueSlot(), args, &callOrTryCall);

  /// C++1y [expr.new]p10:
  ///   [In a new-expression,] an implementation is allowed to omit a call
  ///   to a replaceable global allocation function.
  ///
  /// We model such elidable calls with the 'builtin' attribute.
  /// Matches OGCG CGExprCXX.cpp:1348-1352.
  /// Skip if the callee is an alias — in OGCG, dyn_cast<llvm::Function>
  /// returns null for aliases, so builtin is never added for aliased operators.
  if (calleeDecl->isReplaceableGlobalAllocationFunction() && calleePtr &&
      !calleePtr.getAliasee()) {
    bool hasNoBuiltin = false;
    if (auto extraAttrs = calleePtr.getExtraAttrs())
      hasNoBuiltin = extraAttrs->getElements().get("nobuiltin") != nullptr;
    if (hasNoBuiltin && callOrTryCall) {
      mlir::NamedAttrList callAttrs;
      if (auto existingExtra = callOrTryCall->getAttrOfType<
              cir::ExtraFuncAttributesAttr>("extra_attrs")) {
        for (auto attr : existingExtra.getElements().getValue())
          callAttrs.append(attr.getName(), attr.getValue());
      }
      callAttrs.set("builtin",
                    mlir::UnitAttr::get(cgf.getBuilder().getContext()));
      callOrTryCall->setAttr(
          "extra_attrs",
          cir::ExtraFuncAttributesAttr::get(
          callAttrs.getDictionary(cgf.getBuilder().getContext())));
    }
  }

  if (outCallOp)
    *outCallOp = callOrTryCall;

  if (callOrTryCall)
    cgf.addOpToCurrentSourceAtomWithRank(callOrTryCall, /*rank=*/2);

  return rv;
}

RValue CIRGenFunction::emitNewOrDeleteBuiltinCall(const FunctionProtoType *type,
                                                  const CallExpr *callExpr,
                                                  OverloadedOperatorKind op) {
  CallArgList args;
  emitCallArgs(args, type, callExpr->arguments());
  // Find the allocation or deallocation function that we're calling.
  ASTContext &astContext = getContext();
  assert(op == OO_New || op == OO_Delete);
  DeclarationName name = astContext.DeclarationNames.getCXXOperatorName(op);

  clang::DeclContextLookupResult lookupResult =
      astContext.getTranslationUnitDecl()->lookup(name);
  for (const auto *decl : lookupResult) {
    if (const auto *funcDecl = dyn_cast<FunctionDecl>(decl)) {
      if (astContext.hasSameType(funcDecl->getType(), QualType(type, 0))) {
        // Emit the call to operator new/delete.
        cir::CIRCallOpInterface callOp;
        RValue rv = emitNewDeleteCall(*this, funcDecl, type, args, &callOp);
        if (sanOpts.has(SanitizerKind::AllocToken))
          maybeAttachAllocToken(callOp, callExpr);
        return rv;
      }
    }
  }

  llvm_unreachable("predeclared global operator new/delete is missing");
}

namespace {
/// Cleanup to call 'operator delete[]' with the correct pointer.
struct CallArrayDelete final : EHScopeStack::Cleanup {
  mlir::Value allocatedPtr;
  const FunctionDecl *operatorDelete;
  mlir::Value numElements;
  QualType elementType;
  CharUnits cookieSize;

  CallArrayDelete(mlir::Value allocatedPtr, const FunctionDecl *operatorDelete,
                  mlir::Value numElements, QualType elementType,
                  CharUnits cookieSize)
      : allocatedPtr(allocatedPtr), operatorDelete(operatorDelete),
        numElements(numElements), elementType(elementType),
        cookieSize(cookieSize) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    cgf.emitDeleteCall(operatorDelete, allocatedPtr, elementType, numElements,
                       cookieSize);
  }
};
} // namespace

/// Emit the code for deleting an array of objects.
static void emitArrayDelete(CIRGenFunction &cgf, const CXXDeleteExpr *e,
                            Address deletedPtr, QualType elementType) {
  // Determine whether we need an array cookie.
  bool needsCookie = cgf.cgm.getCXXABI().requiresArrayCookie(e, elementType);

  mlir::Value allocatedPtr;
  mlir::Value numElements = nullptr;
  CharUnits cookieSize = CharUnits::Zero();

  auto &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(e->getSourceRange());
  ASTContext &ctx = cgf.getContext();

  if (!needsCookie) {
    allocatedPtr = deletedPtr.getPointer();
  } else {
    cgf.cgm.getCXXABI().readArrayCookie(cgf, deletedPtr, e, elementType,
                                        numElements, allocatedPtr, cookieSize);
  }

  // Push the 'operator delete[]' cleanup to run even if a dtor throws.
  const FunctionDecl *operatorDelete = e->getOperatorDelete();
  cgf.ehStack.pushCleanup<CallArrayDelete>(NormalAndEHCleanup, allocatedPtr,
                                           operatorDelete, numElements,
                                           elementType, cookieSize);

  // Destroy the array elements.
  if (QualType::DestructionKind dtorKind = elementType.isDestructedType()) {
    (void)dtorKind; // Used to select destruction strategy in OGCG; CIR
                    // currently only handles CXXRecord destructors.
    assert(numElements && "no element count for a type with a destructor!");

    bool useEHCleanup = cgf.needsEHCleanup(dtorKind);

    CharUnits elementSize = ctx.getTypeSizeInChars(elementType);
    CharUnits elementAlign =
        deletedPtr.getAlignment().alignmentOfArrayElement(elementSize);

    if (const auto *rd = elementType->getAsCXXRecordDecl()) {
      if (rd->hasDefinition() && !rd->hasTrivialDestructor()) {
        const CXXDestructorDecl *dtor = rd->getDestructor();

        // When EH cleanup is needed, always use the manual loop path so we
        // can push/pop per-element EH cleanups. ArrayDtor doesn't expose
        // the iteration cursor to the surrounding scope.
        bool useManualLoop = useEHCleanup;

        mlir::Type arrayTy;
        if (!useManualLoop) {
          if (auto constOp = numElements.getDefiningOp<cir::ConstantOp>()) {
            if (auto constInt = constOp.getValueAttr<cir::IntAttr>())
              arrayTy = cir::ArrayType::get(deletedPtr.getElementType(),
                                            constInt.getUInt());
          }
        }
        if (arrayTy && !useManualLoop) {
          mlir::Value arrayOp =
              builder.createPtrBitcast(deletedPtr.getPointer(), arrayTy);
          auto ptrToElmType =
              builder.getPointerTo(deletedPtr.getElementType());
          cir::ArrayDtor::create(
              builder, loc, arrayOp,
              [&](mlir::OpBuilder &b, mlir::Location loc) {
                mlir::BlockArgument arg =
                    b.getInsertionBlock()->addArgument(ptrToElmType, loc);
                Address curAddr =
                    Address(arg, deletedPtr.getElementType(), elementAlign);
                cgf.emitCXXDestructorCall(dtor, Dtor_Complete,
                                          /*ForVirtualBase=*/false,
                                          /*Delegating=*/false, curAddr,
                                          elementType);
                cir::YieldOp::create(builder, loc);
              });
        } else {
          // Dynamic element count or EH cleanup needed — emit a manual
          // reverse destruction loop.
          auto ptrToElmType =
              builder.getPointerTo(deletedPtr.getElementType());
          mlir::Value basePtr = deletedPtr.getPointer();

          // Compute end pointer: base + numElements
          mlir::Value endPtr = cir::PtrStrideOp::create(
              builder, loc, ptrToElmType, basePtr, numElements,
              cir::GEPNoWrapFlags::inbounds);

          // It is legal to allocate a zero-length array, and the length
          // always comes from a cookie so we can't fold this check away.
          mlir::Value isEmpty = builder.createCompare(
              loc, cir::CmpOpKind::eq, basePtr, endPtr);
          cir::IfOp::create(
              builder, loc, builder.createNot(isEmpty),
              /*withElseRegion=*/false,
              [&](mlir::OpBuilder &, mlir::Location) {
                // Alloca for the loop cursor, initialized to end
                Address cursorAddr = cgf.createTempAlloca(
                    ptrToElmType, cgf.getPointerAlign(), loc,
                    "arraydestroy.cur");
                builder.createStore(loc, endPtr, cursorAddr);

                mlir::Value minusOne = builder.getSignedInt(loc, -1, 64);

                // do { --cur; cur->~T(); } while (cur != base);
                builder.createDoWhile(
                    loc,
                    /*condBuilder=*/
                    [&](mlir::OpBuilder &, mlir::Location loc) {
                      mlir::Value cur =
                          builder.createLoad(loc, cursorAddr);
                      auto cmp = cir::CmpOp::create(
                          builder, loc, cir::CmpOpKind::ne, cur, basePtr);
                      builder.createCondition(cmp);
                    },
                    /*bodyBuilder=*/
                    [&](mlir::OpBuilder &, mlir::Location loc) {
                      mlir::Value cur =
                          builder.createLoad(loc, cursorAddr);
                      // Decrement: --cur
                      mlir::Value prev = cir::PtrStrideOp::create(
                          builder, loc, ptrToElmType, cur, minusOne,
                          cir::GEPNoWrapFlags::inbounds);
                      builder.createStore(loc, prev, cursorAddr);
                      // Push EH cleanup to destroy remaining elements
                      // if this destructor throws.
                      if (useEHCleanup)
                        cgf.pushRegularPartialArrayCleanup(
                            basePtr, cursorAddr.getPointer(),
                            elementType, elementAlign,
                            cgf.getDestroyer(dtorKind));
                      // Call destructor on the element
                      Address curAddr(prev, deletedPtr.getElementType(),
                                      elementAlign);
                      cgf.emitCXXDestructorCall(
                          dtor, Dtor_Complete,
                          /*ForVirtualBase=*/false,
                          /*Delegating=*/false, curAddr, elementType);
                      if (useEHCleanup)
                        cgf.popCleanupBlock();
                      cir::YieldOp::create(builder, loc);
                    });

                builder.createYield(loc);
              });
        }
      }
    } else {
      // Non-CXX record types with non-trivial destruction (e.g., ObjC
      // lifetime types) are NYI.
      cgf.cgm.errorNYI(e->getSourceRange(),
                       "emitArrayDelete: non-CXX record destruction");
      return;
    }
  }

  // Pop the cleanup to emit the operator delete[] call.
  cgf.popCleanupBlock();
}

namespace {
/// Calls the given 'operator delete' on a single object.
struct CallObjectDelete final : EHScopeStack::Cleanup {
  mlir::Value ptr;
  const FunctionDecl *operatorDelete;
  QualType elementType;

  CallObjectDelete(mlir::Value ptr, const FunctionDecl *operatorDelete,
                   QualType elementType)
      : ptr(ptr), operatorDelete(operatorDelete), elementType(elementType) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    cgf.emitDeleteCall(operatorDelete, ptr, elementType);
  }
};
} // namespace

void CIRGenFunction::pushCallObjectDeleteCleanup(
    const FunctionDecl *operatorDelete, mlir::Value completePtr,
    QualType elementType) {
  ehStack.pushCleanup<CallObjectDelete>(NormalAndEHCleanup, completePtr,
                                        operatorDelete, elementType);
}

/// Emit the code for deleting a single object.
static void emitObjectDelete(CIRGenFunction &cgf, const CXXDeleteExpr *de,
                             Address ptr, QualType elementType) {
  // C++11 [expr.delete]p3:
  //   If the static type of the object to be deleted is different from its
  //   dynamic type, the static type shall be a base class of the dynamic type
  //   of the object to be deleted and the static type shall have a virtual
  //   destructor or the behavior is undefined.
  //
  // Emit UBSan type check on the pointer before calling the destructor.
  cgf.emitTypeCheck(CIRGenFunction::TCK_MemberCall, de->getExprLoc(),
                    ptr.getPointer(), elementType);

  const FunctionDecl *operatorDelete = de->getOperatorDelete();
  assert(!operatorDelete->isDestroyingOperatorDelete());

  // Find the destructor for the type, if applicable.  If the
  // destructor is virtual, we'll just emit the vcall and return.
  const CXXDestructorDecl *dtor = nullptr;
  if (const auto *rd = elementType->getAsCXXRecordDecl()) {
    if (rd->hasDefinition() && !rd->hasTrivialDestructor()) {
      dtor = rd->getDestructor();

      if (dtor->isVirtual()) {
        bool useVirtualCall = true;
        const Expr *base = de->getArgument();
        if (auto *devirtualizedDtor =
                dyn_cast_or_null<const CXXDestructorDecl>(
                    dtor->getDevirtualizedMethod(
                        base, cgf.cgm.getLangOpts().AppleKext))) {
          useVirtualCall = false;
          const CXXRecordDecl *devirtualizedClass =
              devirtualizedDtor->getParent();
          if (declaresSameEntity(getCXXRecord(base), devirtualizedClass)) {
            // Devirtualized to the class of the base type, so we can call the
            // complete destructor directly like OGCG.
            dtor = devirtualizedDtor;
          } else {
            // Like OGCG, keep the virtual call when devirtualization would
            // require an unsupported this adjustment.
            useVirtualCall = true;
          }
        }
        if (useVirtualCall) {
          cgf.cgm.getCXXABI().emitVirtualObjectDelete(cgf, de, ptr, elementType,
                                                      dtor);
          return;
        }
      }
    }
  }

  // Make sure that we call delete even if the dtor throws.
  // This doesn't have to a conditional cleanup because we're going
  // to pop it off in a second.
  cgf.ehStack.pushCleanup<CallObjectDelete>(
      NormalAndEHCleanup, ptr.getPointer(), operatorDelete, elementType);

  if (dtor) {
    cgf.emitCXXDestructorCall(dtor, Dtor_Complete,
                              /*ForVirtualBase=*/false,
                              /*Delegating=*/false, ptr, elementType);
  } else if (elementType.getObjCLifetime()) {
    cgf.cgm.errorNYI(de->getSourceRange(), "emitObjectDelete: ObjCLifetime");
    return;
  }

  // OGCG previously had a code-size optimization at -Oz that unconditionally
  // called operator delete (delete(nullptr) is a no-op) to avoid the
  // conditional null-check branch. That optimization was reverted in OGCG,
  // so both CIR and OGCG now always emit the null check.

  cgf.popCleanupBlock();
}

void CIRGenFunction::emitCXXDeleteExpr(const CXXDeleteExpr *e) {
  const Expr *arg = e->getArgument();
  Address ptr = emitPointerWithAlignment(arg);

  // OGCG sets profile count at delete call site.

  // Null check the pointer.
  //
  // We could avoid this null check if we can determine that the object
  // destruction is trivial and doesn't require an array cookie; we can
  // unconditionally perform the operator delete call in that case. For now, we
  // assume that deleted pointers are null rarely enough that it's better to
  // keep the branch. This might be worth revisiting for a -O0 code size win.
  mlir::Location loc = getLoc(e->getSourceRange());
  mlir::Value ptrVal = ptr.getPointer();
  mlir::Value isNotNull = builder.createCompare(
      loc, cir::CmpOpKind::ne, ptrVal,
      builder.getNullPtr(ptrVal.getType(), loc));

  cir::IfOp::create(
      builder, loc, isNotNull, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &, mlir::Location) {
        // Inside the non-null branch, the pointer is known non-null.
        ptr.setKnownNonNull();

        QualType deleteTy = e->getDestroyedType();

        // A destroying operator delete overrides the entire operation of the
        // delete expression.
        if (e->getOperatorDelete()->isDestroyingOperatorDelete()) {
          auto *dtor = deleteTy->getAsCXXRecordDecl()->getDestructor();
          if (dtor && dtor->isVirtual()) {
            cgm.getCXXABI().emitVirtualObjectDelete(*this, e, ptr, deleteTy,
                                                    dtor);
          } else {
            emitDeleteCall(e->getOperatorDelete(), ptr.getPointer(), deleteTy);
          }
          builder.createYield(loc);
          return;
        }

        // We might be deleting a pointer to array.
        deleteTy = getContext().getBaseElementType(deleteTy);
        Address deletedPtr =
            ptr.withElementType(builder, convertTypeForMem(deleteTy));

        if (e->isArrayForm() &&
            cgm.getASTContext().getTargetInfo().emitVectorDeletingDtors(
                cgm.getASTContext().getLangOpts())) {
          cgm.errorNYI(e->getSourceRange(),
                       "emitCXXDeleteExpr: emitVectorDeletingDtors");
          return;
        }

        if (e->isArrayForm()) {
          emitArrayDelete(*this, e, deletedPtr, deleteTy);
        } else {
          emitObjectDelete(*this, e, deletedPtr, deleteTy);
        }
        builder.createYield(loc);
      });
}

mlir::Value CIRGenFunction::emitCXXNewExpr(const CXXNewExpr *e) {
  SourceLocRAIIObject newExprLoc{*this, getLoc(e->getExprLoc())};

  // The element type being allocated.
  QualType allocType = getContext().getBaseElementType(e->getAllocatedType());

  // 1. Build a call to the allocation function.
  FunctionDecl *allocator = e->getOperatorNew();

  // If there is a brace-initializer or C++20 parenthesized initializer, cannot
  // allocate fewer elements than inits.
  unsigned minElements = 0;
  if (e->isArray() && e->hasInitializer()) {
    const Expr *init = e->getInitializer();
    const InitListExpr *ile = dyn_cast<InitListExpr>(init);
    const CXXParenListInitExpr *cplie = dyn_cast<CXXParenListInitExpr>(init);
    const Expr *ignoreParen = init->IgnoreParenImpCasts();
    if ((ile && ile->isStringLiteralInit()) ||
        isa<StringLiteral>(ignoreParen) || isa<ObjCEncodeExpr>(ignoreParen)) {
      minElements =
          cast<ConstantArrayType>(init->getType()->getAsArrayTypeUnsafe())
              ->getZExtSize();
    } else if (ile || cplie) {
      minElements = ile ? ile->getNumInits() : cplie->getInitExprs().size();
    }
  }

  mlir::Value numElements = nullptr;
  mlir::Value allocSizeWithoutCookie = nullptr;
  mlir::Value allocSize = emitCXXNewAllocSize(
      *this, e, minElements, numElements, allocSizeWithoutCookie);
  CharUnits allocAlign = getContext().getTypeAlignInChars(allocType);
  bool zeroSizedArrayNew = false;
  if (numElements)
    if (auto numElementsCst = numElements.getDefiningOp<cir::ConstantOp>())
      if (auto intAttr = numElementsCst.getValueAttr<cir::IntAttr>())
        zeroSizedArrayNew = intAttr.getValue().isZero();

  // Emit the allocation call.
  Address allocation = Address::invalid();
  CallArgList allocatorArgs;
  RValue typeIdentityArg;
  if (allocator->isReservedGlobalPlacementOperator()) {
    // If the allocator is a global placement operator, just
    // "inline" it directly.
    assert(e->getNumPlacementArgs() == 1);
    const Expr *arg = *e->placement_arguments().begin();

    LValueBaseInfo baseInfo;
    allocation = emitPointerWithAlignment(arg, &baseInfo);

    // The pointer expression will, in many cases, be an opaque void*.
    // In these cases, discard the computed alignment and use the
    // formal alignment of the allocated type.
    if (baseInfo.getAlignmentSource() != AlignmentSource::Decl)
      allocation = allocation.withAlignment(allocAlign);

    // Set up allocatorArgs for the call to operator delete if it's not
    // the reserved global operator.
    if (e->getOperatorDelete() &&
        !e->getOperatorDelete()->isReservedGlobalPlacementOperator()) {
      allocatorArgs.add(RValue::get(allocSize), getContext().getSizeType());
      allocatorArgs.add(RValue::get(allocation.getPointer()), arg->getType());
    }
  } else {
    const FunctionProtoType *allocatorType =
        allocator->getType()->castAs<FunctionProtoType>();
    ImplicitAllocationParameters iap = e->implicitAllocationParameters();
    unsigned paramsToSkip = 0;

    // Type-aware allocation passes std::type_identity as the first arg.
    if (isTypeAwareAllocation(iap.PassTypeIdentity)) {
      QualType specializedTypeIdentity = allocatorType->getParamType(0);
      CXXScalarValueInitExpr typeIdentityParam(specializedTypeIdentity, nullptr,
                                               SourceLocation());
      typeIdentityArg = emitAnyExprToTemp(&typeIdentityParam);
      allocatorArgs.add(typeIdentityArg, specializedTypeIdentity);
      ++paramsToSkip;
    }

    // The allocation size is the first argument.
    QualType sizeType = getContext().getSizeType();
    allocatorArgs.add(RValue::get(allocSize), sizeType);
    ++paramsToSkip;

    if (allocSize != allocSizeWithoutCookie) {
      CharUnits cookieAlign = getSizeAlign(); // FIXME: Ask the ABI.
      allocAlign = std::max(allocAlign, cookieAlign);
    }

    // The allocation alignment may be passed as the second argument.
    if (e->passAlignment()) {
      QualType alignValT = sizeType;
      unsigned indexOfAlignArg = paramsToSkip;
      if (allocatorType->getNumParams() > indexOfAlignArg) {
        alignValT = allocatorType->getParamType(indexOfAlignArg);
        assert(getContext().hasSameUnqualifiedType(
                   alignValT->castAsEnumDecl()->getIntegerType(), sizeType) &&
               "wrong type for alignment parameter");
        ++paramsToSkip;
      } else {
        // Corner case, passing alignment to 'operator new(size_t, ...)'.
        assert(allocator->isVariadic() && "can't pass alignment to allocator");
      }
      mlir::Location loc = getLoc(e->getSourceRange());
      mlir::Value alignVal = builder.getConstInt(
          loc, mlir::cast<cir::IntType>(convertType(sizeType)),
          allocAlign.getQuantity());
      allocatorArgs.add(RValue::get(alignVal), alignValT);
    }

    // FIXME: Why do we not pass a CalleeDecl here?
    emitCallArgs(allocatorArgs, allocatorType, e->placement_arguments(),
                 AbstractCallee(), paramsToSkip);
    cir::CIRCallOpInterface allocatorCall;
    RValue rv = emitNewDeleteCall(*this, allocator, allocatorType,
                                  allocatorArgs, &allocatorCall);

    // Set !heapallocsite metadata on the call to operator new.
    assert(!cir::MissingFeatures::debugInfoSpecialContext());
    if (sanOpts.has(SanitizerKind::AllocToken))
      maybeAttachAllocToken(allocatorCall, allocType);

    // If this was a call to a global replaceable allocation function that does
    // not take an alignment argument, the allocator is known to produce storage
    // that's suitably aligned for any object that fits, up to a known
    // threshold. Otherwise assume it's suitably aligned for the allocated type.
    CharUnits allocationAlign = allocAlign;
    if (!e->passAlignment() &&
        allocator->isReplaceableGlobalAllocationFunction()) {
      const TargetInfo &target = cgm.getASTContext().getTargetInfo();
      unsigned allocatorAlign = llvm::bit_floor(std::min<uint64_t>(
          target.getNewAlign(), getContext().getTypeSize(allocType)));
      allocationAlign = std::max(
          allocationAlign, getContext().toCharUnitsFromBits(allocatorAlign));
    }

    mlir::Value allocPtr = rv.getValue();
    allocation = Address(
        allocPtr, mlir::cast<cir::PointerType>(allocPtr.getType()).getPointee(),
        allocationAlign);
  }

  // Emit a null check on the allocation result if the allocation
  // function is allowed to return null (because it has a non-throwing
  // exception spec or is the reserved placement new) and we have an
  // interesting initializer will be running sanitizers on the initialization.
  bool nullCheck = e->shouldNullCheckAllocation() &&
                   (!allocType.isPODType(getContext()) || e->hasInitializer() ||
                    sanitizePerformTypeCheck());

  // Perform null check before bitcast if needed.
  mlir::Value nullCheckCond;
  if (nullCheck) {
    mlir::Location loc = getLoc(e->getSourceRange());
    mlir::Value allocPtr = allocation.getPointer();
    auto ptrTy = mlir::cast<cir::PointerType>(allocPtr.getType());
    mlir::Value nullPtr = builder.getNullPtr(ptrTy, loc);
    nullCheckCond =
        builder.createCompare(loc, cir::CmpOpKind::ne, allocPtr, nullPtr);
  }

  // Compute element type before the null check branch — this doesn't access
  // allocation memory and is needed for both paths.
  mlir::Type elementTy;
  if (e->isArray()) {
    // For array new, use the allocated type to handle multidimensional arrays
    // correctly for the result pointer type.
    elementTy = convertTypeForMem(e->getAllocatedType());
  } else {
    elementTy = convertTypeForMem(allocType);
  }

  // Passing pointer through launder.invariant.group to avoid propagation of
  // vptrs information which may be included in previous type.
  // To not break LTO with different optimizations levels, we do it regardless
  // of optimization level.
  if (cgm.getCodeGenOpts().StrictVTablePointers &&
      allocator->isReservedGlobalPlacementOperator()) {
    auto laundered = cir::InvariantGroupOp::create(
        builder, getLoc(e->getSourceRange()), allocation.getPointer());
    allocation = allocation.withPointer(laundered);
  }

  // Helper lambda that performs the common initialization sequence:
  // enter operator-delete cleanup, initialize array cookie, bitcast to
  // element type, run initializer, deactivate cleanup.
  auto doNewInit = [&](mlir::Location loc) -> mlir::Value {
    // If there's an operator delete, enter a cleanup to call it if an
    // exception is thrown.
    EHScopeStack::stable_iterator operatorDeleteCleanup;
    if (e->getOperatorDelete() &&
        !e->getOperatorDelete()->isReservedGlobalPlacementOperator()) {
      enterNewDeleteCleanup(*this, e, typeIdentityArg, allocation, allocSize,
                            allocAlign, allocatorArgs);
      operatorDeleteCleanup = ehStack.stable_begin();
    }

    assert((allocSize == allocSizeWithoutCookie) ==
           calculateCookiePadding(*this, e).isZero());
    if (allocSize != allocSizeWithoutCookie) {
      assert(e->isArray());
      allocation = cgm.getCXXABI().initializeArrayCookie(
          *this, allocation, numElements, e, allocType);
    }

    Address result =
        builder.createElementBitCast(loc, allocation, elementTy);

    if (!zeroSizedArrayNew || !nullCheck) {
      SanitizerSet skippedChecks;
      skippedChecks.set(SanitizerKind::Null, nullCheck);
      if (zeroSizedArrayNew) {
        skippedChecks.set(SanitizerKind::ObjectSize, true);
        skippedChecks.set(SanitizerKind::Alignment, true);
      }
      emitTypeCheck(TCK_ConstructorCall,
                    e->getAllocatedTypeSourceInfo()->getTypeLoc().getBeginLoc(),
                    result.getPointer(), allocType, result.getAlignment(),
                    skippedChecks, numElements);
    }

    {
      // Deactivate per-member cleanups pushed during aggregate initialization
      // before leaving this lambda. When nullCheck is true, doNewInit runs
      // inside a cir.ternary's true branch. emitNewInitializer pushes
      // per-member destruction cleanups (via pushDestroyAndDeferDeactivation)
      // that reference cir.get_member SSA values defined inside the ternary.
      // Without deactivation here, those cleanups remain on the EH stack after
      // the ternary completes, and later throwing calls' cleanup regions
      // reference SSA values from inside the ternary — violating SSA
      // dominance.
      //
      // We use CleanupDeactivationScope (not RunCleanupsScope) to target only
      // the deferred-deactivation cleanups (per-member destructors) without
      // affecting other cleanups like temporary destructors that should
      // persist until the full expression exits.
      //
      // OGCG handles this via ConditionalEvaluation + DominatingValue to
      // save/restore cleanup addresses across conditional branches. CIR
      // models this with structured cir.ternary regions plus
      // CleanupDeactivationScope to keep deferred member cleanups local to
      // the conditional branch.
      CleanupDeactivationScope memberCleanups(*this);
      emitNewInitializer(*this, e, allocType, elementTy, result, numElements,
                         allocSizeWithoutCookie);
    }

    // Deactivate the 'operator delete' cleanup if we finished
    // initialization.
    if (operatorDeleteCleanup.isValid())
      deactivateCleanupBlock(operatorDeleteCleanup, /*dominatingIP=*/nullptr);

    return result.getPointer();
  };

  // When nullCheck is true, all post-allocation work (cookie init, sanitizer
  // checks, operator delete cleanup, initializer) must be guarded behind a
  // null check. Without this guard, nothrow allocation failures can still
  // reach the constructor-call type check and spuriously fire a handler.
  if (nullCheck) {
    mlir::Location loc = getLoc(e->getSourceRange());
    auto resultPtrTy = cir::PointerType::get(builder.getContext(), elementTy);
    Address resultSlot =
        createTempAlloca(resultPtrTy, getPointerAlign(), loc, "new.result");

    cir::IfOp::create(
        builder, loc, nullCheckCond, /*withElseRegion=*/true,
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          mlir::Value resultPtr = doNewInit(ifLoc);
          builder.createStore(ifLoc, resultPtr, resultSlot);
          builder.createYield(ifLoc);
        },
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          mlir::Value nullPtr = builder.getNullPtr(resultPtrTy, ifLoc);
          builder.createStore(ifLoc, nullPtr, resultSlot);
          builder.createYield(ifLoc);
        });

    return builder.createLoad(loc, resultSlot);
  }

  return doNewInit(getLoc(e->getSourceRange()));
}

void CIRGenFunction::emitDeleteCall(const FunctionDecl *deleteFD,
                                    mlir::Value ptr, QualType deleteTy,
                                    mlir::Value numElements,
                                    CharUnits cookieSize) {
  assert((!numElements && cookieSize.isZero()) ||
         deleteFD->getOverloadedOperator() == OO_Array_Delete);

  const auto *deleteFTy = deleteFD->getType()->castAs<FunctionProtoType>();
  CallArgList deleteArgs;

  UsualDeleteParams params = deleteFD->getUsualDeleteParams();
  auto paramTypeIt = deleteFTy->param_type_begin();

  mlir::Operation *tagAllocaOp = nullptr;
  auto emitTag = [&](QualType tagType, const char *tagName) {
    assert(!tagAllocaOp);
    Address tagAddr = createMemTemp(tagType, *currSrcLoc, tagName);
    deleteArgs.add(RValue::getAggregate(tagAddr), tagType);
    tagAllocaOp = tagAddr.getPointer().getDefiningOp();
  };

  // Pass std::type_identity tag if present
  if (isTypeAwareAllocation(params.TypeAwareDelete))
    emitTag(*paramTypeIt++, "typeaware.delete.tag");

  // Pass the pointer itself.
  QualType argTy = *paramTypeIt++;
  mlir::Value deletePtr =
      builder.createBitcast(ptr.getLoc(), ptr, convertType(argTy));
  deleteArgs.add(RValue::get(deletePtr), argTy);

  // Pass the std::destroying_delete tag if present.
  if (params.DestroyingDelete)
    emitTag(*paramTypeIt++, "destroying.delete.tag");

  // Pass the size if the delete function has a size_t parameter.
  if (params.Size) {
    QualType sizeType = *paramTypeIt++;
    CharUnits deleteTypeSize = getContext().getTypeSizeInChars(deleteTy);
    mlir::Type sizeIRTy = convertType(sizeType);
    assert(mlir::isa<cir::IntType>(sizeIRTy) && "expected cir::IntType");
    mlir::Value size =
        builder.getConstInt(*currSrcLoc, sizeIRTy, deleteTypeSize.getQuantity());

    // For array delete, multiply by the number of elements.
    if (numElements)
      size = builder.createMul(*currSrcLoc, size, numElements);

    // If there is a cookie, add the cookie size.
    if (!cookieSize.isZero()) {
      mlir::Value cookieSizeVal =
          builder.getConstInt(*currSrcLoc, sizeIRTy, cookieSize.getQuantity());
      size = builder.createAdd(*currSrcLoc, size, cookieSizeVal);
    }

    deleteArgs.add(RValue::get(size), sizeType);
  }

  // Pass the alignment if the delete function has an align_val_t parameter.
  if (isAlignedAllocation(params.Alignment)) {
    QualType alignValType = *paramTypeIt++;
    CharUnits deleteTypeAlign =
        getContext().toCharUnitsFromBits(getContext().getTypeAlignIfKnown(
            deleteTy, true /* NeedsPreferredAlignment */));
    mlir::Value align = builder.getConstInt(
        *currSrcLoc, convertType(alignValType),
        deleteTypeAlign.getQuantity());
    deleteArgs.add(RValue::get(align), alignValType);
  }

  assert(paramTypeIt == deleteFTy->param_type_end() &&
         "unknown parameter to usual delete function");

  // Emit the call to delete.
  emitNewDeleteCall(*this, deleteFD, deleteFTy, deleteArgs);

  // If call argument lowering didn't use a generated tag argument alloca we
  // remove them.
  if (tagAllocaOp && tagAllocaOp->use_empty()) {
    // Drop any cached "last alloca" entry pointing at this op before erasing it,
    // otherwise getBestAllocaInsertPoint would later dereference a freed op.
    builder.invalidateAllocaInsertPointCache(tagAllocaOp);
    tagAllocaOp->erase();
  }
}

static mlir::Value emitDynamicCastToNull(CIRGenFunction &cgf,
                                         mlir::Location loc, QualType destTy) {
  mlir::Type destCIRTy = cgf.convertType(destTy);
  assert(mlir::isa<cir::PointerType>(destCIRTy) &&
         "result of dynamic_cast should be a ptr");

  if (!destTy->isPointerType()) {
    mlir::Region *currentRegion = cgf.getBuilder().getBlock()->getParent();
    /// C++ [expr.dynamic.cast]p9:
    ///   A failed cast to reference type throws std::bad_cast
    cgf.cgm.getCXXABI().emitBadCastCall(cgf, loc);

    // The call to bad_cast will terminate the current block. Create a new block
    // to hold any follow up code.
    cgf.getBuilder().createBlock(currentRegion, currentRegion->end());
  }

  return cgf.getBuilder().getNullPtr(destCIRTy, loc);
}

mlir::Value CIRGenFunction::emitDynamicCast(Address thisAddr,
                                            const CXXDynamicCastExpr *dce) {
  mlir::Location loc = getLoc(dce->getSourceRange());

  cgm.emitExplicitCastExprType(dce, this);
  QualType destTy = dce->getTypeAsWritten();
  QualType srcTy = dce->getSubExpr()->getType();

  // C++ [expr.dynamic.cast]p7:
  //   If T is "pointer to cv void," then the result is a pointer to the most
  //   derived object pointed to by v.
  bool isDynCastToVoid = destTy->isVoidPointerType();
  bool isRefCast = destTy->isReferenceType();

  // C++ [expr.dynamic.cast]p9:
  //   A failed cast to reference type throws std::bad_cast.
  // Mark the enclosing EH scope as may-throw so exitCXXTryStmt preserves
  // catch regions, and set the function's personality attribute. Without this,
  // the high-level cir.dyn_cast op (which isn't a function call) never triggers
  // populateEHCatchRegions, leaving mayThrow() == false and causing all catch
  // handlers to be erased.
  cir::TryOp refCastUserTry;
  if (isRefCast && isCatchOrCleanupRequired()) {
    refCastUserTry = curLexScope->getClosestTryParent();
    if (refCastUserTry)
      populateCatchHandlersIfRequired(refCastUserTry);
  }

  QualType srcRecordTy;
  QualType destRecordTy;
  if (isDynCastToVoid) {
    srcRecordTy = srcTy->getPointeeType();
    // No destRecordTy.
  } else if (const PointerType *destPTy = destTy->getAs<PointerType>()) {
    srcRecordTy = srcTy->castAs<PointerType>()->getPointeeType();
    destRecordTy = destPTy->getPointeeType();
  } else {
    srcRecordTy = srcTy;
    destRecordTy = destTy->castAs<ReferenceType>()->getPointeeType();
  }

  assert(srcRecordTy->isRecordType() && "source type must be a record type!");

  // Emit UBSan type check for dynamic_cast operation.
  emitTypeCheck(TCK_DynamicOperation, dce->getExprLoc(), thisAddr.getPointer(),
                srcRecordTy);

  if (dce->isAlwaysNull())
    return emitDynamicCastToNull(*this, loc, destTy);

  auto destCirTy = mlir::cast<cir::PointerType>(convertType(destTy));

  // OGCG attempts devirtualization to an exact dynamic cast when the
  // destination type is effectively final (optimization only). CIR handles
  // this in CIRGenItaniumCXXABI::emitDynamicCast for effectively-final types.
  // Source pointer null checking is handled in LoweringPrepareItaniumCXXABI::
  // lowerDynamicCast, which wraps non-reference casts in a TernaryOp.

  // A throwing reference cast with active EH cleanups but no enclosing user try
  // must still run the local destructors as the std::bad_cast propagates out.
  // Wrap the cir.dyn_cast in a synthetic cleanup-only try so LoweringPrepare +
  // FlattenCFG route __cxa_bad_cast through the cleanup landing pad.
  if (isRefCast && isCatchOrCleanupRequired() && !refCastUserTry) {
    return emitMayThrowOpViaSyntheticCleanupTry(
        loc, destCirTy, [&]() -> mlir::Value {
          return cgm.getCXXABI().emitDynamicCast(*this, loc, srcRecordTy,
                                                 destRecordTy, destCirTy,
                                                 isRefCast, thisAddr);
        });
  }

  mlir::Value castResult = cgm.getCXXABI().emitDynamicCast(
      *this, loc, srcRecordTy, destRecordTy, destCirTy, isRefCast, thisAddr);

  // A throwing reference cast inside a user try with active EH cleanups must
  // still run the intervening local destructors as std::bad_cast propagates to
  // the catch. The cir.dyn_cast op is not a call, and its __cxa_bad_cast is only
  // synthesized in LoweringPrepare, so it can't use the callWithExceptionCtx
  // path that normal throwing calls use. Instead, populate the op's cleanup
  // region with the active EH cleanup destructor bodies; LoweringPrepare
  // transfers it onto the __cxa_bad_cast call so FlattenCFG threads them onto
  // the unwind edge (mirrors the eager cleanup emission in
  // emitMayThrowOpViaSyntheticCleanupTry used for the no-user-try case).
  if (isRefCast && refCastUserTry && isCatchOrCleanupRequired()) {
    if (auto dynCastOp = castResult.getDefiningOp<cir::DynamicCastOp>()) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      mlir::Block *cleanupBlock = builder.createBlock(&dynCastOp.getCleanup());
      builder.setInsertionPointToStart(cleanupBlock);
      bool savedIsEmittingEHCleanup = isEmittingEHCleanup;
      isEmittingEHCleanup = true;
      for (EHScopeStack::stable_iterator si = ehStack.getInnermostEHScope();
           si != ehStack.stable_end();
           si = ehStack.find(si)->getEnclosingEHScope()) {
        EHScope &sc = *ehStack.find(si);
        if (sc.getKind() != EHScope::Cleanup)
          break;
        EHCleanupScope &cs = cast<EHCleanupScope>(sc);
        if (!cs.isEHCleanup() || !cs.isActive())
          continue;
        EHScopeStack::Cleanup *cleanup = cs.getCleanup();
        EHScopeStack::Cleanup::Flags flags;
        flags.setIsForEHCleanup();
        flags.setIsEHCleanupKind();
        cleanup->emit(*this, flags);
      }
      isEmittingEHCleanup = savedIsEmittingEHCleanup;
      if (cleanupBlock->empty()) {
        // No active EH cleanups were emitted — drop the region so it stays
        // empty (elided in assembly, ignored by LoweringPrepare).
        cleanupBlock->erase();
      } else {
        if (!builder.getInsertionBlock()->mightHaveTerminator())
          cir::YieldOp::create(builder, loc);
        // Mark the enclosing try as carrying a cleanup so its landing pad runs
        // the transferred destructors before dispatching to the catch.
        refCastUserTry.setCleanup(true);
      }
    }
  }

  return castResult;
}
