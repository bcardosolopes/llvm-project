//===- LoweringPrepare.cpp - pareparation work for LLVM lowering ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LoweringPrepareCXXABI.h"
#include "PassDetail.h"
#include "mlir/IR/Attributes.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/Cuda.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CIR/Dialect/Builder/CIRBaseBuilder.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDataLayout.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/MissingFeatures.h"
#include "clang/CIR/Target/AArch64.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

#include <memory>

using namespace mlir;
using namespace cir;

static std::string getCUDAPrefix(clang::ASTContext *astCtx) {
  if (astCtx->getLangOpts().HIP)
    return "hip";
  return "cuda";
}

static std::string addUnderscoredPrefix(llvm::StringRef cudaPrefix,
                                        llvm::StringRef cudaFunctionName) {
  return ("__" + cudaPrefix + cudaFunctionName).str();
}

namespace mlir {
#define GEN_PASS_DEF_LOWERINGPREPARE
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

static SmallString<128> getTransformedFileName(mlir::ModuleOp mlirModule) {
  SmallString<128> fileName;

  if (mlirModule.getSymName())
    fileName = llvm::sys::path::filename(mlirModule.getSymName()->str());

  if (fileName.empty())
    fileName = "<null>";

  for (size_t i = 0; i < fileName.size(); ++i) {
    // Replace everything that's not [a-zA-Z0-9._] with a _. This set happens
    // to be the set of C preprocessing numbers.
    if (!clang::isPreprocessingNumberBody(fileName[i]))
      fileName[i] = '_';
  }

  return fileName;
}

/// Return the FuncOp called by `callOp`.
static cir::FuncOp getCalledFunction(cir::CallOp callOp) {
  mlir::SymbolRefAttr sym = llvm::dyn_cast_if_present<mlir::SymbolRefAttr>(
      callOp.getCallableForCallee());
  if (!sym)
    return nullptr;
  return dyn_cast_or_null<cir::FuncOp>(
      mlir::SymbolTable::lookupNearestSymbolFrom(callOp, sym));
}

namespace {
struct LoweringPreparePass
    : public impl::LoweringPrepareBase<LoweringPreparePass> {
  LoweringPreparePass() = default;
  void runOnOperation() override;

  void runOnOp(mlir::Operation *op);
  void lowerCastOp(cir::CastOp op);
  void lowerComplexAddOp(cir::ComplexAddOp op);
  void lowerComplexSubOp(cir::ComplexSubOp op);
  void lowerComplexDivOp(cir::ComplexDivOp op);
  void lowerComplexMulOp(cir::ComplexMulOp op);
  void lowerUnaryOp(cir::UnaryOp op);
  void lowerGlobalOp(cir::GlobalOp op);
  void lowerDynamicCastOp(cir::DynamicCastOp op);
  void lowerArrayDtor(cir::ArrayDtor op);
  void lowerArrayCtor(cir::ArrayCtor op);
  void lowerTrivialCopyCall(cir::CallOp op);
  void lowerStdFindOp(cir::StdFindOp op);
  void lowerStrLenOp(cir::StrLenOp op);
  void lowerIterBeginOp(cir::IterBeginOp op);
  void lowerIterEndOp(cir::IterEndOp op);
  void lowerThreeWayCmpOp(cir::CmpThreeWayOp op);
  void lowerThrowOp(cir::ThrowOp op);
  void lowerVAArgOp(cir::VAArgOp op);

  /// Build the function that initializes the specified global
  cir::FuncOp buildCXXGlobalVarDeclInitFunc(cir::GlobalOp op);

  /// Handle the dtor region by registering destructor with __cxa_atexit
  cir::FuncOp getOrCreateDtorFunc(CIRBaseBuilderTy &builder, cir::GlobalOp op,
                                  mlir::Region &dtorRegion,
                                  cir::CallOp &dtorCall);

  /// Build a module init function that calls all the dynamic initializers.
  void buildCXXGlobalInitFunc();

  /// Materialize global ctor/dtor list
  void buildGlobalCtorDtorList();

  /// Collect annotations from a global or function into the module-level list
  void addGlobalAnnotations(mlir::Operation *op, mlir::ArrayAttr annotations);

  /// Materialize the global annotations attribute on the module
  void buildGlobalAnnotationValues();

  ///
  /// CUDA related
  /// ------------

  // Maps CUDA kernel name to device stub function.
  llvm::StringMap<cir::FuncOp> cudaKernelMap;
  // Maps CUDA device-side variable name to host-side (shadow) GlobalOp.
  llvm::StringMap<cir::GlobalOp> cudaVarMap;

  void buildCUDAModuleCtor();
  std::optional<cir::FuncOp> buildCUDAModuleDtor();
  std::optional<cir::FuncOp> buildHIPModuleDtor();
  std::optional<cir::FuncOp> buildCUDARegisterGlobals();

  void buildCUDARegisterGlobalFunctions(cir::CIRBaseBuilderTy &builder,
                                        cir::FuncOp regGlobalFunc);
  void buildCUDARegisterVars(cir::CIRBaseBuilderTy &builder,
                             cir::FuncOp regGlobalFunc);

  cir::FuncOp buildRuntimeFunction(
      mlir::OpBuilder &builder, llvm::StringRef name, mlir::Location loc,
      cir::FuncType type,
      cir::GlobalLinkageKind linkage = cir::GlobalLinkageKind::ExternalLinkage);

  cir::GlobalOp buildRuntimeVariable(
      mlir::OpBuilder &builder, llvm::StringRef name, mlir::Location loc,
      mlir::Type type,
      cir::GlobalLinkageKind linkage = cir::GlobalLinkageKind::ExternalLinkage,
      cir::VisibilityKind visibility = cir::VisibilityKind::Default);

  ///
  /// AST related
  /// -----------

  clang::ASTContext *astCtx;

  // Helper for lowering C++ ABI specific operations.
  std::shared_ptr<cir::LoweringPrepareCXXABI> cxxABI;

  /// Tracks current module.
  mlir::ModuleOp mlirModule;

  /// Data layout for type size queries.
  std::optional<cir::CIRDataLayout> datalayout;

  /// Tracks existing dynamic initializers.
  llvm::StringMap<uint32_t> dynamicInitializerNames;
  llvm::SmallVector<cir::FuncOp> dynamicInitializers;

  /// List of ctors and their priorities to be called before main()
  llvm::SmallVector<std::pair<std::string, uint32_t>, 4> globalCtorList;
  /// List of dtors and their priorities to be called when unloading module.
  llvm::SmallVector<std::pair<std::string, uint32_t>, 4> globalDtorList;
  /// List of annotations in the module
  llvm::SmallVector<mlir::Attribute, 4> globalAnnotations;

  void setASTContext(clang::ASTContext *c) {
    astCtx = c;
    const clang::TargetInfo &target = c->getTargetInfo();
    switch (c->getCXXABIKind()) {
    case clang::TargetCXXABI::GenericItanium:
      if (target.getTriple().getArch() == llvm::Triple::x86_64) {
        cxxABI.reset(cir::LoweringPrepareCXXABI::createX86ABI(/*is64bit=*/true));
        break;
      }
      cxxABI.reset(cir::LoweringPrepareCXXABI::createItaniumABI());
      break;
    case clang::TargetCXXABI::GenericAArch64:
    case clang::TargetCXXABI::AppleARM64:
      cxxABI.reset(cir::LoweringPrepareCXXABI::createAArch64ABI(
          cir::AArch64ABIKind::AAPCS));
      break;
    default:
      llvm_unreachable("NYI");
    }
  }
};

} // namespace

cir::GlobalOp LoweringPreparePass::buildRuntimeVariable(
    mlir::OpBuilder &builder, llvm::StringRef name, mlir::Location loc,
    mlir::Type type, cir::GlobalLinkageKind linkage,
    cir::VisibilityKind visibility) {
  cir::GlobalOp g = dyn_cast_or_null<cir::GlobalOp>(
      mlir::SymbolTable::lookupNearestSymbolFrom(
          mlirModule, mlir::StringAttr::get(mlirModule->getContext(), name)));
  if (!g) {
    g = cir::GlobalOp::create(builder, loc, name, type);
    g.setLinkageAttr(
        cir::GlobalLinkageKindAttr::get(builder.getContext(), linkage));
    mlir::SymbolTable::setSymbolVisibility(
        g, mlir::SymbolTable::Visibility::Private);
    g.setGlobalVisibilityAttr(
        cir::VisibilityAttr::get(builder.getContext(), visibility));
  }
  return g;
}

cir::FuncOp LoweringPreparePass::buildRuntimeFunction(
    mlir::OpBuilder &builder, llvm::StringRef name, mlir::Location loc,
    cir::FuncType type, cir::GlobalLinkageKind linkage) {
  cir::FuncOp f = dyn_cast_or_null<FuncOp>(SymbolTable::lookupNearestSymbolFrom(
      mlirModule, StringAttr::get(mlirModule->getContext(), name)));
  if (!f) {
    f = cir::FuncOp::create(builder, loc, name, type);
    f.setLinkageAttr(
        cir::GlobalLinkageKindAttr::get(builder.getContext(), linkage));
    mlir::SymbolTable::setSymbolVisibility(
        f, mlir::SymbolTable::Visibility::Private);

    assert(!cir::MissingFeatures::opFuncExtraAttrs());
  }
  return f;
}

static mlir::Value lowerScalarToComplexCast(mlir::MLIRContext &ctx,
                                            cir::CastOp op) {
  cir::CIRBaseBuilderTy builder(ctx);
  builder.setInsertionPoint(op);

  mlir::Value src = op.getSrc();
  mlir::Value imag = builder.getNullValue(src.getType(), op.getLoc());
  return builder.createComplexCreate(op.getLoc(), src, imag);
}

static mlir::Value lowerComplexToScalarCast(mlir::MLIRContext &ctx,
                                            cir::CastOp op,
                                            cir::CastKind elemToBoolKind) {
  cir::CIRBaseBuilderTy builder(ctx);
  builder.setInsertionPoint(op);

  mlir::Value src = op.getSrc();
  if (!mlir::isa<cir::BoolType>(op.getType()))
    return builder.createComplexReal(op.getLoc(), src);

  // Complex cast to bool: (bool)(a+bi) => (bool)a || (bool)b
  mlir::Value srcReal = builder.createComplexReal(op.getLoc(), src);
  mlir::Value srcImag = builder.createComplexImag(op.getLoc(), src);

  cir::BoolType boolTy = builder.getBoolTy();
  mlir::Value srcRealToBool =
      builder.createCast(op.getLoc(), elemToBoolKind, srcReal, boolTy);
  mlir::Value srcImagToBool =
      builder.createCast(op.getLoc(), elemToBoolKind, srcImag, boolTy);
  return builder.createLogicalOr(op.getLoc(), srcRealToBool, srcImagToBool);
}

static mlir::Value lowerComplexToComplexCast(mlir::MLIRContext &ctx,
                                             cir::CastOp op,
                                             cir::CastKind scalarCastKind) {
  CIRBaseBuilderTy builder(ctx);
  builder.setInsertionPoint(op);

  mlir::Value src = op.getSrc();
  auto dstComplexElemTy =
      mlir::cast<cir::ComplexType>(op.getType()).getElementType();

  mlir::Value srcReal = builder.createComplexReal(op.getLoc(), src);
  mlir::Value srcImag = builder.createComplexImag(op.getLoc(), src);

  mlir::Value dstReal = builder.createCast(op.getLoc(), scalarCastKind, srcReal,
                                           dstComplexElemTy);
  mlir::Value dstImag = builder.createCast(op.getLoc(), scalarCastKind, srcImag,
                                           dstComplexElemTy);
  return builder.createComplexCreate(op.getLoc(), dstReal, dstImag);
}

void LoweringPreparePass::lowerCastOp(cir::CastOp op) {
  mlir::MLIRContext &ctx = getContext();
  mlir::Value loweredValue = [&]() -> mlir::Value {
    switch (op.getKind()) {
    case cir::CastKind::float_to_complex:
    case cir::CastKind::int_to_complex:
      return lowerScalarToComplexCast(ctx, op);
    case cir::CastKind::float_complex_to_real:
    case cir::CastKind::int_complex_to_real:
      return lowerComplexToScalarCast(ctx, op, op.getKind());
    case cir::CastKind::float_complex_to_bool:
      return lowerComplexToScalarCast(ctx, op, cir::CastKind::float_to_bool);
    case cir::CastKind::int_complex_to_bool:
      return lowerComplexToScalarCast(ctx, op, cir::CastKind::int_to_bool);
    case cir::CastKind::float_complex:
      return lowerComplexToComplexCast(ctx, op, cir::CastKind::floating);
    case cir::CastKind::float_complex_to_int_complex:
      return lowerComplexToComplexCast(ctx, op, cir::CastKind::float_to_int);
    case cir::CastKind::int_complex:
      return lowerComplexToComplexCast(ctx, op, cir::CastKind::integral);
    case cir::CastKind::int_complex_to_float_complex:
      return lowerComplexToComplexCast(ctx, op, cir::CastKind::int_to_float);
    default:
      return nullptr;
    }
  }();

  if (loweredValue) {
    op.replaceAllUsesWith(loweredValue);
    op.erase();
  }
}

void LoweringPreparePass::lowerComplexAddOp(cir::ComplexAddOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op);
  mlir::Location loc = op.getLoc();
  mlir::Value lhs = op.getLhs();
  mlir::Value rhs = op.getRhs();
  mlir::Value lhsReal = builder.createComplexReal(loc, lhs);
  mlir::Value lhsImag = builder.createComplexImag(loc, lhs);
  mlir::Value rhsReal = builder.createComplexReal(loc, rhs);
  mlir::Value rhsImag = builder.createComplexImag(loc, rhs);
  mlir::Value resultReal =
      builder.createBinop(loc, lhsReal, cir::BinOpKind::Add, rhsReal);
  mlir::Value resultImag =
      builder.createBinop(loc, lhsImag, cir::BinOpKind::Add, rhsImag);
  mlir::Value result = builder.createComplexCreate(loc, resultReal, resultImag);
  op.replaceAllUsesWith(result);
  op.erase();
}

