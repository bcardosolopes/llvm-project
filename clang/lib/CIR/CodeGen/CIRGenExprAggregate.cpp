//===- CIRGenExprAggregrate.cpp - Emit CIR Code from Aggregate Expressions ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Aggregate Expr nodes as CIR code.
//
//===----------------------------------------------------------------------===//

#include "CIRGenBuilder.h"
#include "CIRGenCleanup.h"
#include "CIRGenConstantEmitter.h"
#include "CIRGenFunction.h"
#include "CIRGenRecordLayout.h"
#include "CIRGenValue.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "mlir/IR/Builders.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"

#include "clang/AST/ComparisonCategories.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtVisitor.h"
#include <cstdint>
#include <functional>

#include "llvm/Support/CommandLine.h"

using namespace clang;
using namespace clang::CIRGen;

/// Look for a cast expression that is a no-op inverse of the given cast kind.
/// For example, if we're doing NonAtomicToAtomic, look for an
/// AtomicToNonAtomic subexpression to cancel out.
static Expr *findPeephole(Expr *op, CastKind kind, const ASTContext &ctx) {
  op = op->IgnoreParenNoopCasts(ctx);
  if (auto *castE = dyn_cast<CastExpr>(op)) {
    if (castE->getCastKind() == kind)
      return castE->getSubExpr();
  }
  return nullptr;
}

static bool requiresGCMemmoveCollectable(const ASTContext &astContext,
                                         QualType ty) {
  if (const auto *record = ty->getAsRecordDecl())
    return record->hasObjectMember();

  if (!ty->isArrayType())
    return false;

  QualType baseType = astContext.getBaseElementType(ty);
  if (const auto *record = baseType->getAsRecordDecl())
    return record->hasObjectMember();
  return false;
}

namespace {
/// Cleanup that emits a cir.lifetime.end marker for aggregate temporaries.
struct CallLifetimeEnd final : EHScopeStack::Cleanup {
  Address addr;
  CallLifetimeEnd(Address addr) : addr(addr) {}
  void emit(CIRGenFunction &cgf, Flags flags) override {
    auto &builder = cgf.getBuilder();
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *block = builder.getInsertionBlock();
    if (block && block->mightHaveTerminator() && block->getTerminator())
      builder.setInsertionPoint(block->getTerminator());
    builder.createLifetimeEnd(addr.getPointer().getLoc(),
                              addr.getPointer());
  }
};

// FIXME(cir): This should be a common helper between CIRGen
// and traditional CodeGen
/// Is the value of the given expression possibly a reference to or
/// into a __block variable?
static bool isBlockVarRef(const Expr *e) {
  // Make sure we look through parens.
  e = e->IgnoreParens();

  // Check for a direct reference to a __block variable.
  if (const DeclRefExpr *dre = dyn_cast<DeclRefExpr>(e)) {
    const VarDecl *var = dyn_cast<VarDecl>(dre->getDecl());
    return (var && var->hasAttr<BlocksAttr>());
  }

  // More complicated stuff.

  // Binary operators.
  if (const BinaryOperator *op = dyn_cast<BinaryOperator>(e)) {
    // For an assignment or pointer-to-member operation, just care
    // about the LHS.
    if (op->isAssignmentOp() || op->isPtrMemOp())
      return isBlockVarRef(op->getLHS());

    // For a comma, just care about the RHS.
    if (op->getOpcode() == BO_Comma)
      return isBlockVarRef(op->getRHS());

    // FIXME: pointer arithmetic?
    return false;

    // Check both sides of a conditional operator.
  } else if (const AbstractConditionalOperator *op =
                 dyn_cast<AbstractConditionalOperator>(e)) {
    return isBlockVarRef(op->getTrueExpr()) ||
           isBlockVarRef(op->getFalseExpr());

    // OVEs are required to support BinaryConditionalOperators.
  } else if (const OpaqueValueExpr *op = dyn_cast<OpaqueValueExpr>(e)) {
    if (const Expr *src = op->getSourceExpr())
      return isBlockVarRef(src);

    // Casts are necessary to get things like (*(int*)&var) = foo().
    // We don't really care about the kind of cast here, except
    // we don't want to look through l2r casts, because it's okay
    // to get the *value* in a __block variable.
  } else if (const CastExpr *cast = dyn_cast<CastExpr>(e)) {
    if (cast->getCastKind() == CK_LValueToRValue)
      return false;
    return isBlockVarRef(cast->getSubExpr());

    // Handle unary operators.  Again, just aggressively look through
    // it, ignoring the operation.
  } else if (const UnaryOperator *uop = dyn_cast<UnaryOperator>(e)) {
    return isBlockVarRef(uop->getSubExpr());

    // Look into the base of a field access.
  } else if (const MemberExpr *mem = dyn_cast<MemberExpr>(e)) {
    return isBlockVarRef(mem->getBase());

    // Look into the base of a subscript.
  } else if (const ArraySubscriptExpr *sub = dyn_cast<ArraySubscriptExpr>(e)) {
    return isBlockVarRef(sub->getBase());
  }

  return false;
}

/// Determine whether the given cast kind is known to always convert values
/// with all zero bits in their value representation to values with all zero
/// bits in their value representation.
static bool castPreservesZero(const CastExpr *ce) {
  switch (ce->getCastKind()) {
    // No-ops.
  case CK_NoOp:
  case CK_UserDefinedConversion:
  case CK_ConstructorConversion:
  case CK_BitCast:
  case CK_ToUnion:
  case CK_ToVoid:
    // Conversions between (possibly-complex) integral, (possibly-complex)
    // floating-point, and bool.
  case CK_BooleanToSignedIntegral:
  case CK_FloatingCast:
  case CK_FloatingComplexCast:
  case CK_FloatingComplexToBoolean:
  case CK_FloatingComplexToIntegralComplex:
  case CK_FloatingComplexToReal:
  case CK_FloatingRealToComplex:
  case CK_FloatingToBoolean:
  case CK_FloatingToIntegral:
  case CK_IntegralCast:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToBoolean:
  case CK_IntegralComplexToFloatingComplex:
  case CK_IntegralComplexToReal:
  case CK_IntegralRealToComplex:
  case CK_IntegralToBoolean:
  case CK_IntegralToFloating:
    // Reinterpreting integers as pointers and vice versa.
  case CK_IntegralToPointer:
  case CK_PointerToIntegral:
    // Language extensions.
  case CK_VectorSplat:
  case CK_MatrixCast:
  case CK_NonAtomicToAtomic:
  case CK_AtomicToNonAtomic:
  case CK_HLSLVectorTruncation:
  case CK_HLSLMatrixTruncation:
  case CK_HLSLElementwiseCast:
  case CK_HLSLAggregateSplatCast:
    return true;

  case CK_BaseToDerivedMemberPointer:
  case CK_DerivedToBaseMemberPointer:
  case CK_MemberPointerToBoolean:
  case CK_NullToMemberPointer:
  case CK_ReinterpretMemberPointer:
    // FIXME: ABI-dependent.
    return false;

  case CK_AnyPointerToBlockPointerCast:
  case CK_BlockPointerToObjCPointerCast:
  case CK_CPointerToObjCPointerCast:
  case CK_ObjCObjectLValueCast:
  case CK_IntToOCLSampler:
  case CK_ZeroToOCLOpaqueType:
    // FIXME: Check these.
    return false;

  case CK_FixedPointCast:
  case CK_FixedPointToBoolean:
  case CK_FixedPointToFloating:
  case CK_FixedPointToIntegral:
  case CK_FloatingToFixedPoint:
  case CK_IntegralToFixedPoint:
    // FIXME: Do all fixed-point types represent zero as all 0 bits?
    return false;

  case CK_AddressSpaceConversion:
  case CK_BaseToDerived:
  case CK_DerivedToBase:
  case CK_Dynamic:
  case CK_NullToPointer:
  case CK_PointerToBoolean:
    // FIXME: Preserves zeroes only if zero pointers and null pointers have the
    // same representation in all involved address spaces.
    return false;

  case CK_ARCConsumeObject:
  case CK_ARCExtendBlockObject:
  case CK_ARCProduceObject:
  case CK_ARCReclaimReturnedObject:
  case CK_CopyAndAutoreleaseBlockObject:
  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay:
  case CK_BuiltinFnToFnPtr:
  case CK_Dependent:
  case CK_LValueBitCast:
  case CK_LValueToRValue:
  case CK_LValueToRValueBitCast:
  case CK_UncheckedDerivedToBase:
  case CK_HLSLArrayRValue:
    return false;
  }
  llvm_unreachable("Unhandled clang::CastKind enum");
}

/// If emitting this expression will obviously only store zero to memory,
/// return true. This can conservatively return false for hard cases.
static bool isSimpleZero(const Expr *e, CIRGenFunction &cgf) {
  e = e->IgnoreParens();
  while (const auto *ce = dyn_cast<CastExpr>(e)) {
    if (!castPreservesZero(ce))
      break;
    e = ce->getSubExpr()->IgnoreParens();
  }

  // 0
  if (const auto *il = dyn_cast<IntegerLiteral>(e))
    return il->getValue() == 0;
  // +0.0
  if (const auto *fl = dyn_cast<FloatingLiteral>(e))
    return fl->getValue().isPosZero();
  // int()
  if ((isa<ImplicitValueInitExpr>(e) || isa<CXXScalarValueInitExpr>(e)) &&
      cgf.getTypes().isZeroInitializable(e->getType()))
    return true;
  // (int*)0 - Null pointer expressions.
  if (const auto *ice = dyn_cast<CastExpr>(e))
    return ice->getCastKind() == CK_NullToPointer &&
           cgf.getTypes().isZeroInitializable(e->getType()) &&
           !e->HasSideEffects(cgf.getContext());
  // '\0'
  if (const auto *cl = dyn_cast<CharacterLiteral>(e))
    return cl->getValue() == 0;

  // Otherwise, hard case: conservatively return false.
  return false;
}

/// Get an approximate count of the number of non-zero bytes in an
/// initializer. Conservatively returns the full type size for expressions
/// that don't have a simple mostly-zero InitListExpr shape.
static CharUnits getNumNonZeroBytesInInit(const Expr *e, CIRGenFunction &cgf) {
  if (auto *mte = dyn_cast<MaterializeTemporaryExpr>(e))
    e = mte->getSubExpr();
  e = e->IgnoreParenNoopCasts(cgf.getContext());

  if (isSimpleZero(e, cgf))
    return CharUnits::Zero();

  const InitListExpr *ile = dyn_cast<InitListExpr>(e);
  while (ile && ile->isTransparent())
    ile = dyn_cast<InitListExpr>(ile->getInit(0));
  if (!ile || !cgf.getTypes().isZeroInitializable(ile->getType()))
    return cgf.getContext().getTypeSizeInChars(e->getType());

  if (const auto *recordTy = e->getType()->getAsCanonical<RecordType>()) {
    if (!recordTy->isUnionType()) {
      RecordDecl *record = recordTy->getDecl()->getDefinitionOrSelf();
      CharUnits numNonZeroBytes = CharUnits::Zero();

      unsigned ileElement = 0;
      if (auto *cxxRecord = dyn_cast<CXXRecordDecl>(record))
        while (ileElement != cxxRecord->getNumBases())
          numNonZeroBytes +=
              getNumNonZeroBytesInInit(ile->getInit(ileElement++), cgf);

      for (const auto *field : record->fields()) {
        if (field->getType()->isIncompleteArrayType() ||
            ileElement == ile->getNumInits())
          break;
        if (field->isUnnamedBitField())
          continue;

        const Expr *fieldInit = ile->getInit(ileElement++);
        if (field->getType()->isReferenceType()) {
          numNonZeroBytes += cgf.getContext().toCharUnitsFromBits(
              cgf.getTarget().getPointerWidth(LangAS::Default));
        } else {
          numNonZeroBytes += getNumNonZeroBytesInInit(fieldInit, cgf);
        }
      }

      return numNonZeroBytes;
    }
  }

  CharUnits numNonZeroBytes = CharUnits::Zero();
  for (unsigned i = 0, e = ile->getNumInits(); i != e; ++i)
    numNonZeroBytes += getNumNonZeroBytesInInit(ile->getInit(i), cgf);
  return numNonZeroBytes;
}

static bool isAArch64HomogeneousAggregateBaseType(ASTContext &ctx,
                                                  QualType type) {
  if (const auto *builtinType = type->getAs<BuiltinType>())
    return builtinType->isFloatingPoint();

  const auto *vectorType = type->getAs<VectorType>();
  if (!vectorType)
    return false;

  if (auto kind = vectorType->getVectorKind();
      kind == VectorKind::SveFixedLengthData ||
      kind == VectorKind::SveFixedLengthPredicate)
    return false;

  uint64_t vectorSize = ctx.getTypeSize(type);
  return vectorSize == 64 || vectorSize == 128;
}

static bool isAArch64HomogeneousAggregate(ASTContext &ctx, QualType type,
                                          QualType &base, uint64_t &members) {
  if (const auto *arrayType = ctx.getAsConstantArrayType(type)) {
    uint64_t elementCount = arrayType->getZExtSize();
    if (elementCount == 0)
      return false;
    if (!isAArch64HomogeneousAggregate(ctx, arrayType->getElementType(), base,
                                       members))
      return false;
    members *= elementCount;
  } else if (const auto *recordDecl = type->getAsRecordDecl()) {
    if (recordDecl->hasFlexibleArrayMember())
      return false;

    members = 0;

    if (const auto *cxxRecord = dyn_cast<CXXRecordDecl>(recordDecl)) {
      for (const auto &baseSpec : cxxRecord->bases()) {
        if (isEmptyRecordForLayout(ctx, baseSpec.getType()))
          continue;

        uint64_t baseMembers = 0;
        if (!isAArch64HomogeneousAggregate(ctx, baseSpec.getType(), base,
                                           baseMembers))
          return false;
        members += baseMembers;
      }
    }

    for (const auto *field : recordDecl->fields()) {
      QualType fieldType = field->getType();
      while (const auto *arrayType = ctx.getAsConstantArrayType(fieldType)) {
        if (arrayType->isZeroSize())
          return false;
        fieldType = arrayType->getElementType();
      }

      if (isEmptyRecordForLayout(ctx, fieldType) ||
          field->isZeroLengthBitField())
        continue;

      uint64_t fieldMembers = 0;
      if (!isAArch64HomogeneousAggregate(ctx, field->getType(), base,
                                         fieldMembers))
        return false;
      if (recordDecl->isUnion()) {
        if (members < fieldMembers)
          members = fieldMembers;
      } else {
        members += fieldMembers;
      }
    }

    if (base.isNull())
      return false;

    if (ctx.getTypeSize(base) * members != ctx.getTypeSize(type))
      return false;
  } else {
    members = 1;
    if (const auto *complexType = type->getAs<ComplexType>()) {
      members = 2;
      type = complexType->getElementType();
    }

    if (!isAArch64HomogeneousAggregateBaseType(ctx, type))
      return false;

    if (base.isNull()) {
      base = type;
      if (const auto *vectorType = type->getAs<VectorType>()) {
        QualType elementType = vectorType->getElementType();
        unsigned numElements =
            ctx.getTypeSize(type) / ctx.getTypeSize(elementType);
        base = ctx.getVectorType(elementType, numElements,
                                 vectorType->getVectorKind());
      }
    }

    if (base->isVectorType() != type->isVectorType() ||
        ctx.getTypeSize(base) != ctx.getTypeSize(type))
      return false;
  }

  return members > 0 && members <= 4;
}

/// Match OGCG's CheckAggExprForMemSetUse: for large mostly-zero aggregate
/// initlists, pre-zero the destination so the normal emitter only has to write
/// the non-zero pieces.
static void checkAggExprForMemSetUse(AggValueSlot &slot, const Expr *e,
                                     CIRGenFunction &cgf) {
  if (cgf.shouldSuppressAggExprMemSetUse() || slot.isZeroed() ||
      slot.isVolatile() || !slot.getAddress().isValid())
    return;

  if (cgf.getLangOpts().CPlusPlus)
    if (const auto *recordTy =
            cgf.getContext().getBaseElementType(e->getType())
                ->getAsCanonical<RecordType>()) {
      const auto *record = cast<CXXRecordDecl>(recordTy->getDecl());
      if (record->hasUserDeclaredConstructor())
        return;
    }

  CharUnits size = slot.getPreferredSize(cgf.getContext(), e->getType());
  if (size <= CharUnits::fromQuantity(16))
    return;

  CharUnits numNonZeroBytes = getNumNonZeroBytesInInit(e, cgf);
  if (numNonZeroBytes * 4 > size)
    return;

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = e->getSourceRange().isValid()
                           ? cgf.getLoc(e->getSourceRange())
                           : slot.getAddress().getPointer().getLoc();
  auto dstPtrTy =
      mlir::cast<cir::PointerType>(slot.getAddress().getPointer().getType());
  auto voidPtrTy = cir::PointerType::get(
      builder.getContext(), cir::VoidType::get(builder.getContext()),
      dstPtrTy.getAddrSpace());
  mlir::Value voidPtr =
      builder.createBitcast(slot.getAddress().getPointer(), voidPtrTy);
  mlir::Value zeroVal = builder.getConstInt(
      loc, cir::IntType::get(builder.getContext(), 32, true), 0);
  auto sizeTy = mlir::cast<cir::IntType>(
      cgf.convertType(cgf.getContext().getSizeType()));
  mlir::Value sizeVal = builder.getConstInt(loc, sizeTy, size.getQuantity());
  auto memsetOp = cir::MemSetOp::create(builder, loc, voidPtr, zeroVal, sizeVal);
  cgf.addOpToCurrentSourceAtom(memsetOp);
  if (!slot.getAddress().getAlignment().isZero())
    memsetOp->setAttr("cir.dst_align", cgf.getI64AttrCached(
                                           slot.getAddress()
                                               .getAlignment()
                                               .getAsAlign()
                                               .value()));
  slot.setZeroed();
}

class AggExprEmitter : public StmtVisitor<AggExprEmitter> {

