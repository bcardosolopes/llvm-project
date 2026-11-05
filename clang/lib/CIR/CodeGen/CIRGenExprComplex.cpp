#include "CIRGenBuilder.h"
#include "CIRGenConstantEmitter.h"
#include "CIRGenFunction.h"

#include "clang/AST/StmtVisitor.h"

using namespace clang;
using namespace clang::CIRGen;

#ifndef NDEBUG
/// Return the complex type that we are meant to emit.
static const ComplexType *getComplexType(QualType type) {
  type = type.getCanonicalType();
  if (const ComplexType *comp = dyn_cast<ComplexType>(type))
    return comp;
  return cast<ComplexType>(cast<AtomicType>(type)->getValueType());
}
#endif // NDEBUG

namespace {
class ComplexExprEmitter : public StmtVisitor<ComplexExprEmitter, mlir::Value> {
  CIRGenFunction &cgf;
  CIRGenBuilderTy &builder;

public:
  explicit ComplexExprEmitter(CIRGenFunction &cgf)
      : cgf(cgf), builder(cgf.getBuilder()) {}

  //===--------------------------------------------------------------------===//
  //                               Utilities
  //===--------------------------------------------------------------------===//

  /// Given an expression with complex type that represents a value l-value,
  /// this method emits the address of the l-value, then loads and returns the
  /// result.
  mlir::Value emitLoadOfLValue(const Expr *e) {
    return emitLoadOfLValue(cgf.emitLValue(e), e->getExprLoc());
  }

  mlir::Value emitLoadOfLValue(LValue lv, SourceLocation loc);

  /// Store the specified real/imag parts into the
  /// specified value pointer.
  void emitStoreOfComplex(mlir::Location loc, mlir::Value val, LValue lv,
                          bool isInit);

  /// Emit a cast from complex value Val to DestType.
  mlir::Value emitComplexToComplexCast(mlir::Value value, QualType srcType,
                                       QualType destType, SourceLocation loc);

  /// Attach FP constraint attributes to a CastOp if we're in a constrained
  /// FP context. Used to propagate strictfp metadata through complex casts
  /// that get decomposed in LoweringPrepare.
  void attachFPConstraints(mlir::Value val, clang::FPOptions fpFeatures) {
    if (!(fpFeatures.isFPConstrained() || cgf.IsFPConstrained))
      return;
    if (auto castOp = val.getDefiningOp<cir::CastOp>()) {
      auto *ctx = &cgf.getMLIRContext();
      if (auto fpExcept = cgf.getFPExceptionBehavior(ctx, fpFeatures))
        castOp->setAttr("cir.fp_exception", fpExcept);
      if (auto roundMode = CIRGenFunction::getRoundingMode(ctx, fpFeatures))
        castOp->setAttr("cir.rounding_mode", roundMode);
    }
  }

  /// Emit a cast from scalar value Val to DestType.
  mlir::Value emitScalarToComplexCast(mlir::Value value, QualType srcType,
                                      QualType destType, SourceLocation loc);

  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//

  mlir::Value Visit(Expr *e) {
    // OGCG applies debug location for each visited expression via
    // ApplyDebugLocation DL(CGF, E) (CGExprComplex.cpp:100).
    // CIR ops carry their own mlir::Location, so this is handled inherently.
    return StmtVisitor<ComplexExprEmitter, mlir::Value>::Visit(e);
  }

  mlir::Value VisitStmt(Stmt *s) {
    cgf.cgm.errorNYI(s->getSourceRange(),
                     "ComplexExprEmitter::VisitStmt");
    return {};
  }

  mlir::Value VisitExpr(Expr *e);
  mlir::Value VisitConstantExpr(ConstantExpr *e) {
    if (mlir::Attribute result = ConstantEmitter(cgf).tryEmitConstantExpr(e))
      return builder.getConstant(cgf.getLoc(e->getSourceRange()),
                                 mlir::cast<mlir::TypedAttr>(result));

    return Visit(e->getSubExpr());
  }

  mlir::Value VisitParenExpr(ParenExpr *pe) { return Visit(pe->getSubExpr()); }
  mlir::Value VisitGenericSelectionExpr(GenericSelectionExpr *ge) {
    return Visit(ge->getResultExpr());
  }
  mlir::Value VisitImaginaryLiteral(const ImaginaryLiteral *il);
  mlir::Value
  VisitSubstNonTypeTemplateParmExpr(SubstNonTypeTemplateParmExpr *pe) {
    return Visit(pe->getReplacement());
  }
  mlir::Value VisitCoawaitExpr(CoawaitExpr *s) {
    return cgf.emitCoawaitExpr(*s).getComplexValue();
  }
  mlir::Value VisitCoyieldExpr(CoyieldExpr *s) {
    return cgf.emitCoyieldExpr(*s).getComplexValue();
  }
  mlir::Value VisitUnaryCoawait(const UnaryOperator *e) {
    return Visit(e->getSubExpr());
  }

  mlir::Value emitConstant(const CIRGenFunction::ConstantEmission &constant,
                           Expr *e) {
    assert(constant && "not a constant");
    if (constant.isReference())
      return emitLoadOfLValue(constant.getReferenceLValue(cgf, e),
                              e->getExprLoc());

    mlir::TypedAttr valueAttr = constant.getValue();
    return builder.getConstant(cgf.getLoc(e->getSourceRange()), valueAttr);
  }

  // l-values.
  mlir::Value VisitDeclRefExpr(DeclRefExpr *e) {
    if (CIRGenFunction::ConstantEmission constant = cgf.tryEmitAsConstant(e))
      return emitConstant(constant, e);
    return emitLoadOfLValue(e);
  }
  mlir::Value VisitObjCIvarRefExpr(ObjCIvarRefExpr *e) {
    cgf.cgm.errorNYI(e->getSourceRange(),
                     "ComplexExprEmitter::VisitObjCIvarRefExpr");
    return {};
  }
  mlir::Value VisitObjCMessageExpr(ObjCMessageExpr *e) {
    return cgf.emitObjCMessageExpr(e).getComplexValue();
  }
  mlir::Value VisitArraySubscriptExpr(Expr *e) { return emitLoadOfLValue(e); }
  mlir::Value VisitMemberExpr(MemberExpr *me) {
    if (CIRGenFunction::ConstantEmission constant = cgf.tryEmitAsConstant(me)) {
      cgf.emitIgnoredExpr(me->getBase());
      return emitConstant(constant, me);
    }
    return emitLoadOfLValue(me);
  }
  mlir::Value VisitOpaqueValueExpr(OpaqueValueExpr *e) {
    if (e->isGLValue())
      return emitLoadOfLValue(cgf.getOrCreateOpaqueLValueMapping(e),
                              e->getExprLoc());
    return cgf.getOrCreateOpaqueRValueMapping(e).getComplexValue();
  }

  mlir::Value VisitPseudoObjectExpr(PseudoObjectExpr *e) {
    cgf.cgm.errorNYI(e->getSourceRange(),
                     "ComplexExprEmitter::VisitPseudoObjectExpr");
    return {};
  }

  mlir::Value emitCast(CastKind ck, Expr *op, QualType destTy);
  mlir::Value VisitImplicitCastExpr(ImplicitCastExpr *e) {
    // Unlike for scalars, we don't have to worry about function->ptr demotion
    // here.
    if (e->changesVolatileQualification())
      return emitLoadOfLValue(e);
    mlir::Value result =
        emitCast(e->getCastKind(), e->getSubExpr(), e->getType());
    attachFPConstraints(result, e->getFPFeaturesInEffect(cgf.getLangOpts()));
    return result;
  }
  mlir::Value VisitCastExpr(CastExpr *e) {
    if (const auto *ece = dyn_cast<ExplicitCastExpr>(e)) {
      // Bind VLAs in the cast type.
      if (ece->getType()->isVariablyModifiedType()) {
        cgf.cgm.errorNYI(ece->getSourceRange(),
                         "VisitCastExpr VLA type binding for complex");
        return {};
      }
      // Keep explicit-cast type side effects in sync with scalar code paths
      // (VLA binding + any other type-dependent bookkeeping).
      cgf.cgm.emitExplicitCastExprType(ece, &cgf);
    }

    if (e->changesVolatileQualification())
      return emitLoadOfLValue(e);

    // Set up strict FP context before emitting the cast, matching OGCG's
    // CGFPOptionsRAII usage. This ensures inner scalar conversions (e.g.
    // double→float in a FloatingRealToComplex) use constrained intrinsics.
    FPOptions fpFeatures = e->getFPFeaturesInEffect(cgf.getLangOpts());
    bool savedFPConstrained = cgf.IsFPConstrained;
    if (fpFeatures.isFPConstrained())
      cgf.IsFPConstrained = true;

    mlir::Value result =
        emitCast(e->getCastKind(), e->getSubExpr(), e->getType());

    cgf.IsFPConstrained = savedFPConstrained;

    attachFPConstraints(result, fpFeatures);
    return result;
  }
  mlir::Value VisitCallExpr(const CallExpr *e);
  mlir::Value VisitStmtExpr(const StmtExpr *e);

