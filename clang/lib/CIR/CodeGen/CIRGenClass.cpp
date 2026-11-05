//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with C++ code generation of classes
//
//===----------------------------------------------------------------------===//

#include "CIRGenCXXABI.h"
#include "CIRGenFunction.h"
#include "CIRGenRecordLayout.h"
#include "CIRGenSanitizer.h"
#include "CIRGenValue.h"
#include "TargetInfo.h"

#include "clang/AST/Attr.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Type.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/NoSanitizeList.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"

using namespace clang;
using namespace clang::CIRGen;

namespace {
struct CIRVTablePointerAuthInfo {
  unsigned key;
  mlir::Value discriminator;
};

static std::optional<PointerAuthQualifier>
getVTablePointerAuthentication(CIRGenModule &cgm,
                               const CXXRecordDecl *record) {
  return cgm.computeVTPointerAuthentication(record);
}

static SanitizerMask getSanitizerKindForCFICastCheck(
    CIRGenFunction::CFICastCheckKind kind) {
  switch (kind) {
  case CIRGenFunction::CFICastCheckKind::DerivedCast:
    return SanitizerKind::CFIDerivedCast;
  case CIRGenFunction::CFICastCheckKind::UnrelatedCast:
    return SanitizerKind::CFIUnrelatedCast;
  }
  llvm_unreachable("unknown CFI cast check kind");
}

static mlir::Value emitPointerAuthBlendDiscriminator(CIRGenFunction &cgf,
                                                     mlir::Location loc,
                                                     mlir::Value storageAddress,
                                                     mlir::Value discriminator) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Value storageAddressInt =
      builder.createPtrToInt(storageAddress, cgf.cgm.uIntPtrTy);
  return cir::LLVMIntrinsicCallOp::create(
             builder, loc, cgf.cgm.getStringAttrCached("ptrauth.blend"),
             cgf.cgm.uIntPtrTy,
             mlir::ValueRange{storageAddressInt, discriminator})
      .getResult();
}

static std::optional<CIRVTablePointerAuthInfo>
getVTablePointerAuthInfo(CIRGenFunction &cgf, mlir::Location loc,
                         const CXXRecordDecl *record, Address storageAddress) {
  std::optional<PointerAuthQualifier> authentication =
      getVTablePointerAuthentication(cgf.cgm, record);
  if (!authentication)
    return std::nullopt;

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Value discriminator;
  if (unsigned extraDiscriminator = authentication->getExtraDiscriminator())
    discriminator =
        builder.getConstInt(loc, cgf.cgm.uIntPtrTy, extraDiscriminator);

  if (authentication->isAddressDiscriminated()) {
    mlir::Value rawStorageAddress = storageAddress.emitRawPointer();
    if (discriminator)
      discriminator = emitPointerAuthBlendDiscriminator(
          cgf, loc, rawStorageAddress, discriminator);
    else
      discriminator = builder.createPtrToInt(rawStorageAddress,
                                             cgf.cgm.uIntPtrTy);
  }

  if (!discriminator)
    discriminator = builder.getConstInt(loc, cgf.cgm.uIntPtrTy, 0);

  return CIRVTablePointerAuthInfo{authentication->getKey(), discriminator};
}

static mlir::Value emitVTablePointerAuthCommon(CIRGenFunction &cgf,
                                               mlir::Location loc,
                                               llvm::StringRef intrinsicName,
                                               mlir::Value pointer,
                                               unsigned key,
                                               mlir::Value discriminator) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Type originalType = pointer.getType();
  auto rawPtrTy = mlir::isa<cir::VPtrType>(originalType)
                      ? cgf.cgm.getDefaultGlobalsInt8PtrTy()
                      : cgf.cgm.uInt8PtrTy;

  mlir::Value pointerAsPtr = pointer;
  if (!mlir::isa<cir::PointerType>(originalType))
    pointerAsPtr = builder.createBitcast(loc, pointer, rawPtrTy);

  mlir::Value pointerInt =
      builder.createPtrToInt(pointerAsPtr, cgf.cgm.uIntPtrTy);
  mlir::Value keyValue = builder.getConstInt(loc, builder.getUInt32Ty(), key);

  if (!discriminator)
    discriminator = builder.getConstInt(loc, cgf.cgm.uIntPtrTy, 0);

  mlir::Value resultInt = cir::LLVMIntrinsicCallOp::create(
                              builder, loc,
                              cgf.cgm.getStringAttrCached(intrinsicName),
                              cgf.cgm.uIntPtrTy,
                              mlir::ValueRange{pointerInt, keyValue,
                                               discriminator})
                              .getResult();

  if (mlir::isa<cir::PointerType>(originalType))
    return builder.createIntToPtr(resultInt, originalType);

  mlir::Value resultPtr = builder.createIntToPtr(resultInt, rawPtrTy);
  return builder.createBitcast(loc, resultPtr, originalType);
}

static mlir::Value emitVTablePointerAuthSign(
    CIRGenFunction &cgf, mlir::Location loc,
    const CIRVTablePointerAuthInfo &info, mlir::Value pointer) {
  return emitVTablePointerAuthCommon(cgf, loc, "ptrauth.sign", pointer,
                                     info.key, info.discriminator);
}

static mlir::Value emitVTablePointerAuthAuth(
    CIRGenFunction &cgf, mlir::Location loc,
    const CIRVTablePointerAuthInfo &info, mlir::Value pointer) {
  return emitVTablePointerAuthCommon(cgf, loc, "ptrauth.auth", pointer,
                                     info.key, info.discriminator);
}

static mlir::Value emitVTablePointerAuthStrip(CIRGenFunction &cgf,
                                              mlir::Location loc,
                                              mlir::Value pointer,
                                              unsigned key) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Type originalType = pointer.getType();
  auto rawPtrTy = mlir::isa<cir::VPtrType>(originalType)
                      ? cgf.cgm.getDefaultGlobalsInt8PtrTy()
                      : cgf.cgm.uInt8PtrTy;

  mlir::Value pointerAsPtr = pointer;
  if (!mlir::isa<cir::PointerType>(originalType))
    pointerAsPtr = builder.createBitcast(loc, pointer, rawPtrTy);

  mlir::Value pointerInt =
      builder.createPtrToInt(pointerAsPtr, cgf.cgm.uIntPtrTy);
  mlir::Value keyValue = builder.getConstInt(loc, builder.getUInt32Ty(), key);
  mlir::Value resultInt = cir::LLVMIntrinsicCallOp::create(
                              builder, loc,
                              cgf.cgm.getStringAttrCached("ptrauth.strip"),
                              cgf.cgm.uIntPtrTy,
                              mlir::ValueRange{pointerInt, keyValue})
                              .getResult();

  if (mlir::isa<cir::PointerType>(originalType))
    return builder.createIntToPtr(resultInt, originalType);

  mlir::Value resultPtr = builder.createIntToPtr(resultInt, rawPtrTy);
  return builder.createBitcast(loc, resultPtr, originalType);
}
} // namespace

/// Checks whether the given constructor is a valid subject for the
/// complete-to-base constructor delegation optimization, i.e. emitting the
/// complete constructor as a simple call to the base constructor.
bool CIRGenFunction::isConstructorDelegationValid(
    const CXXConstructorDecl *ctor) {
  // Currently we disable the optimization for classes with virtual bases
  // because (1) the address of parameter variables need to be consistent across
  // all initializers but (2) the delegate function call necessarily creates a
  // second copy of the parameter variable.
  //
  // The limiting example (purely theoretical AFAIK):
  //   struct A { A(int &c) { c++; } };
  //   struct A : virtual A {
  //     B(int count) : A(count) { printf("%d\n", count); }
  //   };
  // ...although even this example could in principle be emitted as a delegation
  // since the address of the parameter doesn't escape.
  if (ctor->getParent()->getNumVBases())
    return false;

  // We also disable the optimization for variadic functions because it's
  // impossible to "re-pass" varargs.
  if (ctor->getType()->castAs<FunctionProtoType>()->isVariadic())
    return false;

  // FIXME: Decide if we can do a delegation of a delegating constructor.
  if (ctor->isDelegatingConstructor())
    return false;

  return true;
}

static void emitLValueForAnyFieldInitialization(CIRGenFunction &cgf,
                                                CXXCtorInitializer *memberInit,
                                                LValue &lhs) {
  FieldDecl *field = memberInit->getAnyMember();
  if (memberInit->isIndirectMemberInitializer()) {
    // If we are initializing an anonymous union field, drill down to the field.
    IndirectFieldDecl *indirectField = memberInit->getIndirectMember();
    for (const auto *nd : indirectField->chain()) {
      auto *fd = cast<clang::FieldDecl>(nd);
      lhs = cgf.emitLValueForFieldInitialization(lhs, fd, fd->getName());
    }
  } else {
    lhs = cgf.emitLValueForFieldInitialization(lhs, field, field->getName());
  }
}

static void emitMemberInitializer(CIRGenFunction &cgf,
                                  const CXXRecordDecl *classDecl,
                                  CXXCtorInitializer *memberInit,
                                  const CXXConstructorDecl *constructor,
                                  FunctionArgList &args) {
  CIRGenFunction::ApplySourceAtomGroup atomGroup(cgf);
  CIRGenFunction::SourceLocRAIIObject loc{
      cgf, cgf.getLoc(memberInit->getSourceLocation())};
  assert(memberInit->isAnyMemberInitializer() &&
         "Must have member initializer!");
  assert(memberInit->getInit() && "Must have initializer!");

  // non-static data member initializers
  FieldDecl *field = memberInit->getAnyMember();
  QualType fieldType = field->getType();

  mlir::Value thisPtr = cgf.loadCXXThis();
  CanQualType recordTy = cgf.getContext().getCanonicalTagType(classDecl);

  // If a base constructor is being emitted, create an LValue that has the
  // non-virtual alignment.
  LValue lhs = (cgf.curGD.getCtorType() == Ctor_Base)
                   ? cgf.makeNaturalAlignPointeeAddrLValue(thisPtr, recordTy)
                   : cgf.makeNaturalAlignAddrLValue(thisPtr, recordTy);

  emitLValueForAnyFieldInitialization(cgf, memberInit, lhs);

  // Special case: If we are in a copy or move constructor, and we are copying
  // an array off PODs or classes with trivial copy constructors, ignore the AST
  // and perform the copy we know is equivalent.
  // FIXME: This is hacky at best... if we had a bit more explicit information
  // in the AST, we could generalize it more easily.
  const ConstantArrayType *array =
      cgf.getContext().getAsConstantArrayType(fieldType);
  if (array && constructor->isDefaulted() &&
      constructor->isCopyOrMoveConstructor()) {
    QualType baseElementTy = cgf.getContext().getBaseElementType(array);
    // NOTE(cir): CodeGen allows record types to be memcpy'd if applicable,
    // whereas ClangIR wants to represent all object construction explicitly.
    if (!baseElementTy->isRecordType()) {
      unsigned srcArgIndex =
          cgf.cgm.getCXXABI().getSrcArgforCopyCtor(constructor, args);
      cir::LoadOp srcPtr = cgf.getBuilder().createLoad(
          cgf.getLoc(memberInit->getSourceLocation()),
          cgf.getAddrOfLocalVar(args[srcArgIndex]));
      LValue thisRhslv = cgf.makeNaturalAlignAddrLValue(srcPtr, recordTy);
      LValue src = cgf.emitLValueForFieldInitialization(thisRhslv, field,
                                                        field->getName());

      // Copy the aggregate.
      cgf.emitAggregateCopy(lhs, src, fieldType,
                            cgf.getOverlapForFieldInit(field),
                            lhs.isVolatileQualified());
      // Ensure that we destroy the objects if an exception is thrown later in
      // the constructor.
      QualType::DestructionKind dtorKind = fieldType.isDestructedType();
      if (cgf.needsEHCleanup(dtorKind))
        cgf.pushEHDestroy(dtorKind, lhs.getAddress(), fieldType);
      return;
    }
  }

  cgf.emitInitializerForField(field, lhs, memberInit->getInit());
}

static bool isMemcpyEquivalentSpecialMember(const CXXMethodDecl *d);

namespace {
/// RAII object to indicate that codegen is copying the value representation
/// instead of the object representation. Useful when copying a struct or
/// class which has uninitialized members and we're only performing
/// lvalue-to-rvalue conversion on the object but not its members.
class CopyingValueRepresentation {
public:
  explicit CopyingValueRepresentation(CIRGenFunction &cgf)
      : cgf(cgf), oldSanOpts(cgf.sanOpts) {
    cgf.sanOpts.set(SanitizerKind::Bool, false);
    cgf.sanOpts.set(SanitizerKind::Enum, false);
  }
  ~CopyingValueRepresentation() { cgf.sanOpts = oldSanOpts; }

private:
  CIRGenFunction &cgf;
  SanitizerSet oldSanOpts;
};

class FieldMemcpyizer {
public:
  FieldMemcpyizer(CIRGenFunction &cgf, const CXXMethodDecl *methodDecl,
                  const VarDecl *srcRec)
      : cgf(cgf), methodDecl(methodDecl),
        classDecl(methodDecl->getParent()), srcRec(srcRec),
        recLayout(cgf.getContext().getASTRecordLayout(classDecl)),
        firstField(nullptr), lastField(nullptr), firstFieldOffset(0),
        lastFieldOffset(0), lastAddedFieldIndex(0) {}

  bool isMemcpyableField(FieldDecl *f) const {
    // Never memcpy fields when we are adding poisoned paddings.
    if (cgf.getContext().getLangOpts().SanitizeAddressFieldPadding)
      return false;
    Qualifiers qual = f->getType().getQualifiers();
    if (qual.hasVolatile() || qual.hasObjCLifetime())
      return false;
    if (PointerAuthQualifier ptrAuth =
            qual.getPointerAuth().withoutKeyNone()) {
      if (ptrAuth.isAddressDiscriminated())
        return false;
    }
    return true;
  }

  void addMemcpyableField(FieldDecl *f) {
    if (isEmptyFieldForLayout(cgf.getContext(), f))
      return;
    if (!firstField)
      addInitialField(f);
    else
      addNextField(f);
  }

  CharUnits getMemcpySize(uint64_t firstByteOffset) const {
    ASTContext &astContext = cgf.getContext();
    unsigned lastFieldSize =
        lastField->isBitField()
            ? lastField->getBitWidthValue()
            : astContext.toBits(
                  astContext.getTypeInfoDataSizeInChars(lastField->getType())
                      .Width);
    uint64_t memcpySizeBits = lastFieldOffset + lastFieldSize -
                              firstByteOffset + astContext.getCharWidth() - 1;
    CharUnits memcpySize = astContext.toCharUnitsFromBits(memcpySizeBits);
    return memcpySize;
  }

  void emitMemcpy() {
    // Give the subclass a chance to bail out if it feels the memcpy isn't worth
    // it (e.g. hasn't aggregated enough data).
    if (!firstField) {
      return;
    }

    uint64_t firstByteOffset;
    if (firstField->isBitField()) {
      const CIRGenRecordLayout &rl =
          cgf.getTypes().getCIRGenRecordLayout(firstField->getParent());
      const CIRGenBitFieldInfo &bfInfo = rl.getBitFieldInfo(firstField);
      // firstFieldOffset is not appropriate for bitfields,
      // we need to use the storage offset instead.
      firstByteOffset = cgf.getContext().toBits(bfInfo.storageOffset);
    } else {
      firstByteOffset = firstFieldOffset;
    }

    CharUnits memcpySize = getMemcpySize(firstByteOffset);
    CanQualType recordTy = cgf.getContext().getCanonicalTagType(classDecl);
    Address thisPtr = cgf.loadCXXThisAddress();
    LValue destLv = cgf.makeAddrLValue(thisPtr, recordTy);
    LValue dest = cgf.emitLValueForFieldInitialization(destLv, firstField,
                                                       firstField->getName());
    cir::LoadOp srcPtr = cgf.getBuilder().createLoad(
        cgf.getLoc(methodDecl->getLocation()),
        cgf.getAddrOfLocalVar(srcRec));
    LValue srcLv = cgf.makeNaturalAlignAddrLValue(srcPtr, recordTy);
    LValue src = cgf.emitLValueForFieldInitialization(srcLv, firstField,
                                                      firstField->getName());

    emitMemcpyIR(dest.isBitField() ? dest.getBitFieldAddress()
                                   : dest.getAddress(),
                 src.isBitField() ? src.getBitFieldAddress() : src.getAddress(),
                 memcpySize);
    reset();
  }