  CIRGenFunction &cgf;
  AggValueSlot dest;

  /// Zero-initialize padding bytes between PaddingStart and PaddingEnd.
  /// For bitfield NextField, zeros the storage region instead.
  /// PaddingStart is updated to account for the NextField's size.
  void doZeroInitPadding(uint64_t &paddingStart, uint64_t paddingEnd,
                         const FieldDecl *nextField);

  // Calls `fn` with a valid return value slot, potentially creating a temporary
  // to do so. If a temporary is created, an appropriate copy into `Dest` will
  // be emitted, as will lifetime markers.
  //
  // The given function should take a ReturnValueSlot, and return an RValue that
  // points to said slot.
  void withReturnValueSlot(const Expr *e,
                           llvm::function_ref<RValue(ReturnValueSlot)> fn);

  AggValueSlot ensureSlot(mlir::Location loc, QualType t) {
    if (!dest.isIgnored())
      return dest;
    return cgf.createAggTemp(t, loc, "agg.tmp.ensured");
  }

  void ensureDest(mlir::Location loc, QualType ty) {
    if (!dest.isIgnored())
      return;
    dest = cgf.createAggTemp(ty, loc, "agg.tmp.ensured");
  }

public:
  AggExprEmitter(CIRGenFunction &cgf, AggValueSlot dest)
      : cgf(cgf), dest(dest) {}

  /// Given an expression with aggregate type that represents a value lvalue,
  /// this method emits the address of the lvalue, then loads the result into
  /// DestPtr.
  void emitAggLoadOfLValue(const Expr *e);

  void emitArrayInit(Address destPtr, cir::ArrayType arrayTy, QualType arrayQTy,
                     Expr *exprToVisit, ArrayRef<Expr *> args,
                     Expr *arrayFiller);

  void emitFinalDestCopy(QualType type, RValue src);

  /// Perform the final copy to DestPtr, if desired.
  void emitFinalDestCopy(QualType type, const LValue &src,
                         CIRGenFunction::ExprValueKind srcValueKind =
                             CIRGenFunction::EVK_NonRValue);

  void emitCopy(QualType type, const AggValueSlot &dest,
                const AggValueSlot &src);

  void emitInitializationToLValue(Expr *e, LValue lv);

  void emitNullInitializationToLValue(mlir::Location loc, LValue lv);

  void Visit(Expr *e) { StmtVisitor<AggExprEmitter>::Visit(e); }

  void VisitArraySubscriptExpr(ArraySubscriptExpr *e) {
    emitAggLoadOfLValue(e);
  }

  void VisitCallExpr(const CallExpr *e);
  void VisitStmtExpr(const StmtExpr *e) {
    CIRGenFunction::StmtExprEvaluation eval(cgf);
    Address retAlloca =
        cgf.createMemTemp(e->getType(), cgf.getLoc(e->getSourceRange()));
    (void)cgf.emitCompoundStmt(*e->getSubStmt(), &retAlloca, dest);
  }

  void VisitBinAssign(const BinaryOperator *e) {
    // For an assignment to work, the value on the right has
    // to be compatible with the value on the left.
    assert(cgf.getContext().hasSameUnqualifiedType(e->getLHS()->getType(),
                                                   e->getRHS()->getType()) &&
           "Invalid assignment");

    // If the LHS might be a __block variable and the RHS can trigger a block
    // copy, evaluate the RHS first so we reload the forwarded LHS address
    // after any side effects, matching OGCG's CGExprAgg.cpp.
    if (isBlockVarRef(e->getLHS()) &&
        e->getRHS()->HasSideEffects(cgf.getContext())) {
      ensureDest(cgf.getLoc(e->getSourceRange()), e->getRHS()->getType());
      Visit(const_cast<Expr *>(e->getRHS()));

      // OGCG uses EmitCheckedLValue(..., TCK_Store) here. CIR has no
      // aggregate-specific checked-LValue wrapper yet, so reuse the plain
      // lvalue path.
      LValue lhs = cgf.emitLValue(e->getLHS());

      if (lhs.getType()->isAtomicType() ||
          cgf.isLValueSuitableForInlineAtomic(lhs)) {
        cgf.emitAtomicStore(dest.asRValue(), lhs, /*isInit=*/false);
        return;
      }

      emitCopy(e->getLHS()->getType(),
               AggValueSlot::forLValue(lhs, AggValueSlot::IsDestructed,
                                       AggValueSlot::IsAliased,
                                       AggValueSlot::MayOverlap),
               dest);
      return;
    }

    LValue lhs = cgf.emitLValue(e->getLHS());

    // If we have an atomic type, evaluate into the destination and then
    // do an atomic copy. Also check for MSVC volatile semantics.
    if (lhs.getType()->isAtomicType() ||
        cgf.isLValueSuitableForInlineAtomic(lhs)) {
      ensureDest(cgf.getLoc(e->getSourceRange()), e->getRHS()->getType());
      cgf.emitAggExpr(e->getRHS(), dest);
      cgf.emitAtomicStore(dest.asRValue(), lhs, /*isInit=*/false);
      return;
    }

    // Codegen the RHS so that it stores directly into the LHS.
    AggValueSlot lhsSlot = AggValueSlot::forLValue(
        lhs, AggValueSlot::IsDestructed, AggValueSlot::IsAliased,
        AggValueSlot::MayOverlap);

    // A non-volatile aggregate destination might have volatile member.
    if (!lhsSlot.isVolatile() && cgf.hasVolatileMember(e->getLHS()->getType()))
      lhsSlot.setVolatile(true);

    cgf.emitAggExpr(e->getRHS(), lhsSlot);

    // Copy into the destination if the assignment isn't ignored.
    emitFinalDestCopy(e->getType(), lhs);

    if (!dest.isIgnored() && !dest.isExternallyDestructed() &&
        e->getType().isDestructedType() == QualType::DK_nontrivial_c_struct)
      cgf.pushDestroy(QualType::DK_nontrivial_c_struct, dest.getAddress(),
                      e->getType());
  }

  void VisitDeclRefExpr(DeclRefExpr *e) { emitAggLoadOfLValue(e); }

  void VisitInitListExpr(InitListExpr *e);
  void VisitCXXConstructExpr(const CXXConstructExpr *e);

  void visitCXXParenListOrInitListExpr(Expr *e, ArrayRef<Expr *> args,
                                       FieldDecl *initializedFieldInUnion,
                                       Expr *arrayFiller);
  void VisitCXXDefaultInitExpr(CXXDefaultInitExpr *die) {
    CIRGenFunction::CXXDefaultInitExprScope Scope(cgf, die);
    Visit(die->getExpr());
  }
  void VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *e) {
    // Ensure that we have a slot, but if we already do, remember
    // whether it was externally destructed.
    bool wasExternallyDestructed = dest.isExternallyDestructed();
    ensureDest(cgf.getLoc(e->getSourceRange()), e->getType());

    // We're going to push a destructor if there isn't already one.
    dest.setExternallyDestructed();

    Visit(e->getSubExpr());

    // Push that destructor we promised.
    if (!wasExternallyDestructed)
      cgf.emitCXXTemporary(e->getTemporary(), e->getType(), dest.getAddress());
  }
  void VisitLambdaExpr(LambdaExpr *e);
  void VisitExprWithCleanups(ExprWithCleanups *e);

  // Stubs -- These should be moved up when they are implemented.
  void VisitCastExpr(CastExpr *e) {
    if (const auto *ece = dyn_cast<ExplicitCastExpr>(e))
      cgf.cgm.emitExplicitCastExprType(ece, &cgf);

    switch (e->getCastKind()) {
    case CK_Dynamic: {
      // FIXME: Can this actually happen? We have no test coverage for it.
      assert(isa<CXXDynamicCastExpr>(e) && "CK_Dynamic without a dynamic_cast?");
      (void)0; // Sanitizer-specific CIR parity work remains NYI in this path.
      LValue lv = cgf.emitLValue(e->getSubExpr());
      cgf.emitDynamicCast(lv.getAddress(), cast<CXXDynamicCastExpr>(e));

      if (!dest.isIgnored()) {
        cgf.cgm.errorNYI(e->getExprLoc(),
                         "lvalue dynamic_cast with a destination");
        return;
      }
      break;
    }

    case CK_LValueToRValue:
      // If we're loading from a volatile type, force the destination
      // into existence.
      if (e->getSubExpr()->getType().isVolatileQualified()) {
        bool destruct =
            !dest.isExternallyDestructed() &&
            e->getType().isDestructedType() == QualType::DK_nontrivial_c_struct;
        if (destruct)
          dest.setExternallyDestructed();
        ensureDest(cgf.getLoc(e->getSourceRange()), e->getType());
        Visit(e->getSubExpr());
        if (destruct)
          cgf.pushDestroy(QualType::DK_nontrivial_c_struct, dest.getAddress(),
                          e->getType());
        return;
      }
      [[fallthrough]];
    case CK_NoOp:
    case CK_UserDefinedConversion:
    case CK_ConstructorConversion:
      assert(cgf.getContext().hasSameUnqualifiedType(e->getSubExpr()->getType(),
                                                     e->getType()) &&
             "Implicit cast types must be compatible");
      Visit(e->getSubExpr());
      break;
    case CK_ToUnion: {
      // Evaluate even if the destination is ignored.
      if (dest.isIgnored()) {
        cgf.emitIgnoredExpr(e->getSubExpr());
        break;
      }

      // GCC union extension
      QualType ty = e->getSubExpr()->getType();
      Address castPtr = dest.getAddress().withElementType(cgf.getBuilder(),
                                                          cgf.convertType(ty));
      emitInitializationToLValue(e->getSubExpr(),
                                 cgf.makeAddrLValue(castPtr, ty));
      break;
    }
    case CK_LValueToRValueBitCast: {
      if (dest.isIgnored()) {
        cgf.emitAnyExpr(e->getSubExpr(), AggValueSlot::ignored(),
                        /*ignoreResult=*/true);
        break;
      }

      LValue sourceLV = cgf.emitLValue(e->getSubExpr());
      Address sourceAddress = sourceLV.getAddress();
      Address destAddress = dest.getAddress();

      // Bitcast the source pointer to the destination type and copy.
      mlir::Value srcPtr = cgf.getBuilder().createBitcast(
          cgf.getLoc(e->getExprLoc()), sourceAddress.getPointer(),
          destAddress.getPointer().getType());

      auto &bld = cgf.getBuilder();
      mlir::IntegerAttr dstAlignAttr, srcAlignAttr;
      if (!destAddress.getAlignment().isZero())
        dstAlignAttr = cgf.getI64AttrCached(
            destAddress.getAlignment().getAsAlign().value());
      if (!sourceAddress.getAlignment().isZero())
        srcAlignAttr = cgf.getI64AttrCached(
            sourceAddress.getAlignment().getAsAlign().value());
      bld.createCopy(destAddress.getPointer(), srcPtr, /*isVolatile=*/false,
                     dstAlignAttr, srcAlignAttr);
      break;
    }
    case CK_NonAtomicToAtomic:
    case CK_AtomicToNonAtomic: {
      bool isToAtomic = (e->getCastKind() == CK_NonAtomicToAtomic);

      // Determine the atomic and value types.
      QualType atomicType = e->getSubExpr()->getType();
      QualType valueType = e->getType();
      if (isToAtomic)
        std::swap(atomicType, valueType);

      assert(atomicType->isAtomicType());
      assert(cgf.getContext().hasSameUnqualifiedType(
          valueType, atomicType->castAs<AtomicType>()->getValueType()));

      // Just recurse normally if we're ignoring the result or the
      // atomic type doesn't change representation.
      if (dest.isIgnored() || !cgf.cgm.isPaddedAtomicType(atomicType)) {
        Visit(e->getSubExpr());
        return;
      }

      CastKind peepholeTarget =
          (isToAtomic ? CK_AtomicToNonAtomic : CK_NonAtomicToAtomic);

      // These two cases are reverses of each other; try to peephole them.
      if (Expr *op = findPeephole(e->getSubExpr(), peepholeTarget,
                                  cgf.getContext())) {
        assert(cgf.getContext().hasSameUnqualifiedType(op->getType(),
                                                       e->getType()) &&
               "peephole significantly changed types?");
        Visit(op);
        return;
      }

      // If we're converting an r-value of non-atomic type to an r-value
      // of atomic type, just emit directly into the relevant sub-object.
      if (isToAtomic) {
        AggValueSlot valueDest = dest;
        if (!valueDest.isIgnored() &&
            cgf.cgm.isPaddedAtomicType(atomicType)) {
          // Zero-initialize.  (Strictly speaking, we only need to initialize
          // the padding at the end, but this is simpler.)
          if (!dest.isZeroed())
            cgf.emitNullInitialization(cgf.getLoc(e->getSourceRange()),
                                       dest.getAddress(), atomicType);

          // Build a GEP to refer to the value subobject (field 0 of the
          // padded atomic struct).
          mlir::Type valueCIRTy = cgf.convertTypeForMem(valueType);
          auto valuePtrTy = cir::PointerType::get(valueCIRTy);
          mlir::Value valuePtr = cgf.getBuilder().createGetMember(
              cgf.getLoc(e->getExprLoc()), valuePtrTy,
              valueDest.getAddress().getPointer(), "", 0);
          Address valueAddr(valuePtr, valueCIRTy,
                            valueDest.getAddress().getAlignment());
          valueDest = AggValueSlot::forAddr(
              valueAddr, valueDest.getQualifiers(),
              valueDest.isExternallyDestructed(),
              valueDest.isPotentiallyAliased(), AggValueSlot::DoesNotOverlap,
              AggValueSlot::IsZeroed);
        }

        cgf.emitAggExpr(e->getSubExpr(), valueDest);
        return;
      }

      // Otherwise, we're converting an atomic type to a non-atomic type.
      // Make an atomic temporary, emit into that, and then copy the value out.
      {
        AggValueSlot atomicSlot = cgf.createAggTemp(
            atomicType, cgf.getLoc(e->getSourceRange()),
            "atomic-to-nonatomic.temp");
        cgf.emitAggExpr(e->getSubExpr(), atomicSlot);

        mlir::Type valueCIRTy = cgf.convertTypeForMem(valueType);
        auto valuePtrTy = cir::PointerType::get(valueCIRTy);
        mlir::Value valuePtr = cgf.getBuilder().createGetMember(
            cgf.getLoc(e->getExprLoc()), valuePtrTy,
            atomicSlot.getAddress().getPointer(), "", 0);
        Address valueAddr(valuePtr, valueCIRTy,
                          atomicSlot.getAddress().getAlignment());
        RValue rvalue =
            RValue::getAggregate(valueAddr, atomicSlot.isVolatile());
        emitFinalDestCopy(valueType, rvalue);
        return;
      }
    }
    case CK_AddressSpaceConversion:
      return Visit(e->getSubExpr());

    case CK_DerivedToBase:
    case CK_BaseToDerived:
    case CK_UncheckedDerivedToBase:
      llvm_unreachable("cannot perform hierarchy conversion in EmitAggExpr: "
                       "should have been unpacked before we got here");

    case CK_LValueBitCast:
      llvm_unreachable("should not be emitting lvalue bitcast as rvalue");

    case CK_Dependent:
    case CK_BitCast:
    case CK_ArrayToPointerDecay:
    case CK_FunctionToPointerDecay:
    case CK_NullToPointer:
    case CK_NullToMemberPointer:
    case CK_BaseToDerivedMemberPointer:
    case CK_DerivedToBaseMemberPointer:
    case CK_MemberPointerToBoolean:
    case CK_ReinterpretMemberPointer:
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
    case CK_FloatingRealToComplex:
    case CK_FloatingComplexToReal:
    case CK_FloatingComplexToBoolean:
    case CK_FloatingComplexCast:
    case CK_FloatingComplexToIntegralComplex:
    case CK_IntegralRealToComplex:
    case CK_IntegralComplexToReal:
    case CK_IntegralComplexToBoolean:
    case CK_IntegralComplexCast:
    case CK_IntegralComplexToFloatingComplex:
    case CK_ARCProduceObject:
    case CK_ARCConsumeObject:
    case CK_ARCReclaimReturnedObject:
    case CK_ARCExtendBlockObject:
    case CK_CopyAndAutoreleaseBlockObject:
    case CK_BuiltinFnToFnPtr:
    case CK_ZeroToOCLOpaqueType:
    case CK_MatrixCast:
    case CK_HLSLVectorTruncation:
    case CK_HLSLMatrixTruncation:
    case CK_IntToOCLSampler:
    case CK_FloatingToFixedPoint:
    case CK_FixedPointToFloating:
    case CK_FixedPointCast:
    case CK_FixedPointToBoolean:
    case CK_FixedPointToIntegral:
    case CK_IntegralToFixedPoint:
      llvm_unreachable("cast kind invalid for aggregate types");

    default:
      cgf.cgm.errorNYI(e->getSourceRange(),
                       std::string("AggExprEmitter: VisitCastExpr: ") +
                           e->getCastKindName());
      return;
    }
  }
  void VisitStmt(Stmt *s) {
    cgf.cgm.errorNYI(s->getSourceRange(),
                     std::string("AggExprEmitter::VisitStmt: ") +
                         s->getStmtClassName());
    return;
  }
  void VisitParenExpr(ParenExpr *pe) { Visit(pe->getSubExpr()); }
  void VisitGenericSelectionExpr(GenericSelectionExpr *ge) {
    Visit(ge->getResultExpr());
  }
  void VisitCoawaitExpr(CoawaitExpr *e) {
    cgf.emitCoawaitExpr(*e, dest, /*ignoreResult=*/false);
  }
  void VisitCoyieldExpr(CoyieldExpr *e) {
    cgf.emitCoyieldExpr(*e, dest, /*ignoreResult=*/false);
  }
  void VisitUnaryCoawait(UnaryOperator *e) {
    cgf.cgm.errorNYI(e->getSourceRange(), "AggExprEmitter: VisitUnaryCoawait");
    return;
  }
  void VisitUnaryExtension(UnaryOperator *e) { Visit(e->getSubExpr()); }
  void VisitSubstNonTypeTemplateParmExpr(SubstNonTypeTemplateParmExpr *e) {
    Visit(e->getReplacement());
  }
  void VisitConstantExpr(ConstantExpr *e) {
    ensureDest(cgf.getLoc(e->getSourceRange()), e->getType());

    if (mlir::Attribute result =
            ConstantEmitter(cgf).tryEmitConstantExpr(e)) {
      // Match OGCG's CreateCoercedStore: skip store for zero-size
      // destinations (e.g., consteval constructors of empty structs used
      // as overlapping base classes or [[no_unique_address]] members).
      CharUnits dstSize =
          dest.getPreferredSize(cgf.getContext(), e->getType());
      if (dstSize.isZero())
        return;

      // Store the constant aggregate directly into the destination slot.
      auto loc = cgf.getLoc(e->getSourceRange());
      mlir::Value constVal = cgf.getBuilder().getConstant(
          loc, mlir::cast<mlir::TypedAttr>(result));
      // The constant type may differ from the dest type when a consteval
      // constructor evaluates a derived class — the APValue produces a
      // flattened struct while the alloca uses the CIR record type with
      // nested base subobjects. Match OGCG's CreateCoercedStore by
      // casting the destination pointer to match the constant's type.
      Address destAddr = dest.getAddress();
      if (constVal.getType() != destAddr.getElementType())
        destAddr = destAddr.withElementType(cgf.getBuilder(),
                                            constVal.getType());
      cgf.getBuilder().createStore(loc, constVal, destAddr);
      return;
    }

    Visit(e->getSubExpr());
  }
  void VisitMemberExpr(MemberExpr *e) { emitAggLoadOfLValue(e); }
  void VisitUnaryDeref(UnaryOperator *e) { emitAggLoadOfLValue(e); }
  void VisitStringLiteral(StringLiteral *e) { emitAggLoadOfLValue(e); }
  void VisitCompoundLiteralExpr(CompoundLiteralExpr *e);

