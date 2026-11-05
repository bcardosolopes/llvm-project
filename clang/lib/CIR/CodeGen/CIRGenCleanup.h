//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These classes support the generation of CIR for cleanups, initially based
// on LLVM IR cleanup handling, but ought to change as CIR evolves.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_LIB_CIR_CODEGEN_CIRGENCLEANUP_H
#define CLANG_LIB_CIR_CODEGEN_CIRGENCLEANUP_H

#include "Address.h"
#include "CIRGenModule.h"
#include "EHScopeStack.h"
#include "mlir/IR/Value.h"
#include "clang/AST/StmtCXX.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace clang::CIRGen {

/// The MS C++ ABI needs a pointer to RTTI data plus some flags to describe the
/// type of a catch handler, so we use this wrapper.
struct CatchTypeInfo {
  mlir::TypedAttr rtti;
  unsigned flags;
};

/// A protected scope for zero-cost EH handling.
class EHScope {
  EHScopeStack::stable_iterator enclosingEHScope;

  class CommonBitFields {
    friend class EHScope;
    unsigned kind : 3;
  };
  enum { NumCommonBits = 3 };

  bool scopeMayThrow;

protected:
  class CatchBitFields {
    friend class EHCatchScope;
    unsigned : NumCommonBits;
    unsigned numHandlers : 32 - NumCommonBits;
  };

  class CleanupBitFields {
    friend class EHCleanupScope;
    unsigned : NumCommonBits;

    /// Whether this cleanup needs to be run along normal edges.
    unsigned isNormalCleanup : 1;

    /// Whether this cleanup needs to be run along exception edges.
    unsigned isEHCleanup : 1;

    /// Whether this cleanup is currently active.
    unsigned isActive : 1;

    /// Whether this cleanup is a lifetime marker
    unsigned isLifetimeMarker : 1;

    /// Whether the normal cleanup should test the activation flag.
    unsigned testFlagInNormalCleanup : 1;

    /// Whether the EH cleanup should test the activation flag.
    unsigned testFlagInEHCleanup : 1;

    /// The amount of extra storage needed by the Cleanup.
    /// Always a multiple of the scope-stack alignment.
    unsigned cleanupSize : 12;
  };

  union {
    CommonBitFields commonBits;
    CatchBitFields catchBits;
    CleanupBitFields cleanupBits;
  };

public:
  enum Kind { Cleanup, Catch, Terminate, Filter };

  EHScope(Kind kind, EHScopeStack::stable_iterator enclosingEHScope)
      : enclosingEHScope(enclosingEHScope), scopeMayThrow(false) {
    commonBits.kind = kind;
  }

  Kind getKind() const { return static_cast<Kind>(commonBits.kind); }

  bool mayThrow() const {
    // Traditional LLVM codegen also checks for `!block->use_empty()`, but
    // in CIRGen the block content is not important, just used as a way to
    // signal `hasEHBranches`.
    return scopeMayThrow;
  }

  void setMayThrow(bool mayThrow) { scopeMayThrow = mayThrow; }

  EHScopeStack::stable_iterator getEnclosingEHScope() const {
    return enclosingEHScope;
  }
};

/// A scope which attempts to handle some, possibly all, types of
/// exceptions.
///
/// Objective C \@finally blocks are represented using a cleanup scope
/// after the catch scope.

class EHCatchScope : public EHScope {
  // In effect, we have a flexible array member
  //   Handler Handlers[0];
  // But that's only standard in C99, not C++, so we have to do
  // annoying pointer arithmetic instead.

public:
  struct Handler {
    /// A type info value, or null MLIR attribute for a catch-all
    CatchTypeInfo type;

    /// The catch handler for this type.
    mlir::Region *region;

    /// The catch handler stmt.
    const CXXCatchStmt *stmt;

    bool isCatchAll() const { return type.rtti == nullptr; }
  };

private:
  friend class EHScopeStack;

