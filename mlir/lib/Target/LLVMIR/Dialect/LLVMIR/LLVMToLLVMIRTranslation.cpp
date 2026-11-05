//===- LLVMToLLVMIRTranslation.cpp - Translate LLVM dialect to LLVM IR ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a translation between the MLIR LLVM dialect and LLVM IR.
//
//===----------------------------------------------------------------------===//

#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Operation.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Target/LLVMIR/ModuleTranslation.h"

#include "llvm/ADT/TypeSwitch.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/MatrixBuilder.h"
#include "llvm/IR/MemoryModelRelaxationAnnotations.h"
#include "llvm/Support/LogicalResult.h"

using namespace mlir;
using namespace mlir::LLVM;
using mlir::LLVM::detail::getLLVMConstant;

#include "mlir/Dialect/LLVMIR/LLVMConversionEnumsToLLVM.inc"

static llvm::FastMathFlags getFastmathFlags(FastmathFlagsInterface &op) {
  using llvmFMF = llvm::FastMathFlags;
  using FuncT = void (llvmFMF::*)(bool);
  const std::pair<FastmathFlags, FuncT> handlers[] = {
      // clang-format off
      {FastmathFlags::nnan,     &llvmFMF::setNoNaNs},
      {FastmathFlags::ninf,     &llvmFMF::setNoInfs},
      {FastmathFlags::nsz,      &llvmFMF::setNoSignedZeros},
      {FastmathFlags::arcp,     &llvmFMF::setAllowReciprocal},
      {FastmathFlags::contract, &llvmFMF::setAllowContract},
      {FastmathFlags::afn,      &llvmFMF::setApproxFunc},
      {FastmathFlags::reassoc,  &llvmFMF::setAllowReassoc},
      // clang-format on
  };
  llvm::FastMathFlags ret;
  ::mlir::LLVM::FastmathFlags fmfMlir = op.getFastmathAttr().getValue();
  for (auto it : handlers)
    if (bitEnumContainsAll(fmfMlir, it.first))
      (ret.*(it.second))(true);
  return ret;
}

/// Convert the value of a DenseI64ArrayAttr to a vector of unsigned indices.
static SmallVector<unsigned> extractPosition(ArrayRef<int64_t> indices) {
  SmallVector<unsigned> position;
  llvm::append_range(position, indices);
  return position;
}

/// Convert an LLVM type to a string for printing in diagnostics.
static std::string diagStr(const llvm::Type *type) {
  std::string str;
  llvm::raw_string_ostream os(str);
  type->print(os);
  return str;
}

/// Get the declaration of an overloaded llvm intrinsic. First we get the
/// overloaded argument types and/or result type from the CallIntrinsicOp, and
/// then use those to get the correct declaration of the overloaded intrinsic.
static FailureOr<llvm::Function *>
getOverloadedDeclaration(CallIntrinsicOp op, llvm::Intrinsic::ID id,
                         llvm::Module *module,
                         LLVM::ModuleTranslation &moduleTranslation) {
  SmallVector<llvm::Type *, 8> allArgTys;
  for (Type type : op->getOperandTypes())
    allArgTys.push_back(moduleTranslation.convertType(type));

  llvm::Type *resTy;
  if (op.getNumResults() == 0)
    resTy = llvm::Type::getVoidTy(module->getContext());
  else
    resTy = moduleTranslation.convertType(op.getResult(0).getType());

  // ATM we do not support variadic intrinsics.
  llvm::FunctionType *ft = llvm::FunctionType::get(resTy, allArgTys, false);

  SmallVector<llvm::Intrinsic::IITDescriptor, 8> table;
  getIntrinsicInfoTableEntries(id, table);
  ArrayRef<llvm::Intrinsic::IITDescriptor> tableRef = table;

  SmallVector<llvm::Type *, 8> overloadedArgTys;
  if (llvm::Intrinsic::matchIntrinsicSignature(ft, tableRef,
                                               overloadedArgTys) !=
      llvm::Intrinsic::MatchIntrinsicTypesResult::MatchIntrinsicTypes_Match) {
    return mlir::emitError(op.getLoc(), "call intrinsic signature ")
           << diagStr(ft) << " to overloaded intrinsic " << op.getIntrinAttr()
           << " does not match any of the overloads";
  }

  ArrayRef<llvm::Type *> overloadedArgTysRef = overloadedArgTys;
  return llvm::Intrinsic::getOrInsertDeclaration(module, id,
                                                 overloadedArgTysRef);
}

static llvm::OperandBundleDef
convertOperandBundle(OperandRange bundleOperands, StringRef bundleTag,
                     LLVM::ModuleTranslation &moduleTranslation) {
  std::vector<llvm::Value *> operands;
  operands.reserve(bundleOperands.size());
  for (Value bundleArg : bundleOperands)
    operands.push_back(moduleTranslation.lookupValue(bundleArg));
  return llvm::OperandBundleDef(bundleTag.str(), std::move(operands));
}

static SmallVector<llvm::OperandBundleDef>
convertOperandBundles(OperandRangeRange bundleOperands, ArrayAttr bundleTags,
                      LLVM::ModuleTranslation &moduleTranslation) {
  SmallVector<llvm::OperandBundleDef> bundles;
  bundles.reserve(bundleOperands.size());

  for (auto [operands, tagAttr] : llvm::zip_equal(bundleOperands, bundleTags)) {
    StringRef tag = cast<StringAttr>(tagAttr).getValue();
    bundles.push_back(convertOperandBundle(operands, tag, moduleTranslation));
  }
  return bundles;
}

static SmallVector<llvm::OperandBundleDef>
convertOperandBundles(OperandRangeRange bundleOperands,
                      std::optional<ArrayAttr> bundleTags,
                      LLVM::ModuleTranslation &moduleTranslation) {
  if (!bundleTags)
    return {};
  return convertOperandBundles(bundleOperands, *bundleTags, moduleTranslation);
}

/// Builder for LLVM_CallIntrinsicOp
static LogicalResult
convertCallLLVMIntrinsicOp(CallIntrinsicOp op, llvm::IRBuilderBase &builder,
                           LLVM::ModuleTranslation &moduleTranslation) {
  llvm::Module *module = builder.GetInsertBlock()->getModule();
  llvm::Intrinsic::ID id =
      llvm::Intrinsic::lookupIntrinsicID(op.getIntrinAttr());
  if (!id)
    return mlir::emitError(op.getLoc(), "could not find LLVM intrinsic: ")
           << op.getIntrinAttr();

  llvm::Function *fn = nullptr;
  if (llvm::Intrinsic::isOverloaded(id)) {
    auto fnOrFailure =
        getOverloadedDeclaration(op, id, module, moduleTranslation);
    if (failed(fnOrFailure))
      return failure();
    fn = *fnOrFailure;
  } else {
    fn = llvm::Intrinsic::getOrInsertDeclaration(module, id, {});
  }

  // Check the result type of the call.
  const llvm::Type *intrinType =
      op.getNumResults() == 0
          ? llvm::Type::getVoidTy(module->getContext())
          : moduleTranslation.convertType(op.getResultTypes().front());
  if (intrinType != fn->getReturnType()) {
    return mlir::emitError(op.getLoc(), "intrinsic call returns ")
           << diagStr(intrinType) << " but " << op.getIntrinAttr()
           << " actually returns " << diagStr(fn->getReturnType());
  }

  // Check the argument types of the call. If the function is variadic, check
  // the subrange of required arguments.
  if (!fn->getFunctionType()->isVarArg() &&
      op.getArgs().size() != fn->arg_size()) {
    return mlir::emitError(op.getLoc(), "intrinsic call has ")
           << op.getArgs().size() << " operands but " << op.getIntrinAttr()
           << " expects " << fn->arg_size();
  }
  if (fn->getFunctionType()->isVarArg() &&
      op.getArgs().size() < fn->arg_size()) {
    return mlir::emitError(op.getLoc(), "intrinsic call has ")
           << op.getArgs().size() << " operands but variadic "
           << op.getIntrinAttr() << " expects at least " << fn->arg_size();
  }
  // Check the arguments up to the number the function requires.
  for (unsigned i = 0, e = fn->arg_size(); i != e; ++i) {
    const llvm::Type *expected = fn->getArg(i)->getType();
    const llvm::Type *actual =
        moduleTranslation.convertType(op.getOperandTypes()[i]);
    if (actual != expected) {
      return mlir::emitError(op.getLoc(), "intrinsic call operand #")
             << i << " has type " << diagStr(actual) << " but "
             << op.getIntrinAttr() << " expects " << diagStr(expected);
    }
  }

  FastmathFlagsInterface itf = op;
  builder.setFastMathFlags(getFastmathFlags(itf));

  auto *inst = builder.CreateCall(
      fn, moduleTranslation.lookupValues(op.getArgs()),
      convertOperandBundles(op.getOpBundleOperands(), op.getOpBundleTags(),
                            moduleTranslation));

  // Same three as the call and invoke paths. Without them an unmodelled
  // intrinsic's alias metadata is imported onto the op and then silently
  // discarded on the way back out.
  moduleTranslation.setAccessGroupsMetadata(op, inst);
  moduleTranslation.setAliasScopeMetadata(op, inst);
  moduleTranslation.setTBAAMetadata(op, inst);

  if (failed(moduleTranslation.convertArgAndResultAttrs(op, inst)))
    return failure();

  if (op.getNumResults() == 1)
    moduleTranslation.mapValue(op->getResults().front()) = inst;
  return success();
}

static void convertLinkerOptionsOp(ArrayAttr options,
                                   llvm::IRBuilderBase &builder,
                                   LLVM::ModuleTranslation &moduleTranslation) {
  llvm::Module *llvmModule = moduleTranslation.getLLVMModule();
  llvm::LLVMContext &context = llvmModule->getContext();
  llvm::NamedMDNode *linkerMDNode =
      llvmModule->getOrInsertNamedMetadata("llvm.linker.options");
  SmallVector<llvm::Metadata *> mdNodes;
  mdNodes.reserve(options.size());
  for (auto s : options.getAsRange<StringAttr>()) {
    auto *mdNode = llvm::MDString::get(context, s.getValue());
    mdNodes.push_back(mdNode);
  }

  auto *listMDNode = llvm::MDTuple::get(context, mdNodes);
  linkerMDNode->addOperand(listMDNode);
}