void LoweringPreparePass::lowerComplexSubOp(cir::ComplexSubOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op);
  mlir::Location loc = op.getLoc();
  mlir::Value lhs = op.getLhs();
  mlir::Value rhs = op.getRhs();
  mlir::Value lhsReal = builder.createComplexReal(loc, lhs);
  mlir::Value lhsImag = builder.createComplexImag(loc, lhs);
  mlir::Value rhsReal = builder.createComplexReal(loc, rhs);
  mlir::Value rhsImag = builder.createComplexImag(loc, rhs);
  mlir::Value resultReal =
      builder.createBinop(loc, lhsReal, cir::BinOpKind::Sub, rhsReal);
  mlir::Value resultImag =
      builder.createBinop(loc, lhsImag, cir::BinOpKind::Sub, rhsImag);
  mlir::Value result = builder.createComplexCreate(loc, resultReal, resultImag);
  op.replaceAllUsesWith(result);
  op.erase();
}

static mlir::Value buildComplexBinOpLibCall(
    LoweringPreparePass &pass, CIRBaseBuilderTy &builder,
    llvm::StringRef (*libFuncNameGetter)(llvm::APFloat::Semantics),
    mlir::Location loc, cir::ComplexType ty, mlir::Value lhsReal,
    mlir::Value lhsImag, mlir::Value rhsReal, mlir::Value rhsImag) {
  cir::FPTypeInterface elementTy =
      mlir::cast<cir::FPTypeInterface>(ty.getElementType());

  llvm::StringRef libFuncName = libFuncNameGetter(
      llvm::APFloat::SemanticsToEnum(elementTy.getFloatSemantics()));
  llvm::SmallVector<mlir::Type, 4> libFuncInputTypes(4, elementTy);

  cir::FuncType libFuncTy = cir::FuncType::get(libFuncInputTypes, ty);

  // Insert a declaration for the runtime function to be used in Complex
  // multiplication and division when needed
  cir::FuncOp libFunc;
  {
    mlir::OpBuilder::InsertionGuard ipGuard{builder};
    builder.setInsertionPointToStart(pass.mlirModule.getBody());
    libFunc = pass.buildRuntimeFunction(builder, libFuncName, loc, libFuncTy);
  }

  cir::CallOp call =
      builder.createCallOp(loc, libFunc, {lhsReal, lhsImag, rhsReal, rhsImag});
  return call.getResult();
}

static llvm::StringRef
getComplexDivLibCallName(llvm::APFloat::Semantics semantics) {
  switch (semantics) {
  case llvm::APFloat::S_IEEEhalf:
    return "__divhc3";
  case llvm::APFloat::S_IEEEsingle:
    return "__divsc3";
  case llvm::APFloat::S_IEEEdouble:
    return "__divdc3";
  case llvm::APFloat::S_PPCDoubleDouble:
    return "__divtc3";
  case llvm::APFloat::S_x87DoubleExtended:
    return "__divxc3";
  case llvm::APFloat::S_IEEEquad:
    return "__divtc3";
  default:
    llvm_unreachable("unsupported floating point type");
  }
}

static mlir::Value
buildAlgebraicComplexDiv(CIRBaseBuilderTy &builder, mlir::Location loc,
                         mlir::Value lhsReal, mlir::Value lhsImag,
                         mlir::Value rhsReal, mlir::Value rhsImag) {
  // (a+bi) / (c+di) = ((ac+bd)/(cc+dd)) + ((bc-ad)/(cc+dd))i
  mlir::Value &a = lhsReal;
  mlir::Value &b = lhsImag;
  mlir::Value &c = rhsReal;
  mlir::Value &d = rhsImag;

  mlir::Value ac = builder.createBinop(loc, a, cir::BinOpKind::Mul, c); // a*c
  mlir::Value bd = builder.createBinop(loc, b, cir::BinOpKind::Mul, d); // b*d
  mlir::Value cc = builder.createBinop(loc, c, cir::BinOpKind::Mul, c); // c*c
  mlir::Value dd = builder.createBinop(loc, d, cir::BinOpKind::Mul, d); // d*d
  mlir::Value acbd =
      builder.createBinop(loc, ac, cir::BinOpKind::Add, bd); // ac+bd
  mlir::Value ccdd =
      builder.createBinop(loc, cc, cir::BinOpKind::Add, dd); // cc+dd
  mlir::Value resultReal =
      builder.createBinop(loc, acbd, cir::BinOpKind::Div, ccdd);

  mlir::Value bc = builder.createBinop(loc, b, cir::BinOpKind::Mul, c); // b*c
  mlir::Value ad = builder.createBinop(loc, a, cir::BinOpKind::Mul, d); // a*d
  mlir::Value bcad =
      builder.createBinop(loc, bc, cir::BinOpKind::Sub, ad); // bc-ad
  mlir::Value resultImag =
      builder.createBinop(loc, bcad, cir::BinOpKind::Div, ccdd);
  return builder.createComplexCreate(loc, resultReal, resultImag);
}

static mlir::Value
buildRangeReductionComplexDiv(CIRBaseBuilderTy &builder, mlir::Location loc,
                              mlir::Value lhsReal, mlir::Value lhsImag,
                              mlir::Value rhsReal, mlir::Value rhsImag) {
  // Implements Smith's algorithm for complex division.
  // SMITH, R. L. Algorithm 116: Complex division. Commun. ACM 5, 8 (1962).

  // Let:
  //   - lhs := a+bi
  //   - rhs := c+di
  //   - result := lhs / rhs = e+fi
  //
  // The algorithm pseudocode looks like follows:
  //   if fabs(c) >= fabs(d):
  //     r := d / c
  //     tmp := c + r*d
  //     e = (a + b*r) / tmp
  //     f = (b - a*r) / tmp
  //   else:
  //     r := c / d
  //     tmp := d + r*c
  //     e = (a*r + b) / tmp
  //     f = (b*r - a) / tmp

  mlir::Value &a = lhsReal;
  mlir::Value &b = lhsImag;
  mlir::Value &c = rhsReal;
  mlir::Value &d = rhsImag;

  auto trueBranchBuilder = [&](mlir::OpBuilder &, mlir::Location) {
    mlir::Value r = builder.createBinop(loc, d, cir::BinOpKind::Div,
                                        c); // r := d / c
    mlir::Value rd = builder.createBinop(loc, r, cir::BinOpKind::Mul, d); // r*d
    mlir::Value tmp = builder.createBinop(loc, c, cir::BinOpKind::Add,
                                          rd); // tmp := c + r*d

    mlir::Value br = builder.createBinop(loc, b, cir::BinOpKind::Mul, r); // b*r
    mlir::Value abr =
        builder.createBinop(loc, a, cir::BinOpKind::Add, br); // a + b*r
    mlir::Value e = builder.createBinop(loc, abr, cir::BinOpKind::Div, tmp);

    mlir::Value ar = builder.createBinop(loc, a, cir::BinOpKind::Mul, r); // a*r
    mlir::Value bar =
        builder.createBinop(loc, b, cir::BinOpKind::Sub, ar); // b - a*r
    mlir::Value f = builder.createBinop(loc, bar, cir::BinOpKind::Div, tmp);

    mlir::Value result = builder.createComplexCreate(loc, e, f);
    builder.createYield(loc, result);
  };

  auto falseBranchBuilder = [&](mlir::OpBuilder &, mlir::Location) {
    mlir::Value r = builder.createBinop(loc, c, cir::BinOpKind::Div,
                                        d); // r := c / d
    mlir::Value rc = builder.createBinop(loc, r, cir::BinOpKind::Mul, c); // r*c
    mlir::Value tmp = builder.createBinop(loc, d, cir::BinOpKind::Add,
                                          rc); // tmp := d + r*c

    mlir::Value ar = builder.createBinop(loc, a, cir::BinOpKind::Mul, r); // a*r
    mlir::Value arb =
        builder.createBinop(loc, ar, cir::BinOpKind::Add, b); // a*r + b
    mlir::Value e = builder.createBinop(loc, arb, cir::BinOpKind::Div, tmp);

    mlir::Value br = builder.createBinop(loc, b, cir::BinOpKind::Mul, r); // b*r
    mlir::Value bra =
        builder.createBinop(loc, br, cir::BinOpKind::Sub, a); // b*r - a
    mlir::Value f = builder.createBinop(loc, bra, cir::BinOpKind::Div, tmp);

    mlir::Value result = builder.createComplexCreate(loc, e, f);
    builder.createYield(loc, result);
  };

  auto cFabs = cir::FAbsOp::create(builder, loc, c);
  auto dFabs = cir::FAbsOp::create(builder, loc, d);
  cir::CmpOp cmpResult =
      builder.createCompare(loc, cir::CmpOpKind::ge, cFabs, dFabs);
  auto ternary = cir::TernaryOp::create(builder, loc, cmpResult,
                                        trueBranchBuilder, falseBranchBuilder);

  return ternary.getResult();
}

