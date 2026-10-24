//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PassDetail.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/Region.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <map>

using namespace mlir;
using namespace cir;

namespace mlir {
#define GEN_PASS_DEF_CIRSIMPLIFY
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

//===----------------------------------------------------------------------===//
// Rewrite patterns
//===----------------------------------------------------------------------===//

namespace {

/// Dominance information that stays coherent while patterns rewrite the IR.
/// DominanceInfo caches per region and never notices IR edits, so this doubles
/// as the rewrite listener and drops a region's cached tree whenever its block
/// list changes. Successor edits would need notifyOperationModified as well; no
/// pattern here makes any.
class RewriteDominanceInfo final : public RewriterBase::Listener {
public:
  DominanceInfo &getDominanceInfo() { return domInfo; }

  void notifyBlockInserted(Block *block, Region *previous,
                           Region::iterator previousIt) override {
    domInfo.invalidate(block->getParent(), /*recursive=*/false);
    if (previous)
      domInfo.invalidate(previous, /*recursive=*/false);
  }

  void notifyBlockErased(Block *block) override {
    domInfo.invalidate(block->getParent(), /*recursive=*/false);
  }

private:
  DominanceInfo domInfo;
};

/// Find the `cir.store` operation that stores to the given alloca and dominates
/// the given load operation. Dominance calculation is done through the given
/// DominanceInfo object.
///
/// Return nullptr if no such store operation exists or if multiple store
/// operations satisfy the criteria.
cir::StoreOp findDominatingInitOp(cir::AllocaOp alloca, cir::LoadOp load,
                                  const DominanceInfo &domInfo) {
  cir::StoreOp result;

  // Walk through all uses of the alloca and visit the store operations that
  // store to the alloca
  for (const mlir::OpOperand &use : alloca->getUses()) {
    auto store = mlir::dyn_cast<cir::StoreOp>(use.getOwner());
    if (!store)
      continue;

    // `cir.store` has two operands, we're only interested if the store is
    // storing into the alloca, not if the store is storing the address of the
    // alloca slot into somewhere else
    if (use.getOperandNumber() != cir::StoreOp::odsIndex_addr)
      continue;

    if (domInfo.dominates(store, load)) {
      if (result) {
        // If we have already found a dominating store, then there are multiple
        // dominating stores, we intentionally don't simplify the load.
        return nullptr;
      }
      result = store;
    }
  }

  return result;
}

/// Simplify `cir.load` that loads from an alloca marked as "constant".
///
/// For example:
///
///   %0 = cir.alloca "x" align(4) const : !cir.ptr<!s32i>
///   cir.store %init, %0 : !s32i, !cir.ptr<!s32i>
///   %1 = cir.load %0 : !cir.ptr<!s32i>
///
/// All uses of the load above could be replaced with the SSA value `%init`.
struct SimplifyConstantLoad : public OpRewritePattern<LoadOp> {
  SimplifyConstantLoad(MLIRContext *ctx, RewriteDominanceInfo &domInfo)
      : OpRewritePattern<LoadOp>(ctx), domInfo(domInfo) {}