  void VisitPredefinedExpr(const PredefinedExpr *e) {
    emitAggLoadOfLValue(e);
  }
  void VisitBinaryOperator(const BinaryOperator *e) {
    if (e->getOpcode() == BO_PtrMemD || e->getOpcode() == BO_PtrMemI)
      VisitPointerToDataMemberBinaryOperator(e);
    else {
      cgf.cgm.errorNYI(e->getSourceRange(),
                       "AggExprEmitter: aggregate binary expression");
      return;
    }
  }
  void VisitPointerToDataMemberBinaryOperator(const BinaryOperator *e) {
    LValue lv = cgf.emitPointerToDataMemberBinaryExpr(e);
    emitFinalDestCopy(e->getType(), lv);
  }
  void VisitBinComma(const BinaryOperator *e) {
    cgf.emitIgnoredExpr(e->getLHS());
    Visit(e->getRHS());
  }
  void VisitBinCmp(const BinaryOperator *e) {
    assert(cgf.getContext().hasSameType(e->getLHS()->getType(),
                                        e->getRHS()->getType()));
    const ComparisonCategoryInfo &cmpInfo =
        cgf.getContext().CompCategories.getInfoForType(e->getType());
    assert(cmpInfo.Record->isTriviallyCopyable() &&
           "cannot copy non-trivially copyable aggregate");

    QualType argTy = e->getLHS()->getType();

    if (!argTy->isIntegralOrEnumerationType() && !argTy->isRealFloatingType() &&
        !argTy->isNullPtrType() && !argTy->isPointerType() &&
        !argTy->isMemberPointerType() && !argTy->isAnyComplexType()) {
      cgf.cgm.errorNYI(e->getSourceRange(), "aggregate three-way comparison");
      return;
    }

    auto loc = cgf.getLoc(e->getSourceRange());

    if (argTy->isAnyComplexType()) {
      cgf.cgm.errorNYI(e->getSourceRange(), "complex three-way comparison");
      return;
    }

    auto lhs = cgf.emitAnyExpr(e->getLHS()).getValue();
    auto rhs = cgf.emitAnyExpr(e->getRHS()).getValue();

    mlir::Value resultScalar;
    if (argTy->isNullPtrType()) {
      resultScalar = cgf.getBuilder().getConstInt(
          loc, cmpInfo.getEqualOrEquiv()->getIntValue());
    } else {
      auto ltRes = cmpInfo.getLess()->getIntValue();
      auto eqRes = cmpInfo.getEqualOrEquiv()->getIntValue();
      auto gtRes = cmpInfo.getGreater()->getIntValue();
      auto cmpResultTy = cgf.getBuilder().getSIntNTy(ltRes.getBitWidth());
      if (!cmpInfo.isPartial()) {
        // Strong ordering.
        auto infoAttr = cir::CmpThreeWayInfoAttr::get(
            cgf.getBuilder().getContext(), ltRes.getExtValue(),
            eqRes.getExtValue(), gtRes.getExtValue());
        resultScalar = cir::CmpThreeWayOp::create(
            cgf.getBuilder(), loc, cmpResultTy, lhs, rhs, infoAttr);
      } else {
        // Partial ordering.
        auto unorderedRes = cmpInfo.getUnordered()->getIntValue();
        auto infoAttr = cir::CmpThreeWayInfoAttr::get(
            cgf.getBuilder().getContext(), ltRes.getExtValue(),
            eqRes.getExtValue(), gtRes.getExtValue(),
            unorderedRes.getExtValue());
        resultScalar = cir::CmpThreeWayOp::create(
            cgf.getBuilder(), loc, cmpResultTy, lhs, rhs, infoAttr);
      }
    }

    // Create the return value in the destination slot.
    ensureDest(loc, e->getType());
    LValue destLV = cgf.makeAddrLValue(dest.getAddress(), e->getType());

    // Emit the address of the first (and only) field in the comparison category
    // type, and initialize it from the constant integer value produced above.
    const FieldDecl *resultField = *cmpInfo.Record->field_begin();
    LValue fieldLV = cgf.emitLValueForFieldInitialization(
        destLV, resultField, resultField->getName());
    cgf.emitStoreThroughLValue(RValue::get(resultScalar), fieldLV,
                               /*isInit=*/true);

    // All done! The result is in the dest slot.
  }
  void VisitCXXRewrittenBinaryOperator(CXXRewrittenBinaryOperator *e) {
    Visit(e->getSemanticForm());
  }
  void VisitObjCMessageExpr(ObjCMessageExpr *e) {
    withReturnValueSlot(
        e, [&](ReturnValueSlot slot) { return cgf.emitObjCMessageExpr(e, slot); });
  }
  void VisitObjCIvarRefExpr(ObjCIvarRefExpr *e) { emitAggLoadOfLValue(e); }

  void VisitDesignatedInitUpdateExpr(DesignatedInitUpdateExpr *e) {
    AggValueSlot dest = ensureSlot(cgf.getLoc(e->getExprLoc()), e->getType());
    LValue destLV = cgf.makeAddrLValue(dest.getAddress(), e->getType());
    emitInitializationToLValue(e->getBase(), destLV);
    VisitInitListExpr(e->getUpdater());
  }
  void VisitAbstractConditionalOperator(const AbstractConditionalOperator *e) {
    mlir::Location loc = cgf.getLoc(e->getSourceRange());

    // OGCG integrates with PGO profiling via EmitBranchOnBoolExpr and
    // calls incrementProfileCounter for coverage.

    // Bind the common expression if necessary.
    CIRGenFunction::OpaqueValueMapping binding(cgf, e);

    // Ensure we have a destination slot to copy into.
    ensureDest(loc, e->getType());
    // Mirror OGCG: if this conditional aggregate expression yields a
    // non-trivial C struct into a destination whose lifetime is not externally
    // managed, defer a destroy cleanup after branch emission.
    bool wasExternallyDestructed = dest.isExternallyDestructed();
    bool destroyNonTrivialCStruct =
        !wasExternallyDestructed &&
        e->getType().isDestructedType() == QualType::DK_nontrivial_c_struct;
    dest.setExternallyDestructed(wasExternallyDestructed ||
                                 destroyNonTrivialCStruct);

    CIRGenFunction::ConditionalEvaluation eval(cgf);

    mlir::Value condValue = cgf.evaluateExprAsBool(e->getCond());

    cir::IfOp::create(
        cgf.getBuilder(), loc, condValue, /*withElseRegion=*/true,
        /*thenBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location) {
          eval.beginEvaluation();
          {
            CIRGenFunction::RunCleanupsScope cleanups(cgf);
            Visit(e->getTrueExpr());
            cleanups.deferCleanupsToOuterScope();
          }
          eval.endEvaluation();
          cgf.getBuilder().createYield(loc);
        },
        /*elseBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location) {
          // If the destination slot had to be created while emitting the true
          // branch, preserve the original destruction mode for the false path.
          dest.setExternallyDestructed(wasExternallyDestructed ||
                                       destroyNonTrivialCStruct);
          eval.beginEvaluation();
          {
            CIRGenFunction::RunCleanupsScope cleanups(cgf);
            Visit(e->getFalseExpr());
            cleanups.deferCleanupsToOuterScope();
          }
          eval.endEvaluation();
          cgf.getBuilder().createYield(loc);
        });

    if (destroyNonTrivialCStruct)
      cgf.pushDestroy(QualType::DK_nontrivial_c_struct, dest.getAddress(),
                      e->getType());
  }
  void VisitChooseExpr(const ChooseExpr *e) { Visit(e->getChosenSubExpr()); }
  void VisitCXXParenListInitExpr(CXXParenListInitExpr *e) {
    visitCXXParenListOrInitListExpr(e, e->getInitExprs(),
                                    e->getInitializedFieldInUnion(),
                                    e->getArrayFiller());
  }

  void VisitArrayInitLoopExpr(const ArrayInitLoopExpr *e,
                              mlir::Value outerBegin = {}) {
    // Emit the common subexpression.
    CIRGenFunction::OpaqueValueMapping binding(cgf, e->getCommonExpr());

    Address destPtr = ensureSlot(cgf.getLoc(e->getSourceRange()), e->getType())
                          .getAddress();
    uint64_t numElements = e->getArraySize().getZExtValue();

    if (!numElements)
      return;

    CIRGenBuilderTy &builder = cgf.getBuilder();
    mlir::Location loc = cgf.getLoc(e->getSourceRange());

    QualType elementType =
        cgf.getContext().getAsArrayType(e->getType())->getElementType();
    CharUnits elementSize = cgf.getContext().getTypeSizeInChars(elementType);
    CharUnits elementAlign =
        destPtr.getAlignment().alignmentOfArrayElement(elementSize);

    mlir::Type cirElementType = cgf.convertType(elementType);
    auto destPtrTy =
        mlir::cast<cir::PointerType>(destPtr.getPointer().getType());
    cir::PointerType cirElementPtrType =
        builder.getPointerTo(cirElementType, destPtrTy.getAddrSpace());

    // Get pointer to first element via array-to-pointer decay.
    auto begin = cir::CastOp::create(builder, loc, cirElementPtrType,
                                     cir::CastKind::array_to_ptrdecay,
                                     destPtr.getPointer());

    // Prepare to special-case multidimensional array initialization: share a
    // single cleanup across nested ArrayInitLoopExpr levels, matching OGCG.
    if (!outerBegin)
      outerBegin = begin;
    const auto *innerLoop = dyn_cast<ArrayInitLoopExpr>(e->getSubExpr());

    // Create an alloca for the loop index.
    mlir::Type indexType = cgf.ptrDiffTy;
    Address indexAddr = cgf.createTempAlloca(
        indexType, cgf.getPointerAlign(), loc, "arrayinit.index");

    // Initialize the index to 0.
    cir::ConstantOp zero = builder.getConstInt(
        loc, mlir::cast<cir::IntType>(indexType), 0);
    builder.createStore(loc, zero, indexAddr);

    // Compute the end value.
    cir::ConstantOp numElementsConst = builder.getConstInt(
        loc, mlir::cast<cir::IntType>(indexType), numElements);

    // Exception safety requires destroying already-initialized elements if
    // a per-element initializer throws. Push a partial array cleanup that
    // covers [begin, endOfInit) on the exception unwind path, matching
    // OGCG's pushRegularPartialArrayCleanup (CGExprAgg.cpp:1985).
    QualType::DestructionKind dtorKind = elementType.isDestructedType();
    Address endOfInit = Address::invalid();
    EHScopeStack::stable_iterator partialArrayCleanup =
        cgf.ehStack.stable_end();

    if (cgf.needsEHCleanup(dtorKind) && !innerLoop) {
      mlir::Value cleanupBegin = outerBegin;
      if (cleanupBegin.getType() != cirElementPtrType)
        cleanupBegin = builder.createPtrBitcast(cleanupBegin, cirElementType);
      endOfInit = cgf.createTempAlloca(cirElementPtrType, cgf.getPointerAlign(),
                                       loc, "arrayinit.endOfInit");
      builder.createStore(loc, begin, endOfInit);
      cgf.pushIrregularPartialArrayCleanup(cleanupBegin, endOfInit, elementType,
                                           elementAlign,
                                           cgf.getDestroyer(dtorKind));
      partialArrayCleanup = cgf.ehStack.stable_begin();
    } else {
      dtorKind = QualType::DK_none;
    }

    builder.createDoWhile(
        loc,
        /*condBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          cir::LoadOp currentIndex = builder.createLoad(loc, indexAddr);
          cir::CmpOp cmp = cir::CmpOp::create(builder, loc, cir::CmpOpKind::ne,
                                              currentIndex, numElementsConst);
          builder.createCondition(cmp);
        },
        /*bodyBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          cir::LoadOp currentIndex = builder.createLoad(loc, indexAddr);

          // Compute pointer to the current element.
          mlir::Value element =
              builder.createPtrStride(loc, begin, currentIndex);

          // Temporaries created during per-element initialization are
          // destroyed at the end of each iteration (C++ [class.temporary]p4).
          // The scope must close before the yield so that cleanup ops
          // precede the block terminator.
          {
            CIRGenFunction::RunCleanupsScope cleanupsScope(cgf);

            // Set the array init index for ArrayInitIndexExpr.
            CIRGenFunction::ArrayInitLoopExprScope scope(cgf, currentIndex);

            // Emit the per-element initializer.
            LValue elementLV = cgf.makeAddrLValue(
                Address(element, cirElementType, elementAlign), elementType);
            if (innerLoop) {
              auto elementSlot = AggValueSlot::forLValue(
                  elementLV, AggValueSlot::IsDestructed,
                  AggValueSlot::IsNotAliased, AggValueSlot::DoesNotOverlap);
              AggExprEmitter(cgf, elementSlot)
                  .VisitArrayInitLoopExpr(innerLoop, outerBegin);
            } else {
              emitInitializationToLValue(e->getSubExpr(), elementLV);
            }
          }

          // After successful construction, advance endOfInit past this
          // element so that cleanup destroys [begin, endOfInit) if the
          // next element's initializer throws.
          if (endOfInit.isValid()) {
            cir::ConstantOp stride = builder.getConstInt(
                loc, mlir::cast<cir::IntType>(indexType), 1);
            mlir::Value nextElement =
                builder.createPtrStride(loc, element, stride);
            builder.createStore(loc, nextElement, endOfInit);
          }

          // Increment the index.
          cir::ConstantOp one = builder.getConstInt(
              loc, mlir::cast<cir::IntType>(indexType), 1);
          auto nextIndex = cir::BinOp::create(
              builder, loc, indexType, cir::BinOpKind::Add, currentIndex, one,
              {}, {}, {}, {}, {}, {});

          builder.createStore(loc, nextIndex, indexAddr);

          builder.createYield(loc);
        });

    // Deactivate the partial-array cleanup now that all elements are
    // initialized. The full-array destructor handles normal destruction.
    if (dtorKind)
      cgf.deactivateCleanupBlock(partialArrayCleanup, /*dominatingIP=*/nullptr);
  }
  void VisitImplicitValueInitExpr(ImplicitValueInitExpr *e) {
    QualType t = e->getType();
    mlir::Location loc = cgf.getLoc(e->getSourceRange());
    AggValueSlot slot = ensureSlot(loc, t);
    emitNullInitializationToLValue(loc,
                                   cgf.makeAddrLValue(slot.getAddress(), t));
  }
  void VisitNoInitExpr(NoInitExpr *e) {} // Do nothing.
  void VisitCXXDefaultArgExpr(CXXDefaultArgExpr *dae) {
    CIRGenFunction::CXXDefaultArgExprScope scope(cgf, dae);
    Visit(dae->getExpr());
  }
  void VisitCXXInheritedCtorInitExpr(const CXXInheritedCtorInitExpr *e) {
    AggValueSlot slot =
        ensureSlot(cgf.getLoc(e->getSourceRange()), e->getType());
    cgf.emitInheritedCXXConstructorCall(e->getConstructor(),
                                        e->constructsVBase(), slot.getAddress(),
                                        e->inheritedFromVBase(), e);
  }
  void VisitCXXStdInitializerListExpr(CXXStdInitializerListExpr *e) {
    ASTContext &ctx = cgf.getContext();
    CIRGenFunction::SourceLocRAIIObject locRAIIObject{
        cgf, cgf.getLoc(e->getSourceRange())};
    // Emit an array containing the elements.  The array is externally
    // destructed if the std::initializer_list object is. Mark that we're
    // emitting an initializer_list backing array so emitArrayInit uses the
    // shared cir.cleanup.scope path (this is the O(N^2) hotspot).
    bool savedInitListArray = cgf.emittingInitializerListArray;
    cgf.emittingInitializerListArray = true;
    LValue array = cgf.emitLValue(e->getSubExpr());
    cgf.emittingInitializerListArray = savedInitListArray;
    assert(array.isSimple() && "initializer_list array not a simple lvalue");
    Address arrayPtr = array.getAddress();

    const ConstantArrayType *arrayType =
        ctx.getAsConstantArrayType(e->getSubExpr()->getType());
    assert(arrayType && "std::initializer_list constructed from non-array");

    RecordDecl *record = e->getType()->castAs<RecordType>()->getDecl();
    RecordDecl::field_iterator field = record->field_begin();
    assert(field != record->field_end() &&
           ctx.hasSameType(field->getType()->getPointeeType(),
                           arrayType->getElementType()) &&
           "Expected std::initializer_list first field to be const E *");
    const FieldDecl *startField = *field;
    ++field;
    assert(field != record->field_end() &&
           "Expected std::initializer_list to have two fields");
    const FieldDecl *endOrLengthField = *field;
    ++field;
    assert(field == record->field_end() &&
           "Expected std::initializer_list to only have two fields");

    // Start pointer.
    auto loc = cgf.getLoc(e->getSourceRange());
    AggValueSlot slotDest = ensureSlot(loc, e->getType());
    LValue destLV = cgf.makeAddrLValue(slotDest.getAddress(), e->getType());
    LValue start = cgf.emitLValueForFieldInitialization(destLV, startField,
                                                        startField->getName());
    mlir::Value arrayStart = arrayPtr.emitRawPointer();

    CIRGenBuilderTy &builder = cgf.getBuilder();

    // Store the start pointer through the LValue to preserve TBAA metadata,
    // matching OGCG's EmitStoreThroughLValue(RValue::get(ArrayStart), Start).
    cgf.emitStoreThroughLValue(RValue::get(arrayStart), start);

    QualType sizeQualTy = ctx.getSizeType();
    mlir::Type sizeTy = cgf.cgm.getTypes().convertType(sizeQualTy);
    mlir::Value sizeVal =
        builder.getConstAPInt(loc, sizeTy, arrayType->getSize());

    LValue endOrLength = cgf.emitLValueForFieldInitialization(
        destLV, endOrLengthField, endOrLengthField->getName());
    if (ctx.hasSameType(endOrLengthField->getType(), ctx.getSizeType())) {
      // Length.
      cgf.emitStoreThroughLValue(RValue::get(sizeVal), endOrLength);
    } else {
      // End pointer.
      assert(endOrLengthField->getType()->isPointerType() &&
             ctx.hasSameType(endOrLengthField->getType()->getPointeeType(),
                             arrayType->getElementType()) &&
             "Expected std::initializer_list second field to be const E *");

      // End pointer — points one past the last element.
      // OGCG uses a two-index GEP: GEP [N x T], ptr, {0, Size}. The first
      // index dereferences ptr-to-array → array, and the second indexes the
      // Size-th element. In CIR, we achieve this by decaying the array
      // pointer to an element pointer and then striding by Size.
      mlir::Type eltTy =
          cgf.cgm.getTypes().convertType(arrayType->getElementType());
      mlir::Value decayedPtr =
          builder.maybeBuildArrayDecay(loc, arrayPtr.getPointer(), eltTy);
      auto arrayEnd = builder.createPtrStride(loc, decayedPtr, sizeVal);
      // Same bitcast as for start pointer.
      Address endAddr = endOrLength.getAddress();
      mlir::Value endStoreAddr = endAddr.getPointer();
      auto endPtrTy = mlir::cast<cir::PointerType>(endStoreAddr.getType());
      auto endValuePtrTy = builder.getPointerTo(arrayEnd.getType());
      if (endPtrTy != endValuePtrTy)
        endStoreAddr =
            builder.createPtrBitcast(endStoreAddr, arrayEnd.getType());
      builder.createAlignedStore(loc, arrayEnd, endStoreAddr,
                                 endAddr.getAlignment());
    }
  }
  void VisitCXXScalarValueInitExpr(CXXScalarValueInitExpr *e) {
    QualType t = e->getType();
    mlir::Location loc = cgf.getLoc(e->getSourceRange());
    AggValueSlot slot = ensureSlot(loc, t);
    emitNullInitializationToLValue(loc,
                                   cgf.makeAddrLValue(slot.getAddress(), t));
  }
  void VisitCXXTypeidExpr(CXXTypeidExpr *e) {
    emitAggLoadOfLValue(e);
  }
  void VisitMaterializeTemporaryExpr(MaterializeTemporaryExpr *e) {
    Visit(e->getSubExpr());
  }
  void VisitOpaqueValueExpr(OpaqueValueExpr *e) {
    if (e->isUnique())
      Visit(e->getSourceExpr());
    else
      emitFinalDestCopy(e->getType(),
                        cgf.getOrCreateOpaqueLValueMapping(e));
  }