static mlir::Type higherPrecisionElementTypeForComplexArithmetic(
    mlir::MLIRContext &context, clang::ASTContext &cc,
    CIRBaseBuilderTy &builder, mlir::Type elementType) {

  auto getHigherPrecisionFPType = [&context](mlir::Type type) -> mlir::Type {
    if (mlir::isa<cir::FP16Type>(type))
      return cir::SingleType::get(&context);

    if (mlir::isa<cir::SingleType>(type) || mlir::isa<cir::BF16Type>(type))
      return cir::DoubleType::get(&context);

    if (mlir::isa<cir::DoubleType>(type))
      return cir::LongDoubleType::get(&context, type);

    return type;
  };

  auto getFloatTypeSemantics =
      [&cc](mlir::Type type) -> const llvm::fltSemantics & {
    const clang::TargetInfo &info = cc.getTargetInfo();
    if (mlir::isa<cir::FP16Type>(type))
      return info.getHalfFormat();

    if (mlir::isa<cir::BF16Type>(type))
      return info.getBFloat16Format();

    if (mlir::isa<cir::SingleType>(type))
      return info.getFloatFormat();

    if (mlir::isa<cir::DoubleType>(type))
      return info.getDoubleFormat();

    if (mlir::isa<cir::LongDoubleType>(type)) {
      if (cc.getLangOpts().OpenMP && cc.getLangOpts().OpenMPIsTargetDevice)
        llvm_unreachable("NYI Float type semantics with OpenMP");
      return info.getLongDoubleFormat();
    }

    if (mlir::isa<cir::FP128Type>(type)) {
      if (cc.getLangOpts().OpenMP && cc.getLangOpts().OpenMPIsTargetDevice)
        llvm_unreachable("NYI Float type semantics with OpenMP");
      return info.getFloat128Format();
    }

    llvm_unreachable("Unsupported float type semantics");
  };

  const mlir::Type higherElementType = getHigherPrecisionFPType(elementType);
  const llvm::fltSemantics &elementTypeSemantics =
      getFloatTypeSemantics(elementType);
  const llvm::fltSemantics &higherElementTypeSemantics =
      getFloatTypeSemantics(higherElementType);

  // Check that the promoted type can handle the intermediate values without
  // overflowing. This can be interpreted as:
  // (SmallerType.LargestFiniteVal * SmallerType.LargestFiniteVal) * 2 <=
  //      LargerType.LargestFiniteVal.
  // In terms of exponent it gives this formula:
  // (SmallerType.LargestFiniteVal * SmallerType.LargestFiniteVal
  // doubles the exponent of SmallerType.LargestFiniteVal)
  if (llvm::APFloat::semanticsMaxExponent(elementTypeSemantics) * 2 + 1 <=
      llvm::APFloat::semanticsMaxExponent(higherElementTypeSemantics)) {
    return higherElementType;
  }

  // The intermediate values can't be represented in the promoted type
  // without overflowing.
  return {};
}

static mlir::Value
lowerComplexDiv(LoweringPreparePass &pass, CIRBaseBuilderTy &builder,
                mlir::Location loc, cir::ComplexDivOp op, mlir::Value lhsReal,
                mlir::Value lhsImag, mlir::Value rhsReal, mlir::Value rhsImag,
                mlir::MLIRContext &mlirCx, clang::ASTContext &cc) {
  cir::ComplexType complexTy = op.getType();
  if (mlir::isa<cir::FPTypeInterface>(complexTy.getElementType())) {
    cir::ComplexRangeKind range = op.getRange();
    if (range == cir::ComplexRangeKind::Improved)
      return buildRangeReductionComplexDiv(builder, loc, lhsReal, lhsImag,
                                           rhsReal, rhsImag);

    if (range == cir::ComplexRangeKind::Full)
      return buildComplexBinOpLibCall(pass, builder, &getComplexDivLibCallName,
                                      loc, complexTy, lhsReal, lhsImag, rhsReal,
                                      rhsImag);

    if (range == cir::ComplexRangeKind::Promoted) {
      mlir::Type originalElementType = complexTy.getElementType();
      mlir::Type higherPrecisionElementType =
          higherPrecisionElementTypeForComplexArithmetic(mlirCx, cc, builder,
                                                         originalElementType);

      if (!higherPrecisionElementType)
        return buildRangeReductionComplexDiv(builder, loc, lhsReal, lhsImag,
                                             rhsReal, rhsImag);

      cir::CastKind floatingCastKind = cir::CastKind::floating;
      lhsReal = builder.createCast(floatingCastKind, lhsReal,
                                   higherPrecisionElementType);
      lhsImag = builder.createCast(floatingCastKind, lhsImag,
                                   higherPrecisionElementType);
      rhsReal = builder.createCast(floatingCastKind, rhsReal,
                                   higherPrecisionElementType);
      rhsImag = builder.createCast(floatingCastKind, rhsImag,
                                   higherPrecisionElementType);

      mlir::Value algebraicResult = buildAlgebraicComplexDiv(
          builder, loc, lhsReal, lhsImag, rhsReal, rhsImag);

      mlir::Value resultReal = builder.createComplexReal(loc, algebraicResult);
      mlir::Value resultImag = builder.createComplexImag(loc, algebraicResult);

      mlir::Value finalReal =
          builder.createCast(floatingCastKind, resultReal, originalElementType);
      mlir::Value finalImag =
          builder.createCast(floatingCastKind, resultImag, originalElementType);
      return builder.createComplexCreate(loc, finalReal, finalImag);
    }
  }

  return buildAlgebraicComplexDiv(builder, loc, lhsReal, lhsImag, rhsReal,
                                  rhsImag);
}

void LoweringPreparePass::lowerComplexDivOp(cir::ComplexDivOp op) {
  cir::CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op);
  mlir::Location loc = op.getLoc();
  mlir::TypedValue<cir::ComplexType> lhs = op.getLhs();
  mlir::TypedValue<cir::ComplexType> rhs = op.getRhs();
  mlir::Value lhsReal = builder.createComplexReal(loc, lhs);
  mlir::Value lhsImag = builder.createComplexImag(loc, lhs);
  mlir::Value rhsReal = builder.createComplexReal(loc, rhs);
  mlir::Value rhsImag = builder.createComplexImag(loc, rhs);

  mlir::Value loweredResult =
      lowerComplexDiv(*this, builder, loc, op, lhsReal, lhsImag, rhsReal,
                      rhsImag, getContext(), *astCtx);
  op.replaceAllUsesWith(loweredResult);
  op.erase();
}

static llvm::StringRef
getComplexMulLibCallName(llvm::APFloat::Semantics semantics) {
  switch (semantics) {
  case llvm::APFloat::S_IEEEhalf:
    return "__mulhc3";
  case llvm::APFloat::S_IEEEsingle:
    return "__mulsc3";
  case llvm::APFloat::S_IEEEdouble:
    return "__muldc3";
  case llvm::APFloat::S_PPCDoubleDouble:
    return "__multc3";
  case llvm::APFloat::S_x87DoubleExtended:
    return "__mulxc3";
  case llvm::APFloat::S_IEEEquad:
    return "__multc3";
  default:
    llvm_unreachable("unsupported floating point type");
  }
}

static mlir::Value lowerComplexMul(LoweringPreparePass &pass,
                                   CIRBaseBuilderTy &builder,
                                   mlir::Location loc, cir::ComplexMulOp op,
                                   mlir::Value lhsReal, mlir::Value lhsImag,
                                   mlir::Value rhsReal, mlir::Value rhsImag) {
  // (a+bi) * (c+di) = (ac-bd) + (ad+bc)i
  mlir::Value resultRealLhs =
      builder.createBinop(loc, lhsReal, cir::BinOpKind::Mul, rhsReal);
  mlir::Value resultRealRhs =
      builder.createBinop(loc, lhsImag, cir::BinOpKind::Mul, rhsImag);
  mlir::Value resultImagLhs =
      builder.createBinop(loc, lhsReal, cir::BinOpKind::Mul, rhsImag);
  mlir::Value resultImagRhs =
      builder.createBinop(loc, lhsImag, cir::BinOpKind::Mul, rhsReal);
  mlir::Value resultReal = builder.createBinop(
      loc, resultRealLhs, cir::BinOpKind::Sub, resultRealRhs);
  mlir::Value resultImag = builder.createBinop(
      loc, resultImagLhs, cir::BinOpKind::Add, resultImagRhs);
  mlir::Value algebraicResult =
      builder.createComplexCreate(loc, resultReal, resultImag);

  cir::ComplexType complexTy = op.getType();
  cir::ComplexRangeKind rangeKind = op.getRange();
  if (mlir::isa<cir::IntType>(complexTy.getElementType()) ||
      rangeKind == cir::ComplexRangeKind::Basic ||
      rangeKind == cir::ComplexRangeKind::Improved ||
      rangeKind == cir::ComplexRangeKind::Promoted)
    return algebraicResult;

  assert(!cir::MissingFeatures::fastMathFlags());

  // Check whether the real part and the imaginary part of the result are both
  // NaN. If so, emit a library call to compute the multiplication instead.
  // We check a value against NaN by comparing the value against itself.
  mlir::Value resultRealIsNaN = builder.createIsNaN(loc, resultReal);
  mlir::Value resultImagIsNaN = builder.createIsNaN(loc, resultImag);
  mlir::Value resultRealAndImagAreNaN =
      builder.createLogicalAnd(loc, resultRealIsNaN, resultImagIsNaN);

  return cir::TernaryOp::create(
             builder, loc, resultRealAndImagAreNaN,
             [&](mlir::OpBuilder &, mlir::Location) {
               mlir::Value libCallResult = buildComplexBinOpLibCall(
                   pass, builder, &getComplexMulLibCallName, loc, complexTy,
                   lhsReal, lhsImag, rhsReal, rhsImag);
               builder.createYield(loc, libCallResult);
             },
             [&](mlir::OpBuilder &, mlir::Location) {
               builder.createYield(loc, algebraicResult);
             })
      .getResult();
}

void LoweringPreparePass::lowerComplexMulOp(cir::ComplexMulOp op) {
  cir::CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op);
  mlir::Location loc = op.getLoc();
  mlir::TypedValue<cir::ComplexType> lhs = op.getLhs();
  mlir::TypedValue<cir::ComplexType> rhs = op.getRhs();
  mlir::Value lhsReal = builder.createComplexReal(loc, lhs);
  mlir::Value lhsImag = builder.createComplexImag(loc, lhs);
  mlir::Value rhsReal = builder.createComplexReal(loc, rhs);
  mlir::Value rhsImag = builder.createComplexImag(loc, rhs);
  mlir::Value loweredResult = lowerComplexMul(*this, builder, loc, op, lhsReal,
                                              lhsImag, rhsReal, rhsImag);
  op.replaceAllUsesWith(loweredResult);
  op.erase();
}

void LoweringPreparePass::lowerUnaryOp(cir::UnaryOp op) {
  mlir::Type ty = op.getType();
  if (!mlir::isa<cir::ComplexType>(ty))
    return;

  mlir::Location loc = op.getLoc();
  cir::UnaryOpKind opKind = op.getKind();

  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op);

  mlir::Value operand = op.getInput();
  mlir::Value operandReal = builder.createComplexReal(loc, operand);
  mlir::Value operandImag = builder.createComplexImag(loc, operand);

  mlir::Value resultReal;
  mlir::Value resultImag;

  switch (opKind) {
  case cir::UnaryOpKind::Inc:
  case cir::UnaryOpKind::Dec:
    resultReal = builder.createUnaryOp(loc, opKind, operandReal);
    resultImag = operandImag;
    break;

  case cir::UnaryOpKind::Plus:
  case cir::UnaryOpKind::Minus:
    resultReal = builder.createUnaryOp(loc, opKind, operandReal);
    resultImag = builder.createUnaryOp(loc, opKind, operandImag);
    break;

  case cir::UnaryOpKind::Not:
    resultReal = operandReal;
    resultImag =
        builder.createUnaryOp(loc, cir::UnaryOpKind::Minus, operandImag);
    break;
  }

  mlir::Value result = builder.createComplexCreate(loc, resultReal, resultImag);
  op.replaceAllUsesWith(result);
  op.erase();
}

