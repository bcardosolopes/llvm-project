//===--- CIRGenModule.h - Per-Module state for CIR gen ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the internal per-translation-unit state used for CIR translation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CIR_CODEGEN_CIRGENMODULE_H
#define LLVM_CLANG_LIB_CIR_CODEGEN_CIRGENMODULE_H

#include "CIRGenBuilder.h"
#include "CIRGenCall.h"
#include "CIRGenTBAA.h"
#include "CIRGenTypeCache.h"
#include "CIRGenTypes.h"
#include "CIRGenVTables.h"
#include "CIRGenValue.h"

#include "clang/AST/CharUnits.h"
#include "clang/CIR/Dialect/IR/CIRDataLayout.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"

#include "TargetInfo.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/SymbolTable.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/ProfileList.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/TargetParser/Triple.h"

#include <optional>
#include <string>

namespace clang {
class ASTContext;
class BlockExpr;
class CodeGenOptions;
class CompoundLiteralExpr;
class Decl;
class FunctionDecl;
class GlobalDecl;
class LangOptions;
class MaterializeTemporaryExpr;
class Module;
class ObjCEncodeExpr;
class ObjCInterfaceDecl;
class ObjCMethodDecl;
class OMPExecutableDirective;
class OMPDeclareMapperDecl;
class Selector;
class Sema;
class Stmt;
class TargetInfo;
class TemplateParamObjectDecl;
class UnnamedGlobalConstantDecl;
class VarDecl;

namespace CIRGen {

class CIRGenFunction;
class CIRGenCXXABI;
class CIRGenCUDARuntime;

enum ForDefinition_t : bool { NotForDefinition = false, ForDefinition = true };

/// This class organizes the cross-function state that is used while generating
/// CIR code.
class CIRGenModule : public CIRGenTypeCache {
  CIRGenModule(CIRGenModule &) = delete;
  CIRGenModule &operator=(CIRGenModule &) = delete;

public:
  using SymbolLookupCacheTy = llvm::StringMap<mlir::Operation *>;

  struct DeferredDeclToEmit {
    clang::GlobalDecl gd;
    llvm::StringRef mangledName;
  };

  CIRGenModule(mlir::MLIRContext &mlirContext, clang::ASTContext &astContext,
               const clang::CodeGenOptions &cgo,
               clang::DiagnosticsEngine &diags, bool lowersToLLVMIR);

  ~CIRGenModule();

  friend class CIRGenVTables;

private:
  mutable std::unique_ptr<TargetCIRGenInfo> theTargetCIRGenInfo;

  CIRGenBuilderTy builder;

  /// Hold Clang AST information.
  clang::ASTContext &astContext;

  const clang::LangOptions &langOpts;

  const clang::CodeGenOptions &codeGenOpts;

  const bool lowersToLLVMIR;

  /// A "module" matches a c/cpp source file: containing a list of functions.
  mlir::ModuleOp theModule;
  mutable std::unique_ptr<cir::CIRDataLayout> cachedDataLayout;

  clang::DiagnosticsEngine &diags;

  const clang::TargetInfo &target;

  /// Optional Sema pointer for triggering on-demand template instantiation.
  /// Set by CIRGenerator after construction.
  clang::Sema *sema = nullptr;

  std::unique_ptr<CIRGenCXXABI> abi;

  CIRGenTypes genTypes;

  /// Holds information about C++ vtables.
  CIRGenVTables vtables;

  /// Holds the CUDA runtime
  std::unique_ptr<CIRGenCUDARuntime> cudaRuntime;

  std::unique_ptr<CIRGenTBAA> tbaa;

  /// Per-function codegen information. Updated everytime emitCIR is called
  /// for FunctionDecls's.
  CIRGenFunction *curCGF = nullptr;

  llvm::SmallVector<mlir::Attribute> globalScopeAsm;

  /// Globals/functions that should appear in @llvm.used (prevents both
  /// compiler and linker from stripping).
  llvm::SmallVector<std::string> llvmUsed;
  /// Globals/functions that should appear in @llvm.compiler.used (prevents
  /// only compiler from stripping; linker may still GC).
  llvm::SmallVector<std::string> llvmCompilerUsed;

  /// Used for uniquing of annotation arguments.
  llvm::DenseMap<unsigned, mlir::ArrayAttr> annotationArgs;

  /// Cache SourceLocation-to-MLIR locations at module scope to reduce
  /// redundant SourceManager lookups/FileLineColLoc creation when emitting
  /// many declarations and globals.
  llvm::DenseMap<unsigned, mlir::Location> sourceLocCache;

  /// Cache filename StringAttr values by SourceManager-owned filename pointer.
  /// This reduces repeated StringAttr uniquing/hash/compare work in hot
  /// location creation paths.
  llvm::DenseMap<const char *, mlir::StringAttr> sourceFileAttrCache;

  /// Cache SourceRange-to-MLIR fused locations at module scope.
  llvm::DenseMap<uint64_t, mlir::Location> sourceRangeLocCache;

  /// Cached dictionary attr used by the common no-pragma loop-hint case:
  /// {mustprogress = true}. Reusing this avoids repeated MLIR attribute
  /// uniquing work in template-heavy translation units with many loops.
  mlir::DictionaryAttr mustProgressLoopHintAttr;

  /// Reused BoolAttr singletons to avoid repeated bool attr construction in
  /// hot attribute-building paths.
  mutable mlir::BoolAttr trueAttr;
  mutable mlir::BoolAttr falseAttr;

  /// Cache frequently repeated StringAttr keys (e.g. loop-hint dictionary
  /// field names) by static string literal pointer identity.
  mutable llvm::DenseMap<const char *, mlir::StringAttr> stringAttrCache;
  /// Cache frequently repeated dynamic StringAttr values keyed by string
  /// content (used for repeated option-derived values).
  mutable llvm::StringMap<mlir::StringAttr> dynamicStringAttrCache;

  /// Cache of computed debug field/record attributes per record definition.
  /// Avoids re-iterating RecordDecl fields for every global of the same type.
  llvm::DenseMap<const clang::RecordDecl *, mlir::DictionaryAttr>
      recordDebugFieldAttrCache;

  /// Store deferred function annotations so they can be emitted at the end with
  /// most up to date ValueDecl that will have all the inherited annotations.
  llvm::DenseMap<llvm::StringRef, const ValueDecl *> deferredAnnotations;

  /// Counter tracking the declaration order of C++ globals with dynamic
  /// initialization. Used to ensure _GLOBAL__sub_I_ calls init functions in
  /// TU declaration order, matching OGCG's CXXGlobalInits ordering.
  unsigned cxxGlobalInitOrder = 0;

  /// When a C++ decl with an initializer is deferred, we record its
  /// declaration-order position here. When the decl is finally emitted,
  /// this position is stored on the GlobalOp so LoweringPrepare can sort
  /// init functions correctly. Mirrors OGCG's DelayedCXXInitPosition.
  llvm::DenseMap<const clang::Decl *, unsigned> delayedCXXInitPosition;


  /// An optional suffix appended to internal-linkage function mangled names
  /// when -funique-internal-linkage-names is active. The suffix is
  /// ".__uniq.<MD5-hash-of-source-path>" and is computed once in the
  /// constructor. Empty string when the option is disabled.
  std::string moduleNameHash;

  /// thread_local variables defined or used in this TU.
  std::vector<const clang::VarDecl *> cxxThreadLocals;

  /// The complete set of modules that has been imported.
  llvm::SetVector<clang::Module *> importedModules;

  /// The set of modules for which the module initializers have been emitted.
  llvm::SmallPtrSet<clang::Module *, 16> emittedModuleInitializers;

  /// OpenMP `requires` directive state accumulated for this translation unit.
  bool hasOpenMPRequiresUnifiedSharedMemory = false;
  llvm::AtomicOrdering openMPRequiresAtomicOrdering =
      llvm::AtomicOrdering::Monotonic;
  llvm::DenseMap<const clang::OMPDeclareMapperDecl *, mlir::FlatSymbolRefAttr>
      openMPDeclareMappers;
  llvm::StringMap<mlir::FlatSymbolRefAttr> openMPCriticalDecls;
  llvm::DenseSet<const clang::FunctionDecl *>
      openMPTargetDeviceScannedFunctions;
  llvm::DenseSet<const clang::FunctionDecl *>
      openMPTargetGlobalizedFunctions;
  llvm::DenseSet<const clang::FunctionDecl *>
      openMPTargetGenericFunctions;
  unsigned openMPTargetGenericEmissionDepth = 0;
  llvm::DenseMap<clang::QualType, cir::FuncOp> atomicSetterHelperFns;
  llvm::DenseMap<clang::QualType, cir::FuncOp> atomicGetterHelperFns;
  llvm::SetVector<clang::Decl *> coverageMappingDecls;
  llvm::DenseMap<const clang::Decl *, std::string> coverageMappingPGONames;
  llvm::DenseMap<const clang::Decl *, std::string> coverageMappingSymbolNames;
  unsigned completedFunctionEmissionCount = 0;
  llvm::DenseMap<mlir::Operation *, unsigned> completedFunctionEmissionOrder;
  /// Finished functions that reference a given symbol. This keeps no-proto and
  /// declaration-type repair from rescanning every completed function body for
  /// each symbol query. Stored as function names so later symbol erasure only
  /// leaves harmless stale positives.
  llvm::StringMap<llvm::SmallVector<mlir::StringAttr, 4>>
      completedFunctionSymbolUserIndex;

public:
  mlir::ModuleOp getModule() const { return theModule; }

  /// Erase the top-level CIR module and null the handle. Called after lowering
  /// to LLVM IR to free the (often very large) CIR operations, which are not
  /// held by any OwningOpRef and would otherwise stay resident through backend
  /// emission. See the definition for rationale.
  void releaseModule();

  unsigned getCompletedFunctionEmissionCount() const {
    return completedFunctionEmissionCount;
  }
  void noteFunctionEmissionComplete(cir::FuncOp fn);
  bool wasFunctionEmissionCompleteBefore(cir::FuncOp fn,
                                         unsigned emissionCountSnapshot) const {
    auto it = completedFunctionEmissionOrder.find(fn.getOperation());
    return it != completedFunctionEmissionOrder.end() &&
           it->second <= emissionCountSnapshot;
  }
  void trackCoverageMappingDecl(clang::Decl *D,
                                llvm::StringRef symbolName = {},
                                llvm::StringRef pgoName = {});
  const llvm::SetVector<clang::Decl *> &getCoverageMappingDecls() const {
    return coverageMappingDecls;
  }
  const llvm::DenseMap<const clang::Decl *, std::string> &
  getCoverageMappingPGONames() const {
    return coverageMappingPGONames;
  }
  const llvm::DenseMap<const clang::Decl *, std::string> &
  getCoverageMappingSymbolNames() const {
    return coverageMappingSymbolNames;
  }
  std::string getCoverageMappingPGOName(cir::FuncOp fn) const;
  CIRGenBuilderTy &getBuilder() { return builder; }

  /// After a global's type changes (e.g. a forward-declared RTTI descriptor
  /// becoming its full definition), retype every GetGlobalOp user of `oldName`
  /// that still references the old pointer type to `newGlobalPtrTy`, inserting a
  /// bitcast to preserve each use's original type. Uses the symbol-user index
  /// instead of a whole-module walk, avoiding O(globals x module-size) cost.
  void retypeGetGlobalUsers(mlir::StringAttr oldName,
                            cir::PointerType newGlobalPtrTy);
  mlir::IntegerAttr getI64AttrCached(int64_t value) {
    auto it = i64AttrCache.find(value);
    if (it != i64AttrCache.end())
      return it->second;
    auto ins = i64AttrCache.try_emplace(value, builder.getI64IntegerAttr(value));
    return ins.first->second;
  }
  mlir::StringAttr getUIntStringAttrCached(unsigned value) {
    auto [it, inserted] = uintStringAttrCache.try_emplace(value);
    if (inserted)
      it->second = getStringAttrCached(llvm::utostr(value));
    return it->second;
  }
  clang::ASTContext &getASTContext() const { return astContext; }
  const clang::TargetInfo &getTarget() const { return target; }
  const clang::CodeGenOptions &getCodeGenOpts() const { return codeGenOpts; }
  bool isLoweringToLLVMIR() const { return lowersToLLVMIR; }
  const std::string &getModuleNameHash() const { return moduleNameHash; }

