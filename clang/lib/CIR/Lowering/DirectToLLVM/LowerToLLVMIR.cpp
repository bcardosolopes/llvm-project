//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements lowering of CIR attributes and operations directly to
// LLVMIR.
//
//===----------------------------------------------------------------------===//

#include "CIROpenMPHelpers.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMAttrs.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Target/LLVMIR/LLVMTranslationInterface.h"
#include "mlir/Target/LLVMIR/ModuleTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/OpenMP/OpenMPToLLVMIRTranslation.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/SourceManager.h"
#include "clang/CIR/Dialect/OpenMP/RegisterOpenMPExtensions.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/Analysis/ObjCARCUtil.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Frontend/OpenMP/OMPIRBuilder.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/VirtualFileSystem.h"

using namespace llvm;

namespace cir {
namespace direct {

namespace {
struct GlobalConstantPtrAuthFixup {
  uint64_t ByteOffset;
  uint32_t Key;
  uint64_t Discriminator;
  bool AddressDiscriminated;
};

static const clang::FunctionDecl *
getFunctionDeclFromASTAttr(mlir::Attribute astAttr) {
  if (auto fnAST = mlir::dyn_cast_or_null<cir::ASTFunctionDeclAttr>(astAttr))
    return fnAST.getAst();
  if (auto methodAST =
          mlir::dyn_cast_or_null<cir::ASTCXXMethodDeclAttr>(astAttr))
    return methodAST.getAst();
  return nullptr;
}

static constexpr llvm::StringLiteral kPreserveAccessIndexDITypeAttrName =
    "cir.preserve_access_index.di_type";

static llvm::DINodeArray
collectLLVMBTFDeclTagAnnotations(LLVMContext &llvmCtx,
                                 mlir::ArrayAttr annotations) {
  llvm::SmallVector<llvm::Metadata *> result;
  if (annotations) {
    result.reserve(annotations.size());
    for (mlir::Attribute annot : annotations) {
      auto value = mlir::dyn_cast<mlir::StringAttr>(annot);
      if (!value)
        continue;
      result.push_back(llvm::MDTuple::get(
          llvmCtx,
          {llvm::MDString::get(llvmCtx, "btf_decl_tag"),
           llvm::MDString::get(llvmCtx, value.getValue())}));
    }
  }
  return llvm::DINodeArray(
      result.empty() ? nullptr : llvm::MDTuple::get(llvmCtx, result));
}

static llvm::DINodeArray
collectLLVMBTFTypeTagAnnotations(LLVMContext &llvmCtx,
                                 llvm::ArrayRef<std::string> annotations) {
  llvm::SmallVector<llvm::Metadata *> result;
  result.reserve(annotations.size());
  for (llvm::StringRef value : annotations) {
    result.push_back(llvm::MDTuple::get(
        llvmCtx,
        {llvm::MDString::get(llvmCtx, "btf_type_tag"),
         llvm::MDString::get(llvmCtx, value)}));
  }
  return llvm::DINodeArray(
      result.empty() ? nullptr : llvm::MDTuple::get(llvmCtx, result));
}

static llvm::DIDerivedType *
rebuildDIDerivedTypePreservingAttrs(llvm::LLVMContext &llvmCtx,
                                    const llvm::DIDerivedType *derived,
                                    llvm::DIType *newBase,
                                    llvm::DINodeArray annotations = nullptr) {
  if (!annotations)
    annotations = derived->getAnnotations();
  return llvm::DIDerivedType::get(
      llvmCtx, derived->getTag(), derived->getName(), derived->getFile(),
      derived->getLine(), derived->getScope(), newBase,
      derived->getRawSizeInBits(), derived->getAlignInBits(),
      derived->getRawOffsetInBits(), derived->getDWARFAddressSpace(),
      derived->getPtrAuthData(), derived->getFlags(), derived->getExtraData(),
      annotations);
}

static bool hasPreserveAccessIndexQualifierTag(llvm::DIType *type,
                                               unsigned tag) {
  auto *derived = llvm::dyn_cast_or_null<llvm::DIDerivedType>(type);
  return derived && derived->getTag() == tag;
}

static llvm::DIType *wrapPreserveAccessIndexQualifier(
    llvm::LLVMContext &llvmCtx, llvm::DIType *base, unsigned tag) {
  if (!base || hasPreserveAccessIndexQualifierTag(base, tag))
    return base;
  return llvm::DIDerivedType::get(
      llvmCtx, tag, "", /*File=*/nullptr, /*Line=*/0, /*Scope=*/nullptr, base,
      static_cast<uint64_t>(0), static_cast<uint32_t>(0),
      static_cast<uint64_t>(0), /*DWARFAddressSpace=*/std::nullopt,
      /*PtrAuthData=*/std::nullopt, llvm::DINode::FlagZero,
      /*ExtraData=*/nullptr);
}

static bool getPreserveAccessIndexBoolAt(mlir::ArrayAttr values, size_t index) {
  if (!values || index >= values.size())
    return false;
  if (auto value = mlir::dyn_cast<mlir::BoolAttr>(values[index]))
    return value.getValue();
  return false;
}

static llvm::DIType *
rebuildPreserveAccessIndexDIType(llvm::LLVMContext &llvmCtx,
                                 mlir::Operation *op, llvm::DIType *seedType) {
  if (!seedType)
    return nullptr;

  auto pointerTagLevels = op->getAttrOfType<mlir::ArrayAttr>(
      "cir.debug.pointer_chain_btf_type_tags");
  auto pointeeConstLevels = op->getAttrOfType<mlir::ArrayAttr>(
      "cir.debug.pointer_chain_pointee_const");
  auto pointeeVolatileLevels = op->getAttrOfType<mlir::ArrayAttr>(
      "cir.debug.pointer_chain_pointee_volatile");
  auto pointeeRestrictLevels = op->getAttrOfType<mlir::ArrayAttr>(
      "cir.debug.pointer_chain_pointee_restrict");

  auto rebuildPointerChain =
      [&](auto &&self, llvm::DIType *type, size_t level) -> llvm::DIType * {
    auto *derived = llvm::dyn_cast_or_null<llvm::DIDerivedType>(type);
    if (!derived || derived->getTag() != llvm::dwarf::DW_TAG_pointer_type)
      return type;

    llvm::DIType *newBase = self(self, derived->getBaseType(), level + 1);
    if (getPreserveAccessIndexBoolAt(pointeeRestrictLevels, level))
      newBase = wrapPreserveAccessIndexQualifier(
          llvmCtx, newBase, llvm::dwarf::DW_TAG_restrict_type);
    if (getPreserveAccessIndexBoolAt(pointeeVolatileLevels, level))
      newBase = wrapPreserveAccessIndexQualifier(
          llvmCtx, newBase, llvm::dwarf::DW_TAG_volatile_type);
    if (getPreserveAccessIndexBoolAt(pointeeConstLevels, level))
      newBase = wrapPreserveAccessIndexQualifier(
          llvmCtx, newBase, llvm::dwarf::DW_TAG_const_type);

    llvm::DINodeArray annotations = derived->getAnnotations();
    if (pointerTagLevels && level < pointerTagLevels.size()) {
      if (auto tagGroup =
              mlir::dyn_cast<mlir::ArrayAttr>(pointerTagLevels[level])) {
        llvm::SmallVector<std::string, 2> tags;
        tags.reserve(tagGroup.size());
        for (mlir::Attribute attr : tagGroup)
          if (auto value = mlir::dyn_cast<mlir::StringAttr>(attr))
            tags.push_back(value.getValue().str());
        if (!tags.empty())
          annotations = collectLLVMBTFTypeTagAnnotations(llvmCtx, tags);
      }
    }

    if (newBase == derived->getBaseType() &&
        annotations.get() == derived->getAnnotations().get())
      return type;
    return rebuildDIDerivedTypePreservingAttrs(llvmCtx, derived, newBase,
                                               annotations);
  };

  llvm::DIType *newType = rebuildPointerChain(rebuildPointerChain, seedType, 0);
  if (op->hasAttr("cir.debug.typedef_const"))
    newType = wrapPreserveAccessIndexQualifier(
        llvmCtx, newType, llvm::dwarf::DW_TAG_const_type);
  if (op->hasAttr("cir.debug.typedef_volatile"))
    newType = wrapPreserveAccessIndexQualifier(
        llvmCtx, newType, llvm::dwarf::DW_TAG_volatile_type);
  if (op->hasAttr("cir.debug.typedef_restrict"))
    newType = wrapPreserveAccessIndexQualifier(
        llvmCtx, newType, llvm::dwarf::DW_TAG_restrict_type);

  if (auto typedefNameAttr =
          op->getAttrOfType<mlir::StringAttr>("cir.debug.typedef_name")) {
    llvm::DIFile *typedefFile = nullptr;
    if (auto typedefFileAttr =
            op->getAttrOfType<mlir::StringAttr>("cir.debug.typedef_file")) {
      llvm::StringRef typedefPath = typedefFileAttr.getValue();
      if (!typedefPath.empty())
        typedefFile = llvm::DIFile::get(llvmCtx, typedefPath, "");
    }
    unsigned typedefLine = 0;
    if (auto typedefLineAttr =
            op->getAttrOfType<mlir::IntegerAttr>("cir.debug.typedef_line"))
      typedefLine = static_cast<unsigned>(typedefLineAttr.getInt());
    llvm::DINodeArray typedefAnnotations = collectLLVMBTFDeclTagAnnotations(
        llvmCtx,
        op->getAttrOfType<mlir::ArrayAttr>("cir.debug.typedef_btf_decl_tag"));
    newType = llvm::DIDerivedType::get(
        llvmCtx, llvm::dwarf::DW_TAG_typedef, typedefNameAttr.getValue(),
        typedefFile, typedefLine, /*Scope=*/nullptr, newType,
        static_cast<uint64_t>(0), static_cast<uint32_t>(0),
        static_cast<uint64_t>(0), /*DWARFAddressSpace=*/std::nullopt,
        /*PtrAuthData=*/std::nullopt, llvm::DINode::FlagZero,
        /*ExtraData=*/nullptr, typedefAnnotations);
  }

  if (op->hasAttr("cir.debug.qual_const"))
    newType = wrapPreserveAccessIndexQualifier(
        llvmCtx, newType, llvm::dwarf::DW_TAG_const_type);
  if (op->hasAttr("cir.debug.qual_volatile"))
    newType = wrapPreserveAccessIndexQualifier(
        llvmCtx, newType, llvm::dwarf::DW_TAG_volatile_type);
  if (op->hasAttr("cir.debug.qual_restrict"))
    newType = wrapPreserveAccessIndexQualifier(
        llvmCtx, newType, llvm::dwarf::DW_TAG_restrict_type);
  return newType;
}

static void clearPreserveAccessIndexDebugAttrs(mlir::Operation *op) {
  op->removeAttr(kPreserveAccessIndexDITypeAttrName);
  op->removeAttr("cir.debug.qual_const");
  op->removeAttr("cir.debug.qual_volatile");
  op->removeAttr("cir.debug.qual_restrict");
  op->removeAttr("cir.debug.typedef_name");
  op->removeAttr("cir.debug.typedef_btf_decl_tag");
  op->removeAttr("cir.debug.typedef_file");
  op->removeAttr("cir.debug.typedef_line");
  op->removeAttr("cir.debug.typedef_const");
  op->removeAttr("cir.debug.typedef_volatile");
  op->removeAttr("cir.debug.typedef_restrict");
  op->removeAttr("cir.debug.pointer_chain_btf_type_tags");
  op->removeAttr("cir.debug.pointer_chain_pointee_const");
  op->removeAttr("cir.debug.pointer_chain_pointee_volatile");
  op->removeAttr("cir.debug.pointer_chain_pointee_restrict");
}

static void emitOpenMPDeclareTargetIndirectFunction(
    mlir::LLVM::LLVMFuncOp funcOp, llvm::Function *llvmFunc,
    mlir::LLVM::ModuleTranslation &moduleTranslation) {
  llvm::OpenMPIRBuilder *ompBuilder = moduleTranslation.getOpenMPBuilder();
  if (!ompBuilder || !llvmFunc || llvmFunc->isDeclaration())
    return;

  const clang::FunctionDecl *funcDecl =
      getFunctionDeclFromASTAttr(funcOp->getAttr("ast"));
  if (!funcDecl)
    return;

  std::optional<clang::OMPDeclareTargetDeclAttr *> activeAttr =
      clang::OMPDeclareTargetDeclAttr::getActiveAttr(funcDecl);
  if (!activeAttr || !(*activeAttr)->getIndirect())
    return;

  if (ompBuilder->Config.isTargetDevice()) {
    if (auto declareTargetAttr = mlir::dyn_cast<mlir::omp::DeclareTargetAttr>(
            funcOp->getAttr("omp.declare_target"))) {
      if (auto deviceType = declareTargetAttr.getDeviceType();
          deviceType &&
          deviceType.getValue() == mlir::omp::DeclareTargetDeviceType::host)
        return;
    }
  }

  llvm::Module *llvmModule = moduleTranslation.getLLVMModule();
  auto vfs = llvm::vfs::getRealFileSystem();
  clang::SourceManager &sm = funcDecl->getASTContext().getSourceManager();
  clang::SourceLocation beginLoc = funcDecl->getCanonicalDecl()->getBeginLoc();
  auto fileInfoCallBack = [&]() -> std::tuple<std::string, uint64_t> {
    clang::PresumedLoc pLoc = sm.getPresumedLoc(beginLoc);
    if (!pLoc.isValid())
      return {"", 0};
    if (!vfs->exists(pLoc.getFilename()))
      pLoc = sm.getPresumedLoc(beginLoc, /*UseLineDirectives=*/false);
    if (!pLoc.isValid())
      return {"", 0};
    return {std::string(pLoc.getFilename()), pLoc.getLine()};
  };

  llvm::TargetRegionEntryInfo entryInfo = ompBuilder->getTargetEntryUniqueInfo(
      fileInfoCallBack, *vfs, funcDecl->getName());
  llvm::SmallString<128> entryName;
  ompBuilder->OffloadInfoManager.getTargetRegionEntryFnName(entryName,
                                                            entryInfo);

  llvm::GlobalValue *addr = llvmFunc;
  if (ompBuilder->Config.isTargetDevice()) {
    llvm::LLVMContext &llvmCtx = llvmModule->getContext();
    const llvm::DataLayout &dl = llvmModule->getDataLayout();
    llvm::PointerType *fnPtrTy =
        llvm::PointerType::get(llvmCtx, dl.getProgramAddressSpace());
    addr = new llvm::GlobalVariable(
        *llvmModule, fnPtrTy,
        /*isConstant=*/true, llvm::GlobalValue::ExternalLinkage, llvmFunc,
        entryName, /*InsertBefore=*/nullptr,
        llvm::GlobalValue::NotThreadLocal,
        dl.getDefaultGlobalsAddressSpace());
    addr->setVisibility(llvm::GlobalValue::ProtectedVisibility);
  }

  ompBuilder->OffloadInfoManager.registerDeviceGlobalVarEntryInfo(
      entryName, addr, llvmModule->getDataLayout().getPointerSize(),
      llvm::OffloadEntriesInfoManager::OMPTargetGlobalVarEntryIndirect,
      llvm::GlobalValue::WeakODRLinkage);
}

static llvm::MDNode *buildAllocTokenMetadata(mlir::ArrayAttr payload,
                                             llvm::LLVMContext &llvmCtx) {
  if (payload.empty())
    return llvm::MDNode::get(llvmCtx, {});
  if (payload.size() != 2)
    return nullptr;

  auto typeNameAttr = mlir::dyn_cast<mlir::StringAttr>(payload[0]);
  if (!typeNameAttr)
    return nullptr;

  bool containsPointer = false;
  if (auto boolAttr = mlir::dyn_cast<mlir::BoolAttr>(payload[1])) {
    containsPointer = boolAttr.getValue();
  } else if (auto intAttr = mlir::dyn_cast<mlir::IntegerAttr>(payload[1])) {
    containsPointer = intAttr.getInt() != 0;
  } else {
    return nullptr;
  }

  llvm::Metadata *metadata[] = {
      llvm::MDString::get(llvmCtx, typeNameAttr.getValue()),
      llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
          llvm::Type::getInt1Ty(llvmCtx), containsPointer))};
  return llvm::MDNode::get(llvmCtx, metadata);
}

static llvm::Constant *getNestedStorageAddress(llvm::Constant *baseAddress,
                                               llvm::Type *baseType,
                                               uint64_t index) {
  llvm::LLVMContext &ctx = baseAddress->getContext();
  llvm::Constant *zero =
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0);
  llvm::Constant *idx =
      llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), index);
  llvm::Constant *indices[] = {zero, idx};
  return llvm::ConstantExpr::getInBoundsGetElementPtr(baseType, baseAddress,
                                                      indices);
}

static llvm::Constant *applyGlobalConstantPtrAuthFixup(
    llvm::Constant *constant, llvm::Constant *storageAddress,
    const llvm::DataLayout &dataLayout,
    const GlobalConstantPtrAuthFixup &fixup, uint64_t byteOffset = 0) {
  if (byteOffset == fixup.ByteOffset && constant->getType()->isPointerTy()) {
    llvm::LLVMContext &ctx = constant->getContext();
    llvm::Constant *addressDiscriminator =
        fixup.AddressDiscriminated
            ? storageAddress
            : llvm::Constant::getNullValue(storageAddress->getType());
    return llvm::ConstantPtrAuth::get(
        constant,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), fixup.Key),
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx),
                               fixup.Discriminator),
        addressDiscriminator,
        llvm::Constant::getNullValue(storageAddress->getType()));
  }

  if (byteOffset == fixup.ByteOffset) {
    auto *ptrToInt = llvm::dyn_cast<llvm::ConstantExpr>(constant);
    if (ptrToInt && ptrToInt->getOpcode() == llvm::Instruction::PtrToInt &&
        ptrToInt->getOperand(0)->getType()->isPointerTy()) {
      llvm::LLVMContext &ctx = constant->getContext();
      llvm::Constant *ptrValue =
          llvm::cast<llvm::Constant>(ptrToInt->getOperand(0));
      llvm::Constant *addressDiscriminator =
          fixup.AddressDiscriminated
              ? storageAddress
              : llvm::Constant::getNullValue(storageAddress->getType());
      llvm::Constant *signedPtr = llvm::ConstantPtrAuth::get(
          ptrValue,
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), fixup.Key),
          llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx),
                                 fixup.Discriminator),
          addressDiscriminator,
          llvm::Constant::getNullValue(storageAddress->getType()));
      return llvm::ConstantExpr::getPtrToInt(signedPtr, constant->getType());
    }
  }

  if (auto *structTy = llvm::dyn_cast<llvm::StructType>(constant->getType())) {
    auto *layout = dataLayout.getStructLayout(structTy);
    SmallVector<llvm::Constant *, 8> fields;
    bool changed = false;
    fields.reserve(constant->getNumOperands());
    for (auto [index, operand] : llvm::enumerate(constant->operands())) {
      llvm::Constant *field = llvm::cast<llvm::Constant>(operand);
      uint64_t fieldOffset = byteOffset + layout->getElementOffset(index);
      uint64_t fieldSize = dataLayout.getTypeAllocSize(field->getType());
      if (fixup.ByteOffset >= fieldOffset &&
          fixup.ByteOffset < fieldOffset + fieldSize) {
        llvm::Constant *fieldAddr =
            getNestedStorageAddress(storageAddress, structTy, index);
        field = applyGlobalConstantPtrAuthFixup(field, fieldAddr, dataLayout,
                                                fixup, fieldOffset);
        changed = true;
      }
      fields.push_back(field);
    }
    return changed ? llvm::ConstantStruct::get(structTy, fields) : constant;
  }

  if (auto *arrayTy = llvm::dyn_cast<llvm::ArrayType>(constant->getType())) {
    uint64_t elementSize = dataLayout.getTypeAllocSize(arrayTy->getElementType());
    if (elementSize == 0 || fixup.ByteOffset < byteOffset)
      return constant;
    uint64_t elementIndex = (fixup.ByteOffset - byteOffset) / elementSize;
    if (elementIndex >= constant->getNumOperands())
      return constant;

    SmallVector<llvm::Constant *, 8> elements;
    elements.reserve(constant->getNumOperands());
    for (auto [index, operand] : llvm::enumerate(constant->operands())) {
      llvm::Constant *elt = llvm::cast<llvm::Constant>(operand);
      if (index == elementIndex) {
        llvm::Constant *eltAddr =
            getNestedStorageAddress(storageAddress, arrayTy, index);
        elt = applyGlobalConstantPtrAuthFixup(
            elt, eltAddr, dataLayout, fixup, byteOffset + index * elementSize);
      }
      elements.push_back(elt);
    }
    return llvm::ConstantArray::get(arrayTy, elements);
  }

  return constant;
}
} // namespace

static llvm::Constant *applyMemberFunctionPtrPtrauthAtPath(
    llvm::Constant *constant, llvm::ArrayRef<unsigned> path, uint64_t disc) {
  if (!constant)
    return nullptr;
  if (path.empty()) {
    if (auto *structC = llvm::dyn_cast<llvm::ConstantStruct>(constant)) {
      if (structC->getNumOperands() >= 1) {
        llvm::SmallVector<llvm::Constant *, 8> fields;
        fields.reserve(structC->getNumOperands());
        for (llvm::Value *operand : structC->operands())
          fields.push_back(llvm::cast<llvm::Constant>(operand));
        llvm::Constant *rewrittenPtr =
            applyMemberFunctionPtrPtrauthAtPath(fields[0], {}, disc);
        if (!rewrittenPtr)
          return nullptr;
        fields[0] = rewrittenPtr;
        return llvm::ConstantStruct::get(structC->getType(), fields);
      }
      return nullptr;
    }
    if (auto *arrayC = llvm::dyn_cast<llvm::ConstantArray>(constant)) {
      if (arrayC->getNumOperands() != 1)
        return nullptr;
      llvm::SmallVector<llvm::Constant *, 1> elts;
      elts.push_back(llvm::cast<llvm::Constant>(arrayC->getOperand(0)));
      llvm::Constant *rewrittenElt =
          applyMemberFunctionPtrPtrauthAtPath(elts[0], {}, disc);
      if (!rewrittenElt)
        return nullptr;
      elts[0] = rewrittenElt;
      return llvm::ConstantArray::get(arrayC->getType(), elts);
    }
  }
  if (path.empty()) {
    auto *ptrToInt = llvm::dyn_cast<llvm::ConstantExpr>(constant);
    if (!ptrToInt || ptrToInt->getOpcode() != llvm::Instruction::PtrToInt)
      return nullptr;

    llvm::Constant *ptrValue = ptrToInt->getOperand(0);
    auto *intToPtr = llvm::dyn_cast<llvm::ConstantExpr>(ptrValue);
    if (intToPtr && intToPtr->getOpcode() == llvm::Instruction::IntToPtr) {
      auto *addExpr =
          llvm::dyn_cast<llvm::ConstantExpr>(intToPtr->getOperand(0));
      if (!addExpr || addExpr->getOpcode() != llvm::Instruction::Add)
        return nullptr;
      auto *rhsDisc = llvm::dyn_cast<llvm::ConstantInt>(addExpr->getOperand(1));
      auto *nestedPtrToInt =
          llvm::dyn_cast<llvm::ConstantExpr>(addExpr->getOperand(0));
      if (!rhsDisc || !nestedPtrToInt ||
          nestedPtrToInt->getOpcode() != llvm::Instruction::PtrToInt)
        return nullptr;
      if (rhsDisc->getZExtValue() != disc)
        return nullptr;
      ptrValue = nestedPtrToInt->getOperand(0);
    }

    llvm::Type *ptrTy = ptrValue->getType();
    llvm::LLVMContext &ctx = constant->getContext();
    llvm::Constant *signedPtr = llvm::ConstantPtrAuth::get(
        ptrValue,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0),
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx), disc),
        llvm::Constant::getNullValue(ptrTy),
        llvm::Constant::getNullValue(ptrTy));
    return llvm::ConstantExpr::getPtrToInt(signedPtr, constant->getType());
  }

  unsigned index = path.front();
  auto *agg = llvm::dyn_cast<llvm::ConstantAggregate>(constant);
  if (!agg || index >= agg->getNumOperands())
    return nullptr;

  llvm::SmallVector<llvm::Constant *, 8> fields;
  fields.reserve(agg->getNumOperands());
  for (llvm::Value *operand : agg->operands())
    fields.push_back(llvm::cast<llvm::Constant>(operand));

  llvm::Constant *rewritten =
      applyMemberFunctionPtrPtrauthAtPath(fields[index], path.drop_front(), disc);
  if (!rewritten)
    return nullptr;
  fields[index] = rewritten;

  if (auto *structTy = llvm::dyn_cast<llvm::StructType>(constant->getType()))
    return llvm::ConstantStruct::get(structTy, fields);
  if (auto *arrayTy = llvm::dyn_cast<llvm::ArrayType>(constant->getType()))
    return llvm::ConstantArray::get(arrayTy, fields);
  return nullptr;
}

static llvm::Constant *applyNoCFIAtPath(llvm::Constant *constant,
                                        llvm::ArrayRef<unsigned> path) {
  if (!constant)
    return nullptr;

  if (path.empty()) {
    if (auto *gv = llvm::dyn_cast<llvm::GlobalValue>(constant))
      return llvm::NoCFIValue::get(gv);

    if (auto *ce = llvm::dyn_cast<llvm::ConstantExpr>(constant)) {
      llvm::Constant *operand =
          llvm::dyn_cast<llvm::Constant>(ce->getOperand(0));
      if (!operand)
        return nullptr;
      llvm::Constant *rewritten = applyNoCFIAtPath(operand, {});
      if (!rewritten)
        return nullptr;

      switch (ce->getOpcode()) {
      case llvm::Instruction::BitCast:
        return llvm::ConstantExpr::getBitCast(rewritten, ce->getType());
      case llvm::Instruction::AddrSpaceCast:
        return llvm::ConstantExpr::getAddrSpaceCast(rewritten, ce->getType());
      case llvm::Instruction::PtrToInt:
        return llvm::ConstantExpr::getPtrToInt(rewritten, ce->getType());
      default:
        return nullptr;
      }
    }
    return nullptr;
  }

  unsigned index = path.front();
  auto *agg = llvm::dyn_cast<llvm::ConstantAggregate>(constant);
  if (!agg || index >= agg->getNumOperands())
    return nullptr;

  llvm::SmallVector<llvm::Constant *, 8> fields;
  fields.reserve(agg->getNumOperands());
  for (llvm::Value *operand : agg->operands())
    fields.push_back(llvm::cast<llvm::Constant>(operand));

  llvm::Constant *rewritten =
      applyNoCFIAtPath(fields[index], path.drop_front());
  if (!rewritten)
    return nullptr;
  fields[index] = rewritten;

  if (auto *structTy = llvm::dyn_cast<llvm::StructType>(constant->getType()))
    return llvm::ConstantStruct::get(structTy, fields);
  if (auto *arrayTy = llvm::dyn_cast<llvm::ArrayType>(constant->getType()))
    return llvm::ConstantArray::get(arrayTy, fields);
  return nullptr;
}

namespace {
/// What the translation interface below remembers while it translates one
/// module. The values are LLVM metadata and globals of that module, so they
/// must not outlive its translation, and the interface is one object per
/// MLIRContext, shared by every module translated in it -- the pieces of a
/// partitioned lowering are translated at the same time, each on one thread
/// with its own LLVMContext. So the caches are per thread, and cleared before
/// each module (resetTranslationCaches).
struct TranslationCaches {
  llvm::DenseMap<mlir::LLVM::TBAATagAttr, llvm::MDNode *> tbaaTagCache;
  llvm::DenseMap<mlir::LLVM::TBAATypeDescriptorAttr, llvm::MDNode *>
      tbaaTypeCache;
  llvm::DenseMap<mlir::LLVM::TBAARootAttr, llvm::MDNode *> tbaaRootCache;
  llvm::StringMap<llvm::GlobalVariable *> sanitizerSourceFileGlobals;
  llvm::StringMap<llvm::MDNode *> hiddenTypeIdCache;
  llvm::StringMap<llvm::GlobalVariable *> ubsanTypeDescriptorGVCache;
};

TranslationCaches &translationCaches() {
  static thread_local TranslationCaches caches;
  return caches;
}
} // namespace

void resetTranslationCaches() {
  translationCaches() = TranslationCaches();
}