  void VisitPseudoObjectExpr(PseudoObjectExpr *e) {
    if (e->isGLValue()) {
      LValue lv = cgf.emitPseudoObjectLValue(e);
      return emitFinalDestCopy(e->getType(), lv);
    }

    AggValueSlot slot = ensureSlot(cgf.getLoc(e->getSourceRange()), e->getType());
    bool needsDestruction =
        !slot.isExternallyDestructed() &&
        e->getType().isDestructedType() == QualType::DK_nontrivial_c_struct;
    if (needsDestruction)
      slot.setExternallyDestructed();
    cgf.emitPseudoObjectRValue(e, slot);
    if (needsDestruction)
      cgf.pushDestroy(QualType::DK_nontrivial_c_struct, slot.getAddress(),
                      e->getType());
  }

  void VisitPackIndexingExpr(PackIndexingExpr *e) {
    Visit(e->getSelectedExpr());
  }

  void VisitVAArgExpr(VAArgExpr *e) {
    // Match OGCG's empty-record handling: the va_list side effects still
    // matter, but the aggregate copy itself is a no-op for records whose
    // fields are all empty (isEmptyRecordForLayout catches nested empty
    // structs like `struct { struct {} a; }` that CXXRecordDecl::isEmpty()
    // misses) and zero-sized GNU-extension records.
    if (isEmptyRecordForLayout(cgf.getContext(), e->getType()) ||
        cgf.getContext().getTypeSizeInChars(e->getType()).isZero()) {
      (void)cgf.emitVAArg(e);
      return;
    }

    QualType type = e->getType();

    if (dest.isIgnored()) {
      (void)cgf.emitVAArg(e);
      return;
    }

    llvm::Triple triple = cgf.getTarget().getTriple();
    QualType hfaBase;
    uint64_t hfaMembers = 0;
    bool isAArch64MultiMemberHFA =
        triple.isAArch64() && triple.isOSBinFormatELF() &&
        !e->isMicrosoftABI() &&
        isAArch64HomogeneousAggregate(cgf.getContext(), type, hfaBase,
                                      hfaMembers) &&
        hfaMembers > 1;

    // ELF AArch64 multi-register HFAs/HVAs already need a lowering-time temp
    // for the register save-area reconstruction. In that specific ABI case,
    // writing the va_arg result straight into the final destination removes the
    // redundant frontend-only vaarg.tmp without perturbing other aggregate
    // va_arg paths that rely on memcpy/TBAA-preserving copies.
    if (isAArch64MultiMemberHFA && !dest.isPotentiallyAliased() &&
        type.isNonTrivialToPrimitiveCopy() != QualType::PCK_Struct &&
        type.isNonTrivialToPrimitiveDestructiveMove() !=
            QualType::PCK_Struct) {
      mlir::Value vaArgValue = cgf.emitVAArg(e);
      cgf.emitAggregateStore(vaArgValue, dest.getAddress(), dest.isVolatile());
      return;
    }

    // Otherwise fall back to the temporary so emitFinalDestCopy can preserve
    // the existing aliasing and non-trivial aggregate copy/move semantics.
    mlir::Value vaArgValue = cgf.emitVAArg(e);
    mlir::Location loc = cgf.getLoc(e->getSourceRange());
    Address tmpAddr = cgf.createMemTemp(type, loc, "vaarg.tmp");
    cgf.emitAggregateStore(vaArgValue, tmpAddr);
    LValue tmpLValue = cgf.makeAddrLValue(tmpAddr, type);
    emitFinalDestCopy(type, tmpLValue);
  }

  void VisitCXXThrowExpr(const CXXThrowExpr *e) {
    cgf.emitCXXThrowExpr(e);
  }
  void VisitAtomicExpr(AtomicExpr *e) {
    RValue result = cgf.emitAtomicExpr(e);
    emitFinalDestCopy(e->getType(), result);
  }
};

/// EH cleanup that destroys one FIELD of the initializer_list backing array's
/// PARTIAL element. The element is located at runtime via `cursor` (the
/// arrayinit.endOfInit alloca, which points at the element under construction),
/// so the address is recomputed here and always dominates the shared
/// cir.cleanup.scope landing pad — even when this cleanup is materialized
/// inside a NESTED inner cleanup.scope's dispatch (which is why the field
/// cleanup lives on the EH stack rather than being emitted inline in the outer
/// cleanup region). Complete elements are destroyed by the array cleanup via
/// ~ElementType.
///
/// One of these is pushed per (element, field), so a field's cleanups sit in
/// the dispatch interleaved with the full-expression temporaries its own
/// element's initializers left behind. Both "has this field been built" and
/// "which element does this copy belong to" are read out of one slot, `owner`:
/// it holds the address of the element whose copy of this field is alive, or
/// null when none is, and a copy runs when it finds its own element there.
/// One slot per field serves all of that field's copies, since a copy that
/// runs nulls it and no other copy can then match.
///
/// A copy with no `elementIndex` serves every element: while no initializer
/// has left a temporary behind there is nothing for the field cleanups to be
/// interleaved with, so one copy where the first element built the field says
/// all there is to say. It runs on whatever element is in the slot.
///
/// The guard is emitted here rather than left to the shared walk's active-flag
/// test because the walk clears the flag once it has run a cleanup: a copy
/// skipped for pointing at the wrong element would still clear it and take the
/// destructor away from the copy that wanted it.
struct CursorRelativeFieldDestroy final : EHScopeStack::Cleanup {
  Address owner;
  mlir::Value arrayBegin;
  std::optional<uint64_t> elementIndex;
  QualType elementType;
  const FieldDecl *field;
  QualType fieldType;
  CIRGenFunction::Destroyer *destroyer;
  CharUnits elementAlign;

  CursorRelativeFieldDestroy(Address owner, mlir::Value arrayBegin,
                             std::optional<uint64_t> elementIndex,
                             QualType elementType, const FieldDecl *field,
                             QualType fieldType,
                             CIRGenFunction::Destroyer *destroyer,
                             CharUnits elementAlign)
      : owner(owner), arrayBegin(arrayBegin), elementIndex(elementIndex),
        elementType(elementType), field(field), fieldType(fieldType),
        destroyer(destroyer), elementAlign(elementAlign) {}

