//===- AMDGPU.cpp - TargetInfo for AMDGPU ---------------------------------===//
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
#include "mlir/Dialect/LLVMIR/LLVMAttrs.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "clang/Basic/AddressSpaces.h"
#include "clang/CIR/ABIArgInfo.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
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
// AMDGPU ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class AMDGPUABIInfo : public ABIInfo {
public:
  AMDGPUABIInfo(LowerTypes &lt) : ABIInfo(lt) {}

private:
  static const unsigned maxNumRegsForArgsRet = 16;

  unsigned numRegsForType(mlir::Type ty) const;

  // Coerce HIP scalar pointer arguments from generic pointers to global ones.
  mlir::Type coerceKernelArgumentType(mlir::Type ty, unsigned fromAS,
                                      unsigned toAS) const;

  ABIArgInfo classifyReturnType(mlir::Type ty) const;
  ABIArgInfo classifyArgumentType(mlir::Type ty, bool variadic,
                                  unsigned &numRegsLeft) const;
  ABIArgInfo classifyKernelArgumentType(mlir::Type ty) const;
  void computeInfo(LowerFunctionInfo &fi) const override;
};

class AMDGPUTargetLoweringInfo : public TargetLoweringInfo {
public:
  AMDGPUTargetLoweringInfo(LowerTypes &lt)
      : TargetLoweringInfo(std::make_unique<AMDGPUABIInfo>(lt)) {}

  std::string getLLVMSyncScope(cir::SyncScopeKind syncScope) const override {
    switch (syncScope) {
    case cir::SyncScopeKind::SingleThread:
      return "singlethread";
    case cir::SyncScopeKind::System:
      return "";
    case cir::SyncScopeKind::Device:
      return "agent";
    case cir::SyncScopeKind::Workgroup:
      return "workgroup";
    case cir::SyncScopeKind::Cluster:
      return "cluster";
    case cir::SyncScopeKind::Wavefront:
      return "wavefront";
    }
    llvm_unreachable("unknown sync scope kind");
  }

  // Taken from here: https://llvm.org/docs/AMDGPUUsage.html#address-spaces
  unsigned getTargetAddrSpaceFromCIRAddrSpace(
      cir::LangAddressSpace addrSpace) const override {
    switch (addrSpace) {
    case cir::LangAddressSpace::OffloadPrivate:
      return 5;
    case cir::LangAddressSpace::OffloadLocal:
      return 3;
    case cir::LangAddressSpace::OffloadGlobal:
      return 1;
    case cir::LangAddressSpace::OffloadConstant:
      return 4;
    case cir::LangAddressSpace::OffloadGeneric:
      return 0;
    default:
      cir_cconv_unreachable("Unknown CIR address space for this target");
    }
  }

  mlir::Type getOpaqueType(cir::OpaqueType type) const override {
    if (type.getTag() == cir::OpaqueType::getAMDGCNNamedBarrierTag())
      return mlir::LLVM::LLVMTargetExtType::get(
          type.getContext(), "amdgcn.named.barrier", /*typeParams=*/{},
          /*intParams=*/{0});
    return TargetLoweringInfo::getOpaqueType(type);
  }
};

} // namespace

// Estimate number of registers the type will use when passed in registers.
unsigned AMDGPUABIInfo::numRegsForType(mlir::Type ty) const {
  unsigned numRegs = 0;

  if (auto VT = mlir::dyn_cast<cir::VectorType>(ty)) {
    // Compute from the number of elements. The reported size is based on the
    // in-memory size, which includes padding for 3-vectors.
    uint64_t eltSize = getContext().getTypeSize(VT.getElementType());

    // 16-bit element vectors should be passed as packed.
    if (eltSize == 16)
      return (VT.getSize() + 1) / 2;

    unsigned eltNumRegs = (eltSize + 31) / 32;
    return eltNumRegs * VT.getSize();
  }

  if (auto RT = mlir::dyn_cast<RecordType>(ty)) {
    // Sum up the register usage of all fields.
    for (mlir::Type member : RT.getMembers())
      numRegs += numRegsForType(member);
    return numRegs;
  }

  return (getContext().getTypeSize(ty) + 31) / 32;
}

// Coerce HIP scalar pointer arguments from generic pointers to global ones.
mlir::Type AMDGPUABIInfo::coerceKernelArgumentType(mlir::Type ty,
                                                   unsigned fromAS,
                                                   unsigned toAS) const {
  if (auto ptrTy = mlir::dyn_cast<cir::PointerType>(ty)) {
    mlir::Attribute addrSpaceAttr = ptrTy.getAddrSpace();
    // Pointers with a language address space (e.g. offload_local,
    // offload_constant) already have an explicit address space and should not
    // be coerced. This coercion is only meant for generic/flat pointers.
    if (mlir::isa_and_present<cir::LangAddressSpaceAttr>(addrSpaceAttr))
      return ty;
    unsigned currentAS = 0;
    // Get the current address space.
    if (auto targetAS = mlir::dyn_cast_if_present<cir::TargetAddressSpaceAttr>(
            addrSpaceAttr))
      currentAS = targetAS.getValue();
    // If currentAS is same as the FromAS, coerce it to the ToAS.
    if (currentAS == fromAS) {
      auto ctx = ty.getContext();
      auto newAddrSpaceAttr = cir::TargetAddressSpaceAttr::get(ctx, toAS);
      return cir::PointerType::get(ptrTy.getPointee(), newAddrSpaceAttr);
    }
  }
  return ty;
}

