//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Internal per-function state used for AST-to-ClangIR code gen
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_LIB_CIR_CODEGEN_CIRGENFUNCTION_H
#define CLANG_LIB_CIR_CODEGEN_CIRGENFUNCTION_H

#include "CIRGenBuilder.h"
#include "CIRGenCall.h"
#include "CIRGenModule.h"
#include "CIRGenPGO.h"
#include "CIRGenTypeCache.h"
#include "CIRGenValue.h"
#include "CIRVarBypassDetector.h"
#include "EHScopeStack.h"

#include "Address.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/BaseSubobject.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/CurrentSourceLocExprScope.h"
#include "clang/AST/Decl.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/MissingFeatures.h"
#include "clang/CIR/TypeEvaluationKind.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ScopedHashTable.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"

#include <string>

namespace {
class ScalarExprEmitter;
} // namespace

namespace mlir {
class Operation;
namespace acc {
class LoopOp;
} // namespace acc
} // namespace mlir

namespace clang {
class ObjCEncodeExpr;
class ObjCIvarRefExpr;
class OMPClause;
class OMPExecutableDirective;
class OMPLoopDirective;
namespace analyze_os_log {
class OSLogBufferLayout;
}
}

namespace clang::CIRGen {

struct CGCoroData;
class EHCleanupScope;

enum class CFISanitizerStatKind : unsigned {
  VCall = 0,
  NVCall = 1,
  DerivedCast = 2,
  UnrelatedCast = 3,
  ICall = 4,
};

class CIRGenFunction : public CIRGenTypeCache {
public:
  struct LexicalScope;

  class SanitizerScope {
    CIRGenFunction *cgf;

  public:
    explicit SanitizerScope(CIRGenFunction *cgf) : cgf(cgf) {
      assert(cgf && "sanitizer scope requires CIRGenFunction");
      assert(!cgf->isSanitizerScope && "nested sanitizer scope");
      cgf->isSanitizerScope = true;
    }

    ~SanitizerScope() { cgf->isSanitizerScope = false; }
  };

  CIRGenModule &cgm;

private:
  friend class ::ScalarExprEmitter;
  /// The builder is a helper class to create IR inside a function. The
  /// builder is stateful, in particular it keeps an "insertion point": this
  /// is where the next operations will be introduced.
  CIRGenBuilderTy &builder;

  /// Per-function PGO state for instrumentation-based profiling.
  std::unique_ptr<CIRGenPGO> PGO;

  /// Cache SourceLocation-to-MLIR location lookups for this function.
  /// CIRGen hot paths repeatedly ask for the same source locations while
  /// emitting many operations; caching avoids redundant SourceManager queries
  /// and FileLineColLoc construction.
  llvm::DenseMap<unsigned, mlir::Location> sourceLocCache;

  /// Cache filename StringAttr values by SourceManager-owned filename pointer.
  /// This avoids repeated StringAttr uniquing/hash/compare work for the same
  /// file path while materializing many FileLineColLoc instances.
  llvm::DenseMap<const char *, mlir::StringAttr> sourceFileAttrCache;

  /// Cache SourceRange-to-MLIR location lookups for this function.
  /// Many AST-to-CIRGen paths repeatedly request fused begin/end range
  /// locations for the same ranges; caching avoids redundant FusedLoc
  /// materialization in hot expression/statement emission.
  llvm::DenseMap<uint64_t, mlir::Location> sourceRangeLocCache;

  /// Cache recursive statement property queries used in constant-folded
  /// control-flow collection to avoid rescanning identical AST subtrees.
  struct LabelCacheKey {
    const clang::Stmt *stmt = nullptr;
    bool ignoreCaseStmts = false;

    bool operator==(const LabelCacheKey &rhs) const {
      return stmt == rhs.stmt && ignoreCaseStmts == rhs.ignoreCaseStmts;
    }
  };
  struct LabelCacheKeyInfo {
    static inline LabelCacheKey getEmptyKey() {
      return {llvm::DenseMapInfo<const clang::Stmt *>::getEmptyKey(), false};
    }
    static inline LabelCacheKey getTombstoneKey() {
      return {llvm::DenseMapInfo<const clang::Stmt *>::getTombstoneKey(),
              false};
    }
    static unsigned getHashValue(const LabelCacheKey &key) {
      return llvm::hash_combine(key.stmt, key.ignoreCaseStmts);
    }
    static bool isEqual(const LabelCacheKey &lhs, const LabelCacheKey &rhs) {
      return lhs == rhs;
    }
  };
  llvm::DenseMap<LabelCacheKey, bool, LabelCacheKeyInfo> containsLabelCache;
  llvm::DenseMap<const clang::Stmt *, bool> containsBreakCache;
  llvm::DenseMap<const clang::Stmt *, bool> mightAddDeclToScopeCache;

  struct SourceAtomInfo {
    uint64_t nextAtom = 1;
    uint64_t highestEmittedAtom = 0;
    uint64_t currentAtom = 0;
  };
  SourceAtomInfo sourceAtomInfo;

public:
  class ApplySourceAtomGroup {
    CIRGenFunction *cgf = nullptr;
    uint64_t originalAtom = 0;

  public:
    ApplySourceAtomGroup(CIRGenFunction &cgf);
    ~ApplySourceAtomGroup();
  };

private:
  bool shouldEmitSourceAtoms() const;
  uint64_t reserveSourceAtomGroup() {
    return shouldEmitSourceAtoms() ? sourceAtomInfo.nextAtom++ : 0;
  }
  mlir::Location applySourceScopeMetadata(mlir::Location loc);
  mlir::Location applySourceAtomMetadata(mlir::Location loc, uint64_t group,
                                         uint8_t rank);
  void addOpToSourceAtomWithRank(mlir::Operation *op, uint64_t group,
                                 uint8_t rank);
  void addOpToSpecificSourceAtom(mlir::Operation *keyOp, mlir::Value backup,
                                 uint64_t group);

  /// A jump destination is an abstract label, branching to which may
  /// require a jump out through normal cleanups.
  struct JumpDest {
    JumpDest() = default;
    JumpDest(mlir::Block *block, EHScopeStack::stable_iterator depth = {},
             unsigned index = 0)
        : block(block), scopeDepth(depth), index(index) {}

    bool isValid() const { return block != nullptr; }
    mlir::Block *getBlock() const { return block; }
    EHScopeStack::stable_iterator getScopeDepth() const { return scopeDepth; }
    unsigned getDestIndex() const { return index; }

    // This should be used cautiously.
    void setScopeDepth(EHScopeStack::stable_iterator depth) {
      scopeDepth = depth;
    }

  private:
    mlir::Block *block = nullptr;
    EHScopeStack::stable_iterator scopeDepth = {};
    unsigned index = 0;
  };

public:
  void addOpToCurrentSourceAtom(mlir::Operation *keyOp,
                                mlir::Value backup = nullptr) {
    addOpToSpecificSourceAtom(keyOp, backup, sourceAtomInfo.currentAtom);
  }

  void addOpToCurrentSourceAtomWithRank(mlir::Operation *op, uint8_t rank) {
    addOpToSourceAtomWithRank(op, sourceAtomInfo.currentAtom, rank);
  }

  void addOpToNewSourceAtom(mlir::Operation *keyOp,
                            mlir::Value backup = nullptr) {
    ApplySourceAtomGroup grp(*this);
    addOpToCurrentSourceAtom(keyOp, backup);
  }

  /// The GlobalDecl for the current function being compiled or the global
  /// variable currently being initialized.
  clang::GlobalDecl curGD;

  /// Unified return block.
  /// In CIR this is a function because each scope might have
  /// its associated return block.
  JumpDest returnBlock(mlir::Block *retBlock) {
    // Use prologueCleanupDepth at function level so cleanup threading
    // works. But only for reachable code — dead blocks after
    // return should use the safe path to avoid block structure issues.
    if (!curLexScope || curLexScope->depth == 0) {
      auto *block = builder.getInsertionBlock();
      bool isReachable = !block || !block->hasNoPredecessors() ||
                          block == &block->getParent()->front();
      if (isReachable)
        return JumpDest(retBlock, prologueCleanupDepth,
                        nextCleanupDestIndex++);
    }
    return getJumpDestInCurrentScope(retBlock);
  }

  unsigned nextCleanupDestIndex = 1;

  /// The compiler-generated variable that holds the return value.
  std::optional<mlir::Value> fnRetAlloca;

  /// One-shot loop hint override consumed by the next lowered loop statement.
  std::optional<mlir::DictionaryAttr> pendingLoopHintAttr;

  /// The AST return type for the function currently being emitted.
  clang::QualType curFnRetTy;

  // Holds coroutine data if the current function is a coroutine. We use a
  // wrapper to manage its lifetime, so that we don't have to define CGCoroData
  // in this header.
  struct CGCoroInfo {
    std::unique_ptr<CGCoroData> data;
    CGCoroInfo();
    ~CGCoroInfo();
  };
  CGCoroInfo curCoro;

  bool isCoroutine() const { return curCoro.data != nullptr; }

  // Holds FramePtr for await_suspend wrapper generation,
  // so that __builtin_coro_frame call can be lowered
  // directly to the value of the wrapper's second argument.
  struct AwaitSuspendWrapperInfo {
    mlir::Value framePtr = nullptr;
  };
  AwaitSuspendWrapperInfo curAwaitSuspendWrapper;

  /// Get the result of __builtin_coro_id (u32i token in CIR).
  mlir::Value getCoroIdResult() const;

  /// Initialize coroutine data from an explicit __builtin_coro_id call
  /// (C coroutine builtins path, not C++ coroutine body).
  void initCoroData(cir::CallOp coroId, const CallExpr *coroIdExpr);

  /// Store the result of __builtin_coro_begin for later use by
  /// __builtin_coro_frame.
  void setCoroBeginValue(mlir::Value coroBegin);
  void emitCoroutineDeallocationStmt(const clang::Stmt *stmt);

  /// The temporary alloca to hold the return value. This is
  /// invalid iff the function has no return value.
  Address returnValue = Address::invalid();

  /// Address of the per-function slot tracking the precise source location of
  /// the current return statement for return-value UBSan checks. The slot
  /// stores a void* pointer to a private source-location global, or null when
  /// the function is falling through an implicit return.
  Address returnCheckLocation = Address::invalid();

  /// Tracks the largest vector width used in this function, accumulated from
  /// function argument types, return types, builtins, and inline asm.
  /// Used to emit the "min-legal-vector-width" attribute on X86 targets.
  unsigned largestVectorWidth = 0;

  /// Tracks function scope overall cleanup handling.
  EHScopeStack ehStack;
  llvm::SmallVector<char, 256> lifetimeExtendedCleanupStack;

  /// A deferred cleanup deactivation entry. Tracks a cleanup that should be
  /// deactivated when its enclosing scope ends (e.g. aggregate construction
  /// that pushes per-member destroy cleanups for exception safety).
  struct DeferredDeactivateCleanup {
    EHScopeStack::stable_iterator cleanup;
    mlir::Operation *dominatingIP;
  };
  llvm::SmallVector<DeferredDeactivateCleanup>
      deferredDeactivationCleanupStack;

  /// Tracks a simple `_Defer` body whose duplicated early-exit cleanup call can
  /// be shared later in LLVM IR lowering.
  const CallExpr *deferredCleanupDispatchCandidateCall = nullptr;

  /// Header for data within LifetimeExtendedCleanupStack.
  struct alignas(uint64_t) LifetimeExtendedCleanupHeader {
    /// The size of the following cleanup object.
    unsigned size;
    /// The kind of cleanup to push.
    unsigned kind : 31;
    /// Whether this is a conditional cleanup.
    unsigned isConditional : 1;

    LifetimeExtendedCleanupHeader(unsigned size, CleanupKind kind,
                                  bool isConditional = false)
        : size(size), kind(kind), isConditional(isConditional) {}
    CleanupKind getKind() const { return static_cast<CleanupKind>(kind); }
  };

  GlobalDecl curSEHParent;

  /// A mapping from NRVO variables to the flags used to indicate
  /// when the NRVO has been applied to this variable.
  llvm::DenseMap<const VarDecl *, mlir::Value> nrvoFlags;

  /// LabelMap - Maps each label declaration to its JumpDest, matching
  /// OGCG's structural approach to goto cleanup tracking. For forward-
  /// referenced labels (goto before label), the JumpDest has an invalid
  /// scope depth that gets filled in when the label is emitted.
  llvm::DenseMap<const clang::LabelDecl *, JumpDest> labelMap;
  llvm::SmallVector<const clang::Stmt *, 4> activeNamedLoopTargetStack;

  struct NamedLoopControlTarget {
    const clang::Stmt *loopOrSwitch = nullptr;
    const LexicalScope *scope = nullptr;
    EHScopeStack::stable_iterator breakTargetDepth =
        EHScopeStack::stable_iterator::invalid();
    EHScopeStack::stable_iterator continueTargetDepth =
        EHScopeStack::stable_iterator::invalid();
    std::string breakLabel;
    std::string continueLabel;
  };
  llvm::SmallVector<NamedLoopControlTarget, 8> namedLoopControlStack;
  unsigned nextNamedLoopControlLabel = 0;

  llvm::DenseMap<const clang::ValueDecl *, clang::FieldDecl *>
      lambdaCaptureFields;
  clang::FieldDecl *lambdaThisCaptureField = nullptr;

  /// API for captured statement code generation.
  class CGCapturedStmtInfo {
  public:
    explicit CGCapturedStmtInfo(const clang::CapturedStmt &s,
                                clang::CapturedRegionKind k = clang::CR_Default)
        : kind(k) {
      clang::RecordDecl::field_iterator field =
          s.getCapturedRecordDecl()->field_begin();
      for (clang::CapturedStmt::const_capture_iterator
               i = s.capture_begin(),
               e = s.capture_end();
           i != e; ++i, ++field) {
        if (i->capturesThis())
          cxxThisFieldDecl = *field;
        else if (i->capturesVariable())
          captureFields[i->getCapturedVar()->getCanonicalDecl()] = *field;
        else if (i->capturesVariableByCopy())
          captureFields[i->getCapturedVar()->getCanonicalDecl()] = *field;
      }
    }

    clang::CapturedRegionKind getKind() const { return kind; }

    void setContextValue(mlir::Value v) { contextValue = v; }
    mlir::Value getContextValue() const { return contextValue; }

    const clang::FieldDecl *lookup(const clang::VarDecl *vd) const {
      return captureFields.lookup(vd->getCanonicalDecl());
    }

    bool isCXXThisExprCaptured() const {
      return cxxThisFieldDecl != nullptr;
    }
    clang::FieldDecl *getThisFieldDecl() const { return cxxThisFieldDecl; }

    llvm::StringRef getHelperName() const { return "__captured_stmt"; }
    void setPGOFuncNameOverride(llvm::StringRef name) {
      pgoFuncNameOverride = name.str();
    }
    llvm::StringRef getPGOFuncNameOverride() const {
      return pgoFuncNameOverride;
    }
    bool hasPGOFuncNameOverride() const {
      return !pgoFuncNameOverride.empty();
    }

  private:
    clang::CapturedRegionKind kind;
    mlir::Value contextValue;
    llvm::SmallDenseMap<const clang::VarDecl *, clang::FieldDecl *>
        captureFields;
    clang::FieldDecl *cxxThisFieldDecl = nullptr;
    std::string pgoFuncNameOverride;
  };

  /// RAII for correct setting/restoring of CapturedStmtInfo.
  class CGCapturedStmtRAII {
    CIRGenFunction &cgf;
    CGCapturedStmtInfo *prevCapturedStmtInfo;

  public:
    CGCapturedStmtRAII(CIRGenFunction &cgf, CGCapturedStmtInfo *newInfo)
        : cgf(cgf), prevCapturedStmtInfo(cgf.capturedStmtInfo) {
      cgf.capturedStmtInfo = newInfo;
    }
    ~CGCapturedStmtRAII() {
      cgf.capturedStmtInfo = prevCapturedStmtInfo;
    }
  };

  class OpenMPNontemporalDeclsRAII {
    CIRGenFunction &cgf;
    const bool needToPush;

  public:
    OpenMPNontemporalDeclsRAII(CIRGenFunction &cgf,
                               const OMPLoopDirective &s);
    ~OpenMPNontemporalDeclsRAII();
  };

  class OpenMPScanParentDirectiveRAII {
    CIRGenFunction &cgf;
    const OMPExecutableDirective *oldDirective = nullptr;

  public:
    OpenMPScanParentDirectiveRAII(CIRGenFunction &cgf,
                                  const OMPExecutableDirective &directive,
                                  llvm::ArrayRef<mlir::Value> reductionArgs = {})
        : cgf(cgf), oldDirective(cgf.openMPParentLoopDirectiveForScan) {
      cgf.openMPParentLoopDirectiveForScan = &directive;
      cgf.openMPScanReductionArgsStack.emplace_back(reductionArgs.begin(),
                                                    reductionArgs.end());
    }

    ~OpenMPScanParentDirectiveRAII() {
      cgf.openMPScanReductionArgsStack.pop_back();
      cgf.openMPParentLoopDirectiveForScan = oldDirective;
    }
  };

  CGCapturedStmtInfo *capturedStmtInfo = nullptr;
  bool forceOpenMPTargetOpEmission = false;
  bool emittingOpenMPTargetGenericFunction = false;

  /// Nonzero while emitting the body of an OpenMP directive that is outlined
  /// post-hoc by OpenMPIRBuilder via llvm::CodeExtractor (parallel/task/target/
  /// teams/taskloop and their combined forms). Used to diagnose constructs the
  /// outliner cannot handle -- e.g. a typed catch clause, which lowers to
  /// llvm.eh.typeid.for and makes CodeExtractor reject the region -- before
  /// they reach the crashing outliner.
  unsigned insideOutlinedOpenMPRegion = 0;

  /// EH-scope-stack depth captured at entry to each currently-active outlined
  /// OpenMP region (parallel to insideOutlinedOpenMPRegion). The back() entry
  /// is the innermost region and marks the boundary between in-region EH scopes
  /// (pushed after entry, strictly enclosed by the snapshot) and out-of-region
  /// scopes (present at entry). exceptionEscapesOpenMPRegion walks the EH chain
  /// against this boundary to decide whether an exception would unwind out of
  /// the region -- a cross-region unwind edge that cannot be lowered (FlattenCFG
  /// produces a malformed cross-region block successor that crashes
  /// collectUnreachable), so it is diagnosed as NYI before the edge is built.
  llvm::SmallVector<EHScopeStack::stable_iterator, 2> openMPRegionEHDepth;

  /// RAII helper that marks the dynamic extent of an outlined OpenMP region
  /// body so nested statement emission can query insideOutlinedOpenMPRegion,
  /// and snapshots the EH-scope-stack depth at region entry.
  struct OutlinedOpenMPRegionRAII {
    CIRGenFunction &cgf;
    OutlinedOpenMPRegionRAII(CIRGenFunction &cgf) : cgf(cgf) {
      ++cgf.insideOutlinedOpenMPRegion;
      cgf.openMPRegionEHDepth.push_back(cgf.ehStack.stable_begin());
    }
    ~OutlinedOpenMPRegionRAII() {
      cgf.openMPRegionEHDepth.pop_back();
      --cgf.insideOutlinedOpenMPRegion;
    }
  };

  /// Nonzero while emitting OpenMP-internal data-sharing codegen at the region
  /// boundary: the lastprivate copy-back, firstprivate copy-in, and reduction
  /// combiner. The potentially-throwing calls these emit (copy-assign,
  /// copy-ctor, user reduction op) are compiler-generated, not user exceptions
  /// escaping the region, so exceptionEscapesOpenMPRegion must not gate them --
  /// e.g. a lastprivate copy-back's copy-assign unwinds to the original
  /// variable's dtor cleanup, which predates region entry, and would otherwise
  /// be misclassified as an escaping exception.
  unsigned insideOpenMPRegionDataSharingCopy = 0;

  /// RAII helper that suppresses the escaping-exception gate for the duration of
  /// an OpenMP-internal data-sharing copy/combiner emission.
  struct OpenMPRegionDataSharingCopyRAII {
    CIRGenFunction &cgf;
    OpenMPRegionDataSharingCopyRAII(CIRGenFunction &cgf) : cgf(cgf) {
      ++cgf.insideOpenMPRegionDataSharingCopy;
    }
    ~OpenMPRegionDataSharingCopyRAII() {
      --cgf.insideOpenMPRegionDataSharingCopy;
    }
  };

  /// Determine whether an exception raised at the current insertion point (a
  /// throwing call or a direct throw) would unwind out of the innermost active
  /// outlined OpenMP region to an EH scope established outside that region.
  /// OpenMPIRBuilder outlines these regions post-hoc via llvm::CodeExtractor;
  /// a cross-region unwind edge cannot be represented and makes FlattenCFG emit
  /// a malformed cross-region block successor that crashes collectUnreachable
  /// during CIR-to-LLVM lowering. Walking the EH scope chain outward from the
  /// innermost active scope, the exception escapes iff it reaches a scope that
  /// predates region entry (at or below the openMPRegionEHDepth.back()
  /// snapshot) after crossing only in-region cleanups. In-region cleanups
  /// always re-raise, so they do not stop propagation; an in-region catch (or
  /// other non-cleanup handler) does, and typed in-region catches are covered
  /// by a separate gate, so such a handler is not treated as an escape here.
  bool exceptionEscapesOpenMPRegion();
  llvm::SmallPtrSet<const VarDecl *, 8> openMPTargetEscapedLocalDecls;
  std::string openMPTargetParentNameOverride;
  using OpenMPNontemporalDeclsSet = llvm::SmallPtrSet<const Decl *, 8>;
  llvm::SmallVector<OpenMPNontemporalDeclsSet, 2>
      openMPNontemporalDeclsStack;
  const OMPExecutableDirective *openMPParentLoopDirectiveForScan = nullptr;
  llvm::SmallVector<llvm::SmallVector<mlir::Value, 4>, 2>
      openMPScanReductionArgsStack;
  /// Fixes up OpenMP loop bodies once the function body is built: moves setup
  /// ops out of loop wrappers and repeats loop setup after each omp.scan.
  void finishOpenMPLoopBodies();
  struct OpenMPLastprivateConditionalFrame {
    llvm::DenseMap<const VarDecl *, std::string> declToUniqueName;
    llvm::DenseMap<const VarDecl *, Address> declToFiredAddr;
    Address ivAddr = Address::invalid();
    bool disabled = false;
  };
  llvm::SmallVector<OpenMPLastprivateConditionalFrame, 2>
      openMPLastprivateConditionalStack;

  /// CXXThisDecl - When generating code for a C++ member function,
  /// this will hold the implicit 'this' declaration.
  ImplicitParamDecl *cxxabiThisDecl = nullptr;
  mlir::Value cxxabiThisValue = nullptr;
  mlir::Value cxxThisValue = nullptr;
  clang::CharUnits cxxabiThisAlignment;
  clang::CharUnits cxxThisAlignment;

  /// When generating code for a constructor or destructor, this will hold the
  /// implicit argument (e.g. VTT).
  ImplicitParamDecl *cxxStructorImplicitParamDecl{};
  mlir::Value cxxStructorImplicitParamValue{};

  /// The values of function arguments to use when evaluating
  /// CXXInheritedCtorInitExprs within this context.
  CallArgList cxxInheritedCtorInitExprArgs;

  /// The value of 'this' to sue when evaluating CXXDefaultInitExprs within this
  /// expression.
  Address cxxDefaultInitExprThis = Address::invalid();

  // Holds the Decl for the current outermost non-closure context
  const clang::Decl *curFuncDecl = nullptr;
  /// This is the inner-most code context, which includes blocks.
  const clang::Decl *curCodeDecl = nullptr;

  /// In C++, whether we are code generating a thunk. This controls whether we
  /// should emit cleanups.
  bool curFuncIsThunk = false;

  /// True if the current statement has [[clang::nomerge]] attribute.
  bool inNoMergeAttributedStmt = false;

  /// True if the current statement has [[clang::noinline]] attribute.
  bool inNoInlineAttributedStmt = false;

  /// True if the current statement has [[clang::always_inline]] attribute.
  bool inAlwaysInlineAttributedStmt = false;

  /// True if the current statement has [[clang::noconvergent]] attribute.
  bool inNoConvergentAttributedStmt = false;

  /// The CallExpr within the current statement that the musttail attribute
  /// applies to. nullptr if there is no 'musttail' on the current statement.
  const clang::CallExpr *mustTailCall = nullptr;

  /// The result expression of the return statement currently being emitted, if
  /// any. Mirrors OGCG's CodeGenFunction::RetExpr (set via SaveRetExprRAII in
  /// CGStmt.cpp): used to decide whether a block capture's lifetime should end
  /// at the end of the full expression rather than the end of the scope
  /// enclosing the block expression (see emitBlockLiteral / CGBlocks.cpp:1089).
  const clang::Expr *retExpr = nullptr;

  /// The current function or global initializer that is generated code for.
  /// This is usually a cir::FuncOp, but it can also be a cir::GlobalOp for
  /// global initializers.
  mlir::Operation *curFn = nullptr;
  /// Number of functions that had already finished emission when this function
  /// started. Used to mirror OGCG's emission-order-sensitive TryMarkNoThrow.
  unsigned completedFunctionEmissionCountAtStart = 0;

  /// Save Parameter Decl for coroutine.
  llvm::SmallVector<const ParmVarDecl *> fnArgs;

  using DeclMapTy = llvm::DenseMap<const clang::Decl *, Address>;
  /// This keeps track of the CIR allocas or globals for local C
  /// declarations.
  DeclMapTy localDeclMap;

  struct BlockByrefInfo {
    cir::RecordType type;
    unsigned fieldIndex = 0;
    unsigned copyHelperIndex = 0;
    unsigned disposeHelperIndex = 0;
    unsigned layoutIndex = 0;
    CharUnits byrefAlignment;
    CharUnits fieldOffset;
    Qualifiers::ObjCLifetime byrefLifetime = Qualifiers::OCL_None;
    bool hasByrefLifetime = false;
    bool hasCopyAndDispose = false;
    bool hasByrefExtendedLayout = false;
  };
  llvm::DenseMap<const clang::VarDecl *, BlockByrefInfo> blockByrefInfos;

  // Keep track of cleanups for callee-destructed parameters that were pushed
  // to the cleanup stack in the function prologue, so delegated/thunk-style
  // forwarding calls can deactivate them before transferring ownership.
  llvm::DenseMap<const clang::ParmVarDecl *, EHScopeStack::stable_iterator>
      calleeDestructedParamCleanups;

  /// If a ParmVarDecl has the pass_object_size attribute, this contains a
  /// mapping from that ParmVarDecl to its implicit "object_size" parameter.
  llvm::SmallDenseMap<const clang::ParmVarDecl *,
                      const clang::ImplicitParamDecl *, 2>
      sizeArguments;

  /// Per-function cache for common integer attrs used in hot prolog/alloca
  /// paths. This avoids repeated MLIR IntegerAttr uniquing lookups for the
  /// same small set of values.
  llvm::DenseMap<int64_t, mlir::IntegerAttr> i64AttrCache;
  llvm::DenseMap<int32_t, mlir::IntegerAttr> i32AttrCache;

  /// The type of the condition for the emitting switch statement.
  llvm::SmallVector<mlir::Type, 2> condTypeStack;

  clang::ASTContext &getContext() const { return cgm.getASTContext(); }

  CIRGenBuilderTy &getBuilder() { return builder; }

  CIRGenModule &getCIRGenModule() { return cgm; }
  const CIRGenModule &getCIRGenModule() const { return cgm; }

  mlir::IntegerAttr getI64AttrCached(int64_t value) {
    auto [it, inserted] = i64AttrCache.try_emplace(value);
    if (inserted)
      // Reuse per-module IntegerAttr cache to avoid repeated MLIR uniquer
      // lookups across functions in large translation units.
      it->second = cgm.getI64AttrCached(value);
    return it->second;
  }

  mlir::IntegerAttr getI32AttrCached(int32_t value) {
    auto [it, inserted] = i32AttrCache.try_emplace(value);
    if (inserted)
      it->second = builder.getI32IntegerAttr(value);
    return it->second;
  }

  mlir::IntegerAttr getSizeAttrCached(CharUnits size) {
    return getI64AttrCached(size.getQuantity());
  }

  mlir::Block *getCurFunctionEntryBlock() {
    if (auto fn = mlir::dyn_cast<cir::FuncOp>(curFn))
      return &fn.getRegion().front();
    // For global initializers, use the ctor region of the GlobalOp.
    if (auto globalOp = mlir::dyn_cast<cir::GlobalOp>(curFn))
      return &globalOp.getCtorRegion().front();
    llvm_unreachable("unexpected curFn type");
  }

