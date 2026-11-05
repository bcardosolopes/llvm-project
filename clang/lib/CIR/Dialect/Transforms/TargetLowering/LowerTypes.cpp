//===--- LowerTypes.cpp - Type translation to target-specific types -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file partially mimics clang/lib/CodeGen/CodeGenTypes.cpp. The queries
// are adapted to operate on the CIR dialect, however.
//
//===----------------------------------------------------------------------===//

#include "LowerTypes.h"
#include "CIRToCIRArgMapping.h"
#include "LowerModule.h"
#include "mlir/IR/Types.h"
#include "mlir/Support/LLVM.h"
#include "clang/CIR/ABIArgInfo.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/Support/ErrorHandling.h"

using namespace cir;

using ABIArgInfo = cir::ABIArgInfo;

unsigned LowerTypes::cirCallConvToLLVMCallConv(cir::CallingConv CC) {
  switch (CC) {
  case cir::CallingConv::C:
    return llvm::CallingConv::C;
  case cir::CallingConv::SpirKernel:
    return llvm::CallingConv::SPIR_KERNEL;
  case cir::CallingConv::SpirFunction:
    return llvm::CallingConv::SPIR_FUNC;
  case cir::CallingConv::OpenCLKernel:
    return llvm::CallingConv::SPIR_KERNEL;
  case cir::CallingConv::PTXKernel:
    return llvm::CallingConv::PTX_Kernel;
  case cir::CallingConv::AMDGPUKernel:
    return llvm::CallingConv::AMDGPU_KERNEL;
  case cir::CallingConv::X86StdCall:
    return llvm::CallingConv::X86_StdCall;
  case cir::CallingConv::X86FastCall:
    return llvm::CallingConv::X86_FastCall;
  case cir::CallingConv::X86RegCall:
    return llvm::CallingConv::X86_RegCall;
  case cir::CallingConv::X86ThisCall:
    return llvm::CallingConv::X86_ThisCall;
  case cir::CallingConv::X86VectorCall:
    return llvm::CallingConv::X86_VectorCall;
  case cir::CallingConv::X86_64SysV:
    return llvm::CallingConv::X86_64_SysV;
  case cir::CallingConv::Win64:
    return llvm::CallingConv::Win64;
  case cir::CallingConv::AAPCS:
    return llvm::CallingConv::ARM_AAPCS;
  case cir::CallingConv::AAPCS_VFP:
    return llvm::CallingConv::ARM_AAPCS_VFP;
  case cir::CallingConv::IntelOclBicc:
    return llvm::CallingConv::Intel_OCL_BI;
  case cir::CallingConv::AArch64VectorCall:
    return llvm::CallingConv::AArch64_VectorCall;
  case cir::CallingConv::AArch64SVEPCS:
    return llvm::CallingConv::AArch64_SVE_VectorCall;
  case cir::CallingConv::PreserveMost:
    return llvm::CallingConv::PreserveMost;
  case cir::CallingConv::PreserveAll:
    return llvm::CallingConv::PreserveAll;
  case cir::CallingConv::PreserveNone:
    return llvm::CallingConv::PreserveNone;
  case cir::CallingConv::Swift:
    return llvm::CallingConv::Swift;
  case cir::CallingConv::SwiftAsync:
    return llvm::CallingConv::SwiftTail;
  case cir::CallingConv::X86Intr:
    return llvm::CallingConv::X86_INTR;
  case cir::CallingConv::CXXFastTLS:
    return llvm::CallingConv::CXX_FAST_TLS;
  }
  cir_cconv_unreachable("Unknown calling convention");
}

LowerTypes::LowerTypes(LowerModule &LM)
    : LM(LM), context(LM.getContext()), Target(LM.getTarget()),
      CXXABI(LM.getCXXABI()),
      TheABIInfo(LM.getTargetLoweringInfo().getABIInfo()),
      mlirContext(LM.getMLIRContext()), DL(LM.getModule()) {}

