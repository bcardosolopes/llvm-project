//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Emit OpenMP clause nodes as CIR code.
//
//===----------------------------------------------------------------------===//

#include "CIRGenFunction.h"
#include "CIRGenCXXABI.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/ExprOpenMP.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/Support/raw_ostream.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include <cctype>
#include <limits>

using namespace clang;
using namespace clang::CIRGen;

namespace {
static constexpr llvm::StringLiteral kOpenMPTaskReductionSizeSymAttr =
    "omp.frontend.task_reduction_size_sym";

enum class OpenMPReductionKind {
  Add,
  Sub,
  Mul,
  And,
  Or,
  Xor,
  LAnd,
  LOr,
  Min,
  Max,
};

struct OpenMPDependPointerAndSize {
  mlir::Value pointer;
  mlir::Value size;
};

static mlir::Value emitOpenMPIntegerExpr(CIRGenFunction &cgf, const Expr *e) {
  QualType ty = e->getType();
  if (!ty->isIntegerType() && !ty->isEnumeralType()) {
    cgf.getCIRGenModule().errorNYI(e->getSourceRange(),
                                   "OpenMP clause expression with non-integer "
                                   "type");
    return {};
  }

  mlir::Value expr = cgf.emitScalarExpr(e);
  mlir::Location exprLoc = cgf.getLoc(e->getBeginLoc());
  auto conversionOp = mlir::UnrealizedConversionCastOp::create(
      cgf.getBuilder(), exprLoc, getOpenMPIntegerType(cgf, ty), expr);
  return conversionOp.getResult(0);
}

static mlir::Value emitOpenMPLinearStepExpr(CIRGenFunction &cgf,
                                            const Expr *e) {
  if (!e) {
    mlir::Location loc = mlir::UnknownLoc::get(&cgf.getMLIRContext());
    mlir::Value cirConst =
        cgf.getBuilder().getConstInt(loc, cgf.getBuilder().getSInt32Ty(), 1);
    auto conversionOp = mlir::UnrealizedConversionCastOp::create(
        cgf.getBuilder(), loc, cgf.getBuilder().getI32Type(), cirConst);
    return conversionOp.getResult(0);
  }

  QualType ty = e->getType();
  if (!ty->isIntegerType() && !ty->isEnumeralType()) {
    cgf.getCIRGenModule().errorNYI(e->getSourceRange(),
                                   "OpenMP linear step with non-integer type");
    return {};
  }

  QualType stepTy = cgf.getContext().getIntTypeForBitwidth(
      /*DestWidth=*/32, ty->isSignedIntegerOrEnumerationType());
  mlir::Value expr = cgf.emitScalarExpr(e);
  expr = cgf.emitScalarConversion(expr, ty, stepTy, e->getExprLoc());

  mlir::Location exprLoc = cgf.getLoc(e->getBeginLoc());
  auto conversionOp = mlir::UnrealizedConversionCastOp::create(
      cgf.getBuilder(), exprLoc, cgf.getBuilder().getI32Type(), expr);
  return conversionOp.getResult(0);
}

static mlir::Value scaleOpenMPLinearStepValue(CIRGenFunction &cgf,
                                              mlir::Location loc,
                                              mlir::Value step,
                                              QualType linearTy) {
  linearTy = linearTy.getNonReferenceType();
  if (!linearTy->isPointerType())
    return step;

  uint64_t pointeeSize =
      cgf.getContext().getTypeSizeInChars(linearTy->getPointeeType())
          .getQuantity();
  if (pointeeSize == 1)
    return step;
  if (pointeeSize > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
    cgf.cgm.errorNYI(SourceRange(),
                     "OpenMP linear pointer step with oversized pointee");
    return {};
  }

  auto scaleConst = mlir::LLVM::ConstantOp::create(
      cgf.getBuilder(), loc, cgf.getBuilder().getI32Type(),
      cgf.getBuilder().getI32IntegerAttr(static_cast<int64_t>(pointeeSize)));
  return mlir::LLVM::MulOp::create(cgf.getBuilder(), loc, step, scaleConst);
}

static mlir::Type getOpenMPLinearVarType(CIRGenFunction &cgf, QualType ty) {
  ty = ty.getNonReferenceType();
  if (ty->isIntegerType() || ty->isEnumeralType())
    return getOpenMPIntegerType(cgf, ty);
  if (ty->isPointerType()) {
    unsigned targetAS = cgf.getContext().getTargetAddressSpace(
        ty->getPointeeType().getAddressSpace());
    return mlir::LLVM::LLVMPointerType::get(&cgf.getMLIRContext(), targetAS);
  }

  mlir::Type cirTy = cgf.convertTypeForMem(ty);
  mlir::MLIRContext &ctx = cgf.getMLIRContext();
  if (mlir::isa<cir::FP16Type>(cirTy))
    return mlir::Float16Type::get(&ctx);
  if (mlir::isa<cir::BF16Type>(cirTy))
    return mlir::BFloat16Type::get(&ctx);
  if (mlir::isa<cir::SingleType>(cirTy))
    return mlir::Float32Type::get(&ctx);
  if (mlir::isa<cir::DoubleType>(cirTy))
    return mlir::Float64Type::get(&ctx);
  if (mlir::isa<cir::FP80Type>(cirTy))
    return mlir::Float80Type::get(&ctx);
  if (mlir::isa<cir::FP128Type>(cirTy))
    return mlir::Float128Type::get(&ctx);
  if (auto longDoubleTy = mlir::dyn_cast<cir::LongDoubleType>(cirTy)) {
    mlir::Type underlying = longDoubleTy.getUnderlying();
    if (mlir::isa<cir::DoubleType>(underlying))
      return mlir::Float64Type::get(&ctx);
    if (mlir::isa<cir::FP80Type>(underlying))
      return mlir::Float80Type::get(&ctx);
    if (mlir::isa<cir::FP128Type>(underlying))
      return mlir::Float128Type::get(&ctx);
  }

  return {};
}
static mlir::Value createOpenMPI64Constant(CIRGenFunction &cgf,
                                           mlir::Location loc, int64_t value) {
  return mlir::LLVM::ConstantOp::create(cgf.getBuilder(), loc,
                                        cgf.getBuilder().getI64Type(), value);
}

static mlir::Value castOpenMPValueToI64(CIRGenFunction &cgf,
                                        mlir::Location loc,
                                        mlir::Value value) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  auto i64Ty = mlir::IntegerType::get(&cgf.getMLIRContext(), 64);

  auto castBuiltinInt = [&](mlir::Value intValue,
                            bool isSigned) -> mlir::Value {
    auto intTy = mlir::cast<mlir::IntegerType>(intValue.getType());
    if (intTy == i64Ty)
      return intValue;
    if (intTy.getWidth() < i64Ty.getWidth()) {
      if (isSigned)
        return mlir::LLVM::SExtOp::create(builder, loc, i64Ty, intValue);
      return mlir::LLVM::ZExtOp::create(builder, loc, i64Ty, intValue);
    }
    return mlir::LLVM::TruncOp::create(builder, loc, i64Ty, intValue);
  };

  if (auto intTy = mlir::dyn_cast<mlir::IntegerType>(value.getType()))
    return castBuiltinInt(value, /*isSigned=*/false);

  if (auto cirIntTy = mlir::dyn_cast<cir::IntType>(value.getType())) {
    auto builtinTy =
        mlir::IntegerType::get(&cgf.getMLIRContext(), cirIntTy.getWidth());
    mlir::Value builtinInt = mlir::UnrealizedConversionCastOp::create(
                                 builder, loc, builtinTy, value)
                                 .getResult(0);
    return castBuiltinInt(builtinInt, cirIntTy.isSigned());
  }

  if (mlir::isa<cir::BoolType>(value.getType())) {
    mlir::Value i1 =
        mlir::UnrealizedConversionCastOp::create(builder, loc,
                                                 builder.getI1Type(), value)
            .getResult(0);
    return mlir::LLVM::ZExtOp::create(builder, loc, i64Ty, i1);
  }

  cgf.getCIRGenModule().errorNYI(
      SourceRange(), "OpenMP map bound integer conversion to i64");
  return {};
}

static mlir::Value castOpenMPValueToCIRInt(CIRGenFunction &cgf,
                                           mlir::Location loc,
                                           mlir::Value value,
                                           cir::IntType dstTy) {
  CIRGenBuilderTy &builder = cgf.getBuilder();

  if (auto cirIntTy = mlir::dyn_cast<cir::IntType>(value.getType())) {
    if (cirIntTy == dstTy)
      return value;
    return builder.createIntCast(value, dstTy);
  }

  auto castBuiltinInt = [&](mlir::Value intValue) -> mlir::Value {
    auto srcTy = mlir::cast<mlir::IntegerType>(intValue.getType());
    auto builtinDstTy =
        mlir::IntegerType::get(&cgf.getMLIRContext(), dstTy.getWidth());
    if (srcTy != builtinDstTy) {
      if (srcTy.getWidth() < builtinDstTy.getWidth())
        intValue = mlir::LLVM::ZExtOp::create(builder, loc, builtinDstTy,
                                              intValue);
      else
        intValue = mlir::LLVM::TruncOp::create(builder, loc, builtinDstTy,
                                               intValue);
    }
    return mlir::UnrealizedConversionCastOp::create(builder, loc, dstTy,
                                                    intValue)
        .getResult(0);
  };

  if (mlir::isa<mlir::IntegerType>(value.getType()))
    return castBuiltinInt(value);

  if (mlir::isa<cir::BoolType>(value.getType())) {
    mlir::Value i1 =
        mlir::UnrealizedConversionCastOp::create(builder, loc,
                                                 builder.getI1Type(), value)
            .getResult(0);
    return castBuiltinInt(i1);
  }

  cgf.getCIRGenModule().errorNYI(SourceRange(),
                                 "OpenMP integer conversion to CIR integer");
  return {};
}

static mlir::Value emitOpenMPIndexExpr(CIRGenFunction &cgf, const Expr *e) {
  QualType ty = e->getType();
  if (!ty->isIntegerType() && !ty->isEnumeralType()) {
    cgf.getCIRGenModule().errorNYI(e->getSourceRange(),
                                   "OpenMP map bound expression with "
                                   "non-integer type");
    return {};
  }

  return castOpenMPValueToI64(cgf, cgf.getLoc(e->getBeginLoc()),
                              cgf.emitScalarExpr(e));
}

static mlir::Value getOpenMPUpperBoundFromExtent(CIRGenFunction &cgf,
                                                 mlir::Location loc,
                                                 mlir::Value lowerBound,
                                                 mlir::Value extent) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Value one = createOpenMPI64Constant(cgf, loc, 1);
  mlir::Value extentMinusOne =
      mlir::LLVM::SubOp::create(builder, loc, extent, one);
  return mlir::LLVM::AddOp::create(builder, loc, lowerBound, extentMinusOne);
}

struct OpenMPMapOperandInfo {
  mlir::Location beginLoc;
  mlir::Value varPtr;
  mlir::Value varPtrPtr;
  mlir::Type varType;
  const VarDecl *baseVar;
  std::string name;
  llvm::SmallVector<int64_t, 4> memberIndices;
  llvm::SmallVector<mlir::Value, 4> bounds;
};

static const OMPDeclareMapperDecl *getOpenMPMapperDecl(const Expr *e) {
  if (!e)
    return nullptr;

  e = e->IgnoreParenImpCasts();
  const auto *dre = dyn_cast<DeclRefExpr>(e);
  if (!dre)
    return nullptr;

  return dyn_cast<OMPDeclareMapperDecl>(dre->getDecl());
}

static const Expr *getOpenMPMapBaseExpr(const Expr *e) {
  e = e->IgnoreParenImpCasts();
  while (true) {
    if (const auto *section = dyn_cast<ArraySectionExpr>(e)) {
      e = section->getBase()->IgnoreParenImpCasts();
      continue;
    }
    if (const auto *subscript = dyn_cast<ArraySubscriptExpr>(e)) {
      e = subscript->getBase()->IgnoreParenImpCasts();
      continue;
    }
    if (const auto *member = dyn_cast<MemberExpr>(e)) {
      e = member->getBase()->IgnoreParenImpCasts();
      continue;
    }
    return e;
  }
}

static const VarDecl *getOpenMPMapBaseVarDecl(const Expr *e) {
  const auto *dre = dyn_cast<DeclRefExpr>(getOpenMPMapBaseExpr(e));
  const auto *vd = dre ? dyn_cast<VarDecl>(dre->getDecl()) : nullptr;
  return vd ? vd->getCanonicalDecl() : nullptr;
}

static bool isOpenMPMapCXXThisRooted(const Expr *e) {
  return isa<CXXThisExpr>(getOpenMPMapBaseExpr(e));
}

static const VarDecl *getOpenMPReductionBaseVarDecl(const Expr *e) {
  if (const auto *baseVD = getOpenMPMapBaseVarDecl(e))
    return baseVD;
  return nullptr;
}

static const VarDecl *
getOpenMPPlainTargetDevicePointerVarDecl(const Expr *e,
                                         bool allowArraySections = false) {
  e = e->IgnoreParenImpCasts();
  if (allowArraySections) {
    while (const auto *section = dyn_cast<ArraySectionExpr>(e))
      e = section->getBase()->IgnoreParenImpCasts();
    while (const auto *subscript = dyn_cast<ArraySubscriptExpr>(e))
      e = subscript->getBase()->IgnoreParenImpCasts();
  }

  const auto *dre = dyn_cast<DeclRefExpr>(e);
  const auto *vd = dre ? dyn_cast<VarDecl>(dre->getDecl()) : nullptr;
  if (!vd)
    return nullptr;

  vd = vd->getCanonicalDecl();
  return (vd->isLocalVarDeclOrParm() || vd->hasGlobalStorage()) ? vd
                                                                 : nullptr;
}

static bool isOpenMPReductionVarExpr(const Expr *e) {
  e = e->IgnoreParenImpCasts();
  return isa<DeclRefExpr, ArraySectionExpr, ArraySubscriptExpr>(e);
}

static llvm::SmallVector<int64_t, 4> getOpenMPMapMemberIndices(const Expr *e) {
  const Expr *curExpr = e->IgnoreParenImpCasts();
  while (const auto *section = dyn_cast<ArraySectionExpr>(curExpr))
    curExpr = section->getBase()->IgnoreParenImpCasts();
  while (const auto *subscript = dyn_cast<ArraySubscriptExpr>(curExpr))
    curExpr = subscript->getBase()->IgnoreParenImpCasts();

  llvm::SmallVector<const FieldDecl *, 4> fields;
  while (const auto *member = dyn_cast<MemberExpr>(curExpr)) {
    const auto *field = dyn_cast<FieldDecl>(member->getMemberDecl());
    if (!field)
      return {};
    fields.push_back(field);
    curExpr = member->getBase()->IgnoreParenImpCasts();
  }

  if (!isa<DeclRefExpr, CXXThisExpr>(curExpr))
    return {};

  llvm::SmallVector<int64_t, 4> memberIndices;
  memberIndices.reserve(fields.size());
  for (const FieldDecl *field : llvm::reverse(fields))
    memberIndices.push_back(field->getFieldIndex());
  return memberIndices;
}

static mlir::Value emitOpenMPArraySectionPointer(CIRGenFunction &cgf,
                                                 const Expr *expr,
                                                 bool useUpperBounds);

static std::optional<OpenMPMapOperandInfo>
getOpenMPMapOperandInfo(CIRGenFunction &cgf, const Expr *e,
                        bool usePointerValueForBarePointerExpr = false,
                        bool captureBoundedPointerSource = false) {
  const Expr *curVarExpr = e->IgnoreParenImpCasts();
  QualType originalType =
      curVarExpr->getType().getNonReferenceType().getUnqualifiedType();
  if (const auto *section = dyn_cast<ArraySectionExpr>(curVarExpr))
    originalType = section->getElementType();

  mlir::Location exprLoc = cgf.getLoc(curVarExpr->getBeginLoc());
  llvm::SmallVector<mlir::Value, 4> bounds;
  bool hasBounds = false;

  std::string exprString;
  llvm::raw_string_ostream os(exprString);
  e->printPretty(os, nullptr, cgf.getContext().getPrintingPolicy());
  os.flush();

  auto createBound = [&](mlir::Location boundLoc, mlir::Value lowerBound,
                         mlir::Value upperBound,
                         mlir::Value extent) -> mlir::Value {
    mlir::Value stride = createOpenMPI64Constant(cgf, boundLoc, 1);
    mlir::Value startIdx = createOpenMPI64Constant(cgf, boundLoc, 0);
    auto bound = mlir::omp::MapBoundsOp::create(
        cgf.getBuilder(), boundLoc,
        mlir::omp::MapBoundsType::get(&cgf.getMLIRContext()), lowerBound,
        upperBound, extent, stride,
        /*stride_in_bytes=*/false, startIdx);
    return bound.getResult();
  };

  while (isa<ArraySectionExpr, ArraySubscriptExpr>(curVarExpr)) {
    mlir::Location boundLoc = cgf.getLoc(curVarExpr->getBeginLoc());
    mlir::Value lowerBound;
    mlir::Value upperBound;
    mlir::Value extent;

    if (const auto *section = dyn_cast<ArraySectionExpr>(curVarExpr)) {
      lowerBound = section->getLowerBound()
                       ? emitOpenMPIndexExpr(cgf, section->getLowerBound())
                       : createOpenMPI64Constant(cgf, boundLoc, 0);

      if (const Expr *length = section->getLength()) {
        extent = emitOpenMPIndexExpr(cgf, length);
        if (!extent)
          return std::nullopt;
        upperBound =
            getOpenMPUpperBoundFromExtent(cgf, boundLoc, lowerBound, extent);
      } else if (const auto *arrayTy =
                     cgf.getContext().getAsConstantArrayType(
                         section->getBaseType())) {
        uint64_t size = arrayTy->getLimitedSize();
        extent = createOpenMPI64Constant(cgf, boundLoc, size);
        upperBound = createOpenMPI64Constant(
            cgf, boundLoc, static_cast<int64_t>(size - 1));
      } else if (const auto *vlaTy = cgf.getContext().getAsVariableArrayType(
                     section->getBaseType())) {
        extent = castOpenMPValueToI64(cgf, boundLoc,
                                      cgf.getVLAElements1D(vlaTy).numElts);
        upperBound =
            getOpenMPUpperBoundFromExtent(cgf, boundLoc, lowerBound, extent);
      } else {
        cgf.getCIRGenModule().errorNYI(
            section->getSourceRange(),
            "OpenMP target update array section without constant or VLA bound");
        return std::nullopt;
      }

      curVarExpr = section->getBase()->IgnoreParenImpCasts();
    } else {
      const auto *subscript = cast<ArraySubscriptExpr>(curVarExpr);
      lowerBound = emitOpenMPIndexExpr(cgf, subscript->getIdx());
      if (!lowerBound)
        return std::nullopt;
      extent = createOpenMPI64Constant(cgf, boundLoc, 1);
      upperBound = lowerBound;
      curVarExpr = subscript->getBase()->IgnoreParenImpCasts();
    }

    bounds.push_back(createBound(boundLoc, lowerBound, upperBound, extent));
    hasBounds = true;
  }

  QualType baseType =
      curVarExpr->getType().getNonReferenceType().getUnqualifiedType();
  if (!hasBounds)
    if (const auto *vlaTy = cgf.getContext().getAsVariableArrayType(baseType)) {
      mlir::Location boundLoc = cgf.getLoc(curVarExpr->getBeginLoc());
      mlir::Value lowerBound = createOpenMPI64Constant(cgf, boundLoc, 0);
      mlir::Value extent = castOpenMPValueToI64(
          cgf, boundLoc, cgf.getVLAElements1D(vlaTy).numElts);
      mlir::Value upperBound =
          getOpenMPUpperBoundFromExtent(cgf, boundLoc, lowerBound, extent);
      bounds.push_back(createBound(boundLoc, lowerBound, upperBound, extent));
      hasBounds = true;
    }

  mlir::Value varPtr;
  mlir::Value varPtrPtr;
  bool usePointerValue =
      usePointerValueForBarePointerExpr && !hasBounds && baseType->isPointerType();
  bool retainPointerContainer =
      captureBoundedPointerSource && hasBounds && baseType->isPointerType();
  if (hasBounds && isa<CXXThisExpr>(curVarExpr) && baseType->isPointerType()) {
    // Match OGCG's `map(this[0])` object lowering. The bounded expression is an
    // explicit object map rooted in `this`, not a pointer-container plus attach
    // mapping, so capture the object address directly and omit var_ptr_ptr.
    varPtr = emitOpenMPArraySectionPointer(cgf, e, /*useUpperBounds=*/false);
  } else if (const auto *memExpr = dyn_cast<MemberExpr>(curVarExpr)) {
    if ((hasBounds || usePointerValue) && baseType->isPointerType()) {
      varPtr = cgf.emitScalarExpr(curVarExpr);
      if (hasBounds)
        varPtrPtr = cgf.emitMemberExpr(memExpr).getPointer();
    } else {
      varPtr = cgf.emitMemberExpr(memExpr).getPointer();
    }
  } else if ((hasBounds || usePointerValue) && baseType->isPointerType()) {
    varPtr = cgf.emitScalarExpr(curVarExpr);
    if (retainPointerContainer) {
      if (const auto *dre = dyn_cast<DeclRefExpr>(curVarExpr))
        varPtrPtr = cgf.emitDeclRefLValue(dre).getPointer();
      else
        varPtrPtr = cgf.emitLValue(curVarExpr).getPointer();
    }
  } else if (const auto *dre = dyn_cast<DeclRefExpr>(curVarExpr)) {
    varPtr = cgf.emitDeclRefLValue(dre).getPointer();
  } else {
    varPtr = cgf.emitLValue(curVarExpr).getPointer();
  }

  if (!varPtr)
    return std::nullopt;

  mlir::Type varType;
  if (hasBounds &&
      (baseType->isPointerType() ||
       cgf.getContext().getAsVariableArrayType(baseType))) {
    varType = cgf.convertTypeForMem(originalType);
  } else {
    varType = cgf.convertTypeForMem(baseType);
  }

  return OpenMPMapOperandInfo{
      exprLoc,
      varPtr,
      varPtrPtr,
      varType,
      getOpenMPMapBaseVarDecl(e),
      std::move(exprString),
      getOpenMPMapMemberIndices(e),
      std::move(bounds)};
}

template <typename ClauseTy>
static std::optional<mlir::omp::ClauseMapFlags>
getOpenMPMotionModifierFlags(CIRGenFunction &cgf, const ClauseTy *clause,
                             bool allowMapperModifier);

static std::optional<mlir::omp::ClauseMapFlags>
getOpenMPMapClauseFlags(CIRGenFunction &cgf, const OMPMapClause *clause,
                        bool allowMapperModifier);

static mlir::omp::MapInfoOp
createOpenMPMapInfoOp(CIRGenBuilderTy &builder, const OpenMPMapOperandInfo &map,
                      mlir::omp::ClauseMapFlags mapFlags,
                      mlir::FlatSymbolRefAttr mapperId,
                      mlir::ValueRange members = mlir::ValueRange{},
                      mlir::ArrayAttr membersIndex = mlir::ArrayAttr{},
                      bool partialMap = false) {
  return mlir::omp::MapInfoOp::create(
      builder, map.beginLoc, map.varPtr.getType(), map.varPtr, map.varType,
      mapFlags, mlir::omp::VariableCaptureKind::ByRef, map.varPtrPtr, members,
      membersIndex, map.bounds, mapperId, builder.getStringAttr(map.name),
      partialMap);
}

static OpenMPMapOperandInfo
getOpenMPThisMapOperandInfo(CIRGenFunction &cgf, mlir::Location loc) {
  Address thisAddr = cgf.loadCXXThisAddress();
  return OpenMPMapOperandInfo{loc,
                              thisAddr.getPointer(),
                              mlir::Value{},
                              thisAddr.getElementType(),
                              nullptr,
                              "this",
                              {},
                              {}};
}

static mlir::FlatSymbolRefAttr
getOpenMPMapperId(CIRGenFunction &cgf, const Expr *mapperExpr,
                  SourceLocation fallbackLoc) {
  const OMPDeclareMapperDecl *mapperDecl = getOpenMPMapperDecl(mapperExpr);
  if (!mapperDecl) {
    if (mapperExpr) {
      cgf.getCIRGenModule().errorNYI(
          mapperExpr->getSourceRange(),
          "OpenMP mapper expression without declare mapper declaration");
    }
    return {};
  }

  return cgf.getCIRGenModule().getOrCreateOpenMPDeclareMapper(
      mapperDecl, cgf.getLoc(fallbackLoc));
}

template <typename ContainerTy>
static mlir::ArrayAttr createOpenMPMembersIndexAttr(CIRGenBuilderTy &builder,
                                                    const ContainerTy &indices) {
  llvm::SmallVector<mlir::Attribute, 4> attrs;
  attrs.reserve(indices.size());
  for (const auto &memberIndices : indices)
    attrs.push_back(builder.getI64ArrayAttr(memberIndices));
  return builder.getArrayAttr(attrs);
}

template <typename ClauseTy>
static std::optional<mlir::omp::ClauseMapFlags>
getOpenMPMotionModifierFlags(CIRGenFunction &cgf, const ClauseTy *clause,
                             bool allowMapperModifier) {
  mlir::omp::ClauseMapFlags flags = mlir::omp::ClauseMapFlags::none;
  for (OpenMPMotionModifierKind modifier : clause->getMotionModifiers()) {
    switch (modifier) {
    case OMPC_MOTION_MODIFIER_unknown:
      continue;
    case OMPC_MOTION_MODIFIER_present:
      flags |= mlir::omp::ClauseMapFlags::present;
      continue;
    case OMPC_MOTION_MODIFIER_mapper:
      if (allowMapperModifier)
        continue;
      cgf.getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                     "OpenMP target update mapper modifier");
      return std::nullopt;
    case OMPC_MOTION_MODIFIER_iterator:
      cgf.getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                     "OpenMP target update iterator modifier");
      return std::nullopt;
    }
    llvm_unreachable("unexpected OpenMP motion modifier");
  }

  return flags;
}

static std::optional<mlir::omp::ClauseMapFlags>
getOpenMPMapClauseFlags(CIRGenFunction &cgf, const OMPMapClause *clause,
                        bool allowMapperModifier = false) {
  mlir::omp::ClauseMapFlags flags = mlir::omp::ClauseMapFlags::none;
  if (clause->isImplicit())
    flags |= mlir::omp::ClauseMapFlags::implicit;

  switch (clause->getMapType()) {
  case OMPC_MAP_alloc:
  case OMPC_MAP_release:
    flags |= mlir::omp::ClauseMapFlags::storage;
    break;
  case OMPC_MAP_to:
    flags |= mlir::omp::ClauseMapFlags::to;
    break;
  case OMPC_MAP_from:
    flags |= mlir::omp::ClauseMapFlags::from;
    break;
  case OMPC_MAP_tofrom:
    flags |=
        mlir::omp::ClauseMapFlags::to | mlir::omp::ClauseMapFlags::from;
    break;
  case OMPC_MAP_delete:
    flags |= mlir::omp::ClauseMapFlags::del;
    break;
  case OMPC_MAP_unknown:
    cgf.getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                   "OpenMP target map clause kind");
    return std::nullopt;
  }

  for (OpenMPMapModifierKind modifier : clause->getMapTypeModifiers()) {
    switch (modifier) {
    case OMPC_MAP_MODIFIER_unknown:
      continue;
    case OMPC_MAP_MODIFIER_always:
      flags |= mlir::omp::ClauseMapFlags::always;
      continue;
    case OMPC_MAP_MODIFIER_close:
      flags |= mlir::omp::ClauseMapFlags::close;
      continue;
    case OMPC_MAP_MODIFIER_present:
      flags |= mlir::omp::ClauseMapFlags::present;
      continue;
    case OMPC_MAP_MODIFIER_ompx_hold:
      flags |= mlir::omp::ClauseMapFlags::ompx_hold;
      continue;
    case OMPC_MAP_MODIFIER_mapper:
      if (allowMapperModifier)
        continue;
      cgf.getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                     "OpenMP target map mapper modifier");
      return std::nullopt;
    case OMPC_MAP_MODIFIER_iterator:
      cgf.getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                     "OpenMP target map iterator modifier");
      return std::nullopt;
    case OMPC_MAP_MODIFIER_self:
      cgf.getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                     "OpenMP target map self modifier");
      return std::nullopt;
    case OMPC_MAP_MODIFIER_last:
      llvm_unreachable("unexpected OpenMP map modifier sentinel");
    }
    llvm_unreachable("unexpected OpenMP map modifier");
  }

  return flags;
}