  /// Returns true if a function must make progress, mirroring OGCG's
  /// CodeGenFunction::checkIfFunctionMustProgress.
  bool checkIfFunctionMustProgress() const {
    if (cgm.getCodeGenOpts().getFiniteLoops() ==
        CodeGenOptions::FiniteLoopsKind::Never)
      return false;

    return getLangOpts().CPlusPlus11;
  }

  void eraseMustProgressAttr() {
    auto fn = mlir::dyn_cast_or_null<cir::FuncOp>(curFn);
    if (!fn)
      return;

    auto extraAttrs = fn.getExtraAttrs();
    if (!extraAttrs || !extraAttrs->getElements().get("mustprogress"))
      return;

    mlir::NamedAttrList attrs{extraAttrs->getElements().getValue()};
    attrs.erase("mustprogress");
    fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(fn.getContext())));
  }

  /// Sanitizers enabled for this function.
  clang::SanitizerSet sanOpts;

  /// The explicit cast currently wrapping the expression being emitted.
  /// This mirrors OGCG's CurCast and allows alloc-token inference to see
  /// later casts around allocation calls.
  const CastExpr *CurCast = nullptr;

  /// Used by -fsanitize=nullability-return to determine whether the return
  /// value can be checked.
  mlir::Value retValNullabilityPrecondition = nullptr;
  /// True if all FP operations in this function must use constrained intrinsics.
  /// Set when the function has UsesFPIntrin() or StrictFPAttr, matching OGCG's
  /// Builder.setIsFPConstrained() in CodeGenFunction::StartFunction.
  bool IsFPConstrained = false;
  /// True while emitting the operand of __builtin_preserve_access_index().
  bool IsInPreservedAIRegion = false;

  /// The symbol table maps a variable name to a value in the current scope.
  /// Entering a function creates a new scope, and the function arguments are
  /// added to the mapping. When the processing of a function is terminated,
  /// the scope is destroyed and the mappings created in this scope are
  /// dropped.
  using SymTableTy = llvm::ScopedHashTable<const clang::Decl *, mlir::Value>;
  SymTableTy symbolTable;

  /// Whether a cir.stacksave operation has been added. Used to avoid
  /// inserting cir.stacksave for multiple VLAs in the same scope.
  bool didCallStackSave = false;

  /// Whether to emit lifetime markers for local variables.
  bool shouldEmitLifetimeMarkers = false;

  /// Whether runtime calls being emitted are part of sanitizer helper code and
  /// should carry cir.nosanitize metadata.
  bool isSanitizerScope = false;

  /// Suppress the mostly-zero aggregate memset heuristic while lowering
  /// synthetic init recipes that need to preserve their existing direct-CIR
  /// shape.
  bool suppressAggExprMemSetUse = false;

  /// In ARC, whether this function should autorelease the final return value.
  bool autoreleaseResult = false;

  /// Detects variables whose scope is bypassed by jumps (goto, switch).
  /// When a variable is bypassed, lifetime markers must be suppressed.
  CIRVarBypassDetector bypasses;

  /// Return true if a label was seen in the current scope.
  /// In C (not C++), a label in the current scope means a backward jump may
  /// re-enter the scope of variables declared after the label, so lifetime
  /// markers must be suppressed for those variables.
  bool hasLabelBeenSeenInCurrentScope() const {
    if (curLexScope)
      return curLexScope->hasLabels();
    return !labelMap.empty();
  }

  bool shouldExtendLifetime(const VarDecl &d) const;
  void emitFakeUse(Address addr);
  void pushFakeUseCleanupForVar(const VarDecl &d, Address addr);

  /// Whether or not a Microsoft-style asm block has been processed within
  /// this fuction. These can potentially set the return value.
  bool sawAsmBlock = false;

  mlir::Type convertTypeForMem(QualType t);

  mlir::Type convertType(clang::QualType t);
  mlir::Type convertType(const TypeDecl *t) {
    return convertType(getContext().getTypeDeclType(t));
  }

  /// Get integer from a mlir::Value that is an int constant or a constant op.
  static int64_t getSExtIntValueFromConstOp(mlir::Value val) {
    auto constOp = val.getDefiningOp<cir::ConstantOp>();
    assert(constOp && "getSExtIntValueFromConstOp call with non ConstantOp");
    return constOp.getIntValue().getSExtValue();
  }

  /// Get zero-extended integer from a mlir::Value that is an int constant or a
  /// constant op.
  static int64_t getZExtIntValueFromConstOp(mlir::Value val) {
    auto constOp = val.getDefiningOp<cir::ConstantOp>();
    assert(constOp && "getZExtIntValueFromConstOp call with non ConstantOp");
    return constOp.getIntValue().getZExtValue();
  }

  ///  Return the cir::TypeEvaluationKind of QualType \c type.
  static cir::TypeEvaluationKind getEvaluationKind(clang::QualType type);

  static bool hasScalarEvaluationKind(clang::QualType type) {
    return getEvaluationKind(type) == cir::TEK_Scalar;
  }

  static bool hasAggregateEvaluationKind(clang::QualType type) {
    return getEvaluationKind(type) == cir::TEK_Aggregate;
  }

  CIRGenFunction(CIRGenModule &cgm, CIRGenBuilderTy &builder,
                 bool suppressNewContext = false);
  ~CIRGenFunction();

  CIRGenTypes &getTypes() const { return cgm.getTypes(); }

  const TargetInfo &getTarget() const { return cgm.getTarget(); }
  mlir::MLIRContext &getMLIRContext() { return cgm.getMLIRContext(); }
  mlir::MLIRContext &getMLIRContext() const { return cgm.getMLIRContext(); }

  const TargetCIRGenInfo &getTargetHooks() const {
    return cgm.getTargetCIRGenInfo();
  }

  // ---------------------
  // Opaque value handling
  // ---------------------

  /// Keeps track of the current set of opaque value expressions.
  llvm::DenseMap<const OpaqueValueExpr *, LValue> opaqueLValues;
  llvm::DenseMap<const OpaqueValueExpr *, RValue> opaqueRValues;

  // This keeps track of the associated size for each VLA type.
  // We track this by the size expression rather than the type itself because
  // in certain situations, like a const qualifier applied to an VLA typedef,
  // multiple VLA types can share the same size expression.
  // FIXME: Maybe this could be a stack of maps that is pushed/popped as we
  // enter/leave scopes.
  llvm::DenseMap<const Expr *, mlir::Value> vlaSizeMap;

public:
  /// A non-RAII class containing all the information about a bound
  /// opaque value.  OpaqueValueMapping, below, is a RAII wrapper for
  /// this which makes individual mappings very simple; using this
  /// class directly is useful when you have a variable number of
  /// opaque values or don't want the RAII functionality for some
  /// reason.
  class OpaqueValueMappingData {
    const OpaqueValueExpr *opaqueValue;
    bool boundLValue;

    OpaqueValueMappingData(const OpaqueValueExpr *ov, bool boundLValue)
        : opaqueValue(ov), boundLValue(boundLValue) {}

  public:
    OpaqueValueMappingData() : opaqueValue(nullptr) {}

    static bool shouldBindAsLValue(const Expr *expr) {
      // gl-values should be bound as l-values for obvious reasons.
      // Records should be bound as l-values because IR generation
      // always keeps them in memory.  Expressions of function type
      // act exactly like l-values but are formally required to be
      // r-values in C.
      return expr->isGLValue() || expr->getType()->isFunctionType() ||
             hasAggregateEvaluationKind(expr->getType());
    }

    static OpaqueValueMappingData
    bind(CIRGenFunction &cgf, const OpaqueValueExpr *ov, const Expr *e) {
      if (shouldBindAsLValue(ov))
        return bind(cgf, ov, cgf.emitLValue(e));
      return bind(cgf, ov, cgf.emitAnyExpr(e));
    }

    static OpaqueValueMappingData
    bind(CIRGenFunction &cgf, const OpaqueValueExpr *ov, const LValue &lv) {
      assert(shouldBindAsLValue(ov));
      cgf.opaqueLValues.insert(std::make_pair(ov, lv));
      return OpaqueValueMappingData(ov, true);
    }

    static OpaqueValueMappingData
    bind(CIRGenFunction &cgf, const OpaqueValueExpr *ov, const RValue &rv) {
      assert(!shouldBindAsLValue(ov));
      cgf.opaqueRValues.insert(std::make_pair(ov, rv));

      OpaqueValueMappingData data(ov, false);

      // OGCG inserts a dummy bitcast to protect against trunc(zext) folding
      // during LLVM IR construction. Not needed in CIR — MLIR doesn't do
      // aggressive peephole optimization during IR construction.
      return data;
    }

    bool isValid() const { return opaqueValue != nullptr; }
    void clear() { opaqueValue = nullptr; }

    void unbind(CIRGenFunction &cgf) {
      assert(opaqueValue && "no data to unbind!");

      if (boundLValue) {
        cgf.opaqueLValues.erase(opaqueValue);
      } else {
        cgf.opaqueRValues.erase(opaqueValue);
        // OGCG removes the dummy peephole protection bitcast here. Not
        // needed in CIR — MLIR doesn't do aggressive peephole optimization.
      }
    }
  };

  /// An RAII object to set (and then clear) a mapping for an OpaqueValueExpr.
  class OpaqueValueMapping {
    CIRGenFunction &cgf;
    OpaqueValueMappingData data;

  public:
    static bool shouldBindAsLValue(const Expr *expr) {
      return OpaqueValueMappingData::shouldBindAsLValue(expr);
    }

    /// Build the opaque value mapping for the given conditional
    /// operator if it's the GNU ?: extension.  This is a common
    /// enough pattern that the convenience operator is really
    /// helpful.
    ///
    OpaqueValueMapping(CIRGenFunction &cgf,
                       const AbstractConditionalOperator *op)
        : cgf(cgf) {
      if (mlir::isa<ConditionalOperator>(op))
        // Leave Data empty.
        return;

      const BinaryConditionalOperator *e =
          mlir::cast<BinaryConditionalOperator>(op);
      data = OpaqueValueMappingData::bind(cgf, e->getOpaqueValue(),
                                          e->getCommon());
    }

    /// Build the opaque value mapping for an OpaqueValueExpr whose source
    /// expression is set to the expression the OVE represents.
    OpaqueValueMapping(CIRGenFunction &cgf, const OpaqueValueExpr *ov)
        : cgf(cgf) {
      if (ov) {
        assert(ov->getSourceExpr() && "wrong form of OpaqueValueMapping used "
                                      "for OVE with no source expression");
        data = OpaqueValueMappingData::bind(cgf, ov, ov->getSourceExpr());
      }
    }

    OpaqueValueMapping(CIRGenFunction &cgf, const OpaqueValueExpr *opaqueValue,
                       LValue lvalue)
        : cgf(cgf),
          data(OpaqueValueMappingData::bind(cgf, opaqueValue, lvalue)) {}

    OpaqueValueMapping(CIRGenFunction &cgf, const OpaqueValueExpr *opaqueValue,
                       RValue rvalue)
        : cgf(cgf),
          data(OpaqueValueMappingData::bind(cgf, opaqueValue, rvalue)) {}

    void pop() {
      data.unbind(cgf);
      data.clear();
    }

    ~OpaqueValueMapping() {
      if (data.isValid())
        data.unbind(cgf);
    }
  };

  class VLASizeMapping {
    CIRGenFunction &cgf;
    const Expr *sizeExpr = nullptr;
    mlir::Value oldValue;
    bool hadOldValue = false;

  public:
    VLASizeMapping(CIRGenFunction &cgf, const Expr *sizeExpr,
                   mlir::Value sizeValue)
        : cgf(cgf), sizeExpr(sizeExpr) {
      if (!sizeExpr)
        return;
      if (auto it = cgf.vlaSizeMap.find(sizeExpr); it != cgf.vlaSizeMap.end()) {
        hadOldValue = true;
        oldValue = it->second;
      }
      cgf.vlaSizeMap[sizeExpr] = sizeValue;
    }

    ~VLASizeMapping() {
      if (!sizeExpr)
        return;
      if (hadOldValue)
        cgf.vlaSizeMap[sizeExpr] = oldValue;
      else
        cgf.vlaSizeMap.erase(sizeExpr);
    }
  };

  /// The current index into an ArrayInitLoopExpr, used by
  /// ArrayInitIndexExpr within an ArrayInitLoopExpr.
  mlir::Value arrayInitIndex = {};

  mlir::Value getArrayInitIndex() { return arrayInitIndex; }

  /// The scope of an ArrayInitLoopExpr. Within this scope, the value of the
  /// current loop index is overridden.
  class ArrayInitLoopExprScope {
  public:
    ArrayInitLoopExprScope(CIRGenFunction &cgf, mlir::Value index)
        : cgf(cgf), oldArrayInitIndex(cgf.arrayInitIndex) {
      cgf.arrayInitIndex = index;
    }
    ~ArrayInitLoopExprScope() { cgf.arrayInitIndex = oldArrayInitIndex; }

  private:
    CIRGenFunction &cgf;
    mlir::Value oldArrayInitIndex;
  };

private:
  /// Declare a variable in the current scope, return success if the variable
  /// wasn't declared yet.
  void declare(mlir::Value addrVal, const clang::Decl *var, clang::QualType ty,
               mlir::Location loc, clang::CharUnits alignment,
               bool isParam = false);

public:
  mlir::Value createDummyValue(mlir::Location loc, clang::QualType qt);

  void emitNullInitialization(mlir::Location loc, Address destPtr, QualType ty);

private:
  // Track current variable initialization (if there's one)
  const clang::VarDecl *currVarDecl = nullptr;
  class VarDeclContext {
    CIRGenFunction &p;
    const clang::VarDecl *oldVal = nullptr;

  public:
    VarDeclContext(CIRGenFunction &p, const VarDecl *value) : p(p) {
      if (p.currVarDecl)
        oldVal = p.currVarDecl;
      p.currVarDecl = value;
    }

    /// Can be used to restore the state early, before the dtor
    /// is run.
    void restore() { p.currVarDecl = oldVal; }
    ~VarDeclContext() { restore(); }
  };