  // if (owner == &array[i]) { destroy owner's field; owner = null; }
  void emit(CIRGenFunction &cgf, Flags flags) override {
    CIRGenBuilderTy &builder = cgf.getBuilder();
    mlir::Location loc = *cgf.currSrcLoc;
    mlir::Value cur = builder.createLoad(loc, owner);
    mlir::Value isMine;
    if (!elementIndex) {
      isMine = builder.createCompare(loc, cir::CmpOpKind::ne, cur,
                                     builder.getNullPtr(cur.getType(), loc));
    } else {
      mlir::Value elt = arrayBegin;
      if (*elementIndex) {
        mlir::Value idx =
            builder.getConstantInt(loc, cgf.ptrDiffTy, *elementIndex);
        elt = builder.createPtrStride(loc, arrayBegin, idx);
      }
      isMine = builder.createCompare(loc, cir::CmpOpKind::eq, cur, elt);
    }
    cir::IfOp::create(
        builder, loc, isMine, /*withElseRegion=*/false,
        /*thenBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location l) {
          mlir::Type cirElementType = cgf.convertTypeForMem(elementType);
          const Address elemAddr(cur, cirElementType, elementAlign);
          const LValue elemLV = cgf.makeAddrLValue(elemAddr, elementType);
          LValue fieldLV = cgf.emitLValueForFieldInitialization(
              elemLV, field, field->getName());
          destroyer(cgf, fieldLV.getAddress(), fieldType);
          // The field is gone: a cir.cleanup.scope forming around
          // this one carries a copy of this cleanup too, and it
          // reads the same slot.
          builder.createStore(l, builder.getNullPtr(cur.getType(), l), owner);
          cir::YieldOp::create(b, l);
        });
  }
};

} // namespace

static bool isTrivialFiller(Expr *e) {
  if (!e)
    return true;

  if (isa<ImplicitValueInitExpr>(e))
    return true;

  if (auto *ile = dyn_cast<InitListExpr>(e)) {
    if (ile->getNumInits())
      return false;
    return isTrivialFiller(ile->getArrayFiller());
  }

  if (const auto *cons = dyn_cast_or_null<CXXConstructExpr>(e))
    return cons->getConstructor()->isDefaultConstructor() &&
           cons->getConstructor()->isTrivial();

  return false;
}

/// Given an expression with aggregate type that represents a value lvalue, this
/// method emits the address of the lvalue, then loads the result into DestPtr.
void AggExprEmitter::emitAggLoadOfLValue(const Expr *e) {
  LValue lv = cgf.emitLValue(e);

  // If the type of the l-value is atomic, then do an atomic load.
  if (lv.getType()->isAtomicType() ||
      cgf.isLValueSuitableForInlineAtomic(lv)) {
    cgf.emitAtomicLoad(lv, e->getExprLoc(), dest);
    return;
  }

  emitFinalDestCopy(e->getType(), lv);
}

void AggExprEmitter::VisitCompoundLiteralExpr(CompoundLiteralExpr *e) {
  if (dest.isPotentiallyAliased()) {
    // Just emit a load of the lvalue + a copy, because our compound literal
    // might alias the destination.
    emitAggLoadOfLValue(e);
    return;
  }

  AggValueSlot slot = ensureSlot(cgf.getLoc(e->getSourceRange()), e->getType());

  // Block-scope compound literals are destroyed at the end of the enclosing
  // scope in C.
  bool destruct =
      !cgf.getLangOpts().CPlusPlus && !slot.isExternallyDestructed();
  if (destruct)
    slot.setExternallyDestructed();

  cgf.emitAggExpr(e->getInitializer(), slot);

  if (destruct)
    if (QualType::DestructionKind dtorKind =
            e->getType().isDestructedType()) {
      cgf.pushLifetimeExtendedDestroy(
          cgf.getCleanupKind(dtorKind), slot.getAddress(), e->getType(),
          cgf.getDestroyer(dtorKind), dtorKind & EHCleanup);
    }
}

// Option to summarize array initializers found.
struct PrintArrayInitsOpt : public llvm::cl::opt<bool> {
  using llvm::cl::opt<bool>::opt;
  llvm::SmallVector<std::string> jsonObjects;
  ~PrintArrayInitsOpt() override {
    if (getValue()) {
      llvm::dbgs() << "{ \"inits\": [ ";
      bool first = true;
      for (auto &obj : jsonObjects) {
        if (first)
          first = false;
        else
          llvm::dbgs() << ",\n  ";

        llvm::dbgs() << obj;
      }
      llvm::dbgs() << " ] }\n";
    }
  }
};

/// Look through the braces of a transparent InitListExpr. `{x}` initializing an
/// object of x's own type is sugar for `x` -- a whole-object initializer, not a
/// member-wise list of field initializers -- so the classification below has to
/// peel it first. A one-field record is where it matters: `std::string s{t}`
/// has exactly as many initializers as the record has fields, so an unpeeled
/// list is indistinguishable from a member-wise one.
static Expr *ignoreTransparentInitLists(Expr *e) {
  auto *ile = dyn_cast<InitListExpr>(e->IgnoreParens());
  while (ile && ile->isTransparent()) {
    e = ile->getInit(0);
    ile = dyn_cast<InitListExpr>(e->IgnoreParens());
  }
  return e;
}

void AggExprEmitter::emitArrayInit(Address destPtr, cir::ArrayType arrayTy,
                                   QualType arrayQTy, Expr *e,
                                   ArrayRef<Expr *> args, Expr *arrayFiller) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  const mlir::Location loc = cgf.getLoc(e->getSourceRange());

  // EmbedExpr elements are emitted through emitInitializationToLValue in the
  // explicit-initializer loop below. Constant aggregate initialization uses
  // dedicated EmbedExpr element-count handling in CIRGenExprConstant.

  const uint64_t numInitElements = args.size();

  const QualType elementType =
      cgf.getContext().getAsArrayType(arrayQTy)->getElementType();

  const QualType elementPtrType = cgf.getContext().getPointerType(elementType);
  const CharUnits elementSize =
      cgf.getContext().getTypeSizeInChars(elementType);

  const mlir::Type cirElementType = cgf.convertType(elementType);
  const auto destPtrTy =
      mlir::cast<cir::PointerType>(destPtr.getPointer().getType());
  const cir::PointerType cirElementPtrType =
      builder.getPointerTo(cirElementType, destPtrTy.getAddrSpace());

  // Match OGCG's EmitArrayInit optimization: for large trivially-copyable
  // constant arrays, emit a private constant global and initialize by copying
  // from it instead of per-element stores.
  if (numInitElements * elementSize.getQuantity() > 16 &&
      elementType.isTriviallyCopyableType(cgf.getContext())) {
    LangAS globalAS = cgf.cgm.getGlobalConstantAddressSpace();
    QualType globalArrayQTy = cgf.getContext().getAddrSpaceQualType(
        cgf.getContext().removeAddrSpaceQualType(arrayQTy), globalAS);

    ConstantEmitter emitter(cgf);
    auto constant = mlir::dyn_cast_if_present<mlir::TypedAttr>(
        emitter.tryEmitForInitializer(e, globalAS, globalArrayQTy));
    if (constant) {
      mlir::ptr::MemorySpaceAttrInterface addrSpace =
          cir::toCIRLangAddressSpaceAttr(&cgf.getMLIRContext(), globalAS);
      cir::GlobalOp global = CIRGenModule::createGlobalOp(
          cgf.cgm, loc, cgf.cgm.createGlobalCompoundLiteralName(),
          constant.getType(),
          /*isConstant=*/true, addrSpace);
      global.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
      global.setInitialValueAttr(constant);
      global.setAlignment(
          cgf.getContext().getTypeAlignInChars(globalArrayQTy).getAsAlign().value());
      global->setAttr("unnamed_addr",
                      mlir::UnitAttr::get(&cgf.getMLIRContext()));
      emitter.finalize(global);

      mlir::Type globalPtrTy =
          builder.getPointerTo(global.getSymType(), global.getAddrSpaceAttr());
      mlir::Value srcPtr = cir::GetGlobalOp::create(
          builder, loc, globalPtrTy, global.getSymNameAttr());
      if (srcPtr.getType() != destPtr.getPointer().getType())
        srcPtr = builder.createAddrSpaceCast(loc, srcPtr,
                                             destPtr.getPointer().getType());

      mlir::IntegerAttr dstAlignAttr, srcAlignAttr;
      if (!destPtr.getAlignment().isZero())
        dstAlignAttr = cgf.getI64AttrCached(
            destPtr.getAlignment().getAsAlign().value());
      CharUnits srcAlign = cgf.getContext().getTypeAlignInChars(globalArrayQTy);
      if (!srcAlign.isZero())
        srcAlignAttr = cgf.getI64AttrCached(srcAlign.getAsAlign().value());

      auto copyOp = builder.createCopy(destPtr.getPointer(), srcPtr,
                                       dest.isVolatile(), dstAlignAttr,
                                       srcAlignAttr);
      cgf.addOpToCurrentSourceAtom(copyOp);
      return;
    }
  }

  auto begin = cir::CastOp::create(builder, loc, cirElementPtrType,
                                   cir::CastKind::array_to_ptrdecay,
                                   destPtr.getPointer());

  const CharUnits elementAlign =
      destPtr.getAlignment().alignmentOfArrayElement(elementSize);

  // Exception safety requires us to destroy all the
  // already-constructed members if an initializer throws.
  // For that, we'll need an EH cleanup.
  QualType::DestructionKind dtorKind = elementType.isDestructedType();
  Address endOfInit = Address::invalid();
  EHScopeStack::stable_iterator partialArrayCleanup = cgf.ehStack.stable_end();

  // Shared-landing-pad path (milestone 1: non-aggregate elements). For an
  // initializer list of >1 destructible, non-array elements each initialized by
  // a single ctor/scalar (NOT a member-wise InitListExpr) and with no array
  // filler, emit the element ctors inside ONE cir.cleanup.scope whose cleanup
  // region destroys the constructed prefix [begin, endOfInit) via a single
  // cursor-relative loop. All element ctors share that one landing pad (lowered
  // in FlattenCFG), instead of each materializing the partial-array-destroy
  // prefix inline -> O(N), not O(N^2). The cleanup region uses only
  // op-dominating values: `begin` (defined before the op) and the endOfInit
  // alloca. Member-wise aggregate elements (which push per-field subobject
  // cleanups) are handled in a later milestone; they fall through to the
  // per-call path below.
  // An enclosing lexical user `try` captures the body's exception calls at its
  // own landing pad; the shared cir.cleanup.scope landing pad does not yet
  // compose with that, so fall through to the per-call path in that case. The
  // O(N^2) target (global static initializers) has no enclosing user try, so
  // the win is preserved.
  const bool hasEnclosingTry =
      cgf.curLexScope && cgf.curLexScope->getClosestTryParent();
  // Restrict to C++ destructors: ObjC ARC lifetimes (DK_objc_*_lifetime) use a
  // different personality and non-trivial C structs a different destroy path;
  // both are out of scope for this optimization and fall through.
  //
  // Only fire when exceptions are enabled: cir.cleanup.scope shares an
  // *exception* cleanup landing pad across the element ctors. With exceptions
  // off there are no landing pads to share (element cleanups run only on the
  // normal path), so the op would just emit dead EH code
  // (invoke/landingpad/personality) and diverge from the classical
  // no-exceptions codegen. Fall through to the per-call path.
  const bool sharedCandidate =
      cgf.emittingInitializerListArray && cgf.getLangOpts().Exceptions &&
      dtorKind == QualType::DK_cxx_destructor && numInitElements > 1 &&
      !hasEnclosingTry && numInitElements == arrayTy.getSize() &&
      !elementType->isArrayType();
  bool allSingleObjectInits = sharedCandidate;
  for (uint64_t i = 0; allSingleObjectInits && i != numInitElements; ++i)
    if (isa<InitListExpr>(ignoreTransparentInitLists(args[i])))
      allSingleObjectInits = false;

  // M2: every element is a SIMPLE record aggregate (no bases, not a union, no
  // bitfields) initialized member-wise by an InitListExpr listing exactly the
  // fields. The partial element's constructed fields are destroyed
  // cursor-relative in the shared cleanup region (dominating: load endOfInit +
  // field), guarded by reused per-field flags; complete elements by the array
  // cleanup A.
  //
  // This is the path that has to stop at some length. It puts a cleanup on the
  // EH stack per destructible field, each guarded by its own activation flag,
  // and one shared pad means every element ctor is a predecessor of the block
  // that reads those flags. Once LLVM promotes them the pad holds a PHI per
  // flag with an incoming value per element. SimplifyCFG leaves that alone;
  // JumpThreading is what takes it apart, by duplicating the pad back into its
  // predecessors, where each copy folds against the flags known there -- and it
  // refuses to duplicate a block holding more than PhiDuplicateThreshold (76)
  // PHIs. Past that the PHI web survives to PHI elimination, which emits a copy
  // per (PHI, predecessor): quadratic machine code, ~12x .text at 76 elements
  // and growing.
  //
  // The single-object path above carries no such state -- one endOfInit cursor,
  // one slot, whatever the length -- so it is not capped. Capping it instead
  // costs what this whole op exists to avoid: with no shared scope, every
  // element's ctor keeps its own synthetic try, and each of those landing pads
  // materializes the partial-array destroy inline, which is the O(N^2) shape
  // cir.cleanup.scope was added to replace. A generated 1155-element table of
  // std::pair<char32_t, std::vector<char32_t>> takes 101 s of CIRGen with the
  // shared scope and does not finish in twenty minutes without it.
  constexpr uint64_t maxSharedCleanupScopeElements = 32;
  const clang::RecordDecl *aggRecord = nullptr;
  if (sharedCandidate && !allSingleObjectInits &&
      numInitElements <= maxSharedCleanupScopeElements) {
    if (const clang::RecordDecl *rd = elementType->getAsRecordDecl()) {
      const auto *cxxrd = dyn_cast<clang::CXXRecordDecl>(rd);
      bool ok = !rd->isUnion() && (!cxxrd || cxxrd->getNumBases() == 0);
      unsigned numFields = 0;
      for (const clang::FieldDecl *fd : rd->fields()) {
        if (fd->isBitField()) {
          ok = false;
          break;
        }
        ++numFields;
      }
      for (uint64_t i = 0; ok && i != numInitElements; ++i) {
        const auto *ile =
            dyn_cast<InitListExpr>(ignoreTransparentInitLists(args[i]));
        if (!ile || ile->getNumInits() != numFields)
          ok = false;
      }
      if (ok && numFields > 0)
        aggRecord = rd;
    }
  }

  if (allSingleObjectInits) {
    endOfInit = cgf.createTempAlloca(cirElementPtrType, cgf.getPointerAlign(),
                                     loc, "arrayinit.endOfInit");
    builder.createStore(loc, begin, endOfInit);

    // EH depth before pushing this scope's own cleanup — the shared walk stops
    // here so this scope's landing pad emits only its own cleanup; enclosing
    // cleanups run by chaining the flattened landing pads.
    EHScopeStack::stable_iterator scopeBoundary = cgf.ehStack.stable_begin();

    // The elements are emitted in nested chunks of at most
    // maxSharedCleanupScopeElements, one cir.cleanup.scope per chunk, rather
    // than all under one scope. Every full-expression temporary an element
    // builds (a vector temporary bound to a pair ctor's const& parameter, its
    // defaulted allocator argument, ...) gets a conditional cleanup guarded
    // by its own flag, and one shared pad over N elements reads all N flags:
    // promoted, that is a PHI per flag with an incoming value per element, a
    // web JumpThreading declines past PhiDuplicateThreshold, so it survives
    // to PHI elimination and register allocation as quadratic machine code
    // (an 1148-element generated Unicode table: 7 minutes host, 21-27 minutes
    // arm64, 27 MB .o). Chunking bounds each pad to a chunk's flags; earlier
    // chunks' cleanups run by FlattenCFG chaining each nested pad outward,
    // which also preserves reverse-construction destruction order. Chunk ids
    // come from a per-function allocator so a nested array's chunks never
    // collide with an enclosing array's.
    unsigned savedChunkId = cgf.currentSharedScopeChunkId;
    unsigned baseChunkId = cgf.nextSharedScopeChunkId;
    cgf.nextSharedScopeChunkId +=
        (numInitElements + maxSharedCleanupScopeElements - 1) /
        maxSharedCleanupScopeElements;
    cgf.currentSharedScopeChunkId = baseChunkId;

    // Push the partial-array cleanup onto the EH stack (like the per-call path)
    // so it is visible to the shared cleanup dispatch and destroyed on unwind.
    // All addresses used (`begin` above, the endOfInit alloca) dominate the op.
    // Stamped with the outermost chunk's id so the outermost dispatch — and
    // only it — runs the [begin, endOfInit) destroy.
    cgf.pushIrregularPartialArrayCleanup(begin, endOfInit, elementType,
                                         elementAlign,
                                         cgf.getDestroyer(dtorKind));
    EHScopeStack::stable_iterator partialCleanup = cgf.ehStack.stable_begin();

    std::function<cir::CleanupScopeOp(unsigned, uint64_t)> emitChunkScope =
        [&](unsigned level, uint64_t firstElem) -> cir::CleanupScopeOp {
      uint64_t lastElem =
          std::min(firstElem + maxSharedCleanupScopeElements, numInitElements);
      return cir::CleanupScopeOp::create(
          builder, loc,
          /*bodyBuilder=*/
          [&](mlir::OpBuilder &, mlir::Location) {
            // emittingSharedCleanupScope is set only around the element
            // initializers, as before: the nested scope created below runs
            // with the entry value, so its dispatch keeps the same emission
            // discipline for the destructor calls it contains.
            bool saved = cgf.emittingSharedCleanupScope;
            cgf.emittingSharedCleanupScope = true;
            unsigned savedChunk = cgf.currentSharedScopeChunkId;
            cgf.currentSharedScopeChunkId = baseChunkId + level;
            mlir::Value elt = begin;
            if (firstElem > 0) {
              mlir::Value idx =
                  builder.getConstantInt(loc, cgf.ptrDiffTy, firstElem);
              elt = builder.createPtrStride(loc, begin, idx);
              builder.createStore(loc, elt, endOfInit);
            }
            for (uint64_t i = firstElem; i != lastElem; ++i) {
              if (i > firstElem) {
                mlir::Value idx = builder.getConstantInt(loc, cgf.ptrDiffTy, i);
                elt = builder.createPtrStride(loc, begin, idx);
                builder.createStore(loc, elt, endOfInit);
              }
              const Address address(elt, cirElementType, elementAlign);
              const LValue elementLV = cgf.makeAddrLValue(address, elementType);
              emitInitializationToLValue(args[i], elementLV);
            }
            cgf.currentSharedScopeChunkId = savedChunk;
            cgf.emittingSharedCleanupScope = saved;
            if (lastElem != numInitElements)
              emitChunkScope(level + 1, lastElem);
            cir::YieldOp::create(builder, loc);
          },
          /*cleanupBuilder=*/
          [&](mlir::OpBuilder &, mlir::Location) {
            // Emit this chunk's shared cleanup dispatch: its conditional
            // cleanups (plus, for the outermost chunk, the partial-array
            // cleanup) run ONCE here, shared by every element ctor's landing
            // pad in the chunk (wired in FlattenCFG). Other chunks' cleanups
            // are on their own nested pads, chained outward to this one. The
            // yield lowers to resume.flat (rethrow) after the cleanups run.
            cgf.emitSharedCleanupScopeDispatch(scopeBoundary,
                                               baseChunkId + level);
            cir::YieldOp::create(builder, loc);
          });
    };
    cir::CleanupScopeOp scopeOp = emitChunkScope(0, 0);
    cgf.currentSharedScopeChunkId = savedChunkId;

    // The dispatch above stops at the scope boundary, so the objects already
    // alive around this list are not in it. They reach this pad only if a
    // cir.cleanup.scope forms around this one -- FlattenCFG chains a pad
    // outward, never sideways -- and one may well not: a list initializing a
    // plain local sits alone in its block. Enrol the outermost scope so those
    // cleanups are appended to the region as they pop, exactly as for a
    // formed scope; the nested scopes reach them through the chain.
    cgf.registerSharedCleanupScope(scopeOp, scopeBoundary);

    // Deactivate the partial-array cleanup on the normal path — the fully
    // constructed array's destruction is handled by the enclosing scope's
    // auto-var cleanup. Matches OGCG's CleanupDeactivationScope pattern.
    cgf.deactivateCleanupBlock(partialCleanup, /*dominatingIP=*/nullptr);
    return;
  }

  if (aggRecord) {
    endOfInit = cgf.createTempAlloca(cirElementPtrType, cgf.getPointerAlign(),
                                     loc, "arrayinit.endOfInit");
    builder.createStore(loc, begin, endOfInit);

    // EH depth before pushing this scope's own cleanups — the shared walk stops
    // here (enclosing cleanups run by chaining the flattened landing pads).
    EHScopeStack::stable_iterator scopeBoundary = cgf.ehStack.stable_begin();

    // Whole-element partial-array cleanup A: destroys COMPLETE elements
    // [begin, endOfInit) via ~ElementType. Pushed on the EH stack so it — and
    // this scope's field cleanups — are emitted by the shared dispatch.
    cgf.pushIrregularPartialArrayCleanup(begin, endOfInit, elementType,
                                         elementAlign,
                                         cgf.getDestroyer(dtorKind));
    EHScopeStack::stable_iterator partialCleanup = cgf.ehStack.stable_begin();

    // A destructible field gets a cursor-relative CursorRelativeFieldDestroy
    // on the EH stack, guarded by its own active flag (entry-block alloca).
    // Being on the EH stack (rather than emitted inline in the outer cleanup
    // region) is what lets NESTED inner cir.cleanup.scope dispatches chain to
    // these — e.g. an inner initializer_list<string> throwing while building
    // this element's vector<string> field must still destroy this element's
    // already-constructed string key.
    //
    // They are pushed inside the body, each one where its field finishes
    // constructing, and not hoisted ahead of the body: an unwind destroys
    // everything in reverse order of construction, and an element's
    // initializers create full-expression temporaries of their own, so a
    // hoisted field cleanup would sit below every one of them whatever the
    // construction order was. `T a; T b;` with a defaulted class-type
    // constructor argument is the small case — the temporary built for `b`
    // and the temporary built for `a` are on either side of `a` itself.
    //
    // Re-pushing is only needed once an initializer has actually put something
    // on the EH stack: until then there is nothing for the field cleanups to
    // be ordered against, and the cleanup pushed for the first element serves
    // every element after it.
    //
    // All the copies of one field's cleanup share a single slot -- one per
    // field, not one per (element, field) -- holding the address of the
    // element whose copy of that field is alive, or null when none is (see
    // CursorRelativeFieldDestroy). A slot per copy would instead leave the
    // dispatch reading one per element: the shared dispatch is turned into
    // per-edge code by JumpThreading, which stops at 76 PHIs, so from about
    // that many elements on every unwind edge would keep the whole
    // unspecialized dispatch. Measured on a table of two-string aggregates:
    // 4.5 KiB of .text at 74 elements, 111 KiB at 76.
    //
    // Every cleanup pushed here, in push order, for the deactivation below.
    llvm::SmallVector<EHScopeStack::stable_iterator, 8> fieldCleanups;
    // One slot per destructible field, in field order, made on first use.
    llvm::SmallVector<Address, 4> perFieldOwner;
    // The cleanups read their slot themselves, so the active flag the cleanup
    // machinery wants is only there to keep it from making one bool per copy
    // in deactivateCleanupBlock -- which is the per-element dispatch state the
    // slots exist to avoid. One bool for all of them is enough; nothing reads
    // it.
    Address unusedActiveFlag = Address::invalid();
    bool initPushedCleanup = false;

    auto pushFieldCleanup = [&](const clang::FieldDecl *fd,
                                QualType::DestructionKind fk,
                                unsigned destructibleIdx,
                                std::optional<uint64_t> elementIdx) {
      if (destructibleIdx == perFieldOwner.size()) {
        cir::AllocaOp ownerAlloca = cgf.createTempAlloca(
            cirElementPtrType, loc, "field.owner", /*arraySize=*/nullptr,
            /*insertIntoFnEntryBlock=*/true);
        ownerAlloca.setAlignmentAttr(cgf.cgm.getSize(cgf.getPointerAlign()));
        Address owner(ownerAlloca, cirElementPtrType, cgf.getPointerAlign());
        // The one dispatch below serves the unwind edges of every element,
        // including the edges taken before this field was ever built, so the
        // slot has to start empty.
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointAfter(ownerAlloca);
        builder.createStore(loc, builder.getNullPtr(cirElementPtrType, loc),
                            owner);
        perFieldOwner.push_back(owner);
      }
      if (!unusedActiveFlag.isValid()) {
        cir::AllocaOp flagAlloca = cgf.createTempAlloca(
            builder.getBoolTy(), loc, "field.cond", /*arraySize=*/nullptr,
            /*insertIntoFnEntryBlock=*/true);
        flagAlloca.setAlignmentAttr(cgf.cgm.getSize(CharUnits::One()));
        unusedActiveFlag =
            Address(flagAlloca, builder.getBoolTy(), CharUnits::One());
      }
      cgf.ehStack.pushCleanup<CursorRelativeFieldDestroy>(
          EHCleanup, perFieldOwner[destructibleIdx], begin, elementIdx,
          elementType, fd, fd->getType(), cgf.getDestroyer(fk), elementAlign);
      auto &scope = llvm::cast<EHCleanupScope>(
          *cgf.ehStack.find(cgf.ehStack.stable_begin()));
      // The walk must not guard these on the machinery's flag either: it
      // clears the flag of every cleanup it runs, and a copy that ran and
      // destroyed nothing (wrong element) would clear it out from under the
      // copy that meant to. Marking them active over the range keeps the
      // copies in the dispatch of a scope that forms around this one, which by
      // then sees them deactivated -- with their slots emptied, so they are
      // the no-ops they should be.
      scope.setActiveFlag(unusedActiveFlag);
      scope.setActiveOverCleanupScopeRange();
      fieldCleanups.push_back(cgf.ehStack.stable_begin());
    };

    auto scopeOp = cir::CleanupScopeOp::create(
        builder, loc,
        /*bodyBuilder=*/
        [&](mlir::OpBuilder &, mlir::Location) {
          bool saved = cgf.emittingSharedCleanupScope;
          cgf.emittingSharedCleanupScope = true;
          mlir::Value elt = begin;
          for (uint64_t i = 0; i != numInitElements; ++i) {
            if (i > 0) {
              mlir::Value idx = builder.getConstantInt(loc, cgf.ptrDiffTy, i);
              elt = builder.createPtrStride(loc, begin, idx);
              builder.createStore(loc, elt, endOfInit);
            }
            // The element before this one is complete, so A covers it now and
            // its field cleanups must stop firing.
            for (Address owner : perFieldOwner)
              builder.createStore(
                  loc, builder.getNullPtr(cirElementPtrType, loc), owner);

            const Address address(elt, cirElementType, elementAlign);
            const LValue elementLV = cgf.makeAddrLValue(address, elementType);
            auto *ile = cast<InitListExpr>(ignoreTransparentInitLists(args[i]));
            // This loop initializes the element's fields itself instead of
            // recursing through visitCXXParenListOrInitListExpr, so it has to
            // supply what that would have: a 'this' for any field whose
            // initializer is a CXXDefaultInitExpr, i.e. any field the braced
            // list left to its default member initializer.
            CIRGenFunction::FieldConstructionScope fcScope(cgf, address);
            unsigned fieldIdx = 0;
            unsigned destructibleIdx = 0;
            for (const clang::FieldDecl *fd : aggRecord->fields()) {
              LValue fieldLV = cgf.emitLValueForFieldInitialization(
                  elementLV, fd, fd->getName());
              EHScopeStack::stable_iterator beforeInit =
                  cgf.ehStack.stable_begin();
              emitInitializationToLValue(ile->getInit(fieldIdx), fieldLV);
              // A temporary this initializer left behind is what the field
              // cleanups have to be ordered against. One that was pushed and
              // popped again is already destroyed and is not in the dispatch,
              // so the stack depth is the question, not the traffic.
              if (cgf.ehStack.stable_begin() != beforeInit)
                initPushedCleanup = true;
              ++fieldIdx;

              QualType::DestructionKind fk = fd->getType().isDestructedType();
              if (fk == QualType::DK_none)
                continue;
              // This field of this element exists now, so its slot names this
              // element. Where the order matters the element also needs a
              // cleanup of its own; otherwise the cleanup this field already
              // has serves it, that being the one with no element of its own.
              if (initPushedCleanup || destructibleIdx >= perFieldOwner.size())
                pushFieldCleanup(fd, fk, destructibleIdx,
                                 initPushedCleanup ? std::optional<uint64_t>(i)
                                                   : std::optional<uint64_t>());
              builder.createStore(loc, elt, perFieldOwner[destructibleIdx]);
              ++destructibleIdx;
            }
          }
          cgf.emittingSharedCleanupScope = saved;
          cir::YieldOp::create(builder, loc);
        },
        /*cleanupBuilder=*/
        [&](mlir::OpBuilder &, mlir::Location) {
          // The shared walk emits, from innermost outward: each field cleanup
          // (cursor-relative, flag-guarded) for the partial element, then the
          // array cleanup A (~ElementType over the complete prefix), then any
          // enclosing EH cleanups.
          cgf.emitSharedCleanupScopeDispatch(scopeBoundary);
          cir::YieldOp::create(builder, loc);
        });

    // As above: nothing outside this list is in the dispatch, and a scope may
    // never form around this one to chain to.
    cgf.registerSharedCleanupScope(scopeOp, scopeBoundary);

    // Normal path: deactivate every field cleanup and A, innermost (the last
    // field of the last element) first. The fully constructed array is
    // destroyed by the enclosing scope's auto-var cleanup.
    //
    // Deactivate the exact scopes pushed above rather than whatever now sits on
    // top of the EH stack: initializing the elements can leave full-expression
    // temporary cleanups above ours (e.g. the defaulted `const allocator&`
    // argument of std::vector's initializer_list constructor), in which case
    // the topmost entries are not the ones we pushed. Popping those instead
    // left A and the field cleanups active, so they ran again on the normal
    // path and destroyed the already-destroyed elements a second time.
    //
    // Emptying the slots is what actually stops the field cleanups: the array
    // is complete, so the last element's fields belong to the enclosing
    // scope's cleanup for the whole array, and a dispatch that chains to these
    // copies must find nothing to do.
    for (Address owner : perFieldOwner)
      builder.createStore(loc, builder.getNullPtr(cirElementPtrType, loc),
                          owner);
    for (EHScopeStack::stable_iterator fc : llvm::reverse(fieldCleanups))
      cgf.deactivateCleanupBlock(fc, /*dominatingIP=*/nullptr);
    cgf.deactivateCleanupBlock(partialCleanup, /*dominatingIP=*/nullptr);
    return;
  }

  if (dtorKind) {
    // CIR manually deactivates the partial array cleanup at the end,
    // rather than using CleanupDeactivationScope (functionally equivalent).

    // Create alloca to track end of initialized region for exception safety
    endOfInit = cgf.createTempAlloca(cirElementPtrType, cgf.getPointerAlign(),
                                     loc, "arrayinit.endOfInit");

    // Initialize with beginning of array
    builder.createStore(loc, begin, endOfInit);

    // Register cleanup handler for partial array destruction on exception
    cgf.pushIrregularPartialArrayCleanup(begin, endOfInit, elementType,
                                         elementAlign,
                                         cgf.getDestroyer(dtorKind));
    partialArrayCleanup = cgf.ehStack.stable_begin();
  }

  // The 'current element to initialize'.  The invariants on this
  // variable are complicated.  Essentially, after each iteration of
  // the loop, it points to the last initialized element, except
  // that it points to the beginning of the array before any
  // elements have been initialized.
  mlir::Value element = begin;

  // Don't build the 'one' before the cycle to avoid
  // emmiting the redundant `cir.const 1` instrs.
  mlir::Value one;

  // Collect the initializer list so that we can print array initializers.
  llvm::SmallVector<mlir::Value> initList;

  // Emit the explicit initializers.
  for (uint64_t i = 0; i != numInitElements; ++i) {
    // Advance to the next element.
    if (i > 0) {
      one = builder.getConstantInt(loc, cgf.ptrDiffTy, i);
      element = builder.createPtrStride(loc, begin, one);

      // Tell the cleanup that it needs to destroy up to this
      // element.  TODO: some of these stores can be trivially
      // observed to be unnecessary.
      if (endOfInit.isValid())
        builder.createStore(loc, element, endOfInit);
    }

    const Address address = Address(element, cirElementType, elementAlign);
    const LValue elementLV = cgf.makeAddrLValue(address, elementType);
    emitInitializationToLValue(args[i], elementLV);

    CIRGenStatistics::Stats.collectArrayInit(initList, elementLV.getAddress());
  }

  const uint64_t numArrayElements = arrayTy.getSize();

  // Check whether there's a non-trivial array-fill expression.
  const bool hasTrivialFiller = isTrivialFiller(arrayFiller);

  // Any remaining elements need to be zero-initialized, possibly
  // using the filler expression.  We can skip this if the we're
  // emitting to zeroed memory.
  if (numInitElements != numArrayElements &&
      !(dest.isZeroed() && hasTrivialFiller &&
        cgf.getTypes().isZeroInitializable(elementType))) {
    // Advance to the start of the rest of the array.
    if (numInitElements) {
      one = builder.getConstantInt(loc, cgf.ptrDiffTy, 1);
      element = cir::PtrStrideOp::create(builder, loc, cirElementPtrType,
                                         element, one,
          cir::GEPNoWrapFlags::inbounds);

      // Update endOfInit after advancing to rest of array
      if (endOfInit.isValid())
        builder.createStore(loc, element, endOfInit);
    }

    // Allocate the temporary variable
    // to store the pointer to first unitialized element
    const Address tmpAddr = cgf.createTempAlloca(
        cirElementPtrType, cgf.getPointerAlign(), loc, "arrayinit.temp");
    LValue tmpLV = cgf.makeAddrLValue(tmpAddr, elementPtrType);
    cgf.emitStoreThroughLValue(RValue::get(element), tmpLV);

    // Compute the end of array
    cir::ConstantOp numArrayElementsConst = builder.getConstInt(
        loc, mlir::cast<cir::IntType>(cgf.ptrDiffTy), numArrayElements);
    mlir::Value end = cir::PtrStrideOp::create(builder, loc, cirElementPtrType,
                                               begin, numArrayElementsConst,
        cir::GEPNoWrapFlags::inbounds);

    builder.createDoWhile(
        loc,
        /*condBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          cir::LoadOp currentElement = builder.createLoad(loc, tmpAddr);
          cir::CmpOp cmp = cir::CmpOp::create(builder, loc, cir::CmpOpKind::ne,
                                              currentElement, end);
          builder.createCondition(cmp);
        },
        /*bodyBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          cir::LoadOp currentElement = builder.createLoad(loc, tmpAddr);

          // C++1z [class.temporary]p5:
          //   when a default constructor is called to initialize an element
          //   of an array with no corresponding initializer [...] the
          //   destruction of every temporary created in a default argument
          //   is sequenced before the construction of the next array
          //   element, if any.
          // Matches OGCG's RunCleanupsScope around the filler expression.
          {
            CIRGenFunction::RunCleanupsScope cleanupsScope(cgf);

            // Emit the actual filler expression.
            LValue elementLV = cgf.makeAddrLValue(
                Address(currentElement, cirElementType, elementAlign),
                elementType);
            if (arrayFiller)
              emitInitializationToLValue(arrayFiller, elementLV);
            else
              emitNullInitializationToLValue(loc, elementLV);
          }

          // Advance pointer and store them to temporary variable
          cir::ConstantOp one = builder.getConstInt(
              loc, mlir::cast<cir::IntType>(cgf.ptrDiffTy), 1);
          auto nextElement = cir::PtrStrideOp::create(
              builder, loc, cirElementPtrType, currentElement, one,
              cir::GEPNoWrapFlags::inbounds);

          // Update endOfInit after each filled element
          if (endOfInit.isValid())
            builder.createStore(loc, nextElement, endOfInit);

          cgf.emitStoreThroughLValue(RValue::get(nextElement), tmpLV);

          builder.createYield(loc);
        });
  }

  // Deactivate the partial-array cleanup now that all elements are
  // initialized. Without this, the cleanup would fire during exception
  // unwind even though the full-array destructor (registered by
  // emitAutoVarCleanups) already handles destruction, causing
  // double-destruction. Matches OGCG's CleanupDeactivationScope pattern.
  if (dtorKind)
    cgf.deactivateCleanupBlock(partialArrayCleanup, /*dominatingIP=*/nullptr);

  // Characterize the list of initial elements.
  CIRGenStatistics::Stats.recordArrayInit(initList, arrayTy);
}

