//===- SPIR.cpp - TargetInfo for SPIR and SPIR-V --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "LowerFunctionInfo.h"
#include "LowerModule.h"
#include "LowerTypes.h"
#include "TargetInfo.h"
#include "TargetLoweringInfo.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "clang/Basic/AddressSpaces.h"
#include "clang/CIR/ABIArgInfo.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"

using ABIArgInfo = cir::ABIArgInfo;
using MissingFeature = cir::MissingFeatures;

namespace cir {

//===----------------------------------------------------------------------===//
// SPIR-V ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class SPIRVABIInfo : public ABIInfo {
public:
  SPIRVABIInfo(LowerTypes &LT) : ABIInfo(LT) {}

private:
  ABIArgInfo classifyReturnType(mlir::Type ty) const;
  ABIArgInfo classifyArgumentType(mlir::Type ty) const;

  void computeInfo(LowerFunctionInfo &FI) const override;
};

ABIArgInfo SPIRVABIInfo::classifyReturnType(mlir::Type ty) const {
  if (mlir::isa<VoidType>(ty))
    return ABIArgInfo::getIgnore();

  if (isAggregateTypeForABI(ty))
    return getNaturalAlignIndirect(ty);

  return (isPromotableIntegerTypeForABI(ty) ? ABIArgInfo::getExtend(ty)
                                            : ABIArgInfo::getDirect());
}

ABIArgInfo SPIRVABIInfo::classifyArgumentType(mlir::Type ty) const {
  if (isAggregateTypeForABI(ty))
    return getNaturalAlignIndirect(ty);

  return (isPromotableIntegerTypeForABI(ty) ? ABIArgInfo::getExtend(ty)
                                            : ABIArgInfo::getDirect());
}

void SPIRVABIInfo::computeInfo(LowerFunctionInfo &FI) const {
  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());

  for (auto &&[count, argument] : llvm::enumerate(FI.arguments()))
    argument.info = count < FI.getNumRequiredArgs()
                        ? classifyArgumentType(argument.type)
                        : ABIArgInfo::getDirect();
}

//===----------------------------------------------------------------------===//
// AMDGCN SPIR-V ABI Implementation
//===----------------------------------------------------------------------===//

class AMDGCNSPIRVABIInfo : public ABIInfo {
  static constexpr unsigned maxNumRegsForArgsRet = 16;
  mutable unsigned numRegsLeft = 0;

  unsigned numRegsForType(mlir::Type ty) const;

  // Coerce HIP scalar pointer arguments from generic pointers to global ones.
  mlir::Type coerceKernelArgumentType(mlir::Type ty, unsigned fromAS,
                                      unsigned toAS) const;

  ABIArgInfo classifyReturnType(mlir::Type ty) const;
  ABIArgInfo classifyKernelArgumentType(mlir::Type ty) const;
  ABIArgInfo classifyArgumentType(mlir::Type ty) const;

  void computeInfo(LowerFunctionInfo &FI) const override;

public:
  AMDGCNSPIRVABIInfo(LowerTypes &LT) : ABIInfo(LT) {}
};

unsigned AMDGCNSPIRVABIInfo::numRegsForType(mlir::Type ty) const {
  unsigned numRegs = 0;

  if (auto VT = mlir::dyn_cast<cir::VectorType>(ty)) {
    uint64_t eltSize = getContext().getTypeSize(VT.getElementType());

    // 16-bit element vectors should be passed as packed.
    if (eltSize == 16)
      return (VT.getSize() + 1) / 2;

    unsigned eltNumRegs = (eltSize + 31) / 32;
    return eltNumRegs * VT.getSize();
  }

  if (auto RT = mlir::dyn_cast<RecordType>(ty)) {
    for (mlir::Type member : RT.getMembers())
      numRegs += numRegsForType(member);
    return numRegs;
  }

  return (getContext().getTypeSize(ty) + 31) / 32;
}

mlir::Type AMDGCNSPIRVABIInfo::coerceKernelArgumentType(mlir::Type ty,
                                                         unsigned fromAS,
                                                         unsigned toAS) const {
  if (auto ptrTy = mlir::dyn_cast<cir::PointerType>(ty)) {
    mlir::Attribute addrSpaceAttr = ptrTy.getAddrSpace();
    if (mlir::isa_and_present<cir::LangAddressSpaceAttr>(addrSpaceAttr))
      return ty;
    unsigned currentAS = 0;
    if (auto targetAS = mlir::dyn_cast_if_present<cir::TargetAddressSpaceAttr>(
            addrSpaceAttr))
      currentAS = targetAS.getValue();
    if (currentAS == fromAS) {
      auto ctx = ty.getContext();
      auto newAddrSpaceAttr = cir::TargetAddressSpaceAttr::get(ctx, toAS);
      return cir::PointerType::get(ptrTy.getPointee(), newAddrSpaceAttr);
    }
  }
  return ty;
}