  ProfileList::ExclusionType
  isFunctionBlockedByProfileList(llvm::StringRef funcName,
                                 clang::SourceLocation loc) const;
  ProfileList::ExclusionType
  isFunctionBlockedFromProfileInstr(llvm::StringRef funcName,
                                    clang::SourceLocation loc) const;

  /// Returns the minimum object size for an object of the given type.
  /// For class types, returns the non-virtual size (since the object could
  /// be a subclass), except for effectively final classes.
  /// Matches OGCG CodeGenModule::getMinimumObjectSize.
  clang::CharUnits getMinimumObjectSize(clang::QualType ty) {
    if (const auto *rd = ty->getAsCXXRecordDecl()) {
      if (!rd->hasDefinition())
        return clang::CharUnits::One();
      auto &layout = astContext.getASTRecordLayout(rd);
      if (rd->isEffectivelyFinal())
        return layout.getSize();
      return std::max(layout.getNonVirtualSize(), clang::CharUnits::One());
    }
    return astContext.getTypeSizeInChars(ty);
  }
  clang::DiagnosticsEngine &getDiags() const { return diags; }
  CIRGenTypes &getTypes() { return genTypes; }
  const clang::LangOptions &getLangOpts() const { return langOpts; }

  /// Returns whether this module needs the "all-vtables" type identifier.
  /// Matches OGCG CodeGenModule::NeedAllVtablesTypeId: true if at least one
  /// vtable-based CFI checker is enabled and is not in the trapping mode.
  bool needAllVtablesTypeId() const {
    using clang::SanitizerKind;
    return ((langOpts.Sanitize.has(SanitizerKind::CFIVCall) &&
             !codeGenOpts.SanitizeTrap.has(SanitizerKind::CFIVCall)) ||
            (langOpts.Sanitize.has(SanitizerKind::CFINVCall) &&
             !codeGenOpts.SanitizeTrap.has(SanitizerKind::CFINVCall)) ||
            (langOpts.Sanitize.has(SanitizerKind::CFIDerivedCast) &&
             !codeGenOpts.SanitizeTrap.has(SanitizerKind::CFIDerivedCast)) ||
            (langOpts.Sanitize.has(SanitizerKind::CFIUnrelatedCast) &&
             !codeGenOpts.SanitizeTrap.has(SanitizerKind::CFIUnrelatedCast)));
  }

  bool hasRequiresUnifiedSharedMemory() const {
    return hasOpenMPRequiresUnifiedSharedMemory;
  }
  llvm::AtomicOrdering getOpenMPAtomicDefaultOrdering() const {
    return openMPRequiresAtomicOrdering;
  }

  /// Returns true if default visibility should be mapped to dllexport.
  /// Matches OGCG's CodeGenModule::shouldMapVisibilityToDLLExport.
  bool shouldMapVisibilityToDLLExport(const clang::NamedDecl *D) const {
    return getLangOpts().hasDefaultVisibilityExportMapping() && D &&
           (D->getLinkageAndVisibility().getVisibility() ==
            clang::DefaultVisibility) &&
           (getLangOpts().isAllDefaultVisibilityExportMapping() ||
            (getLangOpts().isExplicitDefaultVisibilityExportMapping() &&
             D->getLinkageAndVisibility().isVisibilityExplicit()));
  }

  const std::vector<const clang::VarDecl *> &getCXXThreadLocals() const {
    return cxxThreadLocals;
  }

  CIRGenCXXABI &getCXXABI() const { return *abi; }
  bool hasCXXABI() const { return abi != nullptr; }
  mlir::MLIRContext &getMLIRContext() { return *builder.getContext(); }
  const mlir::MLIRContext &getMLIRContext() const {
    return *builder.getContext();
  }
  mlir::BoolAttr getBoolAttrCached(bool value) const {
    if (value) {
      if (!trueAttr)
        trueAttr = mlir::BoolAttr::get(builder.getContext(), true);
      return trueAttr;
    }
    if (!falseAttr)
      falseAttr = mlir::BoolAttr::get(builder.getContext(), false);
    return falseAttr;
  }
  mlir::StringAttr getStringAttrCached(const char *value) const {
    auto [it, inserted] = stringAttrCache.try_emplace(value);
    if (inserted)
      it->second = mlir::StringAttr::get(builder.getContext(), value);
    return it->second;
  }
  mlir::StringAttr getStringAttrCached(llvm::StringRef value) const {
    auto [it, inserted] = dynamicStringAttrCache.try_emplace(value);
    if (inserted)
      it->second = mlir::StringAttr::get(builder.getContext(), value);
    return it->second;
  }
  cir::FuncOp getAtomicSetterHelperFn(clang::QualType ty) const {
    auto it = atomicSetterHelperFns.find(ty);
    return it == atomicSetterHelperFns.end() ? cir::FuncOp{} : it->second;
  }
  void setAtomicSetterHelperFn(clang::QualType ty, cir::FuncOp fn) {
    atomicSetterHelperFns[ty] = fn;
  }
  cir::FuncOp getAtomicGetterHelperFn(clang::QualType ty) const {
    auto it = atomicGetterHelperFns.find(ty);
    return it == atomicGetterHelperFns.end() ? cir::FuncOp{} : it->second;
  }
  void setAtomicGetterHelperFn(clang::QualType ty, cir::FuncOp fn) {
    atomicGetterHelperFns[ty] = fn;
  }
  mlir::DictionaryAttr getMustProgressLoopHintAttr();

  /// Return a reference to the configured CUDA runtime.
  CIRGenCUDARuntime &getCUDARuntime() {
    assert(cudaRuntime != nullptr);
    return *cudaRuntime;
  }

  bool shouldEmitCUDAGlobalVar(const VarDecl *global) const;

  void printPostfixForExternalizedDecl(llvm::raw_ostream &os,
                                       const Decl *d) const;

  const cir::CIRDataLayout &getDataLayout() const;

  /// -------
  /// Handling globals
  /// -------

  mlir::Operation *lastGlobalOp = nullptr;

  /// Keep a map between lambda fields and names, this needs to be per module
  /// since lambdas might get generated later as part of defered work, and since
  /// the pointers are supposed to be uniqued, should be fine. Revisit this if
  /// it ends up taking too much memory.
  llvm::DenseMap<const clang::FieldDecl *, llvm::StringRef> lambdaFieldToName;
  /// Map BlockAddrInfoAttr (function name, label name) to the corresponding CIR
  /// LabelOp. This provides the main lookup table used to resolve block
  /// addresses into their label operations.
  llvm::DenseMap<cir::BlockAddrInfoAttr, cir::LabelOp> blockAddressInfoToLabel;
  /// Map CIR BlockAddressOps directly to their resolved LabelOps.
  /// Used once a block address has been successfully lowered to a label.
  llvm::MapVector<cir::BlockAddressOp, cir::LabelOp> blockAddressToLabel;
  /// Track CIR BlockAddressOps that cannot be resolved immediately
  /// because their LabelOp has not yet been emitted. These entries
  /// are solved later once the corresponding label is available.
  llvm::DenseSet<cir::BlockAddressOp> unresolvedBlockAddressToLabel;
  cir::LabelOp lookupBlockAddressInfo(cir::BlockAddrInfoAttr blockInfo);
  std::string getBlockAddressLabelName(const clang::LabelDecl &label) const;
  void mapBlockAddress(cir::BlockAddrInfoAttr blockInfo, cir::LabelOp label);
  void mapUnresolvedBlockAddress(cir::BlockAddressOp op);
  void mapResolvedBlockAddress(cir::BlockAddressOp op, cir::LabelOp);
  void updateResolvedBlockAddress(cir::BlockAddressOp op,
                                  cir::LabelOp newLabel);
  /// Tell the consumer that this variable has been instantiated.
  void handleCXXStaticMemberVarInstantiation(VarDecl *vd);

  llvm::DenseMap<const Decl *, cir::GlobalOp> staticLocalDeclMap;
  llvm::DenseMap<const VarDecl *, cir::GlobalOp> initializerConstants;
  llvm::DenseMap<const UnnamedGlobalConstantDecl *, cir::GlobalOp>
      unnamedGlobalConstantDeclMap;

  /// Map used to track internal linkage functions and variables declared
  /// within extern "C" regions. Maps the unmangled C identifier to the
  /// mangled symbol name. A null value indicates multiple conflicting
  /// declarations exist for that name (suppressing alias creation).
  typedef llvm::MapVector<const IdentifierInfo *, llvm::StringRef>
      StaticExternCMap;
  StaticExternCMap staticExternCValues;

  /// Cache for O(1) symbol lookups by name, replacing the O(N) linear scan
  /// in SymbolTable::lookupSymbolIn that getGlobalValue used previously.
  ///
  /// Store keys as StringRef-backed StringMap entries to avoid constructing
  /// temporary StringAttr values on every lookup in hot paths.
  SymbolLookupCacheTy symbolLookupCache;

  mlir::Operation *getGlobalValue(llvm::StringRef ref);

  /// O(1) lookup of a FuncOp by name in the symbol cache.
  /// Returns nullptr if the name is not found or is not a FuncOp.
  cir::FuncOp lookupFuncOp(llvm::StringRef name) {
    auto *op = getGlobalValue(name);
    return op ? mlir::dyn_cast<cir::FuncOp>(op) : cir::FuncOp{};
  }

  void insertGlobalSymbol(mlir::Operation *op) {
    if (auto sym = mlir::dyn_cast<mlir::SymbolOpInterface>(op))
      symbolLookupCache[sym.getName()] = op;
  }
  void eraseGlobalSymbol(mlir::Operation *op) {
    if (auto sym = mlir::dyn_cast<mlir::SymbolOpInterface>(op))
      symbolLookupCache.erase(sym.getName());
  }

  cir::GlobalOp getStaticLocalDeclAddress(const VarDecl *d) {
    return staticLocalDeclMap[d];
  }

  void setStaticLocalDeclAddress(const VarDecl *d, cir::GlobalOp c) {
    staticLocalDeclMap[d] = c;
  }

  cir::GlobalOp getOrCreateStaticVarDecl(const VarDecl &d,
                                         cir::GlobalLinkageKind linkage);

  /// If the specified mangled name is not in the module, create and return an
  /// mlir::GlobalOp value
  cir::GlobalOp getOrCreateCIRGlobal(llvm::StringRef mangledName, mlir::Type ty,
                                     LangAS langAS, const VarDecl *d,
                                     ForDefinition_t isForDefinition);

  cir::GlobalOp getOrCreateCIRGlobal(const VarDecl *d, mlir::Type ty,
                                     ForDefinition_t isForDefinition);

  static cir::GlobalOp
  createGlobalOp(CIRGenModule &cgm, mlir::Location loc, llvm::StringRef name,
                 mlir::Type t, bool isConstant = false,
                 mlir::ptr::MemorySpaceAttrInterface addrSpace = {},
                 mlir::Operation *insertPoint = nullptr);

  /// Create an unnamed global constant with the given constant initializer.
  /// If the variable already has a cached constant global, use that.
  /// Returns an Address pointing to the global.
  Address createUnnamedGlobalFrom(const VarDecl &d, mlir::Attribute constant,
                                  CharUnits align);