  void reset() { firstField = nullptr; }

protected:
  CIRGenFunction &cgf;
  const CXXMethodDecl *methodDecl;
  const CXXRecordDecl *classDecl;

private:
  void emitMemcpyIR(Address destPtr, Address srcPtr, CharUnits size) {
    mlir::Location loc = cgf.getLoc(methodDecl->getLocation());
    cir::ConstantOp sizeOp =
        cgf.getBuilder().getConstInt(loc, cgf.sizeTy, size.getQuantity());
    auto destPtrTy =
        mlir::cast<cir::PointerType>(destPtr.getPointer().getType());
    auto destVoidPtrTy = cir::PointerType::get(
        cgf.getBuilder().getContext(), cir::VoidType::get(cgf.getBuilder().getContext()),
        destPtrTy.getAddrSpace());
    auto srcPtrTy =
        mlir::cast<cir::PointerType>(srcPtr.getPointer().getType());
    auto srcVoidPtrTy = cir::PointerType::get(
        cgf.getBuilder().getContext(), cir::VoidType::get(cgf.getBuilder().getContext()),
        srcPtrTy.getAddrSpace());
    mlir::Value dest =
        cgf.getBuilder().createBitcast(destPtr.getPointer(), destVoidPtrTy);
    mlir::Value src =
        cgf.getBuilder().createBitcast(srcPtr.getPointer(), srcVoidPtrTy);
    auto memcpyOp =
        cir::MemCpyOp::create(cgf.getBuilder(), loc, dest, src, sizeOp);
    cgf.addOpToCurrentSourceAtom(memcpyOp);
    // Forward pointer alignment as discardable attrs for LowerToLLVM.
    // OGCG passes Address objects (with alignment) to Builder.CreateMemCpy;
    // CIR attaches alignment as attrs that the lowering reads.
    if (unsigned dstAlign = destPtr.getAlignment().getQuantity())
      memcpyOp->setAttr("cir.dst_align",
                        cgf.getI64AttrCached(dstAlign));
    if (unsigned srcAlign = srcPtr.getAlignment().getQuantity())
      memcpyOp->setAttr("cir.src_align",
                        cgf.getI64AttrCached(srcAlign));
  }

  void addInitialField(FieldDecl *f) {
    firstField = f;
    lastField = f;
    firstFieldOffset = recLayout.getFieldOffset(f->getFieldIndex());
    lastFieldOffset = firstFieldOffset;
    lastAddedFieldIndex = f->getFieldIndex();
  }

  void addNextField(FieldDecl *f) {
    // For the most part, the following invariant will hold:
    //   f->getFieldIndex() == lastAddedFieldIndex + 1
    // The one exception is that Sema won't add a copy-initializer for an
    // unnamed bitfield, which will show up here as a gap in the sequence.
    assert(f->getFieldIndex() >= lastAddedFieldIndex + 1 &&
           "Cannot aggregate fields out of order.");
    lastAddedFieldIndex = f->getFieldIndex();

    // The 'first' and 'last' fields are chosen by offset, rather than field
    // index. This allows the code to support bitfields, as well as regular
    // fields.
    uint64_t fOffset = recLayout.getFieldOffset(f->getFieldIndex());
    if (fOffset < firstFieldOffset) {
      firstField = f;
      firstFieldOffset = fOffset;
    } else if (fOffset >= lastFieldOffset) {
      lastField = f;
      lastFieldOffset = fOffset;
    }
  }

  const VarDecl *srcRec;
  const ASTRecordLayout &recLayout;
  FieldDecl *firstField;
  FieldDecl *lastField;
  uint64_t firstFieldOffset, lastFieldOffset;
  unsigned lastAddedFieldIndex;
};

class ConstructorMemcpyizer : public FieldMemcpyizer {
private:
  /// Get source argument for copy constructor. Returns null if not a copy
  /// constructor.
  static const VarDecl *getTrivialCopySource(CIRGenFunction &cgf,
                                             const CXXConstructorDecl *cd,
                                             FunctionArgList &args) {
    if (cd->isCopyOrMoveConstructor() && cd->isDefaulted())
      return args[cgf.cgm.getCXXABI().getSrcArgforCopyCtor(cd, args)];

    return nullptr;
  }

  // Returns true if a CXXCtorInitializer represents a member initialization
  // that can be rolled into a memcpy.
  bool isMemberInitMemcpyable(CXXCtorInitializer *memberInit) const {
    if (!memcpyableCtor)
      return false;
    FieldDecl *field = memberInit->getMember();
    assert(field && "No field for member init.");
    QualType fieldType = field->getType();
    CXXConstructExpr *ce = dyn_cast<CXXConstructExpr>(memberInit->getInit());

    // Bail out on non-memcpyable, not-trivially-copyable members.
    if (!(ce && isMemcpyEquivalentSpecialMember(ce->getConstructor())) &&
        !(fieldType.isTriviallyCopyableType(cgf.getContext()) ||
          fieldType->isReferenceType()))
      return false;

    // Bail out on volatile fields.
    if (!isMemcpyableField(field))
      return false;

    // Otherwise we're good.
    return true;
  }

public:
  ConstructorMemcpyizer(CIRGenFunction &cgf, const CXXConstructorDecl *cd,
                        FunctionArgList &args)
      : FieldMemcpyizer(cgf, cd, getTrivialCopySource(cgf, cd, args)),
        constructorDecl(cd),
        memcpyableCtor(cd->isDefaulted() && cd->isCopyOrMoveConstructor() &&
                       cgf.getLangOpts().getGC() == LangOptions::NonGC),
        args(args) {}

  void addMemberInitializer(CXXCtorInitializer *memberInit) {
    if (isMemberInitMemcpyable(memberInit)) {
      aggregatedInits.push_back(memberInit);
      addMemcpyableField(memberInit->getMember());
    } else {
      emitAggregatedInits();
      emitMemberInitializer(cgf, constructorDecl->getParent(), memberInit,
                            constructorDecl, args);
    }
  }

  void emitAggregatedInits() {
    if (aggregatedInits.size() <= 1) {
      // This memcpy is too small to be worthwhile. Fall back on default
      // codegen.
      if (!aggregatedInits.empty()) {
        CopyingValueRepresentation cvr(cgf);
        emitMemberInitializer(cgf, constructorDecl->getParent(),
                              aggregatedInits[0], constructorDecl, args);
        aggregatedInits.clear();
      }
      reset();
      return;
    }

    pushEHDestructors();
    CIRGenFunction::ApplySourceAtomGroup atomGroup(cgf);
    emitMemcpy();
    aggregatedInits.clear();
  }

  void pushEHDestructors() {
    CanQualType recordTy = cgf.getContext().getCanonicalTagType(classDecl);
    LValue lhs = cgf.makeAddrLValue(cgf.loadCXXThisAddress(), recordTy);

    for (unsigned i = 0; i < aggregatedInits.size(); ++i) {
      CXXCtorInitializer *memberInit = aggregatedInits[i];
      QualType fieldType = memberInit->getAnyMember()->getType();
      QualType::DestructionKind dtorKind = fieldType.isDestructedType();
      if (!cgf.needsEHCleanup(dtorKind))
        continue;
      LValue fieldLhs = lhs;
      emitLValueForAnyFieldInitialization(cgf, memberInit, fieldLhs);
      cgf.pushEHDestroy(dtorKind, fieldLhs.getAddress(), fieldType);
    }
  }

  void finish() { emitAggregatedInits(); }

private:
  const CXXConstructorDecl *constructorDecl;
  bool memcpyableCtor;
  FunctionArgList &args;
  SmallVector<CXXCtorInitializer *, 16> aggregatedInits;
};

class AssignmentMemcpyizer : public FieldMemcpyizer {
private:
  FieldDecl *getMemcpyableField(Stmt *stmt) {
    if (!assignmentsMemcpyable)
      return nullptr;

    if (auto *bo = dyn_cast<BinaryOperator>(stmt)) {
      if (bo->getOpcode() != BO_Assign)
        return nullptr;
      auto *lhs = dyn_cast<MemberExpr>(bo->getLHS());
      if (!lhs)
        return nullptr;
      auto *field = dyn_cast<FieldDecl>(lhs->getMemberDecl());
      if (!field || !isMemcpyableField(field))
        return nullptr;
      Stmt *rhs = bo->getRHS();
      if (auto *cast = dyn_cast<ImplicitCastExpr>(rhs))
        rhs = cast->getSubExpr();
      if (!rhs)
        return nullptr;
      if (auto *rhsMember = dyn_cast<MemberExpr>(rhs))
        return rhsMember->getMemberDecl() == field ? field : nullptr;
      return nullptr;
    }

    if (auto *mce = dyn_cast<CXXMemberCallExpr>(stmt)) {
      auto *method = dyn_cast<CXXMethodDecl>(mce->getCalleeDecl());
      if (!(method && isMemcpyEquivalentSpecialMember(method)))
        return nullptr;
      auto *objectArg = dyn_cast<MemberExpr>(mce->getImplicitObjectArgument());
      if (!objectArg)
        return nullptr;
      auto *field = dyn_cast<FieldDecl>(objectArg->getMemberDecl());
      if (!field || !isMemcpyableField(field))
        return nullptr;
      auto *arg0 = dyn_cast<MemberExpr>(mce->getArg(0));
      if (!arg0 || field != dyn_cast<FieldDecl>(arg0->getMemberDecl()))
        return nullptr;
      return field;
    }

    if (auto *ce = dyn_cast<CallExpr>(stmt)) {
      auto *callee = dyn_cast<FunctionDecl>(ce->getCalleeDecl());
      if (!callee || callee->getBuiltinID() != Builtin::BI__builtin_memcpy)
        return nullptr;
      Expr *dstPtr = ce->getArg(0);
      if (auto *cast = dyn_cast<ImplicitCastExpr>(dstPtr))
        dstPtr = cast->getSubExpr();
      auto *dstAddrOf = dyn_cast<UnaryOperator>(dstPtr);
      if (!dstAddrOf || dstAddrOf->getOpcode() != UO_AddrOf)
        return nullptr;
      auto *dstMember = dyn_cast<MemberExpr>(dstAddrOf->getSubExpr());
      if (!dstMember)
        return nullptr;
      auto *field = dyn_cast<FieldDecl>(dstMember->getMemberDecl());
      if (!field || !isMemcpyableField(field))
        return nullptr;
      Expr *srcPtr = ce->getArg(1);
      if (auto *cast = dyn_cast<ImplicitCastExpr>(srcPtr))
        srcPtr = cast->getSubExpr();
      auto *srcAddrOf = dyn_cast<UnaryOperator>(srcPtr);
      if (!srcAddrOf || srcAddrOf->getOpcode() != UO_AddrOf)
        return nullptr;
      auto *srcMember = dyn_cast<MemberExpr>(srcAddrOf->getSubExpr());
      if (!srcMember || field != dyn_cast<FieldDecl>(srcMember->getMemberDecl()))
        return nullptr;
      return field;
    }

    return nullptr;
  }

  bool assignmentsMemcpyable;
  SmallVector<Stmt *, 16> aggregatedStmts;

public:
  AssignmentMemcpyizer(CIRGenFunction &cgf, const CXXMethodDecl *assignOp,
                       FunctionArgList &args)
      : FieldMemcpyizer(cgf, assignOp, args.back()),
        assignmentsMemcpyable(cgf.getLangOpts().getGC() ==
                              LangOptions::NonGC) {
    assert(args.size() == 2);
  }

  mlir::LogicalResult emitAssignment(Stmt *stmt) {
    if (FieldDecl *field = getMemcpyableField(stmt)) {
      addMemcpyableField(field);
      aggregatedStmts.push_back(stmt);
      return mlir::success();
    }

    if (mlir::failed(emitAggregatedStmts()))
      return mlir::failure();
    return cgf.emitStmt(stmt, /*useCurrentScope=*/true);
  }

  mlir::LogicalResult emitAggregatedStmts() {
    if (aggregatedStmts.size() <= 1) {
      if (!aggregatedStmts.empty()) {
        CopyingValueRepresentation cvr(cgf);
        if (mlir::failed(cgf.emitStmt(aggregatedStmts[0],
                                      /*useCurrentScope=*/true)))
          return mlir::failure();
      }
      reset();
      aggregatedStmts.clear();
      return mlir::success();
    }

    CIRGenFunction::ApplySourceAtomGroup atomGroup(cgf);
    emitMemcpy();
    aggregatedStmts.clear();
    return mlir::success();
  }

  mlir::LogicalResult finish() { return emitAggregatedStmts(); }
};
} // namespace

static bool isInitializerOfDynamicClass(const CXXCtorInitializer *baseInit) {
  const Type *baseType = baseInit->getBaseClass();
  const auto *baseClassDecl = baseType->castAsCXXRecordDecl();
  return baseClassDecl->isDynamicClass();
}

namespace {
/// Call the destructor for a direct base class.
struct CallBaseDtor final : EHScopeStack::Cleanup {
  const CXXRecordDecl *baseClass;
  bool baseIsVirtual;
  CallBaseDtor(const CXXRecordDecl *base, bool baseIsVirtual)
      : baseClass(base), baseIsVirtual(baseIsVirtual) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    const CXXRecordDecl *derivedClass =
        cast<CXXMethodDecl>(cgf.curCodeDecl)->getParent();

    const CXXDestructorDecl *d = baseClass->getDestructor();
    // We are already inside a destructor, so presumably the object being
    // destroyed should have the expected type.
    QualType thisTy = d->getFunctionObjectParameterType();
    assert(cgf.currSrcLoc && "expected source location");
    Address addr = cgf.getAddressOfDirectBaseInCompleteClass(
        *cgf.currSrcLoc, cgf.loadCXXThisAddress(), derivedClass, baseClass,
        baseIsVirtual);
    cgf.emitCXXDestructorCall(d, Dtor_Base, baseIsVirtual,
                              /*delegating=*/false, addr, thisTy);
  }
};

/// A visitor which checks whether an initializer uses 'this' in a
/// way which requires the vtable to be properly set.
struct DynamicThisUseChecker
    : ConstEvaluatedExprVisitor<DynamicThisUseChecker> {
  using super = ConstEvaluatedExprVisitor<DynamicThisUseChecker>;

  bool usesThis = false;

  DynamicThisUseChecker(const ASTContext &c) : super(c) {}

  // Black-list all explicit and implicit references to 'this'.
  //
  // Do we need to worry about external references to 'this' derived
  // from arbitrary code? If so, then anything which runs arbitrary
  // external code might potentially access the vtable.
  void VisitCXXThisExpr(const CXXThisExpr *e) { usesThis = true; }
};
} // end anonymous namespace

static bool baseInitializerUsesThis(ASTContext &c, const Expr *init) {
  DynamicThisUseChecker checker(c);
  checker.Visit(init);
  return checker.usesThis;
}

/// Gets the address of a direct base class within a complete object.
/// This should only be used for (1) non-virtual bases or (2) virtual bases
/// when the type is known to be complete (e.g. in complete destructors).
///
/// The object pointed to by 'thisAddr' is assumed to be non-null.
Address CIRGenFunction::getAddressOfDirectBaseInCompleteClass(
    mlir::Location loc, Address thisAddr, const CXXRecordDecl *derived,
    const CXXRecordDecl *base, bool baseIsVirtual) {
  // 'thisAddr' must be a pointer (in some address space) to Derived.
  assert(thisAddr.getElementType() == convertType(derived));

  // Compute the offset of the virtual base.
  CharUnits offset;
  const ASTRecordLayout &layout = getContext().getASTRecordLayout(derived);
  if (baseIsVirtual)
    offset = layout.getVBaseClassOffset(base);
  else
    offset = layout.getBaseClassOffset(base);

  return builder.createBaseClassAddr(loc, thisAddr, convertType(base),
                                     offset.getQuantity(),
                                     /*assumeNotNull=*/true);
}