public:
  /// Use to track source locations across nested visitor traversals.
  /// Always use a `SourceLocRAIIObject` to change currSrcLoc.
  std::optional<mlir::Location> currSrcLoc;
  class SourceLocRAIIObject {
    CIRGenFunction &cgf;
    std::optional<mlir::Location> oldLoc;

  public:
    SourceLocRAIIObject(CIRGenFunction &cgf, mlir::Location value) : cgf(cgf) {
      if (cgf.currSrcLoc)
        oldLoc = cgf.currSrcLoc;
      cgf.currSrcLoc = value;
    }

    /// Can be used to restore the state early, before the dtor
    /// is run.
    void restore() { cgf.currSrcLoc = oldLoc; }
    ~SourceLocRAIIObject() { restore(); }
  };
  std::optional<mlir::Location> currCallDebugLocOverride;
  class CallDebugLocRAIIObject {
    CIRGenFunction &cgf;
    std::optional<mlir::Location> oldLoc;

  public:
    CallDebugLocRAIIObject(CIRGenFunction &cgf, mlir::Location value)
        : cgf(cgf), oldLoc(cgf.currCallDebugLocOverride) {
      cgf.currCallDebugLocOverride = value;
    }

    /// Can be used to restore the state early, before the dtor
    /// is run.
    void restore() { cgf.currCallDebugLocOverride = oldLoc; }
    ~CallDebugLocRAIIObject() { restore(); }
  };

  using SymTableScopeTy =
      llvm::ScopedHashTableScope<const clang::Decl *, mlir::Value>;

  /// Hold counters for incrementally naming temporaries
  unsigned counterRefTmp = 0;
  unsigned counterAggTmp = 0;
  std::string getCounterRefTmpAsString();
  std::string getCounterAggTmpAsString();

  /// Helpers to convert Clang's SourceLocation to a MLIR Location.
  mlir::Location getSimpleLoc(clang::SourceLocation srcLoc);
  mlir::Location getLoc(clang::SourceLocation srcLoc);
  mlir::Location getLoc(clang::SourceRange srcLoc);
  mlir::Location getLoc(mlir::Location lhs, mlir::Location rhs);

  const clang::LangOptions &getLangOpts() const { return cgm.getLangOpts(); }

  /// Increment the profiler's counter for the given statement.
  void incrementProfileCounter(const clang::Stmt *S,
                               mlir::Value stepV = nullptr) {
    if (PGO)
      PGO->emitCounterIncrement(builder, S, getLoc(S->getBeginLoc()), stepV);
  }

  CIRGenPGO::RegionCountersScope
  enterOpenMPHelperRegionCounters(const clang::CapturedStmt &S);
  std::string getOpenMPHelperPGOName(const clang::CapturedStmt &S);

  /// Per-enclosing-function running counter used to disambiguate the PGO
  /// instrument-profiling names of multiple OpenMP outlined regions, mirroring
  /// the `.N` numbering that classic clang gets from the LLVM symbol table.
  /// Keyed by the enclosing cir::FuncOp operation.
  llvm::DenseMap<mlir::Operation *, unsigned> openMPOutlinedPGOCounter;
  /// Index assigned to each OpenMP outlined region's CapturedStmt, allocated
  /// once when the PGO name attribute is computed and reused when entering the
  /// region counters so both agree on the same disambiguated name.
  llvm::DenseMap<const clang::CapturedStmt *, unsigned> openMPOutlinedRegionIndex;

  /// Convert clang FPOptions to CIR FastMathFlags.
  static cir::FastMathFlags getFastMathFlags(clang::FPOptions fpFeatures) {
    cir::FastMathFlags fmf = cir::FastMathFlags::none;
    if (fpFeatures.getAllowFPReassociate())
      fmf = fmf | cir::FastMathFlags::reassoc;
    if (fpFeatures.getNoHonorNaNs())
      fmf = fmf | cir::FastMathFlags::nnan;
    if (fpFeatures.getNoHonorInfs())
      fmf = fmf | cir::FastMathFlags::ninf;
    if (fpFeatures.getNoSignedZero())
      fmf = fmf | cir::FastMathFlags::nsz;
    if (fpFeatures.getAllowReciprocal())
      fmf = fmf | cir::FastMathFlags::arcp;
    if (fpFeatures.getAllowApproxFunc())
      fmf = fmf | cir::FastMathFlags::afn;
    if (fpFeatures.allowFPContractAcrossStatement())
      fmf = fmf | cir::FastMathFlags::contract;
    return fmf;
  }

  /// Extract FP exception behavior attribute from FPOptions.
  /// Returns {} (null attr) when exception behavior is the default (ignore)
  /// AND the function is not in a constrained FP context. When
  /// IsFPConstrained is true (function-level strictfp), returns the Ignore
  /// attribute so constrained intrinsics are still emitted.
  cir::FPExceptionBehaviorAttr
  getFPExceptionBehavior(mlir::MLIRContext *ctx,
                         clang::FPOptions fpFeatures) const {
    auto mode = fpFeatures.getExceptionMode();
    if (mode == clang::LangOptions::FPE_Ignore) {
      if (IsFPConstrained)
        return cir::FPExceptionBehaviorAttr::get(
            ctx, cir::FPExceptionBehavior::Ignore);
      return {};
    }
    if (mode == clang::LangOptions::FPE_MayTrap)
      return cir::FPExceptionBehaviorAttr::get(
          ctx, cir::FPExceptionBehavior::MayTrap);
    return cir::FPExceptionBehaviorAttr::get(
        ctx, cir::FPExceptionBehavior::Strict);
  }

  /// Extract rounding mode attribute from FPOptions.
  /// Returns {} (null attr) when rounding mode is the default
  /// (NearestTiesToEven).
  static cir::RoundingModeAttr getRoundingMode(mlir::MLIRContext *ctx,
                                               clang::FPOptions fpFeatures) {
    auto rm = fpFeatures.getRoundingMode();
    if (rm == llvm::RoundingMode::NearestTiesToEven)
      return {};
    cir::RoundingMode cirRM;
    switch (rm) {
    case llvm::RoundingMode::TowardZero:
      cirRM = cir::RoundingMode::TowardZero;
      break;
    case llvm::RoundingMode::TowardPositive:
      cirRM = cir::RoundingMode::TowardPositive;
      break;
    case llvm::RoundingMode::TowardNegative:
      cirRM = cir::RoundingMode::TowardNegative;
      break;
    case llvm::RoundingMode::NearestTiesToAway:
      cirRM = cir::RoundingMode::NearestTiesToAway;
      break;
    case llvm::RoundingMode::Dynamic:
      cirRM = cir::RoundingMode::Dynamic;
      break;
    default:
      return {};
    }
    return cir::RoundingModeAttr::get(ctx, cirRM);
  }

  void maybeAttachFPConstraints(mlir::Operation *op,
                                clang::FPOptions fpFeatures) const {
    if (!op || !(fpFeatures.isFPConstrained() || IsFPConstrained))
      return;

    auto *ctx = &getMLIRContext();
    if (auto fpExcept = getFPExceptionBehavior(ctx, fpFeatures))
      op->setAttr("cir.fp_exception", fpExcept);
    if (auto roundMode = getRoundingMode(ctx, fpFeatures))
      op->setAttr("cir.rounding_mode", roundMode);
  }

  void maybeMarkSignalingFPCompare(mlir::Operation *op,
                                   bool isSignaling) const {
    if (op && isSignaling && op->hasAttr("cir.fp_exception"))
      op->setAttr("cir.fp_signaling_compare",
                  mlir::UnitAttr::get(&getMLIRContext()));
  }

  /// True if an insertion point is defined. If not, this indicates that the
  /// current code being emitted is unreachable.
  /// FIXME(cir): we need to inspect this and perhaps use a cleaner mechanism
  /// since we don't yet force null insertion point to designate behavior (like
  /// LLVM's codegen does) and we probably shouldn't.
  bool haveInsertPoint() const {
    return builder.getInsertionBlock() != nullptr;
  }

  // Wrapper for function prototype sources. Wraps either a FunctionProtoType or
  // an ObjCMethodDecl.
  struct PrototypeWrapper {
    llvm::PointerUnion<const clang::FunctionProtoType *,
                       const clang::ObjCMethodDecl *>
        p;

    PrototypeWrapper(const clang::FunctionProtoType *ft) : p(ft) {}
    PrototypeWrapper(const clang::ObjCMethodDecl *md) : p(md) {}
  };

  bool isLValueSuitableForInlineAtomic(LValue lv);

  /// An abstract representation of regular/ObjC call/message targets.
  class AbstractCallee {
    /// The function declaration of the callee.
    [[maybe_unused]] const clang::Decl *calleeDecl;

  public:
    AbstractCallee() : calleeDecl(nullptr) {}
    AbstractCallee(const clang::FunctionDecl *fd) : calleeDecl(fd) {}
    AbstractCallee(const clang::Decl *d) : calleeDecl(d) {}

    const clang::Decl *getDecl() const { return calleeDecl; }

    bool hasFunctionDecl() const {
      return llvm::isa_and_nonnull<clang::FunctionDecl>(calleeDecl);
    }

    unsigned getNumParams() const {
      if (const auto *fd = llvm::dyn_cast<clang::FunctionDecl>(calleeDecl))
        return fd->getNumParams();
      return llvm::cast<clang::ObjCMethodDecl>(calleeDecl)->param_size();
    }

    const clang::ParmVarDecl *getParamDecl(unsigned I) const {
      if (const auto *fd = llvm::dyn_cast<clang::FunctionDecl>(calleeDecl))
        return fd->getParamDecl(I);
      return *(llvm::cast<clang::ObjCMethodDecl>(calleeDecl)->param_begin() +
               I);
    }
  };

  struct VlaSizePair {
    mlir::Value numElts;
    QualType type;

    VlaSizePair(mlir::Value num, QualType ty) : numElts(num), type(ty) {}
  };

  /// Return the number of elements for a single dimension
  /// for the given array type.
  VlaSizePair getVLAElements1D(const VariableArrayType *vla);

  /// Returns an MLIR::Value+QualType pair that corresponds to the size,
  /// in non-variably-sized elements, of a variable length array type,
  /// plus that largest non-variably-sized element type.  Assumes that
  /// the type has already been emitted with emitVariablyModifiedType.
  VlaSizePair getVLASize(const VariableArrayType *type);
  VlaSizePair getVLASize(QualType type);

  /// Emit a store of the VLA size into the lambda capture field.
  /// OGCG: EmitLambdaVLACapture
  void emitLambdaVLACapture(const VariableArrayType *vat, LValue lv) {
    emitStoreThroughLValue(RValue::get(vlaSizeMap[vat->getSizeExpr()]), lv);
  }

  Address getAsNaturalAddressOf(Address addr, QualType pointeeTy);

  mlir::Value emitPointerAuthQualify(PointerAuthQualifier destQualifier,
                                     const Expr *e,
                                     Address destStorageAddress);
  mlir::Value emitPointerAuthQualify(PointerAuthQualifier destQualifier,
                                     mlir::Value value, QualType pointerType,
                                     Address destStorageAddress,
                                     bool isKnownNonNull);
  mlir::Value emitPointerAuthUnqualify(PointerAuthQualifier curQualifier,
                                       mlir::Value value,
                                       QualType pointerType,
                                       Address curStorageAddress,
                                       bool isKnownNonNull);
  CIRGenRuntimePointerAuthInfo emitPointerAuthInfo(PointerAuthQualifier qual,
                                                   Address storageAddress);
  mlir::Value emitPointerAuthBlendDiscriminator(mlir::Value storageAddress,
                                                mlir::Value discriminator);
  mlir::Value emitPointerAuthSign(
      const CIRGenRuntimePointerAuthInfo &pointerAuth, mlir::Value pointer);
  mlir::Value emitPointerAuthAuth(
      const CIRGenRuntimePointerAuthInfo &pointerAuth, mlir::Value pointer);
  mlir::Value emitPointerAuthResignCall(
      mlir::Value value, const CIRGenRuntimePointerAuthInfo &curAuth,
      const CIRGenRuntimePointerAuthInfo &newAuth);
  mlir::Value emitPointerAuthResign(
      mlir::Value value, QualType type,
      const CIRGenRuntimePointerAuthInfo &curAuthInfo,
      const CIRGenRuntimePointerAuthInfo &newAuthInfo, bool isKnownNonNull);
  mlir::Value authPointerToPointerCast(mlir::Value resultPtr,
                                       QualType sourceType,
                                       QualType destType);
  void emitPointerAuthCopy(PointerAuthQualifier qualifier, QualType type,
                           Address destAddress, Address srcAddress);

  mlir::Value getAsNaturalPointerTo(Address addr, QualType pointeeType) {
    return getAsNaturalAddressOf(addr, pointeeType).getBasePointer();
  }

  void finishFunction(SourceLocation endLoc);

  // Many of MSVC builtins are on x64, ARM and AArch64; to avoid repeating code,
  // we handle them here.
  enum class MSVCIntrin {
    _BitScanForward,
    _BitScanReverse,
    _InterlockedAnd,
    _InterlockedCompareExchange,
    _InterlockedDecrement,
    _InterlockedExchange,
    _InterlockedExchangeAdd,
    _InterlockedExchangeSub,
    _InterlockedIncrement,
    _InterlockedOr,
    _InterlockedXor,
    _InterlockedExchangeAdd_acq,
    _InterlockedExchangeAdd_rel,
    _InterlockedExchangeAdd_nf,
    _InterlockedExchange_acq,
    _InterlockedExchange_rel,
    _InterlockedExchange_nf,
    _InterlockedCompareExchange_acq,
    _InterlockedCompareExchange_rel,
    _InterlockedCompareExchange_nf,
    _InterlockedCompareExchange128,
    _InterlockedCompareExchange128_acq,
    _InterlockedCompareExchange128_rel,
    _InterlockedCompareExchange128_nf,
    _InterlockedOr_acq,
    _InterlockedOr_rel,
    _InterlockedOr_nf,
    _InterlockedXor_acq,
    _InterlockedXor_rel,
    _InterlockedXor_nf,
    _InterlockedAnd_acq,
    _InterlockedAnd_rel,
    _InterlockedAnd_nf,
    _InterlockedIncrement_acq,
    _InterlockedIncrement_rel,
    _InterlockedIncrement_nf,
    _InterlockedDecrement_acq,
    _InterlockedDecrement_rel,
    _InterlockedDecrement_nf,
    __fastfail,
  };

  mlir::Value emitMSVCBuiltinExpr(MSVCIntrin builtinID, const CallExpr *e);
  /// Determine whether the given initializer is trivial in the sense
  /// that it requires no code to be generated.
  bool isTrivialInitializer(const Expr *init);

  /// If the specified expression does not fold to a constant, or if it does but
  /// contains a label, return false.  If it constant folds return true and set
  /// the boolean result in Result.
  bool constantFoldsToBool(const clang::Expr *cond, bool &resultBool,
                           bool allowLabels = false);
  bool constantFoldsToSimpleInteger(const clang::Expr *cond,
                                    llvm::APSInt &resultInt,
                                    bool allowLabels = false);
  static const clang::Expr *stripCond(const clang::Expr *cond);
  static bool isInstrumentedCondition(const clang::Expr *cond);

  /// Return true if the statement contains a label in it.  If
  /// this statement is not executed normally, it not containing a label means
  /// that we can just remove the code.
  bool containsLabel(const clang::Stmt *s, bool ignoreCaseStmts = false);
  /// Return true if the statement contains a break out of it.
  /// If the statement (recursively) contains a switch or loop with a break
  /// inside of it, this is fine.
  bool containsBreak(const clang::Stmt *s);
  /// Determine if the given statement might introduce a declaration into the
  /// current scope, by being a (possibly-labelled) DeclStmt.
  bool mightAddDeclToScope(const clang::Stmt *s);

  Address emitExtVectorElementLValue(LValue lv, mlir::Location loc);

  class ConstantEmission {
    // Cannot use mlir::TypedAttr directly here because of bit availability.
    llvm::PointerIntPair<mlir::Attribute, 1, bool> valueAndIsReference;
    ConstantEmission(mlir::TypedAttr c, bool isReference)
        : valueAndIsReference(c, isReference) {}

  public:
    ConstantEmission() {}
    static ConstantEmission forReference(mlir::TypedAttr c) {
      return ConstantEmission(c, true);
    }
    static ConstantEmission forValue(mlir::TypedAttr c) {
      return ConstantEmission(c, false);
    }

    explicit operator bool() const {
      return valueAndIsReference.getOpaqueValue() != nullptr;
    }

    bool isReference() const { return valueAndIsReference.getInt(); }
    LValue getReferenceLValue(CIRGenFunction &cgf, Expr *refExpr) const {
      assert(isReference());
      // The constant is a pointer to the referenced object (e.g. a
      // GlobalViewAttr). Materialize it as an SSA value and wrap it in an
      // LValue with the natural alignment of the expression type.
      mlir::TypedAttr c =
          mlir::cast<mlir::TypedAttr>(valueAndIsReference.getPointer());
      mlir::Value val =
          cgf.builder.getConstant(cgf.getLoc(refExpr->getSourceRange()), c);
      return cgf.makeNaturalAlignAddrLValue(val, refExpr->getType());
    }

    mlir::TypedAttr getValue() const {
      assert(!isReference());
      return mlir::cast<mlir::TypedAttr>(valueAndIsReference.getPointer());
    }
  };

  ConstantEmission tryEmitAsConstant(const DeclRefExpr *refExpr);
  ConstantEmission tryEmitAsConstant(const MemberExpr *me);

  struct AutoVarEmission {
    const clang::VarDecl *variable;
    /// The address of the alloca for languages with explicit address space
    /// (e.g. OpenCL) or alloca casted to generic pointer for address space
    /// agnostic languages (e.g. C++). Invalid if the variable was emitted
    /// as a global constant.
    Address addr;

    /// True if the variable is of aggregate type and has a constant
    /// initializer.
    bool isConstantAggregate = false;

    /// True if the variable is a __block variable that is captured by an
    /// escaping block.
    bool isEscapingByRef = false;

    /// True if the variable was emitted as an offload recipe, and thus doesn't
    /// have the same sort of alloca initialization.
    bool emittedAsOffload = false;

    /// True if lifetime markers should be used for this variable.
    bool useLifetimeMarkers = false;

    /// Address with original alloca instruction (in alloca address space).
    /// Used for lifetime intrinsics. Invalid if NRVO or emitted as global.
    Address allocaAddr = Address::invalid();

    Address getOriginalAllocatedAddress() const { return allocaAddr; }

    mlir::Value nrvoFlag{};

    struct Invalid {};
    AutoVarEmission(Invalid) : variable(nullptr), addr(Address::invalid()) {}

    AutoVarEmission(const clang::VarDecl &variable)
        : variable(&variable), addr(Address::invalid()) {}

    static AutoVarEmission invalid() { return AutoVarEmission(Invalid()); }

    bool wasEmittedAsGlobal() const { return !addr.isValid(); }

    bool wasEmittedAsOffloadClause() const { return emittedAsOffload; }

    /// Returns the raw, allocated address, which is not necessarily
    /// the address of the object itself. It is casted to default
    /// address space for address space agnostic languages.
    Address getAllocatedAddress() const { return addr; }

    // Changes the stored address for the emission.  This function should only
    // be used in extreme cases, and isn't required to model normal AST
    // initialization/variables.
    void setAllocatedAddress(Address a) { addr = a; }

    /// Returns the address of the object within this declaration.
    /// Note that this does not chase the forwarding pointer for
    /// __block decls.
    Address getObjectAddress(CIRGenFunction &cgf) const {
      if (!isEscapingByRef)
        return addr;
      return cgf.emitBlockByrefAddress(addr, variable, /*followForward=*/false);
    }
  };

  /// The given basic block lies in the current EH scope, but may be a
  /// target of a potentially scope-crossing jump; get a stable handle
  /// to which we can perform this jump later.
  /// CIRGen: this mostly tracks state for figuring out the proper scope
  /// information, no actual branches are emitted.
  JumpDest getJumpDestInCurrentScope(mlir::Block *target) {
    return JumpDest(target, ehStack.getInnermostNormalCleanup(),
                    nextCleanupDestIndex++);
  }

  /// Return the JumpDest for a label, creating a forward-reference entry
  /// with invalid scope depth if the label has not been emitted yet.
  /// Matches OGCG's getJumpDestForLabel (CGStmt.cpp:709-725).
  JumpDest getJumpDestForLabel(const clang::LabelDecl *d) {
    auto it = labelMap.find(d);
    if (it != labelMap.end())
      return it->second;
    JumpDest dest(nullptr, EHScopeStack::stable_iterator::invalid(),
                  nextCleanupDestIndex++);
    labelMap[d] = dest;
    return dest;
  }

  /// IndirectBranch - The first time an indirect goto is seen we create a block
  /// reserved for the indirect branch. Unlike before,the actual 'indirectbr'
  /// is emitted at the end of the function, once all block destinations have
  /// been resolved.
  mlir::Block *indirectGotoBlock = nullptr;
  /// Whether any cir.indirect_goto ops were emitted in this function.
  /// Used to avoid marking the indirect branch as poison when the dispatch
  /// block has no predecessors yet (predecessors are added by GotoSolver).
  bool hasIndirectGoto = false;

  void resolveBlockAddresses();
  void finishIndirectBranch();

  /// Perform the usual unary conversions on the specified expression and
  /// compare the result against zero, returning an Int1Ty value.
  mlir::Value evaluateExprAsBool(const clang::Expr *e);

  cir::GlobalOp addInitializerToStaticVarDecl(const VarDecl &d,
                                              cir::GlobalOp gv,
                                              cir::GetGlobalOp gvAddr);

  /// Enter the cleanups necessary to complete the given phase of destruction
  /// for a destructor. The end result should call destructors on members and
  /// base classes in reverse order of their construction.
  void enterDtorCleanups(const CXXDestructorDecl *dtor, CXXDtorType type);

  /// Determines whether an EH cleanup is required to destroy a type
  /// with the given destruction kind.
  /// TODO(cir): could be shared with Clang LLVM codegen
  bool needsEHCleanup(QualType::DestructionKind kind) {
    switch (kind) {
    case QualType::DK_none:
      return false;
    case QualType::DK_cxx_destructor:
    case QualType::DK_objc_weak_lifetime:
    case QualType::DK_nontrivial_c_struct:
      return getLangOpts().Exceptions;
    case QualType::DK_objc_strong_lifetime:
      return getLangOpts().Exceptions &&
             cgm.getCodeGenOpts().ObjCAutoRefCountExceptions;
    }
    llvm_unreachable("bad destruction kind");
  }

  CleanupKind getCleanupKind(QualType::DestructionKind kind) {
    return needsEHCleanup(kind) ? NormalAndEHCleanup : NormalCleanup;
  }

  void pushStackRestore(CleanupKind kind, Address spMem);

  /// Set the address of a local variable.
  void setAddrOfLocalVar(const clang::VarDecl *vd, Address addr) {
    assert(!localDeclMap.count(vd) && "Decl already exists in LocalDeclMap!");
    localDeclMap.insert({vd, addr});

    // Add to the symbol table if not there already.
    if (symbolTable.count(vd))
      return;
    symbolTable.insert(vd, addr.getPointer());
  }

  // Replaces the address of the local variable, if it exists.  Else does the
  // same thing as setAddrOfLocalVar.
  void replaceAddrOfLocalVar(const clang::VarDecl *vd, Address addr) {
    localDeclMap.insert_or_assign(vd, addr);
    if (!symbolTable.count(vd))
      symbolTable.insert(vd, addr.getPointer());
  }

  void eraseAddrOfLocalVar(const clang::VarDecl *vd) {
    localDeclMap.erase(vd);
  }

  // A class to allow reverting changes to a var-decl's registration to the
  // localDeclMap. This is used in cases where things are being inserted into
  // the variable list but don't follow normal lookup/search rules, like in
  // OpenACC recipe generation.
  class DeclMapRevertingRAII {
    CIRGenFunction &cgf;
    const VarDecl *vd;
    bool shouldDelete = false;
    Address oldAddr = Address::invalid();

  public:
    DeclMapRevertingRAII(CIRGenFunction &cgf, const VarDecl *vd)
        : cgf(cgf), vd(vd) {
      auto mapItr = cgf.localDeclMap.find(vd);

      if (mapItr != cgf.localDeclMap.end())
        oldAddr = mapItr->second;
      else
        shouldDelete = true;
    }

    ~DeclMapRevertingRAII() {
      if (shouldDelete)
        cgf.localDeclMap.erase(vd);
      else
        cgf.localDeclMap.insert_or_assign(vd, oldAddr);
    }
  };

  class VlaSizeMapRevertingRAII {
    CIRGenFunction &cgf;
    llvm::DenseMap<const Expr *, mlir::Value> oldVlaSizeMap;

  public:
    explicit VlaSizeMapRevertingRAII(CIRGenFunction &cgf)
        : cgf(cgf), oldVlaSizeMap(cgf.vlaSizeMap) {}

    ~VlaSizeMapRevertingRAII() { cgf.vlaSizeMap.swap(oldVlaSizeMap); }
  };

  bool shouldNullCheckClassCastValue(const CastExpr *ce);

  RValue convertTempToRValue(Address addr, clang::QualType type,
                             clang::SourceLocation loc);

  static bool
  isConstructorDelegationValid(const clang::CXXConstructorDecl *ctor);

  struct VPtr {
    clang::BaseSubobject base;
    const clang::CXXRecordDecl *nearestVBase;
    clang::CharUnits offsetFromNearestVBase;
    const clang::CXXRecordDecl *vtableClass;
  };

  using VisitedVirtualBasesSetTy =
      llvm::SmallPtrSet<const clang::CXXRecordDecl *, 4>;

  using VPtrsVector = llvm::SmallVector<VPtr, 4>;
  VPtrsVector getVTablePointers(const clang::CXXRecordDecl *vtableClass);
  void getVTablePointers(clang::BaseSubobject base,
                         const clang::CXXRecordDecl *nearestVBase,
                         clang::CharUnits offsetFromNearestVBase,
                         bool baseIsNonVirtualPrimaryBase,
                         const clang::CXXRecordDecl *vtableClass,
                         VisitedVirtualBasesSetTy &vbases, VPtrsVector &vptrs);
  // VTableAuthMode - whether loading a vtable pointer must merely authenticate
  // it, must trap even if the result is otherwise unused, or should strip the
  // signature for UBSan vptr checks.
  enum class VTableAuthMode {
    Authenticate,
    MustTrap,
    UnsafeUbsanStrip
  };
  enum class CFICastCheckKind {
    DerivedCast,
    UnrelatedCast
  };
  /// Return the Value of the vtable pointer member pointed to by thisAddr.
  mlir::Value getVTablePtr(mlir::Location loc, Address thisAddr,
                           const clang::CXXRecordDecl *vtableClass,
                           VTableAuthMode authMode =
                               VTableAuthMode::Authenticate);
  void emitVTablePtrCheckForCast(QualType T, Address derived, bool mayBeNull,
                                 CFICastCheckKind kind,
                                 SourceLocation loc);

  /// Emit assumption loads for all vtable pointers in classDecl, asserting
  /// that each vptr equals the expected vtable address point. This enables
  /// devirtualization under -fstrict-vtable-pointers.
  void emitVTableAssumptionLoads(const clang::CXXRecordDecl *classDecl,
                                 Address thisAddr);
  void emitVTableAssumptionLoad(mlir::Location loc, const VPtr &vptr,
                                Address thisAddr);

  /// Build an Apple kext virtual call via direct vtable GEP. Used when
  /// -fapple-kext is set and a qualified virtual call is made.
  CIRGenCallee buildAppleKextVirtualCall(const clang::CXXMethodDecl *md,
                                         clang::NestedNameSpecifier qual,
                                         mlir::Type ty);

  CIRGenCallee buildAppleKextVirtualDestructorCall(
      const clang::CXXDestructorDecl *dd, clang::CXXDtorType type,
      const clang::CXXRecordDecl *rd);

  /// Returns whether we should perform a type checked load when loading a
  /// virtual function for virtual calls to members of RD. This is generally
  /// true when both vcall CFI and whole-program-vtables are enabled.
  bool shouldEmitVTableTypeCheckedLoad(const CXXRecordDecl *rd);

  /// Source location information about the default argument or member
  /// initializer expression we're evaluating, if any.
  clang::CurrentSourceLocExprScope curSourceLocExprScope;
  using SourceLocExprScopeGuard =
      clang::CurrentSourceLocExprScope::SourceLocExprScopeGuard;

  /// A scope within which we are constructing the fields of an object which
  /// might use a CXXDefaultInitExpr. This stashes away a 'this' value to use if
  /// we need to evaluate the CXXDefaultInitExpr within the evaluation.
  class FieldConstructionScope {
  public:
    FieldConstructionScope(CIRGenFunction &cgf, Address thisAddr)
        : cgf(cgf), oldCXXDefaultInitExprThis(cgf.cxxDefaultInitExprThis) {
      cgf.cxxDefaultInitExprThis = thisAddr;
    }
    ~FieldConstructionScope() {
      cgf.cxxDefaultInitExprThis = oldCXXDefaultInitExprThis;
    }

  private:
    CIRGenFunction &cgf;
    Address oldCXXDefaultInitExprThis;
  };

  /// The scope of a CXXDefaultInitExpr. Within this scope, the value of 'this'
  /// is overridden to be the object under construction.
  class CXXDefaultInitExprScope {
  public:
    CXXDefaultInitExprScope(CIRGenFunction &cgf, const CXXDefaultInitExpr *e)
        : cgf{cgf}, oldCXXThisValue(cgf.cxxThisValue),
          oldCXXThisAlignment(cgf.cxxThisAlignment),
          sourceLocScope(e, cgf.curSourceLocExprScope) {
      cgf.cxxThisValue = cgf.cxxDefaultInitExprThis.getPointer();
      cgf.cxxThisAlignment = cgf.cxxDefaultInitExprThis.getAlignment();
    }
    ~CXXDefaultInitExprScope() {
      cgf.cxxThisValue = oldCXXThisValue;
      cgf.cxxThisAlignment = oldCXXThisAlignment;
    }

  public:
    CIRGenFunction &cgf;
    mlir::Value oldCXXThisValue;
    clang::CharUnits oldCXXThisAlignment;
    SourceLocExprScopeGuard sourceLocScope;
  };

  /// Temporarily override the active C++ 'this' pointer within a nested
  /// emission region.
  class CXXThisOverrideScope {
  public:
    CXXThisOverrideScope(CIRGenFunction &cgf, Address thisAddr,
                         bool overrideABIThis = false)
        : cgf(cgf), oldCXXABIThisValue(cgf.cxxabiThisValue),
          oldCXXThisValue(cgf.cxxThisValue),
          oldCXXABIThisAlignment(cgf.cxxabiThisAlignment),
          oldCXXThisAlignment(cgf.cxxThisAlignment),
          overrideABIThis(overrideABIThis) {
      if (overrideABIThis) {
        cgf.cxxabiThisValue = thisAddr.getPointer();
        cgf.cxxabiThisAlignment = thisAddr.getAlignment();
      }
      cgf.cxxThisValue = thisAddr.getPointer();
      cgf.cxxThisAlignment = thisAddr.getAlignment();
    }
    ~CXXThisOverrideScope() {
      if (overrideABIThis) {
        cgf.cxxabiThisValue = oldCXXABIThisValue;
        cgf.cxxabiThisAlignment = oldCXXABIThisAlignment;
      }
      cgf.cxxThisValue = oldCXXThisValue;
      cgf.cxxThisAlignment = oldCXXThisAlignment;
    }

  private:
    CIRGenFunction &cgf;
    mlir::Value oldCXXABIThisValue;
    mlir::Value oldCXXThisValue;
    clang::CharUnits oldCXXABIThisAlignment;
    clang::CharUnits oldCXXThisAlignment;
    bool overrideABIThis;
  };

  struct CXXDefaultArgExprScope : SourceLocExprScopeGuard {
    CXXDefaultArgExprScope(CIRGenFunction &cfg, const CXXDefaultArgExpr *e)
        : SourceLocExprScopeGuard(e, cfg.curSourceLocExprScope) {}
  };

  /// RAII scope for inlining an inherited constructor body. Temporarily
  /// replaces function-level state (CurGD, this, VTT, etc.) so that
  /// emitCtorPrologue sees the inherited constructor's initializers.
  /// Matches OGCG CodeGenFunction::InlinedInheritingConstructorScope.
  class InlinedInheritingConstructorScope {
  public:
    InlinedInheritingConstructorScope(CIRGenFunction &cgf,
                                      clang::GlobalDecl gd)
        : cgf(cgf), oldCurGD(cgf.curGD), oldCurFuncDecl(cgf.curFuncDecl),
          oldCurCodeDecl(cgf.curCodeDecl),
          oldCXXABIThisDecl(cgf.cxxabiThisDecl),
          oldCXXABIThisValue(cgf.cxxabiThisValue),
          oldCXXThisValue(cgf.cxxThisValue),
          oldCXXABIThisAlignment(cgf.cxxabiThisAlignment),
          oldCXXThisAlignment(cgf.cxxThisAlignment),
          oldReturnValue(cgf.returnValue),
          oldCXXInheritedCtorInitExprArgs(
              std::move(cgf.cxxInheritedCtorInitExprArgs)) {
      cgf.curGD = gd;
      cgf.curFuncDecl = cgf.curCodeDecl =
          cast<clang::CXXConstructorDecl>(gd.getDecl());
      cgf.cxxabiThisDecl = nullptr;
      cgf.cxxabiThisValue = nullptr;
      cgf.cxxThisValue = nullptr;
      cgf.cxxabiThisAlignment = clang::CharUnits();
      cgf.cxxThisAlignment = clang::CharUnits();
      cgf.returnValue = Address::invalid();
      cgf.cxxInheritedCtorInitExprArgs.clear();
    }
    ~InlinedInheritingConstructorScope() {
      cgf.curGD = oldCurGD;
      cgf.curFuncDecl = oldCurFuncDecl;
      cgf.curCodeDecl = oldCurCodeDecl;
      cgf.cxxabiThisDecl = oldCXXABIThisDecl;
      cgf.cxxabiThisValue = oldCXXABIThisValue;
      cgf.cxxThisValue = oldCXXThisValue;
      cgf.cxxabiThisAlignment = oldCXXABIThisAlignment;
      cgf.cxxThisAlignment = oldCXXThisAlignment;
      cgf.returnValue = oldReturnValue;
      cgf.cxxInheritedCtorInitExprArgs =
          std::move(oldCXXInheritedCtorInitExprArgs);
    }

  private:
    CIRGenFunction &cgf;
    clang::GlobalDecl oldCurGD;
    const clang::Decl *oldCurFuncDecl;
    const clang::Decl *oldCurCodeDecl;
    ImplicitParamDecl *oldCXXABIThisDecl;
    mlir::Value oldCXXABIThisValue;
    mlir::Value oldCXXThisValue;
    clang::CharUnits oldCXXABIThisAlignment;
    clang::CharUnits oldCXXThisAlignment;
    Address oldReturnValue;
    CallArgList oldCXXInheritedCtorInitExprArgs;
  };

  LValue makeNaturalAlignPointeeAddrLValue(mlir::Value v, clang::QualType t);
  LValue makeNaturalAlignAddrLValue(mlir::Value val, QualType ty);

  /// Construct an address with the natural alignment of T. If a pointer to T
  /// is expected to be signed, the pointer passed to this function must have
  /// been signed, and the returned Address will have the pointer authentication
  /// information needed to authenticate the signed pointer.
  Address makeNaturalAddressForPointer(mlir::Value ptr, QualType t,
                                       CharUnits alignment,
                                       bool forPointeeType = false,
                                       LValueBaseInfo *baseInfo = nullptr,
                                       TBAAAccessInfo *tbaaInfo = nullptr) {
    if (alignment.isZero())
      alignment =
          cgm.getNaturalTypeAlignment(t, baseInfo, tbaaInfo, forPointeeType);
    // Use the pointer's actual pointee type as the element type. This avoids
    // mismatches when the pointer's pointee (from convertType) differs from
    // the storage type (from convertTypeForMem), e.g. for _BitInt types where
    // the pointer carries the semantic width but memory uses storage width.
    auto ptrTy = mlir::cast<cir::PointerType>(ptr.getType());
    return Address(ptr, ptrTy.getPointee(), alignment);
  }

  Address getAddressOfBaseClass(
      Address value, const CXXRecordDecl *derived,
      llvm::iterator_range<CastExpr::path_const_iterator> path,
      bool nullCheckValue, SourceLocation loc);

  Address getAddressOfDerivedClass(
      mlir::Location loc, Address baseAddr, const CXXRecordDecl *derived,
      llvm::iterator_range<CastExpr::path_const_iterator> path,
      bool nullCheckValue);

  /// Return the VTT parameter that should be passed to a base
  /// constructor/destructor with virtual bases.
  /// FIXME: VTTs are Itanium ABI-specific, so the definition should move
  /// to ItaniumCXXABI.cpp together with all the references to VTT.
  mlir::Value getVTTParameter(GlobalDecl gd, bool forVirtualBase,
                              bool delegating);

  LValue makeAddrLValue(Address addr, QualType ty,
                        AlignmentSource source = AlignmentSource::Type) {
    return makeAddrLValue(addr, ty, LValueBaseInfo(source),
                          cgm.getTBAAAccessInfo(ty));
  }

  LValue makeAddrLValue(Address addr, QualType ty, LValueBaseInfo baseInfo) {
    return makeAddrLValue(addr, ty, baseInfo, cgm.getTBAAAccessInfo(ty));
  }

  LValue makeAddrLValue(Address addr, QualType ty, LValueBaseInfo baseInfo,
                        TBAAAccessInfo tbaaInfo) {
    LValue lv = LValue::makeAddr(addr, ty, baseInfo);
    lv.setTBAAInfo(tbaaInfo);
    return lv;
  }

  void initializeVTablePointers(mlir::Location loc,
                                const clang::CXXRecordDecl *rd);
  void initializeVTablePointer(mlir::Location loc, const VPtr &vptr);

  AggValueSlot::Overlap_t getOverlapForFieldInit(const FieldDecl *fd);

  /// Return the address of a local variable.
  Address getAddrOfLocalVar(const clang::VarDecl *vd) {
    auto it = localDeclMap.find(vd);
    assert(it != localDeclMap.end() &&
           "Invalid argument to getAddrOfLocalVar(), no decl!");
    return it->second;
  }

  Address getAddrOfBitFieldStorage(LValue base, const clang::FieldDecl *field,
                                   mlir::Type fieldType, unsigned index);

  /// Given an opaque value expression, return its LValue mapping if it exists,
  /// otherwise create one.
  LValue getOrCreateOpaqueLValueMapping(const OpaqueValueExpr *e);

  /// Given an opaque value expression, return its RValue mapping if it exists,
  /// otherwise create one.
  RValue getOrCreateOpaqueRValueMapping(const OpaqueValueExpr *e);

  /// Load the value for 'this'. This function is only valid while generating
  /// code for an C++ member function.
  /// FIXME(cir): this should return a mlir::Value!
  mlir::Value loadCXXThis() {
    assert(cxxThisValue && "no 'this' value for this function");
    return cxxThisValue;
  }
  Address loadCXXThisAddress();

  /// Load the VTT parameter to base constructors/destructors have virtual
  /// bases. FIXME: Every place that calls LoadCXXVTT is something that needs to
  /// be abstracted properly.
  mlir::Value loadCXXVTT() {
    assert(cxxStructorImplicitParamValue && "no VTT value for this function");
    return cxxStructorImplicitParamValue;
  }

  /// Convert the given pointer to a complete class to the given direct base.
  Address getAddressOfDirectBaseInCompleteClass(mlir::Location loc,
                                                Address value,
                                                const CXXRecordDecl *derived,
                                                const CXXRecordDecl *base,
                                                bool baseIsVirtual);

  /// Determine whether a return value slot may overlap some other object.
  AggValueSlot::Overlap_t getOverlapForReturnValue() {
    // FIXME: Assuming no overlap here breaks guaranteed copy elision for base
    // class subobjects. These cases may need to be revisited depending on the
    // resolution of the relevant core issue.
    return AggValueSlot::DoesNotOverlap;
  }

  /// Determine whether a base class initialization may overlap some other
  /// object.
  AggValueSlot::Overlap_t getOverlapForBaseInit(const CXXRecordDecl *rd,
                                                const CXXRecordDecl *baseRD,
                                                bool isVirtual);

  /// Get an appropriate 'undef' rvalue for the given type.
  /// TODO: What's the equivalent for MLIR? Currently we're only using this for
  /// void types so it just returns RValue::get(nullptr) but it'll need
  /// addressed later.
  RValue getUndefRValue(clang::QualType ty);

  cir::FuncOp generateCode(clang::GlobalDecl gd, cir::FuncOp fn,
                           cir::FuncType funcType);
  cir::FuncOp generateBlockFunctionCode(const clang::BlockExpr *blockExpr,
                                        cir::FuncOp fn,
                                        cir::FuncType funcType);

  /// Generate an Objective-C method. ObjC methods are not FunctionDecls,
  /// so they need a separate code generation path.
  void generateObjCMethod(const clang::ObjCMethodDecl *omd);
  void generateObjCConstructorMethod(clang::ObjCImplementationDecl *imp,
                                     clang::ObjCMethodDecl *omd);
  void generateObjCDestructorMethod(clang::ObjCImplementationDecl *imp,
                                    clang::ObjCMethodDecl *omd);
  cir::FuncOp
  generateObjCAtomicGetterCopyHelperFunction(
      const clang::ObjCPropertyImplDecl *pid);
  cir::FuncOp
  generateObjCAtomicSetterCopyHelperFunction(
      const clang::ObjCPropertyImplDecl *pid);
  void generateObjCPropertyGetter(const clang::ObjCImplementationDecl *imp,
                                  const clang::ObjCPropertyImplDecl *pid);
  void generateObjCPropertySetter(const clang::ObjCImplementationDecl *imp,
                                  const clang::ObjCPropertyImplDecl *pid);

  clang::QualType buildFunctionArgList(clang::GlobalDecl gd,
                                       FunctionArgList &args);

  /// Emit the function prologue: declare function arguments in the symbol
  /// table.
  void emitFunctionProlog(const FunctionArgList &args, mlir::Block *entryBB,
                          const FunctionDecl *fd, SourceLocation bodyBeginLoc);

  /// Emit code for the start of a function.
  /// \param loc       The location to be associated with the function.
  /// \param startLoc  The location of the function body.
  void startFunction(clang::GlobalDecl gd, clang::QualType returnType,
                     cir::FuncOp fn, cir::FuncType funcType,
                     FunctionArgList args, clang::SourceLocation loc,
                     clang::SourceLocation startLoc);

  /// returns true if aggregate type has a volatile member.
  bool hasVolatileMember(QualType t) {
    if (const auto *rd = t->getAsRecordDecl())
      return rd->hasVolatileMember();
    return false;
  }

  void populateUnwindResumeBlock(bool isCleanup, cir::TryOp tryOp);
  void populateEHCatchRegions(EHScopeStack::stable_iterator scope,
                              cir::TryOp tryOp);

  /// Emit, into the current insertion point, the EH bodies of the active EH
  /// cleanups on the stack (innermost outward, stopping at the first
  /// non-Cleanup scope), each guarded by its runtime active flag. This
  /// materializes ONE shared cleanup dispatch for a cir.cleanup.scope's cleanup
  /// region — the same chain populateEHCatchRegions emits per throwing call,
  /// but emitted once and shared by all body calls (avoiding the O(N^2)
  /// per-call materialization). Only valid when every emitted cleanup's
  /// addresses dominate the enclosing cir.cleanup.scope op (array cursors,
  /// auto-var allocas, bases hoisted before the op).
  ///
  /// The walk stops at `boundary` (exclusive), so passing the EH depth captured
  /// at scope entry emits ONLY this scope's own cleanups; enclosing cleanups
  /// run by chaining the flattened landing pads (FlattenCFG), avoiding O(depth)
  /// duplication across nested cir.cleanup.scopes.
  ///
  /// When `sharedScopeChunkFilter` is nonzero, only cleanups pushed during
  /// that chunk of a chunked initializer_list array emission are emitted;
  /// other chunks reach their own nested pad through the same outward pad
  /// chaining. Zero (the default) emits everything down to `boundary`.
  void emitSharedCleanupScopeDispatch(EHScopeStack::stable_iterator boundary,
                                      unsigned sharedScopeChunkFilter = 0);

  /// Move the range pushCleanup marked for `scope` into a cir.cleanup.scope,
  /// unwrap the per-call synthetic `cir.try`s inside it, and emit this
  /// cleanup's EH destructor into the op's cleanup region -- so every throwing
  /// call in the range shares ONE landing pad, which is OGCG's shape. Returns
  /// the op it built, or null (changing nothing) if the range cannot be moved,
  /// in which case the already-emitted per-call tries remain and stay correct.
  cir::CleanupScopeOp formCleanupScopeOp(EHCleanupScope &scope);

  /// Append `scope`'s EH destructor to every pendingSharedCleanupScopes entry
  /// except `skip`. A scope this cleanup neither formed nor encloses -- a
  /// sibling of the one it formed, say -- reaches this destructor no other
  /// way: FlattenCFG chains a pad outward through the scopes it is nested in,
  /// never sideways. `skip` is the scope this same cleanup just formed, which
  /// already emitted the destructor into its own region.
  void appendCleanupToPendingSharedScopes(EHCleanupScope &scope,
                                          cir::CleanupScopeOp skip = {});

  /// Enrol a cir.cleanup.scope built outside formCleanupScopeOp -- the one
  /// CIRGenExprAggregate wraps an initializer_list's element constructors in --
  /// in the same protocol, so the cleanups still live across it are appended to
  /// its region as they pop. Its body holds no per-call synthetic tries to fall
  /// back on, so unlike a formed scope it is never undone. `dispatchBoundary`
  /// is the boundary the region's own dispatch stopped at: cleanups inside it
  /// are already there and must not be appended a second time.
  void
  registerSharedCleanupScope(cir::CleanupScopeOp op,
                             EHScopeStack::stable_iterator dispatchBoundary);

  mlir::Value emitBlockLiteral(const clang::BlockExpr *e);
  RValue emitBlockCallExpr(const clang::CallExpr *e,
                           ReturnValueSlot returnValue, bool ignoreResult);

  /// Tracks the current call operation that may throw, used to populate
  /// its cleanup region during exception handling.
  cir::CallOp callWithExceptionCtx = nullptr;

  /// Boundary for per-call cleanup scope walking. When set to a valid
  /// scope iterator, populateEHCatchRegions will stop its cleanup walk
  /// before reaching this scope. This prevents including pre-existing RAII
  /// scopes that are already handled by an enclosing synthetic TryOp's
  /// handler (e.g., from emitCXXThrowExpr). Without this boundary, cleanup
  /// scopes like ~Guard() would be emitted both in the per-call cleanup
  /// and in the enclosing TryOp handler, causing double-destroy.
  EHScopeStack::stable_iterator ehCleanupWalkBoundary =
      EHScopeStack::stable_end();

  /// True when emitting the body of an EH cleanup scope. Per C++
  /// [except.terminate], if a cleanup itself throws during unwinding,
  /// std::terminate is called. This flag prevents wrapping sub-calls in
  /// new TryOps during cleanup emission, breaking the infinite recursion
  /// that occurs with noexcept(false) destructors.
  bool isEmittingEHCleanup = false;

  /// Active synthetic TryOp shared across calls at the same EH cleanup depth.
  /// When consecutive potentially-throwing calls at the same cleanup depth
  /// have operands that dominate this TryOp, they are inserted into this
  /// TryOp's try body. The first call gets full per-call cleanup emission;
  /// subsequent calls get empty cleanup regions. FlattenCFG shares landing
  /// pads for calls with empty cleanup regions.
  cir::TryOp activeSyntheticTry;

  /// The innermost EH scope when activeSyntheticTry was created.
  EHScopeStack::stable_iterator activeSyntheticTryScope =
      EHScopeStack::stable_end();

  /// When true, emitCallLikeOp emits a throwing call as a bare
  /// `cir.call exception` (no per-call synthetic `cir.try`). The enclosing
  /// `cir.cleanup.scope` provides the single shared landing pad (wired up in
  /// FlattenCFG), so all throwing calls in the scope body share ONE cleanup
  /// dispatch instead of each materializing the active-destructor prefix (the
  /// O(N^2) source). The scope's cleanup region is emitted separately using
  /// only op-dominating values (cursor-relative recompute).
  bool emittingSharedCleanupScope = false;

  /// Which chunk of a chunked initializer_list array emission is being
  /// emitted, or 0 outside one. pushCleanup stamps it onto every EH cleanup
  /// so each nested cir.cleanup.scope's dispatch can emit only its own
  /// chunk's cleanups. Chunks are 1-based; 0 means "unchunked" and matches
  /// the unfiltered dispatch every other caller uses.
  unsigned currentSharedScopeChunkId = 0;

  /// Allocator for the chunk ids above. A chunked array emission takes a
  /// contiguous range (one id per chunk) so a nested array's chunks never
  /// collide with an enclosing array's: sharing an id would emit the inner
  /// array's cleanups into the outer chunk's dispatch as well as its own.
  unsigned nextSharedScopeChunkId = 1;

  /// Source of the ids handed to EH cleanups so a synthetic `cir.try` can name
  /// the cleanup that was innermost when it was created (see
  /// kSyntheticTryCleanupIdAttr). Ids are never reused within a function, which
  /// pointer identity of the popped-and-reallocated EHCleanupScope would not
  /// give us.
  unsigned ehCleanupIdCounter = 0;

  /// Discardable attribute on a synthetic `cir.try`: the id of the EH cleanup
  /// that was innermost when it was created, i.e. whose destructor prefix its
  /// per-call cleanup regions carry. formCleanupScopeOp only unwraps a try
  /// whose id is the cleanup being popped -- any other id means the try's
  /// cleanup runs destructors the shared dispatch would not reproduce.
  static constexpr llvm::StringLiteral kSyntheticTryCleanupIdAttr =
      "cir.synthetic_try_cleanup_id";

  /// Id of the innermost EH cleanup, or 0 if the innermost EH scope is not a
  /// cleanup (or the stack is empty).
  unsigned getInnermostEHCleanupId();

  /// A `cir.cleanup.scope` built by formCleanupScopeOp that is not yet nested
  /// inside another one, paired with the id of the cleanup that built it.
  /// FlattenCFG chains a nested scope's pad to its parent scope's, so an
  /// enclosing cleanup that does *not* form a scope of its own has nothing to
  /// run its destructor on their unwind edge -- popCleanupBlock appends it to
  /// these instead -- but only the cleanups whose guarded range overlaps the
  /// scope, which the formation sequence number identifies.
  struct PendingSharedCleanupScope {
    cir::CleanupScopeOp op;
    unsigned formationSeq;
    /// The synthetic `cir.try`s in `op`'s body that its shared pad has made
    /// redundant. Unwrapping them is the one step forming a scope cannot take
    /// back, so it waits until no cleanup can be appended to `op` any more
    /// (sweepFinalizedCleanupScopes); until then an append that turns out to
    /// break dominance can still undo the whole scope.
    llvm::SmallVector<cir::TryOp, 8> unwrappable;
    /// The scope `op` came to be nested inside, if one formed around it.
    /// FlattenCFG chains `op`'s pad into that one, which takes the appends on
    /// its behalf -- and takes `op` down with it if it is undone.
    cir::CleanupScopeOp coveredBy;
    /// Whether `op` can be undone when an append breaks dominance. A scope
    /// formed from a range can: the range goes back where it came from, with
    /// the per-call synthetic tries it still has. One built directly around
    /// calls that never got a try of their own cannot -- undoing it would
    /// leave them with no unwind edge at all -- so the append is rolled back
    /// instead of the scope.
    bool undoable = true;
    /// For a scope registered with a cleanup region it wrote itself: the
    /// boundary that region's dispatch stopped at. Everything inside it was
    /// already emitted there, so appending it again would destroy the same
    /// object twice. Invalid for a scope formed from a range, whose region
    /// starts out empty.
    EHScopeStack::stable_iterator dispatchBoundary =
        EHScopeStack::stable_iterator::invalid();
  };
  llvm::SmallVector<PendingSharedCleanupScope, 4> pendingSharedCleanupScopes;

  /// Undo `op`: put its body back where it came from and erase it, along with
  /// every scope still nested inside it, which has missed the appends `op` took
  /// in their stead and cannot be given them now.
  void rollbackCleanupScope(cir::CleanupScopeOp op);

  /// Unwrap the redundant synthetic tries of every pending scope no cleanup
  /// left on the EH stack can still append to.
  void sweepFinalizedCleanupScopes();

  /// Spill every value a `cir.cleanup.scope` body defines and something outside
  /// the scope names, so the consumer reads it from a slot instead of from a
  /// definition the region confines.
  void spillValuesEscapingCleanupScopes();

  /// Stamped onto each formed scope and snapshotted by every EH cleanup at
  /// push time; see EHCleanupScope::scopeSeqAtPush.
  unsigned cleanupScopeSeqCounter = 0;

  /// Give `scope` an EH active flag covering exactly its guarded range, so a
  /// shared cir.cleanup.scope can still run it after it is deactivated.
  void giveCleanupEHDeactivationFlag(EHCleanupScope &scope);

  /// True while emitting a std::initializer_list backing array (set in
  /// VisitCXXStdInitializerListExpr). emitArrayInit uses the shared
  /// cir.cleanup.scope path only for these arrays — that is where the O(N^2)
  /// per-call cleanup materialization actually hurts (large brace-init tables /
  /// nested initializer_lists). Standalone
  /// `T arr[N]` and reduction-recipe arrays keep the per-call path.
  bool emittingInitializerListArray = false;

  /// Invalidate the active synthetic TryOp tracking (e.g., when EH scope
  /// depth changes due to cleanup push/pop).
  void closeActiveSyntheticTry();

  /// The cleanup depth enclosing all the cleanups associated with the
  /// parameters.
  EHScopeStack::stable_iterator prologueCleanupDepth;

  bool isCatchOrCleanupRequired();
  void populateCatchHandlersIfRequired(cir::TryOp tryOp);

  /// Thread the active EH cleanups (RAII destructors between the throwing call
  /// and the enclosing try) onto a compiler-synthesized noreturn throwing call
  /// (e.g. __cxa_bad_cast / __cxa_bad_typeid) that sits inside a user try, so
  /// those destructors run on the call's unwind edge. No-op when there is no
  /// enclosing user try or when the enclosing try is a synthetic cleanup-only
  /// try (that path emits the cleanups eagerly in its handler, so threading
  /// them per-call too would destroy the locals twice).
  void threadUserTryCleanupsOntoThrowingCall(cir::CallOp callOp);

  /// Takes the old cleanup stack size and emits the cleanup blocks
  /// that have been added.
  void popCleanupBlocks(EHScopeStack::stable_iterator oldCleanupStackDepth);
  void popCleanupBlocks(EHScopeStack::stable_iterator oldCleanupStackDepth,
                        size_t oldLifetimeExtendedSize);
  void popCleanupBlock();

  /// Emit cleanup code for all active normal cleanups between \p start and
  /// \p depth, WITHOUT popping them. Used for early exits
  /// (break/continue/goto/return) where cleanups must run on the exit path but
  /// remain on the stack for the normal path. If \p start is invalid (the
  /// default), walks from the current ehStack top.
  void emitCleanupsForEarlyExit(
      EHScopeStack::stable_iterator depth,
      EHScopeStack::stable_iterator start =
          EHScopeStack::stable_iterator::invalid());

  /// Deactivate a cleanup that was created in a active state.
  void deactivateCleanupBlock(EHScopeStack::stable_iterator cleanup,
                              mlir::Operation *dominatingIP);

  /// Create an alloca for a conditional cleanup guard (i1/bool). The flag
  /// is initialized to false at the outermost conditional entry point and
  /// set to true at the current insertion point. Matches OGCG's
  /// createCleanupActiveFlag (CGCleanup.cpp:275-288).
  ///
  /// \param initBlock when non-null, the false-initialization is emitted at
  /// the start of this block instead of right after the alloca. The flag
  /// alloca lives in the function entry block, which runs once, so a guard
  /// for a cleanup that can be re-entered (a scope inside a loop) must be
  /// reset on every entry to that scope.
  Address createCleanupActiveFlag(mlir::Block *initBlock = nullptr);

  /// Bind an active flag to the topmost cleanup scope. Sets the scope's
  /// testFlagInNormalCleanup and testFlagInEHCleanup bits so that
  /// popCleanupBlock will pass the flag to emitCleanup. Matches OGCG's
  /// initFullExprCleanupWithFlag (CGCleanup.cpp:290-298).
  void initFullExprCleanupWithFlag(Address activeFlag);

  /// Push a cleanup to be run at the end of the current full-expression.  Safe
  /// against the possibility that we're currently inside a
  /// conditionally-evaluated expression.
  template <class T, class... As>
  void pushFullExprCleanup(CleanupKind kind, As... a) {
    // If we're not in a conditional branch, or if none of the
    // arguments requires saving, then use the unconditional cleanup.
    if (!isInConditionalBranch())
      return ehStack.pushCleanup<T>(kind, a...);

    // In a conditional branch (e.g., ternary expression), create an
    // active flag to guard this cleanup. The flag is initialized to false
    // at the outermost conditional entry and set to true here. When the
    // cleanup is popped, it will only fire if the flag is true (i.e.,
    // this branch was actually taken and the temporary was constructed).
    Address activeFlag = createCleanupActiveFlag();
    ehStack.pushCleanup<T>(kind, a...);
    initFullExprCleanupWithFlag(activeFlag);
  }

  /// Push a cleanup to be executed after the current full-expression is
  /// complete (i.e., at the enclosing scope level rather than at the end
  /// of the current full expression). Used for lifetime-extended temporaries.
  template <class T, class... As>
  void pushCleanupAfterFullExpr(CleanupKind kind, As... a) {
    if (!isInConditionalBranch())
      return pushCleanupAfterFullExprWithActiveFlag<T>(kind, Address::invalid(),
                                                       a...);

    // In a conditional branch: create an active flag to guard the cleanup.
    Address activeFlag = createCleanupActiveFlag();
    pushCleanupAfterFullExprWithActiveFlag<T>(kind, activeFlag, a...);
  }

  /// Push a cleanup to be executed after the current full-expression is
  /// complete, with an optional active flag for conditional cleanup support.
  /// When activeFlag is valid, the flag is stored alongside the cleanup and
  /// used to guard whether the cleanup should fire (matching OGCG's
  /// pushCleanupAfterFullExprWithActiveFlag in CodeGenFunction.h:941-959).
  template <class T, class... As>
  void pushCleanupAfterFullExprWithActiveFlag(CleanupKind kind,
                                              Address activeFlag, As... a) {
    LifetimeExtendedCleanupHeader header = {sizeof(T), kind,
                                            activeFlag.isValid()};

    size_t oldSize = lifetimeExtendedCleanupStack.size();
    lifetimeExtendedCleanupStack.resize(
        oldSize + sizeof(header) + header.size +
        (header.isConditional ? sizeof(activeFlag) : 0));

    static_assert(alignof(LifetimeExtendedCleanupHeader) <= alignof(T),
                  "Cleanup alignment mismatch");
    char *buffer = &lifetimeExtendedCleanupStack[oldSize];
    new (buffer) LifetimeExtendedCleanupHeader(header);
    new (buffer + sizeof(header)) T(a...);
    if (header.isConditional)
      new (buffer + sizeof(header) + sizeof(T)) Address(activeFlag);
  }

  /// RAII scope that deactivates all deferred cleanup deactivations pushed
  /// during its lifetime. Used during aggregate initialization where each
  /// base/field constructor pushes a destroy cleanup for exception safety.
  /// When the aggregate is fully constructed, the cleanups are deactivated
  /// so normal control flow doesn't double-destroy.
  class CleanupDeactivationScope {
    CIRGenFunction &cgf;
    size_t oldDeactivateCleanupStackSize;
    bool deactivated = false;

  public:
    explicit CleanupDeactivationScope(CIRGenFunction &cgf)
        : cgf(cgf), oldDeactivateCleanupStackSize(
                         cgf.deferredDeactivationCleanupStack.size()) {}

    void deferDeactivationToOuterScope() {
      assert(!deactivated && "Already deactivated");
      oldDeactivateCleanupStackSize =
          cgf.deferredDeactivationCleanupStack.size();
    }

    void forceDeactivate() {
      assert(!deactivated && "Already deactivated");
      auto &stack = cgf.deferredDeactivationCleanupStack;
      for (size_t i = stack.size(); i > oldDeactivateCleanupStackSize; --i) {
        auto &entry = stack[i - 1];
        // Nested RunCleanupsScope instances can pop inner cleanup scopes before
        // an outer CleanupDeactivationScope runs. In that case the saved
        // stable_iterator no longer points to a live scope. Skip stale entries
        // instead of dereferencing with EHScopeStack::find, which would assert
        // ("finding savepoint after pop"). This matches OGCG's intent: a
        // cleanup that was already popped does not need deactivation.
        if (!cgf.ehStack.contains(entry.cleanup))
          continue;
        cgf.deactivateCleanupBlock(entry.cleanup, entry.dominatingIP);
        // OGCG erases the dummy DominatingIP instruction here.
        // CIR passes nullptr for now (deactivateCleanupBlock handles it).
      }
      stack.resize(oldDeactivateCleanupStackSize);
      deactivated = true;
    }

    ~CleanupDeactivationScope() {
      if (!deactivated)
        forceDeactivate();
    }
  };

  /// Enters a new scope for capturing cleanups, all of which
  /// will be executed once the scope is exited.
  class RunCleanupsScope {
    EHScopeStack::stable_iterator cleanupStackDepth, oldCleanupStackDepth;
    size_t lifetimeExtendedCleanupStackSize;
    CleanupDeactivationScope deactivateCleanups;

  protected:
    bool performCleanup;
    bool oldDidCallStackSave;

  private:
    RunCleanupsScope(const RunCleanupsScope &) = delete;
    void operator=(const RunCleanupsScope &) = delete;

  protected:
    CIRGenFunction &cgf;

  public:
    /// Enter a new cleanup scope.
    explicit RunCleanupsScope(CIRGenFunction &cgf)
        : deactivateCleanups(cgf), performCleanup(true), cgf(cgf) {
      cleanupStackDepth = cgf.ehStack.stable_begin();
      lifetimeExtendedCleanupStackSize =
          cgf.lifetimeExtendedCleanupStack.size();
      oldDidCallStackSave = cgf.didCallStackSave;
      cgf.didCallStackSave = false;
      oldCleanupStackDepth = cgf.currentCleanupStackDepth;
      cgf.currentCleanupStackDepth = cleanupStackDepth;
    }

    /// Return the cleanup stack depth at the point this scope was entered.
    EHScopeStack::stable_iterator getCleanupStackDepth() const {
      return cleanupStackDepth;
    }

    /// Defer all cleanups pushed during this scope to the parent scope.
    /// After calling this, forceCleanup() will not pop any of the cleanups
    /// pushed since this scope was entered — they remain on the ehStack
    /// and will be popped by an outer RunCleanupsScope. Used for ternary
    /// branch scopes where temporaries must outlive the branch.
    void deferCleanupsToOuterScope() {
      cleanupStackDepth = cgf.ehStack.stable_begin();
      lifetimeExtendedCleanupStackSize =
          cgf.lifetimeExtendedCleanupStack.size();
      deactivateCleanups.deferDeactivationToOuterScope();
    }

    /// Exit this cleanup scope, emitting any accumulated cleanups.
    ~RunCleanupsScope() {
      if (performCleanup)
        forceCleanup();
    }

    /// Force the emission of cleanups now, instead of waiting
    /// until this object is destroyed.
    void forceCleanup() {
      assert(performCleanup && "Already forced cleanup");
      {
        mlir::OpBuilder::InsertionGuard guard(cgf.getBuilder());
        // Deactivate deferred cleanups BEFORE popping cleanup blocks.
        deactivateCleanups.forceDeactivate();
        cgf.didCallStackSave = oldDidCallStackSave;
        cgf.popCleanupBlocks(cleanupStackDepth,
                             lifetimeExtendedCleanupStackSize);
        performCleanup = false;
        cgf.currentCleanupStackDepth = oldCleanupStackDepth;
      }
      // If a noreturn cleanup was emitted (e.g., a [[noreturn]] destructor),
      // it inserted a terminator (cir.unreachable) into the block and created
      // a new dummy block. The InsertionGuard above restored the insertion
      // point back to the original block, which now has a terminator and
      // cannot accept new operations. Move the insertion point to the next
      // block (the dummy block already created by the noreturn handler).
      if (auto *block = cgf.getBuilder().getInsertionBlock()) {
        if (!block->empty() &&
            mlir::isa<cir::UnreachableOp, cir::TrapOp>(block->back())) {
          auto *region = block->getParent();
          auto it = block->getIterator();
          ++it;
          if (it != region->end())
            cgf.getBuilder().setInsertionPointToStart(&*it);
          // If there's no next block, the unreachable is a placeholder
          // (not from a noreturn cleanup), so leave the IP unchanged.
        }
      }
    }
  };

  // Cleanup stack depth of the RunCleanupsScope that was pushed most recently.
  EHScopeStack::stable_iterator currentCleanupStackDepth = ehStack.stable_end();