cir::FuncOp LoweringPreparePass::getOrCreateDtorFunc(CIRBaseBuilderTy &builder,
                                                     cir::GlobalOp op,
                                                     mlir::Region &dtorRegion,
                                                     cir::CallOp &dtorCall) {
  mlir::OpBuilder::InsertionGuard guard(builder);
  assert(!cir::MissingFeatures::astVarDeclInterface());
  assert(!cir::MissingFeatures::opGlobalThreadLocal());

  cir::VoidType voidTy = builder.getVoidTy();
  auto voidPtrTy = cir::PointerType::get(voidTy);

  // Look for operations in dtorBlock
  mlir::Block &dtorBlock = dtorRegion.front();

  // The first operation should be a get_global to retrieve the address
  // of the global variable we're destroying.
  auto opIt = dtorBlock.getOperations().begin();
  cir::GetGlobalOp ggop = mlir::cast<cir::GetGlobalOp>(*opIt);

  // The simple case is just a call to a destructor, like this:
  //
  //   %0 = cir.get_global %globalS : !cir.ptr<!rec_S>
  //   cir.call %_ZN1SD1Ev(%0) : (!cir.ptr<!rec_S>) -> ()
  //   (implicit cir.yield)
  //
  // That is, if the second operation is a call that takes the get_global result
  // as its only operand, and the only other operation is a yield, then we can
  // just return the called function.
  if (dtorBlock.getOperations().size() == 3) {
    auto callOp = mlir::dyn_cast<cir::CallOp>(&*(++opIt));
    auto yieldOp = mlir::dyn_cast<cir::YieldOp>(&*(++opIt));
    if (yieldOp && callOp && callOp.getNumOperands() == 1 &&
        callOp.getArgOperand(0) == ggop) {
      dtorCall = callOp;
      return getCalledFunction(callOp);
    }
  }

  // Otherwise, we need to create a helper function to replace the dtor region.
  // This name is kind of arbitrary, but it matches the name that classic
  // codegen uses, based on the expected case that gets us here.
  builder.setInsertionPointAfter(op);
  SmallString<256> fnName("__cxx_global_array_dtor");
  uint32_t cnt = dynamicInitializerNames[fnName]++;
  if (cnt)
    fnName += "." + std::to_string(cnt);

  // Create the helper function.
  auto fnType = cir::FuncType::get({voidPtrTy}, voidTy);
  cir::FuncOp dtorFunc =
      buildRuntimeFunction(builder, fnName, op.getLoc(), fnType,
                           cir::GlobalLinkageKind::InternalLinkage);
  mlir::Block *entryBB = dtorFunc.addEntryBlock();

  // Move everything from the dtor region into the helper function.
  entryBB->getOperations().splice(entryBB->begin(), dtorBlock.getOperations(),
                                  dtorBlock.begin(), dtorBlock.end());

  // Before erasing this, clone it back into the dtor region
  cir::GetGlobalOp dtorGGop =
      mlir::cast<cir::GetGlobalOp>(entryBB->getOperations().front());
  builder.setInsertionPointToStart(&dtorBlock);
  builder.clone(*dtorGGop.getOperation());

  // Replace all uses of the helper function's get_global with the function
  // argument, bitcasting the void pointer to the original type.
  mlir::Value dtorArg = entryBB->getArgument(0);
  builder.setInsertionPointToStart(entryBB);
  auto castedArg =
      cir::CastOp::create(builder, dtorGGop.getLoc(), dtorGGop.getType(),
                          cir::CastKind::bitcast, dtorArg);
  dtorGGop.replaceAllUsesWith(castedArg.getResult());
  dtorGGop.erase();

  // Replace the yield in the final block with a return
  mlir::Block &finalBlock = dtorFunc.getBody().back();
  auto yieldOp = cast<cir::YieldOp>(finalBlock.getTerminator());
  builder.setInsertionPoint(yieldOp);
  cir::ReturnOp::create(builder, yieldOp->getLoc());
  yieldOp->erase();

  // Create a call to the helper function, passing the original get_global op
  // as the argument.
  cir::GetGlobalOp origGGop =
      mlir::cast<cir::GetGlobalOp>(dtorBlock.getOperations().front());
  builder.setInsertionPointAfter(origGGop);
  mlir::Value ggopResult = origGGop.getResult();
  dtorCall = builder.createCallOp(op.getLoc(), dtorFunc, ggopResult);

  // Add a yield after the call.
  auto finalYield = cir::YieldOp::create(builder, op.getLoc());

  // Erase everything after the yield.
  dtorBlock.getOperations().erase(std::next(mlir::Block::iterator(finalYield)),
                                  dtorBlock.end());
  dtorRegion.getBlocks().erase(std::next(dtorRegion.begin()), dtorRegion.end());

  return dtorFunc;
}

cir::FuncOp
LoweringPreparePass::buildCXXGlobalVarDeclInitFunc(cir::GlobalOp op) {
  // TODO(cir): Store this in the GlobalOp.
  // This should come from the MangleContext, but for now I'm hardcoding it.
  SmallString<256> fnName("__cxx_global_var_init");
  // Get a unique name
  uint32_t cnt = dynamicInitializerNames[fnName]++;
  if (cnt)
    fnName += "." + std::to_string(cnt);

  // Create a variable initialization function.
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op);
  cir::VoidType voidTy = builder.getVoidTy();
  auto fnType = cir::FuncType::get({}, voidTy);
  FuncOp f = buildRuntimeFunction(builder, fnName, op.getLoc(), fnType,
                                  cir::GlobalLinkageKind::InternalLinkage);

  // Move over the initialzation code of the ctor region.
  mlir::Block *entryBB = f.addEntryBlock();
  if (!op.getCtorRegion().empty()) {
    mlir::Block &block = op.getCtorRegion().front();
    entryBB->getOperations().splice(entryBB->begin(), block.getOperations(),
                                    block.begin(), std::prev(block.end()));
  }

  // Register the destructor call with __cxa_atexit
  mlir::Region &dtorRegion = op.getDtorRegion();
  if (!dtorRegion.empty()) {
    assert(!cir::MissingFeatures::astVarDeclInterface());
    assert(!cir::MissingFeatures::opGlobalThreadLocal());

    // Create a variable that binds the atexit to this shared object.
    builder.setInsertionPointToStart(&mlirModule.getBodyRegion().front());
    cir::GlobalOp handle = buildRuntimeVariable(
        builder, "__dso_handle", op.getLoc(), builder.getI8Type(),
        cir::GlobalLinkageKind::ExternalLinkage, cir::VisibilityKind::Hidden);

    // If this is a simple call to a destructor, get the called function.
    // Otherwise, create a helper function for the entire dtor region,
    // replacing the current dtor region body with a call to the helper
    // function.
    cir::CallOp dtorCall;
    cir::FuncOp dtorFunc =
        getOrCreateDtorFunc(builder, op, dtorRegion, dtorCall);

    // Create a runtime helper function:
    //    extern "C" int __cxa_atexit(void (*f)(void *), void *p, void *d);
    auto voidPtrTy = cir::PointerType::get(voidTy);
    auto voidFnTy = cir::FuncType::get({voidPtrTy}, voidTy);
    auto voidFnPtrTy = cir::PointerType::get(voidFnTy);
    auto handlePtrTy = cir::PointerType::get(handle.getSymType());
    auto fnAtExitType =
        cir::FuncType::get({voidFnPtrTy, voidPtrTy, handlePtrTy}, voidTy);
    const char *nameAtExit = "__cxa_atexit";
    cir::FuncOp fnAtExit =
        buildRuntimeFunction(builder, nameAtExit, op.getLoc(), fnAtExitType);

    // Replace the dtor (or helper) call with a call to
    //   __cxa_atexit(&dtor, &var, &__dso_handle)
    builder.setInsertionPointAfter(dtorCall);
    mlir::Value args[3];
    auto dtorPtrTy = cir::PointerType::get(dtorFunc.getFunctionType());
    // dtorPtrTy
    args[0] = cir::GetGlobalOp::create(builder, dtorCall.getLoc(), dtorPtrTy,
                                       dtorFunc.getSymName());
    args[0] = cir::CastOp::create(builder, dtorCall.getLoc(), voidFnPtrTy,
                                  cir::CastKind::bitcast, args[0]);
    args[1] =
        cir::CastOp::create(builder, dtorCall.getLoc(), voidPtrTy,
                            cir::CastKind::bitcast, dtorCall.getArgOperand(0));
    args[2] = cir::GetGlobalOp::create(builder, handle.getLoc(), handlePtrTy,
                                       handle.getSymName());
    builder.createCallOp(dtorCall.getLoc(), fnAtExit, args);
    dtorCall->erase();
    mlir::Block &dtorBlock = dtorRegion.front();
    entryBB->getOperations().splice(entryBB->end(), dtorBlock.getOperations(),
                                    dtorBlock.begin(),
                                    std::prev(dtorBlock.end()));
  }

  // Replace cir.yield with cir.return
  builder.setInsertionPointToEnd(entryBB);
  mlir::Operation *yieldOp = nullptr;
  if (!op.getCtorRegion().empty()) {
    mlir::Block &block = op.getCtorRegion().front();
    yieldOp = &block.getOperations().back();
  } else {
    assert(!dtorRegion.empty());
    mlir::Block &block = dtorRegion.front();
    yieldOp = &block.getOperations().back();
  }

  assert(isa<cir::YieldOp>(*yieldOp));
  cir::ReturnOp::create(builder, yieldOp->getLoc());
  return f;
}

void LoweringPreparePass::lowerGlobalOp(GlobalOp op) {
  mlir::Region &ctorRegion = op.getCtorRegion();
  mlir::Region &dtorRegion = op.getDtorRegion();

  if (!ctorRegion.empty() || !dtorRegion.empty()) {
    // Build a variable initialization function and move the initialzation code
    // in the ctor region over.
    cir::FuncOp f = buildCXXGlobalVarDeclInitFunc(op);

    // Clear the ctor and dtor region
    ctorRegion.getBlocks().clear();
    dtorRegion.getBlocks().clear();

    // If the variable has init_priority, set it on the init function.
    if (auto initPriority = op.getInitPriority())
      f.setGlobalCtorPriority(*initPriority);

    dynamicInitializers.push_back(f);
  }

  if (auto annotations = op.getAnnotationsAttr())
    addGlobalAnnotations(op, annotations);
}

template <typename AttributeTy>
static llvm::SmallVector<mlir::Attribute>
prepareCtorDtorAttrList(mlir::MLIRContext *context,
                        llvm::ArrayRef<std::pair<std::string, uint32_t>> list) {
  llvm::SmallVector<mlir::Attribute> attrs;
  for (const auto &[name, priority] : list)
    attrs.push_back(AttributeTy::get(context, name, priority));
  return attrs;
}