/// EmitFinalDestCopy - Perform the final copy to DestPtr, if desired.
void AggExprEmitter::emitFinalDestCopy(QualType type, RValue src) {
  assert(src.isAggregate() && "value must be aggregate value!");
  LValue srcLV = cgf.makeAddrLValue(src.getAggregateAddress(), type);
  emitFinalDestCopy(type, srcLV, CIRGenFunction::EVK_RValue);
}

/// Perform the final copy to destPtr, if desired.
void AggExprEmitter::emitFinalDestCopy(
    QualType type, const LValue &src,
    CIRGenFunction::ExprValueKind srcValueKind) {
  // If dest is ignored, then we're evaluating an aggregate expression
  // in a context that doesn't care about the result.  Note that loads
  // from volatile l-values force the existence of a non-ignored
  // destination.
  if (dest.isIgnored())
    return;

  if (srcValueKind == CIRGenFunction::EVK_RValue) {
    if (type.isNonTrivialToPrimitiveDestructiveMove() == QualType::PCK_Struct) {
      if (dest.isPotentiallyAliased())
        cgf.emitSupportedNonTrivialCStructMoveAssignment(dest.getAddress(),
                                                         src.getAddress(),
                                                         type);
      else
        cgf.emitSupportedNonTrivialCStructMoveConstructor(dest.getAddress(),
                                                          src.getAddress(),
                                                          type);
      return;
    }
  } else {
    if (type.isNonTrivialToPrimitiveCopy() == QualType::PCK_Struct) {
      if (dest.isPotentiallyAliased())
        cgf.emitSupportedNonTrivialCStructCopyAssignment(dest.getAddress(),
                                                         src.getAddress(),
                                                         type);
      else
        cgf.emitSupportedNonTrivialCStructCopyConstructor(dest.getAddress(),
                                                          src.getAddress(),
                                                          type);
      return;
    }
  }

  assert(!cir::MissingFeatures::aggValueSlotVolatile());

  AggValueSlot srcAgg = AggValueSlot::forLValue(src, AggValueSlot::IsDestructed,
                                                AggValueSlot::IsAliased,
                                                AggValueSlot::MayOverlap);
  emitCopy(type, dest, srcAgg);
}

/// Perform a copy from the source into the destination.
///
/// \param type - the type of the aggregate being copied; qualifiers are
///   ignored
void AggExprEmitter::emitCopy(QualType type, const AggValueSlot &dest,
                              const AggValueSlot &src) {

  // If the result of the assignment is used, copy the LHS there also.
  // It's volatile if either side is.  Use the minimum alignment of
  // the two sides.
  LValue destLV = cgf.makeAddrLValue(dest.getAddress(), type);
  LValue srcLV = cgf.makeAddrLValue(src.getAddress(), type);
  assert(!cir::MissingFeatures::aggValueSlotVolatile());
  cgf.emitAggregateCopy(destLV, srcLV, type, dest.mayOverlap(),
                        dest.isVolatile() || src.isVolatile());
}

void AggExprEmitter::emitInitializationToLValue(Expr *e, LValue lv) {
  const QualType type = lv.getType();

  if (dest.isZeroed() && isSimpleZero(e, cgf))
    return;

  if (isa<ImplicitValueInitExpr, CXXScalarValueInitExpr>(e)) {
    const mlir::Location loc = e->getSourceRange().isValid()
                                   ? cgf.getLoc(e->getSourceRange())
                                   : *cgf.currSrcLoc;
    return emitNullInitializationToLValue(loc, lv);
  }

  if (isa<NoInitExpr>(e))
    return;

  if (type->isReferenceType()) {
    RValue rv = cgf.emitReferenceBindingToExpr(e);
    return cgf.emitStoreThroughLValue(rv, lv);
  }

  switch (cgf.getEvaluationKind(type)) {
  case cir::TEK_Complex:
    cgf.emitComplexExprIntoLValue(e, lv, /*isInit*/ true);
    break;
  case cir::TEK_Aggregate:
    cgf.emitAggExpr(e, AggValueSlot::forLValue(lv, AggValueSlot::IsDestructed,
                                               AggValueSlot::IsNotAliased,
                                               AggValueSlot::MayOverlap,
                                               dest.isZeroed()));

    return;
  case cir::TEK_Scalar:
    if (lv.isSimple())
      cgf.emitScalarInit(e, cgf.getLoc(e->getSourceRange()), lv);
    else
      cgf.emitStoreThroughLValue(RValue::get(cgf.emitScalarExpr(e)), lv);
    return;
  }
}

