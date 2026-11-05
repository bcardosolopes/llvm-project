//===--- LowerModule.h - Abstracts CIR's module lowering --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file partially mimics clang/lib/CodeGen/CodeGenModule.h. The queries are
// adapted to operate on the CIR dialect, however.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_LIB_CIR_DIALECT_TRANSFORMS_TARGETLOWERING_LOWERMODULE_H
#define CLANG_LIB_CIR_DIALECT_TRANSFORMS_TARGETLOWERING_LOWERMODULE_H

#include "CIRLowerContext.h"
#include "LowerTypes.h"
#include "TargetLoweringInfo.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CIR/Dialect/IR/CIRDataLayout.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/MissingFeatures.h"
#include <memory>
#include <optional>

namespace cir {

enum class LowerModuleRequirement {
  FullTargetLowering,
  CXXABIOnly,
};

/// What C++ decides about passing a record by value, which the calling
/// convention depends on and the record's layout does not show.
struct RecordABIFacts {
  /// Not passable in registers (non-trivial copy/move/destructor): arguments
  /// go by invisible reference and returns through sret.
  bool passIndirectly = false;
  /// The caller destroys an argument after the call, so its memory is not
  /// dead on return.
  bool nonTrivialDestructor = false;
  /// ...unless the callee destroys it ([[trivial_abi]]).
  bool destroyedInCallee = false;
};

/// Bits of a cir.record_abi entry.
enum RecordABIFactBits : unsigned {
  RecordPassedIndirectly = 1u << 0,
  RecordNonTrivialDestructor = 1u << 1,
  RecordDestroyedInCallee = 1u << 2,
};

class LowerModule {
  CIRLowerContext context;
  mlir::ModuleOp module;
  const std::unique_ptr<clang::TargetInfo> target;
  mutable std::unique_ptr<TargetLoweringInfo> targetLoweringInfo;
  std::unique_ptr<CIRCXXABI> abi;

  LowerTypes types;

  mlir::PatternRewriter &rewriter;

public:
  LowerModule(clang::LangOptions langOpts, clang::CodeGenOptions codeGenOpts,
              mlir::ModuleOp &module, std::unique_ptr<clang::TargetInfo> target,
              mlir::PatternRewriter &rewriter);
  ~LowerModule() = default;

  // Trivial getters.
  LowerTypes &getTypes() { return types; }
  CIRLowerContext &getContext() { return context; }
  CIRCXXABI &getCXXABI() const { return *abi; }
  const clang::TargetInfo &getTarget() const { return *target; }
  mlir::MLIRContext *getMLIRContext() { return module.getContext(); }
  mlir::ModuleOp &getModule() { return module; }

  /// The C++ facts about passing `rec`: from its AST while it has one, and from
  /// the module's cir.record_abi table once serialization has taken the AST
  /// away -- as it does for the CIR a fat object carries, which cir-link
  /// lowers. Records in neither are trivially passable, as a C record is.
  RecordABIFacts getRecordABIFacts(RecordType rec);

  const cir::CIRDataLayout &getDataLayout() const {
    return types.getDataLayout();
  }

  const TargetLoweringInfo &getTargetLoweringInfo();

  // FIXME(cir): This would be in ASTContext, not CodeGenModule.
  const clang::TargetInfo &getTargetInfo() const { return *target; }

  // FIXME(cir): This would be in ASTContext, not CodeGenModule.
  clang::TargetCXXABI::Kind getCXXABIKind() const {
    auto kind = getTarget().getCXXABI().getKind();
    return context.getLangOpts().CXXABI.value_or(kind);
  }

  bool canUseWindowsSwiftCXXABICompat();
  bool functionCanUseWindowsSwiftCXXABICompat(FuncOp fn);

  void constructAttributeList(llvm::StringRef Name, const LowerFunctionInfo &FI,
                              FuncOp CalleeInfo,
                              FuncOp newFn, unsigned &CallingConv,
                              bool AttrOnCallSite, bool IsThunk);

  void setCIRFunctionAttributes(FuncOp GD, const LowerFunctionInfo &Info,
                                FuncOp F, bool IsThunk);

  /// Override ABI classification for _Atomic aggregate types.
  void applyAtomicOverrides(FuncOp op, const LowerFunctionInfo &FI);

  /// Override ABI classification for __mfp8 params/returns.
  void applyMfp8Overrides(FuncOp op, const LowerFunctionInfo &FI,
                          unsigned argOffset = 0);

  /// Set function attributes for a function declaration.
  void setFunctionAttributes(FuncOp oldFn, FuncOp newFn,
                             bool IsIncompleteFunction, bool IsThunk);

  // Create a CIR FuncOp with with the given signature.
  FuncOp createCIRFunction(llvm::StringRef MangledName, FuncType Ty, FuncOp D,
                           bool ForVTable, bool DontDefer = false,
                           bool IsThunk = false,
                           llvm::ArrayRef<mlir::Attribute> = {},
                           bool IsForDefinition = false);

  // Rewrite CIR FuncOp to match the target ABI.
  llvm::LogicalResult rewriteFunctionDefinition(FuncOp op);

  // Rewrite CIR CallOp to match the target ABI.
  llvm::LogicalResult rewriteFunctionCall(CallOp callOp, FuncOp funcOp = {});

  // Rewrite CIR TryCallOp to match the target ABI.
  llvm::LogicalResult rewriteFunctionCall(TryCallOp tryCallOp,
                                          FuncOp funcOp = {});
};

/// \p language, when given, stands in for the module's cir.lang: a merged
/// module holds functions from TUs of different languages, and the ABI of a
/// function is its own TU's (see CallConvLowering).
std::unique_ptr<LowerModule> createLowerModule(
    mlir::ModuleOp module, mlir::PatternRewriter &rewriter,
    LowerModuleRequirement requirement =
        LowerModuleRequirement::FullTargetLowering,
    std::optional<cir::SourceLanguage> language = std::nullopt);

} // namespace cir

#endif // CLANG_LIB_CIR_DIALECT_TRANSFORMS_TARGETLOWERING_LOWERMODULE_H
