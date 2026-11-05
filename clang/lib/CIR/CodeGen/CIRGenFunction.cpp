//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Internal per-function state used for AST-to-ClangIR code gen
//
//===----------------------------------------------------------------------===//

#include "CIRGenFunction.h"

#include "CIRGenCUDARuntime.h"
#include "clang/AST/ASTLambda.h"
#include "CIRGenCXXABI.h"
#include "CIRGenCall.h"
#include "CIRGenConstantEmitter.h"
#include "CIRGenSanitizer.h"
#include "CIRGenValue.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/AST/IgnoreExpr.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/StmtObjC.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/SaveAndRestore.h"

#include <cassert>

namespace clang {
extern llvm::cl::opt<bool> ClSanitizeGuardChecks;
}

namespace clang::CIRGen {

namespace {
constexpr llvm::StringLiteral kDILocationAtomGroupKey = "llvm.di_atom_group";
constexpr llvm::StringLiteral kDILocationAtomRankKey = "llvm.di_atom_rank";
constexpr llvm::StringLiteral kDILocationScopeLineKey = "llvm.di_scope_line";
constexpr llvm::StringLiteral kDILocationScopeColumnKey = "llvm.di_scope_column";
// Flattened innermost-first list of (line, column) for every enclosing lexical
// scope, so the lowering can build a properly nested DILexicalBlock chain
// (otherwise variables from enclosing blocks are invisible in nested blocks).
constexpr llvm::StringLiteral kDILocationScopeChainKey = "llvm.di_scope_chain";
constexpr uint8_t kMissingReturnTrapHandlerID = 11; // SanitizerHandler::MissingReturn
constexpr uint8_t kVLABoundTrapHandlerID = 24; // SanitizerHandler::VLABoundNotPositive
constexpr llvm::StringLiteral kPreserveDeadUBSanUnreachableBlockAttr =
    "cir.preserve_dead_ubsan_unreachable";

class CheckOpenMPTargetEscapedDecls final
    : public ConstStmtVisitor<CheckOpenMPTargetEscapedDecls> {
  llvm::SetVector<const VarDecl *> escapedDecls;
  bool allEscaped = false;

  void markAsEscaped(const ValueDecl *vd) {
    const auto *var = dyn_cast_or_null<VarDecl>(vd);
    if (!var || isa<ParmVarDecl>(var) || !var->hasLocalStorage() ||
        var->isStaticLocal() || var->hasAttr<OMPAllocateDeclAttr>() ||
        OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(var))
      return;
    escapedDecls.insert(var->getCanonicalDecl());
  }

public:
  explicit CheckOpenMPTargetEscapedDecls(CIRGenFunction &) {}

  void VisitValueDecl(const ValueDecl *vd) {
    if (const auto *var = dyn_cast<VarDecl>(vd);
        var && !isa<ParmVarDecl>(var) && var->hasInit()) {
      llvm::SaveAndRestore escapedScope(allEscaped,
                                        vd->getType()->isLValueReferenceType());
      Visit(var->getInit());
    }
  }

  void VisitCapturedStmt(const CapturedStmt *s) {
    if (!s)
      return;
    for (const CapturedStmt::Capture &capture : s->captures()) {
      if (!capture.capturesVariable() || capture.capturesVariableByCopy())
        continue;
      const ValueDecl *vd = capture.getCapturedVar();
      markAsEscaped(vd);
      if (isa<OMPCapturedExprDecl>(vd))
        VisitValueDecl(vd);
      else if (const auto *var = dyn_cast<VarDecl>(vd); var && var->isInitCapture())
        VisitValueDecl(vd);
    }
    Visit(s->getCapturedStmt());
  }

  void VisitLambdaExpr(const LambdaExpr *e) {
    if (!e)
      return;
    for (const LambdaCapture &capture : e->captures()) {
      if (!capture.capturesVariable() || capture.getCaptureKind() != LCK_ByRef)
        continue;
      const ValueDecl *vd = capture.getCapturedVar();
      markAsEscaped(vd);
      if (e->isInitCapture(&capture) || isa<OMPCapturedExprDecl>(vd))
        VisitValueDecl(vd);
    }
    Visit(e->getBody());
  }

  void VisitBlockExpr(const BlockExpr *e) {
    if (!e)
      return;
    for (const BlockDecl::Capture &capture : e->getBlockDecl()->captures()) {
      if (!capture.isByRef())
        continue;
      const VarDecl *vd = capture.getVariable();
      markAsEscaped(vd);
      if (isa<OMPCapturedExprDecl>(vd) || vd->isInitCapture())
        VisitValueDecl(vd);
    }
    Visit(e->getBlockDecl()->getBody());
  }

  void VisitCallExpr(const CallExpr *e) {
    if (!e)
      return;
    for (const Expr *arg : e->arguments()) {
      if (!arg)
        continue;
      if (arg->isLValue()) {
        llvm::SaveAndRestore escapedScope(allEscaped, true);
        Visit(arg);
      } else {
        Visit(arg);
      }
    }
    Visit(e->getCallee());
  }

  void VisitDeclRefExpr(const DeclRefExpr *e) {
    if (!e)
      return;
    if (allEscaped)
      markAsEscaped(e->getDecl());
    if (isa<OMPCapturedExprDecl>(e->getDecl()))
      VisitValueDecl(e->getDecl());
    else if (const auto *var = dyn_cast<VarDecl>(e->getDecl()); var &&
             var->isInitCapture())
      VisitValueDecl(e->getDecl());
  }

  void VisitUnaryOperator(const UnaryOperator *e) {
    if (!e)
      return;
    if (e->getOpcode() == UO_AddrOf) {
      llvm::SaveAndRestore escapedScope(allEscaped, true);
      Visit(e->getSubExpr());
      return;
    }
    Visit(e->getSubExpr());
  }

  void VisitImplicitCastExpr(const ImplicitCastExpr *e) {
    if (!e)
      return;
    if (e->getCastKind() == CK_ArrayToPointerDecay) {
      llvm::SaveAndRestore escapedScope(allEscaped, true);
      Visit(e->getSubExpr());
      return;
    }
    Visit(e->getSubExpr());
  }

  void VisitExpr(const Expr *e) {
    if (!e)
      return;
    llvm::SaveAndRestore escapedScope(allEscaped,
                                      allEscaped && e->isLValue());
    for (const Stmt *child : e->children())
      if (child)
        Visit(child);
  }

  void VisitStmt(const Stmt *s) {
    if (!s)
      return;
    for (const Stmt *child : s->children())
      if (child)
        Visit(child);
  }

  llvm::SmallPtrSet<const VarDecl *, 8> takeEscapedDecls() && {
    llvm::SmallPtrSet<const VarDecl *, 8> result;
    for (const VarDecl *vd : escapedDecls)
      result.insert(vd);
    return result;
  }
};

static llvm::SmallPtrSet<const VarDecl *, 8>
collectOpenMPTargetEscapedDecls(CIRGenFunction &cgf, const Stmt *body) {
  CheckOpenMPTargetEscapedDecls visitor(cgf);
  visitor.Visit(body);
  return std::move(visitor).takeEscapedDecls();
}

static mlir::Value emitSanitizerValueHandle(CIRGenFunction &cgf,
                                            mlir::Value value, QualType ty,
                                            mlir::Location loc) {
  auto &builder = cgf.getBuilder();
  auto intptrTy =
      mlir::cast<cir::IntType>(cgf.convertType(cgf.getContext().getUIntPtrType()));
  if (value.getType() == intptrTy)
    return value;

  auto intTy = mlir::dyn_cast<cir::IntType>(value.getType());
  assert(intTy && "VLA-bound sanitizer expects integer operands");

  // Match OGCG EmitCheckValue: integers fitting in intptr_t are zero-extended,
  // wider values are passed indirectly via a temporary address.
  if (intTy.getWidth() <= intptrTy.getWidth()) {
    if (intTy.isSigned())
      value = builder.createCast(cir::CastKind::integral, value,
                                 builder.getUIntNTy(intTy.getWidth()));
    if (auto *castOp = value.getDefiningOp())
      markNoSanitize(castOp);
    auto castToIntPtr = builder.createIntCast(value, intptrTy);
    if (auto *castOp = castToIntPtr.getDefiningOp())
      markNoSanitize(castOp);
    return castToIntPtr;
  }

  Address temp = cgf.createMemTemp(ty, loc, "ubsan.vla.arg");
  auto store = builder.createStore(loc, value, temp);
  markNoSanitize(store);
  auto ptrAsInt = builder.createPtrToInt(temp.getPointer(), intptrTy);
  markNoSanitize(ptrAsInt.getDefiningOp());
  return ptrAsInt;
}

static bool matchesStlAllocatorFn(const Decl *d, const ASTContext &ctx) {
  auto *md = dyn_cast_or_null<CXXMethodDecl>(d);
  if (!md || !md->getDeclName().getAsIdentifierInfo() ||
      !md->getDeclName().getAsIdentifierInfo()->isStr("allocate") ||
      (md->getNumParams() != 1 && md->getNumParams() != 2))
    return false;

  if (!ctx.hasSameType(md->parameters()[0]->getType(), ctx.getSizeType()))
    return false;

  if (md->getNumParams() == 2) {
    auto *pt = md->parameters()[1]->getType()->getAs<PointerType>();
    if (!pt || !pt->isVoidPointerType() ||
        !pt->getPointeeType().isConstQualified())
      return false;
  }

  return true;
}

static bool isSupportedCXXByrefHelperType(ASTContext &ctx, const VarDecl *var) {
  QualType ty = var->getType();
  const auto *record = ty->getAsCXXRecordDecl();
  if (!record)
    return false;

  return ctx.getBlockVarCopyInit(var).getCopyExpr() ||
         !record->hasTrivialDestructor();
}

static std::pair<uint64_t, uint8_t>
extractSourceAtomMetadata(mlir::Location loc) {
  if (auto fusedLoc = mlir::dyn_cast<mlir::FusedLoc>(loc)) {
    if (auto metadata =
            mlir::dyn_cast_or_null<mlir::DictionaryAttr>(fusedLoc.getMetadata())) {
      uint64_t atomGroup = 0;
      uint8_t atomRank = 0;
      if (auto atomGroupAttr =
              metadata.getAs<mlir::IntegerAttr>(kDILocationAtomGroupKey))
        if (atomGroupAttr.getValue().isNonNegative())
          atomGroup = atomGroupAttr.getValue().getZExtValue();
      if (auto atomRankAttr =
              metadata.getAs<mlir::IntegerAttr>(kDILocationAtomRankKey))
        if (atomRankAttr.getValue().isNonNegative() &&
            atomRankAttr.getValue().ule(7))
          atomRank = atomRankAttr.getValue().getZExtValue();
      return {atomGroup, atomRank};
    }
  }

  return {0, 0};
}

static mlir::Location stripSourceAtomMetadata(mlir::Location loc) {
  auto fusedLoc = mlir::dyn_cast<mlir::FusedLoc>(loc);
  if (!fusedLoc)
    return loc;

  auto metadata =
      mlir::dyn_cast_or_null<mlir::DictionaryAttr>(fusedLoc.getMetadata());
  if (!metadata)
    return loc;

  mlir::NamedAttrList attrs(metadata.getValue());
  attrs.erase(kDILocationAtomGroupKey);
  attrs.erase(kDILocationAtomRankKey);

  if (attrs.empty()) {
    if (fusedLoc.getLocations().size() == 1)
      return fusedLoc.getLocations().front();
    return mlir::FusedLoc::get(fusedLoc.getLocations(), nullptr,
                               loc.getContext());
  }

  return mlir::FusedLoc::get(fusedLoc.getLocations(),
                             mlir::DictionaryAttr::get(loc.getContext(), attrs),
                             loc.getContext());
}

static bool hasNoThrowExtraAttr(cir::FuncOp fn) {
  if (auto extraAttrs = fn.getExtraAttrs())
    return extraAttrs->getElements().get("nothrow") != nullptr;
  return false;
}

static void simplifyDeadSyntheticTrys(cir::FuncOp fn, CIRGenModule &cgm) {
  llvm::SmallVector<cir::TryOp, 4> deadTrys;
  fn.walk([&](cir::TryOp tryOp) {
    if (!tryOp.getSynthetic())
      return;

    auto &tryRegion = tryOp.getTryRegion();
    auto handlerRegions = tryOp.getHandlerRegions();
    if (!llvm::hasSingleElement(tryRegion) ||
        !llvm::hasSingleElement(handlerRegions))
      return;

    mlir::Block &tryBlock = tryRegion.front();
    mlir::Region &handlerRegion = handlerRegions.front();
    if (!llvm::hasSingleElement(handlerRegion))
      return;

    mlir::Block &handlerBlock = handlerRegion.front();
    if (!llvm::hasSingleElement(handlerBlock) ||
        !mlir::isa<cir::ResumeOp>(handlerBlock.front()))
      return;
    if (!mlir::isa<cir::YieldOp>(tryBlock.getTerminator()))
      return;

    bool sawCall = false;
    for (mlir::Operation &op : tryBlock.without_terminator()) {
      auto call = mlir::dyn_cast<cir::CallOp>(op);
      if (!call || !call.getException())
        return;
      cir::FuncOp callee = call.getCallee()
                               ? cgm.lookupFuncOp(*call.getCallee())
                               : cir::FuncOp{};
      if (!callee || !hasNoThrowExtraAttr(callee))
        return;
      sawCall = true;
    }

    if (sawCall)
      deadTrys.push_back(tryOp);
  });

  for (cir::TryOp tryOp : deadTrys) {
    mlir::Block &tryBlock = tryOp.getTryRegion().front();
    mlir::OpBuilder builder(fn.getContext());
    builder.setInsertionPoint(tryOp);

    for (mlir::Operation &op :
         llvm::make_early_inc_range(tryBlock.without_terminator())) {
      auto call = mlir::cast<cir::CallOp>(op);
      cir::FuncOp callee = call.getCallee()
                               ? cgm.lookupFuncOp(*call.getCallee())
                               : cir::FuncOp{};
      mlir::Type returnType = call.getNumResults()
                                  ? call.getResult().getType()
                                  : cir::VoidType::get(fn.getContext());
      cir::CallOp newCall = cir::CallOp::create(
          builder, call.getLoc(), mlir::SymbolRefAttr::get(callee), returnType,
          call.getArgOperands(), call.getCallingConv(), call.getSideEffect(),
          /*exception=*/nullptr);
      if (auto extraAttrs = call.getExtraAttrs())
        newCall->setAttr("extra_attrs", *extraAttrs);

      for (mlir::NamedAttribute attr : call->getAttrs()) {
        llvm::StringRef name = attr.getName().strref();
        if (name == "callee" || name == "exception" || name == "extra_attrs" ||
            name == "cir.terminate_on_throw")
          continue;
        newCall->setAttr(attr.getName(), attr.getValue());
      }

      if (call.getNumResults() != 0)
        call->replaceAllUsesWith(newCall->getResults());
      call.erase();
    }

    tryOp.erase();
  }
}

static bool shouldWalkSourceAtomBackupChain(mlir::Operation *keyOp,
                                            mlir::Operation *backupOp) {
  auto castOp = mlir::dyn_cast<cir::CastOp>(backupOp);
  if (!castOp)
    return true;

  // For scalar branch conditions like `if (x)`, OGCG only treats the
  // int-to-bool conversion and the branch as key instructions; the source load
  // remains plain. Keep walking through other cast chains, such as return-value
  // materialization, where the original scalar producer is still a backup.
  if (mlir::isa<cir::IfOp>(keyOp) &&
      castOp.getKind() == cir::CastKind::int_to_bool)
    return false;

  return true;
}

struct FakeUseCleanup final : EHScopeStack::Cleanup {
  Address addr;

  FakeUseCleanup(Address addr) : addr(addr) {}

  void emit(CIRGenFunction &cgf, Flags flags) override {
    cgf.emitFakeUse(addr);
  }
};
} // namespace

CIRGenFunction::CIRGenFunction(CIRGenModule &cgm, CIRGenBuilderTy &builder,
                               bool suppressNewContext)
    : CIRGenTypeCache(cgm), cgm{cgm}, builder(builder),
      PGO(std::make_unique<CIRGenPGO>(cgm)) {
  ehStack.setCGF(this);
  if (!suppressNewContext)
    cgm.getCXXABI().getMangleContext().startNewFunction();

  // Determine whether to emit lifetime markers (OGCG CGF.cpp:62-77).
  const auto &cgo = cgm.getCodeGenOpts();
  const auto &lo = cgm.getLangOpts();
  if (!cgo.DisableLifetimeMarkers) {
    if (cgo.SanitizeAddressUseAfterScope ||
        lo.Sanitize.has(SanitizerKind::HWAddress) ||
        lo.Sanitize.has(SanitizerKind::Memory))
      shouldEmitLifetimeMarkers = true;
    else
      shouldEmitLifetimeMarkers = cgo.OptimizationLevel != 0;
  }
}

CIRGenFunction::~CIRGenFunction() {}

bool CIRGenFunction::shouldExtendLifetime(const VarDecl &d) const {
  if (!curCodeDecl || curCodeDecl->isImplicit())
    return false;
  if (d.isImplicit() && &d != cxxabiThisDecl)
    return false;

  QualType ty = d.getType();
  if (ty.isVolatileQualified())
    return false;

  uint64_t maxSize = 4 * getContext().getTypeSize(getContext().UnsignedIntTy);
  if (getContext().getTypeSize(ty) > maxSize)
    return false;

  if (curCodeDecl->hasAttr<NoDebugAttr>() ||
      curCodeDecl->hasAttr<OptimizeNoneAttr>())
    return false;

  return true;
}

void CIRGenFunction::emitFakeUse(Address addr) {
  auto &builder = getBuilder();
  mlir::Location loc = curFn->getLoc();
  mlir::Value val = builder.createLoad(loc, addr);

  // Emit `llvm.fake.use` through the LLVM-intrinsic call op rather than as an
  // ordinary external `cir.func`/`cir.call`. The generic intrinsic lowering
  // produces an `llvm.call_intrinsic "llvm.fake.use"` whose MLIR->LLVM-IR
  // translation resolves the real `Intrinsic::fake_use` declaration, so the
  // backend's fake-use eliminator recognizes and drops it. An ordinary
  // external function would survive to link time as an undefined
  // `llvm.fake.use` symbol once a call is merged/LTO'd across TUs.
  auto voidTy = cir::VoidType::get(builder.getContext());
  mlir::Value ops[] = {val};
  auto call = cir::LLVMIntrinsicCallOp::create(
      builder, loc, builder.getStringAttr("fake.use"), voidTy, ops);
  // fake uses must not be turned into tail calls (mirrors classic CodeGen,
  // which emits `notail call void @llvm.fake.use`).
  call->setAttr("no_tail", mlir::UnitAttr::get(builder.getContext()));
  call->setAttr("nothrow", mlir::UnitAttr::get(&getMLIRContext()));
}

void CIRGenFunction::pushFakeUseCleanupForVar(const VarDecl &d, Address addr) {
  auto extend = cgm.getCodeGenOpts().getExtendVariableLiveness();
  if (extend != clang::CodeGenOptions::ExtendVariableLivenessKind::All &&
      !(extend == clang::CodeGenOptions::ExtendVariableLivenessKind::This &&
        &d == cxxabiThisDecl))
    return;

  if (shouldExtendLifetime(d))
    ehStack.pushCleanup<FakeUseCleanup>(NormalCleanup, addr);
}

mlir::Value CIRGenFunction::emitGuardedUBSanCheck(
    mlir::Location loc, mlir::Value pass,
    SanitizerKind::SanitizerOrdinal ordinal) {
  if (!pass)
    return pass;

  if (!clang::ClSanitizeGuardChecks &&
      cgm.getCodeGenOpts().SanitizeSkipHotCutoffs[ordinal] <= 0)
    return pass;

  mlir::Value checkKind = builder.getConstInt(
      loc, builder.getUInt8Ty(), static_cast<uint64_t>(ordinal));
  mlir::Value allow = cir::LLVMIntrinsicCallOp::create(
                          builder, loc,
                          cgm.getStringAttrCached("allow.ubsan.check"),
                          builder.getBoolTy(), mlir::ValueRange{checkKind})
                          .getResult();
  markNoSanitize(allow.getDefiningOp());

  mlir::Value skipCheck = builder.createNot(allow);
  if (auto *op = skipCheck.getDefiningOp())
    markNoSanitize(op);

  mlir::Value guardedPass = builder.createOr(loc, pass, skipCheck);
  if (auto *op = guardedPass.getDefiningOp())
    markNoSanitize(op);

  return guardedPass;
}

void CIRGenFunction::emitSanitizerStatReportStub(
    mlir::Location loc, CFISanitizerStatKind kind,
    mlir::Operation *insertBefore) {
  if (!cgm.getCodeGenOpts().SanitizeStats)
    return;

  auto stubFnTy = cir::FuncType::get({builder.getUInt32Ty()}, builder.getVoidTy());
  auto stubFn = cgm.createRuntimeFunction(
      stubFnTy, "__cir_sanitizer_stat_report", /*extraAttrs=*/{},
      /*isLocal=*/false, /*assumeConvergent=*/false);

  mlir::OpBuilder::InsertionGuard guard(builder);
  if (insertBefore)
    builder.setInsertionPoint(insertBefore);

  mlir::Value kindValue =
      builder.getConstInt(loc, builder.getUInt32Ty(),
                          static_cast<unsigned>(kind));
  builder.createCallOp(loc, stubFn, kindValue);
}

const CIRGenFunction::BlockByrefInfo &
CIRGenFunction::getBlockByrefInfo(const VarDecl *d) {
  auto it = blockByrefInfos.find(d);
  if (it != blockByrefInfos.end())
    return it->second;

  QualType ty = d->getType();
  bool hasCopyAndDispose = getContext().BlockRequiresCopying(ty, d);
  if (hasCopyAndDispose && !isSupportedObjCByrefHelperType(ty) &&
      !isSupportedNonARCObjectByrefHelperType(getContext(), ty) &&
      !isSupportedCXXByrefHelperType(getContext(), d) &&
      ty.isDestructedType() != QualType::DK_nontrivial_c_struct &&
      ty.isNonTrivialToPrimitiveDestructiveMove() != QualType::PCK_Struct &&
      !isSupportedAddressDiscriminatedByrefHelperType(ty)) {
    cgm.errorNYI(d->getLocation(),
                 "escaping-by-reference local requiring copy/dispose helpers");
  }

  bool hasByrefExtendedLayout = false;
  Qualifiers::ObjCLifetime byrefLifetime = Qualifiers::OCL_None;
  bool hasByrefLifetime =
      getContext().getByrefLifetime(ty, byrefLifetime, hasByrefExtendedLayout);

  const auto &layout = cgm.getDataLayout();
  CharUnits pointerSize =
      CharUnits::fromQuantity(layout.getTypeAllocSize(builder.getVoidPtrTy())
                                  .getFixedValue());
  CharUnits size;
  llvm::SmallVector<mlir::Type, 8> members;

  members.push_back(builder.getVoidPtrTy());
  size += pointerSize;
  members.push_back(builder.getVoidPtrTy());
  size += pointerSize;
  members.push_back(builder.getSInt32Ty());
  size += CharUnits::fromQuantity(4);
  members.push_back(builder.getSInt32Ty());
  size += CharUnits::fromQuantity(4);

  unsigned copyHelperIndex = 0;
  unsigned disposeHelperIndex = 0;
  if (hasCopyAndDispose) {
    copyHelperIndex = members.size();
    members.push_back(builder.getVoidPtrTy());
    size += pointerSize;

    disposeHelperIndex = members.size();
    members.push_back(builder.getVoidPtrTy());
    size += pointerSize;
  }

  unsigned layoutIndex = 0;
  if (hasByrefExtendedLayout) {
    layoutIndex = members.size();
    members.push_back(builder.getVoidPtrTy());
    size += pointerSize;
  }

  mlir::Type varTy = convertTypeForMem(ty);
  CharUnits varAlign = getContext().getDeclAlign(d);
  CharUnits varOffset = size.alignTo(varAlign);
  if (varOffset != size) {
    members.push_back(
        cir::ArrayType::get(builder.getSInt8Ty(), (varOffset - size).getQuantity()));
    size = varOffset;
  }

  bool packed = false;
  if (varOffset == size &&
      layout.getABITypeAlign(varTy) > uint64_t(varAlign.getQuantity()))
    packed = true;
  members.push_back(varTy);

  BlockByrefInfo info;
  info.type = builder.getAnonRecordTy(members, packed);
  info.fieldIndex = members.size() - 1;
  info.copyHelperIndex = copyHelperIndex;
  info.disposeHelperIndex = disposeHelperIndex;
  info.layoutIndex = layoutIndex;
  info.byrefAlignment = std::max(varAlign, getPointerAlign());
  info.fieldOffset = varOffset;
  info.byrefLifetime = byrefLifetime;
  info.hasByrefLifetime = hasByrefLifetime;
  info.hasCopyAndDispose = hasCopyAndDispose;
  info.hasByrefExtendedLayout = hasByrefExtendedLayout;

  auto [newIt, inserted] = blockByrefInfos.try_emplace(d, info);
  assert(inserted && "block byref info inserted recursively");
  return newIt->second;
}

CharUnits CIRGenFunction::getDirectBlockLiteralAlignment(
    llvm::ArrayRef<mlir::Type> literalMembers) const {
  const auto &layout = cgm.getDataLayout();
  CharUnits literalAlign = CharUnits::One();
  for (mlir::Type member : literalMembers) {
    literalAlign = std::max(
        literalAlign,
        CharUnits::fromQuantity(layout.getABITypeAlign(member).value()));
  }
  return literalAlign;
}

CharUnits CIRGenFunction::getDirectBlockLiteralFieldOffset(
    llvm::ArrayRef<mlir::Type> literalMembers, bool packed,
    unsigned fieldIndex) const {
  assert(fieldIndex < literalMembers.size() &&
         "direct block field index out of range");
  const auto &layout = cgm.getDataLayout();
  auto getFieldAlign = [&](mlir::Type member) {
    return packed ? CharUnits::One()
                  : CharUnits::fromQuantity(layout.getABITypeAlign(member).value());
  };
  auto getFieldSize = [&](mlir::Type member) {
    return CharUnits::fromQuantity(
        layout.getTypeAllocSize(member).getFixedValue());
  };

  CharUnits offset;
  for (mlir::Type member : literalMembers.take_front(fieldIndex)) {
    offset = offset.alignTo(getFieldAlign(member));
    offset += getFieldSize(member);
  }
  return offset.alignTo(getFieldAlign(literalMembers[fieldIndex]));
}

CharUnits CIRGenFunction::getDirectBlockLiteralSize(
    llvm::ArrayRef<mlir::Type> literalMembers, bool packed) const {
  const auto &layout = cgm.getDataLayout();
  auto getFieldAlign = [&](mlir::Type member) {
    return packed ? CharUnits::One()
                  : CharUnits::fromQuantity(layout.getABITypeAlign(member).value());
  };
  auto getFieldSize = [&](mlir::Type member) {
    return CharUnits::fromQuantity(
        layout.getTypeAllocSize(member).getFixedValue());
  };

  CharUnits size;
  CharUnits recordAlign = CharUnits::One();
  for (mlir::Type member : literalMembers) {
    CharUnits fieldAlign = getFieldAlign(member);
    size = size.alignTo(fieldAlign);
    size += getFieldSize(member);
    recordAlign = std::max(recordAlign, fieldAlign);
  }
  return size.alignTo(recordAlign);
}

bool CIRGenFunction::shouldPackDirectBlockLiteral(
    llvm::ArrayRef<mlir::Type> literalMembers) const {
  if (literalMembers.size() <= 5)
    return false;

  CharUnits packedSize = getDirectBlockLiteralSize(literalMembers, true);
  CharUnits unpackedSize = getDirectBlockLiteralSize(literalMembers, false);
  if (packedSize >= unpackedSize)
    return false;

  for (unsigned fieldIndex = 0, fieldCount = literalMembers.size();
       fieldIndex != fieldCount; ++fieldIndex)
    if (getDirectBlockLiteralFieldOffset(literalMembers, true, fieldIndex) !=
        getDirectBlockLiteralFieldOffset(literalMembers, false, fieldIndex))
      return false;

  return true;
}

Address CIRGenFunction::emitBlockByrefAddress(Address baseAddr,
                                              const VarDecl *var,
                                              bool followForward) {
  const auto &info = getBlockByrefInfo(var);
  mlir::Location loc = baseAddr.getPointer().getLoc();

  if (followForward) {
    CharUnits forwardingOffset = getPointerAlign();
    CharUnits forwardingAlign =
        baseAddr.getAlignment().alignmentAtOffset(forwardingOffset);
    mlir::Value forwardingField = builder.createGetMember(
        loc, builder.getPointerTo(builder.getVoidPtrTy()), baseAddr.getPointer(),
        "", 1);
    mlir::Value forwarded = builder.createAlignedLoad(
        loc, builder.getVoidPtrTy(), forwardingField, forwardingAlign);
    mlir::Type byrefPtrTy = builder.getPointerTo(info.type);
    if (forwarded.getType() != byrefPtrTy)
      forwarded = builder.createBitcast(loc, forwarded, byrefPtrTy);
    baseAddr = Address(forwarded, info.type, info.byrefAlignment);
  }

  mlir::Type objectTy = convertTypeForMem(var->getType());
  mlir::Value objectPtr = builder.createGetMember(
      loc, builder.getPointerTo(objectTy), baseAddr.getPointer(), "",
      info.fieldIndex);
  return Address(objectPtr, objectTy,
                 baseAddr.getAlignment().alignmentAtOffset(info.fieldOffset));
}

CIRGenFunction::ApplySourceAtomGroup::ApplySourceAtomGroup(CIRGenFunction &cgf)
    : cgf(cgf.shouldEmitSourceAtoms() ? &cgf : nullptr) {
  if (!this->cgf)
    return;

  originalAtom = this->cgf->sourceAtomInfo.currentAtom;
  this->cgf->sourceAtomInfo.currentAtom = this->cgf->sourceAtomInfo.nextAtom++;
}

CIRGenFunction::ApplySourceAtomGroup::~ApplySourceAtomGroup() {
  if (!cgf)
    return;

  cgf->sourceAtomInfo.nextAtom =
      std::min(cgf->sourceAtomInfo.highestEmittedAtom + 1,
               cgf->sourceAtomInfo.nextAtom);
  cgf->sourceAtomInfo.currentAtom = originalAtom;
}

bool CIRGenFunction::shouldEmitSourceAtoms() const {
  if (!curFn || !cgm.getCodeGenOpts().DebugKeyInstructions)
    return false;
  if (cgm.getCodeGenOpts().getDebugInfo() == llvm::codegenoptions::NoDebugInfo)
    return false;
  if (const auto *fd = dyn_cast_or_null<FunctionDecl>(curGD.getDecl()))
    if (llvm::isa_and_present<CoroutineBodyStmt>(fd->getBody()))
      return false;
  return true;
}

mlir::Location CIRGenFunction::applySourceScopeMetadata(mlir::Location loc) {
  LexicalScope *scope = curLexScope;
  while (scope && (!scope->getParentScope() || scope->isCleanupOnly()))
    scope = scope->getParentScope();
  if (!scope)
    return loc;

  // The decorated location is a pure function of (loc, this resolved scope).
  // The same pair recurs for every op sharing a source location within the
  // scope, so memoize on the scope to avoid rebuilding the scope-chain attrs +
  // DictionaryAttr + FusedLoc on every getLoc() call.
  auto &cache = scope->getDIScopeLocCache();
  if (auto it = cache.find(loc); it != cache.end())
    return it->second;

  auto computeResult = [&]() -> mlir::Location {
    auto scopeFileLoc =
        scope->getBeginLoc()->findInstanceOf<mlir::FileLineColLoc>();
    if (!scopeFileLoc || !scopeFileLoc.getLine() || !scopeFileLoc.getColumn())
      return loc;

    auto *ctx = &getMLIRContext();
    auto i32Ty = mlir::IntegerType::get(ctx, 32);
    mlir::NamedAttrList attrs;
    if (auto fusedLoc = mlir::dyn_cast<mlir::FusedLoc>(loc))
      if (auto metadata = mlir::dyn_cast_or_null<mlir::DictionaryAttr>(
              fusedLoc.getMetadata()))
        attrs.append(metadata.getValue().begin(), metadata.getValue().end());

    attrs.set(kDILocationScopeLineKey,
              mlir::IntegerAttr::get(i32Ty, scopeFileLoc.getLine()));
    attrs.set(kDILocationScopeColumnKey,
              mlir::IntegerAttr::get(i32Ty, scopeFileLoc.getColumn()));

    // Record the full enclosing-scope chain (innermost first) so the lowering
    // can materialize nested lexical blocks. Without this, every block is
    // parented directly to the subprogram, making enclosing-block variables
    // (e.g. a loop counter) invisible while debugging inside a nested block.
    llvm::SmallVector<mlir::Attribute> scopeChain;
    for (const LexicalScope *s = scope; s; s = s->getParentScope()) {
      if (!s->getParentScope() || s->isCleanupOnly())
        continue;
      auto fl = s->getBeginLoc()->findInstanceOf<mlir::FileLineColLoc>();
      if (fl && fl.getLine() && fl.getColumn()) {
        scopeChain.push_back(mlir::IntegerAttr::get(i32Ty, fl.getLine()));
        scopeChain.push_back(mlir::IntegerAttr::get(i32Ty, fl.getColumn()));
      }
    }
    if (!scopeChain.empty())
      attrs.set(kDILocationScopeChainKey, mlir::ArrayAttr::get(ctx, scopeChain));

    if (auto fusedLoc = mlir::dyn_cast<mlir::FusedLoc>(loc))
      return mlir::FusedLoc::get(fusedLoc.getLocations(),
                                 mlir::DictionaryAttr::get(ctx, attrs), ctx);
    return mlir::FusedLoc::get({loc}, mlir::DictionaryAttr::get(ctx, attrs),
                               ctx);
  };

  mlir::Location result = computeResult();
  cache.try_emplace(loc, result);
  return result;
}

mlir::Location CIRGenFunction::applySourceAtomMetadata(mlir::Location loc,
                                                       uint64_t group,
                                                       uint8_t rank) {
  if (!group || !rank)
    return loc;

  auto [existingGroup, existingRank] = extractSourceAtomMetadata(loc);
  if (existingGroup && existingRank && existingRank < rank)
    return loc;

  auto *ctx = &getMLIRContext();
  auto i64Ty = mlir::IntegerType::get(ctx, 64);
  auto i8Ty = mlir::IntegerType::get(ctx, 8);
  auto dict = mlir::DictionaryAttr::get(
      ctx,
      {mlir::NamedAttribute(
           mlir::StringAttr::get(ctx, kDILocationAtomGroupKey),
           mlir::IntegerAttr::get(i64Ty, group)),
       mlir::NamedAttribute(
           mlir::StringAttr::get(ctx, kDILocationAtomRankKey),
           mlir::IntegerAttr::get(i8Ty, rank))});
  return mlir::FusedLoc::get({loc}, dict, ctx);
}

void CIRGenFunction::addOpToSourceAtomWithRank(mlir::Operation *op,
                                               uint64_t group, uint8_t rank) {
  if (!op || !group || !rank || !shouldEmitSourceAtoms())
    return;

  op->setLoc(applySourceAtomMetadata(op->getLoc(), group, rank));
  sourceAtomInfo.highestEmittedAtom =
      std::max(group, sourceAtomInfo.highestEmittedAtom);
}

void CIRGenFunction::addOpToSpecificSourceAtom(mlir::Operation *keyOp,
                                               mlir::Value backup,
                                               uint64_t group) {
  if (!keyOp || !group || !shouldEmitSourceAtoms())
    return;

  keyOp->setLoc(applySourceAtomMetadata(keyOp->getLoc(), group, /*rank=*/1));
  sourceAtomInfo.highestEmittedAtom =
      std::max(group, sourceAtomInfo.highestEmittedAtom);

  mlir::Operation *backupOp = backup ? backup.getDefiningOp() : nullptr;
  if (!backupOp)
    return;

  backupOp->setLoc(applySourceAtomMetadata(backupOp->getLoc(), group,
                                           /*rank=*/2));
  if (!shouldWalkSourceAtomBackupChain(keyOp, backupOp))
    return;

  uint8_t rank = 3;
  while (auto castOp = mlir::dyn_cast<cir::CastOp>(backupOp)) {
    backupOp = castOp.getSrc().getDefiningOp();
    if (!backupOp)
      break;
    backupOp->setLoc(
        applySourceAtomMetadata(backupOp->getLoc(), group, rank++));
  }
}

// This is copied from clang/lib/CodeGen/CodeGenFunction.cpp
cir::TypeEvaluationKind CIRGenFunction::getEvaluationKind(QualType type) {
  type = type.getCanonicalType();
  while (true) {
    switch (type->getTypeClass()) {
#define TYPE(name, parent)
#define ABSTRACT_TYPE(name, parent)
#define NON_CANONICAL_TYPE(name, parent) case Type::name:
#define DEPENDENT_TYPE(name, parent) case Type::name:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(name, parent) case Type::name:
#include "clang/AST/TypeNodes.inc"
      llvm_unreachable("non-canonical or dependent type in IR-generation");

    case Type::Auto:
    case Type::DeducedTemplateSpecialization:
      llvm_unreachable("undeduced type in IR-generation");

    // Various scalar types.
    case Type::Builtin:
    case Type::Pointer:
    case Type::BlockPointer:
    case Type::LValueReference:
    case Type::RValueReference:
    case Type::MemberPointer:
    case Type::Vector:
    case Type::ExtVector:
    case Type::ConstantMatrix:
    case Type::FunctionProto:
    case Type::FunctionNoProto:
    case Type::Enum:
    case Type::ObjCObjectPointer:
    case Type::Pipe:
    case Type::BitInt:
    case Type::HLSLAttributedResource:
    case Type::HLSLInlineSpirv:
      return cir::TEK_Scalar;

    // Complexes.
    case Type::Complex:
      return cir::TEK_Complex;

    // Arrays, records, and Objective-C objects.
    case Type::ConstantArray:
    case Type::IncompleteArray:
    case Type::VariableArray:
    case Type::Record:
    case Type::ObjCObject:
    case Type::ObjCInterface:
    case Type::ArrayParameter:
      return cir::TEK_Aggregate;

    // We operate on atomic values according to their underlying type.
    case Type::Atomic:
      type = cast<AtomicType>(type)->getValueType();
      continue;
    }
    llvm_unreachable("unknown type kind!");
  }
}

mlir::Type CIRGenFunction::convertTypeForMem(QualType t) {
  return cgm.getTypes().convertTypeForMem(t);
}

mlir::Type CIRGenFunction::convertType(QualType t) {
  return cgm.getTypes().convertType(t);
}

mlir::Location CIRGenFunction::getSimpleLoc(SourceLocation srcLoc) {
  // Some AST nodes might contain invalid source locations (e.g.
  // CXXDefaultArgExpr), workaround that to still get something out.
  if (srcLoc.isValid()) {
    const unsigned rawEncoding = srcLoc.getRawEncoding();
    if (auto it = sourceLocCache.find(rawEncoding); it != sourceLocCache.end())
      return it->second;

    const SourceManager &sm = getContext().getSourceManager();
    PresumedLoc pLoc = sm.getPresumedLoc(srcLoc);
    const char *filenamePtr = pLoc.getFilename();
    auto [fileIt, inserted] = sourceFileAttrCache.try_emplace(filenamePtr);
    if (inserted)
      fileIt->second = cgm.getStringAttrCached(filenamePtr);
    bool suppressDebugColumns =
        cgm.getCodeGenOpts().getDebugInfo() !=
            llvm::codegenoptions::NoDebugInfo &&
        !cgm.getCodeGenOpts().DebugColumnInfo;
    unsigned column = suppressDebugColumns ? 0 : pLoc.getColumn();
    mlir::Location loc =
        mlir::FileLineColLoc::get(fileIt->second, pLoc.getLine(), column);
    sourceLocCache.try_emplace(rawEncoding, loc);
    return loc;
  }
  // Do our best...
  if (currSrcLoc)
    return *currSrcLoc;
  // Use the function's entry location as a fallback so we never produce
  // UnknownLoc inside a function body.  The LLVM verifier requires every
  // inlinable call in a function with a DISubprogram to carry a !dbg
  // location; UnknownLoc translates to nullptr, violating that rule.
  if (curFn) {
    mlir::Location fnLoc = curFn->getLoc();
    if (!mlir::isa<mlir::UnknownLoc>(fnLoc))
      return fnLoc;
  }
  return builder.getUnknownLoc();
}

mlir::Location CIRGenFunction::getLoc(SourceLocation srcLoc) {
  return applySourceScopeMetadata(getSimpleLoc(srcLoc));
}

mlir::Location CIRGenFunction::getLoc(SourceRange srcLoc) {
  // Some AST nodes might contain invalid source locations (e.g.
  // CXXDefaultArgExpr), workaround that to still get something out.
  if (srcLoc.isValid()) {
    const uint64_t beginEncoding = srcLoc.getBegin().getRawEncoding();
    const uint64_t endEncoding = srcLoc.getEnd().getRawEncoding();
    const uint64_t key = (beginEncoding << 32) | endEncoding;
    if (auto it = sourceRangeLocCache.find(key); it != sourceRangeLocCache.end())
      return applySourceScopeMetadata(it->second);

    mlir::Location beg = getSimpleLoc(srcLoc.getBegin());
    mlir::Location endLoc = getSimpleLoc(srcLoc.getEnd());
    SmallVector<mlir::Location, 2> locs = {beg, endLoc};
    mlir::Attribute metadata;
    mlir::Location loc = mlir::FusedLoc::get(locs, metadata, &getMLIRContext());
    sourceRangeLocCache.try_emplace(key, loc);
    return applySourceScopeMetadata(loc);
  }
  if (currSrcLoc) {
    return applySourceScopeMetadata(*currSrcLoc);
  }
  // Use the function's entry location as a fallback so we never produce
  // UnknownLoc inside a function body.  The LLVM verifier requires every
  // inlinable call in a function with a DISubprogram to carry a !dbg
  // location; UnknownLoc translates to nullptr, violating that rule.
  if (curFn) {
    mlir::Location fnLoc = curFn->getLoc();
    if (!mlir::isa<mlir::UnknownLoc>(fnLoc))
      return applySourceScopeMetadata(fnLoc);
  }
  return builder.getUnknownLoc();
}

mlir::Location CIRGenFunction::getLoc(mlir::Location lhs, mlir::Location rhs) {
  SmallVector<mlir::Location, 2> locs = {lhs, rhs};
  mlir::Attribute metadata;
  return mlir::FusedLoc::get(locs, metadata, &getMLIRContext());
}

bool CIRGenFunction::containsLabel(const Stmt *s, bool ignoreCaseStmts) {
  LabelCacheKey cacheKey{s, ignoreCaseStmts};
  if (auto it = containsLabelCache.find(cacheKey); it != containsLabelCache.end())
    return it->second;

  // Keep label scanning iterative to avoid stack overflows on deeply nested
  // ASTs in large TUs.
  llvm::SmallVector<LabelCacheKey, 32> worklist;
  worklist.push_back(cacheKey);
  llvm::DenseSet<LabelCacheKey, LabelCacheKeyInfo> visited;

  bool hasLabel = false;
  while (!worklist.empty()) {
    LabelCacheKey cur = worklist.pop_back_val();
    if (!cur.stmt)
      continue;
    if (!visited.insert(cur).second)
      continue;

    // If this is a label, we have to emit the code, consider something like:
    // if (0) {  ...  foo:  bar(); }  goto foo;
    //
    // TODO: If anyone cared, we could track __label__'s, since we know that
    // you can't jump to one from outside their declared region.
    if (isa<LabelStmt>(cur.stmt)) {
      hasLabel = true;
      break;
    }

    // If this is a case/default statement, and we haven't seen a switch, we
    // have to emit the code.
    if (isa<SwitchCase>(cur.stmt) && !cur.ignoreCaseStmts) {
      hasLabel = true;
      break;
    }

    // If this is a switch statement, we want to ignore case statements when
    // recursively processing sub-statements.
    bool childIgnoreCaseStmts =
        cur.ignoreCaseStmts || isa<SwitchStmt>(cur.stmt);
    for (const Stmt *subStmt : cur.stmt->children())
      worklist.push_back({subStmt, childIgnoreCaseStmts});
  }

  containsLabelCache.try_emplace(cacheKey, hasLabel);
  return hasLabel;
}

/// Return true if the statement contains a break out of it.
/// If the statement (recursively) contains a switch or loop with a break
/// inside of it, this is fine.
bool CIRGenFunction::containsBreak(const Stmt *s) {
  if (auto it = containsBreakCache.find(s); it != containsBreakCache.end())
    return it->second;

  bool hasBreak = false;
  if (!s)
    hasBreak = false;

  // If this is a switch or loop that defines its own break scope, then we can
  // include it and anything inside of it.
  else if (isa<SwitchStmt>(s) || isa<WhileStmt>(s) || isa<DoStmt>(s) ||
           isa<ForStmt>(s))
    hasBreak = false;

  else if (isa<BreakStmt>(s))
    hasBreak = true;

  else
    hasBreak = std::any_of(s->child_begin(), s->child_end(),
                           [=](const Stmt *subStmt) {
                             return containsBreak(subStmt);
                           });

  containsBreakCache.try_emplace(s, hasBreak);
  return hasBreak;
}

bool CIRGenFunction::mightAddDeclToScope(const Stmt *s) {
  if (auto it = mightAddDeclToScopeCache.find(s);
      it != mightAddDeclToScopeCache.end())
    return it->second;

  bool mightAddDecl = false;
  if (!s)
    mightAddDecl = false;

  // Some statement kinds add a scope and thus never add a decl to the current
  // scope. Note, this list is longer than the list of statements that might
  // have an unscoped decl nested within them, but this way is conservatively
  // correct even if more statement kinds are added.
  else if (isa<IfStmt>(s) || isa<SwitchStmt>(s) || isa<WhileStmt>(s) ||
           isa<DoStmt>(s) || isa<ForStmt>(s) || isa<CompoundStmt>(s) ||
           isa<CXXForRangeStmt>(s) || isa<CXXTryStmt>(s) ||
           isa<ObjCForCollectionStmt>(s) || isa<ObjCAtTryStmt>(s))
    mightAddDecl = false;

  else if (isa<DeclStmt>(s))
    mightAddDecl = true;

  else
    mightAddDecl = std::any_of(s->child_begin(), s->child_end(),
                               [=](const Stmt *subStmt) {
                                 return mightAddDeclToScope(subStmt);
                               });

  mightAddDeclToScopeCache.try_emplace(s, mightAddDecl);
  return mightAddDecl;
}

/// If the specified expression does not fold to a constant, or if it does but
/// contains a label, return false.  If it constant folds return true and set
/// the boolean result in Result.
bool CIRGenFunction::constantFoldsToBool(const Expr *cond, bool &resultBool,
                                         bool allowLabels) {
  // OGCG returns false early here when MC/DC coverage is enabled and profile
  // instrumentation is active, to ensure all conditions are instrumented for
  // complete test vectors. CIR always folds.
  assert(!cir::MissingFeatures::mcdcCoverageFolding());

  llvm::APSInt resultInt;
  if (!constantFoldsToSimpleInteger(cond, resultInt, allowLabels))
    return false;

  resultBool = resultInt.getBoolValue();
  return true;
}

/// If the specified expression does not fold to a constant, or if it does
/// fold but contains a label, return false. If it constant folds, return
/// true and set the folded value.
bool CIRGenFunction::constantFoldsToSimpleInteger(const Expr *cond,
                                                  llvm::APSInt &resultInt,
                                                  bool allowLabels) {
  // FIXME: Rename and handle conversion of other evaluatable things
  // to bool.
  Expr::EvalResult result;
  if (!cond->EvaluateAsInt(result, getContext()))
    return false; // Not foldable, not integer or not fully evaluatable.

  llvm::APSInt intValue = result.Val.getInt();
  if (!allowLabels && containsLabel(cond))
    return false; // Contains a label.

  resultInt = intValue;
  return true;
}

const Expr *CIRGenFunction::stripCond(const Expr *cond) {
  while (true) {
    const Expr *stripped = IgnoreExprNodes(
        cond, IgnoreParensSingleStep, IgnoreUOpLNotSingleStep,
        IgnoreBuiltinExpectSingleStep, IgnoreImplicitCastsSingleStep);
    if (cond == stripped)
      return stripped;
    cond = stripped;
  }
}

bool CIRGenFunction::isInstrumentedCondition(const Expr *cond) {
  const auto *binOp = dyn_cast<BinaryOperator>(stripCond(cond));
  return !binOp || !binOp->isLogicalOp();
}

void CIRGenFunction::emitAndUpdateRetAlloca(QualType type, mlir::Location loc,
                                            CharUnits alignment) {
  if (!type->isVoidType()) {
    // Note: we do NOT pass getLangTempAllocaAddressSpace() here because
    // for sret functions NRVO replaces this alloca with the sret pointer
    // (which has target_address_space(5) on AMDGPU). Mixing lang and target
    // AS representations causes type mismatches in the RAUW'd operations.
    Address retAddr =
        createTempAlloca(convertType(type), alignment, loc, "__retval");
    fnRetAlloca = retAddr.emitRawPointer();
    returnValue = retAddr;
  }
}

void CIRGenFunction::declare(mlir::Value addrVal, const Decl *var, QualType ty,
                             mlir::Location loc, CharUnits alignment,
                             bool isParam) {
  assert(isa<NamedDecl>(var) && "Needs a named decl");
  assert(!symbolTable.count(var) && "not supposed to be available just yet");

  auto allocaOp = addrVal.getDefiningOp<cir::AllocaOp>();
  assert(allocaOp && "expected cir::AllocaOp");

  if (isParam)
    allocaOp.setInitAttr(mlir::UnitAttr::get(&getMLIRContext()));
  if (ty->isReferenceType() || ty.isConstQualified())
    allocaOp.setConstantAttr(mlir::UnitAttr::get(&getMLIRContext()));

  symbolTable.insert(var, allocaOp);
}

/// Matches OGCG rescopeLabels (CGStmt.cpp:758-776). When a LexicalScope
/// exits, labels inside it have stale scope depths pointing to popped
/// scopes. Adjust them outward to the innermost surviving cleanup.
void CIRGenFunction::LexicalScope::rescopeLabels() {
  assert(!labels.empty());
  EHScopeStack::stable_iterator innermostScope =
      cgf.ehStack.getInnermostNormalCleanup();

  for (const clang::LabelDecl *label : labels) {
    auto it = cgf.labelMap.find(label);
    if (it == cgf.labelMap.end())
      continue;
    JumpDest &dest = it->second;
    if (!dest.getScopeDepth().isValid())
      continue;
    assert(innermostScope.encloses(dest.getScopeDepth()));
    dest.setScopeDepth(innermostScope);
  }

  if (innermostScope != EHScopeStack::stable_end() && parentScope)
    parentScope->labels.append(labels.begin(), labels.end());
}

void CIRGenFunction::LexicalScope::cleanup() {
  // For cleanup-only scopes (label sub-statements), just emit cleanup code
  // without scope termination ops (no cir.yield, no return block handling).
  // These scopes exist for RunCleanupsScope tracking and LexicalScope chain
  // visibility, but don't have a corresponding cir.scope op.
  if (cleanupOnly) {
    // Return blocks created inside cleanup-only scopes must be handled by an
    // ancestor scope that emits return terminators. Otherwise they remain
    // unterminated and trip the verifier ("empty block: expect at least a
    // terminator"). This can happen when codegen intentionally skips creating
    // a cir.scope region (e.g. fast-path simple if statements) and reuses a
    // cleanup-only LexicalScope for cleanup tracking.
    if (parentScope && !retBlocks.empty()) {
      for (mlir::Block *retBlock : retBlocks) {
        if (!llvm::is_contained(parentScope->retBlocks, retBlock))
          parentScope->retBlocks.push_back(retBlock);
        auto retLocIt = retLocs.find(retBlock);
        if (retLocIt != retLocs.end())
          parentScope->retLocs.insert_or_assign(retBlock, retLocIt->second);
      }
    }
    if (performCleanup)
      forceCleanup();
    return;
  }

  CIRGenBuilderTy &builder = cgf.builder;
  LexicalScope *localScope = cgf.curLexScope;

  auto applyCleanup = [&]() {
    if (performCleanup) {
      forceCleanup();
    }
  };

  // Cleanup are done right before codegen resumes a scope. This is where
  // objects are destroyed. Process all return blocks.
  // TODO(cir): Handle returning from a switch statement through a cleanup
  // block. We can't simply jump to the cleanup block, because the cleanup block
  // is not part of the case region. Either reemit all cleanups in the return
  // block or wait for MLIR structured control flow to support early exits.
  llvm::SmallVector<mlir::Block *> retBlocks;
  for (mlir::Block *retBlock : localScope->getRetBlocks()) {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToEnd(retBlock);
    retBlocks.push_back(retBlock);
    mlir::Location retLoc = localScope->getRetLoc(retBlock);
    emitReturn(retLoc);
  }

  auto insertCleanupAndLeave = [&](mlir::Block *insPt) {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToEnd(insPt);

    // If we still don't have a cleanup block, it means that `applyCleanup`
    // below might be able to get us one.
    mlir::Block *cleanupBlock = localScope->getCleanupBlock(builder);

    // Leverage and defers to RunCleanupsScope's dtor and scope handling.
    applyCleanup();

    // If we now have one after `applyCleanup`, hook it up properly.
    if (!cleanupBlock && localScope->getCleanupBlock(builder)) {
      cleanupBlock = localScope->getCleanupBlock(builder);
      cir::BrOp::create(builder, insPt->back().getLoc(), cleanupBlock);
    }

    if (localScope->depth == 0) {
      // Reached the end of the function.
      // Special handling only for single return block case
      if (localScope->getRetBlocks().size() == 1) {
        mlir::Block *retBlock = localScope->getRetBlocks()[0];
        mlir::Location retLoc = localScope->getRetLoc(retBlock);
        if (retBlock->getUses().empty()) {
          retBlock->erase();
        } else {
          // Thread return block via cleanup block.
          if (cleanupBlock) {
            for (mlir::BlockOperand &blockUse :
                 llvm::make_early_inc_range(retBlock->getUses())) {
              cir::BrOp brOp = mlir::cast<cir::BrOp>(blockUse.getOwner());
              brOp.setSuccessor(cleanupBlock);
            }
            // Branch from cleanup block to return block. Replace any
            // existing terminator (e.g., cir.yield) that was added earlier.
            if (cleanupBlock->mightHaveTerminator() &&
                cleanupBlock->getTerminator())
              cleanupBlock->getTerminator()->erase();
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPointToEnd(cleanupBlock);
            cir::BrOp::create(builder, retLoc, retBlock);
          } else {
            cir::BrOp::create(builder, retLoc, retBlock);
          }
          return;
        }
      }
      // Add terminator to cleanup block if needed (fallthrough path with
      // no return block — e.g., non-void function that doesn't return).
      if (cleanupBlock && !cleanupBlock->mightHaveTerminator()) {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToEnd(cleanupBlock);
        emitImplicitReturn();
      } else {
        emitImplicitReturn();
      }
      return;
    }

    // Add terminator to cleanup block for non-function scopes.
    if (cleanupBlock && !cleanupBlock->mightHaveTerminator()) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToEnd(cleanupBlock);
      cir::YieldOp::create(builder, localScope->endLoc);
    }

    // End of any local scope != function
    // Ternary ops have to deal with matching arms for yielding types
    // and do return a value, it must do its own cir.yield insertion.
    if (!localScope->isTernary() && !insPt->mightHaveTerminator()) {
      !retVal ? cir::YieldOp::create(builder, localScope->endLoc)
              : cir::YieldOp::create(builder, localScope->endLoc, retVal);
    }
  };