  LogicalResult matchAndRewrite(LoadOp op,
                                PatternRewriter &rewriter) const override {
    // Volatile or atomic loads should not be simplified.
    if (op.getIsVolatile() || op.getMemOrder())
      return mlir::failure();

    auto allocaOp = op.getAddr().getDefiningOp<cir::AllocaOp>();
    if (!allocaOp || !allocaOp.getConstant())
      return mlir::failure();

    cir::StoreOp initStoreOp =
        findDominatingInitOp(allocaOp, op, domInfo.getDominanceInfo());
    if (!initStoreOp)
      return mlir::failure();
    if (initStoreOp.getIsVolatile() || initStoreOp.getMemOrder()) {
      // We intentionally act conservatively here and we don't want to simplify
      // the load if the corresponding store is either volatile or atomic.
      return mlir::failure();
    }

    rewriter.replaceOp(op, initStoreOp.getValue());
    return mlir::success();
  }

private:
  RewriteDominanceInfo &domInfo;
};

/// A scalar leaf of a (possibly nested) record store decomposition: the
/// member-index path from the top record, the leaf type, and its absolute
/// byte offset in the top record.
struct ScalarField {
  llvm::SmallVector<unsigned, 4> path;
  mlir::Type ty;
  uint64_t off;
};

/// Collect the scalar leaves of \p recordTy covering bytes below
/// \p maxBytes (absolute offsets include \p baseOff).
///
/// Struct members recurse (like classic CodeGen's recursive aggregate
/// store emission) down to \p maxDepth nesting levels (0 = no limit);
/// every other member must be a plain scalar. Padding, empty,
/// zero-sized, and past-the-limit members are skipped; arrays,
/// bit-fields, unions, and members past the depth limit fail, as does a
/// scalar leaf straddling \p maxBytes. A nested struct crossing
/// \p maxBytes is fine: its trailing members are simply past the limit.
static LogicalResult
collectScalarFields(mlir::DataLayout &dl, cir::StructType recordTy,
                    uint64_t baseOff, uint64_t maxBytes, unsigned maxDepth,
                    unsigned depth, llvm::SmallVectorImpl<unsigned> &path,
                    llvm::SmallVectorImpl<ScalarField> &fields) {
  if (recordTy.isIncomplete())
    return mlir::failure();
  auto members = recordTy.getMembers();
  auto memberKinds = recordTy.getMemberKinds();
  if (members.empty() || memberKinds.size() != members.size())
    return mlir::failure();
  for (unsigned idx = 0, e = members.size(); idx != e; ++idx) {
    if (memberKinds[idx] == cir::RecordMemberKind::Pad ||
        memberKinds[idx] == cir::RecordMemberKind::Empty)
      continue;
    if (memberKinds[idx] != cir::RecordMemberKind::Data)
      return mlir::failure();
    mlir::Type memberTy = members[idx];
    uint64_t sz = dl.getTypeSize(memberTy);
    if (sz == 0)
      continue;
    uint64_t off = baseOff + recordTy.getElementOffset(dl, idx);
    if (off >= maxBytes)
      continue;
    if (auto nested = dyn_cast<cir::StructType>(memberTy)) {
      if (maxDepth != 0 && depth >= maxDepth)
        return mlir::failure();
      path.push_back(idx);
      LogicalResult res = collectScalarFields(
          dl, nested, off, maxBytes, maxDepth, depth + 1, path, fields);
      path.pop_back();
      if (failed(res))
        return mlir::failure();
      continue;
    }
    if (!isa<cir::IntType, cir::PointerType, cir::BoolType>(memberTy) &&
        !isa<cir::FPTypeInterface>(memberTy))
      return mlir::failure();
    if (off + sz > maxBytes)
      return mlir::failure();
    fields.push_back({llvm::SmallVector<unsigned, 4>(path.begin(), path.end()),
                      memberTy, off});
    fields.back().path.push_back(idx);
  }
  return mlir::success();
}

/// Emit one extract/get_member chain + store per scalar leaf of
/// \p recordTy covering bytes below \p maxBytes, writing \p recordValue's
/// fields into \p dstAddr (which must point to a \p recordTy object).
/// Field stores are cloned from \p storeTemplate (inheriting its
/// attributes) with offset-refined alignment. Recursion descends at most
/// \p maxDepth member levels (0 = no limit).
///
/// Padding gaps below \p maxBytes get no store. Callers decide what
/// skipped padding means: ScalarizeAggregateStore always skips it like
/// classic emission, while narrowing folds only run on call results,
/// whose padding is undef.
static LogicalResult emitScalarFieldStores(
    PatternRewriter &rewriter, StoreOp storeTemplate, mlir::Value recordValue,
    cir::StructType recordTy, mlir::Value dstAddr, uint64_t baseAlign,
    uint64_t maxBytes, unsigned maxDepth, mlir::DataLayout &dl) {
  llvm::SmallVector<ScalarField, 4> fields;
  llvm::SmallVector<unsigned, 4> path;
  if (failed(collectScalarFields(dl, recordTy, /*baseOff=*/0, maxBytes,
                                 maxDepth, /*depth=*/1, path, fields)))
    return mlir::failure();
  // At least one leaf must survive: deleting a store that only covers
  // padding would drop the write entirely (and break downstream passes
  // that key off the store, like TargetLowering's sret-slot reuse).
  if (fields.empty())
    return mlir::failure();

  auto addrSpace = cast<cir::PointerType>(dstAddr.getType()).getAddrSpace();
  // Memoize shared path prefixes: leaves are emitted in layout order into
  // the same block, so an earlier leaf's intermediate extract/get_member
  // dominates and can be reused instead of rebuilt per leaf.
  std::map<std::vector<unsigned>, std::pair<mlir::Value, mlir::Value>>
      prefixMemo;
  for (auto &field : fields) {
    // Walk the path, extracting the value and addressing the field one
    // level at a time.
    mlir::Value member = recordValue;
    mlir::Value memberAddr = dstAddr;
    cir::StructType curTy = recordTy;
    std::vector<unsigned> prefix;
    for (unsigned idx : field.path) {
      prefix.push_back(idx);
      auto it = prefixMemo.find(prefix);
      if (it != prefixMemo.end()) {
        member = it->second.first;
        memberAddr = it->second.second;
      } else {
        mlir::Type memberTy = curTy.getMembers()[idx];
        member = rewriter.create<ExtractMemberOp>(storeTemplate.getLoc(),
                                                  memberTy, member, idx);
        auto fieldPtrTy = cir::PointerType::get(memberTy, addrSpace);
        memberAddr = rewriter.create<GetMemberOp>(
            storeTemplate.getLoc(), fieldPtrTy, memberAddr, "", idx);
        prefixMemo[prefix] = {member, memberAddr};
      }
      if (auto nested = dyn_cast<cir::StructType>(curTy.getMembers()[idx]))
        curTy = nested;
    }
    auto *fieldStore = rewriter.clone(*storeTemplate.getOperation());
    fieldStore->setOperand(cir::StoreOp::odsIndex_value, member);
    fieldStore->setOperand(cir::StoreOp::odsIndex_addr, memberAddr);
    // The field address is the base plus the member offset: its alignment
    // is the base alignment refined by the offset, capped at the field's
    // own ABI alignment.
    uint64_t fieldAlign =
        field.off == 0
            ? baseAlign
            : std::min(baseAlign, uint64_t{1} << llvm::countr_zero(field.off));
    fieldAlign = std::min(fieldAlign, dl.getTypeABIAlignment(field.ty));
    uint64_t defaultAlign = dl.getTypeABIAlignment(field.ty);
    if (fieldAlign == defaultAlign)
      fieldStore->removeAttr("alignment");
    else
      fieldStore->setAttr("alignment", rewriter.getI64IntegerAttr(fieldAlign));
  }
  return mlir::success();
}

/// Whether TargetLowering would reuse \p storeOp's destination as its
/// call's sret slot, making any store rewrite pure pessimization: the
/// call result has a single use (this store into a matching slot) and
/// the slot dominates the call, so the callee would construct directly
/// into the destination. Mirrors TargetLowering's reuse preconditions,
/// plus a size check: small records return in registers (SysV/AArch64:
/// 16 bytes or less), so no sret slot exists to reuse. (Windows uses an
/// 8-byte threshold; the accepted imprecision is that 9-16 byte records
/// fold instead of reusing there — still sound, marginally worse code in
/// a rare shape.) Leave such stores alone; if reuse does not fire,
/// later phases still see the shape.
static bool wouldReuseSretSlot(StoreOp storeOp, mlir::DataLayout &dl,
                               mlir::DominanceInfo &domInfo) {
  auto callOp = storeOp.getValue().getDefiningOp<cir::CallOp>();
  if (!callOp)
    return false;
  mlir::Value result = callOp.getResult();
  if (!result || !result.hasOneUse())
    return false;
  if (storeOp.getAddr().getType() != cir::PointerType::get(result.getType()))
    return false;
  if (dl.getTypeSize(result.getType()) <= 16)
    return false;
  return domInfo.properlyDominates(storeOp.getAddr(), callOp.getOperation());
}

/// Forward a dominating whole-object store into a copy from the slot.
///
/// For example:
///
///   %slot = cir.alloca !cir.ptr<!rec_S>   (no uses besides the store,
///                                          the copy and markers)
///   cir.store %v, %slot : !rec_S, !cir.ptr<!rec_S>
///   cir.copy %slot to %dst : !cir.ptr<!rec_S>
///
/// becomes:
///
///   cir.store %v, %dst : !rec_S, !cir.ptr<!rec_S>
///
/// (with a dst-side bitcast when the copy reads the slot through one, as in
/// CIRGen's coerce temporaries). When the copy moves a strict prefix of the
/// stored bytes (the coercion record is wider than the named record, or the
/// copy skips tail padding), the fold instead emits scalar field stores
/// covering exactly the copied prefix. This is CIR's analog of LLVM
/// MemCpyOpt's store-to-memcpy forwarding: CIR materializes aggregate
/// rvalues through slots far more often than classic CodeGen, and each
/// pair costs the LLVM optimizer a split. Folding them here shrinks the
/// optimizer's input.
///
/// Requirements: the copy is non-volatile; the slot's only uses are the
/// single store, the copy source (through a chain of bitcasts, if any)
/// and lifetime markers; the store is non-volatile, non-atomic and
/// non-nontemporal and dominates the copy; the store covers every byte the
/// copy moves. Narrowing additionally requires a call result as the
/// stored value (only its padding is known-undef, see below) and
/// scalarizable members (arrays and bit-fields bail, like
/// scalarization).
struct SimplifyStoreCopyForward : public OpRewritePattern<CopyOp> {
  SimplifyStoreCopyForward(MLIRContext *ctx, mlir::DominanceInfo &domInfo,
                           unsigned maxDepth)
      : OpRewritePattern<CopyOp>(ctx), domInfo(domInfo), maxDepth(maxDepth) {}