void LoweringPreparePass::buildGlobalCtorDtorList() {
  if (!globalCtorList.empty()) {
    llvm::SmallVector<mlir::Attribute> globalCtors =
        prepareCtorDtorAttrList<cir::GlobalCtorAttr>(&getContext(),
                                                     globalCtorList);

    mlirModule->setAttr(cir::CIRDialect::getGlobalCtorsAttrName(),
                        mlir::ArrayAttr::get(&getContext(), globalCtors));
  }

  if (!globalDtorList.empty()) {
    llvm::SmallVector<mlir::Attribute> globalDtors =
        prepareCtorDtorAttrList<cir::GlobalDtorAttr>(&getContext(),
                                                     globalDtorList);
    mlirModule->setAttr(cir::CIRDialect::getGlobalDtorsAttrName(),
                        mlir::ArrayAttr::get(&getContext(), globalDtors));
  }
}

void LoweringPreparePass::addGlobalAnnotations(mlir::Operation *op,
                                               mlir::ArrayAttr annotations) {
  auto globalValue = cast<mlir::SymbolOpInterface>(op);
  mlir::StringAttr globalValueName = globalValue.getNameAttr();
  for (auto &annot : annotations) {
    llvm::SmallVector<mlir::Attribute, 2> entryArray = {globalValueName, annot};
    globalAnnotations.push_back(
        mlir::ArrayAttr::get(mlirModule.getContext(), entryArray));
  }
}

void LoweringPreparePass::buildGlobalAnnotationValues() {
  if (globalAnnotations.empty())
    return;
  mlir::ArrayAttr annotationValueArray =
      mlir::ArrayAttr::get(mlirModule.getContext(), globalAnnotations);
  mlirModule->setAttr(
      cir::CIRDialect::getGlobalAnnotationsAttrName(),
      cir::GlobalAnnotationValuesAttr::get(annotationValueArray));
}

void LoweringPreparePass::buildCXXGlobalInitFunc() {
  if (dynamicInitializers.empty())
    return;

  // Separate initializers with custom priority from those without.
  // Initializers with init_priority are registered directly in the global
  // ctor list rather than being aggregated into the module init function.
  llvm::SmallVector<cir::FuncOp> defaultPriorityInitializers;
  for (cir::FuncOp &f : dynamicInitializers) {
    if (auto priority = f.getGlobalCtorPriority()) {
      globalCtorList.emplace_back(f.getSymName().str(), *priority);
    } else {
      defaultPriorityInitializers.push_back(f);
    }
  }

  // If there are no default priority initializers, we're done.
  if (defaultPriorityInitializers.empty())
    return;

  SmallString<256> fnName;
  // Include the filename in the symbol name. Including "sub_" matches gcc
  // and makes sure these symbols appear lexicographically behind the symbols
  // with priority (TBD).  Module implementation units behave the same
  // way as a non-modular TU with imports.
  // TODO: check CXX20ModuleInits
  if (astCtx->getCurrentNamedModule() &&
      !astCtx->getCurrentNamedModule()->isModuleImplementation()) {
    llvm::raw_svector_ostream out(fnName);
    std::unique_ptr<clang::MangleContext> mangleCtx(
        astCtx->createMangleContext());
    cast<clang::ItaniumMangleContext>(*mangleCtx)
        .mangleModuleInitializer(astCtx->getCurrentNamedModule(), out);
  } else {
    fnName += "_GLOBAL__sub_I_";
    fnName += getTransformedFileName(mlirModule);
  }

  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointToEnd(&mlirModule.getBodyRegion().back());
  auto fnType = cir::FuncType::get({}, builder.getVoidTy());
  cir::FuncOp f =
      buildRuntimeFunction(builder, fnName, mlirModule.getLoc(), fnType,
                           cir::GlobalLinkageKind::ExternalLinkage);
  builder.setInsertionPointToStart(f.addEntryBlock());
  for (cir::FuncOp &init : defaultPriorityInitializers)
    builder.createCallOp(init.getLoc(), init, {});
  // Add the global init function (not the individual ctor functions) to the
  // global ctor list.
  globalCtorList.emplace_back(fnName,
                              cir::GlobalCtorAttr::getDefaultPriority());

  cir::ReturnOp::create(builder, f.getLoc());
}

void LoweringPreparePass::lowerDynamicCastOp(DynamicCastOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op);

  assert(astCtx && "AST context is not available during lowering prepare");
  auto loweredValue = cxxABI->lowerDynamicCast(builder, *astCtx, op);

  op.replaceAllUsesWith(loweredValue);
  op.erase();
}

void LoweringPreparePass::lowerVAArgOp(VAArgOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPoint(op);

  auto res = cxxABI->lowerVAArg(builder, op, *datalayout);
  if (res) {
    op.replaceAllUsesWith(res);
    op.erase();
  }
}

