//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Decl nodes as CIR code.
//
//===----------------------------------------------------------------------===//

#include "CIRGenConstantEmitter.h"
#include "CIRGenCXXABI.h"
#include "CIRGenFunction.h"
#include "CIRGenSanitizer.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/SymbolTable.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclOpenACC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace clang;
using namespace clang::CIRGen;

namespace {
static void setRuntimeFnNothrow(cir::FuncOp fn) {
  mlir::NamedAttrList attrs;
  if (auto existingAttrs = fn.getExtraAttrs())
    attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
  attrs.set("nothrow", mlir::UnitAttr::get(fn->getContext()));
  fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
      attrs.getDictionary(fn->getContext())));
}

constexpr uint32_t kBlockFieldIsObject = 0x03;
constexpr uint32_t kBlockFieldIsBlock = 0x07;
constexpr uint32_t kBlockFieldIsByref = 0x08;
constexpr uint32_t kBlockFieldIsWeak = 0x10;
constexpr uint32_t kBlockByrefCaller = 128;
constexpr uint32_t kBlockByrefHasCopyDispose = 1u << 25;
constexpr uint32_t kBlockByrefLayoutExtended = 1u << 28;
constexpr uint32_t kBlockByrefLayoutNonObject = 2u << 28;
constexpr uint32_t kBlockByrefLayoutStrong = 3u << 28;
constexpr uint32_t kBlockByrefLayoutWeak = 4u << 28;
constexpr uint32_t kBlockByrefLayoutUnretained = 5u << 28;
constexpr uint8_t kTypeMismatchTrapHandlerID = 22; // SanitizerHandler::TypeMismatch

enum class BlockLayoutOpcode : unsigned char {
  Operator = 0,
  NonObjectBytes = 1,
  NonObjectWords = 2,
  Strong = 3,
  Byref = 4,
  Weak = 5,
  Unretained = 6,
};

struct BlockLayoutEntry {
  BlockLayoutOpcode opcode;
  CharUnits bytePos;
  CharUnits size;

  bool operator<(const BlockLayoutEntry &other) const {
    return bytePos < other.bytePos;
  }
};

static QualType inheritVolatileQualifier(QualType parentTy, QualType childTy) {
  return parentTy.isVolatileQualified() ? childTy.withVolatile() : childTy;
}

static bool isSupportedNonTrivialCStructType(QualType ty);

static mlir::Location getAutoVarDebugLoc(CIRGenFunction &cgf,
                                         const VarDecl &d) {
  // Match OGCG's ApplyDebugLocation in EmitAutoVarInit/EmitAutoVarAlloca:
  // local storage and initialization are attributed to the declaration site,
  // not the full declaration range.
  return cgf.getLoc(d.getLocation());
}

static cir::CallOp emitTypeMismatchStubCall(
    CIRGenFunction &cgf, mlir::Location loc, SourceLocation sourceLoc,
    mlir::Value ptrAsInt, QualType ty, unsigned log2Align,
    CIRGenFunction::TypeCheckKind tck, llvm::StringRef handlerKind) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  auto stubFnTy =
      cir::FuncType::get({ptrAsInt.getType()}, builder.getVoidTy());
  auto stubFn = cgf.cgm.createRuntimeFunction(
      stubFnTy, "__cir_ubsan_type_mismatch_handler", /*attrs=*/{},
      /*isLocal=*/false, /*assumeConvergent=*/false);

  auto call = builder.createCallOp(loc, stubFn, {ptrAsInt});
  setUBSanSourceLocationAttrs(cgf, call, sourceLoc);
  setUBSanTypeNameAttr(cgf, call, ty);
  setUBSanTypeDescriptorAttrs(cgf, call, ty);
  call->setDiscardableAttr("cir.ubsan.log2_alignment",
                           builder.getI8IntegerAttr(log2Align));
  call->setDiscardableAttr("cir.ubsan.type_check_kind",
                           builder.getI8IntegerAttr(static_cast<uint8_t>(tck)));
  call->setDiscardableAttr("cir.ubsan.handler_kind",
                           builder.getStringAttr(handlerKind));
  call->setDiscardableAttr("cir.ubsan.handler_name",
                           builder.getStringAttr("type_mismatch"));
  call->setDiscardableAttr("cir.nosanitize",
                           mlir::UnitAttr::get(builder.getContext()));
  return call;
}

static bool isSupportedNonTrivialCStructFieldType(QualType ty) {
  if (ty.hasAddressDiscriminatedPointerAuth() || ty->isArrayType())
    return false;

  switch (ty.isNonTrivialToPrimitiveDefaultInitialize()) {
  case QualType::PDIK_Trivial:
  case QualType::PDIK_ARCStrong:
  case QualType::PDIK_ARCWeak:
    break;
  case QualType::PDIK_Struct:
    if (!isSupportedNonTrivialCStructType(ty))
      return false;
    break;
  }

  switch (ty.isNonTrivialToPrimitiveDestructiveMove()) {
  case QualType::PCK_Trivial:
  case QualType::PCK_VolatileTrivial:
  case QualType::PCK_ARCStrong:
  case QualType::PCK_ARCWeak:
    break;
  case QualType::PCK_Struct:
    if (!isSupportedNonTrivialCStructType(ty))
      return false;
    break;
  case QualType::PCK_PtrAuth:
    return false;
  }

  switch (ty.isDestructedType()) {
  case QualType::DK_none:
  case QualType::DK_objc_strong_lifetime:
  case QualType::DK_objc_weak_lifetime:
    return true;
  case QualType::DK_nontrivial_c_struct:
    return isSupportedNonTrivialCStructType(ty);
  case QualType::DK_cxx_destructor:
    return false;
  }

  llvm_unreachable("unknown destruction kind");
}

static bool isSupportedNonTrivialCStructType(QualType ty) {
  const auto *recordTy = ty->getAs<RecordType>();
  if (!recordTy || ty->getAsCXXRecordDecl() || recordTy->getDecl()->isUnion())
    return false;

  for (const FieldDecl *field : recordTy->getDecl()->fields()) {
    QualType fieldTy = inheritVolatileQualifier(ty, field->getType());
    if (!isSupportedNonTrivialCStructFieldType(fieldTy))
      return false;
  }

  return true;
}

static bool isSupportedNonTrivialCStructByrefHelperType(QualType ty) {
  return isSupportedNonTrivialCStructType(ty);
}

static void emitSupportedNonTrivialCStructDefaultInit(CIRGenFunction &cgf,
                                                      Address addr,
                                                      QualType ty);

static void destroySupportedNonTrivialCStruct(CIRGenFunction &cgf,
                                              Address addr, QualType ty);

static uint64_t getNonTrivialCStructFieldSizeInBits(const FieldDecl *field,
                                                    QualType fieldTy,
                                                    ASTContext &ctx) {
  if (field && field->isBitField())
    return field->getBitWidthValue();
  return ctx.getTypeSize(fieldTy);
}

template <typename Derived> struct NonTrivialCStructNameBuilderBase {
  ASTContext &ctx;
  std::string name;

  explicit NonTrivialCStructNameBuilderBase(ASTContext &ctx) : ctx(ctx) {}

  Derived &asDerived() { return static_cast<Derived &>(*this); }

  CharUnits getFieldOffset(const FieldDecl *field) const {
    return ctx.toCharUnitsFromBits(
        ctx.getASTRecordLayout(field->getParent())
            .getFieldOffset(field->getFieldIndex()));
  }

  void appendVolatileOffset(bool isVolatile, CharUnits offset) {
    if (isVolatile)
      name += "v";
    name += std::to_string(offset.getQuantity());
  }

  void flushTrivialFields() { asDerived().flushTrivialFields(); }

  void visitARCStrong(QualType fieldTy, const FieldDecl *field,
                      CharUnits curStructOffset) {
    flushTrivialFields();
    name += "_s";
    if (fieldTy->isBlockPointerType())
      name += "b";
    appendVolatileOffset(fieldTy.isVolatileQualified(),
                         curStructOffset + getFieldOffset(field));
  }

  void visitARCWeak(QualType fieldTy, const FieldDecl *field,
                    CharUnits curStructOffset) {
    flushTrivialFields();
    name += "_w";
    appendVolatileOffset(fieldTy.isVolatileQualified(),
                         curStructOffset + getFieldOffset(field));
  }

  void visitStruct(QualType fieldTy, const FieldDecl *field,
                   CharUnits curStructOffset) {
    flushTrivialFields();
    name += "_S";
    visitStructFields(fieldTy, curStructOffset + getFieldOffset(field));
  }

  void visitStructFields(QualType qt, CharUnits curStructOffset) {
    const auto *recordTy = qt->castAs<RecordType>();
    for (const FieldDecl *field : recordTy->getDecl()->fields()) {
      QualType fieldTy = inheritVolatileQualifier(qt, field->getType());
      asDerived().visitField(fieldTy, field, curStructOffset);
    }
    flushTrivialFields();
  }

  std::string finish(QualType qt) {
    visitStructFields(qt, CharUnits::Zero());
    return name;
  }
};

struct NonTrivialCStructMoveConstructorNameBuilder
    : NonTrivialCStructNameBuilderBase<
          NonTrivialCStructMoveConstructorNameBuilder> {
  CharUnits start = CharUnits::Zero();
  CharUnits end = CharUnits::Zero();

  NonTrivialCStructMoveConstructorNameBuilder(CharUnits dstAlignment,
                                              CharUnits srcAlignment,
                                              ASTContext &ctx)
      : NonTrivialCStructNameBuilderBase(ctx) {
    name = "__move_constructor_" +
           std::to_string(dstAlignment.getQuantity()) + "_" +
           std::to_string(srcAlignment.getQuantity());
  }

  void visitTrivial(QualType fieldTy, const FieldDecl *field,
                    CharUnits curStructOffset) {
    uint64_t fieldSize = getNonTrivialCStructFieldSizeInBits(field, fieldTy, ctx);
    if (fieldSize == 0)
      return;

    uint64_t fieldStartInBits =
        ctx.getASTRecordLayout(field->getParent())
            .getFieldOffset(field->getFieldIndex());
    uint64_t fieldEndInBits = fieldStartInBits + fieldSize;
    uint64_t roundedFieldEnd =
        llvm::alignTo(fieldEndInBits, ctx.getCharWidth());

    if (start == end)
      start = curStructOffset + ctx.toCharUnitsFromBits(fieldStartInBits);
    end = curStructOffset + ctx.toCharUnitsFromBits(roundedFieldEnd);
  }

  void flushTrivialFields() {
    if (start == end)
      return;

    name += "_t" + std::to_string(start.getQuantity()) + "w" +
            std::to_string((end - start).getQuantity());
    start = end = CharUnits::Zero();
  }

  void visitField(QualType fieldTy, const FieldDecl *field,
                  CharUnits curStructOffset) {
    switch (fieldTy.isNonTrivialToPrimitiveDestructiveMove()) {
    case QualType::PCK_Trivial:
    case QualType::PCK_VolatileTrivial:
      visitTrivial(fieldTy, field, curStructOffset);
      break;
    case QualType::PCK_ARCStrong:
      visitARCStrong(fieldTy, field, curStructOffset);
      break;
    case QualType::PCK_ARCWeak:
      visitARCWeak(fieldTy, field, curStructOffset);
      break;
    case QualType::PCK_Struct:
      visitStruct(fieldTy, field, curStructOffset);
      break;
    case QualType::PCK_PtrAuth:
      llvm_unreachable("unsupported pointer-auth non-trivial struct field");
    }
  }
};

struct NonTrivialCStructDestructorNameBuilder
    : NonTrivialCStructNameBuilderBase<
          NonTrivialCStructDestructorNameBuilder> {
  explicit NonTrivialCStructDestructorNameBuilder(CharUnits dstAlignment,
                                                  ASTContext &ctx)
      : NonTrivialCStructNameBuilderBase(ctx) {
    name = "__destructor_" + std::to_string(dstAlignment.getQuantity());
  }

  void flushTrivialFields() {}

  void visitField(QualType fieldTy, const FieldDecl *field,
                  CharUnits curStructOffset) {
    switch (fieldTy.isDestructedType()) {
    case QualType::DK_none:
      break;
    case QualType::DK_objc_strong_lifetime:
      visitARCStrong(fieldTy, field, curStructOffset);
      break;
    case QualType::DK_objc_weak_lifetime:
      visitARCWeak(fieldTy, field, curStructOffset);
      break;
    case QualType::DK_nontrivial_c_struct:
      visitStruct(fieldTy, field, curStructOffset);
      break;
    case QualType::DK_cxx_destructor:
      llvm_unreachable("unsupported C++ field in C struct destroy");
    }
  }
};

struct NonTrivialCStructDefaultConstructorNameBuilder
    : NonTrivialCStructNameBuilderBase<
          NonTrivialCStructDefaultConstructorNameBuilder> {
  explicit NonTrivialCStructDefaultConstructorNameBuilder(
      CharUnits dstAlignment, ASTContext &ctx)
      : NonTrivialCStructNameBuilderBase(ctx) {
    name = "__default_constructor_" +
           std::to_string(dstAlignment.getQuantity());
  }

  void flushTrivialFields() {}

  void visitField(QualType fieldTy, const FieldDecl *field,
                  CharUnits curStructOffset) {
    switch (fieldTy.isNonTrivialToPrimitiveDefaultInitialize()) {
    case QualType::PDIK_Trivial:
      break;
    case QualType::PDIK_ARCStrong:
      visitARCStrong(fieldTy, field, curStructOffset);
      break;
    case QualType::PDIK_ARCWeak:
      visitARCWeak(fieldTy, field, curStructOffset);
      break;
    case QualType::PDIK_Struct:
      visitStruct(fieldTy, field, curStructOffset);
      break;
    }
  }
};

static void configureNonTrivialCStructThunk(CIRGenModule &cgm, cir::FuncOp fn) {
  fn.setLinkage(cir::GlobalLinkageKind::LinkOnceODRLinkage);
  fn.setGlobalVisibility(cir::VisibilityKind::Hidden);
  mlir::SymbolTable::setSymbolVisibility(
      fn, CIRGenModule::getMLIRVisibilityFromCIRLinkage(fn.getLinkage()));
  if (cgm.supportsCOMDAT())
    fn.setComdat(true);
  cgm.setDSOLocal(fn.getOperation());
}

static cir::FuncOp
getOrCreateNonTrivialCStructDefaultConstructorThunk(CIRGenFunction &cgf,
                                                    mlir::Location loc,
                                                    QualType ty,
                                                    CharUnits dstAlignment) {
  auto &cgm = cgf.cgm;
  auto &builder = cgm.getBuilder();
  std::string name =
      NonTrivialCStructDefaultConstructorNameBuilder(dstAlignment,
                                                     cgf.getContext())
          .finish(ty);
  if (cir::FuncOp fn = lookupFuncByName(cgm, name))
    return fn;

  auto voidTy = builder.getVoidTy();
  auto voidPtrTy = builder.getVoidPtrTy();
  cir::FuncOp fn = cgm.createCIRFunction(
      loc, name, cir::FuncType::get({voidPtrTy}, voidTy), nullptr);
  configureNonTrivialCStructThunk(cgm, fn);

  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *entry = fn.addEntryBlock();
    builder.setInsertionPointToStart(entry);

    CIRGenFunction helperCGF(cgm, builder, /*suppressNewContext=*/true);
    CIRGenFunction::SourceLocRAIIObject helperLoc(helperCGF, loc);
    mlir::Type objectTy = cgm.getTypes().convertTypeForMem(ty);
    mlir::Type objectPtrTy = builder.getPointerTo(objectTy);
    Address dstAddr(builder.createBitcast(loc, fn.getArgument(0), objectPtrTy),
                    objectTy, dstAlignment);
    emitSupportedNonTrivialCStructDefaultInit(helperCGF, dstAddr, ty);
    cir::ReturnOp::create(builder, loc);
  }

  return fn;
}

static cir::FuncOp
getOrCreateNonTrivialCStructMoveConstructorThunk(CIRGenFunction &cgf,
                                                 mlir::Location loc,
                                                 QualType ty,
                                                 CharUnits dstAlignment,
                                                 CharUnits srcAlignment) {
  auto &cgm = cgf.cgm;
  auto &builder = cgm.getBuilder();
  std::string name = NonTrivialCStructMoveConstructorNameBuilder(
                         dstAlignment, srcAlignment, cgf.getContext())
                         .finish(ty);
  if (cir::FuncOp fn = lookupFuncByName(cgm, name))
    return fn;

  auto voidTy = builder.getVoidTy();
  auto voidPtrTy = builder.getVoidPtrTy();
  cir::FuncOp fn = cgm.createCIRFunction(
      loc, name, cir::FuncType::get({voidPtrTy, voidPtrTy}, voidTy), nullptr);
  configureNonTrivialCStructThunk(cgm, fn);

  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *entry = fn.addEntryBlock();
    builder.setInsertionPointToStart(entry);

    CIRGenFunction helperCGF(cgm, builder, /*suppressNewContext=*/true);
    CIRGenFunction::SourceLocRAIIObject helperLoc(helperCGF, loc);
    mlir::Type objectTy = cgm.getTypes().convertTypeForMem(ty);
    mlir::Type objectPtrTy = builder.getPointerTo(objectTy);
    Address dstAddr(builder.createBitcast(loc, fn.getArgument(0), objectPtrTy),
                    objectTy, dstAlignment);
    Address srcAddr(builder.createBitcast(loc, fn.getArgument(1), objectPtrTy),
                    objectTy, srcAlignment);
    helperCGF.emitSupportedNonTrivialCStructMoveConstructor(dstAddr, srcAddr,
                                                            ty);
    cir::ReturnOp::create(builder, loc);
  }

  return fn;
}

static cir::FuncOp
getOrCreateNonTrivialCStructDestructorThunk(CIRGenFunction &cgf,
                                            mlir::Location loc, QualType ty,
                                            CharUnits dstAlignment) {
  auto &cgm = cgf.cgm;
  auto &builder = cgm.getBuilder();
  std::string name =
      NonTrivialCStructDestructorNameBuilder(dstAlignment, cgf.getContext())
          .finish(ty);
  if (cir::FuncOp fn = lookupFuncByName(cgm, name))
    return fn;

  auto voidTy = builder.getVoidTy();
  auto voidPtrTy = builder.getVoidPtrTy();
  cir::FuncOp fn = cgm.createCIRFunction(
      loc, name, cir::FuncType::get({voidPtrTy}, voidTy), nullptr);
  configureNonTrivialCStructThunk(cgm, fn);

  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *entry = fn.addEntryBlock();
    builder.setInsertionPointToStart(entry);

    CIRGenFunction helperCGF(cgm, builder, /*suppressNewContext=*/true);
    CIRGenFunction::SourceLocRAIIObject helperLoc(helperCGF, loc);
    mlir::Type objectTy = cgm.getTypes().convertTypeForMem(ty);
    mlir::Type objectPtrTy = builder.getPointerTo(objectTy);
    Address dstAddr(builder.createBitcast(loc, fn.getArgument(0), objectPtrTy),
                    objectTy, dstAlignment);
    destroySupportedNonTrivialCStruct(helperCGF, dstAddr, ty);
    cir::ReturnOp::create(builder, loc);
  }

  return fn;
}

static void emitNonTrivialCStructUnaryThunkCall(CIRGenFunction &cgf,
                                                mlir::Location loc,
                                                cir::FuncOp thunk,
                                                Address objectAddr) {
  auto &builder = cgf.getBuilder();
  mlir::Value objectPtr = objectAddr.getPointer();
  mlir::Type voidPtrTy = builder.getVoidPtrTy();
  if (objectPtr.getType() != voidPtrTy)
    objectPtr = builder.createBitcast(loc, objectPtr, voidPtrTy);
  builder.createCallOp(loc, thunk, {objectPtr});
}

enum class NonTrivialCStructOperation {
  CopyConstructor,
  MoveConstructor,
  CopyAssignment,
  MoveAssignment,
};

static void emitSupportedNonTrivialCStructOperation(
    CIRGenFunction &cgf, Address dstAddr, Address srcAddr, QualType ty,
    NonTrivialCStructOperation operation) {
  if (!isSupportedNonTrivialCStructType(ty)) {
    cgf.cgm.errorNYI("unsupported non-trivial C struct operation");
    return;
  }

  const auto *recordTy = ty->castAs<RecordType>();
  LValue dstBase = cgf.makeAddrLValue(dstAddr, ty);
  LValue srcBase = cgf.makeAddrLValue(srcAddr, ty);

  for (const FieldDecl *field : recordTy->getDecl()->fields()) {
    QualType fieldTy = inheritVolatileQualifier(ty, field->getType());
    QualType::PrimitiveCopyKind copyKind =
        fieldTy.isNonTrivialToPrimitiveDestructiveMove();
    LValue dstField = cgf.emitLValueForField(dstBase, field);
    LValue srcField = cgf.emitLValueForField(srcBase, field);

    switch (copyKind) {
    case QualType::PCK_Trivial:
    case QualType::PCK_VolatileTrivial:
      if (CIRGenFunction::hasAggregateEvaluationKind(fieldTy)) {
        cgf.emitAggregateCopy(dstField, srcField, fieldTy,
                              AggValueSlot::DoesNotOverlap,
                              fieldTy.isVolatileQualified());
      } else {
        cgf.emitStoreThroughLValue(
            cgf.emitLoadOfLValue(srcField, SourceLocation()), dstField,
            /*isInit=*/true);
      }
      break;
    case QualType::PCK_ARCStrong: {
      mlir::Location loc = cgf.getLoc(field->getLocation());
      Address dstFieldAddr = dstField.getAddress();
      Address srcFieldAddr = srcField.getAddress();
      mlir::Type fieldMemTy = srcFieldAddr.getElementType();
      mlir::Value value = cgf.getBuilder().createAlignedLoad(
          loc, fieldMemTy, srcFieldAddr.getPointer(),
          srcFieldAddr.getAlignment());
      switch (operation) {
      case NonTrivialCStructOperation::CopyConstructor:
        value = cgf.emitARCRetainNonBlock(value, loc);
        cgf.getBuilder().createStore(loc, value, dstFieldAddr);
        break;
      case NonTrivialCStructOperation::MoveConstructor: {
        mlir::Value nullValue = cgf.getBuilder().getNullValue(fieldMemTy, loc);
        cgf.getBuilder().createStore(loc, nullValue, srcFieldAddr);
        cgf.getBuilder().createStore(loc, value, dstFieldAddr);
        break;
      }
      case NonTrivialCStructOperation::CopyAssignment:
        cgf.emitARCStoreStrongCall(dstFieldAddr, value, loc);
        break;
      case NonTrivialCStructOperation::MoveAssignment: {
        mlir::Value nullValue = cgf.getBuilder().getNullValue(fieldMemTy, loc);
        cgf.getBuilder().createStore(loc, nullValue, srcFieldAddr);
        mlir::Value oldValue = cgf.getBuilder().createAlignedLoad(
            loc, fieldMemTy, dstFieldAddr.getPointer(),
            dstFieldAddr.getAlignment());
        cgf.getBuilder().createStore(loc, value, dstFieldAddr);
        cgf.emitARCRelease(oldValue, loc, /*isImprecise=*/true);
        break;
      }
      }
      break;
    }
    case QualType::PCK_ARCWeak:
      switch (operation) {
      case NonTrivialCStructOperation::CopyConstructor:
        cgf.emitARCCopyWeak(dstField.getAddress(), srcField.getAddress());
        break;
      case NonTrivialCStructOperation::MoveConstructor:
        cgf.emitARCMoveWeak(dstField.getAddress(), srcField.getAddress());
        break;
      case NonTrivialCStructOperation::CopyAssignment:
        cgf.emitARCCopyAssignWeak(dstField.getAddress(), srcField.getAddress());
        break;
      case NonTrivialCStructOperation::MoveAssignment:
        cgf.emitARCMoveAssignWeak(dstField.getAddress(), srcField.getAddress());
        break;
      }
      break;
    case QualType::PCK_Struct:
      emitSupportedNonTrivialCStructOperation(cgf, dstField.getAddress(),
                                              srcField.getAddress(), fieldTy,
                                              operation);
      break;
    case QualType::PCK_PtrAuth:
      llvm_unreachable("unsupported pointer-auth non-trivial struct field");
    }
  }
}

} // namespace

void CIRGenFunction::emitSupportedNonTrivialCStructCopyConstructor(
    Address dst, Address src, QualType ty) {
  emitSupportedNonTrivialCStructOperation(
      *this, dst, src, ty, NonTrivialCStructOperation::CopyConstructor);
}

void CIRGenFunction::emitSupportedNonTrivialCStructMoveConstructor(
    Address dst, Address src, QualType ty) {
  emitSupportedNonTrivialCStructOperation(
      *this, dst, src, ty, NonTrivialCStructOperation::MoveConstructor);
}

void CIRGenFunction::emitSupportedNonTrivialCStructCopyAssignment(
    Address dst, Address src, QualType ty) {
  emitSupportedNonTrivialCStructOperation(
      *this, dst, src, ty, NonTrivialCStructOperation::CopyAssignment);
}

void CIRGenFunction::emitSupportedNonTrivialCStructMoveAssignment(
    Address dst, Address src, QualType ty) {
  emitSupportedNonTrivialCStructOperation(
      *this, dst, src, ty, NonTrivialCStructOperation::MoveAssignment);
}

namespace {

static void emitSupportedNonTrivialCStructDefaultInit(CIRGenFunction &cgf,
                                                      Address addr,
                                                      QualType ty) {
  if (!isSupportedNonTrivialCStructType(ty)) {
    cgf.cgm.errorNYI("unsupported non-trivial C struct default init");
    return;
  }

  const auto *recordTy = ty->castAs<RecordType>();
  LValue base = cgf.makeAddrLValue(addr, ty);

  for (const FieldDecl *field : recordTy->getDecl()->fields()) {
    QualType fieldTy = inheritVolatileQualifier(ty, field->getType());
    QualType::PrimitiveDefaultInitializeKind initKind =
        fieldTy.isNonTrivialToPrimitiveDefaultInitialize();
    if (initKind == QualType::PDIK_Trivial)
      continue;

    LValue fieldLV = cgf.emitLValueForField(base, field);
    switch (initKind) {
    case QualType::PDIK_Trivial:
      llvm_unreachable("handled above");
    case QualType::PDIK_ARCStrong:
    case QualType::PDIK_ARCWeak:
      cgf.emitNullInitialization(cgf.getLoc(field->getLocation()),
                                 fieldLV.getAddress(), fieldTy);
      break;
    case QualType::PDIK_Struct:
      emitSupportedNonTrivialCStructDefaultInit(cgf, fieldLV.getAddress(),
                                                fieldTy);
      break;
    }
  }
}

static void destroySupportedNonTrivialCStruct(CIRGenFunction &cgf,
                                              Address addr, QualType ty) {
  if (!isSupportedNonTrivialCStructType(ty)) {
    cgf.cgm.errorNYI("unsupported non-trivial C struct destroy");
    return;
  }

  const auto *recordTy = ty->castAs<RecordType>();
  LValue base = cgf.makeAddrLValue(addr, ty);

  for (const FieldDecl *field : recordTy->getDecl()->fields()) {
    QualType fieldTy = inheritVolatileQualifier(ty, field->getType());
    QualType::DestructionKind dtorKind = fieldTy.isDestructedType();
    if (dtorKind == QualType::DK_none)
      continue;

    LValue fieldLV = cgf.emitLValueForField(base, field);
    switch (dtorKind) {
    case QualType::DK_none:
      llvm_unreachable("handled above");
    case QualType::DK_objc_strong_lifetime:
      cgf.emitARCDestroyStrong(fieldLV.getAddress(),
                               cgf.getLoc(field->getLocation()),
                               /*isImprecise=*/true);
      break;
    case QualType::DK_objc_weak_lifetime:
      cgf.emitARCDestroyWeak(fieldLV.getAddress());
      break;
    case QualType::DK_nontrivial_c_struct:
      destroySupportedNonTrivialCStruct(cgf, fieldLV.getAddress(), fieldTy);
      break;
    case QualType::DK_cxx_destructor:
      llvm_unreachable("unsupported C++ field in C struct destroy");
    }
  }
}

static void defaultInitSupportedNonTrivialCStructWithThunk(
    CIRGenFunction &cgf, Address addr, QualType ty) {
  if (!isSupportedNonTrivialCStructType(ty)) {
    cgf.cgm.errorNYI("unsupported non-trivial C struct default init");
    return;
  }

  mlir::Location loc = addr.getPointer().getLoc();
  cir::FuncOp thunk = getOrCreateNonTrivialCStructDefaultConstructorThunk(
      cgf, loc, ty, addr.getAlignment());
  emitNonTrivialCStructUnaryThunkCall(cgf, loc, thunk, addr);
}

static void destroySupportedNonTrivialCStructWithThunk(CIRGenFunction &cgf,
                                                       Address addr,
                                                       QualType ty) {
  if (!isSupportedNonTrivialCStructType(ty)) {
    cgf.cgm.errorNYI("unsupported non-trivial C struct destroy");
    return;
  }

  mlir::Location loc = addr.getPointer().getLoc();
  cir::FuncOp thunk =
      getOrCreateNonTrivialCStructDestructorThunk(cgf, loc, ty,
                                                  addr.getAlignment());
  emitNonTrivialCStructUnaryThunkCall(cgf, loc, thunk, addr);
}

static CIRGenRuntimePointerAuthInfo
getByrefHelperFunctionPointerAuthInfo(CIRGenFunction &cgf,
                                      Address storageAddress) {
  const auto &schema =
      cgf.cgm.getCodeGenOpts().PointerAuth.BlockByrefHelperFunctionPointers;
  if (!schema || !cgf.cgm.shouldSignPointer(schema))
    return {};

  CIRGenRuntimePointerAuthInfo info{
      schema.getAuthenticationMode(),
      /*isIsaPointer=*/false,
      /*authenticatesNullValues=*/false,
      schema.getKey(),
      mlir::Value{}};

  mlir::Location loc = storageAddress.getPointer().getLoc();
  uint64_t otherDiscriminator =
      cgf.cgm.getPointerAuthOtherDiscriminator(schema, GlobalDecl(), QualType());
  if (otherDiscriminator) {
    info.discriminator =
        cgf.getBuilder().getConstantInt(loc, cgf.cgm.uIntPtrTy,
                                        otherDiscriminator);
  }

  if (schema.isAddressDiscriminated()) {
    mlir::Value storagePtr = storageAddress.emitRawPointer();
    if (info.discriminator)
      info.discriminator =
          cgf.emitPointerAuthBlendDiscriminator(storagePtr, info.discriminator);
    else
      info.discriminator = cgf.getBuilder().createPtrToInt(storagePtr,
                                                           cgf.cgm.uIntPtrTy);
  }

  return info;
}

static std::string createUniqueInternalSymbolName(CIRGenModule &cgm,
                                                  StringRef base) {
  std::string name(base);
  for (unsigned suffix = 0; cgm.getGlobalValue(name); ++suffix)
    name = (base + "." + llvm::Twine(suffix + 1)).str();
  return name;
}

static const CIRGenFunctionInfo &
getBlockByrefHelperFunctionInfo(CIRGenModule &cgm, bool isCopy) {
  ASTContext &ctx = cgm.getASTContext();
  SmallVector<CanQualType, 2> argTypes;
  argTypes.push_back(ctx.getCanonicalParamType(ctx.VoidPtrTy));
  if (isCopy)
    argTypes.push_back(ctx.getCanonicalParamType(ctx.VoidPtrTy));
  return cgm.getTypes().arrangeCIRFunctionInfo(
      CanQualType::CreateUnsafe(ctx.getCanonicalType(ctx.VoidTy)),
      FnInfoOpts::None, argTypes, RequiredArgs::All, FunctionType::ExtInfo());
}

static cir::FuncOp createBlockByrefHelperFunction(
    CIRGenModule &cgm, mlir::Location loc, StringRef baseName,
    const CIRGenFunctionInfo &info) {
  cir::FuncOp fn = cgm.createCIRFunction(
      loc, createUniqueInternalSymbolName(cgm, baseName),
      cgm.getTypes().getFunctionType(info), nullptr);
  fn.setSymVisibility("private");
  cgm.setInternalFunctionAttributes(GlobalDecl{}, fn, info);
  if (cgm.getTarget().getTriple().isX86()) {
    mlir::NamedAttrList attrs;
    if (auto existingAttrs = fn.getExtraAttrs())
      attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
    attrs.set("min-legal-vector-width", cgm.getStringAttrCached("0"));
    fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(&cgm.getMLIRContext())));
  }
  return fn;
}

