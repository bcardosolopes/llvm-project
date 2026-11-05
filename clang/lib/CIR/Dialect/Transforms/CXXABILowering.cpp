//==- CXXABILowering.cpp - lower C++ operations to target-specific ABI form -=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PassDetail.h"
#include "TargetLowering/LowerModule.h"

#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CIR/Dialect/Builder/CIRBaseBuilder.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/IR/CIRStage.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Support/TimeProfiler.h"

using namespace mlir;
using namespace cir;

namespace mlir {
#define GEN_PASS_DEF_CXXABILOWERING
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

namespace {

#define GET_ABI_LOWERING_PATTERNS
#include "clang/CIR/Dialect/IR/CIRLowering.inc"
#undef GET_ABI_LOWERING_PATTERNS

struct CXXABILoweringPass
    : public impl::CXXABILoweringBase<CXXABILoweringPass> {
  CXXABILoweringPass() = default;
  void runOnOperation() override;
};

static mlir::ArrayAttr collectMemberFunctionPointerPtrauthEntries(
    mlir::MLIRContext *ctx, mlir::Attribute attr, mlir::Type ty,
    llvm::SmallVectorImpl<mlir::Attribute> &indices) {
  llvm::SmallVector<mlir::Attribute> result;

  std::function<void(mlir::Attribute, mlir::Type)> walk =
      [&](mlir::Attribute curAttr, mlir::Type curTy) {
        if (!curAttr || !curTy)
          return;

        if (auto method = mlir::dyn_cast<cir::MethodAttr>(curAttr)) {
          if (!method.isVirtual() && method.hasPtrauthDiscriminator()) {
            llvm::SmallVector<mlir::Attribute> payload(indices.begin(),
                                                       indices.end());
            payload.push_back(mlir::IntegerAttr::get(
                mlir::IntegerType::get(ctx, 64),
                method.getPtrauthDiscriminator().value()));
            result.push_back(mlir::ArrayAttr::get(ctx, payload));
          }
          return;
        }

        if (auto record = mlir::dyn_cast<cir::ConstRecordAttr>(curAttr)) {
          auto recTy = mlir::dyn_cast<cir::RecordType>(curTy);
          if (!recTy)
            return;
          auto members = recTy.getMembers();
          for (auto [i, elt] : llvm::enumerate(record.getMembers())) {
            indices.push_back(
                mlir::IntegerAttr::get(mlir::IntegerType::get(ctx, 32), i));
            walk(elt, members[i]);
            indices.pop_back();
          }
          return;
        }

        if (auto array = mlir::dyn_cast<cir::ConstArrayAttr>(curAttr)) {
          auto arrTy = mlir::dyn_cast<cir::ArrayType>(curTy);
          if (!arrTy)
            return;
          auto elts = mlir::dyn_cast<mlir::ArrayAttr>(array.getElts());
          if (!elts)
            return;
          for (auto [i, elt] : llvm::enumerate(elts)) {
            indices.push_back(
                mlir::IntegerAttr::get(mlir::IntegerType::get(ctx, 32), i));
            walk(elt, arrTy.getElementType());
            indices.pop_back();
          }
        }
      };

  walk(attr, ty);
  if (result.empty())
    return {};
  return mlir::ArrayAttr::get(ctx, result);
}

/// Returns true if the attribute is, or (for ConstRecordAttr/ConstArrayAttr)
/// contains at any nesting depth, an unlowered member-pointer constant.
///
/// Named records are mutated in place by the type converter, so converting a
/// global's or constant's type can rewrite the record's members while still
/// returning an equal type. The patterns below must still fire in that case
/// (see the initializer-content checks), or the initializer keeps abstract
/// member-pointer constants next to an already-lowered record type.
static bool containsMemberPointerConstant(mlir::Attribute attr) {
  if (!attr)
    return false;
  if (mlir::isa<cir::MethodAttr, cir::DataMemberAttr>(attr))
    return true;
  if (auto record = mlir::dyn_cast<cir::ConstRecordAttr>(attr)) {
    for (mlir::Attribute elt : record.getMembers())
      if (containsMemberPointerConstant(elt))
        return true;
  }
  if (auto array = mlir::dyn_cast<cir::ConstArrayAttr>(attr)) {
    // String/dense element storage cannot hold member-pointer constants.
    auto elts = mlir::dyn_cast<mlir::ArrayAttr>(array.getElts());
    if (!elts)
      return false;
    for (mlir::Attribute elt : elts)
      if (containsMemberPointerConstant(elt))
        return true;
  }
  return false;
}

/// A generic ABI lowering rewrite pattern. This conversion pattern matches any
/// CIR dialect operations with at least one operand or result of an
/// ABI-dependent type. This conversion pattern rewrites the matched operation
/// by replacing all its ABI-dependent operands and results with their
/// lowered counterparts.
class CIRGenericCXXABILoweringPattern : public mlir::ConversionPattern {
public:
  CIRGenericCXXABILoweringPattern(mlir::MLIRContext *context,
                                  const mlir::TypeConverter &typeConverter)
      : mlir::ConversionPattern(typeConverter, MatchAnyOpTypeTag(),
                                /*benefit=*/1, context) {}

