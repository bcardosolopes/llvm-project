//===--- CIRGenPointerAuth.cpp - CIR generation for ptr auth --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains common routines relating to the emission of
// pointer authentication operations.
//
//===----------------------------------------------------------------------===//

#include "CIRGenFunction.h"

using namespace clang;
using namespace clang::CIRGen;

namespace {

static CIRGenRuntimePointerAuthInfo
toPointerAuthInfo(const CIRGenPointerAuthInfo &info) {
  return CIRGenRuntimePointerAuthInfo{
      info.authenticationMode, info.isIsaPointer,
      info.authenticatesNullValues, info.key, mlir::Value{}};
}

static bool isZeroConstant(mlir::Value value) {
  if (!value)
    return false;
  if (auto constant = value.getDefiningOp<cir::ConstantOp>())
    if (auto intAttr = mlir::dyn_cast<cir::IntAttr>(constant.getValue()))
      return intAttr.getValue().isZero();
  return false;
}

static mlir::Value castPtrauthResultToOriginalType(CIRGenFunction &cgf,
                                                   mlir::Location loc,
                                                   mlir::Value value,
                                                   mlir::Type originalType) {
  if (value.getType() == originalType)
    return value;

  if (mlir::isa<cir::PointerType>(originalType))
    return cgf.getBuilder().createIntToPtr(value, originalType);

  if (auto intTy = mlir::dyn_cast<cir::IntType>(originalType)) {
    auto uintPtrTy = mlir::cast<cir::IntType>(cgf.cgm.uIntPtrTy);
    auto uintPtrWidth = uintPtrTy.getWidth();
    auto origWidth = intTy.getWidth();
    if (origWidth == uintPtrWidth)
      return cgf.getBuilder().createBitcast(loc, value, originalType);

    if (origWidth < uintPtrWidth)
      return cgf.getBuilder().createCast(loc, cir::CastKind::integral, value,
                                         originalType);

    return cgf.getBuilder().createCast(loc, cir::CastKind::integral, value,
                                       originalType);
  }

  return value;
}

static bool equalAuthPolicies(const CIRGenRuntimePointerAuthInfo &left,
                              const CIRGenRuntimePointerAuthInfo &right) {
  assert((left || right) && "should not be called if neither side is signed");
  if (static_cast<bool>(left) != static_cast<bool>(right))
    return false;
  return left.key == right.key &&
         left.authenticationMode == right.authenticationMode &&
         left.isIsaPointer == right.isIsaPointer &&
         left.authenticatesNullValues == right.authenticatesNullValues &&
         left.discriminator == right.discriminator;
}

static CIRGenRuntimePointerAuthInfo
getPointerAuthInfoForPointeeType(CIRGenFunction &cgf, QualType pointeeType) {
  if (pointeeType.isNull())
    return {};

  if (pointeeType->isFunctionType()) {
    if (std::optional<CIRGenPointerAuthInfo> info =
            cgf.cgm.getFunctionPointerAuthInfo(pointeeType)) {
      CIRGenRuntimePointerAuthInfo authInfo = toPointerAuthInfo(*info);
      if (info->discriminator) {
        mlir::Location loc = mlir::UnknownLoc::get(&cgf.getMLIRContext());
        authInfo.discriminator =
            cgf.getBuilder().getConstantInt(loc, cgf.cgm.uIntPtrTy,
                                            info->discriminator);
      }
      return authInfo;
    }
  }

  return {};
}

static CIRGenRuntimePointerAuthInfo
getPointerAuthInfoForType(CIRGenFunction &cgf, QualType pointerType) {
  if (pointerType.isNull())
    return {};

  if (pointerType->isBlockPointerType())
    return {};

  QualType pointeeType = pointerType->getPointeeType();
  if (pointeeType.isNull())
    return {};

  return getPointerAuthInfoForPointeeType(cgf, pointeeType);
}

static std::pair<mlir::Value, CIRGenRuntimePointerAuthInfo>
emitLoadOfOrigPointerRValue(CIRGenFunction &cgf, LValue lv,
                            SourceLocation loc) {
  mlir::Value value = cgf.emitLoadOfScalar(lv, loc);
  CIRGenRuntimePointerAuthInfo authInfo;
  if (PointerAuthQualifier ptrAuth =
          lv.getQuals().getPointerAuth().withoutKeyNone())
    authInfo = cgf.emitPointerAuthInfo(ptrAuth, lv.getAddress());
  else
    authInfo = getPointerAuthInfoForType(cgf, lv.getType());
  return {value, authInfo};
}

static std::pair<mlir::Value, CIRGenRuntimePointerAuthInfo>
emitOrigPointerRValue(CIRGenFunction &cgf, const Expr *e) {
  assert(e->getType()->isSignableType(cgf.getContext()));

  e = e->IgnoreParens();
  if (const auto *load = dyn_cast<ImplicitCastExpr>(e)) {
    if (load->getCastKind() == CK_LValueToRValue) {
      e = load->getSubExpr()->IgnoreParens();

      // Fold away uses of constant globals/references so we preserve the
      // original storage/discriminator instead of materializing a fresh scalar
      // with only the pointee type's default function-pointer auth.
      if (const auto *refExpr = dyn_cast<DeclRefExpr>(e)) {
        if (auto constant = cgf.tryEmitAsConstant(refExpr)) {
          if (!constant.isReference())
            return {cgf.emitScalarConstant(constant, const_cast<DeclRefExpr *>(
                                                         refExpr)),
                    getPointerAuthInfoForType(cgf, refExpr->getType())};

          LValue lv = constant.getReferenceLValue(
              cgf, const_cast<DeclRefExpr *>(refExpr));
          return emitLoadOfOrigPointerRValue(cgf, lv, refExpr->getLocation());
        }
      }

      LValue lv = cgf.emitLValue(e);
      return emitLoadOfOrigPointerRValue(cgf, lv, e->getExprLoc());
    }
  }

  mlir::Value value = cgf.emitScalarExpr(e);
  return {value, getPointerAuthInfoForType(cgf, e->getType())};
}

} // namespace

