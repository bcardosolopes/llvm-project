//===- RISCV.cpp - TargetInfo for RISC-V ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file mimics clang/lib/CodeGen/Targets/RISCV.cpp. The queries are
// adapted to operate on the CIR dialect, however.
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "LowerFunctionInfo.h"
#include "LowerTypes.h"
#include "TargetInfo.h"
#include "TargetLoweringInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Type.h"
#include "clang/CIR/ABIArgInfo.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "llvm/Support/ErrorHandling.h"

using ABIArgInfo = cir::ABIArgInfo;

namespace cir {

//===----------------------------------------------------------------------===//
// RISC-V ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

/// Return true iff \p ty (an AST QualType) is a floating-point CIR type.
static bool isFPCIRType(mlir::Type ty) {
  return mlir::isa<SingleType, DoubleType, FP16Type, BF16Type, FP80Type,
                   FP128Type, LongDoubleType>(ty);
}

/// Map a scalar AST builtin/integral type that may appear as a leaf of an
/// FP-calling-convention-eligible struct (float/double/half/bfloat/integer) to
/// the corresponding CIR type. Only the leaf types that
/// detectFPCCEligibleStructHelper can produce are handled. Mirrors classic
/// CodeGen's CGT.ConvertType() for those leaves, but stays on the CIR type
/// universe so the coerced signature byte-matches CIRGen's field types.
static mlir::Type convertFPCCFieldType(clang::QualType ty,
                                       const clang::ASTContext &astCtx,
                                       mlir::MLIRContext *mlirCtx) {
  if (const auto *bt = ty->getAs<clang::BuiltinType>()) {
    switch (bt->getKind()) {
    case clang::BuiltinType::Half:
    case clang::BuiltinType::Float16:
      return FP16Type::get(mlirCtx);
    case clang::BuiltinType::BFloat16:
      return BF16Type::get(mlirCtx);
    case clang::BuiltinType::Float:
      return SingleType::get(mlirCtx);
    case clang::BuiltinType::Double:
      return DoubleType::get(mlirCtx);
    default:
      // Wider FP types (long double / __float128 are 128-bit on RISC-V) can
      // never be FP-CC eligible because they exceed FLen; the size guards in
      // detectFPCCEligibleStructHelper reject them before conversion. Returning
      // a null type here keeps such structs off the FP calling convention.
      break;
    }
  }

  // Integral leaf (int + fp pairs). Use a plain integer of the AST width;
  // signedness matches the source integer type.
  if (ty->isIntegralOrEnumerationType()) {
    unsigned width = astCtx.getTypeSize(ty);
    return IntType::get(mlirCtx, width,
                        ty->isSignedIntegerOrEnumerationType());
  }

  return {};
}

/// Mirror of the AST empty-record predicate used by classic CodeGen's
/// isEmptyRecord (with asIfNoUniqueAddr=false). A record is empty if it has no
/// non-empty bases and no non-empty fields.
static bool isEmptyRecordAST(const clang::ASTContext &ctx, clang::QualType t,
                             bool allowArrays);

static bool isEmptyFieldAST(const clang::ASTContext &ctx,
                            const clang::FieldDecl *fd, bool allowArrays) {
  if (fd->isUnnamedBitField())
    return true;

  clang::QualType ft = fd->getType();

  if (allowArrays)
    while (const clang::ConstantArrayType *at =
               ctx.getAsConstantArrayType(ft)) {
      if (at->isZeroSize())
        return true;
      ft = at->getElementType();
    }

  const clang::RecordType *rt = ft->getAs<clang::RecordType>();
  if (!rt)
    return false;

  // C++ record fields are never empty (Itanium ABI), except
  // [[no_unique_address]] fields.
  if (llvm::isa<clang::CXXRecordDecl>(rt->getDecl()) &&
      !fd->hasAttr<clang::NoUniqueAddressAttr>())
    return false;

  return isEmptyRecordAST(ctx, ft, allowArrays);
}

static bool isEmptyRecordAST(const clang::ASTContext &ctx, clang::QualType t,
                             bool allowArrays) {
  const clang::RecordType *rt = t->getAs<clang::RecordType>();
  if (!rt)
    return false;

  const clang::RecordDecl *rd = rt->getDecl()->getDefinitionOrSelf();
  if (rd->hasFlexibleArrayMember())
    return false;

  if (const auto *cxxRD = llvm::dyn_cast<clang::CXXRecordDecl>(rd))
    for (const auto &base : cxxRD->bases())
      if (!isEmptyRecordAST(ctx, base.getType(), allowArrays))
        return false;

  for (const auto *fd : rd->fields())
    if (!isEmptyFieldAST(ctx, fd, allowArrays))
      return false;

  return true;
}

/// Mirror of CIR's ItaniumCXXABI::getRecordArgABI: a C++ record that cannot
/// pass in registers is passed indirectly. Returns true for that case.
static bool astRecordIsIndirect(clang::QualType t) {
  const clang::RecordType *rt = t->getAs<clang::RecordType>();
  if (!rt)
    return false;
  const auto *cxxRD =
      llvm::dyn_cast<clang::CXXRecordDecl>(rt->getDecl());
  return cxxRD && !cxxRD->canPassInRegisters();
}

class RISCVABIInfo : public ABIInfo {
private:
  // Size of the integer ('x') registers in bits.
  unsigned XLen;
  // Size of the floating point ('f') registers in bits. Note that the target
  // ISA might have a wider FLen than the selected ABI (e.g. an RV32IF target
  // with soft float ABI has FLen==0).
  unsigned FLen;
  const int NumArgGPRs;
  const int NumArgFPRs;
  const bool EABI;

