//===--- ABIInfoImpl.cpp - Encapsulate calling convention details ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file partially mimics clang/lib/CodeGen/ABIInfoImpl.cpp. The queries are
// adapted to operate on the CIR dialect, however.
//
//===----------------------------------------------------------------------===//

#include "ABIInfo.h"
#include "ABIInfoImpl.h"
#include "CIRCXXABI.h"
#include "LowerFunction.h"
#include "LowerFunctionInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Type.h"
#include "llvm/Support/ErrorHandling.h"

namespace cir {

bool classifyReturnType(const CIRCXXABI &CXXABI, LowerFunctionInfo &FI,
                        const ABIInfo &Info) {
  mlir::Type Ty = FI.getReturnType();

  if (const auto RT = mlir::dyn_cast<RecordType>(Ty)) {
    // For C records (non-CXXRecordDecl) that can't pass in registers,
    // classify as indirect return. C++ records are handled by CXXABI below.
    if (auto ast = RT.getAst()) {
      const clang::RecordDecl *RD = ast.getRawDecl();
      if (RD && !llvm::isa<clang::CXXRecordDecl>(RD) &&
          !RD->canPassInRegisters()) {
        FI.getReturnInfo() = Info.getNaturalAlignIndirect(Ty);
        return true;
      }
    }
  }

  return CXXABI.classifyReturnType(FI);
}

bool isAggregateTypeForABI(mlir::Type T) {
  // Member function pointers are aggregates for ABI purposes even though
  // they have scalar evaluation kind. In Itanium ABI they are {i64, i64}.
  return !LowerFunction::hasScalarEvaluationKind(T) ||
         mlir::isa<cir::MethodType>(T);
}

mlir::Value emitRoundPointerUpToAlignment(cir::CIRBaseBuilderTy &builder,
                                          mlir::Value ptr, unsigned alignment) {
  // OverflowArgArea = (OverflowArgArea + Align - 1) & -Align;
  auto loc = ptr.getLoc();
  auto i8Ty = mlir::IntegerType::get(builder.getContext(), 8);
  auto i8PtrTy = cir::PointerType::get(i8Ty);
  auto ptrDiffTy =
      cir::IntType::get(builder.getContext(), 64, /*isSigned=*/false);
  auto signedPtrDiffTy =
      cir::IntType::get(builder.getContext(), 64, /*isSigned=*/true);

  mlir::Value alignM1 = cir::ConstantOp::create(
      builder, loc, cir::IntAttr::get(ptrDiffTy, alignment - 1));
  mlir::Value castPtr = builder.createBitcast(ptr, i8PtrTy);
  mlir::Value roundUp = builder.createPtrStride(
      loc, castPtr, alignM1, cir::GEPNoWrapFlags::inbounds);
  mlir::Value roundUpAsOrig = builder.createBitcast(roundUp, ptr.getType());
  mlir::Value negAlignMask = cir::ConstantOp::create(
      builder, loc, cir::IntAttr::get(signedPtrDiffTy, -int64_t(alignment)));
  return cir::PtrMaskOp::create(builder, loc, roundUpAsOrig.getType(),
                                roundUpAsOrig, negAlignMask);
}

mlir::Type useFirstFieldIfTransparentUnion(mlir::Type Ty) {
  if (auto RT = mlir::dyn_cast<RecordType>(Ty)) {
    if (RT.isUnion()) {
      if (auto ast = RT.getAst()) {
        const clang::RecordDecl *rd = ast.getRawDecl();
        if (rd && rd->hasAttr<clang::TransparentUnionAttr>()) {
          cir_cconv_assert(!RT.getMembers().empty() &&
                           "sema created an empty transparent union");
          return RT.getMembers().front();
        }
      }
    }
  }
  return Ty;
}

CIRCXXABI::RecordArgABI getRecordArgABI(const RecordType RT,
                                        CIRCXXABI &CXXABI) {
  return CXXABI.getRecordArgABI(RT);
}

CIRCXXABI::RecordArgABI getRecordArgABI(mlir::Type ty, CIRCXXABI &CXXABI) {
  auto st = mlir::dyn_cast<RecordType>(ty);
  if (!st)
    return CIRCXXABI::RAA_Default;
  return getRecordArgABI(st, CXXABI);
}

static bool isEmptyRecordImpl(const clang::RecordDecl *RD, bool allowArrays);

/// Check whether a clang field is "empty" for the purposes of
/// isEmptyRecord(). Mirrors OGCG's isEmptyField() logic.
static bool isEmptyField(const clang::FieldDecl *FD, bool allowArrays) {
  if (FD->isUnnamedBitField())
    return true;

  clang::QualType FT = FD->getType();

  // Constant arrays of empty records count as empty when allowed.
  if (allowArrays)
    while (const auto *AT =
               FD->getASTContext().getAsConstantArrayType(FT)) {
      if (AT->isZeroSize())
        return true;
      FT = AT->getElementType();
    }

  const clang::RecordType *RT = FT->getAs<clang::RecordType>();
  if (!RT)
    return false;

  // C++ record fields are never empty (Itanium ABI), except
  // [[no_unique_address]] fields.
  if (llvm::isa<clang::CXXRecordDecl>(RT->getDecl()) &&
      !FD->hasAttr<clang::NoUniqueAddressAttr>())
    return false;

  return isEmptyRecordImpl(RT->getDecl(), allowArrays);
}

/// Check whether a clang RecordDecl represents an empty record.
/// Mirrors OGCG's isEmptyRecord() from ABIInfoImpl.cpp.
static bool isEmptyRecordImpl(const clang::RecordDecl *RD, bool allowArrays) {
  if (!RD || RD->hasFlexibleArrayMember())
    return false;

  // Check C++ bases.
  if (const auto *CXXRD = llvm::dyn_cast<clang::CXXRecordDecl>(RD))
    for (const auto &Base : CXXRD->bases())
      if (!isEmptyRecordImpl(Base.getType()->getAsRecordDecl(), allowArrays))
        return false;

  // Check fields.
  for (const auto *FD : RD->fields())
    if (!isEmptyField(FD, allowArrays))
      return false;

  return true;
}

/// Return true if the given CIR type is an empty record (no data members),
/// mirroring OGCG's isEmptyRecord(). Uses AST information from the
/// RecordType when available, since CIR pads empty C++ structs with an i8
/// that would make size-based checks incorrect.
bool isEmptyRecord(mlir::Type T, bool allowArrays) {
  auto RT = mlir::dyn_cast<RecordType>(T);
  if (!RT)
    return false;

  auto ast = RT.getAst();
  if (!ast)
    return false;

  const clang::RecordDecl *RD = ast.getRawDecl();
  if (!RD)
    return false;

  return isEmptyRecordImpl(RD, allowArrays);
}

/// Check whether a clang QualType is aggregate for ABI purposes.
/// Mirrors the relevant subset of OGCG's isAggregateTypeForABI().
static bool isASTAggregateTypeForABI(clang::QualType T) {
  return T->isArrayType() || T->isRecordType() || T->isComplexType() ||
         T->isMemberFunctionPointerType();
}

/// Determine if a QualType is a single-element struct, recursing through
/// bases and single-element arrays. Returns the element type if so, else
/// nullptr. Mirrors OGCG's isSingleElementStruct() from ABIInfoImpl.cpp.
static const clang::Type *
isSingleElementStructImpl(clang::QualType T, const clang::ASTContext &context) {
  const auto *RD = T->getAsRecordDecl();
  if (!RD)
    return nullptr;

  if (RD->hasFlexibleArrayMember())
    return nullptr;

  const clang::Type *found = nullptr;

  // If this is a C++ record, check the bases first.
  if (const auto *CXXRD = llvm::dyn_cast<clang::CXXRecordDecl>(RD)) {
    for (const auto &I : CXXRD->bases()) {
      // Ignore empty records.
      if (isEmptyRecordImpl(I.getType()->getAsRecordDecl(), true))
        continue;

      // If we already found an element then this isn't a single-element struct.
      if (found)
        return nullptr;

      // If this is non-empty and not a single element struct, the composite
      // cannot be a single element struct.
      found = isSingleElementStructImpl(I.getType(), context);
      if (!found)
        return nullptr;
    }
  }

  // Check for single element.
  for (const auto *FD : RD->fields()) {
    clang::QualType FT = FD->getType();

    // Ignore empty fields.
    if (isEmptyField(FD, true))
      continue;

    // If we already found an element then this isn't a single-element struct.
    if (found)
      return nullptr;

    // Treat single element arrays as the element.
    while (const auto *AT = context.getAsConstantArrayType(FT)) {
      if (AT->getZExtSize() != 1)
        break;
      FT = AT->getElementType();
    }

    if (!isASTAggregateTypeForABI(FT)) {
      found = FT.getTypePtr();
    } else {
      found = isSingleElementStructImpl(FT, context);
      if (!found)
        return nullptr;
    }
  }

  // We don't consider a struct a single-element struct if it has
  // padding beyond the element type.
  if (found && context.getTypeSize(found) != context.getTypeSize(T))
    return nullptr;

  return found;
}

/// Find the CIR type corresponding to the single element in a CIR RecordType.
/// Recurses into nested single-element records and unwraps single-element
/// arrays (e.g., struct { float arr[1]; } → float).
static mlir::Type
findSingleElementCIRType(RecordType RT,
                         const clang::Type *astElementType) {
  auto members = RT.getMembers();
  if (members.empty())
    return {};

  // The first CIR member of a single-element struct is always the data
  // member (bases come first for C++, padding comes after data).
  mlir::Type firstMember = members[0];

  // If the first member is itself a record, recurse into it.
  if (auto nestedRT = mlir::dyn_cast<RecordType>(firstMember))
    return findSingleElementCIRType(nestedRT, astElementType);

  // Unwrap single-element arrays (e.g., [1 x float] → float), matching the
  // array-unwrapping done in isSingleElementStructImpl on the AST side.
  while (auto arrTy = mlir::dyn_cast<cir::ArrayType>(firstMember)) {
    if (arrTy.getSize() != 1)
      break;
    firstMember = arrTy.getElementType();
  }

  return firstMember;
}

mlir::Type isSingleElementStruct(mlir::Type T) {
  auto RT = mlir::dyn_cast<RecordType>(T);
  if (!RT)
    return {};

  auto ast = RT.getAst();
  if (!ast)
    return {};

  const clang::RecordDecl *RD = ast.getRawDecl();
  if (!RD)
    return {};

  const clang::ASTContext &context = RD->getASTContext();
  const clang::Type *found = isSingleElementStructImpl(
      context.getCanonicalTagType(RD), context);
  if (!found)
    return {};

  return findSingleElementCIRType(RT, found);
}

} // namespace cir