static const VarDecl *getOpenMPClauseVarDeclOrNull(const Expr *e) {
  e = e ? e->IgnoreImpCasts() : nullptr;
  const auto *dre = dyn_cast_or_null<DeclRefExpr>(e);
  const auto *vd = dre ? dyn_cast<VarDecl>(dre->getDecl()) : nullptr;
  return vd ? vd->getCanonicalDecl() : nullptr;
}

static const Expr *unwrapOpenMPClauseCapturedExprInit(const Expr *e) {
  while (const VarDecl *vd = getOpenMPClauseVarDeclOrNull(e)) {
    const auto *capturedExpr = dyn_cast<OMPCapturedExprDecl>(vd);
    if (!capturedExpr)
      break;
    e = capturedExpr->getInit();
  }
  return e;
}

static const VarDecl *getOpenMPClauseVarDecl(const Expr *e) {
  const VarDecl *vd = getOpenMPClauseVarDeclOrNull(e);
  assert(vd && "expected OpenMP clause variable DeclRefExpr");
  return vd;
}

static const VarDecl *getOpenMPAllocatorVarDecl(const Expr *e) {
  e = e ? e->IgnoreParenImpCasts() : nullptr;
  const auto *dre = dyn_cast_or_null<DeclRefExpr>(e);
  const auto *vd = dre ? dyn_cast<VarDecl>(dre->getDecl()) : nullptr;
  return vd ? vd->getCanonicalDecl() : nullptr;
}

static bool isOpenMPAllocatablePrivateDecl(const VarDecl *vd) {
  if (!vd)
    return false;

  const VarDecl *canonical = vd->getCanonicalDecl();
  if (!canonical->hasAttr<OMPAllocateDeclAttr>())
    return false;

  const auto *attr = canonical->getAttr<OMPAllocateDeclAttr>();
  return !(attr->getAllocatorType() == OMPAllocateDeclAttr::OMPDefaultMemAlloc &&
           !attr->getAllocator());
}

static mlir::Value getOpenMPAllocatorOperand(CIRGenFunction &cgf,
                                             mlir::Location loc,
                                             const Expr *allocator) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  auto voidPtrTy = builder.getVoidPtrTy();
  if (!allocator)
    return builder.getNullValue(voidPtrTy, loc);

  mlir::Value allocatorValue = cgf.emitScalarExpr(allocator);
  if (allocatorValue.getType() == voidPtrTy)
    return allocatorValue;
  if (mlir::isa<cir::PointerType>(allocatorValue.getType()))
    return builder.createBitcast(loc, allocatorValue, voidPtrTy);
  return builder.createIntToPtr(allocatorValue, voidPtrTy);
}

template <typename OpTy>
static mlir::LogicalResult maybeAppendOpenMPPrivateAllocator(
    OpTy &operation, CIRGenFunction &cgf,
    CIRGenFunction::OpenMPClauseEmissionInfo &emissionInfo,
    const VarDecl *vd, const VarDecl *privateCopyVD, mlir::Value privateVar,
    const VarDecl *initVD = nullptr, bool initializeFromOriginal = false) {
  if constexpr (std::is_same_v<OpTy, mlir::omp::ParallelOp> ||
                std::is_same_v<OpTy, mlir::omp::WsloopOp>) {
    if (!isOpenMPAllocatablePrivateDecl(privateCopyVD))
      return mlir::success();

    const VarDecl *canonical = privateCopyVD->getCanonicalDecl();
    const auto *attr = canonical->getAttr<OMPAllocateDeclAttr>();
    if (attr->getAlignment()) {
      emissionInfo.explicitPrivateAllocatorVars.push_back(
          {vd->getCanonicalDecl(), canonical, initVD, initializeFromOriginal});
      return mlir::success();
    }

    operation.getAllocateVarsMutable().append(privateVar);
    operation.getAllocatorVarsMutable().append(getOpenMPAllocatorOperand(
        cgf, privateVar.getLoc(), attr->getAllocator()));
    return mlir::success();
  } else {
    return mlir::success();
  }
}

static const VarDecl *getOpenMPTargetAllocatorPrivateVarDecl(const Expr *e) {
  e = e ? e->IgnoreImpCasts() : nullptr;
  const auto *dre = dyn_cast_or_null<DeclRefExpr>(e);
  return dre ? dyn_cast<VarDecl>(dre->getDecl()) : nullptr;
}

static mlir::Value getOpenMPArrayElementCount(CIRGenFunction &cgf,
                                              mlir::Location loc,
                                              QualType arrayTy) {
  auto countTy = mlir::cast<cir::IntType>(cgf.sizeTy);
  mlir::Value totalCount;
  QualType curTy = arrayTy;
  bool sawArray = false;
  while (const auto *arrTy = cgf.getContext().getAsArrayType(curTy)) {
    sawArray = true;
    mlir::Value dimCount;
    if (const auto *constArrTy = dyn_cast<ConstantArrayType>(arrTy)) {
      dimCount =
          cgf.getBuilder().getConstInt(loc, countTy, constArrTy->getZExtSize());
    } else if (const auto *vlaTy = dyn_cast<VariableArrayType>(arrTy)) {
      dimCount = cgf.getVLAElements1D(vlaTy).numElts;
      if (dimCount.getType() != cgf.sizeTy)
        dimCount = castOpenMPValueToCIRInt(cgf, loc, dimCount, countTy);
    } else {
      llvm_unreachable(
          "OpenMP reduction array type with unsupported extent kind");
    }

    totalCount =
        totalCount
            ? cgf.getBuilder().createMul(loc, totalCount, dimCount,
                                         cir::OverflowBehavior::NoUnsignedWrap)
            : dimCount;
    curTy = arrTy->getElementType();
  }

  assert(sawArray && "expected OpenMP array type");
  return totalCount;
}

static mlir::Value getOpenMPArraySectionIndex(CIRGenFunction &cgf,
                                              const ArraySectionExpr *section,
                                              bool useUpperBound) {
  mlir::Location loc = cgf.getLoc(section->getSourceRange());
  auto indexTy = mlir::cast<cir::IntType>(cgf.ptrDiffTy);
  mlir::Value lowerBound =
      section->getLowerBound()
          ? emitOpenMPIndexExpr(cgf, section->getLowerBound())
          : cgf.getBuilder().getConstInt(loc, indexTy, 0);
  if (lowerBound && lowerBound.getType() != cgf.ptrDiffTy)
    lowerBound = castOpenMPValueToCIRInt(cgf, loc, lowerBound, indexTy);
  if (!lowerBound || !useUpperBound)
    return lowerBound;

  if (const Expr *length = section->getLength()) {
    mlir::Value extent = emitOpenMPIndexExpr(cgf, length);
    if (!extent)
      return {};
    if (extent.getType() != cgf.ptrDiffTy)
      extent = castOpenMPValueToCIRInt(cgf, loc, extent, indexTy);
    mlir::Value one = cgf.getBuilder().getConstInt(loc, indexTy, 1);
    mlir::Value upperBound =
        cgf.getBuilder().createAdd(loc, lowerBound, extent,
                                   cir::OverflowBehavior::None);
    return cgf.getBuilder().createSub(loc, upperBound, one,
                                      cir::OverflowBehavior::None);
  }

  if (section->getColonLocFirst().isInvalid())
    return lowerBound;

  if (const auto *arrayTy =
          cgf.getContext().getAsConstantArrayType(section->getBaseType()))
    return cgf.getBuilder().getConstInt(
        loc, indexTy, static_cast<int64_t>(arrayTy->getZExtSize() - 1));

  cgf.getCIRGenModule().errorNYI(
      section->getSourceRange(),
      "OpenMP reduction array section without explicit length");
  return {};
}

static mlir::Value emitOpenMPArraySectionPointer(CIRGenFunction &cgf,
                                                 const Expr *expr,
                                                 bool useUpperBounds) {
  llvm::SmallVector<const Expr *, 4> indexingExprs;
  const Expr *baseExpr = expr->IgnoreParenImpCasts();
  while (isa<ArraySectionExpr, ArraySubscriptExpr>(baseExpr)) {
    indexingExprs.push_back(baseExpr);
    if (const auto *section = dyn_cast<ArraySectionExpr>(baseExpr)) {
      baseExpr = section->getBase()->IgnoreParenImpCasts();
      continue;
    }
    baseExpr = cast<ArraySubscriptExpr>(baseExpr)->getBase()->IgnoreParenImpCasts();
  }

  mlir::Location loc = cgf.getLoc(expr->getSourceRange());
  QualType currentTy = baseExpr->getType().getNonReferenceType();
  mlir::Value currentPtr;
  if (currentTy->isPointerType()) {
    currentPtr = cgf.emitScalarExpr(baseExpr);
    currentTy = currentTy->getPointeeType();
  } else if (currentTy->isArrayType()) {
    Address baseAddr = cgf.emitLValue(baseExpr).getAddress();
    QualType elementTy = currentTy->castAsArrayTypeUnsafe()->getElementType();
    currentPtr = cgf.getBuilder().maybeBuildArrayDecay(
        loc, baseAddr.getPointer(), cgf.convertTypeForMem(elementTy));
    currentTy = elementTy;
  } else {
    cgf.getCIRGenModule().errorNYI(
        expr->getSourceRange(),
        "OpenMP reduction array section on unsupported base expression");
    return {};
  }

  for (const Expr *indexingExpr : llvm::reverse(indexingExprs)) {
    mlir::Value indexValue;
    if (const auto *section = dyn_cast<ArraySectionExpr>(indexingExpr)) {
      indexValue =
          getOpenMPArraySectionIndex(cgf, section, useUpperBounds);
    } else {
      indexValue =
          emitOpenMPIndexExpr(cgf, cast<ArraySubscriptExpr>(indexingExpr)->getIdx());
    }
    if (!indexValue)
      return {};
    if (indexValue.getType() != cgf.ptrDiffTy)
      indexValue = castOpenMPValueToCIRInt(
          cgf, loc, indexValue, mlir::cast<cir::IntType>(cgf.ptrDiffTy));

    mlir::Type cirCurrentTy = cgf.convertTypeForMem(currentTy);
    currentPtr = cgf.getBuilder().getArrayElement(
        loc, loc, currentPtr, cirCurrentTy, indexValue,
        /*shouldDecay=*/false);

    bool hasMore = indexingExpr != indexingExprs.front();
    if (!hasMore)
      continue;

    if (currentTy->isPointerType()) {
      CharUnits align = cgf.getContext().getTypeAlignInChars(currentTy);
      currentPtr = cgf.getBuilder().createLoad(
          loc, Address(currentPtr, cirCurrentTy, align));
      currentTy = currentTy->getPointeeType();
      continue;
    }

    if (currentTy->isArrayType()) {
      QualType elementTy = currentTy->castAsArrayTypeUnsafe()->getElementType();
      currentPtr = cgf.getBuilder().maybeBuildArrayDecay(
          loc, currentPtr, cgf.convertTypeForMem(elementTy));
      currentTy = elementTy;
      continue;
    }

    cgf.getCIRGenModule().errorNYI(
        expr->getSourceRange(),
        "OpenMP reduction nested array section on unsupported element type");
    return {};
  }

  if (currentTy->isArrayType()) {
    QualType elementTy = currentTy->castAsArrayTypeUnsafe()->getElementType();
    currentPtr = cgf.getBuilder().maybeBuildArrayDecay(
        loc, currentPtr, cgf.convertTypeForMem(elementTy));
    if (useUpperBounds) {
      mlir::Value elementCount =
          getOpenMPArrayElementCount(cgf, loc, currentTy);
      if (!elementCount)
        return {};
      if (elementCount.getType() != cgf.sizeTy)
        elementCount = castOpenMPValueToCIRInt(
            cgf, loc, elementCount, mlir::cast<cir::IntType>(cgf.sizeTy));
      mlir::Value one = cgf.getBuilder().getConstInt(
          loc, mlir::cast<cir::IntType>(cgf.sizeTy), 1);
      mlir::Value lastIndex = cgf.getBuilder().createSub(
          loc, elementCount, one, cir::OverflowBehavior::None);
      if (lastIndex.getType() != cgf.ptrDiffTy)
        lastIndex = castOpenMPValueToCIRInt(
            cgf, loc, lastIndex, mlir::cast<cir::IntType>(cgf.ptrDiffTy));
      currentPtr = cgf.getBuilder().getArrayElement(
          loc, loc, currentPtr, cgf.convertTypeForMem(elementTy), lastIndex,
          /*shouldDecay=*/false);
    }
  }

  return currentPtr;
}

static mlir::Value emitOpenMPArraySectionElementCount(CIRGenFunction &cgf,
                                                      const Expr *expr) {
  mlir::Location loc = cgf.getLoc(expr->getSourceRange());
  mlir::Value lowerBoundPtr =
      emitOpenMPArraySectionPointer(cgf, expr, /*useUpperBounds=*/false);
  if (!lowerBoundPtr)
    return {};
  mlir::Value upperBoundPtr =
      emitOpenMPArraySectionPointer(cgf, expr, /*useUpperBounds=*/true);
  if (!upperBoundPtr)
    return {};

  mlir::Value distance = cir::PtrDiffOp::create(
      cgf.getBuilder(), loc, cgf.ptrDiffTy, upperBoundPtr, lowerBoundPtr);
  if (distance.getType() != cgf.sizeTy)
    distance = castOpenMPValueToCIRInt(
        cgf, loc, distance, mlir::cast<cir::IntType>(cgf.sizeTy));
  mlir::Value one = cgf.getBuilder().getConstInt(
      loc, mlir::cast<cir::IntType>(cgf.sizeTy), 1);
  return cgf.getBuilder().createAdd(loc, distance, one,
                                    cir::OverflowBehavior::None);
}

static QualType getOpenMPArrayBaseElementType(ASTContext &context,
                                              QualType type) {
  type = type.getNonReferenceType().getUnqualifiedType();
  while (const auto *arrayTy = context.getAsArrayType(type))
    type = arrayTy->getElementType();
  return type;
}

static QualType getOpenMPDependElementType(ASTContext &context,
                                           const Expr *expr) {
  llvm::SmallVector<const Expr *, 4> indexingExprs;
  const Expr *baseExpr = expr->IgnoreParenImpCasts();
  while (isa<ArraySectionExpr, ArraySubscriptExpr>(baseExpr)) {
    indexingExprs.push_back(baseExpr);
    if (const auto *section = dyn_cast<ArraySectionExpr>(baseExpr)) {
      baseExpr = section->getBase()->IgnoreParenImpCasts();
      continue;
    }
    baseExpr = cast<ArraySubscriptExpr>(baseExpr)->getBase()->IgnoreParenImpCasts();
  }

  std::reverse(indexingExprs.begin(), indexingExprs.end());
  if (indexingExprs.empty())
    return {};

  QualType currentType =
      baseExpr->getType().getNonReferenceType().getUnqualifiedType();
  if (currentType->isPointerType()) {
    currentType = currentType->getPointeeType();
  } else if (const auto *arrayTy = context.getAsArrayType(currentType)) {
    currentType = arrayTy->getElementType();
  } else {
    return {};
  }

  for (size_t i = 1; i < indexingExprs.size(); ++i) {
    currentType = currentType.getNonReferenceType().getUnqualifiedType();
    if (currentType->isPointerType()) {
      currentType = currentType->getPointeeType();
      continue;
    }
    if (const auto *arrayTy = context.getAsArrayType(currentType)) {
      currentType = arrayTy->getElementType();
      continue;
    }
    return {};
  }

  return getOpenMPArrayBaseElementType(context, currentType);
}

static mlir::Value emitOpenMPDependTypeSize(CIRGenFunction &cgf, QualType type,
                                            mlir::Location loc) {
  type = type.getNonReferenceType().getUnqualifiedType();
  if (!type->isArrayType())
    return createOpenMPI64Constant(
        cgf, loc, cgf.getContext().getTypeSizeInChars(type).getQuantity());

  QualType elementType = getOpenMPArrayBaseElementType(cgf.getContext(), type);
  mlir::Value elementCount = getOpenMPArrayElementCount(cgf, loc, type);
  if (!elementCount)
    return {};

  mlir::Value elementCountI64 = castOpenMPValueToI64(cgf, loc, elementCount);
  if (!elementCountI64)
    return {};

  mlir::Value elementSize = createOpenMPI64Constant(
      cgf, loc, cgf.getContext().getTypeSizeInChars(elementType).getQuantity());
  return mlir::LLVM::MulOp::create(cgf.getBuilder(), loc, elementCountI64,
                                   elementSize);
}

static mlir::Value emitOpenMPDependVarSize(CIRGenFunction &cgf,
                                           const Expr *varExpr) {
  mlir::Location loc = cgf.getLoc(varExpr->getSourceRange());
  const Expr *expr = varExpr->IgnoreParenImpCasts();
  if (!isa<ArraySectionExpr, ArraySubscriptExpr>(expr))
    return emitOpenMPDependTypeSize(cgf, varExpr->getType(), loc);

  QualType elementType = getOpenMPDependElementType(cgf.getContext(), expr);
  if (elementType.isNull()) {
    cgf.getCIRGenModule().errorNYI(
        expr->getSourceRange(), "OpenMP depend array expression element type");
    return {};
  }

  mlir::Value elementCount = emitOpenMPArraySectionElementCount(cgf, expr);
  if (!elementCount)
    return {};

  mlir::Value elementCountI64 = castOpenMPValueToI64(cgf, loc, elementCount);
  if (!elementCountI64)
    return {};

  mlir::Value elementSize = createOpenMPI64Constant(
      cgf, loc, cgf.getContext().getTypeSizeInChars(elementType).getQuantity());
  return mlir::LLVM::MulOp::create(cgf.getBuilder(), loc, elementCountI64,
                                   elementSize);
}

static std::optional<OpenMPDependPointerAndSize>
emitOpenMPDependVarPointerAndSize(CIRGenFunction &cgf, const Expr *varExpr) {
  const Expr *expr = varExpr->IgnoreParenImpCasts();
  mlir::Value dependPtr;
  if (isa<ArraySectionExpr, ArraySubscriptExpr>(expr)) {
    dependPtr = emitOpenMPArraySectionPointer(cgf, varExpr,
                                              /*useUpperBounds=*/false);
  } else {
    dependPtr = cgf.emitLValue(varExpr).getPointer();
  }
  if (!dependPtr)
    return std::nullopt;

  mlir::Value dependSize = emitOpenMPDependVarSize(cgf, varExpr);
  if (!dependSize)
    return std::nullopt;

  return OpenMPDependPointerAndSize{dependPtr, dependSize};
}

/// Element offset of an array section's start from its base variable, or null
/// when the section does not lie inside storage the base variable addresses
/// (e.g. `a[i][0:n]` on an array of pointers).
static mlir::Value emitOpenMPReductionSectionOffset(CIRGenFunction &cgf,
                                                    const Expr *varExpr,
                                                    mlir::Value sectionPtr) {
  const Expr *base = varExpr->IgnoreParenImpCasts();
  unsigned levels = 0;
  bool contiguous = true;
  while (isa<ArraySectionExpr, ArraySubscriptExpr>(base)) {
    base =
        isa<ArraySectionExpr>(base)
            ? cast<ArraySectionExpr>(base)->getBase()->IgnoreParenImpCasts()
            : cast<ArraySubscriptExpr>(base)->getBase()->IgnoreParenImpCasts();
    ++levels;
    contiguous &= base->getType()->isArrayType();
  }
  const auto *dre = dyn_cast<DeclRefExpr>(base);
  QualType baseTy = base->getType().getNonReferenceType();
  if (!dre || levels == 0)
    return {};
  mlir::Value basePtr;
  if (baseTy->isConstantArrayType() && contiguous)
    basePtr = cgf.emitLValue(dre).getPointer();
  else if (baseTy->isPointerType() && levels == 1)
    basePtr = cgf.emitScalarExpr(dre);
  else
    return {};
  mlir::Location loc = cgf.getLoc(varExpr->getSourceRange());
  basePtr = cgf.getBuilder().createBitcast(loc, basePtr, sectionPtr.getType());
  return cir::PtrDiffOp::create(cgf.getBuilder(), loc, cgf.ptrDiffTy,
                                sectionPtr, basePtr);
}

static mlir::Value emitOpenMPReductionVarPointer(CIRGenFunction &cgf,
                                                 const Expr *varExpr) {
  if (isa<ArraySectionExpr>(varExpr->IgnoreParenImpCasts()))
    return emitOpenMPArraySectionPointer(cgf, varExpr,
                                         /*useUpperBounds=*/false);
  return cgf.emitLValue(varExpr).getPointer();
}

static std::optional<OpenMPReductionKind>
getOpenMPReductionKind(DeclarationName reductionName) {
  if (reductionName.getNameKind() == DeclarationName::CXXOperatorName) {
    switch (reductionName.getCXXOverloadedOperator()) {
    case OO_Plus:
      return OpenMPReductionKind::Add;
    case OO_Minus:
      return OpenMPReductionKind::Sub;
    case OO_Star:
      return OpenMPReductionKind::Mul;
    case OO_Amp:
      return OpenMPReductionKind::And;
    case OO_Pipe:
      return OpenMPReductionKind::Or;
    case OO_Caret:
      return OpenMPReductionKind::Xor;
    case OO_AmpAmp:
      return OpenMPReductionKind::LAnd;
    case OO_PipePipe:
      return OpenMPReductionKind::LOr;
    default:
      return std::nullopt;
    }
  }

  if (const IdentifierInfo *ident = reductionName.getAsIdentifierInfo()) {
    if (ident->isStr("min"))
      return OpenMPReductionKind::Min;
    if (ident->isStr("max"))
      return OpenMPReductionKind::Max;
  }

  return std::nullopt;
}

static std::optional<OpenMPReductionKind>
getOpenMPReductionKind(const OMPReductionClause *clause) {
  return getOpenMPReductionKind(clause->getNameInfo().getName());
}

static std::optional<OpenMPReductionKind>
getOpenMPReductionKind(const OMPTaskReductionClause *clause) {
  return getOpenMPReductionKind(clause->getNameInfo().getName());
}

static std::optional<OpenMPReductionKind>
getOpenMPReductionKind(const OMPInReductionClause *clause) {
  return getOpenMPReductionKind(clause->getNameInfo().getName());
}

static StringRef getOpenMPReductionOperatorName(OpenMPReductionKind op) {
  switch (op) {
  case OpenMPReductionKind::Add:
    return "add";
  case OpenMPReductionKind::Sub:
    return "sub";
  case OpenMPReductionKind::Mul:
    return "mul";
  case OpenMPReductionKind::And:
    return "and";
  case OpenMPReductionKind::Or:
    return "or";
  case OpenMPReductionKind::Xor:
    return "xor";
  case OpenMPReductionKind::LAnd:
    return "land";
  case OpenMPReductionKind::LOr:
    return "lor";
  case OpenMPReductionKind::Min:
    return "min";
  case OpenMPReductionKind::Max:
    return "max";
  }
  llvm_unreachable("unexpected OpenMP reduction operator");
}

static std::optional<mlir::omp::ClauseTaskDepend>
getOpenMPTaskDependKind(OpenMPDependClauseKind kind) {
  switch (kind) {
  case OMPC_DEPEND_in:
    return mlir::omp::ClauseTaskDepend::taskdependin;
  case OMPC_DEPEND_out:
    return mlir::omp::ClauseTaskDepend::taskdependout;
  case OMPC_DEPEND_inout:
    return mlir::omp::ClauseTaskDepend::taskdependinout;
  case OMPC_DEPEND_mutexinoutset:
    return mlir::omp::ClauseTaskDepend::taskdependmutexinoutset;
  case OMPC_DEPEND_inoutset:
    return mlir::omp::ClauseTaskDepend::taskdependinoutset;
  case OMPC_DEPEND_depobj:
  case OMPC_DEPEND_source:
  case OMPC_DEPEND_sink:
  case OMPC_DEPEND_outallmemory:
  case OMPC_DEPEND_inoutallmemory:
  case OMPC_DEPEND_unknown:
    return std::nullopt;
  }

  llvm_unreachable("unexpected OpenMP depend kind");
}

static std::string getOpenMPReductionTypeName(mlir::Type ty) {
  if (auto intTy = mlir::dyn_cast<cir::IntType>(ty))
    return (intTy.isSigned() ? "s" : "u") + llvm::utostr(intTy.getWidth());
  if (mlir::isa<cir::BoolType>(ty))
    return "bool";
  if (auto fpTy = mlir::dyn_cast<cir::FPTypeInterface>(ty))
    return "f" + llvm::utostr(fpTy.getWidth());

  std::string printedTy;
  llvm::raw_string_ostream os(printedTy);
  ty.print(os);
  os.flush();

  std::string sanitized;
  sanitized.reserve(printedTy.size());
  bool lastWasUnderscore = false;
  for (char c : printedTy) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      sanitized.push_back(c);
      lastWasUnderscore = false;
      continue;
    }
    if (!lastWasUnderscore) {
      sanitized.push_back('_');
      lastWasUnderscore = true;
    }
  }

  while (!sanitized.empty() && sanitized.back() == '_')
    sanitized.pop_back();
  return sanitized.empty() ? "unknown" : sanitized;
}

/// Check if the clause combiner expression references a user-defined reduction
/// declaration and return that declaration when present.
static const OMPDeclareReductionDecl *
getOpenMPDeclareReductionDecl(const Expr *reductionOp) {
  if (const auto *call = dyn_cast<CallExpr>(reductionOp))
    if (const auto *callee = dyn_cast<OpaqueValueExpr>(call->getCallee()))
      if (const auto *dre = dyn_cast<DeclRefExpr>(
              callee->getSourceExpr()->IgnoreImpCasts()))
        return dyn_cast<OMPDeclareReductionDecl>(dre->getDecl());
  return nullptr;
}

static std::string sanitizeOpenMPSymbolComponent(StringRef str) {
  std::string sanitized;
  sanitized.reserve(str.size());
  bool lastWasUnderscore = false;
  for (char c : str) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      sanitized.push_back(c);
      lastWasUnderscore = false;
      continue;
    }
    if (!lastWasUnderscore) {
      sanitized.push_back('_');
      lastWasUnderscore = true;
    }
  }

  while (!sanitized.empty() && sanitized.back() == '_')
    sanitized.pop_back();
  return sanitized.empty() ? "anon" : sanitized;
}

static std::string
getOpenMPUserDefinedReductionName(CIRGenFunction &cgf,
                                  const OMPDeclareReductionDecl &decl,
                                  mlir::Type reductionTy, bool byRef) {
  std::string base =
      "__omp_decl_reduction_" +
      sanitizeOpenMPSymbolComponent(decl.getDeclName().getAsString()) + "_" +
      (byRef ? "byref_" : "") + getOpenMPReductionTypeName(reductionTy);

  const SourceManager &sm = cgf.getContext().getSourceManager();
  PresumedLoc ploc = sm.getPresumedLoc(decl.getLocation());
  if (ploc.isValid())
    base += ("_" + llvm::Twine(ploc.getLine()) + "_" +
             llvm::Twine(ploc.getColumn()))
                .str();
  return base;
}

