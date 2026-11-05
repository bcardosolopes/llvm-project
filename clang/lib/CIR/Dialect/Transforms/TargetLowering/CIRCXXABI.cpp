//===- CIRCXXABI.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file partially mimics clang/lib/CodeGen/CGCXXABI.cpp. The queries are
// adapted to operate on the CIR dialect, however.
//
//===----------------------------------------------------------------------===//

#include "CIRCXXABI.h"
#include "LowerModule.h"
#include "llvm/Support/ErrorHandling.h"

namespace cir {

CIRCXXABI::~CIRCXXABI() {}

namespace {

class CIRCABI final : public CIRCXXABI {
public:
  explicit CIRCABI(LowerModule &LM) : CIRCXXABI(LM) {}

  bool classifyReturnType(LowerFunctionInfo &FI) const override {
    return false;
  }

  RecordArgABI getRecordArgABI(const RecordType RD) const override {
    return RAA_Default;
  }

  mlir::Type getDataMemberABIType() const override {
    llvm_unreachable("C data-member pointers are not supported");
  }

  mlir::Type getMethodABIType() const override {
    llvm_unreachable("C member-function pointers are not supported");
  }

  mlir::TypedAttr
  lowerDataMemberConstant(cir::DataMemberAttr attr,
                          const mlir::DataLayout &layout,
                          const mlir::TypeConverter &typeConverter) const override {
    llvm_unreachable("C data-member pointers are not supported");
  }

  mlir::TypedAttr
  lowerMethodConstant(cir::MethodAttr attr, const mlir::DataLayout &layout,
                      const mlir::TypeConverter &typeConverter) const override {
    llvm_unreachable("C member-function pointers are not supported");
  }

  mlir::Operation *
  lowerGetRuntimeMember(cir::GetRuntimeMemberOp op, mlir::Type loweredResultTy,
                        mlir::Value loweredAddr, mlir::Value loweredMember,
                        mlir::OpBuilder &builder) const override {
    llvm_unreachable("C member-function pointers are not supported");
  }

  void lowerGetMethod(cir::GetMethodOp op, mlir::Value (&loweredResults)[3],
                      mlir::Value loweredMethod, mlir::Value loweredObjectPtr,
                      mlir::ConversionPatternRewriter &rewriter) const override {
    llvm_unreachable("C member-function pointers are not supported");
  }

  mlir::Value lowerBaseDataMember(cir::BaseDataMemberOp op,
                                  mlir::Value loweredSrc,
                                  mlir::OpBuilder &builder) const override {
    llvm_unreachable("C data-member pointers are not supported");
  }

  mlir::Value
  lowerDerivedDataMember(cir::DerivedDataMemberOp op, mlir::Value loweredSrc,
                         mlir::OpBuilder &builder) const override {
    llvm_unreachable("C data-member pointers are not supported");
  }

  mlir::Value lowerBaseMethod(cir::BaseMethodOp op, mlir::Value loweredSrc,
                              mlir::OpBuilder &builder) const override {
    llvm_unreachable("C member-function pointers are not supported");
  }

  mlir::Value lowerDerivedMethod(cir::DerivedMethodOp op,
                                 mlir::Value loweredSrc,
                                 mlir::OpBuilder &builder) const override {
    llvm_unreachable("C member-function pointers are not supported");
  }

  mlir::Value lowerDataMemberCmp(cir::CmpOp op, mlir::Value loweredLhs,
                                 mlir::Value loweredRhs,
                                 mlir::OpBuilder &builder) const override {
    llvm_unreachable("C data-member pointers are not supported");
  }

  mlir::Value lowerMethodCmp(cir::CmpOp op, mlir::Value loweredLhs,
                             mlir::Value loweredRhs,
                             mlir::OpBuilder &builder) const override {
    llvm_unreachable("C member-function pointers are not supported");
  }

  mlir::Value
  lowerDataMemberBitcast(cir::CastOp op, mlir::Type loweredDstTy,
                         mlir::Value loweredSrc,
                         mlir::OpBuilder &builder) const override {
    llvm_unreachable("C data-member pointers are not supported");
  }

  mlir::Value lowerDataMemberToBoolCast(cir::CastOp op, mlir::Value loweredSrc,
                                        mlir::OpBuilder &builder) const override {
    llvm_unreachable("C data-member pointers are not supported");
  }

  mlir::Value lowerMethodBitcast(cir::CastOp op, mlir::Type loweredDstTy,
                                 mlir::Value loweredSrc,
                                 mlir::OpBuilder &builder) const override {
    llvm_unreachable("C member-function pointers are not supported");
  }

  mlir::Value lowerMethodToBoolCast(cir::CastOp op, mlir::Value loweredSrc,
                                    mlir::OpBuilder &builder) const override {
    llvm_unreachable("C member-function pointers are not supported");
  }
};

} // namespace

CIRCXXABI *CreateCABI(LowerModule &LM) { return new CIRCABI(LM); }

} // namespace cir