public:
  /// Represents a scope, including function bodies, compound statements, and
  /// the substatements of if/while/do/for/switch/try statements.  This class
  /// handles any automatic cleanup, along with the return value.
  struct LexicalScope : public RunCleanupsScope {
  private:
    // Block containing cleanup code for things initialized in this
    // lexical context (scope).
    mlir::Block *cleanupBlock = nullptr;

    // Points to the scope entry block. This is useful, for instance, for
    // helping to insert allocas before finalizing any recursive CodeGen from
    // switches.
    mlir::Block *entryBlock;

    LexicalScope *parentScope = nullptr;

    // Holds the actual value for ScopeKind::Try
    cir::TryOp tryOp = nullptr;

    // On a coroutine body, the OnFallthrough sub stmt holds the handler
    // (CoreturnStmt) for control flow falling off the body. Keep track
    // of emitted co_return in this scope and allow OnFallthrough to be
    // skipeed.
    bool hasCoreturnStmt = false;

    enum Kind {
      Regular,   // cir.if, cir.scope, if_regions
      Ternary,   // cir.ternary
      Switch,    // cir.switch
      Loop,      // cir.for, cir.while, cir.do_while
      Try,       // cir.try
      GlobalInit // cir.global initialization code
    };
    Kind scopeKind = Kind::Regular;

    // The scope return value.
    mlir::Value retVal = nullptr;

    mlir::Location beginLoc;
    mlir::Location endLoc;

    // Clang source range for this scope's AST node (e.g. CompoundStmt or
    // function body). Used by emitGotoStmt to determine whether a forward
    // goto's target label is inside or outside this scope.
    clang::SourceRange clangSourceRange;

    // When true, this scope only handles cleanup (RunCleanupsScope) without
    // emitting cir.yield or other scope termination ops. Used for label
    // sub-statements that are CompoundStmts — they need cleanup tracking
    // and scope chain visibility but don't have a cir.scope op.
    bool cleanupOnly = false;

    // Labels emitted in this scope. Used to detect when a variable's
    // lifetime markers should be suppressed in C (where a backward jump
    // to a label can re-enter the variable's scope without re-initializing).
    llvm::SmallVector<const clang::LabelDecl *, 4> labels;

    // Override cleanup depths for break/continue when a condition variable
    // with a non-trivial destructor is declared in a loop condition.
    // Without these, break/continue emit the condition variable's destructor
    // inline AND the scope exit / step dtor emit it again (double-destroy).
    // These depths exclude the condition variable cleanup so break/continue
    // skip it, letting the scope exit or step dtor handle it instead.
    std::optional<EHScopeStack::stable_iterator> breakTargetDepth;
    std::optional<EHScopeStack::stable_iterator> continueTargetDepth;

    // Memoizes applySourceScopeMetadata results for this scope. The decorated
    // location is a pure function of (incoming loc, this resolved scope), and
    // the same pair recurs for every op sharing a source location within the
    // scope, so caching avoids rebuilding the scope-chain attrs + DictionaryAttr
    // + FusedLoc on every getLoc(). Lifetime is tied to the scope object.
    llvm::DenseMap<mlir::Location, mlir::Location> diScopeLocCache;

  public:
    unsigned depth = 0;

    llvm::DenseMap<mlir::Location, mlir::Location> &getDIScopeLocCache() {
      return diScopeLocCache;
    }

    LexicalScope(CIRGenFunction &cgf, mlir::Location loc, mlir::Block *eb)
        : RunCleanupsScope(cgf), entryBlock(eb), parentScope(cgf.curLexScope),
          beginLoc(loc), endLoc(loc) {

      assert(entryBlock && "LexicalScope requires an entry block");
      cgf.curLexScope = this;
      if (parentScope)
        depth = parentScope->depth + 1;

      if (const auto fusedLoc = mlir::dyn_cast<mlir::FusedLoc>(loc);
          fusedLoc && fusedLoc.getLocations().size() == 2) {
        beginLoc = fusedLoc.getLocations()[0];
        endLoc = fusedLoc.getLocations()[1];
      }
    }

    void setRetVal(mlir::Value v) { retVal = v; }
    mlir::Location getBeginLoc() const { return beginLoc; }
    LexicalScope *getParentScope() const { return parentScope; }

    void setClangSourceRange(clang::SourceRange sr) { clangSourceRange = sr; }
    clang::SourceRange getClangSourceRange() const { return clangSourceRange; }

    void setCleanupOnly() { cleanupOnly = true; }
    bool isCleanupOnly() const { return cleanupOnly; }

    void addLabel(const clang::LabelDecl *label) { labels.push_back(label); }
    bool hasLabels() const { return !labels.empty(); }

    void cleanup();
    void restore() { cgf.curLexScope = parentScope; }

    /// Adjust label scope depths when this scope exits, matching OGCG's
    /// rescopeLabels (CGStmt.cpp:758-776).
    void rescopeLabels();

    ~LexicalScope() {
      cleanup();
      if (hasLabels())
        rescopeLabels();
      restore();
    }

    // ---
    // Coroutine tracking
    // ---
    bool hasCoreturn() const { return hasCoreturnStmt; }
    void setCoreturn() { hasCoreturnStmt = true; }

    // ---
    // Kind
    // ---
    bool isGlobalInit() { return scopeKind == Kind::GlobalInit; }
    bool isLoop() { return scopeKind == Kind::Loop; }
    bool isRegular() { return scopeKind == Kind::Regular; }
    bool isSwitch() { return scopeKind == Kind::Switch; }
    bool isTernary() { return scopeKind == Kind::Ternary; }
    bool isTry() { return scopeKind == Kind::Try; }
    cir::TryOp getClosestTryParent();
    void setAsGlobalInit() { scopeKind = Kind::GlobalInit; }
    void setAsLoop() { scopeKind = Kind::Loop; }
    void setAsSwitch() { scopeKind = Kind::Switch; }
    void setAsTernary() { scopeKind = Kind::Ternary; }
    void setAsTry(cir::TryOp op) {
      scopeKind = Kind::Try;
      tryOp = op;
    }

    // ---
    // Condition variable cleanup depth overrides
    // ---
    void setBreakTargetDepth(EHScopeStack::stable_iterator d) {
      breakTargetDepth = d;
    }
    void setContinueTargetDepth(EHScopeStack::stable_iterator d) {
      continueTargetDepth = d;
    }
    std::optional<EHScopeStack::stable_iterator> getBreakTargetDepth() const {
      return breakTargetDepth;
    }
    std::optional<EHScopeStack::stable_iterator> getContinueTargetDepth() const {
      return continueTargetDepth;
    }

    // Lazy create cleanup block or return what's available.
    mlir::Block *getOrCreateCleanupBlock(mlir::OpBuilder &builder) {
      if (cleanupBlock)
        return cleanupBlock;
      cleanupBlock = createCleanupBlock(builder);
      return cleanupBlock;
    }

    cir::TryOp getTry() {
      assert(isTry());
      return tryOp;
    }

    mlir::Block *getCleanupBlock(mlir::OpBuilder &builder) {
      return cleanupBlock;
    }

    mlir::Block *createCleanupBlock(mlir::OpBuilder &builder) {
      // Create the cleanup block but dont hook it up around just yet.
      mlir::OpBuilder::InsertionGuard guard(builder);
      mlir::Region *r = builder.getBlock() ? builder.getBlock()->getParent()
                                           : &cgf.curFn->getRegion(0);
      cleanupBlock = builder.createBlock(r);
      return cleanupBlock;
    }

    // ---
    // Return handling.
    // ---

  private:
    // On switches we need one return block per region, since cases don't
    // have their own scopes but are distinct regions nonetheless.

    // TODO: This implementation should change once we have support for early
    //       exits in MLIR structured control flow (llvm-project#161575)
    llvm::SmallVector<mlir::Block *> retBlocks;
    llvm::DenseMap<mlir::Block *, mlir::Location> retLocs;
    llvm::DenseMap<cir::CaseOp, unsigned> retBlockInCaseIndex;
    std::optional<unsigned> normalRetBlockIndex;

    // There's usually only one ret block per scope, but this needs to be
    // get or create because of potential unreachable return statements, note
    // that for those, all source location maps to the first one found.
    mlir::Block *createRetBlock(CIRGenFunction &cgf, mlir::Location loc) {
      assert((isa_and_nonnull<cir::CaseOp>(
                  cgf.builder.getBlock()->getParentOp()) ||
              retBlocks.size() == 0) &&
             "only switches can hold more than one ret block");

      // Create the return block but don't hook it up just yet.
      mlir::OpBuilder::InsertionGuard guard(cgf.builder);
      auto *b = cgf.builder.createBlock(cgf.builder.getBlock()->getParent());
      retBlocks.push_back(b);
      updateRetLoc(b, loc);
      return b;
    }

    cir::ReturnOp emitReturn(mlir::Location loc);
    void emitImplicitReturn();

  public:
    llvm::ArrayRef<mlir::Block *> getRetBlocks() { return retBlocks; }
    mlir::Location getRetLoc(mlir::Block *b) { return retLocs.at(b); }
    void updateRetLoc(mlir::Block *b, mlir::Location loc) {
      retLocs.insert_or_assign(b, loc);
    }

    mlir::Block *getOrCreateRetBlock(CIRGenFunction &cgf, mlir::Location loc) {
      // Check if we're inside a case region
      if (auto caseOp = mlir::dyn_cast_if_present<cir::CaseOp>(
              cgf.builder.getBlock()->getParentOp())) {
        auto iter = retBlockInCaseIndex.find(caseOp);
        if (iter != retBlockInCaseIndex.end()) {
          // Reuse existing return block
          mlir::Block *ret = retBlocks[iter->second];
          updateRetLoc(ret, loc);
          return ret;
        }
        // Create new return block
        mlir::Block *ret = createRetBlock(cgf, loc);
        retBlockInCaseIndex[caseOp] = retBlocks.size() - 1;
        return ret;
      }

      if (normalRetBlockIndex) {
        mlir::Block *ret = retBlocks[*normalRetBlockIndex];
        updateRetLoc(ret, loc);
        return ret;
      }

      mlir::Block *ret = createRetBlock(cgf, loc);
      normalRetBlockIndex = retBlocks.size() - 1;
      return ret;
    }

    mlir::Block *getEntryBlock() { return entryBlock; }
  };

  LexicalScope *curLexScope = nullptr;

  struct ActiveObjCFinallyScope {
    const clang::Stmt *body = nullptr;
    clang::SourceRange tryBodyRange;
    LexicalScope *tryBodyScope = nullptr;
  };

  llvm::SmallVector<ActiveObjCFinallyScope, 2> activeObjCFinallyScopes;
  llvm::SmallVector<mlir::Value, 2> objCEHValueStack;

  typedef void Destroyer(CIRGenFunction &cgf, Address addr, QualType ty);

  static Destroyer destroyCXXObject;
  static Destroyer destroyARCStrongImprecise;
  static Destroyer destroyARCStrongPrecise;
  static Destroyer destroyARCWeak;

  void pushDestroy(QualType::DestructionKind dtorKind, Address addr,
                   QualType type);

  void pushDestroy(CleanupKind kind, Address addr, QualType type,
                   Destroyer *destroyer, bool useEHCleanupForArray);

  /// Push a destroy cleanup and defer its deactivation until the enclosing
  /// RunCleanupsScope exits. Used for exception-safe aggregate initialization
  /// where each member needs a destroy cleanup that is deactivated once the
  /// full aggregate is constructed.
  void pushDestroyAndDeferDeactivation(QualType::DestructionKind dtorKind,
                                       Address addr, QualType type);
  void pushDestroyAndDeferDeactivation(CleanupKind cleanupKind, Address addr,
                                       QualType type, Destroyer *destroyer,
                                       bool useEHCleanupForArray);

  void pushEHDestroy(QualType::DestructionKind dtorKind, Address addr,
                     QualType type);

  void pushLifetimeExtendedDestroy(QualType::DestructionKind dtorKind,
                                   Address addr, QualType type);
  void pushLifetimeExtendedDestroy(CleanupKind cleanupKind, Address addr,
                                   QualType type, Destroyer *destroyer,
                                   bool useEHCleanupForArray);

  void pushIrregularPartialArrayCleanup(mlir::Value arrayBegin,
                                        Address arrayEndPointer,
                                        QualType elementType,
                                        CharUnits elementAlign,
                                        Destroyer *destroyer,
                                        CleanupKind cleanupKind = NormalAndEHCleanup);

  Destroyer *getDestroyer(clang::QualType::DestructionKind kind);

  /// ----------------------
  /// CIR emit functions
  /// ----------------------
