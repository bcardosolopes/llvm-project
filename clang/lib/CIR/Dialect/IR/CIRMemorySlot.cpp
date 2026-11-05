//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements MemorySlot-related interfaces for CIR dialect
// operations.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"

using namespace mlir;

/// Conditions the deletion of the operation to the removal of all its uses.
static bool forwardToUsers(Operation *op,
                           SmallVectorImpl<OpOperand *> &newBlockingUses) {
  for (Value result : op->getResults())
    for (OpOperand &use : result.getUses())
      newBlockingUses.push_back(&use);
  return true;
}

/// Looks through bitcast chains to find the root value. If \p val is the
/// result of a chain of CastOp bitcasts, returns the ultimate source operand;
/// otherwise returns \p val unchanged.
static Value lookThroughBitcasts(Value val) {
  while (auto castOp = val.getDefiningOp<cir::CastOp>()) {
    if (castOp.getKind() != cir::CastKind::bitcast)
      break;
    val = castOp.getSrc();
  }
  return val;
}

//===----------------------------------------------------------------------===//
// Interfaces for AllocaOp
//===----------------------------------------------------------------------===//

/// Returns true if this alloca is the destination of a store whose value comes
/// (possibly through value-shaping casts) from the result of a `cir.asm.goto`
/// (an `asm goto` with register outputs). Such a slot must not be promoted by
/// the CIR mem2reg pass: at this point the asm result is stored into the slot
/// only on the fallthrough edge, while the stores on the indirect (label) edges
/// are materialized later, in split blocks, during LLVM-IR translation
/// (`convertInlineAsmGotoOp`). Promoting the slot now would compute the reaching
/// definition on an indirect edge from the pre-asm value rather than the asm
/// output, dropping the output on the branch-taken edge. Leaving the slot in
/// memory lets LLVM's own mem2reg promote it after the split-block stores exist,
/// matching classic codegen. (Multi-output asm goto reaches its output slots
/// through a temp alloca / `cir.get_member` chain that is already unpromotable,
/// so this only needs to catch the direct single-output store.)
static bool isAsmGotoOutputSlot(cir::AllocaOp alloca) {
  mlir::Value slotPtr = alloca.getResult();
  for (mlir::Operation *user : slotPtr.getUsers()) {
    auto storeOp = mlir::dyn_cast<cir::StoreOp>(user);
    if (!storeOp || storeOp.getAddr() != slotPtr)
      continue;
    mlir::Value stored = storeOp.getValue();
    while (auto castOp = stored.getDefiningOp<cir::CastOp>())
      stored = castOp.getSrc();
    if (auto asmGoto = stored.getDefiningOp<cir::InlineAsmGotoOp>())
      if (asmGoto->getNumSuccessors() > 1)
        return true;
  }
  return false;
}

/// Returns true if the type is, or transitively contains (by value), a
/// union record. Pointer elements are opaque: only inline storage can carry
/// another member's live data in padding.
static bool typeContainsUnion(mlir::Type ty,
                              llvm::SmallPtrSetImpl<mlir::Type> &seen) {
  if (!seen.insert(ty).second)
    return false;
  if (auto recordTy = mlir::dyn_cast<cir::RecordType>(ty)) {
    // Incomplete records have no visible members; bail out rather than
    // risk promoting a slot whose layout is still unknown.
    if (recordTy.isIncomplete() || recordTy.isUnion())
      return true;
    for (mlir::Type member : recordTy.getMembers())
      if (typeContainsUnion(member, seen))
        return true;
    return false;
  }
  if (auto arrayTy = mlir::dyn_cast<cir::ArrayType>(ty))
    return typeContainsUnion(arrayTy.getElementType(), seen);
  return false;
}