void CIRGenFunction::emitBaseInitializer(mlir::Location loc,
                                         const CXXRecordDecl *classDecl,
                                         CXXCtorInitializer *baseInit) {
  assert(curFuncDecl && "loading 'this' without a func declaration?");
  assert(isa<CXXMethodDecl>(curFuncDecl));

  assert(baseInit->isBaseInitializer() && "Must have base initializer!");

  Address thisPtr = loadCXXThisAddress();

  const Type *baseType = baseInit->getBaseClass();
  const auto *baseClassDecl = baseType->castAsCXXRecordDecl();

  bool isBaseVirtual = baseInit->isBaseVirtual();

  // If the initializer for the base (other than the constructor
  // itself) accesses 'this' in any way, we need to initialize the
  // vtables.
  if (baseInitializerUsesThis(getContext(), baseInit->getInit()))
    initializeVTablePointers(loc, classDecl);

  // We can pretend to be a complete class because it only matters for
  // virtual bases, and we only do virtual bases for complete ctors.
  Address v = getAddressOfDirectBaseInCompleteClass(
      loc, thisPtr, classDecl, baseClassDecl, isBaseVirtual);
  AggValueSlot aggSlot = AggValueSlot::forAddr(
      v, Qualifiers(), AggValueSlot::IsDestructed, AggValueSlot::IsNotAliased,
      getOverlapForBaseInit(classDecl, baseClassDecl, isBaseVirtual),
      AggValueSlot::IsNotZeroed, AggValueSlot::IsSanitizerChecked);

  emitTypeCheck(TCK_ConstructorCall, baseInit->getSourceLocation(),
                v.getPointer(), getContext().getCanonicalTagType(baseClassDecl));
  emitAggExpr(baseInit->getInit(), aggSlot);

  if (getLangOpts().Exceptions && !baseClassDecl->hasTrivialDestructor())
    ehStack.pushCleanup<CallBaseDtor>(EHCleanup, baseClassDecl, isBaseVirtual);
}

/// This routine generates necessary code to initialize base classes and
/// non-static data members belonging to this constructor.
void CIRGenFunction::emitCtorPrologue(const CXXConstructorDecl *cd,
                                      CXXCtorType ctorType,
                                      FunctionArgList &args) {
  if (cd->isDelegatingConstructor()) {
    emitDelegatingCXXConstructorCall(cd, args);
    return;
  }

  const CXXRecordDecl *classDecl = cd->getParent();

  // Virtual base initializers aren't needed if:
  // - This is a base ctor variant
  // - There are no vbases
  // - The class is abstract, so a complete object of it cannot be constructed
  //
  // The check for an abstract class is necessary because sema may not have
  // marked virtual base destructors referenced.
  bool constructVBases = ctorType != Ctor_Base &&
                         classDecl->getNumVBases() != 0 &&
                         !classDecl->isAbstract();
  if (constructVBases &&
      !cgm.getTarget().getCXXABI().hasConstructorVariants()) {
    cgm.errorNYI(cd->getSourceRange(),
                 "emitCtorPrologue with virtual base without variants");
    return;
  }

  // Create three separate ranges for the different types of initializers.
  auto allInits = cd->inits();

  // Find the boundaries between the three groups.
  auto virtualBaseEnd = std::find_if(
      allInits.begin(), allInits.end(), [](const CXXCtorInitializer *Init) {
        return !(Init->isBaseInitializer() && Init->isBaseVirtual());
      });

  auto nonVirtualBaseEnd = std::find_if(virtualBaseEnd, allInits.end(),
                                        [](const CXXCtorInitializer *Init) {
                                          return !Init->isBaseInitializer();
                                        });

  // Create the three ranges.
  auto virtualBaseInits = llvm::make_range(allInits.begin(), virtualBaseEnd);
  auto nonVirtualBaseInits =
      llvm::make_range(virtualBaseEnd, nonVirtualBaseEnd);
  auto memberInits = llvm::make_range(nonVirtualBaseEnd, allInits.end());

  auto emitInitializer = [&](CXXCtorInitializer *baseInit) {
    // OGCG uses SaveAndRestore per base initializer so that any
    // strict-vtable-pointer launder of cxxThisValue is scoped to
    // that single initializer.
    const mlir::Value oldThisValue = cxxThisValue;
    if (cgm.getCodeGenOpts().StrictVTablePointers &&
        cgm.getCodeGenOpts().OptimizationLevel > 0 &&
        isInitializerOfDynamicClass(baseInit)) {
      // Insert the llvm.launder.invariant.group intrinsic before base
      // initialization to cancel previous vtable assumptions.
      cxxThisValue = cir::InvariantGroupOp::create(
          builder, getLoc(cd->getBeginLoc()), loadCXXThis());
    }
    emitBaseInitializer(getLoc(cd->getBeginLoc()), classDecl, baseInit);
    cxxThisValue = oldThisValue;
  };

  // Process virtual base initializers.
  for (CXXCtorInitializer *virtualBaseInit : virtualBaseInits) {
    if (!constructVBases)
      continue;
    emitInitializer(virtualBaseInit);
  }

  // OGCG has MS ABI-specific VBPtr initialization here. CIR uses Itanium ABI.

  // Then, non-virtual base initializers.
  for (CXXCtorInitializer *nonVirtualBaseInit : nonVirtualBaseInits) {
    assert(!nonVirtualBaseInit->isBaseVirtual());
    emitInitializer(nonVirtualBaseInit);
  }

  initializeVTablePointers(getLoc(cd->getBeginLoc()), classDecl);

  // Finally, initialize class members.
  FieldConstructionScope fcs(*this, loadCXXThisAddress());
  ConstructorMemcpyizer cm(*this, cd, args);
  for (CXXCtorInitializer *member : memberInits) {
    assert(!member->isBaseInitializer());
    assert(member->isAnyMemberInitializer() &&
           "Delegating initializer on non-delegating constructor");
    cm.addMemberInitializer(member);
  }
  cm.finish();
}

static Address applyNonVirtualAndVirtualOffset(
    mlir::Location loc, CIRGenFunction &cgf, Address addr,
    CharUnits nonVirtualOffset, mlir::Value virtualOffset,
    const CXXRecordDecl *derivedClass, const CXXRecordDecl *nearestVBase,
    mlir::Type baseValueTy = {}, bool assumeNotNull = true) {
  // Assert that we have something to do.
  assert(!nonVirtualOffset.isZero() || virtualOffset != nullptr);

  // Compute the offset from the static and dynamic components.
  mlir::Value baseOffset;
  if (!nonVirtualOffset.isZero()) {
    if (virtualOffset) {
      // Combine the non-virtual and virtual offsets by adding them.
      auto &builder = cgf.getBuilder();
      auto offsetTy = virtualOffset.getType();
      mlir::Value nvOffset = builder.getConstant(
          loc, cir::IntAttr::get(offsetTy, nonVirtualOffset.getQuantity()));
      baseOffset = cir::BinOp::create(
          builder, loc, offsetTy, cir::BinOpKind::Add, virtualOffset, nvOffset,
          {}, {}, {}, {}, {}, {});
    } else {
      assert(baseValueTy && "expected base type");
      // If no virtualOffset is present this is the final stop.
      return cgf.getBuilder().createBaseClassAddr(
          loc, addr, baseValueTy, nonVirtualOffset.getQuantity(),
          assumeNotNull);
    }
  } else {
    baseOffset = virtualOffset;
  }

  // Apply the base offset.  cir.ptr_stride adjusts by a number of elements,
  // not bytes.  So the pointer must be cast to a byte pointer and back.

  mlir::Value ptr = addr.getPointer();
  auto ptrTy = mlir::cast<cir::PointerType>(ptr.getType());
  mlir::Type charPtrType =
      cgf.getBuilder().getPointerTo(cgf.getBuilder().getUInt8Ty(),
                                    ptrTy.getAddrSpace());
  mlir::Value charPtr = cgf.getBuilder().createBitcast(ptr, charPtrType);
  mlir::Value adjusted = cir::PtrStrideOp::create(
      cgf.getBuilder(), loc, charPtrType, charPtr, baseOffset,
      cir::GEPNoWrapFlags::inbounds);
  ptr = cgf.getBuilder().createBitcast(adjusted, ptr.getType());

  // If we have a virtual component, the alignment of the result will
  // be relative only to the known alignment of that vbase.
  CharUnits alignment;
  if (virtualOffset) {
    assert(nearestVBase && "virtual offset without vbase?");
    alignment = cgf.cgm.getVBaseAlignment(addr.getAlignment(), derivedClass,
                                          nearestVBase);
  } else {
    alignment = addr.getAlignment();
  }
  alignment = alignment.alignmentAtOffset(nonVirtualOffset);

  return Address(ptr, alignment);
}

void CIRGenFunction::initializeVTablePointer(mlir::Location loc,
                                             const VPtr &vptr) {
  // Compute the address point.
  mlir::Value vtableAddressPoint =
      cgm.getCXXABI().getVTableAddressPointInStructor(
          *this, vptr.vtableClass, vptr.base, vptr.nearestVBase);

  if (!vtableAddressPoint)
    return;

  // Compute where to store the address point.
  mlir::Value virtualOffset{};
  CharUnits nonVirtualOffset = CharUnits::Zero();

  mlir::Type baseValueTy;
  if (cgm.getCXXABI().isVirtualOffsetNeededForVTableField(*this, vptr)) {
    // We need to use the virtual base offset offset because the virtual base
    // might have a different offset in the most derived class.
    virtualOffset = cgm.getCXXABI().getVirtualBaseClassOffset(
        loc, *this, loadCXXThisAddress(), vptr.vtableClass, vptr.nearestVBase);
    nonVirtualOffset = vptr.offsetFromNearestVBase;
  } else {
    // We can just use the base offset in the complete class.
    nonVirtualOffset = vptr.base.getBaseOffset();
    baseValueTy =
        convertType(getContext().getCanonicalTagType(vptr.base.getBase()));
  }

  // Apply the offsets.
  Address classAddr = loadCXXThisAddress();
  if (!nonVirtualOffset.isZero() || virtualOffset) {
    classAddr = applyNonVirtualAndVirtualOffset(
        loc, *this, classAddr, nonVirtualOffset, virtualOffset,
        vptr.vtableClass, vptr.nearestVBase, baseValueTy);
  }

  // Finally, store the address point. Use the same CIR types as the field.
  //
  // vtable field is derived from `this` pointer, therefore they should be in
  // the same addr space.
  auto srcPtrTy =
      mlir::cast<cir::PointerType>(classAddr.getPointer().getType());
  auto vptrPtrTy = cir::PointerType::get(
      cir::VPtrType::get(builder.getContext()), srcPtrTy.getAddrSpace());
  auto vtablePtr = cir::VTableGetVPtrOp::create(builder, loc, vptrPtrTy,
                                                classAddr.getPointer());
  Address vtableField = Address(vtablePtr, classAddr.getAlignment());
  if (auto authInfo =
          getVTablePointerAuthInfo(*this, loc, vptr.base.getBase(), vtableField))
    vtableAddressPoint = emitVTablePointerAuthSign(*this, loc, *authInfo,
                                                   vtableAddressPoint);
  if (cgm.getCodeGenOpts().OptimizationLevel > 0 &&
      cgm.getCodeGenOpts().StrictVTablePointers) {
    // Match OGCG's DecorateInstructionWithInvariantGroup for vptr stores under
    // -fstrict-vtable-pointers. This is LLVM load/store metadata, not a true
    // laundering step, so keep the pointer unchanged and mark the store op.
  }
  auto storeOp = builder.createStore(loc, vtableAddressPoint, vtableField);
  if (cgm.getCodeGenOpts().OptimizationLevel > 0 &&
      cgm.getCodeGenOpts().StrictVTablePointers)
    storeOp->setAttr("cir.invariant_group",
                     mlir::UnitAttr::get(builder.getContext()));
  TBAAAccessInfo tbaaInfo =
      cgm.getTBAAVTablePtrAccessInfo(cir::VPtrType::get(builder.getContext()));
  cgm.decorateOperationWithTBAA(storeOp, tbaaInfo);
}

void CIRGenFunction::initializeVTablePointers(mlir::Location loc,
                                              const CXXRecordDecl *rd) {
  // Ignore classes without a vtable.
  if (!rd->isDynamicClass())
    return;

  // Initialize the vtable pointers for this class and all of its bases.
  if (cgm.getCXXABI().doStructorsInitializeVPtrs(rd))
    for (const auto &vptr : getVTablePointers(rd))
      initializeVTablePointer(loc, vptr);

  if (rd->getNumVBases())
    cgm.getCXXABI().initializeHiddenVirtualInheritanceMembers(*this, rd);
}

CIRGenFunction::VPtrsVector
CIRGenFunction::getVTablePointers(const CXXRecordDecl *vtableClass) {
  CIRGenFunction::VPtrsVector vptrsResult;
  VisitedVirtualBasesSetTy vbases;
  getVTablePointers(BaseSubobject(vtableClass, CharUnits::Zero()),
                    /*NearestVBase=*/nullptr,
                    /*OffsetFromNearestVBase=*/CharUnits::Zero(),
                    /*BaseIsNonVirtualPrimaryBase=*/false, vtableClass, vbases,
                    vptrsResult);
  return vptrsResult;
}

void CIRGenFunction::getVTablePointers(BaseSubobject base,
                                       const CXXRecordDecl *nearestVBase,
                                       CharUnits offsetFromNearestVBase,
                                       bool baseIsNonVirtualPrimaryBase,
                                       const CXXRecordDecl *vtableClass,
                                       VisitedVirtualBasesSetTy &vbases,
                                       VPtrsVector &vptrs) {
  // If this base is a non-virtual primary base the address point has already
  // been set.
  if (!baseIsNonVirtualPrimaryBase) {
    // Initialize the vtable pointer for this base.
    VPtr vptr = {base, nearestVBase, offsetFromNearestVBase, vtableClass};
    vptrs.push_back(vptr);
  }

  const CXXRecordDecl *rd = base.getBase();

  for (const auto &nextBase : rd->bases()) {
    const auto *baseDecl =
        cast<CXXRecordDecl>(nextBase.getType()->castAs<RecordType>()->getDecl())
            ->getDefinitionOrSelf();

    // Ignore classes without a vtable.
    if (!baseDecl->isDynamicClass())
      continue;

    CharUnits baseOffset;
    CharUnits baseOffsetFromNearestVBase;
    bool baseDeclIsNonVirtualPrimaryBase;
    const CXXRecordDecl *nextBaseDecl;

    if (nextBase.isVirtual()) {
      // Check if we've visited this virtual base before.
      if (!vbases.insert(baseDecl).second)
        continue;

      const ASTRecordLayout &layout =
          getContext().getASTRecordLayout(vtableClass);

      nextBaseDecl = baseDecl;
      baseOffset = layout.getVBaseClassOffset(baseDecl);
      baseOffsetFromNearestVBase = CharUnits::Zero();
      baseDeclIsNonVirtualPrimaryBase = false;
    } else {
      const ASTRecordLayout &layout = getContext().getASTRecordLayout(rd);

      nextBaseDecl = nearestVBase;
      baseOffset = base.getBaseOffset() + layout.getBaseClassOffset(baseDecl);
      baseOffsetFromNearestVBase =
          offsetFromNearestVBase + layout.getBaseClassOffset(baseDecl);
      baseDeclIsNonVirtualPrimaryBase = layout.getPrimaryBase() == baseDecl;
    }

    getVTablePointers(BaseSubobject(baseDecl, baseOffset), nextBaseDecl,
                      baseOffsetFromNearestVBase,
                      baseDeclIsNonVirtualPrimaryBase, vtableClass, vbases,
                      vptrs);
  }
}