static uint64_t
getObjCInlineBlockLayoutInstruction(SmallVectorImpl<unsigned char> &layout) {
  unsigned size = layout.size();
  uint64_t result = 0;
  if (size > 3)
    return 0;

  unsigned strongWordCount = 0;
  unsigned byrefWordCount = 0;
  unsigned weakWordCount = 0;
  auto getWordCount = [](unsigned char inst) { return (inst & 0xF) + 1; };

  switch (size) {
  case 3:
    if (static_cast<BlockLayoutOpcode>(layout[0] >> 4) !=
            BlockLayoutOpcode::Strong ||
        static_cast<BlockLayoutOpcode>(layout[1] >> 4) !=
            BlockLayoutOpcode::Byref ||
        static_cast<BlockLayoutOpcode>(layout[2] >> 4) !=
            BlockLayoutOpcode::Weak)
      return 0;
    strongWordCount = getWordCount(layout[0]);
    byrefWordCount = getWordCount(layout[1]);
    weakWordCount = getWordCount(layout[2]);
    break;
  case 2: {
    BlockLayoutOpcode firstOpcode = static_cast<BlockLayoutOpcode>(layout[0] >> 4);
    BlockLayoutOpcode secondOpcode =
        static_cast<BlockLayoutOpcode>(layout[1] >> 4);
    if (firstOpcode == BlockLayoutOpcode::Strong) {
      strongWordCount = getWordCount(layout[0]);
      if (secondOpcode == BlockLayoutOpcode::Byref)
        byrefWordCount = getWordCount(layout[1]);
      else if (secondOpcode == BlockLayoutOpcode::Weak)
        weakWordCount = getWordCount(layout[1]);
      else
        return 0;
    } else if (firstOpcode == BlockLayoutOpcode::Byref &&
               secondOpcode == BlockLayoutOpcode::Weak) {
      byrefWordCount = getWordCount(layout[0]);
      weakWordCount = getWordCount(layout[1]);
    } else {
      return 0;
    }
    break;
  }
  case 1: {
    BlockLayoutOpcode opcode = static_cast<BlockLayoutOpcode>(layout[0] >> 4);
    if (opcode == BlockLayoutOpcode::Strong)
      strongWordCount = getWordCount(layout[0]);
    else if (opcode == BlockLayoutOpcode::Byref)
      byrefWordCount = getWordCount(layout[0]);
    else if (opcode == BlockLayoutOpcode::Weak)
      weakWordCount = getWordCount(layout[0]);
    else
      return 0;
    break;
  }
  case 0:
    return 0;
  }

  if (strongWordCount == 16 || byrefWordCount == 16 || weakWordCount == 16)
    return 0;

  unsigned count = (strongWordCount != 0) + (byrefWordCount != 0) +
                   (weakWordCount != 0);
  if (size != count)
    return 0;

  if (strongWordCount)
    result = strongWordCount;
  result <<= 4;
  if (byrefWordCount)
    result += byrefWordCount;
  result <<= 4;
  if (weakWordCount)
    result += weakWordCount;
  return result;
}

static Qualifiers::ObjCLifetime getObjCByrefLifetime(CIRGenModule &cgm,
                                                     QualType fieldTy) {
  if (auto lifetime = fieldTy.getObjCLifetime())
    return lifetime;
  if (cgm.getLangOpts().ObjCAutoRefCount)
    return Qualifiers::OCL_None;
  if (fieldTy->isObjCObjectPointerType() || fieldTy->isBlockPointerType())
    return Qualifiers::OCL_ExplicitNone;
  return Qualifiers::OCL_None;
}

static void appendObjCBlockLayoutEntry(
    SmallVectorImpl<BlockLayoutEntry> &layoutEntries, bool isByref,
    Qualifiers::ObjCLifetime lifetime, CharUnits bytePos, CharUnits fieldSize) {
  BlockLayoutOpcode opcode = BlockLayoutOpcode::NonObjectBytes;
  if (isByref)
    opcode = BlockLayoutOpcode::Byref;
  else if (lifetime == Qualifiers::OCL_Strong)
    opcode = BlockLayoutOpcode::Strong;
  else if (lifetime == Qualifiers::OCL_Weak)
    opcode = BlockLayoutOpcode::Weak;
  else if (lifetime == Qualifiers::OCL_ExplicitNone)
    opcode = BlockLayoutOpcode::Unretained;

  layoutEntries.push_back({opcode, bytePos, fieldSize});
}

static void buildObjCByrefRecordLayout(CIRGenModule &cgm, const RecordType *recordTy,
                                       CharUnits bytePos, bool &hasUnion,
                                       SmallVectorImpl<BlockLayoutEntry> &layoutEntries);

static void buildObjCByrefLayoutForFields(
    CIRGenModule &cgm, const RecordDecl *recordDecl,
    ArrayRef<const FieldDecl *> fields, CharUnits bytePos, bool &hasUnion,
    SmallVectorImpl<BlockLayoutEntry> &layoutEntries) {
  if (fields.empty())
    return;

  ASTContext &ctx = cgm.getASTContext();
  bool isUnion = recordDecl && recordDecl->isUnion();
  CharUnits maxUnionSize = CharUnits::Zero();
  const FieldDecl *maxField = nullptr;
  CharUnits maxFieldOffset = CharUnits::Zero();
  const FieldDecl *lastBitfieldOrUnnamed = nullptr;
  CharUnits lastBitfieldOrUnnamedOffset = CharUnits::Zero();
  unsigned byteSizeInBits = cgm.getTarget().getCharWidth();
  const ASTRecordLayout &recordLayout = ctx.getASTRecordLayout(recordDecl);

  for (const auto &[index, field] : llvm::enumerate(fields)) {
    CharUnits fieldOffset =
        ctx.toCharUnitsFromBits(recordLayout.getFieldOffset(index));

    if (!field->getIdentifier() || field->isBitField()) {
      lastBitfieldOrUnnamed = field;
      lastBitfieldOrUnnamedOffset = fieldOffset;
      continue;
    }

    lastBitfieldOrUnnamed = nullptr;
    QualType fieldTy = field->getType();
    if (fieldTy->isRecordType() || fieldTy->isUnionType()) {
      if (fieldTy->isUnionType())
        hasUnion = true;
      buildObjCByrefRecordLayout(cgm, fieldTy->castAsCanonical<RecordType>(),
                                 bytePos + fieldOffset, hasUnion, layoutEntries);
      continue;
    }

    if (const auto *arrayTy = cgm.getASTContext().getAsArrayType(fieldTy)) {
      auto *constantArrayTy = cast<ConstantArrayType>(arrayTy);
      uint64_t elementCount = constantArrayTy->getZExtSize();
      fieldTy = constantArrayTy->getElementType();
      while (const auto *nestedArrayTy = cgm.getASTContext().getAsArrayType(fieldTy)) {
        auto *nestedConstantArrayTy = cast<ConstantArrayType>(nestedArrayTy);
        elementCount *= nestedConstantArrayTy->getZExtSize();
        fieldTy = nestedConstantArrayTy->getElementType();
      }
      if (fieldTy->isRecordType() && elementCount) {
        size_t firstNewEntry = layoutEntries.size();
        buildObjCByrefRecordLayout(cgm, fieldTy->castAsCanonical<RecordType>(),
                                   bytePos + fieldOffset, hasUnion,
                                   layoutEntries);
        size_t newEntryCount = layoutEntries.size() - firstNewEntry;
        if (newEntryCount) {
          CharUnits elementSize = ctx.getTypeSizeInChars(fieldTy);
          SmallVector<BlockLayoutEntry, 8> firstElementEntries(
              layoutEntries.begin() + firstNewEntry, layoutEntries.end());
          for (uint64_t elementIndex = 1; elementIndex < elementCount;
               ++elementIndex) {
            CharUnits elementOffset = elementSize * elementIndex;
            for (const BlockLayoutEntry &entry : firstElementEntries)
              layoutEntries.push_back(
                  {entry.opcode, entry.bytePos + elementOffset, entry.size});
          }
        }
        continue;
      }
    }

    CharUnits fieldSize = ctx.getTypeSizeInChars(field->getType());
    if (isUnion) {
      if (fieldSize > maxUnionSize) {
        maxUnionSize = fieldSize;
        maxField = field;
        maxFieldOffset = fieldOffset;
      }
    } else {
      appendObjCBlockLayoutEntry(layoutEntries,
                                 /*isByref=*/false,
                                 getObjCByrefLifetime(cgm, fieldTy),
                                 bytePos + fieldOffset, fieldSize);
    }
  }

  if (lastBitfieldOrUnnamed) {
    if (lastBitfieldOrUnnamed->isBitField()) {
      uint64_t bitFieldSize = lastBitfieldOrUnnamed->getBitWidthValue();
      unsigned unalignedSize =
          (bitFieldSize / byteSizeInBits) + ((bitFieldSize % byteSizeInBits) != 0);
      CharUnits size = CharUnits::fromQuantity(unalignedSize);
      size += lastBitfieldOrUnnamedOffset;
      appendObjCBlockLayoutEntry(
          layoutEntries, /*isByref=*/false,
          getObjCByrefLifetime(cgm, lastBitfieldOrUnnamed->getType()),
          bytePos + lastBitfieldOrUnnamedOffset, size);
    } else {
      CharUnits fieldSize =
          cgm.getASTContext().getTypeSizeInChars(lastBitfieldOrUnnamed->getType());
      appendObjCBlockLayoutEntry(
          layoutEntries, /*isByref=*/false,
          getObjCByrefLifetime(cgm, lastBitfieldOrUnnamed->getType()),
          bytePos + lastBitfieldOrUnnamedOffset, fieldSize);
    }
  }

  if (maxField) {
    appendObjCBlockLayoutEntry(layoutEntries, /*isByref=*/false,
                               getObjCByrefLifetime(cgm, maxField->getType()),
                               bytePos + maxFieldOffset, maxUnionSize);
  }
}

static void buildObjCByrefRecordLayout(CIRGenModule &cgm,
                                       const RecordType *recordTy,
                                       CharUnits bytePos, bool &hasUnion,
                                       SmallVectorImpl<BlockLayoutEntry> &layoutEntries) {
  const RecordDecl *recordDecl = recordTy->getDecl()->getDefinitionOrSelf();
  SmallVector<const FieldDecl *, 16> fields(recordDecl->fields());
  buildObjCByrefLayoutForFields(cgm, recordDecl, fields, bytePos, hasUnion,
                                layoutEntries);
}

static mlir::Value buildObjCByrefLayoutValue(CIRGenModule &cgm,
                                             CIRGenBuilderTy &builder,
                                             mlir::Location loc, QualType type) {
  auto i8PtrTy = builder.getPointerTo(builder.getSInt8Ty());
  const auto *recordTy = type->getAsCanonical<RecordType>();
  if (!recordTy)
    return builder.getNullValue(i8PtrTy, loc);

  SmallVector<BlockLayoutEntry, 8> layoutEntries;
  bool hasUnion = false;
  buildObjCByrefRecordLayout(cgm, recordTy, CharUnits::Zero(), hasUnion,
                             layoutEntries);
  if (layoutEntries.empty())
    return builder.getNullValue(i8PtrTy, loc);

  llvm::sort(layoutEntries);
  unsigned wordSizeInBits = cgm.getTarget().getPointerWidth(LangAS::Default);
  unsigned byteSizeInBits = cgm.getTarget().getCharWidth();
  unsigned wordSizeInBytes = wordSizeInBits / byteSizeInBits;

  SmallVector<unsigned char, 16> layout;
  for (unsigned i = 0, size = layoutEntries.size(); i < size; ++i) {
    BlockLayoutOpcode opcode = layoutEntries[i].opcode;
    CharUnits startBytePos = layoutEntries[i].bytePos;
    CharUnits endBytePos = startBytePos;
    unsigned j = i + 1;
    while (j < size && opcode == layoutEntries[j].opcode) {
      endBytePos = layoutEntries[j].bytePos;
      ++j;
      ++i;
    }
    CharUnits sizeInBytes =
        endBytePos - startBytePos + layoutEntries[j - 1].size;
    if (j < size) {
      CharUnits gap = layoutEntries[j].bytePos - layoutEntries[j - 1].bytePos -
                      layoutEntries[j - 1].size;
      sizeInBytes += gap;
    }
    CharUnits residueInBytes = CharUnits::Zero();
    if (opcode == BlockLayoutOpcode::NonObjectBytes) {
      residueInBytes = sizeInBytes % wordSizeInBytes;
      sizeInBytes -= residueInBytes;
      opcode = BlockLayoutOpcode::NonObjectWords;
    }

    unsigned sizeInWords = sizeInBytes.getQuantity() / wordSizeInBytes;
    while (sizeInWords >= 16) {
      layout.push_back((static_cast<unsigned char>(opcode) << 4) | 0xF);
      sizeInWords -= 16;
    }
    if (sizeInWords > 0)
      layout.push_back((static_cast<unsigned char>(opcode) << 4) |
                       (sizeInWords - 1));
    if (residueInBytes > CharUnits::Zero()) {
      layout.push_back(
          (static_cast<unsigned char>(BlockLayoutOpcode::NonObjectBytes) << 4) |
          (residueInBytes.getQuantity() - 1));
    }
  }

  while (!layout.empty()) {
    BlockLayoutOpcode opcode =
        static_cast<BlockLayoutOpcode>(layout.back() >> 4);
    if (opcode == BlockLayoutOpcode::NonObjectBytes ||
        opcode == BlockLayoutOpcode::NonObjectWords) {
      layout.pop_back();
      continue;
    }
    break;
  }

  if (uint64_t inlineLayout = getObjCInlineBlockLayoutInstruction(layout))
    return builder.getConstant(
        loc, mlir::cast<mlir::TypedAttr>(builder.getConstPtrAttr(i8PtrTy, inlineLayout)));

  layout.push_back(static_cast<unsigned char>(BlockLayoutOpcode::Operator) << 4);
  std::string bitmap(layout.begin(), layout.end());
  auto bitmapAttr = mlir::cast<mlir::TypedAttr>(
      builder.getString(bitmap, builder.getSInt8Ty(), bitmap.size()));
  cir::GlobalOp layoutGlobal = CIRGenModule::createGlobalOp(
      cgm, loc, cgm.getUniqueGlobalName("__byref_layout_"), bitmapAttr.getType(),
      /*isConstant=*/true);
  layoutGlobal.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
  layoutGlobal.setSymVisibility("private");
  layoutGlobal.setAlignment(1);
  CIRGenModule::setInitializer(layoutGlobal, bitmapAttr);
  layoutGlobal->setAttr("unnamed_addr",
                        mlir::UnitAttr::get(&cgm.getMLIRContext()));
  return builder.getConstant(loc, builder.getGlobalViewAttr(i8PtrTy, layoutGlobal));
}

static std::pair<cir::FuncOp, cir::FuncOp>
buildARCStrongByrefHelpers(CIRGenFunction &cgf, mlir::Location loc,
                           const VarDecl *var,
                           const CIRGenFunction::BlockByrefInfo &byrefInfo) {
  auto &cgm = cgf.cgm;
  auto &moduleBuilder = cgm.getBuilder();
  const CIRGenFunctionInfo &copyHelperInfo =
      getBlockByrefHelperFunctionInfo(cgm, /*isCopy=*/true);
  const CIRGenFunctionInfo &disposeHelperInfo =
      getBlockByrefHelperFunctionInfo(cgm, /*isCopy=*/false);

  cir::FuncOp copyHelper = createBlockByrefHelperFunction(
      cgm, loc, "__Block_byref_object_copy_", copyHelperInfo);
  cir::FuncOp disposeHelper = createBlockByrefHelperFunction(
      cgm, loc, "__Block_byref_object_dispose_", disposeHelperInfo);

  auto emitHelperBody = [&](cir::FuncOp fn, bool isCopy) {
    mlir::OpBuilder::InsertionGuard guard(moduleBuilder);
    mlir::Block *entry = fn.addEntryBlock();
    moduleBuilder.setInsertionPointToStart(entry);

    CIRGenFunction helperCGF(cgm, moduleBuilder, /*suppressNewContext=*/true);
    mlir::Type byrefPtrTy = moduleBuilder.getPointerTo(byrefInfo.type);

    mlir::Value dstBase = moduleBuilder.createBitcast(loc, fn.getArgument(0),
                                                      byrefPtrTy);
    Address dstField = helperCGF.emitBlockByrefAddress(
        Address(dstBase, byrefInfo.type, byrefInfo.byrefAlignment), var,
        /*followForward=*/false);

    if (isCopy) {
      mlir::Value srcBase = moduleBuilder.createBitcast(loc, fn.getArgument(1),
                                                        byrefPtrTy);
      Address srcField = helperCGF.emitBlockByrefAddress(
          Address(srcBase, byrefInfo.type, byrefInfo.byrefAlignment), var,
          /*followForward=*/false);
      mlir::Value value = moduleBuilder.createAlignedLoad(
          loc, srcField.getElementType(), srcField.getPointer(),
          srcField.getAlignment());
      mlir::Value nullValue =
          moduleBuilder.getNullValue(srcField.getElementType(), loc);

      if (cgm.getCodeGenOpts().OptimizationLevel == 0) {
        moduleBuilder.createStore(loc, nullValue, dstField);
        helperCGF.emitARCStoreStrongCall(dstField, value, loc);
        helperCGF.emitARCStoreStrongCall(srcField, nullValue, loc);
      } else {
        moduleBuilder.createStore(loc, value, dstField);
        moduleBuilder.createStore(loc, nullValue, srcField);
      }
    } else {
      helperCGF.emitARCDestroyStrong(dstField, loc, /*isImprecise=*/true);
    }

    cir::ReturnOp::create(moduleBuilder, loc);
  };

  emitHelperBody(copyHelper, /*isCopy=*/true);
  emitHelperBody(disposeHelper, /*isCopy=*/false);
  return {copyHelper, disposeHelper};
}

static std::pair<cir::FuncOp, cir::FuncOp>
buildARCWeakByrefHelpers(CIRGenFunction &cgf, mlir::Location loc,
                         const VarDecl *var,
                         const CIRGenFunction::BlockByrefInfo &byrefInfo) {
  auto &cgm = cgf.cgm;
  auto &moduleBuilder = cgm.getBuilder();
  const CIRGenFunctionInfo &copyHelperInfo =
      getBlockByrefHelperFunctionInfo(cgm, /*isCopy=*/true);
  const CIRGenFunctionInfo &disposeHelperInfo =
      getBlockByrefHelperFunctionInfo(cgm, /*isCopy=*/false);

  cir::FuncOp copyHelper = createBlockByrefHelperFunction(
      cgm, loc, "__Block_byref_object_copy_", copyHelperInfo);
  cir::FuncOp disposeHelper = createBlockByrefHelperFunction(
      cgm, loc, "__Block_byref_object_dispose_", disposeHelperInfo);

  auto emitHelperBody = [&](cir::FuncOp fn, bool isCopy) {
    mlir::OpBuilder::InsertionGuard guard(moduleBuilder);
    mlir::Block *entry = fn.addEntryBlock();
    moduleBuilder.setInsertionPointToStart(entry);

    CIRGenFunction helperCGF(cgm, moduleBuilder, /*suppressNewContext=*/true);
    mlir::Type byrefPtrTy = moduleBuilder.getPointerTo(byrefInfo.type);

    mlir::Value dstBase = moduleBuilder.createBitcast(loc, fn.getArgument(0),
                                                      byrefPtrTy);
    Address dstField = helperCGF.emitBlockByrefAddress(
        Address(dstBase, byrefInfo.type, byrefInfo.byrefAlignment), var,
        /*followForward=*/false);

    if (isCopy) {
      mlir::Value srcBase = moduleBuilder.createBitcast(loc, fn.getArgument(1),
                                                        byrefPtrTy);
      Address srcField = helperCGF.emitBlockByrefAddress(
          Address(srcBase, byrefInfo.type, byrefInfo.byrefAlignment), var,
          /*followForward=*/false);
      helperCGF.emitARCMoveWeak(dstField, srcField);
    } else {
      helperCGF.emitARCDestroyWeak(dstField);
    }

    cir::ReturnOp::create(moduleBuilder, loc);
  };

  emitHelperBody(copyHelper, /*isCopy=*/true);
  emitHelperBody(disposeHelper, /*isCopy=*/false);
  return {copyHelper, disposeHelper};
}

static std::pair<cir::FuncOp, cir::FuncOp>
buildARCStrongBlockByrefHelpers(
    CIRGenFunction &cgf, mlir::Location loc, const VarDecl *var,
    const CIRGenFunction::BlockByrefInfo &byrefInfo) {
  auto &cgm = cgf.cgm;
  auto &moduleBuilder = cgm.getBuilder();
  const CIRGenFunctionInfo &copyHelperInfo =
      getBlockByrefHelperFunctionInfo(cgm, /*isCopy=*/true);
  const CIRGenFunctionInfo &disposeHelperInfo =
      getBlockByrefHelperFunctionInfo(cgm, /*isCopy=*/false);

  cir::FuncOp copyHelper = createBlockByrefHelperFunction(
      cgm, loc, "__Block_byref_object_copy_", copyHelperInfo);
  cir::FuncOp disposeHelper = createBlockByrefHelperFunction(
      cgm, loc, "__Block_byref_object_dispose_", disposeHelperInfo);

  auto emitHelperBody = [&](cir::FuncOp fn, bool isCopy) {
    mlir::OpBuilder::InsertionGuard guard(moduleBuilder);
    mlir::Block *entry = fn.addEntryBlock();
    moduleBuilder.setInsertionPointToStart(entry);

    CIRGenFunction helperCGF(cgm, moduleBuilder, /*suppressNewContext=*/true);
    mlir::Type byrefPtrTy = moduleBuilder.getPointerTo(byrefInfo.type);

    mlir::Value dstBase = moduleBuilder.createBitcast(loc, fn.getArgument(0),
                                                      byrefPtrTy);
    Address dstField = helperCGF.emitBlockByrefAddress(
        Address(dstBase, byrefInfo.type, byrefInfo.byrefAlignment), var,
        /*followForward=*/false);

    if (isCopy) {
      mlir::Value srcBase = moduleBuilder.createBitcast(loc, fn.getArgument(1),
                                                        byrefPtrTy);
      Address srcField = helperCGF.emitBlockByrefAddress(
          Address(srcBase, byrefInfo.type, byrefInfo.byrefAlignment), var,
          /*followForward=*/false);
      mlir::Value value = moduleBuilder.createAlignedLoad(
          loc, srcField.getElementType(), srcField.getPointer(),
          srcField.getAlignment());
      value =
          helperCGF.emitARCRetainBlock(value, loc, /*mandatory=*/true);
      moduleBuilder.createStore(loc, value, dstField);
    } else {
      helperCGF.emitARCDestroyStrong(dstField, loc, /*isImprecise=*/true);
    }

    cir::ReturnOp::create(moduleBuilder, loc);
  };

  emitHelperBody(copyHelper, /*isCopy=*/true);
  emitHelperBody(disposeHelper, /*isCopy=*/false);
  return {copyHelper, disposeHelper};
}

static std::pair<cir::FuncOp, cir::FuncOp>
buildCXXByrefHelpers(CIRGenFunction &cgf, mlir::Location loc,
                     const VarDecl *var,
                     const CIRGenFunction::BlockByrefInfo &byrefInfo) {
  auto &cgm = cgf.cgm;
  auto &moduleBuilder = cgm.getBuilder();
  const CIRGenFunctionInfo &copyHelperInfo =
      getBlockByrefHelperFunctionInfo(cgm, /*isCopy=*/true);
  const CIRGenFunctionInfo &disposeHelperInfo =
      getBlockByrefHelperFunctionInfo(cgm, /*isCopy=*/false);

  cir::FuncOp copyHelper = createBlockByrefHelperFunction(
      cgm, loc, "__Block_byref_object_copy_", copyHelperInfo);
  cir::FuncOp disposeHelper = createBlockByrefHelperFunction(
      cgm, loc, "__Block_byref_object_dispose_", disposeHelperInfo);

  const Expr *copyExpr = cgf.getContext().getBlockVarCopyInit(var).getCopyExpr();
  QualType type = var->getType();
  QualType::DestructionKind dtorKind = type.isDestructedType();

  auto emitHelperBody = [&](cir::FuncOp fn, bool isCopy) {
    mlir::OpBuilder::InsertionGuard guard(moduleBuilder);
    mlir::Block *entry = fn.addEntryBlock();
    moduleBuilder.setInsertionPointToStart(entry);

    CIRGenFunction helperCGF(cgm, moduleBuilder, /*suppressNewContext=*/true);
    mlir::Type byrefPtrTy = moduleBuilder.getPointerTo(byrefInfo.type);

    mlir::Value dstBase = moduleBuilder.createBitcast(loc, fn.getArgument(0),
                                                      byrefPtrTy);
    Address dstField = helperCGF.emitBlockByrefAddress(
        Address(dstBase, byrefInfo.type, byrefInfo.byrefAlignment), var,
        /*followForward=*/false);

    if (isCopy) {
      if (copyExpr) {
        const auto *copyCtorExpr =
            cast<CXXConstructExpr>(copyExpr->IgnoreImplicit());
        mlir::Value srcBase = moduleBuilder.createBitcast(loc, fn.getArgument(1),
                                                          byrefPtrTy);
        Address srcField = helperCGF.emitBlockByrefAddress(
            Address(srcBase, byrefInfo.type, byrefInfo.byrefAlignment), var,
            /*followForward=*/false);
        const auto *ctor = copyCtorExpr->getConstructor();
        const auto *ctorProto = ctor->getType()->castAs<FunctionProtoType>();
        // Use a RunCleanupsScope so temporaries created for default arguments
        // are destroyed after the constructor call, matching OGCG's
        // EmitSynthesizedCXXCopyCtor and the block-literal copy helper.
        CIRGenFunction::RunCleanupsScope scope(helperCGF);
        CallArgList args;
        args.add(RValue::get(dstField.getPointer()), ctor->getThisType());
        args.add(RValue::get(srcField.getPointer()),
                 ctor->getParamDecl(0)->getType());
        // Emit remaining (default) arguments from the CXXConstructExpr so a
        // copy-constructor with default parameters is called correctly.
        helperCGF.emitCallArgs(args, ctorProto,
                               drop_begin(copyCtorExpr->arguments(), 1),
                               copyCtorExpr->getConstructor(),
                               /*paramsToSkip=*/1);
        helperCGF.emitCXXConstructorCall(
            ctor, Ctor_Complete, /*forVirtualBase=*/false,
            /*delegating=*/false, dstField, args,
            AggValueSlot::DoesNotOverlap, copyCtorExpr->getExprLoc());
      }
    } else if (dtorKind != QualType::DK_none) {
      helperCGF.emitDestroy(dstField, type, helperCGF.getDestroyer(dtorKind),
                            /*useEHCleanupForArray=*/false);
    }

    cir::ReturnOp::create(moduleBuilder, loc);
  };

  emitHelperBody(copyHelper, /*isCopy=*/true);
  emitHelperBody(disposeHelper, /*isCopy=*/false);
  return {copyHelper, disposeHelper};
}

static std::pair<cir::FuncOp, cir::FuncOp>
buildNonTrivialCStructByrefHelpers(
    CIRGenFunction &cgf, mlir::Location loc, const VarDecl *var,
    const CIRGenFunction::BlockByrefInfo &byrefInfo) {
  auto &cgm = cgf.cgm;
  auto &moduleBuilder = cgm.getBuilder();
  auto voidPtrTy = moduleBuilder.getVoidPtrTy();
  const CIRGenFunctionInfo &copyHelperInfo =
      getBlockByrefHelperFunctionInfo(cgm, /*isCopy=*/true);
  const CIRGenFunctionInfo &disposeHelperInfo =
      getBlockByrefHelperFunctionInfo(cgm, /*isCopy=*/false);

  cir::FuncOp copyHelper = createBlockByrefHelperFunction(
      cgm, loc, "__Block_byref_object_copy_", copyHelperInfo);
  cir::FuncOp disposeHelper = createBlockByrefHelperFunction(
      cgm, loc, "__Block_byref_object_dispose_", disposeHelperInfo);

  auto emitHelperBody = [&](cir::FuncOp fn, bool isCopy) {
    mlir::OpBuilder::InsertionGuard guard(moduleBuilder);
    mlir::Block *entry = fn.addEntryBlock();
    moduleBuilder.setInsertionPointToStart(entry);

    CIRGenFunction helperCGF(cgm, moduleBuilder, /*suppressNewContext=*/true);
    mlir::Type byrefPtrTy = moduleBuilder.getPointerTo(byrefInfo.type);

    mlir::Value dstBase = moduleBuilder.createBitcast(loc, fn.getArgument(0),
                                                      byrefPtrTy);
    Address dstField = helperCGF.emitBlockByrefAddress(
        Address(dstBase, byrefInfo.type, byrefInfo.byrefAlignment), var,
        /*followForward=*/false);

    if (isCopy) {
      mlir::Value srcBase = moduleBuilder.createBitcast(loc, fn.getArgument(1),
                                                        byrefPtrTy);
      Address srcField = helperCGF.emitBlockByrefAddress(
          Address(srcBase, byrefInfo.type, byrefInfo.byrefAlignment), var,
          /*followForward=*/false);
      cir::FuncOp moveCtor = getOrCreateNonTrivialCStructMoveConstructorThunk(
          helperCGF, loc, var->getType(), dstField.getAlignment(),
          srcField.getAlignment());
      mlir::Value dstPtr = dstField.getPointer();
      if (dstPtr.getType() != voidPtrTy)
        dstPtr = moduleBuilder.createBitcast(loc, dstPtr, voidPtrTy);
      mlir::Value srcPtr = srcField.getPointer();
      if (srcPtr.getType() != voidPtrTy)
        srcPtr = moduleBuilder.createBitcast(loc, srcPtr, voidPtrTy);
      moduleBuilder.createCallOp(loc, moveCtor, {dstPtr, srcPtr});
    } else {
      cir::FuncOp dtor = getOrCreateNonTrivialCStructDestructorThunk(
          helperCGF, loc, var->getType(), dstField.getAlignment());
      mlir::Value dstPtr = dstField.getPointer();
      if (dstPtr.getType() != voidPtrTy)
        dstPtr = moduleBuilder.createBitcast(loc, dstPtr, voidPtrTy);
      moduleBuilder.createCallOp(loc, dtor, {dstPtr});
    }

    cir::ReturnOp::create(moduleBuilder, loc);
  };

  emitHelperBody(copyHelper, /*isCopy=*/true);
  emitHelperBody(disposeHelper, /*isCopy=*/false);
  return {copyHelper, disposeHelper};
}