  Handler *getHandlers() { return reinterpret_cast<Handler *>(this + 1); }

  const Handler *getHandlers() const {
    return reinterpret_cast<const Handler *>(this + 1);
  }

public:
  static size_t getSizeForNumHandlers(unsigned n) {
    return sizeof(EHCatchScope) + n * sizeof(Handler);
  }

  EHCatchScope(unsigned numHandlers,
               EHScopeStack::stable_iterator enclosingEHScope)
      : EHScope(Catch, enclosingEHScope) {
    catchBits.numHandlers = numHandlers;
    assert(catchBits.numHandlers == numHandlers && "NumHandlers overflow?");
  }

  unsigned getNumHandlers() const { return catchBits.numHandlers; }

  void setHandler(unsigned i, CatchTypeInfo type, mlir::Region *region,
                  const CXXCatchStmt *stmt) {
    assert(i < getNumHandlers());
    Handler *handler = &getHandlers()[i];
    handler->type = type;
    handler->region = region;
    handler->stmt = stmt;
  }

  const Handler &getHandler(unsigned i) const {
    assert(i < getNumHandlers());
    return getHandlers()[i];
  }

  // Clear all handler blocks.
  // FIXME: it's better to always call clearHandlerBlocks in DTOR and have a
  // 'takeHandler' or some such function which removes ownership from the
  // EHCatchScope object if the handlers should live longer than EHCatchScope.
  void clearHandlerBlocks() {
    // The blocks are owned by TryOp, nothing to delete.
  }

  using iterator = const Handler *;
  iterator begin() const { return getHandlers(); }
  iterator end() const { return getHandlers() + getNumHandlers(); }

  static bool classof(const EHScope *scope) {
    return scope->getKind() == Catch;
  }
};