Address CIRGenFunction::loadCXXThisAddress() {
  assert(curFuncDecl && "loading 'this' without a func declaration?");
  assert(isa<CXXMethodDecl>(curFuncDecl));

  // Lazily compute CXXThisAlignment.
  if (cxxThisAlignment.isZero()) {
    // Just use the best known alignment for the parent.
    // TODO: if we're currently emitting a complete-object ctor/dtor, we can
    // always use the complete-object alignment.
    auto rd = cast<CXXMethodDecl>(curFuncDecl)->getParent();
    cxxThisAlignment = cgm.getClassPointerAlignment(rd);
  }

  return Address(loadCXXThis(), cxxThisAlignment);
}

void CIRGenFunction::emitInitializerForField(FieldDecl *field, LValue lhs,
                                             Expr *init) {
  QualType fieldType = field->getType();
  switch (getEvaluationKind(fieldType)) {
  case cir::TEK_Scalar:
    if (lhs.isSimple()) {
      emitExprAsInit(init, field, lhs, false);
    } else {
      RValue rhs = RValue::get(emitScalarExpr(init));
      emitStoreThroughLValue(rhs, lhs);
    }
    break;
  case cir::TEK_Complex:
    emitComplexExprIntoLValue(init, lhs, /*isInit=*/true);
    break;
  case cir::TEK_Aggregate: {
    AggValueSlot slot = AggValueSlot::forLValue(
        lhs, AggValueSlot::IsDestructed, AggValueSlot::IsNotAliased,
        getOverlapForFieldInit(field), AggValueSlot::IsNotZeroed,
        AggValueSlot::IsSanitizerChecked);
    emitAggExpr(init, slot);
    break;
  }
  }

  // Ensure that we destroy this object if an exception is thrown later in the
  // constructor.
  QualType::DestructionKind dtorKind = fieldType.isDestructedType();
  if (needsEHCleanup(dtorKind)) {
    if (isa<CXXConstructorDecl>(curGD.getDecl())) {
      // Re-derive the field address from a fresh `this` load to avoid
      // capturing mlir::Values that may have been defined inside a cir.scope
      // created by VisitExprWithCleanups during aggregate initialization.
      // Using a stale address from before the scope could cause SSA dominance
      // violations when the cleanup is emitted outside the scope.
      mlir::Value freshThis = loadCXXThis();
      CanQualType recordTy =
          getContext().getCanonicalTagType(field->getParent());
      LValue freshLhs =
          (curGD.getCtorType() == Ctor_Base)
              ? makeNaturalAlignPointeeAddrLValue(freshThis, recordTy)
              : makeNaturalAlignAddrLValue(freshThis, recordTy);
      freshLhs = emitLValueForFieldInitialization(freshLhs, field,
                                                   field->getName());
      pushEHDestroy(dtorKind, freshLhs.getAddress(), fieldType);
    } else {
      // Non-constructor context (e.g., captured statement field init).
      // Use the original LHS address directly, matching OGCG behavior.
      pushEHDestroy(dtorKind, lhs.getAddress(), fieldType);
    }
  }
}

Address CIRGenFunction::emitCXXMemberDataPointerAddress(
    const Expr *e, Address base, mlir::Value memberPtr,
    const MemberPointerType *memberPtrType, LValueBaseInfo *baseInfo,
    TBAAAccessInfo *tbaaInfoOut, bool isInBounds) {
  cir::GEPNoWrapFlags noWrapFlags = isInBounds
                                        ? cir::GEPNoWrapFlags::inbounds
                                        : cir::GEPNoWrapFlags::none;
  cir::GetRuntimeMemberOp op = builder.createGetIndirectMember(
      getLoc(e->getSourceRange()), base.getPointer(), memberPtr, noWrapFlags);

  QualType memberType = memberPtrType->getPointeeType();
  TBAAAccessInfo tbaaInfo;
  CharUnits memberAlign =
      cgm.getNaturalTypeAlignment(memberType, baseInfo, &tbaaInfo);
  if (tbaaInfoOut)
    *tbaaInfoOut = tbaaInfo;
  memberAlign = cgm.getDynamicOffsetAlignment(
      base.getAlignment(), memberPtrType->getMostRecentCXXRecordDecl(),
      memberAlign);

  return Address(op, convertTypeForMem(memberPtrType->getPointeeType()),
                 memberAlign);
}

CharUnits
CIRGenModule::getDynamicOffsetAlignment(CharUnits actualBaseAlign,
                                        const CXXRecordDecl *baseDecl,
                                        CharUnits expectedTargetAlign) {
  // If the base is an incomplete type (which is, alas, possible with
  // member pointers), be pessimistic.
  if (!baseDecl->isCompleteDefinition())
    return std::min(actualBaseAlign, expectedTargetAlign);

  const ASTRecordLayout &baseLayout =
      getASTContext().getASTRecordLayout(baseDecl);
  CharUnits expectedBaseAlign = baseLayout.getNonVirtualAlignment();

  // If the class is properly aligned, assume the target offset is, too.
  //
  // This actually isn't necessarily the right thing to do --- if the
  // class is a complete object, but it's only properly aligned for a
  // base subobject, then the alignments of things relative to it are
  // probably off as well.  (Note that this requires the alignment of
  // the target to be greater than the NV alignment of the derived
  // class.)
  //
  // However, our approach to this kind of under-alignment can only
  // ever be best effort; after all, we're never going to propagate
  // alignments through variables or parameters.  Note, in particular,
  // that constructing a polymorphic type in an address that's less
  // than pointer-aligned will generally trap in the constructor,
  // unless we someday add some sort of attribute to change the
  // assumed alignment of 'this'.  So our goal here is pretty much
  // just to allow the user to explicitly say that a pointer is
  // under-aligned and then safely access its fields and vtables.
  if (actualBaseAlign >= expectedBaseAlign)
    return expectedTargetAlign;

  // Otherwise, we might be offset by an arbitrary multiple of the
  // actual alignment.  The correct adjustment is to take the min of
  // the two alignments.
  return std::min(actualBaseAlign, expectedTargetAlign);
}

/// Return the best known alignment for a pointer to a virtual base,
/// given the alignment of a pointer to the derived class.
clang::CharUnits
CIRGenModule::getVBaseAlignment(CharUnits actualDerivedAlign,
                                const CXXRecordDecl *derivedClass,
                                const CXXRecordDecl *vbaseClass) {
  // The basic idea here is that an underaligned derived pointer might
  // indicate an underaligned base pointer.

  assert(vbaseClass->isCompleteDefinition());
  const ASTRecordLayout &baseLayout =
      getASTContext().getASTRecordLayout(vbaseClass);
  CharUnits expectedVBaseAlign = baseLayout.getNonVirtualAlignment();

  return getDynamicOffsetAlignment(actualDerivedAlign, derivedClass,
                                   expectedVBaseAlign);
}

/// Emit a loop to call a particular constructor for each of several members
/// of an array.
///
/// \param ctor the constructor to call for each element
/// \param arrayType the type of the array to initialize
/// \param arrayBegin an arrayType*
/// \param zeroInitialize true if each element should be
///   zero-initialized before it is constructed
void CIRGenFunction::emitCXXAggrConstructorCall(
    const CXXConstructorDecl *ctor, const clang::ArrayType *arrayType,
    Address arrayBegin, const CXXConstructExpr *e, bool newPointerIsChecked,
    bool zeroInitialize) {
  QualType elementType;
  mlir::Value numElements = emitArrayLength(arrayType, elementType, arrayBegin);
  emitCXXAggrConstructorCall(ctor, numElements, arrayBegin, e,
                             newPointerIsChecked, zeroInitialize);
}

/// Emit a loop to call a particular constructor for each of several members
/// of an array.
///
/// \param ctor the constructor to call for each element
/// \param numElements the number of elements in the array;
///   may be zero
/// \param arrayBase a T*, where T is the type constructed by ctor
/// \param zeroInitialize true if each element should be
///   zero-initialized before it is constructed
void CIRGenFunction::emitCXXAggrConstructorCall(
    const CXXConstructorDecl *ctor, mlir::Value numElements, Address arrayBase,
    const CXXConstructExpr *e, bool newPointerIsChecked, bool zeroInitialize) {
  // It's legal for numElements to be zero.  This can happen both
  // dynamically, because x can be zero in 'new A[x]', and statically,
  // because of GCC extensions that permit zero-length arrays.  There
  // are probably legitimate places where we could assume that this
  // doesn't happen, but it's not clear that it's worth it.

  mlir::Type elementType;
  bool isDynamic = false;

  if (auto arrayTy =
          mlir::dyn_cast<cir::ArrayType>(arrayBase.getElementType())) {
    // Static case: extract innermost element type from array type.
    elementType = arrayTy.getElementType();
    while (auto inner = mlir::dyn_cast<cir::ArrayType>(elementType))
      elementType = inner.getElementType();
  } else {
    // Dynamic case: arrayBase already points to element type directly.
    elementType = arrayBase.getElementType();
    isDynamic = true;
  }
  cir::PointerType ptrToElmType = builder.getPointerTo(elementType);

  // Optimize for a constant count (static path only).
  cir::ArrayType arrayTy;
  if (!isDynamic) {
    arrayTy = mlir::cast<cir::ArrayType>(arrayBase.getElementType());
    if (auto constantCount = numElements.getDefiningOp<cir::ConstantOp>()) {
      if (auto constIntAttr = constantCount.getValueAttr<cir::IntAttr>()) {
        // Just skip out if the constant count is zero.
        if (constIntAttr.getUInt() == 0)
          return;

        arrayTy = cir::ArrayType::get(elementType, constIntAttr.getUInt());
      }

      if (constantCount.use_empty())
        constantCount.erase();
    }
  }

  // The alignment of the base, adjusted by the size of a single element,
  // provides a conservative estimate of the alignment of every element.
  // (This assumes we never start tracking offsetted alignments.)
  //
  // Note that these are complete objects and so we don't need to
  // use the non-virtual size or alignment.
  CanQualType type = getContext().getCanonicalTagType(ctor->getParent());
  CharUnits eltAlignment = arrayBase.getAlignment().alignmentOfArrayElement(
      getContext().getTypeSizeInChars(type));

  // C++ [class.temporary]p4:
  // There are two contexts in which temporaries are destroyed at a different
  // point than the end of the full-expression. The first context is when a
  // default constructor is called to initialize an element of an array.
  // If the constructor has one or more default arguments, the destruction of
  // every temporary created in a default argument expression is sequenced
  // before the construction of the next array element, if any.

  // Create a cursor alloca to track construction progress for partial
  // array cleanup. Uses the "irregular" (alloca-based) approach because
  // CIR's ArrayCtor is a structured op without an explicit loop cursor.
  Address endOfInit = Address::invalid();
  mlir::Value arrayBeginPtr;
  if (getLangOpts().Exceptions &&
      !ctor->getParent()->hasTrivialDestructor()) {
    arrayBeginPtr =
        builder.createPtrBitcast(arrayBase.getPointer(), elementType);
    endOfInit = createTempAlloca(ptrToElmType, getPointerAlign(),
                                *currSrcLoc, "array.endOfInit");
    builder.createStore(*currSrcLoc, arrayBeginPtr, endOfInit);
  }

  {
    RunCleanupsScope scope(*this);

    // OGCG may push/pop convergence control tokens around this construction
    // loop for SPIR-V targets. CIR currently keeps structured loop emission
    // without explicit token threading in this path.

    // Push partial array cleanup that destroys [arrayBegin, endOfInit)
    // if the constructor throws during element construction. We use the
    // "irregular" approach (alloca-based cursor) because CIR's ArrayCtor
    // is a structured op — OGCG uses pushRegularPartialArrayCleanup with
    // a PHI node here, but CIR doesn't have a loop PHI at this level.
    // Push as EHCleanup only — the partial-destroy should only run on the
    // exception unwind path. The normal path has its own cir.array.dtor
    // for end-of-scope destruction (OGCG uses EHCleanup for the regular
    // variant: CGDecl.cpp pushRegularPartialArrayCleanup).
    if (endOfInit.isValid()) {
      pushIrregularPartialArrayCleanup(arrayBeginPtr, endOfInit, type,
                                       eltAlignment, destroyCXXObject,
                                       EHCleanup);
    }

    // Lambda to emit the constructor call for a single element.
    auto emitConstructorCallForElement =
        [&](mlir::Location loc, mlir::Value curPtr) {
          Address curAddr = Address(curPtr, elementType, eltAlignment);

          // Zero initialize the storage, if requested. This must happen
          // per-element inside the loop (not before it) to handle
          // non-zero-initializable types like member pointers correctly.
          if (zeroInitialize)
            emitNullInitialization(loc, curAddr, type);

          // C++ [class.temporary]p4:
          // There are two contexts in which temporaries are destroyed at a
          // different point than the end of the full-expression. The first
          // context is when a default constructor is called to initialize an
          // element of an array. If the constructor has one or more default
          // arguments, the destruction of every temporary created in a
          // default argument expression is sequenced before the construction
          // of the next array element, if any.
          {
            RunCleanupsScope iterationScope(*this);

            auto currAVS = AggValueSlot::forAddr(
                curAddr, type.getQualifiers(), AggValueSlot::IsDestructed,
                AggValueSlot::IsNotAliased, AggValueSlot::DoesNotOverlap,
                AggValueSlot::IsNotZeroed,
                newPointerIsChecked ? AggValueSlot::IsSanitizerChecked
                                    : AggValueSlot::IsNotSanitizerChecked);
            emitCXXConstructorCall(ctor, Ctor_Complete,
                                   /*ForVirtualBase=*/false,
                                   /*Delegating=*/false, currAVS, e);
          }

          // After successful construction, update endOfInit to point past
          // this element. If the next constructor throws, the cleanup will
          // destroy elements [arrayBegin, endOfInit).
          if (endOfInit.isValid()) {
            mlir::Value one = builder.getConstantInt(
                loc, ptrDiffTy, 1);
            mlir::Value nextPtr =
                builder.createPtrStride(loc, curPtr, one);
            builder.createStore(loc, nextPtr, endOfInit);
          }
        };

    if (isDynamic) {
      // Dynamic element count: emit a pointer-based do-while loop guarded
      // by a zero-count check, mirroring OGCG's EmitCXXAggrConstructorCall.
      mlir::Location loc = *currSrcLoc;
      mlir::Value beginPtr =
          builder.createPtrBitcast(arrayBase.getPointer(), elementType);
      mlir::Value endPtr =
          builder.createPtrStride(loc, beginPtr, numElements);

      // Guard: if numElements == 0, skip the loop entirely.
      auto isNotEmpty = builder.createCompare(loc, cir::CmpOpKind::ne,
                                              beginPtr, endPtr);
      cir::IfOp::create(
          builder, loc, isNotEmpty, /*withElseRegion=*/false,
          [&](mlir::OpBuilder &, mlir::Location loc) {
            // Alloca for loop cursor (stores a pointer-to-element).
            auto ptrToPtrElmType = builder.getPointerTo(ptrToElmType);
            mlir::Value curAddrVal = builder.createAlloca(
                loc, ptrToPtrElmType, ptrToElmType, "__array_ctor_idx",
                builder.getAlignmentAttr(getPointerAlign().getQuantity()));
            Address curAddr(curAddrVal, ptrToElmType, getPointerAlign());
            builder.createStore(loc, beginPtr, curAddr);

            builder.createDoWhile(
                loc,
                /*condBuilder=*/
                [&](mlir::OpBuilder &, mlir::Location loc) {
                  auto cur = builder.createLoad(loc, curAddr);
                  auto cmp = builder.createCompare(loc, cir::CmpOpKind::ne,
                                                   cur, endPtr);
                  builder.createCondition(cmp);
                },
                /*bodyBuilder=*/
                [&](mlir::OpBuilder &, mlir::Location loc) {
                  auto cur = builder.createLoad(loc, curAddr);
                  emitConstructorCallForElement(loc, cur);
                  // Advance cursor.
                  mlir::Value one = builder.getConstantInt(
                      loc, ptrDiffTy, 1);
                  mlir::Value next =
                      builder.createPtrStride(loc, cur, one);
                  builder.createStore(loc, next, curAddr);
                  cir::YieldOp::create(builder, loc);
                });
            cir::YieldOp::create(builder, loc);
          });
    } else {
      // Static element count: use ArrayCtor structured op (lowered to a
      // loop by LoweringPrepare).
      mlir::Value arrayOp =
          builder.createPtrBitcast(arrayBase.getPointer(), arrayTy);
      cir::ArrayCtor::create(
          builder, *currSrcLoc, arrayOp,
          [&](mlir::OpBuilder &b, mlir::Location loc) {
            mlir::BlockArgument arg =
                b.getInsertionBlock()->addArgument(ptrToElmType, loc);
            emitConstructorCallForElement(loc, arg);
            cir::YieldOp::create(builder, loc);
          });
    }
  }
}