  /// Add a global constructor or destructor to the module.
  /// The priority is optional, if not specified, the default priority is used.
  void addGlobalCtor(cir::FuncOp ctor,
                     std::optional<int> priority = std::nullopt);
  void addGlobalDtor(cir::FuncOp dtor,
                     std::optional<int> priority = std::nullopt,
                     bool isDtorAttrFunc = false);

  /// Track globals/functions that should appear in @llvm.used or
  /// @llvm.compiler.used arrays in the final LLVM IR.
  void addUsedGlobal(mlir::Operation *gv);
  void addCompilerUsedGlobal(mlir::Operation *gv);
  void addUsedOrCompilerUsedGlobal(mlir::Operation *gv);
  void emitLLVMUsed();

  /// If the declaration has internal linkage but is inside an extern "C"
  /// linkage specification, prepare to emit an alias for it to the expected
  /// name.
  template <typename SomeDecl>
  void maybeHandleStaticInExternC(const SomeDecl *d, llvm::StringRef symName);

  /// Emit aliases from unmangled extern "C" names to their mangled
  /// counterparts for internal-linkage declarations marked with
  /// __attribute__((used)).
  void emitStaticExternCAliases();
  bool checkAndReplaceExternCIFuncs(mlir::Operation *elem,
                                    mlir::Operation *cppFunc);
  void checkAliases();

  bool shouldZeroInitPadding() const {
    // In C23 (N3096) $6.7.10:
    // """
    // If any object is initialized with an empty initializer, then it is
    // subject to default initialization:
    //  - if it is an aggregate, every member is initialized (recursively)
    //  according to these rules, and any padding is initialized to zero bits;
    //  - if it is a union, the first named member is initialized (recursively)
    //  according to these rules, and any padding is initialized to zero bits.
    //
    // If the aggregate or union contains elements or members that are
    // aggregates or unions, these rules apply recursively to the subaggregates
    // or contained unions.
    //
    // If there are fewer initializers in a brace-enclosed list than there are
    // elements or members of an aggregate, or fewer characters in a string
    // literal used to initialize an array of known size than there are elements
    // in the array, the remainder of the aggregate is subject to default
    // initialization.
    // """
    //
    // The standard seems ambiguous in the following two areas:
    // 1. For a union type with empty initializer, if the first named member is
    // not the largest member, then the bytes comes after the first named member
    // but before padding are left unspecified. An example is:
    //    union U { int a; long long b;};
    //    union U u = {};  // The first 4 bytes are 0, but 4-8 bytes are left
    //    unspecified.
    //
    // 2. It only mentions padding for empty initializer, but doesn't mention
    // padding for a non empty initialization list. And if the aggregation or
    // union contains elements or members that are aggregates or unions, and
    // some are non empty initializers, while others are empty initializers,
    // the padding initialization is unclear. An example is:
    //    struct S1 { int a; long long b; };
    //    struct S2 { char c; struct S1 s1; };
    //    // The values for paddings between s2.c and s2.s1.a, between s2.s1.a
    //    and s2.s1.b are unclear.
    //    struct S2 s2 = { 'c' };
    //
    // Here we choose to zero initiailize left bytes of a union type because
    // projects like the Linux kernel are relying on this behavior. If we don't
    // explicitly zero initialize them, the undef values can be optimized to
    // return garbage data. We also choose to zero initialize paddings for
    // aggregates and unions, no matter they are initialized by empty
    // initializers or non empty initializers. This can provide a consistent
    // behavior. So projects like the Linux kernel can rely on it.
    return !getLangOpts().CPlusPlus;
  }

  llvm::StringMap<unsigned> cgGlobalNames;
  std::string getUniqueGlobalName(const std::string &baseName);
  std::string getUniqueBlockName(const clang::BlockExpr *blockExpr,
                                 llvm::StringRef baseName);

  /// Return the mlir::Value for the address of the given global variable.
  /// If Ty is non-null and if the global doesn't exist, then it will be created
  /// with the specified type instead of whatever the normal requested type
  /// would be. If IsForDefinition is true, it is guaranteed that an actual
  /// global with type Ty will be returned, not conversion of a variable with
  /// the same mangled name but some other type.
  mlir::Value
  getAddrOfGlobalVar(const VarDecl *d, mlir::Type ty = {},
                     ForDefinition_t isForDefinition = NotForDefinition);

  /// Erase the address chain materialized by getAddrOfGlobalVar when only its
  /// registration side effect was wanted. Classical codegen can discard the
  /// result because GetAddrOfGlobalVar hands back a constant, but every path
  /// here builds a fresh cir.get_global -- and, for a variable alias, a
  /// bitcast over it. \p addr must have no uses, and its producers are erased
  /// only while they stay use-empty. The cir.global itself is named by symbol
  /// rather than held as an operand, so erasing the chain never unregisters it.
  void eraseUnusedGlobalVarAddr(mlir::Value addr);

  /// Returns the address of the device-side declare-target reference pointer
  /// global for link and to/enter+USM variables, or an empty value when the
  /// variable should use its ordinary device global directly.
  mlir::Value getAddrOfOpenMPDeclareTargetVar(const VarDecl *d);

  /// Return the mlir::GlobalViewAttr for the address of the given global.
  cir::GlobalViewAttr getAddrOfGlobalVarAttr(const VarDecl *d);

  /// Return the mlir::GlobalViewAttr for the address of the given function.
  /// When `noCFI` is true, mark the result as a CFI-unchecked function-entry
  /// pointer, matching OGCG's NoCFIValue semantics.
  cir::GlobalViewAttr getAddrOfFunctionAttr(const ValueDecl *vd,
                                            bool noCFI = false);

  /// Returns a pointer to a global variable representing a temporary with
  /// static or thread storage duration.
  mlir::Operation *
  getAddrOfGlobalTemporary(const clang::MaterializeTemporaryExpr *expr,
                           const clang::Expr *init);

  /// Return the address of a template parameter object (C++20 class-type NTTP).
  cir::GlobalOp
  getAddrOfTemplateParamObject(const clang::TemplateParamObjectDecl *tpo);

  /// Return the address of an unnamed global constant (e.g. the backing store
  /// for __builtin_source_location).
  cir::GlobalOp getAddrOfUnnamedGlobalConstantDecl(
      const clang::UnnamedGlobalConstantDecl *gcd);

  CharUnits computeNonVirtualBaseClassOffset(
      const CXXRecordDecl *derivedClass,
      llvm::iterator_range<CastExpr::path_const_iterator> path);

  /// Get the CIR attributes and calling convention to use for a particular
  /// function type.
  ///
  /// \param name - The function name.
  /// \param info - The function type information.
  /// \param calleeInfo - The callee information these attributes are being
  /// constructed for. If valid, the attributes applied to this decl may
  /// contribute to the function attributes and calling convention.
  /// \param attrs [out] - On return, the attribute list to use.
  /// \param callingConv [out] - On return, the calling convention to use.
  /// \param sideEffect [out] - On return, the side effect type of the
  /// attributes.
  /// \param attrOnCallSite - Whether or not the attributes are on a call site.
  /// \param isThunk - Whether the function is a thunk.
  void constructAttributeList(llvm::StringRef name,
                              const CIRGenFunctionInfo &info,
                              CIRGenCalleeInfo calleeInfo,
                              mlir::NamedAttrList &attrs,
                              cir::CallingConv &callingConv,
                              cir::SideEffect &sideEffect, bool attrOnCallSite,
                              bool isThunk);

  /// Apply CodeGenOpts-driven default function attributes. Matches OGCG's
  /// CodeGenModule::getDefaultFunctionAttributes in CGCall.cpp.
  void getDefaultFunctionAttributes(llvm::StringRef name, bool hasOptnone,
                                    bool attrOnCallSite,
                                    mlir::NamedAttrList &funcAttrs);

  /// Cached default function attributes for common cases. Populated lazily.
  mlir::DictionaryAttr cachedDefaultCallSiteAttrs;
  mlir::DictionaryAttr cachedDefaultFuncDefAttrs;

  /// Cached "target-features" attribute for the common case (no per-function
  /// target attributes). The filtered+sorted+joined feature string is module-
  /// global and identical for every function, so it is computed once. The attr
  /// may be a (possibly empty) string; the bool tracks whether it was computed.
  bool cachedDefaultTargetFeaturesComputed = false;
  mlir::StringAttr cachedDefaultTargetFeaturesAttr;

  /// Will return a global variable of the given type. If a variable with a
  /// different type already exists then a new variable with the right type
  /// will be created and all uses of the old variable will be replaced with a
  /// bitcast to the new variable.
  cir::GlobalOp createOrReplaceCXXRuntimeVariable(
      mlir::Location loc, llvm::StringRef name, mlir::Type ty,
      cir::GlobalLinkageKind linkage, clang::CharUnits alignment,
      mlir::ptr::MemorySpaceAttrInterface addrSpace = {});

  void emitVTable(const CXXRecordDecl *rd);

  void addDeferredVTable(const CXXRecordDecl *rd) {
    deferredVTables.push_back(rd);
  }

  void emitDeferredVTables();

  bool shouldOpportunisticallyEmitVTables();

  /// Try to emit external vtables as available_externally if they have emitted
  /// all inlined virtual functions.
  void emitVTablesOpportunistically();

  /// Return the appropriate linkage for the vtable, VTT, and type information
  /// of the given class.
  cir::GlobalLinkageKind getVTableLinkage(const CXXRecordDecl *rd);

  /// Get the address of the RTTI descriptor for the given type.
  mlir::Attribute getAddrOfRTTIDescriptor(mlir::Location loc, QualType ty,
                                          bool forEH = false);

  static mlir::SymbolTable::Visibility getMLIRVisibility(Visibility v) {
    switch (v) {
    case DefaultVisibility:
      return mlir::SymbolTable::Visibility::Public;
    case HiddenVisibility:
      return mlir::SymbolTable::Visibility::Private;
    case ProtectedVisibility:
      // The distinction between ProtectedVisibility and DefaultVisibility is
      // that symbols with ProtectedVisibility, while visible to the dynamic
      // linker like DefaultVisibility, are guaranteed to always dynamically
      // resolve to a symbol in the current shared object. There is currently no
      // equivalent MLIR visibility, so we fall back on the fact that the symbol
      // is visible.
      return mlir::SymbolTable::Visibility::Public;
    }
    llvm_unreachable("unknown visibility!");
  }

  static cir::VisibilityKind getCIRVisibilityKind(Visibility v) {
    switch (v) {
    case DefaultVisibility:
      return cir::VisibilityKind::Default;
    case HiddenVisibility:
      return cir::VisibilityKind::Hidden;
    case ProtectedVisibility:
      return cir::VisibilityKind::Protected;
    }
    llvm_unreachable("unknown visibility!");
  }

  llvm::DenseMap<mlir::Attribute, cir::GlobalOp> constantStringMap;

  /// Return a constant array for the given string.
  mlir::Attribute getConstantArrayFromStringLiteral(const StringLiteral *e);

  /// Return a global symbol reference to a constant array for the given string
  /// literal.
  cir::GlobalOp getGlobalForStringLiteral(const StringLiteral *s,
                                          llvm::StringRef name = ".str");

  /// Return a global symbol reference to a constant char array containing the
  /// given bytes plus a trailing null terminator.
  cir::GlobalOp getGlobalForConstantCString(mlir::Location loc,
                                            llvm::StringRef str,
                                            llvm::StringRef name = ".str");

  /// Return a global symbol reference to a constant char array for the given
  /// ObjC @encode expression.
  cir::GlobalOp getGlobalForObjCEncodeExpr(const ObjCEncodeExpr *e,
                                           llvm::StringRef name = ".str");