  // If a cleanup block has been created at some point, branch to it
  // and set the insertion point to continue at the cleanup block.
  // Terminators are then inserted either in the cleanup block or
  // inline in this current block.
  mlir::Block *cleanupBlock = localScope->getCleanupBlock(builder);
  if (cleanupBlock)
    insertCleanupAndLeave(cleanupBlock);

  // Now deal with any pending block wrap up like implicit end of
  // scope.

  mlir::Block *curBlock = builder.getBlock();
  if (!curBlock)
    return;
  if (curBlock->mightHaveTerminator() && curBlock->getTerminator()) {
    // When the block already has a terminator (yield, branch, return, etc.),
    // we must emit any pending cleanups (e.g. temporary destructors) BEFORE
    // the terminator so it remains the last operation in the block. Without
    // this, RunCleanupsScope's destructor emits cleanups AFTER the terminator,
    // violating MLIR's terminator invariant (e.g. "cir.yield op must be the
    // last operation in the parent block").
    if (performCleanup) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPoint(curBlock->getTerminator());
      applyCleanup();
    }
    return;
  }

  // A trivially-empty block that directly follows a cir.asm.goto is that
  // asm goto's fallthrough continuation. It legitimately has no predecessors
  // at CIRGen time because the asm.goto -> fallthrough edge is only wired up
  // later by GotoSolver, so it must NOT be treated as dead and erased —
  // GotoSolver would then find no fallthrough block and assert. Terminate it
  // like classic CodeGen, which always emits a real `asm.fallthrough` block
  // (an implicit return at function scope), and keep it.
  if (curBlock->empty() && curBlock->hasNoPredecessors() &&
      !curBlock->isEntryBlock()) {
    if (mlir::Block *prev = curBlock->getPrevNode();
        prev && !prev->empty() &&
        mlir::isa<cir::InlineAsmGotoOp>(prev->back()) &&
        prev->back().getNumSuccessors() == 0) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToEnd(curBlock);
      if (localScope->depth == 0)
        emitImplicitReturn();
      else
        cir::YieldOp::create(builder, localScope->endLoc);
      return;
    }
  }

  // Get rid of any empty block at the end of the scope.
  bool entryBlock = builder.getInsertionBlock()->isEntryBlock();
  // Only erase empty continuation blocks that are actually dead. Hand-built
  // CFG lowering paths can legitimately leave the insertion point on an empty
  // live block that is the target of branches emitted earlier in the scope.
  if (!entryBlock && curBlock->empty() && curBlock->hasNoPredecessors()) {
    mlir::Block *priorCleanupBlock = localScope->getCleanupBlock(builder);

    // If there are pending cleanups, redirect emission to a dedicated cleanup
    // block so that inline cleanup emission (from popCleanupBlock) goes into
    // the cleanup block rather than the dead continuation block. This allows
    // the cleanup block to be properly wired into the CFG.
    // Also create a cleanup block when there are retBlocks that need routing
    // through cleanup, even if the current block has no predecessors (e.g.,
    // the dead block after co_return). Without this, cleanup code (like RAII
    // destructors) is emitted into the dead block and then lost when the dead
    // block is erased below.
    if (!priorCleanupBlock &&
        cgf.ehStack.stable_begin() != getCleanupStackDepth() &&
        (!curBlock->hasNoPredecessors() || !retBlocks.empty())) {
      builder.setInsertionPointToEnd(
          localScope->getOrCreateCleanupBlock(builder));
    }

    applyCleanup();
    mlir::Block *newCleanupBlock = localScope->getCleanupBlock(builder);

    // If applyCleanup created a cleanup block, thread it into the return
    // path: redirect BrOps targeting retBlocks to go through cleanup first.
    if (!priorCleanupBlock && newCleanupBlock) {
      for (mlir::Block *retBlock : retBlocks) {
        for (mlir::BlockOperand &blockUse :
             llvm::make_early_inc_range(retBlock->getUses())) {
          auto brOp = mlir::cast<cir::BrOp>(blockUse.getOwner());
          brOp.setSuccessor(newCleanupBlock);
        }
        if (!newCleanupBlock->mightHaveTerminator() ||
            !newCleanupBlock->getTerminator()) {
          mlir::OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToEnd(newCleanupBlock);
          cir::BrOp::create(builder, localScope->endLoc, retBlock);
        }
      }

      // If the cleanup block still has no terminator (e.g., goto or break
      // diverted control flow out of the scope with no return blocks to
      // route through), terminate it appropriately.
      if (!newCleanupBlock->mightHaveTerminator() ||
          !newCleanupBlock->getTerminator()) {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToEnd(newCleanupBlock);
        // At function scope (depth 0), the parent region is FuncOp which
        // is not a valid parent for cir.yield. Use an implicit return
        // instead (matching the insertCleanupAndLeave depth==0 path).
        if (localScope->depth == 0)
          emitImplicitReturn();
        else
          cir::YieldOp::create(builder, localScope->endLoc);
      }
    }

    // Match OGCG for [[noreturn]] functions under -fsanitize=unreachable:
    // the dead continuation block after a noreturn call still needs the
    // function-scope implicit-return unreachable check.
    if (!newCleanupBlock && localScope->depth == 0) {
      if (const auto *fd =
              dyn_cast_or_null<clang::FunctionDecl>(cgf.curGD.getDecl());
          fd && fd->isNoReturn()) {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToEnd(curBlock);
        emitImplicitReturn();
        // Preserve this synthetic noreturn epilog until LLVM lowering. OGCG
        // materializes the second UBSan unreachable handler in the dead
        // continuation block after a noreturn call chain, and direct-CIR needs
        // the same dead root block to survive FlattenCFG.
        for (mlir::Operation &op : *curBlock) {
          if (isa<cir::IfOp>(op)) {
            op.setAttr(kPreserveDeadUBSanUnreachableBlockAttr,
                       mlir::UnitAttr::get(builder.getContext()));
            break;
          }
        }
        return;
      }
    }

    curBlock->erase();
    for (mlir::Block *retBlock : retBlocks) {
      if (retBlock->getUses().empty())
        retBlock->erase();
    }
    return;
  }

  // If there's a cleanup block, branch to it, nothing else to do.
  if (cleanupBlock) {
    cir::BrOp::create(builder, curBlock->back().getLoc(), cleanupBlock);
    return;
  }

  // No pre-existent cleanup block, emit cleanup code and yield/return.
  insertCleanupAndLeave(curBlock);

  // Clean up any dead blocks left by noreturn cleanup handlers.
  // When a [[noreturn]] destructor fires during cleanup emission, the
  // noreturn call handler emits cir.unreachable and creates a dummy
  // successor block for subsequent codegen. Since the scope is dead
  // after unreachable, the dummy block is never used and must be erased
  // to satisfy the verifier's "last block must be terminated" invariant.
  //
  // The successor block is usually empty, but on ABIs where a noreturn
  // callee still has a non-void return type it is not: the AArch32 C++ ABI
  // has destructors return 'this', so emitCall materializes an undef result
  // value (getUndefRValue) into the dead successor before returning. That
  // value is unreachable and unused, so drop the whole block. Recognize the
  // dead successor by its predecessor ending in cir.unreachable rather than
  // by emptiness alone.
  if (auto *region = curBlock->getParent()) {
    llvm::SmallVector<mlir::Block *, 2> deadBlocks;
    for (mlir::Block &b : *region) {
      if (&b == curBlock || !b.hasNoPredecessors())
        continue;
      if (b.empty()) {
        deadBlocks.push_back(&b);
        continue;
      }
      if (!b.mightHaveTerminator())
        if (mlir::Block *prev = b.getPrevNode();
            prev && !prev->empty() &&
            mlir::isa<cir::UnreachableOp>(prev->back()))
          deadBlocks.push_back(&b);
    }
    for (mlir::Block *b : deadBlocks)
      b->erase();
  }
}

cir::ReturnOp CIRGenFunction::LexicalScope::emitReturn(mlir::Location loc) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  auto [returnAtomGroup, returnAtomRank] = extractSourceAtomMetadata(loc);
  std::optional<llvm::SaveAndRestore<uint64_t>> returnAtomScope;
  if (!cgf.sourceAtomInfo.currentAtom && returnAtomGroup && returnAtomRank == 1)
    returnAtomScope.emplace(cgf.sourceAtomInfo.currentAtom, returnAtomGroup);

  auto fn = dyn_cast<cir::FuncOp>(cgf.curFn);
  assert(fn && "emitReturn from non-function");

  // For coroutines at non-zero depth (inside try body or other nested scope),
  // emit cir.yield instead of cir.return. The coroutine teardown (promise
  // destructor + coro.free + operator delete) must happen AFTER final_suspend,
  // which is emitted at function level. If we emitted cir.return here, it
  // would exit the function before reaching final_suspend. By yielding, we
  // exit the nested region and let control flow to function level where
  // final_suspend + teardown + cir.return are emitted.
  if (fn.getCoroutine() && depth != 0) {
    cir::YieldOp::create(builder, loc);
    return nullptr;
  }

  // Top-level coroutine returns must flow through the shared cleanup block
  // and then the shared coro.ret block. Returning directly here strands the
  // frame deallocation outside the destroy path after coroutine splitting.
  if (fn.getCoroutine()) {
    cir::BrOp::create(builder, loc,
                      cgf.getOrCreateCoroutineCleanupRetBlock(loc));
    return nullptr;
  }
  if (!fn.getFunctionType().hasVoidReturn()) {
    // Load the value from `__retval` and return it via the `cir.return` op.
    mlir::Location valueLoc =
        returnAtomScope ? stripSourceAtomMetadata(loc) : loc;
    auto value = cir::LoadOp::create(builder, valueLoc,
                                     fn.getFunctionType().getReturnType(),
                                     *cgf.fnRetAlloca);
    cgf.emitReturnValueCheck(loc, value);
    mlir::Value retValue = value.getResult();
    if (cgf.autoreleaseResult)
      retValue = cgf.emitARCAutoreleaseReturnValue(retValue, valueLoc);
    auto retOp = cir::ReturnOp::create(builder, loc,
                                       llvm::ArrayRef(retValue));
    cgf.addOpToCurrentSourceAtom(retOp, retValue);
    return retOp;
  }
  auto retOp = cir::ReturnOp::create(builder, loc);
  cgf.addOpToCurrentSourceAtom(retOp);
  return retOp;
}

// This is copied from CodeGenModule::MayDropFunctionReturn.  This is a
// candidate for sharing between CIRGen and CodeGen.
static bool mayDropFunctionReturn(const ASTContext &astContext,
                                  QualType returnType) {
  // We can't just discard the return value for a record type with a complex
  // destructor or a non-trivially copyable type.
  if (const auto *classDecl = returnType->getAsCXXRecordDecl())
    return classDecl->hasTrivialDestructor();
  return returnType.isTriviallyCopyableType(astContext);
}

bool CIRGenFunction::requiresReturnValueCheck() const {
  return requiresReturnValueNullabilityCheck() ||
         (sanOpts.has(SanitizerKind::ReturnsNonnullAttribute) && curCodeDecl &&
          curCodeDecl->getAttr<ReturnsNonNullAttr>());
}

void CIRGenFunction::setReturnValueCheckSourceLocation(SourceLocation loc) {
  if (!requiresReturnValueCheck() || !returnCheckLocation.isValid())
    return;

  mlir::Value storedLocation =
      builder.getNullPtr(builder.getUInt8PtrTy(), getLoc(loc));

  if (loc.isValid()) {
    const SourceManager &sm = getContext().getSourceManager();
    PresumedLoc presumedLoc = sm.getPresumedLoc(loc);
    if (presumedLoc.isValid()) {
      mlir::Location globalLoc = getLoc(loc);
      llvm::StringRef fileName = presumedLoc.getFilename();

      cir::GlobalOp srcGV =
          cgm.getGlobalForConstantCString(globalLoc, fileName, ".src");
      auto srcView = builder.getGlobalViewAttr(builder.getUInt8PtrTy(), srcGV);

      auto srcLocTy = builder.getAnonRecordTy(
          {builder.getUInt8PtrTy(), builder.getSInt32Ty(),
           builder.getSInt32Ty()});
      auto srcLocInit = builder.getAnonConstRecord(
          mlir::ArrayAttr::get(
              &getMLIRContext(),
              {srcView,
               builder.getSInt32AttrLocalCached(
                   static_cast<int32_t>(presumedLoc.getLine())),
               builder.getSInt32AttrLocalCached(
                   static_cast<int32_t>(presumedLoc.getColumn()))}),
          /*packed=*/false, /*padded=*/false, srcLocTy);

      std::string infoName = cgm.getUniqueGlobalName(".ret.nonnull.sloc");
      cir::GlobalOp infoGV = CIRGenModule::createGlobalOp(
          cgm, globalLoc, infoName, srcLocTy, /*isConstant=*/false);
      infoGV.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
      mlir::SymbolTable::setSymbolVisibility(
          infoGV, mlir::SymbolTable::Visibility::Private);
      infoGV->setAttr("unnamed_addr", mlir::UnitAttr::get(&getMLIRContext()));
      infoGV.setAlignmentAttr(cgm.getSize(getPointerAlign()));
      CIRGenModule::setInitializer(infoGV, srcLocInit);
      cgm.setDSOLocal(static_cast<mlir::Operation *>(infoGV));

      auto infoView = builder.getGlobalViewAttr(
          builder.getPointerTo(srcLocTy, infoGV.getAddrSpaceAttr()), infoGV);
      storedLocation = builder.getConstant(globalLoc, infoView);
      if (storedLocation.getType() != builder.getUInt8PtrTy())
        storedLocation = builder.createBitcast(globalLoc, storedLocation,
                                               builder.getUInt8PtrTy());
    }
  }

  auto store = builder.CIRBaseBuilderTy::createStore(
      getLoc(loc.isValid() ? loc : SourceLocation()), storedLocation,
      returnCheckLocation.emitRawPointer());
  store->setAttr("cir.nosanitize", mlir::UnitAttr::get(&getMLIRContext()));
}

void CIRGenFunction::emitReturnValueCheck(mlir::Location loc, mlir::Value rv) {
  if (!requiresReturnValueCheck() || !curCodeDecl ||
      !returnCheckLocation.isValid())
    return;

  ReturnsNonNullAttr *retNNAttr = nullptr;
  if (sanOpts.has(SanitizerKind::ReturnsNonnullAttribute))
    retNNAttr = curCodeDecl->getAttr<ReturnsNonNullAttr>();

  bool checkNullability = !retNNAttr && requiresReturnValueNullabilityCheck();
  if (!retNNAttr && !checkNullability)
    return;

  auto ptrTy = mlir::dyn_cast<cir::PointerType>(rv.getType());
  if (!ptrTy || ptrTy.getAddrSpace())
    return;

  SourceLocation attrLoc;
  auto checkKind = SanitizerKind::SO_ReturnsNonnullAttribute;
  auto trapHandlerID = int8_t{17};
  llvm::StringRef handlerName = "nonnull_return";
  if (retNNAttr) {
    attrLoc = retNNAttr->getLocation();
  } else {
    if (auto *dd = dyn_cast<DeclaratorDecl>(curCodeDecl))
      if (auto *tsi = dd->getTypeSourceInfo())
        if (auto ftl = tsi->getTypeLoc().getAsAdjusted<FunctionTypeLoc>())
          attrLoc = ftl.getReturnLoc().findNullabilityLoc();
    if (auto *omd = dyn_cast<ObjCMethodDecl>(curCodeDecl))
      if (auto *tsi = omd->getReturnTypeSourceInfo())
        attrLoc = tsi->getTypeLoc().findNullabilityLoc();
    checkKind = SanitizerKind::SO_NullabilityReturn;
    trapHandlerID = 15;
    handlerName = "nullability_return";
  }

  const SourceManager &sm = getContext().getSourceManager();
  PresumedLoc presumedLoc = sm.getPresumedLoc(attrLoc);

  mlir::Location checkLoc = loc;
  auto slocLoad = builder.createLoad(checkLoc, returnCheckLocation);
  slocLoad->setAttr("cir.nosanitize", mlir::UnitAttr::get(&getMLIRContext()));
  mlir::Value slocPtr = slocLoad.getResult();

  auto nullI8Ptr = builder.getNullPtr(slocPtr.getType(), checkLoc);
  mlir::Value canCheck =
      cir::CmpOp::create(builder, checkLoc, cir::CmpOpKind::ne, slocPtr,
                         nullI8Ptr);
  canCheck.getDefiningOp()->setAttr("cir.nosanitize",
                                    mlir::UnitAttr::get(&getMLIRContext()));
  if (checkNullability) {
    canCheck =
        builder.createAnd(checkLoc, canCheck, retValNullabilityPrecondition);
    canCheck.getDefiningOp()->setAttr("cir.nosanitize",
                                      mlir::UnitAttr::get(&getMLIRContext()));
  }

  auto outerIf = cir::IfOp::create(
      builder, checkLoc, canCheck, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &, mlir::Location ifLoc) {
        auto nullPtr = builder.getNullPtr(rv.getType(), checkLoc);
        auto isNonNull = cir::CmpOp::create(builder, checkLoc,
                                            cir::CmpOpKind::ne, rv, nullPtr);
        isNonNull->setAttr("cir.nosanitize",
                           mlir::UnitAttr::get(&getMLIRContext()));

        auto innerIf = cir::IfOp::create(
            builder, checkLoc, isNonNull, /*withElseRegion=*/true,
            [&](mlir::OpBuilder &, mlir::Location innerLoc) {
              auto yield = cir::YieldOp::create(builder, innerLoc);
              yield->setAttr("cir.nosanitize",
                             mlir::UnitAttr::get(&getMLIRContext()));
            },
            [&](mlir::OpBuilder &, mlir::Location innerLoc) {
              if (cgm.getCodeGenOpts().SanitizeTrap.has(checkKind)) {
                auto trap = cir::TrapOp::create(builder, checkLoc);
                trap->setAttr(
                    "cir.ubsantrap_handler",
                    mlir::IntegerAttr::get(
                        mlir::IntegerType::get(builder.getContext(), 8),
                        trapHandlerID));
                trap->setAttr("cir.nosanitize",
                              mlir::UnitAttr::get(&getMLIRContext()));
                return;
              }

              auto intptrTy = mlir::cast<cir::IntType>(
                  convertType(getContext().getUIntPtrType()));
              auto ptrAsInt = builder.createPtrToInt(slocPtr, intptrTy);
              if (auto *op = ptrAsInt.getDefiningOp())
                op->setAttr("cir.nosanitize",
                            mlir::UnitAttr::get(&getMLIRContext()));

              auto stubFnTy =
                  cir::FuncType::get({intptrTy}, builder.getVoidTy());
              auto stubFn = cgm.createRuntimeFunction(
                  stubFnTy, "__cir_ubsan_nonnull_return", /*attrs=*/{},
                  /*isLocal=*/false, /*assumeConvergent=*/false);
              auto call = builder.createCallOp(checkLoc, stubFn, {ptrAsInt});
              call->setAttr("cir.call_nomerge",
                            mlir::UnitAttr::get(&getMLIRContext()));
              if (presumedLoc.isValid()) {
                call->setDiscardableAttr("cir.ubsan.attr.source_file",
                                         builder.getStringAttr(
                                             presumedLoc.getFilename()));
                call->setDiscardableAttr(
                    "cir.ubsan.attr.source_line",
                    builder.getI32IntegerAttr(presumedLoc.getLine()));
                call->setDiscardableAttr(
                    "cir.ubsan.attr.source_column",
                    builder.getI32IntegerAttr(presumedLoc.getColumn()));
              }
              call->setDiscardableAttr("cir.ubsan.handler_name",
                                       builder.getStringAttr(handlerName));
              StringRef handlerKind = cgm.getCodeGenOpts().SanitizeRecover.has(
                                          checkKind)
                                          ? "recover"
                                          : "abort";
              call->setDiscardableAttr("cir.ubsan.handler_kind",
                                       builder.getStringAttr(handlerKind));
              call->setDiscardableAttr("cir.nosanitize",
                                       mlir::UnitAttr::get(&getMLIRContext()));

              if (handlerKind == "abort") {
                auto unreachable =
                    cir::UnreachableOp::create(builder, checkLoc);
                unreachable->setAttr("cir.nosanitize",
                                     mlir::UnitAttr::get(&getMLIRContext()));
              } else {
                auto yield = cir::YieldOp::create(builder, innerLoc);
                yield->setAttr("cir.nosanitize",
                               mlir::UnitAttr::get(&getMLIRContext()));
              }
            });
        innerIf->setAttr(
            "cir.branch_weights",
            builder.getArrayAttr({builder.getI32IntegerAttr(1024 * 1024 - 1),
                                  builder.getI32IntegerAttr(1)}));
        innerIf->setAttr("cir.nosanitize",
                         mlir::UnitAttr::get(&getMLIRContext()));

        auto outerYield = cir::YieldOp::create(builder, ifLoc);
        outerYield->setAttr("cir.nosanitize",
                            mlir::UnitAttr::get(&getMLIRContext()));
      });
  // Match OGCG: the outer "can we perform the check?" guard is unweighted.
  // Only the inner handler-vs-continue branch carries likely weights.
  outerIf->setAttr("cir.nosanitize", mlir::UnitAttr::get(&getMLIRContext()));
}