static llvm::Metadata *
convertModuleFlagValue(StringRef key, ArrayAttr arrayAttr,
                       llvm::IRBuilderBase &builder,
                       LLVM::ModuleTranslation &moduleTranslation) {
  llvm::LLVMContext &context = builder.getContext();
  llvm::MDBuilder mdb(context);
  SmallVector<llvm::Metadata *> nodes;

  if (key == LLVMDialect::getModuleFlagKeyCGProfileName()) {
    for (auto entry : arrayAttr.getAsRange<ModuleFlagCGProfileEntryAttr>()) {
      auto getFuncMetadata = [&](FlatSymbolRefAttr sym) -> llvm::Metadata * {
        if (!sym)
          return nullptr;
        if (llvm::Function *fn =
                moduleTranslation.lookupFunction(sym.getValue()))
          return llvm::ValueAsMetadata::get(fn);
        return nullptr;
      };
      llvm::Metadata *fromMetadata = getFuncMetadata(entry.getFrom());
      llvm::Metadata *toMetadata = getFuncMetadata(entry.getTo());

      llvm::Metadata *vals[] = {
          fromMetadata, toMetadata,
          mdb.createConstant(llvm::ConstantInt::get(
              llvm::Type::getInt64Ty(context), entry.getCount()))};
      nodes.push_back(llvm::MDNode::get(context, vals));
    }
    return llvm::MDTuple::getDistinct(context, nodes);
  }
  return nullptr;
}

static llvm::Metadata *convertModuleFlagProfileSummaryAttr(
    StringRef key, ModuleFlagProfileSummaryAttr summaryAttr,
    llvm::IRBuilderBase &builder, LLVM::ModuleTranslation &moduleTranslation) {
  llvm::LLVMContext &context = builder.getContext();
  llvm::MDBuilder mdb(context);

  auto getIntTuple = [&](StringRef key, uint64_t val) -> llvm::MDTuple * {
    SmallVector<llvm::Metadata *> tupleNodes{
        mdb.createString(key), mdb.createConstant(llvm::ConstantInt::get(
                                   llvm::Type::getInt64Ty(context), val))};
    return llvm::MDTuple::get(context, tupleNodes);
  };

  SmallVector<llvm::Metadata *> fmtNode{
      mdb.createString("ProfileFormat"),
      mdb.createString(
          stringifyProfileSummaryFormatKind(summaryAttr.getFormat()))};

  SmallVector<llvm::Metadata *> vals = {
      llvm::MDTuple::get(context, fmtNode),
      getIntTuple("TotalCount", summaryAttr.getTotalCount()),
      getIntTuple("MaxCount", summaryAttr.getMaxCount()),
      getIntTuple("MaxInternalCount", summaryAttr.getMaxInternalCount()),
      getIntTuple("MaxFunctionCount", summaryAttr.getMaxFunctionCount()),
      getIntTuple("NumCounts", summaryAttr.getNumCounts()),
      getIntTuple("NumFunctions", summaryAttr.getNumFunctions()),
  };

  if (summaryAttr.getIsPartialProfile())
    vals.push_back(
        getIntTuple("IsPartialProfile", *summaryAttr.getIsPartialProfile()));

  if (summaryAttr.getPartialProfileRatio()) {
    SmallVector<llvm::Metadata *> tupleNodes{
        mdb.createString("PartialProfileRatio"),
        mdb.createConstant(llvm::ConstantFP::get(
            llvm::Type::getDoubleTy(context),
            summaryAttr.getPartialProfileRatio().getValue()))};
    vals.push_back(llvm::MDTuple::get(context, tupleNodes));
  }

  SmallVector<llvm::Metadata *> detailedEntries;
  llvm::Type *llvmInt64Type = llvm::Type::getInt64Ty(context);
  for (ModuleFlagProfileSummaryDetailedAttr detailedEntry :
       summaryAttr.getDetailedSummary()) {
    SmallVector<llvm::Metadata *> tupleNodes{
        mdb.createConstant(
            llvm::ConstantInt::get(llvmInt64Type, detailedEntry.getCutOff())),
        mdb.createConstant(
            llvm::ConstantInt::get(llvmInt64Type, detailedEntry.getMinCount())),
        mdb.createConstant(llvm::ConstantInt::get(
            llvmInt64Type, detailedEntry.getNumCounts()))};
    detailedEntries.push_back(llvm::MDTuple::get(context, tupleNodes));
  }
  SmallVector<llvm::Metadata *> detailedSummary{
      mdb.createString("DetailedSummary"),
      llvm::MDTuple::get(context, detailedEntries)};
  vals.push_back(llvm::MDTuple::get(context, detailedSummary));

  return llvm::MDNode::get(context, vals);
}

static void convertModuleFlagsOp(ArrayAttr flags, llvm::IRBuilderBase &builder,
                                 LLVM::ModuleTranslation &moduleTranslation) {
  llvm::Module *llvmModule = moduleTranslation.getLLVMModule();
  for (auto flagAttr : flags.getAsRange<ModuleFlagAttr>()) {
    llvm::Metadata *valueMetadata =
        llvm::TypeSwitch<Attribute, llvm::Metadata *>(flagAttr.getValue())
            .Case<StringAttr>([&](auto strAttr) {
              return llvm::MDString::get(builder.getContext(),
                                         strAttr.getValue());
            })
            .Case<IntegerAttr>([&](auto intAttr) {
              return llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                  llvm::Type::getInt32Ty(builder.getContext()),
                  intAttr.getInt()));
            })
            .Case<ArrayAttr>([&](auto arrayAttr) {
              return convertModuleFlagValue(flagAttr.getKey().getValue(),
                                            arrayAttr, builder,
                                            moduleTranslation);
            })
            .Case([&](ModuleFlagProfileSummaryAttr summaryAttr) {
              return convertModuleFlagProfileSummaryAttr(
                  flagAttr.getKey().getValue(), summaryAttr, builder,
                  moduleTranslation);
            })
            .Default([](auto) { return nullptr; });

    assert(valueMetadata && "expected valid metadata");
    llvmModule->addModuleFlag(
        convertModFlagBehaviorToLLVM(flagAttr.getBehavior()),
        flagAttr.getKey().getValue(), valueMetadata);
  }
}

/// The scope `loc` is placed in. At a call site that is the caller's: the
/// callee frame of a CallSiteLoc carries the inlined callee's scope, which the
/// location translation applies to that frame itself, and taking it for the
/// whole location would put the call site in the callee.
static LLVM::DILocalScopeAttr findPlacementScope(Location loc) {
  if (auto fused = dyn_cast<FusedLoc>(loc)) {
    if (auto scope =
            dyn_cast_if_present<LLVM::DILocalScopeAttr>(fused.getMetadata()))
      return scope;
    for (Location inner : fused.getLocations())
      if (LLVM::DILocalScopeAttr scope = findPlacementScope(inner))
        return scope;
    return {};
  }
  if (auto callSite = dyn_cast<CallSiteLoc>(loc))
    return findPlacementScope(callSite.getCaller());
  if (auto name = dyn_cast<NameLoc>(loc))
    return findPlacementScope(name.getChildLoc());
  if (auto opaque = dyn_cast<OpaqueLoc>(loc))
    return findPlacementScope(opaque.getFallbackLocation());
  return {};
}

static llvm::DILocalScope *
getLocalScopeFromLoc(llvm::IRBuilderBase &builder, Location loc,
                     LLVM::ModuleTranslation &moduleTranslation) {
  if (LLVM::DILocalScopeAttr scope = findPlacementScope(loc))
    if (auto *localScope = llvm::dyn_cast<llvm::DILocalScope>(
            moduleTranslation.translateDebugInfo(scope)))
      return localScope;
  return builder.GetInsertBlock()->getParent()->getSubprogram();
}

/// Writes `!irr_loop` back onto the terminator. The first operand is the fixed
/// tag LLVM matches on in `BasicBlock::getIrrLoopHeaderWeight`.
template <typename OpTy>
static void setIrrLoopMetadata(OpTy op, llvm::Instruction *inst) {
  std::optional<uint64_t> weight = op.getIrrLoopHeaderWeight();
  if (!weight)
    return;
  llvm::LLVMContext &ctx = inst->getContext();
  llvm::Metadata *operands[] = {
      llvm::MDString::get(ctx, "loop_header_weight"),
      llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx), *weight))};
  inst->setMetadata(llvm::LLVMContext::MD_irr_loop,
                    llvm::MDNode::get(ctx, operands));
}

/// Writes back the call-site function attributes kept in `passthrough`.
///
/// The shape of an entry says what kind it is, which the names cannot: a bare
/// name is an enum attribute, a name paired with an integer is an integer
/// attribute, and a name paired with a string is a string attribute. That is
/// what lets a string attribute spelled `"noreturn"` survive alongside the
/// enum of the same name.
template <typename OpTy>
static LogicalResult setCallSitePassthroughAttrs(OpTy op,
                                                 llvm::CallBase *call) {
  std::optional<ArrayAttr> passthrough = op.getPassthrough();
  if (!passthrough)
    return success();

  llvm::LLVMContext &ctx = call->getContext();
  ArrayRef<StringLiteral> modelled =
      LLVM::LLVMDialect::getModelledCallSiteAttrNames();

  /// An enum or integer attribute must not also have a slot on the op, or the
  /// same fact would be written from two places. The import never produces
  /// that; a hand-written module can.
  auto checkNotModelled = [&](StringRef name) -> LogicalResult {
    if (llvm::is_contained(modelled, name))
      return op.emitOpError("'")
             << name
             << "' has its own attribute on this operation and cannot "
                "also appear in 'passthrough'";
    return success();
  };

  for (Attribute attr : *passthrough) {
    // A bare name: an enum attribute.
    if (auto name = dyn_cast<StringAttr>(attr)) {
      llvm::Attribute::AttrKind kind =
          llvm::Attribute::getAttrKindFromName(name.getValue());
      if (kind == llvm::Attribute::None ||
          !llvm::Attribute::isEnumAttrKind(kind))
        return op.emitOpError("'")
               << name.getValue() << "' is not an enum function attribute";
      if (failed(checkNotModelled(name.getValue())))
        return failure();
      if (!llvm::Attribute::canUseAsFnAttr(kind))
        return op.emitOpError("'")
               << name.getValue() << "' is not a function attribute";
      call->addFnAttr(kind);
      continue;
    }

    auto pair = dyn_cast<ArrayAttr>(attr);
    auto name =
        pair && pair.size() == 2 ? dyn_cast<StringAttr>(pair[0]) : nullptr;
    if (!pair)
      return op.emitOpError(
          "expected 'passthrough' to contain string or array attributes");
    if (!name)
      return op.emitOpError("expected the first element of a 'passthrough' "
                            "pair to be a string");

    // Paired with an integer: an integer attribute.
    if (auto value = dyn_cast_if_present<IntegerAttr>(pair[1])) {
      llvm::Attribute::AttrKind kind =
          llvm::Attribute::getAttrKindFromName(name.getValue());
      if (kind == llvm::Attribute::None ||
          !llvm::Attribute::isIntAttrKind(kind))
        return op.emitOpError("'")
               << name.getValue() << "' is not an integer function attribute";
      if (failed(checkNotModelled(name.getValue())))
        return failure();
      // LLVM's integer attributes are 64-bit, and `getZExtValue` asserts
      // rather than truncating. The import only ever writes 64 bits, so this
      // is for a hand-written module -- rejected with a diagnostic like the
      // rest, not an abort.
      if (value.getValue().getActiveBits() > 64)
        return op.emitOpError("'")
               << name.getValue()
               << "' expects a value that fits in 64 bits in 'passthrough'";
      call->addFnAttr(
          llvm::Attribute::get(ctx, kind, value.getValue().getZExtValue()));
      continue;
    }

    // Paired with a string: a string attribute, whose name means nothing to
    // LLVM even when it happens to read like one it knows.
    auto value = dyn_cast_if_present<StringAttr>(pair[1]);
    if (!value)
      return op.emitOpError("expected the second element of a 'passthrough' "
                            "pair to be a string or an integer");
    // A string attribute whose name has no LLVM enum -- `frame-pointer`,
    // `target-features`, the `aarch64_*` family -- is the very thing a slot
    // models, so it must not also sit in the bag. Where an enum of the name
    // does exist the string is a different attribute (`"noreturn"` versus the
    // enum), so it is left alone. This mirrors the import filter in
    // `ModuleImport`'s `setCallSitePassthroughAttrs`, so the two agree on what
    // a slot claims; `checkNotModelled` handles the enum and integer cases,
    // where the name alone settles it.
    if (llvm::is_contained(modelled, name.getValue()) &&
        llvm::Attribute::getAttrKindFromName(name.getValue()) ==
            llvm::Attribute::None)
      return op.emitOpError("'")
             << name.getValue()
             << "' has its own attribute on this operation and cannot also "
                "appear in 'passthrough'";
    call->addFnAttr(
        llvm::Attribute::get(ctx, name.getValue(), value.getValue()));
  }
  return success();
}

