//===----- MicrosoftCXXABI.cpp - CIR lowering for Microsoft C++ ABI ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides CIR lowering logic targeting the Microsoft C++ ABI.
//
//===----------------------------------------------------------------------===//

#include "CIRCXXABI.h"
#include "LowerModule.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/CIR/ABIArgInfo.h"
#include "llvm/Support/ErrorHandling.h"

namespace cir {

namespace {

// Mirror the AArch64 HVA detection used by CIR's AArch64 ABI lowering so the
// Microsoft C++ ABI can honor the same vector-return exemption as OGCG.
static bool isHomogeneousAggregate(mlir::Type Ty, mlir::Type &Base,
                                   uint64_t &Members,
                                   const CIRLowerContext &ctx) {
  if (auto CT = mlir::dyn_cast<ComplexType>(Ty)) {
    mlir::Type EltTy = CT.getElementType();
    if (!mlir::isa<SingleType, DoubleType, LongDoubleType, FP16Type, BF16Type>(
            EltTy))
      return false;
    Members = 2;
    if (!Base)
      Base = EltTy;
    else if (Base != EltTy)
      return false;
    return true;
  }

  auto RT = mlir::dyn_cast<RecordType>(Ty);
  if (!RT)
    return false;

  auto recordMembers = RT.getMembers();
  if (recordMembers.empty())
    return false;

  bool isUnion = RT.isUnion();
  Members = 0;
  for (auto member : recordMembers) {
    uint64_t fieldMembers = 0;
    if (mlir::isa<SingleType, DoubleType, LongDoubleType, FP16Type, BF16Type>(
            member)) {
      if (!Base)
        Base = member;
      else if (Base != member)
        return false;
      fieldMembers = 1;
    } else if (auto vecTy = mlir::dyn_cast<VectorType>(member)) {
      if (vecTy.getIsScalable() || vecTy.getIsSveFixedLength())
        return false;
      uint64_t vecSize = ctx.getTypeSize(vecTy);
      if (vecSize != 64 && vecSize != 128)
        return false;
      if (!Base) {
        Base = member;
      } else if (auto baseVec = mlir::dyn_cast<VectorType>(Base)) {
        if (ctx.getTypeSize(baseVec) != vecSize)
          return false;
      } else {
        return false;
      }
      fieldMembers = 1;
    } else if (mlir::isa<RecordType, ComplexType>(member)) {
      mlir::Type nestedBase = {};
      uint64_t nestedMembers = 0;
      if (!isHomogeneousAggregate(member, nestedBase, nestedMembers, ctx))
        return false;
      if (!Base) {
        Base = nestedBase;
      } else if (mlir::isa<VectorType>(Base) &&
                 mlir::isa<VectorType>(nestedBase)) {
        if (ctx.getTypeSize(Base) != ctx.getTypeSize(nestedBase))
          return false;
      } else if (Base != nestedBase) {
        return false;
      }
      fieldMembers = nestedMembers;
    } else if (auto arrTy = mlir::dyn_cast<ArrayType>(member)) {
      mlir::Type eltTy = arrTy.getElementType();
      uint64_t totalElts = arrTy.getSize();
      while (auto nestedArr = mlir::dyn_cast<ArrayType>(eltTy)) {
        totalElts *= nestedArr.getSize();
        eltTy = nestedArr.getElementType();
      }

      if (mlir::isa<SingleType, DoubleType, LongDoubleType, FP16Type,
                    BF16Type>(eltTy)) {
        if (!Base)
          Base = eltTy;
        else if (Base != eltTy)
          return false;
        fieldMembers = totalElts;
      } else if (auto vecEltTy = mlir::dyn_cast<VectorType>(eltTy)) {
        if (vecEltTy.getIsScalable() || vecEltTy.getIsSveFixedLength())
          return false;
        uint64_t vecSize = ctx.getTypeSize(vecEltTy);
        if (vecSize != 64 && vecSize != 128)
          return false;
        if (!Base) {
          Base = eltTy;
        } else if (auto baseVec = mlir::dyn_cast<VectorType>(Base)) {
          if (ctx.getTypeSize(baseVec) != vecSize)
            return false;
        } else {
          return false;
        }
        fieldMembers = totalElts;
      } else {
        return false;
      }
    } else {
      return false;
    }

    if (isUnion) {
      if (Members && Members != fieldMembers)
        return false;
      Members = fieldMembers;
    } else {
      Members += fieldMembers;
    }

    if (Members > 4)
      return false;
  }

  return Members != 0;
}

static bool isTrivialForMSVC(const clang::CXXRecordDecl *RD, RecordType Ty,
                             LowerModule &LM) {
  mlir::Type base = {};
  uint64_t numElts = 0;
  if (LM.getTarget().getTriple().isAArch64() &&
      isHomogeneousAggregate(Ty, base, numElts, LM.getContext()) &&
      mlir::isa<VectorType>(base)) {
    return true;
  }

  if (RD->hasProtectedFields() || RD->hasPrivateFields())
    return false;
  if (RD->getNumBases() > 0)
    return false;
  if (RD->isPolymorphic())
    return false;
  if (RD->hasNonTrivialCopyAssignment())
    return false;
  if (RD->needsImplicitCopyAssignment() && !RD->hasSimpleCopyAssignment())
    return false;

  for (const clang::Decl *D : RD->decls()) {
    if (auto *Ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(D)) {
      if (Ctor->isUserProvided())
        return false;
    } else if (auto *Template = llvm::dyn_cast<clang::FunctionTemplateDecl>(D)) {
      if (llvm::isa<clang::CXXConstructorDecl>(Template->getTemplatedDecl()))
        return false;
    } else if (auto *MethodDecl = llvm::dyn_cast<clang::CXXMethodDecl>(D)) {
      if (MethodDecl->isCopyAssignmentOperator() && MethodDecl->isDeleted())
        return false;
    }
  }

  if (RD->hasNonTrivialDestructor())
    return false;
  return true;
}

static unsigned getASTRecordAlignBits(const clang::RecordDecl *RD) {
  const clang::RecordDecl *Def = RD->getDefinition();
  cir_cconv_assert(Def && "expected complete record");
  const clang::ASTRecordLayout &layout =
      Def->getASTContext().getASTRecordLayout(Def);
  return layout.getAlignment().getQuantity() * 8;
}

class MicrosoftCXXABI final : public CIRCXXABI {
public:
  explicit MicrosoftCXXABI(LowerModule &LM) : CIRCXXABI(LM) {}