static bool
isOpenMPTaskReductionRuntimeDescriptorClause(const OMPClause *clause) {
  if (isa<OMPTaskReductionClause, OMPInReductionClause>(clause))
    return true;
  if (const auto *reductionClause = dyn_cast<OMPReductionClause>(clause))
    return reductionClause->getModifier() == OMPC_REDUCTION_task;
  return false;
}

static std::string
getOpenMPTaskReductionSiteSuffix(CIRGenFunction &cgf, const Expr *varExpr) {
  const VarDecl *vd = getOpenMPReductionBaseVarDecl(varExpr);
  assert(vd && "expected OpenMP reduction base variable");
  std::string suffix =
      sanitizeOpenMPSymbolComponent(vd->getName().empty() ? "anon"
                                                          : vd->getName());

  const SourceManager &sm = cgf.getContext().getSourceManager();
  PresumedLoc ploc = sm.getPresumedLoc(vd->getLocation());
  if (ploc.isValid())
    suffix += ("_" + llvm::Twine(ploc.getLine()) + "_" +
               llvm::Twine(ploc.getColumn()))
                  .str();
  return suffix;
}

static std::string getOpenMPReductionExprSiteSuffix(CIRGenFunction &cgf,
                                                    const Expr *varExpr) {
  const VarDecl *vd = getOpenMPReductionBaseVarDecl(varExpr);
  assert(vd && "expected OpenMP reduction base variable");
  std::string suffix =
      sanitizeOpenMPSymbolComponent(vd->getName().empty() ? "anon"
                                                          : vd->getName());

  const SourceManager &sm = cgf.getContext().getSourceManager();
  PresumedLoc ploc = sm.getPresumedLoc(varExpr->getExprLoc());
  if (!ploc.isValid())
    ploc = sm.getPresumedLoc(vd->getLocation());
  if (ploc.isValid())
    suffix += ("_" + llvm::Twine(ploc.getLine()) + "_" +
               llvm::Twine(ploc.getColumn()))
                  .str();
  return suffix;
}

static cir::GlobalOp
getOrCreateOpenMPTaskReductionSizeGlobal(CIRGenFunction &cgf,
                                         mlir::Location loc,
                                         const Expr *varExpr) {
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::ModuleOp module = cgm.getModule();
  std::string name = "__omp_task_reduction_size_" +
                     getOpenMPTaskReductionSiteSuffix(cgf, varExpr);
  if (auto existing = module.lookupSymbol<cir::GlobalOp>(name))
    return existing;

  cir::GlobalOp global =
      CIRGenModule::createGlobalOp(cgm, loc, name, cgf.sizeTy);
  global.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
  CIRGenModule::setInitializer(global, cir::IntAttr::get(cgf.sizeTy, 0));
  return global;
}

static mlir::FlatSymbolRefAttr
getOpenMPTaskReductionSizeSym(mlir::omp::DeclareReductionOp decl) {
  return decl->getAttrOfType<mlir::FlatSymbolRefAttr>(
      kOpenMPTaskReductionSizeSymAttr);
}

// The op may still sit in a region under construction, which does not reach
// the module's symbol table yet.
static mlir::FlatSymbolRefAttr
lookupOpenMPTaskReductionSizeSym(CIRGenFunction &cgf,
                                 mlir::SymbolRefAttr reductionSym) {
  auto reductionDecl =
      cgf.getCIRGenModule()
          .getModule()
          .lookupSymbol<mlir::omp::DeclareReductionOp>(reductionSym);
  if (!reductionDecl)
    return {};
  return getOpenMPTaskReductionSizeSym(reductionDecl);
}

static Address
getOpenMPTaskReductionSizeAddr(CIRGenFunction &cgf, mlir::Location loc,
                               mlir::FlatSymbolRefAttr sizeSym) {
  auto sizePtrTy = cgf.getBuilder().getPointerTo(cgf.sizeTy);
  mlir::Value sizeGlobal = cir::GetGlobalOp::create(
      cgf.getBuilder(), loc, sizePtrTy, sizeSym.getLeafReference());
  mlir::Value threadprivateAddr = mlir::omp::ThreadprivateOp::create(
      cgf.getBuilder(), loc, sizePtrTy, sizeGlobal);
  CharUnits sizeAlign =
      cgf.getContext().getTypeAlignInChars(cgf.getContext().getSizeType());
  return Address(threadprivateAddr, cgf.sizeTy, sizeAlign);
}

static mlir::Value
emitOpenMPTaskReductionSizeLoad(CIRGenFunction &cgf, mlir::Location loc,
                                mlir::FlatSymbolRefAttr sizeSym) {
  return cgf.getBuilder().createLoad(
      loc, getOpenMPTaskReductionSizeAddr(cgf, loc, sizeSym));
}

static void withOpenMPTaskReductionVLASize(CIRGenFunction &cgf,
                                           mlir::Location loc,
                                           QualType reductionTy,
                                           mlir::FlatSymbolRefAttr sizeSym,
                                           llvm::function_ref<void()> body) {
  if (!reductionTy->isVariablyModifiedType()) {
    body();
    return;
  }

  const auto *vlaTy = cgf.getContext().getAsVariableArrayType(reductionTy);
  assert(vlaTy && "expected variably modified OpenMP reduction type");
  mlir::Value runtimeSize = emitOpenMPTaskReductionSizeLoad(cgf, loc, sizeSym);
  CIRGenFunction::VLASizeMapping sizeScope(cgf, vlaTy->getSizeExpr(),
                                           runtimeSize);
  cgf.emitVariablyModifiedType(reductionTy);
  body();
}

template <typename OpTy>
static void emitOpenMPReductionRuntimeSizeStores(
    CIRGenFunction &cgf, OpTy &op,
    const CIRGenFunction::OpenMPClauseEmissionInfo &emissionInfo) {
  if constexpr (std::is_same_v<OpTy, mlir::omp::ParallelOp> ||
                std::is_same_v<OpTy, mlir::omp::TaskloopOp> ||
                std::is_same_v<OpTy, mlir::omp::TeamsOp> ||
                std::is_same_v<OpTy, mlir::omp::SimdOp> ||
                std::is_same_v<OpTy, mlir::omp::WsloopOp> ||
                std::is_same_v<OpTy, mlir::omp::SectionsOp>) {
    auto reductionSyms = op.getReductionSyms();
    if (!reductionSyms)
      return;

    for (const auto &info : emissionInfo.reductionRuntimeSizes) {
      if (!info.runtimeElementCount || info.index >= reductionSyms->size())
        continue;

      auto reductionSym =
          mlir::dyn_cast<mlir::SymbolRefAttr>((*reductionSyms)[info.index]);
      if (!reductionSym)
        continue;
      mlir::FlatSymbolRefAttr sizeSym =
          lookupOpenMPTaskReductionSizeSym(cgf, reductionSym);
      if (!sizeSym)
        continue;

      mlir::Location loc =
          info.privateVar ? cgf.getLoc(info.privateVar->getLocation())
                          : op->getLoc();
      mlir::Value runtimeSize = info.runtimeElementCount;
      if (runtimeSize.getType() != cgf.sizeTy)
        runtimeSize = cgf.getBuilder().createIntCast(runtimeSize, cgf.sizeTy);
      cgf.getBuilder().createStore(
          loc, runtimeSize, getOpenMPTaskReductionSizeAddr(cgf, loc, sizeSym));
    }
  }
}

static bool isOpenMPScalarReductionType(mlir::Type ty) {
  return mlir::isa<cir::BoolType, cir::IntType, cir::FPTypeInterface>(ty);
}

/// A scalar reduction can only use an in-place atomic combiner (an
/// `cir.atomic.fetch` region on the `omp.declare_reduction`) when the type has a
/// native LLVM `atomicrmw`. This mirrors the types accepted by
/// `CIRToLLVMAtomicFetchOpLowering`: integers plus the small floats/`float`/
/// `double`. Types without a native `atomicrmw` (e.g. `_Bool`, `long double`
/// (x86 fp80), `_Complex`) must omit the atomic region so the OpenMP runtime
/// falls back to a critical-section combine, matching classic CodeGen.
static bool isOpenMPAtomicReductionType(mlir::Type ty) {
  return mlir::isa<cir::IntType, cir::FP16Type, cir::BF16Type, cir::SingleType,
                   cir::DoubleType>(ty);
}

static mlir::Value getOpenMPReductionIdentity(CIRGenFunction &cgf,
                                              mlir::Location loc,
                                              mlir::Type ty,
                                              OpenMPReductionKind op) {
  auto &builder = cgf.getBuilder();

  if (mlir::isa<cir::BoolType>(ty)) {
    switch (op) {
    case OpenMPReductionKind::And:
    case OpenMPReductionKind::LAnd:
    case OpenMPReductionKind::Mul:
      return builder.getTrue(loc);
    case OpenMPReductionKind::Add:
    case OpenMPReductionKind::Sub:
    case OpenMPReductionKind::Or:
    case OpenMPReductionKind::LOr:
    case OpenMPReductionKind::Xor:
      return builder.getFalse(loc);
    default:
      break;
    }
  }

  if (auto intTy = mlir::dyn_cast<cir::IntType>(ty)) {
    unsigned width = intTy.getWidth();
    switch (op) {
    case OpenMPReductionKind::Add:
    case OpenMPReductionKind::Sub:
    case OpenMPReductionKind::Or:
    case OpenMPReductionKind::Xor:
    case OpenMPReductionKind::LOr:
      return builder.getConstInt(loc, ty, 0);
    case OpenMPReductionKind::Mul:
    case OpenMPReductionKind::LAnd:
      return builder.getConstInt(loc, ty, 1);
    case OpenMPReductionKind::And:
      return builder.getConstAPInt(loc, ty, llvm::APInt::getAllOnes(width));
    case OpenMPReductionKind::Min:
      return builder.getConstInt(
          loc,
          intTy.isSigned() ? llvm::APInt::getSignedMaxValue(width)
                           : llvm::APInt::getMaxValue(width),
          /*isUnsigned=*/!intTy.isSigned());
    case OpenMPReductionKind::Max:
      return builder.getConstInt(
          loc,
          intTy.isSigned() ? llvm::APInt::getSignedMinValue(width)
                           : llvm::APInt::getZero(width),
          /*isUnsigned=*/!intTy.isSigned());
    }
  }

  if (mlir::isa<cir::FPTypeInterface>(ty)) {
    const llvm::fltSemantics &semantics =
        mlir::cast<cir::FPTypeInterface>(ty).getFloatSemantics();
    switch (op) {
    case OpenMPReductionKind::Add:
    case OpenMPReductionKind::Sub:
    case OpenMPReductionKind::LOr:
      return builder.getConstFP(loc, ty, llvm::APFloat::getZero(semantics));
    case OpenMPReductionKind::Mul:
    case OpenMPReductionKind::LAnd:
      return builder.getConstFP(loc, ty, llvm::APFloat::getOne(semantics));
    case OpenMPReductionKind::Min:
      return builder.getConstFP(loc, ty, llvm::APFloat::getLargest(semantics));
    case OpenMPReductionKind::Max:
      return builder.getConstFP(loc, ty,
                                llvm::APFloat::getLargest(semantics, true));
    default:
      break;
    }
  }

  cgf.getCIRGenModule().errorNYI(
      SourceRange(), "OpenMP reduction identity for unsupported type/operator");
  return {};
}

static std::optional<cir::AtomicFetchKind>
getOpenMPReductionAtomicKind(OpenMPReductionKind op) {
  switch (op) {
  case OpenMPReductionKind::Add:
    return cir::AtomicFetchKind::Add;
  case OpenMPReductionKind::Sub:
    return cir::AtomicFetchKind::Sub;
  case OpenMPReductionKind::And:
    return cir::AtomicFetchKind::And;
  case OpenMPReductionKind::Or:
    return cir::AtomicFetchKind::Or;
  case OpenMPReductionKind::Xor:
    return cir::AtomicFetchKind::Xor;
  case OpenMPReductionKind::Min:
    return cir::AtomicFetchKind::Min;
  case OpenMPReductionKind::Max:
    return cir::AtomicFetchKind::Max;
  default:
    return std::nullopt;
  }
}

static mlir::omp::ReductionModifierAttr
getOpenMPReductionModifierAttr(CIRGenFunction &cgf, const OMPClause *clause,
                               OpenMPReductionClauseModifier modifier) {
  switch (modifier) {
  case OMPC_REDUCTION_unknown:
  case OMPC_REDUCTION_default:
    return {};
  case OMPC_REDUCTION_inscan:
    return mlir::omp::ReductionModifierAttr::get(
        &cgf.getMLIRContext(), mlir::omp::ReductionModifier::inscan);
  case OMPC_REDUCTION_task:
    return mlir::omp::ReductionModifierAttr::get(
        &cgf.getMLIRContext(), mlir::omp::ReductionModifier::task);
  }

  llvm_unreachable("unexpected OpenMP reduction modifier");
}

static mlir::Value emitOpenMPReductionCombiner(CIRGenFunction &cgf,
                                               CIRGenBuilderTy &builder,
                                               mlir::Location loc,
                                               mlir::Type reductionTy,
                                               const Expr *lhsExpr,
                                               const Expr *rhsExpr,
                                               const Expr *reductionOp) {
  const VarDecl *lhsVD = getOpenMPClauseVarDecl(lhsExpr);
  const VarDecl *rhsVD = getOpenMPClauseVarDecl(rhsExpr);
  CharUnits lhsAlign = cgf.getContext().getDeclAlign(lhsVD);
  CharUnits rhsAlign = cgf.getContext().getDeclAlign(rhsVD);

  mlir::Value lhsAlloca = builder.createAlloca(
      loc, builder.getPointerTo(reductionTy), reductionTy, lhsVD->getName(),
      lhsAlign);
  mlir::Value rhsAlloca = builder.createAlloca(
      loc, builder.getPointerTo(reductionTy), reductionTy, rhsVD->getName(),
      rhsAlign);
  Address lhsAddr(lhsAlloca, reductionTy, lhsAlign);
  Address rhsAddr(rhsAlloca, reductionTy, rhsAlign);
  mlir::Block *block = builder.getInsertionBlock();
  assert(block && block->getNumArguments() >= 2 &&
         "expected declare_reduction combiner block arguments");
  builder.createStore(loc, block->getArgument(0), lhsAddr);
  builder.createStore(loc, block->getArgument(1), rhsAddr);

  CIRGenFunction::DeclMapRevertingRAII lhsGuard(cgf, lhsVD);
  CIRGenFunction::DeclMapRevertingRAII rhsGuard(cgf, rhsVD);
  cgf.setAddrOfLocalVar(lhsVD, lhsAddr);
  cgf.setAddrOfLocalVar(rhsVD, rhsAddr);
  // The user reduction op emitted here is OpenMP-internal codegen at the region
  // boundary, not a user throw escaping the region; suppress the gate.
  CIRGenFunction::OpenMPRegionDataSharingCopyRAII combinerGuard(cgf);
  cgf.emitIgnoredExpr(reductionOp);
  return builder.createLoad(loc, lhsAddr);
}

static void emitOpenMPReductionArrayIdentityInit(CIRGenFunction &cgf,
                                                 Address destAddr,
                                                 QualType arrayTy,
                                                 mlir::Value identity,
                                                 mlir::Location loc) {
  const auto *typedArrayTy = arrayTy->castAsArrayTypeUnsafe();
  QualType elementType;
  mlir::Value numElements = cgf.emitArrayLength(typedArrayTy, elementType, destAddr);
  mlir::Type cirElementTy = cgf.convertTypeForMem(elementType);
  destAddr = destAddr.withElementType(cgf.getBuilder(), cirElementTy);

  mlir::Value destBegin =
      cgf.getBuilder().createPtrBitcast(destAddr.getPointer(), cirElementTy);
  mlir::Value destEnd =
      cgf.getBuilder().createPtrStride(loc, destBegin, numElements);
  mlir::Value isNotEmpty = cgf.getBuilder().createCompare(
      loc, cir::CmpOpKind::ne, destBegin, destEnd);

  CharUnits elementSize = cgf.getContext().getTypeSizeInChars(elementType);
  CharUnits elementAlign =
      destAddr.getAlignment().alignmentOfArrayElement(elementSize);

  cir::IfOp::create(
      cgf.getBuilder(), loc, isNotEmpty, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
        mlir::OpBuilder::InsertionGuard guard(cgf.getBuilder());
        cgf.getBuilder().setInsertionPointToEnd(thenBuilder.getInsertionBlock());

        auto ptrToElementTy = cgf.getBuilder().getPointerTo(cirElementTy);
        auto ptrToPtrTy = cgf.getBuilder().getPointerTo(ptrToElementTy);
        auto ptrAlignAttr =
            cgf.getBuilder().getAlignmentAttr(cgf.getPointerAlign().getQuantity());
        mlir::Value destCurSlot = cgf.getBuilder().createAlloca(
            thenLoc, ptrToPtrTy, ptrToElementTy, "__omp_reduction_dest",
            ptrAlignAttr);
        Address destCurAddr(destCurSlot, ptrToElementTy, cgf.getPointerAlign());
        cgf.getBuilder().createStore(thenLoc, destBegin, destCurAddr);

        cgf.getBuilder().createDoWhile(
            thenLoc,
            /*condBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location loopLoc) {
              mlir::Value curDest = cgf.getBuilder().createLoad(loopLoc, destCurAddr);
              mlir::Value cmp = cgf.getBuilder().createCompare(
                  loopLoc, cir::CmpOpKind::ne, curDest, destEnd);
              cgf.getBuilder().createCondition(cmp);
            },
            /*bodyBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location loopLoc) {
              mlir::Value curDest = cgf.getBuilder().createLoad(loopLoc, destCurAddr);
              Address destElementAddr(curDest, cirElementTy, elementAlign);
              cgf.getBuilder().createStore(loopLoc, identity, destElementAddr);

              mlir::Value one = cgf.getBuilder().getConstInt(
                  loopLoc, mlir::cast<cir::IntType>(cgf.ptrDiffTy), 1);
              mlir::Value nextDest =
                  cgf.getBuilder().createPtrStride(loopLoc, curDest, one);
              cgf.getBuilder().createStore(loopLoc, nextDest, destCurAddr);
              cgf.getBuilder().createYield(loopLoc);
            });

        cir::YieldOp::create(thenBuilder, thenLoc);
      });
}

static void emitOpenMPAggregateReductionInit(CIRGenFunction &cgf,
                                             Address destAddr, QualType arrayTy,
                                             const Expr *initExpr,
                                             mlir::Location loc) {
  const auto *typedArrayTy = arrayTy->castAsArrayTypeUnsafe();
  QualType elementType;
  mlir::Value numElements = cgf.emitArrayLength(typedArrayTy, elementType, destAddr);
  mlir::Type cirElementTy = cgf.convertTypeForMem(elementType);
  destAddr = destAddr.withElementType(cgf.getBuilder(), cirElementTy);

  mlir::Value destBegin =
      cgf.getBuilder().createPtrBitcast(destAddr.getPointer(), cirElementTy);
  mlir::Value destEnd =
      cgf.getBuilder().createPtrStride(loc, destBegin, numElements);
  mlir::Value isNotEmpty = cgf.getBuilder().createCompare(
      loc, cir::CmpOpKind::ne, destBegin, destEnd);

  CharUnits elementSize = cgf.getContext().getTypeSizeInChars(elementType);
  CharUnits elementAlign =
      destAddr.getAlignment().alignmentOfArrayElement(elementSize);

  cir::IfOp::create(
      cgf.getBuilder(), loc, isNotEmpty, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
        mlir::OpBuilder::InsertionGuard guard(cgf.getBuilder());
        cgf.getBuilder().setInsertionPointToEnd(thenBuilder.getInsertionBlock());

        auto ptrToElementTy = cgf.getBuilder().getPointerTo(cirElementTy);
        auto ptrToPtrTy = cgf.getBuilder().getPointerTo(ptrToElementTy);
        auto ptrAlignAttr =
            cgf.getBuilder().getAlignmentAttr(cgf.getPointerAlign().getQuantity());
        mlir::Value destCurSlot = cgf.getBuilder().createAlloca(
            thenLoc, ptrToPtrTy, ptrToElementTy, "__omp_reduction_dest",
            ptrAlignAttr);
        Address destCurAddr(destCurSlot, ptrToElementTy, cgf.getPointerAlign());
        cgf.getBuilder().createStore(thenLoc, destBegin, destCurAddr);

        cgf.getBuilder().createDoWhile(
            thenLoc,
            /*condBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location loopLoc) {
              mlir::Value curDest = cgf.getBuilder().createLoad(loopLoc, destCurAddr);
              mlir::Value cmp = cgf.getBuilder().createCompare(
                  loopLoc, cir::CmpOpKind::ne, curDest, destEnd);
              cgf.getBuilder().createCondition(cmp);
            },
            /*bodyBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location loopLoc) {
              mlir::Value curDest = cgf.getBuilder().createLoad(loopLoc, destCurAddr);
              Address destElementAddr(curDest, cirElementTy, elementAlign);
              cgf.emitAnyExprToMem(initExpr, destElementAddr,
                                   elementType.getQualifiers(),
                                   /*isInit=*/false);

              mlir::Value one = cgf.getBuilder().getConstInt(
                  loopLoc, mlir::cast<cir::IntType>(cgf.ptrDiffTy), 1);
              mlir::Value nextDest =
                  cgf.getBuilder().createPtrStride(loopLoc, curDest, one);
              cgf.getBuilder().createStore(loopLoc, nextDest, destCurAddr);
              cgf.getBuilder().createYield(loopLoc);
            });

        cir::YieldOp::create(thenBuilder, thenLoc);
      });
}