  // Operators.
  mlir::Value VisitPrePostIncDec(const UnaryOperator *e, cir::UnaryOpKind op,
                                 bool isPre) {
    LValue lv = cgf.emitLValue(e->getSubExpr());
    return cgf.emitComplexPrePostIncDec(e, lv, op, isPre);
  }
  mlir::Value VisitUnaryPostDec(const UnaryOperator *e) {
    return VisitPrePostIncDec(e, cir::UnaryOpKind::Dec, false);
  }
  mlir::Value VisitUnaryPostInc(const UnaryOperator *e) {
    return VisitPrePostIncDec(e, cir::UnaryOpKind::Inc, false);
  }
  mlir::Value VisitUnaryPreDec(const UnaryOperator *e) {
    return VisitPrePostIncDec(e, cir::UnaryOpKind::Dec, true);
  }
  mlir::Value VisitUnaryPreInc(const UnaryOperator *e) {
    return VisitPrePostIncDec(e, cir::UnaryOpKind::Inc, true);
  }
  mlir::Value VisitUnaryDeref(const Expr *e) { return emitLoadOfLValue(e); }

  mlir::Value VisitUnaryPlus(const UnaryOperator *e);
  mlir::Value VisitUnaryMinus(const UnaryOperator *e);
  mlir::Value VisitPlusMinus(const UnaryOperator *e, cir::UnaryOpKind kind,
                             QualType promotionType);
  mlir::Value VisitUnaryNot(const UnaryOperator *e);
  // LNot,Real,Imag never return complex.
  mlir::Value VisitUnaryExtension(const UnaryOperator *e) {
    return Visit(e->getSubExpr());
  }
  mlir::Value VisitCXXDefaultArgExpr(CXXDefaultArgExpr *dae) {
    CIRGenFunction::CXXDefaultArgExprScope scope(cgf, dae);
    return Visit(dae->getExpr());
  }
  mlir::Value VisitCXXDefaultInitExpr(CXXDefaultInitExpr *die) {
    CIRGenFunction::CXXDefaultInitExprScope scope(cgf, die);
    return Visit(die->getExpr());
  }
  mlir::Value VisitExprWithCleanups(ExprWithCleanups *e) {
    CIRGenFunction::SourceLocRAIIObject exprLoc{
        cgf, cgf.getLoc(e->getSubExpr()->getExprLoc())};
    CIRGenFunction::RunCleanupsScope scope(cgf);
    mlir::Value result = Visit(e->getSubExpr());
    // OGCG calls Scope.ForceCleanup({&Vals.first, &Vals.second}) to defend
    // against SSA dominance problems in shared cleanup blocks. CIR's
    // structured control flow (MLIR regions) inherently avoids this issue —
    // values remain valid within their defining scope. Intentional divergence.
    return result;
  }
  mlir::Value VisitCXXScalarValueInitExpr(CXXScalarValueInitExpr *e) {
    mlir::Location loc = cgf.getLoc(e->getExprLoc());
    mlir::Type complexTy = cgf.convertType(e->getType());
    return builder.getNullValue(complexTy, loc);
  }
  mlir::Value VisitImplicitValueInitExpr(ImplicitValueInitExpr *e) {
    assert(e->getType()->isAnyComplexType() && "Expected complex type!");
    mlir::Location loc = cgf.getLoc(e->getExprLoc());
    mlir::Type complexTy = cgf.convertType(e->getType());
    return builder.getNullValue(complexTy, loc);
  }

  struct BinOpInfo {
    mlir::Location loc;
    mlir::Value lhs{};
    mlir::Value rhs{};
    QualType ty{}; // Computation Type.
    FPOptions fpFeatures{};
  };

  BinOpInfo emitBinOps(const BinaryOperator *e,
                       QualType promotionTy = QualType());

  mlir::Value emitPromoted(const Expr *e, QualType promotionTy);
  mlir::Value emitPromotedComplexOperand(const Expr *e, QualType promotionTy);
  LValue emitCompoundAssignLValue(
      const CompoundAssignOperator *e,
      mlir::Value (ComplexExprEmitter::*func)(const BinOpInfo &),
      RValue &value);
  mlir::Value emitCompoundAssign(
      const CompoundAssignOperator *e,
      mlir::Value (ComplexExprEmitter::*func)(const BinOpInfo &));

  mlir::Value emitBinAdd(const BinOpInfo &op);
  mlir::Value emitBinSub(const BinOpInfo &op);
  mlir::Value emitBinMul(const BinOpInfo &op);
  mlir::Value emitBinDiv(const BinOpInfo &op);

  QualType higherPrecisionTypeForComplexArithmetic(QualType elementTy) {
    ASTContext &ctx = cgf.getContext();
    const QualType higherElementTy = ctx.GetHigherPrecisionFPType(elementTy);
    const llvm::fltSemantics &elementTySemantics =
        ctx.getFloatTypeSemantics(elementTy);
    const llvm::fltSemantics &higherElementTySemantics =
        ctx.getFloatTypeSemantics(higherElementTy);

    if (llvm::APFloat::semanticsMaxExponent(elementTySemantics) * 2 + 1 <=
        llvm::APFloat::semanticsMaxExponent(higherElementTySemantics)) {
      if (!ctx.getTargetInfo().hasLongDoubleType() &&
          higherElementTy.getCanonicalType().getUnqualifiedType() ==
              ctx.LongDoubleTy)
        return QualType();
      return ctx.getComplexType(higherElementTy);
    }

    return QualType();
  }

  QualType getPromotionType(FPOptionsOverride features, QualType ty,
                            bool isComplexDivisor) {
    if (auto *complexTy = ty->getAs<ComplexType>()) {
      QualType elementTy = complexTy->getElementType().getCanonicalType();
      bool isFloatingType = elementTy->isFloatingType();
      bool isComplexRangePromoted =
          cgf.getLangOpts().getComplexRange() == LangOptions::CX_Promoted;
      bool hasNoComplexRangeOverride = !features.hasComplexRangeOverride();
      bool hasMatchingComplexRange =
          features.hasComplexRangeOverride() &&
          features.getComplexRangeOverride() ==
              cgf.getLangOpts().getComplexRange();

      if (isComplexDivisor && isFloatingType && isComplexRangePromoted &&
          (hasNoComplexRangeOverride || hasMatchingComplexRange))
        return higherPrecisionTypeForComplexArithmetic(elementTy);

      if (elementTy.UseExcessPrecision(cgf.getContext()))
        return cgf.getContext().getComplexType(cgf.getContext().FloatTy);
    }

    if (ty.UseExcessPrecision(cgf.getContext()))
      return cgf.getContext().FloatTy;
    return QualType();
  }

#define HANDLEBINOP(OP)                                                        \
  mlir::Value VisitBin##OP(const BinaryOperator *e) {                          \
    bool isComplexDivisor =                                                     \
        e->getOpcode() == BinaryOperatorKind::BO_Div &&                         \
        e->getRHS()->getType()->isAnyComplexType();                             \
    QualType promotionTy = getPromotionType(                                    \
        e->getStoredFPFeaturesOrDefault(), e->getType(), isComplexDivisor);    \
    mlir::Value result = emitBin##OP(emitBinOps(e, promotionTy));              \
    if (!promotionTy.isNull())                                                 \
      result = cgf.emitUnPromotedValue(result, e->getType());                  \
    return result;                                                             \
  }