static std::pair<cir::FuncOp, cir::FuncOp>
buildAddressDiscriminatedByrefHelpers(
    CIRGenFunction &cgf, mlir::Location loc, const VarDecl *var,
    const CIRGenFunction::BlockByrefInfo &byrefInfo) {
  auto &cgm = cgf.cgm;
  auto &moduleBuilder = cgm.getBuilder();
  const CIRGenFunctionInfo &copyHelperInfo =
      getBlockByrefHelperFunctionInfo(cgm, /*isCopy=*/true);
  const CIRGenFunctionInfo &disposeHelperInfo =
      getBlockByrefHelperFunctionInfo(cgm, /*isCopy=*/false);

  cir::FuncOp copyHelper = createBlockByrefHelperFunction(
      cgm, loc, "__Block_byref_object_copy_", copyHelperInfo);
  cir::FuncOp disposeHelper = createBlockByrefHelperFunction(
      cgm, loc, "__Block_byref_object_dispose_", disposeHelperInfo);

  PointerAuthQualifier qualifier = var->getType().getPointerAuth();
  assert(qualifier && qualifier.isAddressDiscriminated());

  {
    mlir::OpBuilder::InsertionGuard guard(moduleBuilder);
    mlir::Block *entry = copyHelper.addEntryBlock();
    moduleBuilder.setInsertionPointToStart(entry);

    CIRGenFunction helperCGF(cgm, moduleBuilder, /*suppressNewContext=*/true);
    mlir::Type byrefPtrTy = moduleBuilder.getPointerTo(byrefInfo.type);

    mlir::Value dstBase = moduleBuilder.createBitcast(loc, copyHelper.getArgument(0),
                                                      byrefPtrTy);
    Address dstField = helperCGF.emitBlockByrefAddress(
        Address(dstBase, byrefInfo.type, byrefInfo.byrefAlignment), var,
        /*followForward=*/false);

    mlir::Value srcBase = moduleBuilder.createBitcast(loc, copyHelper.getArgument(1),
                                                      byrefPtrTy);
    Address srcField = helperCGF.emitBlockByrefAddress(
        Address(srcBase, byrefInfo.type, byrefInfo.byrefAlignment), var,
        /*followForward=*/false);
    helperCGF.emitPointerAuthCopy(qualifier, var->getType(), dstField, srcField);
    cir::ReturnOp::create(moduleBuilder, loc);
  }

  {
    mlir::OpBuilder::InsertionGuard guard(moduleBuilder);
    mlir::Block *entry = disposeHelper.addEntryBlock();
    moduleBuilder.setInsertionPointToStart(entry);
    cir::ReturnOp::create(moduleBuilder, loc);
  }

  return {copyHelper, disposeHelper};
}

static std::pair<cir::FuncOp, cir::FuncOp>
buildObjectByrefHelpers(CIRGenFunction &cgf, mlir::Location loc,
                        const VarDecl *var,
                        const CIRGenFunction::BlockByrefInfo &byrefInfo) {
  auto &cgm = cgf.cgm;
  auto &moduleBuilder = cgm.getBuilder();
  auto voidTy = moduleBuilder.getVoidTy();
  auto voidPtrTy = moduleBuilder.getVoidPtrTy();
  const CIRGenFunctionInfo &copyHelperInfo =
      getBlockByrefHelperFunctionInfo(cgm, /*isCopy=*/true);
  const CIRGenFunctionInfo &disposeHelperInfo =
      getBlockByrefHelperFunctionInfo(cgm, /*isCopy=*/false);

  cir::FuncOp copyHelper = createBlockByrefHelperFunction(
      cgm, loc, "__Block_byref_object_copy_", copyHelperInfo);
  cir::FuncOp disposeHelper = createBlockByrefHelperFunction(
      cgm, loc, "__Block_byref_object_dispose_", disposeHelperInfo);

  uint32_t flags = var->getType()->isBlockPointerType() ? kBlockFieldIsBlock
                                                        : kBlockFieldIsObject;
  if (var->getType().isObjCGCWeak())
    flags |= kBlockFieldIsWeak;
  flags |= kBlockByrefCaller;

  auto emitHelperBody = [&](cir::FuncOp fn, bool isCopy) {
    mlir::OpBuilder::InsertionGuard guard(moduleBuilder);
    mlir::Block *entry = fn.addEntryBlock();
    moduleBuilder.setInsertionPointToStart(entry);

    CIRGenFunction helperCGF(cgm, moduleBuilder, /*suppressNewContext=*/true);
    mlir::Type byrefPtrTy = moduleBuilder.getPointerTo(byrefInfo.type);
    auto int32Ty = moduleBuilder.getSInt32Ty();

    mlir::Value dstBase = moduleBuilder.createBitcast(loc, fn.getArgument(0),
                                                      byrefPtrTy);
    Address dstField = helperCGF.emitBlockByrefAddress(
        Address(dstBase, byrefInfo.type, byrefInfo.byrefAlignment), var,
        /*followForward=*/false);

    cir::FuncOp runtimeFn = cgm.createRuntimeFunction(
        isCopy ? cir::FuncType::get({voidPtrTy, voidPtrTy, int32Ty}, voidTy)
               : cir::FuncType::get({voidPtrTy, int32Ty}, voidTy),
        isCopy ? "_Block_object_assign" : "_Block_object_dispose");

    mlir::Value flagsValue =
        moduleBuilder.getConstInt(loc, int32Ty, flags);

    if (isCopy) {
      mlir::Value srcBase = moduleBuilder.createBitcast(loc, fn.getArgument(1),
                                                        byrefPtrTy);
      Address srcField = helperCGF.emitBlockByrefAddress(
          Address(srcBase, byrefInfo.type, byrefInfo.byrefAlignment), var,
          /*followForward=*/false);
      mlir::Value dstPtr = dstField.getPointer();
      if (dstPtr.getType() != voidPtrTy)
        dstPtr = moduleBuilder.createBitcast(loc, dstPtr, voidPtrTy);
      mlir::Value srcValue = moduleBuilder.createAlignedLoad(
          loc, voidPtrTy, srcField.getPointer(), srcField.getAlignment());
      helperCGF.emitRuntimeCall(loc, runtimeFn, {dstPtr, srcValue, flagsValue});
    } else {
      mlir::Value fieldValue = moduleBuilder.createAlignedLoad(
          loc, voidPtrTy, dstField.getPointer(), dstField.getAlignment());
      helperCGF.emitRuntimeCall(loc, runtimeFn, {fieldValue, flagsValue});
    }

    cir::ReturnOp::create(moduleBuilder, loc);
  };

  emitHelperBody(copyHelper, /*isCopy=*/true);
  emitHelperBody(disposeHelper, /*isCopy=*/false);
  return {copyHelper, disposeHelper};
}
}

static void
setPragmaClangSectionAttrs(const VarDecl &d, cir::GlobalOp globalOp,
                           CIRGenModule &cgm) {
  if (const auto *sa = d.getAttr<PragmaClangBSSSectionAttr>())
    globalOp->setAttr("cir.pragma_clang_bss_section",
                      cgm.getStringAttrCached(sa->getName()));
  if (const auto *sa = d.getAttr<PragmaClangDataSectionAttr>())
    globalOp->setAttr("cir.pragma_clang_data_section",
                      cgm.getStringAttrCached(sa->getName()));
  if (const auto *sa = d.getAttr<PragmaClangRodataSectionAttr>())
    globalOp->setAttr("cir.pragma_clang_rodata_section",
                      cgm.getStringAttrCached(sa->getName()));
  if (const auto *sa = d.getAttr<PragmaClangRelroSectionAttr>())
    globalOp->setAttr("cir.pragma_clang_relro_section",
                      cgm.getStringAttrCached(sa->getName()));
}

/// Cleanup that emits a cir.lifetime.end marker when a variable goes out of
/// scope (OGCG CGDecl.cpp:567-574).
struct CallLifetimeEnd final : EHScopeStack::Cleanup {
  bool isRedundantBeforeReturn() override { return true; }
  Address addr;
  CallLifetimeEnd(Address addr) : addr(addr) {}
  void emit(CIRGenFunction &cgf, Flags) override {
    auto &builder = cgf.getBuilder();
    // In CIR's structured control flow, a block may already have a terminator
    // (e.g. cir.yield) when cleanups fire during scope exit. We must insert
    // the lifetime.end before any existing terminator to preserve the
    // invariant that terminators are the last operation in a block.
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *block = builder.getInsertionBlock();
    if (block && block->mightHaveTerminator() && block->getTerminator())
      builder.setInsertionPoint(block->getTerminator());
    builder.createLifetimeEnd(addr.getPointer().getLoc(),
                              addr.getPointer());
  }
};

static bool isOpenMPAllocatableDecl(const VarDecl *vd) {
  const VarDecl *canonical = vd->getCanonicalDecl();
  if (!canonical->hasAttr<OMPAllocateDeclAttr>())
    return false;
  const auto *attr = canonical->getAttr<OMPAllocateDeclAttr>();
  return !(attr->getAllocatorType() == OMPAllocateDeclAttr::OMPDefaultMemAlloc &&
           !attr->getAllocator());
}

static std::optional<CharUnits>
getOpenMPAllocateAlignment(CIRGenFunction &cgf, const VarDecl *vd) {
  if (const auto *attr = vd->getAttr<OMPAllocateDeclAttr>()) {
    if (Expr *alignment = attr->getAlignment()) {
      unsigned userAlign =
          alignment->EvaluateKnownConstInt(cgf.getContext()).getExtValue();
      CharUnits naturalAlign = cgf.getCIRGenModule().getNaturalTypeAlignment(
          vd->getType().getNonReferenceType(), /*baseInfo=*/nullptr);
      return CharUnits::fromQuantity(
          std::max<unsigned>(userAlign, naturalAlign.getQuantity()));
    }
  }
  return std::nullopt;
}