static void emitOpenMPAggregateReductionCombiner(CIRGenFunction &cgf,
                                                 Address lhsAddr,
                                                 Address rhsAddr,
                                                 QualType arrayTy,
                                                 const VarDecl *lhsVD,
                                                 const VarDecl *rhsVD,
                                                 const Expr *reductionOp,
                                                 mlir::Location loc) {
  const auto *typedArrayTy = arrayTy->castAsArrayTypeUnsafe();
  QualType elementType;
  mlir::Value numElements = cgf.emitArrayLength(typedArrayTy, elementType, lhsAddr);
  mlir::Type cirElementTy = cgf.convertTypeForMem(elementType);
  lhsAddr = lhsAddr.withElementType(cgf.getBuilder(), cirElementTy);
  rhsAddr = rhsAddr.withElementType(cgf.getBuilder(), cirElementTy);

  mlir::Value lhsBegin =
      cgf.getBuilder().createPtrBitcast(lhsAddr.getPointer(), cirElementTy);
  mlir::Value rhsBegin =
      cgf.getBuilder().createPtrBitcast(rhsAddr.getPointer(), cirElementTy);
  mlir::Value lhsEnd =
      cgf.getBuilder().createPtrStride(loc, lhsBegin, numElements);
  mlir::Value isNotEmpty = cgf.getBuilder().createCompare(
      loc, cir::CmpOpKind::ne, lhsBegin, lhsEnd);

  CharUnits elementSize = cgf.getContext().getTypeSizeInChars(elementType);
  CharUnits lhsElementAlign =
      lhsAddr.getAlignment().alignmentOfArrayElement(elementSize);
  CharUnits rhsElementAlign =
      rhsAddr.getAlignment().alignmentOfArrayElement(elementSize);

  cir::IfOp::create(
      cgf.getBuilder(), loc, isNotEmpty, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
        mlir::OpBuilder::InsertionGuard guard(cgf.getBuilder());
        cgf.getBuilder().setInsertionPointToEnd(thenBuilder.getInsertionBlock());

        auto ptrToElementTy = cgf.getBuilder().getPointerTo(cirElementTy);
        auto ptrToPtrTy = cgf.getBuilder().getPointerTo(ptrToElementTy);
        auto ptrAlignAttr =
            cgf.getBuilder().getAlignmentAttr(cgf.getPointerAlign().getQuantity());
        mlir::Value lhsCurSlot = cgf.getBuilder().createAlloca(
            thenLoc, ptrToPtrTy, ptrToElementTy, "__omp_reduction_lhs",
            ptrAlignAttr);
        mlir::Value rhsCurSlot = cgf.getBuilder().createAlloca(
            thenLoc, ptrToPtrTy, ptrToElementTy, "__omp_reduction_rhs",
            ptrAlignAttr);
        Address lhsCurAddr(lhsCurSlot, ptrToElementTy, cgf.getPointerAlign());
        Address rhsCurAddr(rhsCurSlot, ptrToElementTy, cgf.getPointerAlign());
        cgf.getBuilder().createStore(thenLoc, lhsBegin, lhsCurAddr);
        cgf.getBuilder().createStore(thenLoc, rhsBegin, rhsCurAddr);

        cgf.getBuilder().createDoWhile(
            thenLoc,
            /*condBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location loopLoc) {
              mlir::Value curLhs = cgf.getBuilder().createLoad(loopLoc, lhsCurAddr);
              mlir::Value cmp = cgf.getBuilder().createCompare(
                  loopLoc, cir::CmpOpKind::ne, curLhs, lhsEnd);
              cgf.getBuilder().createCondition(cmp);
            },
            /*bodyBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location loopLoc) {
              mlir::Value curLhs = cgf.getBuilder().createLoad(loopLoc, lhsCurAddr);
              mlir::Value curRhs = cgf.getBuilder().createLoad(loopLoc, rhsCurAddr);
              Address lhsElementAddr(curLhs, cirElementTy, lhsElementAlign);
              Address rhsElementAddr(curRhs, cirElementTy, rhsElementAlign);

              CIRGenFunction::DeclMapRevertingRAII lhsGuard(cgf, lhsVD);
              CIRGenFunction::DeclMapRevertingRAII rhsGuard(cgf, rhsVD);
              cgf.setAddrOfLocalVar(lhsVD, lhsElementAddr);
              cgf.setAddrOfLocalVar(rhsVD, rhsElementAddr);
              cgf.emitIgnoredExpr(reductionOp);

              mlir::Value one = cgf.getBuilder().getConstInt(
                  loopLoc, mlir::cast<cir::IntType>(cgf.ptrDiffTy), 1);
              mlir::Value nextLhs =
                  cgf.getBuilder().createPtrStride(loopLoc, curLhs, one);
              mlir::Value nextRhs =
                  cgf.getBuilder().createPtrStride(loopLoc, curRhs, one);
              cgf.getBuilder().createStore(loopLoc, nextLhs, lhsCurAddr);
              cgf.getBuilder().createStore(loopLoc, nextRhs, rhsCurAddr);
              cgf.getBuilder().createYield(loopLoc);
            });

        cir::YieldOp::create(thenBuilder, thenLoc);
      });
}

static void emitOpenMPUserDefinedByRefReductionInitializer(
    CIRGenFunction &cgf, mlir::Location loc,
    const OMPDeclareReductionDecl &declareReduction, mlir::Value originalArg,
    mlir::Value privateArg);

static void emitOpenMPUserDefinedByRefReductionCombiner(
    CIRGenFunction &cgf, mlir::Location loc,
    const OMPDeclareReductionDecl &declareReduction, mlir::Value lhsArg,
    mlir::Value rhsArg);

static void emitOpenMPUserDefinedAggregateReductionInitializer(
    CIRGenFunction &cgf, mlir::Location loc,
    const OMPDeclareReductionDecl &declareReduction, Address originalAddr,
    Address privateAddr, QualType arrayTy) {
  const auto *typedArrayTy = arrayTy->castAsArrayTypeUnsafe();
  QualType elementType;
  mlir::Value numElements =
      cgf.emitArrayLength(typedArrayTy, elementType, privateAddr);
  mlir::Type cirElementTy = cgf.convertTypeForMem(elementType);
  originalAddr = originalAddr.withElementType(cgf.getBuilder(), cirElementTy);
  privateAddr = privateAddr.withElementType(cgf.getBuilder(), cirElementTy);

  mlir::Value originalBegin =
      cgf.getBuilder().createPtrBitcast(originalAddr.getPointer(), cirElementTy);
  mlir::Value privateBegin =
      cgf.getBuilder().createPtrBitcast(privateAddr.getPointer(), cirElementTy);
  mlir::Value privateEnd =
      cgf.getBuilder().createPtrStride(loc, privateBegin, numElements);
  mlir::Value isNotEmpty = cgf.getBuilder().createCompare(
      loc, cir::CmpOpKind::ne, privateBegin, privateEnd);

  cir::IfOp::create(
      cgf.getBuilder(), loc, isNotEmpty, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
        mlir::OpBuilder::InsertionGuard guard(cgf.getBuilder());
        cgf.getBuilder().setInsertionPointToEnd(thenBuilder.getInsertionBlock());

        auto ptrToElementTy = cgf.getBuilder().getPointerTo(cirElementTy);
        auto ptrToPtrTy = cgf.getBuilder().getPointerTo(ptrToElementTy);
        auto ptrAlignAttr =
            cgf.getBuilder().getAlignmentAttr(cgf.getPointerAlign().getQuantity());
        mlir::Value originalCurSlot = cgf.getBuilder().createAlloca(
            thenLoc, ptrToPtrTy, ptrToElementTy, "__omp_reduction_orig",
            ptrAlignAttr);
        mlir::Value privateCurSlot = cgf.getBuilder().createAlloca(
            thenLoc, ptrToPtrTy, ptrToElementTy, "__omp_reduction_priv",
            ptrAlignAttr);
        Address originalCurAddr(originalCurSlot, ptrToElementTy,
                                cgf.getPointerAlign());
        Address privateCurAddr(privateCurSlot, ptrToElementTy,
                               cgf.getPointerAlign());
        cgf.getBuilder().createStore(thenLoc, originalBegin, originalCurAddr);
        cgf.getBuilder().createStore(thenLoc, privateBegin, privateCurAddr);

        cgf.getBuilder().createDoWhile(
            thenLoc,
            /*condBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location loopLoc) {
              mlir::Value curPrivate =
                  cgf.getBuilder().createLoad(loopLoc, privateCurAddr);
              mlir::Value cmp = cgf.getBuilder().createCompare(
                  loopLoc, cir::CmpOpKind::ne, curPrivate, privateEnd);
              cgf.getBuilder().createCondition(cmp);
            },
            /*bodyBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location loopLoc) {
              mlir::Value curOriginal =
                  cgf.getBuilder().createLoad(loopLoc, originalCurAddr);
              mlir::Value curPrivate =
                  cgf.getBuilder().createLoad(loopLoc, privateCurAddr);
              emitOpenMPUserDefinedByRefReductionInitializer(
                  cgf, loopLoc, declareReduction, curOriginal, curPrivate);

              mlir::Value one = cgf.getBuilder().getConstInt(
                  loopLoc, mlir::cast<cir::IntType>(cgf.ptrDiffTy), 1);
              mlir::Value nextOriginal =
                  cgf.getBuilder().createPtrStride(loopLoc, curOriginal, one);
              mlir::Value nextPrivate =
                  cgf.getBuilder().createPtrStride(loopLoc, curPrivate, one);
              cgf.getBuilder().createStore(loopLoc, nextOriginal,
                                           originalCurAddr);
              cgf.getBuilder().createStore(loopLoc, nextPrivate, privateCurAddr);
              cgf.getBuilder().createYield(loopLoc);
            });

        cir::YieldOp::create(thenBuilder, thenLoc);
      });
}

static void emitOpenMPUserDefinedAggregateReductionCombiner(
    CIRGenFunction &cgf, mlir::Location loc,
    const OMPDeclareReductionDecl &declareReduction, Address lhsAddr,
    Address rhsAddr, QualType arrayTy) {
  const auto *typedArrayTy = arrayTy->castAsArrayTypeUnsafe();
  QualType elementType;
  mlir::Value numElements = cgf.emitArrayLength(typedArrayTy, elementType, lhsAddr);
  mlir::Type cirElementTy = cgf.convertTypeForMem(elementType);
  lhsAddr = lhsAddr.withElementType(cgf.getBuilder(), cirElementTy);
  rhsAddr = rhsAddr.withElementType(cgf.getBuilder(), cirElementTy);

  mlir::Value lhsBegin =
      cgf.getBuilder().createPtrBitcast(lhsAddr.getPointer(), cirElementTy);
  mlir::Value rhsBegin =
      cgf.getBuilder().createPtrBitcast(rhsAddr.getPointer(), cirElementTy);
  mlir::Value lhsEnd =
      cgf.getBuilder().createPtrStride(loc, lhsBegin, numElements);
  mlir::Value isNotEmpty = cgf.getBuilder().createCompare(
      loc, cir::CmpOpKind::ne, lhsBegin, lhsEnd);

  cir::IfOp::create(
      cgf.getBuilder(), loc, isNotEmpty, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
        mlir::OpBuilder::InsertionGuard guard(cgf.getBuilder());
        cgf.getBuilder().setInsertionPointToEnd(thenBuilder.getInsertionBlock());

        auto ptrToElementTy = cgf.getBuilder().getPointerTo(cirElementTy);
        auto ptrToPtrTy = cgf.getBuilder().getPointerTo(ptrToElementTy);
        auto ptrAlignAttr =
            cgf.getBuilder().getAlignmentAttr(cgf.getPointerAlign().getQuantity());
        mlir::Value lhsCurSlot = cgf.getBuilder().createAlloca(
            thenLoc, ptrToPtrTy, ptrToElementTy, "__omp_reduction_lhs",
            ptrAlignAttr);
        mlir::Value rhsCurSlot = cgf.getBuilder().createAlloca(
            thenLoc, ptrToPtrTy, ptrToElementTy, "__omp_reduction_rhs",
            ptrAlignAttr);
        Address lhsCurAddr(lhsCurSlot, ptrToElementTy, cgf.getPointerAlign());
        Address rhsCurAddr(rhsCurSlot, ptrToElementTy, cgf.getPointerAlign());
        cgf.getBuilder().createStore(thenLoc, lhsBegin, lhsCurAddr);
        cgf.getBuilder().createStore(thenLoc, rhsBegin, rhsCurAddr);

        cgf.getBuilder().createDoWhile(
            thenLoc,
            /*condBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location loopLoc) {
              mlir::Value curLhs = cgf.getBuilder().createLoad(loopLoc, lhsCurAddr);
              mlir::Value cmp = cgf.getBuilder().createCompare(
                  loopLoc, cir::CmpOpKind::ne, curLhs, lhsEnd);
              cgf.getBuilder().createCondition(cmp);
            },
            /*bodyBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location loopLoc) {
              mlir::Value curLhs = cgf.getBuilder().createLoad(loopLoc, lhsCurAddr);
              mlir::Value curRhs = cgf.getBuilder().createLoad(loopLoc, rhsCurAddr);
              emitOpenMPUserDefinedByRefReductionCombiner(
                  cgf, loopLoc, declareReduction, curLhs, curRhs);

              mlir::Value one = cgf.getBuilder().getConstInt(
                  loopLoc, mlir::cast<cir::IntType>(cgf.ptrDiffTy), 1);
              mlir::Value nextLhs =
                  cgf.getBuilder().createPtrStride(loopLoc, curLhs, one);
              mlir::Value nextRhs =
                  cgf.getBuilder().createPtrStride(loopLoc, curRhs, one);
              cgf.getBuilder().createStore(loopLoc, nextLhs, lhsCurAddr);
              cgf.getBuilder().createStore(loopLoc, nextRhs, rhsCurAddr);
              cgf.getBuilder().createYield(loopLoc);
            });

        cir::YieldOp::create(thenBuilder, thenLoc);
      });
}

static void emitOpenMPByRefReductionInitializer(CIRGenFunction &cgf,
                                                mlir::Location loc,
                                                const OMPClause *clause,
                                                const Expr *privateExpr,
                                                std::optional<OpenMPReductionKind>
                                                    reductionKind,
                                                mlir::Value allocArg) {
  const VarDecl *privateVD = getOpenMPClauseVarDecl(privateExpr);
  QualType privateTy = privateVD->getType().getNonReferenceType();
  auto ptrTy = mlir::cast<cir::PointerType>(allocArg.getType());
  Address privateAddr(allocArg, ptrTy.getPointee(),
                      cgf.getContext().getDeclAlign(privateVD));

  if (const Expr *init = privateVD->getInit()) {
    if (privateTy->isArrayType()) {
      emitOpenMPAggregateReductionInit(cgf, privateAddr, privateTy, init, loc);
    } else {
      cgf.emitAnyExprToMem(init, privateAddr, init->getType().getQualifiers(),
                           /*isInit=*/true);
    }
    return;
  }

  if (!privateTy->isArrayType()) {
    cgf.getCIRGenModule().errorNYI(
        clause->getBeginLoc(),
        "OpenMP byref reduction for non-array type without initializer");
    return;
  }

  const auto *arrayTy = privateTy->castAsArrayTypeUnsafe();
  QualType elementTy;
  Address lengthAddr = privateAddr;
  cgf.emitArrayLength(arrayTy, elementTy, lengthAddr);
  mlir::Type cirElementTy = cgf.convertTypeForMem(elementTy);
  if (!reductionKind) {
    cgf.emitNullInitialization(loc, privateAddr, privateTy);
    return;
  }
  if (!isOpenMPScalarReductionType(cirElementTy)) {
    cgf.getCIRGenModule().errorNYI(
        clause->getBeginLoc(),
        "OpenMP byref array reduction without scalar identity initializer");
    return;
  }

  mlir::Value identity =
      getOpenMPReductionIdentity(cgf, loc, cirElementTy, *reductionKind);
  emitOpenMPReductionArrayIdentityInit(cgf, privateAddr, privateTy, identity,
                                       loc);
}

static void emitOpenMPUserDefinedReductionInit(
    CIRGenFunction &cgf, mlir::Location loc,
    const OMPDeclareReductionDecl &declareReduction, Address privateAddr,
    Address originalAddr) {
  QualType reductionTy = declareReduction.getType().getNonReferenceType();

  if (!declareReduction.getInitializer()) {
    cgf.emitNullInitialization(loc, privateAddr, reductionTy);
    return;
  }

  const VarDecl *origVD = getOpenMPClauseVarDecl(declareReduction.getInitOrig());
  const VarDecl *privVD = getOpenMPClauseVarDecl(declareReduction.getInitPriv());
  CIRGenFunction::DeclMapRevertingRAII origGuard(cgf, origVD);
  CIRGenFunction::DeclMapRevertingRAII privGuard(cgf, privVD);
  cgf.setAddrOfLocalVar(origVD, originalAddr);
  cgf.setAddrOfLocalVar(privVD, privateAddr);

  if (const Expr *init = privVD->getInit();
      init && !cgf.isTrivialInitializer(init))
    cgf.emitAnyExprToMem(init, privateAddr, init->getType().getQualifiers(),
                         /*isInit=*/true);

  if (declareReduction.getInitializerKind() ==
          OMPDeclareReductionInitKind::Call &&
      declareReduction.getInitializer())
    cgf.emitIgnoredExpr(declareReduction.getInitializer());
}

static mlir::Value emitOpenMPUserDefinedScalarReductionInitializer(
    CIRGenFunction &cgf, mlir::Location loc,
    const OMPDeclareReductionDecl &declareReduction, mlir::Type reductionTy,
    mlir::Value originalArg) {
  QualType declaredTy = declareReduction.getType().getNonReferenceType();
  auto reductionPtrTy = cgf.getBuilder().getPointerTo(reductionTy);
  if (!declareReduction.getInitializer()) {
    CharUnits privateAlign = cgf.getContext().getTypeAlignInChars(declaredTy);
    mlir::Value privateSlot = cgf.getBuilder().createAlloca(
        loc, reductionPtrTy, reductionTy, "__omp_udr_priv", privateAlign);
    Address privateAddr(privateSlot, reductionTy, privateAlign);
    cgf.emitNullInitialization(loc, privateAddr, declaredTy);
    return cgf.getBuilder().createLoad(loc, privateAddr);
  }

  const VarDecl *origVD = getOpenMPClauseVarDecl(declareReduction.getInitOrig());
  const VarDecl *privVD = getOpenMPClauseVarDecl(declareReduction.getInitPriv());
  CharUnits origAlign = cgf.getContext().getDeclAlign(origVD);
  CharUnits privAlign = cgf.getContext().getDeclAlign(privVD);

  mlir::Value originalSlot = cgf.getBuilder().createAlloca(
      loc, reductionPtrTy, reductionTy, "__omp_udr_orig", origAlign);
  mlir::Value privateSlot = cgf.getBuilder().createAlloca(
      loc, reductionPtrTy, reductionTy, "__omp_udr_priv", privAlign);
  Address originalAddr(originalSlot, reductionTy, origAlign);
  Address privateAddr(privateSlot, reductionTy, privAlign);
  cgf.getBuilder().createStore(loc, originalArg, originalAddr);
  emitOpenMPUserDefinedReductionInit(cgf, loc, declareReduction, privateAddr,
                                     originalAddr);
  return cgf.getBuilder().createLoad(loc, privateAddr);
}

static void emitOpenMPUserDefinedByRefReductionInitializer(
    CIRGenFunction &cgf, mlir::Location loc,
    const OMPDeclareReductionDecl &declareReduction, mlir::Value originalArg,
    mlir::Value privateArg) {
  QualType reductionTy = declareReduction.getType().getNonReferenceType();
  auto ptrTy = mlir::cast<cir::PointerType>(privateArg.getType());
  if (!declareReduction.getInitializer()) {
    Address privateAddr(privateArg, ptrTy.getPointee(),
                        cgf.getContext().getTypeAlignInChars(reductionTy));
    cgf.emitNullInitialization(loc, privateAddr, reductionTy);
    return;
  }

  const VarDecl *origVD = getOpenMPClauseVarDecl(declareReduction.getInitOrig());
  const VarDecl *privVD = getOpenMPClauseVarDecl(declareReduction.getInitPriv());

  Address originalAddr(originalArg, ptrTy.getPointee(),
                       cgf.getContext().getDeclAlign(origVD));
  Address privateAddr(privateArg, ptrTy.getPointee(),
                      cgf.getContext().getDeclAlign(privVD));
  emitOpenMPUserDefinedReductionInit(cgf, loc, declareReduction, privateAddr,
                                     originalAddr);
}

static void emitOpenMPByRefReductionCombiner(CIRGenFunction &cgf,
                                             mlir::Location loc,
                                             const Expr *privateExpr,
                                             const Expr *lhsExpr,
                                             const Expr *rhsExpr,
                                             const Expr *reductionOp,
                                             mlir::Value lhsArg,
                                             mlir::Value rhsArg) {
  const VarDecl *privateVD = getOpenMPClauseVarDecl(privateExpr);
  const VarDecl *lhsVD = getOpenMPClauseVarDecl(lhsExpr);
  const VarDecl *rhsVD = getOpenMPClauseVarDecl(rhsExpr);
  QualType privateTy = privateVD->getType().getNonReferenceType();

  auto lhsPtrTy = mlir::cast<cir::PointerType>(lhsArg.getType());
  auto rhsPtrTy = mlir::cast<cir::PointerType>(rhsArg.getType());
  Address lhsAddr(lhsArg, lhsPtrTy.getPointee(),
                  cgf.getContext().getDeclAlign(lhsVD));
  Address rhsAddr(rhsArg, rhsPtrTy.getPointee(),
                  cgf.getContext().getDeclAlign(rhsVD));

  if (privateTy->isArrayType()) {
    emitOpenMPAggregateReductionCombiner(cgf, lhsAddr, rhsAddr, privateTy,
                                         lhsVD, rhsVD, reductionOp, loc);
    return;
  }

  CIRGenFunction::DeclMapRevertingRAII lhsGuard(cgf, lhsVD);
  CIRGenFunction::DeclMapRevertingRAII rhsGuard(cgf, rhsVD);
  cgf.setAddrOfLocalVar(lhsVD, lhsAddr);
  cgf.setAddrOfLocalVar(rhsVD, rhsAddr);
  cgf.emitIgnoredExpr(reductionOp);
}

static mlir::Value emitOpenMPUserDefinedScalarReductionCombiner(
    CIRGenFunction &cgf, mlir::Location loc,
    const OMPDeclareReductionDecl &declareReduction, mlir::Type reductionTy,
    mlir::Value lhsArg, mlir::Value rhsArg) {
  const VarDecl *lhsVD =
      getOpenMPClauseVarDecl(declareReduction.getCombinerOut());
  const VarDecl *rhsVD =
      getOpenMPClauseVarDecl(declareReduction.getCombinerIn());
  auto reductionPtrTy = cgf.getBuilder().getPointerTo(reductionTy);
  CharUnits lhsAlign = cgf.getContext().getDeclAlign(lhsVD);
  CharUnits rhsAlign = cgf.getContext().getDeclAlign(rhsVD);

  mlir::Value lhsSlot = cgf.getBuilder().createAlloca(
      loc, reductionPtrTy, reductionTy, "__omp_udr_lhs", lhsAlign);
  mlir::Value rhsSlot = cgf.getBuilder().createAlloca(
      loc, reductionPtrTy, reductionTy, "__omp_udr_rhs", rhsAlign);
  Address lhsAddr(lhsSlot, reductionTy, lhsAlign);
  Address rhsAddr(rhsSlot, reductionTy, rhsAlign);
  cgf.getBuilder().createStore(loc, lhsArg, lhsAddr);
  cgf.getBuilder().createStore(loc, rhsArg, rhsAddr);

  CIRGenFunction::DeclMapRevertingRAII lhsGuard(cgf, lhsVD);
  CIRGenFunction::DeclMapRevertingRAII rhsGuard(cgf, rhsVD);
  cgf.setAddrOfLocalVar(lhsVD, lhsAddr);
  cgf.setAddrOfLocalVar(rhsVD, rhsAddr);
  cgf.emitIgnoredExpr(declareReduction.getCombiner());
  return cgf.getBuilder().createLoad(loc, lhsAddr);
}

static void emitOpenMPUserDefinedByRefReductionCombiner(
    CIRGenFunction &cgf, mlir::Location loc,
    const OMPDeclareReductionDecl &declareReduction, mlir::Value lhsArg,
    mlir::Value rhsArg) {
  const VarDecl *lhsVD =
      getOpenMPClauseVarDecl(declareReduction.getCombinerOut());
  const VarDecl *rhsVD =
      getOpenMPClauseVarDecl(declareReduction.getCombinerIn());
  auto ptrTy = mlir::cast<cir::PointerType>(lhsArg.getType());

  Address lhsAddr(lhsArg, ptrTy.getPointee(),
                  cgf.getContext().getDeclAlign(lhsVD));
  Address rhsAddr(rhsArg, ptrTy.getPointee(),
                  cgf.getContext().getDeclAlign(rhsVD));

  CIRGenFunction::DeclMapRevertingRAII lhsGuard(cgf, lhsVD);
  CIRGenFunction::DeclMapRevertingRAII rhsGuard(cgf, rhsVD);
  cgf.setAddrOfLocalVar(lhsVD, lhsAddr);
  cgf.setAddrOfLocalVar(rhsVD, rhsAddr);
  cgf.emitIgnoredExpr(declareReduction.getCombiner());
}

static void emitOpenMPByRefReductionCleanup(CIRGenFunction &cgf,
                                            mlir::Location loc,
                                            const Expr *privateExpr,
                                            mlir::Value cleanupArg) {
  const VarDecl *privateVD = getOpenMPClauseVarDecl(privateExpr);
  QualType privateTy = privateVD->getType().getNonReferenceType();
  QualType::DestructionKind dtorKind = privateTy.isDestructedType();
  if (dtorKind == QualType::DK_none)
    return;

  auto cleanupPtrTy = mlir::cast<cir::PointerType>(cleanupArg.getType());
  Address cleanupAddr(cleanupArg, cleanupPtrTy.getPointee(),
                      cgf.getContext().getDeclAlign(privateVD));
  cgf.emitDestroy(cleanupAddr, privateTy, cgf.getDestroyer(dtorKind),
                  cgf.needsEHCleanup(dtorKind));
}

struct OpenMPReductionDeclInfo {
  mlir::omp::DeclareReductionOp decl;
  bool byRef = false;
  mlir::FlatSymbolRefAttr taskReductionSizeSym;
  mlir::Value runtimeElementCount;
};

static OpenMPReductionDeclInfo
getOrCreateOpenMPReductionDecl(CIRGenFunction &cgf, CIRGenBuilderTy &builder,
                               mlir::Location loc,
                               const OMPClause *clause, const Expr *varExpr,
                               const Expr *privateExpr,
                               const Expr *lhsExpr, const Expr *rhsExpr,
                               const Expr *reductionOp, mlir::Type pointerTy,
                               bool allowByRef = false,
                               bool useTaskReductionRuntimeDescriptor = false) {
  QualType originalTy = varExpr->getType().getNonReferenceType();
  const VarDecl *privateVD = getOpenMPClauseVarDecl(privateExpr);
  QualType privateTy = privateVD->getType().getNonReferenceType();
  bool isArraySection = isa<ArraySectionExpr>(varExpr->IgnoreParenImpCasts());
  QualType reductionVarTy = isArraySection ? privateTy : originalTy;
  bool needsRuntimeDescriptor =
      reductionVarTy->isVariablyModifiedType() &&
      (useTaskReductionRuntimeDescriptor ||
       isOpenMPTaskReductionRuntimeDescriptorClause(clause) || isArraySection);
  mlir::Value runtimeElementCount;
  if (reductionVarTy->isVariablyModifiedType() && !needsRuntimeDescriptor) {
    cgf.getCIRGenModule().errorNYI(
        clause->getBeginLoc(),
        "OpenMP variably modified reduction type requires runtime descriptor "
        "lowering");
    return {};
  }
  if (needsRuntimeDescriptor && !reductionVarTy->isArrayType()) {
    cgf.getCIRGenModule().errorNYI(
        clause->getBeginLoc(),
        "OpenMP runtime descriptor reduction for non-array variably modified "
        "type");
    return {};
  }
  if (needsRuntimeDescriptor) {
    runtimeElementCount = isArraySection
                              ? emitOpenMPArraySectionElementCount(cgf, varExpr)
                              : getOpenMPArrayElementCount(cgf, loc,
                                                           reductionVarTy);
    if (!runtimeElementCount)
      return {};
  }

  auto ptrTy = mlir::dyn_cast<cir::PointerType>(pointerTy);
  if (!ptrTy) {
    cgf.getCIRGenModule().errorNYI(
        clause->getBeginLoc(), "OpenMP reduction variable without pointer type");
    return {};
  }

  mlir::Type reductionTy = ptrTy.getPointee();
  // `_Complex` has no native `atomicrmw`, so it is combined by reference
  // (initializer + combiner regions, critical-section fallback) rather than
  // through the scalar in-place atomic path.
  bool isComplex = mlir::isa<cir::ComplexType>(reductionTy);
  bool byRef = isArraySection || originalTy->isArrayType() ||
               !isOpenMPScalarReductionType(reductionTy);
  if (byRef &&
      (!allowByRef ||
       (!isArraySection && !originalTy->isArrayType() &&
        !originalTy->isRecordType() && !isComplex))) {
    cgf.getCIRGenModule().errorNYI(
        clause->getBeginLoc(),
        "OpenMP scalar reduction for non-bool/non-int/non-float type");
    return {};
  }
  if (!byRef && !isOpenMPScalarReductionType(reductionTy)) {
    cgf.getCIRGenModule().errorNYI(
        clause->getBeginLoc(),
        "OpenMP scalar reduction for non-bool/non-int/non-float type");
    return {};
  }

  std::optional<OpenMPReductionKind> reductionKind;
  if (const auto *reductionClause = dyn_cast<OMPReductionClause>(clause))
    reductionKind = getOpenMPReductionKind(reductionClause);
  else if (const auto *taskReductionClause =
               dyn_cast<OMPTaskReductionClause>(clause))
    reductionKind = getOpenMPReductionKind(taskReductionClause);
  else if (const auto *inReductionClause =
               dyn_cast<OMPInReductionClause>(clause))
    reductionKind = getOpenMPReductionKind(inReductionClause);
  else
    llvm_unreachable("unexpected OpenMP reduction clause kind");
  const OMPDeclareReductionDecl *userReductionDecl =
      getOpenMPDeclareReductionDecl(reductionOp);
  if (!reductionKind && !userReductionDecl) {
    cgf.getCIRGenModule().errorNYI(
        clause->getBeginLoc(),
        "OpenMP reduction with non-builtin reduction identifier");
    return {};
  }

  QualType reducedTy = reductionVarTy->isArrayType()
                           ? cgf.getContext().getBaseElementType(reductionVarTy)
                           : reductionVarTy;
  if (userReductionDecl &&
      !cgf.getContext().hasSameType(
          reducedTy, userReductionDecl->getType().getNonReferenceType())) {
    cgf.getCIRGenModule().errorNYI(
        clause->getBeginLoc(),
        "OpenMP reduction variable type does not match declare reduction type");
    return {};
  }

  mlir::ModuleOp module = cgf.getCIRGenModule().getModule();
  std::string declName =
      userReductionDecl
          ? getOpenMPUserDefinedReductionName(cgf, *userReductionDecl,
                                              reductionTy, byRef)
          : ("__omp_reduction_" + getOpenMPReductionOperatorName(*reductionKind) +
             "_" + (byRef ? "byref_" : "") +
             getOpenMPReductionTypeName(reductionTy))
                .str();
  if (needsRuntimeDescriptor)
    declName += "_dynamic_" + getOpenMPTaskReductionSiteSuffix(cgf, varExpr);
  else if (isArraySection)
    declName += "_section_" + getOpenMPReductionExprSiteSuffix(cgf, varExpr);
  if (auto existing = module.lookupSymbol<mlir::omp::DeclareReductionOp>(declName))
    return {existing, byRef, getOpenMPTaskReductionSizeSym(existing),
            runtimeElementCount};

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());
  mlir::TypeAttr byrefElementTypeAttr =
      byRef ? mlir::TypeAttr::get(reductionTy) : mlir::TypeAttr{};
  auto decl = mlir::omp::DeclareReductionOp::create(
      builder, loc, declName, byRef ? pointerTy : reductionTy,
      byrefElementTypeAttr);
  mlir::FlatSymbolRefAttr taskReductionSizeSym;
  if (needsRuntimeDescriptor) {
    cir::GlobalOp sizeGlobal =
        getOrCreateOpenMPTaskReductionSizeGlobal(cgf, loc, varExpr);
    taskReductionSizeSym =
        mlir::FlatSymbolRefAttr::get(sizeGlobal.getSymNameAttr());
    decl->setAttr(kOpenMPTaskReductionSizeSymAttr, taskReductionSizeSym);
  }

  if (byRef) {
    auto allocPtrTy = mlir::cast<cir::PointerType>(pointerTy);
    mlir::Block *allocBlock = builder.createBlock(&decl.getAllocRegion());
    builder.setInsertionPointToEnd(allocBlock);
    mlir::Value privateAlloca;
    mlir::Value elementCount;
    if (taskReductionSizeSym)
      elementCount = emitOpenMPTaskReductionSizeLoad(cgf, loc,
                                                     taskReductionSizeSym);
    else if (runtimeElementCount)
      elementCount = runtimeElementCount;
    else if (isArraySection && reductionVarTy->isArrayType())
      elementCount = getOpenMPArrayElementCount(cgf, loc, reductionVarTy);
    if (isArraySection && reductionVarTy->isArrayType() && !elementCount)
      return {};
    if (elementCount) {
      privateAlloca = builder.createAlloca(
          loc, allocPtrTy, reductionTy, privateVD->getName(),
          cgf.getContext().getDeclAlign(privateVD), elementCount);
    } else {
      privateAlloca = builder.createAlloca(
          loc, allocPtrTy, reductionTy, privateVD->getName(),
          cgf.getContext().getDeclAlign(privateVD));
    }
    mlir::omp::YieldOp::create(builder, loc, privateAlloca);

    mlir::Block *initBlock = builder.createBlock(
        &decl.getInitializerRegion(), {}, {pointerTy, pointerTy}, {loc, loc});
    builder.setInsertionPointToEnd(initBlock);
    {
      CIRGenFunction::LexicalScope regionScope(cgf, loc, initBlock);
      regionScope.setCleanupOnly();
      auto emitInitBody = [&]() {
        if (userReductionDecl && reductionVarTy->isArrayType()) {
          Address originalAddr(initBlock->getArgument(0), allocPtrTy.getPointee(),
                               cgf.getContext().getDeclAlign(privateVD));
          Address privateAddr(initBlock->getArgument(1), allocPtrTy.getPointee(),
                              cgf.getContext().getDeclAlign(privateVD));
          emitOpenMPUserDefinedAggregateReductionInitializer(
              cgf, loc, *userReductionDecl, originalAddr, privateAddr,
              reductionVarTy);
        } else if (userReductionDecl) {
          emitOpenMPUserDefinedByRefReductionInitializer(
              cgf, loc, *userReductionDecl, initBlock->getArgument(0),
              initBlock->getArgument(1));
        } else {
          emitOpenMPByRefReductionInitializer(cgf, loc, clause, privateExpr,
                                              reductionKind,
                                              initBlock->getArgument(1));
        }
      };
      if (taskReductionSizeSym)
        withOpenMPTaskReductionVLASize(cgf, loc, privateTy,
                                       taskReductionSizeSym, emitInitBody);
      else
        emitInitBody();
    }
    mlir::omp::YieldOp::create(builder, loc, initBlock->getArgument(1));

    mlir::Block *combinerBlock = builder.createBlock(
        &decl.getReductionRegion(), {}, {pointerTy, pointerTy}, {loc, loc});
    builder.setInsertionPointToEnd(combinerBlock);
    {
      CIRGenFunction::LexicalScope regionScope(cgf, loc, combinerBlock);
      regionScope.setCleanupOnly();
      auto emitCombinerBody = [&]() {
        if (userReductionDecl && reductionVarTy->isArrayType()) {
          Address lhsAddr(combinerBlock->getArgument(0), allocPtrTy.getPointee(),
                          cgf.getContext().getDeclAlign(privateVD));
          Address rhsAddr(combinerBlock->getArgument(1), allocPtrTy.getPointee(),
                          cgf.getContext().getDeclAlign(privateVD));
          emitOpenMPUserDefinedAggregateReductionCombiner(
              cgf, loc, *userReductionDecl, lhsAddr, rhsAddr, reductionVarTy);
        } else if (userReductionDecl) {
          emitOpenMPUserDefinedByRefReductionCombiner(
              cgf, loc, *userReductionDecl, combinerBlock->getArgument(0),
              combinerBlock->getArgument(1));
        } else {
          emitOpenMPByRefReductionCombiner(cgf, loc, privateExpr, lhsExpr,
                                           rhsExpr, reductionOp,
                                           combinerBlock->getArgument(0),
                                           combinerBlock->getArgument(1));
        }
      };
      if (taskReductionSizeSym)
        withOpenMPTaskReductionVLASize(cgf, loc, privateTy,
                                       taskReductionSizeSym,
                                       emitCombinerBody);
      else
        emitCombinerBody();
    }
    mlir::omp::YieldOp::create(builder, loc, combinerBlock->getArgument(0));

    if (privateTy.isDestructedType() != QualType::DK_none) {
      mlir::Block *cleanupBlock = builder.createBlock(
          &decl.getCleanupRegion(), {}, {pointerTy}, {loc});
      builder.setInsertionPointToEnd(cleanupBlock);
      {
        CIRGenFunction::LexicalScope regionScope(cgf, loc, cleanupBlock);
        regionScope.setCleanupOnly();
        auto emitCleanupBody = [&]() {
          emitOpenMPByRefReductionCleanup(cgf, loc, privateExpr,
                                          cleanupBlock->getArgument(0));
        };
        if (taskReductionSizeSym)
          withOpenMPTaskReductionVLASize(cgf, loc, privateTy,
                                         taskReductionSizeSym,
                                         emitCleanupBody);
        else
          emitCleanupBody();
      }
      mlir::omp::YieldOp::create(builder, loc);
    }
  } else {
    mlir::Block *initBlock = builder.createBlock(&decl.getInitializerRegion(),
                                                 {}, {reductionTy}, {loc});
    builder.setInsertionPointToEnd(initBlock);
    mlir::Value init = userReductionDecl
                           ? emitOpenMPUserDefinedScalarReductionInitializer(
                                 cgf, loc, *userReductionDecl, reductionTy,
                                 initBlock->getArgument(0))
                           : getOpenMPReductionIdentity(cgf, loc, reductionTy,
                                                        *reductionKind);
    mlir::omp::YieldOp::create(builder, loc, init);

    mlir::Block *combinerBlock =
        builder.createBlock(&decl.getReductionRegion(), {},
                            {reductionTy, reductionTy}, {loc, loc});
    builder.setInsertionPointToEnd(combinerBlock);
    mlir::Value combined =
        userReductionDecl
            ? emitOpenMPUserDefinedScalarReductionCombiner(
                  cgf, loc, *userReductionDecl, reductionTy,
                  combinerBlock->getArgument(0), combinerBlock->getArgument(1))
            : emitOpenMPReductionCombiner(cgf, builder, loc, reductionTy,
                                          lhsExpr, rhsExpr, reductionOp);
    mlir::omp::YieldOp::create(builder, loc, combined);

    if (!userReductionDecl && isOpenMPAtomicReductionType(reductionTy))
      if (std::optional<cir::AtomicFetchKind> atomicKind =
              getOpenMPReductionAtomicKind(*reductionKind)) {
      mlir::Block *atomicBlock = builder.createBlock(
          &decl.getAtomicReductionRegion(), {}, {pointerTy, pointerTy},
          {loc, loc});
      builder.setInsertionPointToEnd(atomicBlock);
      mlir::Value rhsValue =
          builder.CIRBaseBuilderTy::createLoad(loc, atomicBlock->getArgument(1));
      cir::AtomicFetchOp::create(
          builder, loc, reductionTy, atomicBlock->getArgument(0), rhsValue,
          *atomicKind, cir::MemOrder::Relaxed,
          /*alignment=*/mlir::IntegerAttr{},
          /*is_volatile=*/false, /*fetchFirst=*/true, /*sync_scope=*/{});
      mlir::omp::YieldOp::create(builder, loc);
    }
  }
  return {decl, byRef, taskReductionSizeSym, runtimeElementCount};
}