/// A cleanup scope which generates the cleanup blocks lazily.
class alignas(EHScopeStack::ScopeStackAlignment) EHCleanupScope
    : public EHScope {
  /// The nearest normal cleanup scope enclosing this one.
  EHScopeStack::stable_iterator enclosingNormal;

  /// The dual entry/exit block along the normal edge.  This is lazily
  /// created if needed before the cleanup is popped.
  mlir::Block *normalBlock = nullptr;

  /// Extra information required for cleanups that have resolved
  /// branches through them.  This has to be allocated on the side
  /// because everything on the cleanup stack has be trivially
  /// movable.
  struct ExtInfo {
    /// The destinations of normal branch-afters and branch-throughs.
    llvm::SmallPtrSet<mlir::Block *, 4> branches;

    /// Normal branch-afters.
    llvm::SmallVector<std::pair<mlir::Block *, mlir::Value>, 4> branchAfters;
  };
  mutable struct ExtInfo *extInfo = nullptr;

  /// An address of an i1 (bool) alloca that tracks whether this cleanup
  /// should actually run. Used for conditional cleanups in ternary
  /// expressions where a temporary may or may not have been constructed.
  Address activeFlag = Address::invalid();

  /// The number of fixups required by enclosing scopes (not including
  /// this one).  If this is the top cleanup scope, all the fixups
  /// from this index onwards belong to this scope.
  unsigned fixupDepth = 0;

  /// Where the cleanup's guarded range begins, when pushCleanup decided this
  /// cleanup should get a shared-landing-pad cir.cleanup.scope. The range runs
  /// from just after `rangeStartAfter` (or the start of `rangeStartBlock` when
  /// null) to the insertion point at pop time; popCleanupBlock moves it into a
  /// cir.cleanup.scope body, so FlattenCFG can give every throwing call in it
  /// ONE landing pad -- OGCG's one-pad-per-cleanup-depth shape -- instead of
  /// one synthetic cir.try, and so one pad, per call.
  ///
  /// The scope is formed at pop time rather than opened at push time so that
  /// the insertion point is never parked inside a region while the body is
  /// emitted; that keeps the surrounding ordering (notably where cir.return
  /// lands) identical to the per-call path.
  mlir::Block *rangeStartBlock = nullptr;
  mlir::Operation *rangeStartAfter = nullptr;
  bool wantsCleanupScope = false;

  /// Function-unique id, so a synthetic cir.try can name this cleanup even
  /// after the scope has been popped and its storage reused. 0 when unset.
  unsigned cleanupId = 0;

  /// Value of CIRGenFunction::cleanupScopeSeqCounter when this cleanup was
  /// pushed. cir.cleanup.scopes formed after that point (higher sequence
  /// number) overlap this cleanup's guarded range and need its destructor on
  /// their shared unwind edge; earlier ones sit entirely before it.
  unsigned scopeSeqAtPush = 0;

  /// Which chunk of a chunked initializer_list array emission this cleanup
  /// was pushed during, or 0 when pushed outside one. A long single-object
  /// array is emitted as nested cir.cleanup.scopes, one per chunk, so no one
  /// pad guards more than a chunk's worth of conditional cleanups; each
  /// nested dispatch emits only its own chunk (see
  /// emitSharedCleanupScopeDispatch). Earlier chunks' cleanups reach their pad
  /// through FlattenCFG's outward pad chaining, never through a later pad.
  unsigned sharedScopeChunkId = 0;

  /// Set when the cleanup is deactivated exactly at the end of the range it
  /// guards (a throw expression retires its FreeException cleanup right where
  /// it pops it). It is inactive from here on, so the normal path must skip it,
  /// but a cir.cleanup.scope formed from that range still has to run it: every
  /// throwing call the scope covers happened while it was live.
  bool activeOverCleanupScopeRange = false;

  struct ExtInfo &getExtInfo() {
    if (!extInfo)
      extInfo = new struct ExtInfo();
    return *extInfo;
  }

  const struct ExtInfo &getExtInfo() const {
    if (!extInfo)
      extInfo = new struct ExtInfo();
    return *extInfo;
  }

public:
  /// Gets the size required for a lazy cleanup scope with the given
  /// cleanup-data requirements.
  static size_t getSizeForCleanupSize(size_t size) {
    return sizeof(EHCleanupScope) + size;
  }

  size_t getAllocatedSize() const {
    return sizeof(EHCleanupScope) + cleanupBits.cleanupSize;
  }

  EHCleanupScope(bool isNormal, bool isEH, unsigned cleanupSize,
                 unsigned fixupDepth,
                 EHScopeStack::stable_iterator enclosingNormal,
                 EHScopeStack::stable_iterator enclosingEH)
      : EHScope(EHScope::Cleanup, enclosingEH),
        enclosingNormal(enclosingNormal), fixupDepth(fixupDepth) {
    cleanupBits.isNormalCleanup = isNormal;
    cleanupBits.isEHCleanup = isEH;
    cleanupBits.isActive = true;
    cleanupBits.isLifetimeMarker = false;
    cleanupBits.testFlagInNormalCleanup = false;
    cleanupBits.testFlagInEHCleanup = false;
    cleanupBits.cleanupSize = cleanupSize;

    assert(cleanupBits.cleanupSize == cleanupSize && "cleanup size overflow");
  }

  void destroy() { delete extInfo; }
  // Objects of EHCleanupScope are not destructed. Use destroy().
  ~EHCleanupScope() = delete;

  mlir::Block *getNormalBlock() const { return normalBlock; }
  void setNormalBlock(mlir::Block *bb) { normalBlock = bb; }

  bool isNormalCleanup() const { return cleanupBits.isNormalCleanup; }
  bool isEHCleanup() const { return cleanupBits.isEHCleanup; }

  bool isActive() const { return cleanupBits.isActive; }
  void setActive(bool isActive) { cleanupBits.isActive = isActive; }

  bool isLifetimeMarker() const { return cleanupBits.isLifetimeMarker; }
  void setLifetimeMarker() { cleanupBits.isLifetimeMarker = true; }

  unsigned getCleanupId() const { return cleanupId; }
  void setCleanupId(unsigned id) { cleanupId = id; }

  unsigned getScopeSeqAtPush() const { return scopeSeqAtPush; }
  void setScopeSeqAtPush(unsigned seq) { scopeSeqAtPush = seq; }

  unsigned getSharedScopeChunkId() const { return sharedScopeChunkId; }
  void setSharedScopeChunkId(unsigned id) { sharedScopeChunkId = id; }

  bool isActiveOverCleanupScopeRange() const {
    return activeOverCleanupScopeRange;
  }
  void setActiveOverCleanupScopeRange() { activeOverCleanupScopeRange = true; }

  bool wantsCleanupScopeOp() const { return wantsCleanupScope; }
  mlir::Block *getRangeStartBlock() const { return rangeStartBlock; }
  mlir::Operation *getRangeStartAfter() const { return rangeStartAfter; }
  void setCleanupScopeRange(mlir::Block *block, mlir::Operation *after) {
    wantsCleanupScope = true;
    rangeStartBlock = block;
    rangeStartAfter = after;
  }

  bool hasActiveFlag() const { return activeFlag.isValid(); }
  Address getActiveFlag() const { return activeFlag; }
  void setActiveFlag(Address var) { activeFlag = var; }

  void setTestFlagInNormalCleanup() {
    cleanupBits.testFlagInNormalCleanup = true;
  }
  bool shouldTestFlagInNormalCleanup() const {
    return cleanupBits.testFlagInNormalCleanup;
  }
  void setTestFlagInEHCleanup() { cleanupBits.testFlagInEHCleanup = true; }
  bool shouldTestFlagInEHCleanup() const {
    return cleanupBits.testFlagInEHCleanup;
  }

  /// True if this cleanup scope has any branch-afters or branch-throughs.
  bool hasBranches() const { return extInfo && !extInfo->branches.empty(); }

  /// Add a branch-after to this cleanup scope.  A branch-after is a
  /// branch from a point protected by this (normal) cleanup to a
  /// point in the normal cleanup scope immediately containing it.
  void addBranchAfter(mlir::Value index, mlir::Block *block) {
    struct ExtInfo &info = getExtInfo();
    if (info.branches.insert(block).second)
      info.branchAfters.push_back(std::make_pair(block, index));
  }

  /// Return the number of unique branch-afters on this scope.
  unsigned getNumBranchAfters() const {
    return extInfo ? extInfo->branchAfters.size() : 0;
  }

  /// Return the destination block of the i'th branch-after.
  mlir::Block *getBranchAfterBlock(unsigned i) const {
    assert(i < getNumBranchAfters());
    return extInfo->branchAfters[i].first;
  }

  /// Return the destination index of the i'th branch-after.
  mlir::Value getBranchAfterIndex(unsigned i) const {
    assert(i < getNumBranchAfters());
    return extInfo->branchAfters[i].second;
  }

  /// Add a branch-through to this cleanup scope.  A branch-through is
  /// a branch from a scope protected by this (normal) cleanup to an
  /// enclosing scope other than the immediately-enclosing normal
  /// cleanup scope.
  ///
  /// \return true if the branch-through was new to this scope
  bool addBranchThrough(mlir::Block *block) {
    return getExtInfo().branches.insert(block).second;
  }

  /// Determines if this cleanup scope has any branch throughs.
  bool hasBranchThroughs() const {
    if (!extInfo)
      return false;
    return (extInfo->branchAfters.size() != extInfo->branches.size());
  }

  unsigned getFixupDepth() const { return fixupDepth; }
  EHScopeStack::stable_iterator getEnclosingNormalCleanup() const {
    return enclosingNormal;
  }

  size_t getCleanupSize() const { return cleanupBits.cleanupSize; }
  void *getCleanupBuffer() { return this + 1; }

  EHScopeStack::Cleanup *getCleanup() {
    return reinterpret_cast<EHScopeStack::Cleanup *>(getCleanupBuffer());
  }

  static bool classof(const EHScope *scope) {
    return (scope->getKind() == Cleanup);
  }

  void markEmitted() {}
};