void CIRGenFunction::emitDelegateCXXConstructorCall(
    const CXXConstructorDecl *ctor, CXXCtorType ctorType,
    const FunctionArgList &args, SourceLocation loc) {
  CallArgList delegateArgs;

  FunctionArgList::const_iterator i = args.begin(), e = args.end();
  assert(i != e && "no parameters to constructor");

  // this
  Address thisAddr = loadCXXThisAddress();
  delegateArgs.add(RValue::get(thisAddr.getPointer()), (*i)->getType());
  ++i;

  // FIXME: The location of the VTT parameter in the parameter list is specific
  // to the Itanium ABI and shouldn't be hardcoded here.
  if (cgm.getCXXABI().needsVTTParameter(curGD)) {
    assert(i != e && "cannot skip vtt parameter, already done with args");
    assert((*i)->getType()->isPointerType() &&
           "skipping parameter not of vtt type");
    ++i;
  }

  // Explicit arguments.
  for (; i != e; ++i) {
    const VarDecl *param = *i;
    // FIXME: per-argument source location
    emitDelegateCallArg(delegateArgs, param, loc);
  }

  emitCXXConstructorCall(ctor, ctorType, /*ForVirtualBase=*/false,
                         /*Delegating=*/true, thisAddr, delegateArgs,
                         AggValueSlot::MayOverlap, loc,
                         /*newPointerIsChecked=*/true);
}

void CIRGenFunction::emitImplicitAssignmentOperatorBody(FunctionArgList &args) {
  const auto *assignOp = cast<CXXMethodDecl>(curGD.getDecl());
  assert(assignOp->isCopyAssignmentOperator() ||
         assignOp->isMoveAssignmentOperator());
  const Stmt *rootS = assignOp->getBody();
  assert(isa<CompoundStmt>(rootS) &&
         "Body of an implicit assignment operator should be compound stmt.");
  const auto *rootCS = cast<CompoundStmt>(rootS);

  cgm.setCXXSpecialMemberAttr(cast<cir::FuncOp>(curFn), assignOp);

  // CIR cleanup/scoping is modeled directly; no standalone RunCleanupsScope
  // guard object is used for implicit assignment operator emission.

  // For trivial union copy/move-assignment, emit a memcpy of the entire
  // object. The AST body only contains `return *this` without any copy
  // statements, so without explicit memcpy the data is never copied.
  // This matches OGCG's AssignmentMemcpyizer which emits memcpy for
  // trivial types.
  const CXXRecordDecl *classDecl = assignOp->getParent();
  if (classDecl->isUnion() && assignOp->isTrivial()) {
    mlir::Location loc = getLoc(assignOp->getSourceRange());
    // 'this' is the first arg, source is the second.
    Address thisAddr = loadCXXThisAddress();
    // The second parameter is the source reference.
    mlir::Value srcVal = builder.createLoad(
        loc, getAddrOfLocalVar(args[1]));
    Address srcAddr(srcVal, thisAddr.getElementType(),
                    thisAddr.getAlignment());

    QualType recordTy = getContext().getCanonicalTagType(classDecl);
    CharUnits size = getContext().getTypeSizeInChars(recordTy);

    mlir::Value dstPtr = builder.createPtrBitcast(thisAddr.getPointer(),
                                                  builder.getVoidTy());
    mlir::Value srcPtr = builder.createPtrBitcast(srcAddr.getPointer(),
                                                  builder.getVoidTy());
    auto sizeTy = mlir::cast<cir::IntType>(
        convertType(getContext().getSizeType()));
    mlir::Value sizeVal = builder.getConstantInt(loc, sizeTy,
                                                 size.getQuantity());
    // Use libc memcpy for the union copy.
    cir::MemCpyOp::create(builder, loc, dstPtr, srcPtr, sizeVal);
  }

  // Classic codegen uses a special class to attempt to replace member
  // assignments with memcpy when the value representation can be copied
  // directly, matching OGCG's AssignmentMemcpyizer.
  // OGCG calls incrementProfileCounter and maybeCreateMCDCCondBitmap here
  // for PGO and MCDC coverage (CGClass.cpp:1655-1656).
  assert(!cir::MissingFeatures::mcdcCoverageFolding());
  AssignmentMemcpyizer assignmentMemcpyizer(*this, assignOp, args);
  for (Stmt *s : rootCS->body())
    if (assignmentMemcpyizer.emitAssignment(s).failed()) {
      cgm.errorNYI(s->getSourceRange(),
                   "emitImplicitAssignmentOperatorBody statement");
      return;
    }
  if (assignmentMemcpyizer.finish().failed()) {
    if (const Stmt *lastStmt =
            rootCS->body_empty() ? nullptr : rootCS->body_back())
      cgm.errorNYI(lastStmt->getSourceRange(),
                   "emitImplicitAssignmentOperatorBody statement");
    else
      cgm.errorNYI(assignOp->getSourceRange(),
                   "emitImplicitAssignmentOperatorBody statement");
    return;
  }
}

void CIRGenFunction::emitForwardingCallToLambda(
    const CXXMethodDecl *callOperator, CallArgList &callArgs) {
  // Get the address of the call operator.
  const CIRGenFunctionInfo &calleeFnInfo =
      cgm.getTypes().arrangeCXXMethodDeclaration(callOperator);
  cir::FuncOp calleePtr = cgm.getAddrOfFunction(
      GlobalDecl(callOperator), cgm.getTypes().getFunctionType(calleeFnInfo));

  // Prepare the return slot.
  const FunctionProtoType *fpt =
      callOperator->getType()->castAs<FunctionProtoType>();
  QualType resultType = fpt->getReturnType();
  ReturnValueSlot returnSlot;
  // For aggregate return types that aren't trivially copyable (or would use
  // sret), pass ReturnValue as the return slot so the callee writes directly
  // into it, avoiding an aggregate copy of a non-trivially-copyable type.
  // This matches OGCG which checks ABIArgInfo::Indirect.
  if (!resultType->isVoidType() && hasAggregateEvaluationKind(resultType) &&
      !resultType.isTriviallyCopyableType(getContext()))
    returnSlot = ReturnValueSlot(returnValue);

  // We don't need to separately arrange the call arguments because
  // the call can't be variadic anyway --- it's impossible to forward
  // variadic arguments.

  // Now emit our call.
  CIRGenCallee callee =
      CIRGenCallee::forDirect(calleePtr, GlobalDecl(callOperator));
  RValue rv = emitCall(calleeFnInfo, callee, returnSlot, callArgs);

  // If necessary, copy the returned value into the slot.
  if (!resultType->isVoidType() && returnSlot.isNull()) {
    if (getLangOpts().ObjCAutoRefCount && resultType->isObjCRetainableType()) {
      cgm.errorNYI(*currSrcLoc,
                   "emitForwardingCallToLambda with ObjCAutoRefCount");
      return;
    }
    emitReturnOfRValue(*currSrcLoc, rv, resultType);
  } else {
    // For void results, there's nothing to copy — just branch to the return
    // block. For non-null return slots (sret), the callee already wrote the
    // return value through the slot pointer.
    auto *retBlock = curLexScope->getOrCreateRetBlock(*this, *currSrcLoc);
    emitBranchThroughCleanup(*currSrcLoc, returnBlock(retBlock));
  }
}

void CIRGenFunction::emitLambdaDelegatingInvokeBody(const CXXMethodDecl *md) {
  const CXXRecordDecl *lambda = md->getParent();

  // Start building arguments for forwarding call
  CallArgList callArgs;

  QualType lambdaType = getContext().getCanonicalTagType(lambda);
  QualType thisType = getContext().getPointerType(lambdaType);
  Address thisPtr =
      createMemTemp(lambdaType, getLoc(md->getSourceRange()), "unused.capture");
  callArgs.add(RValue::get(thisPtr.getPointer()), thisType);

  // Add the rest of the parameters.
  for (auto *param : md->parameters())
    emitDelegateCallArg(callArgs, param, param->getBeginLoc());

  const CXXMethodDecl *callOp = lambda->getLambdaCallOperator();
  // For a generic lambda, find the corresponding call operator specialization
  // to which the call to the static-invoker shall be forwarded.
  if (lambda->isGenericLambda()) {
    assert(md->isFunctionTemplateSpecialization());
    const TemplateArgumentList *tal = md->getTemplateSpecializationArgs();
    FunctionTemplateDecl *callOpTemplate =
        callOp->getDescribedFunctionTemplate();
    void *InsertPos = nullptr;
    FunctionDecl *correspondingCallOpSpecialization =
        callOpTemplate->findSpecialization(tal->asArray(), InsertPos);
    assert(correspondingCallOpSpecialization);
    callOp = cast<CXXMethodDecl>(correspondingCallOpSpecialization);
  }
  emitForwardingCallToLambda(callOp, callArgs);
}

void CIRGenFunction::emitLambdaStaticInvokeBody(const CXXMethodDecl *md) {
  if (md->isVariadic()) {
    // FIXME: Making this work correctly is nasty because it requires either
    // cloning the body of the call operator or making the call operator
    // forward.
    cgm.errorNYI(md->getSourceRange(), "emitLambdaStaticInvokeBody: variadic");
    return;
  }

  emitLambdaDelegatingInvokeBody(md);
}

void CIRGenFunction::destroyCXXObject(CIRGenFunction &cgf, Address addr,
                                      QualType type) {
  const auto *record = type->castAsCXXRecordDecl();
  const CXXDestructorDecl *dtor = record->getDestructor();
  // TODO(cir): Unlike traditional codegen, CIRGen should actually emit trivial
  // dtors which shall be removed on later CIR passes. However, only remove this
  // assertion after we have a test case to exercise this path.
  assert(!dtor->isTrivial());
  cgf.emitCXXDestructorCall(dtor, Dtor_Complete, /*forVirtualBase*/ false,
                            /*delegating=*/false, addr, type);
}

namespace {
mlir::Value loadThisForDtorDelete(CIRGenFunction &cgf,
                                  const CXXDestructorDecl *dd) {
  if (Expr *thisArg = dd->getOperatorDeleteThisArg())
    return cgf.emitScalarExpr(thisArg);
  return cgf.loadCXXThis();
}

/// Call the operator delete associated with the current destructor.
struct CallDtorDelete final : EHScopeStack::Cleanup {
  CallDtorDelete() {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    const CXXDestructorDecl *dtor = cast<CXXDestructorDecl>(cgf.curCodeDecl);
    const CXXRecordDecl *classDecl = dtor->getParent();
    cgf.emitDeleteCall(dtor->getOperatorDelete(),
                       loadThisForDtorDelete(cgf, dtor),
                       cgf.getContext().getCanonicalTagType(classDecl));
  }
};

class DestroyField final : public EHScopeStack::Cleanup {
  const FieldDecl *field;
  CIRGenFunction::Destroyer *destroyer;
  bool useEHCleanupForArray;

public:
  DestroyField(const FieldDecl *field, CIRGenFunction::Destroyer *destroyer,
               bool useEHCleanupForArray)
      : field(field), destroyer(destroyer),
        useEHCleanupForArray(useEHCleanupForArray) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    // Find the address of the field.
    Address thisValue = cgf.loadCXXThisAddress();
    CanQualType recordTy =
        cgf.getContext().getCanonicalTagType(field->getParent());
    LValue thisLV = cgf.makeAddrLValue(thisValue, recordTy);
    LValue lv = cgf.emitLValueForField(thisLV, field);
    assert(lv.isSimple());

    cgf.emitDestroy(lv.getAddress(), field->getType(), destroyer,
                    flags.isForNormalCleanup() && useEHCleanupForArray);
  }
};

// Forward declarations for dtor trivial body checks, needed by
// SanitizeDtorCleanupBuilder. These are duplicated from CIRGenFunction.cpp
// because they're file-static there (matching OGCG's CGClass.cpp pattern).
/// Emit a call to a sanitizer dtor callback (__sanitizer_dtor_callback_fields
/// or __sanitizer_dtor_callback_vptr). Port of OGCG's EmitSanitizerDtorCallback.
static void emitSanitizerDtorCallback(CIRGenFunction &cgf, mlir::Location loc,
                                      StringRef name, mlir::Value ptr,
                                      std::optional<CharUnits::QuantityType>
                                          poisonSize = {}) {
  // Build the function type: void(ptr) or void(ptr, size_t).
  auto &builder = cgf.getBuilder();
  SmallVector<mlir::Type, 2> argTypes = {cgf.voidPtrTy};
  SmallVector<mlir::Value, 2> args;

  // Cast to void* if needed.
  if (ptr.getType() != cgf.voidPtrTy)
    ptr = builder.createBitcast(loc, ptr, cgf.voidPtrTy);
  args.push_back(ptr);

  if (poisonSize.has_value()) {
    argTypes.push_back(cgf.sizeTy);
    args.push_back(
        builder.getConstant(loc, cir::IntAttr::get(cgf.sizeTy, *poisonSize)));
  }

  auto fnTy = cir::FuncType::get(argTypes, cgf.voidTy);
  cir::FuncOp fn = cgf.cgm.createRuntimeFunction(fnTy, name);
  CIRGenFunction::SanitizerScope sanitizerScope(&cgf);
  cgf.emitNounwindRuntimeCall(loc, fn, args);
}

/// Emit __sanitizer_dtor_callback_fields(ptr, size). Port of OGCG's
/// EmitSanitizerDtorFieldsCallback.
static void emitSanitizerDtorFieldsCallback(CIRGenFunction &cgf,
                                            mlir::Location loc,
                                            mlir::Value ptr,
                                            CharUnits::QuantityType
                                                poisonSize) {
  emitSanitizerDtorCallback(cgf, loc, "__sanitizer_dtor_callback_fields", ptr,
                            poisonSize);
}

/// Set disable-tail-calls on the current function. Port of OGCG's
/// CurFn->addFnAttr("disable-tail-calls", "true").
static void setDisableTailCalls(CIRGenFunction &cgf) {
  auto fn = mlir::dyn_cast<cir::FuncOp>(cgf.curFn);
  if (!fn)
    return;
  mlir::NamedAttrList attrs;
  if (auto extraAttrs = fn.getExtraAttrsAttr()) {
    for (auto attr : extraAttrs.getElements().getValue())
      attrs.append(attr.getName(), attr.getValue());
  }
  auto *ctx = &cgf.cgm.getMLIRContext();
  attrs.set("disable-tail-calls", mlir::StringAttr::get(ctx, "true"));
  fn.setExtraAttrsAttr(
      cir::ExtraFuncAttributesAttr::get(attrs.getDictionary(ctx)));
}

/// Poison base class with a trivial destructor. Port of OGCG's
/// SanitizeDtorTrivialBase.
struct SanitizeDtorTrivialBase final : EHScopeStack::Cleanup {
  const CXXRecordDecl *baseClass;
  bool baseIsVirtual;
  SanitizeDtorTrivialBase(const CXXRecordDecl *base, bool baseIsVirtual)
      : baseClass(base), baseIsVirtual(baseIsVirtual) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    const CXXRecordDecl *derivedClass =
        cast<CXXMethodDecl>(cgf.curCodeDecl)->getParent();

    assert(cgf.currSrcLoc && "expected source location");
    Address addr = cgf.getAddressOfDirectBaseInCompleteClass(
        *cgf.currSrcLoc, cgf.loadCXXThisAddress(), derivedClass, baseClass,
        baseIsVirtual);