/// Returns true if this alloca holds a record that is, or contains, a union.
/// Such a slot must not be promoted by the CIR mem2reg pass: promotion
/// introduces whole-record `cir.load`/`cir.store` SSA values, which lower to
/// LLVM `load`/`store` of the union-as-first-member type. LLVM load/store do
/// not preserve padding bytes, but a union member's live data can sit in
/// another member's padding (e.g. a bitfield word packed next to a
/// pointer-sized member), so the round trip silently drops it. Leaving the
/// slot in memory keeps byte-wise `cir.copy` (memcpy) semantics; LLVM's own
/// passes still optimize the lowered code.
static bool isUnionRecordSlot(cir::AllocaOp alloca) {
  llvm::SmallPtrSet<mlir::Type, 8> seen;
  return typeContainsUnion(alloca.getAllocaType(), seen);
}

llvm::SmallVector<MemorySlot> cir::AllocaOp::getPromotableSlots() {
  if (isAsmGotoOutputSlot(*this))
    return {};
  if (isUnionRecordSlot(*this))
    return {};
  return {MemorySlot{getResult(), getAllocaType()}};
}

Value cir::AllocaOp::getDefaultValue(const MemorySlot &slot,
                                     OpBuilder &builder) {
  return cir::ConstantOp::create(builder, getLoc(),
                                 cir::UndefAttr::get(slot.elemType));
}

void cir::AllocaOp::handleBlockArgument(const MemorySlot &slot,
                                        BlockArgument argument,
                                        OpBuilder &builder) {}

