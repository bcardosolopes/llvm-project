//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Builtin calls as CIR or a function call to be
// later resolved.
//
//===----------------------------------------------------------------------===//

#include "CIRGenCall.h"
#include "CIRGenConstantEmitter.h"
#include "CIRGenFunction.h"
#include "CIRGenModule.h"
#include "CIRGenSanitizer.h"
#include "CIRGenValue.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Value.h"
#include "mlir/Support/LLVM.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OSLog.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Basic/XRayInstr.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::CIRGen;
using namespace llvm;

// Location-metadata keys carrying a `__builtin_verbose_trap` category/message
// from CIRGen to the CIR->LLVM lowering (kept in sync with LowerToLLVM.cpp).
static constexpr llvm::StringLiteral kTrapMsgCategoryKey =
    "cir.trap_msg_category";
static constexpr llvm::StringLiteral kTrapMsgMessageKey = "cir.trap_msg_message";

// Record the verbose-trap category/message on `loc` (merging into any existing
// FusedLoc metadata dictionary) so the lowering can synthesize the artificial
// `__clang_trap_msg$<category>$<message>` DISubprogram and inlined-at trap
// location, mirroring classic CGDebugInfo::CreateTrapFailureMessageFor.
static mlir::Location attachTrapMessage(mlir::Location loc,
                                        llvm::StringRef category,
                                        llvm::StringRef message) {
  auto *ctx = loc.getContext();
  mlir::NamedAttrList attrs;
  llvm::SmallVector<mlir::Location, 2> locs = {loc};
  if (auto fusedLoc = mlir::dyn_cast<mlir::FusedLoc>(loc)) {
    if (auto dict = mlir::dyn_cast_or_null<mlir::DictionaryAttr>(
            fusedLoc.getMetadata()))
      attrs.append(dict.getValue().begin(), dict.getValue().end());
    locs.assign(fusedLoc.getLocations().begin(), fusedLoc.getLocations().end());
  }
  attrs.set(kTrapMsgCategoryKey, mlir::StringAttr::get(ctx, category));
  attrs.set(kTrapMsgMessageKey, mlir::StringAttr::get(ctx, message));
  return mlir::FusedLoc::get(locs, mlir::DictionaryAttr::get(ctx, attrs), ctx);
}

static void emitBuiltinArgCheck(CIRGenFunction &cgf,
                                CIRGenFunction::TypeCheckKind kind,
                                Address addr, const Expr *arg,
                                const FunctionDecl *fd, unsigned parmNum) {
  mlir::Value ptr = addr.getPointer();
  cgf.emitNonNullArgCheck(RValue::get(ptr), arg->getType(), arg->getExprLoc(),
                          CIRGenFunction::AbstractCallee(fd), parmNum);

  if (cgf.sanOpts.has(SanitizerKind::Alignment)) {
    SanitizerSet skippedChecks;
    skippedChecks.set(SanitizerKind::All);
    skippedChecks.clear(SanitizerKind::Alignment);
    if (auto *ce = dyn_cast<ImplicitCastExpr>(arg))
      if (ce->getCastKind() == CK_BitCast)
        arg = ce->getSubExpr();
    // Building the memcpy/memmove call bitcasts each pointer argument to
    // `void *`, so `ptr` here is that bitcast rather than the underlying
    // alloca/global. Strip a no-op pointer bitcast so `emitTypeCheck`'s
    // known-base-alignment elision can see through to a statically-aligned
    // base and skip the redundant alignment check, matching classic's
    // `EmitTypeCheck` which operates on `Ptr->stripPointerCasts()`
    // (CGExpr.cpp:767, elision at CGExpr.cpp:854-855).
    if (auto castOp = ptr.getDefiningOp<cir::CastOp>())
      if (castOp.getKind() == cir::CastKind::bitcast)
        ptr = castOp.getSrc();
    cgf.emitTypeCheck(kind, arg->getExprLoc(), ptr, arg->getType(),
                      addr.getAlignment(), skippedChecks);
  }
}

// Forward declaration — defined later, used in builtin ctz/clz sanitizer checks.
static void emitBuiltinSanitizerCheck(CIRGenFunction &cgf,
                                      mlir::Value checkCondition,
                                      SourceLocation exprLoc,
                                      uint8_t checkKind);

static void maybeSetCallStrictFPAttr(CIRGenFunction &cgf, const Expr *expr,
                                     mlir::Operation *op) {
  if (!op)
    return;

  FPOptions fpFeatures = expr->getFPFeaturesInEffect(cgf.getLangOpts());
  if (fpFeatures.isFPConstrained() || cgf.IsFPConstrained)
    op->setAttr("cir.call_strictfp",
                mlir::UnitAttr::get(&cgf.getMLIRContext()));
}

static RValue emitLibraryCall(CIRGenFunction &cgf, const FunctionDecl *fd,
                              const CallExpr *e, mlir::Operation *calleeValue,
                              cir::CIRCallOpInterface *emittedCallOp = nullptr) {
  CIRGenCallee callee = CIRGenCallee::forDirect(calleeValue, GlobalDecl(fd));
  cir::CIRCallOpInterface callOp;
  RValue result =
      cgf.emitCall(e->getCallee()->getType(), callee, e, ReturnValueSlot(),
                   &callOp);
  if (emittedCallOp)
    *emittedCallOp = callOp;

  // Match OGCG CGBuiltin.cpp:emitLibraryCall by marking eligible FP math
  // libcalls with errno TBAA metadata ("int" access tag). We carry a CIR
  // marker here and materialize !tbaa on the lowered LLVM call later.
  if (callOp && fd) {
    unsigned builtinID = fd->getBuiltinID();
    if (builtinID) {
      ASTContext &context = cgf.getContext();
      bool isErrnoMathBuiltin =
          context.BuiltinInfo.isConstWithoutErrnoAndExceptions(builtinID);
      bool isFPConstrained =
          e->getFPFeaturesInEffect(cgf.getLangOpts()).isFPConstrained();
      bool hasIntTBAA = static_cast<bool>(cgf.cgm.getTBAAAccessTagInfo(
          cgf.cgm.getTBAAAccessInfo(context.IntTy)));

      if (isErrnoMathBuiltin && cgf.getLangOpts().MathErrno &&
          !isFPConstrained && result.isScalar() && hasIntTBAA) {
        callOp->setAttr("cir.errno_tbaa",
                        mlir::UnitAttr::get(&cgf.getMLIRContext()));
      }
    }
  }

  // Apply fast-math flags from the CallExpr's FP options to the call op.
  // OGCG sets CGFPOptionsRAII here (CGBuiltin.cpp:807), which stamps FMF on
  // the CallInst via IRBuilder. CIR has no IRBuilder FMF state, so attach
  // FMF explicitly as a discardable attribute.
  if (callOp) {
    FPOptions fpOpts = e->getFPFeaturesInEffect(cgf.getLangOpts());
    cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(fpOpts);
    if (fmf != cir::FastMathFlags::none) {
      auto &ctx = cgf.getCIRGenModule().getMLIRContext();
      callOp->setAttr(
          "cir.fmf",
          mlir::IntegerAttr::get(mlir::IntegerType::get(&ctx, 32),
                                 static_cast<uint32_t>(fmf)));
    }
  }

  // Emit "int" TBAA metadata on FP math libcalls that are const except for
  // errno. Matches OGCG CGBuiltin.cpp:815-829.
  if (callOp) {
    if (unsigned builtinID = fd->getBuiltinID()) {
      ASTContext &context = cgf.getContext();
      if (context.BuiltinInfo.isConstWithoutErrnoAndExceptions(builtinID) &&
          cgf.getCIRGenModule().getLangOpts().MathErrno &&
          !e->getFPFeaturesInEffect(cgf.getLangOpts()).isFPConstrained() &&
          result.isScalar()) {
        TBAAAccessInfo tbaaInfo =
            cgf.getCIRGenModule().getTBAAAccessInfo(context.IntTy);
        if (auto tag =
                cgf.getCIRGenModule().getTBAAAccessTagInfo(tbaaInfo))
          callOp->setAttr("cir.call_tbaa", tag);
      }
    }
  }

  return result;
}

static mlir::Operation *getBuiltinLibcallCallee(CIRGenModule &cgm,
                                                const FunctionDecl *fd,
                                                unsigned builtinID) {
  assert(fd && "expected builtin function declaration");
  ASTContext &context = fd->getASTContext();
  if (context.BuiltinInfo.isLibFunction(builtinID))
    return cgm.getBuiltinLibFunction(fd, builtinID);
  if (context.BuiltinInfo.isPredefinedLibFunction(builtinID))
    return cgm.getAddrOfFunction(GlobalDecl(fd));
  llvm_unreachable("expected builtin library call");
}

// Boolean vectors are treated as an integer bitmask by the bit-count builtins:
// bitcast them to their integer storage type before the bit-count operation and
// any associated zero check, matching OGCG's EmitBitCountExpr behavior.
static mlir::Value bitcastBoolVecToInt(CIRGenBuilderTy &builder,
                                       mlir::Value arg) {
  if (auto vecTy = mlir::dyn_cast<cir::VectorType>(arg.getType())) {
    if (mlir::isa<cir::BoolType>(vecTy.getElementType())) {
      auto storageTy = cir::IntType::get(builder.getContext(), vecTy.getSize(),
                                         /*isSigned=*/false);
      return builder.createBitcast(arg, storageTy);
    }
  }
  return arg;
}

// The generic bit-count builtins (`__builtin_{clzg,ctzg,popcountg}`) accept a
// scalar `_Bool`, which CIRGen keeps as `cir::BoolType`. The bit ops require an
// unsigned integer operand, so promote to a 1-bit unsigned integer — mirroring
// classic clang, which lowers these to the i1 intrinsics (llvm.ctpop.i1 /
// llvm.ctlz.i1 / llvm.cttz.i1). Keeping the 1-bit width preserves the intrinsic
// result: e.g. clz/ctz of a nonzero i1 is 0 (widening to i8 would give 7/0).
static mlir::Value promoteScalarBoolToInt(CIRGenBuilderTy &builder,
                                          mlir::Value arg) {
  if (mlir::isa<cir::BoolType>(arg.getType())) {
    auto storageTy = cir::IntType::get(builder.getContext(), /*width=*/1,
                                       /*isSigned=*/false);
    return builder.createBoolToInt(arg, storageTy);
  }
  return arg;
}

template <typename Op>
static RValue emitBuiltinBitOp(CIRGenFunction &cgf, const CallExpr *e,
                               bool poisonZero = false,
                               bool hasFallback = false,
                               mlir::Value preEmittedArg = {}) {
  mlir::Value arg =
      preEmittedArg ? preEmittedArg : cgf.emitScalarExpr(e->getArg(0));
  CIRGenBuilderTy &builder = cgf.getBuilder();

  arg = bitcastBoolVecToInt(builder, arg);
  arg = promoteScalarBoolToInt(builder, arg);

  Op op;
  if constexpr (std::is_same_v<Op, cir::BitClzOp> ||
                std::is_same_v<Op, cir::BitCtzOp>)
    op = Op::create(builder, cgf.getLoc(e->getSourceRange()), arg, poisonZero);
  else
    op = Op::create(builder, cgf.getLoc(e->getSourceRange()), arg);

  mlir::Value result = op.getResult();
  mlir::Type exprTy = cgf.convertType(e->getType());
  if (exprTy != result.getType()) {
    if (mlir::isa<cir::VectorType>(exprTy) &&
        mlir::isa<cir::VectorType>(result.getType()))
      result = builder.createBitcast(result, exprTy);
    else
      result = builder.createIntCast(result, exprTy);
  }

  if (!hasFallback)
    return RValue::get(result);

  // When a fallback arg is provided (e.g. __builtin_ctzg(x, fallback)),
  // select the fallback value when the input is zero.
  mlir::Location loc = cgf.getLoc(e->getSourceRange());
  mlir::Value zero = builder.getNullValue(arg.getType(), loc);
  mlir::Value isZero =
      builder.createCompare(loc, cir::CmpOpKind::eq, arg, zero);
  mlir::Value fallback = cgf.emitScalarExpr(e->getArg(1));
  mlir::Value selected = builder.createSelect(loc, isZero, fallback, result);
  return RValue::get(selected);
}

/// Emit the conversions required to turn the given value into an
/// integer of the given size.
static mlir::Value emitToInt(CIRGenFunction &cgf, mlir::Value v, QualType t,
                             cir::IntType intType) {
  v = cgf.emitToMemory(v, t);

  if (mlir::isa<cir::PointerType>(v.getType()))
    return cgf.getBuilder().createPtrToInt(v, intType);

  // CIR keeps bools as cir::BoolType (unlike OGCG which widens i1 to i8 in
  // EmitToMemory). For atomic operations we need the integer representation,
  // so convert bool to the target integer type here.
  if (mlir::isa<cir::BoolType>(v.getType()))
    return cgf.getBuilder().createBoolToInt(v, intType);

  assert(v.getType() == intType);
  return v;
}

static mlir::Value emitFromInt(CIRGenFunction &cgf, mlir::Value v, QualType t,
                               mlir::Type resultType) {
  v = cgf.emitFromMemory(v, t);

  if (mlir::isa<cir::PointerType>(resultType))
    return cgf.getBuilder().createIntToPtr(v, resultType);

  // CIR keeps bools as cir::BoolType (unlike OGCG which truncates i8 to i1 in
  // EmitFromMemory). Convert the integer result back to bool when needed.
  if (mlir::isa<cir::BoolType>(resultType))
    return cgf.getBuilder().createCast(cir::CastKind::int_to_bool, v,
                                       resultType);

  assert(v.getType() == resultType);
  return v;
}

static mlir::Value emitBuiltinPtrauthConstantPointer(CIRGenFunction &cgf,
                                                     const Expr *e) {
  Expr::EvalResult result;
  bool succeeded = e->EvaluateAsRValue(result, cgf.getContext());
  assert(succeeded);
  (void)succeeded;

  if (!result.Val.isLValue()) {
    mlir::Attribute attr = ConstantEmitter(cgf).emitAbstract(e, e->getType());
    return cgf.getBuilder().getConstant(cgf.getLoc(e->getExprLoc()),
                                        mlir::cast<mlir::TypedAttr>(attr));
  }

  mlir::Attribute attr = ConstantEmitter(cgf).emitAbstractWithoutDestPtrAuth(
      e->getExprLoc(), result.Val, e->getType());
  return cgf.getBuilder().getConstant(cgf.getLoc(e->getExprLoc()),
                                      mlir::cast<mlir::TypedAttr>(attr));
}

static mlir::Value
emitBuiltinPtrauthDiscriminator(CIRGenFunction &cgf, const Expr *e) {
  e = e->IgnoreParens();
  mlir::Location loc = cgf.getLoc(e->getExprLoc());
  CIRGenBuilderTy &builder = cgf.getBuilder();

  auto castToUIntPtr = [&](mlir::Value value) {
    if (mlir::isa<cir::PointerType>(value.getType()))
      return builder.createPtrToInt(value, cgf.cgm.uIntPtrTy);
    if (value.getType() != cgf.cgm.uIntPtrTy)
      return builder.createCast(loc, cir::CastKind::integral, value,
                                cgf.cgm.uIntPtrTy);
    return value;
  };

  if (const auto *call = dyn_cast<CallExpr>(e)) {
    if (call->getBuiltinCallee() ==
        Builtin::BI__builtin_ptrauth_blend_discriminator) {
      mlir::Value storageAddress =
          emitBuiltinPtrauthConstantPointer(cgf, call->getArg(0));
      mlir::Value extra = castToUIntPtr(cgf.emitScalarExpr(call->getArg(1)));
      return cgf.emitPointerAuthBlendDiscriminator(storageAddress, extra);
    }
  }

  return castToUIntPtr(cgf.emitScalarExpr(e));
}

static RValue emitBuiltinPtrauthSignConstant(CIRGenFunction &cgf,
                                             const CallExpr *e) {
  mlir::Value pointer = emitBuiltinPtrauthConstantPointer(cgf, e->getArg(0));
  unsigned key = e->getArg(1)->EvaluateKnownConstInt(cgf.getContext())
                     .getZExtValue();
  mlir::Value discriminator =
      emitBuiltinPtrauthDiscriminator(cgf, e->getArg(2));

  CIRGenRuntimePointerAuthInfo info{
      PointerAuthenticationMode::SignAndStrip,
      /*isIsaPointer=*/false,
      /*authenticatesNullValues=*/false,
      key,
      discriminator};
  return RValue::get(cgf.emitPointerAuthSign(info, pointer));
}

// Emit the explicit `__builtin_ptrauth_*` builtins as their `llvm.ptrauth.*`
// intrinsics. Mirrors classic CodeGen (CGBuiltin.cpp): every operand is emitted
// by value, the signed value and the pointer-typed discriminators are widened
// to intptr_t (the intrinsics operate on i64, plus an i32 immarg key), and the
// i64 result is cast back to the value's original pointer type for the
// non-blend/non-generic builtins. The intrinsics are not overloaded, so the
// dotted base name suffices; the `llvm.` prefix is added by the CIR->LLVM
// lowering of cir.call_llvm_intrinsic.
static RValue emitBuiltinPtrauth(CIRGenFunction &cgf, const CallExpr *e,
                                 unsigned builtinID) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(e->getSourceRange());
  mlir::Type intPtrTy = cgf.cgm.uIntPtrTy;

  auto toIntIfPtr = [&](mlir::Value v) -> mlir::Value {
    if (mlir::isa<cir::PointerType>(v.getType()))
      return builder.createPtrToInt(v, intPtrTy);
    return v;
  };

  SmallVector<mlir::Value, 5> args;
  for (const Expr *arg : e->arguments())
    args.push_back(cgf.emitScalarExpr(arg));

  // Cast the value to intptr_t, saving its original type.
  mlir::Type origValueType = args[0].getType();
  args[0] = toIntIfPtr(args[0]);

  switch (builtinID) {
  case Builtin::BI__builtin_ptrauth_auth_and_resign:
    args[4] = toIntIfPtr(args[4]);
    [[fallthrough]];
  case Builtin::BI__builtin_ptrauth_auth:
  case Builtin::BI__builtin_ptrauth_sign_unauthenticated:
    args[2] = toIntIfPtr(args[2]);
    break;
  case Builtin::BI__builtin_ptrauth_sign_generic_data:
    args[1] = toIntIfPtr(args[1]);
    break;
  case Builtin::BI__builtin_ptrauth_blend_discriminator:
  case Builtin::BI__builtin_ptrauth_strip:
    break;
  }

  StringRef intrinsicName = [&]() -> StringRef {
    switch (builtinID) {
    case Builtin::BI__builtin_ptrauth_auth:
      return "ptrauth.auth";
    case Builtin::BI__builtin_ptrauth_auth_and_resign:
      return "ptrauth.resign";
    case Builtin::BI__builtin_ptrauth_blend_discriminator:
      return "ptrauth.blend";
    case Builtin::BI__builtin_ptrauth_sign_generic_data:
      return "ptrauth.sign.generic";
    case Builtin::BI__builtin_ptrauth_sign_unauthenticated:
      return "ptrauth.sign";
    case Builtin::BI__builtin_ptrauth_strip:
      return "ptrauth.strip";
    }
    llvm_unreachable("bad ptrauth intrinsic");
  }();

  mlir::Value result =
      cir::LLVMIntrinsicCallOp::create(
          builder, loc, cgf.cgm.getStringAttrCached(intrinsicName), intPtrTy,
          args)
          .getResult();

  // blend/sign_generic yield an integer directly; the others yield the value in
  // its original representation, so restore the pointer type when applicable.
  if (builtinID != Builtin::BI__builtin_ptrauth_sign_generic_data &&
      builtinID != Builtin::BI__builtin_ptrauth_blend_discriminator &&
      mlir::isa<cir::PointerType>(origValueType))
    return RValue::get(builder.createIntToPtr(result, origValueType));

  mlir::Type resultType = cgf.convertType(e->getType());
  if (result.getType() != resultType)
    result = builder.createIntCast(result, resultType);
  return RValue::get(result);
}

/// Check recursively whether a type is or contains a dynamic class that
/// requires __builtin_launder to emit llvm.launder.invariant.group.
/// Ported from OGCG's TypeRequiresBuiltinLaunderImp (CGBuiltin.cpp).
static bool
typeRequiresBuiltinLaunderImp(const clang::ASTContext &ctx, clang::QualType ty,
                              llvm::SmallPtrSetImpl<const clang::Decl *> &seen) {
  if (const auto *arr = ctx.getAsArrayType(ty))
    ty = ctx.getBaseElementType(arr);

  const auto *record = ty->getAsCXXRecordDecl();
  if (!record)
    return false;

  if (!seen.insert(record).second)
    return false;

  assert(record->hasDefinition() &&
         "Incomplete types should already be diagnosed");

  if (record->isDynamicClass())
    return true;

  for (clang::FieldDecl *f : record->fields()) {
    if (typeRequiresBuiltinLaunderImp(ctx, f->getType(), seen))
      return true;
  }
  return false;
}

/// Determine if the specified type requires laundering by checking if it is a
/// dynamic class type or contains a subobject which is a dynamic class type.
/// Ported from OGCG's TypeRequiresBuiltinLaunder (CGBuiltin.cpp).
static bool typeRequiresBuiltinLaunder(CIRGenModule &cgm, clang::QualType ty) {
  if (!cgm.getCodeGenOpts().StrictVTablePointers)
    return false;
  llvm::SmallPtrSet<const clang::Decl *, 16> seen;
  return typeRequiresBuiltinLaunderImp(cgm.getASTContext(), ty, seen);
}

/// Returns true if the out-of-bounds check for a memory builtin cannot be
/// proven safe at compile time. When both sizeArg and dstSizeArg are integer
/// constants and size <= dstSize, the check is provably safe and this returns
/// false (also setting \p size to the evaluated size value).
static bool isMemBuiltinOutOfBoundPossible(const clang::Expr *sizeArg,
                                           const clang::Expr *dstSizeArg,
                                           clang::ASTContext &astContext,
                                           llvm::APSInt &size) {
  clang::Expr::EvalResult sizeResult, dstSizeResult;
  if (!sizeArg->EvaluateAsInt(sizeResult, astContext) ||
      !dstSizeArg->EvaluateAsInt(dstSizeResult, astContext))
    return true;
  size = sizeResult.Val.getInt();
  llvm::APSInt dstSize = dstSizeResult.Val.getInt();
  return size.ugt(dstSize);
}

static Address checkAtomicAlignment(CIRGenFunction &cgf, const CallExpr *e) {
  ASTContext &astContext = cgf.getContext();
  Address ptr = cgf.emitPointerWithAlignment(e->getArg(0));
  unsigned bytes =
      mlir::isa<cir::PointerType>(ptr.getElementType())
          ? astContext.getTypeSizeInChars(astContext.VoidPtrTy).getQuantity()
          : cgf.cgm.getDataLayout().getTypeSizeInBits(ptr.getElementType()) /
                cgf.cgm.getASTContext().getCharWidth();

  unsigned align = ptr.getAlignment().getQuantity();
  if (align % bytes != 0) {
    DiagnosticsEngine &diags = cgf.cgm.getDiags();
    diags.Report(e->getBeginLoc(), diag::warn_sync_op_misaligned);
    // Force address to be at least naturally-aligned.
    return ptr.withAlignment(CharUnits::fromQuantity(bytes));
  }
  return ptr;
}

/// Utility to insert an atomic instruction based on Intrinsic::ID
/// and the expression node.
static mlir::Value makeBinaryAtomicValue(
    CIRGenFunction &cgf, cir::AtomicFetchKind kind, const CallExpr *expr,
    mlir::Type *originalArgType, mlir::Value *emittedArgValue = nullptr,
    cir::MemOrder ordering = cir::MemOrder::SequentiallyConsistent) {

  QualType type = expr->getType();
  QualType ptrType = expr->getArg(0)->getType();

  assert(ptrType->isPointerType());
  assert(
      cgf.getContext().hasSameUnqualifiedType(type, ptrType->getPointeeType()));
  assert(cgf.getContext().hasSameUnqualifiedType(type,
                                                 expr->getArg(1)->getType()));

  Address destAddr = checkAtomicAlignment(cgf, expr);
  CIRGenBuilderTy &builder = cgf.getBuilder();

  mlir::Value val = cgf.emitScalarExpr(expr->getArg(1));
  mlir::Type valueType = val.getType();
  mlir::Value destValue = destAddr.emitRawPointer();

  if (ptrType->getPointeeType()->isPointerType()) {
    // Pointer to pointer
    // `cir.atomic.fetch` expects a pointer to an integer type, so we cast
    // ptr<ptr<T>> to ptr<intPtrSize>
    // Use the pointee type (type) for sizing, not the pointer type (ptrType).
    // Both are pointers so same size on all targets, but semantically type is
    // the correct source (matches OGCG's MakeBinaryAtomicValue).
    cir::IntType ptrSizeInt =
        builder.getSIntNTy(cgf.getContext().getTypeSize(type));
    destValue =
        builder.createBitcast(destValue, builder.getPointerTo(ptrSizeInt));
    val = emitToInt(cgf, val, type, ptrSizeInt);
  } else {
    // Pointer to integer type
    cir::IntType intType =
        ptrType->getPointeeType()->isUnsignedIntegerType()
            ? builder.getUIntNTy(cgf.getContext().getTypeSize(type))
            : builder.getSIntNTy(cgf.getContext().getTypeSize(type));
    val = emitToInt(cgf, val, type, intType);
  }

  // Always report the original argument type so callers can convert back.
  if (originalArgType)
    *originalArgType = valueType;

  // This output argument is needed for post atomic fetch operations
  // that calculate the result of the operation as return value of
  // <binop>_and_fetch builtins. The `AtomicFetch` operation only updates the
  // memory location and returns the old value.
  if (emittedArgValue)
    *emittedArgValue = val;

  auto rmwi = cir::AtomicFetchOp::create(
      builder, cgf.getLoc(expr->getSourceRange()), destValue, val, kind,
      ordering,
      builder.getI64IntegerAttr(destAddr.getAlignment().getAsAlign().value()),
      false, /* is volatile */
      true,  /* fetch first */
      /*sync_scope=*/nullptr);
  return rmwi->getResult(0);
}

static RValue emitBinaryAtomic(CIRGenFunction &cgf, cir::AtomicFetchKind kind,
                               const CallExpr *e) {
  mlir::Type originalArgType;
  mlir::Value result = makeBinaryAtomicValue(cgf, kind, e, &originalArgType);
  return RValue::get(emitFromInt(cgf, result, e->getType(), originalArgType));
}

static RValue emitAtomicXchg(CIRGenFunction &cgf, const CallExpr *e) {
  QualType type = e->getType();
  QualType ptrType = e->getArg(0)->getType();

  Address destAddr = checkAtomicAlignment(cgf, e);
  CIRGenBuilderTy &builder = cgf.getBuilder();

  mlir::Value val = cgf.emitScalarExpr(e->getArg(1));
  mlir::Type valueType = val.getType();
  mlir::Value destValue = destAddr.emitRawPointer();

  if (ptrType->getPointeeType()->isPointerType()) {
    // Use the pointee type (type) for sizing — matches OGCG.
    cir::IntType ptrSizeInt =
        builder.getSIntNTy(cgf.getContext().getTypeSize(type));
    destValue =
        builder.createBitcast(destValue, builder.getPointerTo(ptrSizeInt));
    val = emitToInt(cgf, val, type, ptrSizeInt);
  } else {
    cir::IntType intType =
        ptrType->getPointeeType()->isUnsignedIntegerType()
            ? builder.getUIntNTy(cgf.getContext().getTypeSize(type))
            : builder.getSIntNTy(cgf.getContext().getTypeSize(type));
    val = emitToInt(cgf, val, type, intType);
  }

  auto xchg = cir::AtomicXchgOp::create(
      builder, cgf.getLoc(e->getSourceRange()), destValue, val,
      cir::MemOrder::SequentiallyConsistent,
      builder.getI64IntegerAttr(destAddr.getAlignment().getAsAlign().value()),
      /*is_volatile=*/false, /*sync_scope=*/nullptr);
  return RValue::get(emitFromInt(cgf, xchg->getResult(0), type, valueType));
}

static RValue emitSyncLockRelease(CIRGenFunction &cgf, const CallExpr *e) {
  Address ptr = checkAtomicAlignment(cgf, e);
  CIRGenBuilderTy &builder = cgf.getBuilder();
  QualType elTy = e->getArg(0)->getType()->getPointeeType();

  // OGCG always creates an integer type of the same size as the pointee,
  // which works because LLVM uses opaque pointers. In CIR, pointers are
  // typed, so we must use the actual pointee type to create the zero value.
  mlir::Type valTy = cgf.convertType(elTy);
  builder.createStore(
      cgf.getLoc(e->getSourceRange()),
      builder.getNullValue(valTy, cgf.getLoc(e->getBeginLoc())), ptr,
      /*isVolatile=*/false, /*isNontemporal=*/false,
      /*align=*/mlir::IntegerAttr{},
      /*scope=*/cir::SyncScopeKindAttr{},
      cir::MemOrderAttr::get(&cgf.getMLIRContext(), cir::MemOrder::Release));
  return RValue::get(nullptr);
}

static void emitNontemporalStore(CIRGenFunction &cgf, const CallExpr *expr) {
  mlir::Value val = cgf.emitScalarExpr(expr->getArg(0));
  Address addr = cgf.emitPointerWithAlignment(expr->getArg(1));

  val = cgf.emitToMemory(val, expr->getArg(0)->getType());
  LValue lv = cgf.makeAddrLValue(addr, expr->getArg(0)->getType());
  lv.setNontemporal(true);
  cgf.emitStoreOfScalar(val, lv, false);
}

static mlir::Value emitNontemporalLoad(CIRGenFunction &cgf,
                                       const CallExpr *expr) {
  Address addr = cgf.emitPointerWithAlignment(expr->getArg(0));
  LValue lv = cgf.makeAddrLValue(addr, expr->getType());
  lv.setNontemporal(true);
  return cgf.emitLoadOfScalar(lv, expr->getExprLoc());
}

namespace {
/// A struct to generically describe a bit test intrinsic.
struct BitTest {
  enum ActionKind : uint8_t { TestOnly, Complement, Reset, Set };
  enum InterlockingKind : uint8_t {
    Unlocked,
    Sequential,
    Acquire,
    Release,
    NoFence
  };

  ActionKind Action;
  InterlockingKind Interlocking;
  bool Is64Bit;

  static BitTest decodeBitTestBuiltin(unsigned builtinID);
};
} // namespace

BitTest BitTest::decodeBitTestBuiltin(unsigned builtinID) {
  switch (builtinID) {
  case Builtin::BI_bittest:
    return {TestOnly, Unlocked, false};
  case Builtin::BI_bittestandcomplement:
    return {Complement, Unlocked, false};
  case Builtin::BI_bittestandreset:
    return {Reset, Unlocked, false};
  case Builtin::BI_bittestandset:
    return {Set, Unlocked, false};
  case Builtin::BI_interlockedbittestandreset:
    return {Reset, Sequential, false};
  case Builtin::BI_interlockedbittestandset:
    return {Set, Sequential, false};

  case Builtin::BI_bittest64:
    return {TestOnly, Unlocked, true};
  case Builtin::BI_bittestandcomplement64:
    return {Complement, Unlocked, true};
  case Builtin::BI_bittestandreset64:
    return {Reset, Unlocked, true};
  case Builtin::BI_bittestandset64:
    return {Set, Unlocked, true};
  case Builtin::BI_interlockedbittestandreset64:
    return {Reset, Sequential, true};
  case Builtin::BI_interlockedbittestandset64:
    return {Set, Sequential, true};

  case Builtin::BI_interlockedbittestandset_acq:
    return {Set, Acquire, false};
  case Builtin::BI_interlockedbittestandset_rel:
    return {Set, Release, false};
  case Builtin::BI_interlockedbittestandset_nf:
    return {Set, NoFence, false};
  case Builtin::BI_interlockedbittestandreset_acq:
    return {Reset, Acquire, false};
  case Builtin::BI_interlockedbittestandreset_rel:
    return {Reset, Release, false};
  case Builtin::BI_interlockedbittestandreset_nf:
    return {Reset, NoFence, false};
  case Builtin::BI_interlockedbittestandreset64_acq:
    return {Reset, Acquire, false};
  case Builtin::BI_interlockedbittestandreset64_rel:
    return {Reset, Release, false};
  case Builtin::BI_interlockedbittestandreset64_nf:
    return {Reset, NoFence, false};
  case Builtin::BI_interlockedbittestandset64_acq:
    return {Set, Acquire, false};
  case Builtin::BI_interlockedbittestandset64_rel:
    return {Set, Release, false};
  case Builtin::BI_interlockedbittestandset64_nf:
    return {Set, NoFence, false};
  }
  llvm_unreachable("expected only bittest intrinsics");
}

static cir::MemOrder getBitTestMemOrder(BitTest::InterlockingKind i) {
  switch (i) {
  case BitTest::Unlocked:
    llvm_unreachable("not atomic");
  case BitTest::Sequential:
    return cir::MemOrder::SequentiallyConsistent;
  case BitTest::Acquire:
    return cir::MemOrder::Acquire;
  case BitTest::Release:
    return cir::MemOrder::Release;
  case BitTest::NoFence:
    return cir::MemOrder::Relaxed;
  }
  llvm_unreachable("invalid interlocking kind");
}

static char bitActionToX86BTCode(BitTest::ActionKind a) {
  switch (a) {
  case BitTest::TestOnly:
    return '\0';
  case BitTest::Complement:
    return 'c';
  case BitTest::Reset:
    return 'r';
  case BitTest::Set:
    return 's';
  }
  llvm_unreachable("invalid action");
}

static mlir::Value emitX86BitTestIntrinsic(CIRGenFunction &cgf, BitTest bt,
                                           mlir::Location loc,
                                           mlir::Value bitBase,
                                           mlir::Value bitPos) {
  CIRGenBuilderTy &builder = cgf.getBuilder();

  char action = bitActionToX86BTCode(bt.Action);
  char sizeSuffix = bt.Is64Bit ? 'q' : 'l';

  llvm::SmallString<64> asmString;
  llvm::raw_svector_ostream asmOS(asmString);
  if (bt.Interlocking != BitTest::Unlocked)
    asmOS << "lock ";
  asmOS << "bt";
  if (action)
    asmOS << action;
  asmOS << sizeSuffix << " $2, ($1)";

  std::string constraints = "={@ccc},r,r,~{cc},~{memory}";
  std::string_view machineClobbers = cgf.getTarget().getClobbers();
  if (!machineClobbers.empty()) {
    constraints += ',';
    constraints += machineClobbers;
  }

  assert(mlir::isa<cir::PointerType>(bitBase.getType()) &&
         "expected pointer operand for x86 bt builtin");

  llvm::SmallVector<mlir::Value, 2> inputs = {bitBase, bitPos};
  mlir::Attribute noOperandAttrs = builder.getDictionaryAttr({});
  llvm::SmallVector<mlir::Attribute> operandAttrs = {noOperandAttrs,
                                                     noOperandAttrs};
  std::array<mlir::ValueRange, 3> operands = {
      mlir::ValueRange{}, mlir::ValueRange(inputs), mlir::ValueRange{}};

  cir::InlineAsmOp asmOp = cir::InlineAsmOp::create(
      builder, loc, builder.getUInt8Ty(), operands, asmString, constraints,
      /*sideEffects=*/true, cir::AsmFlavor::x86_att,
      builder.getArrayAttr(operandAttrs));
  return asmOp.getRes();
}

/// Emit a _bittest* intrinsic. These intrinsics take a pointer to an array of
/// bits and a bit position and read and optionally modify the bit at that
/// position. The position index can be arbitrarily large, i.e. it can be
/// larger than 31 or 63, so we need an indexed load in the general case.
static mlir::Value emitBitTestIntrinsic(CIRGenFunction &cgf,
                                        unsigned builtinID,
                                        const CallExpr *e) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(e->getSourceRange());

  mlir::Value bitBase = cgf.emitScalarExpr(e->getArg(0));
  mlir::Value bitPos = cgf.emitScalarExpr(e->getArg(1));
  mlir::Type bitPosType = bitPos.getType();

  BitTest bt = BitTest::decodeBitTestBuiltin(builtinID);

  // Match OGCG's dedicated x86 bt/btc/btr/bts path where available.
  if (cgf.getTarget().getTriple().isX86())
    return emitX86BitTestIntrinsic(cgf, bt, loc, bitBase, bitPos);

  // Generic path: load one byte and test the bit. Use all but the bottom
  // three bits as the byte index, and the bottom three bits to form a mask.
  // Bit = BitBase[BitPos >> 3] & (1 << (BitPos & 0x7)) != 0;
  auto u8Ty = builder.getUIntNTy(8);
  auto bytePtrTy = cir::PointerType::get(u8Ty);

  // ByteIndex = BitPos >> 3 (arithmetic shift right)
  mlir::Value three;
  if (bitPosType == builder.getSInt32Ty())
    three = builder.getConstant(loc, builder.getSInt32AttrLocalCached(3));
  else if (bitPosType == builder.getSInt64Ty())
    three = builder.getConstant(loc, builder.getSInt64AttrLocalCached(3));
  else
    three = builder.getConstant(loc, cir::IntAttr::get(bitPosType, 3));
  auto byteIndex = cir::ShiftOp::create(builder, loc, bitPosType, bitPos,
                                         three, /*isShiftLeft=*/false);

  // ByteAddr = (uint8_t*)BitBase + ByteIndex
  auto byteBasePtr = builder.createBitcast(bitBase, bytePtrTy);
  auto byteAddr = cir::PtrStrideOp::create(builder, loc, bytePtrTy,
                                            byteBasePtr, byteIndex,
                                            cir::GEPNoWrapFlags::inbounds);

  // PosLow = (uint8_t)(BitPos) & 0x7
  auto truncBitPos = builder.createIntCast(bitPos, u8Ty);
  auto seven = builder.getConstant(loc, builder.getUInt8AttrLocalCached(7));
  auto posLow = cir::BinOp::create(builder, loc, cir::BinOpKind::And,
                                    truncBitPos, seven, {}, {}, {}, {}, {}, {});

  // Mask = 1 << PosLow (only needed for non-TestOnly actions)
  mlir::Value mask;
  if (bt.Action != BitTest::TestOnly) {
    auto one = builder.getConstant(loc, builder.getUInt8AttrLocalCached(1));
    mask = cir::ShiftOp::create(builder, loc, u8Ty, one, posLow,
                                 /*isShiftLeft=*/true);
  }

  mlir::Value oldByte;
  if (bt.Interlocking != BitTest::Unlocked) {
    // Atomic path: emit atomicrmw for the interlocked intrinsics.
    cir::AtomicFetchKind rmwOp;
    mlir::Value rmwOperand = mask;
    if (bt.Action == BitTest::Reset) {
      rmwOperand = cir::UnaryOp::create(builder, loc, cir::UnaryOpKind::Not,
                                         mask);
      rmwOp = cir::AtomicFetchKind::And;
    } else {
      rmwOp = cir::AtomicFetchKind::Or;
    }
    auto order = getBitTestMemOrder(bt.Interlocking);
    auto fetchOp = cir::AtomicFetchOp::create(
        builder, loc, byteAddr, rmwOperand, rmwOp, order,
        /*alignment=*/builder.getI64IntegerAttr(1),
        /*isVolatile=*/false, /*fetchFirst=*/true,
        /*sync_scope=*/nullptr);
    oldByte = fetchOp->getResult(0);
  } else {
    // Non-atomic path: plain load, modify, store.
    oldByte = cir::LoadOp::create(builder, loc, byteAddr,
                                   /*isDeref=*/false, /*isVolatile=*/false,
                                   /*is_nontemporal=*/false,
                                   cgf.getI64AttrCached(1),
                                   /*sync_scope=*/cir::SyncScopeKindAttr{},
                                   /*mem_order=*/cir::MemOrderAttr{},
                                   /*tbaa=*/{});
    mlir::Value newByte;
    switch (bt.Action) {
    case BitTest::TestOnly:
      break;
    case BitTest::Complement:
      newByte = cir::BinOp::create(builder, loc, cir::BinOpKind::Xor, oldByte,
                                    mask, {}, {}, {}, {}, {}, {});
      break;
    case BitTest::Reset:
      newByte = cir::BinOp::create(
          builder, loc, cir::BinOpKind::And, oldByte,
          cir::UnaryOp::create(builder, loc, cir::UnaryOpKind::Not, mask), {},
          {}, {}, {}, {}, {});
      break;
    case BitTest::Set:
      newByte = cir::BinOp::create(builder, loc, cir::BinOpKind::Or, oldByte,
                                    mask, {}, {}, {}, {}, {}, {});
      break;
    }
    if (newByte) {
      Address storeAddr(byteAddr, u8Ty, CharUnits::One());
      builder.createStore(loc, newByte, storeAddr);
    }
  }

  // Extract the tested bit: (OldByte >> PosLow) & 1
  auto shiftedByte = cir::ShiftOp::create(builder, loc, u8Ty, oldByte, posLow,
                                           /*isShiftLeft=*/false);
  auto oneU8 = builder.getConstant(loc, builder.getUInt8AttrLocalCached(1));
  auto result = cir::BinOp::create(builder, loc, cir::BinOpKind::And,
                                    shiftedByte, oneU8, {}, {}, {}, {}, {}, {});
  return result;
}

/// Emit a volatile load with monotonic atomic ordering for __iso_volatile_load
/// builtins. These are used on ARM/ARM64 Windows for volatile memory accesses.
static mlir::Value emitISOVolatileLoad(CIRGenFunction &cgf,
                                       const CallExpr *expr) {
  mlir::Value ptr = cgf.emitScalarExpr(expr->getArg(0));
  QualType elTy = expr->getArg(0)->getType()->getPointeeType();
  CharUnits loadSize = cgf.getContext().getTypeSizeInChars(elTy);
  CIRGenBuilderTy &builder = cgf.getBuilder();

  auto intType = builder.getSIntNTy(loadSize.getQuantity() * 8);
  auto ptrTy = cir::PointerType::get(intType);
  if (ptr.getType() != ptrTy)
    ptr = builder.createBitcast(ptr, ptrTy);

  return cir::LoadOp::create(builder, cgf.getLoc(expr->getSourceRange()), ptr,
                             /*isDeref=*/false, /*isVolatile=*/true,
                             /*is_nontemporal=*/false,
                             cgf.getI64AttrCached(loadSize.getQuantity()),
                             /*sync_scope=*/cir::SyncScopeKindAttr{},
                             cir::MemOrderAttr::get(&cgf.getMLIRContext(),
                                                    cir::MemOrder::Relaxed),
                             /*tbaa=*/{});
}

/// Emit a volatile store with monotonic atomic ordering for
/// __iso_volatile_store builtins.
static void emitISOVolatileStore(CIRGenFunction &cgf, const CallExpr *expr) {
  mlir::Value ptr = cgf.emitScalarExpr(expr->getArg(0));
  mlir::Value val = cgf.emitScalarExpr(expr->getArg(1));
  QualType elTy = expr->getArg(0)->getType()->getPointeeType();
  CharUnits storeSize = cgf.getContext().getTypeSizeInChars(elTy);
  CIRGenBuilderTy &builder = cgf.getBuilder();

  auto intType = builder.getSIntNTy(storeSize.getQuantity() * 8);
  auto ptrTy = cir::PointerType::get(intType);
  if (ptr.getType() != ptrTy)
    ptr = builder.createBitcast(ptr, ptrTy);
  if (val.getType() != intType)
    val = builder.createIntCast(val, intType);

  Address addr(ptr, intType, CharUnits::fromQuantity(storeSize.getQuantity()));
  builder.createStore(cgf.getLoc(expr->getSourceRange()), val, addr,
                      /*isVolatile=*/true, /*isNontemporal=*/false,
                      cgf.getI64AttrCached(storeSize.getQuantity()),
                      /*scope=*/cir::SyncScopeKindAttr{},
                      cir::MemOrderAttr::get(&cgf.getMLIRContext(),
                                             cir::MemOrder::Relaxed));
}

static mlir::Value makeAtomicCmpXchgValue(CIRGenFunction &cgf,
                                          const CallExpr *expr,
                                          bool returnBool) {
  QualType typ = returnBool ? expr->getArg(1)->getType() : expr->getType();
  Address destAddr = checkAtomicAlignment(cgf, expr);
  CIRGenBuilderTy &builder = cgf.getBuilder();

  auto intType =
      expr->getArg(0)->getType()->getPointeeType()->isUnsignedIntegerType()
          ? builder.getUIntNTy(cgf.getContext().getTypeSize(typ))
          : builder.getSIntNTy(cgf.getContext().getTypeSize(typ));
  auto cmpVal = cgf.emitScalarExpr(expr->getArg(1));
  mlir::Type valueType = cmpVal.getType();
  cmpVal = emitToInt(cgf, cmpVal, typ, intType);
  auto newVal =
      emitToInt(cgf, cgf.emitScalarExpr(expr->getArg(2)), typ, intType);

  // CIR uses typed pointers, so we need to bitcast the address to match
  // the integer operand type (e.g., ptr<ptr<int>> -> ptr<intN>).
  destAddr = destAddr.withElementType(builder, intType);

  auto op = cir::AtomicCmpXchgOp::create(
      builder, cgf.getLoc(expr->getSourceRange()), cmpVal.getType(),
      builder.getBoolTy(), destAddr.getPointer(), cmpVal, newVal,
      cir::MemOrderAttr::get(&cgf.getMLIRContext(),
                             cir::MemOrder::SequentiallyConsistent),
      cir::MemOrderAttr::get(&cgf.getMLIRContext(),
                             cir::MemOrder::SequentiallyConsistent),
      cgf.getI64AttrCached(destAddr.getAlignment().getAsAlign().value()),
      /*weak=*/nullptr, /*is_volatile=*/nullptr, /*sync_scope=*/nullptr);

  if (returnBool)
    return op.getResult(1);
  // Convert old value back from integer to original type (e.g., intN -> ptr).
  return emitFromInt(cgf, op.getResult(0), typ, valueType);
}

/// Emit an atomic compare-exchange for MSVC _InterlockedCompareExchange
/// intrinsics. MSVC uses (Dest, Exchange, Comparand) argument order, where
/// Exchange is the desired value and Comparand is the expected value. This
/// differs from GNU __sync_val_compare_and_swap which uses (ptr, oldval,
/// newval). The generated cmpxchg is marked volatile for consistency with MSVC
/// (matching OGCG behavior).
static mlir::Value
emitAtomicCmpXchgForMSIntrin(CIRGenFunction &cgf, const CallExpr *expr,
                             cir::MemOrder successOrdering =
                                 cir::MemOrder::SequentiallyConsistent) {
  assert(expr->getArg(0)->getType()->isPointerType());
  assert(cgf.getContext().hasSameUnqualifiedType(
      expr->getType(), expr->getArg(0)->getType()->getPointeeType()));
  assert(cgf.getContext().hasSameUnqualifiedType(expr->getType(),
                                                 expr->getArg(1)->getType()));
  assert(cgf.getContext().hasSameUnqualifiedType(expr->getType(),
                                                 expr->getArg(2)->getType()));

  Address destAddr = checkAtomicAlignment(cgf, expr);
  CIRGenBuilderTy &builder = cgf.getBuilder();

  QualType typ = expr->getType();
  auto intType =
      expr->getArg(0)->getType()->getPointeeType()->isUnsignedIntegerType()
          ? builder.getUIntNTy(cgf.getContext().getTypeSize(typ))
          : builder.getSIntNTy(cgf.getContext().getTypeSize(typ));

  // MSVC: Arg(1) is Exchange (desired), Arg(2) is Comparand (expected).
  auto exchange = cgf.emitScalarExpr(expr->getArg(1));
  mlir::Type valueType = exchange.getType();
  exchange = emitToInt(cgf, exchange, typ, intType);
  auto comparand =
      emitToInt(cgf, cgf.emitScalarExpr(expr->getArg(2)), typ, intType);

  destAddr = destAddr.withElementType(builder, intType);

  // For Release ordering, the failure ordering should be Monotonic.
  auto failureOrdering = successOrdering == cir::MemOrder::Release
                             ? cir::MemOrder::Relaxed
                             : successOrdering;

  // The atomic instruction is marked volatile for consistency with MSVC.
  auto op = cir::AtomicCmpXchgOp::create(
      builder, cgf.getLoc(expr->getSourceRange()), comparand.getType(),
      builder.getBoolTy(), destAddr.getPointer(), comparand, exchange,
      cir::MemOrderAttr::get(&cgf.getMLIRContext(), successOrdering),
      cir::MemOrderAttr::get(&cgf.getMLIRContext(), failureOrdering),
      cgf.getI64AttrCached(destAddr.getAlignment().getAsAlign().value()),
      /*weak=*/nullptr,
      /*is_volatile=*/mlir::UnitAttr::get(&cgf.getMLIRContext()),
      /*sync_scope=*/nullptr);

  // Return the old value, converting back from integer to original type.
  return emitFromInt(cgf, op.getResult(0), typ, valueType);
}


// 64-bit Microsoft platforms support 128 bit cmpxchg operations. They are
// prototyped like this:
//
// unsigned char _InterlockedCompareExchange128...(
//     __int64 volatile * _Destination,
//     __int64 _ExchangeHigh,
//     __int64 _ExchangeLow,
//     __int64 * _ComparandResult);
//
// Note that Destination is assumed to be at least 16-byte aligned, despite
// being typed int64.
static mlir::Value
emitAtomicCmpXchg128ForMSIntrin(CIRGenFunction &cgf, const CallExpr *expr,
                                cir::MemOrder successOrdering) {
  assert(expr->getNumArgs() == 4);
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(expr->getSourceRange());

  mlir::Value destPtr = cgf.emitScalarExpr(expr->getArg(0));
  mlir::Value exchangeHigh = cgf.emitScalarExpr(expr->getArg(1));
  mlir::Value exchangeLow = cgf.emitScalarExpr(expr->getArg(2));
  Address comparandAddr(cgf.emitPointerWithAlignment(expr->getArg(3)));

  // For Release ordering, the failure ordering should be Monotonic.
  auto failureOrdering = successOrdering == cir::MemOrder::Release
                             ? cir::MemOrder::Relaxed
                             : successOrdering;

  // Convert to i128 type. Alignment is overridden to 16 for destination.
  cir::IntType int128Ty = builder.getUIntNTy(128);

  // Bitcast destination pointer to ptr<i128> with 16-byte alignment.
  mlir::Value destAddr =
      builder.createBitcast(destPtr, builder.getPointerTo(int128Ty));

  // Preserve the source pointer alignment while retyping the comparand buffer.
  comparandAddr = comparandAddr.withElementType(builder, int128Ty);

  // Build exchange value: (((i128)hi) << 64) | ((i128)lo)
  // Use zero extension (matching OGCG) to avoid sign-extending negative
  // values which would corrupt the other half when OR'd together.
  cir::IntType u64Ty = builder.getUIntNTy(64);
  mlir::Value hi = builder.createIntCast(
      builder.createIntCast(exchangeHigh, u64Ty), int128Ty);
  mlir::Value lo = builder.createIntCast(
      builder.createIntCast(exchangeLow, u64Ty), int128Ty);
  mlir::Value shift64 = builder.getConstantInt(loc, int128Ty, 64);
  mlir::Value hiShifted = cir::ShiftOp::create(builder, loc, int128Ty, hi,
                                                shift64, /*isShiftleft=*/true);
  mlir::Value exchange = cir::BinOp::create(
      builder, loc, cir::BinOpKind::Or, hiShifted, lo, {}, {}, {}, {}, {}, {});

  // Load the comparand as i128.
  mlir::Value comparand = builder.createLoad(loc, comparandAddr);

  // Perform volatile cmpxchg.
  auto op = cir::AtomicCmpXchgOp::create(
      builder, loc, int128Ty, builder.getBoolTy(), destAddr, comparand,
      exchange,
      cir::MemOrderAttr::get(&cgf.getMLIRContext(), successOrdering),
      cir::MemOrderAttr::get(&cgf.getMLIRContext(), failureOrdering),
      cgf.getI64AttrCached(16), /*weak=*/nullptr,
      /*is_volatile=*/mlir::UnitAttr::get(&cgf.getMLIRContext()),
      /*sync_scope=*/nullptr);

  // Store the old value back to *ComparandResult.
  builder.createStore(loc, op.getResult(0), comparandAddr);

  // Return success as i8 (zext from i1).
  mlir::Value success = op.getResult(1);
  return builder.createBoolToInt(success, builder.getUIntNTy(8));
}


static RValue emitBinaryAtomicPost(CIRGenFunction &cgf,
                                   cir::AtomicFetchKind atomicOpkind,
                                   const CallExpr *e, cir::BinOpKind binopKind,
                                   bool invert = false) {
  mlir::Value emittedArgValue;
  mlir::Type originalArgType;
  clang::QualType typ = e->getType();
  mlir::Value result = makeBinaryAtomicValue(
      cgf, atomicOpkind, e, &originalArgType, &emittedArgValue);
  clang::CIRGen::CIRGenBuilderTy &builder = cgf.getBuilder();
  result = cir::BinOp::create(builder, result.getLoc(), binopKind, result,
                              emittedArgValue, {}, {}, {}, {}, {}, {});

  if (invert)
    result = cir::UnaryOp::create(builder, result.getLoc(),
                                  cir::UnaryOpKind::Not, result);

  result = emitFromInt(cgf, result, typ, originalArgType);
  return RValue::get(result);
}

static void emitAtomicFenceOp(CIRGenFunction &cgf, const CallExpr *expr,
                              cir::SyncScopeKind syncScope) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(expr->getSourceRange());

  auto emitAtomicOpCallBackFn = [&](cir::MemOrder memOrder) {
    cir::AtomicFenceOp::create(
        builder, loc, memOrder,
        cir::SyncScopeKindAttr::get(&cgf.getMLIRContext(), syncScope));
  };

  cgf.emitAtomicExprWithMemOrder(expr->getArg(0), /*isStore*/ false,
                                 /*isLoad*/ false, /*isFence*/ true,
                                 emitAtomicOpCallBackFn);
}

// Forward declaration; defined later in this file.
static RValue errorBuiltinNYI(CIRGenFunction &cgf, const CallExpr *e,
                              unsigned builtinID);

// Map a clang generic-model sync scope to the CIR sync scope kind. Mirrors
// convertSyncScopeToCIR in CIRGenAtomic.cpp (which is file-local there).
static cir::SyncScopeKind mapGenericSyncScopeToCIR(clang::SyncScope scope) {
  switch (scope) {
  case clang::SyncScope::SingleScope:
    return cir::SyncScopeKind::SingleThread;
  case clang::SyncScope::SystemScope:
    return cir::SyncScopeKind::System;
  case clang::SyncScope::DeviceScope:
    return cir::SyncScopeKind::Device;
  case clang::SyncScope::WorkgroupScope:
    return cir::SyncScopeKind::Workgroup;
  case clang::SyncScope::ClusterScope:
    return cir::SyncScopeKind::Cluster;
  case clang::SyncScope::WavefrontScope:
    return cir::SyncScopeKind::Wavefront;
  default:
    return cir::SyncScopeKind::System;
  }
}

// Emit __scoped_atomic_thread_fence. Arg 0 is the memory order, arg 1 is the
// memory scope (generic __MEMORY_SCOPE_* model). Mirrors classic codegen's
// Builtin::BI__scoped_atomic_thread_fence handling in CGBuiltin.cpp.
static RValue emitScopedAtomicFenceOp(CIRGenFunction &cgf, const CallExpr *expr,
                                      unsigned builtinID) {
  std::unique_ptr<AtomicScopeModel> scopeModel =
      AtomicScopeModel::create(AtomicScopeModelKind::Generic);

  Expr::EvalResult scopeEval;
  if (!expr->getArg(1)->EvaluateAsInt(scopeEval, cgf.getContext())) {
    // Dynamic (runtime) scope is not yet handled, matching CIRGenAtomic.cpp's
    // "emitAtomicOp with dynamic sync scope" NYI.
    return errorBuiltinNYI(cgf, expr, builtinID);
  }

  uint64_t scopeVal = scopeEval.Val.getInt().getZExtValue();
  clang::SyncScope clangScope =
      scopeModel->isValid(scopeVal)
          ? scopeModel->map(scopeVal)
          : scopeModel->map(scopeModel->getFallBackValue());
  emitAtomicFenceOp(cgf, expr, mapGenericSyncScopeToCIR(clangScope));
  return RValue::get(nullptr);
}

namespace {
struct WidthAndSignedness {
  unsigned width;
  bool isSigned;
};
} // namespace

static WidthAndSignedness
getIntegerWidthAndSignedness(const clang::ASTContext &astContext,
                             const clang::QualType type) {
  assert(type->isIntegerType() && "Given type is not an integer.");
  unsigned width = type->isBooleanType()  ? 1
                   : type->isBitIntType() ? astContext.getIntWidth(type)
                                          : astContext.getTypeInfo(type).Width;
  bool isSigned = type->isSignedIntegerType();
  return {width, isSigned};
}

// Given one or more integer types, this function produces an integer type that
// encompasses them: any value in one of the given types could be expressed in
// the encompassing type.
static struct WidthAndSignedness
EncompassingIntegerType(ArrayRef<struct WidthAndSignedness> types) {
  assert(types.size() > 0 && "Empty list of types.");

  // If any of the given types is signed, we must return a signed type.
  bool isSigned = llvm::any_of(types, [](const auto &t) { return t.isSigned; });

  // The encompassing type must have a width greater than or equal to the width
  // of the specified types.  Additionally, if the encompassing type is signed,
  // its width must be strictly greater than the width of any unsigned types
  // given.
  unsigned width = 0;
  for (const auto &type : types)
    width = std::max(width, type.width + (isSigned && !type.isSigned));

  return {width, isSigned};
}

/// Determine if a binop is a checked mixed-sign multiply we can specialize.
static bool isSpecialMixedSignMultiply(unsigned builtinID,
                                       WidthAndSignedness leftInfo,
                                       WidthAndSignedness rightInfo,
                                       WidthAndSignedness resultInfo) {
  return builtinID == Builtin::BI__builtin_mul_overflow &&
         std::max(leftInfo.width, rightInfo.width) >= resultInfo.width &&
         leftInfo.isSigned != rightInfo.isSigned;
}

/// Emit a checked mixed-sign multiply. This matches OGCG's specialized path
/// which multiplies the absolute value of the signed operand with the unsigned
/// operand via unsigned overflow intrinsics, then re-applies the sign and the
/// destination-type overflow checks.
static RValue emitCheckedMixedSignMultiply(CIRGenFunction &cgf,
                                           const clang::Expr *leftArg,
                                           WidthAndSignedness leftInfo,
                                           const clang::Expr *rightArg,
                                           WidthAndSignedness rightInfo,
                                           const clang::Expr *resultArg,
                                           clang::QualType resultQTy,
                                           WidthAndSignedness resultInfo) {
  assert(isSpecialMixedSignMultiply(Builtin::BI__builtin_mul_overflow, leftInfo,
                                    rightInfo, resultInfo) &&
         "Not a mixed-sign multiply we can specialize");

  mlir::Location loc = cgf.getLoc(resultArg->getSourceRange());
  CIRGenBuilderTy &builder = cgf.getBuilder();

  const clang::Expr *signedArg = leftInfo.isSigned ? leftArg : rightArg;
  const clang::Expr *unsignedArg = leftInfo.isSigned ? rightArg : leftArg;
  unsigned signedWidth = leftInfo.isSigned ? leftInfo.width : rightInfo.width;
  unsigned unsignedWidth =
      leftInfo.isSigned ? rightInfo.width : leftInfo.width;
  unsigned opWidth = std::max(signedWidth, unsignedWidth);

  auto signedOpTy =
      cir::IntType::get(&cgf.getMLIRContext(), opWidth, /*isSigned=*/true);
  auto unsignedOpTy =
      cir::IntType::get(&cgf.getMLIRContext(), opWidth, /*isSigned=*/false);

  mlir::Value signedValue = cgf.emitScalarExpr(signedArg);
  mlir::Value unsignedValue = cgf.emitScalarExpr(unsignedArg);
  if (signedValue.getType() != signedOpTy)
    signedValue = builder.createIntCast(signedValue, signedOpTy);
  if (unsignedValue.getType() != unsignedOpTy)
    unsignedValue = builder.createIntCast(unsignedValue, unsignedOpTy);

  mlir::Value signedZero = builder.getNullValue(signedOpTy, loc);
  mlir::Value unsignedZero = builder.getNullValue(unsignedOpTy, loc);
  mlir::Value isNegative =
      builder.createCompare(loc, cir::CmpOpKind::lt, signedValue, signedZero);
  mlir::Value absOfNegative = builder.createSub(loc, signedZero, signedValue);
  mlir::Value absSigned =
      builder.createSelect(loc, isNegative, absOfNegative, signedValue);
  mlir::Value absSignedUnsigned = builder.createIntCast(absSigned, unsignedOpTy);

  auto mul = cir::BinOpOverflowOp::create(builder, loc, unsignedOpTy,
                                          cir::BinOpOverflowKind::Mul,
                                          absSignedUnsigned, unsignedValue);
  mlir::Value unsignedResult = mul.getResult();
  mlir::Value overflow = mul.getOverflow();

  mlir::Type actualResultTy = cgf.cgm.convertType(resultQTy);
  cir::IntType resultCIRTy;
  if (auto intTy = mlir::dyn_cast<cir::IntType>(actualResultTy))
    resultCIRTy = intTy;
  else
    resultCIRTy = cir::IntType::get(&cgf.getMLIRContext(), resultInfo.width,
                                    resultInfo.isSigned);

  mlir::Value result;
  if (resultInfo.isSigned) {
    llvm::APInt maxMagnitude =
        llvm::APInt::getSignedMaxValue(resultInfo.width).zext(opWidth);
    mlir::Value maxResult = builder.getConstAPInt(loc, unsignedOpTy, maxMagnitude);
    mlir::Value signAdjust = builder.createBoolToInt(isNegative, unsignedOpTy);
    maxResult = builder.createAdd(loc, maxResult, signAdjust);

    mlir::Value signedOverflow =
        builder.createCompare(loc, cir::CmpOpKind::gt, unsignedResult, maxResult);
    overflow = builder.createOr(loc, overflow, signedOverflow);

    mlir::Value negativeResult =
        builder.createSub(loc, unsignedZero, unsignedResult);
    result = builder.createSelect(loc, isNegative, negativeResult, unsignedResult);
  } else {
    mlir::Value nonZero =
        builder.createCompare(loc, cir::CmpOpKind::ne, unsignedResult, unsignedZero);
    mlir::Value underflow = builder.createAnd(loc, isNegative, nonZero);
    overflow = builder.createOr(loc, overflow, underflow);

    if (resultInfo.width < opWidth) {
      llvm::APInt maxValue = llvm::APInt::getMaxValue(resultInfo.width).zext(opWidth);
      mlir::Value maxResult = builder.getConstAPInt(loc, unsignedOpTy, maxValue);
      mlir::Value truncOverflow = builder.createCompare(
          loc, cir::CmpOpKind::gt, unsignedResult, maxResult);
      overflow = builder.createOr(loc, overflow, truncOverflow);
    }

    mlir::Value negativeResult =
        builder.createSub(loc, unsignedZero, unsignedResult);
    result = builder.createSelect(loc, isNegative, negativeResult, unsignedResult);
  }

  result = builder.createIntCast(result, resultCIRTy);
  if (actualResultTy != resultCIRTy)
    result = builder.createCast(cir::CastKind::int_to_bool, result,
                                actualResultTy);

  Address resultPtr = cgf.emitPointerWithAlignment(resultArg);
  mlir::Type ptrElemTy = resultPtr.getElementType();
  if (result.getType() != ptrElemTy)
    result = builder.createCast(cir::CastKind::integral, result, ptrElemTy);

  bool isVolatile =
      resultArg->getType()->getPointeeType().isVolatileQualified();
  builder.createStore(loc, result, resultPtr, isVolatile);
  return RValue::get(overflow);
}

RValue CIRGenFunction::emitRotate(const CallExpr *e, bool isRotateLeft) {
  mlir::Value input = emitScalarExpr(e->getArg(0));
  mlir::Value amount = emitScalarExpr(e->getArg(1));

  // TODO(cir): MSVC flavor bit rotate builtins use different types for input
  // and amount, but cir.rotate requires them to have the same type. Cast amount
  // to the type of input when necessary.
  assert(!cir::MissingFeatures::msvcBuiltins());

  auto r = cir::RotateOp::create(builder, getLoc(e->getSourceRange()), input,
                                 amount, isRotateLeft);
  return RValue::get(r);
}

template <class Operation>
static RValue emitUnaryMaybeConstrainedFPBuiltin(CIRGenFunction &cgf,
                                                 const CallExpr &e) {
  mlir::Value arg = cgf.emitScalarExpr(e.getArg(0));

  clang::FPOptions fpFeatures = e.getFPFeaturesInEffect(cgf.getLangOpts());
  cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(fpFeatures);
  auto call =
      Operation::create(cgf.getBuilder(), arg.getLoc(), arg.getType(), arg, fmf);
  maybeSetCallStrictFPAttr(cgf, &e, call.getOperation());

  // Attach FP constraint attributes when in a constrained FP context so that
  // LowerToLLVM → LowerToLLVMIR can replace the intrinsic with its
  // constrained variant (e.g. llvm.sin → llvm.experimental.constrained.sin).
  if (fpFeatures.isFPConstrained() || cgf.IsFPConstrained) {
    auto *ctx = &cgf.getMLIRContext();
    if (auto fpExcept = cgf.getFPExceptionBehavior(ctx, fpFeatures))
      call->setAttr("cir.fp_exception", fpExcept);
    if (auto roundMode = CIRGenFunction::getRoundingMode(ctx, fpFeatures))
      call->setAttr("cir.rounding_mode", roundMode);
  }

  return RValue::get(call->getResult(0));
}

template <class Operation>
static RValue emitUnaryFPBuiltin(CIRGenFunction &cgf, const CallExpr &e) {
  mlir::Value arg = cgf.emitScalarExpr(e.getArg(0));
  cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(
      e.getFPFeaturesInEffect(cgf.getLangOpts()));
  auto call =
      Operation::create(cgf.getBuilder(), arg.getLoc(), arg.getType(), arg, fmf);
  maybeSetCallStrictFPAttr(cgf, &e, call.getOperation());
  return RValue::get(call->getResult(0));
}

template <class Operation>
static RValue emitFPToIntRoundBuiltin(CIRGenFunction &cgf,
                                      const CallExpr &e) {
  mlir::Value src = cgf.emitScalarExpr(e.getArg(0));
  mlir::Type resTy = cgf.convertType(e.getType());
  auto call =
      Operation::create(cgf.getBuilder(), src.getLoc(), resTy, src);

  // Attach FP constraint attributes for constrained FP mode so that
  // LowerToLLVMIR can replace with constrained variants.
  clang::FPOptions fpFeatures = e.getFPFeaturesInEffect(cgf.getLangOpts());
  if (fpFeatures.isFPConstrained() || cgf.IsFPConstrained) {
    auto *ctx = &cgf.getMLIRContext();
    if (auto fpExcept = cgf.getFPExceptionBehavior(ctx, fpFeatures))
      call->setAttr("cir.fp_exception", fpExcept);
    if (auto roundMode = CIRGenFunction::getRoundingMode(ctx, fpFeatures))
      call->setAttr("cir.rounding_mode", roundMode);
  }

  return RValue::get(call->getResult(0));
}

template <typename Operation>
static RValue emitPlainBinaryFPBuiltin(CIRGenFunction &cgf, const CallExpr &e) {
  mlir::Value arg0 = cgf.emitScalarExpr(e.getArg(0));
  mlir::Value arg1 = cgf.emitScalarExpr(e.getArg(1));
  cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(
      e.getFPFeaturesInEffect(cgf.getLangOpts()));
  auto call = Operation::create(cgf.getBuilder(), arg0.getLoc(), arg0.getType(),
                                arg0, arg1, fmf);
  maybeSetCallStrictFPAttr(cgf, &e, call.getOperation());
  return RValue::get(call->getResult(0));
}

template <typename Operation>
static RValue emitBinaryFPBuiltin(CIRGenFunction &cgf, const CallExpr &e) {
  mlir::Value arg0 = cgf.emitScalarExpr(e.getArg(0));
  mlir::Value arg1 = cgf.emitScalarExpr(e.getArg(1));
  clang::FPOptions fpFeatures = e.getFPFeaturesInEffect(cgf.getLangOpts());
  cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(fpFeatures);
  auto call = Operation::create(cgf.getBuilder(), arg0.getLoc(), arg0.getType(),
                                arg0, arg1, fmf);
  maybeSetCallStrictFPAttr(cgf, &e, call.getOperation());

  if (fpFeatures.isFPConstrained() || cgf.IsFPConstrained) {
    auto *ctx = &cgf.getMLIRContext();
    if (auto fpExcept = cgf.getFPExceptionBehavior(ctx, fpFeatures))
      call->setAttr("cir.fp_exception", fpExcept);
    if (auto roundMode = CIRGenFunction::getRoundingMode(ctx, fpFeatures))
      call->setAttr("cir.rounding_mode", roundMode);
  }

  return RValue::get(call->getResult(0));
}

template <typename Operation>
static RValue emitBinaryMaybeConstrainedFPBuiltin(CIRGenFunction &cgf,
                                                  const CallExpr &e) {
  return emitBinaryFPBuiltin<Operation>(cgf, e);
}

template <typename Operation>
static RValue emitTernaryFPBuiltin(CIRGenFunction &cgf, const CallExpr &e) {
  mlir::Value arg0 = cgf.emitScalarExpr(e.getArg(0));
  mlir::Value arg1 = cgf.emitScalarExpr(e.getArg(1));
  mlir::Value arg2 = cgf.emitScalarExpr(e.getArg(2));
  cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(
      e.getFPFeaturesInEffect(cgf.getLangOpts()));
  auto call = Operation::create(cgf.getBuilder(), arg0.getLoc(), arg0.getType(),
                                arg0, arg1, arg2, fmf);
  maybeSetCallStrictFPAttr(cgf, &e, call.getOperation());
  return RValue::get(call->getResult(0));
}

template <typename Operation>
static RValue emitTernaryMaybeConstrainedFPBuiltin(CIRGenFunction &cgf,
                                                   const CallExpr &e) {
  mlir::Value arg0 = cgf.emitScalarExpr(e.getArg(0));
  mlir::Value arg1 = cgf.emitScalarExpr(e.getArg(1));
  mlir::Value arg2 = cgf.emitScalarExpr(e.getArg(2));

  clang::FPOptions fpFeatures = e.getFPFeaturesInEffect(cgf.getLangOpts());
  cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(fpFeatures);
  auto call = Operation::create(cgf.getBuilder(), arg0.getLoc(), arg0.getType(),
                                arg0, arg1, arg2, fmf);
  maybeSetCallStrictFPAttr(cgf, &e, call.getOperation());

  if (fpFeatures.isFPConstrained() || cgf.IsFPConstrained) {
    auto *ctx = &cgf.getMLIRContext();
    if (auto fpExcept = cgf.getFPExceptionBehavior(ctx, fpFeatures))
      call->setAttr("cir.fp_exception", fpExcept);
    if (auto roundMode = CIRGenFunction::getRoundingMode(ctx, fpFeatures))
      call->setAttr("cir.rounding_mode", roundMode);
  }

  return RValue::get(call->getResult(0));
}

template <typename Operation>
static RValue emitBinaryIntBuiltin(CIRGenFunction &cgf, const CallExpr &e) {
  mlir::Value arg0 = cgf.emitScalarExpr(e.getArg(0));
  mlir::Value arg1 = cgf.emitScalarExpr(e.getArg(1));
  auto call = Operation::create(cgf.getBuilder(), arg0.getLoc(), arg0.getType(),
                                arg0, arg1);
  return RValue::get(call->getResult(0));
}

template <typename Operation>
static RValue emitTernaryIntBuiltin(CIRGenFunction &cgf, const CallExpr &e) {
  mlir::Value arg0 = cgf.emitScalarExpr(e.getArg(0));
  mlir::Value arg1 = cgf.emitScalarExpr(e.getArg(1));
  mlir::Value arg2 = cgf.emitScalarExpr(e.getArg(2));
  auto call = Operation::create(cgf.getBuilder(), arg0.getLoc(), arg0.getType(),
                                arg0, arg1, arg2);
  return RValue::get(call->getResult(0));
}

template <typename Operation>
static RValue emitVecReductionBuiltin(CIRGenFunction &cgf, const CallExpr &e) {
  mlir::Value vec = cgf.emitScalarExpr(e.getArg(0));
  mlir::Type resTy = cgf.convertType(e.getType());
  auto call = Operation::create(cgf.getBuilder(), vec.getLoc(), resTy, vec);
  return RValue::get(call->getResult(0));
}

static mlir::Value emitSignBit(mlir::Location loc, CIRGenFunction &cgf,
                               mlir::Value val) {
  return cir::SignBitOp::create(cgf.getBuilder(), loc, val)->getResult(0);
}

static RValue errorBuiltinNYI(CIRGenFunction &cgf, const CallExpr *e,
                              unsigned builtinID) {
  // Name the actual target triple rather than hardcoding "X86": this helper is
  // target-agnostic and is reached by amdgcn/nvptx/etc. builtins too.
  std::string target = cgf.getContext().getTargetInfo().getTriple().str();
  std::string msg;
  if (cgf.getContext().BuiltinInfo.isLibFunction(builtinID)) {
    msg = std::string("ClangIR code gen Not Yet Implemented: "
                      "unimplemented ") +
          target + " library function builtin call: " +
          cgf.getContext().BuiltinInfo.getName(builtinID);
  } else {
    msg = std::string("ClangIR code gen Not Yet Implemented: "
                      "unimplemented ") +
          target + " builtin call: " +
          cgf.getContext().BuiltinInfo.getName(builtinID);
  }
  cgf.cgm.error(e->getExprLoc(), msg);

  // Return the appropriate RValue kind for the expression's return type
  // to avoid assertion failures in downstream code that checks the kind.
  // For non-void scalar types, create a poison value to prevent crashes when
  // the result is used as an argument to another call (the error diagnostic
  // will still cause compilation to fail).
  mlir::Location loc = cgf.getLoc(e->getSourceRange());
  switch (CIRGenFunction::getEvaluationKind(e->getType())) {
  case cir::TEK_Complex: {
    // Create a poison value for the complex type to prevent crashes when
    // the result is stored into a complex lvalue.
    mlir::Type resTy = cgf.convertType(e->getType());
    mlir::Value poison =
        cgf.getBuilder().getConstant(loc, cir::PoisonAttr::get(resTy));
    return RValue::getComplex(poison);
  }
  case cir::TEK_Aggregate: {
    // Create a valid temporary alloca so downstream code that tries to use
    // the aggregate address (e.g. emitCallExprLValue -> makeAddrLValue)
    // doesn't crash with an Address::isValid() assertion.
    mlir::Type resTy = cgf.convertType(e->getType());
    Address tmp = cgf.createTempAlloca(
        resTy, cgf.getContext().getTypeAlignInChars(e->getType()), loc,
        "nyi.tmp");
    return RValue::getAggregate(tmp);
  }
  case cir::TEK_Scalar: {
    if (e->getType()->isVoidType())
      return RValue::get(nullptr);
    mlir::Type resTy = cgf.convertType(e->getType());
    mlir::Value poison = cgf.getBuilder().getConstant(
        loc, cir::PoisonAttr::get(resTy));
    return RValue::get(poison);
  }
  }
  llvm_unreachable("bad evaluation kind");
}

static RValue emitBuiltinAlloca(CIRGenFunction &cgf, const CallExpr *e,
                                unsigned builtinID) {
  assert(builtinID == Builtin::BI__builtin_alloca ||
         builtinID == Builtin::BI__builtin_alloca_uninitialized ||
         builtinID == Builtin::BI__builtin_alloca_with_align ||
         builtinID == Builtin::BI__builtin_alloca_with_align_uninitialized ||
         builtinID == Builtin::BIalloca || builtinID == Builtin::BI_alloca);

  // Get alloca size input
  mlir::Value size = cgf.emitScalarExpr(e->getArg(0));

  CharUnits alignmentInBytes;
  if (builtinID == Builtin::BI__builtin_alloca_with_align ||
      builtinID == Builtin::BI__builtin_alloca_with_align_uninitialized) {
    // The second argument is alignment in bits, and must be a constant.
    Expr::EvalResult result;
    e->getArg(1)->EvaluateAsInt(result, cgf.getContext());
    unsigned alignmentInBits = result.Val.getInt().getZExtValue();
    alignmentInBytes = cgf.getContext().toCharUnitsFromBits(alignmentInBits);
  } else {
    // The alignment of the alloca should correspond to __BIGGEST_ALIGNMENT__.
    const TargetInfo &ti = cgf.getContext().getTargetInfo();
    alignmentInBytes =
        cgf.getContext().toCharUnitsFromBits(ti.getSuitableAlign());
  }

  // Emit the alloca op with type `u8 *` to match the semantics of
  // `llvm.alloca`. We later bitcast the type to `void *` to match the
  // semantics of C/C++
  // FIXME(cir): It may make sense to allow AllocaOp of type `u8` to return a
  // pointer of type `void *`. This will require a change to the allocaOp
  // verifier.
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::ptr::MemorySpaceAttrInterface allocaAS = cgf.getCIRAllocaAddressSpace();
  cir::PointerType allocaPtrTy =
      builder.getPointerTo(builder.getUInt8Ty(), allocaAS);
  mlir::Value allocaAddr = builder.createAlloca(
      cgf.getLoc(e->getSourceRange()), allocaPtrTy, builder.getUInt8Ty(),
      "bi_alloca", alignmentInBytes, size);

  // Initialize the allocated buffer if required.
  if (builtinID != Builtin::BI__builtin_alloca_uninitialized &&
      builtinID != Builtin::BI__builtin_alloca_with_align_uninitialized &&
      !cgf.cgm.stopAutoInit()) {
    // Initialize the alloca with the given size and alignment according to
    // the lang opts. Only the trivial non-initialization is supported for
    // now.

    switch (cgf.getLangOpts().getTrivialAutoVarInit()) {
    case LangOptions::TrivialAutoVarInitKind::Uninitialized:
      // Nothing to initialize.
      break;
    case LangOptions::TrivialAutoVarInitKind::Zero: {
      // OGCG: memset(alloca_result, 0, size)
      mlir::Value castAddr =
          builder.createBitcast(allocaAddr, builder.getVoidPtrTy());
      mlir::Value zeroVal = builder.getConstInt(
          cgf.getLoc(e->getSourceRange()), builder.getSInt32Ty(), 0);
      auto memsetOp =
          cir::MemSetOp::create(builder, cgf.getLoc(e->getSourceRange()),
                                castAddr, zeroVal, size);
      memsetOp->setAttr("cir.auto_init",
                        mlir::UnitAttr::get(builder.getContext()));
      memsetOp->setAttr(
          "cir.dst_align",
          cgf.getI64AttrCached(alignmentInBytes.getQuantity()));
      break;
    }
    case LangOptions::TrivialAutoVarInitKind::Pattern: {
      // OGCG: memset(alloca_result, 0xAA, size)
      // 0xAA on 64-bit targets, 0xFF on 32-bit.
      uint8_t patternByte =
          cgf.getContext().getTargetInfo().getMaxPointerWidth() < 64 ? 0xFF
                                                                    : 0xAA;
      mlir::Value castAddr =
          builder.createBitcast(allocaAddr, builder.getVoidPtrTy());
      mlir::Value patternVal = builder.getConstInt(
          cgf.getLoc(e->getSourceRange()), builder.getSInt32Ty(), patternByte);
      auto memsetOp =
          cir::MemSetOp::create(builder, cgf.getLoc(e->getSourceRange()),
                                castAddr, patternVal, size);
      memsetOp->setAttr("cir.auto_init",
                        mlir::UnitAttr::get(builder.getContext()));
      memsetOp->setAttr(
          "cir.dst_align",
          cgf.getI64AttrCached(alignmentInBytes.getQuantity()));
      break;
    }
    }
  }

  // An alloca will always return a pointer to the alloca (stack) address
  // space. This address space need not be the same as the AST / Language
  // default (e.g. in C / C++ auto vars are in the generic address space). At
  // the AST level this is handled within CreateTempAlloca et al., but for the
  // builtin / dynamic alloca we have to handle it here.

  if (!cir::isMatchingAddressSpace(
          allocaAS, e->getType()->getPointeeType().getAddressSpace())) {
    // First bitcast from u8* to void* in the alloca address space.
    mlir::Value castAddr =
        builder.createBitcast(allocaAddr, builder.getVoidPtrTy(allocaAS));
    // Then perform the address space cast to the target address space.
    mlir::Type destTy = cgf.convertType(e->getType());
    return RValue::get(cgf.getTargetHooks().performAddrSpaceCast(
        cgf, castAddr, allocaAS, destTy));
  }

  // Bitcast the alloca to the expected type.
  return RValue::get(
      builder.createBitcast(allocaAddr, builder.getVoidPtrTy(allocaAS)));
}

static mlir::Type
decodeFixedType(ArrayRef<llvm::Intrinsic::IITDescriptor> &infos,
                mlir::MLIRContext *context) {
  using namespace llvm::Intrinsic;

  IITDescriptor descriptor = infos.front();
  infos = infos.slice(1);

  switch (descriptor.Kind) {
  case IITDescriptor::Void:
    return cir::VoidType::get(context);
  case IITDescriptor::VarArg:
    // VarArg isn't a type, just a marker. The caller should handle it.
    llvm_unreachable("VarArg should be handled by getIntrinsicType, not here");
  case IITDescriptor::MMX:
    llvm_unreachable("NYI: IITDescriptor::MMX");
  case IITDescriptor::Token:
    llvm_unreachable("NYI: IITDescriptor::Token");
  case IITDescriptor::Metadata:
    llvm_unreachable("NYI: IITDescriptor::Metadata");
  case IITDescriptor::Half:
    return cir::FP16Type::get(context);
  case IITDescriptor::BFloat:
    return cir::BF16Type::get(context);
  case IITDescriptor::Float:
    return cir::SingleType::get(context);
  case IITDescriptor::Double:
    return cir::DoubleType::get(context);
  case IITDescriptor::Quad:
    return cir::FP128Type::get(context);
  case IITDescriptor::Integer:
    return cir::IntType::get(context, descriptor.Integer_Width,
                             /*isSigned=*/true);
  case IITDescriptor::Vector: {
    mlir::Type elementType = decodeFixedType(infos, context);
    // Vector_Width is an ElementCount, which may be scalable (e.g. the
    // nxv16i1 svbool_t of SVE/SME predicate intrinsics). Use the min element
    // count and carry the scalable flag through to the CIR vector type; calling
    // getFixedValue() on a scalable ElementCount aborts.
    unsigned numElements = descriptor.Vector_Width.getKnownMinValue();
    return cir::VectorType::get(elementType, numElements,
                               descriptor.Vector_Width.isScalable());
  }
  case IITDescriptor::Pointer: {
    // LLVM intrinsics use opaque pointers (no pointee type). CIR requires
    // typed pointers, so use void pointer as the equivalent.
    // The IIT Pointer_AddressSpace is a LLVM target address space number,
    // not a clang::LangAS enum value, so use TargetAddressSpaceAttr directly.
    mlir::Type voidTy = cir::VoidType::get(context);
    mlir::ptr::MemorySpaceAttrInterface addrSpace;
    if (descriptor.Pointer_AddressSpace != 0)
      addrSpace = cir::TargetAddressSpaceAttr::get(
          context, descriptor.Pointer_AddressSpace);
    return cir::PointerType::get(voidTy, addrSpace);
  }
  case IITDescriptor::Struct:
    llvm_unreachable("NYI: IITDescriptor::Struct");
  case IITDescriptor::Argument:
    llvm_unreachable("NYI: IITDescriptor::Argument");
  case IITDescriptor::ExtendArgument:
    llvm_unreachable("NYI: IITDescriptor::ExtendArgument");
  case IITDescriptor::TruncArgument:
    llvm_unreachable("NYI: IITDescriptor::TruncArgument");
  case IITDescriptor::OneNthEltsVecArgument:
    llvm_unreachable("NYI: IITDescriptor::OneNthEltsVecArgument");
  case IITDescriptor::SameVecWidthArgument:
    llvm_unreachable("NYI: IITDescriptor::SameVecWidthArgument");
  case IITDescriptor::VecOfAnyPtrsToElt:
    llvm_unreachable("NYI: IITDescriptor::VecOfAnyPtrsToElt");
  case IITDescriptor::VecElementArgument:
    llvm_unreachable("NYI: IITDescriptor::VecElementArgument");
  case IITDescriptor::Subdivide2Argument:
    llvm_unreachable("NYI: IITDescriptor::Subdivide2Argument");
  case IITDescriptor::Subdivide4Argument:
    llvm_unreachable("NYI: IITDescriptor::Subdivide4Argument");
  case IITDescriptor::VecOfBitcastsToInt:
    llvm_unreachable("NYI: IITDescriptor::VecOfBitcastsToInt");
  case IITDescriptor::AMX:
    // The LLVM x86_amx type is represented in CIR as an opaque type tagged
    // "x86.amx", which the x86 CIR->LLVM type converter lowers to
    // LLVM::LLVMX86AMXType. It only ever appears in AMX intrinsic signatures.
    return cir::OpaqueType::get(
        context, mlir::StringAttr::get(context, cir::OpaqueType::getX86AMXTag()));
  case IITDescriptor::PPCQuad:
    llvm_unreachable("NYI: IITDescriptor::PPCQuad");
  case IITDescriptor::AArch64Svcount:
    llvm_unreachable("NYI: IITDescriptor::AArch64Svcount");
  }
  llvm_unreachable("Unhandled IITDescriptor, must return from switch");
}

// llvm::Intrinsics accepts only LLVMContext. We need to reimplement it here.
/// Helper function to correct integer signedness for intrinsic arguments.
/// IIT always returns signed integers, but the actual intrinsic may expect
/// unsigned integers based on the AST FunctionDecl parameter types.
static mlir::Type getIntrinsicArgumentTypeFromAST(mlir::Type iitType,
                                                  const CallExpr *e,
                                                  unsigned argIndex,
                                                  mlir::MLIRContext *context) {
  // If it's not an integer type, return as-is
  auto intTy = dyn_cast<cir::IntType>(iitType);
  if (!intTy)
    return iitType;

  // Get the FunctionDecl from the CallExpr
  const FunctionDecl *fd = nullptr;
  if (const auto *dre =
          dyn_cast<DeclRefExpr>(e->getCallee()->IgnoreImpCasts())) {
    fd = dyn_cast<FunctionDecl>(dre->getDecl());
  }

  // If we have FunctionDecl and this argument exists, check its signedness
  if (fd && argIndex < fd->getNumParams()) {
    QualType paramType = fd->getParamDecl(argIndex)->getType();
    if (paramType->isUnsignedIntegerType()) {
      // Create unsigned version of the type
      return cir::IntType::get(context, intTy.getWidth(), /*isSigned=*/false);
    }
  }

  // Default: keep IIT type (signed)
  return iitType;
}

static bool isAMDGPUTextureType(ASTContext &context, QualType ty) {
  QualType textureTy = context.AMDGPUTextureTy;
  return !textureTy.isNull() &&
         (ty == textureTy ||
          ty.getCanonicalType() == textureTy.getCanonicalType());
}

static bool isVec8I32Type(mlir::Type ty) {
  auto vecTy = mlir::dyn_cast<cir::VectorType>(ty);
  if (!vecTy || vecTy.getSize() != 8)
    return false;

  auto eltTy = mlir::dyn_cast<cir::IntType>(vecTy.getElementType());
  return eltTy && eltTy.getWidth() == 32;
}

// Match OGCG's AMDGPU image builtin handling by loading pointer-form texture
// descriptors before passing them to LLVM image intrinsics.
static mlir::Value loadTextureDescPtrAsVec8I32(CIRGenFunction &cgf,
                                               mlir::Value rsrcPtr,
                                               mlir::Location loc) {
  auto vecTy = cir::VectorType::get(cgf.sInt32Ty, 8);

  if (rsrcPtr.getType() == vecTy)
    return rsrcPtr;

  if (mlir::isa<cir::PointerType>(rsrcPtr.getType()))
    return cgf.getBuilder()
        .createAlignedLoad(loc, vecTy, rsrcPtr, CharUnits::fromQuantity(32))
        .getResult();

  if (cgf.cgm.getDataLayout().getTypeSizeInBits(rsrcPtr.getType())
          .getFixedValue() == 256)
    return cgf.getBuilder().createBitcast(rsrcPtr, vecTy);

  llvm::report_fatal_error("Unexpected texture resource argument form");
}

// Ensures a pointer argument matches the expected CIR pointer type,
// emitting an addrspacecast for address-space mismatches only.
static mlir::Value getCorrectedPtr(mlir::Value argValue, mlir::Type expectedTy,
                                   CIRGenBuilderTy &builder) {
  auto ptrType = mlir::dyn_cast<cir::PointerType>(argValue.getType());
  assert(ptrType && "expected pointer type");

  auto expectedPtrType = mlir::cast<cir::PointerType>(expectedTy);
  assert(ptrType != expectedPtrType && "pointer types should not match");

  if (ptrType.getAddrSpace() != expectedPtrType.getAddrSpace()) {
    auto newPtrType = cir::PointerType::get(ptrType.getPointee(),
                                            expectedPtrType.getAddrSpace());
    return builder.createAddrSpaceCast(argValue, newPtrType);
  }

  return argValue;
}

static mlir::Value getCorrectedIntrinsicValue(mlir::Value value,
                                              mlir::Type expectedTy,
                                              CIRGenBuilderTy &builder) {
  if (value.getType() == expectedTy)
    return value;

  if (mlir::isa<cir::PointerType>(value.getType()))
    value = getCorrectedPtr(value, expectedTy, builder);

  auto srcIntTy = mlir::dyn_cast<cir::IntType>(value.getType());
  auto dstIntTy = mlir::dyn_cast<cir::IntType>(expectedTy);
  if (srcIntTy && dstIntTy)
    return builder.createIntCast(value, expectedTy);

  auto srcVecTy = mlir::dyn_cast<cir::VectorType>(value.getType());
  auto dstVecTy = mlir::dyn_cast<cir::VectorType>(expectedTy);
  if (srcVecTy && dstVecTy &&
      mlir::isa<cir::IntType>(srcVecTy.getElementType()) &&
      mlir::isa<cir::IntType>(dstVecTy.getElementType()) &&
      srcVecTy.getSize() == dstVecTy.getSize()) {
    return builder.createIntCast(value, expectedTy);
  }

  return builder.createBitcast(value, expectedTy);
}

static bool isAMDGPUTypeSuffixToken(StringRef token) {
  if (token.empty())
    return false;

  auto isDigits = [](StringRef text) {
    if (text.empty())
      return false;
    for (char c : text)
      if (c < '0' || c > '9')
        return false;
    return true;
  };

  if (token.consume_front("v")) {
    size_t split = 0;
    while (split < token.size() && token[split] >= '0' && token[split] <= '9')
      ++split;
    return split != 0 && split != token.size();
  }

  if (token.consume_front("bf"))
    return isDigits(token);

  if (token.front() == 'f' || token.front() == 'i' || token.front() == 'u' ||
      token.front() == 'b')
    return isDigits(token.drop_front());

  return false;
}

static std::string getAMDGPUIntrinsicName(StringRef builtinName,
                                          bool stripTypeSuffix) {
  assert(builtinName.starts_with("__builtin_amdgcn_") &&
         "unexpected builtin prefix");

  SmallVector<StringRef, 8> parts;
  builtinName.drop_front(sizeof("__builtin_amdgcn_") - 1).split(parts, '_');

  if (stripTypeSuffix)
    while (!parts.empty() && isAMDGPUTypeSuffixToken(parts.back()))
      parts.pop_back();

  SmallString<64> intrinsicName("amdgcn");
  for (StringRef part : parts) {
    intrinsicName += ".";
    intrinsicName += part;
  }
  return std::string(intrinsicName);
}

static mlir::Value emitAMDGPUBuiltinAsIntrinsic(CIRGenFunction &cgf,
                                                unsigned builtinID,
                                                const CallExpr *e,
                                                StringRef intrinsicName) {
  unsigned iceArguments = 0;
  ASTContext::GetBuiltinTypeError error;
  cgf.getContext().GetBuiltinType(builtinID, error, &iceArguments);
  assert(error == ASTContext::GE_None && "Should not codegen an error");

  SmallVector<mlir::Value> args;
  for (unsigned i = 0, numArgs = e->getNumArgs(); i != numArgs; ++i) {
    mlir::Value argValue =
        cgf.emitScalarOrConstFoldImmArg(iceArguments, i, e->getArg(i));
    if (isAMDGPUTextureType(cgf.getContext(), e->getArg(i)->getType()))
      argValue = loadTextureDescPtrAsVec8I32(
          cgf, argValue, cgf.getLoc(e->getArg(i)->getSourceRange()));
    args.push_back(argValue);
  }

  mlir::Type resultTy = cgf.convertType(e->getType());
  auto intrinsicCall = cir::LLVMIntrinsicCallOp::create(
      cgf.getBuilder(), cgf.getLoc(e->getSourceRange()),
      cgf.cgm.getStringAttrCached(intrinsicName), resultTy, args);

  if (mlir::isa<cir::VoidType>(resultTy))
    return {};
  return intrinsicCall.getResult();
}

// Emits an RDNA3 WMMA (matrix multiply-accumulate) builtin as its
// llvm.amdgcn.wmma.* intrinsic. Mirrors the classic CodeGen handling in
// TargetBuiltins/AMDGPU.cpp: every operand (the A/B/C matrix vectors plus the
// immediate i1 sign/opsel flags) is emitted by value and passed straight
// through; the intrinsic is overloaded on the result and A/B fragment types,
// which the CIR->LLVM lowering of cir.call_llvm_intrinsic re-derives from the
// operand/result types (so no manual `.v8f32.v16f16`-style mangling here).
static mlir::Value emitAMDGPUWmmaBuiltin(CIRGenFunction &cgf, const CallExpr *e,
                                         StringRef intrinsicName) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  SmallVector<mlir::Value, 6> args;
  for (unsigned i = 0, n = e->getNumArgs(); i != n; ++i)
    args.push_back(cgf.emitScalarExpr(e->getArg(i)));
  mlir::Type resultTy = cgf.convertType(e->getType());
  return cir::LLVMIntrinsicCallOp::create(
             builder, cgf.getLoc(e->getSourceRange()),
             cgf.cgm.getStringAttrCached(intrinsicName), resultTy, args)
      .getResult();
}

// Generates the IR for __builtin_amdgcn_read_exec_*. Mirrors the classic
// CodeGen helper EmitAMDGCNBallotForExec: lowers to the amdgcn.ballot
// intrinsic (overloaded on the register type) applied to a constant `true`.
// For the "hi" variant, extracts the high 32 bits of the i64 result.
static mlir::Value emitAMDGCNBallotForExec(CIRGenFunction &cgf,
                                           const CallExpr *e,
                                           cir::IntType registerType,
                                           bool isExecHi) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(e->getSourceRange());

  mlir::Value trueVal = builder.getBool(true, loc);
  auto call = cir::LLVMIntrinsicCallOp::create(
      builder, loc, cgf.cgm.getStringAttrCached("amdgcn.ballot"), registerType,
      mlir::ValueRange{trueVal});
  mlir::Value result = call.getResult();

  if (isExecHi) {
    mlir::Value shiftAmt = builder.getConstInt(loc, registerType, 32);
    result = builder.createLShr(loc, result, shiftAmt);
    result = builder.createIntCast(result, cgf.getBuilder().getUInt32Ty());
  }

  return result;
}

static std::optional<mlir::Value>
emitAMDGPUBuiltinExpr(CIRGenFunction *cgf, unsigned builtinID,
                      const CallExpr *e) {
  std::string builtinNameStorage =
      cgf->getContext().BuiltinInfo.getName(builtinID);
  StringRef builtinName = builtinNameStorage;
  if (!builtinName.starts_with("__builtin_amdgcn_"))
    return std::nullopt;

  CIRGenBuilderTy &builder = cgf->getBuilder();

  // __builtin_amdgcn_ballot_w32/w64: lower to the amdgcn.ballot intrinsic,
  // overloaded on the (unsigned) result type, applied to the i1 predicate.
  // Classic ref: TargetBuiltins/AMDGPU.cpp ballot_w32/w64.
  if (builtinName == "__builtin_amdgcn_ballot_w32" ||
      builtinName == "__builtin_amdgcn_ballot_w64") {
    mlir::Type resultTy = cgf->convertType(e->getType());
    mlir::Value src = cgf->emitScalarExpr(e->getArg(0));
    auto call = cir::LLVMIntrinsicCallOp::create(
        builder, cgf->getLoc(e->getSourceRange()),
        cgf->cgm.getStringAttrCached("amdgcn.ballot"), resultTy,
        mlir::ValueRange{src});
    return call.getResult();
  }

  // __builtin_amdgcn_read_exec[_lo|_hi]: lower via the amdgcn.ballot idiom.
  // Classic ref: TargetBuiltins/AMDGPU.cpp read_exec* -> EmitAMDGCNBallotForExec.
  if (builtinName == "__builtin_amdgcn_read_exec")
    return emitAMDGCNBallotForExec(*cgf, e, builder.getUInt64Ty(),
                                   /*isExecHi=*/false);
  if (builtinName == "__builtin_amdgcn_read_exec_lo")
    return emitAMDGCNBallotForExec(*cgf, e, builder.getUInt32Ty(),
                                   /*isExecHi=*/false);
  if (builtinName == "__builtin_amdgcn_read_exec_hi")
    return emitAMDGCNBallotForExec(*cgf, e, builder.getUInt64Ty(),
                                   /*isExecHi=*/true);

  mlir::Location loc = cgf->getLoc(e->getSourceRange());

  // __builtin_amdgcn_fmed3f/fmed3h: lower to the amdgcn.fmed3 intrinsic,
  // overloaded on the (float) operand type, applied to the three FP operands.
  // Classic ref: TargetBuiltins/AMDGPU.cpp fmed3f/fmed3h ->
  // emitBuiltinWithOneOverloadedType<3>(Intrinsic::amdgcn_fmed3).
  if (builtinName == "__builtin_amdgcn_fmed3f" ||
      builtinName == "__builtin_amdgcn_fmed3h") {
    mlir::Value src0 = cgf->emitScalarExpr(e->getArg(0));
    mlir::Value src1 = cgf->emitScalarExpr(e->getArg(1));
    mlir::Value src2 = cgf->emitScalarExpr(e->getArg(2));
    auto call = cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgf->cgm.getStringAttrCached("amdgcn.fmed3"),
        src0.getType(), mlir::ValueRange{src0, src1, src2});
    return call.getResult();
  }

  // __builtin_amdgcn_class[f|h]: lower to the amdgcn.class intrinsic, overloaded
  // on the (float) first operand type. The intrinsic returns i1; the builtin's
  // declared result type (int) is produced by a zext, matching classic which
  // emits `zext i1 ... to i32`. Classic ref: TargetBuiltins/AMDGPU.cpp
  // class/classf/classh -> emitFPIntBuiltin(Intrinsic::amdgcn_class).
  if (builtinName == "__builtin_amdgcn_class" ||
      builtinName == "__builtin_amdgcn_classf" ||
      builtinName == "__builtin_amdgcn_classh") {
    mlir::Value src0 = cgf->emitScalarExpr(e->getArg(0));
    mlir::Value src1 = cgf->emitScalarExpr(e->getArg(1));
    auto call = cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgf->cgm.getStringAttrCached("amdgcn.class"),
        builder.getBoolTy(), mlir::ValueRange{src0, src1});
    mlir::Type resultTy = cgf->convertType(e->getType());
    return builder.createIntCast(call.getResult(), resultTy);
  }

  // __builtin_amdgcn_permlane16/permlanex16: lower to the corresponding
  // intrinsic, overloaded on the first operand type, applied to all six
  // operands (the last two are i1 selectors). Classic ref:
  // TargetBuiltins/AMDGPU.cpp permlane16/permlanex16 ->
  // emitBuiltinWithOneOverloadedType<6>.
  if (builtinName == "__builtin_amdgcn_permlane16" ||
      builtinName == "__builtin_amdgcn_permlanex16") {
    SmallVector<mlir::Value, 6> args;
    for (unsigned i = 0, n = e->getNumArgs(); i != n; ++i)
      args.push_back(cgf->emitScalarExpr(e->getArg(i)));
    StringRef intrinsic = builtinName == "__builtin_amdgcn_permlane16"
                              ? "amdgcn.permlane16"
                              : "amdgcn.permlanex16";
    auto call = cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgf->cgm.getStringAttrCached(intrinsic), args[0].getType(),
        args);
    return call.getResult();
  }

  // __builtin_amdgcn_fence(order, scope): emit a cir.atomic.fence with the
  // memory order (C ABI enum) and sync scope (string) parsed from the two
  // constant arguments. Classic ref: TargetBuiltins/AMDGPU.cpp fence ->
  // ProcessOrderScopeAMDGCN + Builder.CreateFence. Only the common cases (a
  // constant scope string and a constant C11 order) are handled; anything with
  // an extra address-space MMRA argument or non-constant operands is left NYI.
  if (builtinName == "__builtin_amdgcn_fence") {
    if (e->getNumArgs() != 2)
      return std::nullopt;

    Expr::EvalResult orderResult;
    if (!e->getArg(0)->EvaluateAsInt(orderResult, cgf->getContext()))
      return std::nullopt;
    int64_t ord = orderResult.Val.getInt().getZExtValue();

    std::optional<cir::MemOrder> order;
    switch (static_cast<llvm::AtomicOrderingCABI>(ord)) {
    case llvm::AtomicOrderingCABI::relaxed:
      order = cir::MemOrder::Relaxed;
      break;
    case llvm::AtomicOrderingCABI::consume:
    case llvm::AtomicOrderingCABI::acquire:
      order = cir::MemOrder::Acquire;
      break;
    case llvm::AtomicOrderingCABI::release:
      order = cir::MemOrder::Release;
      break;
    case llvm::AtomicOrderingCABI::acq_rel:
      order = cir::MemOrder::AcquireRelease;
      break;
    case llvm::AtomicOrderingCABI::seq_cst:
      order = cir::MemOrder::SequentiallyConsistent;
      break;
    }
    if (!order)
      return std::nullopt;

    std::optional<std::string> scopeStr =
        e->getArg(1)->tryEvaluateString(cgf->getContext());
    if (!scopeStr)
      return std::nullopt;

    std::optional<cir::SyncScopeKind> scope =
        llvm::StringSwitch<std::optional<cir::SyncScopeKind>>(*scopeStr)
            .Case("", cir::SyncScopeKind::System)
            .Case("agent", cir::SyncScopeKind::Device)
            .Case("workgroup", cir::SyncScopeKind::Workgroup)
            .Case("cluster", cir::SyncScopeKind::Cluster)
            .Case("wavefront", cir::SyncScopeKind::Wavefront)
            .Case("singlethread", cir::SyncScopeKind::SingleThread)
            .Default(std::nullopt);
    if (!scope)
      return std::nullopt;

    cir::SyncScopeKindAttr scopeAttr =
        *scope == cir::SyncScopeKind::System
            ? cir::SyncScopeKindAttr{}
            : cir::SyncScopeKindAttr::get(&cgf->getMLIRContext(), *scope);
    cir::AtomicFenceOp::create(builder, loc, *order, scopeAttr);
    return mlir::Value{};
  }

  // __builtin_amdgcn_grid_size_x/y/z: load the grid size (dimension x/y/z) out
  // of the HSA kernel_dispatch_packet reached via the dispatch pointer. Classic
  // ref: TargetBuiltins/AMDGPU.cpp grid_size_* -> EmitAMDGPUGridSize:
  //   %dp = call align 4 dereferenceable(64) ptr addrspace(4)
  //             @llvm.amdgcn.dispatch.ptr()
  //   %gep = getelementptr i8, ptr addrspace(4) %dp, i32 (12 + Index*4)
  //   %v = load i32, ptr addrspace(4) %gep, align 4, !range !{i32 1, i32 0},
  //             !invariant.load
  // The `align 4`/`dereferenceable(64)` call-site return attributes are attached
  // via the opt-in `res_attrs` on cir.call_llvm_intrinsic; the `!range`/
  // `!invariant.load` load metadata via the existing `cir.range`/
  // `cir.invariant_load` load attributes.
  if (builtinName == "__builtin_amdgcn_grid_size_x" ||
      builtinName == "__builtin_amdgcn_grid_size_y" ||
      builtinName == "__builtin_amdgcn_grid_size_z") {
    unsigned index = builtinName == "__builtin_amdgcn_grid_size_x" ? 0
                     : builtinName == "__builtin_amdgcn_grid_size_y"
                         ? 1
                         : 2;

    // AMDGPU constant address space is addrspace(4); the dispatch packet is
    // read through it.
    auto constantAS = cir::TargetAddressSpaceAttr::get(&cgf->getMLIRContext(),
                                                       /*addrSpace=*/4);
    cir::IntType i8Ty = builder.getSInt8Ty();
    cir::PointerType dispatchPtrTy = builder.getPointerTo(i8Ty, constantAS);

    // llvm.amdgcn.dispatch.ptr() carrying `align 4`/`dereferenceable(64)`
    // call-site return attributes.
    mlir::NamedAttribute alignAttr = builder.getNamedAttr(
        "llvm.align", builder.getI64IntegerAttr(4));
    mlir::NamedAttribute derefAttr = builder.getNamedAttr(
        "llvm.dereferenceable", builder.getI64IntegerAttr(64));
    mlir::ArrayAttr resAttrs = builder.getArrayAttr(
        {builder.getDictionaryAttr({alignAttr, derefAttr})});
    auto dispatchPtr = cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgf->cgm.getStringAttrCached("amdgcn.dispatch.ptr"),
        dispatchPtrTy, mlir::ValueRange{}, cir::FastMathFlags::none, resAttrs);

    // getelementptr i8, ptr addrspace(4) %dp, i32 (12 + Index*4). Classic emits
    // a plain (non-inbounds) GEP at -O0.
    mlir::Value offset =
        builder.getConstInt(loc, builder.getSInt32Ty(), 12 + index * 4);
    mlir::Value gep = builder.createPtrStride(
        loc, dispatchPtr.getResult(), offset, cir::GEPNoWrapFlags::none);

    // load i32, align 4, !range !{i32 1, i32 0}, !invariant.load. classic uses
    // MDB.createRange(APInt(32, 1), APInt::getZero(32)) == [1, 0] (known
    // non-zero, wrap-around full range). createAlignedLoad bitcasts the i8*
    // GEP result to the result pointee (preserving the address space). The load
    // element type is the builtin's declared result type (unsigned int).
    mlir::Type resultTy = cgf->convertType(e->getType());
    cir::LoadOp load = builder.createAlignedLoad(
        loc, resultTy, gep, clang::CharUnits::fromQuantity(4));
    mlir::IntegerType metaI32 = mlir::IntegerType::get(&cgf->getMLIRContext(), 32,
                                                       mlir::IntegerType::Signless);
    load->setAttr("cir.range",
                  builder.getArrayAttr(
                      {mlir::IntegerAttr::get(metaI32, 1),
                       mlir::IntegerAttr::get(metaI32, 0)}));
    load->setAttr("cir.invariant_load",
                  mlir::UnitAttr::get(builder.getContext()));
    return load.getResult();
  }

  // __builtin_amdgcn_dispatch_ptr: call llvm.amdgcn.dispatch.ptr() carrying the
  // `align 4`/`dereferenceable(64)` call-site return attributes, then cast to
  // the builtin's declared pointer type if it differs from the intrinsic's
  // fixed `ptr addrspace(4)` result. Classic ref: TargetBuiltins/AMDGPU.cpp
  // dispatch_ptr -> EmitAMDGPUDispatchPtr.
  if (builtinName == "__builtin_amdgcn_dispatch_ptr") {
    auto constantAS =
        cir::TargetAddressSpaceAttr::get(&cgf->getMLIRContext(), /*addrSpace=*/4);
    cir::PointerType dispatchPtrTy =
        builder.getPointerTo(builder.getSInt8Ty(), constantAS);
    mlir::NamedAttribute alignAttr =
        builder.getNamedAttr("llvm.align", builder.getI64IntegerAttr(4));
    mlir::NamedAttribute derefAttr =
        builder.getNamedAttr("llvm.dereferenceable", builder.getI64IntegerAttr(64));
    mlir::ArrayAttr resAttrs = builder.getArrayAttr(
        {builder.getDictionaryAttr({alignAttr, derefAttr})});
    auto dispatchPtr = cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgf->cgm.getStringAttrCached("amdgcn.dispatch.ptr"),
        dispatchPtrTy, mlir::ValueRange{}, cir::FastMathFlags::none, resAttrs);
    auto resultTy = mlir::cast<cir::PointerType>(cgf->convertType(e->getType()));
    if (resultTy == dispatchPtrTy)
      return dispatchPtr.getResult();
    if (resultTy.getAddrSpace() == dispatchPtrTy.getAddrSpace())
      return builder.createBitcast(dispatchPtr.getResult(), resultTy);
    return builder.createAddrSpaceCast(dispatchPtr.getResult(), resultTy);
  }

  // __builtin_amdgcn_wave_reduce_*: reduce a value across the wave with a
  // strategy immarg. The intrinsic is overloaded on the value type; the u/i
  // signedness and the b (bitwise) tokens do not appear verbatim in the
  // intrinsic name (unsigned min/max map to umin/umax), so the mapping is
  // explicit. Classic ref: TargetBuiltins/AMDGPU.cpp wave_reduce_* ->
  // getIntrinsicIDforWaveReduction.
  StringRef waveReduceIntrinsic =
      llvm::StringSwitch<StringRef>(builtinName)
          .Cases("__builtin_amdgcn_wave_reduce_add_u32",
                 "__builtin_amdgcn_wave_reduce_add_u64",
                 "amdgcn.wave.reduce.add")
          .Cases("__builtin_amdgcn_wave_reduce_sub_u32",
                 "__builtin_amdgcn_wave_reduce_sub_u64",
                 "amdgcn.wave.reduce.sub")
          .Case("__builtin_amdgcn_wave_reduce_fadd_f32",
                "amdgcn.wave.reduce.fadd")
          .Case("__builtin_amdgcn_wave_reduce_fsub_f32",
                "amdgcn.wave.reduce.fsub")
          .Cases("__builtin_amdgcn_wave_reduce_min_i32",
                 "__builtin_amdgcn_wave_reduce_min_i64",
                 "amdgcn.wave.reduce.min")
          .Cases("__builtin_amdgcn_wave_reduce_min_u32",
                 "__builtin_amdgcn_wave_reduce_min_u64",
                 "amdgcn.wave.reduce.umin")
          .Case("__builtin_amdgcn_wave_reduce_fmin_f32",
                "amdgcn.wave.reduce.fmin")
          .Cases("__builtin_amdgcn_wave_reduce_max_i32",
                 "__builtin_amdgcn_wave_reduce_max_i64",
                 "amdgcn.wave.reduce.max")
          .Cases("__builtin_amdgcn_wave_reduce_max_u32",
                 "__builtin_amdgcn_wave_reduce_max_u64",
                 "amdgcn.wave.reduce.umax")
          .Case("__builtin_amdgcn_wave_reduce_fmax_f32",
                "amdgcn.wave.reduce.fmax")
          .Cases("__builtin_amdgcn_wave_reduce_and_b32",
                 "__builtin_amdgcn_wave_reduce_and_b64",
                 "amdgcn.wave.reduce.and")
          .Cases("__builtin_amdgcn_wave_reduce_or_b32",
                 "__builtin_amdgcn_wave_reduce_or_b64",
                 "amdgcn.wave.reduce.or")
          .Cases("__builtin_amdgcn_wave_reduce_xor_b32",
                 "__builtin_amdgcn_wave_reduce_xor_b64",
                 "amdgcn.wave.reduce.xor")
          .Default("");
  if (!waveReduceIntrinsic.empty())
    return emitAMDGPUBuiltinAsIntrinsic(*cgf, builtinID, e, waveReduceIntrinsic);

  // RDNA3 (gfx1100/gfx1101/gfx1102) WMMA matrix multiply-accumulate builtins
  // -> llvm.amdgcn.wmma.* intrinsics. The `_w32`/`_w64` wave-mode suffix is not
  // part of the intrinsic name (the wave size is implied by the fragment vector
  // widths), so the mapping is explicit rather than derived. Classic ref:
  // TargetBuiltins/AMDGPU.cpp wmma_* -> Intrinsic::amdgcn_wmma_*.
  StringRef wmmaIntrinsic =
      llvm::StringSwitch<StringRef>(builtinName)
          .Cases("__builtin_amdgcn_wmma_f32_16x16x16_f16_w32",
                 "__builtin_amdgcn_wmma_f32_16x16x16_f16_w64",
                 "amdgcn.wmma.f32.16x16x16.f16")
          .Cases("__builtin_amdgcn_wmma_f32_16x16x16_bf16_w32",
                 "__builtin_amdgcn_wmma_f32_16x16x16_bf16_w64",
                 "amdgcn.wmma.f32.16x16x16.bf16")
          .Cases("__builtin_amdgcn_wmma_f16_16x16x16_f16_w32",
                 "__builtin_amdgcn_wmma_f16_16x16x16_f16_w64",
                 "amdgcn.wmma.f16.16x16x16.f16")
          .Cases("__builtin_amdgcn_wmma_bf16_16x16x16_bf16_w32",
                 "__builtin_amdgcn_wmma_bf16_16x16x16_bf16_w64",
                 "amdgcn.wmma.bf16.16x16x16.bf16")
          .Cases("__builtin_amdgcn_wmma_i32_16x16x16_iu8_w32",
                 "__builtin_amdgcn_wmma_i32_16x16x16_iu8_w64",
                 "amdgcn.wmma.i32.16x16x16.iu8")
          .Cases("__builtin_amdgcn_wmma_i32_16x16x16_iu4_w32",
                 "__builtin_amdgcn_wmma_i32_16x16x16_iu4_w64",
                 "amdgcn.wmma.i32.16x16x16.iu4")
          .Cases("__builtin_amdgcn_wmma_f16_16x16x16_f16_tied_w32",
                 "__builtin_amdgcn_wmma_f16_16x16x16_f16_tied_w64",
                 "amdgcn.wmma.f16.16x16x16.f16.tied")
          .Cases("__builtin_amdgcn_wmma_bf16_16x16x16_bf16_tied_w32",
                 "__builtin_amdgcn_wmma_bf16_16x16x16_bf16_tied_w64",
                 "amdgcn.wmma.bf16.16x16x16.bf16.tied")
          .Default("");
  if (!wmmaIntrinsic.empty())
    return emitAMDGPUWmmaBuiltin(*cgf, e, wmmaIntrinsic);

  // Simple 1:1 maps to llvm.amdgcn.* intrinsics overloaded on their operand
  // type(s); operands and immargs pass straight through. Classic ref:
  // TargetBuiltins/AMDGPU.cpp emitBuiltinWithOneOverloadedType<N> /
  // emitFPIntBuiltin cases. Note uicmp/sicmp -> amdgcn.icmp and fcmp ->
  // amdgcn.fcmp: those return a fixed i64 lane mask (the builtin's ulong result
  // type drives the result overload) and take a predicate immarg as the last
  // argument (const-folded by emitScalarOrConstFoldImmArg).
  StringRef simpleIntrinsic =
      llvm::StringSwitch<StringRef>(builtinName)
          .Cases("__builtin_amdgcn_rcp", "__builtin_amdgcn_rcpf",
                 "__builtin_amdgcn_rcph", "__builtin_amdgcn_rcp_bf16",
                 "amdgcn.rcp")
          .Cases("__builtin_amdgcn_sqrt", "__builtin_amdgcn_sqrtf",
                 "__builtin_amdgcn_sqrth", "__builtin_amdgcn_sqrt_bf16",
                 "amdgcn.sqrt")
          .Cases("__builtin_amdgcn_rsq", "__builtin_amdgcn_rsqf",
                 "__builtin_amdgcn_rsqh", "__builtin_amdgcn_rsq_bf16",
                 "amdgcn.rsq")
          .Cases("__builtin_amdgcn_rsq_clamp", "__builtin_amdgcn_rsq_clampf",
                 "amdgcn.rsq.clamp")
          .Cases("__builtin_amdgcn_sinf", "__builtin_amdgcn_sinh",
                 "__builtin_amdgcn_sin_bf16", "amdgcn.sin")
          .Cases("__builtin_amdgcn_cosf", "__builtin_amdgcn_cosh",
                 "__builtin_amdgcn_cos_bf16", "amdgcn.cos")
          .Cases("__builtin_amdgcn_logf", "__builtin_amdgcn_log_bf16",
                 "amdgcn.log")
          .Case("__builtin_amdgcn_log_clampf", "amdgcn.log.clamp")
          .Cases("__builtin_amdgcn_exp2f", "__builtin_amdgcn_exp2_bf16",
                 "amdgcn.exp2")
          .Cases("__builtin_amdgcn_tanhf", "__builtin_amdgcn_tanhh",
                 "__builtin_amdgcn_tanh_bf16", "amdgcn.tanh")
          .Cases("__builtin_amdgcn_fract", "__builtin_amdgcn_fractf",
                 "__builtin_amdgcn_fracth", "amdgcn.fract")
          .Cases("__builtin_amdgcn_frexp_mant", "__builtin_amdgcn_frexp_mantf",
                 "__builtin_amdgcn_frexp_manth", "amdgcn.frexp.mant")
          .Case("__builtin_amdgcn_readfirstlane", "amdgcn.readfirstlane")
          .Case("__builtin_amdgcn_readlane", "amdgcn.readlane")
          .Case("__builtin_amdgcn_permlane64", "amdgcn.permlane64")
          .Case("__builtin_amdgcn_ds_swizzle", "amdgcn.ds.swizzle")
          .Cases("__builtin_amdgcn_div_fixup", "__builtin_amdgcn_div_fixupf",
                 "__builtin_amdgcn_div_fixuph", "amdgcn.div.fixup")
          .Cases("__builtin_amdgcn_trig_preop", "__builtin_amdgcn_trig_preopf",
                 "amdgcn.trig.preop")
          .Case("__builtin_amdgcn_lerp", "amdgcn.lerp")
          .Case("__builtin_amdgcn_ubfe", "amdgcn.ubfe")
          .Case("__builtin_amdgcn_sbfe", "amdgcn.sbfe")
          .Cases("__builtin_amdgcn_uicmp", "__builtin_amdgcn_uicmpl",
                 "__builtin_amdgcn_sicmp", "__builtin_amdgcn_sicmpl",
                 "amdgcn.icmp")
          .Cases("__builtin_amdgcn_fcmp", "__builtin_amdgcn_fcmpf",
                 "amdgcn.fcmp")
          .Default("");
  if (!simpleIntrinsic.empty())
    return emitAMDGPUBuiltinAsIntrinsic(*cgf, builtinID, e, simpleIntrinsic);

  if (builtinName.starts_with("__builtin_amdgcn_image_"))
    return emitAMDGPUBuiltinAsIntrinsic(
        *cgf, builtinID, e,
        getAMDGPUIntrinsicName(builtinName, /*stripTypeSuffix=*/true));

  if (builtinName.starts_with("__builtin_amdgcn_raw_buffer_store_"))
    return emitAMDGPUBuiltinAsIntrinsic(*cgf, builtinID, e,
                                        "amdgcn.raw.ptr.buffer.store");

  if (builtinName.starts_with("__builtin_amdgcn_raw_buffer_load_"))
    return emitAMDGPUBuiltinAsIntrinsic(*cgf, builtinID, e,
                                        "amdgcn.raw.ptr.buffer.load");

  if (builtinName.starts_with("__builtin_amdgcn_raw_ptr_buffer_atomic_"))
    return emitAMDGPUBuiltinAsIntrinsic(
        *cgf, builtinID, e,
        getAMDGPUIntrinsicName(builtinName, /*stripTypeSuffix=*/true));

  if (builtinName == "__builtin_amdgcn_make_buffer_rsrc")
    return emitAMDGPUBuiltinAsIntrinsic(*cgf, builtinID, e,
                                        "amdgcn.make.buffer.rsrc");

  return std::nullopt;
}

static cir::FuncType getIntrinsicType(mlir::MLIRContext *context,
                                      llvm::Intrinsic::ID id) {
  using namespace llvm::Intrinsic;

  SmallVector<IITDescriptor, 8> table;
  getIntrinsicInfoTableEntries(id, table);

  ArrayRef<IITDescriptor> tableRef = table;
  mlir::Type resultTy = decodeFixedType(tableRef, context);

  // A null type means decodeFixedType hit an IIT descriptor with no CIR
  // representation yet (e.g. AMX/x86_amx). Return a null FuncType so the
  // caller can degrade to a clean NYI diagnostic instead of crashing.
  if (!resultTy)
    return {};

  SmallVector<mlir::Type, 8> argTypes;
  bool isVarArg = false;
  while (!tableRef.empty()) {
    auto kind = tableRef.front().Kind;
    if (kind == IITDescriptor::VarArg) {
      isVarArg = true;
      break; // VarArg is last
    }
    mlir::Type argTy = decodeFixedType(tableRef, context);
    if (!argTy)
      return {};
    argTypes.push_back(argTy);
  }

  // CIR convention: no explicit void return type
  if (isa<cir::VoidType>(resultTy))
    return cir::FuncType::get(context, argTypes,
                              /*optionalReturnType=*/nullptr, isVarArg);

  return cir::FuncType::get(context, argTypes, resultTy, isVarArg);
}

static bool shouldCIREmitFPMathIntrinsic(CIRGenFunction &cgf, const CallExpr *e,
                                         unsigned builtinID) {
  std::optional<bool> errnoOverriden;
  // ErrnoOverriden is true if math-errno is overriden via the
  // '#pragma float_control(precise, on)'. This pragma disables fast-math,
  // which implies math-errno.
  if (e->hasStoredFPFeatures()) {
    FPOptionsOverride op = e->getFPFeatures();
    if (op.hasMathErrnoOverride())
      errnoOverriden = op.getMathErrnoOverride();
  }
  // True if 'attribute__((optnone))' is used. This attribute overrides
  // fast-math which implies math-errno.
  bool optNone =
      cgf.curFuncDecl && cgf.curFuncDecl->hasAttr<OptimizeNoneAttr>();
  bool isOptimizationEnabled = cgf.cgm.getCodeGenOpts().OptimizationLevel != 0;
  bool generateFPMathIntrinsics =
      cgf.getContext().BuiltinInfo.shouldGenerateFPMathIntrinsic(
          builtinID, cgf.cgm.getTriple(), errnoOverriden,
          cgf.getLangOpts().MathErrno, optNone, isOptimizationEnabled);
  return generateFPMathIntrinsics;
}

static RValue tryEmitFPMathIntrinsic(CIRGenFunction &cgf, const CallExpr *e,
                                     unsigned builtinID) {
  switch (builtinID) {
  case Builtin::BIacos:
  case Builtin::BIacosf:
  case Builtin::BIacosl:
  case Builtin::BI__builtin_acos:
  case Builtin::BI__builtin_acosf:
  case Builtin::BI__builtin_acosf16:
  case Builtin::BI__builtin_acosl:
  case Builtin::BI__builtin_acosf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::ACosOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_acos:
    return RValue::getIgnored();
  case Builtin::BIasin:
  case Builtin::BIasinf:
  case Builtin::BIasinl:
  case Builtin::BI__builtin_asin:
  case Builtin::BI__builtin_asinf:
  case Builtin::BI__builtin_asinf16:
  case Builtin::BI__builtin_asinl:
  case Builtin::BI__builtin_asinf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::ASinOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_asin:
    return RValue::getIgnored();
  case Builtin::BIatan:
  case Builtin::BIatanf:
  case Builtin::BIatanl:
  case Builtin::BI__builtin_atan:
  case Builtin::BI__builtin_atanf:
  case Builtin::BI__builtin_atanf16:
  case Builtin::BI__builtin_atanl:
  case Builtin::BI__builtin_atanf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::ATanOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_atan:
    return RValue::getIgnored();
  case Builtin::BIatan2:
  case Builtin::BIatan2f:
  case Builtin::BIatan2l:
  case Builtin::BI__builtin_atan2:
  case Builtin::BI__builtin_atan2f:
  case Builtin::BI__builtin_atan2f16:
  case Builtin::BI__builtin_atan2l:
  case Builtin::BI__builtin_atan2f128:
    return emitBinaryMaybeConstrainedFPBuiltin<cir::ATan2Op>(cgf, *e);
  case Builtin::BI__builtin_elementwise_atan2:
    return RValue::getIgnored();
  case Builtin::BIceil:
  case Builtin::BIceilf:
  case Builtin::BIceill:
  case Builtin::BI__builtin_ceil:
  case Builtin::BI__builtin_ceilf:
  case Builtin::BI__builtin_ceilf16:
  case Builtin::BI__builtin_ceill:
  case Builtin::BI__builtin_ceilf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::CeilOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_ceil:
    return RValue::getIgnored();
  case Builtin::BIcopysign:
  case Builtin::BIcopysignf:
  case Builtin::BIcopysignl:
  case Builtin::BI__builtin_copysign:
  case Builtin::BI__builtin_copysignf:
  case Builtin::BI__builtin_copysignf16:
  case Builtin::BI__builtin_copysignl:
  case Builtin::BI__builtin_copysignf128:
    return emitBinaryFPBuiltin<cir::CopysignOp>(cgf, *e);
  case Builtin::BIcos:
  case Builtin::BIcosf:
  case Builtin::BIcosl:
  case Builtin::BI__builtin_cos:
  case Builtin::BI__builtin_cosf:
  case Builtin::BI__builtin_cosf16:
  case Builtin::BI__builtin_cosl:
  case Builtin::BI__builtin_cosf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::CosOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_cos:
    return RValue::getIgnored();
  case Builtin::BIcosh:
  case Builtin::BIcoshf:
  case Builtin::BIcoshl:
  case Builtin::BI__builtin_cosh:
  case Builtin::BI__builtin_coshf:
  case Builtin::BI__builtin_coshf16:
  case Builtin::BI__builtin_coshl:
  case Builtin::BI__builtin_coshf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::CoshOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_cosh:
    return RValue::getIgnored();
  case Builtin::BIexp:
  case Builtin::BIexpf:
  case Builtin::BIexpl:
  case Builtin::BI__builtin_exp:
  case Builtin::BI__builtin_expf:
  case Builtin::BI__builtin_expf16:
  case Builtin::BI__builtin_expl:
  case Builtin::BI__builtin_expf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::ExpOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_exp:
    return RValue::getIgnored();
  case Builtin::BIexp2:
  case Builtin::BIexp2f:
  case Builtin::BIexp2l:
  case Builtin::BI__builtin_exp2:
  case Builtin::BI__builtin_exp2f:
  case Builtin::BI__builtin_exp2f16:
  case Builtin::BI__builtin_exp2l:
  case Builtin::BI__builtin_exp2f128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::Exp2Op>(cgf, *e);
  case Builtin::BI__builtin_elementwise_exp2:
    return RValue::getIgnored();
  case Builtin::BI__builtin_exp10:
  case Builtin::BI__builtin_exp10f:
  case Builtin::BI__builtin_exp10f16:
  case Builtin::BI__builtin_exp10l:
  case Builtin::BI__builtin_exp10f128: {
    // Match OGCG: there is no constrained exp10 intrinsic, so strict FP falls
    // back to the generic builtin path and emits a libcall instead.
    FPOptions fpFeatures = e->getFPFeaturesInEffect(cgf.getLangOpts());
    if (fpFeatures.isFPConstrained() || cgf.IsFPConstrained)
      return RValue::getIgnored();
    return emitUnaryMaybeConstrainedFPBuiltin<cir::Exp10Op>(cgf, *e);
  }
  case Builtin::BI__builtin_elementwise_exp10:
    return RValue::getIgnored();
  case Builtin::BIfabs:
  case Builtin::BIfabsf:
  case Builtin::BIfabsl:
  case Builtin::BI__builtin_fabs:
  case Builtin::BI__builtin_fabsf:
  case Builtin::BI__builtin_fabsf16:
  case Builtin::BI__builtin_fabsl:
  case Builtin::BI__builtin_fabsf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::FAbsOp>(cgf, *e);
  case Builtin::BIfloor:
  case Builtin::BIfloorf:
  case Builtin::BIfloorl:
  case Builtin::BI__builtin_floor:
  case Builtin::BI__builtin_floorf:
  case Builtin::BI__builtin_floorf16:
  case Builtin::BI__builtin_floorl:
  case Builtin::BI__builtin_floorf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::FloorOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_floor:
    return RValue::getIgnored();
  case Builtin::BIfma:
  case Builtin::BIfmaf:
  case Builtin::BIfmal:
  case Builtin::BI__builtin_fma:
  case Builtin::BI__builtin_fmaf:
  case Builtin::BI__builtin_fmaf16:
  case Builtin::BI__builtin_fmal:
  case Builtin::BI__builtin_fmaf128:
    return emitTernaryMaybeConstrainedFPBuiltin<cir::FMAOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_fma:
    return RValue::getIgnored();
  case Builtin::BIfmax:
  case Builtin::BIfmaxf:
  case Builtin::BIfmaxl:
  case Builtin::BI__builtin_fmax:
  case Builtin::BI__builtin_fmaxf:
  case Builtin::BI__builtin_fmaxf16:
  case Builtin::BI__builtin_fmaxl:
  case Builtin::BI__builtin_fmaxf128:
    return emitBinaryMaybeConstrainedFPBuiltin<cir::FMaxNumOp>(cgf, *e);
  case Builtin::BIfmin:
  case Builtin::BIfminf:
  case Builtin::BIfminl:
  case Builtin::BI__builtin_fmin:
  case Builtin::BI__builtin_fminf:
  case Builtin::BI__builtin_fminf16:
  case Builtin::BI__builtin_fminl:
  case Builtin::BI__builtin_fminf128:
    return emitBinaryMaybeConstrainedFPBuiltin<cir::FMinNumOp>(cgf, *e);
  case Builtin::BIfmaximum_num:
  case Builtin::BIfmaximum_numf:
  case Builtin::BIfmaximum_numl:
  case Builtin::BI__builtin_fmaximum_num:
  case Builtin::BI__builtin_fmaximum_numf:
  case Builtin::BI__builtin_fmaximum_numf16:
  case Builtin::BI__builtin_fmaximum_numl:
  case Builtin::BI__builtin_fmaximum_numf128:
    return emitBinaryFPBuiltin<cir::MaximumNumOp>(cgf, *e);
  case Builtin::BIfminimum_num:
  case Builtin::BIfminimum_numf:
  case Builtin::BIfminimum_numl:
  case Builtin::BI__builtin_fminimum_num:
  case Builtin::BI__builtin_fminimum_numf:
  case Builtin::BI__builtin_fminimum_numf16:
  case Builtin::BI__builtin_fminimum_numl:
  case Builtin::BI__builtin_fminimum_numf128:
    return emitBinaryFPBuiltin<cir::MinimumNumOp>(cgf, *e);
  case Builtin::BIfmod:
  case Builtin::BIfmodf:
  case Builtin::BIfmodl:
  case Builtin::BI__builtin_fmod:
  case Builtin::BI__builtin_fmodf:
  case Builtin::BI__builtin_fmodf16:
  case Builtin::BI__builtin_fmodl:
  case Builtin::BI__builtin_fmodf128:
  case Builtin::BI__builtin_elementwise_fmod:
    return emitBinaryFPBuiltin<cir::FModOp>(cgf, *e);
  case Builtin::BIlog:
  case Builtin::BIlogf:
  case Builtin::BIlogl:
  case Builtin::BI__builtin_log:
  case Builtin::BI__builtin_logf:
  case Builtin::BI__builtin_logf16:
  case Builtin::BI__builtin_logl:
  case Builtin::BI__builtin_logf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::LogOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_log:
    return RValue::getIgnored();
  case Builtin::BIlog10:
  case Builtin::BIlog10f:
  case Builtin::BIlog10l:
  case Builtin::BI__builtin_log10:
  case Builtin::BI__builtin_log10f:
  case Builtin::BI__builtin_log10f16:
  case Builtin::BI__builtin_log10l:
  case Builtin::BI__builtin_log10f128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::Log10Op>(cgf, *e);
  case Builtin::BI__builtin_elementwise_log10:
    return RValue::getIgnored();
  case Builtin::BIlog2:
  case Builtin::BIlog2f:
  case Builtin::BIlog2l:
  case Builtin::BI__builtin_log2:
  case Builtin::BI__builtin_log2f:
  case Builtin::BI__builtin_log2f16:
  case Builtin::BI__builtin_log2l:
  case Builtin::BI__builtin_log2f128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::Log2Op>(cgf, *e);
  case Builtin::BI__builtin_elementwise_log2:
    return RValue::getIgnored();
  case Builtin::BInearbyint:
  case Builtin::BInearbyintf:
  case Builtin::BInearbyintl:
  case Builtin::BI__builtin_nearbyint:
  case Builtin::BI__builtin_nearbyintf:
  case Builtin::BI__builtin_nearbyintl:
  case Builtin::BI__builtin_nearbyintf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::NearbyintOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_nearbyint:
    return RValue::getIgnored();
  case Builtin::BIpow:
  case Builtin::BIpowf:
  case Builtin::BIpowl:
  case Builtin::BI__builtin_pow:
  case Builtin::BI__builtin_powf:
  case Builtin::BI__builtin_powf16:
  case Builtin::BI__builtin_powl:
  case Builtin::BI__builtin_powf128:
  case Builtin::BI__builtin_elementwise_pow:
    return emitBinaryMaybeConstrainedFPBuiltin<cir::PowOp>(cgf, *e);
  case Builtin::BIrint:
  case Builtin::BIrintf:
  case Builtin::BIrintl:
  case Builtin::BI__builtin_rint:
  case Builtin::BI__builtin_rintf:
  case Builtin::BI__builtin_rintf16:
  case Builtin::BI__builtin_rintl:
  case Builtin::BI__builtin_rintf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::RintOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_rint:
    return RValue::getIgnored();
  case Builtin::BIround:
  case Builtin::BIroundf:
  case Builtin::BIroundl:
  case Builtin::BI__builtin_round:
  case Builtin::BI__builtin_roundf:
  case Builtin::BI__builtin_roundf16:
  case Builtin::BI__builtin_roundl:
  case Builtin::BI__builtin_roundf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::RoundOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_round:
    return RValue::getIgnored();
  case Builtin::BIroundeven:
  case Builtin::BIroundevenf:
  case Builtin::BIroundevenl:
  case Builtin::BI__builtin_roundeven:
  case Builtin::BI__builtin_roundevenf:
  case Builtin::BI__builtin_roundevenf16:
  case Builtin::BI__builtin_roundevenl:
  case Builtin::BI__builtin_roundevenf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::RoundEvenOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_roundeven:
    return RValue::getIgnored();
  case Builtin::BIsin:
  case Builtin::BIsinf:
  case Builtin::BIsinl:
  case Builtin::BI__builtin_sin:
  case Builtin::BI__builtin_sinf:
  case Builtin::BI__builtin_sinf16:
  case Builtin::BI__builtin_sinl:
  case Builtin::BI__builtin_sinf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::SinOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_sin:
    return RValue::getIgnored();
  case Builtin::BIsinh:
  case Builtin::BIsinhf:
  case Builtin::BIsinhl:
  case Builtin::BI__builtin_sinh:
  case Builtin::BI__builtin_sinhf:
  case Builtin::BI__builtin_sinhf16:
  case Builtin::BI__builtin_sinhl:
  case Builtin::BI__builtin_sinhf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::SinhOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_sinh:
  case Builtin::BI__builtin_sincospi:
  case Builtin::BI__builtin_sincospif:
  case Builtin::BI__builtin_sincospil:
  case Builtin::BIsincos:
  case Builtin::BIsincosf:
  case Builtin::BIsincosl:
  case Builtin::BI__builtin_sincos:
  case Builtin::BI__builtin_sincosf:
  case Builtin::BI__builtin_sincosf16:
  case Builtin::BI__builtin_sincosl:
  case Builtin::BI__builtin_sincosf128:
    return RValue::getIgnored();
  case Builtin::BIsqrt:
  case Builtin::BIsqrtf:
  case Builtin::BIsqrtl:
  case Builtin::BI__builtin_sqrt:
  case Builtin::BI__builtin_sqrtf:
  case Builtin::BI__builtin_sqrtf16:
  case Builtin::BI__builtin_sqrtl:
  case Builtin::BI__builtin_sqrtf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::SqrtOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_sqrt:
    return RValue::getIgnored();
  case Builtin::BItan:
  case Builtin::BItanf:
  case Builtin::BItanl:
  case Builtin::BI__builtin_tan:
  case Builtin::BI__builtin_tanf:
  case Builtin::BI__builtin_tanf16:
  case Builtin::BI__builtin_tanl:
  case Builtin::BI__builtin_tanf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::TanOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_tan:
    return RValue::getIgnored();
  case Builtin::BItanh:
  case Builtin::BItanhf:
  case Builtin::BItanhl:
  case Builtin::BI__builtin_tanh:
  case Builtin::BI__builtin_tanhf:
  case Builtin::BI__builtin_tanhf16:
  case Builtin::BI__builtin_tanhl:
  case Builtin::BI__builtin_tanhf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::TanhOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_tanh:
    return RValue::getIgnored();
  case Builtin::BItrunc:
  case Builtin::BItruncf:
  case Builtin::BItruncl:
  case Builtin::BI__builtin_trunc:
  case Builtin::BI__builtin_truncf:
  case Builtin::BI__builtin_truncf16:
  case Builtin::BI__builtin_truncl:
  case Builtin::BI__builtin_truncf128:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::TruncOp>(cgf, *e);
  case Builtin::BI__builtin_elementwise_trunc:
    break;
  case Builtin::BIlround:
  case Builtin::BIlroundf:
  case Builtin::BIlroundl:
  case Builtin::BI__builtin_lround:
  case Builtin::BI__builtin_lroundf:
  case Builtin::BI__builtin_lroundl:
  case Builtin::BI__builtin_lroundf128:
    return emitFPToIntRoundBuiltin<cir::LroundOp>(cgf, *e);
  case Builtin::BIllround:
  case Builtin::BIllroundf:
  case Builtin::BIllroundl:
  case Builtin::BI__builtin_llround:
  case Builtin::BI__builtin_llroundf:
  case Builtin::BI__builtin_llroundl:
  case Builtin::BI__builtin_llroundf128:
    return emitFPToIntRoundBuiltin<cir::LlroundOp>(cgf, *e);
  case Builtin::BIlrint:
  case Builtin::BIlrintf:
  case Builtin::BIlrintl:
  case Builtin::BI__builtin_lrint:
  case Builtin::BI__builtin_lrintf:
  case Builtin::BI__builtin_lrintl:
  case Builtin::BI__builtin_lrintf128:
    return emitFPToIntRoundBuiltin<cir::LrintOp>(cgf, *e);
  case Builtin::BIllrint:
  case Builtin::BIllrintf:
  case Builtin::BIllrintl:
  case Builtin::BI__builtin_llrint:
  case Builtin::BI__builtin_llrintf:
  case Builtin::BI__builtin_llrintl:
  case Builtin::BI__builtin_llrintf128:
    return emitFPToIntRoundBuiltin<cir::LlrintOp>(cgf, *e);
  case Builtin::BI__builtin_ldexp:
  case Builtin::BI__builtin_ldexpf:
  case Builtin::BI__builtin_ldexpl:
  case Builtin::BI__builtin_ldexpf16:
  case Builtin::BI__builtin_ldexpf128:
  case Builtin::BI__builtin_elementwise_ldexp: {
    mlir::Value val = cgf.emitScalarExpr(e->getArg(0));
    mlir::Value exp = cgf.emitScalarExpr(e->getArg(1));
    auto call = cir::LdexpOp::create(cgf.getBuilder(), val.getLoc(),
                                     val.getType(), val, exp);

    clang::FPOptions fpFeatures = e->getFPFeaturesInEffect(cgf.getLangOpts());
    if (fpFeatures.isFPConstrained() || cgf.IsFPConstrained) {
      auto *ctx = &cgf.getMLIRContext();
      if (auto fpExcept = cgf.getFPExceptionBehavior(ctx, fpFeatures))
        call->setAttr("cir.fp_exception", fpExcept);
      if (auto roundMode = CIRGenFunction::getRoundingMode(ctx, fpFeatures))
        call->setAttr("cir.rounding_mode", roundMode);
    }

    return RValue::get(call->getResult(0));
  }
  default:
    break;
  }

  return RValue::getIgnored();
}

/// Get the argument type for arguments to `os_log_helper`.
static CanQualType getOSLogArgType(ASTContext &c, int size) {
  QualType unsignedTy = c.getIntTypeForBitwidth(size * 8, /*Signed=*/false);
  return c.getCanonicalType(unsignedTy);
}

cir::FuncOp CIRGenFunction::generateBuiltinOSLogHelperFunction(
    const analyze_os_log::OSLogBufferLayout &layout,
    CharUnits bufferAlignment) {
  ASTContext &ctx = getContext();

  llvm::SmallString<64> name;
  {
    llvm::raw_svector_ostream os(name);
    os << "__os_log_helper";
    os << "_" << bufferAlignment.getQuantity();
    os << "_" << int(layout.getSummaryByte());
    os << "_" << int(layout.getNumArgsByte());
    for (const auto &item : layout.Items)
      os << "_" << int(item.getSizeByte()) << "_"
         << int(item.getDescriptorByte());
  }

  // The helper has linkonce_odr linkage so the same encoding shared across
  // translation units collapses to a single definition; return the existing
  // one if this TU already emitted it.
  if (auto existing = cgm.getModule().lookupSymbol<cir::FuncOp>(name.str()))
    return existing;

  FunctionArgList args;
  llvm::SmallVector<mlir::Type, 4> cirArgTys;
  auto *bufferParam = ImplicitParamDecl::Create(
      ctx, nullptr, SourceLocation(), &ctx.Idents.get("buffer"), ctx.VoidPtrTy,
      ImplicitParamKind::Other);
  args.push_back(bufferParam);
  cirArgTys.push_back(convertType(ctx.VoidPtrTy));

  for (unsigned i = 0, e = layout.Items.size(); i != e; ++i) {
    char size = layout.Items[i].getSizeByte();
    if (!size)
      continue;

    QualType argTy = getOSLogArgType(ctx, size);
    args.push_back(ImplicitParamDecl::Create(
        ctx, nullptr, SourceLocation(),
        &ctx.Idents.get(("arg" + std::to_string(i))), argTy,
        ImplicitParamKind::Other));
    cirArgTys.push_back(convertType(argTy));
  }

  mlir::Location loc = builder.getUnknownLoc();
  cir::FuncType fnTy = builder.getFuncType(cirArgTys, builder.getVoidTy());
  cir::FuncOp fn = cgm.createCIRFunction(loc, name.str(), fnTy,
                                         /*funcDecl=*/nullptr);
  fn.setLinkage(cir::GlobalLinkageKind::LinkOnceODRLinkage);
  fn.setGlobalVisibility(cir::VisibilityKind::Hidden);
  mlir::SymbolTable::setSymbolVisibility(
      fn, CIRGenModule::getMLIRVisibilityFromCIRLinkage(fn.getLinkage()));
  if (cgm.supportsCOMDAT())
    fn.setComdat(true);
  cgm.setDSOLocal(fn.getOperation());
  cgm.setCIRFunctionAttributesForDefinition(GlobalDecl{}, fn);

  mlir::OpBuilder::InsertionGuard guard(builder);
  CIRGenFunction helperCGF(cgm, builder, /*suppressNewContext=*/true);
  helperCGF.curGD = GlobalDecl{};
  SourceLocRAIIObject helperLoc(helperCGF, loc);
  mlir::Block *entry = fn.addEntryBlock();

  SymTableScopeTy varScope(helperCGF.symbolTable);
  {
    LexicalScope fnScope(helperCGF, loc, entry);
    helperCGF.startFunction(GlobalDecl{}, ctx.VoidTy, fn, fn.getFunctionType(),
                            args, SourceLocation(), SourceLocation());

    auto &b = helperCGF.getBuilder();
    cir::IntType u8Ty = b.getUInt8Ty();
    cir::PointerType u8PtrTy = b.getPointerTo(u8Ty);
    mlir::Value bufPtr =
        b.createLoad(loc, helperCGF.getAddrOfLocalVar(bufferParam));
    if (bufPtr.getType() != u8PtrTy)
      bufPtr = b.createBitcast(loc, bufPtr, u8PtrTy);

    auto byteAddrAt = [&](int64_t offset, mlir::Type elemTy) -> Address {
      mlir::Value stride = b.getConstInt(loc, b.getSInt64Ty(), offset);
      mlir::Value p =
          b.createPtrStride(loc, bufPtr, stride, cir::GEPNoWrapFlags::none);
      cir::PointerType ptrTy = b.getPointerTo(elemTy);
      if (p.getType() != ptrTy)
        p = b.createBitcast(loc, p, ptrTy);
      CharUnits align =
          bufferAlignment.alignmentAtOffset(CharUnits::fromQuantity(offset));
      return Address(p, elemTy, align);
    };

    auto storeByte = [&](int64_t offset, unsigned char val) {
      b.createStore(loc, b.getConstInt(loc, u8Ty, val),
                    byteAddrAt(offset, u8Ty));
    };

    int64_t offset = 0;
    storeByte(offset++, layout.getSummaryByte());
    storeByte(offset++, layout.getNumArgsByte());

    unsigned i = 1;
    for (const auto &item : layout.Items) {
      storeByte(offset++, item.getDescriptorByte());
      storeByte(offset++, item.getSizeByte());

      CharUnits size = item.size();
      if (!size.getQuantity())
        continue;

      Address argLocal = helperCGF.getAddrOfLocalVar(args[i]);
      mlir::Type elemTy = argLocal.getElementType();
      b.createStore(loc, b.createLoad(loc, argLocal), byteAddrAt(offset, elemTy));
      offset += size.getQuantity();
      ++i;
    }

    cir::ReturnOp::create(b, loc);
    if (mlir::failed(fn.verifyBody()))
      return {};
    helperCGF.finishFunction(SourceLocation());
  }

  return fn;
}

RValue CIRGenFunction::emitBuiltinOSLogFormat(const CallExpr &e) {
  assert(e.getNumArgs() >= 2 &&
         "__builtin_os_log_format takes at least 2 arguments");
  ASTContext &ctx = getContext();
  mlir::Location loc = getLoc(e.getSourceRange());
  analyze_os_log::OSLogBufferLayout layout;
  analyze_os_log::computeOSLogBufferLayout(ctx, &e, layout);
  Address bufAddr = emitPointerWithAlignment(e.getArg(0));

  // Ignore argument 1, the format string. It is not currently used.
  CallArgList args;
  args.add(RValue::get(bufAddr.getPointer()), ctx.VoidPtrTy);
  llvm::SmallVector<QualType, 8> argClangTys;
  argClangTys.push_back(ctx.VoidPtrTy);

  for (const auto &item : layout.Items) {
    int size = item.getSizeByte();
    if (!size)
      continue;

    mlir::Value argVal;
    if (item.getKind() == analyze_os_log::OSLogBufferItem::MaskKind) {
      uint64_t val = 0;
      for (unsigned j = 0, je = item.getMaskType().size(); j != je; ++j)
        val |= ((uint64_t)item.getMaskType()[j]) << j * 8;
      argVal = builder.getConstInt(loc, builder.getUInt64Ty(), val);
    } else if (const Expr *theExpr = item.getExpr()) {
      argVal = emitScalarExpr(theExpr, /*ignoreResultAssign=*/false);
    } else {
      argVal = builder.getConstInt(loc, builder.getUInt32Ty(),
                                   item.getConstValue().getQuantity());
    }

    // Reinterpret the value as an integer of its own width, then widen it to
    // the fixed-width unsigned helper argument type. Floats are bit-cast,
    // pointers are ptr-to-int'd, and signed integers become unsigned of the
    // same width, mirroring classic CodeGen's BitOrPointerCast + ZExtOrBitCast.
    CanQualType argTy = getOSLogArgType(ctx, size);
    mlir::Type destTy = convertType(argTy);
    mlir::Type srcTy = argVal.getType();
    if (mlir::isa<cir::PointerType>(srcTy)) {
      argVal = builder.createPtrToInt(argVal, destTy);
    } else {
      unsigned srcBits =
          cgm.getDataLayout().getTypeSizeInBits(srcTy).getFixedValue();
      cir::IntType sameWidthIntTy = builder.getUIntNTy(srcBits);
      if (srcTy != sameWidthIntTy)
        argVal = builder.createBitcast(loc, argVal, sameWidthIntTy);
      if (argVal.getType() != destTy)
        argVal = builder.createIntCast(argVal, destTy);
    }
    args.add(RValue::get(argVal), argTy);
    argClangTys.push_back(argTy);
  }

  cir::FuncOp helper =
      generateBuiltinOSLogHelperFunction(layout, bufAddr.getAlignment());

  QualType helperFnTy =
      ctx.getFunctionType(ctx.VoidTy, argClangTys, FunctionProtoType::ExtProtoInfo());
  const CIRGenFunctionInfo &fi = cgm.getTypes().arrangeFreeFunctionCall(
      args, helperFnTy->castAs<FunctionType>());
  cir::CIRCallOpInterface callOp;
  emitCall(fi, CIRGenCallee::forDirect(helper), ReturnValueSlot(), args,
           &callOp, loc);

  // The builtin evaluates to a pointer to the start of the buffer.
  mlir::Value resultPtr = bufAddr.getPointer();
  mlir::Type resultTy = convertType(e.getType());
  if (resultPtr.getType() != resultTy)
    resultPtr = builder.createBitcast(loc, resultPtr, resultTy);
  return RValue::get(resultPtr);
}

RValue CIRGenFunction::emitBuiltinExpr(const GlobalDecl &gd, unsigned builtinID,
                                       const CallExpr *e,
                                       ReturnValueSlot returnValue,
                                       bool ignoreResult) {
  mlir::Location loc = getLoc(e->getSourceRange());

  // See if we can constant fold this builtin.  If so, don't emit it at all.
  // TODO: Extend this handling to all builtin calls that we can constant-fold.
  Expr::EvalResult result;
  if (e->isPRValue() && e->EvaluateAsRValue(result, cgm.getASTContext()) &&
      !result.hasSideEffects()) {
    if (result.Val.isInt()) {
      // If the expression type is bool (e.g. __builtin_is_constant_evaluated),
      // produce a CIR bool constant rather than an integer constant.
      if (e->getType()->isBooleanType())
        return RValue::get(
            builder.getBool(result.Val.getInt().getBoolValue(), loc));
      return RValue::get(builder.getConstInt(loc, result.Val.getInt()));
    }
    if (result.Val.isFloat()) {
      // Note: we are using result type of CallExpr to determine the type of
      // the constant. Classic codegen uses the result value to determine the
      // type. We feel it should be Ok to use expression type because it is
      // hard to imagine a builtin function evaluates to a value that
      // over/underflows its own defined type.
      mlir::Type type = convertType(e->getType());
      return RValue::get(builder.getConstFP(loc, type, result.Val.getFloat()));
    }
  }

  const FunctionDecl *fd = gd.getDecl()->getAsFunction();

  // Diagnose missing builtin target features before lowering to CIR/LLVM IR,
  // matching OGCG's CGBuiltin.cpp builtin path.
  checkTargetFeatures(e, fd);

  // If the builtin has been declared explicitly with an assembler label,
  // disable the specialized emitting below. Ideally we should communicate the
  // rename in IR, or at least avoid generating the intrinsic calls that are
  // likely to get lowered to the renamed library functions.
  unsigned builtinIDIfNoAsmLabel = fd->hasAttr<AsmLabelAttr>() ? 0 : builtinID;

  bool generateFPMathIntrinsics =
      shouldCIREmitFPMathIntrinsic(*this, e, builtinID);

  if (generateFPMathIntrinsics) {
    // Try to match the builtinID with a floating point math builtin.
    RValue rv = tryEmitFPMathIntrinsic(*this, e, builtinIDIfNoAsmLabel);

    // Return the result directly if a math intrinsic was generated.
    if (!rv.isIgnored()) {
      return rv;
    }
  }

  assert(!cir::MissingFeatures::builtinCall());

  switch (builtinIDIfNoAsmLabel) {
  default:
    break;

  case Builtin::BI__builtin___CFStringMakeConstantString:
  case Builtin::BI__builtin___NSStringMakeConstantString: {
    mlir::Attribute attr = ConstantEmitter(*this).emitAbstract(e, e->getType());
    auto typedAttr = mlir::cast<mlir::TypedAttr>(attr);
    return RValue::get(cir::ConstantOp::create(builder, getLoc(e->getExprLoc()),
                                               typedAttr));
  }

  // __builtin_is_constant_evaluated() always returns false at runtime.
  // In constexpr/consteval contexts, Clang's constant evaluator handles it.
  // At codegen time, we're always in a runtime context, so fold to false.
  // This matches OGCG which folds via tryEmitAsConstant on the call.
  case Builtin::BI__builtin_is_constant_evaluated: {
    auto loc = getLoc(e->getSourceRange());
    auto boolTy = cir::BoolType::get(builder.getContext());
    auto falseAttr = cir::BoolAttr::get(builder.getContext(), boolTy, false);
    return RValue::get(
        cir::ConstantOp::create(builder, loc, boolTy, falseAttr));
  }

  // C stdarg builtins.
  case Builtin::BI__builtin_stdarg_start:
  case Builtin::BI__builtin_va_start:
  case Builtin::BI__va_start:
  case Builtin::BI__builtin_c23_va_start: {
    mlir::Value vaList = builtinID == Builtin::BI__va_start
                             ? emitScalarExpr(e->getArg(0))
                             : emitVAListRef(e->getArg(0)).getPointer();
    // The second argument to va_start (if present) is just for ABI/frontend
    // type checking — it's the last named parameter before the variadic args.
    // OGCG does not emit it; we use a constant 0 for the count operand.
    mlir::Value count = builder.getConstant(
        getLoc(e->getExprLoc()), builder.getSInt32AttrLocalCached(0));
    emitVAStart(vaList, count);
    return {};
  }

  case Builtin::BI__builtin_va_end:
    emitVAEnd(emitVAListRef(e->getArg(0)).getPointer());
    return {};
  case Builtin::BI__builtin_va_copy: {
    mlir::Value dstPtr = emitVAListRef(e->getArg(0)).getPointer();
    mlir::Value srcPtr = emitVAListRef(e->getArg(1)).getPointer();
    cir::VACopyOp::create(builder, dstPtr.getLoc(), dstPtr, srcPtr);
    return {};
  }

  case Builtin::BI__builtin_ms_va_start:
  case Builtin::BI__builtin_ms_va_end: {
    mlir::Value vaList = emitMSVAListRef(e->getArg(0)).getPointer();
    if (builtinID == Builtin::BI__builtin_ms_va_start) {
      mlir::Value count = builder.getConstant(
          getLoc(e->getExprLoc()), builder.getSInt32AttrLocalCached(0));
      emitVAStart(vaList, count);
    } else {
      emitVAEnd(vaList);
    }
    return {};
  }

  case Builtin::BI__builtin_ms_va_copy: {
    mlir::Location loc = getLoc(e->getExprLoc());
    Address dstAddr = emitMSVAListRef(e->getArg(0));
    Address srcAddr = emitMSVAListRef(e->getArg(1));
    mlir::Value argPtr = builder.createLoad(loc, srcAddr);
    builder.createStore(loc, argPtr, dstAddr);
    return {};
  }

  case Builtin::BIabs:
  case Builtin::BIlabs:
  case Builtin::BIllabs:
  case Builtin::BI__builtin_abs:
  case Builtin::BI__builtin_labs:
  case Builtin::BI__builtin_llabs: {
    bool sanitizeOverflow = sanOpts.has(SanitizerKind::SignedIntegerOverflow);
    mlir::Value arg = emitScalarExpr(e->getArg(0));
    mlir::Value result;
    switch (getLangOpts().getSignedOverflowBehavior()) {
    case LangOptions::SOB_Defined: {
      auto call = cir::AbsOp::create(builder, getLoc(e->getExprLoc()),
                                     arg.getType(), arg, false);
      result = call->getResult(0);
      break;
    }
    case LangOptions::SOB_Undefined: {
      if (!sanitizeOverflow) {
        auto call = cir::AbsOp::create(builder, getLoc(e->getExprLoc()),
                                       arg.getType(), arg, true);
        result = call->getResult(0);
        break;
      }
      // With -fsanitize=signed-integer-overflow, abs checks for negate
      // overflow (INT_MIN) using ssub_with_overflow(0, arg), then selects
      // the negated or original value. Matches OGCG's
      // EmitOverflowCheckedAbs (CGBuiltin.cpp).
      {
        mlir::Location absLoc = getLoc(e->getExprLoc());
        auto intTy = mlir::cast<cir::IntType>(arg.getType());
        mlir::Value zero;
        if (intTy == builder.getSInt32Ty())
          zero =
              builder.getConstant(absLoc, builder.getSInt32AttrLocalCached(0));
        else if (intTy == builder.getSInt64Ty())
          zero =
              builder.getConstant(absLoc, builder.getSInt64AttrLocalCached(0));
        else
          zero = builder.getConstantInt(absLoc, intTy, 0);
        auto overflowOp = cir::BinOpOverflowOp::create(
            builder, absLoc, intTy, cir::BinOpOverflowKind::Sub, zero, arg);
        overflowOp->setAttr("cir.nosanitize",
                            mlir::UnitAttr::get(builder.getContext()));
        mlir::Value negated = overflowOp.getResult();
        mlir::Value overflow = overflowOp.getOverflow();
        mlir::Value noOverflow = builder.createNot(overflow);
        if (auto *op = noOverflow.getDefiningOp())
          op->setAttr("cir.nosanitize",
                      mlir::UnitAttr::get(builder.getContext()));

        // Check if sanitizer is in trap mode or handler mode.
        SanitizerKind::SanitizerOrdinal ordinal =
            SanitizerKind::SO_SignedIntegerOverflow;
        if (sanOpts.has(ordinal)) {
          noOverflow =
              emitGuardedUBSanCheck(absLoc, noOverflow, ordinal);
        }

        bool isTrap =
            cgm.getCodeGenOpts().SanitizeTrap.has(ordinal);
        bool isRecover =
            !isTrap &&
            cgm.getCodeGenOpts().SanitizeRecover.has(ordinal);

        if (sanOpts.has(ordinal) && !isTrap) {
          // Handler mode: emit negate_overflow handler call.
          auto ifOp = cir::IfOp::create(
              builder, absLoc, noOverflow, /*withElseRegion=*/true,
              [&](mlir::OpBuilder &, mlir::Location ifLoc) {
                cir::YieldOp::create(builder, ifLoc);
              },
              [&](mlir::OpBuilder &, mlir::Location ifLoc) {
                // Emit negate overflow handler stub.
                auto intptrTy = builder.getUIntNTy(
                    getContext()
                        .getTypeSize(getContext().getUIntPtrType()));
                auto stubFnTy = cir::FuncType::get({intptrTy},
                                                    builder.getVoidTy());
                auto stubFn = cgm.createRuntimeFunction(
                    stubFnTy, "__cir_ubsan_overflow_handler_negate",
                    /*attrs=*/{}, /*isLocal=*/false,
                    /*assumeConvergent=*/false);
                auto argExt = cir::CastOp::create(
                    builder, ifLoc, intptrTy, cir::CastKind::integral, arg);
                argExt->setAttr(
                    "cir.nosanitize",
                    mlir::UnitAttr::get(builder.getContext()));
                auto call = builder.createCallOp(ifLoc, stubFn, {argExt});
                SourceLocation exprLoc = e->getExprLoc();
                PresumedLoc PLoc =
                    getContext().getSourceManager().getPresumedLoc(exprLoc);
                if (PLoc.isValid()) {
                  call->setDiscardableAttr(
                      "cir.ubsan.source_file",
                      builder.getStringAttr(PLoc.getFilename()));
                  call->setDiscardableAttr(
                      "cir.ubsan.source_line",
                      builder.getI32IntegerAttr(PLoc.getLine()));
                  call->setDiscardableAttr(
                      "cir.ubsan.source_column",
                      builder.getI32IntegerAttr(PLoc.getColumn()));
                }
                // Set type descriptor attrs for the handler.
                SmallString<32> typeName;
                cgm.getDiags().ConvertArgToString(
                    DiagnosticsEngine::ak_qualtype,
                    (intptr_t)e->getType().getAsOpaquePtr(),
                    StringRef(), StringRef(), {}, typeName, {});
                call->setDiscardableAttr("cir.ubsan.type_name",
                                         builder.getStringAttr(typeName));
                call->setDiscardableAttr(
                    "cir.ubsan.handler_name",
                    builder.getStringAttr("negate_overflow"));
                call->setDiscardableAttr(
                    "cir.ubsan.handler_kind",
                    builder.getStringAttr(isRecover ? "recover" : "abort"));
                call->setDiscardableAttr(
                    "cir.nosanitize",
                    mlir::UnitAttr::get(builder.getContext()));
                if (!isRecover) {
                  auto unreachable =
                      cir::UnreachableOp::create(builder, ifLoc);
                  unreachable->setAttr(
                      "cir.nosanitize",
                      mlir::UnitAttr::get(builder.getContext()));
                } else {
                  auto yield = cir::YieldOp::create(builder, ifLoc);
                  yield->setAttr("cir.nosanitize",
                                 mlir::UnitAttr::get(builder.getContext()));
                }
              });
          ifOp->setAttr("cir.branch_weights",
                        builder.getDenseI32ArrayAttr({1048575, 1}));
          ifOp->setAttr("cir.nosanitize",
                        mlir::UnitAttr::get(builder.getContext()));
        } else if (sanOpts.has(ordinal) && isTrap) {
          // Trap mode.
          auto trapIf = cir::IfOp::create(
              builder, absLoc, noOverflow, /*withElseRegion=*/true,
              [&](mlir::OpBuilder &, mlir::Location ifLoc) {
                cir::YieldOp::create(builder, ifLoc);
              },
              [&](mlir::OpBuilder &, mlir::Location ifLoc) {
                auto trapOp = cir::TrapOp::create(builder, ifLoc);
                trapOp->setAttr(
                    "cir.ubsantrap_handler",
                    mlir::IntegerAttr::get(
                        mlir::IntegerType::get(builder.getContext(), 8),
                        13)); // NegateOverflow
                trapOp->setAttr("cir.nosanitize",
                                mlir::UnitAttr::get(builder.getContext()));
              });
          trapIf->setAttr("cir.nosanitize",
                          mlir::UnitAttr::get(builder.getContext()));
        }

        // abs(x) = x >= 0 ? x : -x (using the negated value from overflow
        // check)
        mlir::Value isNeg = cir::CmpOp::create(builder, absLoc,
                                                cir::CmpOpKind::lt, arg, zero);
        isNeg.getDefiningOp()->setAttr(
            "cir.nosanitize", mlir::UnitAttr::get(builder.getContext()));
        result = cir::SelectOp::create(builder, absLoc, isNeg, negated, arg);
        result.getDefiningOp()->setAttr(
            "cir.nosanitize", mlir::UnitAttr::get(builder.getContext()));
        break;
      }
    }
    case LangOptions::SOB_Trapping: {
      if (!sanitizeOverflow) {
        // Overflow-checked abs: check if negation would overflow (i.e. arg ==
        // INT_MIN), trap if so, then compute abs normally with nsw.
        // Matches OGCG's EmitOverflowCheckedAbs pattern: ssub_with_overflow(0,
        // arg), trap on overflow, then select.
        mlir::Location absLoc = getLoc(e->getExprLoc());
        auto intTy = mlir::cast<cir::IntType>(arg.getType());
        mlir::Value zero;
        if (intTy == builder.getSInt32Ty())
          zero = builder.getConstant(absLoc, builder.getSInt32AttrLocalCached(0));
        else if (intTy == builder.getSInt64Ty())
          zero = builder.getConstant(absLoc, builder.getSInt64AttrLocalCached(0));
        else
          zero = builder.getConstantInt(absLoc, intTy, 0);
        auto overflowOp = cir::BinOpOverflowOp::create(
            builder, absLoc, intTy, cir::BinOpOverflowKind::Sub, zero, arg);
        overflowOp->setAttr("cir.nosanitize",
                            mlir::UnitAttr::get(builder.getContext()));
        mlir::Value overflow = overflowOp.getOverflow();

        // Trap on overflow.
        const std::string &trapFuncName = cgm.getCodeGenOpts().TrapFuncName;
        auto trapIf = cir::IfOp::create(
            builder, absLoc, overflow,
            /*withElseRegion=*/false,
            [&](mlir::OpBuilder &, mlir::Location ifLoc) {
              auto trapOp = cir::TrapOp::create(builder, ifLoc);
              // SubOverflow handler ID = 21
              trapOp->setAttr(
                  "cir.ubsantrap_handler",
                  mlir::IntegerAttr::get(
                      mlir::IntegerType::get(builder.getContext(), 8), 21));
              trapOp->setAttr("cir.nosanitize",
                              mlir::UnitAttr::get(builder.getContext()));
              if (!trapFuncName.empty())
                trapOp->setAttr("cir.trap_func_name",
                                cgm.getStringAttrCached(trapFuncName));
            });
        trapIf->setAttr("cir.nosanitize",
                        mlir::UnitAttr::get(builder.getContext()));

        // Now compute abs with nsw — safe because we've trapped on INT_MIN.
        auto call = cir::AbsOp::create(builder, absLoc, arg.getType(), arg,
                                       /*poison=*/true);
        result = call->getResult(0);
        break;
      }
      // With both -ftrapv (SOB_Trapping) and
      // -fsanitize=signed-integer-overflow, the sanitizer takes precedence:
      // abs checks for negate overflow (INT_MIN) using ssub_with_overflow(0,
      // arg) and routes to the negate_overflow UBSan handler (or trap, per
      // -fsanitize-trap), then selects the negated or original value. This
      // mirrors OGCG, where SOB_Undefined+sanitizer falls through to
      // SOB_Trapping and calls EmitOverflowCheckedAbs(E, /*Sanitize=*/true)
      // (CGBuiltin.cpp); identical to the SOB_Undefined+sanitizer path above.
      {
        mlir::Location absLoc = getLoc(e->getExprLoc());
        auto intTy = mlir::cast<cir::IntType>(arg.getType());
        mlir::Value zero;
        if (intTy == builder.getSInt32Ty())
          zero =
              builder.getConstant(absLoc, builder.getSInt32AttrLocalCached(0));
        else if (intTy == builder.getSInt64Ty())
          zero =
              builder.getConstant(absLoc, builder.getSInt64AttrLocalCached(0));
        else
          zero = builder.getConstantInt(absLoc, intTy, 0);
        auto overflowOp = cir::BinOpOverflowOp::create(
            builder, absLoc, intTy, cir::BinOpOverflowKind::Sub, zero, arg);
        overflowOp->setAttr("cir.nosanitize",
                            mlir::UnitAttr::get(builder.getContext()));
        mlir::Value negated = overflowOp.getResult();
        mlir::Value overflow = overflowOp.getOverflow();
        mlir::Value noOverflow = builder.createNot(overflow);
        if (auto *op = noOverflow.getDefiningOp())
          op->setAttr("cir.nosanitize",
                      mlir::UnitAttr::get(builder.getContext()));

        // Check if sanitizer is in trap mode or handler mode.
        SanitizerKind::SanitizerOrdinal ordinal =
            SanitizerKind::SO_SignedIntegerOverflow;
        if (sanOpts.has(ordinal)) {
          noOverflow =
              emitGuardedUBSanCheck(absLoc, noOverflow, ordinal);
        }

        bool isTrap =
            cgm.getCodeGenOpts().SanitizeTrap.has(ordinal);
        bool isRecover =
            !isTrap &&
            cgm.getCodeGenOpts().SanitizeRecover.has(ordinal);

        if (sanOpts.has(ordinal) && !isTrap) {
          // Handler mode: emit negate_overflow handler call.
          auto ifOp = cir::IfOp::create(
              builder, absLoc, noOverflow, /*withElseRegion=*/true,
              [&](mlir::OpBuilder &, mlir::Location ifLoc) {
                cir::YieldOp::create(builder, ifLoc);
              },
              [&](mlir::OpBuilder &, mlir::Location ifLoc) {
                // Emit negate overflow handler stub.
                auto intptrTy = builder.getUIntNTy(
                    getContext()
                        .getTypeSize(getContext().getUIntPtrType()));
                auto stubFnTy = cir::FuncType::get({intptrTy},
                                                    builder.getVoidTy());
                auto stubFn = cgm.createRuntimeFunction(
                    stubFnTy, "__cir_ubsan_overflow_handler_negate",
                    /*attrs=*/{}, /*isLocal=*/false,
                    /*assumeConvergent=*/false);
                auto argExt = cir::CastOp::create(
                    builder, ifLoc, intptrTy, cir::CastKind::integral, arg);
                argExt->setAttr(
                    "cir.nosanitize",
                    mlir::UnitAttr::get(builder.getContext()));
                auto call = builder.createCallOp(ifLoc, stubFn, {argExt});
                SourceLocation exprLoc = e->getExprLoc();
                PresumedLoc PLoc =
                    getContext().getSourceManager().getPresumedLoc(exprLoc);
                if (PLoc.isValid()) {
                  call->setDiscardableAttr(
                      "cir.ubsan.source_file",
                      builder.getStringAttr(PLoc.getFilename()));
                  call->setDiscardableAttr(
                      "cir.ubsan.source_line",
                      builder.getI32IntegerAttr(PLoc.getLine()));
                  call->setDiscardableAttr(
                      "cir.ubsan.source_column",
                      builder.getI32IntegerAttr(PLoc.getColumn()));
                }
                // Set type descriptor attrs for the handler.
                SmallString<32> typeName;
                cgm.getDiags().ConvertArgToString(
                    DiagnosticsEngine::ak_qualtype,
                    (intptr_t)e->getType().getAsOpaquePtr(),
                    StringRef(), StringRef(), {}, typeName, {});
                call->setDiscardableAttr("cir.ubsan.type_name",
                                         builder.getStringAttr(typeName));
                call->setDiscardableAttr(
                    "cir.ubsan.handler_name",
                    builder.getStringAttr("negate_overflow"));
                call->setDiscardableAttr(
                    "cir.ubsan.handler_kind",
                    builder.getStringAttr(isRecover ? "recover" : "abort"));
                call->setDiscardableAttr(
                    "cir.nosanitize",
                    mlir::UnitAttr::get(builder.getContext()));
                if (!isRecover) {
                  auto unreachable =
                      cir::UnreachableOp::create(builder, ifLoc);
                  unreachable->setAttr(
                      "cir.nosanitize",
                      mlir::UnitAttr::get(builder.getContext()));
                } else {
                  auto yield = cir::YieldOp::create(builder, ifLoc);
                  yield->setAttr("cir.nosanitize",
                                 mlir::UnitAttr::get(builder.getContext()));
                }
              });
          ifOp->setAttr("cir.branch_weights",
                        builder.getDenseI32ArrayAttr({1048575, 1}));
          ifOp->setAttr("cir.nosanitize",
                        mlir::UnitAttr::get(builder.getContext()));
        } else if (sanOpts.has(ordinal) && isTrap) {
          // Trap mode.
          auto trapIf = cir::IfOp::create(
              builder, absLoc, noOverflow, /*withElseRegion=*/true,
              [&](mlir::OpBuilder &, mlir::Location ifLoc) {
                cir::YieldOp::create(builder, ifLoc);
              },
              [&](mlir::OpBuilder &, mlir::Location ifLoc) {
                auto trapOp = cir::TrapOp::create(builder, ifLoc);
                trapOp->setAttr(
                    "cir.ubsantrap_handler",
                    mlir::IntegerAttr::get(
                        mlir::IntegerType::get(builder.getContext(), 8),
                        13)); // NegateOverflow
                trapOp->setAttr("cir.nosanitize",
                                mlir::UnitAttr::get(builder.getContext()));
              });
          trapIf->setAttr("cir.nosanitize",
                          mlir::UnitAttr::get(builder.getContext()));
        }

        // abs(x) = x >= 0 ? x : -x (using the negated value from overflow
        // check)
        mlir::Value isNeg = cir::CmpOp::create(builder, absLoc,
                                                cir::CmpOpKind::lt, arg, zero);
        isNeg.getDefiningOp()->setAttr(
            "cir.nosanitize", mlir::UnitAttr::get(builder.getContext()));
        result = cir::SelectOp::create(builder, absLoc, isNeg, negated, arg);
        result.getDefiningOp()->setAttr(
            "cir.nosanitize", mlir::UnitAttr::get(builder.getContext()));
        break;
      }
    }
    }
    return RValue::get(result);
  }
  case Builtin::BI__assume:
  case Builtin::BI__builtin_assume: {
    if (e->getArg(0)->HasSideEffects(getContext()))
      return RValue::get(nullptr);

    mlir::Value argValue = emitCheckedArgForAssume(e->getArg(0));
    cir::AssumeOp::create(builder, loc, argValue);
    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_assume_separate_storage: {
    mlir::Value value0 = emitScalarExpr(e->getArg(0));
    mlir::Value value1 = emitScalarExpr(e->getArg(1));
    cir::AssumeSepStorageOp::create(builder, loc, value0, value1);
    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_assume_dereferenceable: {
    mlir::Value ptrValue = emitScalarExpr(e->getArg(0));
    mlir::Value sizeValue = emitScalarExpr(e->getArg(1));
    cir::AssumeDereferenceableOp::create(builder, loc, ptrValue, sizeValue);
    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_assume_aligned: {
    const Expr *ptrExpr = e->getArg(0);
    mlir::Value ptrValue = emitScalarExpr(ptrExpr);
    mlir::Value offsetValue =
        (e->getNumArgs() > 2) ? emitScalarExpr(e->getArg(2)) : nullptr;

    std::optional<llvm::APSInt> alignment =
        e->getArg(1)->getIntegerConstantExpr(getContext());
    assert(alignment.has_value() &&
           "the second argument to __builtin_assume_aligned must be an "
           "integral constant expression");

    // OGCG clamps the alignment to llvm::Value::MaximumAlignment to avoid
    // overflow when the user-specified alignment exceeds LLVM's limit.
    int64_t alignVal =
        alignment->ugt(llvm::Value::MaximumAlignment)
            ? static_cast<int64_t>(llvm::Value::MaximumAlignment)
            : alignment->getSExtValue();

    mlir::Value result =
        emitAlignmentAssumption(ptrValue, ptrExpr, ptrExpr->getExprLoc(),
                                alignVal, offsetValue);
    return RValue::get(result);
  }

  case Builtin::BI__builtin_complex: {
    mlir::Value real = emitScalarExpr(e->getArg(0));
    mlir::Value imag = emitScalarExpr(e->getArg(1));
    mlir::Value complex = builder.createComplexCreate(loc, real, imag);
    return RValue::getComplex(complex);
  }

  case Builtin::BI__builtin_creal:
  case Builtin::BI__builtin_crealf:
  case Builtin::BI__builtin_creall:
  case Builtin::BIcreal:
  case Builtin::BIcrealf:
  case Builtin::BIcreall: {
    mlir::Value complex = emitComplexExpr(e->getArg(0));
    mlir::Value real = builder.createComplexReal(loc, complex);
    return RValue::get(real);
  }

  case Builtin::BI__builtin_cimag:
  case Builtin::BI__builtin_cimagf:
  case Builtin::BI__builtin_cimagl:
  case Builtin::BIcimag:
  case Builtin::BIcimagf:
  case Builtin::BIcimagl: {
    mlir::Value complex = emitComplexExpr(e->getArg(0));
    mlir::Value imag = builder.createComplexImag(loc, complex);
    return RValue::get(imag);
  }

  case Builtin::BI__builtin_conj:
  case Builtin::BI__builtin_conjf:
  case Builtin::BI__builtin_conjl:
  case Builtin::BIconj:
  case Builtin::BIconjf:
  case Builtin::BIconjl: {
    mlir::Value complex = emitComplexExpr(e->getArg(0));
    mlir::Value conj = builder.createUnaryOp(getLoc(e->getExprLoc()),
                                             cir::UnaryOpKind::Not, complex);
    return RValue::getComplex(conj);
  }

  case Builtin::BI__builtin_clrsb:
  case Builtin::BI__builtin_clrsbl:
  case Builtin::BI__builtin_clrsbll:
    return emitBuiltinBitOp<cir::BitClrsbOp>(*this, e);

  case Builtin::BI__builtin_ctzs:
  case Builtin::BI__builtin_ctz:
  case Builtin::BI__builtin_ctzl:
  case Builtin::BI__builtin_ctzll:
  case Builtin::BI__builtin_ctzg: {
    bool hasFallback = builtinIDIfNoAsmLabel == Builtin::BI__builtin_ctzg &&
                       e->getNumArgs() > 1;
    // Match OGCG EmitCheckedArgForBuiltin: emit arg once, then optionally
    // emit UBSan check, and pass the pre-emitted value to the bit op to
    // avoid double-evaluating the argument expression.
    mlir::Value argValue;
    if (!hasFallback && sanOpts.has(SanitizerKind::Builtin)) {
      argValue = bitcastBoolVecToInt(builder, emitScalarExpr(e->getArg(0)));
      argValue = promoteScalarBoolToInt(builder, argValue);
      mlir::Location loc = getLoc(e->getExprLoc());
      mlir::Value zero = builder.getNullValue(argValue.getType(), loc);
      mlir::Value isNotZero =
          builder.createCompare(loc, cir::CmpOpKind::ne, argValue, zero);
      markNoSanitize(isNotZero.getDefiningOp());
      emitBuiltinSanitizerCheck(*this, isNotZero, e->getArg(0)->getExprLoc(),
                                /*checkKind=*/0); // BCK_CTZPassedZero
    }
    // Match OGCG: use the target hook to determine zero-is-poison.
    // AArch64 returns false (CLZ/CTZ are well-defined for zero),
    // x86 returns true (BSR/BSF are undefined for zero).
    bool poisonZero = hasFallback || getTarget().isCLZForZeroUndef();
    return emitBuiltinBitOp<cir::BitCtzOp>(*this, e, poisonZero, hasFallback,
                                           argValue);
  }

  case Builtin::BI__builtin_clzs:
  case Builtin::BI__builtin_clz:
  case Builtin::BI__builtin_clzl:
  case Builtin::BI__builtin_clzll:
  case Builtin::BI__builtin_clzg: {
    bool hasFallback = builtinIDIfNoAsmLabel == Builtin::BI__builtin_clzg &&
                       e->getNumArgs() > 1;
    // Match OGCG EmitCheckedArgForBuiltin: emit arg once, then optionally
    // emit UBSan check, and pass the pre-emitted value to the bit op.
    mlir::Value argValue;
    if (!hasFallback && sanOpts.has(SanitizerKind::Builtin)) {
      argValue = bitcastBoolVecToInt(builder, emitScalarExpr(e->getArg(0)));
      argValue = promoteScalarBoolToInt(builder, argValue);
      mlir::Location loc = getLoc(e->getExprLoc());
      mlir::Value zero = builder.getNullValue(argValue.getType(), loc);
      mlir::Value isNotZero =
          builder.createCompare(loc, cir::CmpOpKind::ne, argValue, zero);
      markNoSanitize(isNotZero.getDefiningOp());
      emitBuiltinSanitizerCheck(*this, isNotZero, e->getArg(0)->getExprLoc(),
                                /*checkKind=*/1); // BCK_CLZPassedZero
    }
    bool poisonZero = hasFallback || getTarget().isCLZForZeroUndef();
    return emitBuiltinBitOp<cir::BitClzOp>(*this, e, poisonZero, hasFallback,
                                           argValue);
  }

  case Builtin::BI__builtin_elementwise_ctzg:
  case Builtin::BI__builtin_elementwise_clzg: {
    mlir::Value arg = emitScalarExpr(e->getArg(0));
    llvm::StringRef intrinsicName =
        builtinIDIfNoAsmLabel == Builtin::BI__builtin_elementwise_clzg
            ? "ctlz"
            : "cttz";
    mlir::Value zeroIsUndef = builder.getTrue(getLoc(e->getExprLoc()));
    SmallVector<mlir::Value> args = {arg, zeroIsUndef};
    mlir::Value result =
        cir::LLVMIntrinsicCallOp::create(
            builder, loc, cgm.getStringAttrCached(intrinsicName), arg.getType(),
            args)
            .getResult();

    mlir::Type exprTy = convertType(e->getType());
    if (result.getType() != exprTy) {
      if (mlir::isa<cir::VectorType>(result.getType()) &&
          mlir::isa<cir::VectorType>(exprTy))
        result = builder.createBitcast(result, exprTy);
      else
        result = builder.createIntCast(result, exprTy);
    }

    if (e->getNumArgs() <= 1)
      return RValue::get(result);

    mlir::Value isZero;
    if (auto vecTy = mlir::dyn_cast<cir::VectorType>(arg.getType())) {
      mlir::Value zero = builder.getNullValue(arg.getType(), loc);
      mlir::Value cmp = builder.createVecCompare(loc, cir::CmpOpKind::eq, arg,
                                                 zero);
      auto boolVecTy =
          cir::VectorType::get(builder.getBoolTy(), vecTy.getSize());
      isZero = builder.createCast(loc, cir::CastKind::int_to_bool, cmp,
                                  boolVecTy);
    } else {
      mlir::Value zero = builder.getNullValue(arg.getType(), loc);
      isZero = builder.createCompare(loc, cir::CmpOpKind::eq, arg, zero);
    }
    mlir::Value fallback = emitScalarExpr(e->getArg(1));
    mlir::Value selected = builder.createSelect(loc, isZero, fallback, result);
    return RValue::get(selected);
  }

  case Builtin::BI__builtin_ffs:
  case Builtin::BI__builtin_ffsl:
  case Builtin::BI__builtin_ffsll:
    return emitBuiltinBitOp<cir::BitFfsOp>(*this, e);

  case Builtin::BI__builtin_parity:
  case Builtin::BI__builtin_parityl:
  case Builtin::BI__builtin_parityll:
    return emitBuiltinBitOp<cir::BitParityOp>(*this, e);

  case Builtin::BI__lzcnt16:
  case Builtin::BI__lzcnt:
  case Builtin::BI__lzcnt64:
    return emitBuiltinBitOp<cir::BitClzOp>(*this, e, /*poisonZero=*/false);

  case Builtin::BI__popcnt16:
  case Builtin::BI__popcnt:
  case Builtin::BI__popcnt64:
  case Builtin::BI__builtin_popcount:
  case Builtin::BI__builtin_popcountl:
  case Builtin::BI__builtin_popcountll:
  case Builtin::BI__builtin_popcountg:
    return emitBuiltinBitOp<cir::BitPopcountOp>(*this, e);

  case Builtin::BI__builtin_expect:
  case Builtin::BI__builtin_expect_with_probability: {
    mlir::Value argValue = emitScalarExpr(e->getArg(0));
    mlir::Value expectedValue = emitScalarExpr(e->getArg(1));

    mlir::FloatAttr probAttr;
    if (builtinIDIfNoAsmLabel == Builtin::BI__builtin_expect_with_probability) {
      llvm::APFloat probability(0.0);
      const Expr *probArg = e->getArg(2);
      [[maybe_unused]] bool evalSucceeded =
          probArg->EvaluateAsFloat(probability, cgm.getASTContext());
      assert(evalSucceeded &&
             "probability should be able to evaluate as float");
      bool loseInfo = false; // ignored
      probability.convert(llvm::APFloat::IEEEdouble(),
                          llvm::RoundingMode::Dynamic, &loseInfo);
      probAttr = mlir::FloatAttr::get(mlir::Float64Type::get(&getMLIRContext()),
                                      probability);
    }

    auto result = cir::ExpectOp::create(builder, loc, argValue.getType(),
                                        argValue, expectedValue, probAttr);
    return RValue::get(result);
  }

  case Builtin::BI__builtin_unpredictable:
    // Just return the argument. LLVM does not handle this builtin directly.
    // Metadata for this builtin should be added to branches/switches that use
    // it.
    return RValue::get(emitScalarExpr(e->getArg(0)));

  case Builtin::BI__builtin_annotation: {
    mlir::Value annVal = emitScalarExpr(e->getArg(0));

    // Get the annotation string, go through casts. Sema requires this to be a
    // non-wide string literal, potentially casted, so the cast<> is safe.
    const Expr *annotationStrExpr = e->getArg(1)->IgnoreParenCasts();
    StringRef str = cast<StringLiteral>(annotationStrExpr)->getString();

    auto result = cir::BuiltinAnnotationOp::create(
        builder, getLoc(e->getExprLoc()), annVal.getType(), annVal,
        cgm.getStringAttrCached(str));
    return RValue::get(result);
  }

  case Builtin::BI__builtin_bswapg: {
    // __builtin_bswapg is generic over integer types. For 1-bit or 8-bit
    // integers, byte swap is identity — return the input unchanged.
    // For wider types, emit cir.byte_swap as usual.
    // BoolType is 1-bit in CIR, so byte swap is also identity.
    mlir::Value arg = emitScalarExpr(e->getArg(0));
    if (mlir::isa<cir::BoolType>(arg.getType()))
      return RValue::get(arg);
    auto intTy = mlir::cast<cir::IntType>(arg.getType());
    if (intTy.getWidth() == 1 || intTy.getWidth() == 8)
      return RValue::get(arg);
    return RValue::get(cir::ByteSwapOp::create(builder, loc, arg));
  }
  case Builtin::BI__builtin_bswap16:
  case Builtin::BI__builtin_bswap32:
  case Builtin::BI__builtin_bswap64:
  case Builtin::BI_byteswap_ushort:
  case Builtin::BI_byteswap_ulong:
  case Builtin::BI_byteswap_uint64: {
    mlir::Value arg = emitScalarExpr(e->getArg(0));
    return RValue::get(cir::ByteSwapOp::create(builder, loc, arg));
  }

  case Builtin::BI__builtin_bitreverse8:
  case Builtin::BI__builtin_bitreverse16:
  case Builtin::BI__builtin_bitreverse32:
  case Builtin::BI__builtin_bitreverse64: {
    mlir::Value arg = emitScalarExpr(e->getArg(0));
    return RValue::get(cir::BitReverseOp::create(builder, loc, arg));
  }

  case Builtin::BI__builtin_rotateleft8:
  case Builtin::BI__builtin_rotateleft16:
  case Builtin::BI__builtin_rotateleft32:
  case Builtin::BI__builtin_rotateleft64:
    return emitRotate(e, /*isRotateLeft=*/true);

  case Builtin::BI__builtin_rotateright8:
  case Builtin::BI__builtin_rotateright16:
  case Builtin::BI__builtin_rotateright32:
  case Builtin::BI__builtin_rotateright64:
    return emitRotate(e, /*isRotateLeft=*/false);

  case Builtin::BI__builtin_coro_id: {
    // __builtin_coro_id(align, promise, coroaddr, fnaddrs) -> token (u32 in CIR)
    // When called from <coroutine> header or user code, emit all 4 arguments
    // as-is. CIRGenCoroutine.cpp handles the implicit coroutine body path.
    mlir::Location callLoc = getLoc(e->getSourceRange());
    cir::IntType int32Ty = builder.getUInt32Ty();

    mlir::Operation *builtin = cgm.getGlobalValue(cgm.builtinCoroId);
    cir::FuncOp fnOp;
    if (!builtin) {
      fnOp = cgm.createCIRBuiltinFunction(
          callLoc, cgm.builtinCoroId,
          cir::FuncType::get({int32Ty, voidPtrTy, voidPtrTy, voidPtrTy},
                             int32Ty),
          /*FD=*/nullptr);
    } else {
      fnOp = cast<cir::FuncOp>(builtin);
    }

    // Emit all 4 AST arguments: align, promise, coroaddr, fnaddrs.
    // The AST align is signed int, but CIR coro builtins use u32.
    mlir::Value align = emitScalarExpr(e->getArg(0));
    if (align.getType() != int32Ty)
      align = builder.createIntCast(align, int32Ty);
    mlir::Value promise = emitScalarExpr(e->getArg(1));
    mlir::Value coroaddr = emitScalarExpr(e->getArg(2));
    mlir::Value fnaddrs = emitScalarExpr(e->getArg(3));

    auto call = builder.createCallOp(
        callLoc, fnOp,
        mlir::ValueRange{align, promise, coroaddr, fnaddrs});

    // When __builtin_coro_id is used directly (C coroutine builtins),
    // initialize CoroData so later builtins (coro_alloc, coro_begin,
    // coro_free) can find the coro ID. Matches OGCG behavior.
    initCoroData(call, e);

    return RValue::get(call.getResult());
  }
  case Builtin::BI__builtin_coro_promise: {
    // __builtin_coro_promise(handle, align, from) -> ptr
    mlir::Location callLoc = getLoc(e->getSourceRange());
    mlir::Value handle = emitScalarExpr(e->getArg(0));
    mlir::Value align = emitScalarExpr(e->getArg(1));
    mlir::Value from = emitScalarExpr(e->getArg(2));

    // The AST align is 'int' (s32i). Match that in the function signature.
    cir::IntType alignTy =
        mlir::cast<cir::IntType>(convertType(e->getArg(1)->getType()));
    cir::BoolType boolTy = builder.getBoolTy();

    mlir::Operation *builtin =
        cgm.getGlobalValue(cgm.builtinCoroPromise);
    cir::FuncOp fnOp;
    if (!builtin) {
      fnOp = cgm.createCIRBuiltinFunction(
          callLoc, cgm.builtinCoroPromise,
          cir::FuncType::get({voidPtrTy, alignTy, boolTy}, voidPtrTy),
          /*fd=*/nullptr);
    } else {
      fnOp = cast<cir::FuncOp>(builtin);
    }
    auto call = builder.createCallOp(callLoc, fnOp,
                                     mlir::ValueRange{handle, align, from});
    return RValue::get(call.getResult());
  }
  case Builtin::BI__builtin_coro_resume: {
    // __builtin_coro_resume(handle) -> void
    mlir::Location callLoc = getLoc(e->getSourceRange());
    mlir::Value handle = emitScalarExpr(e->getArg(0));

    mlir::Operation *builtin = cgm.getGlobalValue(cgm.builtinCoroResume);
    cir::FuncOp fnOp;
    if (!builtin) {
      fnOp = cgm.createCIRBuiltinFunction(
          callLoc, cgm.builtinCoroResume,
          cir::FuncType::get({voidPtrTy}, builder.getVoidTy()),
          /*fd=*/nullptr);
    } else {
      fnOp = cast<cir::FuncOp>(builtin);
    }
    builder.createCallOp(callLoc, fnOp, mlir::ValueRange{handle});
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_coro_noop: {
    // __builtin_coro_noop() -> ptr
    mlir::Location callLoc = getLoc(e->getSourceRange());

    mlir::Operation *builtin = cgm.getGlobalValue(cgm.builtinCoroNoop);
    cir::FuncOp fnOp;
    if (!builtin) {
      fnOp = cgm.createCIRBuiltinFunction(callLoc, cgm.builtinCoroNoop,
                                           cir::FuncType::get({}, voidPtrTy),
                                           /*fd=*/nullptr);
    } else {
      fnOp = cast<cir::FuncOp>(builtin);
    }
    auto call =
        builder.createCallOp(callLoc, fnOp, mlir::ValueRange{});
    return RValue::get(call.getResult());
  }
  case Builtin::BI__builtin_coro_destroy: {
    // __builtin_coro_destroy(handle) -> void
    mlir::Location callLoc = getLoc(e->getSourceRange());
    mlir::Value handle = emitScalarExpr(e->getArg(0));

    mlir::Operation *builtin =
        cgm.getGlobalValue(cgm.builtinCoroDestroy);
    cir::FuncOp fnOp;
    if (!builtin) {
      fnOp = cgm.createCIRBuiltinFunction(
          callLoc, cgm.builtinCoroDestroy,
          cir::FuncType::get({voidPtrTy}, builder.getVoidTy()),
          /*fd=*/nullptr);
    } else {
      fnOp = cast<cir::FuncOp>(builtin);
    }
    builder.createCallOp(callLoc, fnOp, mlir::ValueRange{handle});
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_coro_done: {
    // __builtin_coro_done(handle) -> bool
    mlir::Location callLoc = getLoc(e->getSourceRange());
    mlir::Value handle = emitScalarExpr(e->getArg(0));

    mlir::Operation *builtin = cgm.getGlobalValue(cgm.builtinCoroDone);
    cir::FuncOp fnOp;
    if (!builtin) {
      fnOp = cgm.createCIRBuiltinFunction(
          callLoc, cgm.builtinCoroDone,
          cir::FuncType::get({voidPtrTy}, builder.getBoolTy()),
          /*fd=*/nullptr);
    } else {
      fnOp = cast<cir::FuncOp>(builtin);
    }
    auto call =
        builder.createCallOp(callLoc, fnOp, mlir::ValueRange{handle});
    return RValue::get(call.getResult());
  }
  case Builtin::BI__builtin_coro_alloc: {
    // __builtin_coro_alloc() -> bool
    // In the AST, coro_alloc takes no arguments. The coro ID token is
    // prepended implicitly (same as OGCG).
    mlir::Location callLoc = getLoc(e->getSourceRange());

    mlir::Operation *builtin =
        cgm.getGlobalValue(cgm.builtinCoroAlloc);
    cir::FuncOp fnOp;
    if (!builtin) {
      fnOp = cgm.createCIRBuiltinFunction(
          callLoc, cgm.builtinCoroAlloc,
          cir::FuncType::get({builder.getUInt32Ty()}, builder.getBoolTy()),
          /*fd=*/nullptr);
    } else {
      fnOp = cast<cir::FuncOp>(builtin);
    }
    auto call = builder.createCallOp(
        callLoc, fnOp, mlir::ValueRange{getCoroIdResult()});
    return RValue::get(call.getResult());
  }
  case Builtin::BI__builtin_coro_begin: {
    // __builtin_coro_begin(mem) -> ptr
    // In the AST, coro_begin takes one argument (mem). The coro ID token is
    // prepended implicitly.
    mlir::Location callLoc = getLoc(e->getSourceRange());
    mlir::Value mem = emitScalarExpr(e->getArg(0));

    mlir::Operation *builtin =
        cgm.getGlobalValue(cgm.builtinCoroBegin);
    cir::FuncOp fnOp;
    if (!builtin) {
      fnOp = cgm.createCIRBuiltinFunction(
          callLoc, cgm.builtinCoroBegin,
          cir::FuncType::get({builder.getUInt32Ty(), voidPtrTy}, voidPtrTy),
          /*fd=*/nullptr);
    } else {
      fnOp = cast<cir::FuncOp>(builtin);
    }
    auto call = builder.createCallOp(
        callLoc, fnOp,
        mlir::ValueRange{getCoroIdResult(), mem});

    // Store the coro.begin result so __builtin_coro_frame() can use it.
    // Matches OGCG behavior.
    setCoroBeginValue(call.getResult());

    return RValue::get(call.getResult());
  }
  case Builtin::BI__builtin_coro_end: {
    // __builtin_coro_end(handle, unwind) -> bool
    mlir::Location callLoc = getLoc(e->getSourceRange());
    mlir::Value handle = emitScalarExpr(e->getArg(0));
    mlir::Value unwind = emitScalarExpr(e->getArg(1));

    mlir::Operation *builtin = cgm.getGlobalValue(cgm.builtinCoroEnd);
    cir::FuncOp fnOp;
    if (!builtin) {
      fnOp = cgm.createCIRBuiltinFunction(
          callLoc, cgm.builtinCoroEnd,
          cir::FuncType::get({voidPtrTy, builder.getBoolTy()},
                             builder.getBoolTy()),
          /*fd=*/nullptr);
    } else {
      fnOp = cast<cir::FuncOp>(builtin);
    }
    auto call = builder.createCallOp(callLoc, fnOp,
                                     mlir::ValueRange{handle, unwind});
    return RValue::get(call.getResult());
  }
  case Builtin::BI__builtin_coro_suspend: {
    // __builtin_coro_suspend(final) -> i8
    // In the AST, coro_suspend takes one argument (i1 final). A save token
    // is prepended implicitly.
    mlir::Location callLoc = getLoc(e->getSourceRange());
    mlir::Value finalVal = emitScalarExpr(e->getArg(0));

    mlir::Operation *builtin =
        cgm.getGlobalValue(cgm.builtinCoroSuspend);
    cir::FuncOp fnOp;
    if (!builtin) {
      // CIR signature: (u32 save_token, bool final) -> s8
      fnOp = cgm.createCIRBuiltinFunction(
          callLoc, cgm.builtinCoroSuspend,
          cir::FuncType::get({builder.getUInt32Ty(), builder.getBoolTy()},
                             cir::IntType::get(builder.getContext(), 8,
                                               /*isSigned=*/true)),
          /*fd=*/nullptr);
    } else {
      fnOp = cast<cir::FuncOp>(builtin);
    }
    // Prepend a ConstantTokenNone equivalent: use 0u32 as a placeholder
    // token. The lowering will replace this with llvm.mlir.none_token.
    auto noneToken = builder.getUInt32(0, callLoc);
    auto call = builder.createCallOp(
        callLoc, fnOp, mlir::ValueRange{noneToken, finalVal});
    return RValue::get(call.getResult());
  }
  case Builtin::BI__builtin_coro_align: {
    // __builtin_coro_align() -> size_t
    // Emit a plain named call to __builtin_coro_align (lowered to
    // llvm.coro.align by lowerCoroBuiltinCall), mirroring the other coro
    // builtins. Routing through emitCall instead would subject the callee to
    // the call-vs-invoke decision (turning it into an invoke under active EH
    // cleanups) and to -fsanitize=function CFI instrumentation, both of which
    // reference the non-lowered __builtin_coro_align symbol and cause link
    // failures. The direct createCallOp path is immune to both.
    mlir::Location callLoc = getLoc(e->getSourceRange());
    mlir::Type resTy = convertType(e->getType());
    mlir::Operation *builtin = cgm.getGlobalValue(cgm.builtinCoroAlign);
    cir::FuncOp fnOp;
    if (!builtin) {
      fnOp = cgm.createCIRBuiltinFunction(
          callLoc, cgm.builtinCoroAlign,
          cir::FuncType::get({}, resTy), /*fd=*/nullptr);
    } else {
      fnOp = cast<cir::FuncOp>(builtin);
    }
    auto call = builder.createCallOp(callLoc, fnOp, mlir::ValueRange{});
    return RValue::get(call.getResult());
  }

  case Builtin::BI__builtin_coro_frame: {
    return emitCoroutineFrame();
  }
  case Builtin::BI__builtin_coro_free: {
    // __builtin_coro_free(handle) in clang AST, but llvm.coro.free needs
    // (token id, ptr handle). We emit as __builtin_coro_free(coroId, handle)
    // in CIR to match the LLVM intrinsic signature.
    mlir::Location callLoc = getLoc(e->getSourceRange());
    mlir::Value handle = emitScalarExpr(e->getArg(0));
    cir::FuncOp fnOp;
    mlir::Operation *builtin = cgm.getGlobalValue(cgm.builtinCoroFree);
    if (!builtin) {
      fnOp = cgm.createCIRBuiltinFunction(
          callLoc, cgm.builtinCoroFree,
          cir::FuncType::get({uInt32Ty, voidPtrTy}, voidPtrTy),
          /*fd=*/nullptr);
    } else {
      fnOp = cast<cir::FuncOp>(builtin);
    }
    auto call = builder.createCallOp(
        callLoc, fnOp,
        mlir::ValueRange{getCoroIdResult(), handle});
    return RValue::get(call.getResult());
  }
  case Builtin::BI__builtin_coro_size: {
    // __builtin_coro_size() -> size_t
    // Emit a plain named call to __builtin_coro_size (lowered to llvm.coro.size
    // by lowerCoroBuiltinCall), mirroring the other coro builtins. See the
    // __builtin_coro_align case for why routing through emitCall is wrong:
    // under active EH cleanups (e.g. -fsanitize=undefined) it becomes an invoke
    // and it also gets -fsanitize=function CFI instrumentation, both of which
    // leave a dangling reference to the non-lowered __builtin_coro_size symbol.
    mlir::Location callLoc = getLoc(e->getSourceRange());
    mlir::Type resTy = convertType(e->getType());
    mlir::Operation *builtin = cgm.getGlobalValue(cgm.builtinCoroSize);
    cir::FuncOp fnOp;
    if (!builtin) {
      fnOp = cgm.createCIRBuiltinFunction(
          callLoc, cgm.builtinCoroSize,
          cir::FuncType::get({}, resTy), /*fd=*/nullptr);
    } else {
      fnOp = cast<cir::FuncOp>(builtin);
    }
    auto call = builder.createCallOp(callLoc, fnOp, mlir::ValueRange{});
    return RValue::get(call.getResult());
  }

  case Builtin::BI__builtin_constant_p: {
    mlir::Type resultType = convertType(e->getType());

    const Expr *arg = e->getArg(0);
    QualType argType = arg->getType();
    // FIXME: The allowance for Obj-C pointers and block pointers is historical
    // and likely a mistake.
    if (!argType->isIntegralOrEnumerationType() && !argType->isFloatingType() &&
        !argType->isObjCObjectPointerType() && !argType->isBlockPointerType()) {
      // Per the GCC documentation, only numeric constants are recognized after
      // inlining.
      return RValue::get(
          builder.getConstInt(getLoc(e->getSourceRange()),
                              mlir::cast<cir::IntType>(resultType), 0));
    }

    if (arg->HasSideEffects(getContext())) {
      // The argument is unevaluated, so be conservative if it might have
      // side-effects.
      return RValue::get(
          builder.getConstInt(getLoc(e->getSourceRange()),
                              mlir::cast<cir::IntType>(resultType), 0));
    }

    mlir::Value argValue = emitScalarExpr(arg);
    if (argType->isObjCObjectPointerType()) {
      // Match OGCG and cast Objective-C object pointers to id because the
      // lowered pointer types for Obj-C classes are opaque.
      argType = getContext().getObjCIdType();
      argValue = builder.createBitcast(getLoc(e->getSourceRange()), argValue,
                                       convertType(argType));
    }

    mlir::Value result = cir::IsConstantOp::create(
        builder, getLoc(e->getSourceRange()), argValue);
    // IsConstantOp returns a bool, but __builtin_constant_p returns an int.
    result = builder.createBoolToInt(result, resultType);
    return RValue::get(result);
  }
  case Builtin::BI__builtin_dynamic_object_size:
  case Builtin::BI__builtin_object_size: {
    unsigned type =
        e->getArg(1)->EvaluateKnownConstInt(getContext()).getZExtValue();
    auto resType = mlir::cast<cir::IntType>(convertType(e->getType()));

    // We pass this builtin onto the optimizer so that it can figure out the
    // object size in more complex cases.
    bool isDynamic = builtinID == Builtin::BI__builtin_dynamic_object_size;
    return RValue::get(emitBuiltinObjectSize(e->getArg(0), type, resType,
                                             /*EmittedE=*/nullptr, isDynamic));
  }

  case Builtin::BI__builtin_prefetch: {
    auto evaluateOperandAsInt = [&](const Expr *arg) {
      Expr::EvalResult res;
      [[maybe_unused]] bool evalSucceed =
          arg->EvaluateAsInt(res, cgm.getASTContext());
      assert(evalSucceed && "expression should be able to evaluate as int");
      return res.Val.getInt().getZExtValue();
    };

    bool isWrite = false;
    if (e->getNumArgs() > 1)
      isWrite = evaluateOperandAsInt(e->getArg(1));

    int locality = 3;
    if (e->getNumArgs() > 2)
      locality = evaluateOperandAsInt(e->getArg(2));

    mlir::Value address = emitScalarExpr(e->getArg(0));
    cir::PrefetchOp::create(builder, loc, address, locality, isWrite);
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_readcyclecounter: {
    mlir::Type u64Ty = builder.getUInt64Ty();
    auto op = cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("readcyclecounter"), u64Ty,
        mlir::ValueRange{});
    return RValue::get(op.getResult());
  }
  case Builtin::BI__builtin_readsteadycounter: {
    mlir::Type u64Ty = builder.getUInt64Ty();
    auto op = cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("readsteadycounter"), u64Ty,
        mlir::ValueRange{});
    return RValue::get(op.getResult());
  }
  case Builtin::BI__builtin_allow_runtime_check: {
    // Model __builtin_allow_runtime_check as a normal CIR call plus a
    // call-site string marker that lowerings can rewrite into
    // llvm.allow.runtime.check(metadata !"kind").
    StringRef kind =
        cast<StringLiteral>(e->getArg(0)->IgnoreParenCasts())->getString();
    constexpr llvm::StringLiteral allowRuntimeCheckThunkName =
        "__cir_allow_runtime_check";

    cir::FuncOp fnOp = cgm.createRuntimeFunction(
        cir::FuncType::get({voidPtrTy}, builder.getBoolTy()),
        allowRuntimeCheckThunkName,
        /*attrs=*/{}, /*isLocal=*/false, /*assumeConvergent=*/false);

    mlir::Value placeholderKindArg = builder.getNullPtr(voidPtrTy, loc);
    auto call =
        builder.createCallOp(loc, fnOp, mlir::ValueRange{placeholderKindArg});
    call->setAttr("cir.call_allow_runtime_check_kind",
                  cgm.getStringAttrCached(kind));
    return RValue::get(call.getResult());
  }
  case Builtin::BI__builtin_allow_sanitize_check: {
    StringRef name =
        cast<StringLiteral>(e->getArg(0)->IgnoreParenCasts())->getString();
    StringRef intrinsicName;

    // Match OGCG: accept kernel and non-kernel names interchangeably whenever
    // one of the corresponding sanitizer modes is enabled for this function.
    if (getLangOpts().Sanitize.hasOneOf(SanitizerKind::Address |
                                        SanitizerKind::KernelAddress) &&
        (name == "address" || name == "kernel-address")) {
      intrinsicName = "allow.sanitize.address";
    } else if (getLangOpts().Sanitize.has(SanitizerKind::Thread) &&
               name == "thread") {
      intrinsicName = "allow.sanitize.thread";
    } else if (getLangOpts().Sanitize.hasOneOf(SanitizerKind::Memory |
                                               SanitizerKind::KernelMemory) &&
               (name == "memory" || name == "kernel-memory")) {
      intrinsicName = "allow.sanitize.memory";
    } else if (getLangOpts().Sanitize.hasOneOf(
                   SanitizerKind::HWAddress | SanitizerKind::KernelHWAddress) &&
               (name == "hwaddress" || name == "kernel-hwaddress")) {
      intrinsicName = "allow.sanitize.hwaddress";
    }

    if (!intrinsicName.empty()) {
      mlir::Value allow = cir::LLVMIntrinsicCallOp::create(
          builder, loc, cgm.getStringAttrCached(intrinsicName),
          builder.getBoolTy(), mlir::ValueRange{})
                              .getResult();
      return RValue::get(allow);
    }

    // If the queried sanitizer is not enabled, fold to false in the frontend,
    // matching OGCG.
    return RValue::get(builder.getBool(false, loc));
  }
  case Builtin::BI__builtin_counted_by_ref: {
    // Default to returning '(void *) 0'.
    auto ptrTy = mlir::cast<cir::PointerType>(convertType(e->getType()));
    mlir::Value result = builder.getNullPtr(ptrTy, loc);

    const Expr *arg = e->getArg(0)->IgnoreParenImpCasts();

    // Look through &arr[idx] to get the base MemberExpr.
    if (auto *UO = dyn_cast<UnaryOperator>(arg);
        UO && UO->getOpcode() == UO_AddrOf) {
      arg = UO->getSubExpr()->IgnoreParenImpCasts();
      if (auto *ASE = dyn_cast<ArraySubscriptExpr>(arg))
        arg = ASE->getBase()->IgnoreParenImpCasts();
    }

    if (const auto *ME = dyn_cast_if_present<MemberExpr>(arg)) {
      if (auto *CATy =
              ME->getMemberDecl()->getType()->getAs<CountAttributedType>();
          CATy && CATy->getKind() == CountAttributedType::CountedBy) {
        const auto *memberDecl = cast<FieldDecl>(ME->getMemberDecl());
        if (const FieldDecl *countFD = memberDecl->findCountedByField()) {
          mlir::Value addr = getAddrOfCountedByField(arg, countFD);
          if (addr)
            result = addr;
        } else {
          llvm::report_fatal_error("Cannot find the counted_by 'count' field");
        }
      }
    }

    return RValue::get(result);
  }
  case Builtin::BI__builtin___clear_cache: {
    // Emit call to llvm.clear_cache intrinsic with begin and end pointers
    mlir::Value begin = emitScalarExpr(e->getArg(0));
    mlir::Value end = emitScalarExpr(e->getArg(1));
    cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("clear_cache"), mlir::Type{},
        mlir::ValueRange{begin, end});
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_trap: {
    // Emit as a non-terminator intrinsic call matching OGCG's EmitTrapCall.
    // Unlike emitTrap() which uses TrapOp (a terminator that adds
    // unreachable), this emits a plain call to @llvm.trap() so subsequent
    // code in the same block remains reachable.
    auto trapCall = cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("trap"), mlir::Type{},
        mlir::ValueRange{});
    if (inNoMergeAttributedStmt)
      trapCall->setAttr("cir.nomerge",
                        mlir::UnitAttr::get(builder.getContext()));
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_verbose_trap: {
    // OGCG attaches a debug annotation from the string arguments via
    // CreateTrapFailureMessageFor (CGBuiltin.cpp:3776-3786): an artificial
    // `__clang_trap_msg$<category>$<message>` DISubprogram plus an inlined-at
    // trap location so the debugger/crash-reporter shows the reason. Record the
    // category/message on the trap location; the CIR->LLVM lowering synthesizes
    // the DISubprogram + inlined-at frame from it. Without debug info this is a
    // plain trap, matching classic codegen.
    auto trapCall = cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("trap"), mlir::Type{},
        mlir::ValueRange{});
    if (inNoMergeAttributedStmt)
      trapCall->setAttr("cir.nomerge",
                        mlir::UnitAttr::get(builder.getContext()));
    if (cgm.getCodeGenOpts().getDebugInfo() !=
        llvm::codegenoptions::NoDebugInfo) {
      std::optional<std::string> category =
          e->getArg(0)->tryEvaluateString(getContext());
      std::optional<std::string> message =
          e->getArg(1)->tryEvaluateString(getContext());
      if (category && message)
        trapCall->setLoc(
            attachTrapMessage(trapCall->getLoc(), *category, *message));
    }
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_debugtrap:
  case Builtin::BI__debugbreak: {
    // Emit llvm.debugtrap intrinsic
    auto debugTrapCall = cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("debugtrap"), mlir::Type{},
        mlir::ValueRange{});
    if (inNoMergeAttributedStmt)
      debugTrapCall->setAttr("cir.nomerge",
                             mlir::UnitAttr::get(builder.getContext()));
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_unreachable:
    emitUnreachable(e->getExprLoc(), /*createNewBlock=*/true);
    return RValue::getIgnored();
  case Builtin::BI__builtin_powi:
  case Builtin::BI__builtin_powif:
  case Builtin::BI__builtin_powil: {
    mlir::Value val = emitScalarExpr(e->getArg(0));
    mlir::Value power = emitScalarExpr(e->getArg(1));
    auto call = cir::PowiOp::create(builder, getLoc(e->getExprLoc()),
                                    val.getType(), val, power);

    clang::FPOptions fpFeatures = e->getFPFeaturesInEffect(getLangOpts());
    if (fpFeatures.isFPConstrained() || IsFPConstrained) {
      auto *ctx = &getMLIRContext();
      if (auto fpExcept = getFPExceptionBehavior(ctx, fpFeatures))
        call->setAttr("cir.fp_exception", fpExcept);
      if (auto roundMode = getRoundingMode(ctx, fpFeatures))
        call->setAttr("cir.rounding_mode", roundMode);
    }

    return RValue::get(call->getResult(0));
  }
  case Builtin::BI__builtin_frexpl:
    // OGCG has PPC double-double long double library call fallback here.
    // PPC is not a supported CIR target.
    [[fallthrough]];
  case Builtin::BI__builtin_frexp:
  case Builtin::BI__builtin_frexpf:
  case Builtin::BI__builtin_frexpf128:
  case Builtin::BI__builtin_frexpf16: {
    // frexp(val, &exp) -> significand. Stores exponent through pointer.
    // CIR: call @__builtin_frexp(val, ptr) -> fp
    // Lowering decomposes to: llvm.frexp + extractvalue + store
    mlir::Location callLoc = getLoc(e->getSourceRange());
    mlir::Value val = emitScalarExpr(e->getArg(0));
    mlir::Value expPtr = emitScalarExpr(e->getArg(1));
    mlir::Type fpTy = val.getType();
    mlir::Type ptrTy = expPtr.getType();

    std::string name = getContext().BuiltinInfo.getName(builtinID);
    mlir::Operation *builtin = cgm.getGlobalValue(name);
    cir::FuncOp fnOp;
    if (!builtin) {
      fnOp = cgm.createCIRBuiltinFunction(
          callLoc, name,
          cir::FuncType::get({fpTy, ptrTy}, fpTy),
          /*FD=*/nullptr);
    } else {
      fnOp = cast<cir::FuncOp>(builtin);
    }
    auto call = builder.createCallOp(callLoc, fnOp,
                                     mlir::ValueRange{val, expPtr});
    maybeSetCallStrictFPAttr(*this, e, call.getOperation());
    return RValue::get(call.getResult());
  }
  case Builtin::BImodf:
  case Builtin::BImodff:
  case Builtin::BImodfl:
  case Builtin::BI__builtin_modf:
  case Builtin::BI__builtin_modff:
  case Builtin::BI__builtin_modfl: {
    FPOptions fpFeatures = e->getFPFeaturesInEffect(getLangOpts());
    bool isFPConstrained = fpFeatures.isFPConstrained() || IsFPConstrained;
    if (isFPConstrained) {
      cir::CIRCallOpInterface call;
      RValue result =
          emitLibraryCall(*this, fd, e,
                          getBuiltinLibcallCallee(cgm, fd, builtinID), &call);
      // Mark this as a library call that must NOT be lowered back into an
      // intrinsic during direct-to-LLVM conversion.
      if (call)
        call->setDiscardableAttr("cir.math_libcall",
                                 mlir::UnitAttr::get(builder.getContext()));
      return result;
    }

    // modf(val, &iptr) -> fractional. Stores integral part through pointer.
    // CIR: call @__builtin_modf(val, ptr) -> fp (or modf/modff/modfl)
    // Lowering decomposes to: llvm.modf + extractvalue + store
    mlir::Location callLoc = getLoc(e->getSourceRange());
    mlir::Value val = emitScalarExpr(e->getArg(0));
    mlir::Value intPartPtr = emitScalarExpr(e->getArg(1));
    mlir::Type fpTy = val.getType();
    mlir::Type ptrTy = intPartPtr.getType();

    std::string name = getContext().BuiltinInfo.getName(builtinID);
    mlir::Operation *builtin = cgm.getGlobalValue(name);
    cir::FuncOp fnOp;
    if (!builtin) {
      fnOp = cgm.createCIRBuiltinFunction(
          callLoc, name,
          cir::FuncType::get({fpTy, ptrTy}, fpTy),
          /*FD=*/nullptr);
    } else {
      fnOp = cast<cir::FuncOp>(builtin);
    }
    auto call = builder.createCallOp(callLoc, fnOp,
                                     mlir::ValueRange{val, intPartPtr});
    return RValue::get(call.getResult());
  }
  case Builtin::BI__builtin_sincospi:
  case Builtin::BI__builtin_sincospif:
  case Builtin::BI__builtin_sincospil:
  case Builtin::BIsincos:
  case Builtin::BIsincosf:
  case Builtin::BIsincosl:
  case Builtin::BI__builtin_sincos:
  case Builtin::BI__builtin_sincosf:
  case Builtin::BI__builtin_sincosf16:
  case Builtin::BI__builtin_sincosl:
  case Builtin::BI__builtin_sincosf128: {
    FPOptions fpFeatures = e->getFPFeaturesInEffect(getLangOpts());
    bool isFPConstrained = fpFeatures.isFPConstrained() || IsFPConstrained;
    // Match OGCG: use the normal library-call path whenever the builtin
    // cannot legally become an intrinsic. That includes -fmath-errno,
    // math-errno overrides, optnone/O0 policy disables, and strict FP,
    // which has no constrained sincos/sincospi intrinsic today.
    if (!generateFPMathIntrinsics || isFPConstrained) {
      // Map builtin to the corresponding library function name.
      StringRef libFnName;
      switch (builtinID) {
      case Builtin::BIsincosf: case Builtin::BI__builtin_sincosf:
        libFnName = "sincosf"; break;
      case Builtin::BIsincos: case Builtin::BI__builtin_sincos:
        libFnName = "sincos"; break;
      case Builtin::BIsincosl: case Builtin::BI__builtin_sincosl:
        libFnName = "sincosl"; break;
      case Builtin::BI__builtin_sincosf16:
        libFnName = "sincosf16"; break;
      case Builtin::BI__builtin_sincosf128:
        libFnName = "sincosf128"; break;
      case Builtin::BI__builtin_sincospif:
        libFnName = "sincospif"; break;
      case Builtin::BI__builtin_sincospi:
        libFnName = "sincospi"; break;
      case Builtin::BI__builtin_sincospil:
        libFnName = "sincospil"; break;
      default:
        llvm_unreachable("unexpected sincos builtin");
      }
      auto fnTy = cir::FuncType::get(
          {convertType(e->getArg(0)->getType()),
           convertType(e->getArg(1)->getType()),
           convertType(e->getArg(2)->getType())},
          builder.getVoidTy());
      auto fn = cgm.getOrCreateCIRFunction(libFnName, fnTy, GlobalDecl(fd),
                                           /*forVTable=*/false);
      cir::CIRCallOpInterface call;
      RValue result = emitLibraryCall(*this, fd, e, fn, &call);
      // Mark this as a library call that must NOT be lowered back into an
      // intrinsic during direct-to-LLVM conversion.
      if (call)
        call->setDiscardableAttr("cir.math_libcall",
                                 mlir::UnitAttr::get(builder.getContext()));
      return result;
    }
    // sincos(val, &sinDest, &cosDest) -> void. Stores sin and cos through
    // pointers.
    // CIR: call @__builtin_sincos(val, sinPtr, cosPtr) -> void
    // Lowering decomposes to: llvm.sincos + extractvalue + store
    mlir::Location callLoc = getLoc(e->getSourceRange());
    mlir::Value val = emitScalarExpr(e->getArg(0));
    mlir::Value sinPtr = emitScalarExpr(e->getArg(1));
    mlir::Value cosPtr = emitScalarExpr(e->getArg(2));
    mlir::Type fpTy = val.getType();
    mlir::Type ptrTy = sinPtr.getType();

    std::string name = getContext().BuiltinInfo.getName(builtinID);
    mlir::Operation *builtin = cgm.getGlobalValue(name);
    cir::FuncOp fnOp;
    if (!builtin) {
      fnOp = cgm.createCIRBuiltinFunction(
          callLoc, name,
          cir::FuncType::get({fpTy, ptrTy, ptrTy}, builder.getVoidTy()),
          /*FD=*/nullptr);
    } else {
      fnOp = cast<cir::FuncOp>(builtin);
    }
    builder.createCallOp(callLoc, fnOp,
                         mlir::ValueRange{val, sinPtr, cosPtr});
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_isgreater:
  case Builtin::BI__builtin_isgreaterequal:
  case Builtin::BI__builtin_isless:
  case Builtin::BI__builtin_islessequal:
  case Builtin::BI__builtin_islessgreater:
  case Builtin::BI__builtin_isunordered: {
    // In strictfp mode, the comparison can raise FP exceptions even when the
    // result is discarded, so we must still emit it for side effects.
    if (ignoreResult && !IsFPConstrained) {
      emitScalarExpr(e->getArg(0));
      emitScalarExpr(e->getArg(1));
      return RValue::getIgnored();
    }

    // Ordered comparisons: we know the arguments to these are matching scalar
    // floating point values.
    mlir::Value lhs = emitScalarExpr(e->getArg(0));
    mlir::Value rhs = emitScalarExpr(e->getArg(1));
    mlir::Location loc = getLoc(e->getExprLoc());

    mlir::Value cmpResult;
    switch (builtinID) {
    default:
      llvm_unreachable("Unknown ordered comparison");
    case Builtin::BI__builtin_isgreater:
      cmpResult = builder.createCompare(loc, cir::CmpOpKind::gt, lhs, rhs);
      break;
    case Builtin::BI__builtin_isgreaterequal:
      cmpResult = builder.createCompare(loc, cir::CmpOpKind::ge, lhs, rhs);
      break;
    case Builtin::BI__builtin_isless:
      cmpResult = builder.createCompare(loc, cir::CmpOpKind::lt, lhs, rhs);
      break;
    case Builtin::BI__builtin_islessequal:
      cmpResult = builder.createCompare(loc, cir::CmpOpKind::le, lhs, rhs);
      break;
    case Builtin::BI__builtin_islessgreater:
      cmpResult = builder.createCompare(loc, cir::CmpOpKind::fone, lhs, rhs);
      break;
    case Builtin::BI__builtin_isunordered:
      cmpResult = builder.createCompare(loc, cir::CmpOpKind::funo, lhs, rhs);
      break;
    }

    // Attach FP constraint attributes for strictfp mode.
    if (IsFPConstrained) {
      FPOptions fpFeatures = e->getFPFeaturesInEffect(getLangOpts());
      if (auto *defOp = cmpResult.getDefiningOp()) {
        auto *ctx = &getMLIRContext();
        if (auto fpExcept = getFPExceptionBehavior(ctx, fpFeatures))
          defOp->setAttr("cir.fp_exception", fpExcept);
        if (auto roundMode = getRoundingMode(ctx, fpFeatures))
          defOp->setAttr("cir.rounding_mode", roundMode);
      }
    }

    if (ignoreResult)
      return RValue::getIgnored();

    return RValue::get(
        builder.createBoolToInt(cmpResult, convertType(e->getType())));
  }
  // From https://clang.llvm.org/docs/LanguageExtensions.html#builtin-isfpclass
  //
  //  The `__builtin_isfpclass()` builtin is a generalization of functions
  //  isnan, isinf, isfinite and some others defined by the C standard. It tests
  //  if the floating-point value, specified by the first argument, falls into
  //  any of data classes, specified by the second argument.
  case Builtin::BI__builtin_isnan: {
    if (ignoreResult) {
      emitScalarExpr(e->getArg(0));
      return RValue::getIgnored();
    }

    mlir::Value v = emitScalarExpr(e->getArg(0));
    mlir::Location loc = getLoc(e->getBeginLoc());
    auto isFPClass = builder.createIsFPClass(loc, v, cir::FPClassTest::fcNan);
    maybeSetCallStrictFPAttr(*this, e, isFPClass.getOperation());
    return RValue::get(
        builder.createBoolToInt(isFPClass, convertType(e->getType())));
  }

  case Builtin::BI__builtin_issignaling: {
    if (ignoreResult) {
      emitScalarExpr(e->getArg(0));
      return RValue::getIgnored();
    }

    mlir::Value v = emitScalarExpr(e->getArg(0));
    mlir::Location loc = getLoc(e->getBeginLoc());
    auto isFPClass =
        builder.createIsFPClass(loc, v, cir::FPClassTest::fcSNan);
    maybeSetCallStrictFPAttr(*this, e, isFPClass.getOperation());
    return RValue::get(
        builder.createBoolToInt(isFPClass, convertType(e->getType())));
  }

  case Builtin::BI__builtin_isinf: {
    if (ignoreResult) {
      emitScalarExpr(e->getArg(0));
      return RValue::getIgnored();
    }

    mlir::Value v = emitScalarExpr(e->getArg(0));
    mlir::Location loc = getLoc(e->getBeginLoc());
    auto isFPClass =
        builder.createIsFPClass(loc, v, cir::FPClassTest::fcInf);
    maybeSetCallStrictFPAttr(*this, e, isFPClass.getOperation());
    return RValue::get(
        builder.createBoolToInt(isFPClass, convertType(e->getType())));
  }
  case Builtin::BIfinite:
  case Builtin::BI__finite:
  case Builtin::BIfinitef:
  case Builtin::BI__finitef:
  case Builtin::BIfinitel:
  case Builtin::BI__finitel:
  case Builtin::BI__builtin_isfinite: {
    if (ignoreResult) {
      emitScalarExpr(e->getArg(0));
      return RValue::getIgnored();
    }

    mlir::Value v = emitScalarExpr(e->getArg(0));
    mlir::Location loc = getLoc(e->getBeginLoc());
    auto isFPClass =
        builder.createIsFPClass(loc, v, cir::FPClassTest::fcFinite);
    maybeSetCallStrictFPAttr(*this, e, isFPClass.getOperation());
    return RValue::get(
        builder.createBoolToInt(isFPClass, convertType(e->getType())));
  }

  case Builtin::BI__builtin_isnormal: {
    if (ignoreResult) {
      emitScalarExpr(e->getArg(0));
      return RValue::getIgnored();
    }

    mlir::Value v = emitScalarExpr(e->getArg(0));
    mlir::Location loc = getLoc(e->getBeginLoc());
    auto isFPClass =
        builder.createIsFPClass(loc, v, cir::FPClassTest::fcNormal);
    maybeSetCallStrictFPAttr(*this, e, isFPClass.getOperation());
    return RValue::get(
        builder.createBoolToInt(isFPClass, convertType(e->getType())));
  }

  case Builtin::BI__builtin_issubnormal: {
    if (ignoreResult) {
      emitScalarExpr(e->getArg(0));
      return RValue::getIgnored();
    }

    mlir::Value v = emitScalarExpr(e->getArg(0));
    mlir::Location loc = getLoc(e->getBeginLoc());
    auto isFPClass =
        builder.createIsFPClass(loc, v, cir::FPClassTest::fcSubnormal);
    maybeSetCallStrictFPAttr(*this, e, isFPClass.getOperation());
    return RValue::get(
        builder.createBoolToInt(isFPClass, convertType(e->getType())));
  }

  case Builtin::BI__builtin_iszero: {
    if (ignoreResult) {
      emitScalarExpr(e->getArg(0));
      return RValue::getIgnored();
    }

    mlir::Value v = emitScalarExpr(e->getArg(0));
    mlir::Location loc = getLoc(e->getBeginLoc());
    auto isFPClass = builder.createIsFPClass(loc, v, cir::FPClassTest::fcZero);
    maybeSetCallStrictFPAttr(*this, e, isFPClass.getOperation());
    return RValue::get(
        builder.createBoolToInt(isFPClass, convertType(e->getType())));
  }
  case Builtin::BI__builtin_isfpclass: {
    if (ignoreResult) {
      emitScalarExpr(e->getArg(0));
      return RValue::getIgnored();
    }

    Expr::EvalResult result;
    if (!e->getArg(1)->EvaluateAsInt(result, cgm.getASTContext()))
      break;

    mlir::Value v = emitScalarExpr(e->getArg(0));
    uint64_t test = result.Val.getInt().getLimitedValue();
    mlir::Location loc = getLoc(e->getBeginLoc());
    auto isFPClass =
        builder.createIsFPClass(loc, v, static_cast<cir::FPClassTest>(test));
    maybeSetCallStrictFPAttr(*this, e, isFPClass.getOperation());
    mlir::Value isfpResult = isFPClass;
    mlir::Type dstTy = convertType(e->getType());

    // For vector types with signed elements, route through unsigned to get
    // ZExt (producing 0/1) instead of SExt (producing 0/-1), matching OGCG.
    if (auto vecTy = mlir::dyn_cast<cir::VectorType>(dstTy)) {
      auto elemTy = mlir::cast<cir::IntType>(vecTy.getElementType());
      if (elemTy.isSigned()) {
        auto uElemTy = cir::IntType::get(builder.getContext(),
                                         elemTy.getWidth(), /*isSigned=*/false);
        auto uVecTy = cir::VectorType::get(uElemTy, vecTy.getSize());
        mlir::Value zext = builder.createBoolToInt(isfpResult, uVecTy);
        return RValue::get(builder.createIntCast(zext, dstTy));
      }
    }

    return RValue::get(builder.createBoolToInt(isfpResult, dstTy));
  }
  case Builtin::BI__builtin_fpclassify: {
    // __builtin_fpclassify(FP_NAN, FP_INFINITY, FP_NORMAL, FP_SUBNORMAL,
    //                      FP_ZERO, value)
    // Returns one of the first 5 integer arguments based on the FP class of
    // the 6th argument.
    mlir::Location loc = getLoc(e->getBeginLoc());

    // In ignored-result contexts, we only need to evaluate the classification
    // operand for side effects.
    if (ignoreResult) {
      // Fast path: when classification result is discarded and the class-value
      // arguments are side-effect free, only evaluate the FP operand.
      if (!e->getArg(0)->HasSideEffects(getContext()) &&
          !e->getArg(1)->HasSideEffects(getContext()) &&
          !e->getArg(2)->HasSideEffects(getContext()) &&
          !e->getArg(3)->HasSideEffects(getContext()) &&
          !e->getArg(4)->HasSideEffects(getContext())) {
        emitScalarExpr(e->getArg(5));
        return RValue::getIgnored();
      }

      // Preserve side effects from class-value arguments in discarded-result
      // contexts.
      emitIgnoredExpr(e->getArg(0));
      emitIgnoredExpr(e->getArg(1));
      emitIgnoredExpr(e->getArg(2));
      emitIgnoredExpr(e->getArg(3));
      emitIgnoredExpr(e->getArg(4));
      emitScalarExpr(e->getArg(5));
      return RValue::getIgnored();
    }

    mlir::Value v = emitScalarExpr(e->getArg(5));
    bool classArgsHaveSideEffects =
        e->getArg(0)->HasSideEffects(getContext()) ||
        e->getArg(1)->HasSideEffects(getContext()) ||
        e->getArg(2)->HasSideEffects(getContext()) ||
        e->getArg(3)->HasSideEffects(getContext()) ||
        e->getArg(4)->HasSideEffects(getContext());

    auto emitLazyClassifyResult = [&](mlir::Value isZero, mlir::Value isNan,
                                      mlir::Value isInf,
                                      mlir::Value isNormal) -> mlir::Value {
      return cir::TernaryOp::create(
                 builder, loc, isZero,
                 [&](mlir::OpBuilder &b, mlir::Location ternLoc) {
                   cir::YieldOp::create(b, ternLoc,
                                        emitScalarExpr(e->getArg(4)));
                 },
                 [&](mlir::OpBuilder &b, mlir::Location ternLoc) {
                   auto notZeroResult = cir::TernaryOp::create(
                       builder, ternLoc, isNan,
                       [&](mlir::OpBuilder &b, mlir::Location ternLoc) {
                         cir::YieldOp::create(b, ternLoc,
                                              emitScalarExpr(e->getArg(0)));
                       },
                       [&](mlir::OpBuilder &b, mlir::Location ternLoc) {
                         auto notNanResult = cir::TernaryOp::create(
                             builder, ternLoc, isInf,
                             [&](mlir::OpBuilder &b, mlir::Location ternLoc) {
                               cir::YieldOp::create(
                                   b, ternLoc, emitScalarExpr(e->getArg(1)));
                             },
                             [&](mlir::OpBuilder &b, mlir::Location ternLoc) {
                               auto normalOrSubnormal =
                                   cir::TernaryOp::create(
                                       builder, ternLoc, isNormal,
                                       [&](mlir::OpBuilder &b,
                                           mlir::Location ternLoc) {
                                         cir::YieldOp::create(
                                             b, ternLoc,
                                             emitScalarExpr(e->getArg(2)));
                                       },
                                       [&](mlir::OpBuilder &b,
                                           mlir::Location ternLoc) {
                                         cir::YieldOp::create(
                                             b, ternLoc,
                                             emitScalarExpr(e->getArg(3)));
                                       })
                                       .getResult();
                               cir::YieldOp::create(b, ternLoc,
                                                    normalOrSubnormal);
                             })
                             .getResult();
                         cir::YieldOp::create(b, ternLoc, notNanResult);
                       })
                       .getResult();
                   cir::YieldOp::create(b, ternLoc, notZeroResult);
                 })
                 .getResult();
    };

    if (IsFPConstrained) {
      // Under strict FP, use FCmp comparisons that can raise FP exceptions,
      // matching OGCG's CreateFCmpOEQ/UNO/UGE under CGFPOptionsRAII.
      auto fpTy = v.getType();
      auto *ctx = &getMLIRContext();

      // Helper to create an FCmp with FP constraint attributes.
      auto createConstrainedCmp = [&](cir::CmpOpKind kind, mlir::Value lhs,
                                      mlir::Value rhs) -> mlir::Value {
        auto cmp = cir::CmpOp::create(builder, loc, kind, lhs, rhs);
        auto fpExcept = cir::FPExceptionBehaviorAttr::get(
            ctx, cir::FPExceptionBehavior::Strict);
        cmp->setAttr("cir.fp_exception", fpExcept);
        return cmp;
      };

      // Zero constant and infinity constant
      mlir::Value zero = builder.getConstFP(
          loc, fpTy, llvm::APFloat::getZero(
                         getContext().getFloatTypeSemantics(
                             e->getArg(5)->getType())));
      mlir::Value inf = builder.getConstFP(
          loc, fpTy, llvm::APFloat::getInf(
                         getContext().getFloatTypeSemantics(
                             e->getArg(5)->getType())));
      llvm::APFloat smallest = llvm::APFloat::getSmallestNormalized(
          getContext().getFloatTypeSemantics(e->getArg(5)->getType()));
      mlir::Value minNormal = builder.getConstFP(loc, fpTy, smallest);
      cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(
          e->getFPFeaturesInEffect(getLangOpts()));

      // Keep the strict FP control flow aligned with OGCG so the synthesized
      // fabs and later compares only execute on the non-zero, non-NaN path.
      mlir::Value result = cir::TernaryOp::create(
                               builder, loc,
                               createConstrainedCmp(cir::CmpOpKind::eq, v, zero),
                               [&](mlir::OpBuilder &b, mlir::Location ternLoc) {
                                 cir::YieldOp::create(
                                     b, ternLoc, emitScalarExpr(e->getArg(4)));
                               },
                               [&](mlir::OpBuilder &b, mlir::Location ternLoc) {
                                 mlir::Value isNan = createConstrainedCmp(
                                     cir::CmpOpKind::funo, v, v);
                                 auto notZeroResult = cir::TernaryOp::create(
                                     builder, ternLoc, isNan,
                                     [&](mlir::OpBuilder &b,
                                         mlir::Location ternLoc) {
                                       cir::YieldOp::create(
                                           b, ternLoc,
                                           emitScalarExpr(e->getArg(0)));
                                     },
                                     [&](mlir::OpBuilder &b,
                                         mlir::Location ternLoc) {
                                       auto vAbsOp = cir::FAbsOp::create(
                                           builder, ternLoc, fpTy, v, fmf);
                                       maybeSetCallStrictFPAttr(
                                           *this, e, vAbsOp.getOperation());
                                       vAbsOp->setAttr(
                                           "cir.call_memory_none",
                                           mlir::UnitAttr::get(
                                               &getMLIRContext()));
                                       mlir::Value vAbs = vAbsOp.getResult();
                                       mlir::Value isInf =
                                           createConstrainedCmp(
                                               cir::CmpOpKind::eq, vAbs, inf);
                                       auto notNanResult =
                                           cir::TernaryOp::create(
                                               builder, ternLoc, isInf,
                                               [&](mlir::OpBuilder &b,
                                                   mlir::Location ternLoc) {
                                                 cir::YieldOp::create(
                                                     b, ternLoc,
                                                     emitScalarExpr(
                                                         e->getArg(1)));
                                               },
                                               [&](mlir::OpBuilder &b,
                                                   mlir::Location ternLoc) {
                                                 mlir::Value isNormal =
                                                     createConstrainedCmp(
                                                         cir::CmpOpKind::fuge,
                                                         vAbs, minNormal);
                                                 auto normalOrSubnormal =
                                                     cir::TernaryOp::create(
                                                         builder, ternLoc,
                                                         isNormal,
                                                         [&](mlir::OpBuilder &b,
                                                             mlir::Location
                                                                 ternLoc) {
                                                           cir::YieldOp::create(
                                                               b, ternLoc,
                                                               emitScalarExpr(
                                                                   e->getArg(
                                                                       2)));
                                                         },
                                                         [&](mlir::OpBuilder &b,
                                                             mlir::Location
                                                                 ternLoc) {
                                                           cir::YieldOp::create(
                                                               b, ternLoc,
                                                               emitScalarExpr(
                                                                   e->getArg(
                                                                       3)));
                                                         })
                                                         .getResult();
                                                 cir::YieldOp::create(
                                                     b, ternLoc,
                                                     normalOrSubnormal);
                                               })
                                               .getResult();
                                       cir::YieldOp::create(b, ternLoc,
                                                            notNanResult);
                                     })
                                     .getResult();
                                 cir::YieldOp::create(b, ternLoc,
                                                      notZeroResult);
                               })
                               .getResult();
      return RValue::get(result);
    }

    mlir::Value isNan =
        builder.createIsFPClass(loc, v, cir::FPClassTest::fcNan);
    mlir::Value isInf =
        builder.createIsFPClass(loc, v, cir::FPClassTest::fcInf);
    mlir::Value isNormal =
        builder.createIsFPClass(loc, v, cir::FPClassTest::fcNormal);
    mlir::Value isZero =
        builder.createIsFPClass(loc, v, cir::FPClassTest::fcZero);

    if (classArgsHaveSideEffects)
      return RValue::get(
          emitLazyClassifyResult(isZero, isNan, isInf, isNormal));

    mlir::Value nanVal = emitScalarExpr(e->getArg(0));
    mlir::Value infVal = emitScalarExpr(e->getArg(1));
    mlir::Value normalVal = emitScalarExpr(e->getArg(2));
    mlir::Value subnormalVal = emitScalarExpr(e->getArg(3));
    mlir::Value zeroVal = emitScalarExpr(e->getArg(4));

    // Build result with select chain. Subnormal is the default (else) case.
    mlir::Value result =
        builder.createSelect(loc, isZero, zeroVal, subnormalVal);
    result = builder.createSelect(loc, isNormal, normalVal, result);
    result = builder.createSelect(loc, isInf, infVal, result);
    result = builder.createSelect(loc, isNan, nanVal, result);

    return RValue::get(result);
  }
  case Builtin::BI__builtin_isinf_sign: {
    if (ignoreResult) {
      emitScalarExpr(e->getArg(0));
      return RValue::getIgnored();
    }

    // __builtin_isinf_sign(x) returns:
    //   -1 if x is negative infinity
    //    0 if x is not infinity
    //    1 if x is positive infinity
    mlir::Location loc = getLoc(e->getBeginLoc());
    mlir::Value arg = emitScalarExpr(e->getArg(0));

    // Get absolute value for infinity check
    cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(
        e->getFPFeaturesInEffect(getLangOpts()));
    auto absArgOp =
        cir::FAbsOp::create(builder, loc, arg.getType(), arg, fmf);
    maybeSetCallStrictFPAttr(*this, e, absArgOp.getOperation());
    absArgOp->setAttr("cir.call_memory_none",
                      mlir::UnitAttr::get(&getMLIRContext()));
    mlir::Value absArg = absArgOp->getResult(0);

    // Check if absolute value is infinity
    mlir::Value isInf;
    if (IsFPConstrained) {
      // Under strict FP, use constrained FCmp matching OGCG.
      mlir::Value inf = builder.getConstFP(
          loc, arg.getType(),
          llvm::APFloat::getInf(getContext().getFloatTypeSemantics(
              e->getArg(0)->getType())));
      isInf = cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq, absArg,
                                  inf);
      auto fpExcept = cir::FPExceptionBehaviorAttr::get(
          &getMLIRContext(), cir::FPExceptionBehavior::Strict);
      isInf.getDefiningOp()->setAttr("cir.fp_exception", fpExcept);
    } else {
      isInf =
          builder.createIsFPClass(loc, absArg, cir::FPClassTest::fcInf);
    }

    // Check sign bit of the original value
    mlir::Value isNeg = emitSignBit(loc, *this, arg);

    // Build the result: isInf ? (isNeg ? -1 : 1) : 0
    mlir::Type intTy = convertType(e->getType());
    mlir::Value zero = builder.getNullValue(intTy, loc);
    mlir::Value one = builder.getConstInt(loc, intTy, 1);
    mlir::Value negOne = builder.getConstInt(loc, intTy, -1);
    mlir::Value signResult = builder.createSelect(loc, isNeg, negOne, one);
    mlir::Value result = builder.createSelect(loc, isInf, signResult, zero);
    return RValue::get(result);
  }
  case Builtin::BI__builtin_nondeterministic_value: {
    // Nondeterministic value is semantically freeze(poison) in LLVM.
    // The argument is unused — it only informs the result type.
    mlir::Type ty = convertType(e->getType());
    mlir::Location loc = getLoc(e->getSourceRange());
    auto poison = cir::ConstantOp::create(builder, loc,
                                          cir::PoisonAttr::get(ty));
    auto frozen = cir::FreezeOp::create(builder, loc, ty, poison);
    return RValue::get(frozen);
  }
  case Builtin::BI__builtin_elementwise_abs: {
    mlir::Type cirTy = convertType(e->getArg(0)->getType());
    bool isIntTy = cir::isIntOrVectorOfIntType(cirTy);
    if (!isIntTy) {
      return emitUnaryFPBuiltin<cir::FAbsOp>(*this, *e);
    }
    mlir::Value arg = emitScalarExpr(e->getArg(0));
    auto call = cir::AbsOp::create(builder, getLoc(e->getExprLoc()),
                                   arg.getType(), arg, false);
    mlir::Value result = call->getResult(0);
    return RValue::get(result);
  }
  case Builtin::BI__builtin_elementwise_acos:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::ACosOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_asin:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::ASinOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_atan:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::ATanOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_ceil:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::CeilOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_exp:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::ExpOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_exp2:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::Exp2Op>(*this, *e);
  case Builtin::BI__builtin_elementwise_floor:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::FloorOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_log:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::LogOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_log2:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::Log2Op>(*this, *e);
  case Builtin::BI__builtin_elementwise_log10:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::Log10Op>(*this, *e);
  case Builtin::BI__builtin_elementwise_nearbyint:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::NearbyintOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_rint:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::RintOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_round:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::RoundOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_roundeven:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::RoundEvenOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_sin:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::SinOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_sqrt:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::SqrtOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_tan:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::TanOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_trunc:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::TruncOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_copysign:
    return emitBinaryFPBuiltin<cir::CopysignOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_pow:
    return emitBinaryFPBuiltin<cir::PowOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_maxnum:
    return emitPlainBinaryFPBuiltin<cir::FMaxNumOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_minnum:
    return emitPlainBinaryFPBuiltin<cir::FMinNumOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_maximum:
    return emitBinaryFPBuiltin<cir::FMaximumOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_minimum:
    return emitBinaryFPBuiltin<cir::FMinimumOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_atan2:
    return emitBinaryFPBuiltin<cir::ATan2Op>(*this, *e);
  case Builtin::BI__builtin_elementwise_exp10: {
    FPOptions fpFeatures = e->getFPFeaturesInEffect(getLangOpts());
    if (fpFeatures.isFPConstrained() || IsFPConstrained)
      return errorBuiltinNYI(*this, e, builtinID);
    return emitUnaryFPBuiltin<cir::Exp10Op>(*this, *e);
  }
  case Builtin::BI__builtin_elementwise_bitreverse: {
    mlir::Value arg = emitScalarExpr(e->getArg(0));
    // CIR's BitReverseOp requires unsigned integers.  A scalar `_Bool` operand
    // is kept as `cir::BoolType`, so promote it to a 1-bit unsigned integer and
    // cast the result back — matching classic clang's `llvm.bitreverse.i1`.
    if (mlir::isa<cir::BoolType>(arg.getType())) {
      auto i1Ty = cir::IntType::get(builder.getContext(), /*width=*/1,
                                    /*isSigned=*/false);
      mlir::Value promoted = builder.createBoolToInt(arg, i1Ty);
      mlir::Value result = cir::BitReverseOp::create(builder, loc, promoted);
      mlir::Type exprTy = convertType(e->getType());
      if (result.getType() != exprTy)
        result = builder.createCast(loc, cir::CastKind::int_to_bool, result,
                                    exprTy);
      return RValue::get(result);
    }
    // CIR's BitReverseOp requires unsigned integers.  When the input is
    // signed, cast to unsigned, apply the op, and cast back.
    mlir::Type origTy = arg.getType();
    bool isSigned = false;
    mlir::Type unsignedTy;
    if (auto intTy = mlir::dyn_cast<cir::IntType>(origTy)) {
      isSigned = intTy.isSigned();
      if (isSigned)
        unsignedTy = cir::IntType::get(builder.getContext(), intTy.getWidth(),
                                       /*isSigned=*/false);
    } else if (auto vecTy = mlir::dyn_cast<cir::VectorType>(origTy)) {
      if (auto intEleTy =
              mlir::dyn_cast<cir::IntType>(vecTy.getElementType())) {
        isSigned = intEleTy.isSigned();
        if (isSigned)
          unsignedTy = cir::VectorType::get(
              cir::IntType::get(builder.getContext(), intEleTy.getWidth(),
                                /*isSigned=*/false),
              vecTy.getSize());
      }
    }
    if (isSigned) {
      arg = builder.createBitcast(arg, unsignedTy);
      mlir::Value result = cir::BitReverseOp::create(builder, loc, arg);
      return RValue::get(builder.createBitcast(result, origTy));
    }
    return RValue::get(cir::BitReverseOp::create(builder, loc, arg));
  }
  case Builtin::BI__builtin_elementwise_popcount: {
    mlir::Value arg = emitScalarExpr(e->getArg(0));
    // CIR's BitPopcountOp requires unsigned integers.  A scalar `_Bool` operand
    // is kept as `cir::BoolType`, so promote it to a 1-bit unsigned integer and
    // cast the result to the expression type — matching classic clang's
    // `llvm.ctpop.i1`.
    if (mlir::isa<cir::BoolType>(arg.getType())) {
      auto i1Ty = cir::IntType::get(builder.getContext(), /*width=*/1,
                                    /*isSigned=*/false);
      mlir::Value promoted = builder.createBoolToInt(arg, i1Ty);
      mlir::Value result =
          cir::BitPopcountOp::create(builder, loc, i1Ty, promoted);
      mlir::Type exprTy = convertType(e->getType());
      if (result.getType() != exprTy) {
        if (mlir::isa<cir::BoolType>(exprTy))
          result = builder.createCast(loc, cir::CastKind::int_to_bool, result,
                                      exprTy);
        else
          result = builder.createIntCast(result, exprTy);
      }
      return RValue::get(result);
    }
    // CIR's BitPopcountOp requires unsigned integers.  When the input is
    // signed, cast to unsigned, apply the op, and cast back.
    mlir::Type origTy = arg.getType();
    bool isSigned = false;
    mlir::Type unsignedTy;
    if (auto intTy = mlir::dyn_cast<cir::IntType>(origTy)) {
      isSigned = intTy.isSigned();
      if (isSigned)
        unsignedTy = cir::IntType::get(builder.getContext(), intTy.getWidth(),
                                       /*isSigned=*/false);
    } else if (auto vecTy = mlir::dyn_cast<cir::VectorType>(origTy)) {
      if (auto intEleTy =
              mlir::dyn_cast<cir::IntType>(vecTy.getElementType())) {
        isSigned = intEleTy.isSigned();
        if (isSigned)
          unsignedTy = cir::VectorType::get(
              cir::IntType::get(builder.getContext(), intEleTy.getWidth(),
                                /*isSigned=*/false),
              vecTy.getSize());
      }
    }
    if (isSigned) {
      arg = builder.createBitcast(arg, unsignedTy);
      mlir::Value result =
          cir::BitPopcountOp::create(builder, loc, unsignedTy, arg);
      return RValue::get(builder.createBitcast(result, origTy));
    }
    return RValue::get(
        cir::BitPopcountOp::create(builder, loc, arg.getType(), arg));
  }
  case Builtin::BI__builtin_elementwise_cos:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::CosOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_cosh:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::CoshOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_sinh:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::SinhOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_tanh:
    return emitUnaryMaybeConstrainedFPBuiltin<cir::TanhOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_fma:
    return emitTernaryMaybeConstrainedFPBuiltin<cir::FMAOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_max: {
    mlir::Type cirTy = convertType(e->getArg(0)->getType());
    if (cir::isIntOrVectorOfIntType(cirTy)) {
      QualType ty = e->getArg(0)->getType();
      if (auto *vecTy = ty->getAs<VectorType>())
        ty = vecTy->getElementType();
      if (ty->isSignedIntegerType())
        return emitBinaryIntBuiltin<cir::SMaxOp>(*this, *e);
      return emitBinaryIntBuiltin<cir::UMaxOp>(*this, *e);
    }
    return emitBinaryFPBuiltin<cir::FMaxNumOp>(*this, *e);
  }
  case Builtin::BI__builtin_elementwise_min: {
    mlir::Type cirTy = convertType(e->getArg(0)->getType());
    if (cir::isIntOrVectorOfIntType(cirTy)) {
      QualType ty = e->getArg(0)->getType();
      if (auto *vecTy = ty->getAs<VectorType>())
        ty = vecTy->getElementType();
      if (ty->isSignedIntegerType())
        return emitBinaryIntBuiltin<cir::SMinOp>(*this, *e);
      return emitBinaryIntBuiltin<cir::UMinOp>(*this, *e);
    }
    return emitBinaryFPBuiltin<cir::FMinNumOp>(*this, *e);
  }
  case Builtin::BI__builtin_elementwise_add_sat: {
    QualType ty = e->getArg(0)->getType();
    if (auto *vecTy = ty->getAs<VectorType>())
      ty = vecTy->getElementType();
    if (ty->isSignedIntegerType())
      return emitBinaryIntBuiltin<cir::SAddSatOp>(*this, *e);
    return emitBinaryIntBuiltin<cir::UAddSatOp>(*this, *e);
  }
  case Builtin::BI__builtin_elementwise_sub_sat: {
    QualType ty = e->getArg(0)->getType();
    if (auto *vecTy = ty->getAs<VectorType>())
      ty = vecTy->getElementType();
    if (ty->isSignedIntegerType())
      return emitBinaryIntBuiltin<cir::SSubSatOp>(*this, *e);
    return emitBinaryIntBuiltin<cir::USubSatOp>(*this, *e);
  }
  case Builtin::BI__builtin_elementwise_canonicalize:
    return emitUnaryFPBuiltin<cir::CanonicalizeOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_fshl:
    return emitTernaryIntBuiltin<cir::FshlOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_fshr:
    return emitTernaryIntBuiltin<cir::FshrOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_maximumnum:
    return emitBinaryFPBuiltin<cir::MaximumNumOp>(*this, *e);
  case Builtin::BI__builtin_elementwise_minimumnum:
    return emitBinaryFPBuiltin<cir::MinimumNumOp>(*this, *e);
  case Builtin::BI__builtin_reduce_max: {
    QualType ty = e->getArg(0)->getType();
    auto *vecTy = ty->castAs<VectorType>();
    QualType eltTy = vecTy->getElementType();
    if (eltTy->isSignedIntegerType())
      return emitVecReductionBuiltin<cir::VecReduceSMaxOp>(*this, *e);
    if (eltTy->isUnsignedIntegerType())
      return emitVecReductionBuiltin<cir::VecReduceUMaxOp>(*this, *e);
    assert(eltTy->isFloatingType());
    return emitVecReductionBuiltin<cir::VecReduceFMaxOp>(*this, *e);
  }
  case Builtin::BI__builtin_reduce_min: {
    QualType ty = e->getArg(0)->getType();
    auto *vecTy = ty->castAs<VectorType>();
    QualType eltTy = vecTy->getElementType();
    if (eltTy->isSignedIntegerType())
      return emitVecReductionBuiltin<cir::VecReduceSMinOp>(*this, *e);
    if (eltTy->isUnsignedIntegerType())
      return emitVecReductionBuiltin<cir::VecReduceUMinOp>(*this, *e);
    assert(eltTy->isFloatingType());
    return emitVecReductionBuiltin<cir::VecReduceFMinOp>(*this, *e);
  }
  case Builtin::BI__builtin_reduce_add:
    return emitVecReductionBuiltin<cir::VecReduceAddOp>(*this, *e);
  case Builtin::BI__builtin_reduce_mul:
    return emitVecReductionBuiltin<cir::VecReduceMulOp>(*this, *e);
  case Builtin::BI__builtin_reduce_xor:
    return emitVecReductionBuiltin<cir::VecReduceXorOp>(*this, *e);
  case Builtin::BI__builtin_reduce_or:
    return emitVecReductionBuiltin<cir::VecReduceOrOp>(*this, *e);
  case Builtin::BI__builtin_reduce_and:
    return emitVecReductionBuiltin<cir::VecReduceAndOp>(*this, *e);
  case Builtin::BI__builtin_reduce_maximum:
    return emitVecReductionBuiltin<cir::VecReduceFMaximumOp>(*this, *e);
  case Builtin::BI__builtin_reduce_minimum:
    return emitVecReductionBuiltin<cir::VecReduceFMinimumOp>(*this, *e);
  case Builtin::BI__builtin_matrix_transpose: {
    mlir::Location loc = getLoc(e->getExprLoc());
    auto *matrixTy = e->getArg(0)->getType()->castAs<ConstantMatrixType>();
    mlir::Type resultTy = convertType(e->getType());
    mlir::Value matrix = emitScalarExpr(e->getArg(0));
    return RValue::get(cir::LLVMIntrinsicCallOp::create(
                           builder, loc,
                           cgm.getStringAttrCached("matrix.transpose"),
                           resultTy,
                           mlir::ValueRange{
                               matrix,
                               builder.getUInt32(matrixTy->getNumRows(), loc),
                               builder.getUInt32(matrixTy->getNumColumns(),
                                                 loc)})
                           .getResult());
  }
  case Builtin::BI__builtin_matrix_column_major_load: {
    mlir::Location loc = getLoc(e->getExprLoc());
    mlir::Value stride = emitScalarExpr(e->getArg(3));
    const auto *resultMatrixTy = e->getType()->getAs<ConstantMatrixType>();
    auto *ptrTy = e->getArg(0)->getType()->getAs<PointerType>();
    assert(ptrTy && "arg0 must be of pointer type");
    bool isVolatile = ptrTy->getPointeeType().isVolatileQualified();

    Address src = emitPointerWithAlignment(e->getArg(0));
    emitNonNullArgCheck(RValue::get(src.getPointer()), e->getArg(0)->getType(),
                        e->getArg(0)->getExprLoc(), fd, 0);
    return RValue::get(cir::LLVMIntrinsicCallOp::create(
                           builder, loc,
                           cgm.getStringAttrCached("matrix.column.major.load"),
                           convertType(e->getType()),
                           mlir::ValueRange{
                               src.getPointer(),
                               builder.getUInt32(
                                   src.getAlignment().getQuantity(), loc),
                               stride,
                               builder.getBool(isVolatile, loc),
                               builder.getUInt32(resultMatrixTy->getNumRows(),
                                                 loc),
                               builder.getUInt32(
                                   resultMatrixTy->getNumColumns(), loc)})
                           .getResult());
  }
  case Builtin::BI__builtin_matrix_column_major_store: {
    mlir::Location loc = getLoc(e->getExprLoc());
    mlir::Value matrix = emitScalarExpr(e->getArg(0));
    Address dst = emitPointerWithAlignment(e->getArg(1));
    mlir::Value stride = emitScalarExpr(e->getArg(2));

    const auto *matrixTy =
        e->getArg(0)->getType()->getAs<ConstantMatrixType>();
    auto *ptrTy = e->getArg(1)->getType()->getAs<PointerType>();
    assert(ptrTy && "arg1 must be of pointer type");
    bool isVolatile = ptrTy->getPointeeType().isVolatileQualified();

    emitNonNullArgCheck(RValue::get(dst.getPointer()), e->getArg(1)->getType(),
                        e->getArg(1)->getExprLoc(), fd, 0);
    cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("matrix.column.major.store"),
        builder.getVoidTy(),
        mlir::ValueRange{
            matrix,
            dst.getPointer(),
            builder.getUInt32(dst.getAlignment().getQuantity(), loc),
            stride,
            builder.getBool(isVolatile, loc),
            builder.getUInt32(matrixTy->getNumRows(), loc),
            builder.getUInt32(matrixTy->getNumColumns(), loc)});
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_masked_load:
  case Builtin::BI__builtin_masked_expand_load: {
    mlir::Value mask = emitScalarExpr(e->getArg(0));
    mlir::Value ptr = emitScalarExpr(e->getArg(1));
    mlir::Type retTy = convertType(e->getType());

    mlir::Value passThru;
    if (e->getNumArgs() > 2)
      passThru = emitScalarExpr(e->getArg(2));
    else
      passThru =
          builder.getConstant(getLoc(e->getSourceRange()),
                              cir::PoisonAttr::get(retTy));

    auto loc = getLoc(e->getSourceRange());
    CharUnits align = cgm.getNaturalTypeAlignment(
        e->getType()->getAs<VectorType>()->getElementType(), nullptr);

    if (builtinID == Builtin::BI__builtin_masked_load) {
      return RValue::get(builder.createMaskedLoad(loc, retTy, ptr,
                                                  align.getAsAlign(), mask,
                                                  passThru));
    }
    // masked_expand_load: @llvm.masked.expandload(ptr, mask, passthru)
    mlir::Value ops[] = {ptr, mask, passThru};
    return RValue::get(
        cir::LLVMIntrinsicCallOp::create(
            builder, loc, cgm.getStringAttrCached("masked.expandload"), retTy,
            ops)
            .getResult());
  }
  case Builtin::BI__builtin_masked_gather: {
    mlir::Value mask = emitScalarExpr(e->getArg(0));
    mlir::Value idx = emitScalarExpr(e->getArg(1));
    mlir::Value ptr = emitScalarExpr(e->getArg(2));
    mlir::Type retTy = convertType(e->getType());

    CharUnits align = cgm.getNaturalTypeAlignment(
        e->getType()->getAs<VectorType>()->getElementType(), nullptr);

    mlir::Value passThru;
    if (e->getNumArgs() > 3)
      passThru = emitScalarExpr(e->getArg(3));
    else
      passThru =
          builder.getConstant(getLoc(e->getSourceRange()),
                              cir::PoisonAttr::get(retTy));

    auto loc = getLoc(e->getSourceRange());
    auto alignVal = cir::ConstantOp::create(
        builder, loc,
        cir::IntAttr::get(builder.getUInt32Ty(), align.getQuantity()));

    // Encode base ptr + vector index + alignment + mask + passthru.
    // LowerToLLVM decomposes into GEP + @llvm.masked.gather.
    mlir::Value ops[] = {ptr, idx, alignVal, mask, passThru};
    return RValue::get(
        cir::LLVMIntrinsicCallOp::create(
            builder, loc,
            cgm.getStringAttrCached("masked.gather.with.gep"), retTy, ops)
            .getResult());
  }
  case Builtin::BI__builtin_masked_store:
  case Builtin::BI__builtin_masked_compress_store: {
    mlir::Value mask = emitScalarExpr(e->getArg(0));
    mlir::Value val = emitScalarExpr(e->getArg(1));
    mlir::Value ptr = emitScalarExpr(e->getArg(2));

    CharUnits align = cgm.getNaturalTypeAlignment(
        e->getArg(1)->getType()->getAs<VectorType>()->getElementType(),
        nullptr);

    auto loc = getLoc(e->getSourceRange());

    if (builtinID == Builtin::BI__builtin_masked_store) {
      builder.createMaskedStore(loc, val, ptr, align.getAsAlign(), mask);
    } else {
      // masked_compress_store: @llvm.masked.compressstore(val, ptr, mask)
      mlir::Value ops[] = {val, ptr, mask};
      cir::LLVMIntrinsicCallOp::create(
          builder, loc, cgm.getStringAttrCached("masked.compressstore"),
          mlir::Type{}, ops);
    }
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_masked_scatter: {
    mlir::Value mask = emitScalarExpr(e->getArg(0));
    mlir::Value idx = emitScalarExpr(e->getArg(1));
    mlir::Value val = emitScalarExpr(e->getArg(2));
    mlir::Value ptr = emitScalarExpr(e->getArg(3));

    CharUnits align = cgm.getNaturalTypeAlignment(
        e->getArg(2)->getType()->getAs<VectorType>()->getElementType(),
        nullptr);

    auto loc = getLoc(e->getSourceRange());
    auto alignVal = cir::ConstantOp::create(
        builder, loc,
        cir::IntAttr::get(builder.getUInt32Ty(), align.getQuantity()));

    // Encode val + base ptr + vector index + alignment + mask.
    // LowerToLLVM decomposes into GEP + @llvm.masked.scatter.
    mlir::Value ops[] = {val, ptr, idx, alignVal, mask};
    cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("masked.scatter.with.gep"),
        mlir::Type{}, ops);
    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_set_flt_rounds: {
    mlir::Value arg = emitScalarExpr(e->getArg(0));
    cir::LLVMIntrinsicCallOp::create(
        builder, getLoc(e->getSourceRange()),
        cgm.getStringAttrCached("set.rounding"),
        mlir::Type{}, mlir::ValueRange{arg});
    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_flt_rounds: {
    // Maps to llvm.get.rounding intrinsic. Returns the current rounding mode.
    mlir::Type resultTy = convertType(e->getType());
    auto call = cir::LLVMIntrinsicCallOp::create(
        builder, getLoc(e->getSourceRange()),
        cgm.getStringAttrCached("get.rounding"), resultTy, mlir::ValueRange{});
    return RValue::get(call.getResult());
  }
  case Builtin::BIalloca:
  case Builtin::BI_alloca:
  case Builtin::BI__builtin_alloca_uninitialized:
  case Builtin::BI__builtin_alloca:
  case Builtin::BI__builtin_alloca_with_align_uninitialized:
  case Builtin::BI__builtin_alloca_with_align:
    return emitBuiltinAlloca(*this, e, builtinID);
  case Builtin::BI__builtin_infer_alloc_token: {
    constexpr llvm::StringLiteral allocTokenIdThunkName =
        "__cir_alloc_token_id";
    mlir::Type resultTy = convertType(e->getType());
    auto thunk = cgm.createRuntimeFunction(
        cir::FuncType::get({builder.getVoidPtrTy()}, resultTy),
        allocTokenIdThunkName,
        /*attrs=*/{}, /*isLocal=*/false, /*assumeConvergent=*/false);
    mlir::Value placeholderArg =
        builder.getNullPtr(builder.getVoidPtrTy(), loc);
    auto call =
        builder.createCallOp(loc, thunk, mlir::ValueRange{placeholderArg});
    call->setAttr("cir.alloc_token",
                  buildAllocToken(e, /*allowUnknown=*/true));
    return RValue::get(call.getResult());
  }
  case Builtin::BIbzero:
  case Builtin::BI__builtin_bzero: {
    // OGCG calls EmitNonNullArgCheck for the destination pointer.
    assert(!cir::MissingFeatures::memBuiltinSanitizerChecks());
    // bzero(dst, n) is equivalent to memset(dst, 0, n)
    Address dest = emitPointerWithAlignment(e->getArg(0));
    mlir::Value sizeVal = emitScalarExpr(e->getArg(1));
    emitNonNullArgCheck(dest, e->getArg(0)->getType(),
                        e->getArg(0)->getExprLoc(), fd, 0);
    mlir::Value zeroVal = builder.getConstInt(
        getLoc(e->getSourceRange()),
        cir::IntType::get(builder.getContext(), 32, /*isSigned=*/true), 0);
    // MemSetOp requires !cir.ptr<!cir.void> — cast typed pointers.
    // Preserve address space from source pointer.
    {
      auto dstPtrTy =
          mlir::cast<cir::PointerType>(dest.getPointer().getType());
      auto dstVoidPtrTy = cir::PointerType::get(
          builder.getContext(), cir::VoidType::get(builder.getContext()),
          dstPtrTy.getAddrSpace());
      mlir::Value destPtr =
          builder.createBitcast(dest.getPointer(), dstVoidPtrTy);
      auto memsetOp = cir::MemSetOp::create(
          builder, getLoc(e->getSourceRange()), destPtr, zeroVal, sizeVal);
      if (unsigned dstAlign = dest.getAlignment().getQuantity())
        memsetOp->setAttr("cir.dst_align", getI64AttrCached(dstAlign));
    }
    return RValue::get(nullptr);
  }
  case Builtin::BIbcopy:
  case Builtin::BI__builtin_bcopy: {
    // OGCG calls EmitNonNullArgCheck for src and dest pointers.
    assert(!cir::MissingFeatures::memBuiltinSanitizerChecks());
    // bcopy(src, dst, n) is equivalent to memmove(dst, src, n)
    Address src = emitPointerWithAlignment(e->getArg(0));
    Address dest = emitPointerWithAlignment(e->getArg(1));
    mlir::Value sizeVal = emitScalarExpr(e->getArg(2));
    emitNonNullArgCheck(RValue::get(src.getPointer()), e->getArg(0)->getType(),
                        e->getArg(0)->getExprLoc(), fd, 0);
    emitNonNullArgCheck(RValue::get(dest.getPointer()), e->getArg(1)->getType(),
                        e->getArg(1)->getExprLoc(), fd, 0);
    // MemMoveOp requires !cir.ptr<!cir.void> — cast typed pointers.
    // Preserve address space from source pointers.
    auto bcopyDestPtrTy =
        mlir::cast<cir::PointerType>(dest.getPointer().getType());
    auto bcopySrcPtrTy =
        mlir::cast<cir::PointerType>(src.getPointer().getType());
    auto bcopyDestVoidPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        bcopyDestPtrTy.getAddrSpace());
    auto bcopySrcVoidPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        bcopySrcPtrTy.getAddrSpace());
    mlir::Value destPtr =
        builder.createBitcast(dest.getPointer(), bcopyDestVoidPtrTy);
    mlir::Value srcPtr =
        builder.createBitcast(src.getPointer(), bcopySrcVoidPtrTy);
    auto memmoveOp = cir::MemMoveOp::create(builder, getLoc(e->getSourceRange()),
                                            destPtr, srcPtr, sizeVal);
    if (unsigned dstAlign = dest.getAlignment().getQuantity())
      memmoveOp->setAttr("cir.dst_align",
                         getI64AttrCached(dstAlign));
    if (unsigned srcAlign = src.getAlignment().getQuantity())
      memmoveOp->setAttr("cir.src_align",
                         getI64AttrCached(srcAlign));
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_memcpy_inline: {
    Address dest = emitPointerWithAlignment(e->getArg(0));
    Address src = emitPointerWithAlignment(e->getArg(1));
    uint64_t size =
        e->getArg(2)->EvaluateKnownConstInt(getContext()).getZExtValue();
    emitBuiltinArgCheck(*this, TCK_Store, dest, e->getArg(0), fd, 0);
    emitBuiltinArgCheck(*this, TCK_Load, src, e->getArg(1), fd, 1);
    auto memcpyInlineOp =
        cir::MemCpyInlineOp::create(builder, getLoc(e->getExprLoc()),
                                    dest.getPointer(), src.getPointer(), size,
                                    /*isVolatile=*/false);
    // Forward alignment to the lowering pass via discardable attributes.
    // Matches OGCG's Builder.CreateMemCpyInline(Dest, Src, Size) which
    // passes Address objects carrying alignment info (CGBuiltin.cpp:4502).
    if (dest.getAlignment().getQuantity())
      memcpyInlineOp->setAttr(
          "cir.dst_align",
          builder.getI32IntegerAttr(dest.getAlignment().getQuantity()));
    if (src.getAlignment().getQuantity())
      memcpyInlineOp->setAttr(
          "cir.src_align",
          builder.getI32IntegerAttr(src.getAlignment().getQuantity()));
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_memset_inline: {
    Address dest = emitPointerWithAlignment(e->getArg(0));
    mlir::Value val = emitScalarExpr(e->getArg(1));
    uint64_t size =
        e->getArg(2)->EvaluateKnownConstInt(getContext()).getZExtValue();
    emitNonNullArgCheck(RValue::get(dest.getPointer()),
                        e->getArg(0)->getType(), e->getArg(0)->getExprLoc(),
                        CIRGenFunction::AbstractCallee(fd), 0);
    auto memsetInlineOp =
        cir::MemSetInlineOp::create(builder, getLoc(e->getExprLoc()),
                                    dest.getPointer(), val, size,
                                    /*isVolatile=*/false);
    // Forward alignment to the lowering pass via discardable attributes.
    if (dest.getAlignment().getQuantity())
      memsetInlineOp->setAttr(
          "cir.dst_align",
          builder.getI32IntegerAttr(dest.getAlignment().getQuantity()));
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_char_memchr: {
    // __builtin_char_memchr takes char* but maps to the standard C library
    // memchr(const void*, int, size_t) -> void*. Emit the call with explicit
    // pointer bitcasts to avoid creating a conflicting @memchr declaration
    // with char* types, which causes verification failures when both
    // __builtin_char_memchr and __builtin_memchr are used in the same TU.
    mlir::Location loc = getLoc(e->getSourceRange());
    mlir::Value ptr = emitScalarExpr(e->getArg(0));
    mlir::Value val = emitScalarExpr(e->getArg(1));
    mlir::Value size = emitScalarExpr(e->getArg(2));

    // Bitcast char* → void* for the first argument.
    auto voidPtr = builder.createBitcast(loc, ptr, builder.getVoidPtrTy());

    // Get @memchr with canonical void* signature via __builtin_memchr.
    cir::FuncOp memchrFn =
        cgm.getBuiltinLibFunction(fd, Builtin::BI__builtin_memchr);

    auto call = builder.createCallOp(loc, memchrFn,
                                     mlir::ValueRange{voidPtr, val, size});

    // Bitcast void* return → char* to match __builtin_char_memchr's type.
    mlir::Value result =
        builder.createBitcast(loc, call.getResult(), ptr.getType());
    return RValue::get(result);
  }
  case Builtin::BImemcpy:
  case Builtin::BI__builtin_memcpy:
  case Builtin::BImempcpy:
  case Builtin::BI__builtin_mempcpy: {
    // OGCG calls EmitArgCheck(TCK_Store) for dest and EmitArgCheck(TCK_Load)
    // for src pointers.
    assert(!cir::MissingFeatures::memBuiltinSanitizerChecks());
    Address dest = emitPointerWithAlignment(e->getArg(0));
    Address src = emitPointerWithAlignment(e->getArg(1));
    mlir::Value sizeVal = emitScalarExpr(e->getArg(2));
    emitBuiltinArgCheck(*this, TCK_Store, dest, e->getArg(0), fd, 0);
    emitBuiltinArgCheck(*this, TCK_Load, src, e->getArg(1), fd, 1);
    // MemCpyOp requires !cir.ptr<!cir.void> — cast typed pointers.
    // Preserve address space from source pointers (e.g. OpenCL global/constant).
    auto destPtrTy = mlir::cast<cir::PointerType>(dest.getPointer().getType());
    auto srcPtrTy = mlir::cast<cir::PointerType>(src.getPointer().getType());
    auto destVoidPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        destPtrTy.getAddrSpace());
    auto srcVoidPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        srcPtrTy.getAddrSpace());
    mlir::Value destPtr =
        builder.createBitcast(dest.getPointer(), destVoidPtrTy);
    mlir::Value srcPtr =
        builder.createBitcast(src.getPointer(), srcVoidPtrTy);
    auto memcpyOp = cir::MemCpyOp::create(builder, getLoc(e->getSourceRange()),
                                           destPtr, srcPtr, sizeVal);
    // Forward pointer alignment as discardable attrs for LowerToLLVM.
    // OGCG passes Address objects (with alignment) to Builder.CreateMemCpy;
    // CIR attaches alignment as attrs that the lowering reads.
    if (unsigned dstAlign = dest.getAlignment().getQuantity())
      memcpyOp->setAttr("cir.dst_align",
                        getI64AttrCached(dstAlign));
    if (unsigned srcAlign = src.getAlignment().getQuantity())
      memcpyOp->setAttr("cir.src_align",
                        getI64AttrCached(srcAlign));
    if (builtinID == Builtin::BImempcpy ||
        builtinID == Builtin::BI__builtin_mempcpy) {
      // mempcpy returns a pointer to the byte following the last written byte
      auto ptrTy = cir::PointerType::get(builder.getUInt8Ty(),
                                         destPtrTy.getAddrSpace());
      auto castBuf = builder.createBitcast(destPtr, ptrTy);
      auto gep = cir::PtrStrideOp::create(builder, getLoc(e->getSourceRange()),
                                          ptrTy, castBuf, sizeVal,
          cir::GEPNoWrapFlags::inbounds);
      // Cast back to void* to match the return type.
      return RValue::get(builder.createBitcast(gep, destVoidPtrTy));
    }
    return RValue::get(destPtr);
  }
  case Builtin::BImemmove:
  case Builtin::BI__builtin_memmove: {
    // OGCG calls EmitArgCheck(TCK_Store) for dest and EmitArgCheck(TCK_Load)
    // for src pointers.
    assert(!cir::MissingFeatures::memBuiltinSanitizerChecks());
    Address dest = emitPointerWithAlignment(e->getArg(0));
    Address src = emitPointerWithAlignment(e->getArg(1));
    mlir::Value sizeVal = emitScalarExpr(e->getArg(2));
    emitBuiltinArgCheck(*this, TCK_Store, dest, e->getArg(0), fd, 0);
    emitBuiltinArgCheck(*this, TCK_Load, src, e->getArg(1), fd, 1);
    // MemMoveOp requires !cir.ptr<!cir.void> — cast typed pointers.
    // Preserve address space from source pointers.
    auto mmDestPtrTy =
        mlir::cast<cir::PointerType>(dest.getPointer().getType());
    auto mmSrcPtrTy =
        mlir::cast<cir::PointerType>(src.getPointer().getType());
    auto mmDestVoidPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        mmDestPtrTy.getAddrSpace());
    auto mmSrcVoidPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        mmSrcPtrTy.getAddrSpace());
    mlir::Value destPtr =
        builder.createBitcast(dest.getPointer(), mmDestVoidPtrTy);
    mlir::Value srcPtr =
        builder.createBitcast(src.getPointer(), mmSrcVoidPtrTy);
    auto memmoveOp = cir::MemMoveOp::create(builder, getLoc(e->getSourceRange()),
                                            destPtr, srcPtr, sizeVal);
    if (unsigned dstAlign = dest.getAlignment().getQuantity())
      memmoveOp->setAttr("cir.dst_align",
                         getI64AttrCached(dstAlign));
    if (unsigned srcAlign = src.getAlignment().getQuantity())
      memmoveOp->setAttr("cir.src_align",
                         getI64AttrCached(srcAlign));
    return RValue::get(destPtr);
  }
  case Builtin::BImemset:
  case Builtin::BI__builtin_memset: {
    // OGCG calls EmitNonNullArgCheck for the destination pointer.
    assert(!cir::MissingFeatures::memBuiltinSanitizerChecks());
    Address dest = emitPointerWithAlignment(e->getArg(0));
    emitNonNullArgCheck(dest, e->getArg(0)->getType(),
                        e->getArg(0)->getExprLoc(), fd, 0);
    mlir::Value byteVal = emitScalarExpr(e->getArg(1));
    mlir::Value sizeVal = emitScalarExpr(e->getArg(2));
    byteVal = builder.createIntCast(byteVal,
                                    cir::IntType::get(builder.getContext(), 32,
                                                      /*isSigned=*/true));
    // MemSetOp requires !cir.ptr<!cir.void> — cast typed pointers.
    // Preserve address space from source pointer.
    {
      auto msDstPtrTy =
          mlir::cast<cir::PointerType>(dest.getPointer().getType());
      auto msDstVoidPtrTy = cir::PointerType::get(
          builder.getContext(), cir::VoidType::get(builder.getContext()),
          msDstPtrTy.getAddrSpace());
      mlir::Value destPtr =
          builder.createBitcast(dest.getPointer(), msDstVoidPtrTy);
      auto memsetOp = cir::MemSetOp::create(
          builder, getLoc(e->getSourceRange()), destPtr, byteVal, sizeVal);
      if (unsigned dstAlign = dest.getAlignment().getQuantity())
        memsetOp->setAttr("cir.dst_align", getI64AttrCached(dstAlign));
      return RValue::get(destPtr);
    }
  }
  case Builtin::BI__builtin___memcpy_chk: {
    // fold __builtin_memcpy_chk(x, y, cst1, cst2) to memcpy iff cst1<=cst2.
    llvm::APSInt size;
    if (isMemBuiltinOutOfBoundPossible(e->getArg(2), e->getArg(3),
                                       cgm.getASTContext(), size))
      break;
    Address dest = emitPointerWithAlignment(e->getArg(0));
    Address src = emitPointerWithAlignment(e->getArg(1));
    auto emitLoc = getLoc(e->getSourceRange());
    cir::ConstantOp sizeOp = builder.getConstInt(emitLoc, size);
    // MemCpyOp requires !cir.ptr<!cir.void> — cast typed pointers.
    // Preserve address space from source pointers.
    auto chkCpyDestAS = mlir::cast<cir::PointerType>(
        dest.getPointer().getType()).getAddrSpace();
    auto chkCpySrcAS = mlir::cast<cir::PointerType>(
        src.getPointer().getType()).getAddrSpace();
    auto chkCpyDestVoidPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        chkCpyDestAS);
    auto chkCpySrcVoidPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        chkCpySrcAS);
    mlir::Value destPtr =
        builder.createBitcast(dest.getPointer(), chkCpyDestVoidPtrTy);
    mlir::Value srcPtr =
        builder.createBitcast(src.getPointer(), chkCpySrcVoidPtrTy);
    auto chkCpyOp =
        cir::MemCpyOp::create(builder, emitLoc, destPtr, srcPtr, sizeOp);
    // Forward pointer alignment. Same pattern as BI__builtin_memcpy.
    if (unsigned dstAlign = dest.getAlignment().getQuantity())
      chkCpyOp->setAttr("cir.dst_align", getI64AttrCached(dstAlign));
    if (unsigned srcAlign = src.getAlignment().getQuantity())
      chkCpyOp->setAttr("cir.src_align", getI64AttrCached(srcAlign));
    return RValue::get(destPtr);
  }
  case Builtin::BI__builtin_objc_memmove_collectable:
    break; // Handled as library call below.
  case Builtin::BI__builtin___memmove_chk: {
    // fold __builtin_memmove_chk(x, y, cst1, cst2) to memmove iff
    // cst1<=cst2.
    llvm::APSInt size;
    if (isMemBuiltinOutOfBoundPossible(e->getArg(2), e->getArg(3),
                                       cgm.getASTContext(), size))
      break;
    Address dest = emitPointerWithAlignment(e->getArg(0));
    Address src = emitPointerWithAlignment(e->getArg(1));
    auto emitLoc = getLoc(e->getSourceRange());
    cir::ConstantOp sizeOp = builder.getConstInt(emitLoc, size);
    // MemMoveOp requires !cir.ptr<!cir.void> — cast typed pointers.
    // Preserve address space from source pointers.
    auto chkMvDestAS = mlir::cast<cir::PointerType>(
        dest.getPointer().getType()).getAddrSpace();
    auto chkMvSrcAS = mlir::cast<cir::PointerType>(
        src.getPointer().getType()).getAddrSpace();
    auto chkMvDestVoidPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        chkMvDestAS);
    auto chkMvSrcVoidPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        chkMvSrcAS);
    mlir::Value destPtr =
        builder.createBitcast(dest.getPointer(), chkMvDestVoidPtrTy);
    mlir::Value srcPtr =
        builder.createBitcast(src.getPointer(), chkMvSrcVoidPtrTy);
    auto chkMvOp =
        cir::MemMoveOp::create(builder, emitLoc, destPtr, srcPtr, sizeOp);
    // Forward pointer alignment. Same pattern as BI__builtin_memmove.
    if (unsigned dstAlign = dest.getAlignment().getQuantity())
      chkMvOp->setAttr("cir.dst_align", getI64AttrCached(dstAlign));
    if (unsigned srcAlign = src.getAlignment().getQuantity())
      chkMvOp->setAttr("cir.src_align", getI64AttrCached(srcAlign));
    return RValue::get(destPtr);
  }
  case Builtin::BI__builtin_trivially_relocate: {
    // __builtin_trivially_relocate(dest, source, count) is semantically a
    // memmove of count * sizeof(element) bytes. Handle it directly like
    // BImemmove rather than as a library call, because the function's Sema-
    // synthesized declaration has reference parameters that would trigger
    // the "reference binding to unmaterialized r-value" assertion in
    // emitCallArg.
    Address dest = emitPointerWithAlignment(e->getArg(0));
    Address src = emitPointerWithAlignment(e->getArg(1));
    mlir::Value sizeVal = emitScalarExpr(e->getArg(2));
    // Multiply count by element size to get byte count.
    CharUnits elementSize =
        getContext().getTypeSizeInChars(
            e->getArg(0)->getType()->getPointeeType());
    auto emitLoc = getLoc(e->getSourceRange());
    mlir::Value elemSizeVal =
        builder.getConstInt(emitLoc, sizeVal.getType(),
                            elementSize.getQuantity());
    sizeVal = cir::BinOp::create(builder, emitLoc, cir::BinOpKind::Mul,
                                 sizeVal, elemSizeVal, {}, {}, {}, {}, {}, {});
    // Cast typed pointers to void* for cir.libc.memmove.
    // Preserve address space from source pointers.
    auto relocDestAS = mlir::cast<cir::PointerType>(
        dest.getPointer().getType()).getAddrSpace();
    auto relocSrcAS = mlir::cast<cir::PointerType>(
        src.getPointer().getType()).getAddrSpace();
    auto relocDestVoidPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        relocDestAS);
    auto relocSrcVoidPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        relocSrcAS);
    mlir::Value destPtr =
        builder.createBitcast(dest.getPointer(), relocDestVoidPtrTy);
    mlir::Value srcPtr =
        builder.createBitcast(src.getPointer(), relocSrcVoidPtrTy);
    auto memmoveOp = cir::MemMoveOp::create(builder, emitLoc, destPtr, srcPtr,
                                            sizeVal);
    if (unsigned dstAlign = dest.getAlignment().getQuantity())
      memmoveOp->setAttr("cir.dst_align",
                         getI64AttrCached(dstAlign));
    if (unsigned srcAlign = src.getAlignment().getQuantity())
      memmoveOp->setAttr("cir.src_align",
                         getI64AttrCached(srcAlign));
    return RValue::get(dest.getPointer());
  }
  case Builtin::BI__builtin___memset_chk: {
    // fold __builtin_memset_chk(x, y, cst1, cst2) to memset iff cst1<=cst2.
    llvm::APSInt size;
    if (isMemBuiltinOutOfBoundPossible(e->getArg(2), e->getArg(3),
                                       cgm.getASTContext(), size))
      break;
    Address dest = emitPointerWithAlignment(e->getArg(0));
    mlir::Value byteVal = emitScalarExpr(e->getArg(1));
    auto emitLoc = getLoc(e->getSourceRange());
    byteVal = builder.createIntCast(
        byteVal,
        cir::IntType::get(builder.getContext(), 32, /*isSigned=*/true));
    cir::ConstantOp sizeOp = builder.getConstInt(emitLoc, size);
    // MemSetOp requires !cir.ptr<!cir.void> — cast typed pointers.
    // Preserve address space from source pointer.
    auto chkMsDestAS = mlir::cast<cir::PointerType>(
        dest.getPointer().getType()).getAddrSpace();
    auto chkMsDestVoidPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        chkMsDestAS);
    mlir::Value destPtr =
        builder.createBitcast(dest.getPointer(), chkMsDestVoidPtrTy);
    auto memsetOp = cir::MemSetOp::create(builder, emitLoc, destPtr, byteVal,
                                          sizeOp);
    if (unsigned dstAlign = dest.getAlignment().getQuantity())
      memsetOp->setAttr("cir.dst_align",
                        getI64AttrCached(dstAlign));
    return RValue::get(destPtr);
  }
  case Builtin::BI__builtin_wmemchr:
  case Builtin::BI__builtin_wmemcmp:
    break; // Handled as library calls below.
  case Builtin::BI__builtin_dwarf_cfa: {
    // Match OGCG by using a hard-coded 0 on targets without a dedicated hook.
    // This is correct for most targets, including our parity targets.
    mlir::Location loc = getLoc(e->getExprLoc());
    return RValue::get(cir::LLVMIntrinsicCallOp::create(
                           builder, loc, cgm.getStringAttrCached("eh.dwarf.cfa"),
                           convertType(e->getType()),
                           mlir::ValueRange{
                               builder.getConstInt(loc, builder.getUInt32Ty(), 0)})
                           .getResult());
  }
  case Builtin::BI__builtin_return_address: {
    llvm::APSInt level = e->getArg(0)->EvaluateKnownConstInt(getContext());
    return RValue::get(cir::ReturnAddrOp::create(
        builder, getLoc(e->getExprLoc()),
        builder.getConstAPInt(loc, builder.getUInt32Ty(), level)));
  }
  case Builtin::BI_ReturnAddress: {
    return RValue::get(cir::ReturnAddrOp::create(
        builder, getLoc(e->getExprLoc()),
        builder.getConstInt(loc, builder.getUInt32Ty(), 0)));
  }
  case Builtin::BI__builtin_frame_address: {
    llvm::APSInt level = e->getArg(0)->EvaluateKnownConstInt(getContext());
    mlir::Location loc = getLoc(e->getExprLoc());
    mlir::Value addr = cir::FrameAddrOp::create(
        builder, loc, allocaInt8PtrTy,
        builder.getConstAPInt(loc, builder.getUInt32Ty(), level));
    // Preserve the alloca address space when bitcasting to void*.
    auto addrPtrTy = mlir::cast<cir::PointerType>(addr.getType());
    auto resultPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        addrPtrTy.getAddrSpace());
    return RValue::get(
        builder.createCast(loc, cir::CastKind::bitcast, addr, resultPtrTy));
  }
  case Builtin::BI__builtin_stack_address: {
    mlir::Location loc = getLoc(e->getExprLoc());
    mlir::Value addr = cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("stackaddress"),
        allocaInt8PtrTy, mlir::ValueRange{}).getResult();
    // Preserve the alloca address space when bitcasting to void*.
    auto addrPtrTy = mlir::cast<cir::PointerType>(addr.getType());
    auto resultPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        addrPtrTy.getAddrSpace());
    return RValue::get(
        builder.createCast(loc, cir::CastKind::bitcast, addr, resultPtrTy));
  }
  case Builtin::BI__builtin_extract_return_addr: {
    // Identity on non-Sparc. TODO: target hook for Sparc.
    return RValue::get(emitScalarExpr(e->getArg(0)));
  }
  case Builtin::BI__builtin_frob_return_addr:
    // Identity on non-Sparc. TODO: target hook for Sparc.
    return RValue::get(emitScalarExpr(e->getArg(0)));
  case Builtin::BI__builtin_dwarf_sp_column:
  case Builtin::BI__builtin_init_dwarf_reg_size_table:
  case Builtin::BI__builtin_eh_return:
    return errorBuiltinNYI(*this, e, builtinID);
  case Builtin::BI__builtin_unwind_init: {
    cir::LLVMIntrinsicCallOp::create(
        builder, getLoc(e->getSourceRange()),
        cgm.getStringAttrCached("eh.unwind.init"),
        mlir::Type{}, mlir::ValueRange{});
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_extend_pointer:
    return errorBuiltinNYI(*this, e, builtinID);

  case Builtin::BI__builtin_setjmp: {
    Address buf = emitPointerWithAlignment(e->getArg(0));
    mlir::Location setjmpLoc = getLoc(e->getExprLoc());

    cir::PointerType ppTy = builder.getPointerTo(builder.getVoidPtrTy());
    mlir::Value castBuf = builder.createBitcast(buf.getPointer(), ppTy);

    if (getTarget().getTriple().isSystemZ()) {
      llvm_unreachable("SYSTEMZ NYI");
    }

    mlir::Value frameAddr =
        cir::FrameAddrOp::create(
            builder, setjmpLoc, builder.getVoidPtrTy(),
            mlir::ValueRange{builder.getUInt32(0, setjmpLoc)})
            .getResult();

    (void)cir::StoreOp::create(static_cast<mlir::OpBuilder &>(builder),
                               setjmpLoc, frameAddr, castBuf,
                               /*is_volatile=*/false, /*is_nontemporal=*/false,
                               /*alignment=*/mlir::IntegerAttr{},
                               /*sync_scope=*/cir::SyncScopeKindAttr{},
                               /*mem_order=*/cir::MemOrderAttr{},
                               /*tbaa=*/cir::TBAAAttr{});
    mlir::Value stacksave =
        cir::StackSaveOp::create(builder, setjmpLoc, builder.getVoidPtrTy())
            .getResult();
    cir::PtrStrideOp stackSaveSlot = cir::PtrStrideOp::create(
        builder, setjmpLoc, ppTy, castBuf, builder.getSInt32(2, setjmpLoc),
        cir::GEPNoWrapFlags::inbounds);
    (void)cir::StoreOp::create(static_cast<mlir::OpBuilder &>(builder),
                               setjmpLoc, stacksave, stackSaveSlot,
                               /*is_volatile=*/false,
                               /*is_nontemporal=*/false,
                               /*alignment=*/mlir::IntegerAttr{},
                               /*sync_scope=*/cir::SyncScopeKindAttr{},
                               /*mem_order=*/cir::MemOrderAttr{},
                               /*tbaa=*/cir::TBAAAttr{});
    auto op = cir::EhSetjmpOp::create(builder, setjmpLoc, castBuf,
                                      /*is_builtin=*/true);
    return RValue::get(op);
  }
  case Builtin::BI__builtin_longjmp: {
    mlir::Value buf = emitScalarExpr(e->getArg(0));
    mlir::Location longjmpLoc = getLoc(e->getExprLoc());

    cir::PointerType ppTy = builder.getPointerTo(builder.getVoidPtrTy());
    mlir::Value castBuf = builder.createBitcast(buf, ppTy);

    cir::EhLongjmpOp::create(builder, longjmpLoc, castBuf);
    cir::UnreachableOp::create(builder, longjmpLoc);
    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_launder: {
    const clang::Expr *arg = e->getArg(0);
    QualType argTy = arg->getType()->getPointeeType();
    mlir::Value ptr = emitScalarExpr(arg);
    // __builtin_launder is a compiler barrier for devirtualization.
    // Only emit launder.invariant.group when StrictVTablePointers is enabled
    // AND the pointee type is a dynamic class (or contains one as a
    // subobject). Matches OGCG's TypeRequiresBuiltinLaunder check.
    if (typeRequiresBuiltinLaunder(getCIRGenModule(), argTy))
      ptr = cir::InvariantGroupOp::create(
          builder, getLoc(e->getSourceRange()), ptr);
    return RValue::get(ptr);
  }

  case Builtin::BI__sync_fetch_and_add:
  case Builtin::BI__sync_fetch_and_sub:
  case Builtin::BI__sync_fetch_and_or:
  case Builtin::BI__sync_fetch_and_and:
  case Builtin::BI__sync_fetch_and_xor:
  case Builtin::BI__sync_fetch_and_nand:
  case Builtin::BI__sync_add_and_fetch:
  case Builtin::BI__sync_sub_and_fetch:
  case Builtin::BI__sync_and_and_fetch:
  case Builtin::BI__sync_or_and_fetch:
  case Builtin::BI__sync_xor_and_fetch:
  case Builtin::BI__sync_nand_and_fetch:
  case Builtin::BI__sync_val_compare_and_swap:
  case Builtin::BI__sync_bool_compare_and_swap:
  case Builtin::BI__sync_lock_test_and_set:
  case Builtin::BI__sync_lock_release:
  case Builtin::BI__sync_swap:
    llvm_unreachable("Shouldn't make it through sema");

  case Builtin::BI__sync_fetch_and_add_1:
  case Builtin::BI__sync_fetch_and_add_2:
  case Builtin::BI__sync_fetch_and_add_4:
  case Builtin::BI__sync_fetch_and_add_8:
  case Builtin::BI__sync_fetch_and_add_16:
    return emitBinaryAtomic(*this, cir::AtomicFetchKind::Add, e);

  case Builtin::BI__sync_fetch_and_sub_1:
  case Builtin::BI__sync_fetch_and_sub_2:
  case Builtin::BI__sync_fetch_and_sub_4:
  case Builtin::BI__sync_fetch_and_sub_8:
  case Builtin::BI__sync_fetch_and_sub_16:
    return emitBinaryAtomic(*this, cir::AtomicFetchKind::Sub, e);

  case Builtin::BI__sync_fetch_and_or_1:
  case Builtin::BI__sync_fetch_and_or_2:
  case Builtin::BI__sync_fetch_and_or_4:
  case Builtin::BI__sync_fetch_and_or_8:
  case Builtin::BI__sync_fetch_and_or_16:
    return emitBinaryAtomic(*this, cir::AtomicFetchKind::Or, e);

  case Builtin::BI__sync_fetch_and_and_1:
  case Builtin::BI__sync_fetch_and_and_2:
  case Builtin::BI__sync_fetch_and_and_4:
  case Builtin::BI__sync_fetch_and_and_8:
  case Builtin::BI__sync_fetch_and_and_16:
    return emitBinaryAtomic(*this, cir::AtomicFetchKind::And, e);

  case Builtin::BI__sync_fetch_and_xor_1:
  case Builtin::BI__sync_fetch_and_xor_2:
  case Builtin::BI__sync_fetch_and_xor_4:
  case Builtin::BI__sync_fetch_and_xor_8:
  case Builtin::BI__sync_fetch_and_xor_16:
    return emitBinaryAtomic(*this, cir::AtomicFetchKind::Xor, e);

  case Builtin::BI__sync_fetch_and_nand_1:
  case Builtin::BI__sync_fetch_and_nand_2:
  case Builtin::BI__sync_fetch_and_nand_4:
  case Builtin::BI__sync_fetch_and_nand_8:
  case Builtin::BI__sync_fetch_and_nand_16:
    return emitBinaryAtomic(*this, cir::AtomicFetchKind::Nand, e);

  case Builtin::BI__sync_fetch_and_min:
    return emitBinaryAtomic(*this, cir::AtomicFetchKind::Min, e);
  case Builtin::BI__sync_fetch_and_max:
    return emitBinaryAtomic(*this, cir::AtomicFetchKind::Max, e);
  case Builtin::BI__sync_fetch_and_umin:
    return emitBinaryAtomic(*this, cir::AtomicFetchKind::UMin, e);
  case Builtin::BI__sync_fetch_and_umax:
    return emitBinaryAtomic(*this, cir::AtomicFetchKind::UMax, e);

  case Builtin::BI__sync_add_and_fetch_1:
  case Builtin::BI__sync_add_and_fetch_2:
  case Builtin::BI__sync_add_and_fetch_4:
  case Builtin::BI__sync_add_and_fetch_8:
  case Builtin::BI__sync_add_and_fetch_16:
    return emitBinaryAtomicPost(*this, cir::AtomicFetchKind::Add, e,
                                cir::BinOpKind::Add);
  case Builtin::BI__sync_sub_and_fetch_1:
  case Builtin::BI__sync_sub_and_fetch_2:
  case Builtin::BI__sync_sub_and_fetch_4:
  case Builtin::BI__sync_sub_and_fetch_8:
  case Builtin::BI__sync_sub_and_fetch_16:
    return emitBinaryAtomicPost(*this, cir::AtomicFetchKind::Sub, e,
                                cir::BinOpKind::Sub);
  case Builtin::BI__sync_and_and_fetch_1:
  case Builtin::BI__sync_and_and_fetch_2:
  case Builtin::BI__sync_and_and_fetch_4:
  case Builtin::BI__sync_and_and_fetch_8:
  case Builtin::BI__sync_and_and_fetch_16:
    return emitBinaryAtomicPost(*this, cir::AtomicFetchKind::And, e,
                                cir::BinOpKind::And);
  case Builtin::BI__sync_or_and_fetch_1:
  case Builtin::BI__sync_or_and_fetch_2:
  case Builtin::BI__sync_or_and_fetch_4:
  case Builtin::BI__sync_or_and_fetch_8:
  case Builtin::BI__sync_or_and_fetch_16:
    return emitBinaryAtomicPost(*this, cir::AtomicFetchKind::Or, e,
                                cir::BinOpKind::Or);
  case Builtin::BI__sync_xor_and_fetch_1:
  case Builtin::BI__sync_xor_and_fetch_2:
  case Builtin::BI__sync_xor_and_fetch_4:
  case Builtin::BI__sync_xor_and_fetch_8:
  case Builtin::BI__sync_xor_and_fetch_16:
    return emitBinaryAtomicPost(*this, cir::AtomicFetchKind::Xor, e,
                                cir::BinOpKind::Xor);
  case Builtin::BI__sync_nand_and_fetch_1:
  case Builtin::BI__sync_nand_and_fetch_2:
  case Builtin::BI__sync_nand_and_fetch_4:
  case Builtin::BI__sync_nand_and_fetch_8:
  case Builtin::BI__sync_nand_and_fetch_16:
    return emitBinaryAtomicPost(*this, cir::AtomicFetchKind::Nand, e,
                                cir::BinOpKind::And, true);

  case Builtin::BI__sync_val_compare_and_swap_1:
  case Builtin::BI__sync_val_compare_and_swap_2:
  case Builtin::BI__sync_val_compare_and_swap_4:
  case Builtin::BI__sync_val_compare_and_swap_8:
  case Builtin::BI__sync_val_compare_and_swap_16:
    return RValue::get(makeAtomicCmpXchgValue(*this, e, false));

  case Builtin::BI__sync_bool_compare_and_swap_1:
  case Builtin::BI__sync_bool_compare_and_swap_2:
  case Builtin::BI__sync_bool_compare_and_swap_4:
  case Builtin::BI__sync_bool_compare_and_swap_8:
  case Builtin::BI__sync_bool_compare_and_swap_16:
    return RValue::get(makeAtomicCmpXchgValue(*this, e, true));

  case Builtin::BI__sync_swap_1:
  case Builtin::BI__sync_swap_2:
  case Builtin::BI__sync_swap_4:
  case Builtin::BI__sync_swap_8:
  case Builtin::BI__sync_swap_16:
  case Builtin::BI__sync_lock_test_and_set_1:
  case Builtin::BI__sync_lock_test_and_set_2:
  case Builtin::BI__sync_lock_test_and_set_4:
  case Builtin::BI__sync_lock_test_and_set_8:
  case Builtin::BI__sync_lock_test_and_set_16:
    return emitAtomicXchg(*this, e);

  case Builtin::BI__sync_lock_release_1:
  case Builtin::BI__sync_lock_release_2:
  case Builtin::BI__sync_lock_release_4:
  case Builtin::BI__sync_lock_release_8:
  case Builtin::BI__sync_lock_release_16:
    return emitSyncLockRelease(*this, e);

  case Builtin::BI__builtin_nontemporal_load:
    return RValue::get(emitNontemporalLoad(*this, e));
  case Builtin::BI__builtin_nontemporal_store:
    emitNontemporalStore(*this, e);
    return RValue::get(nullptr);

  case Builtin::BI__c11_atomic_is_lock_free:
  case Builtin::BI__atomic_is_lock_free: {
    // Call "bool __atomic_is_lock_free(size_t size, void *ptr)".
    // For the __c11 builtin, ptr is null (properly-aligned object).
    mlir::Location callLoc = getLoc(e->getSourceRange());
    mlir::Value size = emitScalarExpr(e->getArg(0));
    mlir::Value ptr;
    if (builtinID == Builtin::BI__atomic_is_lock_free)
      ptr = emitScalarExpr(e->getArg(1));
    else
      ptr = builder.getNullPtr(builder.getVoidPtrTy(), callLoc);

    mlir::Type boolTy = convertType(e->getType());
    mlir::Type sizeTy = size.getType();
    mlir::Type ptrTy = ptr.getType();

    auto fnTy = cir::FuncType::get({sizeTy, ptrTy}, boolTy);
    cir::FuncOp fnOp = cgm.getOrCreateCIRFunction(
        "__atomic_is_lock_free", fnTy, clang::GlobalDecl(), /*forVTable=*/false,
        /*dontDefer=*/false, /*isThunk=*/false,
        /*isForDefinition=*/NotForDefinition);
    auto call =
        builder.createCallOp(callLoc, fnOp, mlir::ValueRange{size, ptr});
    return RValue::get(call.getResult());
  }
  // Note: BI__atomic_test_and_set and BI__atomic_clear are handled by
  // CIRGenAtomic.cpp via AtomicExpr, so they never reach this switch.
  case Builtin::BI__atomic_thread_fence:
  case Builtin::BI__c11_atomic_thread_fence: {
    emitAtomicFenceOp(*this, e, cir::SyncScopeKind::System);
    return RValue::get(nullptr);
  }
  case Builtin::BI__atomic_signal_fence:
  case Builtin::BI__c11_atomic_signal_fence: {
    emitAtomicFenceOp(*this, e, cir::SyncScopeKind::SingleThread);
    return RValue::get(nullptr);
  }
  case Builtin::BI__sync_synchronize: {
    // __sync_synchronize is always seq_cst with system scope.
    mlir::Location loc = getLoc(e->getSourceRange());
    cir::AtomicFenceOp::create(
        builder, loc, cir::MemOrder::SequentiallyConsistent,
        cir::SyncScopeKindAttr::get(&getMLIRContext(),
                                    cir::SyncScopeKind::System));
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_signbit:
  case Builtin::BI__builtin_signbitf:
  case Builtin::BI__builtin_signbitl: {
    if (ignoreResult) {
      emitScalarExpr(e->getArg(0));
      return RValue::getIgnored();
    }

    mlir::Location loc = getLoc(e->getSourceRange());
    mlir::Value value = emitScalarExpr(e->getArg(0));
    mlir::Value isNeg = emitSignBit(loc, *this, value);

    // Convert bool to the return type (int)
    mlir::Type retTy = convertType(e->getType());
    mlir::Value result = builder.createBoolToInt(isNeg, retTy);
    return RValue::get(result);
  }
  case Builtin::BI__scoped_atomic_thread_fence:
    return emitScopedAtomicFenceOp(*this, e, builtinID);
  case Builtin::BI__annotation:
    return errorBuiltinNYI(*this, e, builtinID);
  case Builtin::BI__warn_memset_zero_len:
    return RValue::getIgnored();
  case Builtin::BI__builtin_addcb:
  case Builtin::BI__builtin_addcs:
  case Builtin::BI__builtin_addc:
  case Builtin::BI__builtin_addcl:
  case Builtin::BI__builtin_addcll:
  case Builtin::BI__builtin_subcb:
  case Builtin::BI__builtin_subcs:
  case Builtin::BI__builtin_subc:
  case Builtin::BI__builtin_subcl:
  case Builtin::BI__builtin_subcll: {
    // Translate __builtin_addc(x, y, carryin, &carryout) to:
    //   {sum1, carry1} = uadd.with.overflow(x, y)
    //   {sum2, carry2} = uadd.with.overflow(sum1, carryin)
    //   carryout = zext(carry1 | carry2)
    //   result = sum2
    // Similarly for __builtin_subc with usub.with.overflow.
    mlir::Value x = emitScalarExpr(e->getArg(0));
    mlir::Value y = emitScalarExpr(e->getArg(1));
    mlir::Value carryin = emitScalarExpr(e->getArg(2));
    Address carryOutPtr = emitPointerWithAlignment(e->getArg(3));

    cir::BinOpOverflowKind opKind;
    switch (builtinID) {
    default:
      llvm_unreachable("Unknown multiprecision builtin id.");
    case Builtin::BI__builtin_addcb:
    case Builtin::BI__builtin_addcs:
    case Builtin::BI__builtin_addc:
    case Builtin::BI__builtin_addcl:
    case Builtin::BI__builtin_addcll:
      opKind = cir::BinOpOverflowKind::Add;
      break;
    case Builtin::BI__builtin_subcb:
    case Builtin::BI__builtin_subcs:
    case Builtin::BI__builtin_subc:
    case Builtin::BI__builtin_subcl:
    case Builtin::BI__builtin_subcll:
      opKind = cir::BinOpOverflowKind::Sub;
      break;
    }

    mlir::Location loc = getLoc(e->getSourceRange());
    auto resultTy = mlir::cast<cir::IntType>(x.getType());

    // First overflow op: {sum1, carry1} = op(x, y)
    auto op1 = cir::BinOpOverflowOp::create(builder, loc, resultTy, opKind, x,
                                             y);
    mlir::Value sum1 = op1.getResult();
    mlir::Value carry1 = op1.getOverflow();

    // Second overflow op: {sum2, carry2} = op(sum1, carryin)
    auto op2 = cir::BinOpOverflowOp::create(builder, loc, resultTy, opKind,
                                             sum1, carryin);
    mlir::Value sum2 = op2.getResult();
    mlir::Value carry2 = op2.getOverflow();

    // Combine carries: carryOut = zext(carry1 | carry2)
    mlir::Value carryOr = cir::BinOp::create(builder, loc, carry1.getType(),
                                              cir::BinOpKind::Or, carry1,
                                              carry2, {}, {}, {}, {}, {}, {});
    mlir::Value carryOut =
        builder.createBoolToInt(carryOr, x.getType());
    builder.createStore(loc, carryOut, carryOutPtr);

    return RValue::get(sum2);
  }

  case Builtin::BI__builtin_add_overflow:
  case Builtin::BI__builtin_sub_overflow:
  case Builtin::BI__builtin_mul_overflow: {
    const clang::Expr *leftArg = e->getArg(0);
    const clang::Expr *rightArg = e->getArg(1);
    const clang::Expr *resultArg = e->getArg(2);

    clang::QualType resultQTy =
        resultArg->getType()->castAs<clang::PointerType>()->getPointeeType();

    WidthAndSignedness leftInfo =
        getIntegerWidthAndSignedness(cgm.getASTContext(), leftArg->getType());
    WidthAndSignedness rightInfo =
        getIntegerWidthAndSignedness(cgm.getASTContext(), rightArg->getType());
    WidthAndSignedness resultInfo =
        getIntegerWidthAndSignedness(cgm.getASTContext(), resultQTy);

    if (isSpecialMixedSignMultiply(builtinID, leftInfo, rightInfo, resultInfo))
      return emitCheckedMixedSignMultiply(*this, leftArg, leftInfo, rightArg,
                                          rightInfo, resultArg, resultQTy,
                                          resultInfo);

    // Note we compute the encompassing type with the consideration to the
    // result type, so later in LLVM lowering we don't get redundant integral
    // extension casts.
    WidthAndSignedness encompassingInfo =
        EncompassingIntegerType({leftInfo, rightInfo, resultInfo});

    auto encompassingCIRTy = cir::IntType::get(
        &getMLIRContext(), encompassingInfo.width, encompassingInfo.isSigned);

    // The result type for the overflow op must be cir::IntType. When the
    // result QualType maps to a non-IntType (e.g. _Bool -> cir::BoolType),
    // construct an IntType from the width/signedness info instead.
    mlir::Type actualResultTy = cgm.convertType(resultQTy);
    cir::IntType resultCIRTy;
    if (auto intTy = mlir::dyn_cast<cir::IntType>(actualResultTy))
      resultCIRTy = intTy;
    else
      resultCIRTy = cir::IntType::get(&getMLIRContext(), resultInfo.width,
                                      resultInfo.isSigned);

    mlir::Value left = emitScalarExpr(leftArg);
    mlir::Value right = emitScalarExpr(rightArg);
    Address resultPtr = emitPointerWithAlignment(resultArg);

    // Extend each operand to the encompassing type, if necessary.
    if (left.getType() != encompassingCIRTy) {
      auto castKind = mlir::isa<cir::BoolType>(left.getType())
                          ? cir::CastKind::bool_to_int
                          : cir::CastKind::integral;
      left = builder.createCast(castKind, left, encompassingCIRTy);
    }
    if (right.getType() != encompassingCIRTy) {
      auto castKind = mlir::isa<cir::BoolType>(right.getType())
                          ? cir::CastKind::bool_to_int
                          : cir::CastKind::integral;
      right = builder.createCast(castKind, right, encompassingCIRTy);
    }

    // Perform the operation on the extended values.
    cir::BinOpOverflowKind opKind;
    switch (builtinID) {
    default:
      llvm_unreachable("Unknown overflow builtin id.");
    case Builtin::BI__builtin_add_overflow:
      opKind = cir::BinOpOverflowKind::Add;
      break;
    case Builtin::BI__builtin_sub_overflow:
      opKind = cir::BinOpOverflowKind::Sub;
      break;
    case Builtin::BI__builtin_mul_overflow:
      opKind = cir::BinOpOverflowKind::Mul;
      break;
    }

    mlir::Location loc = getLoc(e->getSourceRange());
    auto arithOp = cir::BinOpOverflowOp::create(builder, loc, resultCIRTy,
                                                opKind, left, right);

    // Here is a slight difference from the original clang CodeGen:
    //   - In the original clang CodeGen, the checked arithmetic result is
    //     first computed as a value of the encompassing type, and then it is
    //     truncated to the actual result type with a second overflow checking.
    //   - In CIRGen, the checked arithmetic operation directly produce the
    //     checked arithmetic result in its expected type.
    //
    // So we don't need a truncation and a second overflow checking here.

    // Cast the result to the actual store type if it differs (e.g. _Bool).
    mlir::Value result = arithOp.getResult();
    if (actualResultTy != resultCIRTy)
      result = builder.createCast(cir::CastKind::int_to_bool, result,
                                  actualResultTy);

    // Convert result to match the pointer's element type if needed
    // (e.g. _BitInt(31) -> i32 for local alloca storage type).
    mlir::Type ptrElemTy = resultPtr.getElementType();
    if (result.getType() != ptrElemTy)
      result = builder.createCast(cir::CastKind::integral, result, ptrElemTy);

    // Finally, store the result using the pointer.
    bool isVolatile =
        resultArg->getType()->getPointeeType().isVolatileQualified();
    builder.createStore(loc, result, resultPtr, isVolatile);

    return RValue::get(arithOp.getOverflow());
  }

  case Builtin::BI__builtin_uadd_overflow:
  case Builtin::BI__builtin_uaddl_overflow:
  case Builtin::BI__builtin_uaddll_overflow:
  case Builtin::BI__builtin_usub_overflow:
  case Builtin::BI__builtin_usubl_overflow:
  case Builtin::BI__builtin_usubll_overflow:
  case Builtin::BI__builtin_umul_overflow:
  case Builtin::BI__builtin_umull_overflow:
  case Builtin::BI__builtin_umulll_overflow:
  case Builtin::BI__builtin_sadd_overflow:
  case Builtin::BI__builtin_saddl_overflow:
  case Builtin::BI__builtin_saddll_overflow:
  case Builtin::BI__builtin_ssub_overflow:
  case Builtin::BI__builtin_ssubl_overflow:
  case Builtin::BI__builtin_ssubll_overflow:
  case Builtin::BI__builtin_smul_overflow:
  case Builtin::BI__builtin_smull_overflow:
  case Builtin::BI__builtin_smulll_overflow: {
    // Scalarize our inputs.
    mlir::Value x = emitScalarExpr(e->getArg(0));
    mlir::Value y = emitScalarExpr(e->getArg(1));

    const clang::Expr *resultArg = e->getArg(2);
    Address resultPtr = emitPointerWithAlignment(resultArg);

    // Decide which of the arithmetic operation we are lowering to:
    cir::BinOpOverflowKind arithKind;
    switch (builtinID) {
    default:
      llvm_unreachable("Unknown overflow builtin id.");
    case Builtin::BI__builtin_uadd_overflow:
    case Builtin::BI__builtin_uaddl_overflow:
    case Builtin::BI__builtin_uaddll_overflow:
    case Builtin::BI__builtin_sadd_overflow:
    case Builtin::BI__builtin_saddl_overflow:
    case Builtin::BI__builtin_saddll_overflow:
      arithKind = cir::BinOpOverflowKind::Add;
      break;
    case Builtin::BI__builtin_usub_overflow:
    case Builtin::BI__builtin_usubl_overflow:
    case Builtin::BI__builtin_usubll_overflow:
    case Builtin::BI__builtin_ssub_overflow:
    case Builtin::BI__builtin_ssubl_overflow:
    case Builtin::BI__builtin_ssubll_overflow:
      arithKind = cir::BinOpOverflowKind::Sub;
      break;
    case Builtin::BI__builtin_umul_overflow:
    case Builtin::BI__builtin_umull_overflow:
    case Builtin::BI__builtin_umulll_overflow:
    case Builtin::BI__builtin_smul_overflow:
    case Builtin::BI__builtin_smull_overflow:
    case Builtin::BI__builtin_smulll_overflow:
      arithKind = cir::BinOpOverflowKind::Mul;
      break;
    }

    clang::QualType resultQTy =
        resultArg->getType()->castAs<clang::PointerType>()->getPointeeType();
    auto resultCIRTy = mlir::cast<cir::IntType>(cgm.convertType(resultQTy));

    mlir::Location loc = getLoc(e->getSourceRange());
    cir::BinOpOverflowOp arithOp = cir::BinOpOverflowOp::create(
        builder, loc, resultCIRTy, arithKind, x, y);

    bool isVolatile =
        resultArg->getType()->getPointeeType().isVolatileQualified();
    builder.createStore(loc, emitToMemory(arithOp.getResult(), resultQTy),
                        resultPtr, isVolatile);

    return RValue::get(arithOp.getOverflow());
  }

  case Builtin::BI__builtin_addressof:
    return RValue::get(emitLValue(e->getArg(0)).getPointer());
  case Builtin::BI__builtin_function_start: {
    const ValueDecl *decl =
        e->getArg(0)->getAsBuiltinConstantDeclRef(getContext());
    if (!decl || !isa<FunctionDecl>(decl))
      return errorBuiltinNYI(*this, e, builtinID);

    mlir::Location loc = getLoc(e->getSourceRange());
    mlir::Value addr =
        builder.getConstant(loc, cgm.getAddrOfFunctionAttr(decl, /*noCFI=*/true));
    mlir::Type resultTy = convertType(e->getType());
    if (addr.getType() != resultTy)
      addr = cir::CastOp::create(builder, loc, resultTy,
                                 cir::CastKind::bitcast, addr);
    return RValue::get(addr);
  }
  case Builtin::BI__builtin_operator_new:
    return emitNewOrDeleteBuiltinCall(
        e->getCallee()->getType()->castAs<FunctionProtoType>(), e, OO_New);
  case Builtin::BI__builtin_operator_delete:
    emitNewOrDeleteBuiltinCall(
        e->getCallee()->getType()->castAs<FunctionProtoType>(), e, OO_Delete);
    return RValue::get(nullptr);
  case Builtin::BI__builtin_is_aligned:
  case Builtin::BI__builtin_align_up:
  case Builtin::BI__builtin_align_down: {
    mlir::Location loc = getLoc(e->getExprLoc());
    // Emit the source value and alignment.
    QualType argType = e->getArg(0)->getType();
    mlir::Value src;
    if (argType->isArrayType())
      src = emitArrayToPointerDecay(e->getArg(0)).getPointer();
    else
      src = emitScalarExpr(e->getArg(0));
    mlir::Type srcType = src.getType();

    // Determine the integer type for arithmetic.
    bool isPtr = mlir::isa<cir::PointerType>(srcType);
    unsigned intWidth;
    if (isPtr)
      intWidth = getContext().getTargetInfo().getPointerWidth(
          argType->getPointeeType().getAddressSpace());
    else
      intWidth = mlir::cast<cir::IntType>(srcType).getWidth();
    auto intType = cir::IntType::get(&getMLIRContext(), intWidth,
                                     /*isSigned=*/false);

    // Emit alignment and compute mask = alignment - 1.
    mlir::Value alignment = emitScalarExpr(e->getArg(1));
    alignment = builder.createIntCast(alignment, intType);
    mlir::Value one = builder.getUnsignedInt(loc, 1, intWidth);
    mlir::Value mask = builder.createSub(loc, alignment, one);

    if (builtinID == Builtin::BI__builtin_is_aligned) {
      // Generate (src & (alignment - 1)) == 0.
      mlir::Value srcAddr = src;
      if (isPtr)
        srcAddr = builder.createPtrToInt(src, intType);
      mlir::Value setBits = builder.createAnd(loc, srcAddr, mask);
      mlir::Value zero = builder.getUnsignedInt(loc, 0, intWidth);
      mlir::Value result =
          builder.createCompare(loc, cir::CmpOpKind::eq, setBits, zero);
      return RValue::get(result);
    }

    // __builtin_align_up / __builtin_align_down
    bool alignUp = (builtinID == Builtin::BI__builtin_align_up);
    mlir::Value srcForMask = src;
    if (alignUp) {
      // Add mask to go over the next alignment boundary.
      if (isPtr) {
        // GEP(src, mask) — advance pointer by mask bytes.
        auto u8PtrTy = cir::PointerType::get(
            &getMLIRContext(), cir::IntType::get(&getMLIRContext(), 8,
                                                 /*isSigned=*/false));
        srcForMask = builder.createBitcast(src, u8PtrTy);
        srcForMask = builder.createPtrStride(loc, srcForMask, mask);
      } else {
        srcForMask = builder.createAdd(loc, srcForMask, mask);
      }
    }

    // Compute ~mask and apply: result = srcForMask & ~mask
    mlir::Value invertedMask = builder.createNot(mask);
    mlir::Value result;
    if (isPtr) {
      // Use cir.ptr_mask to preserve pointer provenance, matching OGCG's
      // use of @llvm.ptrmask (CGBuiltin.cpp:6627-6629).
      result = cir::PtrMaskOp::create(builder, loc, srcType, srcForMask,
                                      invertedMask);
    } else {
      result = builder.createAnd(loc, srcForMask, invertedMask);
    }
    return RValue::get(result);
  }

  case Builtin::BI__builtin_get_vtable_pointer: {
    // __builtin_get_vtable_pointer(ptr) -> const void*
    // Loads the vtable pointer from a polymorphic object.
    const Expr *target = e->getArg(0);
    QualType targetType = target->getType();
    const CXXRecordDecl *decl = targetType->getPointeeCXXRecordDecl();
    assert(decl && "expected pointer to polymorphic class");
    Address thisAddr = emitPointerWithAlignment(target);
    mlir::Value vtablePtr = getVTablePtr(getLoc(e->getSourceRange()), thisAddr,
                                         decl, VTableAuthMode::MustTrap);
    // Cast vtable ptr type to the builtin's return type (const void*).
    mlir::Type retTy = convertType(e->getType());
    if (vtablePtr.getType() != retTy)
      vtablePtr = builder.createBitcast(vtablePtr, retTy);
    return RValue::get(vtablePtr);
  }

  case Builtin::BI_InterlockedIncrement16:
  case Builtin::BI_InterlockedIncrement:
    return RValue::get(
        emitMSVCBuiltinExpr(MSVCIntrin::_InterlockedIncrement, e));
  case Builtin::BI_InterlockedDecrement16:
  case Builtin::BI_InterlockedDecrement:
    return RValue::get(
        emitMSVCBuiltinExpr(MSVCIntrin::_InterlockedDecrement, e));
  case Builtin::BI_InterlockedExchange8:
  case Builtin::BI_InterlockedExchange16:
  case Builtin::BI_InterlockedExchange:
  case Builtin::BI_InterlockedExchangePointer:
    return RValue::get(
        emitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchange, e));
  case Builtin::BI_InterlockedAnd8:
  case Builtin::BI_InterlockedAnd16:
  case Builtin::BI_InterlockedAnd:
    return RValue::get(
        emitMSVCBuiltinExpr(MSVCIntrin::_InterlockedAnd, e));
  case Builtin::BI_InterlockedExchangeAdd8:
  case Builtin::BI_InterlockedExchangeAdd16:
  case Builtin::BI_InterlockedExchangeAdd:
    return RValue::get(
        emitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeAdd, e));
  case Builtin::BI_InterlockedExchangeSub8:
  case Builtin::BI_InterlockedExchangeSub16:
  case Builtin::BI_InterlockedExchangeSub:
    return RValue::get(
        emitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeSub, e));
  case Builtin::BI_InterlockedOr8:
  case Builtin::BI_InterlockedOr16:
  case Builtin::BI_InterlockedOr:
    return RValue::get(
        emitMSVCBuiltinExpr(MSVCIntrin::_InterlockedOr, e));
  case Builtin::BI_InterlockedXor8:
  case Builtin::BI_InterlockedXor16:
  case Builtin::BI_InterlockedXor:
    return RValue::get(
        emitMSVCBuiltinExpr(MSVCIntrin::_InterlockedXor, e));
  case Builtin::BI_InterlockedCompareExchangePointer:
    return RValue::get(
        emitMSVCBuiltinExpr(MSVCIntrin::_InterlockedCompareExchange, e));
  case Builtin::BI_InterlockedCompareExchangePointer_nf:
    return RValue::get(
        emitMSVCBuiltinExpr(MSVCIntrin::_InterlockedCompareExchange_nf, e));
  case Builtin::BI_InterlockedCompareExchange8:
  case Builtin::BI_InterlockedCompareExchange16:
  case Builtin::BI_InterlockedCompareExchange:
  case Builtin::BI_InterlockedCompareExchange64:
    return RValue::get(emitAtomicCmpXchgForMSIntrin(*this, e));

  case Builtin::BI__noop:
    // __noop always evaluates to an integer literal zero.
    return RValue::get(builder.getConstant(
        getLoc(e->getSourceRange()),
        builder.getSInt32AttrLocalCached(0)));
  case Builtin::BI__builtin_call_with_static_chain: {
    const auto *call = cast<CallExpr>(e->getArg(0));
    mlir::Value staticChain = emitScalarExpr(e->getArg(1));
    return emitCall(call->getCallee()->getType(), emitCallee(call->getCallee()),
                    call, returnValue, nullptr, staticChain);
  }
  case Builtin::BI_bittest64:
  case Builtin::BI_bittest:
  case Builtin::BI_bittestandcomplement64:
  case Builtin::BI_bittestandcomplement:
  case Builtin::BI_bittestandreset64:
  case Builtin::BI_bittestandreset:
  case Builtin::BI_bittestandset64:
  case Builtin::BI_bittestandset:
  case Builtin::BI_interlockedbittestandreset:
  case Builtin::BI_interlockedbittestandreset64:
  case Builtin::BI_interlockedbittestandreset64_acq:
  case Builtin::BI_interlockedbittestandreset64_rel:
  case Builtin::BI_interlockedbittestandreset64_nf:
  case Builtin::BI_interlockedbittestandset64:
  case Builtin::BI_interlockedbittestandset64_acq:
  case Builtin::BI_interlockedbittestandset64_rel:
  case Builtin::BI_interlockedbittestandset64_nf:
  case Builtin::BI_interlockedbittestandset:
  case Builtin::BI_interlockedbittestandset_acq:
  case Builtin::BI_interlockedbittestandset_rel:
  case Builtin::BI_interlockedbittestandset_nf:
  case Builtin::BI_interlockedbittestandreset_acq:
  case Builtin::BI_interlockedbittestandreset_rel:
  case Builtin::BI_interlockedbittestandreset_nf:
    return RValue::get(emitBitTestIntrinsic(*this, builtinID, e));
  case Builtin::BI__iso_volatile_load8:
  case Builtin::BI__iso_volatile_load16:
  case Builtin::BI__iso_volatile_load32:
  case Builtin::BI__iso_volatile_load64:
    return RValue::get(emitISOVolatileLoad(*this, e));
  case Builtin::BI__iso_volatile_store8:
  case Builtin::BI__iso_volatile_store16:
  case Builtin::BI__iso_volatile_store32:
  case Builtin::BI__iso_volatile_store64:
    emitISOVolatileStore(*this, e);
    return RValue::get(nullptr);
  case Builtin::BI__builtin_ptrauth_sign_constant:
    return emitBuiltinPtrauthSignConstant(*this, e);
  case Builtin::BI__builtin_ptrauth_auth:
  case Builtin::BI__builtin_ptrauth_auth_and_resign:
  case Builtin::BI__builtin_ptrauth_blend_discriminator:
  case Builtin::BI__builtin_ptrauth_sign_generic_data:
  case Builtin::BI__builtin_ptrauth_sign_unauthenticated:
  case Builtin::BI__builtin_ptrauth_strip:
    return emitBuiltinPtrauth(*this, e, builtinID);
  case Builtin::BI__exception_code:
  case Builtin::BI_exception_code:
  case Builtin::BI__exception_info:
  case Builtin::BI_exception_info:
  case Builtin::BI__abnormal_termination:
  case Builtin::BI_abnormal_termination:
  case Builtin::BI_setjmpex:
    return errorBuiltinNYI(*this, e, builtinID);
  case Builtin::BI_setjmp: {
    if (getTarget().getTriple().isOSMSVCRT() && e->getNumArgs() == 1 &&
        e->getArg(0)->getType()->isPointerType()) {
      if (getTarget().getTriple().getArch() == llvm::Triple::x86) {
        cgm.errorNYI(e->getSourceRange(), "setjmp on x86 MSVCRT");
        return RValue::get(nullptr);
      } else if (getTarget().getTriple().getArch() == llvm::Triple::aarch64) {
        cgm.errorNYI(e->getSourceRange(), "setjmp on aarch64 MSVCRT");
        return RValue::get(nullptr);
      }
      cgm.errorNYI(e->getSourceRange(), "setjmp on generic MSVCRT");
      return RValue::get(nullptr);
    }
    // On non-MSVCRT targets, _setjmp is handled as a normal function call.
    // Match OGCG which breaks here and falls through to the default path.
    break;
  }
  // C++ std:: builtins.
  case Builtin::BImove:
  case Builtin::BImove_if_noexcept:
  case Builtin::BIforward:
  case Builtin::BIforward_like:
  case Builtin::BIas_const:
  case Builtin::BIaddressof:
  case Builtin::BI__addressof:
    return RValue::get(emitLValue(e->getArg(0)).getPointer());
  case Builtin::BI__GetExceptionInfo:
  case Builtin::BI__fastfail:
  case Builtin::BIread_pipe:
  case Builtin::BIwrite_pipe:
  case Builtin::BIreserve_read_pipe:
  case Builtin::BIreserve_write_pipe:
  case Builtin::BIwork_group_reserve_read_pipe:
  case Builtin::BIwork_group_reserve_write_pipe:
  case Builtin::BIsub_group_reserve_read_pipe:
  case Builtin::BIsub_group_reserve_write_pipe:
  case Builtin::BIcommit_read_pipe:
  case Builtin::BIcommit_write_pipe:
  case Builtin::BIwork_group_commit_read_pipe:
  case Builtin::BIwork_group_commit_write_pipe:
  case Builtin::BIsub_group_commit_read_pipe:
  case Builtin::BIsub_group_commit_write_pipe:
  case Builtin::BIget_pipe_num_packets:
  case Builtin::BIget_pipe_max_packets:
  case Builtin::BIto_global:
  case Builtin::BIto_local:
  case Builtin::BIto_private:
  case Builtin::BIenqueue_kernel:
  case Builtin::BIget_kernel_work_group_size:
  case Builtin::BIget_kernel_preferred_work_group_size_multiple:
  case Builtin::BIget_kernel_max_sub_group_size_for_ndrange:
  case Builtin::BIget_kernel_sub_group_count_for_ndrange:
  case Builtin::BI__builtin_store_half:
  case Builtin::BI__builtin_store_halff:
  case Builtin::BI__builtin_load_half:
  case Builtin::BI__builtin_load_halff:
    return errorBuiltinNYI(*this, e, builtinID);
  case Builtin::BI__builtin_printf:
  case Builtin::BIprintf:
    assert(e->getNumArgs() >= 1);
    if (getTarget().getTriple().isNVPTX())
      return RValue::get(emitNVPTXDevicePrintfCallExpr(e));
    break;
  case Builtin::BI__builtin_canonicalize:
  case Builtin::BI__builtin_canonicalizef:
  case Builtin::BI__builtin_canonicalizef16:
  case Builtin::BI__builtin_canonicalizel:
    return emitUnaryFPBuiltin<cir::CanonicalizeOp>(*this, *e);
  case Builtin::BI__xray_customevent: {
    if (!cgm.getCodeGenOpts().XRayInstrumentFunctions)
      return RValue::getIgnored();

    if (!cgm.getCodeGenOpts().XRayInstrumentationBundle.has(
            XRayInstrKind::Custom))
      return RValue::getIgnored();

    if (const auto *xrayAttr = curFuncDecl->getAttr<XRayInstrumentAttr>()) {
      if (xrayAttr->neverXRayInstrument() &&
          !(cgm.getCodeGenOpts().XRayInstrumentFunctions &&
            (cgm.getCodeGenOpts().XRayAlwaysEmitCustomEvents ||
             cgm.getCodeGenOpts().XRayInstrumentationBundle.Mask ==
                 XRayInstrKind::Custom)))
        return RValue::getIgnored();
    }

    mlir::Location loc = getLoc(e->getSourceRange());
    // Arg0: const char* → cast to !cir.ptr<!void> for the intrinsic
    const Expr *arg0Expr = e->getArg(0);
    mlir::Value arg0Val;
    if (arg0Expr->getType()->isArrayType())
      arg0Val = emitArrayToPointerDecay(arg0Expr).getPointer();
    else
      arg0Val = emitScalarExpr(arg0Expr);
    auto voidPtrTy =
        cir::PointerType::get(cir::VoidType::get(builder.getContext()));
    if (arg0Val.getType() != voidPtrTy)
      arg0Val = builder.createCast(loc, cir::CastKind::bitcast, arg0Val,
                                   voidPtrTy);

    // Arg1: size_t → cast to i64 if needed
    mlir::Value arg1Val = emitScalarExpr(e->getArg(1));
    auto i64Ty = cir::IntType::get(builder.getContext(), 64, /*isSigned=*/false);
    if (arg1Val.getType() != i64Ty)
      arg1Val =
          builder.createCast(loc, cir::CastKind::integral, arg1Val, i64Ty);

    cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("xray.customevent"), mlir::Type{},
        mlir::ValueRange{arg0Val, arg1Val});
    return RValue::getIgnored();
  }

  case Builtin::BI__xray_typedevent: {
    if (!cgm.getCodeGenOpts().XRayInstrumentFunctions)
      return RValue::getIgnored();

    if (!cgm.getCodeGenOpts().XRayInstrumentationBundle.has(
            XRayInstrKind::Typed))
      return RValue::getIgnored();

    if (const auto *xrayAttr = curFuncDecl->getAttr<XRayInstrumentAttr>()) {
      if (xrayAttr->neverXRayInstrument() &&
          !(cgm.getCodeGenOpts().XRayInstrumentFunctions &&
            (cgm.getCodeGenOpts().XRayAlwaysEmitTypedEvents ||
             cgm.getCodeGenOpts().XRayInstrumentationBundle.Mask ==
                 XRayInstrKind::Typed)))
        return RValue::getIgnored();
    }

    mlir::Location loc = getLoc(e->getSourceRange());
    auto i64Ty = cir::IntType::get(builder.getContext(), 64, /*isSigned=*/false);

    // Arg0: size_t (event type) → cast to i64 if needed
    mlir::Value arg0Val = emitScalarExpr(e->getArg(0));
    if (arg0Val.getType() != i64Ty)
      arg0Val =
          builder.createCast(loc, cir::CastKind::integral, arg0Val, i64Ty);

    // Arg1: const char* → cast to !cir.ptr<!void> for the intrinsic
    const Expr *arg1Expr = e->getArg(1);
    mlir::Value arg1Val;
    if (arg1Expr->getType()->isArrayType())
      arg1Val = emitArrayToPointerDecay(arg1Expr).getPointer();
    else
      arg1Val = emitScalarExpr(arg1Expr);
    auto voidPtrTy =
        cir::PointerType::get(cir::VoidType::get(builder.getContext()));
    if (arg1Val.getType() != voidPtrTy)
      arg1Val = builder.createCast(loc, cir::CastKind::bitcast, arg1Val,
                                   voidPtrTy);

    // Arg2: size_t → cast to i64 if needed
    mlir::Value arg2Val = emitScalarExpr(e->getArg(2));
    if (arg2Val.getType() != i64Ty)
      arg2Val =
          builder.createCast(loc, cir::CastKind::integral, arg2Val, i64Ty);

    cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("xray.typedevent"), mlir::Type{},
        mlir::ValueRange{arg0Val, arg1Val, arg2Val});
    return RValue::getIgnored();
  }

  case Builtin::BI__arithmetic_fence: {
    // Match OGCG's __arithmetic_fence handling:
    // - only emit llvm.arithmetic.fence when reassociation is enabled and the
    //   target reports support;
    // - for complex values, apply the intrinsic separately to real/imag.
    cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(
        e->getFPFeaturesInEffect(getLangOpts()));
    bool isArithmeticFenceEnabled =
        bitEnumContainsAll(fmf, cir::FastMathFlags::reassoc) &&
        getContext().getTargetInfo().checkArithmeticFenceSupported();

    mlir::Location loc = getLoc(e->getSourceRange());
    QualType argTy = e->getArg(0)->getType();

    if (argTy->isComplexType()) {
      mlir::Value complexVal = emitComplexExpr(e->getArg(0));
      if (!isArithmeticFenceEnabled)
        return RValue::getComplex(complexVal);

      auto complexTy = mlir::cast<cir::ComplexType>(complexVal.getType());
      mlir::Type elementTy = complexTy.getElementType();
      mlir::Value real = builder.createComplexReal(loc, complexVal);
      mlir::Value imag = builder.createComplexImag(loc, complexVal);

      real = cir::LLVMIntrinsicCallOp::create(
                 builder, loc, cgm.getStringAttrCached("arithmetic.fence"),
                 elementTy, mlir::ValueRange{real}, fmf)
                 .getResult();
      imag = cir::LLVMIntrinsicCallOp::create(
                 builder, loc, cgm.getStringAttrCached("arithmetic.fence"),
                 elementTy, mlir::ValueRange{imag}, fmf)
                 .getResult();
      return RValue::getComplex(builder.createComplexCreate(loc, real, imag));
    }

    mlir::Value argVal = emitScalarExpr(e->getArg(0));
    if (!isArithmeticFenceEnabled)
      return RValue::get(argVal);

    return RValue::get(cir::LLVMIntrinsicCallOp::create(
                           builder, loc,
                           cgm.getStringAttrCached("arithmetic.fence"),
                           convertType(argTy), mlir::ValueRange{argVal}, fmf)
                           .getResult());
  }

  case Builtin::BI__builtin_os_log_format:
    return emitBuiltinOSLogFormat(*e);
  case Builtin::BI__builtin_get_device_side_mangled_name:
    return errorBuiltinNYI(*this, e, builtinID);
  case Builtin::BI__builtin_thread_pointer: {
    if (!getContext().getTargetInfo().isTLSSupported()) {
      cgm.errorUnsupported(e, "__builtin_thread_pointer");
      return RValue::get(nullptr);
    }

    mlir::Location loc = getLoc(e->getExprLoc());
    mlir::Value addr = cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("thread.pointer"),
        cgm.getDefaultGlobalsInt8PtrTy(), mlir::ValueRange{})
                           .getResult();

    auto addrPtrTy = mlir::cast<cir::PointerType>(addr.getType());
    auto resultPtrTy = cir::PointerType::get(
        builder.getContext(), cir::VoidType::get(builder.getContext()),
        addrPtrTy.getAddrSpace());
    return RValue::get(
        builder.createCast(loc, cir::CastKind::bitcast, addr, resultPtrTy));
  }
  case Builtin::BI__builtin_preserve_access_index:
    if (cgm.getCodeGenOpts().getDebugInfo() == llvm::codegenoptions::NoDebugInfo) {
      cgm.error(e->getExprLoc(),
                "using builtin_preserve_access_index() without -g");
      return RValue::get(emitScalarExpr(e->getArg(0)));
    }
    if (IsInPreservedAIRegion) {
      cgm.error(e->getExprLoc(),
                "nested builtin_preserve_access_index() not supported");
      return RValue::get(emitScalarExpr(e->getArg(0)));
    }

    IsInPreservedAIRegion = true;
    mlir::Value result = emitScalarExpr(e->getArg(0));
    IsInPreservedAIRegion = false;
    return RValue::get(result);
  }

  // If this is an alias for a lib function (e.g. __builtin_sin), emit
  // the call using the normal call path, but using the unmangled
  // version of the function name.
  if (getContext().BuiltinInfo.isLibFunction(builtinID) ||
      getContext().BuiltinInfo.isPredefinedLibFunction(builtinID))
    return emitLibraryCall(*this, fd, e,
                           getBuiltinLibcallCallee(cgm, fd, builtinID));

  // Some target-specific builtins can have aggregate return values, e.g.
  // __builtin_arm_mve_vld2q_u32. So if the result is an aggregate, force
  // returnValue to be non-null, so that the target-specific emission code can
  // always just emit into it.
  cir::TypeEvaluationKind evalKind = getEvaluationKind(e->getType());
  if (evalKind == cir::TEK_Aggregate && returnValue.isNull()) {
    Address destPtr =
        createMemTemp(e->getType(), getLoc(e->getSourceRange()), "agg.tmp");
    returnValue = ReturnValueSlot(destPtr);
  }

  // Now see if we can emit a target-specific builtin.
  // FIXME: This is a temporary mechanism (double-optional semantics) that will
  // go away once everything is implemented:
  //   1. return `mlir::Value{}` for cases where we have issued the diagnostic.
  //   2. return `std::nullopt` in cases where we didn't issue a diagnostic
  //      but also didn't handle the builtin.
  if (std::optional<mlir::Value> rst =
          emitTargetBuiltinExpr(builtinID, e, returnValue)) {
    mlir::Value v = rst.value();
    // CIR dialect operations may have no results, no values will be returned
    // even if it executes successfully.
    if (!v)
      return RValue::get(nullptr);

    switch (evalKind) {
    case cir::TEK_Scalar: {
      if (mlir::isa<cir::VoidType>(v.getType()))
        return RValue::get(nullptr);
      // Ensure the returned value type matches the builtin's declared return
      // type. Some builtins (e.g., NEON intrinsics) may return a value with
      // a different signedness than the declared return type, which causes
      // type mismatch errors when the result is stored.
      mlir::Type expectedTy = convertType(e->getType());
      if (v.getType() != expectedTy &&
          mlir::isa<cir::VectorType>(v.getType()) &&
          mlir::isa<cir::VectorType>(expectedTy)) {
        v = builder.createBitcast(v, expectedTy);
      }
      return RValue::get(v);
    }
    case cir::TEK_Aggregate:
      return RValue::getAggregate(returnValue.getValue());
    case cir::TEK_Complex:
      llvm_unreachable("No current target builtin returns complex");
    }
    llvm_unreachable("Bad evaluation kind in EmitBuiltinExpr");
  }

  // See if we have a target-specific intrinsic that maps directly to an LLVM
  // intrinsic. This is the generic fallback for target builtins that have a
  // corresponding LLVM intrinsic but no special CIR handling.
  {
    std::string name = getContext().BuiltinInfo.getName(builtinID);
    Intrinsic::ID intrinsicID = Intrinsic::not_intrinsic;
    StringRef prefix =
        llvm::Triple::getArchTypePrefix(getTarget().getTriple().getArch());
    if (!prefix.empty()) {
      intrinsicID = Intrinsic::getIntrinsicForClangBuiltin(prefix.data(), name);
      if (intrinsicID == Intrinsic::not_intrinsic)
        intrinsicID = Intrinsic::getIntrinsicForMSBuiltin(prefix.data(), name);
    }

    if (intrinsicID != Intrinsic::not_intrinsic) {
      unsigned iceArguments = 0;
      ASTContext::GetBuiltinTypeError error;
      getContext().GetBuiltinType(builtinID, error, &iceArguments);
      assert(error == ASTContext::GE_None && "Should not codegen an error");

      // Get the LLVM intrinsic name without the "llvm." prefix.
      StringRef baseName = Intrinsic::getBaseName(intrinsicID);
      assert(baseName.starts_with("llvm.") &&
             "Expected LLVM intrinsic name to start with 'llvm.'");
      StringRef intrinsicName = baseName.drop_front(5); // Drop "llvm."

      cir::FuncType intrinsicType =
          getIntrinsicType(&getMLIRContext(), intrinsicID);

      // A null type means the intrinsic signature uses an IIT type with no CIR
      // representation yet (e.g. the x86_amx type used by AMX .internal
      // intrinsics). Degrade to a clean NYI diagnostic rather than crashing.
      if (!intrinsicType)
        return errorBuiltinNYI(*this, e, builtinID);

      SmallVector<mlir::Value> args;
      for (unsigned i = 0, numArgs = e->getNumArgs(); i != numArgs; ++i) {
        mlir::Value argValue =
            emitScalarOrConstFoldImmArg(iceArguments, i, e->getArg(i));
        mlir::Type expectedTy = intrinsicType.getInput(i);

        if (isAMDGPUTextureType(getContext(), e->getArg(i)->getType()) &&
            isVec8I32Type(expectedTy)) {
          argValue = loadTextureDescPtrAsVec8I32(
              *this, argValue, getLoc(e->getArg(i)->getSourceRange()));
        }

        // If the intrinsic arg type is different from the builtin arg type
        // we need to do a bit cast.
        mlir::Type argType = argValue.getType();

        // AMX: a vector argument (e.g. v256i32) passed where the intrinsic
        // wants x86_amx is bridged with llvm.x86.cast.vector.to.tile, mirroring
        // classic CodeGen. The plain bitcast path below cannot represent this.
        if (auto opaqueTy = mlir::dyn_cast<cir::OpaqueType>(expectedTy);
            opaqueTy &&
            opaqueTy.getTag() == cir::OpaqueType::getX86AMXTag() &&
            argType != expectedTy) {
          argValue =
              cir::LLVMIntrinsicCallOp::create(
                  builder, getLoc(e->getArg(i)->getSourceRange()),
                  cgm.getStringAttrCached("x86.cast.vector.to.tile"), expectedTy,
                  mlir::ValueRange{argValue})
                  .getResult();
          args.push_back(argValue);
          continue;
        }

        // Use helper to get the correct integer type based on AST signedness
        mlir::Type correctedExpectedTy = getIntrinsicArgumentTypeFromAST(
            expectedTy, e, i, &getMLIRContext());

        if (argType != correctedExpectedTy) {
          argValue =
              getCorrectedIntrinsicValue(argValue, correctedExpectedTy, builder);
        }

        args.push_back(argValue);
      }

      mlir::Location loc = getLoc(e->getSourceRange());
      auto intrinsicCall = cir::LLVMIntrinsicCallOp::create(
          builder, loc, cgm.getStringAttrCached(intrinsicName),
          intrinsicType.getReturnType(), args);

      if (isa<cir::VoidType>(intrinsicType.getReturnType()))
        return RValue::get(nullptr);

      mlir::Value result = intrinsicCall.getResult();

      // The LLVM intrinsic return type may differ from the C-level return type
      // in signedness (e.g., LLVM i32 maps to !s32i but C returns unsigned int
      // = !u32i). Cast the result to match the expected C return type.
      mlir::Type expectedRetTy = convertType(e->getType());
      if (result.getType() != expectedRetTy) {
        // AMX: an x86_amx result returned into a vector-typed builtin (e.g.
        // v256i32) is bridged with llvm.x86.cast.tile.to.vector, mirroring
        // classic CodeGen. The plain bitcast path cannot represent this.
        if (auto opaqueTy =
                mlir::dyn_cast<cir::OpaqueType>(result.getType());
            opaqueTy &&
            opaqueTy.getTag() == cir::OpaqueType::getX86AMXTag())
          result =
              cir::LLVMIntrinsicCallOp::create(
                  builder, loc,
                  cgm.getStringAttrCached("x86.cast.tile.to.vector"),
                  expectedRetTy, mlir::ValueRange{result})
                  .getResult();
        else
          result = getCorrectedIntrinsicValue(result, expectedRetTy, builder);
      }

      return RValue::get(result);
    }
  }

  // Return a poison/placeholder value instead of crashing so that the
  // NYI diagnostic is properly reported without aborting.
  return errorBuiltinNYI(*this, e, builtinID);
}

static std::optional<mlir::Value>
emitBPFBuiltinExpr(CIRGenFunction *cgf, unsigned builtinID,
                   const CallExpr *e) {
  switch (builtinID) {
  case BPF::BI__builtin_btf_type_id:
  case BPF::BI__builtin_preserve_type_info: {
    if (cgf->cgm.getCodeGenOpts().getDebugInfo() ==
        llvm::codegenoptions::NoDebugInfo) {
      cgf->cgm.error(e->getExprLoc(), "using builtin function without -g");
      mlir::Location loc = cgf->getLoc(e->getSourceRange());
      mlir::Type resultTy = cgf->convertType(e->getType());
      return cgf->getBuilder().getConstant(loc, cir::PoisonAttr::get(resultTy));
    }

    static uint32_t builtinSeqNum;

    CIRGenBuilderTy &builder = cgf->getBuilder();
    mlir::Location loc = cgf->getLoc(e->getExprLoc());
    mlir::Value seqNum =
        builder.getConstInt(loc, builder.getSInt32Ty(), builtinSeqNum++);
    mlir::Value flag = cgf->emitScalarExpr(e->getArg(1));
    if (flag.getType() != builder.getSInt64Ty())
      flag = getCorrectedIntrinsicValue(flag, builder.getSInt64Ty(), builder);

    llvm::StringRef intrinsicName =
        builtinID == BPF::BI__builtin_btf_type_id ? "bpf.btf.type.id"
                                                  : "bpf.preserve.type.info";
    cir::LLVMIntrinsicCallOp intrinsicCall = cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgf->getCIRGenModule().getStringAttrCached(intrinsicName),
        cgf->convertType(e->getType()), mlir::ValueRange{seqNum, flag});
    setPreserveAccessIndexSourceTypeAttr(*cgf, intrinsicCall,
                                         e->getArg(0)->getType());
    return intrinsicCall.getResult();
  }
  default:
    return std::nullopt;
  }
}

static std::optional<mlir::Value>
emitTargetArchBuiltinExpr(CIRGenFunction *cgf, unsigned builtinID,
                          const CallExpr *e, ReturnValueSlot &returnValue,
                          llvm::Triple::ArchType arch) {
  // When compiling in HipStdPar mode we have to be conservative in rejecting
  // target specific features in the FE, and defer the possible error to the
  // AcceleratorCodeSelection pass, wherein iff an unsupported target builtin is
  // referenced by an accelerator executable function, we emit an error.
  // Returning nullptr here leads to the builtin being handled in
  // EmitStdParUnsupportedBuiltin.
  if (cgf->getLangOpts().HIPStdPar && cgf->getLangOpts().CUDAIsDevice &&
      arch != cgf->getTarget().getTriple().getArch())
    return std::nullopt;

  switch (arch) {
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    return cgf->emitARMBuiltinExpr(builtinID, e, returnValue, arch);
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_32:
  case llvm::Triple::aarch64_be:
    return cgf->emitAArch64BuiltinExpr(builtinID, e, returnValue, arch);
  case llvm::Triple::bpfeb:
  case llvm::Triple::bpfel:
    return emitBPFBuiltinExpr(cgf, builtinID, e);

  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    return cgf->emitX86BuiltinExpr(builtinID, e);

  case llvm::Triple::ppc:
  case llvm::Triple::ppcle:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
  case llvm::Triple::r600:
  case llvm::Triple::amdgcn:
  case llvm::Triple::systemz:
    // These are actually NYI, but that will be reported by emitBuiltinExpr.
    // At this point, we don't even know that the builtin is target-specific.
    return std::nullopt;
  case llvm::Triple::nvptx:
  case llvm::Triple::nvptx64: {
    mlir::Value result = cgf->emitNVPTXBuiltinExpr(builtinID, e);
    if (result)
      return result;
    return std::nullopt;
  }
  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64:
  case llvm::Triple::hexagon:
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
    // These are actually NYI, but that will be reported by emitBuiltinExpr.
    // At this point, we don't even know that the builtin is target-specific.
    return std::nullopt;
  default:
    return std::nullopt;
  }
}

std::optional<mlir::Value>
CIRGenFunction::emitTargetBuiltinExpr(unsigned builtinID, const CallExpr *e,
                                      ReturnValueSlot &returnValue) {
  if (std::optional<mlir::Value> rst =
          emitAMDGPUBuiltinExpr(this, builtinID, e))
    return rst;

  if (getContext().BuiltinInfo.isAuxBuiltinID(builtinID)) {
    assert(getContext().getAuxTargetInfo() && "Missing aux target info");
    return emitTargetArchBuiltinExpr(
        this, getContext().BuiltinInfo.getAuxBuiltinID(builtinID), e,
        returnValue, getContext().getAuxTargetInfo()->getTriple().getArch());
  }

  return emitTargetArchBuiltinExpr(this, builtinID, e, returnValue,
                                   getTarget().getTriple().getArch());
}

mlir::Value CIRGenFunction::emitScalarOrConstFoldImmArg(
    const unsigned iceArguments, const unsigned idx, const Expr *argExpr) {
  mlir::Value arg = {};
  if ((iceArguments & (1 << idx)) == 0) {
    arg = emitScalarExpr(argExpr);
  } else {
    // If this is required to be a constant, constant fold it so that we
    // know that the generated intrinsic gets a ConstantInt.
    const std::optional<llvm::APSInt> result =
        argExpr->getIntegerConstantExpr(getContext());
    assert(result && "Expected argument to be a constant");
    arg = builder.getConstInt(getLoc(argExpr->getSourceRange()), *result);
  }
  return arg;
}

/// Given a builtin id for a function like "__builtin_fabsf", return a Function*
/// for "fabsf".
cir::FuncOp CIRGenModule::getBuiltinLibFunction(const FunctionDecl *fd,
                                                unsigned builtinID) {
  assert(astContext.BuiltinInfo.isLibFunction(builtinID));

  // Get the name, skip over the __builtin_ prefix (if necessary). We may have
  // to build this up so provide a small stack buffer to handle the vast
  // majority of names.
  llvm::SmallString<64> name;
  GlobalDecl d(fd);

  // If the builtin has been declared explicitly with an assembler label,
  // use the mangled name. This differs from the plain label on platforms
  // that prefix labels. Matches OGCG CGBuiltin.cpp:226-227.
  if (fd->hasAttr<AsmLabelAttr>())
    name = getMangledName(d);
  else
    name = astContext.BuiltinInfo.getName(builtinID).substr(10);
  // Use the builtin's canonical type from the builtin table rather than
  // fd->getType(). This is critical when builtinID differs from fd's own
  // builtin ID (e.g. __builtin_char_memchr remapped to BI__builtin_memchr):
  // fd has char* params but BI__builtin_memchr has void* params. Using fd's
  // type would create @memchr with the wrong signature, and unlike LLVM IR's
  // opaque pointers, CIR's typed pointers make this a verification error.
  ASTContext::GetBuiltinTypeError error;
  QualType builtinTy = astContext.GetBuiltinType(builtinID, error);
  mlir::Type type = (error == ASTContext::GE_None)
                        ? convertType(builtinTy)
                        : convertType(fd->getType());
  return getOrCreateCIRFunction(name, type, d, /*forVTable=*/false);
}

/// Emit a UBSan check for -fsanitize=builtin. Used for __builtin_assume
/// (BCK_AssumePassedFalse=2), __builtin_ctz (BCK_CTZPassedZero=0), and
/// __builtin_clz (BCK_CLZPassedZero=1). Matches OGCG's EmitCheck with
/// SanitizerHandler::InvalidBuiltin.
static void emitBuiltinSanitizerCheck(CIRGenFunction &cgf,
                                      mlir::Value checkCondition,
                                      SourceLocation exprLoc,
                                      uint8_t checkKind) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(exprLoc);

  mlir::Value check = cgf.emitGuardedUBSanCheck(loc, checkCondition,
                                                 SanitizerKind::SO_Builtin);

  bool isTrap = cgf.cgm.getCodeGenOpts().SanitizeTrap.has(
      SanitizerKind::SO_Builtin);
  bool isRecover = !isTrap && cgf.cgm.getCodeGenOpts().SanitizeRecover.has(
                                  SanitizerKind::SO_Builtin);

  auto checkIf = cir::IfOp::create(
      builder, loc, check, /*withElseRegion=*/true,
      [&](mlir::OpBuilder &, mlir::Location ifLoc) {
        builder.createYield(ifLoc);
      },
      [&](mlir::OpBuilder &, mlir::Location ifLoc) {
        if (isTrap) {
          auto trap = cir::TrapOp::create(builder, ifLoc);
          // SanitizerHandler::InvalidBuiltin = 8
          trap->setAttr(
              "cir.ubsantrap_handler",
              mlir::IntegerAttr::get(
                  mlir::IntegerType::get(builder.getContext(), 8), 8));
          markNoSanitize(trap);
        } else {
          // Emit stub call to __cir_ubsan_invalid_builtin handler.
          // This stub takes the check kind as a (dead) argument, giving it
          // a distinct FuncType from the zero-arg __cir_ubsan_builtin_unreachable
          // stub; it must use its own symbol so the two declarations never
          // collide. It gets rewritten in LowerToLLVMIR to
          // __ubsan_handle_invalid_builtin[_abort].
          auto intPtrTy = mlir::cast<cir::IntType>(
              cgf.convertType(cgf.getContext().getUIntPtrType()));
          auto stubFnTy =
              cir::FuncType::get({intPtrTy}, builder.getVoidTy());
          auto stubFn = cgf.cgm.createRuntimeFunction(
              stubFnTy, "__cir_ubsan_invalid_builtin", /*attrs=*/{},
              /*isLocal=*/false, /*assumeConvergent=*/false);
          // Pass the check kind as an integer argument.
          mlir::Value kindVal =
              builder.getConstInt(ifLoc, intPtrTy, checkKind);
          auto call = builder.createCallOp(ifLoc, stubFn, {kindVal});
          setUBSanSourceLocationAttrs(cgf, call, exprLoc);
          call->setDiscardableAttr(
              "cir.ubsan.handler_kind",
              builder.getStringAttr(isRecover ? "recover" : "abort"));
          call->setDiscardableAttr(
              "cir.ubsan.handler_name",
              builder.getStringAttr("invalid_builtin"));
          call->setDiscardableAttr(
              "cir.ubsan.builtin_check_kind",
              builder.getI8IntegerAttr(checkKind));
          call->setDiscardableAttr("cir.nosanitize",
                                   mlir::UnitAttr::get(builder.getContext()));

          if (!isRecover) {
            auto unreachable = cir::UnreachableOp::create(builder, ifLoc);
            markNoSanitize(unreachable);
          } else {
            builder.createYield(ifLoc);
          }
        }
      });
  setSanitizerBranchAttrs(builder, checkIf);
}

mlir::Value CIRGenFunction::emitCheckedArgForAssume(const Expr *e) {
  mlir::Value argValue = evaluateExprAsBool(e);
  if (!sanOpts.has(SanitizerKind::Builtin))
    return argValue;

  // Emit UBSan check: if the assumption is false, call the handler.
  // BCK_AssumePassedFalse = 2
  emitBuiltinSanitizerCheck(*this, argValue, e->getExprLoc(),
                            /*checkKind=*/2);
  return argValue;
}

void CIRGenFunction::emitVAStart(mlir::Value vaList, mlir::Value count) {
  // LLVM codegen casts to *i8, no real gain on doing this for CIRGen this
  // early, defer to LLVM lowering.
  cir::VAStartOp::create(builder, vaList.getLoc(), vaList, count);
}

void CIRGenFunction::emitVAEnd(mlir::Value vaList) {
  cir::VAEndOp::create(builder, vaList.getLoc(), vaList);
}

// FIXME(cir): This completely abstracts away the ABI with a generic CIR Op. By
// default this lowers to llvm.va_arg which is incomplete and not ABI-compliant
// on most targets so cir.va_arg will need some ABI handling in LoweringPrepare
mlir::Value CIRGenFunction::emitVAArg(VAArgExpr *ve) {
  mlir::Location loc = cgm.getLoc(ve->getExprLoc());
  // OGCG dispatches to EmitMSVAListRef/EmitMSVAArg for MS ABI va_lists.
  // Mark the op so LoweringPrepare can use the Win64 pointer-bump pattern.
  mlir::Value vaList;
  bool isMSABI = ve->isMicrosoftABI();
  if (isMSABI)
    vaList = emitMSVAListRef(ve->getSubExpr()).getPointer();
  else
    vaList = emitVAListRef(ve->getSubExpr()).getPointer();
  QualType typeToEmit = ve->getType();
  if (typeToEmit->isVariablyModifiedType())
    emitVariablyModifiedType(typeToEmit);
  mlir::Type type = convertType(typeToEmit);
  // Compute the type's "unadjusted" alignment — the natural alignment before
  // #pragma pack adjustments. This is needed by AArch64 AAPCS va_arg lowering
  // (and potentially other targets) where packed/aligned struct attributes
  // cause the ABI alignment to differ from the data layout alignment.
  clang::CharUnits unadjustedAlign =
      getContext().getTypeUnadjustedAlignInChars(typeToEmit);
  auto alignAttr = builder.getI64IntegerAttr(unadjustedAlign.getQuantity());
  auto vaArgOp = cir::VAArgOp::create(builder, loc, type, vaList, alignAttr);
  // The x86-64 SysV ABI rounds the varargs overflow area up using the type's
  // full psABI alignment (OGCG X86_64ABIInfo::EmitVAArg uses
  // getTypeAlignInChars — see X86.cpp EmitX86_64VAArgFromMemory). The CIR data
  // layout can diverge from that value in BOTH directions: it *under*-reports a
  // C-level aligned(N) (struct-wide or member-level), which is modelled as a
  // padded record whose natural alignment stays at its members' base value; and
  // it *over*-reports a _BitInt(N>64), which is modelled as an i128-shaped
  // 16-byte type though its psABI alignment is 8. Thread the authoritative
  // psABI alignment through whenever it differs from the CIR data layout align
  // (which is what LoweringPrepare would otherwise use), so LoweringPrepare
  // rounds the overflow area exactly as classic clang does. When they agree the
  // attr is omitted and the common va_arg IR is left untouched.
  clang::CharUnits abiAlign = getContext().getTypeAlignInChars(typeToEmit);
  uint64_t cirNaturalAlign = cgm.getDataLayout().getABITypeAlign(type).value();
  if (static_cast<uint64_t>(abiAlign.getQuantity()) != cirNaturalAlign)
    vaArgOp->setAttr("cir.x86_overflow_align",
                     builder.getI64IntegerAttr(abiAlign.getQuantity()));
  if (auto tbaaStruct = cgm.getTBAAStructInfo(typeToEmit))
    vaArgOp->setAttr("cir.tbaa_struct", tbaaStruct);
  if (isMSABI)
    vaArgOp->setAttr("cir.ms_abi", builder.getUnitAttr());
  if (!isMSABI &&
      shouldIgnoreAArch64VAArg(getContext(), getTarget(), typeToEmit)) {
    // LoweringPrepare only sees CIR types; thread through OGCG's AArch64
    // va_arg ignore decision so Darwin empty records stay distinguishable from
    // zero-sized-but-not-empty wrappers like SortOfEmpty.
    vaArgOp->setAttr("cir.aarch64_va_abi_ignored", builder.getUnitAttr());
  }
  return vaArgOp;
}

//===----------------------------------------------------------------------===//
// counted_by / __builtin_dynamic_object_size support
//===----------------------------------------------------------------------===//

namespace {

/// StructFieldAccess is a simple visitor class to grab the first MemberExpr
/// from an Expr. It records any ArraySubscriptExpr we meet along the way.
class StructFieldAccess
    : public ConstStmtVisitor<StructFieldAccess, const Expr *> {
  bool AddrOfSeen = false;

public:
  const Expr *ArrayIndex = nullptr;
  QualType ArrayElementTy;

  const Expr *VisitMemberExpr(const MemberExpr *E) {
    if (AddrOfSeen && E->getType()->isArrayType())
      // Avoid forms like '&ptr->array'.
      return nullptr;
    return E;
  }

  const Expr *VisitArraySubscriptExpr(const ArraySubscriptExpr *E) {
    if (ArrayIndex)
      // We don't support multiple subscripts.
      return nullptr;

    AddrOfSeen = false; // '&ptr->array[idx]' is okay.
    ArrayIndex = E->getIdx();
    ArrayElementTy = E->getBase()->getType();
    return Visit(E->getBase());
  }
  const Expr *VisitCastExpr(const CastExpr *E) {
    if (E->getCastKind() == CK_LValueToRValue)
      return E;
    return Visit(E->getSubExpr());
  }
  const Expr *VisitParenExpr(const ParenExpr *E) {
    return Visit(E->getSubExpr());
  }
  const Expr *VisitUnaryAddrOf(const clang::UnaryOperator *E) {
    AddrOfSeen = true;
    return Visit(E->getSubExpr());
  }
  const Expr *VisitUnaryDeref(const clang::UnaryOperator *E) {
    AddrOfSeen = false;
    return Visit(E->getSubExpr());
  }
  const Expr *VisitBinaryOperator(const clang::BinaryOperator *Op) {
    return Op->isCommaOp() ? Visit(Op->getRHS()) : nullptr;
  }
};

} // end anonymous namespace

/// Find a struct's flexible array member. It may be embedded inside multiple
/// sub-structs, but must still be the last field.
static const FieldDecl *findFlexibleArrayMemberField(CIRGenFunction &cgf,
                                                     ASTContext &ctx,
                                                     const RecordDecl *rd) {
  const LangOptions::StrictFlexArraysLevelKind strictFlexArraysLevel =
      cgf.getLangOpts().getStrictFlexArraysLevel();

  if (rd->isImplicit())
    return nullptr;

  for (const FieldDecl *fd : rd->fields()) {
    if (Decl::isFlexibleArrayMemberLike(
            ctx, fd, fd->getType(), strictFlexArraysLevel,
            /*IgnoreTemplateOrMacroSubstitution=*/true))
      return fd;

    if (const auto *innerRd = fd->getType()->getAsRecordDecl())
      if (const FieldDecl *innerFd =
              findFlexibleArrayMemberField(cgf, ctx, innerRd))
        return innerFd;
  }

  return nullptr;
}

static mlir::Value getDefaultBuiltinObjectSizeResult(CIRGenBuilderTy &builder,
                                                     mlir::Location loc,
                                                     unsigned type,
                                                     cir::IntType resType) {
  return builder.getConstInt(loc, resType, (type & 2) ? 0 : -1);
}

/// Emit max(result, 0): clamp negative results to 0.
/// Uses signed comparison to detect negative values, matching OGCG's
/// CreateIsNotNeg behavior.
static mlir::Value emitPositiveResultOrZero(CIRGenFunction &cgf,
                                            mlir::Value res, mlir::Value index,
                                            cir::IntType resType,
                                            bool isSigned) {
  auto &builder = cgf.getBuilder();
  mlir::Location loc = res.getLoc();

  // Use a signed type for the comparison so we can detect negative values.
  // The result type may be unsigned (size_t), but if the count field is signed,
  // the sign-extended value can represent negative counts.
  auto signedType = cir::IntType::get(builder.getContext(),
                                      resType.getWidth(), /*isSigned=*/true);

  // cmp = (result >= 0) using signed comparison
  mlir::Value signedRes = builder.createIntCast(res, signedType);
  mlir::Value signedZero = builder.getConstInt(loc, signedType, 0);
  mlir::Value cmp =
      builder.createCompare(loc, cir::CmpOpKind::ge, signedRes, signedZero);
  if (index) {
    // cmp = (cmp && index >= 0)
    mlir::Value signedIndex = builder.createIntCast(index, signedType);
    cmp = builder.createLogicalAnd(
        loc, cmp,
        builder.createCompare(loc, cir::CmpOpKind::ge, signedIndex,
                              signedZero));
  }

  // return cmp ? result : 0
  mlir::Value zero = builder.getConstInt(loc, resType, 0);
  return builder.createSelect(loc, cmp, res, zero);
}

static std::pair<mlir::Value, mlir::Value>
getCountFieldAndIndex(CIRGenFunction &cgf, const MemberExpr *me,
                      const FieldDecl *arrayFd, const FieldDecl *countFd,
                      const Expr *idx, cir::IntType resType) {
  auto &builder = cgf.getBuilder();

  // count = ptr->count;
  mlir::Value count = cgf.emitLoadOfCountedByField(me, arrayFd, countFd);
  if (!count)
    return std::make_pair(mlir::Value{}, mlir::Value{});
  count = builder.createIntCast(count, resType);

  // index = ptr->index;
  mlir::Value index;
  if (idx) {
    bool idxSigned = idx->getType()->isSignedIntegerType();
    index = cgf.emitScalarExpr(idx);
    (void)idxSigned; // Signedness is handled by the cast to resType.
    index = builder.createIntCast(index, resType);
  }

  return std::make_pair(count, index);
}

mlir::Value CIRGenFunction::emitCountedByPointerSize(
    const ImplicitCastExpr *e, const Expr *idx, mlir::Value emittedE,
    QualType castedArrayElementTy, unsigned type, cir::IntType resType) {
  assert(e->getCastKind() == CK_LValueToRValue &&
         "must be an LValue to RValue cast");

  const MemberExpr *me =
      dyn_cast<MemberExpr>(e->getSubExpr()->IgnoreParenNoopCasts(getContext()));
  if (!me)
    return {};

  const auto *arrayBaseFd = dyn_cast<FieldDecl>(me->getMemberDecl());
  if (!arrayBaseFd || !arrayBaseFd->getType()->isPointerType() ||
      !arrayBaseFd->getType()->isCountAttributedType())
    return {};

  // Get the 'count' FieldDecl.
  const FieldDecl *countFd = arrayBaseFd->findCountedByField();
  if (!countFd)
    return {};

  mlir::Location loc = getLoc(e->getExprLoc());

  auto getElementBaseSize = [&](QualType elementTy) -> std::optional<CharUnits> {
    CharUnits elementSize =
        getContext().getTypeSizeInChars(elementTy->getPointeeType());

    if (elementSize.isZero()) {
      // This might be __sized_by (or __counted_by) on 'void *'.
      [[maybe_unused]] auto *cat = elementTy->getAs<CountAttributedType>();
      assert(cat && "must have a CountAttributedType");
      elementSize = CharUnits::One();
    }

    return std::optional<CharUnits>(elementSize);
  };

  std::optional<CharUnits> arrayElementBaseSize =
      getElementBaseSize(arrayBaseFd->getType());
  if (!arrayElementBaseSize)
    return {};

  std::optional<CharUnits> castedArrayElementBaseSize = arrayElementBaseSize;
  if (!castedArrayElementTy.isNull() &&
      castedArrayElementTy->isPointerType()) {
    castedArrayElementBaseSize = getElementBaseSize(castedArrayElementTy);
    if (!castedArrayElementBaseSize)
      return {};
  }

  bool isSigned = countFd->getType()->isSignedIntegerType();

  // count = ptr->count;
  // index = ptr->index;
  mlir::Value count, index;
  std::tie(count, index) = getCountFieldAndIndex(
      *this, me, arrayBaseFd, countFd, idx, resType);
  if (!count)
    return {};

  // array_element_size = sizeof (*ptr->array)
  auto arrayElementSize = builder.getConstInt(
      loc, resType, arrayElementBaseSize->getQuantity());

  // casted_array_element_size = sizeof (*((cast) ptr->array));
  auto castedArrayElementSize = builder.getConstInt(
      loc, resType, castedArrayElementBaseSize->getQuantity());

  // array_size = count * array_element_size;
  mlir::Value arraySize = builder.createMul(loc, count, arrayElementSize,
      isSigned ? cir::OverflowBehavior::NoSignedWrap
               : cir::OverflowBehavior::NoUnsignedWrap);

  // Option (1) 'ptr->array'
  // result = array_size
  mlir::Value result = arraySize;

  if (idx) { // Option (2) '&((cast) ptr->array)[idx]'
    // index_size = index * casted_array_element_size;
    mlir::Value indexSize = builder.createMul(loc, index, castedArrayElementSize,
        isSigned ? cir::OverflowBehavior::NoSignedWrap
                 : cir::OverflowBehavior::NoUnsignedWrap);

    // result = result - index_size;
    result = builder.createSub(loc, result, indexSize,
        isSigned ? cir::OverflowBehavior::NoSignedWrap
                 : cir::OverflowBehavior::NoUnsignedWrap);
  }

  return emitPositiveResultOrZero(*this, result, index, resType, isSigned);
}

mlir::Value CIRGenFunction::emitCountedByMemberSize(
    const MemberExpr *me, const Expr *idx, mlir::Value emittedE,
    QualType castedArrayElementTy, unsigned type, cir::IntType resType) {
  const auto *fd = dyn_cast<FieldDecl>(me->getMemberDecl());
  if (!fd)
    return {};

  ASTContext &ctx = getContext();
  const RecordDecl *rd = fd->getDeclContext()->getOuterLexicalRecordContext();
  const FieldDecl *flexibleArrayMemberFd = nullptr;

  if (Decl::isFlexibleArrayMemberLike(
          ctx, fd, fd->getType(), getLangOpts().getStrictFlexArraysLevel(),
          /*IgnoreTemplateOrMacroSubstitution=*/true))
    flexibleArrayMemberFd = fd;
  else
    flexibleArrayMemberFd = findFlexibleArrayMemberField(*this, ctx, rd);

  if (!flexibleArrayMemberFd ||
      !flexibleArrayMemberFd->getType()->isCountAttributedType())
    return {};

  const FieldDecl *countFd = flexibleArrayMemberFd->findCountedByField();
  if (!countFd)
    return {};

  mlir::Location loc = getLoc(me->getExprLoc());
  bool isSigned = countFd->getType()->isSignedIntegerType();

  QualType flexibleArrayMemberTy = flexibleArrayMemberFd->getType();
  int64_t charWidth = static_cast<int64_t>(getContext().getCharWidth());

  // field_offset = offsetof (struct s, field);
  mlir::Value fieldOffset;
  if (flexibleArrayMemberFd != fd) {
    const ASTRecordLayout &layout = ctx.getASTRecordLayout(rd);
    // Walk fields to find the offset of fd.
    uint32_t fieldNo = 0;
    int64_t offset = -1;
    for (const FieldDecl *field : rd->fields()) {
      if (field == fd) {
        offset = layout.getFieldOffset(fieldNo);
        break;
      }
      fieldNo++;
    }
    if (offset < 0)
      return {};
    fieldOffset = builder.getConstInt(loc, resType, offset / charWidth);
  }

  // count = ptr->count;
  // index = ptr->index;
  mlir::Value count, index;
  std::tie(count, index) = getCountFieldAndIndex(
      *this, me, flexibleArrayMemberFd, countFd, idx, resType);
  if (!count)
    return {};

  // flexible_array_member_element_size = sizeof (*ptr->array);
  const ArrayType *arrayTy = ctx.getAsArrayType(flexibleArrayMemberTy);
  CharUnits baseSize = ctx.getTypeSizeInChars(arrayTy->getElementType());
  auto flexibleArrayMemberElementSize =
      builder.getConstInt(loc, resType, baseSize.getQuantity());

  // flexible_array_member_size = count * flexible_array_member_element_size;
  mlir::Value flexibleArrayMemberSize = builder.createMul(
      loc, count, flexibleArrayMemberElementSize,
      isSigned ? cir::OverflowBehavior::NoSignedWrap
               : cir::OverflowBehavior::NoUnsignedWrap);

  mlir::Value result;
  if (flexibleArrayMemberFd == fd) {
    if (idx) { // Option (2) '&((cast) ptr->array)[idx]'
      mlir::Value castedFlexibleArrayMemberElementSize =
          flexibleArrayMemberElementSize;
      if (!castedArrayElementTy.isNull() &&
          castedArrayElementTy->isPointerType()) {
        CharUnits castedSize =
            ctx.getTypeSizeInChars(castedArrayElementTy->getPointeeType());
        castedFlexibleArrayMemberElementSize =
            builder.getConstInt(loc, resType, castedSize.getQuantity());
      }

      // index_size = index * casted_flexible_array_member_element_size;
      mlir::Value indexSize = builder.createMul(
          loc, index, castedFlexibleArrayMemberElementSize,
          isSigned ? cir::OverflowBehavior::NoSignedWrap
                   : cir::OverflowBehavior::NoUnsignedWrap);

      // result = flexible_array_member_size - index_size;
      result = builder.createSub(loc, flexibleArrayMemberSize, indexSize,
          isSigned ? cir::OverflowBehavior::NoSignedWrap
                   : cir::OverflowBehavior::NoUnsignedWrap);
    } else { // Option (1) 'ptr->array'
      // result = flexible_array_member_size;
      result = flexibleArrayMemberSize;
    }
  } else {
    // sizeof_struct = sizeof (struct s);
    CharUnits structSize = ctx.getTypeSizeInChars(ctx.getCanonicalTagType(rd));
    mlir::Value sizeofStruct =
        builder.getConstInt(loc, resType, structSize.getQuantity());

    if (idx) { // Option (4) '&((cast) ptr->field_array)[idx]'
      CharUnits castedBaseSize;
      if (!castedArrayElementTy.isNull() &&
          castedArrayElementTy->isPointerType()) {
        castedBaseSize =
            ctx.getTypeSizeInChars(castedArrayElementTy->getPointeeType());
      } else {
        const ArrayType *fieldArrayTy = ctx.getAsArrayType(fd->getType());
        castedBaseSize = ctx.getTypeSizeInChars(fieldArrayTy->getElementType());
      }

      mlir::Value castedFieldElementSize =
          builder.getConstInt(loc, resType, castedBaseSize.getQuantity());

      // field_offset += index * casted_field_element_size;
      mlir::Value mul = builder.createMul(loc, index, castedFieldElementSize,
          isSigned ? cir::OverflowBehavior::NoSignedWrap
                   : cir::OverflowBehavior::NoUnsignedWrap);
      fieldOffset = builder.createAdd(loc, fieldOffset, mul);
    }

    // Option (3) '&ptr->field', and Option (4) continuation.
    // offset_diff = sizeof_struct - field_offset;
    mlir::Value offsetDiff = builder.createSub(loc, sizeofStruct, fieldOffset,
        isSigned ? cir::OverflowBehavior::NoSignedWrap
                 : cir::OverflowBehavior::NoUnsignedWrap);

    // result = offset_diff + flexible_array_member_size;
    result = builder.createAdd(loc, flexibleArrayMemberSize, offsetDiff);
  }

  return emitPositiveResultOrZero(*this, result, index, resType, isSigned);
}

mlir::Value CIRGenFunction::emitCountedBySize(const Expr *e,
                                               mlir::Value emittedE,
                                               unsigned type,
                                               cir::IntType resType) {
  StructFieldAccess visitor;
  e = visitor.Visit(e);
  if (!e)
    return {};

  const Expr *idx = visitor.ArrayIndex;
  if (idx) {
    if (idx->HasSideEffects(getContext()))
      return getDefaultBuiltinObjectSizeResult(
          builder, getLoc(e->getExprLoc()), type, resType);

    if (const auto *il = dyn_cast<IntegerLiteral>(idx)) {
      int64_t val = il->getValue().getSExtValue();
      if (val < 0)
        return getDefaultBuiltinObjectSizeResult(
            builder, getLoc(e->getExprLoc()), type, resType);

      // The index is 0, so we don't need to take it into account.
      if (val == 0)
        idx = nullptr;
    }
  }

  // __counted_by on either a flexible array member or a pointer into a struct
  // with a flexible array member.
  if (const auto *me = dyn_cast<MemberExpr>(e))
    return emitCountedByMemberSize(me, idx, emittedE, visitor.ArrayElementTy,
                                   type, resType);

  // __counted_by on a pointer in a struct.
  if (const auto *ice = dyn_cast<ImplicitCastExpr>(e);
      ice && ice->getCastKind() == CK_LValueToRValue)
    return emitCountedByPointerSize(ice, idx, emittedE, visitor.ArrayElementTy,
                                    type, resType);

  return {};
}

mlir::Value CIRGenFunction::emitBuiltinObjectSize(const Expr *e, unsigned type,
                                                  cir::IntType resType,
                                                  mlir::Value emittedE,
                                                  bool isDynamic) {
  // We need to reference an argument if the pointer is a parameter with the
  // pass_object_size attribute.
  if (auto *d = dyn_cast<DeclRefExpr>(e->IgnoreParenImpCasts())) {
    auto *param = dyn_cast<ParmVarDecl>(d->getDecl());
    auto *ps = d->getDecl()->getAttr<PassObjectSizeAttr>();
    if (param && ps) {
      auto from = ps->getType();
      // areBOSTypesCompatible: exact match, or (0→1), or (3→2).
      bool compatible =
          from == static_cast<int>(type) ||
          (from == 0 && type == 1) || (from == 3 && type == 2);
      if (compatible) {
        auto iter = sizeArguments.find(param);
        assert(iter != sizeArguments.end());

        const ImplicitParamDecl *implicitParam = iter->second;
        auto dIter = localDeclMap.find(implicitParam);
        assert(dIter != localDeclMap.end());

        return emitLoadOfScalar(
            makeAddrLValue(dIter->second, getContext().getSizeType()),
            e->getBeginLoc());
      }
    }
  }

  // LLVM can't handle type=3 appropriately, and __builtin_object_size shouldn't
  // evaluate e for side-effects. In either case, just like original LLVM
  // lowering, we shouldn't lower to `cir.objsize` but to a constant instead.
  if (type == 3 || (!emittedE && e->HasSideEffects(getContext())))
    return builder.getConstInt(getLoc(e->getSourceRange()), resType,
                               (type & 2) ? 0 : -1);

  mlir::Value ptr = emittedE ? emittedE : emitScalarExpr(e);
  assert(mlir::isa<cir::PointerType>(ptr.getType()) &&
         "Non-pointer passed to __builtin_object_size?");

  if (isDynamic) {
    // Emit special code for a flexible array member with the "counted_by"
    // attribute.
    if (mlir::Value v = emitCountedBySize(e, ptr, type, resType))
      return v;
  }

  // Extract the min/max mode from type. CIR only supports type 0
  // (max, whole object) and type 2 (min, whole object), not type 1 or 3
  // (closest subobject variants).
  const bool min = ((type & 2) != 0);
  // For GCC compatibility, __builtin_object_size treats NULL as unknown size.
  auto op =
      cir::ObjSizeOp::create(builder, getLoc(e->getSourceRange()), resType, ptr,
                             min, /*nullUnknown=*/true, isDynamic);
  return op.getResult();
}

mlir::Value CIRGenFunction::evaluateOrEmitBuiltinObjectSize(
    const Expr *e, unsigned type, cir::IntType resType, mlir::Value emittedE,
    bool isDynamic) {
  uint64_t objectSize;
  if (!e->tryEvaluateObjectSize(objectSize, getContext(), type))
    return emitBuiltinObjectSize(e, type, resType, emittedE, isDynamic);
  return builder.getConstInt(getLoc(e->getSourceRange()), resType, objectSize);
}

mlir::Value CIRGenFunction::emitMSVCBuiltinExpr(MSVCIntrin builtinID,
                                                 const CallExpr *e) {
  auto emitAtomicXchgWithOrdering =
      [&](cir::MemOrder ordering) -> mlir::Value {
    QualType type = e->getType();
    QualType ptrType = e->getArg(0)->getType();
    Address destAddr = checkAtomicAlignment(*this, e);

    mlir::Value val = emitScalarExpr(e->getArg(1));
    mlir::Type valueType = val.getType();
    mlir::Value destValue = destAddr.emitRawPointer();

    if (ptrType->getPointeeType()->isPointerType()) {
      cir::IntType ptrSizeInt =
          builder.getSIntNTy(getContext().getTypeSize(type));
      destValue =
          builder.createBitcast(destValue, builder.getPointerTo(ptrSizeInt));
      val = emitToInt(*this, val, type, ptrSizeInt);
    } else {
      cir::IntType intType =
          ptrType->getPointeeType()->isUnsignedIntegerType()
              ? builder.getUIntNTy(getContext().getTypeSize(type))
              : builder.getSIntNTy(getContext().getTypeSize(type));
      val = emitToInt(*this, val, type, intType);
    }

    auto xchg = cir::AtomicXchgOp::create(
        builder, getLoc(e->getSourceRange()), destValue, val, ordering,
        builder.getI64IntegerAttr(destAddr.getAlignment().getAsAlign().value()),
        /*is_volatile=*/false, /*sync_scope=*/nullptr);
    return emitFromInt(*this, xchg->getResult(0), type, valueType);
  };

  switch (builtinID) {
  case MSVCIntrin::_BitScanForward:
  case MSVCIntrin::_BitScanReverse: {
    mlir::Location loc = getLoc(e->getSourceRange());
    Address indexAddr(emitPointerWithAlignment(e->getArg(0)));
    mlir::Value argValue = emitScalarExpr(e->getArg(1));

    mlir::Type argType = argValue.getType();
    mlir::Type indexType = convertType(
        e->getArg(0)->getType()->getPointeeType());
    mlir::Type resultType = convertType(e->getType());

    // Ensure the argument is unsigned for BitCtzOp/BitClzOp.
    auto argIntTy = mlir::cast<cir::IntType>(argType);
    if (argIntTy.isSigned()) {
      argIntTy = builder.getUIntNTy(argIntTy.getWidth());
      argValue = builder.createIntCast(argValue, argIntTy);
    }

    mlir::Value zero = builder.getNullValue(argType, loc);
    mlir::Value isNotZero =
        builder.createCompare(loc, cir::CmpOpKind::ne, argValue, zero);

    cir::IfOp::create(
        builder, loc, isNotZero, /*withElseRegion=*/false,
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          // Match OGCG by only computing/storing the output index in the
          // nonzero case and by using poisonZero=true on the underlying count
          // intrinsic.
          mlir::Value bitPos;
          if (builtinID == MSVCIntrin::_BitScanForward) {
            bitPos = cir::BitCtzOp::create(builder, ifLoc, argValue,
                                           /*poisonZero=*/true)
                         .getResult();
          } else {
            mlir::Value clzResult =
                cir::BitClzOp::create(builder, ifLoc, argValue,
                                      /*poisonZero=*/true)
                    .getResult();
            unsigned argWidth = argIntTy.getWidth();
            if (clzResult.getType() != indexType)
              clzResult = builder.createIntCast(clzResult, indexType);
            auto indexIntTy = mlir::cast<cir::IntType>(indexType);
            mlir::Value lastIndex = builder.getConstant(
                ifLoc, cir::IntAttr::get(indexIntTy, argWidth - 1));
            bitPos = builder.createNSWSub(ifLoc, lastIndex, clzResult);
          }

          // Cast to index type if needed (e.g., 64-bit cttz result to 32-bit
          // index).
          if (bitPos.getType() != indexType)
            bitPos = builder.createIntCast(bitPos, indexType);

          builder.CIRBaseBuilderTy::createStore(ifLoc, bitPos,
                                                indexAddr.emitRawPointer());
          builder.createYield(ifLoc);
        });

    // Return nonzero if a set bit was found (mask != 0), zero otherwise.
    return builder.createBoolToInt(isNotZero, resultType);
  }

  case MSVCIntrin::_InterlockedAnd:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::And, e, nullptr);
  case MSVCIntrin::_InterlockedExchange:
    return emitAtomicXchgWithOrdering(cir::MemOrder::SequentiallyConsistent);
  case MSVCIntrin::_InterlockedExchangeAdd:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::Add, e, nullptr);
  case MSVCIntrin::_InterlockedExchangeSub:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::Sub, e, nullptr);
  case MSVCIntrin::_InterlockedOr:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::Or, e, nullptr);
  case MSVCIntrin::_InterlockedXor:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::Xor, e, nullptr);

  case MSVCIntrin::_InterlockedExchangeAdd_acq:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::Add, e, nullptr,
                                 nullptr, cir::MemOrder::Acquire);
  case MSVCIntrin::_InterlockedExchangeAdd_rel:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::Add, e, nullptr,
                                 nullptr, cir::MemOrder::Release);
  case MSVCIntrin::_InterlockedExchangeAdd_nf:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::Add, e, nullptr,
                                 nullptr, cir::MemOrder::Relaxed);
  case MSVCIntrin::_InterlockedExchange_acq:
    return emitAtomicXchgWithOrdering(cir::MemOrder::Acquire);
  case MSVCIntrin::_InterlockedExchange_rel:
    return emitAtomicXchgWithOrdering(cir::MemOrder::Release);
  case MSVCIntrin::_InterlockedExchange_nf:
    return emitAtomicXchgWithOrdering(cir::MemOrder::Relaxed);

  case MSVCIntrin::_InterlockedCompareExchange:
    return emitAtomicCmpXchgForMSIntrin(*this, e);
  case MSVCIntrin::_InterlockedCompareExchange_acq:
    return emitAtomicCmpXchgForMSIntrin(*this, e, cir::MemOrder::Acquire);
  case MSVCIntrin::_InterlockedCompareExchange_rel:
    return emitAtomicCmpXchgForMSIntrin(*this, e, cir::MemOrder::Release);
  case MSVCIntrin::_InterlockedCompareExchange_nf:
    return emitAtomicCmpXchgForMSIntrin(*this, e, cir::MemOrder::Relaxed);

  case MSVCIntrin::_InterlockedCompareExchange128:
    return emitAtomicCmpXchg128ForMSIntrin(
        *this, e, cir::MemOrder::SequentiallyConsistent);
  case MSVCIntrin::_InterlockedCompareExchange128_acq:
    return emitAtomicCmpXchg128ForMSIntrin(*this, e, cir::MemOrder::Acquire);
  case MSVCIntrin::_InterlockedCompareExchange128_rel:
    return emitAtomicCmpXchg128ForMSIntrin(*this, e, cir::MemOrder::Release);
  case MSVCIntrin::_InterlockedCompareExchange128_nf:
    return emitAtomicCmpXchg128ForMSIntrin(*this, e, cir::MemOrder::Relaxed);

  case MSVCIntrin::_InterlockedOr_acq:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::Or, e, nullptr,
                                 nullptr, cir::MemOrder::Acquire);
  case MSVCIntrin::_InterlockedOr_rel:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::Or, e, nullptr,
                                 nullptr, cir::MemOrder::Release);
  case MSVCIntrin::_InterlockedOr_nf:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::Or, e, nullptr,
                                 nullptr, cir::MemOrder::Relaxed);
  case MSVCIntrin::_InterlockedXor_acq:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::Xor, e, nullptr,
                                 nullptr, cir::MemOrder::Acquire);
  case MSVCIntrin::_InterlockedXor_rel:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::Xor, e, nullptr,
                                 nullptr, cir::MemOrder::Release);
  case MSVCIntrin::_InterlockedXor_nf:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::Xor, e, nullptr,
                                 nullptr, cir::MemOrder::Relaxed);
  case MSVCIntrin::_InterlockedAnd_acq:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::And, e, nullptr,
                                 nullptr, cir::MemOrder::Acquire);
  case MSVCIntrin::_InterlockedAnd_rel:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::And, e, nullptr,
                                 nullptr, cir::MemOrder::Release);
  case MSVCIntrin::_InterlockedAnd_nf:
    return makeBinaryAtomicValue(*this, cir::AtomicFetchKind::And, e, nullptr,
                                 nullptr, cir::MemOrder::Relaxed);

  case MSVCIntrin::_InterlockedIncrement_acq:
  case MSVCIntrin::_InterlockedIncrement_rel:
  case MSVCIntrin::_InterlockedIncrement_nf:
  case MSVCIntrin::_InterlockedDecrement_acq:
  case MSVCIntrin::_InterlockedDecrement_rel:
  case MSVCIntrin::_InterlockedDecrement_nf:
  case MSVCIntrin::_InterlockedDecrement:
  case MSVCIntrin::_InterlockedIncrement: {
    bool isIncrement =
        builtinID == MSVCIntrin::_InterlockedIncrement ||
        builtinID == MSVCIntrin::_InterlockedIncrement_acq ||
        builtinID == MSVCIntrin::_InterlockedIncrement_rel ||
        builtinID == MSVCIntrin::_InterlockedIncrement_nf;

    cir::MemOrder ordering;
    switch (builtinID) {
    case MSVCIntrin::_InterlockedIncrement_acq:
    case MSVCIntrin::_InterlockedDecrement_acq:
      ordering = cir::MemOrder::Acquire;
      break;
    case MSVCIntrin::_InterlockedIncrement_rel:
    case MSVCIntrin::_InterlockedDecrement_rel:
      ordering = cir::MemOrder::Release;
      break;
    case MSVCIntrin::_InterlockedIncrement_nf:
    case MSVCIntrin::_InterlockedDecrement_nf:
      ordering = cir::MemOrder::Relaxed;
      break;
    default:
      ordering = cir::MemOrder::SequentiallyConsistent;
      break;
    }

    mlir::Location loc = getLoc(e->getSourceRange());
    mlir::Type intTy = convertType(e->getType());
    Address destAddr = checkAtomicAlignment(*this, e);
    mlir::Value destValue = destAddr.emitRawPointer();
    cir::IntAttr oneAttr;
    if (intTy == builder.getSInt16Ty())
      oneAttr = builder.getSInt16AttrLocalCached(1);
    else if (intTy == builder.getSInt32Ty())
      oneAttr = builder.getSInt32AttrLocalCached(1);
    else if (intTy == builder.getSInt64Ty())
      oneAttr = builder.getSInt64AttrLocalCached(1);
    else if (intTy == builder.getUInt16Ty())
      oneAttr = builder.getUInt16AttrLocalCached(1);
    else if (intTy == builder.getUInt32Ty())
      oneAttr = builder.getUInt32AttrLocalCached(1);
    else if (intTy == builder.getUInt64Ty())
      oneAttr = builder.getUInt64AttrLocalCached(1);
    else
      oneAttr = cir::IntAttr::get(intTy, 1);
    mlir::Value one = builder.getConstant(loc, oneAttr);

    auto oldVal = cir::AtomicFetchOp::create(
        builder, loc, destValue, one,
        isIncrement ? cir::AtomicFetchKind::Add : cir::AtomicFetchKind::Sub,
        ordering,
        builder.getI64IntegerAttr(destAddr.getAlignment().getAsAlign().value()),
        false, /* is volatile */
        true,  /* fetch first */
        /*sync_scope=*/nullptr);

    return builder.createBinop(
        loc, oldVal->getResult(0),
        isIncrement ? cir::BinOpKind::Add : cir::BinOpKind::Sub, one);
  }

  case MSVCIntrin::__fastfail:
    llvm_unreachable("MSVCIntrin::__fastfail NYI");
  }
  llvm_unreachable("Incorrect MSVC intrinsic!");
}

/// Map a Darwin triple to the base MachO platform ID used by the runtime's
/// __isPlatformVersionAtLeast. Mirrors getBaseMachOPlatformID in CGObjC.cpp.
static unsigned getBaseMachOPlatformID(const llvm::Triple &tt) {
  switch (tt.getOS()) {
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
    return llvm::MachO::PLATFORM_MACOS;
  case llvm::Triple::IOS:
    return llvm::MachO::PLATFORM_IOS;
  case llvm::Triple::TvOS:
    return llvm::MachO::PLATFORM_TVOS;
  case llvm::Triple::WatchOS:
    return llvm::MachO::PLATFORM_WATCHOS;
  case llvm::Triple::XROS:
    return llvm::MachO::PLATFORM_XROS;
  case llvm::Triple::DriverKit:
    return llvm::MachO::PLATFORM_DRIVERKIT;
  default:
    return llvm::MachO::PLATFORM_UNKNOWN;
  }
}

mlir::Value
CIRGenFunction::emitBuiltinAvailable(mlir::Location loc,
                                     const VersionTuple &version) {
  assert(!version.empty() && "unexpected empty version");
  auto int32Ty = builder.getSInt32Ty();
  auto zeroVal = builder.getConstInt(loc, int32Ty, 0);
  std::optional<unsigned> minor = version.getMinor();
  std::optional<unsigned> subMinor = version.getSubminor();

  mlir::Value callRes;

  if (cgm.getTarget().getTriple().isOSDarwin()) {
    // Darwin uses __isPlatformVersionAtLeast(platform, major, minor, subminor).
    auto fnTy = cir::FuncType::get(
        {int32Ty, int32Ty, int32Ty, int32Ty}, int32Ty, /*isVarArg=*/false);
    cir::FuncOp fn =
        cgm.createRuntimeFunction(fnTy, "__isPlatformVersionAtLeast");

    mlir::Value args[] = {
        builder.getConstInt(
            loc, int32Ty,
            getBaseMachOPlatformID(cgm.getTarget().getTriple())),
        builder.getConstInt(loc, int32Ty, version.getMajor()),
        builder.getConstInt(loc, int32Ty, minor.value_or(0)),
        builder.getConstInt(loc, int32Ty, subMinor.value_or(0)),
    };
    callRes = emitNounwindRuntimeCall(loc, fn, args);
  } else {
    // Non-Darwin uses __isOSVersionAtLeast(major, minor, subminor).
    auto fnTy = cir::FuncType::get(
        {int32Ty, int32Ty, int32Ty}, int32Ty, /*isVarArg=*/false);
    cir::FuncOp fn =
        cgm.createRuntimeFunction(fnTy, "__isOSVersionAtLeast");

    mlir::Value args[] = {
        builder.getConstInt(loc, int32Ty, version.getMajor()),
        builder.getConstInt(loc, int32Ty, minor.value_or(0)),
        builder.getConstInt(loc, int32Ty, subMinor.value_or(0)),
    };
    callRes = emitNounwindRuntimeCall(loc, fn, args);
  }

  // Return callRes != 0 as a CIR bool.
  return cir::CmpOp::create(builder, loc, cir::CmpOpKind::ne, callRes,
                             zeroVal);
}