Address CIRGenFunction::getAsNaturalAddressOf(Address addr,
                                              QualType pointeeTy) {
  return addr;
}

mlir::Value CIRGenFunction::emitPointerAuthBlendDiscriminator(
    mlir::Value storageAddress, mlir::Value discriminator) {
  mlir::Location loc = storageAddress.getLoc();
  storageAddress = builder.createPtrToInt(storageAddress, cgm.uIntPtrTy);
  return cir::LLVMIntrinsicCallOp::create(
             builder, loc, cgm.getStringAttrCached("ptrauth.blend"),
             cgm.uIntPtrTy,
             mlir::ValueRange{storageAddress, discriminator})
      .getResult();
}

CIRGenRuntimePointerAuthInfo CIRGenFunction::emitPointerAuthInfo(
    PointerAuthQualifier qual, Address storageAddress) {
  assert(qual && "don't call this if you don't know the qualifier is present");
  if (qual.hasKeyNone())
    return {};

  CIRGenRuntimePointerAuthInfo info{
      qual.getAuthenticationMode(), qual.isIsaPointer(),
      qual.authenticatesNullValues(), qual.getKey(), mlir::Value{}};

  mlir::Location loc = storageAddress.isValid()
                           ? storageAddress.getPointer().getLoc()
                           : (currSrcLoc ? *currSrcLoc
                                         : mlir::UnknownLoc::get(
                                               &getMLIRContext()));
  if (unsigned extra = qual.getExtraDiscriminator())
    info.discriminator = builder.getConstantInt(loc, cgm.uIntPtrTy, extra);

  if (qual.isAddressDiscriminated()) {
    assert(storageAddress.isValid() &&
           "address discrimination requires a storage address");
    mlir::Value storagePtr = storageAddress.emitRawPointer();
    if (info.discriminator)
      info.discriminator =
          emitPointerAuthBlendDiscriminator(storagePtr, info.discriminator);
    else
      info.discriminator = builder.createPtrToInt(storagePtr, cgm.uIntPtrTy);
  }

  return info;
}

mlir::Value CIRGenFunction::emitPointerAuthSign(
    const CIRGenRuntimePointerAuthInfo &pointerAuth, mlir::Value pointer) {
  if (!pointerAuth.shouldSign())
    return pointer;

  mlir::Location loc = pointer.getLoc();
  mlir::Type origType = pointer.getType();
  bool wasPtr = mlir::isa<cir::PointerType>(origType);
  if (wasPtr)
    pointer = builder.createPtrToInt(pointer, cgm.uIntPtrTy);

  mlir::Value key = builder.getConstantInt(loc, builder.getUInt32Ty(),
                                           pointerAuth.key);
  mlir::Value discriminator =
      pointerAuth.discriminator
          ? pointerAuth.discriminator
          : builder.getNullValue(cgm.uIntPtrTy, loc).getResult();
  mlir::Value signedPointer =
      cir::LLVMIntrinsicCallOp::create(
          builder, loc, cgm.getStringAttrCached("ptrauth.sign"),
          cgm.uIntPtrTy, mlir::ValueRange{pointer, key, discriminator})
          .getResult();
  return castPtrauthResultToOriginalType(*this, loc, signedPointer, origType);
}

