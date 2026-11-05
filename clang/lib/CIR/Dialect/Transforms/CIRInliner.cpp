//===- CIRInliner.cpp - Inline small and single-use functions on flat CIR -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MLIR's inliner over a flat, ABI-lowered CIR module, after mem2reg.
//
// The DialectInlinerInterface here is deliberately conservative: it refuses
// invoke-style call sites (cir.try_call) and callees that carry EH, varargs,
// dynamic allocas or by-value arguments, which leaves the getters,
// operator[] and forwarding wrappers this pass is after. It lives here rather
// than in the dialect library because the interface is part of MLIR's
// transform utilities; the pass attaches it to the dialect through its
// dependent-dialect registry, as the LLVM dialect does with its own.
//
//===----------------------------------------------------------------------===//

#include "PassDetail.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"

#include "mlir/Analysis/CallGraph.h"
#include "mlir/Dialect/Func/Extensions/InlinerExtension.h"
#include "mlir/Dialect/LLVMIR/Transforms/InlinerInterfaceImpl.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Threading.h"
#include "mlir/Transforms/Inliner.h"
#include "mlir/Transforms/InliningUtils.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Support/DebugLog.h"
#include "llvm/Support/TimeProfiler.h"

#define DEBUG_TYPE "cir-inline"

using namespace mlir;
using namespace cir;