void AggExprEmitter::VisitCXXConstructExpr(const CXXConstructExpr *e) {
  AggValueSlot slot = ensureSlot(cgf.getLoc(e->getSourceRange()), e->getType());
  cgf.emitCXXConstructExpr(e, slot);
}

void AggExprEmitter::emitNullInitializationToLValue(mlir::Location loc,
                                                    LValue lv) {
  const QualType type = lv.getType();

  // If the destination slot is already zeroed out before the aggregate is
  // copied into it, we don't have to emit any zeros here.
  if (dest.isZeroed() && cgf.getTypes().isZeroInitializable(type))
    return;

  if (cgf.hasScalarEvaluationKind(type)) {
    // For non-aggregates, we can store the appropriate null constant.
    mlir::Value null = cgf.cgm.emitNullConstant(type, loc);
    if (lv.isSimple()) {
      cgf.emitStoreOfScalar(null, lv, /* isInitialization */ true);
      return;
    }

    cgf.emitStoreThroughBitfieldLValue(RValue::get(null), lv);
    return;
  }

  // There's a potential optimization opportunity in combining
  // memsets; that would be easy for arrays, but relatively
  // difficult for structures with the current code.
  cgf.emitNullInitialization(loc, lv.getAddress(), lv.getType());
}

void AggExprEmitter::VisitLambdaExpr(LambdaExpr *e) {
  CIRGenFunction::SourceLocRAIIObject loc{cgf, cgf.getLoc(e->getSourceRange())};
  AggValueSlot slot = ensureSlot(cgf.getLoc(e->getSourceRange()), e->getType());
  [[maybe_unused]] LValue slotLV =
      cgf.makeAddrLValue(slot.getAddress(), e->getType());

  // We'll need to enter cleanup scopes in case any of the element
  // initializers throws an exception or contains branch out of the expressions.
  // Use CleanupDeactivationScope to deactivate capture cleanups after the
  // full lambda is constructed — matching OGCG's pattern. Without this,
  // capture field cleanups stay on the EH stack and can reference values
  // defined inside conditional regions (e.g., ternary branches), causing
  // dominance violations.
  CIRGenFunction::CleanupDeactivationScope cleanupScope(cgf);

  for (auto [curField, capture, captureInit] : llvm::zip(
           e->getLambdaClass()->fields(), e->captures(), e->capture_inits())) {
    // Pick a name for the field.
    llvm::StringRef fieldName = curField->getName();
    if (capture.capturesVariable()) {
      assert(!curField->isBitField() && "lambdas don't have bitfield members!");
      ValueDecl *v = capture.getCapturedVar();
      fieldName = v->getName();
      cgf.cgm.lambdaFieldToName[curField] = fieldName;
    } else if (capture.capturesThis()) {
      cgf.cgm.lambdaFieldToName[curField] = "this";
    } else if (capture.capturesVLAType()) {
      // VLA type captures store the VLA's size, not a pointer to the VLA.
      // The field name is the compiler-generated name from the FieldDecl.
      cgf.cgm.lambdaFieldToName[curField] = fieldName;
    } else {
      cgf.cgm.errorNYI(e->getSourceRange(), "Unhandled capture kind");
      return;
    }

    // Emit initialization
    LValue lv =
        cgf.emitLValueForFieldInitialization(slotLV, curField, fieldName);
    if (curField->hasCapturedVLAType()) {
      // VLA type captures store the size of the VLA into the lambda field.
      cgf.emitLambdaVLACapture(curField->getCapturedVLAType(), lv);
      continue;
    }

    emitInitializationToLValue(captureInit, lv);

    // Push a destructor cleanup and defer its deactivation. The
    // CleanupDeactivationScope above will deactivate this cleanup when
    // the full lambda is constructed, preventing stale references to
    // values defined inside conditional regions.
    //
    // OGCG uses pushDestroyAndDeferDeactivation(NormalAndEHCleanup, ...)
    // but CIR currently only needs EHCleanup since normal-path
    // destruction is handled by the lambda closure's own destructor.
    if (QualType::DestructionKind dtorKind =
            curField->getType().isDestructedType()) {
      // OGCG uses NormalAndEHCleanup here, but CIR intentionally only pushes
      // EHCleanup — normal-path destruction is handled by the closure's
      // own destructor.
      if (cgf.needsEHCleanup(dtorKind))
        cgf.pushDestroyAndDeferDeactivation(
            static_cast<CleanupKind>(cgf.getCleanupKind(dtorKind) & EHCleanup),
            lv.getAddress(),
            curField->getType(), cgf.getDestroyer(dtorKind),
            /*useEHCleanupForArray=*/false);
    }
  }
}

void AggExprEmitter::VisitExprWithCleanups(ExprWithCleanups *e) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location scopeLoc = cgf.getLoc(e->getSourceRange());
  mlir::OpBuilder::InsertPoint scopeBegin;

  cir::ScopeOp::create(builder, scopeLoc, /*scopeBuilder=*/
                       [&](mlir::OpBuilder &b, mlir::Location loc) {
                         scopeBegin = b.saveInsertionPoint();
                       });

  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.restoreInsertionPoint(scopeBegin);
    // Keep the structural cir.scope for downstream CIR consumers, but rely on
    // LexicalScope's single RunCleanupsScope rather than nesting an additional
    // cleanup scope here. The extra scope caused initializer_list temporaries
    // to be destroyed too early.
    CIRGenFunction::SourceLocRAIIObject exprLoc{
        cgf, cgf.getLoc(e->getSubExpr()->getExprLoc())};
    CIRGenFunction::LexicalScope lexScope{cgf, scopeLoc,
                                          builder.getInsertionBlock()};
    Visit(e->getSubExpr());
  }
}

void AggExprEmitter::VisitCallExpr(const CallExpr *e) {
  if (e->getCallReturnType(cgf.getContext())->isReferenceType()) {
    emitAggLoadOfLValue(e);
    return;
  }

  withReturnValueSlot(
      e, [&](ReturnValueSlot slot) { return cgf.emitCallExpr(e, slot); });
}

void AggExprEmitter::withReturnValueSlot(
    const Expr *e, llvm::function_ref<RValue(ReturnValueSlot)> fn) {
  QualType retTy = e->getType();
  mlir::Location loc = cgf.getLoc(e->getSourceRange());

  bool requiresDestruction =
      !dest.isExternallyDestructed() &&
      retTy.isDestructedType() == QualType::DK_nontrivial_c_struct;
  if (requiresDestruction) {
    cgf.cgm.errorNYI(
        e->getSourceRange(),
        "withReturnValueSlot: return value requiring destruction is NYI");
    return;
  }

  const auto &cgo = cgf.cgm.getCodeGenOpts();
  const auto &lo = cgf.getLangOpts();
  bool emitLifetimeMarkers =
      !cgo.DisableLifetimeMarkers &&
      (cgo.SanitizeAddressUseAfterScope ||
       lo.Sanitize.has(SanitizerKind::HWAddress) ||
       lo.Sanitize.has(SanitizerKind::Memory) ||
       cgo.OptimizationLevel != 0);

  // If it makes no observable difference, save a memcpy + temporary.
  //
  // We need to always provide our own temporary if destruction is required.
  // Otherwise, fn will emit its own, notice that it's "unused", and end its
  // lifetime before we have the chance to emit a proper destructor call.
  //
  // CIR's ReturnValueSlot does not model OGCG's IsUnused bit. When an
  // aggregate call result is ignored, low-level call emission will otherwise
  // synthesize its own temp alloca without the full-expression lifetime
  // markers that OGCG emits for the unused result.
  bool useTemp = dest.isPotentiallyAliased() ||
                 (dest.isIgnored() && emitLifetimeMarkers) ||
                 (requiresDestruction && dest.isIgnored());

  Address retAddr = Address::invalid();
  EHScopeStack::stable_iterator lifetimeEndCleanup =
      EHScopeStack::stable_iterator::invalid();
  if (!useTemp) {
    retAddr = dest.getAddress();
  } else {
    retAddr = cgf.createMemTemp(retTy, loc, "tmp");
    if (emitLifetimeMarkers) {
      cgf.getBuilder().createLifetimeStart(loc, retAddr.getPointer());
      cgf.pushFullExprCleanup<CallLifetimeEnd>(NormalEHLifetimeMarker, retAddr);
      lifetimeEndCleanup = cgf.ehStack.stable_begin();
    }
  }

  // OGCG passes IsResultUnused to ReturnValueSlot to enable ABI optimizations
  // when the return value is unused (CGExprAgg.cpp:318). CIR currently uses
  // ReturnValueSlot(retAddr, isVolatile) and does not model IsResultUnused.
  RValue src = fn(ReturnValueSlot(retAddr, dest.isVolatile()));

  if (!useTemp)
    return;

  emitFinalDestCopy(e->getType(), src);

  if (!requiresDestruction && lifetimeEndCleanup.isValid()) {
    cgf.deactivateCleanupBlock(lifetimeEndCleanup,
                               /*dominatingIP=*/nullptr);
    cgf.getBuilder().createLifetimeEnd(loc, retAddr.getPointer());
  }
}

void AggExprEmitter::VisitInitListExpr(InitListExpr *e) {
  if (e->hadArrayRangeDesignator()) {
    // The GNU array range designator extension is not implemented for aggregate
    // initialization. Emit a graceful diagnostic instead of crashing, matching
    // classic CodeGen's ErrorUnsupported (constant range designators are still
    // handled on the constant-folding path and never reach here).
    cgf.cgm.errorNYI(e->getSourceRange(), "GNU array range designator extension");
    return;
  }

  if (e->isTransparent())
    return Visit(e->getInit(0));

  visitCXXParenListOrInitListExpr(
      e, e->inits(), e->getInitializedFieldInUnion(), e->getArrayFiller());
}

void AggExprEmitter::visitCXXParenListOrInitListExpr(
    Expr *e, ArrayRef<Expr *> args, FieldDecl *initializedFieldInUnion,
    Expr *arrayFiller) {

  const mlir::Location loc = cgf.getLoc(e->getSourceRange());
  const AggValueSlot dest = ensureSlot(loc, e->getType());

  if (e->getType()->isConstantArrayType()) {
    cir::ArrayType arrayTy =
        cast<cir::ArrayType>(dest.getAddress().getElementType());
    emitArrayInit(dest.getAddress(), arrayTy, e->getType(), e, args,
                  arrayFiller);
    return;
  } else if (e->getType()->isVariableArrayType()) {
    cgf.cgm.errorNYI(e->getSourceRange(),
                     "visitCXXParenListOrInitListExpr variable array type");
    return;
  }

  if (e->getType()->isArrayType()) {
    cgf.cgm.errorNYI(e->getSourceRange(),
                     "visitCXXParenListOrInitListExpr array type");
    return;
  }

  assert(e->getType()->isRecordType() && "Only support structs/unions here!");

  // Do struct initialization; this code just sets each individual member
  // to the approprate value.  This makes bitfield support automatic;
  // the disadvantage is that the generated code is more difficult for
  // the optimizer, especially with bitfields.
  unsigned numInitElements = args.size();
  auto *record = e->getType()->castAsRecordDecl();

  // We'll need to enter cleanup scopes in case any of the element
  // initializers throws an exception. The CleanupDeactivationScope ensures
  // that the partial-destruction cleanups pushed below are deactivated once
  // the full aggregate is constructed — matching OGCG's pattern.
  CIRGenFunction::CleanupDeactivationScope cleanups(cgf);

  unsigned curInitIndex = 0;

  // Emit initialization of base classes.
  if (auto *cxxrd = dyn_cast<CXXRecordDecl>(record)) {
    assert(numInitElements >= cxxrd->getNumBases() &&
           "missing initializer for base class");
    for (auto &base : cxxrd->bases()) {
      assert(!base.isVirtual() && "should not see vbases here");
      CXXRecordDecl *baseRD = base.getType()->getAsCXXRecordDecl();
      Address address = cgf.getAddressOfDirectBaseInCompleteClass(
          loc, dest.getAddress(), cxxrd, baseRD,
          /*baseIsVirtual=*/false);
      AggValueSlot aggSlot = AggValueSlot::forAddr(
          address, Qualifiers(), AggValueSlot::IsDestructed,
          AggValueSlot::IsNotAliased,
          cgf.getOverlapForBaseInit(cxxrd, baseRD, false));
      cgf.emitAggExpr(args[curInitIndex++], aggSlot);
      if (base.getType().isDestructedType()) {
        // Push a destroy cleanup for the initialized base so it gets
        // destroyed if a later initializer throws. The cleanup is
        // deferred for deactivation — RunCleanupsScope will deactivate
        // it when the full aggregate is constructed.
        cgf.pushDestroyAndDeferDeactivation(
            base.getType().isDestructedType(), address, base.getType());
      }
    }
  }

  // Prepare a 'this' for CXXDefaultInitExprs.
  CIRGenFunction::FieldConstructionScope fcScope(cgf, dest.getAddress());

  LValue destLV = cgf.makeAddrLValue(dest.getAddress(), e->getType());

  const bool zeroInitPadding =
      cgf.cgm.shouldZeroInitPadding() && !dest.isZeroed();

  if (record->isUnion()) {
    // Only initialize one field of a union. The field itself is
    // specified by the initializer list.
    if (!initializedFieldInUnion) {
      // Empty union; we have nothing to do.

#ifndef NDEBUG
      // Make sure that it's really an empty and not a failure of
      // semantic analysis.
      for (const auto *field : record->fields())
        assert(
            (field->isUnnamedBitField() || field->isAnonymousStructOrUnion()) &&
            "Only unnamed bitfields or anonymous class allowed");
#endif
      return;
    }

    // FIXME: volatility
    FieldDecl *field = initializedFieldInUnion;

    LValue fieldLoc =
        cgf.emitLValueForFieldInitialization(destLV, field, field->getName());
    if (numInitElements) {
      // Store the initializer into the field
      emitInitializationToLValue(args[0], fieldLoc);
      if (zeroInitPadding) {
        uint64_t totalSize = cgf.getContext().toBits(
            dest.getPreferredSize(cgf.getContext(), destLV.getType()));
        uint64_t fieldSize =
            cgf.getContext().getTypeSize(fieldLoc.getType());
        doZeroInitPadding(fieldSize, totalSize, nullptr);
      }
    } else {
      // Default-initialize to null.
      if (zeroInitPadding)
        emitNullInitializationToLValue(cgf.getLoc(e->getSourceRange()), destLV);
      else
        emitNullInitializationToLValue(cgf.getLoc(e->getSourceRange()),
                                       fieldLoc);
    }
    return;
  }

  // Here we iterate over the fields; this makes it simpler to both
  // default-initialize fields and skip over unnamed fields.
  const ASTRecordLayout &layout = cgf.getContext().getASTRecordLayout(record);
  uint64_t paddingStart = 0;

  for (const FieldDecl *field : record->fields()) {
    // We're done once we hit the flexible array member.
    if (field->getType()->isIncompleteArrayType())
      break;

    // Always skip anonymous bitfields.
    if (field->isUnnamedBitField())
      continue;

    // We're done if we reach the end of the explicit initializers, we
    // have a zeroed object, and the rest of the fields are
    // zero-initializable.
    if (curInitIndex == numInitElements && dest.isZeroed() &&
        cgf.getTypes().isZeroInitializable(e->getType()))
      break;

    if (zeroInitPadding)
      doZeroInitPadding(paddingStart,
                        layout.getFieldOffset(field->getFieldIndex()), field);

    LValue lv =
        cgf.emitLValueForFieldInitialization(destLV, field, field->getName());
    // OGCG calls setNonGC() here to suppress ObjC GC write barriers for
    // initialized fields. ObjC GC was deprecated in 2012 and is not
    // supported by CIR.

    if (curInitIndex < numInitElements) {
      // Store the initializer into the field.
      CIRGenFunction::SourceLocRAIIObject loc{
          cgf, cgf.getLoc(record->getSourceRange())};
      emitInitializationToLValue(args[curInitIndex++], lv);
    } else {
      // We're out of initializers; default-initialize to null
      emitNullInitializationToLValue(cgf.getLoc(e->getSourceRange()), lv);
    }

    // Push a destructor if necessary.
    // FIXME: if we have an array of structures, all explicitly
    // initialized, we can end up pushing a linear number of cleanups.
    if (QualType::DestructionKind dtorKind =
            field->getType().isDestructedType()) {
      assert(lv.isSimple());
      // Push a destroy cleanup for this field, deferred for deactivation.
      // If a later field's init throws, this cleanup destroys the
      // already-initialized field. When the full aggregate is constructed,
      // RunCleanupsScope deactivates the cleanup so normal control flow
      // doesn't double-destroy.
      cgf.pushDestroyAndDeferDeactivation(dtorKind, lv.getAddress(),
                                          field->getType());
    }

    // From classic codegen, maybe not useful for CIR:
    // If the GEP didn't get used because of a dead zero init or something
    // else, clean it up for -O0 builds and general tidiness.
  }

  // OGCG calls DoZeroInitPadding here for struct fields when
  // shouldZeroInitPadding() is true. Same guard as the union branch above.
  if (zeroInitPadding) {
    uint64_t totalSize = cgf.getContext().toBits(
        dest.getPreferredSize(cgf.getContext(), destLV.getType()));
    doZeroInitPadding(paddingStart, totalSize, nullptr);
  }
}

