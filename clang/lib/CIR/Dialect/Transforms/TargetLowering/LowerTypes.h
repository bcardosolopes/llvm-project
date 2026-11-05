//===--- LowerTypes.cpp - Type lowering for CIR dialect -------------------===//
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

#ifndef LLVM_CLANG_LIB_CIR_DIALECT_TRANSFORMS_TARGETLOWERING_LOWERTYPES_H
#define LLVM_CLANG_LIB_CIR_DIALECT_TRANSFORMS_TARGETLOWERING_LOWERTYPES_H

#include "ABIInfo.h"
#include "CIRCXXABI.h"
#include "CIRLowerContext.h"
#include "LowerCall.h"
#include "mlir/IR/MLIRContext.h"
#include "clang/Basic/Specifiers.h"
#include "clang/CIR/Dialect/IR/CIRDataLayout.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/FnInfoOpts.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <array>
#include <memory>

namespace cir {

// Forward declarations.
class LowerModule;

/// This class organizes lowering to ABI-specific types in CIR.
class LowerTypes {
  // FIXME(cir): This abstraction could likely be replaced by a MLIR interface
  // or direct queries to CIR types. It here mostly for code parity.

private:
  struct FunctionInfoCacheEntry {
    std::unique_ptr<LowerFunctionInfo> FI;
    mlir::Type resultType;
    LowerFunctionInfoArgInfo::TypeProvenance resultProvenance =
        LowerFunctionInfoArgInfo::TypeProvenance::None;
    FnInfoOpts opts = FnInfoOpts::None;
    llvm::SmallVector<mlir::Type, 8> argTypes;
    llvm::SmallVector<LowerFunctionInfoArgInfo::TypeProvenance, 8>
        argProvenances;
    bool allowsOptionalArgs = false;
    unsigned numRequiredArgs = 0;
    cir::CallingConv callingConv = cir::CallingConv::C;
    bool hasRegParm = false;
    unsigned regParm = 0;
  };

  LowerModule &LM;
  CIRLowerContext &context;
  const clang::TargetInfo &Target;
  CIRCXXABI &CXXABI;

  // This should not be moved earlier, since its initialization depends on some
  // of the previous reference members being already initialized
  const ABIInfo &TheABIInfo;

  // Used to build types and other MLIR operations.
  mlir::MLIRContext *mlirContext;

  cir::CIRDataLayout DL;
  llvm::DenseMap<uint64_t, llvm::SmallVector<FunctionInfoCacheEntry, 1>>
      functionInfos;
  // Function infos arranged with caching disabled. Those cannot go in
  // functionInfos -- the caller mutates them afterwards, so they must not be
  // shared with a later matching signature -- but arrangeLLVMFunctionInfo
  // hands back a reference, so they still have to outlive the call. Own them
  // here instead.
  llvm::SmallVector<std::unique_ptr<LowerFunctionInfo>, 4>
      uncachedFunctionInfos;
  // Reusable scratch storage for slow-path call arrangement when call operand
  // types don't match the callee prototype. Reusing this avoids per-call heap
  // churn in compile-time hot paths.
  llvm::SmallVector<mlir::Type, 8> callArgTypesScratch;
  llvm::SmallVector<LowerFunctionInfoArgInfo::TypeProvenance, 8>
      callArgProvenancesScratch;
  // Tiny cache for slow-path call arrangement. Repeated no-proto / variadic
  // mismatch shapes often cycle among a small number of signatures in hot
  // call-conv lowering workloads; this avoids repeated rehash/relookup in
  // arrangeLLVMFunctionInfo for immediate and near-immediate repeats.
  struct SlowPathCallSignature {
    bool valid = false;
    mlir::Type resultType;
    LowerFunctionInfoArgInfo::TypeProvenance resultProvenance =
        LowerFunctionInfoArgInfo::TypeProvenance::None;
    FnInfoOpts opts = FnInfoOpts::None;
    bool allowsOptionalArgs = false;
    unsigned numRequiredArgs = 0;
    cir::CallingConv callingConv = cir::CallingConv::C;
    bool hasRegParm = false;
    unsigned regParm = 0;
    llvm::SmallVector<mlir::Type, 8> argTypes;
    llvm::SmallVector<LowerFunctionInfoArgInfo::TypeProvenance, 8>
        argProvenances;
    const LowerFunctionInfo *FI = nullptr;
  };
  static constexpr unsigned SlowPathCallSignatureCacheSize = 4;
  std::array<SlowPathCallSignature, SlowPathCallSignatureCacheSize>
      lastSlowPathCallSignatures;
  unsigned nextSlowPathCallSignatureSlot = 0;
  unsigned preferredSlowPathCallSignatureSlot = 0;