namespace mlir {
#define GEN_PASS_DEF_CIRINLINER
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

namespace {

bool hasExtraAttr(mlir::Operation *op, llvm::StringRef name) {
  auto extra = op->getAttrOfType<cir::ExtraFuncAttributesAttr>("extra_attrs");
  return extra && extra.getElements().contains(name);
}

/// Whether `callee` may run inside `caller` as far as the target is concerned:
/// every feature it enables is enabled in the caller too, LLVM's rule (the
/// AArch64 and X86 inliners compare feature sets the same way). Inlining
/// otherwise moves instructions into a function that cannot select them:
/// exoplayer2's dav1d tone mapper has `target("arch=armv8.2-a+fp16")`
/// helpers called from a plain NEON function, and the fp16 reciprocal steps
/// inlined there stopped instruction selection. A feature named as enabled
/// in one spelling and implied by another in the caller is not recognised,
/// which only refuses an inline. A target-cpu that differs refuses too.
bool targetCompatible(cir::FuncOp caller, cir::FuncOp callee) {
  auto attr = [](cir::FuncOp func, llvm::StringRef name) -> mlir::StringAttr {
    auto extra = func.getExtraAttrs();
    if (!extra)
      return {};
    return extra->getElements().getAs<mlir::StringAttr>(name);
  };
  if (mlir::StringAttr calleeCPU = attr(callee, "target-cpu"))
    if (calleeCPU != attr(caller, "target-cpu"))
      return false;
  mlir::StringAttr calleeFeatures = attr(callee, "target-features");
  mlir::StringAttr callerFeatures = attr(caller, "target-features");
  // Interned, so a module compiled with one set of flags takes this exit.
  if (!calleeFeatures || calleeFeatures == callerFeatures)
    return true;
  llvm::SmallVector<llvm::StringRef> enabled;
  if (callerFeatures)
    callerFeatures.getValue().split(enabled, ',', /*MaxSplit=*/-1,
                                    /*KeepEmpty=*/false);
  llvm::SmallVector<llvm::StringRef> needed;
  calleeFeatures.getValue().split(needed, ',', /*MaxSplit=*/-1,
                                  /*KeepEmpty=*/false);
  return llvm::all_of(needed, [&](llvm::StringRef feature) {
    return !feature.starts_with("+") || llvm::is_contained(enabled, feature);
  });
}

/// Ops a callee may not contain: EH and landing pads (v1 does not rewire
/// unwind edges), varargs, indirect branches and block addresses, coroutine
/// suspension, setjmp/longjmp, and anything that resizes the stack.
bool isUnsupportedInCallee(mlir::Operation *op) {
  if (mlir::isa<cir::TryCallOp, cir::ResumeOp, cir::ResumeFlatOp,
                cir::EhInflightOp, cir::EhTypeIdOp, cir::CatchParamOp,
                cir::EhSetjmpOp, cir::EhLongjmpOp, cir::VAStartOp,
                cir::IndirectBrOp, cir::BlockAddressOp, cir::GotoOp,
                cir::LabelOp, cir::AwaitOp, cir::StackSaveOp,
                cir::StackRestoreOp>(op))
    return true;
  if (auto alloca = mlir::dyn_cast<cir::AllocaOp>(op))
    return static_cast<bool>(alloca.getDynAllocSize());
  if (auto call = mlir::dyn_cast<cir::CallOp>(op)) {
    // A call into something that returns twice must stay a real call.
    if (hasExtraAttr(op, "returns_twice"))
      return true;
    // A musttail call has to be followed by the callee's return, which
    // inlining turns into a branch.
    if (call.getMustTail())
      return true;
  }
  return false;
}

/// Record which function the inlined code came from. MLIR's inliner has
/// already rewritten every inlined location into a CallSiteLoc chain ending at
/// the call; the frame whose caller is the call is the callee's, and it gets
/// the callee's name and location as a #cir.inlined_function. Lowering turns
/// that into the callee's DISubprogram, which makes the debug translation emit
/// a DILocation in the callee, inlined at the call -- the inline frame crash
/// symbolization and AutoFDO read.
void attachInlineProvenance(mlir::Operation *call, cir::FuncOp callee,
                            iterator_range<Region::iterator> inlinedBlocks) {
  mlir::MLIRContext *ctx = call->getContext();
  auto inlinedFunction = cir::InlinedFunctionAttr::get(
      ctx, mlir::FlatSymbolRefAttr::get(callee.getSymNameAttr()),
      callee.getLoc());
  mlir::Location callLoc = call->getLoc();
  // Most of the inlined ops share a handful of locations, and building one
  // takes the context's uniquer lock, so each is rewritten once.
  llvm::DenseMap<mlir::Location, mlir::Location> tagged;
  std::function<mlir::Location(mlir::Location)> tag =
      [&](mlir::Location loc) -> mlir::Location {
    auto callSite = mlir::dyn_cast<mlir::CallSiteLoc>(loc);
    if (!callSite)
      return loc;
    if (auto it = tagged.find(loc); it != tagged.end())
      return it->second;
    mlir::Location result =
        callSite.getCaller() == callLoc
            ? mlir::CallSiteLoc::get(mlir::FusedLoc::get(ctx,
                                                         {callSite.getCallee()},
                                                         inlinedFunction),
                                     callLoc)
            : mlir::CallSiteLoc::get(callSite.getCallee(),
                                     tag(callSite.getCaller()));
    tagged.try_emplace(loc, result);
    return result;
  };
  for (mlir::Block &block : inlinedBlocks) {
    for (mlir::BlockArgument arg : block.getArguments())
      arg.setLoc(tag(arg.getLoc()));
    block.walk([&](mlir::Operation *op) { op->setLoc(tag(op->getLoc())); });
  }
}

struct CIRInlinerInterface : public DialectInlinerInterface {
  using DialectInlinerInterface::DialectInlinerInterface;