/// Zero-initialize padding bytes between PaddingStart and PaddingEnd.
/// For bitfield NextField, zeros the storage region instead.
/// PaddingStart is updated to account for the NextField's size.
/// Matches OGCG CGExprAgg.cpp DoZeroInitPadding.
void AggExprEmitter::doZeroInitPadding(uint64_t &paddingStart,
                                       uint64_t paddingEnd,
                                       const FieldDecl *nextField) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = dest.getAddress().getPointer().getLoc();

  auto initBytes = [&](uint64_t startBit, uint64_t endBit) {
    CharUnits start = cgf.getContext().toCharUnitsFromBits(startBit);
    CharUnits end = cgf.getContext().toCharUnitsFromBits(endBit);
    CharUnits size = end - start;
    if (size.isZero())
      return;
    // Cast dest to i8*, stride to the offset, then cast to void* for memset.
    auto i8Ty = builder.getUIntNTy(8);
    auto i8PtrTy = cir::PointerType::get(i8Ty);
    mlir::Value addr =
        builder.createBitcast(loc, dest.getAddress().getPointer(), i8PtrTy);
    if (!start.isZero()) {
      mlir::Value offset = builder.getConstInt(
          loc, mlir::cast<cir::IntType>(builder.getSIntNTy(64)),
          start.getQuantity());
      addr = builder.createPtrStride(loc, addr, offset);
    }
    mlir::Value voidPtr =
        builder.createBitcast(loc, addr, builder.getVoidPtrTy());
    auto zeroVal = builder.getConstantInt(
        loc, cir::IntType::get(builder.getContext(), 8, false), 0);
    auto intCastVal = builder.createIntCast(
        zeroVal, cir::IntType::get(builder.getContext(), 32, true));
    auto sizeTy = mlir::cast<cir::IntType>(
        cgf.convertType(cgf.getContext().getSizeType()));
    auto sizeVal =
        builder.getConstantInt(loc, sizeTy, size.getQuantity());
    cir::MemSetOp::create(builder, loc, voidPtr, intCastVal, sizeVal);
  };

  if (nextField != nullptr && nextField->isBitField()) {
    // For bitfields, zero-init the storage region before storing the bits.
    // This avoids needing big/little endian handling.
    const CIRGenRecordLayout &rl =
        cgf.getTypes().getCIRGenRecordLayout(nextField->getParent());
    const CIRGenBitFieldInfo &info = rl.getBitFieldInfo(nextField);
    uint64_t storageStart = cgf.getContext().toBits(info.storageOffset);
    if (storageStart + info.storageSize > paddingStart) {
      if (storageStart > paddingStart)
        initBytes(paddingStart, storageStart);
      // Zero the entire bitfield storage unit.
      auto i8Ty = builder.getUIntNTy(8);
      auto i8PtrTy = cir::PointerType::get(i8Ty);
      mlir::Value addr =
          builder.createBitcast(loc, dest.getAddress().getPointer(), i8PtrTy);
      if (!info.storageOffset.isZero()) {
        mlir::Value offset = builder.getConstInt(
            loc, mlir::cast<cir::IntType>(builder.getSIntNTy(64)),
            info.storageOffset.getQuantity());
        addr = builder.createPtrStride(loc, addr, offset);
      }
      // Store zero to the storage-sized integer.
      auto storageTy =
          cir::IntType::get(builder.getContext(), info.storageSize, false);
      auto storagePtrTy = cir::PointerType::get(storageTy);
      mlir::Value storagePtr = builder.createBitcast(loc, addr, storagePtrTy);
      mlir::Value zero = builder.getConstantInt(loc, storageTy, 0);
      static_cast<cir::CIRBaseBuilderTy &>(builder).createStore(loc, zero,
                                                               storagePtr);
      paddingStart = storageStart + info.storageSize;
    }
    return;
  }

  if (paddingStart < paddingEnd)
    initBytes(paddingStart, paddingEnd);
  if (nextField != nullptr)
    paddingStart =
        paddingEnd + cgf.getContext().getTypeSize(nextField->getType());
}

// TODO(cir): This could be shared with classic codegen.
AggValueSlot::Overlap_t CIRGenFunction::getOverlapForBaseInit(
    const CXXRecordDecl *rd, const CXXRecordDecl *baseRD, bool isVirtual) {
  // If the most-derived object is a field declared with [[no_unique_address]],
  // the tail padding of any virtual base could be reused for other subobjects
  // of that field's class.
  if (isVirtual)
    return AggValueSlot::MayOverlap;

  // Empty bases can overlap earlier bases.
  if (baseRD->isEmpty())
    return AggValueSlot::MayOverlap;

  // If the base class is laid out entirely within the nvsize of the derived
  // class, its tail padding cannot yet be initialized, so we can issue
  // stores at the full width of the base class.
  const ASTRecordLayout &layout = getContext().getASTRecordLayout(rd);
  if (layout.getBaseClassOffset(baseRD) +
          getContext().getASTRecordLayout(baseRD).getSize() <=
      layout.getNonVirtualSize())
    return AggValueSlot::DoesNotOverlap;

  // The tail padding may contain values we need to preserve.
  return AggValueSlot::MayOverlap;
}

void CIRGenFunction::emitAggExpr(const Expr *e, AggValueSlot slot) {
  checkAggExprForMemSetUse(slot, e, *this);
  AggExprEmitter(*this, slot).Visit(const_cast<Expr *>(e));
}

void CIRGenFunction::emitAggregateCopy(LValue dest, LValue src, QualType ty,
                                       AggValueSlot::Overlap_t mayOverlap,
                                       bool isVolatile) {
  // TODO(cir): this function needs improvements, commented code for now since
  // this will be touched again soon.
  assert(!ty->isAnyComplexType() && "Unexpected copy of complex");

  Address destPtr = dest.getAddress();
  Address srcPtr = src.getAddress();

  if (getLangOpts().CPlusPlus) {
    if (auto *record = ty->getAsCXXRecordDecl()) {
      assert((record->hasTrivialCopyConstructor() ||
              record->hasTrivialCopyAssignment() ||
              record->hasTrivialMoveConstructor() ||
              record->hasTrivialMoveAssignment() ||
              record->hasAttr<TrivialABIAttr>() || record->isUnion()) &&
             "Trying to aggregate-copy a type without a trivial copy/move "
             "constructor or assignment operator");
      // Ignore empty classes in C++.
      if (record->isEmpty())
        return;
    }
  }

  assert(!cir::MissingFeatures::cudaSupport());

  // Aggregate assignment turns into llvm.memcpy.  This is almost valid per
  // C99 6.5.16.1p3, which states "If the value being stored in an object is
  // read from another object that overlaps in anyway the storage of the first
  // object, then the overlap shall be exact and the two objects shall have
  // qualified or unqualified versions of a compatible type."
  //
  // memcpy is not defined if the source and destination pointers are exactly
  // equal, but other compilers do this optimization, and almost every memcpy
  // implementation handles this case safely.  If there is a libc that does not
  // safely handle this, we can add a target hook.

  // Get data size info for this aggregate. Don't copy the tail padding if this
  // might be a potentially-overlapping subobject, since the tail padding might
  // be occupied by a different object. Otherwise, copying it is fine.
  TypeInfoChars typeInfo;
  if (mayOverlap == AggValueSlot::MayOverlap)
    typeInfo = getContext().getTypeInfoDataSizeInChars(ty);
  else
    typeInfo = getContext().getTypeInfoInChars(ty);

  assert(!cir::MissingFeatures::aggValueSlotVolatile());

  // Note: TypeInfo.Width can legitimately be zero for GNU empty-record
  // extensions. Keep the existing copy/memcpy selection behavior for those
  // cases. Variably-modified vararg element types are diagnosed explicitly in
  // ScalarExprEmitter::VisitVAArgExpr.

  // NOTE(cir): original codegen would normally convert destPtr and srcPtr to
  // i8* since memcpy operates on bytes. We don't need that in CIR because
  // cir.copy will operate on any CIR pointer that points to a sized type.

  // OGCG passes the computed TypeInfo size to Builder.CreateMemCpy, using
  // getDataSize() for potentially-overlapping subobjects to avoid clobbering
  // tail padding. When the data size differs from the full type size (due to
  // overlap), we must use MemCpyOp with an explicit byte count instead of
  // CopyOp which always copies the full type.
  CharUnits dataSize = typeInfo.Width;
  CharUnits fullSize = getContext().getTypeInfoInChars(ty).Width;
  bool useDataSize =
      mayOverlap == AggValueSlot::MayOverlap && dataSize != fullSize;

  std::optional<mlir::Location> keyedCopyLoc;
  if (sourceAtomInfo.currentAtom && currSrcLoc)
    keyedCopyLoc = *currSrcLoc;

  if (cgm.getLangOpts().getGC() != LangOptions::NonGC &&
      requiresGCMemmoveCollectable(getContext(), ty)) {
    // Match OGCG: ObjC GC copies of aggregates containing object members go
    // through objc_memmove_collectable instead of raw memcpy/copy.
    auto &builder = getBuilder();
    mlir::Location callLoc =
        keyedCopyLoc ? *keyedCopyLoc : srcPtr.getPointer().getLoc();
    auto voidPtrTy = builder.getVoidPtrTy();
    cir::FuncOp memmoveCollectableFn = cgm.createRuntimeFunction(
        builder.getFuncType({voidPtrTy, voidPtrTy, sizeTy}, voidPtrTy),
        "objc_memmove_collectable");
    mlir::Value dst = destPtr.getPointer();
    mlir::Value src = srcPtr.getPointer();
    if (dst.getType() != voidPtrTy)
      dst = builder.createBitcast(callLoc, dst, voidPtrTy);
    if (src.getType() != voidPtrTy)
      src = builder.createBitcast(callLoc, src, voidPtrTy);
    cir::ConstantOp sizeOp =
        builder.getConstInt(callLoc, sizeTy, dataSize.getQuantity());
    emitNounwindRuntimeCall(callLoc, memmoveCollectableFn, {dst, src, sizeOp});
    return;
  }

  // Skip the copy if the source and destination are the same SSA value,
  // unless the copy is volatile (volatile self-copies are observable side
  // effects, e.g., for MMIO-mapped structs).
  if (destPtr.getPointer() != srcPtr.getPointer() || isVolatile) {
    mlir::Value dst = destPtr.getPointer();
    mlir::Value src = srcPtr.getPointer();

    // Check whether the destination pointer has a void pointee type.
    // cir.copy requires a sized pointee type (to infer the byte count from
    // the data layout), so when the pointer is void* we must fall back to
    // cir.libc.memcpy with an explicit byte count, just like the
    // useDataSize path.
    bool dstPointeeIsVoid = mlir::isa<cir::VoidType>(
        mlir::cast<cir::PointerType>(dst.getType()).getPointee());

    if (useDataSize || dstPointeeIsVoid) {
      // Emit an explicit MemCpyOp. When useDataSize is set we use the data
      // size (to avoid clobbering tail padding for potentially-overlapping
      // subobjects); otherwise we use the full type size.
      CharUnits copySize = useDataSize ? dataSize : fullSize;
      mlir::Location memcpyLoc = keyedCopyLoc ? *keyedCopyLoc : src.getLoc();
      cir::ConstantOp sizeOp =
          builder.getConstInt(memcpyLoc, sizeTy, copySize.getQuantity());
      auto dstPtrTy = mlir::cast<cir::PointerType>(dst.getType());
      auto dstVoidPtrTy = cir::PointerType::get(
          builder.getContext(), cir::VoidType::get(builder.getContext()),
          dstPtrTy.getAddrSpace());
      auto srcPtrTy = mlir::cast<cir::PointerType>(src.getType());
      auto srcVoidPtrTy = cir::PointerType::get(
          builder.getContext(), cir::VoidType::get(builder.getContext()),
          srcPtrTy.getAddrSpace());
      mlir::Value dstVoid = builder.createBitcast(dst, dstVoidPtrTy);
      mlir::Value srcVoid = builder.createBitcast(src, srcVoidPtrTy);
      auto memcpyOp =
          cir::MemCpyOp::create(builder, memcpyLoc, dstVoid, srcVoid, sizeOp);
      addOpToCurrentSourceAtom(memcpyOp);
      if (!destPtr.getAlignment().isZero())
        memcpyOp->setAttr("cir.dst_align",
                          getI64AttrCached(
                               destPtr.getAlignment().getAsAlign().value()));
      if (!srcPtr.getAlignment().isZero())
        memcpyOp->setAttr("cir.src_align",
                          getI64AttrCached(
                               srcPtr.getAlignment().getAsAlign().value()));
      if (auto tbaaStruct = cgm.getTBAAStructInfo(ty)) {
        memcpyOp->setAttr("cir.tbaa_struct", tbaaStruct);
      }
    } else {
      // Ensure both pointers have the same type. This can differ when copying
      // between base subobject types and full types (e.g., B.base vs B with
      // [[no_unique_address]]). cir.copy requires matching pointer types.
      if (dst.getType() != src.getType())
        src = builder.createPtrBitcast(
            src, mlir::cast<cir::PointerType>(dst.getType()).getPointee());

      // Pass the alignment from the Address objects to the copy op, so it
      // can be forwarded to the underlying memcpy during lowering.
      mlir::IntegerAttr dstAlignAttr, srcAlignAttr;
      if (!destPtr.getAlignment().isZero())
        dstAlignAttr = getI64AttrCached(
            destPtr.getAlignment().getAsAlign().value());
      if (!srcPtr.getAlignment().isZero())
        srcAlignAttr = getI64AttrCached(
            srcPtr.getAlignment().getAsAlign().value());

      mlir::Location copyLoc = keyedCopyLoc ? *keyedCopyLoc : dst.getLoc();
      [[maybe_unused]] cir::CopyOp copyOp = cir::CopyOp::create(
          builder, copyLoc, dst, src, isVolatile, dstAlignAttr, srcAlignAttr);
      addOpToCurrentSourceAtom(copyOp);
      if (auto tbaaStruct = cgm.getTBAAStructInfo(ty)) {
        copyOp->setAttr("cir.tbaa_struct", tbaaStruct);
      }
    }
  }
}

// TODO(cir): This could be shared with classic codegen.
AggValueSlot::Overlap_t
CIRGenFunction::getOverlapForFieldInit(const FieldDecl *fd) {
  if (!fd->hasAttr<NoUniqueAddressAttr>() || !fd->getType()->isRecordType())
    return AggValueSlot::DoesNotOverlap;

  // Empty fields can overlap earlier fields.
  if (fd->getType()->getAsCXXRecordDecl()->isEmpty())
    return AggValueSlot::MayOverlap;

  // If the field lies entirely within the enclosing class's nvsize, its tail
  // padding cannot overlap any already-initialized object. (The only subobjects
  // with greater addresses that might already be initialized are vbases.)
  const RecordDecl *classRD = fd->getParent();
  const ASTRecordLayout &layout = getContext().getASTRecordLayout(classRD);
  if (layout.getFieldOffset(fd->getFieldIndex()) +
          getContext().getTypeSize(fd->getType()) <=
      (uint64_t)getContext().toBits(layout.getNonVirtualSize()))
    return AggValueSlot::DoesNotOverlap;

  // The tail padding may contain values we need to preserve.
  return AggValueSlot::MayOverlap;
}

LValue CIRGenFunction::emitAggExprToLValue(const Expr *e) {
  assert(hasAggregateEvaluationKind(e->getType()) && "Invalid argument!");
  Address temp = createMemTemp(e->getType(), getLoc(e->getSourceRange()));
  LValue lv = makeAddrLValue(temp, e->getType());
  emitAggExpr(e, AggValueSlot::forLValue(lv, AggValueSlot::IsNotDestructed,
                                         AggValueSlot::IsNotAliased,
                                         AggValueSlot::DoesNotOverlap));
  return lv;
}

LValue CIRGenFunction::emitInitListLValue(const InitListExpr *e) {
  if (!e->isGLValue())
    // Initializing an aggregate temporary in C++11: T{...}.
    return emitAggExprToLValue(e);

  // An lvalue initializer list must be initializing a reference.
  assert(e->isTransparent() && "non-transparent glvalue init list");
  return emitLValue(e->getInit(0));
}

LValue CIRGenFunction::emitCXXConstructLValue(const CXXConstructExpr *e) {
  assert(e->getType()->getAsCXXRecordDecl()->hasTrivialDestructor() &&
         "binding l-value to type which needs a temporary");
  AggValueSlot slot =
      createAggTemp(e->getType(), getLoc(e->getSourceRange()));
  emitCXXConstructExpr(e, slot);
  return makeAddrLValue(slot.getAddress(), e->getType(),
                        AlignmentSource::Decl);
}

LValue
CIRGenFunction::emitCXXBindTemporaryLValue(const CXXBindTemporaryExpr *e) {
  AggValueSlot slot = createAggTemp(e->getType(), getLoc(e->getSourceRange()),
                                    "temp.lvalue");
  slot.setExternallyDestructed();
  emitAggExpr(e->getSubExpr(), slot);
  emitCXXTemporary(e->getTemporary(), e->getType(), slot.getAddress());
  return makeAddrLValue(slot.getAddress(), e->getType(),
                        AlignmentSource::Decl);
}