/// Writes an intrinsic op's call-site attributes back onto the intrinsic call
/// it produced; see `LLVMDialect::getIntrinsicCallFnAttrsName`.
static LogicalResult
setIntrinsicCallAttrs(Operation *op, ArrayRef<llvm::Instruction *> instructions,
                      NamedAttribute attribute,
                      LLVM::ModuleTranslation &moduleTranslation) {
  llvm::CallBase *call = nullptr;
  for (llvm::Instruction *inst : instructions)
    if (auto *candidate = dyn_cast<llvm::CallBase>(inst);
        candidate && candidate->getCalledFunction() &&
        candidate->getCalledFunction()->isIntrinsic())
      call = candidate;
  if (!call)
    return success();
  llvm::LLVMContext &ctx = call->getContext();
  StringRef name = attribute.getName().getValue();
  if (name == LLVMDialect::getIntrinsicCallFnAttrsName()) {
    auto attrs = dyn_cast<ArrayAttr>(attribute.getValue());
    if (!attrs)
      return op->emitOpError("expected an array for ") << name;
    for (Attribute attr : attrs) {
      if (auto kindName = dyn_cast<StringAttr>(attr)) {
        llvm::Attribute::AttrKind kind =
            llvm::Attribute::getAttrKindFromName(kindName.getValue());
        if (!llvm::Attribute::isEnumAttrKind(kind))
          return op->emitOpError("'")
                 << kindName.getValue() << "' is not an enum attribute";
        call->addFnAttr(kind);
        continue;
      }
      auto pair = dyn_cast<ArrayAttr>(attr);
      auto key = pair && pair.size() == 2 ? dyn_cast<StringAttr>(pair[0])
                                          : nullptr;
      if (!key)
        return op->emitOpError("expected a name or a pair in ") << name;
      if (auto value = dyn_cast<IntegerAttr>(pair[1])) {
        llvm::Attribute::AttrKind kind =
            llvm::Attribute::getAttrKindFromName(key.getValue());
        if (!llvm::Attribute::isIntAttrKind(kind) ||
            value.getValue().getActiveBits() > 64)
          return op->emitOpError("'")
                 << key.getValue() << "' is not an integer attribute";
        call->addFnAttr(
            llvm::Attribute::get(ctx, kind, value.getValue().getZExtValue()));
        continue;
      }
      auto value = dyn_cast<StringAttr>(pair[1]);
      if (!value)
        return op->emitOpError("expected an integer or a string in ") << name;
      call->addFnAttr(llvm::Attribute::get(ctx, key.getValue(),
                                           value.getValue()));
    }
    return success();
  }
  auto toBuilder = [&](Attribute attr) -> FailureOr<llvm::AttrBuilder> {
    auto dict = dyn_cast<DictionaryAttr>(attr);
    if (!dict)
      return op->emitOpError("expected a dictionary in ") << name;
    return moduleTranslation.convertParameterAttrs(op->getLoc(), dict);
  };
  if (name == LLVMDialect::getIntrinsicCallResAttrsName()) {
    FailureOr<llvm::AttrBuilder> attrs = toBuilder(attribute.getValue());
    if (failed(attrs))
      return failure();
    call->addRetAttrs(*attrs);
    return success();
  }
  auto args = dyn_cast<ArrayAttr>(attribute.getValue());
  if (!args || args.size() != call->arg_size())
    return op->emitOpError("expected one dictionary per call argument in ")
           << name;
  for (auto [i, arg] : llvm::enumerate(args)) {
    FailureOr<llvm::AttrBuilder> attrs = toBuilder(arg);
    if (failed(attrs))
      return failure();
    call->addParamAttrs(i, *attrs);
  }
  return success();
}

/// Writes back the call-site function attributes the dialect models with a
/// typed slot. Shared by the call and invoke paths.
template <typename OpTy>
static LogicalResult setModelledCallSiteAttrs(OpTy op, llvm::CallBase *call) {
  llvm::LLVMContext &ctx = call->getContext();
  if (op.getOptSize())
    call->addFnAttr(llvm::Attribute::OptimizeForSize);
  if (op.getMinSize())
    call->addFnAttr(llvm::Attribute::MinSize);
  if (op.getNoReturn())
    call->addFnAttr(llvm::Attribute::NoReturn);
  if (op.getCold())
    call->addFnAttr(llvm::Attribute::Cold);
  if (op.getNoMerge())
    call->addFnAttr(llvm::Attribute::NoMerge);
  if (op.getBuiltin())
    call->addFnAttr(llvm::Attribute::Builtin);
  if (op.getConvergent())
    call->addFnAttr(llvm::Attribute::Convergent);
  if (op.getNoUnwind())
    call->addFnAttr(llvm::Attribute::NoUnwind);
  if (op.getWillReturn())
    call->addFnAttr(llvm::Attribute::WillReturn);
  if (op.getNoInline())
    call->addFnAttr(llvm::Attribute::NoInline);
  if (op.getAlwaysInline())
    call->addFnAttr(llvm::Attribute::AlwaysInline);
  if (op.getInlineHint())
    call->addFnAttr(llvm::Attribute::InlineHint);

  // Back out with the same spelling `llvm.func` uses, since these are the
  // attributes it already models and the call now models the same way.
  if (op.getOptimizeNone())
    call->addFnAttr(llvm::Attribute::OptimizeNone);
  if (VScaleRangeAttr vscale = op.getVscaleRangeAttr())
    call->addFnAttr(llvm::Attribute::getWithVScaleRangeArgs(
        ctx, vscale.getMinRange().getInt(), vscale.getMaxRange().getInt()));
  if (UWTableKindAttr uwtable = op.getUwtableKindAttr())
    call->addFnAttr(llvm::Attribute::getWithUWTableKind(
        ctx, convertUWTableKindToLLVM(uwtable.getUwtableKind())));
  if (FramePointerKindAttr fp = op.getFramePointerAttr())
    call->addFnAttr(llvm::Attribute::get(
        ctx, "frame-pointer",
        stringifyFramePointerKind(fp.getFramePointerKind())));
  if (TargetFeaturesAttr features = op.getTargetFeaturesAttr();
      features && !features.nullOrEmpty())
    call->addFnAttr(llvm::Attribute::get(ctx, "target-features",
                                         features.getFeaturesString()));

  auto addString = [&](StringRef name, std::optional<StringRef> value) {
    if (value)
      call->addFnAttr(llvm::Attribute::get(ctx, name, *value));
  };
  addString("target-cpu", op.getTargetCpu());
  addString("tune-cpu", op.getTuneCpu());
  addString("denormal-fp-math", op.getDenormalFpMath());
  addString("denormal-fp-math-f32", op.getDenormalFpMathF32());
  addString("fp-contract", op.getFpContract());
  addString("instrument-function-entry", op.getInstrumentFunctionEntry());
  addString("instrument-function-exit", op.getInstrumentFunctionExit());

  // Booleans spelled as strings, as on a function.
  auto addBool = [&](StringRef name, std::optional<bool> value) {
    if (value)
      call->addFnAttr(
          llvm::Attribute::get(ctx, name, *value ? "true" : "false"));
  };
  addBool("unsafe-fp-math", op.getUnsafeFpMath());
  addBool("no-infs-fp-math", op.getNoInfsFpMath());
  addBool("no-nans-fp-math", op.getNoNansFpMath());
  addBool("approx-func-fp-math", op.getApproxFuncFpMath());
  addBool("no-signed-zeros-fp-math", op.getNoSignedZerosFpMath());

  // The SME family, back to LLVM's `aarch64_*` names.
  auto addFlag = [&](StringRef name, bool set) {
    if (set)
      call->addFnAttr(llvm::Attribute::get(ctx, name));
  };
  addFlag("aarch64_pstate_sm_enabled", op.getArmStreaming());
  addFlag("aarch64_pstate_sm_body", op.getArmLocallyStreaming());
  addFlag("aarch64_pstate_sm_compatible", op.getArmStreamingCompatible());
  addFlag("aarch64_new_za", op.getArmNewZa());
  addFlag("aarch64_in_za", op.getArmInZa());
  addFlag("aarch64_out_za", op.getArmOutZa());
  addFlag("aarch64_inout_za", op.getArmInoutZa());
  addFlag("aarch64_preserves_za", op.getArmPreservesZa());

  if (MemoryEffectsAttr memAttr = op.getMemoryEffectsAttr()) {
    call->setMemoryEffects(
        llvm::MemoryEffects(llvm::MemoryEffects::Location::ArgMem,
                            convertModRefInfoToLLVM(memAttr.getArgMem())) |
        llvm::MemoryEffects(
            llvm::MemoryEffects::Location::InaccessibleMem,
            convertModRefInfoToLLVM(memAttr.getInaccessibleMem())) |
        llvm::MemoryEffects(llvm::MemoryEffects::Location::Other,
                            convertModRefInfoToLLVM(memAttr.getOther())) |
        llvm::MemoryEffects(llvm::MemoryEffects::Location::ErrnoMem,
                            convertModRefInfoToLLVM(memAttr.getErrnoMem())) |
        llvm::MemoryEffects(llvm::MemoryEffects::Location::TargetMem0,
                            convertModRefInfoToLLVM(memAttr.getTargetMem0())) |
        llvm::MemoryEffects(llvm::MemoryEffects::Location::TargetMem1,
                            convertModRefInfoToLLVM(memAttr.getTargetMem1())));
  }

  std::optional<ArrayRef<int32_t>> allocSize = op.getAllocSize();
  if (!allocSize)
    return success();
  // One or two argument indices; anything else is not an `allocsize` LLVM can
  // express, so it is reported rather than silently truncated.
  if (allocSize->size() != 1 && allocSize->size() != 2)
    return op.emitOpError("expected one or two indices in alloc_size");
  // The values matter as well as the count, and nothing else checks them --
  // `DenseI32ArrayAttr` holds any `i32` and the op has no verifier for this.
  // Both of LLVM's guards here are assertions, so a bad value crashes an
  // assert-enabled build and silently corrupts the attribute in a release one.
  if ((*allocSize)[0] < 0 || (allocSize->size() == 2 && (*allocSize)[1] < 0))
    return op.emitOpError("expected non-negative indices in alloc_size");
  std::optional<unsigned> numElems;
  if (allocSize->size() == 2)
    numElems = static_cast<unsigned>((*allocSize)[1]);
  // `allocsize(0, 0)` is rejected by `getWithAllocSizeArgs` itself: an
  // allocation whose size is argument 0 times argument 0 is not a thing LLVM
  // will represent.
  if (allocSize->size() == 2 && (*allocSize)[0] == 0 && (*allocSize)[1] == 0)
    return op.emitOpError("expected alloc_size other than (0, 0)");
  // The indices name parameters, and LLVM's verifier rejects one that names a
  // parameter the callee does not have. Caught here so the diagnostic points
  // at the operation rather than arriving as a module-level verifier failure.
  //
  // Against the callee's parameter count, which is what LLVM compares with --
  // not the operand count. For an indirect call the operands lead with the
  // callee pointer, and for a variadic one the trailing arguments are not
  // parameters; either would make the bound too generous.
  LLVMFunctionType calleeType = op.getCalleeFunctionType();
  unsigned numParams = calleeType.getNumParams();
  for (int32_t index : *allocSize) {
    if (static_cast<unsigned>(index) >= numParams)
      return op.emitOpError("alloc_size index ")
             << index << " is out of range for " << numParams
             << " callee parameters";
    // And it has to name an integer one -- a size is a number. LLVM checks
    // this alongside the bound, so leaving it out would just move the failure
    // to the module verifier.
    if (!calleeType.getParams()[index].isIntOrIndex())
      return op.emitOpError("alloc_size index ")
             << index << " does not name an integer parameter";
  }
  call->addFnAttr(llvm::Attribute::getWithAllocSizeArgs(
      ctx, static_cast<unsigned>((*allocSize)[0]), numElems));
  return success();
}