static void lowerArrayDtorCtorIntoLoop(cir::CIRBaseBuilderTy &builder,
                                       clang::ASTContext *astCtx,
                                       mlir::Operation *op, mlir::Type eltTy,
                                       mlir::Value arrayAddr, uint64_t arrayLen,
                                       bool isCtor) {
  // Generate loop to call into ctor/dtor for every element.
  mlir::Location loc = op->getLoc();

  // TODO: instead of getting the size from the AST context, create alias for
  // PtrDiffTy and unify with CIRGen stuff.
  const unsigned sizeTypeSize =
      astCtx->getTypeSize(astCtx->getSignedSizeType());
  uint64_t endOffset = isCtor ? arrayLen : arrayLen - 1;
  mlir::Value endOffsetVal =
      builder.getUnsignedInt(loc, endOffset, sizeTypeSize);

  auto begin = cir::CastOp::create(builder, loc, eltTy,
                                   cir::CastKind::array_to_ptrdecay, arrayAddr);
  mlir::Value end =
      cir::PtrStrideOp::create(builder, loc, eltTy, begin, endOffsetVal);
  mlir::Value start = isCtor ? begin : end;
  mlir::Value stop = isCtor ? end : begin;

  mlir::Value tmpAddr = builder.createAlloca(
      loc, /*addr type*/ builder.getPointerTo(eltTy),
      /*var type*/ eltTy, "__array_idx", builder.getAlignmentAttr(1));
  builder.createStore(loc, start, tmpAddr);

  cir::DoWhileOp loop = builder.createDoWhile(
      loc,
      /*condBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location loc) {
        auto currentElement = cir::LoadOp::create(b, loc, eltTy, tmpAddr);
        auto cmp = cir::CmpOp::create(builder, loc, cir::CmpOpKind::ne,
                                      currentElement, stop);
        builder.createCondition(cmp);
      },
      /*bodyBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location loc) {
        auto currentElement = cir::LoadOp::create(b, loc, eltTy, tmpAddr);

        cir::CallOp ctorCall;
        op->walk([&](cir::CallOp c) { ctorCall = c; });
        assert(ctorCall && "expected ctor call");

        // Array elements get constructed in order but destructed in reverse.
        mlir::Value stride;
        if (isCtor)
          stride = builder.getUnsignedInt(loc, 1, sizeTypeSize);
        else
          stride = builder.getSignedInt(loc, -1, sizeTypeSize);

        ctorCall->moveBefore(stride.getDefiningOp());
        ctorCall->setOperand(0, currentElement);
        auto nextElement = cir::PtrStrideOp::create(builder, loc, eltTy,
                                                    currentElement, stride);

        // Store the element pointer to the temporary variable
        builder.createStore(loc, nextElement, tmpAddr);
        builder.createYield(loc);
      });

  op->replaceAllUsesWith(loop);
  op->erase();
}

void LoweringPreparePass::lowerArrayDtor(cir::ArrayDtor op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op.getOperation());

  mlir::Type eltTy = op->getRegion(0).getArgument(0).getType();
  assert(!cir::MissingFeatures::vlas());
  auto arrayLen =
      mlir::cast<cir::ArrayType>(op.getAddr().getType().getPointee()).getSize();
  lowerArrayDtorCtorIntoLoop(builder, astCtx, op, eltTy, op.getAddr(), arrayLen,
                             false);
}

void LoweringPreparePass::lowerArrayCtor(cir::ArrayCtor op) {
  cir::CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op.getOperation());

  mlir::Type eltTy = op->getRegion(0).getArgument(0).getType();
  assert(!cir::MissingFeatures::vlas());
  auto arrayLen =
      mlir::cast<cir::ArrayType>(op.getAddr().getType().getPointee()).getSize();
  lowerArrayDtorCtorIntoLoop(builder, astCtx, op, eltTy, op.getAddr(), arrayLen,
                             true);
}

void LoweringPreparePass::lowerTrivialCopyCall(cir::CallOp op) {
  cir::FuncOp funcOp = getCalledFunction(op);
  if (!funcOp)
    return;

  std::optional<cir::CtorKind> ctorKind = funcOp.getCxxConstructorKind();
  if (ctorKind && *ctorKind == cir::CtorKind::Copy) {
    // Safety checks: constructor calls should have no return value and exactly
    // two operands (dest and src).
    if (op.getNumResults() > 0)
      return;
    mlir::ValueRange operands = op.getOperands();
    if (operands.size() != 2)
      return;
    // Replace the copy constructor call with a `CopyOp`
    CIRBaseBuilderTy builder(getContext());
    mlir::Value dest = operands[0];
    mlir::Value src = operands[1];
    builder.setInsertionPoint(op);
    builder.createCopy(dest, src);
    op.erase();
  }
}

void LoweringPreparePass::lowerStdFindOp(StdFindOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op.getOperation());
  auto call = builder.createCallOp(
      op.getLoc(), op.getOriginalFnAttr(), op.getType(),
      mlir::ValueRange{op.getOperand(0), op.getOperand(1), op.getOperand(2)});

  op.replaceAllUsesWith(call);
  op.erase();
}

void LoweringPreparePass::lowerStrLenOp(StrLenOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op.getOperation());
  auto call = builder.createCallOp(op.getLoc(), op.getOriginalFnAttr(),
                                   op.getType(), op.getOperand());

  op.replaceAllUsesWith(call);
  op.erase();
}

void LoweringPreparePass::lowerIterBeginOp(IterBeginOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op.getOperation());
  auto call = builder.createCallOp(op.getLoc(), op.getOriginalFnAttr(),
                                   op.getType(), op.getOperand());

  op.replaceAllUsesWith(call);
  op.erase();
}

void LoweringPreparePass::lowerIterEndOp(IterEndOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op.getOperation());
  auto call = builder.createCallOp(op.getLoc(), op.getOriginalFnAttr(),
                                   op.getType(), op.getOperand());

  op.replaceAllUsesWith(call);
  op.erase();
}

static void canonicalizeIntrinsicThreeWayCmp(CIRBaseBuilderTy &builder,
                                             cir::CmpThreeWayOp op) {
  auto loc = op->getLoc();
  auto cmpInfo = op.getInfo();

  if (cmpInfo.getLt() == -1 && cmpInfo.getEq() == 0 && cmpInfo.getGt() == 1) {
    // The comparison is already in canonicalized form.
    return;
  }

  auto canonicalizedCmpInfo =
      cir::CmpThreeWayInfoAttr::get(builder.getContext(), -1, 0, 1);
  mlir::Value result =
      cir::CmpThreeWayOp::create(builder, loc, op.getType(), op.getLhs(),
                                 op.getRhs(), canonicalizedCmpInfo)
          .getResult();

  auto compareAndYield = [&](mlir::Value input, int64_t test,
                             int64_t yield) -> mlir::Value {
    // Create a conditional branch that tests whether `input` is equal to
    // `test`. If `input` is equal to `test`, yield `yield`. Otherwise, yield
    // `input` as is.
    auto testValue =
        builder.getConstant(loc, cir::IntAttr::get(input.getType(), test));
    auto yieldValue =
        builder.getConstant(loc, cir::IntAttr::get(input.getType(), yield));
    auto eqToTest =
        builder.createCompare(loc, cir::CmpOpKind::eq, input, testValue);
    return builder.createSelect(loc, eqToTest, yieldValue, input);
  };

  if (cmpInfo.getLt() != -1)
    result = compareAndYield(result, -1, cmpInfo.getLt());

  if (cmpInfo.getEq() != 0)
    result = compareAndYield(result, 0, cmpInfo.getEq());

  if (cmpInfo.getGt() != 1)
    result = compareAndYield(result, 1, cmpInfo.getGt());

  op.replaceAllUsesWith(result);
  op.erase();
}

void LoweringPreparePass::lowerThreeWayCmpOp(cir::CmpThreeWayOp op) {
  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointAfter(op);

  if (op.isIntegralComparison() && op.isStrongOrdering()) {
    // For three-way comparisons on integral operands that produce strong
    // ordering, we can generate potentially better code with the `llvm.scmp.*`
    // and `llvm.ucmp.*` intrinsics. Thus we don't replace these comparisons
    // here. They will be lowered directly to LLVMIR during the LLVM lowering
    // pass.
    //
    // But we still need to take a step here. `llvm.scmp.*` and `llvm.ucmp.*`
    // returns -1, 0, or 1 to represent lt, eq, and gt, which are the
    // "canonicalized" result values of three-way comparisons. However,
    // `cir.cmp3way` may not produce canonicalized result. We need to
    // canonicalize the comparison if necessary. This is what we're doing in
    // this special branch.
    canonicalizeIntrinsicThreeWayCmp(builder, op);
    return;
  }

  auto loc = op->getLoc();
  auto cmpInfo = op.getInfo();

  auto buildCmpRes = [&](int64_t value) -> mlir::Value {
    return cir::ConstantOp::create(builder, loc,
                                   cir::IntAttr::get(op.getType(), value));
  };
  auto ltRes = buildCmpRes(cmpInfo.getLt());
  auto eqRes = buildCmpRes(cmpInfo.getEq());
  auto gtRes = buildCmpRes(cmpInfo.getGt());

  auto buildCmp = [&](CmpOpKind kind) -> mlir::Value {
    auto ty = BoolType::get(&getContext());
    return cir::CmpOp::create(builder, loc, ty, kind, op.getLhs(), op.getRhs());
  };
  auto buildSelect = [&](mlir::Value condition, mlir::Value trueResult,
                         mlir::Value falseResult) -> mlir::Value {
    return builder.createSelect(loc, condition, trueResult, falseResult);
  };

  mlir::Value transformedResult;
  if (cmpInfo.getOrdering() == CmpOrdering::Strong) {
    // Strong ordering.
    auto lt = buildCmp(CmpOpKind::lt);
    auto eq = buildCmp(CmpOpKind::eq);
    auto selectOnEq = buildSelect(eq, eqRes, gtRes);
    transformedResult = buildSelect(lt, ltRes, selectOnEq);
  } else {
    // Partial ordering.
    auto unorderedRes = buildCmpRes(*cmpInfo.getUnordered());
    auto lt = buildCmp(CmpOpKind::lt);
    auto eq = buildCmp(CmpOpKind::eq);
    auto gt = buildCmp(CmpOpKind::gt);
    auto selectOnEq = buildSelect(eq, eqRes, unorderedRes);
    auto selectOnGt = buildSelect(gt, gtRes, selectOnEq);
    transformedResult = buildSelect(lt, ltRes, selectOnGt);
  }

  op.replaceAllUsesWith(transformedResult);
  op.erase();
}

void LoweringPreparePass::lowerThrowOp(ThrowOp op) {
  CIRBaseBuilderTy builder(getContext());

  if (op.rethrows()) {
    auto voidTy = cir::VoidType::get(builder.getContext());
    auto fnType = cir::FuncType::get({}, voidTy);
    auto fnName = "__cxa_rethrow";

    builder.setInsertionPointToStart(&mlirModule.getBodyRegion().front());
    FuncOp f = buildRuntimeFunction(builder, fnName, op.getLoc(), fnType);

    builder.setInsertionPointAfter(op.getOperation());
    auto call = builder.createTryCallOp(op.getLoc(), f, {});

    op->replaceAllUsesWith(call);
    op->erase();
  }
}

//===----------------------------------------------------------------------===//
// CUDA/HIP Module Registration Infrastructure
//===----------------------------------------------------------------------===//

void LoweringPreparePass::buildCUDAModuleCtor() {
  if (astCtx->getLangOpts().HIP)
    assert(!cir::MissingFeatures::hipModuleCtor());
  if (astCtx->getLangOpts().GPURelocatableDeviceCode)
    llvm_unreachable("NYI");

  // For CUDA without -fgpu-rdc, it's safe to stop generating ctor
  // if there's nothing to register.
  if (cudaKernelMap.empty() && cudaVarMap.empty())
    return;

  // There's no device-side binary, so no need to proceed for CUDA.
  auto cudaBinaryHandleAttr =
      mlirModule->getAttr(cir::CIRDialect::getCUDABinaryHandleAttrName());
  if (!cudaBinaryHandleAttr) {
    if (astCtx->getLangOpts().HIP)
      assert(!cir::MissingFeatures::hipModuleCtor());
    return;
  }
  std::string cudaGPUBinaryName =
      cast<cir::CUDABinaryHandleAttr>(cudaBinaryHandleAttr).getName();

  constexpr unsigned cudaFatMagic = 0x466243b1;
  constexpr unsigned hipFatMagic = 0x48495046; // "HIPF"

  auto cudaPrefix = getCUDAPrefix(astCtx);

  const unsigned fatMagic =
      astCtx->getLangOpts().HIP ? hipFatMagic : cudaFatMagic;

  // MAC OS X needs special care, but we haven't supported that in CIR yet.
  assert(!cir::MissingFeatures::checkMacOSXTriple());

  CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointToStart(mlirModule.getBody());

  mlir::Location loc = mlirModule.getLoc();

  auto voidTy = VoidType::get(&getContext());
  auto voidPtrTy = PointerType::get(voidTy);
  auto voidPtrPtrTy = PointerType::get(voidPtrTy);
  auto intTy = IntType::get(&getContext(), astCtx->getTypeSize(astCtx->IntTy),
                            astCtx->IntTy->isSignedIntegerType());
  auto charTy = IntType::get(&getContext(), astCtx->getCharWidth(), false);

  // Read the GPU binary and create a constant array for it.
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> cudaGPUBinaryOrErr =
      llvm::MemoryBuffer::getFile(cudaGPUBinaryName);
  if (std::error_code ec = cudaGPUBinaryOrErr.getError()) {
    mlirModule->emitError("cannot open file: " + cudaGPUBinaryName +
                          ec.message());
    return;
  }
  std::unique_ptr<llvm::MemoryBuffer> cudaGPUBinary =
      std::move(cudaGPUBinaryOrErr.get());

  // The section names are different for MAC OS X.
  llvm::StringRef fatbinConstName =
      astCtx->getLangOpts().HIP ? ".hip_fatbin" : ".nv_fatbin";

  llvm::StringRef fatbinSectionName =
      astCtx->getLangOpts().HIP ? ".hipFatBinSegment" : ".nvFatBinSegment";

  // Create a global variable with the contents of GPU binary.
  auto fatbinType =
      ArrayType::get(&getContext(), charTy, cudaGPUBinary->getBuffer().size());

  std::string fatbinStrName = addUnderscoredPrefix(cudaPrefix, "_fatbin_str");
  GlobalOp fatbinStr = GlobalOp::create(
      builder, loc, fatbinStrName, fatbinType, /*isConstant=*/true,
      /*linkage=*/cir::GlobalLinkageKind::PrivateLinkage);
  fatbinStr.setAlignment(8);
  fatbinStr.setInitialValueAttr(cir::ConstArrayAttr::get(
      fatbinType, builder.getStringAttr(cudaGPUBinary->getBuffer())));
  fatbinStr.setSection(fatbinConstName);
  fatbinStr.setPrivate();

  // Create a record FatbinWrapper, pointing to the GPU binary.
  // Record layout:
  //    struct { int magicNum; int version; void *fatbin; void *unused; };
  auto fatbinWrapperType = RecordType::get(
      &getContext(), {intTy, intTy, voidPtrTy, voidPtrTy}, /*packed=*/false,
      /*padded=*/false, RecordType::RecordKind::Struct);

  std::string fatbinWrapperName =
      addUnderscoredPrefix(cudaPrefix, "_fatbin_wrapper");
  GlobalOp fatbinWrapper = GlobalOp::create(
      builder, loc, fatbinWrapperName, fatbinWrapperType, /*isConstant=*/true,
      /*linkage=*/cir::GlobalLinkageKind::InternalLinkage);
  fatbinWrapper.setPrivate();
  fatbinWrapper.setSection(fatbinSectionName);

  auto magicInit = IntAttr::get(intTy, fatMagic);
  auto versionInit = IntAttr::get(intTy, 1);
  auto fatbinStrSymbol =
      mlir::FlatSymbolRefAttr::get(fatbinStr.getSymNameAttr());
  auto fatbinInit = GlobalViewAttr::get(voidPtrTy, fatbinStrSymbol);
  auto unusedInit = builder.getConstNullPtrAttr(voidPtrTy);
  fatbinWrapper.setInitialValueAttr(cir::ConstRecordAttr::get(
      fatbinWrapperType,
      ArrayAttr::get(&getContext(),
                     {magicInit, versionInit, fatbinInit, unusedInit})));

  // GPU fat binary handle is also a global variable in OG.
  std::string gpubinHandleName =
      addUnderscoredPrefix(cudaPrefix, "_gpubin_handle");
  auto gpubinHandle = GlobalOp::create(
      builder, loc, gpubinHandleName, voidPtrPtrTy,
      /*isConstant=*/false, /*linkage=*/GlobalLinkageKind::InternalLinkage);
  gpubinHandle.setInitialValueAttr(builder.getConstNullPtrAttr(voidPtrPtrTy));
  gpubinHandle.setPrivate();

  // Declare this function:
  //    void **__{cuda|hip}RegisterFatBinary(void *);
  std::string regFuncName =
      addUnderscoredPrefix(cudaPrefix, "RegisterFatBinary");
  auto regFuncType = FuncType::get({voidPtrTy}, voidPtrPtrTy);
  auto regFunc = buildRuntimeFunction(builder, regFuncName, loc, regFuncType);

  // Create the module constructor.
  std::string moduleCtorName = addUnderscoredPrefix(cudaPrefix, "_module_ctor");
  auto moduleCtor = buildRuntimeFunction(builder, moduleCtorName, loc,
                                         FuncType::get({}, voidTy),
                                         GlobalLinkageKind::InternalLinkage);
  globalCtorList.emplace_back(moduleCtorName, /*priority=*/65535);
  builder.setInsertionPointToStart(moduleCtor.addEntryBlock());
  if (astCtx->getLangOpts().HIP) {
    auto *entryBlock = builder.getInsertionBlock();
    auto *parent = builder.getInsertionBlock()->getParent();
    auto *ifBlock = builder.createBlock(parent);
    auto *exitBlock = builder.createBlock(parent);
    {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToEnd(entryBlock);
      mlir::Value handle =
          builder.createLoad(loc, builder.createGetGlobal(gpubinHandle));
      auto handlePtrTy = llvm::cast<cir::PointerType>(handle.getType());
      mlir::Value nullPtr = builder.getNullPtr(handlePtrTy, loc);
      auto isNull =
          builder.createCompare(loc, cir::CmpOpKind::eq, handle, nullPtr);
      cir::BrCondOp::create(builder, loc, isNull, ifBlock, exitBlock);
    }
    {
      // When handle is null we need to load the fatbin and register it
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToStart(ifBlock);
      auto wrapper = builder.createGetGlobal(fatbinWrapper);
      auto fatbinVoidPtr = builder.createBitcast(wrapper, voidPtrTy);
      auto gpuBinaryHandleCall =
          builder.createCallOp(loc, regFunc, fatbinVoidPtr);
      auto gpuBinaryHandle = gpuBinaryHandleCall.getResult();
      auto gpuBinaryHandleGlobal = builder.createGetGlobal(gpubinHandle);
      builder.createStore(loc, gpuBinaryHandle, gpuBinaryHandleGlobal);
      cir::BrOp::create(builder, loc, exitBlock);
    }
    {
      // Exit block
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToStart(exitBlock);
      mlir::Value gHandle =
          builder.createLoad(loc, builder.createGetGlobal(gpubinHandle));

      std::optional<FuncOp> regGlobal = buildCUDARegisterGlobals();
      if (regGlobal) {
        builder.createCallOp(loc, *regGlobal, gHandle);
      }

      if (auto dtor = buildHIPModuleDtor()) {
        cir::CIRBaseBuilderTy globalBuilder(getContext());
        globalBuilder.setInsertionPointToStart(mlirModule.getBody());
        FuncOp atexit = buildRuntimeFunction(
            globalBuilder, "atexit", loc,
            FuncType::get(PointerType::get(dtor->getFunctionType()), intTy));

        mlir::Value dtorFunc = GetGlobalOp::create(
            builder, loc, PointerType::get(dtor->getFunctionType()),
            mlir::FlatSymbolRefAttr::get(dtor->getSymNameAttr()));
        builder.createCallOp(loc, atexit, dtorFunc);
      }
      cir::ReturnOp::create(builder, loc);
    }
    return;
  }
  // CUDA CTOR-DTOR generations
  auto wrapper = builder.createGetGlobal(fatbinWrapper);
  auto fatbinVoidPtr = builder.createBitcast(wrapper, voidPtrTy);
  auto gpuBinaryHandleCall = builder.createCallOp(loc, regFunc, fatbinVoidPtr);
  auto gpuBinaryHandle = gpuBinaryHandleCall.getResult();
  auto gpuBinaryHandleGlobal = builder.createGetGlobal(gpubinHandle);
  builder.createStore(loc, gpuBinaryHandle, gpuBinaryHandleGlobal);

  // Generate __cuda_register_globals and call it.
  std::optional<FuncOp> regGlobal = buildCUDARegisterGlobals();
  if (regGlobal) {
    builder.createCallOp(loc, *regGlobal, gpuBinaryHandle);
  }

  // From CUDA 10.1 onwards, we must call this function to end registration:
  //      void __cudaRegisterFatBinaryEnd(void **fatbinHandle);
  if (clang::CudaFeatureEnabled(
          astCtx->getTargetInfo().getSDKVersion(),
          clang::CudaFeature::CUDA_USES_FATBIN_REGISTER_END)) {
    cir::CIRBaseBuilderTy globalBuilder(getContext());
    globalBuilder.setInsertionPointToStart(mlirModule.getBody());
    FuncOp endFunc =
        buildRuntimeFunction(globalBuilder, "__cudaRegisterFatBinaryEnd", loc,
                             FuncType::get({voidPtrPtrTy}, voidTy));
    builder.createCallOp(loc, endFunc, gpuBinaryHandle);
  }

  // Create destructor and register it with atexit().
  if (auto dtor = buildCUDAModuleDtor()) {
    cir::CIRBaseBuilderTy globalBuilder(getContext());
    globalBuilder.setInsertionPointToStart(mlirModule.getBody());
    FuncOp atexit = buildRuntimeFunction(
        globalBuilder, "atexit", loc,
        FuncType::get(PointerType::get(dtor->getFunctionType()), intTy));

    mlir::Value dtorFunc = GetGlobalOp::create(
        builder, loc, PointerType::get(dtor->getFunctionType()),
        mlir::FlatSymbolRefAttr::get(dtor->getSymNameAttr()));
    builder.createCallOp(loc, atexit, dtorFunc);
  }

  cir::ReturnOp::create(builder, loc);
}