static std::string getOpenMPPrivatizerBaseName(
    CIRGenFunction &cgf, const VarDecl *vd,
    llvm::StringRef prefix = "__omp_firstprivate_") {
  std::string base = (prefix + vd->getName()).str();
  const SourceManager &sm = cgf.getContext().getSourceManager();
  PresumedLoc ploc = sm.getPresumedLoc(vd->getLocation());
  if (ploc.isValid())
    base += ("_" + llvm::Twine(ploc.getLine()) + "_" + llvm::Twine(ploc.getColumn()))
                .str();
  return base;
}

static cir::FuncOp getOrCreateOpenMPFirstprivateCopyHelper(
    CIRGenFunction &cgf, CIRGenBuilderTy &builder, mlir::Location loc,
    const VarDecl *vd, mlir::Type valueTy, const VarDecl *privateCopyVD,
    const VarDecl *initVD) {
  assert(privateCopyVD && initVD &&
         "non-trivial firstprivate copy helper requires init expression");

  mlir::ModuleOp module = cgf.getCIRGenModule().getModule();
  std::string baseName =
      getOpenMPPrivatizerBaseName(cgf, vd, "__omp_firstprivate_copy_");
  std::string name = baseName;
  unsigned suffix = 0;
  while (module.lookupSymbol(name))
    name = (baseName + "." + llvm::Twine(++suffix)).str();

  auto voidTy = builder.getVoidTy();
  auto voidPtrTy = builder.getVoidPtrTy();
  cir::FuncOp fn = cgf.getCIRGenModule().createCIRFunction(
      loc, name, builder.getFuncType({voidPtrTy, voidPtrTy}, voidTy), nullptr);
  fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
  fn.setSymVisibility("private");
  cgf.getCIRGenModule().setCIRFunctionAttributesForDefinition(GlobalDecl{}, fn);

  mlir::OpBuilder::InsertionGuard guard(builder);
  CIRGenFunction helperCGF(cgf.getCIRGenModule(), builder,
                           /*suppressNewContext=*/true);
  helperCGF.curGD = GlobalDecl{};
  CIRGenFunction::SourceLocRAIIObject helperLoc(helperCGF, loc);
  mlir::Block *entry = fn.addEntryBlock();

  FunctionArgList args;
  ImplicitParamDecl dstParam(cgf.getContext(), /*DC=*/nullptr,
                             vd->getLocation(), /*Id=*/nullptr,
                             cgf.getContext().VoidPtrTy,
                             ImplicitParamKind::Other);
  ImplicitParamDecl srcParam(cgf.getContext(), /*DC=*/nullptr,
                             vd->getLocation(), /*Id=*/nullptr,
                             cgf.getContext().VoidPtrTy,
                             ImplicitParamKind::Other);
  args.push_back(&dstParam);
  args.push_back(&srcParam);

  CIRGenFunction::SymTableScopeTy varScope(helperCGF.symbolTable);
  {
    CIRGenFunction::LexicalScope fnScope(helperCGF, loc, entry);
    helperCGF.startFunction(GlobalDecl{}, cgf.getContext().VoidTy, fn,
                            fn.getFunctionType(), args, vd->getLocation(),
                            vd->getLocation());

    CIRGenBuilderTy &helperBuilder = helperCGF.getBuilder();
    mlir::Type valuePtrTy = helperBuilder.getPointerTo(valueTy);
    CharUnits align = cgf.getContext().getDeclAlign(vd);

    mlir::Value dstPtr =
        helperBuilder.createLoad(loc, helperCGF.getAddrOfLocalVar(&dstParam));
    if (dstPtr.getType() != valuePtrTy)
      dstPtr = helperBuilder.createBitcast(loc, dstPtr, valuePtrTy);
    Address toAddr(dstPtr, valueTy, align);

    mlir::Value srcPtr =
        helperBuilder.createLoad(loc, helperCGF.getAddrOfLocalVar(&srcParam));
    if (srcPtr.getType() != valuePtrTy)
      srcPtr = helperBuilder.createBitcast(loc, srcPtr, valuePtrTy);
    Address fromAddr(srcPtr, valueTy, align);

    CIRGenFunction::SymTableScopeTy scope(helperCGF.symbolTable);
    CIRGenFunction::DeclMapRevertingRAII initGuard(helperCGF, initVD);
    helperCGF.setAddrOfLocalVar(initVD, fromAddr);
    const Expr *init = privateCopyVD->getInit();
    helperCGF.emitAnyExprToMem(init, toAddr, init->getType().getQualifiers(),
                               /*isInit=*/true);
    helperCGF.finishFunction(vd->getLocation());
  }
  return fn;
}

static mlir::omp::ClauseScheduleKind
getOpenMPScheduleKind(const OMPScheduleClause *clause) {
  switch (clause->getScheduleKind()) {
  case OMPC_SCHEDULE_static:
    return mlir::omp::ClauseScheduleKind::Static;
  case OMPC_SCHEDULE_dynamic:
    return mlir::omp::ClauseScheduleKind::Dynamic;
  case OMPC_SCHEDULE_guided:
    return mlir::omp::ClauseScheduleKind::Guided;
  case OMPC_SCHEDULE_auto:
    return mlir::omp::ClauseScheduleKind::Auto;
  case OMPC_SCHEDULE_runtime:
    return mlir::omp::ClauseScheduleKind::Runtime;
  case OMPC_SCHEDULE_unknown:
    llvm_unreachable("unknown OpenMP schedule kind");
  }
  llvm_unreachable("unexpected OpenMP schedule kind");
}

static mlir::omp::PrivateClauseOp getOrCreateOpenMPPrivatizer(
    CIRGenFunction &cgf, CIRGenBuilderTy &builder, mlir::Location loc,
    const VarDecl *vd, mlir::Type valueTy, mlir::Type pointerTy,
    mlir::omp::DataSharingClauseType dataSharingType,
    const VarDecl *privateCopyVD = nullptr,
    const VarDecl *initVD = nullptr) {
  mlir::ModuleOp module = cgf.getCIRGenModule().getModule();
  std::string baseName = getOpenMPPrivatizerBaseName(cgf, vd);
  std::string name = baseName;
  unsigned suffix = 0;
  while (module.lookupSymbol(name))
    name = (baseName + "." + llvm::Twine(++suffix)).str();

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());

  auto privatizer = mlir::omp::PrivateClauseOp::create(
      builder, loc, name, valueTy, dataSharingType);

  if (dataSharingType != mlir::omp::DataSharingClauseType::FirstPrivate)
    return privatizer;

  mlir::Block *copyBlock = builder.createBlock(
      &privatizer.getCopyRegion(), {}, {pointerTy, pointerTy}, {loc, loc});
  builder.setInsertionPointToEnd(copyBlock);

  QualType varTy = vd->getType();
  CharUnits align = cgf.getContext().getDeclAlign(vd);
  Address fromAddr(copyBlock->getArgument(0), valueTy, align);
  Address toAddr(copyBlock->getArgument(1), valueTy, align);

  if (varTy->isScalarType() || varTy->isReferenceType()) {
    mlir::Value copied = builder.createLoad(loc, fromAddr);
    builder.createStore(loc, copied, toAddr);
  } else if (varTy.isTriviallyCopyableType(cgf.getContext())) {
    // Trivially copyable aggregate: emit memcpy via cir.copy.
    LValue destLV = cgf.makeAddrLValue(toAddr, varTy);
    LValue srcLV = cgf.makeAddrLValue(fromAddr, varTy);
    cgf.emitAggregateCopy(destLV, srcLV, varTy,
                          AggValueSlot::DoesNotOverlap);
  } else if (privateCopyVD && initVD) {
    // Emit non-trivial firstprivate copies in a dedicated helper so any
    // temporaries or cleanup flags stay in a real function body instead of
    // leaking out of the module-level omp.private region.
    cir::FuncOp copyHelper = getOrCreateOpenMPFirstprivateCopyHelper(
        cgf, builder, loc, vd, valueTy, privateCopyVD, initVD);
    mlir::Value dstPtr = toAddr.getPointer();
    if (dstPtr.getType() != builder.getVoidPtrTy())
      dstPtr = builder.createBitcast(loc, dstPtr, builder.getVoidPtrTy());
    mlir::Value srcPtr = fromAddr.getPointer();
    if (srcPtr.getType() != builder.getVoidPtrTy())
      srcPtr = builder.createBitcast(loc, srcPtr, builder.getVoidPtrTy());
    builder.createCallOp(loc, copyHelper, {dstPtr, srcPtr},
                         copyHelper.getCallingConv());
  } else {
    cgf.getCIRGenModule().errorNYI(
        vd->getLocation(),
        "OpenMP parallel firstprivate non-scalar/non-reference variable "
        "without init expression");
  }

  mlir::omp::YieldOp::create(builder, loc, copyBlock->getArgument(1));
  return privatizer;
}