void CIRGenFunction::LexicalScope::emitImplicitReturn() {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  LexicalScope *localScope = cgf.curLexScope;
  uint64_t returnAtom = cgf.reserveSourceAtomGroup();

  // CapturedDecl functions always return void — just emit a void return.
  const auto *fd =
      dyn_cast_or_null<clang::FunctionDecl>(cgf.curGD.getDecl());
  if (!fd) {
    (void)emitReturn(
        cgf.applySourceAtomMetadata(localScope->endLoc, returnAtom, 1));
    return;
  }

  // Naked functions have no compiler-generated prologue/epilogue, so falling
  // off the end is UB. Emit unreachable instead of ret void, matching OGCG's
  // EmitFunctionEpilog (CGCall.cpp:4009-4012).
  if (fd->hasAttr<NakedAttr>()) {
    cir::UnreachableOp::create(builder, localScope->endLoc);
    builder.clearInsertionPoint();
    return;
  }

  // In C++, flowing off the end of a non-void function is always undefined
  // behavior. In C, flowing off the end of a non-void function is undefined
  // behavior only if the non-existent return value is used by the caller.
  // That influences whether the terminating op is trap, unreachable, or
  // return.
  //
  // This check must run before the isNoReturn() early-return below so that
  // non-void [[noreturn]] C++ functions get the trap+unreachable at -O0
  // (matching OGCG's CodeGenFunction.cpp flow where the trap is emitted
  // before EmitFunctionEpilog handles the noreturn case).
  if (cgf.getLangOpts().CPlusPlus && !fd->hasImplicitReturnZero() &&
      !cgf.sawAsmBlock && !fd->getReturnType()->isVoidType() &&
      builder.getInsertionBlock()) {
    bool shouldEmitUnreachable =
        cgf.cgm.getCodeGenOpts().StrictReturn ||
        !mayDropFunctionReturn(fd->getASTContext(), fd->getReturnType());

    if (cgf.sanOpts.has(SanitizerKind::Return)) {
      mlir::Location loc = cgf.getLoc(fd->getLocation());
      auto noSanitize = mlir::UnitAttr::get(builder.getContext());

      // Use a constant-true condition so the handler is in the taken (true)
      // branch of the cir.if. OGCG uses 'br i1 false, cont, handler' which
      // puts the handler on the false branch. CIR's if-op puts the body in
      // the true region, so we need 'true' to match the always-taken
      // semantics. The branch weights mark this as overwhelmingly expected
      // (the handler fires when falling off a non-void function, which
      // should "never happen" in well-formed code, but the sanitizer
      // unconditionally fires to report the UB).
      auto trueVal =
          cir::ConstantOp::create(builder, loc, builder.getTrueAttr());
      auto ifOp = cir::IfOp::create(
          builder, loc, trueVal, /*withElseRegion=*/false,
          [&](mlir::OpBuilder &, mlir::Location ifLoc) {
            if (cgf.cgm.getCodeGenOpts().SanitizeTrap.has(
                    SanitizerKind::Return)) {
              auto trap = cir::TrapOp::create(builder, ifLoc);
              trap->setAttr(
                  "cir.ubsantrap_handler",
                  builder.getI8IntegerAttr(kMissingReturnTrapHandlerID));
              trap->setAttr("cir.nosanitize", noSanitize);
              return;
            }

            auto stubFnTy = cir::FuncType::get({}, builder.getVoidTy());
            auto stubFn = cgf.cgm.createRuntimeFunction(
                stubFnTy, "__cir_ubsan_missing_return", /*attrs=*/{},
                /*isLocal=*/false, /*assumeConvergent=*/false);
            auto call = builder.createCallOp(ifLoc, stubFn, {});

            PresumedLoc presumedLoc =
                cgf.getContext().getSourceManager().getPresumedLoc(
                    fd->getLocation());
            if (presumedLoc.isValid()) {
              call->setDiscardableAttr("cir.ubsan.source_file",
                                       builder.getStringAttr(
                                           presumedLoc.getFilename()));
              call->setDiscardableAttr(
                  "cir.ubsan.source_line",
                  builder.getI32IntegerAttr(presumedLoc.getLine()));
              call->setDiscardableAttr(
                  "cir.ubsan.source_column",
                  builder.getI32IntegerAttr(presumedLoc.getColumn()));
            }

            call->setDiscardableAttr("cir.ubsan.handler_kind",
                                     builder.getStringAttr("abort"));
            call->setDiscardableAttr("cir.ubsan.handler_name",
                                     builder.getStringAttr("missing_return"));
            call->setDiscardableAttr("cir.nosanitize", noSanitize);

            auto unreachable = cir::UnreachableOp::create(builder, ifLoc);
            unreachable->setAttr("cir.nosanitize", noSanitize);
          });
      ifOp->setAttr(
          "cir.branch_weights",
          builder.getArrayAttr({builder.getI32IntegerAttr(1024 * 1024 - 1),
                                builder.getI32IntegerAttr(1)}));
      ifOp->setAttr("cir.nosanitize", noSanitize);

      cir::UnreachableOp::create(builder, localScope->endLoc);
      builder.clearInsertionPoint();
      return;
    }

    if (shouldEmitUnreachable) {
      if (cgf.cgm.getCodeGenOpts().OptimizationLevel == 0) {
        auto trapOp = cir::TrapOp::create(builder, localScope->endLoc);
        // Attach trap-func-name from -ftrap-function flag.
        const std::string &trapFuncName =
            cgf.cgm.getCodeGenOpts().TrapFuncName;
        if (!trapFuncName.empty())
          trapOp->setAttr(
              "cir.trap_func_name",
              cgf.cgm.getStringAttrCached(trapFuncName));
      } else
        cir::UnreachableOp::create(builder, localScope->endLoc);
      builder.clearInsertionPoint();
      return;
    }
  }

  // Noreturn functions don't return — emit unreachable (matching OGCG's
  // EmitFunctionEpilog, CGCall.cpp:4003-4006). Use the function TYPE's
  // noreturn bit, not the declaration's isNoReturn(), because _Noreturn
  // and [[noreturn]] set the decl attribute but don't set the type's
  // noreturn bit. Only __attribute__((noreturn)) sets the type's noreturn
  // bit, and OGCG only inserts unreachable for that case.
  if (fd->getType()->getAs<clang::FunctionType>()->getNoReturnAttr()) {
    SourceLocation endLoc = fd->getLocation();
    if (Stmt *body = fd->getBody())
      endLoc = body->getEndLoc();
    cgf.emitUnreachable(endLoc, /*createNewBlock=*/false);
    builder.clearInsertionPoint();
    return;
  }

  if (cgf.requiresReturnValueCheck())
    cgf.setReturnValueCheckSourceLocation(SourceLocation());

  (void)emitReturn(
      cgf.applySourceAtomMetadata(localScope->endLoc, returnAtom, 1));
}

cir::TryOp CIRGenFunction::LexicalScope::getClosestTryParent() {
  LexicalScope *scope = this;
  while (scope) {
    if (scope->isTry())
      return scope->getTry();
    scope = scope->parentScope;
  }
  return nullptr;
}

/// An argument came in as a promoted argument; demote it back to its
/// declared type.
static mlir::Value emitArgumentDemotion(CIRGenFunction &cgf, const VarDecl *var,
                                        mlir::Value value) {
  mlir::Type ty = cgf.convertType(var->getType());

  // This can happen with promotions that actually don't change the
  // underlying type, like the enum promotions.
  if (value.getType() == ty)
    return value;

  assert((mlir::isa<cir::IntType>(ty) || cir::isAnyFloatingPointType(ty)) &&
         "unexpected promotion type");

  if (mlir::isa<cir::IntType>(ty))
    return cgf.getBuilder().CIRBaseBuilderTy::createIntCast(value, ty);

  return cgf.getBuilder().createFloatingCast(value, ty);
}

void CIRGenFunction::emitFunctionProlog(const FunctionArgList &args,
                                        mlir::Block *entryBB,
                                        const FunctionDecl *fd,
                                        SourceLocation bodyBeginLoc) {
  // Naked functions don't have prologues.
  if (fd && fd->hasAttr<NakedAttr>())
    return;

  openMPTargetEscapedLocalDecls.clear();
  emittingOpenMPTargetGenericFunction = false;
  if (fd && cgm.shouldUseOpenMPTargetSharedLocals(fd) && fd->hasBody()) {
    openMPTargetEscapedLocalDecls =
        collectOpenMPTargetEscapedDecls(*this, fd->getBody());
    cgm.pushOpenMPTargetGenericEmission();
    emittingOpenMPTargetGenericFunction = true;
  }

  // Declare all the function arguments in the symbol table.
  auto funcOp = cast<cir::FuncOp>(entryBB->getParentOp());
  unsigned blockArgIdx = 0;
  for (const VarDecl *paramVar : args) {
    assert(blockArgIdx < entryBB->getNumArguments() &&
           "Ran out of block arguments");
    mlir::Value paramVal = entryBB->getArgument(blockArgIdx);
    ++blockArgIdx;

    CharUnits alignment = getContext().getDeclAlign(paramVar);
    mlir::Location paramLoc = getLoc(paramVar->getSourceRange());
    QualType ty = paramVar->getType();
    paramVal.setLoc(paramLoc);

    mlir::Type paramMemTy =
        ty->isBitIntType() ? convertTypeForMem(ty) : convertType(ty);
    Address allocaAddr = Address::invalid();
    Address paramAddr =
        createTempAlloca(paramMemTy, alignment, paramLoc,
                         cast<NamedDecl>(paramVar)->getName(),
                         /*arraySize=*/nullptr, &allocaAddr,
                         /*ip=*/{}, cgm.getLangTempAllocaAddressSpace());

    declare(allocaAddr.getPointer(), paramVar, paramVar->getType(), paramLoc,
            alignment,
            /*isParam=*/true);

    setAddrOfLocalVar(paramVar, paramAddr);

    // Set 'noalias' if the parameter type has the 'restrict' qualifier.
    // This matches OGCG's behavior in EmitFunctionProlog (CGCall.cpp:3322).
    if (paramVar->getType().isRestrictQualified())
      funcOp.setArgAttr(blockArgIdx - 1, "cir.noalias",
                        builder.getUnitAttr());

    // Set 'swiftasync', 'swiftself', or 'swifterror' based on the parameter
    // ABI attribute. OGCG processes these through ExtParameterInfo ABI
    // (CGCall.cpp:3001-3011), but CIR doesn't have the ExtParameterInfo
    // plumbing yet. Instead, check the attribute directly on the ParmVarDecl.
    if (isa<ParmVarDecl>(paramVar)) {
      if (paramVar->hasAttr<SwiftAsyncContextAttr>())
        funcOp.setArgAttr(blockArgIdx - 1, "cir.swiftasync",
                          builder.getUnitAttr());
      else if (paramVar->hasAttr<SwiftContextAttr>())
        funcOp.setArgAttr(blockArgIdx - 1, "cir.swiftself",
                          builder.getUnitAttr());
      else if (paramVar->hasAttr<SwiftErrorResultAttr>())
        funcOp.setArgAttr(blockArgIdx - 1, "cir.swifterror",
                          builder.getUnitAttr());
      if (paramVar->hasAttr<NoEscapeAttr>())
        funcOp.setArgAttr(blockArgIdx - 1, "cir.noescape",
                          builder.getUnitAttr());
    }

    if (paramVar->hasAttr<AnnotateAttr>())
      emitVarAnnotations(paramVar, paramAddr.emitRawPointer());

    bool isPromoted = isa<ParmVarDecl>(paramVar) &&
                      cast<ParmVarDecl>(paramVar)->isKNRPromoted();
    if (isPromoted)
      paramVal = emitArgumentDemotion(*this, paramVar, paramVal);

    // Location of the store to the param storage tracked as beginning of
    // the function body.
    mlir::Location fnBodyBegin = getLoc(bodyBeginLoc);
    TBAAAccessInfo paramStoreTBAA = cgm.getTBAAAccessInfo(ty);
    bool doStore = true;

    if (hasScalarEvaluationKind(ty)) {
      Qualifiers quals = ty.getQualifiers();
      if (Qualifiers::ObjCLifetime lifetime = quals.getObjCLifetime()) {
        bool isConsumed = paramVar->hasAttr<NSConsumedAttr>();

        if (paramVar->isARCPseudoStrong()) {
          assert(lifetime == Qualifiers::OCL_Strong &&
                 "pseudo-strong variable isn't strong?");
          assert(quals.hasConst() && "pseudo-strong variable should be const!");
          lifetime = Qualifiers::OCL_ExplicitNone;
        }

        if (lifetime == Qualifiers::OCL_Strong) {
          if (!isConsumed) {
            if (cgm.getCodeGenOpts().OptimizationLevel == 0) {
              mlir::Value null =
                  builder.getNullValue(paramAddr.getElementType(), fnBodyBegin);
              auto storeOp = builder.CIRBaseBuilderTy::createStore(
                  fnBodyBegin, null, paramAddr.emitRawPointer());
              cgm.decorateOperationWithTBAA(storeOp, paramStoreTBAA);
              emitARCStoreStrongCall(paramAddr, paramVal, fnBodyBegin);
              doStore = false;
            } else {
              paramVal = emitARCRetainNonBlock(paramVal, fnBodyBegin);
            }
          }

          if (!paramVar->isARCPseudoStrong()) {
            Destroyer *destroyer = paramVar->hasAttr<ObjCPreciseLifetimeAttr>()
                                       ? destroyARCStrongPrecise
                                       : destroyARCStrongImprecise;
            pushDestroy(getCleanupKind(QualType::DK_objc_strong_lifetime),
                        paramAddr, ty, destroyer,
                        needsEHCleanup(QualType::DK_objc_strong_lifetime));
          }
        } else {
          if (isConsumed)
            emitARCConsumeObject(paramVal, fnBodyBegin);

          if (lifetime == Qualifiers::OCL_Weak) {
            emitARCInitWeak(paramAddr, paramVal);
            pushDestroy(QualType::DK_objc_weak_lifetime, paramAddr, ty);
            doStore = false;
          }
        }
      }
    }

    // K&R-style function definitions or transparent_union may have different
    // parameter types in the definition vs the prototype.
    if (doStore && paramVal.getType() != paramAddr.getElementType() &&
        (mlir::isa<cir::RecordType>(paramVal.getType()) ||
         mlir::isa<cir::RecordType>(paramAddr.getElementType()))) {
      // Value-level bitcast is invalid for record (aggregate) types.
      // Instead, bitcast the pointer and store through it. This matches
      // OGCG's coerce-dive approach (store scalar into union's first field
      // via opaque pointer).
      auto dstPtrTy = cir::PointerType::get(paramVal.getType());
      mlir::Value ptr = builder.createBitcast(fnBodyBegin,
                                              paramAddr.emitRawPointer(),
                                              dstPtrTy);
      mlir::IntegerAttr recStoreAlign;
      CharUnits recNaturalAlign =
          cgm.getNaturalTypeAlignment(paramVar->getType(), nullptr);
      if (paramAddr.getAlignment() != recNaturalAlign)
        recStoreAlign = builder.getI64IntegerAttr(
            paramAddr.getAlignment().getQuantity());
      auto storeOp =
          builder.CIRBaseBuilderTy::createStore(
              fnBodyBegin, paramVal, ptr,
              /*isVolatile=*/ty.isVolatileQualified(), recStoreAlign);
      cgm.decorateOperationWithTBAA(storeOp, paramStoreTBAA);
    } else if (doStore) {
      if (paramVal.getType() != paramAddr.getElementType()) {
        if (ty->isBitIntType())
          paramVal = emitToMemory(paramVal, ty);
        if (paramVal.getType() != paramAddr.getElementType())
          paramVal = builder.createBitcast(fnBodyBegin, paramVal,
                                           paramAddr.getElementType());
      }
      // Use the alloca's alignment for the initial parameter store when
      // it differs from the natural alignment (e.g., overaligned params
      // with __attribute__((aligned(N)))). Also always set explicit
      // alignment for _BitInt types, where the CIR-level natural alignment
      // may match the alloca but the LLVM-level i128 alignment (16) can
      // exceed the alloca alignment (8), creating misaligned stores.
      mlir::IntegerAttr storeAlign;
      CharUnits naturalAlign =
          cgm.getNaturalTypeAlignment(paramVar->getType(), nullptr);
      if (paramAddr.getAlignment() != naturalAlign || ty->isBitIntType())
        storeAlign = builder.getI64IntegerAttr(
            paramAddr.getAlignment().getQuantity());
      // When emitting debug info, the store of an incoming argument into its
      // stack slot is part of the prologue and carries no source location
      // (matching classic CodeGen, which leaves these stores bare even under
      // Key Instructions), so the backend places prologue_end on the first real
      // statement rather than on the function's opening brace. Otherwise a
      // function breakpoint would stop before the arguments are spilled,
      // showing uninitialized argument values. With debug info disabled, keep
      // the body location so non-debug consumers (e.g. MLIR analysis passes)
      // still have an accurate location for these stores.
      mlir::Location paramStoreLoc = fnBodyBegin;
      if (cgm.getCodeGenOpts().getDebugInfo() !=
          llvm::codegenoptions::NoDebugInfo)
        paramStoreLoc = mlir::UnknownLoc::get(&getMLIRContext());
      auto storeOp = builder.CIRBaseBuilderTy::createStore(
          paramStoreLoc, paramVal, paramAddr.emitRawPointer(),
          /*isVolatile=*/ty.isVolatileQualified(), storeAlign);
      cgm.decorateOperationWithTBAA(storeOp, paramStoreTBAA);
    }

    // Push a destructor cleanup for this parameter if the ABI requires it.
    // Don't push a cleanup in a thunk for a method that will also emit a
    // cleanup.
    if (ty->isRecordType() && !curFuncIsThunk &&
        ty->castAsRecordDecl()->isParamDestroyedInCallee()) {
      if (QualType::DestructionKind dtorKind =
              paramVar->needsDestruction(getContext())) {
        assert((dtorKind == QualType::DK_cxx_destructor ||
                dtorKind == QualType::DK_nontrivial_c_struct) &&
               "unexpected destructor type");
        pushDestroy(dtorKind, paramAddr, ty);
        calleeDestructedParamCleanups[cast<ParmVarDecl>(paramVar)] =
            ehStack.stable_begin();
      }
    }

    // OGCG EmitParmDecl pushes FakeUse cleanup for -fextend-variable-liveness.
    // Parameters get the same treatment as local variables.
    pushFakeUseCleanupForVar(*paramVar, paramAddr);

    // OGCG EmitParmDecl emits debug info for arg variables via
    // DI->EmitDeclareOfArgVariable and tracks ParamDbgMappings.
    // CIR currently has no dedicated parameter/local debug-declare op in
    // CIRGen; debug metadata materialization is deferred to lowering.

    if (requiresReturnValueNullabilityCheck()) {
      auto nullability = ty->getNullability();
      if (nullability && *nullability == NullabilityKind::NonNull) {
        auto nonNull =
            cir::CmpOp::create(builder, fnBodyBegin, cir::CmpOpKind::ne,
                               paramVal, builder.getNullValue(paramVal.getType(),
                                                              fnBodyBegin));
        nonNull->setAttr("cir.nosanitize",
                         mlir::UnitAttr::get(&getMLIRContext()));
        retValNullabilityPrecondition =
            builder.createAnd(fnBodyBegin, retValNullabilityPrecondition,
                              nonNull);
        retValNullabilityPrecondition.getDefiningOp()->setAttr(
            "cir.nosanitize", mlir::UnitAttr::get(&getMLIRContext()));
      }
    }
  }
  assert(builder.getInsertionBlock() && "Should be valid");
}

