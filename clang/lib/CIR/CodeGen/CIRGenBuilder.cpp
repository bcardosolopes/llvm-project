//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CIRGenBuilder.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace clang::CIRGen;

mlir::Value CIRGenBuilderTy::maybeBuildArrayDecay(mlir::Location loc,
                                                  mlir::Value arrayPtr,
                                                  mlir::Type eltTy) {
  const auto arrayPtrTy = mlir::cast<cir::PointerType>(arrayPtr.getType());
  const auto arrayTy = mlir::dyn_cast<cir::ArrayType>(arrayPtrTy.getPointee());

  if (arrayTy) {
    const cir::PointerType flatPtrTy =
        getPointerTo(arrayTy.getElementType(), arrayPtrTy.getAddrSpace());
    return cir::CastOp::create(*this, loc, flatPtrTy,
                               cir::CastKind::array_to_ptrdecay, arrayPtr);
  }

  assert(arrayPtrTy.getPointee() == eltTy &&
         "flat pointee type must match original array element type");
  return arrayPtr;
}

mlir::Value CIRGenBuilderTy::getArrayElement(mlir::Location arrayLocBegin,
                                             mlir::Location arrayLocEnd,
                                             mlir::Value arrayPtr,
                                             mlir::Type eltTy, mlir::Value idx,
                                             bool shouldDecay,
                                             cir::GEPNoWrapFlags flags) {
  auto arrayPtrTy = mlir::dyn_cast<cir::PointerType>(arrayPtr.getType());
  assert(arrayPtrTy && "expected pointer type");
  // If the array pointer is not decayed, emit a GetElementOp.
  auto arrayTy = mlir::dyn_cast<cir::ArrayType>(arrayPtrTy.getPointee());

  if (shouldDecay && arrayTy && arrayTy == eltTy) {
    auto eltPtrTy =
        getPointerTo(arrayTy.getElementType(), arrayPtrTy.getAddrSpace());
    // GetElementOp requires a fundamental integer index. Cast non-fundamental
    // integer types (e.g. _BitInt(15)) to a standard integer type.
    if (auto intTy = mlir::dyn_cast<cir::IntType>(idx.getType())) {
      if (!intTy.isFundamental()) {
        auto ptrDiffTy =
            cir::IntType::get(getContext(), 64, /*isSigned=*/true);
        idx = createIntCast(idx, ptrDiffTy);
      }
    }
    return cir::GetElementOp::create(*this, arrayLocEnd, eltPtrTy, arrayPtr,
                                     idx);
  }

  // If we don't have sufficient type information, emit a PtrStrideOp.
  mlir::Value basePtr = arrayPtr;
  if (shouldDecay)
    basePtr = maybeBuildArrayDecay(arrayLocBegin, arrayPtr, eltTy);
  const mlir::Type flatPtrTy = basePtr.getType();
  // PtrStrideOp also requires a fundamental integer index.
  if (auto intTy = mlir::dyn_cast<cir::IntType>(idx.getType())) {
    if (!intTy.isFundamental()) {
      auto ptrDiffTy = cir::IntType::get(getContext(), 64, /*isSigned=*/true);
      idx = createIntCast(idx, ptrDiffTy);
    }
  }
  return cir::PtrStrideOp::create(*this, arrayLocEnd, flatPtrTy, basePtr, idx,
                                  flags);
}

cir::ConstantOp CIRGenBuilderTy::getConstInt(mlir::Location loc,
                                             llvm::APSInt intVal) {
  bool isSigned = intVal.isSigned();
  unsigned width = intVal.getBitWidth();
  cir::IntType t = isSigned ? getSIntNTy(width) : getUIntNTy(width);
  return cir::ConstantOp::create(
      *this, loc, cir::IntAttr::get(t, intVal));
}

cir::ConstantOp CIRGenBuilderTy::getConstInt(mlir::Location loc,
                                             llvm::APInt intVal,
                                             bool isUnsigned) {
  return getConstInt(loc, llvm::APSInt(intVal, isUnsigned));
}

cir::ConstantOp CIRGenBuilderTy::getConstInt(mlir::Location loc, mlir::Type t,
                                             uint64_t c) {
  assert(mlir::isa<cir::IntType>(t) && "expected cir::IntType");
  return cir::ConstantOp::create(*this, loc, cir::IntAttr::get(t, c));
}

cir::ConstantOp
clang::CIRGen::CIRGenBuilderTy::getConstFP(mlir::Location loc, mlir::Type t,
                                           llvm::APFloat fpVal) {
  assert(mlir::isa<cir::FPTypeInterface>(t) && "expected floating point type");
  return cir::ConstantOp::create(*this, loc, cir::FPAttr::get(t, fpVal));
}