std::optional<PromotableAllocationOpInterface>
cir::AllocaOp::handlePromotionComplete(const MemorySlot &slot,
                                       Value defaultValue, OpBuilder &builder) {
  if (defaultValue && defaultValue.use_empty())
    defaultValue.getDefiningOp()->erase();
  this->erase();
  return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Interfaces for LoadOp
//===----------------------------------------------------------------------===//

bool cir::LoadOp::loadsFrom(const MemorySlot &slot) {
  return getAddr() == slot.ptr;
}

bool cir::LoadOp::storesTo(const MemorySlot &slot) { return false; }

Value cir::LoadOp::getStored(const MemorySlot &slot, OpBuilder &builder,
                             Value reachingDef, const DataLayout &dataLayout) {
  llvm_unreachable("getStored should not be called on LoadOp");
}

bool cir::LoadOp::canUsesBeRemoved(
    const MemorySlot &slot, const SmallPtrSetImpl<OpOperand *> &blockingUses,
    SmallVectorImpl<OpOperand *> &newBlockingUses,
    const DataLayout &dataLayout) {
  // A volatile load is an externally observable access that must survive
  // mem2reg, so it cannot be removed (this also blocks promotion of the slot).
  // Matches LLVM's isAllocaPromotable, which refuses any volatile user.
  if (getIsVolatile())
    return false;
  if (blockingUses.size() != 1)
    return false;
  Value blockingUse = (*blockingUses.begin())->get();
  return blockingUse == slot.ptr && getAddr() == slot.ptr &&
         getType() == slot.elemType;
}

DeletionKind cir::LoadOp::removeBlockingUses(
    const MemorySlot &slot, const SmallPtrSetImpl<OpOperand *> &blockingUses,
    OpBuilder &builder, Value reachingDefinition,
    const DataLayout &dataLayout) {
  getResult().replaceAllUsesWith(reachingDefinition);
  return DeletionKind::Delete;
}

void cir::LoadOp::getEffects(
    SmallVectorImpl<SideEffects::EffectInstance<MemoryEffects::Effect>>
        &effects) {
  // All loads read from memory.
  effects.emplace_back(MemoryEffects::Read::get(), &getAddrMutable(),
                       SideEffects::DefaultResource::get());
  // Volatile loads are externally observable side effects that must not be
  // removed even if their result is unused. Model this by adding a write
  // effect, which prevents DCE from eliminating the operation.
  if (getIsVolatile())
    effects.emplace_back(MemoryEffects::Write::get(),
                         SideEffects::DefaultResource::get());
}

//===----------------------------------------------------------------------===//
// Interfaces for StoreOp
//===----------------------------------------------------------------------===//

bool cir::StoreOp::loadsFrom(const MemorySlot &slot) { return false; }

bool cir::StoreOp::storesTo(const MemorySlot &slot) {
  return getAddr() == slot.ptr;
}

Value cir::StoreOp::getStored(const MemorySlot &slot, OpBuilder &builder,
                              Value reachingDef, const DataLayout &dataLayout) {
  return getValue();
}

bool cir::StoreOp::canUsesBeRemoved(
    const MemorySlot &slot, const SmallPtrSetImpl<OpOperand *> &blockingUses,
    SmallVectorImpl<OpOperand *> &newBlockingUses,
    const DataLayout &dataLayout) {
  // A volatile store is an externally observable access that must survive
  // mem2reg, so it cannot be removed (this also blocks promotion of the slot).
  // Matches LLVM's isAllocaPromotable, which refuses any volatile user.
  if (getIsVolatile())
    return false;
  if (blockingUses.size() != 1)
    return false;
  Value blockingUse = (*blockingUses.begin())->get();
  return blockingUse == slot.ptr && getAddr() == slot.ptr &&
         getValue() != slot.ptr && slot.elemType == getValue().getType();
}

DeletionKind cir::StoreOp::removeBlockingUses(
    const MemorySlot &slot, const SmallPtrSetImpl<OpOperand *> &blockingUses,
    OpBuilder &builder, Value reachingDefinition,
    const DataLayout &dataLayout) {
  return DeletionKind::Delete;
}

//===----------------------------------------------------------------------===//
// Interfaces for CopyOp
//===----------------------------------------------------------------------===//

bool cir::CopyOp::loadsFrom(const MemorySlot &slot) {
  return lookThroughBitcasts(getSrc()) == slot.ptr;
}

bool cir::CopyOp::storesTo(const MemorySlot &slot) {
  return lookThroughBitcasts(getDst()) == slot.ptr;
}

Value cir::CopyOp::getStored(const MemorySlot &slot, OpBuilder &builder,
                             Value reachingDef, const DataLayout &dataLayout) {
  // Determine the element type of the source pointer.
  auto srcPtrTy = mlir::cast<cir::PointerType>(getSrc().getType());
  mlir::Type srcElemTy = srcPtrTy.getPointee();

  // Load from the source using its native element type.
  Value loaded = cir::LoadOp::create(builder, getLoc(), srcElemTy, getSrc(),
                                     /*isDeref=*/false, /*is_volatile=*/false,
                                     /*is_nontemporal=*/false,
                                     /*alignment=*/getSrcAlignmentAttr(),
                                     /*sync_scope=*/cir::SyncScopeKindAttr(),
                                     /*mem_order=*/cir::MemOrderAttr(),
                                     /*tbaa=*/cir::TBAAAttr{});

  // If the source element type differs from the slot element type (e.g. copy
  // through a bitcast), insert a bitcast to convert to the slot's type.
  if (srcElemTy != slot.elemType)
    loaded = cir::CastOp::create(builder, getLoc(), slot.elemType,
                                 cir::CastKind::bitcast, loaded);
  return loaded;
}

DeletionKind cir::CopyOp::removeBlockingUses(
    const MemorySlot &slot, const SmallPtrSetImpl<OpOperand *> &blockingUses,
    OpBuilder &builder, mlir::Value reachingDefinition,
    const DataLayout &dataLayout) {
  if (loadsFrom(slot)) {
    auto dstPtrTy = mlir::cast<cir::PointerType>(getDst().getType());
    mlir::Type dstElemTy = dstPtrTy.getPointee();

    // If the destination element type differs from the reaching definition's
    // type (e.g. copy through a bitcast), insert a bitcast to convert.
    Value valToStore = reachingDefinition;
    if (dstElemTy != reachingDefinition.getType())
      valToStore = cir::CastOp::create(builder, getLoc(), dstElemTy,
                                       cir::CastKind::bitcast, valToStore);

    cir::StoreOp::create(builder, getLoc(), valToStore, getDst(),
                         /*isVolatile=*/false,
                         /*is_nontemporal=*/false,
                         /*alignment=*/getDstAlignmentAttr(),
                         /*sync_scope=*/cir::SyncScopeKindAttr(),
                         /*mem-order=*/cir::MemOrderAttr(),
                         /*tbaa=*/cir::TBAAAttr{});
  }
  return DeletionKind::Delete;
}

bool cir::CopyOp::canUsesBeRemoved(
    const MemorySlot &slot, const SmallPtrSetImpl<OpOperand *> &blockingUses,
    SmallVectorImpl<OpOperand *> &newBlockingUses,
    const DataLayout &dataLayout) {
  // A volatile copy is an externally observable access that must survive
  // mem2reg, so it cannot be removed (this also blocks promotion of the slot).
  // Matches LoadOp/StoreOp above and LLVM's isAllocaPromotable, which refuses
  // any volatile user.
  if (getIsVolatile())
    return false;

  if (getDst() == getSrc())
    return false;

  // Only a copy of the slot's own type; like LLVM's mem2reg, leave a
  // type-punned slot in memory. A copy through a bitcast to another type is
  // not a direct user of the slot: only mem2reg's followPromotableChains mode,
  // which CIR's lowering turns on, counts its block as defining the slot (run
  // without it, a read past a merge sees the slot's undef default -- a bitfield
  // struct initialized through its constant's layout is such a copy).
  // Promoting it would need a record-to-record value bitcast, which the
  // lowering does through an alloca anyway.
  if (mlir::cast<cir::PointerType>(getDst().getType()).getPointee() !=
      slot.elemType)
    return false;

  return getLength(dataLayout) == dataLayout.getTypeSize(slot.elemType);
}

//===----------------------------------------------------------------------===//
// Interfaces for CastOp
//===----------------------------------------------------------------------===//

bool cir::CastOp::canUsesBeRemoved(
    const SmallPtrSetImpl<OpOperand *> &blockingUses,
    SmallVectorImpl<OpOperand *> &newBlockingUses,
    const DataLayout &dataLayout) {
  if (getKind() == cir::CastKind::bitcast)
    return forwardToUsers(*this, newBlockingUses);
  return false;
}

DeletionKind cir::CastOp::removeBlockingUses(
    const SmallPtrSetImpl<OpOperand *> &blockingUses, OpBuilder &builder) {
  return DeletionKind::Delete;
}

//===----------------------------------------------------------------------===//
// Interfaces for LifetimeStartOp and LifetimeEndOp
//===----------------------------------------------------------------------===//

// A promoted slot is no longer on the stack, so its lifetime markers mean
// nothing and go with it. Without this, every local CIRGen marks -- all of
// them at -O1 and above -- blocks its own promotion.

bool cir::LifetimeStartOp::canUsesBeRemoved(
    const SmallPtrSetImpl<OpOperand *> &blockingUses,
    SmallVectorImpl<OpOperand *> &newBlockingUses,
    const DataLayout &dataLayout) {
  return true;
}

DeletionKind cir::LifetimeStartOp::removeBlockingUses(
    const SmallPtrSetImpl<OpOperand *> &blockingUses, OpBuilder &builder) {
  return DeletionKind::Delete;
}

bool cir::LifetimeEndOp::canUsesBeRemoved(
    const SmallPtrSetImpl<OpOperand *> &blockingUses,
    SmallVectorImpl<OpOperand *> &newBlockingUses,
    const DataLayout &dataLayout) {
  return true;
}

DeletionKind cir::LifetimeEndOp::removeBlockingUses(
    const SmallPtrSetImpl<OpOperand *> &blockingUses, OpBuilder &builder) {
  return DeletionKind::Delete;
}