void CIRGenFunction::startFunction(GlobalDecl gd, QualType returnType,
                                   cir::FuncOp fn, cir::FuncType funcType,
                                   FunctionArgList args, SourceLocation loc,
                                   SourceLocation startLoc) {
  assert(!curFn &&
         "CIRGenFunction can only be used for one function at a time");

  curFn = fn;
  completedFunctionEmissionCountAtStart =
      cgm.getCompletedFunctionEmissionCount();

  const Decl *d = gd.getDecl();

  didCallStackSave = false;
  autoreleaseResult = false;
  retValNullabilityPrecondition = nullptr;
  curCodeDecl = d;
  curFnRetTy = returnType;
  const auto *fd = dyn_cast_or_null<FunctionDecl>(d);
  curFuncDecl = d ? d->getNonClosureContext() : nullptr;

  prologueCleanupDepth = ehStack.stable_begin();

  mlir::Block *entryBB = &fn.getBlocks().front();
  builder.setInsertionPointToStart(entryBB);

  // Set function-level constrained FP flag, matching OGCG's
  // StartFunction (CodeGenFunction.cpp:1086-1096). When this is true, all
  // FP operations in the function emit constrained intrinsics even if the
  // per-expression FPOptions have exception mode "ignore". The strictfp
  // function attribute is set in setCIRFunctionAttributesForDefinition.
  if (const auto *fd = dyn_cast_or_null<FunctionDecl>(d)) {
    if (fd->UsesFPIntrin() || fd->hasAttr<StrictFPAttr>())
      IsFPConstrained = true;
  } else if (d) {
    llvm::RoundingMode rm = getLangOpts().getDefaultRoundingMode();
    auto fpExcept = getLangOpts().getDefaultExceptionMode();
    if (fpExcept != clang::LangOptions::FPE_Ignore ||
        rm != llvm::RoundingMode::NearestTiesToEven)
      IsFPConstrained = true;
  }

  // Initialize per-function sanitizer options, matching OGCG's
  // StartFunction (CodeGenFunction.cpp:776-893).
  sanOpts.set(getLangOpts().Sanitize.Mask);
  do {
#define SANITIZER(NAME, ID)                                                    \
  if (sanOpts.empty())                                                         \
    break;                                                                     \
  if (sanOpts.has(SanitizerKind::ID) &&                                        \
      cgm.isInNoSanitizeList(SanitizerKind::ID, fn.getSymName(), loc))         \
    sanOpts.clear(SanitizerKind::ID);
#include "clang/Basic/Sanitizers.def"
#undef SANITIZER
  } while (false);
  if (d) {
    for (auto *attr : d->specific_attrs<NoSanitizeAttr>())
      sanOpts.clear(attr->getMask());
    if (d->hasAttr<DisableSanitizerInstrumentationAttr>())
      sanOpts.clear();
  }

  // Ignore unrelated casts in STL allocate() since the allocator must cast
  // from void* to T* before object initialization completes.
  if (d && sanOpts.has(SanitizerKind::CFIUnrelatedCast) &&
      matchesStlAllocatorFn(d, getContext()))
    sanOpts.clear(SanitizerKind::CFIUnrelatedCast);

  retValNullabilityPrecondition = nullptr;
  returnCheckLocation = Address::invalid();

  // XRay instrumentation attributes are applied in
  // setCIRFunctionAttributesForDefinition (CIRGenModule.cpp).

  if (fd && sanOpts.has(SanitizerKind::Function) &&
      !fd->getType()->isCFIUncheckedCalleeFunctionType()) {
    if (auto prologueSig = cgm.getUBSanFunctionSignature(fd)) {
      curFn->setAttr(
          "cir.func_sanitize",
          mlir::ArrayAttr::get(
              &getMLIRContext(),
              {builder.getI32IntegerAttr(static_cast<int32_t>(*prologueSig)),
               builder.getI32IntegerAttr(static_cast<int32_t>(
                   cgm.createUBSanFunctionTypeHash(fd->getType())))}));
    }
  }

  if (sanOpts.has(SanitizerKind::NullabilityReturn)) {
    auto nullability = returnType->getNullability();
    if (nullability && *nullability == NullabilityKind::NonNull &&
        !returnType->isRecordType()) {
      if (!(sanOpts.has(SanitizerKind::ReturnsNonnullAttribute) && curCodeDecl &&
            curCodeDecl->getAttr<ReturnsNonNullAttr>()))
        retValNullabilityPrecondition = builder.getTrue(getLoc(loc));
    }
  }

  // OGCG checks isFunctionBlockedFromProfileInstr and adds SkipProfile or
  // NoProfile attributes (CodeGenFunction.cpp:943-955).
  // CIR's profile-instrumentation attr handling no longer tracks this as a
  // MissingFeatures guard.

  // Patchable function entry attributes are set in
  // setCIRFunctionAttributesForDefinition (matching OGCG
  // CodeGenFunction.cpp:957-985).

  // -finstrument-functions attributes are set in
  // setCIRFunctionAttributesForDefinition.

  // mcount/fentry profiling attributes (-pg / -mfentry / -mnop-mcount /
  // -mrecord-mcount) are set in setCIRFunctionAttributesForDefinition.

  // Compute and add vscale_range attribute, matching OGCG
  // (CodeGenFunction.cpp:1108-1127).
  {
    llvm::StringMap<bool> featureMap;
    auto isArmStreaming = TargetInfo::ArmStreamingKind::NotStreaming;
    if (fd) {
      getContext().getFunctionFeatureMap(featureMap, fd);
      if (const auto *t = fd->getType()->getAs<FunctionProtoType>())
        if (t->getAArch64SMEAttributes() &
            FunctionType::SME_PStateSMCompatibleMask)
          isArmStreaming = TargetInfo::ArmStreamingKind::StreamingCompatible;

      if (IsArmStreamingFunction(fd, true))
        isArmStreaming = TargetInfo::ArmStreamingKind::Streaming;
    }
    std::optional<std::pair<unsigned, unsigned>> vscaleRange =
        getContext().getTargetInfo().getVScaleRange(getLangOpts(), isArmStreaming,
                                                   &featureMap);
    if (vscaleRange) {
      if (auto fn = mlir::dyn_cast<cir::FuncOp>(curFn)) {
        mlir::NamedAttrList attrs;
        if (auto existingAttrs = fn.getExtraAttrs())
          attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
        attrs.set("vscale_range",
                  mlir::DenseI32ArrayAttr::get(&cgm.getMLIRContext(),
                                               {static_cast<int32_t>(
                                                    vscaleRange->first),
                                                static_cast<int32_t>(
                                                    vscaleRange->second)}));
        fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
            attrs.getDictionary(&cgm.getMLIRContext())));
      }
    }
  }

  // Initialize LargestVectorWidth from MinVectorWidthAttr if present.
  // Matches OGCG StartFunction (CodeGenFunction.cpp:1363-1365).
  if (curFuncDecl)
    if (const auto *vecWidth = curFuncDecl->getAttr<MinVectorWidthAttr>())
      largestVectorWidth = vecWidth->getVectorWidth();

  // Determine the function body begin location for the prolog.
  // If fd is null or has no body, use startLoc as fallback.
  SourceLocation bodyBeginLoc = startLoc;
  if (fd) {
    if (Stmt *body = fd->getBody())
      bodyBeginLoc = body->getBeginLoc();
    else
      bodyBeginLoc = fd->getLocation();
  }

  emitFunctionProlog(args, entryBB, fd, bodyBeginLoc);

  // Set nonnull/align/dereferenceable attributes on reference parameters.
  // OGCG does this in ConstructAttributeList (CGCall.cpp:2942-2954) using
  // QualType, which is available here but not in the target lowering layer.
  {
    unsigned argIdx = 0;
    for (const VarDecl *paramVar : args) {
      QualType paramType = paramVar->getType();
      const auto *pvd = dyn_cast<ParmVarDecl>(paramVar);
      const AlignValueAttr *alignValueAttr = nullptr;
      if (pvd) {
        alignValueAttr = pvd->getAttr<AlignValueAttr>();
        if (!alignValueAttr) {
          QualType originalTy = pvd->getOriginalType();
          if (const auto *typedefTy = originalTy->getAs<TypedefType>())
            alignValueAttr = typedefTy->getDecl()->getAttr<AlignValueAttr>();
        }
      }

      auto maybeSetArgAlign = [&](unsigned align) {
        if (align == 0)
          return;
        unsigned finalAlign = align;
        if (auto existingAlign = fn.getArgAttrOfType<mlir::IntegerAttr>(
                argIdx, "cir.align"))
          finalAlign = std::max<unsigned>(finalAlign, existingAlign.getInt());
        fn.setArgAttr(argIdx, "cir.align", getI64AttrCached(finalAlign));
      };

      if (const auto *refTy = paramType->getAs<ReferenceType>()) {
        QualType pointeeTy = refTy->getPointeeType();
        // dereferenceable(N) — minimum object size (non-virtual size for
        // non-final classes). Matches OGCG getMinimumObjectSize.
        if (!pointeeTy->isIncompleteType() && pointeeTy->isConstantSizeType()) {
          uint64_t size =
              cgm.getMinimumObjectSize(pointeeTy).getQuantity();
          // LLVM's addDereferenceableAttr drops size 0 (e.g. function types).
          if (size > 0)
            fn.setArgAttr(argIdx, "cir.dereferenceable",
                          getI64AttrCached(size));
        }
        // nonnull — references cannot be null (only for default address
        // space). Matches OGCG CGCall.cpp:2946-2948.
        if (cgm.getASTContext().getTargetAddressSpace(
                pointeeTy.getAddressSpace()) == 0 &&
            !cgm.getCodeGenOpts().NullPointerIsValid)
          fn.setArgAttr(argIdx, "cir.nonnull",
                        mlir::UnitAttr::get(&cgm.getMLIRContext()));
        if (cgm.getASTContext().getTargetAddressSpace(
                pointeeTy.getAddressSpace()) == 0 &&
            !cgm.getCodeGenOpts().NullPointerIsValid)
          fn.setArgAttr(argIdx, "cir.callsite_nonnull",
                        mlir::UnitAttr::get(&cgm.getMLIRContext()));
        // align N — natural alignment of the pointee type.
        // Uses getNaturalTypeAlignment which handles incomplete array types
        // (via getBaseElementType), CXX record non-virtual alignment, etc.
        // Matches OGCG's getNaturalPointeeTypeAlignment call in
        // CGCall.cpp:2951.
        if (pointeeTy->isObjectType()) {
          unsigned align =
              cgm.getNaturalTypeAlignment(pointeeTy, /*baseInfo=*/nullptr,
                                          /*tbaaInfo=*/nullptr,
                                          /*forPointeeType=*/true)
                  .getQuantity();
          maybeSetArgAlign(align);
        }
      }

      // C99 static array parameters: int a[static 10] implies
      // dereferenceable(N) + align(M) on the decayed pointer.
      // Matches OGCG CGCall.cpp:3257-3298 (EmitFunctionProlog).
      if (pvd && paramType->isPointerType()) {
        QualType originalTy = pvd->getOriginalType();
        if (const auto *arrTy =
                getContext().getAsConstantArrayType(originalTy)) {
          if (arrTy->getSizeModifier() == ArraySizeModifier::Static) {
            QualType elemTy = arrTy->getElementType();
            unsigned align =
                cgm.getNaturalTypeAlignment(elemTy, nullptr).getAsAlign().value();
            maybeSetArgAlign(align);
            uint64_t arrSize = arrTy->getZExtSize();
            if (!elemTy->isIncompleteType() && elemTy->isConstantSizeType() &&
                arrSize) {
              uint64_t derefBytes =
                  getContext().getTypeSizeInChars(elemTy).getQuantity() *
                  arrSize;
              fn.setArgAttr(argIdx, "cir.dereferenceable",
                            getI64AttrCached(derefBytes));
            } else if (getContext()
                               .getTargetInfo()
                               .getNullPointerValue(
                                   elemTy.getAddressSpace()) == 0 &&
                       !cgm.getCodeGenOpts().NullPointerIsValid) {
              fn.setArgAttr(argIdx, "cir.nonnull",
                            mlir::UnitAttr::get(&cgm.getMLIRContext()));
            }
          }
        } else if (const auto *arrTy =
                       getContext().getAsVariableArrayType(originalTy)) {
          // VLA with static: can't compute dereferenceable, but
          // in addrspace(0) it must be nonnull.
          if (arrTy->getSizeModifier() == ArraySizeModifier::Static) {
            QualType elemTy = arrTy->getElementType();
            unsigned align =
                cgm.getNaturalTypeAlignment(elemTy, nullptr).getAsAlign().value();
            maybeSetArgAlign(align);
            if (!getContext().getTargetAddressSpace(
                    elemTy.getAddressSpace()) &&
                !cgm.getCodeGenOpts().NullPointerIsValid)
              fn.setArgAttr(argIdx, "cir.nonnull",
                            mlir::UnitAttr::get(&cgm.getMLIRContext()));
          }
        }
      }

      // OpenCL kernel pointer parameters: the OpenCL spec (v3.0.10 s6.3.5)
      // guarantees that pointer args to __kernel functions are aligned to
      // the natural alignment of the pointee type.
      // Matches OGCG CGCall.cpp:2956-2969 (ConstructAttributeList).
      if (fd && paramType->isPointerType() &&
          DeviceKernelAttr::isOpenCLSpelling(
              fd->getAttr<DeviceKernelAttr>())) {
        QualType pointeeTy = paramType->getPointeeType();
        if (!pointeeTy->isIncompleteType() &&
            pointeeTy->isConstantSizeType()) {
          unsigned align =
              cgm.getNaturalTypeAlignment(pointeeTy, nullptr).getAsAlign().value();
          maybeSetArgAlign(align);
        }
      }

      // Set align from __attribute__((align_value(N))) on pointer/reference
      // params, including typedef sugar carrying the attribute.
      if (alignValueAttr &&
          (paramType->isAnyPointerType() || paramType->isReferenceType())) {
        llvm::APSInt alignValue =
            alignValueAttr->getAlignment()->EvaluateKnownConstInt(
                cgm.getASTContext());
        maybeSetArgAlign(alignValue.getLimitedValue(llvm::Value::MaximumAlignment));
      }

      // Mark _Atomic aggregate parameters so the ABI classifier can force
      // indirect passing. In OGCG, AtomicType survives to classify() where
      // getAs<RecordType>() fails (isSugared()==false), defaulting to Memory.
      // CIR strips AtomicType during type conversion, so we propagate it as
      // an attribute instead.
      if (const auto *atomicTy = paramType->getAs<AtomicType>()) {
        if (atomicTy->getValueType()->isRecordType())
          fn.setArgAttr(argIdx, "cir.atomic",
                        mlir::UnitAttr::get(&cgm.getMLIRContext()));
      }

      // nonnull from __attribute__((nonnull)) on pointer parameters.
      // Matches OGCG CGCall.cpp:3073-3099 (getNonNullAttr) and 3250-3255.
      if (!cgm.getCodeGenOpts().NullPointerIsValid &&
          (paramType->isAnyPointerType() || paramType->isBlockPointerType())) {
        bool isNonNull = false;
        // Check per-parameter __attribute__((nonnull)).
        if (pvd && pvd->getAttr<NonNullAttr>())
          isNonNull = true;
        // Check function-level __attribute__((nonnull)) or nonnull(N).
        if (!isNonNull && fd) {
          unsigned paramIdx = pvd ? pvd->getFunctionScopeIndex() : argIdx;
          for (const auto *nnAttr : fd->specific_attrs<NonNullAttr>()) {
            if (nnAttr->isNonNull(paramIdx)) {
              isNonNull = true;
              break;
            }
          }
        }
        if (isNonNull)
          fn.setArgAttr(argIdx, "cir.nonnull",
                        mlir::UnitAttr::get(&cgm.getMLIRContext()));
      }

      ++argIdx;
    }
  }

  auto isBitIntType = [](QualType type) { return type->isBitIntType(); };
  auto isMatrixType = [](QualType type) { return type->isMatrixType(); };

  // Mark _Atomic aggregate return type for indirect passing.
  if (fd) {
    QualType retType = fd->getReturnType();
    if (const auto *atomicTy = retType->getAs<AtomicType>()) {
      if (atomicTy->getValueType()->isRecordType())
        fn->setAttr("cir.atomic_ret",
                    mlir::UnitAttr::get(&cgm.getMLIRContext()));
    }
    if (isBitIntType(retType))
      fn->setAttr("cir.ret.bitint",
                  mlir::UnitAttr::get(&cgm.getMLIRContext()));
    if (isMatrixType(retType))
      fn->setAttr("cir.ret.matrix",
                  mlir::UnitAttr::get(&cgm.getMLIRContext()));
  }

  llvm::SmallVector<bool, 8> bitIntArgs;
  llvm::SmallVector<bool, 8> matrixArgs;
  bitIntArgs.reserve(args.size());
  matrixArgs.reserve(args.size());
  bool hasBitIntArg = false;
  bool hasMatrixArg = false;
  for (const VarDecl *paramVar : args) {
    bool isBitInt = isBitIntType(paramVar->getType());
    bool isMatrix = isMatrixType(paramVar->getType());
    bitIntArgs.push_back(isBitInt);
    matrixArgs.push_back(isMatrix);
    hasBitIntArg |= isBitInt;
    hasMatrixArg |= isMatrix;
  }
  if (hasBitIntArg)
    fn->setAttr("cir.bitint_args",
                mlir::DenseBoolArrayAttr::get(&cgm.getMLIRContext(),
                                             bitIntArgs));
  if (hasMatrixArg)
    fn->setAttr("cir.matrix_args",
                mlir::DenseBoolArrayAttr::get(&cgm.getMLIRContext(),
                                             matrixArgs));

  // Set nonnull/dereferenceable/align attributes on the 'this' pointer.
  // OGCG does this in ConstructAttributeList (CGCall.cpp:2782-2817).
  // The 'this' pointer is a plain pointer (not a reference), so the loop
  // above doesn't catch it. Thunks are excluded (matching OGCG's !IsThunk).
  if (!curFuncIsThunk)
  if (const auto *md = dyn_cast_if_present<CXXMethodDecl>(fd);
      md && md->isImplicitObjectMemberFunction()) {
    // The 'this' parameter is always the first argument.
    if (!args.empty()) {
      const VarDecl *thisParam = args[0];
      if (const auto *ipd = dyn_cast<ImplicitParamDecl>(thisParam);
          ipd && ipd->getParameterKind() == ImplicitParamKind::CXXThis) {
        QualType thisTy = thisParam->getType();
        QualType pointeeTy = thisTy->getPointeeType();

        if (!cgm.getCodeGenOpts().NullPointerIsValid) {
          fn.setArgAttr(0, "cir.nonnull",
                        mlir::UnitAttr::get(&cgm.getMLIRContext()));
          fn.setArgAttr(0, "cir.callsite_nonnull",
                        mlir::UnitAttr::get(&cgm.getMLIRContext()));
          // dereferenceable(N) — minimum object size (non-virtual size for
          // non-final classes, since the object could be a subclass).
          uint64_t size =
              cgm.getMinimumObjectSize(pointeeTy).getQuantity();
          fn.setArgAttr(0, "cir.dereferenceable",
                        getI64AttrCached(size));
        } else {
          // With -fno-delete-null-pointer-checks, use dereferenceable_or_null.
          // Matches OGCG CGCall.cpp:2800-2808.
          uint64_t size =
              cgm.getMinimumObjectSize(pointeeTy).getQuantity();
          fn.setArgAttr(0, "cir.dereferenceable_or_null",
                        getI64AttrCached(size));
        }
        // align N — for C++ class this pointers, use non-virtual alignment
        // to avoid inheriting virtual base alignment.
        if (!pointeeTy->isIncompleteType()) {
          unsigned align;
          if (const auto *rd = pointeeTy->getAsCXXRecordDecl()) {
            if (!rd->hasDefinition()) {
              align = 1;
            } else {
              auto &layout = getContext().getASTRecordLayout(rd);
              if (rd->isEffectivelyFinal())
                align = layout.getAlignment().getQuantity();
              else
                align = layout.getNonVirtualAlignment().getQuantity();
            }
          } else {
            align = getContext().getTypeAlignInChars(pointeeTy).getQuantity();
          }
          fn.setArgAttr(0, "cir.align", getI64AttrCached(align));
        }

        // Add the 'returned' attribute on the 'this' parameter for
        // constructors/destructors that return 'this' (HasThisReturn),
        // except for iOS 5 and earlier where substantial code was compiled
        // with GCC and does not actually return 'this'.
        if (cgm.getCXXABI().hasThisReturn(gd) &&
            !(cgm.getTriple().isiOS() &&
              cgm.getTriple().isOSVersionLT(6))) {
          fn.setArgAttr(0, "cir.returned",
                        mlir::UnitAttr::get(&cgm.getMLIRContext()));
        }
      }
    }
  }

  // If any of the arguments have a variably modified type, make sure to
  // emit the type size, but only if the function is not naked.
  if (!fd || !fd->hasAttr<NakedAttr>()) {
    for (const VarDecl *vd : args) {
      // Dig out the type as written from ParmVarDecls; it's unclear whether
      // the standard (C99 6.9.1p10) requires this, but we're following the
      // precedent set by gcc.
      QualType ty;
      if (const auto *pvd = dyn_cast<ParmVarDecl>(vd))
        ty = pvd->getOriginalType();
      else
        ty = vd->getType();

      if (ty->isVariablyModifiedType())
        emitVariablyModifiedType(ty);
    }
  }

  // When the current function is not void, create an address to store the
  // result value.
  if (!returnType->isVoidType()) {
    // Determine the function body end location.
    // If fd is null or has no body, use loc as fallback.
    SourceLocation bodyEndLoc = loc;
    if (fd) {
      if (Stmt *body = fd->getBody())
        bodyEndLoc = body->getEndLoc();
      else
        bodyEndLoc = fd->getLocation();
    }
    emitAndUpdateRetAlloca(returnType, getLoc(bodyEndLoc),
                           getContext().getTypeAlignInChars(returnType));

    if (getLangOpts().ObjCAutoRefCount &&
        returnType->isObjCRetainableType()) {
      if (const auto *method = dyn_cast_or_null<ObjCMethodDecl>(curCodeDecl))
        autoreleaseResult = !method->hasAttr<NSReturnsRetainedAttr>();
      else if (const auto *fnDecl =
                   dyn_cast_or_null<FunctionDecl>(curCodeDecl))
        autoreleaseResult = !fnDecl->hasAttr<NSReturnsRetainedAttr>();
    }

    // If this is an implicit-return-zero function (e.g. main()), initialize
    // the return value to 0. C99 5.1.2.2.3: reaching the } that terminates
    // main() returns 0.
    if (fd && fd->hasImplicitReturnZero()) {
      mlir::Location retLoc = getLoc(bodyEndLoc);
      mlir::Value zero = builder.getNullValue(convertType(returnType), retLoc);
      builder.CIRBaseBuilderTy::createStore(retLoc, zero,
                                            returnValue.emitRawPointer());
    }
  }

  if (requiresReturnValueCheck()) {
    SourceLocation bodyBeginLoc = startLoc;
    if (fd && fd->hasBody())
      bodyBeginLoc = fd->getBody()->getBeginLoc();
    mlir::Location prologueLoc = getLoc(bodyBeginLoc);
    returnCheckLocation =
        createTempAlloca(builder.getUInt8PtrTy(), getPointerAlign(), prologueLoc,
                         "__return.check.loc");
    auto store = builder.CIRBaseBuilderTy::createStore(
        prologueLoc, builder.getNullPtr(builder.getUInt8PtrTy(), prologueLoc),
        returnCheckLocation.emitRawPointer());
    store->setAttr("cir.nosanitize", mlir::UnitAttr::get(&getMLIRContext()));
  }

  // OGCG sets norecurse on main/OpenCL/HLSL/SYCL/CUDA functions and
  // stackrealign on main when custom stack alignment is specified.
  // Both are now handled in CIRGenModule::setNonAliasAttributes.

  if (isa_and_nonnull<CXXMethodDecl>(d) &&
      cast<CXXMethodDecl>(d)->isImplicitObjectMemberFunction()) {
    cgm.getCXXABI().emitInstanceFunctionProlog(loc, *this);
  }

  // Check the 'this' pointer once per function, if it's available.
  // Matches OGCG CodeGenFunction.cpp:1322-1337.
  if (const auto *md = dyn_cast_if_present<CXXMethodDecl>(d)) {
    if (cxxabiThisValue) {
      SanitizerSet skippedChecks;
      skippedChecks.set(SanitizerKind::ObjectSize, true);
      QualType thisTy = md->getThisType();

      // If this is the call operator of a lambda with no captures, it
      // may have a static invoker function, which may call this operator
      // with a null 'this' pointer.
      if (isLambdaCallOperator(md) && md->getParent()->isCapturelessLambda())
        skippedChecks.set(SanitizerKind::Null, true);

      emitTypeCheck(
          isa<CXXConstructorDecl>(md) ? TCK_ConstructorCall : TCK_MemberCall,
          d->getLocation(), cxxabiThisValue, thisTy, cxxabiThisAlignment,
          skippedChecks);
    }
  }

  if (const auto *md = dyn_cast_if_present<CXXMethodDecl>(d);
      md && !md->isStatic()) {
    bool isInLambda =
        md->getParent()->isLambda() && md->getOverloadedOperator() == OO_Call;
    if (isInLambda) {
      // We're in a lambda.
      auto fn = dyn_cast<cir::FuncOp>(curFn);
      assert(fn && "lambda in non-function region");
      fn.setLambda(true);

      // Figure out the captures.
      md->getParent()->getCaptureFields(lambdaCaptureFields,
                                        lambdaThisCaptureField);
      if (lambdaThisCaptureField) {
        // If the lambda captures the object referred to by '*this' - either by
        // value or by reference, make sure CXXThisValue points to the correct
        // object.

        // Get the lvalue for the field (which is a copy of the enclosing object
        // or contains the address of the enclosing object).
        LValue thisFieldLValue =
            emitLValueForLambdaField(lambdaThisCaptureField);
        if (!lambdaThisCaptureField->getType()->isPointerType()) {
          // If the enclosing object was captured by value, just use its
          // address. Sign this pointer.
          cxxThisValue = thisFieldLValue.getPointer();
        } else {
          // Load the lvalue pointed to by the field, since '*this' was captured
          // by reference.
          cxxThisValue =
              emitLoadOfLValue(thisFieldLValue, SourceLocation()).getValue();
        }
      }
      for (auto *fd : md->getParent()->fields()) {
        if (fd->hasCapturedVLAType()) {
          // Restore VLA sizes from the lambda's captured fields into
          // vlaSizeMap so that VLA-dependent expressions in the lambda
          // body can look up the correct sizes.
          auto exprArg =
              emitLoadOfLValue(emitLValueForLambdaField(fd), SourceLocation())
                  .getValue();
          auto vat = fd->getCapturedVLAType();
          vlaSizeMap[vat->getSizeExpr()] = exprArg;
        }
      }
    } else if (md->isImplicitObjectMemberFunction()) {
      // Not in a lambda; just use 'this' from the method.
      // FIXME: Should we generate a new load for each use of 'this'? The fast
      // register allocator would be happier...
      cxxThisValue = cxxabiThisValue;
    }

    (void)0; // Sanitizer-specific CIR parity work remains NYI in this path.
  }
}

void CIRGenFunction::resolveBlockAddresses() {
  for (cir::BlockAddressOp &blockAddress : cgm.unresolvedBlockAddressToLabel) {
    cir::LabelOp labelOp =
        cgm.lookupBlockAddressInfo(blockAddress.getBlockAddrInfo());
    assert(labelOp && "expected cir.labelOp to already be emitted");
    cgm.updateResolvedBlockAddress(blockAddress, labelOp);
  }
  cgm.unresolvedBlockAddressToLabel.clear();
}

void CIRGenFunction::finishIndirectBranch() {
  if (!indirectGotoBlock)
    return;
  llvm::SmallVector<mlir::Block *> succesors;
  llvm::SmallVector<mlir::ValueRange> rangeOperands;
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToEnd(indirectGotoBlock);
  mlir::Region *indirectGotoRegion = indirectGotoBlock->getParent();
  for (auto &[blockAdd, labelOp] : cgm.blockAddressToLabel) {
    mlir::Block *labelBlock = labelOp->getBlock();
    // Skip labels that live in a nested region (inside an if/switch/loop or
    // statement-expression cir.scope). CIR creates nested regions for
    // cir.scope ops, but MLIR forbids cross-region branches, so such a label
    // cannot be listed as an indirect branch successor here. This is not a
    // dead end even when an actual `goto *p` targets the label: GotoSolver
    // runs after FlattenCFG has hoisted every block into the single
    // function-entry region, re-derives all address-taken labels (from
    // cir.block_address, constant initializers, and global jump tables), and
    // appends the still-missing ones to this cir.indirect_br. So we emit the
    // op now with whatever same-region successors we have and let GotoSolver
    // complete the list on the flattened CFG.
    if (labelBlock->getParent() != indirectGotoRegion)
      continue;
    succesors.push_back(labelBlock);
    rangeOperands.push_back(labelBlock->getArguments());
  }
  cir::IndirectBrOp::create(builder, builder.getUnknownLoc(),
                            indirectGotoBlock->getArgument(0), false,
                            rangeOperands, succesors);
  cgm.blockAddressToLabel.clear();
}

void CIRGenFunction::finishFunction(SourceLocation endLoc) {
  finishOpenMPLoopBodies();
  if (emittingOpenMPTargetGenericFunction) {
    cgm.popOpenMPTargetGenericEmission();
    emittingOpenMPTargetGenericFunction = false;
  }
  openMPTargetEscapedLocalDecls.clear();

  // OGCG asserts BreakContinueStack is empty, but CIR doesn't have one
  // (it uses structured CIR ops for break/continue instead).
  assert(lifetimeExtendedCleanupStack.empty() &&
         "mismatched push/pop of cleanups in EHStack!");
  assert(deferredDeactivationCleanupStack.empty() &&
         "mismatched activate/deactivate of cleanups!");
  // OGCG calls PopCleanupBlocks(PrologueCleanupDepth) and then asserts
  // EHStack.empty(). CIR's prologue cleanups are popped by LexicalScope
  // cleanup instead, and ehStack may still contain scopes at this point,
  // so we don't assert ehStack.empty() here.

  // Resolve block address-to-label mappings, then emit the indirect branch
  // with the corresponding targets.
  resolveBlockAddresses();
  finishIndirectBranch();

  // If a label address was taken but no indirect goto was used, we can't remove
  // the block argument here. Instead, we mark the 'indirectbr' op
  // as poison so that the cleanup can be deferred to lowering, since the
  // verifier doesn't allow the 'indirectbr' target address to be null.
  // Skip this when IndirectGotoOps were emitted — GotoSolver will create
  // BrOps to this block later, so it will have predecessors then.
  if (indirectGotoBlock && !hasIndirectGoto &&
      indirectGotoBlock->hasNoPredecessors()) {
    auto indrBr = cast<cir::IndirectBrOp>(indirectGotoBlock->front());
    indrBr.setPoison(true);
  }

  // Every cleanup has popped and every enclosing expression has taken whatever
  // it was holding, so the values a cir.cleanup.scope confines to its body are
  // known for the first time here.
  spillValuesEscapingCleanupScopes();

  // In CIR, RAII cleanups are handled by LexicalScope::cleanup() which
  // properly wires cleanup blocks into the structured CFG. Popping them here
  // would emit cleanups inline into a dead continuation block, producing
  // unreachable cleanup code. Let LexicalScope::cleanup() handle it instead.

  // Add the min-legal-vector-width attribute. This contains the max width from:
  // 1. min-vector-width attribute used in the source program.
  // 2. Any builtins used that have a vector width specified.
  // 3. Values passed in and out of inline assembly.
  // 4. Width of vector arguments and return types for this function.
  // 5. Width of vector arguments and return types for functions called by this
  //    function.
  //
  // We intentionally do not scan CIR function signature types here. OGCG
  // computes these from the final LLVM function signature in FinishFunction,
  // after ABI coercions. CIR mirrors that in lowerFuncAttributes() where the
  // LLVM-lowered signature is available. Scanning CIR types here can
  // overestimate (e.g. vector types coerced to scalar in ABI lowering).
  //
  // TODO: scan call-site argument/return widths (maxVectorWidth).
  assert(!cir::MissingFeatures::maxVectorWidth());
  if (getContext().getTargetInfo().getTriple().isX86()) {
    if (auto fn = mlir::dyn_cast<cir::FuncOp>(curFn)) {
      mlir::NamedAttrList attrs;
      if (auto existingAttrs = fn.getExtraAttrs())
        attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
      attrs.set("min-legal-vector-width",
                cgm.getUIntStringAttrCached(largestVectorWidth));
      fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
          attrs.getDictionary(&cgm.getMLIRContext())));
    }
  }

  // OGCG checks ShouldInstrumentFunction() and adds instrument-function-exit
  // and instrument-function-exit-inlined attributes for __cyg_profile_func_exit
  // (CodeGenFunction.cpp:428-434). CIR mirrors this via function attributes
  // set in CIRGenModule::setCIRFunctionAttributesForDefinition().

  if (auto fn = mlir::dyn_cast<cir::FuncOp>(curFn))
    cgm.noteFunctionEmissionComplete(fn);
}

mlir::LogicalResult CIRGenFunction::emitFunctionBody(const clang::Stmt *body) {
  incrementProfileCounter(body);
  assert(!cir::MissingFeatures::mcdcCoverageFolding());

  // The caller owns the function-level symbol scope. Keep top-level locals
  // visible until the enclosing LexicalScope finishes emitting cleanups.

  if (const CompoundStmt *block = dyn_cast<CompoundStmt>(body))
    return emitCompoundStmtWithoutScope(*block);

  return emitStmt(body, /*useCurrentScope=*/true);
}

static void eraseEmptyAndUnusedBlocks(cir::FuncOp func) {
  // Remove any leftover blocks that are unreachable and empty, since they do
  // not represent unreachable code useful for warnings nor anything deemed
  // useful in general.
  SmallVector<mlir::Block *> blocksToDelete;
  for (mlir::Block &block : func.getBlocks()) {
    if (block.empty() && block.getUses().empty())
      blocksToDelete.push_back(&block);
  }
  for (mlir::Block *block : blocksToDelete)
    block->erase();
}

cir::FuncOp CIRGenFunction::generateCode(clang::GlobalDecl gd, cir::FuncOp fn,
                                         cir::FuncType funcType) {
  const auto *funcDecl = cast<FunctionDecl>(gd.getDecl());
  curGD = gd;

  if (funcDecl->isInlineBuiltinDeclaration()) {
    // When generating code for a builtin with an inline declaration, use a
    // mangled name to hold the actual body, while keeping an external
    // declaration in case the function pointer is referenced somewhere.
    std::string fdInlineName = (cgm.getMangledName(funcDecl) + ".inline").str();
    cir::FuncOp clone =
        mlir::cast_or_null<cir::FuncOp>(cgm.getGlobalValue(fdInlineName));
    if (!clone) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPoint(fn);
      clone = cir::FuncOp::create(builder, fn.getLoc(), fdInlineName,
                                  fn.getFunctionType());
      cgm.insertGlobalSymbol(clone);
      clone.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
      clone.setSymVisibility("private");
      clone.setInlineKind(cir::InlineKind::AlwaysInline);
    }
    fn.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
    fn.setSymVisibility("private");
    fn = clone;
  } else {
    // Detect the unusual situation where an inline version is shadowed by a
    // non-inline version. In that case we should pick the external one
    // everywhere. That's GCC behavior too.
    // Inline-builtin shadowing can only happen for builtin declarations.
    // Skip the previous-declaration walk for ordinary functions to reduce
    // front-end work in large translation units with long declaration chains.
    if (funcDecl->getBuiltinID()) {
      for (const FunctionDecl *pd = funcDecl->getPreviousDecl(); pd;
           pd = pd->getPreviousDecl()) {
        if (LLVM_UNLIKELY(pd->isInlineBuiltinDeclaration())) {
          std::string inlineName = funcDecl->getName().str() + ".inline";
          if (auto inlineFn = mlir::cast_or_null<cir::FuncOp>(
                  cgm.getGlobalValue(inlineName))) {
            // Replace all uses of the .inline function with the regular
            // function. Use a direct walk over CallOp/TryCallOp instead of
            // symbolKnownUseEmpty+replaceAllSymbolUses which each walk
            // the entire module materializing full DictionaryAttrs per op.
            auto inlineNameAttr = inlineFn.getSymNameAttr();
            auto regularNameAttr = fn.getSymNameAttr();
            cgm.getModule().walk([&](mlir::Operation *op) {
              if (auto call = mlir::dyn_cast<cir::CallOp>(op)) {
                if (auto callee = call.getCalleeAttr();
                    callee && callee.getAttr() == inlineNameAttr)
                  call.setCalleeAttr(
                      mlir::FlatSymbolRefAttr::get(regularNameAttr));
              } else if (auto tryCall =
                             mlir::dyn_cast<cir::TryCallOp>(op)) {
                if (auto callee = tryCall.getCalleeAttr();
                    callee && callee.getAttr() == inlineNameAttr)
                  tryCall.setCalleeAttr(
                      mlir::FlatSymbolRefAttr::get(regularNameAttr));
              }
            });
            inlineFn.erase();
          }
          break;
        }
      }
    }
  }

  SourceLocation loc = funcDecl->getLocation();
  Stmt *body = funcDecl->getBody();
  SourceRange bodyRange =
      body ? body->getSourceRange() : funcDecl->getLocation();

  SourceLocRAIIObject fnLoc{*this, loc.isValid() ? getLoc(loc)
                                                 : builder.getUnknownLoc()};

  auto validMLIRLoc = [&](clang::SourceLocation clangLoc) {
    return clangLoc.isValid() ? getLoc(clangLoc) : builder.getUnknownLoc();
  };
  const mlir::Location fusedLoc = mlir::FusedLoc::get(
      &getMLIRContext(),
      {validMLIRLoc(bodyRange.getBegin()), validMLIRLoc(bodyRange.getEnd())});
  mlir::Block *entryBB = fn.addEntryBlock();

  FunctionArgList args;
  QualType retTy = buildFunctionArgList(gd, args);

  cgm.getTargetCIRGenInfo().checkFunctionABI(cgm, funcDecl);

  // Create a scope in the symbol table to hold variable declarations.
  SymTableScopeTy varScope(symbolTable);

  // Push exception spec scope (terminate scope for noexcept functions) BEFORE
  // the LexicalScope so it sits below the LexicalScope's cleanup depth.
  // This prevents LexicalScope::cleanup()'s popCleanupBlocks from trying
  // to pop the Terminate scope.
  emitStartEHSpec(funcDecl);

  {
    LexicalScope lexScope(*this, fusedLoc, entryBB);
    lexScope.setClangSourceRange(bodyRange);

    // Emit the standard function prologue.
    startFunction(gd, retTy, fn, funcType, args, loc, bodyRange.getBegin());

    PGO->assignRegionCounters(gd, fn);

    // Match OGCG GenerateCode: add mustprogress only for ordinary function
    // definitions. Other StartFunction users such as block invokes and ObjC
    // methods do not get this attribute from OGCG.
    if (!curFuncIsThunk && checkIfFunctionMustProgress()) {
      mlir::NamedAttrList attrs;
      if (auto existingAttrs = fn.getExtraAttrs())
        attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
      attrs.set("mustprogress", mlir::UnitAttr::get(&getMLIRContext()));
      fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
          attrs.getDictionary(&cgm.getMLIRContext())));
    }

    // Emit OpenCL kernel metadata.
    if (funcDecl && (getLangOpts().OpenCL ||
                     (getLangOpts().CUDA &&
                      getContext().getTargetInfo().getTriple().isSPIRV()) ||
                     ((getLangOpts().HIP || getLangOpts().OffloadViaLLVM) &&
                      getLangOpts().CUDAIsDevice))) {
      emitKernelMetadata(funcDecl, fn);
    }

    // Save parameters for coroutine function.
    if (body && isa_and_nonnull<CoroutineBodyStmt>(body))
      llvm::append_range(fnArgs, funcDecl->parameters());

    // Initialize helper which will detect jumps which can cause invalid
    // lifetime markers (OGCG CodeGenFunction.cpp:1548-1557).
    if (body) {
      // Coroutines always emit lifetime markers.
      if (isa<CoroutineBodyStmt>(body))
        shouldEmitLifetimeMarkers = true;

      if (shouldEmitLifetimeMarkers)
        bypasses.Init(body);
    }

    if (isa<CXXDestructorDecl>(funcDecl)) {
      emitDestructorBody(args);
    } else if (isa<CXXConstructorDecl>(funcDecl)) {
      emitConstructorBody(args);
    } else if (getLangOpts().CUDA && !getLangOpts().CUDAIsDevice &&
               funcDecl->hasAttr<CUDAGlobalAttr>()) {
      getCIRGenModule().getCUDARuntime().emitDeviceStub(*this, fn, args);
    } else if (isa<CXXMethodDecl>(funcDecl) &&
               cast<CXXMethodDecl>(funcDecl)->isLambdaStaticInvoker()) {
      // OGCG dispatches to EmitLambdaInAllocaCallOpBody before this when a
      // lambda call operator has inalloca arguments. inalloca is x86-32 MSVC
      // ABI only, not applicable to CIR targets.
      // The lambda static invoker function is special, because it forwards or
      // clones the body of the function call operator (but is actually
      // static).
      emitLambdaStaticInvokeBody(cast<CXXMethodDecl>(funcDecl));
    } else if (funcDecl->isDefaulted() && isa<CXXMethodDecl>(funcDecl) &&
               (cast<CXXMethodDecl>(funcDecl)->isCopyAssignmentOperator() ||
                cast<CXXMethodDecl>(funcDecl)->isMoveAssignmentOperator())) {
      // Implicit copy-assignment gets the same special treatment as implicit
      // copy-constructors.
      emitImplicitAssignmentOperatorBody(args);
    } else if (DeviceKernelAttr::isOpenCLSpelling(
                   funcDecl->getAttr<DeviceKernelAttr>()) &&
               gd.getKernelReferenceKind() == KernelReferenceKind::Kernel) {
      // OpenCL kernel entry point: forward all arguments to the stub.
      CallArgList callArgs;
      for (unsigned i = 0; i < args.size(); ++i) {
        Address argAddr = getAddrOfLocalVar(args[i]);
        QualType argQualType = args[i]->getType();
        RValue argRValue = convertTempToRValue(argAddr, argQualType, loc);
        callArgs.add(argRValue, argQualType);
      }
      GlobalDecl gdStub(funcDecl, KernelReferenceKind::Stub);
      const FunctionType *ft = cast<FunctionType>(funcDecl->getType());
      cgm.getTargetCIRGenInfo().setOCLKernelStubCallingConvention(ft);
      const CIRGenFunctionInfo &fnInfo =
          cgm.getTypes().arrangeFreeFunctionCall(callArgs, ft);
      cir::FuncOp stubFunc = cgm.getAddrOfFunction(gdStub);
      CIRGenCallee stubCallee = CIRGenCallee::forDirect(stubFunc, gdStub);
      emitCall(fnInfo, stubCallee, ReturnValueSlot(), callArgs, nullptr,
               getLoc(loc));
    } else if (body) {
      // Emit standard function body.
      if (mlir::failed(emitFunctionBody(body))) {
        return nullptr;
      }
    } else {
      // Anything without a body should have been handled above.
      llvm_unreachable("no definition for normal function");
    }

    if (mlir::failed(fn.verifyBody()))
      return nullptr;

    finishFunction(bodyRange.getEnd());
  }

  // Pop exception spec scope after LexicalScope destruction (all cleanup
  // scopes have been popped at this point).
  emitEndEHSpec(funcDecl);

  // Some callees (for example internal lambdas) only become known-nothrow
  // after their bodies are emitted. If a caller wrapped such a callee in a
  // synthetic try, strip that now-dead EH scaffolding before inferring
  // nothrow for the caller itself.
  simplifyDeadSyntheticTrys(fn, cgm);

  // Opportunistically mark functions as nounwind if they contain no
  // potentially-throwing operations. Matches OGCG's TryMarkNoThrow
  // (CodeGenFunction.cpp:1408-1418).
  //
  // This must run AFTER the LexicalScope destructor (line above) because
  // cleanup code (e.g. destructor calls for local variables) is emitted
  // when LexicalScope is destroyed. Running before would miss those calls
  // and incorrectly mark functions as nounwind.
  //
  // Skip interposable functions since they could be replaced with a
  // throwing version at link time. Use isInterposableLinkage (not
  // isWeakForLinker) so that ODR linkages are still eligible.
  {
    bool alreadyNothrow = false;
    if (auto existingExtraAttrs = fn.getExtraAttrs())
      alreadyNothrow =
          existingExtraAttrs->getElements().get("nothrow") != nullptr;
    if (!alreadyNothrow &&
        !cir::isInterposableLinkage(fn.getLinkage())) {
      bool mayThrow = false;
      auto directCallCannotThrowLikeOGCG = [&](cir::CallOp call) {
        if (call.getException())
          return false;

        cir::FuncOp callee = call.getCallee()
                                 ? cgm.lookupFuncOp(*call.getCallee())
                                 : cir::FuncOp{};
        if (!callee || !hasNoThrowExtraAttr(callee))
          return false;

        // LLVM's CallBase::hasFnAttr consults direct callee function attrs,
        // but TryMarkNoThrow only sees attrs that were already materialized
        // when the current function started emission.
        return callee.isDeclaration() ||
               cgm.wasFunctionEmissionCompleteBefore(
                   callee, completedFunctionEmissionCountAtStart);
      };
      fn.walk([&](mlir::Operation *op) {
        // Calls without nothrow may throw.
        if (auto callOp = mlir::dyn_cast<cir::CIRCallOpInterface>(op)) {
          if (!callOp->hasAttr("nothrow")) {
            if (auto directCall = mlir::dyn_cast<cir::CallOp>(op);
                !directCall || !directCallCannotThrowLikeOGCG(directCall)) {
              mayThrow = true;
              return mlir::WalkResult::interrupt();
            }
          }
        }
        // Throw and resume ops always throw.
        if (mlir::isa<cir::ThrowOp, cir::ResumeOp>(op)) {
          mayThrow = true;
          return mlir::WalkResult::interrupt();
        }
        // A reference dynamic_cast can throw std::bad_cast.
        if (auto dynCast = mlir::dyn_cast<cir::DynamicCastOp>(op)) {
          if (dynCast.isRefCast()) {
            mayThrow = true;
            return mlir::WalkResult::interrupt();
          }
        }
        return mlir::WalkResult::advance();
      });
      if (!mayThrow) {
        auto existingExtraAttrs = fn.getExtraAttrs();
        mlir::NamedAttrList attrs;
        if (existingExtraAttrs)
          attrs = mlir::NamedAttrList{
              existingExtraAttrs->getElements().getValue()};
        auto nothrowAttr = cir::NoThrowAttr::get(&cgm.getMLIRContext());
        attrs.set(nothrowAttr.getMnemonic(), nothrowAttr);
        fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
            attrs.getDictionary(&cgm.getMLIRContext())));
      }
    }
  }

  eraseEmptyAndUnusedBlocks(fn);
  return fn;
}

