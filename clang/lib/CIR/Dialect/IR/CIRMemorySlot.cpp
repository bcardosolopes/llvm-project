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
  if (getDst() == getSrc())
    return false;

  // A volatile copy is an externally observable access that must survive
  // mem2reg, so it cannot be removed (this also blocks promotion of the
  // slot). Matches LoadOp/StoreOp above and LLVM's isAllocaPromotable,
  // which refuses any volatile user.
  if (getIsVolatile())
    return false;

  // The copy must reference the slot directly. mem2reg's merge analysis
  // only considers direct users of the slot pointer as defining blocks, so
  // a copy through a bitcast would have its value computed but dropped at
  // merges (like LLVM's mem2reg, leave a type-punned slot in memory).
  if (getDst() != slot.ptr && getSrc() != slot.ptr)
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
// Interfaces for LifetimeStartOp and LifetimeEndOp
//===----------------------------------------------------------------------===//

// A promoted slot is no longer on the stack, so its lifetime markers mean
// nothing and go with it. Without this, every local CIRGen marks blocks its
// own promotion.
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

//===----------------------------------------------------------------------===//
// Interfaces for GetMemberOp
//===----------------------------------------------------------------------===//

bool cir::GetMemberOp::canUsesBeRemoved(
    const SmallPtrSetImpl<OpOperand *> &blockingUses,
    SmallVectorImpl<OpOperand *> &newBlockingUses,
    const DataLayout &dataLayout) {
  return forwardToUsers(*this, newBlockingUses);
}

DeletionKind cir::GetMemberOp::removeBlockingUses(
    const SmallPtrSetImpl<OpOperand *> &blockingUses, OpBuilder &builder) {
  return DeletionKind::Delete;
}

//===----------------------------------------------------------------------===//
// PromotableAliaserInterface model for GetMemberOp
//===----------------------------------------------------------------------===//

/// Exposes a struct member access as an alias of the enclosing record slot.
/// `projectSlotValueToAliasValue` extracts the member for loads, and
/// `projectAliasValueToSlotValue` inserts a stored member back into the
/// reaching definition, preserving the other members (and padding).
/// Unions and bit-fields expose no alias: extract/insert member do not work
/// on them.
struct GetMemberOpAliasModel
    : public PromotableAliaserInterface::ExternalModel<GetMemberOpAliasModel,
                                                       cir::GetMemberOp> {
  void
  getPromotableSlotAliases(Operation *op, OpOperand &aliasedSlotPointerOperand,
                           const MemorySlot &parentSlot,
                           SmallVectorImpl<MemorySlot> &newMemorySlots) const {
    auto getMember = cast<cir::GetMemberOp>(op);
    if (aliasedSlotPointerOperand.get() != getMember.getAddr())
      return;
    auto recordTy = dyn_cast<cir::StructType>(parentSlot.valueType);
    if (!recordTy)
      return;
    auto addrPtrTy = dyn_cast<cir::PointerType>(getMember.getAddr().getType());
    if (!addrPtrTy || addrPtrTy.getPointee() != parentSlot.valueType)
      return;
    uint64_t index = static_cast<uint64_t>(getMember.getIndex());
    if (index >= recordTy.getMembers().size())
      return;
    mlir::Type memberTy = recordTy.getElementType(index);
    if (isa<cir::BitFieldType>(memberTy))
      return;
    auto resultPtrTy =
        dyn_cast<cir::PointerType>(getMember.getResult().getType());
    if (!resultPtrTy || resultPtrTy.getPointee() != memberTy)
      return;
    newMemorySlots.push_back(MemorySlot{getMember.getResult(), memberTy});
  }

  Value projectSlotValueToAliasValue(Operation *op,
                                     OpOperand &aliasedSlotPointerOperand,
                                     const MemorySlot &parentSlot,
                                     const MemorySlot &aliasSlot,
                                     Value slotValue,
                                     OpBuilder &builder) const {
    auto getMember = cast<cir::GetMemberOp>(op);
    return cir::ExtractMemberOp::create(
        builder, getMember.getLoc(), aliasSlot.valueType, slotValue,
        static_cast<uint64_t>(getMember.getIndex()));
  }

  Value projectAliasValueToSlotValue(Operation *op,
                                     OpOperand &aliasedSlotPointerOperand,
                                     const MemorySlot &parentSlot,
                                     const MemorySlot &aliasSlot,
                                     Value aliasValue, Value reachingDef,
                                     OpBuilder &builder) const {
    auto getMember = cast<cir::GetMemberOp>(op);
    return cir::InsertMemberOp::create(
        builder, getMember.getLoc(), reachingDef,
        static_cast<uint64_t>(getMember.getIndex()), aliasValue);
  }
};

void cir::registerMemorySlotInterfacesExternalModels(
    mlir::DialectRegistry &registry) {
  registry.addExtension(+[](mlir::MLIRContext *ctx, cir::CIRDialect *dialect) {
    cir::GetMemberOp::attachInterface<GetMemberOpAliasModel>(*ctx);
  });
}