  /// Return a global symbol reference to a constant array for the given string
  /// literal.
  cir::GlobalViewAttr
  getAddrOfConstantStringFromLiteral(const StringLiteral *s,
                                     llvm::StringRef name = ".str");

  /// Return a pointer to a constant char array for the given ObjC @encode
  /// expression.
  cir::GlobalViewAttr
  getAddrOfConstantStringFromObjCEncode(const ObjCEncodeExpr *e,
                                        llvm::StringRef name = ".str");

  /// Return a pointer to a constant Objective-C string object for the given
  /// literal, using the active runtime's object layout.
  mlir::TypedAttr getAddrOfConstantObjCString(const StringLiteral *literal);

  /// Return a pointer to a constant CFString object for the given string.
  cir::GlobalViewAttr getAddrOfConstantCFString(const StringLiteral *literal);

  /// Return a pointer to a constant global no-capture block literal.
  cir::GlobalViewAttr getAddrOfGlobalBlockLiteral(mlir::Location loc,
                                                  clang::GlobalDecl gd,
                                                  const clang::BlockExpr *e,
                                                  const clang::VarDecl *d = nullptr);

  /// Return true when the block invoke uses an ABI-indirect return whose slot
  /// consumes an argument position, matching OGCG's BLOCK_USE_STRET logic.
  bool blockFunctionUsesStret(const clang::FunctionProtoType *blockProto,
                              const FunctionArgList &params,
                              cir::FuncType invokeTy);

  /// Return the selector-name global for the given selector.
  cir::GlobalOp getObjCSelectorNameGlobal(Selector sel);

  /// Return the Mach-O selector-ref global for the given selector.
  cir::GlobalOp getObjCSelectorRefGlobal(Selector sel);

  /// Return the non-Mach-O selector global for the given selector/type pair.
  cir::GlobalOp getObjCGNUSelectorGlobal(
      Selector sel, const ObjCMethodDecl *method = nullptr);

  /// Materialize and load an ObjC selector value.
  mlir::Value getObjCSelector(CIRGenFunction &cgf, Selector sel,
                              mlir::Location loc,
                              const ObjCMethodDecl *method = nullptr);

  /// Return the ObjC runtime class-name string global for the given name.
  cir::GlobalOp getObjCClassNameGlobal(llvm::StringRef className);

  /// Return the ObjC runtime method-type string global for the given encoding.
  cir::GlobalOp getObjCMethodTypeGlobal(llvm::StringRef typeEncoding);
  /// Mangle the symbol name for an Objective-C method implementation.
  void mangleObjCMethodName(const ObjCMethodDecl *method,
                            llvm::raw_ostream &os,
                            bool includePrefixByte = true);

  /// Return the ObjC runtime property-name/attributes string global for the
  /// given value.
  cir::GlobalOp getObjCPropertyNameGlobal(llvm::StringRef value);

  /// Return the runtime ObjC protocol metadata global for the given protocol.
  cir::GlobalOp getObjCProtocolGlobal(mlir::Location loc,
                                      const ObjCProtocolDecl *pd);

  /// Return the Mach-O ObjC protocol-label global for the given protocol.
  cir::GlobalOp getObjCProtocolLabelGlobal(mlir::Location loc,
                                           const ObjCProtocolDecl *pd);

  /// Return the runtime ObjC protocol-ref global for the given protocol.
  cir::GlobalOp getObjCProtocolRefGlobal(mlir::Location loc,
                                         const ObjCProtocolDecl *pd);

  /// Return the GNUstep2 ObjC category metadata global for the given
  /// category implementation.
  cir::GlobalOp getObjCGNUCategoryGlobal(mlir::Location loc,
                                         const ObjCCategoryImplDecl *cid);
  /// Return the Darwin fragile ObjC category metadata global for the given
  /// category implementation.
  cir::GlobalOp getObjCDarwinFragileCategoryGlobal(
      mlir::Location loc, const ObjCCategoryImplDecl *cid);
  /// Return the Darwin non-fragile ObjC category metadata global for the given
  /// category implementation.
  cir::GlobalOp getObjCDarwinCategoryGlobal(mlir::Location loc,
                                            const ObjCCategoryImplDecl *cid);
  /// Emit synthesized ObjC property accessors for an implementation.
  void emitObjCPropertyImplementations(const ObjCImplementationDecl *oid);
  void emitObjCIvarInitializations(ObjCImplementationDecl *oid);
  /// Record an Objective-C compatibility alias for runtime-specific emission.
  void registerObjCCompatibleAlias(const ObjCCompatibleAliasDecl *oad);
  /// Return the GNUstep2 ObjC class metadata global for the given
  /// implementation.
  cir::GlobalOp getObjCGNUClassGlobal(mlir::Location loc,
                                      const ObjCImplementationDecl *oid);
  /// Return the Darwin non-fragile ObjC class metadata global for the given
  /// implementation.
  cir::GlobalOp getObjCDarwinClassGlobal(mlir::Location loc,
                                         const ObjCImplementationDecl *oid);
  /// Canonical element type of the Darwin non-fragile ObjC ivar-offset global
  /// (`OBJC_IVAR_$_<Class>.<ivar>`). Single source of truth shared by the
  /// ivar-access path (synthesized accessors, method bodies) and the
  /// class-metadata path so both agree on width and signedness. Mirrors OGCG's
  /// `ObjCCommonTypesHelper::IvarOffsetVarTy`: signed `int` (32-bit) on arm64,
  /// signed `long` elsewhere.
  cir::IntType getObjCDarwinIvarOffsetVarType();
  /// Emit the legacy GNU ObjC module registration globals and loader helper.
  void emitObjCLegacyGNUModuleInit(mlir::Location loc);
  /// Return the Darwin fragile ObjC class metadata global for the given
  /// implementation.
  cir::GlobalOp getObjCDarwinFragileClassGlobal(
      mlir::Location loc, const ObjCImplementationDecl *oid);
  /// Return the Darwin non-fragile ObjC exception typeinfo global for the
  /// given interface, optionally materializing the definition.
  cir::GlobalOp getObjCDarwinEHTypeGlobal(mlir::Location loc,
                                          const ObjCInterfaceDecl *id,
                                          bool forDefinition);
  /// Return the Darwin non-fragile ObjC 'id' exception typeinfo global.
  cir::GlobalOp getObjCDarwinIdEHTypeGlobal(mlir::Location loc);
  /// Emit the GNUstep2 module bootstrap globals and loader helper.
  void emitObjCGNUModuleInit(mlir::Location loc);
  /// Emit deferred Darwin fragile ObjC module metadata.
  void emitObjCDarwinFragileModuleInfo(mlir::Location loc);
  /// Emit deferred Darwin non-fragile ObjC module metadata.
  void emitObjCDarwinModuleInfo(mlir::Location loc);

  /// Materialize and load an ObjC protocol object value.
  mlir::Value getObjCProtocol(CIRGenFunction &cgf, const ObjCProtocolDecl *pd,
                              mlir::Location loc);

  /// Return the external ObjC runtime class symbol for the given interface.
  cir::GlobalOp getObjCClassSymbol(mlir::Location loc,
                                   const ObjCInterfaceDecl *id);

  /// Return the runtime class-ref global for the given interface.
  cir::GlobalOp getObjCClassRefGlobal(mlir::Location loc,
                                      const ObjCInterfaceDecl *id);
  /// Return the external ObjC runtime metaclass symbol for the given interface.
  cir::GlobalOp getObjCMetaClassSymbol(mlir::Location loc,
                                       const ObjCInterfaceDecl *id);
  /// Return the Mach-O non-fragile super-ref global for the given interface.
  cir::GlobalOp getObjCSuperClassRefGlobal(mlir::Location loc,
                                           const ObjCInterfaceDecl *id,
                                           bool isClassMessage);

  /// Materialize and load an ObjC class value.
  mlir::Value getObjCClass(CIRGenFunction &cgf, const ObjCInterfaceDecl *id,
                           mlir::Location loc);
  /// Materialize and load a Darwin non-fragile ObjC super target
  /// class/metaclass value.
  mlir::Value getObjCSuperClass(CIRGenFunction &cgf, const ObjCInterfaceDecl *id,
                                mlir::Location loc, bool isClassMessage);

  /// Return the cached AST type for the NSFastEnumeration state record.
  QualType getObjCFastEnumerationStateType();
  /// Return the Darwin objc_enumerationMutation runtime declaration.
  cir::FuncOp getObjCEnumerationMutationFunc();

  /// Return the generic objc_msgSend runtime declaration.
  cir::FuncOp getObjCMsgSendFunc();
  /// Return the objc_msgSend_fpret runtime declaration.
  cir::FuncOp getObjCMsgSendFpretFunc();
  /// Return the objc_msgSend_fp2ret runtime declaration.
  cir::FuncOp getObjCMsgSendFp2retFunc();
  /// Return the objc_msgSend_stret runtime declaration.
  cir::FuncOp getObjCMsgSendStretFunc();
  /// Return the objc_msgSend_stret2 runtime declaration.
  cir::FuncOp getObjCMsgSendStret2Func();
  /// Return the generic Darwin objc_msgSendSuper runtime declaration.
  cir::FuncOp getObjCMsgSendSuperFunc();
  /// Return the Darwin objc_msgSendSuper_stret runtime declaration.
  cir::FuncOp getObjCMsgSendSuperStretFunc();
  /// Return the generic Darwin objc_msgSendSuper2 runtime declaration.
  cir::FuncOp getObjCMsgSendSuper2Func();
  /// Return the Darwin objc_msgSendSuper2_stret runtime declaration.
  cir::FuncOp getObjCMsgSendSuper2StretFunc();
  /// Return the generic GNU objc_msg_lookup runtime declaration.
  cir::FuncOp getObjCMsgLookupFunc();
  /// Return the ObjFW objc_msg_lookup_stret runtime declaration.
  cir::FuncOp getObjCMsgLookupStretFunc();
  /// Return the generic GNU objc_msg_lookup_super runtime declaration.
  cir::FuncOp getObjCMsgLookupSuperFunc();
  /// Return the ObjFW objc_msg_lookup_super_stret runtime declaration.
  cir::FuncOp getObjCMsgLookupSuperStretFunc();
  /// Return the GNUstep objc_msg_lookup_sender runtime declaration.
  cir::FuncOp getObjCMsgLookupSenderFunc();
  /// Return the GNUstep objc_slot_lookup_super runtime declaration.
  cir::FuncOp getObjCSlotLookupSuperFunc();

  /// Cache of emitted constant CFString objects keyed by the canonicalized
  /// literal payload (UTF-8 bytes for ASCII-only strings, UTF-16 byte payload
  /// otherwise), matching OGCG's CFConstantStringMap behavior.
  llvm::StringMap<cir::GlobalOp> constantCFStringMap;

  /// Stable unique base symbol names for compiler-generated block artifacts.
  llvm::DenseMap<const clang::BlockExpr *, std::string> uniqueBlockNames;

  /// Cache of emitted Objective-C selector-name globals keyed by selector
  /// string spelling (for example "format:").
  llvm::StringMap<cir::GlobalOp> objcSelectorNameGlobals;
  /// Cache of emitted Objective-C selector-type globals keyed by selector
  /// type encoding strings.
  llvm::StringMap<cir::GlobalOp> objcSelectorTypeGlobals;
  /// Cache of emitted Objective-C class-name globals keyed by runtime name.
  llvm::StringMap<cir::GlobalOp> objcClassNameGlobals;
  /// Cache of emitted Objective-C method-type globals keyed by type encoding.
  llvm::StringMap<cir::GlobalOp> objcMethodTypeGlobals;
  /// Cache of emitted Objective-C property-name/attribute globals keyed by
  /// their string payload.
  llvm::StringMap<cir::GlobalOp> objcPropertyNameGlobals;