static mlir::Value getOpenMPAllocatorValue(CIRGenFunction &cgf,
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

struct CallOpenMPAllocateCleanup final : EHScopeStack::Cleanup {
  cir::FuncOp freeFn;
  SourceLocation::UIntTy locEncoding;
  Address addr;
  const Expr *allocatorExpr;

  CallOpenMPAllocateCleanup(cir::FuncOp freeFn,
                            SourceLocation::UIntTy locEncoding, Address addr,
                            const Expr *allocatorExpr)
      : freeFn(freeFn), locEncoding(locEncoding), addr(addr),
        allocatorExpr(allocatorExpr) {}

  void emit(CIRGenFunction &cgf, Flags) override {
    if (!cgf.haveInsertPoint())
      return;

    SourceLocation sourceLoc = SourceLocation::getFromRawEncoding(locEncoding);
    mlir::Location loc =
        sourceLoc.isValid() ? cgf.getLoc(sourceLoc) : addr.getPointer().getLoc();
    mlir::Value threadID = cgf.emitOpenMPThreadID(sourceLoc, 0);
    mlir::Value ptr = addr.getPointer();
    if (ptr.getType() != cgf.getBuilder().getVoidPtrTy())
      ptr = cgf.getBuilder().createBitcast(loc, ptr,
                                           cgf.getBuilder().getVoidPtrTy());
    mlir::Value allocatorValue =
        getOpenMPAllocatorValue(cgf, loc, allocatorExpr);
    cgf.emitRuntimeCall(loc, freeFn, {threadID, ptr, allocatorValue});
  }
};

struct CallOpenMPTargetSharedCleanup final : EHScopeStack::Cleanup {
  cir::FuncOp freeFn;
  Address addr;
  mlir::Value sizeValue;

  CallOpenMPTargetSharedCleanup(cir::FuncOp freeFn, Address addr,
                                mlir::Value sizeValue)
      : freeFn(freeFn), addr(addr), sizeValue(sizeValue) {}

  void emit(CIRGenFunction &cgf, Flags) override {
    if (!cgf.haveInsertPoint())
      return;

    mlir::Location loc = addr.getPointer().getLoc();
    mlir::Value ptr = addr.getPointer();
    if (ptr.getType() != cgf.getBuilder().getVoidPtrTy())
      ptr = cgf.getBuilder().createBitcast(loc, ptr,
                                           cgf.getBuilder().getVoidPtrTy());
    cgf.emitRuntimeCall(loc, freeFn, {ptr, sizeValue});
  }
};

static Address maybeEmitOpenMPAllocatedLocalAddress(CIRGenFunction &cgf,
                                                    const VarDecl &d,
                                                    QualType ty,
                                                    CharUnits alignment,
                                                    mlir::Location loc) {
  if (!cgf.getLangOpts().OpenMP || !isOpenMPAllocatableDecl(&d))
    return Address::invalid();

  const VarDecl *canonical = d.getCanonicalDecl();
  const auto *attr = canonical->getAttr<OMPAllocateDeclAttr>();
  assert(attr && "expected OpenMP allocate attribute");

  CIRGenBuilderTy &builder = cgf.getBuilder();
  auto sizeIntTy = mlir::cast<cir::IntType>(cgf.sizeTy);

  auto getSizeValue = [&]() -> mlir::Value {
    if (ty->isVariablyModifiedType()) {
      CIRGenFunction::VlaSizePair vlaSize = cgf.getVLASize(ty);
      mlir::Value sizeValue = vlaSize.numElts;
      if (sizeValue.getType() != cgf.sizeTy)
        sizeValue = builder.createIntCast(sizeValue, cgf.sizeTy);
      CharUnits elementSize = cgf.getContext().getTypeSizeInChars(vlaSize.type);
      if (!elementSize.isOne()) {
        mlir::Value elementSizeValue = builder.getConstInt(
            loc, sizeIntTy, elementSize.getQuantity());
        sizeValue = builder.createNUWAMul(loc, sizeValue, elementSizeValue);
      }
      mlir::Value alignMinusOne =
          builder.getConstInt(loc, sizeIntTy, alignment.getQuantity() - 1);
      mlir::Value alignValue =
          builder.getConstInt(loc, sizeIntTy, alignment.getQuantity());
      sizeValue = builder.createNUWAdd(loc, sizeValue, alignMinusOne);
      sizeValue = builder.createBinop(loc, sizeValue, cir::BinOpKind::Div,
                                      alignValue);
      return builder.createNUWAMul(loc, sizeValue, alignValue);
    }

    CharUnits size = cgf.getContext().getTypeSizeInChars(ty);
    return builder.getConstInt(loc, sizeIntTy, size.alignTo(alignment).getQuantity());
  };

  mlir::Value sizeValue = getSizeValue();
  mlir::Value threadID = cgf.emitOpenMPThreadID(d.getBeginLoc(), 0);
  mlir::Value allocatorValue =
      getOpenMPAllocatorValue(cgf, loc, attr->getAllocator());

  std::optional<CharUnits> allocateAlignment =
      getOpenMPAllocateAlignment(cgf, canonical);
  llvm::SmallVector<mlir::Value, 4> args{threadID};
  cir::FuncOp allocFn;
  if (allocateAlignment) {
    args.push_back(
        builder.getConstInt(loc, sizeIntTy, allocateAlignment->getQuantity()));
    args.push_back(sizeValue);
    args.push_back(allocatorValue);
    allocFn = cgf.getCIRGenModule().createRuntimeFunction(
        builder.getFuncType(
            {builder.getSInt32Ty(), cgf.sizeTy, cgf.sizeTy,
             builder.getVoidPtrTy()},
            builder.getVoidPtrTy()),
        "__kmpc_aligned_alloc");
    setRuntimeFnNothrow(allocFn);
    allocFn.setResultAttr(0, "cir.noalias",
                          mlir::UnitAttr::get(allocFn.getContext()));
  } else {
    args.push_back(sizeValue);
    args.push_back(allocatorValue);
    allocFn = cgf.getCIRGenModule().createRuntimeFunction(
        builder.getFuncType(
            {builder.getSInt32Ty(), cgf.sizeTy, builder.getVoidPtrTy()},
            builder.getVoidPtrTy()),
        "__kmpc_alloc");
    setRuntimeFnNothrow(allocFn);
    allocFn.setResultAttr(0, "cir.noalias",
                          mlir::UnitAttr::get(allocFn.getContext()));
  }

  mlir::Value allocatedPtr = cgf.emitRuntimeCall(loc, allocFn, args);
  mlir::Type valueTy = cgf.convertTypeForMem(canonical->getType());
  auto valuePtrTy = builder.getPointerTo(valueTy);
  if (allocatedPtr.getType() != valuePtrTy)
    allocatedPtr = builder.createBitcast(loc, allocatedPtr, valuePtrTy);

  cir::FuncOp freeFn = cgf.getCIRGenModule().createRuntimeFunction(
      builder.getFuncType(
          {builder.getSInt32Ty(), builder.getVoidPtrTy(), builder.getVoidPtrTy()},
          builder.getVoidTy()),
      "__kmpc_free");
  setRuntimeFnNothrow(freeFn);
  Address addr(allocatedPtr, valueTy, alignment);
  cgf.ehStack.pushCleanup<CallOpenMPAllocateCleanup>(
      NormalAndEHCleanup, freeFn, canonical->getLocation().getRawEncoding(),
      addr, attr->getAllocator());
  return addr;
}

static Address maybeEmitOpenMPTargetSharedLocalAddress(CIRGenFunction &cgf,
                                                       const VarDecl &d,
                                                       QualType ty,
                                                       CharUnits alignment,
                                                       mlir::Location loc) {
  if (!cgf.shouldUseOpenMPTargetSharedLocalStorage(d) ||
      isOpenMPAllocatableDecl(&d) || ty->isVariablyModifiedType())
    return Address::invalid();

  CIRGenBuilderTy &builder = cgf.getBuilder();
  auto sizeIntTy = mlir::cast<cir::IntType>(cgf.sizeTy);
  mlir::Value sizeValue =
      builder.getConstInt(loc, sizeIntTy,
                          cgf.getContext().getTypeSizeInChars(ty).getQuantity());

  cir::FuncOp allocFn = cgf.getCIRGenModule().createRuntimeFunction(
      builder.getFuncType({cgf.sizeTy}, builder.getVoidPtrTy()),
      "__kmpc_alloc_shared");
  setRuntimeFnNothrow(allocFn);
  allocFn.setResultAttr(
      0, "cir.align",
      cgf.getCIRGenModule().getI64AttrCached(
          cgf.getContext().getTargetInfo().getNewAlign() / 8));

  mlir::Value allocatedPtr = cgf.emitRuntimeCall(loc, allocFn, {sizeValue});
  mlir::Type valueTy = cgf.convertTypeForMem(ty);
  auto valuePtrTy = builder.getPointerTo(valueTy);
  if (allocatedPtr.getType() != valuePtrTy)
    allocatedPtr = builder.createBitcast(loc, allocatedPtr, valuePtrTy);

  cir::FuncOp freeFn = cgf.getCIRGenModule().createRuntimeFunction(
      builder.getFuncType({builder.getVoidPtrTy(), cgf.sizeTy},
                          builder.getVoidTy()),
      "__kmpc_free_shared");
  setRuntimeFnNothrow(freeFn);

  Address addr(allocatedPtr, valueTy, alignment);
  cgf.ehStack.pushCleanup<CallOpenMPTargetSharedCleanup>(
      NormalAndEHCleanup, freeFn, addr, sizeValue);
  return addr;
}

CIRGenFunction::AutoVarEmission
CIRGenFunction::emitAutoVarAlloca(const VarDecl &d,
                                  mlir::OpBuilder::InsertPoint ip) {
  QualType ty = d.getType();
  if (ty.getAddressSpace() != LangAS::Default &&
      !(ty.getAddressSpace() == LangAS::opencl_private &&
        getContext().getLangOpts().OpenCL)) {
    cgm.errorNYI(d.getSourceRange(), "emitAutoVarAlloca: address space");
    return AutoVarEmission::invalid();
  }

  mlir::Location loc = getAutoVarDebugLoc(*this, d);
  bool nrvo =
      getContext().getLangOpts().ElideConstructors && d.isNRVOVariable();

  CIRGenFunction::AutoVarEmission emission(d);
  emission.isEscapingByRef = d.isEscapingByref();

  CharUnits alignment = getContext().getDeclAlign(&d);

  // If the type is variably-modified, emit all the VLA sizes for it.
  if (ty->isVariablyModifiedType())
    emitVariablyModifiedType(ty);

  Address address = Address::invalid();
  if (Address openMPTargetSharedAddr =
          maybeEmitOpenMPTargetSharedLocalAddress(*this, d, ty, alignment, loc);
      openMPTargetSharedAddr.isValid()) {
    address = openMPTargetSharedAddr;
  } else if (Address openMPAddr =
          maybeEmitOpenMPAllocatedLocalAddress(*this, d, ty, alignment, loc);
      openMPAddr.isValid()) {
    address = openMPAddr;
  } else if (ty->isConstantSizeType()) {
    // If this value is an array, struct, or vector with a statically
    // determinable constant initializer, there are optimizations we can do.
    //
    // TODO: We should constant-evaluate the initializer of any variable,
    // as long as it is initialized by a constant expression. Currently,
    // isConstantInitializer produces wrong answers for structs with
    // reference or bitfield members, and a few other cases, and checking
    // for POD-ness protects us from some of these.
    if (d.getInit() &&
        (ty->isArrayType() || ty->isRecordType() || ty->isVectorType()) &&
        (d.isConstexpr() ||
         ((ty.isPODType(getContext()) ||
           getContext().getBaseElementType(ty)->isObjCObjectPointerType()) &&
          d.getInit()->isConstantInitializer(getContext(), false)))) {

      // If the variable's a const type, and it's neither an NRVO
      // candidate nor a __block variable and has no mutable members,
      // emit it as a global instead.
      // Exception is if a variable is located in non-constant address space
      // in OpenCL.
      // TODO(cir): perhaps we don't need this at all at CIR since this can
      // be done as part of lowering down to LLVM.
      bool needsDtor =
          d.needsDestruction(getContext()) == QualType::DK_cxx_destructor;
      if ((!getContext().getLangOpts().OpenCL ||
           ty.getAddressSpace() == LangAS::opencl_constant) &&
          (cgm.getCodeGenOpts().MergeAllConstants && !nrvo &&
           !d.isEscapingByref() &&
           ty.isConstantStorage(getContext(), true, !needsDtor))) {
        emitStaticVarDecl(d, cir::GlobalLinkageKind::InternalLinkage);

        // Signal this condition to later callbacks.
        emission.addr = Address::invalid();
        assert(emission.wasEmittedAsGlobal());
        return emission;
      }
      // Otherwise, tell the initialization code that we're in this case.
      emission.isConstantAggregate = true;
    }

    // A normal fixed sized variable becomes an alloca in the entry block,
    // unless:
    // - it's an NRVO variable.
    // - we are compiling OpenMP and it's an OpenMP local variable.
    if (nrvo) {
      // The named return value optimization: allocate this variable in the
      // return slot, so that we can elide the copy when returning this
      // variable (C++0x [class.copy]p34).
      address = returnValue;

      if (const RecordDecl *rd = ty->getAsRecordDecl()) {
        if (const auto *cxxrd = dyn_cast<CXXRecordDecl>(rd);
            (cxxrd && !cxxrd->hasTrivialDestructor()) ||
            rd->isNonTrivialToPrimitiveDestroy()) {
          // In LLVM: Create a flag that is used to indicate when the NRVO was
          // applied to this variable. Set it to zero to indicate that NRVO was
          // not applied. For now, use the same approach for CIRGen until we can
          // be sure it's worth doing something more aggressive.
          cir::ConstantOp falseNVRO = builder.getFalse(loc);
          Address nrvoFlag = createTempAlloca(falseNVRO.getType(),
                                              CharUnits::One(), loc, "nrvo",
                                              /*arraySize=*/nullptr);
          assert(builder.getInsertionBlock());
          builder.createStore(loc, falseNVRO, nrvoFlag);

          // Record the NRVO flag for this variable.
          nrvoFlags[&d] = nrvoFlag.getPointer();
          emission.nrvoFlag = nrvoFlag.getPointer();
        }
      }
    } else {
      // A normal fixed sized variable becomes an alloca in the entry block,
      mlir::Type allocaTy = convertTypeForMem(ty);
      CharUnits allocaAlignment = alignment;
      if (emission.isEscapingByRef) {
        const auto &byrefInfo = getBlockByrefInfo(&d);
        allocaTy = byrefInfo.type;
        allocaAlignment = byrefInfo.byrefAlignment;
      }
      // Create the temp alloca and declare variable using it.
      // allocaAddr gets the raw alloca (in alloca address space), while
      // address gets the address-space-casted value for the destination AS.
      // The destination AS comes from the type: in OpenCL CL1.2 locals are
      // in the private AS (matching the alloca AS, so no cast is needed),
      // while in CL2.0+ and C++ the default is generic (requiring a cast).
      Address allocaAddr = Address::invalid();
      address = createTempAlloca(allocaTy, allocaAlignment, loc, d.getName(),
                                 /*arraySize=*/nullptr, &allocaAddr, ip,
                                 ty.getAddressSpace());
      declare(allocaAddr.getPointer(), &d, ty, loc, allocaAlignment);

      // Emit lifetime marker if appropriate (OGCG CGDecl.cpp:1607-1636).
      // OGCG also skips lifetime markers for MSVC catch params.
      // MSVC/Windows is not a supported CIR target.
      if (shouldEmitLifetimeMarkers && !bypasses.IsBypassed(&d) &&
          !(!getLangOpts().CPlusPlus && hasLabelBeenSeenInCurrentScope())) {
        builder.createLifetimeStart(loc, allocaAddr.getPointer());
        emission.useLifetimeMarkers = true;
        emission.allocaAddr = allocaAddr;
      }
    }
  } else {
    // Non-constant size type
    // OpenMP-specific VLA/local allocation handling remains tracked in focused
    // OpenMP declaration/codegen paths.
    if (!didCallStackSave) {
      // Save the stack.
      cir::PointerType defaultTy = allocaInt8PtrTy;
      CharUnits align = CharUnits::fromQuantity(
          cgm.getDataLayout().getAlignment(defaultTy, false));
      Address stack = createTempAlloca(defaultTy, align, loc, "saved_stack");

      mlir::Value v = builder.createStackSave(loc, defaultTy);
      assert(v.getType() == allocaInt8PtrTy);
      builder.createStore(loc, v, stack);

      didCallStackSave = true;

      // Push a cleanup block and restore the stack there.
      // FIXME: in general circumstances, this should be an EH cleanup.
      pushStackRestore(NormalCleanup, stack);
    }

    VlaSizePair vlaSize = getVLASize(ty);
    mlir::Type memTy = convertTypeForMem(vlaSize.type);

    // Allocate memory for the array.
    address =
        createTempAlloca(memTy, alignment, loc, d.getName(), vlaSize.numElts,
                         /*alloca=*/nullptr, builder.saveInsertionPoint());

    // If we have debug info enabled, properly describe the VLA dimensions for
    // this type by registering the vla size expression for each of the
    // dimensions.
  }

  emission.addr = address;
  setAddrOfLocalVar(&d, address);

  if (d.hasAttr<AnnotateAttr>())
    emitVarAnnotations(&d, address.emitRawPointer());

  // Push lifetime.end cleanup (OGCG CGDecl.cpp:1724-1726).
  if (emission.useLifetimeMarkers)
    ehStack.pushCleanup<CallLifetimeEnd>(NormalEHLifetimeMarker,
                                         emission.getOriginalAllocatedAddress());

  // Emit fake.use cleanup for -fextend-variable-liveness (OGCG CGDecl.cpp:1728-1738).
  // When enabled, local variables and parameters both get an end-of-scope
  // fake.use to keep them live for debugging.
  pushFakeUseCleanupForVar(d, emission.getAllocatedAddress());

  return emission;
}

/// Determine whether the given initializer is trivial in the sense
/// that it requires no code to be generated.
bool CIRGenFunction::isTrivialInitializer(const Expr *init) {
  if (!init)
    return true;

  if (const CXXConstructExpr *construct = dyn_cast<CXXConstructExpr>(init))
    if (CXXConstructorDecl *constructor = construct->getConstructor())
      if (constructor->isTrivial() && constructor->isDefaultConstructor() &&
          !construct->requiresZeroInitialization())
        return true;

  return false;
}

/// Get a pattern initialization constant for the given CIR type.
/// This mirrors OGCG's initializationPatternFor() in PatternInit.cpp.
/// Used by -ftrivial-auto-var-init=pattern to fill variables with
/// distinctive values that make use-of-uninitialized bugs easier to detect.
static mlir::TypedAttr getPatternPaddingAttr(CIRGenModule &cgm,
                                             CIRGenBuilderTy &builder,
                                             uint64_t size);

static mlir::TypedAttr getPatternInitAttr(CIRGenModule &cgm,
                                          CIRGenBuilderTy &builder,
                                          mlir::Type ty) {
  // Pattern value: 0xAA repeated on 64-bit targets, 0xFF on 32-bit.
  const uint64_t intValue =
      cgm.getASTContext().getTargetInfo().getMaxPointerWidth() < 64
          ? 0xFFFFFFFFFFFFFFFFull
          : 0xAAAAAAAAAAAAAAAAull;

  if (auto intTy = mlir::dyn_cast<cir::IntType>(ty)) {
    unsigned bitWidth = intTy.getWidth();
    if (bitWidth <= 64)
      return cir::IntAttr::get(
          ty, llvm::APInt(bitWidth, intValue, /*isSigned=*/false,
                          /*implicitTrunc=*/true));
    return cir::IntAttr::get(
        ty, llvm::APInt::getSplat(bitWidth, llvm::APInt(64, intValue)));
  }

  // Pattern-init writes the full storage byte for bools, not the abstract
  // cir.bool value. This matches OGCG's i8 0xAA/0xFF pattern in memory.
  if (mlir::isa<cir::BoolType>(ty))
    return getPatternInitAttr(cgm, builder, cgm.uCharTy);

  if (auto ptrTy = mlir::dyn_cast<cir::PointerType>(ty))
    return builder.getConstPtrAttr(
        ptrTy, static_cast<int64_t>(intValue));

  if (cir::isAnyFloatingPointType(ty)) {
    // Floating-point pattern: negative quiet NaN with all-ones payload.
    // This propagates through FP operations and is distinctive in crashes.
    auto &semantics =
        mlir::cast<cir::FPTypeInterface>(ty).getFloatSemantics();
    unsigned bitWidth = llvm::APFloat::semanticsSizeInBits(semantics);
    llvm::APInt payload(64, 0xFFFFFFFFFFFFFFFFull);
    if (bitWidth >= 64)
      payload = llvm::APInt::getSplat(bitWidth, payload);
    llvm::APFloat nan =
        llvm::APFloat::getQNaN(semantics, /*Negative=*/true, &payload);
    return cir::FPAttr::get(ty, nan);
  }

  if (auto arrTy = mlir::dyn_cast<cir::ArrayType>(ty)) {
    mlir::TypedAttr elemAttr =
        getPatternInitAttr(cgm, builder, arrTy.getElementType());
    auto patternArrTy =
        elemAttr.getType() == arrTy.getElementType()
            ? arrTy
            : cir::ArrayType::get(elemAttr.getType(), arrTy.getSize());
    llvm::SmallVector<mlir::Attribute> elts(arrTy.getSize(), elemAttr);
    return cir::ConstArrayAttr::get(patternArrTy, mlir::ArrayAttr::get(
                                                      builder.getContext(),
                                                      elts));
  }

  if (auto recordTy = mlir::dyn_cast<cir::RecordType>(ty)) {
    llvm::SmallVector<mlir::Attribute> members;

    // Unions already model their storage choice in the CIR record type.
    // Keep that representation and only rewrite structs/classes to make
    // implicit padding bytes explicit.
    if (recordTy.isUnion() ||
        llvm::any_of(recordTy.getMembers(),
                     [](mlir::Type memberTy) { return !cir::isSized(memberTy); })) {
      bool changed = false;
      for (mlir::Type memberTy : recordTy.getMembers()) {
        mlir::TypedAttr memberAttr = getPatternInitAttr(cgm, builder, memberTy);
        changed |= memberAttr.getType() != memberTy;
        members.push_back(memberAttr);
      }
      auto membersAttr =
          mlir::ArrayAttr::get(builder.getContext(), members);
      if (!changed)
        return cir::ConstRecordAttr::get(recordTy, membersAttr);

      llvm::SmallVector<mlir::Type> memberTypes;
      memberTypes.reserve(members.size());
      for (mlir::Attribute member : members)
        memberTypes.push_back(mlir::cast<mlir::TypedAttr>(member).getType());

      auto rebuilt = cir::RecordType::get(
          builder.getContext(), memberTypes, recordTy.getPacked(),
          recordTy.getPadded(), recordTy.getKind());
      return cir::ConstRecordAttr::get(rebuilt, membersAttr);
    }

    cir::CIRDataLayout layout{cgm.getModule()};
    bool changed = false;
    bool insertedPadding = false;
    uint64_t sizeSoFar = 0;

    for (auto [idx, memberTy] : llvm::enumerate(recordTy.getMembers())) {
      uint64_t memberOffset = recordTy.getElementOffset(layout.layout, idx);
      if (sizeSoFar < memberOffset) {
        members.push_back(
            getPatternPaddingAttr(cgm, builder, memberOffset - sizeSoFar));
        sizeSoFar = memberOffset;
        changed = true;
        insertedPadding = true;
      }

      mlir::TypedAttr memberAttr = getPatternInitAttr(cgm, builder, memberTy);
      changed |= memberAttr.getType() != memberTy;
      members.push_back(memberAttr);
      sizeSoFar = memberOffset + layout.getTypeAllocSize(memberAttr.getType());
    }

    uint64_t totalSize = layout.getTypeAllocSize(recordTy);
    if (sizeSoFar < totalSize) {
      members.push_back(
          getPatternPaddingAttr(cgm, builder, totalSize - sizeSoFar));
      changed = true;
      insertedPadding = true;
    }

    auto membersAttr = mlir::ArrayAttr::get(builder.getContext(), members);
    if (!changed)
      return cir::ConstRecordAttr::get(recordTy, membersAttr);

    llvm::SmallVector<mlir::Type> memberTypes;
    memberTypes.reserve(members.size());
    for (mlir::Attribute member : members)
      memberTypes.push_back(mlir::cast<mlir::TypedAttr>(member).getType());

    auto explicitRecordTy =
        cir::RecordType::get(builder.getContext(), memberTypes,
                             recordTy.getPacked(),
                             recordTy.getPadded() || insertedPadding,
                             recordTy.getKind());
    return cir::ConstRecordAttr::get(explicitRecordTy, membersAttr);
  }

  if (auto vecTy = mlir::dyn_cast<cir::VectorType>(ty)) {
    mlir::TypedAttr elemAttr =
        getPatternInitAttr(cgm, builder, vecTy.getElementType());
    llvm::SmallVector<mlir::Attribute> elts(vecTy.getSize(), elemAttr);
    return cir::ConstVectorAttr::get(vecTy, mlir::ArrayAttr::get(
                                                builder.getContext(), elts));
  }

  // Complex types: pattern-init both real and imaginary parts.
  if (auto complexTy = mlir::dyn_cast<cir::ComplexType>(ty)) {
    mlir::TypedAttr elemAttr =
        getPatternInitAttr(cgm, builder, complexTy.getElementType());
    return cir::ConstComplexAttr::get(elemAttr, elemAttr);
  }

  // VPtrType, DataMemberType, and MethodType lack typed constant attributes
  // that can hold arbitrary pattern values. Use PatternInitAttr which lowers
  // to the correct platform-specific pattern (0xAA on 64-bit, 0xFF on 32-bit).
  if (mlir::isa<cir::VPtrType, cir::DataMemberType, cir::MethodType>(ty))
    return cir::PatternInitAttr::get(ty);

  // OpaqueType is target-specific; pattern semantics are unclear, use zero.
  if (mlir::isa<cir::OpaqueType>(ty))
    return builder.getZeroInitAttr(ty);

  llvm_unreachable("Pattern initialization for given type is NYI");
}

static mlir::TypedAttr getPatternPaddingAttr(CIRGenModule &cgm,
                                             CIRGenBuilderTy &builder,
                                             uint64_t size) {
  assert(size && "padding helper only handles non-empty padding");

  mlir::TypedAttr byteAttr = getPatternInitAttr(cgm, builder, cgm.uCharTy);
  auto paddingTy = cir::ArrayType::get(cgm.uCharTy, size);
  llvm::SmallVector<mlir::Attribute> bytes(size, byteAttr);
  return cir::ConstArrayAttr::get(paddingTy, mlir::ArrayAttr::get(
                                                 builder.getContext(), bytes));
}

static bool canDoSingleStore(mlir::Type storeTy) {
  if (mlir::isa<cir::IntType, cir::PointerType, cir::SingleType,
                cir::DoubleType, cir::FP16Type, cir::BF16Type,
                cir::LongDoubleType, cir::FP80Type, cir::FP128Type>(storeTy))
    return true;

  auto vectorTy = mlir::dyn_cast<cir::VectorType>(storeTy);
  if (!vectorTy)
    return false;

  return mlir::isa<cir::IntType, cir::PointerType, cir::SingleType,
                   cir::DoubleType, cir::FP16Type, cir::BF16Type,
                   cir::LongDoubleType, cir::FP80Type, cir::FP128Type>(
      vectorTy.getElementType());
}

/// Decide whether the non-zero parts of a constant can be emitted with no more
/// than NumStores scalar stores after a memset(0).
static bool canEmitInitWithFewStoresAfterBZero(const CIRGenBuilderTy &builder,
                                               mlir::Attribute init,
                                               unsigned &numStores) {
  if (builder.isNullValue(init))
    return true;

  auto typedInit = mlir::dyn_cast<mlir::TypedAttr>(init);
  if (!typedInit)
    return false;

  // Match OGCG: non-null symbol references (e.g. function/global addresses)
  // are not scalar leaves for the bzero+stores heuristic, so arrays of them
  // fall back to memcpy from a constant global instead of emitting one store
  // per element.
  if (mlir::isa<cir::GlobalViewAttr>(typedInit))
    return false;

  if (canDoSingleStore(typedInit.getType())) {
    if (!numStores)
      return false;
    --numStores;
    return true;
  }

  if (auto arrayAttr = mlir::dyn_cast<cir::ConstArrayAttr>(typedInit)) {
    if (auto strAttr = mlir::dyn_cast<mlir::StringAttr>(arrayAttr.getElts())) {
      for (char byte : strAttr.getValue()) {
        if (!byte)
          continue;
        if (!numStores)
          return false;
        --numStores;
      }
      return true;
    }

    if (auto eltsAttr = mlir::dyn_cast<mlir::ArrayAttr>(arrayAttr.getElts())) {
      for (mlir::Attribute elt : eltsAttr)
        if (!canEmitInitWithFewStoresAfterBZero(builder, elt, numStores))
          return false;
      return true;
    }
    return false;
  }

  if (auto recordAttr = mlir::dyn_cast<cir::ConstRecordAttr>(typedInit)) {
    for (mlir::Attribute member : recordAttr.getMembers())
      if (!canEmitInitWithFewStoresAfterBZero(builder, member, numStores))
        return false;
    return true;
  }

  return false;
}

static void setAutoInitAttr(mlir::Operation *op) {
  op->setAttr("cir.auto_init", mlir::UnitAttr::get(op->getContext()));
}

static bool isAccessedBy(const ValueDecl *decl, const Expr *e);

static void emitStoresForInitAfterBZero(CIRGenFunction &cgf, const VarDecl &d,
                                        Address addr, bool isVolatile,
                                        CIRGenBuilderTy &builder,
                                        mlir::TypedAttr constant,
                                        bool isAutoInit) {
  unsigned zeroStoreBudget = 0;
  if (canEmitInitWithFewStoresAfterBZero(builder, constant, zeroStoreBudget))
    return;

  CIRGenModule &cgm = cgf.getCIRGenModule();
  cir::CIRDataLayout layout{cgm.getModule()};
  mlir::Type ty = constant.getType();
  mlir::Location loc = getAutoVarDebugLoc(cgf, d);

  if (canDoSingleStore(ty)) {
    if (addr.getElementType() != ty)
      addr = addr.withElementType(builder, ty);

    mlir::Value constVal = builder.getConstant(loc, constant);
    auto storeOp = builder.createStore(loc, constVal, addr, isVolatile);
    if (isAutoInit)
      setAutoInitAttr(storeOp);
    cgf.addOpToCurrentSourceAtom(storeOp, constVal);
    return;
  }

  if (auto arrayAttr = mlir::dyn_cast<cir::ConstArrayAttr>(constant)) {
    auto arrayTy = mlir::cast<cir::ArrayType>(ty);
    mlir::Type eltTy = arrayTy.getElementType();
    auto addrPtrTy = mlir::cast<cir::PointerType>(addr.getPointer().getType());
    auto eltPtrTy = cir::PointerType::get(eltTy, addrPtrTy.getAddrSpace());
    mlir::Value basePtr = builder.createCast(
        loc, cir::CastKind::array_to_ptrdecay, addr.getPointer(), eltPtrTy);
    uint64_t eltSize = layout.getTypeAllocSize(eltTy);

    auto emitElement = [&](uint64_t idx, mlir::TypedAttr eltAttr) {
      unsigned zeroStoreBudget = 0;
      if (canEmitInitWithFewStoresAfterBZero(builder, eltAttr, zeroStoreBudget))
        return;

      mlir::Value eltAddr = basePtr;
      if (idx != 0) {
        mlir::Value idxVal = builder.getConstInt(
            loc, cir::IntType::get(builder.getContext(), 64, false), idx);
        eltAddr = cir::PtrStrideOp::create(builder, loc, eltPtrTy, basePtr,
                                           idxVal,
                                           cir::GEPNoWrapFlags::inbounds);
      }

      CharUnits eltAlign = addr.getAlignment().alignmentAtOffset(
          CharUnits::fromQuantity(idx * eltSize));
      emitStoresForInitAfterBZero(cgf, d, Address(eltAddr, eltTy, eltAlign),
                                  isVolatile, builder, eltAttr, isAutoInit);
    };

    if (auto strAttr = mlir::dyn_cast<mlir::StringAttr>(arrayAttr.getElts())) {
      auto intTy = mlir::dyn_cast<cir::IntType>(eltTy);
      assert(intTy && "string initializer stored into non-integer array");
      for (auto [idx, byte] : llvm::enumerate(strAttr.getValue())) {
        unsigned char value = static_cast<unsigned char>(byte);
        if (!value)
          continue;
        auto byteAttr =
            cir::IntAttr::get(intTy, llvm::APInt(intTy.getWidth(), value));
        emitElement(idx, byteAttr);
      }
      return;
    }

    if (auto eltsAttr = mlir::dyn_cast<mlir::ArrayAttr>(arrayAttr.getElts())) {
      for (auto [idx, elt] : llvm::enumerate(eltsAttr))
        emitElement(idx, mlir::cast<mlir::TypedAttr>(elt));
      return;
    }

    llvm_unreachable("unsupported constant array initializer");
  }

  if (auto recordAttr = mlir::dyn_cast<cir::ConstRecordAttr>(constant)) {
    auto recordTy = mlir::cast<cir::RecordType>(ty);
    auto recordPtrTy =
        mlir::cast<cir::PointerType>(addr.getPointer().getType());
    llvm::ArrayRef<mlir::Type> members = recordTy.getMembers();

    for (auto [idx, member] : llvm::enumerate(recordAttr.getMembers())) {
      auto memberAttr = mlir::cast<mlir::TypedAttr>(member);
      unsigned zeroStoreBudget = 0;
      if (canEmitInitWithFewStoresAfterBZero(builder, memberAttr,
                                             zeroStoreBudget))
        continue;

      mlir::Type memberTy = members[idx];
      auto memberPtrTy =
          cir::PointerType::get(memberTy, recordPtrTy.getAddrSpace());
      mlir::Value memberPtr = builder.createGetMember(
          loc, memberPtrTy, addr.getPointer(), /*name=*/"", idx);
      CharUnits memberAlign = addr.getAlignment().alignmentAtOffset(
          cgf.getDirectBlockLiteralFieldOffset(members, recordTy.getPacked(),
                                               idx));
      emitStoresForInitAfterBZero(
          cgf, d, Address(memberPtr, memberTy, memberAlign), isVolatile,
          builder, memberAttr, isAutoInit);
    }
    return;
  }

  llvm_unreachable("unsupported constant initializer after memset(0)");
}

/// Check if the constant is all-zero or mostly-zero with a small number of
/// non-zero leaves, and if so, use memset(0) + scalar stores instead of
/// creating a large global constant.
static bool shouldUseBZeroPlusStoresToInitialize(CIRGenBuilderTy &builder,
                                                 mlir::TypedAttr constant,
                                                 uint64_t constantSize) {
  unsigned zeroStoreBudget = 0;
  if (canEmitInitWithFewStoresAfterBZero(builder, constant, zeroStoreBudget))
    return true;

  // Small constants (<=32 bytes) should use memcpy — the overhead of memset
  // + patch stores isn't worth it.
  constexpr uint64_t sizeLimit = 32;
  if (constantSize <= sizeLimit)
    return false;

  unsigned storeBudget = 6;
  return canEmitInitWithFewStoresAfterBZero(builder, constant, storeBudget);
}

/// Check if the constant is filled with a single repeated byte value.
/// Returns the byte value if uniform, or -1 if not.
/// Matches OGCG's shouldUseMemSetToInitialize (CGDecl.cpp:1017-1024) which
/// calls llvm::isBytewiseValue.
static int getUniformByteValue(llvm::APInt value) {
  unsigned bitWidth = value.getBitWidth();
  if (!bitWidth || bitWidth % 8 != 0)
    return -1;

  uint64_t firstByte = value.extractBitsAsZExtValue(8, 0);
  for (unsigned bitOffset = 8; bitOffset < bitWidth; bitOffset += 8)
    if (value.extractBitsAsZExtValue(8, bitOffset) != firstByte)
      return -1;

  return static_cast<int>(firstByte);
}

static int getScalarUniformByteValue(mlir::TypedAttr constant) {
  if (auto intAttr = mlir::dyn_cast<cir::IntAttr>(constant))
    return getUniformByteValue(intAttr.getValue());

  if (auto fpAttr = mlir::dyn_cast<cir::FPAttr>(constant))
    return getUniformByteValue(fpAttr.getValue().bitcastToAPInt());

  return -1;
}

static int getUniformByteValue(mlir::TypedAttr constant,
                               uint64_t constantSize) {
  constexpr uint64_t sizeLimit = 32;
  if (constantSize <= sizeLimit)
    return -1;

  // Check for ConstArrayAttr with uniform element values.
  if (auto arrayAttr = mlir::dyn_cast<cir::ConstArrayAttr>(constant)) {
    if (arrayAttr.getTrailingZerosNum() > 0)
      return -1; // Not uniform if there are trailing zeros mixed in.
    // String literal storage — check byte uniformity.
    if (auto strAttr = mlir::dyn_cast<mlir::StringAttr>(arrayAttr.getElts())) {
      llvm::StringRef data = strAttr.getValue();
      if (data.empty())
        return -1;
      char first = data[0];
      if (data.find_first_not_of(first) == llvm::StringRef::npos)
        return static_cast<unsigned char>(first);
    }
    // Array of scalar constants — check if every element has the same
    // byte-uniform bit pattern. This matches OGCG's isBytewiseValue and
    // catches repeated-byte float/double initializers in addition to integers.
    if (auto eltsAttr = mlir::dyn_cast<mlir::ArrayAttr>(arrayAttr.getElts())) {
      if (eltsAttr.empty())
        return -1;
      auto firstElt = mlir::dyn_cast<mlir::TypedAttr>(eltsAttr[0]);
      if (!firstElt)
        return -1;
      int byte = getScalarUniformByteValue(firstElt);
      if (byte < 0)
        return -1;
      for (auto elt : eltsAttr) {
        auto typedElt = mlir::dyn_cast<mlir::TypedAttr>(elt);
        if (!typedElt || getScalarUniformByteValue(typedElt) != byte)
          return -1;
      }
      return byte;
    }
  }

  return -1;
}

static void emitStoresForConstant(CIRGenFunction &cgf, const VarDecl &d,
                                  Address addr, bool isVolatile,
                                  CIRGenBuilderTy &builder,
                                  mlir::TypedAttr constant,
                                  bool allowSplitConstantStore = true,
                                  bool isAutoInit = false) {
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Type ty = constant.getType();
  cir::CIRDataLayout layout{cgm.getModule()};
  uint64_t constantSize = layout.getTypeAllocSize(ty);
  if (!constantSize)
    return;

  if (canDoSingleStore(ty)) {
    if (addr.getElementType() != ty)
      addr = addr.withElementType(builder, ty);

    if (auto allocaOp = findAllocaOp(addr.getPointer()))
      allocaOp.setInitAttr(mlir::UnitAttr::get(&cgm.getMLIRContext()));

    mlir::Location loc = getAutoVarDebugLoc(cgf, d);

    mlir::Value constVal = builder.getConstant(loc, constant);
    auto storeOp = builder.createStore(loc, constVal, addr, isVolatile);
    if (isAutoInit)
      setAutoInitAttr(storeOp);
    cgf.addOpToCurrentSourceAtom(storeOp, constVal);
    return;
  }

  // Optimization: use memset for constants where every byte is the same
  // (e.g., char buf[1000] filled with 'A'). This avoids creating large
  // global constants. Matches OGCG CGDecl.cpp:1017-1024.
  int uniformByte = getUniformByteValue(constant, constantSize);
  if (uniformByte >= 0) {
    if (addr.getElementType() != ty)
      addr = addr.withElementType(builder, ty);
    auto allocaOp = findAllocaOp(addr.getPointer());
    if (allocaOp)
      allocaOp.setInitAttr(mlir::UnitAttr::get(&cgm.getMLIRContext()));
    mlir::Location loc = getAutoVarDebugLoc(cgf, d);
    auto srcPtrTy =
        mlir::cast<cir::PointerType>(addr.getPointer().getType());
    auto voidPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        srcPtrTy.getAddrSpace());
    mlir::Value destPtr = builder.createBitcast(addr.getPointer(), voidPtrTy);
    auto sizeTy = mlir::cast<cir::IntType>(
        cgf.convertType(cgf.getContext().getSizeType()));
    mlir::Value sizeVal = builder.getConstInt(loc, sizeTy, constantSize);
    mlir::Value byteVal = builder.getConstInt(
        loc, cir::IntType::get(builder.getContext(), 32, true), uniformByte);
    auto memsetOp =
        cir::MemSetOp::create(builder, loc, destPtr, byteVal, sizeVal);
    if (isAutoInit)
      setAutoInitAttr(memsetOp);
    cgf.addOpToCurrentSourceAtom(memsetOp);
    if (!addr.getAlignment().isZero())
      memsetOp->setAttr("cir.dst_align",
                        cgf.getI64AttrCached(
                            addr.getAlignment().getAsAlign().value()));
    return;
  }

  if (addr.getElementType() != ty)
    addr = addr.withElementType(builder, ty);

  // If the address is an alloca, set the init attribute.
  // The address is usually an alloca, but there is at least one case where
  // emitAutoVarInit is called from the OpenACC codegen with an address that
  // is not an alloca. For CUDA/HIP/OpenCL, there may be an address space cast.
  auto allocaOp = findAllocaOp(addr.getPointer());
  if (allocaOp)
    allocaOp.setInitAttr(mlir::UnitAttr::get(&cgm.getMLIRContext()));

  mlir::Location loc = getAutoVarDebugLoc(cgf, d);

  // Optimization: use memset(0) for all-zero or mostly-zero constants.
  // This avoids creating large global constants for arrays like
  // char str[100000] = "abc" (where OGCG uses memset + 3 stores).
  if (shouldUseBZeroPlusStoresToInitialize(builder, constant, constantSize)) {
    auto srcPtrTy =
        mlir::cast<cir::PointerType>(addr.getPointer().getType());
    auto voidPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        srcPtrTy.getAddrSpace());
    mlir::Value destPtr = builder.createBitcast(addr.getPointer(), voidPtrTy);
    auto sizeTy = mlir::cast<cir::IntType>(
        cgf.convertType(cgf.getContext().getSizeType()));
    mlir::Value sizeVal = builder.getConstInt(loc, sizeTy, constantSize);
    mlir::Value zeroVal = builder.getConstInt(
        loc, cir::IntType::get(builder.getContext(), 32, true), 0);
    auto memsetOp =
        cir::MemSetOp::create(builder, loc, destPtr, zeroVal, sizeVal);
    if (isAutoInit)
      setAutoInitAttr(memsetOp);
    cgf.addOpToCurrentSourceAtom(memsetOp);
    // Forward alignment for LLVM backend optimization.
    if (!addr.getAlignment().isZero())
      memsetOp->setAttr("cir.dst_align",
                        cgf.getI64AttrCached(
                            addr.getAlignment().getAsAlign().value()));
    if (addr.getElementType() != ty)
      addr = addr.withElementType(builder, ty);
    emitStoresForInitAfterBZero(cgf, d, addr, isVolatile, builder, constant,
                                isAutoInit);
    return;
  }

  // Tier 4: Split small aggregates into individual stores at -O1+ only when
  // the caller intentionally opts into this path. Ordinary local aggregate
  // init should fall back to memcpy from a constant global, matching OGCG's
  // byte-typed destination guard in CGDecl.cpp:1253-1278.
  if (cgm.getCodeGenOpts().OptimizationLevel > 0 && constantSize <= 64 &&
      allowSplitConstantStore) {
    if (auto arrayAttr = mlir::dyn_cast<cir::ConstArrayAttr>(constant)) {
      auto arrayTy = mlir::cast<cir::ArrayType>(ty);
      mlir::Type eltTy = arrayTy.getElementType();

      // Count non-zero elements and collect their indices+values.
      llvm::SmallVector<std::pair<uint64_t, mlir::Attribute>> nonZeroElts;
      bool canSplit = true;
      if (auto eltsAttr =
              mlir::dyn_cast<mlir::ArrayAttr>(arrayAttr.getElts())) {
        for (auto [idx, elt] : llvm::enumerate(eltsAttr)) {
          // Only handle simple integer/FP element types.
          if (auto intAttr = mlir::dyn_cast<cir::IntAttr>(elt)) {
            if (!intAttr.isNullValue())
              nonZeroElts.push_back({idx, elt});
          } else if (auto fpAttr = mlir::dyn_cast<cir::FPAttr>(elt)) {
            if (!fpAttr.getValue().isPosZero())
              nonZeroElts.push_back({idx, elt});
          } else {
            canSplit = false;
            break;
          }
        }
      } else {
        canSplit = false;
      }

      // Budget: <=6 non-zero stores (matching OGCG's StoreBudget).
      if (canSplit && nonZeroElts.size() <= 6) {
        // Zero the entire array first.
        auto addrPtrTy =
            mlir::cast<cir::PointerType>(addr.getPointer().getType());
        auto voidPtrTy = cir::PointerType::get(
            builder.getContext(), cir::VoidType::get(builder.getContext()),
            addrPtrTy.getAddrSpace());
        mlir::Value destPtr =
            builder.createBitcast(addr.getPointer(), voidPtrTy);
        auto sizeTy = mlir::cast<cir::IntType>(
            cgf.convertType(cgf.getContext().getSizeType()));
        mlir::Value sizeVal = builder.getConstInt(loc, sizeTy, constantSize);
        mlir::Value zeroVal = builder.getConstInt(
            loc, cir::IntType::get(builder.getContext(), 32, true), 0);
        auto memsetOp =
            cir::MemSetOp::create(builder, loc, destPtr, zeroVal, sizeVal);
        if (isAutoInit)
          setAutoInitAttr(memsetOp);
        cgf.addOpToCurrentSourceAtom(memsetOp);

        // Emit individual stores for non-zero elements.
        // First decay the array pointer to an element pointer.
        auto eltPtrTy =
            cir::PointerType::get(eltTy, addrPtrTy.getAddrSpace());
        mlir::Value basePtr = builder.createCast(
            loc, cir::CastKind::array_to_ptrdecay, addr.getPointer(), eltPtrTy);
        for (auto &[idx, elt] : nonZeroElts) {
          mlir::Value eltAddr;
          if (idx == 0) {
            eltAddr = basePtr;
          } else {
            mlir::Value idxVal = builder.getConstInt(
                loc, cir::IntType::get(builder.getContext(), 64, false), idx);
            eltAddr = cir::PtrStrideOp::create(builder, loc, eltPtrTy,
                                               basePtr, idxVal,
                cir::GEPNoWrapFlags::inbounds);
          }
          mlir::Value val;
          if (auto intAttr = mlir::dyn_cast<cir::IntAttr>(elt))
            val = builder.getConstInt(loc, eltTy,
                                      intAttr.getValue().getSExtValue());
          else if (auto fpAttr = mlir::dyn_cast<cir::FPAttr>(elt))
            val = builder.getConstFP(loc, eltTy, fpAttr.getValue());
          if (val) {
            uint64_t eltSize = layout.getTypeAllocSize(eltTy);
            CharUnits eltAlign = addr.getAlignment().alignmentAtOffset(
                CharUnits::fromQuantity(idx * eltSize));
            auto storeOp =
                builder.createStore(loc, val, Address(eltAddr, eltTy, eltAlign));
            if (isAutoInit)
              setAutoInitAttr(storeOp);
            cgf.addOpToCurrentSourceAtom(storeOp, val);
          }
        }
        return;
      }
    }
  }

  // Fallback: create a global constant and use cir.copy to initialize the
  // local.
  CharUnits align = addr.getAlignment();
  Address src = cgm.createUnnamedGlobalFrom(d, constant, align);

  // The source (unnamed global) may have a different address space than the
  // destination (local variable). For example, in OpenCL the global constant
  // is in the constant address space while the local is in the private address
  // space. cir.copy supports cross-address-space copies natively, so no
  // addrspacecast is needed — pass the source pointer directly.

  // Pass alignment from the Address objects to the copy op.
  mlir::IntegerAttr dstAlignAttr, srcAlignAttr;
  if (!addr.getAlignment().isZero())
    dstAlignAttr =
        cgf.getI64AttrCached(addr.getAlignment().getAsAlign().value());
  if (!src.getAlignment().isZero())
    srcAlignAttr = cgf.getI64AttrCached(src.getAlignment().getAsAlign().value());

  auto copyOp = cir::CopyOp::create(builder, loc, addr.getPointer(),
                                    src.getPointer(), isVolatile, dstAlignAttr,
                                    srcAlignAttr);
  if (isAutoInit)
    setAutoInitAttr(copyOp);
  cgf.addOpToCurrentSourceAtom(copyOp);
}

void CIRGenFunction::emitByrefStructureInit(
    const CIRGenFunction::AutoVarEmission &emission) {
  Address addr = emission.addr;
  const auto &byrefInfo = getBlockByrefInfo(emission.variable);
  mlir::Location loc = addr.getPointer().getLoc();
  const auto &layout = cgm.getDataLayout();
  auto fieldOffset = [&](unsigned fieldIndex) {
    CharUnits offset;
    auto members = byrefInfo.type.getMembers();
    for (unsigned i = 0; i < fieldIndex; ++i) {
      offset = offset.alignTo(
          CharUnits::fromQuantity(layout.getABITypeAlign(members[i]).value()));
      offset += CharUnits::fromQuantity(
          layout.getTypeAllocSize(members[i]).getFixedValue());
    }
    return offset.alignTo(
        CharUnits::fromQuantity(layout.getABITypeAlign(members[fieldIndex]).value()));
  };

  auto storeHeaderField = [&](unsigned fieldIndex, mlir::Value value,
                              bool isFunction = false) {
    mlir::Type fieldTy = byrefInfo.type.getMembers()[fieldIndex];
    mlir::Value fieldPtr = builder.createGetMember(
        loc, builder.getPointerTo(fieldTy), addr.getPointer(), "", fieldIndex);
    Address fieldAddr(
        fieldPtr, fieldTy,
        addr.getAlignment().alignmentAtOffset(fieldOffset(fieldIndex)));

    if (isFunction) {
      if (CIRGenRuntimePointerAuthInfo pointerAuth =
              getByrefHelperFunctionPointerAuthInfo(*this, fieldAddr))
        value = emitPointerAuthSign(pointerAuth, value);
    }

    builder.createStore(loc, value, fieldAddr);
  };

  storeHeaderField(0, builder.getNullValue(builder.getVoidPtrTy(), loc));
  mlir::Value forwarding = addr.getPointer();
  if (forwarding.getType() != builder.getVoidPtrTy())
    forwarding = builder.createBitcast(loc, forwarding, builder.getVoidPtrTy());
  storeHeaderField(1, forwarding);
  uint32_t flags = 0;
  if (byrefInfo.hasCopyAndDispose)
    flags |= kBlockByrefHasCopyDispose;
  if (byrefInfo.hasByrefLifetime) {
    if (byrefInfo.hasByrefExtendedLayout) {
      flags |= kBlockByrefLayoutExtended;
    } else {
      switch (byrefInfo.byrefLifetime) {
      case Qualifiers::OCL_Strong:
        flags |= kBlockByrefLayoutStrong;
        break;
      case Qualifiers::OCL_Weak:
        flags |= kBlockByrefLayoutWeak;
        break;
      case Qualifiers::OCL_ExplicitNone:
        flags |= kBlockByrefLayoutUnretained;
        break;
      case Qualifiers::OCL_None:
        if (!emission.variable->getType()->isObjCObjectPointerType() &&
            !emission.variable->getType()->isBlockPointerType())
          flags |= kBlockByrefLayoutNonObject;
        break;
      case Qualifiers::OCL_Autoreleasing:
        break;
      }
    }
  }
  storeHeaderField(2, builder.getConstInt(loc, builder.getSInt32Ty(), flags));
  uint64_t byrefSize =
      cgm.getDataLayout().getTypeAllocSize(byrefInfo.type).getFixedValue();
  storeHeaderField(3, builder.getConstInt(loc, builder.getSInt32Ty(), byrefSize));

  if (byrefInfo.hasByrefExtendedLayout) {
    storeHeaderField(byrefInfo.layoutIndex,
                     buildObjCByrefLayoutValue(cgm, builder, loc,
                                               emission.variable->getType()));
  }

  if (byrefInfo.hasCopyAndDispose) {
    std::pair<cir::FuncOp, cir::FuncOp> helpers;
    QualType type = emission.variable->getType();
    if (isSupportedObjCByrefHelperType(type)) {
      auto lifetime = type.getObjCLifetime();
      helpers =
          lifetime == Qualifiers::OCL_Weak
              ? buildARCWeakByrefHelpers(*this, loc, emission.variable,
                                         byrefInfo)
              : type->isBlockPointerType()
                    ? buildARCStrongBlockByrefHelpers(*this, loc,
                                                      emission.variable,
                                                      byrefInfo)
              : buildARCStrongByrefHelpers(*this, loc, emission.variable,
                                           byrefInfo);
    } else if (isSupportedAddressDiscriminatedByrefHelperType(type)) {
      helpers = buildAddressDiscriminatedByrefHelpers(*this, loc,
                                                      emission.variable,
                                                      byrefInfo);
    } else if (isSupportedNonARCObjectByrefHelperType(getContext(), type)) {
      helpers =
          buildObjectByrefHelpers(*this, loc, emission.variable, byrefInfo);
    } else if (isSupportedNonTrivialCStructByrefHelperType(type)) {
      helpers = buildNonTrivialCStructByrefHelpers(*this, loc,
                                                   emission.variable, byrefInfo);
    } else if (const auto *record = type->getAsCXXRecordDecl();
               record &&
               (getContext().getBlockVarCopyInit(emission.variable).getCopyExpr() ||
                !record->hasTrivialDestructor())) {
      helpers = buildCXXByrefHelpers(*this, loc, emission.variable, byrefInfo);
    } else {
      cgm.errorNYI(emission.variable->getLocation(),
                   "escaping-by-reference local requiring copy/dispose helpers");
      return;
    }
    auto [copyHelper, disposeHelper] = helpers;

    auto copyView = builder.getConstant(
        loc, cir::GlobalViewAttr::get(
                 builder.getPointerTo(copyHelper.getFunctionType()),
                 mlir::FlatSymbolRefAttr::get(&getMLIRContext(),
                                             copyHelper.getSymNameAttr())));
    auto disposeView = builder.getConstant(
        loc, cir::GlobalViewAttr::get(
                 builder.getPointerTo(disposeHelper.getFunctionType()),
                 mlir::FlatSymbolRefAttr::get(&getMLIRContext(),
                                             disposeHelper.getSymNameAttr())));

    storeHeaderField(byrefInfo.copyHelperIndex, copyView, /*isFunction=*/true);
    storeHeaderField(byrefInfo.disposeHelperIndex, disposeView,
                     /*isFunction=*/true);
  }
}