/// An exceptions scope which calls std::terminate if any exception
/// reaches it.
class EHTerminateScope : public EHScope {
public:
  EHTerminateScope(EHScopeStack::stable_iterator enclosingEHScope)
      : EHScope(Terminate, enclosingEHScope) {}
  static size_t getSize() { return sizeof(EHTerminateScope); }

  static bool classof(const EHScope *scope) {
    return scope->getKind() == Terminate;
  }
};

/// An exceptions scope for dynamic exception specifications (throw(T...)).
/// Catches exceptions whose types are NOT in the filter list and calls
/// __cxa_call_unexpected. Used for C++03/C++11/C++14 throw(Type) specs.
/// Essentially ends in a flexible array member:
///   mlir::Attribute FilterTypes[0];
class EHFilterScope : public EHScope {
  unsigned numFilters;

  mlir::Attribute *getFilters() {
    return reinterpret_cast<mlir::Attribute *>(this + 1);
  }
  const mlir::Attribute *getFilters() const {
    return reinterpret_cast<const mlir::Attribute *>(this + 1);
  }

public:
  EHFilterScope(unsigned numFilters,
                EHScopeStack::stable_iterator enclosingEHScope)
      : EHScope(Filter, enclosingEHScope), numFilters(numFilters) {}

  static size_t getSizeForNumFilters(unsigned n) {
    return sizeof(EHFilterScope) + n * sizeof(mlir::Attribute);
  }