    const ASTRecordLayout &baseLayout =
        cgf.getContext().getASTRecordLayout(baseClass);
    CharUnits baseSize = baseLayout.getSize();

    if (!baseSize.isPositive())
      return;

    mlir::Location callbackLoc = cgf.getLoc(baseClass->getLocation());
    emitSanitizerDtorFieldsCallback(cgf, callbackLoc,
                                    addr.emitRawPointer(),
                                    baseSize.getQuantity());

    // Prevent the current stack frame from disappearing from the stack trace.
    setDisableTailCalls(cgf);
  }
};

/// Poison a range of fields with trivial destructors. Port of OGCG's
/// SanitizeDtorFieldRange.
class SanitizeDtorFieldRange final : public EHScopeStack::Cleanup {
  const CXXDestructorDecl *dtor;
  unsigned startIndex;
  unsigned endIndex;

public:
  SanitizeDtorFieldRange(const CXXDestructorDecl *dtor, unsigned startIndex,
                         unsigned endIndex)
      : dtor(dtor), startIndex(startIndex), endIndex(endIndex) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    const ASTContext &context = cgf.getContext();
    const ASTRecordLayout &layout =
        context.getASTRecordLayout(dtor->getParent());

    // It's a first trivial field so it should be at the beginning of a char,
    // still round up start offset just in case.
    CharUnits poisonStart = context.toCharUnitsFromBits(
        layout.getFieldOffset(startIndex) + context.getCharWidth() - 1);

    CharUnits poisonEnd;
    if (endIndex >= layout.getFieldCount()) {
      poisonEnd = layout.getNonVirtualSize();
    } else {
      poisonEnd = context.toCharUnitsFromBits(layout.getFieldOffset(endIndex));
    }
    CharUnits poisonSize = poisonEnd - poisonStart;
    if (!poisonSize.isPositive())
      return;

    assert(cgf.currSrcLoc && "expected source location");
    auto &builder = cgf.getBuilder();

    // Compute the offset pointer: (char*)this + poisonStart.
    mlir::Value thisPtr = cgf.loadCXXThis();
    mlir::Value charPtr =
        builder.createPtrBitcast(thisPtr, cgf.uInt8Ty);
    mlir::Value offset = builder.getConstant(
        *cgf.currSrcLoc,
        cir::IntAttr::get(cgf.sizeTy, poisonStart.getQuantity()));
    mlir::Value offsetPtr = cir::PtrStrideOp::create(
        builder, *cgf.currSrcLoc, charPtr.getType(), charPtr, offset);

    const FieldDecl *topField =
        *std::next(dtor->getParent()->field_begin(), startIndex);
    mlir::Location callbackLoc = cgf.getLoc(topField->getLocation());
    emitSanitizerDtorFieldsCallback(cgf, callbackLoc, offsetPtr,
                                    poisonSize.getQuantity());

    // Prevent the current stack frame from disappearing from the stack trace.
    setDisableTailCalls(cgf);
  }
};

/// Poison vtable pointer. Port of OGCG's SanitizeDtorVTable.
class SanitizeDtorVTable final : public EHScopeStack::Cleanup {
  const CXXDestructorDecl *dtor;

public:
  SanitizeDtorVTable(const CXXDestructorDecl *dtor) : dtor(dtor) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    assert(dtor->getParent()->isDynamicClass());
    (void)dtor;
    // Poison vtable and vtable ptr if they exist for this class.
    mlir::Value vtablePtr = cgf.loadCXXThis();

    assert(cgf.currSrcLoc && "expected source location");
    emitSanitizerDtorCallback(cgf, *cgf.currSrcLoc,
                              "__sanitizer_dtor_callback_vptr", vtablePtr);
  }
};

/// Builder that batches trivial field poisoning into contiguous ranges.
/// Port of OGCG's SanitizeDtorCleanupBuilder.
class SanitizeDtorCleanupBuilder {
  ASTContext &context;
  EHScopeStack &ehStack;
  const CXXDestructorDecl *dd;
  std::optional<unsigned> startIndex;

public:
  SanitizeDtorCleanupBuilder(ASTContext &context, EHScopeStack &ehStack,
                             const CXXDestructorDecl *dd)
      : context(context), ehStack(ehStack), dd(dd),
        startIndex(std::nullopt) {}
  void pushCleanupForField(const FieldDecl *field) {
    if (isEmptyFieldForLayout(context, field))
      return;
    unsigned fieldIndex = field->getFieldIndex();
    if (fieldHasTrivialDestructorBody(context, field)) {
      if (!startIndex)
        startIndex = fieldIndex;
    } else if (startIndex) {
      ehStack.pushCleanup<SanitizeDtorFieldRange>(NormalAndEHCleanup, dd,
                                                   *startIndex, fieldIndex);
      startIndex = std::nullopt;
    }
  }
  void end() {
    if (startIndex)
      ehStack.pushCleanup<SanitizeDtorFieldRange>(NormalAndEHCleanup, dd,
                                                   *startIndex, -1);
  }
};
} // namespace

/// Emit all code that comes at the end of class's destructor. This is to call
/// destructors on members and base classes in reverse order of their
/// construction.
///
/// For a deleting destructor, this also handles the case where a destroying
/// operator delete completely overrides the definition.
void CIRGenFunction::enterDtorCleanups(const CXXDestructorDecl *dd,
                                       CXXDtorType dtorType) {
  assert((!dd->isTrivial() || dd->hasAttr<DLLExportAttr>()) &&
         "Should not emit dtor epilogue for non-exported trivial dtor!");

  // The deleting-destructor phase just needs to call the appropriate
  // operator delete that Sema picked up.
  if (dtorType == Dtor_Deleting) {
    assert(dd->getOperatorDelete() &&
           "operator delete missing - EnterDtorCleanups");
    if (cxxStructorImplicitParamValue) {
      // In the Microsoft ABI, the implicit parameter is a boolean flag
      // controlling whether to call operator delete. OGCG handles this with
      // EmitConditionalDtorDeleteCall or CallDtorDeleteConditional cleanup.
      llvm_unreachable(
          "NYI: deleting destructor with implicit struct param (MSABI)");
    } else {
      if (dd->getOperatorDelete()->isDestroyingOperatorDelete()) {
        // The destroying operator delete takes ownership of both
        // destruction and deallocation. Emit the call and return
        // immediately — the normal destructor body should not run.
        const CXXRecordDecl *classDecl = dd->getParent();
        emitDeleteCall(dd->getOperatorDelete(),
                       loadThisForDtorDelete(*this, dd),
                       getContext().getCanonicalTagType(classDecl));
        // Use emitBranchThroughCleanup to properly exit through all active
        // cleanup scopes, matching OGCG's EmitBranchThroughCleanup(ReturnBlock).
        auto *retBlock =
            curLexScope->getOrCreateRetBlock(*this, *currSrcLoc);
        emitBranchThroughCleanup(*currSrcLoc, returnBlock(retBlock));
        builder.clearInsertionPoint();
      } else {
        ehStack.pushCleanup<CallDtorDelete>(NormalAndEHCleanup);
      }
    }
    return;
  }

  const CXXRecordDecl *classDecl = dd->getParent();

  // Unions have no bases and do not call field destructors.
  if (classDecl->isUnion())
    return;

  // The complete-destructor phase just destructs all the virtual bases.
  if (dtorType == Dtor_Complete) {
    // Poison the vtable pointer such that access after the base
    // and member destructors are invoked is invalid.
    if (cgm.getCodeGenOpts().SanitizeMemoryUseAfterDtor &&
        sanOpts.has(SanitizerKind::Memory) && classDecl->getNumVBases() &&
        classDecl->isPolymorphic())
      ehStack.pushCleanup<SanitizeDtorVTable>(NormalAndEHCleanup, dd);

    // We push them in the forward order so that they'll be popped in
    // the reverse order.
    for (const CXXBaseSpecifier &base : classDecl->vbases()) {
      auto *baseClassDecl = base.getType()->castAsCXXRecordDecl();

      if (baseClassDecl->hasTrivialDestructor()) {
        // Under SanitizeMemoryUseAfterDtor, poison the trivial base class
        // memory. For non-trivial base classes the same is done in the class
        // destructor.
        if (cgm.getCodeGenOpts().SanitizeMemoryUseAfterDtor &&
            sanOpts.has(SanitizerKind::Memory) && !baseClassDecl->isEmpty())
          ehStack.pushCleanup<SanitizeDtorTrivialBase>(NormalAndEHCleanup,
                                                       baseClassDecl,
                                                       /*baseIsVirtual=*/true);
      } else {
        ehStack.pushCleanup<CallBaseDtor>(NormalAndEHCleanup, baseClassDecl,
                                          /*baseIsVirtual=*/true);
      }
    }

    return;
  }

  assert(dtorType == Dtor_Base);
  // Poison the vtable pointer if it has no virtual bases, but inherits
  // virtual functions.
  if (cgm.getCodeGenOpts().SanitizeMemoryUseAfterDtor &&
      sanOpts.has(SanitizerKind::Memory) && !classDecl->getNumVBases() &&
      classDecl->isPolymorphic())
    ehStack.pushCleanup<SanitizeDtorVTable>(NormalAndEHCleanup, dd);

  // Destroy non-virtual bases.
  for (const CXXBaseSpecifier &base : classDecl->bases()) {
    // Ignore virtual bases.
    if (base.isVirtual())
      continue;

    CXXRecordDecl *baseClassDecl = base.getType()->getAsCXXRecordDecl();

    if (baseClassDecl->hasTrivialDestructor()) {
      if (cgm.getCodeGenOpts().SanitizeMemoryUseAfterDtor &&
          sanOpts.has(SanitizerKind::Memory) && !baseClassDecl->isEmpty())
        ehStack.pushCleanup<SanitizeDtorTrivialBase>(NormalAndEHCleanup,
                                                     baseClassDecl,
                                                     /*baseIsVirtual=*/false);
    } else {
      ehStack.pushCleanup<CallBaseDtor>(NormalAndEHCleanup, baseClassDecl,
                                        /*baseIsVirtual=*/false);
    }
  }

  // Poison fields such that access after their destructors are
  // invoked, and before the base class destructor runs, is invalid.
  bool sanitizeFields = cgm.getCodeGenOpts().SanitizeMemoryUseAfterDtor &&
                        sanOpts.has(SanitizerKind::Memory);
  SanitizeDtorCleanupBuilder sanitizeBuilder(getContext(), ehStack, dd);

  // Destroy direct fields.
  for (const FieldDecl *field : classDecl->fields()) {
    if (sanitizeFields)
      sanitizeBuilder.pushCleanupForField(field);

    QualType type = field->getType();
    QualType::DestructionKind dtorKind = type.isDestructedType();
    if (!dtorKind)
      continue;

    // Anonymous union members do not have their destructors called.
    const RecordType *rt = type->getAsUnionType();
    if (rt && rt->getDecl()->isAnonymousStructOrUnion())
      continue;

    CleanupKind cleanupKind = getCleanupKind(dtorKind);
    ehStack.pushCleanup<DestroyField>(cleanupKind, field,
                                      getDestroyer(dtorKind),
                                      cleanupKind & EHCleanup);
  }

  if (sanitizeFields)
    sanitizeBuilder.end();
}

namespace {
/// CallDelegatingCtorDtor - EH cleanup to call the destructor during a
/// delegating constructor if the constructor body throws.
struct CallDelegatingCtorDtor final : EHScopeStack::Cleanup {
  const CXXDestructorDecl *dtor;
  Address addr;
  CXXDtorType type;

  CallDelegatingCtorDtor(const CXXDestructorDecl *dtor, Address addr,
                         CXXDtorType type)
      : dtor(dtor), addr(addr), type(type) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    QualType thisTy = dtor->getFunctionObjectParameterType();
    cgf.emitCXXDestructorCall(dtor, type, /*forVirtualBase=*/false,
                              /*delegating=*/true, addr, thisTy);
  }
};
} // end anonymous namespace

void CIRGenFunction::emitDelegatingCXXConstructorCall(
    const CXXConstructorDecl *ctor, const FunctionArgList &args) {
  assert(ctor->isDelegatingConstructor());

  Address thisPtr = loadCXXThisAddress();

  AggValueSlot aggSlot = AggValueSlot::forAddr(
      thisPtr, Qualifiers(), AggValueSlot::IsDestructed,
      AggValueSlot::IsNotAliased, AggValueSlot::MayOverlap,
      AggValueSlot::IsNotZeroed, AggValueSlot::IsSanitizerChecked);

  emitAggExpr(ctor->init_begin()[0]->getInit(), aggSlot);

  const CXXRecordDecl *classDecl = ctor->getParent();
  if (cgm.getLangOpts().Exceptions && !classDecl->hasTrivialDestructor()) {
    CXXDtorType type =
        curGD.getCtorType() == Ctor_Complete ? Dtor_Complete : Dtor_Base;

    ehStack.pushCleanup<CallDelegatingCtorDtor>(
        EHCleanup, classDecl->getDestructor(), thisPtr, type);
  }
}

void CIRGenFunction::emitCXXDestructorCall(const CXXDestructorDecl *dd,
                                           CXXDtorType type,
                                           bool forVirtualBase, bool delegating,
                                           Address thisAddr, QualType thisTy) {
  cgm.getCXXABI().emitDestructorCall(*this, dd, type, forVirtualBase,
                                     delegating, thisAddr, thisTy);
}

mlir::Value CIRGenFunction::getVTTParameter(GlobalDecl gd, bool forVirtualBase,
                                            bool delegating) {
  if (!cgm.getCXXABI().needsVTTParameter(gd))
    return nullptr;

  const CXXRecordDecl *rd = cast<CXXMethodDecl>(curCodeDecl)->getParent();
  const CXXRecordDecl *base = cast<CXXMethodDecl>(gd.getDecl())->getParent();

  uint64_t subVTTIndex;

  if (delegating) {
    // If this is a delegating constructor call, just load the VTT.
    return loadCXXVTT();
  } else if (rd == base) {
    // If the record matches the base, this is the complete ctor/dtor
    // variant calling the base variant in a class with virtual bases.
    assert(!cgm.getCXXABI().needsVTTParameter(curGD) &&
           "doing no-op VTT offset in base dtor/ctor?");
    assert(!forVirtualBase && "Can't have same class as virtual base!");
    subVTTIndex = 0;
  } else {
    const ASTRecordLayout &layout = getContext().getASTRecordLayout(rd);
    CharUnits baseOffset = forVirtualBase ? layout.getVBaseClassOffset(base)
                                          : layout.getBaseClassOffset(base);

    subVTTIndex =
        cgm.getVTables().getSubVTTIndex(rd, BaseSubobject(base, baseOffset));
    assert(subVTTIndex != 0 && "Sub-VTT index must be greater than zero!");
  }

  mlir::Location loc = cgm.getLoc(rd->getBeginLoc());
  if (cgm.getCXXABI().needsVTTParameter(curGD)) {
    // A VTT parameter was passed to the constructor, use it.
    mlir::Value vtt = loadCXXVTT();
    return builder.createVTTAddrPoint(loc, vtt.getType(), vtt, subVTTIndex);
  } else {
    // We're the complete constructor, so get the VTT by name.
    cir::GlobalOp vtt = cgm.getVTables().getAddrOfVTT(rd);
    auto vttComponentTy =
        builder.getVoidPtrTy(cgm.getDefaultGlobalsAddressSpace());
    return builder.createVTTAddrPoint(
        loc, builder.getPointerTo(vttComponentTy),
        mlir::FlatSymbolRefAttr::get(vtt.getSymNameAttr()), subVTTIndex);
  }
}

