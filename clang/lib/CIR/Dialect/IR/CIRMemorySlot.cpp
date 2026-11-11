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

//===----------------------------------------------------------------------===//
// Interfaces for AllocaOp
//===----------------------------------------------------------------------===//

llvm::SmallVector<MemorySlot> cir::AllocaOp::getPromotableSlots() {
  return {MemorySlot{getResult(), getAllocaType()}};
}

Value cir::AllocaOp::getDefaultValue(const MemorySlot &slot,
                                     OpBuilder &builder) {
  return cir::ConstantOp::create(builder, getLoc(),
                                 cir::UndefAttr::get(slot.valueType));
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
  if (blockingUses.size() != 1)
    return false;

  // Volatile load or atomic load should not be removed.
  if (getIsVolatile() || getMemOrder().has_value())
    return false;

  Value blockingUse = (*blockingUses.begin())->get();
  return blockingUse == slot.ptr && getAddr() == slot.ptr &&
         getType() == slot.valueType;
}

DeletionKind cir::LoadOp::removeBlockingUses(
    const MemorySlot &slot, const SmallPtrSetImpl<OpOperand *> &blockingUses,
    OpBuilder &builder, Value reachingDefinition,
    const DataLayout &dataLayout) {
  getResult().replaceAllUsesWith(reachingDefinition);
  return DeletionKind::Delete;
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
  if (blockingUses.size() != 1)
    return false;

  // Volatile store or atomic store should not be removed.
  if (getIsVolatile() || getMemOrder().has_value())
    return false;

  Value blockingUse = (*blockingUses.begin())->get();
  return blockingUse == slot.ptr && getAddr() == slot.ptr &&
         getValue() != slot.ptr && slot.valueType == getValue().getType();
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
  return getSrc() == slot.ptr;
}

bool cir::CopyOp::storesTo(const MemorySlot &slot) {
  return getDst() == slot.ptr;
}

Value cir::CopyOp::getStored(const MemorySlot &slot, OpBuilder &builder,
                             Value reachingDef, const DataLayout &dataLayout) {
  return cir::LoadOp::create(builder, getLoc(), slot.valueType, getSrc());
}

DeletionKind cir::CopyOp::removeBlockingUses(
    const MemorySlot &slot, const SmallPtrSetImpl<OpOperand *> &blockingUses,
    OpBuilder &builder, mlir::Value reachingDefinition,
    const DataLayout &dataLayout) {
  if (loadsFrom(slot))
    cir::StoreOp::create(builder, getLoc(), reachingDefinition, getDst(),
                         /*is_volatile=*/false,
                         /*is_nontemporal=*/false,
                         /*alignment=*/mlir::IntegerAttr{},
                         /*sync_scope=*/cir::SyncScopeKindAttr(),
                         /*mem-order=*/cir::MemOrderAttr());
  return DeletionKind::Delete;
}

bool cir::CopyOp::canUsesBeRemoved(
    const MemorySlot &slot, const SmallPtrSetImpl<OpOperand *> &blockingUses,
    SmallVectorImpl<OpOperand *> &newBlockingUses,
    const DataLayout &dataLayout) {
  // Like loads and stores, a copy is promotable only when exactly one
  // operand names the slot: a copy between the slot and a pointer
  // derived from it that no alias exposes would otherwise be removed
  // while the removal still uses the derived pointer.
  if (blockingUses.size() != 1 || (*blockingUses.begin())->get() != slot.ptr)
    return false;

  // Volatile copies must stay in memory, like volatile loads and stores.
  if (getIsVolatile())
    return false;

  return getCopySizeInBytes(dataLayout) ==
         dataLayout.getTypeSize(slot.valueType);
}

//===----------------------------------------------------------------------===//
// Interfaces for MatrixColumnMajorLoadOp
//===----------------------------------------------------------------------===//

bool cir::MatrixColumnMajorLoadOp::loadsFrom(const MemorySlot &slot) {
  return getValue() == slot.ptr;
}

bool cir::MatrixColumnMajorLoadOp::storesTo(const MemorySlot &slot) {
  return false;
}

Value cir::MatrixColumnMajorLoadOp::getStored(const MemorySlot &slot,
                                              OpBuilder &builder,
                                              Value reachingDef,
                                              const DataLayout &dataLayout) {
  llvm_unreachable("getStored should not be called on MatrixColumnMajorLoadOp");
}

bool cir::MatrixColumnMajorLoadOp::canUsesBeRemoved(
    const MemorySlot &slot, const SmallPtrSetImpl<OpOperand *> &blockingUses,
    SmallVectorImpl<OpOperand *> &newBlockingUses,
    const DataLayout &dataLayout) {
  if (blockingUses.size() != 1)
    return false;

  // Volatile load should not be removed.
  if (getIsVolatile())
    return false;

  Value blockingUse = (*blockingUses.begin())->get();
  return blockingUse == slot.ptr && getValue() == slot.ptr &&
         getType() == slot.valueType;
}

DeletionKind cir::MatrixColumnMajorLoadOp::removeBlockingUses(
    const MemorySlot &slot, const SmallPtrSetImpl<OpOperand *> &blockingUses,
    OpBuilder &builder, Value reachingDefinition,
    const DataLayout &dataLayout) {
  getResult().replaceAllUsesWith(reachingDefinition);
  return DeletionKind::Delete;
}

//===----------------------------------------------------------------------===//
// Interfaces for MatrixColumnMajorStoreOp
//===----------------------------------------------------------------------===//