mlir::Value CIRGenFunction::emitPointerAuthAuth(
    const CIRGenRuntimePointerAuthInfo &pointerAuth, mlir::Value pointer) {
  mlir::Location loc = pointer.getLoc();
  mlir::Type origType = pointer.getType();
  bool wasPtr = mlir::isa<cir::PointerType>(origType);
  if (wasPtr)
    pointer = builder.createPtrToInt(pointer, cgm.uIntPtrTy);

  if (pointerAuth.shouldStrip()) {
    mlir::Value key = builder.getConstantInt(loc, builder.getUInt32Ty(),
                                             pointerAuth.key);
    mlir::Value strippedPointer =
        cir::LLVMIntrinsicCallOp::create(
            builder, loc, cgm.getStringAttrCached("ptrauth.strip"),
            cgm.uIntPtrTy, mlir::ValueRange{pointer, key})
            .getResult();
    return castPtrauthResultToOriginalType(*this, loc, strippedPointer,
                                           origType);
  }

  if (!pointerAuth.shouldAuth())
    return pointer;

  mlir::Value key = builder.getConstantInt(loc, builder.getUInt32Ty(),
                                           pointerAuth.key);
  mlir::Value discriminator =
      pointerAuth.discriminator
          ? pointerAuth.discriminator
          : builder.getNullValue(cgm.uIntPtrTy, loc).getResult();
  mlir::Value authedPointer =
      cir::LLVMIntrinsicCallOp::create(
          builder, loc, cgm.getStringAttrCached("ptrauth.auth"),
          cgm.uIntPtrTy, mlir::ValueRange{pointer, key, discriminator})
          .getResult();
  return castPtrauthResultToOriginalType(*this, loc, authedPointer, origType);
}

mlir::Value CIRGenFunction::emitPointerAuthResignCall(
    mlir::Value value, const CIRGenRuntimePointerAuthInfo &curAuth,
    const CIRGenRuntimePointerAuthInfo &newAuth) {
  assert(curAuth && newAuth);

  if (curAuth.authenticationMode != PointerAuthenticationMode::SignAndAuth ||
      newAuth.authenticationMode != PointerAuthenticationMode::SignAndAuth) {
    mlir::Value authedValue = emitPointerAuthAuth(curAuth, value);
    return emitPointerAuthSign(newAuth, authedValue);
  }

  mlir::Location loc = value.getLoc();
  mlir::Type origType = value.getType();
  bool wasPtr = mlir::isa<cir::PointerType>(origType);
  if (wasPtr)
    value = builder.createPtrToInt(value, cgm.uIntPtrTy);

  mlir::Value curKey =
      builder.getConstantInt(loc, builder.getUInt32Ty(), curAuth.key);
  mlir::Value newKey =
      builder.getConstantInt(loc, builder.getUInt32Ty(), newAuth.key);
  mlir::Value curDiscriminator =
      curAuth.discriminator
          ? curAuth.discriminator
          : builder.getNullValue(cgm.uIntPtrTy, loc).getResult();
  mlir::Value newDiscriminator =
      newAuth.discriminator
          ? newAuth.discriminator
          : builder.getNullValue(cgm.uIntPtrTy, loc).getResult();

  mlir::Value resignedValue =
      cir::LLVMIntrinsicCallOp::create(
          builder, loc, cgm.getStringAttrCached("ptrauth.resign"),
          cgm.uIntPtrTy,
          mlir::ValueRange{value, curKey, curDiscriminator, newKey,
                           newDiscriminator})
          .getResult();
  return castPtrauthResultToOriginalType(*this, loc, resignedValue, origType);
}

mlir::Value CIRGenFunction::emitPointerAuthResign(
    mlir::Value value, QualType type,
    const CIRGenRuntimePointerAuthInfo &curAuthInfo,
    const CIRGenRuntimePointerAuthInfo &newAuthInfo, bool isKnownNonNull) {
  if (!curAuthInfo && !newAuthInfo)
    return value;

  mlir::Location loc = value.getLoc();
  mlir::Value nullValue = builder.getNullValue(value.getType(), loc);
  (void)type;

  if (isZeroConstant(value))
    return value;

  if (equalAuthPolicies(curAuthInfo, newAuthInfo)) {
    mlir::Value curDiscriminator = curAuthInfo.discriminator;
    mlir::Value newDiscriminator = newAuthInfo.discriminator;
    if (curDiscriminator == newDiscriminator)
      return value;
    if ((!curDiscriminator && isZeroConstant(newDiscriminator)) ||
        (!newDiscriminator && isZeroConstant(curDiscriminator)))
      return value;
  }

  auto applyResign = [&]() -> mlir::Value {
    if (!newAuthInfo)
      return emitPointerAuthAuth(curAuthInfo, value);
    if (!curAuthInfo)
      return emitPointerAuthSign(newAuthInfo, value);
    return emitPointerAuthResignCall(value, curAuthInfo, newAuthInfo);
  };

  if (isKnownNonNull)
    return applyResign();

  mlir::Value isNonNull =
      cir::CmpOp::create(builder, loc, cir::CmpOpKind::ne, value, nullValue);
  return cir::TernaryOp::create(
             builder, loc, isNonNull,
             [&](mlir::OpBuilder &, mlir::Location ternaryLoc) {
               cir::YieldOp::create(builder, ternaryLoc, applyResign());
             },
             [&](mlir::OpBuilder &, mlir::Location ternaryLoc) {
               cir::YieldOp::create(builder, ternaryLoc, nullValue);
             })
      .getResult();
}