void CIRGenFunction::emitAutoVarInit(
    const CIRGenFunction::AutoVarEmission &emission) {
  assert(emission.variable && "emission was not valid!");

  // If this was emitted as a global constant, we're done.
  if (emission.wasEmittedAsGlobal())
    return;

  ApplySourceAtomGroup atomGroup(*this);

  // OGCG returns early here if !HaveInsertPoint() && !ContainsLabel(Init).
  // CIR scope ops always provide an insert point, so the ContainsLabel
  // check is unnecessary — init emission always proceeds.

  const VarDecl &d = *emission.variable;

  QualType type = d.getType();

  // If this local has an initializer, emit it now.
  const Expr *init = d.getInit();

  // Initialize the variable here if it doesn't have a initializer and it is a
  // C struct that is non-trivial to initialize or an array containing such a
  // struct.
  if (!init && type.isNonTrivialToPrimitiveDefaultInitialize() ==
                   QualType::PDIK_Struct) {
    if (!isSupportedNonTrivialCStructType(type)) {
      cgm.errorNYI(d.getSourceRange(),
                   "emitAutoVarInit: non-trivial to default initialize");
      return;
    }
  }

  const Address objectAddr = emission.getObjectAddress(*this);

  // Check whether this is a byref variable that's potentially
  // captured and moved by its own initializer.  If so, we'll need to
  // emit the initializer first, then copy into the variable.
  bool capturedByInit = false;
  if (capturedByInit) {
    cgm.errorNYI(d.getLocation(),
                 "escaping-by-reference local captured by its initializer");
    return;
  }

  if (emission.isEscapingByRef)
    emitByrefStructureInit(emission);

  if (!init && type.isNonTrivialToPrimitiveDefaultInitialize() ==
                   QualType::PDIK_Struct) {
    defaultInitSupportedNonTrivialCStructWithThunk(*this, objectAddr, type);
    mlir::Value val = emission.getAllocatedAddress().getPointer();
    if (auto allocaOp = findAllocaOp(val))
      allocaOp.setInitAttr(mlir::UnitAttr::get(&getMLIRContext()));
    return;
  }

  // Note: constexpr already initializes everything correctly.
  auto hasNoTrivialAutoVarInitAttr = [](const Decl *d) {
    return d && d->hasAttr<NoTrivialAutoVarInitAttr>();
  };
  LangOptions::TrivialAutoVarInitKind trivialAutoVarInit =
      ((d.isConstexpr() || d.getAttr<UninitializedAttr>() ||
        hasNoTrivialAutoVarInitAttr(type->getAsTagDecl()) ||
        hasNoTrivialAutoVarInitAttr(curFuncDecl))
           ? LangOptions::TrivialAutoVarInitKind::Uninitialized
           : getContext().getLangOpts().getTrivialAutoVarInit());

  auto initializeWhatIsTechnicallyUninitialized = [&](Address addr) {
    if (trivialAutoVarInit ==
        LangOptions::TrivialAutoVarInitKind::Uninitialized)
      return;
    // Check -ftrivial-auto-var-init-stop-after=N counter.
    // Matches OGCG CGDecl.cpp:1840/1848 (called per Zero/Pattern case).
    if (cgm.stopAutoInit())
      return;

    // Skip large variables when -ftrivial-auto-var-init-max-size=N is set.
    // OGCG uses getDataLayout().getTypeAllocSize() (CGDecl.cpp:1835,1842-1843);
    // we use getTypeSizeInChars() which is equivalent at the AST level.
    auto trivialAutoVarInitMaxSize =
        getContext().getLangOpts().TrivialAutoVarInitMaxSize;

    CharUnits size = getContext().getTypeSizeInChars(type);
    if (!size.isZero()) {
      if (trivialAutoVarInitMaxSize > 0 &&
          size.getQuantity() >
              static_cast<int64_t>(trivialAutoVarInitMaxSize))
        return;
      switch (trivialAutoVarInit) {
      case LangOptions::TrivialAutoVarInitKind::Uninitialized:
        llvm_unreachable("Uninitialized handled above");
      case LangOptions::TrivialAutoVarInitKind::Zero: {
        mlir::Type elTy = addr.getElementType();
        mlir::TypedAttr zeroConstant = builder.getZeroInitAttr(elTy);
        bool isVolatile = type.isVolatileQualified();
        emitStoresForConstant(*this, d, addr, isVolatile, builder,
                              zeroConstant,
                              /*allowSplitConstantStore=*/true,
                              /*isAutoInit=*/true);
        break;
      }
      case LangOptions::TrivialAutoVarInitKind::Pattern: {
        // Match OGCG's constWithPadding-style behavior by making implicit
        // record padding explicit in the synthesized pattern constant.
        mlir::Type elTy = addr.getElementType();
        mlir::TypedAttr patternConstant =
            getPatternInitAttr(cgm, builder, elTy);
        bool isVolatile = type.isVolatileQualified();
        emitStoresForConstant(*this, d, addr, isVolatile, builder,
                              patternConstant,
                              /*allowSplitConstantStore=*/true,
                              /*isAutoInit=*/true);
        break;
      }
      }
      return;
    }

    // VLAs look zero-sized to getTypeSizeInChars. We can't emit constant
    // stores to them, so emit a memset with the VLA size to initialize.
    const auto *vlaType = getContext().getAsVariableArrayType(type);
    if (!vlaType)
      return;

    VlaSizePair vlaSize = getVLASize(vlaType);
    mlir::Value sizeVal = vlaSize.numElts;
    CharUnits eltSize = getContext().getTypeSizeInChars(vlaSize.type);

    switch (trivialAutoVarInit) {
    case LangOptions::TrivialAutoVarInitKind::Uninitialized:
      llvm_unreachable("Uninitialized handled above");
    case LangOptions::TrivialAutoVarInitKind::Zero: {
      mlir::Location loc = getAutoVarDebugLoc(*this, d);
      // Compute total byte size: numElts * eltSize.
      if (!eltSize.isOne()) {
        mlir::Value eltSizeVal = builder.getConstInt(
            loc, sizeVal.getType(), eltSize.getQuantity());
        sizeVal = builder.createNUWAMul(loc, sizeVal, eltSizeVal);
      }
      // Cast dest to void* and emit memset to zero.
      {
        auto addrPtrTy =
            mlir::cast<cir::PointerType>(addr.getPointer().getType());
        auto voidPtrTy = cir::PointerType::get(
            builder.getContext(), cir::VoidType::get(builder.getContext()),
            addrPtrTy.getAddrSpace());
        mlir::Value destPtr =
            builder.createBitcast(addr.getPointer(), voidPtrTy);
        mlir::Value zeroVal = builder.getConstInt(
            loc, cir::IntType::get(builder.getContext(), 32, true), 0);
        auto memsetOp =
            cir::MemSetOp::create(builder, loc, destPtr, zeroVal, sizeVal);
        setAutoInitAttr(memsetOp);
        if (!addr.getAlignment().isZero())
          memsetOp->setAttr("cir.dst_align",
                            getI64AttrCached(
                                addr.getAlignment().getAsAlign().value()));
      }
      break;
    }
    case LangOptions::TrivialAutoVarInitKind::Pattern: {
      // For VLA pattern initialization, fill with the pattern byte for
      // the element type. Integer/pointer types use 0xAA, float types
      // use 0xFF (NaN). For types with non-uniform byte patterns
      // (e.g., mixed structs), we fall back to 0xAA which is
      // imprecise but prevents crashes.
      mlir::Location loc = getAutoVarDebugLoc(*this, d);
      if (!eltSize.isOne()) {
        mlir::Value eltSizeVal = builder.getConstInt(
            loc, sizeVal.getType(), eltSize.getQuantity());
        sizeVal = builder.createNUWAMul(loc, sizeVal, eltSizeVal);
      }
      auto addrPtrTy =
          mlir::cast<cir::PointerType>(addr.getPointer().getType());
      auto voidPtrTy = cir::PointerType::get(
          builder.getContext(), cir::VoidType::get(builder.getContext()),
          addrPtrTy.getAddrSpace());
      mlir::Value destPtr =
          builder.createBitcast(addr.getPointer(), voidPtrTy);
      // Determine the pattern byte from the element type.
      // Float/double use 0xFF (NaN). Integer/pointer use 0xAA.
      mlir::Type elemTy = addr.getElementType();
      int patternByte = 0xAA; // default for integer/pointer
      if (mlir::isa<cir::SingleType, cir::DoubleType, cir::FP16Type,
                     cir::BF16Type, cir::FP80Type, cir::FP128Type,
                     cir::LongDoubleType>(elemTy))
        patternByte = 0xFF;
      mlir::Value patternVal = builder.getConstInt(
          loc, cir::IntType::get(builder.getContext(), 32, true), patternByte);
      auto memsetOp =
          cir::MemSetOp::create(builder, loc, destPtr, patternVal, sizeVal);
      setAutoInitAttr(memsetOp);
      if (!addr.getAlignment().isZero())
        memsetOp->setAttr("cir.dst_align",
                          getI64AttrCached(
                              addr.getAlignment().getAsAlign().value()));
      break;
    }
    }
  };

  if (isTrivialInitializer(init)) {
    // OGCG calls PGO->markStmtMaybeUsed(Init) here to track coverage for
    // trivial initializers that are skipped during codegen.

    // Only set init for EXPLICIT initializers, not implicit constructor calls.
    // CXXTemporaryObjectExpr represents explicit functional-notation like
    // Type(), while plain CXXConstructExpr may be implicit from declarations
    // like "Derived d;" which shouldn't have init.
    if (init && isa<CXXTemporaryObjectExpr>(init)) {
      mlir::Value val = emission.getAllocatedAddress().getPointer();
      if (auto allocaOp = findAllocaOp(val))
        allocaOp.setInitAttr(mlir::UnitAttr::get(&getMLIRContext()));
    }
    initializeWhatIsTechnicallyUninitialized(objectAddr);
    return;
  }

  mlir::Attribute constant;
  if (emission.isConstantAggregate ||
      d.mightBeUsableInConstantExpressions(getContext())) {
    // FIXME: Differently from LLVM we try not to emit / lower too much
    // here for CIR since we are interested in seeing the ctor in some
    // analysis later on. So CIR's implementation of ConstantEmitter will
    // frequently return an empty Attribute, to signal we want to codegen
    // some trivial ctor calls and whatnots.
    constant = ConstantEmitter(*this).tryEmitAbstractForInitializer(d);
    // OGCG folds long _BitInt constants split into byte arrays back into
    // integers in this local-init path (CGDecl.cpp).
    //
    // CIR constant emission keeps long _BitInt constants in semantic integer
    // form at this stage; memory layout adjustments (alloca/byval/sret byte
    // arrays when needed) happen in CIR->LLVM lowering.
    if (constant && !mlir::isa<cir::ZeroAttr>(constant) &&
        (trivialAutoVarInit !=
         LangOptions::TrivialAutoVarInitKind::Uninitialized)) {
      // OGCG processes the constant through constWithPadding() and
      // replaceUndef() to fill padding and undef bytes with zero/pattern.
      // CIR record types include padding as explicit array-of-i8 members,
      // so the constant already covers padding. CIR constants are fully
      // defined (no undef), so replaceUndef is not needed.
    }
  }

  // NOTE(cir): In case we have a constant initializer, we can just emit a
  // store. But, in CIR, we wish to retain any ctor calls, so if it is a
  // CXX temporary object creation, we ensure the ctor call is used deferring
  // its removal/optimization to the CIR lowering.
  if (!constant || isa<CXXTemporaryObjectExpr>(init)) {
    if (trivialAutoVarInit !=
        LangOptions::TrivialAutoVarInitKind::Uninitialized) {
      // Match OGCG: non-constant aggregate initializers may leave parts of the
      // object untouched, but scalar initializers should only get auto-init if
      // they read from the destination during initialization.
      if (!type->isScalarType() || capturedByInit || isAccessedBy(&d, init))
        initializeWhatIsTechnicallyUninitialized(objectAddr);
    }
    LValue lv = makeAddrLValue(objectAddr, type, AlignmentSource::Decl);
    // OGCG calls setNonGC() here for ObjC GC write barriers. ObjC GC was
    // deprecated in 2012 and is not supported by CIR.
    llvm::SaveAndRestore<bool> suppressAggExprMemSetUseScope(
        suppressAggExprMemSetUse,
        suppressAggExprMemSetUse || emission.wasEmittedAsOffloadClause());
    emitExprAsInit(init, &d, lv);

    if (!emission.wasEmittedAsOffloadClause()) {
      // Mark as initialized - we have an init expression even if no code was
      // generated (e.g., trivial constructor with zero-initialization).
      mlir::Value val = emission.getAllocatedAddress().getPointer();
      assert(val && "Should have an address");
      if (auto allocaOp = findAllocaOp(val))
        allocaOp.setInitAttr(mlir::UnitAttr::get(&getMLIRContext()));
    }

    return;
  }

  // FIXME(cir): migrate most of this file to use mlir::TypedAttr directly.
  auto typedConstant = mlir::dyn_cast<mlir::TypedAttr>(constant);
  assert(typedConstant && "expected typed attribute");
  if (!emission.isConstantAggregate) {
    // For simple scalar/complex initialization, store the value directly.
    LValue lv = makeAddrLValue(objectAddr, type);
    assert(init && "expected initializer");
    mlir::Location initLoc = getLoc(init->getSourceRange());
    // OGCG calls setNonGC() here for ObjC GC write barriers (deprecated).
    mlir::Value constVal = builder.getConstant(initLoc, typedConstant);
    // For reference types, the constant may be a GlobalViewAttr with a
    // different pointer type (e.g., ptr<u8> for RTTI globals) than what
    // the reference alloca expects. Bitcast if needed.
    if (type->isReferenceType() &&
        constVal.getType() != objectAddr.getElementType()) {
      constVal = builder.createBitcast(initLoc, constVal,
                                       objectAddr.getElementType());
    }
    SourceLocRAIIObject locRAII{*this, getAutoVarDebugLoc(*this, d)};
    return emitStoreThroughLValue(RValue::get(constVal), lv);
  }

  // Match OGCG's ordinary constant-aggregate init path: keep memcpy as the
  // fallback instead of enabling the CIR-only O1 split-store heuristic.
  emitStoresForConstant(*this, d, objectAddr, type.isVolatileQualified(),
                        builder, typedConstant,
                        /*allowSplitConstantStore=*/false);
}

namespace {
/// Cleanup that calls a function specified by __attribute__((cleanup(fn)))
/// with a pointer to the variable.
struct CallCleanupFunction final : EHScopeStack::Cleanup {
  cir::FuncOp cleanupFn;
  Address varAddr;

  CallCleanupFunction(cir::FuncOp fn, Address addr)
      : cleanupFn(fn), varAddr(addr) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    auto &builder = cgf.getBuilder();
    mlir::Location loc = varAddr.getPointer().getLoc();

    // Get the address of the variable and cast to the cleanup function's
    // parameter type if needed.
    mlir::Value addr = varAddr.getPointer();
    auto fnTy = cleanupFn.getFunctionType();
    if (fnTy.getNumInputs() > 0) {
      mlir::Type paramTy = fnTy.getInput(0);
      if (addr.getType() != paramTy)
        addr = builder.createBitcast(loc, addr, paramTy);
    }

    // OGCG uses the full EmitCall path with CGFunctionInfo, CallArgList,
    // CGCallee, and proper argument coercion. CIR uses a simplified
    // emitRuntimeCall which bypasses ABI handling. This will break when
    // the cleanup function's parameter type differs from the pointer type
    // or for byref variables.
    // cleanupFunctionABICall -- ABI call cleanup deferred to call conv lowering.
    cir::CallOp cleanupCall = cgf.emitRuntimeCallOp(loc, cleanupFn, {addr});
    cleanupCall->setAttr("cir.cleanup_attr_early_exit",
                         mlir::UnitAttr::get(&cgf.getMLIRContext()));
  }
};

struct CallBlockByrefDispose final : EHScopeStack::Cleanup {
  Address byrefAddr;

  explicit CallBlockByrefDispose(Address addr) : byrefAddr(addr) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    mlir::Location loc = byrefAddr.getPointer().getLoc();
    auto &builder = cgf.getBuilder();
    auto fnTy = cir::FuncType::get(
        {builder.getVoidPtrTy(), builder.getSInt32Ty()}, builder.getVoidTy());
    cir::FuncOp disposeFn =
        cgf.cgm.createRuntimeFunction(fnTy, "_Block_object_dispose");

    mlir::Value byrefPtr = byrefAddr.getPointer();
    if (byrefPtr.getType() != builder.getVoidPtrTy())
      byrefPtr = builder.createBitcast(loc, byrefPtr, builder.getVoidPtrTy());
    cgf.emitRuntimeCall(
        loc, disposeFn,
        {byrefPtr, builder.getConstInt(loc, builder.getSInt32Ty(),
                                       kBlockFieldIsByref)});
  }
};
} // namespace

void CIRGenFunction::emitAutoVarCleanups(
    const CIRGenFunction::AutoVarEmission &emission) {
  // OGCG checks emission.wasEmittedAsGlobal() and !HaveInsertPoint() here.
  // CIR already handles those conditions below.
  // OGCG returns early if !HaveInsertPoint(). CIR's structured scope model
  // keeps an insertion point for reachable auto-var cleanup emission.

  const VarDecl &d = *emission.variable;

  // Check the type for a cleanup.
  if (QualType::DestructionKind dtorKind = d.needsDestruction(getContext())) {
    emitAutoVarTypeCleanup(emission, dtorKind);

    // In CIR's structured scope model, all scope-exit paths share the same
    // cleanup code. When a variable with a destructor is declared inside a
    // switch body (without enclosing braces), its cleanup is emitted at the
    // enclosing scope exit — which is reached by ALL switch cases, including
    // those that never constructed the variable. Make the cleanup conditional
    // so the destructor only runs on paths where the constructor executed.
    //
    // This differs from OGCG where each case generates its own basic blocks
    // and the cleanup code is only reachable from paths through the
    // declaration.
    //
    // The flag alloca lives in the function entry block, so its false
    // initialization has to be emitted at the top of the switch scope rather
    // than next to the alloca: a switch nested in a loop is entered once per
    // iteration, and a flag left true by the iteration that constructed the
    // variable would destroy an unrelated object on the next one.
    LexicalScope *switchScope = nullptr;
    for (LexicalScope *scope = curLexScope; scope;
         scope = scope->getParentScope()) {
      if (scope->isSwitch()) {
        switchScope = scope;
        break;
      }
    }
    if (switchScope) {
      Address activeFlag =
          createCleanupActiveFlag(switchScope->getEntryBlock());
      initFullExprCleanupWithFlag(activeFlag);
    }
  }

  // OGCG pushes ExtendGCLifetime cleanup here for objc_precise_lifetime vars
  // when GC mode is enabled. ObjC GC was deprecated in macOS 10.8 (2012) and
  // is not supported by CIR. ObjC ARC precise lifetime is handled in
  // emitAutoVarTypeCleanup via ObjCPreciseLifetimeAttr. LLVM lifetime
  // markers are emitted in emitAutoVarAlloca via cir.lifetime.start/end.

  // Handle the cleanup attribute.
  if (const auto *ca = d.getAttr<CleanupAttr>()) {
    const FunctionDecl *cleanupFD = ca->getFunctionDecl();
    cir::FuncOp cleanupFn = cgm.getAddrOfFunction(
        GlobalDecl(cleanupFD),
        cgm.getTypes().getFunctionType(
            cgm.getTypes().arrangeFunctionDeclaration(cleanupFD)));
    Address varAddr = emission.getAllocatedAddress();
    ehStack.pushCleanup<CallCleanupFunction>(NormalAndEHCleanup, cleanupFn,
                                             varAddr);
  }

  if (emission.isEscapingByRef)
    ehStack.pushCleanup<CallBlockByrefDispose>(NormalAndEHCleanup,
                                               emission.getAllocatedAddress());
}

/// Emit code and set up symbol table for a variable declaration with auto,
/// register, or no storage class specifier. These turn into simple stack
/// objects, globals depending on target.
void CIRGenFunction::emitAutoVarDecl(const VarDecl &d) {
  CIRGenFunction::AutoVarEmission emission = emitAutoVarAlloca(d);
  emitAutoVarInit(emission);
  emitAutoVarCleanups(emission);
}

void CIRGenFunction::emitVarDecl(const VarDecl &d) {
  // If the declaration has external storage, don't emit it now, allow it to be
  // emitted lazily on its first use.
  if (d.hasExternalStorage())
    return;

  if (d.getStorageDuration() != SD_Automatic) {
    // Static sampler variables translated to function calls.
    if (d.getType()->isSamplerT()) {
      // Nothing needs to be done here, but let's flag it as an error until we
      // have a test. It requires OpenCL support.
      cgm.errorNYI(d.getSourceRange(), "emitVarDecl static sampler type");
      return;
    }

    cir::GlobalLinkageKind linkage =
        cgm.getCIRLinkageVarDefinition(&d, /*IsConstant=*/false);

    // FIXME: We need to force the emission/use of a guard variable for
    // some variables even if we can constant-evaluate them because
    // we can't guarantee every translation unit will constant-evaluate them.

    // Match OGCG's !HaveInsertPoint() behavior for unreachable static locals:
    // still materialize the backing global, but do not emit guarded
    // initialization from a dead continuation block after a noreturn call.
    if (auto *block = builder.getInsertionBlock();
        block && block != &block->getParent()->front() &&
        block->hasNoPredecessors()) {
      (void)cgm.getOrCreateStaticVarDecl(d, linkage);
      return;
    }

    return emitStaticVarDecl(d, linkage);
  }

  // OGCG dispatches OpenCL local address space variables through
  // CGM.getOpenCLRuntime().EmitWorkGroupLocalVarDecl(), which is virtual
  // and can be overridden by target-specific OpenCL runtimes. CIR currently
  // uses emitStaticVarDecl directly (matching the base runtime behavior).
  if (d.getType().getAddressSpace() == LangAS::opencl_local)
    return emitStaticVarDecl(d, cir::GlobalLinkageKind::InternalLinkage);

  assert(d.hasLocalStorage());

  CIRGenFunction::VarDeclContext varDeclCtx{*this, &d};
  return emitAutoVarDecl(d);
}

static std::string getStaticDeclName(CIRGenModule &cgm, const VarDecl &d) {
  if (cgm.getLangOpts().CPlusPlus)
    return cgm.getMangledName(&d).str();

  // If this isn't C++, we don't need a mangled name, just a pretty one.
  assert(!d.isExternallyVisible() && "name shouldn't matter");
  std::string contextName;
  const DeclContext *dc = d.getDeclContext();
  if (auto *cd = dyn_cast<CapturedDecl>(dc))
    dc = cast<DeclContext>(cd->getNonClosureContext());
  if (const auto *fd = dyn_cast<FunctionDecl>(dc))
    contextName = std::string(cgm.getMangledName(fd));
  else if (const auto *bd = dyn_cast<BlockDecl>(dc)) {
    MangleContext &mangleCtx = cgm.getCXXABI().getMangleContext();
    SmallString<256> buffer;
    llvm::raw_svector_ostream out(buffer);
    mangleCtx.mangleGlobalBlock(
        bd, dyn_cast_or_null<NamedDecl>(bd->getBlockManglingContextDecl()),
        out);
    contextName = std::string(out.str());
  } else if (const auto *omd = dyn_cast<ObjCMethodDecl>(dc)) {
    contextName = omd->getSelector().getAsString();
  } else {
    cgm.errorNYI(d.getLocation(), "Unknown context for static var decl");
    return {};
  }

  contextName += "." + d.getNameAsString();
  return contextName;
}

// TODO(cir): LLVM uses a Constant base class. Maybe CIR could leverage an
// interface for all constants?
cir::GlobalOp
CIRGenModule::getOrCreateStaticVarDecl(const VarDecl &d,
                                       cir::GlobalLinkageKind linkage) {
  // In general, we don't always emit static var decls once before we reference
  // them. It is possible to reference them before emitting the function that
  // contains them, and it is possible to emit the containing function multiple
  // times.
  if (cir::GlobalOp existingGV = getStaticLocalDeclAddress(&d))
    return existingGV;

  QualType ty = d.getType();
  assert(ty->isConstantSizeType() && "VLAs can't be static");

  // Use the label if the variable is renamed with the asm-label extension.
  std::string name;
  if (d.hasAttr<AsmLabelAttr>())
    name = std::string(getMangledName(GlobalDecl(&d)));
  else
    name = getStaticDeclName(*this, d);

  mlir::Type lty = getTypes().convertTypeForMem(ty);
  mlir::ptr::MemorySpaceAttrInterface addrSpace =
      cir::toCIRLangAddressSpaceAttr(&getMLIRContext(),
                                     getGlobalVarAddressSpace(&d));

  // OpenCL variables in local address space and CUDA shared
  // variables cannot have an initializer.
  mlir::Attribute init;
  if (d.hasAttr<LoaderUninitializedAttr>())
    init = cir::UndefAttr::get(convertType(ty));
  else if (d.hasAttr<CUDASharedAttr>())
    ; // no initializer for shared vars
  else if (ty.getAddressSpace() != LangAS::opencl_local)
    init = builder.getZeroInitAttr(lty);

  cir::GlobalOp gv = builder.createVersionedGlobal(
      getModule(), getLoc(d.getLocation()), name, lty,
      ty.isConstant(getASTContext()), linkage);
  insertGlobalSymbol(gv);
  if (addrSpace)
    gv.setAddrSpaceAttr(addrSpace);
  // TODO(cir): infer visibility from linkage in global op builder.
  gv.setVisibility(getMLIRVisibilityFromCIRLinkage(linkage));
  gv.setInitialValueAttr(init);
  gv.setAlignment(getASTContext().getDeclAlign(&d).getAsAlign().value());

  if (supportsCOMDAT() && gv.isWeakForLinker())
    gv.setComdat(true);

  if (d.getTLSKind())
    setTLSMode(gv, d);

  setGVProperties(gv, &d);

  // Mirror OGCG by applying target-specific attributes on static locals too.
  getTargetCIRGenInfo().setTargetAttributes(&d, gv, *this);

  // Attach debug info VarDecl so the lowering can create DIGlobalVariable.
  if (getCodeGenOpts().getDebugInfo() != llvm::codegenoptions::NoDebugInfo)
    gv->setAttr("cir.debug.var_decl",
                cir::ASTVarDeclAttr::get(&getMLIRContext(), &d));

  // OG checks if the expected address space, denoted by the type, is the
  // same as the actual address space indicated by attributes. If they aren't
  // the same, an addrspacecast is emitted when this variable is accessed.
  // In CIR however, cir.get_global already carries that information in
  // !cir.ptr type - if this global is in OpenCL local address space, then its
  // type would be !cir.ptr<..., addrspace(offload_local)>. Therefore we don't
  // need an explicit address space cast in CIR: they will get emitted when
  // lowering to LLVM IR.

  // Ensure that the static local gets initialized by making sure the parent
  // function gets emitted eventually.
  const Decl *dc = cast<Decl>(d.getDeclContext());

  // We can't name blocks or captured statements directly, so try to emit their
  // parents.
  if (isa<BlockDecl>(dc) || isa<CapturedDecl>(dc)) {
    dc = dc->getNonClosureContext();
    // FIXME: Ensure that global blocks get emitted.
    if (!dc)
      return gv;
  }

  GlobalDecl gd;
  if (const auto *cd = dyn_cast<CXXConstructorDecl>(dc))
    gd = GlobalDecl(cd, Ctor_Base);
  else if (const auto *dd = dyn_cast<CXXDestructorDecl>(dc))
    gd = GlobalDecl(dd, Dtor_Base);
  else if (const auto *fd = dyn_cast<FunctionDecl>(dc))
    gd = GlobalDecl(fd);
  else {
    // Don't do anything for Obj-C method decls or global closures. We should
    // never defer them.
    assert(isa<ObjCMethodDecl>(dc) && "unexpected parent code decl");
  }
  if (gd.getDecl()) {
    // OGCG wraps this in DisableAutoDeclareTargetRAII to prevent the parent
    // function from being auto-declared as an OpenMP target.
    // CIR currently has no equivalent OpenMP runtime hook at this call-site.
    (void)getAddrOfGlobal(gd);
  }

  // Cache the global early so that self-referencing constexpr initializers
  // (e.g., `static T x(42, &x)`) can find this global during constant
  // evaluation instead of creating a duplicate with a `.1` suffix.
  setStaticLocalDeclAddress(&d, gv);

  return gv;
}

