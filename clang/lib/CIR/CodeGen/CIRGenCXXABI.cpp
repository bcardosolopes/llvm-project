//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides an abstract class for C++ code generation. Concrete subclasses
// of this implement code generation for specific C++ ABIs.
//
//===----------------------------------------------------------------------===//

#include "CIRGenCXXABI.h"
#include "CIRGenFunction.h"

#include "clang/AST/Decl.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/GlobalDecl.h"

using namespace clang;
using namespace clang::CIRGen;

CIRGenCXXABI::~CIRGenCXXABI() {}

CIRGenCXXABI::AddedStructorArgCounts CIRGenCXXABI::addImplicitConstructorArgs(
    CIRGenFunction &cgf, const CXXConstructorDecl *d, CXXCtorType type,
    bool forVirtualBase, bool delegating, CallArgList &args) {
  AddedStructorArgs addedArgs =
      getImplicitConstructorArgs(cgf, d, type, forVirtualBase, delegating);
  for (auto [idx, prefixArg] : llvm::enumerate(addedArgs.prefix))
    args.insert(args.begin() + 1 + idx,
                CallArg(RValue::get(prefixArg.value), prefixArg.type));
  for (const auto &arg : addedArgs.suffix)
    args.add(RValue::get(arg.value), arg.type);
  return AddedStructorArgCounts(addedArgs.prefix.size(),
                                addedArgs.suffix.size());
}

CatchTypeInfo CIRGenCXXABI::getCatchAllTypeInfo() {
  return CatchTypeInfo{{}, 0};
}

void CIRGenCXXABI::buildThisParam(CIRGenFunction &cgf,
                                  FunctionArgList &params) {
  const auto *md = cast<CXXMethodDecl>(cgf.curGD.getDecl());

  // FIXME: I'm not entirely sure I like using a fake decl just for code
  // generation. Maybe we can come up with a better way?
  auto *thisDecl =
      ImplicitParamDecl::Create(cgm.getASTContext(), nullptr, md->getLocation(),
                                &cgm.getASTContext().Idents.get("this"),
                                md->getThisType(), ImplicitParamKind::CXXThis);
  params.push_back(thisDecl);
  cgf.cxxabiThisDecl = thisDecl;

  // Compute the presumed alignment of 'this', which basically comes
  // down to whether we know it's a complete object or not.
  auto &layout = cgm.getASTContext().getASTRecordLayout(md->getParent());
  if (md->getParent()->getNumVBases() == 0 || // avoid vcall in common case
      md->getParent()->isEffectivelyFinal() ||
      isThisCompleteObject(cgf.curGD)) {
    cgf.cxxabiThisAlignment = layout.getAlignment();
  } else {
    cgf.cxxabiThisAlignment = layout.getNonVirtualAlignment();
  }
}

cir::GlobalLinkageKind CIRGenCXXABI::getCXXDestructorLinkage(
    GVALinkage linkage, const CXXDestructorDecl *dtor, CXXDtorType dt) const {
  // Delegate back to cgm by default.
  return cgm.getCIRLinkageForDeclarator(dtor, linkage,
                                        /*isConstantVariable=*/false);
}

mlir::Value CIRGenCXXABI::loadIncomingCXXThis(CIRGenFunction &cgf) {
  ImplicitParamDecl *vd = getThisDecl(cgf);
  Address addr = cgf.getAddrOfLocalVar(vd);
  return cir::LoadOp::create(cgf.getBuilder(), cgf.getLoc(vd->getLocation()),
                             addr.getElementType(), addr.getPointer());
}

void CIRGenCXXABI::setCXXABIThisValue(CIRGenFunction &cgf,
                                      mlir::Value thisPtr) {
  /// Initialize the 'this' slot.
  assert(getThisDecl(cgf) && "no 'this' variable for function");
  cgf.cxxabiThisValue = thisPtr;
}

CharUnits CIRGenCXXABI::getArrayCookieSize(const CXXNewExpr *e) {
  if (!requiresArrayCookie(e))
    return CharUnits::Zero();

  return getArrayCookieSizeImpl(e->getAllocatedType());
}

bool CIRGenCXXABI::requiresArrayCookie(const CXXNewExpr *e) {
  // If the class's usual deallocation function takes two arguments,
  // it needs a cookie.
  if (e->doesUsualArrayDeleteWantSize())
    return true;

  return e->getAllocatedType().isDestructedType();
}