mlir::Value CIRGenFunction::authPointerToPointerCast(mlir::Value resultPtr,
                                                     QualType sourceType,
                                                     QualType destType) {
  CIRGenRuntimePointerAuthInfo curAuthInfo, newAuthInfo;
  if (sourceType->isSignableType(getContext()))
    curAuthInfo = getPointerAuthInfoForType(*this, sourceType);

  if (destType->isSignableType(getContext()))
    newAuthInfo = getPointerAuthInfoForType(*this, destType);

  if (!curAuthInfo && !newAuthInfo)
    return resultPtr;

  // If only one side of the cast is a function pointer, still resign to
  // preserve OGCG's casts to/from opaque/data pointers.
  if (!curAuthInfo && destType->isFunctionPointerType()) {
    if (std::optional<CIRGenPointerAuthInfo> info =
            cgm.getFunctionPointerAuthInfo(sourceType))
      curAuthInfo = toPointerAuthInfo(*info);
  }

  if (!newAuthInfo && sourceType->isFunctionPointerType()) {
    if (std::optional<CIRGenPointerAuthInfo> info =
            cgm.getFunctionPointerAuthInfo(destType))
      newAuthInfo = toPointerAuthInfo(*info);
  }

  return emitPointerAuthResign(resultPtr, destType, curAuthInfo, newAuthInfo,
                               /*isKnownNonNull=*/false);
}

mlir::Value CIRGenFunction::emitPointerAuthQualify(
    PointerAuthQualifier destQualifier, const Expr *e,
    Address destStorageAddress) {
  assert(destQualifier);
  auto [value, curAuthInfo] = emitOrigPointerRValue(*this, e);
  CIRGenRuntimePointerAuthInfo destAuthInfo =
      emitPointerAuthInfo(destQualifier, destStorageAddress);
  return emitPointerAuthResign(value, e->getType(), curAuthInfo, destAuthInfo,
                               /*isKnownNonNull=*/false);
}

mlir::Value CIRGenFunction::emitPointerAuthQualify(
    PointerAuthQualifier destQualifier, mlir::Value value, QualType pointerType,
    Address destStorageAddress, bool isKnownNonNull) {
  assert(destQualifier);
  CIRGenRuntimePointerAuthInfo curAuthInfo =
      getPointerAuthInfoForType(*this, pointerType);
  CIRGenRuntimePointerAuthInfo destAuthInfo =
      emitPointerAuthInfo(destQualifier, destStorageAddress);
  return emitPointerAuthResign(value, pointerType, curAuthInfo, destAuthInfo,
                               isKnownNonNull);
}

mlir::Value CIRGenFunction::emitPointerAuthUnqualify(
    PointerAuthQualifier curQualifier, mlir::Value value, QualType pointerType,
    Address curStorageAddress, bool isKnownNonNull) {
  assert(curQualifier);
  CIRGenRuntimePointerAuthInfo curAuthInfo =
      emitPointerAuthInfo(curQualifier, curStorageAddress);
  CIRGenRuntimePointerAuthInfo destAuthInfo =
      getPointerAuthInfoForType(*this, pointerType);
  return emitPointerAuthResign(value, pointerType, curAuthInfo, destAuthInfo,
                               isKnownNonNull);
}

void CIRGenFunction::emitPointerAuthCopy(PointerAuthQualifier qualifier,
                                         QualType type, Address destAddress,
                                         Address srcAddress) {
  assert(qualifier);
  mlir::Location loc = srcAddress.getPointer().getLoc();
  mlir::Value value = builder.createAlignedLoad(
      loc, srcAddress.getElementType(), srcAddress.getPointer(),
      srcAddress.getAlignment());

  if (qualifier.isAddressDiscriminated()) {
    CIRGenRuntimePointerAuthInfo srcAuthInfo =
        emitPointerAuthInfo(qualifier, srcAddress);
    CIRGenRuntimePointerAuthInfo destAuthInfo =
        emitPointerAuthInfo(qualifier, destAddress);
    value = emitPointerAuthResign(value, type, srcAuthInfo, destAuthInfo,
                                  /*isKnownNonNull=*/false);
  }

  builder.createStore(loc, value, destAddress);
}