Address CIRGenFunction::getAddressOfDerivedClass(
    mlir::Location loc, Address baseAddr, const CXXRecordDecl *derived,
    llvm::iterator_range<CastExpr::path_const_iterator> path,
    bool nullCheckValue) {
  assert(!path.empty() && "Base path should not be empty!");

  QualType derivedTy = getContext().getCanonicalTagType(derived);
  mlir::Type derivedValueTy = convertType(derivedTy);
  CharUnits nonVirtualOffset =
      cgm.computeNonVirtualBaseClassOffset(derived, path);

  // Note that in OG, no offset (nonVirtualOffset.getQuantity() == 0) means it
  // just gives the address back. In CIR a `cir.derived_class` is created and
  // made into a nop later on during lowering.
  Address derivedAddr = builder.createDerivedClassAddr(
      loc, baseAddr, derivedValueTy, nonVirtualOffset.getQuantity(),
      /*assumeNotNull=*/!nullCheckValue);

  if (!nonVirtualOffset.isZero())
    derivedAddr = derivedAddr.withAlignment(cgm.getClassPointerAlignment(derived));

  return derivedAddr;
}

Address CIRGenFunction::getAddressOfBaseClass(
    Address value, const CXXRecordDecl *derived,
    llvm::iterator_range<CastExpr::path_const_iterator> path,
    bool nullCheckValue, SourceLocation loc) {
  assert(!path.empty() && "Base path should not be empty!");

  CastExpr::path_const_iterator start = path.begin();
  const CXXRecordDecl *vBase = nullptr;

  if ((*path.begin())->isVirtual()) {
    vBase = (*start)->getType()->castAsCXXRecordDecl();
    ++start;
  }

  // Compute the static offset of the ultimate destination within its
  // allocating subobject (the virtual base, if there is one, or else
  // the "complete" object that we see).
  CharUnits nonVirtualOffset = cgm.computeNonVirtualBaseClassOffset(
      vBase ? vBase : derived, {start, path.end()});

  // If there's a virtual step, we can sometimes "devirtualize" it.
  // For now, that's limited to when the derived type is final.
  // TODO: "devirtualize" this for accesses to known-complete objects.
  if (vBase && derived->hasAttr<FinalAttr>()) {
    const ASTRecordLayout &layout = getContext().getASTRecordLayout(derived);
    CharUnits vBaseOffset = layout.getVBaseClassOffset(vBase);
    nonVirtualOffset += vBaseOffset;
    vBase = nullptr; // we no longer have a virtual step
  }

  // Get the base pointer type.
  mlir::Type baseValueTy = convertType((path.end()[-1])->getType());
  CanQualType derivedTy = getContext().getCanonicalTagType(derived);
  CharUnits derivedAlign = cgm.getClassPointerAlignment(derived);
  // If there is no virtual base and offset is zero, just bitcast.
  // OGCG doesn't null-check this case either (CGClass.cpp:326-333).
  if (nonVirtualOffset.isZero() && !vBase) {
    if (sanitizePerformTypeCheck()) {
      SanitizerSet skippedChecks;
      skippedChecks.set(SanitizerKind::Null, !nullCheckValue);
      emitTypeCheck(TCK_Upcast, loc, value.getPointer(), derivedTy,
                    derivedAlign, skippedChecks);
    }
    return builder.createBaseClassAddr(getLoc(loc), value, baseValueTy, 0,
                                       /*assumeNotNull=*/true);
  }

  auto emitUpcastTypeCheck = [&] {
    if (!sanitizePerformTypeCheck())
      return;
    SanitizerSet skippedChecks;
    skippedChecks.set(SanitizerKind::Null, true);
    emitTypeCheck(vBase ? TCK_UpcastToVirtualBase : TCK_Upcast, loc,
                  value.getPointer(), derivedTy, derivedAlign, skippedChecks);
  };

  // Preserve the existing fixed-offset lowering for non-virtual base casts.
  // The refactor below is only required for virtual bases, where the offset is
  // computed by dereferencing the vtable and must not execute on null.
  if (!vBase) {
    emitUpcastTypeCheck();
    Address result = applyNonVirtualAndVirtualOffset(
        getLoc(loc), *this, value, nonVirtualOffset, /*virtualOffset=*/nullptr,
        derived, vBase, baseValueTy, /*assumeNotNull=*/!nullCheckValue);
    return result.withElementType(builder, baseValueTy);
  }

  auto computeBaseAddr = [&](bool assumeNotNull) -> Address {
    if (vBase) {
      mlir::Value virtualOffset =
          cgm.getCXXABI().getVirtualBaseClassOffset(getLoc(loc), *this, value,
                                                    derived, vBase);
      Address result = applyNonVirtualAndVirtualOffset(
          getLoc(loc), *this, value, nonVirtualOffset, virtualOffset, derived,
          vBase, baseValueTy, assumeNotNull);
      return result.withElementType(builder, baseValueTy);
    }

    Address result = applyNonVirtualAndVirtualOffset(
        getLoc(loc), *this, value, nonVirtualOffset, /*virtualOffset=*/nullptr,
        derived, vBase, baseValueTy, assumeNotNull);
    return result.withElementType(builder, baseValueTy);
  };

  auto getResultAlignment = [&] {
    if (vBase) {
      CharUnits resultAlign =
          cgm.getVBaseAlignment(value.getAlignment(), derived, vBase);
      return resultAlign.alignmentAtOffset(nonVirtualOffset);
    }
    return value.getAlignment().alignmentAtOffset(nonVirtualOffset);
  };

  if (nullCheckValue) {
    mlir::Location cirLoc = getLoc(loc);
    mlir::Value ptr = value.getPointer();
    auto ptrTy = mlir::cast<cir::PointerType>(ptr.getType());
    mlir::Value nullPtr = builder.getNullPtr(ptrTy, cirLoc);
    mlir::Value isNotNull =
        builder.createCompare(cirLoc, cir::CmpOpKind::ne, ptr, nullPtr);

    // The null result must live in the same address space as the source
    // pointer so it matches the true branch's adjusted pointer; otherwise the
    // ternary's two yields disagree on address space.
    auto basePtrTy = builder.getPointerTo(baseValueTy, ptrTy.getAddrSpace());
    auto ternary = cir::TernaryOp::create(
        builder, cirLoc, isNotNull,
        /*trueBuilder=*/
        [&](mlir::OpBuilder &, mlir::Location) {
          emitUpcastTypeCheck();
          Address result = computeBaseAddr(/*assumeNotNull=*/true);
          builder.createYield(cirLoc, result.getPointer());
        },
        /*falseBuilder=*/
        [&](mlir::OpBuilder &, mlir::Location) {
          mlir::Value nullResult = builder.getNullPtr(basePtrTy, cirLoc);
          builder.createYield(cirLoc, nullResult);
        });

    return Address(ternary.getResult(), getResultAlignment());
  }

  emitUpcastTypeCheck();
  return computeBaseAddr(/*assumeNotNull=*/true);
}

// TODO(cir): this can be shared with LLVM codegen.
bool CIRGenFunction::shouldEmitVTableTypeCheckedLoad(const CXXRecordDecl *rd) {
  if ((!cgm.getCodeGenOpts().WholeProgramVTables ||
       !cgm.hasHiddenLTOVisibility(rd)) &&
      !cgm.getCodeGenOpts().DevirtualizeSpeculatively)
    return false;

  // CIR lowers the virtual-call slot load normally and rewrites the translated
  // LLVM IR sequence to llvm.type.checked.load later in LowerToLLVMIR.
  if (cgm.getCodeGenOpts().VirtualFunctionElimination)
    return true;

  // TODO(cir): match OGCG's CFI vcall trap-mode checked-load path too.
  if (!getLangOpts().Sanitize.has(SanitizerKind::CFIVCall) ||
      !cgm.getCodeGenOpts().SanitizeTrap.has(SanitizerKind::CFIVCall))
    return false;

  std::string typeName = rd->getQualifiedNameAsString();
  return !getContext().getNoSanitizeList().containsType(
      SanitizerKind::CFIVCall, typeName);
}

void CIRGenFunction::emitVTablePtrCheckForCast(QualType T, Address derived,
                                               bool mayBeNull,
                                               CFICastCheckKind kind,
                                               SourceLocation loc) {
  if (!getLangOpts().CPlusPlus)
    return;

  const auto *classDecl = T->getAsCXXRecordDecl();
  if (!classDecl)
    return;

  if (!classDecl->isCompleteDefinition() || !classDecl->isDynamicClass())
    return;

  SanitizerMask sanitizerKind = getSanitizerKindForCFICastCheck(kind);
  if (!sanOpts.has(SanitizerKind::CFICastStrict))
    classDecl = leastDerivedClassWithSameLayout(classDecl);

  if (!cgm.getCodeGenOpts().SanitizeCfiCrossDso &&
      !cgm.hasHiddenLTOVisibility(classDecl))
    return;

  std::string typeName = classDecl->getQualifiedNameAsString();
  if (getContext().getNoSanitizeList().containsType(sanitizerKind, typeName))
    return;

  mlir::Location checkLoc = getLoc(loc);
  QualType canonicalType = getContext().getCanonicalTagType(classDecl);
  StringRef handlerKind = "abort";
  if (cgm.getCodeGenOpts().SanitizeTrap.has(sanitizerKind))
    handlerKind = "trap";
  else if (cgm.getCodeGenOpts().SanitizeRecover.has(sanitizerKind))
    handlerKind = "recover";
  bool noMergeHandler =
      cgm.getCodeGenOpts().OptimizationLevel == 0 ||
      !cgm.getCodeGenOpts().SanitizeMergeHandlers.has(
          kind == CFICastCheckKind::DerivedCast
              ? SanitizerKind::SO_CFIDerivedCast
              : SanitizerKind::SO_CFIUnrelatedCast);
  uint8_t typeCheckKind =
      kind == CFICastCheckKind::DerivedCast ? 2 : 3;
  auto emitCheck = [&](mlir::Location ifLoc) {
    mlir::Value vtable =
        getVTablePtr(ifLoc, derived, classDecl, VTableAuthMode::UnsafeUbsanStrip);
    // The vtable load is part of the sanitizer check, mark it !nosanitize
    // to match OGCG (CGClass.cpp EmitVTablePtrCheckForCast).
    markNoSanitize(vtable.getDefiningOp());
    if (vtable.getType() != builder.getVoidPtrTy()) {
      vtable = builder.createBitcast(ifLoc, vtable, builder.getVoidPtrTy());
      markNoSanitize(vtable.getDefiningOp());
    }

    auto stubFnTy = cir::FuncType::get(
        {builder.getVoidPtrTy(), builder.getVoidPtrTy()}, builder.getBoolTy());
    auto stubFn = cgm.createRuntimeFunction(
        stubFnTy, "__cir_type_test", /*extraAttrs=*/{},
        /*isLocal=*/false, /*assumeConvergent=*/false);
    mlir::Value nullTypeId = builder.getNullPtr(builder.getVoidPtrTy(), ifLoc);
    emitSanitizerStatReportStub(
        ifLoc, kind == CFICastCheckKind::DerivedCast
                   ? CFISanitizerStatKind::DerivedCast
                   : CFISanitizerStatKind::UnrelatedCast);
    auto typeTest = builder.createCallOp(ifLoc, stubFn, {vtable, nullTypeId});
    typeTest->setDiscardableAttr(
        "cir.type_test_id",
        builder.getStringAttr(cgm.createMetadataIdentifierStringForType(
            getContext().getCanonicalTagType(classDecl))));
    typeTest->setDiscardableAttr(
        "cir.nosanitize", mlir::UnitAttr::get(builder.getContext()));

    mlir::Value passed = typeTest.getResult();
    markNoSanitize(passed.getDefiningOp());

    auto ifOp = cir::IfOp::create(
        builder, ifLoc, passed, /*withElseRegion=*/true,
        [&](mlir::OpBuilder &, mlir::Location yieldLoc) {
          builder.createYield(yieldLoc);
        },
        [&](mlir::OpBuilder &, mlir::Location failLoc) {
          if (handlerKind == "trap") {
            auto trap = cir::TrapOp::create(builder, failLoc);
            trap->setAttr(
                "cir.ubsantrap_handler",
                mlir::IntegerAttr::get(
                    mlir::IntegerType::get(builder.getContext(), 8), 2));
            if (noMergeHandler)
              trap->setAttr("cir.nomerge",
                            mlir::UnitAttr::get(builder.getContext()));
            markNoSanitize(trap);
            return;
          }

          auto allVtablesTypeTest = builder.createCallOp(
              failLoc, stubFn, {vtable, nullTypeId});
          allVtablesTypeTest->setDiscardableAttr(
              "cir.type_test_id", builder.getStringAttr("all-vtables"));
          allVtablesTypeTest->setDiscardableAttr(
              "cir.nosanitize", mlir::UnitAttr::get(builder.getContext()));
          mlir::Value validVtable = allVtablesTypeTest.getResult();
          markNoSanitize(validVtable.getDefiningOp());

          auto stubFnTy = cir::FuncType::get({cgm.uIntPtrTy, cgm.uIntPtrTy},
                                             builder.getVoidTy());
          auto failStub = cgm.createRuntimeFunction(
              stubFnTy, "__cir_ubsan_cfi_check_fail", /*attrs=*/{},
              /*isLocal=*/false, /*assumeConvergent=*/false);
          mlir::Value vtableInt =
              builder.createPtrToInt(vtable, cgm.uIntPtrTy);
          markNoSanitize(vtableInt.getDefiningOp());
          mlir::Value validVtableInt =
              builder.createBoolToInt(validVtable, cgm.uIntPtrTy);
          markNoSanitize(validVtableInt.getDefiningOp());
          auto call = builder.createCallOp(failLoc, failStub,
                                           {vtableInt, validVtableInt});
          call->setDiscardableAttr("cir.ubsan.handler_name",
                                   builder.getStringAttr("cfi_check_fail"));
          call->setDiscardableAttr("cir.ubsan.handler_kind",
                                   builder.getStringAttr(handlerKind));
          call->setDiscardableAttr("cir.ubsan.type_check_kind",
                                   builder.getI8IntegerAttr(typeCheckKind));
          call->setDiscardableAttr("cir.nosanitize",
                                   mlir::UnitAttr::get(builder.getContext()));
          if (noMergeHandler)
            call->setAttr("cir.call_nomerge",
                          mlir::UnitAttr::get(builder.getContext()));
          setUBSanSourceLocationAttrs(*this, call, loc);
          setUBSanTypeNameAttr(*this, call, canonicalType);
          setUBSanTypeDescriptorAttrs(*this, call, canonicalType);
          if (cgm.getCodeGenOpts().SanitizeMinimalRuntime)
            call->setAttr("cir.cfi.mfcall.minimal_runtime",
                          mlir::UnitAttr::get(builder.getContext()));
          if (cgm.getCodeGenOpts().SanitizeHandlerPreserveAllRegs)
            call->setAttr("cir.cfi.mfcall.preserve_all_regs",
                          mlir::UnitAttr::get(builder.getContext()));

          if (handlerKind == "abort") {
            auto unreachable = cir::UnreachableOp::create(builder, failLoc);
            markNoSanitize(unreachable);
          } else {
            auto yield = cir::YieldOp::create(builder, failLoc);
            markNoSanitize(yield);
          }
        });
    setSanitizerBranchAttrs(builder, ifOp);
  };

  if (!mayBeNull) {
    emitCheck(checkLoc);
    return;
  }

  mlir::Value nullPtr =
      builder.getNullPtr(derived.getPointer().getType(), checkLoc);
  mlir::Value isNotNull = cir::CmpOp::create(
      builder, checkLoc, cir::CmpOpKind::ne, derived.getPointer(), nullPtr);
  markNoSanitize(isNotNull.getDefiningOp());

  auto ifOp = cir::IfOp::create(
      builder, checkLoc, isNotNull, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &, mlir::Location ifLoc) {
        emitCheck(ifLoc);
        builder.createYield(ifLoc);
      });
  // The null-check branch only gets !nosanitize, not !prof weights.
  // OGCG's null-check ConditionalBranch only carries !nosanitize.
  markNoSanitize(ifOp);
}