ABIArgInfo AMDGCNSPIRVABIInfo::classifyReturnType(mlir::Type ty) const {
  if (llvm::isa<VoidType>(ty))
    return ABIArgInfo::getIgnore();

  if (isAggregateTypeForABI(ty)) {
    if (!getRecordArgABI(ty, getCXXABI())) {
      // Ignore empty structs/unions.
      if (isEmptyRecord(ty, true))
        return ABIArgInfo::getIgnore();

      // Lower single-element structs to just return a regular value.
      if (mlir::Type seltTy = isSingleElementStruct(ty))
        return ABIArgInfo::getDirect(seltTy);

      // Structs with flexible array members cannot be coerced to integers.
      if (auto RT = mlir::dyn_cast<RecordType>(ty))
        if (auto ast = RT.getAst())
          if (const auto *RD = ast.getRawDecl())
            if (RD->hasFlexibleArrayMember())
              return getNaturalAlignIndirect(ty);

      // Pack aggregates into single VGPR or pair.
      uint64_t size = getContext().getTypeSize(ty);
      if (size <= 16)
        return ABIArgInfo::getDirect(
            cir::IntType::get(ty.getContext(), 16, false));

      if (size <= 32)
        return ABIArgInfo::getDirect(
            cir::IntType::get(ty.getContext(), 32, false));

      if (size <= 64) {
        auto i32Ty = cir::IntType::get(ty.getContext(), 32, false);
        return ABIArgInfo::getDirect(cir::ArrayType::get(i32Ty, 2));
      }

      if (numRegsForType(ty) <= maxNumRegsForArgsRet)
        return ABIArgInfo::getDirect();
    }

    // Too large or non-trivial — indirect return.
    return getNaturalAlignIndirect(ty);
  }

  return isPromotableIntegerTypeForABI(ty) ? ABIArgInfo::getExtend(ty)
                                           : ABIArgInfo::getDirect();
}

ABIArgInfo
AMDGCNSPIRVABIInfo::classifyKernelArgumentType(mlir::Type ty) const {
  ty = useFirstFieldIfTransparentUnion(ty);

  // Lower single-element structs to the element type.
  if (mlir::Type seltTy = isSingleElementStruct(ty))
    ty = seltTy;

  mlir::Type origTy = ty;
  mlir::Type coercedTy = origTy;

  // For HIP, coerce pointer arguments from generic to global.
  if (getContext().getLangOpts().HIP) {
    unsigned genericAS =
        getTarget().getTargetAddressSpace(clang::LangAS::Default);
    unsigned globalAS =
        getTarget().getTargetAddressSpace(clang::LangAS::opencl_global);
    coercedTy = coerceKernelArgumentType(origTy, genericAS, globalAS);
  }

  // Aggregate types use IndirectAliased in constant address space.
  if (coercedTy == origTy && isAggregateTypeForABI(ty)) {
    unsigned align = getContext().getTypeAlign(ty) / 8;
    unsigned addrSpace =
        getTarget().getTargetAddressSpace(clang::LangAS::opencl_constant);
    return ABIArgInfo::getIndirectAliased(align, addrSpace, /*Realign=*/false,
                                          /*Padding=*/{});
  }

  return ABIArgInfo::getDirect(coercedTy, 0, nullptr, false);
}