cir::FuncOp CIRGenFunction::generateBlockFunctionCode(const BlockExpr *blockExpr,
                                                      cir::FuncOp fn,
                                                      cir::FuncType funcType) {
  const auto *blockDecl = blockExpr->getBlockDecl();
  curGD = GlobalDecl(blockDecl);

  SourceLocation loc = blockDecl->getLocation();
  Stmt *body = blockDecl->getBody();
  SourceRange bodyRange = body ? body->getSourceRange() : loc;

  SourceLocRAIIObject fnLoc{*this, loc.isValid() ? getLoc(loc)
                                                 : builder.getUnknownLoc()};
  auto validMLIRLoc = [&](clang::SourceLocation clangLoc) {
    return clangLoc.isValid() ? getLoc(clangLoc) : builder.getUnknownLoc();
  };
  mlir::Location fusedLoc = mlir::FusedLoc::get(
      &getMLIRContext(),
      {validMLIRLoc(bodyRange.getBegin()), validMLIRLoc(bodyRange.getEnd())});
  mlir::Block *entryBB = fn.addEntryBlock();

  const IdentifierInfo *ii =
      &getContext().Idents.get(".block_descriptor");
  ImplicitParamDecl selfDecl(getContext(), const_cast<BlockDecl *>(blockDecl),
                             SourceLocation(), ii, getContext().VoidPtrTy,
                             ImplicitParamKind::ObjCSelf);

  FunctionArgList args;
  args.push_back(&selfDecl);
  args.append(blockDecl->param_begin(), blockDecl->param_end());
  QualType retTy = blockExpr->getFunctionType()->getReturnType();

  SymTableScopeTy varScope(symbolTable);
  {
    LexicalScope lexScope(*this, fusedLoc, entryBB);
    lexScope.setClangSourceRange(bodyRange);

    startFunction(GlobalDecl(blockDecl), retTy, fn, funcType, args, loc,
                  bodyRange.getBegin());
    cgm.trackCoverageMappingDecl(const_cast<BlockDecl *>(blockDecl),
                                 fn.getSymName(),
                                 cgm.getCoverageMappingPGOName(fn));
    PGO->assignRegionCounters(GlobalDecl(blockDecl), fn);

    if (blockDecl->hasCaptures()) {
      auto unsupportedCapture = llvm::find_if(
          blockDecl->captures(), [&](const BlockDecl::Capture &capture) {
            if (capture.getCopyExpr())
              return !isSupportedCXXDirectBlockCapture(capture);
            QualType captureTy = capture.getVariable()->getType();
            auto evalKind = getEvaluationKind(captureTy);
            if (captureTy.getObjCLifetime() == Qualifiers::OCL_None)
              return evalKind != cir::TEK_Scalar &&
                     evalKind != cir::TEK_Complex &&
                     evalKind != cir::TEK_Aggregate;
            if (capture.isEscapingByref())
              return false;
            bool isSupportedARCBlockCapture =
                captureTy->isBlockPointerType() &&
                captureTy.isNonTrivialToPrimitiveCopy() ==
                    QualType::PCK_ARCStrong;
            bool isSupportedARCObjCCapture =
                (captureTy.getObjCLifetime() == Qualifiers::OCL_Strong ||
                 captureTy.getObjCLifetime() == Qualifiers::OCL_Weak ||
                 captureTy.getObjCLifetime() == Qualifiers::OCL_ExplicitNone) &&
                captureTy->isObjCRetainableType() &&
                !captureTy->isBlockPointerType();
            return !isSupportedARCObjCCapture && !isSupportedARCBlockCapture;
          });
      if (unsupportedCapture != blockDecl->capture_end()) {
        cgm.error(blockExpr->getExprLoc(),
                  "ClangIR code gen Not Yet Implemented: non-scalar block "
                  "capture");
        return nullptr;
      }

      auto int32Ty = builder.getSInt32Ty();
      auto voidPtrTy = builder.getVoidPtrTy();
      auto i8PtrTy = builder.getPointerTo(builder.getSInt8Ty());
      auto ulongTy = mlir::cast<cir::IntType>(
          cgm.convertType(getContext().UnsignedLongTy));
      auto descTy =
          builder.getAnonRecordTy({ulongTy, ulongTy, i8PtrTy, i8PtrTy});

      SmallVector<mlir::Type, 8> literalMembers{
          builder.getPointerTo(voidPtrTy), int32Ty, int32Ty, voidPtrTy,
          builder.getPointerTo(descTy)};
      const bool capturesCXXThis = blockDecl->capturesCXXThis();
      const unsigned thisFieldIndex = 5;
      const unsigned firstCaptureFieldIndex = thisFieldIndex + capturesCXXThis;
      QualType thisCaptureTy;
      if (capturesCXXThis) {
        const auto *method =
            dyn_cast_or_null<CXXMethodDecl>(blockDecl->getNonClosureContext());
        assert(method && "captured C++ this without enclosing C++ method");
        thisCaptureTy = method->getThisType();
        literalMembers.push_back(convertType(thisCaptureTy));
      }
      for (const auto &capture : blockDecl->captures()) {
        const FieldDecl *lambdaCaptureField =
            capture.isNested() ? lambdaCaptureFields.lookup(capture.getVariable())
                               : nullptr;
        QualType captureFieldTy =
            getDirectBlockCaptureFieldType(capture, lambdaCaptureField);
        literalMembers.push_back(capture.isEscapingByref()
                                     ? mlir::Type(voidPtrTy)
                                     : convertTypeForMem(captureFieldTy));
      }
      bool packedLiteral = shouldPackDirectBlockLiteral(literalMembers);
      auto literalTy = builder.getAnonRecordTy(literalMembers, packedLiteral);

      CharUnits literalAlign = getDirectBlockLiteralAlignment(literalMembers);

      Address selfAddr = getAddrOfLocalVar(&selfDecl);
      mlir::Value selfValue = builder.createAlignedLoad(
          getLoc(blockExpr->getExprLoc()), voidPtrTy, selfAddr.getPointer(),
          selfAddr.getAlignment());
      mlir::Value blockValue = builder.createBitcast(
          getLoc(blockExpr->getExprLoc()), selfValue,
          builder.getPointerTo(literalTy));

      if (capturesCXXThis) {
        mlir::Type fieldTy = literalMembers[thisFieldIndex];
        mlir::Value fieldPtr = builder.createGetMember(
            getLoc(blockExpr->getExprLoc()), builder.getPointerTo(fieldTy),
            blockValue, "", thisFieldIndex);
        CharUnits align = literalAlign.alignmentAtOffset(
            getDirectBlockLiteralFieldOffset(literalMembers, packedLiteral,
                                             thisFieldIndex));
        cxxThisValue = builder.createAlignedLoad(getLoc(blockExpr->getExprLoc()),
                                                 fieldTy, fieldPtr, align);
        if (cxxThisAlignment.isZero()) {
          const auto *record =
              thisCaptureTy->getPointeeCXXRecordDecl();
          cxxThisAlignment = cgm.getClassPointerAlignment(record);
        }
      }

      for (const auto &[index, capture] : llvm::enumerate(blockDecl->captures())) {
        QualType captureTy = capture.getVariable()->getType();
        const FieldDecl *lambdaCaptureField =
            capture.isNested() ? lambdaCaptureFields.lookup(capture.getVariable())
                               : nullptr;
        QualType captureFieldTy =
            getDirectBlockCaptureFieldType(capture, lambdaCaptureField);
        mlir::Type fieldTy = literalMembers[firstCaptureFieldIndex + index];
        mlir::Value fieldPtr = builder.createGetMember(
            getLoc(blockExpr->getExprLoc()), builder.getPointerTo(fieldTy),
            blockValue, "", firstCaptureFieldIndex + index);
        CharUnits align = literalAlign.alignmentAtOffset(
            getDirectBlockLiteralFieldOffset(literalMembers, packedLiteral,
                                             firstCaptureFieldIndex + index));
        if (capture.isEscapingByref()) {
          mlir::Value byrefBase = builder.createAlignedLoad(
              getLoc(blockExpr->getExprLoc()), voidPtrTy, fieldPtr, align);
          const auto &byrefInfo = getBlockByrefInfo(capture.getVariable());
          mlir::Type byrefPtrTy = builder.getPointerTo(byrefInfo.type);
          if (byrefBase.getType() != byrefPtrTy)
            byrefBase =
                builder.createBitcast(getLoc(blockExpr->getExprLoc()), byrefBase,
                                      byrefPtrTy);
          // Register the byref BASE address (not a value-projected address) in
          // localDeclMap, mirroring classic CGBlocks and the non-nested __block
          // path (emitByrefStructureInit). emitDeclRefLValue projects to .value
          // lazily (followForward) when it sees the base element type; storing a
          // pre-projected &value here instead breaks re-capture of this __block
          // var by a further-nested block, which then re-applies the forwarding
          // +value GEPs to the already-projected pointer (wild load / SIGSEGV).
          setAddrOfLocalVar(
              capture.getVariable(),
              Address(byrefBase, byrefInfo.type, byrefInfo.byrefAlignment));
        } else if (captureFieldTy->isReferenceType() &&
                   !captureTy->isReferenceType()) {
          mlir::Value refValue = builder.createAlignedLoad(
              getLoc(blockExpr->getExprLoc()), fieldTy, fieldPtr, align);
          mlir::Type objectTy = convertTypeForMem(captureTy);
          auto objectPtrTy = builder.getPointerTo(objectTy);
          if (refValue.getType() != objectPtrTy)
            refValue = builder.createBitcast(getLoc(blockExpr->getExprLoc()),
                                             refValue, objectPtrTy);
          setAddrOfLocalVar(capture.getVariable(),
                            Address(refValue, objectTy,
                                    getContext().getTypeAlignInChars(captureTy)));
        } else {
          setAddrOfLocalVar(capture.getVariable(),
                            Address(fieldPtr, fieldTy, align));
        }
      }
    }

    if (body && shouldEmitLifetimeMarkers)
      bypasses.Init(body);

    if (body && mlir::failed(emitFunctionBody(body)))
      return nullptr;

    finishFunction(body ? cast<CompoundStmt>(body)->getRBracLoc() : loc);
  }

  eraseEmptyAndUnusedBlocks(fn);
  return fn;
}

void CIRGenFunction::generateObjCMethod(const ObjCMethodDecl *omd) {
  // Get the method's symbol name via MangleContext (produces names like
  // "\01-[ClassName method]").
  SmallString<256> nameBuf;
  {
    llvm::raw_svector_ostream os(nameBuf);
    cgm.mangleObjCMethodName(omd, os);
  }
  StringRef methodName = nameBuf.str();

  // Arrange function info and type.
  const CIRGenFunctionInfo &fi =
      cgm.getTypes().arrangeObjCMethodDeclaration(omd);
  cir::FuncType funcType = cgm.getTypes().getFunctionType(fi);

  // Create or get the FuncOp.
  cir::FuncOp fn = cgm.getOrCreateCIRFunction(
      methodName, funcType, GlobalDecl(omd), /*forVTable=*/false,
      /*dontDefer=*/true, /*isThunk=*/false, ForDefinition);

  // ObjC methods have internal linkage (they are referenced via the
  // runtime method tables, not by symbol name).
  if (!omd->isDirectMethod())
    fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);

  // Build the argument list: self, _cmd, then explicit parameters.
  FunctionArgList args;
  args.push_back(omd->getSelfDecl());
  if (!omd->isDirectMethod())
    args.push_back(omd->getCmdDecl());
  args.append(omd->param_begin(), omd->param_end());

  curGD = GlobalDecl(omd);

  SourceLocation loc = omd->getLocation();
  Stmt *body = omd->getBody();
  SourceRange bodyRange =
      body ? body->getSourceRange() : omd->getLocation();

  SourceLocRAIIObject fnLoc{*this, loc.isValid() ? getLoc(loc)
                                                 : builder.getUnknownLoc()};

  auto validMLIRLoc = [&](SourceLocation clangLoc) {
    return clangLoc.isValid() ? getLoc(clangLoc) : builder.getUnknownLoc();
  };
  const mlir::Location fusedLoc = mlir::FusedLoc::get(
      &getMLIRContext(),
      {validMLIRLoc(bodyRange.getBegin()), validMLIRLoc(bodyRange.getEnd())});
  mlir::Block *entryBB = fn.addEntryBlock();

  // Create a scope in the symbol table to hold variable declarations.
  SymTableScopeTy varScope(symbolTable);

  emitStartEHSpec(omd);

  {
    LexicalScope lexScope(*this, fusedLoc, entryBB);
    lexScope.setClangSourceRange(bodyRange);

    startFunction(GlobalDecl(omd), omd->getReturnType(), fn, funcType, args,
                  loc, bodyRange.getBegin());

    if (omd->isDirectMethod())
      emitObjCDirectMethodPrologue(omd);

    PGO->assignRegionCounters(GlobalDecl(omd), fn);

    // Emit the method body.
    if (body) {
      assert(isa<CompoundStmt>(body));
      if (mlir::failed(emitFunctionBody(body)))
        return;
    }

    if (mlir::failed(fn.verifyBody()))
      return;

    finishFunction(bodyRange.getEnd());
  }

  emitEndEHSpec(omd);

  eraseEmptyAndUnusedBlocks(fn);

  cgm.setCIRFunctionAttributesForDefinition(GlobalDecl(omd), fn);
}

namespace {

enum class CIRObjCPropertyImplStrategyKind {
  Native,
  GetSetProperty,
  SetPropertyAndExpressionGet,
  CopyStruct,
  Expression,
  Unsupported,
};

static bool usesGNUstep2ObjCRuntime(const CIRGenFunction &cgf) {
  const ObjCRuntime runtime = cgf.getContext().getLangOpts().ObjCRuntime;
  return runtime.getKind() == ObjCRuntime::GNUstep &&
         runtime.getVersion() >= VersionTuple(2);
}

static bool usesGNUObjCRuntime(const CIRGenFunction &cgf) {
  const ObjCRuntime runtime = cgf.getContext().getLangOpts().ObjCRuntime;
  return runtime.getKind() == ObjCRuntime::GCC ||
         runtime.getKind() == ObjCRuntime::GNUstep;
}

static bool usesLegacyGNUObjCRuntime(const CIRGenFunction &cgf) {
  const ObjCRuntime runtime = cgf.getContext().getLangOpts().ObjCRuntime;
  return runtime.getKind() == ObjCRuntime::GCC ||
         (runtime.getKind() == ObjCRuntime::GNUstep &&
          runtime.getVersion() < VersionTuple(2));
}

static bool usesNonFragileGNUObjCRuntime(const CIRGenFunction &cgf) {
  return usesGNUObjCRuntime(cgf) &&
         cgf.getContext().getLangOpts().ObjCRuntime.isNonFragile();
}

static bool usesGNUstepCppAtomicObjectRuntime(const CIRGenFunction &cgf) {
  const ObjCRuntime runtime = cgf.getContext().getLangOpts().ObjCRuntime;
  return runtime.getKind() == ObjCRuntime::GNUstep &&
         runtime.getVersion() >= VersionTuple(1, 7);
}

static bool isObjCClassLayoutKnownStatically(const ObjCInterfaceDecl *id) {
  assert(id && "expected Objective-C interface");

  for (; id; id = id->getSuperClass()) {
    if (id->getIdentifier() && id->getIdentifier()->getName() == "NSObject")
      return true;
    if (!id->getImplementation())
      return false;
  }

  return true;
}

static bool hasUnalignedObjCPropertyAtomics(llvm::Triple::ArchType) {
  return false;
}

static CharUnits getMaxObjCPropertyAtomicAccessSize(CIRGenModule &cgm,
                                                    llvm::Triple::ArchType) {
  return cgm.getPointerAlign();
}

class CIRObjCPropertyImplStrategy {
public:
  CIRObjCPropertyImplStrategyKind getKind() const { return kind; }

  bool hasStrongMember() const { return hasStrong; }
  bool isAtomic() const { return isAtomicProperty; }
  bool isCopy() const { return isCopyProperty; }
  CharUnits getIvarSize() const { return ivarSize; }

  CIRObjCPropertyImplStrategy(CIRGenModule &cgm,
                              const ObjCPropertyImplDecl *pid) {
    const ObjCPropertyDecl *property = pid->getPropertyDecl();
    ObjCIvarDecl *ivar = pid->getPropertyIvarDecl();
    if (!property || !ivar || property->isClassProperty()) {
      kind = CIRObjCPropertyImplStrategyKind::Unsupported;
      return;
    }

    ObjCPropertyDecl::SetterKind setterKind = property->getSetterKind();
    isCopyProperty = setterKind == ObjCPropertyDecl::Copy;
    isAtomicProperty = property->isAtomic();
    hasStrong = false;

    QualType ivarTy = ivar->getType();
    auto typeInfo = cgm.getASTContext().getTypeInfoInChars(ivarTy);
    ivarSize = typeInfo.Width;
    ivarAlignment = typeInfo.Align;

    if (isCopyProperty) {
      kind = isAtomicProperty ? CIRObjCPropertyImplStrategyKind::GetSetProperty
                              : CIRObjCPropertyImplStrategyKind::
                                    SetPropertyAndExpressionGet;
      return;
    }

    if (setterKind == ObjCPropertyDecl::Retain) {
      if (cgm.getLangOpts().getGC() == LangOptions::GCOnly) {
      } else if (cgm.getLangOpts().ObjCAutoRefCount && !isAtomicProperty) {
        if (ivarTy.getObjCLifetime() == Qualifiers::OCL_Strong)
          kind = CIRObjCPropertyImplStrategyKind::Expression;
        else
          kind = CIRObjCPropertyImplStrategyKind::SetPropertyAndExpressionGet;
        return;
      } else if (!isAtomicProperty) {
        kind = CIRObjCPropertyImplStrategyKind::SetPropertyAndExpressionGet;
        return;
      } else {
        kind = CIRObjCPropertyImplStrategyKind::GetSetProperty;
        return;
      }
    }

    if (!isAtomicProperty) {
      kind = CIRObjCPropertyImplStrategyKind::Expression;
      return;
    }

    if (ivar->isBitField()) {
      kind = CIRObjCPropertyImplStrategyKind::Expression;
      return;
    }

    if (ivarTy.hasNonTrivialObjCLifetime() ||
        (cgm.getLangOpts().getGC() &&
         cgm.getASTContext().getObjCGCAttrKind(ivarTy))) {
      kind = CIRObjCPropertyImplStrategyKind::Expression;
      return;
    }

    if (cgm.getLangOpts().getGC())
      if (const auto *rd = ivarTy->getAsRecordDecl())
        hasStrong = rd->hasObjectMember();

    if (hasStrong) {
      kind = CIRObjCPropertyImplStrategyKind::CopyStruct;
      return;
    }

    if (!ivarSize.isPowerOfTwo()) {
      kind = CIRObjCPropertyImplStrategyKind::CopyStruct;
      return;
    }

    llvm::Triple::ArchType arch = cgm.getTarget().getTriple().getArch();
    if (ivarAlignment < ivarSize && !hasUnalignedObjCPropertyAtomics(arch)) {
      kind = CIRObjCPropertyImplStrategyKind::CopyStruct;
      return;
    }

    if (ivarSize > getMaxObjCPropertyAtomicAccessSize(cgm, arch)) {
      kind = CIRObjCPropertyImplStrategyKind::CopyStruct;
      return;
    }

    kind = CIRObjCPropertyImplStrategyKind::Native;
  }

private:
  CIRObjCPropertyImplStrategyKind kind =
      CIRObjCPropertyImplStrategyKind::Unsupported;
  bool isAtomicProperty = false;
  bool isCopyProperty = false;
  bool hasStrong = false;
  CharUnits ivarSize = CharUnits::Zero();
  CharUnits ivarAlignment = CharUnits::Zero();
};

static bool hasTrivialGetExpr(const ObjCPropertyImplDecl *pid) {
  const Expr *getter = pid->getGetterCXXConstructor();
  if (!getter)
    return true;

  if (getter->isGLValue())
    return false;

  if (const auto *construct = dyn_cast<CXXConstructExpr>(getter))
    return construct->getConstructor()->isTrivial();

  assert(isa<ExprWithCleanups>(getter));
  return false;
}

static bool hasTrivialSetExpr(const ObjCPropertyImplDecl *pid) {
  Expr *setter = pid->getSetterCXXAssignment();
  if (!setter)
    return true;

  if (auto *call = dyn_cast<CallExpr>(setter)) {
    if (const auto *callee = dyn_cast_or_null<FunctionDecl>(call->getCalleeDecl()))
      return callee->isTrivial();
    return false;
  }

  assert(isa<ExprWithCleanups>(setter));
  return false;
}

static bool useOptimizedObjCPropertySetter(CIRGenModule &cgm) {
  if (cgm.getLangOpts().getGC() != LangOptions::NonGC)
    return false;
  return cgm.getLangOpts().ObjCRuntime.hasOptimizedSetter();
}

static void mangleObjCAccessorName(CIRGenFunction &cgf,
                                   const ObjCMethodDecl *omd,
                                   SmallString<256> &nameBuf) {
  llvm::raw_svector_ostream os(nameBuf);
  cgf.cgm.mangleObjCMethodName(omd, os);
}

static Expr *buildSyntheticObjCSelfLoadExpr(CIRGenFunction &cgf,
                                            const ObjCMethodDecl *omd) {
  ValueDecl *selfDecl = omd->getSelfDecl();
  auto *selfExpr = new (cgf.getContext())
      DeclRefExpr(cgf.getContext(), selfDecl, false, selfDecl->getType(),
                  VK_LValue, SourceLocation());
  return ImplicitCastExpr::Create(cgf.getContext(), selfDecl->getType(),
                                  CK_LValueToRValue, selfExpr, nullptr,
                                  VK_PRValue, FPOptionsOverride());
}

static ObjCIvarRefExpr buildSyntheticObjCIvarRefExpr(CIRGenFunction &cgf,
                                                     const ObjCMethodDecl *omd,
                                                     ObjCIvarDecl *ivar) {
  return ObjCIvarRefExpr(ivar, ivar->getType().getNonReferenceType(),
                         SourceLocation(), SourceLocation(),
                         buildSyntheticObjCSelfLoadExpr(cgf, omd),
                         /*isArrow=*/true, /*isFreeIvar=*/true);
}

static mlir::LogicalResult emitSyntheticVoidReturn(CIRGenFunction &cgf,
                                                   SourceLocation loc) {
  auto *ret = ReturnStmt::Create(cgf.getContext(), loc, nullptr,
                                 /*NRVOCandidate=*/nullptr);
  return cgf.emitReturnStmt(*ret);
}

static std::string mangleGNUObjCTypeEncodingForSymbol(const CIRGenFunction &cgf,
                                                      QualType ty) {
  std::string typeEncoding;
  cgf.getContext().getObjCEncodingForType(ty, typeEncoding);
  if (cgf.getContext().getTargetInfo().getTriple().isOSBinFormatELF())
    llvm::replace(typeEncoding, '@', '\1');
  if (cgf.getContext().getTargetInfo().getTriple().isOSWindows())
    llvm::replace(typeEncoding, '=', '\2');
  return typeEncoding;
}

static cir::GlobalOp getOrCreateObjCIvarOffsetVariable(CIRGenFunction &cgf,
                                                       mlir::Location loc,
                                                       const ObjCInterfaceDecl *id,
                                                       const ObjCIvarDecl *ivar) {
  const ObjCInterfaceDecl *container = ivar->getContainingInterface();
  SmallString<64> name;
  if (usesGNUstep2ObjCRuntime(cgf)) {
    name += "__objc_ivar_offset_";
    name += container->getName();
    name += ".";
    name += ivar->getName();
    name += ".";
    name += mangleGNUObjCTypeEncodingForSymbol(cgf, ivar->getType());
  } else if (usesGNUObjCRuntime(cgf)) {
    name += "__objc_ivar_offset_";
    name += container->getName();
    name += ".";
    name += ivar->getName();
  } else {
    name += "OBJC_IVAR_$_";
    name += container->getObjCRuntimeNameAsString();
    name += ".";
    name += ivar->getName();
  }

  if (auto *existing = cgf.cgm.getGlobalValue(name))
    return mlir::cast<cir::GlobalOp>(existing);

  mlir::Type offsetTy;
  if (usesGNUstep2ObjCRuntime(cgf)) {
    offsetTy = cgf.getBuilder().getUInt32Ty();
  } else if (usesLegacyGNUObjCRuntime(cgf)) {
    offsetTy = cgf.getBuilder().getPointerTo(cgf.getBuilder().getUInt32Ty());
  } else {
    // Darwin non-fragile: share the canonical offset-var type with the
    // class-metadata path (getObjCDarwinClassGlobal) so the definition and this
    // declaration/get_global agree on width and signedness.
    offsetTy = cgf.cgm.getObjCDarwinIvarOffsetVarType();
  }

  const bool isCOFF = cgf.getTarget().getTriple().isOSBinFormatCOFF();
  const bool isGNUstep2DLLImport =
      usesGNUstep2ObjCRuntime(cgf) && isCOFF && container->hasAttr<DLLImportAttr>();
  uint64_t offsetValue = 0;
  bool isConstant = isObjCClassLayoutKnownStatically(id);
  if (usesGNUstep2ObjCRuntime(cgf)) {
    isConstant =
        !isGNUstep2DLLImport && isObjCClassLayoutKnownStatically(container);
    if (isConstant) {
      uint64_t baseOffset =
          cgf.getContext().lookupFieldBitOffset(container, ivar) /
          cgf.getContext().getCharWidth();
      uint64_t superInstanceSize = 0;
      if (const ObjCInterfaceDecl *superClassDecl = container->getSuperClass())
        superInstanceSize =
            cgf.getContext().getASTObjCInterfaceLayout(superClassDecl)
                .getSize()
                .getQuantity();
      offsetValue = baseOffset - superInstanceSize;
    }
  } else if (isConstant) {
    offsetValue =
        cgf.getContext().lookupFieldBitOffset(id, ivar) /
        cgf.getContext().getCharWidth();
  }

  CharUnits ivarOffsetAlign =
      cgf.getTarget().getTriple().getArch() == llvm::Triple::aarch64
          ? cgf.getContext().getTypeAlignInChars(cgf.getContext().IntTy)
          : cgf.getContext().getTypeAlignInChars(cgf.getContext().LongTy);
  CharUnits gnustep2OffsetAlign =
      cgf.getContext().getTypeSizeInChars(cgf.getContext().IntTy);

  cir::GlobalOp gv = CIRGenModule::createGlobalOp(cgf.cgm, loc, name, offsetTy,
                                                  /*isConstant=*/false);
  gv.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
  if (usesGNUstep2ObjCRuntime(cgf)) {
    const bool isPrivateOrPackage =
        ivar->getAccessControl() == ObjCIvarDecl::Private ||
        ivar->getAccessControl() == ObjCIvarDecl::Package;
    const bool isHidden =
        isPrivateOrPackage || container->getVisibility() == HiddenVisibility;
    if (isHidden) {
      gv.setGlobalVisibility(cir::VisibilityKind::Hidden);
      mlir::SymbolTable::setSymbolVisibility(gv, cgf.cgm.getMLIRVisibility(gv));
    } else {
      gv.setGlobalVisibility(cir::VisibilityKind::Default);
      mlir::SymbolTable::setSymbolVisibility(
          gv, gv.isDeclaration() ? cgf.cgm.getMLIRVisibility(gv)
                                 : mlir::SymbolTable::Visibility::Public);
    }
    if (!isGNUstep2DLLImport)
      gv.setAlignmentAttr(cgf.cgm.getSize(gnustep2OffsetAlign));
    if (isCOFF) {
      gv->removeAttr("cir.dll_storage_class");
      gv->removeAttr("dso_local");
      if (isGNUstep2DLLImport) {
        gv->setAttr("cir.dll_storage_class",
                    cgf.cgm.getStringAttrCached("dllimport"));
      } else if (container->hasAttr<DLLExportAttr>() && !isHidden) {
        gv->setAttr("cir.dll_storage_class",
                    cgf.cgm.getStringAttrCached("dllexport"));
        cgf.cgm.setDSOLocal(static_cast<mlir::Operation *>(gv));
      }
    }
    if (!isGNUstep2DLLImport && isConstant)
      gv.setInitialValueAttr(cir::IntAttr::get(offsetTy, offsetValue));
  } else if (usesLegacyGNUObjCRuntime(cgf)) {
    // GNUstep 1.x non-fragile runtimes still use the legacy
    // __objc_ivar_offset_* pointer indirection. Keep this helper aligned with
    // the expression path so synthesized accessors don't try to materialize an
    // integer initializer for a pointer-typed offset global.
    mlir::SymbolTable::setSymbolVisibility(gv,
                                           mlir::SymbolTable::Visibility::Public);
    gv.setAlignmentAttr(cgf.cgm.getSize(cgf.getPointerAlign()));
  } else {
    const bool isDLLImport = isCOFF && container->hasAttr<DLLImportAttr>();
    mlir::SymbolTable::setSymbolVisibility(
        gv, isDLLImport ? cgf.cgm.getMLIRVisibility(gv)
                        : mlir::SymbolTable::Visibility::Public);
    if (!isDLLImport)
      gv.setAlignmentAttr(cgf.cgm.getSize(ivarOffsetAlign));
    if (isCOFF) {
      const bool isPrivateOrPackage =
          ivar->getAccessControl() == ObjCIvarDecl::Private ||
          ivar->getAccessControl() == ObjCIvarDecl::Package;
      if (isDLLImport) {
        gv->setAttr("cir.dll_storage_class",
                    cgf.cgm.getStringAttrCached("dllimport"));
      } else if (container->hasAttr<DLLExportAttr>() && !isPrivateOrPackage) {
        gv->setAttr("cir.dll_storage_class",
                    cgf.cgm.getStringAttrCached("dllexport"));
      }
    }
    if (!isDLLImport) {
      if (!isCOFF)
        gv.setSectionAttr(cgf.cgm.getStringAttrCached("__DATA, __objc_ivar"));
      if (isConstant)
        gv.setInitialValueAttr(cir::IntAttr::get(offsetTy, offsetValue));
    }
  }
  return gv;
}

static mlir::Value emitObjCIvarOffsetAsPointerDiff(CIRGenFunction &cgf,
                                                   const ObjCInterfaceDecl *id,
                                                   const ObjCIvarDecl *ivar,
                                                   mlir::Location loc) {
  mlir::Value offset;
  if (isObjCClassLayoutKnownStatically(id) &&
      !usesNonFragileGNUObjCRuntime(cgf)) {
    uint64_t ivarOffset =
        cgf.getContext().lookupFieldBitOffset(id, ivar) /
        cgf.getContext().getCharWidth();
    offset = cgf.getBuilder().getConstant(
        loc, cir::IntAttr::get(mlir::cast<cir::IntType>(cgf.uIntPtrTy),
                               ivarOffset));
  } else {
    cir::GlobalOp offsetGV = getOrCreateObjCIvarOffsetVariable(cgf, loc, id, ivar);
    mlir::Value offsetAddr = cgf.getBuilder().createGetGlobal(loc, offsetGV);
    CharUnits offsetAlign =
        usesGNUstep2ObjCRuntime(cgf)
            ? cgf.getContext().getTypeSizeInChars(cgf.getContext().IntTy)
            : cgf.getPointerAlign();
    cir::LoadOp load = cgf.getBuilder().createLoad(
        loc, Address(offsetAddr, offsetGV.getSymType(), offsetAlign));
    if (!usesGNUObjCRuntime(cgf))
      load->setAttr("cir.invariant_load",
                    mlir::UnitAttr::get(cgf.getBuilder().getContext()));
    offset = load.getResult();
    if (usesLegacyGNUObjCRuntime(cgf)) {
      auto offsetPtrTy = cgf.getBuilder().getUInt32Ty();
      mlir::Value offsetPtr = cgf.getBuilder().createPtrBitcast(offset, offsetPtrTy);
      cir::LoadOp offsetLoad = cgf.getBuilder().createLoad(
          loc, Address(offsetPtr, offsetPtrTy,
                       cgf.getContext().getTypeSizeInChars(cgf.getContext().IntTy)));
      offset = offsetLoad.getResult();
    }
    if (offset.getType() != cgf.uIntPtrTy)
      offset = cgf.getBuilder().createIntCast(
          offset, mlir::cast<cir::IntType>(cgf.uIntPtrTy));
  }

  if (offset.getType() != cgf.ptrDiffTy)
    offset = cgf.getBuilder().createIntCast(
        offset, mlir::cast<cir::IntType>(cgf.ptrDiffTy));
  return offset;
}

static mlir::Value emitCmdValueForGetterSetterBody(CIRGenFunction &cgf,
                                                   ObjCMethodDecl *omd,
                                                   mlir::Location loc) {
  if (omd->isDirectMethod())
    return cgf.getUndefRValue(cgf.getContext().getObjCSelType()).getValue();
  return cgf.getBuilder().createLoad(loc, cgf.getAddrOfLocalVar(omd->getCmdDecl()));
}

static cir::FuncOp getObjCStructPropertyFunction(CIRGenFunction &cgf,
                                                 bool isGetter) {
  auto &builder = cgf.getBuilder();
  auto voidTy = builder.getVoidTy();
  auto voidPtrTy = builder.getVoidPtrTy();
  mlir::Type sizeTy = usesGNUObjCRuntime(cgf) ? cgf.ptrDiffTy : cgf.sizeTy;
  llvm::StringRef name =
      usesGNUObjCRuntime(cgf)
          ? (isGetter ? "objc_getPropertyStruct" : "objc_setPropertyStruct")
          : "objc_copyStruct";
  return cgf.cgm.createRuntimeFunction(
      builder.getFuncType({voidPtrTy, voidPtrTy, sizeTy, builder.getBoolTy(),
                           builder.getBoolTy()},
                          voidTy),
      name);
}

static cir::FuncOp getObjCPropertyGetterFunction(CIRGenFunction &cgf) {
  auto &builder = cgf.getBuilder();
  auto voidPtrTy = builder.getVoidPtrTy();
  return cgf.cgm.createRuntimeFunction(
      builder.getFuncType({voidPtrTy, voidPtrTy, cgf.ptrDiffTy, builder.getBoolTy()},
                          voidPtrTy),
      "objc_getProperty");
}

static cir::FuncOp getObjCPropertySetterFunction(CIRGenFunction &cgf) {
  auto &builder = cgf.getBuilder();
  auto voidTy = builder.getVoidTy();
  auto voidPtrTy = builder.getVoidPtrTy();
  return cgf.cgm.createRuntimeFunction(
      builder.getFuncType({voidPtrTy, voidPtrTy, cgf.ptrDiffTy, voidPtrTy,
                           builder.getBoolTy(), builder.getBoolTy()},
                          voidTy),
      "objc_setProperty");
}

static cir::FuncOp getObjCOptimizedPropertySetterFunction(CIRGenFunction &cgf,
                                                          bool isAtomic,
                                                          bool isCopy) {
  auto &builder = cgf.getBuilder();
  auto voidTy = builder.getVoidTy();
  auto voidPtrTy = builder.getVoidPtrTy();
  llvm::StringRef name;
  if (isAtomic && isCopy)
    name = "objc_setProperty_atomic_copy";
  else if (isAtomic)
    name = "objc_setProperty_atomic";
  else if (isCopy)
    name = "objc_setProperty_nonatomic_copy";
  else
    name = "objc_setProperty_nonatomic";
  return cgf.cgm.createRuntimeFunction(
      builder.getFuncType({voidPtrTy, voidPtrTy, voidPtrTy, cgf.ptrDiffTy},
                          voidTy),
      name);
}

static cir::FuncOp getObjCCppAtomicObjectFunction(CIRGenFunction &cgf,
                                                  bool isGetter) {
  auto &builder = cgf.getBuilder();
  auto voidTy = builder.getVoidTy();
  auto voidPtrTy = builder.getVoidPtrTy();
  llvm::StringRef name;
  if (usesGNUstepCppAtomicObjectRuntime(cgf))
    name = isGetter ? "objc_getCppObjectAtomic" : "objc_setCppObjectAtomic";
  else
    name = "objc_copyCppObjectAtomic";
  cir::FuncOp fn = cgf.cgm.createRuntimeFunction(
      builder.getFuncType({voidPtrTy, voidPtrTy, voidPtrTy}, voidTy), name);
  // These runtime calls need cir.noundef on the declaration so DirectToLLVM
  // can propagate it to call sites, but the final LLVM declarations should
  // still match OGCG and omit declaration-side noundef.
  fn->removeAttr("cir.no_proto_attrs");
  fn->setAttr("cir.strip_decl_llvm_noundef",
              mlir::UnitAttr::get(&cgf.getMLIRContext()));
  return fn;
}

static void emitStructGetterCall(CIRGenFunction &cgf,
                                 const ObjCMethodDecl *omd,
                                 ObjCIvarDecl *ivar, bool isAtomic,
                                 bool hasStrong,
                                 mlir::Location loc) {
  auto &builder = cgf.getBuilder();
  cir::FuncOp fn = getObjCStructPropertyFunction(cgf, /*isGetter=*/true);
  mlir::Value dest = cgf.returnValue.getPointer();
  if (dest.getType() != builder.getVoidPtrTy())
    dest = builder.createBitcast(loc, dest, builder.getVoidPtrTy());
  ObjCIvarRefExpr ivarRef = buildSyntheticObjCIvarRefExpr(cgf, omd, ivar);
  LValue srcLV = cgf.emitObjCIvarRefLValue(&ivarRef);
  mlir::Value src = srcLV.getPointer();
  if (src.getType() != builder.getVoidPtrTy())
    src = builder.createBitcast(loc, src, builder.getVoidPtrTy());
  mlir::Value size =
      builder.getConstInt(loc, usesGNUObjCRuntime(cgf) ? cgf.ptrDiffTy : cgf.sizeTy,
                          cgf.getContext().getTypeSizeInChars(ivar->getType())
                              .getQuantity());
  builder.createCallOp(
      loc, fn, {dest, src, size, builder.getBool(isAtomic, loc),
                builder.getBool(hasStrong, loc)});
}

static void emitStructSetterCall(CIRGenFunction &cgf, ObjCMethodDecl *omd,
                                 ObjCIvarDecl *ivar, mlir::Location loc) {
  auto &builder = cgf.getBuilder();
  cir::FuncOp fn = getObjCStructPropertyFunction(cgf, /*isGetter=*/false);
  ObjCIvarRefExpr ivarRef = buildSyntheticObjCIvarRefExpr(cgf, omd, ivar);
  LValue ivarLV = cgf.emitObjCIvarRefLValue(&ivarRef);
  mlir::Value dest = ivarLV.getPointer();
  if (dest.getType() != builder.getVoidPtrTy())
    dest = builder.createBitcast(loc, dest, builder.getVoidPtrTy());
  mlir::Value src = builder.createLoad(loc, cgf.getAddrOfLocalVar(*omd->param_begin()));
  if (src.getType() != builder.getVoidPtrTy())
    src = builder.createBitcast(loc, src, builder.getVoidPtrTy());
  mlir::Value size =
      builder.getConstInt(loc, usesGNUObjCRuntime(cgf) ? cgf.ptrDiffTy : cgf.sizeTy,
                          cgf.getContext().getTypeSizeInChars(ivar->getType())
                              .getQuantity());
  builder.createCallOp(loc, fn,
                       {dest, src, size, builder.getTrue(loc), builder.getFalse(loc)});
}

static void emitCPPObjectAtomicGetterCall(CIRGenFunction &cgf,
                                          const ObjCMethodDecl *omd,
                                          ObjCIvarDecl *ivar,
                                          cir::FuncOp atomicHelperFn,
                                          mlir::Location loc) {
  auto &builder = cgf.getBuilder();
  cir::FuncOp fn = getObjCCppAtomicObjectFunction(cgf, /*isGetter=*/true);
  mlir::Value dest = cgf.returnValue.getPointer();
  if (dest.getType() != builder.getVoidPtrTy())
    dest = builder.createBitcast(loc, dest, builder.getVoidPtrTy());
  ObjCIvarRefExpr ivarRef = buildSyntheticObjCIvarRefExpr(cgf, omd, ivar);
  LValue ivarLV = cgf.emitObjCIvarRefLValue(&ivarRef);
  mlir::Value src = ivarLV.getPointer();
  if (src.getType() != builder.getVoidPtrTy())
    src = builder.createBitcast(loc, src, builder.getVoidPtrTy());
  auto helperPtrTy = cir::PointerType::get(atomicHelperFn.getFunctionType());
  mlir::Value helperRef =
      cir::GetGlobalOp::create(builder, loc, helperPtrTy, atomicHelperFn.getSymNameAttr());
  helperRef = builder.createBitcast(loc, helperRef, builder.getVoidPtrTy());
  builder.createCallOp(loc, fn, {dest, src, helperRef});
}

static void emitCPPObjectAtomicSetterCall(CIRGenFunction &cgf, ObjCMethodDecl *omd,
                                          ObjCIvarDecl *ivar, cir::FuncOp atomicHelperFn,
                                          mlir::Location loc) {
  auto &builder = cgf.getBuilder();
  cir::FuncOp fn = getObjCCppAtomicObjectFunction(cgf, /*isGetter=*/false);
  ObjCIvarRefExpr ivarRef = buildSyntheticObjCIvarRefExpr(cgf, omd, ivar);
  LValue ivarLV = cgf.emitObjCIvarRefLValue(&ivarRef);
  mlir::Value dest = ivarLV.getPointer();
  if (dest.getType() != builder.getVoidPtrTy())
    dest = builder.createBitcast(loc, dest, builder.getVoidPtrTy());
  mlir::Value src = builder.createLoad(loc, cgf.getAddrOfLocalVar(*omd->param_begin()));
  if (src.getType() != builder.getVoidPtrTy())
    src = builder.createBitcast(loc, src, builder.getVoidPtrTy());
  auto helperPtrTy = cir::PointerType::get(atomicHelperFn.getFunctionType());
  mlir::Value helperRef =
      cir::GetGlobalOp::create(builder, loc, helperPtrTy, atomicHelperFn.getSymNameAttr());
  helperRef = builder.createBitcast(loc, helperRef, builder.getVoidPtrTy());
  builder.createCallOp(loc, fn, {dest, src, helperRef});
}

struct DestroyObjCIvar final : EHScopeStack::Cleanup {
  DestroyObjCIvar(const ObjCMethodDecl *method, const ObjCIvarDecl *ivar,
                  CIRGenFunction::Destroyer *destroyer,
                  bool useEHCleanupForArray)
      : method(method), ivar(ivar), destroyer(destroyer),
        useEHCleanupForArray(useEHCleanupForArray) {}