/// Implementation of the dialect interface that converts CIR attributes to LLVM
/// IR metadata.
class CIRDialectLLVMIRTranslationInterface
    : public mlir::LLVMTranslationDialectInterface {
public:
  using LLVMTranslationDialectInterface::LLVMTranslationDialectInterface;

private:
  llvm::MDNode *getOrCreateTBAAMetadataNode(mlir::LLVM::TBAANodeAttr attr,
                                            llvm::LLVMContext &ctx) const {
    if (auto root = mlir::dyn_cast<mlir::LLVM::TBAARootAttr>(attr)) {
      if (auto it = translationCaches().tbaaRootCache.find(root); it != translationCaches().tbaaRootCache.end())
        return it->second;
      llvm::MDNode *node = nullptr;
      if (auto id = root.getId()) {
        node = llvm::MDNode::get(ctx, llvm::MDString::get(ctx, id));
      } else {
        auto selfRef = llvm::MDNode::getTemporary(ctx, {});
        node = llvm::MDNode::get(ctx, {selfRef.get()});
        node->replaceOperandWith(0, node);
      }
      translationCaches().tbaaRootCache[root] = node;
      return node;
    }
    if (auto type = mlir::dyn_cast<mlir::LLVM::TBAATypeDescriptorAttr>(attr)) {
      if (auto it = translationCaches().tbaaTypeCache.find(type); it != translationCaches().tbaaTypeCache.end())
        return it->second;
      llvm::SmallVector<llvm::Metadata *, 8> ops;
      ops.push_back(llvm::MDString::get(ctx, type.getId()));
      auto *i64Ty = llvm::Type::getInt64Ty(ctx);
      for (auto member : type.getMembers()) {
        if (auto *child = getOrCreateTBAAMetadataNode(member.getTypeDesc(), ctx))
          ops.push_back(child);
        else
          return nullptr;
        ops.push_back(llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(i64Ty, member.getOffset())));
      }
      auto *node = llvm::MDNode::get(ctx, ops);
      translationCaches().tbaaTypeCache[type] = node;
      return node;
    }
    return nullptr;
  }

  llvm::MDNode *getOrCreateTBAATagMetadataNode(mlir::LLVM::TBAATagAttr tag,
                                               llvm::LLVMContext &ctx) const {
    if (auto it = translationCaches().tbaaTagCache.find(tag); it != translationCaches().tbaaTagCache.end())
      return it->second;
    llvm::SmallVector<llvm::Metadata *, 4> ops;
    auto *base = getOrCreateTBAAMetadataNode(tag.getBaseType(), ctx);
    auto *access = getOrCreateTBAAMetadataNode(tag.getAccessType(), ctx);
    if (!base || !access)
      return nullptr;
    ops.push_back(base);
    ops.push_back(access);
    auto *i64Ty = llvm::Type::getInt64Ty(ctx);
    ops.push_back(llvm::ConstantAsMetadata::get(
        llvm::ConstantInt::get(i64Ty, tag.getOffset())));
    if (tag.getConstant())
      ops.push_back(llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(i64Ty, 1)));
    auto *node = llvm::MDNode::get(ctx, ops);
    translationCaches().tbaaTagCache[tag] = node;
    return node;
  }


  llvm::GlobalVariable *getOrCreateUBSanTypeDescriptorGlobal(
      llvm::Module &mod, llvm::StringRef typeName, uint16_t typeKind,
      uint16_t typeInfo) const {
    std::string cacheKey;
    cacheKey.reserve(typeName.size() + 5);
    cacheKey.append(typeName.begin(), typeName.end());
    cacheKey.push_back('\0');
    cacheKey.push_back(static_cast<char>(typeKind & 0xFF));
    cacheKey.push_back(static_cast<char>((typeKind >> 8) & 0xFF));
    cacheKey.push_back(static_cast<char>(typeInfo & 0xFF));
    cacheKey.push_back(static_cast<char>((typeInfo >> 8) & 0xFF));

    if (auto it = translationCaches().ubsanTypeDescriptorGVCache.find(cacheKey);
        it != translationCaches().ubsanTypeDescriptorGVCache.end())
      return it->second;

    llvm::LLVMContext &ctx = mod.getContext();
    auto *int16Ty = llvm::Type::getInt16Ty(ctx);
    llvm::Constant *typeDescData[] = {
        llvm::ConstantInt::get(int16Ty, typeKind),
        llvm::ConstantInt::get(int16Ty, typeInfo),
        llvm::ConstantDataArray::getString(ctx, typeName)};
    auto *typeDescStruct = llvm::ConstantStruct::getAnon(typeDescData);
    auto *typeDescGV = new llvm::GlobalVariable(
        mod, typeDescStruct->getType(), true,
        llvm::GlobalVariable::PrivateLinkage, typeDescStruct);
    typeDescGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    translationCaches().ubsanTypeDescriptorGVCache.insert({cacheKey, typeDescGV});
    return typeDescGV;
  }

  static llvm::MDNode *
  getAsmSrcLocMD(mlir::ArrayAttr locsAttr, llvm::LLVMContext &ctx) {
    SmallVector<llvm::Metadata *, 8> locMDs;
    for (mlir::Attribute a : locsAttr) {
      auto intAttr = mlir::dyn_cast<mlir::IntegerAttr>(a);
      if (!intAttr)
        continue;
      auto *loc = llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx),
                                         intAttr.getInt());
      locMDs.push_back(llvm::ConstantAsMetadata::get(loc));
    }
    if (locMDs.empty())
      return nullptr;
    return llvm::MDNode::get(ctx, locMDs);
  }

  // Match OGCG's GetAddrOfConstantCString reuse for UBSan source filenames so
  // all checks in one source file share the same private @.src global.
  llvm::Constant *getOrCreateUBSanSourceFileGlobal(
      llvm::Module &module, llvm::StringRef sourceFile) const {
    auto *ptrTy = llvm::PointerType::getUnqual(module.getContext());
    if (sourceFile.empty())
      return llvm::Constant::getNullValue(ptrTy);

    llvm::GlobalVariable *&entry =
        translationCaches().sanitizerSourceFileGlobals[sourceFile];
    if (entry)
      return entry;

    for (llvm::GlobalVariable &gv : module.globals()) {
      if (!gv.hasInitializer())
        continue;
      // Never reuse a global that lives in an explicit section. Annotation
      // strings (from __attribute__((annotate))) are emitted into
      // "llvm.metadata" and can have the same filename text as this UBSan
      // source-location string. Reusing one would make the emitted UBSan
      // descriptor reference a "llvm.metadata" symbol that the AsmPrinter
      // drops, yielding "Undefined temporary symbol .L.str.annot" at assembly.
      if (gv.hasSection())
        continue;
      auto *init = llvm::dyn_cast<llvm::ConstantDataArray>(gv.getInitializer());
      if (!init || !init->isCString())
        continue;
      if (init->getAsCString() != sourceFile)
        continue;
      entry = &gv;
      return &gv;
    }

    auto *filenameInit =
        llvm::ConstantDataArray::getString(module.getContext(), sourceFile);
    auto *gv = new llvm::GlobalVariable(
        module, filenameInit->getType(), true,
        llvm::GlobalVariable::PrivateLinkage, filenameInit, ".src");
    gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    entry = gv;
    return gv;
  }

  static mlir::LogicalResult amendFPConstraintAttrs(
      mlir::Operation *op, llvm::ArrayRef<llvm::Instruction *> instructions,
      mlir::LLVM::ModuleTranslation &moduleTranslation) {
    auto fpExceptAttr =
        op->getAttrOfType<cir::FPExceptionBehaviorAttr>("cir.fp_exception");
    if (!fpExceptAttr)
      return mlir::success();

    llvm::fp::ExceptionBehavior eb;
    switch (fpExceptAttr.getValue()) {
    case cir::FPExceptionBehavior::Ignore:
      eb = llvm::fp::ebIgnore;
      break;
    case cir::FPExceptionBehavior::MayTrap:
      eb = llvm::fp::ebMayTrap;
      break;
    case cir::FPExceptionBehavior::Strict:
      eb = llvm::fp::ebStrict;
      break;
    }

    llvm::RoundingMode rm = llvm::RoundingMode::NearestTiesToEven;
    if (auto rmAttr =
            op->getAttrOfType<cir::RoundingModeAttr>("cir.rounding_mode")) {
      switch (rmAttr.getValue()) {
      case cir::RoundingMode::TowardZero:
        rm = llvm::RoundingMode::TowardZero;
        break;
      case cir::RoundingMode::NearestTiesToEven:
        rm = llvm::RoundingMode::NearestTiesToEven;
        break;
      case cir::RoundingMode::TowardPositive:
        rm = llvm::RoundingMode::TowardPositive;
        break;
      case cir::RoundingMode::TowardNegative:
        rm = llvm::RoundingMode::TowardNegative;
        break;
      case cir::RoundingMode::NearestTiesToAway:
        rm = llvm::RoundingMode::NearestTiesToAway;
        break;
      case cir::RoundingMode::Dynamic:
        rm = llvm::RoundingMode::Dynamic;
        break;
      }
    }

    llvm::SmallVector<llvm::Instruction *, 4> toErase;
    llvm::SmallPtrSet<llvm::Function *, 8> maybeDeadDecls;
    auto maybeSetNoSanitize = [&](llvm::Instruction *inst) {
      if (op->hasAttr("cir.nosanitize"))
        inst->setNoSanitizeMetadata();
    };
    for (auto *inst : instructions) {
      if (auto *binOp = llvm::dyn_cast<llvm::BinaryOperator>(inst)) {
        llvm::Intrinsic::ID iid;
        switch (binOp->getOpcode()) {
        case llvm::Instruction::FAdd:
          iid = llvm::Intrinsic::experimental_constrained_fadd;
          break;
        case llvm::Instruction::FSub:
          iid = llvm::Intrinsic::experimental_constrained_fsub;
          break;
        case llvm::Instruction::FMul:
          iid = llvm::Intrinsic::experimental_constrained_fmul;
          break;
        case llvm::Instruction::FDiv:
          iid = llvm::Intrinsic::experimental_constrained_fdiv;
          break;
        case llvm::Instruction::FRem:
          iid = llvm::Intrinsic::experimental_constrained_frem;
          break;
        default:
          continue;
        }

        llvm::IRBuilder<> irb(binOp);
        irb.setDefaultConstrainedExcept(eb);
        irb.setDefaultConstrainedRounding(rm);
        auto *constrained =
            irb.CreateConstrainedFPBinOp(iid, binOp->getOperand(0),
                                         binOp->getOperand(1));
        constrained->copyFastMathFlags(binOp);
        maybeSetNoSanitize(constrained);
        binOp->replaceAllUsesWith(constrained);
        if (op->getNumResults() > 0)
          moduleTranslation.remapValue(op->getResult(0), constrained);
        toErase.push_back(binOp);
        continue;
      }

      if (auto *callInst = llvm::dyn_cast<llvm::CallInst>(inst)) {
        auto *calledFn = callInst->getCalledFunction();
        if (calledFn) {
          llvm::Intrinsic::ID constrainedIID = llvm::Intrinsic::not_intrinsic;
          switch (calledFn->getIntrinsicID()) {
          case llvm::Intrinsic::fmuladd:
            constrainedIID = llvm::Intrinsic::experimental_constrained_fmuladd;
            break;
          case llvm::Intrinsic::sqrt:
            constrainedIID = llvm::Intrinsic::experimental_constrained_sqrt;
            break;
          case llvm::Intrinsic::pow:
            constrainedIID = llvm::Intrinsic::experimental_constrained_pow;
            break;
          case llvm::Intrinsic::sin:
            constrainedIID = llvm::Intrinsic::experimental_constrained_sin;
            break;
          case llvm::Intrinsic::cos:
            constrainedIID = llvm::Intrinsic::experimental_constrained_cos;
            break;
          case llvm::Intrinsic::tan:
            constrainedIID = llvm::Intrinsic::experimental_constrained_tan;
            break;
          case llvm::Intrinsic::exp:
            constrainedIID = llvm::Intrinsic::experimental_constrained_exp;
            break;
          case llvm::Intrinsic::exp2:
            constrainedIID = llvm::Intrinsic::experimental_constrained_exp2;
            break;
          case llvm::Intrinsic::log:
            constrainedIID = llvm::Intrinsic::experimental_constrained_log;
            break;
          case llvm::Intrinsic::log2:
            constrainedIID = llvm::Intrinsic::experimental_constrained_log2;
            break;
          case llvm::Intrinsic::log10:
            constrainedIID = llvm::Intrinsic::experimental_constrained_log10;
            break;
          case llvm::Intrinsic::rint:
            constrainedIID = llvm::Intrinsic::experimental_constrained_rint;
            break;
          case llvm::Intrinsic::nearbyint:
            constrainedIID =
                llvm::Intrinsic::experimental_constrained_nearbyint;
            break;
          case llvm::Intrinsic::ceil:
            constrainedIID = llvm::Intrinsic::experimental_constrained_ceil;
            break;
          case llvm::Intrinsic::floor:
            constrainedIID = llvm::Intrinsic::experimental_constrained_floor;
            break;
          case llvm::Intrinsic::round:
            constrainedIID = llvm::Intrinsic::experimental_constrained_round;
            break;
          case llvm::Intrinsic::roundeven:
            constrainedIID =
                llvm::Intrinsic::experimental_constrained_roundeven;
            break;
          case llvm::Intrinsic::trunc:
            constrainedIID = llvm::Intrinsic::experimental_constrained_trunc;
            break;
          case llvm::Intrinsic::fma:
            constrainedIID = llvm::Intrinsic::experimental_constrained_fma;
            break;
          case llvm::Intrinsic::maxnum:
            constrainedIID = llvm::Intrinsic::experimental_constrained_maxnum;
            break;
          case llvm::Intrinsic::minnum:
            constrainedIID = llvm::Intrinsic::experimental_constrained_minnum;
            break;
          case llvm::Intrinsic::acos:
            constrainedIID = llvm::Intrinsic::experimental_constrained_acos;
            break;
          case llvm::Intrinsic::asin:
            constrainedIID = llvm::Intrinsic::experimental_constrained_asin;
            break;
          case llvm::Intrinsic::atan:
            constrainedIID = llvm::Intrinsic::experimental_constrained_atan;
            break;
          case llvm::Intrinsic::atan2:
            constrainedIID = llvm::Intrinsic::experimental_constrained_atan2;
            break;
          case llvm::Intrinsic::cosh:
            constrainedIID = llvm::Intrinsic::experimental_constrained_cosh;
            break;
          case llvm::Intrinsic::sinh:
            constrainedIID = llvm::Intrinsic::experimental_constrained_sinh;
            break;
          case llvm::Intrinsic::tanh:
            constrainedIID = llvm::Intrinsic::experimental_constrained_tanh;
            break;
          case llvm::Intrinsic::lrint:
            constrainedIID = llvm::Intrinsic::experimental_constrained_lrint;
            break;
          case llvm::Intrinsic::llrint:
            constrainedIID = llvm::Intrinsic::experimental_constrained_llrint;
            break;
          case llvm::Intrinsic::lround:
            constrainedIID = llvm::Intrinsic::experimental_constrained_lround;
            break;
          case llvm::Intrinsic::llround:
            constrainedIID = llvm::Intrinsic::experimental_constrained_llround;
            break;
          case llvm::Intrinsic::ldexp:
            constrainedIID = llvm::Intrinsic::experimental_constrained_ldexp;
            break;
          case llvm::Intrinsic::powi:
            constrainedIID = llvm::Intrinsic::experimental_constrained_powi;
            break;
          default:
            break;
          }
          if (constrainedIID != llvm::Intrinsic::not_intrinsic) {
            llvm::IRBuilder<> irb(callInst);
            irb.setDefaultConstrainedExcept(eb);
            irb.setDefaultConstrainedRounding(rm);
            llvm::SmallVector<llvm::Value *, 4> args;
            for (unsigned i = 0; i < callInst->arg_size(); ++i)
              args.push_back(callInst->getArgOperand(i));
            llvm::SmallVector<llvm::Type *, 2> overloadTys;
            switch (constrainedIID) {
            case llvm::Intrinsic::experimental_constrained_lrint:
            case llvm::Intrinsic::experimental_constrained_llrint:
            case llvm::Intrinsic::experimental_constrained_lround:
            case llvm::Intrinsic::experimental_constrained_llround:
              overloadTys = {callInst->getType(), callInst->getArgOperand(0)->getType()};
              break;
            case llvm::Intrinsic::experimental_constrained_ldexp:
              overloadTys = {callInst->getArgOperand(0)->getType(),
                             callInst->getArgOperand(1)->getType()};
              break;
            default:
              overloadTys = {callInst->getArgOperand(0)->getType()};
              break;
            }
            auto *constrainedFn = llvm::Intrinsic::getOrInsertDeclaration(
                calledFn->getParent(), constrainedIID, overloadTys);
            auto *constrained = irb.CreateConstrainedFPCall(constrainedFn, args);
            maybeSetNoSanitize(constrained);
            callInst->replaceAllUsesWith(constrained);
            if (op->getNumResults() > 0)
              moduleTranslation.remapValue(op->getResult(0), constrained);
            if (calledFn->isDeclaration() && calledFn->isIntrinsic())
              maybeDeadDecls.insert(calledFn);
            toErase.push_back(callInst);
            continue;
          }
        }
      }

      if (auto *fcmpOp = llvm::dyn_cast<llvm::FCmpInst>(inst)) {
        llvm::IRBuilder<> irb(fcmpOp);
        irb.setDefaultConstrainedExcept(eb);
        irb.setDefaultConstrainedRounding(rm);
        llvm::Intrinsic::ID cmpIID =
            op->hasAttr("cir.fp_signaling_compare")
                ? llvm::Intrinsic::experimental_constrained_fcmps
                : llvm::Intrinsic::experimental_constrained_fcmp;
        auto *constrained = irb.CreateConstrainedFPCmp(
            cmpIID, fcmpOp->getPredicate(), fcmpOp->getOperand(0),
            fcmpOp->getOperand(1));
        maybeSetNoSanitize(constrained);
        fcmpOp->replaceAllUsesWith(constrained);
        if (op->getNumResults() > 0)
          moduleTranslation.remapValue(op->getResult(0), constrained);
        toErase.push_back(fcmpOp);
        continue;
      }

      if (auto *binOp = llvm::dyn_cast<llvm::BinaryOperator>(inst)) {
        llvm::Intrinsic::ID binIID = llvm::Intrinsic::not_intrinsic;
        switch (binOp->getOpcode()) {
        case llvm::Instruction::FRem:
          binIID = llvm::Intrinsic::experimental_constrained_frem;
          break;
        case llvm::Instruction::FMul:
          binIID = llvm::Intrinsic::experimental_constrained_fmul;
          break;
        default:
          break;
        }
        if (binIID != llvm::Intrinsic::not_intrinsic) {
          llvm::IRBuilder<> irb(binOp);
          irb.setDefaultConstrainedExcept(eb);
          irb.setDefaultConstrainedRounding(rm);
          auto *constrainedFn = llvm::Intrinsic::getOrInsertDeclaration(
              binOp->getModule(), binIID, {binOp->getType()});
          auto *constrained = irb.CreateConstrainedFPCall(
              constrainedFn, {binOp->getOperand(0), binOp->getOperand(1)});
          maybeSetNoSanitize(constrained);
          binOp->replaceAllUsesWith(constrained);
          if (op->getNumResults() > 0)
            moduleTranslation.remapValue(op->getResult(0), constrained);
          toErase.push_back(binOp);
          continue;
        }
      }

      auto *castOp = llvm::dyn_cast<llvm::CastInst>(inst);
      if (!castOp)
        continue;

      llvm::Intrinsic::ID iid;
      switch (castOp->getOpcode()) {
      case llvm::Instruction::FPToSI:
        iid = llvm::Intrinsic::experimental_constrained_fptosi;
        break;
      case llvm::Instruction::FPToUI:
        iid = llvm::Intrinsic::experimental_constrained_fptoui;
        break;
      case llvm::Instruction::SIToFP:
        iid = llvm::Intrinsic::experimental_constrained_sitofp;
        break;
      case llvm::Instruction::UIToFP:
        iid = llvm::Intrinsic::experimental_constrained_uitofp;
        break;
      case llvm::Instruction::FPTrunc:
        iid = llvm::Intrinsic::experimental_constrained_fptrunc;
        break;
      case llvm::Instruction::FPExt:
        iid = llvm::Intrinsic::experimental_constrained_fpext;
        break;
      default:
        continue;
      }

      llvm::IRBuilder<> irb(castOp);
      irb.setDefaultConstrainedExcept(eb);
      irb.setDefaultConstrainedRounding(rm);
      auto *constrained =
          irb.CreateConstrainedFPCast(iid, castOp->getOperand(0),
                                      castOp->getType());
      if (llvm::isa<llvm::FPMathOperator>(castOp))
        constrained->copyFastMathFlags(castOp);
      maybeSetNoSanitize(constrained);
      castOp->replaceAllUsesWith(constrained);
      if (op->getNumResults() > 0)
        moduleTranslation.remapValue(op->getResult(0), constrained);
      toErase.push_back(castOp);
    }
    for (auto *deadInst : toErase)
      deadInst->eraseFromParent();
    for (llvm::Function *deadDecl : maybeDeadDecls)
      if (deadDecl->isDeclaration() && deadDecl->use_empty())
        deadDecl->eraseFromParent();
    op->removeAttr("cir.fp_signaling_compare");
    op->removeAttr("cir.fp_exception");
    op->removeAttr("cir.rounding_mode");
    return mlir::success();
  }