public:
  bool getAArch64SVEProcessedOperands(unsigned builtinID, const CallExpr *expr,
                                      SmallVectorImpl<mlir::Value> &ops,
                                      clang::SVETypeFlags typeFlags);
  std::optional<mlir::Value>
  emitAArch64BuiltinExpr(unsigned builtinID, const CallExpr *expr,
                         ReturnValueSlot returnValue,
                         llvm::Triple::ArchType arch);
  std::optional<mlir::Value>
  emitARMBuiltinExpr(unsigned builtinID, const CallExpr *expr,
                     ReturnValueSlot returnValue, llvm::Triple::ArchType arch);
  std::optional<mlir::Value> emitAArch64SMEBuiltinExpr(unsigned builtinID,
                                                       const CallExpr *expr);
  std::optional<mlir::Value> emitAArch64SVEBuiltinExpr(unsigned builtinID,
                                                       const CallExpr *expr);
  mlir::Value emitCommonNeonBuiltinExpr(
      unsigned builtinID, unsigned llvmIntrinsic, unsigned altLLVMIntrinsic,
      const char *nameHint, unsigned modifier, const CallExpr *e,
      llvm::SmallVectorImpl<mlir::Value> &ops, Address ptrOp0, Address ptrOp1,
      llvm::Triple::ArchType arch);

  mlir::Value emitAlignmentAssumption(mlir::Value ptrValue, QualType ty,
                                      SourceLocation loc,
                                      SourceLocation assumptionLoc,
                                      int64_t alignment,
                                      mlir::Value offsetValue = nullptr);

  mlir::Value emitAlignmentAssumption(mlir::Value ptrValue, const Expr *expr,
                                      SourceLocation assumptionLoc,
                                      int64_t alignment,
                                      mlir::Value offsetValue = nullptr);

private:
  void emitAndUpdateRetAlloca(clang::QualType type, mlir::Location loc,
                              clang::CharUnits alignment);

  CIRGenCallee emitDirectCallee(const GlobalDecl &gd);