  mlir::LogicalResult
  matchAndRewrite(mlir::Operation *op, llvm::ArrayRef<mlir::Value> operands,
                  mlir::ConversionPatternRewriter &rewriter) const override {
    // Do not match on operations that have dedicated ABI lowering rewrite rules
    if (llvm::isa<cir::AllocaOp, cir::BaseDataMemberOp, cir::BaseMethodOp,
                  cir::CastOp, cir::CmpOp, cir::ConstantOp,
                  cir::DerivedDataMemberOp, cir::DerivedMethodOp, cir::FuncOp,
                  cir::GetMethodOp, cir::GetRuntimeMemberOp, cir::GlobalOp>(op))
      return mlir::failure();

    const mlir::TypeConverter *typeConverter = getTypeConverter();
    assert(typeConverter &&
           "CIRGenericCXXABILoweringPattern requires a type converter");
    bool operandsAndResultsLegal = typeConverter->isLegal(op);
    bool regionsLegal =
        std::all_of(op->getRegions().begin(), op->getRegions().end(),
                    [typeConverter](mlir::Region &region) {
                      return typeConverter->isLegal(&region);
                    });
    if (operandsAndResultsLegal && regionsLegal) {
      // The operation does not have any CXXABI-dependent operands or results,
      // the match fails.
      return mlir::failure();
    }

    mlir::OperationState loweredOpState(op->getLoc(), op->getName());
    loweredOpState.addOperands(operands);
    loweredOpState.addAttributes(op->getAttrs());
    loweredOpState.addSuccessors(op->getSuccessors());

    // Lower all result types
    llvm::SmallVector<mlir::Type> loweredResultTypes;
    loweredResultTypes.reserve(op->getNumResults());
    for (mlir::Type result : op->getResultTypes())
      loweredResultTypes.push_back(typeConverter->convertType(result));
    loweredOpState.addTypes(loweredResultTypes);

    // Lower all regions
    for (mlir::Region &region : op->getRegions()) {
      mlir::Region *loweredRegion = loweredOpState.addRegion();
      rewriter.inlineRegionBefore(region, *loweredRegion, loweredRegion->end());
      if (mlir::failed(
              rewriter.convertRegionTypes(loweredRegion, *getTypeConverter())))
        return mlir::failure();
    }

    // Clone the operation with lowered operand types and result types
    mlir::Operation *loweredOp = rewriter.create(loweredOpState);

    rewriter.replaceOp(op, loweredOp);
    return mlir::success();
  }
};

} // namespace

mlir::LogicalResult CIRAllocaOpABILowering::matchAndRewrite(
    cir::AllocaOp op, OpAdaptor adaptor,
    mlir::ConversionPatternRewriter &rewriter) const {
  mlir::Type allocaPtrTy = op.getType();
  mlir::Type allocaTy = op.getAllocaType();
  mlir::Type loweredAllocaPtrTy = getTypeConverter()->convertType(allocaPtrTy);
  mlir::Type loweredAllocaTy = getTypeConverter()->convertType(allocaTy);

  cir::AllocaOp loweredOp = cir::AllocaOp::create(
      rewriter, op.getLoc(), loweredAllocaPtrTy, loweredAllocaTy, op.getName(),
      op.getAlignmentAttr(), /*dynAllocSize=*/adaptor.getDynAllocSize());
  loweredOp.setInit(op.getInit());
  loweredOp.setConstant(op.getConstant());
  loweredOp.setAnnotationsAttr(op.getAnnotationsAttr());

  rewriter.replaceOp(op, loweredOp);
  return mlir::success();
}

mlir::LogicalResult CIRCastOpABILowering::matchAndRewrite(
    cir::CastOp op, OpAdaptor adaptor,
    mlir::ConversionPatternRewriter &rewriter) const {
  mlir::Type srcTy = op.getSrc().getType();

  switch (op.getKind()) {
  case cir::CastKind::bitcast: {
    mlir::Type destTy = getTypeConverter()->convertType(op.getType());
    if (mlir::isa<cir::DataMemberType>(srcTy)) {
      mlir::Value loweredResult =
          lowerModule->getCXXABI().lowerDataMemberBitcast(
              op, destTy, adaptor.getSrc(), rewriter);
      rewriter.replaceOp(op, loweredResult);
    } else if (mlir::isa<cir::MethodType>(srcTy)) {
      mlir::Value loweredResult = lowerModule->getCXXABI().lowerMethodBitcast(
          op, destTy, adaptor.getSrc(), rewriter);
      rewriter.replaceOp(op, loweredResult);
    } else {
      // For bitcasts involving types that contain member pointers indirectly
      // (e.g., pointers to member pointer types within std::function's type
      // erasure), re-create with converted types.
      auto loweredCast = cir::CastOp::create(rewriter, op.getLoc(), destTy,
                                             op.getKind(), adaptor.getSrc());
      rewriter.replaceOp(op, loweredCast);
    }
    return mlir::success();
  }
  case cir::CastKind::member_ptr_to_bool: {
    mlir::Value loweredResult;
    // Check for MethodType or ArrayType (ABI-coerced member function pointer
    // on AArch64 where the member pointer is {i64, i64} → array<u64, 2>).
    if (mlir::isa<cir::MethodType>(srcTy) ||
        mlir::isa<cir::ArrayType>(srcTy))
      loweredResult = lowerModule->getCXXABI().lowerMethodToBoolCast(
          op, adaptor.getSrc(), rewriter);
    else
      loweredResult = lowerModule->getCXXABI().lowerDataMemberToBoolCast(
          op, adaptor.getSrc(), rewriter);
    rewriter.replaceOp(op, loweredResult);
    return mlir::success();
  }
  default: {
    // For other cast kinds (e.g., array_to_ptrdecay on arrays of member
    // pointers), re-create with converted types. The generic pattern excludes
    // CastOp, so we handle the type conversion here.
    mlir::Type loweredResultTy =
        getTypeConverter()->convertType(op.getType());
    auto loweredCast = cir::CastOp::create(rewriter, op.getLoc(),
                                           loweredResultTy, op.getKind(),
                                           adaptor.getSrc());
    rewriter.replaceOp(op, loweredCast);
    return mlir::success();
  }
  }
}

mlir::LogicalResult CIRConstantOpABILowering::matchAndRewrite(
    cir::ConstantOp op, OpAdaptor adaptor,
    mlir::ConversionPatternRewriter &rewriter) const {
  auto collectPtrauthPayload = [&](mlir::Attribute attr,
                                   mlir::Type ty) -> mlir::ArrayAttr {
    llvm::SmallVector<mlir::Attribute> indices;
    return collectMemberFunctionPointerPtrauthEntries(op.getContext(), attr, ty,
                                                      indices);
  };

  if (mlir::isa<cir::DataMemberType>(op.getType())) {
    auto dataMember = mlir::cast<cir::DataMemberAttr>(op.getValue());
    mlir::DataLayout layout(op->getParentOfType<mlir::ModuleOp>());
    mlir::TypedAttr abiValue = lowerModule->getCXXABI().lowerDataMemberConstant(
        dataMember, layout, *getTypeConverter());
    rewriter.replaceOpWithNewOp<ConstantOp>(op, abiValue);
    return mlir::success();
  }

  if (mlir::isa<cir::MethodType>(op.getType())) {
    auto method = mlir::cast<cir::MethodAttr>(op.getValue());
    mlir::DataLayout layout(op->getParentOfType<mlir::ModuleOp>());
    mlir::TypedAttr abiValue = lowerModule->getCXXABI().lowerMethodConstant(
        method, layout, *getTypeConverter());
    auto newOp = rewriter.replaceOpWithNewOp<ConstantOp>(op, abiValue);
    if (auto payload = collectPtrauthPayload(op.getValue(), op.getType()))
      newOp->setAttr("cir.member_func_ptr_ptrauth", payload);
    return mlir::success();
  }

  // For composite types (arrays, records) containing member pointer elements,
  // recursively lower the initializer attribute. This matches the approach used
  // in CIRGlobalOpABILowering for global initializers.
  mlir::Type origTy = op.getType();
  mlir::Type loweredTy = getTypeConverter()->convertType(origTy);
  if (!loweredTy ||
      (loweredTy == origTy && !containsMemberPointerConstant(op.getValue())))
    return mlir::failure();

  mlir::DataLayout layout(op->getParentOfType<mlir::ModuleOp>());

  std::function<mlir::Attribute(mlir::Attribute, mlir::Type, mlir::Type)>
      lowerInitAttr = [&](mlir::Attribute attr, mlir::Type origT,
                          mlir::Type lowT) -> mlir::Attribute {
    if (!attr)
      return attr;

    if (auto dm = mlir::dyn_cast<cir::DataMemberAttr>(attr))
      return lowerModule->getCXXABI().lowerDataMemberConstant(
          dm, layout, *getTypeConverter());

    if (auto method = mlir::dyn_cast<cir::MethodAttr>(attr))
      return lowerModule->getCXXABI().lowerMethodConstant(
          method, layout, *getTypeConverter());

    if (auto array = mlir::dyn_cast<cir::ConstArrayAttr>(attr)) {
      auto origArrTy = mlir::cast<cir::ArrayType>(origT);
      auto lowArrTy = mlir::cast<cir::ArrayType>(lowT);
      // String/dense element storage holds no member-pointer constants and
      // needs no per-element lowering; its element type never changes.
      auto elts = mlir::dyn_cast<mlir::ArrayAttr>(array.getElts());
      if (!elts)
        return attr;
      llvm::SmallVector<mlir::Attribute> loweredElts;
      for (mlir::Attribute elt : elts)
        loweredElts.push_back(lowerInitAttr(elt, origArrTy.getElementType(),
                                            lowArrTy.getElementType()));
      return cir::ConstArrayAttr::get(
          lowArrTy, mlir::ArrayAttr::get(op.getContext(), loweredElts));
    }

    if (auto record = mlir::dyn_cast<cir::ConstRecordAttr>(attr)) {
      auto origRecTy = mlir::cast<cir::RecordType>(origT);
      auto lowRecTy = mlir::cast<cir::RecordType>(lowT);
      llvm::SmallVector<mlir::Attribute> loweredElts;
      for (auto [elt, oTy, lTy] : llvm::zip(record.getMembers(),
                                             origRecTy.getMembers(),
                                             lowRecTy.getMembers()))
        loweredElts.push_back(lowerInitAttr(elt, oTy, lTy));
      return cir::ConstRecordAttr::get(
          lowRecTy, mlir::ArrayAttr::get(op.getContext(), loweredElts));
    }

    // GlobalViewAttr references a global symbol — update its type to the
    // converted type while keeping the symbol reference and indices intact.
    if (auto gv = mlir::dyn_cast<cir::GlobalViewAttr>(attr))
      return cir::GlobalViewAttr::get(lowT, gv.getSymbol(), gv.getIndices(),
                                      gv.getRawByteOffset(), gv.getInRange(),
                                      gv.getInbounds(),
                                      gv.getAddrAdjustment(),
                                      gv.getNoCFI());

    // ConstPtrAttr (e.g., null function pointers) — reconstruct with the
    // lowered pointer type. This handles cases like null function pointers
    // whose signature includes DataMemberType parameters.
    if (auto ptr = mlir::dyn_cast<cir::ConstPtrAttr>(attr))
      return cir::ConstPtrAttr::get(mlir::cast<cir::PointerType>(lowT),
                                    ptr.getValue());

    return attr;
  };

  mlir::Attribute loweredInit = lowerInitAttr(op.getValue(), origTy, loweredTy);
  auto newOp = rewriter.replaceOpWithNewOp<ConstantOp>(
      op, mlir::cast<mlir::TypedAttr>(loweredInit));
  if (auto payload = collectPtrauthPayload(op.getValue(), origTy))
    newOp->setAttr("cir.member_func_ptr_ptrauth", payload);
  return mlir::success();
}

mlir::LogicalResult CIRCmpOpABILowering::matchAndRewrite(
    cir::CmpOp op, OpAdaptor adaptor,
    mlir::ConversionPatternRewriter &rewriter) const {
  mlir::Type type = op.getLhs().getType();

  mlir::Value loweredResult;
  if (mlir::isa<cir::DataMemberType>(type))
    loweredResult = lowerModule->getCXXABI().lowerDataMemberCmp(
        op, adaptor.getLhs(), adaptor.getRhs(), rewriter);
  else if (mlir::isa<cir::MethodType>(type))
    loweredResult = lowerModule->getCXXABI().lowerMethodCmp(
        op, adaptor.getLhs(), adaptor.getRhs(), rewriter);
  else {
    // For other types (e.g., pointers to member types), re-create the CmpOp
    // with converted operands. The generic pattern excludes CmpOp, so we
    // handle the type conversion here.
    auto newCmp = cir::CmpOp::create(rewriter, op.getLoc(), op.getKind(),
                                     adaptor.getLhs(), adaptor.getRhs());
    rewriter.replaceOp(op, newCmp);
    return mlir::success();
  }

  rewriter.replaceOp(op, loweredResult);
  return mlir::success();
}

mlir::LogicalResult CIRFuncOpABILowering::matchAndRewrite(
    cir::FuncOp op, OpAdaptor adaptor,
    mlir::ConversionPatternRewriter &rewriter) const {
  cir::FuncType opFuncType = op.getFunctionType();
  mlir::TypeConverter::SignatureConversion signatureConversion(
      opFuncType.getNumInputs());

  for (const auto &[i, argType] : llvm::enumerate(opFuncType.getInputs())) {
    mlir::Type loweredArgType = getTypeConverter()->convertType(argType);
    if (!loweredArgType)
      return mlir::failure();
    signatureConversion.addInputs(i, loweredArgType);
  }

  mlir::Type loweredResultType =
      getTypeConverter()->convertType(opFuncType.getReturnType());
  if (!loweredResultType)
    return mlir::failure();

  auto loweredFuncType =
      cir::FuncType::get(signatureConversion.getConvertedTypes(),
                         loweredResultType, /*isVarArg=*/opFuncType.isVarArg());

  // Create a new cir.func operation for the CXXABI-lowered function.
  cir::FuncOp loweredFuncOp = rewriter.cloneWithoutRegions(op);
  loweredFuncOp.setFunctionType(loweredFuncType);
  rewriter.inlineRegionBefore(op.getBody(), loweredFuncOp.getBody(),
                              loweredFuncOp.end());
  if (mlir::failed(rewriter.convertRegionTypes(
          &loweredFuncOp.getBody(), *getTypeConverter(), &signatureConversion)))
    return mlir::failure();

  rewriter.eraseOp(op);
  return mlir::success();
}

mlir::LogicalResult CIRGlobalOpABILowering::matchAndRewrite(
    cir::GlobalOp op, OpAdaptor adaptor,
    mlir::ConversionPatternRewriter &rewriter) const {
  mlir::Type ty = op.getSymType();
  mlir::Type loweredTy = getTypeConverter()->convertType(ty);
  if (!loweredTy)
    return mlir::failure();

  // If the type didn't change and the initializer holds no member-pointer
  // constants, there's nothing for us to lower. The content check matters
  // because named records are mutated in place by the type converter: their
  // members can already be lowered while the type still compares equal.
  if (ty == loweredTy &&
      !containsMemberPointerConstant(op.getInitialValueAttr()))
    return mlir::failure();

  mlir::DataLayout layout(op->getParentOfType<mlir::ModuleOp>());

  // Recursively lower the initializer attribute. This handles
  // DataMemberAttr/MethodAttr at any nesting depth inside
  // ConstRecordAttr/ConstArrayAttr.
  std::function<mlir::Attribute(mlir::Attribute, mlir::Type, mlir::Type)>
      lowerInitAttr = [&](mlir::Attribute attr, mlir::Type origTy,
                          mlir::Type lowTy) -> mlir::Attribute {
    if (!attr)
      return attr;

    if (auto dm = mlir::dyn_cast<cir::DataMemberAttr>(attr))
      return lowerModule->getCXXABI().lowerDataMemberConstant(
          dm, layout, *getTypeConverter());

    if (auto method = mlir::dyn_cast<cir::MethodAttr>(attr))
      return lowerModule->getCXXABI().lowerMethodConstant(
          method, layout, *getTypeConverter());

    if (auto record = mlir::dyn_cast<cir::ConstRecordAttr>(attr)) {
      auto origRecTy = mlir::cast<cir::RecordType>(origTy);
      auto lowRecTy = mlir::cast<cir::RecordType>(lowTy);
      auto origMembers = origRecTy.getMembers();
      auto lowMembers = lowRecTy.getMembers();
      llvm::SmallVector<mlir::Attribute> loweredElts;
      for (auto [elt, oTy, lTy] :
           llvm::zip(record.getMembers(), origMembers, lowMembers))
        loweredElts.push_back(lowerInitAttr(elt, oTy, lTy));
      return cir::ConstRecordAttr::get(
          lowRecTy, mlir::ArrayAttr::get(op.getContext(), loweredElts));
    }

    if (auto array = mlir::dyn_cast<cir::ConstArrayAttr>(attr)) {
      auto origArrTy = mlir::cast<cir::ArrayType>(origTy);
      auto lowArrTy = mlir::cast<cir::ArrayType>(lowTy);
      // String/dense element storage holds no member-pointer constants and
      // needs no per-element lowering; its element type never changes.
      auto elts = mlir::dyn_cast<mlir::ArrayAttr>(array.getElts());
      if (!elts)
        return attr;
      llvm::SmallVector<mlir::Attribute> loweredElts;
      for (mlir::Attribute elt : elts)
        loweredElts.push_back(
            lowerInitAttr(elt, origArrTy.getElementType(),
                          lowArrTy.getElementType()));
      return cir::ConstArrayAttr::get(
          lowArrTy, mlir::ArrayAttr::get(op.getContext(), loweredElts));
    }

    // Attribute doesn't need lowering (e.g., IntAttr, FPAttr).
    // ConstPtrAttr (e.g., null function pointers) — reconstruct with the
    // lowered pointer type. This handles cases like null function pointers
    // whose signature includes DataMemberType parameters.
    if (auto ptr = mlir::dyn_cast<cir::ConstPtrAttr>(attr))
      return cir::ConstPtrAttr::get(mlir::cast<cir::PointerType>(lowTy),
                                    ptr.getValue());

    return attr;
  };

  mlir::Attribute loweredInit =
      lowerInitAttr(op.getInitialValueAttr(), ty, loweredTy);
  llvm::SmallVector<mlir::Attribute> indices;
  mlir::ArrayAttr ptrauthPayload = collectMemberFunctionPointerPtrauthEntries(
      op.getContext(), op.getInitialValueAttr(), ty, indices);

  auto newOp = mlir::cast<cir::GlobalOp>(rewriter.clone(*op.getOperation()));
  newOp.setInitialValueAttr(loweredInit);
  newOp.setSymType(loweredTy);
  if (ptrauthPayload)
    newOp->setAttr("cir.member_func_ptr_ptrauth", ptrauthPayload);
  rewriter.replaceOp(op, newOp);
  return mlir::success();
}

mlir::LogicalResult CIRBaseDataMemberOpABILowering::matchAndRewrite(
    cir::BaseDataMemberOp op, OpAdaptor adaptor,
    mlir::ConversionPatternRewriter &rewriter) const {
  mlir::Value loweredResult = lowerModule->getCXXABI().lowerBaseDataMember(
      op, adaptor.getSrc(), rewriter);
  rewriter.replaceOp(op, loweredResult);
  return mlir::success();
}

mlir::LogicalResult CIRDerivedDataMemberOpABILowering::matchAndRewrite(
    cir::DerivedDataMemberOp op, OpAdaptor adaptor,
    mlir::ConversionPatternRewriter &rewriter) const {
  mlir::Value loweredResult = lowerModule->getCXXABI().lowerDerivedDataMember(
      op, adaptor.getSrc(), rewriter);
  rewriter.replaceOp(op, loweredResult);
  return mlir::success();
}

mlir::LogicalResult CIRBaseMethodOpABILowering::matchAndRewrite(
    cir::BaseMethodOp op, OpAdaptor adaptor,
    mlir::ConversionPatternRewriter &rewriter) const {
  mlir::Value loweredResult =
      lowerModule->getCXXABI().lowerBaseMethod(op, adaptor.getSrc(), rewriter);
  rewriter.replaceOp(op, loweredResult);
  return mlir::success();
}

mlir::LogicalResult CIRDerivedMethodOpABILowering::matchAndRewrite(
    cir::DerivedMethodOp op, OpAdaptor adaptor,
    mlir::ConversionPatternRewriter &rewriter) const {
  mlir::Value loweredResult = lowerModule->getCXXABI().lowerDerivedMethod(
      op, adaptor.getSrc(), rewriter);
  rewriter.replaceOp(op, loweredResult);
  return mlir::success();
}

mlir::LogicalResult CIRGetMethodOpABILowering::matchAndRewrite(
    cir::GetMethodOp op, OpAdaptor adaptor,
    mlir::ConversionPatternRewriter &rewriter) const {
  mlir::Value loweredResults[3];
  lowerModule->getCXXABI().lowerGetMethod(
      op, loweredResults, adaptor.getMethod(), adaptor.getObject(), rewriter);
  rewriter.replaceOp(op, loweredResults);
  return mlir::success();
}

mlir::LogicalResult CIRGetRuntimeMemberOpABILowering::matchAndRewrite(
    cir::GetRuntimeMemberOp op, OpAdaptor adaptor,
    mlir::ConversionPatternRewriter &rewriter) const {
  mlir::Type resTy = getTypeConverter()->convertType(op.getType());
  mlir::Operation *newOp = lowerModule->getCXXABI().lowerGetRuntimeMember(
      op, resTy, adaptor.getAddr(), adaptor.getMember(), rewriter);
  rewriter.replaceOp(op, newOp);
  return mlir::success();
}

// Prepare the type converter for the CXXABI lowering pass.
// Even though this is a CIR-to-CIR pass, we are eliminating some CIR types.
static void prepareCXXABITypeConverter(mlir::TypeConverter &converter,
                                       mlir::DataLayout &dataLayout,
                                       cir::LowerModule &lowerModule,
                                       bool lowerRecordMemberTypes) {
  converter.addConversion([&](mlir::Type type) -> mlir::Type { return type; });
  // This is necessary in order to convert CIR pointer types that are pointing
  // to CIR types that we are lowering in this pass.
  converter.addConversion([&](cir::PointerType type) -> mlir::Type {
    mlir::Type loweredPointeeType = converter.convertType(type.getPointee());
    if (!loweredPointeeType)
      return {};
    return cir::PointerType::get(type.getContext(), loweredPointeeType,
                                 type.getAddrSpace());
  });
  // Array types may contain DataMemberType or MethodType elements that need
  // to be lowered. Convert the element type recursively.
  converter.addConversion([&](cir::ArrayType type) -> mlir::Type {
    mlir::Type loweredEltType = converter.convertType(type.getElementType());
    if (!loweredEltType)
      return {};
    if (loweredEltType == type.getElementType())
      return type;
    return cir::ArrayType::get(type.getContext(), loweredEltType,
                               type.getSize());
  });
  converter.addConversion([&](cir::DataMemberType type) -> mlir::Type {
    mlir::Type abiType =
        lowerModule.getCXXABI().lowerDataMemberType(type, converter);
    return converter.convertType(abiType);
  });
  converter.addConversion([&](cir::MethodType type) -> mlir::Type {
    mlir::Type abiType =
        lowerModule.getCXXABI().lowerMethodType(type, converter);
    return converter.convertType(abiType);
  });
  if (lowerRecordMemberTypes) {
    // Record types may contain MethodType or DataMemberType members that
    // need to be lowered. Convert the member types and update the record.
    // Use a set to guard against infinite recursion from recursive record
    // types.
    auto recordsBeingConverted =
        std::make_shared<llvm::SmallDenseSet<mlir::Type, 4>>();
    converter.addConversion(
        [&, recordsBeingConverted](cir::RecordType type) -> mlir::Type {
          // Guard against infinite recursion from recursive record types.
          if (!recordsBeingConverted->insert(type).second)
            return type;
          bool needsConversion = false;
          llvm::SmallVector<mlir::Type> convertedMembers;
          for (mlir::Type member : type.getMembers()) {
            mlir::Type converted = converter.convertType(member);
            convertedMembers.push_back(converted);
            if (converted != member)
              needsConversion = true;
          }
          if (needsConversion) {
            if (type.getName()) {
              // Named record: mutate in-place.
              type.complete(convertedMembers, type.getPacked(),
                            type.getPadded());
            } else {
              // Anonymous record: can't mutate, create a new one.
              recordsBeingConverted->erase(type);
              return cir::RecordType::get(type.getContext(), convertedMembers,
                                          type.getPacked(), type.getPadded(),
                                          type.getKind());
            }
          }
          recordsBeingConverted->erase(type);
          return type;
        });
  }
  // This is necessary in order to convert CIR function types that have argument
  // or return types that use CIR types that we are lowering in this pass.
  converter.addConversion([&](cir::FuncType type) -> mlir::Type {
    llvm::SmallVector<mlir::Type> loweredInputTypes;
    loweredInputTypes.reserve(type.getNumInputs());
    if (mlir::failed(
            converter.convertTypes(type.getInputs(), loweredInputTypes)))
      return {};

    mlir::Type loweredReturnType = converter.convertType(type.getReturnType());
    if (!loweredReturnType)
      return {};

    return cir::FuncType::get(loweredInputTypes, loweredReturnType,
                              /*isVarArg=*/type.getVarArg());
  });

  // Register materializations so that the dialect conversion framework can
  // handle type mismatches during partial conversion. Without these, the
  // framework cannot bridge the gap when a converted value (e.g.,
  // cir.ptr<record{i64, i64}>) needs to be used where the original type
  // (e.g., cir.ptr<method<...>>) is still expected by an unconverted op.
  converter.addSourceMaterialization(
      [](mlir::OpBuilder &builder, mlir::Type resultType,
         mlir::ValueRange inputs, mlir::Location loc) -> mlir::Value {
        return mlir::UnrealizedConversionCastOp::create(builder, loc,
                                                        resultType, inputs)
            .getResult(0);
      });
  converter.addTargetMaterialization(
      [](mlir::OpBuilder &builder, mlir::Type resultType,
         mlir::ValueRange inputs, mlir::Location loc) -> mlir::Value {
        return mlir::UnrealizedConversionCastOp::create(builder, loc,
                                                        resultType, inputs)
            .getResult(0);
      });
}

static void
populateCXXABIConversionTarget(mlir::ConversionTarget &target,
                               const mlir::TypeConverter &typeConverter) {
  target.addLegalOp<mlir::ModuleOp>();

  // The ABI lowering pass is interested in CIR operations with operands or
  // results of CXXABI-dependent types, or CIR operations with regions whose
  // block arguments are of CXXABI-dependent types.
  target.addDynamicallyLegalDialect<cir::CIRDialect>(
      [&typeConverter](mlir::Operation *op) {
        if (!typeConverter.isLegal(op))
          return false;
        return std::all_of(op->getRegions().begin(), op->getRegions().end(),
                           [&typeConverter](mlir::Region &region) {
                             return typeConverter.isLegal(&region);
                           });
      });

  // Some CIR ops needs special checking for legality
  target.addDynamicallyLegalOp<cir::FuncOp>([&typeConverter](cir::FuncOp op) {
    return typeConverter.isLegal(op.getFunctionType());
  });
  // Globals and constants can hold member-pointer constants in their
  // initializers. Legality must account for initializer content, not just
  // types: lowerRecordTypeMembers rewrites named record members in place
  // before conversion, so by the time legality is checked the types may no
  // longer mention any member-pointer type while the initializer still does.
  target.addDynamicallyLegalOp<cir::GlobalOp>(
      [&typeConverter](cir::GlobalOp op) {
        return typeConverter.isLegal(op.getSymType()) &&
               !containsMemberPointerConstant(op.getInitialValueAttr());
      });
  target.addDynamicallyLegalOp<cir::ConstantOp>(
      [&typeConverter](cir::ConstantOp op) {
        return typeConverter.isLegal(op.getOperation()) &&
               !containsMemberPointerConstant(op.getValue());
      });
}

//===----------------------------------------------------------------------===//
// The Pass
//===----------------------------------------------------------------------===//

namespace {
/// What CXXABILowering has to touch in a module:
///  - the named records whose own members hold a member pointer. RecordType is
///    identified and mutable, so the type converter cannot give such a record
///    new members; they are rewritten in place before the conversion, and the
///    record's users keep their types.
///  - the top-level ops (functions, globals, ...) with something to convert:
///    a type holding a member pointer outside a named record, or a
///    member-pointer constant. The conversion visits these and nothing else.
struct CXXABIWork {
  llvm::SetVector<cir::RecordType> records;
  llvm::SetVector<mlir::Operation *> roots;
  /// Some record, named or not, holds a member pointer, so the type converter
  /// has to convert record members.
  bool recordsNeedLowering = false;
};

/// Finds the CXXABIWork in one walk over the module's ops and, through a
/// memoized visit, the types they use -- nested records included, however
/// they are reached.
class CXXABIWorkCollector {
public:
  CXXABIWork collect(mlir::ModuleOp module) {
    for (mlir::Operation &top : module.getBody()->getOperations())
      top.walk([&](mlir::Operation *op) {
        if (opNeedsLowering(op))
          work.roots.insert(&top);
      });
    return std::move(work);
  }

private:
  /// Mirrors populateCXXABIConversionTarget: the op's result, operand and
  /// block-argument types, a function's type, a global's type and initializer,
  /// a constant's value. Every type is visited, not only up to the first hit,
  /// so the records they reach are all collected.
  bool opNeedsLowering(mlir::Operation *op) {
    bool needs = false;
    auto visit = [&](mlir::Type ty) { needs |= typeNeedsLowering(ty); };
    for (mlir::Type ty : op->getResultTypes())
      visit(ty);
    for (mlir::Value operand : op->getOperands())
      visit(operand.getType());
    for (mlir::Region &region : op->getRegions())
      for (mlir::Block &block : region)
        for (mlir::BlockArgument arg : block.getArguments())
          visit(arg.getType());
    if (auto fn = mlir::dyn_cast<cir::FuncOp>(op))
      visit(fn.getFunctionType());
    if (auto global = mlir::dyn_cast<cir::GlobalOp>(op)) {
      visit(global.getSymType());
      needs |= containsMemberPointerConstant(global.getInitialValueAttr());
    }
    if (auto constant = mlir::dyn_cast<cir::ConstantOp>(op))
      needs |= containsMemberPointerConstant(constant.getValue());
    return needs;
  }