  const ABIInfo &getABIInfo() const { return TheABIInfo; }

public:
  LowerTypes(LowerModule &LM);
  ~LowerTypes() = default;

  const cir::CIRDataLayout &getDataLayout() const { return DL; }
  LowerModule &getLM() const { return LM; }
  CIRCXXABI &getCXXABI() const { return CXXABI; }
  CIRLowerContext &getContext() { return context; }
  const clang::TargetInfo &getTarget() const { return Target; }
  mlir::MLIRContext *getMLIRContext() { return mlirContext; }
  llvm::SmallVector<mlir::Type, 8> &getCallArgTypesScratch() {
    return callArgTypesScratch;
  }
  llvm::SmallVector<LowerFunctionInfoArgInfo::TypeProvenance, 8> &
  getCallArgProvenancesScratch() {
    return callArgProvenancesScratch;
  }
  std::array<SlowPathCallSignature, SlowPathCallSignatureCacheSize> &
  getLastSlowPathCallSignatures() {
    return lastSlowPathCallSignatures;
  }
  unsigned getSlowPathCallSignatureCacheSize() const {
    return SlowPathCallSignatureCacheSize;
  }
  unsigned getNextSlowPathCallSignatureSlot() const {
    return nextSlowPathCallSignatureSlot;
  }
  unsigned getPreferredSlowPathCallSignatureSlot() const {
    return preferredSlowPathCallSignatureSlot;
  }
  void setPreferredSlowPathCallSignatureSlot(unsigned slot) {
    preferredSlowPathCallSignatureSlot = slot;
  }
  void advanceSlowPathCallSignatureSlot() {
    nextSlowPathCallSignatureSlot =
        (nextSlowPathCallSignatureSlot + 1) % SlowPathCallSignatureCacheSize;
  }

  /// Convert CIR calling convention to LLVM calling convention.
  unsigned cirCallConvToLLVMCallConv(cir::CallingConv CC);

  /// Free functions are functions that are compatible with an ordinary
  /// C function pointer type.
  /// FIXME(cir): Does the "free function" concept makes sense here?
  const LowerFunctionInfo &arrangeFunctionDeclaration(FuncOp fnOp);
  const LowerFunctionInfo &
  arrangeFreeFunctionCall(mlir::ValueRange args, const FuncType fnType,
                          bool chainCall, bool instanceMethod = false,
                          LowerFunctionInfoArgInfo::TypeProvenance
                              resultProvenance =
                                  LowerFunctionInfoArgInfo::TypeProvenance::None,
                          llvm::ArrayRef<LowerFunctionInfoArgInfo::TypeProvenance>
                              argProvenances = {},
                          cir::CallingConv callingConv = cir::CallingConv::C,
                          bool allowCache = true, bool hasRegParm = false,
                          unsigned regParm = 0);
  const LowerFunctionInfo &
  arrangeFreeFunctionType(FuncType FTy,
                          LowerFunctionInfoArgInfo::TypeProvenance
                              resultProvenance =
                                  LowerFunctionInfoArgInfo::TypeProvenance::None,
                          llvm::ArrayRef<LowerFunctionInfoArgInfo::TypeProvenance>
                              argProvenances = {},
                          cir::CallingConv callingConv = cir::CallingConv::C);

  const LowerFunctionInfo &arrangeGlobalDeclaration(FuncOp fnOp);

  /// Arrange the argument and result information for an abstract value
  /// of a given function type.  This is the method which all of the
  /// above functions ultimately defer to.
  ///
  /// \param resultType - ABI-agnostic CIR result type.
  /// \param opts - Options to control the arrangement.
  /// \param argTypes - ABI-agnostic CIR argument types.
  /// \param required - Information about required/optional arguments.
  const LowerFunctionInfo &
  arrangeLLVMFunctionInfo(mlir::Type resultType, cir::FnInfoOpts opts,
                          llvm::ArrayRef<mlir::Type> argTypes,
                          LowerFunctionInfoArgInfo::TypeProvenance
                              resultProvenance,
                          llvm::ArrayRef<LowerFunctionInfoArgInfo::TypeProvenance>
                              argProvenances,
                          RequiredArgs required,
                          bool allowCache = false,
                          cir::CallingConv callingConv = cir::CallingConv::C,
                          bool hasRegParm = false, unsigned regParm = 0);

  /// Return the ABI-specific function type for a CIR function type.
  FuncType getFunctionType(const LowerFunctionInfo &FI);

  /// Convert a CIR type to its ABI-specific default form.
  mlir::Type convertType(mlir::Type T);
};

} // namespace cir

#endif // LLVM_CLANG_LIB_CIR_DIALECT_TRANSFORMS_TARGETLOWERING_LOWERTYPES_H