public:
  Address emitAddrOfFieldStorage(Address base, const FieldDecl *field,
                                 llvm::StringRef fieldName,
                                 unsigned fieldIndex);

  mlir::Value emitAlloca(llvm::StringRef name, mlir::Type ty,
                         mlir::Location loc, clang::CharUnits alignment,
                         bool insertIntoFnEntryBlock,
                         mlir::Value arraySize = nullptr);
  mlir::Value emitAlloca(llvm::StringRef name, mlir::Type ty,
                         mlir::Location loc, clang::CharUnits alignment,
                         mlir::OpBuilder::InsertPoint ip,
                         mlir::Value arraySize = nullptr);

  void emitAggregateStore(mlir::Value value, Address dest,
                          bool destIsVolatile = false);

  void emitAggExpr(const clang::Expr *e, AggValueSlot slot);

  enum ExprValueKind { EVK_RValue, EVK_NonRValue };

  LValue emitAggExprToLValue(const Expr *e);
  LValue emitInitListLValue(const InitListExpr *e);
  LValue emitCXXConstructLValue(const CXXConstructExpr *e);
  LValue emitCXXBindTemporaryLValue(const CXXBindTemporaryExpr *e);

  /// Emit an aggregate copy.
  ///
  /// \param isVolatile \c true iff either the source or the destination is
  ///        volatile.
  /// \param MayOverlap Whether the tail padding of the destination might be
  ///        occupied by some other object. More efficient code can often be
  ///        generated if not.
  void emitAggregateCopy(LValue dest, LValue src, QualType eltTy,
                         AggValueSlot::Overlap_t mayOverlap,
                         bool isVolatile = false);

  /// Emit code to compute the specified expression which can have any type. The
  /// result is returned as an RValue struct. If this is an aggregate
  /// expression, the aggloc/agglocvolatile arguments indicate where the result
  /// should be returned.
  RValue emitAnyExpr(const clang::Expr *e,
                     AggValueSlot aggSlot = AggValueSlot::ignored(),
                     bool ignoreResult = false);

  /// Emits the code necessary to evaluate an arbitrary expression into the
  /// given memory location.
  void emitAnyExprToMem(const Expr *e, Address location, Qualifiers quals,
                        bool isInitializer);

  /// Similarly to emitAnyExpr(), however, the result will always be accessible
  /// even if no aggregate location is provided.
  RValue emitAnyExprToTemp(const clang::Expr *e);

  void emitAnyExprToExn(const Expr *e, Address addr);

  void emitArrayDestroy(mlir::Value begin, mlir::Value numElements,
                        QualType elementType, CharUnits elementAlign,
                        Destroyer *destroyer, bool useEHCleanup = false);

  void pushRegularPartialArrayCleanup(mlir::Value begin, mlir::Value cur,
                                      QualType elementType,
                                      CharUnits elementAlign,
                                      Destroyer *destroyer);

  mlir::Value emitArrayLength(const clang::ArrayType *arrayType,
                              QualType &baseType, Address &addr);
  LValue emitArraySubscriptExpr(const clang::ArraySubscriptExpr *e,
                                bool accessed = false);
  mlir::Value emitMatrixIndexExpr(const clang::Expr *e);
  LValue emitMatrixSubscriptExpr(const clang::MatrixSubscriptExpr *e);

  LValue emitExtVectorElementExpr(const ExtVectorElementExpr *e);

  Address emitArrayToPointerDecay(const Expr *e,
                                  LValueBaseInfo *baseInfo = nullptr,
                                  TBAAAccessInfo *tbaaInfo = nullptr);

  std::pair<mlir::Value, mlir::Type>
  emitAsmInputLValue(const TargetInfo::ConstraintInfo &info, LValue inputValue,
                     QualType inputType, std::string &constraintString,
                     SourceLocation loc);
  std::pair<mlir::Value, mlir::Type>
  emitAsmInput(const TargetInfo::ConstraintInfo &info, const Expr *inputExpr,
               std::string &constraintString);
  mlir::LogicalResult emitAsmStmt(const clang::AsmStmt &s);

  RValue emitAtomicExpr(AtomicExpr *e);
  void emitAtomicInit(Expr *init, LValue dest);
  RValue emitAtomicLoad(LValue src, clang::SourceLocation loc);
  RValue emitAtomicLoad(LValue src, clang::SourceLocation loc,
                        cir::MemOrder order, bool isVolatile);
  void emitAtomicLoad(LValue src, clang::SourceLocation loc,
                      AggValueSlot dest);
  void emitAtomicStore(RValue rvalue, LValue dest, bool isInit);
  void emitAtomicStore(RValue rvalue, LValue dest, cir::MemOrder order,
                       bool isVolatile, bool isInit);
  void emitAtomicExprWithMemOrder(
      const Expr *memOrder, bool isStore, bool isLoad, bool isFence,
      llvm::function_ref<void(cir::MemOrder)> emitAtomicOp);

  AutoVarEmission emitAutoVarAlloca(const clang::VarDecl &d,
                                    mlir::OpBuilder::InsertPoint ip = {});
  Address emitBlockByrefAddress(Address baseAddr, const VarDecl *var,
                                bool followForward);
  const BlockByrefInfo &getBlockByrefInfo(const VarDecl *d);
  CharUnits
  getDirectBlockLiteralAlignment(llvm::ArrayRef<mlir::Type> literalMembers) const;
  CharUnits getDirectBlockLiteralFieldOffset(
      llvm::ArrayRef<mlir::Type> literalMembers, bool packed,
      unsigned fieldIndex) const;
  CharUnits getDirectBlockLiteralSize(
      llvm::ArrayRef<mlir::Type> literalMembers, bool packed) const;
  bool shouldPackDirectBlockLiteral(
      llvm::ArrayRef<mlir::Type> literalMembers) const;
  void emitByrefStructureInit(const AutoVarEmission &emission);

  /// Emit local annotations for the local variable, declared by D.
  void emitVarAnnotations(const clang::VarDecl *d, mlir::Value val);
  Address emitFieldAnnotations(const clang::FieldDecl *d, Address addr);

  /// Emit code and set up symbol table for a variable declaration with auto,
  /// register, or no storage class specifier. These turn into simple stack
  /// objects, globals depending on target.
  void emitAutoVarDecl(const clang::VarDecl &d);
  mlir::Value emitObjCClassRealization(mlir::Location loc,
                                       mlir::Value classObject);
  void emitObjCDirectMethodPrologue(const clang::ObjCMethodDecl *omd);

  void emitAutoVarCleanups(const AutoVarEmission &emission);
  /// Emit the initializer for an allocated variable.  If this call is not
  /// associated with the call to emitAutoVarAlloca (as the address of the
  /// emission is not directly an alloca), the allocatedSeparately parameter can
  /// be used to suppress the assertions.  However, this should only be used in
  /// extreme cases, as it doesn't properly reflect the language/AST.
  void emitAutoVarInit(const AutoVarEmission &emission);
  void emitAutoVarTypeCleanup(const AutoVarEmission &emission,
                              clang::QualType::DestructionKind dtorKind);

  void maybeEmitDeferredVarDeclInit(const VarDecl *vd);

  void emitBaseInitializer(mlir::Location loc, const CXXRecordDecl *classDecl,
                           CXXCtorInitializer *baseInit);

  LValue emitBinaryOperatorLValue(const BinaryOperator *e);

  cir::BrOp emitBranchThroughCleanup(mlir::Location loc, JumpDest dest);

  mlir::LogicalResult emitBreakStmt(const clang::BreakStmt &s);

  RValue emitBuiltinExpr(const clang::GlobalDecl &gd, unsigned builtinID,
                         const clang::CallExpr *e, ReturnValueSlot returnValue,
                         bool ignoreResult = false);

  /// Emit the `__builtin_os_log_format` buffer fill: build the layout via
  /// `analyze_os_log::computeOSLogBufferLayout`, marshal the arguments and
  /// call the shared `__os_log_helper_*` function that performs the stores.
  RValue emitBuiltinOSLogFormat(const clang::CallExpr &e);

  /// Materialize (or reuse) the linkonce_odr `__os_log_helper_*` function that
  /// writes the os_log buffer encoding described by \p layout.
  cir::FuncOp generateBuiltinOSLogHelperFunction(
      const clang::analyze_os_log::OSLogBufferLayout &layout,
      clang::CharUnits bufferAlignment);

  /// Returns a Value corresponding to the size of the given expression by
  /// emitting a `cir.objsize` operation.
  ///
  /// \param e The expression whose object size to compute
  /// \param type Determines the semantics of the object size computation.
  ///   The type parameter is a 2-bit value where:
  ///     bit 0 (type & 1): 0 = whole object, 1 = closest subobject
  ///     bit 1 (type & 2): 0 = maximum size, 2 = minimum size
  /// \param resType The result type for the size value
  /// \param emittedE Optional pre-emitted pointer value. If non-null, we'll
  ///   call `cir.objsize` on this value rather than emitting e.
  /// \param isDynamic If true, allows runtime evaluation via dynamic mode
  mlir::Value emitBuiltinObjectSize(const clang::Expr *e, unsigned type,
                                    cir::IntType resType, mlir::Value emittedE,
                                    bool isDynamic);

  mlir::Value evaluateOrEmitBuiltinObjectSize(const clang::Expr *e,
                                              unsigned type,
                                              cir::IntType resType,
                                              mlir::Value emittedE,
                                              bool isDynamic);

  /// Emit the size of a flexible array member or counted_by pointer field.
  /// Returns {} if the expression doesn't have a counted_by attribute.
  mlir::Value emitCountedBySize(const clang::Expr *e, mlir::Value emittedE,
                                unsigned type, cir::IntType resType);

  /// Emit the size for a counted_by flexible array member access.
  mlir::Value emitCountedByMemberSize(const clang::MemberExpr *me,
                                      const clang::Expr *idx,
                                      mlir::Value emittedE,
                                      clang::QualType castedArrayElementTy,
                                      unsigned type, cir::IntType resType);

  /// Emit the size for a counted_by pointer field access.
  mlir::Value emitCountedByPointerSize(const clang::ImplicitCastExpr *e,
                                       const clang::Expr *idx,
                                       mlir::Value emittedE,
                                       clang::QualType castedArrayElementTy,
                                       unsigned type, cir::IntType resType);

  /// Load the value of a counted_by field for a struct that contains a
  /// flexible array member or a counted_by pointer.
  mlir::Value emitLoadOfCountedByField(const clang::Expr *base,
                                       const clang::FieldDecl *famDecl,
                                       const clang::FieldDecl *countDecl);

  /// Get the address of a counted_by field for a struct that contains a
  /// flexible array member or a counted_by pointer. Returns a null mlir::Value
  /// on failure.
  mlir::Value getAddrOfCountedByField(const clang::Expr *base,
                                      const clang::FieldDecl *countDecl);

  int64_t getAccessedFieldNo(unsigned idx, mlir::ArrayAttr elts);

  void instantiateIndirectGotoBlock();

  RValue emitCall(const CIRGenFunctionInfo &funcInfo,
                  const CIRGenCallee &callee, ReturnValueSlot returnValue,
                  const CallArgList &args, cir::CIRCallOpInterface *callOp,
                  mlir::Location loc, bool isMustTail = false,
                  mlir::Value staticChain = {},
                  SourceLocation callSiteLoc = SourceLocation());
  RValue emitCall(const CIRGenFunctionInfo &funcInfo,
                  const CIRGenCallee &callee, ReturnValueSlot returnValue,
                  const CallArgList &args,
                  cir::CIRCallOpInterface *callOrTryCall = nullptr) {
    assert(currSrcLoc && "source location must have been set");
    return emitCall(funcInfo, callee, returnValue, args, callOrTryCall,
                    *currSrcLoc);
  }

  RValue emitCall(clang::QualType calleeTy, const CIRGenCallee &callee,
                  const clang::CallExpr *e, ReturnValueSlot returnValue,
                  cir::CIRCallOpInterface *callOrInvoke = nullptr,
                  mlir::Value staticChain = {});
  mlir::ArrayAttr buildAllocToken(const clang::CallExpr *e,
                                  bool allowUnknown = false) const;
  mlir::ArrayAttr buildAllocToken(clang::QualType allocType) const;
  void maybeAttachAllocToken(cir::CIRCallOpInterface callOp,
                             const clang::CallExpr *e);
  void maybeAttachAllocToken(cir::CIRCallOpInterface callOp,
                             clang::QualType allocType);
  void emitCallArg(CallArgList &args, const clang::Expr *e,
                   clang::QualType argType,
                   bool *sawDefaultArg = nullptr);
  /// Evaluation order for arguments in a call.
  enum class EvaluationOrder {
    /// No language constraints on evaluation order.
    Default,
    /// Language semantics require left-to-right evaluation.
    ForceLeftToRight,
    /// Language semantics require right-to-left evaluation.
    ForceRightToLeft
  };

  void emitCallArgs(
      CallArgList &args, PrototypeWrapper prototype,
      llvm::iterator_range<clang::CallExpr::const_arg_iterator> argRange,
      AbstractCallee callee = AbstractCallee(), unsigned paramsToSkip = 0,
      EvaluationOrder order = EvaluationOrder::Default,
      bool *sawDefaultArg = nullptr);
  RValue emitCallExpr(const clang::CallExpr *e,
                      ReturnValueSlot returnValue = ReturnValueSlot());
  RValue emitCallExpr(const clang::CallExpr *e, ReturnValueSlot returnValue,
                      bool ignoreResult);
  LValue emitCallExprLValue(const clang::CallExpr *e);
  LValue emitObjCMessageExprLValue(const clang::ObjCMessageExpr *e);
  CIRGenCallee emitCallee(const clang::Expr *e);

  template <typename T>
  mlir::LogicalResult emitCaseDefaultCascade(const T *stmt, mlir::Type condType,
                                             mlir::ArrayAttr value,
                                             cir::CaseOpKind kind,
                                             mlir::ArrayAttr likelihoods,
                                             bool buildingTopLevelCase);

  mlir::LogicalResult emitCaseStmt(const clang::CaseStmt &s,
                                   mlir::Type condType,
                                   bool buildingTopLevelCase,
                                   llvm::ArrayRef<const Attr *> attrs = {});

  const clang::CaseStmt *foldCaseStmt(const clang::CaseStmt &s,
                                      mlir::Type condType,
                                      mlir::ArrayAttr &value,
                                      mlir::ArrayAttr &likelihoods,
                                      clang::Stmt::Likelihood topLikelihood,
                                      cir::CaseOpKind &kind);

  LValue emitCastLValue(const CastExpr *e);

  /// Emits an argument for a call to a `__builtin_assume`. If the builtin
  /// sanitizer is enabled, a runtime check is also emitted.
  mlir::Value emitCheckedArgForAssume(const Expr *e);

  /// Emit a conversion from the specified complex type to the specified
  /// destination type, where the destination type is an LLVM scalar type.
  mlir::Value emitComplexToScalarConversion(mlir::Value src, QualType srcTy,
                                            QualType dstTy, SourceLocation loc);

  LValue emitCompoundAssignmentLValue(const clang::CompoundAssignOperator *e);
  LValue emitCompoundLiteralLValue(const CompoundLiteralExpr *e);

  void emitConstructorBody(FunctionArgList &args);

  mlir::LogicalResult emitCoroutineBody(const CoroutineBodyStmt &s);
  cir::CallOp emitCoroEndBuiltinCall(mlir::Location loc, mlir::Value nullPtr,
                                     bool isUnwind);
  cir::CallOp emitCoroIsInRampBuiltinCall(mlir::Location loc);
  mlir::Block *getOrCreateCoroutineRetBlock(mlir::Location loc);
  mlir::Block *getOrCreateCoroutineCleanupRetBlock(mlir::Location loc);
  cir::CallOp emitCoroIDBuiltinCall(mlir::Location loc, mlir::Value nullPtr);
  cir::CallOp emitCoroAllocBuiltinCall(mlir::Location loc);
  cir::CallOp emitCoroBeginBuiltinCall(mlir::Location loc,
                                       mlir::Value coroframeAddr);
  void emitCoroutineFrameDealloc(mlir::Location loc);
  RValue emitCoroutineFrame();

  /// Generates wrapper function for llvm.coro.await.suspend.* intrinsics.
  /// It encapsulates SuspendExpr in a function, to separate its body
  /// from the main coroutine to avoid miscompilations. The intrinsic
  /// is lowered to this function call in CoroSplit pass.
  /// Function signature is:
  ///   <type> __await_suspend_wrapper__<name>(ptr %awaiter, ptr %hdl)
  /// where type is one of (void, i1, ptr)
  cir::FuncOp
  generateAwaitSuspendWrapper(mlir::Location loc, llvm::Twine const &coroName,
                              llvm::Twine const &suspendPointName,
                              const CoroutineSuspendExpr &s);

  void emitDestroy(Address addr, QualType type, Destroyer *destroyer,
                   bool useEHCleanupForArray);

  void emitDestructorBody(FunctionArgList &args);

  mlir::LogicalResult emitContinueStmt(const clang::ContinueStmt &s);

  mlir::LogicalResult emitCoreturnStmt(const CoreturnStmt &s,
                                       bool isFallthrough = false);

  void emitCXXConstructExpr(const clang::CXXConstructExpr *e,
                            AggValueSlot dest);

  void emitCXXAggrConstructorCall(const CXXConstructorDecl *ctor,
                                  const clang::ArrayType *arrayType,
                                  Address arrayBegin, const CXXConstructExpr *e,
                                  bool newPointerIsChecked,
                                  bool zeroInitialize = false);
  void emitCXXAggrConstructorCall(const CXXConstructorDecl *ctor,
                                  mlir::Value numElements, Address arrayBase,
                                  const CXXConstructExpr *e,
                                  bool newPointerIsChecked,
                                  bool zeroInitialize);
  void emitCXXConstructorCall(const clang::CXXConstructorDecl *d,
                              clang::CXXCtorType type, bool forVirtualBase,
                              bool delegating, AggValueSlot thisAVS,
                              const clang::CXXConstructExpr *e);

  void emitCXXConstructorCall(const clang::CXXConstructorDecl *d,
                              clang::CXXCtorType type, bool forVirtualBase,
                              bool delegating, Address thisAddr,
                              CallArgList &args,
                              AggValueSlot::Overlap_t overlap,
                              clang::SourceLocation loc,
                              bool newPointerIsChecked = false);

  void emitInheritedCXXConstructorCall(
      const clang::CXXConstructorDecl *d, bool forVirtualBase, Address thisAddr,
      bool inheritedFromVBase, const clang::CXXInheritedCtorInitExpr *e);

  void emitInlinedInheritingCXXConstructorCall(
      const clang::CXXConstructorDecl *ctor, clang::CXXCtorType ctorType,
      bool forVirtualBase, bool delegating, CallArgList &args);

  void emitCXXDeleteExpr(const CXXDeleteExpr *e);

  void emitCXXDestructorCall(const CXXDestructorDecl *dd, CXXDtorType type,
                             bool forVirtualBase, bool delegating,
                             Address thisAddr, QualType thisTy);

  RValue emitCXXDestructorCall(GlobalDecl dtor, const CIRGenCallee &callee,
                               mlir::Value thisVal, QualType thisTy,
                               mlir::Value implicitParam,
                               QualType implicitParamTy, const CallExpr *e);

  mlir::LogicalResult emitCXXForRangeStmt(const CXXForRangeStmt &s,
                                          llvm::ArrayRef<const Attr *> attrs);

  RValue emitCXXMemberCallExpr(const clang::CXXMemberCallExpr *e,
                               ReturnValueSlot returnValue);

  Address emitCXXMemberDataPointerAddress(
      const Expr *e, Address base, mlir::Value memberPtr,
      const MemberPointerType *memberPtrType, LValueBaseInfo *baseInfo,
      TBAAAccessInfo *tbaaInfo = nullptr, bool isInBounds = false);

  RValue emitCXXMemberOrOperatorCall(
      const clang::CXXMethodDecl *md, const CIRGenCallee &callee,
      ReturnValueSlot returnValue, mlir::Value thisPtr,
      mlir::Value implicitParam, clang::QualType implicitParamTy,
      const clang::CallExpr *ce, CallArgList *rtlArgs,
      cir::CIRCallOpInterface *callOp = nullptr);

  RValue emitCXXMemberOrOperatorMemberCallExpr(
      const clang::CallExpr *ce, const clang::CXXMethodDecl *md,
      ReturnValueSlot returnValue, bool hasQualifier,
      clang::NestedNameSpecifier qualifier, bool isArrow,
      const clang::Expr *base);

  RValue emitCXXMemberPointerCallExpr(const CXXMemberCallExpr *ce,
                                      ReturnValueSlot returnValue);

  mlir::Value emitCXXNewExpr(const CXXNewExpr *e);

  void emitNewArrayInitializer(const CXXNewExpr *e, QualType elementType,
                               mlir::Type elementTy, Address beginPtr,
                               mlir::Value numElements,
                               mlir::Value allocSizeWithoutCookie);

  RValue emitCXXOperatorMemberCallExpr(const CXXOperatorCallExpr *e,
                                       const CXXMethodDecl *md,
                                       ReturnValueSlot returnValue);

  RValue emitCXXPseudoDestructorExpr(const CXXPseudoDestructorExpr *expr);

  RValue emitNewOrDeleteBuiltinCall(const FunctionProtoType *type,
                                    const CallExpr *callExpr,
                                    OverloadedOperatorKind op);

  void emitCXXTemporary(const CXXTemporary *temporary, QualType tempType,
                        Address ptr);

  void emitCXXThrowExpr(const CXXThrowExpr *e);

  mlir::Value emitCXXTypeidExpr(const CXXTypeidExpr *e);
  LValue emitCXXTypeidLValue(const CXXTypeidExpr *e);

  /// Emit the start of an exception spec - push a terminate scope for
  /// noexcept functions, or a filter scope for throw() specifications.
  void emitStartEHSpec(const clang::Decl *d);

  /// Emit the end of an exception spec - pop the terminate/filter scope.
  void emitEndEHSpec(const clang::Decl *d);

  mlir::LogicalResult emitCXXTryStmt(const clang::CXXTryStmt &s);
  mlir::LogicalResult
  emitCXXTryStmt(const clang::CXXTryStmt &s,
                 llvm::function_ref<mlir::LogicalResult()> emitTryBody);

  mlir::LogicalResult emitCXXTryStmtUnderScope(const clang::CXXTryStmt &s);
  mlir::LogicalResult
  emitCXXTryStmtUnderScope(
      const clang::CXXTryStmt &s,
      llvm::function_ref<mlir::LogicalResult()> emitTryBody);

  void enterCXXTryStmt(const CXXTryStmt &s, cir::TryOp tryOp,
                       bool isFnTryBlock = false);

  void exitCXXTryStmt(const CXXTryStmt &s, bool isFnTryBlock = false);

  void emitCtorPrologue(const clang::CXXConstructorDecl *ctor,
                        clang::CXXCtorType ctorType, FunctionArgList &args);

  // It's important not to confuse this and emitDelegateCXXConstructorCall.
  // Delegating constructors are the C++11 feature. The constructor delegate
  // optimization is used to reduce duplication in the base and complete
  // constructors where they are substantially the same.
  void emitDelegatingCXXConstructorCall(const CXXConstructorDecl *ctor,
                                        const FunctionArgList &args);

  void emitDeleteCall(const FunctionDecl *deleteFD, mlir::Value ptr,
                      QualType deleteTy, mlir::Value numElements = nullptr,
                      CharUnits cookieSize = CharUnits());
  void pushCallObjectDeleteCleanup(const FunctionDecl *operatorDelete,
                                   mlir::Value completePtr,
                                   QualType elementType);

  void setPendingLoopHintAttr(mlir::DictionaryAttr attr) {
    pendingLoopHintAttr = attr;
  }

  mlir::DictionaryAttr consumePendingLoopHintAttr() {
    mlir::DictionaryAttr attr =
        pendingLoopHintAttr.value_or(mlir::DictionaryAttr{});
    pendingLoopHintAttr.reset();
    return attr;
  }

  mlir::LogicalResult emitDoStmt(const clang::DoStmt &s,
                                 llvm::ArrayRef<const clang::Attr *> attrs = {});

  mlir::Value emitDynamicCast(Address thisAddr, const CXXDynamicCastExpr *dce);

  /// Emit an expression as an initializer for an object (variable, field, etc.)
  /// at the given location.  The expression is not necessarily the normal
  /// initializer for the object, and the address is not necessarily
  /// its normal location.
  ///
  /// \param init the initializing expression
  /// \param d the object to act as if we're initializing
  /// \param lvalue the lvalue to initialize
  /// \param capturedByInit true if \p d is a __block variable whose address is
  /// potentially changed by the initializer
  void emitExprAsInit(const clang::Expr *init, const clang::ValueDecl *d,
                      LValue lvalue, bool capturedByInit = false);

  mlir::LogicalResult emitFunctionBody(const clang::Stmt *body);

  void emitKernelMetadata(const clang::FunctionDecl *fd, cir::FuncOp fn);

  mlir::LogicalResult emitGotoStmt(const clang::GotoStmt &s);

  mlir::LogicalResult emitIndirectGotoStmt(const IndirectGotoStmt &s);

  void emitImplicitAssignmentOperatorBody(FunctionArgList &args);

  void emitInitializerForField(clang::FieldDecl *field, LValue lhs,
                               clang::Expr *init);

  LValue emitPredefinedLValue(const PredefinedExpr *e);

  mlir::Value emitPromotedComplexExpr(const Expr *e, QualType promotionType);

  mlir::Value emitPromotedScalarExpr(const Expr *e, QualType promotionType);

  mlir::Value emitPromotedValue(mlir::Value result, QualType promotionType);

  void emitReturnOfRValue(mlir::Location loc, RValue rv, QualType ty);

  bool requiresReturnValueCheck() const;
  bool requiresReturnValueNullabilityCheck() const {
    return static_cast<bool>(retValNullabilityPrecondition);
  }
  void setReturnValueCheckSourceLocation(clang::SourceLocation loc);
  void emitReturnValueCheck(mlir::Location loc, mlir::Value rv);

  cir::CallOp emitRuntimeCallOp(mlir::Location loc, cir::FuncOp callee,
                                llvm::ArrayRef<mlir::Value> args = {});
  mlir::Value emitRuntimeCall(mlir::Location loc, cir::FuncOp callee,
                              llvm::ArrayRef<mlir::Value> args = {});
  cir::CIRCallOpInterface emitRuntimeCallOrInvoke(
      mlir::Location loc, cir::FuncOp callee,
      llvm::ArrayRef<mlir::Value> args = {});

  /// Like emitRuntimeCall, but sets the nothrow attribute on the call.
  /// Matches OGCG's EmitNounwindRuntimeCall.
  mlir::Value emitNounwindRuntimeCall(mlir::Location loc, cir::FuncOp callee,
                                      llvm::ArrayRef<mlir::Value> args = {});

  /// Emit a runtime version check for __builtin_available.
  /// On Darwin, calls __isPlatformVersionAtLeast; otherwise __isOSVersionAtLeast.
  mlir::Value emitBuiltinAvailable(mlir::Location loc,
                                   const VersionTuple &version);

  void pushDeferredCleanupDispatchCandidate(const CallExpr *call) {
    assert(call && "missing deferred cleanup dispatch candidate call");
    assert(!deferredCleanupDispatchCandidateCall &&
           "nested deferred cleanup dispatch candidates are unsupported");
    deferredCleanupDispatchCandidateCall = call;
  }
  void popDeferredCleanupDispatchCandidate(const CallExpr *call) {
    assert(deferredCleanupDispatchCandidateCall == call &&
           "unbalanced deferred cleanup dispatch candidate pop");
    deferredCleanupDispatchCandidateCall = nullptr;
  }
  bool isDeferredCleanupDispatchCandidate(const CallExpr *call) const {
    return deferredCleanupDispatchCandidateCall == call;
  }

  /// Check that builtin calls have their required target features available in
  /// the caller. Emits err_builtin_needs_feature diagnostics as needed.
  void checkTargetFeatures(const CallExpr *e, const FunctionDecl *targetDecl);

  /// Check that always_inline / flatten callees have their required target
  /// features available in the caller. Emits err_function_needs_feature or
  /// err_flatten_function_needs_feature diagnostics as needed.
  void checkTargetFeatures(SourceLocation loc, const FunctionDecl *targetDecl);

  /// Emit a call to __cxa_atexit or __cxa_thread_atexit to register a
  /// destructor for a variable. Uses __cxa_thread_atexit for thread-local
  /// variables, __cxa_atexit otherwise.
  void emitCXXAtExit(mlir::Location loc, cir::FuncOp dtorFn,
                     mlir::Value varAddr, bool isTLS = false);
  cir::FuncOp generateDestroyHelper(Address addr, QualType type,
                                    Destroyer *destroyer,
                                    bool useEHCleanupForArray,
                                    const VarDecl *vd);

  void emitInvariantStart(CharUnits size, mlir::Value addr, mlir::Location loc);

  /// Emit the computation of the specified expression of scalar type.
  mlir::Value emitScalarExpr(const clang::Expr *e,
                             bool ignoreResultAssign = false);

  mlir::Value emitScalarPrePostIncDec(const UnaryOperator *e, LValue lv,
                                      cir::UnaryOpKind kind, bool isPre);

  /// Build a debug stoppoint if we are emitting debug info.
  void emitStopPoint(const Stmt *s);

  // Build CIR for a statement. useCurrentScope should be true if no
  // new scopes need be created when finding a compound statement.
  mlir::LogicalResult emitStmt(const clang::Stmt *s, bool useCurrentScope,
                               llvm::ArrayRef<const Attr *> attrs = {});

  // Emit an Objective-C @finally body. Unlike classic CodeGen, which outlines
  // the @finally into a single shared cleanup, CIRGen re-emits the body on
  // every edge leaving the @try (normal fallthrough, the exceptional/rethrow
  // handler, and each early-exit edge via emitActiveObjCFinallyBodyForNormalExit).
  // Each textual copy is a distinct dynamic instance and must get its own
  // storage, so the @finally body's local VarDecls are removed from the
  // function-wide localDeclMap around each re-emission (and any prior entry is
  // restored afterward). Without this, the second copy's emitAutoVarAlloca would
  // trip the "Decl already exists in LocalDeclMap!" assert in setAddrOfLocalVar.
  mlir::LogicalResult emitObjCFinallyBody(const clang::Stmt *finallyBody,
                                          bool useCurrentScope);

  void pushActiveObjCFinallyScope(const clang::Stmt *body,
                                  clang::SourceRange tryBodyRange,
                                  LexicalScope *tryBodyScope);
  void popActiveObjCFinallyScope();
  void popActiveObjCFinallyScopeIfPresent(const clang::Stmt *body,
                                          const LexicalScope *tryBodyScope);
  mlir::LogicalResult emitActiveObjCFinallyBodyForNormalExit(unsigned index,
                                                             bool &overrodeExit);
  mlir::LogicalResult emitActiveObjCFinallyBodiesForReturn(bool &overrodeExit);
  mlir::LogicalResult
  emitActiveObjCFinallyBodiesForGoto(const clang::LabelDecl *label,
                                     bool &overrodeExit);
  mlir::LogicalResult emitActiveObjCFinallyBodiesForBreakContinue(
      const LexicalScope *targetScope, bool &overrodeExit);
  mlir::LogicalResult emitGotoWithCleanup(
      mlir::Location loc, llvm::StringRef labelName,
      EHScopeStack::stable_iterator targetDepth,
      const LexicalScope *targetScope);
  void emitSyntheticLabel(mlir::Location loc, llvm::StringRef labelName);
  std::string createNamedLoopControlLabel(llvm::StringRef kind);
  NamedLoopControlTarget *
  getNamedLoopControlTarget(const clang::Stmt *loopOrSwitch);
  bool isActiveNamedLoopTarget(const clang::Stmt *stmt) const;

  mlir::LogicalResult emitSimpleStmt(const clang::Stmt *s,
                                     bool useCurrentScope,
                                     llvm::ArrayRef<const Attr *> attrs = {});

  mlir::LogicalResult emitForStmt(const clang::ForStmt &s,
                                  llvm::ArrayRef<const clang::Attr *> attrs = {});

  void emitForwardingCallToLambda(const CXXMethodDecl *lambdaCallOperator,
                                  CallArgList &callArgs);

  RValue emitCoawaitExpr(const CoawaitExpr &e,
                         AggValueSlot aggSlot = AggValueSlot::ignored(),
                         bool ignoreResult = false);
  LValue emitCoawaitLValue(const CoawaitExpr &e);

  RValue emitCoyieldExpr(const CoyieldExpr &e,
                         AggValueSlot aggSlot = AggValueSlot::ignored(),
                         bool ignoreResult = false);
  LValue emitCoyieldLValue(const CoyieldExpr &e);
  /// Emit the computation of the specified expression of complex type,
  /// returning the result.
  mlir::Value emitComplexExpr(const Expr *e);

  void emitComplexExprIntoLValue(const Expr *e, LValue dest, bool isInit);

  mlir::Value emitComplexPrePostIncDec(const UnaryOperator *e, LValue lv,
                                       cir::UnaryOpKind op, bool isPre);

  LValue emitComplexAssignmentLValue(const BinaryOperator *e);
  LValue emitComplexCompoundAssignmentLValue(const CompoundAssignOperator *e);
  LValue emitScalarCompoundAssignWithComplex(const CompoundAssignOperator *e,
                                             mlir::Value &result);

  mlir::LogicalResult
  emitCompoundStmt(const clang::CompoundStmt &s, Address *lastValue = nullptr,
                   AggValueSlot slot = AggValueSlot::ignored());

  mlir::LogicalResult
  emitCompoundStmtWithoutScope(const clang::CompoundStmt &s,
                               Address *lastValue = nullptr,
                               AggValueSlot slot = AggValueSlot::ignored());

  void emitDecl(const clang::Decl &d, bool evaluateConditionDecl = false);
  mlir::LogicalResult emitDeclStmt(const clang::DeclStmt &s);
  mlir::LogicalResult emitDeferStmt(const clang::DeferStmt &s);
  LValue emitDeclRefLValue(const clang::DeclRefExpr *e);
  LValue emitObjCIsaExpr(const clang::ObjCIsaExpr *e);
  LValue emitObjCIvarRefLValue(const clang::ObjCIvarRefExpr *e);
  RValue emitObjCMessageExpr(const clang::ObjCMessageExpr *e,
                             ReturnValueSlot returnValue = ReturnValueSlot());

  mlir::LogicalResult emitAttributedStmt(const clang::AttributedStmt &s);
  mlir::LogicalResult
  emitObjCAutoreleasePoolStmt(const clang::ObjCAutoreleasePoolStmt &s);
  mlir::LogicalResult emitObjCAtTryStmt(const clang::ObjCAtTryStmt &s);
  mlir::LogicalResult emitObjCRethrow(mlir::Location loc);
  mlir::LogicalResult emitObjCAtThrowStmt(const clang::ObjCAtThrowStmt &s);
  mlir::LogicalResult
  emitObjCThrowViaSyntheticTry(const clang::ObjCAtThrowStmt &s);

  /// Emit a value-producing operation that may throw (e.g. a throwing
  /// `cir.dyn_cast ref`, or a `typeid` null-check that reaches
  /// `__cxa_bad_typeid`) inside a synthetic cleanup-only TryOp when there are
  /// active EH cleanups (RAII/ARC destructors) but no enclosing user try. The
  /// synthetic try's handler eagerly emits the active EH cleanup bodies, so
  /// FlattenCFG routes the mandatory throw through a landing pad that runs those
  /// destructors before resuming — without it the local cleanups are dropped as
  /// the exception propagates out. \p emitOp emits the throwing op and returns
  /// its result (or a null Value for a void op). The result is forwarded out of
  /// the try region through a temporary alloca.
  mlir::Value emitMayThrowOpViaSyntheticCleanupTry(
      mlir::Location loc, mlir::Type resultTy,
      llvm::function_ref<mlir::Value()> emitOp);
  mlir::LogicalResult
  emitObjCAtSynchronizedStmt(const clang::ObjCAtSynchronizedStmt &s);
  mlir::LogicalResult
  emitObjCForCollectionStmt(const clang::ObjCForCollectionStmt &s);

  mlir::LogicalResult
  emitCapturedStmt(const clang::CapturedStmt &s,
                   clang::CapturedRegionKind k = clang::CR_Default);
  LValue initCapturedStruct(const clang::CapturedStmt &s);
  cir::FuncOp
  generateCapturedStmtFunction(const clang::CapturedStmt &s);
  cir::FuncOp generateCapturedStmtFunction(
      const clang::CapturedStmt &s,
      llvm::function_ref<void(CIRGenFunction &, const clang::CapturedDecl &)>
          bodyEmitter);

  mlir::LogicalResult emitDefaultStmt(const clang::DefaultStmt &s,
                                      mlir::Type condType,
                                      bool buildingTopLevelCase,
                                      llvm::ArrayRef<const Attr *> attrs = {});

  void emitDelegateCXXConstructorCall(const clang::CXXConstructorDecl *ctor,
                                      clang::CXXCtorType ctorType,
                                      const FunctionArgList &args,
                                      clang::SourceLocation loc);

  /// We are performing a delegate call; that is, the current function is
  /// delegating to another one. Produce a r-value suitable for passing the
  /// given parameter.
  void emitDelegateCallArg(CallArgList &args, const clang::VarDecl *param,
                           clang::SourceLocation loc);

  /// Emit an `if` on a boolean condition to the specified blocks.
  /// FIXME: Based on the condition, this might try to simplify the codegen of
  /// the conditional based on the branch.
  /// In the future, we may apply code generation simplifications here,
  /// similar to those used in classic LLVM  codegen
  /// See `EmitBranchOnBoolExpr` for inspiration.
  mlir::LogicalResult
  emitIfOnBoolExpr(const clang::Expr *cond, const clang::Stmt *thenS,
                   const clang::Stmt *elseS,
                   clang::Stmt::Likelihood LH = clang::Stmt::LH_None,
                   std::optional<int32_t> hlslControlFlowHint = std::nullopt,
                   const clang::Stmt *pgoCounterStmt = nullptr);
  cir::IfOp emitIfOnBoolExpr(
      const clang::Expr *cond, BuilderCallbackRef thenBuilder,
      mlir::Location thenLoc, BuilderCallbackRef elseBuilder,
      std::optional<mlir::Location> elseLoc = {},
      clang::Stmt::Likelihood LH = clang::Stmt::LH_None,
      std::optional<int32_t> hlslControlFlowHint = std::nullopt,
      std::optional<uint64_t> sourceAtomGroup = std::nullopt,
      std::optional<mlir::Location> ifLocOverride = std::nullopt);

  mlir::Value emitOpOnBoolExpr(mlir::Location loc, const clang::Expr *cond);

  /// If \p LH is \c Stmt::LH_Likely or \c Stmt::LH_Unlikely, wraps \p cond
  /// with a \c cir.expect intrinsic to hint the optimizer about the expected
  /// branch direction. Returns \p cond unchanged if \p LH is \c Stmt::LH_None
  /// or optimization is disabled.
  mlir::Value
  emitCondLikelihoodViaExpectIntrinsic(mlir::Value cond,
                                       clang::Stmt::Likelihood LH);

  LValue emitPointerToDataMemberBinaryExpr(const BinaryOperator *e);

  /// Check whether the underlying base pointer is a constant null.
  /// Used to avoid setting inbounds flag for old-style offsetof idioms.
  bool isUnderlyingBasePointerConstantNull(const Expr *e);

  mlir::LogicalResult emitLabel(const clang::LabelDecl &d);
  mlir::LogicalResult emitLabelStmt(const clang::LabelStmt &s);

  void emitLambdaDelegatingInvokeBody(const CXXMethodDecl *md);
  void emitLambdaStaticInvokeBody(const CXXMethodDecl *md);

  void populateCatchHandlers(cir::TryOp tryOp);

  /// Populate a TryOp's catch-all handler region with a call to
  /// std::terminate, used for noexcept terminate scopes.
  void emitTerminateHandlerBody(cir::TryOp tryOp);

  /// Populate a TryOp's catch-all handler region with a call to
  /// __cxa_call_unexpected, used for dynamic exception specification
  /// filter scopes (e.g., void foo() throw(int)).
  void emitFilterHandlerBody(cir::TryOp tryOp);

  mlir::LogicalResult emitIfStmt(const clang::IfStmt &s,
                                 llvm::ArrayRef<const Attr *> attrs = {});

  /// Emit code to compute the specified expression,
  /// ignoring the result.
  void emitIgnoredExpr(const clang::Expr *e);

  /// Emit a conditional operator where the result is ignored. This is
  /// special-cased because the conditional branches may produce non-simple
  /// LValues (e.g., bitfields) that cannot be merged into a single LValue.
  void
  emitIgnoredConditionalOperator(const AbstractConditionalOperator *e);

  RValue emitLoadOfBitfieldLValue(LValue lv, SourceLocation loc);

  /// Load a complex number from the specified l-value.
  mlir::Value emitLoadOfComplex(LValue src, SourceLocation loc);

  RValue emitLoadOfExtVectorElementLValue(LValue lv);

  RValue emitLoadOfGlobalRegLValue(LValue lv);

  /// Given an expression that represents a value lvalue, this method emits
  /// the address of the lvalue, then loads the result as an rvalue,
  /// returning the rvalue.
  RValue emitLoadOfLValue(LValue lv, SourceLocation loc);

  Address emitLoadOfReference(LValue refLVal, mlir::Location loc,
                              LValueBaseInfo *pointeeBaseInfo,
                              TBAAAccessInfo *pointeeTBAAInfo = nullptr);
  LValue emitLoadOfReferenceLValue(Address refAddr, mlir::Location loc,
                                   QualType refTy, AlignmentSource source);

  /// EmitLoadOfScalar - Load a scalar value from an address, taking
  /// care to appropriately convert from the memory representation to
  /// the LLVM value representation.  The l-value must be a simple
  /// l-value.
  mlir::Value emitLoadOfScalar(LValue lvalue, SourceLocation loc);
  mlir::Value emitLoadOfScalar(Address addr, bool isVolatile, QualType ty,
                               SourceLocation loc, LValueBaseInfo baseInfo,
                               TBAAAccessInfo tbaaInfo,
                               bool isNontemporal = false);

  /// Emit code to compute a designator that specifies the location
  /// of the expression.
  /// FIXME: document this function better.
  LValue emitLValue(const clang::Expr *e);
  LValue emitLValueForBitField(LValue base, const FieldDecl *field);
  LValue emitLValueForField(
      LValue base, const clang::FieldDecl *field,
      SourceLocation accessLoc = SourceLocation());

  LValue emitLValueForLambdaField(const FieldDecl *field);
  LValue emitLValueForLambdaField(const FieldDecl *field,
                                  mlir::Value thisValue);

  /// Like emitLValueForField, excpet that if the Field is a reference, this
  /// will return the address of the reference and not the address of the value
  /// stored in the reference.
  LValue emitLValueForFieldInitialization(LValue base,
                                          const clang::FieldDecl *field,
                                          llvm::StringRef fieldName);

  LValue emitMaterializeTemporaryExpr(const MaterializeTemporaryExpr *e);

  LValue emitMemberExpr(const MemberExpr *e);

  LValue emitOpaqueValueLValue(const OpaqueValueExpr *e);

  RValue emitPseudoObjectRValue(const PseudoObjectExpr *e,
                                AggValueSlot slot = AggValueSlot::ignored());
  LValue emitPseudoObjectLValue(const PseudoObjectExpr *e);

  LValue emitConditionalOperatorLValue(const AbstractConditionalOperator *expr);

  /// Given an expression with a pointer type, emit the value and compute our
  /// best estimate of the alignment of the pointee.
  ///
  /// One reasonable way to use this information is when there's a language
  /// guarantee that the pointer must be aligned to some stricter value, and
  /// we're simply trying to ensure that sufficiently obvious uses of under-
  /// aligned objects don't get miscompiled; for example, a placement new
  /// into the address of a local variable.  In such a case, it's quite
  /// reasonable to just ignore the returned alignment when it isn't from an
  /// explicit source.
  Address emitPointerWithAlignment(const clang::Expr *expr,
                                   LValueBaseInfo *baseInfo = nullptr,
                                   TBAAAccessInfo *tbaaInfo = nullptr,
                                   bool isKnownNonNull = false);

  /// Emits a reference binding to the passed in expression.
  RValue emitReferenceBindingToExpr(const Expr *e);

  mlir::LogicalResult emitReturnStmt(const clang::ReturnStmt &s);

  RValue emitRotate(const CallExpr *e, bool isRotateLeft);

  mlir::Value emitScalarConstant(const ConstantEmission &constant, Expr *e);

  /// Emit a conversion from the specified type to the specified destination
  /// type, both of which are CIR scalar types.
  mlir::Value emitScalarConversion(mlir::Value src, clang::QualType srcType,
                                   clang::QualType dstType,
                                   clang::SourceLocation loc);

  void emitScalarInit(const clang::Expr *init, mlir::Location loc,
                      LValue lvalue, bool capturedByInit = false,
                      const clang::ValueDecl *d = nullptr);

  mlir::Value emitScalarOrConstFoldImmArg(unsigned iceArguments, unsigned idx,
                                          const Expr *argExpr);

  void emitStaticVarDecl(const VarDecl &d, cir::GlobalLinkageKind linkage);

  void emitStoreOfComplex(mlir::Location loc, mlir::Value v, LValue dest,
                          bool isInit);

  void emitStoreOfScalar(mlir::Value value, Address addr, bool isVolatile,
                         clang::QualType ty, LValueBaseInfo baseInfo,
                         TBAAAccessInfo tbaaInfo, bool isInit = false,
                         bool isNontemporal = false);
  void emitStoreOfScalar(mlir::Value value, LValue lvalue, bool isInit);

  /// Store the specified rvalue into the specified
  /// lvalue, where both are guaranteed to the have the same type, and that type
  /// is 'Ty'.
  void emitStoreThroughLValue(RValue src, LValue dst, bool isInit = false);

  void emitStoreThroughExtVectorComponentLValue(RValue src, LValue dst);

  void emitStoreThroughGlobalRegLValue(RValue src, LValue dst);

  mlir::Value emitStoreThroughBitfieldLValue(RValue src, LValue dstresult);

  LValue emitStringLiteralLValue(const StringLiteral *e,
                                 llvm::StringRef name = ".str");
  LValue emitObjCEncodeExprLValue(const clang::ObjCEncodeExpr *e);

  mlir::LogicalResult emitSwitchBody(const clang::Stmt *s);
  mlir::LogicalResult emitSwitchCase(const clang::SwitchCase &s,
                                     bool buildingTopLevelCase,
                                     llvm::ArrayRef<const Attr *> attrs = {});
  mlir::LogicalResult emitSwitchStmt(const clang::SwitchStmt &s,
                                     llvm::ArrayRef<const Attr *> attrs = {});

  std::optional<mlir::Value>
  emitTargetBuiltinExpr(unsigned builtinID, const clang::CallExpr *e,
                        ReturnValueSlot &returnValue);

  /// Given a value and its clang type, returns the value casted to its memory
  /// representation.
  /// Note: CIR defers most of the special casting to the final lowering passes
  /// to conserve the high level information.
  mlir::Value emitToMemory(mlir::Value value, clang::QualType ty);

  /// EmitFromMemory - Change a scalar value from its memory
  /// representation to its value representation.
  mlir::Value emitFromMemory(mlir::Value value, clang::QualType ty);

  /// Emit a trap instruction, which is used to abort the program in an abnormal
  /// way, usually for debugging purposes.
  /// \p createNewBlock indicates whether to create a new block for the IR
  /// builder. Since the `cir.trap` operation is a terminator, operations that
  /// follow a trap cannot be emitted after `cir.trap` in the same block. To
  /// ensure these operations get emitted successfully, you need to create a new
  /// dummy block and set the insertion point there before continuing from the
  /// trap operation.
  void emitTrap(mlir::Location loc, bool createNewBlock);

  LValue emitUnaryOpLValue(const clang::UnaryOperator *e);

  mlir::Value emitUnPromotedValue(mlir::Value result, QualType unPromotionType);

  /// Emit a reached-unreachable diagnostic if \p loc is valid and runtime
  /// checking is enabled. Otherwise, just emit an unreachable instruction.
  /// \p createNewBlock indicates whether to create a new block for the IR
  /// builder. Since the `cir.unreachable` operation is a terminator, operations
  /// that follow an unreachable point cannot be emitted after `cir.unreachable`
  /// in the same block. To ensure these operations get emitted successfully,
  /// you need to create a dummy block and set the insertion point there before
  /// continuing from the unreachable point.
  void emitUnreachable(clang::SourceLocation loc, bool createNewBlock);

  /// This method handles emission of any variable declaration
  /// inside a function, including static vars etc.
  void emitVarDecl(const clang::VarDecl &d);

  void emitVariablyModifiedType(QualType ty);

  mlir::LogicalResult emitWhileStmt(const clang::WhileStmt &s,
                                    llvm::ArrayRef<const clang::Attr *> attrs = {});

  std::optional<mlir::Value> emitX86BuiltinExpr(unsigned builtinID,
                                                const CallExpr *expr);

  mlir::Value emitNVPTXBuiltinExpr(unsigned builtinID, const CallExpr *expr);

  mlir::Value emitNVPTXDevicePrintfCallExpr(const CallExpr *expr);

  /// Given an assignment `*lhs = rhs`, emit a test that checks if \p rhs is
  /// nonnull, if 1\p LHS is marked _Nonnull.
  void emitNullabilityCheck(LValue lhs, mlir::Value rhs,
                            clang::SourceLocation loc);

  /// Emit a nonnull argument sanitizer check for a call argument.
  void emitNonNullArgCheck(RValue rv, clang::QualType argType,
                           clang::SourceLocation argLoc, AbstractCallee callee,
                           unsigned parmNum);

  /// Emit a nonnull argument sanitizer check for a call argument address.
  void emitNonNullArgCheck(Address addr, clang::QualType argType,
                           clang::SourceLocation argLoc, AbstractCallee callee,
                           unsigned parmNum);

  /// Compute the nonnull condition for an rvalue, including C++ member
  /// pointers.
  mlir::Value emitNonNullRValueCheck(RValue rv, clang::QualType type);

  //===--------------------------------------------------------------------===//
  //                     UBSan Type Check Support
  //===--------------------------------------------------------------------===//

  /// Situations in which we might emit a check for the suitability of a pointer
  /// or glvalue. Matches OGCG CodeGenFunction::TypeCheckKind.
  enum TypeCheckKind {
    /// Checking the operand of a load. Must be suitably sized and aligned.
    TCK_Load,
    /// Checking the destination of a store. Must be suitably sized and aligned.
    TCK_Store,
    /// Checking the bound value in a reference binding.
    TCK_ReferenceBinding,
    /// Checking the object expression in a non-static data member access.
    TCK_MemberAccess,
    /// Checking the 'this' pointer for a call to a non-static member function.
    TCK_MemberCall,
    /// Checking the 'this' pointer for a constructor call.
    TCK_ConstructorCall,
    /// Checking the operand of a static_cast to a derived pointer type.
    TCK_DowncastPointer,
    /// Checking the operand of a static_cast to a derived reference type.
    TCK_DowncastReference,
    /// Checking the operand of a cast to a base object.
    TCK_Upcast,
    /// Checking the operand of a cast to a virtual base object.
    TCK_UpcastToVirtualBase,
    /// Checking the value assigned to a _Nonnull pointer.
    TCK_NonnullAssign,
    /// Checking the operand of a dynamic_cast or a typeid expression.
    TCK_DynamicOperation
  };

  /// Determine whether the given type-check kind allows null pointers.
  static bool isNullPointerAllowed(TypeCheckKind TCK);

  /// Determine whether the given type-check kind requires a vptr check.
  static bool isVptrCheckRequired(TypeCheckKind TCK, clang::QualType Ty);

  /// Whether any type-check sanitizers are enabled.
  bool sanitizePerformTypeCheck() const;

  LValue emitCheckedLValue(const clang::Expr *e, TypeCheckKind tck);

  /// Emit a check that \p Ptr is a valid pointer for type \p Ty, with the
  /// given alignment. Emits null, alignment, object-size, and vptr checks
  /// as appropriate based on sanitizer settings.
  void emitTypeCheck(TypeCheckKind TCK, clang::SourceLocation Loc,
                     mlir::Value Ptr, clang::QualType Ty,
                     clang::CharUnits Alignment = clang::CharUnits::Zero(),
                     clang::SanitizerSet SkippedChecks = {},
                     mlir::Value ArraySize = {});

  /// Compute the total byte offset for a GEP-style pointer step and whether
  /// scaling the index by the element size overflowed under signed GEP
  /// semantics.
  std::pair<mlir::Value, mlir::Value>
  emitCheckedByteOffset(mlir::Location loc, mlir::Value index,
                        uint64_t elementSize);

  /// Emit UBSan pointer-overflow instrumentation for a GEP-style pointer step
  /// with the given base pointer and total byte offset.
  void emitPointerOverflowCheck(mlir::Location loc, mlir::Value basePtr,
                                mlir::Value totalOffset,
                                mlir::Value offsetOverflows,
                                bool signedIndices, bool isSubtraction,
                                clang::SourceLocation sourceLoc);

  /// Load a compatible hidden pass_object_size parameter for \p E and return
  /// its bound in elements of \p EltTy, or null if no compatible bound exists.
  mlir::Value loadPassedObjectSize(const Expr *E, clang::QualType EltTy);

  /// Emit UBSan out-of-bounds instrumentation for an indexing operation on
  /// \p ArrayExprBase using \p IndexVal. If \p Accessed is false, one-past
  /// addresses are permitted.
  void emitBoundsCheck(const Expr *ArrayExpr, const Expr *ArrayExprBase,
                       mlir::Value IndexVal, clang::QualType IndexType,
                       bool Accessed);

  void emitSanitizerStatReportStub(
      mlir::Location loc, CFISanitizerStatKind kind,
      mlir::Operation *insertBefore = nullptr);

  /// Guard a UBSan "pass" condition with llvm.allow.ubsan.check when
  /// -mllvm -ubsan-guard-checks or per-check hot cutoffs request it.
  mlir::Value emitGuardedUBSanCheck(
      mlir::Location loc, mlir::Value pass,
      clang::SanitizerKind::SanitizerOrdinal ordinal);

  /// Attach call-graph-section callee type metadata to a call if enabled.
  void maybeAttachCallGraphSectionAttrs(cir::CIRCallOpInterface call,
                                        QualType fnType);

  //===--------------------------------------------------------------------===//
  //                         Thunk Emission
  //===--------------------------------------------------------------------===//

  /// Generate a thunk for the given method.
  void generateThunk(cir::FuncOp fn, const CIRGenFunctionInfo &fnInfo,
                     clang::GlobalDecl gd, const clang::ThunkInfo &thunk,
                     bool isUnprototyped);

  void startThunk(cir::FuncOp fn, clang::GlobalDecl gd,
                  const CIRGenFunctionInfo &fnInfo, bool isUnprototyped);

  void emitCallAndReturnForThunk(cir::FuncOp callee,
                                 const clang::ThunkInfo *thunk,
                                 bool isUnprototyped);

  /// Finish thunk generation.
  void finishThunk();

  /// Emit a musttail call for a thunk with a potentially adjusted this pointer.
  void emitMustTailThunk(clang::GlobalDecl gd, mlir::Value adjustedThisPtr,
                         cir::FuncOp callee);

  /// Generate a thunk for a virtual method with varargs and covariant return.
  /// Clones the base function body and adjusts this pointer and return values.
  void generateVarArgsThunk(cir::FuncOp fn, const CIRGenFunctionInfo &fnInfo,
                            clang::GlobalDecl gd, const ThunkInfo &thunk);

  /// An object to manage conditionally-evaluated expressions.
  class ConditionalEvaluation {
    CIRGenFunction &cgf;
    mlir::OpBuilder::InsertPoint insertPt;

  public:
    ConditionalEvaluation(CIRGenFunction &cgf)
        : cgf(cgf), insertPt(cgf.builder.saveInsertionPoint()) {}
    ConditionalEvaluation(CIRGenFunction &cgf, mlir::OpBuilder::InsertPoint ip)
        : cgf(cgf), insertPt(ip) {}

    void beginEvaluation() {
      assert(cgf.outermostConditional != this);
      if (!cgf.outermostConditional)
        cgf.outermostConditional = this;
    }

    void endEvaluation() {
      assert(cgf.outermostConditional != nullptr);
      if (cgf.outermostConditional == this)
        cgf.outermostConditional = nullptr;
    }

    /// Returns the insertion point which will be executed prior to each
    /// evaluation of the conditional code. In LLVM OG, this method
    /// is called getStartingBlock.
    mlir::OpBuilder::InsertPoint getInsertPoint() const { return insertPt; }
  };

  struct ConditionalInfo {
    std::optional<LValue> lhs{}, rhs{};
    mlir::Value result{};
  };

  // Return true if we're currently emitting one branch or the other of a
  // conditional expression.
  bool isInConditionalBranch() const { return outermostConditional != nullptr; }

  void setBeforeOutermostConditional(mlir::Value value, Address addr) {
    assert(isInConditionalBranch());
    {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.restoreInsertionPoint(outermostConditional->getInsertPoint());
      builder.createStore(
          value.getLoc(), value, addr, /*isVolatile=*/false,
          /*isNontemporal=*/false,
          mlir::IntegerAttr::get(
              mlir::IntegerType::get(value.getContext(), 64),
              (uint64_t)addr.getAlignment().getAsAlign().value()));
    }
  }

  // Points to the outermost active conditional control. This is used so that
  // we know if a temporary should be destroyed conditionally.
  ConditionalEvaluation *outermostConditional = nullptr;

  /// An RAII object to record that we're evaluating a statement
  /// expression.
  class StmtExprEvaluation {
    CIRGenFunction &cgf;

    /// We have to save the outermost conditional: cleanups in a
    /// statement expression aren't conditional just because the
    /// StmtExpr is.
    ConditionalEvaluation *savedOutermostConditional;

  public:
    StmtExprEvaluation(CIRGenFunction &cgf)
        : cgf(cgf), savedOutermostConditional(cgf.outermostConditional) {
      cgf.outermostConditional = nullptr;
    }

    ~StmtExprEvaluation() {
      cgf.outermostConditional = savedOutermostConditional;
    }
  };

  template <typename FuncTy>
  ConditionalInfo emitConditionalBlocks(const AbstractConditionalOperator *e,
                                        const FuncTy &branchGenFunc);

  mlir::Value emitTernaryOnBoolExpr(const clang::Expr *cond, mlir::Location loc,
                                    const clang::Stmt *thenS,
                                    const clang::Stmt *elseS);

  /// Build a "reference" to a va_list; this is either the address or the value
  /// of the expression, depending on how va_list is defined.
  Address emitVAListRef(const Expr *e);

  /// Build a "reference" to a Microsoft va_list. Unlike plain va_list,
  /// __builtin_ms_va_list is always handled as an lvalue slot.
  Address emitMSVAListRef(const Expr *e);

  /// Emits the start of a CIR variable-argument operation (`cir.va_start`)
  ///
  /// \param vaList A reference to the \c va_list as emitted by either
  /// \c emitVAListRef or \c emitMSVAListRef.
  ///
  /// \param count The number of arguments in \c vaList
  void emitVAStart(mlir::Value vaList, mlir::Value count);

  /// Emits the end of a CIR variable-argument operation (`cir.va_start`)
  ///
  /// \param vaList A reference to the \c va_list as emitted by either
  /// \c emitVAListRef or \c emitMSVAListRef.
  void emitVAEnd(mlir::Value vaList);

  /// Generate code to get an argument from the passed in pointer
  /// and update it accordingly.
  ///
  /// \param ve The \c VAArgExpr for which to generate code.
  ///
  /// \param vaListAddr Receives a reference to the \c va_list as emitted by
  /// either \c emitVAListRef or \c emitMSVAListRef.
  ///
  /// \returns SSA value with the argument.
  mlir::Value emitVAArg(VAArgExpr *ve);

  /// ----------------------
  /// CIR build helpers
  /// -----------------