/// Add the initializer for 'd' to the global variable that has already been
/// created for it. If the initializer has a different type than gv does, this
/// may free gv and return a different one. Otherwise it just returns gv.
cir::GlobalOp CIRGenFunction::addInitializerToStaticVarDecl(
    const VarDecl &d, cir::GlobalOp gv, cir::GetGlobalOp gvAddr) {
  // OGCG calls PGO->markStmtMaybeUsed(D.getInit()) here for coverage
  // tracking of static variable initializers.

  ConstantEmitter emitter(*this);
  mlir::TypedAttr init =
      mlir::dyn_cast_if_present<mlir::TypedAttr>(emitter.tryEmitForInitializer(d));

  // If constant emission failed, then this should be a C++ static
  // initializer.
  if (!init) {
    if (!getLangOpts().CPlusPlus)
      cgm.errorNYI(d.getInit()->getSourceRange(),
                    "constant l-value expression");
    else if (d.hasFlexibleArrayInit(getContext()))
      cgm.errorNYI(d.getInit()->getSourceRange(),
                    "flexible array initializer");
    else if (haveInsertPoint()) {
      // Since we have a static initializer, this global variable can't
      // be constant.
      gv.setConstant(false);

      // If we've been asked to forbid guard variables, emit an error now.
      // This diagnostic is hard-coded for Darwin's use case; we can find
      // better phrasing if someone else needs it.
      if (cgm.getCodeGenOpts().ForbidGuardVariables)
        cgm.getDiags().Report(d.getLocation(),
            cgm.getDiags().getCustomDiagID(
                clang::DiagnosticsEngine::Error,
                "this initialization requires a guard variable, which "
                "the kernel does not support"));
      cgm.getCXXABI().emitGuardedInit(*this, d, gv,
                                      /*shouldPerformInit=*/true);
    }
    return gv;
  }

  // TODO(cir): In debug builds, assert that the declaration size matches the
  // CIR data-layout type alloc size once we have a direct check wired here.

  // The initializer may differ in type from the global. Rewrite
  // the global to match the initializer.  (We have to do this
  // because some types, like unions, can't be completely represented
  // in the LLVM type system.)
  if (gv.getSymType() != init.getType()) {
    gv.setSymType(init.getType());

    // Normally this should be done with a call to cgm.replaceGlobal(oldGV, gv),
    // but since at this point the current block hasn't been really attached,
    // there's no visibility into the GetGlobalOp corresponding to this Global.
    // Given those constraints, thread in the GetGlobalOp and update it
    // directly.
    gvAddr.getAddr().setType(
        builder.getPointerTo(init.getType(), gv.getAddrSpaceAttr()));
  }

  bool needsDtor =
      d.needsDestruction(getContext()) == QualType::DK_cxx_destructor;

  gv.setConstant(d.getType().isConstantStorage(
      getContext(), /*ExcludeCtor=*/true, !needsDtor));
  gv.setInitialValueAttr(init);

  emitter.finalize(gv);

  if (needsDtor && haveInsertPoint()) {
    // We have a constant initializer, but a nontrivial destructor. We still
    // need to perform a guarded "initialization" in order to register the
    // destructor.
    if (cgm.getCodeGenOpts().ForbidGuardVariables)
      cgm.getDiags().Report(d.getLocation(),
          cgm.getDiags().getCustomDiagID(
              clang::DiagnosticsEngine::Error,
              "this initialization requires a guard variable, which "
              "the kernel does not support"));
    cgm.getCXXABI().emitGuardedInit(*this, d, gv, /*shouldPerformInit=*/false);
  }

  return gv;
}

void CIRGenFunction::emitStaticVarDecl(const VarDecl &d,
                                       cir::GlobalLinkageKind linkage) {
  // Check to see if we already have a global variable for this
  // declaration.  This can happen when double-emitting function
  // bodies, e.g. with complete and base constructors.
  cir::GlobalOp globalOp = cgm.getOrCreateStaticVarDecl(d, linkage);
  // TODO(cir): we should have a way to represent global ops as values without
  // having to emit a get global op. Sometimes these emissions are not used.
  bool isTLS = d.getTLSKind() != VarDecl::TLS_None;
  mlir::Value addr = builder.createGetGlobal(globalOp, isTLS);
  auto getAddrOp = addr.getDefiningOp<cir::GetGlobalOp>();
  assert(getAddrOp && "expected cir::GetGlobalOp");

  CharUnits alignment = getContext().getDeclAlign(&d);

  // Store into LocalDeclMap before generating initializer to handle
  // circular references.
  mlir::Type elemTy = convertTypeForMem(d.getType());
  // The global's symType may have been changed by a prior emission (e.g.,
  // complete constructor changed it during addInitializerToStaticVarDecl).
  // Bitcast the pointer to the expected type if needed, preserving address
  // space.
  auto addrPtrTy = mlir::cast<cir::PointerType>(addr.getType());
  auto targetPtrTy = cir::PointerType::get(builder.getContext(), elemTy,
                                            addrPtrTy.getAddrSpace());
  mlir::Value initialAddr = builder.createBitcast(addr, targetPtrTy);
  setAddrOfLocalVar(&d, Address(initialAddr, elemTy, alignment));

  // We can't have a VLA here, but we can have a pointer to a VLA,
  // even though that doesn't really make any sense.
  // Make sure to evaluate VLA bounds now so that we have them for later.
  if (d.getType()->isVariablyModifiedType())
    emitVariablyModifiedType(d.getType());

  cir::GlobalOp var = globalOp;

  // CUDA's local and local static __shared__ variables should not
  // have any non-empty initializers. This is ensured by Sema.
  // Whatever initializer such variable may have when it gets here is
  // a no-op and should not be emitted.
  bool isCudaSharedVar = getLangOpts().CUDA && getLangOpts().CUDAIsDevice &&
                         d.hasAttr<CUDASharedAttr>();
  // If this value has an initializer, emit it.
  if (d.getInit() && !isCudaSharedVar)
    var = addInitializerToStaticVarDecl(d, var, getAddrOp);

  var.setAlignment(alignment.getAsAlign().value());

  if (cgm.getCodeGenOpts().KeepPersistentStorageVariables)
    cgm.addUsedOrCompilerUsedGlobal(var);

  // From traditional codegen:
  // We may have to cast the constant because of the initializer
  // mismatch above.
  //
  // FIXME: It is really dangerous to store this in the map; if anyone
  // RAUW's the GV uses of this constant will be invalid.
  mlir::Value castedAddr =
      builder.createBitcast(getAddrOp.getAddr(), targetPtrTy);
  localDeclMap.find(&d)->second = Address(castedAddr, elemTy, alignment);
  cgm.setStaticLocalDeclAddress(&d, var);

  cgm.maybeRecordAddressSanitizerGlobalMetadata(&d, /*isDynInit=*/false, var);
  cgm.maybeRecordTypeSanGlobalMetadata(&d, var);
  // OGCG emits local variable debug declarations through CGDebugInfo.
  // CIR records debug compile-unit metadata at module scope and defers
  // detailed debug lowering; there is no dedicated CIR local-var debug op.

  // OGCG handles AnnotateAttr, SectionAttr, PragmaClang*SectionAttr,
  // RetainAttr, and UsedAttr on static local variables here.
  if (d.hasAttr<AnnotateAttr>())
    cgm.addGlobalAnnotations(&d, var);
  // Apply __attribute__((section("name"))) to static local variables.
  // Matches OGCG CGDecl.cpp:455-456.
  if (const auto *sa = d.getAttr<SectionAttr>())
    var.setSectionAttr(cgm.getStringAttrCached(sa->getName()));
  setPragmaClangSectionAttrs(d, var, cgm);
  // OGCG checks RetainAttr and UsedAttr on static locals (CGDecl.cpp:458-461).
  if (d.hasAttr<RetainAttr>())
    cgm.addUsedGlobal(var);
  if (d.hasAttr<UsedAttr>())
    cgm.addUsedOrCompilerUsedGlobal(var);
}

static bool tryEmitARCCopyWeakInit(CIRGenFunction &cgf, const LValue &destLV,
                                   const Expr *init) {
  bool needsCast = false;

  while (const auto *castExpr = dyn_cast<CastExpr>(init->IgnoreParens())) {
    switch (castExpr->getCastKind()) {
    case CK_NoOp:
    case CK_BitCast:
    case CK_BlockPointerToObjCPointerCast:
      needsCast = true;
      break;

    case CK_LValueToRValue: {
      const Expr *srcExpr = castExpr->getSubExpr();
      if (srcExpr->getType().getObjCLifetime() != Qualifiers::OCL_Weak)
        return false;

      LValue srcLV = cgf.emitLValue(srcExpr);
      Address srcAddr = srcLV.getAddress();
      if (needsCast)
        srcAddr = srcAddr.withElementType(cgf.getBuilder(),
                                          destLV.getAddress().getElementType());

      if (srcExpr->isLValue()) {
        cgf.emitARCCopyWeak(destLV.getAddress(), srcAddr);
      } else {
        assert(srcExpr->isXValue());
        cgf.emitARCMoveWeak(destLV.getAddress(), srcAddr);
      }
      return true;
    }

    default:
      return false;
    }

    init = castExpr->getSubExpr();
  }

  return false;
}

static bool isAccessedBy(const VarDecl &var, const Stmt *s) {
  if (const Expr *e = dyn_cast<Expr>(s)) {
    s = e = e->IgnoreParenCasts();

    if (const auto *ref = dyn_cast<DeclRefExpr>(e))
      return ref->getDecl() == &var;
    if (const auto *blockExpr = dyn_cast<BlockExpr>(e)) {
      const BlockDecl *blockDecl = blockExpr->getBlockDecl();
      for (const auto &capture : blockDecl->captures()) {
        if (capture.getVariable() == &var)
          return true;
      }
    }
  }

  for (const Stmt *subStmt : s->children()) {
    if (subStmt && isAccessedBy(var, subStmt))
      return true;
  }

  return false;
}

static bool isAccessedBy(const ValueDecl *decl, const Expr *e) {
  const auto *var = dyn_cast_or_null<VarDecl>(decl);
  return var && isAccessedBy(*var, e);
}

void CIRGenFunction::emitScalarInit(const Expr *init, mlir::Location loc,
                                    LValue lvalue, bool capturedByInit,
                                    const ValueDecl *d) {
  SourceLocRAIIObject locRAII{*this, loc};
  Qualifiers::ObjCLifetime lifetime = lvalue.getType().getObjCLifetime();
  if (lifetime && isa<CXXDefaultInitExpr>(init))
    init = cast<CXXDefaultInitExpr>(init)->getExpr();

  if (lifetime) {
    if (const auto *cleanups = dyn_cast<ExprWithCleanups>(init)) {
      RunCleanupsScope scope(*this);
      return emitScalarInit(cleanups->getSubExpr(), loc, lvalue,
                            capturedByInit, d);
    }
  }

  if (!lifetime) {
    mlir::Value value;
    if (lvalue.getType()->isMemberFunctionPointerType() &&
        !isa<ExplicitCastExpr>(init->IgnoreParens())) {
      if (mlir::Attribute constant =
              ConstantEmitter(*this).tryEmitAbstract(init, lvalue.getType())) {
        value = builder.getConstant(loc, mlir::cast<mlir::TypedAttr>(constant));
      }
    }
    if (!value) {
      if (PointerAuthQualifier ptrAuth =
              lvalue.getQuals().getPointerAuth().withoutKeyNone()) {
        value = emitPointerAuthQualify(ptrAuth, init, lvalue.getAddress());
        lvalue.getQuals().removePointerAuth();
      } else {
        value = emitScalarExpr(init);
      }
    }
    if (capturedByInit) {
      cgm.errorNYI(init->getSourceRange(), "emitScalarInit: captured by init");
      return;
    }
    emitNullabilityCheck(lvalue, value, init->getExprLoc());
    emitStoreThroughLValue(RValue::get(value), lvalue, true);
    return;
  }

  bool accessedByInit = capturedByInit;
  if (!accessedByInit && lifetime != Qualifiers::OCL_ExplicitNone)
    accessedByInit = isAccessedBy(d, init);

  if (accessedByInit) {
    mlir::Value zero = builder.getNullValue(convertType(lvalue.getType()), loc);
    if (lifetime == Qualifiers::OCL_Weak)
      emitARCInitWeak(lvalue.getAddress(), zero);
    else
      emitStoreThroughLValue(RValue::get(zero), lvalue, true);
  }

  mlir::Value value;
  switch (lifetime) {
  case Qualifiers::OCL_Strong:
    if (!d || !isa<VarDecl>(d) || !cast<VarDecl>(d)->isARCPseudoStrong())
      value = emitARCRetainScalarExpr(init);
    else
      // Pseudo-strong locals behave like __unsafe_unretained during init.
      value = emitARCUnsafeUnretainedScalarExpr(init);
    break;
  case Qualifiers::OCL_Autoreleasing:
    value = emitARCRetainAutoreleaseScalarExpr(init);
    break;
  case Qualifiers::OCL_None:
    llvm_unreachable("present but none");
  case Qualifiers::OCL_ExplicitNone:
    value = emitScalarExpr(init);
    break;
  case Qualifiers::OCL_Weak: {
    if (!accessedByInit && tryEmitARCCopyWeakInit(*this, lvalue, init))
      return;

    value = emitScalarExpr(init);
    if (accessedByInit)
      emitARCStoreWeak(lvalue.getAddress(), value, /*ignored=*/true);
    else
      emitARCInitWeak(lvalue.getAddress(), value);
    return;
  }
  }
  if (capturedByInit) {
    cgm.errorNYI(init->getSourceRange(), "emitScalarInit: captured by init");
    return;
  }
  emitNullabilityCheck(lvalue, value, init->getExprLoc());
  if (accessedByInit && lifetime == Qualifiers::OCL_Strong) {
    mlir::Value oldValue = emitLoadOfLValue(lvalue, init->getExprLoc()).getValue();
    emitStoreThroughLValue(RValue::get(value), lvalue, true);
    emitARCRelease(oldValue, loc, /*isImprecise=*/true);
    return;
  }
  emitStoreThroughLValue(RValue::get(value), lvalue, true);
}

void CIRGenFunction::emitExprAsInit(const Expr *init, const ValueDecl *d,
                                    LValue lvalue, bool capturedByInit) {
  SourceLocRAIIObject loc{*this, getLoc(init->getSourceRange())};
  if (capturedByInit) {
    cgm.errorNYI(init->getSourceRange(), "emitExprAsInit: captured by init");
    return;
  }

  QualType type = d->getType();

  if (type->isReferenceType()) {
    RValue rvalue = emitReferenceBindingToExpr(init);
    if (capturedByInit) {
      cgm.errorNYI(init->getSourceRange(), "emitExprAsInit: captured by init");
    }
    emitStoreThroughLValue(rvalue, lvalue, /*isInit=*/true);
    return;
  }
  switch (CIRGenFunction::getEvaluationKind(type)) {
  case cir::TEK_Scalar: {
    mlir::Location loc = getLoc(d->getSourceRange());
    if (const auto *var = dyn_cast<VarDecl>(d))
      loc = getAutoVarDebugLoc(*this, *var);
    emitScalarInit(init, loc, lvalue, capturedByInit, d);
    return;
  }
  case cir::TEK_Complex: {
    mlir::Value complex = emitComplexExpr(init);
    if (capturedByInit) {
      cgm.errorNYI(init->getSourceRange(),
                   "emitExprAsInit: complex type captured by init");
      return;
    }
    mlir::Location loc = getLoc(init->getExprLoc());
    emitStoreOfComplex(loc, complex, lvalue,
                       /*isInit*/ true);
    return;
  }
  case cir::TEK_Aggregate:
    if (type->isAtomicType()) {
      emitAtomicInit(const_cast<Expr *>(init), lvalue);
    } else {
      AggValueSlot::Overlap_t overlap = AggValueSlot::MayOverlap;
      if (isa<VarDecl>(*d))
        overlap = AggValueSlot::DoesNotOverlap;
      else if (auto *fd = dyn_cast<FieldDecl>(d))
        overlap = getOverlapForFieldInit(fd);
      emitAggExpr(
          init, AggValueSlot::forLValue(lvalue, AggValueSlot::IsDestructed,
                                        AggValueSlot::IsNotAliased, overlap));
    }
    return;
  }
  llvm_unreachable("bad evaluation kind");
}

void CIRGenFunction::emitDecl(const Decl &d, bool evaluateConditionDecl) {
  switch (d.getKind()) {
  case Decl::BuiltinTemplate:
  case Decl::TranslationUnit:
  case Decl::ExternCContext:
  case Decl::Namespace:
  case Decl::UnresolvedUsingTypename:
  case Decl::ClassTemplateSpecialization:
  case Decl::ClassTemplatePartialSpecialization:
  case Decl::VarTemplateSpecialization:
  case Decl::VarTemplatePartialSpecialization:
  case Decl::TemplateTypeParm:
  case Decl::UnresolvedUsingValue:
  case Decl::NonTypeTemplateParm:
  case Decl::CXXDeductionGuide:
  case Decl::CXXMethod:
  case Decl::CXXConstructor:
  case Decl::CXXDestructor:
  case Decl::CXXConversion:
  case Decl::Field:
  case Decl::MSProperty:
  case Decl::IndirectField:
  case Decl::ObjCIvar:
  case Decl::ObjCAtDefsField:
  case Decl::ParmVar:
  case Decl::ImplicitParam:
  case Decl::ClassTemplate:
  case Decl::VarTemplate:
  case Decl::FunctionTemplate:
  case Decl::TypeAliasTemplate:
  case Decl::TemplateTemplateParm:
  case Decl::ObjCMethod:
  case Decl::ObjCCategory:
  case Decl::ObjCProtocol:
  case Decl::ObjCInterface:
  case Decl::ObjCCategoryImpl:
  case Decl::ObjCImplementation:
  case Decl::ObjCProperty:
  case Decl::ObjCCompatibleAlias:
  case Decl::PragmaComment:
  case Decl::PragmaDetectMismatch:
  case Decl::AccessSpec:
  case Decl::LinkageSpec:
  case Decl::Export:
  case Decl::ObjCPropertyImpl:
  case Decl::FileScopeAsm:
  case Decl::Friend:
  case Decl::FriendTemplate:
  case Decl::Block:
  case Decl::OutlinedFunction:
  case Decl::Captured:
  case Decl::UsingShadow:
  case Decl::ConstructorUsingShadow:
  case Decl::ObjCTypeParam:
  case Decl::Binding:
  case Decl::UnresolvedUsingIfExists:
  case Decl::HLSLBuffer:
  case Decl::HLSLRootSignature:
  case Decl::TopLevelStmt:
    llvm_unreachable("Declaration should not be in declstmts!");

  case Decl::Function:     // void X();
  case Decl::EnumConstant: // enum ? { X = ? }
  case Decl::StaticAssert: // static_assert(X, ""); [C++0x]
  case Decl::Label:        // __label__ x;
  case Decl::Import:
  case Decl::MSGuid: // __declspec(uuid("..."))
  case Decl::TemplateParamObject:
  case Decl::Empty:
  case Decl::Concept:
  case Decl::ImplicitConceptSpecialization:
  case Decl::LifetimeExtendedTemporary:
  case Decl::RequiresExprBody:
  case Decl::UnnamedGlobalConstant:
    // None of these decls require codegen support.
    return;

  case Decl::Enum:      // enum X;
  case Decl::Record:    // struct/union/class X;
  case Decl::CXXRecord: // struct/union/class X; [C++]
  case Decl::NamespaceAlias:
  case Decl::Using:          // using X; [C++]
  case Decl::UsingEnum:      // using enum X; [C++]
  case Decl::UsingDirective: // using namespace X; [C++]
    // OGCG may complete debug type info here. CIR defers debug metadata
    // materialization to lowering.
    return;
  case Decl::Var:
  case Decl::Decomposition: {
    const VarDecl &vd = cast<VarDecl>(d);
    assert(vd.isLocalVarDecl() &&
           "Should not see file-scope variables inside a function!");
    emitVarDecl(vd);
    if (evaluateConditionDecl)
      maybeEmitDeferredVarDeclInit(&vd);
    return;
  }
  case Decl::OpenACCDeclare:
    emitOpenACCDeclare(cast<OpenACCDeclareDecl>(d));
    return;
  case Decl::OpenACCRoutine:
    emitOpenACCRoutine(cast<OpenACCRoutineDecl>(d));
    return;
  case Decl::OMPThreadPrivate:
    emitOMPThreadPrivateDecl(cast<OMPThreadPrivateDecl>(d));
    return;
  case Decl::OMPGroupPrivate:
    emitOMPGroupPrivateDecl(cast<OMPGroupPrivateDecl>(d));
    return;
  case Decl::OMPAllocate:
    emitOMPAllocateDecl(cast<OMPAllocateDecl>(d));
    return;
  case Decl::OMPCapturedExpr:
    emitOMPCapturedExpr(cast<OMPCapturedExprDecl>(d));
    return;
  case Decl::OMPRequires:
    emitOMPRequiresDecl(cast<OMPRequiresDecl>(d));
    return;
  case Decl::OMPDeclareMapper:
    emitOMPDeclareMapper(cast<OMPDeclareMapperDecl>(d));
    return;
  case Decl::OMPDeclareReduction:
    emitOMPDeclareReduction(cast<OMPDeclareReductionDecl>(d));
    return;
  case Decl::Typedef:     // typedef int X;
  case Decl::TypeAlias: { // using X = int; [C++0x]
    QualType ty = cast<TypedefNameDecl>(d).getUnderlyingType();
    // OGCG may complete debug type info here. CIR defers debug metadata
    // materialization to lowering.
    if (ty->isVariablyModifiedType())
      emitVariablyModifiedType(ty);
    return;
  }
  case Decl::UsingPack:
    for (auto *using_ : cast<UsingPackDecl>(d).expansions())
      emitDecl(*using_, evaluateConditionDecl);
    return;
  }
}

void CIRGenFunction::emitNullabilityCheck(LValue lhs, mlir::Value rhs,
                                          SourceLocation loc) {
  if (!sanOpts.has(SanitizerKind::NullabilityAssign))
    return;

  auto nullability = lhs.getType()->getNullability();
  if (!nullability || *nullability != NullabilityKind::NonNull)
    return;

  mlir::Location checkLoc = getLoc(loc);
  mlir::Value isNotNull = emitNonNullRValueCheck(RValue::get(rhs), lhs.getType());
  isNotNull = emitGuardedUBSanCheck(checkLoc, isNotNull,
                                    SanitizerKind::SO_NullabilityAssign);

  auto ifOp = cir::IfOp::create(
      builder, checkLoc, isNotNull, /*withElseRegion=*/true,
      [&](mlir::OpBuilder &, mlir::Location ifLoc) {
        builder.createYield(ifLoc);
      },
      [&](mlir::OpBuilder &, mlir::Location ifLoc) {
        if (cgm.getCodeGenOpts().SanitizeTrap.has(
                SanitizerKind::NullabilityAssign)) {
          auto trap = cir::TrapOp::create(builder, ifLoc);
          trap->setAttr(
              "cir.ubsantrap_handler",
              mlir::IntegerAttr::get(
                  mlir::IntegerType::get(builder.getContext(), 8),
                  kTypeMismatchTrapHandlerID));
          markNoSanitize(trap);
          return;
        }

        auto intPtrTy =
            mlir::cast<cir::IntType>(convertType(getContext().getUIntPtrType()));
        mlir::Value ptrAsInt = builder.createPtrToInt(rhs, intPtrTy);
        if (auto *op = ptrAsInt.getDefiningOp())
          markNoSanitize(op);

        llvm::StringRef handlerKind = cgm.getCodeGenOpts().SanitizeRecover.has(
                                          SanitizerKind::NullabilityAssign)
                                          ? "recover"
                                          : "abort";
        emitTypeMismatchStubCall(*this, ifLoc, loc, ptrAsInt, lhs.getType(),
                                 /*log2Align=*/0, TCK_NonnullAssign,
                                 handlerKind);
        if (handlerKind == "abort") {
          auto unreachable = cir::UnreachableOp::create(builder, ifLoc);
          markNoSanitize(unreachable);
        } else {
          auto yield = cir::YieldOp::create(builder, ifLoc);
          markNoSanitize(yield);
        }
      });
  setSanitizerBranchAttrs(builder, ifOp);
}

namespace {
struct DestroyObject final : EHScopeStack::Cleanup {
  DestroyObject(Address addr, QualType type,
                CIRGenFunction::Destroyer *destroyer,
                bool useEHCleanupForArray)
      : addr(addr), type(type), destroyer(destroyer),
        useEHCleanupForArray(useEHCleanupForArray) {}

  Address addr;
  QualType type;
  CIRGenFunction::Destroyer *destroyer;
  bool useEHCleanupForArray;

  void emit(CIRGenFunction &cgf, Flags flags) override {
    // Don't use an EH cleanup recursively from an EH cleanup.
    bool useEHCleanupForArray =
        flags.isForNormalCleanup() && this->useEHCleanupForArray;

    cgf.emitDestroy(addr, type, destroyer, useEHCleanupForArray);
  }
};

template <class Derived> struct DestroyNRVOVariable : EHScopeStack::Cleanup {
  DestroyNRVOVariable(Address addr, QualType type, mlir::Value nrvoFlag)
      : nrvoFlag(nrvoFlag), addr(addr), ty(type) {}

  mlir::Value nrvoFlag;
  Address addr;
  QualType ty;

  void emit(CIRGenFunction &cgf, Flags flags) override {
    // Along the exceptions path we always execute the dtor.
    bool nrvo = flags.isForNormalCleanup() && nrvoFlag;

    CIRGenBuilderTy &builder = cgf.getBuilder();
    mlir::OpBuilder::InsertionGuard guard(builder);
    if (nrvo) {
      // If we exited via NRVO, we skip the destructor call.
      mlir::Location loc = addr.getPointer().getLoc();
      mlir::Value didNRVO = builder.createFlagLoad(loc, nrvoFlag);
      mlir::Value notNRVO = builder.createNot(didNRVO);
      cir::IfOp::create(builder, loc, notNRVO, /*withElseRegion=*/false,
                        [&](mlir::OpBuilder &b, mlir::Location) {
                          static_cast<Derived *>(this)->emitDestructorCall(cgf);
                          builder.createYield(loc);
                        });
    } else {
      static_cast<Derived *>(this)->emitDestructorCall(cgf);
    }
  }

  virtual ~DestroyNRVOVariable() = default;
};

struct DestroyNRVOVariableCXX final
    : DestroyNRVOVariable<DestroyNRVOVariableCXX> {
  DestroyNRVOVariableCXX(Address addr, QualType type,
                         const CXXDestructorDecl *dtor, mlir::Value nrvoFlag)
      : DestroyNRVOVariable<DestroyNRVOVariableCXX>(addr, type, nrvoFlag),
        dtor(dtor) {}

  const CXXDestructorDecl *dtor;

  void emitDestructorCall(CIRGenFunction &cgf) {
    cgf.emitCXXDestructorCall(dtor, Dtor_Complete,
                              /*forVirtualBase=*/false,
                              /*delegating=*/false, addr, ty);
  }
};

struct CallStackRestore final : EHScopeStack::Cleanup {
  bool isRedundantBeforeReturn() override { return true; }
  Address stack;
  CallStackRestore(Address stack) : stack(stack) {}
  void emit(CIRGenFunction &cgf, Flags flags) override {
    mlir::Location loc = stack.getPointer().getLoc();
    mlir::Value v = cgf.getBuilder().createLoad(loc, stack);
    cgf.getBuilder().createStackRestore(loc, v);
  }
};

/// A cleanup which performs a partial array destroy where the end pointer is
/// irregularly determined and must be loaded from a local.
struct IrregularPartialArrayDestroy final : EHScopeStack::Cleanup {
  mlir::Value arrayBegin;
  Address arrayEndPointer;
  QualType elementType;
  CIRGenFunction::Destroyer *destroyer;
  CharUnits elementAlign;

  IrregularPartialArrayDestroy(mlir::Value arrayBegin, Address arrayEndPointer,
                               QualType elementType, CharUnits elementAlign,
                               CIRGenFunction::Destroyer *destroyer)
      : arrayBegin(arrayBegin), arrayEndPointer(arrayEndPointer),
        elementType(elementType), destroyer(destroyer),
        elementAlign(elementAlign) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    CIRGenBuilderTy &builder = cgf.getBuilder();
    mlir::Location loc = *cgf.currSrcLoc;

    // Load the current end pointer (tracks how far construction got).
    mlir::Value endPtr = builder.createLoad(loc, arrayEndPointer);

    // If begin == end, nothing was constructed — skip destruction.
    mlir::Value isEmpty = cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq,
                                             arrayBegin, endPtr);

    // Drill down through nested array types to the element type, matching
    // OGCG's emitPartialArrayDestroy behavior.
    QualType baseElementType = elementType;
    while (const ArrayType *arrayType =
               cgf.getContext().getAsArrayType(baseElementType))
      baseElementType = arrayType->getElementType();

    mlir::Type cirElementType = cgf.convertTypeForMem(baseElementType);
    auto ptrToElemTy = builder.getPointerTo(cirElementType);

    // For multi-dimensional arrays (elementType is itself an array, e.g.
    // A[3]), the begin/end pointers have a different type than the base
    // element pointer (e.g. !cir.ptr<!cir.array<!rec_A x 3>> vs
    // !cir.ptr<!rec_A>). Cast them to the base element pointer type so we
    // can iterate element-by-element through the flat buffer.
    // OGCG does this via GEP with zero indices; in CIR we use bitcast
    // since the addresses are identical.
    mlir::Value beginVal = arrayBegin;
    mlir::Value endVal = endPtr;
    if (beginVal.getType() != ptrToElemTy)
      beginVal = builder.createPtrBitcast(beginVal, cirElementType);
    if (endVal.getType() != ptrToElemTy)
      endVal = builder.createPtrBitcast(endVal, cirElementType);

    cir::IfOp::create(
        builder, loc, isEmpty, /*withElseRegion=*/true,
        // then: empty — nothing to destroy
        [&](mlir::OpBuilder &, mlir::Location) {
          cir::YieldOp::create(builder, loc);
        },
        // else: destroy elements from end backward to begin, matching
        // OGCG's pattern: start at past-the-end, decrement first, then
        // destroy. This ensures all elements including index 0 are destroyed.
        [&](mlir::OpBuilder &, mlir::Location) {
          // Alloca for the current pointer (iteration variable).
          Address curPtrAddr = cgf.createTempAlloca(
              ptrToElemTy, cgf.getPointerAlign(), loc,
              "arraydestroy.cur");

          cir::ConstantOp negOne = builder.getConstInt(
              loc, mlir::cast<cir::IntType>(cgf.ptrDiffTy), -1);

          // Start at end (past-the-end pointer).
          builder.createStore(loc, endVal, curPtrAddr);

          builder.createDoWhile(
              loc,
              /*condBuilder=*/
              [&](mlir::OpBuilder &, mlir::Location loc) {
                mlir::Value curPtr = builder.createLoad(loc, curPtrAddr);
                cir::CmpOp cmp = cir::CmpOp::create(
                    builder, loc, cir::CmpOpKind::ne, curPtr, beginVal);
                builder.createCondition(cmp);
              },
              /*bodyBuilder=*/
              [&](mlir::OpBuilder &, mlir::Location loc) {
                mlir::Value curPtr = builder.createLoad(loc, curPtrAddr);

                // Decrement first: curPtr = curPtr - 1
                mlir::Value prev = cir::PtrStrideOp::create(
                    builder, loc, ptrToElemTy, curPtr, negOne,
                    cir::GEPNoWrapFlags::inbounds);
                builder.createStore(loc, prev, curPtrAddr);

                // Then call the destructor on the decremented element.
                Address curAddr(prev, cirElementType, elementAlign);
                destroyer(cgf, curAddr, baseElementType);

                cir::YieldOp::create(builder, loc);
              });
          cir::YieldOp::create(builder, loc);
        });
  }
};
} // namespace

/// Push the standard destructor for the given type as
/// at least a normal cleanup.
void CIRGenFunction::pushDestroy(QualType::DestructionKind dtorKind,
                                 Address addr, QualType type) {
  assert(dtorKind && "cannot push destructor for trivial type");

  CleanupKind cleanupKind = getCleanupKind(dtorKind);
  pushDestroy(cleanupKind, addr, type, getDestroyer(dtorKind),
              cleanupKind & EHCleanup);
}