  /// Cache of emitted Mach-O selector-ref globals keyed by selector
  /// string spelling.
  llvm::StringMap<cir::GlobalOp> objcSelectorRefGlobals;
  /// Cache of emitted non-Mach-O Objective-C selector globals keyed by
  /// selector spelling plus type encoding.
  llvm::StringMap<cir::GlobalOp> objcSelectorGlobals;
  /// Cache of emitted Objective-C protocol metadata globals keyed by the
  /// canonical protocol declaration.
  llvm::DenseMap<const ObjCProtocolDecl *, cir::GlobalOp> objcProtocolGlobals;
  /// Cache of emitted Mach-O Objective-C protocol-label globals keyed by the
  /// canonical protocol declaration.
  llvm::DenseMap<const ObjCProtocolDecl *, cir::GlobalOp>
      objcProtocolLabelGlobals;
  /// Cache of emitted Mach-O Objective-C protocol-ref globals keyed by the
  /// canonical protocol declaration.
  llvm::DenseMap<const ObjCProtocolDecl *, cir::GlobalOp>
      objcProtocolRefGlobals;
  /// Cache of emitted Objective-C category metadata globals keyed by the
  /// category implementation declaration.
  llvm::DenseMap<const ObjCCategoryImplDecl *, cir::GlobalOp>
      objcCategoryGlobals;
  /// List of legacy GNU Objective-C class metadata globals that must be
  /// registered through the old objc_module/objc_symtab path.
  llvm::SmallVector<cir::GlobalOp, 8> legacyGNUClassGlobals;
  /// List of legacy GNU Objective-C category metadata globals that must be
  /// registered through the old objc_module/objc_symtab path.
  llvm::SmallVector<cir::GlobalOp, 8> legacyGNUCategoryGlobals;
  /// Legacy GNU Objective-C protocol declarations tracked for deterministic
  /// holder-category emission.
  llvm::SmallVector<const ObjCProtocolDecl *, 8> legacyGNUProtocols;
  /// Cache of legacy GNU constant NSString objects keyed by literal payload.
  llvm::StringMap<cir::GlobalOp> legacyGNUConstantStringMap;
  /// List of legacy GNU constant NSString objects registered through
  /// .objc_statics.
  llvm::SmallVector<cir::GlobalOp, 8> legacyGNUConstantStringGlobals;
  /// Cache of GNUstep2 constant NSString values keyed by literal payload.
  /// Entries may be either global views or tiny tagged-pointer literals.
  llvm::StringMap<mlir::TypedAttr> gnustep2ConstantStringMap;
  struct GNUstep2COFFEarlyInitEntry {
    cir::GlobalOp sourceGlobal;
    cir::GlobalOp destGlobal;
    unsigned destFieldIndex;
  };
  /// COFF GNUstep v2 requires a late CRT callback that patches class and
  /// constant-string metadata to use PE/COFF-safe symbol references.
  llvm::SmallVector<GNUstep2COFFEarlyInitEntry, 8>
      gnustep2COFFEarlyInitEntries;
  /// Legacy GNU Objective-C compatibility aliases keyed as {class name, alias}.
  llvm::SmallVector<std::pair<std::string, std::string>, 4>
      legacyGNUClassAliases;
  /// Darwin fragile ObjC class globals that must appear in the module
  /// objc_symtab at finalization.
  llvm::SmallVector<cir::GlobalOp> objcDarwinFragileDefinedClasses;
  /// Darwin fragile ObjC category globals that must appear in the module
  /// objc_symtab at finalization.
  llvm::SmallVector<cir::GlobalOp> objcDarwinFragileDefinedCategories;
  /// Darwin non-fragile ObjC class globals that must appear in the module
  /// class lists at finalization.
  llvm::SmallVector<cir::GlobalOp> objcDarwinDefinedClasses;
  /// Darwin non-fragile ObjC category globals that must appear in the module
  /// category list at finalization.
  llvm::SmallVector<cir::GlobalOp> objcDarwinDefinedCategories;
  /// Darwin non-fragile ObjC category globals that must appear in the module
  /// stub-category list at finalization.
  llvm::SmallVector<cir::GlobalOp> objcDarwinDefinedStubCategories;
  /// Darwin non-fragile ObjC classes that require eager +load registration.
  llvm::SmallVector<cir::GlobalOp> objcDarwinDefinedNonLazyClasses;
  /// Darwin non-fragile ObjC categories that require eager +load registration.
  llvm::SmallVector<cir::GlobalOp> objcDarwinDefinedNonLazyCategories;
  /// Cache of Darwin ObjC exception typeinfo globals keyed by the canonical
  /// Objective-C interface declaration.
  llvm::DenseMap<const ObjCInterfaceDecl *, cir::GlobalOp>
      objcDarwinEHTypeGlobals;

  /// Cache of emitted Objective-C class-ref globals keyed by the interface.
  llvm::DenseMap<const ObjCInterfaceDecl *, cir::GlobalOp> objcClassRefGlobals;
  /// Cache of emitted Objective-C super-ref globals keyed by the interface for
  /// instance-method super sends.
  llvm::DenseMap<const ObjCInterfaceDecl *, cir::GlobalOp>
      objcSuperClassRefGlobals;
  /// Cache of emitted Objective-C super-ref globals keyed by the interface for
  /// class-method super sends.
  llvm::DenseMap<const ObjCInterfaceDecl *, cir::GlobalOp>
      objcSuperMetaClassRefGlobals;
  /// Cached AST type for the NSFastEnumeration state record layout.
  QualType objCFastEnumerationStateType;

  /// Lazily-created declaration for the generic objc_msgSend messenger.
  cir::FuncOp objcMsgSendFunc;
  /// Lazily-created declaration for the objc_msgSend_fpret messenger.
  cir::FuncOp objcMsgSendFpretFunc;
  /// Lazily-created declaration for the objc_msgSend_fp2ret messenger.
  cir::FuncOp objcMsgSendFp2retFunc;
  /// Lazily-created declaration for the objc_msgSend_stret messenger.
  cir::FuncOp objcMsgSendStretFunc;
  /// Lazily-created declaration for the objc_msgSend_stret2 messenger.
  cir::FuncOp objcMsgSendStret2Func;
  /// Lazily-created declaration for the Darwin fragile super messenger.
  cir::FuncOp objcMsgSendSuperFunc;
  /// Lazily-created declaration for the Darwin fragile super stret messenger.
  cir::FuncOp objcMsgSendSuperStretFunc;
  /// Lazily-created declaration for the generic Darwin super messenger.
  cir::FuncOp objcMsgSendSuper2Func;
  /// Lazily-created declaration for the Darwin super stret messenger.
  cir::FuncOp objcMsgSendSuper2StretFunc;
  /// Lazily-created declaration for the generic GNU lookup messenger.
  cir::FuncOp objcMsgLookupFunc;
  /// Lazily-created declaration for the ObjFW GNU stret lookup messenger.
  cir::FuncOp objcMsgLookupStretFunc;
  /// Lazily-created declaration for the generic GNU super lookup messenger.
  cir::FuncOp objcMsgLookupSuperFunc;
  /// Lazily-created declaration for the ObjFW GNU super stret lookup messenger.
  cir::FuncOp objcMsgLookupSuperStretFunc;
  /// Lazily-created declaration for the GNUstep sender lookup messenger.
  cir::FuncOp objcMsgLookupSenderFunc;
  /// Lazily-created declaration for the GNUstep slot-based super lookup.
  cir::FuncOp objcSlotLookupSuperFunc;
  /// Lazily-created declaration for objc_enumerationMutation.
  cir::FuncOp objcEnumerationMutationFunc;

  /// Lazily materialized runtime class reference global used by constant
  /// CFString objects (for example __CFConstantStringClassReference or the
  /// Swift runtime class symbol).
  cir::GlobalOp cFConstantStringClassRef;

  unsigned compoundLiteralCnt = 0;
  /// Return the unique name for global compound literal.
  std::string createGlobalCompoundLiteralName() {
    return (llvm::Twine(".compoundLiteral.") +
            llvm::Twine(compoundLiteralCnt++))
        .str();
  }

  /// Map used to avoid emitting the same file-scope compound literal twice.
  llvm::DenseMap<const CompoundLiteralExpr *, cir::GlobalOp>
      emittedCompoundLiterals;

  /// Returns the address space for temporary allocations in the language. This
  /// ensures that the allocated variable's address space matches the
  /// expectations of the AST, rather than using the target's allocation address
  /// space, which may lead to type mismatches in other parts of the IR.
  LangAS getLangTempAllocaAddressSpace() const;

  /// Return the address space for constant globals.
  LangAS getGlobalConstantAddressSpace() const;

  /// Determine the address space for a global variable.
  LangAS getGlobalVarAddressSpace(const VarDecl *d);

  /// Return the target memory space used for default globals such as vtables.
  mlir::ptr::MemorySpaceAttrInterface getDefaultGlobalsAddressSpace() const;

  /// Return an i8 pointer in the target's default globals address space.
  cir::PointerType getDefaultGlobalsInt8PtrTy() {
    return builder.getPointerTo(builder.getUInt8Ty(),
                                getDefaultGlobalsAddressSpace());
  }

  /// Set attributes which are common to any form of a global definition (alias,
  /// Objective-C method, function, global variable).
  ///
  /// NOTE: This should only be called for definitions.
  void setCommonAttributes(GlobalDecl gd, mlir::Operation *op);

  const TargetCIRGenInfo &getTargetCIRGenInfo();

  /// Helpers to convert the presumed location of Clang's SourceLocation to an
  /// MLIR Location.
  mlir::Location getLoc(clang::SourceLocation cLoc);
  mlir::Location getLoc(clang::SourceRange cRange);

  /// Return the best known alignment for an unknown pointer to a
  /// particular class.
  clang::CharUnits getClassPointerAlignment(const clang::CXXRecordDecl *rd);

  /// FIXME: this could likely be a common helper and not necessarily related
  /// with codegen.
  clang::CharUnits
  getNaturalTypeAlignment(clang::QualType t, LValueBaseInfo *baseInfo,
                          TBAAAccessInfo *tbaaInfo = nullptr,
                          bool forPointeeType = false);

  /// TODO: Add TBAAAccessInfo
  CharUnits getDynamicOffsetAlignment(CharUnits actualBaseAlign,
                                      const CXXRecordDecl *baseDecl,
                                      CharUnits expectedTargetAlign);

  /// Returns the assumed alignment of a virtual base of a class.
  CharUnits getVBaseAlignment(CharUnits derivedAlign,
                              const CXXRecordDecl *derived,
                              const CXXRecordDecl *vbase);

  cir::FuncOp
  getAddrOfCXXStructor(clang::GlobalDecl gd,
                       const CIRGenFunctionInfo *fnInfo = nullptr,
                       cir::FuncType fnType = nullptr, bool dontDefer = false,
                       ForDefinition_t isForDefinition = NotForDefinition) {
    return getAddrAndTypeOfCXXStructor(gd, fnInfo, fnType, dontDefer,
                                       isForDefinition)
        .second;
  }

  std::pair<cir::FuncType, cir::FuncOp> getAddrAndTypeOfCXXStructor(
      clang::GlobalDecl gd, const CIRGenFunctionInfo *fnInfo = nullptr,
      cir::FuncType fnType = nullptr, bool dontDefer = false,
      ForDefinition_t isForDefinition = NotForDefinition);

  mlir::Type getVTableComponentType();

  /// Return the CIR address space attribute for globals (e.g.,
  /// target_address_space(1) on AMDGCN). Returns null for the default AS.
  mlir::ptr::MemorySpaceAttrInterface getGlobalMemorySpaceAttr();

  CIRGenVTables &getVTables() { return vtables; }