  const ObjCMethodDecl *method;
  const ObjCIvarDecl *ivar;
  CIRGenFunction::Destroyer *destroyer;
  bool useEHCleanupForArray;

  void emit(CIRGenFunction &cgf, Flags flags) override {
    ObjCIvarRefExpr ivarRef = buildSyntheticObjCIvarRefExpr(
        cgf, method, const_cast<ObjCIvarDecl *>(ivar));
    LValue ivarLV = cgf.emitObjCIvarRefLValue(&ivarRef);
    bool useEHCleanupForArray =
        flags.isForNormalCleanup() && this->useEHCleanupForArray;
    cgf.emitDestroy(ivarLV.getAddress(), ivar->getType(), destroyer,
                    useEHCleanupForArray);
  }
};

static void destroyARCStrongWithStore(CIRGenFunction &cgf, Address addr,
                                      QualType) {
  mlir::Location loc = addr.getPointer().getLoc();
  mlir::Value null = cgf.getBuilder().getNullValue(addr.getElementType(), loc);
  cgf.emitARCStoreStrongCall(addr, null, loc);
}

static mlir::Value emitObjCAtomicPropertyPtrauthGetter(
    CIRGenFunction &cgf, LValue ivarLV, const ObjCIvarDecl *ivar,
    QualType resultType, mlir::Location loc) {
  QualType ivarType = ivar->getType();
  PointerAuthQualifier ptrAuth = ivarType.getPointerAuth();
  assert(ptrAuth && "expected authenticated ivar type");

  auto atomicIntTy = cir::IntType::get(
      cgf.getBuilder().getContext(), cgf.getContext().getTypeSize(ivarType),
      /*isSigned=*/false);
  Address ivarAddr = ivarLV.getAddress().withElementType(cgf.getBuilder(),
                                                         atomicIntTy);
  mlir::IntegerAttr align = cgf.getBuilder().getAlignmentAttr(
      ivarAddr.getAlignment());
  auto orderAttr =
      cir::MemOrderAttr::get(&cgf.getMLIRContext(), cir::MemOrder::Unordered);
  mlir::Value signedValue = cir::LoadOp::create(
      cgf.getBuilder(), loc, ivarAddr.getPointer(), /*isDeref=*/false,
      /*isVolatile=*/ivarLV.isVolatileQualified(),
      /*is_nontemporal=*/false, align,
      /*sync_scope=*/cir::SyncScopeKindAttr{}, orderAttr, /*tbaa=*/{});
  cgf.cgm.decorateOperationWithTBAA(
      mlir::cast<cir::LoadOp>(signedValue.getDefiningOp()),
      ivarLV.getTBAAInfo());

  mlir::Value result = cgf.emitPointerAuthUnqualify(ptrAuth, signedValue,
                                                    resultType, ivarAddr,
                                                    /*isKnownNonNull=*/false);
  mlir::Type cirResultTy = cgf.convertType(resultType);
  if (result.getType() != cirResultTy)
    result = cgf.getBuilder().createIntToPtr(result, cirResultTy);
  return result;
}

static void emitObjCAtomicPropertyPtrauthSetter(CIRGenFunction &cgf,
                                                LValue ivarLV,
                                                const ObjCIvarDecl *ivar,
                                                QualType sourceType,
                                                mlir::Value value,
                                                mlir::Location loc) {
  QualType ivarType = ivar->getType();
  PointerAuthQualifier ptrAuth = ivarType.getPointerAuth();
  assert(ptrAuth && "expected authenticated ivar type");

  auto atomicIntTy = cir::IntType::get(
      cgf.getBuilder().getContext(), cgf.getContext().getTypeSize(ivarType),
      /*isSigned=*/false);
  Address ivarAddr = ivarLV.getAddress().withElementType(cgf.getBuilder(),
                                                         atomicIntTy);
  value = cgf.emitPointerAuthQualify(ptrAuth, value, sourceType, ivarAddr,
                                     /*isKnownNonNull=*/false);
  if (value.getType() != atomicIntTy)
    value = cgf.getBuilder().createPtrToInt(value, atomicIntTy);

  cir::StoreOp store = cgf.getBuilder().createStore(loc, value, ivarAddr);
  store.setMemOrder(cir::MemOrder::Unordered);
  if (ivarLV.isVolatileQualified())
    store.setIsVolatile(true);
  cgf.cgm.decorateOperationWithTBAA(store, ivarLV.getTBAAInfo());
}

} // namespace

cir::FuncOp CIRGenFunction::generateObjCAtomicGetterCopyHelperFunction(
    const ObjCPropertyImplDecl *pid) {
  const ObjCPropertyDecl *property = pid->getPropertyDecl();
  if (!(property->getPropertyAttributes() & ObjCPropertyAttribute::kind_atomic))
    return {};

  QualType ty = property->getType();
  if (cir::FuncOp helper = cgm.getAtomicGetterHelperFn(ty))
    return helper;

  const bool isNonTrivialCStruct =
      ty.isNonTrivialToPrimitiveCopy() == QualType::PCK_Struct;
  if (!isNonTrivialCStruct) {
    if (!getLangOpts().CPlusPlus || !getLangOpts().ObjCRuntime.hasAtomicCopyHelper() ||
        !ty->isRecordType() || hasTrivialGetExpr(pid))
      return {};
    assert(pid->getGetterCXXConstructor() && "missing synthesized getter expr");
  }

  SourceLocation helperSourceLoc = pid->getLocation();
  if (!helperSourceLoc.isValid())
    helperSourceLoc = pid->getGetterMethodDecl()->getLocation();
  mlir::Location loc =
      helperSourceLoc.isValid() ? getLoc(helperSourceLoc) : builder.getUnknownLoc();
  QualType destTy = getContext().getPointerType(ty);
  QualType srcTy = ty;
  srcTy.addConst();
  srcTy = getContext().getPointerType(srcTy);
  mlir::Type destIRTy = convertType(destTy);
  mlir::Type srcIRTy = convertType(srcTy);
  const IdentifierInfo *helperII =
      &getContext().Idents.get("__copy_helper_atomic_property_");
  SmallVector<QualType, 2> helperArgTys{destTy, srcTy};
  QualType helperFnTy =
      getContext().getFunctionType(getContext().VoidTy, helperArgTys, {});
  FunctionDecl *helperDecl = FunctionDecl::Create(
      getContext(), getContext().getTranslationUnitDecl(), helperSourceLoc,
      helperSourceLoc, helperII, helperFnTy, /*TInfo=*/nullptr, SC_Static,
      /*UsesFPIntrin=*/false, /*isInlineSpecified=*/false,
      /*hasWrittenPrototype=*/false);
  ParmVarDecl *helperParams[2];
  ParmVarDecl *dstParam = ParmVarDecl::Create(
      getContext(), helperDecl, helperSourceLoc, helperSourceLoc,
      /*Id=*/nullptr, destTy,
      getContext().getTrivialTypeSourceInfo(destTy, helperSourceLoc),
      SC_None, /*DefArg=*/nullptr);
  ParmVarDecl *srcParam = ParmVarDecl::Create(
      getContext(), helperDecl, helperSourceLoc, helperSourceLoc,
      /*Id=*/nullptr, srcTy,
      getContext().getTrivialTypeSourceInfo(srcTy, helperSourceLoc), SC_None,
      /*DefArg=*/nullptr);
  helperParams[0] = dstParam;
  helperParams[1] = srcParam;
  helperDecl->setParams(helperParams);
  cir::FuncOp fn = cgm.createCIRFunction(
      loc, cgm.getUniqueGlobalName("__copy_helper_atomic_property_"),
      builder.getFuncType({destIRTy, srcIRTy}, builder.getVoidTy()), nullptr);
  fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
  fn.setSymVisibility("private");
  cgm.setCIRFunctionAttributesForDefinition(GlobalDecl{}, fn);

  mlir::OpBuilder::InsertionGuard guard(builder);
  CIRGenFunction helperCGF(cgm, builder, /*suppressNewContext=*/true);
  helperCGF.curGD = GlobalDecl{};
  SourceLocRAIIObject helperLoc(helperCGF, loc);
  mlir::Block *entry = fn.addEntryBlock();

  FunctionArgList args;
  args.push_back(dstParam);
  args.push_back(srcParam);

  SymTableScopeTy varScope(helperCGF.symbolTable);
  {
    LexicalScope fnScope(helperCGF, loc, entry);
    helperCGF.startFunction(GlobalDecl{}, getContext().VoidTy, fn,
                            fn.getFunctionType(), args, helperSourceLoc,
                            helperSourceLoc);

    auto &helperBuilder = helperCGF.getBuilder();
    mlir::Type objectTy = helperCGF.convertTypeForMem(ty);
    mlir::Type objectPtrTy = helperBuilder.getPointerTo(objectTy);
    mlir::Value dstPtr =
        helperBuilder.createLoad(loc, helperCGF.getAddrOfLocalVar(dstParam));
    if (dstPtr.getType() != objectPtrTy)
      dstPtr = helperBuilder.createBitcast(loc, dstPtr, objectPtrTy);
    Address dstAddr(dstPtr, objectTy, getContext().getTypeAlignInChars(ty));

    if (isNonTrivialCStruct) {
      mlir::Value srcPtr =
          helperBuilder.createLoad(loc, helperCGF.getAddrOfLocalVar(srcParam));
      if (srcPtr.getType() != objectPtrTy)
        srcPtr = helperBuilder.createBitcast(loc, srcPtr, objectPtrTy);
      Address srcAddr(srcPtr, objectTy, getContext().getTypeAlignInChars(ty));
      helperCGF.emitSupportedNonTrivialCStructCopyConstructor(dstAddr, srcAddr,
                                                              ty);
    } else {
      DeclRefExpr srcExpr(getContext(), srcParam, false, srcTy, VK_PRValue,
                          helperSourceLoc);
      auto *srcDeref = UnaryOperator::Create(
          getContext(), &srcExpr, UO_Deref, srcTy->getPointeeType(), VK_LValue,
          OK_Ordinary, helperSourceLoc, false, FPOptionsOverride());

      const auto *ctorExpr = cast<CXXConstructExpr>(pid->getGetterCXXConstructor());
      SmallVector<Expr *, 4> ctorArgs;
      ctorArgs.push_back(srcDeref);
      for (const Expr *arg : llvm::drop_begin(ctorExpr->arguments()))
        ctorArgs.push_back(const_cast<Expr *>(arg));

      auto *helperCtor = CXXConstructExpr::Create(
          getContext(), ty, helperSourceLoc, ctorExpr->getConstructor(),
          ctorExpr->isElidable(), ctorArgs, ctorExpr->hadMultipleCandidates(),
          ctorExpr->isListInitialization(),
          ctorExpr->isStdInitListInitialization(),
          ctorExpr->requiresZeroInitialization(),
          ctorExpr->getConstructionKind(), SourceRange(helperSourceLoc));

      helperCGF.emitAggExpr(
          helperCtor,
          AggValueSlot::forAddr(dstAddr, ty.getQualifiers(),
                                AggValueSlot::IsDestructed,
                                AggValueSlot::IsNotAliased,
                                AggValueSlot::DoesNotOverlap));
    }

    cir::ReturnOp::create(helperBuilder, loc);
    if (mlir::failed(fn.verifyBody()))
      return {};
    helperCGF.finishFunction(SourceLocation());
  }

  cgm.setAtomicGetterHelperFn(ty, fn);
  return fn;
}

cir::FuncOp CIRGenFunction::generateObjCAtomicSetterCopyHelperFunction(
    const ObjCPropertyImplDecl *pid) {
  const ObjCPropertyDecl *property = pid->getPropertyDecl();
  if (!(property->getPropertyAttributes() & ObjCPropertyAttribute::kind_atomic))
    return {};

  QualType ty = property->getType();
  if (cir::FuncOp helper = cgm.getAtomicSetterHelperFn(ty))
    return helper;

  const bool isNonTrivialCStruct =
      ty.isNonTrivialToPrimitiveCopy() == QualType::PCK_Struct;
  if (!isNonTrivialCStruct) {
    if (!getLangOpts().CPlusPlus || !getLangOpts().ObjCRuntime.hasAtomicCopyHelper() ||
        !ty->isRecordType() || hasTrivialSetExpr(pid))
      return {};
    assert(pid->getSetterCXXAssignment() && "missing synthesized setter expr");
  }

  SourceLocation helperSourceLoc = pid->getLocation();
  if (!helperSourceLoc.isValid())
    helperSourceLoc = pid->getSetterMethodDecl()->getLocation();
  mlir::Location loc =
      helperSourceLoc.isValid() ? getLoc(helperSourceLoc) : builder.getUnknownLoc();
  QualType destTy = getContext().getPointerType(ty);
  QualType srcTy = ty;
  srcTy.addConst();
  srcTy = getContext().getPointerType(srcTy);
  mlir::Type destIRTy = convertType(destTy);
  mlir::Type srcIRTy = convertType(srcTy);
  const IdentifierInfo *helperII =
      &getContext().Idents.get("__assign_helper_atomic_property_");
  SmallVector<QualType, 2> helperArgTys{destTy, srcTy};
  QualType helperFnTy =
      getContext().getFunctionType(getContext().VoidTy, helperArgTys, {});
  FunctionDecl *helperDecl = FunctionDecl::Create(
      getContext(), getContext().getTranslationUnitDecl(), helperSourceLoc,
      helperSourceLoc, helperII, helperFnTy, /*TInfo=*/nullptr, SC_Static,
      /*UsesFPIntrin=*/false, /*isInlineSpecified=*/false,
      /*hasWrittenPrototype=*/false);
  ParmVarDecl *helperParams[2];
  ParmVarDecl *dstParam = ParmVarDecl::Create(
      getContext(), helperDecl, helperSourceLoc, helperSourceLoc,
      /*Id=*/nullptr, destTy,
      getContext().getTrivialTypeSourceInfo(destTy, helperSourceLoc),
      SC_None, /*DefArg=*/nullptr);
  ParmVarDecl *srcParam = ParmVarDecl::Create(
      getContext(), helperDecl, helperSourceLoc, helperSourceLoc,
      /*Id=*/nullptr, srcTy,
      getContext().getTrivialTypeSourceInfo(srcTy, helperSourceLoc), SC_None,
      /*DefArg=*/nullptr);
  helperParams[0] = dstParam;
  helperParams[1] = srcParam;
  helperDecl->setParams(helperParams);
  cir::FuncOp fn = cgm.createCIRFunction(
      loc, cgm.getUniqueGlobalName("__assign_helper_atomic_property_"),
      builder.getFuncType({destIRTy, srcIRTy}, builder.getVoidTy()), nullptr);
  fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
  fn.setSymVisibility("private");
  cgm.setCIRFunctionAttributesForDefinition(GlobalDecl{}, fn);

  mlir::OpBuilder::InsertionGuard guard(builder);
  CIRGenFunction helperCGF(cgm, builder, /*suppressNewContext=*/true);
  helperCGF.curGD = GlobalDecl{};
  SourceLocRAIIObject helperLoc(helperCGF, loc);
  mlir::Block *entry = fn.addEntryBlock();

  FunctionArgList args;
  args.push_back(dstParam);
  args.push_back(srcParam);

  SymTableScopeTy varScope(helperCGF.symbolTable);
  {
    LexicalScope fnScope(helperCGF, loc, entry);
    helperCGF.startFunction(GlobalDecl{}, getContext().VoidTy, fn,
                            fn.getFunctionType(), args, helperSourceLoc,
                            helperSourceLoc);

    auto &helperBuilder = helperCGF.getBuilder();
    if (isNonTrivialCStruct) {
      mlir::Type objectTy = helperCGF.convertTypeForMem(ty);
      mlir::Type objectPtrTy = helperBuilder.getPointerTo(objectTy);
      mlir::Value dstPtr =
          helperBuilder.createLoad(loc, helperCGF.getAddrOfLocalVar(dstParam));
      if (dstPtr.getType() != objectPtrTy)
        dstPtr = helperBuilder.createBitcast(loc, dstPtr, objectPtrTy);
      mlir::Value srcPtr =
          helperBuilder.createLoad(loc, helperCGF.getAddrOfLocalVar(srcParam));
      if (srcPtr.getType() != objectPtrTy)
        srcPtr = helperBuilder.createBitcast(loc, srcPtr, objectPtrTy);
      Address dstAddr(dstPtr, objectTy, getContext().getTypeAlignInChars(ty));
      Address srcAddr(srcPtr, objectTy, getContext().getTypeAlignInChars(ty));
      helperCGF.emitSupportedNonTrivialCStructMoveAssignment(dstAddr, srcAddr,
                                                             ty);
    } else {
      DeclRefExpr dstExpr(getContext(), dstParam, false, destTy, VK_PRValue,
                          helperSourceLoc);
      auto *dstDeref = UnaryOperator::Create(
          getContext(), &dstExpr, UO_Deref, destTy->getPointeeType(), VK_LValue,
          OK_Ordinary, helperSourceLoc, false, FPOptionsOverride());
      DeclRefExpr srcExpr(getContext(), srcParam, false, srcTy, VK_PRValue,
                          helperSourceLoc);
      auto *srcDeref = UnaryOperator::Create(
          getContext(), &srcExpr, UO_Deref, srcTy->getPointeeType(), VK_LValue,
          OK_Ordinary, helperSourceLoc, false, FPOptionsOverride());

      Expr *opArgs[2] = {dstDeref, srcDeref};
      auto *calleeExpr = cast<CallExpr>(pid->getSetterCXXAssignment());
      auto *helperCall = CXXOperatorCallExpr::Create(
          getContext(), OO_Equal, calleeExpr->getCallee(), opArgs,
          destTy->getPointeeType(), VK_LValue, helperSourceLoc,
          FPOptionsOverride());
      if (mlir::failed(helperCGF.emitStmt(helperCall, /*useCurrentScope=*/true)))
        return {};
    }

    cir::ReturnOp::create(helperBuilder, loc);
    if (mlir::failed(fn.verifyBody()))
      return {};
    helperCGF.finishFunction(SourceLocation());
  }

  cgm.setAtomicSetterHelperFn(ty, fn);
  return fn;
}

void CIRGenFunction::generateObjCConstructorMethod(ObjCImplementationDecl *imp,
                                                   ObjCMethodDecl *omd) {
  assert(imp && omd && "expected synthesized Objective-C constructor");

  omd->createImplicitParams(getContext(), imp->getClassInterface());

  SmallString<256> nameBuf;
  mangleObjCAccessorName(*this, omd, nameBuf);
  const CIRGenFunctionInfo &fi =
      cgm.getTypes().arrangeObjCMethodDeclaration(omd);
  cir::FuncType funcType = cgm.getTypes().getFunctionType(fi);
  cir::FuncOp fn = cgm.getOrCreateCIRFunction(
      nameBuf.str(), funcType, GlobalDecl(omd), /*forVTable=*/false,
      /*dontDefer=*/true, /*isThunk=*/false, ForDefinition);
  if (!omd->isDirectMethod())
    fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);

  FunctionArgList args;
  args.push_back(omd->getSelfDecl());
  if (!omd->isDirectMethod())
    args.push_back(omd->getCmdDecl());
  args.append(omd->param_begin(), omd->param_end());

  curGD = GlobalDecl(omd);
  SourceLocation loc = omd->getLocation();
  SourceRange bodyRange = omd->getSourceRange();
  SourceLocRAIIObject fnLoc{*this, loc.isValid() ? getLoc(loc)
                                                 : builder.getUnknownLoc()};
  auto validMLIRLoc = [&](SourceLocation clangLoc) {
    return clangLoc.isValid() ? getLoc(clangLoc) : builder.getUnknownLoc();
  };
  const mlir::Location fusedLoc = mlir::FusedLoc::get(
      &getMLIRContext(),
      {validMLIRLoc(bodyRange.getBegin()), validMLIRLoc(bodyRange.getEnd())});
  mlir::Block *entryBB = fn.addEntryBlock();
  SymTableScopeTy varScope(symbolTable);

  emitStartEHSpec(omd);

  {
    LexicalScope lexScope(*this, fusedLoc, entryBB);
    lexScope.setClangSourceRange(bodyRange);
    startFunction(GlobalDecl(omd), omd->getReturnType(), fn, funcType, args,
                  loc, bodyRange.getBegin());

    // .cxx_construct returns self directly; ARC must not autorelease it.
    autoreleaseResult = false;

    for (const auto *ivarInit : imp->inits()) {
      auto *ivar = cast<ObjCIvarDecl>(ivarInit->getAnyMember());
      ObjCIvarRefExpr ivarRef = buildSyntheticObjCIvarRefExpr(*this, omd, ivar);
      emitExprAsInit(ivarInit->getInit(), ivar, emitObjCIvarRefLValue(&ivarRef),
                     /*capturedByInit=*/false);
    }

    mlir::Value selfValue =
        emitScalarExpr(buildSyntheticObjCSelfLoadExpr(*this, omd));
    QualType idTy = getContext().getObjCIdType();
    mlir::Type cirIdTy = convertType(idTy);
    if (selfValue.getType() != cirIdTy)
      selfValue = builder.createBitcast(validMLIRLoc(loc), selfValue, cirIdTy);
    emitReturnOfRValue(validMLIRLoc(loc), RValue::get(selfValue), idTy);

    if (mlir::failed(fn.verifyBody()))
      return;
    finishFunction(bodyRange.getEnd());
  }

  emitEndEHSpec(omd);
  eraseEmptyAndUnusedBlocks(fn);
  cgm.setCIRFunctionAttributesForDefinition(GlobalDecl(omd), fn);
}

void CIRGenFunction::generateObjCDestructorMethod(ObjCImplementationDecl *imp,
                                                  ObjCMethodDecl *omd) {
  assert(imp && omd && "expected synthesized Objective-C destructor");

  omd->createImplicitParams(getContext(), imp->getClassInterface());

  SmallString<256> nameBuf;
  mangleObjCAccessorName(*this, omd, nameBuf);
  const CIRGenFunctionInfo &fi =
      cgm.getTypes().arrangeObjCMethodDeclaration(omd);
  cir::FuncType funcType = cgm.getTypes().getFunctionType(fi);
  cir::FuncOp fn = cgm.getOrCreateCIRFunction(
      nameBuf.str(), funcType, GlobalDecl(omd), /*forVTable=*/false,
      /*dontDefer=*/true, /*isThunk=*/false, ForDefinition);
  if (!omd->isDirectMethod())
    fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);

  FunctionArgList args;
  args.push_back(omd->getSelfDecl());
  if (!omd->isDirectMethod())
    args.push_back(omd->getCmdDecl());
  args.append(omd->param_begin(), omd->param_end());

  curGD = GlobalDecl(omd);
  SourceLocation loc = omd->getLocation();
  SourceRange bodyRange = omd->getSourceRange();
  SourceLocRAIIObject fnLoc{*this, loc.isValid() ? getLoc(loc)
                                                 : builder.getUnknownLoc()};
  auto validMLIRLoc = [&](SourceLocation clangLoc) {
    return clangLoc.isValid() ? getLoc(clangLoc) : builder.getUnknownLoc();
  };
  const mlir::Location fusedLoc = mlir::FusedLoc::get(
      &getMLIRContext(),
      {validMLIRLoc(bodyRange.getBegin()), validMLIRLoc(bodyRange.getEnd())});
  mlir::Block *entryBB = fn.addEntryBlock();
  SymTableScopeTy varScope(symbolTable);

  emitStartEHSpec(omd);

  {
    LexicalScope lexScope(*this, fusedLoc, entryBB);
    lexScope.setClangSourceRange(bodyRange);
    startFunction(GlobalDecl(omd), omd->getReturnType(), fn, funcType, args,
                  loc, bodyRange.getBegin());

    RunCleanupsScope runCleanups(*this);
    const ObjCInterfaceDecl *iface = imp->getClassInterface();
    for (const ObjCIvarDecl *ivar = iface->all_declared_ivar_begin(); ivar;
         ivar = ivar->getNextIvar()) {
      QualType ivarTy = ivar->getType();
      QualType::DestructionKind dtorKind = ivarTy.isDestructedType();
      if (!dtorKind)
        continue;

      Destroyer *destroyer = dtorKind == QualType::DK_objc_strong_lifetime
                                 ? destroyARCStrongWithStore
                                 : getDestroyer(dtorKind);
      CleanupKind cleanupKind = getCleanupKind(dtorKind);
      ehStack.pushCleanup<DestroyObjCIvar>(cleanupKind, omd, ivar, destroyer,
                                           cleanupKind & EHCleanup);
    }
    runCleanups.forceCleanup();
    cir::ReturnOp::create(builder, validMLIRLoc(loc));

    if (mlir::failed(fn.verifyBody()))
      return;
    finishFunction(bodyRange.getEnd());
  }

  emitEndEHSpec(omd);
  eraseEmptyAndUnusedBlocks(fn);
  cgm.setCIRFunctionAttributesForDefinition(GlobalDecl(omd), fn);
}

void CIRGenFunction::generateObjCPropertyGetter(
    const ObjCImplementationDecl *imp, const ObjCPropertyImplDecl *pid) {
  ObjCMethodDecl *getterMethod = pid->getGetterMethodDecl();
  assert(getterMethod && "expected synthesized Objective-C getter");
  cir::FuncOp atomicHelperFn = generateObjCAtomicGetterCopyHelperFunction(pid);

  SmallString<256> nameBuf;
  mangleObjCAccessorName(*this, getterMethod, nameBuf);
  const CIRGenFunctionInfo &fi =
      cgm.getTypes().arrangeObjCMethodDeclaration(getterMethod);
  cir::FuncType funcType = cgm.getTypes().getFunctionType(fi);
  cir::FuncOp fn = cgm.getOrCreateCIRFunction(
      nameBuf.str(), funcType, GlobalDecl(getterMethod), /*forVTable=*/false,
      /*dontDefer=*/true, /*isThunk=*/false, ForDefinition);
  if (!getterMethod->isDirectMethod())
    fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);

  FunctionArgList args;
  args.push_back(getterMethod->getSelfDecl());
  if (!getterMethod->isDirectMethod())
    args.push_back(getterMethod->getCmdDecl());

  curGD = GlobalDecl(getterMethod);
  SourceLocation loc = getterMethod->getLocation();
  SourceRange bodyRange = getterMethod->getSourceRange();
  SourceLocRAIIObject fnLoc{*this, loc.isValid() ? getLoc(loc)
                                                 : builder.getUnknownLoc()};
  auto validMLIRLoc = [&](SourceLocation clangLoc) {
    return clangLoc.isValid() ? getLoc(clangLoc) : builder.getUnknownLoc();
  };
  auto emitGetterReturnFromSlot = [&](mlir::Location retLoc) {
    auto *retBlock = curLexScope->getOrCreateRetBlock(*this, retLoc);
    mlir::Region *parentRegion = builder.getBlock()->getParent();
    emitBranchThroughCleanup(retLoc, returnBlock(retBlock));
    builder.createBlock(parentRegion);
  };
  mlir::Location fusedLoc = mlir::FusedLoc::get(
      &getMLIRContext(),
      {validMLIRLoc(bodyRange.getBegin()), validMLIRLoc(bodyRange.getEnd())});
  mlir::Block *entryBB = fn.addEntryBlock();
  SymTableScopeTy varScope(symbolTable);

  emitStartEHSpec(getterMethod);

  {
    LexicalScope lexScope(*this, fusedLoc, entryBB);
    lexScope.setClangSourceRange(bodyRange);
    startFunction(GlobalDecl(getterMethod), getterMethod->getReturnType(), fn,
                  funcType, args, loc, bodyRange.getBegin());
    if (getterMethod->isDirectMethod())
      emitObjCDirectMethodPrologue(getterMethod);

    ObjCIvarDecl *ivar = pid->getPropertyIvarDecl();
    QualType ivarTy = ivar->getType();
    QualType propertyTy = pid->getPropertyDecl()->getType();
    ObjCIvarRefExpr ivarRef =
        buildSyntheticObjCIvarRefExpr(*this, getterMethod, ivar);

    if (ivarTy.isNonTrivialToPrimitiveCopy() == QualType::PCK_Struct) {
      if (!atomicHelperFn) {
        LValue srcLV = emitObjCIvarRefLValue(&ivarRef);
        emitSupportedNonTrivialCStructCopyConstructor(returnValue,
                                                      srcLV.getAddress(), ivarTy);
      } else {
        emitCPPObjectAtomicGetterCall(*this, getterMethod, ivar, atomicHelperFn,
                                      validMLIRLoc(loc));
      }
      emitGetterReturnFromSlot(validMLIRLoc(loc));
    } else if (!hasTrivialGetExpr(pid)) {
      const Expr *getterExpr = pid->getGetterCXXConstructor();
      if (!getterExpr)
        getterExpr = &ivarRef;
      if (!atomicHelperFn) {
        auto *ret = ReturnStmt::Create(
            getContext(), SourceLocation(), const_cast<Expr *>(getterExpr),
            /*NRVOCandidate=*/nullptr);
        if (mlir::failed(emitReturnStmt(*ret)))
          return;
      } else {
        emitCPPObjectAtomicGetterCall(*this, getterMethod, ivar, atomicHelperFn,
                                      validMLIRLoc(loc));
        emitGetterReturnFromSlot(validMLIRLoc(loc));
      }
    } else {
      CIRObjCPropertyImplStrategy strategy(cgm, pid);
      switch (strategy.getKind()) {
      case CIRObjCPropertyImplStrategyKind::Native: {
        LValue ivarLV = emitObjCIvarRefLValue(&ivarRef);
        mlir::Value value;
        if (ivarTy.getPointerAuth())
          value = emitObjCAtomicPropertyPtrauthGetter(
              *this, ivarLV, ivar, getterMethod->getReturnType(),
              validMLIRLoc(loc));
        else
          value = emitAtomicLoad(ivarLV, loc, cir::MemOrder::Unordered,
                                 /*isVolatile=*/ivarLV.isVolatileQualified())
                      .getValue();
        autoreleaseResult = false;
        cir::ReturnOp::create(builder, validMLIRLoc(loc), value);
        builder.createBlock(builder.getBlock()->getParent());
        break;
      }
      case CIRObjCPropertyImplStrategyKind::GetSetProperty: {
        if (ivarTy.getPointerAuth()) {
          cgm.errorNYI(pid->getSourceRange(),
                       "Obj-C getter requiring pointer authentication");
          return;
        }
        mlir::Value cmd =
            emitCmdValueForGetterSetterBody(*this, getterMethod, validMLIRLoc(loc));
        mlir::Value self =
            emitScalarExpr(buildSyntheticObjCSelfLoadExpr(*this, getterMethod));
        if (self.getType() != builder.getVoidPtrTy())
          self = builder.createBitcast(validMLIRLoc(loc), self,
                                       builder.getVoidPtrTy());
        if (cmd.getType() != builder.getVoidPtrTy())
          cmd = builder.createBitcast(validMLIRLoc(loc), cmd,
                                      builder.getVoidPtrTy());
        mlir::Value ivarOffset = emitObjCIvarOffsetAsPointerDiff(
            *this, imp->getClassInterface(), ivar, validMLIRLoc(loc));
        auto call = builder.createCallOp(
            validMLIRLoc(loc), getObjCPropertyGetterFunction(*this),
            {self, cmd, ivarOffset, builder.getTrue(validMLIRLoc(loc))});
        mlir::Value value = call.getResult();
        mlir::Type retTy = convertType(getterMethod->getReturnType());
        if (value.getType() != retTy)
          value = builder.createBitcast(validMLIRLoc(loc), value, retTy);
        autoreleaseResult = false;
        emitReturnOfRValue(validMLIRLoc(loc), RValue::get(value), propertyTy);
        break;
      }
      case CIRObjCPropertyImplStrategyKind::CopyStruct:
        emitStructGetterCall(*this, getterMethod, ivar, strategy.isAtomic(),
                             strategy.hasStrongMember(), validMLIRLoc(loc));
        emitGetterReturnFromSlot(validMLIRLoc(loc));
        break;
      case CIRObjCPropertyImplStrategyKind::Expression:
      case CIRObjCPropertyImplStrategyKind::SetPropertyAndExpressionGet: {
        if (!pid->getGetterCXXConstructor() && getLangOpts().ObjCAutoRefCount &&
            CIRGenFunction::getEvaluationKind(propertyTy) == cir::TEK_Scalar &&
            ivarTy.getObjCLifetime() == Qualifiers::OCL_Weak) {
          LValue ivarLV = emitObjCIvarRefLValue(&ivarRef);
          mlir::Value value = emitARCLoadWeakRetained(ivarLV.getAddress());
          mlir::Type retTy = convertType(getterMethod->getReturnType());
          if (value.getType() != retTy)
            value = builder.createBitcast(validMLIRLoc(loc), value, retTy);
          emitReturnOfRValue(validMLIRLoc(loc), RValue::get(value), propertyTy);
          break;
        }

        const Expr *getterExpr = pid->getGetterCXXConstructor();
        // A synthesized scalar getter that returns the ivar directly (no C++
        // constructor) loads it at +0: the ivar ref carries no
        // CK_ARCProduceObject, so emitScalarExpr produces an unretained value.
        // Classic clang returns that +0 value and SUPPRESSES the epilog
        // autorelease (CGObjC.cpp). clangir must do the same; otherwise the
        // epilog emits objc_autoreleaseReturnValue on a value it never retained
        // -- an OVER-RELEASE that deallocates the returned property value early
        // (use-after-free). Mirrors the sibling Native/GetSetProperty strategies
        // above which also clear autoreleaseResult.
        if (!getterExpr && getLangOpts().ObjCAutoRefCount &&
            CIRGenFunction::getEvaluationKind(propertyTy) == cir::TEK_Scalar)
          autoreleaseResult = false;
        if (!getterExpr)
          getterExpr = &ivarRef;
        auto *ret = ReturnStmt::Create(
            getContext(), SourceLocation(), const_cast<Expr *>(getterExpr),
            /*NRVOCandidate=*/nullptr);
        if (mlir::failed(emitReturnStmt(*ret)))
          return;
        break;
      }
      case CIRObjCPropertyImplStrategyKind::Unsupported:
        cgm.errorNYI(pid->getSourceRange(),
                     "Objective-C synthesized property getter strategy");
        return;
      }
    }

    if (mlir::failed(fn.verifyBody()))
      return;
    finishFunction(bodyRange.getEnd());
  }

  emitEndEHSpec(getterMethod);
  eraseEmptyAndUnusedBlocks(fn);
  cgm.setCIRFunctionAttributesForDefinition(GlobalDecl(getterMethod), fn);
}

