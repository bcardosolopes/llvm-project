//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the attributes in the CIR dialect.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_DIALECT_IR_CIRATTRS_H
#define CLANG_CIR_DIALECT_IR_CIRATTRS_H

#include "mlir/Dialect/Ptr/IR/MemorySpaceInterfaces.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinAttributeInterfaces.h"
#include "clang/Basic/AddressSpaces.h"

#include "clang/CIR/Dialect/IR/CIROpsEnums.h"

#include "clang/CIR/Interfaces/CIRTypeInterfaces.h"

#include "clang/CIR/Interfaces/ASTAttrInterfaces.h"

//===----------------------------------------------------------------------===//
// CIR Dialect Attrs
//===----------------------------------------------------------------------===//

namespace clang {
class FunctionDecl;
class RecordDecl;
class VarDecl;
} // namespace clang

namespace cir {
class ArrayType;
class BoolType;
class ComplexType;
class DataMemberType;
class IntType;
class MethodType;
class PointerType;
class RecordType;
class VectorType;

/// Base class for CIR attributes participating in the TBAA graph.
class TBAAAttr : public mlir::Attribute {
public:
  using Attribute::Attribute;

  /// Support LLVM type casting.
  static bool classof(Attribute attr);

  /// Required by DenseMapInfo to create empty and tombstone key.
  static TBAAAttr getFromOpaquePointer(const void *pointer) {
    return TBAAAttr(reinterpret_cast<const ImplType *>(pointer));
  }
};
} // namespace cir

#define GET_ATTRDEF_CLASSES
#include "clang/CIR/Dialect/IR/CIROpsAttributes.h.inc"

#endif // CLANG_CIR_DIALECT_IR_CIRATTRS_H