public:

  /// Translates the given operation to LLVM IR using the provided IR builder
  /// and saving the state in `moduleTranslation`.
  mlir::LogicalResult convertOperation(
      mlir::Operation *op, llvm::IRBuilderBase &builder,
      mlir::LLVM::ModuleTranslation &moduleTranslation) const final {

    if (auto asmGotoOp = dyn_cast<cir::InlineAsmGotoOp>(op))
      return convertInlineAsmGotoOp(asmGotoOp, builder, moduleTranslation);

    // CIR-side translation hook for `cir.omp.scope_reduction`. Calls
    // `OpenMPIRBuilder::createReductions` directly via the duplicated
    // helpers in `CIROpenMPHelpers.{h,cpp}`. This replaces the path
    // through the upstream `omp.scope_reduction` op + its
    // `convertOmpScopeReduction` translator (deleted in stage 2.3).
    if (auto scopeReductionOp = dyn_cast<cir::OmpScopeReductionOp>(op))
      return convertCIROmpScopeReductionOp(scopeReductionOp, builder,
                                           moduleTranslation);

    if (auto cirOp = llvm::dyn_cast<mlir::LLVM::ZeroOp>(op))
      moduleTranslation.mapValue(cirOp.getResult()) =
          llvm::Constant::getNullValue(
              moduleTranslation.convertType(cirOp.getType()));

    return mlir::success();
  }

  /// Translate `cir.omp.scope_reduction` directly to OpenMPIRBuilder
  /// `createReductions`. Mirror of upstream `convertOmpScopeReduction`
  /// (`mlir/lib/Target/LLVMIR/Dialect/OpenMP/OpenMPToLLVMIRTranslation.cpp`)
  /// re-expressed against the CIR-private op, using the helpers in
  /// `CIROpenMPHelpers.{h,cpp}`.
  mlir::LogicalResult convertCIROmpScopeReductionOp(
      cir::OmpScopeReductionOp op, llvm::IRBuilderBase &builder,
      mlir::LLVM::ModuleTranslation &moduleTranslation) const {
    using namespace cir::direct::openmp_helpers;

    if (failed(cirCheckScopeReductionImplementationStatus(*op)))
      return mlir::failure();

    if (op.getNumReductionVars() == 0)
      return mlir::success();

    if (op.getPrivateReductionVars().size() != op.getNumReductionVars())
      return op.emitOpError()
             << "expected one private reduction variable per reduction variable";

    llvm::ArrayRef<bool> isByRef = cirGetIsByRef(op.getReductionByref());
    assert(isByRef.size() == op.getNumReductionVars());

    llvm::SmallVector<mlir::omp::DeclareReductionOp> reductionDecls;
    cirCollectReductionDecls(op, reductionDecls);

    llvm::SmallVector<llvm::Value *> privateReductionVariables;
    privateReductionVariables.reserve(op.getPrivateReductionVars().size());
    for (mlir::Value privateReductionVar : op.getPrivateReductionVars())
      privateReductionVariables.push_back(
          moduleTranslation.lookupValue(privateReductionVar));

    llvm::SmallVector<OwningReductionGen> owningReductionGens;
    llvm::SmallVector<OwningAtomicReductionGen> owningAtomicReductionGens;
    llvm::SmallVector<OwningDataPtrPtrReductionGen>
        owningReductionGenRefDataPtrGens;
    llvm::SmallVector<llvm::OpenMPIRBuilder::ReductionInfo, 2> reductionInfos;
    cirCollectReductionInfo(op, builder, moduleTranslation, reductionDecls,
                            owningReductionGens, owningAtomicReductionGens,
                            owningReductionGenRefDataPtrGens,
                            privateReductionVariables, reductionInfos,
                            isByRef);

    llvm::OpenMPIRBuilder::InsertPointTy allocaIP =
        cirFindAllocaInsertPoint(builder, moduleTranslation);
    llvm::UnreachableInst *tempTerminator = builder.CreateUnreachable();
    builder.SetInsertPoint(tempTerminator);
    llvm::OpenMPIRBuilder::InsertPointOrErrorTy contInsertPoint =
        moduleTranslation.getOpenMPBuilder()->createReductions(
            builder.saveIP(), allocaIP, reductionInfos, isByRef,
            op.getNowait(),
            /*isTeamsReduction=*/false);

    mlir::LogicalResult res = cirHandleError(contInsertPoint, *op);
    tempTerminator->eraseFromParent();
    if (failed(res))
      return mlir::failure();

    if (!contInsertPoint->getBlock())
      return op.emitOpError() << "failed to convert reductions";

    builder.restoreIP(*contInsertPoint);
    return mlir::success();
  }

  /// Amend LLVM IR operations with CIR dialect attributes.
  mlir::LogicalResult
  amendOperation(mlir::Operation *op,
                 llvm::ArrayRef<llvm::Instruction *> instructions,
                 mlir::NamedAttribute attribute,
                 mlir::LLVM::ModuleTranslation &moduleTranslation) const final {
    // Handle function-level attributes that need LLVM IR metadata.
    if (auto func = llvm::dyn_cast<mlir::LLVM::LLVMFuncOp>(op)) {
      llvm::Function *llvmFunc =
          moduleTranslation.lookupFunction(func.getName());
      if (attribute.getName() == "cir.omp.declare_target_indirect") {
        emitOpenMPDeclareTargetIndirectFunction(func, llvmFunc,
                                                moduleTranslation);
        func->removeAttr(attribute.getName());
        return mlir::success();
      }
      if (attribute.getName() == "cir.dll_storage_class") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
          if (strAttr.getValue() == "dllexport")
            llvmFunc->setDLLStorageClass(
                llvm::GlobalValue::DLLExportStorageClass);
          else if (strAttr.getValue() == "dllimport")
            llvmFunc->setDLLStorageClass(
                llvm::GlobalValue::DLLImportStorageClass);
        }
        func->removeAttr(attribute.getName());
        return mlir::success();
      }
      // Attach !type metadata entries to functions.
      if (attribute.getName() == "cir.func.type_metadata") {
        if (auto arrAttr =
                mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
          auto *i64Ty = llvm::Type::getInt64Ty(llvmFunc->getContext());
          for (mlir::Attribute item : arrAttr) {
            auto pair = mlir::dyn_cast<mlir::ArrayAttr>(item);
            if (!pair || pair.size() < 2)
              continue;
            auto offAttr = mlir::dyn_cast<mlir::IntegerAttr>(pair[0]);
            if (!offAttr)
              continue;

            llvm::Metadata *md = nullptr;
            if (auto typeIdStr = mlir::dyn_cast<mlir::StringAttr>(pair[1])) {
              bool hiddenTypeId = false;
              if (pair.size() >= 3)
                if (auto kindAttr = mlir::dyn_cast<mlir::StringAttr>(pair[2]))
                  hiddenTypeId = kindAttr.getValue() == "hidden";
              if (hiddenTypeId) {
                auto &cached = translationCaches().hiddenTypeIdCache[typeIdStr.getValue().str()];
                if (!cached)
                  cached =
                      llvm::MDNode::getDistinct(llvmFunc->getContext(), {});
                md = cached;
              } else {
                md = llvm::MDString::get(llvmFunc->getContext(),
                                         typeIdStr.getValue());
              }
            } else if (auto typeIdInt =
                           mlir::dyn_cast<mlir::IntegerAttr>(pair[1])) {
              md = llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                  i64Ty, static_cast<uint64_t>(typeIdInt.getInt())));
            }

            if (md)
              llvmFunc->addTypeMetadata(offAttr.getInt(), md);
          }
        }
        func->removeAttr(attribute.getName());
        return mlir::success();
      }
      // Attach call-graph-section !type metadata candidates, then let the
      // module finalization step drop non-viable local indirect targets once
      // full use information is available.
      if (attribute.getName() == "cir.func.call_graph_type_metadata") {
        if (auto arrAttr =
                mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
          SmallVector<llvm::Metadata *, 4> callGraphTypeIDs;
          for (mlir::Attribute item : arrAttr) {
            auto pair = mlir::dyn_cast<mlir::ArrayAttr>(item);
            if (!pair || pair.size() != 2)
              continue;
            auto offAttr = mlir::dyn_cast<mlir::IntegerAttr>(pair[0]);
            auto typeIdStr = mlir::dyn_cast<mlir::StringAttr>(pair[1]);
            if (!offAttr || !typeIdStr)
              continue;

            llvm::Metadata *typeID =
                llvm::MDString::get(llvmFunc->getContext(), typeIdStr.getValue());
            llvmFunc->addTypeMetadata(offAttr.getInt(), typeID);
            callGraphTypeIDs.push_back(typeID);
          }
          if (!callGraphTypeIDs.empty()) {
            unsigned kind = llvmFunc->getContext().getMDKindID(
                "cir.call_graph_section.type_metadata");
            llvmFunc->setMetadata(
                kind, llvm::MDNode::get(llvmFunc->getContext(), callGraphTypeIDs));
          }
        }
        func->removeAttr(attribute.getName());
        return mlir::success();
      }
      // Convert cir.kcfi_type attribute to !kcfi_type metadata on LLVM function.
      // Matches OGCG CodeGenModule::setKCFIType (CodeGenModule.cpp:3206-3218).
      if (attribute.getName() == "cir.kcfi_type") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue())) {
          auto &ctx = llvmFunc->getContext();
          llvm::MDBuilder MDB(ctx);
          uint32_t typeId = static_cast<uint32_t>(intAttr.getInt());
          llvmFunc->setMetadata(
              llvm::LLVMContext::MD_kcfi_type,
              llvm::MDNode::get(ctx, MDB.createConstant(
                                         llvm::ConstantInt::get(
                                             llvm::Type::getInt32Ty(ctx),
                                             typeId))));
        }
        func->removeAttr(attribute.getName());
        return mlir::success();
      }
      if (attribute.getName() == "cir.func_sanitize") {
        if (auto arrAttr =
                mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue());
            arrAttr && arrAttr.size() == 2) {
          auto sigAttr = mlir::dyn_cast<mlir::IntegerAttr>(arrAttr[0]);
          auto hashAttr = mlir::dyn_cast<mlir::IntegerAttr>(arrAttr[1]);
          if (sigAttr && hashAttr) {
            auto &ctx = llvmFunc->getContext();
            llvm::MDBuilder MDB(ctx);
            auto *i32Ty = llvm::Type::getInt32Ty(ctx);
            llvmFunc->setMetadata(
                llvm::LLVMContext::MD_func_sanitize,
                MDB.createRTTIPointerPrologue(
                    llvm::ConstantInt::get(
                        i32Ty, static_cast<uint32_t>(sigAttr.getInt())),
                    llvm::ConstantInt::get(
                        i32Ty, static_cast<uint32_t>(hashAttr.getInt()))));
          }
        }
        func->removeAttr(attribute.getName());
        return mlir::success();
      }
      if (attribute.getName() == "cir.func_has_end_catch_normal_cleanup") {
        for (llvm::BasicBlock &bb : *llvmFunc) {
          llvm::SmallVector<llvm::CallInst *, 2> endCatchCalls;
          for (llvm::Instruction &inst : bb) {
            auto *callInst = llvm::dyn_cast<llvm::CallInst>(&inst);
            auto *calledFn = callInst ? callInst->getCalledFunction() : nullptr;
            if (calledFn && calledFn->getName() == "__cxa_end_catch")
              endCatchCalls.push_back(callInst);
          }
          for (llvm::CallInst *callInst : endCatchCalls)
            rewriteEndCatchWithCleanupInvoke(callInst);
        }
        func->removeAttr(attribute.getName());
        return mlir::success();
      }
      if (attribute.getName() == "cir.win_msvc_raw_objc_catchall") {
        llvmFunc->setMetadata(
            "cir.win_msvc_raw_objc_catchall",
            llvm::MDNode::get(llvmFunc->getContext(), {}));
        func->removeAttr(attribute.getName());
        return mlir::success();
      }
      if (attribute.getName() == "cir.clspv_libclc_builtin") {
        llvmFunc->setMetadata("clspv_libclc_builtin",
                              llvm::MDNode::get(llvmFunc->getContext(), {}));
        func->removeAttr(attribute.getName());
        return mlir::success();
      }
      if (auto extraAttr = mlir::dyn_cast<cir::ExtraFuncAttributesAttr>(
              attribute.getValue())) {
        for (auto attr : extraAttr.getElements()) {
          if (mlir::dyn_cast<cir::OpenCLKernelAttr>(attr.getValue())) {
            const auto uniformAttrName =
                cir::OpenCLKernelUniformWorkGroupSizeAttr::getMnemonic();
            const bool isUniform =
                extraAttr.getElements().getNamed(uniformAttrName).has_value();
            auto attrs = llvmFunc->getAttributes().addFnAttribute(
                llvmFunc->getContext(), "uniform-work-group-size",
                isUniform ? "true" : "false");
            llvmFunc->setAttributes(attrs);
          } else if (attr.getName() == "nonlazybind") {
            llvmFunc->addFnAttr(llvm::Attribute::NonLazyBind);
          } else if (auto clKernelMetadata =
                         mlir::dyn_cast<cir::OpenCLKernelMetadataAttr>(
                             attr.getValue())) {
            emitOpenCLKernelMetadata(clKernelMetadata, llvmFunc,
                                     moduleTranslation);
          } else if (auto clArgMetadata =
                         mlir::dyn_cast<cir::OpenCLKernelArgMetadataAttr>(
                             attr.getValue())) {
            emitOpenCLKernelArgMetadata(clArgMetadata, llvmFunc,
                                        moduleTranslation);
          } else if (attr.getName() == "callback") {
            auto callbackEncoding =
                mlir::dyn_cast<mlir::ArrayAttr>(attr.getValue());
            if (!callbackEncoding || callbackEncoding.empty())
              continue;

            SmallVector<int, 4> payloadIndices;
            int calleeIdx = 0;
            bool validEncoding = true;
            for (auto [index, item] : llvm::enumerate(callbackEncoding)) {
              auto intAttr = mlir::dyn_cast<mlir::IntegerAttr>(item);
              if (!intAttr) {
                validEncoding = false;
                break;
              }

              int value = static_cast<int>(intAttr.getValue().getSExtValue());
              if (index == 0)
                calleeIdx = value;
              else
                payloadIndices.push_back(value);
            }
            if (!validEncoding)
              continue;

            auto &ctx = llvmFunc->getContext();
            llvm::MDBuilder mdBuilder(ctx);
            llvmFunc->addMetadata(
                llvm::LLVMContext::MD_callback,
                *llvm::MDNode::get(
                    ctx, {mdBuilder.createCallbackEncoding(
                             calleeIdx, payloadIndices,
                             /*VarArgsArePassed=*/false)}));
          } else if (attr.getName() == "kernel_arg_name") {
            auto names = mlir::dyn_cast<mlir::ArrayAttr>(attr.getValue());
            if (!names)
              continue;

            SmallVector<llvm::Metadata *, 8> argNames;
            for (auto nameAttr : names) {
              auto str = mlir::dyn_cast<mlir::StringAttr>(nameAttr);
              if (!str)
                continue;
              argNames.push_back(
                  llvm::MDString::get(llvmFunc->getContext(), str.getValue()));
            }

            llvmFunc->setMetadata("kernel_arg_name",
                                  llvm::MDNode::get(llvmFunc->getContext(),
                                                    argNames));
          }
        }
      }
      // Drop amended CIR attribute from LLVM op.
      func->removeAttr(attribute.getName());
    } else if (auto module = llvm::dyn_cast<mlir::ModuleOp>(op)) {
      llvm::Module *llvmModule = moduleTranslation.getLLVMModule();
      auto emitIntegerModuleFlag = [&](llvm::StringRef attrName,
                                       llvm::Module::ModFlagBehavior behavior,
                                       llvm::StringRef flagName) {
        if (attribute.getName() != attrName)
          return;
        auto intAttr = mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue());
        if (!intAttr)
          return;
        auto intTy = mlir::dyn_cast<mlir::IntegerType>(intAttr.getType());
        if (!intTy)
          return;
        auto *llvmIntTy =
            llvm::IntegerType::get(llvmModule->getContext(), intTy.getWidth());
        auto *valueMD = llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
            llvmIntTy, intAttr.getValue().getZExtValue()));
        llvmModule->addModuleFlag(behavior, flagName, valueMD);
      };
      auto emitStringModuleFlag = [&](llvm::StringRef attrName,
                                      llvm::Module::ModFlagBehavior behavior,
                                      llvm::StringRef flagName) {
        if (attribute.getName() != attrName)
          return;
        auto strAttr = mlir::dyn_cast<mlir::StringAttr>(attribute.getValue());
        if (!strAttr)
          return;
        llvmModule->addModuleFlag(
            behavior, flagName,
            llvm::MDString::get(llvmModule->getContext(), strAttr.getValue()));
      };
      if (auto uwTableAttr =
              mlir::dyn_cast<cir::UWTableAttr>(attribute.getValue()))
        llvmModule->setUwtable(convertUWTableKind(uwTableAttr.getValue()));
      else if (auto openclVersionAttr = mlir::dyn_cast<cir::OpenCLVersionAttr>(
                   attribute.getValue())) {
        auto &llvmContext = llvmModule->getContext();
        auto *int32Ty = llvm::IntegerType::get(llvmContext, 32);
        llvm::Metadata *oclVerElts[] = {
            llvm::ConstantAsMetadata::get(
                llvm::ConstantInt::get(int32Ty, openclVersionAttr.getMajor())),
            llvm::ConstantAsMetadata::get(
                llvm::ConstantInt::get(int32Ty, openclVersionAttr.getMinor()))};
        llvm::NamedMDNode *oclVerMD =
            llvmModule->getOrInsertNamedMetadata("opencl.ocl.version");
        oclVerMD->addOperand(llvm::MDNode::get(llvmContext, oclVerElts));
      }
      // Emit wchar_size module flag (OGCG CodeGenModule.cpp:1169-1171).
      if (attribute.getName() == "cir.wchar_size") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue()))
          llvmModule->addModuleFlag(llvm::Module::Error, "wchar_size",
                                    intAttr.getInt());
      }
      emitStringModuleFlag("cir.alloc_token_mode", llvm::Module::Error,
                           "alloc-token-mode");
      emitIntegerModuleFlag("cir.alloc_token_max", llvm::Module::Error,
                            "alloc-token-max");
      if (attribute.getName() == "cir.alloc_token_fast_abi")
        llvmModule->addModuleFlag(llvm::Module::Error,
                                  "alloc-token-fast-abi", 1);
      if (attribute.getName() == "cir.alloc_token_extended")
        llvmModule->addModuleFlag(llvm::Module::Error,
                                  "alloc-token-extended", 1);
      emitIntegerModuleFlag("cir.objc_version", llvm::Module::Error,
                            "Objective-C Version");
      emitIntegerModuleFlag("cir.objc_image_info_version",
                            llvm::Module::Error,
                            "Objective-C Image Info Version");
      emitStringModuleFlag("cir.objc_image_info_section", llvm::Module::Error,
                           "Objective-C Image Info Section");
      emitIntegerModuleFlag("cir.objc_garbage_collection",
                            llvm::Module::Error,
                            "Objective-C Garbage Collection");
      emitIntegerModuleFlag("cir.objc_gc_only", llvm::Module::Error,
                            "Objective-C GC Only");
      if (attribute.getName() == "cir.objc_gc_only_require") {
        auto &ctx = llvmModule->getContext();
        auto *int8Ty = llvm::Type::getInt8Ty(ctx);
        llvm::Metadata *ops[] = {
            llvm::MDString::get(ctx, "Objective-C Garbage Collection"),
            llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                int8Ty, static_cast<uint8_t>(1u << 1)))};
        llvmModule->addModuleFlag(llvm::Module::Require, "Objective-C GC Only",
                                  llvm::MDNode::get(ctx, ops));
      }
      emitIntegerModuleFlag("cir.objc_is_simulated", llvm::Module::Error,
                            "Objective-C Is Simulated");
      emitIntegerModuleFlag("cir.objc_class_properties", llvm::Module::Error,
                            "Objective-C Class Properties");
      emitIntegerModuleFlag("cir.objc_enforce_class_ro_pointer_signing",
                            llvm::Module::Error,
                            "Objective-C Enforce ClassRO Pointer Signing");
      emitStringModuleFlag("cir.objc_arc_rv_marker", llvm::Module::Error,
                           llvm::objcarc::getRVMarkerModuleFlagStr());
      // Emit OpenMP version module flags (OGCG CodeGenModule.cpp:1440-1445).
      if (attribute.getName() == "cir.openmp") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue())) {
          llvmModule->addModuleFlag(llvm::Module::Max, "openmp",
                                    intAttr.getInt());
          // Also set the version on the OMPIRBuilder config so that
          // version-dependent lowering (e.g. flush suppression for atomic
          // capture in OpenMP 5.1+) works correctly.
          llvm::OpenMPIRBuilder *ompBuilder =
              moduleTranslation.getOpenMPBuilder();
          ompBuilder->Config.setOpenMPVersion(intAttr.getInt());
        }
      }
      if (attribute.getName() == "cir.openmp_device") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue()))
          llvmModule->addModuleFlag(llvm::Module::Max, "openmp-device",
                                    intAttr.getInt());
      }
      // Emit PIC Level module flag (OGCG CodeGenModule.cpp:1473-1475).
      if (attribute.getName() == "cir.pic_level") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue()))
          llvmModule->setPICLevel(
              static_cast<llvm::PICLevel::Level>(intAttr.getInt()));
      }
      // Emit PIE Level module flag (OGCG CodeGenModule.cpp:1476-1477).
      if (attribute.getName() == "cir.pie_level") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue()))
          llvmModule->setPIELevel(
              static_cast<llvm::PIELevel::Level>(intAttr.getInt()));
      }
      // Emit Code Model module flag (OGCG CodeGenModule.cpp:1480-1490).
      if (attribute.getName() == "cir.code_model") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue()))
          llvmModule->setCodeModel(
              static_cast<llvm::CodeModel::Model>(intAttr.getInt()));
      }
      // Emit Large Data Threshold module flag (OGCG CodeGenModule.cpp:1492-1496).
      if (attribute.getName() == "cir.large_data_threshold") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue()))
          llvmModule->setLargeDataThreshold(intAttr.getInt());
      }
      // Emit frame-pointer module flag (OGCG CodeGenModule.cpp:1511-1527).
      // Map clang::CodeGenOptions::FramePointerKind enum to
      // llvm::FramePointerKind (different orderings).
      if (attribute.getName() == "cir.frame_pointer") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue())) {
          static const llvm::FramePointerKind clangToLLVM[] = {
              llvm::FramePointerKind::NonLeafNoReserve, // CG 0
              llvm::FramePointerKind::None,             // CG 1
              llvm::FramePointerKind::Reserved,         // CG 2
              llvm::FramePointerKind::NonLeaf,          // CG 3
              llvm::FramePointerKind::All,              // CG 4
          };
          unsigned idx = intAttr.getInt();
          if (idx < std::size(clangToLLVM))
            llvmModule->setFramePointer(clangToLLVM[idx]);
        }
      }
      // Emit MemProfProfileFilename module flag (OGCG CodeGenModule.cpp:1410).
      if (attribute.getName() == "cir.memprof_filename") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
          auto &ctx = llvmModule->getContext();
          llvmModule->addModuleFlag(
              llvm::Module::Error, "MemProfProfileFilename",
              llvm::MDString::get(ctx, strAttr.getValue()));
        }
      }
      // Emit llvm.ident metadata (OGCG CodeGenModule.cpp:8074-8082).
      if (attribute.getName() == "cir.ident") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
          auto &llvmContext = llvmModule->getContext();
          llvm::NamedMDNode *identMD =
              llvmModule->getOrInsertNamedMetadata("llvm.ident");
          llvm::Metadata *identNode[] = {
              llvm::MDString::get(llvmContext, strAttr.getValue())};
          identMD->addOperand(llvm::MDNode::get(llvmContext, identNode));
        }
      }
      // Emit stack-protector-guard module flags
      // (OGCG CodeGenModule.cpp:1547-1557).
      if (attribute.getName() == "cir.stack_protector_guard") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue()))
          llvmModule->setStackProtectorGuard(strAttr.getValue().str());
      }
      if (attribute.getName() == "cir.stack_protector_guard_reg") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue()))
          llvmModule->setStackProtectorGuardReg(strAttr.getValue().str());
      }
      if (attribute.getName() == "cir.stack_protector_guard_symbol") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue()))
          llvmModule->setStackProtectorGuardSymbol(strAttr.getValue().str());
      }
      if (attribute.getName() == "cir.stack_protector_guard_offset") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue()))
          llvmModule->setStackProtectorGuardOffset(intAttr.getInt());
      }
      // Emit probe-stack and stack-probe-size module flags
      // (OGCG CodeGenModule.cpp:1401-1408).
      if (attribute.getName() == "cir.probe_stack") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
          llvmModule->addModuleFlag(
              llvm::Module::Override, "probe-stack",
              llvm::MDString::get(llvmModule->getContext(),
                                  strAttr.getValue()));
        }
      }
      if (attribute.getName() == "cir.stack_probe_size") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue()))
          llvmModule->addModuleFlag(llvm::Module::Min, "stack-probe-size",
                                    static_cast<uint32_t>(intAttr.getInt()));
      }
      emitIntegerModuleFlag("cir.num_register_parameters",
                            llvm::Module::Error,
                            "NumRegisterParameters");
      emitIntegerModuleFlag("cir.direct_access_external_data",
                            llvm::Module::Max,
                            "direct-access-external-data");
      // Emit SkipRaxSetup module flag for -mskip-rax-setup
      // (OGCG CodeGenModule.cpp:1560-1561).
      if (attribute.getName() == "cir.skip_rax_setup")
        llvmModule->addModuleFlag(llvm::Module::Override, "SkipRaxSetup", 1);
      // Emit MaxTLSAlign module flag (OGCG CodeGenModule.cpp:1565-1567).
      if (attribute.getName() == "cir.max_tls_align") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue()))
          llvmModule->addModuleFlag(llvm::Module::Error, "MaxTLSAlign",
                                    static_cast<uint32_t>(intAttr.getInt()));
      }
      // Emit cf-protection module flags (OGCG CodeGenModule.cpp:1280-1290).
      emitIntegerModuleFlag("cir.cf_protection_return", llvm::Module::Min,
                            "cf-protection-return");
      emitIntegerModuleFlag("cir.cf_protection_branch", llvm::Module::Min,
                            "cf-protection-branch");
      // Windows/platform-specific module flags.
      if (attribute.getName() == "cir.cfguard") {
        if (auto intAttr = mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue()))
          llvmModule->addModuleFlag(llvm::Module::Warning, "cfguard",
                                    static_cast<uint32_t>(intAttr.getInt()));
      }
      if (attribute.getName() == "cir.ehcontguard")
        llvmModule->addModuleFlag(llvm::Module::Warning, "ehcontguard", 1);
      if (attribute.getName() == "cir.ms_kernel")
        llvmModule->addModuleFlag(llvm::Module::Warning, "ms-kernel", 1);
      if (attribute.getName() == "cir.regcallv4")
        llvmModule->addModuleFlag(llvm::Module::Override, "RegCallv4", 1);
      if (attribute.getName() == "cir.semantic_interposition")
        llvmModule->setSemanticInterposition(true);
      if (attribute.getName() == "cir.strict_vtable_pointers") {
        llvmModule->addModuleFlag(llvm::Module::Error, "StrictVTablePointers",
                                  1);
        auto &ctx = llvmModule->getContext();
        llvm::Metadata *ops[] = {
            llvm::MDString::get(ctx, "StrictVTablePointers"),
            llvm::ConstantAsMetadata::get(
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 1))};
        llvmModule->addModuleFlag(
            llvm::Module::Require, "StrictVTablePointersRequirement",
            llvm::MDNode::get(ctx, ops));
      }
      if (attribute.getName() == "cir.virtual_function_elim") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue()))
          llvmModule->addModuleFlag(llvm::Module::Error,
                                    "Virtual Function Elim",
                                    static_cast<uint32_t>(intAttr.getInt()));
      }
      // Emit Cross-DSO CFI and canonical-jump-table sanitizer flags.
      if (attribute.getName() == "cir.cross_dso_cfi")
        llvmModule->addModuleFlag(llvm::Module::Override, "Cross-DSO CFI", 1);
      if (attribute.getName() == "cir.cfi_canonical_jump_tables") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue()))
          llvmModule->addModuleFlag(
              llvm::Module::Override, "CFI Canonical Jump Tables",
              static_cast<uint32_t>(intAttr.getInt()));
      }
      if (attribute.getName() == "cir.cfi_normalize_integers")
        llvmModule->addModuleFlag(llvm::Module::Override,
                                  "cfi-normalize-integers", 1);
      emitIntegerModuleFlag("cir.tag_stack_memory_buildattr",
                            llvm::Module::Override,
                            "tag-stack-memory-buildattr");
      // Emit RtLibUseGOT module flag for -fno-plt
      // (OGCG CodeGenModule.cpp:1500-1501).
      if (attribute.getName() == "cir.rtlib_use_got")
        llvmModule->setRtLibUseGOT();
      // Emit !llvm.dependent-libraries metadata.
      if (attribute.getName() == "cir.dependent_libraries") {
        if (auto arrayAttr =
                mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
          auto *nmd = llvmModule->getOrInsertNamedMetadata(
              "llvm.dependent-libraries");
          for (auto attr : arrayAttr) {
            if (auto strAttr = mlir::dyn_cast<mlir::StringAttr>(attr))
              nmd->addOperand(llvm::MDNode::get(
                  llvmModule->getContext(),
                  llvm::MDString::get(llvmModule->getContext(),
                                      strAttr.getValue())));
          }
        }
      }
      // Emit AArch64 branch protection module flags.
      auto emitMinFlag = [&](llvm::StringRef attrName,
                             llvm::StringRef flagName) {
        if (attribute.getName() == attrName)
          if (auto intAttr =
                  mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue()))
            llvmModule->addModuleFlag(llvm::Module::Min, flagName,
                                      static_cast<uint32_t>(intAttr.getInt()));
      };
      emitMinFlag("cir.bte", "branch-target-enforcement");
      emitMinFlag("cir.bp_pauth_lr", "branch-protection-pauth-lr");
      emitMinFlag("cir.gcs", "guarded-control-stack");
      emitMinFlag("cir.sign_return_address", "sign-return-address");
      emitMinFlag("cir.sign_return_address_all", "sign-return-address-all");
      emitMinFlag("cir.sign_return_address_with_bkey",
                  "sign-return-address-with-bkey");
      emitMinFlag("cir.ptrauth_elf_got", "ptrauth-elf-got");
      emitMinFlag("cir.ptrauth_sign_personality", "ptrauth-sign-personality");
      emitIntegerModuleFlag("cir.aarch64_elf_pauthabi_platform",
                            llvm::Module::Error,
                            "aarch64-elf-pauthabi-platform");
      emitIntegerModuleFlag("cir.aarch64_elf_pauthabi_version",
                            llvm::Module::Error,
                            "aarch64-elf-pauthabi-version");
      // Emit KCFI module flags (OGCG CodeGenModule.cpp:1263-1278).
      if (attribute.getName() == "cir.kcfi")
        llvmModule->addModuleFlag(llvm::Module::Override, "kcfi", 1);
      if (attribute.getName() == "cir.kcfi_offset") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue()))
          llvmModule->addModuleFlag(llvm::Module::Override, "kcfi-offset",
                                    static_cast<uint32_t>(intAttr.getInt()));
      }
      if (attribute.getName() == "cir.kcfi_arity")
        llvmModule->addModuleFlag(llvm::Module::Override, "kcfi-arity", 1);
      if (attribute.getName() == "cir.kcfi_hash") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue()))
          llvmModule->addModuleFlag(
              llvm::Module::Override, "kcfi-hash",
              llvm::MDString::get(llvmModule->getContext(),
                                  strAttr.getValue()));
      }
      // Emit amdhsa_code_object_version module flag
      // (matches OGCG CodeGenModule.cpp:1022-1031).
      emitIntegerModuleFlag("cir.amdhsa_code_object_version",
                            llvm::Module::Error,
                            "amdhsa_code_object_version");
      // Drop amended CIR attribute from LLVM op.
      module->removeAttr(attribute.getName());
    } else if (llvm::isa<mlir::LLVM::CallOp>(op) ||
               llvm::isa<mlir::LLVM::InvokeOp>(op) ||
               llvm::isa<mlir::LLVM::CallIntrinsicOp>(op) ||
               llvm::isa<mlir::LLVM::UBSanTrap>(op)) {
      auto collectCallBases = [&]() {
        llvm::SmallVector<llvm::CallBase *, 1> callBases;
        callBases.reserve(instructions.size());
        for (auto *inst : instructions)
          if (auto *callBase = llvm::dyn_cast<llvm::CallBase>(inst))
            callBases.push_back(callBase);
        if (callBases.empty()) {
          for (mlir::Value result : op->getResults())
            if (auto *callBase = llvm::dyn_cast_or_null<llvm::CallBase>(
                    moduleTranslation.lookupValue(result)))
              callBases.push_back(callBase);
        }
        return callBases;
      };
      llvm::StringRef attrName = attribute.getName();

      // UBSan/CFI-mfcall sibling attributes are pure metadata that is consumed
      // exclusively by the "cir.ubsan.handler_name" rewrite below (read via
      // op->getAttrOfType / op->hasAttr on the op, never via callBases). That
      // rewrite calls callBase->eraseFromParent() to replace the CIR UBSan stub
      // with the real __ubsan_handle_* call. Because
      // ModuleTranslation::convertDialectAttributes invokes amendOperation once
      // per dialect attribute with the SAME `instructions` ArrayRef, a later
      // call for one of these sibling attrs would re-run collectCallBases() and
      // dereference the llvm::CallInst that handler_name already freed
      // (heap-use-after-free). None of these siblings has its own amendment
      // action, and the per-attr cleanup below never removes them individually
      // (handler_name batch-erases the cir.ubsan.* set; the only cir.cfi.mfcall.*
      // attrs are kept), so returning early here is behavior-preserving and
      // order-independent while avoiding the freed-instruction access.
      if ((attrName.starts_with("cir.ubsan.") &&
           attrName != "cir.ubsan.handler_name") ||
          attrName.starts_with("cir.cfi.mfcall."))
        return mlir::success();

      llvm::SmallVector<llvm::CallBase *, 1> callBases = collectCallBases();

      if ((attrName == "cir.fp_exception" || attrName == "cir.rounding_mode") &&
          op->hasAttr("cir.fp_exception"))
        return amendFPConstraintAttrs(op, instructions, moduleTranslation);

      // Forward call-site allocsize attribute to LLVM IR.
      if (attrName == "cir.call_allocsize") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
          uint64_t rawRepr;
          if (!strAttr.getValue().getAsInteger(10, rawRepr)) {
            for (auto *callBase : callBases)
              callBase->addFnAttr(llvm::Attribute::get(
                  callBase->getContext(), llvm::Attribute::AllocSize,
                  rawRepr));
          }
        }
      }
      if (attrName == "cir.call_graph.callee_type") {
        if (auto typeIdAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
          for (auto *callBase : callBases) {
            if (!callBase->isIndirectCall())
              continue;
            llvm::LLVMContext &ctx = callBase->getContext();
            llvm::Metadata *offset =
                llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                    llvm::Type::getInt64Ty(ctx), 0));
            llvm::Metadata *typeId =
                llvm::MDString::get(ctx, typeIdAttr.getValue());
            llvm::MDTuple *typeTuple =
                llvm::MDTuple::get(ctx, {offset, typeId});
            callBase->setMetadata(
                llvm::LLVMContext::MD_callee_type,
                llvm::MDNode::get(ctx, {typeTuple}));
          }
        }
        op->removeAttr(attribute.getName());
        return mlir::success();
      }
      if (attrName == kPreserveAccessIndexDITypeAttrName) {
        llvm::DIType *metadataType = nullptr;
        if (auto diTypeAttr =
                mlir::dyn_cast<mlir::LLVM::DITypeAttr>(attribute.getValue())) {
          metadataType = llvm::dyn_cast_or_null<llvm::DIType>(
              moduleTranslation.translateDebugInfo(diTypeAttr));
          metadataType = rebuildPreserveAccessIndexDIType(
              moduleTranslation.getLLVMModule()->getContext(), op,
              metadataType);
          if (auto *metadata = llvm::dyn_cast_or_null<llvm::MDNode>(
                  metadataType)) {
            for (auto *callBase : callBases)
              callBase->setMetadata(
                  llvm::LLVMContext::MD_preserve_access_index, metadata);
          }
        }
        clearPreserveAccessIndexDebugAttrs(op);
        return mlir::success();
      }
      if (attrName.starts_with("cir.debug.") &&
          op->hasAttr(kPreserveAccessIndexDITypeAttrName))
        return mlir::success();
      enum class SimpleCallFnAttrKind {
        None,
        NoMerge,
        NoBuiltin,
        Builtin,
        StrictFP,
        NoCallback,
        NoReturn,
        ReturnsTwice,
        Cold,
        Hot,
        Convergent,
        NoDuplicate,
        OptimizeForSize,
        MinSize,
        CoroElideSafe,
      };
      auto simpleCallAttrKind =
          llvm::StringSwitch<SimpleCallFnAttrKind>(attrName)
              .Case("cir.call_nomerge", SimpleCallFnAttrKind::NoMerge)
              .Case("cir.call_nobuiltin", SimpleCallFnAttrKind::NoBuiltin)
              .Case("cir.call_builtin", SimpleCallFnAttrKind::Builtin)
              .Case("cir.call_strictfp", SimpleCallFnAttrKind::StrictFP)
              .Case("cir.call_nocallback", SimpleCallFnAttrKind::NoCallback)
              .Case("cir.call_noreturn", SimpleCallFnAttrKind::NoReturn)
              .Case("cir.call_returns_twice",
                    SimpleCallFnAttrKind::ReturnsTwice)
              .Case("cir.call_cold", SimpleCallFnAttrKind::Cold)
              .Case("cir.call_hot", SimpleCallFnAttrKind::Hot)
              .Case("cir.call_convergent", SimpleCallFnAttrKind::Convergent)
              .Case("cir.call_noduplicate", SimpleCallFnAttrKind::NoDuplicate)
              .Case("cir.call_optsize", SimpleCallFnAttrKind::OptimizeForSize)
              .Case("cir.call_minsize", SimpleCallFnAttrKind::MinSize)
              .Case("cir.call_coro_elide_safe",
                    SimpleCallFnAttrKind::CoroElideSafe)
              .Default(SimpleCallFnAttrKind::None);
      if (simpleCallAttrKind != SimpleCallFnAttrKind::None) {
        for (auto *callBase : callBases) {
          switch (simpleCallAttrKind) {
          case SimpleCallFnAttrKind::NoMerge:
            callBase->addFnAttr(llvm::Attribute::NoMerge);
            break;
          case SimpleCallFnAttrKind::NoBuiltin:
            callBase->addFnAttr(llvm::Attribute::NoBuiltin);
            break;
          case SimpleCallFnAttrKind::Builtin:
            callBase->addFnAttr(llvm::Attribute::Builtin);
            break;
          case SimpleCallFnAttrKind::StrictFP:
            callBase->addFnAttr(llvm::Attribute::StrictFP);
            break;
          case SimpleCallFnAttrKind::NoCallback:
            callBase->addFnAttr(llvm::Attribute::NoCallback);
            break;
          case SimpleCallFnAttrKind::NoReturn:
            callBase->addFnAttr(llvm::Attribute::NoReturn);
            break;
          case SimpleCallFnAttrKind::ReturnsTwice:
            callBase->addFnAttr(llvm::Attribute::ReturnsTwice);
            break;
          case SimpleCallFnAttrKind::Cold:
            callBase->addFnAttr(llvm::Attribute::Cold);
            break;
          case SimpleCallFnAttrKind::Hot:
            callBase->addFnAttr(llvm::Attribute::Hot);
            break;
          case SimpleCallFnAttrKind::Convergent:
            callBase->addFnAttr(llvm::Attribute::Convergent);
            break;
          case SimpleCallFnAttrKind::NoDuplicate:
            callBase->addFnAttr(llvm::Attribute::NoDuplicate);
            break;
          case SimpleCallFnAttrKind::OptimizeForSize:
            callBase->addFnAttr(llvm::Attribute::OptimizeForSize);
            break;
          case SimpleCallFnAttrKind::MinSize:
            callBase->addFnAttr(llvm::Attribute::MinSize);
            break;
          case SimpleCallFnAttrKind::CoroElideSafe:
            callBase->addFnAttr(llvm::Attribute::CoroElideSafe);
            break;
          case SimpleCallFnAttrKind::None:
            llvm_unreachable("unexpected simple call attribute kind");
          }
        }
      }
      if (attrName == "cir.call_memory_none") {
        for (auto *callBase : callBases)
          callBase->setDoesNotAccessMemory();
      }
      if (attrName == "must_tail" || attrName == "tail" ||
          attrName == "no_tail") {
        llvm::CallInst::TailCallKind kind = llvm::CallInst::TCK_None;
        if (attrName == "must_tail")
          kind = llvm::CallInst::TCK_MustTail;
        else if (attrName == "tail")
          kind = llvm::CallInst::TCK_Tail;
        else if (attrName == "no_tail")
          kind = llvm::CallInst::TCK_NoTail;
        for (auto *callBase : callBases)
          if (auto *callInst = llvm::dyn_cast<llvm::CallInst>(callBase))
            callInst->setTailCallKind(kind);
      }
      // Forward no_caller_saved_registers attribute to LLVM IR call instruction.
      if (attrName == "cir.call_no_caller_saved_registers") {
        for (auto *callBase : callBases)
          callBase->addFnAttr(llvm::Attribute::get(
              callBase->getContext(), "no_caller_saved_registers"));
      }
      // Forward noalias return attribute to LLVM IR call instruction.
      // This is set on operator new calls when AssumeSaneOperatorNew is true.
      if (attrName == "cir.call_noalias_return") {
        for (auto *callBase : callBases)
          callBase->addRetAttr(llvm::Attribute::NoAlias);
      }
      // Forward no-builtins / no-builtin-<name> string attrs to LLVM IR.
      if (attrName == "cir.call_no-builtins" ||
          attrName.starts_with("cir.call_no-builtin-")) {
        llvm::StringRef llvmAttrName = attrName.drop_front(
            llvm::StringRef("cir.call_").size());
        for (auto *callBase : callBases)
          callBase->addFnAttr(llvm::Attribute::get(
              callBase->getContext(), llvmAttrName));
      }
      // Forward AArch64 SME/streaming callsite attrs as LLVM string attrs.
      // These are emitted by AddAttributesFromFunctionProtoType in CIRGen
      // (e.g. aarch64_pstate_sm_enabled, aarch64_pstate_sm_compatible,
      // aarch64_za_state_agnostic, aarch64_preserves_za, etc.).
      if (attrName.starts_with("cir.call_aarch64_") &&
          mlir::isa<mlir::UnitAttr>(attribute.getValue())) {
        llvm::StringRef llvmAttrName = attrName.drop_front(
            llvm::StringRef("cir.call_").size());
        for (auto *callBase : callBases)
          callBase->addFnAttr(llvm::Attribute::get(
              callBase->getContext(), llvmAttrName));
      }
      // Forward sample-profile-suffix-elision-policy to LLVM IR call site.
      if (attrName == "cir.call_sample_profile_suffix") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
          for (auto *callBase : callBases)
            callBase->addFnAttr(llvm::Attribute::get(
                callBase->getContext(),
                "sample-profile-suffix-elision-policy",
                strAttr.getValue()));
        }
      }
      // Forward !srcloc metadata for __attribute__((error/warning)) call sites.
      if (attrName == "cir.call_srcloc") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue())) {
          for (auto *callBase : callBases) {
            auto &llvmCtx = callBase->getContext();
            llvm::ConstantInt *rawLoc = llvm::ConstantInt::get(
                llvm::Type::getInt64Ty(llvmCtx), intAttr.getInt());
            callBase->setMetadata("srcloc", llvm::MDNode::get(
                llvmCtx, {llvm::ConstantAsMetadata::get(rawLoc)}));
          }
        }
      }
      if (attrName == "cir.gnu_objc_message_send") {
        if (auto payload =
                mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
          if (payload.size() == 3) {
            auto selectorAttr = mlir::dyn_cast<mlir::StringAttr>(payload[0]);
            auto receiverClassAttr =
                mlir::dyn_cast<mlir::StringAttr>(payload[1]);
            bool isClass = false;
            if (auto isClassAttr = mlir::dyn_cast<mlir::BoolAttr>(payload[2])) {
              isClass = isClassAttr.getValue();
            } else if (auto isClassAttr =
                           mlir::dyn_cast<mlir::IntegerAttr>(payload[2])) {
              isClass = isClassAttr.getInt() != 0;
            }

            if (selectorAttr && receiverClassAttr) {
              for (auto *callBase : callBases) {
                auto &llvmCtx = callBase->getContext();
                llvm::Metadata *metadata[] = {
                    llvm::MDString::get(llvmCtx, selectorAttr.getValue()),
                    llvm::MDString::get(llvmCtx, receiverClassAttr.getValue()),
                    llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                        llvm::Type::getInt1Ty(llvmCtx), isClass))};
                callBase->setMetadata("GNUObjCMessageSend",
                                      llvm::MDNode::get(llvmCtx, metadata));
              }
            }
          }
        }
      }
      if (attrName == "cir.alloc_token") {
        if (auto payload =
                mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
          for (auto *callBase : callBases) {
            auto *allocTokenMD =
                buildAllocTokenMetadata(payload, callBase->getContext());
            if (!allocTokenMD)
              continue;

            auto *calledFn = callBase->getCalledFunction();
            if (calledFn && calledFn->getName() == "__cir_alloc_token_id") {
              llvm::Module *mod = calledFn->getParent();
              llvm::Function *allocTokenId =
                  llvm::Intrinsic::getOrInsertDeclaration(
                      mod, llvm::Intrinsic::alloc_token_id,
                      {callBase->getType()});
              llvm::Value *metadataArg = llvm::MetadataAsValue::get(
                  callBase->getContext(), allocTokenMD);
              callBase->setCalledFunction(allocTokenId);
              callBase->setArgOperand(0, metadataArg);
              continue;
            }

            if (!payload.empty())
              callBase->setMetadata(llvm::LLVMContext::MD_alloc_token,
                                    allocTokenMD);
          }
        }
      }
      // Rewrite CIR allow-runtime-check marker to llvm.allow.runtime.check
      // with an MDString metadata operand.
      if (attrName == "cir.call_allow_runtime_check_kind") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
          for (auto *callBase : callBases) {
            auto *calledFn = callBase->getCalledFunction();
            if (!calledFn)
              continue;
            if (calledFn->getName() != "__cir_allow_runtime_check")
              continue;

            llvm::Module *mod = calledFn->getParent();
            llvm::Function *allowFn = llvm::Intrinsic::getOrInsertDeclaration(
                mod, llvm::Intrinsic::allow_runtime_check);
            llvm::Metadata *kindMD =
                llvm::MDString::get(callBase->getContext(), strAttr.getValue());
            llvm::Value *kindVal = llvm::MetadataAsValue::get(
                callBase->getContext(), kindMD);
            callBase->setCalledFunction(allowFn);
            callBase->setArgOperand(0, kindVal);
          }
        }
      }
      // Forward !clang.imprecise_release metadata to ARC intrinsic calls.
      // This metadata tells the ARC optimizer that the release can be
      // moved/merged (set for imprecise lifetime, not for precise).
      if (attrName == "cir.imprecise_release") {
        for (auto *callBase : callBases)
          callBase->setMetadata(
              "clang.imprecise_release",
              llvm::MDNode::get(callBase->getContext(), {}));
      }
      if (attrName == "cir.arc.copy_on_escape") {
        for (auto *callBase : callBases)
          callBase->setMetadata(
              "clang.arc.copy_on_escape",
              llvm::MDNode::get(callBase->getContext(), {}));
      }
      // In ObjC ARC mode without ARC EH semantics, mirror OGCG's
      // !clang.arc.no_objc_arc_exceptions metadata on all call/invoke sites so
      // ARC opts can ignore unwind edges aggressively.
      if (attrName == "cir.arc.no_objc_arc_exceptions") {
        for (auto *callBase : callBases)
          callBase->setMetadata(
              "clang.arc.no_objc_arc_exceptions",
              llvm::MDNode::get(callBase->getContext(), {}));
      }
      if (attrName == "cir.cleanup_attr_early_exit") {
        for (auto *callBase : callBases)
          callBase->setMetadata(
              "cir.cleanup_attr_early_exit.pending",
              llvm::MDNode::get(callBase->getContext(), {}));
      }
      if (attrName == "cir.defer_cleanup_dispatch_candidate") {
        for (auto *callBase : callBases) {
          auto &ctx = callBase->getContext();
          if (auto intAttr =
                  mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue())) {
            callBase->setMetadata(
                "cir.defer_cleanup_dispatch_candidate.pending",
                llvm::MDNode::get(
                    ctx, llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                             llvm::Type::getInt64Ty(ctx),
                             intAttr.getValue().getZExtValue()))));
          } else {
            callBase->setMetadata(
                "cir.defer_cleanup_dispatch_candidate.pending",
                llvm::MDNode::get(ctx, {}));
          }
        }
      }
      // Mark ARC attached-call operand bundles for post-translation fixup.
      // amendOperation cannot replace call instructions because the per-op
      // value mapping becomes stale for subsequent translated users.
      if (attrName == "cir.arc.attachedcall") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
          for (auto *callBase : callBases) {
            auto &ctx = callBase->getContext();
            callBase->setMetadata(
                "cir.arc.attachedcall.pending",
                llvm::MDNode::get(
                    ctx, llvm::MDString::get(ctx, strAttr.getValue())));
          }
        }
      }
      // Mark indirect calls for KCFI operand bundle addition in post-processing.
      // We cannot replace the call instruction here (during per-op translation)
      // because it would invalidate the MLIR value mapping for later ops.
      // Instead, attach temporary metadata that the post-translation step
      // converts into a proper operand bundle.
      if (attrName == "cir.kcfi") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue())) {
          uint32_t typeId = static_cast<uint32_t>(intAttr.getInt());
          for (auto *callBase : callBases) {
            auto &ctx = callBase->getContext();
            llvm::MDBuilder MDB(ctx);
            callBase->setMetadata(
                "cir.kcfi.pending",
                llvm::MDNode::get(ctx,
                                  MDB.createConstant(llvm::ConstantInt::get(
                                      llvm::Type::getInt32Ty(ctx), typeId))));
          }
        }
      }
      if (attrName == "cir.ptrauth") {
        if (auto payload =
                mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
          if (payload.size() == 2) {
            auto keyAttr = mlir::dyn_cast<mlir::IntegerAttr>(payload[0]);
            auto discAttr = mlir::dyn_cast<mlir::IntegerAttr>(payload[1]);
            if (keyAttr && discAttr) {
              for (auto *callBase : callBases) {
                auto &ctx = callBase->getContext();
                llvm::MDBuilder MDB(ctx);
                callBase->setMetadata(
                    "cir.ptrauth.pending",
                    llvm::MDNode::get(
                        ctx,
                        {MDB.createConstant(llvm::ConstantInt::get(
                             llvm::Type::getInt32Ty(ctx), keyAttr.getInt())),
                         MDB.createConstant(llvm::ConstantInt::get(
                             llvm::Type::getInt64Ty(ctx),
                             discAttr.getValue().getZExtValue()))}));
              }
            }
          }
        }
      }
      if (attrName == "cir.ptrauth.blend_vtable_slot") {
        for (auto *callBase : callBases)
          callBase->setMetadata("cir.ptrauth.blend_vtable_slot.pending",
                                llvm::MDNode::get(callBase->getContext(), {}));
      }
      if (attrName == "cir.throw_dtor_ptrauth") {
        if (auto payload =
                mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
          if (payload.size() == 2) {
            auto keyAttr = mlir::dyn_cast<mlir::IntegerAttr>(payload[0]);
            auto discAttr = mlir::dyn_cast<mlir::IntegerAttr>(payload[1]);
            if (keyAttr && discAttr) {
              for (auto *callBase : callBases) {
                auto *calledFn = callBase->getCalledFunction();
                if (!calledFn || calledFn->getName() != "__cxa_throw")
                  continue;
                if (callBase->arg_size() < 3)
                  continue;
                auto *dtorC =
                    llvm::dyn_cast<llvm::Constant>(callBase->getArgOperand(2));
                if (!dtorC)
                  continue;
                llvm::LLVMContext &ctx = callBase->getContext();
                llvm::Constant *signedDtor = llvm::ConstantPtrAuth::get(
                    dtorC,
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx),
                                           keyAttr.getInt()),
                    llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx),
                                           discAttr.getValue().getZExtValue()),
                    llvm::Constant::getNullValue(dtorC->getType()),
                    llvm::Constant::getNullValue(dtorC->getType()));
                callBase->setArgOperand(2, signedDtor);
              }
            }
          }
        }
      }
      if (attrName == "cir.atexit_callback_ptrauth") {
        if (auto payload =
                mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
          if (payload.size() == 2) {
            auto keyAttr = mlir::dyn_cast<mlir::IntegerAttr>(payload[0]);
            auto discAttr = mlir::dyn_cast<mlir::IntegerAttr>(payload[1]);
            if (keyAttr && discAttr) {
              for (auto *callBase : callBases) {
                auto *calledFn = callBase->getCalledFunction();
                if (!calledFn)
                  continue;
                StringRef calledName = calledFn->getName();
                if (calledName != "__cxa_atexit" &&
                    calledName != "__cxa_thread_atexit" &&
                    calledName != "_tlv_atexit" && calledName != "atexit")
                  continue;
                if (callBase->arg_size() < 1)
                  continue;
                auto *callback =
                    llvm::dyn_cast<llvm::Constant>(callBase->getArgOperand(0));
                if (!callback)
                  continue;
                llvm::LLVMContext &ctx = callBase->getContext();
                llvm::Constant *signedCallback = llvm::ConstantPtrAuth::get(
                    callback,
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx),
                                           keyAttr.getInt()),
                    llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx),
                                           discAttr.getValue().getZExtValue()),
                    llvm::Constant::getNullValue(callback->getType()),
                    llvm::Constant::getNullValue(callback->getType()));
                callBase->setArgOperand(0, signedCallback);
              }
            }
          }
        }
      }
      if (attrName == "cir.cfi.icall.typeid") {
        auto typeIdAttr =
            mlir::dyn_cast<mlir::StringAttr>(attribute.getValue());
        if (typeIdAttr) {
          auto handlerKindAttr =
              op->getAttrOfType<mlir::StringAttr>("cir.cfi.icall.kind");
          auto typeTestKindAttr = op->getAttrOfType<mlir::StringAttr>(
              "cir.cfi.icall.typetest_kind");
          bool cfiMinimalRuntime =
              op->hasAttr("cir.cfi.icall.minimal_runtime");
          bool cfiPreserveAllRegs =
              op->hasAttr("cir.cfi.icall.preserve_all_regs");
          auto sourceFileAttr =
              op->getAttrOfType<mlir::StringAttr>("cir.ubsan.source_file");
          auto sourceLineAttr =
              op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.source_line");
          auto sourceColumnAttr =
              op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.source_column");
          auto typeNameAttr =
              op->getAttrOfType<mlir::StringAttr>("cir.ubsan.type_name");
          auto typeKindAttr =
              op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.type_kind");
          auto typeInfoAttr =
              op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.type_info");
          auto tckAttr = op->getAttrOfType<mlir::IntegerAttr>(
              "cir.ubsan.type_check_kind");

          for (auto *inst : instructions) {
            auto *callBase = llvm::dyn_cast<llvm::CallBase>(inst);
            if (!callBase)
              continue;

            llvm::LLVMContext &ctx = callBase->getContext();
            llvm::MDBuilder mdb(ctx);
            auto intMD = [&](uint64_t value) -> llvm::Metadata * {
              return mdb.createConstant(
                  llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx), value));
            };
            llvm::Metadata *typeIdMD = nullptr;
            if (typeTestKindAttr && typeTestKindAttr.getValue() == "hidden") {
              auto &cached = translationCaches().hiddenTypeIdCache[typeIdAttr.getValue().str()];
              if (!cached)
                cached = llvm::MDNode::getDistinct(ctx, {});
              typeIdMD = cached;
            } else {
              typeIdMD = llvm::MDString::get(ctx, typeIdAttr.getValue());
            }
            callBase->setMetadata(
                "cir.cfi.icall.pending",
                llvm::MDNode::get(
                    ctx,
                    {typeIdMD,
                     llvm::MDString::get(
                         ctx, handlerKindAttr ? handlerKindAttr.getValue()
                                              : llvm::StringRef("abort")),
                     intMD(cfiMinimalRuntime), intMD(cfiPreserveAllRegs),
                     llvm::MDString::get(
                         ctx, sourceFileAttr ? sourceFileAttr.getValue()
                                             : llvm::StringRef()),
                     intMD(sourceLineAttr ? sourceLineAttr.getInt() : 0),
                     intMD(sourceColumnAttr ? sourceColumnAttr.getInt() : 0),
                     llvm::MDString::get(
                         ctx, typeNameAttr ? typeNameAttr.getValue()
                                           : llvm::StringRef()),
                     intMD(typeKindAttr
                               ? typeKindAttr.getValue().zextOrTrunc(16)
                                     .getZExtValue()
                               : 0xFFFF),
                     intMD(typeInfoAttr
                               ? typeInfoAttr.getValue().zextOrTrunc(16)
                                     .getZExtValue()
                               : 0),
                     intMD(tckAttr ? tckAttr.getValue().zextOrTrunc(8)
                                         .getZExtValue()
                                 : 0),
                     llvm::MDString::get(ctx, typeIdAttr.getValue())}));
          }

          op->removeAttr("cir.cfi.icall.typeid");
          op->removeAttr("cir.cfi.icall.kind");
          op->removeAttr("cir.cfi.icall.typetest_kind");
          op->removeAttr("cir.cfi.icall.minimal_runtime");
          op->removeAttr("cir.cfi.icall.preserve_all_regs");
          return mlir::success();
        }
      }
      // Emit virtual-call devirtualization IR sequences for calls marked by
      // CIRGen. This covers both speculative/WPD type tests and the stronger
      // VFE llvm.type.checked.load form used for hidden-LTO virtual calls.
      if (attrName == "cir.virtual.typeid") {
        auto typeIdAttr =
            mlir::dyn_cast<mlir::StringAttr>(attribute.getValue());
        if (typeIdAttr) {
          bool useTypeCheckedLoad =
              op->hasAttr("cir.virtual.typecheckedload") ||
              !op->hasAttr("cir.virtual.typetest_kind");
          bool useDistinctTypeId = op->hasAttr("cir.virtual.typeid_distinct");
          auto cfiVCallKindAttr =
              op->getAttrOfType<mlir::StringAttr>("cir.virtual.cfi_vcall_kind");
          bool isCFIVCall = cfiVCallKindAttr != nullptr;
          bool cfiMinimalRuntime =
              op->hasAttr("cir.virtual.cfi_minimal_runtime");
          bool cfiPreserveAllRegs =
              op->hasAttr("cir.virtual.cfi_preserve_all_regs");
          llvm::StringRef kind = "hidden";
          if (auto kindAttr = op->getAttrOfType<mlir::StringAttr>(
                  "cir.virtual.typetest_kind"))
            kind = kindAttr.getValue();
          auto sourceFileAttr =
              op->getAttrOfType<mlir::StringAttr>("cir.ubsan.source_file");
          auto sourceLineAttr =
              op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.source_line");
          auto sourceColumnAttr =
              op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.source_column");
          auto typeNameAttr =
              op->getAttrOfType<mlir::StringAttr>("cir.ubsan.type_name");
          auto typeKindAttr =
              op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.type_kind");
          auto typeInfoAttr =
              op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.type_info");
          auto tckAttr = op->getAttrOfType<mlir::IntegerAttr>(
              "cir.ubsan.type_check_kind");

          if (isCFIVCall) {
            for (auto *inst : instructions) {
              auto *callBase = llvm::dyn_cast<llvm::CallBase>(inst);
              if (!callBase)
                continue;
              if (auto *calledFn = callBase->getCalledFunction();
                  calledFn &&
                  calledFn->getName() == "__cir_virtual_memptr_slot_load")
                continue;

              llvm::LLVMContext &ctx = callBase->getContext();
              llvm::MDBuilder mdb(ctx);
              auto intMD = [&](uint64_t value) -> llvm::Metadata * {
                return mdb.createConstant(
                    llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx), value));
              };
              callBase->setMetadata(
                  "cir.cfi.vcall.pending",
                  llvm::MDNode::get(
                      ctx,
                      {llvm::MDString::get(ctx, typeIdAttr.getValue()),
                       llvm::MDString::get(ctx, kind),
                       llvm::MDString::get(
                           ctx, cfiVCallKindAttr ? cfiVCallKindAttr.getValue()
                                                 : llvm::StringRef()),
                       intMD(useTypeCheckedLoad), intMD(cfiMinimalRuntime),
                       intMD(cfiPreserveAllRegs),
                       llvm::MDString::get(
                           ctx, sourceFileAttr ? sourceFileAttr.getValue()
                                               : llvm::StringRef()),
                       intMD(sourceLineAttr ? sourceLineAttr.getInt() : 0),
                       intMD(sourceColumnAttr ? sourceColumnAttr.getInt() : 0),
                       llvm::MDString::get(
                           ctx, typeNameAttr ? typeNameAttr.getValue()
                                             : llvm::StringRef()),
                       intMD(typeKindAttr
                                 ? typeKindAttr.getValue().zextOrTrunc(16)
                                       .getZExtValue()
                                 : 0xFFFF),
                       intMD(typeInfoAttr
                                 ? typeInfoAttr.getValue().zextOrTrunc(16)
                                       .getZExtValue()
                                 : 0),
                       intMD(tckAttr ? tckAttr.getValue().zextOrTrunc(8)
                                           .getZExtValue()
                                    : 0)}));
            }

            op->removeAttr("cir.virtual.cfi_vcall_kind");
            op->removeAttr("cir.virtual.cfi_minimal_runtime");
            op->removeAttr("cir.virtual.cfi_preserve_all_regs");
            op->removeAttr("cir.virtual.typecheckedload");
            op->removeAttr("cir.virtual.typetest_kind");
            return mlir::success();
          }

          for (auto *inst : instructions) {
            auto *callBase = llvm::dyn_cast<llvm::CallBase>(inst);
            if (!callBase)
              continue;

            if (auto *calledFn = callBase->getCalledFunction();
                calledFn &&
                calledFn->getName() == "__cir_virtual_memptr_slot_load") {
              if (callBase->arg_size() < 1)
                continue;
              llvm::Value *slotAddr = callBase->getArgOperand(0);
              if (!slotAddr || !slotAddr->getType()->isPointerTy())
                continue;

              llvm::IRBuilder<> irb(callBase);
              llvm::Metadata *md = nullptr;
              if (useDistinctTypeId) {
                auto &cached = translationCaches().hiddenTypeIdCache[typeIdAttr.getValue().str()];
                if (!cached)
                  cached = llvm::MDNode::getDistinct(callBase->getContext(), {});
                md = cached;
              } else {
                md = llvm::MDString::get(callBase->getContext(),
                                         typeIdAttr.getValue());
              }
              llvm::Value *typeId =
                  llvm::MetadataAsValue::get(callBase->getContext(), md);
              bool noSanitize = false;
              if (auto *slotAddrInst = llvm::dyn_cast<llvm::Instruction>(slotAddr))
                noSanitize =
                    slotAddrInst->hasMetadata(llvm::LLVMContext::MD_nosanitize);

              if (useTypeCheckedLoad) {
                llvm::Function *checkedLoadFn =
                    llvm::Intrinsic::getOrInsertDeclaration(
                        callBase->getModule(),
                        llvm::Intrinsic::type_checked_load);
                llvm::Value *checkedLoad = irb.CreateCall(
                    checkedLoadFn,
                    {slotAddr, llvm::ConstantInt::get(
                                  llvm::Type::getInt32Ty(callBase->getContext()),
                                  0),
                     typeId});
                llvm::Value *checkedFn =
                    irb.CreateExtractValue(checkedLoad, {0});
                llvm::Value *checkedOk =
                    irb.CreateExtractValue(checkedLoad, {1});
                if (noSanitize) {
                  if (auto *ci = llvm::dyn_cast<llvm::Instruction>(checkedLoad))
                    ci->setNoSanitizeMetadata();
                  if (auto *ev = llvm::dyn_cast<llvm::Instruction>(checkedFn))
                    ev->setNoSanitizeMetadata();
                  if (auto *ev = llvm::dyn_cast<llvm::Instruction>(checkedOk))
                    ev->setNoSanitizeMetadata();
                }
                callBase->replaceAllUsesWith(checkedFn);
                if (op->getNumResults() > 0)
                  moduleTranslation.remapValue(op->getResult(0), checkedFn);
                callBase->eraseFromParent();
                (void)checkedOk;
                continue;
              }

              llvm::Intrinsic::ID iid =
                  (kind == "public") ? llvm::Intrinsic::public_type_test
                                     : llvm::Intrinsic::type_test;
              llvm::Function *typeTestFn =
                  llvm::Intrinsic::getOrInsertDeclaration(callBase->getModule(),
                                                          iid);
              llvm::Value *test = irb.CreateCall(typeTestFn, {slotAddr, typeId});
              llvm::Function *assumeFn = llvm::Intrinsic::getOrInsertDeclaration(
                  callBase->getModule(), llvm::Intrinsic::assume);
              llvm::Value *assume = irb.CreateCall(assumeFn, {test});
              llvm::Value *loadedFn = irb.CreateLoad(
                  llvm::PointerType::getUnqual(callBase->getContext()), slotAddr);
              if (noSanitize) {
                if (auto *call = llvm::dyn_cast<llvm::Instruction>(test))
                  call->setNoSanitizeMetadata();
                if (auto *call = llvm::dyn_cast<llvm::Instruction>(assume))
                  call->setNoSanitizeMetadata();
                if (auto *load = llvm::dyn_cast<llvm::Instruction>(loadedFn))
                  load->setNoSanitizeMetadata();
              }
              callBase->replaceAllUsesWith(loadedFn);
              if (op->getNumResults() > 0)
                moduleTranslation.remapValue(op->getResult(0), loadedFn);
              callBase->eraseFromParent();
              continue;
            }

            if (callBase->arg_size() < 1)
              continue;
            llvm::Value *thisArg = callBase->getArgOperand(0);
            if (!thisArg || !thisArg->getType()->isPointerTy())
              continue;

            // Find the existing vtable load by tracing back from the
            // indirect callee: callee (vfn load) → GEP → vtable load.
            // This avoids creating a redundant vtable load and ensures
            // the inserted metadata operation is placed in the same region
            // as OGCG's sequence.
            llvm::LoadInst *vtableLoad = nullptr;
            llvm::GetElementPtrInst *slotGEP = nullptr;
            llvm::LoadInst *slotLoad = nullptr;
            llvm::Instruction *insertPt = callBase;
            llvm::Value *calledOp = callBase->getCalledOperand();
            if (auto *vfnLoad = llvm::dyn_cast<llvm::LoadInst>(calledOp)) {
              slotLoad = vfnLoad;
              if (auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(
                      vfnLoad->getPointerOperand())) {
                slotGEP = gep;
                if (auto *vtLoad = llvm::dyn_cast<llvm::LoadInst>(
                        gep->getPointerOperand())) {
                  vtableLoad = vtLoad;
                  insertPt = gep; // insert before slot GEP / load
                }
              }
            }

            // Fallback: create a fresh vtable load from this pointer.
            llvm::IRBuilder<> irb(insertPt);
            llvm::Value *vtable = vtableLoad;
            if (!vtable)
              vtable = irb.CreateLoad(
                  llvm::PointerType::getUnqual(callBase->getContext()),
                  thisArg, "vtable");

            llvm::Metadata *md = nullptr;
            if (isCFIVCall) {
              md = llvm::MDString::get(callBase->getContext(),
                                       typeIdAttr.getValue());
            } else if (useDistinctTypeId) {
              auto &cached = translationCaches().hiddenTypeIdCache[typeIdAttr.getValue().str()];
              if (!cached)
                cached = llvm::MDNode::getDistinct(callBase->getContext(), {});
              md = cached;
            } else {
              md = llvm::MDString::get(callBase->getContext(),
                                       typeIdAttr.getValue());
            }
            llvm::Value *typeId =
                llvm::MetadataAsValue::get(callBase->getContext(), md);
            llvm::MDNode *noSanMD =
                callBase->getMetadata(llvm::LLVMContext::MD_nosanitize);
            auto setNoSanitize = [&](llvm::Instruction *instruction) {
              if (instruction && noSanMD)
                instruction->setMetadata(llvm::LLVMContext::MD_nosanitize,
                                         noSanMD);
            };
            auto stripCallNoSanitize = [&]() {
              if (noSanMD)
                callBase->setMetadata(llvm::LLVMContext::MD_nosanitize,
                                      nullptr);
            };

            if (useTypeCheckedLoad) {
              if (!slotGEP || !slotLoad)
                continue;

              llvm::APInt offsetBits(64, 0, /*isSigned=*/false);
              if (!slotGEP->accumulateConstantOffset(
                      callBase->getModule()->getDataLayout(), offsetBits))
                continue;
              uint64_t byteOffset = offsetBits.getZExtValue();

              llvm::Function *checkedLoadFn =
                  llvm::Intrinsic::getOrInsertDeclaration(
                      callBase->getModule(), llvm::Intrinsic::type_checked_load);
              llvm::Value *checkedLoad = irb.CreateCall(
                  checkedLoadFn,
                  {vtable, llvm::ConstantInt::get(
                               llvm::Type::getInt32Ty(callBase->getContext()),
                               byteOffset),
                   typeId});
              llvm::Value *checkedFn =
                  irb.CreateExtractValue(checkedLoad, {0});
              llvm::Value *checkedOk =
                  irb.CreateExtractValue(checkedLoad, {1});
              setNoSanitize(llvm::dyn_cast<llvm::Instruction>(checkedLoad));
              setNoSanitize(llvm::dyn_cast<llvm::Instruction>(checkedFn));
              setNoSanitize(llvm::dyn_cast<llvm::Instruction>(checkedOk));
              callBase->setCalledOperand(checkedFn);
              slotLoad->replaceAllUsesWith(checkedFn);
              slotLoad->eraseFromParent();
              if (slotGEP->use_empty())
                slotGEP->eraseFromParent();
              stripCallNoSanitize();
              if (!isCFIVCall || cfiVCallKindAttr.getValue() != "trap")
                continue;

              auto *callInst = llvm::dyn_cast<llvm::CallInst>(callBase);
              if (!callInst)
                continue;
              llvm::BasicBlock *origBB = callInst->getParent();
              llvm::BasicBlock *contBB =
                  origBB->splitBasicBlock(callInst->getIterator(), "cont");
              origBB->getTerminator()->eraseFromParent();
              llvm::BasicBlock *trapBB = llvm::BasicBlock::Create(
                  callBase->getContext(), "trap", origBB->getParent(), contBB);

              llvm::IRBuilder<> preBrBuilder(origBB);
              auto *branch =
                  preBrBuilder.CreateCondBr(checkedOk, contBB, trapBB);
              llvm::MDBuilder mdb(callBase->getContext());
              branch->setMetadata(
                  llvm::LLVMContext::MD_prof,
                  mdb.createBranchWeights(1024 * 1024 - 1, 1));
              setNoSanitize(branch);

              llvm::IRBuilder<> trapBuilder(trapBB);
              llvm::Function *trapFn = llvm::Intrinsic::getOrInsertDeclaration(
                  callBase->getModule(), llvm::Intrinsic::ubsantrap);
              auto *trapCall = trapBuilder.CreateCall(
                  trapFn, {llvm::ConstantInt::get(trapBuilder.getInt8Ty(), 2)});
              trapCall->addFnAttr(llvm::Attribute::NoMerge);
              trapCall->setDoesNotReturn();
              trapCall->setDoesNotThrow();
              setNoSanitize(trapCall);
              auto *unreachable = trapBuilder.CreateUnreachable();
              setNoSanitize(unreachable);
            } else {
              llvm::Intrinsic::ID iid = isCFIVCall
                                            ? llvm::Intrinsic::type_test
                                            : ((kind == "public")
                                                   ? llvm::Intrinsic::public_type_test
                                                   : llvm::Intrinsic::type_test);
              llvm::Function *typeTestFn =
                  llvm::Intrinsic::getOrInsertDeclaration(callBase->getModule(),
                                                          iid);
              llvm::Value *test = irb.CreateCall(typeTestFn, {vtable, typeId});
              setNoSanitize(llvm::dyn_cast<llvm::Instruction>(test));
              stripCallNoSanitize();
              if (!isCFIVCall) {
                llvm::Function *assumeFn =
                    llvm::Intrinsic::getOrInsertDeclaration(
                        callBase->getModule(), llvm::Intrinsic::assume);
                auto *assumeCall = llvm::dyn_cast<llvm::Instruction>(
                    irb.CreateCall(assumeFn, {test}));
                setNoSanitize(assumeCall);
                continue;
              }

              auto *callInst = llvm::dyn_cast<llvm::CallInst>(callBase);
              if (!callInst)
                continue;

              llvm::BasicBlock *origBB = callInst->getParent();
              llvm::BasicBlock *contBB =
                  origBB->splitBasicBlock(callInst->getIterator(), "cont");
              origBB->getTerminator()->eraseFromParent();

              // Move the vfn GEP + load from origBB to contBB (before
              // the call) so they only execute on the success path,
              // matching OGCG.
              if (slotGEP)
                slotGEP->moveBefore(*contBB, callInst->getIterator());
              if (slotLoad)
                slotLoad->moveBefore(*contBB, callInst->getIterator());

              llvm::Module *mod = callBase->getModule();
              const bool crossDsoCFI =
                  mod->getModuleFlag("Cross-DSO CFI") != nullptr;
              llvm::StringRef cfiKind = cfiVCallKindAttr.getValue();
              llvm::StringRef failBlockName =
                  cfiKind == "trap"
                      ? llvm::StringRef("trap")
                      : (crossDsoCFI ? llvm::StringRef("cfi.slowpath")
                                     : llvm::StringRef("handler.cfi_check_fail"));
              llvm::BasicBlock *failBB = llvm::BasicBlock::Create(
                  callBase->getContext(), failBlockName, origBB->getParent(),
                  contBB);

              llvm::IRBuilder<> preBrBuilder(origBB);
              auto *branch = preBrBuilder.CreateCondBr(test, contBB, failBB);
              llvm::MDBuilder mdb(callBase->getContext());
              branch->setMetadata(
                  llvm::LLVMContext::MD_prof,
                  mdb.createBranchWeights(1024 * 1024 - 1, 1));
              setNoSanitize(branch);

              llvm::IRBuilder<> failBuilder(failBB);
              if (cfiKind == "trap" && !crossDsoCFI) {
                llvm::Function *trapFn = llvm::Intrinsic::getOrInsertDeclaration(
                    callBase->getModule(), llvm::Intrinsic::ubsantrap);
                auto *trapCall = failBuilder.CreateCall(
                    trapFn,
                    {llvm::ConstantInt::get(failBuilder.getInt8Ty(), 2)});
                trapCall->addFnAttr(llvm::Attribute::NoMerge);
                trapCall->setDoesNotReturn();
                trapCall->setDoesNotThrow();
                setNoSanitize(trapCall);
                auto *unreachable = failBuilder.CreateUnreachable();
                setNoSanitize(unreachable);
                continue;
              }

              llvm::LLVMContext &ctx = callBase->getContext();
              auto *int8Ty = llvm::Type::getInt8Ty(ctx);
              auto *int32Ty = llvm::Type::getInt32Ty(ctx);
              auto *int64Ty = llvm::Type::getInt64Ty(ctx);

              llvm::Constant *filenameGV = llvm::Constant::getNullValue(
                  llvm::PointerType::getUnqual(ctx));
              int line = 0;
              int column = 0;
              if (sourceFileAttr && sourceLineAttr && sourceColumnAttr) {
                llvm::Constant *filenameInit =
                    llvm::ConstantDataArray::getString(
                        ctx, sourceFileAttr.getValue());
                auto *gv = new llvm::GlobalVariable(
                    *mod, filenameInit->getType(), true,
                    llvm::GlobalVariable::PrivateLinkage, filenameInit, ".src");
                gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
                filenameGV = gv;
                line = static_cast<int>(sourceLineAttr.getInt());
                column = static_cast<int>(sourceColumnAttr.getInt());
              }
              llvm::Constant *srcLocData[] = {
                  filenameGV, llvm::ConstantInt::get(int32Ty, line),
                  llvm::ConstantInt::get(int32Ty, column)};
              auto *srcLocStruct = llvm::ConstantStruct::getAnon(srcLocData);

              llvm::StringRef currentTypeName =
                  typeNameAttr ? typeNameAttr.getValue() : "'unknown'";
              llvm::APInt typeKindValue =
                  typeKindAttr ? typeKindAttr.getValue().zextOrTrunc(16)
                               : llvm::APInt(16, 0xFFFF, false);
              llvm::APInt typeInfoValue =
                  typeInfoAttr ? typeInfoAttr.getValue().zextOrTrunc(16)
                               : llvm::APInt(16, 0, false);
              llvm::Constant *typeDescData[] = {
                  llvm::ConstantInt::get(ctx, typeKindValue),
                  llvm::ConstantInt::get(ctx, typeInfoValue),
                  llvm::ConstantDataArray::getString(ctx, currentTypeName)};
              auto *typeDescStruct =
                  llvm::ConstantStruct::getAnon(typeDescData);
              auto *typeDescGV = new llvm::GlobalVariable(
                  *mod, typeDescStruct->getType(), true,
                  llvm::GlobalVariable::PrivateLinkage, typeDescStruct);
              typeDescGV->setUnnamedAddr(
                  llvm::GlobalValue::UnnamedAddr::Global);

              llvm::Constant *staticData[] = {
                  llvm::ConstantInt::get(int8Ty, 0), srcLocStruct, typeDescGV};
              auto *infoStruct = llvm::ConstantStruct::getAnon(staticData);
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              if (crossDsoCFI) {
                uint64_t crossDsoTypeId = llvm::MD5Hash(typeIdAttr.getValue());
                llvm::FunctionType *slowPathFnTy = nullptr;
                llvm::FunctionCallee slowPathCallee;
                llvm::SmallVector<llvm::Value *, 3> slowPathArgs;
                if (cfiKind == "trap") {
                  slowPathFnTy = llvm::FunctionType::get(
                      llvm::Type::getVoidTy(ctx),
                      {int64Ty, llvm::PointerType::getUnqual(ctx)}, false);
                  slowPathCallee =
                      mod->getOrInsertFunction("__cfi_slowpath", slowPathFnTy);
                  slowPathArgs = {
                      llvm::ConstantInt::get(int64Ty, crossDsoTypeId), vtable};
                } else {
                  slowPathFnTy = llvm::FunctionType::get(
                      llvm::Type::getVoidTy(ctx),
                      {int64Ty, llvm::PointerType::getUnqual(ctx),
                       llvm::PointerType::getUnqual(ctx)},
                      false);
                  slowPathCallee = mod->getOrInsertFunction(
                      "__cfi_slowpath_diag", slowPathFnTy);
                  slowPathArgs = {
                      llvm::ConstantInt::get(int64Ty, crossDsoTypeId), vtable,
                      infoGV};
                }
                auto *slowPathFn =
                    llvm::cast<llvm::Function>(slowPathCallee.getCallee());
                auto *slowPathCall = failBuilder.CreateCall(
                    slowPathFnTy, slowPathFn, slowPathArgs);
                slowPathCall->setDoesNotThrow();
                setNoSanitize(slowPathCall);
                auto *recoverBr = failBuilder.CreateBr(contBB);
                setNoSanitize(recoverBr);
                continue;
              }

              llvm::Function *handlerFn = nullptr;
              llvm::FunctionType *handlerFnTy = nullptr;
              llvm::SmallVector<llvm::Value *, 3> handlerArgs;
              if (cfiMinimalRuntime) {
                std::string handlerName;
                if (cfiKind == "abort")
                  handlerName = "__ubsan_handle_cfi_check_fail_minimal_abort";
                else if (cfiPreserveAllRegs)
                  handlerName =
                      "__ubsan_handle_cfi_check_fail_minimal_preserve";
                else
                  handlerName = "__ubsan_handle_cfi_check_fail_minimal";
                handlerFnTy =
                    llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {}, false);
                auto handlerCallee =
                    mod->getOrInsertFunction(handlerName, handlerFnTy);
                handlerFn = llvm::cast<llvm::Function>(handlerCallee.getCallee());
                if (cfiPreserveAllRegs && cfiKind != "abort") {
                  handlerFn->setCallingConv(llvm::CallingConv::PreserveAll);
                }
              } else {
                std::string handlerName = "__ubsan_handle_cfi_check_fail";
                if (cfiKind == "abort")
                  handlerName += "_abort";
                handlerFnTy = llvm::FunctionType::get(
                    llvm::Type::getVoidTy(ctx),
                    {llvm::PointerType::getUnqual(ctx), int64Ty, int64Ty},
                    false);
                auto handlerCallee =
                    mod->getOrInsertFunction(handlerName, handlerFnTy);
                handlerFn = llvm::cast<llvm::Function>(handlerCallee.getCallee());

                llvm::Value *allVtablesTypeId = llvm::MetadataAsValue::get(
                    ctx, llvm::MDString::get(ctx, "all-vtables"));
                llvm::Function *allVtablesTestFn =
                    llvm::Intrinsic::getOrInsertDeclaration(
                        mod, llvm::Intrinsic::type_test);
                llvm::Value *validVtable = failBuilder.CreateCall(
                    allVtablesTestFn, {vtable, allVtablesTypeId});
                llvm::Value *vtableInt = failBuilder.CreatePtrToInt(vtable, int64Ty);
                llvm::Value *validVtableInt =
                    failBuilder.CreateZExt(validVtable, int64Ty);
                setNoSanitize(llvm::dyn_cast<llvm::Instruction>(validVtable));
                setNoSanitize(llvm::dyn_cast<llvm::Instruction>(vtableInt));
                setNoSanitize(llvm::dyn_cast<llvm::Instruction>(validVtableInt));
                handlerArgs = {infoGV, vtableInt, validVtableInt};
              }

              handlerFn->setUWTableKind(llvm::UWTableKind::Default);
              if (cfiKind == "abort") {
                handlerFn->setDoesNotReturn();
                handlerFn->setDoesNotThrow();
              }

              auto *handlerCall =
                  failBuilder.CreateCall(handlerFnTy, handlerFn, handlerArgs);
              if (cfiPreserveAllRegs && cfiMinimalRuntime && cfiKind != "abort")
                handlerCall->setCallingConv(llvm::CallingConv::PreserveAll);
              handlerCall->setDoesNotThrow();
              handlerCall->addFnAttr(llvm::Attribute::NoMerge);
              if (cfiKind == "abort")
                handlerCall->setDoesNotReturn();
              setNoSanitize(handlerCall);

              if (cfiKind == "abort") {
                auto *unreachable = failBuilder.CreateUnreachable();
                setNoSanitize(unreachable);
              } else {
                auto *recoverBr = failBuilder.CreateBr(contBB);
                setNoSanitize(recoverBr);
              }
            }
          }
        }
      }
      // Forward !nosanitize metadata to call instructions.
      if (attrName == "cir.nosanitize") {
        for (auto *callBase : callBases)
          callBase->setNoSanitizeMetadata();
      }
      // Rewrite CIR UBSan stubs to real UBSan handler calls with proper
      // static data globals.
      if (attrName == "cir.ubsan.handler_name") {
        auto handlerNameAttr =
            mlir::dyn_cast<mlir::StringAttr>(attribute.getValue());
        if (handlerNameAttr) {
          llvm::StringRef handlerName = handlerNameAttr.getValue();
          auto sourceFile =
              op->getAttrOfType<mlir::StringAttr>("cir.ubsan.source_file");
          auto sourceLine =
              op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.source_line");
          auto sourceColumn =
              op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.source_column");
          auto typeNameAttr =
              op->getAttrOfType<mlir::StringAttr>("cir.ubsan.type_name");
          auto typeKindAttr =
              op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.type_kind");
          auto typeInfoAttr =
              op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.type_info");
          auto srcTypeNameAttr =
              op->getAttrOfType<mlir::StringAttr>("cir.ubsan.src_type_name");
          auto srcTypeKindAttr = op->getAttrOfType<mlir::IntegerAttr>(
              "cir.ubsan.src_type_kind");
          auto srcTypeInfoAttr = op->getAttrOfType<mlir::IntegerAttr>(
              "cir.ubsan.src_type_info");
          auto dstTypeNameAttr =
              op->getAttrOfType<mlir::StringAttr>("cir.ubsan.dst_type_name");
          auto dstTypeKindAttr = op->getAttrOfType<mlir::IntegerAttr>(
              "cir.ubsan.dst_type_kind");
          auto dstTypeInfoAttr = op->getAttrOfType<mlir::IntegerAttr>(
              "cir.ubsan.dst_type_info");
          auto implicitConversionCheckKindAttr =
              op->getAttrOfType<mlir::IntegerAttr>(
                  "cir.ubsan.implicit_conversion_check_kind");
          auto attrSourceFile =
              op->getAttrOfType<mlir::StringAttr>("cir.ubsan.attr.source_file");
          auto attrSourceLine = op->getAttrOfType<mlir::IntegerAttr>(
              "cir.ubsan.attr.source_line");
          auto attrSourceColumn = op->getAttrOfType<mlir::IntegerAttr>(
              "cir.ubsan.attr.source_column");
          auto argIndexAttr =
              op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.arg_index");
          auto tckAttr = op->getAttrOfType<mlir::IntegerAttr>(
              "cir.ubsan.type_check_kind");
          auto handlerKindAttr =
              op->getAttrOfType<mlir::StringAttr>("cir.ubsan.handler_kind");
          bool cfiMFCallMinimalRuntime =
              op->hasAttr("cir.cfi.mfcall.minimal_runtime");
          bool cfiMFCallPreserveAllRegs =
              op->hasAttr("cir.cfi.mfcall.preserve_all_regs");
          llvm::StringRef handlerKind =
              handlerKindAttr ? handlerKindAttr.getValue() : "abort";

          for (auto *callBase : callBases) {
            auto *calledFn = callBase->getCalledFunction();
            if (!calledFn)
              continue;
            llvm::StringRef calleeName = calledFn->getName();
            if (calleeName != "__cir_ubsan_type_mismatch_handler" &&
                calleeName != "__cir_ubsan_vptr_check" &&
                calleeName != "__cir_ubsan_builtin_unreachable" &&
                calleeName != "__cir_ubsan_invalid_builtin" &&
                calleeName != "__cir_ubsan_missing_return" &&
                calleeName != "__cir_ubsan_nonnull_return" &&
                calleeName != "__cir_ubsan_load_invalid_value_check" &&
                calleeName != "__cir_ubsan_vla_bound_not_positive" &&
                calleeName != "__cir_ubsan_function_type_mismatch_handler" &&
                calleeName != "__cir_ubsan_nonnull_arg_check" &&
                calleeName != "__cir_ubsan_nonnull_return_check" &&
                calleeName != "__cir_ubsan_pointer_overflow_check" &&
                calleeName != "__cir_ubsan_overflow_handler" &&
                calleeName != "__cir_ubsan_overflow_handler_negate" &&
                calleeName != "__cir_ubsan_float_cast_overflow" &&
                calleeName != "__cir_ubsan_nonnull_arg" &&
                calleeName != "__cir_ubsan_nonnull_return" &&
                calleeName != "__cir_ubsan_cfi_check_fail" &&
                calleeName != "__cir_ubsan_cfi_check_fail_with_data" &&
                calleeName != "__cir_ubsan_shift_out_of_bounds" &&
                calleeName != "__cir_ubsan_shift_out_of_bounds_abort" &&
                calleeName != "__cir_ubsan_implicit_conversion" &&
                calleeName != "__cir_ubsan_implicit_conversion_abort" &&
                calleeName != "__cir_ubsan_out_of_bounds" &&
                calleeName != "__cir_ubsan_alignment_assumption_handler")
              continue;

            llvm::Module *mod = calledFn->getParent();
            auto &ctx = callBase->getContext();
            llvm::CallingConv::ID runtimeCallConv = calledFn->getCallingConv();
            llvm::IRBuilder<> irb(callBase);
            auto *intptrTy = irb.getIntPtrTy(mod->getDataLayout());
            auto *int8Ty = irb.getInt8Ty();
            auto *int32Ty = irb.getInt32Ty();
            // UBSan info/data globals are emitted in the module's default
            // globals address space (e.g. AS1 on AMDGCN), so each handler's
            // data-pointer parameter must use that address space too; otherwise
            // the call argument and parameter pointer types mismatch.
            auto *globalsPtrTy = llvm::PointerType::get(
                ctx, mod->getDataLayout().getDefaultGlobalsAddressSpace());
            auto inferOverflowHandlerKind = [&](mlir::Operation *callOp) {
              for (mlir::Operation *nextOp = callOp->getNextNode(); nextOp;
                   nextOp = nextOp->getNextNode()) {
                if (nextOp->getName().getStringRef().starts_with(
                        "llvm.intr.dbg."))
                  continue;
                if (llvm::isa<mlir::LLVM::UnreachableOp>(nextOp))
                  return llvm::StringRef("abort");
                return llvm::StringRef("recover");
              }
              return handlerKind;
            };
            llvm::StringRef effectiveHandlerKind = handlerKind;

            if (handlerName == "dynamic_type_cache_miss") {
              auto rttiNameAttr =
                  op->getAttrOfType<mlir::StringAttr>("cir.ubsan.rtti_name");
              auto typeHashAttr =
                  op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.type_hash");
              llvm::MDBuilder mdb(ctx);
              auto *lineConst =
                  llvm::ConstantInt::get(int32Ty,
                                         sourceLine ? sourceLine.getInt() : 0);
              auto *columnConst = llvm::ConstantInt::get(
                  int32Ty, sourceColumn ? sourceColumn.getInt() : 0);
              auto *tckConst = llvm::ConstantInt::get(int8Ty, tckAttr
                                                                   ? tckAttr.getInt()
                                                                   : 0);
              auto *typeHashConst = llvm::ConstantInt::get(
                  irb.getInt64Ty(),
                  typeHashAttr ? static_cast<uint64_t>(typeHashAttr.getInt())
                               : 0);
              callBase->setMetadata(
                  "cir.ubsan.vptr.pending",
                  llvm::MDNode::get(
                      ctx,
                      {llvm::MDString::get(
                           ctx,
                           sourceFile ? sourceFile.getValue() : llvm::StringRef()),
                       mdb.createConstant(lineConst),
                       mdb.createConstant(columnConst),
                       llvm::MDString::get(
                           ctx, typeNameAttr ? typeNameAttr.getValue()
                                             : llvm::StringRef("'unknown'")),
                       mdb.createConstant(tckConst),
                       mdb.createConstant(typeHashConst),
                       llvm::MDString::get(
                           ctx, rttiNameAttr ? rttiNameAttr.getValue()
                                             : llvm::StringRef()),
                       llvm::MDString::get(ctx, handlerKind)}));
              continue;
            }

            // Build source location constant.
            llvm::Constant *filenameGV;
            int line = 0, column = 0;
            if (sourceFile && sourceLine && sourceColumn) {
              filenameGV =
                  getOrCreateUBSanSourceFileGlobal(*mod, sourceFile.getValue());
              line = static_cast<int>(sourceLine.getInt());
              column = static_cast<int>(sourceColumn.getInt());
            } else {
              filenameGV = llvm::Constant::getNullValue(
                  llvm::PointerType::getUnqual(ctx));
            }
            llvm::Constant *srcLocData[] = {
                filenameGV, llvm::ConstantInt::get(int32Ty, line),
                llvm::ConstantInt::get(int32Ty, column)};
            auto *srcLocStruct = llvm::ConstantStruct::getAnon(srcLocData);

            auto getOrCreateTypeDescGVFromAttrs =
                [&](mlir::StringAttr nameAttr, mlir::IntegerAttr kindAttr,
                    mlir::IntegerAttr infoAttr) -> llvm::GlobalVariable * {
              llvm::StringRef currentTypeName =
                  nameAttr ? nameAttr.getValue() : "'unknown'";
              uint16_t typeKind =
                  kindAttr ? static_cast<uint16_t>(kindAttr.getInt()) : 0xFFFF;
              uint16_t typeInfo =
                  infoAttr ? static_cast<uint16_t>(infoAttr.getInt()) : 0;
              return getOrCreateUBSanTypeDescriptorGlobal(
                  *mod, currentTypeName, typeKind, typeInfo);
            };

            llvm::GlobalVariable *typeDescGV = nullptr;
            auto getTypeDescGV = [&]() -> llvm::GlobalVariable * {
              if (typeDescGV)
                return typeDescGV;
              typeDescGV = getOrCreateTypeDescGVFromAttrs(typeNameAttr,
                                                          typeKindAttr,
                                                          typeInfoAttr);
              return typeDescGV;
            };

            uint8_t tck = tckAttr ? static_cast<uint8_t>(tckAttr.getInt()) : 0;

            if (handlerName == "cfi_check_fail") {
              bool noMergeCall = callBase->hasFnAttr(llvm::Attribute::NoMerge);
              llvm::Function *handlerFn = nullptr;
              llvm::FunctionType *handlerFnTy = nullptr;
              llvm::SmallVector<llvm::Value *, 3> handlerArgs;
              if (cfiMFCallMinimalRuntime) {
                std::string realHandlerName;
                if (handlerKind == "abort")
                  realHandlerName =
                      "__ubsan_handle_cfi_check_fail_minimal_abort";
                else if (cfiMFCallPreserveAllRegs)
                  realHandlerName =
                      "__ubsan_handle_cfi_check_fail_minimal_preserve";
                else
                  realHandlerName = "__ubsan_handle_cfi_check_fail_minimal";
                handlerFnTy = llvm::FunctionType::get(
                    llvm::Type::getVoidTy(ctx), {}, false);
                auto handlerCallee =
                    mod->getOrInsertFunction(realHandlerName, handlerFnTy);
                handlerFn = llvm::cast<llvm::Function>(handlerCallee.getCallee());
              } else {
                std::string realHandlerName = "__ubsan_handle_cfi_check_fail";
                if (handlerKind == "abort")
                  realHandlerName += "_abort";
                auto toI64 = [&](llvm::Value *value) -> llvm::Value * {
                  llvm::Value *converted = value;
                  if (value->getType()->isPointerTy())
                    converted = irb.CreatePtrToInt(value, irb.getInt64Ty());
                  else if (!value->getType()->isIntegerTy(64))
                    converted = irb.CreateZExtOrTrunc(value, irb.getInt64Ty());
                  if (auto *inst = llvm::dyn_cast<llvm::Instruction>(converted))
                    inst->setMetadata(llvm::LLVMContext::MD_nosanitize,
                                      llvm::MDNode::get(ctx, {}));
                  return converted;
                };
                if (callBase->arg_size() == 3) {
                  handlerFnTy = llvm::FunctionType::get(
                      llvm::Type::getVoidTy(ctx),
                      {irb.getInt64Ty(), irb.getInt64Ty(), irb.getInt64Ty()},
                      false);
                  auto handlerCallee =
                      mod->getOrInsertFunction(realHandlerName, handlerFnTy);
                  handlerFn =
                      llvm::cast<llvm::Function>(handlerCallee.getCallee());
                  handlerArgs = {
                      toI64(callBase->getArgOperand(0)),
                      toI64(callBase->getArgOperand(1)),
                      toI64(callBase->getArgOperand(2))};
                } else {
                  handlerFnTy = llvm::FunctionType::get(
                      llvm::Type::getVoidTy(ctx),
                      {globalsPtrTy, irb.getInt64Ty(),
                       irb.getInt64Ty()},
                      false);
                  auto handlerCallee =
                      mod->getOrInsertFunction(realHandlerName, handlerFnTy);
                  handlerFn =
                      llvm::cast<llvm::Function>(handlerCallee.getCallee());
                  llvm::Constant *staticData[] = {
                      llvm::ConstantInt::get(int8Ty, tck), srcLocStruct,
                      getTypeDescGV()};
                  auto *infoStruct = llvm::ConstantStruct::getAnon(staticData);
                  auto *infoGV = new llvm::GlobalVariable(
                      *mod, infoStruct->getType(), false,
                      llvm::GlobalVariable::PrivateLinkage, infoStruct);
                  infoGV->setUnnamedAddr(
                      llvm::GlobalValue::UnnamedAddr::Global);

                  llvm::Value *checkedValue =
                      toI64(callBase->getArgOperand(0));
                  llvm::Value *validVtableOrUndef =
                      tck == 5 ? llvm::UndefValue::get(irb.getInt64Ty())
                               : toI64(callBase->getArgOperand(1));
                  handlerArgs = {infoGV, checkedValue, validVtableOrUndef};
                }

                llvm::AttrBuilder handlerAttrs(ctx);
                handlerAttrs.addUWTableAttr(llvm::UWTableKind::Default);
                handlerFn->setAttributes(llvm::AttributeList::get(
                    ctx, llvm::AttributeList::FunctionIndex, handlerAttrs));
              }

              handlerFn->setUWTableKind(llvm::UWTableKind::Default);
              if (handlerKind == "abort") {
                handlerFn->setDoesNotThrow();
                handlerFn->setDoesNotReturn();
              }

              auto *handlerCall =
                  irb.CreateCall(handlerFnTy, handlerFn, handlerArgs);
              handlerCall->setMetadata(llvm::LLVMContext::MD_nosanitize,
                                       llvm::MDNode::get(ctx, {}));
              if (noMergeCall)
                handlerCall->addFnAttr(llvm::Attribute::NoMerge);
              handlerCall->setDoesNotThrow();
              if (handlerKind == "abort")
                handlerCall->setDoesNotReturn();
              if (cfiMFCallPreserveAllRegs && cfiMFCallMinimalRuntime &&
                  handlerKind != "abort")
                handlerCall->setCallingConv(llvm::CallingConv::PreserveAll);
              callBase->eraseFromParent();
              continue;
            }

            auto setUnaryUBSanHandlerAttrs = [&](llvm::Function *handlerFn,
                                                llvm::StringRef kind) {
              llvm::AttrBuilder handlerAttrs(ctx);
              handlerAttrs.addUWTableAttr(llvm::UWTableKind::Default);
              if (kind == "abort") {
                handlerAttrs.addAttribute(llvm::Attribute::NoReturn);
                handlerAttrs.addAttribute(llvm::Attribute::NoUnwind);
              }
              handlerFn->setAttributes(llvm::AttributeList::get(
                  ctx, llvm::AttributeList::FunctionIndex, handlerAttrs));
            };
            auto setUnaryUBSanCallAttrs = [&](llvm::CallBase *handlerCall,
                                              llvm::StringRef kind) {
              handlerCall->setMetadata(llvm::LLVMContext::MD_nosanitize,
                                       llvm::MDNode::get(ctx, {}));
              handlerCall->addFnAttr(llvm::Attribute::NoMerge);
              if (kind == "abort") {
                handlerCall->addFnAttr(llvm::Attribute::NoReturn);
                handlerCall->addFnAttr(llvm::Attribute::NoUnwind);
              } else {
                handlerCall->addFnAttr(llvm::Attribute::NoUnwind);
              }
            };

            if (handlerName == "builtin_unreachable") {
              auto *infoStruct = llvm::ConstantStruct::getAnon({srcLocStruct});
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy}, false);
              auto handlerCallee = mod->getOrInsertFunction(
                  "__ubsan_handle_builtin_unreachable", handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              setUnaryUBSanHandlerAttrs(handlerFn, "abort");

              auto *handlerCall =
                  irb.CreateCall(handlerFnTy, handlerFn, {infoGV});
              setUnaryUBSanCallAttrs(handlerCall, "abort");
              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "invalid_builtin") {
              auto builtinCheckKindAttr =
                  op->getAttrOfType<mlir::IntegerAttr>(
                      "cir.ubsan.builtin_check_kind");
              uint8_t builtinCheckKind =
                  builtinCheckKindAttr
                      ? static_cast<uint8_t>(builtinCheckKindAttr.getInt())
                      : 0;
              llvm::Constant *staticData[] = {
                  srcLocStruct,
                  llvm::ConstantInt::get(int8Ty, builtinCheckKind)};
              auto *infoStruct = llvm::ConstantStruct::getAnon(staticData);
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              std::string realHandlerName =
                  "__ubsan_handle_invalid_builtin";
              if (handlerKind == "abort")
                realHandlerName += "_abort";

              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy}, false);
              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              setUnaryUBSanHandlerAttrs(handlerFn, handlerKind);

              auto *handlerCall =
                  irb.CreateCall(handlerFnTy, handlerFn, {infoGV});
              setUnaryUBSanCallAttrs(handlerCall, handlerKind);
              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "missing_return") {
              auto *infoStruct = llvm::ConstantStruct::getAnon({srcLocStruct});
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy}, false);
              auto handlerCallee = mod->getOrInsertFunction(
                  "__ubsan_handle_missing_return", handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              setUnaryUBSanHandlerAttrs(handlerFn, "abort");

              auto *handlerCall =
                  irb.CreateCall(handlerFnTy, handlerFn, {infoGV});
              setUnaryUBSanCallAttrs(handlerCall, "abort");
              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "nonnull_return" &&
                       calleeName == "__cir_ubsan_nonnull_return") {
              llvm::Constant *attrFilenameGV;
              int attrLine = 0, attrColumn = 0;
              if (attrSourceFile && attrSourceLine && attrSourceColumn) {
                attrFilenameGV = getOrCreateUBSanSourceFileGlobal(
                    *mod, attrSourceFile.getValue());
                attrLine = static_cast<int>(attrSourceLine.getInt());
                attrColumn = static_cast<int>(attrSourceColumn.getInt());
              } else {
                attrFilenameGV = llvm::Constant::getNullValue(
                    llvm::PointerType::getUnqual(ctx));
              }

              llvm::Constant *attrLocData[] = {
                  attrFilenameGV, llvm::ConstantInt::get(int32Ty, attrLine),
                  llvm::ConstantInt::get(int32Ty, attrColumn)};
              auto *attrLocStruct = llvm::ConstantStruct::getAnon(attrLocData);
              auto *infoStruct =
                  llvm::ConstantStruct::getAnon({attrLocStruct});
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              std::string realHandlerName = "__ubsan_handle_nonnull_return_v1";
              if (handlerKind == "abort")
                realHandlerName += "_abort";

              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy, intptrTy}, false);
              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              handlerFn->setCallingConv(runtimeCallConv);
              llvm::AttrBuilder handlerAttrs(ctx);
              handlerAttrs.addUWTableAttr(llvm::UWTableKind::Default);
              if (handlerKind == "abort") {
                handlerAttrs.addAttribute(llvm::Attribute::NoReturn);
                handlerAttrs.addAttribute(llvm::Attribute::NoUnwind);
              }
              handlerFn->setAttributes(llvm::AttributeList::get(
                  ctx, llvm::AttributeList::FunctionIndex, handlerAttrs));

              auto *dynamicLoc = callBase->getArgOperand(0);
              auto *handlerCall = irb.CreateCall(
                  handlerFnTy, handlerFn, {infoGV, dynamicLoc});
              handlerCall->setMetadata(llvm::LLVMContext::MD_nosanitize,
                                       llvm::MDNode::get(ctx, {}));
              handlerCall->addFnAttr(llvm::Attribute::NoMerge);
              if (handlerKind == "abort") {
                handlerCall->addFnAttr(llvm::Attribute::NoReturn);
                handlerCall->addFnAttr(llvm::Attribute::NoUnwind);
              } else {
                handlerCall->addFnAttr(llvm::Attribute::NoUnwind);
              }
              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "negate_overflow") {
              // Unary negation overflow: handler takes (ptr, intptr) — one
              // dynamic value (the operand), matching OGCG's
              // __ubsan_handle_negate_overflow{,_abort}.
              effectiveHandlerKind = inferOverflowHandlerKind(op);
              llvm::Constant *staticData[] = {srcLocStruct, getTypeDescGV()};
              auto *infoStruct = llvm::ConstantStruct::getAnon(staticData);
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              std::string realHandlerName =
                  "__ubsan_handle_negate_overflow";
              if (effectiveHandlerKind == "abort")
                realHandlerName += "_abort";

              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy, intptrTy},
                  false);
              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              llvm::AttrBuilder handlerAttrs(ctx);
              handlerAttrs.addUWTableAttr(llvm::UWTableKind::Default);
              if (effectiveHandlerKind == "abort") {
                handlerAttrs.addAttribute(llvm::Attribute::NoReturn);
                handlerAttrs.addAttribute(llvm::Attribute::NoUnwind);
              }
              handlerFn->setAttributes(llvm::AttributeList::get(
                  ctx, llvm::AttributeList::FunctionIndex, handlerAttrs));

              // Negate handler takes only one dynamic value (the operand).
              auto *val = callBase->getArgOperand(0);
              auto *handlerCall =
                  irb.CreateCall(handlerFnTy, handlerFn, {infoGV, val});
              handlerCall->setMetadata(llvm::LLVMContext::MD_nosanitize,
                                       llvm::MDNode::get(ctx, {}));
              handlerCall->addFnAttr(llvm::Attribute::NoUnwind);
              handlerCall->addFnAttr(llvm::Attribute::NoMerge);
              if (effectiveHandlerKind == "abort")
                handlerCall->addFnAttr(llvm::Attribute::NoReturn);
              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "add_overflow" ||
                       handlerName == "sub_overflow" ||
                       handlerName == "mul_overflow" ||
                       handlerName == "divrem_overflow") {
              // In the direct CIR pipeline, recover-vs-abort is encoded in the
              // CFG shape after the stub call. Use that shape here instead of
              // trusting the forwarded string attr alone, so both clang
              // -emit-llvm -fclangir and cir-translate agree on the real UBSan
              // handler variant to materialize.
              effectiveHandlerKind = inferOverflowHandlerKind(op);
              llvm::Constant *staticData[] = {srcLocStruct, getTypeDescGV()};
              auto *infoStruct = llvm::ConstantStruct::getAnon(staticData);
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              std::string realHandlerName = "__ubsan_handle_";
              realHandlerName += handlerName.str();
              if (effectiveHandlerKind == "abort")
                realHandlerName += "_abort";

              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy, intptrTy, intptrTy},
                  false);
              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              llvm::AttrBuilder handlerAttrs(ctx);
              handlerAttrs.addUWTableAttr(llvm::UWTableKind::Default);
              if (effectiveHandlerKind == "abort") {
                handlerAttrs.addAttribute(llvm::Attribute::NoReturn);
                handlerAttrs.addAttribute(llvm::Attribute::NoUnwind);
              }
              handlerFn->setAttributes(llvm::AttributeList::get(
                  ctx, llvm::AttributeList::FunctionIndex, handlerAttrs));

              auto *lhs = callBase->getArgOperand(0);
              auto *rhs = callBase->getArgOperand(1);
              auto *handlerCall =
                  irb.CreateCall(handlerFnTy, handlerFn, {infoGV, lhs, rhs});
              handlerCall->setMetadata(llvm::LLVMContext::MD_nosanitize,
                                       llvm::MDNode::get(ctx, {}));
              handlerCall->addFnAttr(llvm::Attribute::NoUnwind);
              handlerCall->addFnAttr(llvm::Attribute::NoMerge);
              if (effectiveHandlerKind == "abort")
                handlerCall->addFnAttr(llvm::Attribute::NoReturn);
              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "type_mismatch") {
              // Build TypeMismatchData: {SourceLocation, TypeDescriptor*,
              //                          log2_alignment, type_check_kind}
              auto log2AlignAttr = op->getAttrOfType<mlir::IntegerAttr>(
                  "cir.ubsan.log2_alignment");
              uint8_t log2Align =
                  log2AlignAttr
                      ? static_cast<uint8_t>(log2AlignAttr.getInt())
                      : 1;
              llvm::Constant *staticData[] = {
                  srcLocStruct, getTypeDescGV(),
                  llvm::ConstantInt::get(int8Ty, log2Align),
                  llvm::ConstantInt::get(int8Ty, tck)};
              auto *infoStruct = llvm::ConstantStruct::getAnon(staticData);
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              // Determine the handler function name.
              std::string realHandlerName =
                  "__ubsan_handle_type_mismatch_v1";
              if (handlerKind == "abort")
                realHandlerName += "_abort";

              // Declare or get the handler function.
              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy, intptrTy}, false);
              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              setUnaryUBSanHandlerAttrs(handlerFn, handlerKind);

              // Replace the stub call with the real handler call.
              // The stub has 1 arg (ptr_as_int); the handler needs 2
              // (infoGV, ptr_as_int).
              auto *ptrAsInt = callBase->getArgOperand(0);
              auto *handlerCall = irb.CreateCall(
                  handlerFnTy, handlerFn, {infoGV, ptrAsInt});
              setUnaryUBSanCallAttrs(handlerCall, handlerKind);
              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "load_invalid_value") {
              llvm::Constant *staticData[] = {srcLocStruct, getTypeDescGV()};
              auto *infoStruct = llvm::ConstantStruct::getAnon(staticData);
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              std::string realHandlerName =
                  "__ubsan_handle_load_invalid_value";
              if (handlerKind == "abort")
                realHandlerName += "_abort";

              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy, intptrTy}, false);
              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              setUnaryUBSanHandlerAttrs(handlerFn, handlerKind);

              auto *dynamicValue = callBase->getArgOperand(0);
              auto *handlerCall = irb.CreateCall(
                  handlerFnTy, handlerFn, {infoGV, dynamicValue});
              setUnaryUBSanCallAttrs(handlerCall, handlerKind);

              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "float_cast_overflow") {
              auto lhsTypeNameAttr =
                  op->getAttrOfType<mlir::StringAttr>("cir.ubsan.lhs_type_name");
              auto lhsTypeKindAttr =
                  op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.lhs_type_kind");
              auto lhsTypeInfoAttr =
                  op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.lhs_type_info");
              auto rhsTypeNameAttr =
                  op->getAttrOfType<mlir::StringAttr>("cir.ubsan.rhs_type_name");
              auto rhsTypeKindAttr =
                  op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.rhs_type_kind");
              auto rhsTypeInfoAttr =
                  op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.rhs_type_info");

              auto *srcTypeDescGV = getOrCreateTypeDescGVFromAttrs(
                  lhsTypeNameAttr, lhsTypeKindAttr, lhsTypeInfoAttr);
              auto *dstTypeDescGV = getOrCreateTypeDescGVFromAttrs(
                  rhsTypeNameAttr, rhsTypeKindAttr, rhsTypeInfoAttr);

              llvm::Constant *staticData[] = {srcLocStruct, srcTypeDescGV,
                                              dstTypeDescGV};
              auto *infoStruct = llvm::ConstantStruct::getAnon(staticData);
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              std::string realHandlerName =
                  "__ubsan_handle_float_cast_overflow";
              if (handlerKind == "abort")
                realHandlerName += "_abort";

              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy, intptrTy}, false);
              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              setUnaryUBSanHandlerAttrs(handlerFn, handlerKind);

              auto *dynamicValue = callBase->getArgOperand(0);
              auto *handlerCall = irb.CreateCall(
                  handlerFnTy, handlerFn, {infoGV, dynamicValue});
              setUnaryUBSanCallAttrs(handlerCall, handlerKind);

              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "vla_bound_not_positive") {
              llvm::Constant *staticData[] = {srcLocStruct, getTypeDescGV()};
              auto *infoStruct = llvm::ConstantStruct::getAnon(staticData);
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              std::string realHandlerName =
                  "__ubsan_handle_vla_bound_not_positive";
              if (handlerKind == "abort")
                realHandlerName += "_abort";

              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy, intptrTy}, false);
              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              setUnaryUBSanHandlerAttrs(handlerFn, handlerKind);

              auto *dynamicValue = callBase->getArgOperand(0);
              auto *handlerCall = irb.CreateCall(
                  handlerFnTy, handlerFn, {infoGV, dynamicValue});
              setUnaryUBSanCallAttrs(handlerCall, handlerKind);

              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "function_type_mismatch") {
              llvm::Constant *staticData[] = {srcLocStruct, getTypeDescGV()};
              auto *infoStruct = llvm::ConstantStruct::getAnon(staticData);
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              std::string realHandlerName =
                  "__ubsan_handle_function_type_mismatch";
              if (handlerKind == "abort")
                realHandlerName += "_abort";

              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy, intptrTy}, false);
              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              llvm::AttrBuilder handlerAttrs(ctx);
              handlerAttrs.addUWTableAttr(llvm::UWTableKind::Default);
              if (handlerKind == "abort") {
                handlerAttrs.addAttribute(llvm::Attribute::NoReturn);
                handlerAttrs.addAttribute(llvm::Attribute::NoUnwind);
              }
              handlerFn->setAttributes(llvm::AttributeList::get(
                  ctx, llvm::AttributeList::FunctionIndex, handlerAttrs));

              auto *functionValue = callBase->getArgOperand(0);
              auto *handlerCall = irb.CreateCall(
                  handlerFnTy, handlerFn, {infoGV, functionValue});
              handlerCall->setCallingConv(runtimeCallConv);
              handlerCall->setMetadata(
                  llvm::LLVMContext::MD_nosanitize,
                  llvm::MDNode::get(ctx, {}));
              handlerCall->addFnAttr(llvm::Attribute::NoMerge);
              if (handlerKind == "abort") {
                handlerCall->addFnAttr(llvm::Attribute::NoReturn);
                handlerCall->addFnAttr(llvm::Attribute::NoUnwind);
              } else {
                handlerCall->addFnAttr(llvm::Attribute::NoUnwind);
              }

              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "nonnull_arg" &&
                       calleeName == "__cir_ubsan_nonnull_arg") {
              llvm::Constant *attrFilenameGV;
              int attrLine = 0, attrColumn = 0;
              if (attrSourceFile && attrSourceLine && attrSourceColumn) {
                attrFilenameGV = getOrCreateUBSanSourceFileGlobal(
                    *mod, attrSourceFile.getValue());
                attrLine = static_cast<int>(attrSourceLine.getInt());
                attrColumn = static_cast<int>(attrSourceColumn.getInt());
              } else {
                attrFilenameGV = llvm::Constant::getNullValue(
                    llvm::PointerType::getUnqual(ctx));
              }

              llvm::Constant *attrLocData[] = {
                  attrFilenameGV, llvm::ConstantInt::get(int32Ty, attrLine),
                  llvm::ConstantInt::get(int32Ty, attrColumn)};
              auto *attrLocStruct = llvm::ConstantStruct::getAnon(attrLocData);
              auto *infoStruct = llvm::ConstantStruct::getAnon(
                  {srcLocStruct, attrLocStruct,
                   llvm::ConstantInt::get(int32Ty,
                                          argIndexAttr ? argIndexAttr.getInt()
                                                       : 0)});
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              std::string realHandlerName = "__ubsan_handle_nonnull_arg";
              if (handlerKind == "abort")
                realHandlerName += "_abort";

              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy}, false);
              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              setUnaryUBSanHandlerAttrs(handlerFn, handlerKind);

              auto *handlerCall =
                  irb.CreateCall(handlerFnTy, handlerFn, {infoGV});
              setUnaryUBSanCallAttrs(handlerCall, handlerKind);

              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "nonnull_return" ||
                       handlerName == "nullability_return") {
              llvm::Constant *attrFilenameGV;
              int attrLine = 0, attrColumn = 0;
              if (attrSourceFile && attrSourceLine && attrSourceColumn) {
                attrFilenameGV = getOrCreateUBSanSourceFileGlobal(
                    *mod, attrSourceFile.getValue());
                attrLine = static_cast<int>(attrSourceLine.getInt());
                attrColumn = static_cast<int>(attrSourceColumn.getInt());
              } else {
                attrFilenameGV = llvm::Constant::getNullValue(
                    llvm::PointerType::getUnqual(ctx));
              }

              llvm::Constant *attrLocData[] = {
                  attrFilenameGV, llvm::ConstantInt::get(int32Ty, attrLine),
                  llvm::ConstantInt::get(int32Ty, attrColumn)};
              auto *attrLocStruct = llvm::ConstantStruct::getAnon(attrLocData);
              auto *infoStruct = llvm::ConstantStruct::getAnon({attrLocStruct});
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              std::string realHandlerName =
                  handlerName == "nullability_return"
                      ? "__ubsan_handle_nullability_return_v1"
                      : "__ubsan_handle_nonnull_return_v1";
              if (handlerKind == "abort")
                realHandlerName += "_abort";

              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy, intptrTy}, false);
              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              if (handlerKind == "abort") {
                handlerFn->addFnAttr(llvm::Attribute::NoReturn);
                handlerFn->addFnAttr(llvm::Attribute::NoUnwind);
              }

              auto *locAsInt = llvm::ConstantExpr::getPtrToInt(infoGV, intptrTy);
              auto *handlerCall =
                  irb.CreateCall(handlerFnTy, handlerFn, {infoGV, locAsInt});
              handlerCall->setMetadata(llvm::LLVMContext::MD_nosanitize,
                                       llvm::MDNode::get(ctx, {}));
              handlerCall->addFnAttr(llvm::Attribute::NoMerge);
              if (handlerKind == "abort")
                handlerCall->addFnAttr(llvm::Attribute::NoReturn);
              else
                handlerCall->addFnAttr(llvm::Attribute::NoUnwind);

              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "shift_out_of_bounds") {
              auto lhsTypeNameAttr =
                  op->getAttrOfType<mlir::StringAttr>("cir.ubsan.lhs_type_name");
              auto lhsTypeKindAttr =
                  op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.lhs_type_kind");
              auto lhsTypeInfoAttr =
                  op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.lhs_type_info");
              auto rhsTypeNameAttr =
                  op->getAttrOfType<mlir::StringAttr>("cir.ubsan.rhs_type_name");
              auto rhsTypeKindAttr =
                  op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.rhs_type_kind");
              auto rhsTypeInfoAttr =
                  op->getAttrOfType<mlir::IntegerAttr>("cir.ubsan.rhs_type_info");

              auto *lhsTypeDescGV = getOrCreateTypeDescGVFromAttrs(
                  lhsTypeNameAttr, lhsTypeKindAttr, lhsTypeInfoAttr);
              auto *rhsTypeDescGV = getOrCreateTypeDescGVFromAttrs(
                  rhsTypeNameAttr, rhsTypeKindAttr, rhsTypeInfoAttr);

              llvm::Constant *staticData[] = {srcLocStruct, lhsTypeDescGV,
                                              rhsTypeDescGV};
              auto *infoStruct = llvm::ConstantStruct::getAnon(staticData);
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              bool isAbortHandler =
                  calleeName == "__cir_ubsan_shift_out_of_bounds_abort";
              std::string realHandlerName =
                  "__ubsan_handle_shift_out_of_bounds";
              if (isAbortHandler)
                realHandlerName += "_abort";

              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy, intptrTy, intptrTy},
                  false);
              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              llvm::StringRef kind = isAbortHandler ? "abort" : "recover";
              setUnaryUBSanHandlerAttrs(handlerFn, kind);

              auto *lhsValue = callBase->getArgOperand(0);
              auto *rhsValue = callBase->getArgOperand(1);
              auto *handlerCall = irb.CreateCall(
                  handlerFnTy, handlerFn, {infoGV, lhsValue, rhsValue});
              setUnaryUBSanCallAttrs(handlerCall, kind);
              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "implicit_conversion") {
              auto *srcTypeDescGV = getOrCreateTypeDescGVFromAttrs(
                  srcTypeNameAttr, srcTypeKindAttr, srcTypeInfoAttr);
              auto *dstTypeDescGV = getOrCreateTypeDescGVFromAttrs(
                  dstTypeNameAttr, dstTypeKindAttr, dstTypeInfoAttr);
              uint8_t checkKind =
                  implicitConversionCheckKindAttr
                      ? static_cast<uint8_t>(
                            implicitConversionCheckKindAttr.getInt())
                      : 0;

              llvm::Constant *staticData[] = {
                  srcLocStruct, srcTypeDescGV, dstTypeDescGV,
                  llvm::ConstantInt::get(int8Ty, checkKind),
                  llvm::ConstantInt::get(int32Ty, 0)};
              auto *infoStruct = llvm::ConstantStruct::getAnon(staticData);
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              bool isAbortHandler =
                  calleeName == "__cir_ubsan_implicit_conversion_abort";
              std::string realHandlerName =
                  "__ubsan_handle_implicit_conversion";
              if (isAbortHandler)
                realHandlerName += "_abort";

              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy, intptrTy, intptrTy},
                  false);
              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              setUnaryUBSanHandlerAttrs(handlerFn,
                                        isAbortHandler ? "abort" : "recover");

              auto *srcValue = callBase->getArgOperand(0);
              auto *dstValue = callBase->getArgOperand(1);
              auto *handlerCall = irb.CreateCall(
                  handlerFnTy, handlerFn, {infoGV, srcValue, dstValue});
              setUnaryUBSanCallAttrs(handlerCall,
                                     isAbortHandler ? "abort" : "recover");

              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "out_of_bounds") {
              auto indexTypeNameAttr = op->getAttrOfType<mlir::StringAttr>(
                  "cir.ubsan.index_type_name");
              auto indexTypeKindAttr = op->getAttrOfType<mlir::IntegerAttr>(
                  "cir.ubsan.index_type_kind");
              auto indexTypeInfoAttr = op->getAttrOfType<mlir::IntegerAttr>(
                  "cir.ubsan.index_type_info");

              llvm::StringRef indexTypeName = indexTypeNameAttr
                                                  ? indexTypeNameAttr.getValue()
                                                  : "'unknown'";
              uint16_t indexTypeKind =
                  indexTypeKindAttr
                      ? static_cast<uint16_t>(indexTypeKindAttr.getValue()
                                                  .zextOrTrunc(16)
                                                  .getZExtValue())
                      : static_cast<uint16_t>(0xFFFF);
              uint16_t indexTypeInfo =
                  indexTypeInfoAttr
                      ? static_cast<uint16_t>(indexTypeInfoAttr.getValue()
                                                  .zextOrTrunc(16)
                                                  .getZExtValue())
                      : static_cast<uint16_t>(0);
              // Reuse the cached type-descriptor global so an index type
              // shared with other UBSan checks (e.g. a signed-overflow check
              // on the same int type) is emitted once, matching classic clang.
              auto *indexTypeDescGV = getOrCreateUBSanTypeDescriptorGlobal(
                  *mod, indexTypeName, indexTypeKind, indexTypeInfo);

              llvm::Constant *staticData[] = {srcLocStruct, getTypeDescGV(),
                                              indexTypeDescGV};
              auto *infoStruct = llvm::ConstantStruct::getAnon(staticData);
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              std::string realHandlerName = "__ubsan_handle_out_of_bounds";
              if (handlerKind == "abort")
                realHandlerName += "_abort";

              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy, intptrTy}, false);
              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              setUnaryUBSanHandlerAttrs(handlerFn, handlerKind);

              auto *dynamicIndex = callBase->getArgOperand(0);
              auto *handlerCall = irb.CreateCall(
                  handlerFnTy, handlerFn, {infoGV, dynamicIndex});
              setUnaryUBSanCallAttrs(handlerCall, handlerKind);

              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "nonnull_arg" ||
                       handlerName == "nullability_arg") {
              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy}, false);
              std::string realHandlerName = "__ubsan_handle_";
              realHandlerName += handlerName.str();
              if (handlerKind == "abort")
                realHandlerName += "_abort";

              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              setUnaryUBSanHandlerAttrs(handlerFn, handlerKind);

              auto *dataPtr = callBase->getArgOperand(0);
              auto *handlerCall =
                  irb.CreateCall(handlerFnTy, handlerFn, {dataPtr});
              setUnaryUBSanCallAttrs(handlerCall, handlerKind);

              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "nonnull_return" ||
                       handlerName == "nullability_return") {
              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy, intptrTy}, false);
              std::string realHandlerName = "__ubsan_handle_";
              realHandlerName += handlerName.str();
              realHandlerName += "_v1";
              if (handlerKind == "abort")
                realHandlerName += "_abort";

              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              setUnaryUBSanHandlerAttrs(handlerFn, handlerKind);

              auto *dataPtr = callBase->getArgOperand(0);
              auto *slocPtr = callBase->getArgOperand(1);
              auto *handlerCall =
                  irb.CreateCall(handlerFnTy, handlerFn, {dataPtr, slocPtr});
              setUnaryUBSanCallAttrs(handlerCall, handlerKind);

              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "pointer_overflow") {
              auto *infoStruct = llvm::ConstantStruct::getAnon({srcLocStruct});
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              std::string realHandlerName =
                  "__ubsan_handle_pointer_overflow";
              if (handlerKind == "abort")
                realHandlerName += "_abort";

              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy, intptrTy, intptrTy},
                  false);
              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              setUnaryUBSanHandlerAttrs(handlerFn, handlerKind);

              auto *base = callBase->getArgOperand(0);
              auto *result = callBase->getArgOperand(1);
              auto *handlerCall = irb.CreateCall(
                  handlerFnTy, handlerFn, {infoGV, base, result});
              setUnaryUBSanCallAttrs(handlerCall, handlerKind);

              callBase->eraseFromParent();
              continue;
            } else if (handlerName == "alignment_assumption") {
              // Build secondary source location struct for the assumption
              // site (e.g., __builtin_assume_aligned call location).
              auto secFileAttr = op->getAttrOfType<mlir::StringAttr>(
                  "cir.ubsan.secondary_source_file");
              auto secLineAttr = op->getAttrOfType<mlir::IntegerAttr>(
                  "cir.ubsan.secondary_source_line");
              auto secColAttr = op->getAttrOfType<mlir::IntegerAttr>(
                  "cir.ubsan.secondary_source_column");
              // Match OGCG: absent secondary source information lowers as an
              // invalid SourceLocation payload, not as a duplicate of the
              // primary site.
              llvm::Constant *secSrcLocStruct =
                  llvm::Constant::getNullValue(srcLocStruct->getType());
              if (secFileAttr && secLineAttr && secColAttr) {
                llvm::Constant *secFileGV = irb.CreateGlobalStringPtr(
                    secFileAttr.getValue(), "", 0, mod);
                llvm::Constant *secLocData[] = {
                    secFileGV,
                    llvm::ConstantInt::get(int32Ty,
                                           secLineAttr.getInt()),
                    llvm::ConstantInt::get(int32Ty,
                                           secColAttr.getInt())};
                secSrcLocStruct =
                    llvm::ConstantStruct::getAnon(secLocData);
              }

              llvm::Constant *staticData[] = {srcLocStruct, secSrcLocStruct,
                                              getTypeDescGV()};
              auto *infoStruct = llvm::ConstantStruct::getAnon(staticData);
              auto *infoGV = new llvm::GlobalVariable(
                  *mod, infoStruct->getType(), false,
                  llvm::GlobalVariable::PrivateLinkage, infoStruct);
              infoGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

              std::string realHandlerName =
                  "__ubsan_handle_alignment_assumption";
              if (handlerKind == "abort")
                realHandlerName += "_abort";

              // Handler signature: (ptr info, intptr_t ptr, intptr_t align,
              //                     intptr_t offset)
              auto *handlerFnTy = llvm::FunctionType::get(
                  llvm::Type::getVoidTy(ctx),
                  {globalsPtrTy, intptrTy, intptrTy,
                   intptrTy},
                  false);
              auto handlerCallee =
                  mod->getOrInsertFunction(realHandlerName, handlerFnTy);
              auto *handlerFn =
                  llvm::cast<llvm::Function>(handlerCallee.getCallee());
              setUnaryUBSanHandlerAttrs(handlerFn, handlerKind);

              auto *ptrValue = callBase->getArgOperand(0);
              auto *alignValue = callBase->getArgOperand(1);
              auto *offsetValue = callBase->getArgOperand(2);
              auto *handlerCall = irb.CreateCall(
                  handlerFnTy, handlerFn,
                  {infoGV, ptrValue, alignValue, offsetValue});
              setUnaryUBSanCallAttrs(handlerCall, handlerKind);
              callBase->eraseFromParent();
              continue;
            }
          }
          // Clean up related UBSan attributes.
          op->removeAttr("cir.ubsan.source_file");
          op->removeAttr("cir.ubsan.source_line");
          op->removeAttr("cir.ubsan.source_column");
          op->removeAttr("cir.ubsan.attr.source_file");
          op->removeAttr("cir.ubsan.attr.source_line");
          op->removeAttr("cir.ubsan.attr.source_column");
          op->removeAttr("cir.ubsan.arg_index");
          op->removeAttr("cir.ubsan.type_name");
          op->removeAttr("cir.ubsan.type_check_kind");
          op->removeAttr("cir.ubsan.log2_alignment");
          op->removeAttr("cir.ubsan.type_hash");
          op->removeAttr("cir.ubsan.rtti_name");
          op->removeAttr("cir.ubsan.handler_kind");
          op->removeAttr("cir.ubsan.lhs_type_name");
          op->removeAttr("cir.ubsan.lhs_type_kind");
          op->removeAttr("cir.ubsan.lhs_type_info");
          op->removeAttr("cir.ubsan.rhs_type_name");
          op->removeAttr("cir.ubsan.rhs_type_kind");
          op->removeAttr("cir.ubsan.rhs_type_info");
        }
      }
      // Rewrite __cir_type_test stub calls to llvm.type.test intrinsic
      // with proper metadata operand for CFI member-function-pointer checks.
      if (attrName == "cir.type_test_id") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
          for (auto *callBase : callBases) {
            auto *calledFn = callBase->getCalledFunction();
            if (!calledFn || calledFn->getName() != "__cir_type_test")
              continue;

            llvm::Module *mod = calledFn->getParent();
            llvm::Function *typeTestFn =
                llvm::Intrinsic::getOrInsertDeclaration(
                    mod, llvm::Intrinsic::type_test);

            // Metadata: MDString for externally visible types, distinct
            // MDNode for non-externally-visible types (empty string).
            llvm::Metadata *md;
            if (strAttr.getValue().empty()) {
              md = llvm::MDNode::getDistinct(callBase->getContext(), {});
            } else {
              md = llvm::MDString::get(callBase->getContext(),
                                        strAttr.getValue());
            }
            llvm::Value *typeId =
                llvm::MetadataAsValue::get(callBase->getContext(), md);

            callBase->setCalledFunction(typeTestFn);
            // The original stub call is typed as (ptr, ptr) -> bool, so it may
            // carry call-site ABI attrs such as noundef/zeroext. Once operand
            // 1 becomes metadata, those attrs are no longer valid.
            callBase->setAttributes(llvm::AttributeList());
            // arg0 stays (the pointer to test), arg1 becomes metadata.
            callBase->setArgOperand(1, typeId);
          }
        }
      }
      // Rewrite __cir_instrprof_increment stub calls in the call-op
      // captured scope (non-OpenMP CIR codegen). The OpenMP-parallel case
      // is handled in a separate `else if` branch on omp.ParallelOp below.
      if (attrName == "cir.instrprof_func_name") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
          for (auto *callBase : callBases) {
            auto *calledFn = callBase->getCalledFunction();
            if (!calledFn)
              continue;
            const bool isStep =
                calledFn->getName() == "__cir_instrprof_increment_step";
            if (calledFn->getName() != "__cir_instrprof_increment" && !isStep)
              continue;

            llvm::Module *mod = calledFn->getParent();

            // Determine the PGO function linkage. classic CodeGenPGO creates
            // the FuncNameVar with the instrumented function's own linkage
            // (CodeGenPGO::setFuncName -> createPGOFuncNameVar). Use the
            // containing LLVM function's linkage here: it is always available
            // and always correct, whereas the cir.instrprof_func_linkage
            // attribute is stripped from the op before this handler runs (the
            // dialect attributes are visited in sorted order, so the
            // ..._linkage attribute is consumed and dropped before the
            // ..._name attribute that drives this rewrite).
            auto linkage = callBase->getFunction()->getLinkage();

            // Create or reuse the FuncNameVar global. Multiple
            // instrprof.increment calls in the same function must share ONE
            // FuncNameVar, otherwise the downstream InstrProfiling pass keys
            // its per-function counter array off distinct name globals and
            // emits duplicate @__profc_/@__profd_ arrays (one per counter),
            // splitting the function's counters and corrupting merges.
            //
            // The lookup name must match the name createPGOFuncNameVar will
            // actually assign. That routine remaps the linkage before naming
            // (InstrProf.cpp createPGOFuncNameVar): internal/external become
            // private, extern_weak/available_externally become linkonce. Only
            // isLocalLinkage() affects the name (local names sanitize illegal
            // chars such as the ':' in a file-prefixed static-function name),
            // so mirror that remap when computing the lookup name; without it
            // an internal function whose PGO name defaulted to external
            // linkage would sanitize inconsistently and never dedup.
            auto nameVarLinkage = linkage;
            if (mod->getTargetTriple().isGPU())
              nameVarLinkage = llvm::GlobalValue::ExternalLinkage;
            else if (nameVarLinkage == llvm::GlobalValue::ExternalWeakLinkage)
              nameVarLinkage = llvm::GlobalValue::LinkOnceAnyLinkage;
            else if (nameVarLinkage ==
                     llvm::GlobalValue::AvailableExternallyLinkage)
              nameVarLinkage = llvm::GlobalValue::LinkOnceODRLinkage;
            else if (nameVarLinkage == llvm::GlobalValue::InternalLinkage ||
                     nameVarLinkage == llvm::GlobalValue::ExternalLinkage)
              nameVarLinkage = llvm::GlobalValue::PrivateLinkage;

            auto varName = llvm::getPGOFuncNameVarName(strAttr.getValue(),
                                                       nameVarLinkage);
            auto *funcNameVar = mod->getNamedGlobal(varName);
            if (!funcNameVar)
              funcNameVar = llvm::createPGOFuncNameVar(*mod, linkage,
                                                       strAttr.getValue());

            // Create PGOFuncName metadata on parent function (idempotent
            // if already set).
            llvm::createPGOFuncNameMetadata(*callBase->getFunction(),
                                            strAttr.getValue());

            // Get the instrprof.increment[.step] intrinsic.
            auto *instrprofFn = llvm::Intrinsic::getOrInsertDeclaration(
                mod, isStep ? llvm::Intrinsic::instrprof_increment_step
                            : llvm::Intrinsic::instrprof_increment);

            // Normalize FuncNameVar pointer to addrspace 0.
            auto *normalizedPtr =
                llvm::ConstantExpr::getPointerBitCastOrAddrSpaceCast(
                    funcNameVar,
                    llvm::PointerType::get(callBase->getContext(), 0));

            // Replace call: target -> instrprof.increment,
            // arg0 -> funcNameVar.
            callBase->setCalledFunction(instrprofFn);
            callBase->setArgOperand(0, normalizedPtr);
          }
        }
      }
      // Drop cir.instrprof_func_linkage alongside the name attribute.
      if (attrName == "cir.instrprof_func_name")
        op->removeAttr("cir.instrprof_func_linkage");

      // Drop amended CIR attributes from the LLVM op after all per-attribute
      // amend callbacks have run for this call-like op. Some UBSan rewrites
      // need to inspect sibling attrs such as cir.ubsan.handler_kind while
      // handling cir.ubsan.handler_name; removing them eagerly here would make
      // the translation order observable and break recover-vs-abort parity.
      if (attrName.starts_with("cir.ubsan.")) {
        if (attrName == "cir.ubsan.handler_name") {
          constexpr llvm::StringLiteral UBSanAttrsToErase[] = {
              "cir.ubsan.handler_name", "cir.ubsan.source_file",
              "cir.ubsan.source_line", "cir.ubsan.source_column",
              "cir.ubsan.attr.source_file", "cir.ubsan.attr.source_line",
              "cir.ubsan.attr.source_column", "cir.ubsan.arg_index",
              "cir.ubsan.type_name",   "cir.ubsan.type_kind",
              "cir.ubsan.type_info",   "cir.ubsan.type_check_kind",
              "cir.ubsan.log2_alignment", "cir.ubsan.type_hash",
              "cir.ubsan.rtti_name",   "cir.ubsan.handler_kind",
              "cir.cfi.mfcall.minimal_runtime",
              "cir.cfi.mfcall.preserve_all_regs",
              "cir.ubsan.src_type_name", "cir.ubsan.src_type_kind",
              "cir.ubsan.src_type_info", "cir.ubsan.dst_type_name",
              "cir.ubsan.dst_type_kind", "cir.ubsan.dst_type_info",
              "cir.ubsan.implicit_conversion_check_kind",
              "cir.ubsan.lhs_type_name", "cir.ubsan.lhs_type_kind",
              "cir.ubsan.lhs_type_info", "cir.ubsan.rhs_type_name",
              "cir.ubsan.rhs_type_kind", "cir.ubsan.rhs_type_info",
              "cir.ubsan.index_type_name", "cir.ubsan.index_type_kind",
              "cir.ubsan.index_type_info"};
          for (llvm::StringLiteral name : UBSanAttrsToErase)
            op->removeAttr(name);
        }
      } else if (attrName.starts_with("cir.virtual.")) {
        static constexpr llvm::StringLiteral VirtualCallAttrsToKeep[] = {
            "cir.virtual.typeid",
            "cir.virtual.typeid_distinct",
            "cir.virtual.cfi_vcall_kind",
            "cir.virtual.cfi_minimal_runtime",
            "cir.virtual.cfi_preserve_all_regs",
            "cir.virtual.typecheckedload",
            "cir.virtual.typetest_kind",
        };
        if (!llvm::is_contained(VirtualCallAttrsToKeep, attrName))
          op->removeAttr(attribute.getName());
      } else if (attrName.starts_with("cir.cfi.icall.")) {
        static constexpr llvm::StringLiteral CFIICallAttrsToKeep[] = {
            "cir.cfi.icall.typeid",
            "cir.cfi.icall.kind",
            "cir.cfi.icall.minimal_runtime",
            "cir.cfi.icall.preserve_all_regs",
        };
        if (!llvm::is_contained(CFIICallAttrsToKeep, attrName))
          op->removeAttr(attribute.getName());
      } else if (attrName.starts_with("cir.cfi.mfcall.")) {
        static constexpr llvm::StringLiteral CFIMFCallAttrsToKeep[] = {
            "cir.cfi.mfcall.minimal_runtime",
            "cir.cfi.mfcall.preserve_all_regs",
        };
        if (!llvm::is_contained(CFIMFCallAttrsToKeep, attrName))
          op->removeAttr(attribute.getName());
      } else if (attrName.starts_with("cir.call_")) {
        // Generic handler: forward unknown cir.call_<name> attrs with string
        // values as LLVM string attributes on the call instruction (e.g.
        // modular-format). Skip attrs already handled by specific handlers.
        static constexpr llvm::StringLiteral handledCallAttrs[] = {
            "cir.call_allocsize",
            "cir.call_nomerge",
            "cir.call_nobuiltin",
            "cir.call_builtin",
            "cir.call_strictfp",
            "cir.call_memory_none",
            "cir.call_nocallback",
            "cir.call_noreturn",
            "cir.call_returns_twice",
            "cir.call_cold",
            "cir.call_hot",
            "cir.call_convergent",
            "cir.call_noduplicate",
            "cir.call_noalias_return",
            "cir.call_srcloc",
            "cir.call_optsize",
            "cir.call_minsize",
            "cir.call_no_caller_saved_registers",
            "cir.call_coro_elide_safe",
            "cir.call_sample_profile_suffix",
            "cir.call_allow_runtime_check_kind",
        };
        if (!llvm::is_contained(handledCallAttrs, attrName) &&
            !attrName.starts_with("cir.call_no-builtin") &&
            !attrName.starts_with("cir.call_aarch64_") &&
            !attrName.starts_with("cir.call_cir.")) {
          llvm::StringRef llvmAttrName =
              attrName.drop_front(llvm::StringRef("cir.call_").size());
          if (auto strVal =
                  mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
            for (auto *callBase : callBases)
              callBase->addFnAttr(llvm::Attribute::get(
                  callBase->getContext(), llvmAttrName, strVal.getValue()));
          } else if (mlir::isa<mlir::UnitAttr>(attribute.getValue())) {
            for (auto *callBase : callBases)
              callBase->addFnAttr(llvm::Attribute::get(
                  callBase->getContext(), llvmAttrName));
          }
        }
        op->removeAttr(attribute.getName());
      } else {
        op->removeAttr(attribute.getName());
      }
    } else if (attribute.getName() == "cir.call_strictfp" ||
               attribute.getName() == "cir.call_memory_none") {
      llvm::SmallVector<llvm::CallBase *, 1> callBases;
      callBases.reserve(instructions.size());
      for (auto *inst : instructions)
        if (auto *callBase = llvm::dyn_cast<llvm::CallBase>(inst))
          callBases.push_back(callBase);
      if (callBases.empty()) {
        for (mlir::Value result : op->getResults())
          if (auto *callBase = llvm::dyn_cast_or_null<llvm::CallBase>(
                  moduleTranslation.lookupValue(result)))
            callBases.push_back(callBase);
      }
      for (auto *callBase : callBases)
        if (attribute.getName() == "cir.call_strictfp")
          callBase->addFnAttr(llvm::Attribute::StrictFP);
        else
          callBase->setDoesNotAccessMemory();
      op->removeAttr(attribute.getName());
    } else if (llvm::isa<mlir::LLVM::InlineAsmOp>(op)) {
      // Forward nounwind to inline asm CallInst. OGCG unconditionally adds
      // nounwind to inline asm calls (CGStmt.cpp:2655) unless there is an
      // unwind clobber.
      if (attribute.getName() == "cir.asm_nounwind") {
        for (auto *inst : instructions)
          if (auto *callBase = llvm::dyn_cast<llvm::CallBase>(inst))
            callBase->addFnAttr(llvm::Attribute::NoUnwind);
      }
      // Forward readnone/readonly memory effects to inline asm CallInst.
      // OGCG sets these in UpdateAsmCallInst (CGStmt.cpp:2659-2665).
      if (attribute.getName() == "cir.asm_readnone") {
        for (auto *inst : instructions)
          if (auto *callBase = llvm::dyn_cast<llvm::CallBase>(inst))
            callBase->setDoesNotAccessMemory();
      }
      if (attribute.getName() == "cir.asm_readonly") {
        for (auto *inst : instructions)
          if (auto *callBase = llvm::dyn_cast<llvm::CallBase>(inst))
            callBase->setOnlyReadsMemory();
      }
      // Forward nomerge from [[clang::nomerge]] to inline asm CallInst.
      // Matches OGCG UpdateAsmCallInst (CGStmt.cpp:2657-2658).
      if (attribute.getName() == "cir.asm_nomerge") {
        for (auto *inst : instructions)
          if (auto *callBase = llvm::dyn_cast<llvm::CallBase>(inst))
            callBase->addFnAttr(llvm::Attribute::NoMerge);
      }
      // Forward convergent to inline asm CallInst (CUDA/OpenCL modes).
      // Matches OGCG UpdateAsmCallInst (CGStmt.cpp:2696-2701).
      if (attribute.getName() == "cir.asm_convergent") {
        for (auto *inst : instructions)
          if (auto *callBase = llvm::dyn_cast<llvm::CallBase>(inst))
            callBase->addFnAttr(llvm::Attribute::Convergent);
      }
      // Forward !srcloc metadata for inline asm backend diagnostics.
      if (attribute.getName() == "cir.asm_srcloc") {
        if (auto locs = mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
          if (llvm::MDNode *srcloc =
                  getAsmSrcLocMD(locs, moduleTranslation.getLLVMContext())) {
            for (auto *inst : instructions)
              if (auto *callBase = llvm::dyn_cast<llvm::CallBase>(inst))
                callBase->setMetadata("srcloc", srcloc);
          }
        }
      }
      op->removeAttr(attribute.getName());
    } else if (llvm::isa<mlir::LLVM::CondBrOp>(op) ||
               llvm::isa<mlir::LLVM::SwitchOp>(op)) {
      // Forward !unpredictable metadata to branch/switch instructions.
      if (attribute.getName() == "cir.unpredictable") {
        for (auto *inst : instructions) {
          if (auto *brInst = llvm::dyn_cast<llvm::BranchInst>(inst)) {
            brInst->setMetadata(
                llvm::LLVMContext::MD_unpredictable,
                llvm::MDNode::get(brInst->getContext(), {}));
          } else if (auto *swInst = llvm::dyn_cast<llvm::SwitchInst>(inst)) {
            swInst->setMetadata(
                llvm::LLVMContext::MD_unpredictable,
                llvm::MDNode::get(swInst->getContext(), {}));
          }
        }
        op->removeAttr(attribute.getName());
      }
      if (attribute.getName() == "cir.hlsl.controlflow.hint") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue())) {
          auto *hintConstant = llvm::ConstantInt::get(
              llvm::Type::getInt32Ty(moduleTranslation.getLLVMContext()),
              intAttr.getInt());
          for (auto *inst : instructions) {
            if (auto *branchLike = llvm::dyn_cast<llvm::Instruction>(inst)) {
              llvm::MDBuilder mdBuilder(branchLike->getContext());
              llvm::Metadata *vals[] = {
                  mdBuilder.createString("hlsl.controlflow.hint"),
                  mdBuilder.createConstant(hintConstant)};
              branchLike->setMetadata(
                  "hlsl.controlflow.hint",
                  llvm::MDNode::get(branchLike->getContext(), vals));
            }
          }
        }
        op->removeAttr(attribute.getName());
      }
      if (attribute.getName() == "cir.nosanitize") {
        for (auto *inst : instructions)
          if (auto *i = llvm::dyn_cast<llvm::Instruction>(inst))
            i->setNoSanitizeMetadata();
        op->removeAttr(attribute.getName());
      }
    } else if (llvm::isa<mlir::LLVM::UBSanTrap>(op) ||
               llvm::isa<mlir::LLVM::Trap>(op) ||
               llvm::isa<mlir::LLVM::UnreachableOp>(op) ||
               llvm::isa<mlir::LLVM::ExtractValueOp>(op) ||
               (llvm::isa<mlir::LLVM::FCmpOp>(op) &&
                !op->hasAttr("cir.fp_exception")) ||
               llvm::isa<mlir::LLVM::CallIntrinsicOp>(op) ||
               llvm::isa<mlir::LLVM::AndOp>(op) ||
               llvm::isa<mlir::LLVM::BitcastOp>(op) ||
               llvm::isa<mlir::LLVM::ICmpOp>(op) ||
               llvm::isa<mlir::LLVM::IntToPtrOp>(op) ||
               llvm::isa<mlir::LLVM::PtrToIntOp>(op) ||
               llvm::isa<mlir::LLVM::SExtOp>(op) ||
               llvm::isa<mlir::LLVM::ZExtOp>(op) ||
               llvm::isa<mlir::LLVM::TruncOp>(op) ||
               llvm::isa<mlir::LLVM::XOrOp>(op) ||
               llvm::isa<mlir::LLVM::BrOp>(op) ||
               llvm::isa<mlir::LLVM::GEPOp>(op)) {
      if (attribute.getName() == "cir.nosanitize") {
        for (auto *inst : instructions)
          if (auto *i = llvm::dyn_cast<llvm::Instruction>(inst))
            i->setNoSanitizeMetadata();
        op->removeAttr(attribute.getName());
      }
    } else if (auto globalOp = llvm::dyn_cast<mlir::LLVM::GlobalOp>(op)) {
      auto *gv = cast<llvm::GlobalVariable>(
          moduleTranslation.lookupGlobal(globalOp));
      // Keep the final LLVM IR global visibility in sync with the lowered
      // LLVM dialect global before applying CIR-specific fixups.
      switch (globalOp.getVisibility_()) {
      case mlir::LLVM::Visibility::Default:
        gv->setVisibility(llvm::GlobalValue::DefaultVisibility);
        break;
      case mlir::LLVM::Visibility::Hidden:
        gv->setVisibility(llvm::GlobalValue::HiddenVisibility);
        break;
      case mlir::LLVM::Visibility::Protected:
        gv->setVisibility(llvm::GlobalValue::ProtectedVisibility);
        break;
      }

      // Set the TLS model on the LLVM IR GlobalVariable. The MLIR LLVM dialect
      // only supports a boolean thread_local flag (defaulting to
      // GeneralDynamic), so non-default TLS models are carried as a
      // discardable "cir.tls_model" attribute.
      if (attribute.getName() == "cir.tls_model") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue())) {
          auto model = static_cast<cir::TLS_Model>(intAttr.getInt());
          switch (model) {
          case cir::TLS_Model::LocalDynamic:
            gv->setThreadLocalMode(
                llvm::GlobalValue::LocalDynamicTLSModel);
            break;
          case cir::TLS_Model::InitialExec:
            gv->setThreadLocalMode(
                llvm::GlobalValue::InitialExecTLSModel);
            break;
          case cir::TLS_Model::LocalExec:
            gv->setThreadLocalMode(
                llvm::GlobalValue::LocalExecTLSModel);
            break;
          default:
            break;
          }
        }
        op->removeAttr(attribute.getName());
      }
      // Set DLL storage class on GlobalVariable.
      if (attribute.getName() == "cir.dll_storage_class") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
          if (strAttr.getValue() == "dllexport")
            gv->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
          else if (strAttr.getValue() == "dllimport")
            gv->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
        }
        op->removeAttr(attribute.getName());
      }
      if (attribute.getName() == "cir.no_cfi") {
        if (auto payload =
                mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
          llvm::Constant *init = gv->getInitializer();
          if (init) {
            llvm::Constant *rewrittenInit = init;
            for (mlir::Attribute entryAttr : payload) {
              auto entry = mlir::dyn_cast<mlir::ArrayAttr>(entryAttr);
              if (!entry)
                continue;
              llvm::SmallVector<unsigned, 8> path;
              bool validPath = true;
              for (mlir::Attribute idxAttr : entry) {
                auto idx = mlir::dyn_cast<mlir::IntegerAttr>(idxAttr);
                if (!idx) {
                  validPath = false;
                  break;
                }
                path.push_back(static_cast<unsigned>(idx.getInt()));
              }
              if (!validPath)
                continue;
              if (llvm::Constant *updated =
                      applyNoCFIAtPath(rewrittenInit, path))
                rewrittenInit = updated;
            }
            if (rewrittenInit != init)
              gv->setInitializer(rewrittenInit);
          }
        }
        op->removeAttr(attribute.getName());
      }
      // Match OGCG CodeGenVTables::RemoveHwasanMetadata for relative vtables
      // and RTTI proxy globals that must remain unsanitized under HWASan.
      if (attribute.getName() == "cir.no_sanitize_hwaddress") {
        llvm::GlobalValue::SanitizerMetadata meta;
        if (gv->hasSanitizerMetadata())
          meta = gv->getSanitizerMetadata();
        meta.NoHWAddress = true;
        gv->setSanitizerMetadata(meta);
        op->removeAttr(attribute.getName());
      }
      // Match OGCG SanitizerMetadata::reportGlobal() for ASan/KASan globals
      // that should bypass global instrumentation.
      if (attribute.getName() == "cir.no_sanitize_address") {
        llvm::GlobalValue::SanitizerMetadata meta;
        if (gv->hasSanitizerMetadata())
          meta = gv->getSanitizerMetadata();
        meta.NoAddress = true;
        gv->setSanitizerMetadata(meta);
        op->removeAttr(attribute.getName());
      }
      // Match OGCG SanitizerMetadata::reportGlobal() for init-order metadata.
      if (attribute.getName() == "cir.sanitize_address_dyninit") {
        llvm::GlobalValue::SanitizerMetadata meta;
        if (gv->hasSanitizerMetadata())
          meta = gv->getSanitizerMetadata();
        meta.IsDynInit = true;
        gv->setSanitizerMetadata(meta);
        op->removeAttr(attribute.getName());
      }
      // Match OGCG SanitizerMetadata::reportGlobal() for MemtagGlobals.
      if (attribute.getName() == "cir.sanitize_memtag") {
        llvm::GlobalValue::SanitizerMetadata meta;
        if (gv->hasSanitizerMetadata())
          meta = gv->getSanitizerMetadata();
        meta.Memtag = true;
        gv->setSanitizerMetadata(meta);
        op->removeAttr(attribute.getName());
      }
      // Attach !type metadata entries to vtable globals.
      if (attribute.getName() == "cir.vtable.type_metadata") {
        if (auto arrAttr =
                mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
          for (mlir::Attribute item : arrAttr) {
            auto pair = mlir::dyn_cast<mlir::ArrayAttr>(item);
            if (!pair || pair.size() < 2)
              continue;
            auto offAttr = mlir::dyn_cast<mlir::IntegerAttr>(pair[0]);
            auto typeIdAttr = mlir::dyn_cast<mlir::StringAttr>(pair[1]);
            if (!offAttr || !typeIdAttr)
              continue;
            llvm::Metadata *md = nullptr;
            if (pair.size() >= 3) {
              auto kindAttr = mlir::dyn_cast<mlir::StringAttr>(pair[2]);
              if (kindAttr && kindAttr.getValue() == "hidden") {
                auto &cached =
                    translationCaches().hiddenTypeIdCache[typeIdAttr.getValue().str()];
                if (!cached)
                  cached = llvm::MDNode::getDistinct(gv->getContext(), {});
                md = cached;
              } else if (kindAttr && kindAttr.getValue() == "crossdso") {
                // Match OGCG CodeGenModule::CreateCrossDsoCfiTypeId: the numeric
                // cross-DSO CFI id is the MD5 hash of the mangled type name.
                // Vtable base-type ids are "_ZTS..." names, so the coarse-
                // grained "_ZTSF..." collapse never applies here.
                uint64_t id = llvm::MD5Hash(typeIdAttr.getValue());
                md = llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                    llvm::Type::getInt64Ty(gv->getContext()), id));
              }
            }
            if (!md)
              md = llvm::MDString::get(gv->getContext(), typeIdAttr.getValue());
            gv->addTypeMetadata(offAttr.getInt(), md);
          }
        }
        op->removeAttr(attribute.getName());
      }
      if (attribute.getName() == "cir.vtable.vcall_visibility") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue()))
          cast<llvm::GlobalObject>(gv)->setVCallVisibilityMetadata(
              static_cast<llvm::GlobalObject::VCallVisibility>(
                  intAttr.getInt()));
        op->removeAttr(attribute.getName());
      }
      // Forward objc_arc_inert attribute to LLVM global (e.g. CFString constants).
      if (attribute.getName() == "cir.objc_arc_inert") {
        gv->addAttribute("objc_arc_inert");
        op->removeAttr(attribute.getName());
      }
      // Match OGCG CodeGenModule::setNonAliasAttributes / CGDecl.cpp handling
      // of #pragma clang section bss/data/rodata/relro on global variables.
      if (attribute.getName() == "cir.pragma_clang_bss_section") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
          gv->addAttribute("bss-section", strAttr.getValue());
        }
        op->removeAttr(attribute.getName());
      }
      if (attribute.getName() == "cir.pragma_clang_data_section") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
          gv->addAttribute("data-section", strAttr.getValue());
        }
        op->removeAttr(attribute.getName());
      }
      if (attribute.getName() == "cir.pragma_clang_rodata_section") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
          auto *gv = cast<llvm::GlobalVariable>(
              moduleTranslation.lookupGlobal(globalOp));
          gv->addAttribute("rodata-section", strAttr.getValue());
        }
        op->removeAttr(attribute.getName());
      }
      if (attribute.getName() == "cir.pragma_clang_relro_section") {
        if (auto strAttr =
                mlir::dyn_cast<mlir::StringAttr>(attribute.getValue())) {
          auto *gv = cast<llvm::GlobalVariable>(
              moduleTranslation.lookupGlobal(globalOp));
          gv->addAttribute("relro-section", strAttr.getValue());
        }
        op->removeAttr(attribute.getName());
      }
      // Per-variable code_model attribute (OGCG CodeGenModule.cpp:5594-5596).
      if (attribute.getName() == "cir.code_model") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue())) {
          auto *gv = cast<llvm::GlobalVariable>(
              moduleTranslation.lookupGlobal(globalOp));
          gv->setCodeModel(
              static_cast<llvm::CodeModel::Model>(intAttr.getInt()));
        }
        op->removeAttr(attribute.getName());
      }
      if (attribute.getName() == "cir.rtti_vtable_ptrauth") {
        auto *gv = cast<llvm::GlobalVariable>(
            moduleTranslation.lookupGlobal(globalOp));
        auto payload = mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue());
        if (payload && payload.size() == 3) {
          auto keyAttr = mlir::dyn_cast<mlir::IntegerAttr>(payload[0]);
          auto discAttr = mlir::dyn_cast<mlir::IntegerAttr>(payload[1]);
          auto addrDiscAttr = mlir::dyn_cast<mlir::BoolAttr>(payload[2]);
          llvm::Constant *init = gv->getInitializer();
          auto *structTy =
              init ? llvm::dyn_cast<llvm::StructType>(init->getType()) : nullptr;
          if (keyAttr && discAttr && addrDiscAttr && init && structTy &&
              init->getNumOperands() != 0) {
            SmallVector<llvm::Constant *, 8> fields;
            fields.reserve(init->getNumOperands());
            for (llvm::Value *operand : init->operands())
              fields.push_back(cast<llvm::Constant>(operand));

            llvm::LLVMContext &ctx = gv->getContext();
            llvm::Constant *storageAddress =
                addrDiscAttr.getValue()
                    ? cast<llvm::Constant>(gv)
                    : llvm::Constant::getNullValue(gv->getType());
            fields[0] = llvm::ConstantPtrAuth::get(
                fields[0],
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx),
                                       keyAttr.getInt()),
                llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx),
                                       discAttr.getValue().getZExtValue()),
                storageAddress,
                llvm::Constant::getNullValue(gv->getType()));
            gv->setInitializer(llvm::ConstantStruct::get(structTy, fields));
          }
        }
        op->removeAttr(attribute.getName());
      }
      if (attribute.getName() == "cir.vtable_vfunc_ptrauth") {
        auto *gv = cast<llvm::GlobalVariable>(
            moduleTranslation.lookupGlobal(globalOp));
        auto payload = mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue());
        llvm::Constant *init = gv->getInitializer();
        auto *structTy =
            init ? llvm::dyn_cast<llvm::StructType>(init->getType()) : nullptr;
        if (payload && init && structTy) {
          SmallVector<llvm::Constant *, 8> vtables;
          vtables.reserve(init->getNumOperands());
          for (llvm::Value *operand : init->operands())
            vtables.push_back(cast<llvm::Constant>(operand));

          llvm::LLVMContext &ctx = gv->getContext();
          auto *i32Ty = llvm::Type::getInt32Ty(ctx);
          auto *i64Ty = llvm::Type::getInt64Ty(ctx);
          auto *nullStorage = llvm::Constant::getNullValue(gv->getType());

          for (mlir::Attribute entryAttr : payload) {
            auto entry = mlir::dyn_cast<mlir::ArrayAttr>(entryAttr);
            if (!entry || entry.size() != 5)
              continue;

            auto vtableIndexAttr = mlir::dyn_cast<mlir::IntegerAttr>(entry[0]);
            auto componentIndexAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(entry[1]);
            auto keyAttr = mlir::dyn_cast<mlir::IntegerAttr>(entry[2]);
            auto discAttr = mlir::dyn_cast<mlir::IntegerAttr>(entry[3]);
            auto addrDiscAttr = mlir::dyn_cast<mlir::BoolAttr>(entry[4]);
            if (!vtableIndexAttr || !componentIndexAttr || !keyAttr ||
                !discAttr || !addrDiscAttr)
              continue;

            uint64_t vtableIndex = vtableIndexAttr.getInt();
            uint64_t componentIndex = componentIndexAttr.getInt();
            if (vtableIndex >= vtables.size())
              continue;

            auto *subArray =
                llvm::dyn_cast<llvm::ConstantArray>(vtables[vtableIndex]);
            if (!subArray || componentIndex >= subArray->getNumOperands())
              continue;

            SmallVector<llvm::Constant *, 8> components;
            components.reserve(subArray->getNumOperands());
            for (llvm::Value *operand : subArray->operands())
              components.push_back(cast<llvm::Constant>(operand));

            llvm::Constant *storageAddress = nullStorage;
            if (addrDiscAttr.getValue()) {
              llvm::Constant *indices[] = {
                  llvm::ConstantInt::get(i32Ty, 0),
                  llvm::ConstantInt::get(i32Ty, vtableIndex),
                  llvm::ConstantInt::get(i32Ty, componentIndex)};
              storageAddress = llvm::ConstantExpr::getInBoundsGetElementPtr(
                  structTy, cast<llvm::Constant>(gv), indices);
            }

            components[componentIndex] = llvm::ConstantPtrAuth::get(
                components[componentIndex],
                llvm::ConstantInt::get(i32Ty, keyAttr.getInt()),
                llvm::ConstantInt::get(i64Ty,
                                       discAttr.getValue().getZExtValue()),
                storageAddress, nullStorage);
            vtables[vtableIndex] =
                llvm::ConstantArray::get(subArray->getType(), components);
          }

          gv->setInitializer(llvm::ConstantStruct::get(structTy, vtables));
        }
        op->removeAttr(attribute.getName());
      }
      if (attribute.getName() == "cir.vtt_ptrauth") {
        auto *gv = cast<llvm::GlobalVariable>(
            moduleTranslation.lookupGlobal(globalOp));
        auto payload = mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue());
        llvm::Constant *init = gv->getInitializer();
        auto *arrayTy =
            init ? llvm::dyn_cast<llvm::ArrayType>(init->getType()) : nullptr;
        if (payload && payload.size() == 3 && init && arrayTy) {
          auto keyAttr = mlir::dyn_cast<mlir::IntegerAttr>(payload[0]);
          auto discAttr = mlir::dyn_cast<mlir::IntegerAttr>(payload[1]);
          auto addrDiscAttr = mlir::dyn_cast<mlir::BoolAttr>(payload[2]);
          if (keyAttr && discAttr && addrDiscAttr) {
            SmallVector<llvm::Constant *, 8> elements;
            elements.reserve(init->getNumOperands());
            for (llvm::Value *operand : init->operands())
              elements.push_back(cast<llvm::Constant>(operand));

            llvm::LLVMContext &ctx = gv->getContext();
            auto *i32Ty = llvm::Type::getInt32Ty(ctx);
            auto *i64Ty = llvm::Type::getInt64Ty(ctx);
            auto *nullStorage = llvm::Constant::getNullValue(gv->getType());
            for (unsigned i = 0; i < elements.size(); ++i) {
              llvm::Constant *storageAddress = nullStorage;
              if (addrDiscAttr.getValue()) {
                llvm::Constant *indices[] = {
                    llvm::ConstantInt::get(i32Ty, 0),
                    llvm::ConstantInt::get(i32Ty, i)};
                storageAddress = llvm::ConstantExpr::getInBoundsGetElementPtr(
                    arrayTy, cast<llvm::Constant>(gv), indices);
              }

              elements[i] = llvm::ConstantPtrAuth::get(
                  elements[i], llvm::ConstantInt::get(i32Ty, keyAttr.getInt()),
                  llvm::ConstantInt::get(
                      i64Ty, discAttr.getValue().getZExtValue()),
                  storageAddress, nullStorage);
            }
            gv->setInitializer(llvm::ConstantArray::get(arrayTy, elements));
          }
        }
        op->removeAttr(attribute.getName());
      }
      if (attribute.getName() == "cir.member_func_ptr_ptrauth") {
        auto *gv = cast<llvm::GlobalVariable>(
            moduleTranslation.lookupGlobal(globalOp));
        if (auto payload =
                mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
          llvm::Constant *init = gv->getInitializer();
          if (init) {
            llvm::Constant *rewrittenInit = init;
            for (mlir::Attribute entryAttr : payload) {
              auto entry = mlir::dyn_cast<mlir::ArrayAttr>(entryAttr);
              if (!entry || entry.empty())
                continue;
              auto discAttr = mlir::dyn_cast<mlir::IntegerAttr>(
                  entry[entry.size() - 1]);
              if (!discAttr)
                continue;
              llvm::SmallVector<unsigned, 8> path;
              bool validPath = true;
              for (unsigned i = 0, e = entry.size() - 1; i != e; ++i) {
                mlir::Attribute idxAttr = entry[i];
                auto idx = mlir::dyn_cast<mlir::IntegerAttr>(idxAttr);
                if (!idx) {
                  validPath = false;
                  break;
                }
                path.push_back(static_cast<unsigned>(idx.getInt()));
              }
              if (!validPath)
                continue;
              if (llvm::Constant *updated = applyMemberFunctionPtrPtrauthAtPath(
                      rewrittenInit, path,
                      discAttr.getValue().getZExtValue()))
                rewrittenInit = updated;
            }
            if (rewrittenInit != init)
              gv->setInitializer(rewrittenInit);
          }
        }
        op->removeAttr(attribute.getName());
      }
      if (attribute.getName() == "cir.global_const_ptrauth") {
        auto *gv = cast<llvm::GlobalVariable>(
            moduleTranslation.lookupGlobal(globalOp));
        if (auto payload =
                mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
          llvm::Constant *init = gv->getInitializer();
          if (init) {
            llvm::SmallVector<GlobalConstantPtrAuthFixup, 8> fixups;
            for (mlir::Attribute item : payload) {
              auto arr = mlir::dyn_cast<mlir::ArrayAttr>(item);
              if (!arr || arr.size() != 4)
                continue;
              auto offsetAttr = mlir::dyn_cast<mlir::IntegerAttr>(arr[0]);
              auto keyAttr = mlir::dyn_cast<mlir::IntegerAttr>(arr[1]);
              auto discAttr = mlir::dyn_cast<mlir::IntegerAttr>(arr[2]);
              auto addrDiscAttr = mlir::dyn_cast<mlir::BoolAttr>(arr[3]);
              if (!offsetAttr || !keyAttr || !discAttr || !addrDiscAttr)
                continue;
              fixups.push_back({offsetAttr.getValue().getZExtValue(),
                                static_cast<uint32_t>(keyAttr.getInt()),
                                discAttr.getValue().getZExtValue(),
                                addrDiscAttr.getValue()});
            }
            for (const GlobalConstantPtrAuthFixup &fixup : fixups)
              init = applyGlobalConstantPtrAuthFixup(
                  init, gv, gv->getParent()->getDataLayout(), fixup);
            gv->setInitializer(init);
          }
        }
        op->removeAttr(attribute.getName());
      }
    } else if (llvm::isa<mlir::LLVM::ThreadlocalAddressOp>(op)) {
      // Forward alignment to the llvm.threadlocal.address intrinsic call.
      // OGCG's IRBuilderBase::CreateThreadLocalAddress reads alignment from
      // the GlobalVariable and adds it as both a param attr and return attr.
      if (attribute.getName() == "cir.tls_align") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue())) {
          unsigned alignment = intAttr.getInt();
          for (auto *inst : instructions) {
            if (auto *callBase = llvm::dyn_cast<llvm::CallBase>(inst)) {
              auto alignAttr = llvm::Attribute::getWithAlignment(
                  callBase->getContext(), llvm::Align(alignment));
              callBase->addParamAttr(0, alignAttr);
              callBase->addRetAttr(alignAttr);
            }
          }
        }
        op->removeAttr(attribute.getName());
      }
    } else if (llvm::isa<mlir::LLVM::StoreOp>(op)) {
      if (attribute.getName() == "cir.auto_init") {
        for (auto *inst : instructions) {
          if (auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(inst)) {
            llvm::Metadata *ops[] = {
                llvm::MDString::get(storeInst->getContext(), "auto-init")};
            storeInst->setMetadata(llvm::LLVMContext::MD_annotation,
                                   llvm::MDNode::get(storeInst->getContext(),
                                                     ops));
          }
        }
        op->removeAttr(attribute.getName());
      } else if (attribute.getName() == "cir.nosanitize") {
        for (auto *inst : instructions) {
          if (auto *storeInst = llvm::dyn_cast<llvm::StoreInst>(inst))
            storeInst->setNoSanitizeMetadata();
        }
        op->removeAttr(attribute.getName());
      }
    } else if (auto loadOp = llvm::dyn_cast<mlir::LLVM::LoadOp>(op)) {
      // Attach load metadata to LLVM loads.
      // These discardable attributes were set during CIR codegen in
      // emitLoadOfReference and forwarded through LowerToLLVM.
      //
      // Collect candidate llvm.load instructions: prefer the captured-scope
      // instructions, but fall back to looking up the result value (this is
      // needed when the LLVM::LoadOp is processed inside an OpenMP-target
      // outliner that doesn't surface its instructions to the capture
      // scope).
      llvm::SmallVector<llvm::LoadInst *, 1> loadInsts;
      loadInsts.reserve(instructions.size());
      for (llvm::Instruction *inst : instructions)
        if (auto *loadInst = llvm::dyn_cast<llvm::LoadInst>(inst))
          loadInsts.push_back(loadInst);
      if (loadInsts.empty()) {
        if (auto *loadInst = llvm::dyn_cast_or_null<llvm::LoadInst>(
                moduleTranslation.lookupValue(loadOp.getRes())))
          loadInsts.push_back(loadInst);
      }

      if (attribute.getName() == "cir.nonnull") {
        for (llvm::LoadInst *loadInst : loadInsts)
          loadInst->setMetadata(
              llvm::LLVMContext::MD_nonnull,
              llvm::MDNode::get(loadInst->getContext(), {}));
        op->removeAttr(attribute.getName());
      } else if (attribute.getName() == "cir.ref_align") {
        if (auto intAttr =
                mlir::dyn_cast<mlir::IntegerAttr>(attribute.getValue())) {
          for (llvm::LoadInst *loadInst : loadInsts) {
            auto &ctx = loadInst->getContext();
            llvm::MDBuilder mdb(ctx);
            loadInst->setMetadata(
                llvm::LLVMContext::MD_align,
                llvm::MDNode::get(
                    ctx, mdb.createConstant(llvm::ConstantInt::get(
                             llvm::IntegerType::get(ctx, 64),
                             intAttr.getInt()))));
          }
          op->removeAttr(attribute.getName());
        }
      } else if (attribute.getName() == "cir.range") {
        if (auto rangeAttr =
                mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
          if (rangeAttr.size() == 2) {
            auto minAttr = mlir::dyn_cast<mlir::IntegerAttr>(rangeAttr[0]);
            auto endAttr = mlir::dyn_cast<mlir::IntegerAttr>(rangeAttr[1]);
            if (minAttr && endAttr) {
              for (llvm::LoadInst *loadInst : loadInsts) {
                llvm::MDBuilder mdb(loadInst->getContext());
                loadInst->setMetadata(llvm::LLVMContext::MD_range,
                                      mdb.createRange(minAttr.getValue(),
                                                      endAttr.getValue()));
              }
            }
          }
          op->removeAttr(attribute.getName());
        }
      } else if (attribute.getName() == "cir.noundef_load") {
        for (llvm::LoadInst *loadInst : loadInsts)
          loadInst->setMetadata(llvm::LLVMContext::MD_noundef,
                                llvm::MDNode::get(loadInst->getContext(), {}));
        op->removeAttr(attribute.getName());
      } else if (attribute.getName() == "cir.nosanitize") {
        for (llvm::LoadInst *loadInst : loadInsts)
          loadInst->setNoSanitizeMetadata();
        op->removeAttr(attribute.getName());
      }
    } else if (llvm::isa<mlir::LLVM::AddressOfOp, mlir::LLVM::PtrToIntOp>(op)) {
      if (attribute.getName() == "cir.no_cfi") {
        if (auto payload =
                mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
          auto rewriteNoCFI = [&](mlir::Value valueResult) {
            llvm::Value *mappedValue = moduleTranslation.lookupValue(valueResult);
            llvm::Constant *init = llvm::dyn_cast<llvm::Constant>(mappedValue);
            if (!init)
              return;

            llvm::Constant *rewrittenInit = init;
            for (mlir::Attribute entryAttr : payload) {
              auto entry = mlir::dyn_cast<mlir::ArrayAttr>(entryAttr);
              if (!entry)
                continue;
              llvm::SmallVector<unsigned, 8> path;
              bool validPath = true;
              for (mlir::Attribute idxAttr : entry) {
                auto idx = mlir::dyn_cast<mlir::IntegerAttr>(idxAttr);
                if (!idx) {
                  validPath = false;
                  break;
                }
                path.push_back(static_cast<unsigned>(idx.getInt()));
              }
              if (!validPath)
                continue;
              if (llvm::Constant *updated =
                      applyNoCFIAtPath(rewrittenInit, path))
                rewrittenInit = updated;
            }
            if (rewrittenInit != init)
              moduleTranslation.remapValue(valueResult, rewrittenInit);
          };

          if (auto addrOfOp = llvm::dyn_cast<mlir::LLVM::AddressOfOp>(op))
            rewriteNoCFI(addrOfOp.getResult());
          else if (auto ptrToIntOp = llvm::dyn_cast<mlir::LLVM::PtrToIntOp>(op))
            rewriteNoCFI(ptrToIntOp.getRes());
        }
        op->removeAttr(attribute.getName());
      }
    } else if (llvm::isa<mlir::LLVM::ConstantOp, mlir::LLVM::InsertValueOp>(
                   op)) {
      if (attribute.getName() == "cir.member_func_ptr_ptrauth") {
        auto rewriteMemberFunctionPtrConstant =
            [&](mlir::Value valueResult) {
              if (auto payload =
                      mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
                llvm::Value *mappedValue =
                    moduleTranslation.lookupValue(valueResult);
                llvm::Constant *init = llvm::dyn_cast<llvm::Constant>(mappedValue);
                if (!init)
                  return;

                llvm::Constant *rewrittenInit = init;
                for (mlir::Attribute entryAttr : payload) {
                  auto entry = mlir::dyn_cast<mlir::ArrayAttr>(entryAttr);
                  if (!entry || entry.empty())
                    continue;
                  auto discAttr = mlir::dyn_cast<mlir::IntegerAttr>(
                      entry[entry.size() - 1]);
                  if (!discAttr)
                    continue;
                  llvm::SmallVector<unsigned, 8> path;
                  bool validPath = true;
                  for (unsigned i = 0, e = entry.size() - 1; i != e; ++i) {
                    mlir::Attribute idxAttr = entry[i];
                    auto idx = mlir::dyn_cast<mlir::IntegerAttr>(idxAttr);
                    if (!idx) {
                      validPath = false;
                      break;
                    }
                    path.push_back(static_cast<unsigned>(idx.getInt()));
                  }
                  if (!validPath)
                    continue;
                  if (llvm::Constant *updated =
                          applyMemberFunctionPtrPtrauthAtPath(
                              rewrittenInit, path,
                              discAttr.getValue().getZExtValue()))
                    rewrittenInit = updated;
                }
                if (rewrittenInit != init)
                  moduleTranslation.remapValue(valueResult, rewrittenInit);
              }
            };

        if (auto constOp = llvm::dyn_cast<mlir::LLVM::ConstantOp>(op))
          rewriteMemberFunctionPtrConstant(constOp.getResult());
        else if (auto insertValueOp =
                     llvm::dyn_cast<mlir::LLVM::InsertValueOp>(op))
          rewriteMemberFunctionPtrConstant(insertValueOp.getRes());

        op->removeAttr(attribute.getName());
      } else if (attribute.getName() == "cir.no_cfi") {
        auto rewriteNoCFIConstant = [&](mlir::Value valueResult) {
          if (auto payload =
                  mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
            llvm::Value *mappedValue = moduleTranslation.lookupValue(valueResult);
            llvm::Constant *init = llvm::dyn_cast<llvm::Constant>(mappedValue);
            if (!init)
              return;

            llvm::Constant *rewrittenInit = init;
            for (mlir::Attribute entryAttr : payload) {
              auto entry = mlir::dyn_cast<mlir::ArrayAttr>(entryAttr);
              if (!entry)
                continue;
              llvm::SmallVector<unsigned, 8> path;
              bool validPath = true;
              for (mlir::Attribute idxAttr : entry) {
                auto idx = mlir::dyn_cast<mlir::IntegerAttr>(idxAttr);
                if (!idx) {
                  validPath = false;
                  break;
                }
                path.push_back(static_cast<unsigned>(idx.getInt()));
              }
              if (!validPath)
                continue;
              if (llvm::Constant *updated =
                      applyNoCFIAtPath(rewrittenInit, path))
                rewrittenInit = updated;
            }
            if (rewrittenInit != init)
              moduleTranslation.remapValue(valueResult, rewrittenInit);
          }
        };

        if (auto constOp = llvm::dyn_cast<mlir::LLVM::ConstantOp>(op))
          rewriteNoCFIConstant(constOp.getResult());
        else if (auto insertValueOp =
                     llvm::dyn_cast<mlir::LLVM::InsertValueOp>(op))
          rewriteNoCFIConstant(insertValueOp.getRes());

        op->removeAttr(attribute.getName());
      }
    } else if (llvm::isa<mlir::LLVM::GEPOp>(op)) {
      if (attribute.getName() == "cir.nosanitize") {
        for (auto *inst : instructions) {
          if (auto *gepInst = llvm::dyn_cast<llvm::GetElementPtrInst>(inst))
            gepInst->setNoSanitizeMetadata();
        }
        op->removeAttr(attribute.getName());
      }
    } else if (llvm::isa<mlir::LLVM::MemcpyOp, mlir::LLVM::MemmoveOp,
                         mlir::LLVM::MemcpyInlineOp, mlir::LLVM::MemsetOp,
                         mlir::LLVM::MemsetInlineOp>(op)) {
      if (attribute.getName() == "cir.auto_init") {
        for (auto *inst : instructions) {
          if (auto *instr = llvm::dyn_cast<llvm::Instruction>(inst)) {
            llvm::Metadata *ops[] = {
                llvm::MDString::get(instr->getContext(), "auto-init")};
            instr->setMetadata(llvm::LLVMContext::MD_annotation,
                               llvm::MDNode::get(instr->getContext(), ops));
          }
        }
        op->removeAttr(attribute.getName());
      } else if (attribute.getName() == "cir.nosanitize") {
        for (auto *inst : instructions) {
          if (auto *instr = llvm::dyn_cast<llvm::Instruction>(inst))
            instr->setNoSanitizeMetadata();
        }
        op->removeAttr(attribute.getName());
      }
      // Forward !tbaa.struct metadata for aggregate memcpy/memmove operations.
      else if (attribute.getName() == "cir.tbaa_struct") {
        if (auto tbaaStruct =
                mlir::dyn_cast<mlir::ArrayAttr>(attribute.getValue())) {
          llvm::SmallVector<llvm::Metadata *, 12> mdOps;
          auto *i64Ty =
              llvm::Type::getInt64Ty(moduleTranslation.getLLVMContext());

          auto appendTriple = [&](mlir::ArrayAttr triple) {
            if (triple.size() != 3)
              return;
            auto off = mlir::dyn_cast<mlir::IntegerAttr>(triple[0]);
            auto size = mlir::dyn_cast<mlir::IntegerAttr>(triple[1]);
            auto tag = mlir::dyn_cast<mlir::LLVM::TBAATagAttr>(triple[2]);
            if (!off || !size || !tag)
              return;
            if (auto *tagNode = getOrCreateTBAATagMetadataNode(
                    tag, moduleTranslation.getLLVMContext())) {
              mdOps.push_back(llvm::ConstantAsMetadata::get(
                  llvm::ConstantInt::get(i64Ty, off.getInt())));
              mdOps.push_back(llvm::ConstantAsMetadata::get(
                  llvm::ConstantInt::get(i64Ty, size.getInt())));
              mdOps.push_back(tagNode);
            }
          };

          // Support either a list of triples or a flat [off, size, tag] tuple.
          if (!tbaaStruct.empty() &&
              mlir::isa<mlir::ArrayAttr>(tbaaStruct[0])) {
            for (mlir::Attribute elt : tbaaStruct)
              if (auto triple = mlir::dyn_cast<mlir::ArrayAttr>(elt))
                appendTriple(triple);
          } else if (tbaaStruct.size() == 3) {
            appendTriple(tbaaStruct);
          }

          if (!mdOps.empty()) {
            auto *node =
                llvm::MDNode::get(moduleTranslation.getLLVMContext(), mdOps);
            for (auto *inst : instructions) {
              if (auto *instr = llvm::dyn_cast<llvm::Instruction>(inst))
                instr->setMetadata(llvm::LLVMContext::MD_tbaa_struct, node);
            }
          }
        }
        op->removeAttr(attribute.getName());
      }
    } else if ((attribute.getName() == "cir.fp_exception" ||
                attribute.getName() == "cir.rounding_mode") &&
               op->hasAttr("cir.fp_exception")) {
      return amendFPConstraintAttrs(op, instructions, moduleTranslation);
    } else if (llvm::isa<mlir::LLVM::AllocaOp>(op) &&
               attribute.getName() == "cir.swifterror") {
      // Set the swifterror attribute on the LLVM AllocaInst. MLIR's LLVM
      // dialect AllocaOp doesn't have a built-in swifterror attribute, so
      // we carry it as a discardable CIR attribute and apply it here during
      // MLIR-to-LLVM-IR translation.
      for (auto *inst : instructions) {
        if (auto *allocaInst = llvm::dyn_cast<llvm::AllocaInst>(inst))
          allocaInst->setSwiftError(true);
      }
      op->removeAttr(attribute.getName());
    } else if (llvm::isa<mlir::LLVM::AllocaOp>(op) &&
               attribute.getName() == "cir.coro_outside_frame") {
      for (auto *inst : instructions) {
        if (auto *allocaInst = llvm::dyn_cast<llvm::AllocaInst>(inst))
          allocaInst->setMetadata(
              llvm::LLVMContext::MD_coro_outside_frame,
              llvm::MDNode::get(allocaInst->getContext(), {}));
      }
      op->removeAttr(attribute.getName());
    } else if (attribute.getName() == "cir.x_volatile" ||
               attribute.getName() == "cir.v_volatile") {
      // CIR-private volatile flags for `omp.atomic.read` / `omp.atomic.write`.
      // The shared OpenMP-to-LLVM translation emits non-volatile atomic
      // load/store inside a successor basic block reached via a branch from
      // the captured-instruction scope, so we walk that successor block to
      // find the atomic op. `cir.x_volatile` applies to the load-from-x in
      // atomic.read or the store-to-x in atomic.write; `cir.v_volatile`
      // applies to the store-to-v in atomic.read.
      bool isXVolatile = attribute.getName() == "cir.x_volatile";
      bool isAtomicWrite = llvm::isa<mlir::omp::AtomicWriteOp>(op);
      auto applyVolatile = [&](llvm::Instruction *inst) {
        // omp.atomic.read => atomic LoadInst (x) + plain StoreInst (v).
        // omp.atomic.write => atomic StoreInst (x).
        if (auto *load = llvm::dyn_cast<llvm::LoadInst>(inst)) {
          if (load->isAtomic() && isXVolatile && !isAtomicWrite)
            load->setVolatile(true);
        } else if (auto *store = llvm::dyn_cast<llvm::StoreInst>(inst)) {
          if (isAtomicWrite) {
            if (isXVolatile && store->isAtomic())
              store->setVolatile(true);
          } else if (!isXVolatile && !store->isAtomic()) {
            store->setVolatile(true);
          }
        }
      };
      for (auto *inst : instructions) {
        applyVolatile(inst);
        // The captured scope often only contains the branch into the block
        // holding the atomic load/store; follow the branch's successor block
        // and walk it for atomic ops.
        if (auto *br = llvm::dyn_cast<llvm::BranchInst>(inst)) {
          if (br->isUnconditional()) {
            for (llvm::Instruction &succInst : *br->getSuccessor(0))
              applyVolatile(&succInst);
          }
        }
      }
      op->removeAttr(attribute.getName());
    } else if (llvm::isa<mlir::omp::ParallelOp>(op) &&
               (attribute.getName() == "cir.instrprof_func_name" ||
                attribute.getName() == "cir.instrprof_func_linkage")) {
      // OpenMP-parallel PGO instrprof rewrite. CIR codegen emits
      // `__cir_instrprof_increment` stub calls inside the parallel-region
      // body and tags the omp.parallel op with `cir.instrprof_func_name` +
      // `cir.instrprof_func_linkage`. The translation captures the
      // parallel-region body's instructions in our scope, including the
      // stub calls; we rewrite them to `llvm.instrprof.increment` here.
      //
      // We must NOT walk the captured-instruction list (`instructions`):
      // translating a nested omp.wsloop runs `createReductionsAndCleanup`,
      // which `eraseFromParent()`s reduction-cleanup instructions that are
      // still present in the captured list, leaving dangling pointers
      // (heap-use-after-free). Additionally, OpenMP outlines the parallel
      // region into a separate function, relocating the stub calls away from
      // the captured scope entirely. Instead, we locate the live stub calls
      // robustly via the never-stale use-list of the
      // `__cir_instrprof_increment` declaration.
      //
      // Linkage is read once via `cir.instrprof_func_name`; the linkage
      // attribute itself is dropped without further work to avoid handling
      // it twice.
      if (attribute.getName() == "cir.instrprof_func_linkage") {
        op->removeAttr(attribute.getName());
        return mlir::success();
      }
      auto strAttr = mlir::dyn_cast<mlir::StringAttr>(attribute.getValue());
      if (!strAttr) {
        op->removeAttr(attribute.getName());
        return mlir::success();
      }
      auto linkage = llvm::GlobalValue::InternalLinkage;
      if (auto linkageAttr = op->getAttrOfType<mlir::IntegerAttr>(
              "cir.instrprof_func_linkage"))
        linkage = static_cast<llvm::GlobalValue::LinkageTypes>(
            linkageAttr.getInt());
      llvm::Module *mod = moduleTranslation.getLLVMModule();
      // Rewrite both the plain (+1) and stepped instrprof stubs to their
      // respective intrinsics.
      const std::pair<llvm::StringRef, llvm::Intrinsic::ID> stubs[] = {
          {"__cir_instrprof_increment", llvm::Intrinsic::instrprof_increment},
          {"__cir_instrprof_increment_step",
           llvm::Intrinsic::instrprof_increment_step}};
      for (auto [stubSym, intrinsicID] : stubs) {
        llvm::Function *stubFn = mod->getFunction(stubSym);
        if (!stubFn)
          continue;
        auto varName =
            llvm::getPGOFuncNameVarName(strAttr.getValue(), linkage);
        auto *funcNameVar = mod->getNamedGlobal(varName);
        if (!funcNameVar)
          funcNameVar =
              llvm::createPGOFuncNameVar(*mod, linkage, strAttr.getValue());
        auto *instrprofFn =
            llvm::Intrinsic::getOrInsertDeclaration(mod, intrinsicID);
        auto *normalizedPtr =
            llvm::ConstantExpr::getPointerBitCastOrAddrSpaceCast(
                funcNameVar,
                llvm::PointerType::get(mod->getContext(), 0));
        // Snapshot users first; setCalledFunction mutates the use-list.
        llvm::SmallVector<llvm::CallBase *, 4> stubCalls;
        for (llvm::User *user : stubFn->users())
          if (auto *callBase = llvm::dyn_cast<llvm::CallBase>(user))
            if (callBase->getCalledFunction() == stubFn)
              stubCalls.push_back(callBase);
        // Note: we deliberately do NOT call createPGOFuncNameMetadata here.
        // The OpenMP outlining hook attachPGOFuncNameMetadataIfMissing()
        // (run during OpenMPIRBuilder::finalize) scans each outlined function
        // for llvm.instrprof.increment calls and attaches the correct
        // !PGOFuncName from the FuncNameVar initializer, matching the classic
        // (non-CIR) code path. Setting it here on the call's current parent
        // function risks mis-tagging the wrong function.
        for (llvm::CallBase *callBase : stubCalls) {
          callBase->setCalledFunction(instrprofFn);
          callBase->setArgOperand(0, normalizedPtr);
        }
      }
      op->removeAttr(attribute.getName());
      op->removeAttr("cir.instrprof_func_linkage");
    }
    return mlir::success();
  }