  bool isLegalToInline(Operation *call, Operation *callable,
                       bool wouldBeCloned) const final {
    auto callOp = mlir::dyn_cast<cir::CallOp>(call);
    auto callee = mlir::dyn_cast<cir::FuncOp>(callable);
    if (!callOp || !callee || callee.isDeclaration())
      return false;
    // A musttail call site keeps its guaranteed tail call.
    if (hasExtraAttr(callOp, "noinline") || callOp.getMustTail())
      return false;
    if (callee.getInlineKind() == cir::InlineKind::NoInline ||
        callee.getOptNone() || callee.getCoroutine() || callee.getAliasee())
      return false;
    if (hasExtraAttr(callee, "returns_twice"))
      return false;
    if (callee.getCallingConv() != callOp.getCallingConv())
      return false;
    // Nothing is inlined into a function that asked not to be optimized.
    auto caller = call->getParentOfType<cir::FuncOp>();
    if (!caller || caller.getOptNone() || !targetCompatible(caller, callee))
      return false;
    // By-value arguments need the copy the callee would have made; v1 leaves
    // them alone rather than materialize it.
    if (mlir::ArrayAttr argAttrs = callee.getArgAttrsAttr())
      for (auto dict : argAttrs.getAsRange<mlir::DictionaryAttr>())
        if (dict && (dict.contains("cir.byval") || dict.contains("cir.inalloca")))
          return false;
    for (mlir::Block &block : callee.getBody())
      if (block.empty() || !block.back().hasTrait<OpTrait::IsTerminator>())
        return false;
    return true;
  }

  bool isLegalToInline(Region *, Region *, bool, IRMapping &) const final {
    return true;
  }

  bool isLegalToInline(Operation *op, Region *, bool, IRMapping &) const final {
    return !isUnsupportedInCallee(op);
  }

  void handleTerminator(Operation *op, Block *newDest) const final {
    auto returnOp = mlir::dyn_cast<cir::ReturnOp>(op);
    if (!returnOp)
      return;
    OpBuilder builder(op);
    cir::BrOp::create(builder, op->getLoc(), newDest, returnOp.getOperands());
    op->erase();
  }

  bool allowSingleBlockOptimization(
      iterator_range<Region::iterator> inlinedBlocks) const final {
    return inlinedBlocks.empty() ||
           mlir::isa<cir::ReturnOp>(inlinedBlocks.begin()->getTerminator());
  }

  void handleTerminator(Operation *op, ValueRange valuesToRepl) const final {
    auto returnOp = mlir::cast<cir::ReturnOp>(op);
    assert(returnOp.getNumOperands() == valuesToRepl.size());
    for (auto [dst, src] : llvm::zip(valuesToRepl, returnOp.getOperands()))
      dst.replaceAllUsesWith(src);
  }

