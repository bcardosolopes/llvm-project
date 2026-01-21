//===- SPIR.cpp - TargetInfo for SPIR and SPIR-V --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "LowerFunctionInfo.h"
#include "LowerTypes.h"
#include "TargetInfo.h"
#include "TargetLoweringInfo.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "clang/CIR/ABIArgInfo.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"
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

class SPIRVTargetLoweringInfo : public TargetLoweringInfo {
public:
  SPIRVTargetLoweringInfo(LowerTypes &LT)
      : TargetLoweringInfo(std::make_unique<SPIRVABIInfo>(LT)) {}

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
  return std::make_unique<SPIRVTargetLoweringInfo>(lowerModule.getTypes());
}

} // namespace cir