  unsigned getNumFilters() const { return numFilters; }

  void setFilter(unsigned i, mlir::Attribute typeInfo) {
    assert(i < numFilters);
    getFilters()[i] = typeInfo;
  }

  mlir::Attribute getFilter(unsigned i) const {
    assert(i < numFilters);
    return getFilters()[i];
  }

  static bool classof(const EHScope *scope) {
    return scope->getKind() == Filter;
  }
};

/// A non-stable pointer into the scope stack.
class EHScopeStack::iterator {
  char *ptr = nullptr;

  friend class EHScopeStack;
  explicit iterator(char *ptr) : ptr(ptr) {}

public:
  iterator() = default;

  EHScope *get() const { return reinterpret_cast<EHScope *>(ptr); }

  EHScope *operator->() const { return get(); }
  EHScope &operator*() const { return *get(); }

  iterator &operator++() {
    size_t size;
    switch (get()->getKind()) {
    case EHScope::Catch:
      size = EHCatchScope::getSizeForNumHandlers(
          static_cast<const EHCatchScope *>(get())->getNumHandlers());
      break;

    case EHScope::Filter:
      size = EHFilterScope::getSizeForNumFilters(
          static_cast<const EHFilterScope *>(get())->getNumFilters());
      break;

    case EHScope::Cleanup:
      size = static_cast<const EHCleanupScope *>(get())->getAllocatedSize();
      break;

    case EHScope::Terminate:
      size = EHTerminateScope::getSize();
      break;
    }
    ptr += llvm::alignTo(size, ScopeStackAlignment);
    return *this;
  }

  bool operator==(iterator other) const { return ptr == other.ptr; }
  bool operator!=(iterator other) const { return ptr != other.ptr; }
};

inline EHScopeStack::iterator EHScopeStack::begin() const {
  return iterator(startOfData);
}

inline EHScopeStack::iterator EHScopeStack::end() const {
  return iterator(endOfBuffer);
}

inline EHScopeStack::iterator
EHScopeStack::find(stable_iterator savePoint) const {
  assert(savePoint.isValid() && "finding invalid savepoint");
  assert(savePoint.size <= stable_begin().size &&
         "finding savepoint after pop");
  return iterator(endOfBuffer - savePoint.size);
}

