//===- LLVMInterfaces.cpp - LLVM Interfaces ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines op interfaces for the LLVM dialect in MLIR.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/LLVMIR/LLVMInterfaces.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

using namespace mlir;
using namespace mlir::LLVM;

/// Verifies that all elements of `array` are instances of `Attr`.
template <class AttrT>
static LogicalResult isArrayOf(Operation *op, ArrayAttr array) {
  for (Attribute iter : array)
    if (!isa<AttrT>(iter))
      return op->emitOpError("expected op to return array of ")
             << AttrT::getMnemonic() << " attributes";
  return success();
}

//===----------------------------------------------------------------------===//
// AccessGroupOpInterface
//===----------------------------------------------------------------------===//

LogicalResult mlir::LLVM::detail::verifyAccessGroupOpInterface(Operation *op) {
  auto iface = cast<AccessGroupOpInterface>(op);
  ArrayAttr accessGroups = iface.getAccessGroupsOrNull();
  if (!accessGroups)
    return success();

  return isArrayOf<AccessGroupAttr>(op, accessGroups);
}

//===----------------------------------------------------------------------===//
// AliasAnalysisOpInterface
//===----------------------------------------------------------------------===//

LogicalResult
mlir::LLVM::detail::verifyAliasAnalysisOpInterface(Operation *op) {
  auto iface = cast<AliasAnalysisOpInterface>(op);

  if (auto aliasScopes = iface.getAliasScopesOrNull())
    if (failed(isArrayOf<AliasScopeAttr>(op, aliasScopes)))
      return failure();

  if (auto noAliasScopes = iface.getNoAliasScopesOrNull())
    if (failed(isArrayOf<AliasScopeAttr>(op, noAliasScopes)))
      return failure();

  ArrayAttr tags = iface.getTBAATagsOrNull();
  if (!tags)
    return success();

  return isArrayOf<TBAATagAttr>(op, tags);
}

//===----------------------------------------------------------------------===//
// DereferenceableOpInterface
//===----------------------------------------------------------------------===//

LogicalResult
mlir::LLVM::detail::verifyDereferenceableOpInterface(Operation *op) {
  auto iface = cast<DereferenceableOpInterface>(op);

  if (auto derefAttr = iface.getDereferenceableOrNull())
    if (op->getNumResults() != 1 ||
        !mlir::isa<LLVMPointerType>(op->getResult(0).getType()))
      return op->emitOpError(
          "expected op to return a single LLVM pointer type");

  return success();
}

SmallVector<Value> mlir::LLVM::AtomicCmpXchgOp::getAccessedOperands() {
  return {getPtr()};
}

SmallVector<Value> mlir::LLVM::AtomicRMWOp::getAccessedOperands() {
  return {getPtr()};
}

SmallVector<Value> mlir::LLVM::LoadOp::getAccessedOperands() {
  return {getAddr()};
}

SmallVector<Value> mlir::LLVM::StoreOp::getAccessedOperands() {
  return {getAddr()};
}

// The annotation intrinsics access the pointer they annotate, and nothing
// else: the annotation string, file name and attribute operands are constants
// clang emits alongside it, not memory the call touches.
SmallVector<Value> mlir::LLVM::VarAnnotation::getAccessedOperands() {
  return {getVal()};
}

SmallVector<Value> mlir::LLVM::PtrAnnotation::getAccessedOperands() {
  return {getPtr()};
}

SmallVector<Value> mlir::LLVM::MemcpyOp::getAccessedOperands() {
  return {getDst(), getSrc()};
}

SmallVector<Value> mlir::LLVM::MemcpyInlineOp::getAccessedOperands() {
  return {getDst(), getSrc()};
}

SmallVector<Value> mlir::LLVM::MemmoveOp::getAccessedOperands() {
  return {getDst(), getSrc()};
}

SmallVector<Value> mlir::LLVM::MemsetOp::getAccessedOperands() {
  return {getDst()};
}