template <typename OpTy>
class OpenMPClauseCIREmitter final
    : public ConstOMPClauseVisitor<OpenMPClauseCIREmitter<OpTy>> {
  OpTy &operation;
  CIRGen::CIRGenFunction &cgf;
  CIRGen::CIRGenBuilderTy &builder;
  ArrayRef<const VarDecl *> enclosingFirstprivateVars;
  bool allowWsloopConditionalLastprivate;
  CIRGenFunction::OpenMPClauseEmissionInfo emissionInfo;
  struct PendingTargetAllocatorPrivateInfo {
    const OMPAllocateClause *clause = nullptr;
    const VarDecl *allocatorVar = nullptr;
  };
  llvm::SmallPtrSet<const VarDecl *, 4> targetUsesAllocatorVars;
  llvm::SmallDenseMap<const VarDecl *, PendingTargetAllocatorPrivateInfo, 4>
      pendingTargetAllocatorPrivateVars;
  llvm::SmallDenseMap<const VarDecl *, const VarDecl *, 8> privateCopyDecls;

  bool isEnclosingFirstprivateVar(const VarDecl *vd) const {
    const VarDecl *canonical = vd->getCanonicalDecl();
    return llvm::any_of(enclosingFirstprivateVars, [&](const VarDecl *candidate) {
      return candidate->getCanonicalDecl() == canonical;
    });
  }

  void preScanTargetAllocatorPrivates(ArrayRef<const OMPClause *> clauses) {
    if constexpr (!std::is_same_v<OpTy, mlir::omp::TargetOp>)
      return;

    for (const OMPClause *clause : clauses) {
      const auto *usesAllocatorsClause =
          dyn_cast<OMPUsesAllocatorsClause>(clause);
      if (!usesAllocatorsClause)
        continue;

      for (unsigned i = 0, e = usesAllocatorsClause->getNumberOfAllocators();
           i != e; ++i) {
        OMPUsesAllocatorsClause::Data data =
            usesAllocatorsClause->getAllocatorData(i);
        if (const VarDecl *allocatorVar =
                data.AllocatorTraits
                    ? getOpenMPAllocatorVarDecl(data.Allocator)
                    : nullptr) {
          targetUsesAllocatorVars.insert(allocatorVar);
        }
      }
    }

    for (const OMPClause *clause : clauses) {
      const auto *allocateClause = dyn_cast<OMPAllocateClause>(clause);
      if (!allocateClause || allocateClause->getAlignment())
        continue;

      const VarDecl *allocatorVar =
          getOpenMPAllocatorVarDecl(allocateClause->getAllocator());
      if (!allocatorVar || !targetUsesAllocatorVars.count(allocatorVar))
        continue;

      for (const Expr *varExpr : allocateClause->varlist()) {
        const VarDecl *privateVar =
            getOpenMPTargetAllocatorPrivateVarDecl(varExpr);
        if (!privateVar) {
          cgf.cgm.errorNYI(varExpr->getBeginLoc(),
                           "OMPAllocateClause target uses_allocators private "
                           "variable expression");
          continue;
        }

        pendingTargetAllocatorPrivateVars[privateVar->getCanonicalDecl()] = {
            allocateClause, allocatorVar};
      }
    }
  }

  void recordPrivateCopyDecl(const Expr *varExpr, const Expr *privateCopyExpr) {
    const VarDecl *vd = getOpenMPClauseVarDeclOrNull(varExpr);
    const VarDecl *privateCopyVD =
        getOpenMPClauseVarDeclOrNull(privateCopyExpr);
    if (!vd || !privateCopyVD)
      return;
    privateCopyDecls.try_emplace(vd->getCanonicalDecl(), privateCopyVD);
  }

  void preScanPrivateCopyDecls(ArrayRef<const OMPClause *> clauses) {
    for (const OMPClause *clause : clauses) {
      if (const auto *privateClause = dyn_cast<OMPPrivateClause>(clause)) {
        for (auto [varExpr, privateCopyExpr] :
             llvm::zip_equal(privateClause->varlist(),
                             privateClause->private_copies()))
          recordPrivateCopyDecl(varExpr, privateCopyExpr);
        continue;
      }

      if (const auto *firstprivateClause =
              dyn_cast<OMPFirstprivateClause>(clause)) {
        for (auto [varExpr, privateCopyExpr] :
             llvm::zip_equal(firstprivateClause->varlist(),
                             firstprivateClause->private_copies()))
          recordPrivateCopyDecl(varExpr, privateCopyExpr);
        continue;
      }

      if (const auto *lastprivateClause = dyn_cast<OMPLastprivateClause>(clause))
        for (auto [varExpr, privateCopyExpr] :
             llvm::zip_equal(lastprivateClause->varlist(),
                             lastprivateClause->private_copies()))
          recordPrivateCopyDecl(varExpr, privateCopyExpr);
    }
  }

  const VarDecl *getAssociatedPrivateCopyDecl(const VarDecl *vd,
                                              const Expr *privateCopyExpr) const {
    if (const VarDecl *privateCopyVD =
            getOpenMPClauseVarDeclOrNull(privateCopyExpr))
      return privateCopyVD;

    auto it = privateCopyDecls.find(vd->getCanonicalDecl());
    return it == privateCopyDecls.end() ? nullptr : it->second;
  }

  static bool needsRuntimeDependPayload(const OMPDependClause *clause) {
    if (clause->getModifier())
      return true;

    switch (clause->getDependencyKind()) {
    case OMPC_DEPEND_depobj:
    case OMPC_DEPEND_outallmemory:
    case OMPC_DEPEND_inoutallmemory:
      return true;
    case OMPC_DEPEND_in:
    case OMPC_DEPEND_out:
    case OMPC_DEPEND_inout:
    case OMPC_DEPEND_mutexinoutset:
    case OMPC_DEPEND_inoutset:
    case OMPC_DEPEND_source:
    case OMPC_DEPEND_sink:
    case OMPC_DEPEND_unknown:
      break;
    }

    return llvm::any_of(clause->varlist(), [](const Expr *expr) {
      return isa<OMPArrayShapingExpr>(expr->IgnoreParenImpCasts());
    });
  }

  void preScanRuntimeDependPayload(ArrayRef<const OMPClause *> clauses) {
    if constexpr (!(std::is_same_v<OpTy, mlir::omp::TaskOp> ||
                    std::is_same_v<OpTy, mlir::omp::TaskwaitOp> ||
                    std::is_same_v<OpTy, mlir::omp::TargetOp>))
      return;

    for (const OMPClause *clause : clauses) {
      const auto *dependClause = dyn_cast<OMPDependClause>(clause);
      if (!dependClause)
        continue;
      if (needsRuntimeDependPayload(dependClause)) {
        emissionInfo.useRuntimeDependPayload = true;
        return;
      }
    }
  }

public:
  OpenMPClauseCIREmitter(OpTy &operation, CIRGen::CIRGenFunction &cgf,
                         CIRGen::CIRGenBuilderTy &builder,
                         ArrayRef<const VarDecl *> enclosingFirstprivateVars,
                         bool allowWsloopConditionalLastprivate)
      : operation(operation), cgf(cgf), builder(builder),
        enclosingFirstprivateVars(enclosingFirstprivateVars),
        allowWsloopConditionalLastprivate(allowWsloopConditionalLastprivate) {}

  void VisitOMPClause(const OMPClause *clause) {
    cgf.cgm.errorNYI(clause->getBeginLoc(), "OpenMPClause ",
                     llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
  }

  void VisitOMPProcBindClause(const OMPProcBindClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::ParallelOp>) {
      mlir::omp::ClauseProcBindKind kind;
      switch (clause->getProcBindKind()) {
      case llvm::omp::ProcBindKind::OMP_PROC_BIND_master:
        kind = mlir::omp::ClauseProcBindKind::Master;
        break;
      case llvm::omp::ProcBindKind::OMP_PROC_BIND_close:
        kind = mlir::omp::ClauseProcBindKind::Close;
        break;
      case llvm::omp::ProcBindKind::OMP_PROC_BIND_spread:
        kind = mlir::omp::ClauseProcBindKind::Spread;
        break;
      case llvm::omp::ProcBindKind::OMP_PROC_BIND_primary:
        kind = mlir::omp::ClauseProcBindKind::Primary;
        break;
      case llvm::omp::ProcBindKind::OMP_PROC_BIND_default:
        // 'default' in the classic-codegen does no runtime call/doesn't
        // really do anything. So this is a no-op, and thus shouldn't change
        // the IR.
        return;
      case llvm::omp::ProcBindKind::OMP_PROC_BIND_unknown:
        llvm_unreachable("unknown proc-bind kind");
      }
      operation.setProcBindKind(kind);
    } else {
      cgf.cgm.errorNYI(
          clause->getBeginLoc(),
          "OMPProcBindClause unimplemented on this directive kind");
    }
  }

  void VisitOMPIfClause(const OMPIfClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::ParallelOp> ||
                  std::is_same_v<OpTy, mlir::omp::TeamsOp> ||
                  std::is_same_v<OpTy, mlir::omp::TaskOp> ||
                  std::is_same_v<OpTy, mlir::omp::TaskloopOp> ||
                  std::is_same_v<OpTy, mlir::omp::TargetOp> ||
                  std::is_same_v<OpTy, mlir::omp::SimdOp> ||
                  std::is_same_v<OpTy, mlir::omp::TargetUpdateOp>) {
      OpenMPDirectiveKind modifier = clause->getNameModifier();
      if constexpr (std::is_same_v<OpTy, mlir::omp::TaskOp> ||
                    std::is_same_v<OpTy, mlir::omp::TaskloopOp>) {
        if (modifier != llvm::omp::OMPD_unknown &&
            modifier != (std::is_same_v<OpTy, mlir::omp::TaskOp>
                             ? llvm::omp::OMPD_task
                             : llvm::omp::OMPD_taskloop)) {
          cgf.cgm.errorNYI(clause->getBeginLoc(),
                           std::is_same_v<OpTy, mlir::omp::TaskOp>
                               ? "OMPIfClause modifier on task directive"
                               : "OMPIfClause modifier on taskloop directive");
          return;
        }
      } else if constexpr (std::is_same_v<OpTy, mlir::omp::TargetOp>) {
        if (modifier != llvm::omp::OMPD_unknown &&
            modifier != llvm::omp::OMPD_target) {
          cgf.cgm.errorNYI(clause->getBeginLoc(),
                           "OMPIfClause modifier on target directive");
          return;
        }
      } else if constexpr (std::is_same_v<OpTy, mlir::omp::TargetUpdateOp>) {
        if (modifier != llvm::omp::OMPD_unknown &&
            modifier != llvm::omp::OMPD_target_update) {
          cgf.cgm.errorNYI(clause->getBeginLoc(),
                           "OMPIfClause modifier on target update directive");
          return;
        }
      } else if constexpr (std::is_same_v<OpTy, mlir::omp::SimdOp>) {
        if (modifier != llvm::omp::OMPD_unknown &&
            modifier != llvm::omp::OMPD_simd) {
          cgf.cgm.errorNYI(clause->getBeginLoc(),
                           "OMPIfClause modifier on simd directive");
          return;
        }
      } else if (modifier != llvm::omp::OMPD_unknown &&
                 modifier != llvm::omp::OMPD_parallel &&
                 modifier != llvm::omp::OMPD_teams) {
        cgf.cgm.errorNYI(clause->getBeginLoc(),
                         "OMPIfClause modifier on combined directive");
        return;
      }

      operation.getIfExprMutable().assign(
          emitOpenMPBoolExpr(cgf, clause->getCondition()));
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPIfClause unimplemented on this directive kind");
    }
  }

  void VisitOMPFinalClause(const OMPFinalClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TaskOp> ||
                  std::is_same_v<OpTy, mlir::omp::TaskloopOp>) {
      operation.getFinalMutable().assign(
          emitOpenMPBoolExpr(cgf, clause->getCondition()));
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPFinalClause unimplemented on this directive kind");
    }
  }

  void VisitOMPPriorityClause(const OMPPriorityClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TaskOp> ||
                  std::is_same_v<OpTy, mlir::omp::TaskloopOp>) {
      operation.getPriorityMutable().assign(
          emitOpenMPIntegerExpr(cgf, clause->getPriority()));
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPPriorityClause unimplemented on this directive kind");
    }
  }

  void VisitOMPMergeableClause(const OMPMergeableClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TaskOp> ||
                  std::is_same_v<OpTy, mlir::omp::TaskloopOp>) {
      operation.setMergeable(true);
    } else {
      cgf.cgm.errorNYI(
          clause->getBeginLoc(),
          "OMPMergeableClause unimplemented on this directive kind");
    }
  }

  void VisitOMPUntiedClause(const OMPUntiedClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TaskOp> ||
                  std::is_same_v<OpTy, mlir::omp::TaskloopOp>) {
      operation.setUntied(true);
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPUntiedClause unimplemented on this directive kind");
    }
  }

  void VisitOMPNogroupClause(const OMPNogroupClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TaskloopOp>) {
      operation.setNogroup(true);
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPNogroupClause unimplemented on this directive kind");
    }
  }

  void VisitOMPGrainsizeClause(const OMPGrainsizeClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TaskloopOp>) {
      if (clause->getModifier() == OMPC_GRAINSIZE_strict) {
        operation.setGrainsizeModAttr(mlir::omp::ClauseGrainsizeTypeAttr::get(
            builder.getContext(), mlir::omp::ClauseGrainsizeType::Strict));
      } else if (clause->getModifier() != OMPC_GRAINSIZE_unknown) {
        cgf.cgm.errorNYI(clause->getBeginLoc(),
                         "OMPGrainsizeClause modifier on taskloop directive");
        return;
      }

      operation.getGrainsizeMutable().assign(
          emitOpenMPIntegerExpr(cgf, clause->getGrainsize()));
    } else {
      cgf.cgm.errorNYI(
          clause->getBeginLoc(),
          "OMPGrainsizeClause unimplemented on this directive kind");
    }
  }

  void VisitOMPNumTasksClause(const OMPNumTasksClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TaskloopOp>) {
      if (clause->getModifier() == OMPC_NUMTASKS_strict) {
        operation.setNumTasksModAttr(mlir::omp::ClauseNumTasksTypeAttr::get(
            builder.getContext(), mlir::omp::ClauseNumTasksType::Strict));
      } else if (clause->getModifier() != OMPC_NUMTASKS_unknown) {
        cgf.cgm.errorNYI(clause->getBeginLoc(),
                         "OMPNumTasksClause modifier on taskloop directive");
        return;
      }

      operation.getNumTasksMutable().assign(
          emitOpenMPIntegerExpr(cgf, clause->getNumTasks()));
    } else {
      cgf.cgm.errorNYI(
          clause->getBeginLoc(),
          "OMPNumTasksClause unimplemented on this directive kind");
    }
  }

  void VisitOMPNowaitClause(const OMPNowaitClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::WsloopOp> ||
                  std::is_same_v<OpTy, mlir::omp::TaskwaitOp> ||
                  std::is_same_v<OpTy, mlir::omp::TargetOp> ||
                  std::is_same_v<OpTy, mlir::omp::TargetUpdateOp>) {
      operation.setNowait(true);
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPNowaitClause unimplemented on this directive kind");
    }
  }

  void VisitOMPOrderedClause(const OMPOrderedClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::WsloopOp>) {
      int64_t orderedLoops = 0;
      if (const Expr *numForLoops = clause->getNumForLoops()) {
        orderedLoops = numForLoops->EvaluateKnownConstInt(cgf.getContext())
                           .getSExtValue();
      }
      operation->setAttr("ordered", builder.getI64IntegerAttr(orderedLoops));
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPOrderedClause unimplemented on this directive kind");
    }
  }

  void VisitOMPDetachClause(const OMPDetachClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TaskOp>) {
      const Expr *evtExpr = clause->getEventHandler()->IgnoreParenImpCasts();
      mlir::Value eventHandle = cgf.emitLValue(evtExpr).getPointer();
      auto pointerTy = mlir::dyn_cast<cir::PointerType>(eventHandle.getType());
      if (!pointerTy) {
        cgf.cgm.errorNYI(clause->getBeginLoc(),
                         "OMPDetachClause non-pointer event handle storage");
        return;
      }
      operation.getEventHandleMutable().assign(eventHandle);
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPDetachClause unimplemented on this directive kind");
    }
  }

  void VisitOMPDependClause(const OMPDependClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TaskOp> ||
                  std::is_same_v<OpTy, mlir::omp::TaskwaitOp> ||
                  std::is_same_v<OpTy, mlir::omp::TargetOp> ||
                  std::is_same_v<OpTy, mlir::omp::TargetUpdateOp>) {
      if constexpr (std::is_same_v<OpTy, mlir::omp::TaskOp> ||
                    std::is_same_v<OpTy, mlir::omp::TaskwaitOp> ||
                    std::is_same_v<OpTy, mlir::omp::TargetOp>) {
        if (emissionInfo.useRuntimeDependPayload)
          return;
      }

      if (clause->getModifier()) {
        cgf.cgm.errorNYI(clause->getBeginLoc(),
                         "OMPDependClause modifier on this directive");
        return;
      }

      auto kind = getOpenMPTaskDependKind(clause->getDependencyKind());
      if (!kind) {
        cgf.cgm.errorNYI(clause->getBeginLoc(),
                         "OMPDependClause kind on task/taskwait directive");
        return;
      }

      llvm::SmallVector<mlir::Attribute, 4> dependKinds;
      llvm::SmallVector<mlir::Value, 4> dependVars;
      llvm::SmallVector<mlir::Value, 4> dependSizes;
      dependKinds.reserve(clause->varlist_size());
      dependVars.reserve(clause->varlist_size());
      dependSizes.reserve(clause->varlist_size());

      for (const Expr *varExpr : clause->varlist()) {
        auto dependInfo = emitOpenMPDependVarPointerAndSize(cgf, varExpr);
        if (!dependInfo)
          return;
        auto pointerTy =
            mlir::dyn_cast<cir::PointerType>(dependInfo->pointer.getType());
        if (!pointerTy) {
          cgf.cgm.errorNYI(varExpr->getBeginLoc(),
                           "OpenMP taskwait depend non-pointer capture");
          return;
        }

        dependVars.push_back(dependInfo->pointer);
        dependSizes.push_back(dependInfo->size);
        dependKinds.push_back(mlir::omp::ClauseTaskDependAttr::get(
            &cgf.getMLIRContext(), *kind));
      }

      operation.getDependVarsMutable().append(dependVars);
      operation.getDependSizesMutable().append(dependSizes);
      // The OpenMP `depend_sizes`
      // variadic operand on the upstream-shared depend-clause-bearing ops
      // is a CIR-only addition. A clean
      // wrapper-op carve-out for the SSA-variadic operand would touch ~12
      // CIRGen sites + 7 op surfaces (parser/printer/builder/verifier on
      // omp.task, omp.target, omp.target_data/enter/exit/update,
      // omp.taskwait) plus the offload privatization pass; deferred. As
      // with the prior four carve-outs (severity_kind /
      // num_threads_modifier / message_var / taskdependlist), stamp a
      // CIR-namespaced marker `cir.omp.depend_sizes_owned` (UnitAttr) at
      // the producer so the OpenMP-to-LLVM translator gates its
      // `depend_sizes` reads on a CIR-owned signal rather than blindly
      // consuming the shared TD operand. The TD operand stays in
      // OpenMPClauses.td for verifier/printer consistency; the marker
      // pins ownership of the lowered runtime depend-record sizes until
      // the wrapper-op follow-up lands.
      if (!dependSizes.empty())
        operation->setAttr("cir.omp.depend_sizes_owned",
                           mlir::UnitAttr::get(&cgf.getMLIRContext()));
      llvm::SmallVector<mlir::Attribute, 4> allDependKinds;
      if (auto existingKinds = operation.getDependKinds())
        llvm::append_range(allDependKinds, *existingKinds);
      llvm::append_range(allDependKinds, dependKinds);
      operation.setDependKindsAttr(mlir::ArrayAttr::get(&cgf.getMLIRContext(),
                                                        allDependKinds));
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPDependClause unimplemented on this directive kind");
    }
  }

  void VisitOMPDeviceClause(const OMPDeviceClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TargetOp> ||
                  std::is_same_v<OpTy, mlir::omp::TargetUpdateOp>) {
      if constexpr (std::is_same_v<OpTy, mlir::omp::TargetOp>) {
        if (clause->getModifier() == OMPC_DEVICE_ancestor) {
          cgf.cgm.errorNYI(clause->getBeginLoc(),
                           "OMPDeviceClause ancestor modifier on target "
                           "directive");
          return;
        }
      }
      operation.getDeviceMutable().assign(
          emitOpenMPIntegerExpr(cgf, clause->getDevice()));
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPDeviceClause unimplemented on this directive kind");
    }
  }

  void VisitOMPMapClause(const OMPMapClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TargetOp>) {
      if (const_cast<OMPMapClause *>(clause)->getIteratorModifier()) {
        cgf.cgm.errorNYI(clause->getBeginLoc(),
                         "OMPMapClause iterator modifier on target");
        return;
      }

      std::optional<mlir::omp::ClauseMapFlags> mapFlags =
          getOpenMPMapClauseFlags(cgf, clause,
                                  /*allowMapperModifier=*/true);
      if (!mapFlags)
        return;

      struct PendingDirectMap {
        mlir::Value map;
        const VarDecl *baseVar = nullptr;
        bool isThis = false;
      };
      struct PendingMemberParentMap {
        const Expr *baseExpr = nullptr;
        const VarDecl *baseVar = nullptr;
        bool isThis = false;
        mlir::Location loc;
        llvm::SmallVector<mlir::Value, 4> childMaps;
        llvm::SmallVector<llvm::SmallVector<int64_t, 4>, 4> childMemberIndices;
      };
      struct PendingMapOrderEntry {
        bool isParentGroup = false;
        unsigned index = 0;
      };

      llvm::SmallDenseSet<const VarDecl *, 4> directMapBases;
      for (const Expr *varExpr : clause->varlist()) {
        if (!getOpenMPMapMemberIndices(varExpr).empty())
          continue;
        if (const VarDecl *baseVar = getOpenMPMapBaseVarDecl(varExpr))
          directMapBases.insert(baseVar);
      }

      llvm::SmallVector<PendingDirectMap, 4> directMaps;
      llvm::SmallVector<PendingMemberParentMap, 4> parentGroups;
      llvm::SmallVector<PendingMapOrderEntry, 4> mapOrder;
      directMaps.reserve(clause->varlist_size());
      parentGroups.reserve(clause->varlist_size());
      mapOrder.reserve(clause->varlist_size());
      unsigned mapStartIdx = operation.getMapVars().size();
      auto mapperIt = clause->mapperlist_begin();
      auto recordBaseMap = [&](const VarDecl *baseVar, unsigned mapIndex) {
        if (!baseVar)
          return;

        for (const VarDecl *existingVar : emissionInfo.mapVars)
          if (existingVar == baseVar)
            return;

        emissionInfo.mapIndices.push_back(mapIndex);
        emissionInfo.mapVars.push_back(baseVar);
      };
      auto getOrCreateParentGroup = [&](const Expr *baseExpr,
                                        const VarDecl *baseVar, bool isThis,
                                        mlir::Location loc) -> unsigned {
        for (auto [index, group] : llvm::enumerate(parentGroups))
          if (group.baseVar == baseVar && group.isThis == isThis)
            return index;

        unsigned index = parentGroups.size();
        parentGroups.push_back({baseExpr, baseVar, isThis, loc, {}, {}});
        mapOrder.push_back({/*isParentGroup=*/true, index});
        return index;
      };

      for (const Expr *varExpr : clause->varlist()) {
        bool isThisRooted = isOpenMPMapCXXThisRooted(varExpr);
        std::optional<OpenMPMapOperandInfo> mapInfo =
            getOpenMPMapOperandInfo(
                cgf, varExpr,
                /*usePointerValueForBarePointerExpr=*/false,
                /*captureBoundedPointerSource=*/true);
        if (!mapInfo)
          return;

        mlir::FlatSymbolRefAttr mapperId =
            getOpenMPMapperId(cgf, *mapperIt, clause->getBeginLoc());
        auto mapInfoOp =
            createOpenMPMapInfoOp(builder, *mapInfo, *mapFlags, mapperId);
        bool isThisMemberMap = !mapInfo->memberIndices.empty() &&
                               isOpenMPMapCXXThisRooted(varExpr);
        bool needsParentMemberMap =
            isThisMemberMap ||
            (mapInfo->baseVar && !mapInfo->memberIndices.empty() &&
             !directMapBases.contains(mapInfo->baseVar));
        if (needsParentMemberMap) {
          unsigned parentIndex =
              getOrCreateParentGroup(getOpenMPMapBaseExpr(varExpr),
                                     mapInfo->baseVar, isThisMemberMap,
                                     mapInfo->beginLoc);
          auto &parent = parentGroups[parentIndex];
          parent.childMaps.push_back(mapInfoOp.getResult());
          parent.childMemberIndices.push_back(mapInfo->memberIndices);
        } else {
          directMaps.push_back(
              {mapInfoOp.getResult(),
               mapInfo->memberIndices.empty() ? mapInfo->baseVar : nullptr,
               isThisRooted});
          mapOrder.push_back(
              {/*isParentGroup=*/false,
               static_cast<unsigned>(directMaps.size() - 1)});
        }
        ++mapperIt;
      }

      llvm::SmallVector<mlir::Value, 4> mapVars;
      mapVars.reserve(directMaps.size() + clause->varlist_size());
      for (const PendingMapOrderEntry &entry : mapOrder) {
        if (!entry.isParentGroup) {
          const auto &direct = directMaps[entry.index];
          unsigned mapIndex = mapStartIdx + mapVars.size();
          mapVars.push_back(direct.map);
          if (direct.isThis) {
            if (!emissionInfo.targetThisMapIndex)
              emissionInfo.targetThisMapIndex = mapIndex;
          }
          recordBaseMap(direct.baseVar, mapIndex);
          continue;
        }

        const auto &parentGroup = parentGroups[entry.index];
        std::optional<OpenMPMapOperandInfo> parentInfo;
        if (parentGroup.isThis) {
          parentInfo = getOpenMPThisMapOperandInfo(cgf, parentGroup.loc);
        } else {
          parentInfo = getOpenMPMapOperandInfo(cgf, parentGroup.baseExpr);
        }
        if (!parentInfo)
          return;
        auto parentMapInfo = createOpenMPMapInfoOp(
            builder, *parentInfo, *mapFlags, mlir::FlatSymbolRefAttr{},
            parentGroup.childMaps,
            createOpenMPMembersIndexAttr(builder,
                                         parentGroup.childMemberIndices),
            /*partialMap=*/true);
        unsigned parentMapIndex = mapStartIdx + mapVars.size();
        if (parentGroup.isThis) {
          if (!emissionInfo.targetThisMapIndex)
            emissionInfo.targetThisMapIndex = parentMapIndex;
        } else {
          recordBaseMap(parentGroup.baseVar, parentMapIndex);
        }
        mapVars.push_back(parentMapInfo.getResult());
        llvm::append_range(mapVars, parentGroup.childMaps);
      }

      operation.getMapVarsMutable().append(mapVars);
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPMapClause unimplemented on this directive kind");
    }
  }

  void VisitOMPIsDevicePtrClause(const OMPIsDevicePtrClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TargetOp>) {
      // MLIR's target lowering derives the plain-target TARGET_PARAM/LITERAL
      // runtime maptype from the backing map.info `is_device_ptr` flag.
      constexpr auto pointerClauseFlags = mlir::omp::ClauseMapFlags::is_device_ptr;

      llvm::SmallVector<mlir::Value, 4> mapVars;
      mapVars.reserve(clause->varlist_size());
      unsigned mapStartIdx = operation.getMapVars().size();
      for (const Expr *varExpr : clause->varlist()) {
        const VarDecl *vd = getOpenMPPlainTargetDevicePointerVarDecl(varExpr);
        if (!vd) {
          cgf.cgm.errorNYI(clause->getBeginLoc(),
                           "OMPIsDevicePtrClause target item without "
                           "directly remappable DeclRefExpr base");
          return;
        }

        std::optional<OpenMPMapOperandInfo> mapInfo = getOpenMPMapOperandInfo(
            cgf, varExpr, /*usePointerValueForBarePointerExpr=*/true);
        if (!mapInfo)
          return;

        auto mapInfoOp = createOpenMPMapInfoOp(builder, *mapInfo,
                                               pointerClauseFlags,
                                               mlir::FlatSymbolRefAttr{});
        mapVars.push_back(mapInfoOp.getResult());
        unsigned mapIndex = mapStartIdx + mapVars.size() - 1;
        emissionInfo.mapIndices.push_back(mapIndex);
        if (vd->getType()->isReferenceType())
          emissionInfo.mapReferenceValueIndices.push_back(mapIndex);
        emissionInfo.mapVars.push_back(vd);
      }

      operation.getMapVarsMutable().append(mapVars);
    } else {
      cgf.cgm.errorNYI(
          clause->getBeginLoc(),
          "OMPIsDevicePtrClause unimplemented on this directive kind");
    }
  }

  void VisitOMPHasDeviceAddrClause(const OMPHasDeviceAddrClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TargetOp>) {
      // MLIR's target lowering derives the plain-target TARGET_PARAM/LITERAL
      // runtime maptype from the backing map.info `is_device_ptr` flag.
      constexpr auto pointerClauseFlags = mlir::omp::ClauseMapFlags::is_device_ptr;

      llvm::SmallVector<mlir::Value, 4> hasDeviceAddrVars;
      hasDeviceAddrVars.reserve(clause->varlist_size());
      unsigned hdaStartIdx = operation.getHasDeviceAddrVars().size();
      for (const Expr *varExpr : clause->varlist()) {
        const VarDecl *vd =
            getOpenMPPlainTargetDevicePointerVarDecl(varExpr,
                                                     /*allowArraySections=*/true);
        if (!vd) {
          cgf.cgm.errorNYI(clause->getBeginLoc(),
                           "OMPHasDeviceAddrClause target item without "
                           "directly remappable DeclRefExpr base");
          return;
        }

        std::optional<OpenMPMapOperandInfo> mapInfo =
            getOpenMPMapOperandInfo(cgf, varExpr);
        if (!mapInfo)
          return;

        auto mapInfoOp = createOpenMPMapInfoOp(builder, *mapInfo,
                                               pointerClauseFlags,
                                               mlir::FlatSymbolRefAttr{});
        hasDeviceAddrVars.push_back(mapInfoOp.getResult());
        emissionInfo.hasDeviceAddrIndices.push_back(hdaStartIdx +
                                                    hasDeviceAddrVars.size() -
                                                    1);
        emissionInfo.hasDeviceAddrVars.push_back(vd);
      }

      operation.getHasDeviceAddrVarsMutable().append(hasDeviceAddrVars);
    } else {
      cgf.cgm.errorNYI(
          clause->getBeginLoc(),
          "OMPHasDeviceAddrClause unimplemented on this directive kind");
    }
  }

  void VisitOMPToClause(const OMPToClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TargetUpdateOp>) {
      if (clause->getIteratorModifier()) {
        cgf.cgm.errorNYI(clause->getBeginLoc(),
                         "OMPToClause iterator modifier on target update");
        return;
      }

      std::optional<mlir::omp::ClauseMapFlags> modifierFlags =
          getOpenMPMotionModifierFlags(cgf, clause,
                                       /*allowMapperModifier=*/true);
      if (!modifierFlags)
        return;

      llvm::SmallVector<mlir::Value, 4> mapVars;
      mapVars.reserve(clause->varlist_size());
      auto mapperIt = clause->mapperlist_begin();
      for (const Expr *varExpr : clause->varlist()) {
        std::optional<OpenMPMapOperandInfo> mapInfo =
            getOpenMPMapOperandInfo(cgf, varExpr);
        if (!mapInfo)
          return;

        mlir::FlatSymbolRefAttr mapperId =
            getOpenMPMapperId(cgf, *mapperIt, clause->getBeginLoc());
        auto mapInfoOp = createOpenMPMapInfoOp(
            builder, *mapInfo, mlir::omp::ClauseMapFlags::to | *modifierFlags,
            mapperId);
        mapVars.push_back(mapInfoOp.getResult());
        ++mapperIt;
      }
      operation.getMapVarsMutable().append(mapVars);
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPToClause unimplemented on this directive kind");
    }
  }

  void VisitOMPFromClause(const OMPFromClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TargetUpdateOp>) {
      if (clause->getIteratorModifier()) {
        cgf.cgm.errorNYI(clause->getBeginLoc(),
                         "OMPFromClause iterator modifier on target update");
        return;
      }

      std::optional<mlir::omp::ClauseMapFlags> modifierFlags =
          getOpenMPMotionModifierFlags(cgf, clause,
                                       /*allowMapperModifier=*/true);
      if (!modifierFlags)
        return;

      llvm::SmallVector<mlir::Value, 4> mapVars;
      mapVars.reserve(clause->varlist_size());
      auto mapperIt = clause->mapperlist_begin();
      for (const Expr *varExpr : clause->varlist()) {
        std::optional<OpenMPMapOperandInfo> mapInfo =
            getOpenMPMapOperandInfo(cgf, varExpr);
        if (!mapInfo)
          return;

        mlir::FlatSymbolRefAttr mapperId =
            getOpenMPMapperId(cgf, *mapperIt, clause->getBeginLoc());
        auto mapInfoOp = createOpenMPMapInfoOp(
            builder, *mapInfo,
            mlir::omp::ClauseMapFlags::from | *modifierFlags, mapperId);
        mapVars.push_back(mapInfoOp.getResult());
        ++mapperIt;
      }
      operation.getMapVarsMutable().append(mapVars);
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPFromClause unimplemented on this directive kind");
    }
  }

  void VisitOMPAllocateClause(const OMPAllocateClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::ParallelOp> ||
                  std::is_same_v<OpTy, mlir::omp::WsloopOp>) {
      // Non-target parallel/worksharing allocate(...) is carried by the
      // synthesized private-copy decl attributes on the associated
      // private-like clauses.
      return;
    } else if constexpr (std::is_same_v<OpTy, mlir::omp::TaskOp> ||
                  std::is_same_v<OpTy, mlir::omp::TaskloopOp> ||
                  std::is_same_v<OpTy, mlir::omp::TaskgroupOp> ||
                  std::is_same_v<OpTy, mlir::omp::TargetOp>) {
      if (clause->getAlignment()) {
        cgf.cgm.errorNYI(clause->getBeginLoc(),
                         "OMPAllocateClause alignment on this directive kind");
        return;
      }

      const Expr *allocatorExpr = clause->getAllocator();
      if (!allocatorExpr) {
        cgf.cgm.errorNYI(clause->getBeginLoc(),
                         "OMPAllocateClause default allocator on this "
                         "directive");
        return;
      }

      if constexpr (std::is_same_v<OpTy, mlir::omp::TargetOp>) {
        if (const VarDecl *allocatorVar =
                getOpenMPAllocatorVarDecl(allocatorExpr)) {
          if (targetUsesAllocatorVars.count(allocatorVar))
            return;
          cgf.cgm.errorNYI(
              clause->getBeginLoc(),
              "OMPAllocateClause variable allocator on target directive");
          return;
        }
      }

      mlir::Value allocatorVar = cgf.emitScalarExpr(allocatorExpr);
      llvm::SmallVector<mlir::Value, 4> allocateVars;
      llvm::SmallVector<mlir::Value, 4> allocatorVars;
      allocateVars.reserve(clause->varlist_size());
      allocatorVars.reserve(clause->varlist_size());

      for (const Expr *varExpr : clause->varlist()) {
        mlir::Value allocateVar = cgf.emitLValue(varExpr).getPointer();
        auto pointerTy = mlir::dyn_cast<cir::PointerType>(allocateVar.getType());
        if (!pointerTy) {
          cgf.cgm.errorNYI(varExpr->getBeginLoc(),
                           "OpenMP task allocate non-pointer capture");
          return;
        }

        allocateVars.push_back(allocateVar);
        allocatorVars.push_back(allocatorVar);
      }

      operation.getAllocateVarsMutable().append(allocateVars);
      operation.getAllocatorVarsMutable().append(allocatorVars);
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPAllocateClause unimplemented on this directive "
                       "kind");
    }
  }

  void VisitOMPUsesAllocatorsClause(const OMPUsesAllocatorsClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TargetOp>) {
      llvm::SmallVector<mlir::Value, 4> mapVars;

      for (unsigned i = 0, e = clause->getNumberOfAllocators(); i != e; ++i) {
        OMPUsesAllocatorsClause::Data data = clause->getAllocatorData(i);
        if (!data.AllocatorTraits)
          continue;

        const VarDecl *allocatorVar = getOpenMPAllocatorVarDecl(data.Allocator);
        const VarDecl *traitsVar = getOpenMPAllocatorVarDecl(data.AllocatorTraits);
        if (!allocatorVar || !traitsVar) {
          cgf.cgm.errorNYI(clause->getBeginLoc(),
                           "OpenMP uses_allocators non-DeclRef allocator data");
          return;
        }

        std::optional<OpenMPMapOperandInfo> traitsMapInfo =
            getOpenMPMapOperandInfo(cgf, data.AllocatorTraits);
        if (!traitsMapInfo)
          return;

        auto mapInfoOp = createOpenMPMapInfoOp(
            builder, *traitsMapInfo, mlir::omp::ClauseMapFlags::implicit,
            mlir::FlatSymbolRefAttr{});
        mapVars.push_back(mapInfoOp.getResult());
        emissionInfo.mapIndices.push_back(operation.getMapVars().size() +
                                          mapVars.size() - 1);
        emissionInfo.mapVars.push_back(traitsVar);
        emissionInfo.usesAllocatorVars.push_back(
            {allocatorVar, traitsVar, emissionInfo.mapIndices.back()});
      }

      if (!mapVars.empty())
        operation.getMapVarsMutable().append(mapVars);
      return;
    }

    cgf.cgm.errorNYI(clause->getBeginLoc(),
                     "OMPUsesAllocatorsClause unimplemented on this directive "
                     "kind");
  }

  void VisitOMPTaskReductionClause(const OMPTaskReductionClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TaskgroupOp>) {
      llvm::SmallVector<mlir::Value, 4> reductionVars;
      llvm::SmallVector<bool, 4> reductionByRef;
      llvm::SmallVector<mlir::Attribute, 4> reductionSyms;
      llvm::SmallVector<unsigned, 4> reductionRemapIndices;
      llvm::SmallVector<const VarDecl *, 4> reductionEmissionVars;
      llvm::SmallVector<CIRGenFunction::OpenMPTaskReductionRuntimeSizeInfo, 4>
          reductionRuntimeSizes;
      reductionVars.reserve(clause->varlist_size());
      reductionByRef.reserve(clause->varlist_size());
      reductionSyms.reserve(clause->varlist_size());
      reductionRemapIndices.reserve(clause->varlist_size());
      reductionEmissionVars.reserve(clause->varlist_size());
      reductionRuntimeSizes.reserve(clause->varlist_size());
      unsigned reductionStartIdx = operation.getTaskReductionVars().size();

      for (auto [varExpr, privateExpr, lhsExpr, rhsExpr, reductionOp] :
           llvm::zip_equal(clause->varlist(), clause->privates(),
                           clause->lhs_exprs(), clause->rhs_exprs(),
                           clause->reduction_ops())) {
        if (!isOpenMPReductionVarExpr(varExpr)) {
          cgf.cgm.errorNYI(varExpr->getExprLoc(),
                           "OpenMP reduction on non-variable expression "
                           "(e.g. array element)");
          return;
        }
        const VarDecl *vd =
            isa<DeclRefExpr>(varExpr->IgnoreImpCasts())
                ? getOpenMPClauseVarDecl(varExpr)
                : nullptr;
        const VarDecl *privateVD = getOpenMPClauseVarDecl(privateExpr);
        const VarDecl *baseVD = getOpenMPReductionBaseVarDecl(varExpr);
        mlir::Value reductionVar =
            emitOpenMPReductionVarPointer(cgf, varExpr);
        if (!reductionVar)
          return;
        OpenMPReductionDeclInfo declInfo = getOrCreateOpenMPReductionDecl(
            cgf, builder,
            baseVD ? cgf.getLoc(baseVD->getLocation())
                   : cgf.getLoc(varExpr->getExprLoc()),
            clause, varExpr, privateExpr, lhsExpr, rhsExpr, reductionOp,
            reductionVar.getType(), /*allowByRef=*/true);
        if (!declInfo.decl)
          return;
        unsigned reductionIndex = reductionStartIdx++;
        reductionVars.push_back(reductionVar);
        reductionByRef.push_back(declInfo.byRef);
        reductionSyms.push_back(mlir::SymbolRefAttr::get(declInfo.decl));
        if (vd) {
          reductionRemapIndices.push_back(reductionIndex);
          reductionEmissionVars.push_back(vd);
        }
        if (declInfo.taskReductionSizeSym) {
          reductionRuntimeSizes.push_back(
              {reductionIndex, privateVD, vd,
               declInfo.runtimeElementCount});
        }
      }

      operation.getTaskReductionVarsMutable().append(reductionVars);
      llvm::append_range(emissionInfo.taskReductionIndices,
                         reductionRemapIndices);
      llvm::append_range(emissionInfo.taskReductionVars, reductionEmissionVars);
      llvm::append_range(emissionInfo.taskReductionRuntimeSizes,
                         reductionRuntimeSizes);

      llvm::SmallVector<bool, 4> allReductionByRef;
      if (auto existingByRef = operation.getTaskReductionByref())
        llvm::append_range(allReductionByRef, *existingByRef);
      llvm::append_range(allReductionByRef, reductionByRef);
      operation.setTaskReductionByref(mlir::DenseBoolArrayAttr::get(
          builder.getContext(), allReductionByRef));

      llvm::SmallVector<mlir::Attribute, 4> allReductionSyms;
      if (auto existingSyms = operation.getTaskReductionSyms())
        llvm::append_range(allReductionSyms, *existingSyms);
      llvm::append_range(allReductionSyms, reductionSyms);
      operation.setTaskReductionSymsAttr(
          mlir::ArrayAttr::get(builder.getContext(), allReductionSyms));
    } else {
      cgf.cgm.errorNYI(
          clause->getBeginLoc(),
          "OMPTaskReductionClause unimplemented on this directive kind");
    }
  }

  void VisitOMPInReductionClause(const OMPInReductionClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TaskOp> ||
                  std::is_same_v<OpTy, mlir::omp::TaskloopOp>) {
      llvm::SmallVector<mlir::Value, 4> reductionVars;
      llvm::SmallVector<bool, 4> reductionByRef;
      llvm::SmallVector<mlir::Attribute, 4> reductionSyms;
      llvm::SmallVector<unsigned, 4> reductionRemapIndices;
      llvm::SmallVector<const VarDecl *, 4> reductionEmissionVars;
      llvm::SmallVector<CIRGenFunction::OpenMPTaskReductionRuntimeSizeInfo, 4>
          reductionRuntimeSizes;
      reductionVars.reserve(clause->varlist_size());
      reductionByRef.reserve(clause->varlist_size());
      reductionSyms.reserve(clause->varlist_size());
      reductionRemapIndices.reserve(clause->varlist_size());
      reductionEmissionVars.reserve(clause->varlist_size());
      reductionRuntimeSizes.reserve(clause->varlist_size());
      unsigned reductionStartIdx = operation.getInReductionVars().size();

      for (auto [varExpr, privateExpr, lhsExpr, rhsExpr, reductionOp] :
           llvm::zip_equal(clause->varlist(), clause->privates(),
                           clause->lhs_exprs(), clause->rhs_exprs(),
                           clause->reduction_ops())) {
        if (!isOpenMPReductionVarExpr(varExpr)) {
          cgf.cgm.errorNYI(varExpr->getExprLoc(),
                           "OpenMP reduction on non-variable expression "
                           "(e.g. array element)");
          return;
        }
        const VarDecl *vd =
            isa<DeclRefExpr>(varExpr->IgnoreImpCasts())
                ? getOpenMPClauseVarDecl(varExpr)
                : nullptr;
        const VarDecl *privateVD = getOpenMPClauseVarDecl(privateExpr);
        const VarDecl *baseVD = getOpenMPReductionBaseVarDecl(varExpr);
        mlir::Value reductionVar =
            emitOpenMPReductionVarPointer(cgf, varExpr);
        if (!reductionVar)
          return;
        OpenMPReductionDeclInfo declInfo = getOrCreateOpenMPReductionDecl(
            cgf, builder,
            baseVD ? cgf.getLoc(baseVD->getLocation())
                   : cgf.getLoc(varExpr->getExprLoc()),
            clause, varExpr, privateExpr, lhsExpr, rhsExpr, reductionOp,
            reductionVar.getType(), /*allowByRef=*/true);
        if (!declInfo.decl)
          return;
        unsigned reductionIndex = reductionStartIdx++;
        reductionVars.push_back(reductionVar);
        reductionByRef.push_back(declInfo.byRef);
        reductionSyms.push_back(mlir::SymbolRefAttr::get(declInfo.decl));
        if (vd) {
          reductionRemapIndices.push_back(reductionIndex);
          reductionEmissionVars.push_back(vd);
        }
        if (declInfo.taskReductionSizeSym) {
          reductionRuntimeSizes.push_back(
              {reductionIndex, privateVD, vd,
               declInfo.runtimeElementCount});
        }
      }

      operation.getInReductionVarsMutable().append(reductionVars);
      llvm::append_range(emissionInfo.inReductionIndices,
                         reductionRemapIndices);
      llvm::append_range(emissionInfo.inReductionVars, reductionEmissionVars);
      llvm::append_range(emissionInfo.inReductionRuntimeSizes,
                         reductionRuntimeSizes);

      llvm::SmallVector<bool, 4> allReductionByRef;
      if (auto existingByRef = operation.getInReductionByref())
        llvm::append_range(allReductionByRef, *existingByRef);
      llvm::append_range(allReductionByRef, reductionByRef);
      operation.setInReductionByref(allReductionByRef);

      llvm::SmallVector<mlir::Attribute, 4> allReductionSyms;
      if (auto existingSyms = operation.getInReductionSyms())
        llvm::append_range(allReductionSyms, *existingSyms);
      llvm::append_range(allReductionSyms, reductionSyms);
      operation.setInReductionSymsAttr(
          mlir::ArrayAttr::get(builder.getContext(), allReductionSyms));
    } else {
      cgf.cgm.errorNYI(
          clause->getBeginLoc(),
          "OMPInReductionClause unimplemented on this directive kind");
    }
  }

  void VisitOMPNumThreadsClause(const OMPNumThreadsClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::ParallelOp>) {
      operation.getNumThreadsMutable().assign(
          emitOpenMPIntegerExpr(cgf, clause->getNumThreads()));
      switch (clause->getModifier()) {
      case OMPC_NUMTHREADS_unknown:
        break;
      case OMPC_NUMTHREADS_strict:
        // The source of truth for the
        // OpenMP 6.0 `num_threads(strict: ...)` modifier's lowered IR
        // effect now lives in a CIR-namespaced discardable attribute;
        // the OpenMP-to-LLVM translator reads
        // `cir.omp.num_threads_modifier` instead of the shared
        // `omp.parallel` TD operand. The TD operand is kept in sync so
        // the dialect verifier (which checks that severity implies
        // num_threads(strict)) still sees a consistent op.
        operation.setNumThreadsModifier(mlir::omp::NumThreadsModifier::strict);
        operation->setAttr(
            "cir.omp.num_threads_modifier",
            mlir::omp::NumThreadsModifierAttr::get(
                &cgf.getMLIRContext(), mlir::omp::NumThreadsModifier::strict));
        break;
      }
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPNumThreadsClause unimplemented on this directive "
                       "kind");
    }
  }

  void VisitOMPSeverityClause(const OMPSeverityClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::ParallelOp>) {
      mlir::omp::SeverityKind kind;
      switch (clause->getSeverityKind()) {
      case OMPC_SEVERITY_unknown:
        llvm_unreachable("unknown OpenMP severity kind");
      case OMPC_SEVERITY_warning:
        kind = mlir::omp::SeverityKind::warning;
        break;
      case OMPC_SEVERITY_fatal:
        kind = mlir::omp::SeverityKind::fatal;
        break;
      }
      // The source of truth for the
      // OpenMP 6.0 `severity` clause's lowered IR effect now lives in a
      // CIR-namespaced discardable attribute; the OpenMP-to-LLVM
      // translator reads `cir.omp.severity_kind` instead of the shared
      // `omp.parallel` TD operand. The TD operand is kept in sync so
      // the dialect verifier (which checks that severity implies
      // num_threads(strict)) still sees a consistent op.
      operation.setSeverityKind(kind);
      operation->setAttr(
          "cir.omp.severity_kind",
          mlir::omp::SeverityKindAttr::get(&cgf.getMLIRContext(), kind));
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPSeverityClause unimplemented on this directive "
                       "kind");
    }
  }

  void VisitOMPMessageClause(const OMPMessageClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::ParallelOp>) {
      mlir::Location loc = cgf.getLoc(clause->getBeginLoc());
      auto charPtrTy = builder.getPointerTo(builder.getSInt8Ty());
      mlir::Value message = cgf.emitScalarExpr(clause->getMessageString());
      if (message.getType() != charPtrTy)
        message = builder.createBitcast(loc, message, charPtrTy);
      // The OpenMP 6.0 `message`
      // clause is a CIR-only TD operand on the upstream-shared
      // `omp.parallel` op. Until the SSA-value carve-out (which
      // requires a CIR-emitted private-global SymbolRef indirection)
      // lands, assign the operand and stamp a CIR-namespaced marker
      // attribute so the OpenMP-to-LLVM translator can gate its
      // `getMessageVar()` read on a CIR-owned signal rather than
      // assuming upstream dataflow semantics. The TD operand is still
      // the data channel; the marker pins ownership.
      operation.getMessageVarMutable().assign(message);
      operation->setAttr("cir.omp.message_var_owned",
                         mlir::UnitAttr::get(&cgf.getMLIRContext()));
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPMessageClause unimplemented on this directive "
                       "kind");
    }
  }

  void VisitOMPNumTeamsClause(const OMPNumTeamsClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TeamsOp>) {
      operation.getNumTeamsUpperMutable().assign(
          emitOpenMPIntegerExpr(cgf, clause->getNumTeams().front()));
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPNumTeamsClause unimplemented on this directive "
                       "kind");
    }
  }

  void VisitOMPThreadLimitClause(const OMPThreadLimitClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TeamsOp> ||
                  std::is_same_v<OpTy, mlir::omp::TargetOp>) {
      operation.getThreadLimitMutable().assign(
          emitOpenMPIntegerExpr(cgf, clause->getThreadLimit().front()));
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPThreadLimitClause unimplemented on this directive "
                       "kind");
    }
  }

  void VisitOMPFirstprivateClause(const OMPFirstprivateClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::ParallelOp> ||
                  std::is_same_v<OpTy, mlir::omp::TeamsOp> ||
                  std::is_same_v<OpTy, mlir::omp::TargetOp> ||
                  std::is_same_v<OpTy, mlir::omp::TaskOp> ||
                  std::is_same_v<OpTy, mlir::omp::TaskloopOp> ||
                  std::is_same_v<OpTy, mlir::omp::SectionsOp> ||
                  std::is_same_v<OpTy, mlir::omp::WsloopOp>) {
      llvm::SmallVector<mlir::Value, 4> privateVars;
      llvm::SmallVector<mlir::Attribute, 4> privateSyms;
      privateVars.reserve(clause->varlist_size());
      privateSyms.reserve(clause->varlist_size());
      unsigned firstPrivateStartIdx = operation.getPrivateVars().size();

      llvm::SmallVector<const Expr *, 4> varExprs(clause->varlist());
      llvm::SmallVector<const Expr *, 4> privateCopyExprs(
          clause->private_copies());
      llvm::SmallVector<const Expr *, 4> initExprs(clause->inits());
      if constexpr (!std::is_same_v<OpTy, mlir::omp::TargetOp>) {
        std::reverse(varExprs.begin(), varExprs.end());
        std::reverse(privateCopyExprs.begin(), privateCopyExprs.end());
        std::reverse(initExprs.begin(), initExprs.end());
      }
      for (auto [varExpr, privateCopyExpr, initExpr] :
           llvm::zip_equal(varExprs, privateCopyExprs, initExprs)) {
        const VarDecl *vd = getOpenMPClauseVarDecl(varExpr);
        mlir::Value privateVar = cgf.emitLValue(varExpr).getPointer();
        auto pointerTy = mlir::dyn_cast<cir::PointerType>(privateVar.getType());
        if (!pointerTy) {
          cgf.cgm.errorNYI(vd->getLocation(),
                           "OpenMP parallel firstprivate non-pointer capture");
          return;
        }

        const VarDecl *privateCopyVD =
            getAssociatedPrivateCopyDecl(vd, privateCopyExpr);
        const VarDecl *initVD =
            cast<VarDecl>(cast<DeclRefExpr>(initExpr)->getDecl());

        if constexpr (std::is_same_v<OpTy, mlir::omp::TargetOp>) {
          if (auto it =
                  pendingTargetAllocatorPrivateVars.find(vd->getCanonicalDecl());
              it != pendingTargetAllocatorPrivateVars.end()) {
            emissionInfo.targetAllocatorPrivateVars.push_back(
                {vd, it->second.allocatorVar, privateCopyVD, initVD});
            pendingTargetAllocatorPrivateVars.erase(it);
          }
        }

        if (mlir::failed(maybeAppendOpenMPPrivateAllocator(
                operation, cgf, emissionInfo, vd, privateCopyVD, privateVar,
                initVD, /*initializeFromOriginal=*/true)))
          return;

        auto privatizer = getOrCreateOpenMPPrivatizer(
            cgf, builder, cgf.getLoc(vd->getLocation()), vd,
            pointerTy.getPointee(), privateVar.getType(),
            mlir::omp::DataSharingClauseType::FirstPrivate,
            privateCopyVD, initVD);
        privateVars.push_back(privateVar);
        privateSyms.push_back(mlir::SymbolRefAttr::get(privatizer));
        emissionInfo.privateIndices.push_back(firstPrivateStartIdx++);
        emissionInfo.privateVars.push_back(vd);
        if constexpr (std::is_same_v<OpTy, mlir::omp::TargetOp>) {
          unsigned targetMapFlags =
              clause->isImplicit()
                  ? static_cast<unsigned>(mlir::omp::ClauseMapFlags::implicit)
                  : 0;
          if (!clause->isImplicit() &&
              vd->getType().getNonReferenceType()->isAnyPointerType())
            targetMapFlags |=
                static_cast<unsigned>(mlir::omp::ClauseMapFlags::literal);
          emissionInfo.targetFirstprivateMapFlags[vd->getCanonicalDecl()] =
              targetMapFlags;
        }
      }

      operation.getPrivateVarsMutable().append(privateVars);

      llvm::SmallVector<mlir::Attribute, 4> allPrivateSyms;
      if (auto existingSyms = operation.getPrivateSyms())
        llvm::append_range(allPrivateSyms, *existingSyms);
      llvm::append_range(allPrivateSyms, privateSyms);
      operation.setPrivateSymsAttr(
          mlir::ArrayAttr::get(builder.getContext(), allPrivateSyms));
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPFirstprivateClause unimplemented on this directive "
                       "kind");
    }
  }

  void VisitOMPCopyinClause(const OMPCopyinClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::ParallelOp>) {
      // Copyin is lowered as an explicit outlined-region prologue in
      // CIRGenStmtOpenMP.cpp, matching OGCG's runtime-oriented sequencing.
      return;
    }

    cgf.cgm.errorNYI(clause->getBeginLoc(),
                     "OMPCopyinClause unimplemented on this directive kind");
  }

  void VisitOMPDefaultClause(const OMPDefaultClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::ParallelOp>) {
      if (clause->getDefaultVC() != OMPC_DEFAULT_VC_all) {
        cgf.cgm.errorNYI(clause->getBeginLoc(),
                         "OMPDefaultClause modifier on parallel directive");
        return;
      }

      switch (clause->getDefaultKind()) {
      case llvm::omp::OMP_DEFAULT_none:
      case llvm::omp::OMP_DEFAULT_shared:
        // These only constrain Sema's data-sharing checks. OGCG emits no
        // distinct runtime or IR change for the parallel op itself.
        return;
      case llvm::omp::OMP_DEFAULT_private:
      case llvm::omp::OMP_DEFAULT_firstprivate:
        cgf.cgm.errorNYI(clause->getBeginLoc(),
                         "OMPDefaultClause kind on parallel directive");
        return;
      case llvm::omp::OMP_DEFAULT_unknown:
        llvm_unreachable("unknown OpenMP default kind");
      }
    }

    cgf.cgm.errorNYI(clause->getBeginLoc(),
                     "OMPDefaultClause unimplemented on this directive kind");
  }

  void VisitOMPSharedClause(const OMPSharedClause *) {
    // The shared clause is a Sema-only data-sharing constraint. Variables
    // listed in shared() are captured by reference (the default), so no
    // additional codegen is required.
  }

  void VisitOMPPrivateClause(const OMPPrivateClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::ParallelOp> ||
                  std::is_same_v<OpTy, mlir::omp::WsloopOp> ||
                  std::is_same_v<OpTy, mlir::omp::SimdOp> ||
                  std::is_same_v<OpTy, mlir::omp::TeamsOp> ||
                  std::is_same_v<OpTy, mlir::omp::TargetOp> ||
                  std::is_same_v<OpTy, mlir::omp::TaskOp> ||
                  std::is_same_v<OpTy, mlir::omp::TaskloopOp> ||
                  std::is_same_v<OpTy, mlir::omp::SectionsOp>) {
      llvm::SmallVector<mlir::Value, 4> privateVars;
      llvm::SmallVector<mlir::Attribute, 4> privateSyms;
      privateVars.reserve(clause->varlist_size());
      privateSyms.reserve(clause->varlist_size());
      unsigned privateStartIdx = operation.getPrivateVars().size();

      for (auto [varExpr, privateCopyExpr] :
           llvm::zip_equal(clause->varlist(), clause->private_copies())) {
        const VarDecl *vd = getOpenMPClauseVarDecl(varExpr);
        mlir::Value privateVar = cgf.emitLValue(varExpr).getPointer();
        auto pointerTy = mlir::dyn_cast<cir::PointerType>(privateVar.getType());
        if (!pointerTy) {
          cgf.cgm.errorNYI(vd->getLocation(),
                           "OpenMP private non-pointer capture");
          return;
        }

        const VarDecl *privateCopyVD =
            cast<VarDecl>(cast<DeclRefExpr>(privateCopyExpr)->getDecl());

        if constexpr (std::is_same_v<OpTy, mlir::omp::TargetOp>) {
          if (auto it =
                  pendingTargetAllocatorPrivateVars.find(vd->getCanonicalDecl());
              it != pendingTargetAllocatorPrivateVars.end()) {
            emissionInfo.targetAllocatorPrivateVars.push_back(
                {vd, it->second.allocatorVar, nullptr, nullptr});
            pendingTargetAllocatorPrivateVars.erase(it);
            continue;
          }
        }

        if (mlir::failed(maybeAppendOpenMPPrivateAllocator(
                operation, cgf, emissionInfo, vd, privateCopyVD, privateVar)))
          return;

        auto privatizer = getOrCreateOpenMPPrivatizer(
            cgf, builder, cgf.getLoc(vd->getLocation()), vd,
            pointerTy.getPointee(), privateVar.getType(),
            mlir::omp::DataSharingClauseType::Private);
        privateVars.push_back(privateVar);
        privateSyms.push_back(mlir::SymbolRefAttr::get(privatizer));
        emissionInfo.privateIndices.push_back(privateStartIdx++);
        emissionInfo.privateVars.push_back(vd);
      }

      operation.getPrivateVarsMutable().append(privateVars);

      llvm::SmallVector<mlir::Attribute, 4> allPrivateSyms;
      if (auto existingSyms = operation.getPrivateSyms())
        llvm::append_range(allPrivateSyms, *existingSyms);
      llvm::append_range(allPrivateSyms, privateSyms);
      operation.setPrivateSymsAttr(
          mlir::ArrayAttr::get(builder.getContext(), allPrivateSyms));
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPPrivateClause unimplemented on this directive kind");
    }
  }

  void VisitOMPLinearClause(const OMPLinearClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::SimdOp> ||
                  std::is_same_v<OpTy, mlir::omp::WsloopOp>) {
      if (clause->getModifier() != OMPC_LINEAR_val &&
          clause->getModifier() != OMPC_LINEAR_uval) {
        cgf.cgm.errorNYI(clause->getBeginLoc(),
                         "OMPLinearClause modifier on this directive kind");
        return;
      }

      mlir::Value linearStep = emitOpenMPLinearStepExpr(cgf, clause->getStep());
      if (!linearStep)
        return;

      llvm::SmallVector<mlir::Value, 4> linearVars;
      llvm::SmallVector<mlir::Value, 4> linearStepVars;
      llvm::SmallVector<mlir::Attribute, 4> linearVarTypes;
      linearVars.reserve(clause->varlist_size());
      linearStepVars.reserve(clause->varlist_size());
      linearVarTypes.reserve(clause->varlist_size());

      for (const Expr *varExpr : clause->varlist()) {
        const VarDecl *vd = getOpenMPClauseVarDecl(varExpr);
        QualType linearTy = vd->getType().getNonReferenceType();
        if (!linearTy->isIntegerType() && !linearTy->isEnumeralType() &&
            !linearTy->isRealFloatingType() && !linearTy->isPointerType()) {
          cgf.cgm.errorNYI(vd->getLocation(),
                           "OMPLinearClause variable type on this directive "
                           "kind");
          return;
        }

        mlir::Value linearVar = cgf.emitLValue(varExpr).getPointer();
        auto pointerTy = mlir::dyn_cast<cir::PointerType>(linearVar.getType());
        if (!pointerTy) {
          cgf.cgm.errorNYI(vd->getLocation(),
                           "OMPLinearClause non-pointer capture");
          return;
        }

        mlir::Value adjustedLinearStep = scaleOpenMPLinearStepValue(
            cgf, linearVar.getLoc(), linearStep, linearTy);
        if (!adjustedLinearStep)
          return;

        linearVars.push_back(linearVar);
        linearStepVars.push_back(adjustedLinearStep);
        mlir::Type linearVarType = getOpenMPLinearVarType(cgf, linearTy);
        if (!linearVarType) {
          cgf.cgm.errorNYI(vd->getLocation(),
                           "OMPLinearClause variable type translation on this "
                           "directive kind");
          return;
        }
        linearVarTypes.push_back(mlir::TypeAttr::get(linearVarType));
      }

      operation.getLinearVarsMutable().append(linearVars);
      operation.getLinearStepVarsMutable().append(linearStepVars);

      llvm::SmallVector<mlir::Attribute, 4> allLinearVarTypes;
      if (auto existingTypes = operation.getLinearVarTypes())
        llvm::append_range(allLinearVarTypes, *existingTypes);
      llvm::append_range(allLinearVarTypes, linearVarTypes);
      operation.setLinearVarTypesAttr(
          mlir::ArrayAttr::get(builder.getContext(), allLinearVarTypes));
      return;
    }

    cgf.cgm.errorNYI(clause->getBeginLoc(),
                     "OMPLinearClause unimplemented on this directive kind");
  }

  void VisitOMPAlignedClause(const OMPAlignedClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::SimdOp>) {
      llvm::SmallVector<mlir::Value, 4> alignedVars;
      llvm::SmallVector<mlir::Attribute, 4> alignments;
      alignedVars.reserve(clause->varlist_size());
      alignments.reserve(clause->varlist_size());

      int64_t clauseAlignment = 0;
      if (const Expr *alignmentExpr = clause->getAlignment()) {
        clauseAlignment =
            alignmentExpr->EvaluateKnownConstInt(cgf.getContext())
                .getSExtValue();
      }

      for (const Expr *varExpr : clause->varlist()) {
        mlir::Value alignedVar = cgf.emitScalarExpr(varExpr);
        auto pointerTy = mlir::dyn_cast<cir::PointerType>(alignedVar.getType());
        if (!pointerTy) {
          const VarDecl *vd = getOpenMPClauseVarDecl(varExpr);
          cgf.cgm.errorNYI(vd->getLocation(),
                           "OMPAlignedClause non-pointer capture");
          return;
        }

        int64_t alignment = clauseAlignment;
        if (alignment == 0) {
          QualType pointeeTy{varExpr->getType()->getPointeeOrArrayElementType(),
                             0};
          alignment = cgf.getContext()
                          .toCharUnitsFromBits(
                              cgf.getContext().getOpenMPDefaultSimdAlign(
                                  pointeeTy))
                          .getQuantity();
        }

        alignedVars.push_back(alignedVar);
        alignments.push_back(builder.getI64IntegerAttr(alignment));
      }

      operation.getAlignedVarsMutable().append(alignedVars);

      llvm::SmallVector<mlir::Attribute, 4> allAlignments;
      if (auto existingAlignments = operation.getAlignments())
        llvm::append_range(allAlignments, *existingAlignments);
      llvm::append_range(allAlignments, alignments);
      operation.setAlignmentsAttr(
          mlir::ArrayAttr::get(builder.getContext(), allAlignments));
      return;
    }

    cgf.cgm.errorNYI(clause->getBeginLoc(),
                     "OMPAlignedClause unimplemented on this directive kind");
  }

  void VisitOMPNontemporalClause(const OMPNontemporalClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::SimdOp>) {
      llvm::SmallVector<mlir::Value, 4> nontemporalVars;
      nontemporalVars.reserve(clause->varlist_size());

      for (const Stmt *ref : clause->private_refs()) {
        const auto *varExpr = cast<Expr>(ref);
        mlir::Value nontemporalVar = cgf.emitLValue(varExpr).getPointer();
        auto pointerTy =
            mlir::dyn_cast<cir::PointerType>(nontemporalVar.getType());
        if (!pointerTy) {
          cgf.cgm.errorNYI(varExpr->getExprLoc(),
                           "OMPNontemporalClause non-pointer capture");
          return;
        }

        nontemporalVars.push_back(nontemporalVar);
      }

      operation.getNontemporalVarsMutable().append(nontemporalVars);
      return;
    }

    cgf.cgm.errorNYI(
        clause->getBeginLoc(),
        "OMPNontemporalClause unimplemented on this directive kind");
  }

  void VisitOMPOrderClause(const OMPOrderClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::SimdOp> ||
                  std::is_same_v<OpTy, mlir::omp::WsloopOp>) {
      switch (clause->getKind()) {
      case OMPC_ORDER_concurrent:
        operation.setOrderAttr(mlir::omp::ClauseOrderKindAttr::get(
            builder.getContext(), mlir::omp::ClauseOrderKind::Concurrent));
        break;
      case OMPC_ORDER_unknown:
        llvm_unreachable("unknown OpenMP order kind");
      }

      switch (clause->getModifier()) {
      case OMPC_ORDER_MODIFIER_unknown:
        break;
      case OMPC_ORDER_MODIFIER_reproducible:
        operation.setOrderModAttr(mlir::omp::OrderModifierAttr::get(
            builder.getContext(), mlir::omp::OrderModifier::reproducible));
        break;
      case OMPC_ORDER_MODIFIER_unconstrained:
        operation.setOrderModAttr(mlir::omp::OrderModifierAttr::get(
            builder.getContext(), mlir::omp::OrderModifier::unconstrained));
        break;
      case OMPC_ORDER_MODIFIER_last:
        llvm_unreachable("unexpected OpenMP order modifier sentinel");
      }
      return;
    }

    cgf.cgm.errorNYI(clause->getBeginLoc(),
                     "OMPOrderClause unimplemented on this directive kind");
  }

  void VisitOMPSafelenClause(const OMPSafelenClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::SimdOp>) {
      int64_t safelen =
          clause->getSafelen()->EvaluateKnownConstInt(cgf.getContext())
              .getSExtValue();
      operation.setSafelenAttr(builder.getI64IntegerAttr(safelen));
      return;
    }

    cgf.cgm.errorNYI(clause->getBeginLoc(),
                     "OMPSafelenClause unimplemented on this directive kind");
  }

  void VisitOMPSimdlenClause(const OMPSimdlenClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::SimdOp>) {
      int64_t simdlen =
          clause->getSimdlen()->EvaluateKnownConstInt(cgf.getContext())
              .getSExtValue();
      operation.setSimdlenAttr(builder.getI64IntegerAttr(simdlen));
      return;
    }

    cgf.cgm.errorNYI(clause->getBeginLoc(),
                     "OMPSimdlenClause unimplemented on this directive kind");
  }

  void VisitOMPLastprivateClause(const OMPLastprivateClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TargetOp>) {
      llvm::SmallVector<mlir::Value, 4> privateVars;
      llvm::SmallVector<mlir::Attribute, 4> privateSyms;
      privateVars.reserve(clause->varlist_size());
      privateSyms.reserve(clause->varlist_size());
      unsigned privateStartIdx = operation.getPrivateVars().size();

      llvm::SmallVector<const Expr *, 4> varExprs(clause->varlist());
      std::reverse(varExprs.begin(), varExprs.end());
      for (const Expr *varExpr : varExprs) {
        const VarDecl *vd = getOpenMPClauseVarDecl(varExpr);
        if (isEnclosingFirstprivateVar(vd))
          continue;

        mlir::Value privateVar = cgf.emitLValue(varExpr).getPointer();
        auto pointerTy = mlir::dyn_cast<cir::PointerType>(privateVar.getType());
        if (!pointerTy) {
          cgf.cgm.errorNYI(vd->getLocation(),
                           "OpenMP target lastprivate non-pointer capture");
          return;
        }

        auto privatizer = getOrCreateOpenMPPrivatizer(
            cgf, builder, cgf.getLoc(vd->getLocation()), vd,
            pointerTy.getPointee(), privateVar.getType(),
            mlir::omp::DataSharingClauseType::FirstPrivate);
        privateVars.push_back(privateVar);
        privateSyms.push_back(mlir::SymbolRefAttr::get(privatizer));
        emissionInfo.privateIndices.push_back(privateStartIdx++);
        emissionInfo.privateVars.push_back(vd);
      }

      operation.getPrivateVarsMutable().append(privateVars);

      llvm::SmallVector<mlir::Attribute, 4> allPrivateSyms;
      if (auto existingSyms = operation.getPrivateSyms())
        llvm::append_range(allPrivateSyms, *existingSyms);
      llvm::append_range(allPrivateSyms, privateSyms);
      operation.setPrivateSymsAttr(
          mlir::ArrayAttr::get(builder.getContext(), allPrivateSyms));
    } else if constexpr (std::is_same_v<OpTy, mlir::omp::WsloopOp> ||
                         std::is_same_v<OpTy, mlir::omp::SimdOp> ||
                         std::is_same_v<OpTy, mlir::omp::DistributeOp> ||
                         std::is_same_v<OpTy, mlir::omp::TaskloopOp>) {
      if constexpr (std::is_same_v<OpTy, mlir::omp::WsloopOp>) {
        if (clause->getKind() == OMPC_LASTPRIVATE_conditional &&
            !allowWsloopConditionalLastprivate) {
          cgf.cgm.errorNYI(clause->getBeginLoc(),
                           "OpenMP conditional lastprivate on parallel for");
          return;
        }
      } else if constexpr (std::is_same_v<OpTy, mlir::omp::DistributeOp>) {
        if (clause->getKind() == OMPC_LASTPRIVATE_conditional) {
          cgf.cgm.errorNYI(clause->getBeginLoc(),
                           "OpenMP conditional lastprivate on distribute");
          return;
        }
      } else if constexpr (std::is_same_v<OpTy, mlir::omp::TaskloopOp>) {
        if (clause->getKind() == OMPC_LASTPRIVATE_conditional) {
          cgf.cgm.errorNYI(clause->getBeginLoc(),
                           "OpenMP conditional lastprivate on taskloop");
          return;
        }
      }

      llvm::SmallVector<mlir::Value, 4> privateVars;
      llvm::SmallVector<mlir::Attribute, 4> privateSyms;
      privateVars.reserve(clause->varlist_size());
      privateSyms.reserve(clause->varlist_size());
      unsigned privateStartIdx = operation.getPrivateVars().size();

      auto srcIt = clause->source_exprs().begin();
      auto destIt = clause->destination_exprs().begin();
      auto assignIt = clause->assignment_ops().begin();
      auto privateCopyIt = clause->private_copies().begin();
      for (const Expr *varExpr : clause->varlist()) {
        const VarDecl *vd = getOpenMPClauseVarDecl(varExpr);
        mlir::Value privateVar = cgf.emitLValue(varExpr).getPointer();
        auto pointerTy = mlir::dyn_cast<cir::PointerType>(privateVar.getType());
        if (!pointerTy) {
          cgf.cgm.errorNYI(
              vd->getLocation(),
              std::is_same_v<OpTy, mlir::omp::WsloopOp>
                  ? "OpenMP parallel for lastprivate non-pointer capture"
                  : (std::is_same_v<OpTy, mlir::omp::SimdOp>
                         ? "OpenMP simd lastprivate non-pointer capture"
                         : (std::is_same_v<OpTy, mlir::omp::DistributeOp>
                                ? "OpenMP distribute lastprivate non-pointer "
                                  "capture"
                                : "OpenMP taskloop lastprivate non-pointer "
                                  "capture")));
          return;
        }

        const VarDecl *privateCopyVD =
            getAssociatedPrivateCopyDecl(vd, *privateCopyIt);
        bool isFirstprivate = isEnclosingFirstprivateVar(vd);
        if (mlir::failed(maybeAppendOpenMPPrivateAllocator(
                operation, cgf, emissionInfo, vd, privateCopyVD, privateVar,
                /*initVD=*/nullptr, isFirstprivate)))
          return;

        auto privatizer = getOrCreateOpenMPPrivatizer(
            cgf, builder, cgf.getLoc(vd->getLocation()), vd,
            pointerTy.getPointee(), privateVar.getType(),
            isFirstprivate ? mlir::omp::DataSharingClauseType::FirstPrivate
                           : mlir::omp::DataSharingClauseType::Private);
        privateVars.push_back(privateVar);
        privateSyms.push_back(mlir::SymbolRefAttr::get(privatizer));
        emissionInfo.privateIndices.push_back(privateStartIdx);
        emissionInfo.privateVars.push_back(vd);
        emissionInfo.lastprivateIndices.push_back(privateStartIdx++);
        emissionInfo.lastprivateVars.push_back(vd);
        emissionInfo.lastprivateSourceVDs.push_back(
            cast<VarDecl>(cast<DeclRefExpr>(*srcIt)->getDecl()));
        emissionInfo.lastprivateDestVDs.push_back(
            cast<VarDecl>(cast<DeclRefExpr>(*destIt)->getDecl()));
        emissionInfo.lastprivateAssignmentOps.push_back(*assignIt);
        emissionInfo.lastprivateConditionals.push_back(
            clause->getKind() == OMPC_LASTPRIVATE_conditional);
        ++privateCopyIt;
        ++srcIt;
        ++destIt;
        ++assignIt;
        if constexpr (std::is_same_v<OpTy, mlir::omp::WsloopOp>) {
          if (isFirstprivate)
            operation.setPrivateNeedsBarrier(true);
        }
      }

      operation.getPrivateVarsMutable().append(privateVars);

      llvm::SmallVector<mlir::Attribute, 4> allPrivateSyms;
      if (auto existingSyms = operation.getPrivateSyms())
        llvm::append_range(allPrivateSyms, *existingSyms);
      llvm::append_range(allPrivateSyms, privateSyms);
      operation.setPrivateSymsAttr(
          mlir::ArrayAttr::get(builder.getContext(), allPrivateSyms));
    } else {
      cgf.cgm.errorNYI(
          clause->getBeginLoc(),
          "OMPLastprivateClause unimplemented on this directive kind");
    }
  }

  void VisitOMPScheduleClause(const OMPScheduleClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::WsloopOp>) {
      operation.setScheduleKind(getOpenMPScheduleKind(clause));
      // Some nested target lowerings precompute a region-local chunk value
      // before entering omp.parallel. Preserve that override here.
      if (const Expr *chunk = clause->getChunkSize();
          chunk && !operation.getScheduleChunk())
        operation.getScheduleChunkMutable().assign(
            emitOpenMPIntegerExpr(
                cgf, unwrapOpenMPClauseCapturedExprInit(chunk)));

      auto setScheduleModifier = [&](OpenMPScheduleClauseModifier modifier) {
        switch (modifier) {
        case OMPC_SCHEDULE_MODIFIER_unknown:
          return;
        case OMPC_SCHEDULE_MODIFIER_monotonic:
          operation.setScheduleMod(mlir::omp::ScheduleModifier::monotonic);
          return;
        case OMPC_SCHEDULE_MODIFIER_nonmonotonic:
          operation.setScheduleMod(mlir::omp::ScheduleModifier::nonmonotonic);
          return;
        case OMPC_SCHEDULE_MODIFIER_simd:
          operation.setScheduleSimd(true);
          return;
        case OMPC_SCHEDULE_MODIFIER_last:
          llvm_unreachable("unexpected OpenMP schedule modifier");
        }
        llvm_unreachable("unexpected OpenMP schedule modifier");
      };

      setScheduleModifier(clause->getFirstScheduleModifier());
      setScheduleModifier(clause->getSecondScheduleModifier());
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPScheduleClause unimplemented on this directive "
                       "kind");
    }
  }

  void VisitOMPReductionClause(const OMPReductionClause *clause) {
    if constexpr (std::is_same_v<OpTy, mlir::omp::TargetOp>) {
      llvm::SmallVector<mlir::Value, 4> mapVars;
      mapVars.reserve(clause->varlist_size());
      unsigned mapStartIdx = operation.getMapVars().size();

      llvm::SmallPtrSet<const VarDecl *, 4> mappedVars;
      auto markMappedVars = [&](auto &&vars) {
        for (const VarDecl *vd : vars)
          mappedVars.insert(vd->getCanonicalDecl());
      };
      markMappedVars(emissionInfo.hasDeviceAddrVars);
      markMappedVars(emissionInfo.mapVars);
      markMappedVars(emissionInfo.privateVars);

      for (const Expr *varExpr : clause->varlist()) {
        if (!isOpenMPReductionVarExpr(varExpr)) {
          cgf.cgm.errorNYI(varExpr->getExprLoc(),
                           "OpenMP reduction on non-variable expression "
                           "(e.g. array element)");
          return;
        }

        const VarDecl *vd = getOpenMPReductionBaseVarDecl(varExpr);
        if (!vd) {
          cgf.cgm.errorNYI(clause->getBeginLoc(),
                           "OpenMP target reduction capture without directly "
                           "remappable base variable");
          return;
        }
        if (!mappedVars.insert(vd->getCanonicalDecl()).second)
          continue;

        std::optional<OpenMPMapOperandInfo> mapInfo = getOpenMPMapOperandInfo(
            cgf, varExpr,
            /*usePointerValueForBarePointerExpr=*/false,
            /*captureBoundedPointerSource=*/true);
        if (!mapInfo)
          return;

        auto mapInfoOp = createOpenMPMapInfoOp(
            builder, *mapInfo,
            mlir::omp::ClauseMapFlags::to | mlir::omp::ClauseMapFlags::from,
            mlir::FlatSymbolRefAttr{});
        mapVars.push_back(mapInfoOp.getResult());
        emissionInfo.mapIndices.push_back(mapStartIdx + mapVars.size() - 1);
        emissionInfo.mapVars.push_back(vd);
      }

      operation.getMapVarsMutable().append(mapVars);
    } else if constexpr (std::is_same_v<OpTy, mlir::omp::ParallelOp> ||
                  std::is_same_v<OpTy, mlir::omp::WsloopOp> ||
                  std::is_same_v<OpTy, mlir::omp::TeamsOp> ||
                  std::is_same_v<OpTy, mlir::omp::TaskloopOp> ||
                  std::is_same_v<OpTy, mlir::omp::SimdOp> ||
                  std::is_same_v<OpTy, mlir::omp::SectionsOp> ||
                  std::is_same_v<OpTy, cir::OmpScopeReductionOp>) {
      mlir::omp::ReductionModifierAttr reductionMod =
          getOpenMPReductionModifierAttr(cgf, clause, clause->getModifier());
      if constexpr (std::is_same_v<OpTy, mlir::omp::TeamsOp>) {
        if (reductionMod) {
          cgf.cgm.errorNYI(clause->getBeginLoc(),
                           "OpenMP reduction modifier on parallel for");
          return;
        }
      } else if (reductionMod) {
        // `cir::OmpScopeReductionOp::getReductionModAttr()` returns the
        // payload as a generic `mlir::Attribute` (the CIR op stores it
        // as `OptionalAttr<AnyAttr>` so its tablegen does not depend on
        // OpenMPEnums.td). Re-cast for the equality check; the upstream
        // op kinds return `omp::ReductionModifierAttr` directly.
        if (auto existingModRaw = operation.getReductionModAttr()) {
          mlir::omp::ReductionModifierAttr existingMod;
          if constexpr (std::is_same_v<OpTy, cir::OmpScopeReductionOp>)
            existingMod =
                mlir::dyn_cast<mlir::omp::ReductionModifierAttr>(existingModRaw);
          else
            existingMod = existingModRaw;
          assert(existingMod && existingMod.getValue() == reductionMod.getValue() &&
                 "conflicting OpenMP reduction modifiers");
          (void)existingMod;
        } else {
          operation->setAttr("reduction_mod", reductionMod);
        }
      } else if (clause->getModifier() != OMPC_REDUCTION_unknown &&
                 clause->getModifier() != OMPC_REDUCTION_default) {
        cgf.cgm.errorNYI(clause->getBeginLoc(),
                         "OpenMP reduction modifier on parallel for");
        return;
      }

      llvm::SmallVector<mlir::Value, 4> reductionVars;
      llvm::SmallVector<bool, 4> reductionByRef;
      llvm::SmallVector<mlir::Attribute, 4> reductionSyms;
      llvm::SmallVector<unsigned, 4> reductionRemapIndices;
      llvm::SmallVector<const VarDecl *, 4> reductionEmissionVars;
      llvm::SmallVector<CIRGenFunction::OpenMPTaskReductionRuntimeSizeInfo, 4>
          reductionRuntimeSizes;
      reductionVars.reserve(clause->varlist_size());
      reductionByRef.reserve(clause->varlist_size());
      reductionSyms.reserve(clause->varlist_size());
      reductionRemapIndices.reserve(clause->varlist_size());
      reductionEmissionVars.reserve(clause->varlist_size());
      reductionRuntimeSizes.reserve(clause->varlist_size());
      llvm::SmallVector<
          CIRGenFunction::OpenMPClauseEmissionInfo::OpenMPReductionSectionInfo,
          4>
          reductionSections;
      unsigned reductionStartIdx = operation.getReductionVars().size();

      for (auto [varExpr, privateExpr, lhsExpr, rhsExpr, reductionOp] :
           llvm::zip_equal(clause->varlist(), clause->privates(),
                           clause->lhs_exprs(), clause->rhs_exprs(),
                           clause->reduction_ops())) {
        if (!isOpenMPReductionVarExpr(varExpr)) {
          cgf.cgm.errorNYI(varExpr->getExprLoc(),
                           "OpenMP reduction on non-variable expression "
                           "(e.g. array element)");
          return;
        }
        const VarDecl *vd =
            isa<DeclRefExpr>(varExpr->IgnoreImpCasts())
                ? getOpenMPClauseVarDecl(varExpr)
                : nullptr;
        const VarDecl *privateVD = getOpenMPClauseVarDecl(privateExpr);
        const VarDecl *baseVD = getOpenMPReductionBaseVarDecl(varExpr);
        mlir::Value reductionVar =
            emitOpenMPReductionVarPointer(cgf, varExpr);
        if (!reductionVar)
          return;
        QualType reductionVarTy =
            varExpr->getType().getNonReferenceType();
        bool useTaskReductionRuntimeDescriptor =
            mlir::isa<mlir::omp::TaskloopOp>(operation.getOperation()) ||
            reductionVarTy->isVariablyModifiedType();
        OpenMPReductionDeclInfo declInfo = getOrCreateOpenMPReductionDecl(
            cgf, builder,
            baseVD ? cgf.getLoc(baseVD->getLocation())
                   : cgf.getLoc(varExpr->getExprLoc()),
            clause, varExpr, privateExpr, lhsExpr, rhsExpr, reductionOp,
            reductionVar.getType(), /*allowByRef=*/true,
            useTaskReductionRuntimeDescriptor);
        if (!declInfo.decl)
          return;
        unsigned reductionIndex = reductionStartIdx++;
        reductionVars.push_back(reductionVar);
        reductionByRef.push_back(declInfo.byRef);
        reductionSyms.push_back(mlir::SymbolRefAttr::get(declInfo.decl));
        if (vd) {
          reductionRemapIndices.push_back(reductionIndex);
          reductionEmissionVars.push_back(vd);
        } else if (baseVD) {
          if (mlir::Value offset =
                  emitOpenMPReductionSectionOffset(cgf, varExpr, reductionVar))
            reductionSections.push_back({reductionIndex, baseVD, offset});
        }
        if (declInfo.taskReductionSizeSym) {
          reductionRuntimeSizes.push_back(
              {reductionIndex, privateVD, vd,
               declInfo.runtimeElementCount});
        }
      }

      operation.getReductionVarsMutable().append(reductionVars);
      llvm::append_range(emissionInfo.reductionIndices,
                         reductionRemapIndices);
      llvm::append_range(emissionInfo.reductionVars, reductionEmissionVars);
      llvm::append_range(emissionInfo.reductionSections, reductionSections);
      llvm::append_range(emissionInfo.reductionRuntimeSizes,
                         reductionRuntimeSizes);

      llvm::SmallVector<bool, 4> allReductionByRef;
      if (auto existingByRef = operation.getReductionByref())
        llvm::append_range(allReductionByRef, *existingByRef);
      llvm::append_range(allReductionByRef, reductionByRef);
      operation.setReductionByref(
          mlir::DenseBoolArrayAttr::get(builder.getContext(), allReductionByRef));

      llvm::SmallVector<mlir::Attribute, 4> allReductionSyms;
      if (auto existingSyms = operation.getReductionSyms())
        llvm::append_range(allReductionSyms, *existingSyms);
      llvm::append_range(allReductionSyms, reductionSyms);
      operation.setReductionSymsAttr(
          mlir::ArrayAttr::get(builder.getContext(), allReductionSyms));
    } else {
      cgf.cgm.errorNYI(clause->getBeginLoc(),
                       "OMPReductionClause unimplemented on this directive "
                       "kind");
    }
  }

  void emitClauses(ArrayRef<const OMPClause *> clauses) {
    preScanPrivateCopyDecls(clauses);
    preScanTargetAllocatorPrivates(clauses);
    preScanRuntimeDependPayload(clauses);
    for (const auto *c : clauses)
      this->Visit(c);
    for (const auto &[_, info] : pendingTargetAllocatorPrivateVars) {
      cgf.cgm.errorNYI(info.clause->getBeginLoc(),
                       "OMPAllocateClause target uses_allocators allocator on "
                       "non-private/firstprivate variable");
    }
  }

  CIRGenFunction::OpenMPClauseEmissionInfo takeEmissionInfo() {
    return std::move(emissionInfo);
  }
};
template <typename OpTy>
auto makeClauseEmitter(OpTy &op, CIRGen::CIRGenFunction &cgf,
                       CIRGen::CIRGenBuilderTy &builder,
                       ArrayRef<const VarDecl *> enclosingFirstprivateVars,
                       bool allowWsloopConditionalLastprivate) {
  return OpenMPClauseCIREmitter<OpTy>(op, cgf, builder,
                                      enclosingFirstprivateVars,
                                      allowWsloopConditionalLastprivate);
}
} // namespace