  HANDLEBINOP(Add)
  HANDLEBINOP(Sub)
  HANDLEBINOP(Mul)
  HANDLEBINOP(Div)
#undef HANDLEBINOP

  mlir::Value VisitCXXRewrittenBinaryOperator(CXXRewrittenBinaryOperator *e) {
    return Visit(e->getSemanticForm());
  }

  // Compound assignments.
  mlir::Value VisitBinAddAssign(const CompoundAssignOperator *e) {
    return emitCompoundAssign(e, &ComplexExprEmitter::emitBinAdd);
  }
  mlir::Value VisitBinSubAssign(const CompoundAssignOperator *e) {
    return emitCompoundAssign(e, &ComplexExprEmitter::emitBinSub);
  }
  mlir::Value VisitBinMulAssign(const CompoundAssignOperator *e) {
    return emitCompoundAssign(e, &ComplexExprEmitter::emitBinMul);
  }
  mlir::Value VisitBinDivAssign(const CompoundAssignOperator *e) {
    return emitCompoundAssign(e, &ComplexExprEmitter::emitBinDiv);
  }

  // GCC rejects rem/and/or/xor for integer complex.
  // Logical and/or always return int, never complex.

  // No comparisons produce a complex result.

  LValue emitBinAssignLValue(const BinaryOperator *e, mlir::Value &val);
  mlir::Value VisitBinAssign(const BinaryOperator *e);
  mlir::Value VisitBinComma(const BinaryOperator *e);

  mlir::Value
  VisitAbstractConditionalOperator(const AbstractConditionalOperator *e);
  mlir::Value VisitChooseExpr(ChooseExpr *e);

  mlir::Value VisitInitListExpr(InitListExpr *e);

  mlir::Value VisitCompoundLiteralExpr(CompoundLiteralExpr *e) {
    return emitLoadOfLValue(e);
  }

  mlir::Value VisitVAArgExpr(VAArgExpr *e);

  mlir::Value VisitAtomicExpr(AtomicExpr *e) {
    return cgf.emitAtomicExpr(e).getComplexValue();
  }

  mlir::Value VisitPackIndexingExpr(PackIndexingExpr *e) {
    return Visit(e->getSelectedExpr());
  }
};
} // namespace

//===----------------------------------------------------------------------===//
//                                Utilities
//===----------------------------------------------------------------------===//

/// EmitLoadOfLValue - Given an RValue reference for a complex, emit code to
/// load the real and imaginary pieces, returning them as Real/Imag.
mlir::Value ComplexExprEmitter::emitLoadOfLValue(LValue lv,
                                                 SourceLocation loc) {
  assert(lv.isSimple() && "non-simple complex l-value?");
  if (lv.getType()->isAtomicType())
    return cgf.emitAtomicLoad(lv, loc).getComplexValue();

  const Address srcAddr = lv.getAddress();
  return builder.createLoad(cgf.getLoc(loc), srcAddr, lv.isVolatileQualified());
}

/// EmitStoreOfComplex - Store the specified real/imag parts into the
/// specified value pointer.
void ComplexExprEmitter::emitStoreOfComplex(mlir::Location loc, mlir::Value val,
                                            LValue lv, bool isInit) {
  if (lv.getType()->isAtomicType() ||
      (!isInit && cgf.isLValueSuitableForInlineAtomic(lv)))
    return cgf.emitAtomicStore(RValue::getComplex(val), lv, isInit);

  const Address destAddr = lv.getAddress();
  auto storeOp = builder.createStore(loc, val, destAddr,
                                     lv.isVolatileQualified());
  cgf.addOpToCurrentSourceAtom(storeOp, val);
}

//===----------------------------------------------------------------------===//
//                            Visitor Methods
//===----------------------------------------------------------------------===//

mlir::Value ComplexExprEmitter::VisitExpr(Expr *e) {
  cgf.cgm.errorNYI(e->getSourceRange(),
                   "ComplexExprEmitter::VisitExpr catch-all");
  return {};
}

mlir::Value
ComplexExprEmitter::VisitImaginaryLiteral(const ImaginaryLiteral *il) {
  auto ty = mlir::cast<cir::ComplexType>(cgf.convertType(il->getType()));
  mlir::Type elementTy = ty.getElementType();
  mlir::Location loc = cgf.getLoc(il->getExprLoc());

  mlir::TypedAttr realValueAttr;
  mlir::TypedAttr imagValueAttr;

  if (mlir::isa<cir::IntType>(elementTy)) {
    llvm::APInt imagValue = cast<IntegerLiteral>(il->getSubExpr())->getValue();
    realValueAttr = cir::IntAttr::get(elementTy, 0);
    imagValueAttr = cir::IntAttr::get(elementTy, imagValue);
  } else {
    assert(mlir::isa<cir::FPTypeInterface>(elementTy) &&
           "Expected complex element type to be floating-point");

    llvm::APFloat imagValue =
        cast<FloatingLiteral>(il->getSubExpr())->getValue();
    realValueAttr = cir::FPAttr::get(
        elementTy, llvm::APFloat::getZero(imagValue.getSemantics()));
    imagValueAttr = cir::FPAttr::get(elementTy, imagValue);
  }

  auto complexAttr = cir::ConstComplexAttr::get(realValueAttr, imagValueAttr);
  return cir::ConstantOp::create(builder, loc, complexAttr);
}

mlir::Value ComplexExprEmitter::VisitCallExpr(const CallExpr *e) {
  if (e->getCallReturnType(cgf.getContext())->isReferenceType())
    return emitLoadOfLValue(e);
  return cgf.emitCallExpr(e).getComplexValue();
}

mlir::Value ComplexExprEmitter::VisitStmtExpr(const StmtExpr *e) {
  CIRGenFunction::StmtExprEvaluation eval(cgf);
  Address retAlloca =
      cgf.createMemTemp(e->getType(), cgf.getLoc(e->getSourceRange()));
  (void)cgf.emitCompoundStmt(*e->getSubStmt(), &retAlloca);
  assert(retAlloca.isValid() && "Expected complex return value");
  return emitLoadOfLValue(cgf.makeAddrLValue(retAlloca, e->getType()),
                          e->getExprLoc());
}

mlir::Value ComplexExprEmitter::emitComplexToComplexCast(mlir::Value val,
                                                         QualType srcType,
                                                         QualType destType,
                                                         SourceLocation loc) {
  if (srcType == destType)
    return val;

  // Get the src/dest element type.
  QualType srcElemTy = srcType->castAs<ComplexType>()->getElementType();
  QualType destElemTy = destType->castAs<ComplexType>()->getElementType();

  cir::CastKind castOpKind;
  if (srcElemTy->isFloatingType() && destElemTy->isFloatingType())
    castOpKind = cir::CastKind::float_complex;
  else if (srcElemTy->isFloatingType() && destElemTy->isIntegerType())
    castOpKind = cir::CastKind::float_complex_to_int_complex;
  else if (srcElemTy->isIntegerType() && destElemTy->isFloatingType())
    castOpKind = cir::CastKind::int_complex_to_float_complex;
  else if (srcElemTy->isIntegerType() && destElemTy->isIntegerType())
    castOpKind = cir::CastKind::int_complex;
  else
    llvm_unreachable("unexpected src type or dest type");

  return builder.createCast(cgf.getLoc(loc), castOpKind, val,
                            cgf.convertType(destType));
}

mlir::Value ComplexExprEmitter::emitScalarToComplexCast(mlir::Value val,
                                                        QualType srcType,
                                                        QualType destType,
                                                        SourceLocation loc) {
  // Convert the input element to the element type of the complex.
  // This matches OGCG which calls EmitScalarConversion before pairing
  // (realval, 0). For example, bool → complex float requires converting
  // the bool to float first, then wrapping in complex.
  QualType elementType = destType->castAs<ComplexType>()->getElementType();
  val = cgf.emitScalarConversion(val, srcType, elementType, loc);
  srcType = elementType;

  cir::CastKind castOpKind;
  if (srcType->isFloatingType())
    castOpKind = cir::CastKind::float_to_complex;
  else if (srcType->isIntegerType())
    castOpKind = cir::CastKind::int_to_complex;
  else
    llvm_unreachable("unexpected src type");

  return builder.createCast(cgf.getLoc(loc), castOpKind, val,
                            cgf.convertType(destType));
}