private:
  static llvm::BasicBlock *skipEmptyCleanupBlocks(llvm::BasicBlock *bb) {
    llvm::SmallPtrSet<llvm::BasicBlock *, 8> visited;
    while (bb && visited.insert(bb).second) {
      if (!bb->phis().empty())
        return nullptr;

      bool hasRealInst = false;
      for (llvm::Instruction &inst : *bb) {
        if (&inst == bb->getTerminator())
          continue;
        hasRealInst = true;
        break;
      }
      if (hasRealInst)
        return bb;

      auto *br = llvm::dyn_cast<llvm::BranchInst>(bb->getTerminator());
      if (!br || !br->isUnconditional())
        return nullptr;
      bb = br->getSuccessor(0);
    }
    return nullptr;
  }

  static bool collectLinearCleanupChain(llvm::BasicBlock *startBB,
                                        llvm::SmallVectorImpl<llvm::BasicBlock *> &blocks) {
    llvm::SmallPtrSet<llvm::BasicBlock *, 8> visited;
    bool hasCleanupInsts = false;
    llvm::BasicBlock *bb = startBB;
    while (bb && visited.insert(bb).second) {
      if (!bb->phis().empty())
        return false;
      blocks.push_back(bb);

      for (llvm::Instruction &inst : *bb) {
        if (&inst == bb->getTerminator())
          continue;
        hasCleanupInsts = true;
        if (auto *callBase = llvm::dyn_cast<llvm::CallBase>(&inst))
          if (!callBase->doesNotThrow())
            return false;
      }

      llvm::Instruction *term = bb->getTerminator();
      if (llvm::isa<llvm::ReturnInst>(term))
        return hasCleanupInsts;

      auto *br = llvm::dyn_cast<llvm::BranchInst>(term);
      if (!br || !br->isUnconditional())
        return false;
      bb = br->getSuccessor(0);
    }
    return false;
  }

  static llvm::CallBase *findNormalCoroutineEnd(llvm::Function *fn) {
    llvm::CallBase *coroEndCall = nullptr;
    for (llvm::BasicBlock &bb : *fn) {
      if (!llvm::isa<llvm::ReturnInst>(bb.getTerminator()) ||
          !bb.phis().empty())
        continue;

      for (llvm::Instruction &inst : bb) {
        if (&inst == bb.getTerminator())
          break;

        auto *callBase = llvm::dyn_cast<llvm::CallBase>(&inst);
        auto *calledFn = callBase ? callBase->getCalledFunction() : nullptr;
        if (!calledFn || !calledFn->isIntrinsic() ||
            calledFn->getIntrinsicID() != llvm::Intrinsic::coro_end)
          continue;

        auto *unwindArg =
            llvm::dyn_cast<llvm::ConstantInt>(callBase->getArgOperand(1));
        if (!unwindArg || unwindArg->isOne())
          continue;

        // Coroutine factories can still materialize the returned handle after
        // llvm.coro.end(..., false) in the shared return block. We only need
        // the unique normal coro.end site itself.
        if (coroEndCall)
          return nullptr;
        coroEndCall = callBase;
      }
    }
    return coroEndCall;
  }

  static bool collectLinearCleanupChainToTarget(
      llvm::BasicBlock *startBB, llvm::BasicBlock *targetBB,
      llvm::SmallVectorImpl<llvm::BasicBlock *> &blocks) {
    llvm::SmallPtrSet<llvm::BasicBlock *, 8> visited;
    bool hasCleanupInsts = false;
    llvm::BasicBlock *bb = startBB;
    while (bb && bb != targetBB && visited.insert(bb).second) {
      if (!bb->phis().empty())
        return false;
      blocks.push_back(bb);

      for (llvm::Instruction &inst : *bb) {
        if (&inst == bb->getTerminator())
          continue;
        hasCleanupInsts = true;
        if (auto *callBase = llvm::dyn_cast<llvm::CallBase>(&inst))
          if (!callBase->doesNotThrow())
            return false;
      }

      auto *br = llvm::dyn_cast<llvm::BranchInst>(bb->getTerminator());
      if (!br || !br->isUnconditional())
        return false;
      bb = br->getSuccessor(0);
    }
    return bb == targetBB && hasCleanupInsts;
  }

  static bool collectCoroutineEndCleanupChain(
      llvm::BasicBlock *coroEndBB,
      llvm::SmallVectorImpl<llvm::BasicBlock *> &blocks) {
    bool foundCleanupChain = false;
    for (llvm::BasicBlock *pred : llvm::predecessors(coroEndBB)) {
      llvm::SmallVector<llvm::BasicBlock *, 4> predBlocks;
      if (!collectLinearCleanupChainToTarget(pred, coroEndBB, predBlocks))
        continue;
      if (foundCleanupChain)
        return false;
      blocks.assign(predBlocks.begin(), predBlocks.end());
      foundCleanupChain = true;
    }
    return foundCleanupChain;
  }

  static bool collectCleanupBlockInstructions(
      llvm::BasicBlock *bb,
      llvm::SmallVectorImpl<llvm::Instruction *> &insts) {
    if (!bb || !bb->getTerminator() || !bb->phis().empty())
      return false;

    bool hasCleanupInsts = false;
    for (llvm::Instruction &inst : *bb) {
      if (&inst == bb->getTerminator())
        continue;
      hasCleanupInsts = true;
      if (auto *callBase = llvm::dyn_cast<llvm::CallBase>(&inst))
        if (!callBase->doesNotThrow())
          return false;
      insts.push_back(&inst);
    }
    return hasCleanupInsts;
  }

  static bool hasCoroFreeCall(llvm::ArrayRef<llvm::Instruction *> insts) {
    for (llvm::Instruction *inst : insts) {
      auto *callBase = llvm::dyn_cast<llvm::CallBase>(inst);
      auto *calledFn = callBase ? callBase->getCalledFunction() : nullptr;
      if (calledFn && calledFn->isIntrinsic() &&
          calledFn->getIntrinsicID() == llvm::Intrinsic::coro_free)
        return true;
    }
    return false;
  }

  static void cloneCleanupInstructions(
      llvm::IRBuilder<> &builder, llvm::ArrayRef<llvm::Instruction *> insts,
      llvm::DenseMap<llvm::Value *, llvm::Value *> &valueMap) {
    for (llvm::Instruction *inst : insts) {
      auto *clone = inst->clone();
      for (unsigned i = 0, e = clone->getNumOperands(); i != e; ++i) {
        llvm::Value *operand = clone->getOperand(i);
        if (auto it = valueMap.find(operand); it != valueMap.end())
          clone->setOperand(i, it->second);
      }
      builder.Insert(clone);
      valueMap[inst] = clone;
    }
  }

  // Clone a single-entry/single-exit region of basic blocks (in `blocks`, entry
  // first) into freshly created blocks, preserving the region's internal CFG.
  // The one edge that leaves the region (to `exitBB`) is redirected to
  // `newExitDest`. `valueMap` is shared with any predecessor cleanup blocks so
  // that values (e.g. a cloned coro.free result or the coro.free slot store)
  // flow into this region's clones; every cloned instruction and block is also
  // added to it so intra-region uses and branch targets are remapped to the
  // clones. Returns the block that clones `blocks.front()`, i.e. the region's
  // new entry.
  //
  // This is used to rematerialize the frame-dealloc region on a synthesized
  // exception cleanup path. Cloning the whole region (frame reload, any
  // sanitizer check diamond, coro.size and the operator-delete call) keeps the
  // pointer that feeds operator delete defined locally, so it dominates the
  // delete. Mirrors classic CodeGen's CallCoroDelete::Emit, which re-emits the
  // entire `if (coro.free) delete` sequence for each cleanup firing.
  static llvm::BasicBlock *cloneCleanupRegion(
      llvm::Function *fn, llvm::BasicBlock *insertBefore,
      llvm::ArrayRef<llvm::BasicBlock *> blocks, llvm::BasicBlock *exitBB,
      llvm::BasicBlock *newExitDest,
      llvm::DenseMap<llvm::Value *, llvm::Value *> &valueMap) {
    llvm::LLVMContext &ctx = fn->getContext();

    // First pass: create an empty clone for every block so that intra-region
    // branch targets (including back/forward edges within a sanitizer diamond)
    // can be remapped in the second pass.
    for (llvm::BasicBlock *bb : blocks) {
      auto *clone = llvm::BasicBlock::Create(
          ctx, bb->getName() + ".endcatch.coro", fn, insertBefore);
      valueMap[bb] = clone;
    }

    // Second pass: clone each block's instructions (terminator included),
    // remapping value operands and successor block operands through valueMap.
    for (llvm::BasicBlock *bb : blocks) {
      auto *cloneBB = llvm::cast<llvm::BasicBlock>(valueMap[bb]);
      llvm::IRBuilder<> builder(cloneBB);
      for (llvm::Instruction &inst : *bb) {
        auto *clone = inst.clone();
        for (unsigned i = 0, e = clone->getNumOperands(); i != e; ++i) {
          llvm::Value *operand = clone->getOperand(i);
          // Redirect the single edge that leaves the region.
          if (operand == exitBB)
            operand = newExitDest;
          if (auto it = valueMap.find(operand); it != valueMap.end())
            operand = it->second;
          clone->setOperand(i, operand);
        }
        builder.Insert(clone);
        valueMap[&inst] = clone;
      }
    }

    return llvm::cast<llvm::BasicBlock>(valueMap[blocks.front()]);
  }

  // Collect the single-entry/single-exit region entered at `entryBB` whose only
  // exit edge targets `exitBB`. On success `blocks` holds every block of the
  // region (entry first, in a deterministic order) and every block reaches
  // `exitBB` without leaving through any other edge. Rejects PHIs (the cloned
  // region would need incoming-block fixups) and may-throw calls (a cleanup
  // must not itself unwind). Bounded so a malformed CFG cannot loop forever.
  static bool collectSingleExitCleanupRegion(
      llvm::BasicBlock *entryBB, llvm::BasicBlock *exitBB,
      llvm::SmallVectorImpl<llvm::BasicBlock *> &blocks) {
    if (entryBB == exitBB)
      return false;

    llvm::SmallPtrSet<llvm::BasicBlock *, 8> inRegion;
    llvm::SmallVector<llvm::BasicBlock *, 8> worklist;
    worklist.push_back(entryBB);
    inRegion.insert(entryBB);

    constexpr unsigned kMaxRegionBlocks = 64;
    while (!worklist.empty()) {
      llvm::BasicBlock *bb = worklist.pop_back_val();
      blocks.push_back(bb);
      if (blocks.size() > kMaxRegionBlocks)
        return false;

      if (!bb->phis().empty())
        return false;

      // The region is a cleanup diamond: interior control flow uses plain
      // branches, and a sanitizer trap block is a leaf ending in `unreachable`
      // (it aborts, so it neither reaches the exit nor escapes the region).
      // Anything else (switch/invoke/ret/resume) means this is not the simple
      // dealloc region we know how to rematerialize.
      llvm::Instruction *term = bb->getTerminator();
      if (!llvm::isa<llvm::BranchInst>(term) &&
          !llvm::isa<llvm::UnreachableInst>(term))
        return false;
      for (llvm::Instruction &inst : *bb) {
        if (&inst == term)
          continue;
        if (auto *callBase = llvm::dyn_cast<llvm::CallBase>(&inst))
          if (!callBase->doesNotThrow())
            return false;
      }

      for (llvm::BasicBlock *succ : llvm::successors(bb)) {
        if (succ == exitBB)
          continue;
        // A block inside the region must not escape to anywhere but `exitBB`;
        // in particular it may not branch back to the region entry (which would
        // make the entry multi-entry) nor to the coroutine's normal path.
        if (succ == entryBB)
          return false;
        if (inRegion.insert(succ).second)
          worklist.push_back(succ);
      }
    }

    // Every region block's non-exit successors are themselves in the region,
    // and the exit is reached, so this is a genuine single-exit region.
    return !blocks.empty();
  }

  struct GuardedCoroutineCleanupPattern {
    llvm::BasicBlock *guardBB = nullptr;
    llvm::BranchInst *guardBr = nullptr;
    llvm::BasicBlock *deleteBB = nullptr;
    llvm::SmallVector<llvm::Instruction *, 8> guardInsts;
    // The frame-dealloc region entered at `deleteBB`, whose only exit is the
    // coro.end block. A single block in the common case; multiple blocks when a
    // sanitizer (e.g. -fsanitize=function on the coro.size callee) inserts a
    // check diamond between the frame reload and operator delete.
    llvm::SmallVector<llvm::BasicBlock *, 4> deleteRegionBlocks;
  };

  static bool collectGuardedCoroutineEndCleanup(
      llvm::BasicBlock *coroEndBB, GuardedCoroutineCleanupPattern &pattern) {
    bool foundPattern = false;
    for (llvm::BasicBlock *pred : llvm::predecessors(coroEndBB)) {
      auto *guardBr = llvm::dyn_cast<llvm::BranchInst>(pred->getTerminator());
      if (!guardBr || !guardBr->isConditional() || !pred->phis().empty())
        continue;

      llvm::BasicBlock *deleteBB = nullptr;
      if (guardBr->getSuccessor(0) == coroEndBB)
        deleteBB = guardBr->getSuccessor(1);
      else if (guardBr->getSuccessor(1) == coroEndBB)
        deleteBB = guardBr->getSuccessor(0);
      else
        continue;

      GuardedCoroutineCleanupPattern candidate;
      candidate.guardBB = pred;
      candidate.guardBr = guardBr;
      candidate.deleteBB = deleteBB;
      if (!collectCleanupBlockInstructions(pred, candidate.guardInsts) ||
          !hasCoroFreeCall(candidate.guardInsts) ||
          !collectSingleExitCleanupRegion(deleteBB, coroEndBB,
                                          candidate.deleteRegionBlocks))
        continue;

      if (foundPattern)
        return false;
      pattern = std::move(candidate);
      foundPattern = true;
    }
    return foundPattern;
  }

  static llvm::BasicBlock *createCoroutineCleanupLandingPad(
      llvm::CallInst *callInst, llvm::BasicBlock *normalDest,
      llvm::CallBase *coroEndCall) {
    if (!callInst || !normalDest || !coroEndCall)
      return nullptr;

    llvm::BasicBlock *coroEndBB = coroEndCall->getParent();
    if (!coroEndBB || !coroEndBB->phis().empty())
      return nullptr;

    for (llvm::Instruction &inst : *coroEndBB) {
      if (&inst == coroEndCall)
        break;
      if (auto *callBase = llvm::dyn_cast<llvm::CallBase>(&inst))
        if (!callBase->doesNotThrow())
          return nullptr;
    }

    llvm::Function *fn = callInst->getParent()->getParent();
    llvm::LLVMContext &ctx = fn->getContext();
    auto *landingPadTy = llvm::StructType::get(
        llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx));

    auto *unwindBB =
        llvm::BasicBlock::Create(ctx, "lpad.endcatch.coro", fn, normalDest);
    auto *cleanupContBB = llvm::BasicBlock::Create(
        ctx, "cleanup.cont.endcatch.coro", fn, normalDest);
    auto *resumeBB =
        llvm::BasicBlock::Create(ctx, "eh.resume.endcatch.coro", fn, normalDest);

    llvm::IRBuilder<> unwindBuilder(unwindBB);
    auto *landingPad = unwindBuilder.CreateLandingPad(landingPadTy, 0);
    landingPad->setCleanup(true);

    auto *coroEndClone = llvm::cast<llvm::Instruction>(coroEndCall->clone());
    coroEndClone->setOperand(1, unwindBuilder.getTrue());
    unwindBuilder.Insert(coroEndClone);

    llvm::Function *coroIsInRampFn = llvm::Intrinsic::getOrInsertDeclaration(
        fn->getParent(), llvm::Intrinsic::coro_is_in_ramp);
    auto *inRamp = unwindBuilder.CreateCall(coroIsInRampFn, {}, "InRamp");
    unwindBuilder.CreateCondBr(inRamp, cleanupContBB, resumeBB);

    llvm::SmallVector<llvm::Instruction *, 8> cleanupInsts;
    GuardedCoroutineCleanupPattern guardedCleanup;
    bool hasGuardedCleanup =
        collectGuardedCoroutineEndCleanup(coroEndBB, guardedCleanup);
    for (llvm::Instruction &inst : *coroEndBB) {
      if (&inst == coroEndCall)
        break;
      cleanupInsts.push_back(&inst);
    }
    if (!hasGuardedCleanup && cleanupInsts.empty()) {
      llvm::SmallVector<llvm::BasicBlock *, 4> cleanupBlocks;
      if (collectCoroutineEndCleanupChain(coroEndBB, cleanupBlocks))
        for (llvm::BasicBlock *bb : cleanupBlocks)
          for (llvm::Instruction &inst : *bb)
            if (&inst != bb->getTerminator())
              cleanupInsts.push_back(&inst);
    }

    llvm::BasicBlock *cleanupTailBB = nullptr;
    llvm::BasicBlock *cleanupTailDest = resumeBB;
    if (hasGuardedCleanup && !cleanupInsts.empty()) {
      cleanupTailBB = llvm::BasicBlock::Create(
          ctx, "cleanup.tail.endcatch.coro", fn, normalDest);
      cleanupTailDest = cleanupTailBB;
    }

    if (hasGuardedCleanup) {
      llvm::IRBuilder<> cleanupBuilder(cleanupContBB);
      llvm::DenseMap<llvm::Value *, llvm::Value *> valueMap;
      cloneCleanupInstructions(cleanupBuilder, guardedCleanup.guardInsts,
                               valueMap);

      // Clone the whole frame-dealloc region (frame reload, any sanitizer check
      // diamond, coro.size and operator delete) so the pointer feeding delete
      // is defined locally and dominates its use. The region's exit edge (to
      // the coro.end block) is redirected to `cleanupTailDest`.
      llvm::BasicBlock *cleanupDeleteBB = cloneCleanupRegion(
          fn, normalDest, guardedCleanup.deleteRegionBlocks, coroEndBB,
          cleanupTailDest, valueMap);

      llvm::Value *cond = guardedCleanup.guardBr->getCondition();
      if (auto it = valueMap.find(cond); it != valueMap.end())
        cond = it->second;

      llvm::BasicBlock *succ0 =
          guardedCleanup.guardBr->getSuccessor(0) == guardedCleanup.deleteBB
              ? cleanupDeleteBB
              : cleanupTailDest;
      llvm::BasicBlock *succ1 =
          guardedCleanup.guardBr->getSuccessor(1) == guardedCleanup.deleteBB
              ? cleanupDeleteBB
              : cleanupTailDest;
      cleanupBuilder.CreateCondBr(cond, succ0, succ1);
    } else {
      llvm::IRBuilder<> cleanupBuilder(cleanupContBB);
      llvm::DenseMap<llvm::Value *, llvm::Value *> valueMap;
      cloneCleanupInstructions(cleanupBuilder, cleanupInsts, valueMap);
      cleanupBuilder.CreateBr(resumeBB);
    }

    if (cleanupTailBB) {
      llvm::IRBuilder<> cleanupTailBuilder(cleanupTailBB);
      llvm::DenseMap<llvm::Value *, llvm::Value *> valueMap;
      cloneCleanupInstructions(cleanupTailBuilder, cleanupInsts, valueMap);
      cleanupTailBuilder.CreateBr(resumeBB);
    }

    // The resume PHI takes the same landing-pad value from every predecessor.
    // Enumerate the actual predecessors from the finished CFG so that multi-
    // block cloned cleanup regions (e.g. a sanitizer check diamond) are handled
    // uniformly.
    llvm::IRBuilder<> resumeBuilder(resumeBB);
    llvm::SmallVector<llvm::BasicBlock *, 4> uniquePreds;
    llvm::SmallPtrSet<llvm::BasicBlock *, 4> seenPreds;
    for (llvm::BasicBlock *pred : llvm::predecessors(resumeBB))
      if (seenPreds.insert(pred).second)
        uniquePreds.push_back(pred);
    auto *resumeValue = resumeBuilder.CreatePHI(
        landingPadTy, uniquePreds.size(), "lpad.val");
    for (llvm::BasicBlock *resumePred : uniquePreds)
      resumeValue->addIncoming(landingPad, resumePred);
    resumeBuilder.CreateResume(resumeValue);

    return unwindBB;
  }

  static bool rewriteEndCatchWithCleanupInvoke(llvm::CallInst *callInst) {
    auto *calledFn = callInst ? callInst->getCalledFunction() : nullptr;
    if (!calledFn || calledFn->getName() != "__cxa_end_catch" ||
        callInst->doesNotThrow())
      return false;

    auto *callBB = callInst->getParent();
    auto *term = llvm::dyn_cast<llvm::BranchInst>(callBB->getTerminator());

    // If the end_catch is not the last instruction before an unconditional
    // branch (e.g., early-exit path where cleanup calls follow end_catch in
    // the same block), split the block after the call to create the expected
    // layout where end_catch terminates the block.
    if (!term || !term->isUnconditional() || callInst->getNextNode() != term) {
      auto *nextInst = callInst->getNextNode();
      if (!nextInst)
        return false;
      callBB->splitBasicBlock(nextInst, "endcatch.split");
      term = llvm::cast<llvm::BranchInst>(callBB->getTerminator());
    }

    llvm::BasicBlock *normalDest = term->getSuccessor(0);

    // Follow empty blocks to find either a cleanup chain or a shared
    // cleanup landing pad from a subsequent invoke in the same scope.
    llvm::BasicBlock *unwindBB = nullptr;

    // First, try to find the cleanup chain via skipEmptyCleanupBlocks.
    llvm::BasicBlock *cleanupStart = skipEmptyCleanupBlocks(normalDest);

    // If skipEmptyCleanupBlocks fails (e.g., the chain ends with an invoke
    // terminator which looks "empty"), follow the branch chain to find
    // a block ending with an invoke whose unwind dest we can share.
    if (!cleanupStart) {
      llvm::SmallPtrSet<llvm::BasicBlock *, 8> visited;
      llvm::BasicBlock *bb = normalDest;
      while (bb && visited.insert(bb).second) {
        if (!bb->phis().empty())
          break;
        if (auto *inv = llvm::dyn_cast<llvm::InvokeInst>(bb->getTerminator())) {
          unwindBB = inv->getUnwindDest();
          break;
        }
        auto *br = llvm::dyn_cast<llvm::BranchInst>(bb->getTerminator());
        if (!br || !br->isUnconditional())
          break;
        bb = br->getSuccessor(0);
      }
    }

    llvm::SmallVector<llvm::BasicBlock *, 8> cleanupBlocks;
    if (cleanupStart && collectLinearCleanupChain(cleanupStart, cleanupBlocks)) {
      // The cleanup is a simple linear chain of nothrow calls ending in
      // a return. Clone the chain into a new landing pad block.
      llvm::Function *fn = callBB->getParent();
      llvm::LLVMContext &ctx = fn->getContext();
      unwindBB = llvm::BasicBlock::Create(ctx, "lpad.endcatch.cleanup",
                                          fn, normalDest);
      llvm::IRBuilder<> cleanupBuilder(unwindBB);
      auto *landingPadTy = llvm::StructType::get(
          llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx));
      auto *landingPad = cleanupBuilder.CreateLandingPad(landingPadTy, 0);
      landingPad->setCleanup(true);

      llvm::DenseMap<llvm::Value *, llvm::Value *> valueMap;
      for (llvm::BasicBlock *bb : cleanupBlocks) {
        for (llvm::Instruction &inst : *bb) {
          if (&inst == bb->getTerminator())
            continue;
          auto *clone = inst.clone();
          for (unsigned i = 0, e = clone->getNumOperands(); i != e; ++i) {
            llvm::Value *operand = clone->getOperand(i);
            if (auto it = valueMap.find(operand); it != valueMap.end())
              clone->setOperand(i, it->second);
          }
          cleanupBuilder.Insert(clone);
          valueMap[&inst] = clone;
        }
      }
      cleanupBuilder.CreateResume(landingPad);
    }

    // Fallback: when the normal-path-forward search fails (e.g., continue
    // inside a catch within a loop where the normal path goes back to the
    // loop), search for resume blocks in the function whose cleanup actions
    // (nothrow destructor calls) can be cloned into a new landing pad. Only
    // consider blocks with actual RAII destructor calls — skip blocks whose
    // only cleanup action is __cxa_end_catch (those belong to other catch
    // handlers, not to outer RAII scopes).
    if (!unwindBB) {
      llvm::Function *fn = callBB->getParent();
      llvm::LLVMContext &ctx = fn->getContext();
      for (llvm::BasicBlock &bb : *fn) {
        if (!llvm::isa<llvm::ResumeInst>(bb.getTerminator()))
          continue;

        // Collect cleanup instructions: skip PHI, landingpad, insertvalue,
        // extractvalue (these are EH bookkeeping, not actual cleanups).
        llvm::SmallVector<llvm::Instruction *, 4> cleanupInsts;
        bool valid = true;
        bool hasNonEndCatchCleanup = false;
        for (llvm::Instruction &inst : bb) {
          if (&inst == bb.getTerminator())
            continue;
          if (llvm::isa<llvm::PHINode>(&inst) ||
              llvm::isa<llvm::LandingPadInst>(&inst) ||
              llvm::isa<llvm::InsertValueInst>(&inst) ||
              llvm::isa<llvm::ExtractValueInst>(&inst))
            continue;
          if (auto *call = llvm::dyn_cast<llvm::CallBase>(&inst)) {
            if (!call->doesNotThrow()) {
              valid = false;
              break;
            }
            // Skip blocks whose cleanup is only __cxa_end_catch — those
            // belong to other catch handlers, not outer RAII destructors.
            auto *calledFn = call->getCalledFunction();
            if (!calledFn || calledFn->getName() != "__cxa_end_catch")
              hasNonEndCatchCleanup = true;
          } else {
            hasNonEndCatchCleanup = true;
          }
          // Reject cleanup instructions that use values defined in other
          // blocks: those values may not dominate the new landing pad we
          // are about to create, causing "Instruction does not dominate
          // all uses!" verifier errors.  Only operands defined within this
          // same cleanup block (which will be cloned) or constants/globals
          // /arguments (which dominate everything) are safe.
          for (llvm::Value *operand : inst.operands()) {
            if (llvm::isa<llvm::Constant>(operand) ||
                llvm::isa<llvm::Argument>(operand))
              continue;
            if (auto *opInst = llvm::dyn_cast<llvm::Instruction>(operand)) {
              if (opInst->getParent() != &bb) {
                valid = false;
                break;
              }
            }
          }
          if (!valid)
            break;
          cleanupInsts.push_back(&inst);
        }
        if (!valid || cleanupInsts.empty() || !hasNonEndCatchCleanup)
          continue;

        // Clone the cleanup instructions into a new landing pad block.
        unwindBB = llvm::BasicBlock::Create(ctx, "lpad.endcatch.cleanup",
                                            fn, normalDest);
        llvm::IRBuilder<> cleanupBuilder(unwindBB);
        auto *landingPadTy = llvm::StructType::get(
            llvm::PointerType::getUnqual(ctx), llvm::Type::getInt32Ty(ctx));
        auto *landingPad = cleanupBuilder.CreateLandingPad(landingPadTy, 0);
        landingPad->setCleanup(true);

        llvm::DenseMap<llvm::Value *, llvm::Value *> valueMap;
        for (llvm::Instruction *inst : cleanupInsts) {
          auto *clone = inst->clone();
          for (unsigned i = 0, e = clone->getNumOperands(); i != e; ++i) {
            llvm::Value *operand = clone->getOperand(i);
            if (auto it = valueMap.find(operand); it != valueMap.end())
              clone->setOperand(i, it->second);
          }
          cleanupBuilder.Insert(clone);
          valueMap[inst] = clone;
        }
        cleanupBuilder.CreateResume(landingPad);
        break;
      }
    }

    // Coroutines can exit a catch handler into final_suspend. In that case the
    // normal successor after __cxa_end_catch is not a cleanup chain and there
    // is no pre-existing resume block to clone, but OGCG still creates a
    // landingpad that calls llvm.coro.end(..., true) and resumes. Rebuild that
    // cleanup path from the shared normal coro.end block.
    if (!unwindBB)
      if (llvm::CallBase *coroEndCall =
              findNormalCoroutineEnd(callBB->getParent()))
        unwindBB =
            createCoroutineCleanupLandingPad(callInst, normalDest, coroEndCall);

    if (!unwindBB)
      return false;

    llvm::IRBuilder<> builder(term);
    llvm::SmallVector<llvm::Value *, 4> args(callInst->args());
    auto *invoke = builder.CreateInvoke(callInst->getFunctionType(),
                                        callInst->getCalledOperand(),
                                        normalDest, unwindBB, args);
    invoke->setCallingConv(callInst->getCallingConv());
    invoke->setAttributes(callInst->getAttributes());
    invoke->setDebugLoc(callInst->getDebugLoc());
    invoke->copyMetadata(*callInst);

    term->eraseFromParent();
    callInst->eraseFromParent();
    return true;
  }

  /// Convert a cir.asm.goto op directly to an LLVM IR callbr instruction.
  /// This bypasses the MLIR LLVM dialect because it has no callbr equivalent.
  mlir::LogicalResult convertInlineAsmGotoOp(
      cir::InlineAsmGotoOp op, llvm::IRBuilderBase &builder,
      mlir::LLVM::ModuleTranslation &moduleTranslation) const {
    StringRef llvmAttrName = mlir::LLVM::InlineAsmOp::getElementTypeAttrName();

    // Collect operand values.
    SmallVector<llvm::Value *> args;
    for (auto operandRange : op.getAsmOperands())
      for (mlir::Value operand : operandRange)
        args.push_back(moduleTranslation.lookupValue(operand));

    // Build the LLVM function type for the inline asm.
    SmallVector<llvm::Type *> argTypes;
    for (auto *arg : args)
      argTypes.push_back(arg->getType());

    // Determine result type: void if no outputs, or the converted result type.
    llvm::Type *resultTy;
    if (op.getRes())
      resultTy = moduleTranslation.convertType(op.getRes().getType());
    else
      resultTy = llvm::Type::getVoidTy(moduleTranslation.getLLVMContext());

    llvm::FunctionType *funcTy =
        llvm::FunctionType::get(resultTy, argTypes, false);

    // Determine asm dialect.
    bool isATT = op.getAsmFlavor() == cir::AsmFlavor::x86_att;
    llvm::InlineAsm::AsmDialect asmDialect =
        isATT ? llvm::InlineAsm::AD_ATT : llvm::InlineAsm::AD_Intel;

    // Create the InlineAsm object.
    llvm::InlineAsm *ia = llvm::InlineAsm::get(
        funcTy, op.getAsmString().str(), op.getConstraints().str(),
        op.getSideEffects(), /*isAlignStack=*/false, asmDialect);

    // Get successor blocks.
    assert(op.getTargets().size() >= 2 &&
           "asm.goto must have fallthrough + at least one label");
    llvm::BasicBlock *fallthroughBB =
        moduleTranslation.lookupBlock(op.getTargets()[0]);

    SmallVector<llvm::BasicBlock *, 4> indirectDests;
    for (unsigned i = 1; i < op.getTargets().size(); ++i)
      indirectDests.push_back(
          moduleTranslation.lookupBlock(op.getTargets()[i]));

    // Emit callbr.
    llvm::CallBrInst *cbr =
        builder.CreateCallBr(ia, fallthroughBB, indirectDests, args);
    moduleTranslation.mapBranch(op, cbr);

    for (auto [argNo, operandAttr] : llvm::enumerate(op.getOperandAttrs())) {
      auto dictAttr = mlir::dyn_cast_or_null<mlir::DictionaryAttr>(operandAttr);
      if (!dictAttr)
        continue;

      auto typeAttr =
          mlir::dyn_cast_or_null<mlir::TypeAttr>(dictAttr.get(llvmAttrName));
      if (!typeAttr)
        continue;

      cbr->addParamAttr(
          argNo, llvm::Attribute::get(moduleTranslation.getLLVMContext(),
                                      llvm::Attribute::ElementType,
                                      moduleTranslation.convertType(
                                          typeAttr.getValue())));
    }

    // Asm goto is always nounwind (OGCG UpdateAsmCallInst CGStmt.cpp:2654-2655
    // passes HasUnwindClobber=false for the goto path).
    cbr->addFnAttr(llvm::Attribute::NoUnwind);
    if (op->hasAttr("cir.asm_readnone"))
      cbr->setDoesNotAccessMemory();
    else if (op->hasAttr("cir.asm_readonly"))
      cbr->setOnlyReadsMemory();
    // Forward nomerge from [[clang::nomerge]] statement attribute.
    if (op->hasAttr("cir.nomerge"))
      cbr->addFnAttr(llvm::Attribute::NoMerge);
    // Forward convergent for CUDA/OpenCL modes.
    if (op->hasAttr(cir::ConvergentAttr::getMnemonic()))
      cbr->addFnAttr(llvm::Attribute::Convergent);
    if (auto locs = op->getAttrOfType<mlir::ArrayAttr>("cir.asm_srcloc"))
      if (llvm::MDNode *srcloc =
              getAsmSrcLocMD(locs, moduleTranslation.getLLVMContext()))
        cbr->setMetadata("srcloc", srcloc);

    // Map the CIR result to the callbr value.
    if (op.getRes())
      moduleTranslation.mapValue(op.getRes(), cbr);

    // If the asm goto has register outputs, create split blocks for indirect
    // destinations to replicate the output stores. This matches OGCG's
    // split-block pattern — callbr results must be used in blocks dominated
    // by the destination, so we create a synthetic block for each indirect
    // destination that stores the output values before branching to the real
    // label block.
    if (op.getRes()) {
      // Find the output store destinations by walking the CIR uses of the
      // asm.goto result in the fallthrough block.
      SmallVector<AsmGotoOutputStoreInfo> outputStores;
      findAsmGotoOutputStores(op, moduleTranslation, outputStores);

      for (unsigned i = 0, e = indirectDests.size(); i < e; ++i) {
        llvm::BasicBlock *origDest = indirectDests[i];
        llvm::BasicBlock *splitBB = llvm::BasicBlock::Create(
            moduleTranslation.getLLVMContext(),
            origDest->getName() + ".split",
            fallthroughBB->getParent(), origDest);

        llvm::IRBuilder<> splitBuilder(splitBB);

        // Emit stores in the split block for each register output.
        emitSplitBlockStores(splitBuilder, cbr, outputStores);

        splitBuilder.CreateBr(origDest);

        // Update callbr to point to the split block instead.
        cbr->setIndirectDest(i, splitBB);
      }
    }

    return mlir::success();
  }

  enum class AsmGotoOutputTransformKind {
    Bitcast,
    SExt,
    ZExt,
    Trunc,
    IntToPtr,
    PtrToInt,
    FPTrunc,
    FPExt,
  };

  struct AsmGotoOutputTransform {
    AsmGotoOutputTransformKind kind;
    llvm::Type *type = nullptr;
  };

  struct AsmGotoOutputStoreInfo {
    llvm::Value *addr = nullptr;
    SmallVector<AsmGotoOutputTransform, 2> transforms;
  };

  bool getAsmGotoOutputTransform(
      mlir::Operation *user, mlir::Value source,
      mlir::LLVM::ModuleTranslation &moduleTranslation,
      AsmGotoOutputTransform &transform) const {
    if (user->getNumOperands() != 1 || user->getOperand(0) != source ||
        user->getNumResults() != 1)
      return false;

    llvm::Type *resultTy =
        moduleTranslation.convertType(user->getResult(0).getType());
    if (llvm::isa<mlir::LLVM::BitcastOp>(user))
      transform = {AsmGotoOutputTransformKind::Bitcast, resultTy};
    else if (llvm::isa<mlir::LLVM::SExtOp>(user))
      transform = {AsmGotoOutputTransformKind::SExt, resultTy};
    else if (llvm::isa<mlir::LLVM::ZExtOp>(user))
      transform = {AsmGotoOutputTransformKind::ZExt, resultTy};
    else if (llvm::isa<mlir::LLVM::TruncOp>(user))
      transform = {AsmGotoOutputTransformKind::Trunc, resultTy};
    else if (llvm::isa<mlir::LLVM::IntToPtrOp>(user))
      transform = {AsmGotoOutputTransformKind::IntToPtr, resultTy};
    else if (llvm::isa<mlir::LLVM::PtrToIntOp>(user))
      transform = {AsmGotoOutputTransformKind::PtrToInt, resultTy};
    else if (llvm::isa<mlir::LLVM::FPTruncOp>(user))
      transform = {AsmGotoOutputTransformKind::FPTrunc, resultTy};
    else if (llvm::isa<mlir::LLVM::FPExtOp>(user))
      transform = {AsmGotoOutputTransformKind::FPExt, resultTy};
    else
      return false;

    return true;
  }

  bool findAsmGotoOutputStore(
      mlir::Value source,
      mlir::LLVM::ModuleTranslation &moduleTranslation,
      AsmGotoOutputStoreInfo &outputStore) const {
    for (mlir::Operation *user : source.getUsers()) {
      if (auto storeOp = mlir::dyn_cast<mlir::LLVM::StoreOp>(user)) {
        if (storeOp.getValue() != source)
          continue;
        outputStore.addr = moduleTranslation.lookupValue(storeOp.getAddr());
        return true;
      }

      AsmGotoOutputTransform transform;
      if (!getAsmGotoOutputTransform(user, source, moduleTranslation,
                                     transform))
        continue;

      outputStore.transforms.push_back(transform);
      if (findAsmGotoOutputStore(user->getResult(0), moduleTranslation,
                                 outputStore))
        return true;
      outputStore.transforms.pop_back();
    }

    return false;
  }

  /// Find the output store destinations for an asm.goto with register outputs.
  /// After CIR-to-LLVM dialect lowering, the stores in the fallthrough block
  /// are LLVM dialect stores (not CIR stores). Walk the uses of the asm.goto
  /// result to find the final destination alloca plus any cast/truncation chain
  /// between the asm result and the eventual store.
  void findAsmGotoOutputStores(
      cir::InlineAsmGotoOp op,
      mlir::LLVM::ModuleTranslation &moduleTranslation,
      SmallVectorImpl<AsmGotoOutputStoreInfo> &outputStores) const {
    mlir::Value res = op.getRes();
    if (!res)
      return;

    llvm::Type *resultTy = moduleTranslation.convertType(res.getType());

    // For single output: trace the final store from the result through any
    // intervening unary cast ops (e.g. trunc from widened tied outputs).
    if (!resultTy->isStructTy()) {
      outputStores.resize(1);
      findAsmGotoOutputStore(res, moduleTranslation, outputStores[0]);
      return;
    }

    // For multiple outputs: CIR stores the struct result to a temp alloca,
    // then extracts individual fields via GEP+load and stores them to the
    // actual output allocas. Trace through that chain to find each output
    // alloca and any value-shaping casts so split blocks can replay the same
    // extractvalue/cast/store sequence directly from the callbr result.
    auto *structTy = llvm::cast<llvm::StructType>(resultTy);
    unsigned numOutputs = structTy->getNumElements();
    outputStores.resize(numOutputs);

    // Step 1: Find the store of the struct result to the temp alloca.
    mlir::Value tempAllocaAddr;
    for (mlir::Operation *user : res.getUsers()) {
      if (auto storeOp = mlir::dyn_cast<mlir::LLVM::StoreOp>(user)) {
        if (storeOp.getValue() == res) {
          tempAllocaAddr = storeOp.getAddr();
          break;
        }
      }
    }
    if (!tempAllocaAddr)
      return;

    // Step 2: Find GEP(temp_alloca, 0, i) → load → store(val, output_alloca).
    for (mlir::Operation *user : tempAllocaAddr.getUsers()) {
      auto gepOp = mlir::dyn_cast<mlir::LLVM::GEPOp>(user);
      if (!gepOp)
        continue;

      // Expect: getelementptr {struct}, ptr %temp, 0, <field_idx>
      auto rawIndices = gepOp.getRawConstantIndices();
      if (rawIndices.size() != 2 || rawIndices[0] != 0)
        continue;
      int32_t fieldIdx = rawIndices[1];
      if (fieldIdx < 0 || static_cast<unsigned>(fieldIdx) >= numOutputs)
        continue;

      // Find load from GEP result.
      for (mlir::Operation *gepUser : gepOp.getResult().getUsers()) {
        auto loadOp = mlir::dyn_cast<mlir::LLVM::LoadOp>(gepUser);
        if (!loadOp)
          continue;
        findAsmGotoOutputStore(loadOp.getResult(), moduleTranslation,
                               outputStores[fieldIdx]);
      }
    }
  }

  llvm::Value *applyAsmGotoOutputTransforms(
      llvm::IRBuilder<> &splitBuilder, llvm::Value *value,
      ArrayRef<AsmGotoOutputTransform> transforms) const {
    for (const AsmGotoOutputTransform &transform : transforms) {
      switch (transform.kind) {
      case AsmGotoOutputTransformKind::Bitcast:
        value = splitBuilder.CreateBitCast(value, transform.type);
        break;
      case AsmGotoOutputTransformKind::SExt:
        value = splitBuilder.CreateSExt(value, transform.type);
        break;
      case AsmGotoOutputTransformKind::ZExt:
        value = splitBuilder.CreateZExt(value, transform.type);
        break;
      case AsmGotoOutputTransformKind::Trunc:
        value = splitBuilder.CreateTrunc(value, transform.type);
        break;
      case AsmGotoOutputTransformKind::IntToPtr:
        value = splitBuilder.CreateIntToPtr(value, transform.type);
        break;
      case AsmGotoOutputTransformKind::PtrToInt:
        value = splitBuilder.CreatePtrToInt(value, transform.type);
        break;
      case AsmGotoOutputTransformKind::FPTrunc:
        value = splitBuilder.CreateFPTrunc(value, transform.type);
        break;
      case AsmGotoOutputTransformKind::FPExt:
        value = splitBuilder.CreateFPExt(value, transform.type);
        break;
      }
    }
    return value;
  }

  /// Emit stores in a split block for each register output.
  void emitSplitBlockStores(
      llvm::IRBuilder<> &splitBuilder,
      llvm::CallBrInst *cbr,
      ArrayRef<AsmGotoOutputStoreInfo> outputStores) const {
    for (unsigned j = 0, numOuts = outputStores.size(); j < numOuts; ++j) {
      if (!outputStores[j].addr)
        continue;

      llvm::Value *value =
          outputStores.size() == 1
              ? static_cast<llvm::Value *>(cbr)
              : splitBuilder.CreateExtractValue(cbr, j, "asmresult");
      value = applyAsmGotoOutputTransforms(splitBuilder, value,
                                           outputStores[j].transforms);
      splitBuilder.CreateStore(value, outputStores[j].addr);
    }
  }

  void emitOpenCLKernelMetadata(
      cir::OpenCLKernelMetadataAttr clKernelMetadata, llvm::Function *llvmFunc,
      mlir::LLVM::ModuleTranslation &moduleTranslation) const {
    auto &vmCtx = moduleTranslation.getLLVMContext();

    auto lowerArrayAttr = [&](mlir::ArrayAttr arrayAttr) {
      llvm::SmallVector<llvm::Metadata *, 3> attrMDArgs;
      for (mlir::Attribute attr : arrayAttr) {
        int64_t value = mlir::cast<mlir::IntegerAttr>(attr).getInt();
        attrMDArgs.push_back(
            llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                llvm::IntegerType::get(vmCtx, 32), llvm::APInt(32, value))));
      }
      return llvm::MDNode::get(vmCtx, attrMDArgs);
    };

    if (auto workGroupSizeHint = clKernelMetadata.getWorkGroupSizeHint()) {
      llvmFunc->setMetadata("work_group_size_hint",
                            lowerArrayAttr(workGroupSizeHint));
    }

    if (auto reqdWorkGroupSize = clKernelMetadata.getReqdWorkGroupSize()) {
      llvmFunc->setMetadata("reqd_work_group_size",
                            lowerArrayAttr(reqdWorkGroupSize));
    }

    if (auto vecTypeHint = clKernelMetadata.getVecTypeHint()) {
      auto hintQTy = vecTypeHint.getValue();
      bool isSignedInteger = *clKernelMetadata.getVecTypeHintSignedness();
      llvm::Metadata *attrMDArgs[] = {
          llvm::ConstantAsMetadata::get(
              llvm::PoisonValue::get(moduleTranslation.convertType(hintQTy))),
          llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
              llvm::IntegerType::get(vmCtx, 32),
              llvm::APInt(32, (uint64_t)(isSignedInteger ? 1 : 0))))};
      llvmFunc->setMetadata("vec_type_hint",
                            llvm::MDNode::get(vmCtx, attrMDArgs));
    }

    if (auto intelReqdSubgroupSize =
            clKernelMetadata.getIntelReqdSubGroupSize()) {
      int64_t reqdSubgroupSize = intelReqdSubgroupSize.getInt();
      llvm::Metadata *attrMDArgs[] = {
          llvm::ConstantAsMetadata::get(
              llvm::ConstantInt::get(llvm::IntegerType::get(vmCtx, 32),
                                     llvm::APInt(32, reqdSubgroupSize))),
      };
      llvmFunc->setMetadata("intel_reqd_sub_group_size",
                            llvm::MDNode::get(vmCtx, attrMDArgs));
    }
  }

  void emitOpenCLKernelArgMetadata(
      cir::OpenCLKernelArgMetadataAttr clArgMetadata, llvm::Function *llvmFunc,
      mlir::LLVM::ModuleTranslation &moduleTranslation) const {
    auto &vmCtx = moduleTranslation.getLLVMContext();

    // MDNode for the kernel argument address space qualifiers.
    SmallVector<llvm::Metadata *, 8> addressQuals;

    // MDNode for the kernel argument access qualifiers (images only).
    SmallVector<llvm::Metadata *, 8> accessQuals;

    // MDNode for the kernel argument type names.
    SmallVector<llvm::Metadata *, 8> argTypeNames;

    // MDNode for the kernel argument base type names.
    SmallVector<llvm::Metadata *, 8> argBaseTypeNames;

    // MDNode for the kernel argument type qualifiers.
    SmallVector<llvm::Metadata *, 8> argTypeQuals;

    // MDNode for the kernel argument names.
    SmallVector<llvm::Metadata *, 8> argNames;

    auto lowerStringAttr = [&](mlir::Attribute strAttr) {
      return llvm::MDString::get(
          vmCtx, mlir::cast<mlir::StringAttr>(strAttr).getValue());
    };

    bool shouldEmitArgName = !!clArgMetadata.getName();

    auto addressSpaceValues =
        clArgMetadata.getAddrSpace().getAsValueRange<mlir::IntegerAttr>();

    for (auto &&[i, addrSpace] : llvm::enumerate(addressSpaceValues)) {
      // Address space qualifier.
      addressQuals.push_back(
          llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
              llvm::IntegerType::get(vmCtx, 32), addrSpace)));

      // Access qualifier.
      accessQuals.push_back(lowerStringAttr(clArgMetadata.getAccessQual()[i]));

      // Type name.
      argTypeNames.push_back(lowerStringAttr(clArgMetadata.getType()[i]));

      // Base type name.
      argBaseTypeNames.push_back(
          lowerStringAttr(clArgMetadata.getBaseType()[i]));

      // Type qualifier.
      argTypeQuals.push_back(lowerStringAttr(clArgMetadata.getTypeQual()[i]));

      // Argument name.
      if (shouldEmitArgName)
        argNames.push_back(lowerStringAttr(clArgMetadata.getName()[i]));
    }

    llvmFunc->setMetadata("kernel_arg_addr_space",
                          llvm::MDNode::get(vmCtx, addressQuals));
    llvmFunc->setMetadata("kernel_arg_access_qual",
                          llvm::MDNode::get(vmCtx, accessQuals));
    llvmFunc->setMetadata("kernel_arg_type",
                          llvm::MDNode::get(vmCtx, argTypeNames));
    llvmFunc->setMetadata("kernel_arg_base_type",
                          llvm::MDNode::get(vmCtx, argBaseTypeNames));
    llvmFunc->setMetadata("kernel_arg_type_qual",
                          llvm::MDNode::get(vmCtx, argTypeQuals));
    if (shouldEmitArgName)
      llvmFunc->setMetadata("kernel_arg_name",
                            llvm::MDNode::get(vmCtx, argNames));
  }

  static llvm::UWTableKind convertUWTableKind(cir::UWTableKind kind) {
    switch (kind) {
    case cir::UWTableKind::None:
      return llvm::UWTableKind::None;
    case cir::UWTableKind::Sync:
      return llvm::UWTableKind::Sync;
    case cir::UWTableKind::Async:
      return llvm::UWTableKind::Async;
    }
    llvm_unreachable("unknown UWTableKind");
  }
};

void registerCIRDialectTranslation(mlir::DialectRegistry &registry) {
  registry.insert<cir::CIRDialect, mlir::omp::OpenMPDialect>();
  cir::omp::registerOpenMPExtensions(registry);
  registry.addExtension(+[](mlir::MLIRContext *ctx, cir::CIRDialect *dialect) {
    dialect->addInterfaces<CIRDialectLLVMIRTranslationInterface>();
  });
  mlir::registerOpenMPDialectTranslation(registry);
}

} // namespace direct
} // namespace cir

namespace mlir {
void registerCIRDialectTranslation(mlir::MLIRContext &context) {
  mlir::DialectRegistry registry;
  cir::direct::registerCIRDialectTranslation(registry);
  context.appendDialectRegistry(registry);
}
} // namespace mlir