  /// The inlined code records the callee it came from, and the callee's
  /// constant-size allocas move to the caller's entry block, the only place
  /// lowering expects a static alloca; their lifetime markers stay where the
  /// callee had them.
  void processInlinedCallBlocks(
      Operation *call, Operation *callable,
      iterator_range<Region::iterator> inlinedBlocks) const final {
    if (inlinedBlocks.empty())
      return;
    auto caller = call->getParentOfType<cir::FuncOp>();
    if (!caller)
      return;
    if (auto callee = mlir::dyn_cast<cir::FuncOp>(callable))
      attachInlineProvenance(call, callee, inlinedBlocks);
    mlir::Block *callerEntry = &caller.getBody().front();
    mlir::Block *calleeEntry = &*inlinedBlocks.begin();
    if (callerEntry == calleeEntry)
      return;
    llvm::SmallVector<cir::AllocaOp> allocas(
        calleeEntry->getOps<cir::AllocaOp>());
    for (cir::AllocaOp alloca : allocas)
      alloca->moveBefore(callerEntry, callerEntry->begin());
  }
};

/// Whether nothing outside this module can reference `func`, so it may be
/// dropped once it has no use here: internal and private linkage, and a
/// hidden linkonce_odr (a default-visibility one may still be imported by
/// another library at link time).
bool isDroppableWhenUnused(cir::FuncOp func) {
  if (func.getGlobalCtorPriority() || func.getGlobalDtorPriority())
    return false;
  switch (func.getLinkage()) {
  case cir::GlobalLinkageKind::InternalLinkage:
  case cir::GlobalLinkageKind::PrivateLinkage:
  case cir::GlobalLinkageKind::AvailableExternallyLinkage:
    return true;
  case cir::GlobalLinkageKind::LinkOnceODRLinkage:
    return func.getGlobalVisibilityAttr().getValue() ==
           cir::VisibilityKind::Hidden;
  default:
    return false;
  }
}

/// A weighted size of `func`'s body, stopping as soon as it passes `limit`.
/// Calls cost more than the ops around them; constants, allocas, lifetime
/// markers, no-op casts and the return cost nothing.
unsigned weightedSize(cir::FuncOp func, unsigned limit) {
  unsigned size = 0;
  for (mlir::Block &block : func.getBody()) {
    for (mlir::Operation &op : block) {
      if (mlir::isa<cir::ConstantOp, cir::AllocaOp, cir::LifetimeStartOp,
                    cir::LifetimeEndOp, cir::ReturnOp>(op))
        continue;
      if (auto cast = mlir::dyn_cast<cir::CastOp>(op);
          cast && cast.getKind() == cir::CastKind::bitcast)
        continue;
      size += mlir::isa<cir::CallOp>(op) ? 3 : 1;
      if (size > limit)
        return size;
    }
  }
  return size;
}

/// Finds the symbol references in ops' own attributes -- calls, address-of,
/// initializers -- for the use counts.
class SymbolRefFinder {
public:
  /// Call `fn` with the root of every symbol reference in `op`'s attributes.
  /// CIR ops keep their inherent attributes in properties, and
  /// Operation::getAttrDictionary would build (and unique) a dictionary for
  /// each op to show them; reading them one by one does not.
  void forEach(mlir::Operation *op,
               llvm::function_ref<void(mlir::StringAttr)> fn) {
    std::optional<mlir::RegisteredOperationName> info = op->getRegisteredInfo();
    if (!info || !op->getPropertiesStorageSize()) {
      visit(op->getAttrDictionary(), fn);
      return;
    }
    for (mlir::StringAttr name : info->getAttributeNames())
      if (std::optional<mlir::Attribute> attr = op->getInherentAttr(name))
        if (*attr)
          visit(*attr, fn);
    for (mlir::NamedAttribute attr : op->getDiscardableAttrs())
      visit(attr.getValue(), fn);
  }

private:
  /// The references inside each composite attribute, found once: attributes
  /// are uniqued, and the large ones -- a global's initializer, a call's
  /// extra attributes -- recur.
  llvm::DenseMap<mlir::Attribute, llvm::SmallVector<mlir::StringAttr, 1>>
      refsByAttr;

  void visit(mlir::Attribute attr,
             llvm::function_ref<void(mlir::StringAttr)> fn) {
    if (auto ref = mlir::dyn_cast<mlir::FlatSymbolRefAttr>(attr)) {
      fn(ref.getAttr());
      return;
    }
    if (mlir::isa<mlir::StringAttr, mlir::IntegerAttr, mlir::TypeAttr,
                  mlir::UnitAttr>(attr))
      return;
    auto it = refsByAttr.find(attr);
    if (it == refsByAttr.end()) {
      llvm::SmallVector<mlir::StringAttr, 1> refs;
      attr.walk([&](mlir::SymbolRefAttr ref) {
        refs.push_back(ref.getRootReference());
      });
      it = refsByAttr.try_emplace(attr, std::move(refs)).first;
    }
    for (mlir::StringAttr name : it->second)
      fn(name);
  }
};

/// How many times each symbol is referenced from an op attribute anywhere in
/// `module`. SymbolTable::getSymbolUses would be the obvious tool, but it gives
/// up on a CIR module, which holds ops it cannot rule out as nested symbol
/// tables.
///
/// The module's own attributes also name functions by string: the global
/// constructor and destructor lists, llvm.used, the annotations. Every string
/// there counts as a use of the function of that name, which can only keep a
/// function alive, never drop one that is used.
///
/// The module's top-level ops are counted in chunks in parallel, each chunk
/// with its own finder (its memo is lost; attributes recur mostly within a
/// chunk), and the counts summed.
llvm::DenseMap<mlir::StringAttr, unsigned>
countSymbolRefs(mlir::ModuleOp module, SymbolRefFinder &finder) {
  llvm::SmallVector<mlir::Operation *> ops;
  for (mlir::Operation &op : *module.getBody())
    ops.push_back(&op);
  mlir::MLIRContext *ctx = module.getContext();
  size_t chunks = std::min<size_t>(
      ops.size(), ctx->isMultithreadingEnabled() ? 4 * ctx->getNumThreads() : 1);
  std::vector<llvm::DenseMap<mlir::StringAttr, unsigned>> partial(chunks);
  mlir::parallelFor(ctx, 0, chunks, [&](size_t chunk) {
    // One chunk is the caller's thread, which keeps the caller's memo.
    SymbolRefFinder localFinder;
    SymbolRefFinder &chunkFinder = chunks == 1 ? finder : localFinder;
    for (size_t i = chunk * ops.size() / chunks,
                e = (chunk + 1) * ops.size() / chunks;
         i != e; ++i)
      ops[i]->walk([&](mlir::Operation *op) {
        chunkFinder.forEach(op,
                            [&](mlir::StringAttr name) { ++partial[chunk][name]; });
      });
  });
  llvm::DenseMap<mlir::StringAttr, unsigned> counts;
  for (const auto &chunkCounts : partial)
    for (const auto &[name, count] : chunkCounts)
      counts[name] += count;
  finder.forEach(module, [&](mlir::StringAttr name) { ++counts[name]; });
  module->getAttrDictionary().walk(
      [&](mlir::StringAttr name) { ++counts[name]; });
  return counts;
}

struct CIRInlinerPass : public impl::CIRInlinerBase<CIRInlinerPass> {
  using CIRInlinerBase::CIRInlinerBase;