mlir::Value ComplexExprEmitter::emitCast(CastKind ck, Expr *op,
                                         QualType destTy) {
  switch (ck) {
  case CK_Dependent:
    llvm_unreachable("dependent type must be resolved before the CIR codegen");

  case CK_NoOp:
  case CK_LValueToRValue:
    return Visit(op);

  case CK_UserDefinedConversion:
    return Visit(op);

  // Atomic to non-atomic casts may be more than a no-op for some platforms
  // and for some types.
  case CK_AtomicToNonAtomic:
  case CK_NonAtomicToAtomic:
    return Visit(op);

  case CK_LValueBitCast: {
    LValue origLV = cgf.emitLValue(op);
    Address addr =
        origLV.getAddress().withElementType(builder, cgf.convertType(destTy));
    LValue destLV = cgf.makeAddrLValue(addr, destTy);
    return emitLoadOfLValue(destLV, op->getExprLoc());
  }

  case CK_LValueToRValueBitCast: {
    LValue sourceLVal = cgf.emitLValue(op);
    Address addr = sourceLVal.getAddress().withElementType(
        builder, cgf.convertTypeForMem(destTy));
    LValue destLV = cgf.makeAddrLValue(addr, destTy);
    destLV.setTBAAInfo(TBAAAccessInfo::getMayAliasInfo());
    return emitLoadOfLValue(destLV, op->getExprLoc());
  }

  case CK_BitCast:
  case CK_BaseToDerived:
  case CK_DerivedToBase:
  case CK_UncheckedDerivedToBase:
  case CK_Dynamic:
  case CK_ToUnion:
  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay:
  case CK_NullToPointer:
  case CK_NullToMemberPointer:
  case CK_BaseToDerivedMemberPointer:
  case CK_DerivedToBaseMemberPointer:
  case CK_MemberPointerToBoolean:
  case CK_ReinterpretMemberPointer:
  case CK_ConstructorConversion:
  case CK_IntegralToPointer:
  case CK_PointerToIntegral:
  case CK_PointerToBoolean:
  case CK_ToVoid:
  case CK_VectorSplat:
  case CK_IntegralCast:
  case CK_BooleanToSignedIntegral:
  case CK_IntegralToBoolean:
  case CK_IntegralToFloating:
  case CK_FloatingToIntegral:
  case CK_FloatingToBoolean:
  case CK_FloatingCast:
  case CK_CPointerToObjCPointerCast:
  case CK_BlockPointerToObjCPointerCast:
  case CK_AnyPointerToBlockPointerCast:
  case CK_ObjCObjectLValueCast:
  case CK_FloatingComplexToReal:
  case CK_FloatingComplexToBoolean:
  case CK_IntegralComplexToReal:
  case CK_IntegralComplexToBoolean:
  case CK_ARCProduceObject:
  case CK_ARCConsumeObject:
  case CK_ARCReclaimReturnedObject:
  case CK_ARCExtendBlockObject:
  case CK_CopyAndAutoreleaseBlockObject:
  case CK_BuiltinFnToFnPtr:
  case CK_ZeroToOCLOpaqueType:
  case CK_AddressSpaceConversion:
  case CK_IntToOCLSampler:
  case CK_FloatingToFixedPoint:
  case CK_FixedPointToFloating:
  case CK_FixedPointCast:
  case CK_FixedPointToBoolean:
  case CK_FixedPointToIntegral:
  case CK_IntegralToFixedPoint:
  case CK_MatrixCast:
  case CK_HLSLVectorTruncation:
  case CK_HLSLMatrixTruncation:
  case CK_HLSLArrayRValue:
  case CK_HLSLElementwiseCast:
  case CK_HLSLAggregateSplatCast:
    llvm_unreachable("invalid cast kind for complex value");

  case CK_FloatingRealToComplex:
  case CK_IntegralRealToComplex: {
    return emitScalarToComplexCast(cgf.emitScalarExpr(op), op->getType(),
                                   destTy, op->getExprLoc());
  }

  case CK_FloatingComplexCast:
  case CK_FloatingComplexToIntegralComplex:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToFloatingComplex: {
    return emitComplexToComplexCast(Visit(op), op->getType(), destTy,
                                    op->getExprLoc());
  }
  }

  llvm_unreachable("unknown cast resulting in complex value");
}

mlir::Value ComplexExprEmitter::VisitUnaryPlus(const UnaryOperator *e) {
  QualType promotionTy =
      getPromotionType(FPOptionsOverride(), e->getSubExpr()->getType(),
                       /*isComplexDivisor=*/false);
  mlir::Value result = VisitPlusMinus(e, cir::UnaryOpKind::Plus, promotionTy);
  if (!promotionTy.isNull())
    return cgf.emitUnPromotedValue(result, e->getSubExpr()->getType());
  return result;
}

mlir::Value ComplexExprEmitter::VisitUnaryMinus(const UnaryOperator *e) {
  QualType promotionTy =
      getPromotionType(FPOptionsOverride(), e->getSubExpr()->getType(),
                       /*isComplexDivisor=*/false);
  mlir::Value result = VisitPlusMinus(e, cir::UnaryOpKind::Minus, promotionTy);
  if (!promotionTy.isNull())
    return cgf.emitUnPromotedValue(result, e->getSubExpr()->getType());
  return result;
}

mlir::Value ComplexExprEmitter::VisitPlusMinus(const UnaryOperator *e,
                                               cir::UnaryOpKind kind,
                                               QualType promotionType) {
  assert((kind == cir::UnaryOpKind::Plus ||
          kind == cir::UnaryOpKind::Minus) &&
         "Invalid UnaryOp kind for ComplexType Plus or Minus");

  mlir::Value op;
  if (!promotionType.isNull())
    // Avoid creating a nested ComplexExprEmitter for promoted operands while
    // already in a ComplexExprEmitter walk.
    op = emitPromoted(e->getSubExpr(), promotionType);
  else
    op = Visit(e->getSubExpr());
  return builder.createUnaryOp(cgf.getLoc(e->getExprLoc()), kind, op);
}

mlir::Value ComplexExprEmitter::VisitUnaryNot(const UnaryOperator *e) {
  mlir::Value op = Visit(e->getSubExpr());
  return builder.createNot(op);
}

mlir::Value ComplexExprEmitter::emitBinAdd(const BinOpInfo &op) {
  cir::FastMathFlags fmf =
      CIRGenFunction::getFastMathFlags(op.fpFeatures);
  auto *ctx = &cgf.getMLIRContext();
  auto fpExcept = cgf.getFPExceptionBehavior(ctx, op.fpFeatures);
  auto roundMode = CIRGenFunction::getRoundingMode(ctx, op.fpFeatures);

  if (mlir::isa<cir::ComplexType>(op.lhs.getType()) &&
      mlir::isa<cir::ComplexType>(op.rhs.getType())) {
    // When strict FP is enabled, decompose into scalar operations so that
    // constrained FP intrinsics are used. ComplexAddOp doesn't carry
    // FP exception/rounding attributes.
    if (fpExcept) {
      mlir::Value lhsR = builder.createComplexReal(op.loc, op.lhs);
      mlir::Value lhsI = builder.createComplexImag(op.loc, op.lhs);
      mlir::Value rhsR = builder.createComplexReal(op.loc, op.rhs);
      mlir::Value rhsI = builder.createComplexImag(op.loc, op.rhs);
      mlir::Value addR =
          builder.createFAdd(op.loc, lhsR, rhsR, fmf, fpExcept, roundMode);
      mlir::Value addI =
          builder.createFAdd(op.loc, lhsI, rhsI, fmf, fpExcept, roundMode);
      return builder.createComplexCreate(op.loc, addR, addI);
    }
    return cir::ComplexAddOp::create(builder, op.loc, op.lhs, op.rhs, fmf);
  }

  if (mlir::isa<cir::ComplexType>(op.lhs.getType())) {
    mlir::Value real = builder.createComplexReal(op.loc, op.lhs);
    mlir::Value imag = builder.createComplexImag(op.loc, op.lhs);
    mlir::Value newReal;
    if (cir::isFPOrVectorOfFPType(real.getType())) {
      newReal =
          builder.createFAdd(op.loc, real, op.rhs, fmf, fpExcept, roundMode);
    } else {
      newReal = builder.createAdd(op.loc, real, op.rhs);
    }
    return builder.createComplexCreate(op.loc, newReal, imag);
  }

  assert(mlir::isa<cir::ComplexType>(op.rhs.getType()));
  mlir::Value real = builder.createComplexReal(op.loc, op.rhs);
  mlir::Value imag = builder.createComplexImag(op.loc, op.rhs);
  mlir::Value newReal;
  if (cir::isFPOrVectorOfFPType(real.getType())) {
    newReal =
        builder.createFAdd(op.loc, op.lhs, real, fmf, fpExcept, roundMode);
  } else {
    newReal = builder.createAdd(op.loc, op.lhs, real);
  }
  return builder.createComplexCreate(op.loc, newReal, imag);
}