mlir::FlatSymbolRefAttr
CIRGenModule::getOrCreateOpenMPDeclareMapper(const OMPDeclareMapperDecl *d,
                                             mlir::Location loc) {
  if (auto it = openMPDeclareMappers.find(d); it != openMPDeclareMappers.end())
    return it->second;

  std::string typeName;
  llvm::raw_string_ostream typeNameOS(typeName);
  getCXXABI().getMangleContext().mangleCanonicalTypeName(d->getType(),
                                                         typeNameOS, false);
  typeNameOS.flush();

  std::string mapperId;
  llvm::raw_string_ostream mapperIdOS(mapperId);
  d->printName(mapperIdOS);
  mapperIdOS.flush();

  std::string baseName = "omp_mapper." + typeName + "." + mapperId;
  std::string symbolName = baseName;
  unsigned suffix = 0;
  while (mlir::Operation *existing = getGlobalValue(symbolName)) {
    if (auto mapper = mlir::dyn_cast<mlir::omp::DeclareMapperOp>(existing)) {
      auto symbol = mlir::FlatSymbolRefAttr::get(mapper.getSymNameAttr());
      openMPDeclareMappers.try_emplace(d, symbol);
      return symbol;
    }
    symbolName = (baseName + "." + llvm::Twine(++suffix)).str();
  }

  mlir::Type mapperTy = getTypes().convertTypeForMem(d->getType());
  auto mapperPtrTy = builder.getPointerTo(mapperTy);

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(getModule().getBody());
  auto mapperDecl =
      mlir::omp::DeclareMapperOp::create(builder, loc, symbolName, mapperTy);
  insertGlobalSymbol(mapperDecl);
  auto symbol = mlir::FlatSymbolRefAttr::get(mapperDecl.getSymNameAttr());
  openMPDeclareMappers.try_emplace(d, symbol);

  mlir::Block *body = builder.createBlock(&mapperDecl.getRegion());
  mlir::Value mapperArg = body->addArgument(mapperPtrTy, loc);
  builder.setInsertionPointToStart(body);

  CIRGenFunction mapperCGF(*this, builder, /*suppressNewContext=*/true);
  CIRGenFunction::SourceLocRAIIObject mapperLoc(mapperCGF, loc);
  CIRGenFunction::SymTableScopeTy mapperScope(mapperCGF.symbolTable);

  const auto *mapperVD = cast<VarDecl>(cast<DeclRefExpr>(
      d->getMapperVarRef()->IgnoreParenImpCasts())->getDecl());
  mapperCGF.setAddrOfLocalVar(mapperVD,
                              Address(mapperArg, mapperTy,
                                      getASTContext().getDeclAlign(mapperVD)));

  llvm::SmallVector<mlir::Value, 4> childMaps;
  llvm::SmallVector<llvm::SmallVector<int64_t, 4>, 4> childMemberIndices;
  bool sawMemberMap = false;
  bool sawDirectMap = false;

  for (const OMPClause *clause : d->clauselists()) {
    const auto *mapClause = dyn_cast<OMPMapClause>(clause);
    if (!mapClause) {
      errorNYI(d->getSourceRange(), "OpenMP declare mapper clauses");
      return symbol;
    }

    std::optional<mlir::omp::ClauseMapFlags> mapFlags =
        getOpenMPMapClauseFlags(mapperCGF, mapClause,
                                /*allowMapperModifier=*/true);
    if (!mapFlags)
      return symbol;

    auto mapperIt = mapClause->mapperlist_begin();
    for (const Expr *varExpr : mapClause->varlist()) {
      std::optional<OpenMPMapOperandInfo> mapInfo =
          getOpenMPMapOperandInfo(mapperCGF, varExpr);
      if (!mapInfo)
        return symbol;

      mlir::FlatSymbolRefAttr nestedMapper =
          getOpenMPMapperId(mapperCGF, *mapperIt, mapClause->getBeginLoc());
      auto mapInfoOp =
          createOpenMPMapInfoOp(builder, *mapInfo, *mapFlags, nestedMapper);
      childMaps.push_back(mapInfoOp.getResult());

      if (mapInfo->memberIndices.empty())
        sawDirectMap = true;
      else {
        sawMemberMap = true;
        childMemberIndices.push_back(mapInfo->memberIndices);
      }

      ++mapperIt;
    }
  }

  if (sawMemberMap && sawDirectMap) {
    errorNYI(d->getSourceRange(),
             "OpenMP declare mapper mixed direct and member mappings");
    return symbol;
  }

  llvm::SmallVector<mlir::Value, 4> mapperMaps;
  if (sawMemberMap) {
    OpenMPMapOperandInfo parentMap{loc,
                                   mapperArg,
                                   mlir::Value{},
                                   mapperTy,
                                   mapperVD,
                                   mapperVD->getName().str(),
                                   {},
                                   {}};
    auto parentMapInfo = createOpenMPMapInfoOp(
        builder, parentMap, mlir::omp::ClauseMapFlags::implicit,
        mlir::FlatSymbolRefAttr{}, childMaps,
        createOpenMPMembersIndexAttr(builder, childMemberIndices),
        /*partialMap=*/true);
    mapperMaps.push_back(parentMapInfo.getResult());
    llvm::append_range(mapperMaps, childMaps);
  } else {
    llvm::append_range(mapperMaps, childMaps);
  }

  mlir::omp::DeclareMapperInfoOp::create(builder, loc, mapperMaps);
  return symbol;
}