void CIRGenFunction::pushDestroy(CleanupKind cleanupKind, Address addr,
                                 QualType type, Destroyer *destroyer,
                                 bool useEHCleanupForArray) {
  pushFullExprCleanup<DestroyObject>(cleanupKind, addr, type, destroyer,
                                     useEHCleanupForArray);
}

void CIRGenFunction::pushDestroyAndDeferDeactivation(
    QualType::DestructionKind dtorKind, Address addr, QualType type) {
  assert(dtorKind && "cannot push destructor for trivial type");
  CleanupKind cleanupKind = getCleanupKind(dtorKind);
  pushDestroyAndDeferDeactivation(cleanupKind, addr, type,
                                  getDestroyer(dtorKind),
                                  cleanupKind & EHCleanup);
}

void CIRGenFunction::pushDestroyAndDeferDeactivation(
    CleanupKind cleanupKind, Address addr, QualType type, Destroyer *destroyer,
    bool useEHCleanupForArray) {
  // Push the destroy cleanup.
  pushDestroy(cleanupKind, addr, type, destroyer, useEHCleanupForArray);

  // Create an active flag for this cleanup if it doesn't already have one.
  // When inside a conditional branch (e.g., ternary else), pushFullExprCleanup
  // may already set an active flag for conditional execution. In that case,
  // skip creating a second one — the deactivation scope will still deactivate
  // the cleanup correctly.
  EHCleanupScope &cleanup = cast<EHCleanupScope>(*ehStack.begin());
  if (!cleanup.hasActiveFlag()) {
    Address activeFlag = createCleanupActiveFlag();
    initFullExprCleanupWithFlag(activeFlag);
  }

  // Record the cleanup for deferred deactivation.
  deferredDeactivationCleanupStack.push_back(
      {ehStack.stable_begin(), /*dominatingIP=*/nullptr});
}

/// pushEHDestroy - Push the standard destructor for the given type as
/// an EH-only cleanup.
void CIRGenFunction::pushEHDestroy(QualType::DestructionKind dtorKind,
                                   Address addr, QualType type) {
  assert(dtorKind && "cannot push destructor for trivial type");
  assert(needsEHCleanup(dtorKind));
  pushDestroy(EHCleanup, addr, type, getDestroyer(dtorKind), true);
}

void CIRGenFunction::pushLifetimeExtendedDestroy(
    QualType::DestructionKind dtorKind, Address addr, QualType type) {
  CleanupKind cleanupKind = getCleanupKind(dtorKind);
  pushLifetimeExtendedDestroy(cleanupKind, addr, type, getDestroyer(dtorKind),
                              cleanupKind & EHCleanup);
}

void CIRGenFunction::pushLifetimeExtendedDestroy(CleanupKind cleanupKind,
                                                  Address addr, QualType type,
                                                  Destroyer *destroyer,
                                                  bool useEHCleanupForArray) {
  // If we're not in a conditional branch, we don't need to bother generating a
  // conditional cleanup.
  if (!isInConditionalBranch()) {
    // FIXME: When popping normal cleanups, we need to keep this EH cleanup
    // around in case a temporary's destructor throws an exception.

    // Add the cleanup to the EHStack. After the full-expr, this would be
    // deactivated before being popped from the stack.
    pushDestroyAndDeferDeactivation(cleanupKind, addr, type, destroyer,
                                    useEHCleanupForArray);

    // Since this is lifetime-extended, push it once again to the EHStack after
    // the full expression.
    return pushCleanupAfterFullExprWithActiveFlag<DestroyObject>(
        cleanupKind, Address::invalid(), addr, type, destroyer,
        useEHCleanupForArray);
  }

  // In a conditional branch, keep the destroy cleanup active for the remainder
  // of the current full expression, then queue the lifetime-extended cleanup
  // that takes over after that active cleanup is deactivated. This matches
  // OGCG's conditional lifetime-extension path and lets sibling branches see
  // the live cleanup when deciding between call and invoke.
  pushDestroyAndDeferDeactivation(cleanupKind, addr, type, destroyer,
                                  useEHCleanupForArray);
  pushCleanupAfterFullExpr<DestroyObject>(cleanupKind, addr, type, destroyer,
                                          useEHCleanupForArray);
}

/// A cleanup to destroy already-constructed elements of an array during
/// exception unwinding. The end pointer is regularly determined: it's
/// stored in an alloca that the destruction loop updates each iteration.
/// Mirrors OGCG's RegularPartialArrayDestroy (CGDecl.cpp).
struct RegularPartialArrayDestroy final : EHScopeStack::Cleanup {
  mlir::Value arrayBegin;
  Address cursorAddr; // alloca tracking current destruction position
  QualType elementType;
  CIRGenFunction::Destroyer *destroyer;
  CharUnits elementAlign;

  RegularPartialArrayDestroy(mlir::Value arrayBegin, Address cursorAddr,
                             QualType elementType, CharUnits elementAlign,
                             CIRGenFunction::Destroyer *destroyer)
      : arrayBegin(arrayBegin), cursorAddr(cursorAddr),
        elementType(elementType), destroyer(destroyer),
        elementAlign(elementAlign) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    CIRGenBuilderTy &builder = cgf.getBuilder();
    mlir::Location loc = *cgf.currSrcLoc;

    // Load the current cursor position (where destruction stopped/failed).
    mlir::Value endPtr = builder.createLoad(loc, cursorAddr);

    // If begin == end, nothing left to destroy.
    mlir::Value isEmpty = cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq,
                                             arrayBegin, endPtr);
    cir::IfOp::create(
        builder, loc, builder.createNot(isEmpty),
        /*withElseRegion=*/false,
        [&](mlir::OpBuilder &, mlir::Location loc) {
          mlir::Type cirElementType = cgf.convertTypeForMem(elementType);
          auto ptrToElmType = builder.getPointerTo(cirElementType);
          Address loopCurAddr = cgf.createTempAlloca(
              ptrToElmType, cgf.getPointerAlign(), loc,
              "__partial_array_dtor_cur");
          builder.createStore(loc, endPtr, loopCurAddr);

          cir::ConstantOp negOne = builder.getConstInt(
              loc,
              mlir::cast<cir::IntType>(cgf.getBuilder().getSIntNTy(64)), -1);

          builder.createDoWhile(
              loc,
              [&](mlir::OpBuilder &, mlir::Location loc) {
                mlir::Value cur = builder.createLoad(loc, loopCurAddr);
                auto done = builder.createCompare(loc, cir::CmpOpKind::ne,
                                                  cur, arrayBegin);
                builder.createCondition(done);
              },
              [&](mlir::OpBuilder &, mlir::Location loc) {
                mlir::Value cur = builder.createLoad(loc, loopCurAddr);
                mlir::Value prev = builder.createPtrStride(loc, cur, negOne);
                builder.createStore(loc, prev, loopCurAddr);
                Address elemAddr(prev, cirElementType, elementAlign);
                // No nested EH cleanup to avoid infinite recursion.
                destroyer(cgf, elemAddr, elementType);
                cir::YieldOp::create(builder, loc);
              });

          cir::YieldOp::create(builder, loc);
        });
  }
};

/// Push an EH cleanup to destroy already-constructed elements of the given
/// array.  The cleanup may be popped with DeactivateCleanupBlock or
/// PopCleanupBlock.
///
/// \param elementType - the immediate element type of the array;
///   possibly still an array type
void CIRGenFunction::pushIrregularPartialArrayCleanup(mlir::Value arrayBegin,
                                                      Address arrayEndPointer,
                                                      QualType elementType,
                                                      CharUnits elementAlign,
                                                      Destroyer *destroyer,
                                                      CleanupKind cleanupKind) {
  pushFullExprCleanup<IrregularPartialArrayDestroy>(
      cleanupKind, arrayBegin, arrayEndPointer, elementType, elementAlign,
      destroyer);
}

void CIRGenFunction::pushRegularPartialArrayCleanup(mlir::Value begin,
                                                    mlir::Value cur,
                                                    QualType elementType,
                                                    CharUnits elementAlign,
                                                    Destroyer *destroyer) {
  // `cur` is expected to be the cursor alloca pointer that the destruction
  // loop updates each iteration. The cleanup loads from it to determine
  // the current destruction position.
  auto ptrToElmType = builder.getPointerTo(convertTypeForMem(elementType));
  Address curAddr(cur, ptrToElmType, getPointerAlign());
  pushFullExprCleanup<RegularPartialArrayDestroy>(EHCleanup, begin, curAddr,
                                                  elementType, elementAlign,
                                                  destroyer);
}

/// Destroys all the elements of the given array, beginning from last to first.
/// The array cannot be zero-length.
///
/// \param begin - a type* denoting the first element of the array
/// \param numElements - the number of elements in the array
/// \param elementType - the element type of the array
/// \param destroyer - the function to call to destroy elements
void CIRGenFunction::emitArrayDestroy(mlir::Value begin,
                                      mlir::Value numElements,
                                      QualType elementType,
                                      CharUnits elementAlign,
                                      Destroyer *destroyer,
                                      bool useEHCleanup) {
  assert(!elementType->isArrayType());

  // Differently from LLVM traditional codegen, use a higher level
  // representation instead of lowering directly to a loop.
  mlir::Type cirElementType = convertTypeForMem(elementType);
  // The element pointer must live in the same address space as the array base
  // (e.g. an array global in a non-default address space); otherwise the
  // element stride/cleanup pointers cross address spaces and lower to an
  // invalid cast.
  auto beginAddrSpace =
      mlir::cast<cir::PointerType>(begin.getType()).getAddrSpace();
  cir::PointerType ptrToElmType =
      builder.getPointerTo(cirElementType, beginAddrSpace);

  uint64_t size = 0;
  bool isDynamic = false;

  // Optimize for a constant array size.
  if (auto constantCount = numElements.getDefiningOp<cir::ConstantOp>()) {
    if (auto constIntAttr = constantCount.getValueAttr<cir::IntAttr>())
      size = constIntAttr.getUInt();
  } else {
    // Dynamic array size (VLA). Use element-pointer-based destruction
    // loop instead of ArrayDtor (which requires a static array type).
    isDynamic = true;
  }

  // Only use the manual loop for EH cleanup when exceptions are actually
  // enabled. The useEHCleanup flag may be true even without -fexceptions
  // (from getCleanupKind returning NormalAndEHCleanup), but in that case
  // the EH cleanup never fires, so we can use the efficient ArrayDtor path.
  bool needsManualLoop = isDynamic ||
                         (useEHCleanup && getLangOpts().Exceptions);

  if (needsManualLoop) {
    // For VLA destruction or when EH cleanup is needed, emit a reverse
    // do-while loop. ArrayDtor can't support per-element EH cleanup because
    // it doesn't expose the iteration cursor to the surrounding scope.
    //   if (begin != end) { ptr = end; do { --ptr; destroy(*ptr); } while (ptr != begin); }

    // Ensure begin is an element pointer (it might be a pointer-to-array).
    if (begin.getType() != ptrToElmType)
      begin = builder.createPtrBitcast(begin, cirElementType);

    // Compute end pointer: begin + numElements (or begin + constant size)
    mlir::Value endPtr;
    if (isDynamic) {
      endPtr = builder.createPtrStride(*currSrcLoc, begin, numElements);
    } else {
      // Constant size — compute end from known size.
      auto sizeConst = builder.getConstInt(
          *currSrcLoc, mlir::cast<cir::IntType>(ptrDiffTy), size);
      endPtr = builder.createPtrStride(*currSrcLoc, begin, sizeConst);
    }

    auto isEmpty =
        builder.createCompare(*currSrcLoc, cir::CmpOpKind::eq, begin, endPtr);
    cir::IfOp::create(
        builder, *currSrcLoc, isEmpty, /*withElseRegion=*/true,
        /*thenBuilder=*/
        [&](mlir::OpBuilder &, mlir::Location loc) {
          // Empty array — nothing to destroy.
          cir::YieldOp::create(builder, loc);
        },
        /*elseBuilder=*/
        [&](mlir::OpBuilder &, mlir::Location loc) {
          // Destruction loop (reverse order).
          Address curAddr = createTempAlloca(
              ptrToElmType, getPointerAlign(), loc, "__array_dtor_cur");
          builder.createStore(loc, endPtr, curAddr);

          cir::ConstantOp negOne = builder.getConstInt(
              loc, mlir::cast<cir::IntType>(ptrDiffTy), -1);

          builder.createDoWhile(
              loc,
              /*condBuilder=*/
              [&](mlir::OpBuilder &, mlir::Location loc) {
                mlir::Value cur = builder.createLoad(loc, curAddr);
                auto done = builder.createCompare(loc, cir::CmpOpKind::ne,
                                                  cur, begin);
                builder.createCondition(done);
              },
              /*bodyBuilder=*/
              [&](mlir::OpBuilder &, mlir::Location loc) {
                mlir::Value cur = builder.createLoad(loc, curAddr);
                mlir::Value prev =
                    builder.createPtrStride(loc, cur, negOne);
                builder.createStore(loc, prev, curAddr);
                Address elemAddr(prev, cirElementType, elementAlign);
                if (useEHCleanup)
                  pushRegularPartialArrayCleanup(begin,
                                                 curAddr.getPointer(),
                                                 elementType, elementAlign,
                                                 destroyer);
                destroyer(*this, elemAddr, elementType);
                if (useEHCleanup)
                  popCleanupBlock();
                cir::YieldOp::create(builder, loc);
              });
          cir::YieldOp::create(builder, loc);
        });
    return;
  }

  auto arrayTy = cir::ArrayType::get(cirElementType, size);
  mlir::Value arrayOp = builder.createPtrBitcast(begin, arrayTy);

  // Emit the dtor call that will execute for every array element.
  // This path is only taken for constant-size arrays without EH cleanup.
  cir::ArrayDtor::create(
      builder, *currSrcLoc, arrayOp,
      [&](mlir::OpBuilder &b, mlir::Location loc) {
        auto arg = b.getInsertionBlock()->addArgument(ptrToElmType, loc);
        Address curAddr = Address(arg, cirElementType, elementAlign);

        // Perform the actual destruction there.
        destroyer(*this, curAddr, elementType);

        cir::YieldOp::create(builder, loc);
      });
}

/// Immediately perform the destruction of the given object.
///
/// \param addr - the address of the object; a type*
/// \param type - the type of the object; if an array type, all
///   objects are destroyed in reverse order
/// \param destroyer - the function to call to destroy individual
///   elements
/// \param useEHCleanupForArray - whether to push an EH cleanup to destroy
///   the remaining elements in case the destruction of a single
///   element throws
void CIRGenFunction::emitDestroy(Address addr, QualType type,
                                 Destroyer *destroyer,
                                 bool useEHCleanupForArray) {
  const ArrayType *arrayType = getContext().getAsArrayType(type);
  if (!arrayType)
    return destroyer(*this, addr, type);

  mlir::Value length = emitArrayLength(arrayType, type, addr);

  CharUnits elementAlign = addr.getAlignment().alignmentOfArrayElement(
      getContext().getTypeSizeInChars(type));

  auto constantCount = length.getDefiningOp<cir::ConstantOp>();

  // If it's a constant zero, we can just skip the entire thing.
  if (constantCount) {
    auto constIntAttr = mlir::dyn_cast<cir::IntAttr>(constantCount.getValue());
    if (constIntAttr && constIntAttr.getUInt() == 0)
      return;
  }

  mlir::Value begin = addr.getPointer();
  emitArrayDestroy(begin, length, type, elementAlign, destroyer,
                   useEHCleanupForArray);

  // If the array destroy didn't use the length op, we can erase it.
  if (constantCount && constantCount.use_empty())
    constantCount.erase();
}

CIRGenFunction::Destroyer *
CIRGenFunction::getDestroyer(QualType::DestructionKind kind) {
  switch (kind) {
  case QualType::DK_none:
    llvm_unreachable("no destroyer for trivial dtor");
  case QualType::DK_cxx_destructor:
    return destroyCXXObject;
  case QualType::DK_objc_strong_lifetime:
    return destroyARCStrongImprecise;
  case QualType::DK_objc_weak_lifetime:
    return destroyARCWeak;
  case QualType::DK_nontrivial_c_struct:
    return destroySupportedNonTrivialCStructWithThunk;
  }
  llvm_unreachable("Unknown DestructionKind");
}

void CIRGenFunction::pushStackRestore(CleanupKind kind, Address spMem) {
  ehStack.pushCleanup<CallStackRestore>(kind, spMem);
}

/// Enter a destroy cleanup for the given local variable.
void CIRGenFunction::emitAutoVarTypeCleanup(
    const CIRGenFunction::AutoVarEmission &emission,
    QualType::DestructionKind dtorKind) {
  assert(dtorKind != QualType::DK_none);

  // Note that for __block variables, we want to destroy the
  // original stack object, not the possibly forwarded object.
  Address addr = emission.getObjectAddress(*this);

  const VarDecl *var = emission.variable;
  QualType type = var->getType();

  CleanupKind cleanupKind = getCleanupKind(dtorKind);
  CIRGenFunction::Destroyer *destroyer = nullptr;

  switch (dtorKind) {
  case QualType::DK_none:
    llvm_unreachable("no cleanup for trivially-destructible variable");

  case QualType::DK_cxx_destructor:
    // If there's an NRVO flag on the emission, we need a different
    // cleanup.
    if (emission.nrvoFlag) {
      assert(!type->isArrayType());
      CXXDestructorDecl *dtor = type->getAsCXXRecordDecl()->getDestructor();
      ehStack.pushCleanup<DestroyNRVOVariableCXX>(cleanupKind, addr, type, dtor,
                                                  emission.nrvoFlag);
      return;
    }
    // Otherwise, this is handled below.
    break;

  case QualType::DK_objc_strong_lifetime:
    // ObjC ARC strong lifetime: release the object at scope exit.
    // Use imprecise lifetime by default (matches OGCG CGDecl.cpp:2175).
    // Pseudo-strong variables skip cleanup entirely (OGCG CGDecl.cpp:2168).
    if (var->isARCPseudoStrong())
      return;
    // Use getARCCleanupKind() semantics: an EH cleanup runs objc_release on the
    // exception-unwind path only under -fobjc-arc-exceptions. By default ARC
    // deliberately leaks on unwind (matches OGCG getARCCleanupKind).
    cleanupKind = getCleanupKind(QualType::DK_objc_strong_lifetime);
    if (!var->hasAttr<ObjCPreciseLifetimeAttr>())
      destroyer = CIRGenFunction::destroyARCStrongImprecise;
    else
      destroyer = CIRGenFunction::destroyARCStrongPrecise;
    break;

  case QualType::DK_objc_weak_lifetime:
    break;
  case QualType::DK_nontrivial_c_struct:
    if (!isSupportedNonTrivialCStructType(type)) {
      cgm.errorNYI(var->getSourceRange(),
                   "emitAutoVarTypeCleanup: ObjC weak / nontrivial C struct");
      return;
    }
    break;
  }

  // If we haven't chosen a more specific destroyer, use the default.
  if (!destroyer)
    destroyer = getDestroyer(dtorKind);

  // Use an EH cleanup in array destructors iff the destructor itself
  // is being pushed as an EH cleanup.
  bool useEHCleanup = (cleanupKind & EHCleanup);
  ehStack.pushCleanup<DestroyObject>(cleanupKind, addr, type, destroyer,
                                     useEHCleanup);
}

void CIRGenFunction::maybeEmitDeferredVarDeclInit(const VarDecl *vd) {
  if (auto *dd = dyn_cast_if_present<DecompositionDecl>(vd)) {
    for (auto *b : dd->flat_bindings())
      if (auto *hd = b->getHoldingVar())
        emitVarDecl(*hd);
  }
}

//===----------------------------------------------------------------------===//
// ObjC ARC Runtime Helpers
//===----------------------------------------------------------------------===//

/// Helper to get or create a declaration for an ObjC ARC runtime function.
static cir::FuncOp getARCRuntimeFunction(CIRGenModule &cgm,
                                         llvm::StringRef name,
                                         cir::FuncType funcType) {
  constexpr llvm::StringLiteral arcRuntimeAttr = "cir.arc.runtime";
  if (auto *existing = cgm.getGlobalValue(name)) {
    auto fn = mlir::cast<cir::FuncOp>(existing);
    fn->setAttr(arcRuntimeAttr, mlir::UnitAttr::get(&cgm.getMLIRContext()));
    return fn;
  }
  auto fn = cgm.createCIRFunction(
      mlir::UnknownLoc::get(&cgm.getMLIRContext()), name, funcType, nullptr);
  // ObjC ARC runtime functions are nounwind.
  mlir::NamedAttrList attrs;
  if (auto existingExtra = fn.getExtraAttrs())
    attrs = mlir::NamedAttrList{existingExtra->getElements().getValue()};
  auto noThrow = cir::NoThrowAttr::get(&cgm.getMLIRContext());
  attrs.set(noThrow.getMnemonic(), noThrow);
  fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
      attrs.getDictionary(&cgm.getMLIRContext())));
  fn->setAttr(arcRuntimeAttr, mlir::UnitAttr::get(&cgm.getMLIRContext()));
  return fn;
}

static cir::FuncOp getObjCAutoreleasePoolPopRuntimeFunction(CIRGenModule &cgm) {
  auto voidTy = cir::VoidType::get(&cgm.getMLIRContext());
  auto ptrTy = cgm.getBuilder().getVoidPtrTy();
  auto funcType = cir::FuncType::get({ptrTy}, voidTy);
  return cgm.createRuntimeFunction(funcType, "objc_autoreleasePoolPop");
}

static std::string getObjCClassRefsSectionName(const CIRGenModule &cgm) {
  switch (cgm.getTriple().getObjectFormat()) {
  case llvm::Triple::UnknownObjectFormat:
    llvm_unreachable("unexpected object file format");
  case llvm::Triple::MachO:
    return "__DATA,__objc_classrefs,regular,no_dead_strip";
  case llvm::Triple::ELF:
    return "objc_classrefs";
  case llvm::Triple::COFF:
    return ".objc_classrefs$B";
  case llvm::Triple::Wasm:
  case llvm::Triple::GOFF:
  case llvm::Triple::SPIRV:
  case llvm::Triple::XCOFF:
  case llvm::Triple::DXContainer:
    llvm_unreachable(
        "Objective-C support is unimplemented for object file format");
  }

  llvm_unreachable("Unhandled llvm::Triple::ObjectFormatType enum");
}

static mlir::Value getObjCClassByRuntimeName(CIRGenFunction &cgf,
                                             mlir::Location loc,
                                             llvm::StringRef runtimeName) {
  CIRGenModule &cgm = cgf.cgm;
  CIRGenBuilderTy &builder = cgm.getBuilder();

  std::string className = ("OBJC_CLASS_$_" + runtimeName).str();
  cir::GlobalOp classGV;
  if (auto *existing = cgm.getGlobalValue(className)) {
    classGV = mlir::cast<cir::GlobalOp>(existing);
  } else {
    classGV = cgm.createOrReplaceCXXRuntimeVariable(
        loc, className, builder.getUInt8Ty(),
        cir::GlobalLinkageKind::ExternalLinkage, CharUnits::Zero());
    classGV.setConstant(false);
    if (cgm.getTriple().isOSBinFormatCOFF() && classGV.isDeclaration())
      classGV->removeAttr("dso_local");
  }

  std::string classRefName = ("OBJC_CLASSLIST_REFERENCES_$_" + runtimeName).str();
  cir::GlobalOp classRefGV;
  if (auto *existing = cgm.getGlobalValue(classRefName)) {
    classRefGV = mlir::cast<cir::GlobalOp>(existing);
  } else {
    cir::PointerType classPtrTy =
        builder.getPointerTo(classGV.getSymType(), classGV.getAddrSpaceAttr());
    classRefGV = CIRGenModule::createGlobalOp(cgm, loc, classRefName,
                                              classPtrTy, /*isConstant=*/false);
    classRefGV.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
    mlir::SymbolTable::setSymbolVisibility(
        classRefGV, mlir::SymbolTable::Visibility::Private);
    classRefGV.setAlignmentAttr(cgm.getSize(cgf.getPointerAlign()));
    classRefGV.setSectionAttr(
        cgm.getStringAttrCached(getObjCClassRefsSectionName(cgm)));
    CIRGenModule::setInitializer(
        classRefGV, cir::GlobalViewAttr::get(
                        classPtrTy,
                        mlir::FlatSymbolRefAttr::get(classGV.getSymNameAttr())));
    cgm.setDSOLocal(static_cast<mlir::Operation *>(classRefGV));
    cgm.addCompilerUsedGlobal(classRefGV);
  }

  mlir::Value classRefAddr = builder.createGetGlobal(loc, classRefGV);
  return builder.createLoad(
      loc, Address(classRefAddr, classRefGV.getSymType(), cgf.getPointerAlign()));
}

namespace {

static bool shouldUseARCAttachedCallBundle(CIRGenFunction &cgf) {
  llvm::Triple::ArchType arch = cgf.cgm.getTriple().getArch();
  return cgf.cgm.getCodeGenOpts().OptimizationLevel > 0 &&
         (arch == llvm::Triple::aarch64 || arch == llvm::Triple::aarch64_32 ||
          arch == llvm::Triple::x86_64);
}

static mlir::Value emitARCOperationAfterCall(
    CIRGenFunction &cgf, mlir::Value value,
    llvm::function_ref<mlir::Value(CIRGenFunction &, mlir::Value,
                                   mlir::Location)>
        doAfterCall,
    llvm::function_ref<mlir::Value(CIRGenFunction &, mlir::Value,
                                   mlir::Location)>
        doFallback) {
  mlir::OpBuilder::InsertionGuard guard(cgf.getBuilder());
  mlir::Location loc = value.getLoc();
  mlir::Operation *defOp = value.getDefiningOp();

  if (!defOp)
    return doFallback(cgf, value, loc);

  if (defOp->hasAttr("cir.arc.attachedcall"))
    return doFallback(cgf, value, loc);

  if (isa<cir::CallOp>(defOp)) {
    cgf.getBuilder().setInsertionPointAfterValue(value);
    return doAfterCall(cgf, value, loc);
  }

  if (auto castOp = dyn_cast<cir::CastOp>(defOp)) {
    if (castOp.getKind() == cir::CastKind::bitcast) {
      mlir::Value operand = emitARCOperationAfterCall(cgf, castOp.getSrc(),
                                                      doAfterCall, doFallback);
      castOp.setOperand(operand);
      return value;
    }
  }

  return doFallback(cgf, value, loc);
}

/// Emit the target's inline-asm marker that precedes a plain (non
/// attached-call) objc_retain/unsafeClaimAutoreleasedReturnValue call. The
/// runtime scans for this instruction to enable its return-value fast path.
/// Mirrors OGCG CGObjC.cpp emitAutoreleasedReturnValueMarker.
static void emitARCRVMarkerAsm(CIRGenFunction &cgf, mlir::Location loc) {
  llvm::StringRef markerKey =
      cgf.cgm.getTargetCIRGenInfo().getARCRetainAutoreleasedReturnValueMarker();
  if (markerKey.empty())
    return;

  CIRGenBuilderTy &builder = cgf.getBuilder();
  llvm::SmallVector<mlir::ValueRange, 3> operands = {
      mlir::ValueRange{}, mlir::ValueRange{}, mlir::ValueRange{}};
  (void)cir::InlineAsmOp::create(
      builder, loc, mlir::Type{}, operands, /*asmString=*/markerKey,
      /*constraints=*/"", /*sideEffects=*/true, cir::AsmFlavor::x86_att,
      builder.getArrayAttr({}));
}

static mlir::Value emitARCRetainAutoreleasedReturnValueMarker(
    CIRGenFunction &cgf, mlir::Value value, mlir::Location loc) {
  if (shouldUseARCAttachedCallBundle(cgf)) {
    if (auto *defOp = value.getDefiningOp()) {
      defOp->setAttr("cir.arc.attachedcall",
                     cgf.cgm.getStringAttrCached(
                         "llvm.objc.retainAutoreleasedReturnValue"));
      llvm::StringRef marker =
          cgf.cgm.getTargetCIRGenInfo()
              .getARCRetainAutoreleasedReturnValueMarker();
      if (!marker.empty() &&
          !cgf.cgm.getModule()->hasAttr("cir.objc_arc_rv_marker")) {
        cgf.cgm.getModule()->setAttr("cir.objc_arc_rv_marker",
                                     cgf.cgm.getStringAttrCached(marker));
      }
      auto voidTy = cir::VoidType::get(cgf.getBuilder().getContext());
      auto funcTy = cir::FuncType::get({}, voidTy, /*isVarArg=*/true);
      cir::FuncOp noopFn;
      if (auto *existing =
              cgf.cgm.getGlobalValue("llvm.objc.clang.arc.noop.use"))
        noopFn = mlir::cast<cir::FuncOp>(existing);
      else
        noopFn = cgf.cgm.createCIRFunction(
            mlir::UnknownLoc::get(&cgf.cgm.getMLIRContext()),
            "llvm.objc.clang.arc.noop.use", funcTy, nullptr);
      auto noop = cgf.getBuilder().createCallOp(loc, noopFn, {value});
      noop->setAttr("nothrow",
                    mlir::UnitAttr::get(cgf.getBuilder().getContext()));
      return value;
    }
  }

  // Non-attached-call (e.g. -O0) fallback. Emit the target's inline-asm marker
  // immediately before the runtime call so the runtime's return-value
  // optimization can recognize the handshake, matching OGCG's
  // emitAutoreleasedReturnValueMarker.
  emitARCRVMarkerAsm(cgf, loc);

  auto ptrTy = cgf.getBuilder().getVoidPtrTy();
  auto funcType = cir::FuncType::get({ptrTy}, ptrTy);
  cir::FuncOp fn =
      getARCRuntimeFunction(cgf.cgm, "objc_retainAutoreleasedReturnValue",
                            funcType);
  mlir::Value castVal = cgf.getBuilder().createBitcast(value, ptrTy);
  auto callOp = cgf.getBuilder().createCallOp(loc, fn, {castVal});
  callOp->setAttr("nothrow", mlir::UnitAttr::get(cgf.getBuilder().getContext()));
  return cgf.getBuilder().createBitcast(callOp.getResult(), value.getType());
}

static mlir::Value emitARCUnsafeClaimAutoreleasedReturnValueMarker(
    CIRGenFunction &cgf, mlir::Value value, mlir::Location loc) {
  if (shouldUseARCAttachedCallBundle(cgf)) {
    if (auto *defOp = value.getDefiningOp()) {
      defOp->setAttr("cir.arc.attachedcall",
                     cgf.cgm.getStringAttrCached(
                         "llvm.objc.unsafeClaimAutoreleasedReturnValue"));
      auto voidTy = cir::VoidType::get(cgf.getBuilder().getContext());
      auto funcTy = cir::FuncType::get({}, voidTy, /*isVarArg=*/true);
      cir::FuncOp noopFn;
      if (auto *existing =
              cgf.cgm.getGlobalValue("llvm.objc.clang.arc.noop.use"))
        noopFn = mlir::cast<cir::FuncOp>(existing);
      else
        noopFn = cgf.cgm.createCIRFunction(
            mlir::UnknownLoc::get(&cgf.cgm.getMLIRContext()),
            "llvm.objc.clang.arc.noop.use", funcTy, nullptr);
      auto noop = cgf.getBuilder().createCallOp(loc, noopFn, {value});
      noop->setAttr("nothrow",
                    mlir::UnitAttr::get(cgf.getBuilder().getContext()));
      return value;
    }
  }

  // Non-attached-call (e.g. -O0) fallback. Emit the target's inline-asm marker
  // immediately before the runtime call, matching OGCG's
  // emitAutoreleasedReturnValueMarker.
  emitARCRVMarkerAsm(cgf, loc);

  auto ptrTy = cgf.getBuilder().getVoidPtrTy();
  auto funcType = cir::FuncType::get({ptrTy}, ptrTy);
  cir::FuncOp fn =
      getARCRuntimeFunction(cgf.cgm, "objc_unsafeClaimAutoreleasedReturnValue",
                            funcType);
  mlir::Value castVal = cgf.getBuilder().createBitcast(value, ptrTy);
  auto callOp = cgf.getBuilder().createCallOp(loc, fn, {castVal});
  callOp->setAttr("nothrow", mlir::UnitAttr::get(cgf.getBuilder().getContext()));
  return cgf.getBuilder().createBitcast(callOp.getResult(), value.getType());
}

static mlir::Value emitARCRetainPseudoObjectExpr(CIRGenFunction &cgf,
                                                 const PseudoObjectExpr *expr) {
  SmallVector<CIRGenFunction::OpaqueValueMappingData, 4> opaques;

  const Expr *resultExpr = expr->getResultExpr();
  assert(resultExpr && "Pseudo-object expression must have a result");

  mlir::Value result = nullptr;
  for (PseudoObjectExpr::const_semantics_iterator i = expr->semantics_begin(),
                                                  e = expr->semantics_end();
       i != e; ++i) {
    const Expr *semantic = *i;

    if (const auto *ov = dyn_cast<OpaqueValueExpr>(semantic)) {
      if (ov->isUnique()) {
        assert(ov != resultExpr &&
               "A unique OVE cannot be the result expression");
        continue;
      }

      using OVMA = CIRGenFunction::OpaqueValueMappingData;
      OVMA opaqueData;
      if (ov == resultExpr) {
        assert(!OVMA::shouldBindAsLValue(ov) &&
               "ARC retain pseudo-object results must be scalar");
        result = cgf.emitARCRetainScalarExpr(ov->getSourceExpr());
        opaqueData = OVMA::bind(cgf, ov, RValue::get(result));
      } else {
        opaqueData = OVMA::bind(cgf, ov, ov->getSourceExpr());
      }
      opaques.push_back(opaqueData);
    } else if (semantic == resultExpr) {
      result = cgf.emitARCRetainScalarExpr(semantic);
    } else {
      cgf.emitIgnoredExpr(semantic);
    }
  }

  for (auto &opaque : opaques)
    opaque.unbind(cgf);

  return result;
}

static mlir::Value emitARCRetainCallResult(CIRGenFunction &cgf,
                                           const Expr *expr) {
  mlir::Value value = cgf.emitScalarExpr(expr);
  return emitARCOperationAfterCall(
      cgf, value,
      [](CIRGenFunction &cgf, mlir::Value value, mlir::Location loc) {
        return emitARCRetainAutoreleasedReturnValueMarker(cgf, value, loc);
      },
      [](CIRGenFunction &cgf, mlir::Value value, mlir::Location loc) {
        return cgf.emitARCRetainNonBlock(value, loc);
      });
}

static mlir::Value emitARCUnsafeClaimCallResult(CIRGenFunction &cgf,
                                                const Expr *expr) {
  mlir::Value value = cgf.emitScalarExpr(expr);
  return emitARCOperationAfterCall(
      cgf, value,
      [](CIRGenFunction &cgf, mlir::Value value, mlir::Location loc) {
        return emitARCUnsafeClaimAutoreleasedReturnValueMarker(cgf, value, loc);
      },
      [](CIRGenFunction &, mlir::Value value, mlir::Location) {
        return value;
      });
}

static bool shouldEmitSeparateBlockRetain(const Expr *expr) {
  assert(expr->getType()->isBlockPointerType());
  expr = expr->IgnoreParens();

  if (isa<BlockExpr>(expr))
    return false;

  if (const auto *castExpr = dyn_cast<CastExpr>(expr)) {
    switch (castExpr->getCastKind()) {
    case CK_LValueToRValue:
    case CK_ARCReclaimReturnedObject:
    case CK_ARCConsumeObject:
    case CK_ARCProduceObject:
      return false;
    case CK_NoOp:
    case CK_BitCast:
      return shouldEmitSeparateBlockRetain(castExpr->getSubExpr());
    case CK_AnyPointerToBlockPointerCast:
    default:
      return true;
    }
  }

  return true;
}

} // namespace