mlir::Value ComplexExprEmitter::emitBinSub(const BinOpInfo &op) {
  cir::FastMathFlags fmf =
      CIRGenFunction::getFastMathFlags(op.fpFeatures);
  auto *ctx = &cgf.getMLIRContext();
  auto fpExcept = cgf.getFPExceptionBehavior(ctx, op.fpFeatures);
  auto roundMode = CIRGenFunction::getRoundingMode(ctx, op.fpFeatures);

  if (mlir::isa<cir::ComplexType>(op.lhs.getType()) &&
      mlir::isa<cir::ComplexType>(op.rhs.getType())) {
    // When strict FP is enabled, decompose into scalar operations.
    if (fpExcept) {
      mlir::Value lhsR = builder.createComplexReal(op.loc, op.lhs);
      mlir::Value lhsI = builder.createComplexImag(op.loc, op.lhs);
      mlir::Value rhsR = builder.createComplexReal(op.loc, op.rhs);
      mlir::Value rhsI = builder.createComplexImag(op.loc, op.rhs);
      mlir::Value subR =
          builder.createFSub(op.loc, lhsR, rhsR, fmf, fpExcept, roundMode);
      mlir::Value subI =
          builder.createFSub(op.loc, lhsI, rhsI, fmf, fpExcept, roundMode);
      return builder.createComplexCreate(op.loc, subR, subI);
    }
    return cir::ComplexSubOp::create(builder, op.loc, op.lhs, op.rhs, fmf);
  }

  if (mlir::isa<cir::ComplexType>(op.lhs.getType())) {
    mlir::Value real = builder.createComplexReal(op.loc, op.lhs);
    mlir::Value imag = builder.createComplexImag(op.loc, op.lhs);
    mlir::Value newReal;
    if (cir::isFPOrVectorOfFPType(real.getType())) {
      newReal =
          builder.createFSub(op.loc, real, op.rhs, fmf, fpExcept, roundMode);
    } else {
      newReal = builder.createSub(op.loc, real, op.rhs);
    }
    return builder.createComplexCreate(op.loc, newReal, imag);
  }

  assert(mlir::isa<cir::ComplexType>(op.rhs.getType()));
  mlir::Value real = builder.createComplexReal(op.loc, op.rhs);
  mlir::Value imag = builder.createComplexImag(op.loc, op.rhs);
  mlir::Value newReal;
  if (cir::isFPOrVectorOfFPType(real.getType())) {
    newReal =
        builder.createFSub(op.loc, op.lhs, real, fmf, fpExcept, roundMode);
  } else {
    newReal = builder.createSub(op.loc, op.lhs, real);
  }
  mlir::Value negImag =
      builder.createUnaryOp(op.loc, cir::UnaryOpKind::Minus, imag);
  return builder.createComplexCreate(op.loc, newReal, negImag);
}

static cir::ComplexRangeKind
getComplexRangeAttr(LangOptions::ComplexRangeKind range) {
  switch (range) {
  case LangOptions::CX_Full:
    return cir::ComplexRangeKind::Full;
  case LangOptions::CX_Improved:
    return cir::ComplexRangeKind::Improved;
  case LangOptions::CX_Promoted:
    return cir::ComplexRangeKind::Promoted;
  case LangOptions::CX_Basic:
    return cir::ComplexRangeKind::Basic;
  case LangOptions::CX_None:
    // The default value for ComplexRangeKind is Full if no option is selected
    return cir::ComplexRangeKind::Full;
  }
}

mlir::Value ComplexExprEmitter::emitBinMul(const BinOpInfo &op) {
  cir::ComplexRangeKind rangeKind =
      getComplexRangeAttr(op.fpFeatures.getComplexRange());
  cir::FastMathFlags fmf =
      CIRGenFunction::getFastMathFlags(op.fpFeatures);
  auto *ctx = &cgf.getMLIRContext();
  auto fpExcept = cgf.getFPExceptionBehavior(ctx, op.fpFeatures);
  auto roundMode = CIRGenFunction::getRoundingMode(ctx, op.fpFeatures);

  if (mlir::isa<cir::ComplexType>(op.lhs.getType()) &&
      mlir::isa<cir::ComplexType>(op.rhs.getType())) {
    // When strict FP is enabled, decompose into scalar operations so that
    // constrained FP intrinsics are used. ComplexMulOp doesn't carry
    // FP exception/rounding attributes.
    if (fpExcept) {
      mlir::Value lhsR = builder.createComplexReal(op.loc, op.lhs);
      mlir::Value lhsI = builder.createComplexImag(op.loc, op.lhs);
      mlir::Value rhsR = builder.createComplexReal(op.loc, op.rhs);
      mlir::Value rhsI = builder.createComplexImag(op.loc, op.rhs);
      // (a+bi) * (c+di) = (ac-bd) + (ad+bc)i
      mlir::Value ac =
          builder.createFMul(op.loc, lhsR, rhsR, fmf, fpExcept, roundMode);
      mlir::Value bd =
          builder.createFMul(op.loc, lhsI, rhsI, fmf, fpExcept, roundMode);
      mlir::Value ad =
          builder.createFMul(op.loc, lhsR, rhsI, fmf, fpExcept, roundMode);
      mlir::Value bc =
          builder.createFMul(op.loc, lhsI, rhsR, fmf, fpExcept, roundMode);
      mlir::Value resR =
          builder.createFSub(op.loc, ac, bd, fmf, fpExcept, roundMode);
      mlir::Value resI =
          builder.createFAdd(op.loc, ad, bc, fmf, fpExcept, roundMode);
      return builder.createComplexCreate(op.loc, resR, resI);
    }
    return cir::ComplexMulOp::create(builder, op.loc, op.lhs, op.rhs,
                                     rangeKind, fmf);
  }

  if (mlir::isa<cir::ComplexType>(op.lhs.getType())) {
    mlir::Value real = builder.createComplexReal(op.loc, op.lhs);
    mlir::Value imag = builder.createComplexImag(op.loc, op.lhs);
    mlir::Value newReal, newImag;
    if (cir::isFPOrVectorOfFPType(real.getType())) {
      newReal =
          builder.createFMul(op.loc, real, op.rhs, fmf, fpExcept, roundMode);
      newImag =
          builder.createFMul(op.loc, imag, op.rhs, fmf, fpExcept, roundMode);
    } else {
      newReal = builder.createMul(op.loc, real, op.rhs);
      newImag = builder.createMul(op.loc, imag, op.rhs);
    }
    return builder.createComplexCreate(op.loc, newReal, newImag);
  }

  assert(mlir::isa<cir::ComplexType>(op.rhs.getType()));
  mlir::Value real = builder.createComplexReal(op.loc, op.rhs);
  mlir::Value imag = builder.createComplexImag(op.loc, op.rhs);
  mlir::Value newReal, newImag;
  if (cir::isFPOrVectorOfFPType(real.getType())) {
    newReal =
        builder.createFMul(op.loc, op.lhs, real, fmf, fpExcept, roundMode);
    newImag =
        builder.createFMul(op.loc, op.lhs, imag, fmf, fpExcept, roundMode);
  } else {
    newReal = builder.createMul(op.loc, op.lhs, real);
    newImag = builder.createMul(op.loc, op.lhs, imag);
  }
  return builder.createComplexCreate(op.loc, newReal, newImag);
}