public:
  cir::AllocaOp createTempAlloca(mlir::Type ty, mlir::Location loc,
                                 const Twine &name = "tmp",
                                 mlir::Value arraySize = nullptr,
                                 bool insertIntoFnEntryBlock = false);
  cir::AllocaOp createTempAlloca(mlir::Type ty, mlir::Location loc,
                                 const Twine &name = "tmp",
                                 mlir::OpBuilder::InsertPoint ip = {},
                                 mlir::Value arraySize = nullptr);
  /// Create a temporary alloca and optionally cast it to a destination address
  /// space. If \p destAddrSpace is provided, the alloca pointer is cast to that
  /// address space; otherwise it is cast to the default (generic) address
  /// space. When \p destAddrSpace matches the alloca's address space, no cast
  /// is emitted. Callers that need the address in a specific address space
  /// (e.g. OpenCL CL1.2 where locals are in the private AS) should pass the
  /// type's address space.
  Address createTempAlloca(mlir::Type ty, CharUnits align, mlir::Location loc,
                           const Twine &name = "tmp",
                           mlir::Value arraySize = nullptr,
                           Address *alloca = nullptr,
                           mlir::OpBuilder::InsertPoint ip = {},
                           std::optional<clang::LangAS> destLangAS = {});
  Address createTempAllocaWithoutCast(mlir::Type ty, CharUnits align,
                                      mlir::Location loc,
                                      const Twine &name = "tmp",
                                      mlir::Value arraySize = nullptr,
                                      mlir::OpBuilder::InsertPoint ip = {});

  /// Create a temporary memory object of the given type, with
  /// appropriate alignmen and cast it to the default address space. Returns
  /// the original alloca instruction by \p Alloca if it is not nullptr.
  Address createMemTemp(QualType t, mlir::Location loc,
                        const Twine &name = "tmp", Address *alloca = nullptr,
                        mlir::OpBuilder::InsertPoint ip = {});
  Address createMemTemp(QualType t, CharUnits align, mlir::Location loc,
                        const Twine &name = "tmp", Address *alloca = nullptr,
                        mlir::OpBuilder::InsertPoint ip = {});

  //===--------------------------------------------------------------------===//
  //                         OpenMP Emission
  //===--------------------------------------------------------------------===//
public:
  mlir::LogicalResult emitOMPScopeDirective(const OMPScopeDirective &s);
  mlir::LogicalResult emitOMPErrorDirective(const OMPErrorDirective &s);
  mlir::LogicalResult emitOMPParallelDirective(const OMPParallelDirective &s);
  mlir::LogicalResult emitOMPTaskwaitDirective(const OMPTaskwaitDirective &s);
  mlir::LogicalResult emitOMPTaskyieldDirective(const OMPTaskyieldDirective &s);
  mlir::LogicalResult emitOMPBarrierDirective(const OMPBarrierDirective &s);
  mlir::LogicalResult emitOMPMetaDirective(const OMPMetaDirective &s);
  mlir::LogicalResult emitOMPCanonicalLoop(const OMPCanonicalLoop &s);
  mlir::LogicalResult emitOMPSimdDirective(const OMPSimdDirective &s);
  mlir::LogicalResult emitOMPTileDirective(const OMPTileDirective &s);
  mlir::LogicalResult emitOMPUnrollDirective(const OMPUnrollDirective &s);
  mlir::LogicalResult emitOMPFuseDirective(const OMPFuseDirective &s);
  mlir::LogicalResult emitOMPForDirective(const OMPForDirective &s);
  mlir::LogicalResult emitOMPForSimdDirective(const OMPForSimdDirective &s);
  mlir::LogicalResult emitOMPSectionsDirective(const OMPSectionsDirective &s);
  mlir::LogicalResult emitOMPSectionsWithWsloop(
      ArrayRef<const OMPClause *> wsloopClauses, const Stmt *body,
      ArrayRef<const VarDecl *> enclosingFirstprivateVars,
      SourceLocation directiveLoc, SourceLocation endLoc, bool nowait,
      bool emitNowaitLastprivateBarrier);
  mlir::LogicalResult emitOMPSectionDirective(const OMPSectionDirective &s);
  mlir::LogicalResult emitOMPSingleDirective(const OMPSingleDirective &s);
  mlir::LogicalResult emitOMPMasterDirective(const OMPMasterDirective &s);
  mlir::LogicalResult emitOMPCriticalDirective(const OMPCriticalDirective &s);
  mlir::LogicalResult
  emitOMPParallelForDirective(const OMPParallelForDirective &s);
  mlir::LogicalResult
  emitOMPParallelForSimdDirective(const OMPParallelForSimdDirective &s);
  mlir::LogicalResult
  emitOMPParallelMasterDirective(const OMPParallelMasterDirective &s);
  mlir::LogicalResult
  emitOMPParallelSectionsDirective(const OMPParallelSectionsDirective &s);
  mlir::LogicalResult emitOMPTaskDirective(const OMPTaskDirective &s);
  mlir::LogicalResult emitOMPTaskgroupDirective(const OMPTaskgroupDirective &s);
  mlir::LogicalResult emitOMPFlushDirective(const OMPFlushDirective &s);
  mlir::LogicalResult emitOMPDepobjDirective(const OMPDepobjDirective &s);
  mlir::LogicalResult emitOMPScanDirective(const OMPScanDirective &s);
  mlir::LogicalResult emitOMPOrderedDirective(const OMPOrderedDirective &s);
  mlir::LogicalResult emitOMPAtomicDirective(const OMPAtomicDirective &s);
  mlir::LogicalResult emitOMPTargetDirective(const OMPTargetDirective &s);
  mlir::LogicalResult emitOMPTeamsDirective(const OMPTeamsDirective &s);
  mlir::LogicalResult
  emitOMPCancellationPointDirective(const OMPCancellationPointDirective &s);
  mlir::LogicalResult emitOMPCancelDirective(const OMPCancelDirective &s);
  mlir::LogicalResult
  emitOMPTargetDataDirective(const OMPTargetDataDirective &s);
  mlir::LogicalResult
  emitOMPTargetEnterDataDirective(const OMPTargetEnterDataDirective &s);
  mlir::LogicalResult
  emitOMPTargetExitDataDirective(const OMPTargetExitDataDirective &s);
  mlir::LogicalResult
  emitOMPTargetParallelDirective(const OMPTargetParallelDirective &s);
  mlir::LogicalResult
  emitOMPTargetParallelForDirective(const OMPTargetParallelForDirective &s);
  mlir::LogicalResult emitSimpleOMPTargetParallelWsloopDirective(
      const OMPLoopDirective &s, llvm::StringRef directiveName);
  mlir::LogicalResult emitOMPTaskLoopDirective(const OMPTaskLoopDirective &s);
  mlir::LogicalResult
  emitOMPTaskLoopSimdDirective(const OMPTaskLoopSimdDirective &s);
  mlir::LogicalResult
  emitOMPMaskedTaskLoopDirective(const OMPMaskedTaskLoopDirective &s);
  mlir::LogicalResult
  emitOMPMaskedTaskLoopSimdDirective(const OMPMaskedTaskLoopSimdDirective &s);
  mlir::LogicalResult
  emitOMPMasterTaskLoopDirective(const OMPMasterTaskLoopDirective &s);
  mlir::LogicalResult
  emitOMPMasterTaskLoopSimdDirective(const OMPMasterTaskLoopSimdDirective &s);
  mlir::LogicalResult
  emitOMPParallelGenericLoopDirective(const OMPParallelGenericLoopDirective &s);
  mlir::LogicalResult
  emitOMPParallelMaskedDirective(const OMPParallelMaskedDirective &s);
  mlir::LogicalResult emitOMPParallelMaskedTaskLoopDirective(
      const OMPParallelMaskedTaskLoopDirective &s);
  mlir::LogicalResult emitOMPParallelMaskedTaskLoopSimdDirective(
      const OMPParallelMaskedTaskLoopSimdDirective &s);
  mlir::LogicalResult emitOMPParallelMasterTaskLoopDirective(
      const OMPParallelMasterTaskLoopDirective &s);
  mlir::LogicalResult emitOMPParallelMasterTaskLoopSimdDirective(
      const OMPParallelMasterTaskLoopSimdDirective &s);
  mlir::LogicalResult
  emitOMPDistributeDirective(const OMPDistributeDirective &s);
  mlir::LogicalResult emitOMPDistributeParallelForDirective(
      const OMPDistributeParallelForDirective &s);
  mlir::LogicalResult emitOMPDistributeParallelForSimdDirective(
      const OMPDistributeParallelForSimdDirective &s);
  mlir::LogicalResult
  emitOMPDistributeSimdDirective(const OMPDistributeSimdDirective &s);
  mlir::LogicalResult emitOMPTargetParallelGenericLoopDirective(
      const OMPTargetParallelGenericLoopDirective &s);
  mlir::LogicalResult emitOMPTargetParallelForSimdDirective(
      const OMPTargetParallelForSimdDirective &s);
  mlir::LogicalResult
  emitOMPTargetSimdDirective(const OMPTargetSimdDirective &s);
  mlir::LogicalResult emitOMPTargetTeamsGenericLoopDirective(
      const OMPTargetTeamsGenericLoopDirective &s);
  mlir::LogicalResult
  emitOMPTargetUpdateDirective(const OMPTargetUpdateDirective &s);
  mlir::LogicalResult
  emitOMPTeamsDistributeDirective(const OMPTeamsDistributeDirective &s);
  mlir::LogicalResult
  emitOMPTeamsDistributeSimdDirective(const OMPTeamsDistributeSimdDirective &s);
  mlir::LogicalResult emitOMPTeamsDistributeParallelForSimdDirective(
      const OMPTeamsDistributeParallelForSimdDirective &s);
  mlir::LogicalResult emitOMPTeamsDistributeParallelForDirective(
      const OMPTeamsDistributeParallelForDirective &s);
  mlir::LogicalResult
  emitOMPTeamsGenericLoopDirective(const OMPTeamsGenericLoopDirective &s);
  mlir::LogicalResult
  emitOMPTargetTeamsDirective(const OMPTargetTeamsDirective &s);
  mlir::LogicalResult emitOMPTargetTeamsDistributeDirective(
      const OMPTargetTeamsDistributeDirective &s);
  mlir::LogicalResult emitOMPTargetTeamsDistributeParallelForDirective(
      const OMPTargetTeamsDistributeParallelForDirective &s);
  mlir::LogicalResult emitOMPTargetTeamsDistributeParallelForSimdDirective(
      const OMPTargetTeamsDistributeParallelForSimdDirective &s);
  mlir::LogicalResult emitOMPTargetTeamsDistributeSimdDirective(
      const OMPTargetTeamsDistributeSimdDirective &s);
  mlir::LogicalResult emitOMPTargetTeamsDistributeDirectiveImpl(
      const OMPLoopDirective &s, llvm::StringRef directiveName,
      bool allowBind);
  mlir::LogicalResult emitOMPInteropDirective(const OMPInteropDirective &s);
  mlir::LogicalResult emitOMPDispatchDirective(const OMPDispatchDirective &s);
  mlir::LogicalResult
  emitOMPGenericLoopDirective(const OMPGenericLoopDirective &s);
  mlir::LogicalResult emitOMPReverseDirective(const OMPReverseDirective &s);
  mlir::LogicalResult
  emitOMPInterchangeDirective(const OMPInterchangeDirective &s);
  mlir::LogicalResult emitOMPAssumeDirective(const OMPAssumeDirective &s);
  mlir::LogicalResult emitOMPMaskedDirective(const OMPMaskedDirective &s);
  mlir::LogicalResult emitOMPStripeDirective(const OMPStripeDirective &s);
  mlir::LogicalResult emitOMPTargetTeamsDistributeParallelForDirectiveImpl(
      const OMPLoopDirective &s, llvm::StringRef directiveName,
      bool allowBind, bool hasCancel);

  void emitOMPThreadPrivateDecl(const OMPThreadPrivateDecl &d);
  void emitOMPThreadPrivateVarDefinition(const VarDecl *vd, Address vdAddr,
                                         bool performInit);
  mlir::Value emitOpenMPRuntimeLocation(SourceLocation loc,
                                        unsigned flags = 0);
  mlir::Value emitOpenMPThreadID(SourceLocation loc, unsigned flags = 0);
  bool shouldEmitOpenMPTargetAsOperation() const {
    return !getLangOpts().OpenMPIsTargetDevice || forceOpenMPTargetOpEmission;
  }
  bool shouldUseOpenMPTargetSharedLocalStorage(const VarDecl &d) const {
    return emittingOpenMPTargetGenericFunction &&
           openMPTargetEscapedLocalDecls.count(d.getCanonicalDecl()) != 0;
  }
  llvm::StringRef getOpenMPTargetParentNameOverride() const {
    return openMPTargetParentNameOverride;
  }
  bool isOpenMPNontemporalDecl(const ValueDecl *vd) const;
  bool pushOpenMPLastprivateConditional(ArrayRef<const OMPClause *> clauses,
                                        Address ivAddr,
                                        SourceLocation directiveLoc);
  bool pushDisabledOpenMPLastprivateConditional(
      const OMPExecutableDirective &directive);
  void popOpenMPLastprivateConditional();
  void checkAndEmitOpenMPLastprivateConditional(const Expr *lhs);
  void emitOpenMPLastprivateConditionalFinalUpdate(const VarDecl *vd,
                                                   Address privateAddr,
                                                   SourceLocation loc);
  void emitScannedOMPTargetDirective(const OMPExecutableDirective &d,
                                     llvm::StringRef parentName);
  void emitOMPGroupPrivateDecl(const OMPGroupPrivateDecl &d);
  void emitOMPCapturedExpr(const OMPCapturedExprDecl &d);
  void emitOMPAllocateDecl(const OMPAllocateDecl &d);
  void emitOMPDeclareReduction(const OMPDeclareReductionDecl &d);
  void emitOMPDeclareMapper(const OMPDeclareMapperDecl &d);
  void emitOMPRequiresDecl(const OMPRequiresDecl &d);

  struct OpenMPTaskReductionRuntimeSizeInfo {
    unsigned index;
    const VarDecl *privateVar;
    const VarDecl *remapVar;
    mlir::Value runtimeElementCount;
  };

  struct OpenMPClauseEmissionInfo {
    struct OpenMPUsesAllocatorInfo {
      const VarDecl *allocatorVar = nullptr;
      const VarDecl *traitsVar = nullptr;
      unsigned mapIndex = 0;
    };
    struct OpenMPTargetAllocatorPrivateInfo {
      const VarDecl *privateVar = nullptr;
      const VarDecl *allocatorVar = nullptr;
      const VarDecl *privateCopyVD = nullptr;
      const VarDecl *initVD = nullptr;
      int64_t sourceMapIndex = -1;
    };
    struct OpenMPExplicitPrivateAllocatorInfo {
      const VarDecl *privateVar = nullptr;
      const VarDecl *privateCopyVD = nullptr;
      const VarDecl *initVD = nullptr;
      bool initializeFromOriginal = false;
    };

    std::optional<unsigned> targetThisMapIndex;
    llvm::SmallVector<unsigned, 4> hasDeviceAddrIndices;
    llvm::SmallVector<const VarDecl *, 4> hasDeviceAddrVars;
    llvm::SmallVector<unsigned, 4> mapIndices;
    llvm::SmallVector<unsigned, 4> mapReferenceValueIndices;
    llvm::SmallVector<const VarDecl *, 4> mapVars;
    llvm::SmallVector<OpenMPUsesAllocatorInfo, 4> usesAllocatorVars;
    llvm::SmallVector<OpenMPExplicitPrivateAllocatorInfo, 4>
        explicitPrivateAllocatorVars;
    llvm::SmallVector<OpenMPTargetAllocatorPrivateInfo, 4>
        targetAllocatorPrivateVars;
    llvm::SmallVector<unsigned, 4> privateIndices;
    llvm::SmallVector<const VarDecl *, 4> privateVars;
    llvm::SmallDenseMap<const VarDecl *, unsigned, 4>
        targetFirstprivateMapFlags;
    llvm::SmallVector<unsigned, 4> capturedVLASizeIndices;
    llvm::SmallVector<const Expr *, 4> capturedVLASizeExprs;
    llvm::SmallVector<unsigned, 4> lastprivateIndices;
    llvm::SmallVector<const VarDecl *, 4> lastprivateVars;
    llvm::SmallVector<const VarDecl *, 4> lastprivateSourceVDs;
    llvm::SmallVector<const VarDecl *, 4> lastprivateDestVDs;
    llvm::SmallVector<const Expr *, 4> lastprivateAssignmentOps;
    llvm::SmallVector<bool, 4> lastprivateConditionals;
    llvm::SmallVector<unsigned, 4> inReductionIndices;
    llvm::SmallVector<const VarDecl *, 4> inReductionVars;
    llvm::SmallVector<OpenMPTaskReductionRuntimeSizeInfo, 4>
        inReductionRuntimeSizes;
    llvm::SmallVector<unsigned, 4> reductionIndices;
    llvm::SmallVector<const VarDecl *, 4> reductionVars;
    llvm::SmallVector<OpenMPTaskReductionRuntimeSizeInfo, 4>
        reductionRuntimeSizes;
    /// An array-section reduction: the body reaches the section through its
    /// base variable, which must then resolve into the private copy.
    /// `offset` is the section start's element offset from the base.
    struct OpenMPReductionSectionInfo {
      unsigned index;
      const VarDecl *baseVar;
      mlir::Value offset;
    };
    llvm::SmallVector<OpenMPReductionSectionInfo, 4> reductionSections;
    llvm::SmallVector<unsigned, 4> taskReductionIndices;
    llvm::SmallVector<const VarDecl *, 4> taskReductionVars;
    llvm::SmallVector<OpenMPTaskReductionRuntimeSizeInfo, 4>
        taskReductionRuntimeSizes;
    bool useRuntimeDependPayload = false;
  };

  template <typename Op>
  OpenMPClauseEmissionInfo
  emitOpenMPClauseInfo(Op &op, ArrayRef<const OMPClause *> clauses,
                       ArrayRef<const VarDecl *> enclosingFirstprivateVars = {},
                       bool allowWsloopConditionalLastprivate = false) {
    return emitOpenMPClauses(op, clauses, enclosingFirstprivateVars,
                             allowWsloopConditionalLastprivate);
  }