  /// cir::containsMemberPointer, memoized, recording on the way the named
  /// records whose own members hold a member pointer. A named record is false
  /// here like there: it is rewritten in place and keeps its identity. That
  /// also makes the placeholder entered before a record's members are visited
  /// the right answer for a record reached again through its own members.
  bool typeNeedsLowering(mlir::Type ty) {
    auto [it, inserted] = seen.try_emplace(ty, false);
    if (!inserted)
      return it->second;
    bool needs = false;
    if (mlir::isa<cir::MethodType, cir::DataMemberType>(ty)) {
      needs = true;
    } else if (auto ptrTy = mlir::dyn_cast<cir::PointerType>(ty)) {
      needs = typeNeedsLowering(ptrTy.getPointee());
    } else if (auto arrTy = mlir::dyn_cast<cir::ArrayType>(ty)) {
      needs = typeNeedsLowering(arrTy.getElementType());
    } else if (auto vecTy = mlir::dyn_cast<cir::VectorType>(ty)) {
      needs = typeNeedsLowering(vecTy.getElementType());
    } else if (auto fnTy = mlir::dyn_cast<cir::FuncType>(ty)) {
      for (mlir::Type input : fnTy.getInputs())
        needs |= typeNeedsLowering(input);
      needs |= typeNeedsLowering(fnTy.getReturnType());
    } else if (auto recTy = mlir::dyn_cast<cir::RecordType>(ty);
               recTy && !recTy.isIncomplete()) {
      bool membersNeed = false;
      for (mlir::Type member : recTy.getMembers())
        membersNeed |= typeNeedsLowering(member);
      work.recordsNeedLowering |= membersNeed;
      if (!recTy.getName())
        needs = membersNeed;
      else if (membersNeed)
        work.records.insert(recTy);
    }
    seen[ty] = needs;
    return needs;
  }