std::optional<FuncOp> LoweringPreparePass::buildCUDARegisterGlobals() {
  // There is nothing to register.
  if (cudaKernelMap.empty() && cudaVarMap.empty())
    return {};

  cir::CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointToStart(mlirModule.getBody());

  auto loc = mlirModule.getLoc();
  auto cudaPrefix = getCUDAPrefix(astCtx);

  auto voidTy = VoidType::get(&getContext());
  auto voidPtrTy = PointerType::get(voidTy);
  auto voidPtrPtrTy = PointerType::get(voidPtrTy);

  // Create the function:
  //      void __cuda_register_globals(void **fatbinHandle)
  std::string regGlobalFuncName =
      addUnderscoredPrefix(cudaPrefix, "_register_globals");
  auto regGlobalFuncTy = FuncType::get({voidPtrPtrTy}, voidTy);
  FuncOp regGlobalFunc =
      buildRuntimeFunction(builder, regGlobalFuncName, loc, regGlobalFuncTy,
                           /*linkage=*/GlobalLinkageKind::InternalLinkage);
  builder.setInsertionPointToStart(regGlobalFunc.addEntryBlock());

  buildCUDARegisterGlobalFunctions(builder, regGlobalFunc);
  buildCUDARegisterVars(builder, regGlobalFunc);

  ReturnOp::create(builder, loc);
  return regGlobalFunc;
}

void LoweringPreparePass::buildCUDARegisterGlobalFunctions(
    cir::CIRBaseBuilderTy &builder, FuncOp regGlobalFunc) {
  if (cudaKernelMap.empty())
    return;

  auto loc = mlirModule.getLoc();
  auto cudaPrefix = getCUDAPrefix(astCtx);

  auto voidTy = VoidType::get(&getContext());
  auto voidPtrTy = PointerType::get(voidTy);
  auto voidPtrPtrTy = PointerType::get(voidPtrTy);
  auto intTy = IntType::get(&getContext(), astCtx->getTypeSize(astCtx->IntTy),
                            astCtx->IntTy->isSignedIntegerType());
  auto charTy = IntType::get(&getContext(), astCtx->getCharWidth(), false);

  mlir::Value fatbinHandle = *regGlobalFunc.args_begin();

  cir::CIRBaseBuilderTy globalBuilder(getContext());
  globalBuilder.setInsertionPointToStart(mlirModule.getBody());

  FuncOp cudaRegisterFunction = buildRuntimeFunction(
      globalBuilder, addUnderscoredPrefix(cudaPrefix, "RegisterFunction"), loc,
      FuncType::get({voidPtrPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, intTy,
                     voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy},
                    intTy));

  auto makeConstantString = [&](llvm::StringRef str) -> GlobalOp {
    auto strType = ArrayType::get(&getContext(), charTy, 1 + str.size());

    auto tmpString =
        GlobalOp::create(globalBuilder, loc, (".str" + str).str(), strType,
                         /*isConstant=*/true,
                         /*linkage=*/cir::GlobalLinkageKind::PrivateLinkage);

    tmpString.setInitialValueAttr(ConstArrayAttr::get(
        strType, StringAttr::get(&getContext(), str + "\0")));
    tmpString.setPrivate();
    return tmpString;
  };

  auto cirNullPtr = builder.getNullPtr(voidPtrTy, loc);
  for (auto kernelName : cudaKernelMap.keys()) {
    FuncOp deviceStub = cudaKernelMap[kernelName];
    GlobalOp deviceFuncStr = makeConstantString(kernelName);
    mlir::Value deviceFunc = builder.createBitcast(
        builder.createGetGlobal(deviceFuncStr), voidPtrTy);
    if (astCtx->getLangOpts().HIP) {
      auto funcHandle = cast<GlobalOp>(mlirModule.lookupSymbol(kernelName));
      mlir::Value hostFunc =
          builder.createBitcast(builder.createGetGlobal(funcHandle), voidPtrTy);
      builder.createCallOp(
          loc, cudaRegisterFunction,
          {fatbinHandle, hostFunc, deviceFunc, deviceFunc,
           ConstantOp::create(builder, loc, IntAttr::get(intTy, -1)),
           cirNullPtr, cirNullPtr, cirNullPtr, cirNullPtr, cirNullPtr});
    } else {
      mlir::Value hostFunc = builder.createBitcast(
          GetGlobalOp::create(
              builder, loc, PointerType::get(deviceStub.getFunctionType()),
              mlir::FlatSymbolRefAttr::get(deviceStub.getSymNameAttr())),
          voidPtrTy);
      builder.createCallOp(
          loc, cudaRegisterFunction,
          {fatbinHandle, hostFunc, deviceFunc, deviceFunc,
           ConstantOp::create(builder, loc, IntAttr::get(intTy, -1)),
           cirNullPtr, cirNullPtr, cirNullPtr, cirNullPtr, cirNullPtr});
    }
  }
}

void LoweringPreparePass::buildCUDARegisterVars(cir::CIRBaseBuilderTy &builder,
                                                FuncOp regGlobalFunc) {
  if (cudaVarMap.empty())
    return;

  auto loc = mlirModule.getLoc();
  auto cudaPrefix = getCUDAPrefix(astCtx);

  auto voidTy = VoidType::get(&getContext());
  auto voidPtrTy = PointerType::get(voidTy);
  auto voidPtrPtrTy = PointerType::get(voidPtrTy);
  auto intTy = IntType::get(&getContext(), astCtx->getTypeSize(astCtx->IntTy),
                            astCtx->IntTy->isSignedIntegerType());
  auto charTy = IntType::get(&getContext(), astCtx->getCharWidth(), false);
  unsigned sizeTypeSize = astCtx->getTypeSize(astCtx->getSignedSizeType());
  auto sizeTy = IntType::get(&getContext(), sizeTypeSize, false);

  mlir::Value fatbinHandle = *regGlobalFunc.args_begin();

  cir::CIRBaseBuilderTy globalBuilder(getContext());
  globalBuilder.setInsertionPointToStart(mlirModule.getBody());

  FuncOp cudaRegisterVar = buildRuntimeFunction(
      globalBuilder, addUnderscoredPrefix(cudaPrefix, "RegisterVar"), loc,
      FuncType::get({voidPtrPtrTy, voidPtrTy, voidPtrTy, voidPtrTy, intTy,
                     sizeTy, intTy, intTy},
                    voidTy));

  unsigned int count = 0;
  auto makeConstantString = [&](llvm::StringRef str) -> GlobalOp {
    auto strType = ArrayType::get(&getContext(), charTy, 1 + str.size());

    auto tmpString = GlobalOp::create(
        globalBuilder, loc, (".str" + str + std::to_string(count++)).str(),
        strType, /*isConstant=*/true,
        /*linkage=*/cir::GlobalLinkageKind::PrivateLinkage);

    tmpString.setInitialValueAttr(ConstArrayAttr::get(
        strType, StringAttr::get(&getContext(), str + "\0")));
    tmpString.setPrivate();
    return tmpString;
  };

  for (auto &[deviceSideName, global] : cudaVarMap) {
    GlobalOp varNameStr = makeConstantString(deviceSideName);
    mlir::Value varNameValue =
        builder.createBitcast(builder.createGetGlobal(varNameStr), voidPtrTy);

    auto globalVarValue =
        builder.createBitcast(builder.createGetGlobal(global), voidPtrTy);

    auto deviceRegistrationAttr =
        global->getAttrOfType<cir::CUDAVarRegistrationInfoAttr>(
            cir::CUDAVarRegistrationInfoAttr::getMnemonic());

    bool isExternFlag =
        deviceRegistrationAttr ? deviceRegistrationAttr.getIsExtern() : false;
    bool isConstantFlag = deviceRegistrationAttr
                              ? deviceRegistrationAttr.getIsConstant()
                              : global.getConstant();
    bool isManagedFlag =
        deviceRegistrationAttr ? deviceRegistrationAttr.getIsManaged() : false;

    auto kind = deviceRegistrationAttr ? deviceRegistrationAttr.getKind()
                                       : cir::CUDADeviceVarKind::Variable;

    switch (kind) {
    case cir::CUDADeviceVarKind::Variable: {
      if (isManagedFlag)
        llvm_unreachable("Managed Variables NYI");

      auto isExtern =
          ConstantOp::create(builder, loc, IntAttr::get(intTy, isExternFlag));
      // Get type size - use the global's type to determine the size
      auto globalTy = global.getSymType();
      unsigned typeSizeInBits = 0;
      if (auto intType = dyn_cast<cir::IntType>(globalTy))
        typeSizeInBits = intType.getWidth();
      else
        typeSizeInBits = 64; // fallback for pointer types
      auto varSize = ConstantOp::create(
          builder, loc, IntAttr::get(sizeTy, typeSizeInBits / 8));
      auto isConstant =
          ConstantOp::create(builder, loc, IntAttr::get(intTy, isConstantFlag));
      auto zero = ConstantOp::create(builder, loc, IntAttr::get(intTy, 0));
      builder.createCallOp(loc, cudaRegisterVar,
                           {fatbinHandle, globalVarValue, varNameValue,
                            varNameValue, isExtern, varSize, isConstant, zero});
    } break;
    case cir::CUDADeviceVarKind::Surface:
      llvm_unreachable("Surface Registration NYI");
      break;
    case cir::CUDADeviceVarKind::Texture:
      llvm_unreachable("Texture Registration NYI");
      break;
    }
  }
}