/// Return the ABI-specific function type for a CIR function type.
FuncType LowerTypes::getFunctionType(const LowerFunctionInfo &FI) {

  mlir::Type resultType = {};
  const cir::ABIArgInfo &retAI = FI.getReturnInfo();
  switch (retAI.getKind()) {
  case ABIArgInfo::Extend:
  case ABIArgInfo::Direct:
    resultType = retAI.getCoerceToType();
    break;
  case cir::ABIArgInfo::Ignore:
  case cir::ABIArgInfo::Indirect:
    resultType = VoidType::get(getMLIRContext());
    break;
  case cir::ABIArgInfo::CoerceAndExpand:
    resultType = retAI.getUnpaddedCoerceAndExpandType();
    break;
  default:
    cir_cconv_unreachable("Missing ABIArgInfo::Kind");
  }

  CIRToCIRArgMapping IRFunctionArgs(getContext(), FI, true);
  llvm::SmallVector<mlir::Type, 8> ArgTypes(IRFunctionArgs.totalIRArgs());

  // Compute the alloca address space from the data layout, matching OGCG's
  // use of getAllocaAddrSpace() for sret and indirect pointer types.
  mlir::ptr::MemorySpaceAttrInterface allocaAddrSpaceAttr;
  if (auto allocaAS = mlir::cast_if_present<mlir::IntegerAttr>(
          DL.layout.getAllocaMemorySpace())) {
    unsigned allocaAddrSpace =
        static_cast<unsigned>(allocaAS.getValue().getZExtValue());
    if (allocaAddrSpace)
      allocaAddrSpaceAttr =
          cir::TargetAddressSpaceAttr::get(mlirContext, allocaAddrSpace);
  }

  // Add type for sret argument.
  if (IRFunctionArgs.hasSRetArg()) {
    mlir::Type ret = FI.getReturnType();
    ArgTypes[IRFunctionArgs.getSRetArgNo()] =
        cir::PointerType::get(ret, allocaAddrSpaceAttr);
  }

  // Add in all of the required arguments.
  unsigned ArgNo = 0;
  LowerFunctionInfo::const_arg_iterator it = FI.arg_begin(),
                                        ie = it + FI.getNumRequiredArgs();
  for (; it != ie; ++it, ++ArgNo) {
    const ABIArgInfo &ArgInfo = it->info;

    unsigned FirstIRArg, NumIRArgs;
    std::tie(FirstIRArg, NumIRArgs) = IRFunctionArgs.getIRArgs(ArgNo);

    if (IRFunctionArgs.hasPaddingArg(ArgNo))
      ArgTypes[IRFunctionArgs.getPaddingArgNo(ArgNo)] =
          ArgInfo.getPaddingType();

    switch (ArgInfo.getKind()) {
    case ABIArgInfo::Extend:
    case ABIArgInfo::Direct: {
      // Fast-isel and the optimizer generally like scalar values better than
      // FCAs, so we flatten them if this is safe to do for this argument.
      mlir::Type argType = ArgInfo.getCoerceToType();
      RecordType st = mlir::dyn_cast<RecordType>(argType);
      if (st && ArgInfo.isDirect() && ArgInfo.getCanBeFlattened()) {
        cir_cconv_assert(NumIRArgs == st.getNumElements());
        for (unsigned i = 0, e = st.getNumElements(); i != e; ++i)
          ArgTypes[FirstIRArg + i] = st.getMembers()[i];
      } else {
        cir_cconv_assert(NumIRArgs == 1);
        ArgTypes[FirstIRArg] = argType;
      }
      break;
    }
    case ABIArgInfo::Indirect: {
      // Indirect arguments are on the stack, which is the alloca addr space.
      mlir::Type argType = (FI.arg_begin() + ArgNo)->type;
      ArgTypes[FirstIRArg] =
          cir::PointerType::get(argType, allocaAddrSpaceAttr);
      break;
    }
    case ABIArgInfo::IndirectAliased: {
      mlir::Type argType = (FI.arg_begin() + ArgNo)->type;
      unsigned addrSpace = ArgInfo.getIndirectAddrSpace();
      mlir::ptr::MemorySpaceAttrInterface addrSpaceAttr;
      if (addrSpace)
        addrSpaceAttr = cir::TargetAddressSpaceAttr::get(
            getMLIRContext(), addrSpace);
      ArgTypes[FirstIRArg] =
          cir::PointerType::get(argType, addrSpaceAttr);
      break;
    }
    case ABIArgInfo::Ignore:
      // Ignored arguments have no corresponding IR argument.
      cir_cconv_assert(NumIRArgs == 0);
      break;
    case ABIArgInfo::CoerceAndExpand: {
      auto types = ArgInfo.getCoerceAndExpandTypeSequence();
      unsigned idx = FirstIRArg;
      for (auto ty : types)
        ArgTypes[idx++] = ty;
      assert(idx == FirstIRArg + NumIRArgs);
      break;
    }
    default:
      cir_cconv_unreachable("Missing ABIArgInfo::Kind");
    }
  }

  return FuncType::get(ArgTypes, resultType, FI.isVariadic());
}




/// Convert a CIR type to its ABI-specific default form.
mlir::Type LowerTypes::convertType(mlir::Type t) {
  /// NOTE(cir): It the original codegen this method is used to get the default
  /// LLVM IR representation for a given AST type. When a the ABI-specific
  /// function info sets a nullptr for a return or argument type, the default
  /// type given by this method is used. In CIR's case, its types are already
  /// supposed to be ABI-specific, so this method is not really useful here.
  /// It's kept here for codegen parity's sake.

  // Certain CIR types are already ABI-specific, so we just return them.
  if (mlir::isa<BoolType, IntType, SingleType, DoubleType, VoidType,
                PointerType, ComplexType, VectorType, RecordType, OpaqueType,
                FP16Type, BF16Type, FP80Type, FP128Type, LongDoubleType,
                ArrayType, FuncType, MethodType, DataMemberType>(t)) {
    return t;
  }

  // Other types may reach here in edge cases (e.g. exception handling).
  // Return them as-is since they don't need ABI-specific conversion.
  return t;
}