/// Builds a MemProf call stack node: one constant integer per source-location
/// hash. Shared by `!callsite` and by the first operand of every `!memprof`
/// MemInfoBlock, the same way the importer shares one reader for both.
static llvm::MDNode *convertCallStack(llvm::LLVMContext &ctx,
                                      ArrayRef<int64_t> ids) {
  llvm::Type *i64 = llvm::Type::getInt64Ty(ctx);
  SmallVector<llvm::Metadata *> operands;
  operands.reserve(ids.size());
  for (int64_t id : ids)
    operands.push_back(llvm::ConstantAsMetadata::get(
        llvm::ConstantInt::get(i64, static_cast<uint64_t>(id))));
  return llvm::MDNode::get(ctx, operands);
}

/// Builds one `!memprof` MemInfoBlock: the allocation call stack, then the
/// allocation-type tags, then the `(context id, total size)` pairs. The order
/// is what the verifier requires -- strings first, nodes after -- so it is not
/// a free choice. `MemProfMIBAttr::verify` has already rejected any other
/// element kind, which is what makes the casts below unconditional.
static llvm::MDNode *convertMemProfMIB(llvm::LLVMContext &ctx,
                                       MemProfMIBAttr mib) {
  llvm::Type *i64 = llvm::Type::getInt64Ty(ctx);
  SmallVector<llvm::Metadata *> operands;
  operands.push_back(convertCallStack(ctx, mib.getCallStack().asArrayRef()));
  for (Attribute tag : mib.getTags())
    operands.push_back(
        llvm::MDString::get(ctx, cast<StringAttr>(tag).getValue()));
  if (ArrayAttr contextSizes = mib.getContextSizes()) {
    for (Attribute contextSize : contextSizes) {
      auto entry = cast<MemProfContextSizeAttr>(contextSize);
      llvm::Metadata *pair[] = {
          llvm::ConstantAsMetadata::get(
              llvm::ConstantInt::get(i64, entry.getId())),
          llvm::ConstantAsMetadata::get(
              llvm::ConstantInt::get(i64, entry.getSize()))};
      operands.push_back(llvm::MDNode::get(ctx, pair));
    }
  }
  return llvm::MDNode::get(ctx, operands);
}

/// Writes `!clang.arc.no_objc_arc_exceptions` back onto the call. The node is
/// valueless, so the presence of the attribute is the whole content, and the
/// kind is registered by name rather than being a fixed one.
template <typename OpTy>
static void setNoObjCARCExceptionsMetadata(OpTy op, llvm::Instruction *inst) {
  if (!op.getNoObjcArcExceptions())
    return;
  llvm::LLVMContext &ctx = inst->getContext();
  inst->setMetadata(ctx.getMDKindID("clang.arc.no_objc_arc_exceptions"),
                    llvm::MDNode::get(ctx, {}));
}

/// Writes `!callees` back onto the call. A symbol that no longer resolves is
/// dropped from the list rather than failing the export: the node is a hint to
/// indirect call promotion, and a stale entry is worse than a shorter list.
template <typename OpTy>
static void setCalleesMetadata(OpTy op, llvm::Instruction *inst,
                               LLVM::ModuleTranslation &moduleTranslation) {
  ArrayAttr callees = op.getCalleesAttr();
  if (!callees)
    return;

  SmallVector<llvm::Metadata *> nodes;
  nodes.reserve(callees.size());
  for (Attribute callee : callees) {
    llvm::Function *func = moduleTranslation.lookupFunction(
        cast<FlatSymbolRefAttr>(callee).getValue());
    if (!func)
      continue;
    nodes.push_back(llvm::ValueAsMetadata::get(func));
  }
  if (nodes.empty())
    return;

  inst->setMetadata(llvm::LLVMContext::MD_callees,
                    llvm::MDNode::get(inst->getContext(), nodes));
}

/// Writes `!unpredictable` back onto the instruction. The node is valueless, so
/// the presence of the attribute is the whole content.
template <typename OpTy>
static void setUnpredictableMetadata(OpTy op, llvm::Instruction *inst) {
  if (!op.getUnpredictable())
    return;
  inst->setMetadata(llvm::LLVMContext::MD_unpredictable,
                    llvm::MDNode::get(inst->getContext(), {}));
}

/// Builds the `llvm::InlineAsm` callee of an `llvm.inline_asm` or an
/// `llvm.callbr`.
template <typename OpTy>
static llvm::InlineAsm *
convertInlineAsm(OpTy op, LLVM::LLVMFunctionType type,
                 LLVM::ModuleTranslation &moduleTranslation) {
  auto *llvmType =
      static_cast<llvm::FunctionType *>(moduleTranslation.convertType(type));
  if (op.getAsmDialect())
    return llvm::InlineAsm::get(llvmType, op.getAsmString(),
                                op.getConstraints(), op.getHasSideEffects(),
                                op.getIsAlignStack(),
                                convertAsmDialectToLLVM(*op.getAsmDialect()));
  return llvm::InlineAsm::get(llvmType, op.getAsmString(), op.getConstraints(),
                              op.getHasSideEffects(), op.getIsAlignStack());
}

/// Writes the metadata and operand attributes an `llvm.inline_asm` or an
/// `llvm.callbr` carries onto the call of its asm.
template <typename OpTy>
static LogicalResult
setInlineAsmCallMetadataAndAttrs(OpTy op, llvm::CallBase *inst,
                                 LLVM::ModuleTranslation &moduleTranslation) {
  // Same three as the call and invoke paths. Inline asm that touches memory
  // carries the same alias metadata a call does.
  moduleTranslation.setAccessGroupsMetadata(op, inst);
  moduleTranslation.setAliasScopeMetadata(op, inst);
  moduleTranslation.setTBAAMetadata(op, inst);
  if (ArrayRef<int64_t> srcloc = op.getSrcloc().value_or(ArrayRef<int64_t>());
      !srcloc.empty()) {
    llvm::LLVMContext &ctx = moduleTranslation.getLLVMContext();
    SmallVector<llvm::Metadata *> cookies;
    cookies.reserve(srcloc.size());
    llvm::Type *i64 = llvm::Type::getInt64Ty(ctx);
    for (int64_t cookie : srcloc)
      cookies.push_back(llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::getSigned(i64, cookie)));
    // `srcloc` is registered by name rather than being a fixed kind.
    inst->setMetadata(ctx.getMDKindID("srcloc"),
                      llvm::MDTuple::get(ctx, cookies));
  }
  // Operand attributes are indexed by argument, whether or not the asm has
  // a result: a void asm has no result on `llvm.callbr` (and may have none on
  // `llvm.inline_asm`), and its argument 0 is still argument 0.
  std::optional<ArrayAttr> operandAttrs = op.getOperandAttrs();
  if (!operandAttrs)
    return success();
  for (auto [argIdx, attr] : llvm::enumerate(*operandAttrs)) {
    auto dict = dyn_cast_if_present<DictionaryAttr>(attr);
    if (!dict || dict.empty())
      continue;
    FailureOr<llvm::AttrBuilder> b =
        moduleTranslation.convertParameterAttrs(op.getLoc(), dict);
    if (failed(b))
      return failure();
    if (auto elementType =
            dict.template getAs<TypeAttr>(InlineAsmOp::getElementTypeAttrName()))
      b->addTypeAttr(llvm::Attribute::ElementType,
                     moduleTranslation.convertType(elementType.getValue()));
    inst->addParamAttrs(argIdx, *b);
  }
  return success();
}

static LogicalResult
convertParameterAndResultAttrs(mlir::Location loc, ArrayAttr argAttrsArray,
                               ArrayAttr resAttrsArray, llvm::CallBase *call,
                               LLVM::ModuleTranslation &moduleTranslation) {
  if (argAttrsArray) {
    for (auto [argIdx, argAttrsAttr] : llvm::enumerate(argAttrsArray)) {
      if (auto argAttrs = cast<DictionaryAttr>(argAttrsAttr);
          !argAttrs.empty()) {
        FailureOr<llvm::AttrBuilder> attrBuilder =
            moduleTranslation.convertParameterAttrs(loc, argAttrs);
        if (failed(attrBuilder))
          return failure();
        call->addParamAttrs(argIdx, *attrBuilder);
      }
    }
  }

  if (resAttrsArray && resAttrsArray.size() > 0) {
    if (resAttrsArray.size() != 1)
      return mlir::emitError(loc, "llvm.func cannot have multiple results");
    if (auto resAttrs = cast<DictionaryAttr>(resAttrsArray[0]);
        !resAttrs.empty()) {
      FailureOr<llvm::AttrBuilder> attrBuilder =
          moduleTranslation.convertParameterAttrs(loc, resAttrs);
      if (failed(attrBuilder))
        return failure();
      call->addRetAttrs(*attrBuilder);
    }
  }
  return success();
}