template <typename Op>
CIRGenFunction::OpenMPClauseEmissionInfo
CIRGenFunction::emitOpenMPClauses(
    Op &op, ArrayRef<const OMPClause *> clauses,
    ArrayRef<const VarDecl *> enclosingFirstprivateVars,
    bool allowWsloopConditionalLastprivate) {
  mlir::OpBuilder::InsertionGuard guardCase(builder);
  builder.setInsertionPoint(op);
  auto emitter = makeClauseEmitter(op, *this, builder,
                                   enclosingFirstprivateVars,
                                   allowWsloopConditionalLastprivate);
  emitter.emitClauses(clauses);
  OpenMPClauseEmissionInfo emissionInfo = emitter.takeEmissionInfo();
  emitOpenMPReductionRuntimeSizeStores(*this, op, emissionInfo);
  return emissionInfo;
}

// We're defining the template for this in a .cpp file, so we have to explicitly
// specialize the templates.
#define EXPL_SPEC(N)                                                           \
  template CIRGenFunction::OpenMPClauseEmissionInfo                            \
  CIRGenFunction::emitOpenMPClauses<N>(                                        \
      N &, ArrayRef<const OMPClause *>, ArrayRef<const VarDecl *>, bool);
EXPL_SPEC(mlir::omp::ParallelOp)
EXPL_SPEC(mlir::omp::TaskOp)
EXPL_SPEC(mlir::omp::TaskloopOp)
EXPL_SPEC(mlir::omp::TaskgroupOp)
EXPL_SPEC(mlir::omp::TaskwaitOp)
EXPL_SPEC(mlir::omp::TeamsOp)
EXPL_SPEC(mlir::omp::DistributeOp)
EXPL_SPEC(mlir::omp::SimdOp)
EXPL_SPEC(mlir::omp::WsloopOp)
EXPL_SPEC(mlir::omp::SectionsOp)
EXPL_SPEC(cir::OmpScopeReductionOp)
EXPL_SPEC(mlir::omp::TargetOp)
EXPL_SPEC(mlir::omp::TargetUpdateOp)
#undef EXPL_SPEC