void CIRGenFunction::destroyARCStrongImprecise(CIRGenFunction &cgf,
                                                Address addr, QualType type) {
  cgf.emitARCDestroyStrong(addr, cgf.curFn->getLoc(), /*isImprecise=*/true);
}

void CIRGenFunction::destroyARCStrongPrecise(CIRGenFunction &cgf,
                                              Address addr, QualType type) {
  // Precise lifetime: load the value and call objc_release directly.
  // Unlike imprecise (which uses storeStrong), precise release must NOT
  // be moved or merged by the ARC optimizer.
  mlir::Location loc = cgf.curFn->getLoc();
  mlir::Value value = cgf.builder.createLoad(loc, addr);
  cgf.emitARCRelease(value, loc);
}

void CIRGenFunction::destroyARCWeak(CIRGenFunction &cgf, Address addr,
                                    QualType type) {
  cgf.emitARCDestroyWeak(addr);
}

void CIRGenFunction::emitARCDestroyStrong(Address addr, mlir::Location loc,
                                          bool isImprecise) {
  // At -O0, OGCG calls objc_storeStrong(&addr, null). At -O1+, it loads
  // and calls objc_release with the imprecise flag. Match OGCG's behavior
  // (CGObjC.cpp:2510-2520) so that !clang.imprecise_release metadata is
  // emitted at -O1+ for the ARC optimizer.
  if (cgm.getCodeGenOpts().OptimizationLevel == 0) {
    auto ptrTy = builder.getPointerTo(builder.getVoidPtrTy());
    mlir::Value castAddr = builder.createBitcast(addr.getPointer(), ptrTy);
    mlir::Value null = builder.getNullValue(builder.getVoidPtrTy(), loc);
    emitARCStoreStrongCall(Address(castAddr, builder.getVoidPtrTy(),
                                   addr.getAlignment()),
                           null, loc);
    return;
  }

  mlir::Value value = builder.createLoad(loc, addr);
  emitARCRelease(value, loc, isImprecise);
}

mlir::Value CIRGenFunction::emitARCLoadWeak(Address addr) {
  mlir::Location loc = addr.getPointer().getLoc();
  auto ptrTy = builder.getVoidPtrTy();
  auto pptrTy = builder.getPointerTo(ptrTy);
  auto funcType = cir::FuncType::get({pptrTy}, ptrTy);
  cir::FuncOp fn = getARCRuntimeFunction(cgm, "objc_loadWeak", funcType);

  mlir::Value castAddr = builder.createBitcast(addr.getPointer(), pptrTy);
  auto call = builder.createCallOp(loc, fn, {castAddr});
  call->setAttr("nothrow", mlir::UnitAttr::get(builder.getContext()));
  return builder.createBitcast(call.getResult(), addr.getElementType());
}

mlir::Value CIRGenFunction::emitARCLoadWeakRetained(Address addr) {
  mlir::Location loc = addr.getPointer().getLoc();
  auto ptrTy = builder.getVoidPtrTy();
  auto pptrTy = builder.getPointerTo(ptrTy);
  auto funcType = cir::FuncType::get({pptrTy}, ptrTy);
  cir::FuncOp fn =
      getARCRuntimeFunction(cgm, "objc_loadWeakRetained", funcType);

  mlir::Value castAddr = builder.createBitcast(addr.getPointer(), pptrTy);
  auto call = builder.createCallOp(loc, fn, {castAddr});
  call->setAttr("nothrow", mlir::UnitAttr::get(builder.getContext()));
  return builder.createBitcast(call.getResult(), addr.getElementType());
}

mlir::Value CIRGenFunction::emitARCStoreWeak(Address addr, mlir::Value value,
                                             bool ignored) {
  mlir::Location loc = value.getLoc();
  auto ptrTy = builder.getVoidPtrTy();
  auto pptrTy = builder.getPointerTo(ptrTy);
  auto funcType = cir::FuncType::get({pptrTy, ptrTy}, ptrTy);
  cir::FuncOp fn = getARCRuntimeFunction(cgm, "objc_storeWeak", funcType);

  mlir::Value castAddr = builder.createBitcast(addr.getPointer(), pptrTy);
  mlir::Value castVal = builder.createBitcast(value, ptrTy);
  auto call = builder.createCallOp(loc, fn, {castAddr, castVal});
  call->setAttr("nothrow", mlir::UnitAttr::get(builder.getContext()));
  if (ignored)
    return value;
  return builder.createBitcast(call.getResult(), value.getType());
}

void CIRGenFunction::emitARCInitWeak(Address addr, mlir::Value value) {
  mlir::Location loc = value.getLoc();
  if (auto constOp = value.getDefiningOp<cir::ConstantOp>();
      constOp && builder.isNullValue(constOp.getValue()) &&
      cgm.getCodeGenOpts().OptimizationLevel == 0) {
    builder.createStore(loc, value, addr);
    return;
  }

  auto ptrTy = builder.getVoidPtrTy();
  auto pptrTy = builder.getPointerTo(ptrTy);
  auto funcType = cir::FuncType::get({pptrTy, ptrTy}, ptrTy);
  cir::FuncOp fn = getARCRuntimeFunction(cgm, "objc_initWeak", funcType);

  mlir::Value castAddr = builder.createBitcast(addr.getPointer(), pptrTy);
  mlir::Value castVal = builder.createBitcast(value, ptrTy);
  auto call = builder.createCallOp(loc, fn, {castAddr, castVal});
  call->setAttr("nothrow", mlir::UnitAttr::get(builder.getContext()));
}

void CIRGenFunction::emitARCDestroyWeak(Address addr) {
  mlir::Location loc = addr.getPointer().getLoc();
  auto voidTy = cir::VoidType::get(builder.getContext());
  auto ptrTy = builder.getVoidPtrTy();
  auto pptrTy = builder.getPointerTo(ptrTy);
  auto funcType = cir::FuncType::get({pptrTy}, voidTy);
  cir::FuncOp fn = getARCRuntimeFunction(cgm, "objc_destroyWeak", funcType);

  mlir::Value castAddr = builder.createBitcast(addr.getPointer(), pptrTy);
  auto call = builder.createCallOp(loc, fn, {castAddr});
  call->setAttr("nothrow", mlir::UnitAttr::get(builder.getContext()));
}

void CIRGenFunction::emitARCMoveWeak(Address dst, Address src) {
  mlir::Location loc = dst.getPointer().getLoc();
  auto voidTy = cir::VoidType::get(builder.getContext());
  auto ptrTy = builder.getVoidPtrTy();
  auto pptrTy = builder.getPointerTo(ptrTy);
  auto funcType = cir::FuncType::get({pptrTy, pptrTy}, voidTy);
  cir::FuncOp fn = getARCRuntimeFunction(cgm, "objc_moveWeak", funcType);

  mlir::Value castDst = builder.createBitcast(dst.getPointer(), pptrTy);
  mlir::Value castSrc = builder.createBitcast(src.getPointer(), pptrTy);
  auto call = builder.createCallOp(loc, fn, {castDst, castSrc});
  call->setAttr("nothrow", mlir::UnitAttr::get(builder.getContext()));
}

void CIRGenFunction::emitARCCopyWeak(Address dst, Address src) {
  mlir::Location loc = dst.getPointer().getLoc();
  auto voidTy = cir::VoidType::get(builder.getContext());
  auto ptrTy = builder.getVoidPtrTy();
  auto pptrTy = builder.getPointerTo(ptrTy);
  auto funcType = cir::FuncType::get({pptrTy, pptrTy}, voidTy);
  cir::FuncOp fn = getARCRuntimeFunction(cgm, "objc_copyWeak", funcType);

  mlir::Value castDst = builder.createBitcast(dst.getPointer(), pptrTy);
  mlir::Value castSrc = builder.createBitcast(src.getPointer(), pptrTy);
  auto call = builder.createCallOp(loc, fn, {castDst, castSrc});
  call->setAttr("nothrow", mlir::UnitAttr::get(builder.getContext()));
}

void CIRGenFunction::emitARCCopyAssignWeak(Address dst, Address src) {
  mlir::Value object = emitARCLoadWeakRetained(src);
  emitARCStoreWeak(dst, object, /*ignored=*/false);
  emitARCRelease(object, object.getLoc(), /*isImprecise=*/true);
}

void CIRGenFunction::emitARCMoveAssignWeak(Address dst, Address src) {
  mlir::Value object = emitARCLoadWeakRetained(src);
  emitARCStoreWeak(dst, object, /*ignored=*/false);
  emitARCDestroyWeak(src);
  emitARCRelease(object, object.getLoc(), /*isImprecise=*/true);
}

mlir::Value CIRGenFunction::emitObjCAutoreleasePoolPush(mlir::Location loc) {
  if (cgm.getLangOpts().ObjCRuntime.hasNativeARC()) {
    return cir::LLVMIntrinsicCallOp::create(
               builder, loc,
               cgm.getStringAttrCached("objc.autoreleasePoolPush"),
               builder.getVoidPtrTy())
        .getResult();
  }

  mlir::Value poolClass =
      getObjCClassByRuntimeName(*this, loc, "NSAutoreleasePool");

  const IdentifierInfo *allocII = &getContext().Idents.get("alloc");
  Selector allocSel = getContext().Selectors.getSelector(0, &allocII);
  mlir::Value allocated =
      emitObjCMsgSend(*this, loc, getContext().getObjCIdType(), poolClass, allocSel)
          .getValue();

  const IdentifierInfo *initII = &getContext().Idents.get("init");
  Selector initSel = getContext().Selectors.getSelector(0, &initII);
  return emitObjCMsgSend(*this, loc, getContext().getObjCIdType(), allocated,
                         initSel)
      .getValue();
}

void CIRGenFunction::emitObjCAutoreleasePoolPop(mlir::Value token,
                                                mlir::Location loc) {
  assert(token && "expected autorelease-pool token");

  if (!cgm.getLangOpts().ObjCRuntime.hasNativeARC()) {
    const IdentifierInfo *drainII = &getContext().Idents.get("drain");
    Selector drainSel = getContext().Selectors.getSelector(0, &drainII);
    (void)emitObjCMsgSend(*this, loc, getContext().VoidTy, token, drainSel);
    return;
  }

  if (!isCatchOrCleanupRequired()) {
    cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("objc.autoreleasePoolPop"),
        mlir::Type{}, mlir::ValueRange{token});
    return;
  }

  auto popFunc = getObjCAutoreleasePoolPopRuntimeFunction(cgm);
  QualType popFnTy = getContext().getFunctionType(
      getContext().VoidTy, {getContext().VoidPtrTy},
      FunctionProtoType::ExtProtoInfo());
  auto popProto = CanQual<FunctionProtoType>::CreateUnsafe(
      getContext().getCanonicalType(popFnTy));
  const auto &funcInfo = getTypes().arrangeFreeFunctionType(popProto);

  CallArgList args;
  args.add(RValue::get(token), getContext().VoidPtrTy);
  (void)emitCall(funcInfo, CIRGenCallee::forDirect(popFunc), ReturnValueSlot(),
                 args, /*callOp=*/nullptr, loc);
}

void CIRGenFunction::emitObjCAutoreleasePoolCleanup(mlir::Value token) {
  struct CallObjCAutoreleasePoolPop final : EHScopeStack::Cleanup {
    mlir::Value token;

    explicit CallObjCAutoreleasePoolPop(mlir::Value token) : token(token) {}

    void emit(CIRGenFunction &cgf, Flags flags) override {
      cgf.emitObjCAutoreleasePoolPop(token, cgf.curFn->getLoc());
    }
  };

  ehStack.pushCleanup<CallObjCAutoreleasePoolPop>(NormalCleanup, token);
}

void CIRGenFunction::emitARCStoreStrongCall(Address addr, mlir::Value value,
                                            mlir::Location loc) {
  // Emit: void objc_storeStrong(id *, id)
  auto voidTy = cir::VoidType::get(builder.getContext());
  auto ptrTy = builder.getVoidPtrTy();
  auto pptrTy = builder.getPointerTo(ptrTy);
  auto funcType = cir::FuncType::get({pptrTy, ptrTy}, voidTy);
  cir::FuncOp fn =
      getARCRuntimeFunction(cgm, "objc_storeStrong", funcType);

  mlir::Value castAddr = builder.createBitcast(addr.getPointer(), pptrTy);
  mlir::Value castVal = builder.createBitcast(value, ptrTy);
  auto call = builder.createCallOp(loc, fn, {castAddr, castVal});
  call->setAttr("nothrow", mlir::UnitAttr::get(builder.getContext()));
}

mlir::Value CIRGenFunction::emitARCRetainNonBlock(mlir::Value value,
                                                   mlir::Location loc) {
  // Short-circuit on null constants.
  if (auto constOp = value.getDefiningOp<cir::ConstantOp>()) {
    if (builder.isNullValue(constOp.getValue()))
      return value;
  }

  // Emit: id objc_retain(id)
  auto ptrTy = builder.getVoidPtrTy();
  auto funcType = cir::FuncType::get({ptrTy}, ptrTy);
  cir::FuncOp fn = getARCRuntimeFunction(cgm, "objc_retain", funcType);

  mlir::Value castVal = builder.createBitcast(value, ptrTy);
  auto callOp = builder.createCallOp(loc, fn, {castVal});
  callOp->setAttr("nothrow", mlir::UnitAttr::get(builder.getContext()));
  mlir::Value result = callOp.getResult();
  return builder.createBitcast(result, value.getType());
}

mlir::Value CIRGenFunction::emitARCAutorelease(mlir::Value value,
                                               mlir::Location loc) {
  if (auto constOp = value.getDefiningOp<cir::ConstantOp>()) {
    if (builder.isNullValue(constOp.getValue()))
      return value;
  }

  auto ptrTy = builder.getVoidPtrTy();
  auto funcType = cir::FuncType::get({ptrTy}, ptrTy);
  cir::FuncOp fn = getARCRuntimeFunction(cgm, "objc_autorelease", funcType);

  mlir::Value castVal = builder.createBitcast(value, ptrTy);
  auto callOp = builder.createCallOp(loc, fn, {castVal});
  callOp->setAttr("nothrow", mlir::UnitAttr::get(builder.getContext()));
  mlir::Value result = callOp.getResult();
  return builder.createBitcast(result, value.getType());
}

mlir::Value CIRGenFunction::emitARCAutoreleaseReturnValue(mlir::Value value,
                                                          mlir::Location loc) {
  if (auto constOp = value.getDefiningOp<cir::ConstantOp>()) {
    if (builder.isNullValue(constOp.getValue()))
      return value;
  }

  auto ptrTy = builder.getVoidPtrTy();
  auto funcType = cir::FuncType::get({ptrTy}, ptrTy);
  cir::FuncOp fn =
      getARCRuntimeFunction(cgm, "objc_autoreleaseReturnValue", funcType);

  mlir::Value castVal = builder.createBitcast(value, ptrTy);
  auto callOp = builder.createCallOp(loc, fn, {castVal});
  callOp->setAttr("nothrow", mlir::UnitAttr::get(builder.getContext()));
  mlir::Value result = callOp.getResult();
  return builder.createBitcast(result, value.getType());
}

mlir::Value CIRGenFunction::emitARCRetainAutorelease(mlir::Value value,
                                                     mlir::Location loc) {
  if (auto constOp = value.getDefiningOp<cir::ConstantOp>()) {
    if (builder.isNullValue(constOp.getValue()))
      return value;
  }

  auto ptrTy = builder.getVoidPtrTy();
  auto funcType = cir::FuncType::get({ptrTy}, ptrTy);
  cir::FuncOp fn = getARCRuntimeFunction(cgm, "objc_retainAutorelease",
                                         funcType);

  mlir::Value castVal = builder.createBitcast(value, ptrTy);
  auto callOp = builder.createCallOp(loc, fn, {castVal});
  callOp->setAttr("nothrow", mlir::UnitAttr::get(builder.getContext()));
  mlir::Value result = callOp.getResult();
  return builder.createBitcast(result, value.getType());
}

mlir::Value CIRGenFunction::emitARCRetainBlock(mlir::Value value,
                                               mlir::Location loc,
                                               bool mandatory) {
  if (auto constOp = value.getDefiningOp<cir::ConstantOp>()) {
    if (builder.isNullValue(constOp.getValue()))
      return value;
  }

  auto ptrTy = builder.getVoidPtrTy();
  auto funcType = cir::FuncType::get({ptrTy}, ptrTy);
  cir::FuncOp fn = getARCRuntimeFunction(cgm, "objc_retainBlock", funcType);

  mlir::Value castVal = builder.createBitcast(value, ptrTy);
  auto callOp = builder.createCallOp(loc, fn, {castVal});
  callOp->setAttr("nothrow", mlir::UnitAttr::get(builder.getContext()));
  if (!mandatory)
    callOp->setAttr("cir.arc.copy_on_escape",
                    mlir::UnitAttr::get(builder.getContext()));
  mlir::Value result = callOp.getResult();
  return builder.createBitcast(result, value.getType());
}

mlir::Value CIRGenFunction::emitARCRetainScalarExpr(const Expr *expr) {
  if (const auto *cleanups = dyn_cast<ExprWithCleanups>(expr)) {
    RunCleanupsScope scope(*this);
    return emitARCRetainScalarExpr(cleanups->getSubExpr());
  }

  expr = expr->IgnoreParens();
  if (const auto *pseudo = dyn_cast<PseudoObjectExpr>(expr))
    return emitARCRetainPseudoObjectExpr(*this, pseudo);

  if (const auto *ce = dyn_cast<CastExpr>(expr)) {
    switch (ce->getCastKind()) {
    case CK_NoOp:
    case CK_CPointerToObjCPointerCast:
    case CK_BlockPointerToObjCPointerCast:
    case CK_AnyPointerToBlockPointerCast:
    case CK_BitCast:
      return emitARCRetainScalarExpr(ce->getSubExpr());
    case CK_ARCConsumeObject:
      return emitScalarExpr(ce->getSubExpr());
    case CK_ARCExtendBlockObject:
      return emitARCExtendBlockObject(ce->getSubExpr(),
                                      /*consumed=*/false);
    case CK_ARCReclaimReturnedObject:
      return emitARCRetainCallResult(*this, ce->getSubExpr());
    case CK_LValueToRValue: {
      // Loading retained from a non-const __strong xvalue is a move: steal the
      // pointer and null the source instead of retaining, matching classic
      // tryEmitARCRetainLoadOfScalar. This is what a synthesized move
      // ctor/assignment does for a __strong member. Any other load falls
      // through to the load-and-retain (copy) path below.
      const Expr *subExpr = ce->getSubExpr()->IgnoreParens();
      QualType subType = subExpr->getType();
      if (subExpr->isXValue() && !subType.isConstQualified() &&
          subType.getObjCLifetime() == Qualifiers::OCL_Strong) {
        LValue lv = emitLValue(subExpr);
        mlir::Location loc = getLoc(subExpr->getExprLoc());
        mlir::Value result =
            emitLoadOfLValue(lv, subExpr->getExprLoc()).getValue();
        mlir::Value zero = builder.getNullValue(convertType(subType), loc);
        emitStoreThroughLValue(RValue::get(zero), lv);
        return result;
      }
      break;
    }
    default:
      break;
    }
  }

  if (isa<CallExpr>(expr) ||
      (isa<ObjCMessageExpr>(expr) &&
       !cast<ObjCMessageExpr>(expr)->isDelegateInitCall()))
    return emitARCRetainCallResult(*this, expr);

  mlir::Location loc = getLoc(expr->getExprLoc());
  mlir::Value value = emitScalarExpr(expr);
  if (expr->getType()->isBlockPointerType())
    return emitARCRetainBlock(value, loc, /*mandatory=*/false);
  return emitARCRetainNonBlock(value, loc);
}

mlir::Value CIRGenFunction::emitARCRetainAutoreleaseScalarExpr(
    const Expr *expr) {
  if (const auto *cleanups = dyn_cast<ExprWithCleanups>(expr)) {
    RunCleanupsScope scope(*this);
    return emitARCRetainAutoreleaseScalarExpr(cleanups->getSubExpr());
  }

  expr = expr->IgnoreParens();
  if (const auto *pseudo = dyn_cast<PseudoObjectExpr>(expr))
    return emitARCAutorelease(emitARCRetainPseudoObjectExpr(*this, pseudo),
                              getLoc(expr->getExprLoc()));

  if (const auto *ce = dyn_cast<CastExpr>(expr)) {
    switch (ce->getCastKind()) {
    case CK_NoOp:
    case CK_CPointerToObjCPointerCast:
    case CK_BlockPointerToObjCPointerCast:
    case CK_AnyPointerToBlockPointerCast:
    case CK_BitCast:
      return emitARCRetainAutoreleaseScalarExpr(ce->getSubExpr());
    case CK_ARCConsumeObject:
      return emitARCAutorelease(emitScalarExpr(ce->getSubExpr()),
                                getLoc(expr->getExprLoc()));
    case CK_ARCExtendBlockObject:
      return emitARCAutorelease(
          emitARCExtendBlockObject(ce->getSubExpr(), /*consumed=*/false),
          getLoc(expr->getExprLoc()));
    case CK_ARCReclaimReturnedObject:
      return emitARCAutorelease(
          emitARCRetainCallResult(*this, ce->getSubExpr()),
          getLoc(expr->getExprLoc()));
    default:
      break;
    }
  }

  mlir::Location loc = getLoc(expr->getExprLoc());
  if (isa<CallExpr>(expr) ||
      (isa<ObjCMessageExpr>(expr) &&
       !cast<ObjCMessageExpr>(expr)->isDelegateInitCall())) {
    mlir::Value value = emitARCRetainCallResult(*this, expr);
    return emitARCAutorelease(value, loc);
  }

  mlir::Value value = emitScalarExpr(expr);
  if (expr->getType()->isBlockPointerType()) {
    value = emitARCRetainBlock(value, loc, /*mandatory=*/true);
    return emitARCAutorelease(value, loc);
  }
  return emitARCRetainAutorelease(value, loc);
}

mlir::Value CIRGenFunction::emitObjCThrowOperand(const Expr *expr) {
  if (getLangOpts().ObjCAutoRefCount)
    return emitARCRetainAutoreleaseScalarExpr(expr);
  return emitScalarExpr(expr);
}

mlir::Value CIRGenFunction::emitARCReclaimReturnedObject(const Expr *expr) {
  mlir::Value value = emitARCRetainCallResult(*this, expr);
  emitARCConsumeObject(value, getLoc(expr->getExprLoc()));
  return value;
}

mlir::Value CIRGenFunction::emitARCUnsafeClaimReturnedObject(const Expr *expr) {
  return emitARCUnsafeClaimCallResult(*this, expr);
}

mlir::Value CIRGenFunction::emitARCExtendBlockObject(const Expr *expr,
                                                     bool consumed) {
  assert(expr->getType()->isBlockPointerType() &&
         "ARC block extension requires block-pointer input");

  mlir::Value value = emitScalarExpr(expr);
  bool doRetain = true;

  if (!shouldEmitSeparateBlockRetain(expr))
    if (const auto *blockExpr = dyn_cast<BlockExpr>(expr->IgnoreParens()))
      if (cgm.getCodeGenOpts().ObjCAvoidHeapifyLocalBlocks &&
          blockExpr->getBlockDecl()->canAvoidCopyToHeap())
        doRetain = false;

  if (doRetain)
    value = emitARCRetainBlock(value, getLoc(expr->getExprLoc()),
                               /*mandatory=*/true);
  if (consumed)
    emitARCConsumeObject(value, getLoc(expr->getExprLoc()));
  return value;
}

mlir::Value CIRGenFunction::emitBlockCopyAndAutorelease(mlir::Value block,
                                                        QualType ty,
                                                        mlir::Location loc) {
  const IdentifierInfo *copyID = &getContext().Idents.get("copy");
  Selector copySelector = getContext().Selectors.getNullarySelector(copyID);
  const IdentifierInfo *autoreleaseID = &getContext().Idents.get("autorelease");
  Selector autoreleaseSelector =
      getContext().Selectors.getNullarySelector(autoreleaseID);

  mlir::Value value =
      emitObjCMsgSend(*this, loc, ty, block, copySelector).getValue();
  return emitObjCMsgSend(*this, loc, ty, value, autoreleaseSelector)
      .getValue();
}

void CIRGenFunction::emitARCRelease(mlir::Value value, mlir::Location loc,
                                    bool isImprecise) {
  // Short-circuit on null constants.
  if (auto constOp = value.getDefiningOp<cir::ConstantOp>()) {
    if (builder.isNullValue(constOp.getValue()))
      return;
  }

  // Emit: void objc_release(id)
  auto voidTy = cir::VoidType::get(builder.getContext());
  auto ptrTy = builder.getVoidPtrTy();
  auto funcType = cir::FuncType::get({ptrTy}, voidTy);
  cir::FuncOp fn = getARCRuntimeFunction(cgm, "objc_release", funcType);

  mlir::Value castVal = builder.createBitcast(value, ptrTy);
  auto call = builder.createCallOp(loc, fn, {castVal});
  call->setAttr("nothrow", mlir::UnitAttr::get(builder.getContext()));
  if (isImprecise)
    call->setAttr("cir.imprecise_release",
                  mlir::UnitAttr::get(builder.getContext()));
}

void CIRGenFunction::emitARCConsumeObject(mlir::Value value,
                                           mlir::Location loc) {
  // Push a cleanup to call objc_release at end of full-expression.
  // This matches OGCG's EmitObjCConsumeObject (CGObjC.cpp:2150-2158).
  struct CallObjCRelease final : EHScopeStack::Cleanup {
    mlir::Value object;
    CallObjCRelease(mlir::Value obj) : object(obj) {}
    void emit(CIRGenFunction &cgf, Flags flags) override {
      cgf.emitARCRelease(object, cgf.curFn->getLoc(), /*isImprecise=*/true);
    }
  };

  struct CallObjCReleaseFromAddr final : EHScopeStack::Cleanup {
    Address objectAddr;
    CallObjCReleaseFromAddr(Address addr) : objectAddr(addr) {}
    void emit(CIRGenFunction &cgf, Flags flags) override {
      mlir::Value object = cgf.builder.createLoad(cgf.curFn->getLoc(),
                                                  objectAddr);
      cgf.emitARCRelease(object, cgf.curFn->getLoc(), /*isImprecise=*/true);
    }
  };

  if (!isInConditionalBranch())
    return pushFullExprCleanup<CallObjCRelease>(
        getCleanupKind(QualType::DK_objc_strong_lifetime), value);

  // Cleanups for conditionally-evaluated expressions can run after the branch
  // region has ended. Spill the consumed value to dominating storage so the
  // cleanup doesn't capture a branch-local SSA operand.
  Address objectAddr = createTempAlloca(
      value.getType(), getPointerAlign(), loc, "arc.consume",
      /*arraySize=*/nullptr, /*alloca=*/nullptr,
      builder.getBestAllocaInsertPoint(getCurFunctionEntryBlock()));
  builder.createStore(loc, value, objectAddr);
  pushFullExprCleanup<CallObjCReleaseFromAddr>(
      getCleanupKind(QualType::DK_objc_strong_lifetime), objectAddr);
}
