//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Emit Expr nodes with scalar CIR types as CIR code.
//
//===----------------------------------------------------------------------===//

#include "CIRGenConstantEmitter.h"
#include "CIRGenCXXABI.h"
#include "CIRGenFunction.h"
#include "CIRGenRecordLayout.h"
#include "CIRGenSanitizer.h"
#include "CIRGenValue.h"
#include "../Dialect/Transforms/TargetLowering/LowerModule.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"

#include "llvm/ADT/APFixedPoint.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/Value.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

using namespace clang;
using namespace clang::CIRGen;

namespace {

static mlir::Value emitARCUnsafeUnretainedScalarExpr(CIRGenFunction &cgf,
                                                     const Expr *expr);

static void emitARCIntrinsicUse(CIRGenFunction &cgf, mlir::ValueRange values,
                                mlir::Location loc) {
  if (values.empty())
    return;

  auto voidTy = cir::VoidType::get(cgf.getBuilder().getContext());
  auto funcTy = cir::FuncType::get({}, voidTy, /*isVarArg=*/true);
  cir::FuncOp useFn;
  if (auto *existing = cgf.cgm.getGlobalValue("llvm.objc.clang.arc.use"))
    useFn = mlir::cast<cir::FuncOp>(existing);
  else
    useFn = cgf.cgm.createCIRFunction(
        mlir::UnknownLoc::get(&cgf.cgm.getMLIRContext()),
        "llvm.objc.clang.arc.use", funcTy, nullptr);
  auto use = cgf.getBuilder().createCallOp(loc, useFn, values);
  use->setAttr("nothrow", mlir::UnitAttr::get(cgf.getBuilder().getContext()));
}

static mlir::Value emitObjCCollectionLiteral(CIRGenFunction &cgf, const Expr *e,
                                             const ObjCMethodDecl *method) {
  ASTContext &ctx = cgf.getContext();
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(e->getSourceRange());
  const auto *arrayLiteral = dyn_cast<ObjCArrayLiteral>(e);
  const auto *dictLiteral =
      arrayLiteral ? nullptr : cast<ObjCDictionaryLiteral>(e);

  if (!method || !method->isClassMethod()) {
    cgf.cgm.errorNYI(e->getSourceRange(),
                     "ScalarExprEmitter: objc collection without class method");
    return {};
  }

  const uint64_t numElements =
      arrayLiteral ? arrayLiteral->getNumElements() : dictLiteral->getNumElements();
  if (numElements == 0 &&
      cgf.cgm.getLangOpts().ObjCRuntime.hasEmptyCollections()) {
    StringRef constantName =
        arrayLiteral ? "__NSArray0__" : "__NSDictionary0__";
    QualType objCIdTy = ctx.getObjCIdType();
    cir::GlobalOp constant =
        cgf.cgm.createOrReplaceCXXRuntimeVariable(
            loc, constantName, cgf.convertType(objCIdTy),
            cir::GlobalLinkageKind::ExternalLinkage, CharUnits::Zero());
    constant.setConstant(false);

    mlir::Value constantAddr = builder.createGetGlobal(loc, constant);
    cir::LoadOp load = builder.createLoad(
        loc, Address(constantAddr, constant.getSymType(), cgf.getPointerAlign()));
    load->setAttr("cir.invariant_load",
                  mlir::UnitAttr::get(builder.getContext()));

    mlir::Value scalar = load.getResult();
    mlir::Type exprTy = cgf.convertType(e->getType());
    if (scalar.getType() != exprTy)
      scalar = builder.createBitcast(loc, scalar, exprTy);
    return scalar;
  }

  QualType elementType = ctx.getObjCIdType().withConst();
  llvm::APInt apNumElements(ctx.getTypeSize(ctx.getSizeType()), numElements);
  QualType elementArrayType = ctx.getConstantArrayType(
      elementType, apNumElements, nullptr, ArraySizeModifier::Normal,
      /*IndexTypeQuals=*/0);
  Address objects = cgf.createMemTemp(elementArrayType, loc, "objects");
  Address keys = Address::invalid();
  if (dictLiteral)
    keys = cgf.createMemTemp(elementArrayType, loc, "keys");

  mlir::Type elementTy = cgf.convertTypeForMem(elementType);
  CharUnits elementSize = ctx.getTypeSizeInChars(elementType);
  llvm::SmallVector<mlir::Value, 16> neededObjects;
  const bool trackNeededObjects =
      ctx.getLangOpts().ObjCAutoRefCount &&
      cgf.cgm.getCodeGenOpts().OptimizationLevel != 0;

  auto getElementAddress = [&](Address array, uint64_t index) {
    mlir::Value idx = builder.getConstInt(
        loc, mlir::cast<cir::IntType>(cgf.ptrDiffTy), index);
    mlir::Value ptr = builder.getArrayElement(loc, loc, array.getPointer(),
                                              array.getElementType(), idx,
                                              /*shouldDecay=*/true);
    return Address(ptr, elementTy,
                   array.getAlignment().alignmentAtOffset(elementSize * index));
  };

  for (uint64_t i = 0; i < numElements; ++i) {
    if (arrayLiteral) {
      const Expr *rhs = arrayLiteral->getElement(i);
      mlir::Value value = cgf.emitScalarExpr(rhs);
      cgf.emitStoreThroughLValue(
          RValue::get(value),
          cgf.makeAddrLValue(getElementAddress(objects, i), elementType),
          /*isInit=*/true);
      if (trackNeededObjects)
        neededObjects.push_back(value);
      continue;
    }

    const Expr *keyExpr = dictLiteral->getKeyValueElement(i).Key;
    mlir::Value keyValue = cgf.emitScalarExpr(keyExpr);
    cgf.emitStoreThroughLValue(
        RValue::get(keyValue),
        cgf.makeAddrLValue(getElementAddress(keys, i), elementType),
        /*isInit=*/true);

    const Expr *valueExpr = dictLiteral->getKeyValueElement(i).Value;
    mlir::Value valueValue = cgf.emitScalarExpr(valueExpr);
    cgf.emitStoreThroughLValue(
        RValue::get(valueValue),
        cgf.makeAddrLValue(getElementAddress(objects, i), elementType),
        /*isInit=*/true);

    if (trackNeededObjects) {
      neededObjects.push_back(keyValue);
      neededObjects.push_back(valueValue);
    }
  }

  CallArgList args;
  auto paramIt = method->param_begin();
  const ParmVarDecl *argDecl = *paramIt++;
  QualType argTy = argDecl->getType().getUnqualifiedType();
  args.add(RValue::get(
               builder.maybeBuildArrayDecay(loc, objects.getPointer(),
                                            objects.getElementType())),
           argTy);
  if (dictLiteral) {
    argDecl = *paramIt++;
    argTy = argDecl->getType().getUnqualifiedType();
    args.add(RValue::get(
                 builder.maybeBuildArrayDecay(loc, keys.getPointer(),
                                              keys.getElementType())),
             argTy);
  }
  argDecl = *paramIt;
  argTy = argDecl->getType().getUnqualifiedType();
  mlir::Value count = builder.getConstInt(loc, cgf.convertType(argTy), numElements);
  args.add(RValue::get(count), argTy);

  QualType resultType = e->getType();
  const auto *interfacePointerType = resultType->getAsObjCInterfacePointerType();
  if (!interfacePointerType) {
    cgf.cgm.errorNYI(e->getSourceRange(),
                     "ScalarExprEmitter: objc collection without interface");
    return {};
  }

  const ObjCInterfaceDecl *classDecl =
      interfacePointerType->getObjectType()->getInterface();
  if (!classDecl) {
    cgf.cgm.errorNYI(e->getSourceRange(),
                     "ScalarExprEmitter: objc collection without class");
    return {};
  }

  QualType objCIdTy = ctx.getObjCIdType();
  QualType objCSelTy = ctx.getObjCSelType();
  CallArgList actualArgs;
  actualArgs.add(RValue::get(cgf.cgm.getObjCClass(cgf, classDecl, loc)), objCIdTy);
  actualArgs.add(RValue::get(cgf.cgm.getObjCSelector(cgf, method->getSelector(),
                                                     loc)),
                 objCSelTy);
  actualArgs.addFrom(args);

  llvm::SmallVector<QualType, 4> argTypes;
  argTypes.push_back(objCIdTy);
  argTypes.push_back(objCSelTy);
  for (const CallArg &arg : llvm::drop_begin(actualArgs, 2))
    argTypes.push_back(arg.ty);

  QualType msgFnTy = ctx.getFunctionType(method->getReturnType(), argTypes,
                                         FunctionProtoType::ExtProtoInfo());
  auto msgProto =
      CanQual<FunctionProtoType>::CreateUnsafe(ctx.getCanonicalType(msgFnTy));
  const CIRGenFunctionInfo &funcInfo =
      cgf.getTypes().arrangeFreeFunctionType(msgProto);
  cir::FuncType exactFnTy = cgf.getTypes().getFunctionType(funcInfo);
  cir::FuncOp msgSendFn = cgf.cgm.getObjCMsgSendFunc();
  mlir::Value msgSendAddr = cir::GetGlobalOp::create(
      builder, loc, builder.getPointerTo(msgSendFn.getFunctionType()),
      msgSendFn.getSymNameAttr());
  mlir::Value msgSendPtr =
      builder.createBitcast(loc, msgSendAddr, builder.getPointerTo(exactFnTy));

  RValue result = cgf.emitCall(
      funcInfo, CIRGenCallee::forDirect(msgSendPtr.getDefiningOp()),
      ReturnValueSlot(), actualArgs, /*callOrTryCall=*/nullptr, loc);

  if (trackNeededObjects)
    emitARCIntrinsicUse(cgf, neededObjects, loc);

  mlir::Value scalar = result.getValue();
  mlir::Type exprTy = cgf.convertType(e->getType());
  if (scalar.getType() != exprTy)
    scalar = builder.createBitcast(loc, scalar, exprTy);
  return scalar;
}

static bool shouldExtendReceiverForInnerPointerMessage(
    const ObjCMessageExpr *message) {
  switch (message->getReceiverKind()) {
  case ObjCMessageExpr::Instance: {
    const Expr *receiver = message->getInstanceReceiver();

    if (auto opaque = dyn_cast<OpaqueValueExpr>(receiver)) {
      if (opaque->getSourceExpr())
        receiver = opaque->getSourceExpr()->IgnoreParens();
    }

    const auto *ice = dyn_cast<ImplicitCastExpr>(receiver);
    if (!ice || ice->getCastKind() != CK_LValueToRValue)
      return true;
    receiver = ice->getSubExpr()->IgnoreParens();

    if (auto opaque = dyn_cast<OpaqueValueExpr>(receiver)) {
      if (opaque->getSourceExpr())
        receiver = opaque->getSourceExpr()->IgnoreParens();
    }

    if (receiver->getType().getObjCLifetime() != Qualifiers::OCL_Strong)
      return true;

    if (isa<MemberExpr>(receiver) || isa<ObjCIvarRefExpr>(receiver))
      return false;

    const auto *declRef = dyn_cast<DeclRefExpr>(ice->getSubExpr());
    if (!declRef)
      return true;
    const auto *var = dyn_cast<VarDecl>(declRef->getDecl());
    if (!var)
      return true;

    return var->hasLocalStorage() && !var->hasAttr<ObjCPreciseLifetimeAttr>();
  }

  case ObjCMessageExpr::Class:
  case ObjCMessageExpr::SuperClass:
  case ObjCMessageExpr::SuperInstance:
    return false;
  }

  llvm_unreachable("invalid receiver kind");
}

/// Given an expression of ObjC pointer type, check whether it was immediately
/// loaded from a __weak l-value.
static const Expr *findWeakLValue(const Expr *e) {
  assert(e->getType()->isObjCRetainableType());
  e = e->IgnoreParens();
  if (const auto *ce = dyn_cast<CastExpr>(e)) {
    if (ce->getCastKind() == CK_LValueToRValue) {
      if (ce->getSubExpr()->getType().getObjCLifetime() == Qualifiers::OCL_Weak)
        return ce->getSubExpr();
    }
  }

  return nullptr;
}

static mlir::Value stripObjCPointerBitCasts(mlir::Value value) {
  while (auto castOp = value.getDefiningOp<cir::CastOp>()) {
    if (castOp.getKind() != cir::CastKind::bitcast)
      break;
    value = castOp.getSrc();
  }
  return value;
}

static mlir::Value getAddrOfObjCRuntimeNameString(CIRGenFunction &cgf,
                                                  mlir::Location loc,
                                                  llvm::StringRef runtimeName) {
  CIRGenModule &cgm = cgf.cgm;
  CIRGenBuilderTy &builder = cgm.getBuilder();

  auto stringAttr =
      builder.getString(runtimeName, builder.getUInt8Ty(), runtimeName.size() + 1);
  auto arrayTy = mlir::cast<cir::ArrayType>(
      mlir::cast<mlir::TypedAttr>(stringAttr).getType());
  cir::GlobalOp stringGV = CIRGenModule::createGlobalOp(
      cgm, loc, cgm.getUniqueGlobalName(".objc.class_name"), arrayTy,
      /*isConstant=*/true);
  stringGV.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
  mlir::SymbolTable::setSymbolVisibility(
      stringGV, mlir::SymbolTable::Visibility::Private);
  stringGV.setAlignmentAttr(
      cgm.getSize(cgf.getContext().getTypeAlignInChars(cgf.getContext().CharTy)));
  stringGV->setAttr("unnamed_addr", mlir::UnitAttr::get(builder.getContext()));
  CIRGenModule::setInitializer(stringGV, stringAttr);
  cgm.setDSOLocal(static_cast<mlir::Operation *>(stringGV));

  auto view = builder.getGlobalViewAttr(
      builder.getPointerTo(arrayTy.getElementType(), stringGV.getAddrSpaceAttr()),
      stringGV);
  return builder.getConstant(loc, view);
}

static mlir::Value lookupObjCGNUClassByName(CIRGenFunction &cgf,
                                            mlir::Location loc,
                                            llvm::StringRef runtimeName) {
  CIRGenModule &cgm = cgf.cgm;
  CIRGenBuilderTy &builder = cgm.getBuilder();

  mlir::Value className =
      getAddrOfObjCRuntimeNameString(cgf, loc, runtimeName);
  cir::FuncType fnTy = cir::FuncType::get({className.getType()}, cgm.voidPtrTy);
  cir::FuncOp lookupFn = cgm.createRuntimeFunction(fnTy, "objc_lookup_class");
  return builder.createCallOp(loc, lookupFn, {className}).getResult();
}

static bool isWeakLinkedObjCClass(const ObjCInterfaceDecl *id) {
  for (const ObjCInterfaceDecl *curr = id; curr; curr = curr->getSuperClass())
    if (curr->isWeakImported())
      return true;

  return false;
}

static mlir::Value getObjCGNUClass(CIRGenFunction &cgf, mlir::Location loc,
                                   const ObjCInterfaceDecl *id) {
  assert(id && "expected Objective-C interface");

  CIRGenModule &cgm = cgf.cgm;
  CIRGenBuilderTy &builder = cgm.getBuilder();
  const ObjCRuntime runtime = cgf.getLangOpts().ObjCRuntime;
  if (runtime.getKind() == ObjCRuntime::GNUstep &&
      runtime.getVersion() >= VersionTuple(2) &&
      !isWeakLinkedObjCClass(id))
    return cgm.getObjCClass(cgf, id, loc);

  if (runtime.getKind() == ObjCRuntime::ObjFW && !isWeakLinkedObjCClass(id)) {
    cir::GlobalOp classSymbol = cgm.getObjCClassSymbol(loc, id);
    mlir::Value classAddr = builder.createGetGlobal(loc, classSymbol);
    if (classAddr.getType() != cgm.voidPtrTy)
      classAddr = builder.createBitcast(loc, classAddr, cgm.voidPtrTy);
    return classAddr;
  }

  return lookupObjCGNUClassByName(cgf, loc, id->getObjCRuntimeNameAsString());
}

static bool objCMessageResultUsesFPRet(CIRGenFunction &cgf,
                                       QualType resultType) {
  const auto *builtinType = resultType->getAs<BuiltinType>();
  if (!builtinType)
    return false;

  switch (builtinType->getKind()) {
  default:
    return false;
  case BuiltinType::Float:
    return cgf.getTarget().useObjCFPRetForRealType(FloatModeKind::Float);
  case BuiltinType::Double:
    return cgf.getTarget().useObjCFPRetForRealType(FloatModeKind::Double);
  case BuiltinType::LongDouble:
    return cgf.getTarget().useObjCFPRetForRealType(FloatModeKind::LongDouble);
  }
}

static bool objCMessageResultUsesFP2Ret(CIRGenFunction &cgf,
                                        QualType resultType) {
  const auto *complexType = resultType->getAs<ComplexType>();
  if (!complexType)
    return false;

  const auto *builtinType =
      complexType->getElementType()->getAs<BuiltinType>();
  return builtinType && builtinType->getKind() == BuiltinType::LongDouble &&
         cgf.getTarget().useObjCFP2RetForComplexLongDouble();
}

static bool objCMessageResultUsesX86Stret(CIRGenFunction &cgf,
                                          QualType resultType) {
  return cgf.getTarget().getTriple().isX86() &&
         resultType->isStructureOrClassType() &&
         cgf.getContext().getTypeSize(resultType) > 128;
}

static bool isDarwinVTableDispatchedSelector(CIRGenFunction &cgf,
                                             Selector sel) {
  switch (cgf.cgm.getCodeGenOpts().getObjCDispatchMethod()) {
  case CodeGenOptions::Legacy:
    return false;
  case CodeGenOptions::NonLegacy:
    return true;
  case CodeGenOptions::Mixed:
    break;
  }

  std::string spelling = sel.getAsString();
  if (llvm::StringSwitch<bool>(spelling)
          .Cases({"alloc", "class", "self", "isFlipped", "length", "count"},
                 true)
          .Cases({"allocWithZone:", "isKindOfClass:",
                  "respondsToSelector:", "objectForKey:", "objectAtIndex:",
                  "isEqualToString:", "isEqual:"},
                 true)
          .Default(false))
    return true;

  if (cgf.getLangOpts().getGC() != LangOptions::GCOnly &&
      llvm::StringSwitch<bool>(spelling)
          .Cases({"retain", "release", "autorelease"}, true)
          .Default(false))
    return true;

  if (cgf.getLangOpts().getGC() != LangOptions::NonGC &&
      llvm::StringSwitch<bool>(spelling)
          .Cases({"hash", "addObject:",
                  "countByEnumeratingWithState:objects:count:"},
                 true)
          .Default(false))
    return true;

  return false;
}

static void appendSelectorForMessageRefTable(std::string &buffer,
                                             Selector selector) {
  if (selector.isUnarySelector()) {
    buffer += selector.getNameForSlot(0);
    return;
  }

  for (unsigned i = 0, e = selector.getNumArgs(); i != e; ++i) {
    buffer += selector.getNameForSlot(i);
    buffer += '_';
  }
}

static cir::FuncOp getObjCDarwinFixupMessageSendFunc(CIRGenFunction &cgf,
                                                     bool isSuperMessage,
                                                     bool usesStructReturn,
                                                     bool usesFPRet) {
  CIRGenModule &cgm = cgf.cgm;

  llvm::StringRef name;
  mlir::Type resultTy = cgm.voidPtrTy;
  if (usesStructReturn) {
    name = isSuperMessage ? "objc_msgSendSuper2_stret_fixup"
                          : "objc_msgSend_stret_fixup";
  } else if (!isSuperMessage && usesFPRet) {
    name = "objc_msgSend_fpret_fixup";
  } else {
    name = isSuperMessage ? "objc_msgSendSuper2_fixup" : "objc_msgSend_fixup";
  }

  cir::FuncType fnTy =
      cir::FuncType::get({cgm.voidPtrTy, cgm.voidPtrTy}, resultTy,
                         /*isVarArg=*/true);
  return cgm.createRuntimeFunction(fnTy, name);
}

static cir::GlobalOp getObjCDarwinMessageRefGlobal(CIRGenFunction &cgf,
                                                   mlir::Location loc,
                                                   Selector selector,
                                                   cir::FuncOp fixupFn) {
  CIRGenModule &cgm = cgf.cgm;
  CIRGenBuilderTy &builder = cgf.getBuilder();

  std::string name = "_";
  name += fixupFn.getSymName();
  name += "_";
  appendSelectorForMessageRefTable(name, selector);

  if (auto existing =
          mlir::dyn_cast_or_null<cir::GlobalOp>(cgm.getGlobalValue(name)))
    return existing;

  cir::GlobalOp selectorNameGV = cgm.getObjCSelectorNameGlobal(selector);
  auto messageRefTy = builder.getAnonRecordTy({cgm.voidPtrTy, cgm.voidPtrTy});
  auto init = builder.getAnonConstRecord(
      builder.getArrayAttr(
          {cir::GlobalViewAttr::get(
               cgm.voidPtrTy,
               mlir::FlatSymbolRefAttr::get(fixupFn.getSymNameAttr())),
           builder.getGlobalViewAttr(cgm.voidPtrTy, selectorNameGV)}),
      /*packed=*/false, /*padded=*/false, messageRefTy);

  cir::GlobalOp gv =
      CIRGenModule::createGlobalOp(cgm, loc, name, messageRefTy,
                                   /*isConstant=*/false);
  gv.setLinkage(cir::GlobalLinkageKind::WeakAnyLinkage);
  gv.setGlobalVisibility(cir::VisibilityKind::Hidden);
  mlir::SymbolTable::setSymbolVisibility(gv, cgm.getMLIRVisibility(gv));
  gv.setAlignmentAttr(cgm.getSize(CharUnits::fromQuantity(16)));
  switch (cgm.getTriple().getObjectFormat()) {
  case llvm::Triple::MachO:
    gv.setSectionAttr(
        cgm.getStringAttrCached("__DATA,__objc_msgrefs,coalesced"));
    break;
  case llvm::Triple::ELF:
    gv.setSectionAttr(cgm.getStringAttrCached("objc_msgrefs"));
    break;
  case llvm::Triple::COFF:
    gv.setSectionAttr(cgm.getStringAttrCached(".objc_msgrefs$B"));
    break;
  case llvm::Triple::UnknownObjectFormat:
    llvm_unreachable("unexpected object file format");
  case llvm::Triple::Wasm:
  case llvm::Triple::GOFF:
  case llvm::Triple::SPIRV:
  case llvm::Triple::XCOFF:
  case llvm::Triple::DXContainer:
    llvm_unreachable(
        "Objective-C support is unimplemented for object file format");
  }
  CIRGenModule::setInitializer(gv, init);
  cgm.setDSOLocal(static_cast<mlir::Operation *>(gv));
  return gv;
}

static bool isAArch64HomogeneousAggregateBaseType(ASTContext &ctx,
                                                  QualType type) {
  if (const auto *builtinType = type->getAs<BuiltinType>())
    return builtinType->isFloatingPoint();

  const auto *vectorType = type->getAs<VectorType>();
  if (!vectorType)
    return false;

  if (auto kind = vectorType->getVectorKind();
      kind == VectorKind::SveFixedLengthData ||
      kind == VectorKind::SveFixedLengthPredicate)
    return false;

  uint64_t vectorSize = ctx.getTypeSize(type);
  return vectorSize == 64 || vectorSize == 128;
}

static bool isAArch64HomogeneousAggregate(ASTContext &ctx, QualType type,
                                          QualType &base, uint64_t &members) {
  if (const auto *arrayType = ctx.getAsConstantArrayType(type)) {
    uint64_t elementCount = arrayType->getZExtSize();
    if (elementCount == 0)
      return false;
    if (!isAArch64HomogeneousAggregate(ctx, arrayType->getElementType(), base,
                                       members))
      return false;
    members *= elementCount;
  } else if (const auto *recordDecl = type->getAsRecordDecl()) {
    if (recordDecl->hasFlexibleArrayMember())
      return false;

    members = 0;

    if (const auto *cxxRecord = dyn_cast<CXXRecordDecl>(recordDecl)) {
      for (const auto &baseSpec : cxxRecord->bases()) {
        if (isEmptyRecordForLayout(ctx, baseSpec.getType()))
          continue;

        uint64_t baseMembers = 0;
        if (!isAArch64HomogeneousAggregate(ctx, baseSpec.getType(), base,
                                           baseMembers))
          return false;
        members += baseMembers;
      }
    }

    for (const auto *field : recordDecl->fields()) {
      QualType fieldType = field->getType();
      while (const auto *arrayType = ctx.getAsConstantArrayType(fieldType)) {
        if (arrayType->isZeroSize())
          return false;
        fieldType = arrayType->getElementType();
      }

      if (isEmptyRecordForLayout(ctx, fieldType) || field->isZeroLengthBitField())
        continue;

      uint64_t fieldMembers = 0;
      if (!isAArch64HomogeneousAggregate(ctx, field->getType(), base,
                                         fieldMembers))
        return false;
      if (recordDecl->isUnion()) {
        if (members < fieldMembers)
          members = fieldMembers;
      } else {
        members += fieldMembers;
      }
    }

    if (base.isNull())
      return false;

    if (ctx.getTypeSize(base) * members != ctx.getTypeSize(type))
      return false;
  } else {
    members = 1;
    if (const auto *complexType = type->getAs<ComplexType>()) {
      members = 2;
      type = complexType->getElementType();
    }

    if (!isAArch64HomogeneousAggregateBaseType(ctx, type))
      return false;

    if (base.isNull()) {
      base = type;
      if (const auto *vectorType = type->getAs<VectorType>()) {
        QualType elementType = vectorType->getElementType();
        unsigned numElements =
            ctx.getTypeSize(type) / ctx.getTypeSize(elementType);
        base = ctx.getVectorType(elementType, numElements,
                                 vectorType->getVectorKind());
      }
    }

    if (base->isVectorType() != type->isVectorType() ||
        ctx.getTypeSize(base) != ctx.getTypeSize(type))
      return false;
  }

  return members > 0 && members <= 4;
}

static bool objCMessageResultUsesAArch64Stret(CIRGenFunction &cgf,
                                              QualType resultType) {
  if (!cgf.getTarget().getTriple().isAArch64() ||
      !resultType->isStructureOrClassType())
    return false;

  if (const auto *recordDecl = resultType->getAsRecordDecl())
    if (!recordDecl->canPassInRegisters())
      return true;

  if (cgf.getContext().getTypeSize(resultType) == 0)
    return false;

  QualType hfaBase;
  uint64_t hfaMembers = 0;
  if (isAArch64HomogeneousAggregate(cgf.getContext(), resultType, hfaBase,
                                    hfaMembers))
    return false;

  return cgf.getContext().getTypeSize(resultType) > 128;
}

// GNU-family runtimes need the messenger symbol before ABI-lowered call
// information exists in CIR lowering, so preserve the AArch64 HFA/HVA
// direct-return exception here instead of using an x86-only stret rule.
static bool objCMessageResultUsesGNUStructReturnMessenger(CIRGenFunction &cgf,
                                                          QualType resultType) {
  if (objCMessageResultUsesAArch64Stret(cgf, resultType))
    return true;
  return objCMessageResultUsesX86Stret(cgf, resultType);
}

static bool getGNUNonLegacyMessageReturnABI(CIRGenFunction &cgf,
                                            QualType resultType,
                                            cir::FuncType exactFnTy,
                                            bool &usesStructReturnMessenger,
                                            bool &usesInRegStructReturnMessenger) {
  usesStructReturnMessenger = false;
  usesInRegStructReturnMessenger = false;
  // Any aggregate, complex, _BitInt, or vector return may be classified as an
  // indirect (sret) return by the ABI and therefore require the struct-return
  // messenger; only definitely-direct scalar/void returns can be fast-pathed.
  // Restricting this to isStructureOrClassType() wrongly used the plain
  // messenger for e.g. _Complex returns that are returned indirectly on i386.
  if (!resultType->isAggregateType() && !resultType->isAnyComplexType() &&
      !resultType->isBitIntType() && !resultType->isVectorType())
    return false;

  mlir::PatternRewriter rewriter(&cgf.cgm.getMLIRContext());
  auto lowerModule = cir::createLowerModule(cgf.cgm.getModule(), rewriter);
  if (!lowerModule)
    return false;

  const auto &fnInfo = lowerModule->getTypes().arrangeFreeFunctionType(
      exactFnTy, /*resultProvenance=*/cir::LowerFunctionInfoArgInfo::TypeProvenance::None,
      /*argProvenances=*/{}, cir::CallingConv::C);
  const auto &retInfo = fnInfo.getReturnInfo();
  usesStructReturnMessenger = retInfo.isIndirect();
  const llvm::Triple &triple = cgf.getTarget().getTriple();
  usesInRegStructReturnMessenger =
      usesStructReturnMessenger && triple.isWindowsMSVCEnvironment() &&
      triple.isAArch64() && retInfo.getInReg();
  return true;
}

static bool objCMessageResultUsesIndirectReturnABI(CIRGenFunction &cgf,
                                                   cir::FuncType exactFnTy) {
  mlir::PatternRewriter rewriter(&cgf.cgm.getMLIRContext());
  auto lowerModule = cir::createLowerModule(cgf.cgm.getModule(), rewriter);
  if (!lowerModule)
    return false;

  const auto &fnInfo = lowerModule->getTypes().arrangeFreeFunctionType(
      exactFnTy,
      /*resultProvenance=*/cir::LowerFunctionInfoArgInfo::TypeProvenance::None,
      /*argProvenances=*/{}, cir::CallingConv::C);
  return fnInfo.getReturnInfo().isIndirect();
}

static bool canObjCMessageReceiverBeNull(CIRGenFunction &cgf,
                                         const ObjCMethodDecl *currentMethod,
                                         const ObjCMethodDecl *messageMethod,
                                         bool isSuperMessage,
                                         const ObjCInterfaceDecl *classReceiver,
                                         mlir::Value receiver) {
  if (isSuperMessage)
    return false;

  if (classReceiver && messageMethod && messageMethod->isClassMethod())
    return isWeakLinkedObjCClass(classReceiver);

  if (currentMethod) {
    const VarDecl *selfDecl = currentMethod->getSelfDecl();
    if (selfDecl && selfDecl->getType().isConstQualified()) {
      mlir::Value strippedReceiver = stripObjCPointerBitCasts(receiver);
      if (auto loadOp = strippedReceiver.getDefiningOp<cir::LoadOp>()) {
        Address selfAddr = cgf.getAddrOfLocalVar(selfDecl);
        mlir::Value strippedAddr = stripObjCPointerBitCasts(loadOp.getAddr());
        mlir::Value strippedSelf = stripObjCPointerBitCasts(selfAddr.getPointer());
        if (strippedAddr == strippedSelf)
          return false;
      }
    }
  }

  return true;
}

static void eraseCallNoReturnAttr(cir::CIRCallOpInterface callOp) {
  if (!callOp)
    return;

  auto extraAttrs =
      callOp->getAttrOfType<cir::ExtraFuncAttributesAttr>("extra_attrs");
  if (!extraAttrs || !extraAttrs.getElements().get("noreturn"))
    return;

  mlir::NamedAttrList attrs{extraAttrs.getElements().getValue()};
  attrs.erase("noreturn");
  if (attrs.empty())
    callOp->removeAttr("extra_attrs");
  else
    callOp->setAttr("extra_attrs", cir::ExtraFuncAttributesAttr::get(
                                       attrs.getDictionary(callOp->getContext())));
}

static void eraseFuncNoReturnAttr(cir::FuncOp fn) {
  auto extraAttrs = fn.getExtraAttrs();
  if (!extraAttrs || !extraAttrs->getElements().get("noreturn"))
    return;

  mlir::NamedAttrList attrs{extraAttrs->getElements().getValue()};
  attrs.erase("noreturn");
  if (attrs.empty())
    fn->removeAttr("extra_attrs");
  else
    fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(fn.getContext())));
}

static void destroyObjCCalleeDestroyedArguments(CIRGenFunction &cgf,
                                                mlir::Location loc,
                                                const ObjCMethodDecl *method,
                                                const CallArgList &actualArgs) {
  if (!method)
    return;

  auto argIt = actualArgs.begin();
  if (actualArgs.size() >= 2)
    argIt += 2;

  for (const ParmVarDecl *param : method->parameters()) {
    assert(argIt != actualArgs.end() && "missing Objective-C call argument");
    const CallArg &arg = *argIt++;

    if (param->hasAttr<NSConsumedAttr>() &&
        param->getType()->isObjCRetainableType()) {
      RValue rv = arg.getKnownRValue();
      assert(rv.isScalar() && "ns_consumed Objective-C arg must be scalar");
      if (mlir::Value object = rv.getValue())
        cgf.emitARCRelease(object, loc, /*isImprecise=*/true);
      continue;
    }

    QualType paramTy = param->getType();
    const auto *record = paramTy->getAsRecordDecl();
    if (!record || !record->isParamDestroyedInCallee())
      continue;

    QualType::DestructionKind dtorKind = paramTy.isDestructedType();
    if (dtorKind != QualType::DK_cxx_destructor &&
        dtorKind != QualType::DK_nontrivial_c_struct)
      continue;

    Address addr = arg.hasLValue() ? arg.getKnownLValue().getAddress()
                                   : arg.getKnownRValue().getAggregateAddress();
    cgf.emitDestroy(addr, paramTy, cgf.getDestroyer(dtorKind),
                    /*useEHCleanupForArray=*/false);
  }
}

static void maybeAttachMemberFunctionPointerAuthAttrs(
    mlir::Operation *op, QualType srcType, QualType destType,
    clang::CIRGen::CIRGenFunction &cgf) {
  if (!srcType->isMemberFunctionPointerType() ||
      !destType->isMemberFunctionPointerType())
    return;

  auto srcAuthInfo = cgf.cgm.getMemberFunctionPointerAuthInfo(srcType);
  auto dstAuthInfo = cgf.cgm.getMemberFunctionPointerAuthInfo(destType);
  if (!srcAuthInfo && !dstAuthInfo)
    return;

  auto &builder = cgf.getBuilder();
  if (srcAuthInfo) {
    op->setAttr("cir.memptr.ptrauth_src_discriminator",
                builder.getI64IntegerAttr(srcAuthInfo->discriminator));
  }
  if (dstAuthInfo) {
    op->setAttr("cir.memptr.ptrauth_dst_discriminator",
                builder.getI64IntegerAttr(dstAuthInfo->discriminator));
  }
}

struct BinOpInfo {
  mlir::Value lhs;
  mlir::Value rhs;
  SourceRange loc;
  QualType fullType;             // Type of operands and result
  QualType compType;             // Type used for computations. Element type
                                 // for vectors, otherwise same as FullType.
  BinaryOperator::Opcode opcode; // Opcode of BinOp to perform
  FPOptions fpfeatures;
  const Expr *e; // Entire expr, for error unsupported.  May not be binop.

  /// Check if the binop computes a division or a remainder.
  bool isDivRemOp() const {
    return opcode == BO_Div || opcode == BO_Rem || opcode == BO_DivAssign ||
           opcode == BO_RemAssign;
  }

  /// Check if the binop can result in integer overflow.
  /// Matches OGCG CGExprScalar.cpp BinOpInfo::mayHaveIntegerOverflow.
  bool mayHaveIntegerOverflow() const {
    // Without constant input, we can't rule out overflow.
    auto lhsci = lhs.getDefiningOp<cir::ConstantOp>();
    auto rhsci = rhs.getDefiningOp<cir::ConstantOp>();
    if (!lhsci || !rhsci)
      return true;

    // Both operands are constants — check for overflow precisely.
    auto lhsIntAttr = lhsci.getValueAttr<cir::IntAttr>();
    auto rhsIntAttr = rhsci.getValueAttr<cir::IntAttr>();
    if (!lhsIntAttr || !rhsIntAttr)
      return true;

    const llvm::APInt &lhsAP = lhsIntAttr.getValue();
    const llvm::APInt &rhsAP = rhsIntAttr.getValue();
    bool isSigned = fullType->hasSignedIntegerRepresentation();
    bool overflow = true;
    llvm::APInt result;

    if (opcode == BO_Add) {
      result = isSigned ? lhsAP.sadd_ov(rhsAP, overflow)
                        : lhsAP.uadd_ov(rhsAP, overflow);
    } else if (opcode == BO_Sub) {
      result = isSigned ? lhsAP.ssub_ov(rhsAP, overflow)
                        : lhsAP.usub_ov(rhsAP, overflow);
    } else if (opcode == BO_Mul) {
      result = isSigned ? lhsAP.smul_ov(rhsAP, overflow)
                        : lhsAP.umul_ov(rhsAP, overflow);
    } else if (opcode == BO_Div || opcode == BO_Rem) {
      if (isSigned && !rhsAP.isZero())
        result = lhsAP.sdiv_ov(rhsAP, overflow);
      else
        return false;
    }
    return overflow;
  }

  /// Check if the binop can divide by zero.
  /// Matches OGCG CGExprScalar.cpp BinOpInfo::mayHaveIntegerDivisionByZero.
  bool mayHaveIntegerDivisionByZero() const {
    auto rhsci = rhs.getDefiningOp<cir::ConstantOp>();
    if (!rhsci)
      return true;

    auto rhsIntAttr = rhsci.getValueAttr<cir::IntAttr>();
    if (!rhsIntAttr)
      return true;

    return rhsIntAttr.getValue().isZero();
  }

  /// Check if the binop can result in a float division by zero.
  /// Matches OGCG CGExprScalar.cpp BinOpInfo::mayHaveFloatDivisionByZero.
  bool mayHaveFloatDivisionByZero() const {
    if (!isDivRemOp())
      return true;

    auto rhsci = rhs.getDefiningOp<cir::ConstantOp>();
    if (!rhsci)
      return true;

    auto rhsFPAttr = rhsci.getValueAttr<cir::FPAttr>();
    if (!rhsFPAttr)
      return true;

    return rhsFPAttr.getValue().isZero();
  }

  /// Check if at least one operand is a fixed point type. In such cases,
  /// this operation did not follow usual arithmetic conversion and both
  /// operands might not be of the same type.
  bool isFixedPointOp() const {
    // We cannot simply check the result type since comparison operations
    // return an int.
    if (const auto *binOp = llvm::dyn_cast<BinaryOperator>(e)) {
      QualType lhstype = binOp->getLHS()->getType();
      QualType rhstype = binOp->getRHS()->getType();
      return lhstype->isFixedPointType() || rhstype->isFixedPointType();
    }
    if (const auto *unop = llvm::dyn_cast<UnaryOperator>(e))
      return unop->getSubExpr()->getType()->isFixedPointType();
    return false;
  }

  bool rhsHasSignedIntegerRepresentation() const {
    if (const auto *binOp = llvm::dyn_cast<BinaryOperator>(e)) {
      QualType rhsType = binOp->getRHS()->getType();
      return rhsType->hasSignedIntegerRepresentation();
    }
    return false;
  }
};

/// Check if a unary negation overflow check can be elided because the constant
/// operands provably don't overflow. This is the fast path of
/// canElideOverflowCheck for the unary minus case — it only checks constant
/// folding, which is sufficient for negation of integer literals like -5.
static bool canElideNegationOverflowCheck(const BinOpInfo &op) {
  // If the binop has constant inputs and we can prove there is no overflow,
  // we can elide the overflow check.
  if (!op.mayHaveIntegerOverflow())
    return true;

  // If a unary op has a widened operand, the op cannot overflow.
  if (const auto *uo = dyn_cast<UnaryOperator>(op.e))
    return !uo->canOverflow();

  return false;
}

static bool isProvablyNullValue(CIRGenBuilderTy &builder, mlir::Value value) {
  if (auto constOp = value.getDefiningOp<cir::ConstantOp>())
    return builder.isNullValue(constOp.getValue());
  return false;
}

static mlir::StringAttr getOverflowHandlerNameAttr(CIRGenBuilderTy &builder,
                                                   BinaryOperator::Opcode op) {
  switch (op) {
  case BO_Add:
  case BO_AddAssign:
    return builder.getStringAttr("add_overflow");
  case BO_Sub:
  case BO_SubAssign:
    return builder.getStringAttr("sub_overflow");
  case BO_Mul:
  case BO_MulAssign:
    return builder.getStringAttr("mul_overflow");
  case BO_Div:
  case BO_DivAssign:
  case BO_Rem:
  case BO_RemAssign:
    return builder.getStringAttr("divrem_overflow");
  default:
    llvm_unreachable("unexpected opcode for overflow-checked binop");
  }
}

static mlir::Value emitCheckValue(CIRGenFunction &cgf, mlir::Value value,
                                  QualType ty, mlir::Location loc) {
  auto &builder = cgf.getBuilder();
  auto intptrTy = builder.getUIntNTy(
      cgf.getContext().getTypeSize(cgf.getContext().getUIntPtrType()));
  if (value.getType() == intptrTy)
    return value;

  // Floating-point values which fit in intptr_t are bitcast to integers and
  // passed directly, matching OGCG CodeGenFunction::EmitCheckValue.
  if (ty->isFloatingType()) {
    unsigned bits = static_cast<unsigned>(cgf.getContext().getTypeSize(ty));
    if (bits <= intptrTy.getWidth()) {
      value = builder.createBitcast(loc, value, builder.getUIntNTy(bits));
      if (auto *castOp = value.getDefiningOp())
        markNoSanitize(castOp);
    }
  }

  if (auto intTy = mlir::dyn_cast<cir::IntType>(value.getType())) {
    // Integers which fit in intptr_t are zero-extended and passed directly.
    if (intTy.getWidth() <= intptrTy.getWidth()) {
      if (intTy.isSigned()) {
        value = builder.createCast(cir::CastKind::integral, value,
                                   builder.getUIntNTy(intTy.getWidth()));
        // Only mark the cast we just created, not the original value's
        // defining op which may be a user-written source load.
        if (auto *castOp = value.getDefiningOp())
          markNoSanitize(castOp);
      }
      auto castToIntPtr = builder.createIntCast(value, intptrTy);
      if (auto *castOp = castToIntPtr.getDefiningOp())
        markNoSanitize(castOp);
      return castToIntPtr;
    }
  }

  // Pointers are passed directly, everything else is passed by address.
  if (mlir::isa<cir::PointerType>(value.getType())) {
    auto ptrAsInt = builder.createPtrToInt(value, intptrTy);
    if (auto *castOp = ptrAsInt.getDefiningOp())
      markNoSanitize(castOp);
    return ptrAsInt;
  }

  Address temp = cgf.createMemTemp(ty, loc, "ubsan.overflow.arg");
  // Store the value's in-memory representation. For types whose value width
  // differs from their storage width (e.g. _BitInt(100) is an i100 value but is
  // stored as i128), storing the raw value into the storage-typed temp would
  // mismatch the alloca's pointee type and fail verification.
  mlir::Value memValue = cgf.emitToMemory(value, ty);
  auto store = builder.createStore(loc, memValue, temp);
  markNoSanitize(store);
  auto ptrAsInt = builder.createPtrToInt(temp.getPointer(), intptrTy);
  markNoSanitize(ptrAsInt.getDefiningOp());
  return ptrAsInt;
}

static void emitOverflowHandlerStubCall(CIRGenFunction &cgf,
                                        CIRGenBuilderTy &builder,
                                        mlir::Location loc,
                                        const BinOpInfo &ops,
                                        llvm::StringRef handlerKind) {
  auto intptrTy = builder.getUIntNTy(
      cgf.getContext().getTypeSize(cgf.getContext().getUIntPtrType()));

  // Check if this is a unary negation (synthesized as 0 - val).
  // OGCG uses __ubsan_handle_negate_overflow with only one dynamic arg.
  const UnaryOperator *UO =
      ops.e ? dyn_cast<UnaryOperator>(ops.e) : nullptr;
  bool isNegate = UO && UO->getOpcode() == UO_Minus;

  cir::FuncOp stubFn;
  cir::CallOp call;
  if (isNegate) {
    auto stubFnTy =
        cir::FuncType::get({intptrTy}, builder.getVoidTy());
    stubFn = cgf.cgm.createRuntimeFunction(
        stubFnTy, "__cir_ubsan_overflow_handler_negate", /*attrs=*/{},
        /*isLocal=*/false, /*assumeConvergent=*/false);
    call = builder.createCallOp(
        loc, stubFn,
        {emitCheckValue(cgf, ops.rhs, ops.fullType, loc)});
  } else {
    auto stubFnTy =
        cir::FuncType::get({intptrTy, intptrTy}, builder.getVoidTy());
    stubFn = cgf.cgm.createRuntimeFunction(
        stubFnTy, "__cir_ubsan_overflow_handler", /*attrs=*/{},
        /*isLocal=*/false, /*assumeConvergent=*/false);
    call = builder.createCallOp(
        loc, stubFn,
        {emitCheckValue(cgf, ops.lhs, ops.fullType, loc),
         emitCheckValue(cgf, ops.rhs, ops.fullType, loc)});
  }

  SourceLocation exprLoc = ops.e ? ops.e->getExprLoc() : ops.loc.getBegin();
  setUBSanSourceLocationAttrs(cgf, call, exprLoc);
  setUBSanTypeNameAttr(cgf, call, ops.fullType);
  setUBSanTypeDescriptorAttrs(cgf, call, ops.fullType);
  call->setDiscardableAttr(
      "cir.ubsan.handler_name",
      builder.getStringAttr(isNegate ? "negate_overflow"
                                     : getOverflowHandlerNameAttr(builder,
                                                                  ops.opcode)
                                           .getValue()));
  call->setDiscardableAttr("cir.ubsan.handler_kind",
                           builder.getStringAttr(handlerKind));
  call->setDiscardableAttr("cir.nosanitize",
                           mlir::UnitAttr::get(builder.getContext()));
}

/// Emit an overflow-checked binary operation for -ftrapv and overflow
/// sanitizers. Creates a cir.binop.overflow operation and then either:
///   * calls a user-specified handler function (-ftrapv-handler),
///   * emits a UBSan runtime call for recover/abort sanitizer mode, or
///   * traps on overflow for -ftrapv / -fsanitize-trap.
static mlir::Value emitOverflowCheckedBinOp(CIRGenFunction &cgf,
                                            CIRGenBuilderTy &builder,
                                            const BinOpInfo &ops) {
  cir::BinOpOverflowKind opKind;
  // Handler IDs for llvm.ubsantrap, matching SanitizerHandler enum ordinals
  // from clang/lib/CodeGen/SanitizerHandler.h.
  uint8_t handlerID;
  // OpID encodes (opcode << 1 | isSigned) for the overflow handler function.
  // Matches OGCG CGExprScalar.cpp:4246-4248.
  unsigned opID = 0;
  bool isSigned = ops.fullType->isSignedIntegerOrEnumerationType();
  switch (ops.opcode) {
  case BO_Add:
  case BO_AddAssign:
    opKind = cir::BinOpOverflowKind::Add;
    handlerID = 0; // SanitizerHandler::AddOverflow
    opID = 1;
    break;
  case BO_Sub:
  case BO_SubAssign:
    opKind = cir::BinOpOverflowKind::Sub;
    handlerID = 21; // SanitizerHandler::SubOverflow
    opID = 2;
    break;
  case BO_Mul:
  case BO_MulAssign:
    opKind = cir::BinOpOverflowKind::Mul;
    handlerID = 12; // SanitizerHandler::MulOverflow
    opID = 3;
    break;
  default:
    llvm_unreachable("unexpected opcode for overflow-checked binop");
  }

  opID <<= 1;
  if (isSigned)
    opID |= 1;

  mlir::Location loc = cgf.getLoc(ops.loc);
  auto resultTy = mlir::cast<cir::IntType>(cgf.convertType(ops.fullType));
  auto overflowOp = cir::BinOpOverflowOp::create(builder, loc, resultTy,
                                                 opKind, ops.lhs, ops.rhs);
  markNoSanitize(overflowOp);
  mlir::Value result = overflowOp.getResult();
  mlir::Value overflow = overflowOp.getOverflow();

  // Check if a user-specified overflow handler is set (-ftrapv-handler).
  // If so, call the handler and use its return value instead of trapping.
  // Matches OGCG CGExprScalar.cpp:4262-4324.
  const std::string &handlerName = cgf.getLangOpts().OverflowHandler;
  if (!handlerName.empty()) {
    // Create the handler function type: i64(i64, i64, i8, i8, ...)
    auto i64Ty = cir::IntType::get(builder.getContext(), 64, /*isSigned=*/true);
    auto i8Ty = cir::IntType::get(builder.getContext(), 8, /*isSigned=*/false);
    auto handlerFuncTy = cir::FuncType::get({i64Ty, i64Ty, i8Ty, i8Ty},
                                            i64Ty, /*isVarArg=*/true);
    auto handlerFunc =
        cgf.cgm.createRuntimeFunction(handlerFuncTy, handlerName);

    // Use cir.ternary: on overflow call handler, otherwise use normal result.
    // All operations in the overflow path are marked !nosanitize (matching
    // OGCG's SanitizerDebugLocation scope) and the handler call is nounwind
    // (OGCG uses EmitNounwindRuntimeCall).
    auto ternaryOp = cir::TernaryOp::create(
        builder, loc, overflow,
        /*trueBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location ternLoc) {
          // Sign-extend operands to i64 for the handler.
          mlir::Value lhs =
              cir::CastOp::create(b, ternLoc, i64Ty,
                                  cir::CastKind::integral, ops.lhs);
          markNoSanitize(lhs.getDefiningOp());
          mlir::Value rhs =
              cir::CastOp::create(b, ternLoc, i64Ty,
                                  cir::CastKind::integral, ops.rhs);
          markNoSanitize(rhs.getDefiningOp());
          mlir::Value opIDVal = cir::ConstantOp::create(
              b, ternLoc,
              cir::IntAttr::get(i8Ty, static_cast<uint64_t>(opID)));
          mlir::Value bitWidthVal = cir::ConstantOp::create(
              b, ternLoc,
              cir::IntAttr::get(i8Ty,
                                static_cast<uint64_t>(resultTy.getWidth())));

          // Call the handler: i64 handler(i64 lhs, i64 rhs, i8 opID, i8 bw)
          auto callOp = cir::CallOp::create(
              b, ternLoc,
              mlir::SymbolRefAttr::get(b.getContext(),
                                       handlerFunc.getSymName()),
              i64Ty, mlir::ValueRange{lhs, rhs, opIDVal, bitWidthVal});
          markNoSanitize(callOp);
          callOp.setNothrow(true);

          // Truncate the i64 result back to the operation type.
          mlir::Value handlerResult = cir::CastOp::create(
              b, ternLoc, resultTy, cir::CastKind::integral,
              callOp.getResult());
          markNoSanitize(handlerResult.getDefiningOp());
          cir::YieldOp::create(b, ternLoc, handlerResult);
        },
        /*falseBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location ternLoc) {
          cir::YieldOp::create(b, ternLoc, result);
        });
    markNoSanitize(ternaryOp);
    return ternaryOp.getResult();
  }

  SanitizerKind::SanitizerOrdinal sanitizerKind =
      isSigned ? SanitizerKind::SO_SignedIntegerOverflow
               : SanitizerKind::SO_UnsignedIntegerOverflow;
  mlir::Value noOverflow;
  if (cgf.sanOpts.has(sanitizerKind)) {
    noOverflow = builder.createNot(overflow);
    if (auto *notOp = noOverflow.getDefiningOp())
      markNoSanitize(notOp);
    noOverflow = cgf.emitGuardedUBSanCheck(loc, noOverflow, sanitizerKind);
  }

  if (cgf.sanOpts.has(sanitizerKind) &&
      !cgf.cgm.getCodeGenOpts().SanitizeTrap.has(sanitizerKind)) {
    auto ifOp = cir::IfOp::create(
        builder, loc, noOverflow, /*withElseRegion=*/true,
        [&](mlir::OpBuilder &b, mlir::Location ifLoc) {
          cir::YieldOp::create(b, ifLoc);
        },
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          StringRef handlerKind = "abort";
          if (cgf.cgm.getCodeGenOpts().SanitizeRecover.has(sanitizerKind))
            handlerKind = "recover";
          emitOverflowHandlerStubCall(cgf, builder, ifLoc, ops, handlerKind);

          if (handlerKind == "abort") {
            auto unreachable = cir::UnreachableOp::create(builder, ifLoc);
            markNoSanitize(unreachable);
          } else {
            auto yield = cir::YieldOp::create(builder, ifLoc);
            markNoSanitize(yield);
          }
        });
    setSanitizerBranchAttrs(builder, ifOp);
    return result;
  }

  // No handler specified — trap on overflow.
  // Mark with ubsantrap handler ID so LowerToLLVM
  // emits llvm.ubsantrap(i8 N) instead of llvm.trap().
  const std::string &trapFuncName = cgf.cgm.getCodeGenOpts().TrapFuncName;
  if (cgf.sanOpts.has(sanitizerKind)) {
    // Sanitizer trap path: use NegateOverflow for unary negation, matching
    // OGCG's EmitBinOpCheck which checks for UnaryOperator UO_Minus.
    // Note: the -ftrapv path (below) keeps SubOverflow, also matching OGCG.
    uint8_t trapHandlerID = handlerID;
    {
      const UnaryOperator *UO =
          ops.e ? dyn_cast<UnaryOperator>(ops.e) : nullptr;
      if (UO && UO->getOpcode() == UO_Minus)
        trapHandlerID = 13; // SanitizerHandler::NegateOverflow
    }
    auto trapIf = cir::IfOp::create(
        builder, loc, noOverflow, /*withElseRegion=*/true,
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          auto yield = cir::YieldOp::create(builder, ifLoc);
          markNoSanitize(yield);
        },
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          auto trapOp = cir::TrapOp::create(builder, ifLoc);
          trapOp->setAttr(
              "cir.ubsantrap_handler",
              mlir::IntegerAttr::get(
                  mlir::IntegerType::get(builder.getContext(), 8),
                  trapHandlerID));
          markNoSanitize(trapOp);
          if (!trapFuncName.empty())
            trapOp->setAttr("cir.trap_func_name",
                            cgf.cgm.getStringAttrCached(trapFuncName));
        });
    setSanitizerBranchAttrs(builder, trapIf);
  } else {
    auto trapIf = cir::IfOp::create(
        builder, loc, overflow, /*withElseRegion=*/false,
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          auto trapOp = cir::TrapOp::create(builder, ifLoc);
          trapOp->setAttr(
              "cir.ubsantrap_handler",
              mlir::IntegerAttr::get(
                  mlir::IntegerType::get(builder.getContext(), 8), handlerID));
          markNoSanitize(trapOp);
          if (!trapFuncName.empty())
            trapOp->setAttr("cir.trap_func_name",
                            cgf.cgm.getStringAttrCached(trapFuncName));
        });
    trapIf->setAttr("cir.nosanitize", mlir::UnitAttr::get(builder.getContext()));
  }

  return result;
}

static mlir::Value emitSanitizerValueHandle(CIRGenFunction &cgf,
                                            CIRGenBuilderTy &builder,
                                            mlir::Location loc,
                                            mlir::Value value,
                                            QualType valueTy) {
  auto intptrTy = mlir::cast<cir::IntType>(
      cgf.convertType(cgf.getContext().getUIntPtrType()));
  if (value.getType() == intptrTy)
    return value;

  if (mlir::isa<cir::FPTypeInterface>(value.getType())) {
    unsigned bitWidth = cgf.getContext().getTypeSize(valueTy);
    if (bitWidth <= intptrTy.getWidth()) {
      value = builder.createBitcast(loc, value, builder.getUIntNTy(bitWidth));
      if (auto *op = value.getDefiningOp())
        markNoSanitize(op);
      auto ptrSizedValue = builder.createIntCast(value, intptrTy);
      if (auto *op = ptrSizedValue.getDefiningOp())
        markNoSanitize(op);
      return ptrSizedValue;
    }
  }

  if (auto intTy = mlir::dyn_cast<cir::IntType>(value.getType());
      intTy && intTy.getWidth() <= intptrTy.getWidth()) {
    value = castIntegerToSignedness(builder, loc, value, /*isSigned=*/false);
    if (auto *op = value.getDefiningOp())
      markNoSanitize(op);
    auto ptrSizedValue =
        builder.createCast(loc, cir::CastKind::integral, value, intptrTy);
    if (auto *op = ptrSizedValue.getDefiningOp())
      markNoSanitize(op);
    return ptrSizedValue;
  }

  if (mlir::isa<cir::PointerType>(value.getType())) {
    auto ptrAsInt = builder.createPtrToInt(value, intptrTy);
    markNoSanitize(ptrAsInt.getDefiningOp());
    return ptrAsInt;
  }

  Address tmpAddr = cgf.createTempAlloca(cgf.convertTypeForMem(valueTy),
                                         cgf.getContext().getTypeAlignInChars(valueTy),
                                         loc, "ubsan.value");
  // Store the in-memory representation: for types whose value width differs from
  // their storage width (e.g. _BitInt(100) is an i100 value stored as i128),
  // storing the raw value into the storage-typed temp mismatches the alloca's
  // pointee type and fails verification.
  mlir::Value memValue = cgf.emitToMemory(value, valueTy);
  auto store = builder.createStore(loc, memValue, tmpAddr);
  markNoSanitize(store);
  auto ptrAsInt = builder.createPtrToInt(tmpAddr.getPointer(), intptrTy);
  markNoSanitize(ptrAsInt.getDefiningOp());
  return ptrAsInt;
}

static void setUBSanTypeDescriptorAttrs(CIRGenFunction &cgf,
                                        CIRGenBuilderTy &builder,
                                        mlir::Operation *op,
                                        llvm::StringRef prefix,
                                        QualType ty) {
  uint16_t typeKind = 0xFFFF;
  uint16_t typeInfo = 0;

  if (ty->isIntegerType()) {
    typeKind = 0;
    typeInfo = (llvm::Log2_32(cgf.getContext().getTypeSize(ty)) << 1) |
               (ty->isSignedIntegerType() ? 1 : 0);
    if (ty->isSignedIntegerType() && ty->getAs<BitIntType>())
      typeKind = 2;
  } else if (ty->isFloatingType()) {
    typeKind = 1;
    typeInfo = cgf.getContext().getTypeSize(ty);
  }

  SmallString<32> typeName;
  cgf.cgm.getDiags().ConvertArgToString(
      DiagnosticsEngine::ak_qualtype, (intptr_t)ty.getAsOpaquePtr(),
      StringRef(), StringRef(), {}, typeName, {});

  auto attrName = [&](llvm::StringRef suffix) {
    return builder.getStringAttr((prefix + suffix).str());
  };

  op->setDiscardableAttr(attrName("_type_name"), builder.getStringAttr(typeName));
  op->setDiscardableAttr(attrName("_type_kind"),
                         builder.getI32IntegerAttr(typeKind));
  op->setDiscardableAttr(attrName("_type_info"),
                         builder.getI32IntegerAttr(typeInfo));
}

static mlir::Value buildAndNoSanitize(CIRGenBuilderTy &builder,
                                      mlir::Location loc, mlir::Value lhs,
                                      mlir::Value rhs);

/// Known implicit conversion check kinds.
/// Keep in sync with compiler-rt's ImplicitConversionCheckKind.
enum ImplicitConversionCheckKind : unsigned char {
  ICCK_IntegerTruncation = 0,
  ICCK_UnsignedIntegerTruncation = 1,
  ICCK_SignedIntegerTruncation = 2,
  ICCK_IntegerSignChange = 3,
  ICCK_SignedIntegerTruncationOrSignChange = 4,
};

using ImplicitConversionCheck =
    std::pair<mlir::Value, SanitizerKind::SanitizerOrdinal>;

static bool isPotentiallyEligibleForImplicitIntegerConversionCheck(
    QualType srcType, QualType dstType) {
  return srcType->isIntegerType() && dstType->isIntegerType();
}

static std::pair<ImplicitConversionCheckKind, ImplicitConversionCheck>
emitIntegerTruncationCheckHelper(CIRGenBuilderTy &builder, mlir::Location loc,
                                 mlir::Value src, QualType srcType,
                                 mlir::Value dst, QualType dstType) {
  auto srcIntTy = mlir::cast<cir::IntType>(src.getType());
  auto dstIntTy = mlir::cast<cir::IntType>(dst.getType());

  assert(srcIntTy.getWidth() > dstIntTy.getWidth() &&
         "expected a truncating integer conversion");

  bool srcSigned = srcType->isSignedIntegerOrEnumerationType();
  bool dstSigned = dstType->isSignedIntegerOrEnumerationType();

  ImplicitConversionCheckKind kind;
  SanitizerKind::SanitizerOrdinal ordinal;
  if (!srcSigned && !dstSigned) {
    kind = ICCK_UnsignedIntegerTruncation;
    ordinal = SanitizerKind::SO_ImplicitUnsignedIntegerTruncation;
  } else {
    kind = ICCK_SignedIntegerTruncation;
    ordinal = SanitizerKind::SO_ImplicitSignedIntegerTruncation;
  }

  auto compareTy =
      cir::IntType::get(builder.getContext(), srcIntTy.getWidth(), dstSigned);
  mlir::Value extendedDst =
      castIntegerToSignedness(builder, loc, dst, dstSigned);
  if (auto *op = extendedDst.getDefiningOp())
    markNoSanitize(op);
  if (extendedDst.getType() != compareTy) {
    extendedDst = builder.createIntCast(extendedDst, compareTy);
    if (auto *op = extendedDst.getDefiningOp())
      markNoSanitize(op);
  }

  mlir::Value srcForCompare =
      castIntegerToSignedness(builder, loc, src, dstSigned);
  if (auto *op = srcForCompare.getDefiningOp())
    markNoSanitize(op);

  mlir::Value check = cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq,
                                         extendedDst, srcForCompare);
  markNoSanitize(check.getDefiningOp());

  return {kind, {check, ordinal}};
}

static mlir::Value emitIsNegativeTestHelper(CIRGenBuilderTy &builder,
                                            mlir::Location loc,
                                            mlir::Value value,
                                            QualType valueType) {
  if (!valueType->isSignedIntegerOrEnumerationType())
    return builder.getBool(false, loc);

  value = castIntegerToSignedness(builder, loc, value, /*isSigned=*/true);
  if (auto *op = value.getDefiningOp())
    markNoSanitize(op);

  auto zero = builder.getConstInt(loc, value.getType(), 0);
  mlir::Value isNegative =
      cir::CmpOp::create(builder, loc, cir::CmpOpKind::lt, value, zero);
  markNoSanitize(isNegative.getDefiningOp());
  return isNegative;
}

static std::pair<ImplicitConversionCheckKind, ImplicitConversionCheck>
emitIntegerSignChangeCheckHelper(CIRGenBuilderTy &builder, mlir::Location loc,
                                 mlir::Value src, QualType srcType,
                                 mlir::Value dst, QualType dstType) {
  auto srcIntTy = mlir::cast<cir::IntType>(src.getType());
  auto dstIntTy = mlir::cast<cir::IntType>(dst.getType());
  bool srcSigned = srcType->isSignedIntegerOrEnumerationType();
  bool dstSigned = dstType->isSignedIntegerOrEnumerationType();
  (void)srcSigned;
  (void)dstSigned;

  assert(((srcIntTy.getWidth() != dstIntTy.getWidth()) ||
          (srcSigned != dstSigned)) &&
         "expected sign-change check only for non-identical integer casts");

  mlir::Value srcIsNegative =
      emitIsNegativeTestHelper(builder, loc, src, srcType);
  mlir::Value dstIsNegative =
      emitIsNegativeTestHelper(builder, loc, dst, dstType);
  mlir::Value check = cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq,
                                         srcIsNegative, dstIsNegative);
  markNoSanitize(check.getDefiningOp());

  return {ICCK_IntegerSignChange,
          {check, SanitizerKind::SO_ImplicitIntegerSignChange}};
}

static void emitImplicitConversionStubCall(
    CIRGenFunction &cgf, CIRGenBuilderTy &builder, mlir::Location loc,
    SourceLocation sourceLoc, QualType srcType, QualType dstType,
    mlir::Value src, mlir::Value dst, ImplicitConversionCheckKind checkKind,
    llvm::StringRef handlerKind) {
  auto intptrTy = builder.getUIntNTy(
      cgf.getContext().getTypeSize(cgf.getContext().getUIntPtrType()));
  auto stubFnTy = cir::FuncType::get({intptrTy, intptrTy}, builder.getVoidTy());
  llvm::StringRef stubName =
      handlerKind == "abort" ? "__cir_ubsan_implicit_conversion_abort"
                             : "__cir_ubsan_implicit_conversion";
  auto stubFn = cgf.cgm.createRuntimeFunction(
      stubFnTy, stubName, /*attrs=*/{},
      /*isLocal=*/false, /*assumeConvergent=*/false);

  mlir::Value srcArg =
      emitSanitizerValueHandle(cgf, builder, loc, src, srcType);
  mlir::Value dstArg =
      emitSanitizerValueHandle(cgf, builder, loc, dst, dstType);
  if (auto *op = srcArg.getDefiningOp())
    markNoSanitize(op);
  if (auto *op = dstArg.getDefiningOp())
    markNoSanitize(op);

  auto call = builder.createCallOp(loc, stubFn, {srcArg, dstArg});
  setUBSanSourceLocationAttrs(cgf, call, sourceLoc);
  setUBSanTypeDescriptorAttrs(cgf, builder, call, "cir.ubsan.src", srcType);
  setUBSanTypeDescriptorAttrs(cgf, builder, call, "cir.ubsan.dst", dstType);
  call->setDiscardableAttr(
      "cir.ubsan.implicit_conversion_check_kind",
      mlir::IntegerAttr::get(mlir::IntegerType::get(builder.getContext(), 8),
                             checkKind));
  call->setDiscardableAttr("cir.ubsan.handler_kind",
                           builder.getStringAttr(handlerKind));
  call->setDiscardableAttr("cir.ubsan.handler_name",
                           builder.getStringAttr("implicit_conversion"));
  call->setDiscardableAttr("cir.nosanitize",
                           mlir::UnitAttr::get(builder.getContext()));
}

static void emitImplicitConversionTrap(CIRGenFunction &cgf,
                                       CIRGenBuilderTy &builder,
                                       mlir::Location loc) {
  // Matches SanitizerHandler::ImplicitConversion from
  // clang/lib/CodeGen/SanitizerHandler.h.
  constexpr uint8_t trapHandlerID = 7;

  auto trapOp = cir::TrapOp::create(builder, loc);
  trapOp->setAttr("cir.ubsantrap_handler",
                  mlir::IntegerAttr::get(
                      mlir::IntegerType::get(builder.getContext(), 8),
                      trapHandlerID));
  markNoSanitize(trapOp);

  const std::string &trapFuncName = cgf.cgm.getCodeGenOpts().TrapFuncName;
  if (!trapFuncName.empty())
    trapOp->setAttr("cir.trap_func_name",
                    cgf.cgm.getStringAttrCached(trapFuncName));
}

static void emitImplicitConversionSanitizerCheck(
    CIRGenFunction &cgf, CIRGenBuilderTy &builder, mlir::Location loc,
    SourceLocation sourceLoc, QualType srcType, QualType dstType,
    mlir::Value src, mlir::Value dst, ImplicitConversionCheckKind checkKind,
    llvm::ArrayRef<ImplicitConversionCheck> checks) {
  if (checks.empty())
    return;

  auto combineChecks = [&](mlir::Value lhsCheck,
                           mlir::Value rhsCheck) -> mlir::Value {
    if (!lhsCheck)
      return rhsCheck;
    if (!rhsCheck)
      return lhsCheck;
    return buildAndNoSanitize(builder, loc, lhsCheck, rhsCheck);
  };

  mlir::Value trapPass;
  mlir::Value fatalPass;
  mlir::Value recoverPass;

  for (auto [check, ordinal] : checks) {
    bool trap = cgf.cgm.getCodeGenOpts().SanitizeTrap.has(ordinal);
    bool recover =
        !trap && cgf.cgm.getCodeGenOpts().SanitizeRecover.has(ordinal);

    if (trap) {
      trapPass = combineChecks(trapPass, check);
      continue;
    }

    if (recover)
      recoverPass = combineChecks(recoverPass, check);
    else
      fatalPass = combineChecks(fatalPass, check);
  }

  if (trapPass) {
    auto trapIf = cir::IfOp::create(
        builder, loc, trapPass, /*withElseRegion=*/true,
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          builder.createYield(ifLoc);
        },
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          emitImplicitConversionTrap(cgf, builder, ifLoc);
        });
    setSanitizerBranchAttrs(builder, trapIf);
  }

  auto emitRuntimeCheck = [&](mlir::Value pass, llvm::StringRef handlerKind) {
    if (!pass)
      return;

    auto handlerIf = cir::IfOp::create(
        builder, loc, pass, /*withElseRegion=*/true,
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          builder.createYield(ifLoc);
        },
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          emitImplicitConversionStubCall(cgf, builder, ifLoc, sourceLoc,
                                         srcType, dstType, src, dst, checkKind,
                                         handlerKind);
          if (handlerKind == "abort") {
            auto unreachable = cir::UnreachableOp::create(builder, ifLoc);
            markNoSanitize(unreachable);
          } else {
            builder.createYield(ifLoc);
          }
        });
    setSanitizerBranchAttrs(builder, handlerIf);
  };

  emitRuntimeCheck(fatalPass, "abort");
  emitRuntimeCheck(recoverPass, "recover");
}

static void emitIntegerTruncationSanitizerCheck(
    CIRGenFunction &cgf, CIRGenBuilderTy &builder, mlir::Location loc,
    SourceLocation sourceLoc, mlir::Value src, QualType srcType, mlir::Value dst,
    QualType dstType) {
  if (!cgf.sanOpts.hasOneOf(SanitizerKind::ImplicitIntegerTruncation))
    return;

  if (!isPotentiallyEligibleForImplicitIntegerConversionCheck(srcType, dstType))
    return;

  // A conversion to/from bool is never a truncation/sign-change, and bool is a
  // cir::BoolType, not a cir::IntType; bail before the cast (matches classic).
  if (srcType->isBooleanType() || dstType->isBooleanType())
    return;

  auto srcIntTy = mlir::cast<cir::IntType>(src.getType());
  auto dstIntTy = mlir::cast<cir::IntType>(dst.getType());
  if (srcIntTy.getWidth() <= dstIntTy.getWidth())
    return;

  bool srcSigned = srcType->isSignedIntegerOrEnumerationType();
  bool dstSigned = dstType->isSignedIntegerOrEnumerationType();
  if (cgf.sanOpts.has(SanitizerKind::ImplicitIntegerSignChange) &&
      !srcSigned && dstSigned)
    return;

  auto check =
      emitIntegerTruncationCheckHelper(builder, loc, src, srcType, dst, dstType);
  if (!cgf.sanOpts.has(check.second.second))
    return;

  if (check.first == ICCK_UnsignedIntegerTruncation) {
    if (cgf.getContext().isTypeIgnoredBySanitizer(
            SanitizerKind::ImplicitUnsignedIntegerTruncation, dstType))
      return;
  } else if (cgf.getContext().isTypeIgnoredBySanitizer(
                 SanitizerKind::ImplicitSignedIntegerTruncation, dstType)) {
    return;
  }

  emitImplicitConversionSanitizerCheck(
      cgf, builder, loc, sourceLoc, srcType, dstType, src, dst, check.first,
      llvm::ArrayRef<ImplicitConversionCheck>(check.second));
}

static void emitIntegerSignChangeSanitizerCheck(
    CIRGenFunction &cgf, CIRGenBuilderTy &builder, mlir::Location loc,
    SourceLocation sourceLoc, mlir::Value src, QualType srcType, mlir::Value dst,
    QualType dstType) {
  if (!cgf.sanOpts.has(SanitizerKind::ImplicitIntegerSignChange))
    return;

  if (!isPotentiallyEligibleForImplicitIntegerConversionCheck(srcType, dstType))
    return;

  // A conversion to/from bool is never a truncation/sign-change, and bool is a
  // cir::BoolType, not a cir::IntType; bail before the cast (matches classic).
  if (srcType->isBooleanType() || dstType->isBooleanType())
    return;

  auto srcIntTy = mlir::cast<cir::IntType>(src.getType());
  auto dstIntTy = mlir::cast<cir::IntType>(dst.getType());
  bool srcSigned = srcType->isSignedIntegerOrEnumerationType();
  bool dstSigned = dstType->isSignedIntegerOrEnumerationType();

  unsigned srcBits = srcIntTy.getWidth();
  unsigned dstBits = dstIntTy.getWidth();
  if (srcSigned == dstSigned && srcBits == dstBits)
    return;
  if (!srcSigned && !dstSigned)
    return;
  if (dstBits > srcBits && dstSigned)
    return;
  if (cgf.sanOpts.has(SanitizerKind::ImplicitSignedIntegerTruncation) &&
      srcBits > dstBits && srcSigned)
    return;

  if (dstSigned &&
      cgf.getContext().isTypeIgnoredBySanitizer(
          SanitizerKind::ImplicitSignedIntegerTruncation, dstType))
    return;
  if (!dstSigned &&
      cgf.getContext().isTypeIgnoredBySanitizer(
          SanitizerKind::ImplicitUnsignedIntegerTruncation, dstType))
    return;

  auto signCheck =
      emitIntegerSignChangeCheckHelper(builder, loc, src, srcType, dst, dstType);
  ImplicitConversionCheckKind checkKind = signCheck.first;
  llvm::SmallVector<ImplicitConversionCheck, 2> checks = {signCheck.second};

  if (cgf.sanOpts.has(SanitizerKind::ImplicitSignedIntegerTruncation) &&
      srcBits > dstBits && !srcSigned && dstSigned) {
    auto truncationCheck = emitIntegerTruncationCheckHelper(
        builder, loc, src, srcType, dst, dstType);
    checkKind = ICCK_SignedIntegerTruncationOrSignChange;
    checks.push_back(truncationCheck.second);
  }

  emitImplicitConversionSanitizerCheck(cgf, builder, loc, sourceLoc, srcType,
                                       dstType, src, dst, checkKind, checks);
}

// Emit UBSan implicit-bitfield-conversion check.
// Matches OGCG CodeGenFunction::EmitBitfieldConversionCheck.
static void emitBitfieldConversionCheckImpl(CIRGenFunction &cgf,
                                            CIRGenBuilderTy &builder,
                                            mlir::Location loc,
                                            SourceLocation sourceLoc,
                                            mlir::Value src, QualType srcType,
                                            mlir::Value dst, QualType dstType,
                                            const CIRGenBitFieldInfo &info) {
  if (!cgf.sanOpts.has(SanitizerKind::ImplicitBitfieldConversion))
    return;

  if (!isPotentiallyEligibleForImplicitIntegerConversionCheck(srcType, dstType))
    return;

  if (dstType->isBooleanType() || srcType->isBooleanType())
    return;

  unsigned srcBits =
      mlir::cast<cir::IntType>(
          cgf.convertType(srcType))
          .getWidth();
  unsigned dstBits = info.size;

  bool srcSigned = srcType->isSignedIntegerOrEnumerationType();
  bool dstSigned = dstType->isSignedIntegerOrEnumerationType();

  bool emitTruncation = dstBits < srcBits;
  bool emitTruncationFromUnsignedToSigned =
      emitTruncation && dstSigned && !srcSigned;

  bool sameTypeSameSize = srcSigned == dstSigned && srcBits == dstBits;
  bool bothUnsigned = !srcSigned && !dstSigned;
  bool largerSigned = (dstBits > srcBits) && dstSigned;
  bool emitSignChange = !sameTypeSameSize && !bothUnsigned && !largerSigned;

  if (!emitTruncation && !emitSignChange)
    return;

  ImplicitConversionCheckKind checkKind;
  ImplicitConversionCheck check;

  if (emitTruncation) {
    // Truncation check: extend dst back to src width and compare.
    bool checkSrcSigned = srcSigned;
    (void)checkSrcSigned;
    ImplicitConversionCheckKind kind;
    if (!srcSigned && !dstSigned)
      kind = ICCK_UnsignedIntegerTruncation;
    else
      kind = ICCK_SignedIntegerTruncation;

    auto srcIntTy = mlir::cast<cir::IntType>(src.getType());
    auto compareTy =
        cir::IntType::get(builder.getContext(), srcIntTy.getWidth(), dstSigned);
    mlir::Value extDst =
        castIntegerToSignedness(builder, loc, dst, dstSigned);
    if (auto *op = extDst.getDefiningOp())
      markNoSanitize(op);
    if (extDst.getType() != compareTy) {
      extDst = builder.createIntCast(extDst, compareTy);
      if (auto *op = extDst.getDefiningOp())
        markNoSanitize(op);
    }

    mlir::Value srcForCompare =
        castIntegerToSignedness(builder, loc, src, dstSigned);
    if (auto *op = srcForCompare.getDefiningOp())
      markNoSanitize(op);

    mlir::Value truncCheck = cir::CmpOp::create(
        builder, loc, cir::CmpOpKind::eq, extDst, srcForCompare);
    markNoSanitize(truncCheck.getDefiningOp());

    checkKind = kind;
    if (emitTruncationFromUnsignedToSigned)
      checkKind = ICCK_SignedIntegerTruncationOrSignChange;
    check = {truncCheck, SanitizerKind::SO_ImplicitBitfieldConversion};
  } else {
    // Sign change check.
    mlir::Value srcNeg =
        emitIsNegativeTestHelper(builder, loc, src, srcType);
    mlir::Value dstNeg =
        emitIsNegativeTestHelper(builder, loc, dst, dstType);
    mlir::Value signCheck = cir::CmpOp::create(
        builder, loc, cir::CmpOpKind::eq, srcNeg, dstNeg);
    markNoSanitize(signCheck.getDefiningOp());

    checkKind = ICCK_IntegerSignChange;
    check = {signCheck, SanitizerKind::SO_ImplicitBitfieldConversion};
  }

  emitImplicitConversionSanitizerCheck(cgf, builder, loc, sourceLoc, srcType,
                                       dstType, src, dst, checkKind, {check});
}

static mlir::Value buildAndNoSanitize(CIRGenBuilderTy &builder,
                                      mlir::Location loc, mlir::Value lhs,
                                      mlir::Value rhs) {
  auto result = builder.createAnd(loc, lhs, rhs);
  if (auto *op = result.getDefiningOp())
    markNoSanitize(op);
  return result;
}

static void emitFloatCastOverflowStubCall(CIRGenFunction &cgf,
                                          CIRGenBuilderTy &builder,
                                          mlir::Location loc,
                                          SourceLocation sourceLoc,
                                          QualType srcTy, QualType dstTy,
                                          mlir::Value value,
                                          llvm::StringRef handlerKind) {
  auto intptrTy = cgf.convertType(cgf.getContext().getUIntPtrType());
  auto stubFnTy = cir::FuncType::get({intptrTy}, builder.getVoidTy());
  auto stubFn = cgf.cgm.createRuntimeFunction(
      stubFnTy, "__cir_ubsan_float_cast_overflow", /*attrs=*/{},
      /*isLocal=*/false, /*assumeConvergent=*/false);

  mlir::Value arg = emitSanitizerValueHandle(cgf, builder, loc, value, srcTy);
  if (auto *op = arg.getDefiningOp())
    markNoSanitize(op);

  auto call = builder.createCallOp(loc, stubFn, {arg});
  setUBSanSourceLocationAttrs(cgf, call, sourceLoc);
  setUBSanTypeDescriptorAttrs(cgf, builder, call, "cir.ubsan.lhs", srcTy);
  setUBSanTypeDescriptorAttrs(cgf, builder, call, "cir.ubsan.rhs", dstTy);
  call->setDiscardableAttr("cir.ubsan.handler_kind",
                           builder.getStringAttr(handlerKind));
  call->setDiscardableAttr("cir.ubsan.handler_name",
                           builder.getStringAttr("float_cast_overflow"));
  call->setDiscardableAttr("cir.nosanitize",
                           mlir::UnitAttr::get(builder.getContext()));
}

static void emitFloatConversionCheck(CIRGenFunction &cgf,
                                     CIRGenBuilderTy &builder,
                                     mlir::Value origSrc,
                                     QualType origSrcType, mlir::Value src,
                                     QualType srcType, QualType dstType,
                                     SourceLocation sourceLoc) {
  assert(origSrcType->isFloatingType() &&
         "float-cast-overflow requires floating-point source type");
  if ((!dstType->isIntegerType() && !dstType->isEnumeralType()) ||
      dstType->isBooleanType())
    return;

  using llvm::APFloat;
  using llvm::APSInt;

  const llvm::fltSemantics &srcSema =
      cgf.getContext().getFloatTypeSemantics(origSrcType);
  unsigned width = cgf.getContext().getIntWidth(dstType);
  bool isUnsigned = dstType->isUnsignedIntegerOrEnumerationType();

  APSInt min = APSInt::getMinValue(width, isUnsigned);
  APFloat minSrc(srcSema, APFloat::uninitialized);
  if (minSrc.convertFromAPInt(min, !isUnsigned, APFloat::rmTowardZero) &
      APFloat::opOverflow)
    minSrc = APFloat::getInf(srcSema, true);
  else
    minSrc.subtract(APFloat(srcSema, 1), APFloat::rmTowardNegative);

  APSInt max = APSInt::getMaxValue(width, isUnsigned);
  APFloat maxSrc(srcSema, APFloat::uninitialized);
  if (maxSrc.convertFromAPInt(max, !isUnsigned, APFloat::rmTowardZero) &
      APFloat::opOverflow)
    maxSrc = APFloat::getInf(srcSema, false);
  else
    maxSrc.add(APFloat(srcSema, 1), APFloat::rmTowardPositive);

  if (origSrcType->isHalfType()) {
    const llvm::fltSemantics &sema =
        cgf.getContext().getFloatTypeSemantics(srcType);
    bool isInexact;
    minSrc.convert(sema, APFloat::rmTowardZero, &isInexact);
    maxSrc.convert(sema, APFloat::rmTowardZero, &isInexact);
  }

  mlir::Location loc = cgf.getLoc(sourceLoc);
  auto minBound = builder.getConstFP(loc, src.getType(), minSrc);
  auto maxBound = builder.getConstFP(loc, src.getType(), maxSrc);
  mlir::Value ge = cir::CmpOp::create(builder, loc, cir::CmpOpKind::gt, src,
                                      minBound.getResult());
  mlir::Value le = cir::CmpOp::create(builder, loc, cir::CmpOpKind::lt, src,
                                      maxBound.getResult());
  markNoSanitize(ge.getDefiningOp());
  markNoSanitize(le.getDefiningOp());
  mlir::Value check = buildAndNoSanitize(builder, loc, ge, le);

  if (cgf.cgm.getCodeGenOpts().SanitizeTrap.has(
          SanitizerKind::FloatCastOverflow)) {
    auto trapIf = cir::IfOp::create(
        builder, loc, check, /*withElseRegion=*/true,
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          builder.createYield(ifLoc);
        },
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          auto trap = cir::TrapOp::create(builder, ifLoc);
          trap->setAttr("cir.ubsantrap_handler",
                        mlir::IntegerAttr::get(
                            mlir::IntegerType::get(builder.getContext(), 8),
                            5));
          markNoSanitize(trap);
        });
    setSanitizerBranchAttrs(builder, trapIf);
    return;
  }

  StringRef handlerKind =
      cgf.cgm.getCodeGenOpts().SanitizeRecover.has(
          SanitizerKind::FloatCastOverflow)
          ? "recover"
          : "abort";
  auto ifOp = cir::IfOp::create(
      builder, loc, check, /*withElseRegion=*/true,
      [&](mlir::OpBuilder &, mlir::Location ifLoc) {
        builder.createYield(ifLoc);
      },
      [&](mlir::OpBuilder &, mlir::Location ifLoc) {
        emitFloatCastOverflowStubCall(cgf, builder, ifLoc, sourceLoc,
                                      origSrcType, dstType, origSrc,
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

static mlir::Value getMaximumShiftAmount(CIRGenBuilderTy &builder,
                                         mlir::Location loc, mlir::Value lhs,
                                         mlir::Value rhs, bool rhsIsSigned) {
  auto lhsTy = mlir::cast<cir::IntType>(lhs.getType());
  auto rhsTy = mlir::cast<cir::IntType>(rhs.getType());

  llvm::APInt rhsMax =
      rhsIsSigned ? llvm::APInt::getSignedMaxValue(rhsTy.getWidth())
                  : llvm::APInt::getMaxValue(rhsTy.getWidth());

  // For a given type of LHS the maximum shift amount is width(LHS)-1, however
  // it can occur that width(LHS)-1 > range(RHS). Since there is no check for
  // this in APInt's constructor, this results in an assertion failure.
  // Constrain the return value to be max(RHS) in this case.
  if (rhsMax.ult(lhsTy.getWidth()))
    return builder.getConstInt(loc, rhsMax, rhsTy.isUnsigned());

  llvm::APInt maxShift(rhsTy.getWidth(), lhsTy.getWidth() - 1);
  return builder.getConstInt(loc, maxShift, rhsTy.isUnsigned());
}

static void emitShiftOutOfBoundsStubCall(CIRGenFunction &cgf,
                                         CIRGenBuilderTy &builder,
                                         mlir::Location loc,
                                         SourceLocation sourceLoc,
                                         QualType lhsTy, QualType rhsTy,
                                         mlir::Value lhs, mlir::Value rhs,
                                         llvm::StringRef handlerKind) {
  auto intptrTy = cgf.convertType(cgf.getContext().getUIntPtrType());
  auto stubFnTy = cir::FuncType::get({intptrTy, intptrTy}, builder.getVoidTy());
  llvm::StringRef stubName =
      handlerKind == "abort" ? "__cir_ubsan_shift_out_of_bounds_abort"
                              : "__cir_ubsan_shift_out_of_bounds";
  auto stubFn = cgf.cgm.createRuntimeFunction(
      stubFnTy, stubName, /*attrs=*/{},
      /*isLocal=*/false, /*assumeConvergent=*/false);

  mlir::Value lhsArg = emitSanitizerValueHandle(cgf, builder, loc, lhs, lhsTy);
  mlir::Value rhsArg = emitSanitizerValueHandle(cgf, builder, loc, rhs, rhsTy);
  if (auto *op = lhsArg.getDefiningOp())
    markNoSanitize(op);
  if (auto *op = rhsArg.getDefiningOp())
    markNoSanitize(op);

  auto call = builder.createCallOp(loc, stubFn, {lhsArg, rhsArg});

  PresumedLoc presumedLoc =
      cgf.getContext().getSourceManager().getPresumedLoc(sourceLoc);
  if (presumedLoc.isValid()) {
    call->setDiscardableAttr("cir.ubsan.source_file",
                             builder.getStringAttr(presumedLoc.getFilename()));
    call->setDiscardableAttr("cir.ubsan.source_line",
                             builder.getI32IntegerAttr(presumedLoc.getLine()));
    call->setDiscardableAttr("cir.ubsan.source_column",
                             builder.getI32IntegerAttr(presumedLoc.getColumn()));
  }

  setUBSanTypeDescriptorAttrs(cgf, builder, call, "cir.ubsan.lhs", lhsTy);
  setUBSanTypeDescriptorAttrs(cgf, builder, call, "cir.ubsan.rhs", rhsTy);
  call->setDiscardableAttr("cir.ubsan.handler_kind",
                           builder.getStringAttr(handlerKind));
  call->setDiscardableAttr("cir.ubsan.handler_name",
                           builder.getStringAttr("shift_out_of_bounds"));
  call->setDiscardableAttr("cir.nosanitize",
                           mlir::UnitAttr::get(builder.getContext()));
}

static void emitShiftTrap(CIRGenBuilderTy &builder, mlir::Location loc) {
  auto trap = cir::TrapOp::create(builder, loc);
  trap->setAttr("cir.ubsantrap_handler",
                mlir::IntegerAttr::get(
                    mlir::IntegerType::get(builder.getContext(), 8), 20));
  trap->setAttr("cir.nosanitize", mlir::UnitAttr::get(builder.getContext()));
}

class ScalarExprEmitter : public StmtVisitor<ScalarExprEmitter, mlir::Value> {
  CIRGenFunction &cgf;
  CIRGenBuilderTy &builder;
  // Unlike classic codegen we set this to false or use std::exchange to read
  // the value instead of calling TestAndClearIgnoreResultAssign to make it
  // explicit when the value is used
  bool ignoreResultAssign;

public:
  ScalarExprEmitter(CIRGenFunction &cgf, CIRGenBuilderTy &builder,
                    bool ignoreResultAssign = false)
      : cgf(cgf), builder(builder), ignoreResultAssign(ignoreResultAssign) {}

  //===--------------------------------------------------------------------===//
  //                               Utilities
  //===--------------------------------------------------------------------===//
  mlir::Type convertType(QualType ty) { return cgf.convertType(ty); }

  mlir::Value emitComplexToScalarConversion(mlir::Location loc,
                                            mlir::Value value, CastKind kind,
                                            QualType destTy);

  mlir::Value emitNullValue(QualType ty, mlir::Location loc) {
    return cgf.cgm.emitNullConstant(ty, loc);
  }

  mlir::Value emitPromotedValue(mlir::Value result, QualType promotionType) {
    return builder.createFloatingCast(result, cgf.convertType(promotionType));
  }

  mlir::Value emitUnPromotedValue(mlir::Value result, QualType exprType) {
    return builder.createFloatingCast(result, cgf.convertType(exprType));
  }

  mlir::Value emitPromoted(const Expr *e, QualType promotionType);

  mlir::Value maybePromoteBoolResult(mlir::Value value,
                                     mlir::Type dstTy) const {
    if (mlir::isa<cir::IntType>(dstTy))
      return builder.createBoolToInt(value, dstTy);
    if (mlir::isa<cir::BoolType>(dstTy))
      return value;
    llvm_unreachable("Can only promote integer or boolean types");
  }

  bool shouldInstrumentProfile() const {
    return cgf.cgm.getCodeGenOpts().hasProfileClangInstr();
  }

  void maybeIncrementProfileCounter(const Stmt *stmt) {
    if (shouldInstrumentProfile())
      cgf.incrementProfileCounter(stmt);
  }

  void maybeIncrementConditionCounter(const Expr *condExpr,
                                      mlir::Value condValue, bool onTrue,
                                      mlir::Location loc) {
    if (!shouldInstrumentProfile() ||
        !CIRGenFunction::isInstrumentedCondition(condExpr))
      return;

    mlir::Value counterCond =
        onTrue ? condValue : builder.createNot(condValue);
    cir::IfOp::create(builder, loc, counterCond, /*withElseRegion=*/false,
                      [&](mlir::OpBuilder &b, mlir::Location ifLoc) {
                        cgf.incrementProfileCounter(condExpr);
                        cir::YieldOp::create(b, ifLoc);
                      });
  }

  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//

  mlir::Value Visit(Expr *e) {
    return StmtVisitor<ScalarExprEmitter, mlir::Value>::Visit(e);
  }

  mlir::Value VisitStmt(Stmt *s) {
    llvm_unreachable("Statement passed to ScalarExprEmitter");
  }

  mlir::Value VisitExpr(Expr *e) {
    cgf.getCIRGenModule().errorNYI(
        e->getSourceRange(), "scalar expression kind: ", e->getStmtClassName());
    return {};
  }

  mlir::Value VisitConstantExpr(ConstantExpr *e) {
    // A constant expression of type 'void' generates no code and produces no
    // value.
    if (e->getType()->isVoidType())
      return {};

    if (mlir::Attribute result = ConstantEmitter(cgf).tryEmitConstantExpr(e)) {
      if (e->isGLValue()) {
        // The constant evaluator may have already converted this to an rvalue.
        if (e->hasAPValueResult() && !e->getAPValueResult().isLValue())
          return builder.getConstant(cgf.getLoc(e->getSourceRange()),
                                     mlir::cast<mlir::TypedAttr>(result));
        // Otherwise it's an address — load from it.
        mlir::Value addr = builder.getConstant(
            cgf.getLoc(e->getSourceRange()),
            mlir::cast<mlir::TypedAttr>(result));
        mlir::Type loadTy = convertType(e->getType());
        CharUnits align =
            cgf.getContext().getTypeAlignInChars(e->getType());
        return cgf.emitLoadOfScalar(
            Address(addr, loadTy, align),
            /*isVolatile=*/false, e->getType(), e->getExprLoc(),
            LValueBaseInfo(AlignmentSource::Decl), TBAAAccessInfo());
      }

      return builder.getConstant(cgf.getLoc(e->getSourceRange()),
                                 mlir::cast<mlir::TypedAttr>(result));
    }

    // If constant emission failed, fall through to visit the subexpression.
    return Visit(e->getSubExpr());
  }

  mlir::Value VisitPackIndexingExpr(PackIndexingExpr *e) {
    return Visit(e->getSelectedExpr());
  }

  mlir::Value VisitParenExpr(ParenExpr *pe) { return Visit(pe->getSubExpr()); }

  mlir::Value VisitGenericSelectionExpr(GenericSelectionExpr *ge) {
    return Visit(ge->getResultExpr());
  }

  void emitLValueAlignmentAssumption(const Expr *e, mlir::Value value,
                                     bool keepAsAssumeForCallResult = false) {
    const AlignValueAttr *alignValueAttr = nullptr;
    if (const auto *declRefExpr = dyn_cast<DeclRefExpr>(e)) {
      const ValueDecl *valueDecl = declRefExpr->getDecl();
      if (valueDecl->getType()->isReferenceType()) {
        if (const auto *typedefTy = valueDecl->getType()
                                        .getNonReferenceType()
                                        ->getAs<TypedefType>())
          alignValueAttr = typedefTy->getDecl()->getAttr<AlignValueAttr>();
      } else {
        // For non-reference parameters, OGCG emits alignment assumptions in the
        // function prologue (or prefers assumptions only when alignment
        // sanitizer is enabled). CIR doesn't support sanitizers yet, so avoid
        // duplicating assumptions for plain parameter loads.
        if (isa<ParmVarDecl>(valueDecl))
          return;
        alignValueAttr = valueDecl->getAttr<AlignValueAttr>();
      }
    }

    if (!alignValueAttr)
      if (const auto *typedefTy = e->getType()->getAs<TypedefType>())
        alignValueAttr = typedefTy->getDecl()->getAttr<AlignValueAttr>();

    if (!alignValueAttr)
      return;

    llvm::APSInt alignValue =
        alignValueAttr->getAlignment()->EvaluateKnownConstInt(
            cgf.getContext());
    uint64_t alignment =
        alignValue.getLimitedValue(llvm::Value::MaximumAlignment);
    if (alignment == 0)
      return;
    mlir::Value assumedValue =
        cgf.emitAlignmentAssumption(value, e, alignValueAttr->getLocation(),
                                    static_cast<int64_t>(alignment));
    if (!keepAsAssumeForCallResult)
      return;

    if (auto assume =
            assumedValue.getDefiningOp<cir::AssumeAlignedOp>()) {
      // Match OGCG's align_value expression path: keep this as an explicit
      // llvm.assume instead of folding it into a call-site return attribute.
      assume->setDiscardableAttr("cir.keep_llvm_assume",
                                 mlir::UnitAttr::get(builder.getContext()));
    }
  }

  LValue emitCheckedLValue(const Expr *e, CIRGenFunction::TypeCheckKind tck) {
    return cgf.emitCheckedLValue(e, tck);
  }

  /// Emits the address of the l-value, then loads and returns the result.
  mlir::Value emitLoadOfLValue(const Expr *e) {
    LValue lv = emitCheckedLValue(e, CIRGenFunction::TCK_Load);
    mlir::Value loadValue = cgf.emitLoadOfLValue(lv, e->getExprLoc()).getValue();
    emitLValueAlignmentAssumption(e, loadValue);
    return loadValue;
  }

  mlir::Value VisitCoawaitExpr(CoawaitExpr *s) {
    return cgf.emitCoawaitExpr(*s).getValue();
  }

  mlir::Value VisitCoyieldExpr(CoyieldExpr *e) {
    return cgf.emitCoyieldExpr(*e).getValue();
  }

  mlir::Value VisitUnaryCoawait(const UnaryOperator *e) {
    return Visit(e->getSubExpr());
  }

  mlir::Value emitLoadOfLValue(LValue lv, SourceLocation loc) {
    return cgf.emitLoadOfLValue(lv, loc).getValue();
  }

  // l-values
  mlir::Value VisitDeclRefExpr(DeclRefExpr *e) {
    if (CIRGenFunction::ConstantEmission constant = cgf.tryEmitAsConstant(e))
      return cgf.emitScalarConstant(constant, e);

    return emitLoadOfLValue(e);
  }

  mlir::Value VisitAddrLabelExpr(const AddrLabelExpr *e) {
    auto func = cast<cir::FuncOp>(cgf.curFn);
    auto blockAddrLabelName = cgf.cgm.getBlockAddressLabelName(*e->getLabel());
    cir::BlockAddrInfoAttr blockInfoAttr = cir::BlockAddrInfoAttr::get(
        &cgf.getMLIRContext(), func.getSymName(), blockAddrLabelName);
    cir::BlockAddressOp blockAddressOp = cir::BlockAddressOp::create(
        builder, cgf.getLoc(e->getSourceRange()), cgf.convertType(e->getType()),
        blockInfoAttr);
    cir::LabelOp resolvedLabel = cgf.cgm.lookupBlockAddressInfo(blockInfoAttr);
    if (!resolvedLabel) {
      cgf.cgm.mapUnresolvedBlockAddress(blockAddressOp);
      // Still add the op to maintain insertion order it will be resolved in
      // resolveBlockAddresses
      cgf.cgm.mapResolvedBlockAddress(blockAddressOp, nullptr);
    } else {
      cgf.cgm.mapResolvedBlockAddress(blockAddressOp, resolvedLabel);
    }
    cgf.instantiateIndirectGotoBlock();
    return blockAddressOp;
  }

  mlir::Value VisitIntegerLiteral(const IntegerLiteral *e) {
    mlir::Type type = cgf.convertType(e->getType());
    return cir::ConstantOp::create(builder, cgf.getLoc(e->getExprLoc()),
                                   cir::IntAttr::get(type, e->getValue()));
  }

  mlir::Value VisitFixedPointLiteral(const FixedPointLiteral *e) {
    return builder.getConstAPInt(cgf.getLoc(e->getExprLoc()),
                                 convertType(e->getType()), e->getValue());
  }

  mlir::Value VisitFloatingLiteral(const FloatingLiteral *e) {
    mlir::Type type = cgf.convertType(e->getType());
    assert(mlir::isa<cir::FPTypeInterface>(type) &&
           "expect floating-point type");
    return cir::ConstantOp::create(builder, cgf.getLoc(e->getExprLoc()),
                                   cir::FPAttr::get(type, e->getValue()));
  }

  mlir::Value VisitCharacterLiteral(const CharacterLiteral *e) {
    mlir::Type ty = cgf.convertType(e->getType());
    auto init = cir::IntAttr::get(ty, e->getValue());
    return cir::ConstantOp::create(builder, cgf.getLoc(e->getExprLoc()), init);
  }

  mlir::Value VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr *e) {
    return builder.getBool(e->getValue(), cgf.getLoc(e->getExprLoc()));
  }

  mlir::Value VisitCXXScalarValueInitExpr(const CXXScalarValueInitExpr *e) {
    if (e->getType()->isVoidType())
      return {};

    return emitNullValue(e->getType(), cgf.getLoc(e->getSourceRange()));
  }

  mlir::Value VisitGNUNullExpr(const GNUNullExpr *e) {
    return emitNullValue(e->getType(), cgf.getLoc(e->getSourceRange()));
  }

  mlir::Value VisitOffsetOfExpr(OffsetOfExpr *e);

  mlir::Value VisitSizeOfPackExpr(SizeOfPackExpr *e) {
    // sizeof... returns size_t
    mlir::Type sizeTy =
        cgf.cgm.getTypes().convertType(cgf.getContext().getSizeType());
    return builder.getConstInt(cgf.getLoc(e->getSourceRange()), sizeTy,
                               e->getPackLength());
  }
  mlir::Value VisitPseudoObjectExpr(PseudoObjectExpr *e) {
    return cgf.emitPseudoObjectRValue(e).getValue();
  }
  mlir::Value VisitSYCLUniqueStableNameExpr(SYCLUniqueStableNameExpr *e) {
    cgf.cgm.errorNYI(e->getSourceRange(),
                     "ScalarExprEmitter: sycl unique stable name");
    return {};
  }
  mlir::Value VisitEmbedExpr(EmbedExpr *e) {
    assert(e->getDataElementCount() == 1);
    auto it = e->begin();
    llvm::APInt value = (*it)->getValue();
    return builder.getConstInt(cgf.getLoc(e->getExprLoc()), value,
                               e->getType()->isUnsignedIntegerType());
  }
  mlir::Value VisitOpaqueValueExpr(OpaqueValueExpr *e) {
    if (e->isGLValue())
      return emitLoadOfLValue(cgf.getOrCreateOpaqueLValueMapping(e),
                              e->getExprLoc());

    // Otherwise, assume the mapping is the scalar directly.
    return cgf.getOrCreateOpaqueRValueMapping(e).getValue();
  }

  mlir::Value VisitObjCSelectorExpr(ObjCSelectorExpr *e) {
    mlir::Location loc = cgf.getLoc(e->getSourceRange());
    return cgf.cgm.getObjCSelector(cgf, e->getSelector(), loc);
  }
  mlir::Value VisitObjCProtocolExpr(ObjCProtocolExpr *e) {
    const ObjCRuntime runtime = cgf.getLangOpts().ObjCRuntime;
    bool useDarwinProtocolRuntime =
        runtime.isNeXTFamily() && runtime.isNonFragile();
    bool useGNUProtocolRuntime = runtime.getKind() == ObjCRuntime::GCC ||
                                 runtime.getKind() == ObjCRuntime::GNUstep ||
                                 runtime.getKind() == ObjCRuntime::ObjFW;
    if (!useDarwinProtocolRuntime && !useGNUProtocolRuntime) {
      cgf.cgm.errorNYI(e->getSourceRange(), "ScalarExprEmitter: objc protocol");
      return {};
    }

    mlir::Location loc = cgf.getLoc(e->getSourceRange());
    mlir::Type exprTy = cgf.convertType(e->getType());
    mlir::Value protocol = cgf.cgm.getObjCProtocol(cgf, e->getProtocol(), loc);
    if (protocol.getType() != exprTy)
      protocol = builder.createBitcast(loc, protocol, exprTy);
    return protocol;
  }
  mlir::Value VisitObjCIvarRefExpr(ObjCIvarRefExpr *e) {
    return emitLoadOfLValue(e);
  }
  mlir::Value VisitObjCMessageExpr(ObjCMessageExpr *e) {
    return cgf.emitObjCMessageExpr(e).getValue();
  }
  mlir::Value VisitObjCIsaExpr(ObjCIsaExpr *e) {
    return emitLoadOfLValue(cgf.emitObjCIsaExpr(e), e->getExprLoc());
  }
  mlir::Value VisitObjCAvailabilityCheckExpr(ObjCAvailabilityCheckExpr *e) {
    VersionTuple version = e->getVersion();
    auto loc = cgf.getLoc(e->getSourceRange());

    // If we're checking for a platform older than our minimum deployment
    // target, we can fold the check away.
    if (version <= cgf.cgm.getTarget().getPlatformMinVersion()) {
      auto boolTy = builder.getBoolTy();
      return cir::ConstantOp::create(
          builder, loc, boolTy,
          cir::BoolAttr::get(builder.getContext(), boolTy, true));
    }

    return cgf.emitBuiltinAvailable(loc, version);
  }

  mlir::Value VisitMatrixSingleSubscriptExpr(MatrixSingleSubscriptExpr *e) {
    ignoreResultAssign = false;

    const auto *matrixTy = e->getBase()->getType()->castAs<ConstantMatrixType>();
    mlir::Value rowIdx = cgf.emitMatrixIndexExpr(e->getRowIdx());
    mlir::Value matrix = Visit(e->getBase());
    auto resultTy = mlir::cast<cir::VectorType>(cgf.convertType(e->getType()));
    SmallVector<mlir::Value, 8> rowElements;
    rowElements.reserve(matrixTy->getNumColumns());
    const bool isRowMajor =
        cgf.getLangOpts().getDefaultMatrixMemoryLayout() ==
        LangOptions::MatrixMemoryLayout::MatrixRowMajor;
    mlir::Location loc = cgf.getLoc(e->getSourceRange());

    for (unsigned col = 0; col < matrixTy->getNumColumns(); ++col) {
      mlir::Value colIdx = builder.getConstInt(loc, cgf.ptrDiffTy, col);
      mlir::Value stride = builder.getConstInt(
          loc, cgf.ptrDiffTy,
          isRowMajor ? matrixTy->getNumColumns() : matrixTy->getNumRows());
      mlir::Value scaled = cir::BinOp::create(
          builder, loc, cir::BinOpKind::Mul, isRowMajor ? rowIdx : colIdx,
          stride, /*no_unsigned_wrap=*/false, /*no_signed_wrap=*/false,
          /*saturated=*/false, cir::FastMathFlags::none,
          cir::FPExceptionBehaviorAttr{}, cir::RoundingModeAttr{});
      mlir::Value finalIdx = cir::BinOp::create(
          builder, loc, cir::BinOpKind::Add, scaled,
          isRowMajor ? colIdx : rowIdx, /*no_unsigned_wrap=*/false,
          /*no_signed_wrap=*/false, /*saturated=*/false,
          cir::FastMathFlags::none, cir::FPExceptionBehaviorAttr{},
          cir::RoundingModeAttr{});
      rowElements.push_back(
          cir::VecExtractOp::create(builder, loc, matrix, finalIdx));
    }

    return cir::VecCreateOp::create(builder, loc, resultTy, rowElements);
  }

  mlir::Value VisitMatrixSubscriptExpr(MatrixSubscriptExpr *e) {
    ignoreResultAssign = false;
    return cgf.emitLoadOfLValue(cgf.emitMatrixSubscriptExpr(e), e->getExprLoc())
        .getValue();
  }

  mlir::Value VisitCastExpr(CastExpr *e);
  mlir::Value VisitCallExpr(const CallExpr *e);

  mlir::Value VisitStmtExpr(StmtExpr *e) {
    CIRGenFunction::StmtExprEvaluation eval(cgf);
    if (e->getType()->isVoidType()) {
      (void)cgf.emitCompoundStmt(*e->getSubStmt());
      return {};
    }

    Address retAlloca =
        cgf.createMemTemp(e->getType(), cgf.getLoc(e->getSourceRange()));
    (void)cgf.emitCompoundStmt(*e->getSubStmt(), &retAlloca);

    return cgf.emitLoadOfScalar(cgf.makeAddrLValue(retAlloca, e->getType()),
                                e->getExprLoc());
  }

  mlir::Value VisitArraySubscriptExpr(ArraySubscriptExpr *e) {
    ignoreResultAssign = false;

    if (e->getBase()->getType()->isVectorType()) {
      const mlir::Location loc = cgf.getLoc(e->getSourceRange());
      const mlir::Value vecValue = Visit(e->getBase());
      const mlir::Value indexValue = Visit(e->getIdx());
      if (cgf.sanOpts.has(SanitizerKind::ArrayBounds))
        cgf.emitBoundsCheck(e, e->getBase(), indexValue, e->getIdx()->getType(),
                            /*Accessed=*/true);
      return cir::VecExtractOp::create(cgf.builder, loc, vecValue, indexValue);
    }
    // Just load the lvalue formed by the subscript expression.
    return emitLoadOfLValue(e);
  }

  mlir::Value VisitShuffleVectorExpr(ShuffleVectorExpr *e) {
    if (e->getNumSubExprs() == 2) {
      // The undocumented form of __builtin_shufflevector.
      mlir::Value inputVec = Visit(e->getExpr(0));
      mlir::Value indexVec = Visit(e->getExpr(1));
      return cir::VecShuffleDynamicOp::create(
          cgf.builder, cgf.getLoc(e->getSourceRange()), inputVec, indexVec);
    }

    mlir::Value vec1 = Visit(e->getExpr(0));
    mlir::Value vec2 = Visit(e->getExpr(1));

    // The documented form of __builtin_shufflevector, where the indices are
    // a variable number of integer constants. The constants will be stored
    // in an ArrayAttr.
    SmallVector<mlir::Attribute, 8> indices;
    for (unsigned i = 2; i < e->getNumSubExprs(); ++i) {
      indices.push_back(
          cir::IntAttr::get(cgf.builder.getSInt64Ty(),
                            e->getExpr(i)
                                ->EvaluateKnownConstInt(cgf.getContext())
                                .getSExtValue()));
    }

    return cir::VecShuffleOp::create(cgf.builder,
                                     cgf.getLoc(e->getSourceRange()),
                                     cgf.convertType(e->getType()), vec1, vec2,
                                     cgf.builder.getArrayAttr(indices));
  }

  mlir::Value VisitConvertVectorExpr(ConvertVectorExpr *e) {
    // __builtin_convertvector is an element-wise cast, and is implemented as a
    // regular cast. The back end handles casts of vectors correctly.
    // Pass FPOptions from the expression to propagate rounding mode and
    // exception behavior set by pragmas (e.g. STDC FENV_ROUND).
    FPOptions fpfeatures = e->getFPFeaturesInEffect(cgf.getLangOpts());
    return emitScalarConversion(
        Visit(e->getSrcExpr()), e->getSrcExpr()->getType(), e->getType(),
        e->getSourceRange().getBegin(), ScalarConversionOpts(),
        CIRGenFunction::getFastMathFlags(fpfeatures), fpfeatures);
  }

  mlir::Value VisitExtVectorElementExpr(Expr *e) { return emitLoadOfLValue(e); }

  mlir::Value VisitMemberExpr(MemberExpr *e);

  mlir::Value VisitCompoundLiteralExpr(CompoundLiteralExpr *e) {
    return emitLoadOfLValue(e);
  }

  mlir::Value VisitInitListExpr(InitListExpr *e);

  mlir::Value VisitArrayInitIndexExpr(ArrayInitIndexExpr *e) {
    assert(cgf.getArrayInitIndex() &&
           "ArrayInitIndexExpr not inside an ArrayInitLoopExpr?");
    return cgf.getArrayInitIndex();
  }

  mlir::Value VisitImplicitValueInitExpr(const ImplicitValueInitExpr *e) {
    return emitNullValue(e->getType(), cgf.getLoc(e->getSourceRange()));
  }

  mlir::Value VisitExplicitCastExpr(ExplicitCastExpr *e) {
    cgf.cgm.emitExplicitCastExprType(e, &cgf);
    return VisitCastExpr(e);
  }

  mlir::Value VisitCXXNullPtrLiteralExpr(CXXNullPtrLiteralExpr *e) {
    return cgf.cgm.emitNullConstant(e->getType(),
                                    cgf.getLoc(e->getSourceRange()));
  }

  /// Perform a pointer to boolean conversion.
  mlir::Value emitPointerToBoolConversion(mlir::Value v, QualType qt) {
    // TODO(cir): comparing the ptr to null is done when lowering CIR to LLVM.
    // We might want to have a separate pass for these types of conversions.
    return cgf.getBuilder().createPtrToBoolCast(v);
  }

  mlir::Value emitFloatToBoolConversion(mlir::Value src, mlir::Location loc) {
    cir::BoolType boolTy = builder.getBoolTy();
    return cir::CastOp::create(builder, loc, boolTy,
                               cir::CastKind::float_to_bool, src);
  }

  mlir::Value emitIntToBoolConversion(mlir::Value srcVal, mlir::Location loc) {
    // Because of the type rules of C, we often end up computing a
    // logical value, then zero extending it to int, then wanting it
    // as a logical value again.
    // TODO: optimize this common case here or leave it for later
    // CIR passes?
    cir::BoolType boolTy = builder.getBoolTy();
    // If the source is already a bool (e.g. from an enum with bool underlying
    // type), no cast is needed.
    if (srcVal.getType() == boolTy)
      return srcVal;
    return cir::CastOp::create(builder, loc, boolTy, cir::CastKind::int_to_bool,
                               srcVal);
  }

  // Convert a vector value to a vector<bool> by testing each element for
  // non-zero.
  mlir::Value emitVectorToBoolConversion(mlir::Value src, mlir::Location loc,
                                         FPOptions fpfeatures = FPOptions()) {
    auto vecType = mlir::cast<cir::VectorType>(src.getType());
    auto elemType = vecType.getElementType();
    uint64_t numElts = vecType.getSize();

    // Build a zero vector of the same type
    auto zeroElemAttr = builder.getZeroInitAttr(elemType);
    llvm::SmallVector<mlir::Attribute> zeroElems(numElts, zeroElemAttr);
    auto zeroVecAttr =
        cir::ConstVectorAttr::get(vecType, builder.getArrayAttr(zeroElems));
    auto zeroVec = cir::ConstantOp::create(builder, loc, vecType, zeroVecAttr);

    // Perform elementwise comparison: src != 0
    auto boolElemType = cir::BoolType::get(builder.getContext());
    auto boolVecType = cir::VectorType::get(boolElemType, numElts);
    auto cmp = cir::VecCmpOp::create(builder, loc, boolVecType,
                                     cir::CmpOpKind::ne, src, zeroVec);
    if (cir::isFPOrVectorOfFPType(src.getType()))
      cgf.maybeAttachFPConstraints(cmp.getOperation(), fpfeatures);
    return cmp;
  }

  /// Convert the specified expression value to a boolean (!cir.bool) truth
  /// value. This is equivalent to "Val != 0".
  mlir::Value emitConversionToBool(mlir::Value src, QualType srcType,
                                   mlir::Location loc,
                                   FPOptions fpfeatures = FPOptions()) {
    assert(srcType.isCanonical() && "EmitScalarConversion strips typedefs");

    if (srcType->isRealFloatingType())
      return emitFloatToBoolConversion(src, loc);

    if (llvm::isa<MemberPointerType>(srcType))
      llvm_unreachable("member pointer to bool not implemented");

    if (srcType->isIntegerType())
      return emitIntToBoolConversion(src, loc);

    // Convert vector values to vector<bool>
    if (srcType->isVectorType())
      return emitVectorToBoolConversion(src, loc, fpfeatures);

    assert(::mlir::isa<cir::PointerType>(src.getType()) &&
           "expected pointer type for pointer-to-bool conversion");
    return emitPointerToBoolConversion(src, srcType);
  }

  // Emit a conversion from the specified type to the specified destination
  // type, both of which are CIR scalar types.
  struct ScalarConversionOpts {
    bool treatBooleanAsSigned;
    bool emitImplicitIntegerTruncationChecks;
    bool emitImplicitIntegerSignChangeChecks;

    ScalarConversionOpts()
        : treatBooleanAsSigned(false),
          emitImplicitIntegerTruncationChecks(false),
          emitImplicitIntegerSignChangeChecks(false) {}

    ScalarConversionOpts(clang::SanitizerSet sanOpts)
        : treatBooleanAsSigned(false),
          emitImplicitIntegerTruncationChecks(
              sanOpts.hasOneOf(SanitizerKind::ImplicitIntegerTruncation)),
          emitImplicitIntegerSignChangeChecks(
              sanOpts.has(SanitizerKind::ImplicitIntegerSignChange)) {}
  };

  // Conversion from bool, integral, or floating-point to integral or
  // floating-point. Conversions involving other types are handled elsewhere.
  // Conversion to bool is handled elsewhere because that's a comparison against
  // zero, not a simple cast. This handles both individual scalars and vectors.
  mlir::Value emitScalarCast(mlir::Value src, QualType srcType,
                             QualType dstType, mlir::Type srcTy,
                             mlir::Type dstTy, ScalarConversionOpts opts,
                             cir::FastMathFlags fmf =
                                 cir::FastMathFlags::none,
                             FPOptions fpfeatures = FPOptions()) {
    assert(!srcType->isMatrixType() && !dstType->isMatrixType() &&
           "Internal error: matrix types not handled by this function.");
    assert(!(mlir::isa<mlir::IntegerType>(srcTy) ||
             mlir::isa<mlir::IntegerType>(dstTy)) &&
           "Obsolete code. Don't use mlir::IntegerType with CIR.");

    mlir::Type fullDstTy = dstTy;
    if (mlir::isa<cir::VectorType>(srcTy) &&
        mlir::isa<cir::VectorType>(dstTy)) {
      // Use the element types of the vectors to figure out the CastKind.
      srcTy = mlir::cast<cir::VectorType>(srcTy).getElementType();
      dstTy = mlir::cast<cir::VectorType>(dstTy).getElementType();
    }

    std::optional<cir::CastKind> castKind;

    if (mlir::isa<cir::BoolType>(srcTy)) {
      if (opts.treatBooleanAsSigned)
        (void)0; // Sanitizer-specific CIR parity work remains NYI in this path.
      if (cgf.getBuilder().isInt(dstTy)) {
        castKind = cir::CastKind::bool_to_int;
        if (opts.treatBooleanAsSigned) {
          // CK_BooleanToSignedIntegral: true -> -1, false -> 0.
          // OGCG uses sext for this. We emit bool_to_int (zext: true -> 1)
          // then negate (1 -> -1) to get the same result.
          mlir::Value zext = builder.createOrFold<cir::CastOp>(
              src.getLoc(), fullDstTy, *castKind, src);
          mlir::Value neg = builder.createNeg(zext);
          // createNeg may cast unsigned to signed internally; cast back to
          // the original destination type to preserve signedness.
          if (neg.getType() != fullDstTy)
            neg = builder.createIntCast(neg, fullDstTy);
          return neg;
        }
      } else if (mlir::isa<cir::FPTypeInterface>(dstTy))
        castKind = cir::CastKind::bool_to_float;
      else
        llvm_unreachable("Internal error: Cast to unexpected type");
    } else if (cgf.getBuilder().isInt(srcTy)) {
      if (cgf.getBuilder().isInt(dstTy))
        castKind = cir::CastKind::integral;
      else if (mlir::isa<cir::FPTypeInterface>(dstTy))
        castKind = cir::CastKind::int_to_float;
      else if (mlir::isa<cir::BoolType>(dstTy))
        castKind = cir::CastKind::int_to_bool;
      else
        llvm_unreachable("Internal error: Cast to unexpected type");
    } else if (mlir::isa<cir::FPTypeInterface>(srcTy)) {
      if (cgf.getBuilder().isInt(dstTy)) {
        // If we can't recognize overflow as undefined behavior, assume that
        // overflow saturates. This protects against normal optimizations if we
        // are compiling with non-standard FP semantics.
        if (!cgf.cgm.getCodeGenOpts().StrictFloatCastOverflow) {
          castKind = cir::CastKind::float_to_int_sat;
        } else {
          castKind = cir::CastKind::float_to_int;
        }
      } else if (mlir::isa<cir::FPTypeInterface>(dstTy)) {
        castKind = cir::CastKind::floating;
      } else if (mlir::isa<cir::BoolType>(dstTy)) {
        castKind = cir::CastKind::float_to_bool;
      } else {
        llvm_unreachable("Internal error: Cast to unexpected type");
      }
    } else {
      llvm_unreachable("Internal error: Cast from unexpected type");
    }

    assert(castKind.has_value() && "Internal error: CastKind not set.");

    bool isFPCast = *castKind == cir::CastKind::floating ||
                    *castKind == cir::CastKind::int_to_float ||
                    *castKind == cir::CastKind::float_to_int ||
                    *castKind == cir::CastKind::float_to_bool;
    bool isConstrained =
        isFPCast && (fpfeatures.isFPConstrained() || cgf.IsFPConstrained);

    // Under strict FP mode, don't constant-fold FP casts — they must go
    // through constrained intrinsics to preserve FP exception semantics.
    // Using create() instead of createOrFold() prevents the MLIR folder
    // from reducing e.g. (float)2 to 2.0f before FP attributes are set.
    mlir::Value cast;
    if (isConstrained)
      cast = cir::CastOp::create(builder, src.getLoc(), fullDstTy, *castKind,
                                 src, fmf);
    else
      cast = builder.createOrFold<cir::CastOp>(src.getLoc(), fullDstTy,
                                               *castKind, src, fmf);

    if (isConstrained) {
      auto castOp = cast.getDefiningOp<cir::CastOp>();
      assert(castOp && "expected CastOp, not a folded constant");
      auto *ctx = &cgf.getMLIRContext();
      if (auto fpExcept = cgf.getFPExceptionBehavior(ctx, fpfeatures))
        castOp->setAttr("cir.fp_exception", fpExcept);
      if (auto roundMode = CIRGenFunction::getRoundingMode(ctx, fpfeatures))
        castOp->setAttr("cir.rounding_mode", roundMode);
    }
    return cast;
  }

  mlir::Value
  VisitSubstNonTypeTemplateParmExpr(SubstNonTypeTemplateParmExpr *e) {
    return Visit(e->getReplacement());
  }

  mlir::Value VisitVAArgExpr(VAArgExpr *ve) {
    return cgf.emitVAArg(ve);
  }

  mlir::Value VisitCXXRewrittenBinaryOperator(CXXRewrittenBinaryOperator *e) {
    return Visit(e->getSemanticForm());
  }

  mlir::Value VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *e);
  mlir::Value
  VisitAbstractConditionalOperator(const AbstractConditionalOperator *e);

  // Unary Operators.
  mlir::Value VisitUnaryPostDec(const UnaryOperator *e) {
    LValue lv = cgf.emitLValue(e->getSubExpr());
    return emitScalarPrePostIncDec(e, lv, cir::UnaryOpKind::Dec, false);
  }
  mlir::Value VisitUnaryPostInc(const UnaryOperator *e) {
    LValue lv = cgf.emitLValue(e->getSubExpr());
    return emitScalarPrePostIncDec(e, lv, cir::UnaryOpKind::Inc, false);
  }
  mlir::Value VisitUnaryPreDec(const UnaryOperator *e) {
    LValue lv = cgf.emitLValue(e->getSubExpr());
    return emitScalarPrePostIncDec(e, lv, cir::UnaryOpKind::Dec, true);
  }
  mlir::Value VisitUnaryPreInc(const UnaryOperator *e) {
    LValue lv = cgf.emitLValue(e->getSubExpr());
    return emitScalarPrePostIncDec(e, lv, cir::UnaryOpKind::Inc, true);
  }
  mlir::Value emitScalarPrePostIncDec(const UnaryOperator *e, LValue lv,
                                      cir::UnaryOpKind kind, bool isPre) {
    CIRGenFunction::ApplySourceAtomGroup atomGroup(cgf);

    QualType type = e->getSubExpr()->getType();

    mlir::Value value;
    mlir::Value input;
    auto getFPConstant = [&](mlir::Location loc, mlir::Type ty,
                             int amount) -> mlir::Value {
      llvm::APFloat f(static_cast<float>(amount));
      bool ignored;

      const llvm::fltSemantics *fs;
      if (mlir::isa<cir::SingleType>(ty))
        fs = &llvm::APFloat::IEEEsingle();
      else if (mlir::isa<cir::DoubleType>(ty))
        fs = &llvm::APFloat::IEEEdouble();
      else if (mlir::isa<cir::LongDoubleType>(ty))
        fs = &cgf.getTarget().getLongDoubleFormat();
      else if (mlir::isa<cir::FP16Type>(ty))
        fs = &cgf.getTarget().getHalfFormat();
      else if (mlir::isa<cir::BF16Type>(ty))
        fs = &cgf.getTarget().getBFloat16Format();
      else if (mlir::isa<cir::FP128Type>(ty))
        fs = &cgf.getTarget().getFloat128Format();
      else if (mlir::isa<cir::FP80Type>(ty))
        fs = &llvm::APFloat::x87DoubleExtended();
      else {
        cgf.cgm.errorNYI(e->getSourceRange(),
                         "Atomic inc/dec unsupported floating type");
        return mlir::Value();
      }
      f.convert(*fs, llvm::APFloat::rmTowardZero, &ignored);
      return builder.getConstant(loc, cir::FPAttr::get(ty, f));
    };

    if (const AtomicType *atomicTy = type->getAs<AtomicType>()) {
      QualType valueTy = atomicTy->getValueType();
      // Special case for atomic bool increment. Bool increment always sets
      // to true. Matches OGCG (CGExprScalar.cpp:3187-3199).
      if (valueTy->isBooleanType() && kind == cir::UnaryOpKind::Inc) {
        mlir::Location loc = cgf.getLoc(e->getSourceRange());
        mlir::Value trueVal =
            builder.getBool(true, loc);
        if (isPre) {
          // Pre-increment: atomic store true, return true.
          auto orderAttr = cir::MemOrderAttr::get(
              builder.getContext(), cir::MemOrder::SequentiallyConsistent);
          builder.createStore(loc, trueVal, lv.getAddress(),
                              lv.isVolatile(),
                              /*isNontemporal=*/false,
                              /*align=*/mlir::IntegerAttr{},
                              /*syncScope=*/cir::SyncScopeKindAttr{},
                              orderAttr);
          return trueVal;
        }
        // Post-increment: atomicrmw xchg with true, return old value.
        // Atomic ops require integer types, so cast bool ptr to u8i ptr
        // and use u8i value 1. Cast result back to bool.
        auto u8Ty = builder.getUIntNTy(8);
        mlir::Value oneVal = builder.getConstantInt(loc, u8Ty, 1);
        auto u8PtrTy = builder.getPointerTo(u8Ty);
        mlir::Value castedPtr =
            builder.createBitcast(loc, lv.getPointer(), u8PtrTy);
        auto xchgResult = cir::AtomicXchgOp::create(
            builder, loc, castedPtr, oneVal,
            cir::MemOrder::SequentiallyConsistent,
            builder.getI64IntegerAttr(lv.getAlignment().getQuantity()),
            /*is_volatile=*/false, /*sync_scope=*/nullptr);
        return cir::CastOp::create(builder, loc, builder.getBoolTy(),
                                   cir::CastKind::int_to_bool, xchgResult);
      }
      // Atomic bool decrement: must use a CAS loop to normalize through
      // i1 truncation at each step, ensuring only valid bool values (0/1).
      // OGCG falls through to the generic CAS loop (CGExprScalar.cpp:3246).
      // atomicrmw sub is wrong: 0 - 1 = 0xFF which is not a valid bool.
      if (valueTy->isBooleanType() && kind == cir::UnaryOpKind::Dec) {
        mlir::Location loc = cgf.getLoc(e->getSourceRange());
        auto u8Ty = builder.getUIntNTy(8);
        auto boolTy = builder.getBoolTy();

        // Initial atomic load, then normalize: bool -> u8 for CAS loop.
        mlir::Value loadedBool =
            cgf.emitLoadOfLValue(lv, e->getExprLoc()).getValue();
        mlir::Value initialVal = cir::CastOp::create(
            builder, loc, u8Ty, cir::CastKind::bool_to_int, loadedBool);

        auto u8PtrTy = builder.getPointerTo(u8Ty);
        mlir::Value castedPtr =
            builder.createBitcast(loc, lv.getPointer(), u8PtrTy);

        CharUnits valAlign = CharUnits::One();
        Address expectedAddr =
            cgf.createTempAlloca(u8Ty, valAlign, loc, "atomic.expected");
        builder.createStore(loc, initialVal, expectedAddr);

        Address resultAddr =
            cgf.createTempAlloca(u8Ty, valAlign, loc, "atomic.result");

        Address successAddr =
            cgf.createTempAlloca(boolTy, valAlign, loc, "atomic.success");

        mlir::IntegerAttr cmpxchgAlign = cgf.getI64AttrCached(
            lv.getAddress().getAlignment().getAsAlign().value());

        builder.createDoWhile(
            loc,
            /*condBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location condLoc) {
              mlir::Value success = builder.createLoad(condLoc, successAddr);
              mlir::Value shouldContinue = builder.createNot(success);
              builder.createCondition(shouldContinue);
            },
            /*bodyBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location bodyLoc) {
              mlir::Value expected = builder.createLoad(bodyLoc, expectedAddr);

              // Normalize: trunc to i1, then decrement (add -1), then zext
              // back. Bool decrement: (bool)((int)b - 1) == (b ? false : true)
              // i.e. the result is !b.
              mlir::Value asBool = cir::CastOp::create(
                  builder, bodyLoc, boolTy, cir::CastKind::int_to_bool,
                  expected);
              mlir::Value decremented = builder.createNot(asBool);
              mlir::Value newVal = cir::CastOp::create(
                  builder, bodyLoc, u8Ty, cir::CastKind::bool_to_int,
                  decremented);

              builder.createStore(bodyLoc, isPre ? newVal : expected,
                                  resultAddr);

              auto cmpxchgOp = cir::AtomicCmpXchgOp::create(
                  builder, bodyLoc, u8Ty, boolTy, castedPtr, expected, newVal,
                  cir::MemOrder::SequentiallyConsistent,
                  cir::MemOrder::SequentiallyConsistent, cmpxchgAlign,
                  /*weak=*/false, /*is_volatile=*/lv.isVolatileQualified(),
                  /*sync_scope=*/cir::SyncScopeKindAttr{});

              // Feed observed value (normalized) to next iteration.
              mlir::Value oldBool = cir::CastOp::create(
                  builder, bodyLoc, boolTy, cir::CastKind::int_to_bool,
                  cmpxchgOp.getOld());
              mlir::Value oldNorm = cir::CastOp::create(
                  builder, bodyLoc, u8Ty, cir::CastKind::bool_to_int, oldBool);
              builder.createStore(bodyLoc, oldNorm, expectedAddr);
              builder.createStore(bodyLoc, cmpxchgOp.getSuccess(), successAddr);
              builder.createYield(bodyLoc);
            });

        mlir::Value resultU8 = builder.createLoad(loc, resultAddr);
        return cir::CastOp::create(builder, loc, boolTy,
                                   cir::CastKind::int_to_bool, resultU8);
      }
      // Fast path for integer atomic inc/dec: use atomicrmw add/sub.
      // Matches OGCG (CGExprScalar.cpp:3203-3218).
      // Skip this fast path when unsigned overflow sanitizer is active on
      // unsigned types — we need the CAS loop to check for overflow.
      if (!valueTy->isBooleanType() && valueTy->isIntegerType() &&
          !(valueTy->isUnsignedIntegerType() &&
            cgf.sanOpts.has(SanitizerKind::UnsignedIntegerOverflow)) &&
          cgf.getLangOpts().getSignedOverflowBehavior() !=
              LangOptions::SOB_Trapping) {
        mlir::Location loc = cgf.getLoc(e->getSourceRange());
        auto intTy = mlir::cast<cir::IntType>(cgf.convertType(valueTy));
        mlir::Value one = builder.getConstantInt(loc, intTy, 1);
        auto fetchKind = (kind == cir::UnaryOpKind::Inc)
                             ? cir::AtomicFetchKind::Add
                             : cir::AtomicFetchKind::Sub;
        // For pre-inc/dec, we want the new value (fetch after op).
        // For post-inc/dec, we want the old value (fetch before op).
        bool fetchFirst = !isPre;
        auto result = cir::AtomicFetchOp::create(
            builder, loc, lv.getPointer(), one, fetchKind,
            cir::MemOrder::SequentiallyConsistent,
            builder.getI64IntegerAttr(lv.getAlignment().getQuantity()),
            /*is_volatile=*/lv.isVolatile(), fetchFirst,
            /*sync_scope=*/nullptr);
        return result;
      }
      // Fast path for float/double atomics: use atomicrmw fadd/fsub.
      // Matches OGCG's AtomicRMW float path.
      if (valueTy->isFloatingType() &&
          mlir::isa<cir::SingleType, cir::DoubleType>(
              cgf.convertType(valueTy))) {
        mlir::Location loc = cgf.getLoc(e->getSourceRange());
        mlir::Value one = getFPConstant(loc, cgf.convertType(valueTy), 1);
        auto fetchKind = (kind == cir::UnaryOpKind::Inc)
                             ? cir::AtomicFetchKind::Add
                             : cir::AtomicFetchKind::Sub;
        // For pre-inc/dec, return the new value (fetch after op).
        // For post-inc/dec, return the old value (fetch before op).
        bool fetchFirst = !isPre;
        auto result = cir::AtomicFetchOp::create(
            builder, loc, lv.getPointer(), one, fetchKind,
            cir::MemOrder::SequentiallyConsistent,
            builder.getI64IntegerAttr(lv.getAlignment().getQuantity()),
            /*is_volatile=*/lv.isVolatile(), fetchFirst,
            /*sync_scope=*/nullptr);
        return result;
      }

      // Slow path for non-atomicrmw floating atomics (e.g. x86_fp80):
      // emit a CAS loop matching OGCG's fallback behavior.
      if (valueTy->isFloatingType()) {
        mlir::Location loc = cgf.getLoc(e->getSourceRange());
        SourceLocation srcLoc = e->getExprLoc();

        mlir::Type valueCIRTy = cgf.convertType(valueTy);
        int amountInt = (kind == cir::UnaryOpKind::Inc) ? 1 : -1;
        mlir::Value amount = getFPConstant(loc, valueCIRTy, amountInt);

        // Initial atomic load.
        mlir::Value initialVal = cgf.emitLoadOfLValue(lv, srcLoc).getValue();
        initialVal = cgf.emitToMemory(initialVal, valueTy);

        // Allocas for CAS loop state (replaces PHI nodes in OGCG).
        CharUnits valAlign = cgf.getContext().getTypeAlignInChars(valueTy);
        Address expectedAddr =
            cgf.createTempAlloca(valueCIRTy, valAlign, loc, "atomic.expected");
        builder.createStore(loc, initialVal, expectedAddr);

        Address resultAddr =
            cgf.createTempAlloca(valueCIRTy, valAlign, loc, "atomic.result");

        mlir::Type boolCIRTy = builder.getBoolTy();
        CharUnits boolAlign = CharUnits::One();
        Address successAddr =
            cgf.createTempAlloca(boolCIRTy, boolAlign, loc, "atomic.success");

        mlir::IntegerAttr cmpxchgAlign = cgf.getI64AttrCached(
            lv.getAddress().getAlignment().getAsAlign().value());

        builder.createDoWhile(
            loc,
            /*condBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location condLoc) {
              mlir::Value success = builder.createLoad(condLoc, successAddr);
              // Continue looping while !success.
              mlir::Value shouldContinue = builder.createNot(success);
              builder.createCondition(shouldContinue);
            },
            /*bodyBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location bodyLoc) {
              // Load expected value.
              mlir::Value expected = builder.createLoad(bodyLoc, expectedAddr);

              // Match OGCG's generic floating inc/dec path: signed amount via
              // add, even for decrement.
              mlir::Value newVal = cir::BinOp::create(
                  builder, bodyLoc, expected.getType(), cir::BinOpKind::Add, expected,
                  amount, {}, {}, {}, {}, {}, {});

              // Store the expression result matching pre/post semantics.
              builder.createStore(bodyLoc, isPre ? newVal : expected,
                                  resultAddr);

              // Atomic compare-and-exchange.
              auto cmpxchgOp = cir::AtomicCmpXchgOp::create(
                  builder, bodyLoc, expected.getType(), builder.getBoolTy(),
                  lv.getPointer(), expected, newVal,
                  cir::MemOrder::SequentiallyConsistent,
                  cir::MemOrder::SequentiallyConsistent, cmpxchgAlign,
                  /*weak=*/false, /*is_volatile=*/lv.isVolatileQualified(),
                  /*sync_scope=*/cir::SyncScopeKindAttr{});

              // Feed the observed value to the next iteration.
              mlir::Value oldVal = cgf.emitToMemory(cmpxchgOp.getOld(), valueTy);
              builder.createStore(bodyLoc, oldVal, expectedAddr);
              builder.createStore(bodyLoc, cmpxchgOp.getSuccess(), successAddr);
              builder.createYield(bodyLoc);
            });

        return builder.createLoad(loc, resultAddr);
      }

      // Slow path (e.g. sanitizer-checked) requires a CAS loop.
      cgf.cgm.errorNYI(e->getSourceRange(),
                       "Atomic inc/dec requiring CAS loop");
      return {};
    } else {
      value = cgf.emitLoadOfLValue(lv, e->getExprLoc()).getValue();
      input = value;
    }

    // NOTE: When possible, more frequent cases are handled first.

    // Special case of integer increment that we have to check first: bool++.
    // Due to promotion rules, we get:
    //   bool++ -> bool = bool + 1
    //          -> bool = (int)bool + 1
    //          -> bool = ((int)bool + 1 != 0)
    // An interesting aspect of this is that increment is always true.
    // Decrement does not have this property.
    if (kind == cir::UnaryOpKind::Inc && type->isBooleanType()) {
      value = builder.getTrue(cgf.getLoc(e->getExprLoc()));
    } else if (type->isIntegerType()) {
      QualType promotedType;
      [[maybe_unused]] bool canPerformLossyDemotionCheck = false;
      if (cgf.getContext().isPromotableIntegerType(type)) {
        promotedType = cgf.getContext().getPromotedIntegerType(type);
        assert(promotedType != type && "Shouldn't promote to the same type.");
        canPerformLossyDemotionCheck = true;
        canPerformLossyDemotionCheck &=
            cgf.getContext().getCanonicalType(type) !=
            cgf.getContext().getCanonicalType(promotedType);
        canPerformLossyDemotionCheck &=
            type->isIntegerType() && promotedType->isIntegerType();

        // TODO(cir): Currently, we store bitwidths in CIR types only for
        // integers. This might also be required for other types.

        assert(
            (!canPerformLossyDemotionCheck ||
             type->isSignedIntegerOrEnumerationType() ||
             promotedType->isSignedIntegerOrEnumerationType() ||
             mlir::cast<cir::IntType>(cgf.convertType(type)).getWidth() ==
                 mlir::cast<cir::IntType>(cgf.convertType(promotedType)).getWidth()) &&
            "The following check expects that if we do promotion to different "
            "underlying canonical type, at least one of the types (either "
            "base or promoted) will be signed, or the bitwidths will match.");
      }

      (void)0; // Sanitizer-specific CIR parity work remains NYI in this path.
      if (e->canOverflow() && type->isSignedIntegerOrEnumerationType()) {
        value = emitIncDecConsiderOverflowBehavior(e, value, kind);
      } else if (e->canOverflow() && type->isUnsignedIntegerType() &&
                 cgf.sanOpts.has(SanitizerKind::UnsignedIntegerOverflow) &&
                 !cgf.getContext().isTypeIgnoredBySanitizer(
                     SanitizerKind::UnsignedIntegerOverflow, e->getType())) {
        // Unsigned inc/dec overflow check — build a synthetic BinOpInfo
        // and emit an overflow-checked add/sub, matching OGCG's
        // EmitOverflowCheckedBinOp (CGExprScalar.cpp:3338-3344).
        BinOpInfo info;
        mlir::Location loc = cgf.getLoc(e->getSourceRange());
        auto intTy = mlir::cast<cir::IntType>(value.getType());
        info.lhs = value;
        info.rhs = builder.getConstantInt(loc, intTy, 1);
        info.fullType = e->getType();
        info.compType = e->getType();
        info.opcode = e->isIncrementOp() ? BO_Add : BO_Sub;
        info.e = e;
        info.loc = e->getSourceRange();
        value = emitOverflowCheckedBinOp(cgf, builder, info);
      } else {
        cir::UnaryOpKind kind =
            e->isIncrementOp() ? cir::UnaryOpKind::Inc : cir::UnaryOpKind::Dec;
        // NOTE(CIR): clang calls CreateAdd but folds this to a unary op
        value = emitUnaryOp(e, kind, input, /*nsw=*/false);
      }
    } else if (const PointerType *ptr = type->getAs<PointerType>()) {
      QualType type = ptr->getPointeeType();
      if (const VariableArrayType *vla =
              cgf.getContext().getAsVariableArrayType(type)) {
        // VLA types don't have constant size.
        mlir::Location loc = cgf.getLoc(e->getSourceRange());
        CIRGenBuilderTy &builder = cgf.getBuilder();
        mlir::Value numElts = cgf.getVLASize(vla).numElts;
        if (kind != cir::UnaryOpKind::Inc)
          numElts = cir::UnaryOp::create(builder, loc, numElts.getType(),
                                         cir::UnaryOpKind::Minus, numElts);
        (void)0; // Sanitizer-specific CIR parity work remains NYI in this path.
        // Match OGCG's EmitCheckedInBoundsGEP: inbounds when pointer overflow
        // is not defined, nuw for increment (non-subtraction with unsigned
        // indices).
        cir::GEPNoWrapFlags nwFlags = cir::GEPNoWrapFlags::none;
        if (!cgf.getLangOpts().PointerOverflowDefined) {
          nwFlags = cir::GEPNoWrapFlags::inbounds;
          if (kind == cir::UnaryOpKind::Inc)
            nwFlags = nwFlags | cir::GEPNoWrapFlags::nuw;
        }
        value = cir::PtrStrideOp::create(builder, loc, value.getType(), value,
                                         numElts, nwFlags);
        if (!cgf.getLangOpts().PointerOverflowDefined &&
            cgf.sanOpts.has(SanitizerKind::PointerOverflow)) {
          uint64_t elementSize = cgf.cgm.getDataLayout()
                                     .getTypeAllocSize(cgf.convertTypeForMem(
                                         vla->getElementType()))
                                     .getFixedValue();
          auto [byteOffset, offsetOverflow] =
              cgf.emitCheckedByteOffset(loc, numElts, elementSize);
          cgf.emitPointerOverflowCheck(loc, input, byteOffset, offsetOverflow,
                                       /*signedIndices=*/true,
                                       /*isSubtraction=*/kind !=
                                           cir::UnaryOpKind::Inc,
                                       e->getExprLoc());
        }
      } else if (type->isFunctionType()) {
        // Arithmetic on function pointers (!) is just +-1.
        // GNU C extension treats sizeof(function) as 1.
        mlir::Location loc = cgf.getLoc(e->getSourceRange());
        CIRGenBuilderTy &builder = cgf.getBuilder();
        int amount = kind == cir::UnaryOpKind::Inc ? 1 : -1;
        mlir::Value amt = builder.getSInt32(amount, loc);
        (void)0; // Sanitizer-specific CIR parity work remains NYI in this path.
        cir::GEPNoWrapFlags nwFlags = cir::GEPNoWrapFlags::none;
        if (!cgf.getLangOpts().PointerOverflowDefined) {
          nwFlags = cir::GEPNoWrapFlags::inbounds;
          if (kind == cir::UnaryOpKind::Inc)
            nwFlags = nwFlags | cir::GEPNoWrapFlags::nuw;
        }
        value = cir::PtrStrideOp::create(builder, loc, value.getType(),
                                         value, amt, nwFlags);
        if (!cgf.getLangOpts().PointerOverflowDefined &&
            cgf.sanOpts.has(SanitizerKind::PointerOverflow)) {
          auto [byteOffset, offsetOverflow] =
              cgf.emitCheckedByteOffset(loc, amt, 1);
          cgf.emitPointerOverflowCheck(loc, input, byteOffset, offsetOverflow,
                                       /*signedIndices=*/false,
                                       /*isSubtraction=*/kind !=
                                           cir::UnaryOpKind::Inc,
                                       e->getExprLoc());
        }
      } else {
        // For everything else, we can just do a simple increment.
        mlir::Location loc = cgf.getLoc(e->getSourceRange());
        CIRGenBuilderTy &builder = cgf.getBuilder();
        int amount = kind == cir::UnaryOpKind::Inc ? 1 : -1;
        mlir::Value amt = builder.getSInt32(amount, loc);
        (void)0; // Sanitizer-specific CIR parity work remains NYI in this path.
        // Match OGCG: pointer inc/dec uses inbounds GEP when pointer
        // overflow is not defined. Add nuw for increment (non-subtraction).
        cir::GEPNoWrapFlags nwFlags = cir::GEPNoWrapFlags::none;
        if (!cgf.getLangOpts().PointerOverflowDefined) {
          nwFlags = cir::GEPNoWrapFlags::inbounds;
          if (kind == cir::UnaryOpKind::Inc)
            nwFlags = nwFlags | cir::GEPNoWrapFlags::nuw;
        }
        value = cir::PtrStrideOp::create(builder, loc, value.getType(),
                                         value, amt, nwFlags);
        if (!cgf.getLangOpts().PointerOverflowDefined &&
            cgf.sanOpts.has(SanitizerKind::PointerOverflow)) {
          uint64_t elementSize = 1;
          if (!type->isVoidType())
            elementSize =
                cgf.cgm.getDataLayout()
                    .getTypeAllocSize(cgf.convertTypeForMem(type))
                    .getFixedValue();
          auto [byteOffset, offsetOverflow] =
              cgf.emitCheckedByteOffset(loc, amt, elementSize);
          cgf.emitPointerOverflowCheck(loc, input, byteOffset, offsetOverflow,
                                       /*signedIndices=*/false,
                                       /*isSubtraction=*/kind !=
                                           cir::UnaryOpKind::Inc,
                                       e->getExprLoc());
        }
      }
    } else if (type->isVectorType()) {
      mlir::Location loc = cgf.getLoc(e->getSourceRange());
      auto vecTy = mlir::cast<cir::VectorType>(value.getType());
      auto elemTy = vecTy.getElementType();
      int amount = (kind == cir::UnaryOpKind::Inc) ? 1 : -1;

      if (type->hasIntegerRepresentation()) {
        // Integer vector: splat constant 1/-1 and add.
        mlir::Value scalar = builder.getConstInt(loc, elemTy, amount);
        mlir::Value splat =
            cir::VecSplatOp::create(builder, loc, vecTy, scalar);
        value = cir::BinOp::create(builder, loc, vecTy,
                                   cir::BinOpKind::Add, value, splat,
                                   {}, {}, {}, {}, {}, {});
      } else {
        // FP vector: splat constant 1.0/-1.0 and fadd.
        llvm::APFloat f(static_cast<float>(amount));
        bool ignored;
        const llvm::fltSemantics &sem =
            mlir::cast<cir::FPTypeInterface>(elemTy).getFloatSemantics();
        f.convert(sem, llvm::APFloat::rmTowardZero, &ignored);
        mlir::Value scalar =
            builder.getConstant(loc, cir::FPAttr::get(elemTy, f));
        mlir::Value splat =
            cir::VecSplatOp::create(builder, loc, vecTy, scalar);
        value = builder.createFAdd(loc, value, splat);
      }
    } else if (type->isRealFloatingType()) {
      cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(
          e->getFPFeaturesInEffect(cgf.getLangOpts()));

      FPOptions fpfeatures = e->getFPFeaturesInEffect(cgf.getLangOpts());
      auto *ctx = &cgf.getMLIRContext();
      auto fpExcept = cgf.getFPExceptionBehavior(ctx, fpfeatures);
      auto roundMode = CIRGenFunction::getRoundingMode(ctx, fpfeatures);

      if (type->isHalfType() &&
          !cgf.getContext().getLangOpts().NativeHalfType) {
        // Another special case: half FP increment should be done via float
        if (cgf.getContext().getTargetInfo().useFP16ConversionIntrinsics()) {
          cgf.cgm.errorNYI(e->getSourceRange(),
                           "Unary inc/dec half via FP16 conversion intrinsics");
          return {};
        } else {
          value = builder.createCast(cgf.getLoc(e->getExprLoc()),
                                     cir::CastKind::floating, input,
                                     cgf.cgm.floatTy);
          if (fpExcept || roundMode) {
            if (auto castOp = value.getDefiningOp<cir::CastOp>()) {
              if (fpExcept)
                castOp->setAttr("cir.fp_exception", fpExcept);
              if (roundMode)
                castOp->setAttr("cir.rounding_mode", roundMode);
            }
          }
        }
      }

      if (mlir::isa<cir::SingleType, cir::DoubleType>(value.getType())) {
        // Create the inc/dec operation.
        // NOTE(CIR): clang calls CreateAdd but folds this to a unary op
        assert((kind == cir::UnaryOpKind::Inc ||
                kind == cir::UnaryOpKind::Dec) &&
               "Invalid UnaryOp kind");
        value = emitUnaryOp(e, kind, value, /*nsw=*/false, fmf);
        if (fpExcept || roundMode) {
          if (auto unaryOp = value.getDefiningOp<cir::UnaryOp>()) {
            if (fpExcept)
              unaryOp->setAttr("cir.fp_exception", fpExcept);
            if (roundMode)
              unaryOp->setAttr("cir.rounding_mode", roundMode);
          }
        }
      } else {
        // Remaining types are Half, Bfloat16, LongDouble, __ibm128 or
        // __float128. Use binop(add) with a constant.
        int amount = (kind == cir::UnaryOpKind::Inc) ? 1 : -1;
        llvm::APFloat f(static_cast<float>(amount));
        bool ignored;
        const llvm::fltSemantics *fs;
        // Don't use getFloatTypeSemantics because Half isn't
        // necessarily represented using the "half" LLVM type.
        if (mlir::isa<cir::LongDoubleType>(value.getType()))
          fs = &cgf.getTarget().getLongDoubleFormat();
        else if (mlir::isa<cir::FP16Type>(value.getType()))
          fs = &cgf.getTarget().getHalfFormat();
        else if (mlir::isa<cir::BF16Type>(value.getType()))
          fs = &cgf.getTarget().getBFloat16Format();
        else if (mlir::isa<cir::FP128Type>(value.getType()))
          fs = &cgf.getTarget().getFloat128Format();
        else if (mlir::isa<cir::FP80Type>(value.getType()))
          fs = &llvm::APFloat::x87DoubleExtended();
        else {
          cgf.cgm.errorNYI(e->getSourceRange(),
                           "Unary inc/dec fp128 / ppc_fp128");
          return {};
        }
        f.convert(*fs, llvm::APFloat::rmTowardZero, &ignored);

        mlir::Location loc = cgf.getLoc(e->getExprLoc());
        mlir::Value amt =
            builder.getConstant(loc, cir::FPAttr::get(value.getType(), f));
        value =
            builder.createFAdd(loc, value, amt, fmf, fpExcept, roundMode);
      }

      if (type->isHalfType() &&
          !cgf.getContext().getLangOpts().NativeHalfType) {
        if (cgf.getContext().getTargetInfo().useFP16ConversionIntrinsics()) {
          cgf.cgm.errorNYI(e->getSourceRange(),
                           "Unary inc/dec half via FP16 conversion intrinsics");
          return {};
        } else {
          value = builder.createCast(cgf.getLoc(e->getExprLoc()),
                                     cir::CastKind::floating, value,
                                     input.getType());
          if (fpExcept || roundMode) {
            if (auto castOp = value.getDefiningOp<cir::CastOp>()) {
              if (fpExcept)
                castOp->setAttr("cir.fp_exception", fpExcept);
              if (roundMode)
                castOp->setAttr("cir.rounding_mode", roundMode);
            }
          }
        }
      }
    } else if (type->isFixedPointType()) {
      mlir::Location loc = cgf.getLoc(e->getExprLoc());
      BinOpInfo info;
      info.lhs = value;
      info.fullType = e->getType();
      info.compType = info.fullType;
      info.opcode =
          kind == cir::UnaryOpKind::Inc ? BO_Add : BO_Sub;
      info.loc = e->getSourceRange();
      info.fpfeatures = e->getFPFeaturesInEffect(cgf.getLangOpts());
      info.e = e;

      auto width = mlir::cast<cir::IntType>(value.getType()).getWidth();
      info.rhs = builder.getConstAPInt(loc, value.getType(),
                                       llvm::APInt(width, 1));
      if (type->isSignedFixedPointType()) {
        info.opcode =
            kind == cir::UnaryOpKind::Inc ? BO_Sub : BO_Add;
        info.rhs = builder.createNeg(info.rhs);
      }
      QualType srcIntTy = cgf.getContext().getIntTypeForBitwidth(
          width, /*Signed=*/true);
      info.rhs =
          emitFixedPointConversion(info.rhs, srcIntTy, info.fullType,
                                   e->getExprLoc());
      value = emitFixedPointBinOp(info);
    } else {
      const auto *objPtr = type->castAs<ObjCObjectPointerType>();
      mlir::Location loc = cgf.getLoc(e->getSourceRange());
      CharUnits size =
          cgf.getContext().getTypeSizeInChars(objPtr->getObjectType());
      int64_t amount =
          kind == cir::UnaryOpKind::Inc ? size.getQuantity() : -size.getQuantity();
      mlir::Value amt = builder.getConstInt(loc, cgf.ptrDiffTy, amount);

      // Match OGCG's Objective-C pointer inc/dec path: use an inbounds byte
      // step when pointer overflow is undefined, plus nuw for increment.
      cir::GEPNoWrapFlags nwFlags = cir::GEPNoWrapFlags::none;
      if (!cgf.getLangOpts().PointerOverflowDefined) {
        nwFlags = cir::GEPNoWrapFlags::inbounds;
        if (kind == cir::UnaryOpKind::Inc)
          nwFlags = nwFlags | cir::GEPNoWrapFlags::nuw;
      }

      value = cir::PtrStrideOp::create(builder, loc, value.getType(), value,
                                       amt, nwFlags);
      if (!cgf.getLangOpts().PointerOverflowDefined &&
          cgf.sanOpts.has(SanitizerKind::PointerOverflow)) {
        auto [byteOffset, offsetOverflow] =
            cgf.emitCheckedByteOffset(loc, amt, 1);
        cgf.emitPointerOverflowCheck(loc, input, byteOffset, offsetOverflow,
                                     /*signedIndices=*/false,
                                     /*isSubtraction=*/kind !=
                                         cir::UnaryOpKind::Inc,
                                     e->getExprLoc());
      }
    }

    CIRGenFunction::SourceLocRAIIObject sourceloc{
        cgf, cgf.getLoc(e->getSourceRange())};

    // Store the updated result through the lvalue
    if (lv.isBitField()) {
      mlir::Value srcVal = value;
      value = cgf.emitStoreThroughBitfieldLValue(RValue::get(value), lv);
      emitBitfieldConversionCheckImpl(
          cgf, builder, cgf.getLoc(e->getExprLoc()), e->getExprLoc(), srcVal,
          e->getType(), value, e->getType(), lv.getBitFieldInfo());
    } else {
      cgf.emitStoreThroughLValue(RValue::get(value), lv);
    }

    if (cgf.getLangOpts().OpenMP)
      cgf.checkAndEmitOpenMPLastprivateConditional(e->getSubExpr());

    // If this is a postinc, return the value read from memory, otherwise use
    // the updated value.
    return isPre ? value : input;
  }

  mlir::Value emitIncDecConsiderOverflowBehavior(const UnaryOperator *e,
                                                 mlir::Value inVal,
                                                 cir::UnaryOpKind kind) {
    assert((kind == cir::UnaryOpKind::Inc ||
            kind == cir::UnaryOpKind::Dec) &&
           "Invalid UnaryOp kind");
    switch (cgf.getLangOpts().getSignedOverflowBehavior()) {
    case LangOptions::SOB_Defined:
      return emitUnaryOp(e, kind, inVal, /*nsw=*/false);
    case LangOptions::SOB_Undefined:
      if (cgf.sanOpts.has(SanitizerKind::SignedIntegerOverflow) &&
          e->canOverflow() &&
          !cgf.getContext().isTypeIgnoredBySanitizer(
              SanitizerKind::SignedIntegerOverflow, e->getType())) {
        // UBSan signed overflow check for inc/dec. Build synthetic
        // BinOpInfo, matching OGCG EmitOverflowCheckedBinOp.
        BinOpInfo info;
        mlir::Location loc = cgf.getLoc(e->getSourceRange());
        auto intTy = mlir::cast<cir::IntType>(inVal.getType());
        info.lhs = inVal;
        info.rhs = builder.getConstantInt(loc, intTy, 1);
        info.fullType = e->getType();
        info.compType = e->getType();
        info.opcode = (kind == cir::UnaryOpKind::Inc) ? BO_Add : BO_Sub;
        info.e = e;
        info.loc = e->getSourceRange();
        return emitOverflowCheckedBinOp(cgf, builder, info);
      }
      return emitUnaryOp(e, kind, inVal, /*nsw=*/true);
    case LangOptions::SOB_Trapping: {
      if (!e->canOverflow())
        return emitUnaryOp(e, kind, inVal, /*nsw=*/true);
      // Build a synthetic BinOpInfo for the overflow-checked operation.
      // Inc = add(val, 1), Dec = sub(val, 1).
      BinOpInfo info;
      mlir::Location loc = cgf.getLoc(e->getSourceRange());
      auto intTy = mlir::cast<cir::IntType>(inVal.getType());
      info.lhs = inVal;
      info.rhs = builder.getConstantInt(loc, intTy, 1);
      info.fullType = e->getType();
      info.compType = e->getType();
      info.opcode = (kind == cir::UnaryOpKind::Inc) ? BO_Add : BO_Sub;
      info.e = e;
      info.loc = e->getSourceRange();
      return emitOverflowCheckedBinOp(cgf, builder, info);
    }
    }
    llvm_unreachable("Unexpected signed overflow behavior kind");
  }

  mlir::Value VisitUnaryAddrOf(const UnaryOperator *e) {
    if (llvm::isa<MemberPointerType>(e->getType()))
      return cgf.cgm.emitMemberPointerConstant(e);

    return cgf.emitLValue(e->getSubExpr()).getPointer();
  }

  mlir::Value VisitUnaryDeref(const UnaryOperator *e) {
    if (e->getType()->isVoidType())
      return Visit(e->getSubExpr()); // the actual value should be unused
    return emitLoadOfLValue(e);
  }

  mlir::Value VisitUnaryPlus(const UnaryOperator *e) {
    QualType promotionType = getPromotionType(e->getSubExpr()->getType());
    mlir::Value result =
        emitUnaryPlusOrMinus(e, cir::UnaryOpKind::Plus, promotionType);
    if (result && !promotionType.isNull())
      return emitUnPromotedValue(result, e->getType());
    return result;
  }

  mlir::Value VisitUnaryMinus(const UnaryOperator *e) {
    QualType promotionType = getPromotionType(e->getSubExpr()->getType());
    mlir::Value result =
        emitUnaryPlusOrMinus(e, cir::UnaryOpKind::Minus, promotionType);
    if (result && !promotionType.isNull())
      return emitUnPromotedValue(result, e->getType());
    return result;
  }

  mlir::Value emitUnaryPlusOrMinus(const UnaryOperator *e,
                                   cir::UnaryOpKind kind,
                                   QualType promotionType) {
    ignoreResultAssign = false;
    mlir::Value operand;
    if (!promotionType.isNull())
      operand = cgf.emitPromotedScalarExpr(e->getSubExpr(), promotionType);
    else
      operand = Visit(e->getSubExpr());

    // Match OGCG: unary minus on signed integers respects overflow behavior.
    // Under -fwrapv (SOB_Defined), nsw must NOT be set.
    bool nsw = false;
    if (kind == cir::UnaryOpKind::Minus &&
        e->getType()->isSignedIntegerType()) {
      switch (cgf.getLangOpts().getSignedOverflowBehavior()) {
      case LangOptions::SOB_Defined:
        // -fwrapv: wrapping is defined, no nsw.
        nsw = false;
        break;
      case LangOptions::SOB_Undefined:
        if (cgf.sanOpts.has(SanitizerKind::SignedIntegerOverflow) &&
            e->canOverflow() &&
            !cgf.getContext().isTypeIgnoredBySanitizer(
                SanitizerKind::SignedIntegerOverflow, e->getType())) {
          // UBSan negation overflow check: synthesize as 0 - val.
          BinOpInfo info;
          info.lhs = builder.getConstantInt(
              cgf.getLoc(e->getSourceRange()),
              mlir::cast<cir::IntType>(operand.getType()), 0);
          info.rhs = operand;
          info.fullType = e->getType();
          info.compType = e->getType();
          info.opcode = BO_Sub;
          info.fpfeatures = e->getFPFeaturesInEffect(cgf.getLangOpts());
          info.e = e;
          info.loc = e->getSourceRange();
          // Match OGCG: elide the check if constant operands prove no
          // overflow (e.g., negating a small literal like -5).
          if (canElideNegationOverflowCheck(info))
            break; // fall through to nsw = true
          return emitOverflowCheckedBinOp(cgf, builder, info);
        }
        nsw = true;
        break;
      case LangOptions::SOB_Trapping:
        if (!e->canOverflow()) {
          nsw = true;
          break;
        }
        // Negation overflow check: synthesize as 0 - val, matching OGCG's
        // pattern of routing negation through EmitSub/EmitOverflowCheckedBinOp.
        {
          BinOpInfo info;
          info.lhs = builder.getConstantInt(
              cgf.getLoc(e->getSourceRange()),
              mlir::cast<cir::IntType>(operand.getType()), 0);
          info.rhs = operand;
          info.fullType = e->getType();
          info.compType = e->getType();
          info.opcode = BO_Sub;
          info.fpfeatures = e->getFPFeaturesInEffect(cgf.getLangOpts());
          info.e = e;
          info.loc = e->getSourceRange();
          // Match OGCG: elide the check if constant operands prove no
          // overflow (e.g., negating a small literal like -5).
          if (canElideNegationOverflowCheck(info))
            break; // fall through to nsw = true
          return emitOverflowCheckedBinOp(cgf, builder, info);
        }
      }
    }

    // Unsigned negation overflow check: -fsanitize=unsigned-integer-overflow.
    // Matches OGCG's EmitSub which checks UnsignedIntegerOverflow for
    // unsigned subtraction (negation is synthesized as 0 - val).
    if (kind == cir::UnaryOpKind::Minus &&
        e->getType()->isUnsignedIntegerType() &&
        cgf.sanOpts.has(SanitizerKind::UnsignedIntegerOverflow) &&
        e->canOverflow() &&
        !cgf.getContext().isTypeIgnoredBySanitizer(
            SanitizerKind::UnsignedIntegerOverflow, e->getType())) {
      BinOpInfo info;
      info.lhs = builder.getConstantInt(
          cgf.getLoc(e->getSourceRange()),
          mlir::cast<cir::IntType>(operand.getType()), 0);
      info.rhs = operand;
      info.fullType = e->getType();
      info.compType = e->getType();
      info.opcode = BO_Sub;
      info.fpfeatures = e->getFPFeaturesInEffect(cgf.getLangOpts());
      info.e = e;
      info.loc = e->getSourceRange();
      // Match OGCG: elide the check if constant operands prove no overflow.
      if (!canElideNegationOverflowCheck(info))
        return emitOverflowCheckedBinOp(cgf, builder, info);
    }

    // NOTE: LLVM codegen will lower this directly to either a FNeg
    // or a Sub instruction.  In CIR this will be handled later in LowerToLLVM.
    cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(
        e->getFPFeaturesInEffect(cgf.getLangOpts()));
    return emitUnaryOp(e, kind, operand, nsw, fmf);
  }

  mlir::Value emitUnaryOp(const UnaryOperator *e, cir::UnaryOpKind kind,
                          mlir::Value input, bool nsw = false,
                          cir::FastMathFlags fmf = cir::FastMathFlags::none) {
    return builder.createOrFold<cir::UnaryOp>(
        cgf.getLoc(e->getSourceRange().getBegin()), input.getType(), kind,
        input, nsw, fmf);
  }

  mlir::Value VisitUnaryNot(const UnaryOperator *e) {
    ignoreResultAssign = false;
    mlir::Value op = Visit(e->getSubExpr());
    return emitUnaryOp(e, cir::UnaryOpKind::Not, op);
  }

  mlir::Value VisitUnaryLNot(const UnaryOperator *e);

  mlir::Value VisitUnaryReal(const UnaryOperator *e);
  mlir::Value VisitUnaryImag(const UnaryOperator *e);
  mlir::Value VisitRealImag(const UnaryOperator *e,
                            QualType promotionType = QualType());

  mlir::Value VisitUnaryExtension(const UnaryOperator *e) {
    return Visit(e->getSubExpr());
  }

  // C++
  mlir::Value VisitMaterializeTemporaryExpr(const MaterializeTemporaryExpr *e) {
    return emitLoadOfLValue(e);
  }
  mlir::Value VisitSourceLocExpr(SourceLocExpr *e) {
    ASTContext &ctx = cgf.getContext();
    APValue evaluated =
        e->EvaluateInContext(ctx, cgf.curSourceLocExprScope.getDefaultExpr());
    mlir::Attribute attribute = ConstantEmitter(cgf).emitAbstract(
        e->getLocation(), evaluated, e->getType());
    mlir::TypedAttr typedAttr = mlir::cast<mlir::TypedAttr>(attribute);
    return cir::ConstantOp::create(builder, cgf.getLoc(e->getExprLoc()),
                                   typedAttr);
  }
  mlir::Value VisitCXXDefaultArgExpr(CXXDefaultArgExpr *dae) {
    CIRGenFunction::CXXDefaultArgExprScope scope(cgf, dae);
    return Visit(dae->getExpr());
  }
  mlir::Value VisitCXXDefaultInitExpr(CXXDefaultInitExpr *die) {
    CIRGenFunction::CXXDefaultInitExprScope scope(cgf, die);
    return Visit(die->getExpr());
  }

  mlir::Value VisitCXXThisExpr(CXXThisExpr *te) { return cgf.loadCXXThis(); }

  mlir::Value VisitExprWithCleanups(ExprWithCleanups *e);
  mlir::Value VisitCXXNewExpr(const CXXNewExpr *e) {
    return cgf.emitCXXNewExpr(e);
  }
  mlir::Value VisitCXXDeleteExpr(const CXXDeleteExpr *e) {
    cgf.emitCXXDeleteExpr(e);
    return {};
  }
  mlir::Value VisitTypeTraitExpr(const TypeTraitExpr *e) {
    mlir::Location loc = cgf.getLoc(e->getExprLoc());
    if (e->isStoredAsBoolean()) {
      mlir::Type type = cgf.convertType(e->getType());
      if (mlir::isa<cir::BoolType>(type))
        return builder.getBool(e->getBoolValue(), loc);
      return builder.getConstInt(loc, type, e->getBoolValue());
    }
    assert(e->getAPValue().isInt() && "APValue type not supported");
    return builder.getConstInt(loc, e->getAPValue().getInt());
  }
  mlir::Value
  VisitConceptSpecializationExpr(const ConceptSpecializationExpr *e) {
    return builder.getBool(e->isSatisfied(), cgf.getLoc(e->getExprLoc()));
  }
  mlir::Value VisitRequiresExpr(const RequiresExpr *e) {
    return builder.getBool(e->isSatisfied(), cgf.getLoc(e->getExprLoc()));
  }
  mlir::Value VisitArrayTypeTraitExpr(const ArrayTypeTraitExpr *e) {
    mlir::Type type = cgf.convertType(e->getType());
    mlir::Location loc = cgf.getLoc(e->getExprLoc());
    return builder.getConstInt(loc, type, e->getValue());
  }
  mlir::Value VisitExpressionTraitExpr(const ExpressionTraitExpr *e) {
    return builder.getBool(e->getValue(), cgf.getLoc(e->getExprLoc()));
  }
  mlir::Value VisitCXXPseudoDestructorExpr(const CXXPseudoDestructorExpr *e) {
    // Pseudo-destructor call on a scalar type (e.g., t.~int()). Per
    // [expr.pseudo]p1, the result is void — just evaluate the base for
    // side effects.
    cgf.emitScalarExpr(e->getBase());
    return {};
  }
  mlir::Value VisitCXXThrowExpr(const CXXThrowExpr *e) {
    cgf.emitCXXThrowExpr(e);
    return {};
  }

  mlir::Value VisitCXXNoexceptExpr(CXXNoexceptExpr *e) {
    return builder.getBool(e->getValue(), cgf.getLoc(e->getExprLoc()));
  }

  /// Emit a conversion from the specified type to the specified destination
  /// type, both of which are CIR scalar types.
  /// TODO: do we need ScalarConversionOpts here? Should be done in another
  /// pass.
  mlir::Value
  emitScalarConversion(mlir::Value src, QualType srcType, QualType dstType,
                       SourceLocation loc,
                       ScalarConversionOpts opts = ScalarConversionOpts(),
                       cir::FastMathFlags fmf = cir::FastMathFlags::none,
                       FPOptions fpfeatures = FPOptions()) {
    // A dependent type can survive into CIRGen when a Sema substitution bug
    // leaves SubstNonTypeTemplateParmExpr with its un-substituted type (see
    // e.g. compound-assign expressions whose computation types stay
    // dependent). The actual CIR value already carries the correct concrete
    // type, so treat the conversion as a no-op.
    if (srcType->isDependentType() || dstType->isDependentType())
      return src;

    // Keep fixed-point lowering in a dedicated helper rather than following
    // the normal integer conversion path. Although fixed-point values are
    // represented with CIR integers, the scaling and saturation semantics
    // differ from plain integral conversions.
    if (srcType->isFixedPointType()) {
      if (dstType->isBooleanType())
        return emitIntToBoolConversion(src, cgf.getLoc(loc));
      if (dstType->isFixedPointType() || dstType->isIntegerType() ||
          dstType->isRealFloatingType())
        return emitFixedPointConversion(src, srcType, dstType, loc, fmf,
                                        fpfeatures);
      llvm_unreachable(
          "unhandled scalar conversion from fixed-point to non-scalar type");
    } else if (dstType->isFixedPointType()) {
      if (srcType->isIntegerType() || srcType->isRealFloatingType())
        return emitFixedPointConversion(src, srcType, dstType, loc, fmf,
                                        fpfeatures);
      llvm_unreachable(
          "unhandled scalar conversion to fixed-point from non-scalar type");
    }

    QualType noncanonicalSrcType = srcType;
    QualType noncanonicalDstType = dstType;
    srcType = srcType.getCanonicalType();
    dstType = dstType.getCanonicalType();
    if (srcType == dstType) {
      if (opts.emitImplicitIntegerSignChangeChecks)
        emitIntegerSignChangeSanitizerCheck(cgf, builder, cgf.getLoc(loc), loc,
                                            src, noncanonicalSrcType, src,
                                            noncanonicalDstType);
      return src;
    }

    if (dstType->isVoidType())
      return {};

    mlir::Type mlirSrcType = src.getType();

    // Handle conversions to bool first, they are special: comparisons against
    // 0.
    if (dstType->isBooleanType())
      return emitConversionToBool(src, srcType, cgf.getLoc(loc), fpfeatures);

    mlir::Type mlirDstType = cgf.convertType(dstType);
    mlir::Value origSrc = src;
    QualType origSrcType = srcType;

    if (srcType->isHalfType() &&
        !cgf.getContext().getLangOpts().NativeHalfType) {
      // Cast to FP using the intrinsic if the half type itself isn't supported.
      if (mlir::isa<cir::FPTypeInterface>(mlirDstType)) {
        (void)cgf.getContext().getTargetInfo().useFP16ConversionIntrinsics();
      } else {
        // Cast to other types through float, using either the intrinsic or
        // FPExt, depending on whether the half type itself is supported (as
        // opposed to operations on half, available with NativeHalfType).
        (void)cgf.getContext().getTargetInfo().useFP16ConversionIntrinsics();
        // FIXME(cir): For now lets pretend we shouldn't use the conversion
        // intrinsics and insert a cast here unconditionally.
        src = builder.createCast(cgf.getLoc(loc), cir::CastKind::floating, src,
                                 cgf.floatTy);
        if (fpfeatures.isFPConstrained() || cgf.IsFPConstrained) {
          if (auto castOp = src.getDefiningOp<cir::CastOp>()) {
            auto *ctx = &cgf.getMLIRContext();
            if (auto fpExcept = cgf.getFPExceptionBehavior(ctx, fpfeatures))
              castOp->setAttr("cir.fp_exception", fpExcept);
            if (auto roundMode =
                    CIRGenFunction::getRoundingMode(ctx, fpfeatures))
              castOp->setAttr("cir.rounding_mode", roundMode);
          }
        }
        srcType = cgf.getContext().FloatTy;
        mlirSrcType = cgf.floatTy;
      }
    }

    // TODO(cir): LLVM codegen ignore conversions like int -> uint,
    // is there anything to be done for CIR here?
    if (mlirSrcType == mlirDstType) {
      if (opts.emitImplicitIntegerSignChangeChecks)
        emitIntegerSignChangeSanitizerCheck(cgf, builder, cgf.getLoc(loc), loc,
                                            src, noncanonicalSrcType, src,
                                            noncanonicalDstType);
      return src;
    }

    // Handle pointer conversions next: pointers can only be converted to/from
    // other pointers and integers. Check for pointer types in terms of LLVM, as
    // some native types (like Obj-C id) may map to a pointer type.
    if (auto dstPT = dyn_cast<cir::PointerType>(mlirDstType)) {
      // The source value may be an integer, or a pointer.
      if (isa<cir::PointerType>(mlirSrcType))
        return builder.createBitcast(src, dstPT);

      assert(srcType->isIntegerType() &&
             "Not ptr->ptr or int->ptr conversion?");
      return builder.createIntToPtr(src, dstPT);
    }

    if (isa<cir::PointerType>(mlirSrcType)) {
      // Must be an ptr to int cast.
      assert(isa<cir::IntType>(mlirDstType) && "not ptr->int?");
      return builder.createPtrToInt(src, mlirDstType);
    }

    // A scalar can be splatted to an extended vector of the same element type
    if (dstType->isExtVectorType() && !srcType->isVectorType()) {
      // Sema should add casts to make sure that the source expression's type
      // is the same as the vector's element type (sans qualifiers)
      assert(dstType->castAs<ExtVectorType>()->getElementType().getTypePtr() ==
                 srcType.getTypePtr() &&
             "Splatted expr doesn't match with vector element type?");

      return cir::VecSplatOp::create(builder, cgf.getLoc(loc), mlirDstType,
                                     src);
    }

    if (srcType->isMatrixType() && dstType->isMatrixType()) {
      cgf.getCIRGenModule().errorNYI(loc,
                                     "matrix type to matrix type conversion");
      return {};
    }
    assert(!srcType->isMatrixType() && !dstType->isMatrixType() &&
           "Internal error: conversion between matrix type and scalar type");

    // Finally, we have the arithmetic types or vectors of arithmetic types.
    mlir::Value res = nullptr;
    mlir::Type resTy = mlirDstType;

    if (cgf.sanOpts.has(SanitizerKind::FloatCastOverflow) &&
        origSrcType->isFloatingType())
      emitFloatConversionCheck(cgf, builder, origSrc, origSrcType, src,
                               srcType, dstType, loc);

    // Cast to half through float if half isn't a native type.
    // Matches OGCG CGExprScalar.cpp:1729.
    if (dstType->isHalfType() &&
        !cgf.getContext().getLangOpts().NativeHalfType) {
      if (mlir::isa<cir::FPTypeInterface>(mlirSrcType)) {
        // FP → half: single step fptrunc is fine.
      } else {
        // int → half: go through float first.
        mlirDstType = cgf.floatTy;
      }
    }

    res = emitScalarCast(src, srcType, dstType, mlirSrcType, mlirDstType, opts,
                         fmf, fpfeatures);

    if (mlirDstType != resTy) {
      (void)cgf.getContext().getTargetInfo().useFP16ConversionIntrinsics();
      // FIXME(cir): For now we never use FP16 conversion intrinsics even if
      // required by the target. Change that once this is implemented
      res = builder.createCast(cgf.getLoc(loc), cir::CastKind::floating, res,
                               resTy);
      if (fpfeatures.isFPConstrained() || cgf.IsFPConstrained) {
        if (auto castOp = res.getDefiningOp<cir::CastOp>()) {
          auto *ctx = &cgf.getMLIRContext();
          if (auto fpExcept = cgf.getFPExceptionBehavior(ctx, fpfeatures))
            castOp->setAttr("cir.fp_exception", fpExcept);
          if (auto roundMode =
                  CIRGenFunction::getRoundingMode(ctx, fpfeatures))
            castOp->setAttr("cir.rounding_mode", roundMode);
        }
      }
    }

    if (opts.emitImplicitIntegerTruncationChecks)
      emitIntegerTruncationSanitizerCheck(cgf, builder, cgf.getLoc(loc), loc,
                                          src, noncanonicalSrcType, res,
                                          noncanonicalDstType);

    if (opts.emitImplicitIntegerSignChangeChecks)
      emitIntegerSignChangeSanitizerCheck(cgf, builder, cgf.getLoc(loc), loc,
                                          src, noncanonicalSrcType, res,
                                          noncanonicalDstType);

    return res;
  }

  BinOpInfo emitBinOps(const BinaryOperator *e,
                       QualType promotionType = QualType()) {
    ignoreResultAssign = false;
    BinOpInfo result;
    result.lhs = cgf.emitPromotedScalarExpr(e->getLHS(), promotionType);
    result.rhs = cgf.emitPromotedScalarExpr(e->getRHS(), promotionType);
    if (!promotionType.isNull())
      result.fullType = promotionType;
    else
      result.fullType = e->getType();
    // A dependent result type can survive from un-rebuilt template AST nodes
    // (e.g. SubstNonTypeTemplateParmExpr retaining the unsubstituted type).
    // Fall back to a concrete operand type so downstream code paths (integer
    // vs float dispatch) work correctly.
    if (result.fullType->isDependentType()) {
      QualType lhsTy = e->getLHS()->getType();
      result.fullType = lhsTy->isDependentType() ? e->getRHS()->getType()
                                                 : lhsTy;
      // If both operand types are dependent, infer from the MLIR values.
      if (result.fullType->isDependentType()) {
        mlir::Type mlirTy = result.lhs ? result.lhs.getType()
                            : result.rhs ? result.rhs.getType()
                            : nullptr;
        if (mlirTy) {
          if (mlir::isa<cir::IntType>(mlirTy))
            result.fullType = cgf.getContext().IntTy;
          else if (mlir::isa<cir::SingleType>(mlirTy))
            result.fullType = cgf.getContext().FloatTy;
          else if (mlir::isa<cir::DoubleType>(mlirTy))
            result.fullType = cgf.getContext().DoubleTy;
        }
      }
      // With dependent types, Sema's implicit casts (e.g., int→float) are
      // missing.  Convert operands to the resolved fullType if needed.
      // Use the actual MLIR type to determine the source QualType since
      // the AST expression type may be dependent.
      mlir::Type targetTy = cgf.convertType(result.fullType);
      if (result.lhs && result.lhs.getType() != targetTy) {
        QualType srcTy = e->getLHS()->getType();
        if (srcTy->isDependentType()) {
          if (mlir::isa<cir::IntType>(result.lhs.getType()))
            srcTy = cgf.getContext().IntTy;
          else if (mlir::isa<cir::SingleType>(result.lhs.getType()))
            srcTy = cgf.getContext().FloatTy;
          else if (mlir::isa<cir::DoubleType>(result.lhs.getType()))
            srcTy = cgf.getContext().DoubleTy;
        }
        result.lhs = emitScalarConversion(result.lhs, srcTy, result.fullType,
                                          e->getExprLoc());
      }
      if (result.rhs && result.rhs.getType() != targetTy) {
        QualType srcTy = e->getRHS()->getType();
        if (srcTy->isDependentType()) {
          if (mlir::isa<cir::IntType>(result.rhs.getType()))
            srcTy = cgf.getContext().IntTy;
          else if (mlir::isa<cir::SingleType>(result.rhs.getType()))
            srcTy = cgf.getContext().FloatTy;
          else if (mlir::isa<cir::DoubleType>(result.rhs.getType()))
            srcTy = cgf.getContext().DoubleTy;
        }
        result.rhs = emitScalarConversion(result.rhs, srcTy, result.fullType,
                                          e->getExprLoc());
      }
    }
    result.compType = result.fullType;
    if (const auto *vecType = dyn_cast_or_null<VectorType>(result.fullType)) {
      result.compType = vecType->getElementType();
    }
    if (result.fullType->isConstantMatrixType()) {
      auto matrixTy = mlir::cast<cir::VectorType>(cgf.convertType(result.fullType));
      auto loc = cgf.getLoc(e->getExprLoc());
      auto splatMatrixScalar = [&](mlir::Value scalar) {
        assert(scalar.getType() == matrixTy.getElementType() &&
               "matrix scalar operand should match matrix element type");
        auto zeroIndex = builder.getUInt64(0, loc);
        auto seed = cir::VecInsertOp::create(
            builder, loc,
            builder.getConstant(loc, cir::PoisonAttr::get(matrixTy)), scalar,
            zeroIndex);
        SmallVector<int64_t, 16> mask(matrixTy.getSize(), 0);
        return builder.createVecShuffle(loc, seed, mask).getResult();
      };
      if (result.lhs && result.lhs.getType() == matrixTy.getElementType())
        result.lhs = splatMatrixScalar(result.lhs);
      if (result.rhs && result.rhs.getType() == matrixTy.getElementType())
        result.rhs = splatMatrixScalar(result.rhs);
    }
    result.opcode = e->getOpcode();
    result.loc = e->getSourceRange();
    result.fpfeatures = e->getFPFeaturesInEffect(cgf.getLangOpts());
    result.e = e;
    return result;
  }

  mlir::Value emitMul(const BinOpInfo &ops);
  mlir::Value emitDiv(const BinOpInfo &ops);
  mlir::Value emitRem(const BinOpInfo &ops);
  mlir::Value emitFixedPointConversion(mlir::Value src, QualType srcType,
                                       QualType dstType, SourceLocation loc,
                                       cir::FastMathFlags fmf =
                                           cir::FastMathFlags::none,
                                       FPOptions fpfeatures = FPOptions());
  mlir::Value emitFixedPointBinOp(const BinOpInfo &ops);
  mlir::Value emitAdd(const BinOpInfo &ops);
  mlir::Value emitSub(const BinOpInfo &ops);
  mlir::Value emitShl(const BinOpInfo &ops);
  mlir::Value emitShr(const BinOpInfo &ops);
  mlir::Value emitAnd(const BinOpInfo &ops);
  mlir::Value emitXor(const BinOpInfo &ops);
  mlir::Value emitOr(const BinOpInfo &ops);

  LValue emitCompoundAssignLValue(
      const CompoundAssignOperator *e,
      mlir::Value (ScalarExprEmitter::*f)(const BinOpInfo &),
      mlir::Value &result);
  mlir::Value
  emitCompoundAssign(const CompoundAssignOperator *e,
                     mlir::Value (ScalarExprEmitter::*f)(const BinOpInfo &));

  // TODO(cir): Candidate to be in a common AST helper between CIR and LLVM
  // codegen.
  QualType getPromotionType(QualType ty) {
    const clang::ASTContext &ctx = cgf.getContext();
    if (auto *complexTy = ty->getAs<ComplexType>()) {
      QualType elementTy = complexTy->getElementType();
      if (elementTy.UseExcessPrecision(ctx))
        return ctx.getComplexType(ctx.FloatTy);
    }

    if (ty.UseExcessPrecision(cgf.getContext())) {
      if (auto *vt = ty->getAs<VectorType>()) {
        unsigned numElements = vt->getNumElements();
        return ctx.getVectorType(ctx.FloatTy, numElements, vt->getVectorKind());
      }
      return cgf.getContext().FloatTy;
    }

    return QualType();
  }

// Binary operators and binary compound assignment operators.
#define HANDLEBINOP(OP)                                                        \
  mlir::Value VisitBin##OP(const BinaryOperator *e) {                          \
    QualType promotionTy = getPromotionType(e->getType());                     \
    auto result = emit##OP(emitBinOps(e, promotionTy));                        \
    if (result && !promotionTy.isNull())                                       \
      result = emitUnPromotedValue(result, e->getType());                      \
    return result;                                                             \
  }                                                                            \
  mlir::Value VisitBin##OP##Assign(const CompoundAssignOperator *e) {          \
    return emitCompoundAssign(e, &ScalarExprEmitter::emit##OP);                \
  }

  HANDLEBINOP(Mul)
  HANDLEBINOP(Div)
  HANDLEBINOP(Rem)
  HANDLEBINOP(Add)
  HANDLEBINOP(Sub)
  HANDLEBINOP(Shl)
  HANDLEBINOP(Shr)
  HANDLEBINOP(And)
  HANDLEBINOP(Xor)
  HANDLEBINOP(Or)
#undef HANDLEBINOP

  mlir::Value emitCmp(const BinaryOperator *e) {
    ignoreResultAssign = false;
    const mlir::Location loc = cgf.getLoc(e->getExprLoc());
    mlir::Value result;
    QualType lhsTy = e->getLHS()->getType();
    QualType rhsTy = e->getRHS()->getType();

    auto clangCmpToCIRCmp =
        [](clang::BinaryOperatorKind clangCmp) -> cir::CmpOpKind {
      switch (clangCmp) {
      case BO_LT:
        return cir::CmpOpKind::lt;
      case BO_GT:
        return cir::CmpOpKind::gt;
      case BO_LE:
        return cir::CmpOpKind::le;
      case BO_GE:
        return cir::CmpOpKind::ge;
      case BO_EQ:
        return cir::CmpOpKind::eq;
      case BO_NE:
        return cir::CmpOpKind::ne;
      default:
        llvm_unreachable("unsupported comparison kind for cir.cmp");
      }
    };

    cir::CmpOpKind kind = clangCmpToCIRCmp(e->getOpcode());
    if (lhsTy->getAs<MemberPointerType>()) {
      assert(!cir::MissingFeatures::dataMemberType());
      assert(e->getOpcode() == BO_EQ || e->getOpcode() == BO_NE);
      mlir::Value lhs = cgf.emitScalarExpr(e->getLHS());
      mlir::Value rhs = cgf.emitScalarExpr(e->getRHS());
      result = builder.createCompare(loc, kind, lhs, rhs);
    } else if (!lhsTy->isAnyComplexType() && !rhsTy->isAnyComplexType()) {
      BinOpInfo boInfo = emitBinOps(e);
      mlir::Value lhs = boInfo.lhs;
      mlir::Value rhs = boInfo.rhs;

      // OGCG distinguishes signaling (relational: LT/GT/LE/GE) vs
      // non-signaling (equality: EQ/NE) FP comparisons. Carry the distinction
      // as a discardable attribute so strict lowering can select constrained
      // fcmps where required.
      assert(!cir::MissingFeatures::fpSignalingComparisons());
      bool isSignalingFPCmp =
          e->getOpcode() == BO_LT || e->getOpcode() == BO_GT ||
          e->getOpcode() == BO_LE || e->getOpcode() == BO_GE;

      if (lhsTy->isVectorType()) {
        if (!e->getType()->isVectorType()) {
          // If AltiVec, the comparison results in a numeric type, so we use
          // intrinsics comparing vectors and giving 0 or 1 as a result
          cgf.cgm.errorNYI(loc, "AltiVec comparison");
          result = builder.getBool(false, loc);
        } else {
          // Other kinds of vectors. Element-wise comparison returning
          // a vector.
          cir::FastMathFlags fmf =
              CIRGenFunction::getFastMathFlags(boInfo.fpfeatures);
          result = cir::VecCmpOp::create(builder, cgf.getLoc(boInfo.loc),
                                         cgf.convertType(boInfo.fullType), kind,
                                         boInfo.lhs, boInfo.rhs, fmf);
          if (cir::isFPOrVectorOfFPType(boInfo.lhs.getType())) {
            cgf.maybeAttachFPConstraints(result.getDefiningOp(),
                                         boInfo.fpfeatures);
            cgf.maybeMarkSignalingFPCompare(result.getDefiningOp(),
                                            isSignalingFPCmp);
          }
        }
      } else if (boInfo.isFixedPointOp()) {
        result = emitFixedPointBinOp(boInfo);
      } else {
        // integers and pointers
        if (cgf.cgm.getCodeGenOpts().StrictVTablePointers &&
            mlir::isa<cir::PointerType>(lhs.getType()) &&
            mlir::isa<cir::PointerType>(rhs.getType())) {
          // Dynamic information is required to be stripped for comparisons,
          // because it could leak the dynamic information. Based on
          // comparisons of pointers to dynamic objects, the optimizer can
          // replace one pointer with another, which might be incorrect in
          // presence of invariant groups. Comparison with null is safe
          // because null does not carry any dynamic information.
          auto isNullConstPtr = [](mlir::Value v) {
            if (auto constOp = v.getDefiningOp<cir::ConstantOp>())
              return constOp.isNullPtr();
            return false;
          };
          if (!isNullConstPtr(lhs) && !isNullConstPtr(rhs)) {
            if (lhsTy.mayBeDynamicClass())
              lhs = cir::StripInvariantGroupOp::create(builder, loc, lhs);
            if (rhsTy.mayBeDynamicClass())
              rhs = cir::StripInvariantGroupOp::create(builder, loc, rhs);
          }
        }

        // The two operands of a C pointer comparison share the same source
        // type, so they must live in the same address space. On targets where
        // a global lives in a non-generic address space (e.g. amdgcn globals
        // in AS 1) the address of that global can reach the comparison still
        // carrying its native address space while the other operand is
        // generic. cir.cmp requires identical operand types, so reconcile the
        // address spaces here by casting the non-matching operand to the
        // other's (generic) space, mirroring the addrspacecast classic
        // CodeGen bakes into the global-var address.
        auto lhsPtrTy = mlir::dyn_cast<cir::PointerType>(lhs.getType());
        auto rhsPtrTy = mlir::dyn_cast<cir::PointerType>(rhs.getType());
        if (lhsPtrTy && rhsPtrTy &&
            lhsPtrTy.getAddrSpace() != rhsPtrTy.getAddrSpace()) {
          // Prefer casting the operand that is not in the default (generic)
          // address space; if neither is default, unify onto the LHS space.
          if (!rhsPtrTy.getAddrSpace()) {
            lhs = builder.createAddrSpaceCast(lhs, rhsPtrTy);
          } else {
            rhs = builder.createAddrSpaceCast(rhs, lhsPtrTy);
          }
        }

        cir::CmpOpKind kind = clangCmpToCIRCmp(e->getOpcode());
        cir::FastMathFlags fmf =
            CIRGenFunction::getFastMathFlags(boInfo.fpfeatures);
        result = builder.createCompare(loc, kind, lhs, rhs, fmf);
        if (boInfo.fpfeatures.isFPConstrained() || cgf.IsFPConstrained) {
          if (cir::isFPOrVectorOfFPType(lhs.getType())) {
            auto *defOp = result.getDefiningOp();
            cgf.maybeAttachFPConstraints(defOp, boInfo.fpfeatures);
            cgf.maybeMarkSignalingFPCompare(defOp, isSignalingFPCmp);
          }
        }
      }
    } else {
      // Complex Comparison: can only be an equality comparison.
      assert(e->getOpcode() == BO_EQ || e->getOpcode() == BO_NE);

      // Emit each operand as a complex value. If one side is scalar,
      // promote it to complex with zero imaginary part.
      mlir::Value lhs, rhs;
      if (lhsTy->isAnyComplexType()) {
        lhs = cgf.emitComplexExpr(e->getLHS());
      } else {
        mlir::Value real = Visit(e->getLHS());
        mlir::Value imag = builder.getNullValue(real.getType(), loc);
        lhs = builder.createComplexCreate(loc, real, imag);
      }
      if (rhsTy->isAnyComplexType()) {
        rhs = cgf.emitComplexExpr(e->getRHS());
      } else {
        mlir::Value real = Visit(e->getRHS());
        mlir::Value imag = builder.getNullValue(real.getType(), loc);
        rhs = builder.createComplexCreate(loc, real, imag);
      }
      result = cir::CmpOp::create(builder, loc, kind, lhs, rhs);
    }

    return emitScalarConversion(result, cgf.getContext().BoolTy, e->getType(),
                                e->getExprLoc());
  }

// Comparisons.
#define VISITCOMP(CODE)                                                        \
  mlir::Value VisitBin##CODE(const BinaryOperator *E) { return emitCmp(E); }
  VISITCOMP(LT)
  VISITCOMP(GT)
  VISITCOMP(LE)
  VISITCOMP(GE)
  VISITCOMP(EQ)
  VISITCOMP(NE)
#undef VISITCOMP

  mlir::Value VisitBinAssign(const BinaryOperator *e) {
    CIRGenFunction::ApplySourceAtomGroup atomGroup(cgf);
    const bool ignore = std::exchange(ignoreResultAssign, false);

    mlir::Value rhs;
    LValue lhs;

    if (PointerAuthQualifier ptrAuth =
            e->getLHS()->getType().getPointerAuth().withoutKeyNone()) {
      lhs = emitCheckedLValue(e->getLHS(), CIRGenFunction::TCK_Store);
      LValue copiedLV = lhs;
      copiedLV.getQuals().removePointerAuth();
      rhs = cgf.emitPointerAuthQualify(ptrAuth, e->getRHS(),
                                       copiedLV.getAddress());
      cgf.emitNullabilityCheck(copiedLV, rhs, e->getExprLoc());
      {
        CIRGenFunction::SourceLocRAIIObject loc{
            cgf, cgf.getLoc(e->getExprLoc())};
        cgf.emitStoreThroughLValue(RValue::get(rhs), copiedLV);
      }

      if (ignore)
        return nullptr;
      return cgf.emitPointerAuthUnqualify(ptrAuth, rhs, copiedLV.getType(),
                                          copiedLV.getAddress(),
                                          /*isKnownNonNull=*/false);
    }

    switch (e->getLHS()->getType().getObjCLifetime()) {
    case Qualifiers::OCL_Strong:
      rhs = cgf.emitARCRetainScalarExpr(e->getRHS());
      lhs = emitCheckedLValue(e->getLHS(), CIRGenFunction::TCK_Store);
      {
        CIRGenFunction::SourceLocRAIIObject loc{
            cgf, cgf.getLoc(e->getExprLoc())};
        mlir::Value oldValue = cgf.emitLoadOfScalar(lhs, SourceLocation());
        cgf.emitStoreThroughLValue(RValue::get(rhs), lhs);
        cgf.emitARCRelease(oldValue, cgf.getLoc(e->getExprLoc()));
      }
      break;
    case Qualifiers::OCL_Autoreleasing:
      rhs = cgf.emitARCRetainAutoreleaseScalarExpr(e->getRHS());
      lhs = emitCheckedLValue(e->getLHS(), CIRGenFunction::TCK_Store);
      {
        CIRGenFunction::SourceLocRAIIObject loc{
            cgf, cgf.getLoc(e->getExprLoc())};
        cgf.emitStoreThroughLValue(RValue::get(rhs), lhs);
      }
      break;
    case Qualifiers::OCL_Weak:
      rhs = Visit(e->getRHS());
      lhs = emitCheckedLValue(e->getLHS(), CIRGenFunction::TCK_Store);
      rhs = cgf.emitARCStoreWeak(lhs.getAddress(), rhs, ignore);
      break;
    case Qualifiers::OCL_ExplicitNone:
      rhs = ignore ? emitARCUnsafeUnretainedScalarExpr(cgf, e->getRHS())
                   : Visit(e->getRHS());
      lhs = emitCheckedLValue(e->getLHS(), CIRGenFunction::TCK_Store);

      if (lhs.isBitField()) {
        mlir::Value srcVal = rhs;
        rhs = cgf.emitStoreThroughBitfieldLValue(RValue::get(rhs), lhs);
        emitBitfieldConversionCheckImpl(
            cgf, builder, cgf.getLoc(e->getExprLoc()), e->getExprLoc(), srcVal,
            e->getRHS()->getType(), rhs, e->getLHS()->getType(),
            lhs.getBitFieldInfo());
      } else {
        CIRGenFunction::SourceLocRAIIObject loc{
            cgf, cgf.getLoc(e->getExprLoc())};
        cgf.emitStoreThroughLValue(RValue::get(rhs), lhs);
      }
      break;
    case Qualifiers::OCL_None:
      // __block variables need to have the rhs evaluated first, plus this
      // should improve codegen just a little.
      rhs = Visit(e->getRHS());
      lhs = emitCheckedLValue(e->getLHS(), CIRGenFunction::TCK_Store);

      // Store the value into the LHS. Bit-fields are handled specially because
      // the result is altered by the store, i.e., [C99 6.5.16p1]
      // 'An assignment expression has the value of the left operand after the
      // assignment...'.
      if (lhs.isBitField()) {
        mlir::Value srcVal = rhs;
        rhs = cgf.emitStoreThroughBitfieldLValue(RValue::get(rhs), lhs);
        emitBitfieldConversionCheckImpl(
            cgf, builder, cgf.getLoc(e->getExprLoc()), e->getExprLoc(), srcVal,
            e->getRHS()->getType(), rhs, e->getLHS()->getType(),
            lhs.getBitFieldInfo());
      } else {
        cgf.emitNullabilityCheck(lhs, rhs, e->getExprLoc());
        CIRGenFunction::SourceLocRAIIObject loc{
            cgf, cgf.getLoc(e->getExprLoc())};
        cgf.emitStoreThroughLValue(RValue::get(rhs), lhs);
      }
    }

    if (cgf.getLangOpts().OpenMP)
      cgf.checkAndEmitOpenMPLastprivateConditional(e->getLHS());

    // If the result is clearly ignored, return now.
    if (ignore)
      return nullptr;

    // The result of an assignment in C is the assigned r-value.
    if (!cgf.getLangOpts().CPlusPlus)
      return rhs;

    // If the lvalue is non-volatile, return the computed value of the
    // assignment.
    if (!lhs.isVolatile())
      return rhs;

    // Otherwise, reload the value.
    return emitLoadOfLValue(lhs, e->getExprLoc());
  }

  mlir::Value VisitBinComma(const BinaryOperator *e) {
    cgf.emitIgnoredExpr(e->getLHS());
    // NOTE: We don't need to EnsureInsertPoint() like LLVM codegen.
    return Visit(e->getRHS());
  }

  mlir::Value emitVectorLogicalOp(const BinaryOperator *e,
                                  cir::BinOpKind opKind);
  mlir::Value VisitBinLAnd(const clang::BinaryOperator *e) {
    if (e->getType()->isVectorType()) {
      maybeIncrementProfileCounter(e);
      return emitVectorLogicalOp(e, cir::BinOpKind::And);
    }

    mlir::Type resTy = cgf.convertType(e->getType());
    mlir::Location loc = cgf.getLoc(e->getExprLoc());

    // If we have 0 && RHS, see if we can elide RHS, if so, just return 0.
    // If we have 1 && X, just emit X without inserting the control flow.
    bool lhsCondVal;
    if (cgf.constantFoldsToBool(e->getLHS(), lhsCondVal)) {
      if (lhsCondVal) {
        // If we have 1 && X, just emit X.
        maybeIncrementProfileCounter(e);
        mlir::Value rhsCond = cgf.evaluateExprAsBool(e->getRHS());
        maybeIncrementConditionCounter(e->getRHS(), rhsCond, /*onTrue=*/true,
                                      cgf.getLoc(e->getRHS()->getSourceRange()));
        return maybePromoteBoolResult(rhsCond, resTy);
      }
      // 0 && RHS: If it is safe, just elide the RHS, and return 0/false.
      if (!cgf.containsLabel(e->getRHS()))
        return builder.getNullValue(resTy, loc);
    }

    CIRGenFunction::ConditionalEvaluation eval(cgf);

    mlir::Value lhsCondV = cgf.evaluateExprAsBool(e->getLHS());
    auto resOp = cir::TernaryOp::create(
        builder, loc, lhsCondV, /*trueBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          CIRGenFunction::LexicalScope lexScope{cgf, loc,
                                                b.getInsertionBlock()};
          cgf.curLexScope->setAsTernary();
          maybeIncrementProfileCounter(e);
          eval.beginEvaluation();
          mlir::Value res = cgf.evaluateExprAsBool(e->getRHS());
          eval.endEvaluation();
          maybeIncrementConditionCounter(e->getRHS(), res, /*onTrue=*/true,
                                        cgf.getLoc(e->getRHS()->getSourceRange()));
          lexScope.deferCleanupsToOuterScope();
          cir::YieldOp::create(b, loc, res);
        },
        /*falseBuilder*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          CIRGenFunction::LexicalScope lexScope{cgf, loc,
                                                b.getInsertionBlock()};
          cgf.curLexScope->setAsTernary();
          auto res = cir::ConstantOp::create(b, loc, builder.getFalseAttr());
          cir::YieldOp::create(b, loc, res.getRes());
        });
    return maybePromoteBoolResult(resOp.getResult(), resTy);
  }

  mlir::Value VisitBinLOr(const clang::BinaryOperator *e) {
    if (e->getType()->isVectorType()) {
      maybeIncrementProfileCounter(e);
      return emitVectorLogicalOp(e, cir::BinOpKind::Or);
    }

    mlir::Type resTy = cgf.convertType(e->getType());
    mlir::Location loc = cgf.getLoc(e->getExprLoc());

    // If we have 1 || RHS, see if we can elide RHS, if so, just return 1.
    // If we have 0 || X, just emit X without inserting the control flow.
    bool lhsCondVal;
    if (cgf.constantFoldsToBool(e->getLHS(), lhsCondVal)) {
      if (!lhsCondVal) {
        // If we have 0 || X, just emit X.
        maybeIncrementProfileCounter(e);
        mlir::Value rhsCond = cgf.evaluateExprAsBool(e->getRHS());
        maybeIncrementConditionCounter(e->getRHS(), rhsCond, /*onTrue=*/false,
                                      cgf.getLoc(e->getRHS()->getSourceRange()));
        return maybePromoteBoolResult(rhsCond, resTy);
      }
      // 1 || RHS: If it is safe, just elide the RHS, and return 1/true.
      if (!cgf.containsLabel(e->getRHS())) {
        if (auto intTy = mlir::dyn_cast<cir::IntType>(resTy))
          return builder.getConstInt(loc, intTy, 1);
        return builder.getBool(true, loc);
      }
    }

    CIRGenFunction::ConditionalEvaluation eval(cgf);

    mlir::Value lhsCondV = cgf.evaluateExprAsBool(e->getLHS());
    auto resOp = cir::TernaryOp::create(
        builder, loc, lhsCondV, /*trueBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          CIRGenFunction::LexicalScope lexScope{cgf, loc,
                                                b.getInsertionBlock()};
          cgf.curLexScope->setAsTernary();
          auto res = cir::ConstantOp::create(b, loc, builder.getTrueAttr());
          cir::YieldOp::create(b, loc, res.getRes());
        },
        /*falseBuilder*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          CIRGenFunction::LexicalScope lexScope{cgf, loc,
                                                b.getInsertionBlock()};
          cgf.curLexScope->setAsTernary();
          maybeIncrementProfileCounter(e);
          eval.beginEvaluation();
          mlir::Value res = cgf.evaluateExprAsBool(e->getRHS());
          eval.endEvaluation();
          maybeIncrementConditionCounter(e->getRHS(), res, /*onTrue=*/false,
                                        cgf.getLoc(e->getRHS()->getSourceRange()));
          lexScope.deferCleanupsToOuterScope();
          cir::YieldOp::create(b, loc, res);
        });

    return maybePromoteBoolResult(resOp.getResult(), resTy);
  }

  mlir::Value VisitBinPtrMemD(const BinaryOperator *e) {
    return emitLoadOfLValue(e);
  }

  mlir::Value VisitBinPtrMemI(const BinaryOperator *e) {
    return emitLoadOfLValue(e);
  }

  // Other Operators.
  mlir::Value VisitBlockExpr(const BlockExpr *e) {
    return cgf.emitBlockLiteral(e);
  }

  mlir::Value VisitChooseExpr(ChooseExpr *e) {
    return Visit(e->getChosenSubExpr());
  }

  mlir::Value VisitObjCStringLiteral(const ObjCStringLiteral *e) {
    mlir::Location loc = cgf.getLoc(e->getSourceRange());
    mlir::Value result =
        builder.getConstant(loc, cgf.cgm.getAddrOfConstantObjCString(
                                     e->getString()));
    mlir::Type exprTy = cgf.convertType(e->getType());
    if (result.getType() != exprTy)
      result = builder.createBitcast(loc, result, exprTy);
    return result;
  }
  mlir::Value VisitObjCBoxedExpr(ObjCBoxedExpr *e) {
    mlir::Location loc = cgf.getLoc(e->getSourceRange());
    if (e->isExpressibleAsConstantInitializer()) {
      ConstantEmitter emitter(cgf);
      if (mlir::Attribute constant = emitter.tryEmitAbstract(e, e->getType()))
        return builder.getConstant(loc, mlir::cast<mlir::TypedAttr>(constant));
    }

    const ObjCMethodDecl *boxingMethod = e->getBoxingMethod();
    const Expr *subExpr = e->getSubExpr();

    if (!boxingMethod || !boxingMethod->isClassMethod()) {
      cgf.cgm.errorNYI(e->getSourceRange(),
                       "ScalarExprEmitter: boxed expr without class method");
      return {};
    }

    const ObjCInterfaceDecl *classDecl = boxingMethod->getClassInterface();
    if (!classDecl) {
      cgf.cgm.errorNYI(e->getSourceRange(),
                       "ScalarExprEmitter: boxed expr without class interface");
      return {};
    }

    mlir::Value receiver = cgf.cgm.getObjCClass(cgf, classDecl, loc);

    CallArgList args;
    const ParmVarDecl *argDecl = *boxingMethod->param_begin();
    QualType argTy = argDecl->getType().getUnqualifiedType();
    const QualType valueTy(subExpr->getType().getCanonicalType());
    if (valueTy->isObjCBoxableRecordType()) {
      Address temporary = cgf.createMemTemp(subExpr->getType(), loc, "boxed.record");
      cgf.emitAnyExprToMem(subExpr, temporary, Qualifiers(), /*isInit=*/true);
      mlir::Value bitcast =
          builder.createBitcast(loc, temporary.getPointer(), cgf.convertType(argTy));
      args.add(RValue::get(bitcast), argTy);

      std::string encoding;
      cgf.getContext().getObjCEncodingForType(valueTy, encoding);
      cir::GlobalOp global = cgf.cgm.getGlobalForConstantCString(loc, encoding);
      mlir::Value encodingAddr = builder.createGetGlobal(loc, global);

      const ParmVarDecl *encodingDecl = boxingMethod->parameters()[1];
      QualType encodingTy = encodingDecl->getType().getUnqualifiedType();
      args.add(RValue::get(builder.createBitcast(loc, encodingAddr,
                                                 cgf.convertType(encodingTy))),
               encodingTy);
    } else {
      args.add(cgf.emitAnyExpr(subExpr), argTy);
    }

    QualType objCIdTy = cgf.getContext().getObjCIdType();
    QualType objCSelTy = cgf.getContext().getObjCSelType();
    CallArgList actualArgs;
    actualArgs.add(RValue::get(receiver), objCIdTy);
    actualArgs.add(RValue::get(cgf.cgm.getObjCSelector(cgf,
                                                       boxingMethod->getSelector(),
                                                       loc)),
                   objCSelTy);
    actualArgs.addFrom(args);

    llvm::SmallVector<QualType, 4> argTypes;
    argTypes.push_back(objCIdTy);
    argTypes.push_back(objCSelTy);
    for (const CallArg &arg : llvm::drop_begin(actualArgs, 2))
      argTypes.push_back(arg.ty);

    QualType msgFnTy = cgf.getContext().getFunctionType(
        boxingMethod->getReturnType(), argTypes,
        FunctionProtoType::ExtProtoInfo());
    auto msgProto = CanQual<FunctionProtoType>::CreateUnsafe(
        cgf.getContext().getCanonicalType(msgFnTy));
    const CIRGenFunctionInfo &funcInfo =
        cgf.getTypes().arrangeFreeFunctionType(msgProto);
    cir::FuncType exactFnTy = cgf.getTypes().getFunctionType(funcInfo);
    cir::FuncOp msgSendFn = cgf.cgm.getObjCMsgSendFunc();
    mlir::Value msgSendAddr = cir::GetGlobalOp::create(
        builder, loc, builder.getPointerTo(msgSendFn.getFunctionType()),
        msgSendFn.getSymNameAttr());
    mlir::Value msgSendPtr =
        builder.createBitcast(loc, msgSendAddr, builder.getPointerTo(exactFnTy));

    RValue result = cgf.emitCall(
        funcInfo, CIRGenCallee::forDirect(msgSendPtr.getDefiningOp()),
        ReturnValueSlot(), actualArgs, /*callOrTryCall=*/nullptr, loc);
    mlir::Value scalar = result.getValue();
    mlir::Type exprTy = cgf.convertType(e->getType());
    if (scalar.getType() != exprTy)
      scalar = builder.createBitcast(loc, scalar, exprTy);
    return scalar;
  }
  mlir::Value VisitObjCArrayLiteral(ObjCArrayLiteral *e) {
    return emitObjCCollectionLiteral(cgf, e, e->getArrayWithObjectsMethod());
  }
  mlir::Value VisitObjCDictionaryLiteral(ObjCDictionaryLiteral *e) {
    return emitObjCCollectionLiteral(cgf, e, e->getDictWithObjectsMethod());
  }

  mlir::Value VisitAsTypeExpr(AsTypeExpr *e) {
    mlir::Type srcTy = cgf.convertType(e->getSrcExpr()->getType());
    mlir::Type dstTy = cgf.convertType(e->getType());

    mlir::Value src = cgf.emitScalarExpr(e->getSrcExpr());
    mlir::Location loc = cgf.getLoc(e->getSourceRange());

    // Pointer-to-pointer: use address_space cast or bitcast.
    if (mlir::isa<cir::PointerType>(srcTy) &&
        mlir::isa<cir::PointerType>(dstTy)) {
      auto srcPtrTy = mlir::cast<cir::PointerType>(srcTy);
      auto dstPtrTy = mlir::cast<cir::PointerType>(dstTy);
      if (srcPtrTy.getAddrSpace() != dstPtrTy.getAddrSpace())
        return builder.createAddrSpaceCast(loc, src, dstTy);
      return builder.createBitcast(src, dstTy);
    }

    unsigned numElementsSrc =
        mlir::isa<cir::VectorType>(srcTy)
            ? mlir::cast<cir::VectorType>(srcTy).getSize()
            : 0;
    unsigned numElementsDst =
        mlir::isa<cir::VectorType>(dstTy)
            ? mlir::cast<cir::VectorType>(dstTy).getSize()
            : 0;

    // Use bit vector expansion for ext_vector_type boolean vectors.
    if (e->getType()->isExtVectorBoolType()) {
      cgf.cgm.errorNYI(e->getSourceRange(),
                        "AsTypeExpr with ext_vector_bool destination");
      return builder.getConstant(loc, cir::PoisonAttr::get(dstTy));
    }

    // Going from vec3 to non-vec3 is a special case and requires a shuffle
    // vector to get a vec4, then a bitcast if the target type is different.
    if (numElementsSrc == 3 && numElementsDst != 3) {
      // shufflevector <3 x T> -> <4 x T> with mask {0, 1, 2, -1}
      llvm::SmallVector<int64_t, 4> mask = {0, 1, 2, -1};
      src = builder.createVecShuffle(loc, src, mask);
      if (src.getType() != dstTy)
        src = builder.createBitcast(src, dstTy);
      return src;
    }

    // Going from non-vec3 to vec3 is a special case and requires a bitcast
    // to vec4 if the original type is not vec4, then a shuffle vector to
    // get a vec3.
    if (numElementsSrc != 3 && numElementsDst == 3) {
      auto dstVecTy = mlir::cast<cir::VectorType>(dstTy);
      auto vec4Ty = cir::VectorType::get(dstVecTy.getElementType(), 4);
      if (src.getType() != vec4Ty)
        src = builder.createBitcast(src, vec4Ty);
      llvm::SmallVector<int64_t, 3> mask = {0, 1, 2};
      src = builder.createVecShuffle(loc, src, mask);
      return src;
    }

    // Same-size non-pointer: use bitcast.
    return builder.createBitcast(src, dstTy);
  }

  mlir::Value VisitAtomicExpr(AtomicExpr *e) {
    return cgf.emitAtomicExpr(e).getValue();
  }
};

static mlir::Value emitARCUnsafeUnretainedScalarExpr(CIRGenFunction &cgf,
                                                     const Expr *expr) {
  return cgf.emitARCUnsafeUnretainedScalarExpr(expr);
}

LValue ScalarExprEmitter::emitCompoundAssignLValue(
    const CompoundAssignOperator *e,
    mlir::Value (ScalarExprEmitter::*func)(const BinOpInfo &),
    mlir::Value &result) {
  if (e->getComputationResultType()->isAnyComplexType())
    return cgf.emitScalarCompoundAssignWithComplex(e, result);

  QualType lhsTy = e->getLHS()->getType();
  BinOpInfo opInfo;

  // Emit the RHS first.  __block variables need to have the rhs evaluated
  // first, plus this should improve codegen a little.

  // Work around a Sema substitution bug where SubstNonTypeTemplateParmExpr
  // retains its un-substituted type, leaving computationLHSType /
  // computationResultType as <dependent type> even after instantiation.
  // Fall back to the concrete LHS type in that case.
  QualType compLHSTy = e->getComputationLHSType();
  QualType compResultTy = e->getComputationResultType();
  if (compLHSTy->isDependentType())
    compLHSTy = lhsTy;
  if (compResultTy->isDependentType())
    compResultTy = lhsTy;

  QualType promotionTypeCR = getPromotionType(compResultTy);
  if (promotionTypeCR.isNull())
    promotionTypeCR = compResultTy;

  QualType promotionTypeLHS = getPromotionType(compLHSTy);
  QualType promotionTypeRHS = getPromotionType(e->getRHS()->getType());

  if (!promotionTypeRHS.isNull())
    opInfo.rhs = cgf.emitPromotedScalarExpr(e->getRHS(), promotionTypeRHS);
  else
    opInfo.rhs = Visit(e->getRHS());

  opInfo.fullType = promotionTypeCR;
  opInfo.compType = opInfo.fullType;
  if (const auto *vecType = dyn_cast_or_null<VectorType>(opInfo.fullType))
    opInfo.compType = vecType->getElementType();
  opInfo.opcode = e->getOpcode();
  opInfo.fpfeatures = e->getFPFeaturesInEffect(cgf.getLangOpts());
  opInfo.e = e;
  opInfo.loc = e->getSourceRange();

  // Load/convert the LHS
  // OGCG calls EmitCheckedLValue(E->getLHS(), TCK_Store) here for sanitizer
  // checks on the store target.
  LValue lhsLV = emitCheckedLValue(e->getLHS(), CIRGenFunction::TCK_Store);

  if (const AtomicType *atomicTy = lhsTy->getAs<AtomicType>()) {
    QualType valueTy = atomicTy->getValueType();
    // Fast path for integer atomics with atomicrmw-compatible ops.
    // Matches OGCG (CGExprScalar.cpp:3958-4010).
    // Skip this fast path when unsigned overflow sanitizer is active on
    // unsigned types — we need the CAS loop to check for overflow.
    if (!valueTy->isBooleanType() && valueTy->isIntegerType() &&
        !(valueTy->isUnsignedIntegerType() &&
          cgf.sanOpts.has(SanitizerKind::UnsignedIntegerOverflow)) &&
        cgf.getLangOpts().getSignedOverflowBehavior() !=
            LangOptions::SOB_Trapping) {
      cir::AtomicFetchKind atomicOp;
      cir::BinOpKind binOp;
      bool hasAtomicOp = true;
      switch (opInfo.opcode) {
      // No atomicrmw operands for *, %, /, <<, >>
      case BO_MulAssign:
      case BO_DivAssign:
      case BO_RemAssign:
      case BO_ShlAssign:
      case BO_ShrAssign:
        hasAtomicOp = false;
        break;
      case BO_AddAssign:
        atomicOp = cir::AtomicFetchKind::Add;
        binOp = cir::BinOpKind::Add;
        break;
      case BO_SubAssign:
        atomicOp = cir::AtomicFetchKind::Sub;
        binOp = cir::BinOpKind::Sub;
        break;
      case BO_AndAssign:
        atomicOp = cir::AtomicFetchKind::And;
        binOp = cir::BinOpKind::And;
        break;
      case BO_XorAssign:
        atomicOp = cir::AtomicFetchKind::Xor;
        binOp = cir::BinOpKind::Xor;
        break;
      case BO_OrAssign:
        atomicOp = cir::AtomicFetchKind::Or;
        binOp = cir::BinOpKind::Or;
        break;
      default:
        llvm_unreachable("Invalid compound assignment type");
      }
      if (hasAtomicOp) {
        mlir::Location loc = cgf.getLoc(e->getSourceRange());
        // Convert the RHS to the atomic value type.
        mlir::Value amt =
            emitScalarConversion(opInfo.rhs, e->getRHS()->getType(), lhsTy,
                                 e->getExprLoc());
        // Emit atomicrmw — returns the old value.
        auto oldVal = cir::AtomicFetchOp::create(
            builder, loc, lhsLV.getPointer(), amt, atomicOp,
            cir::MemOrder::SequentiallyConsistent,
            builder.getI64IntegerAttr(lhsLV.getAlignment().getQuantity()),
            /*is_volatile=*/lhsLV.isVolatile(), /*fetchFirst=*/true,
            /*sync_scope=*/nullptr);
        // Compute the new value: old op amt.
        result = cir::BinOp::create(builder, loc, oldVal.getType(), binOp,
                                    oldVal, amt, {}, {}, {}, {}, {}, {});
        return lhsLV;
      }
    }
    // Slow path (CAS loop) for *, /, %, <<, >>, non-integer, and bool atomics.
    // Matches OGCG (CGExprScalar.cpp:4012-4061).
    //
    // The pattern: atomically load the current value, compute the new value
    // using the binary operator, then try a compare-and-swap. If the CAS
    // fails (another thread changed the value), retry with the value
    // returned by the CAS.
    {
      mlir::Location loc = cgf.getLoc(e->getSourceRange());
      SourceLocation srcLoc = e->getExprLoc();

      // FIXME: For floating point types, we should be saving and restoring
      // the floating point environment in the loop.

      // Initial atomic load.
      mlir::Value initialVal = emitLoadOfLValue(lhsLV, srcLoc);
      initialVal = cgf.emitToMemory(initialVal, valueTy);

      // Allocas for CAS loop state (replaces PHI nodes in OGCG).
      mlir::Type valCIRTy = cgf.convertType(valueTy);
      CharUnits valAlign =
          cgf.getContext().getTypeAlignInChars(valueTy);

      Address expectedAddr =
          cgf.createTempAlloca(valCIRTy, valAlign, loc, "atomic.expected");
      builder.createStore(loc, initialVal, expectedAddr);

      Address resultAddr =
          cgf.createTempAlloca(valCIRTy, valAlign, loc, "atomic.result");

      mlir::Type boolCIRTy = builder.getBoolTy();
      CharUnits boolAlign = CharUnits::One();
      Address successAddr =
          cgf.createTempAlloca(boolCIRTy, boolAlign, loc, "atomic.success");

      mlir::IntegerAttr cmpxchgAlign = cgf.getI64AttrCached(
          lhsLV.getAddress().getAlignment().getAsAlign().value());

      builder.createDoWhile(
          loc,
          /*condBuilder=*/
          [&](mlir::OpBuilder &b, mlir::Location condLoc) {
            mlir::Value success = builder.createLoad(condLoc, successAddr);
            // Continue looping while !success.
            mlir::Value shouldContinue = builder.createNot(success);
            builder.createCondition(shouldContinue);
          },
          /*bodyBuilder=*/
          [&](mlir::OpBuilder &b, mlir::Location bodyLoc) {
            // Load expected value.
            mlir::Value expected = builder.createLoad(bodyLoc, expectedAddr);

            // Set up opInfo with expected as LHS and apply conversions.
            opInfo.lhs = expected;
            if (!promotionTypeLHS.isNull())
              opInfo.lhs = emitScalarConversion(opInfo.lhs, valueTy,
                                                promotionTypeLHS, srcLoc);
            else
              opInfo.lhs = emitScalarConversion(
                  opInfo.lhs, valueTy, compLHSTy, srcLoc);

            // Binary operation.
            mlir::Value newVal = (this->*func)(opInfo);

            // Convert result back to LHS type.
            assert(!cir::MissingFeatures::bitfieldCompoundAssignSanOpts());
            newVal = emitScalarConversion(newVal, promotionTypeCR, valueTy,
                                          srcLoc,
                                          ScalarConversionOpts(cgf.sanOpts));

            // Store the result for the outer scope.
            builder.createStore(bodyLoc, newVal, resultAddr);

            // Atomic compare-and-exchange.
            auto cmpxchgOp = cir::AtomicCmpXchgOp::create(
                builder, bodyLoc, expected.getType(), builder.getBoolTy(),
                lhsLV.getPointer(), expected, newVal,
                cir::MemOrder::SequentiallyConsistent,
                cir::MemOrder::SequentiallyConsistent,
                /*alignment=*/cmpxchgAlign,
                /*weak=*/false,
                /*is_volatile=*/lhsLV.isVolatileQualified(),
                /*sync_scope=*/cir::SyncScopeKindAttr{});

            // Store old value for next iteration.
            mlir::Value oldVal = cmpxchgOp.getOld();
            oldVal = cgf.emitToMemory(oldVal, valueTy);
            builder.createStore(bodyLoc, oldVal, expectedAddr);

            // Store success flag.
            builder.createStore(bodyLoc, cmpxchgOp.getSuccess(), successAddr);

            builder.createYield(bodyLoc);
          });

      // Load the final result from the alloca.
      result = builder.createLoad(loc, resultAddr);
      return lhsLV;
    }
  }

  opInfo.lhs = emitLoadOfLValue(lhsLV, e->getExprLoc());

  CIRGenFunction::SourceLocRAIIObject sourceloc{
      cgf, cgf.getLoc(e->getSourceRange())};
  SourceLocation loc = e->getExprLoc();
  if (!promotionTypeLHS.isNull())
    opInfo.lhs = emitScalarConversion(opInfo.lhs, lhsTy, promotionTypeLHS, loc);
  else
    opInfo.lhs = emitScalarConversion(opInfo.lhs, lhsTy,
                                      compLHSTy, loc);

  // For a matrix compound assignment with a scalar operand (e.g. `m += s`),
  // splat the scalar to the matrix type so the binary operator sees same-typed
  // operands. This mirrors emitBinOps and OGCG's scalar-splat behavior; without
  // it the matrix LHS and scalar RHS reach cir.binop with mismatched types.
  if (opInfo.fullType->isConstantMatrixType()) {
    auto matrixTy =
        mlir::cast<cir::VectorType>(cgf.convertType(opInfo.fullType));
    mlir::Location matrixLoc = cgf.getLoc(e->getExprLoc());
    auto splatMatrixScalar = [&](mlir::Value scalar) {
      auto zeroIndex = builder.getUInt64(0, matrixLoc);
      auto seed = cir::VecInsertOp::create(
          builder, matrixLoc,
          builder.getConstant(matrixLoc, cir::PoisonAttr::get(matrixTy)),
          scalar, zeroIndex);
      SmallVector<int64_t, 16> mask(matrixTy.getSize(), 0);
      return builder.createVecShuffle(matrixLoc, seed, mask).getResult();
    };
    if (opInfo.lhs && opInfo.lhs.getType() == matrixTy.getElementType())
      opInfo.lhs = splatMatrixScalar(opInfo.lhs);
    if (opInfo.rhs && opInfo.rhs.getType() == matrixTy.getElementType())
      opInfo.rhs = splatMatrixScalar(opInfo.rhs);
  }

  // Expand the binary operator.
  // CIR does not need a CGFPOptionsRAII guard here because fast-math flags
  // are passed per-op directly in the individual emit functions.
  result = (this->*func)(opInfo);

  // Convert the result back to the LHS type,
  // potentially with Implicit Conversion sanitizer check.
  // OGCG skips implicit conversion sanitizer checks for bitfield LHS
  // (uses default ScalarConversionOpts) but CIR always passes sanOpts.
  assert(!cir::MissingFeatures::bitfieldCompoundAssignSanOpts());
  result = emitScalarConversion(result, promotionTypeCR, lhsTy, loc,
                                ScalarConversionOpts(cgf.sanOpts));

  // Store the result value into the LHS lvalue. Bit-fields are handled
  // specially because the result is altered by the store, i.e., [C99 6.5.16p1]
  // 'An assignment expression has the value of the left operand after the
  // assignment...'.
  if (lhsLV.isBitField()) {
    mlir::Value srcVal = result;
    result = cgf.emitStoreThroughBitfieldLValue(RValue::get(result), lhsLV);
    emitBitfieldConversionCheckImpl(cgf, builder, cgf.getLoc(e->getExprLoc()),
                                    e->getExprLoc(), srcVal,
                                    e->getComputationResultType(), result,
                                    e->getLHS()->getType(),
                                    lhsLV.getBitFieldInfo());
  } else {
    cgf.emitStoreThroughLValue(RValue::get(result), lhsLV);
  }

  if (cgf.getLangOpts().OpenMP)
    cgf.checkAndEmitOpenMPLastprivateConditional(e->getLHS());

  return lhsLV;
}

mlir::Value ScalarExprEmitter::emitComplexToScalarConversion(mlir::Location lov,
                                                             mlir::Value value,
                                                             CastKind kind,
                                                             QualType destTy) {
  cir::CastKind castOpKind;
  switch (kind) {
  case CK_FloatingComplexToReal:
    castOpKind = cir::CastKind::float_complex_to_real;
    break;
  case CK_IntegralComplexToReal:
    castOpKind = cir::CastKind::int_complex_to_real;
    break;
  case CK_FloatingComplexToBoolean:
    castOpKind = cir::CastKind::float_complex_to_bool;
    break;
  case CK_IntegralComplexToBoolean:
    castOpKind = cir::CastKind::int_complex_to_bool;
    break;
  default:
    llvm_unreachable("invalid complex-to-scalar cast kind");
  }

  return builder.createCast(lov, castOpKind, value, cgf.convertType(destTy));
}

mlir::Value ScalarExprEmitter::emitPromoted(const Expr *e,
                                            QualType promotionType) {
  e = e->IgnoreParens();
  if (const auto *bo = dyn_cast<BinaryOperator>(e)) {
    switch (bo->getOpcode()) {
#define HANDLE_BINOP(OP)                                                       \
  case BO_##OP:                                                                \
    return emit##OP(emitBinOps(bo, promotionType));
      HANDLE_BINOP(Add)
      HANDLE_BINOP(Sub)
      HANDLE_BINOP(Mul)
      HANDLE_BINOP(Div)
#undef HANDLE_BINOP
    default:
      break;
    }
  } else if (const auto *uo = dyn_cast<UnaryOperator>(e)) {
    switch (uo->getOpcode()) {
    case UO_Imag:
    case UO_Real:
      return VisitRealImag(uo, promotionType);
    case UO_Minus:
      return emitUnaryPlusOrMinus(uo, cir::UnaryOpKind::Minus, promotionType);
    case UO_Plus:
      return emitUnaryPlusOrMinus(uo, cir::UnaryOpKind::Plus, promotionType);
    default:
      break;
    }
  }
  mlir::Value result = Visit(const_cast<Expr *>(e));
  if (result) {
    if (!promotionType.isNull())
      return emitPromotedValue(result, promotionType);
    return emitUnPromotedValue(result, e->getType());
  }
  return result;
}

mlir::Value ScalarExprEmitter::emitCompoundAssign(
    const CompoundAssignOperator *e,
    mlir::Value (ScalarExprEmitter::*func)(const BinOpInfo &)) {
  bool ignore = std::exchange(ignoreResultAssign, false);
  CIRGenFunction::ApplySourceAtomGroup atomGroup(cgf);
  mlir::Value rhs;
  LValue lhs = emitCompoundAssignLValue(e, func, rhs);

  // If the result is clearly ignored, return now.
  if (ignore)
    return {};

  // The result of an assignment in C is the assigned r-value.
  if (!cgf.getLangOpts().CPlusPlus)
    return rhs;

  // If the lvalue is non-volatile, return the computed value of the assignment.
  if (!lhs.isVolatile())
    return rhs;

  // Otherwise, reload the value.
  return emitLoadOfLValue(lhs, e->getExprLoc());
}

mlir::Value ScalarExprEmitter::VisitExprWithCleanups(ExprWithCleanups *e) {
  mlir::Location scopeLoc = cgf.getLoc(e->getSourceRange());
  mlir::OpBuilder &builder = cgf.builder;

  auto scope = cir::ScopeOp::create(
      builder, scopeLoc,
      /*scopeBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Type &yieldTy, mlir::Location loc) {
        CIRGenFunction::SourceLocRAIIObject exprLoc{
            cgf, cgf.getLoc(e->getSubExpr()->getExprLoc())};
        CIRGenFunction::LexicalScope lexScope{cgf, loc,
                                              builder.getInsertionBlock()};
        mlir::Value scopeYieldVal = Visit(e->getSubExpr());
        if (scopeYieldVal) {
          // Defend against dominance problems caused by jumps out of expression
          // evaluation through the shared cleanup block.
          lexScope.forceCleanup();
          cir::YieldOp::create(builder, loc, scopeYieldVal);
          yieldTy = scopeYieldVal.getType();
        }
      });

  return scope.getNumResults() > 0 ? scope->getResult(0) : nullptr;
}

} // namespace

void clang::CIRGen::emitBitfieldConversionCheck(
    CIRGenFunction &cgf, CIRGenBuilderTy &builder, mlir::Location loc,
    SourceLocation sourceLoc, mlir::Value src, QualType srcType, mlir::Value dst,
    QualType dstType, const CIRGenBitFieldInfo &info) {
  emitBitfieldConversionCheckImpl(cgf, builder, loc, sourceLoc, src, srcType,
                                  dst, dstType, info);
}

mlir::Value CIRGenFunction::emitObjCClassRealization(mlir::Location loc,
                                                     mlir::Value classObject) {
  QualType objCIdTy = getContext().getObjCIdType();
  QualType objCSelTy = getContext().getObjCSelType();
  QualType voidPtrTy = getContext().getPointerType(getContext().VoidTy);
  const IdentifierInfo *selfIIs[] = {&getContext().Idents.get("self")};
  Selector selfSel = getContext().Selectors.getSelector(0, selfIIs);

  llvm::SmallVector<QualType, 2> argTypes{objCIdTy, objCSelTy};
  QualType msgFnTy = getContext().getFunctionType(
      objCIdTy, argTypes, FunctionProtoType::ExtProtoInfo());
  auto msgProto = CanQual<FunctionProtoType>::CreateUnsafe(
      getContext().getCanonicalType(msgFnTy));
  const CIRGenFunctionInfo &funcInfo =
      getTypes().arrangeFreeFunctionType(msgProto);
  cir::FuncType exactFnTy = getTypes().getFunctionType(funcInfo);

  CallArgList args;
  args.add(RValue::get(classObject), objCIdTy);

  mlir::Value msgSendPtr;
  bool useDarwinFixupDispatch =
      getLangOpts().ObjCRuntime.isNeXTFamily() &&
      isDarwinVTableDispatchedSelector(*this, selfSel);
  if (useDarwinFixupDispatch) {
    cir::FuncOp msgSendFn = getObjCDarwinFixupMessageSendFunc(
        *this, /*isSuperMessage=*/false, /*usesStructReturn=*/false,
        /*usesFPRet=*/false);
    cir::GlobalOp messageRefGV =
        getObjCDarwinMessageRefGlobal(*this, loc, selfSel, msgSendFn);
    CharUnits messageRefAlign = getPointerAlign();
    mlir::Value messageRefAddr = builder.createGetGlobal(loc, messageRefGV);
    mlir::Value messengerField = builder.createGetMember(
        loc, builder.getPointerTo(cgm.voidPtrTy), messageRefAddr, "", 0);
    mlir::Value messengerPtr = builder.createLoad(
        loc, Address(messengerField, cgm.voidPtrTy,
                     messageRefAlign.alignmentAtOffset(CharUnits::Zero())));
    msgSendPtr =
        builder.createBitcast(loc, messengerPtr, builder.getPointerTo(exactFnTy));

    mlir::Value messageRefArg = messageRefAddr;
    if (messageRefArg.getType() != cgm.voidPtrTy)
      messageRefArg = builder.createBitcast(loc, messageRefArg, cgm.voidPtrTy);
    args.add(RValue::get(messageRefArg), voidPtrTy);
  } else {
    cir::FuncOp msgSendFn = cgm.getObjCMsgSendFunc();
    mlir::Value msgSendAddr = cir::GetGlobalOp::create(
        builder, loc, builder.getPointerTo(msgSendFn.getFunctionType()),
        msgSendFn.getSymNameAttr());
    msgSendPtr =
        builder.createBitcast(loc, msgSendAddr, builder.getPointerTo(exactFnTy));
    args.add(RValue::get(cgm.getObjCSelector(*this, selfSel, loc)), objCSelTy);
  }

  RValue realizedClass = emitCall(
      funcInfo, CIRGenCallee::forDirect(msgSendPtr.getDefiningOp()),
      ReturnValueSlot(), args, /*callOp=*/nullptr, loc);

  mlir::Value scalar = realizedClass.getValue();
  mlir::Type exprTy = convertType(objCIdTy);
  if (scalar.getType() != exprTy)
    scalar = builder.createBitcast(loc, scalar, exprTy);
  return scalar;
}

mlir::Value CIRGenFunction::emitARCUnsafeUnretainedScalarExpr(const Expr *expr) {
  if (const auto *cleanups = dyn_cast<ExprWithCleanups>(expr)) {
    RunCleanupsScope scope(*this);
    return emitARCUnsafeUnretainedScalarExpr(cleanups->getSubExpr());
  }

  expr = expr->IgnoreParens();

  if (const auto *castExpr = dyn_cast<CastExpr>(expr)) {
    switch (castExpr->getCastKind()) {
    case CK_NoOp:
      return emitARCUnsafeUnretainedScalarExpr(castExpr->getSubExpr());
    case CK_CPointerToObjCPointerCast:
    case CK_BlockPointerToObjCPointerCast:
    case CK_AnyPointerToBlockPointerCast:
    case CK_BitCast: {
      mlir::Value value =
          emitARCUnsafeUnretainedScalarExpr(castExpr->getSubExpr());
      return getBuilder().createBitcast(value, convertType(expr->getType()));
    }
    case CK_LValueToRValue:
      return emitScalarExpr(expr);
    case CK_ARCConsumeObject: {
      mlir::Value value = emitScalarExpr(castExpr->getSubExpr());
      emitARCConsumeObject(value, getLoc(castExpr->getExprLoc()));
      return value;
    }
    case CK_ARCExtendBlockObject:
      return emitARCExtendBlockObject(castExpr->getSubExpr());
    case CK_ARCReclaimReturnedObject:
      return emitARCUnsafeClaimReturnedObject(castExpr->getSubExpr());
    default:
      return emitScalarExpr(expr);
    }
  }

  if (const auto *binaryOp = dyn_cast<BinaryOperator>(expr)) {
    switch (binaryOp->getOpcode()) {
    case BO_Comma:
      emitIgnoredExpr(binaryOp->getLHS());
      return emitARCUnsafeUnretainedScalarExpr(binaryOp->getRHS());
    case BO_Assign:
      if (binaryOp->getLHS()->getType().getObjCLifetime() ==
          Qualifiers::OCL_ExplicitNone) {
        mlir::Value rhs =
            emitARCUnsafeUnretainedScalarExpr(binaryOp->getRHS());
        LValue lhs = ScalarExprEmitter(*this, getBuilder())
                         .emitCheckedLValue(binaryOp->getLHS(), TCK_Store);
        if (lhs.isBitField()) {
          mlir::Value srcVal = rhs;
          rhs = emitStoreThroughBitfieldLValue(RValue::get(rhs), lhs);
          emitBitfieldConversionCheckImpl(
              *this, getBuilder(), getLoc(binaryOp->getExprLoc()),
              binaryOp->getExprLoc(), srcVal, binaryOp->getRHS()->getType(),
              rhs, binaryOp->getLHS()->getType(), lhs.getBitFieldInfo());
        } else {
          SourceLocRAIIObject loc{*this, getLoc(binaryOp->getSourceRange())};
          emitStoreThroughLValue(RValue::get(rhs), lhs);
        }
        return rhs;
      }
      break;
    default:
      break;
    }
  }

  return emitScalarExpr(expr);
}

static RValue adjustObjCObjectType(CIRGenFunction &cgf, mlir::Location loc,
                                   QualType expectedType, RValue result) {
  if (!expectedType->isObjCRetainableType())
    return result;

  mlir::Value value = result.getValue();
  mlir::Type expectedIRType = cgf.convertType(expectedType);
  if (value.getType() == expectedIRType)
    return result;

  return RValue::get(cgf.getBuilder().createBitcast(loc, value, expectedIRType));
}

static bool hasDarwinNonFragileObjCRuntime(CIRGenFunction &cgf) {
  const ObjCRuntime runtime = cgf.getLangOpts().ObjCRuntime;
  return runtime.isNeXTFamily() && runtime.isNonFragile();
}

static bool hasDarwinObjCRuntime(CIRGenFunction &cgf) {
  const ObjCRuntime runtime = cgf.getLangOpts().ObjCRuntime;
  return hasDarwinNonFragileObjCRuntime(cgf) ||
         (runtime.isNeXTFamily() && !runtime.isNonFragile() &&
          cgf.getTarget().getTriple().isOSDarwin() &&
          cgf.getTarget().getTriple().isOSBinFormatMachO());
}

static void mergeFuncExtraAttr(cir::FuncOp fn, llvm::StringRef name,
                               mlir::Attribute value) {
  mlir::NamedAttrList attrs;
  if (auto existing = fn.getExtraAttrsAttr())
    attrs = mlir::NamedAttrList{existing.getElements().getValue()};
  attrs.set(name, value);
  fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
      attrs.getDictionary(fn.getContext())));
}

static const CIRGenFunctionInfo &
getObjCRuntimeValueOperationFnInfo(CIRGenFunction &cgf, QualType returnType) {
  QualType argType = cgf.getContext().getPointerType(cgf.getContext().VoidTy);
  QualType argTypes[] = {argType};
  QualType fnType = cgf.getContext().getFunctionType(
      returnType, argTypes, FunctionProtoType::ExtProtoInfo());
  auto proto = CanQual<FunctionProtoType>::CreateUnsafe(
      cgf.getContext().getCanonicalType(fnType));
  return cgf.getTypes().arrangeFreeFunctionType(proto);
}

static RValue emitObjCRuntimeValueOperation(CIRGenFunction &cgf,
                                            mlir::Location loc,
                                            mlir::Value value,
                                            QualType resultType,
                                            llvm::StringRef fnName,
                                            bool tailCall = false) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Type expectedIRType = cgf.convertType(resultType);
  if (auto constOp = value.getDefiningOp<cir::ConstantOp>()) {
    if (builder.isNullValue(constOp.getValue())) {
      if (value.getType() != expectedIRType)
        value = builder.createBitcast(loc, value, expectedIRType);
      return RValue::get(value);
    }
  }

  mlir::Type ptrTy = builder.getVoidPtrTy();
  cir::FuncType fnTy = cir::FuncType::get({ptrTy}, ptrTy);
  cir::FuncOp fn = cgf.cgm.createRuntimeFunction(fnTy, fnName);
  if (fnName == "objc_retain")
    mergeFuncExtraAttr(fn, "nonlazybind",
                       mlir::UnitAttr::get(&cgf.getMLIRContext()));

  QualType runtimePtrTy =
      cgf.getContext().getPointerType(cgf.getContext().VoidTy);
  CallArgList args;
  if (value.getType() != ptrTy)
    value = builder.createBitcast(loc, value, ptrTy);
  args.add(RValue::get(value), runtimePtrTy);

  cir::CIRCallOpInterface callOp;
  RValue result = cgf.emitCall(
      getObjCRuntimeValueOperationFnInfo(cgf, runtimePtrTy),
      CIRGenCallee::forDirect(fn), ReturnValueSlot(), args, &callOp, loc);
  if (tailCall && callOp)
    callOp->setAttr("tail", mlir::UnitAttr::get(&cgf.getMLIRContext()));

  mlir::Value resultValue = result.getValue();
  if (resultValue.getType() != expectedIRType)
    resultValue = builder.createBitcast(loc, resultValue, expectedIRType);
  return RValue::get(resultValue);
}

static RValue emitObjCRuntimeVoidOperation(CIRGenFunction &cgf,
                                           mlir::Location loc,
                                           mlir::Value value,
                                           llvm::StringRef fnName) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  if (auto constOp = value.getDefiningOp<cir::ConstantOp>()) {
    if (builder.isNullValue(constOp.getValue()))
      return RValue::getIgnored();
  }

  auto ptrTy = builder.getVoidPtrTy();
  cir::FuncType fnTy =
      cir::FuncType::get({ptrTy}, cir::VoidType::get(builder.getContext()));
  cir::FuncOp fn = cgf.cgm.createRuntimeFunction(fnTy, fnName);
  if (fnName == "objc_release")
    mergeFuncExtraAttr(fn, "nonlazybind",
                       mlir::UnitAttr::get(&cgf.getMLIRContext()));

  QualType runtimePtrTy =
      cgf.getContext().getPointerType(cgf.getContext().VoidTy);
  CallArgList args;
  if (value.getType() != ptrTy)
    value = builder.createBitcast(loc, value, ptrTy);
  args.add(RValue::get(value), runtimePtrTy);

  cgf.emitCall(getObjCRuntimeValueOperationFnInfo(cgf, cgf.getContext().VoidTy),
               CIRGenCallee::forDirect(fn), ReturnValueSlot(), args,
               /*callOp=*/nullptr, loc);
  return RValue::getIgnored();
}

static std::optional<RValue> tryEmitSpecializedObjCMessageSend(
    CIRGenFunction &cgf, mlir::Location loc, QualType resultType,
    mlir::Value receiver, const CallArgList &args, Selector sel,
    bool isClassMessage, bool isSuperMessage) {
  CIRGenModule &cgm = cgf.cgm;
  if (isSuperMessage || !cgm.getCodeGenOpts().ObjCConvertMessagesToRuntimeCalls)
    return std::nullopt;

  const ObjCRuntime runtime = cgm.getLangOpts().ObjCRuntime;
  switch (sel.getMethodFamily()) {
  case OMF_alloc:
    if (isClassMessage && runtime.shouldUseRuntimeFunctionsForAlloc() &&
        resultType->isObjCObjectPointerType()) {
      if (sel.isUnarySelector() && sel.getNameForSlot(0) == "alloc")
        return emitObjCRuntimeValueOperation(cgf, loc, receiver, resultType,
                                             "objc_alloc");

      if (sel.isKeywordSelector() && sel.getNumArgs() == 1 && args.size() == 1 &&
          args.front().ty->isPointerType() &&
          sel.getNameForSlot(0) == "allocWithZone") {
        mlir::Value arg = args.front().getKnownRValue().getValue();
        if (auto constOp = arg.getDefiningOp<cir::ConstantOp>()) {
          if (cgf.getBuilder().isNullValue(constOp.getValue()))
            return emitObjCRuntimeValueOperation(
                cgf, loc, receiver, resultType, "objc_allocWithZone");
        }
      }
    }
    break;

  case OMF_autorelease:
    if (resultType->isObjCObjectPointerType() &&
        cgm.getLangOpts().getGC() == LangOptions::NonGC &&
        runtime.shouldUseARCFunctionsForRetainRelease()) {
      return emitObjCRuntimeValueOperation(cgf, loc, receiver, resultType,
                                           "objc_autorelease",
                                           /*tailCall=*/true);
    }
    break;

  case OMF_retain:
    if (resultType->isObjCObjectPointerType() &&
        cgm.getLangOpts().getGC() == LangOptions::NonGC &&
        runtime.shouldUseARCFunctionsForRetainRelease()) {
      return emitObjCRuntimeValueOperation(cgf, loc, receiver, resultType,
                                           "objc_retain");
    }
    break;

  case OMF_release:
    if (resultType->isVoidType() &&
        cgm.getLangOpts().getGC() == LangOptions::NonGC &&
        runtime.shouldUseARCFunctionsForRetainRelease()) {
      return emitObjCRuntimeVoidOperation(cgf, loc, receiver, "objc_release");
    }
    break;

  default:
    break;
  }

  return std::nullopt;
}

RValue CIRGenFunction::emitObjCMessageExpr(const ObjCMessageExpr *e,
                                           ReturnValueSlot returnValue) {
  mlir::Location loc = getLoc(e->getSourceRange());
  const ObjCRuntime runtime = getLangOpts().ObjCRuntime;
  bool useDarwinObjCRuntime = hasDarwinObjCRuntime(*this);
  bool useDarwinNonFragileObjCRuntime = hasDarwinNonFragileObjCRuntime(*this);
  bool useGNUObjCRuntime =
      runtime.getKind() == ObjCRuntime::GCC ||
      runtime.getKind() == ObjCRuntime::GNUstep ||
      runtime.getKind() == ObjCRuntime::ObjFW;
  bool useGNUNonLegacyObjCRuntime =
      useGNUObjCRuntime &&
      cgm.getCodeGenOpts().getObjCDispatchMethod() != CodeGenOptions::Legacy;

  if (!useDarwinObjCRuntime && !useGNUObjCRuntime) {
    cgm.errorNYI(e->getSourceRange(), "ObjC message on unsupported runtime");
    return RValue::get(nullptr);
  }

  const ObjCMethodDecl *method = e->getMethodDecl();
  const auto *currentMethod = dyn_cast_or_null<ObjCMethodDecl>(curGD.getDecl());
  if (method && e->getReceiverKind() == ObjCMessageExpr::Instance &&
      method->getMethodFamily() == OMF_retain) {
    if (const Expr *lvalueExpr = findWeakLValue(e->getInstanceReceiver())) {
      LValue lvalue = emitLValue(lvalueExpr);
      return adjustObjCObjectType(
          *this, loc, e->getType(),
          RValue::get(emitARCLoadWeakRetained(lvalue.getAddress())));
    }
  }

  mlir::Value receiver;
  const ObjCInterfaceDecl *classReceiver = nullptr;
  bool isSuperMessage = false;
  bool isClassMessage = false;
  switch (e->getReceiverKind()) {
  case ObjCMessageExpr::Instance:
    isClassMessage = e->getInstanceReceiver()->getType()->isObjCClassType();
    receiver = emitScalarExpr(e->getInstanceReceiver());
    break;
  case ObjCMessageExpr::Class: {
    classReceiver = e->getReceiverInterface();
    if (!classReceiver) {
      cgm.errorNYI(e->getSourceRange(),
                   "objc class receiver without interface");
      return RValue::get(nullptr);
    }
    if (useDarwinObjCRuntime) {
      receiver = cgm.getObjCClass(*this, classReceiver, loc);
    } else {
      receiver = getObjCGNUClass(*this, loc, classReceiver);
    }
    isClassMessage = true;
    break;
  }
  case ObjCMessageExpr::SuperInstance:
  case ObjCMessageExpr::SuperClass: {
    if (!currentMethod) {
      cgm.errorNYI(e->getSourceRange(), "super objc message outside objc method");
      return RValue::get(nullptr);
    }
    classReceiver = currentMethod->getClassInterface();
    const auto *selfDecl = currentMethod->getSelfDecl();
    Address selfAddr = getAddrOfLocalVar(selfDecl);
    receiver = builder.createAlignedLoad(loc, cgm.voidPtrTy,
                                         selfAddr.getPointer(),
                                         selfAddr.getAlignment());
    isSuperMessage = true;
    isClassMessage = e->getReceiverKind() == ObjCMessageExpr::SuperClass;
    break;
  }
  }

  if (method && getLangOpts().ObjCAutoRefCount &&
      method->hasAttr<ObjCReturnsInnerPointerAttr>() &&
      shouldExtendReceiverForInnerPointerMessage(e))
    receiver = emitARCRetainAutorelease(receiver, loc);

  QualType resultType = method ? method->getReturnType() : e->getType();

  CallArgList messageArgs;
  llvm::iterator_range<CallExpr::const_arg_iterator> argRange(e->arg_begin(),
                                                              e->arg_end());
  if (method) {
    emitCallArgs(messageArgs, PrototypeWrapper(method), argRange,
                 AbstractCallee(method), /*paramsToSkip=*/0,
                 EvaluationOrder::Default);
  } else {
    for (const Expr *arg : argRange)
      emitCallArg(messageArgs, arg, arg->getType());
  }

  if (useDarwinObjCRuntime) {
    if (std::optional<RValue> specializedResult =
            tryEmitSpecializedObjCMessageSend(*this, loc, resultType, receiver,
                                              messageArgs, e->getSelector(),
                                              isClassMessage, isSuperMessage))
      return *specializedResult;
  }

  QualType objCIdTy = getContext().getObjCIdType();
  QualType objCSelTy = getContext().getObjCSelType();
  QualType voidPtrTy = getContext().getPointerType(getContext().VoidTy);
  bool useDarwinFixupDispatch =
      useDarwinNonFragileObjCRuntime &&
      (!method || !method->isDirectMethod()) &&
      isDarwinVTableDispatchedSelector(*this, e->getSelector());
  bool useTypedGNUSelector = method && useGNUObjCRuntime && !isSuperMessage;
  mlir::Value selector;
  if (!method || !method->isDirectMethod()) {
    if (!useDarwinFixupDispatch) {
      selector = useTypedGNUSelector
                     ? cgm.getObjCSelector(*this, e->getSelector(), loc, method)
                     : cgm.getObjCSelector(*this, e->getSelector(), loc);
    }
  }

  mlir::Value callReceiver = receiver;
  QualType callReceiverTy = objCIdTy;
  Address superLookupAddr = Address::invalid();
  if (isSuperMessage && useDarwinObjCRuntime) {
    if (!classReceiver) {
      cgm.errorNYI(e->getSourceRange(),
                   "objc super receiver without enclosing interface");
      return RValue::get(nullptr);
    }

    auto objCSuperTy =
        builder.getAnonRecordTy({cgm.voidPtrTy, cgm.voidPtrTy});
    mlir::Value superAlloca = builder.createAlloca(
        loc, builder.getPointerTo(objCSuperTy), objCSuperTy, "objc_super",
        getPointerAlign());
    Address superAddr(superAlloca, objCSuperTy, getPointerAlign());

    mlir::Value receiverField = builder.createGetMember(
        loc, builder.getPointerTo(cgm.voidPtrTy), superAddr.getPointer(), "", 0);
    builder.createStore(
        loc, receiver,
        Address(receiverField, cgm.voidPtrTy,
                superAddr.getAlignment().alignmentAtOffset(CharUnits::Zero())));

    mlir::Value superClass;
    if (useDarwinNonFragileObjCRuntime) {
      superClass =
          cgm.getObjCSuperClass(*this, classReceiver, loc, isClassMessage);
    } else {
      const ObjCInterfaceDecl *superClassDecl = classReceiver->getSuperClass();
      if (!superClassDecl) {
        cgm.errorNYI(e->getSourceRange(),
                     "objc super receiver without superclass");
        return RValue::get(nullptr);
      }

      cir::GlobalOp superClassNameGV =
          cgm.getObjCClassNameGlobal(superClassDecl->getObjCRuntimeNameAsString());
      auto superClassNamePtrTy = builder.getPointerTo(
          cgm.uCharTy, superClassNameGV.getAddrSpaceAttr());
      superClass = builder.getConstant(
          loc, builder.getGlobalViewAttr(superClassNamePtrTy, superClassNameGV));
      if (superClass.getType() != cgm.voidPtrTy)
        superClass = builder.createBitcast(loc, superClass, cgm.voidPtrTy);
    }
    mlir::Value classField = builder.createGetMember(
        loc, builder.getPointerTo(cgm.voidPtrTy), superAddr.getPointer(), "", 1);
    builder.createStore(
        loc, superClass,
        Address(classField, cgm.voidPtrTy,
                superAddr.getAlignment().alignmentAtOffset(getPointerAlign())));

    callReceiver = superAddr.getPointer();
    callReceiverTy = getContext().getPointerType(getContext().VoidTy);
  } else if (isSuperMessage) {
    if (!classReceiver || !classReceiver->getSuperClass()) {
      cgm.errorNYI(e->getSourceRange(), "objc super receiver without superclass");
      return RValue::get(nullptr);
    }

    auto objCSuperTy =
        builder.getAnonRecordTy({cgm.voidPtrTy, cgm.voidPtrTy});
    mlir::Value superAlloca = builder.createAlloca(
        loc, builder.getPointerTo(objCSuperTy), objCSuperTy, "objc_super",
        getPointerAlign());
    superLookupAddr = Address(superAlloca, objCSuperTy, getPointerAlign());

    mlir::Value receiverValue = receiver;
    if (receiverValue.getType() != cgm.voidPtrTy)
      receiverValue = builder.createBitcast(loc, receiverValue, cgm.voidPtrTy);
    mlir::Value receiverField = builder.createGetMember(
        loc, builder.getPointerTo(cgm.voidPtrTy), superLookupAddr.getPointer(), "",
        0);
    builder.createStore(
        loc, receiverValue,
        Address(receiverField, cgm.voidPtrTy,
                superLookupAddr.getAlignment().alignmentAtOffset(
                    CharUnits::Zero())));

    mlir::Value superClass =
        getObjCGNUClass(*this, loc, classReceiver->getSuperClass());
    if (isClassMessage) {
      mlir::Type ptrToVoidPtrTy = builder.getPointerTo(cgm.voidPtrTy);
      if (superClass.getType() != ptrToVoidPtrTy)
        superClass = builder.createBitcast(loc, superClass, ptrToVoidPtrTy);
      superClass =
          builder.createLoad(loc, Address(superClass, cgm.voidPtrTy,
                                          getPointerAlign()));
    }
    if (superClass.getType() != cgm.voidPtrTy)
      superClass = builder.createBitcast(loc, superClass, cgm.voidPtrTy);
    mlir::Value classField = builder.createGetMember(
        loc, builder.getPointerTo(cgm.voidPtrTy), superLookupAddr.getPointer(), "",
        1);
    builder.createStore(
        loc, superClass,
        Address(classField, cgm.voidPtrTy,
                superLookupAddr.getAlignment().alignmentAtOffset(
                    getPointerAlign())));
  }

  CallArgList actualArgs;
  actualArgs.add(RValue::get(callReceiver), callReceiverTy);
  if (!method || !method->isDirectMethod()) {
    if (useDarwinFixupDispatch) {
      actualArgs.add(RValue::get(builder.getNullValue(cgm.voidPtrTy, loc)),
                     voidPtrTy);
    } else {
      actualArgs.add(RValue::get(selector), objCSelTy);
    }
  }
  actualArgs.addFrom(messageArgs);
  bool receiverCanBeNull = canObjCMessageReceiverBeNull(
      *this, currentMethod, method, isSuperMessage, classReceiver, receiver);
  const CIRGenFunctionInfo *funcInfo = nullptr;
  bool usesStructReturnMessenger = false;
  bool usesFPRetMessenger = false;
  bool usesFP2RetMessenger = false;
  cir::FuncType exactFnTy;
  mlir::Value darwinMsgSendPtr;
  mlir::Value gnuMsgSendPtr;
  if (method && method->isDirectMethod()) {
    const CIRGenFunctionInfo &directInfo =
        getTypes().arrangeObjCMethodDeclaration(method);
    funcInfo = &directInfo;
    SmallString<256> nameBuf;
    {
      llvm::raw_svector_ostream os(nameBuf);
      cgm.mangleObjCMethodName(method, os);
    }
    cir::FuncOp directFn = cgm.getOrCreateCIRFunction(
        nameBuf.str(), getTypes().getFunctionType(directInfo),
        GlobalDecl(method), /*forVTable=*/false, /*dontDefer=*/true,
        /*isThunk=*/false, NotForDefinition);
    bool suppressNullableDirectNoReturn = receiverCanBeNull && !method->isDefined();
    if (suppressNullableDirectNoReturn)
      eraseFuncNoReturnAttr(directFn);

    cir::CIRCallOpInterface callOp;
    CIRGenCallee directCallee =
        suppressNullableDirectNoReturn
            ? CIRGenCallee::forDirect(directFn)
            : CIRGenCallee::forDirect(directFn, GlobalDecl(method));
    RValue result = emitCall(directInfo, directCallee, returnValue, actualArgs,
                             &callOp, loc);
    if (receiverCanBeNull)
      eraseCallNoReturnAttr(callOp);
    return result.isScalar()
               ? adjustObjCObjectType(*this, loc, e->getType(), result)
               : result;
  } else {
    llvm::SmallVector<QualType, 4> argTypes;
    argTypes.push_back(callReceiverTy);
    argTypes.push_back(objCSelTy);
    for (const CallArg &arg : llvm::drop_begin(actualArgs, 2))
      argTypes.push_back(arg.ty);

    QualType msgFnTy = getContext().getFunctionType(
        resultType, argTypes,
        FunctionProtoType::ExtProtoInfo());
    auto msgProto = CanQual<FunctionProtoType>::CreateUnsafe(
        getContext().getCanonicalType(msgFnTy));
    const CIRGenFunctionInfo &msgInfo =
        getTypes().arrangeFreeFunctionType(msgProto);
    funcInfo = &msgInfo;
    exactFnTy = getTypes().getFunctionType(msgInfo);
    if (useGNUObjCRuntime)
      usesStructReturnMessenger =
          objCMessageResultUsesGNUStructReturnMessenger(*this, resultType);
    if (useDarwinObjCRuntime) {
      usesStructReturnMessenger =
          getTarget().getTriple().isX86() &&
          objCMessageResultUsesIndirectReturnABI(*this, exactFnTy);
      usesFPRetMessenger = objCMessageResultUsesFPRet(*this, resultType);
      usesFP2RetMessenger = objCMessageResultUsesFP2Ret(*this, resultType);
      if (useDarwinFixupDispatch) {
        cir::FuncOp msgSendFn = getObjCDarwinFixupMessageSendFunc(
            *this, isSuperMessage, usesStructReturnMessenger,
            usesFPRetMessenger);
        cir::GlobalOp messageRefGV =
            getObjCDarwinMessageRefGlobal(*this, loc, e->getSelector(), msgSendFn);
        CharUnits messageRefAlign = getPointerAlign();
        mlir::Value messageRefAddr = builder.createGetGlobal(loc, messageRefGV);
        mlir::Value messengerField = builder.createGetMember(
            loc, builder.getPointerTo(cgm.voidPtrTy), messageRefAddr, "", 0);
        mlir::Value messengerPtr = builder.createLoad(
            loc, Address(messengerField, cgm.voidPtrTy,
                         messageRefAlign.alignmentAtOffset(CharUnits::Zero())));
        darwinMsgSendPtr =
            builder.createBitcast(loc, messengerPtr, builder.getPointerTo(exactFnTy));

        mlir::Value messageRefArg = messageRefAddr;
        if (messageRefArg.getType() != cgm.voidPtrTy)
          messageRefArg = builder.createBitcast(loc, messageRefArg, cgm.voidPtrTy);
        actualArgs[1] = CallArg(RValue::get(messageRefArg), voidPtrTy);
      } else {
        cir::FuncOp msgSendFn;
        if (isSuperMessage) {
          if (useDarwinNonFragileObjCRuntime) {
            msgSendFn = usesStructReturnMessenger
                            ? cgm.getObjCMsgSendSuper2StretFunc()
                            : cgm.getObjCMsgSendSuper2Func();
          } else {
            msgSendFn = usesStructReturnMessenger
                            ? cgm.getObjCMsgSendSuperStretFunc()
                            : cgm.getObjCMsgSendSuperFunc();
          }
        } else {
          if (usesStructReturnMessenger) {
            msgSendFn = cgm.getObjCMsgSendStretFunc();
          } else if (usesFPRetMessenger) {
            msgSendFn = cgm.getObjCMsgSendFpretFunc();
          } else if (usesFP2RetMessenger) {
            msgSendFn = cgm.getObjCMsgSendFp2retFunc();
          } else {
            msgSendFn = cgm.getObjCMsgSendFunc();
          }
        }
        mlir::Value msgSendAddr = cir::GetGlobalOp::create(
            builder, loc, builder.getPointerTo(msgSendFn.getFunctionType()),
            msgSendFn.getSymNameAttr());
        darwinMsgSendPtr = builder.createBitcast(
            loc, msgSendAddr, builder.getPointerTo(exactFnTy));
      }
    } else if (useGNUNonLegacyObjCRuntime && !isSuperMessage) {
      usesFPRetMessenger = objCMessageResultUsesFPRet(*this, resultType);
      bool usesInRegStructReturnMessenger = false;
      if (!getGNUNonLegacyMessageReturnABI(*this, resultType, exactFnTy,
                                           usesStructReturnMessenger,
                                           usesInRegStructReturnMessenger)) {
        usesStructReturnMessenger =
            objCMessageResultUsesGNUStructReturnMessenger(*this, resultType);
      }
      cir::FuncOp msgSendFn =
          usesFPRetMessenger
              ? cgm.getObjCMsgSendFpretFunc()
              : (usesInRegStructReturnMessenger
                     ? cgm.getObjCMsgSendStret2Func()
                     : (usesStructReturnMessenger ? cgm.getObjCMsgSendStretFunc()
                                                  : cgm.getObjCMsgSendFunc()));
      mlir::Value msgSendAddr = cir::GetGlobalOp::create(
          builder, loc, builder.getPointerTo(msgSendFn.getFunctionType()),
          msgSendFn.getSymNameAttr());
      gnuMsgSendPtr =
          builder.createBitcast(loc, msgSendAddr, builder.getPointerTo(exactFnTy));
    }
  }

  auto attachObjCNoReturnAttr = [&](cir::CIRCallOpInterface callOp) {
    if (!callOp)
      return;

    auto mergeCallExtraAttr = [&](llvm::StringRef name, mlir::Attribute value) {
      mlir::NamedAttrList callAttrs;
      if (auto existingExtra =
              callOp->getAttrOfType<cir::ExtraFuncAttributesAttr>(
                  "extra_attrs")) {
        for (auto attr : existingExtra.getElements().getValue())
          callAttrs.append(attr.getName(), attr.getValue());
      }
      callAttrs.set(name, value);
      callOp->setAttr("extra_attrs", cir::ExtraFuncAttributesAttr::get(
                                         callAttrs.getDictionary(
                                             &getMLIRContext())));
    };

    if (!method || !method->hasAttr<NoReturnAttr>() || receiverCanBeNull)
      return;

    auto noReturn = cir::NoReturnAttr::get(&getMLIRContext());
    mergeCallExtraAttr(noReturn.getMnemonic(), noReturn);
  };

  auto attachNoThrowAttr = [&](cir::CIRCallOpInterface callOp) {
    if (!callOp)
      return;
    callOp->setAttr("nothrow", mlir::UnitAttr::get(&getMLIRContext()));
  };

  auto emitDarwinMessageCall = [&](ReturnValueSlot slot) -> RValue {
    cir::CIRCallOpInterface callOp;
    RValue result = emitCall(
        *funcInfo, CIRGenCallee::forDirect(darwinMsgSendPtr.getDefiningOp()),
        slot, actualArgs, &callOp, loc);
    attachObjCNoReturnAttr(callOp);
    return result;
  };

  auto attachGNUObjCMessageSendAttrs = [&](cir::CIRCallOpInterface callOp) {
    if (!callOp)
      return;

    std::string receiverClassName;
    if (isSuperMessage && classReceiver && classReceiver->getSuperClass()) {
      receiverClassName =
          classReceiver->getSuperClass()->getNameAsString();
    } else if (isClassMessage && classReceiver) {
      receiverClassName = classReceiver->getNameAsString();
    }

    callOp->setAttr(
        "cir.gnu_objc_message_send",
        mlir::ArrayAttr::get(
            &cgm.getMLIRContext(),
            {cgm.getStringAttrCached(e->getSelector().getAsString()),
             cgm.getStringAttrCached(receiverClassName),
             cgm.getBoolAttrCached(isClassMessage)}));
  };

  if (useDarwinObjCRuntime) {
    bool hasParamDestroyedInCallee = method && method->hasParamDestroyedInCallee();
    bool requiresIndirectReturnNullCheck =
        !returnValue.isNull() && !isSuperMessage && receiverCanBeNull &&
        objCMessageResultUsesIndirectReturnABI(*this, exactFnTy);

    bool requiresNilReceiverCheck =
        !isSuperMessage && receiverCanBeNull &&
        (hasParamDestroyedInCallee || requiresIndirectReturnNullCheck);

    if (requiresNilReceiverCheck && isProvablyNullValue(builder, receiver)) {
      destroyObjCCalleeDestroyedArguments(*this, loc, method, actualArgs);
      if (returnValue.isNull()) {
        if (e->getType()->isVoidType())
          return RValue::get(nullptr);
        mlir::Value nullValue = cgm.emitNullConstant(e->getType(), loc);
        return getEvaluationKind(e->getType()) == cir::TEK_Complex
                   ? RValue::getComplex(nullValue)
                   : RValue::get(nullValue);
      }

      emitNullInitialization(loc, returnValue.getValue(), e->getType());
      return RValue::getAggregate(returnValue.getValue(),
                                  returnValue.isVolatile());
    }

    if (requiresNilReceiverCheck) {
      mlir::Value nullReceiver = builder.getNullValue(receiver.getType(), loc);
      mlir::Value isNull = cir::CmpOp::create(
          builder, loc, cir::CmpOpKind::eq, receiver, nullReceiver);

      if (!returnValue.isNull()) {
        cir::IfOp::create(
            builder, loc, isNull, /*withElseRegion=*/true,
            [&](mlir::OpBuilder &, mlir::Location) {
              destroyObjCCalleeDestroyedArguments(*this, loc, method,
                                                  actualArgs);
              emitNullInitialization(loc, returnValue.getValue(), e->getType());
              builder.createYield(loc);
            },
            [&](mlir::OpBuilder &, mlir::Location) {
              (void)emitDarwinMessageCall(returnValue);
              builder.createYield(loc);
            });
        return RValue::getAggregate(returnValue.getValue(),
                                    returnValue.isVolatile());
      }

      if (e->getType()->isVoidType()) {
        cir::IfOp::create(
            builder, loc, isNull, /*withElseRegion=*/true,
            [&](mlir::OpBuilder &, mlir::Location) {
              destroyObjCCalleeDestroyedArguments(*this, loc, method,
                                                  actualArgs);
              builder.createYield(loc);
            },
            [&](mlir::OpBuilder &, mlir::Location) {
              (void)emitDarwinMessageCall(ReturnValueSlot());
              builder.createYield(loc);
            });
        return RValue::get(nullptr);
      }

      mlir::Value nullValue = cgm.emitNullConstant(e->getType(), loc);
      auto ternary = cir::TernaryOp::create(
          builder, loc, isNull,
          [&](mlir::OpBuilder &, mlir::Location) {
            destroyObjCCalleeDestroyedArguments(*this, loc, method, actualArgs);
            builder.createYield(loc, nullValue);
          },
          [&](mlir::OpBuilder &, mlir::Location) {
            RValue callResult = emitDarwinMessageCall(ReturnValueSlot());
            builder.createYield(
                loc, callResult.isComplex() ? callResult.getComplexValue()
                                            : callResult.getValue());
          });
      return getEvaluationKind(e->getType()) == cir::TEK_Complex
                 ? RValue::getComplex(ternary.getResult())
                 : RValue::get(ternary.getResult());
    }

    RValue result = emitDarwinMessageCall(returnValue);
    return result.isScalar()
               ? adjustObjCObjectType(*this, loc, e->getType(), result)
               : result;
  }

  auto emitGNUMessageCall = [&](ReturnValueSlot slot) -> RValue {
    if (useGNUNonLegacyObjCRuntime && !isSuperMessage) {
      cir::CIRCallOpInterface callOp;
      RValue result = emitCall(*funcInfo,
                               CIRGenCallee::forDirect(
                                   gnuMsgSendPtr.getDefiningOp()),
                               slot, actualArgs, &callOp, loc);
      attachGNUObjCMessageSendAttrs(callOp);
      attachObjCNoReturnAttr(callOp);
      return result;
    }

    CallArgList callArgs(actualArgs);
    mlir::Value imp;

    switch (runtime.getKind()) {
    case ObjCRuntime::GCC:
      if (isSuperMessage) {
        auto lookupCall = builder.createCallOp(
            loc, cgm.getObjCMsgLookupSuperFunc(),
            {superLookupAddr.getPointer(), selector});
        attachNoThrowAttr(lookupCall);
        imp = lookupCall.getResult();
      } else {
        auto lookupCall = builder.createCallOp(
            loc, cgm.getObjCMsgLookupFunc(),
            {callArgs[0].getKnownRValue().getValue(), selector});
        attachGNUObjCMessageSendAttrs(lookupCall);
        imp = lookupCall.getResult();
      }
      break;
    case ObjCRuntime::ObjFW:
      if (isSuperMessage) {
        auto lookupCall = builder.createCallOp(
            loc, usesStructReturnMessenger
                     ? cgm.getObjCMsgLookupSuperStretFunc()
                     : cgm.getObjCMsgLookupSuperFunc(),
            {superLookupAddr.getPointer(), selector});
        attachNoThrowAttr(lookupCall);
        attachGNUObjCMessageSendAttrs(lookupCall);
        imp = lookupCall.getResult();
      } else {
        auto lookupCall = builder.createCallOp(
            loc, usesStructReturnMessenger ? cgm.getObjCMsgLookupStretFunc()
                                           : cgm.getObjCMsgLookupFunc(),
            {callArgs[0].getKnownRValue().getValue(), selector});
        attachGNUObjCMessageSendAttrs(lookupCall);
        imp = lookupCall.getResult();
      }
      break;
    case ObjCRuntime::GNUstep:
      if (isSuperMessage) {
        if (runtime.getVersion() >= VersionTuple(2)) {
          auto lookupCall = builder.createCallOp(
              loc, cgm.getObjCMsgLookupSuperFunc(),
              {superLookupAddr.getPointer(), selector});
          attachNoThrowAttr(lookupCall);
          imp = lookupCall.getResult();
        } else {
          auto lookupCall = builder.createCallOp(
              loc, cgm.getObjCSlotLookupSuperFunc(),
              {superLookupAddr.getPointer(), selector});
          attachNoThrowAttr(lookupCall);
          mlir::Value slot = lookupCall.getResult();
          mlir::Value impAddr = builder.createGetMember(
              loc, builder.getPointerTo(cgm.voidPtrTy), slot, "", 4);
          imp = builder.createLoad(
              loc, Address(impAddr, cgm.voidPtrTy, getPointerAlign()));
        }
      } else {
        Address receiverSlot = createTempAlloca(cgm.voidPtrTy, getPointerAlign(),
                                                loc, "objc.receiver");
        mlir::Value originalReceiver = callArgs[0].getKnownRValue().getValue();
        if (originalReceiver.getType() != cgm.voidPtrTy)
          originalReceiver =
              builder.createBitcast(loc, originalReceiver, cgm.voidPtrTy);
        builder.createStore(loc, originalReceiver, receiverSlot);

        mlir::Value sender = builder.getNullValue(cgm.voidPtrTy, loc);
        if (currentMethod) {
          Address selfAddr = getAddrOfLocalVar(currentMethod->getSelfDecl());
          sender = builder.createLoad(loc, selfAddr);
          if (sender.getType() != cgm.voidPtrTy)
            sender = builder.createBitcast(loc, sender, cgm.voidPtrTy);
        }

        auto lookupSenderCall = builder.createCallOp(
            loc, cgm.getObjCMsgLookupSenderFunc(),
            {receiverSlot.getPointer(), selector, sender});
        mlir::NamedAttrList lookupSenderAttrs;
        if (auto existingExtra =
                lookupSenderCall->getAttrOfType<cir::ExtraFuncAttributesAttr>(
                    "extra_attrs")) {
          lookupSenderAttrs = mlir::NamedAttrList{
              existingExtra.getElements().getValue()};
        }
        lookupSenderAttrs.set(
            "side_effect", cir::SideEffectAttr::get(&getMLIRContext(),
                                                    cir::SideEffect::Pure));
        lookupSenderCall->setAttr(
            "extra_attrs", cir::ExtraFuncAttributesAttr::get(
                               lookupSenderAttrs.getDictionary(
                                   &getMLIRContext())));
        attachGNUObjCMessageSendAttrs(lookupSenderCall);
        mlir::Value slot = lookupSenderCall.getResult();
        mlir::Value impAddr = builder.createGetMember(
            loc, builder.getPointerTo(cgm.voidPtrTy), slot, "", 4);
        imp = builder.createLoad(
            loc, Address(impAddr, cgm.voidPtrTy, getPointerAlign()));

        mlir::Value rewrittenReceiver =
            builder.createLoad(loc, receiverSlot, /*isVolatile=*/true);
        callArgs[0] = CallArg(RValue::get(rewrittenReceiver), callReceiverTy);
      }
      break;
    default:
      cgm.errorNYI(e->getSourceRange(), "GNU ObjC runtime kind");
      return RValue::get(nullptr);
    }

    mlir::Type exactPtrTy = builder.getPointerTo(exactFnTy);
    if (imp.getType() != exactPtrTy)
      imp = builder.createBitcast(loc, imp, exactPtrTy);

    cir::CIRCallOpInterface callOp;
    RValue result = emitCall(*funcInfo,
                             CIRGenCallee::forDirect(imp.getDefiningOp()), slot,
                             callArgs, &callOp, loc);
    attachGNUObjCMessageSendAttrs(callOp);
    attachObjCNoReturnAttr(callOp);
    return result;
  };

  bool hasParamDestroyedInCallee = method && method->hasParamDestroyedInCallee();
  bool requiresExplicitZeroResult = false;
  if (!isSuperMessage && receiverCanBeNull) {
    QualType resultTy = method ? method->getReturnType() : e->getType();
    if (resultTy->isVoidType()) {
      // The runtime nil stub handles void results.
    } else if (resultTy->hasPointerRepresentation() &&
               getTypes().isZeroInitializable(resultTy)) {
      // The runtime nil stub handles pointer-like zero values.
    } else if (resultTy->isIntegralOrEnumerationType()) {
      // The runtime nil stub handles integral zero values.
    } else {
      requiresExplicitZeroResult = true;
    }
  }

  bool requiresNilReceiverCheck =
      !isSuperMessage && receiverCanBeNull &&
      (hasParamDestroyedInCallee || requiresExplicitZeroResult);

  if (requiresNilReceiverCheck && isProvablyNullValue(builder, receiver)) {
    destroyObjCCalleeDestroyedArguments(*this, loc, method, actualArgs);
    if (returnValue.isNull()) {
      if (e->getType()->isVoidType())
        return RValue::get(nullptr);
      mlir::Value nullValue = cgm.emitNullConstant(e->getType(), loc);
      return getEvaluationKind(e->getType()) == cir::TEK_Complex
                 ? RValue::getComplex(nullValue)
                 : RValue::get(nullValue);
    }

    emitNullInitialization(loc, returnValue.getValue(), e->getType());
    return RValue::getAggregate(returnValue.getValue(), returnValue.isVolatile());
  }

  if (requiresNilReceiverCheck) {
    mlir::Value nullReceiver = builder.getNullValue(receiver.getType(), loc);
    mlir::Value isNull = cir::CmpOp::create(
        builder, loc, cir::CmpOpKind::eq, receiver, nullReceiver);

    if (!returnValue.isNull()) {
      cir::IfOp::create(
          builder, loc, isNull, /*withElseRegion=*/true,
          [&](mlir::OpBuilder &, mlir::Location) {
            destroyObjCCalleeDestroyedArguments(*this, loc, method, actualArgs);
            emitNullInitialization(loc, returnValue.getValue(), e->getType());
            builder.createYield(loc);
          },
          [&](mlir::OpBuilder &, mlir::Location) {
            (void)emitGNUMessageCall(returnValue);
            builder.createYield(loc);
          });
      return RValue::getAggregate(returnValue.getValue(), returnValue.isVolatile());
    }

    if (e->getType()->isVoidType()) {
      cir::IfOp::create(
          builder, loc, isNull, /*withElseRegion=*/true,
          [&](mlir::OpBuilder &, mlir::Location) {
            destroyObjCCalleeDestroyedArguments(*this, loc, method, actualArgs);
            builder.createYield(loc);
          },
          [&](mlir::OpBuilder &, mlir::Location) {
            (void)emitGNUMessageCall(ReturnValueSlot());
            builder.createYield(loc);
          });
      return RValue::get(nullptr);
    }

    mlir::Value nullValue = cgm.emitNullConstant(e->getType(), loc);
    auto ternary = cir::TernaryOp::create(
        builder, loc, isNull,
        [&](mlir::OpBuilder &, mlir::Location) {
          destroyObjCCalleeDestroyedArguments(*this, loc, method, actualArgs);
          builder.createYield(loc, nullValue);
        },
        [&](mlir::OpBuilder &, mlir::Location) {
          RValue callResult = emitGNUMessageCall(ReturnValueSlot());
          builder.createYield(
              loc, callResult.isComplex() ? callResult.getComplexValue()
                                          : callResult.getValue());
        });
    return getEvaluationKind(e->getType()) == cir::TEK_Complex
               ? RValue::getComplex(ternary.getResult())
               : RValue::get(ternary.getResult());
  }

  RValue result = emitGNUMessageCall(returnValue);

  return result.isScalar()
             ? adjustObjCObjectType(*this, loc, e->getType(), result)
             : result;
}

LValue
CIRGenFunction::emitCompoundAssignmentLValue(const CompoundAssignOperator *e) {
  ApplySourceAtomGroup atomGroup(*this);
  ScalarExprEmitter emitter(*this, builder);
  mlir::Value result;
  switch (e->getOpcode()) {
#define COMPOUND_OP(Op)                                                        \
  case BO_##Op##Assign:                                                        \
    return emitter.emitCompoundAssignLValue(e, &ScalarExprEmitter::emit##Op,   \
                                            result)
    COMPOUND_OP(Mul);
    COMPOUND_OP(Div);
    COMPOUND_OP(Rem);
    COMPOUND_OP(Add);
    COMPOUND_OP(Sub);
    COMPOUND_OP(Shl);
    COMPOUND_OP(Shr);
    COMPOUND_OP(And);
    COMPOUND_OP(Xor);
    COMPOUND_OP(Or);
#undef COMPOUND_OP

  case BO_PtrMemD:
  case BO_PtrMemI:
  case BO_Mul:
  case BO_Div:
  case BO_Rem:
  case BO_Add:
  case BO_Sub:
  case BO_Shl:
  case BO_Shr:
  case BO_LT:
  case BO_GT:
  case BO_LE:
  case BO_GE:
  case BO_EQ:
  case BO_NE:
  case BO_Cmp:
  case BO_And:
  case BO_Xor:
  case BO_Or:
  case BO_LAnd:
  case BO_LOr:
  case BO_Assign:
  case BO_Comma:
    llvm_unreachable("Not valid compound assignment operators");
  }
  llvm_unreachable("Unhandled compound assignment operator");
}

/// Emit the computation of the specified expression of scalar type.
mlir::Value CIRGenFunction::emitScalarExpr(const Expr *e,
                                           bool ignoreResultAssign) {
  assert(e && hasScalarEvaluationKind(e->getType()) &&
         "Invalid scalar expression to emit");

  return ScalarExprEmitter(*this, builder, ignoreResultAssign)
      .Visit(const_cast<Expr *>(e));
}

mlir::Value CIRGenFunction::emitPromotedScalarExpr(const Expr *e,
                                                   QualType promotionType) {
  if (!promotionType.isNull())
    return ScalarExprEmitter(*this, builder).emitPromoted(e, promotionType);
  return ScalarExprEmitter(*this, builder).Visit(const_cast<Expr *>(e));
}

[[maybe_unused]] static bool mustVisitNullValue(const Expr *e) {
  // If a null pointer expression's type is the C++0x nullptr_t and
  // the expression is not a simple literal, it must be evaluated
  // for its potential side effects.
  if (isa<IntegerLiteral>(e) || isa<CXXNullPtrLiteralExpr>(e))
    return false;
  return e->getType()->isNullPtrType();
}

/// If \p e is a widened promoted integer, get its base (unpromoted) type.
static std::optional<QualType>
getUnwidenedIntegerType(const ASTContext &astContext, const Expr *e) {
  const Expr *base = e->IgnoreImpCasts();
  if (e == base)
    return std::nullopt;

  QualType baseTy = base->getType();
  if (!astContext.isPromotableIntegerType(baseTy) ||
      astContext.getTypeSize(baseTy) >= astContext.getTypeSize(e->getType()))
    return std::nullopt;

  return baseTy;
}

/// Check if \p e is a widened promoted integer.
[[maybe_unused]] static bool isWidenedIntegerOp(const ASTContext &astContext,
                                                const Expr *e) {
  return getUnwidenedIntegerType(astContext, e).has_value();
}

/// Check if we can skip the overflow check for \p Op.
static bool canElideOverflowCheck(const ASTContext &astContext,
                                  const BinOpInfo &op) {
  assert((isa<UnaryOperator>(op.e) || isa<BinaryOperator>(op.e)) &&
         "Expected a unary or binary operator");

  // OGCG also checks isTypeIgnoredBySanitizer, NegUnsignedConst overflow
  // pattern exclusion, and hasExcludedOverflowPattern on the BinaryOperator.
  // CIR currently skips these checks and therefore may conservatively keep
  // overflow checks in some excluded-pattern cases.

  // If the binop has constant inputs and we can prove there is no overflow,
  // we can elide the overflow check.
  if (!op.mayHaveIntegerOverflow())
    return true;

  // If a unary op has a widened operand, the op cannot overflow.
  if (const auto *uo = dyn_cast<UnaryOperator>(op.e))
    return !uo->canOverflow();

  // We usually don't need overflow checks for binops with widened operands.
  // Multiplication with promoted unsigned operands is a special case.
  const auto *bo = cast<BinaryOperator>(op.e);
  std::optional<QualType> optionalLHSTy =
      getUnwidenedIntegerType(astContext, bo->getLHS());
  if (!optionalLHSTy)
    return false;

  std::optional<QualType> optionalRHSTy =
      getUnwidenedIntegerType(astContext, bo->getRHS());
  if (!optionalRHSTy)
    return false;

  QualType lhsTy = *optionalLHSTy;
  QualType rhsTy = *optionalRHSTy;

  // This is the simple case: binops without unsigned multiplication, and with
  // widened operands. No overflow check is needed here.
  if ((op.opcode != BO_Mul && op.opcode != BO_MulAssign) ||
      !lhsTy->isUnsignedIntegerType() || !rhsTy->isUnsignedIntegerType())
    return true;

  // For unsigned multiplication the overflow check can be elided if either one
  // of the unpromoted types are less than half the size of the promoted type.
  unsigned promotedSize = astContext.getTypeSize(op.e->getType());
  return (2 * astContext.getTypeSize(lhsTy)) < promotedSize ||
         (2 * astContext.getTypeSize(rhsTy)) < promotedSize;
}

static bool shouldEmitUnsignedOverflowCheck(CIRGenFunction &cgf,
                                            const BinOpInfo &ops) {
  return ops.fullType->isUnsignedIntegerType() &&
         cgf.sanOpts.has(SanitizerKind::UnsignedIntegerOverflow) &&
         !canElideOverflowCheck(cgf.getContext(), ops);
}

static void emitIntegerDivAndRemSanitizerCheck(CIRGenFunction &cgf,
                                               CIRGenBuilderTy &builder,
                                               const BinOpInfo &ops) {
  mlir::Location loc = cgf.getLoc(ops.loc);
  auto combineChecks = [&](mlir::Value lhsCheck,
                           mlir::Value rhsCheck) -> mlir::Value {
    if (!lhsCheck)
      return rhsCheck;
    if (!rhsCheck)
      return lhsCheck;
    return buildAndNoSanitize(builder, loc, lhsCheck, rhsCheck);
  };

  mlir::Value validDivisor;
  if (cgf.sanOpts.has(SanitizerKind::IntegerDivideByZero) &&
      ops.mayHaveIntegerDivisionByZero()) {
    mlir::Value zero = builder.getConstInt(loc, ops.rhs.getType(), 0);
    validDivisor = cir::CmpOp::create(builder, loc, cir::CmpOpKind::ne,
                                      ops.rhs, zero);
    markNoSanitize(validDivisor.getDefiningOp());
    validDivisor = cgf.emitGuardedUBSanCheck(
        loc, validDivisor, SanitizerKind::SO_IntegerDivideByZero);
  }

  mlir::Value noSignedOverflow;
  if (cgf.sanOpts.has(SanitizerKind::SignedIntegerOverflow) &&
      ops.fullType->hasSignedIntegerRepresentation() &&
      ops.mayHaveIntegerOverflow()) {
    const auto *bo = llvm::dyn_cast_or_null<BinaryOperator>(ops.e);
    if (!bo || !getUnwidenedIntegerType(cgf.getContext(), bo->getLHS()).has_value()) {
      auto intTy = mlir::cast<cir::IntType>(ops.lhs.getType());
      mlir::Value intMin = builder.getConstInt(
          loc, llvm::APInt::getSignedMinValue(intTy.getWidth()),
          /*isUnsigned=*/false);
      mlir::Value negOne = builder.getConstInt(
          loc, llvm::APInt::getAllOnes(intTy.getWidth()),
          /*isUnsigned=*/false);
      auto lhsCmp = cir::CmpOp::create(builder, loc, cir::CmpOpKind::ne,
                                       ops.lhs, intMin);
      auto rhsCmp = cir::CmpOp::create(builder, loc, cir::CmpOpKind::ne,
                                       ops.rhs, negOne);
      markNoSanitize(lhsCmp.getOperation());
      markNoSanitize(rhsCmp.getOperation());
      noSignedOverflow = builder.createOr(loc, lhsCmp, rhsCmp);
      markNoSanitize(noSignedOverflow.getDefiningOp());
      noSignedOverflow = cgf.emitGuardedUBSanCheck(
          loc, noSignedOverflow, SanitizerKind::SO_SignedIntegerOverflow);
    }
  }

  if (!validDivisor && !noSignedOverflow)
    return;

  bool divisorTrap = validDivisor && cgf.cgm.getCodeGenOpts().SanitizeTrap.has(
                                        SanitizerKind::IntegerDivideByZero);
  bool divisorRecover =
      validDivisor && !divisorTrap &&
      cgf.cgm.getCodeGenOpts().SanitizeRecover.has(
          SanitizerKind::IntegerDivideByZero);
  bool divisorFatal = validDivisor && !divisorTrap && !divisorRecover;

  bool overflowTrap =
      noSignedOverflow &&
      cgf.cgm.getCodeGenOpts().SanitizeTrap.has(
          SanitizerKind::SignedIntegerOverflow);
  bool overflowRecover =
      noSignedOverflow && !overflowTrap &&
      cgf.cgm.getCodeGenOpts().SanitizeRecover.has(
          SanitizerKind::SignedIntegerOverflow);
  bool overflowFatal =
      noSignedOverflow && !overflowTrap && !overflowRecover;

  auto emitTrap = [&](mlir::Location ifLoc) {
    auto trap = cir::TrapOp::create(builder, ifLoc);
    trap->setAttr("cir.ubsantrap_handler",
                  mlir::IntegerAttr::get(
                      mlir::IntegerType::get(builder.getContext(), 8), 3));
    markNoSanitize(trap);
  };

  mlir::Value trapPass = combineChecks(divisorTrap ? validDivisor : mlir::Value(),
                                       overflowTrap ? noSignedOverflow
                                                    : mlir::Value());
  if (trapPass) {
    auto trapIf = cir::IfOp::create(
        builder, loc, trapPass, /*withElseRegion=*/true,
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          auto yield = cir::YieldOp::create(builder, ifLoc);
          markNoSanitize(yield);
        },
        [&](mlir::OpBuilder &, mlir::Location ifLoc) { emitTrap(ifLoc); });
    setSanitizerBranchAttrs(builder, trapIf);
  }

  auto emitRuntimeCheck = [&](mlir::Value pass, llvm::StringRef handlerKind) {
    if (!pass)
      return;

    auto handlerIf = cir::IfOp::create(
        builder, loc, pass, /*withElseRegion=*/true,
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          auto yield = cir::YieldOp::create(builder, ifLoc);
          markNoSanitize(yield);
        },
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          emitOverflowHandlerStubCall(cgf, builder, ifLoc, ops, handlerKind);
          if (handlerKind == "abort") {
            auto unreachable = cir::UnreachableOp::create(builder, ifLoc);
            markNoSanitize(unreachable);
          } else {
            auto yield = cir::YieldOp::create(builder, ifLoc);
            markNoSanitize(yield);
          }
        });
    setSanitizerBranchAttrs(builder, handlerIf);
  };

  emitRuntimeCheck(
      combineChecks(divisorFatal ? validDivisor : mlir::Value(),
                    overflowFatal ? noSignedOverflow : mlir::Value()),
      "abort");
  emitRuntimeCheck(
      combineChecks(divisorRecover ? validDivisor : mlir::Value(),
                    overflowRecover ? noSignedOverflow : mlir::Value()),
      "recover");
}

static void emitFloatDivSanitizerCheck(CIRGenFunction &cgf,
                                       CIRGenBuilderTy &builder,
                                       const BinOpInfo &ops) {
  if (!cgf.sanOpts.has(SanitizerKind::FloatDivideByZero) ||
      !ops.fullType->isRealFloatingType() ||
      !ops.mayHaveFloatDivisionByZero())
    return;

  mlir::Location loc = cgf.getLoc(ops.loc);
  mlir::Value zero = builder.getConstFP(
      loc, ops.rhs.getType(),
      llvm::APFloat::getZero(cgf.getContext().getFloatTypeSemantics(
          ops.fullType)));
  mlir::Value nonZero =
      cir::CmpOp::create(builder, loc, cir::CmpOpKind::ne, ops.rhs, zero);
  markNoSanitize(nonZero.getDefiningOp());

  bool trap = cgf.cgm.getCodeGenOpts().SanitizeTrap.has(
      SanitizerKind::FloatDivideByZero);
  bool recover = !trap && cgf.cgm.getCodeGenOpts().SanitizeRecover.has(
                              SanitizerKind::FloatDivideByZero);
  bool fatal = !trap && !recover;

  auto emitTrap = [&](mlir::Location ifLoc) {
    auto trapOp = cir::TrapOp::create(builder, ifLoc);
    trapOp->setAttr("cir.ubsantrap_handler",
                    mlir::IntegerAttr::get(
                        mlir::IntegerType::get(builder.getContext(), 8), 3));
    markNoSanitize(trapOp);
  };

  if (trap) {
    auto trapIf = cir::IfOp::create(
        builder, loc, nonZero, /*withElseRegion=*/true,
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          auto yield = cir::YieldOp::create(builder, ifLoc);
          markNoSanitize(yield);
        },
        [&](mlir::OpBuilder &, mlir::Location ifLoc) { emitTrap(ifLoc); });
    setSanitizerBranchAttrs(builder, trapIf);
  }

  auto emitRuntimeCheck = [&](llvm::StringRef handlerKind) {
    auto handlerIf = cir::IfOp::create(
        builder, loc, nonZero, /*withElseRegion=*/true,
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          auto yield = cir::YieldOp::create(builder, ifLoc);
          markNoSanitize(yield);
        },
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          emitOverflowHandlerStubCall(cgf, builder, ifLoc, ops, handlerKind);
          if (handlerKind == "abort") {
            auto unreachable = cir::UnreachableOp::create(builder, ifLoc);
            markNoSanitize(unreachable);
          } else {
            auto yield = cir::YieldOp::create(builder, ifLoc);
            markNoSanitize(yield);
          }
        });
    setSanitizerBranchAttrs(builder, handlerIf);
  };

  if (fatal)
    emitRuntimeCheck("abort");
  if (recover)
    emitRuntimeCheck("recover");
}

/// Emit pointer + index arithmetic.
static mlir::Value emitPointerArithmetic(CIRGenFunction &cgf,
                                         const BinOpInfo &op,
                                         bool isSubtraction) {
  // Must have binary (not unary) expr here.  Unary pointer
  // increment/decrement doesn't use this path.
  const BinaryOperator *expr = cast<BinaryOperator>(op.e);

  mlir::Value pointer = op.lhs;
  Expr *pointerOperand = expr->getLHS();
  mlir::Value index = op.rhs;
  Expr *indexOperand = expr->getRHS();

  // In the case of subtraction, the FE has ensured that the LHS is always the
  // pointer. However, addition can have the pointer on either side. We will
  // always have a pointer operand and an integer operand, so if the LHS wasn't
  // a pointer, we need to swap our values.
  if (!isSubtraction && !mlir::isa<cir::PointerType>(pointer.getType())) {
    std::swap(pointer, index);
    std::swap(pointerOperand, indexOperand);
  }
  assert(mlir::isa<cir::PointerType>(pointer.getType()) &&
         "Need a pointer operand");
  assert(mlir::isa<cir::IntType>(index.getType()) && "Need an integer operand");

  auto maybeEmitArrayBoundsCheck = [&](mlir::Value indexToCheck) {
    if (!cgf.sanOpts.has(SanitizerKind::ArrayBounds))
      return;

    if (auto intTy = mlir::dyn_cast<cir::IntType>(indexToCheck.getType()))
      if (!intTy.isFundamental()) {
        indexToCheck = cgf.getBuilder().createIntCast(indexToCheck,
                                                      cgf.ptrDiffTy);
        if (auto *op = indexToCheck.getDefiningOp())
          markNoSanitize(op);
      }
    cgf.emitBoundsCheck(expr, pointerOperand, indexToCheck,
                        indexOperand->getType(), /*Accessed=*/false);
  };

  // Some versions of glibc and gcc use idioms (particularly in their malloc
  // routines) that add a pointer-sized integer (known to be a pointer value)
  // to a null pointer in order to cast the value back to an integer or as
  // part of a pointer alignment algorithm.  This is undefined behavior, but
  // we'd like to be able to compile programs that use it.
  //
  // Normally, we'd generate a GEP with a null-pointer base here in response
  // to that code, but it's also UB to dereference a pointer created that
  // way.  Instead (as an acknowledged hack to tolerate the idiom) we will
  // generate a direct cast of the integer value to a pointer.
  //
  // The idiom (p = nullptr + N) is not met if any of the following are true:
  //
  //   The operation is subtraction.
  //   The index is not pointer-sized.
  //   The pointer type is not byte-sized.
  //
  if (BinaryOperator::isNullPointerArithmeticExtension(
          cgf.getContext(), op.opcode, expr->getLHS(), expr->getRHS())) {
    mlir::Value ptr = cgf.getBuilder().createIntToPtr(index, pointer.getType());
    if (!cgf.getLangOpts().PointerOverflowDefined &&
        cgf.sanOpts.has(SanitizerKind::PointerOverflow) &&
        !cgf.cgm.getCodeGenOpts().NullPointerIsValid) {
      auto [byteOffset, offsetOverflow] =
          cgf.emitCheckedByteOffset(cgf.getLoc(op.e->getExprLoc()), index, 1);
      cgf.emitPointerOverflowCheck(cgf.getLoc(op.e->getExprLoc()), pointer,
                                   byteOffset, offsetOverflow,
                                   mlir::cast<cir::IntType>(index.getType())
                                       .isSigned(),
                                   isSubtraction, op.e->getExprLoc());
    }
    return ptr;
  }

  // Differently from LLVM codegen, ABI bits for index sizes is handled during
  // LLVM lowering.

  // If this is subtraction, negate the index.
  if (isSubtraction)
    index = cgf.getBuilder().createNeg(index);
  maybeEmitArrayBoundsCheck(index);

  const PointerType *pointerType =
      pointerOperand->getType()->getAs<PointerType>();
  if (!pointerType) {
    const auto *objPtrType =
        pointerOperand->getType()->getAs<ObjCObjectPointerType>();
    assert(objPtrType && "Expected Objective-C object pointer");

    mlir::Location loc = cgf.getLoc(op.e->getExprLoc());
    auto indexTy = mlir::cast<cir::IntType>(index.getType());
    uint64_t objectSize =
        cgf.getContext().getTypeSizeInChars(objPtrType->getPointeeType())
            .getQuantity();
    mlir::Value objectSizeValue =
        cgf.getBuilder().getConstInt(loc, indexTy, objectSize);
    index = cgf.getBuilder().createMul(loc, index, objectSizeValue);

    if (!indexTy.isFundamental())
      index = cgf.getBuilder().createIntCast(index, cgf.ptrDiffTy);

    return cir::PtrStrideOp::create(cgf.getBuilder(), loc, pointer.getType(),
                                    pointer, index,
                                    cir::GEPNoWrapFlags::none);
  }

  QualType elementType = pointerType->getPointeeType();
  if (const VariableArrayType *vla =
          cgf.getContext().getAsVariableArrayType(elementType)) {
    // The element count here is the total number of non-VLA elements.
    mlir::Value numElements = cgf.getVLASize(vla).numElts;

    // GEP indexes are signed, and scaling an index isn't permitted to
    // signed-overflow, so we use the same semantics for our explicit
    // multiply.  We suppress this if overflow is not undefined behavior.
    if (index.getType() != numElements.getType())
      index = cgf.getBuilder().createIntCast(index, numElements.getType());

    cir::OverflowBehavior overflowBehavior =
        cgf.getLangOpts().PointerOverflowDefined
            ? cir::OverflowBehavior::None
            : cir::OverflowBehavior::NoSignedWrap;
    index = cgf.getBuilder().createMul(cgf.getLoc(op.e->getExprLoc()), index,
                                       numElements, overflowBehavior);

    // Match OGCG: VLA pointer arithmetic uses inbounds GEP when pointer
    // overflow is not defined.
    cir::GEPNoWrapFlags nwFlags = cir::GEPNoWrapFlags::none;
    if (!cgf.getLangOpts().PointerOverflowDefined)
      nwFlags = cir::GEPNoWrapFlags::inbounds;
    mlir::Location loc = cgf.getLoc(op.e->getExprLoc());
    mlir::Value ptr = cir::PtrStrideOp::create(cgf.getBuilder(), loc,
                                               pointer.getType(), pointer,
                                               index, nwFlags);
    if (!cgf.getLangOpts().PointerOverflowDefined &&
        cgf.sanOpts.has(SanitizerKind::PointerOverflow)) {
      uint64_t elementSize = cgf.cgm.getDataLayout()
                                 .getTypeAllocSize(cgf.convertTypeForMem(
                                     vla->getElementType()))
                                 .getFixedValue();
      auto [byteOffset, offsetOverflow] =
          cgf.emitCheckedByteOffset(loc, index, elementSize);
      cgf.emitPointerOverflowCheck(loc, pointer, byteOffset, offsetOverflow,
                                   /*signedIndices=*/true, isSubtraction,
                                   op.e->getExprLoc());
    }
    return ptr;
  }

  // Explicitly handle GNU void* and function pointer arithmetic extensions.
  // These are treated as byte-sized for pointer arithmetic purposes.
  // Note: We don't need to use elementType here since PtrStrideOp works
  // directly with the pointer type.

  // Determine the GEP no-wrap flags.
  // Match OGCG: only use inbounds when pointer overflow is not defined.
  bool signedIndices = mlir::cast<cir::IntType>(index.getType()).isSigned();
  cir::GEPNoWrapFlags nwFlags = cir::GEPNoWrapFlags::none;
  if (!cgf.getLangOpts().PointerOverflowDefined) {
    nwFlags = cir::GEPNoWrapFlags::inbounds;
    if (!signedIndices && !isSubtraction)
      nwFlags = nwFlags | cir::GEPNoWrapFlags::nuw;
  }

  // PtrStrideOp requires a fundamental integer index. Cast non-fundamental
  // integer types (e.g. _BitInt(9)) to a standard integer type.
  if (auto intTy = mlir::dyn_cast<cir::IntType>(index.getType())) {
    if (!intTy.isFundamental()) {
      index = cgf.getBuilder().createIntCast(index, cgf.ptrDiffTy);
    }
  }

  mlir::Location loc = cgf.getLoc(op.e->getExprLoc());
  mlir::Value result =
      cir::PtrStrideOp::create(cgf.getBuilder(), loc, pointer.getType(),
                               pointer, index, nwFlags);
  if (!cgf.getLangOpts().PointerOverflowDefined &&
      cgf.sanOpts.has(SanitizerKind::PointerOverflow)) {
    uint64_t elementSize = 1;
    if (!elementType->isVoidType() && !elementType->isFunctionType())
      elementSize =
          cgf.cgm.getDataLayout()
              .getTypeAllocSize(cgf.convertTypeForMem(elementType))
              .getFixedValue();
    auto [byteOffset, offsetOverflow] =
        cgf.emitCheckedByteOffset(loc, index, elementSize);
    cgf.emitPointerOverflowCheck(loc, pointer, byteOffset, offsetOverflow,
                                 signedIndices, isSubtraction,
                                 op.e->getExprLoc());
  }
  return result;
}

static cir::IntType getFixedPointIntType(CIRGenBuilderTy &builder,
                                         unsigned width, bool isSigned) {
  return cir::IntType::get(builder.getContext(), width, isSigned);
}

static const llvm::fltSemantics &getFloatSemanticsForType(mlir::Type type) {
  if (auto longDoubleTy = mlir::dyn_cast<cir::LongDoubleType>(type))
    return longDoubleTy.getFloatSemantics();
  return mlir::cast<cir::FPTypeInterface>(type).getFloatSemantics();
}

static mlir::Type getFloatTypeForSemantics(CIRGenFunction &cgf, mlir::Type dstTy,
                                           const llvm::fltSemantics &semantics) {
  if (auto longDoubleTy = mlir::dyn_cast<cir::LongDoubleType>(dstTy)) {
    if (&longDoubleTy.getFloatSemantics() == &semantics)
      return dstTy;
  } else if (auto fpTy = mlir::dyn_cast<cir::FPTypeInterface>(dstTy)) {
    if (&fpTy.getFloatSemantics() == &semantics)
      return dstTy;
  }

  CIRGenBuilderTy &builder = cgf.getBuilder();
  if (&semantics == &cgf.getTarget().getHalfFormat())
    return builder.getFp16Ty();
  if (&semantics == &cgf.getTarget().getBFloat16Format())
    return builder.getBfloat6Ty();
  if (&semantics == &llvm::APFloat::IEEEsingle())
    return builder.getSingleTy();
  if (&semantics == &llvm::APFloat::IEEEdouble())
    return builder.getDoubleTy();
  if (&semantics == &cgf.getTarget().getFloat128Format())
    return cgf.cgm.fP128Ty;
  return builder.getLongDoubleTy(semantics);
}

class CIRFixedPointBuilder {
  CIRGenFunction &cgf;
  CIRGenBuilderTy &builder;
  mlir::Location loc;
  cir::FastMathFlags fmf;
  FPOptions fpfeatures;
  cir::FPExceptionBehaviorAttr fpExcept;
  cir::RoundingModeAttr roundMode;

  static llvm::FixedPointSemantics
  getCommonBinopSemantic(const llvm::FixedPointSemantics &lhsSema,
                         const llvm::FixedPointSemantics &rhsSema) {
    auto common = lhsSema.getCommonSemantics(rhsSema);
    bool bothPadded =
        lhsSema.hasUnsignedPadding() && rhsSema.hasUnsignedPadding();
    return llvm::FixedPointSemantics(
        common.getWidth() + static_cast<unsigned>(bothPadded &&
                                                  common.isSaturated()),
        common.getScale(), common.isSigned(), common.isSaturated(),
        bothPadded);
  }

  bool hasFPConstraints() const {
    return fpfeatures.isFPConstrained() || cgf.IsFPConstrained;
  }

  void maybeAttachFPAttrs(cir::CastOp castOp) const {
    if (!hasFPConstraints())
      return;
    if (fpExcept)
      castOp->setAttr("cir.fp_exception", fpExcept);
    if (roundMode)
      castOp->setAttr("cir.rounding_mode", roundMode);
  }

  mlir::Value createFPCast(cir::CastKind kind, mlir::Value src,
                           mlir::Type dstTy) const {
    auto castOp = cir::CastOp::create(builder, loc, dstTy, kind, src, fmf);
    maybeAttachFPAttrs(castOp);
    return castOp.getResult();
  }

  mlir::Value createPow2Constant(mlir::Type type, int scale) const {
    llvm::APFloat factor(std::pow(2.0, scale));
    bool ignored;
    factor.convert(getFloatSemanticsForType(type), llvm::APFloat::rmTowardZero,
                   &ignored);
    return builder.getConstant(loc, cir::FPAttr::get(type, factor)).getResult();
  }

  mlir::Value createIntCast(mlir::Value value, unsigned dstWidth,
                            bool dstIsSigned, bool srcIsSigned) const {
    value = castIntegerToSignedness(builder, loc, value, srcIsSigned);
    mlir::Type dstTy = getFixedPointIntType(builder, dstWidth, dstIsSigned);
    if (value.getType() == dstTy)
      return value;
    return builder.createIntCast(value, dstTy);
  }

  mlir::Value createCompare(cir::CmpOpKind kind, mlir::Value lhs,
                            mlir::Value rhs, bool isSigned) const {
    lhs = castIntegerToSignedness(builder, loc, lhs, isSigned);
    rhs = castIntegerToSignedness(builder, loc, rhs, isSigned);
    return builder.createCompare(loc, kind, lhs, rhs).getResult();
  }

  mlir::Value createIntrinsic(llvm::StringRef name, mlir::Type resultTy,
                              mlir::ValueRange args) const {
    return cir::LLVMIntrinsicCallOp::create(
               builder, loc, cgf.cgm.getStringAttrCached(name), resultTy, args)
        .getResult();
  }

  mlir::Type getAccommodatingFloatType(mlir::Type dstTy,
                                       const llvm::FixedPointSemantics &sema)
      const {
    const llvm::fltSemantics *floatSema = &getFloatSemanticsForType(dstTy);
    while (!sema.fitsInFloatSemantics(*floatSema))
      floatSema = llvm::APFixedPoint::promoteFloatSemantics(floatSema);
    return getFloatTypeForSemantics(cgf, dstTy, *floatSema);
  }

  mlir::Value convert(mlir::Value src,
                      const llvm::FixedPointSemantics &srcSema,
                      const llvm::FixedPointSemantics &dstSema,
                      bool dstIsInteger) const {
    unsigned srcWidth = srcSema.getWidth();
    unsigned dstWidth = dstSema.getWidth();
    unsigned srcScale = srcSema.getScale();
    unsigned dstScale = dstSema.getScale();
    bool srcIsSigned = srcSema.isSigned();
    bool dstIsSigned = dstSema.isSigned();

    mlir::Value result = src;
    unsigned resultWidth = srcWidth;

    if (dstScale < srcScale) {
      result = castIntegerToSignedness(builder, loc, result, srcIsSigned);
      if (dstIsInteger && srcIsSigned) {
        mlir::Value zero = builder.getConstAPInt(
            loc, result.getType(), llvm::APInt(resultWidth, 0));
        mlir::Value isNegative =
            builder.createCompare(loc, cir::CmpOpKind::lt, result, zero)
                .getResult();
        mlir::Value lowBits = builder.getConstAPInt(
            loc, result.getType(),
            llvm::APInt::getLowBitsSet(resultWidth, srcScale));
        mlir::Value rounded = builder.createAdd(loc, result, lowBits);
        result = builder.createSelect(loc, isNegative, rounded, result);
      }
      result = builder.createShiftRight(loc, result, srcScale - dstScale);
    }

    if (!dstSema.isSaturated()) {
      result = createIntCast(result, dstWidth, dstIsSigned, srcIsSigned);
      if (dstScale > srcScale)
        result = builder.createShiftLeft(loc, result, dstScale - srcScale);
      return result;
    }

    result = castIntegerToSignedness(builder, loc, result, srcIsSigned);
    if (dstScale > srcScale) {
      resultWidth = std::max(srcWidth + dstScale - srcScale, dstWidth);
      result = createIntCast(result, resultWidth, srcIsSigned, srcIsSigned);
      result = builder.createShiftLeft(loc, result, dstScale - srcScale);
    }

    bool lessIntBits = dstSema.getIntegralBits() < srcSema.getIntegralBits();
    if (lessIntBits) {
      mlir::Type resultTy =
          getFixedPointIntType(builder, resultWidth, srcIsSigned);
      mlir::Value max = builder.getConstAPInt(
          loc, resultTy,
          llvm::APFixedPoint::getMax(dstSema).getValue().extOrTrunc(
              resultWidth));
      mlir::Value tooHigh =
          builder.createCompare(loc, cir::CmpOpKind::gt, result, max)
              .getResult();
      result = builder.createSelect(loc, tooHigh, max, result);
    }

    if (srcIsSigned && (lessIntBits || !dstIsSigned)) {
      result = castIntegerToSignedness(builder, loc, result, /*isSigned=*/true);
      mlir::Type resultTy =
          getFixedPointIntType(builder, resultWidth, /*isSigned=*/true);
      mlir::Value min = builder.getConstAPInt(
          loc, resultTy,
          llvm::APFixedPoint::getMin(dstSema).getValue().extOrTrunc(
              resultWidth));
      mlir::Value tooLow =
          builder.createCompare(loc, cir::CmpOpKind::lt, result, min)
              .getResult();
      result = builder.createSelect(loc, tooLow, min, result);
    }

    if (resultWidth != dstWidth ||
        mlir::cast<cir::IntType>(result.getType()).isSigned() != dstIsSigned)
      result = createIntCast(result, dstWidth, dstIsSigned, srcIsSigned);
    return result;
  }

public:
  CIRFixedPointBuilder(CIRGenFunction &cgf, mlir::Location loc,
                       cir::FastMathFlags fmf = cir::FastMathFlags::none,
                       FPOptions fpfeatures = FPOptions())
      : cgf(cgf), builder(cgf.getBuilder()), loc(loc), fmf(fmf),
        fpfeatures(fpfeatures) {
    auto *ctx = &cgf.getMLIRContext();
    fpExcept = cgf.getFPExceptionBehavior(ctx, fpfeatures);
    roundMode = CIRGenFunction::getRoundingMode(ctx, fpfeatures);
  }

  mlir::Value createFixedToFixed(mlir::Value src,
                                 const llvm::FixedPointSemantics &srcSema,
                                 const llvm::FixedPointSemantics &dstSema) const {
    return convert(src, srcSema, dstSema, /*dstIsInteger=*/false);
  }

  mlir::Value createFixedToInteger(mlir::Value src,
                                   const llvm::FixedPointSemantics &srcSema,
                                   unsigned dstWidth,
                                   bool dstIsSigned) const {
    return convert(
        src, srcSema,
        llvm::FixedPointSemantics::GetIntegerSemantics(dstWidth, dstIsSigned),
        /*dstIsInteger=*/true);
  }

  mlir::Value createIntegerToFixed(mlir::Value src, bool srcIsSigned,
                                   const llvm::FixedPointSemantics &dstSema) const {
    return convert(
        src,
        llvm::FixedPointSemantics::GetIntegerSemantics(
            mlir::cast<cir::IntType>(src.getType()).getWidth(), srcIsSigned),
        dstSema, /*dstIsInteger=*/false);
  }

  mlir::Value createFixedToFloating(mlir::Value src,
                                    const llvm::FixedPointSemantics &srcSema,
                                    mlir::Type dstTy) const {
    mlir::Type opTy = getAccommodatingFloatType(dstTy, srcSema);
    src = castIntegerToSignedness(builder, loc, src, srcSema.isSigned());
    mlir::Value result = createFPCast(cir::CastKind::int_to_float, src, opTy);
    result = builder.createFMul(loc, result,
                                createPow2Constant(opTy, -int(srcSema.getScale())),
                                fmf, fpExcept, roundMode);
    if (opTy != dstTy)
      result = createFPCast(cir::CastKind::floating, result, dstTy);
    return result;
  }

  mlir::Value createFloatingToFixed(mlir::Value src,
                                    const llvm::FixedPointSemantics &dstSema) const {
    bool useSigned = dstSema.isSigned() || dstSema.hasUnsignedPadding();
    mlir::Type opTy = getAccommodatingFloatType(src.getType(), dstSema);
    mlir::Value result = src;
    if (opTy != src.getType())
      result = createFPCast(cir::CastKind::floating, result, opTy);
    result = builder.createFMul(loc, result,
                                createPow2Constant(opTy, dstSema.getScale()),
                                fmf, fpExcept, roundMode);

    mlir::Type resultTy =
        getFixedPointIntType(builder, dstSema.getWidth(), useSigned);
    cir::CastKind castKind = dstSema.isSaturated()
                                 ? cir::CastKind::float_to_int_sat
                                 : cir::CastKind::float_to_int;
    result = createFPCast(castKind, result, resultTy);

    if (dstSema.isSaturated() && dstSema.hasUnsignedPadding()) {
      result = castIntegerToSignedness(builder, loc, result, /*isSigned=*/true);
      mlir::Value zero =
          builder.getConstAPInt(loc, result.getType(), llvm::APInt(
                                                          dstSema.getWidth(), 0));
      mlir::Value negative =
          builder.createCompare(loc, cir::CmpOpKind::lt, result, zero)
              .getResult();
      result = builder.createSelect(loc, negative, zero, result);
    }

    if (useSigned != dstSema.isSigned())
      result = createIntCast(result, dstSema.getWidth(), dstSema.isSigned(),
                             useSigned);
    return result;
  }

  mlir::Value createAdd(mlir::Value lhs,
                        const llvm::FixedPointSemantics &lhsSema,
                        mlir::Value rhs,
                        const llvm::FixedPointSemantics &rhsSema) const {
    auto commonBinSema = getCommonBinopSemantic(lhsSema, rhsSema);
    mlir::Value wideLhs = createFixedToFixed(lhs, lhsSema, commonBinSema);
    mlir::Value wideRhs = createFixedToFixed(rhs, rhsSema, commonBinSema);

    mlir::Value result;
    if (commonBinSema.isSaturated()) {
      bool useSigned = commonBinSema.isSigned() || commonBinSema.hasUnsignedPadding();
      wideLhs = castIntegerToSignedness(builder, loc, wideLhs, useSigned);
      wideRhs = castIntegerToSignedness(builder, loc, wideRhs, useSigned);
      result = builder.createAdd(loc, wideLhs, wideRhs,
                                 cir::OverflowBehavior::Saturated);
      if (useSigned != commonBinSema.isSigned())
        result = castIntegerToSignedness(builder, loc, result,
                                         commonBinSema.isSigned());
    } else {
      result = builder.createAdd(loc, wideLhs, wideRhs);
    }

    return createFixedToFixed(result, commonBinSema,
                              lhsSema.getCommonSemantics(rhsSema));
  }

  mlir::Value createSub(mlir::Value lhs,
                        const llvm::FixedPointSemantics &lhsSema,
                        mlir::Value rhs,
                        const llvm::FixedPointSemantics &rhsSema) const {
    auto commonBinSema = getCommonBinopSemantic(lhsSema, rhsSema);
    bool useSigned = commonBinSema.isSigned() || commonBinSema.hasUnsignedPadding();
    mlir::Value wideLhs = createFixedToFixed(lhs, lhsSema, commonBinSema);
    mlir::Value wideRhs = createFixedToFixed(rhs, rhsSema, commonBinSema);

    mlir::Value result;
    if (commonBinSema.isSaturated()) {
      wideLhs = castIntegerToSignedness(builder, loc, wideLhs, useSigned);
      wideRhs = castIntegerToSignedness(builder, loc, wideRhs, useSigned);
      result = builder.createSub(loc, wideLhs, wideRhs,
                                 cir::OverflowBehavior::Saturated);

      if (commonBinSema.hasUnsignedPadding()) {
        mlir::Value zero =
            builder.getConstAPInt(loc, result.getType(),
                                  llvm::APInt(commonBinSema.getWidth(), 0));
        mlir::Value negative =
            builder.createCompare(loc, cir::CmpOpKind::lt, result, zero)
                .getResult();
        result = builder.createSelect(loc, negative, zero, result);
      }

      if (useSigned != commonBinSema.isSigned())
        result = castIntegerToSignedness(builder, loc, result,
                                         commonBinSema.isSigned());
    } else {
      result = builder.createSub(loc, wideLhs, wideRhs);
    }

    return createFixedToFixed(result, commonBinSema,
                              lhsSema.getCommonSemantics(rhsSema));
  }

  mlir::Value createMul(mlir::Value lhs,
                        const llvm::FixedPointSemantics &lhsSema,
                        mlir::Value rhs,
                        const llvm::FixedPointSemantics &rhsSema) const {
    auto commonSema = lhsSema.getCommonSemantics(rhsSema);
    auto commonBinSema = getCommonBinopSemantic(lhsSema, rhsSema);
    bool useSigned = commonBinSema.isSigned() || commonBinSema.hasUnsignedPadding();
    mlir::Value wideLhs = createFixedToFixed(lhs, lhsSema, commonBinSema);
    mlir::Value wideRhs = createFixedToFixed(rhs, rhsSema, commonBinSema);
    wideLhs = castIntegerToSignedness(builder, loc, wideLhs, useSigned);
    wideRhs = castIntegerToSignedness(builder, loc, wideRhs, useSigned);

    llvm::StringRef intrinsicName;
    if (commonBinSema.isSaturated())
      intrinsicName = useSigned ? "smul.fix.sat" : "umul.fix.sat";
    else
      intrinsicName = useSigned ? "smul.fix" : "umul.fix";

    mlir::Value result = createIntrinsic(
        intrinsicName, wideLhs.getType(),
        {wideLhs, wideRhs, builder.getUInt32(commonBinSema.getScale(), loc)});
    if (useSigned != commonBinSema.isSigned())
      result =
          castIntegerToSignedness(builder, loc, result, commonBinSema.isSigned());
    return createFixedToFixed(result, commonBinSema, commonSema);
  }

  mlir::Value createDiv(mlir::Value lhs,
                        const llvm::FixedPointSemantics &lhsSema,
                        mlir::Value rhs,
                        const llvm::FixedPointSemantics &rhsSema) const {
    auto commonSema = lhsSema.getCommonSemantics(rhsSema);
    auto commonBinSema = getCommonBinopSemantic(lhsSema, rhsSema);
    bool useSigned = commonBinSema.isSigned() || commonBinSema.hasUnsignedPadding();
    mlir::Value wideLhs = createFixedToFixed(lhs, lhsSema, commonBinSema);
    mlir::Value wideRhs = createFixedToFixed(rhs, rhsSema, commonBinSema);
    wideLhs = castIntegerToSignedness(builder, loc, wideLhs, useSigned);
    wideRhs = castIntegerToSignedness(builder, loc, wideRhs, useSigned);

    llvm::StringRef intrinsicName;
    if (commonBinSema.isSaturated())
      intrinsicName = useSigned ? "sdiv.fix.sat" : "udiv.fix.sat";
    else
      intrinsicName = useSigned ? "sdiv.fix" : "udiv.fix";

    mlir::Value result = createIntrinsic(
        intrinsicName, wideLhs.getType(),
        {wideLhs, wideRhs, builder.getUInt32(commonBinSema.getScale(), loc)});
    if (useSigned != commonBinSema.isSigned())
      result =
          castIntegerToSignedness(builder, loc, result, commonBinSema.isSigned());
    return createFixedToFixed(result, commonBinSema, commonSema);
  }

  mlir::Value createShl(mlir::Value lhs,
                        const llvm::FixedPointSemantics &lhsSema,
                        mlir::Value rhs) const {
    bool useSigned = lhsSema.isSigned() || lhsSema.hasUnsignedPadding();
    mlir::Value rhsCast =
        createIntCast(rhs, lhsSema.getWidth(), lhsSema.isSigned(),
                      /*srcIsSigned=*/false);

    if (!lhsSema.isSaturated())
      return builder.createShiftLeft(loc, lhs, rhsCast);

    mlir::Value lhsCast = castIntegerToSignedness(builder, loc, lhs, useSigned);
    rhsCast = createIntCast(rhs, lhsSema.getWidth(), useSigned,
                            /*srcIsSigned=*/false);
    mlir::Value result =
        createIntrinsic(useSigned ? "sshl.sat" : "ushl.sat", lhsCast.getType(),
                        {lhsCast, rhsCast});
    if (useSigned != lhsSema.isSigned())
      result = castIntegerToSignedness(builder, loc, result, lhsSema.isSigned());
    return result;
  }

  mlir::Value createShr(mlir::Value lhs,
                        const llvm::FixedPointSemantics &lhsSema,
                        mlir::Value rhs) const {
    mlir::Value rhsCast =
        createIntCast(rhs, lhsSema.getWidth(), lhsSema.isSigned(),
                      /*srcIsSigned=*/false);
    lhs = castIntegerToSignedness(builder, loc, lhs, lhsSema.isSigned());
    return builder.createShiftRight(loc, lhs, rhsCast);
  }

  mlir::Value createCompare(mlir::Value lhs,
                            const llvm::FixedPointSemantics &lhsSema,
                            mlir::Value rhs,
                            const llvm::FixedPointSemantics &rhsSema,
                            cir::CmpOpKind kind) const {
    auto commonBinSema = getCommonBinopSemantic(lhsSema, rhsSema);
    mlir::Value wideLhs = createFixedToFixed(lhs, lhsSema, commonBinSema);
    mlir::Value wideRhs = createFixedToFixed(rhs, rhsSema, commonBinSema);
    return createCompare(kind, wideLhs, wideRhs, commonBinSema.isSigned());
  }
};

mlir::Value ScalarExprEmitter::emitFixedPointConversion(
    mlir::Value src, QualType srcType, QualType dstType, SourceLocation loc,
    cir::FastMathFlags fmf, FPOptions fpfeatures) {
  CIRFixedPointBuilder fpBuilder(cgf, cgf.getLoc(loc), fmf, fpfeatures);
  ASTContext &ctx = cgf.getContext();
  if (srcType->isRealFloatingType())
    return fpBuilder.createFloatingToFixed(src,
                                           ctx.getFixedPointSemantics(dstType));
  if (dstType->isRealFloatingType())
    return fpBuilder.createFixedToFloating(src,
                                           ctx.getFixedPointSemantics(srcType),
                                           cgf.convertType(dstType));

  auto srcFPSema = ctx.getFixedPointSemantics(srcType);
  auto dstFPSema = ctx.getFixedPointSemantics(dstType);
  if (dstType->isIntegerType())
    return fpBuilder.createFixedToInteger(src, srcFPSema, dstFPSema.getWidth(),
                                          dstFPSema.isSigned());
  if (srcType->isIntegerType())
    return fpBuilder.createIntegerToFixed(src, srcFPSema.isSigned(), dstFPSema);
  return fpBuilder.createFixedToFixed(src, srcFPSema, dstFPSema);
}

mlir::Value ScalarExprEmitter::emitFixedPointBinOp(const BinOpInfo &ops) {
  QualType resultType = ops.fullType;
  QualType lhsType;
  QualType rhsType;

  if (const auto *binOp = dyn_cast<BinaryOperator>(ops.e)) {
    rhsType = binOp->getRHS()->getType();
    if (const auto *cao = dyn_cast<CompoundAssignOperator>(binOp)) {
      lhsType = cao->getComputationLHSType();
      resultType = cao->getComputationResultType();
    } else {
      lhsType = binOp->getLHS()->getType();
    }
  } else if (const auto *unOp = dyn_cast<UnaryOperator>(ops.e)) {
    lhsType = unOp->getSubExpr()->getType();
    rhsType = lhsType;
  } else {
    llvm_unreachable("unexpected fixed-point op without AST node");
  }

  ASTContext &ctx = cgf.getContext();
  auto lhsSema = ctx.getFixedPointSemantics(lhsType);
  auto rhsSema = ctx.getFixedPointSemantics(rhsType);
  auto commonSema = lhsSema.getCommonSemantics(rhsSema);
  auto resultSema = ctx.getFixedPointSemantics(resultType);

  CIRFixedPointBuilder fpBuilder(
      cgf, cgf.getLoc(ops.loc),
      CIRGenFunction::getFastMathFlags(ops.fpfeatures), ops.fpfeatures);

  mlir::Value result;
  switch (ops.opcode) {
  case BO_AddAssign:
  case BO_Add:
    result = fpBuilder.createAdd(ops.lhs, lhsSema, ops.rhs, rhsSema);
    break;
  case BO_SubAssign:
  case BO_Sub:
    result = fpBuilder.createSub(ops.lhs, lhsSema, ops.rhs, rhsSema);
    break;
  case BO_MulAssign:
  case BO_Mul:
    result = fpBuilder.createMul(ops.lhs, lhsSema, ops.rhs, rhsSema);
    break;
  case BO_DivAssign:
  case BO_Div:
    result = fpBuilder.createDiv(ops.lhs, lhsSema, ops.rhs, rhsSema);
    break;
  case BO_ShlAssign:
  case BO_Shl:
    result = fpBuilder.createShl(ops.lhs, lhsSema, ops.rhs);
    break;
  case BO_ShrAssign:
  case BO_Shr:
    result = fpBuilder.createShr(ops.lhs, lhsSema, ops.rhs);
    break;
  case BO_LT:
    return fpBuilder.createCompare(ops.lhs, lhsSema, ops.rhs, rhsSema,
                                   cir::CmpOpKind::lt);
  case BO_GT:
    return fpBuilder.createCompare(ops.lhs, lhsSema, ops.rhs, rhsSema,
                                   cir::CmpOpKind::gt);
  case BO_LE:
    return fpBuilder.createCompare(ops.lhs, lhsSema, ops.rhs, rhsSema,
                                   cir::CmpOpKind::le);
  case BO_GE:
    return fpBuilder.createCompare(ops.lhs, lhsSema, ops.rhs, rhsSema,
                                   cir::CmpOpKind::ge);
  case BO_EQ:
    return fpBuilder.createCompare(ops.lhs, lhsSema, ops.rhs, rhsSema,
                                   cir::CmpOpKind::eq);
  case BO_NE:
    return fpBuilder.createCompare(ops.lhs, lhsSema, ops.rhs, rhsSema,
                                   cir::CmpOpKind::ne);
  case BO_Cmp:
  case BO_LAnd:
  case BO_LOr:
    llvm_unreachable("found unimplemented fixed-point binary operation");
  case BO_PtrMemD:
  case BO_PtrMemI:
  case BO_Rem:
  case BO_Xor:
  case BO_And:
  case BO_Or:
  case BO_Assign:
  case BO_RemAssign:
  case BO_AndAssign:
  case BO_XorAssign:
  case BO_OrAssign:
  case BO_Comma:
    llvm_unreachable("found unsupported binary operation for fixed-point types");
  }

  bool isShift = BinaryOperator::isShiftOp(ops.opcode) ||
                 BinaryOperator::isShiftAssignOp(ops.opcode);
  return fpBuilder.createFixedToFixed(result, isShift ? lhsSema : commonSema,
                                      resultSema);
}

mlir::Value ScalarExprEmitter::emitMul(const BinOpInfo &ops) {
  const mlir::Location loc = cgf.getLoc(ops.loc);
  if (ops.fullType->isSignedIntegerOrEnumerationType()) {
    switch (cgf.getLangOpts().getSignedOverflowBehavior()) {
    case LangOptions::SOB_Defined:
      if (!cgf.sanOpts.has(SanitizerKind::SignedIntegerOverflow))
        return builder.createMul(loc, ops.lhs, ops.rhs);
      [[fallthrough]];
    case LangOptions::SOB_Undefined:
      if (!cgf.sanOpts.has(SanitizerKind::SignedIntegerOverflow))
        return builder.createNSWMul(loc, ops.lhs, ops.rhs);
      [[fallthrough]];
    case LangOptions::SOB_Trapping:
      if (canElideOverflowCheck(cgf.getContext(), ops))
        return builder.createNSWMul(loc, ops.lhs, ops.rhs);
      return emitOverflowCheckedBinOp(cgf, builder, ops);
    }
  }
  if (shouldEmitUnsignedOverflowCheck(cgf, ops))
    return emitOverflowCheckedBinOp(cgf, builder, ops);
  if (ops.fullType->isConstantMatrixType()) {
    auto *binOp = cast<BinaryOperator>(ops.e);
    auto *lhsMatrixTy =
        dyn_cast<ConstantMatrixType>(binOp->getLHS()->getType().getCanonicalType());
    auto *rhsMatrixTy =
        dyn_cast<ConstantMatrixType>(binOp->getRHS()->getType().getCanonicalType());
    if (lhsMatrixTy && rhsMatrixTy) {
      mlir::Type resultTy = cgf.convertType(ops.fullType);
      return cir::LLVMIntrinsicCallOp::create(
                 builder, loc,
                 cgf.cgm.getStringAttrCached("matrix.multiply"), resultTy,
                 mlir::ValueRange{ops.lhs, ops.rhs,
                                  builder.getUInt32(lhsMatrixTy->getNumRows(), loc),
                                  builder.getUInt32(lhsMatrixTy->getNumColumns(), loc),
                                  builder.getUInt32(rhsMatrixTy->getNumColumns(), loc)})
          .getResult();
    }
  }

  if (cir::isFPOrVectorOfFPType(ops.lhs.getType())) {
    cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(ops.fpfeatures);
    auto *ctx = &cgf.getMLIRContext();
    return builder.createFMul(loc, ops.lhs, ops.rhs, fmf,
        cgf.getFPExceptionBehavior(ctx, ops.fpfeatures),
        CIRGenFunction::getRoundingMode(ctx, ops.fpfeatures));
  }

  if (ops.isFixedPointOp())
    return emitFixedPointBinOp(ops);

  return cir::BinOp::create(builder, cgf.getLoc(ops.loc),
                            cgf.convertType(ops.fullType), cir::BinOpKind::Mul,
                            ops.lhs, ops.rhs, {}, {}, {}, {}, {}, {});
}
mlir::Value ScalarExprEmitter::emitDiv(const BinOpInfo &ops) {
  assert(!cir::MissingFeatures::matrixType());
  if (ops.isFixedPointOp())
    return emitFixedPointBinOp(ops);
  if (cir::isFPOrVectorOfFPType(ops.lhs.getType())) {
    emitFloatDivSanitizerCheck(cgf, builder, ops);
    cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(ops.fpfeatures);
    // OGCG formerly had setDivFPAccuracy for !fpmath metadata; FP accuracy
    // is now handled through FastMathFlags and FPOptions at the op level.
    auto *ctx = &cgf.getMLIRContext();
    return builder.createFDiv(cgf.getLoc(ops.loc), ops.lhs, ops.rhs, fmf,
        cgf.getFPExceptionBehavior(ctx, ops.fpfeatures),
        CIRGenFunction::getRoundingMode(ctx, ops.fpfeatures));
  }
  if (ops.fullType->isIntegerType() &&
      (cgf.sanOpts.has(SanitizerKind::IntegerDivideByZero) ||
       cgf.sanOpts.has(SanitizerKind::SignedIntegerOverflow)) &&
      (ops.mayHaveIntegerDivisionByZero() || ops.mayHaveIntegerOverflow()))
    emitIntegerDivAndRemSanitizerCheck(cgf, builder, ops);
  return cir::BinOp::create(builder, cgf.getLoc(ops.loc),
                            cgf.convertType(ops.fullType), cir::BinOpKind::Div,
                            ops.lhs, ops.rhs, {}, {}, {}, {}, {}, {});
}
mlir::Value ScalarExprEmitter::emitRem(const BinOpInfo &ops) {
  if (ops.isFixedPointOp()) {
    cgf.cgm.errorNYI("fixed point");
    return {};
  }
  if (ops.fullType->isIntegerType() &&
      (cgf.sanOpts.has(SanitizerKind::IntegerDivideByZero) ||
       cgf.sanOpts.has(SanitizerKind::SignedIntegerOverflow)) &&
      (ops.mayHaveIntegerDivisionByZero() || ops.mayHaveIntegerOverflow()))
    emitIntegerDivAndRemSanitizerCheck(cgf, builder, ops);
  // OGCG allows floating-point remainder for HLSL in EmitRem.
  // CIR emits cir.binop rem for both integer and floating-point operands;
  // lowering maps FP rem to llvm.frem and integer rem to llvm.[su]rem.
  return cir::BinOp::create(builder, cgf.getLoc(ops.loc),
                            cgf.convertType(ops.fullType), cir::BinOpKind::Rem,
                            ops.lhs, ops.rhs, {}, {}, {}, {}, {}, {});
}

// Check if a CIR value is a floating-point multiply with no existing uses,
// suitable for folding into an fmuladd. Returns the defining BinOp if so.
// Is `val` a floating-point value produced entirely from constants? True for a
// cir.const FP, or a cir.binop (add/sub/mul/div) whose operands are themselves
// constant FP expressions. Classic clang's IRBuilder constant-folds such
// expressions into a single ConstantFP as they are built, so they are never a
// live op there. ClangIR has no FP-binop folder, so e.g. `(8.4/6.3)*-3.8`
// survives as live cir.binop ops; if fused into an fmuladd the whole constant
// product gets a single (fused) rounding, diverging from classic clang and gcc
// on targets with a real fma (aarch64). Depth-bounded to avoid pathological
// recursion on very deep expressions.
static bool isConstantFPExpr(mlir::Value val, unsigned depth = 0) {
  if (depth > 16)
    return false;
  if (!cir::isFPOrVectorOfFPType(val.getType()))
    return false;
  mlir::Operation *op = val.getDefiningOp();
  if (!op)
    return false;
  if (mlir::isa<cir::ConstantOp>(op))
    return true;
  if (auto b = mlir::dyn_cast<cir::BinOp>(op)) {
    switch (b.getKind()) {
    case cir::BinOpKind::Add:
    case cir::BinOpKind::Sub:
    case cir::BinOpKind::Mul:
    case cir::BinOpKind::Div:
      return isConstantFPExpr(b.getLhs(), depth + 1) &&
             isConstantFPExpr(b.getRhs(), depth + 1);
    default:
      return false;
    }
  }
  return false;
}

static cir::BinOp isFMul(mlir::Value val) {
  auto binOp = val.getDefiningOp<cir::BinOp>();
  if (!binOp)
    return nullptr;
  if (binOp.getKind() != cir::BinOpKind::Mul)
    return nullptr;
  if (!cir::isFPOrVectorOfFPType(val.getType()))
    return nullptr;
  // Do not fuse a multiply that is a wholly constant FP expression into an
  // fmuladd. Classic clang would have already folded such a product into a
  // single ConstantFP (so it is never a live fusable multiply); fusing it here
  // applies a single (fused) rounding to the constant product, diverging from
  // classic clang and gcc on targets with a real fma (e.g. aarch64).
  if (isConstantFPExpr(binOp))
    return nullptr;
  return binOp;
}

// Check if a CIR value is a floating-point negation (UnaryOp with Minus kind).
// If so, return the operand being negated.
static mlir::Value peekThroughFNeg(mlir::Value val) {
  auto unaryOp = val.getDefiningOp<cir::UnaryOp>();
  if (!unaryOp)
    return {};
  if (unaryOp.getKind() != cir::UnaryOpKind::Minus)
    return {};
  if (!cir::isFPOrVectorOfFPType(val.getType()))
    return {};
  return unaryOp.getInput();
}

// Try to emit an fmuladd intrinsic for a floating-point add or sub whose
// operand is a multiply. This mirrors OGCG's tryEmitFMulAdd / buildFMulAdd
// pattern (CGExprScalar.cpp). Only applies when -ffp-contract=on.
static mlir::Value tryEmitFMulAdd(const BinOpInfo &ops,
                                  CIRGenFunction &cgf,
                                  CIRGenBuilderTy &builder,
                                  bool isSub = false) {
  assert((ops.opcode == BO_Add || ops.opcode == BO_AddAssign ||
          ops.opcode == BO_Sub || ops.opcode == BO_SubAssign) &&
         "Only fadd/fsub can be the root of an fmuladd.");

  // Only emit fmuladd for -ffp-contract=on (within-statement contraction).
  if (!ops.fpfeatures.allowFPContractWithinStatement())
    return {};

  mlir::Value lhs = ops.lhs;
  mlir::Value rhs = ops.rhs;

  // Peek through fneg on LHS.
  bool negLHS = false;
  if (mlir::Value inner = peekThroughFNeg(lhs)) {
    if (lhs.use_empty() && inner.hasOneUse()) {
      lhs = inner;
      negLHS = true;
    }
  }

  // Peek through fneg on RHS.
  bool negRHS = false;
  if (mlir::Value inner = peekThroughFNeg(rhs)) {
    if (rhs.use_empty() && inner.hasOneUse()) {
      rhs = inner;
      negRHS = true;
    }
  }

  const mlir::Location loc = cgf.getLoc(ops.loc);
  cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(ops.fpfeatures);

  auto buildFMulAdd = [&](cir::BinOp mulOp, mlir::Value addend,
                          bool negMul, bool negAdd) -> mlir::Value {
    mlir::Value mulOp0 = mulOp.getLhs();
    mlir::Value mulOp1 = mulOp.getRhs();
    if (negMul)
      mulOp0 = cir::UnaryOp::create(builder, loc, mulOp0.getType(),
                                    cir::UnaryOpKind::Minus, mulOp0,
                                    /*nsw=*/false, fmf);
    if (negAdd)
      addend = cir::UnaryOp::create(builder, loc, addend.getType(),
                                    cir::UnaryOpKind::Minus, addend,
                                    /*nsw=*/false, fmf);
    auto result = cir::FMulAddOp::create(builder, loc, mulOp0, mulOp1, addend,
                                         fmf);
    // Attach FP constraint attributes when constrained, so lowering can emit
    // constrained.fmuladd instead of regular fmuladd.
    if (ops.fpfeatures.isFPConstrained() || cgf.IsFPConstrained) {
      auto *ctx = &cgf.getMLIRContext();
      if (auto fpExcept =
              cgf.getFPExceptionBehavior(ctx, ops.fpfeatures))
        result->setAttr("cir.fp_exception", fpExcept);
      if (auto roundMode =
              CIRGenFunction::getRoundingMode(ctx, ops.fpfeatures))
        result->setAttr("cir.rounding_mode", roundMode);
    }
    // Erase the original mul since it's been folded into fmuladd.
    mulOp->erase();
    // If we peeked through fneg, erase it too.
    return result;
  };

  // Check if LHS is an fmul.
  if (cir::BinOp lhsMul = isFMul(lhs)) {
    if (lhsMul->use_empty() || negLHS) {
      if (negLHS) {
        // Erase the fneg we peeked through.
        ops.lhs.getDefiningOp()->erase();
      }
      return buildFMulAdd(lhsMul, ops.rhs, negLHS, isSub);
    }
  }

  // Check if RHS is an fmul.
  if (cir::BinOp rhsMul = isFMul(rhs)) {
    if (rhsMul->use_empty() || negRHS) {
      if (negRHS) {
        // Erase the fneg we peeked through.
        ops.rhs.getDefiningOp()->erase();
      }
      return buildFMulAdd(rhsMul, ops.lhs, isSub ^ negRHS, false);
    }
  }

  return {};
}

mlir::Value ScalarExprEmitter::emitAdd(const BinOpInfo &ops) {
  if (mlir::isa<cir::PointerType>(ops.lhs.getType()) ||
      mlir::isa<cir::PointerType>(ops.rhs.getType()))
    return emitPointerArithmetic(cgf, ops, /*isSubtraction=*/false);

  const mlir::Location loc = cgf.getLoc(ops.loc);
  if (ops.fullType->isSignedIntegerOrEnumerationType()) {
    switch (cgf.getLangOpts().getSignedOverflowBehavior()) {
    case LangOptions::SOB_Defined:
      if (!cgf.sanOpts.has(SanitizerKind::SignedIntegerOverflow))
        return builder.createAdd(loc, ops.lhs, ops.rhs);
      [[fallthrough]];
    case LangOptions::SOB_Undefined:
      if (!cgf.sanOpts.has(SanitizerKind::SignedIntegerOverflow))
        return builder.createNSWAdd(loc, ops.lhs, ops.rhs);
      [[fallthrough]];
    case LangOptions::SOB_Trapping:
      if (canElideOverflowCheck(cgf.getContext(), ops))
        return builder.createNSWAdd(loc, ops.lhs, ops.rhs);
      return emitOverflowCheckedBinOp(cgf, builder, ops);
    }
  }
  if (shouldEmitUnsignedOverflowCheck(cgf, ops))
    return emitOverflowCheckedBinOp(cgf, builder, ops);

  if (cir::isFPOrVectorOfFPType(ops.lhs.getType())) {
    // Try to form an fmuladd (for -ffp-contract=on).
    if (mlir::Value fMulAdd = tryEmitFMulAdd(ops, cgf, builder))
      return fMulAdd;
    cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(ops.fpfeatures);
    auto *ctx = &cgf.getMLIRContext();
    return builder.createFAdd(loc, ops.lhs, ops.rhs, fmf,
        cgf.getFPExceptionBehavior(ctx, ops.fpfeatures),
        CIRGenFunction::getRoundingMode(ctx, ops.fpfeatures));
  }

  if (ops.isFixedPointOp())
    return emitFixedPointBinOp(ops);

  return cir::BinOp::create(builder, loc, cgf.convertType(ops.fullType),
                            cir::BinOpKind::Add, ops.lhs, ops.rhs,
                            {}, {}, {}, {}, {}, {});
}

mlir::Value ScalarExprEmitter::emitSub(const BinOpInfo &ops) {
  const mlir::Location loc = cgf.getLoc(ops.loc);
  // The LHS is always a pointer if either side is.
  if (!mlir::isa<cir::PointerType>(ops.lhs.getType())) {
    if (ops.fullType->isSignedIntegerOrEnumerationType()) {
      switch (cgf.getLangOpts().getSignedOverflowBehavior()) {
      case LangOptions::SOB_Defined: {
        if (!cgf.sanOpts.has(SanitizerKind::SignedIntegerOverflow))
          return builder.createSub(loc, ops.lhs, ops.rhs);
        [[fallthrough]];
      }
      case LangOptions::SOB_Undefined:
        if (!cgf.sanOpts.has(SanitizerKind::SignedIntegerOverflow))
          return builder.createNSWSub(loc, ops.lhs, ops.rhs);
        [[fallthrough]];
      case LangOptions::SOB_Trapping:
        if (canElideOverflowCheck(cgf.getContext(), ops))
          return builder.createNSWSub(loc, ops.lhs, ops.rhs);
        return emitOverflowCheckedBinOp(cgf, builder, ops);
      }
    }
    if (shouldEmitUnsignedOverflowCheck(cgf, ops))
      return emitOverflowCheckedBinOp(cgf, builder, ops);

    if (cir::isFPOrVectorOfFPType(ops.lhs.getType())) {
      // Try to form an fmuladd (for -ffp-contract=on).
      if (mlir::Value fMulAdd = tryEmitFMulAdd(ops, cgf, builder, /*isSub=*/true))
        return fMulAdd;
      cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(ops.fpfeatures);
      auto *ctx = &cgf.getMLIRContext();
      return builder.createFSub(loc, ops.lhs, ops.rhs, fmf,
          cgf.getFPExceptionBehavior(ctx, ops.fpfeatures),
          CIRGenFunction::getRoundingMode(ctx, ops.fpfeatures));
    }

    if (ops.isFixedPointOp())
      return emitFixedPointBinOp(ops);

    return cir::BinOp::create(builder, cgf.getLoc(ops.loc),
                              cgf.convertType(ops.fullType), cir::BinOpKind::Sub,
                              ops.lhs, ops.rhs,
                              {}, {}, {}, {}, {}, {});
  }

  // If the RHS is not a pointer, then we have normal pointer
  // arithmetic.
  if (!mlir::isa<cir::PointerType>(ops.rhs.getType()))
    return emitPointerArithmetic(cgf, ops, /*isSubtraction=*/true);

  // Otherwise, this is a pointer subtraction

  // Do the raw subtraction part.
  //
  // TODO(cir): note for LLVM lowering out of this; when expanding this into
  // LLVM we shall take VLA's, division by element size, etc.
  //
  // See more in `EmitSub` in CGExprScalar.cpp.
  assert(!cir::MissingFeatures::llvmLoweringPtrDiffConsidersPointee());

  // If the pointer types differ (e.g., different address spaces), cast the RHS
  // to match the LHS type. This handles OpenCL generic/global pointer
  // subtraction where the AST doesn't insert implicit address space casts.
  mlir::Value lhs = ops.lhs;
  mlir::Value rhs = ops.rhs;
  if (lhs.getType() != rhs.getType()) {
    auto lhsPtrTy = mlir::cast<cir::PointerType>(lhs.getType());
    auto rhsPtrTy = mlir::cast<cir::PointerType>(rhs.getType());
    if (lhsPtrTy.getAddrSpace() != rhsPtrTy.getAddrSpace()) {
      rhs = builder.createAddrSpaceCast(rhs, lhsPtrTy);
    } else {
      rhs = builder.createBitcast(rhs, lhsPtrTy);
    }
  }

  mlir::Value diff = cir::PtrDiffOp::create(builder, cgf.getLoc(ops.loc),
                                            cgf.ptrDiffTy, lhs, rhs);

  const BinaryOperator *expr = cast<BinaryOperator>(ops.e);
  QualType elementType = expr->getLHS()->getType()->getPointeeType();

  mlir::Value divisor;

  // Check if this is a VLA pointee type.
  if (const auto *vla = cgf.getContext().getAsVariableArrayType(elementType)) {
    auto vlaSize = cgf.getVLASize(vla);
    elementType = vlaSize.type;
    divisor = vlaSize.numElts;

    // Note: Unlike OGCG which works with raw byte differences, CIR's
    // PtrDiffOp already divides by the static pointee element size in its
    // LLVM lowering. So we only divide by the VLA dimension count here,
    // not by (numElts * eltSize) as OGCG does.
    // TODO: If PtrDiffOp changes to return raw byte differences
    // (llvmLoweringPtrDiffConsidersPointee), add eltSize multiplication
    // here, matching OGCG's CreateNUWMul pattern.
    if (divisor.getType() != cgf.ptrDiffTy)
      divisor = builder.createIntCast(divisor, cgf.ptrDiffTy);
  } else {
    // cir::ptrdiff correctly computes the ABI difference of 2 pointers. We
    // do not need to compute anything else here. We just return it.
    return diff;
  }

  // OGCG uses CreateExactSDiv here because C defines pointer difference only
  // when both pointers are into the same array, making the division exact.
  // CIR records the arithmetic as a normal signed division in CIR; exactness
  // is an LLVM IR flag and not currently modeled at the CIR binop level.
  return cir::BinOp::create(builder, loc, cgf.ptrDiffTy, cir::BinOpKind::Div,
                            diff, divisor, {}, {}, {}, {}, {}, {});
}

// Helper to apply OpenCL-style shift masking. It handles both vector and scalar
// RHS. According to OpenCL 6.3j, shift values are effectively modulo word size
// of LHS.
static mlir::Value maskShiftAmount(CIRGenFunction &cgf,
                                   CIRGenBuilderTy &builder,
                                   const BinOpInfo &ops) {
  // LHS determines bit width
  unsigned bitWidth = [&]() {
    if (auto vecTy = mlir::dyn_cast<cir::VectorType>(ops.lhs.getType())) {
      auto elemTy = mlir::cast<cir::IntType>(vecTy.getElementType());
      return elemTy.getWidth();
    }
    auto lhsTy = mlir::cast<cir::IntType>(ops.lhs.getType());
    return lhsTy.getWidth();
  }();

  // OGCG uses URem for non-power-of-2 bit widths (e.g. _BitInt(24))
  // and AND for power-of-2 widths. CIR always uses AND, which gives
  // wrong results for non-power-of-2 widths since (width-1) is not
  // a valid bitmask in that case.
  // For non-power-of-2 bit widths (e.g. _BitInt(24)), AND masking with
  // (width-1) doesn't work because (width-1) isn't a valid bitmask. Use
  // URem instead, matching OGCG (CGExprScalar.cpp:4127-4143).
  bool isPowerOfTwo = (bitWidth & (bitWidth - 1)) == 0;

  mlir::Location loc = ops.rhs.getLoc();

  // Vector RHS: perform element-wise masking
  if (auto vecTy = mlir::dyn_cast<cir::VectorType>(ops.rhs.getType())) {
    auto elemTy = mlir::cast<cir::IntType>(vecTy.getElementType());
    mlir::Value widthScalar = builder.getConstInt(
        loc, elemTy, isPowerOfTwo ? (bitWidth - 1) : bitWidth);
    mlir::Value widthVec =
        cir::VecSplatOp::create(builder, loc, vecTy, widthScalar).getResult();
    auto opKind = isPowerOfTwo ? cir::BinOpKind::And : cir::BinOpKind::Rem;
    return cir::BinOp::create(builder, loc, vecTy, opKind, ops.rhs,
                              widthVec, {}, {}, {}, {}, {}, {});
  }

  // Scalar RHS
  auto intTy = mlir::cast<cir::IntType>(ops.rhs.getType());
  if (isPowerOfTwo) {
    // Power-of-2: use AND with (bitWidth - 1)
    mlir::Value mask = builder.getConstInt(loc, intTy, bitWidth - 1);
    return cir::BinOp::create(builder, loc, intTy, cir::BinOpKind::And,
                              ops.rhs, mask, {}, {}, {}, {}, {}, {});
  }
  // Non-power-of-2: use URem with bitWidth
  mlir::Value width = builder.getConstInt(loc, intTy, bitWidth);
  return cir::BinOp::create(builder, loc, intTy, cir::BinOpKind::Rem, ops.rhs,
                            width, {}, {}, {}, {}, {}, {});
}

mlir::Value ScalarExprEmitter::emitShl(const BinOpInfo &ops) {
  // TODO: This misses out on the sanitizer check below.
  if (ops.isFixedPointOp())
    return emitFixedPointBinOp(ops);

  // CIR accepts scalar shifts between different types, but the verifier
  // requires vector shift operands to have matching element widths. Cast the
  // RHS to match the LHS element width for vector shifts.
  // Matches OGCG CGExprScalar.cpp EmitShl which calls CreateIntCast on RHS.
  mlir::Value rhs = ops.rhs;
  if (auto lhsVecTy = mlir::dyn_cast<cir::VectorType>(ops.lhs.getType())) {
    auto rhsVecTy = mlir::cast<cir::VectorType>(rhs.getType());
    auto lhsEleW = mlir::cast<cir::IntType>(lhsVecTy.getElementType()).getWidth();
    auto rhsEleW = mlir::cast<cir::IntType>(rhsVecTy.getElementType()).getWidth();
    if (lhsEleW != rhsEleW) {
      // OGCG uses CreateIntCast(RHS, LHS->getType(), false) — the false
      // means unsigned (zext). Shift amounts should always be zero-extended
      // regardless of the source type's signedness.
      auto rhsEleIntTy = mlir::cast<cir::IntType>(rhsVecTy.getElementType());
      if (rhsEleIntTy.isSigned()) {
        auto unsignedEleTy = cir::IntType::get(
            builder.getContext(), rhsEleIntTy.getWidth(), /*isSigned=*/false);
        auto unsignedVecTy = cir::VectorType::get(
            unsignedEleTy, rhsVecTy.getSize());
        rhs = builder.createIntCast(rhs, unsignedVecTy);
      }
      rhs = builder.createIntCast(rhs, lhsVecTy);
    }
  }

  bool sanitizeSignedBase = cgf.sanOpts.has(SanitizerKind::ShiftBase) &&
                            ops.compType->hasSignedIntegerRepresentation() &&
                            !cgf.getLangOpts().isSignedOverflowDefined() &&
                            !cgf.getLangOpts().CPlusPlus20;
  bool sanitizeUnsignedBase =
      cgf.sanOpts.has(SanitizerKind::UnsignedShiftBase) &&
      ops.compType->hasUnsignedIntegerRepresentation();
  bool sanitizeBase = sanitizeSignedBase || sanitizeUnsignedBase;
  bool sanitizeExponent = cgf.sanOpts.has(SanitizerKind::ShiftExponent);

  // OpenCL 6.3j: shift values are effectively % word size of LHS.
  // OGCG also applies this masking for HLSL.
  if (cgf.getLangOpts().OpenCL || cgf.getLangOpts().HLSL) {
    BinOpInfo maskedOps = ops;
    maskedOps.rhs = rhs;
    mlir::Value rhsMasked = maskShiftAmount(cgf, builder, maskedOps);
    return builder.createShiftLeft(cgf.getLoc(ops.loc), ops.lhs, rhsMasked);
  } else if ((sanitizeBase || sanitizeExponent) &&
             mlir::isa<cir::IntType>(ops.lhs.getType())) {
    mlir::Location loc = cgf.getLoc(ops.loc);
    bool rhsIsSigned = ops.rhsHasSignedIntegerRepresentation();

    mlir::Value rhsForExponent =
        castIntegerToSignedness(builder, loc, ops.rhs, /*isSigned=*/false);
    if (rhsForExponent != ops.rhs)
      markNoSanitize(rhsForExponent.getDefiningOp());

    mlir::Value widthMinusOne =
        getMaximumShiftAmount(builder, loc, ops.lhs, rhsForExponent,
                              rhsIsSigned);
    mlir::Value validExponent = cir::CmpOp::create(
        builder, loc, cir::CmpOpKind::le, rhsForExponent, widthMinusOne);
    markNoSanitize(validExponent.getDefiningOp());

    mlir::Value validBase;
    if (sanitizeBase) {
      auto lhsTy = mlir::cast<cir::IntType>(ops.lhs.getType());
      auto rhsPromotedTy =
          cir::IntType::get(builder.getContext(), lhsTy.getWidth(),
                            /*isSigned=*/false);
      mlir::Value rhsPromoted = rhsForExponent;
      if (rhsPromoted.getType() != rhsPromotedTy) {
        rhsPromoted = builder.createIntCast(rhsPromoted, rhsPromotedTy);
        markNoSanitize(rhsPromoted.getDefiningOp());
      }

      mlir::Value lhsForCheck =
          castIntegerToSignedness(builder, loc, ops.lhs, /*isSigned=*/false);
      if (lhsForCheck != ops.lhs)
        markNoSanitize(lhsForCheck.getDefiningOp());

      mlir::Value promotedWidthMinusOne =
          getMaximumShiftAmount(builder, loc, ops.lhs, rhsPromoted,
                                rhsIsSigned);
      validBase = cir::TernaryOp::create(
          builder, loc, validExponent,
          /*trueBuilder=*/
          [&](mlir::OpBuilder &b, mlir::Location ternLoc) {
            auto shiftedOutWidth = builder.createSub(
                ternLoc, promotedWidthMinusOne, rhsPromoted,
                cir::OverflowBehavior::NoUnsignedWrap |
                    cir::OverflowBehavior::NoSignedWrap);
            markNoSanitize(shiftedOutWidth.getDefiningOp());

            mlir::Value bitsShiftedOff =
                builder.createShiftRight(ternLoc, lhsForCheck, shiftedOutWidth);
            markNoSanitize(bitsShiftedOff.getDefiningOp());

            if (sanitizeUnsignedBase || cgf.getLangOpts().CPlusPlus) {
              bitsShiftedOff =
                  builder.createShiftRight(ternLoc, bitsShiftedOff, 1);
              markNoSanitize(bitsShiftedOff.getDefiningOp());
            }

            mlir::Value zero = builder.getConstInt(
                ternLoc, bitsShiftedOff.getType(), 0);
            mlir::Value noOverflow = cir::CmpOp::create(
                builder, ternLoc, cir::CmpOpKind::eq, bitsShiftedOff, zero);
            markNoSanitize(noOverflow.getDefiningOp());
            cir::YieldOp::create(b, ternLoc, noOverflow);
          },
          /*falseBuilder=*/
          [&](mlir::OpBuilder &b, mlir::Location ternLoc) {
            cir::YieldOp::create(b, ternLoc,
                                 mlir::ValueRange{builder.getBool(true, ternLoc)});
          })
                      .getResult();
    }

    auto *bo = llvm::dyn_cast<BinaryOperator>(ops.e);
    QualType lhsExprTy = bo ? bo->getLHS()->getType() : ops.fullType;
    QualType rhsExprTy = bo ? bo->getRHS()->getType() : ops.fullType;
    SourceLocation sourceLoc = ops.e ? ops.e->getExprLoc() : ops.loc.getBegin();

    auto combineChecks = [&](mlir::Value lhsCheck,
                             mlir::Value rhsCheck) -> mlir::Value {
      if (!lhsCheck)
        return rhsCheck;
      if (!rhsCheck)
        return lhsCheck;
      return buildAndNoSanitize(builder, loc, lhsCheck, rhsCheck);
    };

    bool baseTrap = false, baseRecover = false, baseFatal = false;
    bool exponentTrap = false, exponentRecover = false, exponentFatal = false;

    if (sanitizeBase) {
      auto baseKind = sanitizeSignedBase ? SanitizerKind::ShiftBase
                                         : SanitizerKind::UnsignedShiftBase;
      baseTrap = cgf.cgm.getCodeGenOpts().SanitizeTrap.has(baseKind);
      baseRecover =
          !baseTrap && cgf.cgm.getCodeGenOpts().SanitizeRecover.has(baseKind);
      baseFatal = !baseTrap && !baseRecover;
    }
    if (sanitizeExponent) {
      exponentTrap =
          cgf.cgm.getCodeGenOpts().SanitizeTrap.has(SanitizerKind::ShiftExponent);
      exponentRecover =
          !exponentTrap &&
          cgf.cgm.getCodeGenOpts().SanitizeRecover.has(
              SanitizerKind::ShiftExponent);
      exponentFatal = !exponentTrap && !exponentRecover;
    }

    mlir::Value trapPass = combineChecks(baseTrap ? validBase : mlir::Value(),
                                         exponentTrap ? validExponent
                                                      : mlir::Value());
    if (trapPass) {
      auto trapIf = cir::IfOp::create(
          builder, loc, trapPass, /*withElseRegion=*/true,
          [&](mlir::OpBuilder &, mlir::Location ifLoc) {
            builder.createYield(ifLoc);
          },
          [&](mlir::OpBuilder &, mlir::Location ifLoc) {
            emitShiftTrap(builder, ifLoc);
          });
      setSanitizerBranchAttrs(builder, trapIf);
    }

    mlir::Value fatalPass = combineChecks(baseFatal ? validBase : mlir::Value(),
                                          exponentFatal ? validExponent
                                                        : mlir::Value());
    mlir::Value recoverPass =
        combineChecks(baseRecover ? validBase : mlir::Value(),
                      exponentRecover ? validExponent : mlir::Value());

    if (fatalPass || recoverPass) {
      mlir::Value jointPass = combineChecks(fatalPass, recoverPass);
      auto handlerIf = cir::IfOp::create(
          builder, loc, jointPass, /*withElseRegion=*/true,
          [&](mlir::OpBuilder &, mlir::Location ifLoc) {
            builder.createYield(ifLoc);
          },
          [&](mlir::OpBuilder &, mlir::Location ifLoc) {
            if (fatalPass && recoverPass) {
              auto innerIf = cir::IfOp::create(
                  builder, ifLoc, fatalPass, /*withElseRegion=*/true,
                  [&](mlir::OpBuilder &, mlir::Location innerLoc) {
                    emitShiftOutOfBoundsStubCall(
                        cgf, builder, innerLoc, sourceLoc, lhsExprTy, rhsExprTy,
                        ops.lhs, ops.rhs, /*handlerKind=*/"recover");
                    builder.createYield(innerLoc);
                  },
                  [&](mlir::OpBuilder &, mlir::Location innerLoc) {
                    emitShiftOutOfBoundsStubCall(
                        cgf, builder, innerLoc, sourceLoc, lhsExprTy, rhsExprTy,
                        ops.lhs, ops.rhs, /*handlerKind=*/"abort");
                    auto unreachable =
                        cir::UnreachableOp::create(builder, innerLoc);
                    markNoSanitize(unreachable);
                  });
              setSanitizerBranchAttrs(builder, innerIf);
              return;
            }

            llvm::StringRef handlerKind = fatalPass ? "abort" : "recover";
            emitShiftOutOfBoundsStubCall(cgf, builder, ifLoc, sourceLoc,
                                         lhsExprTy, rhsExprTy, ops.lhs, ops.rhs,
                                         handlerKind);
            if (handlerKind == "abort") {
              auto unreachable = cir::UnreachableOp::create(builder, ifLoc);
              markNoSanitize(unreachable);
            } else {
              builder.createYield(ifLoc);
            }
          });
      setSanitizerBranchAttrs(builder, handlerIf);
    }
  }

  return builder.createShiftLeft(cgf.getLoc(ops.loc), ops.lhs, rhs);
}

mlir::Value ScalarExprEmitter::emitShr(const BinOpInfo &ops) {
  // TODO: This misses out on the sanitizer check below.
  if (ops.isFixedPointOp())
    return emitFixedPointBinOp(ops);

  // CIR accepts scalar shifts between different types, but the verifier
  // requires vector shift operands to have matching element widths. Cast the
  // RHS to match the LHS element width for vector shifts.
  // Matches OGCG CGExprScalar.cpp EmitShr which calls CreateIntCast on RHS.
  mlir::Value rhs = ops.rhs;
  if (auto lhsVecTy = mlir::dyn_cast<cir::VectorType>(ops.lhs.getType())) {
    auto rhsVecTy = mlir::cast<cir::VectorType>(rhs.getType());
    auto lhsEleW = mlir::cast<cir::IntType>(lhsVecTy.getElementType()).getWidth();
    auto rhsEleW = mlir::cast<cir::IntType>(rhsVecTy.getElementType()).getWidth();
    if (lhsEleW != rhsEleW) {
      // OGCG uses CreateIntCast(RHS, LHS->getType(), false) — the false
      // means unsigned (zext). Shift amounts should always be zero-extended
      // regardless of the source type's signedness.
      auto rhsEleIntTy = mlir::cast<cir::IntType>(rhsVecTy.getElementType());
      if (rhsEleIntTy.isSigned()) {
        auto unsignedEleTy = cir::IntType::get(
            builder.getContext(), rhsEleIntTy.getWidth(), /*isSigned=*/false);
        auto unsignedVecTy = cir::VectorType::get(
            unsignedEleTy, rhsVecTy.getSize());
        rhs = builder.createIntCast(rhs, unsignedVecTy);
      }
      rhs = builder.createIntCast(rhs, lhsVecTy);
    }
  }

  // OpenCL 6.3j: shift values are effectively % word size of LHS.
  // OGCG also applies this masking for HLSL.
  if (cgf.getLangOpts().OpenCL || cgf.getLangOpts().HLSL) {
    BinOpInfo maskedOps = ops;
    maskedOps.rhs = rhs;
    mlir::Value rhsMasked = maskShiftAmount(cgf, builder, maskedOps);
    return builder.createShiftRight(cgf.getLoc(ops.loc), ops.lhs, rhsMasked);
  } else if (cgf.sanOpts.has(SanitizerKind::ShiftExponent) &&
             mlir::isa<cir::IntType>(ops.lhs.getType())) {
    mlir::Location loc = cgf.getLoc(ops.loc);
    bool rhsIsSigned = ops.rhsHasSignedIntegerRepresentation();

    mlir::Value rhsForExponent =
        castIntegerToSignedness(builder, loc, ops.rhs, /*isSigned=*/false);
    if (rhsForExponent != ops.rhs)
      markNoSanitize(rhsForExponent.getDefiningOp());

    mlir::Value valid = cir::CmpOp::create(
        builder, loc, cir::CmpOpKind::le, rhsForExponent,
        getMaximumShiftAmount(builder, loc, ops.lhs, rhsForExponent,
                              rhsIsSigned));
    markNoSanitize(valid.getDefiningOp());

    auto *bo = llvm::dyn_cast<BinaryOperator>(ops.e);
    QualType lhsExprTy = bo ? bo->getLHS()->getType() : ops.fullType;
    QualType rhsExprTy = bo ? bo->getRHS()->getType() : ops.fullType;
    SourceLocation sourceLoc = ops.e ? ops.e->getExprLoc() : ops.loc.getBegin();

    bool trapShiftExponent =
        cgf.cgm.getCodeGenOpts().SanitizeTrap.has(SanitizerKind::ShiftExponent);
    bool recoverShiftExponent =
        !trapShiftExponent &&
        cgf.cgm.getCodeGenOpts().SanitizeRecover.has(
            SanitizerKind::ShiftExponent);

    if (trapShiftExponent) {
      auto trapIf = cir::IfOp::create(
          builder, loc, valid, /*withElseRegion=*/true,
          [&](mlir::OpBuilder &, mlir::Location ifLoc) {
            builder.createYield(ifLoc);
          },
          [&](mlir::OpBuilder &, mlir::Location ifLoc) {
            emitShiftTrap(builder, ifLoc);
          });
      setSanitizerBranchAttrs(builder, trapIf);
    }

    if (!trapShiftExponent || recoverShiftExponent) {
      auto handlerIf = cir::IfOp::create(
          builder, loc, valid, /*withElseRegion=*/true,
          [&](mlir::OpBuilder &, mlir::Location ifLoc) {
            builder.createYield(ifLoc);
          },
          [&](mlir::OpBuilder &, mlir::Location ifLoc) {
            llvm::StringRef handlerKind =
                recoverShiftExponent ? "recover" : "abort";
            emitShiftOutOfBoundsStubCall(cgf, builder, ifLoc, sourceLoc,
                                         lhsExprTy, rhsExprTy, ops.lhs, ops.rhs,
                                         handlerKind);
            if (handlerKind == "abort") {
              auto unreachable = cir::UnreachableOp::create(builder, ifLoc);
              markNoSanitize(unreachable);
            } else {
              builder.createYield(ifLoc);
            }
          });
      setSanitizerBranchAttrs(builder, handlerIf);
    }
  }

  // Note that we don't need to distinguish unsigned treatment at this
  // point since it will be handled later by LLVM lowering.
  return builder.createShiftRight(cgf.getLoc(ops.loc), ops.lhs, rhs);
}

mlir::Value ScalarExprEmitter::emitAnd(const BinOpInfo &ops) {
  return cir::BinOp::create(builder, cgf.getLoc(ops.loc),
                            cgf.convertType(ops.fullType), cir::BinOpKind::And,
                            ops.lhs, ops.rhs, {}, {}, {}, {}, {}, {});
}
mlir::Value ScalarExprEmitter::emitXor(const BinOpInfo &ops) {
  return cir::BinOp::create(builder, cgf.getLoc(ops.loc),
                            cgf.convertType(ops.fullType), cir::BinOpKind::Xor,
                            ops.lhs, ops.rhs, {}, {}, {}, {}, {}, {});
}
mlir::Value ScalarExprEmitter::emitOr(const BinOpInfo &ops) {
  return cir::BinOp::create(builder, cgf.getLoc(ops.loc),
                            cgf.convertType(ops.fullType), cir::BinOpKind::Or,
                            ops.lhs, ops.rhs, {}, {}, {}, {}, {}, {});
}

// Emit code for an explicit or implicit cast.  Implicit
// casts have to handle a more broad range of conversions than explicit
// casts, as they handle things like function to ptr-to-function decay
// etc.
mlir::Value ScalarExprEmitter::VisitCastExpr(CastExpr *ce) {
  llvm::scope_exit restoreCurCast(
      [this, prev = cgf.CurCast] { cgf.CurCast = prev; });
  cgf.CurCast = ce;

  Expr *subExpr = ce->getSubExpr();
  QualType destTy = ce->getType();
  CastKind kind = ce->getCastKind();

  // OGCG creates CGFPOptionsRAII at the top of VisitCastExpr to set FP options
  // (rounding mode, exceptions) for the entire cast expression evaluation.

  // These cases are generally not written to ignore the result of evaluating
  // their sub-expressions, so we clear this now.
  ignoreResultAssign = false;

  switch (kind) {
  case clang::CK_Dependent: {
    // A dependent cast can survive into CIRGen when a Sema substitution bug
    // leaves SubstNonTypeTemplateParmExpr with its un-substituted type.
    // Treat as transparent — the CIR value already carries the correct
    // concrete MLIR type even though the AST QualType may be dependent.
    return Visit(const_cast<Expr *>(subExpr));
  }
  case clang::CK_BuiltinFnToFnPtr:
    llvm_unreachable("builtin functions are handled elsewhere");

  case CK_CPointerToObjCPointerCast:
  case CK_BlockPointerToObjCPointerCast:
  case CK_AnyPointerToBlockPointerCast:
  case CK_BitCast: {
    mlir::Value src = Visit(const_cast<Expr *>(subExpr));
    mlir::Type dstTy = cgf.convertType(destTy);

    if (auto srcVecTy = mlir::dyn_cast<cir::VectorType>(src.getType())) {
      if (auto dstVecTy = mlir::dyn_cast<cir::VectorType>(dstTy)) {
        if (srcVecTy.getElementType() == dstVecTy.getElementType() &&
            srcVecTy.getIsScalable() != dstVecTy.getIsScalable()) {
          auto loc = cgf.getLoc(subExpr->getSourceRange());
          auto zeroIndex = cgf.getBuilder().getUInt64(0, loc);

          if (!srcVecTy.getIsScalable() && dstVecTy.getIsScalable()) {
            mlir::Value poison = cgf.getBuilder().getConstant(
                loc, cir::PoisonAttr::get(dstTy));
            return cir::LLVMIntrinsicCallOp::create(
                       builder, loc,
                       cgf.cgm.getStringAttrCached("vector.insert"), dstTy,
                       mlir::ValueRange{poison, src, zeroIndex})
                .getResult();
          }

          if (srcVecTy.getIsScalable() && !dstVecTy.getIsScalable())
            return cir::LLVMIntrinsicCallOp::create(
                       builder, loc,
                       cgf.cgm.getStringAttrCached("vector.extract"), dstTy,
                       mlir::ValueRange{src, zeroIndex})
                .getResult();
        }

        // Fixed-length SVE predicate <-> sizeless SVE predicate. The fixed
        // svbool_t is spelled as a compact fixed <N x i8> storage vector while
        // the sizeless svbool_t is <vscale x 16 x i1>; the element types differ
        // (i8 vs i1) so the same-element path above does not fire, and a direct
        // bitcast between a fixed and a scalable vector is invalid. Route the
        // conversion through a scalable <vscale x M x i8> predicate carrier via
        // vector.insert/extract, mirroring classic CGExprScalar.cpp's
        // fixed-length-SVE predicate handling.
        auto isPredElt = [](mlir::Type t, unsigned width) {
          if (mlir::isa<cir::BoolType>(t))
            return width == 1;
          auto intTy = mlir::dyn_cast<cir::IntType>(t);
          return intTy && intTy.getWidth() == width;
        };

        // fixed <N x i8> -> sizeless <vscale x 16 x i1>
        if (!srcVecTy.getIsScalable() && dstVecTy.getIsScalable() &&
            isPredElt(srcVecTy.getElementType(), 8) &&
            isPredElt(dstVecTy.getElementType(), 1)) {
          auto loc = cgf.getLoc(subExpr->getSourceRange());
          auto zeroIndex = cgf.getBuilder().getUInt64(0, loc);
          // Carrier is the scalable i8 vector holding the predicate bytes:
          // <vscale x divideCeil(16, 8) x i8> = <vscale x 2 x i8>.
          unsigned carrierElts =
              llvm::divideCeil(dstVecTy.getSize(), 8);
          auto carrierTy = cir::VectorType::get(
              srcVecTy.getElementType(), carrierElts, /*is_scalable=*/true);
          mlir::Value poison = cgf.getBuilder().getConstant(
              loc, cir::PoisonAttr::get(carrierTy));
          mlir::Value inserted =
              cir::LLVMIntrinsicCallOp::create(
                  builder, loc, cgf.cgm.getStringAttrCached("vector.insert"),
                  carrierTy, mlir::ValueRange{poison, src, zeroIndex})
                  .getResult();
          return cgf.getBuilder().createBitcast(loc, inserted, dstTy);
        }

        // sizeless <vscale x 16 x i1> -> fixed <N x i8>
        if (srcVecTy.getIsScalable() && !dstVecTy.getIsScalable() &&
            isPredElt(srcVecTy.getElementType(), 1) &&
            isPredElt(dstVecTy.getElementType(), 8)) {
          auto loc = cgf.getLoc(subExpr->getSourceRange());
          auto zeroIndex = cgf.getBuilder().getUInt64(0, loc);
          // Bitcast the scalable predicate to its scalable i8 carrier
          // <vscale x divideCeil(16, 8) x i8>, then extract the fixed bytes.
          unsigned carrierElts =
              llvm::divideCeil(srcVecTy.getSize(), 8);
          auto carrierTy = cir::VectorType::get(
              dstVecTy.getElementType(), carrierElts, /*is_scalable=*/true);
          mlir::Value carrier =
              cgf.getBuilder().createBitcast(loc, src, carrierTy);
          return cir::LLVMIntrinsicCallOp::create(
                     builder, loc,
                     cgf.cgm.getStringAttrCached("vector.extract"), dstTy,
                     mlir::ValueRange{carrier, zeroIndex})
              .getResult();
        }
      }
    }

    auto srcPtrTy = mlir::dyn_cast<cir::PointerType>(src.getType());
    auto dstPtrTy = mlir::dyn_cast<cir::PointerType>(dstTy);

    if (srcPtrTy && dstPtrTy &&
        srcPtrTy.getAddrSpace() != dstPtrTy.getAddrSpace()) {
      // When the address spaces differ, we need to handle this carefully.
      // First bitcast to change the pointee type (in the source address space),
      // then address space cast to change the address space.
      if (srcPtrTy.getPointee() != dstPtrTy.getPointee()) {
        auto intermediateTy = cir::PointerType::get(dstPtrTy.getPointee(),
                                                    srcPtrTy.getAddrSpace());
        src = cgf.getBuilder().createBitcast(
            cgf.getLoc(subExpr->getSourceRange()), src, intermediateTy);
      }
      return cgf.getBuilder().createAddrSpaceCast(
          cgf.getLoc(subExpr->getSourceRange()), src, dstTy);
    }

    // CFIUnrelatedCast only applies to pointer casts. This bitcast path is also
    // reached for non-pointer scalar bitcasts (e.g. vector-to-vector casts like
    // (__v4si)v); destTy->getPointeeType() would be null there and crash
    // convertTypeForMem. Match classic CodeGen, which guards on PointerType.
    if (cgf.sanOpts.has(SanitizerKind::CFIUnrelatedCast) &&
        destTy->isPointerType()) {
      mlir::Type destPointeeTy = cgf.convertTypeForMem(destTy->getPointeeType());
      mlir::Value checkPtr = src;
      if (auto srcPtrTy = mlir::dyn_cast<cir::PointerType>(checkPtr.getType());
          srcPtrTy && srcPtrTy.getPointee() != destPointeeTy) {
        auto checkPtrTy =
            cir::PointerType::get(destPointeeTy, srcPtrTy.getAddrSpace());
        checkPtr = cgf.getBuilder().createBitcast(
            cgf.getLoc(subExpr->getSourceRange()), checkPtr, checkPtrTy);
      }
      Address checkAddr(checkPtr, destPointeeTy, cgf.getPointerAlign());
      cgf.emitVTablePtrCheckForCast(
          destTy->getPointeeType(), checkAddr, /*mayBeNull=*/true,
          CIRGenFunction::CFICastCheckKind::UnrelatedCast,
          subExpr->getBeginLoc());
    }

    if (cgf.cgm.getCodeGenOpts().StrictVTablePointers) {
      const QualType srcType = subExpr->getType();
      if (srcType.mayBeNotDynamicClass() && destTy.mayBeDynamicClass()) {
        // Casting to pointer that could carry dynamic information (provided
        // by invariant.group) requires launder.
        src = cir::InvariantGroupOp::create(
            builder, cgf.getLoc(subExpr->getSourceRange()), src);
      } else if (srcType.mayBeDynamicClass() &&
                 destTy.mayBeNotDynamicClass()) {
        // Casting to pointer that does not carry dynamic information
        // (provided by invariant.group) requires stripping it. Note that
        // we don't do it if the source could not be dynamic type and
        // destination could be dynamic because dynamic information is
        // already laundered. It is because launder(strip(src)) ==
        // launder(src), so there is no need to add extra strip before
        // launder.
        src = cir::StripInvariantGroupOp::create(
            builder, cgf.getLoc(subExpr->getSourceRange()), src);
      }
    }

  
    // OGCG updates !heapallocsite metadata on explicit pointer casts by
    // rewriting call-site debug metadata attached to LLVM CallBase values.
    // CIR currently does not represent this LLVM-only debug metadata at CIR
    // level, so there is nothing to update during scalar CIR generation.

    mlir::Value result =
        cgf.getBuilder().createBitcast(cgf.getLoc(subExpr->getSourceRange()),
                                       src, dstTy);
    return cgf.authPointerToPointerCast(result, subExpr->getType(), destTy);
  }
  case CK_AddressSpaceConversion: {
    Expr::EvalResult result;
    if (subExpr->EvaluateAsRValue(result, cgf.getContext()) &&
        result.Val.isNullPointer()) {
      // If e has side effect, it is emitted even if its final result is a
      // null pointer. In that case, a DCE pass should be able to
      // eliminate the useless instructions emitted during translating E.
      if (result.HasSideEffects)
        Visit(subExpr);
      return cgf.cgm.emitNullConstant(destTy,
                                      cgf.getLoc(subExpr->getExprLoc()));
    }

    clang::QualType srcTy = subExpr->IgnoreImpCasts()->getType();
    if (srcTy->isPointerType() || srcTy->isReferenceType())
      srcTy = srcTy->getPointeeType();

    clang::LangAS srcLangAS = srcTy.getAddressSpace();
    mlir::ptr::MemorySpaceAttrInterface subExprAS;
    if (clang::isTargetAddressSpace(srcLangAS))
      subExprAS = cir::toCIRTargetAddressSpace(cgf.getMLIRContext(), srcLangAS);
    else
      subExprAS =
          cir::toCIRLangAddressSpaceAttr(&cgf.getMLIRContext(), srcLangAS);
    // Since target may map different address spaces in AST to the same address
    // space, an address space conversion may end up as a bitcast.
    return cgf.cgm.getTargetCIRGenInfo().performAddrSpaceCast(
        cgf, Visit(subExpr), subExprAS, convertType(destTy));
  }

  case CK_AtomicToNonAtomic:
  case CK_NonAtomicToAtomic:
  case CK_UserDefinedConversion:
    return Visit(const_cast<Expr *>(subExpr));
  case CK_NoOp: {
    // When a NoOp cast changes volatile qualification, we must emit a load
    // through the l-value to respect volatile semantics (OGCG
    // CGExprScalar.cpp:2717).
    if (ce->changesVolatileQualification())
      return emitLoadOfLValue(ce);
    auto v = Visit(const_cast<Expr *>(subExpr));
    if (v) {
      // CK_NoOp can model a pointer qualification conversion, which can remove
      // an array bound and change the IR type.
      // FIXME: Once pointee types are removed from IR, remove this.
      mlir::Type t = cgf.convertType(destTy);
      if (t != v.getType()) {
        auto srcPtrTy = mlir::dyn_cast<cir::PointerType>(v.getType());
        auto dstPtrTy = mlir::dyn_cast<cir::PointerType>(t);
        if (srcPtrTy && dstPtrTy &&
            srcPtrTy.getAddrSpace() != dstPtrTy.getAddrSpace()) {
          // Address space conversion (e.g., in CUDA/HIP/OpenCL).
          v = cgf.getBuilder().createAddrSpaceCast(v, t);
        } else {
          // Type changed due to qualification conversion (e.g., array bound
          // removal, member pointer qualification). Use bitcast to reconcile
          // the CIR type difference — this is a no-op at the IR value level.
          v = cgf.getBuilder().createBitcast(v, t);
        }
      }
    }
    if (v && subExpr->getType()->isPointerType() && destTy->isPointerType())
      return cgf.authPointerToPointerCast(v, subExpr->getType(), destTy);
    return v;
  }
  case CK_IntegralToPointer: {
    mlir::Type destCIRTy = cgf.convertType(destTy);
    mlir::Value src = Visit(const_cast<Expr *>(subExpr));

    // Properly resize by casting to an int of the same size as the pointer.
    // Clang's IntegralToPointer includes 'bool' as the source, but in CIR
    // 'bool' is not an integral type.  So check the source type to get the
    // correct CIR conversion.
    mlir::Type middleTy = cgf.cgm.getDataLayout().getIntPtrType(destCIRTy);
    mlir::Value middleVal = builder.createCast(
        subExpr->getType()->isBooleanType() ? cir::CastKind::bool_to_int
                                            : cir::CastKind::integral,
        src, middleTy);

    if (cgf.cgm.getCodeGenOpts().StrictVTablePointers) {
      // Going from integer to pointer that could be dynamic requires
      // reloading dynamic information from invariant.group.
      if (destTy.mayBeDynamicClass()) {
        mlir::Value result = builder.createIntToPtr(middleVal, destCIRTy);
        return cir::InvariantGroupOp::create(
            builder, cgf.getLoc(subExpr->getSourceRange()), result);
      }
    }

      return builder.createIntToPtr(middleVal, destCIRTy);
  }

  case CK_BaseToDerived: {
    const CXXRecordDecl *derivedClassDecl = destTy->getPointeeCXXRecordDecl();
    assert(derivedClassDecl && "BaseToDerived arg isn't a C++ object pointer!");
    Address base = cgf.emitPointerWithAlignment(subExpr);
    Address derived = cgf.getAddressOfDerivedClass(
        cgf.getLoc(ce->getSourceRange()), base, derivedClassDecl, ce->path(),
        cgf.shouldNullCheckClassCastValue(ce));

    // C++11 [expr.static.cast]p11: Behavior is undefined if a downcast is
    // performed and the object is not of the derived type.
    if (cgf.sanitizePerformTypeCheck())
      cgf.emitTypeCheck(CIRGenFunction::TCK_DowncastPointer,
                        ce->getExprLoc(), derived.getPointer(),
                        destTy->getPointeeType(), derived.getAlignment());

    if (cgf.sanOpts.has(SanitizerKind::CFIDerivedCast))
      cgf.emitVTablePtrCheckForCast(
          destTy->getPointeeType(), derived, /*mayBeNull=*/true,
          CIRGenFunction::CFICastCheckKind::DerivedCast, ce->getBeginLoc());

    return cgf.getAsNaturalPointerTo(derived, ce->getType()->getPointeeType());
  }
  case CK_UncheckedDerivedToBase:
  case CK_DerivedToBase: {
    // The EmitPointerWithAlignment path does this fine; just discard
    // the alignment.
    return cgf.getAsNaturalPointerTo(cgf.emitPointerWithAlignment(ce),
                                     ce->getType()->getPointeeType());
  }
  case CK_Dynamic: {
    Address v = cgf.emitPointerWithAlignment(subExpr);
    const auto *dce = cast<CXXDynamicCastExpr>(ce);
    return cgf.emitDynamicCast(v, dce);
  }
  case CK_ArrayToPointerDecay:
    return cgf.emitArrayToPointerDecay(subExpr).getPointer();

  case CK_NullToPointer: {
    if (mustVisitNullValue(subExpr))
      cgf.emitIgnoredExpr(subExpr);

    // Note that DestTy is used as the MLIR type instead of a custom
    // nullptr type.
    mlir::Type ty = cgf.convertType(destTy);
    return builder.getNullPtr(ty, cgf.getLoc(subExpr->getExprLoc()));
  }

  case CK_NullToMemberPointer: {
    if (mustVisitNullValue(subExpr))
      cgf.emitIgnoredExpr(subExpr);

    const MemberPointerType *mpt = ce->getType()->getAs<MemberPointerType>();
    if (mpt->isMemberFunctionPointerType()) {
      auto ty = mlir::cast<cir::MethodType>(cgf.convertType(destTy));
      return builder.getNullMethodPtr(ty, cgf.getLoc(subExpr->getExprLoc()));
    }

    auto ty = mlir::cast<cir::DataMemberType>(cgf.convertType(destTy));
    return builder.getNullDataMemberPtr(ty, cgf.getLoc(subExpr->getExprLoc()));
  }

  case CK_ReinterpretMemberPointer: {
    mlir::Value src = Visit(subExpr);
    mlir::Value cast = builder.createBitcast(cgf.getLoc(subExpr->getExprLoc()),
                                             src, cgf.convertType(destTy));
    if (auto castOp = cast.getDefiningOp<cir::CastOp>())
      maybeAttachMemberFunctionPointerAuthAttrs(castOp, subExpr->getType(),
                                               destTy, cgf);
    return cast;
  }
  case CK_BaseToDerivedMemberPointer:
  case CK_DerivedToBaseMemberPointer: {
    mlir::Value src = Visit(subExpr);

    assert(!cir::MissingFeatures::memberFuncPtrAuthInfo());

    QualType derivedTy =
        kind == CK_DerivedToBaseMemberPointer ? subExpr->getType() : destTy;
    const auto *mpType = derivedTy->castAs<MemberPointerType>();
    // Use getMostRecentCXXRecordDecl() to obtain the class from the
    // canonical member-pointer type, matching OGCG's
    // CGCXXABI::getMemberPointerAdjustment. Going through the
    // non-canonical qualifier can yield a sugar-level derived class
    // (e.g. DynamicMessage) that doesn't match the cast path, which
    // walks from the canonical class (e.g. Message) to the base.
    const CXXRecordDecl *derivedClass = mpType->getMostRecentCXXRecordDecl();
    // OGCG dispatches through getCXXABI().getMemberPointerAdjustment(E)
    // which allows ABI-specific overrides (only relevant for MSVC ABI,
    // which CIR does not target). CIR computes the offset directly,
    // equivalent to Itanium ABI's getMemberPointerAdjustment.
    CharUnits offset =
        cgf.cgm.computeNonVirtualBaseClassOffset(derivedClass, ce->path());

    mlir::Location loc = cgf.getLoc(subExpr->getExprLoc());
    mlir::Type resultTy = cgf.convertType(destTy);
    mlir::IntegerAttr offsetAttr = builder.getIndexAttr(offset.getQuantity());

    if (subExpr->getType()->isMemberFunctionPointerType()) {
      if (kind == CK_BaseToDerivedMemberPointer) {
        auto methodOp =
            cir::DerivedMethodOp::create(builder, loc, resultTy, src, offsetAttr);
        maybeAttachMemberFunctionPointerAuthAttrs(methodOp, subExpr->getType(),
                                                 destTy, cgf);
        return methodOp;
      }
      auto methodOp =
          cir::BaseMethodOp::create(builder, loc, resultTy, src, offsetAttr);
      maybeAttachMemberFunctionPointerAuthAttrs(methodOp, subExpr->getType(),
                                               destTy, cgf);
      return methodOp;
    }

    if (kind == CK_BaseToDerivedMemberPointer)
      return cir::DerivedDataMemberOp::create(builder, loc, resultTy, src,
                                              offsetAttr);
    return cir::BaseDataMemberOp::create(builder, loc, resultTy, src,
                                         offsetAttr);
  }

  case CK_LValueToRValue:
    assert(cgf.getContext().hasSameUnqualifiedType(subExpr->getType(), destTy));
    assert(subExpr->isGLValue() && "lvalue-to-rvalue applied to r-value!");
    return Visit(const_cast<Expr *>(subExpr));

  case CK_IntegralCast: {
    // OGCG shortcuts ExtVector integral casts via CreateIntCast.
    // CIR routes integral casts through emitScalarConversion/emitScalarCast,
    // which already handles vector integer casts with signedness carried by
    // the source/destination element CIR integer types.
    ScalarConversionOpts opts;
    if (auto *ice = dyn_cast<ImplicitCastExpr>(ce)) {
      if (!ice->isPartOfExplicitCast())
        opts = ScalarConversionOpts(cgf.sanOpts);
    }
    return emitScalarConversion(Visit(subExpr), subExpr->getType(), destTy,
                                ce->getExprLoc(), opts);
  }

  case CK_FloatingComplexToReal:
  case CK_IntegralComplexToReal:
  case CK_FloatingComplexToBoolean:
  case CK_IntegralComplexToBoolean: {
    mlir::Value value = cgf.emitComplexExpr(subExpr);
    return emitComplexToScalarConversion(cgf.getLoc(ce->getExprLoc()), value,
                                         kind, destTy);
  }

  case CK_FloatingRealToComplex:
  case CK_FloatingComplexCast:
  case CK_IntegralRealToComplex:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToFloatingComplex:
  case CK_FloatingComplexToIntegralComplex:
    llvm_unreachable("scalar cast to non-scalar value");

  case CK_PointerToIntegral: {
    assert(!destTy->isBooleanType() && "bool should use PointerToBool");
    mlir::Value ptrExpr = Visit(subExpr);
    if (cgf.cgm.getCodeGenOpts().StrictVTablePointers) {
      const QualType srcType = subExpr->getType();
      // Casting to integer requires stripping dynamic information as it does
      // not carry it.
      if (srcType.mayBeDynamicClass())
        ptrExpr = cir::StripInvariantGroupOp::create(
            builder, cgf.getLoc(subExpr->getSourceRange()), ptrExpr);
    }
    return builder.createPtrToInt(ptrExpr, cgf.convertType(destTy));
  }
  case CK_ToVoid:
    cgf.emitIgnoredExpr(subExpr);
    return {};

  case CK_FixedPointCast:
  case CK_FixedPointToBoolean:
  case CK_FixedPointToIntegral:
  case CK_IntegralToFixedPoint:
    return emitScalarConversion(Visit(subExpr), subExpr->getType(), destTy,
                                ce->getExprLoc());

  case CK_IntegralToFloating:
  case CK_FloatingToIntegral:
  case CK_FloatingCast:
  case CK_FixedPointToFloating:
  case CK_FloatingToFixedPoint: {
    cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(
        ce->getFPFeaturesInEffect(cgf.getLangOpts()));
    FPOptions fpfeatures = ce->getFPFeaturesInEffect(cgf.getLangOpts());
    return emitScalarConversion(Visit(subExpr), subExpr->getType(), destTy,
                                ce->getExprLoc(), ScalarConversionOpts(), fmf,
                                fpfeatures);
  }

  case CK_BooleanToSignedIntegral: {
    ScalarConversionOpts opts;
    opts.treatBooleanAsSigned = true;
    return emitScalarConversion(Visit(subExpr), subExpr->getType(), destTy,
                                ce->getExprLoc(), opts);
  }

  case CK_IntegralToBoolean: {
    mlir::Value src = Visit(subExpr);
    if (subExpr->getType()->isVectorType())
      return emitVectorToBoolConversion(src, cgf.getLoc(ce->getSourceRange()));
    return emitIntToBoolConversion(src, cgf.getLoc(ce->getSourceRange()));
  }

  case CK_PointerToBoolean:
    return emitPointerToBoolConversion(Visit(subExpr), subExpr->getType());
  case CK_FloatingToBoolean: {
    mlir::Value result = emitFloatToBoolConversion(
        Visit(subExpr), cgf.getLoc(subExpr->getExprLoc()));
    FPOptions fpfeatures = ce->getFPFeaturesInEffect(cgf.getLangOpts());
    if (fpfeatures.isFPConstrained() || cgf.IsFPConstrained) {
      if (auto castOp = result.getDefiningOp<cir::CastOp>()) {
        auto *ctx = &cgf.getMLIRContext();
        if (auto fpExcept = cgf.getFPExceptionBehavior(ctx, fpfeatures))
          castOp->setAttr("cir.fp_exception", fpExcept);
        if (auto roundMode = CIRGenFunction::getRoundingMode(ctx, fpfeatures))
          castOp->setAttr("cir.rounding_mode", roundMode);
      }
    }
    return result;
  }
  case CK_MemberPointerToBoolean: {
    mlir::Value memPtr = Visit(subExpr);
    return builder.createCast(cgf.getLoc(ce->getSourceRange()),
                              cir::CastKind::member_ptr_to_bool, memPtr,
                              cgf.convertType(destTy));
  }

  case CK_VectorSplat: {
    // Create a vector object and fill all elements with the same scalar value.
    assert(destTy->isVectorType() && "CK_VectorSplat to non-vector type");
    return cir::VecSplatOp::create(builder,
                                   cgf.getLoc(subExpr->getSourceRange()),
                                   cgf.convertType(destTy), Visit(subExpr));
  }
  case CK_FunctionToPointerDecay:
    return cgf.emitLValue(subExpr).getPointer();

  case CK_LValueBitCast:
  case CK_ObjCObjectLValueCast:
  case CK_LValueToRValueBitCast: {
    LValue sourceLVal = cgf.emitLValue(subExpr);
    Address sourceAddr = sourceLVal.getAddress();

    mlir::Type destElemTy = cgf.convertTypeForMem(destTy);
    mlir::Type destPtrTy = builder.getPointerTo(destElemTy);
    mlir::Value destPtr = builder.createBitcast(
        cgf.getLoc(subExpr->getExprLoc()), sourceAddr.getPointer(), destPtrTy);

    Address destAddr(destPtr, destElemTy, sourceAddr.getAlignment(),
                     sourceAddr.isKnownNonNull());
    LValue destLVal = cgf.makeAddrLValue(destAddr, destTy);
    // LValueToRValueBitCast is a reinterpret, so use MayAlias TBAA
    // (matching OGCG CGExprScalar.cpp:2540).
    if (kind == CK_LValueToRValueBitCast)
      destLVal.setTBAAInfo(TBAAAccessInfo::getMayAliasInfo());
    return emitLoadOfLValue(destLVal, ce->getExprLoc());
  }

  case CK_ZeroToOCLOpaqueType: {
    mlir::Type ty = cgf.convertType(destTy);
    return builder.getNullValue(ty, cgf.getLoc(subExpr->getExprLoc()));
  }

  case CK_ARCProduceObject: {
    // Emit objc_retain on the sub-expression value. This produces a +1
    // reference that the caller takes ownership of.
    return cgf.emitARCRetainScalarExpr(subExpr);
  }
  case CK_ARCConsumeObject: {
    // The sub-expression produces a +1 value that we consume.
    // Push a cleanup to release it at end of full-expression.
    mlir::Value value = Visit(const_cast<Expr *>(subExpr));
    cgf.emitARCConsumeObject(value, cgf.getLoc(ce->getExprLoc()));
    return value;
  }
  case CK_ARCReclaimReturnedObject: {
    return cgf.emitARCReclaimReturnedObject(subExpr);
  }
  case CK_ARCExtendBlockObject:
    return cgf.emitARCExtendBlockObject(subExpr);
  case CK_CopyAndAutoreleaseBlockObject:
    return cgf.emitBlockCopyAndAutorelease(
        Visit(const_cast<Expr *>(subExpr)), subExpr->getType(),
        cgf.getLoc(ce->getExprLoc()));

  default:
    cgf.getCIRGenModule().errorNYI(subExpr->getSourceRange(),
                                   "CastExpr: ", ce->getCastKindName());
    return {};
  }
  return {};
}

mlir::Value ScalarExprEmitter::VisitCallExpr(const CallExpr *e) {
  if (e->getCallReturnType(cgf.getContext())->isReferenceType())
    return emitLoadOfLValue(e);

  const bool ignore = std::exchange(ignoreResultAssign, false);
  auto v = cgf.emitCallExpr(e, ReturnValueSlot(), ignore).getValue();
  emitLValueAlignmentAssumption(e, v,
                                /*keepAsAssumeForCallResult=*/true);
  return v;
}

mlir::Value ScalarExprEmitter::VisitMemberExpr(MemberExpr *e) {
  if (CIRGenFunction::ConstantEmission constant = cgf.tryEmitAsConstant(e)) {
    // Match emitMemberExpr's static-member path: the base expression may carry
    // side effects even when the static member value itself can be folded.
    cgf.emitIgnoredExpr(e->getBase());
    return cgf.emitScalarConstant(constant, e);
  }

  Expr::EvalResult result;
  if (e->EvaluateAsInt(result, cgf.getContext(), Expr::SE_AllowSideEffects)) {
    llvm::APSInt value = result.Val.getInt();
    cgf.emitIgnoredExpr(e->getBase());
    // For boolean types, produce !cir.bool instead of !cir.int<u, 1>.
    // EvaluateAsInt returns a 1-bit unsigned APSInt for bools, but CIR
    // distinguishes bool from integer types.
    if (e->getType()->isBooleanType())
      return builder.getBool(value.getBoolValue(), cgf.getLoc(e->getExprLoc()));
    return builder.getConstInt(cgf.getLoc(e->getExprLoc()), value);
  }
  return emitLoadOfLValue(e);
}

mlir::Value ScalarExprEmitter::VisitInitListExpr(InitListExpr *e) {
  const unsigned numInitElements = e->getNumInits();

  [[maybe_unused]] const bool ignore = std::exchange(ignoreResultAssign, false);
  assert((ignore == false ||
          (numInitElements == 0 && e->getType()->isVoidType())) &&
         "init list ignored");

  if (e->hadArrayRangeDesignator()) {
    cgf.cgm.errorNYI(e->getSourceRange(), "ArrayRangeDesignator");
    return {};
  }

  if (e->getType()->isVectorType()) {
    const auto vectorType =
        mlir::cast<cir::VectorType>(cgf.convertType(e->getType()));
    auto elemTy = vectorType.getElementType();

    SmallVector<mlir::Value, 16> elements;

    for (Expr *init : e->inits()) {
      if (isa<ExtVectorElementExpr>(init)) {
        cgf.cgm.errorNYI(
            init->getExprLoc(),
            "vector initializer with ExtVectorElementExpr (swizzle)");
        return {};
      }

      mlir::Value v = Visit(init);
      mlir::Type vTy = v.getType();

      // Scalar element: one lane
      if (vTy == elemTy) {
        elements.push_back(v);
        continue;
      }

      // Subvector: flatten into scalar lanes
      auto subVecTy = mlir::cast<cir::VectorType>(vTy);
      if (subVecTy.getElementType() != elemTy)
        llvm_unreachable("invalid vector initializer element type");

      // Full vector replacement
      for (unsigned i = 0; i < subVecTy.getSize(); ++i) {
        auto idx = cgf.getBuilder().getUInt32(i, cgf.getLoc(e->getExprLoc()));
        elements.push_back(cir::VecExtractOp::create(
            cgf.getBuilder(), cgf.getLoc(e->getExprLoc()), v, idx));
      }

      continue;
    }

    // Zero-initialize remaining lanes
    if (elements.size() < vectorType.getSize()) {
      const mlir::Value zeroValue = cgf.getBuilder().getNullValue(
          elemTy, cgf.getLoc(e->getSourceRange()));

      while (elements.size() < vectorType.getSize())
        elements.push_back(zeroValue);
    }

    // Truncate excess lanes if any
    elements.resize(vectorType.getSize());

    return cir::VecCreateOp::create(cgf.getBuilder(),
                                    cgf.getLoc(e->getSourceRange()), vectorType,
                                    elements);
  }

  // C++11 value-initialization for the scalar.
  if (numInitElements == 0) {
    if (e->getType()->isVoidType())
      return {};
    return emitNullValue(e->getType(), cgf.getLoc(e->getExprLoc()));
  }

  return Visit(e->getInit(0));
}

mlir::Value CIRGenFunction::emitScalarConversion(mlir::Value src,
                                                 QualType srcTy, QualType dstTy,
                                                 SourceLocation loc) {
  assert(CIRGenFunction::hasScalarEvaluationKind(srcTy) &&
         CIRGenFunction::hasScalarEvaluationKind(dstTy) &&
         "Invalid scalar expression to emit");
  return ScalarExprEmitter(*this, builder)
      .emitScalarConversion(src, srcTy, dstTy, loc);
}

mlir::Value CIRGenFunction::emitComplexToScalarConversion(mlir::Value src,
                                                          QualType srcTy,
                                                          QualType dstTy,
                                                          SourceLocation loc) {
  assert(srcTy->isAnyComplexType() && hasScalarEvaluationKind(dstTy) &&
         "Invalid complex -> scalar conversion");

  QualType complexElemTy = srcTy->castAs<ComplexType>()->getElementType();
  if (dstTy->isBooleanType()) {
    auto kind = complexElemTy->isFloatingType()
                    ? cir::CastKind::float_complex_to_bool
                    : cir::CastKind::int_complex_to_bool;
    return builder.createCast(getLoc(loc), kind, src, convertType(dstTy));
  }

  auto kind = complexElemTy->isFloatingType()
                  ? cir::CastKind::float_complex_to_real
                  : cir::CastKind::int_complex_to_real;
  mlir::Value real =
      builder.createCast(getLoc(loc), kind, src, convertType(complexElemTy));
  return emitScalarConversion(real, complexElemTy, dstTy, loc);
}

mlir::Value ScalarExprEmitter::VisitUnaryLNot(const UnaryOperator *e) {
  // Perform vector logical not on comparison with zero vector.
  if (e->getType()->isVectorType() &&
      e->getType()->castAs<VectorType>()->getVectorKind() ==
          VectorKind::Generic) {
    mlir::Value oper = Visit(e->getSubExpr());
    mlir::Location loc = cgf.getLoc(e->getExprLoc());
    auto operVecTy = mlir::cast<cir::VectorType>(oper.getType());
    auto exprVecTy = mlir::cast<cir::VectorType>(cgf.convertType(e->getType()));
    mlir::Value zeroVec = builder.getNullValue(operVecTy, loc);
    // VecCmpOp with wider-than-i1 result type lowers to ICmpOp/FCmpOp +
    // SExtOp, producing -1 (all ones) for true, matching OGCG's CreateSExt.
    auto cmp = cir::VecCmpOp::create(builder, loc, exprVecTy,
                                     cir::CmpOpKind::eq, oper, zeroVec);
    if (cir::isFPOrVectorOfFPType(oper.getType()))
      cgf.maybeAttachFPConstraints(cmp.getOperation(),
                                   e->getFPFeaturesInEffect(
                                       cgf.getLangOpts()));
    return cmp;
  }

  // Compare operand to zero.
  mlir::Value boolVal = cgf.evaluateExprAsBool(e->getSubExpr());

  // Invert value.
  boolVal = builder.createNot(boolVal);

  // ZExt result to the expr type.
  return maybePromoteBoolResult(boolVal, cgf.convertType(e->getType()));
}

mlir::Value ScalarExprEmitter::VisitOffsetOfExpr(OffsetOfExpr *e) {
  // Try folding the offsetof to a constant.
  Expr::EvalResult evalResult;
  if (e->EvaluateAsInt(evalResult, cgf.getContext())) {
    mlir::Type type = cgf.convertType(e->getType());
    llvm::APSInt value = evalResult.Val.getInt();
    return builder.getConstAPInt(cgf.getLoc(e->getExprLoc()), type, value);
  }

  // Loop over the components of the offsetof to compute the value.
  // Matches OGCG CGExprScalar.cpp VisitOffsetOfExpr.
  unsigned n = e->getNumComponents();
  mlir::Location loc = cgf.getLoc(e->getExprLoc());
  mlir::Type resultType = cgf.convertType(e->getType());
  mlir::Value result = builder.getConstInt(
      loc, mlir::cast<cir::IntType>(resultType), 0);
  QualType currentType = e->getTypeSourceInfo()->getType();

  for (unsigned i = 0; i != n; ++i) {
    OffsetOfNode on = e->getComponent(i);
    mlir::Value offset;
    switch (on.getKind()) {
    case OffsetOfNode::Array: {
      // Compute the index.
      Expr *idxExpr = e->getIndexExpr(on.getArrayExprIndex());
      mlir::Value idx = cgf.emitScalarExpr(idxExpr);
      bool idxSigned =
          idxExpr->getType()->isSignedIntegerOrEnumerationType();
      idx = builder.createIntCast(idx, resultType);
      (void)idxSigned; // Signedness is encoded in the CIR IntType.

      // Save the element type.
      currentType =
          cgf.getContext().getAsArrayType(currentType)->getElementType();

      // Compute the element size.
      int64_t elemSize =
          cgf.getContext().getTypeSizeInChars(currentType).getQuantity();
      mlir::Value elemSizeVal = builder.getConstInt(
          loc, mlir::cast<cir::IntType>(resultType), elemSize);

      // Multiply out to compute the offset.
      offset = cir::BinOp::create(builder, loc, resultType,
                                  cir::BinOpKind::Mul, idx, elemSizeVal,
                                  {}, {}, {}, {}, {}, {});
      break;
    }

    case OffsetOfNode::Field: {
      FieldDecl *memberDecl = on.getField();
      auto *rd = currentType->castAsRecordDecl();
      const ASTRecordLayout &rl = cgf.getContext().getASTRecordLayout(rd);

      // Reuse AST-provided field index instead of re-walking field_begin().
      // This avoids an O(N) scan for every offsetof field component.
      unsigned fieldIdx = memberDecl->getFieldIndex();
      assert(fieldIdx < rl.getFieldCount() && "offsetof field in wrong type");

      // Compute the offset to the field.
      int64_t offsetInt =
          rl.getFieldOffset(fieldIdx) / cgf.getContext().getCharWidth();
      offset = builder.getConstInt(
          loc, mlir::cast<cir::IntType>(resultType), offsetInt);

      // Save the element type.
      currentType = memberDecl->getType();
      break;
    }

    case OffsetOfNode::Identifier:
      llvm_unreachable("dependent __builtin_offsetof");

    case OffsetOfNode::Base: {
      if (on.getBase()->isVirtual()) {
        cgf.getCIRGenModule().errorNYI(e->getSourceRange(),
                                       "virtual base in offsetof");
        return {};
      }

      const ASTRecordLayout &rl = cgf.getContext().getASTRecordLayout(
          currentType->castAsCanonical<RecordType>()->getDecl());

      // Save the element type.
      currentType = on.getBase()->getType();

      // Compute the offset to the base.
      auto *baseRD = currentType->castAsCXXRecordDecl();
      CharUnits offsetInt = rl.getBaseClassOffset(baseRD);
      offset = builder.getConstInt(
          loc, mlir::cast<cir::IntType>(resultType),
          offsetInt.getQuantity());
      break;
    }
    }
    result = cir::BinOp::create(builder, loc, resultType,
                                cir::BinOpKind::Add, result, offset,
                                {}, {}, {}, {}, {}, {});
  }
  return result;
}

mlir::Value ScalarExprEmitter::VisitUnaryReal(const UnaryOperator *e) {
  QualType promotionTy = getPromotionType(e->getSubExpr()->getType());
  mlir::Value result = VisitRealImag(e, promotionTy);
  if (result && !promotionTy.isNull())
    result = emitUnPromotedValue(result, e->getType());
  return result;
}

mlir::Value ScalarExprEmitter::VisitUnaryImag(const UnaryOperator *e) {
  QualType promotionTy = getPromotionType(e->getSubExpr()->getType());
  mlir::Value result = VisitRealImag(e, promotionTy);
  if (result && !promotionTy.isNull())
    result = emitUnPromotedValue(result, e->getType());
  return result;
}

mlir::Value ScalarExprEmitter::VisitRealImag(const UnaryOperator *e,
                                             QualType promotionTy) {
  assert((e->getOpcode() == clang::UO_Real ||
          e->getOpcode() == clang::UO_Imag) &&
         "Invalid UnaryOp kind for ComplexType Real or Imag");

  Expr *op = e->getSubExpr();
  mlir::Location loc = cgf.getLoc(e->getExprLoc());
  if (op->getType()->isAnyComplexType()) {
    // If it's an l-value, load through the appropriate subobject l-value.
    // Note that we have to ask `e` because `op` might be an l-value that
    // this won't work for, e.g. an Obj-C property.
    if (e->isGLValue()) {
      if (!promotionTy.isNull()) {
        mlir::Value complex = cgf.emitComplexExpr(op);
        promotionTy = promotionTy->isAnyComplexType()
                          ? promotionTy
                          : cgf.getContext().getComplexType(promotionTy);
        complex = cgf.emitPromotedValue(complex, promotionTy);
        return e->getOpcode() == clang::UO_Real
                   ? builder.createComplexReal(loc, complex)
                   : builder.createComplexImag(loc, complex);
      }
      // Load only the needed element through the lvalue path.
      return cgf.emitLoadOfLValue(cgf.emitLValue(e), e->getExprLoc())
          .getValue();
    }

    // rvalue: load the full complex value and project.
    mlir::Value complex = cgf.emitComplexExpr(op);
    return e->getOpcode() == clang::UO_Real
               ? builder.createComplexReal(loc, complex)
               : builder.createComplexImag(loc, complex);
  }

  // __real on a non-complex scalar is an identity operation.
  if (e->getOpcode() == UO_Real) {
    mlir::Value operand = promotionTy.isNull()
                              ? Visit(op)
                              : cgf.emitPromotedScalarExpr(op, promotionTy);
    return operand;
  }

  // __imag on a scalar returns zero. Emit the subexpr to ensure side
  // effects are evaluated, but not the actual value.
  if (op->isGLValue()) {
    cgf.emitLValue(op);
  } else if (!promotionTy.isNull()) {
    cgf.emitPromotedScalarExpr(op, promotionTy);
  } else {
    cgf.emitScalarExpr(op, true);
  }
  mlir::Type resTy = !promotionTy.isNull() ? cgf.convertType(promotionTy)
                                            : cgf.convertType(e->getType());
  return builder.getNullValue(resTy, loc);
}

/// Return the size or alignment of the type of argument of the sizeof
/// expression as an integer.
mlir::Value ScalarExprEmitter::VisitUnaryExprOrTypeTraitExpr(
    const UnaryExprOrTypeTraitExpr *e) {
  const QualType typeToSize = e->getTypeOfArgument();
  const mlir::Location loc = cgf.getLoc(e->getSourceRange());
  if (auto kind = e->getKind();
      kind == UETT_SizeOf || kind == UETT_DataSizeOf || kind == UETT_CountOf) {
    if (const VariableArrayType *vat =
            cgf.getContext().getAsVariableArrayType(typeToSize)) {
      // For _Countof, we only want to evaluate if the extent is actually
      // variable as opposed to a multi-dimensional array whose extent is
      // constant but whose element type is variable.
      bool evaluateExtent = true;
      if (kind == UETT_CountOf && vat->getElementType()->isArrayType()) {
        evaluateExtent =
            !vat->getSizeExpr()->isIntegerConstantExpr(cgf.getContext());
      }

      if (evaluateExtent) {
        if (e->isArgumentType()) {
          // sizeof(type) - make sure to emit the VLA size.
          cgf.emitVariablyModifiedType(typeToSize);
        } else {
          // C99 6.5.3.4p2: If the argument is an expression of type
          // VLA, it is evaluated.
          cgf.emitIgnoredExpr(e->getArgumentExpr());
        }

        // For _Countof, we just want to return the size of a single dimension.
        if (kind == UETT_CountOf)
          return cgf.getVLAElements1D(vat).numElts;

        // For sizeof and __datasizeof, we need to scale the number of elements
        // by the size of the array element type.
        CIRGenFunction::VlaSizePair vlaSize = cgf.getVLASize(vat);
        mlir::Value numElts = vlaSize.numElts;

        // Scale the number of non-VLA elements by the non-VLA element size.
        CharUnits eltSize = cgf.getContext().getTypeSizeInChars(vlaSize.type);
        if (!eltSize.isOne()) {
          mlir::Location loc = cgf.getLoc(e->getSourceRange());
          // The element-size constant must match the width of the size type
          // (size_t), which is target dependent (e.g. 32-bit on i386); build it
          // directly at that width rather than from the fixed-width result of
          // CIRGenModule::getSize.
          mlir::Value eltSizeValue = builder.getConstantInt(
              numElts.getLoc(), numElts.getType(), eltSize.getQuantity());
          return builder.createMul(loc, eltSizeValue, numElts,
                                   cir::OverflowBehavior::NoUnsignedWrap);
        }

        return numElts;
      }
    }
  } else if (e->getKind() == UETT_OpenMPRequiredSimdAlign) {
    auto alignment = cgf.getContext()
                         .toCharUnitsFromBits(
                             cgf.getContext().getOpenMPDefaultSimdAlign(
                                 e->getTypeOfArgument()->getPointeeType()))
                         .getQuantity();
    mlir::Type sizeTy =
        cgf.cgm.getTypes().convertType(cgf.getContext().getSizeType());
    return builder.getConstInt(loc, sizeTy, alignment);
  } else if (e->getKind() == UETT_VectorElements) {
    auto vecTy = cast<cir::VectorType>(convertType(e->getTypeOfArgument()));
    if (vecTy.getIsScalable()) {
      cgf.getCIRGenModule().errorNYI(
          e->getSourceRange(),
          "VisitUnaryExprOrTypeTraitExpr: sizeOf scalable vector");
      return builder.getConstant(
          loc, cir::IntAttr::get(cgf.cgm.uInt64Ty,
                                 e->EvaluateKnownConstInt(cgf.getContext())));
    }

    return builder.getConstant(
        loc, cir::IntAttr::get(convertType(e->getType()), vecTy.getSize()));
  }

  return builder.getConstant(
      loc, cir::IntAttr::get(convertType(e->getType()),
                             e->EvaluateKnownConstInt(cgf.getContext())));
}

/// Return true if the specified expression is cheap enough and side-effect-free
/// enough to evaluate unconditionally instead of conditionally.  This is used
/// to convert control flow into selects in some cases.
/// TODO(cir): can be shared with LLVM codegen.
static bool isCheapEnoughToEvaluateUnconditionally(const Expr *e,
                                                   CIRGenFunction &cgf) {
  // Anything that is an integer or floating point constant is fine.
  return e->IgnoreParens()->isEvaluatable(cgf.getContext());
}

mlir::Value ScalarExprEmitter::VisitAbstractConditionalOperator(
    const AbstractConditionalOperator *e) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(e->getSourceRange());
  mlir::Type resultTy = cgf.convertType(e->getType());
  ignoreResultAssign = false;

  // Fast-math flags in effect for this conditional. When the result is
  // floating point these must flow onto the resulting cir.select /
  // cir.ternary so that (under -ffast-math) the fcmp+select can be folded
  // to llvm.maxnum / llvm.minnum, matching classic codegen. Non-FP results
  // (e.g. integer or pointer selects) carry no fast-math flags.
  cir::FastMathFlags fmf = cir::FastMathFlags::none;
  if (cir::isFPOrVectorOfFPType(resultTy))
    fmf = CIRGenFunction::getFastMathFlags(
        e->getFPFeaturesInEffect(cgf.getLangOpts()));

  // Convert a conditional arm to the result type. A plain bitcast cannot cross
  // address spaces, so when an arm and the result are pointers differing in
  // address space (e.g. a global in a non-default address space yielded for a
  // generic-address-space result), insert an address-space cast.
  auto convertArmToResultTy = [&](mlir::Location loc,
                                  mlir::Value v) -> mlir::Value {
    if (v.getType() == resultTy)
      return v;
    auto srcPtrTy = mlir::dyn_cast<cir::PointerType>(v.getType());
    auto dstPtrTy = mlir::dyn_cast<cir::PointerType>(resultTy);
    if (srcPtrTy && dstPtrTy &&
        srcPtrTy.getAddrSpace() != dstPtrTy.getAddrSpace()) {
      if (srcPtrTy.getPointee() != dstPtrTy.getPointee()) {
        auto intermediateTy = cir::PointerType::get(dstPtrTy.getPointee(),
                                                    srcPtrTy.getAddrSpace());
        v = builder.createBitcast(loc, v, intermediateTy);
      }
      return builder.createAddrSpaceCast(loc, v, resultTy);
    }
    return builder.createBitcast(loc, v, resultTy);
  };

  // Bind the common expression if necessary.
  CIRGenFunction::OpaqueValueMapping binding(cgf, e);

  Expr *condExpr = e->getCond();
  Expr *lhsExpr = e->getTrueExpr();
  Expr *rhsExpr = e->getFalseExpr();

  // If the condition constant folds and can be elided, try to avoid emitting
  // the condition and the dead arm.
  bool condExprBool;
  if (cgf.constantFoldsToBool(condExpr, condExprBool)) {
    Expr *live = lhsExpr, *dead = rhsExpr;
    if (!condExprBool)
      std::swap(live, dead);

    // If the dead side doesn't have labels we need, just emit the Live part.
    if (!cgf.containsLabel(dead)) {
      if (condExprBool)
        maybeIncrementProfileCounter(e);
      mlir::Value result = Visit(live);

      // If the live part is a throw expression, it acts like it has a void
      // type, so evaluating it returns a null Value.  However, a conditional
      // with non-void type must return a non-null Value.
      if (!result && !e->getType()->isVoidType()) {
        result = builder.getConstant(
            loc, cir::PoisonAttr::get(builder.getContext(),
                                      cgf.convertType(e->getType())));
      }

      return result;
    }
  }

  QualType condType = condExpr->getType();

  // OpenCL: If the condition is a vector, we can treat this condition like
  // the select function.
  if ((cgf.getLangOpts().OpenCL && condType->isVectorType()) ||
      condType->isExtVectorType()) {
    // Treat the conditional operator as an element-wise select on vectors.
    // OpenCL/ext_vector uses the MSB (sign bit) of the condition, not
    // non-zero. Convert the condition by comparing < 0, which produces 0/-1
    // matching VecTernaryOp's non-zero semantics.
    maybeIncrementProfileCounter(e);
    mlir::Value condValue = Visit(condExpr);
    mlir::Value lhsValue = Visit(lhsExpr);
    mlir::Value rhsValue = Visit(rhsExpr);

    auto condVecTy = mlir::cast<cir::VectorType>(condValue.getType());
    auto condElemTy = condVecTy.getElementType();
    if (!mlir::isa<cir::BoolType>(condElemTy)) {
      mlir::Value zeroVec = builder.getNullValue(condVecTy, loc);
      condValue = cir::VecCmpOp::create(builder, loc, condVecTy,
                                        cir::CmpOpKind::lt, condValue, zeroVec);
    }

    return cir::VecTernaryOp::create(builder, loc, condValue, lhsValue,
                                     rhsValue);
  }

  if (condType->isVectorType() || condType->isSveVLSBuiltinType()) {
    if (!condType->isVectorType()) {
      cgf.cgm.errorNYI(loc, "TernaryOp for SVE vector");
      return {};
    }

    maybeIncrementProfileCounter(e);
    mlir::Value condValue = Visit(condExpr);
    mlir::Value lhsValue = Visit(lhsExpr);
    mlir::Value rhsValue = Visit(rhsExpr);
    return cir::VecTernaryOp::create(builder, loc, condValue, lhsValue,
                                     rhsValue);
  }

  // If this is a really simple expression (like x ? 4 : 5), emit this as a
  // select instead of as control flow.  We can only do this if it is cheap
  // and safe to evaluate the LHS and RHS unconditionally.
  if (!shouldInstrumentProfile() &&
      isCheapEnoughToEvaluateUnconditionally(lhsExpr, cgf) &&
      isCheapEnoughToEvaluateUnconditionally(rhsExpr, cgf)) {
    mlir::Value condV = cgf.evaluateExprAsBool(condExpr);

    mlir::Value lhs = Visit(lhsExpr);
    mlir::Value rhs = Visit(rhsExpr);
    if (!lhs || e->getType()->isVoidType()) {
      // If the conditional has void type, make sure we return a null Value.
      // Note: Visit() may return a non-null value even for void expressions
      // (e.g., *void_ptr returns the pointer), but the value is unused.
      assert((!rhs || e->getType()->isVoidType()) &&
             "lhs and rhs types must match");
      return {};
    }

    lhs = convertArmToResultTy(loc, lhs);
    rhs = convertArmToResultTy(loc, rhs);

    return builder.createSelect(loc, condV, lhs, rhs, fmf);
  }

  mlir::Value condV = cgf.emitOpOnBoolExpr(loc, condExpr);
  CIRGenFunction::ConditionalEvaluation eval(cgf);
  SmallVector<mlir::OpBuilder::InsertPoint, 2> insertPoints{};
  mlir::Type yieldTy{};

  auto patchVoidOrThrowSites = [&]() {
    if (insertPoints.empty())
      return;
    // If both arms are void, so be it.
    if (!yieldTy)
      yieldTy = cgf.voidTy;

    // Insert required yields.
    for (mlir::OpBuilder::InsertPoint &toInsert : insertPoints) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.restoreInsertionPoint(toInsert);

      // If the block already has a terminator (e.g., cir.unreachable from a
      // [[noreturn]] destructor cleanup), don't emit a yield after it.
      // The noreturn path also creates a dead successor block for subsequent
      // codegen — erase it since the ternary branch is fully terminated.
      mlir::Block *block = builder.getBlock();
      if (block && !block->empty() &&
          block->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
        mlir::Region *region = block->getParent();
        llvm::SmallVector<mlir::Block *, 2> deadBlocks;
        for (mlir::Block &b : *region)
          if (&b != block && b.empty() && b.hasNoPredecessors())
            deadBlocks.push_back(&b);
        for (mlir::Block *b : deadBlocks)
          b->erase();
        continue;
      }

      // Block does not return: build empty yield.
      if (mlir::isa<cir::VoidType>(yieldTy)) {
        cir::YieldOp::create(builder, loc);
      } else { // Block returns: set null yield value.
        mlir::Value op0 = builder.getNullValue(yieldTy, loc);
        cir::YieldOp::create(builder, loc, op0);
      }
    }
  };

  cir::TernaryOp ternaryOp = cir::TernaryOp::create(
      builder, loc, condV,
      /*trueBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location loc) {
        maybeIncrementProfileCounter(e);
        eval.beginEvaluation();
        mlir::Value lhs;
        lhs = Visit(lhsExpr);
        eval.endEvaluation();

        if (lhs && !e->getType()->isVoidType()) {
          lhs = convertArmToResultTy(loc, lhs);
          yieldTy = lhs.getType();
          cir::YieldOp::create(b, loc, lhs);
          return;
        }
        // If LHS or RHS is a throw or void expression we need to
        // patch arms as to properly match yield types.
        insertPoints.push_back(b.saveInsertionPoint());
      },
      /*falseBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location loc) {
        eval.beginEvaluation();
        mlir::Value rhs;
        rhs = Visit(rhsExpr);
        eval.endEvaluation();

        if (rhs && !e->getType()->isVoidType()) {
          rhs = convertArmToResultTy(loc, rhs);
          yieldTy = rhs.getType();
          cir::YieldOp::create(b, loc, rhs);
        } else {
          // If LHS or RHS is a throw or void expression we need to
          // patch arms as to properly match yield types.
          insertPoints.push_back(b.saveInsertionPoint());
        }

        patchVoidOrThrowSites();
      });
  if (fmf != cir::FastMathFlags::none)
    ternaryOp.setFastMathFlags(fmf);
  return ternaryOp.getResult();
}

mlir::Value CIRGenFunction::emitScalarPrePostIncDec(const UnaryOperator *e,
                                                    LValue lv,
                                                    cir::UnaryOpKind kind,
                                                    bool isPre) {
  return ScalarExprEmitter(*this, builder)
      .emitScalarPrePostIncDec(e, lv, kind, isPre);
}

// Emit elementwise vector logical operations
mlir::Value ScalarExprEmitter::emitVectorLogicalOp(const BinaryOperator *e,
                                                   cir::BinOpKind opKind) {
  mlir::Location loc = cgf.getLoc(e->getExprLoc());
  mlir::Type resTy = convertType(e->getType());

  mlir::Value lhs = Visit(e->getLHS());
  mlir::Value rhs = Visit(e->getRHS());

  // Build zero vector of the same type
  cir::ConstantOp zeroVec = builder.getNullValue(lhs.getType(), loc);

  auto vecTy = mlir::cast<cir::VectorType>(lhs.getType());
  auto boolElemTy = builder.getBoolTy();
  auto boolVecTy = cir::VectorType::get(boolElemTy, vecTy.getSize());

  // CIR uses VecCmpOp with ne for both FP and integer vectors. The lowering
  // correctly dispatches: FP → FCmpOp(une), integer → ICmpOp(ne).

  // Compare operands to zero to produce vector<bool>
  auto lhsBool = cir::VecCmpOp::create(builder, loc, boolVecTy,
                                       cir::CmpOpKind::ne, lhs, zeroVec);
  auto rhsBool = cir::VecCmpOp::create(builder, loc, boolVecTy,
                                       cir::CmpOpKind::ne, rhs, zeroVec);
  if (cir::isFPOrVectorOfFPType(lhs.getType())) {
    auto fpfeatures = e->getFPFeaturesInEffect(cgf.getLangOpts());
    cgf.maybeAttachFPConstraints(lhsBool.getOperation(), fpfeatures);
    cgf.maybeAttachFPConstraints(rhsBool.getOperation(), fpfeatures);
  }

  // Elementwise logical operation on vector<bool>
  auto logicVal =
      cir::BinOp::create(builder, loc, boolVecTy, opKind, lhsBool, rhsBool,
                         {}, {}, {}, {}, {}, {});

  if (resTy == boolVecTy)
    return logicVal;

  // Convert back to result vector type. Clang Sema::GetSignedVectorType
  // guarantees the result is always a signed integer vector, so
  // createBoolToInt's lowering uses SExtOp: true -> -1 (all ones),
  // matching OGCG's unconditional CreateSExt.
  auto resVecTy = mlir::cast<cir::VectorType>(resTy);
  if (mlir::isa<cir::IntType>(resVecTy.getElementType()))
    return builder.createBoolToInt(logicVal, resVecTy);

  cgf.cgm.errorNYI("vector logical op with non-integer result element type");
  return {};
}