std::optional<FuncOp> LoweringPreparePass::buildHIPModuleDtor() {
  if (!mlirModule->getAttr(cir::CIRDialect::getCUDABinaryHandleAttrName()))
    return {};

  std::string prefix = getCUDAPrefix(astCtx);

  auto voidTy = VoidType::get(&getContext());
  auto voidPtrPtrTy = PointerType::get(PointerType::get(voidTy));

  auto loc = mlirModule.getLoc();

  cir::CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointToStart(mlirModule.getBody());

  std::string unregisterFuncName =
      addUnderscoredPrefix(prefix, "UnregisterFatBinary");
  FuncOp unregisterFunc = buildRuntimeFunction(
      builder, unregisterFuncName, loc, FuncType::get({voidPtrPtrTy}, voidTy));

  std::string dtorName = addUnderscoredPrefix(prefix, "_module_dtor");
  FuncOp dtor =
      buildRuntimeFunction(builder, dtorName, loc, FuncType::get({}, voidTy),
                           GlobalLinkageKind::InternalLinkage);

  std::string gpubinName = addUnderscoredPrefix(prefix, "_gpubin_handle");
  auto gpuBinGlobal = cast<GlobalOp>(mlirModule.lookupSymbol(gpubinName));
  auto *entryBlock = dtor.addEntryBlock();
  auto *ifBlock = builder.createBlock(&dtor.getBody());
  auto *exitBlock = builder.createBlock(&dtor.getBody());
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToEnd(entryBlock);
  mlir::Value handle =
      builder.createLoad(loc, builder.createGetGlobal(gpuBinGlobal));
  auto handlePtrTy = llvm::cast<cir::PointerType>(handle.getType());
  mlir::Value nullPtr = builder.getNullPtr(handlePtrTy, loc);
  auto isNull = builder.createCompare(loc, cir::CmpOpKind::ne, handle, nullPtr);
  cir::BrCondOp::create(builder, loc, isNull, ifBlock, exitBlock);
  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(ifBlock);
    builder.createCallOp(loc, unregisterFunc, handle);
    builder.createStore(loc, nullPtr, builder.createGetGlobal(gpuBinGlobal));
    cir::BrOp::create(builder, loc, exitBlock);
  }
  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(exitBlock);
    cir::ReturnOp::create(builder, loc);
  }
  return dtor;
}

std::optional<FuncOp> LoweringPreparePass::buildCUDAModuleDtor() {
  if (!mlirModule->getAttr(cir::CIRDialect::getCUDABinaryHandleAttrName()))
    return {};

  std::string prefix = getCUDAPrefix(astCtx);

  auto voidTy = VoidType::get(&getContext());
  auto voidPtrPtrTy = PointerType::get(PointerType::get(voidTy));

  auto loc = mlirModule.getLoc();

  cir::CIRBaseBuilderTy builder(getContext());
  builder.setInsertionPointToStart(mlirModule.getBody());

  std::string unregisterFuncName =
      addUnderscoredPrefix(prefix, "UnregisterFatBinary");
  FuncOp unregisterFunc = buildRuntimeFunction(
      builder, unregisterFuncName, loc, FuncType::get({voidPtrPtrTy}, voidTy));

  std::string dtorName = addUnderscoredPrefix(prefix, "_module_dtor");
  FuncOp dtor =
      buildRuntimeFunction(builder, dtorName, loc, FuncType::get({}, voidTy),
                           GlobalLinkageKind::InternalLinkage);

  std::string gpubinName = addUnderscoredPrefix(prefix, "_gpubin_handle");
  auto gpuBinGlobal = cast<GlobalOp>(mlirModule.lookupSymbol(gpubinName));
  builder.setInsertionPointToStart(dtor.addEntryBlock());
  mlir::Value handle =
      builder.createLoad(loc, builder.createGetGlobal(gpuBinGlobal));
  builder.createCallOp(loc, unregisterFunc, handle);
  cir::ReturnOp::create(builder, loc);
  return dtor;
}

void LoweringPreparePass::runOnOp(mlir::Operation *op) {
  if (auto arrayCtor = dyn_cast<cir::ArrayCtor>(op)) {
    lowerArrayCtor(arrayCtor);
  } else if (auto arrayDtor = dyn_cast<cir::ArrayDtor>(op)) {
    lowerArrayDtor(arrayDtor);
  } else if (auto cast = mlir::dyn_cast<cir::CastOp>(op)) {
    lowerCastOp(cast);
  } else if (auto complexAdd = mlir::dyn_cast<cir::ComplexAddOp>(op)) {
    lowerComplexAddOp(complexAdd);
  } else if (auto complexSub = mlir::dyn_cast<cir::ComplexSubOp>(op)) {
    lowerComplexSubOp(complexSub);
  } else if (auto complexDiv = mlir::dyn_cast<cir::ComplexDivOp>(op)) {
    lowerComplexDivOp(complexDiv);
  } else if (auto complexMul = mlir::dyn_cast<cir::ComplexMulOp>(op)) {
    lowerComplexMulOp(complexMul);
  } else if (auto glob = mlir::dyn_cast<cir::GlobalOp>(op)) {
    lowerGlobalOp(glob);
    if (auto attr = op->getAttr(cir::CUDAShadowNameAttr::getMnemonic())) {
      auto shadowNameAttr = dyn_cast<cir::CUDAShadowNameAttr>(attr);
      std::string deviceSideName = shadowNameAttr.getDeviceSideName();
      cudaVarMap[deviceSideName] = glob;
    }
  } else if (auto dynamicCast = mlir::dyn_cast<cir::DynamicCastOp>(op)) {
    lowerDynamicCastOp(dynamicCast);
  } else if (auto unary = mlir::dyn_cast<cir::UnaryOp>(op)) {
    lowerUnaryOp(unary);
  } else if (auto callOp = dyn_cast<cir::CallOp>(op)) {
    lowerTrivialCopyCall(callOp);
  } else if (auto stdFind = dyn_cast<cir::StdFindOp>(op)) {
    lowerStdFindOp(stdFind);
  } else if (auto strLen = dyn_cast<cir::StrLenOp>(op)) {
    lowerStrLenOp(strLen);
  } else if (auto iterBegin = dyn_cast<cir::IterBeginOp>(op)) {
    lowerIterBeginOp(iterBegin);
  } else if (auto iterEnd = dyn_cast<cir::IterEndOp>(op)) {
    lowerIterEndOp(iterEnd);
  } else if (auto threeWayCmp = dyn_cast<cir::CmpThreeWayOp>(op)) {
    lowerThreeWayCmpOp(threeWayCmp);
  } else if (auto throwOp = dyn_cast<cir::ThrowOp>(op)) {
    lowerThrowOp(throwOp);
  } else if (auto vaArgOp = dyn_cast<cir::VAArgOp>(op)) {
    lowerVAArgOp(vaArgOp);
  } else if (auto fnOp = dyn_cast<cir::FuncOp>(op)) {
    if (auto globalCtor = fnOp.getGlobalCtorPriority())
      globalCtorList.emplace_back(fnOp.getName(), globalCtor.value());
    else if (auto globalDtor = fnOp.getGlobalDtorPriority())
      globalDtorList.emplace_back(fnOp.getName(), globalDtor.value());
    if (auto annotations = fnOp.getAnnotationsAttr())
      addGlobalAnnotations(fnOp, annotations);
    if (auto extraAttrs = fnOp.getExtraAttrs()) {
      if (auto attr = extraAttrs->getElements().get(
              cir::CUDAKernelNameAttr::getMnemonic())) {
        auto cudaKernelAttr = dyn_cast<cir::CUDAKernelNameAttr>(attr);
        std::string kernelName = cudaKernelAttr.getKernelName();
        cudaKernelMap[kernelName] = fnOp;
      }
    }
  }
}

void LoweringPreparePass::runOnOperation() {
  mlir::Operation *op = getOperation();
  if (isa<::mlir::ModuleOp>(op)) {
    mlirModule = cast<::mlir::ModuleOp>(op);
    datalayout.emplace(mlirModule);
  }

  if (mlirModule)
    datalayout.emplace(mlirModule);

  llvm::SmallVector<mlir::Operation *> opsToTransform;

  op->walk([&](mlir::Operation *op) {
    if (mlir::isa<cir::ArrayCtor, cir::ArrayDtor, cir::CastOp,
                  cir::ComplexAddOp, cir::ComplexSubOp, cir::ComplexMulOp,
                  cir::ComplexDivOp, cir::CmpThreeWayOp, cir::VAArgOp,
                  cir::DynamicCastOp, cir::FuncOp, cir::CallOp, cir::GlobalOp,
                  cir::UnaryOp, cir::StdFindOp, cir::StrLenOp, cir::IterEndOp,
                  cir::IterBeginOp, cir::ThrowOp>(op))
      opsToTransform.push_back(op);
  });

  for (mlir::Operation *o : opsToTransform)
    runOnOp(o);

  if (astCtx->getLangOpts().CUDA && !astCtx->getLangOpts().CUDAIsDevice) {
    buildCUDAModuleCtor();
  }

  buildCXXGlobalInitFunc();
  buildGlobalCtorDtorList();
  buildGlobalAnnotationValues();
}

std::unique_ptr<Pass> mlir::createLoweringPreparePass() {
  return std::make_unique<LoweringPreparePass>();
}

std::unique_ptr<Pass>
mlir::createLoweringPreparePass(clang::ASTContext *astCtx) {
  auto pass = std::make_unique<LoweringPreparePass>();
  pass->setASTContext(astCtx);
  return std::move(pass);
}