  LogicalResult matchAndRewrite(CopyOp copyOp,
                                PatternRewriter &rewriter) const override {
    // Volatile copies cannot be forwarded.
    if (copyOp.getIsVolatile())
      return mlir::failure();

    // Trace the copy source through bitcasts (the coerce-temporary shape)
    // back to an alloca. casts[0] is adjacent to the copy.
    mlir::Value src = copyOp.getSrc();
    llvm::SmallVector<CastOp, 2> casts;
    while (auto cast = src.getDefiningOp<CastOp>()) {
      if (cast.getKind() != cir::CastKind::bitcast)
        return mlir::failure();
      casts.push_back(cast);
      src = cast.getSrc();
    }
    auto allocaOp = src.getDefiningOp<AllocaOp>();
    if (!allocaOp)
      return mlir::failure();

    // Every bitcast in the chain must feed only the next link (or the copy).
    for (auto [idx, cast] : llvm::enumerate(casts)) {
      Operation *user =
          idx == 0 ? copyOp.getOperation() : casts[idx - 1].getOperation();
      if (!cast->hasOneUse() || *cast->user_begin() != user)
        return mlir::failure();
    }

    // The slot's only other use must be the single store into it. Lifetime
    // markers are benign: they go away with the dead slot below.
    Operation *chainUser =
        casts.empty() ? copyOp.getOperation() : casts.back().getOperation();
    mlir::Value chainValue =
        casts.empty() ? copyOp.getSrc() : casts.back().getSrc();
    StoreOp storeOp;
    for (OpOperand &use : allocaOp->getUses()) {
      Operation *owner = use.getOwner();
      if (isa<LifetimeStartOp, LifetimeEndOp>(owner))
        continue;
      if (owner == chainUser && use.get() == chainValue)
        continue;
      auto store = dyn_cast<StoreOp>(owner);
      if (!store || use.get() != store.getAddr() || storeOp)
        return mlir::failure();
      storeOp = store;
    }
    if (!storeOp)
      return mlir::failure();

    // Only plain stores can be relocated.
    if (storeOp.getIsVolatile() || storeOp.getIsNontemporal() ||
        storeOp.getMemOrder() || storeOp.getSyncScope())
      return mlir::failure();

    // Do not forward parameter spills. CallConvLowering consumes stores of
    // entry block arguments only off plain matching allocas: a bitcast
    // destination (or two parameters sharing one slot) is NYI there, and
    // the spill exists for CallConv to rewrite anyway.
    if (auto blockArg = dyn_cast<mlir::BlockArgument>(storeOp.getValue())) {
      mlir::Block *block = blockArg.getOwner();
      if (block->isEntryBlock() && isa<cir::FuncOp>(block->getParentOp()))
        return mlir::failure();
    }

    // The store must dominate the copy (same region: dominance is a
    // single-region query, and moving the store across regions would change
    // where it executes relative to region boundaries).
    if (storeOp->getParentRegion() != copyOp->getParentRegion() ||
        !domInfo.dominates(storeOp, copyOp))
      return mlir::failure();

    auto storePtrTy = cast<cir::PointerType>(storeOp.getAddr().getType());
    auto copySrcPtrTy = cast<cir::PointerType>(copyOp.getSrc().getType());
    auto copyDstPtrTy = cast<cir::PointerType>(copyOp.getDst().getType());

    // Keep address spaces out of the picture: all three pointers must agree.
    if (storePtrTy.getAddrSpace() != copySrcPtrTy.getAddrSpace() ||
        storePtrTy.getAddrSpace() != copyDstPtrTy.getAddrSpace())
      return mlir::failure();

    // The store must cover every byte the copy moves. Layouts may differ
    // (coerce records vs named records), and the copy may move fewer bytes
    // than the store wrote: the coercion record is typically wider than the
    // named record, and skip_tail_padding copies move only the data bytes.
    // The rewrite below transfers exactly the copied prefix and nothing
    // more, so both cases fold to identical memory state.
    mlir::Type storePointee = storePtrTy.getPointee();
    mlir::Type copyPointee = copySrcPtrTy.getPointee();
    mlir::DataLayout dl(copyOp->getParentOfType<mlir::ModuleOp>());
    uint64_t storeSize = dl.getTypeSize(storePointee);
    uint64_t copySize = copyOp.getCopySizeInBytes(dl);
    if (copySize > storeSize)
      return mlir::failure();

    // The moved store keeps the weaker (smaller) of the two alignments:
    // weakening a store's alignment promise is always sound.
    uint64_t storeAlign =
        storeOp.getAlignment().value_or(dl.getTypeABIAlignment(storePointee));
    uint64_t dstAlign =
        copyOp.getDstAlignment().value_or(dl.getTypeABIAlignment(copyPointee));
    uint64_t newAlign = std::min(storeAlign, dstAlign);

    // Rewrite at the copy. A zero-byte copy is a no-op and the store feeds
    // only the dead slot, so both just go away.
    rewriter.setInsertionPoint(copyOp);
    if (copySize != 0) {
      if (copySize == storeSize) {
        // Equal sizes: bitcast the destination when the pointee types
        // differ, then clone the store there (keeping the stored value).
        mlir::Value newDst = copyOp.getDst();
        if (storePointee != copyPointee) {
          auto dstPtrTy =
              cir::PointerType::get(storePointee, copyDstPtrTy.getAddrSpace());
          newDst = rewriter.create<CastOp>(copyOp.getLoc(), dstPtrTy,
                                           cir::CastKind::bitcast, newDst);
        }
        auto *newStore = rewriter.clone(*storeOp.getOperation());
        newStore->setOperand(cir::StoreOp::odsIndex_addr, newDst);
        uint64_t defaultAlign = dl.getTypeABIAlignment(storePointee);
        if (newAlign == defaultAlign)
          newStore->removeAttr("alignment");
        else
          newStore->setAttr("alignment", rewriter.getI64IntegerAttr(newAlign));
      } else {
        // Narrowing: the copy moves a strict prefix of the stored bytes.
        // Emit scalar field stores covering exactly the copied prefix
        // directly into the destination. Bytes in the prefix covered by
        // no field are the stored value's padding: the copy overwrote
        // them with that padding, while the field stores leave the old
        // destination content in place. That is a sound refinement only
        // when the padding is undef, so the stored value must be a call
        // result (a call's padding is undef; a loaded value's is not).
        // A single-use call result feeding a dominating slot is left for
        // TargetLowering's sret-slot reuse instead (direct construction
        // beats any fold).
        if (!isa_and_nonnull<cir::CallOp>(storeOp.getValue().getDefiningOp()) ||
            wouldReuseSretSlot(storeOp, dl, domInfo))
          return mlir::failure();
        auto recordTy = dyn_cast<cir::StructType>(storePointee);
        if (!recordTy)
          return mlir::failure();
        // Address the destination with the stored record's layout: field
        // offsets below the copied size are valid byte offsets into it.
        mlir::Value narrowDst = copyOp.getDst();
        if (storePointee != copyDstPtrTy.getPointee()) {
          auto dstPtrTy =
              cir::PointerType::get(storePointee, copyDstPtrTy.getAddrSpace());
          narrowDst = rewriter.create<CastOp>(
              copyOp.getLoc(), dstPtrTy, cir::CastKind::bitcast, narrowDst);
        }
        if (failed(emitScalarFieldStores(rewriter, storeOp, storeOp.getValue(),
                                         recordTy, narrowDst, newAlign,
                                         copySize, maxDepth, dl)))
          return mlir::failure();
      }
    }

    // Erase the copy, the chain, the old store, and the now-dead slot with
    // its markers.
    rewriter.eraseOp(copyOp);
    for (CastOp cast : casts)
      rewriter.eraseOp(cast);
    rewriter.eraseOp(storeOp);
    for (Operation *user : llvm::make_early_inc_range(allocaOp->getUsers())) {
      if (!isa<LifetimeStartOp, LifetimeEndOp>(user))
        return mlir::success();
      rewriter.eraseOp(user);
    }
    rewriter.eraseOp(allocaOp);
    return mlir::success();
  }

private:
  mlir::DominanceInfo &domInfo;
  unsigned maxDepth;
};

/// Simplify suitable ternary operations into select operations.
///
/// For now we only simplify those ternary operations whose true and false
/// branches directly yield a value or a constant. That is, both of the true and
/// the false branch must either contain a cir.yield operation as the only
/// operation in the branch, or contain a cir.const operation followed by a
/// cir.yield operation that yields the constant value.
///
/// For example, we will simplify the following ternary operation:
///
///   %0 = ...
///   %1 = cir.ternary (%condition, true {
///     %2 = cir.const ...
///     cir.yield %2
///   } false {
///     cir.yield %0
///
/// into the following sequence of operations:
///
///   %1 = cir.const ...
///   %0 = cir.select if %condition then %1 else %2
struct SimplifyTernary final : public OpRewritePattern<TernaryOp> {
  using OpRewritePattern<TernaryOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(TernaryOp op,
                                PatternRewriter &rewriter) const override {
    if (op->getNumResults() != 1)
      return mlir::failure();

    if (!isSimpleTernaryBranch(op.getTrueRegion()) ||
        !isSimpleTernaryBranch(op.getFalseRegion()))
      return mlir::failure();

    cir::YieldOp trueBranchYieldOp =
        mlir::cast<cir::YieldOp>(op.getTrueRegion().front().getTerminator());
    cir::YieldOp falseBranchYieldOp =
        mlir::cast<cir::YieldOp>(op.getFalseRegion().front().getTerminator());
    mlir::Value trueValue = trueBranchYieldOp.getArgs()[0];
    mlir::Value falseValue = falseBranchYieldOp.getArgs()[0];

    rewriter.inlineBlockBefore(&op.getTrueRegion().front(), op);
    rewriter.inlineBlockBefore(&op.getFalseRegion().front(), op);
    rewriter.eraseOp(trueBranchYieldOp);
    rewriter.eraseOp(falseBranchYieldOp);
    rewriter.replaceOpWithNewOp<cir::SelectOp>(op, op.getCond(), trueValue,
                                               falseValue);

    return mlir::success();
  }

private:
  bool isSimpleTernaryBranch(mlir::Region &region) const {
    if (!region.hasOneBlock())
      return false;

    mlir::Block &onlyBlock = region.front();
    mlir::Block::OpListType &ops = onlyBlock.getOperations();

    // The region/block could only contain at most 2 operations.
    if (ops.size() > 2)
      return false;

    if (ops.size() == 1) {
      // The region/block only contain a cir.yield operation.
      return true;
    }

    // Check whether the region/block contains a cir.const followed by a
    // cir.yield that yields the value.
    auto yieldOp = mlir::cast<cir::YieldOp>(onlyBlock.getTerminator());
    auto yieldValueDefOp =
        yieldOp.getArgs()[0].getDefiningOp<cir::ConstantOp>();
    return yieldValueDefOp && yieldValueDefOp->getBlock() == &onlyBlock;
  }
};

/// Simplify select operations with boolean constants into simpler forms.
///
/// This pattern simplifies select operations where both true and false values
/// are boolean constants. Two specific cases are handled:
///
/// 1. When selecting between true and false based on a condition,
///    the operation simplifies to just the condition itself:
///
///    %0 = cir.select if %condition then true else false
///    ->
///    (replaced with %condition directly)
///
/// 2. When selecting between false and true based on a condition,
///    the operation simplifies to the logical negation of the condition:
///
///    %0 = cir.select if %condition then false else true
///    ->
///    %0 = cir.not %condition
struct SimplifySelect : public OpRewritePattern<SelectOp> {
  using OpRewritePattern<SelectOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(SelectOp op,
                                PatternRewriter &rewriter) const final {
    auto trueValueOp = op.getTrueValue().getDefiningOp<cir::ConstantOp>();
    auto falseValueOp = op.getFalseValue().getDefiningOp<cir::ConstantOp>();
    if (!trueValueOp || !falseValueOp)
      return mlir::failure();

    auto trueValue = trueValueOp.getValueAttr<cir::BoolAttr>();
    auto falseValue = falseValueOp.getValueAttr<cir::BoolAttr>();
    if (!trueValue || !falseValue)
      return mlir::failure();

    // cir.select if %0 then #true else #false -> %0
    if (trueValue.getValue() && !falseValue.getValue()) {
      rewriter.replaceAllUsesWith(op, op.getCondition());
      rewriter.eraseOp(op);
      return mlir::success();
    }

    // cir.select if %0 then #false else #true -> cir.not %0
    if (!trueValue.getValue() && falseValue.getValue()) {
      rewriter.replaceOpWithNewOp<cir::NotOp>(op, op.getCondition());
      return mlir::success();
    }

    return mlir::failure();
  }
};

/// Simplify `cir.switch` operations by folding cascading cases
/// into a single `cir.case` with the `anyof` kind.
///
/// This pattern identifies cascading cases within a `cir.switch` operation.
/// Cascading cases are defined as consecutive `cir.case` operations of kind
/// `equal`, each containing a single `cir.yield` operation in their body.
///
/// The pattern merges these cascading cases into a single `cir.case` operation
/// with kind `anyof`, aggregating all the case values.
///
/// The merging process continues until a `cir.case` with a different body
/// (e.g., containing `cir.break` or compound stmt) is encountered, which
/// breaks the chain.
///
/// Example:
///
/// Before:
///   cir.case equal, [#cir.int<0> : !s32i] {
///     cir.yield
///   }
///   cir.case equal, [#cir.int<1> : !s32i] {
///     cir.yield
///   }
///   cir.case equal, [#cir.int<2> : !s32i] {
///     cir.break
///   }
///
/// After applying SimplifySwitch:
///   cir.case anyof, [#cir.int<0> : !s32i, #cir.int<1> : !s32i, #cir.int<2> :
///   !s32i] {
///     cir.break
///   }
struct SimplifySwitch : public OpRewritePattern<SwitchOp> {
  using OpRewritePattern<SwitchOp>::OpRewritePattern;
  LogicalResult matchAndRewrite(SwitchOp op,
                                PatternRewriter &rewriter) const override {

    LogicalResult changed = mlir::failure();
    SmallVector<CaseOp, 8> cases;
    SmallVector<CaseOp, 4> cascadingCases;
    SmallVector<mlir::Attribute, 4> cascadingCaseValues;

    op.collectCases(cases);
    if (cases.empty())
      return mlir::failure();

    auto resetMergeState = [&]() {
      cascadingCases.clear();
      cascadingCaseValues.clear();
    };

    auto flushMergedOps = [&]() {
      for (CaseOp &c : cascadingCases)
        rewriter.eraseOp(c);
      resetMergeState();
    };

    auto mergeCascadingInto = [&](CaseOp &target) {
      rewriter.modifyOpInPlace(target, [&]() {
        target.setValueAttr(rewriter.getArrayAttr(cascadingCaseValues));
        target.setKind(CaseOpKind::Anyof);
      });
      changed = mlir::success();
    };

    // Merge all pending cascading cases into the last one collected, which
    // survives as a distinct `cir.case`; the rest are erased since their
    // values have been folded into it.
    auto mergeLastCascadingAndFlush = [&]() {
      assert(!cir::MissingFeatures::foldRangeCase());
      CaseOp lastCascadingCase = cascadingCases.back();
      mergeCascadingInto(lastCascadingCase);
      cascadingCases.pop_back();
      flushMergedOps();
    };

    for (CaseOp c : cases) {
      // Cascading cases must be textually adjacent to the previously
      // collected cascading case. This is false when something is in the
      // way (e.g. a goto) or when the previous cascading case was found
      // nested inside a sibling case's body (e.g. a case label that falls
      // through into a compound statement) rather than next to `c`.
      bool isAdjacentToLastCascadingCase =
          !cascadingCases.empty() &&
          c->getPrevNode() == cascadingCases.back().getOperation();

      if (!cascadingCases.empty() && !isAdjacentToLastCascadingCase) {
        if (cascadingCases.size() > 1)
          mergeLastCascadingAndFlush();
        else
          resetMergeState();
      }

      cir::CaseOpKind kind = c.getKind();
      if (kind == cir::CaseOpKind::Equal &&
          isa<YieldOp>(c.getCaseRegion().front().front())) {
        // If the case contains only a YieldOp, collect it for cascading merge
        cascadingCases.push_back(c);
        cascadingCaseValues.push_back(c.getValue()[0]);
      } else if (kind == cir::CaseOpKind::Equal && !cascadingCases.empty()) {
        // merge previously collected cascading cases
        cascadingCaseValues.push_back(c.getValue()[0]);
        mergeCascadingInto(c);
        flushMergedOps();
      } else if (kind != cir::CaseOpKind::Equal && cascadingCases.size() > 1) {
        // If a Default, Anyof or Range case is found and there are previous
        // cascading cases, merge all of them into the last cascading case.
        // We don't currently fold case range statements with other case
        // statements.
        mergeLastCascadingAndFlush();
      } else {
        resetMergeState();
      }
    }

    // Edge case: all cases are simple cascading cases
    if (cascadingCases.size() == cases.size())
      mergeLastCascadingAndFlush();

    return changed;
  }
};

struct SimplifyVecSplat : public OpRewritePattern<VecSplatOp> {
  using OpRewritePattern<VecSplatOp>::OpRewritePattern;
  LogicalResult matchAndRewrite(VecSplatOp op,
                                PatternRewriter &rewriter) const override {
    mlir::Value splatValue = op.getValue();
    auto constant = splatValue.getDefiningOp<cir::ConstantOp>();
    if (!constant)
      return mlir::failure();

    auto value = constant.getValue();
    if (!mlir::isa_and_nonnull<cir::IntAttr>(value) &&
        !mlir::isa_and_nonnull<cir::FPAttr>(value))
      return mlir::failure();

    // FIXME(CIR): We should consider making a matrix constant attribute so that
    // we can simplify it here too.
    assert(!MissingFeatures::matrixType());
    auto resultType = mlir::dyn_cast<cir::VectorType>(op.getResult().getType());
    if (!resultType || resultType.isScalable())
      return mlir::failure();
    SmallVector<mlir::Attribute, 16> elements(resultType.getFixedSize(), value);
    auto constVecAttr = cir::ConstVectorAttr::get(
        resultType, mlir::ArrayAttr::get(getContext(), elements));

    rewriter.replaceOpWithNewOp<cir::ConstantOp>(op, constVecAttr);
    return mlir::success();
  }
};

//===----------------------------------------------------------------------===//
// CIRSimplifyPass
//===----------------------------------------------------------------------===//

struct CIRSimplifyPass : public impl::CIRSimplifyBase<CIRSimplifyPass> {
  using CIRSimplifyBase::CIRSimplifyBase;