mlir::Value ComplexExprEmitter::emitBinDiv(const BinOpInfo &op) {
  cir::ComplexRangeKind rangeKind =
      getComplexRangeAttr(op.fpFeatures.getComplexRange());
  cir::FastMathFlags fmf =
      CIRGenFunction::getFastMathFlags(op.fpFeatures);
  auto *ctx = &cgf.getMLIRContext();
  auto fpExcept = cgf.getFPExceptionBehavior(ctx, op.fpFeatures);
  auto roundMode = CIRGenFunction::getRoundingMode(ctx, op.fpFeatures);

  // Handle division between two complex values. In the case of complex integer
  // types mixed with scalar integers, the scalar integer type will always be
  // promoted to a complex integer value with a zero imaginary component when
  // the AST is formed.
  if (mlir::isa<cir::ComplexType>(op.lhs.getType()) &&
      mlir::isa<cir::ComplexType>(op.rhs.getType())) {
    // When strict FP is enabled, decompose into scalar operations so that
    // constrained FP intrinsics are used. ComplexDivOp doesn't carry
    // FP exception/rounding attributes. (follow-up)
    if (fpExcept) {
      mlir::Value lhsR = builder.createComplexReal(op.loc, op.lhs);
      mlir::Value lhsI = builder.createComplexImag(op.loc, op.lhs);
      mlir::Value rhsR = builder.createComplexReal(op.loc, op.rhs);
      mlir::Value rhsI = builder.createComplexImag(op.loc, op.rhs);
      // (a+bi) / (c+di) = ((ac+bd)/(cc+dd)) + i((bc-ad)/(cc+dd))
      mlir::Value ac =
          builder.createFMul(op.loc, lhsR, rhsR, fmf, fpExcept, roundMode);
      mlir::Value bd =
          builder.createFMul(op.loc, lhsI, rhsI, fmf, fpExcept, roundMode);
      mlir::Value acPbd =
          builder.createFAdd(op.loc, ac, bd, fmf, fpExcept, roundMode);
      mlir::Value cc =
          builder.createFMul(op.loc, rhsR, rhsR, fmf, fpExcept, roundMode);
      mlir::Value dd =
          builder.createFMul(op.loc, rhsI, rhsI, fmf, fpExcept, roundMode);
      mlir::Value ccPdd =
          builder.createFAdd(op.loc, cc, dd, fmf, fpExcept, roundMode);
      mlir::Value bc =
          builder.createFMul(op.loc, lhsI, rhsR, fmf, fpExcept, roundMode);
      mlir::Value ad =
          builder.createFMul(op.loc, lhsR, rhsI, fmf, fpExcept, roundMode);
      mlir::Value bcMad =
          builder.createFSub(op.loc, bc, ad, fmf, fpExcept, roundMode);
      mlir::Value resR =
          builder.createFDiv(op.loc, acPbd, ccPdd, fmf, fpExcept, roundMode);
      mlir::Value resI =
          builder.createFDiv(op.loc, bcMad, ccPdd, fmf, fpExcept, roundMode);
      return builder.createComplexCreate(op.loc, resR, resI);
    }
    return cir::ComplexDivOp::create(builder, op.loc, op.lhs, op.rhs,
                                     rangeKind, fmf);
  }

  // The C99 standard (G.5.1) defines division of a complex value by a real
  // value in the following simplified form.
  if (mlir::isa<cir::ComplexType>(op.lhs.getType())) {
    assert(mlir::cast<cir::ComplexType>(op.lhs.getType()).getElementType() ==
           op.rhs.getType());
    mlir::Value real = builder.createComplexReal(op.loc, op.lhs);
    mlir::Value imag = builder.createComplexImag(op.loc, op.lhs);
    mlir::Value newReal =
        builder.createFDiv(op.loc, real, op.rhs, fmf, fpExcept, roundMode);
    mlir::Value newImag =
        builder.createFDiv(op.loc, imag, op.rhs, fmf, fpExcept, roundMode);
    return builder.createComplexCreate(op.loc, newReal, newImag);
  }

  assert(mlir::isa<cir::ComplexType>(op.rhs.getType()));
  if (fpExcept) {
    // When strict FP is enabled, decompose scalar/complex division inline
    // rather than going through ComplexDivOp.
    cir::ConstantOp zero = builder.getNullValue(op.lhs.getType(), op.loc);
    mlir::Value lhsR = op.lhs;
    mlir::Value lhsI = zero;
    mlir::Value rhsR = builder.createComplexReal(op.loc, op.rhs);
    mlir::Value rhsI = builder.createComplexImag(op.loc, op.rhs);
    // (a+0i) / (c+di) = ((ac)/(cc+dd)) + i((0c-ad)/(cc+dd))
    //                  = (ac/(cc+dd)) + i(-ad/(cc+dd))
    mlir::Value ac =
        builder.createFMul(op.loc, lhsR, rhsR, fmf, fpExcept, roundMode);
    mlir::Value cc =
        builder.createFMul(op.loc, rhsR, rhsR, fmf, fpExcept, roundMode);
    mlir::Value dd =
        builder.createFMul(op.loc, rhsI, rhsI, fmf, fpExcept, roundMode);
    mlir::Value ccPdd =
        builder.createFAdd(op.loc, cc, dd, fmf, fpExcept, roundMode);
    mlir::Value ad =
        builder.createFMul(op.loc, lhsR, rhsI, fmf, fpExcept, roundMode);
    mlir::Value negAd =
        builder.createUnaryOp(op.loc, cir::UnaryOpKind::Minus, ad);
    mlir::Value resR =
        builder.createFDiv(op.loc, ac, ccPdd, fmf, fpExcept, roundMode);
    mlir::Value resI =
        builder.createFDiv(op.loc, negAd, ccPdd, fmf, fpExcept, roundMode);
    return builder.createComplexCreate(op.loc, resR, resI);
  }
  cir::ConstantOp nullValue = builder.getNullValue(op.lhs.getType(), op.loc);
  mlir::Value lhs = builder.createComplexCreate(op.loc, op.lhs, nullValue);
  return cir::ComplexDivOp::create(builder, op.loc, lhs, op.rhs, rangeKind,
                                   fmf);
}

static void addScalarComplexResultSourceAtom(CIRGenFunction &cgf,
                                             mlir::Value result) {
  if (auto create = result.getDefiningOp<cir::ComplexCreateOp>()) {
    if (mlir::Operation *realOp = create.getReal().getDefiningOp()) {
      cgf.addOpToCurrentSourceAtomWithRank(realOp, 2);
      return;
    }
  }
  if (mlir::Operation *resultOp = result.getDefiningOp())
    cgf.addOpToCurrentSourceAtomWithRank(resultOp, 2);
}

mlir::Value CIRGenFunction::emitUnPromotedValue(mlir::Value result,
                                                QualType unPromotionType) {
  assert(!mlir::cast<cir::ComplexType>(result.getType()).isIntegerComplex() &&
         "integral complex will never be promoted");
  return builder.createCast(cir::CastKind::float_complex, result,
                            convertType(unPromotionType));
}

mlir::Value CIRGenFunction::emitPromotedValue(mlir::Value result,
                                              QualType promotionType) {
  assert(!mlir::cast<cir::ComplexType>(result.getType()).isIntegerComplex() &&
         "integral complex will never be promoted");
  return builder.createCast(cir::CastKind::float_complex, result,
                            convertType(promotionType));
}

mlir::Value ComplexExprEmitter::emitPromoted(const Expr *e,
                                             QualType promotionTy) {
  e = e->IgnoreParens();
  if (const auto *bo = dyn_cast<BinaryOperator>(e)) {
    switch (bo->getOpcode()) {
#define HANDLE_BINOP(OP)                                                       \
  case BO_##OP:                                                                \
    return emitBin##OP(emitBinOps(bo, promotionTy));
      HANDLE_BINOP(Add)
      HANDLE_BINOP(Sub)
      HANDLE_BINOP(Mul)
      HANDLE_BINOP(Div)
#undef HANDLE_BINOP
    default:
      break;
    }
  } else if (const auto *unaryOp = dyn_cast<UnaryOperator>(e)) {
    switch (unaryOp->getOpcode()) {
    case UO_Minus:
    case UO_Plus: {
      auto kind = unaryOp->getOpcode() == UO_Plus ? cir::UnaryOpKind::Plus
                                                  : cir::UnaryOpKind::Minus;
      return VisitPlusMinus(unaryOp, kind, promotionTy);
    }
    default:
      break;
    }
  }

  mlir::Value result = Visit(const_cast<Expr *>(e));
  if (!promotionTy.isNull())
    return cgf.emitPromotedValue(result, promotionTy);

  return result;
}

