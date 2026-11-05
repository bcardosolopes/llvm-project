//===--- CIRGenPGO.h - PGO Instrumentation for CIR CodeGen -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Instrumentation-based profile-guided optimization for CIR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CIR_CODEGEN_CIRGENPGO_H
#define LLVM_CLANG_LIB_CIR_CODEGEN_CIRGENPGO_H

#include "CIRGenBuilder.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/GlobalValue.h"
#include <memory>
#include <string>

namespace clang {
class Decl;
class Stmt;

namespace CIRGen {

class CIRGenModule;

/// Per-function PGO state for CIR code generation.
///
/// This is a minimal port of OGCG's CodeGenPGO. It handles instrumentation
/// generation (-fprofile-instr-generate) only. Profile-use
/// (-fprofile-instr-use) and MC/DC coverage are left as future work.
class CIRGenPGO {
  CIRGenModule &CGM;

  struct SavedState {
    std::string FuncName;
    unsigned NumRegionCounters = 0;
    uint64_t FunctionHash = 0;
    std::unique_ptr<llvm::DenseMap<const Stmt *, unsigned>> RegionCounterMap;
    unsigned FuncLinkage = 0;
  };

  std::string FuncName;
  unsigned NumRegionCounters = 0;
  uint64_t FunctionHash = 0;

  /// Map from statements to their assigned counter indices.
  std::unique_ptr<llvm::DenseMap<const Stmt *, unsigned>> RegionCounterMap;

  /// LLVM linkage type for the function, needed for FuncNameVar creation
  /// during CIR->LLVM lowering.
  unsigned FuncLinkage = 0;

public:
  class RegionCountersScope {
    CIRGenPGO *PGO = nullptr;
    SavedState State;

    friend class CIRGenPGO;
    explicit RegionCountersScope(CIRGenPGO &PGO);

  public:
    RegionCountersScope() = default;
    RegionCountersScope(RegionCountersScope &&Other) noexcept;
    RegionCountersScope &operator=(RegionCountersScope &&Other) noexcept;
    RegionCountersScope(const RegionCountersScope &) = delete;
    RegionCountersScope &operator=(const RegionCountersScope &) = delete;
    ~RegionCountersScope();
  };

  CIRGenPGO(CIRGenModule &CGM) : CGM(CGM) {}

  /// Assign counters to regions and configure them for PGO of a given
  /// function. Does nothing if instrumentation is not enabled.
  void assignRegionCounters(GlobalDecl GD, cir::FuncOp Fn);

  /// Temporarily switch the active counter map and PGO function identity to
  /// a non-FunctionDecl helper while emitting it inside another function.
  RegionCountersScope
  enterRegionCounters(GlobalDecl GD, llvm::StringRef RawName,
                      llvm::GlobalValue::LinkageTypes Linkage);

  /// Emit a counter increment for the given statement. When \p StepV is
  /// non-null the counter is incremented by that (integer or boolean) value
  /// instead of by one, mirroring classic CodeGen's stepped counters (used so a
  /// do-while region counter holds the loop backedge count).
  void emitCounterIncrement(CIRGenBuilderTy &Builder, const Stmt *S,
                            mlir::Location Loc, mlir::Value StepV = nullptr);

  /// Whether region counters have been assigned.
  bool haveRegionCounters() const { return RegionCounterMap != nullptr; }

private:
  SavedState saveState();
  void restoreState(SavedState State);
  void clearRegionCounters();
  bool shouldAssignRegionCounters(GlobalDecl GD) const;
  void mapRegionCounters(const Decl *D);
  void setFuncName(llvm::StringRef Name,
                   llvm::GlobalValue::LinkageTypes Linkage);
  void setFuncName(cir::FuncOp Fn);
};

} // namespace CIRGen
} // namespace clang

#endif // LLVM_CLANG_LIB_CIR_CODEGEN_CIRGENPGO_H