static LogicalResult
convertOperationImpl(Operation &opInst, llvm::IRBuilderBase &builder,
                     LLVM::ModuleTranslation &moduleTranslation) {

  llvm::IRBuilder<>::FastMathFlagGuard fmfGuard(builder);
  if (auto fmf = dyn_cast<FastmathFlagsInterface>(opInst))
    builder.setFastMathFlags(getFastmathFlags(fmf));

  // Guard against storing void values. This can occur when CIR lowers a
  // struct-returning function with sret convention: the CIR call returns a
  // struct but the LLVM function returns void. The CIR store of the result
  // produces an llvm.store of a void value, which would crash in
  // CreateAlignedStore when computing alignment for void. Skip such stores
  // as the callee already wrote the result to the sret pointer.
  if (auto storeOp = dyn_cast<LLVM::StoreOp>(opInst)) {
    llvm::Value *val = moduleTranslation.lookupValue(storeOp.getValue());
    if (val && val->getType()->isVoidTy())
      return success();
  }

#include "mlir/Dialect/LLVMIR/LLVMConversions.inc"
#include "mlir/Dialect/LLVMIR/LLVMIntrinsicConversions.inc"

  // Emit function calls.  If the "callee" attribute is present, this is a
  // direct function call and we also need to look up the remapped function
  // itself.  Otherwise, this is an indirect call and the callee is the first
  // operand, look it up as a normal value.
  if (auto callOp = dyn_cast<LLVM::CallOp>(opInst)) {
    auto operands = moduleTranslation.lookupValues(callOp.getCalleeOperands());
    SmallVector<llvm::OperandBundleDef> opBundles =
        convertOperandBundles(callOp.getOpBundleOperands(),
                              callOp.getOpBundleTags(), moduleTranslation);
    ArrayRef<llvm::Value *> operandsRef(operands);
    llvm::FunctionType *calleeType = llvm::cast<llvm::FunctionType>(
        moduleTranslation.convertType(callOp.getCalleeFunctionType()));
    llvm::CallInst *call;
    if (auto attr = callOp.getCalleeAttr()) {
      if (llvm::Function *function =
              moduleTranslation.lookupFunction(attr.getValue())) {
        llvm::FunctionType *functionType = function->getFunctionType();
        // Use the call-site function type when it differs from the declared
        // function type and the callee is not an intrinsic or vararg.
        // This handles cases where the call-site argument types differ from
        // the declared parameter types (e.g., coerced struct arguments).
        bool needsCallSiteType =
            functionType != calleeType && !function->isIntrinsic() &&
            !functionType->isVarArg();
        if (!needsCallSiteType)
          call = builder.CreateCall(function, operandsRef, opBundles);
        else
          call = builder.CreateCall(calleeType, function, operandsRef,
                                    opBundles);
      } else {
        Operation *moduleOp = parentLLVMModule(&opInst);
        Operation *ifuncOp =
            moduleTranslation.symbolTable().lookupSymbolIn(moduleOp, attr);
        llvm::GlobalValue *ifunc = moduleTranslation.lookupIFunc(ifuncOp);
        call = builder.CreateCall(calleeType, ifunc, operandsRef, opBundles);
      }
    } else {
      call = builder.CreateCall(calleeType, operandsRef.front(),
                                operandsRef.drop_front(), opBundles);
    }
    call->setCallingConv(convertCConvToLLVM(callOp.getCConv()));
    call->setTailCallKind(convertTailCallKindToLLVM(callOp.getTailCallKind()));
    if (failed(moduleTranslation.convertArgAndResultAttrs(callOp, call)))
      return failure();

    if (failed(setModelledCallSiteAttrs(callOp, call)))
      return failure();
    if (failed(setCallSitePassthroughAttrs(callOp, call)))
      return failure();

    moduleTranslation.setAccessGroupsMetadata(callOp, call);
    moduleTranslation.setAliasScopeMetadata(callOp, call);
    moduleTranslation.setTBAAMetadata(callOp, call);
    moduleTranslation.setHeapAllocSiteMetadata(callOp.getHeapallocsiteAttr(),
                                               call);
    setCalleesMetadata(callOp, call, moduleTranslation);
    setNoObjCARCExceptionsMetadata(callOp, call);
    // If the called function has a result, remap the corresponding value.  Note
    // that LLVM IR dialect CallOp has either 0 or 1 result.
    if (opInst.getNumResults() != 0)
      moduleTranslation.mapValue(opInst.getResult(0), call);
    // Check that LLVM call returns void for 0-result functions.
    else if (!call->getType()->isVoidTy())
      return failure();
    moduleTranslation.mapCall(callOp, call);
    return success();
  }

  if (auto inlineAsmOp = dyn_cast<LLVM::InlineAsmOp>(opInst)) {
    SmallVector<Type, 8> operandTypes;
    llvm::append_range(operandTypes, inlineAsmOp.getOperands().getTypes());
    Type resultType =
        inlineAsmOp.getNumResults() == 0
            ? LLVM::LLVMVoidType::get(&moduleTranslation.getContext())
            : inlineAsmOp.getResultTypes()[0];
    llvm::InlineAsm *inlineAsmInst = convertInlineAsm(
        inlineAsmOp, LLVM::LLVMFunctionType::get(resultType, operandTypes),
        moduleTranslation);
    llvm::CallInst *inst = builder.CreateCall(
        inlineAsmInst,
        moduleTranslation.lookupValues(inlineAsmOp.getOperands()));
    inst->setTailCallKind(convertTailCallKindToLLVM(
        inlineAsmOp.getTailCallKindAttr().getTailCallKind()));
    if (failed(setInlineAsmCallMetadataAndAttrs(inlineAsmOp, inst,
                                                moduleTranslation)) ||
        failed(convertParameterAndResultAttrs(
            inlineAsmOp.getLoc(), /*argAttrsArray=*/nullptr,
            inlineAsmOp.getResAttrsAttr(), inst, moduleTranslation)) ||
        failed(setModelledCallSiteAttrs(inlineAsmOp, inst)) ||
        failed(setCallSitePassthroughAttrs(inlineAsmOp, inst)))
      return failure();
    if (opInst.getNumResults() != 0)
      moduleTranslation.mapValue(opInst.getResult(0), inst);
    return success();
  }

  if (auto callBrOp = dyn_cast<LLVM::CallBrOp>(opInst)) {
    llvm::InlineAsm *inlineAsm = convertInlineAsm(
        callBrOp, callBrOp.getCalleeFunctionType(), moduleTranslation);
    SmallVector<llvm::BasicBlock *> indirectDests;
    for (Block *dest : callBrOp.getIndirectDests())
      indirectDests.push_back(moduleTranslation.lookupBlock(dest));
    llvm::CallBrInst *inst = builder.CreateCallBr(
        inlineAsm, moduleTranslation.lookupBlock(callBrOp.getDefaultDest()),
        indirectDests,
        moduleTranslation.lookupValues(callBrOp.getAsmOperands()));
    if (failed(setInlineAsmCallMetadataAndAttrs(callBrOp, inst,
                                                moduleTranslation)) ||
        failed(setModelledCallSiteAttrs(callBrOp, inst)) ||
        failed(setCallSitePassthroughAttrs(callBrOp, inst)))
      return failure();
    moduleTranslation.mapBranch(callBrOp, inst);
    if (opInst.getNumResults() != 0)
      moduleTranslation.mapValue(opInst.getResult(0), inst);
    return success();
  }

  if (auto invOp = dyn_cast<LLVM::InvokeOp>(opInst)) {
    auto operands = moduleTranslation.lookupValues(invOp.getCalleeOperands());
    SmallVector<llvm::OperandBundleDef> opBundles =
        convertOperandBundles(invOp.getOpBundleOperands(),
                              invOp.getOpBundleTags(), moduleTranslation);
    ArrayRef<llvm::Value *> operandsRef(operands);
    llvm::FunctionType *calleeType = llvm::cast<llvm::FunctionType>(
        moduleTranslation.convertType(invOp.getCalleeFunctionType()));
    llvm::InvokeInst *result;
    if (auto attr = opInst.getAttrOfType<FlatSymbolRefAttr>("callee")) {
      if (llvm::Function *function =
              moduleTranslation.lookupFunction(attr.getValue())) {
        llvm::FunctionType *functionType = function->getFunctionType();
        // Use the call-site function type when it differs from the declared
        // function type and the callee is not an intrinsic or vararg.
        // This handles cases where the call-site argument types differ from
        // the declared parameter types (e.g., coerced struct arguments).
        bool needsCallSiteType =
            functionType != calleeType && !function->isIntrinsic() &&
            !functionType->isVarArg();
        if (!needsCallSiteType) {
          result = builder.CreateInvoke(
              function, moduleTranslation.lookupBlock(invOp.getSuccessor(0)),
              moduleTranslation.lookupBlock(invOp.getSuccessor(1)), operandsRef,
              opBundles);
        } else {
          result = builder.CreateInvoke(
              calleeType, function,
              moduleTranslation.lookupBlock(invOp.getSuccessor(0)),
              moduleTranslation.lookupBlock(invOp.getSuccessor(1)), operandsRef,
              opBundles);
        }
      } else {
        // The callee may be an ifunc rather than a function.
        Operation *moduleOp = parentLLVMModule(&opInst);
        Operation *ifuncOp =
            moduleTranslation.symbolTable().lookupSymbolIn(moduleOp, attr);
        llvm::GlobalValue *ifunc = moduleTranslation.lookupIFunc(ifuncOp);
        result = builder.CreateInvoke(
            calleeType, ifunc,
            moduleTranslation.lookupBlock(invOp.getSuccessor(0)),
            moduleTranslation.lookupBlock(invOp.getSuccessor(1)), operandsRef,
            opBundles);
      }
    } else {
      result = builder.CreateInvoke(
          calleeType, operandsRef.front(),
          moduleTranslation.lookupBlock(invOp.getSuccessor(0)),
          moduleTranslation.lookupBlock(invOp.getSuccessor(1)),
          operandsRef.drop_front(), opBundles);
    }
    result->setCallingConv(convertCConvToLLVM(invOp.getCConv()));
    // Same order as the call path: the two are independent today, and keeping
    // them in step means nobody has to work out whether the difference means
    // something.
    if (failed(moduleTranslation.convertArgAndResultAttrs(invOp, result)))
      return failure();
    if (failed(setModelledCallSiteAttrs(invOp, result)))
      return failure();
    if (failed(setCallSitePassthroughAttrs(invOp, result)))
      return failure();
    // Same three as the call path above. Without them an invoke's alias
    // metadata is imported onto the op and then silently discarded on the way
    // back out.
    moduleTranslation.setAccessGroupsMetadata(invOp, result);
    moduleTranslation.setAliasScopeMetadata(invOp, result);
    moduleTranslation.setTBAAMetadata(invOp, result);
    moduleTranslation.setHeapAllocSiteMetadata(invOp.getHeapallocsiteAttr(),
                                               result);
    setCalleesMetadata(invOp, result, moduleTranslation);
    setNoObjCARCExceptionsMetadata(invOp, result);
    // An invoke can be a loop latch, so it writes !llvm.loop the way the branch
    // and switch paths do.
    moduleTranslation.setLoopMetadata(invOp, result);
    setIrrLoopMetadata(invOp, result);
    moduleTranslation.mapBranch(invOp, result);
    // InvokeOp can only have 0 or 1 result
    if (invOp->getNumResults() != 0) {
      moduleTranslation.mapValue(opInst.getResult(0), result);
      return success();
    }
    return success(result->getType()->isVoidTy());
  }

  if (auto lpOp = dyn_cast<LLVM::LandingpadOp>(opInst)) {
    llvm::Type *ty = moduleTranslation.convertType(lpOp.getType());
    llvm::LandingPadInst *lpi =
        builder.CreateLandingPad(ty, lpOp.getNumOperands());
    lpi->setCleanup(lpOp.getCleanup());

    // Add clauses
    for (llvm::Value *operand :
         moduleTranslation.lookupValues(lpOp.getOperands())) {
      // All operands should be constant - checked by verifier
      if (auto *constOperand = dyn_cast<llvm::Constant>(operand))
        lpi->addClause(constOperand);
    }
    moduleTranslation.mapValue(lpOp.getResult(), lpi);
    return success();
  }

  // Emit branches.  We need to look up the remapped blocks and ignore the
  // block arguments that were transformed into PHI nodes.
  if (auto brOp = dyn_cast<LLVM::BrOp>(opInst)) {
    llvm::BranchInst *branch =
        builder.CreateBr(moduleTranslation.lookupBlock(brOp.getSuccessor()));
    // ModuleImport and the export legalization tag the forwarding blocks they
    // insert (an invoke whose result feeds its normal destination, a
    // successor repeated with arguments); pass the tag on so a consumer can
    // fold them away and recover the original CFG.
    if (opInst.hasAttr("llvm_forwarding_branch"))
      branch->setMetadata("llvm_forwarding_branch",
                          llvm::MDNode::get(branch->getContext(), {}));
    moduleTranslation.mapBranch(&opInst, branch);
    moduleTranslation.setLoopMetadata(&opInst, branch);
    setIrrLoopMetadata(brOp, branch);
    return success();
  }
  if (auto condbrOp = dyn_cast<LLVM::CondBrOp>(opInst)) {
    llvm::BranchInst *branch = builder.CreateCondBr(
        moduleTranslation.lookupValue(condbrOp.getOperand(0)),
        moduleTranslation.lookupBlock(condbrOp.getSuccessor(0)),
        moduleTranslation.lookupBlock(condbrOp.getSuccessor(1)));
    moduleTranslation.mapBranch(&opInst, branch);
    moduleTranslation.setLoopMetadata(&opInst, branch);
    setUnpredictableMetadata(condbrOp, branch);
    setIrrLoopMetadata(condbrOp, branch);
    return success();
  }
  if (auto switchOp = dyn_cast<LLVM::SwitchOp>(opInst)) {
    llvm::SwitchInst *switchInst = builder.CreateSwitch(
        moduleTranslation.lookupValue(switchOp.getValue()),
        moduleTranslation.lookupBlock(switchOp.getDefaultDestination()),
        switchOp.getCaseDestinations().size());

    // Handle switch with zero cases. The loop annotation still has to be
    // written: a latch switch that has only a default destination is degenerate
    // but legal, and returning here would drop it.
    if (!switchOp.getCaseValues()) {
      moduleTranslation.setLoopMetadata(&opInst, switchInst);
      moduleTranslation.mapBranch(&opInst, switchInst);
      setUnpredictableMetadata(switchOp, switchInst);
      setIrrLoopMetadata(switchOp, switchInst);
      return success();
    }

    auto *ty = llvm::cast<llvm::IntegerType>(
        moduleTranslation.convertType(switchOp.getValue().getType()));
    for (auto i :
         llvm::zip(llvm::cast<DenseIntElementsAttr>(*switchOp.getCaseValues()),
                   switchOp.getCaseDestinations()))
      switchInst->addCase(
          llvm::ConstantInt::get(ty, std::get<0>(i).getLimitedValue()),
          moduleTranslation.lookupBlock(std::get<1>(i)));

    moduleTranslation.setLoopMetadata(&opInst, switchInst);
    moduleTranslation.mapBranch(&opInst, switchInst);
    setUnpredictableMetadata(switchOp, switchInst);
    setIrrLoopMetadata(switchOp, switchInst);
    return success();
  }
  if (auto indBrOp = dyn_cast<LLVM::IndirectBrOp>(opInst)) {
    llvm::IndirectBrInst *indBr = builder.CreateIndirectBr(
        moduleTranslation.lookupValue(indBrOp.getAddr()),
        indBrOp->getNumSuccessors());
    for (auto *succ : indBrOp.getSuccessors())
      indBr->addDestination(moduleTranslation.lookupBlock(succ));
    moduleTranslation.mapBranch(&opInst, indBr);
    return success();
  }

  // Emit addressof.  We need to look up the global value referenced by the
  // operation and store it in the MLIR-to-LLVM value mapping.  This does not
  // emit any LLVM instruction.
  if (auto addressOfOp = dyn_cast<LLVM::AddressOfOp>(opInst)) {
    LLVM::GlobalOp global =
        addressOfOp.getGlobal(moduleTranslation.symbolTable());
    LLVM::LLVMFuncOp function =
        addressOfOp.getFunction(moduleTranslation.symbolTable());
    LLVM::AliasOp alias = addressOfOp.getAlias(moduleTranslation.symbolTable());
    LLVM::IFuncOp ifunc = addressOfOp.getIFunc(moduleTranslation.symbolTable());

    // The verifier should not have allowed this.
    assert((global || function || alias || ifunc) &&
           "referencing an undefined global, function, alias, or ifunc");

    llvm::Value *llvmValue = nullptr;
    if (global)
      llvmValue = moduleTranslation.lookupGlobal(global);
    else if (alias)
      llvmValue = moduleTranslation.lookupAlias(alias);
    else if (function)
      llvmValue = moduleTranslation.lookupFunction(function.getName());
    else
      llvmValue = moduleTranslation.lookupIFunc(ifunc);

    moduleTranslation.mapValue(addressOfOp.getResult(), llvmValue);
    return success();
  }

  // Emit dso_local_equivalent. We need to look up the global value referenced
  // by the operation and store it in the MLIR-to-LLVM value mapping.
  if (auto dsoLocalEquivalentOp =
          dyn_cast<LLVM::DSOLocalEquivalentOp>(opInst)) {
    LLVM::LLVMFuncOp function =
        dsoLocalEquivalentOp.getFunction(moduleTranslation.symbolTable());
    LLVM::AliasOp alias =
        dsoLocalEquivalentOp.getAlias(moduleTranslation.symbolTable());

    // The verifier should not have allowed this.
    assert((function || alias) &&
           "referencing an undefined function, or alias");

    llvm::Value *llvmValue = nullptr;
    if (alias)
      llvmValue = moduleTranslation.lookupAlias(alias);
    else
      llvmValue = moduleTranslation.lookupFunction(function.getName());

    moduleTranslation.mapValue(
        dsoLocalEquivalentOp.getResult(),
        llvm::DSOLocalEquivalent::get(cast<llvm::GlobalValue>(llvmValue)));
    return success();
  }

  // Emit blockaddress. We first need to find the LLVM block referenced by this
  // operation and then create a LLVM block address for it.
  if (auto blockAddressOp = dyn_cast<LLVM::BlockAddressOp>(opInst)) {
    BlockAddressAttr blockAddressAttr = blockAddressOp.getBlockAddr();
    llvm::BasicBlock *llvmBlock =
        moduleTranslation.lookupBlockAddress(blockAddressAttr);

    llvm::Value *llvmValue = nullptr;
    StringRef fnName = blockAddressAttr.getFunction().getValue();
    if (llvmBlock) {
      llvm::Function *llvmFn = moduleTranslation.lookupFunction(fnName);
      llvmValue = llvm::BlockAddress::get(llvmFn, llvmBlock);
    } else {
      // The matching LLVM block is not yet emitted, a placeholder is created
      // in its place. When the LLVM block is emitted later in translation,
      // the llvmValue is replaced with the actual llvm::BlockAddress.
      // A GlobalVariable is chosen as placeholder because in general LLVM
      // constants are uniqued and are not proper for RAUW, since that could
      // harm unrelated uses of the constant.
      // The placeholder must live in address space 0: the resolved
      // llvm::BlockAddress it is RAUW'd with takes the function's (program)
      // address space, and letting the GlobalVariable ctor default to the
      // module's default globals address space (nonzero on e.g. amdgcn)
      // yields a type-mismatched initializer when this feeds a global.
      llvmValue = new llvm::GlobalVariable(
          *moduleTranslation.getLLVMModule(),
          llvm::PointerType::getUnqual(moduleTranslation.getLLVMContext()),
          /*isConstant=*/true, llvm::GlobalValue::LinkageTypes::ExternalLinkage,
          /*Initializer=*/nullptr,
          Twine("__mlir_block_address_")
              .concat(Twine(fnName))
              .concat(Twine((uint64_t)blockAddressOp.getOperation())),
          /*InsertBefore=*/nullptr, llvm::GlobalValue::NotThreadLocal,
          /*AddressSpace=*/0);
      moduleTranslation.mapUnresolvedBlockAddress(blockAddressOp, llvmValue);
    }

    moduleTranslation.mapValue(blockAddressOp.getResult(), llvmValue);
    return success();
  }

  // Emit block label. If this label is seen before BlockAddressOp is
  // translated, go ahead and already map it.
  if (auto blockTagOp = dyn_cast<LLVM::BlockTagOp>(opInst)) {
    auto funcOp = blockTagOp->getParentOfType<LLVMFuncOp>();
    BlockAddressAttr blockAddressAttr = BlockAddressAttr::get(
        &moduleTranslation.getContext(),
        FlatSymbolRefAttr::get(&moduleTranslation.getContext(),
                               funcOp.getName()),
        blockTagOp.getTag());
    moduleTranslation.mapBlockAddress(blockAddressAttr,
                                      builder.GetInsertBlock());
    return success();
  }

  return failure();
}