  ItaniumVTableContext &getItaniumVTableContext() {
    return vtables.getItaniumVTableContext();
  }
  const ItaniumVTableContext &getItaniumVTableContext() const {
    return vtables.getItaniumVTableContext();
  }

  /// This contains all the decls which have definitions but which are deferred
  /// for emission and therefore should only be output if they are actually
  /// used. If a decl is in this, then it is known to have not been referenced
  /// yet.
  std::map<llvm::StringRef, clang::GlobalDecl> deferredDecls;

  // This is a list of deferred decls which we have seen that *are* actually
  // referenced. These get code generated when the module is done.
  std::vector<DeferredDeclToEmit> deferredDeclsToEmit;
  llvm::DenseSet<clang::GlobalDecl> deferredDeclsToEmitSet;
  void addDeferredDeclToEmit(clang::GlobalDecl GD) {
    if (deferredDeclsToEmitSet.insert(GD).second)
      deferredDeclsToEmit.emplace_back(DeferredDeclToEmit{GD, getMangledName(GD)});
  }

  /// Multi-version functions that need resolver/ifunc emission at TU
  /// finalization. Mirrors OGCG's MultiVersionFuncs.
  std::vector<clang::GlobalDecl> multiVersionFuncs;
  llvm::StringSet<> deferredResolversToEmit;

  /// Symbols created as extern_weak due to weakref attributes. When the
  /// same symbol is later referenced normally (without weakref), its linkage
  /// is reset to external. Matches OGCG's WeakRefReferences.
  llvm::DenseSet<mlir::Operation *> weakRefReferences;

  /// A queue of (previously-emitted) vtables that we deferred checking for
  /// completeness, and which we will either emit or discard at end of TU.
  std::vector<const clang::CXXRecordDecl *> deferredVTables;

  /// A queue of (optional) vtables that may be emitted opportunistically.
  std::vector<const clang::CXXRecordDecl *> opportunisticVTables;

  /// Inline member/friend functions (linkonce_odr/weak_odr) that we observed
  /// as declaration-only placeholders during normal codegen. release() uses
  /// this targeted set instead of re-scanning all mangledDeclNames.
  llvm::DenseSet<clang::GlobalDecl> deferredInlineMemberCandidates;

  /// Map of materialized global temporaries to their GlobalOps.
  llvm::DenseMap<const clang::Expr *, mlir::Operation *>
      materializedGlobalTemporaryMap;

  bool emitCommonTopLevelDecl(clang::Decl *decl);
  void emitUncommonTopLevelDecl(clang::Decl *decl);
  void emitTopLevelDecl(clang::Decl *decl);

  /// Determine whether the definition must be emitted; if this returns \c
  /// false, the definition can be emitted lazily if it's used.
  bool mustBeEmitted(const clang::ValueDecl *d);

  /// Determine whether the definition can be emitted eagerly, or should be
  /// delayed until the end of the translation unit. This is relevant for
  /// definitions whose linkage can change, e.g. implicit function
  /// instantiations which may later be explicitly instantiated.
  bool mayBeEmittedEagerly(const clang::ValueDecl *d);

  bool verifyModule() const;

  /// Return the address of the given function. If funcType is non-null, then
  /// this function will use the specified type if it has to create it.
  // TODO: this is a bit weird as `GetAddr` given we give back a FuncOp?
  cir::FuncOp
  getAddrOfFunction(clang::GlobalDecl gd, mlir::Type funcType = nullptr,
                    bool forVTable = false, bool dontDefer = false,
                    ForDefinition_t isForDefinition = NotForDefinition);

  /// Get a reference to the target of a weak reference.
  /// Returns either cir.func (function weakref) or cir.global (variable
  /// weakref), matching OGCG's function/global dispatch.
  mlir::Operation *getWeakRefReference(const clang::ValueDecl *vd);

  /// Get the address of a thunk and emit it if necessary.
  cir::FuncOp getAddrOfThunk(llvm::StringRef name, mlir::Type fnTy,
                             clang::GlobalDecl gd);

  mlir::Operation *
  getAddrOfGlobal(clang::GlobalDecl gd,
                  ForDefinition_t isForDefinition = NotForDefinition);

  // Return whether RTTI information should be emitted for this target.
  bool shouldEmitRTTI(bool forEH = false) {
    return (forEH || getLangOpts().RTTI) &&
           (!getLangOpts().isTargetDevice() || !getTriple().isGPU());
  }

  /// Emit type info if type of an expression is a variably modified
  /// type. Also emit proper debug info for cast types.
  void emitExplicitCastExprType(const ExplicitCastExpr *e,
                                CIRGenFunction *cgf = nullptr);

  /// Emit code for a single global function or variable declaration. Forward
  /// declarations are emitted lazily.
  void emitGlobal(clang::GlobalDecl gd);

  void emitAliasForGlobal(llvm::StringRef mangledName, mlir::Operation *op,
                          GlobalDecl aliasGD, cir::FuncOp aliasee,
                          cir::GlobalLinkageKind linkage);

  mlir::Type convertType(clang::QualType type);

  /// Set the visibility for the given global.
  void setGlobalVisibility(mlir::Operation *op, const NamedDecl *d) const;
  void setDSOLocal(mlir::Operation *op) const;
  void setDSOLocal(cir::CIRGlobalValueInterface gv) const;

  /// Set visibility, dllimport/dllexport and dso_local.
  /// This must be called after dllimport/dllexport is set.
  void setGVProperties(mlir::Operation *op, const NamedDecl *d) const;
  void setGVPropertiesAux(mlir::Operation *op, const NamedDecl *d) const;

  /// Set TLS mode for the given operation based on the given variable
  /// declaration.
  void setTLSMode(mlir::Operation *op, const VarDecl &d);

  /// Get TLS mode from CodeGenOptions.
  cir::TLS_Model getDefaultCIRTLSModel() const;

  /// Compute the KCFI type identifier hash for a function type.
  uint32_t createKCFITypeId(QualType T, llvm::StringRef Salt = {});

  /// Compute the UBSan function type hash for a function type.
  uint32_t createUBSanFunctionTypeHash(QualType T);

  /// Return the UBSan function prologue signature for \p FD, or std::nullopt
  /// when the target or function shape is unsupported.
  std::optional<uint32_t> getUBSanFunctionSignature(const FunctionDecl *FD);

  /// Set KCFI type metadata on a CIR function.
  void setCIRKCFIType(const FunctionDecl *FD, cir::FuncOp F);

  /// Return the decl-specific pointer-auth discriminator for the given decl.
  uint16_t getPointerAuthDeclDiscriminator(clang::GlobalDecl declaration);

  /// Return the member-function-pointer ptrauth schema info for the given
  /// member pointer type, if any.
  std::optional<CIRGenPointerAuthInfo>
  getMemberFunctionPointerAuthInfo(clang::QualType memberPointerType) const;

  /// Whether a type has hidden LTO visibility, i.e. CFI checks can be
  /// applied to it.
  /// Ported from OGCG CGVTables.cpp:HasHiddenLTOVisibility.
  bool hasHiddenLTOVisibility(const CXXRecordDecl *RD);

  /// Whether a type always has LTO visibility public.
  /// Ported from OGCG CGVTables.cpp:AlwaysHasLTOVisibilityPublic.
  bool alwaysHasLTOVisibilityPublic(const CXXRecordDecl *RD);

  /// Return the set of most-base classes for CFI member function pointer
  /// type tests.
  /// Ported from OGCG CodeGenModule.cpp:getMostBaseClasses.
  llvm::SmallVector<const CXXRecordDecl *, 0>
  getMostBaseClasses(const CXXRecordDecl *RD);

  /// Create a metadata identifier string for a type (for CFI type tests).
  /// Returns empty string for non-externally-visible types (distinct MDNode
  /// case). Ported from OGCG CreateMetadataIdentifierForType.
  std::string createMetadataIdentifierStringForType(QualType T);

  /// Create a stable metadata identifier key for a type even when it is not
  /// externally visible. CIR uses this to preserve hidden type identity until
  /// final LLVM lowering can rebuild distinct metadata nodes.
  std::string createStableMetadataIdentifierKeyForType(QualType T);

  /// Same as above but for virtual member pointer types (appends ".virtual"
  /// suffix). Ported from OGCG CreateMetadataIdentifierForVirtualMemPtrType.
  std::string createMetadataIdentifierStringForVirtualMemPtrType(QualType T);

  /// Create a stable metadata identifier key for a virtual member pointer type
  /// even when it is not externally visible. CIR uses this to preserve hidden
  /// type identity until final LLVM lowering can rebuild distinct metadata
  /// nodes.
  std::string createStableMetadataIdentifierKeyForVirtualMemPtrType(QualType T);

  /// Set function attributes for a function declaration.
  void setFunctionAttributes(GlobalDecl gd, cir::FuncOp f,
                             bool isIncompleteFunction, bool isThunk);

  /// Set the CIR function attributes (Sext, zext, etc).
  void setCIRFunctionAttributes(GlobalDecl gd, const CIRGenFunctionInfo &info,
                                cir::FuncOp func, bool isThunk);

  /// Set OGCG-style attributes on synthesized internal function definitions
  /// that still have decl-backed ABI information, such as block invokes.
  void setInternalFunctionAttributes(GlobalDecl gd, cir::FuncOp func,
                                     const CIRGenFunctionInfo &info,
                                     bool isThunk = false);
  /// Collect the full OGCG-style default attribute bundle for compiler-
  /// generated internal init/cleanup helpers (for example `_GLOBAL__sub_I_`
  /// and `__dtor_*` stubs).
  void getDefaultInternalFunctionAttributes(mlir::NamedAttrList &funcAttrs);

  /// Give a compiler-generated function with no Decl the default attributes
  /// (no-trapping-math, frame-pointer, target CPU and features, ...), matching
  /// OGCG's SetLLVMFunctionAttributes(GlobalDecl(), ...). Used for TLS wrappers.
  void setDefaultFunctionAttributesForCompilerGenerated(cir::FuncOp fn);

  /// Set extra attributes (inline, etc.) for a function.
  void setCIRFunctionAttributesForDefinition(clang::GlobalDecl gd, cir::FuncOp f,
                                             bool isThunk = false);
  /// Match OGCG's markAsIgnoreThreadCheckingAtRuntime helper by replacing
  /// sanitize_thread with sanitize_thread_no_checking_at_run_time.
  void markAsIgnoreThreadCheckingAtRuntime(cir::FuncOp fn);
  /// Attach CFI icall !type metadata entries on a function declaration or
  /// definition, matching OGCG createFunctionTypeMetadataForIcall.
  void addFunctionTypeMetadataForIcall(const clang::FunctionDecl *fd,
                                       cir::FuncOp func);
  /// Attach call-graph-section !type metadata entries on a function
  /// declaration or definition, matching OGCG createIndirectFunctionTypeMD.
  void addFunctionTypeMetadataForCallGraphSection(const clang::FunctionDecl *fd,
                                                  cir::FuncOp func);

  void emitGlobalDefinition(clang::GlobalDecl gd,
                            mlir::Operation *op = nullptr);
  void emitGlobalFunctionDefinition(clang::GlobalDecl gd, mlir::Operation *op);
  void emitMultiVersionFunctionDefinition(clang::GlobalDecl gd,
                                          mlir::Operation *op);
  void emitAliasDefinition(clang::GlobalDecl gd);
  void addDeferredMultiVersionResolverToEmit(clang::GlobalDecl gd);
  void updateMultiVersionNames(clang::GlobalDecl gd,
                               const clang::FunctionDecl *fd,
                               llvm::StringRef &mangledName);

  /// Create or return the ifunc/resolver for a multi-versioned function.
  /// Mirrors OGCG's GetOrCreateMultiVersionResolver.
  cir::FuncOp getOrCreateMultiVersionResolver(clang::GlobalDecl gd);