  bool classifyReturnType(LowerFunctionInfo &FI) const override {
    const auto RDTy = mlir::dyn_cast<RecordType>(FI.getReturnType());
    if (!RDTy)
      return false;

    auto ast = RDTy.getAst();
    if (!ast)
      return false;

    const auto *RD = llvm::dyn_cast<clang::CXXRecordDecl>(ast.getRawDecl());
    if (!RD)
      return false;

    bool isTrivialForABI =
        RD->canPassInRegisters() && isTrivialForMSVC(RD, RDTy, LM);
    bool isIndirectReturn = !isTrivialForABI || FI.isInstanceMethod();
    if (!isIndirectReturn)
      return false;

    unsigned alignBytes = getASTRecordAlignBits(RD) / 8;
    FI.getReturnInfo() =
        cir::ABIArgInfo::getIndirect(alignBytes, /*byVal=*/false);
    FI.getReturnInfo().setSRetAfterThis(FI.isInstanceMethod());
    FI.getReturnInfo().setInReg(LM.getTarget().getTriple().isAArch64());
    return true;
  }

  RecordArgABI getRecordArgABI(const RecordType RDTy) const override {
    auto ast = RDTy.getAst();
    if (!ast)
      return RAA_Default;

    const auto *RD = llvm::dyn_cast<clang::CXXRecordDecl>(ast.getRawDecl());
    if (!RD)
      return RAA_Default;

    if (RD->canPassInRegisters())
      return RAA_Default;

    switch (LM.getTarget().getTriple().getArch()) {
    default:
      return RAA_Indirect;
    case llvm::Triple::thumb:
      return RAA_Indirect;
    case llvm::Triple::x86: {
      clang::QualType recordTy =
          RD->getASTContext().getCanonicalTagType(const_cast<clang::CXXRecordDecl *>(RD));
      clang::TypeInfo info = RD->getASTContext().getTypeInfo(recordTy);
      if (info.isAlignRequired() && info.Align > 32)
        return RAA_Indirect;
      return RAA_DirectInMemory;
    }
    case llvm::Triple::x86_64:
    case llvm::Triple::aarch64:
      return RAA_Indirect;
    }
  }