static LogicalResult
amendOperationImpl(Operation &op, ArrayRef<llvm::Instruction *> instructions,
                   NamedAttribute attribute,
                   LLVM::ModuleTranslation &moduleTranslation) {
  StringRef name = attribute.getName();

  if (name == LLVMDialect::getMmraAttrName()) {
    SmallVector<llvm::MMRAMetadata::TagT> tags;
    if (auto oneTag = dyn_cast<LLVM::MMRATagAttr>(attribute.getValue())) {
      tags.emplace_back(oneTag.getPrefix(), oneTag.getSuffix());
    } else if (auto manyTags = dyn_cast<ArrayAttr>(attribute.getValue())) {
      for (Attribute attr : manyTags) {
        auto tag = dyn_cast<MMRATagAttr>(attr);
        if (!tag)
          return op.emitOpError(
              "MMRA annotations array contains value that isn't an MMRA tag");
        tags.emplace_back(tag.getPrefix(), tag.getSuffix());
      }
    } else {
      return op.emitOpError(
          "llvm.mmra is something other than an MMRA tag or an array of them");
    }
    llvm::MDTuple *mmraMd =
        llvm::MMRAMetadata::getMD(moduleTranslation.getLLVMContext(), tags);
    if (!mmraMd) {
      // Empty list, canonicalizes to nothing
      return success();
    }
    for (llvm::Instruction *inst : instructions) {
      // `!mmra` is legal only on the memory-access, atomic and fence
      // instructions, so it goes on the ones that can carry it rather than on
      // everything the op produced -- writing it wider is IR the verifier
      // rejects. `canInstructionHaveMMRAs` is the verifier's own predicate, so
      // the two cannot disagree.
      if (!llvm::canInstructionHaveMMRAs(*inst))
        continue;
      inst->setMetadata(llvm::LLVMContext::MD_mmra, mmraMd);
    }
    return success();
  }

  // The fallback carrier for `!llvm.loop`: an op with no typed
  // `loop_annotation` slot. Writes to every instruction the op produced, for
  // the same reason `!nosanitize` below does.
  if (name == LLVMDialect::getLoopAnnotationAttrName()) {
    auto annotation = dyn_cast<LoopAnnotationAttr>(attribute.getValue());
    if (!annotation)
      return op.emitOpError() << "expected a loop annotation in "
                              << LLVMDialect::getLoopAnnotationAttrName();
    llvm::MDNode *node = moduleTranslation.getLoopMetadataNode(annotation, &op);
    if (!node)
      return op.emitOpError() << "failed to translate the loop annotation in "
                              << LLVMDialect::getLoopAnnotationAttrName();
    // An op that produced no instruction has nowhere to put the metadata.
    // Import accepts this attribute on any carrier, so without this the loss
    // would simply move from import -- where it is reported -- to export,
    // where it would be silent. Report it here instead.
    if (instructions.empty())
      return op.emitWarning()
             << "dropped !llvm.loop: the operation produced no LLVM "
                "instruction to attach it to";
    for (llvm::Instruction *inst : instructions)
      inst->setMetadata(llvm::LLVMContext::MD_loop, node);
    return success();
  }
  // `!nosanitize` goes on every instruction the op produced: it has no single
  // carrier, and an op can expand into several accesses that must each stay
  // uninstrumented.
  if (name == LLVMDialect::getNoSanitizeAttrName()) {
    for (llvm::Instruction *inst : instructions)
      inst->setMetadata(llvm::LLVMContext::MD_nosanitize,
                        llvm::MDNode::get(inst->getContext(), {}));
    return success();
  }
  // `!memprof` and `!callsite` are call-only -- the verifier rejects them
  // anywhere else -- so unlike `!nosanitize` above they go on the call the op
  // produced rather than on every instruction.
  if (name == LLVMDialect::getMemProfAttrName()) {
    auto mibs = dyn_cast<ArrayAttr>(attribute.getValue());
    if (!mibs)
      return op.emitOpError("expected an array for ")
             << LLVMDialect::getMemProfAttrName();
    for (llvm::Instruction *inst : instructions) {
      if (!isa<llvm::CallBase>(inst))
        continue;
      SmallVector<llvm::Metadata *> nodes;
      for (Attribute mibAttr : mibs) {
        auto mib = dyn_cast<MemProfMIBAttr>(mibAttr);
        if (!mib)
          return op.emitOpError("expected a memprof_mib for ")
                 << LLVMDialect::getMemProfAttrName();
        nodes.push_back(convertMemProfMIB(inst->getContext(), mib));
      }
      inst->setMetadata(llvm::LLVMContext::MD_memprof,
                        llvm::MDNode::get(inst->getContext(), nodes));
    }
    return success();
  }
  if (name == LLVMDialect::getTailCallKindAttrName()) {
    auto kind = dyn_cast<LLVM::TailCallKindAttr>(attribute.getValue());
    if (!kind)
      return op.emitOpError("expected a tail call kind for ")
             << LLVMDialect::getTailCallKindAttrName();
    for (llvm::Instruction *inst : instructions)
      // An `invoke` cannot be a tail call, so only a `CallInst` takes it.
      if (auto *callInst = dyn_cast<llvm::CallInst>(inst))
        callInst->setTailCallKind(
            convertTailCallKindToLLVM(kind.getTailCallKind()));
    return success();
  }
  if (name == LLVMDialect::getMemParallelLoopAccessAttrName()) {
    auto loops = dyn_cast<ArrayAttr>(attribute.getValue());
    if (!loops || loops.empty())
      return op.emitOpError("expected a non-empty array for ")
             << LLVMDialect::getMemParallelLoopAccessAttrName();
    SmallVector<llvm::Metadata *> nodes;
    for (Attribute loopAttr : loops) {
      auto annotation = dyn_cast<LoopAnnotationAttr>(loopAttr);
      if (!annotation)
        return op.emitOpError("expected a loop_annotation in ")
               << LLVMDialect::getMemParallelLoopAccessAttrName();
      // The very same node the loop's own `!llvm.loop` gets: the check in
      // `Loop::isAnnotatedParallel` is `is_contained(operands, loopID)`, so
      // an equal-but-separate node is no use. The translation caches by
      // attribute, and loop annotations carry an identity, so this is the
      // loop's node rather than a copy of it.
      llvm::MDNode *node =
          moduleTranslation.getLoopMetadataNode(annotation, &op);
      if (!node)
        return op.emitOpError("failed to translate a loop annotation in ")
               << LLVMDialect::getMemParallelLoopAccessAttrName();
      nodes.push_back(node);
    }
    // One loop is written as the loop identifier itself, which is what LLVM
    // emits and what the nested case wraps in a list.
    llvm::MDNode *node =
        nodes.size() == 1
            ? cast<llvm::MDNode>(nodes.front())
            : llvm::MDNode::get(moduleTranslation.getLLVMContext(), nodes);
    for (llvm::Instruction *inst : instructions)
      inst->setMetadata(llvm::LLVMContext::MD_mem_parallel_loop_access, node);
    return success();
  }
  if (name == LLVMDialect::getDIAssignIDAttrName()) {
    auto assignID = dyn_cast<LLVM::DIAssignIDAttr>(attribute.getValue());
    if (!assignID)
      return op.emitOpError("expected a di_assign_id for ")
             << LLVMDialect::getDIAssignIDAttrName();
    // One node per identity, from the translation's cache: a store and the
    // `#dbg_assign` describing it have to come out naming the same one, and
    // a fresh distinct node each time would silently break the link.
    llvm::MDNode *node = moduleTranslation.translateDIAssignID(assignID);
    for (llvm::Instruction *inst : instructions)
      inst->setMetadata(llvm::LLVMContext::MD_DIAssignID, node);
    return success();
  }
  if (name == LLVMDialect::getPreserveAccessIndexAttrName()) {
    auto type = dyn_cast<LLVM::DITypeAttr>(attribute.getValue());
    if (!type)
      return op.emitOpError("expected a DIType for ")
             << LLVMDialect::getPreserveAccessIndexAttrName();
    // Built once: the node depends only on `type`, so instructions sharing
    // the attribute share the node rather than rebuilding it each time.
    llvm::MDNode *node = moduleTranslation.getPreserveAccessIndexNode(type);
    for (llvm::Instruction *inst : instructions)
      inst->setMetadata(llvm::LLVMContext::MD_preserve_access_index, node);
    return success();
  }
  if (name == LLVMDialect::getPCSectionsAttrName()) {
    auto sections = dyn_cast<ArrayAttr>(attribute.getValue());
    if (!sections)
      return op.emitOpError("expected an array for ")
             << LLVMDialect::getPCSectionsAttrName();
    // Built once: the node depends only on `sections`, and rebuilding it per
    // instruction would also mean a malformed one goes unreported on an op
    // that produced no instructions at all.
    llvm::MDNode *node = moduleTranslation.getPCSectionsNode(sections);
    if (!node)
      return op.emitOpError("expected (section, aux) pairs in ")
             << LLVMDialect::getPCSectionsAttrName();
    for (llvm::Instruction *inst : instructions)
      inst->setMetadata(llvm::LLVMContext::MD_pcsections, node);
    return success();
  }
  // Both of these are valueless: the presence of the metadata is the whole
  // content, so an empty node is the exact round trip.
  //
  // Unlike the three kinds below, these go on every instruction the op
  // produced rather than a filtered subset. `Verifier::visitInstruction`
  // constrains the carrier of neither, so a wider write cannot produce IR
  // the verifier rejects, and every consumer reads the kind off an
  // instruction it already holds -- `SpillUtils` off an `AllocaInst`,
  // `ImplicitNullChecks` off a block terminator -- so a copy elsewhere is
  // inert. Filtering would also be wrong for `!make.implicit`, which is not
  // a branch-only kind: `GuardUtils` reads it off a guard call, `Local`'s
  // switch-to-branch rewrite off a `SwitchInst`, `CodeGenPrepare` off a
  // `SelectInst`, and `IRBuilder::CreateCondBr` copies it off whatever
  // instruction it is handed.
  for (auto [unitName, kind] :
       {std::pair{LLVMDialect::getMakeImplicitAttrName(),
                  llvm::LLVMContext::MD_make_implicit},
        std::pair{LLVMDialect::getCoroOutsideFrameAttrName(),
                  llvm::LLVMContext::MD_coro_outside_frame}}) {
    if (name != unitName)
      continue;
    for (llvm::Instruction *inst : instructions)
      inst->setMetadata(kind, llvm::MDNode::get(inst->getContext(), {}));
    return success();
  }
  // Each of these three is only legal on some instructions, so each is
  // written onto the ones that can carry it rather than onto everything the
  // op produced. Writing it wider would produce IR the verifier rejects.
  if (name == LLVMDialect::getFPMathAttrName()) {
    auto accuracy = dyn_cast<FloatAttr>(attribute.getValue());
    if (!accuracy)
      return op.emitOpError("expected a float for ")
             << LLVMDialect::getFPMathAttrName();
    llvm::LLVMContext &ctx = moduleTranslation.getLLVMContext();
    // The metadata holds a `float`, so narrow first and judge the value that
    // will actually be written: a double small enough to round to zero would
    // otherwise pass a check on the original and still emit a zero.
    //
    // Converted from the `APFloat` rather than through `double`. A `f80` or
    // `f128` attribute would otherwise round twice, and a value near an f32
    // midpoint can land on a different float that way.
    llvm::APFloat accuracyValue = accuracy.getValue();
    bool losesInfo = false;
    accuracyValue.convert(llvm::APFloat::IEEEsingle(),
                          llvm::APFloat::rmNearestTiesToEven, &losesInfo);
    auto *value =
        cast<llvm::ConstantFP>(llvm::ConstantFP::get(ctx, accuracyValue));
    // LLVM's verifier wants a positive, finite, non-zero accuracy. Without
    // this the op exports cleanly and the module fails verification later,
    // with nothing left pointing back at the attribute that caused it.
    if (!accuracyValue.isFiniteNonZero() || accuracyValue.isNegative())
      return op.emitOpError("expected a positive finite value for ")
             << LLVMDialect::getFPMathAttrName();
    llvm::MDNode *node =
        llvm::MDNode::get(ctx, llvm::ConstantAsMetadata::get(value));
    for (llvm::Instruction *inst : instructions) {
      if (!inst->getType()->isFPOrFPVectorTy())
        continue;
      inst->setMetadata(llvm::LLVMContext::MD_fpmath, node);
    }
    return success();
  }
  if (name == LLVMDialect::getAlignMetadataAttrName()) {
    auto align = dyn_cast<IntegerAttr>(attribute.getValue());
    if (!align)
      return op.emitOpError("expected an integer for ")
             << LLVMDialect::getAlignMetadataAttrName();
    // An alignment is a power of two within LLVM's limit. The value is
    // written as an `i64`, so anything wider than that cannot be one either.
    const llvm::APInt &alignValue = align.getValue();
    if (alignValue.getActiveBits() > 64 ||
        !llvm::isPowerOf2_64(alignValue.getZExtValue()) ||
        alignValue.getZExtValue() > llvm::Value::MaximumAlignment)
      return op.emitOpError("expected a power of two no greater than ")
             << llvm::Value::MaximumAlignment << " for "
             << LLVMDialect::getAlignMetadataAttrName();
    llvm::LLVMContext &ctx = moduleTranslation.getLLVMContext();
    llvm::MDNode *node = llvm::MDNode::get(
        ctx, llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                 llvm::Type::getInt64Ty(ctx), alignValue.getZExtValue())));
    for (llvm::Instruction *inst : instructions) {
      // The verifier allows `!align` only on a load of a pointer.
      if (!isa<llvm::LoadInst>(inst) || !inst->getType()->isPointerTy())
        continue;
      inst->setMetadata(llvm::LLVMContext::MD_align, node);
    }
    return success();
  }
  if (name == LLVMDialect::getNoaliasAddrspaceAttrName()) {
    auto ranges = dyn_cast<ArrayAttr>(attribute.getValue());
    if (!ranges)
      return op.emitOpError("expected an array for ")
             << LLVMDialect::getNoaliasAddrspaceAttrName();
    // `ConstantRangeAttr` only promises that the two bounds have the same
    // width, and this is a discardable attribute, so there is no op verifier
    // behind it either. Everything LLVM requires of the list is checked here;
    // otherwise a bad one exports cleanly and fails module verification with
    // nothing left pointing back at the attribute.
    StringRef rangesName = LLVMDialect::getNoaliasAddrspaceAttrName();
    if (ranges.empty())
      return op.emitOpError("expected at least one range in ") << rangesName;

    llvm::LLVMContext &ctx = moduleTranslation.getLLVMContext();
    llvm::Type *boundTy = llvm::IntegerType::get(ctx, 32);
    SmallVector<llvm::ConstantRange> intervals;
    SmallVector<llvm::Metadata *> bounds;
    for (Attribute rangeAttr : ranges) {
      auto range = dyn_cast<LLVM::ConstantRangeAttr>(rangeAttr);
      if (!range)
        return op.emitOpError("expected a constant_range in ") << rangesName;
      APInt lower = range.getLower();
      APInt upper = range.getUpper();
      if (lower.getBitWidth() != 32)
        return op.emitOpError("expected 32-bit bounds in ") << rangesName;
      // Equal bounds name either no address space or every one of them.
      // LLVM rejects both, and `ConstantRange` will not hold the first.
      if (lower == upper)
        return op.emitOpError("expected distinct bounds in ") << rangesName;

      llvm::ConstantRange interval(lower, upper);
      if (!intervals.empty()) {
        const llvm::ConstantRange &previous = intervals.back();
        if (!interval.intersectWith(previous).isEmptySet())
          return op.emitOpError("expected non-overlapping ranges in ")
                 << rangesName;
        if (!lower.sgt(previous.getLower()))
          return op.emitOpError("expected ranges in increasing order in ")
                 << rangesName;
        // Two ranges that meet are one range, and LLVM wants it written as
        // one.
        if (interval.getUpper() == previous.getLower() ||
            interval.getLower() == previous.getUpper())
          return op.emitOpError("expected non-contiguous ranges in ")
                 << rangesName;
      }
      intervals.push_back(interval);

      // The pairs stay separate: unioning them would claim address spaces
      // between two disjoint ranges that the metadata never named.
      bounds.push_back(llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(boundTy, lower)));
      bounds.push_back(llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(boundTy, upper)));
    }
    // Past two ranges the last one can wrap around into the first, which the
    // pairwise check never compares.
    if (intervals.size() > 2) {
      const llvm::ConstantRange &first = intervals.front();
      const llvm::ConstantRange &last = intervals.back();
      if (!first.intersectWith(last).isEmptySet())
        return op.emitOpError("expected non-overlapping ranges in ")
               << rangesName;
      if (first.getUpper() == last.getLower() ||
          first.getLower() == last.getUpper())
        return op.emitOpError("expected non-contiguous ranges in ")
               << rangesName;
    }

    llvm::MDNode *node = llvm::MDNode::get(ctx, bounds);
    for (llvm::Instruction *inst : instructions) {
      // `llvm::CallInst`, not `llvm::CallBase`: the verifier accepts
      // `!noalias.addrspace` on a call but not on an invoke or a callbr.
      if (!isa<llvm::LoadInst, llvm::StoreInst, llvm::AtomicRMWInst,
               llvm::AtomicCmpXchgInst, llvm::CallInst>(inst))
        continue;
      inst->setMetadata(llvm::LLVMContext::MD_noalias_addrspace, node);
    }
    return success();
  }
  // A value profile goes on the call or load the op produced: indirect call
  // targets and memory intrinsic sizes on a call, vtable targets on a load.
  if (name == LLVMDialect::getValueProfileAttrName()) {
    auto values = dyn_cast<DenseI64ArrayAttr>(attribute.getValue());
    if (!values || values.size() < 2 || values.size() % 2 != 0)
      return op.emitOpError("expected a kind, a total and value-count "
                            "pairs for ")
             << LLVMDialect::getValueProfileAttrName();
    for (llvm::Instruction *inst : instructions) {
      if (!isa<llvm::CallBase, llvm::LoadInst>(inst))
        continue;
      llvm::LLVMContext &ctx = inst->getContext();
      llvm::Type *i32 = llvm::Type::getInt32Ty(ctx);
      llvm::Type *i64 = llvm::Type::getInt64Ty(ctx);
      SmallVector<llvm::Metadata *> operands = {
          llvm::MDString::get(ctx, "VP"),
          llvm::ConstantAsMetadata::get(
              llvm::ConstantInt::get(i32, values[0]))};
      for (int64_t value : values.asArrayRef().drop_front())
        operands.push_back(llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(i64, static_cast<uint64_t>(value))));
      inst->setMetadata(llvm::LLVMContext::MD_prof,
                        llvm::MDNode::get(ctx, operands));
    }
    return success();
  }
  if (name == LLVMDialect::getCallsiteAttrName()) {
    auto callStack = dyn_cast<DenseI64ArrayAttr>(attribute.getValue());
    if (!callStack)
      return op.emitOpError("expected a dense i64 array for ")
             << LLVMDialect::getCallsiteAttrName();
    for (llvm::Instruction *inst : instructions) {
      if (!isa<llvm::CallBase>(inst))
        continue;
      inst->setMetadata(
          llvm::LLVMContext::MD_callsite,
          convertCallStack(inst->getContext(), callStack.asArrayRef()));
    }
    return success();
  }
  if (name == LLVMDialect::getIntrinsicCallFnAttrsName() ||
      name == LLVMDialect::getIntrinsicCallArgAttrsName() ||
      name == LLVMDialect::getIntrinsicCallResAttrsName())
    return setIntrinsicCallAttrs(&op, instructions, attribute,
                                 moduleTranslation);
  // Read by `connectPHINodes`.
  if (name == LLVMDialect::getPhiPositionsAttrName() ||
      name == LLVMDialect::getPhiFastmathAttrName())
    return success();
  return success();
}