mlir::Value CIRGenFunction::getVTablePtr(mlir::Location loc, Address thisAddr,
                                         const CXXRecordDecl *rd,
                                         VTableAuthMode authMode) {
  auto srcPtrTy = mlir::cast<cir::PointerType>(thisAddr.getPointer().getType());
  auto vptrPtrTy = cir::PointerType::get(
      cir::VPtrType::get(builder.getContext()), srcPtrTy.getAddrSpace());
  auto vtablePtr = cir::VTableGetVPtrOp::create(builder, loc, vptrPtrTy,
                                                thisAddr.getPointer());
  Address vtablePtrAddr = Address(vtablePtr, thisAddr.getAlignment());

  auto vtableLoad = builder.createLoad(loc, vtablePtrAddr);
  if (cgm.getCodeGenOpts().OptimizationLevel > 0 &&
      cgm.getCodeGenOpts().StrictVTablePointers)
    vtableLoad->setAttr("cir.invariant_group",
                        mlir::UnitAttr::get(builder.getContext()));

  // Decorate the vtable pointer load with TBAA information.
  cgm.decorateOperationWithTBAA(
      vtableLoad, cgm.getTBAAVTablePtrAccessInfo(
                      cir::VPtrType::get(builder.getContext())));

  mlir::Value vtable = vtableLoad;

  if (auto authInfo = getVTablePointerAuthInfo(*this, loc, rd, thisAddr)) {
    if (authMode != VTableAuthMode::UnsafeUbsanStrip) {
      vtable = emitVTablePointerAuthAuth(*this, loc, *authInfo, vtable);
      if (authMode == VTableAuthMode::MustTrap) {
        mlir::Value trapPtr =
            builder.createBitcast(loc, vtable, cgm.getDefaultGlobalsInt8PtrTy());
        Address trapAddr(trapPtr, builder.getUInt8Ty(), getPointerAlign());
        (void)builder.createLoad(loc, trapAddr, /*isVolatile=*/true);
      }
    } else {
      vtable = emitVTablePointerAuthStrip(*this, loc, vtable, /*key=*/0);
    }
  }

  return vtable;
}

void CIRGenFunction::emitVTableAssumptionLoad(mlir::Location loc,
                                              const VPtr &vptr,
                                              Address thisAddr) {
  mlir::Value vtableAddressPoint =
      cgm.getCXXABI().getVTableAddressPoint(vptr.base, vptr.vtableClass);
  if (!vtableAddressPoint)
    return;

  // We can just use the base offset in the complete class.
  CharUnits nonVirtualOffset = vptr.base.getBaseOffset();

  if (!nonVirtualOffset.isZero()) {
    mlir::Type baseValueTy =
        convertType(getContext().getCanonicalTagType(vptr.base.getBase()));
    thisAddr = applyNonVirtualAndVirtualOffset(
        loc, *this, thisAddr, nonVirtualOffset, /*virtualOffset=*/nullptr,
        vptr.vtableClass, vptr.nearestVBase, baseValueTy);
  }

  mlir::Value vptrValue = getVTablePtr(loc, thisAddr, vptr.vtableClass);
  mlir::Value cmp = builder.createCompare(loc, cir::CmpOpKind::eq, vptrValue,
                                          vtableAddressPoint);
  cir::AssumeOp::create(builder, loc, cmp);
}

void CIRGenFunction::emitVTableAssumptionLoads(
    const CXXRecordDecl *classDecl, Address thisAddr) {
  if (cgm.getCXXABI().doStructorsInitializeVPtrs(classDecl))
    for (const VPtr &vptr : getVTablePointers(classDecl))
      emitVTableAssumptionLoad(getLoc(classDecl->getLocation()), vptr,
                               thisAddr);
}

static bool isMemcpyEquivalentSpecialMember(const CXXMethodDecl *d) {
  auto *cd = dyn_cast<CXXConstructorDecl>(d);
  if (!(cd && cd->isCopyOrMoveConstructor()) &&
      !d->isCopyAssignmentOperator() && !d->isMoveAssignmentOperator())
    return false;

  // We can emit a memcpy for a trivial copy or move constructor/assignment.
  if (d->isTrivial() && !d->getParent()->mayInsertExtraPadding())
    return true;

  // We *must* emit a memcpy for a defaulted union copy or move op.
  if (d->getParent()->isUnion() && d->isDefaulted())
    return true;

  return false;
}

void CIRGenFunction::emitCXXConstructorCall(const clang::CXXConstructorDecl *d,
                                            clang::CXXCtorType type,
                                            bool forVirtualBase,
                                            bool delegating,
                                            AggValueSlot thisAVS,
                                            const clang::CXXConstructExpr *e) {
  CallArgList args;
  Address thisAddr = thisAVS.getAddress();
  QualType thisType = d->getThisType();
  mlir::Value thisPtr = thisAddr.getPointer();

  // The address space of the slot (e.g., global) may differ from the address
  // space of the constructor's implicit "this" parameter (e.g., generic). Cast
  // if needed.
  LangAS slotAS = thisAVS.getQualifiers().getAddressSpace();
  LangAS thisAS = d->getFunctionObjectParameterType().getAddressSpace();
  if (slotAS != thisAS) {
    auto ptrTy = mlir::cast<cir::PointerType>(thisPtr.getType());
    mlir::ptr::MemorySpaceAttrInterface expectedAddrSpace =
        cir::toCIRLangAddressSpaceAttr(builder.getContext(), thisAS);
    auto expectedPtrTy =
        builder.getPointerTo(ptrTy.getPointee(), expectedAddrSpace);
    thisPtr = builder.createAddrSpaceCast(thisPtr, expectedPtrTy);
  }

  args.add(RValue::get(thisPtr), thisType);

  // If this is a trivial constructor, just emit what's needed. If this is a
  // union copy constructor, we must emit a memcpy, because the AST does not
  // model that copy.
  if (isMemcpyEquivalentSpecialMember(d)) {
    assert(e->getNumArgs() == 1 && "unexpected argcount for trivial ctor");

    const Expr *arg = e->getArg(0);
    LValue src = emitLValue(arg);
    QualType destTy = getContext().getCanonicalTagType(d->getParent());
    LValue dest = makeAddrLValue(thisAddr, destTy);
    emitAggregateCopy(dest, src, src.getType(), thisAVS.mayOverlap());
    return;
  }

  const FunctionProtoType *fpt = d->getType()->castAs<FunctionProtoType>();

  EvaluationOrder order = e->isListInitialization()
                              ? EvaluationOrder::ForceLeftToRight
                              : EvaluationOrder::Default;

  emitCallArgs(args, fpt, e->arguments(), e->getConstructor(),
               /*ParamsToSkip=*/0, order);

  emitCXXConstructorCall(d, type, forVirtualBase, delegating, thisAddr, args,
                         thisAVS.mayOverlap(), e->getExprLoc(),
                         thisAVS.isSanitizerChecked());
}

static bool canEmitDelegateCallArgs(CIRGenFunction &cgf,
                                    const CXXConstructorDecl *ctor,
                                    CXXCtorType type, CallArgList &args) {
  // We can't forward a variadic call.
  if (ctor->isVariadic())
    return false;

  if (cgf.getTarget().getCXXABI().areArgsDestroyedLeftToRightInCallee()) {
    // If the parameters are callee-cleanup, it's not safe to forward.
    for (auto *p : ctor->parameters())
      if (p->needsDestruction(cgf.getContext()))
        return false;

    // Likewise if they're inalloca (still NYI in call lowering).
  }

  // Anything else should be OK.
  return true;
}

void CIRGenFunction::emitCXXConstructorCall(
    const CXXConstructorDecl *d, CXXCtorType type, bool forVirtualBase,
    bool delegating, Address thisAddr, CallArgList &args,
    AggValueSlot::Overlap_t overlap, SourceLocation loc,
    bool newPointerIsChecked) {

  const CXXRecordDecl *crd = d->getParent();

  if (!newPointerIsChecked)
    emitTypeCheck(TCK_ConstructorCall, loc, thisAddr.getPointer(),
                  getContext().getCanonicalTagType(crd), CharUnits::Zero());

  // Trivial default constructors have empty bodies — no code to emit.
  // OGCG returns early here (CGClass.cpp:2351-2354). The value-initialization
  // (if any) is handled by emitNullInitialization in the caller.
  if (d->isTrivial() && d->isDefaultConstructor()) {
    assert(args.size() == 1 && "trivial default ctor with args");
    return;
  }

  // If this is a trivial constructor, just emit what's needed. If this is a
  // union copy constructor, we must emit a memcpy, because the AST does not
  // model that copy.
  if (isMemcpyEquivalentSpecialMember(d)) {
    assert(args.size() == 2 && "unexpected argcount for trivial ctor");
    QualType srcTy = d->getParamDecl(0)->getType().getNonReferenceType();
    Address src = makeNaturalAddressForPointer(
        args[1].getKnownRValue().getValue(), srcTy, CharUnits::Zero());
    LValue srcLVal = makeAddrLValue(src, srcTy);
    QualType destTy = getContext().getCanonicalTagType(crd);
    LValue destLVal = makeAddrLValue(thisAddr, destTy);
    emitAggregateCopy(destLVal, srcLVal, srcLVal.getType(), overlap);
    return;
  }

  bool passPrototypeArgs = true;

  // Check whether we can actually emit the constructor before trying to do so.
  if (auto inherited = d->getInheritedConstructor()) {
    passPrototypeArgs = getTypes().inheritingCtorHasParams(inherited, type);
    if (passPrototypeArgs && !canEmitDelegateCallArgs(*this, d, type, args)) {
      emitInlinedInheritingCXXConstructorCall(d, type, forVirtualBase,
                                              delegating, args);
      return;
    }
    // When the base-object variant of an inheriting constructor lives in a
    // virtual base, the definition omits the forwarded parameters (it only
    // does VTT setup, not the actual base construction). OGCG allows calling
    // with extra args that the callee ignores via opaque pointers, but CIR's
    // typed function signatures require the call operands to match the
    // callee's parameter count. Strip the forwarded prototype args, keeping
    // only 'this'.
    if (!passPrototypeArgs)
      args.truncate(1);
  }

  // Insert any ABI-specific implicit constructor arguments.
  CIRGenCXXABI::AddedStructorArgCounts extraArgs =
      cgm.getCXXABI().addImplicitConstructorArgs(*this, d, type, forVirtualBase,
                                                 delegating, args);

  // Emit the call.
  auto calleePtr = cgm.getAddrOfCXXStructor(GlobalDecl(d, type));
  const CIRGenFunctionInfo &info = cgm.getTypes().arrangeCXXConstructorCall(
      args, d, type, extraArgs.prefix, extraArgs.suffix, passPrototypeArgs);
  CIRGenCallee callee = CIRGenCallee::forDirect(calleePtr, GlobalDecl(d, type));
  cir::CIRCallOpInterface c;
  emitCall(info, callee, ReturnValueSlot(), args, &c, getLoc(loc));

  // Generate vtable assumptions if we're constructing a complete object
  // with a vtable. We don't do this for base subobjects for two reasons:
  // first, it's incorrect for classes with virtual bases, and second, we're
  // about to overwrite the vptrs anyway.
  if (cgm.getCodeGenOpts().OptimizationLevel > 0 && crd->isDynamicClass() &&
      type != Ctor_Base &&
      cgm.getCXXABI().canSpeculativelyEmitVTable(crd) &&
      cgm.getCodeGenOpts().StrictVTablePointers)
    emitVTableAssumptionLoads(crd, thisAddr);
}

void CIRGenFunction::emitInheritedCXXConstructorCall(
    const CXXConstructorDecl *d, bool forVirtualBase, Address thisAddr,
    bool inheritedFromVBase, const CXXInheritedCtorInitExpr *e) {
  CallArgList args;
  CallArg thisArg(RValue::get(getAsNaturalPointerTo(
                      thisAddr, d->getThisType()->getPointeeType())),
                  d->getThisType());

  // Forward the parameters.
  if (inheritedFromVBase &&
      cgm.getTarget().getCXXABI().hasConstructorVariants()) {
    // Nothing to do; this construction is not responsible for constructing
    // the base class containing the inherited constructor.
    args.push_back(thisArg);
  } else if (!cxxInheritedCtorInitExprArgs.empty()) {
    // The inheriting constructor was inlined; just inject its arguments.
    assert(cxxInheritedCtorInitExprArgs.size() >= d->getNumParams() &&
           "wrong number of parameters for inherited constructor call");
    args = cxxInheritedCtorInitExprArgs;
    args[0] = thisArg;
  } else {
    // The inheriting constructor was not inlined. Emit delegating arguments.
    args.push_back(thisArg);
    const auto *outerCtor = cast<CXXConstructorDecl>(curCodeDecl);
    assert(outerCtor->getNumParams() == d->getNumParams());
    assert(!outerCtor->isVariadic() && "should have been inlined");
    for (const auto *param : outerCtor->parameters()) {
      assert(getContext().hasSameUnqualifiedType(
          outerCtor->getParamDecl(param->getFunctionScopeIndex())->getType(),
          param->getType()));
      emitDelegateCallArg(args, param, e->getLocation());

      // Forward __attribute__((pass_object_size)).
      // Matches OGCG CGClass.cpp EmitInheritedCXXConstructorCall.
      if (param->hasAttr<PassObjectSizeAttr>()) {
        auto *posParam = sizeArguments[param];
        assert(posParam && "missing pass_object_size value for forwarding");
        emitDelegateCallArg(args, posParam, e->getLocation());
      }
    }
  }

  emitCXXConstructorCall(d, Ctor_Base, forVirtualBase, /*delegating=*/false,
                         thisAddr, args, AggValueSlot::MayOverlap,
                         e->getLocation(), /*newPointerIsChecked=*/true);
}

void CIRGenFunction::emitInlinedInheritingCXXConstructorCall(
    const CXXConstructorDecl *ctor, CXXCtorType ctorType, bool forVirtualBase,
    bool delegating, CallArgList &args) {
  GlobalDecl gd(ctor, ctorType);
  InlinedInheritingConstructorScope scope(*this, gd);
  RunCleanupsScope runCleanups(*this);

  // Save the arguments to be passed to the inherited constructor.
  cxxInheritedCtorInitExprArgs = args;

  FunctionArgList params;
  QualType retType = buildFunctionArgList(curGD, params);

  // Insert any ABI-specific implicit constructor arguments.
  cgm.getCXXABI().addImplicitConstructorArgs(*this, ctor, ctorType,
                                             forVirtualBase, delegating, args);

  // Emit a simplified prolog. We only need to emit the implicit params.
  // Create allocas for implicit params (this, VTT) and store arg values.
  assert(args.size() >= params.size() && "too few arguments for call");
  mlir::Location loc = getLoc(ctor->getLocation());
  for (unsigned i = 0, n = args.size(); i != n; ++i) {
    if (i < params.size() && isa<ImplicitParamDecl>(params[i])) {
      RValue rv = args[i].getKnownRValue();
      assert(!rv.isComplex() && "complex indirect params not supported");
      mlir::Value val =
          rv.isScalar() ? rv.getValue()
                        : rv.getAggregateAddress().emitRawPointer();
      QualType paramTy = params[i]->getType();
      CharUnits alignment = getContext().getDeclAlign(params[i]);
      Address paramAddr = createTempAlloca(
          convertType(paramTy), alignment, loc, params[i]->getName(),
          /*arraySize=*/nullptr, /*allocaAddr=*/nullptr,
          /*ip=*/{}, cgm.getLangTempAllocaAddressSpace());
      builder.CIRBaseBuilderTy::createStore(loc, val,
                                            paramAddr.emitRawPointer());
      // Insert directly into localDeclMap (not setAddrOfLocalVar) because
      // the symbol table may not have an active scope in the inlined
      // inherited constructor context.
      localDeclMap.insert({params[i], paramAddr});
    }
  }

  // Create a return value slot if the ABI implementation wants one.
  if (!retType->isVoidType())
    returnValue = createMemTemp(retType, loc, "retval.inhctor");

  cgm.getCXXABI().emitInstanceFunctionProlog(ctor->getLocation(), *this);
  cxxThisValue = cxxabiThisValue;

  // Directly emit the constructor initializers.
  emitCtorPrologue(ctor, ctorType, params);
}
