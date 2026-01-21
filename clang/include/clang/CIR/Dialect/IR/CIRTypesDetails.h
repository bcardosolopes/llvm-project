//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains implementation details, such as storage structures, of
// CIR dialect types.
//
//===----------------------------------------------------------------------===//
#ifndef CIR_DIALECT_IR_CIRTYPESDETAILS_H
#define CIR_DIALECT_IR_CIRTYPESDETAILS_H

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/Support/LogicalResult.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/Interfaces/ASTAttrInterfaces.h"
#include "llvm/ADT/Hashing.h"

namespace cir {
namespace detail {

//===----------------------------------------------------------------------===//
// CIR RecordTypeStorage
//===----------------------------------------------------------------------===//

/// Type storage for CIR record types.
struct RecordTypeStorage : public mlir::TypeStorage {
  struct KeyTy {
    llvm::ArrayRef<mlir::Type> members;
    mlir::StringAttr name;
    bool incomplete;
    bool packed;
    bool padded;
    RecordType::RecordKind kind;
    cir::ASTRecordDeclInterface ast;

    KeyTy(llvm::ArrayRef<mlir::Type> members, mlir::StringAttr name,
          bool incomplete, bool packed, bool padded,
          RecordType::RecordKind kind, cir::ASTRecordDeclInterface ast = {})
        : members(members), name(name), incomplete(incomplete), packed(packed),
          padded(padded), kind(kind), ast(ast) {}
  };

  llvm::ArrayRef<mlir::Type> members;
  mlir::StringAttr name;
  bool incomplete;
  bool packed;
  bool padded;
  RecordType::RecordKind kind;
  cir::ASTRecordDeclInterface ast;

  RecordTypeStorage(llvm::ArrayRef<mlir::Type> members, mlir::StringAttr name,
                    bool incomplete, bool packed, bool padded,
                    RecordType::RecordKind kind,
                    cir::ASTRecordDeclInterface ast = {})
      : members(members), name(name), incomplete(incomplete), packed(packed),
        padded(padded), kind(kind), ast(ast) {
    assert((name || !incomplete) && "Incomplete records must have a name");
  }

  KeyTy getAsKey() const {
    return KeyTy(members, name, incomplete, packed, padded, kind, ast);
  }

  bool operator==(const KeyTy &key) const {
    if (name)
      return (name == key.name) && (kind == key.kind);
    return std::tie(members, name, incomplete, packed, padded, kind) ==
           std::tie(key.members, key.name, key.incomplete, key.packed,
                    key.padded, key.kind);
  }

  static llvm::hash_code hashKey(const KeyTy &key) {
    if (key.name)
      return llvm::hash_combine(key.name, key.kind);
    return llvm::hash_combine(key.members, key.incomplete, key.packed,
                              key.padded, key.kind);
  }

  static RecordTypeStorage *construct(mlir::TypeStorageAllocator &allocator,
                                      const KeyTy &key) {
    return new (allocator.allocate<RecordTypeStorage>()) RecordTypeStorage(
        allocator.copyInto(key.members), key.name, key.incomplete, key.packed,
        key.padded, key.kind, key.ast);
  }

  /// Mutates the members and attributes an identified record.
  ///
  /// Once a record is mutated, it is marked as complete, preventing further
  /// mutations. Anonymous records are always complete and cannot be mutated.
  /// This method does not fail if a mutation of a complete record does not
  /// change the record.
  llvm::LogicalResult mutate(mlir::TypeStorageAllocator &allocator,
                             llvm::ArrayRef<mlir::Type> members, bool packed,
                             bool padded,
                             cir::ASTRecordDeclInterface ast = {}) {
    // Anonymous records cannot mutate.
    if (!name)
      return llvm::failure();

    // Mutation of complete records are allowed if they change nothing, or
    // if they only update member types (e.g., during CXX ABI lowering where
    // MethodType/DataMemberType members are replaced with their lowered forms).
    if (!incomplete) {
      if ((this->members == members) && (this->packed == packed) &&
          (this->padded == padded))
        return mlir::success();
      // Allow re-mutation if only member types changed (same count, same
      // packed/padded).
      if ((this->members.size() == members.size()) &&
          (this->packed == packed) && (this->padded == padded)) {
        this->members = allocator.copyInto(members);
        return mlir::success();
      }
      return mlir::failure();
    }

    // Mutate incomplete record.
    this->members = allocator.copyInto(members);
    this->packed = packed;
    this->padded = padded;
    this->ast = ast;

    incomplete = false;
    return llvm::success();
  }

  /// Replace member types for a complete identified record. This is used by
  /// the CXX ABI lowering pass to update member types (e.g., lowering
  /// MethodType and DataMemberType) within record types. Unlike the primary
  /// mutate() overload, this allows modifying members of complete records.
  llvm::LogicalResult mutate(mlir::TypeStorageAllocator &allocator,
                             llvm::ArrayRef<mlir::Type> newMembers) {
    // Only named, complete records can have their members replaced.
    if (!name || incomplete)
      return llvm::failure();
    // The number of members must match.
    if (newMembers.size() != members.size())
      return llvm::failure();
    // If nothing changed, this is a no-op.
    if (this->members == newMembers)
      return llvm::success();
    this->members = allocator.copyInto(newMembers);
    return llvm::success();
  }
};

} // namespace detail
} // namespace cir

#endif // CIR_DIALECT_IR_CIRTYPESDETAILS_H