void CIRGenFunction::generateObjCPropertySetter(
    const ObjCImplementationDecl *imp, const ObjCPropertyImplDecl *pid) {
  ObjCMethodDecl *setterMethod = pid->getSetterMethodDecl();
  assert(setterMethod && "expected synthesized Objective-C setter");
  cir::FuncOp atomicHelperFn = generateObjCAtomicSetterCopyHelperFunction(pid);

  SmallString<256> nameBuf;
  mangleObjCAccessorName(*this, setterMethod, nameBuf);
  const CIRGenFunctionInfo &fi =
      cgm.getTypes().arrangeObjCMethodDeclaration(setterMethod);
  cir::FuncType funcType = cgm.getTypes().getFunctionType(fi);
  cir::FuncOp fn = cgm.getOrCreateCIRFunction(
      nameBuf.str(), funcType, GlobalDecl(setterMethod), /*forVTable=*/false,
      /*dontDefer=*/true, /*isThunk=*/false, ForDefinition);
  if (!setterMethod->isDirectMethod())
    fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);

  FunctionArgList args;
  args.push_back(setterMethod->getSelfDecl());
  if (!setterMethod->isDirectMethod())
    args.push_back(setterMethod->getCmdDecl());
  args.append(setterMethod->param_begin(), setterMethod->param_end());

  curGD = GlobalDecl(setterMethod);
  SourceLocation loc = setterMethod->getLocation();
  SourceRange bodyRange = setterMethod->getSourceRange();
  SourceLocRAIIObject fnLoc{*this, loc.isValid() ? getLoc(loc)
                                                 : builder.getUnknownLoc()};
  auto validMLIRLoc = [&](SourceLocation clangLoc) {
    return clangLoc.isValid() ? getLoc(clangLoc) : builder.getUnknownLoc();
  };
  mlir::Location fusedLoc = mlir::FusedLoc::get(
      &getMLIRContext(),
      {validMLIRLoc(bodyRange.getBegin()), validMLIRLoc(bodyRange.getEnd())});
  mlir::Block *entryBB = fn.addEntryBlock();
  SymTableScopeTy varScope(symbolTable);

  emitStartEHSpec(setterMethod);

  {
    LexicalScope lexScope(*this, fusedLoc, entryBB);
    lexScope.setClangSourceRange(bodyRange);
    startFunction(GlobalDecl(setterMethod), setterMethod->getReturnType(), fn,
                  funcType, args, loc, bodyRange.getBegin());
    if (setterMethod->isDirectMethod())
      emitObjCDirectMethodPrologue(setterMethod);

    ObjCIvarDecl *ivar = pid->getPropertyIvarDecl();
    QualType ivarTy = ivar->getType();
    ObjCIvarRefExpr ivarRef =
        buildSyntheticObjCIvarRefExpr(*this, setterMethod, ivar);

    if (ivarTy.isNonTrivialToPrimitiveCopy() == QualType::PCK_Struct) {
      ParmVarDecl *argDecl = *setterMethod->param_begin();
      if (!atomicHelperFn) {
        LValue dstLV = emitObjCIvarRefLValue(&ivarRef);
        emitSupportedNonTrivialCStructMoveAssignment(dstLV.getAddress(),
                                                     getAddrOfLocalVar(argDecl),
                                                     ivarTy);
      } else {
        emitCPPObjectAtomicSetterCall(*this, setterMethod, ivar, atomicHelperFn,
                                      validMLIRLoc(loc));
      }
      if (auto cleanup = calleeDestructedParamCleanups.lookup(argDecl);
          cleanup.isValid())
        deactivateCleanupBlock(cleanup, nullptr);
      if (mlir::failed(emitSyntheticVoidReturn(*this, setterMethod->getEndLoc())))
        return;
    } else if (!hasTrivialSetExpr(pid)) {
      if (const Stmt *setterExpr = pid->getSetterCXXAssignment()) {
        if (!atomicHelperFn) {
          if (mlir::failed(emitStmt(setterExpr, /*useCurrentScope=*/true)))
            return;
        } else {
          emitCPPObjectAtomicSetterCall(*this, setterMethod, ivar, atomicHelperFn,
                                        validMLIRLoc(loc));
        }
      }
      if (mlir::failed(emitSyntheticVoidReturn(*this, setterMethod->getEndLoc())))
        return;
    } else {
      CIRObjCPropertyImplStrategy strategy(cgm, pid);
      switch (strategy.getKind()) {
      case CIRObjCPropertyImplStrategyKind::Native: {
        LValue ivarLV = emitObjCIvarRefLValue(&ivarRef);
        ParmVarDecl *argDecl = *setterMethod->param_begin();
        mlir::Value argValue =
            builder.createLoad(validMLIRLoc(loc), getAddrOfLocalVar(argDecl));
        if (ivarTy.getPointerAuth())
          emitObjCAtomicPropertyPtrauthSetter(
              *this, ivarLV, ivar, argDecl->getType(), argValue,
              validMLIRLoc(loc));
        else
          emitAtomicStore(RValue::get(argValue), ivarLV,
                          cir::MemOrder::Unordered,
                          /*isVolatile=*/ivarLV.isVolatileQualified(),
                          /*isInit=*/false);
        if (mlir::failed(
                emitSyntheticVoidReturn(*this, setterMethod->getEndLoc())))
          return;
        break;
      }
      case CIRObjCPropertyImplStrategyKind::GetSetProperty:
      case CIRObjCPropertyImplStrategyKind::SetPropertyAndExpressionGet: {
        mlir::Value cmd =
            emitCmdValueForGetterSetterBody(*this, setterMethod, validMLIRLoc(loc));
        mlir::Value self =
            emitScalarExpr(buildSyntheticObjCSelfLoadExpr(*this, setterMethod));
        mlir::Value argValue =
            builder.createLoad(validMLIRLoc(loc),
                               getAddrOfLocalVar(*setterMethod->param_begin()));
        if (self.getType() != builder.getVoidPtrTy())
          self = builder.createBitcast(validMLIRLoc(loc), self,
                                       builder.getVoidPtrTy());
        if (cmd.getType() != builder.getVoidPtrTy())
          cmd = builder.createBitcast(validMLIRLoc(loc), cmd,
                                      builder.getVoidPtrTy());
        if (argValue.getType() != builder.getVoidPtrTy())
          argValue = builder.createBitcast(validMLIRLoc(loc), argValue,
                                           builder.getVoidPtrTy());
        mlir::Value ivarOffset = emitObjCIvarOffsetAsPointerDiff(
            *this, imp->getClassInterface(), ivar, validMLIRLoc(loc));

        if (useOptimizedObjCPropertySetter(cgm)) {
          builder.createCallOp(
              validMLIRLoc(loc),
              getObjCOptimizedPropertySetterFunction(*this, strategy.isAtomic(),
                                                     strategy.isCopy()),
              {self, cmd, argValue, ivarOffset});
        } else {
          builder.createCallOp(
              validMLIRLoc(loc), getObjCPropertySetterFunction(*this),
              {self, cmd, ivarOffset, argValue,
               builder.getBool(strategy.isAtomic(), validMLIRLoc(loc)),
               builder.getBool(strategy.isCopy(), validMLIRLoc(loc))});
        }

        if (mlir::failed(
                emitSyntheticVoidReturn(*this, setterMethod->getEndLoc())))
          return;
        break;
      }
      case CIRObjCPropertyImplStrategyKind::CopyStruct:
        emitStructSetterCall(*this, setterMethod, ivar, validMLIRLoc(loc));
        if (mlir::failed(
                emitSyntheticVoidReturn(*this, setterMethod->getEndLoc())))
          return;
        break;
      case CIRObjCPropertyImplStrategyKind::Expression: {
        if (const Stmt *setterExpr = pid->getSetterCXXAssignment()) {
          if (mlir::failed(emitStmt(setterExpr, /*useCurrentScope=*/true)))
            return;
        } else {
          ParmVarDecl *argDecl = *setterMethod->param_begin();
          QualType argType = argDecl->getType().getNonReferenceType();
          DeclRefExpr arg(getContext(), argDecl, false, argType, VK_LValue,
                          SourceLocation());
          auto *argExpr =
              new (getContext()) DeclRefExpr(getContext(), arg.getDecl(), false,
                                             arg.getType(), VK_LValue,
                                             SourceLocation());
          auto *argLoad = ImplicitCastExpr::Create(
              getContext(), argType.getUnqualifiedType(), CK_LValueToRValue,
              argExpr, nullptr, VK_PRValue, FPOptionsOverride());
          Expr *finalArg = argLoad;
          CastKind argCK = CK_NoOp;
          if (ivarRef.getType()->isObjCObjectPointerType()) {
            if (argLoad->getType()->isObjCObjectPointerType())
              argCK = CK_BitCast;
            else if (argLoad->getType()->isBlockPointerType())
              argCK = CK_BlockPointerToObjCPointerCast;
            else
              argCK = CK_CPointerToObjCPointerCast;
          } else if (ivarRef.getType()->isBlockPointerType()) {
            argCK = argLoad->getType()->isBlockPointerType()
                        ? CK_BitCast
                        : CK_AnyPointerToBlockPointerCast;
          } else if (ivarRef.getType()->isPointerType()) {
            argCK = CK_BitCast;
          } else if (argLoad->getType()->isAtomicType() &&
                     !ivarRef.getType()->isAtomicType()) {
            argCK = CK_AtomicToNonAtomic;
          } else if (!argLoad->getType()->isAtomicType() &&
                     ivarRef.getType()->isAtomicType()) {
            argCK = CK_NonAtomicToAtomic;
          }
          if (!getContext().hasSameUnqualifiedType(ivarRef.getType(),
                                                   argLoad->getType())) {
            finalArg = ImplicitCastExpr::Create(
                getContext(), ivarRef.getType(), argCK, argLoad, nullptr,
                VK_PRValue, FPOptionsOverride());
          }

          auto *assign = BinaryOperator::Create(
              getContext(), &ivarRef, finalArg, BO_Assign, ivarRef.getType(),
              VK_PRValue, OK_Ordinary, SourceLocation(), FPOptionsOverride());
          if (mlir::failed(emitStmt(assign, /*useCurrentScope=*/true)))
            return;
        }
        if (mlir::failed(
                emitSyntheticVoidReturn(*this, setterMethod->getEndLoc())))
          return;
        break;
      }
      case CIRObjCPropertyImplStrategyKind::Unsupported:
        cgm.errorNYI(pid->getSourceRange(),
                     "Objective-C synthesized property setter strategy");
        return;
      }
    }

    if (mlir::failed(fn.verifyBody()))
      return;
    finishFunction(bodyRange.getEnd());
  }

  emitEndEHSpec(setterMethod);
  eraseEmptyAndUnusedBlocks(fn);
  cgm.setCIRFunctionAttributesForDefinition(GlobalDecl(setterMethod), fn);
}

void CIRGenFunction::emitObjCDirectMethodPrologue(const ObjCMethodDecl *omd) {
  mlir::Location loc =
      omd->getLocation().isValid() ? getLoc(omd->getLocation())
                                   : builder.getUnknownLoc();

  Address selfAddr = getAddrOfLocalVar(omd->getSelfDecl());
  bool receiverCanBeNull = omd->isInstanceMethod();
  const ObjCInterfaceDecl *receiverID =
      omd->isClassMethod() ? omd->getClassInterface() : nullptr;
  if (omd->isClassMethod()) {
    if (!usesGNUstep2ObjCRuntime(*this)) {
      mlir::Value selfValue = builder.createLoad(loc, selfAddr);
      mlir::Value realizedClass = emitObjCClassRealization(loc, selfValue);
      builder.createStore(loc, realizedClass, selfAddr);
    }

    for (const ObjCInterfaceDecl *id = receiverID; id;
         id = id->getSuperClass()) {
      if (id->isWeakImported()) {
        receiverCanBeNull = true;
        break;
      }
    }
  }

  if (receiverCanBeNull) {
    mlir::Value selfValue = builder.createLoad(loc, selfAddr);
    mlir::Value zero = builder.getNullPtr(selfValue.getType(), loc);
    mlir::Value isNil =
        cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq, selfValue, zero);
    auto *curBlock = builder.getInsertionBlock();
    mlir::Region *parentRegion = curBlock->getParent();
    auto *nilBlock = new mlir::Block();
    parentRegion->push_back(nilBlock);
    auto *contBlock = new mlir::Block();
    parentRegion->push_back(contBlock);

    builder.setInsertionPointToEnd(curBlock);
    cir::BrCondOp::create(builder, loc, isNil, nilBlock, contBlock);

    builder.setInsertionPointToStart(nilBlock);
    if (!omd->getReturnType()->isVoidType())
      emitNullInitialization(loc, returnValue, omd->getReturnType());
    auto *retBlock = curLexScope->getOrCreateRetBlock(
        *this, applySourceAtomMetadata(loc, sourceAtomInfo.currentAtom, 1));
    emitBranchThroughCleanup(loc, returnBlock(retBlock));

    builder.setInsertionPointToStart(contBlock);
  }

  if (omd->isClassMethod() && usesGNUstep2ObjCRuntime(*this)) {
    auto unsignedLongTy =
        mlir::cast<cir::IntType>(convertType(getContext().UnsignedLongTy));
    auto classStartTy = builder.getAnonRecordTy(
        {cgm.voidPtrTy, cgm.voidPtrTy, cgm.voidPtrTy, unsignedLongTy,
         unsignedLongTy},
        /*packed=*/false, /*padded=*/false);

    mlir::Value selfValue = builder.createLoad(loc, selfAddr);
    mlir::Value classStartPtr =
        builder.createBitcast(loc, selfValue, builder.getPointerTo(classStartTy));
    mlir::Value flagsField = builder.createGetMember(
        loc, builder.getPointerTo(unsignedLongTy), classStartPtr, "", 4);
    auto flagsAlign =
        CharUnits::fromQuantity(getContext().getTypeAlign(getContext().UnsignedLongTy));
    mlir::Value flags = builder.createLoad(
        loc, Address(flagsField, unsignedLongTy, flagsAlign));
    mlir::Value initMask = builder.getConstantInt(
        loc, unsignedLongTy, static_cast<int64_t>(1U << 8));
    mlir::Value isInitialized = builder.createAnd(loc, flags, initMask);
    mlir::Value zero = builder.getConstantInt(loc, unsignedLongTy, 0);
    mlir::Value needsInitialize =
        cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq, isInitialized, zero);

    auto *curBlock = builder.getInsertionBlock();
    mlir::Region *parentRegion = curBlock->getParent();
    auto *notInitializedBlock = new mlir::Block();
    parentRegion->push_back(notInitializedBlock);
    auto *initializedBlock = new mlir::Block();
    parentRegion->push_back(initializedBlock);

    builder.setInsertionPointToEnd(curBlock);
    cir::BrCondOp::create(builder, loc, needsInitialize, notInitializedBlock,
                          initializedBlock);

    builder.setInsertionPointToStart(notInitializedBlock);
    cir::FuncOp sendInitializeFn = cgm.createRuntimeFunction(
        builder.getFuncType({cgm.voidPtrTy}, builder.getVoidTy()),
        "objc_send_initialize");
    builder.createCallOp(loc, sendInitializeFn, {selfValue});
    cir::BrOp::create(builder, loc, initializedBlock);

    builder.setInsertionPointToStart(initializedBlock);
  }

  if (omd->getCmdDecl()->isUsed()) {
    emitVarDecl(*omd->getCmdDecl());
    mlir::Value selector = cgm.getObjCSelector(*this, omd->getSelector(), loc);
    Address cmdAddr = getAddrOfLocalVar(omd->getCmdDecl());
    builder.createStore(loc, selector, cmdAddr);
  }
}

void CIRGenFunction::emitConstructorBody(FunctionArgList &args) {
  (void)0; // Sanitizer-specific CIR parity work remains NYI in this path.
  const auto *ctor = cast<CXXConstructorDecl>(curGD.getDecl());
  CXXCtorType ctorType = curGD.getCtorType();

  assert((cgm.getTarget().getCXXABI().hasConstructorVariants() ||
          ctorType == Ctor_Complete) &&
         "can only generate complete ctor for this ABI");

  cgm.setCXXSpecialMemberAttr(cast<cir::FuncOp>(curFn), ctor);

  // For trivial union copy/move constructors, emit a memcpy of the entire
  // object. The AST body is empty (no member initializers) because the
  // compiler doesn't know which union member is active. Without explicit
  // memcpy the data is never copied. This matches OGCG's
  // isMemcpyEquivalentSpecialMember which emits memcpy at the call site;
  // CIR instead emits it in the constructor body (same approach used for
  // trivial union copy/move-assignment in emitImplicitAssignmentOperatorBody).
  const CXXRecordDecl *classDecl = ctor->getParent();
  if (classDecl->isUnion() && ctor->isCopyOrMoveConstructor() &&
      (ctor->isTrivial() || ctor->isDefaulted())) {
    mlir::Location loc = getLoc(ctor->getSourceRange());
    Address thisAddr = loadCXXThisAddress();
    // The source is the second parameter (first is 'this').
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
    cir::MemCpyOp::create(builder, loc, dstPtr, srcPtr, sizeVal);
  }

  if (ctorType == Ctor_Complete && isConstructorDelegationValid(ctor) &&
      cgm.getTarget().getCXXABI().hasConstructorVariants()) {
    emitDelegateCXXConstructorCall(ctor, Ctor_Base, args, ctor->getEndLoc());
    return;
  }

  const FunctionDecl *definition = nullptr;
  Stmt *body = ctor->getBody(definition);
  assert(definition == ctor && "emitting wrong constructor body");

  // Enter the function-try-block before the constructor prologue if
  // applicable.
  bool isTryBody = isa_and_nonnull<CXXTryStmt>(body);

  incrementProfileCounter(body);
  assert(!cir::MissingFeatures::mcdcCoverageFolding());

  // TODO: in restricted cases, we can emit the vbase initializers of a
  // complete ctor and then delegate to the base ctor.

  if (isTryBody) {
    const auto *tryStmt = cast<CXXTryStmt>(body);
    unsigned numHandlers = tryStmt->getNumHandlers();
    mlir::Location tryLoc = getLoc(tryStmt->getBeginLoc());
    mlir::OpBuilder::InsertPoint beginInsertTryBody;

    bool hasCatchAll = false;
    for (unsigned i = 0; i != numHandlers; ++i) {
      hasCatchAll |= tryStmt->getHandler(i)->getExceptionDecl() == nullptr;
      if (hasCatchAll)
        break;
    }

    auto tryOp = cir::TryOp::create(
        builder, tryLoc,
        /*tryBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          beginInsertTryBody = builder.saveInsertionPoint();
        },
        /*handlersBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc,
            mlir::OperationState &result) {
          mlir::OpBuilder::InsertionGuard guard(b);
          unsigned numRegionsToCreate =
              hasCatchAll ? numHandlers : numHandlers + 1;
          for (unsigned i = 0; i != numRegionsToCreate; ++i) {
            mlir::Region *region = result.addRegion();
            builder.createBlock(region);
          }
        });

    {
      mlir::Location loc = tryOp.getLoc();
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.restoreInsertionPoint(beginInsertTryBody);
      LexicalScope tryScope{*this, loc, builder.getInsertionBlock()};
      tryScope.setAsTry(tryOp);

      enterCXXTryStmt(*tryStmt, tryOp, /*isFnTryBlock=*/true);

      // Wrap prologue + body in a RunCleanupsScope so member destructor
      // cleanups pushed by emitCtorPrologue are popped before
      // exitCXXTryStmt, which expects EHCatchScope on top of the EH stack.
      {
        RunCleanupsScope runCleanups(*this);

        // Emit the constructor prologue inside the try region, so that
        // exceptions from member/base initialization are caught.
        emitCtorPrologue(ctor, ctorType, args);

        // Emit the try body.
        {
          mlir::OpBuilder::InsertionGuard bodyGuard(builder);
          LexicalScope tryBodyScope{*this, loc, builder.getInsertionBlock()};
          unsigned errorsBefore = cgm.getDiags().getNumErrors();
          if (mlir::failed(
                  emitStmt(tryStmt->getTryBlock(), /*useCurrentScope=*/true))) {
            if (cgm.getDiags().getNumErrors() == errorsBefore)
              cgm.errorNYI(tryStmt->getSourceRange(),
                           "emitConstructorBody try body failure");
            return;
          }
        }

        runCleanups.forceCleanup();
      }

      // Emit catch handlers with implicit rethrow for constructors.
      exitCXXTryStmt(*tryStmt, /*isFnTryBlock=*/true);
    }
    return;
  }

  // Emit the constructor prologue, i.e. the base and member initializers.
  {
    RunCleanupsScope runCleanups(*this);

    emitCtorPrologue(ctor, ctorType, args);

    // Emit the body of the statement.
    // TODO(cir): propagate this result via mlir::logical result. Just
    // unreachable now just to have it handled.
    unsigned errorsBefore = cgm.getDiags().getNumErrors();
    if (mlir::failed(emitStmt(body, true))) {
      if (cgm.getDiags().getNumErrors() == errorsBefore)
        cgm.errorNYI(body->getSourceRange(),
                     "emitConstructorBody body statement failure");
      return;
    }

    // Emit any cleanup blocks associated with the member or base
    // initializers, which includes (along the exceptional path) the
    // destructors for those members and bases that were fully constructed.
    runCleanups.forceCleanup();
  }
}

/// Check whether we need to initialize any vtable pointers before calling
/// this destructor. Port of OGCG's CanSkipVTablePointerInitialization
/// (CGClass.cpp:1397-1419).
static bool canSkipVTablePointerInitialization(CIRGenFunction &cgf,
                                               const CXXDestructorDecl *dtor) {
  const CXXRecordDecl *classDecl = dtor->getParent();
  if (!classDecl->isDynamicClass())
    return true;

  // For a final class, the vtable pointer is known to already point to the
  // class's vtable.
  if (classDecl->isEffectivelyFinal())
    return true;

  if (!dtor->hasTrivialBody())
    return false;

  // Check the fields.
  for (const auto *field : classDecl->fields())
    if (!fieldHasTrivialDestructorBody(cgf.getContext(), field))
      return false;

  return true;
}

/// Emits the body of the current destructor.
void CIRGenFunction::emitDestructorBody(FunctionArgList &args) {
  const CXXDestructorDecl *dtor = cast<CXXDestructorDecl>(curGD.getDecl());
  CXXDtorType dtorType = curGD.getDtorType();

  cgm.setCXXSpecialMemberAttr(cast<cir::FuncOp>(curFn), dtor);

  // For an abstract class, non-base destructors are never used (and can't
  // be emitted in general, because vbase dtors may not have been validated
  // by Sema), but the Itanium ABI doesn't make them optional and Clang may
  // in fact emit references to them from other compilations, so emit them
  // as functions containing a trap instruction.
  if (dtorType != Dtor_Base && dtor->getParent()->isAbstract()) {
    emitTrap(getLoc(dtor->getSourceRange()), /*createNewBlock=*/true);
    return;
  }

  Stmt *body = dtor->getBody();
  if (body) {
    incrementProfileCounter(body);
    assert(!cir::MissingFeatures::mcdcCoverageFolding());
  }

  // The call to operator delete in a deleting destructor happens
  // outside of the function-try-block, which means it's always
  // possible to delegate the destructor body to the complete
  // destructor.  Do so.
  if (dtorType == Dtor_Deleting || dtorType == Dtor_VectorDeleting) {
    if (cxxStructorImplicitParamValue && dtorType == Dtor_VectorDeleting) {
      cgm.errorNYI(dtor->getSourceRange(), "emitConditionalArrayDtorCall");
      return;
    }
    RunCleanupsScope dtorEpilogue(*this);
    enterDtorCleanups(dtor, Dtor_Deleting);
    if (haveInsertPoint()) {
      QualType thisTy = dtor->getFunctionObjectParameterType();
      emitCXXDestructorCall(dtor, Dtor_Complete, /*forVirtualBase=*/false,
                            /*delegating=*/false, loadCXXThisAddress(), thisTy);
    }
    return;
  }

  // If the body is a function-try-block, enter the try before
  // anything else.
  const bool isTryBody = isa_and_nonnull<CXXTryStmt>(body);
  const CXXTryStmt *tryStmt =
      isTryBody ? cast<CXXTryStmt>(body) : nullptr;

  // For a function-try-block destructor, create a TryOp that wraps the
  // entire destructor body including epilogue cleanups. This ensures
  // exceptions during member/base destruction are caught by the
  // function-try-block's handlers.
  cir::TryOp tryOp;
  mlir::OpBuilder::InsertPoint beginInsertTryBody;

  if (isTryBody) {
    unsigned numHandlers = tryStmt->getNumHandlers();
    mlir::Location tryLoc = getLoc(tryStmt->getBeginLoc());

    bool hasCatchAll = false;
    for (unsigned i = 0; i != numHandlers; ++i) {
      hasCatchAll |= tryStmt->getHandler(i)->getExceptionDecl() == nullptr;
      if (hasCatchAll)
        break;
    }

    tryOp = cir::TryOp::create(
        builder, tryLoc,
        /*tryBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          beginInsertTryBody = builder.saveInsertionPoint();
        },
        /*handlersBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc,
            mlir::OperationState &result) {
          mlir::OpBuilder::InsertionGuard guard(b);
          unsigned numRegionsToCreate =
              hasCatchAll ? numHandlers : numHandlers + 1;
          for (unsigned i = 0; i != numRegionsToCreate; ++i) {
            mlir::Region *region = result.addRegion();
            builder.createBlock(region);
          }
        });
  }

  {
    // If isTryBody, redirect emission into the TryOp's try region.
    std::optional<mlir::OpBuilder::InsertionGuard> tryGuard;
    std::optional<LexicalScope> tryScope;

    if (isTryBody) {
      tryGuard.emplace(builder);
      builder.restoreInsertionPoint(beginInsertTryBody);
      mlir::Location loc = tryOp.getLoc();
      tryScope.emplace(*this, loc, builder.getInsertionBlock());
      tryScope->setAsTry(tryOp);

      enterCXXTryStmt(*tryStmt, tryOp, /*isFnTryBlock=*/true);
    }

    (void)0; // Sanitizer-specific CIR parity work remains NYI in this path.

    // Enter the epilogue cleanups.
    RunCleanupsScope dtorEpilogue(*this);

    // If this is the complete variant, just invoke the base variant;
    // the epilogue will destruct the virtual bases.  But we can't do
    // this optimization if the body is a function-try-block, because
    // we'd introduce *two* handler blocks.  In the Microsoft ABI, we
    // always delegate because we might not have a definition in this TU.
    switch (dtorType) {
    case Dtor_Unified:
      llvm_unreachable("not expecting a unified dtor");
    case Dtor_Comdat:
      llvm_unreachable("not expecting a COMDAT");
    case Dtor_Deleting:
    case Dtor_VectorDeleting:
      llvm_unreachable("already handled deleting case");

    case Dtor_Complete:
      assert((body || getTarget().getCXXABI().isMicrosoft()) &&
             "can't emit a dtor without a body for non-Microsoft ABIs");

      // Enter the cleanup scopes for virtual bases.
      enterDtorCleanups(dtor, Dtor_Complete);

      if (!isTryBody) {
        QualType thisTy = dtor->getFunctionObjectParameterType();
        emitCXXDestructorCall(dtor, Dtor_Base, /*forVirtualBase=*/false,
                              /*delegating=*/false, loadCXXThisAddress(),
                              thisTy);
        break;
      }

      // Fallthrough: act like we're in the base variant.
      [[fallthrough]];

    case Dtor_Base:
      assert(body);

      // Enter the cleanup scopes for fields and non-virtual bases.
      enterDtorCleanups(dtor, Dtor_Base);

      // Initialize the vtable pointers before entering the body.
      // Port of OGCG's CanSkipVTablePointerInitialization check
      // (CGClass.cpp:1397-1419). Skips vtable init when the class is
      // non-dynamic, effectively final, or when the destructor body and
      // all field/base destructors are trivially trivial.
      {
        if (!canSkipVTablePointerInitialization(*this, dtor)) {
          // Insert the llvm.launder.invariant.group intrinsic before
          // initializing the vptrs to cancel any previous assumptions we might
          // have made.
          if (cgm.getCodeGenOpts().StrictVTablePointers &&
              cgm.getCodeGenOpts().OptimizationLevel > 0)
            cxxThisValue = cir::InvariantGroupOp::create(
                builder, getLoc(dtor->getBeginLoc()), loadCXXThis());
          initializeVTablePointers(getLoc(dtor->getBeginLoc()),
                                   dtor->getParent());
        }
      }

      // Emit the destructor body. For function-try-blocks, emit only the
      // inner try block — the catch handlers are emitted by exitCXXTryStmt.
      if (isTryBody) {
        (void)emitStmt(tryStmt->getTryBlock(), /*useCurrentScope=*/true);
      } else if (body) {
        (void)emitStmt(body, /*useCurrentScope=*/true);
      } else {
        assert(dtor->isImplicit() && "bodyless dtor not implicit");
        // nothing to do besides what's in the epilogue
      }
      // OGCG: -fapple-kext inlines destructor calls into the caller's body.
      // Apple kernel extensions are not relevant for CIR targets.

      break;
    }

    // Jump out through the epilogue cleanups.
    dtorEpilogue.forceCleanup();

    // Exit the try if applicable — emit catch handlers with implicit
    // rethrow per [except.handle]p11.
    if (isTryBody)
      exitCXXTryStmt(*tryStmt, /*isFnTryBlock=*/true);
  }
}

/// Given a value of type T* that may not be to a complete object, construct
/// an l-vlaue withi the natural pointee alignment of T.
LValue CIRGenFunction::makeNaturalAlignPointeeAddrLValue(mlir::Value val,
                                                         QualType ty) {
  // FIXME(cir): is it safe to assume Op->getResult(0) is valid? Perhaps
  // assert on the result type first.
  LValueBaseInfo baseInfo;
  TBAAAccessInfo tbaaInfo;
  CharUnits align =
      cgm.getNaturalTypeAlignment(ty, &baseInfo, &tbaaInfo,
                                  /*forPointeeType=*/true);
  return makeAddrLValue(Address(val, align), ty, baseInfo, tbaaInfo);
}

LValue CIRGenFunction::makeNaturalAlignAddrLValue(mlir::Value val,
                                                  QualType ty) {
  LValueBaseInfo baseInfo;
  TBAAAccessInfo tbaaInfo;
  CharUnits alignment = cgm.getNaturalTypeAlignment(ty, &baseInfo, &tbaaInfo);
  Address addr(val, convertTypeForMem(ty), alignment);
  return makeAddrLValue(addr, ty, baseInfo, tbaaInfo);
}

clang::QualType CIRGenFunction::buildFunctionArgList(clang::GlobalDecl gd,
                                                     FunctionArgList &args) {
  const auto *fd = cast<FunctionDecl>(gd.getDecl());
  QualType retTy = fd->getReturnType();

  const auto *md = dyn_cast<CXXMethodDecl>(fd);
  if (md && md->isImplicitObjectMemberFunction()) {
    if (cgm.getCXXABI().hasThisReturn(gd)) {
      retTy = md->getThisType();
    } else if (cgm.getCXXABI().hasMostDerivedReturn(gd)) {
      retTy = cgm.getASTContext().VoidPtrTy;
    }
    cgm.getCXXABI().buildThisParam(*this, args);
  }

  // The base version of an inheriting constructor whose constructed base is a
  // virtual base is not passed any arguments (because it doesn't actually
  // call the inherited constructor).
  bool passedParams = true;
  if (const auto *cd = dyn_cast<CXXConstructorDecl>(fd))
    if (auto inherited = cd->getInheritedConstructor())
      passedParams =
          getTypes().inheritingCtorHasParams(inherited, gd.getCtorType());

  if (passedParams) {
    for (auto *param : fd->parameters()) {
      args.push_back(param);
      if (!param->hasAttr<PassObjectSizeAttr>())
        continue;

      // Create an implicit parameter for the pass_object_size size argument.
      // Matches OGCG CodeGenFunction::BuildFunctionArgList.
      auto *implicit = ImplicitParamDecl::Create(
          getContext(), param->getDeclContext(), param->getLocation(),
          /*Id=*/nullptr, getContext().getSizeType(), ImplicitParamKind::Other);
      sizeArguments[param] = implicit;
      args.push_back(implicit);
    }
  }

  if (md && (isa<CXXConstructorDecl>(md) || isa<CXXDestructorDecl>(md)))
    cgm.getCXXABI().addImplicitStructorParams(*this, retTy, args);

  return retTy;
}