  CXXABIWork work;
  llvm::DenseMap<mlir::Type, bool> seen;
};
} // namespace

/// Replace the members of each collected record with their ABI-lowered
/// equivalents: every member the type converter would change, not only direct
/// member pointers, since operations reach members through the converted
/// types.
static void lowerRecordTypeMembers(llvm::ArrayRef<cir::RecordType> records,
                                   mlir::TypeConverter &typeConverter) {
  for (cir::RecordType recTy : records) {
    llvm::SmallVector<mlir::Type> newMembers;
    newMembers.reserve(recTy.getMembers().size());
    for (mlir::Type memberTy : recTy.getMembers())
      newMembers.push_back(typeConverter.convertType(memberTy));
    recTy.replaceMemberTypes(newMembers);
  }
}

void CXXABILoweringPass::runOnOperation() {
  // Whichever way this returns, the module has been through it.
  auto recordStage = llvm::make_scope_exit(
      [&] { cir::advanceStage(getOperation(), cir::CIRStage::Prepare); });
  auto module = mlir::cast<mlir::ModuleOp>(getOperation());
  mlir::MLIRContext *ctx = module.getContext();

  // If the triple is not present, e.g. CIR modules parsed from text, we
  // cannot init LowerModule properly.
  assert(!cir::MissingFeatures::makeTripleAlwaysPresent());
  // If no target triple is available, skip the ABI lowering pass.
  if (!module->hasAttr(cir::CIRDialect::getTripleAttrName()))
    return;

  // C has no member pointers, so there is nothing to look for.
  if (auto langAttr = mlir::dyn_cast_if_present<cir::SourceLanguageAttr>(
          module->getAttr(cir::CIRDialect::getSourceLanguageAttrName()))) {
    if (langAttr.getValue() == cir::SourceLanguage::C)
      return;
  }

  CXXABIWork work = CXXABIWorkCollector().collect(module);
  if (work.records.empty() && work.roots.empty())
    return;

  mlir::PatternRewriter rewriter(ctx);
  std::unique_ptr<cir::LowerModule> lowerModule =
      cir::createLowerModule(module, rewriter,
                             cir::LowerModuleRequirement::CXXABIOnly);

  // If createLowerModule returned nullptr, the module is still missing target
  // information we need to reconstruct the ABI context. Skip gracefully rather
  // than dereferencing a null pointer.
  if (!lowerModule)
    return;

  mlir::DataLayout dataLayout(module);
  mlir::TypeConverter typeConverter;
  prepareCXXABITypeConverter(typeConverter, dataLayout, *lowerModule,
                             work.recordsNeedLowering);

  // Lower MethodType and DataMemberType members inside record types before
  // running the dialect conversion. This is needed because RecordType is
  // identified by name and its members cannot be changed by the type
  // converter.
  lowerRecordTypeMembers(work.records.getArrayRef(), typeConverter);

  mlir::RewritePatternSet patterns(ctx);
  patterns.add<CIRGenericCXXABILoweringPattern>(patterns.getContext(),
                                                typeConverter);
  patterns.add<
#define GET_ABI_LOWERING_PATTERNS_LIST
#include "clang/CIR/Dialect/IR/CIRLowering.inc"
#undef GET_ABI_LOWERING_PATTERNS_LIST
      >(patterns.getContext(), typeConverter, dataLayout, *lowerModule);

  mlir::ConversionTarget target(*ctx);
  populateCXXABIConversionTarget(target, typeConverter);

  if (failed(mlir::applyPartialConversion(work.roots.getArrayRef(), target,
                                          std::move(patterns))))
    signalPassFailure();

  // Clean up any unrealized_conversion_cast ops left by the dialect conversion
  // framework for ABI type lowering. These can arise when the conversion
  // framework processes operations out of order (e.g., a user is in a dead
  // block). The materialization bridges a converted type (e.g.,
  // ptr<record{i64, i64}>) back to the original ABI type (e.g.,
  // ptr<method<...>>), but both types represent the same ABI data.
  //
  // Only clean up casts where both input and output are CIR types (from our
  // ABI materializations). Leave casts that bridge CIR types to non-CIR types
  // (e.g., !cir.bool → i1 for OpenACC) untouched.
  //
  // The walk covers the module, not the roots: the conversion replaces some of
  // them (functions, globals) with new ops.
  llvm::SmallVector<mlir::UnrealizedConversionCastOp> castsToErase;
  module.walk([&](mlir::UnrealizedConversionCastOp castOp) {
    if (castOp.getNumResults() != 1 || castOp.getInputs().size() != 1)
      return;
    mlir::Type inputType = castOp.getInputs()[0].getType();
    mlir::Type resultType = castOp.getResult(0).getType();
    // Only clean up casts where both sides are CIR dialect types.
    if (!mlir::isa<cir::CIRDialect>(inputType.getDialect()) ||
        !mlir::isa<cir::CIRDialect>(resultType.getDialect()))
      return;
    castOp.getResult(0).replaceAllUsesWith(castOp.getInputs()[0]);
    castsToErase.push_back(castOp);
  });
  for (auto castOp : castsToErase)
    castOp.erase();
}

std::unique_ptr<Pass> mlir::createCXXABILoweringPass() {
  return std::make_unique<CXXABILoweringPass>();
}