  /// Emit the resolver/ifunc/alias for a cpu_dispatch multiversion function.
  /// Mirrors OGCG's emitCPUDispatchDefinition.
  void emitCPUDispatchDefinition(clang::GlobalDecl gd);

  /// Emit the deferred AArch64 resolver/ifunc path for target_version and
  /// target_clones declarations. Returns true when the declaration was handled
  /// by the AArch64-specific path.
  bool emitAArch64MultiVersionFunction(clang::GlobalDecl gd,
                                       const clang::FunctionDecl *fd);

  /// Emit all deferred multi-version resolvers. Called from release().
  /// Mirrors OGCG's emitMultiVersionFunctions.
  void emitMultiVersionFunctions();
  void emitGlobalVarDefinition(const clang::VarDecl *vd,
                               bool isTentative = false);

  /// Emit the function that initializes the specified global
  void emitCXXGlobalVarDeclInit(const VarDecl *varDecl, cir::GlobalOp addr,
                                bool performInit);

  void emitCXXGlobalVarDeclInitFunc(const VarDecl *vd, cir::GlobalOp addr,
                                    bool performInit);

  void emitGlobalOpenACCDecl(const clang::OpenACCConstructDecl *cd);
  void emitGlobalOpenACCRoutineDecl(const clang::OpenACCRoutineDecl *cd);
  void emitGlobalOpenACCDeclareDecl(const clang::OpenACCDeclareDecl *cd);
  template <typename BeforeOpTy, typename DataClauseTy>
  void emitGlobalOpenACCDeclareDataOperands(const Expr *varOperand,
                                            DataClauseTy dataClause,
                                            OpenACCModifierKind modifiers,
                                            bool structured, bool implicit,
                                            bool requiresDtor);
  // Each of the acc.routine operations must have a unique name, so we just use
  // an integer counter.  This is how Flang does it, so it seems reasonable.
  unsigned routineCounter = 0;

  /// Counter for -ftrivial-auto-var-init-stop-after=N. Tracks how many
  /// auto variables have been trivially initialized so far.
  unsigned numAutoVarInit = 0;

  /// Check if auto-init should stop. Returns true if the stop-after counter
  /// has been exceeded. Matches OGCG CodeGenModule::stopAutoInit.
  bool stopAutoInit();

  void emitOpenACCRoutineDecl(const clang::FunctionDecl *funcDecl,
                              cir::FuncOp func, SourceLocation pragmaLoc,
                              ArrayRef<const OpenACCClause *> clauses);

  void emitOMPThreadPrivateDecl(const OMPThreadPrivateDecl *d);
  void emitOMPGroupPrivateDecl(const OMPGroupPrivateDecl *d);
  void emitOMPCapturedExpr(const OMPCapturedExprDecl *d);
  void emitOMPAllocateDecl(const OMPAllocateDecl *d);
  void emitOMPDeclareReduction(const OMPDeclareReductionDecl *d);
  void emitOMPDeclareMapper(const OMPDeclareMapperDecl *d);

  /// Marks function \p fn with properly mangled Vector Function ABI variant
  /// names for each `#pragma omp declare simd` attribute on \p fd, mirroring
  /// classic CodeGen's CGOpenMPRuntime::emitDeclareSimdFunction.
  void emitOMPDeclareSimdFunction(const clang::FunctionDecl *fd,
                                  cir::FuncOp fn);
  void emitOpenMPTargetDeviceStub(llvm::StringRef parentName,
                                  const OMPExecutableDirective &d);
  mlir::FlatSymbolRefAttr
  getOrCreateOpenMPDeclareMapper(const OMPDeclareMapperDecl *d,
                                 mlir::Location loc);
  mlir::FlatSymbolRefAttr
  getOrCreateOpenMPCriticalDecl(llvm::StringRef criticalName,
                                std::optional<uint64_t> hint,
                                mlir::Location loc);
  void emitOMPRequiresDecl(const OMPRequiresDecl *d);
  bool shouldSkipOpenMPTargetDeviceEmission(GlobalDecl gd);
  bool markAsOpenMPTargetGlobal(GlobalDecl gd);
  void markAsOpenMPTargetGenericFunction(const FunctionDecl *fd);
  bool shouldUseOpenMPTargetSharedLocals(const FunctionDecl *fd) const;
  bool isEmittingOpenMPTargetGenericFunction() const;
  void pushOpenMPTargetGenericEmission();
  void popOpenMPTargetGenericEmission();
  void processOMPRequiresDecl(const OMPRequiresDecl *d);
  void emitPragmaCommentDecl(const PragmaCommentDecl *decl);
  void emitPragmaDetectMismatchDecl(const PragmaDetectMismatchDecl *decl);
  std::string getDependentLibraryOption(llvm::StringRef lib) const;
  std::optional<std::string> getDetectMismatchOption(llvm::StringRef name,
                                                     llvm::StringRef value) const;

  // C++ related functions.
  void emitDeclContext(const DeclContext *dc);

  /// Return the result of value-initializing the given type, i.e. a null
  /// expression of the given type.
  mlir::Value emitNullConstant(QualType t, mlir::Location loc);

  mlir::TypedAttr emitNullConstantAttr(QualType t);

  /// Return a null constant appropriate for zero-initializing a base class with
  /// the given type. This is usually, but not always, an LLVM null constant.
  mlir::TypedAttr emitNullConstantForBase(const CXXRecordDecl *record);

  void maybeScanOpenMPTargetDeviceDecl(GlobalDecl gd);
  void scanForOpenMPTargetRegions(const Stmt *s, llvm::StringRef parentName);

  mlir::Value emitMemberPointerConstant(const UnaryOperator *e);

  /// Returns the canonical declaration key used for mangled-name caching.
  /// ABIs without constructor variants share the complete-constructor mangling
  /// between base and complete constructors, matching OGCG.
  static clang::GlobalDecl
  getCanonicalDeclForMangledName(clang::GlobalDecl gd,
                                 const clang::TargetInfo &target);

  llvm::StringRef getMangledName(clang::GlobalDecl gd);
  void
  forEachMangledDecl(llvm::function_ref<void(llvm::StringRef, clang::GlobalDecl)>
                         Callback) const;
  // This function is to support the OpenACC 'bind' clause, which names an
  // alternate name for the function to be called by. This function mangles
  // `attachedFunction` as-if its name was actually `bindName` (that is, with
  // the same signature).  It has some additional complications, as the 'bind'
  // target is always going to be a global function, so member functions need an
  // explicit instead of implicit 'this' parameter, and thus gets mangled
  // differently.
  std::string getOpenACCBindMangledName(const IdentifierInfo *bindName,
                                        const FunctionDecl *attachedFunction);

  void emitTentativeDefinition(const VarDecl *d);

  // Make sure that this type is translated.
  void updateCompletedType(const clang::TagDecl *td);

  /// Try to emit a base destructor as an alias to its primary
  /// base-class destructor.
  bool tryEmitBaseDestructorAsAlias(const CXXDestructorDecl *d);

  // Produce code for this constructor/destructor. This method doesn't try to
  // apply any ABI rules about which other constructors/destructors are needed
  // or if they are alias to each other.
  cir::FuncOp codegenCXXStructor(clang::GlobalDecl gd);

  bool supportsCOMDAT() const;
  void maybeSetTrivialComdat(const clang::Decl &d, mlir::Operation *op);

  static void setInitializer(cir::GlobalOp &op, mlir::Attribute value);

  void replaceUsesOfNonProtoTypeWithRealFunction(mlir::Operation *old,
                                                 cir::FuncOp newFn);

  cir::FuncOp
  getOrCreateCIRFunction(llvm::StringRef mangledName, mlir::Type funcType,
                         clang::GlobalDecl gd, bool forVTable,
                         bool dontDefer = false, bool isThunk = false,
                         ForDefinition_t isForDefinition = NotForDefinition,
                         mlir::ArrayAttr extraAttrs = {});

  cir::FuncOp createCIRFunction(mlir::Location loc, llvm::StringRef name,
                                cir::FuncType funcType,
                                const clang::FunctionDecl *funcDecl);

  /// Create a CIR function with builtin attribute set.
  cir::FuncOp createCIRBuiltinFunction(mlir::Location loc, llvm::StringRef name,
                                       cir::FuncType ty,
                                       const clang::FunctionDecl *fd);

  /// Mark the function as a special member (e.g. constructor, destructor)
  void setCXXSpecialMemberAttr(cir::FuncOp funcOp,
                               const clang::FunctionDecl *funcDecl);

  // Runtime helpers are always plain external declarations; they must never
  // adopt a definition deferred in this TU (e.g. a libc __gnu_inline body whose
  // name collides with the helper), which the deferred path would pull into
  // emission and let the inliner interpose (see #216 nvptx vprintf->vfprintf).
  // Default dontDefer=true mirrors classic CodeGen's non-deferring
  // CreateRuntimeFunction so no future runtime-helper site can regress.
  cir::FuncOp createRuntimeFunction(cir::FuncType ty, llvm::StringRef name,
                                    mlir::ArrayAttr = {}, bool isLocal = false,
                                    bool assumeConvergent = false,
                                    bool dontDefer = true);

  void maybeSetDisableTailCallsForBlockInvoke(cir::FuncOp fn,
                                              const clang::BlockDecl *blockDecl);

  static constexpr const char *builtinCoroId = "__builtin_coro_id";
  static constexpr const char *builtinCoroAlloc = "__builtin_coro_alloc";
  static constexpr const char *builtinCoroBegin = "__builtin_coro_begin";
  static constexpr const char *builtinCoroEnd = "__builtin_coro_end";
  static constexpr const char *builtinCoroIsInRamp =
      "__builtin_coro_is_in_ramp";
  static constexpr const char *builtinCoroFree = "__builtin_coro_free";
  static constexpr const char *builtinCoroPromise = "__builtin_coro_promise";
  static constexpr const char *builtinCoroResume = "__builtin_coro_resume";
  static constexpr const char *builtinCoroNoop = "__builtin_coro_noop";
  static constexpr const char *builtinCoroDestroy = "__builtin_coro_destroy";
  static constexpr const char *builtinCoroDone = "__builtin_coro_done";
  static constexpr const char *builtinCoroSuspend = "__builtin_coro_suspend";
  static constexpr const char *builtinCoroSize = "__builtin_coro_size";
  static constexpr const char *builtinCoroAlign = "__builtin_coro_align";
  static constexpr const char *builtinCoroAwaitSuspendVoid =
      "__builtin_coro_await_suspend_void";
  static constexpr const char *builtinCoroAwaitSuspendBool =
      "__builtin_coro_await_suspend_bool";
  static constexpr const char *builtinCoroAwaitSuspendHandle =
      "__builtin_coro_await_suspend_handle";

  /// Given a builtin id for a function like "__builtin_fabsf", return a
  /// Function* for "fabsf".
  cir::FuncOp getBuiltinLibFunction(const FunctionDecl *fd, unsigned builtinID);

  mlir::IntegerAttr getSize(CharUnits size) {
    return builder.getSizeFromCharUnits(size);
  }

  /// Emit any needed decls for which code generation was deferred.
  void emitDeferred(unsigned recursionLimit);

  /// Helper for `emitDeferred` to apply actual codegen.
  void emitGlobalDecl(const clang::GlobalDecl &d,
                      llvm::StringRef precomputedMangledName = {});

  const llvm::Triple &getTriple() const { return target.getTriple(); }

  // Finalize CIR code generation.
  void release();

  /// Set the Sema instance for on-demand template instantiation.
  void setSema(clang::Sema &s) { sema = &s; }

  /// Emit all the global annotations.
  void emitGlobalAnnotations();

  /// Emit additional args of the annotation.
  mlir::ArrayAttr emitAnnotationArgs(const clang::AnnotateAttr *attr);