namespace {
/// Implementation of the dialect interface that converts operations belonging
/// to the LLVM dialect to LLVM IR.
class LLVMDialectLLVMIRTranslationInterface
    : public LLVMTranslationDialectInterface {
public:
  using LLVMTranslationDialectInterface::LLVMTranslationDialectInterface;

  /// Translates the given operation to LLVM IR using the provided IR builder
  /// and saving the state in `moduleTranslation`.
  LogicalResult
  convertOperation(Operation *op, llvm::IRBuilderBase &builder,
                   LLVM::ModuleTranslation &moduleTranslation) const final {
    return convertOperationImpl(*op, builder, moduleTranslation);
  }

  /// Handle some metadata that is represented as a discardable attribute.
  LogicalResult
  amendOperation(Operation *op, ArrayRef<llvm::Instruction *> instructions,
                 NamedAttribute attribute,
                 LLVM::ModuleTranslation &moduleTranslation) const final {
    return amendOperationImpl(*op, instructions, attribute, moduleTranslation);
  }
};
} // namespace

void mlir::registerLLVMDialectTranslation(DialectRegistry &registry) {
  registry.insert<LLVM::LLVMDialect>();
  registry.addExtension(+[](MLIRContext *ctx, LLVM::LLVMDialect *dialect) {
    dialect->addInterfaces<LLVMDialectLLVMIRTranslationInterface>();
  });
}

void mlir::registerLLVMDialectTranslation(MLIRContext &context) {
  DialectRegistry registry;
  registerLLVMDialectTranslation(registry);
  context.appendDialectRegistry(registry);
}