  mlir::Type getDataMemberABIType() const override {
    llvm_unreachable(
        "Microsoft data-member pointers are not yet supported in CIR target "
        "lowering");
  }

  mlir::Type getMethodABIType() const override {
    llvm_unreachable(
        "Microsoft member-function pointers are not yet supported in CIR "
        "target lowering");
  }

  mlir::TypedAttr
  lowerDataMemberConstant(cir::DataMemberAttr, const mlir::DataLayout &,
                          const mlir::TypeConverter &) const override {
    llvm_unreachable(
        "Microsoft data-member pointers are not yet supported in CIR target "
        "lowering");
  }

  mlir::TypedAttr
  lowerMethodConstant(cir::MethodAttr, const mlir::DataLayout &,
                      const mlir::TypeConverter &) const override {
    llvm_unreachable(
        "Microsoft member-function pointers are not yet supported in CIR "
        "target lowering");
  }

  mlir::Operation *
  lowerGetRuntimeMember(cir::GetRuntimeMemberOp, mlir::Type, mlir::Value,
                        mlir::Value, mlir::OpBuilder &) const override {
    llvm_unreachable(
        "Microsoft member-function pointers are not yet supported in CIR "
        "target lowering");
  }

  void lowerGetMethod(cir::GetMethodOp, mlir::Value (&)[3], mlir::Value,
                      mlir::Value,
                      mlir::ConversionPatternRewriter &) const override {
    llvm_unreachable(
        "Microsoft member-function pointers are not yet supported in CIR "
        "target lowering");
  }

  mlir::Value lowerBaseDataMember(cir::BaseDataMemberOp, mlir::Value,
                                  mlir::OpBuilder &) const override {
    llvm_unreachable(
        "Microsoft data-member pointers are not yet supported in CIR target "
        "lowering");
  }

  mlir::Value lowerDerivedDataMember(cir::DerivedDataMemberOp, mlir::Value,
                                     mlir::OpBuilder &) const override {
    llvm_unreachable(
        "Microsoft data-member pointers are not yet supported in CIR target "
        "lowering");
  }

  mlir::Value lowerBaseMethod(cir::BaseMethodOp, mlir::Value,
                              mlir::OpBuilder &) const override {
    llvm_unreachable(
        "Microsoft member-function pointers are not yet supported in CIR "
        "target lowering");
  }

  mlir::Value lowerDerivedMethod(cir::DerivedMethodOp, mlir::Value,
                                 mlir::OpBuilder &) const override {
    llvm_unreachable(
        "Microsoft member-function pointers are not yet supported in CIR "
        "target lowering");
  }

  mlir::Value lowerDataMemberCmp(cir::CmpOp, mlir::Value, mlir::Value,
                                 mlir::OpBuilder &) const override {
    llvm_unreachable(
        "Microsoft data-member pointers are not yet supported in CIR target "
        "lowering");
  }

  mlir::Value lowerMethodCmp(cir::CmpOp, mlir::Value, mlir::Value,
                             mlir::OpBuilder &) const override {
    llvm_unreachable(
        "Microsoft member-function pointers are not yet supported in CIR "
        "target lowering");
  }

  mlir::Value lowerDataMemberBitcast(cir::CastOp, mlir::Type, mlir::Value,
                                     mlir::OpBuilder &) const override {
    llvm_unreachable(
        "Microsoft data-member pointers are not yet supported in CIR target "
        "lowering");
  }

  mlir::Value lowerDataMemberToBoolCast(cir::CastOp, mlir::Value,
                                        mlir::OpBuilder &) const override {
    llvm_unreachable(
        "Microsoft data-member pointers are not yet supported in CIR target "
        "lowering");
  }

  mlir::Value lowerMethodBitcast(cir::CastOp, mlir::Type, mlir::Value,
                                 mlir::OpBuilder &) const override {
    llvm_unreachable(
        "Microsoft member-function pointers are not yet supported in CIR "
        "target lowering");
  }

  mlir::Value lowerMethodToBoolCast(cir::CastOp, mlir::Value,
                                    mlir::OpBuilder &) const override {
    llvm_unreachable(
        "Microsoft member-function pointers are not yet supported in CIR "
        "target lowering");
  }
};

} // namespace

CIRCXXABI *CreateMicrosoftCXXABI(LowerModule &LM) {
  return new MicrosoftCXXABI(LM);
}

} // namespace cir