  void getDependentDialects(DialectRegistry &registry) const override {
    CIRInlinerBase::getDependentDialects(registry);
    registry.addExtension(+[](MLIRContext *, cir::CIRDialect *dialect) {
      dialect->addInterfaces<CIRInlinerInterface>();
    });
    // MLIR's inliner asks every loaded dialect for its inliner interface, and
    // the LLVM and func dialects, which come loaded with CIR, only promise
    // theirs; the implementations are in these extensions.
    mlir::LLVM::registerInlinerInterface(registry);
    mlir::func::registerInlinerExtension(registry);
  }

  void runOnOperation() override;

  LogicalResult runNested(OpPassManager &pipeline, Operation *op) {
    return runPipeline(pipeline, op);
  }

private:
  /// Symbol uses at the start of the pass, for the only-use rule. Inlining
  /// changes them as it goes; a stale count can only make a size decision
  /// less precise, never an illegal inline.
  llvm::DenseMap<mlir::StringAttr, unsigned> useCounts;

  /// The strongly connected component of each call graph node at the start of
  /// the pass. A call between two nodes of one component is recursive.
  llvm::DenseMap<const mlir::CallGraphNode *, unsigned> sccOf;

  /// A running estimate of each caller's weighted size: its size when first
  /// asked about, plus the size of each callee inlined into it since.
  llvm::DenseMap<mlir::Operation *, unsigned> callerSize;

  /// Each callee's weighted size, up to maxCallerSize, measured once: callees
  /// are inlined bottom-up, so a callee is done growing when it is first
  /// asked about.
  llvm::DenseMap<mlir::Operation *, unsigned> calleeSizes;