SmallVector<Value> mlir::LLVM::MemsetInlineOp::getAccessedOperands() {
  return {getDst()};
}

SmallVector<Value> mlir::LLVM::LifetimeStartOp::getAccessedOperands() {
  return {getPtr()};
}

SmallVector<Value> mlir::LLVM::LifetimeEndOp::getAccessedOperands() {
  return {getPtr()};
}

SmallVector<Value> mlir::LLVM::InvariantStartOp::getAccessedOperands() {
  return {getPtr()};
}

SmallVector<Value> mlir::LLVM::InvokeOp::getAccessedOperands() {
  return llvm::filter_to_vector(getArgOperands(), [](Value arg) {
    return isa<LLVMPointerType>(arg.getType());
  });
}

SmallVector<Value> mlir::LLVM::CallOp::getAccessedOperands() {
  return llvm::filter_to_vector(getArgOperands(), [](Value arg) {
    return isa<LLVMPointerType>(arg.getType());
  });
}

SmallVector<Value> mlir::LLVM::CallIntrinsicOp::getAccessedOperands() {
  // Same rule as llvm.call: an intrinsic the dialect does not model has no
  // per-argument information beyond the type, so every pointer operand is a
  // candidate.
  return llvm::filter_to_vector(getArgs(), [](Value arg) {
    return isa<LLVMPointerType>(arg.getType());
  });
}

SmallVector<Value> mlir::LLVM::InlineAsmOp::getAccessedOperands() {
  // Same rule as llvm.call: the asm string is opaque here, so every pointer
  // operand is a candidate.
  return llvm::filter_to_vector(getOperands(), [](Value arg) {
    return isa<LLVMPointerType>(arg.getType());
  });
}

SmallVector<Value> mlir::LLVM::CallBrOp::getAccessedOperands() {
  return llvm::filter_to_vector(getAsmOperands(), [](Value arg) {
    return isa<LLVMPointerType>(arg.getType());
  });
}

// The intrinsics below carry alias metadata but name no pointer the metadata
// describes: `stacksave` and `trap` take no operands at all, `assume` takes a
// condition, and the va_* family and `stackrestore` operate on the va_list or
// the saved stack pointer rather than on the memory the surrounding scope is
// reasoning about. They are here because real IR attaches !noalias to them
// inside a noalias region, not because they access a named location.
// Unlike trap and assume, a prefetch does name the location it touches.
SmallVector<Value> mlir::LLVM::Prefetch::getAccessedOperands() {
  return {getAddr()};
}

SmallVector<Value> mlir::LLVM::AssumeOp::getAccessedOperands() { return {}; }

// A fence orders accesses rather than performing one, so it names no location
// either; it is here because real IR attaches !noalias to it.
SmallVector<Value> mlir::LLVM::FenceOp::getAccessedOperands() { return {}; }

SmallVector<Value> mlir::LLVM::StackSaveOp::getAccessedOperands() { return {}; }

SmallVector<Value> mlir::LLVM::StackRestoreOp::getAccessedOperands() {
  return {};
}

SmallVector<Value> mlir::LLVM::Trap::getAccessedOperands() { return {}; }

// Same as trap: a ubsantrap names no location, but real IR attaches !noalias to
// it because the check it guards sits inside a noalias region.
SmallVector<Value> mlir::LLVM::UBSanTrap::getAccessedOperands() { return {}; }

SmallVector<Value> mlir::LLVM::VaStartOp::getAccessedOperands() {
  return {getArgList()};
}

SmallVector<Value> mlir::LLVM::VaCopyOp::getAccessedOperands() {
  return {getDestList(), getSrcList()};
}

SmallVector<Value> mlir::LLVM::VaEndOp::getAccessedOperands() {
  return {getArgList()};
}

SmallVector<Value>
mlir::LLVM::NoAliasScopeDeclOp::getAccessedOperands() {
  return {};
}

#include "mlir/Dialect/LLVMIR/LLVMInterfaces.cpp.inc"
