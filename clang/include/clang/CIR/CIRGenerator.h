//===- CIRGenerator.h - CIR Generation from Clang AST ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a simple interface to perform CIR generation from Clang
// AST
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CIR_CIRGENERATOR_H
#define LLVM_CLANG_CIR_CIRGENERATOR_H

#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/CodeGenOptions.h"

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/Support/VirtualFileSystem.h"

#include <memory>

namespace clang {
class BackendConsumer;
class CoverageSourceInfo;
class DeclGroupRef;
class DiagnosticsEngine;
class HeaderSearchOptions;
class PreprocessorOptions;
namespace CIRGen {
class CIRGenModule;
} // namespace CIRGen
} // namespace clang

namespace llvm {
class Module;
} // namespace llvm

namespace mlir {
class MLIRContext;
} // namespace mlir
namespace cir {
class CIRGenerator : public clang::ASTConsumer {
  virtual void anchor();
  clang::DiagnosticsEngine &diags;
  clang::ASTContext *astContext;
  // Only used for debug info.
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs;

  const clang::CodeGenOptions &codeGenOpts;

  unsigned handlingTopLevelDecls;
  bool lowersToLLVMIR;
  clang::CoverageSourceInfo *coverageSourceInfo;

  /// Set to true after HandleTranslationUnit to prevent late-arriving decls
  /// (e.g. from PCH/module deserialization) from being processed after codegen
  /// is finished. Matches OGCG's IRGenFinished flag.
  bool irGenFinished = false;

  /// Use this when emitting decls to block re-entrant decl emission. It will
  /// emit all deferred decls on scope exit. Set EmitDeferred to false if decl
  /// emission must be deferred longer, like at the end of a tag definition.
  struct HandlingTopLevelDeclRAII {
    CIRGenerator &self;
    bool emitDeferred;
    HandlingTopLevelDeclRAII(CIRGenerator &self, bool emitDeferred = true)
        : self{self}, emitDeferred{emitDeferred} {
      ++self.handlingTopLevelDecls;
    }
    ~HandlingTopLevelDeclRAII() {
      unsigned Level = --self.handlingTopLevelDecls;
      if (Level == 0 && emitDeferred)
        self.emitDeferredDecls();
    }
  };

protected:
  std::unique_ptr<mlir::MLIRContext> mlirContext;
  std::unique_ptr<clang::CIRGen::CIRGenModule> cgm;

private:
  llvm::SmallVector<clang::FunctionDecl *, 8> deferredInlineMemberFuncDefs;

public:
  CIRGenerator(clang::DiagnosticsEngine &diags,
               llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs,
               const clang::CodeGenOptions &cgo, bool lowersToLLVMIR,
               clang::CoverageSourceInfo *coverageSourceInfo = nullptr);
  ~CIRGenerator() override;
  void Initialize(clang::ASTContext &astContext) override;
  bool HandleTopLevelDecl(clang::DeclGroupRef group) override;
  void HandleTranslationUnit(clang::ASTContext &astContext) override;
  void HandleInlineFunctionDefinition(clang::FunctionDecl *d) override;
  void HandleTagDeclDefinition(clang::TagDecl *d) override;
  void HandleTagDeclRequiredDefinition(const clang::TagDecl *D) override;
  void HandleCXXStaticMemberVarInstantiation(clang::VarDecl *D) override;
  void
  HandleOpenACCRoutineReference(const clang::FunctionDecl *FD,
                                const clang::OpenACCRoutineDecl *RD) override;
  void CompleteTentativeDefinition(clang::VarDecl *d) override;
  void HandleVTable(clang::CXXRecordDecl *rd) override;

  /// Pass Sema to CIRGenModule for on-demand template instantiation.
  void setSema(clang::Sema &s);

  mlir::ModuleOp getModule() const;
  /// Erase the underlying CIR module to free memory after lowering to LLVM IR.
  void releaseModule();
  mlir::MLIRContext &getMLIRContext() { return *mlirContext; };
  const mlir::MLIRContext &getMLIRContext() const { return *mlirContext; };

  bool verifyModule() const;

  bool emitCoverageMapping(const clang::HeaderSearchOptions &headerSearchOpts,
                           const clang::PreprocessorOptions &preprocessorOpts,
                           llvm::Module &llvmModule);
  void populateFunctionSourceLocations(clang::BackendConsumer &BC) const;

  /// Record on `module` where each record it lays out is defined, and where
  /// each of its members is (cir.record_locations). For a module serialized
  /// without its AST, which is what a link sees.
  void addRecordLocations(mlir::ModuleOp module) const;

  void emitDeferredDecls();
};

} // namespace cir

#endif // LLVM_CLANG_CIR_CIRGENERATOR_H