  bool isProfitable(const Inliner::ResolvedCall &resolved);
  void dropUnusedFunctions(mlir::ModuleOp module, SymbolRefFinder &symbolRefs);
};

bool CIRInlinerPass::isProfitable(const Inliner::ResolvedCall &resolved) {
  // An indirect or external call resolves to a node with no region.
  mlir::Region *region = resolved.targetNode->getCallableRegion();
  auto callee =
      region ? mlir::dyn_cast<cir::FuncOp>(region->getParentOp()) : nullptr;
  if (!callee || callee.isDeclaration())
    return false;

  // A caller stops taking inlined code once it has grown past
  // maxCallerSize. Without a bound, inlining around a recursive cycle
  // multiplied: generated encoders for recursive types grew to 12 million
  // instructions each in one real library.
  // A call outside any function (none reaches here today) has no caller to
  // bound, and must not share a size with the others.
  auto caller = resolved.call->getParentOfType<cir::FuncOp>();
  auto [calleeIt, firstSized] = calleeSizes.try_emplace(callee, 0);
  if (firstSized)
    calleeIt->second = weightedSize(callee, maxCallerSize);
  unsigned calleeSize = calleeIt->second;
  // Nothing is inserted into callerSize before inlined() uses this.
  unsigned *grown = nullptr;
  if (caller) {
    auto [sizeIt, firstAsked] = callerSize.try_emplace(caller, 0);
    if (firstAsked)
      sizeIt->second = weightedSize(caller, maxCallerSize);
    if (sizeIt->second >= maxCallerSize) {
      LDBG() << "skip " << callee.getSymName() << ": caller too large ("
             << sizeIt->second << ")";
      return false;
    }
    grown = &sizeIt->second;
  }
  auto inlined = [&] {
    if (grown)
      *grown += calleeSize;
    return true;
  };

  // Around a recursive cycle, only callees small enough for the size rule:
  // the always-inline and only-use rules would copy the cycle into itself, a
  // whole level more at each round.
  auto sourceScc = sccOf.find(resolved.sourceNode);
  auto targetScc = sccOf.find(resolved.targetNode);
  bool recursive = sourceScc != sccOf.end() && targetScc != sccOf.end() &&
                   sourceScc->second == targetScc->second;
  if (recursive) {
    LDBG() << (calleeSize <= threshold ? "inline " : "skip ")
           << callee.getSymName() << ": recursive, size " << calleeSize;
    return calleeSize <= threshold && inlined();
  }

  if (callee.getInlineKind() == cir::InlineKind::AlwaysInline ||
      hasExtraAttr(resolved.call, "alwaysinline"))
    return inlined();
  // Hotness would go here: there is no profile on this path yet, so every
  // call counts as neutral and the decision is size alone.
  unsigned uses = useCounts.lookup(callee.getSymNameAttr());
  if (isDroppableWhenUnused(callee) && uses == 1) {
    LDBG() << "inline " << callee.getSymName() << ": its only use";
    return inlined();
  }
  unsigned size = std::min(calleeSize, threshold + 1);
  LDBG() << (size <= threshold ? "inline " : "skip ") << callee.getSymName()
         << ": size " << size << " (threshold " << threshold << "), " << uses
         << " uses, droppable " << isDroppableWhenUnused(callee);
  return size <= threshold && inlined();
}

/// Drop the droppable functions left without a use, and then the ones only
/// those referenced, and so on: uses are counted once, and erasing a function
/// takes its own references off the counts.
void CIRInlinerPass::dropUnusedFunctions(mlir::ModuleOp module,
                                         SymbolRefFinder &symbolRefs) {
  llvm::DenseMap<mlir::StringAttr, unsigned> refs =
      countSymbolRefs(module, symbolRefs);
  llvm::DenseMap<mlir::StringAttr, cir::FuncOp> droppable;
  llvm::SmallVector<cir::FuncOp> dead;
  for (auto func : module.getOps<cir::FuncOp>()) {
    if (func.isDeclaration() || !isDroppableWhenUnused(func))
      continue;
    droppable[func.getSymNameAttr()] = func;
    if (refs.lookup(func.getSymNameAttr()) == 0)
      dead.push_back(func);
  }
  while (!dead.empty()) {
    cir::FuncOp func = dead.pop_back_val();
    LDBG() << "drop " << func.getSymName() << ": no use left";
    droppable.erase(func.getSymNameAttr());
    func->walk([&](mlir::Operation *op) {
      symbolRefs.forEach(op, [&](mlir::StringAttr name) {
        unsigned &count = refs[name];
        if (count && --count == 0)
          if (cir::FuncOp callee = droppable.lookup(name))
            dead.push_back(callee);
      });
    });
    func.erase();
  }
}

void CIRInlinerPass::runOnOperation() {
  llvm::TimeTraceScope scope("CIR Inliner");
  mlir::ModuleOp module = getOperation();

  SymbolRefFinder symbolRefs;
  {
    llvm::TimeTraceScope countScope("countSymbolRefs");
    useCounts = countSymbolRefs(module, symbolRefs);
  }

  // MLIR's inliner erases a private callable once its use list is empty, and
  // that list cannot see a function named only from a module attribute -- a
  // global constructor is the common case. Keep those public while it runs.
  llvm::DenseSet<mlir::StringAttr> namedByModule;
  module->getAttrDictionary().walk(
      [&](mlir::StringAttr name) { namedByModule.insert(name); });
  llvm::SmallVector<cir::FuncOp> pinned;
  for (auto func : module.getOps<cir::FuncOp>())
    if (func.isPrivate() && namedByModule.contains(func.getSymNameAttr())) {
      func.setPublic();
      pinned.push_back(func);
    }

  // MLIR's inliner moves a callee into its only caller instead of copying it,
  // and erases the callees it leaves without a use, only when the callee is
  // discardable to MLIR: private. A hidden linkonce_odr or available_externally
  // function is droppable here (isDroppableWhenUnused) but public to MLIR, so
  // each one inlined into its only caller was copied, and the original kept
  // until dropUnusedFunctions at the end. On one real library (12.8 M ops) that
  // copied 6.5 M ops and the link ran out of memory. Those functions are
  // private while the inliner runs, and the ones that survive get their
  // visibility back.
  llvm::DenseSet<mlir::StringAttr> madePrivate;
  for (auto func : module.getOps<cir::FuncOp>())
    if (!func.isPrivate() && !func.isDeclaration() &&
        isDroppableWhenUnused(func) &&
        !namedByModule.contains(func.getSymNameAttr())) {
      func.setPrivate();
      madePrivate.insert(func.getSymNameAttr());
    }

  // No simplification pipeline between inlining rounds: mem2reg runs again
  // after this pass, and nothing else is validated on CIR yet.
  InlinerConfig config;
  config.setDefaultPipeline(nullptr);
  auto runPipelineHelper = [](Pass &pass, OpPassManager &pipeline,
                              Operation *op) {
    return static_cast<CIRInlinerPass &>(pass).runNested(pipeline, op);
  };
  CallGraph *cg;
  {
    llvm::TimeTraceScope callGraphScope("buildCallGraph");
    cg = &getAnalysis<CallGraph>();
    unsigned scc = 0;
    for (auto it = llvm::scc_begin(static_cast<const CallGraph *>(cg));
         !it.isAtEnd(); ++it, ++scc)
      for (const CallGraphNode *node : *it)
        sccOf[node] = scc;
  }
  Inliner inliner(module, *cg, *this, getAnalysisManager(), runPipelineHelper,
                  config, [this](const Inliner::ResolvedCall &resolved) {
                    return isProfitable(resolved);
                  });
  LogicalResult inlined = failure();
  {
    llvm::TimeTraceScope inlineScope("doInlining");
    inlined = inliner.doInlining();
  }
  for (cir::FuncOp func : pinned)
    func.setPrivate();
  if (!madePrivate.empty())
    for (auto func : module.getOps<cir::FuncOp>())
      if (madePrivate.contains(func.getSymNameAttr()))
        func.setPublic();
  if (failed(inlined))
    return signalPassFailure();

  llvm::TimeTraceScope dropScope("dropUnusedFunctions");
  dropUnusedFunctions(module, symbolRefs);
}

} // namespace

std::unique_ptr<Pass> mlir::createCIRInlinerPass() {
  return std::make_unique<CIRInlinerPass>();
}

std::unique_ptr<Pass>
mlir::createCIRInlinerPass(std::optional<unsigned> threshold) {
  if (!threshold)
    return createCIRInlinerPass();
  CIRInlinerOptions options;
  options.threshold = *threshold;
  return std::make_unique<CIRInlinerPass>(options);
}