inline void EHScopeStack::popCatch() {
  assert(!empty() && "popping exception stack when not empty");

  EHCatchScope &scope = llvm::cast<EHCatchScope>(*begin());
  innermostEHScope = scope.getEnclosingEHScope();
  deallocate(EHCatchScope::getSizeForNumHandlers(scope.getNumHandlers()));
}

inline void EHScopeStack::pushTerminate() {
  char *buffer = allocate(EHTerminateScope::getSize());
  new (buffer) EHTerminateScope(innermostEHScope);
  innermostEHScope = stable_begin();
}

inline void EHScopeStack::popTerminate() {
  assert(!empty() && "popping exception stack when not empty");

  EHTerminateScope &scope = llvm::cast<EHTerminateScope>(*begin());
  innermostEHScope = scope.getEnclosingEHScope();
  deallocate(EHTerminateScope::getSize());
}

inline EHFilterScope *EHScopeStack::pushFilter(unsigned numFilters) {
  char *buffer = allocate(EHFilterScope::getSizeForNumFilters(numFilters));
  auto *filter = new (buffer) EHFilterScope(numFilters, innermostEHScope);
  innermostEHScope = stable_begin();
  return filter;
}

inline void EHScopeStack::popFilter() {
  assert(!empty() && "popping exception stack when not empty");

  EHFilterScope &scope = llvm::cast<EHFilterScope>(*begin());
  innermostEHScope = scope.getEnclosingEHScope();
  deallocate(EHFilterScope::getSizeForNumFilters(scope.getNumFilters()));
}

/// The exceptions personality for a function.
struct EHPersonality {
  const char *personalityFn = nullptr;

  // If this is non-null, this personality requires a non-standard
  // function for rethrowing an exception after a catchall cleanup.
  // This function must have prototype void(void*).
  const char *catchallRethrowFn = nullptr;

  static const EHPersonality &get(CIRGenModule &cgm,
                                  const clang::FunctionDecl *fd);
  static const EHPersonality &get(CIRGenFunction &cgf);

  static const EHPersonality GNU_C;
  static const EHPersonality GNU_C_SJLJ;
  static const EHPersonality GNU_C_SEH;
  static const EHPersonality GNU_ObjC;
  static const EHPersonality GNU_ObjC_SJLJ;
  static const EHPersonality GNU_ObjC_SEH;
  static const EHPersonality GNUstep_ObjC;
  static const EHPersonality GNU_ObjCXX;
  static const EHPersonality NeXT_ObjC;
  static const EHPersonality GNU_CPlusPlus;
  static const EHPersonality GNU_CPlusPlus_SJLJ;
  static const EHPersonality GNU_CPlusPlus_SEH;
  static const EHPersonality MSVC_except_handler;
  static const EHPersonality MSVC_C_specific_handler;
  static const EHPersonality MSVC_CxxFrameHandler3;
  static const EHPersonality GNU_Wasm_CPlusPlus;
  static const EHPersonality XL_CPlusPlus;
  static const EHPersonality ZOS_CPlusPlus;

  /// Does this personality use landingpads or the family of pad instructions
  /// designed to form funclets?
  bool usesFuncletPads() const {
    return isMSVCPersonality() || isWasmPersonality();
  }

  bool isMSVCPersonality() const {
    return this == &MSVC_except_handler || this == &MSVC_C_specific_handler ||
           this == &MSVC_CxxFrameHandler3;
  }

  bool isWasmPersonality() const { return this == &GNU_Wasm_CPlusPlus; }

  bool isMSVCXXPersonality() const { return this == &MSVC_CxxFrameHandler3; }
};

} // namespace clang::CIRGen
#endif // CLANG_LIB_CIR_CODEGEN_CIRGENCLEANUP_H
