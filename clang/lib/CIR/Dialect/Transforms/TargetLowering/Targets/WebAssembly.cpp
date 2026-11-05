//===- WebAssembly.cpp - TargetInfo for WebAssembly ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file mimics clang/lib/CodeGen/Targets/WebAssembly.cpp. The queries are
// adapted to operate on the CIR dialect, however.
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
// WebAssembly ABI Implementation
//
// This is a very simple ABI that relies a lot on the default classification
// logic (which the CIR framework inlines here, since there is no shared
// DefaultABIInfo class as there is in classic CodeGen).
//===----------------------------------------------------------------------===//

namespace {

class WebAssemblyABIInfo : public ABIInfo {
public:
  WebAssemblyABIInfo(LowerTypes &LT) : ABIInfo(LT) {}

private:
  ABIArgInfo classifyReturnType(mlir::Type ty) const;
  ABIArgInfo classifyArgumentType(mlir::Type ty) const;

  // The "default" classification for scalars and other aggregates, mirroring
  // OGCG's DefaultABIInfo which WebAssembly.cpp falls back to.
  ABIArgInfo defaultClassifyReturnType(mlir::Type ty) const;
  ABIArgInfo defaultClassifyArgumentType(mlir::Type ty) const;

  void computeInfo(LowerFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &Arg : FI.arguments())
      Arg.info = classifyArgumentType(Arg.type);
  }
};

ABIArgInfo
WebAssemblyABIInfo::defaultClassifyArgumentType(mlir::Type ty) const {
  ty = useFirstFieldIfTransparentUnion(ty);

  if (isAggregateTypeForABI(ty)) {
    // Records with non-trivial destructors/copy-constructors should not be
    // passed by value.
    if (CIRCXXABI::RecordArgABI RAA = getRecordArgABI(ty, getCXXABI()))
      return getNaturalAlignIndirect(ty, RAA == CIRCXXABI::RAA_DirectInMemory);

    return getNaturalAlignIndirect(ty);
  }

  // Integers wider than 128 bits (e.g. _BitInt(200)) are passed indirectly.
  if (auto intTy = mlir::dyn_cast<IntType>(ty))
    if (intTy.getWidth() > 128)
      return getNaturalAlignIndirect(ty);

  return (isPromotableIntegerTypeForABI(ty) ? ABIArgInfo::getExtend(ty)
                                            : ABIArgInfo::getDirect());
}

ABIArgInfo WebAssemblyABIInfo::defaultClassifyReturnType(mlir::Type ty) const {
  if (mlir::isa<VoidType>(ty))
    return ABIArgInfo::getIgnore();

  if (isAggregateTypeForABI(ty))
    return getNaturalAlignIndirect(ty);

  // Integers wider than 128 bits (e.g. _BitInt(200)) are returned indirectly.
  if (auto intTy = mlir::dyn_cast<IntType>(ty))
    if (intTy.getWidth() > 128)
      return getNaturalAlignIndirect(ty);

  return (isPromotableIntegerTypeForABI(ty) ? ABIArgInfo::getExtend(ty)
                                            : ABIArgInfo::getDirect());
}

/// Classify argument of given type \p ty.
ABIArgInfo WebAssemblyABIInfo::classifyArgumentType(mlir::Type ty) const {
  ty = useFirstFieldIfTransparentUnion(ty);

  if (isAggregateTypeForABI(ty)) {
    // Records with non-trivial destructors/copy-constructors should not be
    // passed by value.
    if (CIRCXXABI::RecordArgABI RAA = getRecordArgABI(ty, getCXXABI()))
      return getNaturalAlignIndirect(ty, RAA == CIRCXXABI::RAA_DirectInMemory);
    // Ignore empty structs/unions.
    if (isEmptyRecord(ty, /*allowArrays=*/true))
      return ABIArgInfo::getIgnore();
    // Lower single-element structs to just pass a regular value. TODO: We
    // could do reasonable-size multiple-element structs too, using getExpand(),
    // though watch out for things like bitfields. The experimental multivalue
    // ABI (which fully expands other aggregates) is not modeled here; the
    // default wasm target uses the MVP ABI.
    if (mlir::Type seltTy = isSingleElementStruct(ty))
      return ABIArgInfo::getDirect(seltTy);
  }

  // Otherwise just do the default thing.
  return defaultClassifyArgumentType(ty);
}

ABIArgInfo WebAssemblyABIInfo::classifyReturnType(mlir::Type ty) const {
  if (isAggregateTypeForABI(ty)) {
    // Records with non-trivial destructors/copy-constructors should not be
    // returned by value.
    if (!getRecordArgABI(ty, getCXXABI())) {
      // Ignore empty structs/unions.
      if (isEmptyRecord(ty, /*allowArrays=*/true))
        return ABIArgInfo::getIgnore();
      // Lower single-element structs to just return a regular value. TODO: We
      // could do reasonable-size multiple-element structs too, using
      // ABIArgInfo::getDirect(). The experimental multivalue ABI (which
      // returns other aggregates directly) is not modeled here.
      if (mlir::Type seltTy = isSingleElementStruct(ty))
        return ABIArgInfo::getDirect(seltTy);
    }
  }

  // Otherwise just do the default thing.
  return defaultClassifyReturnType(ty);
}

class WebAssemblyTargetLoweringInfo : public TargetLoweringInfo {
public:
  WebAssemblyTargetLoweringInfo(LowerTypes &LT)
      : TargetLoweringInfo(std::make_unique<WebAssemblyABIInfo>(LT)) {}

  unsigned getTargetAddrSpaceFromCIRAddrSpace(
      cir::LangAddressSpace addrSpace) const override {
    // WebAssembly uses a flat address space model; all address spaces map to 0.
    return 0;
  }
};

} // namespace

std::unique_ptr<TargetLoweringInfo>
createWebAssemblyTargetLoweringInfo(LowerModule &lowerModule) {
  // -target wasm32/wasm64-unknown-unknown selects the default MVP ABI; the
  // experimental multivalue ABI is opt-in and not wired up here.
  return std::make_unique<WebAssemblyTargetLoweringInfo>(
      lowerModule.getTypes());
}

} // namespace cir