bool CIRGenBuilderTy::computeGlobalViewIndicesFromFlatOffset(
    int64_t offset, mlir::Type ty, cir::CIRDataLayout layout,
    llvm::SmallVectorImpl<int64_t> &indices) {
  if (!offset)
    return true;

  auto getIndexAndNewOffset =
      [](int64_t offset, int64_t eltSize) -> std::pair<int64_t, int64_t> {
    int64_t divRet = offset / eltSize;
    int64_t modRet = offset % eltSize;
    // Floor-divide: ensure the remainder is non-negative.
    if (modRet < 0) {
      divRet -= 1;
      modRet += eltSize;
    }
    return {divRet, modRet};
  };

  bool found = false;
  mlir::Type subType =
      llvm::TypeSwitch<mlir::Type, mlir::Type>(ty)
          .Case<cir::ArrayType>([&](auto arrayTy) {
            int64_t eltSize = layout.getTypeAllocSize(arrayTy.getElementType());
            const auto [index, newOffset] =
                getIndexAndNewOffset(offset, eltSize);
            indices.push_back(index);
            offset = newOffset;
            found = true;
            return arrayTy.getElementType();
          })
          .Case<cir::RecordType>([&](auto recordTy) {
            ArrayRef<mlir::Type> elts = recordTy.getMembers();
            int64_t pos = 0;
            for (size_t i = 0; i < elts.size(); ++i) {
              // An unsized member (e.g. the `!cir.vptr` of a polymorphic class)
              // has no queryable alloc size, so member indices cannot be walked
              // past it. Signal failure and let the caller fall back to a raw
              // byte offset (matching classic CodeGen, which emits a byte-offset
              // GEP into such records).
              if (!cir::isSized(elts[i])) {
                found = false;
                return mlir::Type();
              }
              int64_t eltSize =
                  (int64_t)layout.getTypeAllocSize(elts[i]).getFixedValue();
              unsigned alignMask = layout.getABITypeAlign(elts[i]).value() - 1;
              if (recordTy.getPacked())
                alignMask = 0;
              // Union's fields have the same offset, so no need to change pos
              // here, we just need to find eltSize that is greater then the
              // required offset. The same is true for the similar union type
              // check below
              if (!recordTy.isUnion())
                pos = (pos + alignMask) & ~alignMask;
              // A negative flat offset points before this record (e.g. a global
              // pointer constant such as `(char *)&record_global - K`). Unlike
              // arrays, which can use a negative element index, a record offset
              // cannot be expressed as member indices, so signal failure and let
              // the caller fall back to a raw byte offset (matching classic
              // CodeGen, which emits a byte-offset GEP for such pointers).
              if (offset < 0) {
                found = false;
                return mlir::Type();
              }
              // For zero-sized elements (e.g. char data[0]), eltSize is 0 so
              // the range [pos, pos+eltSize) is empty. Match when offset == pos
              // to handle pointers to zero-length trailing arrays.
              if (offset < pos + eltSize ||
                  (eltSize == 0 && offset == pos)) {
                indices.push_back(i);
                offset -= pos;
                found = true;
                return elts[i];
              }
              // No need to update pos here, see the comment above.
              if (!recordTy.isUnion())
                pos += eltSize;
            }
            // Offset was not found within the record (e.g. tail padding,
            // virtual bases, bitfields, or alignment mismatches).
            found = false;
            return mlir::Type();
          })
          .Default([&](mlir::Type otherTy) {
            // Unexpected type — can't decompose further.
            found = false;
            return mlir::Type();
          });

  if (!found) {
    indices.clear();
    return false;
  }

  assert(subType);
  return computeGlobalViewIndicesFromFlatOffset(offset, subType, layout,
                                                indices);
}

uint64_t CIRGenBuilderTy::computeOffsetFromGlobalViewIndices(
    const cir::CIRDataLayout &layout, mlir::Type ty,
    llvm::ArrayRef<int64_t> indices) {

  int64_t offset = 0;
  for (int64_t idx : indices) {
    if (auto recordTy = mlir::dyn_cast<cir::RecordType>(ty)) {
      offset += recordTy.getElementOffset(layout.layout, idx);
      assert(idx < (int64_t)recordTy.getMembers().size());
      ty = recordTy.getMembers()[idx];
    } else if (auto arrayTy = mlir::dyn_cast<cir::ArrayType>(ty)) {
      ty = arrayTy.getElementType();
      offset += layout.getTypeAllocSize(ty) * idx;
    } else {
      llvm_unreachable("unexpected type in computeOffsetFromGlobalViewIndices");
    }
  }

  return offset;
}

cir::RecordType clang::CIRGen::CIRGenBuilderTy::getCompleteRecordType(
    mlir::ArrayAttr fields, bool packed, bool padded, llvm::StringRef name) {
  assert(!cir::MissingFeatures::astRecordDeclAttr());
  llvm::SmallVector<mlir::Type> members;
  members.reserve(fields.size());
  llvm::transform(fields, std::back_inserter(members),
                  [](mlir::Attribute attr) {
                    return mlir::cast<mlir::TypedAttr>(attr).getType();
                  });

  if (name.empty())
    return getAnonRecordTy(members, packed, padded);

  return getCompleteNamedRecordType(members, packed, padded, name);
}

mlir::Attribute clang::CIRGen::CIRGenBuilderTy::getConstRecordOrZeroAttr(
    mlir::ArrayAttr arrayAttr, bool packed, bool padded, mlir::Type type) {
  auto recordTy = mlir::cast_or_null<cir::RecordType>(type);

  // Record type not specified: create anon record type from members.
  if (!recordTy) {
    recordTy = getCompleteRecordType(arrayAttr, packed, padded);
  }

  // Return zero or anonymous constant record.
  const bool isZero = llvm::all_of(
      arrayAttr, [&](mlir::Attribute a) { return isNullValue(a); });
  if (isZero)
    return cir::ZeroAttr::get(recordTy);
  return cir::ConstRecordAttr::get(recordTy, arrayAttr);
}

// This can't be defined in Address.h because that file is included by
// CIRGenBuilder.h
Address Address::withElementType(CIRGenBuilderTy &builder,
                                 mlir::Type elemTy) const {
  // OGCG preserves Offset field when changing element type. CIR's Address
  // doesn't track byte offsets (uses GEP-based addressing instead).

  return Address(builder.createPtrBitcast(getBasePointer(), elemTy), elemTy,
                 getAlignment(), isKnownNonNull());
}