bool CIRGenCXXABI::requiresArrayCookie(const CXXDeleteExpr *e,
                                       QualType elementType) {
  // If the class's usual deallocation function takes two arguments,
  // it needs a cookie.
  if (e->doesUsualArrayDeleteWantSize())
    return true;

  return elementType.isDestructedType();
}

void CIRGenCXXABI::readArrayCookie(CIRGenFunction &cgf, Address ptr,
                                   const CXXDeleteExpr *e,
                                   QualType elementType,
                                   mlir::Value &numElements,
                                   mlir::Value &allocPtr,
                                   CharUnits &cookieSize) {
  auto &builder = cgf.getBuilder();
  mlir::Type byteTy = builder.getUInt8Ty();
  ptr = ptr.withElementType(builder, byteTy);

  if (!requiresArrayCookie(e, elementType)) {
    allocPtr = ptr.emitRawPointer();
    numElements = nullptr;
    cookieSize = CharUnits::Zero();
    return;
  }

  mlir::Location loc = cgf.curFn->getLoc();
  cookieSize = getArrayCookieSizeImpl(elementType);
  mlir::Value negCookieSize =
      builder.getSignedInt(loc, -cookieSize.getQuantity(), /*width=*/32);
  mlir::Value allocBytePtr = builder.createPtrStride(loc, ptr.getPointer(),
                                                     negCookieSize);
  Address allocAddr(allocBytePtr, byteTy,
                    ptr.getAlignment().alignmentAtOffset(-cookieSize),
                    ptr.isKnownNonNull());
  allocPtr = allocAddr.emitRawPointer();
  numElements = readArrayCookieImpl(cgf, allocAddr, cookieSize);
}

void CIRGenCXXABI::readArrayCookie(CIRGenFunction &cgf, Address ptr,
                                   QualType elementType,
                                   mlir::Value &numElements,
                                   mlir::Value &allocPtr,
                                   CharUnits &cookieSize) {
  assert(elementType.isDestructedType());

  auto &builder = cgf.getBuilder();
  mlir::Type byteTy = builder.getUInt8Ty();
  ptr = ptr.withElementType(builder, byteTy);

  mlir::Location loc = cgf.curFn->getLoc();
  cookieSize = getArrayCookieSizeImpl(elementType);
  mlir::Value negCookieSize =
      builder.getSignedInt(loc, -cookieSize.getQuantity(), /*width=*/32);
  mlir::Value allocBytePtr = builder.createPtrStride(loc, ptr.getPointer(),
                                                     negCookieSize);
  Address allocAddr(allocBytePtr, byteTy,
                    ptr.getAlignment().alignmentAtOffset(-cookieSize),
                    ptr.isKnownNonNull());
  allocPtr = allocAddr.emitRawPointer();
  numElements = readArrayCookieImpl(cgf, allocAddr, cookieSize);
}

bool CIRGenCXXABI::isZeroInitializable(const MemberPointerType *mpt) {
  // Fake answer.
  return true;
}

void CIRGenCXXABI::emitReturnFromThunk(CIRGenFunction &cgf, RValue rv,
                                       QualType resultType) {
  assert(!cgf.hasAggregateEvaluationKind(resultType) &&
         "cannot handle aggregates");
  auto loc = cgf.curFn->getLoc();
  // Thunks are forwarding trampolines; when the return value is already a
  // scalar SSA value, return it directly instead of routing through
  // emitReturnOfRValue's temporary return slot path.
  if (rv.isScalar()) {
    cir::ReturnOp::create(cgf.getBuilder(), loc, rv.getValue());
    return;
  }
  cgf.emitReturnOfRValue(loc, rv, resultType);
}

void CIRGenCXXABI::emitGuardedInit(CIRGenFunction &cgf, const VarDecl &d,
                                   cir::GlobalOp var, bool shouldPerformInit) {
  // Default: not supported. ABI-specific subclasses should override.
  cgm.errorNYI(d.getSourceRange(), "emitGuardedInit: unsupported ABI");
}


// OGCG has emitTerminateForUnexpectedException which calls std::terminate
// via a nounwind runtime call when an unexpected exception is thrown. CIR
// does not implement this base class method yet.