ABIArgInfo AMDGCNSPIRVABIInfo::classifyArgumentType(mlir::Type ty) const {
  assert(numRegsLeft <= maxNumRegsForArgsRet && "register estimate underflow");

  ty = useFirstFieldIfTransparentUnion(ty);

  if (isAggregateTypeForABI(ty)) {
    // Records with non-trivial destructors/copy-constructors should not be
    // passed by value.
    if (auto RAA = getRecordArgABI(ty, getCXXABI()))
      return getNaturalAlignIndirect(ty,
                                     RAA == CIRCXXABI::RAA_DirectInMemory);

    // Ignore empty structs/unions.
    if (isEmptyRecord(ty, true))
      return ABIArgInfo::getIgnore();

    // Lower single-element structs to just pass a regular value.
    if (mlir::Type seltTy = isSingleElementStruct(ty))
      return ABIArgInfo::getDirect(seltTy);

    // Structs with flexible array members get default classification.
    if (auto RT = mlir::dyn_cast<RecordType>(ty))
      if (auto ast = RT.getAst())
        if (const auto *RD = ast.getRawDecl())
          if (RD->hasFlexibleArrayMember())
            return ABIArgInfo::getDirect();

    // Pack aggregates <= 8 bytes into single VGPR or pair.
    uint64_t size = getContext().getTypeSize(ty);
    if (size <= 64) {
      unsigned numRegs = (size + 31) / 32;
      numRegsLeft -= std::min(numRegsLeft, numRegs);

      if (size <= 16)
        return ABIArgInfo::getDirect(
            cir::IntType::get(ty.getContext(), 16, false));

      if (size <= 32)
        return ABIArgInfo::getDirect(
            cir::IntType::get(ty.getContext(), 32, false));

      auto i32Ty = cir::IntType::get(ty.getContext(), 32, false);
      return ABIArgInfo::getDirect(cir::ArrayType::get(i32Ty, 2));
    }

    if (numRegsLeft > 0) {
      unsigned numRegs = numRegsForType(ty);
      if (numRegsLeft >= numRegs) {
        numRegsLeft -= numRegs;
        return ABIArgInfo::getDirect();
      }
    }

    // Use pass-by-reference for struct arguments in function ABI.
    unsigned align = getContext().getTypeAlign(ty) / 8;
    unsigned addrSpace =
        getTarget().getTargetAddressSpace(clang::LangAS::opencl_private);
    return ABIArgInfo::getIndirectAliased(align, addrSpace);
  }

  ABIArgInfo argInfo =
      (isPromotableIntegerTypeForABI(ty) ? ABIArgInfo::getExtend(ty)
                                         : ABIArgInfo::getDirect());

  // Track register usage.
  if (!argInfo.isIndirect()) {
    unsigned numRegs = numRegsForType(ty);
    numRegsLeft -= std::min(numRegs, numRegsLeft);
  }

  return argInfo;
}

void AMDGCNSPIRVABIInfo::computeInfo(LowerFunctionInfo &FI) const {
  llvm::CallingConv::ID cc = FI.getCallingConvention();

  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());

  numRegsLeft = maxNumRegsForArgsRet;
  for (auto &argument : FI.arguments()) {
    if (cc == llvm::CallingConv::SPIR_KERNEL)
      argument.info = classifyKernelArgumentType(argument.type);
    else
      argument.info = classifyArgumentType(argument.type);
  }
}

class SPIRVTargetLoweringInfo : public TargetLoweringInfo {
public:
  SPIRVTargetLoweringInfo(std::unique_ptr<ABIInfo> abiInfo)
      : TargetLoweringInfo(std::move(abiInfo)) {}

  unsigned getTargetAddrSpaceFromCIRAddrSpace(
      cir::LangAddressSpace addrSpace) const override {
    switch (addrSpace) {
    case cir::LangAddressSpace::OffloadPrivate:
      return 0;
    case cir::LangAddressSpace::OffloadLocal:
      return 3;
    case cir::LangAddressSpace::OffloadGlobal:
      return 1;
    case cir::LangAddressSpace::OffloadConstant:
      return 2;
    case cir::LangAddressSpace::OffloadGeneric:
      return 4;
    default:
      cir_cconv_unreachable("Unknown CIR address space for this target");
    }
  }

  mlir::Type getOpaqueType(cir::OpaqueType type) const override {
    if (type.getTag() != cir::OpaqueType::getOpenCLEventTag())
      llvm_unreachable("NYI");

    return mlir::LLVM::LLVMTargetExtType::get(type.getContext(),
                                              /*extTypeName=*/"spirv.Event",
                                              /*typeParams=*/{},
                                              /*intParams=*/{});
  }
};

} // namespace

std::unique_ptr<TargetLoweringInfo>
createSPIRVTargetLoweringInfo(LowerModule &lowerModule) {
  LowerTypes &LT = lowerModule.getTypes();
  const llvm::Triple &Triple = lowerModule.getTarget().getTriple();

  std::unique_ptr<ABIInfo> abiInfo;
  if (Triple.getVendor() == llvm::Triple::AMD)
    abiInfo = std::make_unique<AMDGCNSPIRVABIInfo>(LT);
  else
    abiInfo = std::make_unique<SPIRVABIInfo>(LT);

  return std::make_unique<SPIRVTargetLoweringInfo>(std::move(abiInfo));
}

} // namespace cir