/// Emit code to compute a designator that specifies the location
/// of the expression.
/// FIXME: document this function better.
LValue CIRGenFunction::emitLValue(const Expr *e) {
  // OGCG applies debug location for each l-value emission via
  // ApplyDebugLocation DL(*this, E) (CodeGenFunction.cpp in EmitLValue).
  // CIR ops carry their own mlir::Location, so this is handled inherently.
  switch (e->getStmtClass()) {
  default:
    cgm.errorNYI(e->getSourceRange(),
                 "l-value not implemented for this expression kind");
    return LValue();
  case Expr::ConditionalOperatorClass:
    return emitConditionalOperatorLValue(cast<ConditionalOperator>(e));
  case Expr::BinaryConditionalOperatorClass:
    return emitConditionalOperatorLValue(cast<BinaryConditionalOperator>(e));
  case Expr::ArraySubscriptExprClass:
    return emitArraySubscriptExpr(cast<ArraySubscriptExpr>(e));
  case Expr::MatrixSubscriptExprClass:
    return emitMatrixSubscriptExpr(cast<MatrixSubscriptExpr>(e));
  case Expr::ExtVectorElementExprClass:
    return emitExtVectorElementExpr(cast<ExtVectorElementExpr>(e));
  case Expr::UnaryOperatorClass:
    return emitUnaryOpLValue(cast<UnaryOperator>(e));
  case Expr::StringLiteralClass:
    return emitStringLiteralLValue(cast<StringLiteral>(e));
  case Expr::ObjCEncodeExprClass:
    return emitObjCEncodeExprLValue(cast<ObjCEncodeExpr>(e));
  case Expr::PseudoObjectExprClass:
    return emitPseudoObjectLValue(cast<PseudoObjectExpr>(e));
  case Expr::ObjCMessageExprClass:
    return emitObjCMessageExprLValue(cast<ObjCMessageExpr>(e));
  case Expr::MemberExprClass:
    return emitMemberExpr(cast<MemberExpr>(e));
  case Expr::ObjCIsaExprClass:
    return emitObjCIsaExpr(cast<ObjCIsaExpr>(e));
  case Expr::ObjCIvarRefExprClass:
    return emitObjCIvarRefLValue(cast<ObjCIvarRefExpr>(e));
  case Expr::CompoundLiteralExprClass:
    return emitCompoundLiteralLValue(cast<CompoundLiteralExpr>(e));
  case Expr::PredefinedExprClass:
    return emitPredefinedLValue(cast<PredefinedExpr>(e));
  case Expr::BinaryOperatorClass:
    return emitBinaryOperatorLValue(cast<BinaryOperator>(e));
  case Expr::CompoundAssignOperatorClass: {
    QualType ty = e->getType();
    if (const AtomicType *at = ty->getAs<AtomicType>())
      ty = at->getValueType();
    if (!ty->isAnyComplexType())
      return emitCompoundAssignmentLValue(cast<CompoundAssignOperator>(e));

    return emitComplexCompoundAssignmentLValue(cast<CompoundAssignOperator>(e));
  }
  case Expr::CallExprClass:
  case Expr::CXXMemberCallExprClass:
  case Expr::CXXOperatorCallExprClass:
  case Expr::UserDefinedLiteralClass:
    return emitCallExprLValue(cast<CallExpr>(e));
  case Expr::ExprWithCleanupsClass: {
    const auto *cleanups = cast<ExprWithCleanups>(e);
    RunCleanupsScope scope(*this);
    LValue lv = emitLValue(cleanups->getSubExpr());
    // cleanupWithPreservedValues -- CIR uses MLIR SSA values which persist across cleanup scopes.
    return lv;
  }
  case Expr::CXXDefaultArgExprClass: {
    auto *dae = cast<CXXDefaultArgExpr>(e);
    CXXDefaultArgExprScope scope(*this, dae);
    return emitLValue(dae->getExpr());
  }
  case Expr::ParenExprClass:
    return emitLValue(cast<ParenExpr>(e)->getSubExpr());
  case Expr::GenericSelectionExprClass:
    return emitLValue(cast<GenericSelectionExpr>(e)->getResultExpr());
  case Expr::InitListExprClass:
    return emitInitListLValue(cast<InitListExpr>(e));
  case Expr::CXXTemporaryObjectExprClass:
  case Expr::CXXConstructExprClass:
    return emitCXXConstructLValue(cast<CXXConstructExpr>(e));
  case Expr::DeclRefExprClass:
    return emitDeclRefLValue(cast<DeclRefExpr>(e));
  case Expr::CStyleCastExprClass:
  case Expr::CXXFunctionalCastExprClass:
  case Expr::CXXStaticCastExprClass:
  case Expr::CXXDynamicCastExprClass:
  case Expr::CXXReinterpretCastExprClass:
  case Expr::CXXConstCastExprClass:
  case Expr::CXXAddrspaceCastExprClass:
  case Expr::ImplicitCastExprClass:
    return emitCastLValue(cast<CastExpr>(e));
  case Expr::MaterializeTemporaryExprClass:
    return emitMaterializeTemporaryExpr(cast<MaterializeTemporaryExpr>(e));
  case Expr::OpaqueValueExprClass:
    return emitOpaqueValueLValue(cast<OpaqueValueExpr>(e));
  case Expr::ChooseExprClass:
    return emitLValue(cast<ChooseExpr>(e)->getChosenSubExpr());
  case Expr::CXXTypeidExprClass:
    return emitCXXTypeidLValue(cast<CXXTypeidExpr>(e));
  case Expr::CXXRewrittenBinaryOperatorClass:
    return emitLValue(
        cast<CXXRewrittenBinaryOperator>(e)->getSemanticForm());
  case Expr::SubstNonTypeTemplateParmExprClass:
    return emitLValue(
        cast<SubstNonTypeTemplateParmExpr>(e)->getReplacement());
  case Expr::CXXDefaultInitExprClass: {
    auto *die = cast<CXXDefaultInitExpr>(e);
    CXXDefaultInitExprScope scope(*this, die);
    return emitLValue(die->getExpr());
  }
  case Expr::CXXThisExprClass:
    return makeAddrLValue(loadCXXThisAddress(), e->getType());
  case Expr::CXXBindTemporaryExprClass:
    return emitCXXBindTemporaryLValue(cast<CXXBindTemporaryExpr>(e));
  case Expr::CoawaitExprClass:
    return emitCoawaitLValue(*cast<CoawaitExpr>(e));
  case Expr::CoyieldExprClass:
    return emitCoyieldLValue(*cast<CoyieldExpr>(e));
  case Expr::ConstantExprClass: {
    const ConstantExpr *ce = cast<ConstantExpr>(e);
    if (mlir::Attribute result =
            ConstantEmitter(*this).tryEmitConstantExpr(ce)) {
      QualType retType = ce->getType();
      // For reference-returning consteval functions, the ConstantExpr is a
      // GLValue and the result is an address (e.g. a GlobalViewAttr).
      // Materialize the constant and wrap it in an LValue.
      if (ce->isGLValue()) {
        const Expr *se = ce->getSubExpr()->IgnoreImplicit();
        if (isa<CallExpr>(se))
          retType = cast<CallExpr>(se)
                        ->getCallReturnType(getContext())
                        ->getPointeeType();
      }
      mlir::Value val = builder.getConstant(
          getLoc(ce->getExprLoc()), mlir::cast<mlir::TypedAttr>(result));
      CharUnits alignment =
          cgm.getNaturalTypeAlignment(retType, /*baseInfo=*/nullptr);
      return makeAddrLValue(
          Address(val, convertType(retType), alignment), retType,
          AlignmentSource::Decl);
    }
    return emitLValue(ce->getSubExpr());
  }
  case Expr::StmtExprClass: {
    RValue rv = emitAnyExprToTemp(e);
    return makeAddrLValue(rv.getAggregateAddress(), e->getType(),
                          AlignmentSource::Decl);
  }
  case Expr::LambdaExprClass:
    return emitAggExprToLValue(e);
  case Expr::VAArgExprClass:
    return emitAggExprToLValue(e);
  case Expr::PackIndexingExprClass:
    return emitLValue(cast<PackIndexingExpr>(e)->getSelectedExpr());
  }
}

mlir::Value CIRGenFunction::emitMatrixIndexExpr(const Expr *e) {
  mlir::Value idx = emitScalarExpr(e);
  if (idx.getType() == ptrDiffTy)
    return idx;
  return builder.createIntCast(idx, ptrDiffTy);
}

static std::string getVersionedTmpName(llvm::StringRef name, unsigned cnt) {
  SmallString<256> buffer;
  llvm::raw_svector_ostream out(buffer);
  out << name << cnt;
  return std::string(out.str());
}

std::string CIRGenFunction::getCounterRefTmpAsString() {
  return getVersionedTmpName("ref.tmp", counterRefTmp++);
}

std::string CIRGenFunction::getCounterAggTmpAsString() {
  return getVersionedTmpName("agg.tmp", counterAggTmp++);
}

void CIRGenFunction::emitNullInitialization(mlir::Location loc, Address destPtr,
                                            QualType ty) {
  // Ignore empty classes in C++.
  if (getLangOpts().CPlusPlus)
    if (const auto *rd = ty->getAsCXXRecordDecl(); rd && rd->isEmpty())
      return;

  // If the destination pointer has a different element type (e.g., i8 from
  // a previous cast), cast it to the correct type for the store.
  mlir::Type cirTy = convertType(ty);
  if (destPtr.getElementType() != cirTy)
    destPtr = destPtr.withElementType(builder, cirTy);

  // Get size and alignment info for this aggregate.
  const CharUnits size = getContext().getTypeSizeInChars(ty);
  if (size.isZero()) {
    // But note that getTypeInfo returns 0 for a VLA.
    if (const auto *vlaType =
            dyn_cast_or_null<VariableArrayType>(
                getContext().getAsArrayType(ty))) {
      // VLA null initialization: compute dynamic size and memset to zero.
      // Matches OGCG's EmitNullInitialization VLA path.
      VlaSizePair vlaSize = getVLASize(vlaType);
      mlir::Value sizeVal = vlaSize.numElts;
      CharUnits eltSize = getContext().getTypeSizeInChars(vlaSize.type);
      if (!eltSize.isOne()) {
        mlir::Value eltSizeVal = builder.getConstInt(
            loc, sizeVal.getType(), eltSize.getQuantity());
        sizeVal = builder.createNUWAMul(loc, sizeVal, eltSizeVal);
      }
      auto destAddrPtrTy =
          mlir::cast<cir::PointerType>(destPtr.getPointer().getType());
      auto voidPtrTy = cir::PointerType::get(
          builder.getContext(), cir::VoidType::get(builder.getContext()),
          destAddrPtrTy.getAddrSpace());
      mlir::Value voidDest =
          builder.createBitcast(destPtr.getPointer(), voidPtrTy);
      mlir::Value zeroVal = builder.getConstInt(
          loc, cir::IntType::get(builder.getContext(), 32, true), 0);
      auto memsetOp =
          cir::MemSetOp::create(builder, loc, voidDest, zeroVal, sizeVal);
      addOpToCurrentSourceAtom(memsetOp);
      return;
    } else {
      return;
    }
  }

  // If the type contains a pointer to data member we can't memset it to zero.
  // Instead, create a null constant and copy it to the destination.
  // TODO: there are other patterns besides zero that we can usefully memset,
  // like -1, which happens to be the pattern used by member-pointers.
  // OGCG creates a private global with the null constant and memcpy's from it.
  // In CIR, we can just emit the null constant value directly and store it.
  if (!cgm.getTypes().isZeroInitializable(ty)) {
    mlir::Value nullVal = cgm.emitNullConstant(ty, loc);
    auto storeOp = builder.createStore(loc, nullVal, destPtr);
    addOpToCurrentSourceAtom(storeOp, nullVal);
    return;
  }

  // In LLVM Codegen: otherwise, just memset the whole thing to zero using
  // Builder.CreateMemSet. In CIR just emit a store of #cir.zero to the
  // respective address.
  // Builder.CreateMemSet(DestPtr, Builder.getInt8(0), SizeVal, false);
  const mlir::Value zeroValue = builder.getNullValue(convertType(ty), loc);
  auto storeOp = builder.createStore(loc, zeroValue, destPtr);
  addOpToCurrentSourceAtom(storeOp, zeroValue);
}

// TODO(cir): should be shared with LLVM codegen.
bool CIRGenFunction::shouldNullCheckClassCastValue(const CastExpr *ce) {
  const Expr *e = ce->getSubExpr();

  if (ce->getCastKind() == CK_UncheckedDerivedToBase)
    return false;

  if (isa<CXXThisExpr>(e->IgnoreParens())) {
    // We always assume that 'this' is never null.
    return false;
  }

  if (const ImplicitCastExpr *ice = dyn_cast<ImplicitCastExpr>(ce)) {
    // And that glvalue casts are never null.
    if (ice->isGLValue())
      return false;
  }

  return true;
}

/// Computes the length of an array in elements, as well as the base
/// element type and a properly-typed first element pointer.
mlir::Value
CIRGenFunction::emitArrayLength(const clang::ArrayType *origArrayType,
                                QualType &baseType, Address &addr) {
  const clang::ArrayType *arrayType = origArrayType;

  // If it's a VLA, we have to load the stored size.  Note that
  // this is the size of the VLA in bytes, not its size in elements.
  if (isa<VariableArrayType>(arrayType)) {
    mlir::Value numVLAElements =
        getVLASize(cast<VariableArrayType>(arrayType)).numElts;

    // Walk into all VLAs.  This doesn't require changes to addr,
    // which has type T* where T is the first non-VLA element type.
    do {
      QualType elementType = arrayType->getElementType();
      arrayType = getContext().getAsArrayType(elementType);

      // If we only have VLA components, 'addr' requires no adjustment.
      if (!arrayType) {
        baseType = elementType;
        return numVLAElements;
      }
    } while (isa<VariableArrayType>(arrayType));

    // We get out here only if we find a constant array type inside
    // the VLA. Multiply by the constant array sizes.
    uint64_t countFromCLAs = 1;
    while (auto constArray = dyn_cast<ConstantArrayType>(arrayType)) {
      countFromCLAs *= constArray->getZExtSize();
      QualType eltType = constArray->getElementType();
      arrayType = getContext().getAsArrayType(eltType);
      if (!arrayType) {
        baseType = eltType;
        break;
      }
    }
    mlir::Value constCount =
        builder.getConstInt(*currSrcLoc, sizeTy, countFromCLAs);
    numVLAElements =
        builder.createNUWAMul(*currSrcLoc, numVLAElements, constCount);

    // Update addr to point to the base element type, matching OGCG which
    // GEPs through nested constant arrays. Without this, addr retains the
    // nested array type (e.g., ptr<array<S x 3>>) causing createPtrStride
    // in emitArrayDestroy to compute wrong end pointer.
    addr = addr.withElementType(builder, convertTypeForMem(baseType));

    return numVLAElements;
  }

  uint64_t countFromCLAs = 1;
  QualType eltType;

  auto cirArrayType = mlir::dyn_cast<cir::ArrayType>(addr.getElementType());

  // OpenMP array-section reductions materialize backing storage as a pointer to
  // the first element even when the semantic reduction type is a constant
  // array. In that case the Clang type still carries the full array shape, but
  // the CIR address has already decayed to the base element type.
  if (!cirArrayType) {
    while (auto constArray = dyn_cast<ConstantArrayType>(arrayType)) {
      countFromCLAs *= constArray->getZExtSize();
      eltType = constArray->getElementType();
      arrayType = getContext().getAsArrayType(eltType);
      if (!arrayType)
        break;
    }

    if (arrayType) {
      cgm.errorNYI("emitArrayLength for non-array underlying types");
      return {};
    }

    mlir::Type baseElementTy = convertTypeForMem(eltType);
    if (addr.getElementType() != baseElementTy) {
      cgm.errorNYI("emitArrayLength for non-array underlying types");
      return {};
    }

    baseType = eltType;
    return builder.getConstInt(*currSrcLoc, sizeTy, countFromCLAs);
  }

  while (cirArrayType) {
    assert(isa<ConstantArrayType>(arrayType));
    countFromCLAs *= cirArrayType.getSize();
    eltType = arrayType->getElementType();

    cirArrayType =
        mlir::dyn_cast<cir::ArrayType>(cirArrayType.getElementType());

    arrayType = getContext().getAsArrayType(arrayType->getElementType());
    assert((!cirArrayType || arrayType) &&
           "CIR and Clang types are out-of-sync");
  }

  if (arrayType) {
    // From this point onwards, the Clang array type has been emitted
    // as some other type (probably a packed struct). Compute the array
    // size, and just emit the 'begin' expression as a bitcast.
    cgm.errorNYI("emitArrayLength for non-array underlying types");
    return {};
  }

  baseType = eltType;
  return builder.getConstInt(*currSrcLoc, sizeTy, countFromCLAs);
}

void CIRGenFunction::instantiateIndirectGotoBlock() {
  // If we already made the indirect branch for indirect goto, return its block.
  if (indirectGotoBlock)
    return;

  mlir::OpBuilder::InsertionGuard guard(builder);
  // Create the block in the function body region (not the current region),
  // so that it's accessible after FlattenCFG flattens all nested regions.
  indirectGotoBlock =
      builder.createBlock(&curFn->getRegion(0), {}, {voidPtrTy},
                          {builder.getUnknownLoc()});
}

mlir::Value CIRGenFunction::emitAlignmentAssumption(
    mlir::Value ptrValue, QualType ty, SourceLocation loc,
    SourceLocation assumptionLoc, int64_t alignment, mlir::Value offsetValue) {
  // Match OGCG CodeGenFunction::emitAlignmentAssumption: when
  // -fsanitize=alignment is active, emit an alignment check before the
  // llvm.assume. If the pointer is not aligned, call the
  // __ubsan_handle_alignment_assumption handler.
  if (sanOpts.has(SanitizerKind::Alignment) &&
      !ty->getPointeeType().isVolatileQualified()) {
    mlir::Location cirLoc = getLoc(assumptionLoc);
    auto intPtrTy = mlir::cast<cir::IntType>(
        convertType(getContext().getUIntPtrType()));

    // ptrtoint
    mlir::Value ptrAsInt = builder.createPtrToInt(ptrValue, intPtrTy);
    markNoSanitize(ptrAsInt.getDefiningOp());

    // Subtract offset if provided and non-zero.
    mlir::Value adjustedPtr = ptrAsInt;
    if (offsetValue) {
      // Cast offset to intptr width if needed (match OGCG).
      if (offsetValue.getType() != intPtrTy)
        offsetValue = builder.createIntCast(offsetValue, intPtrTy);
      bool isOffsetZero = false;
      if (auto constOp = offsetValue.getDefiningOp<cir::ConstantOp>())
        if (auto intAttr = constOp.getValueAttr<cir::IntAttr>())
          isOffsetZero = intAttr.getValue().isZero();
      if (!isOffsetZero) {
        adjustedPtr =
            builder.createSub(cirLoc, adjustedPtr, offsetValue);
        markNoSanitize(adjustedPtr.getDefiningOp());
      }
    }

    // mask = alignment - 1; maskedptr = adjustedPtr & mask; check = (maskedptr == 0)
    mlir::Value alignVal =
        builder.getConstInt(cirLoc, intPtrTy, alignment);
    mlir::Value one = builder.getConstInt(cirLoc, intPtrTy, 1);
    mlir::Value mask = builder.createSub(cirLoc, alignVal, one);
    markNoSanitize(mask.getDefiningOp());
    mlir::Value maskedPtr = builder.createAnd(cirLoc, adjustedPtr, mask);
    markNoSanitize(maskedPtr.getDefiningOp());
    mlir::Value zero = builder.getConstInt(cirLoc, intPtrTy, 0);
    mlir::Value isAligned =
        builder.createCompare(cirLoc, cir::CmpOpKind::eq, maskedPtr, zero);
    markNoSanitize(isAligned.getDefiningOp());

    isAligned = emitGuardedUBSanCheck(cirLoc, isAligned,
                                      SanitizerKind::SO_Alignment);

    // Determine handler mode.
    bool isTrap =
        cgm.getCodeGenOpts().SanitizeTrap.has(SanitizerKind::SO_Alignment);
    bool isRecover =
        !isTrap &&
        cgm.getCodeGenOpts().SanitizeRecover.has(SanitizerKind::SO_Alignment);

    // Emit conditional check: if not aligned, call handler.
    auto checkIf = cir::IfOp::create(
        builder, cirLoc, isAligned, /*withElseRegion=*/true,
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          builder.createYield(ifLoc);
        },
        [&](mlir::OpBuilder &, mlir::Location ifLoc) {
          if (isTrap) {
            auto trap = cir::TrapOp::create(builder, ifLoc);
            // SanitizerHandler::AlignmentAssumption = 23
            trap->setAttr(
                "cir.ubsantrap_handler",
                mlir::IntegerAttr::get(
                    mlir::IntegerType::get(builder.getContext(), 8), 23));
            markNoSanitize(trap);
          } else {
            // Emit stub call to alignment assumption handler.
            auto stubFnTy = cir::FuncType::get(
                {ptrAsInt.getType(), ptrAsInt.getType(), ptrAsInt.getType()},
                builder.getVoidTy());
            auto stubFn = cgm.createRuntimeFunction(
                stubFnTy, "__cir_ubsan_alignment_assumption_handler",
                /*attrs=*/{}, /*isLocal=*/false, /*assumeConvergent=*/false);

            // Prepare offset: if no offset, pass i1 false (0).
            mlir::Value offsetArg =
                offsetValue
                    ? offsetValue
                    : builder.getConstInt(ifLoc, intPtrTy, 0);

            auto call = builder.createCallOp(
                ifLoc, stubFn, {ptrAsInt, alignVal, offsetArg});
            setUBSanSourceLocationAttrs(*this, call, loc);
            // Set secondary source location (assumption site) only when
            // it's a valid, different location. OGCG uses zeroinitializer
            // when there's no separate assumption location.
            if (assumptionLoc.isValid() && assumptionLoc != loc) {
              mlir::Location secLoc = getLoc(assumptionLoc);
              auto fileLoc = mlir::dyn_cast<mlir::FileLineColLoc>(secLoc);
              if (!fileLoc)
                fileLoc = secLoc->findInstanceOf<mlir::FileLineColLoc>();
              if (fileLoc) {
                call->setDiscardableAttr("cir.ubsan.secondary_source_file",
                                         fileLoc.getFilename());
                call->setDiscardableAttr(
                    "cir.ubsan.secondary_source_line",
                    builder.getI32IntegerAttr(fileLoc.getLine()));
                call->setDiscardableAttr(
                    "cir.ubsan.secondary_source_column",
                    builder.getI32IntegerAttr(fileLoc.getColumn()));
              }
            }
            setUBSanTypeNameAttr(*this, call, ty);
            setUBSanTypeDescriptorAttrs(*this, call, ty);
            call->setDiscardableAttr(
                "cir.ubsan.handler_kind",
                builder.getStringAttr(isRecover ? "recover" : "abort"));
            call->setDiscardableAttr(
                "cir.ubsan.handler_name",
                builder.getStringAttr("alignment_assumption"));
            call->setDiscardableAttr(
                "cir.nosanitize",
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

  return cir::AssumeAlignedOp::create(builder, getLoc(assumptionLoc), ptrValue,
                                      getI64AttrCached(alignment),
                                      /*alignment_value=*/nullptr,
                                      offsetValue);
}

mlir::Value CIRGenFunction::emitAlignmentAssumption(
    mlir::Value ptrValue, const Expr *expr, SourceLocation assumptionLoc,
    int64_t alignment, mlir::Value offsetValue) {
  QualType ty = expr->getType();
  SourceLocation loc = expr->getExprLoc();
  return emitAlignmentAssumption(ptrValue, ty, loc, assumptionLoc, alignment,
                                 offsetValue);
}

CIRGenFunction::VlaSizePair CIRGenFunction::getVLASize(QualType type) {
  const VariableArrayType *vla =
      cgm.getASTContext().getAsVariableArrayType(type);
  assert(vla && "type was not a variable array type!");
  return getVLASize(vla);
}

CIRGenFunction::VlaSizePair
CIRGenFunction::getVLASize(const VariableArrayType *type) {
  // The number of elements so far; always size_t.
  mlir::Value numElements;

  QualType elementType;
  do {
    elementType = type->getElementType();
    mlir::Value vlaSize = vlaSizeMap[type->getSizeExpr()];
    assert(vlaSize && "no size for VLA!");
    assert(vlaSize.getType() == sizeTy);

    if (!numElements) {
      numElements = vlaSize;
    } else {
      // It's undefined behavior if this wraps around, so mark it that way.
      // FIXME: Teach -fsanitize=undefined to trap this.

      numElements =
          builder.createMul(numElements.getLoc(), numElements, vlaSize,
                            cir::OverflowBehavior::NoUnsignedWrap);
    }
  } while ((type = getContext().getAsVariableArrayType(elementType)));

  assert(numElements && "Undefined elements number");
  return {numElements, elementType};
}

CIRGenFunction::VlaSizePair
CIRGenFunction::getVLAElements1D(const VariableArrayType *vla) {
  mlir::Value vlaSize = vlaSizeMap[vla->getSizeExpr()];
  assert(vlaSize && "no size for VLA!");
  assert(vlaSize.getType() == sizeTy);
  return {vlaSize, vla->getElementType()};
}

// TODO(cir): Most of this function can be shared between CIRGen
// and traditional LLVM codegen
void CIRGenFunction::emitVariablyModifiedType(QualType type) {
  assert(type->isVariablyModifiedType() &&
         "Must pass variably modified type to EmitVLASizes!");

  // We're going to walk down into the type and look for VLA
  // expressions.
  do {
    assert(type->isVariablyModifiedType());

    const Type *ty = type.getTypePtr();
    switch (ty->getTypeClass()) {
    case Type::HLSLInlineSpirv:
    case Type::PredefinedSugar:
      llvm_unreachable("type class is never variably-modified!");

#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_TYPE(Class, Base)
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base)
#include "clang/AST/TypeNodes.inc"
      llvm_unreachable(
          "dependent type must be resolved before the CIR codegen");

    // These types are never variably-modified.
    case Type::Builtin:
    case Type::Complex:
    case Type::Vector:
    case Type::ExtVector:
    case Type::ConstantMatrix:
    case Type::Record:
    case Type::Enum:
    case Type::Using:
    case Type::TemplateSpecialization:
    case Type::ObjCTypeParam:
    case Type::ObjCObject:
    case Type::ObjCInterface:
    case Type::ObjCObjectPointer:
    case Type::BitInt:
      llvm_unreachable("type class is never variably-modified!");

    case Type::Adjusted:
      type = cast<clang::AdjustedType>(ty)->getAdjustedType();
      break;

    case Type::Decayed:
      type = cast<clang::DecayedType>(ty)->getPointeeType();
      break;

    case Type::Pointer:
      type = cast<clang::PointerType>(ty)->getPointeeType();
      break;

    case Type::BlockPointer:
      type = cast<clang::BlockPointerType>(ty)->getPointeeType();
      break;

    case Type::LValueReference:
    case Type::RValueReference:
      type = cast<clang::ReferenceType>(ty)->getPointeeType();
      break;

    case Type::MemberPointer:
      type = cast<clang::MemberPointerType>(ty)->getPointeeType();
      break;

    case Type::ArrayParameter:
    case Type::ConstantArray:
    case Type::IncompleteArray:
      // Losing element qualification here is fine.
      type = cast<clang::ArrayType>(ty)->getElementType();
      break;

    case Type::VariableArray: {
      // Losing element qualification here is fine.
      const VariableArrayType *vat = cast<clang::VariableArrayType>(ty);

      // Unknown size indication requires no size computation.
      // Otherwise, evaluate and record it.
      if (const Expr *sizeExpr = vat->getSizeExpr()) {
        // It's possible that we might have emitted this already,
        // e.g. with a typedef and a pointer to it.
        mlir::Value &entry = vlaSizeMap[sizeExpr];
        if (!entry) {
          mlir::Value size = emitScalarExpr(sizeExpr);

          // C11 6.7.6.2p5:
          //   If the size is an expression that is not an integer constant
          //   expression [...] each time it is evaluated it shall have a value
          //   greater than zero.
          if (sanOpts.has(SanitizerKind::VLABound)) {
            mlir::Location checkLoc = getLoc(sizeExpr->getBeginLoc());
            auto noSanitize = mlir::UnitAttr::get(builder.getContext());
            mlir::Value zero = builder.getConstInt(checkLoc, size.getType(), 0);
            mlir::Value isPositive = cir::CmpOp::create(
                builder, checkLoc, cir::CmpOpKind::gt, size, zero);
            markNoSanitize(isPositive.getDefiningOp());

            auto ifOp = cir::IfOp::create(
                builder, checkLoc, isPositive, /*withElseRegion=*/true,
                [&](mlir::OpBuilder &, mlir::Location thenLoc) {
                  auto yield = cir::YieldOp::create(builder, thenLoc);
                  yield->setAttr("cir.nosanitize", noSanitize);
                },
                [&](mlir::OpBuilder &, mlir::Location elseLoc) {
                  if (cgm.getCodeGenOpts().SanitizeTrap.has(
                          SanitizerKind::VLABound)) {
                    auto trap = cir::TrapOp::create(builder, elseLoc);
                    trap->setAttr(
                        "cir.ubsantrap_handler",
                        mlir::IntegerAttr::get(
                            mlir::IntegerType::get(builder.getContext(), 8),
                            kVLABoundTrapHandlerID));
                    trap->setAttr("cir.nosanitize", noSanitize);
                    return;
                  }

                  auto intptrTy = mlir::cast<cir::IntType>(
                      convertType(getContext().getUIntPtrType()));
                  auto handlerArg = emitSanitizerValueHandle(
                      *this, size, sizeExpr->getType(), elseLoc);
                  auto stubFnTy =
                      cir::FuncType::get({intptrTy}, builder.getVoidTy());
                  auto stubFn = cgm.createRuntimeFunction(
                      stubFnTy, "__cir_ubsan_vla_bound_not_positive",
                      /*attrs=*/{}, /*isLocal=*/false,
                      /*assumeConvergent=*/false);
                  auto call = builder.createCallOp(elseLoc, stubFn, {handlerArg});
                  call->setAttr("cir.call_nomerge", noSanitize);
                  setUBSanSourceLocationAttrs(*this, call,
                                              sizeExpr->getBeginLoc());
                  setUBSanTypeNameAttr(*this, call, sizeExpr->getType());
                  setUBSanTypeDescriptorAttrs(*this, call, sizeExpr->getType());
                  call->setDiscardableAttr(
                      "cir.ubsan.handler_name",
                      builder.getStringAttr("vla_bound_not_positive"));
                  StringRef handlerKind =
                      cgm.getCodeGenOpts().SanitizeRecover.has(
                          SanitizerKind::VLABound)
                          ? "recover"
                          : "abort";
                  call->setDiscardableAttr("cir.ubsan.handler_kind",
                                           builder.getStringAttr(handlerKind));
                  call->setDiscardableAttr("cir.nosanitize", noSanitize);

                  if (handlerKind == "abort") {
                    auto unreachable =
                        cir::UnreachableOp::create(builder, elseLoc);
                    unreachable->setAttr("cir.nosanitize", noSanitize);
                  } else {
                    auto yield = cir::YieldOp::create(builder, elseLoc);
                    yield->setAttr("cir.nosanitize", noSanitize);
                  }
                });
            setSanitizerBranchAttrs(builder, ifOp);
          }

          // Always zexting here would be wrong if it weren't
          // undefined behavior to have a negative bound.
          // FIXME: What about when size's type is larger than size_t?
          // Use zero-extension (unsigned cast) matching OGCG
          // (CodeGenFunction.cpp:2573).
          auto srcIntTy = mlir::cast<cir::IntType>(size.getType());
          if (srcIntTy.isSigned()) {
            // Cast through unsigned of same width first to force zext.
            auto unsignedSrcTy = cir::IntType::get(
                builder.getContext(), srcIntTy.getWidth(), /*isSigned=*/false);
            size = builder.createCast(cir::CastKind::integral, size,
                                      unsignedSrcTy);
          }
          entry = builder.createIntCast(size, sizeTy);
        }
      }
      type = vat->getElementType();
      break;
    }

    case Type::FunctionProto:
    case Type::FunctionNoProto:
      type = cast<clang::FunctionType>(ty)->getReturnType();
      break;

    case Type::Paren:
    case Type::TypeOf:
    case Type::UnaryTransform:
    case Type::Attributed:
    case Type::BTFTagAttributed:
    case Type::HLSLAttributedResource:
    case Type::SubstTemplateTypeParm:
    case Type::MacroQualified:
    case Type::CountAttributed:
      // Keep walking after single level desugaring.
      type = type.getSingleStepDesugaredType(getContext());
      break;

    case Type::Typedef:
    case Type::Decltype:
    case Type::Auto:
    case Type::DeducedTemplateSpecialization:
    case Type::PackIndexing:
      // Stop walking: nothing to do.
      return;

    case Type::TypeOfExpr:
      // Stop walking: emit typeof expression.
      emitIgnoredExpr(cast<clang::TypeOfExprType>(ty)->getUnderlyingExpr());
      return;

    case Type::Atomic:
      type = cast<clang::AtomicType>(ty)->getValueType();
      break;

    case Type::Pipe:
      type = cast<clang::PipeType>(ty)->getElementType();
      break;
    }
  } while (type->isVariablyModifiedType());
}

Address CIRGenFunction::emitVAListRef(const Expr *e) {
  if (getContext().getBuiltinVaListType()->isArrayType())
    return emitPointerWithAlignment(e);
  return emitLValue(e).getAddress();
}

Address CIRGenFunction::emitMSVAListRef(const Expr *e) {
  return emitLValue(e).getAddress();
}

void CIRGenFunction::emitVarAnnotations(const VarDecl *d, mlir::Value val) {
  assert(d->hasAttr<AnnotateAttr>() && "no annotate attribute");
  llvm::SmallVector<mlir::Attribute, 4> annotations;
  for (const auto *annot : d->specific_attrs<AnnotateAttr>())
    annotations.push_back(cgm.emitAnnotateAttr(annot));
  auto allocaOp = val.getDefiningOp<cir::AllocaOp>();
  assert(allocaOp && "expects available alloca");
  allocaOp.setAnnotationsAttr(builder.getArrayAttr(annotations));
}

Address CIRGenFunction::emitFieldAnnotations(const FieldDecl *d,
                                             Address addr) {
  assert(d->hasAttr<AnnotateAttr>() && "no annotate attribute");
  llvm::SmallVector<mlir::Attribute, 4> annotations;
  for (const auto *annot : d->specific_attrs<AnnotateAttr>())
    annotations.push_back(cgm.emitAnnotateAttr(annot));
  mlir::Location loc = getLoc(d->getLocation());
  mlir::Value result = cir::PtrAnnotationOp::create(
      builder, loc, addr.getPointer().getType(), addr.getPointer(),
      builder.getArrayAttr(annotations));
  return Address(result, addr.getElementType(), addr.getAlignment());
}

void CIRGenFunction::emitKernelMetadata(const FunctionDecl *fd,
                                        cir::FuncOp fn) {
  if (!(fd->hasAttr<DeviceKernelAttr>() &&
        DeviceKernelAttr::isOpenCLSpelling(fd->getAttr<DeviceKernelAttr>())) &&
      !fd->hasAttr<CUDAGlobalAttr>())
    return;

  cgm.genKernelArgMetadata(fn, fd, this);

  if (!getLangOpts().OpenCL)
    return;

  using cir::OpenCLKernelMetadataAttr;

  mlir::ArrayAttr workGroupSizeHintAttr, reqdWorkGroupSizeAttr;
  mlir::TypeAttr vecTypeHintAttr;
  std::optional<bool> vecTypeHintSignedness;
  mlir::IntegerAttr intelReqdSubGroupSizeAttr;

  if (const VecTypeHintAttr *a = fd->getAttr<VecTypeHintAttr>()) {
    mlir::Type typeHintValue = convertType(a->getTypeHint());
    vecTypeHintAttr = mlir::TypeAttr::get(typeHintValue);
    vecTypeHintSignedness =
        OpenCLKernelMetadataAttr::isSignedHint(typeHintValue);
  }

  if (const WorkGroupSizeHintAttr *a = fd->getAttr<WorkGroupSizeHintAttr>()) {
    auto eval = [&](Expr *e) {
      return e->EvaluateKnownConstInt(fd->getASTContext()).getExtValue();
    };
    workGroupSizeHintAttr = builder.getI32ArrayAttr({
        static_cast<int32_t>(eval(a->getXDim())),
        static_cast<int32_t>(eval(a->getYDim())),
        static_cast<int32_t>(eval(a->getZDim())),
    });
  }

  if (const ReqdWorkGroupSizeAttr *a = fd->getAttr<ReqdWorkGroupSizeAttr>()) {
    auto eval = [&](Expr *e) {
      return e->EvaluateKnownConstInt(fd->getASTContext()).getExtValue();
    };
    reqdWorkGroupSizeAttr = builder.getI32ArrayAttr({
        static_cast<int32_t>(eval(a->getXDim())),
        static_cast<int32_t>(eval(a->getYDim())),
        static_cast<int32_t>(eval(a->getZDim())),
    });
  }

  if (const OpenCLIntelReqdSubGroupSizeAttr *a =
          fd->getAttr<OpenCLIntelReqdSubGroupSizeAttr>()) {
    intelReqdSubGroupSizeAttr = getI32AttrCached(a->getSubGroupSize());
  }

  // Skip the metadata attr if no hints are present.
  if (!vecTypeHintAttr && !workGroupSizeHintAttr && !reqdWorkGroupSizeAttr &&
      !intelReqdSubGroupSizeAttr)
    return;

  // Append the kernel metadata to the extra attributes dictionary.
  mlir::NamedAttrList attrs;
  if (auto existingAttrs = fn.getExtraAttrs())
    attrs.append(existingAttrs->getElements());

  auto kernelMetadataAttr = OpenCLKernelMetadataAttr::get(
      &getMLIRContext(), workGroupSizeHintAttr, reqdWorkGroupSizeAttr,
      vecTypeHintAttr, vecTypeHintSignedness, intelReqdSubGroupSizeAttr);
  attrs.append(kernelMetadataAttr.getMnemonic(), kernelMetadataAttr);

  fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
      attrs.getDictionary(&getMLIRContext())));
}

// Call-graph-section type generalization: generalizes pointer types to void*
// (preserving CVR qualifiers) so that indirect-call type identifiers are
// stable across translation units.
static QualType generalizeTypeForCallGraphSection(ASTContext &ctx,
                                                  QualType ty) {
  if (const RecordType *ut = ty->getAsUnionType()) {
    const RecordDecl *ud = ut->getDecl()->getDefinitionOrSelf();
    if (ud->hasAttr<TransparentUnionAttr>() && !ud->fields().empty())
      ty = ud->fields().begin()->getType();
  }

  if (!ty->isPointerType())
    return ty;

  return ctx.getPointerType(
      QualType(ctx.VoidTy)
          .withCVRQualifiers(ty->getPointeeType().getCVRQualifiers()));
}

static QualType generalizeFunctionTypeForCallGraphSection(ASTContext &ctx,
                                                          QualType ty) {
  if (auto *fnType = ty->getAs<FunctionProtoType>()) {
    SmallVector<QualType, 8> generalizedParams;
    for (QualType param : fnType->param_types())
      generalizedParams.push_back(
          generalizeTypeForCallGraphSection(ctx, param));
    return ctx.getFunctionType(
        generalizeTypeForCallGraphSection(ctx, fnType->getReturnType()),
        generalizedParams, fnType->getExtProtoInfo());
  }

  if (auto *fnType = ty->getAs<FunctionNoProtoType>())
    return ctx.getFunctionNoProtoType(
        generalizeTypeForCallGraphSection(ctx, fnType->getReturnType()));

  llvm_unreachable("encountered unknown FunctionType in call graph metadata");
}

void CIRGenFunction::maybeAttachCallGraphSectionAttrs(
    cir::CIRCallOpInterface call, QualType fnType) {
  if (!cgm.getCodeGenOpts().CallGraphSection)
    return;

  fnType =
      generalizeFunctionTypeForCallGraphSection(getContext(), fnType);
  std::string typeId = cgm.createMetadataIdentifierStringForType(fnType);
  if (typeId.empty())
    return;

  typeId += ".generalized";
  call->setAttr("cir.call_graph.callee_type",
                cgm.getStringAttrCached(typeId));
}

} // namespace clang::CIRGen