  void runOnOperation() override;

private:
  void runSimplifyMemoryPatterns(llvm::SmallVectorImpl<Operation *> &loadOps,
                                 llvm::SmallVectorImpl<Operation *> &copyOps);
};

void populateMergeCleanupPatterns(RewritePatternSet &patterns,
                                  RewriteDominanceInfo &domInfo) {
  // clang-format off
  patterns.add<
    SimplifyTernary,
    SimplifySelect,
    SimplifySwitch,
    SimplifyVecSplat
  >(patterns.getContext());
  // clang-format on
  patterns.add<SimplifyConstantLoad>(patterns.getContext(), domInfo);
}

void CIRSimplifyPass::runOnOperation() {
  // Collect rewrite patterns. SimplifyConstantLoad queries dominance, so the
  // dominance info doubles as the driver's listener to stay coherent.
  RewriteDominanceInfo domInfo;
  RewritePatternSet patterns(&getContext());
  populateMergeCleanupPatterns(patterns, domInfo);

  // Collect operations to apply patterns. All op lists come from one
  // walk: the first-phase patterns create no LoadOps/CopyOps (Select/Not/
  // Constant only) and erase none, so the load/copy sets are the same
  // before and after they run.
  llvm::SmallVector<Operation *, 16> ops;
  llvm::SmallVector<Operation *, 16> loadOps;
  llvm::SmallVector<Operation *, 16> copyOps;
  getOperation()->walk([&](Operation *op) {
    if (isa<TernaryOp, SelectOp, SwitchOp, VecSplatOp, LoadOp>(op))
      ops.push_back(op);
    else if (isa<LoadOp>(op))
      loadOps.push_back(op);
    else if (isa<CopyOp>(op))
      copyOps.push_back(op);
  });

  // Apply patterns. Scope the rewrite to the body region, which encloses
  // the common ancestor the driver would otherwise search for.
  GreedyRewriteConfig config;
  config.setScope(&getOperation()->getRegion(0));
  if (applyOpPatternsGreedily(ops, std::move(patterns), config).failed())
    signalPassFailure();

  // SimplifyConstantLoad and SimplifyStoreCopyForward need to query dominance
  // information, which could be invalidated by other rewrite patterns. Thus
  // we run them separately after other patterns have been applied.
  runSimplifyMemoryPatterns(loadOps, copyOps);
}

void CIRSimplifyPass::runSimplifyMemoryPatterns(
    llvm::SmallVectorImpl<Operation *> &loadOps,
    llvm::SmallVectorImpl<Operation *> &copyOps) {
  RewritePatternSet patterns(&getContext());
  // One shared DominanceInfo for both patterns: it caches per-region
  // dominator trees, and separate instances would recompute the same
  // trees for every large function.
  RewriteDominanceInfo domInfo;
  patterns.add<SimplifyConstantLoad>(patterns.getContext(), domInfo);
  patterns.add<SimplifyStoreCopyForward>(patterns.getContext(),
                                         domInfo.getDominanceInfo(),
                                         scalarizeMaxDepth);

  // Both patterns only replace single ops in place (no CFG changes), so the
  // dominance queries stay valid across the whole run. Scope the rewrite to
  // the body region, which encloses the common ancestor the driver would
  // otherwise search for.
  GreedyRewriteConfig config;
  config.setScope(&getOperation()->getRegion(0));
  llvm::SmallVector<Operation *, 16> roots(loadOps.begin(), loadOps.end());
  roots.append(copyOps.begin(), copyOps.end());
  if (applyOpPatternsGreedily(roots, std::move(patterns), config).failed())
    signalPassFailure();
}

} // namespace

std::unique_ptr<Pass> mlir::createCIRSimplifyPass() {
  return std::make_unique<CIRSimplifyPass>();
}

std::unique_ptr<Pass> mlir::createCIRSimplifyPass(unsigned scalarizeMaxDepth) {
  CIRSimplifyOptions options;
  options.scalarizeMaxDepth = scalarizeMaxDepth;
  return std::make_unique<CIRSimplifyPass>(options);
}