  /// Create an AnnotationAttr for the given annotate attribute.
  cir::AnnotationAttr emitAnnotateAttr(const clang::AnnotateAttr *aa,
                                       clang::SourceLocation declLoc = {});

  /// Add global annotations for a global value.
  void addGlobalAnnotations(const ValueDecl *d, mlir::Operation *gv);

  uint64_t getPointerAuthOtherDiscriminator(
      const clang::PointerAuthSchema &schema, clang::GlobalDecl decl,
      clang::QualType ty) const;
  bool shouldSignPointer(const clang::PointerAuthSchema &schema) const;
  std::optional<clang::PointerAuthQualifier>
  computeVTPointerAuthentication(const clang::CXXRecordDecl *record) const;

  std::optional<CIRGenPointerAuthInfo>
  getFunctionPointerAuthInfo(clang::QualType ty) const;

  /// -------
  /// Visibility and Linkage
  /// -------

  static mlir::SymbolTable::Visibility
  getMLIRVisibilityFromCIRLinkage(cir::GlobalLinkageKind GLK);
  static cir::VisibilityKind getGlobalVisibilityKindFromClangVisibility(
      clang::VisibilityAttr::VisibilityType visibility);
  cir::VisibilityAttr getGlobalVisibilityAttrFromDecl(const Decl *decl);
  cir::GlobalLinkageKind getFunctionLinkage(GlobalDecl gd);
  bool isTriviallyRecursive(const FunctionDecl *fd);
  bool shouldEmitFunction(GlobalDecl gd);
  static mlir::SymbolTable::Visibility getMLIRVisibility(cir::GlobalOp op);
  cir::GlobalLinkageKind getCIRLinkageForDeclarator(const DeclaratorDecl *dd,
                                                    GVALinkage linkage,
                                                    bool isConstantVariable);
  void setFunctionLinkage(GlobalDecl gd, cir::FuncOp f) {
    cir::GlobalLinkageKind l = getFunctionLinkage(gd);
    f.setLinkageAttr(cir::GlobalLinkageKindAttr::get(&getMLIRContext(), l));
    mlir::SymbolTable::setSymbolVisibility(f,
                                           getMLIRVisibilityFromCIRLinkage(l));
  }

  cir::GlobalLinkageKind getCIRLinkageVarDefinition(const VarDecl *vd,
                                                    bool isConstant);

  void addReplacement(llvm::StringRef name, llvm::StringRef replacementName);

  /// Helpers to emit "not yet implemented" error diagnostics
  DiagnosticBuilder errorNYI(SourceLocation, llvm::StringRef);

  template <typename T>
  DiagnosticBuilder errorNYI(SourceLocation loc, llvm::StringRef feature,
                             const T &name) {
    unsigned diagID =
        diags.getCustomDiagID(DiagnosticsEngine::Error,
                              "ClangIR code gen Not Yet Implemented: %0: %1");
    return diags.Report(astContext.getFullLoc(loc), diagID) << feature << name;
  }

  DiagnosticBuilder errorNYI(mlir::Location loc, llvm::StringRef feature) {
    // TODO: Convert the location to a SourceLocation
    unsigned diagID = diags.getCustomDiagID(
        DiagnosticsEngine::Error, "ClangIR code gen Not Yet Implemented: %0");
    return diags.Report(diagID) << feature;
  }

  DiagnosticBuilder errorNYI(llvm::StringRef feature) const {
    unsigned diagID = diags.getCustomDiagID(
        DiagnosticsEngine::Error, "ClangIR code gen Not Yet Implemented: %0");
    return diags.Report(diagID) << feature;
  }

  DiagnosticBuilder errorNYI(SourceRange, llvm::StringRef);

  template <typename T>
  DiagnosticBuilder errorNYI(SourceRange loc, llvm::StringRef feature,
                             const T &name) {
    return errorNYI(loc.getBegin(), feature, name) << loc;
  }

  /// Emit a general error that something can't be done.
  void error(SourceLocation loc, llvm::StringRef error);

  /// Determine whether the given type has padding bytes that need to be
  /// zero-initialized for atomic operations.
  bool isPaddedAtomicType(QualType type);
  bool isPaddedAtomicType(const AtomicType *type);

  /// Print out an error that codegen doesn't support the specified stmt yet.
  void errorUnsupported(const Stmt *s, llvm::StringRef type);

  /// Print out an error that codegen doesn't support the specified decl yet.
  void errorUnsupported(const Decl *d, llvm::StringRef type);

  /// OpenCL v1.2 s5.6.4.6 allows the compiler to store kernel argument
  /// information in the program executable. The argument information stored
  /// includes the argument name, its type, the address and access qualifiers
  /// used. This helper can be used to generate metadata for source code kernel
  /// function as well as generated implicitly kernels. If a kernel is generated
  /// implicitly null value has to be passed to the last two parameters,
  /// otherwise all parameters must have valid non-null values.
  /// \param fn is a pointer to IR function being generated.
  /// \param fd is a pointer to function declaration if any.
  /// \param cgf is a pointer to CIRGenFunction that generates this function.
  void buildOpenCLMetadata();

  void genKernelArgMetadata(cir::FuncOp fn, const FunctionDecl *fd = nullptr,
                            CIRGenFunction *cgf = nullptr);

  template <typename Op>
  void decorateOperationWithTBAA(Op op, TBAAAccessInfo tbaaInfo) {
    if (auto tag = getTBAAAccessTagInfo(tbaaInfo)) {
      op.setTbaaAttr(tag);
    }
  }

  cir::TBAAAttr getTBAATypeInfo(QualType qTy);

  /// Get TBAA information that describes an access to an object of the given
  /// type.
  TBAAAccessInfo getTBAAAccessInfo(QualType accessType);

  /// Get the TBAA information that describes an access to a virtual table
  /// pointer.
  TBAAAccessInfo getTBAAVTablePtrAccessInfo(mlir::Type vTablePtrType);

  mlir::ArrayAttr getTBAAStructInfo(QualType qTy);

  /// Get attribute that describes the given base access type. Return null if
  /// type is not suitable for use in TBAA access tags.
  cir::TBAAAttr getTBAABaseTypeInfo(QualType qTy);

  cir::TBAAAttr getTBAAAccessTagInfo(TBAAAccessInfo tbaaInfo);

  /// Get merged TBAA information for the purposes of type casts.
  TBAAAccessInfo mergeTBAAInfoForCast(TBAAAccessInfo sourceInfo,
                                      TBAAAccessInfo targetInfo);

  /// Get merged TBAA information for the purposes of conditional operator.
  TBAAAccessInfo mergeTBAAInfoForConditionalOperator(TBAAAccessInfo infoA,
                                                     TBAAAccessInfo infoB);

  TBAAAccessInfo getTBAAInfoForSubobject(LValue base, QualType accessType) {
    if (base.getTBAAInfo().isMayAlias())
      return TBAAAccessInfo::getMayAliasInfo();
    return getTBAAAccessInfo(accessType);
  }

  void maybeRecordAddressSanitizerGlobalMetadata(SourceLocation loc,
                                                 llvm::StringRef globalName,
                                                 QualType ty,
                                                 SanitizerMask noSanitizeMask,
                                                 bool isDynInit,
                                                 cir::GlobalOp gv);
  void maybeRecordAddressSanitizerGlobalMetadata(const VarDecl *vd,
                                                 bool isDynInit,
                                                 cir::GlobalOp gv);
  bool isInNoSanitizeList(SanitizerMask kind, llvm::StringRef functionName,
                          SourceLocation loc) const;
  bool isInNoSanitizeList(SanitizerMask kind, llvm::StringRef globalName,
                          SourceLocation loc, QualType ty,
                          llvm::StringRef category = {}) const;

  /// Record MemTag/TypeSan global metadata on the CIR global so direct LLVM
  /// lowering can later materialize OGCG-visible global sanitizer metadata.
  void maybeRecordTypeSanGlobalMetadata(SourceLocation loc,
                                        llvm::StringRef globalName, QualType ty,
                                        SanitizerMask noSanitizeMask,
                                        cir::GlobalOp gv);
  void maybeRecordTypeSanGlobalMetadata(const VarDecl *vd, cir::GlobalOp gv);

private:
  bool isDeferredInlineMemberCandidate(clang::GlobalDecl gd,
                                       llvm::StringRef mangledName);
  void maybeTrackDeferredInlineMemberCandidate(clang::GlobalDecl gd,
                                               llvm::StringRef mangledName);

  llvm::DenseMap<int64_t, mlir::IntegerAttr> i64AttrCache;
  llvm::DenseMap<unsigned, mlir::StringAttr> uintStringAttrCache;
  // An ordered map of canonical GlobalDecls to their mangled names.
  llvm::MapVector<clang::GlobalDecl, llvm::StringRef> mangledDeclNames;
  llvm::StringMap<clang::GlobalDecl, llvm::BumpPtrAllocator> manglings;
  // Candidate declarations for end-of-TU inline-member deferred emission in
  // release(). Collected incrementally to avoid full mangledDeclNames scans.
  llvm::DenseSet<clang::GlobalDecl> deferredInlineMemberDeclCandidateSet;
  std::vector<std::pair<clang::GlobalDecl, llvm::StringRef>>
      deferredInlineMemberDeclCandidates;
  // Candidate declarations for end-of-TU implicit-template deferred
  // instantiation retries in release(). We only record declarations where
  // eager Sema instantiation was attempted but could not produce a body.
  llvm::DenseSet<clang::GlobalDecl> deferredImplicitTemplateRetrySet;
  std::vector<std::pair<clang::GlobalDecl, llvm::StringRef>>
      deferredImplicitTemplateRetryCandidates;
  mutable llvm::DenseMap<clang::GlobalDecl, uint16_t>
      ptrAuthDiscriminatorHashes;

  // Maps a mangled name to the mangled name of its replacement (aliasee).
  // We store names instead of raw Operation* pointers because operations
  // can be erased and recreated during deferred emission, invalidating
  // any raw pointers stored earlier during code generation.
  llvm::StringMap<std::string> replacements;
  /// Call replaceAllUsesWith on all pairs in replacements.
  void applyReplacements();
  void indexFunctionSymbolUsers(cir::FuncOp fn);
  void forEachPotentialFunctionUserOfSymbol(
      mlir::StringAttr targetNameAttr,
      llvm::function_ref<void(cir::FuncOp)> visitor);
  void collectTopLevelGetGlobalUsers(
      mlir::StringAttr targetNameAttr,
      llvm::SmallVectorImpl<cir::GetGlobalOp> &getGlobalUsers);

  /// A helper function to replace all uses of OldF to NewF that replace
  /// the type of pointer arguments. This is not needed to tradtional
  /// pipeline since LLVM has opaque pointers but CIR not.
  void replacePointerTypeArgs(cir::FuncOp oldF, cir::FuncOp newF,
                              ArrayRef<mlir::Operation *> users);
  void replacePointerTypeArgs(cir::FuncOp oldF, cir::FuncOp newF,
                              ArrayRef<cir::CallOp> callUsers,
                              ArrayRef<cir::GetGlobalOp> getGlobalUsers);

  void setNonAliasAttributes(GlobalDecl gd, mlir::Operation *op);
  void recordDeferredInlineMemberCandidate(clang::GlobalDecl gd,
                                           llvm::StringRef mangledName);
  void recordDeferredImplicitTemplateRetryCandidate(clang::GlobalDecl gd,
                                                    llvm::StringRef mangledName);

  /// Map source language used to a CIR attribute.
  std::optional<cir::SourceLanguage> getCIRSourceLanguage() const;
};
} // namespace CIRGen

} // namespace clang

#endif // LLVM_CLANG_LIB_CIR_CODEGEN_CIRGENMODULE_H