  // AST context bound while classifying a record (for the AST-driven
  // FP-calling-convention struct flattening below).
  mutable const clang::ASTContext *astCtx = nullptr;

  bool detectFPCCEligibleStructHelper(clang::QualType ty,
                                      clang::CharUnits curOff,
                                      mlir::Type &field1Ty,
                                      clang::CharUnits &field1Off,
                                      mlir::Type &field2Ty,
                                      clang::CharUnits &field2Off) const;

public:
  RISCVABIInfo(LowerTypes &LT, unsigned XLen, unsigned FLen, bool EABI)
      : ABIInfo(LT), XLen(XLen), FLen(FLen), NumArgGPRs(EABI ? 6 : 8),
        NumArgFPRs(FLen != 0 ? 8 : 0), EABI(EABI) {}

  void computeInfo(LowerFunctionInfo &FI) const override;

  ABIArgInfo classifyArgumentType(mlir::Type ty, bool isFixed, bool isBitInt,
                                  int &argGPRsLeft, int &argFPRsLeft) const;
  ABIArgInfo classifyReturnType(mlir::Type retTy, bool isBitInt) const;

  ABIArgInfo extendType(mlir::Type ty, mlir::Type coerceTy = {}) const;

  bool detectFPCCEligibleStruct(mlir::Type ty, mlir::Type &field1Ty,
                                clang::CharUnits &field1Off,
                                mlir::Type &field2Ty,
                                clang::CharUnits &field2Off, int &neededArgGPRs,
                                int &neededArgFPRs) const;
  ABIArgInfo coerceAndExpandFPCCEligibleStruct(mlir::Type field1Ty,
                                               clang::CharUnits field1Off,
                                               mlir::Type field2Ty,
                                               clang::CharUnits field2Off) const;
};

void RISCVABIInfo::computeInfo(LowerFunctionInfo &FI) const {
  mlir::Type retTy = FI.getReturnType();
  if (!cir::classifyReturnType(getCXXABI(), FI, *this))
    FI.getReturnInfo() =
        classifyReturnType(retTy, FI.getReturnArgInfo().isBitIntType());

  // IsRetIndirect is true if classifyArgumentType indicated the value should
  // be passed indirect, or if the type size is a scalar greater than 2*XLen
  // and not a complex type with elements <= FLen. e.g. fp128 is passed direct
  // in LLVM IR, relying on the backend lowering code to rewrite the argument
  // list and pass indirectly on RV32.
  bool isRetIndirect = FI.getReturnInfo().getKind() == ABIArgInfo::Indirect;
  if (!isRetIndirect && !isAggregateTypeForABI(retTy) &&
      getContext().getTypeSize(retTy) > (2 * XLen)) {
    if (auto complexTy = mlir::dyn_cast<ComplexType>(retTy); complexTy && FLen) {
      mlir::Type eltTy = complexTy.getElementType();
      isRetIndirect = getContext().getTypeSize(eltTy) > FLen;
    } else {
      // This is a normal scalar > 2*XLen, such as fp128 on RV32.
      isRetIndirect = true;
    }
  }

  int argGPRsLeft = isRetIndirect ? NumArgGPRs - 1 : NumArgGPRs;
  int argFPRsLeft = NumArgFPRs;
  int numFixedArgs = FI.getNumRequiredArgs();

  int argNum = 0;
  for (auto &argInfo : FI.arguments()) {
    bool isFixed = argNum < numFixedArgs;
    argInfo.info = classifyArgumentType(argInfo.type, isFixed,
                                        argInfo.isBitIntType(), argGPRsLeft,
                                        argFPRsLeft);
    argNum++;
  }
}

// Returns true if the struct is a potential candidate for the floating point
// calling convention. If this function returns true, the caller is
// responsible for checking that if there is only a single field then that
// field is a float.
//
// Operates on the AST QualType (obtained from the CIR RecordType's attached
// AST) so field offsets, bitfields, C++ bases, arrays, and complex types are
// classified identically to classic CodeGen. Produces CIR field types.
bool RISCVABIInfo::detectFPCCEligibleStructHelper(
    clang::QualType ty, clang::CharUnits curOff, mlir::Type &field1Ty,
    clang::CharUnits &field1Off, mlir::Type &field2Ty,
    clang::CharUnits &field2Off) const {
  mlir::MLIRContext *mlirCtx = LT.getMLIRContext();
  bool isInt = ty->isIntegralOrEnumerationType();
  bool isFloat = ty->isRealFloatingType();

  if (isInt || isFloat) {
    uint64_t size = astCtx->getTypeSize(ty);
    if (isInt && size > XLen)
      return false;
    // Can't be eligible if larger than the FP registers. Handling of half
    // precision values has been specified in the ABI, so don't block those.
    if (isFloat && size > FLen)
      return false;
    // Can't be eligible if an integer type was already found (int+int pairs
    // are not eligible).
    if (isInt && field1Ty && mlir::isa<IntType>(field1Ty))
      return false;
    if (!field1Ty) {
      field1Ty = convertFPCCFieldType(ty, *astCtx, mlirCtx);
      field1Off = curOff;
      return true;
    }
    if (!field2Ty) {
      field2Ty = convertFPCCFieldType(ty, *astCtx, mlirCtx);
      field2Off = curOff;
      return true;
    }
    return false;
  }

  if (const auto *cTy = ty->getAs<clang::ComplexType>()) {
    if (field1Ty)
      return false;
    clang::QualType eltTy = cTy->getElementType();
    if (astCtx->getTypeSize(eltTy) > FLen)
      return false;
    field1Ty = convertFPCCFieldType(eltTy, *astCtx, mlirCtx);
    field1Off = curOff;
    field2Ty = field1Ty;
    field2Off = field1Off + astCtx->getTypeSizeInChars(eltTy);
    return true;
  }

  if (const clang::ConstantArrayType *aTy =
          astCtx->getAsConstantArrayType(ty)) {
    uint64_t arraySize = aTy->getZExtSize();
    clang::QualType eltTy = aTy->getElementType();
    // Non-zero-length arrays of empty records make the struct ineligible for
    // the FP calling convention in C++.
    if (const auto *rTy = eltTy->getAsCanonical<clang::RecordType>()) {
      if (arraySize != 0 &&
          llvm::isa<clang::CXXRecordDecl>(rTy->getDecl()) &&
          isEmptyRecordAST(*astCtx, eltTy, /*allowArrays=*/true))
        return false;
    }
    clang::CharUnits eltSize = astCtx->getTypeSizeInChars(eltTy);
    for (uint64_t i = 0; i < arraySize; ++i) {
      bool ret = detectFPCCEligibleStructHelper(eltTy, curOff, field1Ty,
                                                field1Off, field2Ty, field2Off);
      if (!ret)
        return false;
      curOff += eltSize;
    }
    return true;
  }

  if (const auto *rTy = ty->getAsCanonical<clang::RecordType>()) {
    // Structures with either a non-trivial destructor or a non-trivial
    // copy constructor are not eligible for the FP calling convention.
    if (astRecordIsIndirect(ty))
      return false;
    if (isEmptyRecordAST(*astCtx, ty, /*allowArrays=*/true))
      return true;
    const clang::RecordDecl *rd = rTy->getDecl()->getDefinitionOrSelf();
    // Unions aren't eligible unless they're empty (which is caught above).
    if (rd->isUnion())
      return false;
    const clang::ASTRecordLayout &layout = astCtx->getASTRecordLayout(rd);
    // If this is a C++ record, check the bases first.
    if (const auto *cxxRD = llvm::dyn_cast<clang::CXXRecordDecl>(rd)) {
      for (const clang::CXXBaseSpecifier &b : cxxRD->bases()) {
        const auto *bDecl = b.getType()->castAsCXXRecordDecl();
        clang::CharUnits baseOff = layout.getBaseClassOffset(bDecl);
        bool ret = detectFPCCEligibleStructHelper(
            b.getType(), curOff + baseOff, field1Ty, field1Off, field2Ty,
            field2Off);
        if (!ret)
          return false;
      }
    }
    int zeroWidthBitFieldCount = 0;
    for (const clang::FieldDecl *fd : rd->fields()) {
      uint64_t fieldOffInBits = layout.getFieldOffset(fd->getFieldIndex());
      clang::QualType qTy = fd->getType();
      if (fd->isBitField()) {
        unsigned bitWidth = fd->getBitWidthValue();
        // Allow a bitfield with a type greater than XLen as long as the
        // bitwidth is XLen or less.
        if (astCtx->getTypeSize(qTy) > XLen && bitWidth <= XLen)
          qTy = astCtx->getIntTypeForBitwidth(XLen, false);
        if (bitWidth == 0) {
          zeroWidthBitFieldCount++;
          continue;
        }
      }

      bool ret = detectFPCCEligibleStructHelper(
          qTy, curOff + astCtx->toCharUnitsFromBits(fieldOffInBits), field1Ty,
          field1Off, field2Ty, field2Off);
      if (!ret)
        return false;

      // As a quirk of the ABI, zero-width bitfields aren't ignored for fp+fp
      // or int+fp structs, but are ignored for a struct with an fp field and
      // any number of zero-width bitfields.
      if (field2Ty && zeroWidthBitFieldCount > 0)
        return false;
    }
    return field1Ty != nullptr;
  }

  return false;
}

// Determine if a struct is eligible for passing according to the floating
// point calling convention (i.e., when flattened it contains a single fp
// value, fp+fp, or int+fp of appropriate size). If so, neededArgFPRs and
// neededArgGPRs are incremented appropriately.
bool RISCVABIInfo::detectFPCCEligibleStruct(
    mlir::Type ty, mlir::Type &field1Ty, clang::CharUnits &field1Off,
    mlir::Type &field2Ty, clang::CharUnits &field2Off, int &neededArgGPRs,
    int &neededArgFPRs) const {
  auto rt = mlir::dyn_cast<RecordType>(ty);
  if (!rt)
    return false;
  auto ast = rt.getAst();
  if (!ast)
    return false;
  const clang::RecordDecl *rd = ast.getRawDecl();
  if (!rd)
    return false;

  astCtx = &rd->getASTContext();
  clang::QualType astTy = astCtx->getCanonicalTagType(rd);

  field1Ty = {};
  field2Ty = {};
  neededArgGPRs = 0;
  neededArgFPRs = 0;
  bool isCandidate = detectFPCCEligibleStructHelper(
      astTy, clang::CharUnits::Zero(), field1Ty, field1Off, field2Ty,
      field2Off);
  if (!field1Ty)
    return false;
  // Not really a candidate if we have a single int but no float.
  if (field1Ty && !field2Ty && !isFPCIRType(field1Ty))
    return false;
  if (!isCandidate)
    return false;
  if (field1Ty && isFPCIRType(field1Ty))
    neededArgFPRs++;
  else if (field1Ty)
    neededArgGPRs++;
  if (field2Ty && isFPCIRType(field2Ty))
    neededArgFPRs++;
  else if (field2Ty)
    neededArgGPRs++;
  return true;
}

// Call getCoerceAndExpand for the two-element flattened struct described by
// field1Ty, field1Off, field2Ty, field2Off. This method will create an
// appropriate coerceToType and unpaddedCoerceToType.
ABIArgInfo RISCVABIInfo::coerceAndExpandFPCCEligibleStruct(
    mlir::Type field1Ty, clang::CharUnits field1Off, mlir::Type field2Ty,
    clang::CharUnits field2Off) const {
  mlir::MLIRContext *ctx = LT.getMLIRContext();
  auto i8Ty = IntType::get(ctx, 8, /*isSigned=*/false);

  llvm::SmallVector<mlir::Type, 3> coerceElts;
  llvm::SmallVector<mlir::Type, 2> unpaddedCoerceElts;
  if (!field1Off.isZero())
    coerceElts.push_back(ArrayType::get(i8Ty, field1Off.getQuantity()));

  coerceElts.push_back(field1Ty);
  unpaddedCoerceElts.push_back(field1Ty);

  if (!field2Ty) {
    auto coerceToType = RecordType::get(ctx, coerceElts,
                                        /*packed=*/!field1Off.isZero(),
                                        /*padded=*/false,
                                        RecordType::RecordKind::Struct);
    return ABIArgInfo::getCoerceAndExpand(coerceToType, unpaddedCoerceElts[0]);
  }

  const cir::CIRDataLayout &dl = getDataLayout();
  clang::CharUnits field2Align =
      clang::CharUnits::fromQuantity(dl.getABITypeAlign(field2Ty).value());
  clang::CharUnits field1End =
      field1Off +
      clang::CharUnits::fromQuantity(dl.getTypeStoreSize(field1Ty));
  clang::CharUnits field2OffNoPadNoPack = field1End.alignTo(field2Align);

  clang::CharUnits padding = clang::CharUnits::Zero();
  if (field2Off > field2OffNoPadNoPack)
    padding = field2Off - field2OffNoPadNoPack;
  else if (field2Off != field2Align && field2Off > field1End)
    padding = field2Off - field1End;

  bool isPacked = !field2Off.isMultipleOf(field2Align);

  if (!padding.isZero())
    coerceElts.push_back(ArrayType::get(i8Ty, padding.getQuantity()));

  coerceElts.push_back(field2Ty);
  unpaddedCoerceElts.push_back(field2Ty);

  auto coerceToType = RecordType::get(ctx, coerceElts, isPacked,
                                      /*padded=*/false,
                                      RecordType::RecordKind::Struct);
  auto unpaddedCoerceToType =
      RecordType::get(ctx, unpaddedCoerceElts, isPacked, /*padded=*/false,
                      RecordType::RecordKind::Struct);

  return ABIArgInfo::getCoerceAndExpand(coerceToType, unpaddedCoerceToType);
}

ABIArgInfo RISCVABIInfo::classifyArgumentType(mlir::Type ty, bool isFixed,
                                              bool isBitInt, int &argGPRsLeft,
                                              int &argFPRsLeft) const {
  cir_cconv_assert(argGPRsLeft <= NumArgGPRs && "Arg GPR tracking underflow");
  ty = useFirstFieldIfTransparentUnion(ty);

  // Structures with either a non-trivial destructor or a non-trivial
  // copy constructor are always passed indirectly.
  if (CIRCXXABI::RecordArgABI RAA = getRecordArgABI(ty, getCXXABI())) {
    if (argGPRsLeft)
      argGPRsLeft -= 1;
    return getNaturalAlignIndirect(
        ty, /*ByVal=*/RAA == CIRCXXABI::RAA_DirectInMemory);
  }

  uint64_t size = getContext().getTypeSize(ty);

  // Ignore empty structs/unions whose size is zero. According to the calling
  // convention empty structs/unions are required to be sized types in C++.
  if (isEmptyRecord(ty, /*allowArrays=*/true) && size == 0)
    return ABIArgInfo::getIgnore();

  // Pass floating point values via FPRs if possible.
  if (isFixed && isFPCIRType(ty) && FLen >= size && argFPRsLeft) {
    argFPRsLeft--;
    return ABIArgInfo::getDirect();
  }

  // Complex types for the hard float ABI must be passed direct rather than
  // using CoerceAndExpand. Classic CodeGen returns a bare direct and relies on
  // complex-expansion to split the value into two FP registers; the CIR
  // framework only expands a Direct value when its coerce type is a record, so
  // coerce to a two-element {eltTy, eltTy} record to obtain the same
  // (float,float)/(double,double) argument shape and {..}-aggregate return.
  if (isFixed) {
    if (auto complexTy = mlir::dyn_cast<ComplexType>(ty);
        complexTy && FLen && argFPRsLeft >= 2) {
      mlir::Type eltTy = complexTy.getElementType();
      if (getContext().getTypeSize(eltTy) <= FLen) {
        argFPRsLeft -= 2;
        auto coerceTy =
            RecordType::get(LT.getMLIRContext(), {eltTy, eltTy},
                            /*packed=*/false, /*padded=*/false,
                            RecordType::RecordKind::Struct);
        return ABIArgInfo::getDirect(coerceTy);
      }
    }
  }

  if (isFixed && FLen && mlir::isa<RecordType>(ty) &&
      !mlir::cast<RecordType>(ty).isUnion()) {
    mlir::Type field1Ty;
    mlir::Type field2Ty;
    clang::CharUnits field1Off = clang::CharUnits::Zero();
    clang::CharUnits field2Off = clang::CharUnits::Zero();
    int neededArgGPRs = 0;
    int neededArgFPRs = 0;
    bool isCandidate =
        detectFPCCEligibleStruct(ty, field1Ty, field1Off, field2Ty, field2Off,
                                 neededArgGPRs, neededArgFPRs);
    if (isCandidate && neededArgGPRs <= argGPRsLeft &&
        neededArgFPRs <= argFPRsLeft) {
      argGPRsLeft -= neededArgGPRs;
      argFPRsLeft -= neededArgFPRs;
      return coerceAndExpandFPCCEligibleStruct(field1Ty, field1Off, field2Ty,
                                               field2Off);
    }
  }

  uint64_t neededAlign = getContext().getTypeAlign(ty);
  // Determine the number of GPRs needed to pass the current argument
  // according to the ABI. 2*XLen-aligned varargs are passed in "aligned"
  // register pairs, so may consume 3 registers.
  int neededArgGPRs = 1;
  if (!isFixed && neededAlign == 2 * XLen)
    neededArgGPRs = 2 + (EABI && XLen == 32 ? 0 : (argGPRsLeft % 2));
  else if (size > XLen && size <= 2 * XLen)
    neededArgGPRs = 2;

  if (neededArgGPRs > argGPRsLeft)
    neededArgGPRs = argGPRsLeft;

  argGPRsLeft -= neededArgGPRs;

  if (!isAggregateTypeForABI(ty) && !mlir::isa<VectorType>(ty)) {
    // NOTE(cir): Enum types are already lowered to their underlying integer in
    // CIR, so there is no enum handling here.

    // _BitInt(N): CIR represents _BitInt as IntType, indistinguishable from a
    // fundamental integer of the same width, so the BitInt provenance flag
    // (threaded from the FuncOp's cir.bitint_args / cir.ret.bitint markers)
    // selects this path. Mirrors classic RISCV.cpp's BitIntType handling.
    if (isBitInt) {
      auto intTy = mlir::cast<IntType>(ty);
      unsigned numBits = intTy.getWidth();
      if (XLen == 64 && numBits == 32)
        return extendType(ty, ty);
      if (numBits <= 2 * XLen)
        return ABIArgInfo::getExtend(ty, ty);
      return getNaturalAlignIndirect(ty, /*ByVal=*/false);
    }

    // All integral types (and bool) narrower than XLen are promoted to XLen.
    if (size < XLen && (mlir::isa<IntType>(ty) || mlir::isa<BoolType>(ty)))
      return extendType(ty, ty);

    return ABIArgInfo::getDirect();
  }

  // NOTE(cir): RVV fixed-length-vector (VLS) calling-convention handling is not
  // modeled; the default riscv targets used here do not select it.

  // Aggregates which are <= 2*XLen will be passed in registers if possible,
  // so coerce to integers.
  if (size <= 2 * XLen) {
    mlir::MLIRContext *ctx = LT.getMLIRContext();
    unsigned alignment = getContext().getTypeAlign(ty);

    // Use a single XLen int if possible, 2*XLen if 2*XLen alignment is
    // required, and a 2-element XLen array if only XLen alignment is required.
    if (size <= XLen) {
      return ABIArgInfo::getDirect(IntType::get(ctx, XLen, /*isSigned=*/false));
    } else if (alignment == 2 * XLen) {
      return ABIArgInfo::getDirect(
          IntType::get(ctx, 2 * XLen, /*isSigned=*/false));
    } else {
      return ABIArgInfo::getDirect(
          ArrayType::get(IntType::get(ctx, XLen, /*isSigned=*/false), 2));
    }
  }
  return getNaturalAlignIndirect(ty, /*ByVal=*/false);
}

ABIArgInfo RISCVABIInfo::classifyReturnType(mlir::Type retTy,
                                            bool isBitInt) const {
  if (mlir::isa<VoidType>(retTy))
    return ABIArgInfo::getIgnore();

  int argGPRsLeft = 2;
  int argFPRsLeft = FLen ? 2 : 0;

  // The rules for return and argument types are the same, so defer to
  // classifyArgumentType.
  return classifyArgumentType(retTy, /*isFixed=*/true, isBitInt, argGPRsLeft,
                              argFPRsLeft);
}

ABIArgInfo RISCVABIInfo::extendType(mlir::Type ty, mlir::Type coerceTy) const {
  unsigned tySize = getContext().getTypeSize(ty);
  // RV64 ABI requires unsigned 32 bit integers to be sign extended.
  if (auto intTy = mlir::dyn_cast<IntType>(ty))
    if (XLen == 64 && intTy.isUnsigned() && tySize == 32)
      return ABIArgInfo::getSignExtend(ty, coerceTy);
  return ABIArgInfo::getExtend(ty, coerceTy);
}

class RISCVTargetLoweringInfo : public TargetLoweringInfo {
public:
  RISCVTargetLoweringInfo(LowerTypes &LT, unsigned XLen, unsigned FLen,
                          bool EABI)
      : TargetLoweringInfo(
            std::make_unique<RISCVABIInfo>(LT, XLen, FLen, EABI)) {}

  unsigned getTargetAddrSpaceFromCIRAddrSpace(
      cir::LangAddressSpace addrSpace) const override {
    // RISC-V uses a flat address space model; all address spaces map to 0.
    return 0;
  }
};

} // namespace

std::unique_ptr<TargetLoweringInfo>
createRISCVTargetLoweringInfo(LowerModule &lowerModule, unsigned XLen,
                              unsigned FLen, bool EABI) {
  return std::make_unique<RISCVTargetLoweringInfo>(lowerModule.getTypes(), XLen,
                                                   FLen, EABI);
}

} // namespace cir