ABIArgInfo AMDGPUABIInfo::classifyReturnType(mlir::Type ty) const {
  if (llvm::isa<VoidType>(ty))
    return ABIArgInfo::getIgnore();

  if (isAggregateTypeForABI(ty)) {
    // Records with non-trivial destructors/copy-constructors should not be
    // returned by value.
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

  // OG treats enums as their underlying type.
  // This has already been done for CIR.

  return isPromotableIntegerTypeForABI(ty) ? ABIArgInfo::getExtend(ty)
                                           : ABIArgInfo::getDirect();
}

ABIArgInfo AMDGPUABIInfo::classifyArgumentType(mlir::Type ty, bool variadic,
                                               unsigned &numRegsLeft) const {
  assert(numRegsLeft <= maxNumRegsForArgsRet && "register estimate underflow");

  ty = useFirstFieldIfTransparentUnion(ty);

  if (variadic)
    return ABIArgInfo::getDirect(nullptr, 0, nullptr, false, 0);

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

    // Structs with flexible array members fall back to default classification
    // (indirect passing), matching OGCG's DefaultABIInfo::classifyArgumentType.
    if (auto RT = mlir::dyn_cast<RecordType>(ty))
      if (auto ast = RT.getAst())
        if (const auto *RD = ast.getRawDecl())
          if (RD->hasFlexibleArrayMember())
            return getNaturalAlignIndirect(ty);

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

      // [2 x i32]
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
    unsigned align = getContext().getTypeAlign(ty) / 8; // bits → bytes
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

ABIArgInfo AMDGPUABIInfo::classifyKernelArgumentType(mlir::Type ty) const {
  ty = useFirstFieldIfTransparentUnion(ty);

  // Lower single-element structs to the element type, so they are
  // passed directly rather than via byref.
  if (mlir::Type seltTy = isSingleElementStruct(ty))
    ty = seltTy;

  // Aggregate types are passed via byref in constant address space for kernel
  // arguments, matching OGCG's IndirectAliased classification. The callee
  // defensively copies the data to a local variable.
  if (isAggregateTypeForABI(ty)) {
    unsigned align = getContext().getTypeAlign(ty) / 8; // bits → bytes
    unsigned addrSpace =
        getTarget().getTargetAddressSpace(clang::LangAS::opencl_constant);
    return ABIArgInfo::getIndirectAliased(align, addrSpace, /*Realign=*/false,
                                          /*Padding=*/{});
  }

  mlir::Type origTy = ty;
  mlir::Type coercedTy = origTy;

  // Only HIP coerces kernel pointer arguments from generic to global; plain
  // C/C++ amdgcn kernels keep generic pointers. This mirrors classic
  // AMDGPUABIInfo::classifyKernelArgumentType, which gates on
  // getContext().getLangOpts().HIP. LangOpts.HIP is propagated into the
  // reconstructed LangOptions via the cir.hip module attribute.
  bool isHIP = getContext().getLangOpts().HIP;

  // For HIP, coerce pointer arguments from generic to global
  if (isHIP) {
    unsigned genericAS =
        getTarget().getTargetAddressSpace(clang::LangAS::Default);
    unsigned globalAS =
        getTarget().getTargetAddressSpace(clang::LangAS::cuda_device);
    coercedTy = coerceKernelArgumentType(origTy, genericAS, globalAS);
  }

  // If we set CanBeFlattened to true, CodeGen will expand the struct to its
  // individual elements, which confuses the Clover OpenCL backend; therefore we
  // have to set it to false here. Other args of getDirect() are just defaults.
  return ABIArgInfo::getDirect(coercedTy, 0, nullptr, false);
}

void AMDGPUABIInfo::computeInfo(LowerFunctionInfo &fi) const {
  llvm::CallingConv::ID cc = fi.getCallingConvention();

  if (!getCXXABI().classifyReturnType(fi))
    fi.getReturnInfo() = classifyReturnType(fi.getReturnType());

  unsigned argumentIndex = 0;
  const unsigned numFixedArguments = fi.getNumRequiredArgs();

  unsigned numRegsLeft = maxNumRegsForArgsRet;
  for (auto &argument : fi.arguments()) {
    if (cc == llvm::CallingConv::AMDGPU_KERNEL) {
      argument.info = classifyKernelArgumentType(argument.type);
    } else {
      bool fixedArgument = argumentIndex++ < numFixedArguments;
      argument.info =
          classifyArgumentType(argument.type, !fixedArgument, numRegsLeft);
    }
  }
}

std::unique_ptr<TargetLoweringInfo>
createAMDGPUTargetLoweringInfo(LowerModule &lowerModule) {
  return std::make_unique<AMDGPUTargetLoweringInfo>(lowerModule.getTypes());
}

} // namespace cir