bool cir::MatrixColumnMajorStoreOp::loadsFrom(const MemorySlot &slot) {
  return false;
}

bool cir::MatrixColumnMajorStoreOp::storesTo(const MemorySlot &slot) {
  return getValue() == slot.ptr;
}

Value cir::MatrixColumnMajorStoreOp::getStored(const MemorySlot &slot,
                                               OpBuilder &builder,
                                               Value reachingDef,
                                               const DataLayout &dataLayout) {
  return getMatrix();
}

bool cir::MatrixColumnMajorStoreOp::canUsesBeRemoved(
    const MemorySlot &slot, const SmallPtrSetImpl<OpOperand *> &blockingUses,
    SmallVectorImpl<OpOperand *> &newBlockingUses,
    const DataLayout &dataLayout) {
  if (blockingUses.size() != 1)
    return false;

  // Volatile store should not be removed.
  if (getIsVolatile())
    return false;

  Value blockingUse = (*blockingUses.begin())->get();
  return blockingUse == slot.ptr && getValue() == slot.ptr &&
         getValue() != slot.ptr && slot.valueType == getValue().getType();
}

DeletionKind cir::MatrixColumnMajorStoreOp::removeBlockingUses(
    const MemorySlot &slot, const SmallPtrSetImpl<OpOperand *> &blockingUses,
    OpBuilder &builder, Value reachingDefinition,
    const DataLayout &dataLayout) {
  return DeletionKind::Delete;
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
// Interfaces for GetMemberOp
//===----------------------------------------------------------------------===//

// Returns the type of the member this `get_member` addresses when it can be
// exposed as a promotable alias of its enclosing slot: a plain data member
// of a struct, addressed in bounds, whose result points at the member type.
// Only data members alias. Unions are not structs; pad members hold no
// value; empty members can share storage with other members; and a
// bit-field member points at a storage unit shared with sibling
// bit-fields, so none of them can be projected as an independent SSA
// member value.
static std::optional<mlir::Type>
getPromotableMemberType(cir::GetMemberOp getMember) {
  auto addrPtrTy = dyn_cast<cir::PointerType>(getMember.getAddr().getType());
  if (!addrPtrTy)
    return std::nullopt;
  auto recordTy = dyn_cast<cir::StructType>(addrPtrTy.getPointee());
  if (!recordTy)
    return std::nullopt;
  uint64_t index = static_cast<uint64_t>(getMember.getIndex());
  if (index >= recordTy.getMembers().size())
    return std::nullopt;
  llvm::ArrayRef<cir::RecordMemberKind> kinds = recordTy.getMemberKinds();
  if (index >= kinds.size() || kinds[index] != cir::RecordMemberKind::Data)
    return std::nullopt;
  mlir::Type memberTy = recordTy.getElementType(index);
  auto resultPtrTy =
      dyn_cast<cir::PointerType>(getMember.getResult().getType());
  if (!resultPtrTy || resultPtrTy.getPointee() != memberTy)
    return std::nullopt;
  return memberTy;
}

bool cir::GetMemberOp::canUsesBeRemoved(
    const SmallPtrSetImpl<OpOperand *> &blockingUses,
    SmallVectorImpl<OpOperand *> &newBlockingUses,
    const DataLayout &dataLayout) {
  // Only forward uses when the op exposes a promotable alias. Forwarding a
  // refused alias lets Mem2Reg judge downstream memory ops against the
  // root slot and delete copies it should keep.
  if (!getPromotableMemberType(*this))
    return false;
  return forwardToUsers(*this, newBlockingUses);
}

DeletionKind cir::GetMemberOp::removeBlockingUses(
    const SmallPtrSetImpl<OpOperand *> &blockingUses, OpBuilder &builder) {
  return DeletionKind::Delete;
}

void cir::GetMemberOp::getPromotableSlotAliases(
    mlir::OpOperand &aliasedSlotPointerOperand,
    const mlir::MemorySlot &parentSlot,
    llvm::SmallVectorImpl<mlir::MemorySlot> &newMemorySlots) {
  if (aliasedSlotPointerOperand.get() != getAddr())
    return;
  std::optional<mlir::Type> memberTy = getPromotableMemberType(*this);
  if (!memberTy)
    return;
  auto addrPtrTy = cast<cir::PointerType>(getAddr().getType());
  if (addrPtrTy.getPointee() != parentSlot.valueType)
    return;
  newMemorySlots.push_back(MemorySlot{getResult(), *memberTy});
}

mlir::Value cir::GetMemberOp::projectSlotValueToAliasValue(
    mlir::OpOperand &aliasedSlotPointerOperand,
    const mlir::MemorySlot &parentSlot, const mlir::MemorySlot &aliasSlot,
    mlir::Value slotValue, mlir::OpBuilder &builder) {
  return cir::ExtractMemberOp::create(builder, getLoc(), aliasSlot.valueType,
                                      slotValue,
                                      static_cast<uint64_t>(getIndex()));
}

mlir::Value cir::GetMemberOp::projectAliasValueToSlotValue(
    mlir::OpOperand &aliasedSlotPointerOperand,
    const mlir::MemorySlot &parentSlot, const mlir::MemorySlot &aliasSlot,
    mlir::Value aliasValue, mlir::Value reachingDef,
    mlir::OpBuilder &builder) {
  return cir::InsertMemberOp::create(builder, getLoc(), reachingDef,
                                     static_cast<uint64_t>(getIndex()),
                                     aliasValue);
}