mlir::Value CIRGenFunction::emitPromotedComplexExpr(const Expr *e,
                                                    QualType promotionType) {
  return ComplexExprEmitter(*this).emitPromoted(e, promotionType);
}

mlir::Value
ComplexExprEmitter::emitPromotedComplexOperand(const Expr *e,
                                               QualType promotionTy) {
  if (e->getType()->isAnyComplexType()) {
    if (!promotionTy.isNull())
      // Keep promotion in the current emitter to avoid extra emitter
      // construction in hot complex-expression paths.
      return emitPromoted(e, promotionTy);
    return Visit(const_cast<Expr *>(e));
  }

  if (!promotionTy.isNull()) {
    QualType complexElementTy =
        promotionTy->castAs<ComplexType>()->getElementType();
    return cgf.emitPromotedScalarExpr(e, complexElementTy);
  }
  return cgf.emitScalarExpr(e);
}

ComplexExprEmitter::BinOpInfo
ComplexExprEmitter::emitBinOps(const BinaryOperator *e, QualType promotionTy) {
  BinOpInfo binOpInfo{cgf.getLoc(e->getExprLoc())};
  binOpInfo.lhs = emitPromotedComplexOperand(e->getLHS(), promotionTy);
  binOpInfo.rhs = emitPromotedComplexOperand(e->getRHS(), promotionTy);
  binOpInfo.ty = promotionTy.isNull() ? e->getType() : promotionTy;
  binOpInfo.fpFeatures = e->getFPFeaturesInEffect(cgf.getLangOpts());
  return binOpInfo;
}

LValue ComplexExprEmitter::emitCompoundAssignLValue(
    const CompoundAssignOperator *e,
    mlir::Value (ComplexExprEmitter::*func)(const BinOpInfo &), RValue &value) {
  QualType lhsTy = e->getLHS()->getType();
  QualType rhsTy = e->getRHS()->getType();
  SourceLocation exprLoc = e->getExprLoc();
  mlir::Location loc = cgf.getLoc(exprLoc);

  if (const AtomicType *at = lhsTy->getAs<AtomicType>())
    lhsTy = at->getValueType();

  BinOpInfo opInfo{loc};
  opInfo.fpFeatures = e->getFPFeaturesInEffect(cgf.getLangOpts());


  // Load the RHS and LHS operands.
  // __block variables need to have the rhs evaluated first, plus this should
  // improve codegen a little.
  const bool isComplexDivisor = e->getOpcode() == BO_DivAssign &&
                                e->getRHS()->getType()->isAnyComplexType();
  QualType promotionTypeCR = getPromotionType(e->getStoredFPFeaturesOrDefault(),
                                              e->getComputationResultType(),
                                              isComplexDivisor);
  opInfo.ty = promotionTypeCR.isNull() ? e->getComputationResultType()
                                       : promotionTypeCR;

  QualType complexElementTy =
      opInfo.ty->castAs<ComplexType>()->getElementType();
  QualType promotionTypeRHS =
      getPromotionType(e->getStoredFPFeaturesOrDefault(), rhsTy,
                       isComplexDivisor);

  // The RHS should have been converted to the computation type.
  if (e->getRHS()->getType()->isRealFloatingType()) {
    if (!promotionTypeRHS.isNull()) {
      opInfo.rhs = cgf.emitPromotedScalarExpr(e->getRHS(), promotionTypeRHS);
    } else {
      assert(cgf.getContext().hasSameUnqualifiedType(complexElementTy, rhsTy));
      opInfo.rhs = cgf.emitScalarExpr(e->getRHS());
    }
  } else {
    if (!promotionTypeRHS.isNull()) {
      // Reuse this emitter for promoted complex RHS to avoid nested emitter
      // creation during compound assignment lowering.
      opInfo.rhs = emitPromoted(e->getRHS(), promotionTypeRHS);
    } else {
      assert(cgf.getContext().hasSameUnqualifiedType(opInfo.ty, rhsTy));
      opInfo.rhs = Visit(e->getRHS());
    }
  }

  LValue lhs = cgf.emitLValue(e->getLHS());

  // Load from the l-value and convert it.
  QualType promotionTypeLHS = getPromotionType(
      e->getStoredFPFeaturesOrDefault(), e->getComputationLHSType(),
      isComplexDivisor);
  if (lhsTy->isAnyComplexType()) {
    mlir::Value lhsValue = emitLoadOfLValue(lhs, exprLoc);
    QualType destTy = promotionTypeLHS.isNull() ? opInfo.ty : promotionTypeLHS;
    opInfo.lhs = emitComplexToComplexCast(lhsValue, lhsTy, destTy, exprLoc);
    attachFPConstraints(opInfo.lhs, opInfo.fpFeatures);
  } else {
    mlir::Value lhsVal = cgf.emitLoadOfLValue(lhs, exprLoc).getValue();
    // For floating point real operands we can directly pass the scalar form
    // to the binary operator emission and potentially get more efficient code.
    if (lhsTy->isRealFloatingType()) {
      QualType promotedComplexElementTy;
      if (!promotionTypeLHS.isNull()) {
        promotedComplexElementTy =
            cast<ComplexType>(promotionTypeLHS)->getElementType();
        if (!cgf.getContext().hasSameUnqualifiedType(promotedComplexElementTy,
                                                     promotionTypeLHS))
          lhsVal = cgf.emitScalarConversion(lhsVal, lhsTy,
                                            promotedComplexElementTy, exprLoc);
      } else {
        if (!cgf.getContext().hasSameUnqualifiedType(complexElementTy, lhsTy))
          lhsVal = cgf.emitScalarConversion(lhsVal, lhsTy, complexElementTy,
                                            exprLoc);
      }
      opInfo.lhs = lhsVal;
    } else {
      opInfo.lhs = emitScalarToComplexCast(lhsVal, lhsTy, opInfo.ty, exprLoc);
    }
  }

  // Expand the binary operator.
  mlir::Value result = (this->*func)(opInfo);

  // Truncate the result and store it into the LHS lvalue.
  CIRGenFunction::SourceLocRAIIObject sourceLoc{
      cgf, cgf.getLoc(e->getSourceRange())};
  if (lhsTy->isAnyComplexType()) {
    mlir::Value resultValue =
        emitComplexToComplexCast(result, opInfo.ty, lhsTy, exprLoc);
    attachFPConstraints(resultValue, opInfo.fpFeatures);
    emitStoreOfComplex(loc, resultValue, lhs, /*isInit*/ false);
    value = RValue::getComplex(resultValue);
  } else {
    addScalarComplexResultSourceAtom(cgf, result);
    mlir::Value resultValue =
        cgf.emitComplexToScalarConversion(result, opInfo.ty, lhsTy, exprLoc);
    cgf.emitStoreThroughLValue(RValue::get(resultValue), lhs, /*isInit*/ false);
    value = RValue::get(resultValue);
  }

  return lhs;
}

mlir::Value ComplexExprEmitter::emitCompoundAssign(
    const CompoundAssignOperator *e,
    mlir::Value (ComplexExprEmitter::*func)(const BinOpInfo &)) {
  CIRGenFunction::ApplySourceAtomGroup atomGroup(cgf);
  RValue val;
  LValue lv = emitCompoundAssignLValue(e, func, val);

  // The result of an assignment in C is the assigned r-value.
  if (!cgf.getLangOpts().CPlusPlus)
    return val.getComplexValue();

  // If the lvalue is non-volatile, return the computed value of the assignment.
  if (!lv.isVolatileQualified())
    return val.getComplexValue();

  return emitLoadOfLValue(lv, e->getExprLoc());
}

LValue ComplexExprEmitter::emitBinAssignLValue(const BinaryOperator *e,
                                               mlir::Value &value) {
  assert(cgf.getContext().hasSameUnqualifiedType(e->getLHS()->getType(),
                                                 e->getRHS()->getType()) &&
         "Invalid assignment");

  // Emit the RHS.  __block variables need the RHS evaluated first.
  value = Visit(e->getRHS());

  // Compute the address to store into.
  LValue lhs = cgf.emitLValue(e->getLHS());

  // Store the result value into the LHS lvalue.
  emitStoreOfComplex(cgf.getLoc(e->getExprLoc()), value, lhs,
                     /*isInit*/ false);
  return lhs;
}