private:
  template <typename Op>
  OpenMPClauseEmissionInfo
  emitOpenMPClauses(Op &op, ArrayRef<const OMPClause *> clauses,
                    ArrayRef<const VarDecl *> enclosingFirstprivateVars = {},
                    bool allowWsloopConditionalLastprivate = false);

  mlir::LogicalResult emitSimpleOMPTaskLoopDirective(
      const OMPLoopDirective &s, llvm::StringRef directiveName,
      bool wrapInMaster, bool wrapInMasked, bool simd);
  mlir::LogicalResult emitSimpleOMPForDirective(
      const OMPLoopDirective &s, ArrayRef<const OMPClause *> clauses,
      llvm::StringRef directiveName, bool hasCancel);
  mlir::LogicalResult emitSimpleOpenMPSimdDirective(
      const OMPLoopDirective &s, ArrayRef<const OMPClause *> clauses,
      llvm::StringRef directiveName, llvm::StringRef clauseContext);
  mlir::LogicalResult emitOMPDistributeParallelWsloopDirective(
      const OMPLoopDirective &s, llvm::StringRef directiveName, bool simd);
  mlir::LogicalResult emitSimpleOMPParallelTaskLoopDirective(
      const OMPLoopDirective &s, llvm::StringRef directiveName,
      bool wrapInMaster, bool wrapInMasked, bool simd);
  mlir::LogicalResult emitSimpleOMPParallelDirective(
      const OMPExecutableDirective &s, llvm::StringRef directiveName,
      bool wrapInMaster, bool wrapInMasked);

  //===--------------------------------------------------------------------===//
  //                         OpenACC Emission
  //===--------------------------------------------------------------------===//
private:
  template <typename Op>
  Op emitOpenACCOp(mlir::Location start, OpenACCDirectiveKind dirKind,
                   llvm::ArrayRef<const OpenACCClause *> clauses);
  // Function to do the basic implementation of an operation with an Associated
  // Statement.  Models AssociatedStmtConstruct.
  template <typename Op, typename TermOp>
  mlir::LogicalResult
  emitOpenACCOpAssociatedStmt(mlir::Location start, mlir::Location end,
                              OpenACCDirectiveKind dirKind,
                              llvm::ArrayRef<const OpenACCClause *> clauses,
                              const Stmt *associatedStmt);

  template <typename Op, typename TermOp>
  mlir::LogicalResult emitOpenACCOpCombinedConstruct(
      mlir::Location start, mlir::Location end, OpenACCDirectiveKind dirKind,
      llvm::ArrayRef<const OpenACCClause *> clauses, const Stmt *loopStmt);

  template <typename Op>
  void emitOpenACCClauses(Op &op, OpenACCDirectiveKind dirKind,
                          ArrayRef<const OpenACCClause *> clauses);
  // The second template argument doesn't need to be a template, since it should
  // always be an mlir::acc::LoopOp, but as this is a template anyway, we make
  // it a template argument as this way we can avoid including the OpenACC MLIR
  // headers here. We will count on linker failures/explicit instantiation to
  // ensure we don't mess this up, but it is only called from 1 place, and
  // instantiated 3x.
  template <typename ComputeOp, typename LoopOp>
  void emitOpenACCClauses(ComputeOp &op, LoopOp &loopOp,
                          OpenACCDirectiveKind dirKind,
                          ArrayRef<const OpenACCClause *> clauses);

  // The OpenACC LoopOp requires that we have auto, seq, or independent on all
  // LoopOp operations for the 'none' device type case. This function checks if
  // the LoopOp has one, else it updates it to have one.
  void updateLoopOpParallelism(mlir::acc::LoopOp &op, bool isOrphan,
                               OpenACCDirectiveKind dk);

  // The OpenACC 'cache' construct actually applies to the 'loop' if present. So
  // keep track of the 'loop' so that we can add the cache vars to it correctly.
  mlir::acc::LoopOp *activeLoopOp = nullptr;

  struct ActiveOpenACCLoopRAII {
    CIRGenFunction &cgf;
    mlir::acc::LoopOp *oldLoopOp;

    ActiveOpenACCLoopRAII(CIRGenFunction &cgf, mlir::acc::LoopOp *newOp)
        : cgf(cgf), oldLoopOp(cgf.activeLoopOp) {
      cgf.activeLoopOp = newOp;
    }
    ~ActiveOpenACCLoopRAII() { cgf.activeLoopOp = oldLoopOp; }
  };

  // Keep track of the last place we inserted a 'recipe' so that we can insert
  // the next one in lexical order.
  mlir::OpBuilder::InsertPoint lastRecipeLocation;

public:
  // Helper type used to store the list of important information for a 'data'
  // clause variable, or a 'cache' variable reference.
  struct OpenACCDataOperandInfo {
    mlir::Location beginLoc;
    mlir::Value varValue;
    std::string name;
    // The type of the original variable reference: that is, after 'bounds' have
    // removed pointers/array types/etc. So in the case of int arr[5], and a
    // private(arr[1]), 'origType' is 'int', but 'baseType' is 'int[5]'.
    QualType origType;
    QualType baseType;
    llvm::SmallVector<mlir::Value> bounds;
    // The list of types that we found when going through the bounds, which we
    // can use to properly set the alloca section.
    llvm::SmallVector<QualType> boundTypes;
  };

  // Gets the collection of info required to lower and OpenACC clause or cache
  // construct variable reference.
  OpenACCDataOperandInfo getOpenACCDataOperandInfo(const Expr *e);
  // Helper function to emit the integer expressions as required by an OpenACC
  // clause/construct.
  mlir::Value emitOpenACCIntExpr(const Expr *intExpr);
  // Helper function to emit an integer constant as an mlir int type, used for
  // constants in OpenACC constructs/clauses.
  mlir::Value createOpenACCConstantInt(mlir::Location loc, unsigned width,
                                       int64_t value);

  mlir::LogicalResult
  emitOpenACCComputeConstruct(const OpenACCComputeConstruct &s);
  mlir::LogicalResult emitOpenACCLoopConstruct(const OpenACCLoopConstruct &s);
  mlir::LogicalResult
  emitOpenACCCombinedConstruct(const OpenACCCombinedConstruct &s);
  mlir::LogicalResult emitOpenACCDataConstruct(const OpenACCDataConstruct &s);
  mlir::LogicalResult
  emitOpenACCEnterDataConstruct(const OpenACCEnterDataConstruct &s);
  mlir::LogicalResult
  emitOpenACCExitDataConstruct(const OpenACCExitDataConstruct &s);
  mlir::LogicalResult
  emitOpenACCHostDataConstruct(const OpenACCHostDataConstruct &s);
  mlir::LogicalResult emitOpenACCWaitConstruct(const OpenACCWaitConstruct &s);
  mlir::LogicalResult emitOpenACCInitConstruct(const OpenACCInitConstruct &s);
  mlir::LogicalResult
  emitOpenACCShutdownConstruct(const OpenACCShutdownConstruct &s);
  mlir::LogicalResult emitOpenACCSetConstruct(const OpenACCSetConstruct &s);
  mlir::LogicalResult
  emitOpenACCUpdateConstruct(const OpenACCUpdateConstruct &s);
  mlir::LogicalResult
  emitOpenACCAtomicConstruct(const OpenACCAtomicConstruct &s);
  mlir::LogicalResult emitOpenACCCacheConstruct(const OpenACCCacheConstruct &s);

  void emitOpenACCDeclare(const OpenACCDeclareDecl &d);
  void emitOpenACCRoutine(const OpenACCRoutineDecl &d);

  /// Create a temporary memory object for the given aggregate type.
  AggValueSlot createAggTemp(QualType ty, mlir::Location loc,
                             const Twine &name = "tmp",
                             Address *alloca = nullptr) {
    return AggValueSlot::forAddr(
        createMemTemp(ty, loc, name, alloca), ty.getQualifiers(),
        AggValueSlot::IsNotDestructed, AggValueSlot::IsNotAliased,
        AggValueSlot::DoesNotOverlap);
  }

  bool shouldSuppressAggExprMemSetUse() const {
    return suppressAggExprMemSetUse;
  }

private:
  QualType getVarArgType(const Expr *arg);

public:
  void emitSupportedNonTrivialCStructCopyConstructor(Address dst, Address src,
                                                     QualType ty);
  void emitSupportedNonTrivialCStructMoveConstructor(Address dst, Address src,
                                                     QualType ty);
  void emitSupportedNonTrivialCStructCopyAssignment(Address dst, Address src,
                                                    QualType ty);
  void emitSupportedNonTrivialCStructMoveAssignment(Address dst, Address src,
                                                    QualType ty);

  // ObjC ARC runtime helpers
  /// Emit a call to objc_retain and return the retained value.
  /// Short-circuits on null (returns null without calling).
  mlir::Value emitARCRetainNonBlock(mlir::Value value, mlir::Location loc);

  /// Emit a call to objc_autorelease and return the autoreleased value.
  /// Short-circuits on null.
  mlir::Value emitARCAutorelease(mlir::Value value, mlir::Location loc);

  /// Emit a call to objc_autoreleaseReturnValue and return the autoreleased
  /// value. Short-circuits on null.
  mlir::Value emitARCAutoreleaseReturnValue(mlir::Value value,
                                            mlir::Location loc);

  /// Emit a call to objc_retainAutorelease and return the fused result.
  /// Short-circuits on null.
  mlir::Value emitARCRetainAutorelease(mlir::Value value,
                                       mlir::Location loc);

  /// Emit a call to objc_retainBlock and return the retained block value.
  /// If \p mandatory is false, propagate ARC copy-on-escape metadata so LLVM
  /// can elide the heap copy when the block never escapes.
  mlir::Value emitARCRetainBlock(mlir::Value value, mlir::Location loc,
                                 bool mandatory);

  /// Emit ARC retain code for a scalar expression, matching OGCG's
  /// EmitARCRetainScalarExpr. This recognizes returned-object reclaim
  /// patterns so the retain can be attached to the producing call instead of
  /// blindly nesting additional retains.
  mlir::Value emitARCRetainScalarExpr(const clang::Expr *expr);

  /// Emit ARC retain+autorelease code for a scalar expression, matching
  /// OGCG's autoreleasing initialization and assignment behavior.
  mlir::Value emitARCRetainAutoreleaseScalarExpr(const clang::Expr *expr);

  /// Emit ARC reclaim for a returned object (+0 to +1 followed by a
  /// full-expression consume), matching OGCG's EmitARCReclaimReturnedObject.
  mlir::Value emitARCReclaimReturnedObject(const clang::Expr *expr);

  /// Emit ARC unsafe-claim for a returned object in __unsafe_unretained
  /// contexts, matching OGCG's EmitARCUnsafeClaimAutoreleasedReturnValue use.
  mlir::Value emitARCUnsafeClaimReturnedObject(const clang::Expr *expr);

  /// Emit ARC scalar evaluation for __unsafe_unretained contexts.
  mlir::Value emitARCUnsafeUnretainedScalarExpr(const clang::Expr *expr);

  /// Emit ARC block-lifetime extension for CK_ARCExtendBlockObject. If
  /// \p consumed is true, also schedule the matching full-expression consume.
  mlir::Value emitARCExtendBlockObject(const clang::Expr *expr,
                                       bool consumed = true);

  /// Emit the Objective-C `copy` + `autorelease` sequence used by
  /// CK_CopyAndAutoreleaseBlockObject.
  mlir::Value emitBlockCopyAndAutorelease(mlir::Value block, QualType ty,
                                          mlir::Location loc);

  /// Emit a call to objc_release. If \p isImprecise is true, the release
  /// can be moved/merged by the ARC optimizer and gets
  /// !clang.imprecise_release metadata.
  void emitARCRelease(mlir::Value value, mlir::Location loc,
                      bool isImprecise = false);

  /// Emit a call to objc_storeStrong(&addr, value).
  void emitARCStoreStrongCall(Address addr, mlir::Value value,
                              mlir::Location loc);

  /// Emit ARC weak runtime operations.
  mlir::Value emitARCLoadWeak(Address addr);
  mlir::Value emitARCStoreWeak(Address addr, mlir::Value value, bool ignored);
  void emitARCInitWeak(Address addr, mlir::Value value);
  void emitARCDestroyWeak(Address addr);
  void emitARCMoveWeak(Address dst, Address src);
  void emitARCCopyWeak(Address dst, Address src);
  void emitARCCopyAssignWeak(Address dst, Address src);
  void emitARCMoveAssignWeak(Address dst, Address src);
  mlir::Value emitARCLoadWeakRetained(Address addr);

  /// Push a cleanup to call objc_release at end of full-expression.
  void emitARCConsumeObject(mlir::Value value, mlir::Location loc);

  /// Emit ARC destroy for strong lifetime variables.
  /// At -O1+, emits load + objc_release (with imprecise metadata if
  /// \p isImprecise is true). At -O0, uses objc_storeStrong.
  void emitARCDestroyStrong(Address addr, mlir::Location loc,
                            bool isImprecise = false);

  /// Emit ARC autorelease-pool push/pop helpers, matching OGCG's use of
  /// ARC intrinsics on the normal path and objc_autoreleasePoolPop calls
  /// when unwinding is active.
  mlir::Value emitObjCAutoreleasePoolPush(mlir::Location loc);
  void emitObjCAutoreleasePoolPop(mlir::Value token, mlir::Location loc);
  void emitObjCAutoreleasePoolCleanup(mlir::Value token);

  /// Emit the operand for an Objective-C throw statement. Under ARC this
  /// performs the required retain/autorelease sequence before the runtime
  /// helper is invoked.
  mlir::Value emitObjCThrowOperand(const clang::Expr *expr);
};

//===----------------------------------------------------------------------===//
// Block capture helpers (shared between CIRGenExpr.cpp and CIRGenFunction.cpp)
//===----------------------------------------------------------------------===//

/// Whether a block capture should use a direct C++ copy-construction path.
inline bool
isSupportedCXXDirectBlockCapture(const BlockDecl::Capture &capture) {
  return capture.getCopyExpr() && !capture.isEscapingByref() &&
         isa<CXXConstructExpr>(capture.getCopyExpr()->IgnoreImplicit());
}

/// Return the field type to use for a direct block capture variable.
inline QualType
getDirectBlockCaptureFieldType(const BlockDecl::Capture &capture,
                               const FieldDecl *lambdaCaptureField) {
  if (isSupportedCXXDirectBlockCapture(capture))
    return capture.getCopyExpr()->getType();
  if (lambdaCaptureField && lambdaCaptureField->getType()->isReferenceType())
    return lambdaCaptureField->getType();
  return capture.getVariable()->getType();
}

//===----------------------------------------------------------------------===//
// Destructor body triviality checks (shared between CIRGenClass.cpp and
// CIRGenFunction.cpp). Port of OGCG's HasTrivialDestructorBody /
// FieldHasTrivialDestructorBody from CGClass.cpp.
//===----------------------------------------------------------------------===//

inline bool fieldHasTrivialDestructorBody(ASTContext &context,
                                          const FieldDecl *field);

inline bool hasTrivialDestructorBody(ASTContext &context,
                                     const CXXRecordDecl *baseClassDecl,
                                     const CXXRecordDecl *mostDerivedClassDecl) {
  if (baseClassDecl->hasTrivialDestructor())
    return true;

  if (!baseClassDecl->getDestructor()->hasTrivialBody())
    return false;

  for (const auto *field : baseClassDecl->fields())
    if (!fieldHasTrivialDestructorBody(context, field))
      return false;

  for (const auto &i : baseClassDecl->bases()) {
    if (i.isVirtual())
      continue;
    const auto *nonVirtualBase = i.getType()->castAsCXXRecordDecl();
    if (!hasTrivialDestructorBody(context, nonVirtualBase,
                                  mostDerivedClassDecl))
      return false;
  }

  if (baseClassDecl == mostDerivedClassDecl) {
    for (const auto &i : baseClassDecl->vbases()) {
      const auto *virtualBase = i.getType()->castAsCXXRecordDecl();
      if (!hasTrivialDestructorBody(context, virtualBase,
                                    mostDerivedClassDecl))
        return false;
    }
  }

  return true;
}

inline bool fieldHasTrivialDestructorBody(ASTContext &context,
                                          const FieldDecl *field) {
  QualType fieldBaseElementType =
      context.getBaseElementType(field->getType());

  auto *fieldClassDecl = fieldBaseElementType->getAsCXXRecordDecl();
  if (!fieldClassDecl)
    return true;

  if (fieldClassDecl->isUnion() && fieldClassDecl->isAnonymousStructOrUnion())
    return true;

  return hasTrivialDestructorBody(context, fieldClassDecl, fieldClassDecl);
}

//===----------------------------------------------------------------------===//
// Sanitizer attribute helpers (shared across CIRGenClass.cpp,
// CIRGenExpr.cpp, CIRGenExprScalar.cpp, CIRGenFunction.cpp).
//===----------------------------------------------------------------------===//

inline void markNoSanitize(mlir::Operation *op) {
  op->setAttr("cir.nosanitize", mlir::UnitAttr::get(op->getContext()));
}

inline void setSanitizerBranchAttrs(CIRGenBuilderTy &builder,
                                    cir::IfOp ifOp) {
  auto likelyWeight = builder.getI32IntegerAttr(1024 * 1024 - 1);
  auto unlikelyWeight = builder.getI32IntegerAttr(1);
  ifOp->setAttr("cir.branch_weights",
                builder.getArrayAttr({likelyWeight, unlikelyWeight}));
  ifOp->setAttr("cir.nosanitize",
                mlir::UnitAttr::get(builder.getContext()));
}

//===----------------------------------------------------------------------===//
// Symbol table lookup helpers (shared across CIRGenDecl.cpp,
// CIRGenExpr.cpp, CIRGenModule.cpp).
//===----------------------------------------------------------------------===//

inline cir::GlobalOp lookupGlobalByName(CIRGenModule &cgm, StringRef name) {
  return dyn_cast_or_null<cir::GlobalOp>(cgm.getGlobalValue(name));
}

inline cir::FuncOp lookupFuncByName(CIRGenModule &cgm, StringRef name) {
  return dyn_cast_or_null<cir::FuncOp>(cgm.getGlobalValue(name));
}

//===----------------------------------------------------------------------===//
// AllocaOp finder (shared between CIRGenDecl.cpp and CIRGenExpr.cpp).
//===----------------------------------------------------------------------===//

inline cir::AllocaOp findAllocaOp(mlir::Value val) {
  if (auto allocaOp = val.getDefiningOp<cir::AllocaOp>())
    return allocaOp;
  if (auto castOp = val.getDefiningOp<cir::CastOp>())
    if (castOp.getKind() == cir::CastKind::address_space)
      return castOp.getSrc().getDefiningOp<cir::AllocaOp>();
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Integer signedness cast (shared between CIRGenExpr.cpp and
// CIRGenExprScalar.cpp).
//===----------------------------------------------------------------------===//

inline mlir::Value castIntegerToSignedness(CIRGenBuilderTy &builder,
                                           mlir::Location loc,
                                           mlir::Value value, bool isSigned) {
  auto intTy = mlir::dyn_cast<cir::IntType>(value.getType());
  if (!intTy || intTy.isSigned() == isSigned)
    return value;

  auto castTy =
      cir::IntType::get(builder.getContext(), intTy.getWidth(), isSigned);
  return builder.createCast(loc, cir::CastKind::integral, value, castTy);
}

// Emit UBSan implicit-bitfield-conversion checks for paths shared between
// CIRGenExpr.cpp and CIRGenExprScalar.cpp.
void emitBitfieldConversionCheck(CIRGenFunction &cgf, CIRGenBuilderTy &builder,
                                 mlir::Location loc,
                                 SourceLocation sourceLoc, mlir::Value src,
                                 QualType srcType, mlir::Value dst,
                                 QualType dstType,
                                 const CIRGenBitFieldInfo &info);

//===----------------------------------------------------------------------===//
// CFI icall type generalization (shared between CIRGenExpr.cpp and
// CIRGenModule.cpp). Port of OGCG's GeneralizeType /
// GeneralizeFunctionType from CodeGenModule.cpp.
//===----------------------------------------------------------------------===//

inline QualType generalizeTypeForCfiIcall(ASTContext &ctx, QualType ty,
                                          bool generalizePointers) {
  if (const clang::RecordType *ut = ty->getAsUnionType()) {
    const RecordDecl *ud = ut->getDecl()->getDefinitionOrSelf();
    if (ud->hasAttr<TransparentUnionAttr>() && !ud->fields().empty())
      ty = ud->fields().begin()->getType();
  }

  if (!generalizePointers || !ty->isPointerType())
    return ty;

  return ctx.getPointerType(
      QualType(ctx.VoidTy)
          .withCVRQualifiers(ty->getPointeeType().getCVRQualifiers()));
}

inline QualType generalizeFunctionTypeForCfiIcall(ASTContext &ctx, QualType ty,
                                                  bool generalizePointers) {
  if (auto *fnType = ty->getAs<FunctionProtoType>()) {
    SmallVector<QualType, 8> generalizedParams;
    for (QualType param : fnType->param_types())
      generalizedParams.push_back(
          generalizeTypeForCfiIcall(ctx, param, generalizePointers));
    return ctx.getFunctionType(
        generalizeTypeForCfiIcall(ctx, fnType->getReturnType(),
                                  generalizePointers),
        generalizedParams, fnType->getExtProtoInfo());
  }

  if (auto *fnType = ty->getAs<FunctionNoProtoType>())
    return ctx.getFunctionNoProtoType(generalizeTypeForCfiIcall(
        ctx, fnType->getReturnType(), generalizePointers));

  llvm_unreachable("encountered unknown FunctionType in CFI icall metadata");
}

//===----------------------------------------------------------------------===//
// Thread-local variable helpers (shared between CIRGenExpr.cpp and
// CIRGenItaniumCXXABI.cpp). Port of OGCG's isEmittedWithConstantInitializer /
// usesThreadWrapperFunction from ItaniumCXXABI.cpp.
//===----------------------------------------------------------------------===//

inline bool mayNeedDestruction(const VarDecl *vd, ASTContext &ctx) {
  if (vd->needsDestruction(ctx))
    return true;
  const Type *t = vd->getType()->getBaseElementTypeUnsafe();
  return t->isRecordType() && t->isIncompleteType();
}

inline bool isEmittedWithConstantInitializer(const VarDecl *vd,
                                             ASTContext &ctx) {
  vd = vd->getMostRecentDecl();
  if (vd->hasAttr<ConstInitAttr>())
    return true;
  if (vd->isWeak() || vd->hasAttr<SelectAnyAttr>())
    return false;
  const VarDecl *initDecl = vd->getInitializingDeclaration();
  if (!initDecl)
    return false;
  if (!initDecl->hasInit())
    return true;
  if (isUniqueGVALinkage(ctx.GetGVALinkageForVariable(vd)))
    return !mayNeedDestruction(vd, ctx) && initDecl->evaluateValue();
  return initDecl->hasConstantInitialization();
}

inline bool usesThreadWrapperFunction(const VarDecl *vd, ASTContext &ctx) {
  return !isEmittedWithConstantInitializer(vd, ctx) ||
         mayNeedDestruction(vd, ctx);
}

//===----------------------------------------------------------------------===//
// Extern global helper (shared between CIRGenBuiltinX86.cpp and
// CIRGenBuiltinAArch64.cpp).
//===----------------------------------------------------------------------===//

inline cir::GlobalOp getOrCreateExternGlobal(CIRGenModule &cgm,
                                             mlir::Location loc,
                                             llvm::StringRef name,
                                             mlir::Type ty) {
  if (auto *existing = cgm.getGlobalValue(name))
    return mlir::cast<cir::GlobalOp>(existing);
  CIRGenBuilderTy &b = cgm.getBuilder();
  mlir::OpBuilder::InsertionGuard guard(b);
  if (cgm.lastGlobalOp)
    b.setInsertionPointAfter(cgm.lastGlobalOp);
  else
    b.setInsertionPointToStart(cgm.getModule().getBody());
  auto op = cir::GlobalOp::create(b, loc, name, ty,
                                  /*isConstant=*/false,
                                  cir::GlobalLinkageKind::ExternalLinkage);
  op.setDsoLocal(true);
  op.setSymVisibility("private");
  cgm.insertGlobalSymbol(op);
  cgm.lastGlobalOp = op;
  return op;
}

inline cir::RecordType getX86CpuModelType(mlir::MLIRContext &ctx) {
  auto int32Ty = cir::IntType::get(&ctx, 32, true);
  auto cpuFeatures1Ty = cir::ArrayType::get(int32Ty, 1);
  return cir::RecordType::get(&ctx, {int32Ty, int32Ty, int32Ty, cpuFeatures1Ty},
                              /*packed=*/false, /*padded=*/false,
                              cir::RecordType::Struct);
}

//===----------------------------------------------------------------------===//
// OpenMP integer/bool expression helpers (shared between CIRGenStmtOpenMP.cpp
// and CIRGenOpenMPClause.cpp).
//===----------------------------------------------------------------------===//

inline mlir::IntegerType getOpenMPIntegerType(CIRGenFunction &cgf,
                                              QualType ty) {
  assert(ty->isIntegerType() || ty->isEnumeralType());
  return mlir::IntegerType::get(&cgf.getMLIRContext(),
                                cgf.getContext().getIntWidth(ty));
}

inline mlir::Value emitOpenMPBoolExpr(CIRGenFunction &cgf, const Expr *e) {
  mlir::Value expr = cgf.emitScalarExpr(e);
  QualType exprTy = e->getType();
  if (!cgf.getContext().hasSameType(exprTy, cgf.getContext().BoolTy))
    expr = cgf.emitScalarConversion(expr, exprTy, cgf.getContext().BoolTy,
                                    e->getBeginLoc());

  mlir::Location exprLoc = cgf.getLoc(e->getBeginLoc());
  auto conversionOp = mlir::UnrealizedConversionCastOp::create(
      cgf.getBuilder(), exprLoc,
      mlir::IntegerType::get(&cgf.getMLIRContext(), 1), expr);
  return conversionOp.getResult(0);
}

//===----------------------------------------------------------------------===//
// ObjC msgSend helper (shared between CIRGenDecl.cpp and CIRGenStmt.cpp).
//===----------------------------------------------------------------------===//

inline RValue emitObjCMsgSend(CIRGenFunction &cgf, mlir::Location loc,
                              QualType returnTy, mlir::Value receiver,
                              Selector selector,
                              llvm::ArrayRef<CallArg> extraArgs = {}) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  QualType objCIdTy = cgf.getContext().getObjCIdType();
  QualType objCSelTy = cgf.getContext().getObjCSelType();

  llvm::SmallVector<QualType, 4> argTypes;
  argTypes.push_back(objCIdTy);
  argTypes.push_back(objCSelTy);
  for (const CallArg &arg : extraArgs)
    argTypes.push_back(arg.ty);

  QualType msgFnTy = cgf.getContext().getFunctionType(
      returnTy, argTypes, FunctionProtoType::ExtProtoInfo());
  auto msgProto = CanQual<FunctionProtoType>::CreateUnsafe(
      cgf.getContext().getCanonicalType(msgFnTy));
  const CIRGenFunctionInfo &funcInfo =
      cgf.getTypes().arrangeFreeFunctionType(msgProto);
  cir::FuncType exactFnTy = cgf.getTypes().getFunctionType(funcInfo);

  cir::FuncOp msgSendFn = cgf.cgm.getObjCMsgSendFunc();
  mlir::Value msgSendAddr = cir::GetGlobalOp::create(
      builder, loc, builder.getPointerTo(msgSendFn.getFunctionType()),
      msgSendFn.getSymNameAttr());
  mlir::Value msgSendPtr =
      builder.createBitcast(loc, msgSendAddr, builder.getPointerTo(exactFnTy));

  CallArgList args;
  mlir::Type idTy = cgf.convertType(objCIdTy);
  if (receiver.getType() != idTy)
    receiver = builder.createBitcast(loc, receiver, idTy);
  args.add(RValue::get(receiver), objCIdTy);
  args.add(RValue::get(cgf.cgm.getObjCSelector(cgf, selector, loc)),
           objCSelTy);
  for (const CallArg &arg : extraArgs) {
    assert(!arg.hasLValue() && "objc_msgSend helper expects scalar args");
    args.add(arg.getKnownRValue(), arg.ty);
  }

  return cgf.emitCall(funcInfo,
                      CIRGenCallee::forDirect(msgSendPtr.getDefiningOp()),
                      ReturnValueSlot(), args, /*callOp=*/nullptr, loc);
}

//===----------------------------------------------------------------------===//
// ObjC byref helper type predicates (shared between CIRGenDecl.cpp and
// CIRGenFunction.cpp).
//===----------------------------------------------------------------------===//

inline bool isSupportedObjCByrefHelperType(QualType ty) {
  Qualifiers::ObjCLifetime lifetime = ty.getObjCLifetime();
  return (lifetime == Qualifiers::OCL_Strong ||
          lifetime == Qualifiers::OCL_Weak) &&
         ty->isObjCRetainableType();
}

inline bool isSupportedAddressDiscriminatedByrefHelperType(QualType ty) {
  return ty.hasAddressDiscriminatedPointerAuth();
}

inline bool isSupportedNonARCObjectByrefHelperType(ASTContext &ctx,
                                                   QualType ty) {
  return ty->isBlockPointerType() || ctx.isObjCNSObjectType(ty) ||
         ty->isObjCObjectPointerType();
}

void setPreserveAccessIndexSourceTypeAttr(CIRGenFunction &cgf,
                                          mlir::Operation *op,
                                          QualType sourceType);

} // namespace clang::CIRGen

#endif
