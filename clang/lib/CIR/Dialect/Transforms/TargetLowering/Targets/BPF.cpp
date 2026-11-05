//===- BPF.cpp - TargetInfo for BPF ---------------------------------------===//
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
#include "clang/CIR/ABIArgInfo.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "llvm/Support/ErrorHandling.h"

using ABIArgInfo = cir::ABIArgInfo;

namespace cir {

//===----------------------------------------------------------------------===//
// BPF ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class BPFABIInfo : public ABIInfo {
public:
  BPFABIInfo(LowerTypes &LT) : ABIInfo(LT) {}

private:
  ABIArgInfo classifyReturnType(mlir::Type ty) const;
  ABIArgInfo classifyArgumentType(mlir::Type ty) const;

  void computeInfo(LowerFunctionInfo &FI) const override;
};

ABIArgInfo BPFABIInfo::classifyReturnType(mlir::Type ty) const {
  if (mlir::isa<VoidType>(ty))
    return ABIArgInfo::getIgnore();

  if (isAggregateTypeForABI(ty))
    return getNaturalAlignIndirect(ty);

  // Integers wider than 128 bits (e.g. _BitInt(200)) are returned indirectly.
  if (auto intTy = mlir::dyn_cast<IntType>(ty))
    if (intTy.getWidth() > 128)
      return getNaturalAlignIndirect(ty);

  return ABIArgInfo::getDirect();
}

ABIArgInfo BPFABIInfo::classifyArgumentType(mlir::Type ty) const {
  ty = useFirstFieldIfTransparentUnion(ty);

  if (isAggregateTypeForABI(ty)) {
    uint64_t Bits = getContext().getTypeSize(ty);
    if (Bits == 0)
      return ABIArgInfo::getIgnore();

    // If the aggregate needs 1 or 2 registers, coerce to integer type(s).
    if (Bits <= 128) {
      mlir::Type coerceTy;
      if (Bits <= 64) {
        coerceTy = IntType::get(LT.getMLIRContext(),
                                llvm::alignTo(Bits, 8), /*isSigned=*/false);
      } else {
        mlir::Type regTy =
            IntType::get(LT.getMLIRContext(), 64, /*isSigned=*/false);
        coerceTy = ArrayType::get(regTy, 2);
      }
      return ABIArgInfo::getDirect(coerceTy);
    }

    // Aggregates larger than 128 bits are passed by reference.
    return getNaturalAlignIndirect(ty);
  }

  // Integers wider than 128 bits (e.g. _BitInt(200)) are passed indirectly.
  if (auto intTy = mlir::dyn_cast<IntType>(ty))
    if (intTy.getWidth() > 128)
      return getNaturalAlignIndirect(ty);

  return (isPromotableIntegerTypeForABI(ty) ? ABIArgInfo::getExtend(ty)
                                            : ABIArgInfo::getDirect());
}

void BPFABIInfo::computeInfo(LowerFunctionInfo &FI) const {
  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());

  for (auto &&[count, argument] : llvm::enumerate(FI.arguments()))
    argument.info = count < FI.getNumRequiredArgs()
                        ? classifyArgumentType(argument.type)
                        : ABIArgInfo::getDirect();
}

class BPFTargetLoweringInfo : public TargetLoweringInfo {
public:
  BPFTargetLoweringInfo(LowerTypes &LT)
      : TargetLoweringInfo(std::make_unique<BPFABIInfo>(LT)) {}

  unsigned getTargetAddrSpaceFromCIRAddrSpace(
      cir::LangAddressSpace addrSpace) const override {
    // BPF uses a flat address space model; all address spaces map to 0.
    return 0;
  }
};

} // namespace

std::unique_ptr<TargetLoweringInfo>
createBPFTargetLoweringInfo(LowerModule &lowerModule) {
  return std::make_unique<BPFTargetLoweringInfo>(lowerModule.getTypes());
}

} // namespace cir