mlir::Value ComplexExprEmitter::VisitBinAssign(const BinaryOperator *e) {
  CIRGenFunction::ApplySourceAtomGroup atomGroup(cgf);
  mlir::Value value;
  LValue lv = emitBinAssignLValue(e, value);

  // The result of an assignment in C is the assigned r-value.
  if (!cgf.getLangOpts().CPlusPlus)
    return value;

  // If the lvalue is non-volatile, return the computed value of the
  // assignment.
  if (!lv.isVolatile())
    return value;

  return emitLoadOfLValue(lv, e->getExprLoc());
}

mlir::Value ComplexExprEmitter::VisitBinComma(const BinaryOperator *e) {
  cgf.emitIgnoredExpr(e->getLHS());
  return Visit(e->getRHS());
}

mlir::Value ComplexExprEmitter::VisitAbstractConditionalOperator(
    const AbstractConditionalOperator *e) {
  mlir::Location loc = cgf.getLoc(e->getSourceRange());

  // Bind the common expression if necessary.
  CIRGenFunction::OpaqueValueMapping binding(cgf, e);

  CIRGenFunction::ConditionalEvaluation eval(cgf);

  // OGCG integrates with PGO profiling via EmitBranchOnBoolExpr and
  // calls incrementProfileCounter for coverage.

  Expr *cond = e->getCond()->IgnoreParens();
  mlir::Value condValue = cgf.evaluateExprAsBool(cond);

  return cir::TernaryOp::create(
             builder, loc, condValue,
             /*thenBuilder=*/
             [&](mlir::OpBuilder &b, mlir::Location loc) {
               eval.beginEvaluation();
               mlir::Value trueValue = Visit(e->getTrueExpr());
               cir::YieldOp::create(b, loc, trueValue);
               eval.endEvaluation();
             },
             /*elseBuilder=*/
             [&](mlir::OpBuilder &b, mlir::Location loc) {
               eval.beginEvaluation();
               mlir::Value falseValue = Visit(e->getFalseExpr());
               cir::YieldOp::create(b, loc, falseValue);
               eval.endEvaluation();
             })
      .getResult();
}

mlir::Value ComplexExprEmitter::VisitChooseExpr(ChooseExpr *e) {
  return Visit(e->getChosenSubExpr());
}

mlir::Value ComplexExprEmitter::VisitInitListExpr(InitListExpr *e) {
  mlir::Location loc = cgf.getLoc(e->getExprLoc());
  if (e->getNumInits() == 2) {
    mlir::Value real = cgf.emitScalarExpr(e->getInit(0));
    mlir::Value imag = cgf.emitScalarExpr(e->getInit(1));
    return builder.createComplexCreate(loc, real, imag);
  }

  if (e->getNumInits() == 1)
    return Visit(e->getInit(0));

  assert(e->getNumInits() == 0 && "Unexpected number of inits");
  mlir::Type complexTy = cgf.convertType(e->getType());
  return builder.getNullValue(complexTy, loc);
}

mlir::Value ComplexExprEmitter::VisitVAArgExpr(VAArgExpr *e) {
  return cgf.emitVAArg(e);
}

//===----------------------------------------------------------------------===//
//                         Entry Point into this File
//===----------------------------------------------------------------------===//

/// EmitComplexExpr - Emit the computation of the specified expression of
/// complex type, ignoring the result.
mlir::Value CIRGenFunction::emitComplexExpr(const Expr *e) {
  assert(e && getComplexType(e->getType()) &&
         "Invalid complex expression to emit");

  // OGCG accepts IgnoreReal/IgnoreImag parameters to skip computing the
  // unused component (e.g., for __real__/__imag__ unary ops). CIR preserves
  // the full complex expression and defers dead code elimination to MLIR/LLVM
  // passes, which remove unused extractions. Intentional divergence.
  return ComplexExprEmitter(*this).Visit(const_cast<Expr *>(e));
}

void CIRGenFunction::emitComplexExprIntoLValue(const Expr *e, LValue dest,
                                               bool isInit) {
  assert(e && getComplexType(e->getType()) &&
         "Invalid complex expression to emit");
  ComplexExprEmitter emitter(*this);
  mlir::Value value = emitter.Visit(const_cast<Expr *>(e));
  emitter.emitStoreOfComplex(getLoc(e->getExprLoc()), value, dest, isInit);
}

/// EmitStoreOfComplex - Store a complex number into the specified l-value.
void CIRGenFunction::emitStoreOfComplex(mlir::Location loc, mlir::Value v,
                                        LValue dest, bool isInit) {
  ComplexExprEmitter(*this).emitStoreOfComplex(loc, v, dest, isInit);
}

mlir::Value CIRGenFunction::emitLoadOfComplex(LValue src, SourceLocation loc) {
  return ComplexExprEmitter(*this).emitLoadOfLValue(src, loc);
}

LValue CIRGenFunction::emitComplexAssignmentLValue(const BinaryOperator *e) {
  assert(e->getOpcode() == BO_Assign && "Expected assign op");

  ApplySourceAtomGroup atomGroup(*this);
  mlir::Value value; // ignored
  LValue lvalue = ComplexExprEmitter(*this).emitBinAssignLValue(e, value);
  if (getLangOpts().OpenMP)
    checkAndEmitOpenMPLastprivateConditional(e->getLHS());

  return lvalue;
}

using CompoundFunc =
    mlir::Value (ComplexExprEmitter::*)(const ComplexExprEmitter::BinOpInfo &);

static CompoundFunc getComplexOp(BinaryOperatorKind op) {
  switch (op) {
  case BO_MulAssign:
    return &ComplexExprEmitter::emitBinMul;
  case BO_DivAssign:
    return &ComplexExprEmitter::emitBinDiv;
  case BO_SubAssign:
    return &ComplexExprEmitter::emitBinSub;
  case BO_AddAssign:
    return &ComplexExprEmitter::emitBinAdd;
  default:
    llvm_unreachable("unexpected complex compound assignment");
  }
}

LValue CIRGenFunction::emitComplexCompoundAssignmentLValue(
    const CompoundAssignOperator *e) {
  ApplySourceAtomGroup atomGroup(*this);
  CompoundFunc op = getComplexOp(e->getOpcode());
  RValue val;
  LValue lvalue = ComplexExprEmitter(*this).emitCompoundAssignLValue(e, op, val);
  if (getLangOpts().OpenMP)
    checkAndEmitOpenMPLastprivateConditional(e->getLHS());
  return lvalue;
}

mlir::Value CIRGenFunction::emitComplexPrePostIncDec(const UnaryOperator *e,
                                                     LValue lv,
                                                     cir::UnaryOpKind op,
                                                     bool isPre) {
  assert((op == cir::UnaryOpKind::Inc ||
          op == cir::UnaryOpKind::Dec) &&
         "Invalid UnaryOp kind for ComplexType");

  mlir::Value inVal = emitLoadOfComplex(lv, e->getExprLoc());
  mlir::Location loc = getLoc(e->getExprLoc());
  mlir::Value incVal = builder.createUnaryOp(loc, op, inVal);

  // Store the updated result through the lvalue.
  emitStoreOfComplex(loc, incVal, lv, /*isInit=*/false);

  if (getLangOpts().OpenMP)
    checkAndEmitOpenMPLastprivateConditional(e->getSubExpr());

  // If this is a postinc, return the value read from memory, otherwise use the
  // updated value.
  return isPre ? incVal : inVal;
}

LValue CIRGenFunction::emitScalarCompoundAssignWithComplex(
    const CompoundAssignOperator *e, mlir::Value &result) {
  // Key Instructions: Don't need to create an atom group here; one will already
  // be active through scalar handling code.
  CompoundFunc op = getComplexOp(e->getOpcode());
  RValue value;
  LValue ret = ComplexExprEmitter(*this).emitCompoundAssignLValue(e, op, value);
  result = value.getValue();
  if (getLangOpts().OpenMP)
    checkAndEmitOpenMPLastprivateConditional(e->getLHS());
  return ret;
}
