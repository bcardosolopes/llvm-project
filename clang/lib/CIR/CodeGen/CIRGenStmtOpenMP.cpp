//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Emit OpenMP Stmt nodes as CIR code.
//
//===----------------------------------------------------------------------===//

#include "CIRGenBuilder.h"
#include "CIRGenFunction.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/StmtOpenMP.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Frontend/OpenMP/OMPConstants.h"
#include "llvm/IR/Instructions.h"
#include <algorithm>
#include <limits>
#include <memory>

using namespace clang;
using namespace clang::CIRGen;

CIRGenFunction::OpenMPNontemporalDeclsRAII::OpenMPNontemporalDeclsRAII(
    CIRGenFunction &cgf, const OMPLoopDirective &s)
    : cgf(cgf), needToPush(s.hasClausesOfKind<OMPNontemporalClause>()) {
  if (!needToPush)
    return;

  OpenMPNontemporalDeclsSet &decls =
      cgf.openMPNontemporalDeclsStack.emplace_back();
  for (const auto *clause : s.getClausesOfKind<OMPNontemporalClause>()) {
    for (const Stmt *ref : clause->private_refs()) {
      const Expr *simpleRefExpr = cast<Expr>(ref)->IgnoreParenImpCasts();
      const ValueDecl *vd;
      if (const auto *dre = dyn_cast<DeclRefExpr>(simpleRefExpr)) {
        vd = cast<ValueDecl>(dre->getDecl());
      } else {
        const auto *me = cast<MemberExpr>(simpleRefExpr);
        assert((me->isImplicitCXXThis() ||
                isa<CXXThisExpr>(me->getBase()->IgnoreParenImpCasts())) &&
               "expected member of current class");
        vd = cast<ValueDecl>(me->getMemberDecl());
      }
      decls.insert(vd->getCanonicalDecl());
    }
  }
}

CIRGenFunction::OpenMPNontemporalDeclsRAII::~OpenMPNontemporalDeclsRAII() {
  if (!needToPush)
    return;
  cgf.openMPNontemporalDeclsStack.pop_back();
}

bool CIRGenFunction::isOpenMPNontemporalDecl(const ValueDecl *vd) const {
  if (!vd)
    return false;

  return llvm::any_of(
      openMPNontemporalDeclsStack,
      [vd](const OpenMPNontemporalDeclsSet &decls) {
        return decls.contains(vd->getCanonicalDecl());
      });
}

namespace {

static void setRuntimeFnNothrow(cir::FuncOp fn) {
  mlir::NamedAttrList attrs;
  if (auto existingAttrs = fn.getExtraAttrs())
    attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
  attrs.set("nothrow", mlir::UnitAttr::get(fn->getContext()));
  fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
      attrs.getDictionary(fn->getContext())));
}

/// Emit the OpenMP loop body wrapped in a do_while(false) so that `continue`
/// statements produce valid cir.continue ops targeting a CIR LoopOpInterface.
/// Without this wrapper, cir.continue inside omp.loop_nest has no CIR loop
/// parent and fails verification.
static mlir::LogicalResult
emitOMPLoopBody(CIRGenFunction &cgf, CIRGenBuilderTy &builder,
                mlir::Location loc, const Stmt *body) {
  mlir::LogicalResult bodyRes = mlir::success();
  auto doWhileOp = builder.createDoWhile(
      loc,
      /*condBuilder=*/
      [&](mlir::OpBuilder &, mlir::Location condLoc) {
        auto falseVal =
            cir::ConstantOp::create(builder, condLoc, builder.getFalseAttr());
        builder.createCondition(falseVal);
      },
      /*bodyBuilder=*/
      [&](mlir::OpBuilder &, mlir::Location bodyLoc) {
        bodyRes = cgf.emitStmt(body, /*useCurrentScope=*/false);
      });

  // Ensure all blocks in the do-while body have proper terminators.
  for (mlir::Block &block : doWhileOp.getBody()) {
    if (block.empty() ||
        !block.back().hasTrait<mlir::OpTrait::IsTerminator>()) {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToEnd(&block);
      builder.createYield(loc);
    }
  }
  return bodyRes;
}

static const VarDecl *getOpenMPVarDeclFromExpr(const Expr *e) {
  if (!e)
    return nullptr;
  e = e->IgnoreImpCasts();
  const auto *dre = dyn_cast<DeclRefExpr>(e);
  if (!dre)
    return nullptr;
  return dyn_cast<VarDecl>(dre->getDecl());
}

static bool isOpenMPVarExpr(const Expr *e, const VarDecl *vd) {
  return getOpenMPVarDeclFromExpr(e) == vd;
}

static std::string
getOpenMPLastprivateConditionalName(const VarDecl *vd,
                                    SourceLocation directiveLoc) {
  std::string name = ("__omp_lp_cond_" + vd->getName()).str();
  name += ("_" + llvm::Twine(vd->getCanonicalDecl()->getBeginLoc()
                                 .getRawEncoding()) +
           "_" + llvm::Twine(directiveLoc.getRawEncoding()))
              .str();
  return name;
}

static Address getOrCreateOpenMPLastprivateConditionalGlobal(
    CIRGenFunction &cgf, mlir::Location loc, llvm::StringRef name,
    mlir::Type valueTy, CharUnits alignment) {
  CIRGenModule &cgm = cgf.getCIRGenModule();
  cir::GlobalOp global =
      mlir::dyn_cast_or_null<cir::GlobalOp>(cgm.getGlobalValue(name));
  if (!global) {
    global = CIRGenModule::createGlobalOp(cgm, loc, name, valueTy,
                                          /*isConstant=*/false);
    global.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    global.setSymVisibility("private");
    global.setInitialValueAttr(cgf.getBuilder().getZeroInitAttr(valueTy));
  }

  mlir::Type ptrTy =
      cgf.getBuilder().getPointerTo(global.getSymType(), global.getAddrSpaceAttr());
  mlir::Value addr = cir::GetGlobalOp::create(cgf.getBuilder(), loc, ptrTy,
                                              global.getSymNameAttr(),
                                              /*threadLocal=*/false);
  return Address(addr, global.getSymType(), alignment);
}

static void emitOpenMPLastprivateConditionalUpdate(
    CIRGenFunction &cgf, mlir::Location loc, Address ivAddr,
    Address privateAddr, QualType valueTy, llvm::StringRef uniqueName,
    SourceLocation sourceLoc) {
  std::string lastIVName = (llvm::Twine(uniqueName) + ".iv").str();
  Address lastIVAddr = getOrCreateOpenMPLastprivateConditionalGlobal(
      cgf, loc, lastIVName, ivAddr.getElementType(), ivAddr.getAlignment());
  Address lastValueAddr = getOrCreateOpenMPLastprivateConditionalGlobal(
      cgf, loc, uniqueName, cgf.convertTypeForMem(valueTy),
      cgf.getContext().getTypeAlignInChars(valueTy));

  auto emitUpdate = [&](mlir::OpBuilder &updateBuilder, mlir::Location updateLoc) {
    mlir::OpBuilder::InsertionGuard guard(cgf.getBuilder());
    cgf.getBuilder().setInsertionPointToEnd(updateBuilder.getInsertionBlock());

    mlir::Value ivValue = cgf.getBuilder().createLoad(updateLoc, ivAddr);
    mlir::Value lastIVValue = cgf.getBuilder().createLoad(updateLoc, lastIVAddr);
    mlir::Value shouldUpdate = cgf.getBuilder().createCompare(
        updateLoc, cir::CmpOpKind::le, lastIVValue, ivValue);

    cir::IfOp::create(
        cgf.getBuilder(), updateLoc, shouldUpdate, /*withElseRegion=*/false,
        [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
          mlir::OpBuilder::InsertionGuard thenGuard(cgf.getBuilder());
          cgf.getBuilder().setInsertionPointToEnd(thenBuilder.getInsertionBlock());
          cgf.getBuilder().createStore(thenLoc, ivValue, lastIVAddr);

          LValue privateLValue = cgf.makeAddrLValue(privateAddr, valueTy);
          LValue globalLValue = cgf.makeAddrLValue(lastValueAddr, valueTy);
          switch (cgf.getEvaluationKind(valueTy)) {
          case cir::TEK_Scalar: {
            mlir::Value privateValue =
                cgf.emitLoadOfScalar(privateLValue, sourceLoc);
            cgf.emitStoreOfScalar(privateValue, globalLValue, /*isInit=*/false);
            break;
          }
          case cir::TEK_Complex: {
            mlir::Value privateValue =
                cgf.emitLoadOfComplex(privateLValue, sourceLoc);
            cgf.emitStoreOfComplex(thenLoc, privateValue, globalLValue,
                                   /*isInit=*/false);
            break;
          }
          case cir::TEK_Aggregate:
            cgf.getCIRGenModule().errorNYI(
                updateLoc, "OpenMP conditional lastprivate aggregate");
            break;
          }

          cir::YieldOp::create(thenBuilder, thenLoc);
        });
  };

  if (cgf.getLangOpts().OpenMPSimd) {
    emitUpdate(cgf.getBuilder(), loc);
    return;
  }

  mlir::FlatSymbolRefAttr criticalName =
      cgf.getCIRGenModule().getOrCreateOpenMPCriticalDecl(uniqueName, std::nullopt,
                                                          loc);
  auto criticalOp = mlir::omp::CriticalOp::create(cgf.getBuilder(), loc, criticalName);
  {
    mlir::OpBuilder::InsertionGuard guard(cgf.getBuilder());
    mlir::Block *block = cgf.getBuilder().createBlock(&criticalOp.getRegion());
    cgf.getBuilder().setInsertionPointToEnd(block);
    emitUpdate(cgf.getBuilder(), loc);
    mlir::omp::TerminatorOp::create(cgf.getBuilder(), loc);
  }
}

static void emitOpenMPLastprivateConditionalSetFired(CIRGenFunction &cgf,
                                                     mlir::Location loc,
                                                     Address firedAddr) {
  QualType firedTy = cgf.getContext().UnsignedCharTy;
  mlir::Value one = cgf.getBuilder().getConstInt(
      loc, mlir::cast<cir::IntType>(firedAddr.getElementType()), 1);
  cgf.emitAtomicStore(RValue::get(one), cgf.makeAddrLValue(firedAddr, firedTy),
                      cir::MemOrder::Relaxed, /*isVolatile=*/true,
                      /*isInit=*/false);
}

} // namespace

bool CIRGenFunction::pushOpenMPLastprivateConditional(
    ArrayRef<const OMPClause *> clauses, Address ivAddr,
    SourceLocation directiveLoc) {
  if (getLangOpts().OpenMP < 50)
    return false;

  OpenMPLastprivateConditionalFrame frame;
  frame.ivAddr = ivAddr;
  for (const OMPClause *clause : clauses) {
    if (clause->getClauseKind() != llvm::omp::OMPC_lastprivate)
      continue;

    const auto *lastprivateClause = cast<OMPLastprivateClause>(clause);
    if (lastprivateClause->getKind() != OMPC_LASTPRIVATE_conditional)
      continue;

    for (const Expr *varExpr : lastprivateClause->varlist()) {
      const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr);
      if (!vd)
        continue;
      vd = vd->getCanonicalDecl();
      frame.declToUniqueName.try_emplace(
          vd, getOpenMPLastprivateConditionalName(vd, directiveLoc));
    }
  }

  if (frame.declToUniqueName.empty())
    return false;

  mlir::Location loc = getLoc(directiveLoc);
  QualType firedTy = getContext().UnsignedCharTy;
  mlir::Type firedMemTy = convertTypeForMem(firedTy);
  CharUnits firedAlign = getContext().getTypeAlignInChars(firedTy);
  for (const auto &[vd, _] : frame.declToUniqueName) {
    Address firedRawAlloca = Address::invalid();
    Address firedAddr = createTempAlloca(
        firedMemTy, firedAlign, loc,
        ("__omp_lp_cond_fired_" + vd->getName()).str(), /*arraySize=*/nullptr,
        &firedRawAlloca);
    builder.createStore(
        loc, builder.getConstInt(loc, mlir::cast<cir::IntType>(firedMemTy), 0),
        firedAddr);
    frame.declToFiredAddr.try_emplace(vd, firedAddr);
  }

  openMPLastprivateConditionalStack.push_back(std::move(frame));
  return true;
}

bool CIRGenFunction::pushDisabledOpenMPLastprivateConditional(
    const OMPExecutableDirective &directive) {
  if (getLangOpts().OpenMP < 50 || openMPLastprivateConditionalStack.empty())
    return false;

  const CapturedStmt *capturedStmt = directive.getInnermostCapturedStmt();
  if (!capturedStmt)
    return false;

  const OpenMPLastprivateConditionalFrame *activeFrame = nullptr;
  for (const OpenMPLastprivateConditionalFrame &candidate :
       llvm::reverse(openMPLastprivateConditionalStack)) {
    if (!candidate.disabled) {
      activeFrame = &candidate;
      break;
    }
  }
  if (!activeFrame)
    return false;

  OpenMPLastprivateConditionalFrame frame;
  frame.disabled = true;
  for (const auto &[vd, _] : activeFrame->declToUniqueName)
    if (capturedStmt->capturesVariable(vd))
      frame.declToUniqueName.try_emplace(vd);

  if (frame.declToUniqueName.empty())
    return false;

  openMPLastprivateConditionalStack.push_back(std::move(frame));
  return true;
}

void CIRGenFunction::popOpenMPLastprivateConditional() {
  assert(!openMPLastprivateConditionalStack.empty() &&
         "no active OpenMP lastprivate conditional frame");
  openMPLastprivateConditionalStack.pop_back();
}

void CIRGenFunction::checkAndEmitOpenMPLastprivateConditional(const Expr *lhs) {
  if (getLangOpts().OpenMP < 50 || openMPLastprivateConditionalStack.empty() ||
      !lhs)
    return;

  const VarDecl *vd = getOpenMPVarDeclFromExpr(lhs);
  if (!vd)
    return;
  vd = vd->getCanonicalDecl();

  const OpenMPLastprivateConditionalFrame *frame = nullptr;
  const std::string *uniqueName = nullptr;
  Address firedAddr = Address::invalid();
  bool disabled = false;
  for (const OpenMPLastprivateConditionalFrame &candidate :
       llvm::reverse(openMPLastprivateConditionalStack)) {
    auto it = candidate.declToUniqueName.find(vd);
    if (it == candidate.declToUniqueName.end())
      continue;
    if (candidate.disabled) {
      disabled = true;
      continue;
    }
    frame = &candidate;
    uniqueName = &it->second;
    if (auto firedIt = candidate.declToFiredAddr.find(vd);
        firedIt != candidate.declToFiredAddr.end())
      firedAddr = firedIt->second;
    break;
  }
  if (!frame)
    return;

  QualType valueTy = vd->getType().getNonReferenceType();
  mlir::Location loc = getLoc(lhs->getExprLoc());
  if (disabled) {
    if (firedAddr.isValid())
      emitOpenMPLastprivateConditionalSetFired(*this, loc, firedAddr);
    return;
  }

  emitOpenMPLastprivateConditionalUpdate(
      *this, loc, frame->ivAddr, emitLValue(lhs).getAddress(), valueTy,
      *uniqueName, lhs->getExprLoc());
}

void CIRGenFunction::emitOpenMPLastprivateConditionalFinalUpdate(
    const VarDecl *vd, Address privateAddr, SourceLocation loc) {
  if (getLangOpts().OpenMP < 50 || openMPLastprivateConditionalStack.empty())
    return;

  vd = vd->getCanonicalDecl();
  const std::string *uniqueName = nullptr;
  for (const OpenMPLastprivateConditionalFrame &candidate :
       llvm::reverse(openMPLastprivateConditionalStack)) {
    auto it = candidate.declToUniqueName.find(vd);
    if (it == candidate.declToUniqueName.end())
      continue;
    if (candidate.disabled)
      continue;
    uniqueName = &it->second;
    break;
  }
  if (!uniqueName)
    return;

  cir::GlobalOp global =
      mlir::dyn_cast_or_null<cir::GlobalOp>(getCIRGenModule().getGlobalValue(*uniqueName));
  if (!global)
    return;

  QualType valueTy = vd->getType().getNonReferenceType();
  Address globalAddr = getOrCreateOpenMPLastprivateConditionalGlobal(
      *this, getLoc(loc), *uniqueName, convertTypeForMem(valueTy),
      getContext().getTypeAlignInChars(valueTy));
  LValue privateLValue = makeAddrLValue(privateAddr, valueTy);
  LValue globalLValue = makeAddrLValue(globalAddr, valueTy);
  switch (getEvaluationKind(valueTy)) {
  case cir::TEK_Scalar: {
    mlir::Value value = emitLoadOfScalar(globalLValue, loc);
    emitStoreOfScalar(value, privateLValue, /*isInit=*/false);
    break;
  }
  case cir::TEK_Complex: {
    mlir::Value value = emitLoadOfComplex(globalLValue, loc);
    emitStoreOfComplex(getLoc(loc), value, privateLValue, /*isInit=*/false);
    break;
  }
  case cir::TEK_Aggregate:
    getCIRGenModule().errorNYI(vd->getSourceRange(),
                               "OpenMP conditional lastprivate aggregate");
    break;
  }
}

static void
checkAndEmitSharedOpenMPLastprivateConditional(
    CIRGenFunction &cgf, const OMPExecutableDirective &directive,
    const llvm::SmallPtrSetImpl<const VarDecl *> &ignoredDecls) {
  if (cgf.getLangOpts().OpenMP < 50 ||
      cgf.openMPLastprivateConditionalStack.empty())
    return;

  const CapturedStmt *capturedStmt = directive.getInnermostCapturedStmt();
  if (!capturedStmt)
    return;

  const CIRGenFunction::OpenMPLastprivateConditionalFrame *frame = nullptr;
  for (const CIRGenFunction::OpenMPLastprivateConditionalFrame &candidate :
       llvm::reverse(cgf.openMPLastprivateConditionalStack)) {
    if (!candidate.disabled) {
      frame = &candidate;
      break;
    }
  }
  if (!frame)
    return;

  mlir::Location loc = cgf.getLoc(directive.getBeginLoc());
  for (const auto &entry : frame->declToUniqueName) {
    const VarDecl *vd = entry.first;
    const std::string &frameUniqueName = entry.second;
    if (ignoredDecls.contains(vd) || !capturedStmt->capturesVariable(vd))
      continue;
    auto firedIt = frame->declToFiredAddr.find(vd);
    if (firedIt == frame->declToFiredAddr.end())
      continue;
    const VarDecl *capturedVD = vd;
    std::string capturedUniqueName = frameUniqueName;

    mlir::Value firedValue = cgf.getBuilder().createLoad(loc, firedIt->second);
    mlir::Value zero = cgf.getBuilder().getConstInt(
        loc, mlir::cast<cir::IntType>(firedIt->second.getElementType()), 0);
    mlir::Value shouldUpdate = cgf.getBuilder().createCompare(
        loc, cir::CmpOpKind::ne, firedValue, zero);

    cir::IfOp::create(
        cgf.getBuilder(), loc, shouldUpdate, /*withElseRegion=*/false,
        [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
          mlir::OpBuilder::InsertionGuard guard(cgf.getBuilder());
          cgf.getBuilder().setInsertionPointToEnd(thenBuilder.getInsertionBlock());
          emitOpenMPLastprivateConditionalUpdate(
              cgf, thenLoc, frame->ivAddr, cgf.getAddrOfLocalVar(capturedVD),
              capturedVD->getType().getNonReferenceType(), capturedUniqueName,
              directive.getBeginLoc());
          cir::YieldOp::create(thenBuilder, thenLoc);
        });
  }
}

static void
checkForOpenMPLastprivateConditionalUpdate(CIRGenFunction &cgf,
                                           const OMPExecutableDirective &s) {
  if (cgf.getLangOpts().OpenMP < 50)
    return;

  llvm::SmallPtrSet<const VarDecl *, 8> ignoredDecls;
  auto markIgnored = [&](const Expr *ref) {
    if (const VarDecl *vd = getOpenMPVarDeclFromExpr(ref))
      ignoredDecls.insert(vd->getCanonicalDecl());
  };
  auto maybeEmitUpdate = [&](const Expr *ref) {
    if (!ref || !ref->getType()->isScalarType())
      return;
    markIgnored(ref);
    cgf.checkAndEmitOpenMPLastprivateConditional(ref);
  };

  for (const auto *clause : s.getClausesOfKind<OMPReductionClause>())
    for (const Expr *ref : clause->varlist())
      maybeEmitUpdate(ref);
  for (const auto *clause : s.getClausesOfKind<OMPLastprivateClause>())
    for (const Expr *ref : clause->varlist())
      maybeEmitUpdate(ref);
  for (const auto *clause : s.getClausesOfKind<OMPLinearClause>())
    for (const Expr *ref : clause->varlist())
      maybeEmitUpdate(ref);
  for (const auto *clause : s.getClausesOfKind<OMPFirstprivateClause>())
    for (const Expr *ref : clause->varlist())
      if (ref->getType()->isScalarType())
        markIgnored(ref);
  for (const auto *clause : s.getClausesOfKind<OMPPrivateClause>())
    for (const Expr *ref : clause->varlist())
      if (ref->getType()->isScalarType())
        markIgnored(ref);
  checkAndEmitSharedOpenMPLastprivateConditional(cgf, s, ignoredDecls);
}

namespace {

static void collectOpenMPReferencedVarDecls(
    const Expr *expr, llvm::SmallVectorImpl<const VarDecl *> &vars,
    llvm::SmallPtrSetImpl<const VarDecl *> &seenVars) {
  if (!expr)
    return;

  expr = expr->IgnoreParenImpCasts();
  if (const VarDecl *vd = getOpenMPVarDeclFromExpr(expr)) {
    const VarDecl *canonicalVD = vd->getCanonicalDecl();
    if (const auto *capturedExpr = dyn_cast<OMPCapturedExprDecl>(vd)) {
      collectOpenMPReferencedVarDecls(capturedExpr->getInit(), vars, seenVars);
      return;
    }
    if (const auto *capturedExpr = dyn_cast<OMPCapturedExprDecl>(canonicalVD)) {
      collectOpenMPReferencedVarDecls(capturedExpr->getInit(), vars, seenVars);
      return;
    }

    if (seenVars.insert(canonicalVD).second)
      vars.push_back(vd);
    return;
  }

  for (const Stmt *child : expr->children())
    if (const auto *childExpr = dyn_cast_or_null<Expr>(child))
      collectOpenMPReferencedVarDecls(childExpr, vars, seenVars);
}

static void collectOpenMPSimdSyntheticFirstprivateVars(
    ArrayRef<const OMPClause *> clauses,
    llvm::SmallVectorImpl<const VarDecl *> &vars) {
  llvm::SmallPtrSet<const VarDecl *, 8> seenVars;
  for (const OMPClause *clause : clauses) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if:
      collectOpenMPReferencedVarDecls(
          cast<OMPIfClause>(clause)->getCondition(), vars, seenVars);
      break;
    case llvm::omp::OMPC_aligned: {
      const auto *alignedClause = cast<OMPAlignedClause>(clause);
      for (const Expr *varExpr : alignedClause->varlist())
        collectOpenMPReferencedVarDecls(varExpr, vars, seenVars);
      collectOpenMPReferencedVarDecls(alignedClause->getAlignment(), vars,
                                      seenVars);
      break;
    }
    case llvm::omp::OMPC_linear: {
      const auto *linearClause = cast<OMPLinearClause>(clause);
      for (const Expr *varExpr : linearClause->varlist())
        collectOpenMPReferencedVarDecls(varExpr, vars, seenVars);
      collectOpenMPReferencedVarDecls(linearClause->getStep(), vars, seenVars);
      break;
    }
    case llvm::omp::OMPC_nontemporal:
      for (const Expr *varExpr : cast<OMPNontemporalClause>(clause)->varlist())
        collectOpenMPReferencedVarDecls(varExpr, vars, seenVars);
      break;
    default:
      break;
    }
  }
}

static void collectOpenMPClauseSyntheticFirstprivateVars(
    ArrayRef<const OMPClause *> clauses,
    llvm::SmallVectorImpl<const VarDecl *> &vars) {
  llvm::SmallPtrSet<const VarDecl *, 8> seenVars;
  for (const OMPClause *clause : clauses) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if:
      collectOpenMPReferencedVarDecls(
          cast<OMPIfClause>(clause)->getCondition(), vars, seenVars);
      break;
    case llvm::omp::OMPC_device:
      collectOpenMPReferencedVarDecls(
          cast<OMPDeviceClause>(clause)->getDevice(), vars, seenVars);
      break;
    case llvm::omp::OMPC_num_threads:
      collectOpenMPReferencedVarDecls(
          cast<OMPNumThreadsClause>(clause)->getNumThreads(), vars, seenVars);
      break;
    case llvm::omp::OMPC_message:
      collectOpenMPReferencedVarDecls(
          cast<OMPMessageClause>(clause)->getMessageString(), vars, seenVars);
      break;
    case llvm::omp::OMPC_thread_limit:
      collectOpenMPReferencedVarDecls(
          cast<OMPThreadLimitClause>(clause)->getThreadLimit().front(), vars,
          seenVars);
      break;
    case llvm::omp::OMPC_schedule: {
      const auto *scheduleClause = cast<OMPScheduleClause>(clause);
      collectOpenMPReferencedVarDecls(scheduleClause->getChunkSize(), vars,
                                      seenVars);
      break;
    }
    case llvm::omp::OMPC_linear:
      collectOpenMPReferencedVarDecls(cast<OMPLinearClause>(clause)->getStep(),
                                      vars, seenVars);
      break;
    default:
      break;
    }
  }
}

static const Expr *getOpenMPScheduleChunkExpr(
    ArrayRef<const OMPClause *> clauses) {
  for (const OMPClause *clause : clauses) {
    if (clause->getClauseKind() != llvm::omp::OMPC_schedule)
      continue;
    if (const Expr *chunk = cast<OMPScheduleClause>(clause)->getChunkSize())
      return chunk;
  }
  return nullptr;
}

static const Expr *unwrapOpenMPCapturedExprInit(const Expr *expr) {
  while (const VarDecl *vd = getOpenMPVarDeclFromExpr(expr)) {
    const VarDecl *canonicalVD = vd->getCanonicalDecl();
    const auto *capturedExpr = dyn_cast<OMPCapturedExprDecl>(vd);
    if (!capturedExpr)
      capturedExpr = dyn_cast<OMPCapturedExprDecl>(canonicalVD);
    if (!capturedExpr)
      break;
    expr = capturedExpr->getInit();
  }
  return expr;
}

static bool exprContainsOpenMPCapturedExpr(const Expr *expr) {
  if (!expr)
    return false;

  expr = expr->IgnoreParenImpCasts();
  if (const VarDecl *vd = getOpenMPVarDeclFromExpr(expr)) {
    const VarDecl *canonicalVD = vd->getCanonicalDecl();
    if (isa<OMPCapturedExprDecl>(vd) || isa<OMPCapturedExprDecl>(canonicalVD))
      return true;
  }

  for (const Stmt *child : expr->children())
    if (const auto *childExpr = dyn_cast_or_null<Expr>(child))
      if (exprContainsOpenMPCapturedExpr(childExpr))
        return true;

  return false;
}

static mlir::Value createOpenMPIntegerConstant(CIRGenFunction &cgf,
                                               mlir::Location loc,
                                               mlir::Type ty, int64_t value);

static mlir::Value scaleOpenMPSimdLinearStepValue(CIRGenFunction &cgf,
                                                  mlir::Location loc,
                                                  mlir::Value step,
                                                  QualType linearTy) {
  linearTy = linearTy.getNonReferenceType();
  if (!linearTy->isPointerType())
    return step;

  uint64_t pointeeSize =
      cgf.getContext().getTypeSizeInChars(linearTy->getPointeeType())
          .getQuantity();
  if (pointeeSize == 1)
    return step;
  if (pointeeSize > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
    cgf.getCIRGenModule().errorNYI(
        SourceRange(), "OpenMP linear pointer step with oversized pointee");
    return {};
  }

  mlir::Value scale =
      createOpenMPIntegerConstant(cgf, loc, cgf.getBuilder().getI32Type(),
                                  static_cast<int64_t>(pointeeSize));
  return mlir::LLVM::MulOp::create(cgf.getBuilder(), loc, step, scale);
}

static mlir::Value emitOpenMPSimdLinearStepExpr(CIRGenFunction &cgf,
                                                const Expr *e) {
  if (!e)
    return createOpenMPIntegerConstant(
        cgf, mlir::UnknownLoc::get(&cgf.getMLIRContext()),
        cgf.getBuilder().getI32Type(), 1);

  QualType ty = e->getType();
  if (!ty->isIntegerType() && !ty->isEnumeralType()) {
    cgf.getCIRGenModule().errorNYI(e->getSourceRange(),
                                   "OpenMP linear step with non-integer type");
    return {};
  }

  QualType stepTy = cgf.getContext().getIntTypeForBitwidth(
      /*DestWidth=*/32, ty->isSignedIntegerOrEnumerationType());
  mlir::Value expr = cgf.emitScalarExpr(e);
  expr = cgf.emitScalarConversion(expr, ty, stepTy, e->getExprLoc());

  mlir::Location exprLoc = cgf.getLoc(e->getBeginLoc());
  auto conversionOp = mlir::UnrealizedConversionCastOp::create(
      cgf.getBuilder(), exprLoc, cgf.getBuilder().getI32Type(), expr);
  return conversionOp.getResult(0);
}

static mlir::Type getOpenMPSimdLinearVarType(CIRGenFunction &cgf, QualType ty) {
  ty = ty.getNonReferenceType();
  if (ty->isIntegerType() || ty->isEnumeralType())
    return getOpenMPIntegerType(cgf, ty);
  if (ty->isPointerType()) {
    unsigned targetAS = cgf.getContext().getTargetAddressSpace(
        ty->getPointeeType().getAddressSpace());
    return mlir::LLVM::LLVMPointerType::get(&cgf.getMLIRContext(), targetAS);
  }

  mlir::Type cirTy = cgf.convertTypeForMem(ty);
  mlir::MLIRContext &ctx = cgf.getMLIRContext();
  if (mlir::isa<cir::FP16Type>(cirTy))
    return mlir::Float16Type::get(&ctx);
  if (mlir::isa<cir::BF16Type>(cirTy))
    return mlir::BFloat16Type::get(&ctx);
  if (mlir::isa<cir::SingleType>(cirTy))
    return mlir::Float32Type::get(&ctx);
  if (mlir::isa<cir::DoubleType>(cirTy))
    return mlir::Float64Type::get(&ctx);
  if (mlir::isa<cir::FP80Type>(cirTy))
    return mlir::Float80Type::get(&ctx);
  if (mlir::isa<cir::FP128Type>(cirTy))
    return mlir::Float128Type::get(&ctx);
  if (auto longDoubleTy = mlir::dyn_cast<cir::LongDoubleType>(cirTy)) {
    mlir::Type underlying = longDoubleTy.getUnderlying();
    if (mlir::isa<cir::DoubleType>(underlying))
      return mlir::Float64Type::get(&ctx);
    if (mlir::isa<cir::FP80Type>(underlying))
      return mlir::Float80Type::get(&ctx);
    if (mlir::isa<cir::FP128Type>(underlying))
      return mlir::Float128Type::get(&ctx);
  }

  return {};
}
static mlir::LogicalResult emitOpenMPSimdClausesForComposite(
    CIRGenFunction &cgf, mlir::omp::SimdOp simdOp,
    ArrayRef<const OMPClause *> clauses) {
  CIRGenBuilderTy &builder = cgf.getBuilder();

  for (const OMPClause *clause : clauses) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if:
      simdOp.getIfExprMutable().assign(
          emitOpenMPBoolExpr(cgf, cast<OMPIfClause>(clause)->getCondition()));
      break;
    case llvm::omp::OMPC_linear: {
      const auto *linearClause = cast<OMPLinearClause>(clause);
      if (linearClause->getModifier() != OMPC_LINEAR_val &&
          linearClause->getModifier() != OMPC_LINEAR_uval) {
        cgf.getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                       "OMPLinearClause modifier on simd "
                                       "directive");
        return mlir::failure();
      }

      mlir::Value linearStep =
          emitOpenMPSimdLinearStepExpr(cgf, linearClause->getStep());
      if (!linearStep)
        return mlir::failure();

      llvm::SmallVector<mlir::Value, 4> linearVars;
      llvm::SmallVector<mlir::Value, 4> linearStepVars;
      llvm::SmallVector<mlir::Attribute, 4> linearVarTypes;
      linearVars.reserve(linearClause->varlist_size());
      linearStepVars.reserve(linearClause->varlist_size());
      linearVarTypes.reserve(linearClause->varlist_size());

      for (const Expr *varExpr : linearClause->varlist()) {
        const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr);
        QualType linearTy = vd->getType().getNonReferenceType();
        if (!linearTy->isIntegerType() && !linearTy->isEnumeralType() &&
            !linearTy->isRealFloatingType() && !linearTy->isPointerType()) {
          cgf.getCIRGenModule().errorNYI(
              vd->getLocation(),
              "OMPLinearClause variable type on simd directive");
          return mlir::failure();
        }

        mlir::Value linearVar = cgf.emitLValue(varExpr).getPointer();
        auto pointerTy = mlir::dyn_cast<cir::PointerType>(linearVar.getType());
        if (!pointerTy) {
          cgf.getCIRGenModule().errorNYI(vd->getLocation(),
                                         "OMPLinearClause non-pointer capture");
          return mlir::failure();
        }

        mlir::Type linearVarType = getOpenMPSimdLinearVarType(cgf, linearTy);
        if (!linearVarType) {
          cgf.getCIRGenModule().errorNYI(
              vd->getLocation(),
              "OMPLinearClause variable type translation on simd directive");
          return mlir::failure();
        }

        mlir::Value adjustedLinearStep = scaleOpenMPSimdLinearStepValue(
            cgf, linearVar.getLoc(), linearStep, linearTy);
        if (!adjustedLinearStep)
          return mlir::failure();
        linearVars.push_back(linearVar);
        linearStepVars.push_back(adjustedLinearStep);
        linearVarTypes.push_back(mlir::TypeAttr::get(linearVarType));
      }

      simdOp.getLinearVarsMutable().append(linearVars);
      simdOp.getLinearStepVarsMutable().append(linearStepVars);

      llvm::SmallVector<mlir::Attribute, 4> allLinearVarTypes;
      if (auto existingTypes = simdOp.getLinearVarTypes())
        llvm::append_range(allLinearVarTypes, *existingTypes);
      llvm::append_range(allLinearVarTypes, linearVarTypes);
      simdOp.setLinearVarTypesAttr(
          mlir::ArrayAttr::get(builder.getContext(), allLinearVarTypes));
      break;
    }
    case llvm::omp::OMPC_nontemporal: {
      const auto *nontemporalClause = cast<OMPNontemporalClause>(clause);
      llvm::SmallVector<mlir::Value, 4> nontemporalVars;
      nontemporalVars.reserve(nontemporalClause->varlist_size());

      for (const Stmt *ref : nontemporalClause->private_refs()) {
        const auto *varExpr = cast<Expr>(ref);
        mlir::Value nontemporalVar = cgf.emitLValue(varExpr).getPointer();
        auto pointerTy =
            mlir::dyn_cast<cir::PointerType>(nontemporalVar.getType());
        if (!pointerTy) {
          cgf.getCIRGenModule().errorNYI(
              varExpr->getExprLoc(), "OMPNontemporalClause non-pointer capture");
          return mlir::failure();
        }

        nontemporalVars.push_back(nontemporalVar);
      }

      simdOp.getNontemporalVarsMutable().append(nontemporalVars);
      break;
    }
    case llvm::omp::OMPC_aligned: {
      const auto *alignedClause = cast<OMPAlignedClause>(clause);
      llvm::SmallVector<mlir::Value, 4> alignedVars;
      llvm::SmallVector<mlir::Attribute, 4> alignments;
      alignedVars.reserve(alignedClause->varlist_size());
      alignments.reserve(alignedClause->varlist_size());

      int64_t clauseAlignment = 0;
      if (const Expr *alignmentExpr = alignedClause->getAlignment()) {
        clauseAlignment =
            alignmentExpr->EvaluateKnownConstInt(cgf.getContext())
                .getSExtValue();
      }

      for (const Expr *varExpr : alignedClause->varlist()) {
        mlir::Value alignedVar = cgf.emitScalarExpr(varExpr);
        auto pointerTy = mlir::dyn_cast<cir::PointerType>(alignedVar.getType());
        if (!pointerTy) {
          const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr);
          cgf.getCIRGenModule().errorNYI(vd->getLocation(),
                                         "OMPAlignedClause non-pointer "
                                         "capture");
          return mlir::failure();
        }

        int64_t alignment = clauseAlignment;
        if (alignment == 0) {
          QualType pointeeTy{varExpr->getType()->getPointeeOrArrayElementType(),
                             0};
          alignment = cgf.getContext()
                          .toCharUnitsFromBits(
                              cgf.getContext().getOpenMPDefaultSimdAlign(
                                  pointeeTy))
                          .getQuantity();
        }

        alignedVars.push_back(alignedVar);
        alignments.push_back(builder.getI64IntegerAttr(alignment));
      }

      simdOp.getAlignedVarsMutable().append(alignedVars);

      llvm::SmallVector<mlir::Attribute, 4> allAlignments;
      if (auto existingAlignments = simdOp.getAlignments())
        llvm::append_range(allAlignments, *existingAlignments);
      llvm::append_range(allAlignments, alignments);
      simdOp.setAlignmentsAttr(
          mlir::ArrayAttr::get(builder.getContext(), allAlignments));
      break;
    }
    case llvm::omp::OMPC_order: {
      const auto *orderClause = cast<OMPOrderClause>(clause);
      switch (orderClause->getKind()) {
      case OMPC_ORDER_concurrent:
        simdOp.setOrderAttr(mlir::omp::ClauseOrderKindAttr::get(
            builder.getContext(), mlir::omp::ClauseOrderKind::Concurrent));
        break;
      case OMPC_ORDER_unknown:
        llvm_unreachable("unknown OpenMP order kind");
      }

      switch (orderClause->getModifier()) {
      case OMPC_ORDER_MODIFIER_unknown:
        break;
      case OMPC_ORDER_MODIFIER_reproducible:
        simdOp.setOrderModAttr(mlir::omp::OrderModifierAttr::get(
            builder.getContext(), mlir::omp::OrderModifier::reproducible));
        break;
      case OMPC_ORDER_MODIFIER_unconstrained:
        simdOp.setOrderModAttr(mlir::omp::OrderModifierAttr::get(
            builder.getContext(), mlir::omp::OrderModifier::unconstrained));
        break;
      case OMPC_ORDER_MODIFIER_last:
        llvm_unreachable("unexpected OpenMP order modifier sentinel");
      }
      break;
    }
    case llvm::omp::OMPC_safelen: {
      int64_t safelen =
          cast<OMPSafelenClause>(clause)
              ->getSafelen()
              ->EvaluateKnownConstInt(cgf.getContext())
              .getSExtValue();
      simdOp.setSafelenAttr(builder.getI64IntegerAttr(safelen));
      break;
    }
    case llvm::omp::OMPC_simdlen: {
      int64_t simdlen =
          cast<OMPSimdlenClause>(clause)
              ->getSimdlen()
              ->EvaluateKnownConstInt(cgf.getContext())
              .getSExtValue();
      simdOp.setSimdlenAttr(builder.getI64IntegerAttr(simdlen));
      break;
    }
    default:
      break;
    }
  }

  return mlir::success();
}

struct OpenMPSimdCompositeClauseData {
  mlir::Value ifExpr;
  llvm::SmallVector<mlir::Value, 4> linearVars;
  llvm::SmallVector<mlir::Value, 4> linearStepVars;
  llvm::SmallVector<mlir::Attribute, 4> linearVarTypes;
  llvm::SmallVector<mlir::Value, 4> nontemporalVars;
  llvm::SmallVector<mlir::Value, 4> alignedVars;
  llvm::SmallVector<mlir::Attribute, 4> alignments;
  mlir::omp::ClauseOrderKindAttr order;
  mlir::omp::OrderModifierAttr orderMod;
  mlir::IntegerAttr safelen;
  mlir::IntegerAttr simdlen;
};

static mlir::LogicalResult collectOpenMPSimdClauseDataForComposite(
    CIRGenFunction &cgf, ArrayRef<const OMPClause *> clauses,
    OpenMPSimdCompositeClauseData &data) {
  CIRGenBuilderTy &builder = cgf.getBuilder();

  for (const OMPClause *clause : clauses) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if:
      data.ifExpr =
          emitOpenMPBoolExpr(cgf, cast<OMPIfClause>(clause)->getCondition());
      break;
    case llvm::omp::OMPC_linear: {
      const auto *linearClause = cast<OMPLinearClause>(clause);
      if (linearClause->getModifier() != OMPC_LINEAR_val &&
          linearClause->getModifier() != OMPC_LINEAR_uval) {
        cgf.getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                       "OMPLinearClause modifier on simd "
                                       "directive");
        return mlir::failure();
      }

      mlir::Value linearStep =
          emitOpenMPSimdLinearStepExpr(cgf, linearClause->getStep());
      if (!linearStep)
        return mlir::failure();

      for (const Expr *varExpr : linearClause->varlist()) {
        const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr);
        QualType linearTy = vd->getType().getNonReferenceType();
        if (!linearTy->isIntegerType() && !linearTy->isEnumeralType() &&
            !linearTy->isRealFloatingType() && !linearTy->isPointerType()) {
          cgf.getCIRGenModule().errorNYI(
              vd->getLocation(),
              "OMPLinearClause variable type on simd directive");
          return mlir::failure();
        }

        mlir::Value linearVar = cgf.emitLValue(varExpr).getPointer();
        auto pointerTy = mlir::dyn_cast<cir::PointerType>(linearVar.getType());
        if (!pointerTy) {
          cgf.getCIRGenModule().errorNYI(vd->getLocation(),
                                         "OMPLinearClause non-pointer capture");
          return mlir::failure();
        }

        mlir::Type linearVarType = getOpenMPSimdLinearVarType(cgf, linearTy);
        if (!linearVarType) {
          cgf.getCIRGenModule().errorNYI(
              vd->getLocation(),
              "OMPLinearClause variable type translation on simd directive");
          return mlir::failure();
        }

        mlir::Value adjustedLinearStep = scaleOpenMPSimdLinearStepValue(
            cgf, linearVar.getLoc(), linearStep, linearTy);
        if (!adjustedLinearStep)
          return mlir::failure();
        data.linearVars.push_back(linearVar);
        data.linearStepVars.push_back(adjustedLinearStep);
        data.linearVarTypes.push_back(mlir::TypeAttr::get(linearVarType));
      }
      break;
    }
    case llvm::omp::OMPC_nontemporal: {
      const auto *nontemporalClause = cast<OMPNontemporalClause>(clause);
      for (const Stmt *ref : nontemporalClause->private_refs()) {
        const auto *varExpr = cast<Expr>(ref);
        mlir::Value nontemporalVar = cgf.emitLValue(varExpr).getPointer();
        auto pointerTy =
            mlir::dyn_cast<cir::PointerType>(nontemporalVar.getType());
        if (!pointerTy) {
          cgf.getCIRGenModule().errorNYI(
              varExpr->getExprLoc(), "OMPNontemporalClause non-pointer capture");
          return mlir::failure();
        }
        data.nontemporalVars.push_back(nontemporalVar);
      }
      break;
    }
    case llvm::omp::OMPC_aligned: {
      const auto *alignedClause = cast<OMPAlignedClause>(clause);
      int64_t clauseAlignment = 0;
      if (const Expr *alignmentExpr = alignedClause->getAlignment()) {
        clauseAlignment =
            alignmentExpr->EvaluateKnownConstInt(cgf.getContext())
                .getSExtValue();
      }

      for (const Expr *varExpr : alignedClause->varlist()) {
        mlir::Value alignedVar = cgf.emitScalarExpr(varExpr);
        auto pointerTy = mlir::dyn_cast<cir::PointerType>(alignedVar.getType());
        if (!pointerTy) {
          const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr);
          cgf.getCIRGenModule().errorNYI(vd->getLocation(),
                                         "OMPAlignedClause non-pointer "
                                         "capture");
          return mlir::failure();
        }

        int64_t alignment = clauseAlignment;
        if (alignment == 0) {
          QualType pointeeTy{varExpr->getType()->getPointeeOrArrayElementType(),
                             0};
          alignment = cgf.getContext()
                          .toCharUnitsFromBits(
                              cgf.getContext().getOpenMPDefaultSimdAlign(
                                  pointeeTy))
                          .getQuantity();
        }

        data.alignedVars.push_back(alignedVar);
        data.alignments.push_back(builder.getI64IntegerAttr(alignment));
      }
      break;
    }
    case llvm::omp::OMPC_order: {
      const auto *orderClause = cast<OMPOrderClause>(clause);
      switch (orderClause->getKind()) {
      case OMPC_ORDER_concurrent:
        data.order = mlir::omp::ClauseOrderKindAttr::get(
            builder.getContext(), mlir::omp::ClauseOrderKind::Concurrent);
        break;
      case OMPC_ORDER_unknown:
        llvm_unreachable("unknown OpenMP order kind");
      }

      switch (orderClause->getModifier()) {
      case OMPC_ORDER_MODIFIER_unknown:
        break;
      case OMPC_ORDER_MODIFIER_reproducible:
        data.orderMod = mlir::omp::OrderModifierAttr::get(
            builder.getContext(), mlir::omp::OrderModifier::reproducible);
        break;
      case OMPC_ORDER_MODIFIER_unconstrained:
        data.orderMod = mlir::omp::OrderModifierAttr::get(
            builder.getContext(), mlir::omp::OrderModifier::unconstrained);
        break;
      case OMPC_ORDER_MODIFIER_last:
        llvm_unreachable("unexpected OpenMP order modifier sentinel");
      }
      break;
    }
    case llvm::omp::OMPC_safelen: {
      int64_t safelen =
          cast<OMPSafelenClause>(clause)
              ->getSafelen()
              ->EvaluateKnownConstInt(cgf.getContext())
              .getSExtValue();
      data.safelen = builder.getI64IntegerAttr(safelen);
      break;
    }
    case llvm::omp::OMPC_simdlen: {
      int64_t simdlen =
          cast<OMPSimdlenClause>(clause)
              ->getSimdlen()
              ->EvaluateKnownConstInt(cgf.getContext())
              .getSExtValue();
      data.simdlen = builder.getI64IntegerAttr(simdlen);
      break;
    }
    default:
      break;
    }
  }

  return mlir::success();
}

static bool isSimpleOpenMPSimdClauseKind(llvm::omp::Clause clauseKind) {
  switch (clauseKind) {
  case llvm::omp::OMPC_aligned:
  case llvm::omp::OMPC_if:
  case llvm::omp::OMPC_linear:
  case llvm::omp::OMPC_lastprivate:
  case llvm::omp::OMPC_nontemporal:
  case llvm::omp::OMPC_order:
  case llvm::omp::OMPC_private:
  case llvm::omp::OMPC_reduction:
  case llvm::omp::OMPC_collapse:
  case llvm::omp::OMPC_safelen:
  case llvm::omp::OMPC_simdlen:
    return true;
  default:
    return false;
  }
}

static void materializeOpenMPCapturedClauseExpr(
    CIRGenFunction &cgf, const Expr *expr,
    llvm::SmallPtrSetImpl<const VarDecl *> &materializedVars) {
  const VarDecl *vd = getOpenMPVarDeclFromExpr(expr);
  if (!vd)
    return;
  const VarDecl *canonicalVD = vd->getCanonicalDecl();

  const auto *capturedExpr = dyn_cast<OMPCapturedExprDecl>(vd);
  if (!capturedExpr)
    capturedExpr = dyn_cast<OMPCapturedExprDecl>(canonicalVD);
  if (!capturedExpr || !materializedVars.insert(canonicalVD).second)
    return;

  const Expr *init = capturedExpr->getInit();
  if (!capturedExpr->getType()->isReferenceType()) {
    Address addr = cgf.emitLValue(expr).getAddress();
    cgf.emitAnyExprToMem(init, addr, init->getType().getQualifiers(),
                         /*isInitializer=*/true);
    return;
  }

  Address addr = Address::invalid();
  if (auto it = cgf.localDeclMap.find(vd); it != cgf.localDeclMap.end()) {
    addr = it->second;
  } else if (vd != canonicalVD) {
    if (auto it = cgf.localDeclMap.find(canonicalVD);
        it != cgf.localDeclMap.end())
      addr = it->second;
  }
  if (!addr.isValid()) {
    mlir::Location loc = cgf.getLoc(capturedExpr->getLocation());
    QualType capturedTy = capturedExpr->getType();
    addr = cgf.createTempAlloca(
        cgf.convertTypeForMem(capturedTy),
        cgf.getContext().getDeclAlign(capturedExpr), loc,
        capturedExpr->getName(), /*arraySize=*/nullptr, /*alloca=*/nullptr,
        cgf.getBuilder().saveInsertionPoint());
    cgf.replaceAddrOfLocalVar(canonicalVD, addr);
    if (vd != canonicalVD)
      cgf.replaceAddrOfLocalVar(vd, addr);
  }
  // Initialize the captured-decl storage itself so reference captures bind to
  // the original object instead of storing through the referenced lvalue.
  cgf.emitExprAsInit(init, capturedExpr,
                     cgf.makeAddrLValue(addr, capturedExpr->getType()),
                     /*capturedByInit=*/false);
}

static void materializeOpenMPMapClauseExprs(
    CIRGenFunction &cgf, const Expr *expr,
    llvm::SmallPtrSetImpl<const VarDecl *> &materializedVars) {
  expr = expr->IgnoreParenImpCasts();
  while (expr) {
    materializeOpenMPCapturedClauseExpr(cgf, expr, materializedVars);

    if (const auto *section = dyn_cast<ArraySectionExpr>(expr)) {
      if (const Expr *lowerBound = section->getLowerBound())
        materializeOpenMPCapturedClauseExpr(cgf, lowerBound, materializedVars);
      if (const Expr *length = section->getLength())
        materializeOpenMPCapturedClauseExpr(cgf, length, materializedVars);
      expr = section->getBase()->IgnoreParenImpCasts();
      continue;
    }

    if (const auto *subscript = dyn_cast<ArraySubscriptExpr>(expr)) {
      materializeOpenMPCapturedClauseExpr(cgf, subscript->getIdx(),
                                          materializedVars);
      expr = subscript->getBase()->IgnoreParenImpCasts();
      continue;
    }

    break;
  }
}

static void rematerializeOpenMPCapturedExprTree(
    CIRGenFunction &cgf, const Expr *expr,
    llvm::SmallPtrSetImpl<const VarDecl *> &materializedVars,
    llvm::SmallVectorImpl<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>>
        &declGuards);

static void rematerializeOpenMPCapturedExpr(
    CIRGenFunction &cgf, const Expr *expr,
    llvm::SmallPtrSetImpl<const VarDecl *> &materializedVars,
    llvm::SmallVectorImpl<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>>
        &declGuards) {
  const VarDecl *capturedVD = getOpenMPVarDeclFromExpr(expr);
  if (!capturedVD)
    return;
  const VarDecl *canonicalVD = capturedVD->getCanonicalDecl();

  const auto *capturedExpr = dyn_cast<OMPCapturedExprDecl>(capturedVD);
  if (!capturedExpr)
    capturedExpr = dyn_cast<OMPCapturedExprDecl>(canonicalVD);
  if (!capturedExpr || !materializedVars.insert(canonicalVD).second)
    return;

  declGuards.push_back(
      std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(cgf, canonicalVD));
  cgf.eraseAddrOfLocalVar(canonicalVD);
  if (capturedVD != canonicalVD) {
    declGuards.push_back(std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(
        cgf, capturedVD));
    cgf.eraseAddrOfLocalVar(capturedVD);
  }
  mlir::Location loc = cgf.getLoc(capturedExpr->getLocation());
  QualType capturedTy = capturedExpr->getType();
  Address capturedAddr = cgf.createTempAlloca(
      cgf.convertTypeForMem(capturedTy), cgf.getContext().getDeclAlign(capturedExpr),
      loc, capturedExpr->getName(),
      /*arraySize=*/nullptr, /*alloca=*/nullptr,
      cgf.getBuilder().saveInsertionPoint());
  cgf.replaceAddrOfLocalVar(canonicalVD, capturedAddr);
  const Expr *init = capturedExpr->getInit();
  // Captured clause exprs are frequently synthesized as a decl that references
  // other synthetic captured expr decls in its initializer. Rematerialize that
  // hidden init tree first so evaluating the root init inside a target region
  // does not reach back to values defined outside the region.
  rematerializeOpenMPCapturedExprTree(cgf, init, materializedVars, declGuards);
  if (capturedTy->isReferenceType()) {
    cgf.emitExprAsInit(init, capturedExpr,
                       cgf.makeAddrLValue(capturedAddr, capturedTy),
                       /*capturedByInit=*/false);
  } else {
    cgf.emitAnyExprToMem(init, capturedAddr, init->getType().getQualifiers(),
                         /*isInitializer=*/true);
  }
  if (capturedVD != canonicalVD) {
    cgf.replaceAddrOfLocalVar(capturedVD, capturedAddr);
  }
}

static void rematerializeOpenMPCapturedExprTree(
    CIRGenFunction &cgf, const Expr *expr,
    llvm::SmallPtrSetImpl<const VarDecl *> &materializedVars,
    llvm::SmallVectorImpl<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>>
        &declGuards) {
  if (!expr)
    return;

  expr = expr->IgnoreParenImpCasts();
  rematerializeOpenMPCapturedExpr(cgf, expr, materializedVars, declGuards);
  for (const Stmt *child : expr->children())
    if (const auto *childExpr = dyn_cast_or_null<Expr>(child))
      rematerializeOpenMPCapturedExprTree(cgf, childExpr, materializedVars,
                                          declGuards);
}

static void rematerializeOpenMPMapClauseExprs(
    CIRGenFunction &cgf, const Expr *expr,
    llvm::SmallPtrSetImpl<const VarDecl *> &materializedVars,
    llvm::SmallVectorImpl<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>>
        &declGuards) {
  expr = expr->IgnoreParenImpCasts();
  while (expr) {
    rematerializeOpenMPCapturedExpr(cgf, expr, materializedVars, declGuards);

    if (const auto *section = dyn_cast<ArraySectionExpr>(expr)) {
      if (const Expr *lowerBound = section->getLowerBound())
        rematerializeOpenMPCapturedExpr(cgf, lowerBound, materializedVars,
                                        declGuards);
      if (const Expr *length = section->getLength())
        rematerializeOpenMPCapturedExpr(cgf, length, materializedVars,
                                        declGuards);
      expr = section->getBase()->IgnoreParenImpCasts();
      continue;
    }

    if (const auto *subscript = dyn_cast<ArraySubscriptExpr>(expr)) {
      rematerializeOpenMPCapturedExpr(cgf, subscript->getIdx(),
                                      materializedVars, declGuards);
      expr = subscript->getBase()->IgnoreParenImpCasts();
      continue;
    }

    break;
  }
}

static void rematerializeOpenMPClauseExprs(
    CIRGenFunction &cgf, ArrayRef<const OMPClause *> clauses,
    llvm::SmallPtrSetImpl<const VarDecl *> &materializedVars,
    llvm::SmallVectorImpl<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>>
        &declGuards) {
  for (const OMPClause *clause : clauses) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if:
      rematerializeOpenMPCapturedExprTree(
          cgf, cast<OMPIfClause>(clause)->getCondition(), materializedVars,
          declGuards);
      break;
    case llvm::omp::OMPC_device:
      rematerializeOpenMPCapturedExprTree(
          cgf, cast<OMPDeviceClause>(clause)->getDevice(), materializedVars,
          declGuards);
      break;
    case llvm::omp::OMPC_map:
      for (const Expr *varExpr : cast<OMPMapClause>(clause)->varlist())
        rematerializeOpenMPMapClauseExprs(cgf, varExpr, materializedVars,
                                          declGuards);
      break;
    case llvm::omp::OMPC_reduction:
      for (const Expr *varExpr : cast<OMPReductionClause>(clause)->varlist())
        rematerializeOpenMPMapClauseExprs(cgf, varExpr, materializedVars,
                                          declGuards);
      break;
    case llvm::omp::OMPC_num_threads:
      rematerializeOpenMPCapturedExprTree(
          cgf, cast<OMPNumThreadsClause>(clause)->getNumThreads(),
          materializedVars, declGuards);
      break;
    case llvm::omp::OMPC_message:
      rematerializeOpenMPCapturedExprTree(
          cgf, cast<OMPMessageClause>(clause)->getMessageString(),
          materializedVars, declGuards);
      break;
    case llvm::omp::OMPC_thread_limit:
      rematerializeOpenMPCapturedExprTree(
          cgf, cast<OMPThreadLimitClause>(clause)->getThreadLimit().front(),
          materializedVars, declGuards);
      break;
    case llvm::omp::OMPC_schedule: {
      const auto *scheduleClause = cast<OMPScheduleClause>(clause);
      if (const Expr *chunk = scheduleClause->getChunkSize())
        rematerializeOpenMPCapturedExprTree(cgf, chunk, materializedVars,
                                            declGuards);
      break;
    }
    case llvm::omp::OMPC_linear:
      rematerializeOpenMPCapturedExprTree(
          cgf, cast<OMPLinearClause>(clause)->getStep(), materializedVars,
          declGuards);
      break;
    default:
      break;
    }
  }
}

static mlir::LogicalResult emitOpenMPTransformPreInits(CIRGenFunction &cgf,
                                                       const Stmt *preInits) {
  if (!preInits)
    return mlir::success();

  llvm::SmallVector<const Stmt *> preInitStmts;
  if (const auto *compound = dyn_cast<CompoundStmt>(preInits))
    llvm::append_range(preInitStmts, compound->body());
  else
    preInitStmts.push_back(preInits);

  for (const Stmt *stmt : preInitStmts) {
    if (const auto *declStmt = dyn_cast<DeclStmt>(stmt)) {
      for (const Decl *decl : declStmt->decls()) {
        if (const auto *vd = dyn_cast<VarDecl>(decl)) {
          // Match OGCG here so OpenMP synthetic OMPCapturedExprDecl preinits
          // are materialized through the normal local VarDecl path.
          cgf.emitVarDecl(*vd);
          cgf.maybeEmitDeferredVarDeclInit(vd);
          continue;
        }
        cgf.emitDecl(*decl, /*evaluateConditionDecl=*/true);
      }
      continue;
    }

    if (mlir::failed(cgf.emitStmt(stmt, /*useCurrentScope=*/true)))
      return mlir::failure();
  }

  return mlir::success();
}

static mlir::DictionaryAttr
createOpenMPUnrollLoopHint(CIRGenFunction &cgf, const OMPUnrollDirective &s) {
  llvm::SmallVector<mlir::NamedAttribute> entries;
  auto &builder = cgf.getBuilder();
  auto &cgm = cgf.getCIRGenModule();

  entries.push_back(
      {cgm.getStringAttrCached("mustprogress"), cgm.getBoolAttrCached(true)});

  // OGCG always sets LoopAttributes::Enable first, then conditionally
  // adds the count or full flag.  Mirror that: always emit unroll_enable,
  // then add unroll_count or unroll_full where appropriate.
  entries.push_back(
      {cgm.getStringAttrCached("unroll_enable"), cgm.getBoolAttrCached(true)});

  if (s.hasClausesOfKind<OMPFullClause>()) {
    entries.push_back(
        {cgm.getStringAttrCached("unroll_full"), cgm.getBoolAttrCached(true)});
  } else if (const auto *partialClause = s.getSingleClause<OMPPartialClause>()) {
    if (const Expr *factorExpr = partialClause->getFactor()) {
      uint64_t factor =
          factorExpr->EvaluateKnownConstInt(cgf.getContext()).getZExtValue();
      entries.push_back({cgm.getStringAttrCached("unroll_count"),
                         cgf.getI32AttrCached(factor)});
    }
  }

  entries.push_back({cgm.getStringAttrCached("loop_start_loc"),
                     cgf.getLoc(s.getBeginLoc())});
  entries.push_back(
      {cgm.getStringAttrCached("loop_end_loc"), cgf.getLoc(s.getEndLoc())});
  return mlir::DictionaryAttr::get(builder.getContext(), entries);
}

static std::string getOpenMPPrivatizerBaseName(CIRGenFunction &cgf,
                                               const VarDecl *vd,
                                               llvm::StringRef prefix) {
  std::string base = (prefix + vd->getName()).str();
  const SourceManager &sm = cgf.getContext().getSourceManager();
  PresumedLoc ploc = sm.getPresumedLoc(vd->getLocation());
  if (ploc.isValid())
    base += ("_" + llvm::Twine(ploc.getLine()) + "_" +
             llvm::Twine(ploc.getColumn()))
                .str();
  return base;
}

static mlir::omp::PrivateClauseOp getOrCreateOpenMPPrivatePrivatizer(
    CIRGenFunction &cgf, CIRGenBuilderTy &builder, mlir::Location loc,
    const VarDecl *vd, mlir::Type valueTy) {
  mlir::ModuleOp module = cgf.getCIRGenModule().getModule();
  std::string baseName =
      getOpenMPPrivatizerBaseName(cgf, vd, "__omp_private_");
  std::string name = baseName;
  unsigned suffix = 0;
  while (module.lookupSymbol(name))
    name = (baseName + "." + llvm::Twine(++suffix)).str();

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());
  return mlir::omp::PrivateClauseOp::create(
      builder, loc, name, valueTy, mlir::omp::DataSharingClauseType::Private);
}

static void emitOpenMPLastprivateCopy(CIRGenFunction &cgf, const VarDecl *vd,
                                      Address srcAddr, Address destAddr,
                                      const VarDecl *srcVD,
                                      const VarDecl *destVD,
                                      const Expr *assignmentOp) {
  QualType varTy = vd->getType();
  mlir::Location loc = cgf.getLoc(vd->getLocation());

  if (varTy->isScalarType() || varTy->isReferenceType()) {
    mlir::Value value = cgf.getBuilder().createLoad(loc, srcAddr);
    cgf.emitStoreOfScalar(value, cgf.makeAddrLValue(destAddr, varTy),
                          /*isInit=*/false);
    return;
  }

  // For aggregate types, use the assignment expression from the clause.
  // This correctly handles non-trivially-copyable types by calling the
  // copy assignment operator instead of memcpy. That copy-assign can throw and
  // its unwind edge lands on the original variable's dtor cleanup outside the
  // region; suppress the escaping-exception gate for this internal copy-back.
  CIRGenFunction::OpenMPRegionDataSharingCopyRAII copyGuard(cgf);
  CIRGenFunction::SymTableScopeTy scope(cgf.symbolTable);
  cgf.setAddrOfLocalVar(srcVD, srcAddr);
  cgf.setAddrOfLocalVar(destVD, destAddr);
  cgf.emitIgnoredExpr(assignmentOp);
}

using OpenMPCopyinMasterAddr = std::pair<const VarDecl *, Address>;

static std::optional<Address> lookupOpenMPAddressableDeclAddress(
    CIRGenFunction &cgf, const VarDecl *vd,
    mlir::Operation *insertBeforeOp = nullptr) {
  const VarDecl *canonicalVD = vd->getCanonicalDecl();

  auto lookupLocalDeclAddress = [&](const VarDecl *lookupVD)
      -> std::optional<Address> {
    auto it = cgf.localDeclMap.find(lookupVD);
    if (it != cgf.localDeclMap.end())
      return it->second;
    return std::nullopt;
  };

  if (std::optional<Address> localAddr = lookupLocalDeclAddress(vd))
    return localAddr;
  if (canonicalVD != vd)
    if (std::optional<Address> canonicalAddr =
            lookupLocalDeclAddress(canonicalVD))
      return canonicalAddr;

  if (!canonicalVD->hasGlobalStorage())
    return std::nullopt;

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::OpBuilder::InsertionGuard guard(builder);
  if (insertBeforeOp)
    builder.setInsertionPoint(insertBeforeOp);
  mlir::Type valueTy = cgf.convertTypeForMem(canonicalVD->getType());
  return Address(cgf.getCIRGenModule().getAddrOfGlobalVar(canonicalVD, valueTy),
                 valueTy, cgf.getContext().getDeclAlign(canonicalVD));
}

struct OpenMPCopyinItem {
  const Expr *varExpr = nullptr;
  const VarDecl *canonicalVD = nullptr;
  const VarDecl *srcVD = nullptr;
  const VarDecl *destVD = nullptr;
  const Expr *assignmentOp = nullptr;
  QualType type;
  Address masterAddr = Address::invalid();
};

static Address getOpenMPCopyinMasterAddress(CIRGenFunction &cgf,
                                            const Expr *varExpr,
                                            const VarDecl *vd) {
  vd = vd->getCanonicalDecl();
  if (cgf.getLangOpts().OpenMPUseTLS &&
      cgf.getContext().getTargetInfo().isTLSSupported())
    return cgf.emitLValue(varExpr).getAddress();

  mlir::Type valueTy = cgf.convertTypeForMem(vd->getType());
  return Address(cgf.getCIRGenModule().getAddrOfGlobalVar(vd, valueTy), valueTy,
                 cgf.getContext().getDeclAlign(vd));
}

static llvm::SmallVector<OpenMPCopyinMasterAddr, 4>
collectOpenMPCopyinMasterAddresses(CIRGenFunction &cgf,
                                   const OMPExecutableDirective &s) {
  llvm::SmallPtrSet<const VarDecl *, 4> seenVars;
  llvm::SmallVector<OpenMPCopyinMasterAddr, 4> masterAddrs;

  for (const auto *clause : s.getClausesOfKind<OMPCopyinClause>()) {
    for (const Expr *varExpr : clause->varlist()) {
      const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr);
      if (!vd) {
        cgf.getCIRGenModule().errorNYI(
            varExpr->getSourceRange(),
            "OpenMP copyin clause expression without an addressable variable");
        continue;
      }

      vd = vd->getCanonicalDecl();
      if (!seenVars.insert(vd).second)
        continue;

      masterAddrs.emplace_back(vd,
                               getOpenMPCopyinMasterAddress(cgf, varExpr, vd));
    }
  }

  return masterAddrs;
}

class OpenMPTLSCopyinDeclMapScope {
  llvm::SmallVector<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>, 4>
      declGuards;

public:
  OpenMPTLSCopyinDeclMapScope(CIRGenFunction &cgf,
                              const OMPExecutableDirective &s) {
    if (!cgf.getLangOpts().OpenMPUseTLS ||
        !cgf.getContext().getTargetInfo().isTLSSupported())
      return;

    llvm::SmallPtrSet<const VarDecl *, 4> seenVars;
    for (const auto *clause : s.getClausesOfKind<OMPCopyinClause>()) {
      for (const Expr *varExpr : clause->varlist()) {
        const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr);
        if (!vd) {
          cgf.getCIRGenModule().errorNYI(
              varExpr->getSourceRange(),
              "OpenMP copyin clause expression without an addressable "
              "variable");
          continue;
        }

        vd = vd->getCanonicalDecl();
        if (!seenVars.insert(vd).second ||
            !vd->hasAttr<OMPThreadPrivateDeclAttr>())
          continue;

        // Match OGCG's LocalDeclMap.erase(VD) for TLS copyin variables so
        // region uses rematerialize the current thread's TLS address instead
        // of reusing a cached master-thread mapping from localDeclMap.
        declGuards.push_back(
            std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(cgf, vd));
        cgf.eraseAddrOfLocalVar(vd);
      }
    }
  }
};

static constexpr llvm::StringLiteral kOpenMPTaskReductionSizeSymAttr =
    "omp.frontend.task_reduction_size_sym";

// The op may still sit in a region under construction, which does not reach
// the module's symbol table yet.
static mlir::FlatSymbolRefAttr
lookupOpenMPTaskReductionSizeSym(CIRGenFunction &cgf,
                                 mlir::SymbolRefAttr reductionSym) {
  auto reductionDecl =
      cgf.getCIRGenModule()
          .getModule()
          .lookupSymbol<mlir::omp::DeclareReductionOp>(reductionSym);
  if (!reductionDecl)
    return {};
  return reductionDecl->getAttrOfType<mlir::FlatSymbolRefAttr>(
      kOpenMPTaskReductionSizeSymAttr);
}

static Address
getOpenMPTaskReductionSizeAddr(CIRGenFunction &cgf, mlir::Location loc,
                               mlir::FlatSymbolRefAttr sizeSym) {
  auto sizePtrTy = cgf.getBuilder().getPointerTo(cgf.sizeTy);
  mlir::Value sizeGlobal = cir::GetGlobalOp::create(
      cgf.getBuilder(), loc, sizePtrTy, sizeSym.getLeafReference());
  mlir::Value threadprivateAddr = mlir::omp::ThreadprivateOp::create(
      cgf.getBuilder(), loc, sizePtrTy, sizeGlobal);
  CharUnits sizeAlign =
      cgf.getContext().getTypeAlignInChars(cgf.getContext().getSizeType());
  return Address(threadprivateAddr, cgf.sizeTy, sizeAlign);
}

static mlir::Value getOpenMPTaskReductionRuntimeElementCount(
    CIRGenFunction &cgf, mlir::Location loc, QualType reductionTy) {
  if (reductionTy->isVariablyModifiedType())
    cgf.emitVariablyModifiedType(reductionTy);

  mlir::Value totalCount;
  QualType curTy = reductionTy;
  bool sawArray = false;
  while (const auto *arrTy = cgf.getContext().getAsArrayType(curTy)) {
    sawArray = true;
    mlir::Value dimCount;
    if (const auto *constArrTy = dyn_cast<ConstantArrayType>(arrTy)) {
      dimCount = cgf.getBuilder().getConstInt(
          loc, mlir::cast<cir::IntType>(cgf.sizeTy), constArrTy->getZExtSize());
    } else if (const auto *vlaTy = dyn_cast<VariableArrayType>(arrTy)) {
      dimCount = cgf.getVLAElements1D(vlaTy).numElts;
      if (dimCount.getType() != cgf.sizeTy)
        dimCount = cgf.getBuilder().createIntCast(dimCount, cgf.sizeTy);
    } else {
      llvm_unreachable(
          "OpenMP task reduction array type with unsupported extent kind");
    }

    totalCount =
        totalCount
            ? cgf.getBuilder().createMul(loc, totalCount, dimCount,
                                         cir::OverflowBehavior::NoUnsignedWrap)
            : dimCount;
    curTy = arrTy->getElementType();
  }

  assert(sawArray && "expected OpenMP task reduction array type");
  return totalCount;
}

struct OpenMPTaskReductionVLASizeScope {
  mlir::Value runtimeSize;
  std::unique_ptr<CIRGenFunction::VLASizeMapping> sizeMapping;

  OpenMPTaskReductionVLASizeScope(CIRGenFunction &cgf, const VarDecl *privateVD,
                                  const VarDecl *remapVD,
                                  mlir::FlatSymbolRefAttr sizeSym,
                                  mlir::Location loc,
                                  mlir::Value runtimeSize)
      : runtimeSize(runtimeSize) {
    if (this->runtimeSize.getType() != cgf.sizeTy)
      this->runtimeSize =
          cgf.getBuilder().createIntCast(this->runtimeSize, cgf.sizeTy);
    if (remapVD) {
      if (const auto *vlaTy = cgf.getContext().getAsVariableArrayType(
              remapVD->getType().getNonReferenceType())) {
        sizeMapping = std::make_unique<CIRGenFunction::VLASizeMapping>(
            cgf, vlaTy->getSizeExpr(), this->runtimeSize);
      }
    }
    cgf.getBuilder().createStore(
        loc, this->runtimeSize,
        getOpenMPTaskReductionSizeAddr(cgf, loc, sizeSym));
  }

  OpenMPTaskReductionVLASizeScope(CIRGenFunction &cgf, const VarDecl *privateVD,
                                  const VarDecl *remapVD,
                                  mlir::FlatSymbolRefAttr sizeSym,
                                  mlir::Location loc)
      : OpenMPTaskReductionVLASizeScope(
            cgf, privateVD, remapVD, sizeSym, loc,
            getOpenMPTaskReductionRuntimeElementCount(
                cgf, loc, privateVD->getType().getNonReferenceType())) {}
};

static void emitOpenMPTaskReductionArraySectionRuntimeSizeScopes(
    CIRGenFunction &cgf, mlir::Operation *op,
    ArrayRef<CIRGenFunction::OpenMPTaskReductionRuntimeSizeInfo> runtimeInfos,
    std::optional<mlir::ArrayAttr> reductionSyms,
    llvm::SmallVectorImpl<std::unique_ptr<OpenMPTaskReductionVLASizeScope>>
        &vlaSizeScopes) {
  if (!reductionSyms)
    return;

  for (const auto &info : runtimeInfos) {
    if (info.remapVar || !info.runtimeElementCount)
      continue;
    if (info.index >= reductionSyms->size())
      continue;

    auto reductionSym =
        mlir::dyn_cast<mlir::SymbolRefAttr>((*reductionSyms)[info.index]);
    if (!reductionSym)
      continue;
    mlir::FlatSymbolRefAttr sizeSym =
        lookupOpenMPTaskReductionSizeSym(cgf, reductionSym);
    if (!sizeSym)
      continue;

    vlaSizeScopes.push_back(std::make_unique<OpenMPTaskReductionVLASizeScope>(
        cgf, info.privateVar, info.remapVar, sizeSym,
        cgf.getLoc(info.privateVar->getLocation()),
        info.runtimeElementCount));
  }
}

static void remapOpenMPReductionDecls(
    CIRGenFunction &cgf, mlir::Operation *op,
    const CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo,
    llvm::SmallVectorImpl<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>>
        &declGuards,
    std::optional<mlir::ArrayAttr> reductionSyms = std::nullopt,
    llvm::SmallVectorImpl<std::unique_ptr<OpenMPTaskReductionVLASizeScope>>
        *vlaSizeScopes = nullptr) {
  auto reductionArgs =
      mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(op)
          .getReductionBlockArgs();
  assert(clauseInfo.reductionIndices.size() == clauseInfo.reductionVars.size() &&
         "mismatched OpenMP reduction metadata");
  for (auto [index, vd] :
       llvm::zip_equal(clauseInfo.reductionIndices, clauseInfo.reductionVars)) {
    assert(index < reductionArgs.size() &&
           "unexpected OpenMP reduction block arg index");
    mlir::Value reductionArg = reductionArgs[index];
    auto ptrTy = mlir::cast<cir::PointerType>(reductionArg.getType());
    declGuards.push_back(
        std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(cgf, vd));
    cgf.replaceAddrOfLocalVar(
        vd, Address(reductionArg, ptrTy.getPointee(),
                    cgf.getContext().getDeclAlign(vd)));
    if (!vlaSizeScopes || !reductionSyms || index >= reductionSyms->size())
      continue;
    const auto *vlaTy =
        cgf.getContext().getAsVariableArrayType(vd->getType().getNonReferenceType());
    if (!vlaTy)
      continue;
    auto reductionSym =
        mlir::dyn_cast<mlir::SymbolRefAttr>((*reductionSyms)[index]);
    if (!reductionSym)
      continue;
    mlir::FlatSymbolRefAttr sizeSym =
        lookupOpenMPTaskReductionSizeSym(cgf, reductionSym);
    if (!sizeSym)
      continue;
    vlaSizeScopes->push_back(std::make_unique<OpenMPTaskReductionVLASizeScope>(
        cgf, vd, vd, sizeSym, cgf.getLoc(vd->getLocation())));
  }

  // Point each array-section base at the private copy, shifted back by the
  // section's offset so that indexing the base lands in the private storage.
  CIRGenBuilderTy &builder = cgf.getBuilder();
  llvm::SmallPtrSet<const VarDecl *, 4> remappedBases;
  for (const auto &section : clauseInfo.reductionSections) {
    assert(section.index < reductionArgs.size() &&
           "unexpected OpenMP reduction block arg index");
    const VarDecl *vd = section.baseVar;
    if (!remappedBases.insert(vd).second)
      continue;
    mlir::Location loc = cgf.getLoc(vd->getLocation());
    mlir::Value privateBase = builder.createPtrStride(
        loc, reductionArgs[section.index], builder.createNeg(section.offset),
        cir::GEPNoWrapFlags::none);
    QualType baseTy = vd->getType().getNonReferenceType();
    CharUnits align = cgf.getContext().getDeclAlign(vd);
    declGuards.push_back(
        std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(cgf, vd));
    if (baseTy->isPointerType()) {
      // The slot must be per-thread but outside the loop: in front of the
      // outermost loop wrapper. A task has no per-task place outside its body,
      // so taskloop keeps the shared base.
      if (mlir::isa<mlir::omp::TaskloopOp>(op)) {
        declGuards.pop_back();
        continue;
      }
      mlir::Operation *slotPoint = op;
      while (mlir::isa_and_nonnull<mlir::omp::LoopWrapperInterface>(
          slotPoint->getParentOp()))
        slotPoint = slotPoint->getParentOp();
      mlir::Type ptrTy = cgf.convertTypeForMem(baseTy);
      mlir::Value slotPtr;
      {
        mlir::OpBuilder::InsertionGuard guard(builder);
        if (mlir::isa<mlir::omp::LoopWrapperInterface>(slotPoint))
          builder.setInsertionPoint(slotPoint);
        slotPtr = builder.createAlloca(loc, builder.getPointerTo(ptrTy), ptrTy,
                                       "omp.reduction.section.base", align);
      }
      Address slot(slotPtr, ptrTy, align);
      builder.createStore(loc, builder.createBitcast(loc, privateBase, ptrTy),
                          slot);
      cgf.replaceAddrOfLocalVar(vd, slot);
      continue;
    }
    mlir::Type memTy = cgf.convertTypeForMem(baseTy);
    cgf.replaceAddrOfLocalVar(
        vd, Address(builder.createBitcast(loc, privateBase,
                                          builder.getPointerTo(memTy)),
                    memTy, align));
  }
}

static const CIRGenFunction::OpenMPClauseEmissionInfo::
    OpenMPExplicitPrivateAllocatorInfo *
lookupOpenMPExplicitPrivateAllocatorInfo(
    const CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo,
    const VarDecl *vd);

static std::optional<Address> emitOpenMPExplicitPrivateAllocatorAddress(
    CIRGenFunction &cgf, const VarDecl *vd, const VarDecl *privateCopyVD);

static mlir::LogicalResult emitOpenMPFirstprivateCopyToAddress(
    CIRGenFunction &cgf, const VarDecl *vd, Address fromAddr, Address toAddr,
    const VarDecl *privateCopyVD, const VarDecl *initVD);

static mlir::LogicalResult remapOpenMPPrivateDecls(
    CIRGenFunction &cgf, mlir::Operation *op,
    const CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo,
    llvm::SmallVectorImpl<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>>
        &declGuards,
    llvm::DenseMap<const VarDecl *, Address> *originalDecls = nullptr) {
  auto privateArgs =
      mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(op).getPrivateBlockArgs();
  assert(clauseInfo.privateIndices.size() == clauseInfo.privateVars.size() &&
         "mismatched OpenMP private metadata");
  for (auto [index, vd] :
       llvm::zip_equal(clauseInfo.privateIndices, clauseInfo.privateVars)) {
    assert(index < privateArgs.size() &&
           "unexpected OpenMP private block arg index");
    mlir::Value privateArg = privateArgs[index];
    auto ptrTy = mlir::cast<cir::PointerType>(privateArg.getType());
    Address privateAddr(privateArg, ptrTy.getPointee(),
                        cgf.getContext().getDeclAlign(vd));
    std::optional<Address> originalAddr =
        lookupOpenMPAddressableDeclAddress(cgf, vd, op);
    if (originalDecls && originalAddr)
      originalDecls->insert({vd->getCanonicalDecl(), *originalAddr});

    if (const auto *allocatorInfo =
            lookupOpenMPExplicitPrivateAllocatorInfo(clauseInfo, vd)) {
      std::optional<Address> allocatedAddr = emitOpenMPExplicitPrivateAllocatorAddress(
          cgf, vd, allocatorInfo->privateCopyVD);
      if (!allocatedAddr)
        return mlir::failure();

      if (allocatorInfo->initializeFromOriginal) {
        if (!originalAddr) {
          cgf.getCIRGenModule().errorNYI(
              vd->getLocation(),
              "OpenMP aligned private allocator without original address");
          return mlir::failure();
        }
        if (mlir::failed(emitOpenMPFirstprivateCopyToAddress(
                cgf, vd, *originalAddr, *allocatedAddr,
                allocatorInfo->privateCopyVD, allocatorInfo->initVD)))
          return mlir::failure();
      }

      privateAddr = *allocatedAddr;
    }

    declGuards.push_back(
        std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(cgf, vd));
    cgf.replaceAddrOfLocalVar(vd, privateAddr);
  }
  return mlir::success();
}

static void remapOpenMPTaskReductionDecls(
    CIRGenFunction &cgf, mlir::Operation *op,
    const CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo,
    llvm::SmallVectorImpl<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>>
        &declGuards,
    std::optional<mlir::ArrayAttr> reductionSyms = std::nullopt,
    llvm::SmallVectorImpl<std::unique_ptr<OpenMPTaskReductionVLASizeScope>>
        *vlaSizeScopes = nullptr) {
  auto reductionArgs =
      mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(op)
          .getTaskReductionBlockArgs();
  assert(clauseInfo.taskReductionIndices.size() ==
             clauseInfo.taskReductionVars.size() &&
         "mismatched OpenMP task reduction metadata");
  for (auto [index, vd] : llvm::zip_equal(clauseInfo.taskReductionIndices,
                                          clauseInfo.taskReductionVars)) {
    assert(index < reductionArgs.size() &&
           "unexpected OpenMP task reduction block arg index");
    mlir::Value reductionArg = reductionArgs[index];
    auto ptrTy = mlir::cast<cir::PointerType>(reductionArg.getType());
    declGuards.push_back(
        std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(cgf, vd));
    cgf.replaceAddrOfLocalVar(
        vd, Address(reductionArg, ptrTy.getPointee(),
                    cgf.getContext().getDeclAlign(vd)));
    if (!vlaSizeScopes || !reductionSyms || index >= reductionSyms->size())
      continue;
    const auto *vlaTy =
        cgf.getContext().getAsVariableArrayType(vd->getType().getNonReferenceType());
    if (!vlaTy)
      continue;
    auto reductionSym =
        mlir::dyn_cast<mlir::SymbolRefAttr>((*reductionSyms)[index]);
    if (!reductionSym)
      continue;
    mlir::FlatSymbolRefAttr sizeSym =
        lookupOpenMPTaskReductionSizeSym(cgf, reductionSym);
    if (!sizeSym)
      continue;
    vlaSizeScopes->push_back(std::make_unique<OpenMPTaskReductionVLASizeScope>(
        cgf, vd, vd, sizeSym, cgf.getLoc(vd->getLocation())));
  }
}

static void remapOpenMPInReductionDecls(
    CIRGenFunction &cgf, mlir::Operation *op,
    const CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo,
    llvm::SmallVectorImpl<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>>
        &declGuards,
    std::optional<mlir::ArrayAttr> reductionSyms = std::nullopt,
    llvm::SmallVectorImpl<std::unique_ptr<OpenMPTaskReductionVLASizeScope>>
        *vlaSizeScopes = nullptr) {
  auto reductionArgs =
      mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(op)
          .getInReductionBlockArgs();
  assert(clauseInfo.inReductionIndices.size() ==
             clauseInfo.inReductionVars.size() &&
         "mismatched OpenMP in_reduction metadata");
  for (auto [index, vd] : llvm::zip_equal(clauseInfo.inReductionIndices,
                                          clauseInfo.inReductionVars)) {
    assert(index < reductionArgs.size() &&
           "unexpected OpenMP in_reduction block arg index");
    mlir::Value reductionArg = reductionArgs[index];
    auto ptrTy = mlir::cast<cir::PointerType>(reductionArg.getType());
    declGuards.push_back(
        std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(cgf, vd));
    cgf.replaceAddrOfLocalVar(
        vd, Address(reductionArg, ptrTy.getPointee(),
                    cgf.getContext().getDeclAlign(vd)));
    if (!vlaSizeScopes || !reductionSyms || index >= reductionSyms->size())
      continue;
    const auto *vlaTy =
        cgf.getContext().getAsVariableArrayType(vd->getType().getNonReferenceType());
    if (!vlaTy)
      continue;
    auto reductionSym =
        mlir::dyn_cast<mlir::SymbolRefAttr>((*reductionSyms)[index]);
    if (!reductionSym)
      continue;
    mlir::FlatSymbolRefAttr sizeSym =
        lookupOpenMPTaskReductionSizeSym(cgf, reductionSym);
    if (!sizeSym)
      continue;
    vlaSizeScopes->push_back(std::make_unique<OpenMPTaskReductionVLASizeScope>(
        cgf, vd, vd, sizeSym, cgf.getLoc(vd->getLocation())));
  }
}

static bool areOpenMPExprsSemanticallyEqual(ASTContext &ctx, const Expr *lhs,
                                            const Expr *rhs) {
  if (lhs == rhs)
    return true;
  if (!lhs || !rhs)
    return false;

  lhs = lhs->IgnoreParenNoopCasts(ctx);
  rhs = rhs->IgnoreParenNoopCasts(ctx);
  if (lhs == rhs)
    return true;
  if (lhs->getStmtClass() != rhs->getStmtClass())
    return false;

  if (const auto *lhsDeclRef = dyn_cast<DeclRefExpr>(lhs)) {
    const auto *rhsDeclRef = dyn_cast<DeclRefExpr>(rhs);
    return rhsDeclRef && lhsDeclRef->getDecl()->getCanonicalDecl() ==
                             rhsDeclRef->getDecl()->getCanonicalDecl();
  }

  if (const auto *lhsArraySubscript = dyn_cast<ArraySubscriptExpr>(lhs)) {
    const auto *rhsArraySubscript = dyn_cast<ArraySubscriptExpr>(rhs);
    return rhsArraySubscript &&
           areOpenMPExprsSemanticallyEqual(ctx, lhsArraySubscript->getBase(),
                                           rhsArraySubscript->getBase()) &&
           areOpenMPExprsSemanticallyEqual(ctx, lhsArraySubscript->getIdx(),
                                           rhsArraySubscript->getIdx());
  }

  if (const auto *lhsMember = dyn_cast<MemberExpr>(lhs)) {
    const auto *rhsMember = dyn_cast<MemberExpr>(rhs);
    return rhsMember &&
           lhsMember->getMemberDecl()->getCanonicalDecl() ==
               rhsMember->getMemberDecl()->getCanonicalDecl() &&
           areOpenMPExprsSemanticallyEqual(ctx, lhsMember->getBase(),
                                           rhsMember->getBase());
  }

  if (const auto *lhsUnary = dyn_cast<UnaryOperator>(lhs)) {
    const auto *rhsUnary = dyn_cast<UnaryOperator>(rhs);
    return rhsUnary && lhsUnary->getOpcode() == rhsUnary->getOpcode() &&
           areOpenMPExprsSemanticallyEqual(ctx, lhsUnary->getSubExpr(),
                                           rhsUnary->getSubExpr());
  }

  if (const auto *lhsBinary = dyn_cast<BinaryOperator>(lhs)) {
    const auto *rhsBinary = dyn_cast<BinaryOperator>(rhs);
    return rhsBinary && lhsBinary->getOpcode() == rhsBinary->getOpcode() &&
           areOpenMPExprsSemanticallyEqual(ctx, lhsBinary->getLHS(),
                                           rhsBinary->getLHS()) &&
           areOpenMPExprsSemanticallyEqual(ctx, lhsBinary->getRHS(),
                                           rhsBinary->getRHS());
  }

  if (const auto *lhsArraySection = dyn_cast<ArraySectionExpr>(lhs)) {
    const auto *rhsArraySection = dyn_cast<ArraySectionExpr>(rhs);
    return rhsArraySection &&
           areOpenMPExprsSemanticallyEqual(ctx, lhsArraySection->getBase(),
                                           rhsArraySection->getBase()) &&
           areOpenMPExprsSemanticallyEqual(ctx,
                                           lhsArraySection->getLowerBound(),
                                           rhsArraySection->getLowerBound()) &&
           areOpenMPExprsSemanticallyEqual(ctx, lhsArraySection->getLength(),
                                           rhsArraySection->getLength());
  }

  if (const auto *lhsCast = dyn_cast<CastExpr>(lhs)) {
    const auto *rhsCast = dyn_cast<CastExpr>(rhs);
    return rhsCast && lhsCast->getCastKind() == rhsCast->getCastKind() &&
           areOpenMPExprsSemanticallyEqual(ctx, lhsCast->getSubExpr(),
                                           rhsCast->getSubExpr());
  }

  if (isa<CXXThisExpr>(lhs) && isa<CXXThisExpr>(rhs))
    return true;

  if (const auto *lhsInt = dyn_cast<IntegerLiteral>(lhs)) {
    const auto *rhsInt = dyn_cast<IntegerLiteral>(rhs);
    return rhsInt && lhsInt->getValue() == rhsInt->getValue();
  }

  if (const auto *lhsChar = dyn_cast<CharacterLiteral>(lhs)) {
    const auto *rhsChar = dyn_cast<CharacterLiteral>(rhs);
    return rhsChar && lhsChar->getValue() == rhsChar->getValue();
  }

  if (const auto *lhsFloat = dyn_cast<FloatingLiteral>(lhs)) {
    const auto *rhsFloat = dyn_cast<FloatingLiteral>(rhs);
    return rhsFloat && lhsFloat->getValue().bitwiseIsEqual(rhsFloat->getValue());
  }

  if (const auto *lhsString = dyn_cast<StringLiteral>(lhs)) {
    const auto *rhsString = dyn_cast<StringLiteral>(rhs);
    return rhsString && lhsString->getString() == rhsString->getString();
  }

  return false;
}

static llvm::SmallVector<const Expr *, 4>
collectOpenMPInscanReductionExprs(const OMPExecutableDirective &directive) {
  llvm::SmallVector<const Expr *, 4> reductionExprs;
  for (const auto *clause : directive.getClausesOfKind<OMPReductionClause>()) {
    if (clause->getModifier() != OMPC_REDUCTION_inscan)
      continue;
    for (const Expr *varExpr : clause->varlist())
      reductionExprs.push_back(varExpr);
  }
  return reductionExprs;
}

template <typename OpenMPLoopReductionOp>
static llvm::SmallVector<mlir::Value, 4>
collectOpenMPReductionRegionArgs(OpenMPLoopReductionOp op) {
  llvm::SmallVector<mlir::Value, 4> reductionArgs;
  if (op->getRegion(0).empty() || op.getNumReductionVars() == 0)
    return reductionArgs;

  auto blockArgs = op->getRegion(0).front().getArguments();
  assert(blockArgs.size() >= op.getNumReductionVars() &&
         "expected reduction block args in loop wrapper region");
  auto currentArgs = blockArgs.take_back(op.getNumReductionVars());
  reductionArgs.append(currentArgs.begin(), currentArgs.end());
  return reductionArgs;
}

static llvm::SmallVector<mlir::Value, 4>
collectCurrentOpenMPScanReductionArgs(CIRGenFunction &cgf,
                                      unsigned expectedCount) {
  llvm::SmallVector<mlir::Value, 4> reductionArgs;
  mlir::Block *insertionBlock = cgf.getBuilder().getInsertionBlock();
  auto considerReductionArgs = [&](llvm::ArrayRef<mlir::Value> currentArgs) {
    if (expectedCount && currentArgs.size() == expectedCount) {
      reductionArgs.assign(currentArgs.begin(), currentArgs.end());
      return true;
    }
    if (currentArgs.size() > reductionArgs.size())
      reductionArgs.assign(currentArgs.begin(), currentArgs.end());
    return false;
  };

  for (mlir::Operation *op = insertionBlock ? insertionBlock->getParentOp()
                                            : nullptr;
       op; op = op->getParentOp()) {
    if (auto wsloopOp = mlir::dyn_cast<mlir::omp::WsloopOp>(op)) {
      if (auto reductionMod = wsloopOp.getReductionMod();
          reductionMod &&
          *reductionMod == mlir::omp::ReductionModifier::inscan) {
        if (considerReductionArgs(collectOpenMPReductionRegionArgs(wsloopOp)) &&
            expectedCount)
          break;
      }
      continue;
    }
    if (auto simdOp = mlir::dyn_cast<mlir::omp::SimdOp>(op)) {
      if (auto reductionMod = simdOp.getReductionMod();
          reductionMod &&
          *reductionMod == mlir::omp::ReductionModifier::inscan) {
        if (considerReductionArgs(collectOpenMPReductionRegionArgs(simdOp)) &&
            expectedCount)
          break;
      }
    }
  }
  return reductionArgs;
}

static mlir::LogicalResult
finalizeOMPTargetPrivateMappings(CIRGenFunction &cgf,
                                 mlir::omp::TargetOp targetOp,
                                 const CIRGenFunction::OpenMPClauseEmissionInfo
                                     &clauseInfo) {
  mlir::OperandRange privateVars = targetOp.getPrivateVars();
  if (privateVars.empty())
    return mlir::success();

  std::optional<mlir::ArrayAttr> privateSyms = targetOp.getPrivateSyms();
  if (!privateSyms || privateSyms->size() != privateVars.size()) {
    cgf.getCIRGenModule().errorNYI(targetOp.getLoc(),
                                   "OpenMP target private symbol bookkeeping");
    return mlir::failure();
  }

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPoint(targetOp);

  llvm::SmallVector<int64_t, 4> privateMapIndices;
  llvm::SmallVector<mlir::Value, 4> newMapVars;
  privateMapIndices.reserve(privateVars.size());
  unsigned nextMapIndex = targetOp.getMapVars().size();

  for (auto [privateVarIdx, privateVarSymPair] :
       llvm::enumerate(llvm::zip_equal(privateVars, *privateSyms))) {
    mlir::Value privateVar = std::get<0>(privateVarSymPair);
    mlir::Attribute privateSym = std::get<1>(privateVarSymPair);
    auto privatizer = mlir::SymbolTable::lookupNearestSymbolFrom<
        mlir::omp::PrivateClauseOp>(
        targetOp, mlir::cast<mlir::SymbolRefAttr>(privateSym));
    if (!privatizer) {
      cgf.getCIRGenModule().errorNYI(targetOp.getLoc(),
                                     "OpenMP target private privatizer lookup");
      return mlir::failure();
    }
    if (!privatizer.needsMap()) {
      privateMapIndices.push_back(-1);
      continue;
    }

    auto ptrTy = mlir::dyn_cast<cir::PointerType>(privateVar.getType());
    if (!ptrTy) {
      cgf.getCIRGenModule().errorNYI(
          targetOp.getLoc(),
          "OpenMP target mapped privatizer source without pointer storage");
      return mlir::failure();
    }

    mlir::omp::ClauseMapFlags mapFlags = mlir::omp::ClauseMapFlags::to;
    mlir::omp::VariableCaptureKind captureKind =
        mlir::omp::VariableCaptureKind::ByRef;
    if (privatizer.getDataSharingType() ==
        mlir::omp::DataSharingClauseType::FirstPrivate) {
      // Match OGCG's target capture semantics for firstprivate vars.
      // Explicit firstprivate maps are target parameters without the
      // implicit bit, while synthetic/default-mapped captures keep it.
      mapFlags = mlir::omp::ClauseMapFlags::implicit;
      captureKind = mlir::omp::VariableCaptureKind::ByCopy;
      if (privateVarIdx < clauseInfo.privateVars.size()) {
        const VarDecl *vd = clauseInfo.privateVars[privateVarIdx];
        auto flagIt = clauseInfo.targetFirstprivateMapFlags.find(
            vd->getCanonicalDecl());
        if (flagIt != clauseInfo.targetFirstprivateMapFlags.end())
          mapFlags = static_cast<mlir::omp::ClauseMapFlags>(flagIt->second);
      }
    }

    auto mapInfoOp = mlir::omp::MapInfoOp::create(
        builder, privateVar.getLoc(), privateVar.getType(), privateVar,
        ptrTy.getPointee(), mapFlags, captureKind,
        /*var_ptr_ptr=*/mlir::Value{},
        /*members=*/mlir::ValueRange{},
        /*members_index=*/mlir::ArrayAttr{},
        /*bounds=*/mlir::ValueRange{},
        /*mapper_id=*/mlir::FlatSymbolRefAttr{},
        /*name=*/mlir::StringAttr{},
        /*partial_map=*/false);
    newMapVars.push_back(mapInfoOp.getResult());
    privateMapIndices.push_back(nextMapIndex++);
  }

  if (!newMapVars.empty())
    targetOp.getMapVarsMutable().append(newMapVars);
  targetOp.setPrivateMapsAttr(mlir::DenseI64ArrayAttr::get(
      builder.getContext(), privateMapIndices));
  return mlir::success();
}

static void shiftOpenMPTargetMapIndices(
    CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo, unsigned offset) {
  if (!offset)
    return;

  if (clauseInfo.targetThisMapIndex)
    *clauseInfo.targetThisMapIndex += offset;
  for (unsigned &index : clauseInfo.mapIndices)
    index += offset;
  for (unsigned &index : clauseInfo.mapReferenceValueIndices)
    index += offset;
  for (auto &info : clauseInfo.usesAllocatorVars)
    info.mapIndex += offset;
  for (auto &info : clauseInfo.targetAllocatorPrivateVars)
    if (info.sourceMapIndex >= 0)
      info.sourceMapIndex += offset;
}

static void promoteOMPTargetPrivateMapsForOffload(
    mlir::omp::TargetOp targetOp,
    CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo) {
  mlir::DenseI64ArrayAttr privateMapsAttr = targetOp.getPrivateMapsAttr();
  if (!privateMapsAttr || privateMapsAttr.empty())
    return;

  llvm::SmallVector<mlir::Value, 8> oldMapVars(targetOp.getMapVars().begin(),
                                               targetOp.getMapVars().end());
  if (oldMapVars.empty())
    return;

  llvm::SmallBitVector promoted(oldMapVars.size());
  llvm::SmallVector<mlir::Value, 8> reorderedMapVars;
  llvm::SmallVector<int64_t, 8> reorderedPrivateMaps;
  reorderedMapVars.reserve(oldMapVars.size());
  reorderedPrivateMaps.reserve(privateMapsAttr.size());

  for (int64_t oldIndex : privateMapsAttr.asArrayRef()) {
    if (oldIndex < 0) {
      reorderedPrivateMaps.push_back(-1);
      continue;
    }

    unsigned mapIndex = static_cast<unsigned>(oldIndex);
    assert(mapIndex < oldMapVars.size() &&
           "invalid OpenMP target private map index");
    if (!promoted.test(mapIndex)) {
      promoted.set(mapIndex);
      reorderedMapVars.push_back(oldMapVars[mapIndex]);
    }
    reorderedPrivateMaps.push_back(reorderedMapVars.size() - 1);
  }

  unsigned promotedCount = reorderedMapVars.size();
  if (!promotedCount)
    return;

  for (auto [index, mapVar] : llvm::enumerate(oldMapVars))
    if (!promoted.test(index))
      reorderedMapVars.push_back(mapVar);

  targetOp.getMapVarsMutable().assign(reorderedMapVars);
  targetOp.setPrivateMapsAttr(mlir::DenseI64ArrayAttr::get(
      targetOp.getContext(), reorderedPrivateMaps));
  shiftOpenMPTargetMapIndices(clauseInfo, promotedCount);
}

static llvm::SmallVector<const VarDecl *, 8>
filterOpenMPSyntheticFirstprivateVars(
    ArrayRef<const VarDecl *> vars,
    const CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo) {
  llvm::SmallPtrSet<const VarDecl *, 8> mappedVars;
  auto markMappedVars = [&](auto &&clauseVars) {
    for (const VarDecl *vd : clauseVars)
      mappedVars.insert(vd->getCanonicalDecl());
  };

  markMappedVars(clauseInfo.hasDeviceAddrVars);
  markMappedVars(clauseInfo.mapVars);
  markMappedVars(clauseInfo.privateVars);
  markMappedVars(clauseInfo.lastprivateVars);
  markMappedVars(clauseInfo.inReductionVars);
  markMappedVars(clauseInfo.reductionVars);
  markMappedVars(clauseInfo.taskReductionVars);

  llvm::SmallVector<const VarDecl *, 8> filteredVars;
  for (const VarDecl *vd : vars)
    if (mappedVars.insert(vd->getCanonicalDecl()).second)
      filteredVars.push_back(vd);
  return filteredVars;
}

template <typename OpTy>
static mlir::LogicalResult appendOpenMPSyntheticFirstprivatePrivates(
    CIRGenFunction &cgf, OpTy &op, ArrayRef<const VarDecl *> vars,
    CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo);

static bool isInsideOpenMPTargetRegion(CIRGenFunction &cgf) {
  mlir::Block *insertionBlock = cgf.getBuilder().getInsertionBlock();
  if (!insertionBlock)
    return false;
  mlir::Operation *parentOp = insertionBlock->getParentOp();
  return parentOp && parentOp->getParentOfType<mlir::omp::TargetOp>();
}

static mlir::LogicalResult
appendOpenMPParallelNestedSyntheticFirstprivatePrivates(
    CIRGenFunction &cgf, mlir::omp::ParallelOp parallelOp,
    ArrayRef<const OMPClause *> nestedClauses,
    CIRGenFunction::OpenMPClauseEmissionInfo &parallelClauseInfo) {
  llvm::SmallVector<const VarDecl *, 8> syntheticFirstprivateVars;
  collectOpenMPClauseSyntheticFirstprivateVars(nestedClauses,
                                               syntheticFirstprivateVars);
  syntheticFirstprivateVars = filterOpenMPSyntheticFirstprivateVars(
      syntheticFirstprivateVars, parallelClauseInfo);
  return appendOpenMPSyntheticFirstprivatePrivates(
      cgf, parallelOp, syntheticFirstprivateVars, parallelClauseInfo);
}

static mlir::LogicalResult appendHostOnlyOMPTargetCapturedMappings(
    CIRGenFunction &cgf, mlir::omp::TargetOp targetOp,
    CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo,
    const CapturedStmt &capturedStmt) {
  llvm::SmallPtrSet<const VarDecl *, 8> mappedDecls;
  for (const VarDecl *mappedVD : clauseInfo.hasDeviceAddrVars)
    mappedDecls.insert(mappedVD->getCanonicalDecl());
  for (const VarDecl *mappedVD : clauseInfo.mapVars)
    mappedDecls.insert(mappedVD->getCanonicalDecl());
  for (const VarDecl *privateVD : clauseInfo.privateVars)
    mappedDecls.insert(privateVD->getCanonicalDecl());
  for (const VarDecl *reductionVD : clauseInfo.inReductionVars)
    mappedDecls.insert(reductionVD->getCanonicalDecl());

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPoint(targetOp);

  llvm::SmallVector<mlir::Value, 4> capturedMaps;
  unsigned mapStartIdx = targetOp.getMapVars().size();
  CapturedStmt::const_capture_iterator captureIt = capturedStmt.capture_begin();
  CapturedStmt::const_capture_iterator captureEnd = capturedStmt.capture_end();
  for (; captureIt != captureEnd; ++captureIt) {
    if (!captureIt->capturesVariable() && !captureIt->capturesVariableByCopy())
      continue;

    const VarDecl *capturedVD = captureIt->getCapturedVar();
    if (!capturedVD)
      continue;
    const VarDecl *canonicalVD = capturedVD->getCanonicalDecl();
    if (!mappedDecls.insert(canonicalVD).second)
      continue;

    auto findLocalDecl = [&](const VarDecl *vd) {
      return cgf.localDeclMap.find(vd);
    };

    auto localIt = findLocalDecl(canonicalVD);
    if (localIt == cgf.localDeclMap.end() && capturedVD != canonicalVD)
      localIt = findLocalDecl(capturedVD);
    if (localIt == cgf.localDeclMap.end()) {
      if (const auto *capturedExpr = dyn_cast<OMPCapturedExprDecl>(capturedVD))
        cgf.emitOMPCapturedExpr(*capturedExpr);
      else if (const auto *capturedExpr =
                   dyn_cast<OMPCapturedExprDecl>(canonicalVD))
        cgf.emitOMPCapturedExpr(*capturedExpr);

      localIt = findLocalDecl(canonicalVD);
      if (localIt == cgf.localDeclMap.end() && capturedVD != canonicalVD)
        localIt = findLocalDecl(capturedVD);
    }
    if (localIt == cgf.localDeclMap.end()) {
      cgf.getCIRGenModule().errorNYI(
          capturedVD->getLocation(),
          "OpenMP host-only target capture remapping without local storage");
      return mlir::failure();
    }

    Address captureAddr = localIt->second;
    mlir::Location loc = cgf.getLoc(captureIt->getLocation());
    auto mapInfoOp = mlir::omp::MapInfoOp::create(
        builder, loc, captureAddr.getPointer().getType(),
        captureAddr.getPointer(), captureAddr.getElementType(),
        mlir::omp::ClauseMapFlags::implicit,
        captureIt->capturesVariableByCopy()
            ? mlir::omp::VariableCaptureKind::ByCopy
            : mlir::omp::VariableCaptureKind::ByRef,
        /*var_ptr_ptr=*/mlir::Value{},
        /*members=*/mlir::ValueRange{},
        /*members_index=*/mlir::ArrayAttr{},
        /*bounds=*/mlir::ValueRange{},
        /*mapper_id=*/mlir::FlatSymbolRefAttr{},
        /*name=*/builder.getStringAttr(canonicalVD->getName()),
        /*partial_map=*/false);
    capturedMaps.push_back(mapInfoOp.getResult());
    clauseInfo.mapIndices.push_back(mapStartIdx + capturedMaps.size() - 1);
    clauseInfo.mapVars.push_back(canonicalVD);
  }

  if (!capturedMaps.empty())
    targetOp.getMapVarsMutable().append(capturedMaps);
  return mlir::success();
}

static void materializeOMPTargetClauseExprs(
    CIRGenFunction &cgf, ArrayRef<const OMPClause *> clauses,
    llvm::SmallPtrSetImpl<const VarDecl *> &materializedClauseVars) {
  for (const OMPClause *clause : clauses) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if:
      materializeOpenMPCapturedClauseExpr(
          cgf, cast<OMPIfClause>(clause)->getCondition(),
          materializedClauseVars);
      break;
    case llvm::omp::OMPC_device:
      materializeOpenMPCapturedClauseExpr(
          cgf, cast<OMPDeviceClause>(clause)->getDevice(),
          materializedClauseVars);
      break;
    case llvm::omp::OMPC_is_device_ptr:
      for (const Expr *varExpr : cast<OMPIsDevicePtrClause>(clause)->varlist())
        materializeOpenMPMapClauseExprs(cgf, varExpr, materializedClauseVars);
      break;
    case llvm::omp::OMPC_has_device_addr:
      for (const Expr *varExpr :
           cast<OMPHasDeviceAddrClause>(clause)->varlist())
        materializeOpenMPMapClauseExprs(cgf, varExpr, materializedClauseVars);
      break;
    case llvm::omp::OMPC_map:
      for (const Expr *varExpr : cast<OMPMapClause>(clause)->varlist())
        materializeOpenMPMapClauseExprs(cgf, varExpr, materializedClauseVars);
      break;
    case llvm::omp::OMPC_reduction:
      for (const Expr *varExpr : cast<OMPReductionClause>(clause)->varlist())
        materializeOpenMPMapClauseExprs(cgf, varExpr, materializedClauseVars);
      break;
    case llvm::omp::OMPC_thread_limit:
      materializeOpenMPCapturedClauseExpr(
          cgf, cast<OMPThreadLimitClause>(clause)->getThreadLimit().front(),
          materializedClauseVars);
      break;
    case llvm::omp::OMPC_uses_allocators:
      for (unsigned i = 0,
                    e = cast<OMPUsesAllocatorsClause>(clause)
                            ->getNumberOfAllocators();
           i != e; ++i) {
        auto data =
            cast<OMPUsesAllocatorsClause>(clause)->getAllocatorData(i);
        if (data.AllocatorTraits)
          materializeOpenMPCapturedClauseExpr(cgf, data.AllocatorTraits,
                                              materializedClauseVars);
      }
      break;
    default:
      break;
    }
  }
}

static void materializeOpenMPSimdCompositeClauseExprs(
    CIRGenFunction &cgf, ArrayRef<const OMPClause *> clauses,
    llvm::SmallPtrSetImpl<const VarDecl *> &materializedClauseVars) {
  for (const OMPClause *clause : clauses) {
    if (clause->getClauseKind() != llvm::omp::OMPC_if)
      continue;
    materializeOpenMPCapturedClauseExpr(
        cgf, cast<OMPIfClause>(clause)->getCondition(),
        materializedClauseVars);
  }
}

static void materializeOpenMPSimpleSimdClauseExprs(
    CIRGenFunction &cgf, ArrayRef<const OMPClause *> clauses,
    llvm::SmallPtrSetImpl<const VarDecl *> &materializedClauseVars) {
  for (const OMPClause *clause : clauses) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if:
      materializeOpenMPCapturedClauseExpr(
          cgf, cast<OMPIfClause>(clause)->getCondition(),
          materializedClauseVars);
      break;
    case llvm::omp::OMPC_private:
      for (const Expr *varExpr : cast<OMPPrivateClause>(clause)->varlist())
        materializeOpenMPCapturedClauseExpr(cgf, varExpr,
                                            materializedClauseVars);
      break;
    case llvm::omp::OMPC_linear: {
      const auto *linearClause = cast<OMPLinearClause>(clause);
      materializeOpenMPCapturedClauseExpr(cgf, linearClause->getStep(),
                                          materializedClauseVars);
      for (const Expr *varExpr : linearClause->varlist())
        materializeOpenMPCapturedClauseExpr(cgf, varExpr,
                                            materializedClauseVars);
      break;
    }
    case llvm::omp::OMPC_lastprivate:
      for (const Expr *varExpr : cast<OMPLastprivateClause>(clause)->varlist())
        materializeOpenMPCapturedClauseExpr(cgf, varExpr,
                                            materializedClauseVars);
      break;
    case llvm::omp::OMPC_nontemporal:
      for (const Stmt *ref : cast<OMPNontemporalClause>(clause)->private_refs())
        materializeOpenMPCapturedClauseExpr(cgf, cast<Expr>(ref),
                                            materializedClauseVars);
      break;
    case llvm::omp::OMPC_aligned: {
      const auto *alignedClause = cast<OMPAlignedClause>(clause);
      if (const Expr *alignmentExpr = alignedClause->getAlignment())
        materializeOpenMPCapturedClauseExpr(cgf, alignmentExpr,
                                            materializedClauseVars);
      for (const Expr *varExpr : alignedClause->varlist())
        materializeOpenMPCapturedClauseExpr(cgf, varExpr,
                                            materializedClauseVars);
      break;
    }
    case llvm::omp::OMPC_reduction:
      for (const Expr *varExpr : cast<OMPReductionClause>(clause)->varlist())
        materializeOpenMPCapturedClauseExpr(cgf, varExpr,
                                            materializedClauseVars);
      break;
    default:
      break;
    }
  }
}

static cir::FuncOp getOpenMPInitAllocatorRuntimeFunction(CIRGenFunction &cgf) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  cir::FuncOp fn = cgf.cgm.createRuntimeFunction(
      builder.getFuncType({builder.getSInt32Ty(), builder.getVoidPtrTy(),
                           builder.getSInt32Ty(), builder.getVoidPtrTy()},
                          builder.getVoidPtrTy()),
      "__kmpc_init_allocator");
  setRuntimeFnNothrow(fn);
  return fn;
}

static cir::FuncOp getOpenMPDestroyAllocatorRuntimeFunction(
    CIRGenFunction &cgf) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  cir::FuncOp fn = cgf.cgm.createRuntimeFunction(
      builder.getFuncType(
          {builder.getSInt32Ty(), builder.getVoidPtrTy()}, builder.getVoidTy()),
      "__kmpc_destroy_allocator");
  setRuntimeFnNothrow(fn);
  return fn;
}

static cir::FuncOp getOpenMPAllocRuntimeFunction(CIRGenFunction &cgf) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  cir::FuncOp fn = cgf.cgm.createRuntimeFunction(
      builder.getFuncType(
          {builder.getSInt32Ty(), cgf.sizeTy, builder.getVoidPtrTy()},
          builder.getVoidPtrTy()),
      "__kmpc_alloc");
  setRuntimeFnNothrow(fn);
  fn.setResultAttr(0, "cir.noalias",
                   mlir::UnitAttr::get(fn.getContext()));
  return fn;
}

static cir::FuncOp getOpenMPFreeRuntimeFunction(CIRGenFunction &cgf) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  cir::FuncOp fn = cgf.cgm.createRuntimeFunction(
      builder.getFuncType(
          {builder.getSInt32Ty(), builder.getVoidPtrTy(), builder.getVoidPtrTy()},
          builder.getVoidTy()),
      "__kmpc_free");
  setRuntimeFnNothrow(fn);
  return fn;
}

static mlir::Value convertOpenMPAllocatorValueToVoidPtr(CIRGenFunction &cgf,
                                                        mlir::Location loc,
                                                        mlir::Value value) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Type voidPtrTy = builder.getVoidPtrTy();
  if (value.getType() == voidPtrTy)
    return value;
  if (mlir::isa<cir::PointerType>(value.getType()))
    return builder.createBitcast(loc, value, voidPtrTy);
  return builder.createIntToPtr(value, voidPtrTy);
}

static std::optional<CharUnits>
getOpenMPAllocateAlignment(CIRGenFunction &cgf, const VarDecl *privateCopyVD) {
  const auto *attr =
      privateCopyVD ? privateCopyVD->getAttr<OMPAllocateDeclAttr>() : nullptr;
  if (!attr || !attr->getAlignment())
    return std::nullopt;

  unsigned userAlign =
      attr->getAlignment()->EvaluateKnownConstInt(cgf.getContext()).getExtValue();
  CharUnits naturalAlign = cgf.getCIRGenModule().getNaturalTypeAlignment(
      privateCopyVD->getType().getNonReferenceType(), /*baseInfo=*/nullptr);
  return CharUnits::fromQuantity(
      std::max<unsigned>(userAlign, naturalAlign.getQuantity()));
}

static mlir::Value getOpenMPAllocatorValue(CIRGenFunction &cgf,
                                           mlir::Location loc,
                                           const Expr *allocatorExpr) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Type voidPtrTy = builder.getVoidPtrTy();
  if (!allocatorExpr)
    return builder.getNullValue(voidPtrTy, loc);

  mlir::Value allocatorValue = cgf.emitScalarExpr(allocatorExpr);
  return convertOpenMPAllocatorValueToVoidPtr(cgf, loc, allocatorValue);
}

struct CallOpenMPPrivateAllocateCleanup final : EHScopeStack::Cleanup {
  cir::FuncOp freeFn;
  SourceLocation::UIntTy locEncoding;
  Address addr;
  const Expr *allocatorExpr;

  CallOpenMPPrivateAllocateCleanup(cir::FuncOp freeFn,
                                   SourceLocation::UIntTy locEncoding,
                                   Address addr, const Expr *allocatorExpr)
      : freeFn(freeFn), locEncoding(locEncoding), addr(addr),
        allocatorExpr(allocatorExpr) {}

  void emit(CIRGenFunction &cgf, Flags) override {
    if (!cgf.haveInsertPoint())
      return;

    SourceLocation sourceLoc = SourceLocation::getFromRawEncoding(locEncoding);
    mlir::Location loc =
        sourceLoc.isValid() ? cgf.getLoc(sourceLoc) : addr.getPointer().getLoc();
    mlir::Value privatePtr = addr.getPointer();
    if (privatePtr.getType() != cgf.getBuilder().getVoidPtrTy())
      privatePtr = cgf.getBuilder().createBitcast(
          loc, privatePtr, cgf.getBuilder().getVoidPtrTy());
    cgf.emitRuntimeCall(
        loc, freeFn,
        {cgf.emitOpenMPThreadID(sourceLoc, 0), privatePtr,
         getOpenMPAllocatorValue(cgf, loc, allocatorExpr)});
  }
};

static mlir::LogicalResult emitOpenMPFirstprivateCopyToAddress(
    CIRGenFunction &cgf, const VarDecl *vd, Address fromAddr, Address toAddr,
    const VarDecl *privateCopyVD, const VarDecl *initVD) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  QualType varTy = vd->getType();
  mlir::Location loc = cgf.getLoc(vd->getLocation());

  if (varTy->isScalarType() || varTy->isReferenceType()) {
    mlir::Value copied = builder.createLoad(loc, fromAddr);
    builder.createStore(loc, copied, toAddr);
    return mlir::success();
  }

  if (varTy.isTriviallyCopyableType(cgf.getContext())) {
    LValue destLV = cgf.makeAddrLValue(toAddr, varTy);
    LValue srcLV = cgf.makeAddrLValue(fromAddr, varTy);
    cgf.emitAggregateCopy(destLV, srcLV, varTy, AggValueSlot::DoesNotOverlap);
    return mlir::success();
  }

  if (!privateCopyVD || !initVD) {
    cgf.getCIRGenModule().errorNYI(
        vd->getLocation(),
        "OpenMP target allocator firstprivate non-scalar/non-reference "
        "variable without init expression");
    return mlir::failure();
  }

  // The copy-ctor emitted here is OpenMP-internal codegen at the region
  // boundary, not a user throw escaping the region; suppress the gate.
  CIRGenFunction::OpenMPRegionDataSharingCopyRAII copyGuard(cgf);
  CIRGenFunction::SymTableScopeTy scope(cgf.symbolTable);
  cgf.setAddrOfLocalVar(initVD, fromAddr);
  const Expr *init = privateCopyVD->getInit();
  cgf.emitAnyExprToMem(init, toAddr, init->getType().getQualifiers(),
                       /*isInit=*/true);
  return mlir::success();
}

static const CIRGenFunction::OpenMPClauseEmissionInfo::
    OpenMPExplicitPrivateAllocatorInfo *
lookupOpenMPExplicitPrivateAllocatorInfo(
    const CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo,
    const VarDecl *vd) {
  const VarDecl *canonicalVD = vd->getCanonicalDecl();
  for (const auto &info : clauseInfo.explicitPrivateAllocatorVars)
    if (info.privateVar == canonicalVD)
      return &info;
  return nullptr;
}

static std::optional<Address> emitOpenMPExplicitPrivateAllocatorAddress(
    CIRGenFunction &cgf, const VarDecl *vd, const VarDecl *privateCopyVD) {
  const auto *attr =
      privateCopyVD ? privateCopyVD->getAttr<OMPAllocateDeclAttr>() : nullptr;
  if (!attr) {
    cgf.getCIRGenModule().errorNYI(vd->getLocation(),
                                   "OpenMP private allocator attribute");
    return std::nullopt;
  }

  QualType varTy = vd->getType();
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(vd->getLocation());
  CharUnits alignment = cgf.getContext().getDeclAlign(vd);
  auto sizeIntTy = mlir::cast<cir::IntType>(cgf.sizeTy);

  auto getSizeValue = [&]() -> mlir::Value {
    if (varTy->isVariablyModifiedType()) {
      CIRGenFunction::VlaSizePair vlaSize = cgf.getVLASize(varTy);
      mlir::Value sizeValue = vlaSize.numElts;
      if (sizeValue.getType() != cgf.sizeTy)
        sizeValue = builder.createIntCast(sizeValue, cgf.sizeTy);
      CharUnits elementSize = cgf.getContext().getTypeSizeInChars(vlaSize.type);
      if (!elementSize.isOne()) {
        mlir::Value elementSizeValue =
            builder.getConstInt(loc, sizeIntTy, elementSize.getQuantity());
        sizeValue = builder.createNUWAMul(loc, sizeValue, elementSizeValue);
      }
      mlir::Value alignMinusOne =
          builder.getConstInt(loc, sizeIntTy, alignment.getQuantity() - 1);
      mlir::Value alignValue =
          builder.getConstInt(loc, sizeIntTy, alignment.getQuantity());
      sizeValue = builder.createNUWAdd(loc, sizeValue, alignMinusOne);
      sizeValue =
          builder.createBinop(loc, sizeValue, cir::BinOpKind::Div, alignValue);
      return builder.createNUWAMul(loc, sizeValue, alignValue);
    }

    CharUnits size = cgf.getContext().getTypeSizeInChars(varTy);
    return builder.getConstInt(loc, sizeIntTy,
                               size.alignTo(alignment).getQuantity());
  };

  std::optional<CharUnits> allocateAlignment =
      getOpenMPAllocateAlignment(cgf, privateCopyVD);
  if (!allocateAlignment) {
    cgf.getCIRGenModule().errorNYI(vd->getLocation(),
                                   "OpenMP explicit private allocator "
                                   "without alignment");
    return std::nullopt;
  }

  cir::FuncOp allocFn = cgf.getCIRGenModule().createRuntimeFunction(
      builder.getFuncType(
          {builder.getSInt32Ty(), cgf.sizeTy, cgf.sizeTy,
           builder.getVoidPtrTy()},
          builder.getVoidPtrTy()),
      "__kmpc_aligned_alloc");
  setRuntimeFnNothrow(allocFn);
  allocFn.setResultAttr(0, "cir.noalias",
                        mlir::UnitAttr::get(allocFn.getContext()));
  mlir::Value allocatedPtr = cgf.emitRuntimeCall(
      loc, allocFn,
      {cgf.emitOpenMPThreadID(vd->getLocation(), 0),
       builder.getConstInt(loc, sizeIntTy, allocateAlignment->getQuantity()),
       getSizeValue(), getOpenMPAllocatorValue(cgf, loc, attr->getAllocator())});

  mlir::Type valueTy = cgf.convertTypeForMem(varTy);
  mlir::Type valuePtrTy = builder.getPointerTo(valueTy);
  if (allocatedPtr.getType() != valuePtrTy)
    allocatedPtr = builder.createBitcast(loc, allocatedPtr, valuePtrTy);

  cir::FuncOp freeFn = getOpenMPFreeRuntimeFunction(cgf);
  Address addr(allocatedPtr, valueTy, alignment);
  cgf.ehStack.pushCleanup<CallOpenMPPrivateAllocateCleanup>(
      NormalAndEHCleanup, freeFn, vd->getLocation().getRawEncoding(), addr,
      attr->getAllocator());
  return addr;
}

static Address getOpenMPPrivateStorageAddress(CIRGenFunction &cgf,
                                              const VarDecl *vd) {
  std::optional<Address> privateAddr =
      lookupOpenMPAddressableDeclAddress(cgf, vd);
  assert(privateAddr && "missing OpenMP private storage address");
  return *privateAddr;
}

static std::optional<Address> emitOpenMPTargetAllocatorPrivateAddress(
    CIRGenFunction &cgf, const VarDecl *vd, Address allocatorAddr) {
  QualType varTy = vd->getType();
  if (varTy->isVariablyModifiedType()) {
    cgf.getCIRGenModule().errorNYI(
        vd->getLocation(),
        "OpenMP target uses_allocators allocator for variably modified "
        "private/firstprivate variable");
    return std::nullopt;
  }

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(vd->getLocation());
  CharUnits alignment = cgf.getContext().getDeclAlign(vd);
  auto sizeIntTy = mlir::cast<cir::IntType>(cgf.sizeTy);
  mlir::Value allocatorValue = builder.createLoad(loc, allocatorAddr);
  allocatorValue = convertOpenMPAllocatorValueToVoidPtr(cgf, loc, allocatorValue);
  mlir::Value sizeValue = builder.getConstInt(
      loc, sizeIntTy,
      cgf.getContext().getTypeSizeInChars(varTy).alignTo(alignment).getQuantity());
  mlir::Value allocatedPtr = cgf.emitRuntimeCall(
      loc, getOpenMPAllocRuntimeFunction(cgf),
      {cgf.emitOpenMPThreadID(vd->getLocation(), 0), sizeValue, allocatorValue});

  mlir::Type valueTy = cgf.convertTypeForMem(varTy);
  mlir::Type valuePtrTy = builder.getPointerTo(valueTy);
  if (allocatedPtr.getType() != valuePtrTy)
    allocatedPtr = builder.createBitcast(loc, allocatedPtr, valuePtrTy);
  return Address(allocatedPtr, valueTy, alignment);
}

static void emitOpenMPTargetAllocatorPrivateFree(CIRGenFunction &cgf,
                                                 const VarDecl *vd,
                                                 Address privateAddr,
                                                 Address allocatorAddr) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(vd->getLocation());
  mlir::Value privatePtr = privateAddr.getPointer();
  if (privatePtr.getType() != builder.getVoidPtrTy())
    privatePtr = builder.createBitcast(loc, privatePtr, builder.getVoidPtrTy());
  mlir::Value allocatorValue = builder.createLoad(loc, allocatorAddr);
  allocatorValue = convertOpenMPAllocatorValueToVoidPtr(cgf, loc, allocatorValue);
  cgf.emitRuntimeCall(loc, getOpenMPFreeRuntimeFunction(cgf),
                      {cgf.emitOpenMPThreadID(vd->getLocation(), 0), privatePtr,
                       allocatorValue});
}

static mlir::Value getOpenMPTargetUsesAllocatorTraitsPtr(CIRGenFunction &cgf,
                                                         mlir::Location loc,
                                                         const VarDecl *vd) {
  Address traitsAddr = cgf.getAddrOfLocalVar(vd);
  mlir::Value ptr = traitsAddr.getPointer();
  if (ptr.getType() != cgf.getBuilder().getVoidPtrTy())
    ptr = cgf.getBuilder().createBitcast(loc, ptr, cgf.getBuilder().getVoidPtrTy());
  return ptr;
}

static llvm::FailureOr<mlir::omp::PrivateClauseOp>
getOrCreateOpenMPSyntheticFirstprivatePrivatizer(CIRGenFunction &cgf,
                                                 mlir::Location loc,
                                                 const VarDecl *vd,
                                                 mlir::Type valueTy,
                                                 mlir::Type pointerTy) {
  mlir::ModuleOp module = cgf.getCIRGenModule().getModule();
  std::string baseName =
      getOpenMPPrivatizerBaseName(cgf, vd, "__omp_firstprivate_");
  std::string name = baseName;
  unsigned suffix = 0;
  while (module.lookupSymbol(name))
    name = (baseName + "." + llvm::Twine(++suffix)).str();

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());

  auto privatizer = mlir::omp::PrivateClauseOp::create(
      builder, loc, name, valueTy, mlir::omp::DataSharingClauseType::FirstPrivate);

  mlir::Block *copyBlock = builder.createBlock(
      &privatizer.getCopyRegion(), {}, {pointerTy, pointerTy}, {loc, loc});
  builder.setInsertionPointToEnd(copyBlock);

  CharUnits align = cgf.getContext().getDeclAlign(vd);
  Address fromAddr(copyBlock->getArgument(0), valueTy, align);
  Address toAddr(copyBlock->getArgument(1), valueTy, align);
  if (mlir::failed(
          emitOpenMPFirstprivateCopyToAddress(cgf, vd, fromAddr, toAddr,
                                              /*privateCopyVD=*/nullptr,
                                              /*initVD=*/nullptr)))
    return llvm::failure();

  mlir::omp::YieldOp::create(builder, loc, copyBlock->getArgument(1));
  return privatizer;
}

static llvm::FailureOr<mlir::omp::PrivateClauseOp>
getOrCreateOpenMPVLASizeFirstprivatePrivatizer(CIRGenFunction &cgf,
                                               mlir::Location loc,
                                               mlir::Type valueTy,
                                               mlir::Type pointerTy) {
  mlir::ModuleOp module = cgf.getCIRGenModule().getModule();
  std::string baseName = "__omp_firstprivate_vla_size";
  std::string name = baseName;
  unsigned suffix = 0;
  while (module.lookupSymbol(name))
    name = (baseName + "." + llvm::Twine(++suffix)).str();

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());

  auto privatizer = mlir::omp::PrivateClauseOp::create(
      builder, loc, name, valueTy, mlir::omp::DataSharingClauseType::FirstPrivate);

  mlir::Block *copyBlock = builder.createBlock(
      &privatizer.getCopyRegion(), {}, {pointerTy, pointerTy}, {loc, loc});
  builder.setInsertionPointToEnd(copyBlock);

  CharUnits align = cgf.getContext().getTypeAlignInChars(
      cgf.getContext().getSizeType());
  Address fromAddr(copyBlock->getArgument(0), valueTy, align);
  Address toAddr(copyBlock->getArgument(1), valueTy, align);
  mlir::Value copied = builder.createLoad(loc, fromAddr);
  builder.createStore(loc, copied, toAddr);
  mlir::omp::YieldOp::create(builder, loc, copyBlock->getArgument(1));
  return privatizer;
}

template <typename OpTy>
static mlir::LogicalResult appendOpenMPSyntheticFirstprivatePrivates(
    CIRGenFunction &cgf, OpTy &op, ArrayRef<const VarDecl *> vars,
    CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo) {
  if (vars.empty())
    return mlir::success();

  CIRGenBuilderTy &builder = cgf.getBuilder();

  llvm::SmallVector<mlir::Value, 4> privateVars;
  llvm::SmallVector<mlir::Attribute, 4> privateSyms;
  privateVars.reserve(vars.size());
  privateSyms.reserve(vars.size());
  unsigned firstPrivateStartIdx = op.getPrivateVars().size();

  for (const VarDecl *vd : vars) {
    std::optional<Address> mappedAddr =
        lookupOpenMPAddressableDeclAddress(cgf, vd, op.getOperation());
    if (!mappedAddr) {
      cgf.getCIRGenModule().errorNYI(
          vd->getLocation(),
          "OpenMP synthetic firstprivate capture without addressable storage");
      return mlir::failure();
    }

    mlir::Value privateVar = mappedAddr->getPointer();
    auto pointerTy = mlir::dyn_cast<cir::PointerType>(privateVar.getType());
    if (!pointerTy) {
      cgf.getCIRGenModule().errorNYI(
          vd->getLocation(),
          "OpenMP synthetic firstprivate non-pointer capture");
      return mlir::failure();
    }

    llvm::FailureOr<mlir::omp::PrivateClauseOp> privatizer =
        getOrCreateOpenMPSyntheticFirstprivatePrivatizer(
            cgf, cgf.getLoc(vd->getLocation()), vd, pointerTy.getPointee(),
            privateVar.getType());
    if (llvm::failed(privatizer))
      return mlir::failure();

    privateVars.push_back(privateVar);
    privateSyms.push_back(mlir::SymbolRefAttr::get(*privatizer));
    clauseInfo.privateIndices.push_back(firstPrivateStartIdx++);
    clauseInfo.privateVars.push_back(vd);
    if constexpr (std::is_same_v<OpTy, mlir::omp::TargetOp>) {
      clauseInfo.targetFirstprivateMapFlags[vd->getCanonicalDecl()] =
          static_cast<unsigned>(mlir::omp::ClauseMapFlags::implicit);
    }
  }

  op.getPrivateVarsMutable().append(privateVars);

  llvm::SmallVector<mlir::Attribute, 4> allPrivateSyms;
  if (auto existingSyms = op.getPrivateSyms())
    llvm::append_range(allPrivateSyms, *existingSyms);
  llvm::append_range(allPrivateSyms, privateSyms);
  op.setPrivateSymsAttr(
      mlir::ArrayAttr::get(builder.getContext(), allPrivateSyms));
  return mlir::success();
}

static mlir::LogicalResult appendOpenMPTargetCapturedVLASizePrivates(
    CIRGenFunction &cgf, mlir::omp::TargetOp &targetOp,
    const CapturedStmt &capturedStmt,
    CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo) {
  const RecordDecl *capturedRecord = capturedStmt.getCapturedRecordDecl();
  if (!capturedRecord)
    return mlir::success();

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPoint(targetOp);
  mlir::Type sizeTy = cgf.sizeTy;
  mlir::Type sizePtrTy = builder.getPointerTo(sizeTy);
  CharUnits sizeAlign = cgf.getContext().getTypeAlignInChars(
      cgf.getContext().getSizeType());

  llvm::SmallPtrSet<const Expr *, 4> seenSizeExprs;
  llvm::SmallVector<mlir::Value, 4> privateVars;
  llvm::SmallVector<mlir::Attribute, 4> privateSyms;
  unsigned privateStartIdx = targetOp.getPrivateVars().size();

  for (const FieldDecl *fd : capturedRecord->fields()) {
    if (!fd->hasCapturedVLAType())
      continue;

    const VariableArrayType *vat = fd->getCapturedVLAType();
    const Expr *sizeExpr = vat->getSizeExpr();
    if (!seenSizeExprs.insert(sizeExpr).second)
      continue;

    auto it = cgf.vlaSizeMap.find(sizeExpr);
    if (it == cgf.vlaSizeMap.end()) {
      cgf.getCIRGenModule().errorNYI(
          fd->getLocation(),
          "OpenMP target captured VLA size without active size mapping");
      return mlir::failure();
    }

    mlir::Location loc = cgf.getLoc(fd->getLocation());
    mlir::Value sizeValue = it->second;
    if (sizeValue.getType() != sizeTy)
      sizeValue = builder.createIntCast(sizeValue, sizeTy);

    Address sizeStorage =
        cgf.createTempAlloca(sizeTy, sizeAlign, loc, "__omp_vla_size");
    builder.createStore(loc, sizeValue, sizeStorage);

    llvm::FailureOr<mlir::omp::PrivateClauseOp> privatizer =
        getOrCreateOpenMPVLASizeFirstprivatePrivatizer(cgf, loc, sizeTy,
                                                       sizePtrTy);
    if (llvm::failed(privatizer))
      return mlir::failure();

    privateVars.push_back(sizeStorage.getPointer());
    privateSyms.push_back(mlir::SymbolRefAttr::get(*privatizer));
    clauseInfo.capturedVLASizeIndices.push_back(privateStartIdx++);
    clauseInfo.capturedVLASizeExprs.push_back(sizeExpr);
  }

  if (privateVars.empty())
    return mlir::success();

  targetOp.getPrivateVarsMutable().append(privateVars);

  llvm::SmallVector<mlir::Attribute, 4> allPrivateSyms;
  if (auto existingSyms = targetOp.getPrivateSyms())
    llvm::append_range(allPrivateSyms, *existingSyms);
  llvm::append_range(allPrivateSyms, privateSyms);
  targetOp.setPrivateSymsAttr(
      mlir::ArrayAttr::get(builder.getContext(), allPrivateSyms));
  return mlir::success();
}

static mlir::LogicalResult emitOMPTargetRegionBody(
    CIRGenFunction &cgf, mlir::omp::TargetOp targetOp,
    const CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo,
    mlir::Location begin, mlir::Location end,
    llvm::function_ref<mlir::LogicalResult()> bodyEmitter,
    bool wrapInScope = true) {
  auto getMappedVarAddress =
      [&](const VarDecl *vd, mlir::Value mappedArg,
          bool materializeReferenceFromValue = false)
      -> std::optional<Address> {
    mlir::Type varTy = cgf.convertTypeForMem(vd->getType());
    CharUnits align = cgf.getContext().getDeclAlign(vd);
    if (auto ptrTy = mlir::dyn_cast<cir::PointerType>(mappedArg.getType());
        ptrTy && ptrTy.getPointee() == varTy)
      return Address(mappedArg, varTy, align);

    if (mappedArg.getType() == varTy) {
      mlir::Value tmpAddr =
          cgf.emitAlloca(vd->getName(), varTy, mappedArg.getLoc(), align,
                         cgf.getBuilder().saveInsertionPoint());
      Address tmp(tmpAddr, varTy, align);
      cgf.getBuilder().createStore(mappedArg.getLoc(), mappedArg, tmp);
      return tmp;
    }

    if (materializeReferenceFromValue && vd->getType()->isReferenceType()) {
      QualType pointeeTy = vd->getType().getNonReferenceType();
      mlir::Type pointeeMemTy = cgf.convertTypeForMem(pointeeTy);
      if (mappedArg.getType() == pointeeMemTy) {
        mlir::OpBuilder::InsertPoint ip = cgf.getBuilder().saveInsertionPoint();
        CharUnits pointeeAlign = cgf.getContext().getTypeAlignInChars(pointeeTy);
        mlir::Value pointeeAddr =
            cgf.emitAlloca(vd->getName(), pointeeMemTy, mappedArg.getLoc(),
                           pointeeAlign, ip);
        Address pointeeStorage(pointeeAddr, pointeeMemTy, pointeeAlign);
        cgf.getBuilder().createStore(mappedArg.getLoc(), mappedArg,
                                     pointeeStorage);

        mlir::Value refAddr =
            cgf.emitAlloca(vd->getName(), varTy, mappedArg.getLoc(), align, ip);
        Address refStorage(refAddr, varTy, align);
        cgf.getBuilder().createStore(mappedArg.getLoc(), pointeeAddr,
                                     refStorage);
        return refStorage;
      }
    }

    cgf.getCIRGenModule().errorNYI(
        vd->getLocation(),
        "OpenMP target mapped variable address materialization");
    return std::nullopt;
  };

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::OpBuilder::InsertionGuard guard(builder);

  llvm::SmallVector<mlir::Type, 4> argTypes;
  llvm::SmallVector<mlir::Location, 4> argLocs;
  for (mlir::Value hasDeviceAddrVar : targetOp.getHasDeviceAddrVars()) {
    argTypes.push_back(hasDeviceAddrVar.getType());
    argLocs.push_back(hasDeviceAddrVar.getLoc());
  }
  for (mlir::Value hostEvalVar : targetOp.getHostEvalVars()) {
    argTypes.push_back(hostEvalVar.getType());
    argLocs.push_back(hostEvalVar.getLoc());
  }
  for (mlir::Value inReductionVar : targetOp.getInReductionVars()) {
    argTypes.push_back(inReductionVar.getType());
    argLocs.push_back(inReductionVar.getLoc());
  }
  for (mlir::Value mapVar : targetOp.getMapVars()) {
    argTypes.push_back(mapVar.getType());
    argLocs.push_back(mapVar.getLoc());
  }
  for (mlir::Value privateVar : targetOp.getPrivateVars()) {
    argTypes.push_back(privateVar.getType());
    argLocs.push_back(privateVar.getLoc());
  }
  builder.createBlock(&targetOp.getRegion(), {}, argTypes, argLocs);
  builder.setInsertionPointToEnd(&targetOp.getRegion().back());

  llvm::SmallVector<std::pair<const VarDecl *, Address>, 4> mappedDeclAddrs;
  mlir::LogicalResult res = mlir::success();
  auto argIface = mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
      targetOp.getOperation());
  auto hasDeviceAddrArgs = argIface.getHasDeviceAddrBlockArgs();
  assert(clauseInfo.hasDeviceAddrIndices.size() ==
             clauseInfo.hasDeviceAddrVars.size() &&
         "mismatched OpenMP target has_device_addr metadata");
  for (auto [index, vd] : llvm::zip_equal(clauseInfo.hasDeviceAddrIndices,
                                          clauseInfo.hasDeviceAddrVars)) {
    assert(index < hasDeviceAddrArgs.size() &&
           "unexpected OpenMP target has_device_addr block arg index");
    std::optional<Address> mappedAddr =
        getMappedVarAddress(vd, hasDeviceAddrArgs[index]);
    if (!mappedAddr)
      return mlir::failure();
    mappedDeclAddrs.emplace_back(vd, *mappedAddr);
  }

  auto mapArgs = argIface.getMapBlockArgs();
  assert(clauseInfo.mapIndices.size() == clauseInfo.mapVars.size() &&
         "mismatched OpenMP target map metadata");
  for (auto [index, vd] : llvm::zip_equal(clauseInfo.mapIndices,
                                          clauseInfo.mapVars)) {
    assert(index < mapArgs.size() &&
           "unexpected OpenMP target map block arg index");
    bool materializeReferenceFromValue =
        std::find(clauseInfo.mapReferenceValueIndices.begin(),
                  clauseInfo.mapReferenceValueIndices.end(),
                  index) != clauseInfo.mapReferenceValueIndices.end();
    std::optional<Address> mappedAddr = getMappedVarAddress(
        vd, mapArgs[index], materializeReferenceFromValue);
    if (!mappedAddr)
      return mlir::failure();
    mappedDeclAddrs.emplace_back(vd, *mappedAddr);
  }

  auto emitTargetBody = [&]() {
    llvm::SmallVector<
        std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>, 4>
        declGuards;
    for (auto [vd, mappedAddr] : mappedDeclAddrs) {
      declGuards.push_back(
          std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(cgf, vd));
      cgf.replaceAddrOfLocalVar(vd, mappedAddr);
    }

    auto privateArgs = argIface.getPrivateBlockArgs();
    assert(clauseInfo.privateIndices.size() == clauseInfo.privateVars.size() &&
           "mismatched OpenMP target private metadata");
    for (auto [index, vd] :
         llvm::zip_equal(clauseInfo.privateIndices, clauseInfo.privateVars)) {
      assert(index < privateArgs.size() &&
             "unexpected OpenMP target private block arg index");
      mlir::Value privateArg = privateArgs[index];
      auto ptrTy = mlir::cast<cir::PointerType>(privateArg.getType());
      declGuards.push_back(
          std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(cgf, vd));
      cgf.replaceAddrOfLocalVar(
          vd, Address(privateArg, ptrTy.getPointee(),
                      cgf.getContext().getDeclAlign(vd)));
    }

    assert(clauseInfo.capturedVLASizeIndices.size() ==
               clauseInfo.capturedVLASizeExprs.size() &&
           "mismatched OpenMP target VLA size metadata");
    llvm::SmallVector<std::unique_ptr<CIRGenFunction::VLASizeMapping>, 4>
        vlaSizeGuards;
    for (auto [index, sizeExpr] : llvm::zip_equal(
             clauseInfo.capturedVLASizeIndices,
             clauseInfo.capturedVLASizeExprs)) {
      assert(index < privateArgs.size() &&
             "unexpected OpenMP target VLA size private block arg index");
      mlir::Value privateArg = privateArgs[index];
      auto ptrTy = mlir::dyn_cast<cir::PointerType>(privateArg.getType());
      if (!ptrTy) {
        cgf.getCIRGenModule().errorNYI(
            targetOp.getLoc(),
            "OpenMP target captured VLA size without pointer storage");
        res = mlir::failure();
        return;
      }

      CharUnits align = cgf.getContext().getTypeAlignInChars(
          cgf.getContext().getSizeType());
      mlir::Value capturedSize = builder.createLoad(
          privateArg.getLoc(), Address(privateArg, ptrTy.getPointee(), align));
      if (capturedSize.getType() != cgf.sizeTy)
        capturedSize = builder.createIntCast(capturedSize, cgf.sizeTy);
      vlaSizeGuards.push_back(
          std::make_unique<CIRGenFunction::VLASizeMapping>(cgf, sizeExpr,
                                                           capturedSize));
    }

    llvm::SmallVector<Address, 4> allocatorStorage;
    llvm::SmallDenseMap<const VarDecl *, Address, 4> allocatorStorageByVar;
    allocatorStorage.reserve(clauseInfo.usesAllocatorVars.size());
    for (const auto &usesAllocator : clauseInfo.usesAllocatorVars) {
      declGuards.push_back(std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(
          cgf, usesAllocator.allocatorVar));
      allocatorStorage.push_back(cgf.createTempAlloca(
          cgf.convertTypeForMem(usesAllocator.allocatorVar->getType()),
          cgf.getContext().getDeclAlign(usesAllocator.allocatorVar),
          cgf.getLoc(usesAllocator.allocatorVar->getLocation()),
          usesAllocator.allocatorVar->getName()));

      mlir::Location loc = cgf.getLoc(usesAllocator.allocatorVar->getLocation());
      const ConstantArrayType *traitsArrayTy =
          cgf.getContext().getAsConstantArrayType(
              usesAllocator.traitsVar->getType().getCanonicalType());
      if (!traitsArrayTy) {
        cgf.getCIRGenModule().errorNYI(
            usesAllocator.traitsVar->getLocation(),
            "OpenMP target uses_allocators traits without constant array type");
        res = mlir::failure();
        return;
      }

      mlir::Value allocator = cgf.emitRuntimeCall(
          loc, getOpenMPInitAllocatorRuntimeFunction(cgf),
          {cgf.emitOpenMPThreadID(usesAllocator.allocatorVar->getLocation(), 0),
           builder.getNullValue(builder.getVoidPtrTy(), loc),
           builder.getSInt32(
               static_cast<int32_t>(traitsArrayTy->getSize().getZExtValue()),
               loc),
           getOpenMPTargetUsesAllocatorTraitsPtr(cgf, loc,
                                                 usesAllocator.traitsVar)});

      mlir::Value storedAllocator =
          builder.createPtrToInt(allocator, cgf.convertType(
                                                usesAllocator.allocatorVar->getType()));
      builder.createStore(loc, storedAllocator, allocatorStorage.back());
      cgf.replaceAddrOfLocalVar(usesAllocator.allocatorVar,
                                allocatorStorage.back());
      allocatorStorageByVar.try_emplace(usesAllocator.allocatorVar,
                                        allocatorStorage.back());
    }

    if (mlir::failed(res))
      return;

    llvm::SmallVector<Address, 4> targetAllocatorPrivateStorage;
    targetAllocatorPrivateStorage.reserve(
        clauseInfo.targetAllocatorPrivateVars.size());
    for (const auto &targetAllocatorPrivate :
         clauseInfo.targetAllocatorPrivateVars) {
      auto allocatorIt =
          allocatorStorageByVar.find(targetAllocatorPrivate.allocatorVar);
      if (allocatorIt == allocatorStorageByVar.end()) {
        cgf.getCIRGenModule().errorNYI(
            targetAllocatorPrivate.privateVar->getLocation(),
            "OpenMP target uses_allocators allocator private without matching "
            "allocator storage");
        res = mlir::failure();
        return;
      }

      std::optional<Address> privateAddr = emitOpenMPTargetAllocatorPrivateAddress(
          cgf, targetAllocatorPrivate.privateVar, allocatorIt->second);
      if (!privateAddr) {
        res = mlir::failure();
        return;
      }

      if (targetAllocatorPrivate.privateCopyVD) {
        std::optional<Address> sourceAddr;
        if (targetAllocatorPrivate.sourceMapIndex >= 0) {
          if (static_cast<size_t>(targetAllocatorPrivate.sourceMapIndex) >=
              mapArgs.size()) {
            cgf.getCIRGenModule().errorNYI(
                targetAllocatorPrivate.privateVar->getLocation(),
                "OpenMP target allocator firstprivate source mapping");
            res = mlir::failure();
            return;
          }

          sourceAddr = getMappedVarAddress(
              targetAllocatorPrivate.privateVar,
              mapArgs[targetAllocatorPrivate.sourceMapIndex]);
        } else {
          sourceAddr = lookupOpenMPAddressableDeclAddress(
              cgf, targetAllocatorPrivate.privateVar);
        }
        if (!sourceAddr) {
          cgf.getCIRGenModule().errorNYI(
              targetAllocatorPrivate.privateVar->getLocation(),
              "OpenMP target allocator firstprivate source address");
          res = mlir::failure();
          return;
        }

        if (mlir::failed(emitOpenMPFirstprivateCopyToAddress(
                cgf, targetAllocatorPrivate.privateVar, *sourceAddr, *privateAddr,
                targetAllocatorPrivate.privateCopyVD,
                targetAllocatorPrivate.initVD))) {
          res = mlir::failure();
          return;
        }
      }

      declGuards.push_back(std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(
          cgf, targetAllocatorPrivate.privateVar));
      cgf.replaceAddrOfLocalVar(targetAllocatorPrivate.privateVar, *privateAddr);
      targetAllocatorPrivateStorage.push_back(*privateAddr);
    }

    auto emitBodyWithMappedThis = [&]() {
      if (!clauseInfo.targetThisMapIndex) {
        res = bodyEmitter();
        return;
      }

      unsigned thisMapIndex = *clauseInfo.targetThisMapIndex;
      if (thisMapIndex >= mapArgs.size()) {
        cgf.getCIRGenModule().errorNYI(
            targetOp.getLoc(), "OpenMP target mapped 'this' block arg lookup");
        res = mlir::failure();
        return;
      }

      mlir::Value mappedThisArg = mapArgs[thisMapIndex];
      auto ptrTy = mlir::dyn_cast<cir::PointerType>(mappedThisArg.getType());
      if (!ptrTy) {
        cgf.getCIRGenModule().errorNYI(
            targetOp.getLoc(),
            "OpenMP target mapped 'this' without pointer type");
        res = mlir::failure();
        return;
      }

      CharUnits thisAlign = cgf.loadCXXThisAddress().getAlignment();
      CIRGenFunction::CXXThisOverrideScope thisScope(
          cgf, Address(mappedThisArg, ptrTy.getPointee(), thisAlign),
          /*overrideABIThis=*/true);
      res = bodyEmitter();
    };

    emitBodyWithMappedThis();

    if (mlir::failed(res))
      return;

    for (auto [targetAllocatorPrivate, storage] :
         llvm::zip_equal(llvm::reverse(clauseInfo.targetAllocatorPrivateVars),
                         llvm::reverse(targetAllocatorPrivateStorage))) {
      auto allocatorIt =
          allocatorStorageByVar.find(targetAllocatorPrivate.allocatorVar);
      assert(allocatorIt != allocatorStorageByVar.end() &&
             "missing OpenMP target uses_allocators allocator storage");
      emitOpenMPTargetAllocatorPrivateFree(
          cgf, targetAllocatorPrivate.privateVar, storage, allocatorIt->second);
    }

    for (auto [usesAllocator, emission] :
         llvm::zip_equal(llvm::reverse(clauseInfo.usesAllocatorVars),
                         llvm::reverse(allocatorStorage))) {
      mlir::Location loc = cgf.getLoc(usesAllocator.allocatorVar->getLocation());
      mlir::Value allocatorValue = builder.createLoad(loc, emission);
      allocatorValue =
          builder.createIntToPtr(allocatorValue, builder.getVoidPtrTy());
      cgf.emitRuntimeCall(
          loc, getOpenMPDestroyAllocatorRuntimeFunction(cgf),
          {cgf.emitOpenMPThreadID(usesAllocator.allocatorVar->getLocation(), 0),
           allocatorValue});
    }
  };

  if (wrapInScope) {
    cir::ScopeOp::create(
        builder, begin, /*scopeBuilder=*/
        [&](mlir::OpBuilder &, mlir::Location) {
          CIRGenFunction::LexicalScope ls{cgf, begin,
                                          builder.getInsertionBlock()};
          emitTargetBody();
        });
  } else {
    emitTargetBody();
  }

  mlir::omp::TerminatorOp::create(builder, end);
  return res;
}

static mlir::LogicalResult emitOMPTaskloopRegionBody(
    CIRGenFunction &cgf, mlir::omp::TaskloopOp &taskloopOp,
    const CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo,
    llvm::function_ref<mlir::LogicalResult()> bodyEmitter) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::OpBuilder::InsertionGuard guard(builder);

  llvm::SmallVector<mlir::Type, 4> argTypes;
  llvm::SmallVector<mlir::Location, 4> argLocs;
  for (mlir::Value inReductionVar : taskloopOp.getInReductionVars()) {
    argTypes.push_back(inReductionVar.getType());
    argLocs.push_back(inReductionVar.getLoc());
  }
  for (mlir::Value privateVar : taskloopOp.getPrivateVars()) {
    argTypes.push_back(privateVar.getType());
    argLocs.push_back(privateVar.getLoc());
  }
  for (mlir::Value reductionVar : taskloopOp.getReductionVars()) {
    argTypes.push_back(reductionVar.getType());
    argLocs.push_back(reductionVar.getLoc());
  }
  builder.createBlock(&taskloopOp.getRegion(), {}, argTypes, argLocs);
  builder.setInsertionPointToEnd(&taskloopOp.getRegion().back());

  llvm::SmallVector<std::pair<const VarDecl *, Address>, 4> savedDecls;
  llvm::SmallVector<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>, 4>
      reductionDeclGuards;
  llvm::SmallVector<std::unique_ptr<OpenMPTaskReductionVLASizeScope>, 4>
      reductionVLAScopes;
  auto argIface =
      mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(taskloopOp.getOperation());
  auto privateArgs = argIface.getPrivateBlockArgs();
  assert(clauseInfo.privateIndices.size() == clauseInfo.privateVars.size() &&
         "mismatched OpenMP private metadata");
  for (auto [index, vd] :
       llvm::zip_equal(clauseInfo.privateIndices, clauseInfo.privateVars)) {
    assert(index < privateArgs.size() &&
           "unexpected OpenMP private block arg index");
    mlir::Value privateArg = privateArgs[index];
    auto ptrTy = mlir::cast<cir::PointerType>(privateArg.getType());
    if (auto it = cgf.localDeclMap.find(vd); it != cgf.localDeclMap.end())
      savedDecls.emplace_back(vd, it->second);
    cgf.replaceAddrOfLocalVar(
        vd, Address(privateArg, ptrTy.getPointee(),
                    cgf.getContext().getDeclAlign(vd)));
  }

  remapOpenMPInReductionDecls(cgf, taskloopOp.getOperation(), clauseInfo,
                              reductionDeclGuards,
                              taskloopOp.getInReductionSyms(),
                              &reductionVLAScopes);
  emitOpenMPTaskReductionArraySectionRuntimeSizeScopes(
      cgf, taskloopOp.getOperation(), clauseInfo.inReductionRuntimeSizes,
      taskloopOp.getInReductionSyms(), reductionVLAScopes);
  remapOpenMPReductionDecls(cgf, taskloopOp.getOperation(), clauseInfo,
                            reductionDeclGuards);

  mlir::LogicalResult res = bodyEmitter();

  for (auto [vd, addr] : llvm::reverse(savedDecls))
    cgf.replaceAddrOfLocalVar(vd, addr);

  return res;
}

static Address lookupOpenMPCopyinMasterAddress(
    ArrayRef<OpenMPCopyinMasterAddr> masterAddrs, const VarDecl *vd) {
  vd = vd->getCanonicalDecl();
  for (auto [candidate, addr] : masterAddrs) {
    if (candidate == vd)
      return addr;
  }
  return Address::invalid();
}

static void emitOpenMPCopyinArrayElementwiseAssignment(
    CIRGenFunction &cgf, Address destAddr, Address srcAddr, QualType originalTy,
    const VarDecl *destVD, const VarDecl *srcVD, const Expr *assignmentOp) {
  const auto *arrayTy = originalTy->castAsArrayTypeUnsafe();
  mlir::Location loc = cgf.getLoc(assignmentOp->getBeginLoc());
  CIRGenFunction::SourceLocRAIIObject copyLoc(
      cgf, cgf.getLoc(assignmentOp->getSourceRange()));

  QualType elementType;
  mlir::Value numElements = cgf.emitArrayLength(arrayTy, elementType, destAddr);
  mlir::Type cirElementTy = cgf.convertTypeForMem(elementType);
  destAddr = destAddr.withElementType(cgf.getBuilder(), cirElementTy);
  srcAddr = srcAddr.withElementType(cgf.getBuilder(), cirElementTy);

  mlir::Value destBegin =
      cgf.getBuilder().createPtrBitcast(destAddr.getPointer(), cirElementTy);
  mlir::Value srcBegin =
      cgf.getBuilder().createPtrBitcast(srcAddr.getPointer(), cirElementTy);
  mlir::Value destEnd =
      cgf.getBuilder().createPtrStride(loc, destBegin, numElements);
  mlir::Value isNotEmpty = cgf.getBuilder().createCompare(
      loc, cir::CmpOpKind::ne, destBegin, destEnd);

  CharUnits elementSize = cgf.getContext().getTypeSizeInChars(elementType);
  CharUnits destElementAlign =
      destAddr.getAlignment().alignmentOfArrayElement(elementSize);
  CharUnits srcElementAlign =
      srcAddr.getAlignment().alignmentOfArrayElement(elementSize);

  cir::IfOp::create(
      cgf.getBuilder(), loc, isNotEmpty, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
        mlir::OpBuilder::InsertionGuard guard(cgf.getBuilder());
        cgf.getBuilder().setInsertionPointToEnd(thenBuilder.getInsertionBlock());

        auto ptrToElementTy = cgf.getBuilder().getPointerTo(cirElementTy);
        auto ptrToPtrTy = cgf.getBuilder().getPointerTo(ptrToElementTy);
        auto ptrAlignAttr =
            cgf.getBuilder().getAlignmentAttr(cgf.getPointerAlign().getQuantity());
        mlir::Value destCurSlot = cgf.getBuilder().createAlloca(
            thenLoc, ptrToPtrTy, ptrToElementTy, "__omp_copyin_dest",
            ptrAlignAttr);
        mlir::Value srcCurSlot = cgf.getBuilder().createAlloca(
            thenLoc, ptrToPtrTy, ptrToElementTy, "__omp_copyin_src",
            ptrAlignAttr);
        Address destCurAddr(destCurSlot, ptrToElementTy, cgf.getPointerAlign());
        Address srcCurAddr(srcCurSlot, ptrToElementTy, cgf.getPointerAlign());
        cgf.getBuilder().createStore(thenLoc, destBegin, destCurAddr);
        cgf.getBuilder().createStore(thenLoc, srcBegin, srcCurAddr);

        cgf.getBuilder().createDoWhile(
            thenLoc,
            /*condBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location loopLoc) {
              mlir::Value curDest = cgf.getBuilder().createLoad(loopLoc, destCurAddr);
              mlir::Value cmp = cgf.getBuilder().createCompare(
                  loopLoc, cir::CmpOpKind::ne, curDest, destEnd);
              cgf.getBuilder().createCondition(cmp);
            },
            /*bodyBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location loopLoc) {
              mlir::Value curDest = cgf.getBuilder().createLoad(loopLoc, destCurAddr);
              mlir::Value curSrc = cgf.getBuilder().createLoad(loopLoc, srcCurAddr);
              Address destElementAddr(curDest, cirElementTy, destElementAlign);
              Address srcElementAddr(curSrc, cirElementTy, srcElementAlign);

              CIRGenFunction::DeclMapRevertingRAII destGuard(cgf, destVD);
              CIRGenFunction::DeclMapRevertingRAII srcGuard(cgf, srcVD);
              cgf.setAddrOfLocalVar(destVD, destElementAddr);
              cgf.setAddrOfLocalVar(srcVD, srcElementAddr);
              cgf.emitIgnoredExpr(assignmentOp);

              mlir::Value one = cgf.getBuilder().getConstInt(
                  loopLoc, mlir::cast<cir::IntType>(cgf.ptrDiffTy), 1);
              mlir::Value nextDest =
                  cgf.getBuilder().createPtrStride(loopLoc, curDest, one);
              mlir::Value nextSrc =
                  cgf.getBuilder().createPtrStride(loopLoc, curSrc, one);
              cgf.getBuilder().createStore(loopLoc, nextDest, destCurAddr);
              cgf.getBuilder().createStore(loopLoc, nextSrc, srcCurAddr);
              cgf.getBuilder().createYield(loopLoc);
            });

        cir::YieldOp::create(thenBuilder, thenLoc);
      });
}

static void emitOpenMPCopyinAssignment(CIRGenFunction &cgf, Address destAddr,
                                       Address srcAddr, QualType originalTy,
                                       const VarDecl *destVD,
                                       const VarDecl *srcVD,
                                       const Expr *assignmentOp) {
  CIRGenFunction::SourceLocRAIIObject copyLoc(
      cgf, cgf.getLoc(assignmentOp->getSourceRange()));

  if (originalTy->isArrayType()) {
    const auto *assign = dyn_cast<BinaryOperator>(assignmentOp);
    if (assign && assign->getOpcode() == BO_Assign) {
      cgf.emitAggregateCopy(cgf.makeAddrLValue(destAddr, originalTy),
                            cgf.makeAddrLValue(srcAddr, originalTy), originalTy,
                            AggValueSlot::DoesNotOverlap);
      return;
    }

    emitOpenMPCopyinArrayElementwiseAssignment(cgf, destAddr, srcAddr,
                                               originalTy, destVD, srcVD,
                                               assignmentOp);
    return;
  }

  CIRGenFunction::DeclMapRevertingRAII destGuard(cgf, destVD);
  CIRGenFunction::DeclMapRevertingRAII srcGuard(cgf, srcVD);
  cgf.setAddrOfLocalVar(destVD, destAddr);
  cgf.setAddrOfLocalVar(srcVD, srcAddr);
  cgf.emitIgnoredExpr(assignmentOp);
}

static bool emitOpenMPCopyinClause(
    CIRGenFunction &cgf, const OMPExecutableDirective &s,
    ArrayRef<OpenMPCopyinMasterAddr> masterAddrs) {
  llvm::SmallPtrSet<const VarDecl *, 4> copiedVars;
  llvm::SmallVector<OpenMPCopyinItem, 4> items;

  for (const auto *clause : s.getClausesOfKind<OMPCopyinClause>()) {
    auto varIt = clause->varlist_begin();
    auto srcIt = clause->source_exprs().begin();
    auto destIt = clause->destination_exprs().begin();
    for (const Expr *assignmentOp : clause->assignment_ops()) {
      const VarDecl *vd = getOpenMPVarDeclFromExpr(*varIt);
      if (!vd) {
        cgf.getCIRGenModule().errorNYI(
            (*varIt)->getSourceRange(),
            "OpenMP copyin clause expression without an addressable variable");
        continue;
      }

      const VarDecl *canonicalVD = vd->getCanonicalDecl();
      if (copiedVars.insert(canonicalVD).second) {
        Address masterAddr =
            lookupOpenMPCopyinMasterAddress(masterAddrs, canonicalVD);
        assert(masterAddr.isValid() && "missing OpenMP copyin master address");

        const VarDecl *srcVD = getOpenMPVarDeclFromExpr(*srcIt);
        const VarDecl *destVD = getOpenMPVarDeclFromExpr(*destIt);
        assert(srcVD && destVD &&
               "expected OpenMP copyin source/destination pseudo variables");

        items.push_back({*varIt, canonicalVD, srcVD, destVD, assignmentOp,
                         vd->getType(), masterAddr});
      }

      ++varIt;
      ++srcIt;
      ++destIt;
    }
  }

  if (items.empty())
    return false;

  mlir::Location loc = cgf.getLoc(s.getBeginLoc());
  Address firstPrivateAddr = cgf.emitLValue(items.front().varExpr).getAddress();
  mlir::Value needsCopy = cgf.getBuilder().createCompare(
      loc, cir::CmpOpKind::ne, items.front().masterAddr.getPointer(),
      firstPrivateAddr.getPointer());
  cir::IfOp::create(
      cgf.getBuilder(), loc, needsCopy, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
        mlir::OpBuilder::InsertionGuard guard(cgf.getBuilder());
        cgf.getBuilder().setInsertionPointToEnd(thenBuilder.getInsertionBlock());
        for (const auto &[varExpr, canonicalVD, srcVD, destVD, assignmentOp, type,
                          masterAddr] : items) {
          (void)canonicalVD;
          Address privateAddr =
              varExpr == items.front().varExpr
                  ? firstPrivateAddr
                  : cgf.emitLValue(varExpr).getAddress();
          emitOpenMPCopyinAssignment(cgf, privateAddr, masterAddr, type, destVD,
                                     srcVD, assignmentOp);
        }
        cir::YieldOp::create(thenBuilder, thenLoc);
      });

  mlir::omp::BarrierOp::create(cgf.getBuilder(), loc);
  return true;
}

static cir::FuncOp getOrCreateOpenMPCopyprivateCopyFunction(
    CIRGenFunction &cgf, CIRGenBuilderTy &builder, mlir::Location loc,
    const VarDecl *vd, mlir::Type elementTy, const VarDecl *srcVD,
    const VarDecl *destVD, const Expr *assignmentOp) {
  mlir::ModuleOp module = cgf.getCIRGenModule().getModule();
  std::string baseName =
      getOpenMPPrivatizerBaseName(cgf, vd, "__omp_copyprivate_");
  std::string name = baseName;
  unsigned suffix = 0;
  while (module.lookupSymbol(name))
    name = (baseName + "." + llvm::Twine(++suffix)).str();

  auto voidTy = builder.getVoidTy();
  auto pointerTy = builder.getPointerTo(elementTy);
  auto fnTy = cir::FuncType::get({pointerTy, pointerTy}, voidTy);
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());
  cir::FuncOp fn = cir::FuncOp::create(builder, loc, name, fnTy);
  fn.setPrivate();

  mlir::Block *entry = fn.addEntryBlock();
  builder.setInsertionPointToStart(entry);

  CIRGenFunction helperCGF(cgf.getCIRGenModule(), builder,
                           /*suppressNewContext=*/true);
  CIRGenFunction::SourceLocRAIIObject copyLoc(helperCGF, loc);
  CharUnits align = cgf.getContext().getDeclAlign(vd);
  Address dstAddr(fn.getArgument(0), elementTy, align);
  Address srcAddr(fn.getArgument(1), elementTy, align);

  emitOpenMPLastprivateCopy(helperCGF, vd, srcAddr, dstAddr, srcVD, destVD,
                            assignmentOp);
  cir::ReturnOp::create(builder, loc);
  return fn;
}

struct OpenMPSimpleCanonicalForInfo {
  const ForStmt *forStmt = nullptr;
  const VarDecl *iv = nullptr;
  const Expr *lowerBound = nullptr;
  const Expr *upperBound = nullptr;
  bool upperInclusive = false;
};

struct OpenMPSimpleGeneratedLoopInfo {
  const Stmt *body = nullptr;
  const VarDecl *logicalIV = nullptr;
  llvm::SmallVector<const VarDecl *, 4> sourceIVs;
  const Expr *preCond = nullptr;
  const Expr *lastIteration = nullptr;
  llvm::SmallVector<const Expr *, 4> updates;
  llvm::SmallVector<const Expr *, 4> finals;
};

struct OpenMPCollapsedGeneratedSimdInfo {
  const Stmt *body = nullptr;
  const VarDecl *logicalIV = nullptr;
  llvm::SmallVector<const VarDecl *, 4> sourceIVs;
  const Expr *preCond = nullptr;
  const Expr *lastIteration = nullptr;
  llvm::SmallVector<const Expr *, 4> updates;
  llvm::SmallVector<const Expr *, 4> finals;
};

static std::optional<OpenMPSimpleCanonicalForInfo>
getOpenMPSimpleCanonicalForInfo(const OMPLoopDirective &s) {
  // collapse(N) with N > 1 requires multi-loop collapsing which is NYI.
  if (s.getLoopsNumber() > 1)
    return std::nullopt;

  const Stmt *loopStmt = s.getRawStmt();
  while (loopStmt) {
    if (const auto *captured = dyn_cast<CapturedStmt>(loopStmt)) {
      loopStmt = captured->getCapturedStmt();
      continue;
    }

    if (const auto *canonicalLoop = dyn_cast<OMPCanonicalLoop>(loopStmt)) {
      loopStmt = canonicalLoop->getLoopStmt();
      continue;
    }

    if (const auto *compoundStmt = dyn_cast<CompoundStmt>(loopStmt)) {
      if (compoundStmt->size() != 1 || !compoundStmt->body_front())
        break;
      loopStmt = compoundStmt->body_front();
      continue;
    }

    break;
  }

  const auto *forStmt = dyn_cast_or_null<ForStmt>(loopStmt);
  if (!forStmt)
    return std::nullopt;

  const VarDecl *iv = nullptr;
  const Expr *lowerBound = nullptr;
  const Stmt *initStmt = forStmt->getInit();
  if (const auto *declStmt = dyn_cast_or_null<DeclStmt>(initStmt)) {
    if (!declStmt->isSingleDecl())
      return std::nullopt;
    iv = dyn_cast<VarDecl>(declStmt->getSingleDecl());
    if (!iv || !iv->getInit())
      return std::nullopt;
    lowerBound = iv->getInit();
  } else {
    const auto *initExpr = dyn_cast_or_null<Expr>(initStmt);
    if (!initExpr)
      return std::nullopt;
    const auto *assign =
        dyn_cast<BinaryOperator>(initExpr->IgnoreImpCasts());
    if (!assign || !assign->isAssignmentOp())
      return std::nullopt;
    iv = getOpenMPVarDeclFromExpr(assign->getLHS());
    if (!iv)
      return std::nullopt;
    lowerBound = assign->getRHS();
  }

  const auto *cond =
      dyn_cast_or_null<BinaryOperator>(forStmt->getCond()->IgnoreImpCasts());
  if (!cond || !isOpenMPVarExpr(cond->getLHS(), iv))
    return std::nullopt;

  bool upperInclusive = false;
  switch (cond->getOpcode()) {
  case BO_LT:
    upperInclusive = false;
    break;
  case BO_LE:
    upperInclusive = true;
    break;
  default:
    return std::nullopt;
  }

  const Expr *incExpr = forStmt->getInc();
  if (!incExpr)
    return std::nullopt;
  incExpr = incExpr->IgnoreImpCasts();
  if (const auto *unaryInc = dyn_cast<UnaryOperator>(incExpr)) {
    if ((unaryInc->getOpcode() != UO_PreInc &&
         unaryInc->getOpcode() != UO_PostInc) ||
        !isOpenMPVarExpr(unaryInc->getSubExpr(), iv))
      return std::nullopt;
  } else if (const auto *assign = dyn_cast<BinaryOperator>(incExpr)) {
    if (!assign->isAssignmentOp() || !isOpenMPVarExpr(assign->getLHS(), iv))
      return std::nullopt;
    const auto *rhs = dyn_cast<BinaryOperator>(assign->getRHS()->IgnoreImpCasts());
    if (!rhs || rhs->getOpcode() != BO_Add || !isOpenMPVarExpr(rhs->getLHS(), iv))
      return std::nullopt;
    const auto *step = dyn_cast<IntegerLiteral>(rhs->getRHS()->IgnoreImpCasts());
    if (!step || step->getValue() != 1)
      return std::nullopt;
  } else {
    return std::nullopt;
  }

  const Expr *upperBound = cond->getRHS();

  // Reject non-integer bounds — fall through to the generated-loop path which
  // uses Sema-normalized integer bounds instead.
  QualType lbTy = lowerBound->getType();
  QualType ubTy = upperBound->getType();
  if ((!lbTy->isIntegerType() && !lbTy->isEnumeralType()) ||
      (!ubTy->isIntegerType() && !ubTy->isEnumeralType()))
    return std::nullopt;

  return OpenMPSimpleCanonicalForInfo{forStmt, iv, lowerBound, upperBound,
                                      upperInclusive};
}

static std::optional<OpenMPSimpleGeneratedLoopInfo>
getOpenMPSimpleGeneratedLoopInfo(const OMPLoopDirective &s) {
  const Stmt *body = s.getBody();
  if (!body)
    return std::nullopt;

  const VarDecl *logicalIV = getOpenMPVarDeclFromExpr(s.getIterationVariable());
  if (!logicalIV)
    return std::nullopt;

  ArrayRef<Expr *> counters = s.counters();
  ArrayRef<Expr *> updates = s.updates();
  ArrayRef<Expr *> finals = s.finals();
  if (counters.size() != s.getLoopsNumber() ||
      updates.size() != s.getLoopsNumber() ||
      finals.size() != s.getLoopsNumber())
    return std::nullopt;

  OpenMPSimpleGeneratedLoopInfo info;
  info.body = body;
  info.logicalIV = logicalIV;
  info.preCond = s.getPreCond();
  info.lastIteration = s.getLastIteration();
  if (!info.preCond || !info.lastIteration)
    return std::nullopt;
  info.sourceIVs.reserve(counters.size());
  info.updates.reserve(updates.size());
  info.finals.reserve(finals.size());
  for (Expr *counter : counters) {
    const VarDecl *sourceIV = getOpenMPVarDeclFromExpr(counter);
    if (!sourceIV)
      return std::nullopt;
    info.sourceIVs.push_back(sourceIV);
  }
  for (Expr *update : updates)
    info.updates.push_back(update);
  for (Expr *final : finals)
    info.finals.push_back(final);

  QualType logicalIVTy = logicalIV->getType();
  if (!logicalIVTy->isIntegerType() && !logicalIVTy->isEnumeralType())
    return std::nullopt;

  return info;
}

static std::optional<OpenMPCollapsedGeneratedSimdInfo>
getOpenMPCollapsedGeneratedSimdInfo(const OMPLoopDirective &s) {
  if (s.getLoopsNumber() <= 1)
    return std::nullopt;

  const Stmt *body = s.getBody();
  if (!body)
    return std::nullopt;

  const VarDecl *logicalIV = getOpenMPVarDeclFromExpr(s.getIterationVariable());
  if (!logicalIV || !s.getPreCond() || !s.getLastIteration())
    return std::nullopt;

  QualType logicalIVTy = logicalIV->getType();
  if (!logicalIVTy->isIntegerType() && !logicalIVTy->isEnumeralType())
    return std::nullopt;

  ArrayRef<Expr *> counters = s.counters();
  ArrayRef<Expr *> updates = s.updates();
  ArrayRef<Expr *> finals = s.finals();
  unsigned numLoops = s.getLoopsNumber();
  if (counters.size() != numLoops || updates.size() != numLoops ||
      finals.size() != numLoops)
    return std::nullopt;

  OpenMPCollapsedGeneratedSimdInfo info;
  info.body = body;
  info.logicalIV = logicalIV;
  info.preCond = s.getPreCond();
  info.lastIteration = s.getLastIteration();
  info.sourceIVs.reserve(numLoops);
  info.updates.reserve(numLoops);
  info.finals.reserve(numLoops);
  for (auto [counter, update, final] : llvm::zip_equal(counters, updates, finals)) {
    const VarDecl *sourceIV = getOpenMPVarDeclFromExpr(counter);
    if (!sourceIV)
      return std::nullopt;
    info.sourceIVs.push_back(sourceIV);
    info.updates.push_back(update);
    info.finals.push_back(final);
  }
  return info;
}

static mlir::Value emitOpenMPIntegerExpr(CIRGenFunction &cgf, const Expr *e) {
  QualType ty = e->getType();
  if (!ty->isIntegerType() && !ty->isEnumeralType()) {
    cgf.getCIRGenModule().errorNYI(e->getSourceRange(),
                                   "OpenMP loop bound/step with non-integer "
                                   "expression type");
    return {};
  }

  mlir::Value expr = cgf.emitScalarExpr(e);
  mlir::Location exprLoc = cgf.getLoc(e->getBeginLoc());
  auto conversionOp = mlir::UnrealizedConversionCastOp::create(
      cgf.getBuilder(), exprLoc, getOpenMPIntegerType(cgf, ty), expr);
  return conversionOp.getResult(0);
}

struct OpenMPTargetParallelHostEvalClauseInfo {
  const OMPIfClause *IfClause = nullptr;
  const OMPNumThreadsClause *NumThreadsClause = nullptr;
};

static void materializeOpenMPTargetParallelHostEvalClauseExprs(
    CIRGenFunction &cgf,
    const OpenMPTargetParallelHostEvalClauseInfo &clauseInfo,
    llvm::SmallPtrSetImpl<const VarDecl *> &materializedClauseVars) {
  if (clauseInfo.IfClause)
    materializeOpenMPCapturedClauseExpr(cgf, clauseInfo.IfClause->getCondition(),
                                        materializedClauseVars);
  if (clauseInfo.NumThreadsClause)
    materializeOpenMPCapturedClauseExpr(
        cgf, clauseInfo.NumThreadsClause->getNumThreads(),
        materializedClauseVars);
}

static llvm::SmallVector<mlir::Value, 2> emitOpenMPTargetParallelHostEvalVars(
    CIRGenFunction &cgf,
    const OpenMPTargetParallelHostEvalClauseInfo &clauseInfo) {
  llvm::SmallVector<mlir::Value, 2> hostEvalVars;
  if (clauseInfo.IfClause)
    hostEvalVars.push_back(
        emitOpenMPBoolExpr(cgf, clauseInfo.IfClause->getCondition()));
  if (clauseInfo.NumThreadsClause)
    hostEvalVars.push_back(
        emitOpenMPIntegerExpr(cgf, clauseInfo.NumThreadsClause->getNumThreads()));
  return hostEvalVars;
}

static void bindOpenMPTargetParallelHostEvalArgs(
    mlir::omp::TargetOp targetOp, mlir::omp::ParallelOp parallelOp,
    const OpenMPTargetParallelHostEvalClauseInfo &clauseInfo) {
  auto targetArgIface =
      mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(targetOp.getOperation());
  auto hostEvalArgs = targetArgIface.getHostEvalBlockArgs();
  unsigned nextHostEvalArg = 0;

  if (clauseInfo.IfClause) {
    assert(nextHostEvalArg < hostEvalArgs.size() &&
           "missing OpenMP target host_eval block arg for parallel if");
    parallelOp.getIfExprMutable().assign(hostEvalArgs[nextHostEvalArg++]);
  }
  if (clauseInfo.NumThreadsClause) {
    assert(nextHostEvalArg < hostEvalArgs.size() &&
           "missing OpenMP target host_eval block arg for num_threads");
    parallelOp.getNumThreadsMutable().assign(hostEvalArgs[nextHostEvalArg++]);
    if (clauseInfo.NumThreadsClause->getModifier() == OMPC_NUMTHREADS_strict) {
      // Mirror the modifier into
      // the CIR-namespaced discardable attribute that the OpenMP-to-LLVM
      // translator reads (see VisitOMPNumThreadsClause for full
      // rationale). The TD setter is kept for the dialect verifier.
      parallelOp.setNumThreadsModifier(mlir::omp::NumThreadsModifier::strict);
      parallelOp->setAttr(
          "cir.omp.num_threads_modifier",
          mlir::omp::NumThreadsModifierAttr::get(
              parallelOp.getContext(), mlir::omp::NumThreadsModifier::strict));
    }
  }
}

static std::optional<mlir::IntegerType>
getOpenMPWorksharingLoopType(CIRGenFunction &cgf, mlir::Value lowerBound,
                             mlir::Value upperBound) {
  auto getWidth = [](mlir::Type ty) -> std::optional<unsigned> {
    if (auto intTy = mlir::dyn_cast<mlir::IntegerType>(ty))
      return intTy.getWidth();
    return std::nullopt;
  };

  std::optional<unsigned> lowerWidth = getWidth(lowerBound.getType());
  std::optional<unsigned> upperWidth = getWidth(upperBound.getType());
  if (!lowerWidth || !upperWidth)
    return std::nullopt;

  unsigned width = std::max({32u, *lowerWidth, *upperWidth});
  if (width <= 32)
    width = 32;
  else if (width <= 64)
    width = 64;
  else
    return std::nullopt;

  return mlir::IntegerType::get(&cgf.getMLIRContext(), width);
}

static mlir::Value castOpenMPValueToBuiltinInt(CIRGenFunction &cgf,
                                               mlir::Location loc,
                                               mlir::Value value,
                                               mlir::IntegerType dstTy,
                                               bool isSigned) {
  CIRGenBuilderTy &builder = cgf.getBuilder();

  auto castBuiltinInt = [&](mlir::Value intValue) -> mlir::Value {
    auto srcTy = mlir::cast<mlir::IntegerType>(intValue.getType());
    if (srcTy == dstTy)
      return intValue;

    if (srcTy.getWidth() < dstTy.getWidth()) {
      if (isSigned)
        return mlir::LLVM::SExtOp::create(builder, loc, dstTy, intValue);
      return mlir::LLVM::ZExtOp::create(builder, loc, dstTy, intValue);
    }

    return mlir::LLVM::TruncOp::create(builder, loc, dstTy, intValue);
  };

  if (mlir::isa<mlir::IntegerType>(value.getType()))
    return castBuiltinInt(value);

  if (auto cirIntTy = mlir::dyn_cast<cir::IntType>(value.getType())) {
    auto cirDstTy =
        cir::IntType::get(&cgf.getMLIRContext(), dstTy.getWidth(), isSigned);
    if (cirIntTy != cirDstTy)
      value = builder.createIntCast(value, cirDstTy);
    return mlir::UnrealizedConversionCastOp::create(builder, loc, dstTy, value)
        .getResult(0);
  }

  if (mlir::isa<cir::BoolType>(value.getType())) {
    mlir::Value i1 =
        mlir::UnrealizedConversionCastOp::create(builder, loc,
                                                 builder.getI1Type(), value)
            .getResult(0);
    return castBuiltinInt(i1);
  }

  cgf.getCIRGenModule().errorNYI(SourceRange(),
                                 "OpenMP loop bound integer conversion");
  return {};
}

static mlir::Value emitOpenMPInt64Expr(CIRGenFunction &cgf, const Expr *e) {
  QualType int64Ty = cgf.getContext().getIntTypeForBitwidth(
      /*DestWidth=*/64, /*Signed=*/true);
  mlir::Value expr = cgf.emitScalarExpr(e);
  expr = cgf.emitScalarConversion(expr, e->getType(), int64Ty,
                                  e->getExprLoc());

  mlir::Location exprLoc = cgf.getLoc(e->getBeginLoc());
  auto conversionOp = mlir::UnrealizedConversionCastOp::create(
      cgf.getBuilder(), exprLoc,
      mlir::IntegerType::get(&cgf.getMLIRContext(), 64), expr);
  return conversionOp.getResult(0);
}

static mlir::omp::ClauseCancellationConstructType
getOpenMPCancellationConstructType(CIRGenFunction &cgf, SourceRange range,
                                   OpenMPDirectiveKind region) {
  switch (region) {
  case llvm::omp::OMPD_for:
    return mlir::omp::ClauseCancellationConstructType::Loop;
  case llvm::omp::OMPD_parallel:
    return mlir::omp::ClauseCancellationConstructType::Parallel;
  case llvm::omp::OMPD_sections:
    return mlir::omp::ClauseCancellationConstructType::Sections;
  case llvm::omp::OMPD_taskgroup:
    return mlir::omp::ClauseCancellationConstructType::Taskgroup;
  default:
    cgf.getCIRGenModule().errorNYI(
        range, "OpenMP cancellation region",
        llvm::omp::getOpenMPDirectiveName(region));
    return mlir::omp::ClauseCancellationConstructType::Parallel;
  }
}

static mlir::Value createOpenMPIntegerConstant(CIRGenFunction &cgf,
                                               mlir::Location loc,
                                               mlir::Type ty, int64_t value) {
  auto intTy = mlir::dyn_cast<mlir::IntegerType>(ty);
  assert(intTy && "expected builtin integer type for OpenMP loop constant");
  auto cirTy = cir::IntType::get(&cgf.getMLIRContext(), intTy.getWidth(),
                                 /*isSigned=*/value < 0);
  mlir::Value cirConst = cgf.getBuilder().getConstInt(loc, cirTy, value);
  auto conversionOp =
      mlir::UnrealizedConversionCastOp::create(cgf.getBuilder(), loc, intTy,
                                               cirConst);
  return conversionOp.getResult(0);
}

static mlir::Value castOpenMPValueToCIRType(CIRGenFunction &cgf,
                                            mlir::Location loc,
                                            mlir::Value value,
                                            mlir::Type targetType) {
  auto conversionOp = mlir::UnrealizedConversionCastOp::create(
      cgf.getBuilder(), loc, targetType, value);
  return conversionOp.getResult(0);
}

static std::optional<llvm::omp::RTLDependenceKindTy>
getOpenMPRTLDependKind(OpenMPDependClauseKind kind) {
  switch (kind) {
  case OMPC_DEPEND_in:
    return llvm::omp::RTLDependenceKindTy::DepIn;
  case OMPC_DEPEND_out:
  case OMPC_DEPEND_inout:
    return llvm::omp::RTLDependenceKindTy::DepInOut;
  case OMPC_DEPEND_mutexinoutset:
    return llvm::omp::RTLDependenceKindTy::DepMutexInOutSet;
  case OMPC_DEPEND_inoutset:
    return llvm::omp::RTLDependenceKindTy::DepInOutSet;
  case OMPC_DEPEND_outallmemory:
    return llvm::omp::RTLDependenceKindTy::DepOmpAllMem;
  case OMPC_DEPEND_depobj:
  case OMPC_DEPEND_source:
  case OMPC_DEPEND_sink:
  case OMPC_DEPEND_inoutallmemory:
  case OMPC_DEPEND_unknown:
    return std::nullopt;
  }

  llvm_unreachable("unexpected OpenMP depend kind");
}

struct OpenMPInteropDependInfo {
  mlir::Value numDependences;
  mlir::Value dependenceList;
};

static mlir::Value castInteropIntToSInt32(CIRGenFunction &cgf,
                                          mlir::Location loc,
                                          mlir::Value value) {
  auto s32Ty = cgf.getBuilder().getSInt32Ty();
  if (value.getType() == s32Ty)
    return value;
  return cgf.getBuilder().createCast(loc, cir::CastKind::integral, value,
                                     s32Ty);
}

static std::optional<OpenMPInteropDependInfo>
emitOpenMPInteropDependInfo(CIRGenFunction &cgf,
                            const OMPInteropDirective &directive,
                            mlir::Location loc) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  auto voidPtrTy = builder.getVoidPtrTy();
  auto intptrTy = mlir::cast<cir::IntType>(
      cgf.convertType(cgf.getContext().getUIntPtrType()));
  auto depFlagTy = builder.getUInt8Ty();

  struct DependRecord {
    llvm::omp::RTLDependenceKindTy kind;
    const Expr *expr;
  };
  llvm::SmallVector<DependRecord, 4> dependRecords;

  for (const auto *clause : directive.getClausesOfKind<OMPDependClause>()) {
    if (clause->getModifier()) {
      cgf.getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                     "OMPDependClause modifier on interop "
                                     "directive");
      return std::nullopt;
    }

    auto kind = getOpenMPRTLDependKind(clause->getDependencyKind());
    if (!kind) {
      cgf.getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                     "OMPDependClause kind on interop "
                                     "directive");
      return std::nullopt;
    }

    for (const Expr *varExpr : clause->varlist())
      dependRecords.push_back({*kind, varExpr});
  }

  if (dependRecords.empty())
    return OpenMPInteropDependInfo{
        builder.getSInt32(0, loc),
        builder.getNullValue(voidPtrTy, loc),
    };

  cir::RecordType depInfoTy =
      builder.getAnonRecordTy({intptrTy, intptrTy, depFlagTy},
                              /*packed=*/false, /*padded=*/true);
  CharUnits depAlign = cgf.getPointerAlign();
  mlir::Value arraySize = builder.getConstInt(
      loc, mlir::cast<cir::IntType>(cgf.sizeTy), dependRecords.size());
  mlir::OpBuilder::InsertPoint depArrayIP = builder.saveInsertionPoint();
  Address depArray =
      cgf.createTempAlloca(depInfoTy, depAlign, loc, "__omp_interop_deps",
                           arraySize, /*allocaAddr=*/nullptr, depArrayIP);

  auto oneIndexTy = mlir::cast<cir::IntType>(cgf.ptrDiffTy);
  for (auto [index, dep] : llvm::enumerate(dependRecords)) {
    mlir::Value elementPtr = depArray.getPointer();
    if (index != 0) {
      mlir::Value stride =
          builder.getConstInt(loc, oneIndexTy, static_cast<int64_t>(index));
      elementPtr = builder.createPtrStride(loc, elementPtr, stride);
    }

    mlir::Value baseAddrField = builder.createGetMember(
        loc, builder.getPointerTo(intptrTy), elementPtr, "base_addr", 0);
    mlir::Value sizeField = builder.createGetMember(
        loc, builder.getPointerTo(intptrTy), elementPtr, "len", 1);
    mlir::Value flagsField = builder.createGetMember(
        loc, builder.getPointerTo(depFlagTy), elementPtr, "flags", 2);

    mlir::Value dependVarPtr = cgf.emitLValue(dep.expr).getPointer();
    mlir::Value baseAddr = builder.createPtrToInt(dependVarPtr, intptrTy);
    mlir::Value size = builder.getConstInt(
        loc, intptrTy,
        cgf.getContext().getTypeSizeInChars(dep.expr->getType()).getQuantity());
    mlir::Value flags = builder.getConstInt(
        loc, depFlagTy, static_cast<unsigned>(dep.kind));

    builder.createStore(loc, baseAddr,
                        Address(baseAddrField, intptrTy, depAlign));
    builder.createStore(loc, size, Address(sizeField, intptrTy, depAlign));
    builder.createStore(loc, flags, Address(flagsField, depFlagTy, depAlign));
  }

  return OpenMPInteropDependInfo{
      builder.getSInt32(static_cast<int32_t>(dependRecords.size()), loc),
      builder.createBitcast(loc, depArray.getPointer(), voidPtrTy),
  };
}

static cir::FuncOp getOpenMPInteropRuntimeFunction(CIRGenFunction &cgf,
                                                   llvm::StringRef name,
                                                   mlir::Type ompLocTy,
                                                   mlir::Type interopVarTy,
                                                   bool isInit) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  auto s32Ty = builder.getSInt32Ty();
  auto voidPtrTy = builder.getVoidPtrTy();
  llvm::SmallVector<mlir::Type, 8> argTys{ompLocTy, s32Ty, interopVarTy};
  if (isInit)
    argTys.push_back(s32Ty);
  argTys.append({s32Ty, s32Ty, voidPtrTy, s32Ty});
  return cgf.cgm.createRuntimeFunction(builder.getFuncType(argTys,
                                                           builder.getVoidTy()),
                                       name);
}

static void emitOpenMPInteropRuntimeCall(
    CIRGenFunction &cgf, SourceLocation beginLoc, llvm::StringRef runtimeName,
    mlir::Value interopVarPtr,
    std::optional<llvm::omp::OMPInteropType> interopType, mlir::Value device,
    OpenMPInteropDependInfo dependInfo, bool hasNowaitClause) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(beginLoc);

  mlir::Value ompLoc = cgf.emitOpenMPRuntimeLocation(beginLoc, 0);
  mlir::Value threadID = cgf.emitOpenMPThreadID(beginLoc, 0);
  if (!device)
    device = builder.getSInt32(-1, loc);
  else
    device = castInteropIntToSInt32(cgf, loc, device);

  mlir::Value nowaitVal =
      builder.getSInt32(hasNowaitClause ? 1 : 0, loc);
  llvm::SmallVector<mlir::Value, 8> args{ompLoc, threadID, interopVarPtr};
  if (interopType)
    args.push_back(
        builder.getSInt32(static_cast<int32_t>(*interopType), loc));
  args.append({device, dependInfo.numDependences, dependInfo.dependenceList,
               nowaitVal});

  cir::FuncOp runtimeFn = getOpenMPInteropRuntimeFunction(
      cgf, runtimeName, ompLoc.getType(), interopVarPtr.getType(),
      interopType.has_value());
  cgf.emitRuntimeCall(loc, runtimeFn, args);
}

struct OpenMPDepobjDependData {
  OpenMPDependClauseKind depKind;
  const Expr *iteratorExpr = nullptr;
  llvm::SmallVector<const Expr *, 4> depExprs;
};

struct OpenMPDepobjRecordInfo {
  cir::RecordType depInfoTy;
  cir::IntType baseAddrTy;
  cir::IntType sizeTy;
  cir::IntType flagTy;
  CharUnits alignment;
  uint64_t sizeInBytes;
};

struct OpenMPDepobjElements {
  mlir::Value numDeps;
  mlir::Value basePtr;
  mlir::Value headerPtr;
};

struct OpenMPDepobjPointerAndSize {
  mlir::Value addr;
  mlir::Value size;
};

struct OpenMPRuntimeDependInfo {
  mlir::Value numDeps;
  mlir::Value depArray;
};

static llvm::SmallVector<const OMPDependClause *, 4>
collectOpenMPDependClauses(const OMPExecutableDirective &directive) {
  llvm::SmallVector<const OMPDependClause *, 4> clauses;
  for (const auto *clause : directive.getClausesOfKind<OMPDependClause>())
    clauses.push_back(clause);
  return clauses;
}

static llvm::SmallVector<OpenMPDepobjDependData, 4>
buildOpenMPDepobjDependences(ArrayRef<const OMPDependClause *> clauses) {
  llvm::SmallVector<OpenMPDepobjDependData, 4> dependences;

  bool hasOmpAllMemory = llvm::any_of(
      clauses,
      [](const OMPDependClause *clause) {
        return clause->getDependencyKind() == OMPC_DEPEND_outallmemory ||
               clause->getDependencyKind() == OMPC_DEPEND_inoutallmemory;
      });
  if (hasOmpAllMemory) {
    OpenMPDepobjDependData &dep =
        dependences.emplace_back(OpenMPDepobjDependData{
            OMPC_DEPEND_outallmemory,
            /*iteratorExpr=*/nullptr,
            /*depExprs=*/{},
        });
    // Match OGCG and keep a synthetic null expression for omp_all_memory so
    // the record emission logic can stay uniform.
    dep.depExprs.push_back(nullptr);
  }

  for (const auto *clause : clauses) {
    OpenMPDependClauseKind kind = clause->getDependencyKind();
    if (kind == OMPC_DEPEND_outallmemory || kind == OMPC_DEPEND_inoutallmemory)
      continue;
    if (hasOmpAllMemory && (kind == OMPC_DEPEND_out || kind == OMPC_DEPEND_inout))
      continue;

    OpenMPDepobjDependData &dep =
        dependences.emplace_back(OpenMPDepobjDependData{
            kind,
            clause->getModifier(),
            /*depExprs=*/{},
        });
    dep.depExprs.append(clause->varlist_begin(), clause->varlist_end());
  }

  return dependences;
}

static llvm::SmallVector<OpenMPDepobjDependData, 4>
buildOpenMPDepobjDependences(const OMPExecutableDirective &directive) {
  return buildOpenMPDepobjDependences(collectOpenMPDependClauses(directive));
}

static OpenMPDepobjRecordInfo getOpenMPDepobjRecordInfo(CIRGenFunction &cgf) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  auto baseAddrTy = mlir::cast<cir::IntType>(
      cgf.convertType(cgf.getContext().getUIntPtrType()));
  auto sizeTy = mlir::cast<cir::IntType>(cgf.sizeTy);
  auto flagTy = builder.getUInt8Ty();
  cir::RecordType depInfoTy =
      builder.getAnonRecordTy({baseAddrTy, sizeTy, flagTy},
                              /*packed=*/false, /*padded=*/true);
  CharUnits alignment = CharUnits::fromQuantity(
      cgf.getCIRGenModule().getDataLayout().getABITypeAlign(depInfoTy).value());
  uint64_t sizeInBytes =
      cgf.getCIRGenModule().getDataLayout().getTypeAllocSize(depInfoTy)
          .getFixedValue();
  return {depInfoTy, baseAddrTy, sizeTy, flagTy, alignment, sizeInBytes};
}

static mlir::Value getSignedOpenMPConstant(CIRGenFunction &cgf,
                                           mlir::Location loc,
                                           cir::IntType ty, int64_t value) {
  llvm::APInt signedValue(ty.getWidth(), static_cast<uint64_t>(value),
                          /*isSigned=*/true);
  return cgf.getBuilder().getConstInt(loc, signedValue, /*isUnsigned=*/false);
}

static cir::FuncOp getOpenMPDepobjAllocRuntimeFunction(CIRGenFunction &cgf) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  cir::FuncOp fn = cgf.cgm.createRuntimeFunction(
      builder.getFuncType(
          {builder.getSInt32Ty(), cgf.sizeTy, builder.getVoidPtrTy()},
          builder.getVoidPtrTy()),
      "__kmpc_alloc");
  fn.setResultAttr(0, "cir.noalias",
                   mlir::UnitAttr::get(fn.getContext()));
  return fn;
}

static cir::FuncOp getOpenMPDepobjFreeRuntimeFunction(CIRGenFunction &cgf) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  return cgf.cgm.createRuntimeFunction(
      builder.getFuncType(
          {builder.getSInt32Ty(), builder.getVoidPtrTy(), builder.getVoidPtrTy()},
          builder.getVoidTy()),
      "__kmpc_free");
}

static std::optional<OpenMPDepobjPointerAndSize>
emitOpenMPDepobjPointerAndSize(CIRGenFunction &cgf,
                               const OpenMPDepobjRecordInfo &recordInfo,
                               const Expr *depExpr, mlir::Location loc) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  if (!depExpr) {
    return OpenMPDepobjPointerAndSize{
        builder.getConstInt(loc, recordInfo.baseAddrTy, 0),
        builder.getConstInt(loc, recordInfo.sizeTy, 0),
    };
  }

  if (const auto *shapingExpr =
          dyn_cast<OMPArrayShapingExpr>(depExpr->IgnoreParenImpCasts())) {
    const Expr *base = shapingExpr->getBase();
    QualType baseTy = base->getType();
    if (!baseTy->isPointerType()) {
      cgf.getCIRGenModule().errorNYI(depExpr->getSourceRange(),
                                     "OpenMP depobj shaping base type");
      return std::nullopt;
    }

    mlir::Value baseAddr =
        builder.createPtrToInt(cgf.emitScalarExpr(base), recordInfo.baseAddrTy);
    mlir::Value size = builder.getConstInt(
        loc, recordInfo.sizeTy,
        cgf.getContext()
            .getTypeSizeInChars(baseTy->castAs<PointerType>()->getPointeeType())
            .getQuantity());
    for (const Expr *dimension : shapingExpr->getDimensions()) {
      mlir::Value dimValue = cgf.emitScalarExpr(dimension);
      if (dimValue.getType() != recordInfo.sizeTy)
        dimValue = builder.createIntCast(dimValue, recordInfo.sizeTy);
      size = builder.createMul(loc, size, dimValue);
    }
    return OpenMPDepobjPointerAndSize{baseAddr, size};
  }

  if (isa<ArraySectionExpr>(depExpr->IgnoreParenImpCasts())) {
    cgf.getCIRGenModule().errorNYI(depExpr->getSourceRange(),
                                   "OpenMP depobj ArraySectionExpr");
    return std::nullopt;
  }

  mlir::Value depPtr = cgf.emitLValue(depExpr).getPointer();
  return OpenMPDepobjPointerAndSize{
      builder.createPtrToInt(depPtr, recordInfo.baseAddrTy),
      builder.getConstInt(
          loc, recordInfo.sizeTy,
          cgf.getContext().getTypeSizeInChars(depExpr->getType()).getQuantity()),
  };
}

static mlir::LogicalResult
emitOpenMPDepobjIterators(CIRGenFunction &cgf,
                          const OMPIteratorExpr *iteratorExpr, unsigned index,
                          llvm::function_ref<mlir::LogicalResult()> bodyEmitter) {
  if (!iteratorExpr || index == iteratorExpr->numOfIterators())
    return bodyEmitter();

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(iteratorExpr->getExprLoc());
  const auto *iteratorDecl = cast<VarDecl>(iteratorExpr->getIteratorDecl(index));
  const OMPIteratorHelperData &helper = iteratorExpr->getHelper(index);

  cgf.emitVarDecl(*iteratorDecl);
  cgf.emitVarDecl(*helper.CounterVD);
  Address counterAddr = cgf.getAddrOfLocalVar(helper.CounterVD);

  auto counterTy = mlir::cast<cir::IntType>(counterAddr.getElementType());
  mlir::Value zero = builder.getConstInt(loc, counterTy, 0);
  builder.createStore(loc, zero, counterAddr);

  mlir::Value upper = cgf.emitScalarExpr(helper.Upper);
  if (upper.getType() != counterTy)
    upper = builder.createIntCast(upper, counterTy);

  mlir::LogicalResult bodyResult = mlir::success();
  mlir::Value hasIterations =
      builder.createCompare(loc, cir::CmpOpKind::lt, zero, upper);
  cir::IfOp::create(
      builder, loc, hasIterations, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());

        builder.createDoWhile(
            thenLoc,
            /*condBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location loopLoc) {
              mlir::Value counter = builder.createLoad(loopLoc, counterAddr);
              mlir::Value cmp =
                  builder.createCompare(loopLoc, cir::CmpOpKind::lt, counter, upper);
              builder.createCondition(cmp);
            },
            /*bodyBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location loopLoc) {
              cgf.emitIgnoredExpr(helper.Update);
              if (mlir::failed(bodyResult = emitOpenMPDepobjIterators(
                                   cgf, iteratorExpr, index + 1, bodyEmitter))) {
                builder.createYield(loopLoc);
                return;
              }
              cgf.emitIgnoredExpr(helper.CounterUpdate);
              builder.createYield(loopLoc);
            });

        cir::YieldOp::create(thenBuilder, thenLoc);
      });

  return bodyResult;
}

static mlir::LogicalResult
emitOpenMPDepobjDependRecord(CIRGenFunction &cgf,
                             const OpenMPDepobjRecordInfo &recordInfo,
                             const OpenMPDepobjDependData &depData,
                             const Expr *depExpr, Address depArray,
                             Address posAddr, mlir::Location loc) {
  auto rtlKind = getOpenMPRTLDependKind(depData.depKind);
  if (!rtlKind) {
    cgf.getCIRGenModule().errorNYI(depExpr ? depExpr->getSourceRange()
                                           : SourceRange(),
                                   "OpenMP depobj depend kind");
    return mlir::failure();
  }

  auto depInfo = emitOpenMPDepobjPointerAndSize(cgf, recordInfo, depExpr, loc);
  if (!depInfo)
    return mlir::failure();

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Value pos = builder.createLoad(loc, posAddr);
  mlir::Value elementPtr = builder.createPtrStride(loc, depArray.getPointer(), pos);
  mlir::Value baseAddrField = builder.createGetMember(
      loc, builder.getPointerTo(recordInfo.baseAddrTy), elementPtr, "base_addr",
      0);
  mlir::Value sizeField = builder.createGetMember(
      loc, builder.getPointerTo(recordInfo.sizeTy), elementPtr, "len", 1);
  mlir::Value flagsField = builder.createGetMember(
      loc, builder.getPointerTo(recordInfo.flagTy), elementPtr, "flags", 2);

  builder.createStore(
      loc, depInfo->addr,
      Address(baseAddrField, recordInfo.baseAddrTy, recordInfo.alignment));
  builder.createStore(loc, depInfo->size,
                      Address(sizeField, recordInfo.sizeTy, recordInfo.alignment));
  builder.createStore(
      loc,
      builder.getConstInt(loc, recordInfo.flagTy,
                          static_cast<unsigned>(*rtlKind)),
      Address(flagsField, recordInfo.flagTy, recordInfo.alignment));

  mlir::Value one =
      builder.getConstInt(loc, mlir::cast<cir::IntType>(cgf.ptrDiffTy), 1);
  builder.createStore(loc, builder.createAdd(loc, pos, one), posAddr);
  return mlir::success();
}

static mlir::LogicalResult
emitOpenMPDepobjDependData(CIRGenFunction &cgf,
                           const OpenMPDepobjRecordInfo &recordInfo,
                           const OpenMPDepobjDependData &depData,
                           Address depArray, Address posAddr,
                           mlir::Location loc) {
  auto emitRecords = [&]() -> mlir::LogicalResult {
    for (const Expr *depExpr : depData.depExprs)
      if (mlir::failed(emitOpenMPDepobjDependRecord(
              cgf, recordInfo, depData, depExpr, depArray, posAddr, loc)))
        return mlir::failure();
    return mlir::success();
  };

  const auto *iteratorExpr = cast_or_null<OMPIteratorExpr>(
      depData.iteratorExpr ? depData.iteratorExpr->IgnoreParenImpCasts()
                           : nullptr);
  return emitOpenMPDepobjIterators(cgf, iteratorExpr, 0, emitRecords);
}

static std::optional<Address>
emitOpenMPDepobjDependClause(CIRGenFunction &cgf,
                             const OpenMPDepobjDependData &depData,
                             SourceLocation beginLoc) {
  if (depData.depExprs.empty())
    return Address::invalid();

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(beginLoc);
  OpenMPDepobjRecordInfo recordInfo = getOpenMPDepobjRecordInfo(cgf);

  mlir::Value numDeps =
      builder.getConstInt(loc, recordInfo.sizeTy, depData.depExprs.size());
  if (const auto *iteratorExpr = cast_or_null<OMPIteratorExpr>(
          depData.iteratorExpr ? depData.iteratorExpr->IgnoreParenImpCasts()
                               : nullptr)) {
    numDeps = builder.getConstInt(loc, recordInfo.sizeTy, 1);
    for (unsigned i = 0, e = iteratorExpr->numOfIterators(); i != e; ++i) {
      mlir::Value upper = cgf.emitScalarExpr(iteratorExpr->getHelper(i).Upper);
      if (upper.getType() != recordInfo.sizeTy)
        upper = builder.createIntCast(upper, recordInfo.sizeTy);
      numDeps = builder.createMul(loc, numDeps, upper);
    }
  }

  mlir::Value totalRecords = builder.createAdd(
      loc, numDeps, builder.getConstInt(loc, recordInfo.sizeTy, 1));
  mlir::Value allocSize = builder.createMul(
      loc, totalRecords,
      builder.getConstInt(loc, recordInfo.sizeTy, recordInfo.sizeInBytes));
  mlir::Value rawMemory = cgf.emitRuntimeCall(
      loc, getOpenMPDepobjAllocRuntimeFunction(cgf),
      {cgf.emitOpenMPThreadID(beginLoc, 0), allocSize,
       builder.getNullValue(builder.getVoidPtrTy(), loc)});
  mlir::Value depArrayPtr =
      builder.createBitcast(loc, rawMemory, builder.getPointerTo(recordInfo.depInfoTy));
  Address depArray(depArrayPtr, recordInfo.depInfoTy, recordInfo.alignment);

  mlir::Value numDepsField = builder.createGetMember(
      loc, builder.getPointerTo(recordInfo.baseAddrTy), depArrayPtr,
      "base_addr", 0);
  mlir::Value storedNumDeps = numDeps;
  if (storedNumDeps.getType() != recordInfo.baseAddrTy)
    storedNumDeps = builder.createIntCast(storedNumDeps, recordInfo.baseAddrTy);
  builder.createStore(
      loc, storedNumDeps,
      Address(numDepsField, recordInfo.baseAddrTy, recordInfo.alignment));

  Address posAddr = cgf.createTempAlloca(
      cgf.ptrDiffTy, cgf.getPointerAlign(), loc, "__omp_depobj_pos");
  builder.createStore(
      loc, builder.getConstInt(loc, mlir::cast<cir::IntType>(cgf.ptrDiffTy), 1),
      posAddr);
  if (mlir::failed(
          emitOpenMPDepobjDependData(cgf, recordInfo, depData, depArray, posAddr,
                                     loc)))
    return std::nullopt;

  mlir::Value firstDepPtr = builder.createPtrStride(
      loc, depArrayPtr,
      builder.getConstInt(loc, mlir::cast<cir::IntType>(cgf.ptrDiffTy), 1));
  return Address(firstDepPtr, recordInfo.depInfoTy, recordInfo.alignment);
}

static OpenMPDepobjElements
emitOpenMPDepobjElements(CIRGenFunction &cgf, LValue depobjLValue,
                         const OpenMPDepobjRecordInfo &recordInfo,
                         mlir::Location loc) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Value depobjPtr = cgf.emitLoadOfScalar(depobjLValue, SourceLocation());
  mlir::Value basePtr =
      builder.createBitcast(loc, depobjPtr, builder.getPointerTo(recordInfo.depInfoTy));
  mlir::Value headerPtr = builder.createPtrStride(
      loc, basePtr,
      getSignedOpenMPConstant(cgf, loc, mlir::cast<cir::IntType>(cgf.ptrDiffTy),
                              -1));
  mlir::Value numDepsField = builder.createGetMember(
      loc, builder.getPointerTo(recordInfo.baseAddrTy), headerPtr, "base_addr",
      0);
  mlir::Value numDeps = builder.createLoad(
      loc, Address(numDepsField, recordInfo.baseAddrTy, recordInfo.alignment));
  return {numDeps, basePtr, headerPtr};
}

static std::optional<mlir::Value>
emitOpenMPDepobjElementCount(CIRGenFunction &cgf,
                             const OpenMPDepobjRecordInfo &recordInfo,
                             const OpenMPDepobjDependData &depData,
                             mlir::Location loc) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CharUnits countAlign =
      cgf.getContext().getTypeAlignInChars(cgf.getContext().getSizeType());
  Address countAddr = cgf.createTempAlloca(
      cgf.sizeTy, countAlign, loc, "__omp_depobj_count");
  builder.createStore(
      loc, builder.getConstInt(loc, mlir::cast<cir::IntType>(cgf.sizeTy), 0),
      countAddr);

  auto emitCount = [&]() -> mlir::LogicalResult {
    for (const Expr *depExpr : depData.depExprs) {
      LValue depobjLValue = cgf.emitLValue(depExpr->IgnoreParenImpCasts());
      OpenMPDepobjElements depobj =
          emitOpenMPDepobjElements(cgf, depobjLValue, recordInfo, loc);
      mlir::Value numDeps = depobj.numDeps;
      if (numDeps.getType() != cgf.sizeTy)
        numDeps = builder.createIntCast(numDeps, cgf.sizeTy);
      mlir::Value cur = builder.createLoad(loc, countAddr);
      builder.createStore(loc, builder.createAdd(loc, cur, numDeps), countAddr);
    }
    return mlir::success();
  };

  const auto *iteratorExpr = cast_or_null<OMPIteratorExpr>(
      depData.iteratorExpr ? depData.iteratorExpr->IgnoreParenImpCasts()
                           : nullptr);
  if (mlir::failed(emitOpenMPDepobjIterators(cgf, iteratorExpr, 0, emitCount)))
    return std::nullopt;

  return builder.createLoad(loc, countAddr);
}

static mlir::LogicalResult
emitOpenMPAppendDepobjDependData(CIRGenFunction &cgf,
                                 const OpenMPDepobjRecordInfo &recordInfo,
                                 const OpenMPDepobjDependData &depData,
                                 Address depArray, Address posAddr,
                                 mlir::Location loc) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  auto emitCopies = [&]() -> mlir::LogicalResult {
    for (const Expr *depExpr : depData.depExprs) {
      LValue depobjLValue = cgf.emitLValue(depExpr->IgnoreParenImpCasts());
      OpenMPDepobjElements depobj =
          emitOpenMPDepobjElements(cgf, depobjLValue, recordInfo, loc);

      mlir::Value copySize = depobj.numDeps;
      if (copySize.getType() != recordInfo.sizeTy)
        copySize = builder.createIntCast(copySize, recordInfo.sizeTy);
      copySize = builder.createMul(
          loc, copySize,
          builder.getConstInt(loc, recordInfo.sizeTy, recordInfo.sizeInBytes));

      mlir::Value pos = builder.createLoad(loc, posAddr);
      mlir::Value destPtr = builder.createPtrStride(loc, depArray.getPointer(), pos);
      mlir::Value destVoid =
          builder.createBitcast(loc, destPtr, builder.getVoidPtrTy());
      mlir::Value srcVoid =
          builder.createBitcast(loc, depobj.basePtr, builder.getVoidPtrTy());
      cir::MemCpyOp::create(builder, loc, destVoid, srcVoid, copySize);

      mlir::Value numDeps = depobj.numDeps;
      if (numDeps.getType() != cgf.ptrDiffTy)
        numDeps = builder.createIntCast(numDeps, cgf.ptrDiffTy);
      builder.createStore(loc, builder.createAdd(loc, pos, numDeps), posAddr);
    }
    return mlir::success();
  };

  const auto *iteratorExpr = cast_or_null<OMPIteratorExpr>(
      depData.iteratorExpr ? depData.iteratorExpr->IgnoreParenImpCasts()
                           : nullptr);
  return emitOpenMPDepobjIterators(cgf, iteratorExpr, 0, emitCopies);
}

static mlir::LogicalResult
emitOpenMPDepobjDestroyClause(CIRGenFunction &cgf, LValue depobjLValue,
                              SourceLocation beginLoc) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(beginLoc);
  OpenMPDepobjRecordInfo recordInfo = getOpenMPDepobjRecordInfo(cgf);
  OpenMPDepobjElements depobj =
      emitOpenMPDepobjElements(cgf, depobjLValue, recordInfo, loc);
  mlir::Value headerPtr =
      builder.createBitcast(loc, depobj.headerPtr, builder.getVoidPtrTy());
  cgf.emitRuntimeCall(loc, getOpenMPDepobjFreeRuntimeFunction(cgf),
                      {cgf.emitOpenMPThreadID(beginLoc, 0), headerPtr,
                       builder.getNullValue(builder.getVoidPtrTy(), loc)});
  return mlir::success();
}

static mlir::LogicalResult
emitOpenMPDepobjUpdateClause(CIRGenFunction &cgf, LValue depobjLValue,
                             OpenMPDependClauseKind depKind,
                             SourceLocation beginLoc) {
  auto rtlKind = getOpenMPRTLDependKind(depKind);
  if (!rtlKind) {
    cgf.getCIRGenModule().errorNYI(beginLoc, "OpenMP depobj update kind");
    return mlir::failure();
  }

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(beginLoc);
  OpenMPDepobjRecordInfo recordInfo = getOpenMPDepobjRecordInfo(cgf);
  OpenMPDepobjElements depobj =
      emitOpenMPDepobjElements(cgf, depobjLValue, recordInfo, loc);

  mlir::Value numDeps = depobj.numDeps;
  if (numDeps.getType() != cgf.ptrDiffTy)
    numDeps = builder.createIntCast(numDeps, cgf.ptrDiffTy);
  mlir::Value endPtr = builder.createPtrStride(loc, depobj.basePtr, numDeps);
  mlir::Value isNotEmpty =
      builder.createCompare(loc, cir::CmpOpKind::ne, depobj.basePtr, endPtr);

  cir::IfOp::create(
      builder, loc, isNotEmpty, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());

        auto currentPtrTy = builder.getPointerTo(recordInfo.depInfoTy);
        auto currentPtrPtrTy = builder.getPointerTo(currentPtrTy);
        auto ptrAlignAttr =
            builder.getAlignmentAttr(cgf.getPointerAlign().getQuantity());
        mlir::Value currentSlot = builder.createAlloca(
            thenLoc, currentPtrPtrTy, currentPtrTy, "__omp_depobj_cur",
            ptrAlignAttr);
        Address currentAddr(currentSlot, currentPtrTy, cgf.getPointerAlign());
        builder.createStore(thenLoc, depobj.basePtr, currentAddr);

        builder.createDoWhile(
            thenLoc,
            /*condBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location loopLoc) {
              mlir::Value current = builder.createLoad(loopLoc, currentAddr);
              mlir::Value cmp =
                  builder.createCompare(loopLoc, cir::CmpOpKind::ne, current, endPtr);
              builder.createCondition(cmp);
            },
            /*bodyBuilder=*/
            [&](mlir::OpBuilder &, mlir::Location loopLoc) {
              mlir::Value current = builder.createLoad(loopLoc, currentAddr);
              mlir::Value flagsField = builder.createGetMember(
                  loopLoc, builder.getPointerTo(recordInfo.flagTy), current,
                  "flags", 2);
              builder.createStore(
                  loopLoc,
                  builder.getConstInt(loopLoc, recordInfo.flagTy,
                                      static_cast<unsigned>(*rtlKind)),
                  Address(flagsField, recordInfo.flagTy, recordInfo.alignment));
              mlir::Value next = builder.createPtrStride(
                  loopLoc, current,
                  builder.getConstInt(loopLoc,
                                      mlir::cast<cir::IntType>(cgf.ptrDiffTy), 1));
              builder.createStore(loopLoc, next, currentAddr);
              builder.createYield(loopLoc);
            });

        cir::YieldOp::create(thenBuilder, thenLoc);
      });

  return mlir::success();
}

static std::optional<OpenMPRuntimeDependInfo>
emitOpenMPRuntimeDependPayload(
    CIRGenFunction &cgf, ArrayRef<const OMPDependClause *> dependClauses,
    SourceLocation beginLoc) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(beginLoc);
  OpenMPDepobjRecordInfo recordInfo = getOpenMPDepobjRecordInfo(cgf);
  llvm::SmallVector<OpenMPDepobjDependData, 4> dependences =
      buildOpenMPDepobjDependences(dependClauses);
  if (dependences.empty())
    return std::nullopt;

  mlir::Value numDeps =
      builder.getConstInt(loc, recordInfo.sizeTy, static_cast<int64_t>(0));
  for (const OpenMPDepobjDependData &depData : dependences) {
    if (depData.depKind == OMPC_DEPEND_depobj) {
      std::optional<mlir::Value> depobjCount =
          emitOpenMPDepobjElementCount(cgf, recordInfo, depData, loc);
      if (!depobjCount)
        return std::nullopt;
      mlir::Value count = *depobjCount;
      if (count.getType() != recordInfo.sizeTy)
        count = builder.createIntCast(count, recordInfo.sizeTy);
      numDeps = builder.createAdd(loc, numDeps, count);
      continue;
    }

    mlir::Value depCount = builder.getConstInt(
        loc, recordInfo.sizeTy, static_cast<int64_t>(depData.depExprs.size()));
    if (const auto *iteratorExpr = cast_or_null<OMPIteratorExpr>(
            depData.iteratorExpr ? depData.iteratorExpr->IgnoreParenImpCasts()
                                 : nullptr)) {
      depCount = builder.getConstInt(loc, recordInfo.sizeTy, 1);
      for (unsigned i = 0, e = iteratorExpr->numOfIterators(); i != e; ++i) {
        mlir::Value upper = cgf.emitScalarExpr(iteratorExpr->getHelper(i).Upper);
        if (upper.getType() != recordInfo.sizeTy)
          upper = builder.createIntCast(upper, recordInfo.sizeTy);
        depCount = builder.createMul(loc, depCount, upper);
      }
      depCount = builder.createMul(
          loc, depCount,
          builder.getConstInt(loc, recordInfo.sizeTy,
                              static_cast<int64_t>(depData.depExprs.size())));
    }
    numDeps = builder.createAdd(loc, numDeps, depCount);
  }

  mlir::OpBuilder::InsertPoint depArrayIP = builder.saveInsertionPoint();
  Address depArray = cgf.createTempAlloca(
      recordInfo.depInfoTy, recordInfo.alignment, loc, "__omp_task_deps",
      numDeps, /*allocaAddr=*/nullptr, depArrayIP);
  Address posAddr = cgf.createTempAlloca(
      cgf.ptrDiffTy, cgf.getPointerAlign(), loc, "__omp_task_dep_pos");
  builder.createStore(
      loc, builder.getConstInt(loc, mlir::cast<cir::IntType>(cgf.ptrDiffTy), 0),
      posAddr);

  for (const OpenMPDepobjDependData &depData : dependences) {
    mlir::LogicalResult result =
        depData.depKind == OMPC_DEPEND_depobj
            ? emitOpenMPAppendDepobjDependData(cgf, recordInfo, depData,
                                               depArray, posAddr, loc)
            : emitOpenMPDepobjDependData(cgf, recordInfo, depData, depArray,
                                         posAddr, loc);
    if (mlir::failed(result))
      return std::nullopt;
  }

  return OpenMPRuntimeDependInfo{numDeps, depArray.getPointer()};
}

template <typename OpTy>
static void setOpenMPRuntimeDependPayload(CIRGenFunction &cgf, OpTy op,
                                          const OpenMPRuntimeDependInfo &dependInfo) {
  mlir::Value numDeps = dependInfo.numDeps;
  auto ompCountTy = mlir::IntegerType::get(&cgf.getMLIRContext(), 64);
  if (numDeps.getType() != ompCountTy)
    numDeps = castOpenMPValueToCIRType(cgf, op.getLoc(), numDeps, ompCountTy);

  op.getDependVarsMutable().append(mlir::ValueRange{dependInfo.depArray});
  op.getDependSizesMutable().append(mlir::ValueRange{numDeps});
  // Stamp the CIR-namespaced
  // `cir.omp.depend_sizes_owned` marker so the OpenMP-to-LLVM translator
  // gates `depend_sizes` consumption on a CIR-owned signal. See the
  // matching carve-out in `CIRGenOpenMPClause.cpp::setOMPDependClause`.
  op->setAttr("cir.omp.depend_sizes_owned",
              mlir::UnitAttr::get(&cgf.getMLIRContext()));
  llvm::SmallVector<mlir::Attribute, 1> dependKinds{
      mlir::omp::ClauseTaskDependAttr::get(
          &cgf.getMLIRContext(), mlir::omp::ClauseTaskDepend::taskdependlist)};
  op.setDependKindsAttr(
      mlir::ArrayAttr::get(&cgf.getMLIRContext(), dependKinds));
  // The OpenMP `taskdependlist`
  // enum case is a CIR-only addition to the upstream-shared
  // `ClauseTaskDepend` enum. A clean
  // carve-out via a parallel CIR-private enum would require duplicating
  // the entire `ClauseTaskDepend` attribute surface (parser/printer/
  // verifier). Stamp a CIR-namespaced marker `cir.omp.taskdependlist`
  // (UnitAttr) on the depend-clause-bearing op so the OpenMP-to-LLVM
  // translator gates its prebuilt-payload fast path on a CIR-owned
  // signal rather than inspecting the shared enum case directly. The
  // shared enum case stays in `OpenMPEnums.td` for verifier
  // consistency; the marker pins ownership of the prebuilt-payload
  // dispatch contract until the parallel-enum follow-up lands. Same
  // marker-attribute disposition as the `cir.omp.message_var_owned`
  // read.
  op->setAttr("cir.omp.taskdependlist",
              mlir::UnitAttr::get(&cgf.getMLIRContext()));
}

template <typename OpTy, typename DirectiveTy>
static mlir::LogicalResult maybeEmitOpenMPRuntimeDependPayload(
    CIRGenFunction &cgf, OpTy op, const DirectiveTy &directive,
    const CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo) {
  if (!clauseInfo.useRuntimeDependPayload)
    return mlir::success();

  llvm::SmallVector<const OMPDependClause *, 4> dependClauses =
      collectOpenMPDependClauses(directive);
  if (dependClauses.empty())
    return mlir::success();

  mlir::OpBuilder::InsertionGuard guard(cgf.getBuilder());
  cgf.getBuilder().setInsertionPoint(op);
  std::optional<OpenMPRuntimeDependInfo> dependInfo =
      emitOpenMPRuntimeDependPayload(cgf, dependClauses,
                                     directive.getBeginLoc());
  if (!dependInfo)
    return mlir::failure();

  setOpenMPRuntimeDependPayload(cgf, op, *dependInfo);
  return mlir::success();
}

static mlir::omp::ClauseDepend
getOpenMPOrderedDependType(CIRGenFunction &cgf, SourceRange range,
                           OpenMPDependClauseKind kind) {
  switch (kind) {
  case OMPC_DEPEND_source:
    return mlir::omp::ClauseDepend::dependsource;
  case OMPC_DEPEND_sink:
    return mlir::omp::ClauseDepend::dependsink;
  case OMPC_DEPEND_in:
  case OMPC_DEPEND_out:
  case OMPC_DEPEND_inout:
  case OMPC_DEPEND_mutexinoutset:
  case OMPC_DEPEND_inoutset:
  case OMPC_DEPEND_depobj:
  case OMPC_DEPEND_outallmemory:
  case OMPC_DEPEND_inoutallmemory:
  case OMPC_DEPEND_unknown:
    cgf.getCIRGenModule().errorNYI(range, "OpenMP ordered depend clause kind");
    return mlir::omp::ClauseDepend::dependsource;
  }

  llvm_unreachable("unexpected OpenMP depend kind");
}

static mlir::omp::ClauseDepend
getOpenMPOrderedDependType(CIRGenFunction &cgf, SourceRange range,
                           OpenMPDoacrossClauseModifier kind) {
  switch (kind) {
  case OMPC_DOACROSS_source:
  case OMPC_DOACROSS_source_omp_cur_iteration:
    return mlir::omp::ClauseDepend::dependsource;
  case OMPC_DOACROSS_sink:
  case OMPC_DOACROSS_sink_omp_cur_iteration:
    return mlir::omp::ClauseDepend::dependsink;
  case OMPC_DOACROSS_unknown:
    cgf.getCIRGenModule().errorNYI(range,
                                   "OpenMP ordered doacross clause kind");
    return mlir::omp::ClauseDepend::dependsource;
  }

  llvm_unreachable("unexpected OpenMP doacross kind");
}

template <typename ClauseTy>
static void emitOpenMPDoacrossOrderedOp(CIRGenFunction &cgf,
                                        mlir::Location begin,
                                        const ClauseTy *clause) {
  llvm::SmallVector<mlir::Value, 4> dependVars;
  dependVars.reserve(clause->getNumLoops());
  for (unsigned i = 0, e = clause->getNumLoops(); i != e; ++i)
    dependVars.push_back(emitOpenMPInt64Expr(cgf, clause->getLoopData(i)));

  mlir::omp::OrderedOperands clauseOps;
  clauseOps.doacrossNumLoops =
      cgf.getBuilder().getI64IntegerAttr(clause->getNumLoops());
  clauseOps.doacrossDependVars.append(dependVars.begin(), dependVars.end());

  if constexpr (std::is_same_v<ClauseTy, OMPDependClause>) {
    clauseOps.doacrossDependType = mlir::omp::ClauseDependAttr::get(
        &cgf.getMLIRContext(),
        getOpenMPOrderedDependType(
            cgf, SourceRange(clause->getBeginLoc(), clause->getEndLoc()),
                                   clause->getDependencyKind()));
  } else {
    clauseOps.doacrossDependType = mlir::omp::ClauseDependAttr::get(
        &cgf.getMLIRContext(),
        getOpenMPOrderedDependType(
            cgf, SourceRange(clause->getBeginLoc(), clause->getEndLoc()),
                                   clause->getDependenceType()));
  }

  mlir::omp::OrderedOp::create(cgf.getBuilder(), begin, clauseOps);
}

// Build a module-level `omp.private {type = private}` recipe (a fresh per-task
// copy with no copy-in) and return it. Used to privatize a synthesized loop
// induction variable that has no explicit data-sharing clause.
static mlir::omp::PrivateClauseOp
createOpenMPPrivatePrivatizer(CIRGenFunction &cgf, CIRGenBuilderTy &builder,
                              mlir::Location loc, llvm::StringRef baseName,
                              mlir::Type valueTy) {
  mlir::ModuleOp module = cgf.getCIRGenModule().getModule();
  std::string name = baseName.str();
  unsigned suffix = 0;
  while (module.lookupSymbol(name))
    name = (baseName + "." + llvm::Twine(++suffix)).str();

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());
  return mlir::omp::PrivateClauseOp::create(
      builder, loc, name, valueTy, mlir::omp::DataSharingClauseType::Private);
}

} // namespace

mlir::LogicalResult CIRGenFunction::emitSimpleOMPTaskLoopDirective(
    const OMPLoopDirective &s, llvm::StringRef directiveName,
    bool wrapInMaster, bool wrapInMasked, bool simd) {
  assert(!(wrapInMaster && wrapInMasked) &&
         "taskloop wrapper cannot be both master and masked");

  auto loopInfo = getOpenMPSimpleCanonicalForInfo(s);
  auto generatedLoopInfo =
      (!loopInfo && simd) ? getOpenMPSimpleGeneratedLoopInfo(s)
                          : std::optional<OpenMPSimpleGeneratedLoopInfo>{};
  auto collapsedGeneratedLoopInfo =
      (!loopInfo && simd && !generatedLoopInfo)
          ? getOpenMPCollapsedGeneratedSimdInfo(s)
          : std::optional<OpenMPCollapsedGeneratedSimdInfo>{};
  if (!loopInfo && !generatedLoopInfo && !collapsedGeneratedLoopInfo) {
    getCIRGenModule().errorNYI(
        s.getSourceRange(),
        (directiveName + " for non-canonical/non-unit-stride loop").str());
    return mlir::failure();
  }

  llvm::SmallVector<const OMPClause *, 4> taskloopClauses;
  llvm::SmallVector<const OMPClause *, 8> simdClauses;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_allocate:
    case llvm::omp::OMPC_firstprivate:
    case llvm::omp::OMPC_final:
    case llvm::omp::OMPC_grainsize:
    case llvm::omp::OMPC_in_reduction:
    case llvm::omp::OMPC_lastprivate:
    case llvm::omp::OMPC_mergeable:
    case llvm::omp::OMPC_nogroup:
    case llvm::omp::OMPC_num_tasks:
    case llvm::omp::OMPC_private:
    case llvm::omp::OMPC_priority:
    case llvm::omp::OMPC_reduction:
    case llvm::omp::OMPC_shared:
    case llvm::omp::OMPC_untied:
      taskloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_if:
      if (const auto *ifClause = dyn_cast<OMPIfClause>(clause)) {
        if (ifClause->getNameModifier() == llvm::omp::OMPD_unknown ||
            ifClause->getNameModifier() == llvm::omp::OMPD_taskloop) {
          taskloopClauses.push_back(clause);
          break;
        }
        if (ifClause->getNameModifier() == llvm::omp::OMPD_simd) {
          if (!simd) {
            getCIRGenModule().errorNYI(
                clause->getBeginLoc(), (directiveName + " clause").str(),
                llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
            return mlir::failure();
          }
          simdClauses.push_back(clause);
          break;
        }
      }
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), (directiveName + " if clause modifier").str());
      return mlir::failure();
    case llvm::omp::OMPC_aligned:
    case llvm::omp::OMPC_linear:
    case llvm::omp::OMPC_nontemporal:
    case llvm::omp::OMPC_order:
    case llvm::omp::OMPC_safelen:
    case llvm::omp::OMPC_simdlen:
      if (!simd) {
        getCIRGenModule().errorNYI(
            clause->getBeginLoc(), (directiveName + " clause").str(),
            llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
        return mlir::failure();
      }
      simdClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_collapse:
      break;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), (directiveName + " clause").str(),
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      return mlir::failure();
    }
  }

  mlir::LogicalResult res = mlir::success();
  OpenMPNontemporalDeclsRAII nontemporalDecls(*this, s);
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  llvm::SmallPtrSet<const VarDecl *, 4> materializedClauseVars;
  if (simd)
    materializeOpenMPSimdCompositeClauseExprs(*this, simdClauses,
                                              materializedClauseVars);

  const Stmt *generatedBody = nullptr;
  const VarDecl *generatedLogicalIV = nullptr;
  const Expr *generatedPreCond = nullptr;
  const Expr *generatedLastIteration = nullptr;
  llvm::SmallVector<const VarDecl *, 4> generatedSourceIVs;
  llvm::SmallVector<const Expr *, 4> generatedUpdates;
  llvm::SmallVector<const Expr *, 4> generatedFinals;
  llvm::SmallVector<bool, 4> emitFinals;
  if (!loopInfo) {
    assert(simd && (generatedLoopInfo || collapsedGeneratedLoopInfo) &&
           "expected generated simd loop info for non-canonical taskloop simd");
    generatedBody = generatedLoopInfo ? generatedLoopInfo->body
                                      : collapsedGeneratedLoopInfo->body;
    generatedLogicalIV =
        generatedLoopInfo ? generatedLoopInfo->logicalIV
                          : collapsedGeneratedLoopInfo->logicalIV;
    generatedPreCond = generatedLoopInfo ? generatedLoopInfo->preCond
                                         : collapsedGeneratedLoopInfo->preCond;
    generatedLastIteration =
        generatedLoopInfo ? generatedLoopInfo->lastIteration
                          : collapsedGeneratedLoopInfo->lastIteration;
    if (generatedLoopInfo) {
      llvm::append_range(generatedSourceIVs, generatedLoopInfo->sourceIVs);
      llvm::append_range(generatedUpdates, generatedLoopInfo->updates);
      llvm::append_range(generatedFinals, generatedLoopInfo->finals);
    } else {
      llvm::append_range(generatedSourceIVs,
                         collapsedGeneratedLoopInfo->sourceIVs);
      llvm::append_range(generatedUpdates, collapsedGeneratedLoopInfo->updates);
      llvm::append_range(generatedFinals, collapsedGeneratedLoopInfo->finals);
    }

    emitFinals.reserve(generatedSourceIVs.size());
    for (const VarDecl *sourceIV : generatedSourceIVs) {
      emitFinals.push_back(
          localDeclMap.count(sourceIV) || sourceIV->hasGlobalStorage() ||
          (capturedStmtInfo && capturedStmtInfo->lookup(sourceIV)));
    }
  }

  CharUnits ivAlign = CharUnits::Zero();
  mlir::Type ivType;
  Address ivSeedRawAlloca = Address::invalid();
  mlir::Value lowerBound;
  mlir::Value upperBound;
  mlir::Type loopTy;
  mlir::Value step;
  if (loopInfo) {
    ivAlign = getContext().getDeclAlign(loopInfo->iv);
    ivType = convertTypeForMem(loopInfo->iv->getType());
    createTempAlloca(ivType, ivAlign, getLoc(loopInfo->iv->getLocation()),
                     loopInfo->iv->getName(), /*arraySize=*/nullptr,
                     &ivSeedRawAlloca);
    if (!localDeclMap.count(loopInfo->iv))
      declare(ivSeedRawAlloca.getPointer(), loopInfo->iv,
              loopInfo->iv->getType(), getLoc(loopInfo->iv->getSourceRange()),
              ivAlign);

    lowerBound = emitOpenMPIntegerExpr(*this, loopInfo->lowerBound);
    upperBound = emitOpenMPIntegerExpr(*this, loopInfo->upperBound);
    loopTy = lowerBound.getType();
    if (loopTy != upperBound.getType()) {
      getCIRGenModule().errorNYI(
          s.getSourceRange(),
          (directiveName + " with mismatched loop bound types").str());
      return mlir::failure();
    }
    step = createOpenMPIntegerConstant(*this, begin, loopTy, 1);
  }

  auto emitCanonicalLoopNest =
      [&](const OpenMPClauseEmissionInfo &clauseInfo,
          const llvm::DenseMap<const VarDecl *, Address>
              &taskloopOriginalDecls) -> mlir::LogicalResult {
    mlir::omp::LoopNestOperands loopClauseOps;
    loopClauseOps.loopLowerBounds.push_back(lowerBound);
    loopClauseOps.loopUpperBounds.push_back(upperBound);
    loopClauseOps.loopSteps.push_back(step);
    if (loopInfo->upperInclusive)
      loopClauseOps.loopInclusive = builder.getUnitAttr();

    auto loopNestOp =
        mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
    loopNestOp->setAttr(
        "omp.frontend.iv_signed",
        builder.getDenseBoolArrayAttr(
            {loopInfo->iv->getType()->hasSignedIntegerRepresentation()}));

    mlir::Block *loopBlock = builder.createBlock(
        &loopNestOp.getRegion(), {}, {loopTy},
        {getLoc(loopInfo->iv->getLocation())});
    builder.setInsertionPointToEnd(loopBlock);

    // The loop IV is privatized per task through the taskloop's `omp.private`
    // machinery (see the privatizer wiring in emitTaskloopBody), so its address
    // is the per-task private copy bound by emitOMPTaskloopRegionBody, not the
    // shared enclosing-scope alloca.
    Address ivAddr = getAddrOfLocalVar(loopInfo->iv);
    mlir::Value ivValue = castOpenMPValueToCIRType(
        *this, getLoc(loopInfo->iv->getLocation()), loopBlock->getArgument(0),
        ivType);
    builder.createStore(getLoc(loopInfo->iv->getLocation()), ivValue, ivAddr);

    mlir::LogicalResult bodyRes =
        emitOMPLoopBody(*this, builder, begin, loopInfo->forStmt->getBody());
    if (!clauseInfo.lastprivateIndices.empty()) {
      assert(clauseInfo.lastprivateIndices.size() ==
                 clauseInfo.lastprivateVars.size() &&
             "mismatched OpenMP taskloop lastprivate metadata");
      mlir::Location ivLoc = getLoc(loopInfo->iv->getLocation());
      auto cirIVTy = mlir::dyn_cast<cir::IntType>(ivType);
      assert(cirIVTy && "expected CIR integer type for taskloop IV");
      mlir::Value upperBoundValue =
          castOpenMPValueToCIRType(*this, ivLoc, upperBound, ivType);
      mlir::Value stepValue = builder.getConstInt(ivLoc, cirIVTy, 1);
      mlir::Value nextIV = builder.createAdd(ivLoc, ivValue, stepValue);
      mlir::Value isLastIteration = builder.createCompare(
          ivLoc, loopInfo->upperInclusive ? cir::CmpOpKind::gt
                                          : cir::CmpOpKind::ge,
          nextIV, upperBoundValue);

      cir::IfOp::create(
          builder, ivLoc, isLastIteration, /*withElseRegion=*/false,
          [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
            for (auto [index, vd, srcVD, destVD, assignOp] :
                 llvm::zip_equal(clauseInfo.lastprivateIndices,
                                 clauseInfo.lastprivateVars,
                                 clauseInfo.lastprivateSourceVDs,
                                 clauseInfo.lastprivateDestVDs,
                                 clauseInfo.lastprivateAssignmentOps)) {
              (void)index;
              Address srcAddr = getOpenMPPrivateStorageAddress(*this, vd);
              auto originalIt =
                  taskloopOriginalDecls.find(vd->getCanonicalDecl());
              assert(originalIt != taskloopOriginalDecls.end() &&
                     "missing taskloop lastprivate destination address");
              emitOpenMPLastprivateCopy(*this, vd, srcAddr,
                                        originalIt->second, srcVD, destVD,
                                        assignOp);
            }
            cir::YieldOp::create(thenBuilder, thenLoc);
          });
    }
    mlir::omp::YieldOp::create(builder, end);
    return bodyRes;
  };

  auto emitGeneratedLoopNest =
      [&](mlir::Value generatedLowerBound, mlir::Value generatedUpperBound,
          mlir::Value generatedStep, mlir::Type generatedLoopTy,
          const OpenMPClauseEmissionInfo &clauseInfo,
          const llvm::DenseMap<const VarDecl *, Address>
              &taskloopOriginalDecls,
          const llvm::DenseMap<const VarDecl *, Address>
              &sourceIVOriginalAddrs,
          const llvm::SmallPtrSet<const VarDecl *, 4> &generatedSourceIVSet)
      -> mlir::LogicalResult {
    mlir::omp::LoopNestOperands loopClauseOps;
    loopClauseOps.loopLowerBounds.push_back(generatedLowerBound);
    loopClauseOps.loopUpperBounds.push_back(generatedUpperBound);
    loopClauseOps.loopSteps.push_back(generatedStep);
    loopClauseOps.loopInclusive = builder.getUnitAttr();

    auto loopNestOp = mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
    bool logicalIVSigned =
        generatedLogicalIV->getType()->hasSignedIntegerRepresentation();
    loopNestOp->setAttr("omp.frontend.iv_signed",
                        builder.getDenseBoolArrayAttr({logicalIVSigned}));

    mlir::Block *loopBlock = builder.createBlock(
        &loopNestOp.getRegion(), {}, {generatedLoopTy},
        {getLoc(generatedLogicalIV->getLocation())});
    builder.setInsertionPointToEnd(loopBlock);

    auto createLoopVarStorage = [&](const VarDecl *vd) {
      CharUnits vdAlign = getContext().getDeclAlign(vd);
      mlir::Type vdType = convertTypeForMem(vd->getType());
      Address rawAlloca = Address::invalid();
      createTempAlloca(vdType, vdAlign, getLoc(vd->getLocation()),
                       vd->getName(), /*arraySize=*/nullptr, &rawAlloca);
      return std::pair{rawAlloca, vdType};
    };

    auto [logicalIVRawAlloca, logicalIVType] =
        createLoopVarStorage(generatedLogicalIV);
    llvm::SmallVector<Address, 4> sourceIVRawAllocas;
    sourceIVRawAllocas.reserve(generatedSourceIVs.size());
    for (const VarDecl *sourceIV : generatedSourceIVs)
      sourceIVRawAllocas.push_back(createLoopVarStorage(sourceIV).first);

    llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
        sourceIVMapGuards;
    sourceIVMapGuards.reserve(generatedSourceIVs.size());
    for (auto [sourceIV, sourceIVRawAlloca] :
         llvm::zip_equal(generatedSourceIVs, sourceIVRawAllocas)) {
      sourceIVMapGuards.push_back(
          std::make_unique<DeclMapRevertingRAII>(*this, sourceIV));
      replaceAddrOfLocalVar(sourceIV, sourceIVRawAlloca);
    }

    DeclMapRevertingRAII logicalIVMapGuard(*this, generatedLogicalIV);
    replaceAddrOfLocalVar(generatedLogicalIV, logicalIVRawAlloca);

    mlir::Value logicalIVValue = loopBlock->getArgument(0);
    if (auto ompIVTy = mlir::dyn_cast<mlir::IntegerType>(logicalIVValue.getType())) {
      auto cirLoopTy =
          cir::IntType::get(&getMLIRContext(), ompIVTy.getWidth(), logicalIVSigned);
      logicalIVValue = mlir::UnrealizedConversionCastOp::create(
                           builder, getLoc(generatedLogicalIV->getLocation()),
                           cirLoopTy, logicalIVValue)
                           .getResult(0);
      if (cirLoopTy != logicalIVType) {
        logicalIVValue =
            builder.createIntCast(logicalIVValue,
                                  mlir::cast<cir::IntType>(logicalIVType));
      }
    } else {
      logicalIVValue = castOpenMPValueToCIRType(
          *this, getLoc(generatedLogicalIV->getLocation()), logicalIVValue,
          logicalIVType);
    }

    builder.createStore(getLoc(generatedLogicalIV->getLocation()),
                        logicalIVValue, logicalIVRawAlloca);
    for (const Expr *update : generatedUpdates)
      emitIgnoredExpr(update);

    mlir::LogicalResult bodyRes =
        emitOMPLoopBody(*this, builder, begin, generatedBody);
    bool anyEmitFinals =
        llvm::any_of(emitFinals, [](bool emitFinal) { return emitFinal; });
    if (anyEmitFinals || !clauseInfo.lastprivateIndices.empty()) {
      assert(clauseInfo.lastprivateIndices.size() ==
                 clauseInfo.lastprivateVars.size() &&
             "mismatched OpenMP taskloop lastprivate metadata");
      mlir::Location ivLoc = getLoc(generatedLogicalIV->getLocation());
      auto cirIVTy = mlir::dyn_cast<cir::IntType>(logicalIVType);
      assert(cirIVTy && "expected CIR integer type for generated taskloop IV");
      mlir::Value upperBoundValue = castOpenMPValueToCIRType(
          *this, ivLoc, generatedUpperBound, logicalIVType);
      mlir::Value stepValue = builder.getConstInt(ivLoc, cirIVTy, 1);
      mlir::Value nextIV = builder.createAdd(ivLoc, logicalIVValue, stepValue);
      mlir::Value isLastIteration = builder.createCompare(
          ivLoc, cir::CmpOpKind::gt, nextIV, upperBoundValue);

      cir::IfOp::create(
          builder, ivLoc, isLastIteration, /*withElseRegion=*/false,
          [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
            for (auto [idx, final] : llvm::enumerate(generatedFinals)) {
              if (!emitFinals[idx] || !final)
                continue;
              const VarDecl *sourceIV = generatedSourceIVs[idx];
              auto originalIt = sourceIVOriginalAddrs.find(sourceIV);
              assert(originalIt != sourceIVOriginalAddrs.end() &&
                     "missing taskloop source IV destination address");
              DeclMapRevertingRAII finalSourceIVMapGuard(*this, sourceIV);
              replaceAddrOfLocalVar(sourceIV, originalIt->second);
              emitIgnoredExpr(final);
            }
            for (auto [index, vd, srcVD, destVD, assignOp] :
                 llvm::zip_equal(clauseInfo.lastprivateIndices,
                                 clauseInfo.lastprivateVars,
                                 clauseInfo.lastprivateSourceVDs,
                                 clauseInfo.lastprivateDestVDs,
                                 clauseInfo.lastprivateAssignmentOps)) {
              (void)index;
              if (generatedSourceIVSet.contains(vd->getCanonicalDecl()))
                continue;
              Address srcAddr = getOpenMPPrivateStorageAddress(*this, vd);
              auto originalIt =
                  taskloopOriginalDecls.find(vd->getCanonicalDecl());
              assert(originalIt != taskloopOriginalDecls.end() &&
                     "missing taskloop lastprivate destination address");
              emitOpenMPLastprivateCopy(*this, vd, srcAddr,
                                        originalIt->second, srcVD, destVD,
                                        assignOp);
            }
            cir::YieldOp::create(thenBuilder, thenLoc);
          });
    }
    mlir::omp::YieldOp::create(builder, end);
    return bodyRes;
  };

  auto emitTaskloopBody = [&](mlir::Value generatedLoopLowerBound = mlir::Value{},
                              mlir::Value generatedLoopUpperBound = mlir::Value{},
                              mlir::Value generatedLoopStep = mlir::Value{},
                              mlir::Type generatedLoopTy = mlir::Type())
      -> mlir::LogicalResult {
    OpenMPSimdCompositeClauseData simdClauseData;
    if (simd && mlir::failed(collectOpenMPSimdClauseDataForComposite(
                    *this, simdClauses, simdClauseData)))
      return mlir::failure();

    llvm::DenseMap<const VarDecl *, Address> taskloopOriginalDecls;
    llvm::DenseMap<const VarDecl *, Address> sourceIVOriginalAddrs;
    llvm::SmallPtrSet<const VarDecl *, 4> generatedSourceIVSet;
    for (const VarDecl *sourceIV : generatedSourceIVs)
      generatedSourceIVSet.insert(sourceIV->getCanonicalDecl());

    auto taskloopOp = mlir::omp::TaskloopOp::create(
        builder, begin,
        /*allocate_vars=*/mlir::ValueRange{},
        /*allocator_vars=*/mlir::ValueRange{},
        /*final=*/mlir::Value{},
        /*grainsize_mod=*/mlir::omp::ClauseGrainsizeTypeAttr{},
        /*grainsize=*/mlir::Value{},
        /*if_expr=*/mlir::Value{},
        /*in_reduction_vars=*/mlir::ValueRange{},
        /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
        /*in_reduction_syms=*/mlir::ArrayAttr{},
        /*mergeable=*/false,
        /*nogroup=*/false,
        /*num_tasks_mod=*/mlir::omp::ClauseNumTasksTypeAttr{},
        /*num_tasks=*/mlir::Value{},
        /*priority=*/mlir::Value{},
        /*private_vars=*/mlir::ValueRange{},
        /*private_syms=*/mlir::ArrayAttr{},
        /*private_needs_barrier=*/false,
        /*reduction_mod=*/mlir::omp::ReductionModifierAttr{},
        /*reduction_vars=*/mlir::ValueRange{},
        /*reduction_byref=*/mlir::DenseBoolArrayAttr{},
        /*reduction_syms=*/mlir::ArrayAttr{},
        /*untied=*/false);
    OpenMPClauseEmissionInfo clauseInfo =
        emitOpenMPClauses(taskloopOp, taskloopClauses);

    // Privatize the loop induction variable per task. Without this the single
    // enclosing-scope IV alloca is captured by value into every task context,
    // so concurrent tasks share one IV storage and race. Route the IV through
    // the taskloop's `omp.private` set (mirroring classic clang's
    // EmitOMPPrivateLoopCounters) so the OMPIRBuilder gives each task its own
    // copy; emitOMPTaskloopRegionBody then binds the IV to that block arg.
    if (loopInfo) {
      auto ivPrivatizer = createOpenMPPrivatePrivatizer(
          *this, builder, getLoc(loopInfo->iv->getLocation()),
          ("__omp_taskloop_iv_" + loopInfo->iv->getName()).str(), ivType);
      unsigned ivPrivateIdx = taskloopOp.getPrivateVars().size();
      taskloopOp.getPrivateVarsMutable().append({ivSeedRawAlloca.getPointer()});
      llvm::SmallVector<mlir::Attribute, 4> allPrivateSyms;
      if (auto existingSyms = taskloopOp.getPrivateSyms())
        llvm::append_range(allPrivateSyms, *existingSyms);
      allPrivateSyms.push_back(mlir::SymbolRefAttr::get(ivPrivatizer));
      taskloopOp.setPrivateSymsAttr(
          mlir::ArrayAttr::get(builder.getContext(), allPrivateSyms));
      clauseInfo.privateIndices.push_back(ivPrivateIdx);
      clauseInfo.privateVars.push_back(loopInfo->iv);
    }
    {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPoint(taskloopOp);
      for (const OMPClause *clause : taskloopClauses) {
        if (clause->getClauseKind() != llvm::omp::OMPC_lastprivate)
          continue;
        const auto *lastprivateClause = cast<OMPLastprivateClause>(clause);
        for (const Expr *varExpr : lastprivateClause->varlist()) {
          const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr);
          if (!vd)
            continue;
          taskloopOriginalDecls.try_emplace(vd->getCanonicalDecl(),
                                            emitLValue(varExpr).getAddress());
        }
      }
      for (auto [idx, sourceIV] : llvm::enumerate(generatedSourceIVs)) {
        if (!emitFinals[idx])
          continue;
        sourceIVOriginalAddrs.try_emplace(sourceIV,
                                          emitLValue(s.counters()[idx])
                                              .getAddress());
      }
    }
    if (simd)
      taskloopOp.setComposite(true);

    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::LogicalResult taskloopRes = emitOMPTaskloopRegionBody(
        *this, taskloopOp, clauseInfo, [&]() -> mlir::LogicalResult {
          llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
              simdReductionGuards;
          if (simd) {
            mlir::ArrayAttr alignments =
                simdClauseData.alignments.empty()
                    ? mlir::ArrayAttr{}
                    : mlir::ArrayAttr::get(builder.getContext(),
                                           simdClauseData.alignments);
            mlir::ArrayAttr linearVarTypes =
                simdClauseData.linearVarTypes.empty()
                    ? mlir::ArrayAttr{}
                    : mlir::ArrayAttr::get(builder.getContext(),
                                           simdClauseData.linearVarTypes);
            // For `taskloop simd reduction`, the accumulation is a genuine
            // loop-carried dependency that the simd vectorizer would otherwise
            // be told to ignore (applySimd stamps parallel-access metadata).
            // Carry the reduction onto the inner `omp.simd` too, so the loop
            // body drives a simd-level reduction copy that is combined back
            // into the taskloop's per-task reduction copy after the loop. The
            // simd reduction operands must be the taskloop's per-task reduction
            // block args (not its operands, which name the shared original).
            auto taskloopArgIface =
                mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                    taskloopOp.getOperation());
            llvm::SmallVector<mlir::Value, 4> simdReductionVars(
                taskloopArgIface.getReductionBlockArgs());
            mlir::DenseBoolArrayAttr simdReductionByref =
                simdReductionVars.empty() ? mlir::DenseBoolArrayAttr{}
                                          : taskloopOp.getReductionByrefAttr();
            mlir::ArrayAttr simdReductionSyms =
                simdReductionVars.empty()
                    ? mlir::ArrayAttr{}
                    : taskloopOp.getReductionSymsAttr();
            auto simdOp = mlir::omp::SimdOp::create(
                builder, begin,
                simdClauseData.alignedVars, alignments, simdClauseData.ifExpr,
                simdClauseData.linearVars, simdClauseData.linearStepVars,
                linearVarTypes, simdClauseData.nontemporalVars,
                simdClauseData.order, simdClauseData.orderMod,
                /*private_vars=*/mlir::ValueRange{},
                /*private_syms=*/mlir::ArrayAttr{},
                /*private_needs_barrier=*/false,
                /*reduction_mod=*/mlir::omp::ReductionModifierAttr{},
                simdReductionVars, simdReductionByref, simdReductionSyms,
                simdClauseData.safelen, simdClauseData.simdlen);
            simdOp.setComposite(true);

            llvm::SmallVector<mlir::Type, 4> simdArgTypes;
            llvm::SmallVector<mlir::Location, 4> simdArgLocs;
            for (mlir::Value reductionVar : simdReductionVars) {
              simdArgTypes.push_back(reductionVar.getType());
              simdArgLocs.push_back(reductionVar.getLoc());
            }
            mlir::Block *simdBlock = builder.createBlock(
                &simdOp.getRegion(), {}, simdArgTypes, simdArgLocs);
            builder.setInsertionPointToEnd(simdBlock);
            // Rebind the reduction decls to the simd-level reduction block args
            // for the duration of the loop body (these guards outlive the body
            // emission below and revert on return).
            remapOpenMPReductionDecls(*this, simdOp.getOperation(), clauseInfo,
                                      simdReductionGuards);
          }

          return loopInfo
                     ? emitCanonicalLoopNest(clauseInfo,
                                             taskloopOriginalDecls)
                     : emitGeneratedLoopNest(generatedLoopLowerBound,
                                             generatedLoopUpperBound,
                                             generatedLoopStep,
                                             generatedLoopTy, clauseInfo,
                                             taskloopOriginalDecls,
                                             sourceIVOriginalAddrs,
                                             generatedSourceIVSet);
        });
    if (mlir::failed(taskloopRes))
      return taskloopRes;

    if (!loopInfo) {
      for (auto [idx, final] : llvm::enumerate(generatedFinals))
        if (emitFinals[idx] && final)
          emitIgnoredExpr(final);
    }
    return taskloopRes;
  };

  auto emitTaskloop = [&]() -> mlir::LogicalResult {
    if (loopInfo)
      return emitTaskloopBody();

    if (mlir::failed(emitOpenMPTransformPreInits(*this, s.getPreInits())))
      return mlir::failure();

    bool preCondConstant = false;
    if (generatedPreCond->EvaluateAsBooleanCondition(preCondConstant,
                                                     getContext())) {
      if (!preCondConstant)
        return mlir::success();
      mlir::Value generatedLoopUpperBound =
          emitOpenMPIntegerExpr(*this, generatedLastIteration);
      mlir::Type generatedLoopTy = generatedLoopUpperBound.getType();
      mlir::Value generatedLoopLowerBound =
          createOpenMPIntegerConstant(*this, begin, generatedLoopTy, 0);
      mlir::Value generatedLoopStep =
          createOpenMPIntegerConstant(*this, begin, generatedLoopTy, 1);
      return emitTaskloopBody(generatedLoopLowerBound, generatedLoopUpperBound,
                              generatedLoopStep, generatedLoopTy);
    }

    mlir::LogicalResult taskloopRes = mlir::success();
    mlir::Value preCond = emitOpOnBoolExpr(begin, generatedPreCond);
    cir::IfOp::create(
        builder, begin, preCond, /*withElseRegion=*/false,
        [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
          mlir::OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
          mlir::Value generatedLoopUpperBound =
              emitOpenMPIntegerExpr(*this, generatedLastIteration);
          mlir::Type generatedLoopTy = generatedLoopUpperBound.getType();
          mlir::Value generatedLoopLowerBound =
              createOpenMPIntegerConstant(*this, begin, generatedLoopTy, 0);
          mlir::Value generatedLoopStep =
              createOpenMPIntegerConstant(*this, begin, generatedLoopTy, 1);
          taskloopRes = emitTaskloopBody(generatedLoopLowerBound,
                                         generatedLoopUpperBound,
                                         generatedLoopStep, generatedLoopTy);
          cir::YieldOp::create(thenBuilder, thenLoc);
        });
    return taskloopRes;
  };

  if (wrapInMaster) {
    auto masterOp = mlir::omp::MasterOp::create(builder, begin);
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *block = builder.createBlock(&masterOp.getRegion());
    builder.setInsertionPointToEnd(block);
    res = emitTaskloop();
    mlir::omp::TerminatorOp::create(builder, end);
  } else if (wrapInMasked) {
    auto maskedOp = mlir::omp::MaskedOp::create(builder, begin, mlir::Value{});
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *block = builder.createBlock(&maskedOp.getRegion());
    builder.setInsertionPointToEnd(block);
    res = emitTaskloop();
    mlir::omp::TerminatorOp::create(builder, end);
  } else {
    res = emitTaskloop();
  }

  return res;
}

mlir::LogicalResult CIRGenFunction::emitSimpleOMPParallelTaskLoopDirective(
    const OMPLoopDirective &s, llvm::StringRef directiveName,
    bool wrapInMaster, bool wrapInMasked, bool simd) {
  assert(!(wrapInMaster && wrapInMasked) &&
         "parallel taskloop wrapper cannot be both master and masked");

  mlir::LogicalResult res = mlir::success();
  llvm::SmallVector<mlir::Type> retTy;
  llvm::SmallVector<mlir::Value> operands;
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  auto parallelOp =
      mlir::omp::ParallelOp::create(builder, begin, retTy, operands);
  const CapturedStmt *parallelCaptured =
      s.getCapturedStmt(OpenMPDirectiveKind::OMPD_parallel);
  assert(parallelCaptured && "expected OpenMP parallel captured stmt");
  parallelOp->setAttr("cir.instrprof_func_name",
                      cgm.getStringAttrCached(
                          getOpenMPHelperPGOName(*parallelCaptured)));
  parallelOp->setAttr("cir.instrprof_func_linkage",
                      builder.getI32IntegerAttr(static_cast<int32_t>(
                          llvm::GlobalValue::InternalLinkage)));

  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *block = builder.createBlock(&parallelOp.getRegion());
    builder.setInsertionPointToEnd(block);

    cir::ScopeOp::create(
        builder, begin, [&](mlir::OpBuilder &b, mlir::Location loc) {
          auto pgoScope = enterOpenMPHelperRegionCounters(*parallelCaptured);
          incrementProfileCounter(parallelCaptured->getCapturedDecl()->getBody());
          LexicalScope ls{*this, begin, builder.getInsertionBlock()};
          res = emitSimpleOMPTaskLoopDirective(
              s, directiveName, wrapInMaster, wrapInMasked, simd);
        });

    mlir::omp::TerminatorOp::create(builder, end);
  }

  return res;
}

mlir::LogicalResult CIRGenFunction::emitSimpleOMPParallelDirective(
    const OMPExecutableDirective &s, llvm::StringRef directiveName,
    bool wrapInMaster, bool wrapInMasked) {
  assert(!(wrapInMaster && wrapInMasked) &&
         "parallel wrapper cannot be both master and masked");

  // Split the combined-directive clauses: the parallel-related clauses drive
  // the enclosing `omp.parallel`, while `filter` drives the inner `omp.masked`.
  llvm::SmallVector<const OMPClause *, 8> parallelClauses;
  const Expr *filterExpr = nullptr;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if:
    case llvm::omp::OMPC_num_threads:
    case llvm::omp::OMPC_severity:
    case llvm::omp::OMPC_message:
    case llvm::omp::OMPC_proc_bind:
    case llvm::omp::OMPC_firstprivate:
    case llvm::omp::OMPC_private:
    case llvm::omp::OMPC_copyin:
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_default:
    case llvm::omp::OMPC_shared:
      break;
    case llvm::omp::OMPC_filter:
      if (!wrapInMasked) {
        getCIRGenModule().errorNYI(
            clause->getBeginLoc(), (directiveName + " clause").str(),
            llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
        return mlir::failure();
      }
      filterExpr = cast<OMPFilterClause>(clause)->getThreadID();
      break;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), (directiveName + " clause").str(),
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      return mlir::failure();
    }
  }
  llvm::SmallVector<OpenMPCopyinMasterAddr, 4> copyinMasterAddrs =
      collectOpenMPCopyinMasterAddresses(*this, s);

  mlir::LogicalResult res = mlir::success();
  llvm::SmallVector<mlir::Type> retTy;
  llvm::SmallVector<mlir::Value> operands;
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  auto parallelOp =
      mlir::omp::ParallelOp::create(builder, begin, retTy, operands);
  OpenMPClauseEmissionInfo parallelClauseInfo =
      emitOpenMPClauses(parallelOp, parallelClauses);
  const CapturedStmt *parallelCaptured =
      s.getCapturedStmt(OpenMPDirectiveKind::OMPD_parallel);
  assert(parallelCaptured && "expected OpenMP parallel captured stmt");
  parallelOp->setAttr("cir.instrprof_func_name",
                      cgm.getStringAttrCached(
                          getOpenMPHelperPGOName(*parallelCaptured)));
  parallelOp->setAttr("cir.instrprof_func_linkage",
                      builder.getI32IntegerAttr(static_cast<int32_t>(
                          llvm::GlobalValue::InternalLinkage)));

  {
    mlir::OpBuilder::InsertionGuard guard(builder);

    llvm::SmallVector<mlir::Type, 4> privateArgTypes;
    llvm::SmallVector<mlir::Location, 4> privateArgLocs;
    for (mlir::Value privateVar : parallelOp.getPrivateVars()) {
      privateArgTypes.push_back(privateVar.getType());
      privateArgLocs.push_back(privateVar.getLoc());
    }

    mlir::Block *block = builder.createBlock(&parallelOp.getRegion(), {},
                                             privateArgTypes, privateArgLocs);
    builder.setInsertionPointToEnd(block);

    cir::ScopeOp::create(
        builder, begin, [&](mlir::OpBuilder &b, mlir::Location loc) {
          auto pgoScope = enterOpenMPHelperRegionCounters(*parallelCaptured);
          LexicalScope ls{*this, begin, b.getInsertionBlock()};
          llvm::SmallVector<std::pair<const VarDecl *, Address>, 4> savedDecls;
          auto privateArgs = mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                                 parallelOp.getOperation())
                                 .getPrivateBlockArgs();
          assert(parallelClauseInfo.privateIndices.size() ==
                     parallelClauseInfo.privateVars.size() &&
                 "mismatched OpenMP parallel private metadata");
          for (auto [index, vd] :
               llvm::zip_equal(parallelClauseInfo.privateIndices,
                               parallelClauseInfo.privateVars)) {
            assert(index < privateArgs.size() &&
                   "unexpected OpenMP parallel private block arg index");
            mlir::Value privateArg = privateArgs[index];
            auto ptrTy = mlir::cast<cir::PointerType>(privateArg.getType());
            auto it = localDeclMap.find(vd);
            if (it != localDeclMap.end())
              savedDecls.emplace_back(vd, it->second);
            replaceAddrOfLocalVar(
                vd, Address(privateArg, ptrTy.getPointee(),
                            getContext().getDeclAlign(vd)));
          }

          OpenMPTLSCopyinDeclMapScope tlsCopyinDecls(*this, s);
          (void)emitOpenMPCopyinClause(*this, s, copyinMasterAddrs);
          incrementProfileCounter(parallelCaptured->getCapturedDecl()->getBody());
          if (wrapInMaster) {
            auto masterOp = mlir::omp::MasterOp::create(b, begin);
            {
              mlir::OpBuilder::InsertionGuard guard(b);
              mlir::Block *masterBlock = b.createBlock(&masterOp.getRegion());
              b.setInsertionPointToEnd(masterBlock);
              res =
                  emitStmt(parallelCaptured->getCapturedStmt(),
                           /*useCurrentScope=*/true);
              mlir::omp::TerminatorOp::create(b, end);
            }
            b.setInsertionPointAfter(masterOp);
          } else if (wrapInMasked) {
            llvm::SmallPtrSet<const VarDecl *, 1> materializedFilterVars;
            if (filterExpr)
              materializeOpenMPCapturedClauseExpr(*this, filterExpr,
                                                  materializedFilterVars);
            mlir::Value filterVal =
                filterExpr ? emitOpenMPIntegerExpr(*this, filterExpr)
                           : mlir::Value{};
            auto maskedOp = mlir::omp::MaskedOp::create(b, begin, filterVal);
            {
              mlir::OpBuilder::InsertionGuard guard(b);
              mlir::Block *maskedBlock = b.createBlock(&maskedOp.getRegion());
              b.setInsertionPointToEnd(maskedBlock);
              res =
                  emitStmt(parallelCaptured->getCapturedStmt(),
                           /*useCurrentScope=*/true);
              mlir::omp::TerminatorOp::create(b, end);
            }
            b.setInsertionPointAfter(maskedOp);
          } else {
            llvm_unreachable("expected wrapped parallel OpenMP directive");
          }

          for (auto [vd, addr] : llvm::reverse(savedDecls))
            replaceAddrOfLocalVar(vd, addr);
          cir::YieldOp::create(b, end);
        });

    mlir::omp::TerminatorOp::create(builder, end);
  }

  return res;
}

namespace {

static mlir::omp::ClauseMemoryOrderKindAttr
getOpenMPAtomicMemoryOrderAttr(CIRGenFunction &cgf, llvm::AtomicOrdering ao) {
  mlir::omp::ClauseMemoryOrderKind kind;
  switch (ao) {
  case llvm::AtomicOrdering::SequentiallyConsistent:
    kind = mlir::omp::ClauseMemoryOrderKind::Seq_cst;
    break;
  case llvm::AtomicOrdering::AcquireRelease:
    kind = mlir::omp::ClauseMemoryOrderKind::Acq_rel;
    break;
  case llvm::AtomicOrdering::Acquire:
    kind = mlir::omp::ClauseMemoryOrderKind::Acquire;
    break;
  case llvm::AtomicOrdering::Release:
    kind = mlir::omp::ClauseMemoryOrderKind::Release;
    break;
  case llvm::AtomicOrdering::Monotonic:
    kind = mlir::omp::ClauseMemoryOrderKind::Relaxed;
    break;
  case llvm::AtomicOrdering::NotAtomic:
  case llvm::AtomicOrdering::Unordered:
    cgf.getCIRGenModule().errorNYI("unsupported OpenMP atomic ordering");
    return {};
  }

  return mlir::omp::ClauseMemoryOrderKindAttr::get(&cgf.getMLIRContext(),
                                                   kind);
}

static cir::MemOrder getCIRAtomicMemoryOrder(CIRGenFunction &cgf,
                                             llvm::AtomicOrdering ao) {
  switch (ao) {
  case llvm::AtomicOrdering::SequentiallyConsistent:
    return cir::MemOrder::SequentiallyConsistent;
  case llvm::AtomicOrdering::AcquireRelease:
    return cir::MemOrder::AcquireRelease;
  case llvm::AtomicOrdering::Acquire:
    return cir::MemOrder::Acquire;
  case llvm::AtomicOrdering::Release:
    return cir::MemOrder::Release;
  case llvm::AtomicOrdering::Monotonic:
    return cir::MemOrder::Relaxed;
  case llvm::AtomicOrdering::NotAtomic:
  case llvm::AtomicOrdering::Unordered:
    cgf.getCIRGenModule().errorNYI("unsupported OpenMP atomic ordering");
    return cir::MemOrder::Relaxed;
  }
}

static mlir::Value
emitOpenMPAtomicScalarExpr(CIRGenFunction &cgf, const Expr *e,
                           QualType targetTy) {
  RValue rv = cgf.emitAnyExpr(e);
  if (!rv.isScalar()) {
    cgf.getCIRGenModule().errorNYI(e->getSourceRange(),
                                   "non-scalar OpenMP atomic expression");
    return {};
  }

  mlir::Value value = rv.getValue();
  QualType sourceTy = e->getType();
  if (!cgf.getContext().hasSameType(sourceTy, targetTy))
    value = cgf.emitScalarConversion(value, sourceTy, targetTy, e->getExprLoc());
  return value;
}

static RValue emitOpenMPAtomicValueExpr(CIRGenFunction &cgf, const Expr *e) {
  RValue rv = cgf.emitAnyExpr(e);
  if (rv.isAggregate()) {
    cgf.getCIRGenModule().errorNYI(e->getSourceRange(),
                                   "aggregate OpenMP atomic expression");
    return RValue::getIgnored();
  }
  return rv;
}

static RValue getOpenMPAtomicValueAsRValue(CIRGenFunction &cgf, mlir::Value value,
                                           QualType type,
                                           SourceRange range) {
  switch (cgf.getEvaluationKind(type)) {
  case cir::TEK_Scalar:
    return RValue::get(value);
  case cir::TEK_Complex:
    return RValue::getComplex(value);
  case cir::TEK_Aggregate:
    cgf.getCIRGenModule().errorNYI(range, "aggregate OpenMP atomic update");
    return RValue::getIgnored();
  }
  llvm_unreachable("unexpected evaluation kind");
}

static mlir::Value getOpenMPAtomicValueOperand(RValue value) {
  if (value.isScalar())
    return value.getValue();
  assert(value.isComplex() && "expected scalar or complex OpenMP atomic value");
  return value.getComplexValue();
}

static RValue convertOpenMPAtomicValue(CIRGenFunction &cgf, RValue value,
                                       QualType sourceTy, QualType targetTy,
                                       SourceLocation loc) {
  if (cgf.getContext().hasSameType(sourceTy, targetTy))
    return value;

  switch (cgf.getEvaluationKind(targetTy)) {
  case cir::TEK_Scalar:
    if (value.isScalar())
      return RValue::get(
          cgf.emitScalarConversion(value.getValue(), sourceTy, targetTy, loc));
    assert(value.isComplex() && "expected scalar or complex OpenMP value");
    return RValue::get(cgf.emitComplexToScalarConversion(
        value.getComplexValue(), sourceTy, targetTy, loc));
  case cir::TEK_Complex: {
    auto &builder = cgf.getBuilder();
    mlir::Location mlirLoc = cgf.getLoc(loc);
    QualType targetElemTy = targetTy->castAs<ComplexType>()->getElementType();

    if (value.isScalar()) {
      mlir::Value scalar =
          cgf.emitScalarConversion(value.getValue(), sourceTy, targetElemTy, loc);
      cir::CastKind kind = targetElemTy->isFloatingType()
                               ? cir::CastKind::float_to_complex
                               : cir::CastKind::int_to_complex;
      return RValue::getComplex(
          builder.createCast(mlirLoc, kind, scalar, cgf.convertType(targetTy)));
    }

    QualType sourceElemTy = sourceTy->castAs<ComplexType>()->getElementType();
    cir::CastKind kind;
    if (sourceElemTy->isFloatingType() && targetElemTy->isFloatingType())
      kind = cir::CastKind::float_complex;
    else if (sourceElemTy->isFloatingType() && targetElemTy->isIntegerType())
      kind = cir::CastKind::float_complex_to_int_complex;
    else if (sourceElemTy->isIntegerType() && targetElemTy->isFloatingType())
      kind = cir::CastKind::int_complex_to_float_complex;
    else if (sourceElemTy->isIntegerType() && targetElemTy->isIntegerType())
      kind = cir::CastKind::int_complex;
    else
      llvm_unreachable("unexpected complex OpenMP atomic conversion");

    return RValue::getComplex(builder.createCast(
        mlirLoc, kind, value.getComplexValue(), cgf.convertType(targetTy)));
  }
  case cir::TEK_Aggregate:
    cgf.getCIRGenModule().errorNYI(targetTy.getAsString() +
                                   " aggregate OpenMP atomic value conversion");
    return RValue::getIgnored();
  }
  llvm_unreachable("unexpected evaluation kind");
}

static bool isOpenMPAtomicVectorComponentLValue(LValue lvalue) {
  return lvalue.isVectorElt() || lvalue.isExtVectorElt();
}

static RValue
getOpenMPAtomicVectorComponentValue(CIRGenFunction &cgf, LValue lvalue,
                                    mlir::Value vectorValue,
                                    SourceRange range, SourceLocation loc) {
  auto &builder = cgf.getBuilder();
  mlir::Location mlirLoc = cgf.getLoc(loc);
  if (lvalue.isVectorElt()) {
    return RValue::get(cir::VecExtractOp::create(builder, mlirLoc, vectorValue,
                                                 lvalue.getVectorIdx()));
  }

  assert(lvalue.isExtVectorElt() && "expected vector component lvalue");
  if (lvalue.getType()->isVectorType()) {
    cgf.getCIRGenModule().errorNYI(range,
                                   "non-scalar OpenMP atomic ext-vector "
                                   "component");
    return RValue::getIgnored();
  }

  int64_t fieldNo = cgf.getAccessedFieldNo(0, lvalue.getExtVectorElts());
  cir::ConstantOp index =
      builder.getConstInt(mlirLoc, builder.getSInt64Ty(), fieldNo);
  return RValue::get(
      cir::VecExtractOp::create(builder, mlirLoc, vectorValue, index));
}

static mlir::Value
setOpenMPAtomicVectorComponentValue(CIRGenFunction &cgf, LValue lvalue,
                                    mlir::Value vectorValue,
                                    mlir::Value elementValue,
                                    SourceRange range, SourceLocation loc) {
  auto &builder = cgf.getBuilder();
  mlir::Location mlirLoc = cgf.getLoc(loc);
  if (lvalue.isVectorElt()) {
    return cir::VecInsertOp::create(builder, mlirLoc, vectorValue, elementValue,
                                    lvalue.getVectorIdx());
  }

  assert(lvalue.isExtVectorElt() && "expected vector component lvalue");
  if (lvalue.getType()->isVectorType()) {
    cgf.getCIRGenModule().errorNYI(range,
                                   "non-scalar OpenMP atomic ext-vector "
                                   "component");
    return {};
  }

  int64_t fieldNo = cgf.getAccessedFieldNo(0, lvalue.getExtVectorElts());
  cir::ConstantOp index =
      builder.getConstInt(mlirLoc, builder.getSInt64Ty(), fieldNo);
  return cir::VecInsertOp::create(builder, mlirLoc, vectorValue, elementValue,
                                  index);
}

static RValue emitOpenMPAtomicSimpleLoad(CIRGenFunction &cgf,
                                         llvm::AtomicOrdering ao,
                                         const Expr *expr, LValue lvalue,
                                         SourceLocation loc) {
  CIRGenFunction::SourceLocRAIIObject sourceLoc(cgf, cgf.getLoc(loc));
  cir::MemOrder order = getCIRAtomicMemoryOrder(
      cgf, llvm::AtomicCmpXchgInst::getStrongestFailureOrdering(ao));
  if (lvalue.isGlobalReg())
    return cgf.emitLoadOfLValue(lvalue, loc);

  if (lvalue.isVectorElt()) {
    LValue vectorLValue = cgf.makeAddrLValue(lvalue.getVectorAddress(),
                                             lvalue.getType(),
                                             lvalue.getBaseInfo(),
                                             lvalue.getTBAAInfo());
    RValue vectorValue = cgf.emitAtomicLoad(vectorLValue, loc, order,
                                            lvalue.isVolatileQualified());
    mlir::Value element =
        cir::VecExtractOp::create(cgf.getBuilder(), cgf.getLoc(loc),
                                  vectorValue.getValue(),
                                  lvalue.getVectorIdx());
    return RValue::get(element);
  }

  if (lvalue.isExtVectorElt()) {
    const auto *extVectorExpr =
        dyn_cast<ExtVectorElementExpr>(expr->IgnoreParenImpCasts());
    if (!extVectorExpr || expr->getType()->isVectorType()) {
      cgf.getCIRGenModule().errorNYI(expr->getSourceRange(),
                                     "non-scalar OpenMP atomic vector read");
      return RValue::get(mlir::Value());
    }

    LValue vectorLValue = cgf.makeAddrLValue(lvalue.getExtVectorAddress(),
                                             extVectorExpr->getBase()->getType(),
                                             lvalue.getBaseInfo(),
                                             lvalue.getTBAAInfo());
    RValue vectorValue = cgf.emitAtomicLoad(vectorLValue, loc, order,
                                            lvalue.isVolatileQualified());
    int64_t fieldNo = cgf.getAccessedFieldNo(0, lvalue.getExtVectorElts());
    cir::ConstantOp index = cgf.getBuilder().getConstInt(
        cgf.getLoc(loc), cgf.getBuilder().getSInt64Ty(), fieldNo);
    mlir::Value element =
        cir::VecExtractOp::create(cgf.getBuilder(), cgf.getLoc(loc),
                                  vectorValue.getValue(), index);
    return RValue::get(element);
  }

  if (lvalue.isBitField()) {
    // Bitfield atomic read: atomically load the entire storage integer, then
    // extract the bitfield value. omp.atomic.read doesn't understand bitfield
    // extraction, so we bypass it and use a direct atomic load.
    const CIRGenBitFieldInfo &info = lvalue.getBitFieldInfo();
    Address storageAddr = lvalue.getBitFieldAddress();

    // Create a simple LValue for the storage integer so emitAtomicLoad works.
    QualType storageTy =
        cgf.getContext().getIntTypeForBitwidth(info.storageSize, /*Signed=*/false);
    LValue storageLV = cgf.makeAddrLValue(storageAddr, storageTy,
                                          lvalue.getBaseInfo(),
                                          lvalue.getTBAAInfo());
    RValue storageRV = cgf.emitAtomicLoad(storageLV, loc, order,
                                          lvalue.isVolatileQualified());

    // Store the atomically loaded value to a temporary, then extract the
    // bitfield from it.
    mlir::Location cirLoc = cgf.getLoc(loc);
    Address tempAddr = cgf.createMemTemp(storageTy, cirLoc, "atomic.bitfield");
    cgf.getBuilder().createStore(cirLoc, storageRV.getValue(), tempAddr);

    LValue tempBFLV = LValue::makeBitfield(tempAddr, info, lvalue.getType(),
                                           lvalue.getBaseInfo(),
                                           lvalue.getTBAAInfo());
    return cgf.emitLoadOfBitfieldLValue(tempBFLV, loc);
  }

  return cgf.emitAtomicLoad(lvalue, loc, order, lvalue.isVolatileQualified());
}

static mlir::LogicalResult
emitOpenMPAtomicSimpleStore(CIRGenFunction &cgf, const Expr *target,
                            RValue value, QualType sourceTy,
                            SourceLocation loc) {
  if (!value.isScalar()) {
    cgf.getCIRGenModule().errorNYI(target->getSourceRange(),
                                   "non-scalar OpenMP atomic store");
    return mlir::failure();
  }

  LValue targetLValue = cgf.emitLValue(target);
  QualType targetTy = target->getType().getNonReferenceType();
  mlir::Value stored = value.getValue();
  if (!cgf.getContext().hasSameType(sourceTy, targetTy))
    stored = cgf.emitScalarConversion(stored, sourceTy, targetTy, loc);

  CIRGenFunction::SourceLocRAIIObject sourceLoc(cgf, cgf.getLoc(loc));
  cgf.emitStoreThroughLValue(RValue::get(stored), targetLValue,
                             /*isInit=*/false);
  return mlir::success();
}

static mlir::LogicalResult
buildOpenMPAtomicUpdateValue(CIRGenFunction &cgf, const Expr *x,
                             const Expr *e, const Expr *ue,
                             bool isXLHSInRHSPart,
                             RValue xValue,
                             RValue exprValue, RValue &result) {
  if (!ue) {
    result = exprValue;
    QualType xTy = x->getType().getNonReferenceType();
    if (!cgf.getContext().hasSameType(e->getType(), xTy))
      result = RValue::get(cgf.emitScalarConversion(result.getValue(),
                                                    e->getType(), xTy,
                                                    e->getExprLoc()));
    return mlir::success();
  }

  const auto *updateExpr = dyn_cast<BinaryOperator>(ue->IgnoreImpCasts());
  if (!updateExpr) {
    cgf.getCIRGenModule().errorNYI(ue->getSourceRange(),
                                   "unexpected OpenMP atomic update form");
    return mlir::failure();
  }

  const auto *lhs =
      dyn_cast<OpaqueValueExpr>(updateExpr->getLHS()->IgnoreImpCasts());
  const auto *rhs =
      dyn_cast<OpaqueValueExpr>(updateExpr->getRHS()->IgnoreImpCasts());
  if (!lhs || !rhs) {
    cgf.getCIRGenModule().errorNYI(ue->getSourceRange(),
                                   "OpenMP atomic update without opaque values");
    return mlir::failure();
  }

  const OpaqueValueExpr *xRValExpr = isXLHSInRHSPart ? lhs : rhs;
  const OpaqueValueExpr *eRValExpr = isXLHSInRHSPart ? rhs : lhs;
  CIRGenFunction::OpaqueValueMapping mapExpr(cgf, eRValExpr, exprValue);
  CIRGenFunction::OpaqueValueMapping mapX(cgf, xRValExpr, xValue);
  result = cgf.emitAnyExpr(ue);
  if (!result.isScalar()) {
    cgf.getCIRGenModule().errorNYI(ue->getSourceRange(),
                                   "non-scalar OpenMP atomic update");
    return mlir::failure();
  }

  QualType xTy = x->getType().getNonReferenceType();
  if (!cgf.getContext().hasSameType(ue->getType(), xTy))
    result = RValue::get(cgf.emitScalarConversion(result.getValue(),
                                                  ue->getType(), xTy,
                                                  ue->getExprLoc()));
  return mlir::success();
}

static mlir::LogicalResult
buildOpenMPAtomicUpdateRegion(CIRGenFunction &cgf, CIRGenBuilderTy &builder,
                              mlir::Location start, mlir::Location end,
                              mlir::omp::AtomicUpdateOp op, mlir::Type argTy,
                              LValue xLValue, const Expr *x, const Expr *e,
                              const Expr *ue,
                              bool isXLHSInRHSPart) {
  builder.setInsertionPoint(op);
  QualType xTy = x->getType().getNonReferenceType();
  RValue exprValue = emitOpenMPAtomicValueExpr(cgf, e);
  if (exprValue.isIgnored())
    return mlir::failure();

  // The block argument type may have been widened (e.g. bool i1 -> i8).
  // Determine the original CIR type so we can insert narrowing/widening casts.
  mlir::Type origTy = cgf.convertType(xTy);
  bool isVectorComponent = isOpenMPAtomicVectorComponentLValue(xLValue);
  bool needsWidening = !isVectorComponent && (argTy != origTy);

  mlir::OpBuilder::InsertionGuard guard(builder);
  auto *recipeBlock =
      builder.createBlock(&op.getRegion(), op.getRegion().end(), {argTy},
                          SmallVector<mlir::Location, 1>{start});
  builder.setInsertionPointToEnd(recipeBlock);

  // Helper: narrow the widened block argument back to the original type so that
  // the update expression can use it.
  auto narrowBlockArg = [&]() -> mlir::Value {
    mlir::Value arg = recipeBlock->getArgument(0);
    if (!needsWidening)
      return arg;
    // bool case: i8 -> bool via int_to_bool
    if (mlir::isa<cir::BoolType>(origTy))
      return cir::CastOp::create(builder, start, origTy,
                                  cir::CastKind::int_to_bool, arg);
    return arg;
  };

  // Helper: widen the yielded value to match the block argument type.
  auto widenYieldValue = [&](mlir::Value val) -> mlir::Value {
    if (!needsWidening)
      return val;
    // bool case: bool -> i8 via bool_to_int
    if (mlir::isa<cir::BoolType>(origTy))
      return cir::CastOp::create(builder, end, argTy,
                                  cir::CastKind::bool_to_int, val);
    return val;
  };

  if (isVectorComponent) {
    RValue xValue = getOpenMPAtomicVectorComponentValue(
        cgf, xLValue, recipeBlock->getArgument(0), x->getSourceRange(),
        x->getExprLoc());
    if (xValue.isIgnored())
      return mlir::failure();

    RValue updateValue;
    if (mlir::failed(buildOpenMPAtomicUpdateValue(
            cgf, x, e, ue, isXLHSInRHSPart, xValue, exprValue, updateValue)))
      return mlir::failure();
    if (!updateValue.isScalar()) {
      cgf.getCIRGenModule().errorNYI(
          (ue ? ue : x)->getSourceRange(),
          "non-scalar OpenMP atomic vector component update");
      return mlir::failure();
    }

    mlir::Value yielded = setOpenMPAtomicVectorComponentValue(
        cgf, xLValue, recipeBlock->getArgument(0), updateValue.getValue(),
        x->getSourceRange(), (ue ? ue : e)->getExprLoc());
    if (!yielded)
      return mlir::failure();
    mlir::omp::YieldOp::create(builder, end, yielded);
    return mlir::success();
  }

  if (!ue) {
    RValue yielded =
        convertOpenMPAtomicValue(cgf, exprValue, e->getType(), xTy,
                                 e->getExprLoc());
    if (yielded.isIgnored())
      return mlir::failure();
    mlir::Value yieldVal =
        widenYieldValue(getOpenMPAtomicValueOperand(yielded));
    mlir::omp::YieldOp::create(builder, end, yieldVal);
    return mlir::success();
  }

  const auto *updateExpr = dyn_cast<BinaryOperator>(ue->IgnoreImpCasts());
  if (!updateExpr) {
    cgf.getCIRGenModule().errorNYI(ue->getSourceRange(),
                                   "unexpected OpenMP atomic update form");
    return mlir::failure();
  }

  const auto *lhs = dyn_cast<OpaqueValueExpr>(updateExpr->getLHS()
                                                  ->IgnoreImpCasts());
  const auto *rhs = dyn_cast<OpaqueValueExpr>(updateExpr->getRHS()
                                                  ->IgnoreImpCasts());
  if (!lhs || !rhs) {
    cgf.getCIRGenModule().errorNYI(ue->getSourceRange(),
                                   "OpenMP atomic update without opaque values");
    return mlir::failure();
  }

  const OpaqueValueExpr *xRValExpr = isXLHSInRHSPart ? lhs : rhs;
  const OpaqueValueExpr *eRValExpr = isXLHSInRHSPart ? rhs : lhs;
  CIRGenFunction::OpaqueValueMapping mapExpr(cgf, eRValExpr, exprValue);
  mlir::Value xVal = narrowBlockArg();
  RValue xValue = getOpenMPAtomicValueAsRValue(cgf, xVal,
                                               xTy, ue->getSourceRange());
  if (xValue.isIgnored())
    return mlir::failure();
  CIRGenFunction::OpaqueValueMapping mapX(cgf, xRValExpr, xValue);
  RValue updateValue = emitOpenMPAtomicValueExpr(cgf, ue);
  if (updateValue.isIgnored())
    return mlir::failure();

  RValue yielded =
      convertOpenMPAtomicValue(cgf, updateValue, ue->getType(), xTy,
                               ue->getExprLoc());
  if (yielded.isIgnored())
    return mlir::failure();
  mlir::Value yieldVal =
      widenYieldValue(getOpenMPAtomicValueOperand(yielded));
  mlir::omp::YieldOp::create(builder, end, yieldVal);
  return mlir::success();
}

/// Return a type suitable for LLVM atomic operations.  LLVM requires atomic
/// loads and stores to operate on byte-sized types, so \c cir::BoolType (which
/// lowers to \c i1) must be widened to \c i8.
static mlir::Type getAtomicElementType(mlir::Type ty) {
  if (mlir::isa<cir::BoolType>(ty))
    return cir::IntType::get(ty.getContext(), 8, /*isSigned=*/false);
  return ty;
}

static mlir::omp::AtomicReadOp
emitOpenMPAtomicRead(CIRGenFunction &cgf, CIRGenBuilderTy &builder,
                     mlir::Location loc, mlir::Value xPtr, mlir::Value vPtr,
                     mlir::Type elementTy, bool xIsVolatile, bool vIsVolatile,
                     mlir::omp::ClauseMemoryOrderKindAttr memoryOrder) {
  elementTy = getAtomicElementType(elementTy);
  mlir::IntegerAttr noHint;
  auto op = mlir::omp::AtomicReadOp::create(
      builder, loc, xPtr, vPtr, mlir::TypeAttr::get(elementTy), noHint,
      memoryOrder);
  if (xIsVolatile)
    op->setAttr("cir.x_volatile", builder.getUnitAttr());
  if (vIsVolatile)
    op->setAttr("cir.v_volatile", builder.getUnitAttr());
  return op;
}

static mlir::omp::AtomicWriteOp
emitOpenMPAtomicWrite(CIRGenFunction &cgf, CIRGenBuilderTy &builder,
                      mlir::Location loc, mlir::Value xPtr,
                      mlir::Value exprValue, bool xIsVolatile,
                      mlir::omp::ClauseMemoryOrderKindAttr memoryOrder) {
  // LLVM requires atomic stores to operate on byte-sized types.  Widen
  // bool (i1) to i8 for both the value and the pointer operand so that the
  // MLIR verifier (which checks pointer-element == value type) is satisfied.
  if (mlir::isa<cir::BoolType>(exprValue.getType())) {
    auto i8Ty = cir::IntType::get(exprValue.getContext(), 8, /*isSigned=*/false);
    exprValue = cir::CastOp::create(builder, loc, i8Ty,
                                    cir::CastKind::bool_to_int, exprValue);
    xPtr = cir::CastOp::create(builder, loc, cir::PointerType::get(i8Ty),
                                cir::CastKind::bitcast, xPtr);
  }
  mlir::IntegerAttr noHint;
  auto op = mlir::omp::AtomicWriteOp::create(builder, loc, xPtr, exprValue,
                                             noHint, memoryOrder);
  if (xIsVolatile)
    op->setAttr("cir.x_volatile", builder.getUnitAttr());
  return op;
}

static mlir::omp::AtomicReadOp
emitOpenMPAtomicRead(CIRGenFunction &cgf, CIRGenBuilderTy &builder,
                     mlir::Location loc, const Expr *x, const Expr *v,
                     mlir::omp::ClauseMemoryOrderKindAttr memoryOrder) {
  LValue xlv = cgf.emitLValue(x);
  LValue vlv = cgf.emitLValue(v);
  mlir::Value xPtr = xlv.getPointer();
  mlir::Value vPtr = vlv.getPointer();
  mlir::Type elementTy =
      mlir::cast<cir::PointerType>(xPtr.getType()).getPointee();
  return emitOpenMPAtomicRead(cgf, builder, loc, xPtr, vPtr, elementTy,
                              xlv.isVolatileQualified(),
                              vlv.isVolatileQualified(), memoryOrder);
}

static mlir::omp::AtomicWriteOp
emitOpenMPAtomicWrite(CIRGenFunction &cgf, CIRGenBuilderTy &builder,
                      mlir::Location loc, const Expr *x, const Expr *e,
                      mlir::omp::ClauseMemoryOrderKindAttr memoryOrder) {
  QualType xTy = x->getType().getNonReferenceType();
  LValue xlv = cgf.emitLValue(x);
  mlir::Value xPtr = xlv.getPointer();
  mlir::Value exprValue = emitOpenMPAtomicScalarExpr(cgf, e, xTy);
  if (!exprValue)
    return {};
  return emitOpenMPAtomicWrite(cgf, builder, loc, xPtr, exprValue,
                               xlv.isVolatileQualified(), memoryOrder);
}

static OpenMPClauseKind
getOpenMPAtomicDirectiveKind(CIRGenFunction &cgf, const OMPAtomicDirective &s) {
  llvm::SmallSet<OpenMPClauseKind, 2> kindsEncountered;
  OpenMPClauseKind kind = llvm::omp::OMPC_unknown;

  for (const OMPClause *clause : s.clauses()) {
    OpenMPClauseKind clauseKind = clause->getClauseKind();
    if (clauseKind == llvm::omp::OMPC_seq_cst ||
        clauseKind == llvm::omp::OMPC_acq_rel ||
        clauseKind == llvm::omp::OMPC_acquire ||
        clauseKind == llvm::omp::OMPC_release ||
        clauseKind == llvm::omp::OMPC_relaxed ||
        clauseKind == llvm::omp::OMPC_hint)
      continue;

    switch (clauseKind) {
    case llvm::omp::OMPC_weak:
      cgf.getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                     "OpenMP atomic weak clause");
      break;
    case llvm::omp::OMPC_compare:
    case llvm::omp::OMPC_fail:
      break;
    default:
      break;
    }

    kind = clauseKind;
    kindsEncountered.insert(clauseKind);
  }

  if (kindsEncountered.contains(llvm::omp::OMPC_compare) &&
      (kindsEncountered.contains(llvm::omp::OMPC_capture) ||
       kindsEncountered.contains(llvm::omp::OMPC_fail)))
    kind = llvm::omp::OMPC_compare;

  return kind;
}

static llvm::AtomicOrdering
getOpenMPAtomicOrdering(CIRGenFunction &cgf, const OMPAtomicDirective &s,
                        OpenMPClauseKind kind) {
  llvm::AtomicOrdering ao = cgf.getCIRGenModule().getOpenMPAtomicDefaultOrdering();
  bool memOrderingSpecified = false;
  if (s.getSingleClause<OMPSeqCstClause>()) {
    ao = llvm::AtomicOrdering::SequentiallyConsistent;
    memOrderingSpecified = true;
  } else if (s.getSingleClause<OMPAcqRelClause>()) {
    ao = llvm::AtomicOrdering::AcquireRelease;
    memOrderingSpecified = true;
  } else if (s.getSingleClause<OMPAcquireClause>()) {
    ao = llvm::AtomicOrdering::Acquire;
    memOrderingSpecified = true;
  } else if (s.getSingleClause<OMPReleaseClause>()) {
    ao = llvm::AtomicOrdering::Release;
    memOrderingSpecified = true;
  } else if (s.getSingleClause<OMPRelaxedClause>()) {
    ao = llvm::AtomicOrdering::Monotonic;
    memOrderingSpecified = true;
  }

  if (memOrderingSpecified)
    return ao;

  llvm::AtomicOrdering defaultOrder =
      cgf.getCIRGenModule().getOpenMPAtomicDefaultOrdering();
  if (defaultOrder == llvm::AtomicOrdering::Monotonic ||
      defaultOrder == llvm::AtomicOrdering::SequentiallyConsistent ||
      (defaultOrder == llvm::AtomicOrdering::AcquireRelease &&
       kind == llvm::omp::OMPC_capture)) {
    return defaultOrder;
  }

  if (defaultOrder == llvm::AtomicOrdering::AcquireRelease) {
    if (kind == llvm::omp::OMPC_unknown ||
        kind == llvm::omp::OMPC_update ||
        kind == llvm::omp::OMPC_write)
      return llvm::AtomicOrdering::Release;
    if (kind == llvm::omp::OMPC_read)
      return llvm::AtomicOrdering::Acquire;
  }

  return ao;
}

static llvm::AtomicOrdering
getOpenMPAtomicFailOrdering(const OMPAtomicDirective &s) {
  const auto *failClause = s.getSingleClause<OMPFailClause>();
  if (!failClause)
    return llvm::AtomicOrdering::NotAtomic;

  switch (failClause->getFailParameter()) {
  case llvm::omp::OMPC_relaxed:
    return llvm::AtomicOrdering::Monotonic;
  case llvm::omp::OMPC_acquire:
    return llvm::AtomicOrdering::Acquire;
  case llvm::omp::OMPC_seq_cst:
    return llvm::AtomicOrdering::SequentiallyConsistent;
  default:
    llvm_unreachable("unexpected OpenMP fail ordering");
  }
}

static void emitOpenMPFlush(CIRGenFunction &cgf, mlir::Location loc) {
  mlir::omp::FlushOp::create(cgf.getBuilder(), loc, mlir::ValueRange{});
}

static void emitOpenMPFlushAfterAtomicRead(CIRGenFunction &cgf,
                                           mlir::Location loc,
                                           llvm::AtomicOrdering ao) {
  switch (ao) {
  case llvm::AtomicOrdering::Acquire:
  case llvm::AtomicOrdering::AcquireRelease:
  case llvm::AtomicOrdering::SequentiallyConsistent:
    emitOpenMPFlush(cgf, loc);
    return;
  case llvm::AtomicOrdering::Monotonic:
  case llvm::AtomicOrdering::Release:
    return;
  case llvm::AtomicOrdering::NotAtomic:
  case llvm::AtomicOrdering::Unordered:
    cgf.getCIRGenModule().errorNYI("unsupported OpenMP atomic ordering");
    return;
  }
}

static void emitOpenMPFlushAfterAtomicWrite(CIRGenFunction &cgf,
                                            mlir::Location loc,
                                            llvm::AtomicOrdering ao) {
  switch (ao) {
  case llvm::AtomicOrdering::Release:
  case llvm::AtomicOrdering::AcquireRelease:
  case llvm::AtomicOrdering::SequentiallyConsistent:
    emitOpenMPFlush(cgf, loc);
    return;
  case llvm::AtomicOrdering::Acquire:
  case llvm::AtomicOrdering::Monotonic:
    return;
  case llvm::AtomicOrdering::NotAtomic:
  case llvm::AtomicOrdering::Unordered:
    cgf.getCIRGenModule().errorNYI("unsupported OpenMP atomic ordering");
    return;
  }
}

static void emitOpenMPFlushAfterAtomicCapture(CIRGenFunction &cgf,
                                              mlir::Location loc,
                                              llvm::AtomicOrdering ao) {
  if (cgf.getLangOpts().OpenMP >= 51)
    return;

  switch (ao) {
  case llvm::AtomicOrdering::Release:
  case llvm::AtomicOrdering::Acquire:
  case llvm::AtomicOrdering::AcquireRelease:
  case llvm::AtomicOrdering::SequentiallyConsistent:
    emitOpenMPFlush(cgf, loc);
    return;
  case llvm::AtomicOrdering::Monotonic:
    return;
  case llvm::AtomicOrdering::NotAtomic:
  case llvm::AtomicOrdering::Unordered:
    cgf.getCIRGenModule().errorNYI("unsupported OpenMP atomic ordering");
    return;
  }
}

static void emitOpenMPFlushAfterAtomicCompare(CIRGenFunction &cgf,
                                              mlir::Location loc,
                                              llvm::AtomicOrdering ao) {
  switch (ao) {
  case llvm::AtomicOrdering::Release:
  case llvm::AtomicOrdering::AcquireRelease:
  case llvm::AtomicOrdering::SequentiallyConsistent:
    emitOpenMPFlush(cgf, loc);
    return;
  case llvm::AtomicOrdering::Acquire:
  case llvm::AtomicOrdering::Monotonic:
    return;
  case llvm::AtomicOrdering::NotAtomic:
  case llvm::AtomicOrdering::Unordered:
    cgf.getCIRGenModule().errorNYI("unsupported OpenMP atomic ordering");
    return;
  }
}

static cir::AtomicFetchKind
getOpenMPAtomicCompareFetchKind(CIRGenFunction &cgf,
                                const OMPAtomicDirective &s, QualType xTy) {
  const auto *cond =
      dyn_cast<BinaryOperator>(s.getCondExpr()->IgnoreParenImpCasts());
  if (!cond) {
    cgf.getCIRGenModule().errorNYI(s.getCondExpr()->getSourceRange(),
                                   "unexpected OpenMP atomic compare form");
    return cir::AtomicFetchKind::Min;
  }

  auto getMinKind = [&]() -> cir::AtomicFetchKind {
    if (xTy->isFloatingType())
      return cir::AtomicFetchKind::Min;
    if (xTy->hasSignedIntegerRepresentation())
      return cir::AtomicFetchKind::Min;
    return cir::AtomicFetchKind::UMin;
  };
  auto getMaxKind = [&]() -> cir::AtomicFetchKind {
    if (xTy->isFloatingType())
      return cir::AtomicFetchKind::Max;
    if (xTy->hasSignedIntegerRepresentation())
      return cir::AtomicFetchKind::Max;
    return cir::AtomicFetchKind::UMax;
  };

  switch (cond->getOpcode()) {
  case BO_GT:
    return s.isXLHSInRHSPart() ? getMinKind() : getMaxKind();
  case BO_LT:
    return s.isXLHSInRHSPart() ? getMaxKind() : getMinKind();
  default:
    cgf.getCIRGenModule().errorNYI(s.getCondExpr()->getSourceRange(),
                                   "unexpected OpenMP atomic compare opcode");
    return cir::AtomicFetchKind::Min;
  }
}

static cir::CmpOpKind
getOpenMPAtomicCompareCapturePredicate(cir::AtomicFetchKind kind) {
  switch (kind) {
  case cir::AtomicFetchKind::Max:
  case cir::AtomicFetchKind::UMax:
    return cir::CmpOpKind::gt;
  case cir::AtomicFetchKind::Min:
  case cir::AtomicFetchKind::UMin:
    return cir::CmpOpKind::lt;
  default:
    llvm_unreachable("unexpected OpenMP atomic compare fetch kind");
  }
}

static void emitOpenMPAtomicCompareCaptureStore(CIRGenFunction &cgf,
                                                const Expr *target,
                                                mlir::Value value) {
  cgf.emitStoreOfScalar(value, cgf.emitLValue(target), /*isInit=*/false);
}

static void emitOpenMPAtomicCompareCaptureResult(CIRGenFunction &cgf,
                                                 mlir::Location loc,
                                                 const Expr *r,
                                                 mlir::Value success) {
  if (!r)
    return;

  QualType rTy = r->getType().getNonReferenceType();
  mlir::Value result = success;
  if (!rTy->isBooleanType()) {
    mlir::Type cirRTy = cgf.convertType(rTy);
    result = cgf.getBuilder().createCast(loc, cir::CastKind::bool_to_int,
                                         success, cirRTy);
    if (auto intTy = mlir::dyn_cast<cir::IntType>(cirRTy); intTy.isSigned())
      result = cgf.getBuilder().createNeg(result);
  }
  emitOpenMPAtomicCompareCaptureStore(cgf, r, result);
}

static void emitOpenMPAtomicCompare(CIRGenFunction &cgf,
                                    const OMPAtomicDirective &s,
                                    llvm::AtomicOrdering ao,
                                    llvm::AtomicOrdering failAo) {
  mlir::Location loc = cgf.getLoc(s.getBeginLoc());
  CIRGenBuilderTy &builder = cgf.getBuilder();
  LValue xLValue = cgf.emitLValue(s.getX());
  mlir::Value xPtr = xLValue.getPointer();
  QualType xTy = s.getX()->getType().getNonReferenceType();
  bool isVolatile = xLValue.isVolatileQualified();
  mlir::IntegerAttr alignmentAttr;
  if (!xLValue.getAddress().getAlignment().isZero()) {
    alignmentAttr = cgf.getI64AttrCached(
        xLValue.getAddress().getAlignment().getAsAlign().value());
  }

  const auto *cond =
      dyn_cast<BinaryOperator>(s.getCondExpr()->IgnoreParenImpCasts());
  if (!cond) {
    cgf.getCIRGenModule().errorNYI(s.getCondExpr()->getSourceRange(),
                                   "unexpected OpenMP atomic compare form");
    return;
  }

  switch (cond->getOpcode()) {
  case BO_EQ: {
    mlir::Value expected = emitOpenMPAtomicScalarExpr(cgf, s.getExpr(), xTy);
    mlir::Value desired = emitOpenMPAtomicScalarExpr(cgf, s.getD(), xTy);
    llvm::AtomicOrdering effectiveFailAo =
        failAo == llvm::AtomicOrdering::NotAtomic
            ? llvm::AtomicCmpXchgInst::getStrongestFailureOrdering(ao)
            : failAo;
    auto cmpxchg = cir::AtomicCmpXchgOp::create(
        builder, loc, expected.getType(), builder.getBoolTy(), xPtr, expected,
        desired, getCIRAtomicMemoryOrder(cgf, ao),
        getCIRAtomicMemoryOrder(cgf, effectiveFailAo), alignmentAttr,
        /*weak=*/false, /*is_volatile=*/isVolatile,
        /*sync_scope=*/cir::SyncScopeKindAttr{});

    if (s.getV()) {
      if (s.isFailOnly()) {
        mlir::Value failed = builder.createNot(cmpxchg.getSuccess());
        cir::IfOp::create(
            builder, loc, failed, /*withElseRegion=*/false,
            [&](mlir::OpBuilder &, mlir::Location) {
              emitOpenMPAtomicCompareCaptureStore(cgf, s.getV(),
                                                  cmpxchg.getOld());
              builder.createYield(loc);
            });
      } else if (s.isPostfixUpdate()) {
        emitOpenMPAtomicCompareCaptureStore(cgf, s.getV(), cmpxchg.getOld());
      } else {
        mlir::Value captured = builder.createSelect(
            loc, cmpxchg.getSuccess(), expected, cmpxchg.getOld());
        emitOpenMPAtomicCompareCaptureStore(cgf, s.getV(), captured);
      }
    }

    emitOpenMPAtomicCompareCaptureResult(cgf, loc, s.getR(),
                                         cmpxchg.getSuccess());
    break;
  }
  case BO_LT:
  case BO_GT: {
    mlir::Value value = emitOpenMPAtomicScalarExpr(cgf, s.getExpr(), xTy);
    cir::AtomicFetchKind fetchKind =
        getOpenMPAtomicCompareFetchKind(cgf, s, xTy);
    auto fetch = cir::AtomicFetchOp::create(
        builder, loc, xPtr, value, getOpenMPAtomicCompareFetchKind(cgf, s, xTy),
        getCIRAtomicMemoryOrder(cgf, ao), /*alignment=*/mlir::IntegerAttr{},
        /*is_volatile=*/isVolatile, /*fetchFirst=*/true,
        /*sync_scope=*/nullptr);

    if (s.getR()) {
      cgf.getCIRGenModule().errorNYI(s.getSourceRange(),
                                     "OpenMP atomic compare result capture for "
                                     "non-equality compare");
      return;
    }
    if (s.isFailOnly()) {
      cgf.getCIRGenModule().errorNYI(s.getSourceRange(),
                                     "OpenMP atomic compare fail-only for "
                                     "non-equality compare");
      return;
    }

    if (s.getV()) {
      if (s.isPostfixUpdate()) {
        emitOpenMPAtomicCompareCaptureStore(cgf, s.getV(), fetch.getResult());
      } else {
        mlir::Value cmp = builder.createCompare(
            loc, getOpenMPAtomicCompareCapturePredicate(fetchKind),
            fetch.getResult(), value);
        mlir::Value captured =
            builder.createSelect(loc, cmp, value, fetch.getResult());
        emitOpenMPAtomicCompareCaptureStore(cgf, s.getV(), captured);
      }
    }
    break;
  }
  default:
    cgf.getCIRGenModule().errorNYI(s.getCondExpr()->getSourceRange(),
                                   "unexpected OpenMP atomic compare opcode");
    return;
  }

  emitOpenMPFlushAfterAtomicCompare(cgf, loc, ao);
}

static mlir::LogicalResult
emitOpenMPAtomicDirectiveWithGlobalRegX(CIRGenFunction &cgf,
                                        const OMPAtomicDirective &s,
                                        OpenMPClauseKind kind,
                                        llvm::AtomicOrdering atomicOrdering,
                                        mlir::Location loc) {
  LValue xLValue = cgf.emitLValue(s.getX());
  assert(xLValue.isGlobalReg() && "expected global register lvalue");

  QualType xTy = s.getX()->getType().getNonReferenceType();
  auto loadX = [&]() {
    return emitOpenMPAtomicSimpleLoad(cgf, atomicOrdering, s.getX(), xLValue,
                                      s.getX()->getExprLoc());
  };

  switch (kind) {
  case llvm::omp::OMPC_read: {
    RValue loaded = loadX();
    emitOpenMPFlushAfterAtomicRead(cgf, loc, atomicOrdering);
    if (mlir::failed(emitOpenMPAtomicSimpleStore(
            cgf, s.getV(), loaded, xTy, s.getV()->getExprLoc())))
      return mlir::failure();
    cgf.checkAndEmitOpenMPLastprivateConditional(s.getV());
    return mlir::success();
  }
  case llvm::omp::OMPC_write: {
    RValue exprValue = cgf.emitAnyExpr(s.getExpr());
    if (!exprValue.isScalar()) {
      cgf.getCIRGenModule().errorNYI(
          s.getExpr()->getSourceRange(), "non-scalar OpenMP atomic expression");
      return mlir::failure();
    }
    if (mlir::failed(emitOpenMPAtomicSimpleStore(
            cgf, s.getX(), exprValue, s.getExpr()->getType(),
            s.getX()->getExprLoc())))
      return mlir::failure();
    cgf.checkAndEmitOpenMPLastprivateConditional(s.getX());
    emitOpenMPFlushAfterAtomicWrite(cgf, loc, atomicOrdering);
    return mlir::success();
  }
  case llvm::omp::OMPC_unknown:
  case llvm::omp::OMPC_update: {
    RValue exprValue = cgf.emitAnyExpr(s.getExpr());
    if (!exprValue.isScalar()) {
      cgf.getCIRGenModule().errorNYI(
          s.getExpr()->getSourceRange(), "non-scalar OpenMP atomic expression");
      return mlir::failure();
    }
    RValue oldValue = loadX();
    RValue newValue;
    if (mlir::failed(buildOpenMPAtomicUpdateValue(
            cgf, s.getX(), s.getExpr(), s.getUpdateExpr(),
            s.isXLHSInRHSPart(), oldValue, exprValue, newValue)))
      return mlir::failure();
    if (mlir::failed(emitOpenMPAtomicSimpleStore(
            cgf, s.getX(), newValue, xTy, s.getX()->getExprLoc())))
      return mlir::failure();
    cgf.checkAndEmitOpenMPLastprivateConditional(s.getX());
    emitOpenMPFlushAfterAtomicWrite(cgf, loc, atomicOrdering);
    return mlir::success();
  }
  case llvm::omp::OMPC_capture: {
    RValue exprValue = cgf.emitAnyExpr(s.getExpr());
    if (!exprValue.isScalar()) {
      cgf.getCIRGenModule().errorNYI(
          s.getExpr()->getSourceRange(), "non-scalar OpenMP atomic expression");
      return mlir::failure();
    }
    RValue oldValue = loadX();
    RValue newValue;
    if (mlir::failed(buildOpenMPAtomicUpdateValue(
            cgf, s.getX(), s.getExpr(), s.getUpdateExpr(),
            s.isXLHSInRHSPart(), oldValue, exprValue, newValue)))
      return mlir::failure();
    if (mlir::failed(emitOpenMPAtomicSimpleStore(
            cgf, s.getX(), newValue, xTy, s.getX()->getExprLoc())))
      return mlir::failure();

    RValue capturedValue = s.isPostfixUpdate() ? oldValue : newValue;
    if (mlir::failed(emitOpenMPAtomicSimpleStore(
            cgf, s.getV(), capturedValue, xTy, s.getV()->getExprLoc())))
      return mlir::failure();
    cgf.checkAndEmitOpenMPLastprivateConditional(s.getX());
    cgf.checkAndEmitOpenMPLastprivateConditional(s.getV());
    emitOpenMPFlushAfterAtomicCapture(cgf, loc, atomicOrdering);
    return mlir::success();
  }
  case llvm::omp::OMPC_compare:
    cgf.getCIRGenModule().errorNYI(
        s.getBeginLoc(), "OpenMP atomic compare with register variable");
    return mlir::failure();
  case llvm::omp::OMPC_fail:
  case llvm::omp::OMPC_hint:
  case llvm::omp::OMPC_weak:
    llvm_unreachable("unsupported clauses handled before emission");
  default:
    cgf.getCIRGenModule().errorNYI(s.getSourceRange(),
                                   "unexpected OpenMP atomic clause kind");
    return mlir::failure();
  }
}

} // namespace

mlir::FlatSymbolRefAttr CIRGenModule::getOrCreateOpenMPCriticalDecl(
    llvm::StringRef criticalName, std::optional<uint64_t> hint,
    mlir::Location loc) {
  if (auto it = openMPCriticalDecls.find(criticalName);
      it != openMPCriticalDecls.end())
    return it->second;

  std::string baseName = ("__omp_critical_" + criticalName).str();
  std::string symbolName = baseName;
  unsigned suffix = 0;
  while (mlir::Operation *existing = getGlobalValue(symbolName)) {
    if (auto decl = mlir::dyn_cast<mlir::omp::CriticalDeclareOp>(existing)) {
      auto symbol = mlir::FlatSymbolRefAttr::get(decl.getSymNameAttr());
      auto [mapIt, _] = openMPCriticalDecls.try_emplace(criticalName, symbol);
      return mapIt->second;
    }
    symbolName = (baseName + "." + llvm::Twine(++suffix)).str();
  }

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(getModule().getBody());
  auto hintAttr = hint ? builder.getI64IntegerAttr(*hint) : mlir::IntegerAttr{};
  auto decl = mlir::omp::CriticalDeclareOp::create(
      builder, loc, getStringAttrCached(symbolName), hintAttr);
  insertGlobalSymbol(decl);
  auto symbol = mlir::FlatSymbolRefAttr::get(decl.getSymNameAttr());
  auto [mapIt, _] = openMPCriticalDecls.try_emplace(criticalName, symbol);
  return mapIt->second;
}

mlir::LogicalResult
CIRGenFunction::emitOMPScopeDirective(const OMPScopeDirective &s) {
  struct OpenMPScopePrivateInfo {
    const VarDecl *originalVD = nullptr;
    const VarDecl *privateVD = nullptr;
    const VarDecl *initVD = nullptr;
    Address initAddr = Address::invalid();
  };

  auto getScopeReductionBaseVarDecl = [](const Expr *e) -> const VarDecl * {
    e = e->IgnoreParenImpCasts();
    while (true) {
      if (const auto *section = dyn_cast<ArraySectionExpr>(e)) {
        e = section->getBase()->IgnoreParenImpCasts();
        continue;
      }
      if (const auto *subscript = dyn_cast<ArraySubscriptExpr>(e)) {
        e = subscript->getBase()->IgnoreParenImpCasts();
        continue;
      }
      if (const auto *member = dyn_cast<MemberExpr>(e)) {
        e = member->getBase()->IgnoreParenImpCasts();
        continue;
      }
      const auto *dre = dyn_cast<DeclRefExpr>(e);
      return dre ? dyn_cast<VarDecl>(dre->getDecl()) : nullptr;
    }
  };

  auto emitUnsupportedScopeClause = [&](const OMPClause *clause) {
    getCIRGenModule().errorNYI(
        clause->getBeginLoc(), "OpenMP OMPScopeDirective clause",
        llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
  };

  bool nowait = false;
  llvm::SmallPtrSet<const VarDecl *, 8> emittedAsPrivate;
  llvm::SmallPtrSet<const VarDecl *, 8> emittedAsFirstprivate;
  llvm::SmallPtrSet<const VarDecl *, 8> emittedAsReduction;
  llvm::SmallVector<const OMPClause *, 4> reductionClauses;
  llvm::SmallVector<const VarDecl *, 4> reductionPrivateVDs;
  llvm::SmallVector<OpenMPScopePrivateInfo, 8> privateInfos;

  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_nowait:
      nowait = true;
      break;
    case llvm::omp::OMPC_allocate:
    case llvm::omp::OMPC_shared:
      break;
    case llvm::omp::OMPC_default: {
      const auto *defaultClause = cast<OMPDefaultClause>(clause);
      if (defaultClause->getDefaultVC() != OMPC_DEFAULT_VC_all) {
        emitUnsupportedScopeClause(clause);
        return mlir::failure();
      }
      switch (defaultClause->getDefaultKind()) {
      case llvm::omp::OMP_DEFAULT_none:
      case llvm::omp::OMP_DEFAULT_shared:
        break;
      case llvm::omp::OMP_DEFAULT_private:
      case llvm::omp::OMP_DEFAULT_firstprivate:
      case llvm::omp::OMP_DEFAULT_unknown:
        emitUnsupportedScopeClause(clause);
        return mlir::failure();
      }
      break;
    }
    case llvm::omp::OMPC_private: {
      const auto *privateClause = cast<OMPPrivateClause>(clause);
      auto varIt = privateClause->varlist_begin();
      for (const Expr *privateCopyExpr : privateClause->private_copies()) {
        const auto *originalVD =
            cast<VarDecl>(cast<DeclRefExpr>(*varIt)->getDecl());
        const VarDecl *canonical = originalVD->getCanonicalDecl();
        if (emittedAsPrivate.insert(canonical).second) {
          const auto *privateVD =
              cast<VarDecl>(cast<DeclRefExpr>(privateCopyExpr)->getDecl());
          privateInfos.push_back({originalVD, privateVD});
        }
        ++varIt;
      }
      break;
    }
    case llvm::omp::OMPC_firstprivate: {
      const auto *firstprivateClause = cast<OMPFirstprivateClause>(clause);
      auto varIt = firstprivateClause->varlist_begin();
      auto initIt = firstprivateClause->inits().begin();
      for (const Expr *privateCopyExpr : firstprivateClause->private_copies()) {
        const auto *originalVD =
            cast<VarDecl>(cast<DeclRefExpr>(*varIt)->getDecl());
        const VarDecl *canonical = originalVD->getCanonicalDecl();
        if (emittedAsFirstprivate.insert(canonical).second) {
          if (originalVD->getType()->isArrayType()) {
            emitUnsupportedScopeClause(clause);
            return mlir::failure();
          }

          const auto *privateVD =
              cast<VarDecl>(cast<DeclRefExpr>(privateCopyExpr)->getDecl());
          const auto *initVD =
              cast<VarDecl>(cast<DeclRefExpr>(*initIt)->getDecl());
          privateInfos.push_back(
              {originalVD, privateVD, initVD, emitLValue(*varIt).getAddress()});
        }
        ++varIt;
        ++initIt;
      }
      break;
    }
    case llvm::omp::OMPC_reduction: {
      const auto *reductionClause = cast<OMPReductionClause>(clause);
      if (reductionClause->getModifier() != OMPC_REDUCTION_unknown &&
          reductionClause->getModifier() != OMPC_REDUCTION_default) {
        emitUnsupportedScopeClause(clause);
        return mlir::failure();
      }

      reductionClauses.push_back(clause);
      for (auto [varExpr, privateExpr] :
           llvm::zip_equal(reductionClause->varlist(),
                           reductionClause->privates())) {
        const VarDecl *originalVD = getScopeReductionBaseVarDecl(varExpr);
        if (!originalVD) {
          emitUnsupportedScopeClause(clause);
          return mlir::failure();
        }

        const VarDecl *canonical = originalVD->getCanonicalDecl();
        if (emittedAsReduction.insert(canonical).second) {
          const auto *privateVD =
              cast<VarDecl>(cast<DeclRefExpr>(privateExpr)->getDecl());
          privateInfos.push_back({originalVD, privateVD});
          reductionPrivateVDs.push_back(privateVD);
        }
      }
      break;
    }
    default:
      emitUnsupportedScopeClause(clause);
      return mlir::failure();
    }
  }

  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::LogicalResult res =
      mlir::success();
  cir::ScopeOp::create(
      builder, begin, [&](mlir::OpBuilder &, mlir::Location) {
        CIRGenFunction::LexicalScope ls{*this, begin,
                                        builder.getInsertionBlock()};
        // Build the CIR-private `cir.omp.scope_reduction` op directly
        // (no longer routed through the deleted upstream
        // `mlir::omp::ScopeReductionOp`). The shared
        // `emitOpenMPClauses<cir::OmpScopeReductionOp>` template
        // specialization populates `reduction_vars` /
        // `reduction_byref` / `reduction_syms`; this code then appends
        // the private accumulators (with the per-byref alloca dance)
        // and erases the op if no reductions ended up being collected.
        cir::OmpScopeReductionOp scopeReductionOp;
        if (!reductionClauses.empty()) {
          scopeReductionOp = cir::OmpScopeReductionOp::create(
              builder, begin,
              /*nowait=*/false,
              /*reduction_mod=*/mlir::Attribute{},
              /*reduction_vars=*/mlir::ValueRange{},
              /*reduction_byref=*/mlir::DenseBoolArrayAttr{},
              /*reduction_syms=*/mlir::ArrayAttr{},
              /*private_reduction_vars=*/mlir::ValueRange{});
          if (nowait)
            scopeReductionOp->setAttr("nowait", builder.getUnitAttr());
          (void)emitOpenMPClauses(scopeReductionOp, reductionClauses);
        }
        llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 8> declGuards;
        for (const auto &[originalVD, privateVD, initVD, initAddr] :
             privateInfos) {
          if (initVD) {
            DeclMapRevertingRAII initGuard(*this, initVD);
            if (localDeclMap.count(initVD)) {
              replaceAddrOfLocalVar(initVD, initAddr);
              emitVarDecl(*privateVD);
            } else {
              SymTableScopeTy initScope(symbolTable);
              setAddrOfLocalVar(initVD, initAddr);
              emitVarDecl(*privateVD);
            }
          } else {
            emitVarDecl(*privateVD);
          }
          declGuards.push_back(
              std::make_unique<DeclMapRevertingRAII>(*this, originalVD));
          replaceAddrOfLocalVar(originalVD, getAddrOfLocalVar(privateVD));
        }
        res = emitStmt(s.getInnermostCapturedStmt()->getCapturedStmt(),
                       /*useCurrentScope=*/true);
        if (mlir::succeeded(res) && scopeReductionOp) {
          assert(scopeReductionOp.getNumReductionVars() ==
                     reductionPrivateVDs.size() &&
                 "mismatched OpenMP scope reduction bookkeeping");
          auto reductionByRef = scopeReductionOp.getReductionByref();
          assert(reductionByRef &&
                 reductionByRef->size() == reductionPrivateVDs.size() &&
                 "mismatched OpenMP scope reduction byref bookkeeping");

          llvm::SmallVector<mlir::Value, 4> privateReductionVars;
          for (auto [byRef, privateVD] :
               llvm::zip_equal(*reductionByRef, reductionPrivateVDs)) {
            Address privateAddr = getAddrOfLocalVar(privateVD);
            mlir::Value privateReductionVar = privateAddr.getPointer();
            if (byRef) {
              Address slot = createTempAlloca(
                  privateReductionVar.getType(), getPointerAlign(),
                  getLoc(privateVD->getLocation()),
                  "__omp_scope_reduction_private");
              builder.createStore(getLoc(privateVD->getLocation()),
                                  privateReductionVar, slot);
              privateReductionVar = slot.getPointer();
            }
            privateReductionVars.push_back(privateReductionVar);
          }
          scopeReductionOp.getPrivateReductionVarsMutable().append(
              privateReductionVars);
          scopeReductionOp->moveBefore(builder.getInsertionBlock(),
                                       builder.getInsertionPoint());
        }
      });

  if (!nowait)
    mlir::omp::BarrierOp::create(builder, begin);

  return res;
}
mlir::LogicalResult
CIRGenFunction::emitOMPErrorDirective(const OMPErrorDirective &s) {
  const auto *messageClause = s.getSingleClause<OMPMessageClause>();
  const auto *severityClause = s.getSingleClause<OMPSeverityClause>();
  mlir::Location loc = getLoc(s.getBeginLoc());

  mlir::Value ompLoc = emitOpenMPRuntimeLocation(s.getBeginLoc(), 0);
  mlir::Value severity = builder.getConstInt(
      loc, builder.getSInt32Ty(),
      (!severityClause ||
       severityClause->getSeverityKind() == OMPC_SEVERITY_fatal)
          ? 2
          : 1);

  auto charPtrTy = builder.getPointerTo(builder.getSInt8Ty());
  mlir::Value message = builder.getNullValue(charPtrTy, loc);
  if (const Expr *messageExpr =
          messageClause ? messageClause->getMessageString() : nullptr) {
    message = emitScalarExpr(messageExpr);
    if (message.getType() != charPtrTy)
      message = builder.createBitcast(loc, message, charPtrTy);
  }

  cir::FuncOp errorFn = cgm.createRuntimeFunction(
      builder.getFuncType({ompLoc.getType(), builder.getSInt32Ty(), charPtrTy},
                          builder.getVoidTy()),
      "__kmpc_error");
  emitRuntimeCall(loc, errorFn, {ompLoc, severity, message});
  return mlir::success();
}

/// Create the region entry block for an OpenMP operation with private block
/// args, set up private-arg replacement inside a LexicalScope, call
/// \p bodyEmitter, restore addresses, and add the terminator.
template <typename Op>
static mlir::LogicalResult emitOMPPrivateRegionBody(
    CIRGenFunction &cgf, Op &op,
    const CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo,
    mlir::Location begin, mlir::Location end,
    llvm::function_ref<mlir::LogicalResult()> bodyEmitter) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::OpBuilder::InsertionGuard guard(builder);

  llvm::SmallVector<mlir::Type, 4> argTypes;
  llvm::SmallVector<mlir::Location, 4> argLocs;
  if constexpr (std::is_same_v<Op, mlir::omp::TaskOp>) {
    for (mlir::Value inReductionVar : op.getInReductionVars()) {
      argTypes.push_back(inReductionVar.getType());
      argLocs.push_back(inReductionVar.getLoc());
    }
  }
  for (mlir::Value privateVar : op.getPrivateVars()) {
    argTypes.push_back(privateVar.getType());
    argLocs.push_back(privateVar.getLoc());
  }
  if constexpr (std::is_same_v<Op, mlir::omp::ParallelOp>) {
    for (mlir::Value reductionVar : op.getReductionVars()) {
      argTypes.push_back(reductionVar.getType());
      argLocs.push_back(reductionVar.getLoc());
    }
  }
  builder.createBlock(&op.getRegion(), {}, argTypes, argLocs);
  builder.setInsertionPointToEnd(&op.getRegion().back());

  mlir::LogicalResult res = mlir::success();
  cir::ScopeOp::create(
      builder, begin, /*scopeBuilder=*/
      [&](mlir::OpBuilder &, mlir::Location) {
        CIRGenFunction::LexicalScope ls{cgf, begin,
                                        builder.getInsertionBlock()};
        llvm::SmallVector<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>,
                          4>
            privateDeclGuards;
        llvm::SmallVector<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>,
                          4>
            reductionDeclGuards;
        llvm::SmallVector<std::unique_ptr<OpenMPTaskReductionVLASizeScope>, 4>
            reductionVLAScopes;
        if (mlir::failed(remapOpenMPPrivateDecls(cgf, op.getOperation(),
                                                 clauseInfo,
                                                 privateDeclGuards))) {
          res = mlir::failure();
          return;
        }

        if constexpr (std::is_same_v<Op, mlir::omp::TaskOp>)
          remapOpenMPInReductionDecls(cgf, op.getOperation(), clauseInfo,
                                      reductionDeclGuards,
                                      op.getInReductionSyms(),
                                      &reductionVLAScopes);
        if constexpr (std::is_same_v<Op, mlir::omp::TaskOp>)
          emitOpenMPTaskReductionArraySectionRuntimeSizeScopes(
              cgf, op.getOperation(), clauseInfo.inReductionRuntimeSizes,
              op.getInReductionSyms(), reductionVLAScopes);

        if constexpr (std::is_same_v<Op, mlir::omp::ParallelOp>)
          remapOpenMPReductionDecls(cgf, op.getOperation(), clauseInfo,
                                    reductionDeclGuards,
                                    op.getReductionSyms(),
                                    &reductionVLAScopes);
        if constexpr (std::is_same_v<Op, mlir::omp::ParallelOp>)
          emitOpenMPTaskReductionArraySectionRuntimeSizeScopes(
              cgf, op.getOperation(), clauseInfo.reductionRuntimeSizes,
              op.getReductionSyms(), reductionVLAScopes);

        res = bodyEmitter();
      });

  mlir::omp::TerminatorOp::create(builder, end);
  return res;
}

mlir::LogicalResult
CIRGenFunction::emitOMPParallelDirective(const OMPParallelDirective &s) {
  llvm::SmallVector<mlir::Type> retTy;
  llvm::SmallVector<mlir::Value> operands;
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  llvm::SmallVector<OpenMPCopyinMasterAddr, 4> copyinMasterAddrs =
      collectOpenMPCopyinMasterAddresses(*this, s);

  auto parallelOp =
      mlir::omp::ParallelOp::create(builder, begin, retTy, operands);
  OpenMPClauseEmissionInfo clauseInfo = emitOpenMPClauses(parallelOp, s.clauses());
  const CapturedStmt *parallelCaptured =
      s.getCapturedStmt(OpenMPDirectiveKind::OMPD_parallel);
  assert(parallelCaptured && "expected OpenMP parallel captured stmt");
  parallelOp->setAttr("cir.instrprof_func_name",
                      cgm.getStringAttrCached(
                          getOpenMPHelperPGOName(*parallelCaptured)));
  parallelOp->setAttr("cir.instrprof_func_linkage",
                      builder.getI32IntegerAttr(static_cast<int32_t>(
                          llvm::GlobalValue::InternalLinkage)));

  mlir::LogicalResult res = emitOMPPrivateRegionBody(
      *this, parallelOp, clauseInfo, begin, end, [&]() -> mlir::LogicalResult {
        auto pgoScope = enterOpenMPHelperRegionCounters(*parallelCaptured);
        OpenMPTLSCopyinDeclMapScope tlsCopyinDecls(*this, s);
        (void)emitOpenMPCopyinClause(*this, s, copyinMasterAddrs);
        bool hasDisabledLastprivateConditional =
            pushDisabledOpenMPLastprivateConditional(s);
        incrementProfileCounter(parallelCaptured->getCapturedDecl()->getBody());
        mlir::LogicalResult innerRes =
            emitStmt(parallelCaptured->getCapturedStmt(),
                     /*useCurrentScope=*/true);
        if (hasDisabledLastprivateConditional)
          popOpenMPLastprivateConditional();
        return innerRes;
      });
  if (mlir::succeeded(res))
    checkForOpenMPLastprivateConditionalUpdate(*this, s);
  return res;
}

mlir::LogicalResult
CIRGenFunction::emitOMPTaskwaitDirective(const OMPTaskwaitDirective &s) {
  auto taskwaitOp = mlir::omp::TaskwaitOp::create(
      builder, getLoc(s.getSourceRange()),
      /*depend_kinds=*/mlir::ArrayAttr{},
      /*depend_vars=*/mlir::ValueRange{},
      /*depend_sizes=*/mlir::ValueRange{},
      /*nowait=*/false);
  OpenMPClauseEmissionInfo clauseInfo = emitOpenMPClauses(taskwaitOp, s.clauses());
  if (mlir::failed(
          maybeEmitOpenMPRuntimeDependPayload(*this, taskwaitOp, s,
                                              clauseInfo)))
    return mlir::failure();
  return mlir::success();
}
mlir::LogicalResult
CIRGenFunction::emitOMPTaskyieldDirective(const OMPTaskyieldDirective &s) {
  mlir::omp::TaskyieldOp::create(builder, getLoc(s.getSourceRange()));
  return mlir::success();
}
mlir::LogicalResult
CIRGenFunction::emitOMPBarrierDirective(const OMPBarrierDirective &s) {
  mlir::omp::BarrierOp::create(builder, getLoc(s.getBeginLoc()));
  assert(s.clauses().empty() && "omp barrier doesn't support clauses");
  return mlir::success();
}
mlir::LogicalResult
CIRGenFunction::emitOMPMetaDirective(const OMPMetaDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPMetaDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPCanonicalLoop(const OMPCanonicalLoop &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPCanonicalLoop");
  return mlir::failure();
}

mlir::LogicalResult CIRGenFunction::emitSimpleOpenMPSimdDirective(
    const OMPLoopDirective &s,
    ArrayRef<const OMPClause *> clauses, llvm::StringRef directiveName,
    llvm::StringRef clauseContext) {
  auto loopInfo = getOpenMPSimpleGeneratedLoopInfo(s);
  if (!loopInfo) {
    std::string feature = directiveName.str();
    feature += " for non-canonical/non-unit-stride loop";
    getCIRGenModule().errorNYI(s.getSourceRange(), feature);
    return mlir::failure();
  }

  for (const OMPClause *clause : clauses) {
    if (!isSimpleOpenMPSimdClauseKind(clause->getClauseKind())) {
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), clauseContext,
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
    }
  }
  llvm::SmallVector<const OMPClause *, 8> simdClauses;
  for (const OMPClause *clause : clauses)
    if (clause->getClauseKind() != llvm::omp::OMPC_collapse)
      simdClauses.push_back(clause);

  if (mlir::failed(emitOpenMPTransformPreInits(*this, s.getPreInits())))
    return mlir::failure();

  llvm::SmallPtrSet<const VarDecl *, 4> materializedClauseVars;
  materializeOpenMPSimpleSimdClauseExprs(*this, simdClauses,
                                         materializedClauseVars);
  if (auto canonicalInfo = getOpenMPSimpleCanonicalForInfo(s)) {
    materializeOpenMPCapturedClauseExpr(*this, canonicalInfo->lowerBound,
                                        materializedClauseVars);
    materializeOpenMPCapturedClauseExpr(*this, canonicalInfo->upperBound,
                                        materializedClauseVars);
  }

  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  bool preCondConstant = false;
  if (loopInfo->preCond->EvaluateAsBooleanCondition(preCondConstant,
                                                    getContext()) &&
      !preCondConstant)
    return mlir::success();
  llvm::SmallVector<bool, 4> emitFinals;
  emitFinals.reserve(loopInfo->sourceIVs.size());
  for (const VarDecl *sourceIV : loopInfo->sourceIVs) {
    emitFinals.push_back(
        localDeclMap.count(sourceIV) || sourceIV->hasGlobalStorage() ||
        (capturedStmtInfo && capturedStmtInfo->lookup(sourceIV)));
  }

  auto emitSimdLoop = [&]() -> mlir::LogicalResult {
    mlir::LogicalResult res = mlir::success();
    CIRGenBuilderTy &builder = getBuilder();
    OpenMPNontemporalDeclsRAII nontemporalDecls(*this, s);

    bool loopTySigned =
        loopInfo->logicalIV->getType()->hasSignedIntegerRepresentation();
    mlir::Value upperBound =
        emitOpenMPIntegerExpr(*this, loopInfo->lastIteration);
    mlir::Type loopTy = upperBound.getType();
    mlir::Value lowerBound =
        createOpenMPIntegerConstant(*this, begin, loopTy, 0);
    mlir::Value step = createOpenMPIntegerConstant(*this, begin, loopTy, 1);

    mlir::omp::SimdOperands simdClauseOps;
    auto simdOp = mlir::omp::SimdOp::create(builder, begin, simdClauseOps);
    OpenMPClauseEmissionInfo clauseInfo =
        emitOpenMPClauses(simdOp, simdClauses);
    llvm::DenseMap<const VarDecl *, Address> sourceIVOriginalAddrs;
    llvm::DenseMap<const VarDecl *, Address> simdOriginalDecls;
    {
      // Materialize original destination addresses before the simd op so any
      // reference-backed/member-backed lvalues dominate the simd region.
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPoint(simdOp);
      for (auto [idx, sourceIV] : llvm::enumerate(loopInfo->sourceIVs)) {
        if (!emitFinals[idx])
          continue;
        sourceIVOriginalAddrs.try_emplace(
            sourceIV, emitLValue(s.counters()[idx]).getAddress());
      }
      for (const OMPClause *clause : simdClauses) {
        if (clause->getClauseKind() != llvm::omp::OMPC_lastprivate)
          continue;
        const auto *lastprivateClause = cast<OMPLastprivateClause>(clause);
        for (const Expr *varExpr : lastprivateClause->varlist()) {
          const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr);
          if (!vd)
            continue;
          simdOriginalDecls.try_emplace(vd->getCanonicalDecl(),
                                        emitLValue(varExpr).getAddress());
        }
      }
    }

    {
      mlir::OpBuilder::InsertionGuard guard(builder);
      llvm::SmallVector<mlir::Type, 4> simdArgTypes;
      llvm::SmallVector<mlir::Location, 4> simdArgLocs;
      for (mlir::Value privateVar : simdOp.getPrivateVars()) {
        simdArgTypes.push_back(privateVar.getType());
        simdArgLocs.push_back(privateVar.getLoc());
      }
      for (mlir::Value reductionVar : simdOp.getReductionVars()) {
        simdArgTypes.push_back(reductionVar.getType());
        simdArgLocs.push_back(reductionVar.getLoc());
      }
      mlir::Block *simdBlock = builder.createBlock(&simdOp.getRegion(), {},
                                                   simdArgTypes, simdArgLocs);
      builder.setInsertionPointToEnd(simdBlock);
      llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
          privateDeclGuards;
      llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
          reductionDeclGuards;
      auto simdArgIface = mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
          simdOp.getOperation());
      auto privateArgs = simdArgIface.getPrivateBlockArgs();
      assert(clauseInfo.privateIndices.size() == clauseInfo.privateVars.size() &&
             "mismatched OpenMP simd private metadata");
      for (auto [index, vd] :
           llvm::zip_equal(clauseInfo.privateIndices, clauseInfo.privateVars)) {
        assert(index < privateArgs.size() &&
               "unexpected OpenMP simd private block arg index");
        mlir::Value privateArg = privateArgs[index];
        auto ptrTy = mlir::cast<cir::PointerType>(privateArg.getType());
        privateDeclGuards.push_back(
            std::make_unique<DeclMapRevertingRAII>(*this, vd));
        replaceAddrOfLocalVar(
            vd, Address(privateArg, ptrTy.getPointee(),
                        getContext().getDeclAlign(vd)));
      }
      remapOpenMPReductionDecls(*this, simdOp.getOperation(), clauseInfo,
                                reductionDeclGuards);

      mlir::omp::LoopNestOperands loopClauseOps;
      loopClauseOps.loopLowerBounds.push_back(lowerBound);
      loopClauseOps.loopUpperBounds.push_back(upperBound);
      loopClauseOps.loopSteps.push_back(step);
      loopClauseOps.loopInclusive = builder.getUnitAttr();

      auto loopNestOp =
          mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
      loopNestOp->setAttr(
          "omp.frontend.iv_signed",
          builder.getDenseBoolArrayAttr({loopTySigned}));

      mlir::Block *loopBlock = builder.createBlock(
          &loopNestOp.getRegion(), {}, {loopTy},
          {getLoc(loopInfo->logicalIV->getLocation())});
      builder.setInsertionPointToEnd(loopBlock);

      auto createLoopVarStorage = [&](const VarDecl *vd) {
        CharUnits vdAlign = getContext().getDeclAlign(vd);
        mlir::Type vdType = convertTypeForMem(vd->getType());
        Address rawAlloca = Address::invalid();
        createTempAlloca(vdType, vdAlign, getLoc(vd->getLocation()),
                         vd->getName(), /*arraySize=*/nullptr, &rawAlloca);
        return std::pair{rawAlloca, vdType};
      };

      auto [logicalIVRawAlloca, logicalIVType] =
          createLoopVarStorage(loopInfo->logicalIV);
      llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
          sourceIVMapGuards;
      sourceIVMapGuards.reserve(loopInfo->sourceIVs.size());
      for (const VarDecl *sourceIV : loopInfo->sourceIVs) {
        auto [sourceIVRawAlloca, sourceIVType] = createLoopVarStorage(sourceIV);
        (void)sourceIVType;
        sourceIVMapGuards.push_back(
            std::make_unique<DeclMapRevertingRAII>(*this, sourceIV));
        replaceAddrOfLocalVar(sourceIV, sourceIVRawAlloca);
      }

      DeclMapRevertingRAII logicalIVMapGuard(*this, loopInfo->logicalIV);
      replaceAddrOfLocalVar(loopInfo->logicalIV, logicalIVRawAlloca);
      llvm::SmallPtrSet<const VarDecl *, 4> sourceIVSet;
      for (const VarDecl *sourceIV : loopInfo->sourceIVs)
        sourceIVSet.insert(sourceIV->getCanonicalDecl());
      bool hasConditionalLastprivate = pushOpenMPLastprivateConditional(
          simdClauses, logicalIVRawAlloca, s.getBeginLoc());

      mlir::Value logicalIVValue = loopBlock->getArgument(0);
      if (auto ompIVTy =
              mlir::dyn_cast<mlir::IntegerType>(logicalIVValue.getType())) {
        auto cirLoopTy = cir::IntType::get(&getMLIRContext(), ompIVTy.getWidth(),
                                           loopTySigned);
        logicalIVValue = mlir::UnrealizedConversionCastOp::create(
                             builder, getLoc(loopInfo->logicalIV->getLocation()),
                             cirLoopTy, logicalIVValue)
                             .getResult(0);
        if (cirLoopTy != logicalIVType) {
          logicalIVValue =
              builder.createIntCast(logicalIVValue,
                                    mlir::cast<cir::IntType>(logicalIVType));
        }
      } else {
        logicalIVValue = castOpenMPValueToCIRType(
            *this, getLoc(loopInfo->logicalIV->getLocation()),
            logicalIVValue, logicalIVType);
      }

      builder.createStore(getLoc(loopInfo->logicalIV->getLocation()),
                          logicalIVValue, logicalIVRawAlloca);
      for (const Expr *update : loopInfo->updates)
        emitIgnoredExpr(update);

      OpenMPScanParentDirectiveRAII scanDirectiveScope(
          *this, s, collectOpenMPReductionRegionArgs(simdOp));
      res = emitOMPLoopBody(*this, builder, begin, loopInfo->body);
      bool anyEmitFinals =
          llvm::any_of(emitFinals, [](bool emitFinal) { return emitFinal; });
      if (anyEmitFinals || !clauseInfo.lastprivateIndices.empty()) {
        mlir::Location ivLoc = getLoc(loopInfo->logicalIV->getLocation());
        auto cirIVTy = mlir::dyn_cast<cir::IntType>(logicalIVType);
        assert(cirIVTy && "expected CIR integer type for generated simd IV");
        mlir::Value upperBoundValue =
            castOpenMPValueToCIRType(*this, ivLoc, upperBound, logicalIVType);
        mlir::Value stepValue = builder.getConstInt(ivLoc, cirIVTy, 1);
        mlir::Value nextIV = builder.createAdd(ivLoc, logicalIVValue, stepValue);
        mlir::Value isLastIteration =
            builder.createCompare(ivLoc, cir::CmpOpKind::gt, nextIV,
                                  upperBoundValue);

        cir::IfOp::create(
            builder, ivLoc, isLastIteration, /*withElseRegion=*/false,
            [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
              mlir::OpBuilder::InsertionGuard guard(builder);
              builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());

              for (auto [idx, final] : llvm::enumerate(loopInfo->finals)) {
                if (!emitFinals[idx] || !final)
                  continue;
                const VarDecl *sourceIV = loopInfo->sourceIVs[idx];
                auto originalIt = sourceIVOriginalAddrs.find(sourceIV);
                assert(originalIt != sourceIVOriginalAddrs.end() &&
                       "missing simd source IV destination address");
                DeclMapRevertingRAII finalSourceIVMapGuard(*this, sourceIV);
                replaceAddrOfLocalVar(sourceIV, originalIt->second);
                emitIgnoredExpr(final);
              }
              if (!clauseInfo.lastprivateIndices.empty()) {
                assert(clauseInfo.lastprivateIndices.size() ==
                           clauseInfo.lastprivateVars.size() &&
                       "mismatched OpenMP simd lastprivate metadata");
                for (auto [index, vd, srcVD, destVD, assignOp] :
                     llvm::zip_equal(clauseInfo.lastprivateIndices,
                                     clauseInfo.lastprivateVars,
                                     clauseInfo.lastprivateSourceVDs,
                                     clauseInfo.lastprivateDestVDs,
                                     clauseInfo.lastprivateAssignmentOps)) {
                  if (sourceIVSet.contains(vd->getCanonicalDecl()))
                    continue;
                  assert(index < privateArgs.size() &&
                         "unexpected OpenMP simd lastprivate block arg index");
                  Address srcAddr = getOpenMPPrivateStorageAddress(*this, vd);
                  emitOpenMPLastprivateConditionalFinalUpdate(
                      vd, srcAddr, vd->getLocation());
                  auto originalIt =
                      simdOriginalDecls.find(vd->getCanonicalDecl());
                  assert(originalIt != simdOriginalDecls.end() &&
                         "missing simd lastprivate destination address");
                  emitOpenMPLastprivateCopy(*this, vd, srcAddr,
                                            originalIt->second, srcVD, destVD,
                                            assignOp);
                }
              }
              cir::YieldOp::create(thenBuilder, thenLoc);
            });
      }
      if (hasConditionalLastprivate)
        popOpenMPLastprivateConditional();
      reductionDeclGuards.clear();
      mlir::omp::YieldOp::create(builder, end);
    }
    return res;
  };

  if (loopInfo->preCond->EvaluateAsBooleanCondition(preCondConstant,
                                                    getContext()))
    return emitSimdLoop();

  mlir::LogicalResult res = mlir::success();
  mlir::Value preCond = emitOpOnBoolExpr(begin, loopInfo->preCond);
  cir::IfOp::create(
      builder, begin, preCond, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &thenBuilder, mlir::Location) {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
        res = emitSimdLoop();
        builder.createYield(begin);
      });
  return res;
}

mlir::LogicalResult
CIRGenFunction::emitOMPSimdDirective(const OMPSimdDirective &s) {
  return emitSimpleOpenMPSimdDirective(s, s.clauses(),
                                       "OpenMP OMPSimdDirective",
                                       "OpenMP simd clause");
}
mlir::LogicalResult
CIRGenFunction::emitOMPTileDirective(const OMPTileDirective &s) {
  if (mlir::failed(emitOpenMPTransformPreInits(*this, s.getPreInits())))
    return mlir::failure();

  Stmt *transformedStmt = s.getTransformedStmt();
  if (!transformedStmt) {
    getCIRGenModule().errorNYI(s.getSourceRange(),
                               "OpenMP OMPTileDirective dependent transform");
    return mlir::failure();
  }

  return emitStmt(transformedStmt, /*useCurrentScope=*/true);
}
mlir::LogicalResult
CIRGenFunction::emitOMPUnrollDirective(const OMPUnrollDirective &s) {
  // Note: OGCG does not emit pre-inits for unroll directives. The loop
  // variable is declared by the associated ForStmt's init statement, so
  // emitting pre-inits here would cause a duplicate symbol table entry.

  const Stmt *associatedStmt = s.getAssociatedStmt();
  if (!isa_and_nonnull<ForStmt>(associatedStmt)) {
    getCIRGenModule().errorNYI(s.getSourceRange(),
                               "OpenMP OMPUnrollDirective non-ForStmt");
    return mlir::failure();
  }

  setPendingLoopHintAttr(createOpenMPUnrollLoopHint(*this, s));
  return emitStmt(associatedStmt, /*useCurrentScope=*/true);
}
mlir::LogicalResult
CIRGenFunction::emitOMPFuseDirective(const OMPFuseDirective &s) {
  if (mlir::failed(emitOpenMPTransformPreInits(*this, s.getPreInits())))
    return mlir::failure();

  Stmt *transformedStmt = s.getTransformedStmt();
  if (!transformedStmt) {
    getCIRGenModule().errorNYI(s.getSourceRange(),
                               "OpenMP OMPFuseDirective dependent transform");
    return mlir::failure();
  }

  return emitStmt(transformedStmt, /*useCurrentScope=*/true);
}
mlir::LogicalResult
CIRGenFunction::emitSimpleOMPForDirective(
    const OMPLoopDirective &s, ArrayRef<const OMPClause *> clauses,
    llvm::StringRef directiveName, bool hasCancel) {
  auto loopInfo = getOpenMPSimpleCanonicalForInfo(s);
  auto generatedLoopInfo =
      loopInfo ? std::optional<OpenMPSimpleGeneratedLoopInfo>{}
               : getOpenMPSimpleGeneratedLoopInfo(s);
  if (!loopInfo && !generatedLoopInfo) {
    std::string feature = directiveName.str();
    feature += " for non-canonical/non-unit-stride loop";
    getCIRGenModule().errorNYI(s.getSourceRange(), feature);
    return mlir::failure();
  }

  if (hasCancel) {
    std::string feature = directiveName.str();
    feature += " with Cancel";
    getCIRGenModule().errorNYI(s.getBeginLoc(), feature);
    return mlir::failure();
  }
  for (const OMPClause *clause : clauses) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_nowait:
    case llvm::omp::OMPC_order:
    case llvm::omp::OMPC_ordered:
    case llvm::omp::OMPC_private:
    case llvm::omp::OMPC_firstprivate:
    case llvm::omp::OMPC_schedule:
    case llvm::omp::OMPC_lastprivate:
    case llvm::omp::OMPC_reduction:
    case llvm::omp::OMPC_collapse:
      break;
    default:
      std::string feature = directiveName.str();
      feature += " clauses";
      getCIRGenModule().errorNYI(s.getSourceRange(), feature);
      break;
    }
  }

  llvm::SmallVector<const OMPClause *, 8> wsloopClauses;
  for (const OMPClause *clause : clauses)
    if (clause->getClauseKind() != llvm::omp::OMPC_collapse)
      wsloopClauses.push_back(clause);

  mlir::LogicalResult res = mlir::success();
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  if (!loopInfo) {
    assert(generatedLoopInfo &&
           "expected generated loop info for non-canonical omp for");

    if (mlir::failed(emitOpenMPTransformPreInits(*this, s.getPreInits())))
      return mlir::failure();

    bool preCondConstant = false;
    if (generatedLoopInfo->preCond->EvaluateAsBooleanCondition(preCondConstant,
                                                               getContext()) &&
        !preCondConstant)
      return mlir::success();

    llvm::SmallVector<bool, 4> emitFinals;
    emitFinals.reserve(generatedLoopInfo->sourceIVs.size());
    for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs) {
      emitFinals.push_back(localDeclMap.count(sourceIV) ||
                           sourceIV->hasGlobalStorage() ||
                           (capturedStmtInfo &&
                            capturedStmtInfo->lookup(sourceIV)));
    }

    auto emitGeneratedWsloop = [&]() -> mlir::LogicalResult {
      mlir::Value upperBound =
          emitOpenMPIntegerExpr(*this, generatedLoopInfo->lastIteration);
      mlir::Type loopTy = upperBound.getType();
      mlir::Value lowerBound =
          createOpenMPIntegerConstant(*this, begin, loopTy, 0);
      mlir::Value step =
          createOpenMPIntegerConstant(*this, begin, loopTy, 1);

      auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
      OpenMPClauseEmissionInfo clauseInfo =
          emitOpenMPClauses(wsloopOp, wsloopClauses,
                            /*enclosingFirstprivateVars=*/{},
                            /*allowWsloopConditionalLastprivate=*/true);

      llvm::DenseMap<const VarDecl *, Address> sourceIVOriginalAddrs;
      {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPoint(wsloopOp);
        for (auto [idx, sourceIV] :
             llvm::enumerate(generatedLoopInfo->sourceIVs)) {
          if (!emitFinals[idx])
            continue;
          sourceIVOriginalAddrs.try_emplace(
              sourceIV, emitLValue(s.counters()[idx]).getAddress());
        }
      }

      {
        mlir::OpBuilder::InsertionGuard guard(builder);
        llvm::DenseMap<const VarDecl *, Address> wsloopOriginalDecls;
        llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
            wsloopPrivateDeclGuards;
        llvm::SmallVector<mlir::Type, 4> wsloopArgTypes;
        llvm::SmallVector<mlir::Location, 4> wsloopArgLocs;
        for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
          wsloopArgTypes.push_back(privateVar.getType());
          wsloopArgLocs.push_back(privateVar.getLoc());
        }
        llvm::SmallVector<mlir::Type, 4> reductionArgTypes;
        llvm::SmallVector<mlir::Location, 4> reductionArgLocs;
        for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
          reductionArgTypes.push_back(reductionVar.getType());
          reductionArgLocs.push_back(reductionVar.getLoc());
        }
        wsloopArgTypes.append(reductionArgTypes);
        wsloopArgLocs.append(reductionArgLocs);
        mlir::Block *wsloopBlock = builder.createBlock(&wsloopOp.getRegion(), {},
                                                       wsloopArgTypes,
                                                       wsloopArgLocs);
        builder.setInsertionPointToEnd(wsloopBlock);
        llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
            reductionDeclGuards;
        llvm::SmallVector<std::unique_ptr<OpenMPTaskReductionVLASizeScope>, 4>
            reductionVLAScopes;
        auto wsloopArgIface = mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
            wsloopOp.getOperation());
        auto wsloopPrivateArgs = wsloopArgIface.getPrivateBlockArgs();
        if (mlir::failed(remapOpenMPPrivateDecls(
                *this, wsloopOp.getOperation(), clauseInfo,
                wsloopPrivateDeclGuards, &wsloopOriginalDecls)))
          return mlir::failure();
        remapOpenMPReductionDecls(*this, wsloopOp.getOperation(), clauseInfo,
                                  reductionDeclGuards,
                                  wsloopOp.getReductionSyms(),
                                  &reductionVLAScopes);
        emitOpenMPTaskReductionArraySectionRuntimeSizeScopes(
            *this, wsloopOp.getOperation(),
            clauseInfo.reductionRuntimeSizes,
            wsloopOp.getReductionSyms(), reductionVLAScopes);

        mlir::omp::LoopNestOperands loopClauseOps;
        loopClauseOps.loopLowerBounds.push_back(lowerBound);
        loopClauseOps.loopUpperBounds.push_back(upperBound);
        loopClauseOps.loopSteps.push_back(step);
        loopClauseOps.loopInclusive = builder.getUnitAttr();

        auto loopNestOp =
            mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
        bool logicalIVSigned =
            generatedLoopInfo->logicalIV->getType()->hasSignedIntegerRepresentation();
        loopNestOp->setAttr("omp.frontend.iv_signed",
                            builder.getDenseBoolArrayAttr({logicalIVSigned}));
        mlir::Block *loopBlock = builder.createBlock(
            &loopNestOp.getRegion(), {}, {loopTy},
            {getLoc(generatedLoopInfo->logicalIV->getLocation())});
        builder.setInsertionPointToEnd(loopBlock);

        auto createLoopVarStorage = [&](const VarDecl *vd) {
          CharUnits vdAlign = getContext().getDeclAlign(vd);
          mlir::Type vdType = convertTypeForMem(vd->getType());
          Address rawAlloca = Address::invalid();
          createTempAlloca(vdType, vdAlign, getLoc(vd->getLocation()),
                           vd->getName(), /*arraySize=*/nullptr, &rawAlloca);
          return std::pair{rawAlloca, vdType};
        };

        auto [logicalIVRawAlloca, logicalIVType] =
            createLoopVarStorage(generatedLoopInfo->logicalIV);
        llvm::SmallVector<Address, 4> sourceIVRawAllocas;
        sourceIVRawAllocas.reserve(generatedLoopInfo->sourceIVs.size());
        for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs)
          sourceIVRawAllocas.push_back(createLoopVarStorage(sourceIV).first);

        mlir::LogicalResult bodyRes = mlir::success();
        {
          llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
              sourceIVMapGuards;
          sourceIVMapGuards.reserve(generatedLoopInfo->sourceIVs.size());
          for (auto [sourceIV, sourceIVRawAlloca] :
               llvm::zip_equal(generatedLoopInfo->sourceIVs,
                               sourceIVRawAllocas)) {
            sourceIVMapGuards.push_back(
                std::make_unique<DeclMapRevertingRAII>(*this, sourceIV));
            replaceAddrOfLocalVar(sourceIV, sourceIVRawAlloca);
          }
          DeclMapRevertingRAII logicalIVMapGuard(*this,
                                                 generatedLoopInfo->logicalIV);
          replaceAddrOfLocalVar(generatedLoopInfo->logicalIV,
                                logicalIVRawAlloca);
          llvm::SmallPtrSet<const VarDecl *, 4> sourceIVSet;
          for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs)
            sourceIVSet.insert(sourceIV->getCanonicalDecl());

          mlir::Value logicalIVValue = loopBlock->getArgument(0);
          if (auto ompIVTy =
                  mlir::dyn_cast<mlir::IntegerType>(logicalIVValue.getType())) {
            auto cirLoopTy = cir::IntType::get(&getMLIRContext(),
                                               ompIVTy.getWidth(),
                                               logicalIVSigned);
            logicalIVValue = mlir::UnrealizedConversionCastOp::create(
                                 builder,
                                 getLoc(generatedLoopInfo->logicalIV
                                            ->getLocation()),
                                 cirLoopTy, logicalIVValue)
                                 .getResult(0);
            if (cirLoopTy != logicalIVType) {
              logicalIVValue = builder.createIntCast(
                  logicalIVValue, mlir::cast<cir::IntType>(logicalIVType));
            }
          } else {
            logicalIVValue = castOpenMPValueToCIRType(
                *this, getLoc(generatedLoopInfo->logicalIV->getLocation()),
                logicalIVValue, logicalIVType);
          }
          builder.createStore(getLoc(generatedLoopInfo->logicalIV->getLocation()),
                              logicalIVValue, logicalIVRawAlloca);
          for (const Expr *update : generatedLoopInfo->updates)
            emitIgnoredExpr(update);

          bool hasConditionalLastprivate = pushOpenMPLastprivateConditional(
              clauses, logicalIVRawAlloca, s.getBeginLoc());
          OpenMPScanParentDirectiveRAII scanDirectiveScope(
              *this, s, collectOpenMPReductionRegionArgs(wsloopOp));
          bodyRes = emitOMPLoopBody(*this, builder, begin,
                                    generatedLoopInfo->body);
          bool anyEmitFinals =
              llvm::any_of(emitFinals,
                           [](bool emitFinal) { return emitFinal; });
          if (anyEmitFinals || !clauseInfo.lastprivateIndices.empty()) {
            assert(clauseInfo.lastprivateIndices.size() ==
                       clauseInfo.lastprivateVars.size() &&
                   "mismatched OpenMP wsloop lastprivate metadata");
            assert(clauseInfo.lastprivateIndices.size() ==
                       clauseInfo.lastprivateConditionals.size() &&
                   "mismatched OpenMP wsloop conditional-lastprivate "
                   "metadata");
            mlir::Location ivLoc =
                getLoc(generatedLoopInfo->logicalIV->getLocation());
            auto cirIVTy = mlir::dyn_cast<cir::IntType>(logicalIVType);
            assert(cirIVTy &&
                   "expected CIR integer type for generated logical IV");
            mlir::Value upperBoundValue = castOpenMPValueToCIRType(
                *this, ivLoc, upperBound, logicalIVType);
            mlir::Value stepValue = builder.getConstInt(ivLoc, cirIVTy, 1);
            mlir::Value nextIV =
                builder.createAdd(ivLoc, logicalIVValue, stepValue);
            mlir::Value isLastIteration = builder.createCompare(
                ivLoc, cir::CmpOpKind::gt, nextIV, upperBoundValue);

            cir::IfOp::create(
                builder, ivLoc, isLastIteration, /*withElseRegion=*/false,
                [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
                  mlir::OpBuilder::InsertionGuard guard(builder);
                  builder.setInsertionPointToEnd(
                      thenBuilder.getInsertionBlock());
                  for (auto [idx, final] :
                       llvm::enumerate(generatedLoopInfo->finals)) {
                    if (!emitFinals[idx] || !final)
                      continue;
                    const VarDecl *sourceIV = generatedLoopInfo->sourceIVs[idx];
                    auto originalIt = sourceIVOriginalAddrs.find(sourceIV);
                    assert(originalIt != sourceIVOriginalAddrs.end() &&
                           "missing source IV destination address");
                    DeclMapRevertingRAII finalSourceIVMapGuard(*this, sourceIV);
                    replaceAddrOfLocalVar(sourceIV, originalIt->second);
                    emitIgnoredExpr(final);
                  }
                  for (auto [index, vd, srcVD, destVD, assignOp, isConditional] :
                       llvm::zip_equal(clauseInfo.lastprivateIndices,
                                       clauseInfo.lastprivateVars,
                                       clauseInfo.lastprivateSourceVDs,
                                       clauseInfo.lastprivateDestVDs,
                                       clauseInfo.lastprivateAssignmentOps,
                                       clauseInfo.lastprivateConditionals)) {
                    if (sourceIVSet.contains(vd->getCanonicalDecl()))
                      continue;
                    assert(index < wsloopPrivateArgs.size() &&
                           "unexpected OpenMP wsloop lastprivate block arg index");
                    Address srcAddr = getOpenMPPrivateStorageAddress(*this, vd);
                    if (isConditional)
                      emitOpenMPLastprivateConditionalFinalUpdate(
                          vd, srcAddr, vd->getLocation());
                    auto originalIt =
                        wsloopOriginalDecls.find(vd->getCanonicalDecl());
                    assert(originalIt != wsloopOriginalDecls.end() &&
                           "missing lastprivate destination address");
                    emitOpenMPLastprivateCopy(*this, vd, srcAddr,
                                              originalIt->second, srcVD, destVD,
                                              assignOp);
                  }
                  cir::YieldOp::create(thenBuilder, thenLoc);
                });
          }
          if (hasConditionalLastprivate)
            popOpenMPLastprivateConditional();
        }
        wsloopPrivateDeclGuards.clear();
        reductionDeclGuards.clear();
        mlir::omp::YieldOp::create(builder, end);
        return bodyRes;
      }
    };

    if (generatedLoopInfo->preCond->EvaluateAsBooleanCondition(preCondConstant,
                                                               getContext())) {
      res = emitGeneratedWsloop();
      return res;
    }

    mlir::Value preCond = emitOpOnBoolExpr(begin, generatedLoopInfo->preCond);
    cir::IfOp::create(
        builder, begin, preCond, /*withElseRegion=*/false,
        [&](mlir::OpBuilder &thenBuilder, mlir::Location) {
          mlir::OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
          res = emitGeneratedWsloop();
          builder.createYield(begin);
        });
    return res;
  }

  QualType ivValueQualType = loopInfo->iv->getType().getNonReferenceType();
  bool ivSigned = ivValueQualType->hasSignedIntegerRepresentation();
  CharUnits ivDeclAlign = getContext().getDeclAlign(loopInfo->iv);
  CharUnits ivValueAlign = getContext().getTypeAlignInChars(ivValueQualType);
  mlir::Type ivDeclType = convertTypeForMem(loopInfo->iv->getType());
  mlir::Type ivValueType = convertTypeForMem(ivValueQualType);
  Address ivDeclRawAlloca = Address::invalid();
  createTempAlloca(ivDeclType, ivDeclAlign, getLoc(loopInfo->iv->getLocation()),
                   loopInfo->iv->getName(), /*arraySize=*/nullptr,
                   &ivDeclRawAlloca);
  Address ivValueRawAlloca = ivDeclRawAlloca;
  if (loopInfo->iv->getType()->isReferenceType()) {
    createTempAlloca(ivValueType, ivValueAlign, getLoc(loopInfo->iv->getLocation()),
                     (loopInfo->iv->getName() + ".priv").str(),
                     /*arraySize=*/nullptr, &ivValueRawAlloca);
    builder.createStore(getLoc(loopInfo->iv->getLocation()),
                        ivValueRawAlloca.getPointer(), ivDeclRawAlloca);
  }
  if (!localDeclMap.count(loopInfo->iv))
    declare(ivDeclRawAlloca.getPointer(), loopInfo->iv,
            loopInfo->iv->getType(), getLoc(loopInfo->iv->getSourceRange()),
            ivDeclAlign);

  mlir::Value lowerBound = emitOpenMPIntegerExpr(*this, loopInfo->lowerBound);
  mlir::Value upperBound = emitOpenMPIntegerExpr(*this, loopInfo->upperBound);
  std::optional<mlir::IntegerType> loopTy =
      getOpenMPWorksharingLoopType(*this, lowerBound, upperBound);
  if (!loopTy) {
    std::string feature = directiveName.str();
    feature += " with unsupported loop bound types";
    getCIRGenModule().errorNYI(s.getSourceRange(), feature);
    return mlir::failure();
  }
  lowerBound =
      castOpenMPValueToBuiltinInt(*this, begin, lowerBound, *loopTy, ivSigned);
  upperBound =
      castOpenMPValueToBuiltinInt(*this, begin, upperBound, *loopTy, ivSigned);
  mlir::Value step = createOpenMPIntegerConstant(*this, begin, *loopTy, 1);

  auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
  OpenMPClauseEmissionInfo clauseInfo =
      emitOpenMPClauses(wsloopOp, wsloopClauses,
                        /*enclosingFirstprivateVars=*/{},
                        /*allowWsloopConditionalLastprivate=*/true);

  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    llvm::DenseMap<const VarDecl *, Address> wsloopOriginalDecls;
    llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
        wsloopPrivateDeclGuards;
    llvm::SmallVector<mlir::Type, 4> wsloopPrivateArgTypes;
    llvm::SmallVector<mlir::Location, 4> wsloopPrivateArgLocs;
    for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
      wsloopPrivateArgTypes.push_back(privateVar.getType());
      wsloopPrivateArgLocs.push_back(privateVar.getLoc());
    }
    llvm::SmallVector<mlir::Type, 4> reductionArgTypes;
    llvm::SmallVector<mlir::Location, 4> reductionArgLocs;
    for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
      reductionArgTypes.push_back(reductionVar.getType());
      reductionArgLocs.push_back(reductionVar.getLoc());
    }
    wsloopPrivateArgTypes.append(reductionArgTypes);
    wsloopPrivateArgLocs.append(reductionArgLocs);
    mlir::Block *wsloopBlock = builder.createBlock(&wsloopOp.getRegion(), {},
                                                   wsloopPrivateArgTypes,
                                                   wsloopPrivateArgLocs);
    builder.setInsertionPointToEnd(wsloopBlock);
    llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
        reductionDeclGuards;
    llvm::SmallVector<std::unique_ptr<OpenMPTaskReductionVLASizeScope>, 4>
        reductionVLAScopes;
    auto wsloopPrivateArgs =
        mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(wsloopOp.getOperation())
            .getPrivateBlockArgs();
    if (mlir::failed(remapOpenMPPrivateDecls(
            *this, wsloopOp.getOperation(), clauseInfo,
            wsloopPrivateDeclGuards, &wsloopOriginalDecls)))
      return mlir::failure();
    remapOpenMPReductionDecls(*this, wsloopOp.getOperation(), clauseInfo,
                              reductionDeclGuards,
                              wsloopOp.getReductionSyms(),
                              &reductionVLAScopes);
    emitOpenMPTaskReductionArraySectionRuntimeSizeScopes(
        *this, wsloopOp.getOperation(),
        clauseInfo.reductionRuntimeSizes,
        wsloopOp.getReductionSyms(), reductionVLAScopes);

    mlir::omp::LoopNestOperands loopClauseOps;
    loopClauseOps.loopLowerBounds.push_back(lowerBound);
    loopClauseOps.loopUpperBounds.push_back(upperBound);
    loopClauseOps.loopSteps.push_back(step);
    if (loopInfo->upperInclusive)
      loopClauseOps.loopInclusive = builder.getUnitAttr();

    auto loopNestOp =
        mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
    loopNestOp->setAttr(
        "omp.frontend.iv_signed",
        builder.getDenseBoolArrayAttr(
            {ivSigned}));

    mlir::Block *loopBlock = builder.createBlock(
        &loopNestOp.getRegion(), {}, {*loopTy},
        {getLoc(loopInfo->iv->getLocation())});
    builder.setInsertionPointToEnd(loopBlock);

    DeclMapRevertingRAII ivMapGuard(*this, loopInfo->iv);
    replaceAddrOfLocalVar(loopInfo->iv, ivDeclRawAlloca);
    auto logicalIVType =
        cir::IntType::get(&getMLIRContext(), loopTy->getWidth(), ivSigned);
    mlir::Value logicalIVValue = castOpenMPValueToCIRType(
        *this, getLoc(loopInfo->iv->getLocation()), loopBlock->getArgument(0),
        logicalIVType);
    mlir::Value ivValue = logicalIVValue;
    if (logicalIVType != ivValueType)
      ivValue = builder.createIntCast(ivValue, mlir::cast<cir::IntType>(ivValueType));
    builder.createStore(getLoc(loopInfo->iv->getLocation()), ivValue,
                        ivValueRawAlloca);

    OpenMPScanParentDirectiveRAII scanDirectiveScope(
        *this, s, collectOpenMPReductionRegionArgs(wsloopOp));
    bool hasConditionalLastprivate =
        pushOpenMPLastprivateConditional(clauses, ivValueRawAlloca,
                                         s.getBeginLoc());
    res = emitOMPLoopBody(*this, builder, begin, loopInfo->forStmt->getBody());
    if (!clauseInfo.lastprivateIndices.empty()) {
      assert(clauseInfo.lastprivateIndices.size() ==
                 clauseInfo.lastprivateVars.size() &&
             "mismatched OpenMP wsloop lastprivate metadata");
      assert(clauseInfo.lastprivateIndices.size() ==
                 clauseInfo.lastprivateConditionals.size() &&
             "mismatched OpenMP wsloop conditional-lastprivate metadata");
      mlir::Location ivLoc = getLoc(loopInfo->iv->getLocation());
      auto cirIVTy = mlir::cast<cir::IntType>(logicalIVType);
      mlir::Value upperBoundValue =
          castOpenMPValueToCIRType(*this, ivLoc, upperBound, logicalIVType);
      mlir::Value stepValue = builder.getConstInt(ivLoc, cirIVTy, 1);
      mlir::Value nextIV = builder.createAdd(ivLoc, logicalIVValue, stepValue);
      mlir::Value isLastIteration = builder.createCompare(
          ivLoc, loopInfo->upperInclusive ? cir::CmpOpKind::gt
                                          : cir::CmpOpKind::ge,
          nextIV, upperBoundValue);

      cir::IfOp::create(
          builder, ivLoc, isLastIteration, /*withElseRegion=*/false,
          [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
            for (auto [index, vd, srcVD, destVD, assignOp, isConditional] :
                 llvm::zip_equal(clauseInfo.lastprivateIndices,
                                 clauseInfo.lastprivateVars,
                                 clauseInfo.lastprivateSourceVDs,
                                 clauseInfo.lastprivateDestVDs,
                                 clauseInfo.lastprivateAssignmentOps,
                                 clauseInfo.lastprivateConditionals)) {
              assert(index < wsloopPrivateArgs.size() &&
                     "unexpected OpenMP wsloop lastprivate block arg index");
              Address srcAddr = getOpenMPPrivateStorageAddress(*this, vd);
              if (isConditional)
                emitOpenMPLastprivateConditionalFinalUpdate(
                    vd, srcAddr, vd->getLocation());
              auto originalIt = wsloopOriginalDecls.find(vd->getCanonicalDecl());
              assert(originalIt != wsloopOriginalDecls.end() &&
                     "missing lastprivate destination address");
              emitOpenMPLastprivateCopy(*this, vd, srcAddr, originalIt->second,
                                        srcVD, destVD, assignOp);
            }
            cir::YieldOp::create(thenBuilder, thenLoc);
          });
    }
    if (hasConditionalLastprivate)
      popOpenMPLastprivateConditional();
    wsloopPrivateDeclGuards.clear();
    reductionDeclGuards.clear();
    mlir::omp::YieldOp::create(builder, end);
  }

  return res;
}
mlir::LogicalResult
CIRGenFunction::emitOMPForDirective(const OMPForDirective &s) {
  return emitSimpleOMPForDirective(s, s.clauses(), "OpenMP OMPForDirective",
                                   s.hasCancel());
}
mlir::LogicalResult
CIRGenFunction::emitOMPForSimdDirective(const OMPForSimdDirective &s) {
  auto loopInfo = getOpenMPSimpleCanonicalForInfo(s);
  auto generatedLoopInfo =
      loopInfo ? std::optional<OpenMPSimpleGeneratedLoopInfo>{}
               : getOpenMPSimpleGeneratedLoopInfo(s);
  auto collapsedGeneratedLoopInfo =
      (loopInfo || generatedLoopInfo)
          ? std::optional<OpenMPCollapsedGeneratedSimdInfo>{}
          : getOpenMPCollapsedGeneratedSimdInfo(s);
  if (!loopInfo && !generatedLoopInfo && !collapsedGeneratedLoopInfo) {
    getCIRGenModule().errorNYI(
        s.getSourceRange(),
        "OpenMP OMPForSimdDirective for non-canonical/non-unit-stride loop");
    return mlir::failure();
  }

  bool nowait = false;
  llvm::SmallVector<const OMPClause *, 4> wsloopClauses;
  llvm::SmallVector<const OMPClause *, 8> simdClauses;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_nowait:
      nowait = true;
      break;
    case llvm::omp::OMPC_ordered:
    case llvm::omp::OMPC_schedule:
    case llvm::omp::OMPC_private:
    case llvm::omp::OMPC_lastprivate:
    case llvm::omp::OMPC_reduction:
    case llvm::omp::OMPC_linear:
      wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_if:
    case llvm::omp::OMPC_aligned:
    case llvm::omp::OMPC_nontemporal:
    case llvm::omp::OMPC_order:
    case llvm::omp::OMPC_safelen:
    case llvm::omp::OMPC_simdlen:
      simdClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_collapse:
      break;
    default:
      getCIRGenModule().errorNYI(s.getSourceRange(),
                                 "OpenMP OMPForSimdDirective clauses");
      break;
    }
  }

  mlir::LogicalResult res = mlir::success();
  OpenMPNontemporalDeclsRAII nontemporalDecls(*this, s);
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  if (!loopInfo) {
    assert((generatedLoopInfo || collapsedGeneratedLoopInfo) &&
           "expected generated simd loop info for non-canonical omp for simd");

    const Stmt *generatedBody = generatedLoopInfo ? generatedLoopInfo->body
                                                  : collapsedGeneratedLoopInfo
                                                        ->body;
    const VarDecl *generatedLogicalIV =
        generatedLoopInfo ? generatedLoopInfo->logicalIV
                          : collapsedGeneratedLoopInfo->logicalIV;
    const Expr *generatedPreCond =
        generatedLoopInfo ? generatedLoopInfo->preCond
                          : collapsedGeneratedLoopInfo->preCond;
    const Expr *generatedLastIteration =
        generatedLoopInfo ? generatedLoopInfo->lastIteration
                          : collapsedGeneratedLoopInfo->lastIteration;
    llvm::SmallVector<const VarDecl *, 4> generatedSourceIVs;
    llvm::SmallVector<const Expr *, 4> generatedUpdates;
    llvm::SmallVector<const Expr *, 4> generatedFinals;
    if (generatedLoopInfo) {
      llvm::append_range(generatedSourceIVs, generatedLoopInfo->sourceIVs);
      llvm::append_range(generatedUpdates, generatedLoopInfo->updates);
      llvm::append_range(generatedFinals, generatedLoopInfo->finals);
    } else {
      llvm::append_range(generatedSourceIVs,
                         collapsedGeneratedLoopInfo->sourceIVs);
      llvm::append_range(generatedUpdates, collapsedGeneratedLoopInfo->updates);
      llvm::append_range(generatedFinals, collapsedGeneratedLoopInfo->finals);
    }

    if (mlir::failed(emitOpenMPTransformPreInits(*this, s.getPreInits())))
      return mlir::failure();

    bool preCondConstant = false;
    if (generatedPreCond->EvaluateAsBooleanCondition(preCondConstant,
                                                     getContext()) &&
        !preCondConstant)
      return mlir::success();

    llvm::SmallVector<bool, 4> emitFinals;
    emitFinals.reserve(generatedSourceIVs.size());
    for (const VarDecl *sourceIV : generatedSourceIVs) {
      emitFinals.push_back(
          localDeclMap.count(sourceIV) || sourceIV->hasGlobalStorage() ||
          (capturedStmtInfo && capturedStmtInfo->lookup(sourceIV)));
    }

    auto emitGeneratedWsloop = [&]() -> mlir::LogicalResult {
      mlir::Value upperBound =
          emitOpenMPIntegerExpr(*this, generatedLastIteration);
      mlir::Type loopTy = upperBound.getType();
      mlir::Value lowerBound =
          createOpenMPIntegerConstant(*this, begin, loopTy, 0);
      mlir::Value step =
          createOpenMPIntegerConstant(*this, begin, loopTy, 1);

      auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
      wsloopOp.setNowait(nowait);
      wsloopOp.setComposite(true);
      OpenMPClauseEmissionInfo wsloopClauseInfo =
          emitOpenMPClauses(wsloopOp, wsloopClauses,
                            /*enclosingFirstprivateVars=*/{},
                            /*allowWsloopConditionalLastprivate=*/true);

      {
        mlir::OpBuilder::InsertionGuard guard(builder);
        llvm::SmallVector<std::pair<const VarDecl *, Address>, 4> savedDecls;
        llvm::DenseMap<const VarDecl *, Address> wsloopOriginalDecls;
        llvm::SmallVector<mlir::Type, 4> wsloopArgTypes;
        llvm::SmallVector<mlir::Location, 4> wsloopArgLocs;
        for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
          wsloopArgTypes.push_back(privateVar.getType());
          wsloopArgLocs.push_back(privateVar.getLoc());
        }
        llvm::SmallVector<mlir::Type, 4> reductionArgTypes;
        llvm::SmallVector<mlir::Location, 4> reductionArgLocs;
        for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
          reductionArgTypes.push_back(reductionVar.getType());
          reductionArgLocs.push_back(reductionVar.getLoc());
        }
        wsloopArgTypes.append(reductionArgTypes);
        wsloopArgLocs.append(reductionArgLocs);
        mlir::Block *wsloopBlock = builder.createBlock(&wsloopOp.getRegion(), {},
                                                       wsloopArgTypes,
                                                       wsloopArgLocs);
        builder.setInsertionPointToEnd(wsloopBlock);
        llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
            reductionDeclGuards;
        auto wsloopArgIface = mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
            wsloopOp.getOperation());
        auto privateArgs = wsloopArgIface.getPrivateBlockArgs();
        assert(wsloopClauseInfo.privateIndices.size() ==
                   wsloopClauseInfo.privateVars.size() &&
               "mismatched OpenMP wsloop private metadata");
        for (auto [index, vd] : llvm::zip_equal(wsloopClauseInfo.privateIndices,
                                                wsloopClauseInfo.privateVars)) {
          assert(index < privateArgs.size() &&
                 "unexpected OpenMP wsloop private block arg index");
          mlir::Value privateArg = privateArgs[index];
          auto ptrTy = mlir::cast<cir::PointerType>(privateArg.getType());
          if (auto it = localDeclMap.find(vd); it != localDeclMap.end()) {
            savedDecls.emplace_back(vd, it->second);
            wsloopOriginalDecls.insert({vd->getCanonicalDecl(), it->second});
          }
          replaceAddrOfLocalVar(
              vd, Address(privateArg, ptrTy.getPointee(),
                          getContext().getDeclAlign(vd)));
        }
        remapOpenMPReductionDecls(*this, wsloopOp.getOperation(),
                                  wsloopClauseInfo, reductionDeclGuards);

        mlir::omp::SimdOperands simdClauseOps;
        auto simdOp = mlir::omp::SimdOp::create(builder, begin, simdClauseOps);
        simdOp.setComposite(true);
        {
          mlir::OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPoint(wsloopOp->getBlock(),
                                    mlir::Block::iterator(wsloopOp));
          if (mlir::failed(
                  emitOpenMPSimdClausesForComposite(*this, simdOp,
                                                    simdClauses)))
            return mlir::failure();
        }

        mlir::Block *simdBlock = builder.createBlock(&simdOp.getRegion());
        builder.setInsertionPointToEnd(simdBlock);

        mlir::omp::LoopNestOperands loopClauseOps;
        loopClauseOps.loopLowerBounds.push_back(lowerBound);
        loopClauseOps.loopUpperBounds.push_back(upperBound);
        loopClauseOps.loopSteps.push_back(step);
        loopClauseOps.loopInclusive = builder.getUnitAttr();

        auto loopNestOp =
            mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
        bool logicalIVSigned =
            generatedLogicalIV->getType()->hasSignedIntegerRepresentation();
        loopNestOp->setAttr("omp.frontend.iv_signed",
                            builder.getDenseBoolArrayAttr({logicalIVSigned}));
        mlir::Block *loopBlock = builder.createBlock(
            &loopNestOp.getRegion(), {}, {loopTy},
            {getLoc(generatedLogicalIV->getLocation())});
        builder.setInsertionPointToEnd(loopBlock);

        auto createLoopVarStorage = [&](const VarDecl *vd) {
          CharUnits vdAlign = getContext().getDeclAlign(vd);
          mlir::Type vdType = convertTypeForMem(vd->getType());
          Address rawAlloca = Address::invalid();
          createTempAlloca(vdType, vdAlign, getLoc(vd->getLocation()),
                           vd->getName(), /*arraySize=*/nullptr, &rawAlloca);
          return std::pair{rawAlloca, vdType};
        };

        auto [logicalIVRawAlloca, logicalIVType] =
            createLoopVarStorage(generatedLogicalIV);
        llvm::SmallVector<Address, 4> sourceIVRawAllocas;
        sourceIVRawAllocas.reserve(generatedSourceIVs.size());
        for (const VarDecl *sourceIV : generatedSourceIVs)
          sourceIVRawAllocas.push_back(createLoopVarStorage(sourceIV).first);

        mlir::Value logicalIVValue;
        bool hasConditionalLastprivate = false;
        mlir::LogicalResult bodyRes = mlir::success();
        {
          llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
              sourceIVMapGuards;
          sourceIVMapGuards.reserve(generatedSourceIVs.size());
          for (auto [sourceIV, sourceIVRawAlloca] :
               llvm::zip_equal(generatedSourceIVs, sourceIVRawAllocas)) {
            sourceIVMapGuards.push_back(
                std::make_unique<DeclMapRevertingRAII>(*this, sourceIV));
            replaceAddrOfLocalVar(sourceIV, sourceIVRawAlloca);
          }

          DeclMapRevertingRAII logicalIVMapGuard(*this, generatedLogicalIV);
          replaceAddrOfLocalVar(generatedLogicalIV, logicalIVRawAlloca);

          logicalIVValue = loopBlock->getArgument(0);
          if (auto ompIVTy =
                  mlir::dyn_cast<mlir::IntegerType>(logicalIVValue.getType())) {
            auto cirLoopTy = cir::IntType::get(&getMLIRContext(),
                                               ompIVTy.getWidth(),
                                               logicalIVSigned);
            logicalIVValue = mlir::UnrealizedConversionCastOp::create(
                                 builder,
                                 getLoc(generatedLogicalIV->getLocation()),
                                 cirLoopTy, logicalIVValue)
                                 .getResult(0);
            if (cirLoopTy != logicalIVType) {
              logicalIVValue = builder.createIntCast(
                  logicalIVValue, mlir::cast<cir::IntType>(logicalIVType));
            }
          } else {
            logicalIVValue = castOpenMPValueToCIRType(
                *this, getLoc(generatedLogicalIV->getLocation()),
                logicalIVValue, logicalIVType);
          }

          builder.createStore(getLoc(generatedLogicalIV->getLocation()),
                              logicalIVValue, logicalIVRawAlloca);
          for (const Expr *update : generatedUpdates)
            emitIgnoredExpr(update);

          OpenMPScanParentDirectiveRAII scanDirectiveScope(
              *this, s, collectOpenMPReductionRegionArgs(wsloopOp));
          hasConditionalLastprivate = pushOpenMPLastprivateConditional(
              wsloopClauses, logicalIVRawAlloca, s.getBeginLoc());
          bodyRes = emitOMPLoopBody(*this, builder, begin, generatedBody);
        }
        if (!wsloopClauseInfo.lastprivateIndices.empty()) {
          assert(wsloopClauseInfo.lastprivateIndices.size() ==
                     wsloopClauseInfo.lastprivateVars.size() &&
                 "mismatched OpenMP wsloop lastprivate metadata");
          assert(wsloopClauseInfo.lastprivateIndices.size() ==
                     wsloopClauseInfo.lastprivateConditionals.size() &&
                 "mismatched OpenMP wsloop conditional-lastprivate metadata");
          mlir::Location ivLoc = getLoc(generatedLogicalIV->getLocation());
          auto cirIVTy = mlir::dyn_cast<cir::IntType>(logicalIVType);
          assert(cirIVTy &&
                 "expected CIR integer type for generated logical IV");
          mlir::Value upperBoundValue =
              castOpenMPValueToCIRType(*this, ivLoc, upperBound, logicalIVType);
          mlir::Value stepValue = builder.getConstInt(ivLoc, cirIVTy, 1);
          mlir::Value nextIV =
              builder.createAdd(ivLoc, logicalIVValue, stepValue);
          mlir::Value isLastIteration = builder.createCompare(
              ivLoc, cir::CmpOpKind::gt, nextIV, upperBoundValue);

          cir::IfOp::create(
              builder, ivLoc, isLastIteration, /*withElseRegion=*/false,
              [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
                mlir::OpBuilder::InsertionGuard guard(builder);
                builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
                for (auto [index, vd, srcVD, destVD, assignOp, isConditional] :
                     llvm::zip_equal(
                         wsloopClauseInfo.lastprivateIndices,
                         wsloopClauseInfo.lastprivateVars,
                         wsloopClauseInfo.lastprivateSourceVDs,
                         wsloopClauseInfo.lastprivateDestVDs,
                         wsloopClauseInfo.lastprivateAssignmentOps,
                         wsloopClauseInfo.lastprivateConditionals)) {
                  assert(index < privateArgs.size() &&
                         "unexpected OpenMP wsloop lastprivate block arg "
                         "index");
                  Address srcAddr = getOpenMPPrivateStorageAddress(*this, vd);
                  if (isConditional)
                    emitOpenMPLastprivateConditionalFinalUpdate(
                        vd, srcAddr, vd->getLocation());
                  auto originalIt =
                      wsloopOriginalDecls.find(vd->getCanonicalDecl());
                  assert(originalIt != wsloopOriginalDecls.end() &&
                         "missing lastprivate destination address");
                  emitOpenMPLastprivateCopy(*this, vd, srcAddr,
                                            originalIt->second, srcVD, destVD,
                                            assignOp);
                }
                cir::YieldOp::create(thenBuilder, thenLoc);
              });
        }
        if (hasConditionalLastprivate)
          popOpenMPLastprivateConditional();
        reductionDeclGuards.clear();
        mlir::omp::YieldOp::create(builder, end);
        builder.setInsertionPointAfter(wsloopOp);
        for (auto [vd, addr] : llvm::reverse(savedDecls))
          replaceAddrOfLocalVar(vd, addr);

        for (auto [idx, final] : llvm::enumerate(generatedFinals))
          if (emitFinals[idx] && final)
            emitIgnoredExpr(final);

        return bodyRes;
      }
    };

    if (generatedPreCond->EvaluateAsBooleanCondition(preCondConstant,
                                                     getContext()))
      return emitGeneratedWsloop();

    mlir::LogicalResult res = mlir::success();
    mlir::Value preCond = emitOpOnBoolExpr(begin, generatedPreCond);
    cir::IfOp::create(
        builder, begin, preCond, /*withElseRegion=*/false,
        [&](mlir::OpBuilder &thenBuilder, mlir::Location) {
          mlir::OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
          res = emitGeneratedWsloop();
          builder.createYield(begin);
        });
    return res;
  }

  CharUnits ivAlign = getContext().getDeclAlign(loopInfo->iv);
  mlir::Type ivType = convertTypeForMem(loopInfo->iv->getType());
  Address ivSeedRawAlloca = Address::invalid();
  createTempAlloca(ivType, ivAlign, getLoc(loopInfo->iv->getLocation()),
                   loopInfo->iv->getName(), /*arraySize=*/nullptr,
                   &ivSeedRawAlloca);
  if (!localDeclMap.count(loopInfo->iv))
    declare(ivSeedRawAlloca.getPointer(), loopInfo->iv,
            loopInfo->iv->getType(), getLoc(loopInfo->iv->getSourceRange()),
            ivAlign);

  mlir::Value lowerBound = emitOpenMPIntegerExpr(*this, loopInfo->lowerBound);
  mlir::Value upperBound = emitOpenMPIntegerExpr(*this, loopInfo->upperBound);
  mlir::Type loopTy = lowerBound.getType();
  if (loopTy != upperBound.getType()) {
    getCIRGenModule().errorNYI(
        s.getSourceRange(),
        "OpenMP OMPForSimdDirective with mismatched loop bound types");
    return mlir::failure();
  }
  mlir::Value step = createOpenMPIntegerConstant(*this, begin, loopTy, 1);

  auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
  wsloopOp.setNowait(nowait);
  wsloopOp.setComposite(true);
  OpenMPClauseEmissionInfo clauseInfo =
      emitOpenMPClauses(wsloopOp, wsloopClauses,
                        /*enclosingFirstprivateVars=*/{},
                        /*allowWsloopConditionalLastprivate=*/true);

  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    llvm::SmallVector<std::pair<const VarDecl *, Address>, 4> savedDecls;
    llvm::DenseMap<const VarDecl *, Address> wsloopOriginalDecls;
    llvm::SmallVector<mlir::Type, 4> wsloopPrivateArgTypes;
    llvm::SmallVector<mlir::Location, 4> wsloopPrivateArgLocs;
    for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
      wsloopPrivateArgTypes.push_back(privateVar.getType());
      wsloopPrivateArgLocs.push_back(privateVar.getLoc());
    }
    llvm::SmallVector<mlir::Type, 4> reductionArgTypes;
    llvm::SmallVector<mlir::Location, 4> reductionArgLocs;
    for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
      reductionArgTypes.push_back(reductionVar.getType());
      reductionArgLocs.push_back(reductionVar.getLoc());
    }
    wsloopPrivateArgTypes.append(reductionArgTypes);
    wsloopPrivateArgLocs.append(reductionArgLocs);
    mlir::Block *wsloopBlock = builder.createBlock(&wsloopOp.getRegion(), {},
                                                   wsloopPrivateArgTypes,
                                                   wsloopPrivateArgLocs);
    builder.setInsertionPointToEnd(wsloopBlock);
    llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
        reductionDeclGuards;
    auto wsloopPrivateArgs =
        mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(wsloopOp.getOperation())
            .getPrivateBlockArgs();
    assert(clauseInfo.privateIndices.size() == clauseInfo.privateVars.size() &&
           "mismatched OpenMP wsloop private metadata");
    for (auto [index, vd] :
         llvm::zip_equal(clauseInfo.privateIndices, clauseInfo.privateVars)) {
      assert(index < wsloopPrivateArgs.size() &&
             "unexpected OpenMP wsloop private block arg index");
      mlir::Value privateArg = wsloopPrivateArgs[index];
      auto ptrTy = mlir::cast<cir::PointerType>(privateArg.getType());
      if (auto it = localDeclMap.find(vd); it != localDeclMap.end()) {
        savedDecls.emplace_back(vd, it->second);
        wsloopOriginalDecls.insert({vd->getCanonicalDecl(), it->second});
      }
      replaceAddrOfLocalVar(
          vd, Address(privateArg, ptrTy.getPointee(),
                      getContext().getDeclAlign(vd)));
    }
    remapOpenMPReductionDecls(*this, wsloopOp.getOperation(), clauseInfo,
                              reductionDeclGuards);

    mlir::omp::SimdOperands simdClauseOps;
    auto simdOp = mlir::omp::SimdOp::create(builder, begin, simdClauseOps);
    simdOp.setComposite(true);
    {
      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPoint(wsloopOp->getBlock(),
                                mlir::Block::iterator(wsloopOp));
      if (mlir::failed(
              emitOpenMPSimdClausesForComposite(*this, simdOp, simdClauses))) {
        res = mlir::failure();
        return res;
      }
    }

    mlir::Block *simdBlock = builder.createBlock(&simdOp.getRegion());
    builder.setInsertionPointToEnd(simdBlock);

    mlir::omp::LoopNestOperands loopClauseOps;
    loopClauseOps.loopLowerBounds.push_back(lowerBound);
    loopClauseOps.loopUpperBounds.push_back(upperBound);
    loopClauseOps.loopSteps.push_back(step);
    if (loopInfo->upperInclusive)
      loopClauseOps.loopInclusive = builder.getUnitAttr();

    auto loopNestOp =
        mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
    loopNestOp->setAttr(
        "omp.frontend.iv_signed",
        builder.getDenseBoolArrayAttr(
            {loopInfo->iv->getType()->hasSignedIntegerRepresentation()}));

    mlir::Block *loopBlock = builder.createBlock(
        &loopNestOp.getRegion(), {}, {loopTy},
        {getLoc(loopInfo->iv->getLocation())});
    builder.setInsertionPointToEnd(loopBlock);

    DeclMapRevertingRAII ivMapGuard(*this, loopInfo->iv);
    replaceAddrOfLocalVar(loopInfo->iv, ivSeedRawAlloca);
    mlir::Value ivValue = castOpenMPValueToCIRType(
        *this, getLoc(loopInfo->iv->getLocation()), loopBlock->getArgument(0),
        ivType);
    builder.createStore(getLoc(loopInfo->iv->getLocation()), ivValue,
                        ivSeedRawAlloca);

    OpenMPScanParentDirectiveRAII scanDirectiveScope(
        *this, s, collectOpenMPReductionRegionArgs(wsloopOp));
    bool hasConditionalLastprivate =
        pushOpenMPLastprivateConditional(wsloopClauses, ivSeedRawAlloca,
                                         s.getBeginLoc());
    res = emitOMPLoopBody(*this, builder, begin, loopInfo->forStmt->getBody());
    if (!clauseInfo.lastprivateIndices.empty()) {
      assert(clauseInfo.lastprivateIndices.size() ==
                 clauseInfo.lastprivateVars.size() &&
             "mismatched OpenMP wsloop lastprivate metadata");
      assert(clauseInfo.lastprivateIndices.size() ==
                 clauseInfo.lastprivateConditionals.size() &&
             "mismatched OpenMP wsloop conditional-lastprivate metadata");
      mlir::Location ivLoc = getLoc(loopInfo->iv->getLocation());
      auto cirIVTy = mlir::dyn_cast<cir::IntType>(ivType);
      assert(cirIVTy && "expected CIR integer type for loop IV");
      mlir::Value upperBoundValue =
          castOpenMPValueToCIRType(*this, ivLoc, upperBound, ivType);
      mlir::Value stepValue = builder.getConstInt(ivLoc, cirIVTy, 1);
      mlir::Value nextIV = builder.createAdd(ivLoc, ivValue, stepValue);
      mlir::Value isLastIteration = builder.createCompare(
          ivLoc, loopInfo->upperInclusive ? cir::CmpOpKind::gt
                                          : cir::CmpOpKind::ge,
          nextIV, upperBoundValue);

      cir::IfOp::create(
          builder, ivLoc, isLastIteration, /*withElseRegion=*/false,
          [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
            for (auto [index, vd, srcVD, destVD, assignOp, isConditional] :
                 llvm::zip_equal(
                     clauseInfo.lastprivateIndices, clauseInfo.lastprivateVars,
                     clauseInfo.lastprivateSourceVDs,
                     clauseInfo.lastprivateDestVDs,
                     clauseInfo.lastprivateAssignmentOps,
                     clauseInfo.lastprivateConditionals)) {
              assert(index < wsloopPrivateArgs.size() &&
                     "unexpected OpenMP wsloop lastprivate block arg index");
              Address srcAddr = getOpenMPPrivateStorageAddress(*this, vd);
              if (isConditional)
                emitOpenMPLastprivateConditionalFinalUpdate(
                    vd, srcAddr, vd->getLocation());
              auto originalIt = wsloopOriginalDecls.find(vd->getCanonicalDecl());
              assert(originalIt != wsloopOriginalDecls.end() &&
                     "missing lastprivate destination address");
              emitOpenMPLastprivateCopy(*this, vd, srcAddr, originalIt->second,
                                        srcVD, destVD, assignOp);
            }
            cir::YieldOp::create(thenBuilder, thenLoc);
          });
    }
    if (hasConditionalLastprivate)
      popOpenMPLastprivateConditional();
    reductionDeclGuards.clear();
    mlir::omp::YieldOp::create(builder, end);
    builder.setInsertionPointAfter(wsloopOp);
    for (auto [vd, addr] : llvm::reverse(savedDecls))
      replaceAddrOfLocalVar(vd, addr);
  }

  return res;
}

struct OpenMPSectionBody {
  llvm::SmallVector<const Stmt *, 4> stmts;
};

static void collectOpenMPSectionBodies(
    const Stmt *body,
    llvm::SmallVectorImpl<OpenMPSectionBody> &sectionBodies) {
  if (!body)
    return;

  if (const auto *cs = dyn_cast<CompoundStmt>(body)) {
    // OpenMP permits an implicit first section before the first omp.section.
    bool hasExplicitSections = llvm::any_of(
        cs->body(),
        [](const Stmt *child) { return isa<OMPSectionDirective>(child); });
    if (hasExplicitSections) {
      OpenMPSectionBody *currentSection = nullptr;
      for (const Stmt *child : cs->body()) {
        if (const auto *sd = dyn_cast<OMPSectionDirective>(child)) {
          sectionBodies.emplace_back();
          currentSection = &sectionBodies.back();
          currentSection->stmts.push_back(sd->getAssociatedStmt());
          continue;
        }

        if (!currentSection) {
          sectionBodies.emplace_back();
          currentSection = &sectionBodies.back();
        }
        currentSection->stmts.push_back(child);
      }
      return;
    }
  }

  sectionBodies.emplace_back();
  sectionBodies.back().stmts.push_back(body);
}

static mlir::LogicalResult emitOpenMPSectionBody(
    CIRGenFunction &cgf, llvm::ArrayRef<const Stmt *> stmts) {
  mlir::LogicalResult res = mlir::success();
  for (const Stmt *stmt : stmts) {
    res = cgf.emitStmt(stmt, /*useCurrentScope=*/true);
    if (mlir::failed(res))
      break;
  }
  return res;
}

static mlir::LogicalResult emitOpenMPSectionSwitch(
    CIRGenFunction &cgf, mlir::Location loc, mlir::Value cond,
    ArrayRef<OpenMPSectionBody> sectionBodies) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  auto condTy = mlir::dyn_cast<cir::IntType>(cond.getType());
  if (!condTy) {
    cgf.getCIRGenModule().errorNYI(SourceRange(),
                                   "OpenMP sections switch with non-integer IV");
    return mlir::failure();
  }

  mlir::LogicalResult res = mlir::success();
  unsigned condWidth = condTy.getWidth();
  cir::SwitchOp::create(
      builder, loc, cond,
      [&](mlir::OpBuilder &, mlir::Location switchLoc,
          mlir::OperationState &) {
        auto emitCase = [&](mlir::ArrayAttr value, cir::CaseOpKind kind,
                            const OpenMPSectionBody *caseBody) {
          mlir::OpBuilder::InsertPoint insertPoint;
          cir::CaseOp::create(builder, switchLoc, value, kind, insertPoint);
          auto caseOp =
              mlir::cast<cir::CaseOp>(insertPoint.getBlock()->getParentOp());
          mlir::OpBuilder::InsertionGuard guard(builder);
          builder.restoreInsertionPoint(insertPoint);
          if (caseBody)
            res = emitOpenMPSectionBody(cgf, caseBody->stmts);
          if (mlir::succeeded(res)) {
            for (mlir::Block &block : caseOp.getCaseRegion()) {
              if (block.empty() ||
                  !block.back().hasTrait<mlir::OpTrait::IsTerminator>()) {
                mlir::OpBuilder::InsertionGuard blockGuard(builder);
                builder.setInsertionPointToEnd(&block);
                cir::YieldOp::create(builder, switchLoc);
              }
            }
          }
        };

        for (auto [index, sectionBody] : llvm::enumerate(sectionBodies)) {
          mlir::ArrayAttr caseValue = builder.getArrayAttr(
              {cir::IntAttr::get(
                  condTy,
                  llvm::APInt(condWidth, static_cast<uint64_t>(index),
                              /*isSigned=*/false))});
          emitCase(caseValue, cir::CaseOpKind::Equal, &sectionBody);
          if (mlir::failed(res))
            break;
        }

        if (mlir::succeeded(res))
          emitCase(builder.getArrayAttr({}), cir::CaseOpKind::Default,
                   /*caseBody=*/nullptr);
        if (mlir::succeeded(res))
          cir::YieldOp::create(builder, switchLoc);
      });
  return res;
}

mlir::LogicalResult CIRGenFunction::emitOMPSectionsWithWsloop(
    ArrayRef<const OMPClause *> wsloopClauses, const Stmt *body,
    ArrayRef<const VarDecl *> enclosingFirstprivateVars,
    SourceLocation directiveLoc, SourceLocation endLoc, bool nowait,
    bool emitNowaitLastprivateBarrier) {
  llvm::SmallVector<OpenMPSectionBody, 4> sectionBodies;
  collectOpenMPSectionBodies(body, sectionBodies);
  if (sectionBodies.empty())
    return mlir::success();

  mlir::LogicalResult res = mlir::success();
  mlir::Location begin = getLoc(directiveLoc);
  mlir::Location end = getLoc(endLoc);

  bool hasConditionalLastprivateClause = llvm::any_of(
      wsloopClauses, [](const OMPClause *clause) {
        return clause->getClauseKind() == llvm::omp::OMPC_lastprivate &&
               cast<OMPLastprivateClause>(clause)->getKind() ==
                   OMPC_LASTPRIVATE_conditional;
      });

  mlir::Type sectionIVType = builder.getSInt32Ty();
  Address sectionIVRawAlloca = Address::invalid();
  if (hasConditionalLastprivateClause) {
    createTempAlloca(sectionIVType,
                     getContext().getTypeAlignInChars(getContext().IntTy),
                     begin, "__omp_sections_iv", /*arraySize=*/nullptr,
                     &sectionIVRawAlloca);
  }

  mlir::Type loopTy = builder.getI32Type();
  mlir::Value lowerBound =
      createOpenMPIntegerConstant(*this, begin, loopTy, 0);
  mlir::Value upperBound = createOpenMPIntegerConstant(
      *this, begin, loopTy, static_cast<int64_t>(sectionBodies.size()) - 1);
  mlir::Value step = createOpenMPIntegerConstant(*this, begin, loopTy, 1);

  auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
  wsloopOp.setNowait(nowait);
  OpenMPClauseEmissionInfo clauseInfo =
      emitOpenMPClauses(wsloopOp, wsloopClauses, enclosingFirstprivateVars,
                        /*allowWsloopConditionalLastprivate=*/true);

  llvm::DenseMap<const VarDecl *, Address> wsloopOriginalDecls;
  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPoint(wsloopOp);
    for (const OMPClause *clause : wsloopClauses) {
      if (clause->getClauseKind() != llvm::omp::OMPC_lastprivate)
        continue;

      const auto *lastprivateClause = cast<OMPLastprivateClause>(clause);
      for (const Expr *varExpr : lastprivateClause->varlist()) {
        const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr);
        if (!vd)
          continue;
        wsloopOriginalDecls.try_emplace(vd->getCanonicalDecl(),
                                        emitLValue(varExpr).getAddress());
      }
    }
  }

  {
    mlir::OpBuilder::InsertionGuard guard(builder);

    llvm::SmallVector<mlir::Type, 4> wsloopArgTypes;
    llvm::SmallVector<mlir::Location, 4> wsloopArgLocs;
    for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
      wsloopArgTypes.push_back(privateVar.getType());
      wsloopArgLocs.push_back(privateVar.getLoc());
    }
    for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
      wsloopArgTypes.push_back(reductionVar.getType());
      wsloopArgLocs.push_back(reductionVar.getLoc());
    }

    mlir::Block *wsloopBlock = builder.createBlock(&wsloopOp.getRegion(), {},
                                                   wsloopArgTypes,
                                                   wsloopArgLocs);
    builder.setInsertionPointToEnd(wsloopBlock);

    llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
        wsloopPrivateDeclGuards;
    llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
        reductionDeclGuards;
    auto wsloopPrivateArgs =
        mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(wsloopOp.getOperation())
            .getPrivateBlockArgs();
    assert(clauseInfo.privateIndices.size() == clauseInfo.privateVars.size() &&
           "mismatched OpenMP wsloop private metadata");
    for (auto [index, vd] :
         llvm::zip_equal(clauseInfo.privateIndices, clauseInfo.privateVars)) {
      assert(index < wsloopPrivateArgs.size() &&
             "unexpected OpenMP wsloop private block arg index");
      mlir::Value privateArg = wsloopPrivateArgs[index];
      auto ptrTy = mlir::cast<cir::PointerType>(privateArg.getType());
      wsloopPrivateDeclGuards.push_back(
          std::make_unique<DeclMapRevertingRAII>(*this, vd));
      replaceAddrOfLocalVar(
          vd, Address(privateArg, ptrTy.getPointee(),
                      getContext().getDeclAlign(vd)));
    }
    remapOpenMPReductionDecls(*this, wsloopOp.getOperation(), clauseInfo,
                              reductionDeclGuards);

    mlir::omp::LoopNestOperands loopClauseOps;
    loopClauseOps.loopLowerBounds.push_back(lowerBound);
    loopClauseOps.loopUpperBounds.push_back(upperBound);
    loopClauseOps.loopSteps.push_back(step);
    loopClauseOps.loopInclusive = builder.getUnitAttr();

    auto loopNestOp =
        mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
    loopNestOp->setAttr("omp.frontend.iv_signed",
                        builder.getDenseBoolArrayAttr({true}));

    mlir::Block *loopBlock =
        builder.createBlock(&loopNestOp.getRegion(), {}, {loopTy}, {begin});
    builder.setInsertionPointToEnd(loopBlock);

    mlir::Value sectionIVValue = castOpenMPValueToCIRType(
        *this, begin, loopBlock->getArgument(0), sectionIVType);
    if (sectionIVRawAlloca.isValid())
      builder.createStore(begin, sectionIVValue, sectionIVRawAlloca);

    bool hasConditionalLastprivate =
        sectionIVRawAlloca.isValid() &&
        pushOpenMPLastprivateConditional(wsloopClauses, sectionIVRawAlloca,
                                         directiveLoc);

    res = emitOpenMPSectionSwitch(*this, begin, sectionIVValue, sectionBodies);

    if (!clauseInfo.lastprivateIndices.empty()) {
      assert(clauseInfo.lastprivateIndices.size() ==
                 clauseInfo.lastprivateVars.size() &&
             "mismatched OpenMP wsloop lastprivate metadata");
      assert(clauseInfo.lastprivateIndices.size() ==
                 clauseInfo.lastprivateConditionals.size() &&
             "mismatched OpenMP wsloop conditional-lastprivate metadata");
      mlir::Location ivLoc = begin;
      auto cirIVTy = mlir::cast<cir::IntType>(sectionIVType);
      mlir::Value upperBoundValue =
          castOpenMPValueToCIRType(*this, ivLoc, upperBound, sectionIVType);
      mlir::Value stepValue = builder.getConstInt(ivLoc, cirIVTy, 1);
      mlir::Value nextIV = builder.createAdd(ivLoc, sectionIVValue, stepValue);
      mlir::Value isLastIteration =
          builder.createCompare(ivLoc, cir::CmpOpKind::gt, nextIV,
                                upperBoundValue);

      cir::IfOp::create(
          builder, ivLoc, isLastIteration, /*withElseRegion=*/false,
          [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
            for (auto [index, vd, srcVD, destVD, assignOp, isConditional] :
                 llvm::zip_equal(
                     clauseInfo.lastprivateIndices, clauseInfo.lastprivateVars,
                     clauseInfo.lastprivateSourceVDs,
                     clauseInfo.lastprivateDestVDs,
                     clauseInfo.lastprivateAssignmentOps,
                     clauseInfo.lastprivateConditionals)) {
              assert(index < wsloopPrivateArgs.size() &&
                     "unexpected OpenMP wsloop lastprivate block arg index");
              Address srcAddr = getOpenMPPrivateStorageAddress(*this, vd);
              if (isConditional)
                emitOpenMPLastprivateConditionalFinalUpdate(
                    vd, srcAddr, vd->getLocation());
              auto originalIt = wsloopOriginalDecls.find(vd->getCanonicalDecl());
              assert(originalIt != wsloopOriginalDecls.end() &&
                     "missing lastprivate destination address");
              emitOpenMPLastprivateCopy(*this, vd, srcAddr, originalIt->second,
                                        srcVD, destVD, assignOp);
            }
            cir::YieldOp::create(builder, thenLoc);
          });
    }

    if (hasConditionalLastprivate)
      popOpenMPLastprivateConditional();
    wsloopPrivateDeclGuards.clear();
    reductionDeclGuards.clear();
    mlir::omp::YieldOp::create(builder, end);
  }

  if (emitNowaitLastprivateBarrier && !clauseInfo.lastprivateIndices.empty())
    mlir::omp::BarrierOp::create(builder, begin);

  return res;
}

mlir::LogicalResult
CIRGenFunction::emitOMPSectionsDirective(const OMPSectionsDirective &s) {
  if (s.hasClausesOfKind<OMPLastprivateClause>()) {
    bool nowait = false;
    llvm::SmallVector<const OMPClause *, 8> wsloopClauses;
    for (const OMPClause *clause : s.clauses()) {
      switch (clause->getClauseKind()) {
      case llvm::omp::OMPC_nowait:
        nowait = true;
        break;
      case llvm::omp::OMPC_private:
      case llvm::omp::OMPC_lastprivate:
      case llvm::omp::OMPC_reduction:
        wsloopClauses.push_back(clause);
        break;
      case llvm::omp::OMPC_shared:
        break;
      case llvm::omp::OMPC_firstprivate:
      case llvm::omp::OMPC_allocate:
        getCIRGenModule().errorNYI(
            clause->getBeginLoc(), "OpenMP sections clause",
            llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
        return mlir::failure();
      default:
        getCIRGenModule().errorNYI(s.getSourceRange(),
                                   "OpenMP OMPSectionsDirective clauses");
        return mlir::failure();
      }
    }

    return emitOMPSectionsWithWsloop(
        wsloopClauses, s.getInnermostCapturedStmt()->getCapturedStmt(),
        /*enclosingFirstprivateVars=*/{},
        s.getBeginLoc(), s.getEndLoc(), nowait,
        /*emitNowaitLastprivateBarrier=*/nowait);
  }

  // Separate nowait (handled directly on the op) from data-sharing clauses
  // that go through emitOpenMPClauses.
  bool nowait = false;
  llvm::SmallVector<const OMPClause *, 8> dataClauses;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_nowait:
      nowait = true;
      break;
    case llvm::omp::OMPC_private:
    case llvm::omp::OMPC_firstprivate:
    case llvm::omp::OMPC_reduction:
      dataClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_lastprivate:
    case llvm::omp::OMPC_allocate:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), "OpenMP sections clause",
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      break;
    case llvm::omp::OMPC_shared:
      break;
    default:
      getCIRGenModule().errorNYI(s.getSourceRange(),
                                 "OpenMP OMPSectionsDirective clauses");
      break;
    }
  }

  mlir::LogicalResult res = mlir::success();
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  auto sectionsOp = mlir::omp::SectionsOp::create(
      builder, begin,
      /*allocate_vars=*/mlir::ValueRange{},
      /*allocator_vars=*/mlir::ValueRange{},
      /*nowait=*/nowait,
      /*private_vars=*/mlir::ValueRange{},
      /*private_syms=*/mlir::ArrayAttr{},
      /*private_needs_barrier=*/false,
      /*reduction_mod=*/mlir::omp::ReductionModifierAttr{},
      /*reduction_vars=*/mlir::ValueRange{},
      /*reduction_byref=*/mlir::DenseBoolArrayAttr{},
      /*reduction_syms=*/mlir::ArrayAttr{});

  OpenMPClauseEmissionInfo sectionsClauseInfo =
      emitOpenMPClauses(sectionsOp, dataClauses);

  {
    mlir::OpBuilder::InsertionGuard guard(builder);

    // Collect block arg types for the sections region: private args first,
    // then reduction args, matching the BlockArgOpenMPOpInterface order.
    llvm::SmallVector<mlir::Type, 4> sectionsArgTypes;
    llvm::SmallVector<mlir::Location, 4> sectionsArgLocs;
    for (mlir::Value v : sectionsOp.getPrivateVars()) {
      sectionsArgTypes.push_back(v.getType());
      sectionsArgLocs.push_back(v.getLoc());
    }
    for (mlir::Value v : sectionsOp.getReductionVars()) {
      sectionsArgTypes.push_back(v.getType());
      sectionsArgLocs.push_back(v.getLoc());
    }

    mlir::Block *sectionsBlock = builder.createBlock(
        &sectionsOp.getRegion(), {}, sectionsArgTypes, sectionsArgLocs);
    builder.setInsertionPointToEnd(sectionsBlock);

    // Emit each omp.section with matching block args and reduction variable
    // remapping.
    const Stmt *body =
        s.getInnermostCapturedStmt()->getCapturedStmt();

    llvm::SmallVector<OpenMPSectionBody, 4> sectionBodies;
    collectOpenMPSectionBodies(body, sectionBodies);

    auto emitSingleSection = [&](ArrayRef<const Stmt *> sectionBody) {
      auto sectionOp = mlir::omp::SectionOp::create(builder, begin);
      mlir::OpBuilder::InsertionGuard sectionGuard(builder);
      mlir::Block *sectionBlock = builder.createBlock(
          &sectionOp.getRegion(), {}, sectionsArgTypes, sectionsArgLocs);
      builder.setInsertionPointToEnd(sectionBlock);

      llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
          sectionDeclGuards;
      if (mlir::failed(remapOpenMPPrivateDecls(
              *this, sectionOp.getOperation(), sectionsClauseInfo,
              sectionDeclGuards))) {
        res = mlir::failure();
        return;
      }
      remapOpenMPReductionDecls(*this, sectionOp.getOperation(),
                                sectionsClauseInfo, sectionDeclGuards);

      res = emitOpenMPSectionBody(*this, sectionBody);
      sectionDeclGuards.clear();
      mlir::omp::TerminatorOp::create(builder, end);
    };

    for (const OpenMPSectionBody &sectionBody : sectionBodies) {
      emitSingleSection(sectionBody.stmts);
      if (mlir::failed(res))
        break;
    }

    mlir::omp::TerminatorOp::create(builder, end);
  }

  return res;
}
mlir::LogicalResult
CIRGenFunction::emitOMPSectionDirective(const OMPSectionDirective &s) {
  assert(s.clauses().empty() && "omp section doesn't support clauses");

  mlir::LogicalResult res = mlir::success();
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  auto sectionOp = mlir::omp::SectionOp::create(builder, begin);

  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *block = builder.createBlock(&sectionOp.getRegion());
    builder.setInsertionPointToEnd(block);
    res = emitStmt(s.getAssociatedStmt(), /*useCurrentScope=*/true);
    mlir::omp::TerminatorOp::create(builder, end);
  }

  return res;
}
mlir::LogicalResult
CIRGenFunction::emitOMPSingleDirective(const OMPSingleDirective &s) {
  bool nowait = false;
  llvm::SmallVector<mlir::Value, 4> copyprivateVars;
  llvm::SmallVector<mlir::Attribute, 4> copyprivateSyms;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_nowait:
      nowait = true;
      break;
    case llvm::omp::OMPC_copyprivate: {
      const auto *copyprivate = cast<OMPCopyprivateClause>(clause);
      auto srcIt = copyprivate->source_exprs().begin();
      auto destIt = copyprivate->destination_exprs().begin();
      auto assignIt = copyprivate->assignment_ops().begin();
      for (const Expr *varExpr :
           llvm::make_range(copyprivate->varlist_begin(),
                            copyprivate->varlist_end())) {
        const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr);
        if (!vd) {
          getCIRGenModule().errorNYI(varExpr->getSourceRange(),
                                     "OpenMP copyprivate with non-variable "
                                     "clause expression");
          continue;
        }

        const auto *srcVD =
            cast<VarDecl>(cast<DeclRefExpr>(*srcIt)->getDecl());
        const auto *destVD =
            cast<VarDecl>(cast<DeclRefExpr>(*destIt)->getDecl());
        const Expr *assignmentOp = *assignIt;

        Address varAddr = emitLValue(varExpr).getAddress();
        cir::FuncOp copyFn = getOrCreateOpenMPCopyprivateCopyFunction(
            *this, builder, getLoc(varExpr->getBeginLoc()), vd,
            varAddr.getElementType(), srcVD, destVD, assignmentOp);
        copyprivateVars.push_back(varAddr.getPointer());
        copyprivateSyms.push_back(mlir::FlatSymbolRefAttr::get(copyFn));

        ++srcIt;
        ++destIt;
        ++assignIt;
      }
      break;
    }
    default:
      getCIRGenModule().errorNYI(s.getSourceRange(),
                                 "OpenMP OMPSingleDirective clauses");
      break;
    }
  }

  mlir::LogicalResult res = mlir::success();
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  auto singleOp = mlir::omp::SingleOp::create(
      builder, begin,
      /*allocate_vars=*/mlir::ValueRange{},
      /*allocator_vars=*/mlir::ValueRange{},
      /*copyprivate_vars=*/copyprivateVars,
      /*copyprivate_syms=*/mlir::ArrayAttr::get(builder.getContext(),
                                                copyprivateSyms),
      /*nowait=*/nowait,
      /*private_vars=*/mlir::ValueRange{},
      /*private_syms=*/mlir::ArrayAttr{},
      /*private_needs_barrier=*/false);

  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *block = builder.createBlock(&singleOp.getRegion());
    builder.setInsertionPointToEnd(block);
    res = emitStmt(s.getAssociatedStmt(), /*useCurrentScope=*/true);
    mlir::omp::TerminatorOp::create(builder, end);
  }

  return res;
}
mlir::LogicalResult
CIRGenFunction::emitOMPMasterDirective(const OMPMasterDirective &s) {
  assert(s.clauses().empty() && "omp master doesn't support clauses");

  mlir::LogicalResult res = mlir::success();
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  auto masterOp = mlir::omp::MasterOp::create(builder, begin);

  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *block = builder.createBlock(&masterOp.getRegion());
    builder.setInsertionPointToEnd(block);
    res = emitStmt(s.getAssociatedStmt(), /*useCurrentScope=*/true);
    mlir::omp::TerminatorOp::create(builder, end);
  }

  return res;
}
mlir::LogicalResult
CIRGenFunction::emitOMPCriticalDirective(const OMPCriticalDirective &s) {
  std::optional<uint64_t> hint;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_hint:
      hint = cast<OMPHintClause>(clause)
                 ->getHint()
                 ->EvaluateKnownConstInt(getContext())
                 .getZExtValue();
      break;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), "OpenMP critical clause",
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      return mlir::failure();
    }
  }

  mlir::FlatSymbolRefAttr criticalName;
  if (!s.getDirectiveName().getName().isEmpty()) {
    criticalName = getCIRGenModule().getOrCreateOpenMPCriticalDecl(
        s.getDirectiveName().getAsString(), hint, getLoc(s.getBeginLoc()));
  }

  mlir::LogicalResult res = mlir::success();
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  auto criticalOp = mlir::omp::CriticalOp::create(builder, begin, criticalName);

  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *block = builder.createBlock(&criticalOp.getRegion());
    builder.setInsertionPointToEnd(block);
    res = emitStmt(s.getAssociatedStmt(), /*useCurrentScope=*/true);
    mlir::omp::TerminatorOp::create(builder, end);
  }

  return res;
}
mlir::LogicalResult
CIRGenFunction::emitOMPParallelForDirective(const OMPParallelForDirective &s) {
  auto loopInfo = getOpenMPSimpleCanonicalForInfo(s);
  if (!loopInfo) {
    getCIRGenModule().errorNYI(
        s.getSourceRange(),
        "OpenMP OMPParallelForDirective for non-canonical/non-unit-stride loop");
    return mlir::failure();
  }

  if (s.hasCancel()) {
    getCIRGenModule().errorNYI(s.getBeginLoc(),
                               "OpenMP ParallelFor with Cancel");
    return mlir::failure();
  }

  mlir::LogicalResult res = mlir::success();
  llvm::SmallVector<mlir::Type> retTy;
  llvm::SmallVector<mlir::Value> operands;
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  llvm::SmallVector<OpenMPCopyinMasterAddr, 4> copyinMasterAddrs =
      collectOpenMPCopyinMasterAddresses(*this, s);

  auto parallelOp =
      mlir::omp::ParallelOp::create(builder, begin, retTy, operands);
  llvm::SmallVector<const OMPClause *, 8> parallelClauses;
  llvm::SmallVector<const OMPClause *, 8> wsloopClauses;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if:
    case llvm::omp::OMPC_num_threads:
    case llvm::omp::OMPC_severity:
    case llvm::omp::OMPC_message:
    case llvm::omp::OMPC_proc_bind:
    case llvm::omp::OMPC_firstprivate:
    case llvm::omp::OMPC_copyin:
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_private:
    case llvm::omp::OMPC_lastprivate:
    case llvm::omp::OMPC_reduction:
    case llvm::omp::OMPC_schedule:
    case llvm::omp::OMPC_ordered:
    case llvm::omp::OMPC_order:
      wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_default:
    case llvm::omp::OMPC_shared:
    case llvm::omp::OMPC_collapse:
      break;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), "OpenMP parallel for clause",
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      break;
    }
  }

  OpenMPClauseEmissionInfo parallelClauseInfo =
      emitOpenMPClauses(parallelOp, parallelClauses);
  const CapturedStmt *parallelCaptured =
      s.getCapturedStmt(OpenMPDirectiveKind::OMPD_parallel);
  assert(parallelCaptured && "expected OpenMP parallel captured stmt");
  parallelOp->setAttr("cir.instrprof_func_name",
                      cgm.getStringAttrCached(
                          getOpenMPHelperPGOName(*parallelCaptured)));
  parallelOp->setAttr("cir.instrprof_func_linkage",
                      builder.getI32IntegerAttr(static_cast<int32_t>(
                          llvm::GlobalValue::InternalLinkage)));
  CharUnits ivAlign = getContext().getDeclAlign(loopInfo->iv);
  mlir::Type ivType = convertTypeForMem(loopInfo->iv->getType());
  Address ivSeedRawAlloca = Address::invalid();
  Address ivSeedAddr = createTempAlloca(ivType, ivAlign,
                                        getLoc(loopInfo->iv->getLocation()),
                                        loopInfo->iv->getName(),
                                        /*arraySize=*/nullptr,
                                        &ivSeedRawAlloca);
  if (!localDeclMap.count(loopInfo->iv))
    declare(ivSeedRawAlloca.getPointer(), loopInfo->iv,
            loopInfo->iv->getType(), getLoc(loopInfo->iv->getSourceRange()),
            ivAlign);
  unsigned ivParallelPrivateIndex = parallelOp.getPrivateVars().size();
  auto ivPrivatizer = getOrCreateOpenMPPrivatePrivatizer(
      *this, builder, getLoc(loopInfo->iv->getLocation()), loopInfo->iv,
      ivType);
  parallelOp.getPrivateVarsMutable().append(ivSeedAddr.getPointer());
  llvm::SmallVector<mlir::Attribute, 4> parallelPrivateSyms;
  if (auto existingSyms = parallelOp.getPrivateSyms())
    llvm::append_range(parallelPrivateSyms, *existingSyms);
  parallelPrivateSyms.push_back(mlir::SymbolRefAttr::get(ivPrivatizer));
  parallelOp.setPrivateSymsAttr(
      mlir::ArrayAttr::get(builder.getContext(), parallelPrivateSyms));
  parallelClauseInfo.privateIndices.push_back(ivParallelPrivateIndex);
  parallelClauseInfo.privateVars.push_back(loopInfo->iv);
  if (isInsideOpenMPTargetRegion(*this) &&
      mlir::failed(appendOpenMPParallelNestedSyntheticFirstprivatePrivates(
          *this, parallelOp, wsloopClauses, parallelClauseInfo)))
    return mlir::failure();

  {
    mlir::OpBuilder::InsertionGuard guardCase(builder);
    llvm::SmallVector<mlir::Type, 4> privateArgTypes;
    llvm::SmallVector<mlir::Location, 4> privateArgLocs;
    for (mlir::Value privateVar : parallelOp.getPrivateVars()) {
      privateArgTypes.push_back(privateVar.getType());
      privateArgLocs.push_back(privateVar.getLoc());
    }

    mlir::Block *block = builder.createBlock(&parallelOp.getRegion(), {},
                                             privateArgTypes, privateArgLocs);
    builder.setInsertionPointToEnd(block);

    cir::ScopeOp::create(
        builder, begin, /*scopeBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          auto pgoScope = enterOpenMPHelperRegionCounters(*parallelCaptured);
          LexicalScope ls{*this, begin, builder.getInsertionBlock()};
          llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
              parallelPrivateDeclGuards;
          llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
              wsloopPrivateDeclGuards;
          llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
              reductionDeclGuards;
          llvm::DenseMap<const VarDecl *, Address> parallelOriginalDecls;
          llvm::DenseMap<const VarDecl *, Address> wsloopOriginalDecls;
          if (mlir::failed(remapOpenMPPrivateDecls(
                  *this, parallelOp.getOperation(), parallelClauseInfo,
                  parallelPrivateDeclGuards, &parallelOriginalDecls))) {
            res = mlir::failure();
            return;
          }

          OpenMPTLSCopyinDeclMapScope tlsCopyinDecls(*this, s);
          (void)emitOpenMPCopyinClause(*this, s, copyinMasterAddrs);
          bool hasDisabledLastprivateConditional =
              pushDisabledOpenMPLastprivateConditional(s);
          auto popDisabledLastprivateConditional = llvm::scope_exit([&] {
            if (hasDisabledLastprivateConditional)
              popOpenMPLastprivateConditional();
          });
          incrementProfileCounter(parallelCaptured->getCapturedDecl()->getBody());
          DeclMapRevertingRAII ivMapGuard(*this, loopInfo->iv);

          mlir::Value lowerBound =
              emitOpenMPIntegerExpr(*this, loopInfo->lowerBound);
          mlir::Value upperBound =
              emitOpenMPIntegerExpr(*this, loopInfo->upperBound);
          mlir::Type loopTy = lowerBound.getType();
          if (loopTy != upperBound.getType()) {
            getCIRGenModule().errorNYI(
                s.getSourceRange(),
                "OpenMP OMPParallelForDirective with mismatched loop bound types");
            return;
          }
          mlir::Value step =
              createOpenMPIntegerConstant(*this, begin, loopTy, 1);

          auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
          // This is the combined 'parallel for' construct, so the enclosing
          // parallel region already provides the end-of-construct
          // synchronization. Mark the nested worksharing loop as nowait to
          // avoid materializing an extra __kmpc_barrier after loop finalization
          // in LLVM IR, matching OGCG.
          wsloopOp.setNowait(true);
          OpenMPClauseEmissionInfo wsloopClauseInfo =
              emitOpenMPClauses(wsloopOp, wsloopClauses,
                                parallelClauseInfo.privateVars,
                                /*allowWsloopConditionalLastprivate=*/true);
          llvm::SmallVector<mlir::Type, 4> wsloopPrivateArgTypes;
          llvm::SmallVector<mlir::Location, 4> wsloopPrivateArgLocs;
          for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
            wsloopPrivateArgTypes.push_back(privateVar.getType());
            wsloopPrivateArgLocs.push_back(privateVar.getLoc());
          }
          llvm::SmallVector<mlir::Type, 4> wsloopReductionArgTypes;
          llvm::SmallVector<mlir::Location, 4> wsloopReductionArgLocs;
          for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
            wsloopReductionArgTypes.push_back(reductionVar.getType());
            wsloopReductionArgLocs.push_back(reductionVar.getLoc());
          }
          wsloopPrivateArgTypes.append(wsloopReductionArgTypes);
          wsloopPrivateArgLocs.append(wsloopReductionArgLocs);
          mlir::Block *wsloopBlock = builder.createBlock(
              &wsloopOp.getRegion(), {}, wsloopPrivateArgTypes,
              wsloopPrivateArgLocs);
          builder.setInsertionPointToEnd(wsloopBlock);

          mlir::omp::LoopNestOperands loopClauseOps;
          loopClauseOps.loopLowerBounds.push_back(lowerBound);
          loopClauseOps.loopUpperBounds.push_back(upperBound);
          loopClauseOps.loopSteps.push_back(step);
          if (loopInfo->upperInclusive)
            loopClauseOps.loopInclusive = builder.getUnitAttr();

          auto loopNestOp =
              mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
          loopNestOp->setAttr(
              "omp.frontend.iv_signed",
              builder.getDenseBoolArrayAttr(
                  {loopInfo->iv->getType()->hasSignedIntegerRepresentation()}));
          mlir::Block *loopBlock = builder.createBlock(
              &loopNestOp.getRegion(), {}, {loopTy},
              {getLoc(loopInfo->iv->getLocation())});
          builder.setInsertionPointToEnd(loopBlock);
          auto wsloopPrivateArgs =
              mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                  wsloopOp.getOperation())
                  .getPrivateBlockArgs();
          if (mlir::failed(remapOpenMPPrivateDecls(
                  *this, wsloopOp.getOperation(), wsloopClauseInfo,
                  wsloopPrivateDeclGuards, &wsloopOriginalDecls))) {
            res = mlir::failure();
            return;
          }
          remapOpenMPReductionDecls(*this, wsloopOp.getOperation(),
                                    wsloopClauseInfo, reductionDeclGuards);

          mlir::Value ivValue = castOpenMPValueToCIRType(
              *this, getLoc(loopInfo->iv->getLocation()),
              loopBlock->getArgument(0), ivType);
          auto ivIt = localDeclMap.find(loopInfo->iv);
          assert(ivIt != localDeclMap.end() &&
                 "expected loop IV address to be mapped");
          builder.createStore(getLoc(loopInfo->iv->getLocation()), ivValue,
                              ivIt->second);

          bool hasConditionalLastprivate = pushOpenMPLastprivateConditional(
              wsloopClauses, ivIt->second, s.getBeginLoc());
          OpenMPScanParentDirectiveRAII scanDirectiveScope(
              *this, s, collectOpenMPReductionRegionArgs(wsloopOp));
          res = emitOMPLoopBody(*this, builder, begin,
                                loopInfo->forStmt->getBody());
          if (!wsloopClauseInfo.lastprivateIndices.empty()) {
            assert(wsloopClauseInfo.lastprivateIndices.size() ==
                       wsloopClauseInfo.lastprivateVars.size() &&
                   "mismatched OpenMP wsloop lastprivate metadata");
            assert(wsloopClauseInfo.lastprivateIndices.size() ==
                       wsloopClauseInfo.lastprivateConditionals.size() &&
                   "mismatched OpenMP wsloop conditional-lastprivate metadata");
            mlir::Location ivLoc = getLoc(loopInfo->iv->getLocation());
            auto cirIVTy = mlir::dyn_cast<cir::IntType>(ivType);
            assert(cirIVTy && "expected CIR integer type for loop IV");
            mlir::Value upperBoundValue =
                castOpenMPValueToCIRType(*this, ivLoc, upperBound, ivType);
            mlir::Value stepValue = builder.getConstInt(ivLoc, cirIVTy, 1);
            mlir::Value nextIV = builder.createAdd(ivLoc, ivValue, stepValue);
            mlir::Value isLastIteration = builder.createCompare(
                ivLoc, loopInfo->upperInclusive ? cir::CmpOpKind::gt
                                                : cir::CmpOpKind::ge,
                nextIV, upperBoundValue);

            cir::IfOp::create(
                builder, ivLoc, isLastIteration, /*withElseRegion=*/false,
                [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
                  mlir::OpBuilder::InsertionGuard guard(builder);
                  builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
                  for (auto [index, vd, srcVD, destVD, assignOp,
                             isConditional] :
                       llvm::zip_equal(
                           wsloopClauseInfo.lastprivateIndices,
                           wsloopClauseInfo.lastprivateVars,
                           wsloopClauseInfo.lastprivateSourceVDs,
                           wsloopClauseInfo.lastprivateDestVDs,
                           wsloopClauseInfo.lastprivateAssignmentOps,
                           wsloopClauseInfo.lastprivateConditionals)) {
                    assert(index < wsloopPrivateArgs.size() &&
                           "unexpected OpenMP wsloop lastprivate block arg "
                           "index");
                    Address srcAddr = getOpenMPPrivateStorageAddress(*this, vd);
                    const VarDecl *canonicalVD = vd->getCanonicalDecl();
                    Address destAddr = Address::invalid();
                    if (auto parallelIt =
                            parallelOriginalDecls.find(canonicalVD);
                        parallelIt != parallelOriginalDecls.end()) {
                      destAddr = parallelIt->second;
                    } else if (auto wsloopIt =
                                   wsloopOriginalDecls.find(canonicalVD);
                               wsloopIt != wsloopOriginalDecls.end()) {
                      destAddr = wsloopIt->second;
                    }
                    assert(destAddr.isValid() &&
                           "missing lastprivate destination address");
                    if (isConditional)
                      emitOpenMPLastprivateConditionalFinalUpdate(
                          vd, srcAddr, vd->getLocation());
                    emitOpenMPLastprivateCopy(*this, vd, srcAddr, destAddr,
                                             srcVD, destVD, assignOp);
                  }
                  cir::YieldOp::create(thenBuilder, thenLoc);
                });
          }
          if (hasConditionalLastprivate)
            popOpenMPLastprivateConditional();
          mlir::omp::YieldOp::create(builder, end);
          builder.setInsertionPointAfter(wsloopOp);

          reductionDeclGuards.clear();
        });

    mlir::omp::TerminatorOp::create(builder, end);
  }
  if (mlir::succeeded(res))
    checkForOpenMPLastprivateConditionalUpdate(*this, s);
  return res;
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelForSimdDirective(
    const OMPParallelForSimdDirective &s) {
  auto loopInfo = getOpenMPSimpleCanonicalForInfo(s);
  auto generatedLoopInfo =
      loopInfo ? std::optional<OpenMPSimpleGeneratedLoopInfo>{}
               : getOpenMPSimpleGeneratedLoopInfo(s);
  if (!loopInfo && !generatedLoopInfo) {
    getCIRGenModule().errorNYI(
        s.getSourceRange(),
        "OpenMP OMPParallelForSimdDirective for "
        "non-canonical/non-unit-stride loop");
    return mlir::failure();
  }

  mlir::LogicalResult res = mlir::success();
  llvm::SmallVector<mlir::Type> retTy;
  llvm::SmallVector<mlir::Value> operands;
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  llvm::SmallVector<OpenMPCopyinMasterAddr, 4> copyinMasterAddrs =
      collectOpenMPCopyinMasterAddresses(*this, s);
  llvm::SmallVector<const OMPClause *, 8> parallelClauses;
  llvm::SmallVector<const OMPClause *, 8> wsloopClauses;
  llvm::SmallVector<const OMPClause *, 8> simdClauses;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if: {
      const auto *ifClause = cast<OMPIfClause>(clause);
      if (ifClause->getNameModifier() == llvm::omp::OMPD_unknown ||
          ifClause->getNameModifier() == llvm::omp::OMPD_parallel) {
        parallelClauses.push_back(clause);
        break;
      }
      if (ifClause->getNameModifier() == llvm::omp::OMPD_simd) {
        simdClauses.push_back(clause);
        break;
      }
      getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                 "OpenMP parallel for simd if clause modifier");
      break;
    }
    case llvm::omp::OMPC_num_threads:
    case llvm::omp::OMPC_severity:
    case llvm::omp::OMPC_message:
    case llvm::omp::OMPC_proc_bind:
    case llvm::omp::OMPC_firstprivate:
    case llvm::omp::OMPC_copyin:
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_private:
    case llvm::omp::OMPC_lastprivate:
    case llvm::omp::OMPC_reduction:
    case llvm::omp::OMPC_schedule:
    case llvm::omp::OMPC_ordered:
    case llvm::omp::OMPC_linear:
      wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_aligned:
    case llvm::omp::OMPC_order:
    case llvm::omp::OMPC_nontemporal:
    case llvm::omp::OMPC_safelen:
    case llvm::omp::OMPC_simdlen:
      simdClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_default:
    case llvm::omp::OMPC_shared:
    case llvm::omp::OMPC_collapse:
      break;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), "OpenMP parallel for simd clause",
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      break;
    }
  }

  llvm::SmallPtrSet<const VarDecl *, 4> materializedClauseVars;
  materializeOpenMPSimdCompositeClauseExprs(*this, simdClauses,
                                            materializedClauseVars);
  auto parallelOp =
      mlir::omp::ParallelOp::create(builder, begin, retTy, operands);
  OpenMPClauseEmissionInfo parallelClauseInfo =
      emitOpenMPClauses(parallelOp, parallelClauses);
  const CapturedStmt *parallelCaptured =
      s.getCapturedStmt(OpenMPDirectiveKind::OMPD_parallel);
  assert(parallelCaptured && "expected OpenMP parallel captured stmt");
  parallelOp->setAttr("cir.instrprof_func_name",
                      cgm.getStringAttrCached(
                          getOpenMPHelperPGOName(*parallelCaptured)));
  parallelOp->setAttr("cir.instrprof_func_linkage",
                      builder.getI32IntegerAttr(static_cast<int32_t>(
                          llvm::GlobalValue::InternalLinkage)));
  Address ivSeedRawAlloca = Address::invalid();
  mlir::Type ivType;
  llvm::SmallVector<mlir::Attribute, 4> parallelPrivateSyms;
  if (auto existingSyms = parallelOp.getPrivateSyms())
    llvm::append_range(parallelPrivateSyms, *existingSyms);
  if (loopInfo) {
    CharUnits ivAlign = getContext().getDeclAlign(loopInfo->iv);
    ivType = convertTypeForMem(loopInfo->iv->getType());
    Address ivSeedAddr =
        createTempAlloca(ivType, ivAlign, getLoc(loopInfo->iv->getLocation()),
                         loopInfo->iv->getName(), /*arraySize=*/nullptr,
                         &ivSeedRawAlloca);
    if (!localDeclMap.count(loopInfo->iv))
      declare(ivSeedRawAlloca.getPointer(), loopInfo->iv,
              loopInfo->iv->getType(),
              getLoc(loopInfo->iv->getSourceRange()), ivAlign);
    unsigned ivParallelPrivateIndex = parallelOp.getPrivateVars().size();
    auto ivPrivatizer = getOrCreateOpenMPPrivatePrivatizer(
        *this, builder, getLoc(loopInfo->iv->getLocation()), loopInfo->iv,
        ivType);
    parallelOp.getPrivateVarsMutable().append(ivSeedAddr.getPointer());
    parallelPrivateSyms.push_back(mlir::SymbolRefAttr::get(ivPrivatizer));
    parallelClauseInfo.privateIndices.push_back(ivParallelPrivateIndex);
    parallelClauseInfo.privateVars.push_back(loopInfo->iv);
  }
  parallelOp.setPrivateSymsAttr(
      mlir::ArrayAttr::get(builder.getContext(), parallelPrivateSyms));
  if (isInsideOpenMPTargetRegion(*this) &&
      mlir::failed(appendOpenMPParallelNestedSyntheticFirstprivatePrivates(
          *this, parallelOp, wsloopClauses, parallelClauseInfo)))
    return mlir::failure();

  {
    mlir::OpBuilder::InsertionGuard guardCase(builder);
    llvm::SmallVector<mlir::Type, 4> privateArgTypes;
    llvm::SmallVector<mlir::Location, 4> privateArgLocs;
    for (mlir::Value privateVar : parallelOp.getPrivateVars()) {
      privateArgTypes.push_back(privateVar.getType());
      privateArgLocs.push_back(privateVar.getLoc());
    }

    mlir::Block *block = builder.createBlock(&parallelOp.getRegion(), {},
                                             privateArgTypes, privateArgLocs);
    builder.setInsertionPointToEnd(block);

    cir::ScopeOp::create(
        builder, begin, /*scopeBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          auto pgoScope = enterOpenMPHelperRegionCounters(*parallelCaptured);
          OpenMPNontemporalDeclsRAII nontemporalDecls(*this, s);
          LexicalScope ls{*this, begin, builder.getInsertionBlock()};
          llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
              parallelPrivateDeclGuards;
          llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
              reductionDeclGuards;
          llvm::DenseMap<const VarDecl *, Address> parallelOriginalDecls;
          llvm::DenseMap<const VarDecl *, Address> wsloopOriginalDecls;
          if (mlir::failed(remapOpenMPPrivateDecls(
                  *this, parallelOp.getOperation(), parallelClauseInfo,
                  parallelPrivateDeclGuards, &parallelOriginalDecls))) {
            res = mlir::failure();
            return;
          }

          OpenMPTLSCopyinDeclMapScope tlsCopyinDecls(*this, s);
          (void)emitOpenMPCopyinClause(*this, s, copyinMasterAddrs);
          bool hasDisabledLastprivateConditional =
              pushDisabledOpenMPLastprivateConditional(s);
          auto popDisabledLastprivateConditional = llvm::scope_exit([&] {
            if (hasDisabledLastprivateConditional)
              popOpenMPLastprivateConditional();
          });
          incrementProfileCounter(parallelCaptured->getCapturedDecl()->getBody());

          if (!loopInfo) {
            assert(generatedLoopInfo &&
                   "expected generated simd loop info for non-canonical "
                   "parallel for simd");

            if (mlir::failed(
                    emitOpenMPTransformPreInits(*this, s.getPreInits()))) {
              res = mlir::failure();
              return;
            }

            bool preCondConstant = false;
            if (generatedLoopInfo->preCond->EvaluateAsBooleanCondition(
                    preCondConstant, getContext()) &&
                !preCondConstant)
              return;

            llvm::SmallVector<bool, 4> emitFinals;
            emitFinals.reserve(generatedLoopInfo->sourceIVs.size());
            for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs) {
              emitFinals.push_back(
                  localDeclMap.count(sourceIV) ||
                  sourceIV->hasGlobalStorage() ||
                  (capturedStmtInfo && capturedStmtInfo->lookup(sourceIV)));
            }

            auto emitGeneratedWsloop = [&]() -> mlir::LogicalResult {
              mlir::Value upperBound =
                  emitOpenMPIntegerExpr(*this, generatedLoopInfo->lastIteration);
              mlir::Type loopTy = upperBound.getType();
              mlir::Value lowerBound =
                  createOpenMPIntegerConstant(*this, begin, loopTy, 0);
              mlir::Value step =
                  createOpenMPIntegerConstant(*this, begin, loopTy, 1);

              auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
              wsloopOp.setNowait(true);
              wsloopOp.setComposite(true);
              OpenMPClauseEmissionInfo wsloopClauseInfo =
                  emitOpenMPClauses(wsloopOp, wsloopClauses,
                                    parallelClauseInfo.privateVars,
                                    /*allowWsloopConditionalLastprivate=*/true);
              llvm::SmallVector<mlir::Type, 4> wsloopPrivateArgTypes;
              llvm::SmallVector<mlir::Location, 4> wsloopPrivateArgLocs;
              for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
                wsloopPrivateArgTypes.push_back(privateVar.getType());
                wsloopPrivateArgLocs.push_back(privateVar.getLoc());
              }
              llvm::SmallVector<mlir::Type, 4> wsloopReductionArgTypes;
              llvm::SmallVector<mlir::Location, 4> wsloopReductionArgLocs;
              for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
                wsloopReductionArgTypes.push_back(reductionVar.getType());
                wsloopReductionArgLocs.push_back(reductionVar.getLoc());
              }
              wsloopPrivateArgTypes.append(wsloopReductionArgTypes);
              wsloopPrivateArgLocs.append(wsloopReductionArgLocs);
              mlir::Block *wsloopBlock = builder.createBlock(
                  &wsloopOp.getRegion(), {}, wsloopPrivateArgTypes,
                  wsloopPrivateArgLocs);
              builder.setInsertionPointToEnd(wsloopBlock);

              mlir::omp::SimdOperands simdClauseOps;
              auto simdOp =
                  mlir::omp::SimdOp::create(builder, begin, simdClauseOps);
              simdOp.setComposite(true);
              {
                mlir::OpBuilder::InsertionGuard guard(builder);
                builder.setInsertionPoint(wsloopOp->getBlock(),
                                          mlir::Block::iterator(wsloopOp));
                if (mlir::failed(emitOpenMPSimdClausesForComposite(
                        *this, simdOp, simdClauses)))
                  return mlir::failure();
              }

              mlir::Block *simdBlock = builder.createBlock(&simdOp.getRegion());
              builder.setInsertionPointToEnd(simdBlock);

              mlir::omp::LoopNestOperands loopClauseOps;
              loopClauseOps.loopLowerBounds.push_back(lowerBound);
              loopClauseOps.loopUpperBounds.push_back(upperBound);
              loopClauseOps.loopSteps.push_back(step);
              loopClauseOps.loopInclusive = builder.getUnitAttr();

              auto loopNestOp =
                  mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
              bool logicalIVSigned = generatedLoopInfo->logicalIV->getType()
                                         ->hasSignedIntegerRepresentation();
              loopNestOp->setAttr(
                  "omp.frontend.iv_signed",
                  builder.getDenseBoolArrayAttr({logicalIVSigned}));
              mlir::Block *loopBlock = builder.createBlock(
                  &loopNestOp.getRegion(), {}, {loopTy},
                  {getLoc(generatedLoopInfo->logicalIV->getLocation())});
              builder.setInsertionPointToEnd(loopBlock);

              auto createLoopVarStorage = [&](const VarDecl *vd) {
                CharUnits vdAlign = getContext().getDeclAlign(vd);
                mlir::Type vdType = convertTypeForMem(vd->getType());
                Address rawAlloca = Address::invalid();
                createTempAlloca(vdType, vdAlign, getLoc(vd->getLocation()),
                                 vd->getName(), /*arraySize=*/nullptr,
                                 &rawAlloca);
                return std::pair{rawAlloca, vdType};
              };

              auto [logicalIVRawAlloca, logicalIVType] =
                  createLoopVarStorage(generatedLoopInfo->logicalIV);
              llvm::SmallVector<Address, 4> sourceIVRawAllocas;
              sourceIVRawAllocas.reserve(generatedLoopInfo->sourceIVs.size());
              for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs) {
                auto [sourceIVRawAlloca, sourceIVType] =
                    createLoopVarStorage(sourceIV);
                (void)sourceIVType;
                sourceIVRawAllocas.push_back(sourceIVRawAlloca);
              }

              mlir::LogicalResult bodyRes = mlir::success();
              {
                llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                    wsloopPrivateDeclGuards;
                RunCleanupsScope wsloopPrivateCleanupScope(*this);
                llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                    sourceIVMapGuards;
                sourceIVMapGuards.reserve(generatedLoopInfo->sourceIVs.size());
                for (auto [sourceIV, sourceIVRawAlloca] :
                     llvm::zip_equal(generatedLoopInfo->sourceIVs,
                                     sourceIVRawAllocas)) {
                  sourceIVMapGuards.push_back(
                      std::make_unique<DeclMapRevertingRAII>(*this, sourceIV));
                  replaceAddrOfLocalVar(sourceIV, sourceIVRawAlloca);
                }
                DeclMapRevertingRAII logicalIVMapGuard(
                    *this, generatedLoopInfo->logicalIV);
                replaceAddrOfLocalVar(generatedLoopInfo->logicalIV,
                                      logicalIVRawAlloca);

                auto wsloopPrivateArgs =
                    mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                        wsloopOp.getOperation())
                        .getPrivateBlockArgs();
                if (mlir::failed(remapOpenMPPrivateDecls(
                        *this, wsloopOp.getOperation(), wsloopClauseInfo,
                        wsloopPrivateDeclGuards, &wsloopOriginalDecls))) {
                  res = mlir::failure();
                  return mlir::failure();
                }
                remapOpenMPReductionDecls(*this, wsloopOp.getOperation(),
                                          wsloopClauseInfo,
                                          reductionDeclGuards);

                mlir::Value logicalIVValue = loopBlock->getArgument(0);
                if (auto ompIVTy =
                        mlir::dyn_cast<mlir::IntegerType>(logicalIVValue.getType())) {
                  auto cirLoopTy = cir::IntType::get(
                      &getMLIRContext(), ompIVTy.getWidth(), logicalIVSigned);
                  logicalIVValue = mlir::UnrealizedConversionCastOp::create(
                                       builder,
                                       getLoc(generatedLoopInfo->logicalIV
                                                  ->getLocation()),
                                       cirLoopTy, logicalIVValue)
                                       .getResult(0);
                  if (cirLoopTy != logicalIVType) {
                    logicalIVValue = builder.createIntCast(
                        logicalIVValue, mlir::cast<cir::IntType>(logicalIVType));
                  }
                } else {
                  logicalIVValue = castOpenMPValueToCIRType(
                      *this, getLoc(generatedLoopInfo->logicalIV->getLocation()),
                      logicalIVValue, logicalIVType);
                }
                builder.createStore(
                    getLoc(generatedLoopInfo->logicalIV->getLocation()),
                    logicalIVValue, logicalIVRawAlloca);
                for (const Expr *update : generatedLoopInfo->updates)
                  emitIgnoredExpr(update);

                bool hasConditionalLastprivate =
                    pushOpenMPLastprivateConditional(
                        wsloopClauses, logicalIVRawAlloca, s.getBeginLoc());
                OpenMPScanParentDirectiveRAII scanDirectiveScope(
                    *this, s, collectOpenMPReductionRegionArgs(wsloopOp));
                bodyRes = emitOMPLoopBody(*this, builder, begin,
                                          generatedLoopInfo->body);
                if (!wsloopClauseInfo.lastprivateIndices.empty()) {
                  assert(wsloopClauseInfo.lastprivateIndices.size() ==
                             wsloopClauseInfo.lastprivateVars.size() &&
                         "mismatched OpenMP wsloop lastprivate metadata");
                  assert(wsloopClauseInfo.lastprivateIndices.size() ==
                             wsloopClauseInfo.lastprivateConditionals.size() &&
                         "mismatched OpenMP wsloop conditional-lastprivate "
                         "metadata");
                  mlir::Location ivLoc =
                      getLoc(generatedLoopInfo->logicalIV->getLocation());
                  auto cirIVTy = mlir::dyn_cast<cir::IntType>(logicalIVType);
                  assert(cirIVTy &&
                         "expected CIR integer type for generated logical IV");
                  mlir::Value upperBoundValue = castOpenMPValueToCIRType(
                      *this, ivLoc, upperBound, logicalIVType);
                  mlir::Value stepValue = builder.getConstInt(ivLoc, cirIVTy, 1);
                  mlir::Value nextIV =
                      builder.createAdd(ivLoc, logicalIVValue, stepValue);
                  mlir::Value isLastIteration = builder.createCompare(
                      ivLoc, cir::CmpOpKind::gt, nextIV, upperBoundValue);

                  cir::IfOp::create(
                      builder, ivLoc, isLastIteration,
                      /*withElseRegion=*/false,
                      [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
                        mlir::OpBuilder::InsertionGuard guard(builder);
                        builder.setInsertionPointToEnd(
                            thenBuilder.getInsertionBlock());
                        for (auto [index, vd, srcVD, destVD, assignOp,
                                   isConditional] :
                             llvm::zip_equal(
                                 wsloopClauseInfo.lastprivateIndices,
                                 wsloopClauseInfo.lastprivateVars,
                                 wsloopClauseInfo.lastprivateSourceVDs,
                                 wsloopClauseInfo.lastprivateDestVDs,
                                 wsloopClauseInfo.lastprivateAssignmentOps,
                                 wsloopClauseInfo.lastprivateConditionals)) {
                          assert(index < wsloopPrivateArgs.size() &&
                                 "unexpected OpenMP wsloop lastprivate block "
                                 "arg index");
                          Address srcAddr =
                              getOpenMPPrivateStorageAddress(*this, vd);
                          const VarDecl *canonicalVD = vd->getCanonicalDecl();
                          Address destAddr = Address::invalid();
                          if (auto parallelIt =
                                  parallelOriginalDecls.find(canonicalVD);
                              parallelIt != parallelOriginalDecls.end()) {
                            destAddr = parallelIt->second;
                          } else if (auto wsloopIt =
                                         wsloopOriginalDecls.find(canonicalVD);
                                     wsloopIt != wsloopOriginalDecls.end()) {
                            destAddr = wsloopIt->second;
                          }
                          assert(destAddr.isValid() &&
                                 "missing lastprivate destination address");
                          if (isConditional)
                            emitOpenMPLastprivateConditionalFinalUpdate(
                                vd, srcAddr, vd->getLocation());
                          emitOpenMPLastprivateCopy(*this, vd, srcAddr, destAddr,
                                                    srcVD, destVD, assignOp);
                        }
                        cir::YieldOp::create(thenBuilder, thenLoc);
                      });
                }
                if (hasConditionalLastprivate)
                  popOpenMPLastprivateConditional();
              }
              mlir::omp::YieldOp::create(builder, end);
              builder.setInsertionPointAfter(wsloopOp);

              reductionDeclGuards.clear();
              parallelPrivateDeclGuards.clear();

              for (auto [idx, final] :
                   llvm::enumerate(generatedLoopInfo->finals))
                if (emitFinals[idx] && final)
                  emitIgnoredExpr(final);

              return bodyRes;
            };

            if (generatedLoopInfo->preCond->EvaluateAsBooleanCondition(
                    preCondConstant, getContext())) {
              res = emitGeneratedWsloop();
              return;
            }

            mlir::Value preCond =
                emitOpOnBoolExpr(begin, generatedLoopInfo->preCond);
            cir::IfOp::create(
                builder, begin, preCond, /*withElseRegion=*/false,
                [&](mlir::OpBuilder &thenBuilder, mlir::Location) {
                  mlir::OpBuilder::InsertionGuard guard(builder);
                  builder.setInsertionPointToEnd(
                      thenBuilder.getInsertionBlock());
                  res = emitGeneratedWsloop();
                  builder.createYield(begin);
                });
            return;
          }

          DeclMapRevertingRAII ivMapGuard(*this, loopInfo->iv);

          mlir::Value lowerBound =
              emitOpenMPIntegerExpr(*this, loopInfo->lowerBound);
          mlir::Value upperBound =
              emitOpenMPIntegerExpr(*this, loopInfo->upperBound);
          mlir::Type loopTy = lowerBound.getType();
          if (loopTy != upperBound.getType()) {
            getCIRGenModule().errorNYI(
                s.getSourceRange(),
                "OpenMP OMPParallelForSimdDirective with mismatched loop "
                "bound types");
            return;
          }
          mlir::Value step =
              createOpenMPIntegerConstant(*this, begin, loopTy, 1);

          auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
          wsloopOp.setNowait(true);
          wsloopOp.setComposite(true);
          OpenMPClauseEmissionInfo wsloopClauseInfo =
              emitOpenMPClauses(wsloopOp, wsloopClauses,
                                parallelClauseInfo.privateVars,
                                /*allowWsloopConditionalLastprivate=*/true);
          llvm::SmallVector<mlir::Type, 4> wsloopPrivateArgTypes;
          llvm::SmallVector<mlir::Location, 4> wsloopPrivateArgLocs;
          for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
            wsloopPrivateArgTypes.push_back(privateVar.getType());
            wsloopPrivateArgLocs.push_back(privateVar.getLoc());
          }
          llvm::SmallVector<mlir::Type, 4> wsloopReductionArgTypes;
          llvm::SmallVector<mlir::Location, 4> wsloopReductionArgLocs;
          for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
            wsloopReductionArgTypes.push_back(reductionVar.getType());
            wsloopReductionArgLocs.push_back(reductionVar.getLoc());
          }
          wsloopPrivateArgTypes.append(wsloopReductionArgTypes);
          wsloopPrivateArgLocs.append(wsloopReductionArgLocs);
          mlir::Block *wsloopBlock = builder.createBlock(
              &wsloopOp.getRegion(), {}, wsloopPrivateArgTypes,
              wsloopPrivateArgLocs);
          builder.setInsertionPointToEnd(wsloopBlock);

          mlir::omp::SimdOperands simdClauseOps;
          auto simdOp = mlir::omp::SimdOp::create(builder, begin, simdClauseOps);
          simdOp.setComposite(true);
          {
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPoint(wsloopOp->getBlock(),
                                      mlir::Block::iterator(wsloopOp));
            if (mlir::failed(
                    emitOpenMPSimdClausesForComposite(*this, simdOp,
                                                      simdClauses))) {
              res = mlir::failure();
              return;
            }
          }
          if (mlir::failed(res))
            return;

          mlir::Block *simdBlock = builder.createBlock(&simdOp.getRegion());
          builder.setInsertionPointToEnd(simdBlock);

          mlir::omp::LoopNestOperands loopClauseOps;
          loopClauseOps.loopLowerBounds.push_back(lowerBound);
          loopClauseOps.loopUpperBounds.push_back(upperBound);
          loopClauseOps.loopSteps.push_back(step);
          if (loopInfo->upperInclusive)
            loopClauseOps.loopInclusive = builder.getUnitAttr();

          auto loopNestOp =
              mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
          loopNestOp->setAttr(
              "omp.frontend.iv_signed",
              builder.getDenseBoolArrayAttr(
                  {loopInfo->iv->getType()->hasSignedIntegerRepresentation()}));
          mlir::Block *loopBlock = builder.createBlock(
              &loopNestOp.getRegion(), {}, {loopTy},
              {getLoc(loopInfo->iv->getLocation())});
          builder.setInsertionPointToEnd(loopBlock);
          llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
              wsloopPrivateDeclGuards;
          auto wsloopPrivateArgs =
              mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                  wsloopOp.getOperation())
                  .getPrivateBlockArgs();
          if (mlir::failed(remapOpenMPPrivateDecls(
                  *this, wsloopOp.getOperation(), wsloopClauseInfo,
                  wsloopPrivateDeclGuards, &wsloopOriginalDecls))) {
            res = mlir::failure();
            return;
          }
          remapOpenMPReductionDecls(*this, wsloopOp.getOperation(),
                                    wsloopClauseInfo, reductionDeclGuards);

          mlir::Value ivValue = castOpenMPValueToCIRType(
              *this, getLoc(loopInfo->iv->getLocation()),
              loopBlock->getArgument(0), ivType);
          auto ivIt = localDeclMap.find(loopInfo->iv);
          assert(ivIt != localDeclMap.end() &&
                 "expected loop IV address to be mapped");
          builder.createStore(getLoc(loopInfo->iv->getLocation()), ivValue,
                              ivIt->second);

          bool hasConditionalLastprivate = pushOpenMPLastprivateConditional(
              wsloopClauses, ivIt->second, s.getBeginLoc());
          OpenMPScanParentDirectiveRAII scanDirectiveScope(
              *this, s, collectOpenMPReductionRegionArgs(wsloopOp));
          res = emitOMPLoopBody(*this, builder, begin,
                                loopInfo->forStmt->getBody());
          if (!wsloopClauseInfo.lastprivateIndices.empty()) {
            assert(wsloopClauseInfo.lastprivateIndices.size() ==
                       wsloopClauseInfo.lastprivateVars.size() &&
                   "mismatched OpenMP wsloop lastprivate metadata");
            assert(wsloopClauseInfo.lastprivateIndices.size() ==
                       wsloopClauseInfo.lastprivateConditionals.size() &&
                   "mismatched OpenMP wsloop conditional-lastprivate "
                   "metadata");
            mlir::Location ivLoc = getLoc(loopInfo->iv->getLocation());
            auto cirIVTy = mlir::dyn_cast<cir::IntType>(ivType);
            assert(cirIVTy && "expected CIR integer type for loop IV");
            mlir::Value upperBoundValue =
                castOpenMPValueToCIRType(*this, ivLoc, upperBound, ivType);
            mlir::Value stepValue = builder.getConstInt(ivLoc, cirIVTy, 1);
            mlir::Value nextIV = builder.createAdd(ivLoc, ivValue, stepValue);
            mlir::Value isLastIteration = builder.createCompare(
                ivLoc, loopInfo->upperInclusive ? cir::CmpOpKind::gt
                                                : cir::CmpOpKind::ge,
                nextIV, upperBoundValue);

            cir::IfOp::create(
                builder, ivLoc, isLastIteration, /*withElseRegion=*/false,
                [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
                  mlir::OpBuilder::InsertionGuard guard(builder);
                  builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
                  for (auto [index, vd, srcVD, destVD, assignOp,
                             isConditional] :
                       llvm::zip_equal(
                           wsloopClauseInfo.lastprivateIndices,
                           wsloopClauseInfo.lastprivateVars,
                           wsloopClauseInfo.lastprivateSourceVDs,
                           wsloopClauseInfo.lastprivateDestVDs,
                           wsloopClauseInfo.lastprivateAssignmentOps,
                           wsloopClauseInfo.lastprivateConditionals)) {
                    assert(index < wsloopPrivateArgs.size() &&
                           "unexpected OpenMP wsloop lastprivate block arg "
                           "index");
                    Address srcAddr = getOpenMPPrivateStorageAddress(*this, vd);
                    const VarDecl *canonicalVD = vd->getCanonicalDecl();
                    Address destAddr = Address::invalid();
                    if (auto parallelIt =
                            parallelOriginalDecls.find(canonicalVD);
                        parallelIt != parallelOriginalDecls.end()) {
                      destAddr = parallelIt->second;
                    } else if (auto wsloopIt =
                                   wsloopOriginalDecls.find(canonicalVD);
                               wsloopIt != wsloopOriginalDecls.end()) {
                      destAddr = wsloopIt->second;
                    }
                    assert(destAddr.isValid() &&
                           "missing lastprivate destination address");
                    if (isConditional)
                      emitOpenMPLastprivateConditionalFinalUpdate(
                          vd, srcAddr, vd->getLocation());
                    emitOpenMPLastprivateCopy(*this, vd, srcAddr, destAddr,
                                             srcVD, destVD, assignOp);
                  }
                  cir::YieldOp::create(thenBuilder, thenLoc);
                });
          }
          if (hasConditionalLastprivate)
            popOpenMPLastprivateConditional();
          mlir::omp::YieldOp::create(builder, end);
          builder.setInsertionPointAfter(wsloopOp);

          reductionDeclGuards.clear();
        });

    mlir::omp::TerminatorOp::create(builder, end);
  }
  if (mlir::succeeded(res))
    checkForOpenMPLastprivateConditionalUpdate(*this, s);
  return res;
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelMasterDirective(
    const OMPParallelMasterDirective &s) {
  return emitSimpleOMPParallelDirective(
      s, "OpenMP OMPParallelMasterDirective",
      /*wrapInMaster=*/true,
      /*wrapInMasked=*/false);
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelSectionsDirective(
    const OMPParallelSectionsDirective &s) {
  llvm::SmallVector<const OMPClause *, 8> parallelClauses;
  llvm::SmallVector<const OMPClause *, 8> sectionsClauses;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if:
    case llvm::omp::OMPC_num_threads:
    case llvm::omp::OMPC_severity:
    case llvm::omp::OMPC_message:
    case llvm::omp::OMPC_proc_bind:
    case llvm::omp::OMPC_firstprivate:
    case llvm::omp::OMPC_copyin:
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_private:
    case llvm::omp::OMPC_lastprivate:
    case llvm::omp::OMPC_reduction:
      sectionsClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_default:
    case llvm::omp::OMPC_shared:
      break;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), "OpenMP parallel sections clause",
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      break;
    }
  }
  llvm::SmallVector<OpenMPCopyinMasterAddr, 4> copyinMasterAddrs =
      collectOpenMPCopyinMasterAddresses(*this, s);

  mlir::LogicalResult res = mlir::success();
  llvm::SmallVector<mlir::Type> retTy;
  llvm::SmallVector<mlir::Value> operands;
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  auto parallelOp =
      mlir::omp::ParallelOp::create(builder, begin, retTy, operands);

  OpenMPClauseEmissionInfo parallelClauseInfo =
      emitOpenMPClauses(parallelOp, parallelClauses);
  const CapturedStmt *parallelCaptured =
      s.getCapturedStmt(OpenMPDirectiveKind::OMPD_parallel);
  assert(parallelCaptured && "expected OpenMP parallel captured stmt");
  parallelOp->setAttr("cir.instrprof_func_name",
                      cgm.getStringAttrCached(
                          getOpenMPHelperPGOName(*parallelCaptured)));
  parallelOp->setAttr("cir.instrprof_func_linkage",
                      builder.getI32IntegerAttr(static_cast<int32_t>(
                          llvm::GlobalValue::InternalLinkage)));

  {
    mlir::OpBuilder::InsertionGuard guardCase(builder);

    llvm::SmallVector<mlir::Type, 4> privateArgTypes;
    llvm::SmallVector<mlir::Location, 4> privateArgLocs;
    for (mlir::Value privateVar : parallelOp.getPrivateVars()) {
      privateArgTypes.push_back(privateVar.getType());
      privateArgLocs.push_back(privateVar.getLoc());
    }

    mlir::Block *block = builder.createBlock(&parallelOp.getRegion(), {},
                                             privateArgTypes, privateArgLocs);
    builder.setInsertionPointToEnd(block);

    cir::ScopeOp::create(
        builder, begin, /*scopeBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          auto pgoScope = enterOpenMPHelperRegionCounters(*parallelCaptured);
          LexicalScope ls{*this, begin, builder.getInsertionBlock()};
          llvm::SmallVector<std::pair<const VarDecl *, Address>, 4> savedDecls;
          auto privateArgs =
              mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                  parallelOp.getOperation())
                  .getPrivateBlockArgs();
          assert(parallelClauseInfo.privateIndices.size() ==
                     parallelClauseInfo.privateVars.size() &&
                 "mismatched OpenMP parallel private metadata");
          for (auto [index, vd] :
               llvm::zip_equal(parallelClauseInfo.privateIndices,
                               parallelClauseInfo.privateVars)) {
            assert(index < privateArgs.size() &&
                   "unexpected OpenMP parallel private block arg index");
            mlir::Value privateArg = privateArgs[index];
            auto ptrTy = mlir::cast<cir::PointerType>(privateArg.getType());
            auto it = localDeclMap.find(vd);
            if (it != localDeclMap.end())
              savedDecls.emplace_back(vd, it->second);
            replaceAddrOfLocalVar(
                vd, Address(privateArg, ptrTy.getPointee(),
                            getContext().getDeclAlign(vd)));
          }

          OpenMPTLSCopyinDeclMapScope tlsCopyinDecls(*this, s);
          (void)emitOpenMPCopyinClause(*this, s, copyinMasterAddrs);
          bool hasDisabledLastprivateConditional =
              pushDisabledOpenMPLastprivateConditional(s);
          auto popDisabledLastprivateConditional = llvm::scope_exit([&] {
            if (hasDisabledLastprivateConditional)
              popOpenMPLastprivateConditional();
          });
          incrementProfileCounter(parallelCaptured->getCapturedDecl()->getBody());
          const Stmt *body =
              s.getCapturedStmt(OpenMPDirectiveKind::OMPD_parallel)
                  ->getCapturedStmt();

          if (llvm::any_of(sectionsClauses, [](const OMPClause *clause) {
                return clause->getClauseKind() == llvm::omp::OMPC_lastprivate;
              })) {
            res = emitOMPSectionsWithWsloop(
                sectionsClauses, body, parallelClauseInfo.privateVars,
                s.getBeginLoc(), s.getEndLoc(),
                /*nowait=*/true,
                /*emitNowaitLastprivateBarrier=*/false);
            for (auto [vd, addr] : llvm::reverse(savedDecls))
              replaceAddrOfLocalVar(vd, addr);
            return;
          }

          // This is the combined 'parallel sections' construct, so the
          // enclosing parallel region already provides the end-of-construct
          // synchronization.  Mark the nested sections as nowait to avoid
          // materializing an extra __kmpc_barrier / using the blocking
          // __kmpc_reduce variant, matching OGCG.
          auto sectionsOp = mlir::omp::SectionsOp::create(
              builder, begin,
              /*allocate_vars=*/mlir::ValueRange{},
              /*allocator_vars=*/mlir::ValueRange{},
              /*nowait=*/true,
              /*private_vars=*/mlir::ValueRange{},
              /*private_syms=*/mlir::ArrayAttr{},
              /*private_needs_barrier=*/false,
              /*reduction_mod=*/mlir::omp::ReductionModifierAttr{},
              /*reduction_vars=*/mlir::ValueRange{},
              /*reduction_byref=*/mlir::DenseBoolArrayAttr{},
              /*reduction_syms=*/mlir::ArrayAttr{});
          OpenMPClauseEmissionInfo sectionsClauseInfo =
              emitOpenMPClauses(sectionsOp, sectionsClauses);

          {
            mlir::OpBuilder::InsertionGuard guard(builder);

            // Collect block arg types for the sections region: private
            // args first, then reduction args, matching the
            // BlockArgOpenMPOpInterface order.
            llvm::SmallVector<mlir::Type, 4> sectionsArgTypes;
            llvm::SmallVector<mlir::Location, 4> sectionsArgLocs;
            for (mlir::Value v : sectionsOp.getPrivateVars()) {
              sectionsArgTypes.push_back(v.getType());
              sectionsArgLocs.push_back(v.getLoc());
            }
            for (mlir::Value v : sectionsOp.getReductionVars()) {
              sectionsArgTypes.push_back(v.getType());
              sectionsArgLocs.push_back(v.getLoc());
            }

            mlir::Block *sectionsBlock = builder.createBlock(
                &sectionsOp.getRegion(), {}, sectionsArgTypes,
                sectionsArgLocs);
            builder.setInsertionPointToEnd(sectionsBlock);

            // Emit each omp.section with matching block args and
            // reduction variable remapping.
            llvm::SmallVector<OpenMPSectionBody, 4> sectionBodies;
            collectOpenMPSectionBodies(body, sectionBodies);

            auto emitSingleSection = [&](ArrayRef<const Stmt *> sectionBody) {
              auto sectionOp =
                  mlir::omp::SectionOp::create(builder, begin);
              mlir::OpBuilder::InsertionGuard sectionGuard(builder);
              mlir::Block *sectionBlock = builder.createBlock(
                  &sectionOp.getRegion(), {}, sectionsArgTypes,
                  sectionsArgLocs);
              builder.setInsertionPointToEnd(sectionBlock);

              llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                  sectionDeclGuards;
              if (mlir::failed(remapOpenMPPrivateDecls(
                      *this, sectionOp.getOperation(), sectionsClauseInfo,
                      sectionDeclGuards))) {
                res = mlir::failure();
                return;
              }
              remapOpenMPReductionDecls(*this, sectionOp.getOperation(),
                                        sectionsClauseInfo,
                                        sectionDeclGuards);

              res = emitOpenMPSectionBody(*this, sectionBody);
              sectionDeclGuards.clear();
              mlir::omp::TerminatorOp::create(builder, end);
            };

            for (const OpenMPSectionBody &sectionBody : sectionBodies) {
              emitSingleSection(sectionBody.stmts);
              if (mlir::failed(res))
                break;
            }

            mlir::omp::TerminatorOp::create(builder, end);
          }

          for (auto [vd, addr] : llvm::reverse(savedDecls))
            replaceAddrOfLocalVar(vd, addr);
        });

    mlir::omp::TerminatorOp::create(builder, end);
  }

  if (mlir::succeeded(res))
    checkForOpenMPLastprivateConditionalUpdate(*this, s);
  return res;
}
mlir::LogicalResult
CIRGenFunction::emitOMPTaskDirective(const OMPTaskDirective &s) {
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  auto taskOp = mlir::omp::TaskOp::create(
      builder, begin,
      /*allocate_vars=*/mlir::ValueRange{},
      /*allocator_vars=*/mlir::ValueRange{},
      /*depend_kinds=*/mlir::ArrayAttr{},
      /*depend_vars=*/mlir::ValueRange{},
      /*depend_sizes=*/mlir::ValueRange{},
      /*final=*/mlir::Value{},
      /*if_expr=*/mlir::Value{},
      /*in_reduction_vars=*/mlir::ValueRange{},
      /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
      /*in_reduction_syms=*/mlir::ArrayAttr{},
      /*mergeable=*/false,
      /*priority=*/mlir::Value{},
      /*private_vars=*/mlir::ValueRange{},
      /*private_syms=*/mlir::ArrayAttr{},
      /*private_needs_barrier=*/false,
      /*untied=*/false,
      /*event_handle=*/mlir::Value{});
  OpenMPClauseEmissionInfo clauseInfo = emitOpenMPClauses(taskOp, s.clauses());
  if (mlir::failed(
          maybeEmitOpenMPRuntimeDependPayload(*this, taskOp, s, clauseInfo)))
    return mlir::failure();

  return emitOMPPrivateRegionBody(
      *this, taskOp, clauseInfo, begin, end, [&]() -> mlir::LogicalResult {
        return emitStmt(s.getInnermostCapturedStmt()->getCapturedStmt(),
                        /*useCurrentScope=*/true);
      });
}
mlir::LogicalResult
CIRGenFunction::emitOMPTaskgroupDirective(const OMPTaskgroupDirective &s) {
  mlir::LogicalResult res = mlir::success();
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  auto taskgroupOp = mlir::omp::TaskgroupOp::create(
      builder, begin,
      /*allocate_vars=*/mlir::ValueRange{},
      /*allocator_vars=*/mlir::ValueRange{},
      /*task_reduction_vars=*/mlir::ValueRange{},
      /*task_reduction_byref=*/mlir::DenseBoolArrayAttr{},
      /*task_reduction_syms=*/mlir::ArrayAttr{});
  OpenMPClauseEmissionInfo clauseInfo =
      emitOpenMPClauses(taskgroupOp, s.clauses());

  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    llvm::SmallVector<mlir::Type, 4> reductionArgTypes;
    llvm::SmallVector<mlir::Location, 4> reductionArgLocs;
    for (mlir::Value reductionVar : taskgroupOp.getTaskReductionVars()) {
      reductionArgTypes.push_back(reductionVar.getType());
      reductionArgLocs.push_back(reductionVar.getLoc());
    }

    mlir::Block *block = builder.createBlock(&taskgroupOp.getRegion(), {},
                                             reductionArgTypes,
                                             reductionArgLocs);
    builder.setInsertionPointToEnd(block);
    llvm::SmallVector<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>, 4>
        reductionDeclGuards;
    llvm::SmallVector<std::unique_ptr<OpenMPTaskReductionVLASizeScope>, 4>
        reductionVLAScopes;
    remapOpenMPTaskReductionDecls(*this, taskgroupOp.getOperation(), clauseInfo,
                                  reductionDeclGuards,
                                  taskgroupOp.getTaskReductionSyms(),
                                  &reductionVLAScopes);
    emitOpenMPTaskReductionArraySectionRuntimeSizeScopes(
        *this, taskgroupOp.getOperation(), clauseInfo.taskReductionRuntimeSizes,
        taskgroupOp.getTaskReductionSyms(), reductionVLAScopes);
    res = emitStmt(s.getInnermostCapturedStmt()->getCapturedStmt(),
                   /*useCurrentScope=*/true);
    mlir::omp::TerminatorOp::create(builder, end);
  }

  return res;
}
mlir::LogicalResult
CIRGenFunction::emitOMPFlushDirective(const OMPFlushDirective &s) {
  mlir::omp::FlushOp::create(builder, getLoc(s.getSourceRange()),
                             /*varList=*/mlir::ValueRange{});
  return mlir::success();
}
mlir::LogicalResult
CIRGenFunction::emitOMPDepobjDirective(const OMPDepobjDirective &s) {
  if (getLangOpts().OpenMPSimd)
    return mlir::success();

  const auto *depobjClause = s.getSingleClause<OMPDepobjClause>();
  LValue depobjLValue = emitLValue(depobjClause->getDepobj());

  if (const auto *dependClause = s.getSingleClause<OMPDependClause>()) {
    for (const OpenMPDepobjDependData &depData :
         buildOpenMPDepobjDependences(s)) {
      std::optional<Address> depAddr =
          emitOpenMPDepobjDependClause(*this, depData,
                                       dependClause->getBeginLoc());
      if (!depAddr)
        return mlir::failure();
      mlir::Value storedPtr = builder.createBitcast(
          getLoc(dependClause->getBeginLoc()), depAddr->getPointer(),
          depobjLValue.getAddress().getElementType());
      emitStoreOfScalar(storedPtr, depobjLValue, /*isInit=*/false);
    }
    return mlir::success();
  }

  if (const auto *destroyClause = s.getSingleClause<OMPDestroyClause>())
    return emitOpenMPDepobjDestroyClause(*this, depobjLValue,
                                         destroyClause->getBeginLoc());

  if (const auto *updateClause = s.getSingleClause<OMPUpdateClause>())
    return emitOpenMPDepobjUpdateClause(*this, depobjLValue,
                                        updateClause->getDependencyKind(),
                                        updateClause->getBeginLoc());

  return mlir::success();
}

// Clause remapping can emit setup ops (e.g. the private base of an
// array-section reduction) while the insertion point is in a loop wrapper,
// which may only hold its nested wrapper or omp.loop_nest. Move the ops that
// use the wrappers' block arguments to the start of the loop body and the rest
// in front of the outermost wrapper, where they run once.
static void sinkOpenMPLoopWrapperSetup(mlir::omp::LoopNestOp loopNest) {
  llvm::SmallVector<mlir::Operation *> setup;
  mlir::Operation *outermost = loopNest;
  for (mlir::Operation *wrapper = loopNest->getParentOp();
       wrapper && mlir::isa<mlir::omp::LoopWrapperInterface>(wrapper);
       wrapper = wrapper->getParentOp()) {
    outermost = wrapper;
    llvm::SmallVector<mlir::Operation *> ops;
    for (mlir::Operation &op : wrapper->getRegion(0).front())
      if (!op.hasTrait<mlir::OpTrait::IsTerminator>() &&
          !mlir::isa<mlir::omp::LoopWrapperInterface, mlir::omp::LoopNestOp>(
              op))
        ops.push_back(&op);
    setup.insert(setup.begin(), ops.begin(), ops.end());
  }
  if (setup.empty())
    return;

  llvm::SmallPtrSet<mlir::Operation *, 8> inBody;
  auto needsBody = [&](mlir::Operation *op) {
    for (mlir::Value operand : op->getOperands()) {
      if (auto arg = mlir::dyn_cast<mlir::BlockArgument>(operand)) {
        if (outermost->isAncestor(arg.getOwner()->getParentOp()))
          return true;
      } else if (inBody.contains(operand.getDefiningOp())) {
        return true;
      }
    }
    return false;
  };
  mlir::Block &body = loopNest.getRegion().front();
  mlir::Block::iterator bodyPos = body.begin();
  for (mlir::Operation *op : setup) {
    if (needsBody(op)) {
      inBody.insert(op);
      op->moveBefore(&body, bodyPos);
    } else {
      op->moveBefore(outermost);
    }
  }
}

// In an inscan loop, code after the scan reaches the body's inner blocks
// without passing the body entry (e.g. through the back edge of the
// do-while(false) that wraps the body), so a value computed at the body entry
// does not dominate its uses there. Recompute pure entry values at each use.
static void localizeOpenMPLoopSetup(mlir::omp::LoopNestOp loopNest) {
  mlir::Block &entry = loopNest.getRegion().front();
  llvm::SmallVector<mlir::Operation *> candidates;
  for (mlir::Operation &op : entry)
    if (mlir::isPure(&op) && op.getNumResults() == 1 &&
        !op.hasTrait<mlir::OpTrait::IsTerminator>())
      candidates.push_back(&op);
  for (mlir::Operation *op : llvm::reverse(candidates)) {
    mlir::Value result = op->getResult(0);
    for (mlir::OpOperand &use : llvm::make_early_inc_range(result.getUses())) {
      mlir::Operation *user = use.getOwner();
      if (user->getBlock() == &entry)
        continue;
      mlir::OpBuilder userBuilder(user);
      use.set(userBuilder.clone(*op)->getResult(0));
    }
    if (result.use_empty())
      op->erase();
  }
}

// The scan phase of an inscan loop enters the body at the scan directive,
// past the body-entry setup in front of the do-while that holds the loop body:
// the code that materializes the loop variables from the omp.loop_nest IVs and
// the values that clause remapping computed. Repeat that setup after the scan
// op and redirect later uses to the copies. This runs once the function body
// is built: loop bodies are emitted inside region builders, so the enclosing
// omp.loop_nest is not reachable while the scan is emitted.
static void rematerializeOpenMPLoopSetupAfterScan(CIRGenBuilderTy &builder,
                                                  mlir::DominanceInfo &domInfo,
                                                  mlir::Operation *scanOp) {
  auto loopNest = scanOp->getParentOfType<mlir::omp::LoopNestOp>();
  if (!loopNest)
    return;
  mlir::Region &body = loopNest.getRegion();
  mlir::Block &entry = body.front();
  mlir::Operation *scanAnchor = entry.findAncestorOpInBlock(*scanOp);

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointAfter(scanOp);
  mlir::IRMapping mapping;
  auto isAvailable = [&](mlir::Value v) {
    if (!body.isAncestor(v.getParentRegion()))
      return true;
    if (auto arg = mlir::dyn_cast<mlir::BlockArgument>(v))
      return arg.getOwner() == &entry;
    return mapping.contains(v);
  };
  for (mlir::Operation &op : entry) {
    if (&op == scanAnchor)
      break;
    if ((!mlir::isPure(&op) && !mlir::isa<cir::LoadOp, cir::StoreOp>(op)) ||
        !llvm::all_of(op.getOperands(), isAvailable))
      continue;
    mlir::Operation *copy = builder.clone(op, mapping);
    for (auto [orig, repl] : llvm::zip(op.getResults(), copy->getResults()))
      orig.replaceUsesWithIf(repl, [&](mlir::OpOperand &use) {
        return domInfo.properlyDominates(copy, use.getOwner());
      });
  }
}

mlir::LogicalResult
CIRGenFunction::emitOMPScanDirective(const OMPScanDirective &s) {
  mlir::omp::ScanOperands scanClauseOps;
  llvm::SmallVector<const Expr *, 4> parentReductionExprs =
      openMPParentLoopDirectiveForScan
          ? collectOpenMPInscanReductionExprs(*openMPParentLoopDirectiveForScan)
          : llvm::SmallVector<const Expr *, 4>{};
  llvm::SmallVector<mlir::Value, 4> reductionArgs =
      !openMPScanReductionArgsStack.empty()
          ? openMPScanReductionArgsStack.back()
          : llvm::SmallVector<mlir::Value, 4>{};
  if (reductionArgs.empty() && !parentReductionExprs.empty())
    reductionArgs =
        collectCurrentOpenMPScanReductionArgs(*this, parentReductionExprs.size());
  const bool incompleteReductionState =
      !parentReductionExprs.empty() &&
      reductionArgs.size() < parentReductionExprs.size();

  auto findMappedReductionArg = [&](const Expr *scanExpr) -> mlir::Value {
    unsigned numCandidates =
        std::min(parentReductionExprs.size(), reductionArgs.size());
    for (unsigned i = 0; i < numCandidates; ++i)
      if (areOpenMPExprsSemanticallyEqual(getContext(), scanExpr,
                                          parentReductionExprs[i]))
        return reductionArgs[i];
    return {};
  };

  auto emitScanOperand =
      [&](const Expr *varExpr,
          llvm::SmallVectorImpl<mlir::Value> &clauseOperands)
      -> mlir::LogicalResult {
    if (mlir::Value reductionArg = findMappedReductionArg(varExpr)) {
      clauseOperands.push_back(reductionArg);
      return mlir::success();
    }

    if (isa<ArraySectionExpr>(varExpr->IgnoreParenImpCasts())) {
      getCIRGenModule().errorNYI(varExpr->getSourceRange(),
                                 incompleteReductionState
                                     ? "OpenMP scan ArraySectionExpr missing "
                                       "reduction argument mapping"
                                     : "OpenMP scan ArraySectionExpr");
      return mlir::failure();
    }

    clauseOperands.push_back(emitLValue(varExpr).getPointer());
    return mlir::success();
  };

  if (const auto *inclusive = s.getSingleClause<OMPInclusiveClause>()) {
    for (const Expr *varExpr : inclusive->varlist()) {
      if (mlir::failed(emitScanOperand(varExpr, scanClauseOps.inclusiveVars)))
        return mlir::failure();
    }
  } else if (const auto *exclusive = s.getSingleClause<OMPExclusiveClause>()) {
    for (const Expr *varExpr : exclusive->varlist()) {
      if (mlir::failed(emitScanOperand(varExpr, scanClauseOps.exclusiveVars)))
        return mlir::failure();
    }
  } else {
    llvm_unreachable("expected inclusive or exclusive omp scan clause");
  }

  if (scanClauseOps.inclusiveVars.empty() && scanClauseOps.exclusiveVars.empty())
    return mlir::success();

  mlir::omp::ScanOp::create(builder, getLoc(s.getSourceRange()), scanClauseOps);
  return mlir::success();
}

void CIRGenFunction::finishOpenMPLoopBodies() {
  if (!curFn)
    return;
  curFn->walk([](mlir::omp::LoopNestOp loopNest) {
    sinkOpenMPLoopWrapperSetup(loopNest);
  });
  llvm::SmallVector<mlir::omp::ScanOp> scanOps;
  curFn->walk([&](mlir::omp::ScanOp scanOp) { scanOps.push_back(scanOp); });
  if (scanOps.empty())
    return;
  llvm::SmallPtrSet<mlir::Operation *, 4> scanLoops;
  for (mlir::omp::ScanOp scanOp : scanOps)
    if (auto loopNest = scanOp->getParentOfType<mlir::omp::LoopNestOp>())
      if (scanLoops.insert(loopNest).second)
        localizeOpenMPLoopSetup(loopNest);
  mlir::DominanceInfo domInfo(curFn);
  for (mlir::omp::ScanOp scanOp : scanOps)
    rematerializeOpenMPLoopSetupAfterScan(builder, domInfo, scanOp);
}

mlir::LogicalResult
CIRGenFunction::emitOMPOrderedDirective(const OMPOrderedDirective &s) {
  if (s.hasClausesOfKind<OMPDependClause>() ||
      s.hasClausesOfKind<OMPDoacrossClause>()) {
    assert(!s.hasAssociatedStmt() &&
           "No associated statement must be in ordered depend/doacross "
           "construct.");

    mlir::Location begin = getLoc(s.getBeginLoc());
    for (const auto *clause : s.getClausesOfKind<OMPDependClause>())
      emitOpenMPDoacrossOrderedOp(*this, begin, clause);
    for (const auto *clause : s.getClausesOfKind<OMPDoacrossClause>())
      emitOpenMPDoacrossOrderedOp(*this, begin, clause);
    return mlir::success();
  }

  bool parLevelSimd = false;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_simd:
      parLevelSimd = true;
      break;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), "OpenMP ordered clause",
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      break;
    }
  }

  mlir::LogicalResult res = mlir::success();
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  auto orderedOp = mlir::omp::OrderedRegionOp::create(
      builder, begin, /*par_level_simd=*/parLevelSimd);

  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *block = builder.createBlock(&orderedOp.getRegion());
    builder.setInsertionPointToEnd(block);
    res = emitStmt(s.getInnermostCapturedStmt()->getCapturedStmt(),
                   /*useCurrentScope=*/true);
    mlir::omp::TerminatorOp::create(builder, end);
  }

  return res;
}
mlir::LogicalResult
CIRGenFunction::emitOMPAtomicDirective(const OMPAtomicDirective &s) {
  OpenMPClauseKind kind = getOpenMPAtomicDirectiveKind(*this, s);
  llvm::AtomicOrdering atomicOrdering =
      getOpenMPAtomicOrdering(*this, s, kind);
  llvm::AtomicOrdering failOrdering = getOpenMPAtomicFailOrdering(s);
  auto memoryOrderAttr =
      getOpenMPAtomicMemoryOrderAttr(*this, atomicOrdering);
  mlir::Location start = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  LexicalScope scope(*this, getLoc(s.getSourceRange()),
                     builder.getInsertionBlock());
  scope.setCleanupOnly();
  emitStopPoint(s.getAssociatedStmt());

  LValue xlv = emitLValue(s.getX());
  if (xlv.isGlobalReg())
    return emitOpenMPAtomicDirectiveWithGlobalRegX(*this, s, kind,
                                                   atomicOrdering, start);

  bool vIsGlobalReg = s.getV() && emitLValue(s.getV()).isGlobalReg();

  switch (kind) {
  case llvm::omp::OMPC_read:
    if (vIsGlobalReg || xlv.isVectorElt() || xlv.isExtVectorElt() ||
        xlv.isBitField()) {
      RValue loaded = emitOpenMPAtomicSimpleLoad(
          *this, atomicOrdering, s.getX(), xlv, s.getX()->getExprLoc());
      emitOpenMPFlushAfterAtomicRead(*this, start, atomicOrdering);
      if (mlir::failed(emitOpenMPAtomicSimpleStore(
              *this, s.getV(), loaded,
              s.getX()->getType().getNonReferenceType(),
              s.getV()->getExprLoc())))
        return mlir::failure();
      checkAndEmitOpenMPLastprivateConditional(s.getV());
      return mlir::success();
    }
    emitOpenMPAtomicRead(*this, builder, start, s.getX(), s.getV(),
                         memoryOrderAttr);
    checkAndEmitOpenMPLastprivateConditional(s.getV());
    return mlir::success();
  case llvm::omp::OMPC_write:
    if (xlv.isBitField()) {
      // Bitfield atomic write: use an omp.atomic.update on the storage integer.
      // The update region takes the old storage, sets the bitfield to the new
      // value, and yields the updated storage.
      const CIRGenBitFieldInfo &info = xlv.getBitFieldInfo();
      Address storageAddr = xlv.getBitFieldAddress();
      QualType storageTy =
          getContext().getIntTypeForBitwidth(info.storageSize, /*Signed=*/false);
      mlir::Value storagePtr = storageAddr.getPointer();
      mlir::Type storageCIRTy = convertType(storageTy);

      // Evaluate the expression to write.
      RValue exprValue = emitAnyExpr(s.getExpr());
      QualType xTy = s.getX()->getType().getNonReferenceType();
      mlir::Value newVal = exprValue.getValue();
      if (!getContext().hasSameType(s.getExpr()->getType(), xTy))
        newVal = emitScalarConversion(newVal, s.getExpr()->getType(), xTy,
                                      s.getExpr()->getExprLoc());

      // Build omp.atomic.update on the storage integer.
      mlir::IntegerAttr noHint;
      auto op = mlir::omp::AtomicUpdateOp::create(
          builder, start, storagePtr, /*atomic_control=*/{}, noHint,
          memoryOrderAttr);

      {
        mlir::OpBuilder::InsertionGuard guard(builder);
        auto *block = builder.createBlock(
            &op.getRegion(), op.getRegion().end(), {storageCIRTy},
            SmallVector<mlir::Location, 1>{start});
        builder.setInsertionPointToEnd(block);

        // Store old storage to temp, set bitfield, load updated storage.
        Address tempAddr = createMemTemp(storageTy, start, "atomic.bf.temp");
        builder.createStore(start, block->getArgument(0), tempAddr);
        LValue tempBFLV = LValue::makeBitfield(tempAddr, info, xlv.getType(),
                                                xlv.getBaseInfo(),
                                                xlv.getTBAAInfo());
        emitStoreThroughBitfieldLValue(RValue::get(newVal), tempBFLV);
        mlir::Value updatedStorage = builder.createLoad(start, tempAddr);
        mlir::omp::YieldOp::create(builder, end, updatedStorage);
      }

      builder.setInsertionPointAfter(op);
      emitOpenMPFlushAfterAtomicWrite(*this, start, atomicOrdering);
      checkAndEmitOpenMPLastprivateConditional(s.getX());
      return mlir::success();
    }
    if (isOpenMPAtomicVectorComponentLValue(xlv)) {
      // Vector element atomic write: use omp.atomic.update on the whole vector.
      // The update region inserts the new element value into the old vector.
      mlir::Value xPtr = xlv.getPointer();
      mlir::Type pointeeTy =
          mlir::cast<cir::PointerType>(xPtr.getType()).getPointee();
      QualType xTy = s.getX()->getType().getNonReferenceType();

      // Evaluate the expression before building the update region.
      RValue exprValue = emitAnyExpr(s.getExpr());
      mlir::Value newVal = exprValue.getValue();
      if (!getContext().hasSameType(s.getExpr()->getType(), xTy))
        newVal = emitScalarConversion(newVal, s.getExpr()->getType(), xTy,
                                      s.getExpr()->getExprLoc());

      mlir::IntegerAttr noHint;
      auto op = mlir::omp::AtomicUpdateOp::create(
          builder, start, xPtr, /*atomic_control=*/{}, noHint,
          memoryOrderAttr);

      {
        mlir::OpBuilder::InsertionGuard guard(builder);
        auto *block = builder.createBlock(
            &op.getRegion(), op.getRegion().end(), {pointeeTy},
            SmallVector<mlir::Location, 1>{start});
        builder.setInsertionPointToEnd(block);

        mlir::Value yielded = setOpenMPAtomicVectorComponentValue(
            *this, xlv, block->getArgument(0), newVal,
            s.getX()->getSourceRange(), s.getExpr()->getExprLoc());
        if (!yielded)
          return mlir::failure();
        mlir::omp::YieldOp::create(builder, end, yielded);
      }

      builder.setInsertionPointAfter(op);
      emitOpenMPFlushAfterAtomicWrite(*this, start, atomicOrdering);
      checkAndEmitOpenMPLastprivateConditional(s.getX());
      return mlir::success();
    }
    if (getEvaluationKind(s.getExpr()->getType()) != cir::TEK_Scalar) {
      // Non-scalar atomic writes (e.g. complex types): bypass omp.atomic.write
      // which only supports scalar types. Use emitAtomicStore directly, which
      // handles complex types via integer-casting or __atomic_store libcalls.
      RValue exprValue = emitAnyExpr(s.getExpr());
      cir::MemOrder order = getCIRAtomicMemoryOrder(*this, atomicOrdering);
      emitAtomicStore(exprValue, xlv, order, xlv.isVolatileQualified(), false);
      emitOpenMPFlushAfterAtomicWrite(*this, start, atomicOrdering);
      checkAndEmitOpenMPLastprivateConditional(s.getX());
      return mlir::success();
    }
    emitOpenMPAtomicWrite(*this, builder, start, s.getX(), s.getExpr(),
                          memoryOrderAttr);
    checkAndEmitOpenMPLastprivateConditional(s.getX());
    return mlir::success();
  case llvm::omp::OMPC_unknown:
  case llvm::omp::OMPC_update: {
    if (xlv.isBitField()) {
      // Bitfield atomic update: use omp.atomic.update on the storage integer.
      // The update region extracts the bitfield, applies the update expression,
      // sets the bitfield back, and yields the updated storage.
      const CIRGenBitFieldInfo &info = xlv.getBitFieldInfo();
      Address storageAddr = xlv.getBitFieldAddress();
      QualType storageTy =
          getContext().getIntTypeForBitwidth(info.storageSize, /*Signed=*/false);
      mlir::Value storagePtr = storageAddr.getPointer();
      mlir::Type storageCIRTy = convertType(storageTy);

      mlir::IntegerAttr noHint;
      auto op = mlir::omp::AtomicUpdateOp::create(
          builder, start, storagePtr, /*atomic_control=*/{}, noHint,
          memoryOrderAttr);

      builder.setInsertionPoint(op);
      RValue exprValue = emitOpenMPAtomicValueExpr(*this, s.getExpr());
      if (exprValue.isIgnored())
        return mlir::failure();

      {
        mlir::OpBuilder::InsertionGuard guard(builder);
        auto *block = builder.createBlock(
            &op.getRegion(), op.getRegion().end(), {storageCIRTy},
            SmallVector<mlir::Location, 1>{start});
        builder.setInsertionPointToEnd(block);

        // Extract old bitfield value from old storage.
        Address tempAddr = createMemTemp(storageTy, start, "atomic.bf.temp");
        builder.createStore(start, block->getArgument(0), tempAddr);
        LValue tempBFLV = LValue::makeBitfield(tempAddr, info, xlv.getType(),
                                                xlv.getBaseInfo(),
                                                xlv.getTBAAInfo());
        RValue oldBFVal = emitLoadOfBitfieldLValue(tempBFLV,
                                                    s.getX()->getExprLoc());

        // Compute updated bitfield value using the update expression.
        RValue updateResult;
        if (mlir::failed(buildOpenMPAtomicUpdateValue(
                *this, s.getX(), s.getExpr(), s.getUpdateExpr(),
                s.isXLHSInRHSPart(), oldBFVal, exprValue, updateResult)))
          return mlir::failure();

        // Set the new bitfield value into a copy of the old storage.
        Address desiredAddr = createMemTemp(storageTy, start, "atomic.bf.desired");
        builder.createStore(start, block->getArgument(0), desiredAddr);
        LValue desiredBFLV = LValue::makeBitfield(desiredAddr, info, xlv.getType(),
                                                   xlv.getBaseInfo(),
                                                   xlv.getTBAAInfo());
        emitStoreThroughBitfieldLValue(RValue::get(updateResult.getValue()),
                                       desiredBFLV);
        mlir::Value updatedStorage = builder.createLoad(start, desiredAddr);
        mlir::omp::YieldOp::create(builder, end, updatedStorage);
      }

      builder.setInsertionPointAfter(op);
      checkAndEmitOpenMPLastprivateConditional(s.getX());
      return mlir::success();
    }
    mlir::IntegerAttr noHint;
    mlir::Value xPtr = xlv.getPointer();
    mlir::Type pointeeTy =
        mlir::cast<cir::PointerType>(xPtr.getType()).getPointee();
    // LLVM requires atomic ops to be byte-sized.  Widen bool (i1) -> i8.
    mlir::Type atomicTy = getAtomicElementType(pointeeTy);
    if (atomicTy != pointeeTy) {
      xPtr = cir::CastOp::create(builder, start,
                                  cir::PointerType::get(atomicTy),
                                  cir::CastKind::bitcast, xPtr);
    }
    auto op = mlir::omp::AtomicUpdateOp::create(
        builder, start, xPtr, /*atomic_control=*/{}, noHint,
        memoryOrderAttr);
    mlir::LogicalResult res = buildOpenMPAtomicUpdateRegion(
        *this, builder, start, end, op, atomicTy, xlv, s.getX(), s.getExpr(),
        s.getUpdateExpr(), s.isXLHSInRHSPart());
    builder.setInsertionPointAfter(op);
    if (mlir::succeeded(res))
      checkAndEmitOpenMPLastprivateConditional(s.getX());
    return res;
  }
  case llvm::omp::OMPC_capture: {
    if (xlv.isBitField()) {
      // Bitfield atomic capture: use omp.atomic.capture with read+update on
      // the storage integer, then extract the captured bitfield value.
      const CIRGenBitFieldInfo &info = xlv.getBitFieldInfo();
      Address storageAddr = xlv.getBitFieldAddress();
      QualType storageTy =
          getContext().getIntTypeForBitwidth(info.storageSize, /*Signed=*/false);
      mlir::Value storagePtr = storageAddr.getPointer();
      mlir::Type storageCIRTy = convertType(storageTy);
      QualType xTy = s.getX()->getType().getNonReferenceType();

      // Temp to hold the captured storage value (whole integer, not bitfield).
      Address captureTempAddr =
          createMemTemp(storageTy, start, "__omp_atomic_bf_capture");
      mlir::Value capturePtr = captureTempAddr.getPointer();

      mlir::IntegerAttr noHint;
      auto captureOp = mlir::omp::AtomicCaptureOp::create(builder, start,
                                                           noHint,
                                                           memoryOrderAttr);
      mlir::LogicalResult res = mlir::success();

      // Evaluate the expression outside the capture region.
      builder.setInsertionPoint(captureOp);
      RValue exprValue = emitOpenMPAtomicValueExpr(*this, s.getExpr());
      if (exprValue.isIgnored())
        return mlir::failure();

      {
        mlir::OpBuilder::InsertionGuard guard(builder);
        mlir::Block *captureBlock = builder.createBlock(
            &captureOp.getRegion(), captureOp.getRegion().end(), {}, {});
        builder.setInsertionPointToStart(captureBlock);
        auto terminator = mlir::omp::TerminatorOp::create(builder, end);
        builder.setInsertionPoint(captureOp);

        if (s.isPostfixUpdate()) {
          // { v = x; x = update(x); } — read first, then update
          auto readOp = emitOpenMPAtomicRead(
              *this, builder, start, storagePtr, capturePtr, storageCIRTy,
              xlv.isVolatileQualified(), /*vIsVolatile=*/false,
              /*memoryOrder=*/{});

          if (s.getUpdateExpr()) {
            auto updateOp = mlir::omp::AtomicUpdateOp::create(
                builder, start, storagePtr, /*atomic_control=*/{}, noHint,
                /*memory_order=*/{});
            {
              mlir::OpBuilder::InsertionGuard guard2(builder);
              auto *block = builder.createBlock(
                  &updateOp.getRegion(), updateOp.getRegion().end(),
                  {storageCIRTy}, SmallVector<mlir::Location, 1>{start});
              builder.setInsertionPointToEnd(block);

              Address tempAddr =
                  createMemTemp(storageTy, start, "atomic.bf.temp");
              builder.createStore(start, block->getArgument(0), tempAddr);
              LValue tempBFLV = LValue::makeBitfield(tempAddr, info,
                                                      xlv.getType(),
                                                      xlv.getBaseInfo(),
                                                      xlv.getTBAAInfo());
              RValue oldBFVal =
                  emitLoadOfBitfieldLValue(tempBFLV, s.getX()->getExprLoc());

              RValue updateResult;
              if (mlir::failed(buildOpenMPAtomicUpdateValue(
                      *this, s.getX(), s.getExpr(), s.getUpdateExpr(),
                      s.isXLHSInRHSPart(), oldBFVal, exprValue, updateResult)))
                res = mlir::failure();
              else {
                Address desiredAddr =
                    createMemTemp(storageTy, start, "atomic.bf.desired");
                builder.createStore(start, block->getArgument(0), desiredAddr);
                LValue desiredBFLV = LValue::makeBitfield(
                    desiredAddr, info, xlv.getType(), xlv.getBaseInfo(),
                    xlv.getTBAAInfo());
                emitStoreThroughBitfieldLValue(
                    RValue::get(updateResult.getValue()), desiredBFLV);
                mlir::Value updatedStorage =
                    builder.createLoad(start, desiredAddr);
                mlir::omp::YieldOp::create(builder, end, updatedStorage);
              }
            }
            readOp->moveBefore(terminator);
            updateOp->moveBefore(terminator);
          } else {
            // { v = x; x = expr; } — read then write
            mlir::Value newVal = exprValue.getValue();
            if (!getContext().hasSameType(s.getExpr()->getType(), xTy))
              newVal = emitScalarConversion(newVal, s.getExpr()->getType(), xTy,
                                            s.getExpr()->getExprLoc());
            // For write of a bitfield, we still need an atomic.update to set
            // the bitfield within the storage integer.
            auto updateOp = mlir::omp::AtomicUpdateOp::create(
                builder, start, storagePtr, /*atomic_control=*/{}, noHint,
                /*memory_order=*/{});
            {
              mlir::OpBuilder::InsertionGuard guard2(builder);
              auto *block = builder.createBlock(
                  &updateOp.getRegion(), updateOp.getRegion().end(),
                  {storageCIRTy}, SmallVector<mlir::Location, 1>{start});
              builder.setInsertionPointToEnd(block);

              Address desiredAddr =
                  createMemTemp(storageTy, start, "atomic.bf.desired");
              builder.createStore(start, block->getArgument(0), desiredAddr);
              LValue desiredBFLV = LValue::makeBitfield(
                  desiredAddr, info, xlv.getType(), xlv.getBaseInfo(),
                  xlv.getTBAAInfo());
              emitStoreThroughBitfieldLValue(RValue::get(newVal), desiredBFLV);
              mlir::Value updatedStorage =
                  builder.createLoad(start, desiredAddr);
              mlir::omp::YieldOp::create(builder, end, updatedStorage);
            }
            readOp->moveBefore(terminator);
            updateOp->moveBefore(terminator);
          }
        } else {
          // { x = update(x); v = x; } — update first, then read
          auto updateOp = mlir::omp::AtomicUpdateOp::create(
              builder, start, storagePtr, /*atomic_control=*/{}, noHint,
              /*memory_order=*/{});
          {
            mlir::OpBuilder::InsertionGuard guard2(builder);
            auto *block = builder.createBlock(
                &updateOp.getRegion(), updateOp.getRegion().end(),
                {storageCIRTy}, SmallVector<mlir::Location, 1>{start});
            builder.setInsertionPointToEnd(block);

            Address tempAddr =
                createMemTemp(storageTy, start, "atomic.bf.temp");
            builder.createStore(start, block->getArgument(0), tempAddr);
            LValue tempBFLV = LValue::makeBitfield(tempAddr, info,
                                                    xlv.getType(),
                                                    xlv.getBaseInfo(),
                                                    xlv.getTBAAInfo());
            RValue oldBFVal =
                emitLoadOfBitfieldLValue(tempBFLV, s.getX()->getExprLoc());

            RValue updateResult;
            if (mlir::failed(buildOpenMPAtomicUpdateValue(
                    *this, s.getX(), s.getExpr(), s.getUpdateExpr(),
                    s.isXLHSInRHSPart(), oldBFVal, exprValue, updateResult)))
              res = mlir::failure();
            else {
              Address desiredAddr =
                  createMemTemp(storageTy, start, "atomic.bf.desired");
              builder.createStore(start, block->getArgument(0), desiredAddr);
              LValue desiredBFLV = LValue::makeBitfield(
                  desiredAddr, info, xlv.getType(), xlv.getBaseInfo(),
                  xlv.getTBAAInfo());
              emitStoreThroughBitfieldLValue(
                  RValue::get(updateResult.getValue()), desiredBFLV);
              mlir::Value updatedStorage =
                  builder.createLoad(start, desiredAddr);
              mlir::omp::YieldOp::create(builder, end, updatedStorage);
            }
          }
          auto readOp = emitOpenMPAtomicRead(
              *this, builder, start, storagePtr, capturePtr, storageCIRTy,
              xlv.isVolatileQualified(), /*vIsVolatile=*/false,
              /*memoryOrder=*/{});
          updateOp->moveBefore(terminator);
          readOp->moveBefore(terminator);
        }
      }

      builder.setInsertionPointAfter(captureOp);
      if (mlir::succeeded(res)) {
        // Extract the bitfield value from the captured storage integer.
        LValue captureBFLV = LValue::makeBitfield(
            captureTempAddr, info, xlv.getType(), xlv.getBaseInfo(),
            xlv.getTBAAInfo());
        RValue capturedBFVal =
            emitLoadOfBitfieldLValue(captureBFLV, s.getV()->getExprLoc());
        if (mlir::failed(emitOpenMPAtomicSimpleStore(
                *this, s.getV(), capturedBFVal, xTy, s.getV()->getExprLoc())))
          return mlir::failure();
        checkAndEmitOpenMPLastprivateConditional(s.getX());
        checkAndEmitOpenMPLastprivateConditional(s.getV());
      }
      return res;
    }
    mlir::IntegerAttr noHint;
    mlir::Value xPtr = xlv.getPointer();
    QualType xTy = s.getX()->getType().getNonReferenceType();
    mlir::Type pointeeTy =
        mlir::cast<cir::PointerType>(xPtr.getType()).getPointee();
    if (isOpenMPAtomicVectorComponentLValue(xlv)) {
      Address captureTempAddr = createTempAlloca(
          pointeeTy, xlv.getAlignment(), start, "__omp_atomic_capture_v");
      auto op = mlir::omp::AtomicCaptureOp::create(builder, start, noHint,
                                                   memoryOrderAttr);
      mlir::LogicalResult res = mlir::success();

      mlir::OpBuilder::InsertionGuard guard(builder);
      mlir::Block *block =
          builder.createBlock(&op.getRegion(), op.getRegion().end(), {}, {});
      builder.setInsertionPointToStart(block);
      auto terminator = mlir::omp::TerminatorOp::create(builder, end);

      builder.setInsertionPoint(op);
      if (s.isPostfixUpdate()) {
        auto readOp = emitOpenMPAtomicRead(
            *this, builder, start, xPtr, captureTempAddr.getPointer(),
            pointeeTy, xlv.isVolatileQualified(), /*vIsVolatile=*/false,
            /*memoryOrder=*/{});
        auto updateOp = mlir::omp::AtomicUpdateOp::create(
            builder, start, xPtr, /*atomic_control=*/{}, noHint,
            /*memory_order=*/{});
        res = buildOpenMPAtomicUpdateRegion(*this, builder, start, end,
                                            updateOp, pointeeTy, xlv, s.getX(),
                                            s.getExpr(), s.getUpdateExpr(),
                                            s.isXLHSInRHSPart());
        readOp->moveBefore(terminator);
        updateOp->moveBefore(terminator);
      } else {
        auto updateOp = mlir::omp::AtomicUpdateOp::create(
            builder, start, xPtr, /*atomic_control=*/{}, noHint,
            /*memory_order=*/{});
        res = buildOpenMPAtomicUpdateRegion(*this, builder, start, end, updateOp,
                                            pointeeTy, xlv, s.getX(),
                                            s.getExpr(), s.getUpdateExpr(),
                                            s.isXLHSInRHSPart());
        auto readOp = emitOpenMPAtomicRead(
            *this, builder, start, xPtr, captureTempAddr.getPointer(),
            pointeeTy, xlv.isVolatileQualified(), /*vIsVolatile=*/false,
            /*memoryOrder=*/{});
        updateOp->moveBefore(terminator);
        readOp->moveBefore(terminator);
      }

      builder.setInsertionPointAfter(op);
      if (mlir::succeeded(res)) {
        mlir::Value capturedVector = builder.createLoad(start, captureTempAddr);
        RValue capturedValue = getOpenMPAtomicVectorComponentValue(
            *this, xlv, capturedVector, s.getV()->getSourceRange(),
            s.getV()->getExprLoc());
        if (capturedValue.isIgnored())
          return mlir::failure();
        if (mlir::failed(emitOpenMPAtomicSimpleStore(
                *this, s.getV(), capturedValue, xTy, s.getV()->getExprLoc())))
          return mlir::failure();
        checkAndEmitOpenMPLastprivateConditional(s.getX());
        checkAndEmitOpenMPLastprivateConditional(s.getV());
      }
      return res;
    }

    Address vTempAddr = Address::invalid();
    mlir::Value vPtr;
    bool vIsVolatile = false;
    if (vIsGlobalReg) {
      vTempAddr = createMemTemp(xTy, start, "__omp_atomic_capture_v");
      vPtr = vTempAddr.getPointer();
    } else {
      LValue vlv = emitLValue(s.getV());
      vPtr = vlv.getPointer();
      vIsVolatile = vlv.isVolatileQualified();
    }
    // LLVM requires atomic ops to be byte-sized.  Widen bool (i1) -> i8.
    mlir::Type atomicTy = getAtomicElementType(pointeeTy);
    if (atomicTy != pointeeTy) {
      auto widenedPtrTy = cir::PointerType::get(atomicTy);
      xPtr = cir::CastOp::create(builder, start, widenedPtrTy,
                                  cir::CastKind::bitcast, xPtr);
      vPtr = cir::CastOp::create(builder, start, widenedPtrTy,
                                  cir::CastKind::bitcast, vPtr);
    }
    auto op = mlir::omp::AtomicCaptureOp::create(builder, start, noHint,
                                                 memoryOrderAttr);
    mlir::LogicalResult res = mlir::success();

    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *block =
        builder.createBlock(&op.getRegion(), op.getRegion().end(), {}, {});
    builder.setInsertionPointToStart(block);
    auto terminator = mlir::omp::TerminatorOp::create(builder, end);

    builder.setInsertionPoint(op);

    if (s.isPostfixUpdate()) {
      auto readOp = emitOpenMPAtomicRead(
          *this, builder, start, xPtr, vPtr, atomicTy,
          xlv.isVolatileQualified(), vIsVolatile, /*memoryOrder=*/{});
      if (s.getUpdateExpr()) {
        auto updateOp = mlir::omp::AtomicUpdateOp::create(
            builder, start, xPtr, /*atomic_control=*/{}, noHint,
            /*memory_order=*/{});
        res = buildOpenMPAtomicUpdateRegion(*this, builder, start, end,
                                            updateOp, atomicTy, xlv, s.getX(),
                                            s.getExpr(), s.getUpdateExpr(),
                                            s.isXLHSInRHSPart());
        readOp->moveBefore(terminator);
        updateOp->moveBefore(terminator);
      } else {
        mlir::Value exprValue =
            emitOpenMPAtomicScalarExpr(*this, s.getExpr(), xTy);
        if (!exprValue) {
          res = mlir::failure();
        } else {
          auto writeOp = emitOpenMPAtomicWrite(
              *this, builder, start, xPtr, exprValue,
              xlv.isVolatileQualified(), /*memoryOrder=*/{});
          readOp->moveBefore(terminator);
          writeOp->moveBefore(terminator);
        }
      }
    } else {
      auto updateOp = mlir::omp::AtomicUpdateOp::create(
          builder, start, xPtr, /*atomic_control=*/{}, noHint,
          /*memory_order=*/{});
      res = buildOpenMPAtomicUpdateRegion(*this, builder, start, end, updateOp,
                                          atomicTy, xlv, s.getX(),
                                          s.getExpr(),
                                          s.getUpdateExpr(),
                                          s.isXLHSInRHSPart());
      auto readOp = emitOpenMPAtomicRead(
          *this, builder, start, xPtr, vPtr, atomicTy,
          xlv.isVolatileQualified(), vIsVolatile, /*memoryOrder=*/{});
      updateOp->moveBefore(terminator);
      readOp->moveBefore(terminator);
    }
    builder.setInsertionPointAfter(op);
    if (mlir::succeeded(res)) {
      if (vIsGlobalReg) {
        RValue captured =
            emitLoadOfLValue(makeAddrLValue(vTempAddr, xTy),
                             s.getV()->getExprLoc());
        if (mlir::failed(emitOpenMPAtomicSimpleStore(
                *this, s.getV(), captured, xTy, s.getV()->getExprLoc())))
          return mlir::failure();
      }
      checkAndEmitOpenMPLastprivateConditional(s.getX());
      checkAndEmitOpenMPLastprivateConditional(s.getV());
    }
    return res;
  }
  case llvm::omp::OMPC_compare:
    emitOpenMPAtomicCompare(*this, s, atomicOrdering, failOrdering);
    return mlir::success();
  case llvm::omp::OMPC_fail:
  case llvm::omp::OMPC_hint:
  case llvm::omp::OMPC_weak:
    llvm_unreachable("unsupported clauses handled before emission");
  default:
    getCIRGenModule().errorNYI(s.getSourceRange(),
                               "unexpected OpenMP atomic clause kind");
    return mlir::failure();
  }
}
static constexpr llvm::StringLiteral kOpenMPTargetParentNameAttr =
    "omp.target_parent_name";

static void annotateOpenMPTargetParentName(CIRGenFunction &cgf,
                                           mlir::omp::TargetOp targetOp) {
  llvm::StringRef parentName = cgf.getOpenMPTargetParentNameOverride();
  if (parentName.empty())
    return;
  targetOp->setAttr(kOpenMPTargetParentNameAttr,
                    cgf.getCIRGenModule().getStringAttrCached(parentName));
}

void CIRGenFunction::emitScannedOMPTargetDirective(
    const OMPExecutableDirective &d, llvm::StringRef parentName) {
  struct TargetOpEmissionScope {
    CIRGenFunction &cgf;
    bool prevForce = false;
    std::string prevParentName;

    TargetOpEmissionScope(CIRGenFunction &cgf, llvm::StringRef parentName)
        : cgf(cgf), prevForce(cgf.forceOpenMPTargetOpEmission),
          prevParentName(cgf.openMPTargetParentNameOverride) {
      cgf.forceOpenMPTargetOpEmission = true;
      cgf.openMPTargetParentNameOverride = parentName.str();
    }

    ~TargetOpEmissionScope() {
      cgf.forceOpenMPTargetOpEmission = prevForce;
      cgf.openMPTargetParentNameOverride = std::move(prevParentName);
    }
  } scope(*this, parentName);

  (void)emitStmt(&d, /*useCurrentScope=*/true);
}

void CIRGenModule::emitOpenMPTargetDeviceStub(
    llvm::StringRef parentName, const OMPExecutableDirective &d) {
  const CapturedStmt *targetCaptured =
      d.getCapturedStmt(OpenMPDirectiveKind::OMPD_target);
  if (!targetCaptured)
    return;

  struct GenericEmissionScope {
    CIRGenModule &cgm;
    explicit GenericEmissionScope(CIRGenModule &cgm) : cgm(cgm) {
      cgm.pushOpenMPTargetGenericEmission();
    }
    ~GenericEmissionScope() { cgm.popOpenMPTargetGenericEmission(); }
  } genericScope(*this);

  CIRGenFunction cgf(*this, builder, /*suppressNewContext=*/true);
  auto *info =
      new CIRGenFunction::CGCapturedStmtInfo(*targetCaptured, clang::CR_OpenMP);
  {
    CIRGenFunction::CGCapturedStmtRAII capInfoRAII(cgf, info);
    (void)cgf.generateCapturedStmtFunction(
        *targetCaptured,
        [&](CIRGenFunction &stubCGF, const CapturedDecl &) {
          stubCGF.emitScannedOMPTargetDirective(d, parentName);
        });
  }
  delete info;
}

mlir::LogicalResult
CIRGenFunction::emitOMPTargetDirective(const OMPTargetDirective &s) {
  if (!shouldEmitOpenMPTargetAsOperation())
    return emitStmt(s.getInnermostCapturedStmt()->getCapturedStmt(),
                    /*useCurrentScope=*/true);

  const bool hasOffloadTargets = !getLangOpts().OMPTargetTriples.empty();
  const CapturedStmt *targetCaptured =
      s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target);
  assert(targetCaptured && "expected OpenMP target captured stmt");

  llvm::SmallPtrSet<const VarDecl *, 4> materializedClauseVars;
  materializeOMPTargetClauseExprs(*this, s.clauses(), materializedClauseVars);

  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  auto targetOp = mlir::omp::TargetOp::create(
      builder, begin,
      /*allocate_vars=*/mlir::ValueRange{},
      /*allocator_vars=*/mlir::ValueRange{},
      /*bare=*/false,
      /*depend_kinds=*/mlir::ArrayAttr{},
      /*depend_vars=*/mlir::ValueRange{},
      /*depend_sizes=*/mlir::ValueRange{},
      /*device=*/mlir::Value{},
      /*has_device_addr_vars=*/mlir::ValueRange{},
      /*host_eval_vars=*/mlir::ValueRange{},
      /*if_expr=*/mlir::Value{},
      /*in_reduction_vars=*/mlir::ValueRange{},
      /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
      /*in_reduction_syms=*/mlir::ArrayAttr{},
      /*is_device_ptr_vars=*/mlir::ValueRange{},
      /*map_vars=*/mlir::ValueRange{},
      /*nowait=*/false,
      /*private_vars=*/mlir::ValueRange{},
      /*private_syms=*/mlir::ArrayAttr{},
      /*private_needs_barrier=*/false,
      /*thread_limit=*/mlir::Value{},
      /*private_maps=*/mlir::DenseI64ArrayAttr{});
  annotateOpenMPTargetParentName(*this, targetOp);
  OpenMPClauseEmissionInfo clauseInfo = emitOpenMPClauses(targetOp, s.clauses());
  if (mlir::failed(appendOpenMPTargetCapturedVLASizePrivates(
          *this, targetOp, *targetCaptured, clauseInfo)))
    return mlir::failure();
  if (mlir::failed(
          maybeEmitOpenMPRuntimeDependPayload(*this, targetOp, s, clauseInfo)))
    return mlir::failure();
  if (!hasOffloadTargets)
    if (mlir::failed(appendHostOnlyOMPTargetCapturedMappings(
            *this, targetOp, clauseInfo, *targetCaptured)))
      return mlir::failure();
  if (mlir::failed(finalizeOMPTargetPrivateMappings(*this, targetOp,
                                                    clauseInfo)))
    return mlir::failure();
  if (hasOffloadTargets)
    promoteOMPTargetPrivateMapsForOffload(targetOp, clauseInfo);

  bool wrapTargetBodyInScope = true;
  const Stmt *targetBody = targetCaptured->getCapturedStmt();
  while (targetBody) {
    if (const auto *captured = dyn_cast<CapturedStmt>(targetBody)) {
      targetBody = captured->getCapturedStmt();
      continue;
    }
    if (const auto *compound = dyn_cast<CompoundStmt>(targetBody)) {
      if (compound->size() == 1 && compound->body_front()) {
        targetBody = compound->body_front();
        continue;
      }
    }
    break;
  }
  // Keep nested OpenMP constructs directly under omp.target when possible:
  // MLIR's verifier rejects wrappers like cir.scope between omp.target and
  // ops such as omp.teams.
  if (isa_and_nonnull<OMPExecutableDirective>(targetBody))
    wrapTargetBodyInScope = false;

  return emitOMPTargetRegionBody(
      *this, targetOp, clauseInfo, begin, end, [&]() -> mlir::LogicalResult {
        return emitStmt(targetCaptured->getCapturedStmt(),
                        /*useCurrentScope=*/true);
      },
      wrapTargetBodyInScope);
}

/// Emit the region body for an omp::TeamsOp: create the entry block, set up
/// private/reduction block-arg replacement inside a LexicalScope, emit
/// \p bodyEmitter, restore saved addresses, and add the terminator.
static mlir::LogicalResult
emitOMPTeamsRegionBody(CIRGenFunction &cgf, mlir::omp::TeamsOp teamsOp,
                       const CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo,
                       llvm::function_ref<mlir::LogicalResult()> bodyEmitter,
                       mlir::Location begin, mlir::Location end,
                       bool wrapInScope = true) {
  mlir::LogicalResult res = mlir::success();
  CIRGenBuilderTy &builder = cgf.getBuilder();

  mlir::OpBuilder::InsertionGuard guardCase(builder);
  llvm::SmallVector<mlir::Type, 4> teamsArgTypes;
  llvm::SmallVector<mlir::Location, 4> teamsArgLocs;
  for (mlir::Value privateVar : teamsOp.getPrivateVars()) {
    teamsArgTypes.push_back(privateVar.getType());
    teamsArgLocs.push_back(privateVar.getLoc());
  }
  for (mlir::Value reductionVar : teamsOp.getReductionVars()) {
    teamsArgTypes.push_back(reductionVar.getType());
    teamsArgLocs.push_back(reductionVar.getLoc());
  }

  mlir::Block *block = builder.createBlock(&teamsOp.getRegion(), {},
                                           teamsArgTypes, teamsArgLocs);
  builder.setInsertionPointToEnd(block);

  auto emitBody = [&]() {
    CIRGenFunction::LexicalScope ls{cgf, begin, builder.getInsertionBlock()};
    if (!wrapInScope)
      ls.setCleanupOnly();
    llvm::SmallVector<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>, 4>
        declGuards;
    auto argIface = mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
        teamsOp.getOperation());
    auto privateArgs = argIface.getPrivateBlockArgs();
    auto reductionArgs = argIface.getReductionBlockArgs();
    assert(clauseInfo.privateIndices.size() == clauseInfo.privateVars.size() &&
           "mismatched OpenMP teams private metadata");
    for (auto [index, vd] :
         llvm::zip_equal(clauseInfo.privateIndices, clauseInfo.privateVars)) {
      assert(index < privateArgs.size() &&
             "unexpected OpenMP teams private block arg index");
      mlir::Value privateArg = privateArgs[index];
      auto ptrTy = mlir::cast<cir::PointerType>(privateArg.getType());
      declGuards.push_back(
          std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(cgf, vd));
      cgf.replaceAddrOfLocalVar(
          vd, Address(privateArg, ptrTy.getPointee(),
                      cgf.getContext().getDeclAlign(vd)));
    }
    assert(clauseInfo.reductionIndices.size() == clauseInfo.reductionVars.size() &&
           "mismatched OpenMP teams reduction metadata");
    for (auto [index, vd] :
         llvm::zip_equal(clauseInfo.reductionIndices, clauseInfo.reductionVars)) {
      assert(index < reductionArgs.size() &&
             "unexpected OpenMP teams reduction block arg index");
      mlir::Value reductionArg = reductionArgs[index];
      auto ptrTy = mlir::cast<cir::PointerType>(reductionArg.getType());
      declGuards.push_back(
          std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(cgf, vd));
      cgf.replaceAddrOfLocalVar(
          vd, Address(reductionArg, ptrTy.getPointee(),
                      cgf.getContext().getDeclAlign(vd)));
    }

    res = bodyEmitter();
  };

  if (wrapInScope) {
    cir::ScopeOp::create(
        builder, begin, [&](mlir::OpBuilder &, mlir::Location) { emitBody(); });
  } else {
    emitBody();
  }

  mlir::omp::TerminatorOp::create(builder, end);

  return res;
}

static mlir::LogicalResult
emitOMPTeamsRegionBody(CIRGenFunction &cgf, mlir::omp::TeamsOp teamsOp,
                       const CIRGenFunction::OpenMPClauseEmissionInfo &clauseInfo,
                       const Stmt *body, mlir::Location begin,
                       mlir::Location end, bool wrapInScope = true) {
  return emitOMPTeamsRegionBody(
      cgf, teamsOp, clauseInfo,
      [&]() -> mlir::LogicalResult {
        return cgf.emitStmt(body, /*useCurrentScope=*/true);
      },
      begin, end, wrapInScope);
}

static mlir::LogicalResult
emitOMPDistributeLoop(CIRGenFunction &cgf, const OMPLoopDirective &s,
                      ArrayRef<const OMPClause *> distributeClauses,
                      llvm::StringRef directiveName, mlir::Location begin,
                      mlir::Location end, bool rematerializeClauseExprs,
                      mlir::Value distScheduleChunkOverride = {},
                      mlir::Value lowerBoundOverride = {},
                      mlir::Value upperBoundOverride = {},
                      mlir::Value stepOverride = {},
                      ArrayRef<const VarDecl *> enclosingFirstprivateVars = {}) {
  auto loopInfo = getOpenMPSimpleCanonicalForInfo(s);
  auto generatedLoopInfo =
      loopInfo ? std::optional<OpenMPSimpleGeneratedLoopInfo>{}
               : getOpenMPSimpleGeneratedLoopInfo(s);
  if (!loopInfo && !generatedLoopInfo) {
    std::string feature = directiveName.str();
    feature += " for non-canonical/non-unit-stride loop";
    cgf.getCIRGenModule().errorNYI(s.getSourceRange(), feature);
    return mlir::failure();
  }
  if (!loopInfo &&
      (lowerBoundOverride || upperBoundOverride || stepOverride)) {
    std::string feature = directiveName.str();
    feature += " target host_eval for generated loop";
    cgf.getCIRGenModule().errorNYI(s.getSourceRange(), feature);
    return mlir::failure();
  }

  llvm::SmallPtrSet<const VarDecl *, 4> rematerializedClauseVars;
  llvm::SmallVector<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>, 4>
      rematerializedClauseGuards;
  if (rematerializeClauseExprs) {
    for (const OMPClause *clause : distributeClauses) {
      if (clause->getClauseKind() != llvm::omp::OMPC_dist_schedule)
        continue;
      if (const Expr *chunk = cast<OMPDistScheduleClause>(clause)->getChunkSize();
          chunk && !distScheduleChunkOverride)
        rematerializeOpenMPCapturedExpr(cgf, chunk, rematerializedClauseVars,
                                        rematerializedClauseGuards);
    }
    if (loopInfo && !lowerBoundOverride)
      rematerializeOpenMPCapturedExpr(cgf, loopInfo->lowerBound,
                                      rematerializedClauseVars,
                                      rematerializedClauseGuards);
    if (loopInfo && !upperBoundOverride)
      rematerializeOpenMPCapturedExpr(cgf, loopInfo->upperBound,
                                      rematerializedClauseVars,
                                      rematerializedClauseGuards);
    if (generatedLoopInfo && !upperBoundOverride)
      rematerializeOpenMPCapturedExpr(cgf, generatedLoopInfo->lastIteration,
                                      rematerializedClauseVars,
                                      rematerializedClauseGuards);
  }

  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Value distScheduleChunk;
  bool distScheduleStatic = false;
  mlir::omp::ClauseOrderKindAttr order;
  mlir::omp::OrderModifierAttr orderMod;
  llvm::SmallVector<const OMPClause *, 4> distributeLastprivateClauses;
  for (const OMPClause *clause : distributeClauses) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_dist_schedule: {
      const auto *distScheduleClause = cast<OMPDistScheduleClause>(clause);
      if (distScheduleClause->getDistScheduleKind() !=
          OMPC_DIST_SCHEDULE_static) {
        cgf.getCIRGenModule().errorNYI(
            clause->getBeginLoc(),
            (directiveName + " dist_schedule kind").str());
        return mlir::failure();
      }
      distScheduleStatic = true;
      if (const Expr *chunk = distScheduleClause->getChunkSize())
        distScheduleChunk = distScheduleChunkOverride
                                ? distScheduleChunkOverride
                                : emitOpenMPIntegerExpr(cgf, chunk);
      break;
    }
    case llvm::omp::OMPC_order: {
      const auto *orderClause = cast<OMPOrderClause>(clause);
      switch (orderClause->getKind()) {
      case OMPC_ORDER_concurrent:
        order = mlir::omp::ClauseOrderKindAttr::get(
            builder.getContext(), mlir::omp::ClauseOrderKind::Concurrent);
        break;
      case OMPC_ORDER_unknown:
        llvm_unreachable("unknown OpenMP order kind");
      }

      switch (orderClause->getModifier()) {
      case OMPC_ORDER_MODIFIER_unknown:
        break;
      case OMPC_ORDER_MODIFIER_reproducible:
        orderMod = mlir::omp::OrderModifierAttr::get(
            builder.getContext(), mlir::omp::OrderModifier::reproducible);
        break;
      case OMPC_ORDER_MODIFIER_unconstrained:
        orderMod = mlir::omp::OrderModifierAttr::get(
            builder.getContext(), mlir::omp::OrderModifier::unconstrained);
        break;
      case OMPC_ORDER_MODIFIER_last:
        llvm_unreachable("unexpected OpenMP order modifier sentinel");
      }
      break;
    }
    case llvm::omp::OMPC_collapse:
      break;
    case llvm::omp::OMPC_lastprivate:
      distributeLastprivateClauses.push_back(clause);
      break;
    default:
      cgf.getCIRGenModule().errorNYI(
          clause->getBeginLoc(), (directiveName + " clause").str(),
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      return mlir::failure();
    }
  }

  mlir::Operation *insertionParent =
      builder.getInsertionBlock() ? builder.getInsertionBlock()->getParentOp()
                                  : nullptr;
  bool insideTargetRegion =
      insertionParent && insertionParent->getParentOfType<mlir::omp::TargetOp>();
  CIRGenFunction::OpenMPClauseEmissionInfo distributeClauseInfo;
  llvm::SmallVector<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>, 4>
      distributePrivateDeclGuards;
  llvm::DenseMap<const VarDecl *, Address> distributeOriginalDecls;
  auto emitDistributeLastprivateFinals =
      [&](mlir::Location ivLoc, mlir::Value isLastIteration,
          unsigned numDistributePrivateArgs,
          llvm::function_ref<void()> emitFinals,
          const llvm::SmallPtrSetImpl<const VarDecl *> *skipLastprivateVars =
              nullptr) {
        if (distributeClauseInfo.lastprivateIndices.empty())
          return;
        assert(distributeClauseInfo.lastprivateIndices.size() ==
                   distributeClauseInfo.lastprivateVars.size() &&
               "mismatched OpenMP distribute lastprivate metadata");
        assert(distributeClauseInfo.lastprivateIndices.size() ==
                   distributeClauseInfo.lastprivateConditionals.size() &&
               "mismatched OpenMP distribute conditional-lastprivate metadata");
        cir::IfOp::create(
            builder, ivLoc, isLastIteration, /*withElseRegion=*/false,
            [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
              mlir::OpBuilder::InsertionGuard guard(builder);
              builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
              emitFinals();
              for (auto [index, vd, srcVD, destVD, assignOp, isConditional] :
                   llvm::zip_equal(
                       distributeClauseInfo.lastprivateIndices,
                       distributeClauseInfo.lastprivateVars,
                       distributeClauseInfo.lastprivateSourceVDs,
                       distributeClauseInfo.lastprivateDestVDs,
                       distributeClauseInfo.lastprivateAssignmentOps,
                       distributeClauseInfo.lastprivateConditionals)) {
                if (skipLastprivateVars &&
                    skipLastprivateVars->contains(vd->getCanonicalDecl()))
                  continue;
                assert(index < numDistributePrivateArgs &&
                       "unexpected OpenMP distribute lastprivate block arg "
                       "index");
                Address srcAddr = getOpenMPPrivateStorageAddress(cgf, vd);
                auto originalIt =
                    distributeOriginalDecls.find(vd->getCanonicalDecl());
                assert(originalIt != distributeOriginalDecls.end() &&
                       "missing lastprivate destination address");
                if (isConditional)
                  cgf.emitOpenMPLastprivateConditionalFinalUpdate(
                      vd, srcAddr, vd->getLocation());
                emitOpenMPLastprivateCopy(cgf, vd, srcAddr, originalIt->second,
                                          srcVD, destVD, assignOp);
              }
              cir::YieldOp::create(thenBuilder, thenLoc);
            });
      };
  auto emitSimpleDistributeLoop = [&](mlir::Value loopLowerBound,
                                      mlir::Value loopUpperBound,
                                      mlir::Value loopStep,
                                      bool loopInclusive,
                                      bool ivSigned,
                                      mlir::Location ivLoc,
                                      llvm::function_ref<mlir::LogicalResult(
                                          mlir::Block *, unsigned)> emitBody)
      -> mlir::LogicalResult {
    mlir::omp::DistributeOperands distributeClauseOps;
    auto distributeOp =
        mlir::omp::DistributeOp::create(builder, begin, distributeClauseOps);
    distributePrivateDeclGuards.clear();
    distributeOriginalDecls.clear();
    distributeClauseInfo = CIRGenFunction::OpenMPClauseEmissionInfo();
    if (!distributeLastprivateClauses.empty()) {
      distributeClauseInfo = cgf.emitOpenMPClauseInfo(
          distributeOp, distributeLastprivateClauses,
          enclosingFirstprivateVars);
    }
    if (distScheduleStatic)
      distributeOp.setDistScheduleStatic(true);
    if (distScheduleChunk)
      distributeOp.getDistScheduleChunkSizeMutable().assign(distScheduleChunk);
    if (order)
      distributeOp.setOrderAttr(order);
    if (orderMod)
      distributeOp.setOrderModAttr(orderMod);

    mlir::OpBuilder::InsertionGuard guard(builder);
    llvm::SmallVector<mlir::Type, 4> distributeArgTypes;
    llvm::SmallVector<mlir::Location, 4> distributeArgLocs;
    for (mlir::Value privateVar : distributeOp.getPrivateVars()) {
      distributeArgTypes.push_back(privateVar.getType());
      distributeArgLocs.push_back(privateVar.getLoc());
    }
    mlir::Block *distributeBlock = builder.createBlock(
        &distributeOp.getRegion(), {}, distributeArgTypes, distributeArgLocs);
    builder.setInsertionPointToEnd(distributeBlock);
    if (mlir::failed(remapOpenMPPrivateDecls(
            cgf, distributeOp.getOperation(), distributeClauseInfo,
            distributePrivateDeclGuards, &distributeOriginalDecls)))
      return mlir::failure();

    mlir::omp::LoopNestOperands loopClauseOps;
    loopClauseOps.loopLowerBounds.push_back(loopLowerBound);
    loopClauseOps.loopUpperBounds.push_back(loopUpperBound);
    loopClauseOps.loopSteps.push_back(loopStep);
    if (loopInclusive)
      loopClauseOps.loopInclusive = builder.getUnitAttr();

    auto loopNestOp =
        mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
    loopNestOp->setAttr("omp.frontend.iv_signed",
                        builder.getDenseBoolArrayAttr({ivSigned}));
    mlir::Block *loopBlock =
        builder.createBlock(&loopNestOp.getRegion(), {}, {loopUpperBound.getType()},
                            {ivLoc});
    builder.setInsertionPointToEnd(loopBlock);

    mlir::LogicalResult res =
        emitBody(loopBlock, distributeOp.getPrivateVars().size());
    distributePrivateDeclGuards.clear();
    mlir::omp::YieldOp::create(builder, end);
    return res;
  };

  if (loopInfo) {
    bool ivSigned = loopInfo->iv->getType()->hasSignedIntegerRepresentation();
    mlir::Value lowerBound =
        lowerBoundOverride ? lowerBoundOverride
                           : emitOpenMPIntegerExpr(cgf, loopInfo->lowerBound);
    mlir::Value upperBound =
        upperBoundOverride ? upperBoundOverride
                           : emitOpenMPIntegerExpr(cgf, loopInfo->upperBound);
    std::optional<mlir::IntegerType> loopTy =
        getOpenMPWorksharingLoopType(cgf, lowerBound, upperBound);
    if (!loopTy) {
      generatedLoopInfo = getOpenMPSimpleGeneratedLoopInfo(s);
      if (!generatedLoopInfo) {
        std::string feature = directiveName.str();
        feature += " with unsupported loop bound types";
        cgf.getCIRGenModule().errorNYI(s.getSourceRange(), feature);
        return mlir::failure();
      }
    }
    if (loopTy) {
      lowerBound =
          castOpenMPValueToBuiltinInt(cgf, begin, lowerBound, *loopTy, ivSigned);
      upperBound =
          castOpenMPValueToBuiltinInt(cgf, begin, upperBound, *loopTy, ivSigned);
      mlir::Value step =
          stepOverride
              ? castOpenMPValueToBuiltinInt(cgf, begin, stepOverride, *loopTy,
                                            ivSigned)
              : createOpenMPIntegerConstant(cgf, begin, *loopTy, 1);

      return emitSimpleDistributeLoop(
          lowerBound, upperBound, step, loopInfo->upperInclusive,
          ivSigned, cgf.getLoc(loopInfo->iv->getLocation()),
          [&](mlir::Block *loopBlock,
              unsigned numDistributePrivateArgs) -> mlir::LogicalResult {
            CharUnits ivAlign = cgf.getContext().getDeclAlign(loopInfo->iv);
            mlir::Type ivType = cgf.convertTypeForMem(loopInfo->iv->getType());
            Address ivStorage = cgf.createTempAlloca(
                ivType, ivAlign, cgf.getLoc(loopInfo->iv->getLocation()),
                loopInfo->iv->getName());

            CIRGenFunction::DeclMapRevertingRAII ivMapGuard(cgf, loopInfo->iv);
            cgf.replaceAddrOfLocalVar(loopInfo->iv, ivStorage);
            auto logicalIVType = cir::IntType::get(&cgf.getMLIRContext(),
                                                   loopTy->getWidth(), ivSigned);
            mlir::Value logicalIVValue = castOpenMPValueToCIRType(
                cgf, cgf.getLoc(loopInfo->iv->getLocation()),
                loopBlock->getArgument(0), logicalIVType);
            mlir::Value ivValue = logicalIVValue;
            if (logicalIVType != ivType)
              ivValue = builder.createIntCast(ivValue,
                                              mlir::cast<cir::IntType>(ivType));
            builder.createStore(cgf.getLoc(loopInfo->iv->getLocation()),
                                ivValue, ivStorage);

            mlir::LogicalResult res = emitOMPLoopBody(
                cgf, builder, begin, loopInfo->forStmt->getBody());
            if (!distributeClauseInfo.lastprivateIndices.empty()) {
              mlir::Location ivLoc = cgf.getLoc(loopInfo->iv->getLocation());
              auto cirIVTy = mlir::cast<cir::IntType>(logicalIVType);
              mlir::Value upperBoundValue =
                  castOpenMPValueToCIRType(cgf, ivLoc, upperBound, logicalIVType);
              mlir::Value stepValue = builder.getConstInt(ivLoc, cirIVTy, 1);
              mlir::Value nextIV = builder.createAdd(ivLoc, logicalIVValue, stepValue);
              mlir::Value isLastIteration = builder.createCompare(
                  ivLoc, loopInfo->upperInclusive ? cir::CmpOpKind::gt
                                                  : cir::CmpOpKind::ge,
                  nextIV, upperBoundValue);
              emitDistributeLastprivateFinals(
                  ivLoc, isLastIteration, numDistributePrivateArgs,
                  []() {});
            }
            return res;
          });
    }
  }

  assert(generatedLoopInfo && "expected generated loop info fallback");
  if (mlir::failed(emitOpenMPTransformPreInits(cgf, s.getPreInits())))
    return mlir::failure();

  if (insideTargetRegion) {
    rematerializeOpenMPCapturedExprTree(cgf, generatedLoopInfo->preCond,
                                        rematerializedClauseVars,
                                        rematerializedClauseGuards);
    rematerializeOpenMPCapturedExprTree(cgf, generatedLoopInfo->lastIteration,
                                        rematerializedClauseVars,
                                        rematerializedClauseGuards);
    for (const Expr *update : generatedLoopInfo->updates)
      rematerializeOpenMPCapturedExprTree(cgf, update, rematerializedClauseVars,
                                          rematerializedClauseGuards);
  }
  auto emitGeneratedDistributeLoop = [&]() -> mlir::LogicalResult {
    mlir::Value upperBound =
        emitOpenMPIntegerExpr(cgf, generatedLoopInfo->lastIteration);
    mlir::Type loopTy = upperBound.getType();
    mlir::Value lowerBound = createOpenMPIntegerConstant(cgf, begin, loopTy, 0);
    mlir::Value step = createOpenMPIntegerConstant(cgf, begin, loopTy, 1);
    const VarDecl *logicalIV = generatedLoopInfo->logicalIV;
    bool logicalIVSigned =
        logicalIV->getType()->hasSignedIntegerRepresentation();
    mlir::Location logicalIVLoc = cgf.getLoc(logicalIV->getLocation());
    llvm::SmallVector<bool, 4> emitFinals;
    emitFinals.reserve(generatedLoopInfo->sourceIVs.size());
    for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs) {
      emitFinals.push_back(cgf.localDeclMap.count(sourceIV) ||
                           sourceIV->hasGlobalStorage() ||
                           (cgf.capturedStmtInfo &&
                            cgf.capturedStmtInfo->lookup(sourceIV)));
    }
    llvm::DenseMap<const VarDecl *, Address> sourceIVOriginalAddrs;
    for (auto [idx, sourceIV] : llvm::enumerate(generatedLoopInfo->sourceIVs)) {
      if (!emitFinals[idx])
        continue;
      sourceIVOriginalAddrs.try_emplace(
          sourceIV, cgf.emitLValue(s.counters()[idx]).getAddress());
    }

    return emitSimpleDistributeLoop(
        lowerBound, upperBound, step, /*loopInclusive=*/true, logicalIVSigned,
        logicalIVLoc, [&](mlir::Block *loopBlock,
                          unsigned numDistributePrivateArgs)
                          -> mlir::LogicalResult {
          auto createLoopVarStorage = [&](const VarDecl *vd) {
            CharUnits vdAlign = cgf.getContext().getDeclAlign(vd);
            mlir::Type vdType = cgf.convertTypeForMem(vd->getType());
            Address rawAlloca = Address::invalid();
            cgf.createTempAlloca(vdType, vdAlign, cgf.getLoc(vd->getLocation()),
                                 vd->getName(), /*arraySize=*/nullptr,
                                 &rawAlloca);
            return std::pair{rawAlloca, vdType};
          };

          auto [logicalIVRawAlloca, logicalIVType] =
              createLoopVarStorage(logicalIV);
          llvm::SmallVector<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>,
                            4>
              sourceIVMapGuards;
          sourceIVMapGuards.reserve(generatedLoopInfo->sourceIVs.size());
          for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs) {
            auto [sourceIVRawAlloca, sourceIVType] =
                createLoopVarStorage(sourceIV);
            (void)sourceIVType;
            sourceIVMapGuards.push_back(
                std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(cgf,
                                                                       sourceIV));
            cgf.replaceAddrOfLocalVar(sourceIV, sourceIVRawAlloca);
          }

          CIRGenFunction::DeclMapRevertingRAII logicalIVMapGuard(cgf, logicalIV);
          cgf.replaceAddrOfLocalVar(logicalIV, logicalIVRawAlloca);
          llvm::SmallPtrSet<const VarDecl *, 4> sourceIVSet;
          for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs)
            sourceIVSet.insert(sourceIV->getCanonicalDecl());

          mlir::Value logicalIVValue = loopBlock->getArgument(0);
          if (auto ompIVTy =
                  mlir::dyn_cast<mlir::IntegerType>(logicalIVValue.getType())) {
            auto cirLoopTy = cir::IntType::get(&cgf.getMLIRContext(),
                                               ompIVTy.getWidth(),
                                               logicalIVSigned);
            logicalIVValue = mlir::UnrealizedConversionCastOp::create(
                                 builder, logicalIVLoc, cirLoopTy,
                                 logicalIVValue)
                                 .getResult(0);
            if (cirLoopTy != logicalIVType) {
              logicalIVValue =
                  builder.createIntCast(logicalIVValue,
                                        mlir::cast<cir::IntType>(logicalIVType));
            }
          } else {
            logicalIVValue =
                castOpenMPValueToCIRType(cgf, logicalIVLoc, logicalIVValue,
                                         logicalIVType);
          }

          builder.createStore(logicalIVLoc, logicalIVValue, logicalIVRawAlloca);
          for (const Expr *update : generatedLoopInfo->updates)
            cgf.emitIgnoredExpr(update);
          mlir::LogicalResult res =
              emitOMPLoopBody(cgf, builder, begin, generatedLoopInfo->body);
          if (!distributeClauseInfo.lastprivateIndices.empty()) {
            auto cirIVTy = mlir::cast<cir::IntType>(logicalIVType);
            mlir::Value upperBoundValue =
                castOpenMPValueToCIRType(cgf, logicalIVLoc, upperBound,
                                         logicalIVType);
            mlir::Value stepValue = builder.getConstInt(logicalIVLoc, cirIVTy, 1);
            mlir::Value nextIV =
                builder.createAdd(logicalIVLoc, logicalIVValue, stepValue);
            mlir::Value isLastIteration = builder.createCompare(
                logicalIVLoc, cir::CmpOpKind::gt, nextIV, upperBoundValue);
            emitDistributeLastprivateFinals(
                logicalIVLoc, isLastIteration, numDistributePrivateArgs,
                [&]() {
                  for (auto [idx, final] :
                       llvm::enumerate(generatedLoopInfo->finals)) {
                    if (!emitFinals[idx] || !final)
                      continue;
                    const VarDecl *sourceIV = generatedLoopInfo->sourceIVs[idx];
                    auto originalIt = sourceIVOriginalAddrs.find(sourceIV);
                    assert(originalIt != sourceIVOriginalAddrs.end() &&
                           "missing distribute loop source IV destination "
                           "address");
                    CIRGenFunction::DeclMapRevertingRAII finalSourceIVMapGuard(
                        cgf, sourceIV);
                    cgf.replaceAddrOfLocalVar(sourceIV, originalIt->second);
                    cgf.emitIgnoredExpr(final);
                  }
                },
                &sourceIVSet);
          }
          return res;
        });
  };

  bool preCondConstant = false;
  if (generatedLoopInfo->preCond->EvaluateAsBooleanCondition(preCondConstant,
                                                             cgf.getContext())) {
    if (!preCondConstant)
      return mlir::success();
    return emitGeneratedDistributeLoop();
  }

  mlir::LogicalResult res = mlir::success();
  mlir::Value preCond = cgf.emitOpOnBoolExpr(begin, generatedLoopInfo->preCond);
  cir::IfOp::create(
      builder, begin, preCond, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &thenBuilder, mlir::Location) {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
        res = emitGeneratedDistributeLoop();
        builder.createYield(begin);
      });
  return res;
}

// Match OGCG: plain distribute and generic loop bind(teams) both lower
// through the same inlined distribute path.
static mlir::LogicalResult
emitSimpleOMPDistributeDirective(CIRGenFunction &cgf,
                                 const OMPLoopDirective &s,
                                 ArrayRef<const OMPClause *> clauses,
                                 llvm::StringRef directiveName,
                                 ArrayRef<const VarDecl *> enclosingFirstprivateVars =
                                     {}) {
  return emitOMPDistributeLoop(cgf, s, clauses, directiveName,
                               cgf.getLoc(s.getBeginLoc()),
                               cgf.getLoc(s.getEndLoc()),
                               /*rematerializeClauseExprs=*/false,
                               /*distScheduleChunkOverride=*/{},
                               /*lowerBoundOverride=*/{},
                               /*upperBoundOverride=*/{},
                               /*stepOverride=*/{},
                               enclosingFirstprivateVars);
}

mlir::LogicalResult CIRGenFunction::emitOMPDistributeParallelWsloopDirective(
    const OMPLoopDirective &s, llvm::StringRef directiveName, bool simd) {
  CIRGenFunction &cgf = *this;
  auto loopInfo = getOpenMPSimpleCanonicalForInfo(s);
  if (!loopInfo) {
    std::string feature = directiveName.str();
    feature += " for non-canonical/non-unit-stride loop";
    cgf.getCIRGenModule().errorNYI(s.getSourceRange(), feature);
    return mlir::failure();
  }

  bool hasCancel = false;
  if (!simd)
    if (const auto *distributeParallelFor =
            dyn_cast<OMPDistributeParallelForDirective>(&s)) {
      hasCancel = distributeParallelFor->hasCancel();
    }

  mlir::LogicalResult res = mlir::success();
  llvm::SmallVector<mlir::Type> retTy;
  llvm::SmallVector<mlir::Value> operands;
  mlir::Location begin = cgf.getLoc(s.getBeginLoc());
  mlir::Location end = cgf.getLoc(s.getEndLoc());
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  llvm::SmallVector<OpenMPCopyinMasterAddr, 4> copyinMasterAddrs =
      collectOpenMPCopyinMasterAddresses(cgf, s);

  llvm::SmallVector<const OMPClause *, 8> distributeClauses;
  llvm::SmallVector<const OMPClause *, 8> parallelClauses;
  llvm::SmallVector<const OMPClause *, 8> wsloopClauses;
  llvm::SmallVector<const OMPClause *, 8> simdClauses;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if: {
      const auto *ifClause = cast<OMPIfClause>(clause);
      if (ifClause->getNameModifier() == llvm::omp::OMPD_unknown ||
          ifClause->getNameModifier() == llvm::omp::OMPD_parallel) {
        parallelClauses.push_back(clause);
        break;
      }
      if (simd && ifClause->getNameModifier() == llvm::omp::OMPD_simd) {
        simdClauses.push_back(clause);
        break;
      }
      std::string feature = directiveName.str();
      feature += " if clause modifier";
      cgf.getCIRGenModule().errorNYI(clause->getBeginLoc(), feature);
      return mlir::failure();
    }
    case llvm::omp::OMPC_num_threads:
    case llvm::omp::OMPC_severity:
    case llvm::omp::OMPC_message:
    case llvm::omp::OMPC_proc_bind:
    case llvm::omp::OMPC_firstprivate:
    case llvm::omp::OMPC_copyin:
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_dist_schedule:
      distributeClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_private:
    case llvm::omp::OMPC_lastprivate:
    case llvm::omp::OMPC_reduction:
    case llvm::omp::OMPC_schedule:
    case llvm::omp::OMPC_ordered:
      wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_order:
      if (simd)
        simdClauses.push_back(clause);
      else
        wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_linear:
      if (simd) {
        wsloopClauses.push_back(clause);
        break;
      }
      [[fallthrough]];
    case llvm::omp::OMPC_aligned:
    case llvm::omp::OMPC_nontemporal:
    case llvm::omp::OMPC_safelen:
    case llvm::omp::OMPC_simdlen:
      if (simd) {
        simdClauses.push_back(clause);
        break;
      }
      [[fallthrough]];
    case llvm::omp::OMPC_default:
    case llvm::omp::OMPC_shared:
    case llvm::omp::OMPC_collapse:
      if (clause->getClauseKind() == llvm::omp::OMPC_default ||
          clause->getClauseKind() == llvm::omp::OMPC_shared ||
          clause->getClauseKind() == llvm::omp::OMPC_collapse)
        break;
      [[fallthrough]];
    default: {
      std::string feature = directiveName.str();
      feature += " clause";
      cgf.getCIRGenModule().errorNYI(
          clause->getBeginLoc(), feature,
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      return mlir::failure();
    }
    }
  }

  llvm::SmallPtrSet<const VarDecl *, 4> materializedClauseVars;
  if (simd)
    materializeOpenMPSimdCompositeClauseExprs(cgf, simdClauses,
                                              materializedClauseVars);
  const Expr *wsloopScheduleChunkExpr = getOpenMPScheduleChunkExpr(wsloopClauses);
  bool wsloopScheduleChunkNeedsRematerialization =
      wsloopScheduleChunkExpr &&
      exprContainsOpenMPCapturedExpr(wsloopScheduleChunkExpr);

  mlir::Operation *insertionParent =
      builder.getInsertionBlock() ? builder.getInsertionBlock()->getParentOp()
                                  : nullptr;
  bool insideTargetRegion =
      insertionParent && insertionParent->getParentOfType<mlir::omp::TargetOp>();
  llvm::SmallPtrSet<const VarDecl *, 4> rematerializedClauseVars;
  llvm::SmallVector<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>, 8>
      rematerializedClauseGuards;
  if (insideTargetRegion) {
    rematerializeOpenMPClauseExprs(cgf, parallelClauses,
                                   rematerializedClauseVars,
                                   rematerializedClauseGuards);
    rematerializeOpenMPClauseExprs(cgf, wsloopClauses,
                                   rematerializedClauseVars,
                                   rematerializedClauseGuards);
    if (simd)
      rematerializeOpenMPClauseExprs(cgf, simdClauses,
                                     rematerializedClauseVars,
                                     rematerializedClauseGuards);
  }
  if (wsloopScheduleChunkNeedsRematerialization && !insideTargetRegion)
    rematerializeOpenMPCapturedExprTree(cgf, wsloopScheduleChunkExpr,
                                        rematerializedClauseVars,
                                        rematerializedClauseGuards);
  mlir::Value wsloopScheduleChunkValue;
  if (wsloopScheduleChunkExpr &&
      (insideTargetRegion || wsloopScheduleChunkNeedsRematerialization))
    wsloopScheduleChunkValue =
        emitOpenMPIntegerExpr(cgf,
                              unwrapOpenMPCapturedExprInit(
                                  wsloopScheduleChunkExpr));

  auto parallelOp =
      mlir::omp::ParallelOp::create(builder, begin, retTy, operands);
  parallelOp.setComposite(true);
  OpenMPClauseEmissionInfo parallelClauseInfo =
      emitOpenMPClauses(parallelOp, parallelClauses);
  const CapturedStmt *parallelCaptured =
      s.getCapturedStmt(OpenMPDirectiveKind::OMPD_parallel);
  assert(parallelCaptured && "expected OpenMP parallel captured stmt");
  parallelOp->setAttr("cir.instrprof_func_name",
                      cgm.getStringAttrCached(
                          getOpenMPHelperPGOName(*parallelCaptured)));
  parallelOp->setAttr("cir.instrprof_func_linkage",
                      builder.getI32IntegerAttr(static_cast<int32_t>(
                          llvm::GlobalValue::InternalLinkage)));

  CharUnits ivAlign = cgf.getContext().getDeclAlign(loopInfo->iv);
  mlir::Type ivType = cgf.convertTypeForMem(loopInfo->iv->getType());
  Address ivSeedRawAlloca = Address::invalid();
  Address ivSeedAddr = cgf.createTempAlloca(
      ivType, ivAlign, cgf.getLoc(loopInfo->iv->getLocation()),
      loopInfo->iv->getName(), /*arraySize=*/nullptr, &ivSeedRawAlloca);
  if (!cgf.localDeclMap.count(loopInfo->iv))
    cgf.declare(ivSeedRawAlloca.getPointer(), loopInfo->iv,
                loopInfo->iv->getType(),
                cgf.getLoc(loopInfo->iv->getSourceRange()), ivAlign);
  unsigned ivParallelPrivateIndex = parallelOp.getPrivateVars().size();
  auto ivPrivatizer = getOrCreateOpenMPPrivatePrivatizer(
      cgf, builder, cgf.getLoc(loopInfo->iv->getLocation()), loopInfo->iv,
      ivType);
  parallelOp.getPrivateVarsMutable().append(ivSeedAddr.getPointer());
  llvm::SmallVector<mlir::Attribute, 4> parallelPrivateSyms;
  if (auto existingSyms = parallelOp.getPrivateSyms())
    llvm::append_range(parallelPrivateSyms, *existingSyms);
  parallelPrivateSyms.push_back(mlir::SymbolRefAttr::get(ivPrivatizer));
  parallelOp.setPrivateSymsAttr(
      mlir::ArrayAttr::get(builder.getContext(), parallelPrivateSyms));
  parallelClauseInfo.privateIndices.push_back(ivParallelPrivateIndex);
  parallelClauseInfo.privateVars.push_back(loopInfo->iv);
  if (isInsideOpenMPTargetRegion(cgf) &&
      mlir::failed(appendOpenMPParallelNestedSyntheticFirstprivatePrivates(
          cgf, parallelOp, wsloopClauses, parallelClauseInfo)))
    return mlir::failure();

  bool distScheduleStatic = false;
  const Expr *distScheduleChunkExpr = nullptr;
  for (const OMPClause *clause : distributeClauses) {
    const auto *distScheduleClause = cast<OMPDistScheduleClause>(clause);
    if (distScheduleClause->getDistScheduleKind() !=
        OMPC_DIST_SCHEDULE_static) {
      std::string feature = directiveName.str();
      feature += " dist_schedule kind";
      cgf.getCIRGenModule().errorNYI(clause->getBeginLoc(), feature);
      return mlir::failure();
    }
    distScheduleStatic = true;
    if (const Expr *chunk = distScheduleClause->getChunkSize())
      distScheduleChunkExpr = chunk;
  }

  {
    mlir::OpBuilder::InsertionGuard guardCase(builder);
    llvm::SmallVector<mlir::Type, 4> privateArgTypes;
    llvm::SmallVector<mlir::Location, 4> privateArgLocs;
    for (mlir::Value privateVar : parallelOp.getPrivateVars()) {
      privateArgTypes.push_back(privateVar.getType());
      privateArgLocs.push_back(privateVar.getLoc());
    }

    mlir::Block *block = builder.createBlock(&parallelOp.getRegion(), {},
                                             privateArgTypes, privateArgLocs);
    builder.setInsertionPointToEnd(block);

    auto pgoScope = cgf.enterOpenMPHelperRegionCounters(*parallelCaptured);
    std::unique_ptr<CIRGenFunction::OpenMPNontemporalDeclsRAII> nontemporalDecls;
    if (simd)
      nontemporalDecls = std::make_unique<
          CIRGenFunction::OpenMPNontemporalDeclsRAII>(cgf, s);
    LexicalScope ls{cgf, begin, builder.getInsertionBlock()};
    llvm::SmallVector<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>, 4>
        parallelPrivateDeclGuards;
    llvm::SmallVector<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>, 4>
        wsloopPrivateDeclGuards;
    llvm::SmallVector<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>, 4>
        reductionDeclGuards;
    llvm::DenseMap<const VarDecl *, Address> parallelOriginalDecls;
    llvm::DenseMap<const VarDecl *, Address> wsloopOriginalDecls;
    if (mlir::failed(remapOpenMPPrivateDecls(
            cgf, parallelOp.getOperation(), parallelClauseInfo,
            parallelPrivateDeclGuards, &parallelOriginalDecls)))
      return mlir::failure();

    OpenMPTLSCopyinDeclMapScope tlsCopyinDecls(cgf, s);
    (void)emitOpenMPCopyinClause(cgf, s, copyinMasterAddrs);
    bool hasDisabledLastprivateConditional =
        cgf.pushDisabledOpenMPLastprivateConditional(s);
    auto popDisabledLastprivateConditional = llvm::scope_exit([&] {
      if (hasDisabledLastprivateConditional)
        cgf.popOpenMPLastprivateConditional();
    });
    cgf.incrementProfileCounter(parallelCaptured->getCapturedDecl()->getBody());
    CIRGenFunction::DeclMapRevertingRAII ivMapGuard(cgf, loopInfo->iv);

    OpenMPSimdCompositeClauseData simdClauseData;
    if (simd && mlir::failed(collectOpenMPSimdClauseDataForComposite(
                    cgf, simdClauses, simdClauseData)))
      return mlir::failure();

    mlir::Value lowerBound = emitOpenMPIntegerExpr(cgf, loopInfo->lowerBound);
    mlir::Value upperBound = emitOpenMPIntegerExpr(cgf, loopInfo->upperBound);
    mlir::Type loopTy = lowerBound.getType();
    if (loopTy != upperBound.getType()) {
      std::string feature = directiveName.str();
      feature += " with mismatched loop bound types";
      cgf.getCIRGenModule().errorNYI(s.getSourceRange(), feature);
      return mlir::failure();
    }
    mlir::Value step = createOpenMPIntegerConstant(cgf, begin, loopTy, 1);
    mlir::Value distScheduleChunk;
    if (distScheduleChunkExpr)
      distScheduleChunk = emitOpenMPIntegerExpr(cgf, distScheduleChunkExpr);

    mlir::omp::DistributeOperands distributeClauseOps;
    auto distributeOp =
        mlir::omp::DistributeOp::create(builder, begin, distributeClauseOps);
    distributeOp.setComposite(true);
    if (distScheduleStatic)
      distributeOp.setDistScheduleStatic(true);
    if (distScheduleChunk)
      distributeOp.getDistScheduleChunkSizeMutable().assign(distScheduleChunk);

    {
      mlir::OpBuilder::InsertionGuard guard(builder);
      mlir::Block *distributeBlock =
          builder.createBlock(&distributeOp.getRegion());
      builder.setInsertionPoint(distributeOp);
      auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
      if (!hasCancel)
        wsloopOp.setNowait(true);
      wsloopOp.setComposite(true);
      if (wsloopScheduleChunkValue)
        wsloopOp.getScheduleChunkMutable().assign(wsloopScheduleChunkValue);
      OpenMPClauseEmissionInfo wsloopClauseInfo =
          emitOpenMPClauses(wsloopOp, wsloopClauses,
                            parallelClauseInfo.privateVars,
                            /*allowWsloopConditionalLastprivate=*/true);
      wsloopOp->moveBefore(distributeBlock, distributeBlock->end());

      llvm::SmallVector<mlir::Type, 4> wsloopPrivateArgTypes;
      llvm::SmallVector<mlir::Location, 4> wsloopPrivateArgLocs;
      for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
        wsloopPrivateArgTypes.push_back(privateVar.getType());
        wsloopPrivateArgLocs.push_back(privateVar.getLoc());
      }
      llvm::SmallVector<mlir::Type, 4> wsloopReductionArgTypes;
      llvm::SmallVector<mlir::Location, 4> wsloopReductionArgLocs;
      for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
        wsloopReductionArgTypes.push_back(reductionVar.getType());
        wsloopReductionArgLocs.push_back(reductionVar.getLoc());
      }
      wsloopPrivateArgTypes.append(wsloopReductionArgTypes);
      wsloopPrivateArgLocs.append(wsloopReductionArgLocs);
      mlir::Block *wsloopBlock = builder.createBlock(
          &wsloopOp.getRegion(), {}, wsloopPrivateArgTypes,
          wsloopPrivateArgLocs);
      builder.setInsertionPointToEnd(wsloopBlock);

      if (simd) {
        mlir::ArrayAttr alignments =
            simdClauseData.alignments.empty()
                ? mlir::ArrayAttr{}
                : mlir::ArrayAttr::get(builder.getContext(),
                                       simdClauseData.alignments);
        mlir::ArrayAttr linearVarTypes =
            simdClauseData.linearVarTypes.empty()
                ? mlir::ArrayAttr{}
                : mlir::ArrayAttr::get(builder.getContext(),
                                       simdClauseData.linearVarTypes);
        auto simdOp = mlir::omp::SimdOp::create(
            builder, begin, simdClauseData.alignedVars, alignments,
            simdClauseData.ifExpr, simdClauseData.linearVars,
            simdClauseData.linearStepVars, linearVarTypes,
            simdClauseData.nontemporalVars, simdClauseData.order,
            simdClauseData.orderMod,
            /*private_vars=*/mlir::ValueRange{},
            /*private_syms=*/mlir::ArrayAttr{},
            /*private_needs_barrier=*/false,
            /*reduction_mod=*/mlir::omp::ReductionModifierAttr{},
            /*reduction_vars=*/mlir::ValueRange{},
            /*reduction_byref=*/mlir::DenseBoolArrayAttr{},
            /*reduction_syms=*/mlir::ArrayAttr{}, simdClauseData.safelen,
            simdClauseData.simdlen);
        simdOp.setComposite(true);
        mlir::Block *simdBlock = builder.createBlock(&simdOp.getRegion());
        builder.setInsertionPointToEnd(simdBlock);
      }

      mlir::omp::LoopNestOperands loopClauseOps;
      loopClauseOps.loopLowerBounds.push_back(lowerBound);
      loopClauseOps.loopUpperBounds.push_back(upperBound);
      loopClauseOps.loopSteps.push_back(step);
      if (loopInfo->upperInclusive)
        loopClauseOps.loopInclusive = builder.getUnitAttr();

      auto loopNestOp =
          mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
      loopNestOp->setAttr(
          "omp.frontend.iv_signed",
          builder.getDenseBoolArrayAttr(
              {loopInfo->iv->getType()->hasSignedIntegerRepresentation()}));
      mlir::Block *loopBlock = builder.createBlock(
          &loopNestOp.getRegion(), {}, {loopTy},
          {cgf.getLoc(loopInfo->iv->getLocation())});
      builder.setInsertionPointToEnd(loopBlock);
      auto wsloopPrivateArgs =
          mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
              wsloopOp.getOperation())
              .getPrivateBlockArgs();
      if (mlir::failed(remapOpenMPPrivateDecls(
              cgf, wsloopOp.getOperation(), wsloopClauseInfo,
              wsloopPrivateDeclGuards, &wsloopOriginalDecls)))
        return mlir::failure();
      remapOpenMPReductionDecls(cgf, wsloopOp.getOperation(),
                                wsloopClauseInfo, reductionDeclGuards);

      mlir::Value ivValue = castOpenMPValueToCIRType(
          cgf, cgf.getLoc(loopInfo->iv->getLocation()),
          loopBlock->getArgument(0), ivType);
      auto ivIt = cgf.localDeclMap.find(loopInfo->iv);
      assert(ivIt != cgf.localDeclMap.end() &&
             "expected loop IV address to be mapped");
      builder.createStore(cgf.getLoc(loopInfo->iv->getLocation()), ivValue,
                          ivIt->second);

      bool hasConditionalLastprivate = cgf.pushOpenMPLastprivateConditional(
          wsloopClauses, ivIt->second, s.getBeginLoc());
      OpenMPScanParentDirectiveRAII scanDirectiveScope(
          cgf, s, collectOpenMPReductionRegionArgs(wsloopOp));
      res = emitOMPLoopBody(cgf, builder, begin, loopInfo->forStmt->getBody());
      if (!wsloopClauseInfo.lastprivateIndices.empty()) {
        assert(wsloopClauseInfo.lastprivateIndices.size() ==
                   wsloopClauseInfo.lastprivateVars.size() &&
               "mismatched OpenMP wsloop lastprivate metadata");
        assert(wsloopClauseInfo.lastprivateIndices.size() ==
                   wsloopClauseInfo.lastprivateConditionals.size() &&
               "mismatched OpenMP wsloop conditional-lastprivate metadata");
        mlir::Location ivLoc = cgf.getLoc(loopInfo->iv->getLocation());
        auto cirIVTy = mlir::dyn_cast<cir::IntType>(ivType);
        assert(cirIVTy && "expected CIR integer type for loop IV");
        mlir::Value upperBoundValue =
            castOpenMPValueToCIRType(cgf, ivLoc, upperBound, ivType);
        mlir::Value stepValue = builder.getConstInt(ivLoc, cirIVTy, 1);
        mlir::Value nextIV = builder.createAdd(ivLoc, ivValue, stepValue);
        mlir::Value isLastIteration = builder.createCompare(
            ivLoc, loopInfo->upperInclusive ? cir::CmpOpKind::gt
                                            : cir::CmpOpKind::ge,
            nextIV, upperBoundValue);

        cir::IfOp::create(
            builder, ivLoc, isLastIteration, /*withElseRegion=*/false,
            [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
              mlir::OpBuilder::InsertionGuard guard(builder);
              builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
              for (auto [index, vd, srcVD, destVD, assignOp, isConditional] :
                   llvm::zip_equal(
                       wsloopClauseInfo.lastprivateIndices,
                       wsloopClauseInfo.lastprivateVars,
                       wsloopClauseInfo.lastprivateSourceVDs,
                       wsloopClauseInfo.lastprivateDestVDs,
                       wsloopClauseInfo.lastprivateAssignmentOps,
                       wsloopClauseInfo.lastprivateConditionals)) {
                assert(index < wsloopPrivateArgs.size() &&
                       "unexpected OpenMP wsloop lastprivate block arg index");
                Address srcAddr = getOpenMPPrivateStorageAddress(cgf, vd);
                const VarDecl *canonicalVD = vd->getCanonicalDecl();
                Address destAddr = Address::invalid();
                if (auto parallelIt = parallelOriginalDecls.find(canonicalVD);
                    parallelIt != parallelOriginalDecls.end()) {
                  destAddr = parallelIt->second;
                } else if (auto wsloopIt = wsloopOriginalDecls.find(canonicalVD);
                           wsloopIt != wsloopOriginalDecls.end()) {
                  destAddr = wsloopIt->second;
                }
                assert(destAddr.isValid() &&
                       "missing lastprivate destination address");
                if (isConditional)
                  cgf.emitOpenMPLastprivateConditionalFinalUpdate(
                      vd, srcAddr, vd->getLocation());
                emitOpenMPLastprivateCopy(cgf, vd, srcAddr, destAddr, srcVD,
                                         destVD, assignOp);
              }
              cir::YieldOp::create(thenBuilder, thenLoc);
            });
      }
      if (hasConditionalLastprivate)
        cgf.popOpenMPLastprivateConditional();
      mlir::omp::YieldOp::create(builder, end);
      builder.setInsertionPointAfter(distributeOp);
      reductionDeclGuards.clear();
    }

    mlir::omp::TerminatorOp::create(builder, end);
  }

  if (mlir::succeeded(res))
    checkForOpenMPLastprivateConditionalUpdate(cgf, s);
  return res;
}

mlir::LogicalResult
CIRGenFunction::emitOMPTeamsDirective(const OMPTeamsDirective &s) {
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  auto teamsOp = mlir::omp::TeamsOp::create(
      builder, begin,
      /*allocate_vars=*/mlir::ValueRange{},
      /*allocator_vars=*/mlir::ValueRange{},
      /*if_expr=*/mlir::Value{},
      /*num_teams_lower=*/mlir::Value{},
      /*num_teams_upper=*/mlir::Value{},
      /*private_vars=*/mlir::ValueRange{},
      /*private_syms=*/mlir::ArrayAttr{},
      /*private_needs_barrier=*/false,
      /*reduction_mod=*/mlir::omp::ReductionModifierAttr{},
      /*reduction_vars=*/mlir::ValueRange{},
      /*reduction_byref=*/mlir::DenseBoolArrayAttr{},
      /*reduction_syms=*/mlir::ArrayAttr{},
      /*thread_limit=*/mlir::Value{});
  OpenMPClauseEmissionInfo clauseInfo = emitOpenMPClauses(teamsOp, s.clauses());

  return emitOMPTeamsRegionBody(
      *this, teamsOp, clauseInfo,
      s.getCapturedStmt(llvm::omp::OMPD_teams)->getCapturedStmt(), begin, end);
}
mlir::LogicalResult CIRGenFunction::emitOMPCancellationPointDirective(
    const OMPCancellationPointDirective &s) {
  if (!s.clauses().empty()) {
    getCIRGenModule().errorNYI(
        s.getSourceRange(), "OpenMP OMPCancellationPointDirective clauses");
    return mlir::failure();
  }

  mlir::omp::CancellationPointOp::create(
      builder, getLoc(s.getBeginLoc()),
      getOpenMPCancellationConstructType(*this, s.getSourceRange(),
                                         s.getCancelRegion()));
  return mlir::success();
}
mlir::LogicalResult
CIRGenFunction::emitOMPCancelDirective(const OMPCancelDirective &s) {
  const Expr *ifCond = nullptr;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if: {
      const auto *ifClause = cast<OMPIfClause>(clause);
      OpenMPDirectiveKind modifier = ifClause->getNameModifier();
      if (modifier != llvm::omp::OMPD_unknown &&
          modifier != llvm::omp::OMPD_cancel) {
        getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                   "OMPIfClause modifier on cancel directive");
        break;
      }
      ifCond = ifClause->getCondition();
      break;
    }
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), "OpenMP cancel clause",
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      break;
    }
  }

  mlir::omp::CancelOp::create(
      builder, getLoc(s.getBeginLoc()),
      getOpenMPCancellationConstructType(*this, s.getSourceRange(),
                                         s.getCancelRegion()),
      ifCond ? emitOpenMPBoolExpr(*this, ifCond) : mlir::Value{});
  return mlir::success();
}
mlir::LogicalResult
CIRGenFunction::emitOMPTargetDataDirective(const OMPTargetDataDirective &s) {
  // Match classic codegen: without target devices, target data emits its body
  // directly and none of its data-mapping clause expressions are evaluated.
  if (getLangOpts().OMPTargetTriples.empty())
    return emitStmt(s.getInnermostCapturedStmt()->getCapturedStmt(),
                    /*useCurrentScope=*/true);

  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTargetDataDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetEnterDataDirective(
    const OMPTargetEnterDataDirective &s) {
  // Match classic codegen: without target devices, target enter data is a
  // no-op and none of its clause expressions are evaluated.
  if (getLangOpts().OMPTargetTriples.empty())
    return mlir::success();

  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTargetEnterDataDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetExitDataDirective(
    const OMPTargetExitDataDirective &s) {
  // Match classic codegen: without target devices, target exit data is a
  // no-op and none of its clause expressions are evaluated.
  if (getLangOpts().OMPTargetTriples.empty())
    return mlir::success();

  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTargetExitDataDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetParallelDirective(
    const OMPTargetParallelDirective &s) {
  const bool isTargetDevice =
      getLangOpts().OpenMPIsTargetDevice && !forceOpenMPTargetOpEmission;
  const bool hasOffloadTargets = !getLangOpts().OMPTargetTriples.empty();
  const bool useTargetOffload = hasOffloadTargets && !isTargetDevice;
  const bool hasTargetAllocatorClauses = llvm::any_of(
      s.clauses(), [](const OMPClause *clause) {
        return clause->getClauseKind() == llvm::omp::OMPC_uses_allocators ||
               clause->getClauseKind() == llvm::omp::OMPC_allocate;
      });

  llvm::SmallVector<const OMPClause *, 8> targetClauses;
  llvm::SmallVector<const OMPClause *, 4> targetReductionClauses;
  llvm::SmallVector<const OMPClause *, 8> parallelClauses;
  llvm::SmallVector<const VarDecl *, 8> targetSyntheticFirstprivateVars;
  llvm::SmallVector<const Expr *, 2> hostOnlyTargetSideEffectExprs;
  OpenMPTargetParallelHostEvalClauseInfo parallelHostEvalClauses;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_depend:
    case llvm::omp::OMPC_nowait:
    case llvm::omp::OMPC_uses_allocators:
    case llvm::omp::OMPC_allocate:
    case llvm::omp::OMPC_thread_limit:
      targetClauses.push_back(clause);
      if (clause->getClauseKind() == llvm::omp::OMPC_thread_limit &&
          !useTargetOffload)
        hostOnlyTargetSideEffectExprs.push_back(
            cast<OMPThreadLimitClause>(clause)->getThreadLimit().front());
      break;
    case llvm::omp::OMPC_firstprivate:
    case llvm::omp::OMPC_private:
      if (useTargetOffload || hasTargetAllocatorClauses)
        targetClauses.push_back(clause);
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_if: {
      const auto *ifClause = cast<OMPIfClause>(clause);
      if (ifClause->getNameModifier() == llvm::omp::OMPD_unknown ||
          ifClause->getNameModifier() == llvm::omp::OMPD_target) {
        if (useTargetOffload)
          targetClauses.push_back(clause);
        break;
      }
      if (ifClause->getNameModifier() == llvm::omp::OMPD_parallel) {
        if (useTargetOffload)
          parallelHostEvalClauses.IfClause = ifClause;
        else
          parallelClauses.push_back(clause);
        break;
      }
      getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                 "OpenMP target parallel if clause modifier");
      break;
    }
    case llvm::omp::OMPC_device:
    case llvm::omp::OMPC_map:
    case llvm::omp::OMPC_is_device_ptr:
    case llvm::omp::OMPC_has_device_addr:
      if (useTargetOffload)
        targetClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_num_threads:
      if (useTargetOffload) {
        parallelHostEvalClauses.NumThreadsClause =
            cast<OMPNumThreadsClause>(clause);
        break;
      }
      [[fallthrough]];
    case llvm::omp::OMPC_severity:
    case llvm::omp::OMPC_message:
    case llvm::omp::OMPC_proc_bind:
    case llvm::omp::OMPC_copyin:
    case llvm::omp::OMPC_default:
    case llvm::omp::OMPC_shared:
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_reduction:
      parallelClauses.push_back(clause);
      targetReductionClauses.push_back(clause);
      break;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), "OpenMP target parallel clause",
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      break;
    }
  }

  if (useTargetOffload || !targetClauses.empty())
    targetClauses.append(targetReductionClauses.begin(),
                         targetReductionClauses.end());

  if (useTargetOffload || !targetClauses.empty())
    collectOpenMPClauseSyntheticFirstprivateVars(parallelClauses,
                                                 targetSyntheticFirstprivateVars);

  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  mlir::omp::TargetOp targetOp;
  auto emitParallelBody = [&](bool insideTargetRegion) -> mlir::LogicalResult {
    llvm::SmallPtrSet<const VarDecl *, 4> rematerializedClauseVars;
    llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
        rematerializedClauseGuards;
    if (insideTargetRegion)
      rematerializeOpenMPClauseExprs(*this, parallelClauses,
                                     rematerializedClauseVars,
                                     rematerializedClauseGuards);

    llvm::SmallVector<mlir::Type> retTy;
    llvm::SmallVector<mlir::Value> operands;
    llvm::SmallVector<OpenMPCopyinMasterAddr, 4> copyinMasterAddrs =
        collectOpenMPCopyinMasterAddresses(*this, s);

    auto parallelOp =
        mlir::omp::ParallelOp::create(builder, begin, retTy, operands);
    OpenMPClauseEmissionInfo clauseInfo =
        emitOpenMPClauses(parallelOp, parallelClauses);
    if (insideTargetRegion && useTargetOffload)
      bindOpenMPTargetParallelHostEvalArgs(targetOp, parallelOp,
                                           parallelHostEvalClauses);
    const CapturedStmt *parallelCaptured =
        s.getCapturedStmt(OpenMPDirectiveKind::OMPD_parallel);
    assert(parallelCaptured && "expected OpenMP parallel captured stmt");
    parallelOp->setAttr("cir.instrprof_func_name",
                        cgm.getStringAttrCached(
                            getOpenMPHelperPGOName(*parallelCaptured)));
    parallelOp->setAttr("cir.instrprof_func_linkage",
                        builder.getI32IntegerAttr(static_cast<int32_t>(
                            llvm::GlobalValue::InternalLinkage)));

    return emitOMPPrivateRegionBody(
        *this, parallelOp, clauseInfo, begin, end,
        [&]() -> mlir::LogicalResult {
          auto pgoScope = enterOpenMPHelperRegionCounters(*parallelCaptured);
          OpenMPTLSCopyinDeclMapScope tlsCopyinDecls(*this, s);
          (void)emitOpenMPCopyinClause(*this, s, copyinMasterAddrs);
          incrementProfileCounter(parallelCaptured->getCapturedDecl()->getBody());
          return emitStmt(parallelCaptured->getCapturedStmt(),
                          /*useCurrentScope=*/true);
        });
  };

  llvm::SmallPtrSet<const VarDecl *, 4> materializedClauseVars;
  llvm::SmallVector<mlir::Value, 2> hostEvalVars;
  if (!useTargetOffload) {
    for (const OMPClause *clause : s.clauses()) {
      if (clause->getClauseKind() != llvm::omp::OMPC_device)
        continue;
      materializeOpenMPCapturedClauseExpr(
          *this, cast<OMPDeviceClause>(clause)->getDevice(),
          materializedClauseVars);
    }
    for (const Expr *expr : hostOnlyTargetSideEffectExprs)
      materializeOpenMPCapturedClauseExpr(*this, expr, materializedClauseVars);
    if (targetClauses.empty())
      return emitParallelBody(/*insideTargetRegion=*/false);

    materializeOMPTargetClauseExprs(*this, parallelClauses,
                                    materializedClauseVars);
  } else {
    materializeOMPTargetClauseExprs(*this, targetClauses,
                                    materializedClauseVars);
    materializeOpenMPTargetParallelHostEvalClauseExprs(
        *this, parallelHostEvalClauses, materializedClauseVars);
    hostEvalVars =
        emitOpenMPTargetParallelHostEvalVars(*this, parallelHostEvalClauses);
  }

  targetOp = mlir::omp::TargetOp::create(
      builder, begin,
      /*allocate_vars=*/mlir::ValueRange{},
      /*allocator_vars=*/mlir::ValueRange{},
      /*bare=*/false,
      /*depend_kinds=*/mlir::ArrayAttr{},
      /*depend_vars=*/mlir::ValueRange{},
      /*depend_sizes=*/mlir::ValueRange{},
      /*device=*/mlir::Value{},
      /*has_device_addr_vars=*/mlir::ValueRange{},
      /*host_eval_vars=*/hostEvalVars,
      /*if_expr=*/mlir::Value{},
      /*in_reduction_vars=*/mlir::ValueRange{},
      /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
      /*in_reduction_syms=*/mlir::ArrayAttr{},
      /*is_device_ptr_vars=*/mlir::ValueRange{},
      /*map_vars=*/mlir::ValueRange{},
      /*nowait=*/false,
      /*private_vars=*/mlir::ValueRange{},
      /*private_syms=*/mlir::ArrayAttr{},
      /*private_needs_barrier=*/false,
      /*thread_limit=*/mlir::Value{},
      /*private_maps=*/mlir::DenseI64ArrayAttr{});
  annotateOpenMPTargetParentName(*this, targetOp);
  OpenMPClauseEmissionInfo targetClauseInfo =
      emitOpenMPClauses(targetOp, targetClauses);
  targetSyntheticFirstprivateVars = filterOpenMPSyntheticFirstprivateVars(
      targetSyntheticFirstprivateVars, targetClauseInfo);
  if (mlir::failed(appendOpenMPSyntheticFirstprivatePrivates(
          *this, targetOp, targetSyntheticFirstprivateVars, targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(appendOpenMPTargetCapturedVLASizePrivates(
          *this, targetOp,
          *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target),
          targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(maybeEmitOpenMPRuntimeDependPayload(
          *this, targetOp, s, targetClauseInfo)))
    return mlir::failure();
  if (!useTargetOffload && !isTargetDevice)
    if (mlir::failed(appendHostOnlyOMPTargetCapturedMappings(
            *this, targetOp, targetClauseInfo,
            *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target))))
      return mlir::failure();
  if (mlir::failed(finalizeOMPTargetPrivateMappings(*this, targetOp,
                                                    targetClauseInfo)))
    return mlir::failure();
  if (useTargetOffload)
    promoteOMPTargetPrivateMapsForOffload(targetOp, targetClauseInfo);

  return emitOMPTargetRegionBody(
      *this, targetOp, targetClauseInfo, begin, end,
      [&]() -> mlir::LogicalResult {
        return emitParallelBody(/*insideTargetRegion=*/true);
      });
}
mlir::LogicalResult CIRGenFunction::emitSimpleOMPTargetParallelWsloopDirective(
    const OMPLoopDirective &s, llvm::StringRef directiveName) {
  auto loopInfo = getOpenMPSimpleCanonicalForInfo(s);
  auto generatedLoopInfo =
      getOpenMPSimpleGeneratedLoopInfo(s);
  bool useGeneratedLoopInfo = !loopInfo;
  if (loopInfo && generatedLoopInfo &&
      loopInfo->lowerBound->getType().getCanonicalType() !=
          loopInfo->upperBound->getType().getCanonicalType()) {
    useGeneratedLoopInfo = true;
  }
  if (!loopInfo && !generatedLoopInfo) {
    std::string feature = directiveName.str();
    feature += " for non-canonical/non-unit-stride loop";
    getCIRGenModule().errorNYI(s.getSourceRange(), feature);
    return mlir::failure();
  }

  const bool isTargetDevice =
      getLangOpts().OpenMPIsTargetDevice && !forceOpenMPTargetOpEmission;
  const bool hasOffloadTargets = !getLangOpts().OMPTargetTriples.empty();
  const bool useTargetOffload = hasOffloadTargets && !isTargetDevice;
  const bool hasTargetAllocatorClauses = llvm::any_of(
      s.clauses(), [](const OMPClause *clause) {
        return clause->getClauseKind() == llvm::omp::OMPC_uses_allocators ||
               clause->getClauseKind() == llvm::omp::OMPC_allocate;
      });

  llvm::SmallVector<const OMPClause *, 8> targetClauses;
  llvm::SmallVector<const OMPClause *, 4> targetReductionClauses;
  llvm::SmallVector<const OMPClause *, 8> parallelClauses;
  llvm::SmallVector<const OMPClause *, 8> wsloopClauses;
  llvm::SmallVector<const VarDecl *, 8> targetFirstprivateVars;
  llvm::SmallVector<const Expr *, 2> hostOnlyTargetSideEffectExprs;
  OpenMPTargetParallelHostEvalClauseInfo parallelHostEvalClauses;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_depend:
    case llvm::omp::OMPC_nowait:
    case llvm::omp::OMPC_uses_allocators:
    case llvm::omp::OMPC_allocate:
    case llvm::omp::OMPC_thread_limit:
      targetClauses.push_back(clause);
      if (clause->getClauseKind() == llvm::omp::OMPC_thread_limit &&
          !useTargetOffload)
        hostOnlyTargetSideEffectExprs.push_back(
            cast<OMPThreadLimitClause>(clause)->getThreadLimit().front());
      break;
    case llvm::omp::OMPC_firstprivate:
      if (useTargetOffload || hasTargetAllocatorClauses) {
        targetClauses.push_back(clause);
        for (const Expr *varExpr : cast<OMPFirstprivateClause>(clause)->varlist())
          if (const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr))
            targetFirstprivateVars.push_back(vd);
      }
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_private:
      if (useTargetOffload || hasTargetAllocatorClauses)
        targetClauses.push_back(clause);
      wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_if: {
      const auto *ifClause = cast<OMPIfClause>(clause);
      if (ifClause->getNameModifier() == llvm::omp::OMPD_unknown ||
          ifClause->getNameModifier() == llvm::omp::OMPD_target) {
        if (useTargetOffload)
          targetClauses.push_back(clause);
        break;
      }
      if (ifClause->getNameModifier() == llvm::omp::OMPD_parallel) {
        if (useTargetOffload)
          parallelHostEvalClauses.IfClause = ifClause;
        else
          parallelClauses.push_back(clause);
        break;
      }
      std::string feature = directiveName.str();
      feature += " if clause modifier";
      getCIRGenModule().errorNYI(clause->getBeginLoc(), feature);
      break;
    }
    case llvm::omp::OMPC_device:
    case llvm::omp::OMPC_map:
    case llvm::omp::OMPC_is_device_ptr:
    case llvm::omp::OMPC_has_device_addr:
      if (useTargetOffload)
        targetClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_num_threads:
      if (useTargetOffload) {
        parallelHostEvalClauses.NumThreadsClause =
            cast<OMPNumThreadsClause>(clause);
        break;
      }
      [[fallthrough]];
    case llvm::omp::OMPC_severity:
    case llvm::omp::OMPC_message:
    case llvm::omp::OMPC_proc_bind:
    case llvm::omp::OMPC_copyin:
    case llvm::omp::OMPC_default:
    case llvm::omp::OMPC_shared:
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_bind:
      // Match OGCG: target parallel loop is emitted as target + parallel + for
      // regardless of bind kind, so this clause is accepted but does not alter
      // the generated CIR shape here.
      break;
    case llvm::omp::OMPC_lastprivate:
      if (useTargetOffload)
        targetClauses.push_back(clause);
      [[fallthrough]];
    case llvm::omp::OMPC_schedule:
    case llvm::omp::OMPC_order:
    case llvm::omp::OMPC_ordered:
    case llvm::omp::OMPC_collapse:
      wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_reduction:
      wsloopClauses.push_back(clause);
      targetReductionClauses.push_back(clause);
      break;
    default:
      std::string feature = directiveName.str();
      feature += " clause";
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), feature,
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      break;
    }
  }

  if (useTargetOffload || !targetClauses.empty())
    targetClauses.append(targetReductionClauses.begin(),
                         targetReductionClauses.end());

  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  mlir::omp::TargetOp targetOp;
  auto emitParallelLoopBody = [&](bool insideTargetRegion)
      -> mlir::LogicalResult {
    mlir::LogicalResult res = mlir::success();
    llvm::SmallPtrSet<const VarDecl *, 4> rematerializedClauseVars;
    llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 8>
        rematerializedClauseGuards;
    if (insideTargetRegion) {
      rematerializeOpenMPClauseExprs(*this, parallelClauses,
                                     rematerializedClauseVars,
                                     rematerializedClauseGuards);
      rematerializeOpenMPClauseExprs(*this, wsloopClauses,
                                     rematerializedClauseVars,
                                     rematerializedClauseGuards);
      if (!useGeneratedLoopInfo) {
        rematerializeOpenMPCapturedExpr(*this, loopInfo->lowerBound,
                                        rematerializedClauseVars,
                                        rematerializedClauseGuards);
        rematerializeOpenMPCapturedExpr(*this, loopInfo->upperBound,
                                        rematerializedClauseVars,
                                        rematerializedClauseGuards);
      }
    }
    mlir::Value wsloopScheduleChunkValue;
    if (insideTargetRegion)
      if (const Expr *wsloopScheduleChunkExpr =
              getOpenMPScheduleChunkExpr(wsloopClauses))
        wsloopScheduleChunkValue =
            emitOpenMPIntegerExpr(*this,
                                  unwrapOpenMPCapturedExprInit(
                                      wsloopScheduleChunkExpr));

    llvm::SmallVector<mlir::Type> retTy;
    llvm::SmallVector<mlir::Value> operands;
    llvm::SmallVector<OpenMPCopyinMasterAddr, 4> copyinMasterAddrs =
        collectOpenMPCopyinMasterAddresses(*this, s);

    auto parallelOp =
        mlir::omp::ParallelOp::create(builder, begin, retTy, operands);
    OpenMPClauseEmissionInfo parallelClauseInfo =
        emitOpenMPClauses(parallelOp, parallelClauses);
    if (insideTargetRegion && useTargetOffload)
      bindOpenMPTargetParallelHostEvalArgs(targetOp, parallelOp,
                                           parallelHostEvalClauses);
    const CapturedStmt *parallelCaptured =
        s.getCapturedStmt(OpenMPDirectiveKind::OMPD_parallel);
    assert(parallelCaptured && "expected OpenMP parallel captured stmt");
    parallelOp->setAttr("cir.instrprof_func_name",
                        cgm.getStringAttrCached(
                            getOpenMPHelperPGOName(*parallelCaptured)));
    parallelOp->setAttr("cir.instrprof_func_linkage",
                        builder.getI32IntegerAttr(static_cast<int32_t>(
                            llvm::GlobalValue::InternalLinkage)));
    mlir::Type ivType;
    Address ivSeedRawAlloca = Address::invalid();
    llvm::SmallVector<mlir::Attribute, 4> parallelPrivateSyms;
    if (auto existingSyms = parallelOp.getPrivateSyms())
      llvm::append_range(parallelPrivateSyms, *existingSyms);
    if (!useGeneratedLoopInfo) {
      CharUnits ivAlign = getContext().getDeclAlign(loopInfo->iv);
      ivType = convertTypeForMem(loopInfo->iv->getType());
      Address ivSeedAddr = createTempAlloca(
          ivType, ivAlign, getLoc(loopInfo->iv->getLocation()),
          loopInfo->iv->getName(), /*arraySize=*/nullptr, &ivSeedRawAlloca);
      if (!localDeclMap.count(loopInfo->iv))
        declare(ivSeedRawAlloca.getPointer(), loopInfo->iv,
                loopInfo->iv->getType(),
                getLoc(loopInfo->iv->getSourceRange()), ivAlign);
      unsigned ivParallelPrivateIndex = parallelOp.getPrivateVars().size();
      auto ivPrivatizer = getOrCreateOpenMPPrivatePrivatizer(
          *this, builder, getLoc(loopInfo->iv->getLocation()), loopInfo->iv,
          ivType);
      parallelOp.getPrivateVarsMutable().append(ivSeedAddr.getPointer());
      parallelPrivateSyms.push_back(mlir::SymbolRefAttr::get(ivPrivatizer));
      parallelClauseInfo.privateIndices.push_back(ivParallelPrivateIndex);
      parallelClauseInfo.privateVars.push_back(loopInfo->iv);
    }
    parallelOp.setPrivateSymsAttr(
        mlir::ArrayAttr::get(builder.getContext(), parallelPrivateSyms));
    if (insideTargetRegion &&
        mlir::failed(appendOpenMPParallelNestedSyntheticFirstprivatePrivates(
            *this, parallelOp, wsloopClauses, parallelClauseInfo)))
      return mlir::failure();

    {
      mlir::OpBuilder::InsertionGuard guardCase(builder);
      llvm::SmallVector<mlir::Type, 4> privateArgTypes;
      llvm::SmallVector<mlir::Location, 4> privateArgLocs;
      for (mlir::Value privateVar : parallelOp.getPrivateVars()) {
        privateArgTypes.push_back(privateVar.getType());
        privateArgLocs.push_back(privateVar.getLoc());
      }

      mlir::Block *block = builder.createBlock(&parallelOp.getRegion(), {},
                                               privateArgTypes, privateArgLocs);
      builder.setInsertionPointToEnd(block);

      cir::ScopeOp::create(
          builder, begin, /*scopeBuilder=*/
          [&](mlir::OpBuilder &, mlir::Location) {
            auto pgoScope = enterOpenMPHelperRegionCounters(*parallelCaptured);
            LexicalScope ls{*this, begin, builder.getInsertionBlock()};
            llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                parallelPrivateDeclGuards;
            llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                wsloopPrivateDeclGuards;
            llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                reductionDeclGuards;
            llvm::DenseMap<const VarDecl *, Address> parallelOriginalDecls;
            llvm::DenseMap<const VarDecl *, Address> wsloopOriginalDecls;
            if (mlir::failed(remapOpenMPPrivateDecls(
                    *this, parallelOp.getOperation(), parallelClauseInfo,
                    parallelPrivateDeclGuards, &parallelOriginalDecls))) {
              res = mlir::failure();
              return;
            }

            OpenMPTLSCopyinDeclMapScope tlsCopyinDecls(*this, s);
            (void)emitOpenMPCopyinClause(*this, s, copyinMasterAddrs);
            incrementProfileCounter(
                parallelCaptured->getCapturedDecl()->getBody());

            if (useGeneratedLoopInfo) {
              assert(generatedLoopInfo &&
                     "expected generated loop info for target parallel wsloop");

              if (mlir::failed(
                      emitOpenMPTransformPreInits(*this, s.getPreInits()))) {
                res = mlir::failure();
                return;
              }

              bool preCondConstant = false;
              if (generatedLoopInfo->preCond->EvaluateAsBooleanCondition(
                      preCondConstant, getContext()) &&
                  !preCondConstant)
                return;

              llvm::SmallVector<bool, 4> emitFinals;
              emitFinals.reserve(generatedLoopInfo->sourceIVs.size());
              for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs) {
                emitFinals.push_back(localDeclMap.count(sourceIV) ||
                                     sourceIV->hasGlobalStorage() ||
                                     (capturedStmtInfo &&
                                      capturedStmtInfo->lookup(sourceIV)));
              }

              auto emitGeneratedWsloop = [&]() -> mlir::LogicalResult {
                mlir::Value upperBound =
                    emitOpenMPIntegerExpr(*this, generatedLoopInfo->lastIteration);
                mlir::Type loopTy = upperBound.getType();
                mlir::Value lowerBound =
                    createOpenMPIntegerConstant(*this, begin, loopTy, 0);
                mlir::Value step =
                    createOpenMPIntegerConstant(*this, begin, loopTy, 1);

                auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
                wsloopOp.setNowait(true);
                if (wsloopScheduleChunkValue)
                  wsloopOp.getScheduleChunkMutable().assign(
                      wsloopScheduleChunkValue);
                OpenMPClauseEmissionInfo wsloopClauseInfo =
                    emitOpenMPClauses(wsloopOp, wsloopClauses,
                                      parallelClauseInfo.privateVars,
                                      /*allowWsloopConditionalLastprivate=*/true);
                llvm::SmallVector<mlir::Type, 4> wsloopPrivateArgTypes;
                llvm::SmallVector<mlir::Location, 4> wsloopPrivateArgLocs;
                for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
                  wsloopPrivateArgTypes.push_back(privateVar.getType());
                  wsloopPrivateArgLocs.push_back(privateVar.getLoc());
                }
                llvm::SmallVector<mlir::Type, 4> wsloopReductionArgTypes;
                llvm::SmallVector<mlir::Location, 4> wsloopReductionArgLocs;
                for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
                  wsloopReductionArgTypes.push_back(reductionVar.getType());
                  wsloopReductionArgLocs.push_back(reductionVar.getLoc());
                }
                wsloopPrivateArgTypes.append(wsloopReductionArgTypes);
                wsloopPrivateArgLocs.append(wsloopReductionArgLocs);
                mlir::Block *wsloopBlock = builder.createBlock(
                    &wsloopOp.getRegion(), {}, wsloopPrivateArgTypes,
                    wsloopPrivateArgLocs);
                builder.setInsertionPointToEnd(wsloopBlock);

                mlir::omp::LoopNestOperands loopClauseOps;
                loopClauseOps.loopLowerBounds.push_back(lowerBound);
                loopClauseOps.loopUpperBounds.push_back(upperBound);
                loopClauseOps.loopSteps.push_back(step);
                loopClauseOps.loopInclusive = builder.getUnitAttr();

                auto loopNestOp =
                    mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
                bool logicalIVSigned = generatedLoopInfo->logicalIV->getType()
                                           ->hasSignedIntegerRepresentation();
                loopNestOp->setAttr("omp.frontend.iv_signed",
                                    builder.getDenseBoolArrayAttr(
                                        {logicalIVSigned}));
                mlir::Block *loopBlock = builder.createBlock(
                    &loopNestOp.getRegion(), {}, {loopTy},
                    {getLoc(generatedLoopInfo->logicalIV->getLocation())});
                builder.setInsertionPointToEnd(loopBlock);

                auto createLoopVarStorage = [&](const VarDecl *vd) {
                  CharUnits vdAlign = getContext().getDeclAlign(vd);
                  mlir::Type vdType = convertTypeForMem(vd->getType());
                  Address rawAlloca = Address::invalid();
                  createTempAlloca(vdType, vdAlign, getLoc(vd->getLocation()),
                                   vd->getName(), /*arraySize=*/nullptr,
                                   &rawAlloca);
                  return std::pair{rawAlloca, vdType};
                };

                auto [logicalIVRawAlloca, logicalIVType] =
                    createLoopVarStorage(generatedLoopInfo->logicalIV);
                llvm::SmallVector<Address, 4> sourceIVRawAllocas;
                sourceIVRawAllocas.reserve(generatedLoopInfo->sourceIVs.size());
                for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs) {
                  auto [sourceIVRawAlloca, sourceIVType] =
                      createLoopVarStorage(sourceIV);
                  (void)sourceIVType;
                  sourceIVRawAllocas.push_back(sourceIVRawAlloca);
                }

                mlir::LogicalResult bodyRes = mlir::success();
                {
                  llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                      wsloopPrivateDeclGuards;
                  llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                      sourceIVMapGuards;
                  sourceIVMapGuards.reserve(generatedLoopInfo->sourceIVs.size());
                  for (auto [sourceIV, sourceIVRawAlloca] :
                       llvm::zip_equal(generatedLoopInfo->sourceIVs,
                                       sourceIVRawAllocas)) {
                    sourceIVMapGuards.push_back(
                        std::make_unique<DeclMapRevertingRAII>(*this, sourceIV));
                    replaceAddrOfLocalVar(sourceIV, sourceIVRawAlloca);
                  }
                  DeclMapRevertingRAII logicalIVMapGuard(
                      *this, generatedLoopInfo->logicalIV);
                  replaceAddrOfLocalVar(generatedLoopInfo->logicalIV,
                                        logicalIVRawAlloca);

                  auto wsloopPrivateArgs =
                      mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                          wsloopOp.getOperation())
                          .getPrivateBlockArgs();
                  if (mlir::failed(remapOpenMPPrivateDecls(
                          *this, wsloopOp.getOperation(), wsloopClauseInfo,
                          wsloopPrivateDeclGuards, &wsloopOriginalDecls))) {
                    res = mlir::failure();
                    return mlir::failure();
                  }
                  remapOpenMPReductionDecls(*this, wsloopOp.getOperation(),
                                            wsloopClauseInfo,
                                            reductionDeclGuards);

                  mlir::Value logicalIVValue = loopBlock->getArgument(0);
                  if (auto ompIVTy =
                          mlir::dyn_cast<mlir::IntegerType>(
                              logicalIVValue.getType())) {
                    auto cirLoopTy = cir::IntType::get(
                        &getMLIRContext(), ompIVTy.getWidth(), logicalIVSigned);
                    logicalIVValue = mlir::UnrealizedConversionCastOp::create(
                                         builder,
                                         getLoc(generatedLoopInfo->logicalIV
                                                    ->getLocation()),
                                         cirLoopTy, logicalIVValue)
                                         .getResult(0);
                    if (cirLoopTy != logicalIVType) {
                      logicalIVValue = builder.createIntCast(
                          logicalIVValue,
                          mlir::cast<cir::IntType>(logicalIVType));
                    }
                  } else {
                    logicalIVValue = castOpenMPValueToCIRType(
                        *this,
                        getLoc(generatedLoopInfo->logicalIV->getLocation()),
                        logicalIVValue, logicalIVType);
                  }
                  builder.createStore(
                      getLoc(generatedLoopInfo->logicalIV->getLocation()),
                      logicalIVValue, logicalIVRawAlloca);
                  for (const Expr *update : generatedLoopInfo->updates)
                    emitIgnoredExpr(update);

                  bool hasConditionalLastprivate = pushOpenMPLastprivateConditional(
                      wsloopClauses, logicalIVRawAlloca, s.getBeginLoc());
                  OpenMPScanParentDirectiveRAII scanDirectiveScope(
                      *this, s, collectOpenMPReductionRegionArgs(wsloopOp));
                  bodyRes = emitOMPLoopBody(*this, builder, begin,
                                            generatedLoopInfo->body);
                  if (!wsloopClauseInfo.lastprivateIndices.empty()) {
                    assert(wsloopClauseInfo.lastprivateIndices.size() ==
                               wsloopClauseInfo.lastprivateVars.size() &&
                           "mismatched OpenMP wsloop lastprivate metadata");
                    assert(wsloopClauseInfo.lastprivateIndices.size() ==
                               wsloopClauseInfo.lastprivateConditionals.size() &&
                           "mismatched OpenMP wsloop conditional-lastprivate "
                           "metadata");
                    mlir::Location ivLoc =
                        getLoc(generatedLoopInfo->logicalIV->getLocation());
                    auto cirIVTy = mlir::dyn_cast<cir::IntType>(logicalIVType);
                    assert(cirIVTy &&
                           "expected CIR integer type for generated logical IV");
                    mlir::Value upperBoundValue = castOpenMPValueToCIRType(
                        *this, ivLoc, upperBound, logicalIVType);
                    mlir::Value stepValue =
                        builder.getConstInt(ivLoc, cirIVTy, 1);
                    mlir::Value nextIV =
                        builder.createAdd(ivLoc, logicalIVValue, stepValue);
                    mlir::Value isLastIteration = builder.createCompare(
                        ivLoc, cir::CmpOpKind::gt, nextIV, upperBoundValue);

                    cir::IfOp::create(
                        builder, ivLoc, isLastIteration,
                        /*withElseRegion=*/false,
                        [&](mlir::OpBuilder &thenBuilder,
                            mlir::Location thenLoc) {
                          mlir::OpBuilder::InsertionGuard guard(builder);
                          builder.setInsertionPointToEnd(
                              thenBuilder.getInsertionBlock());
                          for (auto [index, vd, srcVD, destVD, assignOp,
                                     isConditional] :
                               llvm::zip_equal(
                                   wsloopClauseInfo.lastprivateIndices,
                                   wsloopClauseInfo.lastprivateVars,
                                   wsloopClauseInfo.lastprivateSourceVDs,
                                   wsloopClauseInfo.lastprivateDestVDs,
                                   wsloopClauseInfo.lastprivateAssignmentOps,
                                   wsloopClauseInfo.lastprivateConditionals)) {
                            assert(index < wsloopPrivateArgs.size() &&
                                   "unexpected OpenMP wsloop lastprivate "
                                   "block arg index");
                            Address srcAddr =
                                getOpenMPPrivateStorageAddress(*this, vd);
                            const VarDecl *canonicalVD = vd->getCanonicalDecl();
                            Address destAddr = Address::invalid();
                            if (auto parallelIt =
                                    parallelOriginalDecls.find(canonicalVD);
                                parallelIt != parallelOriginalDecls.end()) {
                              destAddr = parallelIt->second;
                            } else if (auto wsloopIt =
                                           wsloopOriginalDecls.find(canonicalVD);
                                       wsloopIt != wsloopOriginalDecls.end()) {
                              destAddr = wsloopIt->second;
                            }
                            assert(destAddr.isValid() &&
                                   "missing lastprivate destination address");
                            if (isConditional)
                              emitOpenMPLastprivateConditionalFinalUpdate(
                                  vd, srcAddr, vd->getLocation());
                            emitOpenMPLastprivateCopy(
                                *this, vd, srcAddr, destAddr, srcVD, destVD,
                                assignOp);
                          }
                          cir::YieldOp::create(thenBuilder, thenLoc);
                        });
                  }
                  if (hasConditionalLastprivate)
                    popOpenMPLastprivateConditional();
                }
                mlir::omp::YieldOp::create(builder, end);
                builder.setInsertionPointAfter(wsloopOp);

                reductionDeclGuards.clear();
                parallelPrivateDeclGuards.clear();

                for (auto [idx, final] :
                     llvm::enumerate(generatedLoopInfo->finals))
                  if (emitFinals[idx] && final)
                    emitIgnoredExpr(final);

                return bodyRes;
              };

              if (generatedLoopInfo->preCond->EvaluateAsBooleanCondition(
                      preCondConstant, getContext())) {
                res = emitGeneratedWsloop();
                return;
              }

              mlir::Value preCond =
                  emitOpOnBoolExpr(begin, generatedLoopInfo->preCond);
              cir::IfOp::create(
                  builder, begin, preCond, /*withElseRegion=*/false,
                  [&](mlir::OpBuilder &thenBuilder, mlir::Location) {
                    mlir::OpBuilder::InsertionGuard guard(builder);
                    builder.setInsertionPointToEnd(
                        thenBuilder.getInsertionBlock());
                    res = emitGeneratedWsloop();
                    builder.createYield(begin);
                  });
              return;
            }
            DeclMapRevertingRAII ivMapGuard(*this, loopInfo->iv);

            mlir::Value lowerBound =
                emitOpenMPIntegerExpr(*this, loopInfo->lowerBound);
            mlir::Value upperBound =
                emitOpenMPIntegerExpr(*this, loopInfo->upperBound);
            mlir::Type loopTy = lowerBound.getType();
            if (loopTy != upperBound.getType()) {
              std::string feature = directiveName.str();
              feature += " with mismatched loop bound types";
              getCIRGenModule().errorNYI(s.getSourceRange(), feature);
              return;
            }
            mlir::Value step =
                createOpenMPIntegerConstant(*this, begin, loopTy, 1);

            auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
            wsloopOp.setNowait(true);
            if (wsloopScheduleChunkValue)
              wsloopOp.getScheduleChunkMutable().assign(
                  wsloopScheduleChunkValue);
            OpenMPClauseEmissionInfo wsloopClauseInfo =
                emitOpenMPClauses(wsloopOp, wsloopClauses,
                                  parallelClauseInfo.privateVars,
                                  /*allowWsloopConditionalLastprivate=*/true);
            llvm::SmallVector<mlir::Type, 4> wsloopPrivateArgTypes;
            llvm::SmallVector<mlir::Location, 4> wsloopPrivateArgLocs;
            for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
              wsloopPrivateArgTypes.push_back(privateVar.getType());
              wsloopPrivateArgLocs.push_back(privateVar.getLoc());
            }
            llvm::SmallVector<mlir::Type, 4> wsloopReductionArgTypes;
            llvm::SmallVector<mlir::Location, 4> wsloopReductionArgLocs;
            for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
              wsloopReductionArgTypes.push_back(reductionVar.getType());
              wsloopReductionArgLocs.push_back(reductionVar.getLoc());
            }
            wsloopPrivateArgTypes.append(wsloopReductionArgTypes);
            wsloopPrivateArgLocs.append(wsloopReductionArgLocs);
            mlir::Block *wsloopBlock = builder.createBlock(
                &wsloopOp.getRegion(), {}, wsloopPrivateArgTypes,
                wsloopPrivateArgLocs);
            builder.setInsertionPointToEnd(wsloopBlock);

            mlir::omp::LoopNestOperands loopClauseOps;
            loopClauseOps.loopLowerBounds.push_back(lowerBound);
            loopClauseOps.loopUpperBounds.push_back(upperBound);
            loopClauseOps.loopSteps.push_back(step);
            if (loopInfo->upperInclusive)
              loopClauseOps.loopInclusive = builder.getUnitAttr();

            auto loopNestOp =
                mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
            loopNestOp->setAttr(
                "omp.frontend.iv_signed",
                builder.getDenseBoolArrayAttr(
                    {loopInfo->iv->getType()
                         ->hasSignedIntegerRepresentation()}));
            mlir::Block *loopBlock = builder.createBlock(
                &loopNestOp.getRegion(), {}, {loopTy},
                {getLoc(loopInfo->iv->getLocation())});
            builder.setInsertionPointToEnd(loopBlock);
            auto wsloopPrivateArgs =
                mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                    wsloopOp.getOperation())
                    .getPrivateBlockArgs();
            if (mlir::failed(remapOpenMPPrivateDecls(
                    *this, wsloopOp.getOperation(), wsloopClauseInfo,
                    wsloopPrivateDeclGuards, &wsloopOriginalDecls))) {
              res = mlir::failure();
              return;
            }
            remapOpenMPReductionDecls(*this, wsloopOp.getOperation(),
                                      wsloopClauseInfo, reductionDeclGuards);

            mlir::Value ivValue = castOpenMPValueToCIRType(
                *this, getLoc(loopInfo->iv->getLocation()),
                loopBlock->getArgument(0), ivType);
            auto ivIt = localDeclMap.find(loopInfo->iv);
            assert(ivIt != localDeclMap.end() &&
                   "expected loop IV address to be mapped");
            builder.createStore(getLoc(loopInfo->iv->getLocation()), ivValue,
                                ivIt->second);

            bool hasConditionalLastprivate = pushOpenMPLastprivateConditional(
                wsloopClauses, ivIt->second, s.getBeginLoc());
            OpenMPScanParentDirectiveRAII scanDirectiveScope(
                *this, s, collectOpenMPReductionRegionArgs(wsloopOp));
            res = emitOMPLoopBody(*this, builder, begin,
                                  loopInfo->forStmt->getBody());
            if (!wsloopClauseInfo.lastprivateIndices.empty()) {
              assert(wsloopClauseInfo.lastprivateIndices.size() ==
                         wsloopClauseInfo.lastprivateVars.size() &&
                     "mismatched OpenMP wsloop lastprivate metadata");
              assert(wsloopClauseInfo.lastprivateIndices.size() ==
                         wsloopClauseInfo.lastprivateConditionals.size() &&
                     "mismatched OpenMP wsloop conditional-lastprivate "
                     "metadata");
              mlir::Location ivLoc = getLoc(loopInfo->iv->getLocation());
              auto cirIVTy = mlir::dyn_cast<cir::IntType>(ivType);
              assert(cirIVTy && "expected CIR integer type for loop IV");
              mlir::Value upperBoundValue =
                  castOpenMPValueToCIRType(*this, ivLoc, upperBound, ivType);
              mlir::Value stepValue = builder.getConstInt(ivLoc, cirIVTy, 1);
              mlir::Value nextIV = builder.createAdd(ivLoc, ivValue, stepValue);
              mlir::Value isLastIteration = builder.createCompare(
                  ivLoc, loopInfo->upperInclusive ? cir::CmpOpKind::gt
                                                  : cir::CmpOpKind::ge,
                  nextIV, upperBoundValue);

              cir::IfOp::create(
                  builder, ivLoc, isLastIteration, /*withElseRegion=*/false,
                  [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
                    mlir::OpBuilder::InsertionGuard guard(builder);
                    builder.setInsertionPointToEnd(
                        thenBuilder.getInsertionBlock());
                    for (auto [index, vd, srcVD, destVD, assignOp,
                               isConditional] :
                         llvm::zip_equal(
                             wsloopClauseInfo.lastprivateIndices,
                             wsloopClauseInfo.lastprivateVars,
                             wsloopClauseInfo.lastprivateSourceVDs,
                             wsloopClauseInfo.lastprivateDestVDs,
                             wsloopClauseInfo.lastprivateAssignmentOps,
                             wsloopClauseInfo.lastprivateConditionals)) {
                      assert(index < wsloopPrivateArgs.size() &&
                             "unexpected OpenMP wsloop lastprivate block arg "
                             "index");
                      Address srcAddr =
                          getOpenMPPrivateStorageAddress(*this, vd);
                      const VarDecl *canonicalVD = vd->getCanonicalDecl();
                      Address destAddr = Address::invalid();
                      if (auto parallelIt =
                              parallelOriginalDecls.find(canonicalVD);
                          parallelIt != parallelOriginalDecls.end()) {
                        destAddr = parallelIt->second;
                      } else if (auto wsloopIt =
                                     wsloopOriginalDecls.find(canonicalVD);
                                 wsloopIt != wsloopOriginalDecls.end()) {
                        destAddr = wsloopIt->second;
                      }
                      assert(destAddr.isValid() &&
                             "missing lastprivate destination address");
                      if (isConditional)
                        emitOpenMPLastprivateConditionalFinalUpdate(
                            vd, srcAddr, vd->getLocation());
                      emitOpenMPLastprivateCopy(*this, vd, srcAddr, destAddr,
                                                srcVD, destVD, assignOp);
                    }
                    cir::YieldOp::create(thenBuilder, thenLoc);
                  });
            }
            if (hasConditionalLastprivate)
              popOpenMPLastprivateConditional();
            mlir::omp::YieldOp::create(builder, end);
            builder.setInsertionPointAfter(wsloopOp);

            reductionDeclGuards.clear();
          });

      mlir::omp::TerminatorOp::create(builder, end);
    }
    return res;
  };

  llvm::SmallPtrSet<const VarDecl *, 4> materializedClauseVars;
  llvm::SmallVector<mlir::Value, 2> hostEvalVars;
  if (!useTargetOffload) {
    for (const OMPClause *clause : s.clauses()) {
      if (clause->getClauseKind() != llvm::omp::OMPC_device)
        continue;
      materializeOpenMPCapturedClauseExpr(
          *this, cast<OMPDeviceClause>(clause)->getDevice(),
          materializedClauseVars);
    }
    for (const Expr *expr : hostOnlyTargetSideEffectExprs)
      materializeOpenMPCapturedClauseExpr(*this, expr, materializedClauseVars);
    if (targetClauses.empty())
      return emitParallelLoopBody(/*insideTargetRegion=*/false);

    materializeOMPTargetClauseExprs(*this, parallelClauses,
                                    materializedClauseVars);
    materializeOMPTargetClauseExprs(*this, wsloopClauses,
                                    materializedClauseVars);
    if (loopInfo) {
      materializeOpenMPCapturedClauseExpr(*this, loopInfo->lowerBound,
                                          materializedClauseVars);
      materializeOpenMPCapturedClauseExpr(*this, loopInfo->upperBound,
                                          materializedClauseVars);
    }
  } else {
    materializeOMPTargetClauseExprs(*this, targetClauses,
                                    materializedClauseVars);
    materializeOpenMPTargetParallelHostEvalClauseExprs(
        *this, parallelHostEvalClauses, materializedClauseVars);
    hostEvalVars =
        emitOpenMPTargetParallelHostEvalVars(*this, parallelHostEvalClauses);
  }

  targetOp = mlir::omp::TargetOp::create(
      builder, begin,
      /*allocate_vars=*/mlir::ValueRange{},
      /*allocator_vars=*/mlir::ValueRange{},
      /*bare=*/false,
      /*depend_kinds=*/mlir::ArrayAttr{},
      /*depend_vars=*/mlir::ValueRange{},
      /*depend_sizes=*/mlir::ValueRange{},
      /*device=*/mlir::Value{},
      /*has_device_addr_vars=*/mlir::ValueRange{},
      /*host_eval_vars=*/hostEvalVars,
      /*if_expr=*/mlir::Value{},
      /*in_reduction_vars=*/mlir::ValueRange{},
      /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
      /*in_reduction_syms=*/mlir::ArrayAttr{},
      /*is_device_ptr_vars=*/mlir::ValueRange{},
      /*map_vars=*/mlir::ValueRange{},
      /*nowait=*/false,
      /*private_vars=*/mlir::ValueRange{},
      /*private_syms=*/mlir::ArrayAttr{},
      /*private_needs_barrier=*/false,
      /*thread_limit=*/mlir::Value{},
      /*private_maps=*/mlir::DenseI64ArrayAttr{});
  annotateOpenMPTargetParentName(*this, targetOp);
  OpenMPClauseEmissionInfo targetClauseInfo =
      emitOpenMPClauses(targetOp, targetClauses, targetFirstprivateVars);
  if (mlir::failed(appendOpenMPTargetCapturedVLASizePrivates(
          *this, targetOp,
          *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target),
          targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(maybeEmitOpenMPRuntimeDependPayload(
          *this, targetOp, s, targetClauseInfo)))
    return mlir::failure();
  if (!useTargetOffload && !isTargetDevice)
    if (mlir::failed(appendHostOnlyOMPTargetCapturedMappings(
            *this, targetOp, targetClauseInfo,
            *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target))))
      return mlir::failure();
  if (mlir::failed(finalizeOMPTargetPrivateMappings(*this, targetOp,
                                                    targetClauseInfo)))
    return mlir::failure();
  if (useTargetOffload)
    promoteOMPTargetPrivateMapsForOffload(targetOp, targetClauseInfo);

  return emitOMPTargetRegionBody(
      *this, targetOp, targetClauseInfo, begin, end,
      [&]() -> mlir::LogicalResult {
        return emitParallelLoopBody(/*insideTargetRegion=*/true);
      });
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetParallelForDirective(
    const OMPTargetParallelForDirective &s) {
  if (s.hasCancel()) {
    getCIRGenModule().errorNYI(s.getBeginLoc(),
                               "OpenMP TargetParallelFor with Cancel");
    return mlir::failure();
  }

  return emitSimpleOMPTargetParallelWsloopDirective(
      s, "OpenMP OMPTargetParallelForDirective");
}
mlir::LogicalResult
CIRGenFunction::emitOMPTaskLoopDirective(const OMPTaskLoopDirective &s) {
  return emitSimpleOMPTaskLoopDirective(s, "OpenMP OMPTaskLoopDirective",
                                        /*wrapInMaster=*/false,
                                        /*wrapInMasked=*/false,
                                        /*simd=*/false);
}
mlir::LogicalResult CIRGenFunction::emitOMPTaskLoopSimdDirective(
    const OMPTaskLoopSimdDirective &s) {
  return emitSimpleOMPTaskLoopDirective(s, "OpenMP OMPTaskLoopSimdDirective",
                                        /*wrapInMaster=*/false,
                                        /*wrapInMasked=*/false,
                                        /*simd=*/true);
}
mlir::LogicalResult CIRGenFunction::emitOMPMaskedTaskLoopDirective(
    const OMPMaskedTaskLoopDirective &s) {
  return emitSimpleOMPTaskLoopDirective(s, "OpenMP OMPMaskedTaskLoopDirective",
                                        /*wrapInMaster=*/false,
                                        /*wrapInMasked=*/true,
                                        /*simd=*/false);
}
mlir::LogicalResult CIRGenFunction::emitOMPMaskedTaskLoopSimdDirective(
    const OMPMaskedTaskLoopSimdDirective &s) {
  return emitSimpleOMPTaskLoopDirective(
      s, "OpenMP OMPMaskedTaskLoopSimdDirective",
      /*wrapInMaster=*/false,
      /*wrapInMasked=*/true,
      /*simd=*/true);
}
mlir::LogicalResult CIRGenFunction::emitOMPMasterTaskLoopDirective(
    const OMPMasterTaskLoopDirective &s) {
  return emitSimpleOMPTaskLoopDirective(s, "OpenMP OMPMasterTaskLoopDirective",
                                        /*wrapInMaster=*/true,
                                        /*wrapInMasked=*/false,
                                        /*simd=*/false);
}
mlir::LogicalResult CIRGenFunction::emitOMPMasterTaskLoopSimdDirective(
    const OMPMasterTaskLoopSimdDirective &s) {
  return emitSimpleOMPTaskLoopDirective(
      s, "OpenMP OMPMasterTaskLoopSimdDirective",
      /*wrapInMaster=*/true,
      /*wrapInMasked=*/false,
      /*simd=*/true);
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelGenericLoopDirective(
    const OMPParallelGenericLoopDirective &s) {
  auto loopInfo = getOpenMPSimpleCanonicalForInfo(s);
  auto generatedLoopInfo = getOpenMPSimpleGeneratedLoopInfo(s);
  bool useGeneratedLoopInfo = !loopInfo;
  if (loopInfo && generatedLoopInfo &&
      loopInfo->lowerBound->getType().getCanonicalType() !=
          loopInfo->upperBound->getType().getCanonicalType()) {
    useGeneratedLoopInfo = true;
  }
  if (!loopInfo && !generatedLoopInfo) {
    getCIRGenModule().errorNYI(
        s.getSourceRange(),
        "OpenMP OMPParallelGenericLoopDirective for "
        "non-canonical/non-unit-stride loop");
    return mlir::failure();
  }

  llvm::SmallVector<const OMPClause *, 8> parallelClauses;
  llvm::SmallVector<const OMPClause *, 8> wsloopClauses;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if: {
      const auto *ifClause = cast<OMPIfClause>(clause);
      if (ifClause->getNameModifier() == llvm::omp::OMPD_unknown ||
          ifClause->getNameModifier() == llvm::omp::OMPD_parallel) {
        parallelClauses.push_back(clause);
        break;
      }
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(),
          "OpenMP parallel loop if clause modifier");
      return mlir::failure();
    }
    case llvm::omp::OMPC_num_threads:
    case llvm::omp::OMPC_severity:
    case llvm::omp::OMPC_message:
    case llvm::omp::OMPC_proc_bind:
    case llvm::omp::OMPC_firstprivate:
    case llvm::omp::OMPC_copyin:
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_private:
    case llvm::omp::OMPC_lastprivate:
    case llvm::omp::OMPC_reduction:
    case llvm::omp::OMPC_schedule:
    case llvm::omp::OMPC_order:
    case llvm::omp::OMPC_ordered:
      wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_bind:
      // Match OGCG: parallel loop is lowered as parallel + for regardless of
      // bind kind.
      break;
    case llvm::omp::OMPC_allocate:
      // Lower non-target parallel loop allocate(...) through the synthesized
      // private-copy decl attributes rather than as a direct combined-op
      // clause. The owning private-like clause will attach allocator operands
      // to the corresponding parallel/wsloop private entry.
      break;
    case llvm::omp::OMPC_collapse:
    case llvm::omp::OMPC_default:
    case llvm::omp::OMPC_shared:
      break;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), "OpenMP parallel loop clause",
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      return mlir::failure();
    }
  }

  mlir::LogicalResult res = mlir::success();
  llvm::SmallVector<mlir::Type> retTy;
  llvm::SmallVector<mlir::Value> operands;
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  llvm::SmallVector<OpenMPCopyinMasterAddr, 4> copyinMasterAddrs =
      collectOpenMPCopyinMasterAddresses(*this, s);

  auto parallelOp =
      mlir::omp::ParallelOp::create(builder, begin, retTy, operands);
  OpenMPClauseEmissionInfo parallelClauseInfo =
      emitOpenMPClauses(parallelOp, parallelClauses);
  const CapturedStmt *parallelCaptured =
      s.getCapturedStmt(OpenMPDirectiveKind::OMPD_parallel);
  assert(parallelCaptured && "expected OpenMP parallel captured stmt");
  parallelOp->setAttr("cir.instrprof_func_name",
                      cgm.getStringAttrCached(
                          getOpenMPHelperPGOName(*parallelCaptured)));
  parallelOp->setAttr("cir.instrprof_func_linkage",
                      builder.getI32IntegerAttr(static_cast<int32_t>(
                          llvm::GlobalValue::InternalLinkage)));
  mlir::Type ivType;
  Address ivSeedRawAlloca = Address::invalid();
  llvm::SmallVector<mlir::Attribute, 4> parallelPrivateSyms;
  if (auto existingSyms = parallelOp.getPrivateSyms())
    llvm::append_range(parallelPrivateSyms, *existingSyms);
  if (!useGeneratedLoopInfo) {
    CharUnits ivAlign = getContext().getDeclAlign(loopInfo->iv);
    ivType = convertTypeForMem(loopInfo->iv->getType());
    Address ivSeedAddr = createTempAlloca(
        ivType, ivAlign, getLoc(loopInfo->iv->getLocation()),
        loopInfo->iv->getName(), /*arraySize=*/nullptr, &ivSeedRawAlloca);
    if (!localDeclMap.count(loopInfo->iv))
      declare(ivSeedRawAlloca.getPointer(), loopInfo->iv, loopInfo->iv->getType(),
              getLoc(loopInfo->iv->getSourceRange()), ivAlign);
    unsigned ivParallelPrivateIndex = parallelOp.getPrivateVars().size();
    auto ivPrivatizer = getOrCreateOpenMPPrivatePrivatizer(
        *this, builder, getLoc(loopInfo->iv->getLocation()), loopInfo->iv,
        ivType);
    parallelOp.getPrivateVarsMutable().append(ivSeedAddr.getPointer());
    parallelPrivateSyms.push_back(mlir::SymbolRefAttr::get(ivPrivatizer));
    parallelClauseInfo.privateIndices.push_back(ivParallelPrivateIndex);
    parallelClauseInfo.privateVars.push_back(loopInfo->iv);
  }
  parallelOp.setPrivateSymsAttr(
      mlir::ArrayAttr::get(builder.getContext(), parallelPrivateSyms));
  if (isInsideOpenMPTargetRegion(*this) &&
      mlir::failed(appendOpenMPParallelNestedSyntheticFirstprivatePrivates(
          *this, parallelOp, wsloopClauses, parallelClauseInfo))) {
    return mlir::failure();
  }

  {
    mlir::OpBuilder::InsertionGuard guardCase(builder);
    llvm::SmallVector<mlir::Type, 4> privateArgTypes;
    llvm::SmallVector<mlir::Location, 4> privateArgLocs;
    for (mlir::Value privateVar : parallelOp.getPrivateVars()) {
      privateArgTypes.push_back(privateVar.getType());
      privateArgLocs.push_back(privateVar.getLoc());
    }

    mlir::Block *block = builder.createBlock(&parallelOp.getRegion(), {},
                                             privateArgTypes, privateArgLocs);
    builder.setInsertionPointToEnd(block);

    cir::ScopeOp::create(
        builder, begin, /*scopeBuilder=*/
        [&](mlir::OpBuilder &, mlir::Location) {
          auto pgoScope = enterOpenMPHelperRegionCounters(*parallelCaptured);
          LexicalScope ls{*this, begin, builder.getInsertionBlock()};
          llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
              parallelPrivateDeclGuards;
          llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
              wsloopPrivateDeclGuards;
          llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
              reductionDeclGuards;
          llvm::DenseMap<const VarDecl *, Address> parallelOriginalDecls;
          llvm::DenseMap<const VarDecl *, Address> wsloopOriginalDecls;
          if (mlir::failed(remapOpenMPPrivateDecls(
                  *this, parallelOp.getOperation(), parallelClauseInfo,
                  parallelPrivateDeclGuards, &parallelOriginalDecls))) {
            res = mlir::failure();
            return;
          }

          OpenMPTLSCopyinDeclMapScope tlsCopyinDecls(*this, s);
          (void)emitOpenMPCopyinClause(*this, s, copyinMasterAddrs);
          bool hasDisabledLastprivateConditional =
              pushDisabledOpenMPLastprivateConditional(s);
          auto popDisabledLastprivateConditional = llvm::scope_exit([&] {
            if (hasDisabledLastprivateConditional)
              popOpenMPLastprivateConditional();
          });
          incrementProfileCounter(parallelCaptured->getCapturedDecl()->getBody());

          if (useGeneratedLoopInfo) {
            assert(generatedLoopInfo &&
                   "expected generated loop info for parallel generic loop");

            if (mlir::failed(
                    emitOpenMPTransformPreInits(*this, s.getPreInits()))) {
              res = mlir::failure();
              return;
            }

            bool preCondConstant = false;
            if (generatedLoopInfo->preCond->EvaluateAsBooleanCondition(
                    preCondConstant, getContext()) &&
                !preCondConstant)
              return;

            llvm::SmallVector<bool, 4> emitFinals;
            emitFinals.reserve(generatedLoopInfo->sourceIVs.size());
            for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs) {
              emitFinals.push_back(localDeclMap.count(sourceIV) ||
                                   sourceIV->hasGlobalStorage() ||
                                   (capturedStmtInfo &&
                                    capturedStmtInfo->lookup(sourceIV)));
            }

            auto emitGeneratedWsloop = [&]() -> mlir::LogicalResult {
              mlir::Value upperBound =
                  emitOpenMPIntegerExpr(*this, generatedLoopInfo->lastIteration);
              mlir::Type loopTy = upperBound.getType();
              mlir::Value lowerBound =
                  createOpenMPIntegerConstant(*this, begin, loopTy, 0);
              mlir::Value step =
                  createOpenMPIntegerConstant(*this, begin, loopTy, 1);

              auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
              wsloopOp.setNowait(true);
              OpenMPClauseEmissionInfo wsloopClauseInfo =
                  emitOpenMPClauses(wsloopOp, wsloopClauses,
                                    parallelClauseInfo.privateVars,
                                    /*allowWsloopConditionalLastprivate=*/true);
              llvm::DenseMap<const VarDecl *, Address> sourceIVOriginalAddrs;
              {
                // Materialize the original destination addresses before the
                // wsloop so reference-backed/member-backed lvalues dominate
                // the generated loop region.
                mlir::OpBuilder::InsertionGuard guard(builder);
                builder.setInsertionPoint(wsloopOp);
                for (auto [idx, sourceIV] :
                     llvm::enumerate(generatedLoopInfo->sourceIVs)) {
                  if (!emitFinals[idx])
                    continue;
                  sourceIVOriginalAddrs.try_emplace(
                      sourceIV, emitLValue(s.counters()[idx]).getAddress());
                }
              }
              llvm::SmallVector<mlir::Type, 4> wsloopPrivateArgTypes;
              llvm::SmallVector<mlir::Location, 4> wsloopPrivateArgLocs;
              for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
                wsloopPrivateArgTypes.push_back(privateVar.getType());
                wsloopPrivateArgLocs.push_back(privateVar.getLoc());
              }
              llvm::SmallVector<mlir::Type, 4> wsloopReductionArgTypes;
              llvm::SmallVector<mlir::Location, 4> wsloopReductionArgLocs;
              for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
                wsloopReductionArgTypes.push_back(reductionVar.getType());
                wsloopReductionArgLocs.push_back(reductionVar.getLoc());
              }
              wsloopPrivateArgTypes.append(wsloopReductionArgTypes);
              wsloopPrivateArgLocs.append(wsloopReductionArgLocs);
              mlir::Block *wsloopBlock = builder.createBlock(
                  &wsloopOp.getRegion(), {}, wsloopPrivateArgTypes,
                  wsloopPrivateArgLocs);
              llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                  wsloopPrivateDeclGuards;
              RunCleanupsScope wsloopPrivateCleanupScope(*this);
              {
                mlir::OpBuilder::InsertionGuard guard(builder);
                builder.setInsertionPoint(wsloopOp);
                if (mlir::failed(remapOpenMPPrivateDecls(
                        *this, wsloopOp.getOperation(), wsloopClauseInfo,
                        wsloopPrivateDeclGuards, &wsloopOriginalDecls))) {
                  res = mlir::failure();
                  return mlir::failure();
                }
                remapOpenMPReductionDecls(*this, wsloopOp.getOperation(),
                                          wsloopClauseInfo,
                                          reductionDeclGuards);
              }
              builder.setInsertionPointToEnd(wsloopBlock);

              mlir::omp::LoopNestOperands loopClauseOps;
              loopClauseOps.loopLowerBounds.push_back(lowerBound);
              loopClauseOps.loopUpperBounds.push_back(upperBound);
              loopClauseOps.loopSteps.push_back(step);
              loopClauseOps.loopInclusive = builder.getUnitAttr();

              auto loopNestOp =
                  mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
              bool logicalIVSigned = generatedLoopInfo->logicalIV->getType()
                                         ->hasSignedIntegerRepresentation();
              loopNestOp->setAttr("omp.frontend.iv_signed",
                                  builder.getDenseBoolArrayAttr(
                                      {logicalIVSigned}));
              mlir::Block *loopBlock = builder.createBlock(
                  &loopNestOp.getRegion(), {}, {loopTy},
                  {getLoc(generatedLoopInfo->logicalIV->getLocation())});
              builder.setInsertionPointToEnd(loopBlock);

              auto createLoopVarStorage = [&](const VarDecl *vd) {
                CharUnits vdAlign = getContext().getDeclAlign(vd);
                mlir::Type vdType = convertTypeForMem(vd->getType());
                Address rawAlloca = Address::invalid();
                createTempAlloca(vdType, vdAlign, getLoc(vd->getLocation()),
                                 vd->getName(), /*arraySize=*/nullptr,
                                 &rawAlloca);
                return std::pair{rawAlloca, vdType};
              };

              auto [logicalIVRawAlloca, logicalIVType] =
                  createLoopVarStorage(generatedLoopInfo->logicalIV);
              llvm::SmallVector<Address, 4> sourceIVRawAllocas;
              sourceIVRawAllocas.reserve(generatedLoopInfo->sourceIVs.size());
              for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs) {
                auto [sourceIVRawAlloca, sourceIVType] =
                    createLoopVarStorage(sourceIV);
                (void)sourceIVType;
                sourceIVRawAllocas.push_back(sourceIVRawAlloca);
              }

              mlir::LogicalResult bodyRes = mlir::success();
              {
                llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                    sourceIVMapGuards;
                sourceIVMapGuards.reserve(generatedLoopInfo->sourceIVs.size());
                for (auto [sourceIV, sourceIVRawAlloca] :
                     llvm::zip_equal(generatedLoopInfo->sourceIVs,
                                     sourceIVRawAllocas)) {
                  sourceIVMapGuards.push_back(
                      std::make_unique<DeclMapRevertingRAII>(*this, sourceIV));
                  replaceAddrOfLocalVar(sourceIV, sourceIVRawAlloca);
                }
                DeclMapRevertingRAII logicalIVMapGuard(
                    *this, generatedLoopInfo->logicalIV);
                replaceAddrOfLocalVar(generatedLoopInfo->logicalIV,
                                      logicalIVRawAlloca);
                llvm::SmallPtrSet<const VarDecl *, 4> sourceIVSet;
                for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs)
                  sourceIVSet.insert(sourceIV->getCanonicalDecl());

                auto wsloopPrivateArgs =
                    mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                        wsloopOp.getOperation())
                        .getPrivateBlockArgs();

                mlir::Value logicalIVValue = loopBlock->getArgument(0);
                if (auto ompIVTy =
                        mlir::dyn_cast<mlir::IntegerType>(
                            logicalIVValue.getType())) {
                  auto cirLoopTy = cir::IntType::get(
                      &getMLIRContext(), ompIVTy.getWidth(), logicalIVSigned);
                  logicalIVValue = mlir::UnrealizedConversionCastOp::create(
                                       builder,
                                       getLoc(generatedLoopInfo->logicalIV
                                                  ->getLocation()),
                                       cirLoopTy, logicalIVValue)
                                       .getResult(0);
                  if (cirLoopTy != logicalIVType) {
                    logicalIVValue = builder.createIntCast(
                        logicalIVValue,
                        mlir::cast<cir::IntType>(logicalIVType));
                  }
                } else {
                  logicalIVValue = castOpenMPValueToCIRType(
                      *this,
                      getLoc(generatedLoopInfo->logicalIV->getLocation()),
                      logicalIVValue, logicalIVType);
                }
                builder.createStore(
                    getLoc(generatedLoopInfo->logicalIV->getLocation()),
                    logicalIVValue, logicalIVRawAlloca);
                for (const Expr *update : generatedLoopInfo->updates)
                  emitIgnoredExpr(update);

                bool hasConditionalLastprivate =
                    pushOpenMPLastprivateConditional(
                        wsloopClauses, logicalIVRawAlloca, s.getBeginLoc());
                OpenMPScanParentDirectiveRAII scanDirectiveScope(
                    *this, s, collectOpenMPReductionRegionArgs(wsloopOp));
                bodyRes = emitOMPLoopBody(*this, builder, begin,
                                          generatedLoopInfo->body);
                bool anyEmitFinals =
                    llvm::any_of(emitFinals, [](bool emitFinal) {
                      return emitFinal;
                    });
                if (anyEmitFinals || !wsloopClauseInfo.lastprivateIndices.empty()) {
                  assert(wsloopClauseInfo.lastprivateIndices.size() ==
                             wsloopClauseInfo.lastprivateVars.size() &&
                         "mismatched OpenMP wsloop lastprivate metadata");
                  assert(wsloopClauseInfo.lastprivateIndices.size() ==
                             wsloopClauseInfo.lastprivateConditionals.size() &&
                         "mismatched OpenMP wsloop conditional-lastprivate "
                         "metadata");
                  mlir::Location ivLoc =
                      getLoc(generatedLoopInfo->logicalIV->getLocation());
                  auto cirIVTy = mlir::dyn_cast<cir::IntType>(logicalIVType);
                  assert(cirIVTy &&
                         "expected CIR integer type for generated logical IV");
                  mlir::Value upperBoundValue = castOpenMPValueToCIRType(
                      *this, ivLoc, upperBound, logicalIVType);
                  mlir::Value stepValue =
                      builder.getConstInt(ivLoc, cirIVTy, 1);
                  mlir::Value nextIV =
                      builder.createAdd(ivLoc, logicalIVValue, stepValue);
                  mlir::Value isLastIteration = builder.createCompare(
                      ivLoc, cir::CmpOpKind::gt, nextIV, upperBoundValue);

                  cir::IfOp::create(
                      builder, ivLoc, isLastIteration,
                      /*withElseRegion=*/false,
                      [&](mlir::OpBuilder &thenBuilder,
                          mlir::Location thenLoc) {
                        mlir::OpBuilder::InsertionGuard guard(builder);
                        builder.setInsertionPointToEnd(
                            thenBuilder.getInsertionBlock());
                        for (auto [idx, final] :
                             llvm::enumerate(generatedLoopInfo->finals)) {
                          if (!emitFinals[idx] || !final)
                            continue;
                          const VarDecl *sourceIV =
                              generatedLoopInfo->sourceIVs[idx];
                          auto originalIt = sourceIVOriginalAddrs.find(sourceIV);
                          assert(originalIt != sourceIVOriginalAddrs.end() &&
                                 "missing parallel loop source IV destination "
                                 "address");
                          DeclMapRevertingRAII finalSourceIVMapGuard(*this,
                                                                     sourceIV);
                          replaceAddrOfLocalVar(sourceIV, originalIt->second);
                          emitIgnoredExpr(final);
                        }
                        for (auto [index, vd, srcVD, destVD, assignOp,
                                   isConditional] :
                             llvm::zip_equal(
                                 wsloopClauseInfo.lastprivateIndices,
                                 wsloopClauseInfo.lastprivateVars,
                                 wsloopClauseInfo.lastprivateSourceVDs,
                                 wsloopClauseInfo.lastprivateDestVDs,
                                 wsloopClauseInfo.lastprivateAssignmentOps,
                                 wsloopClauseInfo.lastprivateConditionals)) {
                          if (sourceIVSet.contains(vd->getCanonicalDecl()))
                            continue;
                          assert(index < wsloopPrivateArgs.size() &&
                                 "unexpected OpenMP wsloop lastprivate "
                                 "block arg index");
                          Address srcAddr =
                              getOpenMPPrivateStorageAddress(*this, vd);
                          const VarDecl *canonicalVD = vd->getCanonicalDecl();
                          Address destAddr = Address::invalid();
                          if (auto parallelIt =
                                  parallelOriginalDecls.find(canonicalVD);
                              parallelIt != parallelOriginalDecls.end()) {
                            destAddr = parallelIt->second;
                          } else if (auto wsloopIt =
                                         wsloopOriginalDecls.find(canonicalVD);
                                     wsloopIt != wsloopOriginalDecls.end()) {
                            destAddr = wsloopIt->second;
                          }
                          assert(destAddr.isValid() &&
                                 "missing lastprivate destination address");
                          if (isConditional)
                            emitOpenMPLastprivateConditionalFinalUpdate(
                                vd, srcAddr, vd->getLocation());
                          emitOpenMPLastprivateCopy(*this, vd, srcAddr, destAddr,
                                                    srcVD, destVD, assignOp);
                        }
                        cir::YieldOp::create(thenBuilder, thenLoc);
                      });
                }
                if (hasConditionalLastprivate)
                  popOpenMPLastprivateConditional();
              }
              mlir::omp::YieldOp::create(builder, end);
              builder.setInsertionPointAfter(wsloopOp);
              wsloopPrivateCleanupScope.forceCleanup();

              reductionDeclGuards.clear();
              parallelPrivateDeclGuards.clear();
              for (auto [idx, sourceIV] :
                   llvm::enumerate(generatedLoopInfo->sourceIVs)) {
                if (!emitFinals[idx])
                  continue;
                auto originalIt = sourceIVOriginalAddrs.find(sourceIV);
                assert(originalIt != sourceIVOriginalAddrs.end() &&
                       "missing parallel loop source IV restore address");
                replaceAddrOfLocalVar(sourceIV, originalIt->second);
              }

              return bodyRes;
            };

            if (generatedLoopInfo->preCond->EvaluateAsBooleanCondition(
                    preCondConstant, getContext())) {
              res = emitGeneratedWsloop();
              return;
            }

            mlir::Value preCond =
                emitOpOnBoolExpr(begin, generatedLoopInfo->preCond);
            cir::IfOp::create(
                builder, begin, preCond, /*withElseRegion=*/false,
                [&](mlir::OpBuilder &thenBuilder, mlir::Location) {
                  mlir::OpBuilder::InsertionGuard guard(builder);
                  builder.setInsertionPointToEnd(
                      thenBuilder.getInsertionBlock());
                  res = emitGeneratedWsloop();
                  builder.createYield(begin);
                });
            return;
          }

          DeclMapRevertingRAII ivMapGuard(*this, loopInfo->iv);

          mlir::Value lowerBound =
              emitOpenMPIntegerExpr(*this, loopInfo->lowerBound);
          mlir::Value upperBound =
              emitOpenMPIntegerExpr(*this, loopInfo->upperBound);
          mlir::Type loopTy = lowerBound.getType();
          if (loopTy != upperBound.getType()) {
            getCIRGenModule().errorNYI(
                s.getSourceRange(),
                "OpenMP OMPParallelGenericLoopDirective with mismatched loop "
                "bound types");
            return;
          }
          mlir::Value step =
              createOpenMPIntegerConstant(*this, begin, loopTy, 1);

          auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
          wsloopOp.setNowait(true);
          OpenMPClauseEmissionInfo wsloopClauseInfo =
              emitOpenMPClauses(wsloopOp, wsloopClauses,
                                parallelClauseInfo.privateVars,
                                /*allowWsloopConditionalLastprivate=*/true);
          llvm::SmallVector<mlir::Type, 4> wsloopPrivateArgTypes;
          llvm::SmallVector<mlir::Location, 4> wsloopPrivateArgLocs;
          for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
            wsloopPrivateArgTypes.push_back(privateVar.getType());
            wsloopPrivateArgLocs.push_back(privateVar.getLoc());
          }
          llvm::SmallVector<mlir::Type, 4> wsloopReductionArgTypes;
          llvm::SmallVector<mlir::Location, 4> wsloopReductionArgLocs;
          for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
            wsloopReductionArgTypes.push_back(reductionVar.getType());
            wsloopReductionArgLocs.push_back(reductionVar.getLoc());
          }
          wsloopPrivateArgTypes.append(wsloopReductionArgTypes);
          wsloopPrivateArgLocs.append(wsloopReductionArgLocs);
          mlir::Block *wsloopBlock = builder.createBlock(
              &wsloopOp.getRegion(), {}, wsloopPrivateArgTypes,
              wsloopPrivateArgLocs);
          RunCleanupsScope wsloopPrivateCleanupScope(*this);
          {
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPoint(wsloopOp);
            if (mlir::failed(remapOpenMPPrivateDecls(
                    *this, wsloopOp.getOperation(), wsloopClauseInfo,
                    wsloopPrivateDeclGuards, &wsloopOriginalDecls))) {
              res = mlir::failure();
              return;
            }
            remapOpenMPReductionDecls(*this, wsloopOp.getOperation(),
                                      wsloopClauseInfo, reductionDeclGuards);
          }
          builder.setInsertionPointToEnd(wsloopBlock);

          mlir::omp::LoopNestOperands loopClauseOps;
          loopClauseOps.loopLowerBounds.push_back(lowerBound);
          loopClauseOps.loopUpperBounds.push_back(upperBound);
          loopClauseOps.loopSteps.push_back(step);
          if (loopInfo->upperInclusive)
            loopClauseOps.loopInclusive = builder.getUnitAttr();

          auto loopNestOp =
              mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
          loopNestOp->setAttr(
              "omp.frontend.iv_signed",
              builder.getDenseBoolArrayAttr(
                  {loopInfo->iv->getType()->hasSignedIntegerRepresentation()}));
          mlir::Block *loopBlock = builder.createBlock(
              &loopNestOp.getRegion(), {}, {loopTy},
              {getLoc(loopInfo->iv->getLocation())});
          builder.setInsertionPointToEnd(loopBlock);
          auto wsloopPrivateArgs =
              mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                  wsloopOp.getOperation())
                  .getPrivateBlockArgs();

          mlir::Value ivValue = castOpenMPValueToCIRType(
              *this, getLoc(loopInfo->iv->getLocation()),
              loopBlock->getArgument(0), ivType);
          auto ivIt = localDeclMap.find(loopInfo->iv);
          assert(ivIt != localDeclMap.end() &&
                 "expected loop IV address to be mapped");
          builder.createStore(getLoc(loopInfo->iv->getLocation()), ivValue,
                              ivIt->second);

          bool hasConditionalLastprivate = pushOpenMPLastprivateConditional(
              wsloopClauses, ivIt->second, s.getBeginLoc());
          OpenMPScanParentDirectiveRAII scanDirectiveScope(
              *this, s, collectOpenMPReductionRegionArgs(wsloopOp));
          res = emitOMPLoopBody(*this, builder, begin,
                                loopInfo->forStmt->getBody());
          if (!wsloopClauseInfo.lastprivateIndices.empty()) {
            assert(wsloopClauseInfo.lastprivateIndices.size() ==
                       wsloopClauseInfo.lastprivateVars.size() &&
                   "mismatched OpenMP wsloop lastprivate metadata");
            assert(wsloopClauseInfo.lastprivateIndices.size() ==
                       wsloopClauseInfo.lastprivateConditionals.size() &&
                   "mismatched OpenMP wsloop conditional-lastprivate "
                   "metadata");
            mlir::Location ivLoc = getLoc(loopInfo->iv->getLocation());
            auto cirIVTy = mlir::dyn_cast<cir::IntType>(ivType);
            assert(cirIVTy && "expected CIR integer type for loop IV");
            mlir::Value upperBoundValue =
                castOpenMPValueToCIRType(*this, ivLoc, upperBound, ivType);
            mlir::Value stepValue = builder.getConstInt(ivLoc, cirIVTy, 1);
            mlir::Value nextIV = builder.createAdd(ivLoc, ivValue, stepValue);
            mlir::Value isLastIteration = builder.createCompare(
                ivLoc, loopInfo->upperInclusive ? cir::CmpOpKind::gt
                                                : cir::CmpOpKind::ge,
                nextIV, upperBoundValue);

            cir::IfOp::create(
                builder, ivLoc, isLastIteration, /*withElseRegion=*/false,
                [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
                  mlir::OpBuilder::InsertionGuard guard(builder);
                  builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
                  for (auto [index, vd, srcVD, destVD, assignOp,
                             isConditional] :
                       llvm::zip_equal(
                           wsloopClauseInfo.lastprivateIndices,
                           wsloopClauseInfo.lastprivateVars,
                           wsloopClauseInfo.lastprivateSourceVDs,
                           wsloopClauseInfo.lastprivateDestVDs,
                           wsloopClauseInfo.lastprivateAssignmentOps,
                           wsloopClauseInfo.lastprivateConditionals)) {
                    assert(index < wsloopPrivateArgs.size() &&
                           "unexpected OpenMP wsloop lastprivate block arg "
                           "index");
                    Address srcAddr = getOpenMPPrivateStorageAddress(*this, vd);
                    const VarDecl *canonicalVD = vd->getCanonicalDecl();
                    Address destAddr = Address::invalid();
                    if (auto parallelIt =
                            parallelOriginalDecls.find(canonicalVD);
                        parallelIt != parallelOriginalDecls.end()) {
                      destAddr = parallelIt->second;
                    } else if (auto wsloopIt =
                                   wsloopOriginalDecls.find(canonicalVD);
                               wsloopIt != wsloopOriginalDecls.end()) {
                      destAddr = wsloopIt->second;
                    }
                    assert(destAddr.isValid() &&
                           "missing lastprivate destination address");
                    if (isConditional)
                      emitOpenMPLastprivateConditionalFinalUpdate(
                          vd, srcAddr, vd->getLocation());
                    emitOpenMPLastprivateCopy(*this, vd, srcAddr, destAddr,
                                             srcVD, destVD, assignOp);
                  }
                  cir::YieldOp::create(thenBuilder, thenLoc);
                });
          }
          if (hasConditionalLastprivate)
            popOpenMPLastprivateConditional();
          mlir::omp::YieldOp::create(builder, end);
          builder.setInsertionPointAfter(wsloopOp);
          wsloopPrivateCleanupScope.forceCleanup();

          reductionDeclGuards.clear();
        });

    mlir::omp::TerminatorOp::create(builder, end);
  }
  if (mlir::succeeded(res))
    checkForOpenMPLastprivateConditionalUpdate(*this, s);
  return res;
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelMaskedDirective(
    const OMPParallelMaskedDirective &s) {
  return emitSimpleOMPParallelDirective(
      s, "OpenMP OMPParallelMaskedDirective",
      /*wrapInMaster=*/false,
      /*wrapInMasked=*/true);
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelMaskedTaskLoopDirective(
    const OMPParallelMaskedTaskLoopDirective &s) {
  return emitSimpleOMPParallelTaskLoopDirective(
      s, "OpenMP OMPParallelMaskedTaskLoopDirective",
      /*wrapInMaster=*/false,
      /*wrapInMasked=*/true,
      /*simd=*/false);
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelMaskedTaskLoopSimdDirective(
    const OMPParallelMaskedTaskLoopSimdDirective &s) {
  return emitSimpleOMPParallelTaskLoopDirective(
      s, "OpenMP OMPParallelMaskedTaskLoopSimdDirective",
      /*wrapInMaster=*/false,
      /*wrapInMasked=*/true,
      /*simd=*/true);
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelMasterTaskLoopDirective(
    const OMPParallelMasterTaskLoopDirective &s) {
  return emitSimpleOMPParallelTaskLoopDirective(
      s, "OpenMP OMPParallelMasterTaskLoopDirective",
      /*wrapInMaster=*/true,
      /*wrapInMasked=*/false,
      /*simd=*/false);
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelMasterTaskLoopSimdDirective(
    const OMPParallelMasterTaskLoopSimdDirective &s) {
  return emitSimpleOMPParallelTaskLoopDirective(
      s, "OpenMP OMPParallelMasterTaskLoopSimdDirective",
      /*wrapInMaster=*/true,
      /*wrapInMasked=*/false,
      /*simd=*/true);
}
mlir::LogicalResult
CIRGenFunction::emitOMPDistributeDirective(const OMPDistributeDirective &s) {
  return emitSimpleOMPDistributeDirective(
      *this, s, s.clauses(), "OpenMP OMPDistributeDirective");
}
mlir::LogicalResult CIRGenFunction::emitOMPDistributeParallelForDirective(
    const OMPDistributeParallelForDirective &s) {
  return emitOMPDistributeParallelWsloopDirective(
      s, "OpenMP OMPDistributeParallelForDirective", /*simd=*/false);
}
mlir::LogicalResult CIRGenFunction::emitOMPDistributeParallelForSimdDirective(
    const OMPDistributeParallelForSimdDirective &s) {
  return emitOMPDistributeParallelWsloopDirective(
      s, "OpenMP OMPDistributeParallelForSimdDirective", /*simd=*/true);
}
static mlir::LogicalResult emitOMPDistributeSimdLoop(
    CIRGenFunction &cgf, const OMPLoopDirective &s,
    ArrayRef<const OMPClause *> distributeClauses,
    ArrayRef<const OMPClause *> simdClauses, llvm::StringRef directiveName,
    mlir::Location begin, mlir::Location end, bool rematerializeClauseExprs,
    mlir::Value distScheduleChunkOverride = {},
    mlir::Value lowerBoundOverride = {}, mlir::Value upperBoundOverride = {},
    mlir::Value stepOverride = {}) {
  auto loopInfo = getOpenMPSimpleCanonicalForInfo(s);
  auto generatedLoopInfo =
      loopInfo ? std::optional<OpenMPSimpleGeneratedLoopInfo>{}
               : getOpenMPSimpleGeneratedLoopInfo(s);
  if (!loopInfo && !generatedLoopInfo) {
    std::string feature = directiveName.str();
    feature += " for non-canonical/non-unit-stride loop";
    cgf.getCIRGenModule().errorNYI(s.getSourceRange(), feature);
    return mlir::failure();
  }
  if (!loopInfo &&
      (lowerBoundOverride || upperBoundOverride || stepOverride)) {
    std::string feature = directiveName.str();
    feature += " target host_eval for generated loop";
    cgf.getCIRGenModule().errorNYI(s.getSourceRange(), feature);
    return mlir::failure();
  }

  llvm::SmallPtrSet<const VarDecl *, 4> rematerializedClauseVars;
  llvm::SmallVector<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>, 8>
      rematerializedClauseGuards;
  auto rematerializeGeneratedLoopExprs = [&]() {
    if (!rematerializeClauseExprs || !generatedLoopInfo)
      return;
    rematerializeOpenMPCapturedExprTree(cgf, generatedLoopInfo->preCond,
                                        rematerializedClauseVars,
                                        rematerializedClauseGuards);
    rematerializeOpenMPCapturedExprTree(cgf, generatedLoopInfo->lastIteration,
                                        rematerializedClauseVars,
                                        rematerializedClauseGuards);
    for (const Expr *update : generatedLoopInfo->updates)
      rematerializeOpenMPCapturedExprTree(cgf, update, rematerializedClauseVars,
                                          rematerializedClauseGuards);
  };
  if (rematerializeClauseExprs) {
    for (const OMPClause *clause : distributeClauses) {
      if (clause->getClauseKind() != llvm::omp::OMPC_dist_schedule)
        continue;
      if (const Expr *chunk = cast<OMPDistScheduleClause>(clause)->getChunkSize();
          chunk && !distScheduleChunkOverride)
        rematerializeOpenMPCapturedExpr(cgf, chunk, rematerializedClauseVars,
                                        rematerializedClauseGuards);
    }
    rematerializeOpenMPClauseExprs(cgf, simdClauses, rematerializedClauseVars,
                                   rematerializedClauseGuards);
    if (loopInfo && !lowerBoundOverride)
      rematerializeOpenMPCapturedExpr(cgf, loopInfo->lowerBound,
                                      rematerializedClauseVars,
                                      rematerializedClauseGuards);
    if (loopInfo && !upperBoundOverride)
      rematerializeOpenMPCapturedExpr(cgf, loopInfo->upperBound,
                                      rematerializedClauseVars,
                                      rematerializedClauseGuards);
    rematerializeGeneratedLoopExprs();
  }

  mlir::Value distScheduleChunk;
  bool distScheduleStatic = false;
  for (const OMPClause *clause : distributeClauses) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_dist_schedule: {
      const auto *distScheduleClause = cast<OMPDistScheduleClause>(clause);
      if (distScheduleClause->getDistScheduleKind() !=
          OMPC_DIST_SCHEDULE_static) {
        std::string feature = directiveName.str();
        feature += " dist_schedule kind";
        cgf.getCIRGenModule().errorNYI(clause->getBeginLoc(), feature);
        return mlir::failure();
      }
      distScheduleStatic = true;
      if (const Expr *chunk = distScheduleClause->getChunkSize())
        distScheduleChunk = distScheduleChunkOverride
                                ? distScheduleChunkOverride
                                : emitOpenMPIntegerExpr(cgf, chunk);
      break;
    }
    case llvm::omp::OMPC_collapse:
      break;
    default: {
      std::string clauseContext = directiveName.str();
      clauseContext += " clause";
      cgf.getCIRGenModule().errorNYI(
          clause->getBeginLoc(), clauseContext,
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      return mlir::failure();
    }
    }
  }

  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenFunction::OpenMPNontemporalDeclsRAII nontemporalDecls(cgf, s);

  auto createCompositeSimdOp =
      [&](const OpenMPSimdCompositeClauseData &simdClauseData) {
        mlir::ArrayAttr alignments =
            simdClauseData.alignments.empty()
                ? mlir::ArrayAttr{}
                : mlir::ArrayAttr::get(builder.getContext(),
                                       simdClauseData.alignments);
        mlir::ArrayAttr linearVarTypes =
            simdClauseData.linearVarTypes.empty()
                ? mlir::ArrayAttr{}
                : mlir::ArrayAttr::get(builder.getContext(),
                                       simdClauseData.linearVarTypes);
        auto simdOp = mlir::omp::SimdOp::create(
            builder, begin, simdClauseData.alignedVars, alignments,
            simdClauseData.ifExpr, simdClauseData.linearVars,
            simdClauseData.linearStepVars, linearVarTypes,
            simdClauseData.nontemporalVars, simdClauseData.order,
            simdClauseData.orderMod,
            /*private_vars=*/mlir::ValueRange{},
            /*private_syms=*/mlir::ArrayAttr{},
            /*private_needs_barrier=*/false,
            /*reduction_mod=*/mlir::omp::ReductionModifierAttr{},
            /*reduction_vars=*/mlir::ValueRange{},
            /*reduction_byref=*/mlir::DenseBoolArrayAttr{},
            /*reduction_syms=*/mlir::ArrayAttr{}, simdClauseData.safelen,
            simdClauseData.simdlen);
        simdOp.setComposite(true);
        return simdOp;
      };

  auto emitDistributeSimdLoop =
      [&](mlir::Value lowerBound, mlir::Value upperBound, mlir::Value step,
          bool loopInclusive, bool ivSigned, mlir::Location ivLoc,
          const OpenMPSimdCompositeClauseData &simdClauseData,
          llvm::function_ref<mlir::LogicalResult(mlir::Block *)> emitBody)
      -> mlir::LogicalResult {
    mlir::omp::DistributeOperands distributeClauseOps;
    auto distributeOp =
        mlir::omp::DistributeOp::create(builder, begin, distributeClauseOps);
    distributeOp.setComposite(true);
    if (distScheduleStatic)
      distributeOp.setDistScheduleStatic(true);
    if (distScheduleChunk)
      distributeOp.getDistScheduleChunkSizeMutable().assign(distScheduleChunk);

    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *distributeBlock = builder.createBlock(&distributeOp.getRegion());
    builder.setInsertionPointToEnd(distributeBlock);

    auto simdOp = createCompositeSimdOp(simdClauseData);
    mlir::Block *simdBlock = builder.createBlock(&simdOp.getRegion());
    builder.setInsertionPointToEnd(simdBlock);

    mlir::omp::LoopNestOperands loopClauseOps;
    loopClauseOps.loopLowerBounds.push_back(lowerBound);
    loopClauseOps.loopUpperBounds.push_back(upperBound);
    loopClauseOps.loopSteps.push_back(step);
    if (loopInclusive)
      loopClauseOps.loopInclusive = builder.getUnitAttr();

    auto loopNestOp =
        mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
    loopNestOp->setAttr("omp.frontend.iv_signed",
                        builder.getDenseBoolArrayAttr({ivSigned}));
    mlir::Block *loopBlock =
        builder.createBlock(&loopNestOp.getRegion(), {}, {upperBound.getType()},
                            {ivLoc});
    builder.setInsertionPointToEnd(loopBlock);

    mlir::LogicalResult bodyRes = emitBody(loopBlock);
    mlir::omp::YieldOp::create(builder, end);
    return bodyRes;
  };

  if (loopInfo) {
    bool ivSigned = loopInfo->iv->getType()->hasSignedIntegerRepresentation();
    mlir::Value lowerBound =
        lowerBoundOverride ? lowerBoundOverride
                           : emitOpenMPIntegerExpr(cgf, loopInfo->lowerBound);
    mlir::Value upperBound =
        upperBoundOverride ? upperBoundOverride
                           : emitOpenMPIntegerExpr(cgf, loopInfo->upperBound);
    std::optional<mlir::IntegerType> loopTy =
        getOpenMPWorksharingLoopType(cgf, lowerBound, upperBound);
    if (!loopTy) {
      generatedLoopInfo = getOpenMPSimpleGeneratedLoopInfo(s);
      if (!generatedLoopInfo) {
        std::string feature = directiveName.str();
        feature += " with unsupported loop bound types";
        cgf.getCIRGenModule().errorNYI(s.getSourceRange(), feature);
        return mlir::failure();
      }
      rematerializeGeneratedLoopExprs();
    } else {
      lowerBound = castOpenMPValueToBuiltinInt(cgf, begin, lowerBound, *loopTy,
                                               ivSigned);
      upperBound = castOpenMPValueToBuiltinInt(cgf, begin, upperBound, *loopTy,
                                               ivSigned);
      mlir::Value step =
          stepOverride
              ? castOpenMPValueToBuiltinInt(cgf, begin, stepOverride, *loopTy,
                                            ivSigned)
              : createOpenMPIntegerConstant(cgf, begin, *loopTy, 1);

      CharUnits ivAlign = cgf.getContext().getDeclAlign(loopInfo->iv);
      mlir::Type ivType = cgf.convertTypeForMem(loopInfo->iv->getType());
      Address ivSeedRawAlloca = Address::invalid();
      cgf.createTempAlloca(ivType, ivAlign, cgf.getLoc(loopInfo->iv->getLocation()),
                           loopInfo->iv->getName(), /*arraySize=*/nullptr,
                           &ivSeedRawAlloca);
      OpenMPSimdCompositeClauseData simdClauseData;
      CIRGenFunction::DeclMapRevertingRAII ivMapGuard(cgf, loopInfo->iv);
      cgf.replaceAddrOfLocalVar(loopInfo->iv, ivSeedRawAlloca);
      if (mlir::failed(collectOpenMPSimdClauseDataForComposite(
              cgf, simdClauses, simdClauseData)))
        return mlir::failure();

      return emitDistributeSimdLoop(
          lowerBound, upperBound, step, loopInfo->upperInclusive, ivSigned,
          cgf.getLoc(loopInfo->iv->getLocation()), simdClauseData,
          [&](mlir::Block *loopBlock) -> mlir::LogicalResult {
            auto logicalIVType =
                cir::IntType::get(&cgf.getMLIRContext(), loopTy->getWidth(),
                                  ivSigned);
            mlir::Value logicalIVValue = castOpenMPValueToCIRType(
                cgf, cgf.getLoc(loopInfo->iv->getLocation()),
                loopBlock->getArgument(0), logicalIVType);
            mlir::Value ivValue = logicalIVValue;
            if (logicalIVType != ivType)
              ivValue = builder.createIntCast(ivValue,
                                              mlir::cast<cir::IntType>(ivType));
            builder.createStore(cgf.getLoc(loopInfo->iv->getLocation()), ivValue,
                                ivSeedRawAlloca);

            return emitOMPLoopBody(cgf, builder, begin,
                                   loopInfo->forStmt->getBody());
          });
    }
  }

  assert(generatedLoopInfo && "expected generated loop info fallback");
  llvm::SmallVector<bool, 4> emitFinals;
  emitFinals.reserve(generatedLoopInfo->sourceIVs.size());
  for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs) {
    emitFinals.push_back(
        cgf.localDeclMap.count(sourceIV) || sourceIV->hasGlobalStorage() ||
        (cgf.capturedStmtInfo && cgf.capturedStmtInfo->lookup(sourceIV)));
  }

  auto emitGeneratedDistributeSimdLoop = [&]() -> mlir::LogicalResult {
    mlir::Value upperBound =
        emitOpenMPIntegerExpr(cgf, generatedLoopInfo->lastIteration);
    mlir::Type loopTy = upperBound.getType();
    mlir::Value lowerBound =
        createOpenMPIntegerConstant(cgf, begin, loopTy, 0);
    mlir::Value step = createOpenMPIntegerConstant(cgf, begin, loopTy, 1);
    const VarDecl *logicalIV = generatedLoopInfo->logicalIV;
    bool logicalIVSigned =
        logicalIV->getType()->hasSignedIntegerRepresentation();
    mlir::Location logicalIVLoc = cgf.getLoc(logicalIV->getLocation());

    auto createLoopVarStorage = [&](const VarDecl *vd) {
      CharUnits vdAlign = cgf.getContext().getDeclAlign(vd);
      mlir::Type vdType = cgf.convertTypeForMem(vd->getType());
      Address rawAlloca = Address::invalid();
      cgf.createTempAlloca(vdType, vdAlign, cgf.getLoc(vd->getLocation()),
                           vd->getName(), /*arraySize=*/nullptr, &rawAlloca);
      return std::pair{rawAlloca, vdType};
    };

    auto logicalIVStorage = createLoopVarStorage(logicalIV);
    Address logicalIVRawAlloca = logicalIVStorage.first;
    mlir::Type logicalIVType = logicalIVStorage.second;
    llvm::SmallVector<Address, 4> sourceIVRawAllocas;
    sourceIVRawAllocas.reserve(generatedLoopInfo->sourceIVs.size());
    for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs)
      sourceIVRawAllocas.push_back(createLoopVarStorage(sourceIV).first);

    OpenMPSimdCompositeClauseData simdClauseData;
    mlir::LogicalResult bodyRes = mlir::success();
    {
      llvm::SmallVector<std::unique_ptr<CIRGenFunction::DeclMapRevertingRAII>, 4>
          sourceIVMapGuards;
      sourceIVMapGuards.reserve(generatedLoopInfo->sourceIVs.size());
      for (auto [sourceIV, sourceIVRawAlloca] :
           llvm::zip_equal(generatedLoopInfo->sourceIVs, sourceIVRawAllocas)) {
        sourceIVMapGuards.push_back(
            std::make_unique<CIRGenFunction::DeclMapRevertingRAII>(cgf,
                                                                   sourceIV));
        cgf.replaceAddrOfLocalVar(sourceIV, sourceIVRawAlloca);
      }

      CIRGenFunction::DeclMapRevertingRAII logicalIVMapGuard(cgf, logicalIV);
      cgf.replaceAddrOfLocalVar(logicalIV, logicalIVRawAlloca);
      if (mlir::failed(collectOpenMPSimdClauseDataForComposite(
              cgf, simdClauses, simdClauseData)))
        return mlir::failure();

      bodyRes = emitDistributeSimdLoop(
          lowerBound, upperBound, step, /*loopInclusive=*/true,
          logicalIVSigned, logicalIVLoc, simdClauseData,
          [&](mlir::Block *loopBlock) -> mlir::LogicalResult {
            mlir::Value logicalIVValue = loopBlock->getArgument(0);
            if (auto ompIVTy =
                    mlir::dyn_cast<mlir::IntegerType>(logicalIVValue.getType())) {
              auto cirLoopTy = cir::IntType::get(&cgf.getMLIRContext(),
                                                 ompIVTy.getWidth(),
                                                 logicalIVSigned);
              logicalIVValue = mlir::UnrealizedConversionCastOp::create(
                                   builder, logicalIVLoc, cirLoopTy,
                                   logicalIVValue)
                                   .getResult(0);
              if (cirLoopTy != logicalIVType) {
                logicalIVValue = builder.createIntCast(
                    logicalIVValue, mlir::cast<cir::IntType>(logicalIVType));
              }
            } else {
              logicalIVValue = castOpenMPValueToCIRType(
                  cgf, logicalIVLoc, logicalIVValue, logicalIVType);
            }

            builder.createStore(logicalIVLoc, logicalIVValue,
                                logicalIVRawAlloca);
            for (const Expr *update : generatedLoopInfo->updates)
              cgf.emitIgnoredExpr(update);
            return emitOMPLoopBody(cgf, builder, begin, generatedLoopInfo->body);
          });
    }
    if (mlir::failed(bodyRes))
      return bodyRes;

    for (auto [idx, final] : llvm::enumerate(generatedLoopInfo->finals))
      if (emitFinals[idx] && final)
        cgf.emitIgnoredExpr(final);
    return bodyRes;
  };

  bool preCondConstant = false;
  if (generatedLoopInfo->preCond->EvaluateAsBooleanCondition(preCondConstant,
                                                             cgf.getContext())) {
    if (!preCondConstant)
      return mlir::success();
    return emitGeneratedDistributeSimdLoop();
  }

  mlir::LogicalResult res = mlir::success();
  mlir::Value preCond = cgf.emitOpOnBoolExpr(begin, generatedLoopInfo->preCond);
  cir::IfOp::create(
      builder, begin, preCond, /*withElseRegion=*/false,
      [&](mlir::OpBuilder &thenBuilder, mlir::Location) {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToEnd(thenBuilder.getInsertionBlock());
        res = emitGeneratedDistributeSimdLoop();
        builder.createYield(begin);
      });
  return res;
}

mlir::LogicalResult CIRGenFunction::emitOMPDistributeSimdDirective(
    const OMPDistributeSimdDirective &s) {
  llvm::SmallVector<const OMPClause *, 4> distributeClauses;
  llvm::SmallVector<const OMPClause *, 8> simdClauses;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if: {
      const auto *ifClause = cast<OMPIfClause>(clause);
      if (ifClause->getNameModifier() == llvm::omp::OMPD_unknown ||
          ifClause->getNameModifier() == llvm::omp::OMPD_simd) {
        simdClauses.push_back(clause);
        break;
      }
      getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                 "OpenMP distribute simd if clause modifier");
      return mlir::failure();
    }
    case llvm::omp::OMPC_dist_schedule:
      distributeClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_aligned:
    case llvm::omp::OMPC_nontemporal:
    case llvm::omp::OMPC_order:
    case llvm::omp::OMPC_safelen:
    case llvm::omp::OMPC_simdlen:
      simdClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_linear:
      // Match OGCG: distribute simd only permits loop-IV linear clauses, and
      // classic lowering keeps those on the ordinary distribute-loop IV path
      // instead of materializing generic simd linear privatization/finalization.
      break;
    case llvm::omp::OMPC_collapse:
      break;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), "OpenMP distribute simd clause",
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      return mlir::failure();
    }
  }

  return emitOMPDistributeSimdLoop(
      *this, s, distributeClauses, simdClauses,
      "OpenMP OMPDistributeSimdDirective", getLoc(s.getBeginLoc()),
      getLoc(s.getEndLoc()),
      /*rematerializeClauseExprs=*/false);
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetParallelGenericLoopDirective(
    const OMPTargetParallelGenericLoopDirective &s) {
  return emitSimpleOMPTargetParallelWsloopDirective(
      s, "OpenMP OMPTargetParallelGenericLoopDirective");
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetParallelForSimdDirective(
    const OMPTargetParallelForSimdDirective &s) {
  auto loopInfo = getOpenMPSimpleCanonicalForInfo(s);
  auto generatedLoopInfo = loopInfo ? std::optional<OpenMPSimpleGeneratedLoopInfo>{}
                                    : getOpenMPSimpleGeneratedLoopInfo(s);
  if (!loopInfo && !generatedLoopInfo) {
    getCIRGenModule().errorNYI(
        s.getSourceRange(),
        "OpenMP OMPTargetParallelForSimdDirective for "
        "non-canonical/non-unit-stride loop");
    return mlir::failure();
  }

  const bool isTargetDevice =
      getLangOpts().OpenMPIsTargetDevice && !forceOpenMPTargetOpEmission;
  const bool hasOffloadTargets = !getLangOpts().OMPTargetTriples.empty();
  const bool useTargetOffload = hasOffloadTargets && !isTargetDevice;
  const bool hasTargetAllocatorClauses = llvm::any_of(
      s.clauses(), [](const OMPClause *clause) {
        return clause->getClauseKind() == llvm::omp::OMPC_uses_allocators ||
               clause->getClauseKind() == llvm::omp::OMPC_allocate;
      });

  llvm::SmallVector<const OMPClause *, 8> targetClauses;
  llvm::SmallVector<const OMPClause *, 4> targetReductionClauses;
  llvm::SmallVector<const OMPClause *, 8> parallelClauses;
  llvm::SmallVector<const OMPClause *, 8> wsloopClauses;
  llvm::SmallVector<const OMPClause *, 8> simdClauses;
  llvm::SmallVector<const VarDecl *, 8> targetFirstprivateVars;
  llvm::SmallVector<const VarDecl *, 8> targetSyntheticFirstprivateVars;
  llvm::SmallVector<const Expr *, 2> hostOnlyTargetSideEffectExprs;
  OpenMPTargetParallelHostEvalClauseInfo parallelHostEvalClauses;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_depend:
    case llvm::omp::OMPC_nowait:
    case llvm::omp::OMPC_uses_allocators:
    case llvm::omp::OMPC_allocate:
    case llvm::omp::OMPC_thread_limit:
      targetClauses.push_back(clause);
      if (clause->getClauseKind() == llvm::omp::OMPC_thread_limit &&
          !useTargetOffload)
        hostOnlyTargetSideEffectExprs.push_back(
            cast<OMPThreadLimitClause>(clause)->getThreadLimit().front());
      break;
    case llvm::omp::OMPC_firstprivate:
      if (useTargetOffload || hasTargetAllocatorClauses) {
        targetClauses.push_back(clause);
        for (const Expr *varExpr : cast<OMPFirstprivateClause>(clause)->varlist())
          if (const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr))
            targetFirstprivateVars.push_back(vd);
      }
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_private:
      if (useTargetOffload || hasTargetAllocatorClauses)
        targetClauses.push_back(clause);
      wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_if: {
      const auto *ifClause = cast<OMPIfClause>(clause);
      if (ifClause->getNameModifier() == llvm::omp::OMPD_unknown ||
          ifClause->getNameModifier() == llvm::omp::OMPD_target) {
        if (useTargetOffload)
          targetClauses.push_back(clause);
        break;
      }
      if (ifClause->getNameModifier() == llvm::omp::OMPD_parallel) {
        if (useTargetOffload)
          parallelHostEvalClauses.IfClause = ifClause;
        else
          parallelClauses.push_back(clause);
        break;
      }
      if (ifClause->getNameModifier() == llvm::omp::OMPD_simd) {
        simdClauses.push_back(clause);
        break;
      }
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(),
          "OpenMP target parallel for simd if clause modifier");
      break;
    }
    case llvm::omp::OMPC_device:
    case llvm::omp::OMPC_map:
    case llvm::omp::OMPC_is_device_ptr:
    case llvm::omp::OMPC_has_device_addr:
      if (useTargetOffload)
        targetClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_num_threads:
      if (useTargetOffload) {
        parallelHostEvalClauses.NumThreadsClause =
            cast<OMPNumThreadsClause>(clause);
        break;
      }
      [[fallthrough]];
    case llvm::omp::OMPC_severity:
    case llvm::omp::OMPC_message:
    case llvm::omp::OMPC_proc_bind:
    case llvm::omp::OMPC_copyin:
    case llvm::omp::OMPC_default:
    case llvm::omp::OMPC_shared:
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_lastprivate:
      if (useTargetOffload)
        targetClauses.push_back(clause);
      [[fallthrough]];
    case llvm::omp::OMPC_schedule:
    case llvm::omp::OMPC_ordered:
    case llvm::omp::OMPC_collapse:
      wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_reduction:
      wsloopClauses.push_back(clause);
      targetReductionClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_linear:
      wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_aligned:
    case llvm::omp::OMPC_order:
    case llvm::omp::OMPC_nontemporal:
    case llvm::omp::OMPC_safelen:
    case llvm::omp::OMPC_simdlen:
      simdClauses.push_back(clause);
      break;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), "OpenMP target parallel for simd clause",
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      break;
    }
  }

  if (useTargetOffload || !targetClauses.empty())
    targetClauses.append(targetReductionClauses.begin(),
                         targetReductionClauses.end());

  if (useTargetOffload || hasTargetAllocatorClauses) {
    llvm::SmallVector<const OMPClause *, 16> targetNestedClauses;
    targetNestedClauses.append(wsloopClauses.begin(), wsloopClauses.end());
    targetNestedClauses.append(simdClauses.begin(), simdClauses.end());
    collectOpenMPSimdSyntheticFirstprivateVars(
        targetNestedClauses, targetSyntheticFirstprivateVars);
  }

  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  mlir::omp::TargetOp targetOp;
  auto emitParallelForSimdBody =
      [&](bool insideTargetRegion) -> mlir::LogicalResult {
    mlir::LogicalResult res = mlir::success();
    OpenMPNontemporalDeclsRAII nontemporalDecls(*this, s);
    llvm::SmallPtrSet<const VarDecl *, 4> rematerializedClauseVars;
    llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 8>
        rematerializedClauseGuards;
    if (insideTargetRegion) {
      rematerializeOpenMPClauseExprs(*this, parallelClauses,
                                     rematerializedClauseVars,
                                     rematerializedClauseGuards);
      rematerializeOpenMPClauseExprs(*this, wsloopClauses,
                                     rematerializedClauseVars,
                                     rematerializedClauseGuards);
      rematerializeOpenMPClauseExprs(*this, simdClauses,
                                     rematerializedClauseVars,
                                     rematerializedClauseGuards);
      if (loopInfo) {
        rematerializeOpenMPCapturedExpr(*this, loopInfo->lowerBound,
                                        rematerializedClauseVars,
                                        rematerializedClauseGuards);
        rematerializeOpenMPCapturedExpr(*this, loopInfo->upperBound,
                                        rematerializedClauseVars,
                                        rematerializedClauseGuards);
      }
    }

    llvm::SmallVector<mlir::Type> retTy;
    llvm::SmallVector<mlir::Value> operands;
    llvm::SmallVector<OpenMPCopyinMasterAddr, 4> copyinMasterAddrs =
        collectOpenMPCopyinMasterAddresses(*this, s);

    auto parallelOp =
        mlir::omp::ParallelOp::create(builder, begin, retTy, operands);
    OpenMPClauseEmissionInfo parallelClauseInfo =
        emitOpenMPClauses(parallelOp, parallelClauses);
    if (insideTargetRegion && useTargetOffload)
      bindOpenMPTargetParallelHostEvalArgs(targetOp, parallelOp,
                                           parallelHostEvalClauses);
    const CapturedStmt *parallelCaptured =
        s.getCapturedStmt(OpenMPDirectiveKind::OMPD_parallel);
    assert(parallelCaptured && "expected OpenMP parallel captured stmt");
    parallelOp->setAttr("cir.instrprof_func_name",
                        cgm.getStringAttrCached(
                            getOpenMPHelperPGOName(*parallelCaptured)));
    parallelOp->setAttr("cir.instrprof_func_linkage",
                        builder.getI32IntegerAttr(static_cast<int32_t>(
                            llvm::GlobalValue::InternalLinkage)));
    Address ivSeedRawAlloca = Address::invalid();
    mlir::Type ivType;
    llvm::SmallVector<mlir::Attribute, 4> parallelPrivateSyms;
    if (auto existingSyms = parallelOp.getPrivateSyms())
      llvm::append_range(parallelPrivateSyms, *existingSyms);
    if (loopInfo) {
      CharUnits ivAlign = getContext().getDeclAlign(loopInfo->iv);
      ivType = convertTypeForMem(loopInfo->iv->getType());
      Address ivSeedAddr =
          createTempAlloca(ivType, ivAlign, getLoc(loopInfo->iv->getLocation()),
                           loopInfo->iv->getName(), /*arraySize=*/nullptr,
                           &ivSeedRawAlloca);
      if (!localDeclMap.count(loopInfo->iv))
        declare(ivSeedRawAlloca.getPointer(), loopInfo->iv,
                loopInfo->iv->getType(),
                getLoc(loopInfo->iv->getSourceRange()), ivAlign);
      unsigned ivParallelPrivateIndex = parallelOp.getPrivateVars().size();
      auto ivPrivatizer = getOrCreateOpenMPPrivatePrivatizer(
          *this, builder, getLoc(loopInfo->iv->getLocation()), loopInfo->iv,
          ivType);
      parallelOp.getPrivateVarsMutable().append(ivSeedAddr.getPointer());
      parallelPrivateSyms.push_back(mlir::SymbolRefAttr::get(ivPrivatizer));
      parallelClauseInfo.privateIndices.push_back(ivParallelPrivateIndex);
      parallelClauseInfo.privateVars.push_back(loopInfo->iv);
    }
    parallelOp.setPrivateSymsAttr(
        mlir::ArrayAttr::get(builder.getContext(), parallelPrivateSyms));
    if (insideTargetRegion &&
        mlir::failed(appendOpenMPParallelNestedSyntheticFirstprivatePrivates(
            *this, parallelOp, wsloopClauses, parallelClauseInfo)))
      return mlir::failure();

    {
      mlir::OpBuilder::InsertionGuard guardCase(builder);
      llvm::SmallVector<mlir::Type, 4> privateArgTypes;
      llvm::SmallVector<mlir::Location, 4> privateArgLocs;
      for (mlir::Value privateVar : parallelOp.getPrivateVars()) {
        privateArgTypes.push_back(privateVar.getType());
        privateArgLocs.push_back(privateVar.getLoc());
      }

      mlir::Block *block = builder.createBlock(&parallelOp.getRegion(), {},
                                               privateArgTypes, privateArgLocs);
      builder.setInsertionPointToEnd(block);

      cir::ScopeOp::create(
          builder, begin, /*scopeBuilder=*/
          [&](mlir::OpBuilder &, mlir::Location) {
            auto pgoScope = enterOpenMPHelperRegionCounters(*parallelCaptured);
            LexicalScope ls{*this, begin, builder.getInsertionBlock()};
            llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                parallelPrivateDeclGuards;
            llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                reductionDeclGuards;
            llvm::DenseMap<const VarDecl *, Address> parallelOriginalDecls;
            llvm::DenseMap<const VarDecl *, Address> wsloopOriginalDecls;
            if (mlir::failed(remapOpenMPPrivateDecls(
                    *this, parallelOp.getOperation(), parallelClauseInfo,
                    parallelPrivateDeclGuards, &parallelOriginalDecls))) {
              res = mlir::failure();
              return;
            }

            OpenMPTLSCopyinDeclMapScope tlsCopyinDeclMapScope(*this, s);
            (void)emitOpenMPCopyinClause(*this, s, copyinMasterAddrs);
            incrementProfileCounter(
                parallelCaptured->getCapturedDecl()->getBody());

            if (!loopInfo) {
              assert(generatedLoopInfo &&
                     "expected generated simd loop info for non-canonical "
                     "target parallel for simd");

              if (mlir::failed(
                      emitOpenMPTransformPreInits(*this, s.getPreInits()))) {
                res = mlir::failure();
                return;
              }

              bool preCondConstant = false;
              if (generatedLoopInfo->preCond->EvaluateAsBooleanCondition(
                      preCondConstant, getContext()) &&
                  !preCondConstant)
                return;

              llvm::SmallVector<bool, 4> emitFinals;
              emitFinals.reserve(generatedLoopInfo->sourceIVs.size());
              for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs) {
                emitFinals.push_back(
                    localDeclMap.count(sourceIV) ||
                    sourceIV->hasGlobalStorage() ||
                    (capturedStmtInfo && capturedStmtInfo->lookup(sourceIV)));
              }

              auto emitGeneratedWsloop = [&]() -> mlir::LogicalResult {
                mlir::Value upperBound =
                    emitOpenMPIntegerExpr(*this,
                                          generatedLoopInfo->lastIteration);
                mlir::Type loopTy = upperBound.getType();
                mlir::Value lowerBound =
                    createOpenMPIntegerConstant(*this, begin, loopTy, 0);
                mlir::Value step =
                    createOpenMPIntegerConstant(*this, begin, loopTy, 1);

                auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
                wsloopOp.setNowait(true);
                wsloopOp.setComposite(true);
                OpenMPClauseEmissionInfo wsloopClauseInfo =
                    emitOpenMPClauses(wsloopOp, wsloopClauses,
                                      parallelClauseInfo.privateVars,
                                      /*allowWsloopConditionalLastprivate=*/
                                          true);
                llvm::SmallVector<mlir::Type, 4> wsloopPrivateArgTypes;
                llvm::SmallVector<mlir::Location, 4> wsloopPrivateArgLocs;
                for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
                  wsloopPrivateArgTypes.push_back(privateVar.getType());
                  wsloopPrivateArgLocs.push_back(privateVar.getLoc());
                }
                llvm::SmallVector<mlir::Type, 4> wsloopReductionArgTypes;
                llvm::SmallVector<mlir::Location, 4> wsloopReductionArgLocs;
                for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
                  wsloopReductionArgTypes.push_back(reductionVar.getType());
                  wsloopReductionArgLocs.push_back(reductionVar.getLoc());
                }
                wsloopPrivateArgTypes.append(wsloopReductionArgTypes);
                wsloopPrivateArgLocs.append(wsloopReductionArgLocs);
                mlir::Block *wsloopBlock = builder.createBlock(
                    &wsloopOp.getRegion(), {}, wsloopPrivateArgTypes,
                    wsloopPrivateArgLocs);
                builder.setInsertionPointToEnd(wsloopBlock);

                mlir::omp::SimdOperands simdClauseOps;
                auto simdOp =
                    mlir::omp::SimdOp::create(builder, begin, simdClauseOps);
                simdOp.setComposite(true);
                {
                  mlir::OpBuilder::InsertionGuard guard(builder);
                  builder.setInsertionPoint(wsloopOp->getBlock(),
                                            mlir::Block::iterator(wsloopOp));
                  if (mlir::failed(emitOpenMPSimdClausesForComposite(
                          *this, simdOp, simdClauses)))
                    return mlir::failure();
                }

                mlir::Block *simdBlock = builder.createBlock(&simdOp.getRegion());
                builder.setInsertionPointToEnd(simdBlock);

                mlir::omp::LoopNestOperands loopClauseOps;
                loopClauseOps.loopLowerBounds.push_back(lowerBound);
                loopClauseOps.loopUpperBounds.push_back(upperBound);
                loopClauseOps.loopSteps.push_back(step);
                loopClauseOps.loopInclusive = builder.getUnitAttr();

                auto loopNestOp =
                    mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
                bool logicalIVSigned = generatedLoopInfo->logicalIV->getType()
                                           ->hasSignedIntegerRepresentation();
                loopNestOp->setAttr(
                    "omp.frontend.iv_signed",
                    builder.getDenseBoolArrayAttr({logicalIVSigned}));
                mlir::Block *loopBlock = builder.createBlock(
                    &loopNestOp.getRegion(), {}, {loopTy},
                    {getLoc(generatedLoopInfo->logicalIV->getLocation())});
                builder.setInsertionPointToEnd(loopBlock);

                auto createLoopVarStorage = [&](const VarDecl *vd) {
                  CharUnits vdAlign = getContext().getDeclAlign(vd);
                  mlir::Type vdType = convertTypeForMem(vd->getType());
                  Address rawAlloca = Address::invalid();
                  createTempAlloca(vdType, vdAlign, getLoc(vd->getLocation()),
                                   vd->getName(), /*arraySize=*/nullptr,
                                   &rawAlloca);
                  return std::pair{rawAlloca, vdType};
                };

                auto [logicalIVRawAlloca, logicalIVType] =
                    createLoopVarStorage(generatedLoopInfo->logicalIV);
                llvm::SmallVector<Address, 4> sourceIVRawAllocas;
                sourceIVRawAllocas.reserve(generatedLoopInfo->sourceIVs.size());
                for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs) {
                  auto [sourceIVRawAlloca, sourceIVType] =
                      createLoopVarStorage(sourceIV);
                  (void)sourceIVType;
                  sourceIVRawAllocas.push_back(sourceIVRawAlloca);
                }

                mlir::LogicalResult bodyRes = mlir::success();
                {
                  llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                      wsloopPrivateDeclGuards;
                  llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                      sourceIVMapGuards;
                  sourceIVMapGuards.reserve(generatedLoopInfo->sourceIVs.size());
                  for (auto [sourceIV, sourceIVRawAlloca] :
                       llvm::zip_equal(generatedLoopInfo->sourceIVs,
                                       sourceIVRawAllocas)) {
                    sourceIVMapGuards.push_back(
                        std::make_unique<DeclMapRevertingRAII>(*this,
                                                               sourceIV));
                    replaceAddrOfLocalVar(sourceIV, sourceIVRawAlloca);
                  }
                  DeclMapRevertingRAII logicalIVMapGuard(
                      *this, generatedLoopInfo->logicalIV);
                  replaceAddrOfLocalVar(generatedLoopInfo->logicalIV,
                                        logicalIVRawAlloca);

                  auto wsloopPrivateArgs =
                      mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                          wsloopOp.getOperation())
                          .getPrivateBlockArgs();
                  if (mlir::failed(remapOpenMPPrivateDecls(
                          *this, wsloopOp.getOperation(), wsloopClauseInfo,
                          wsloopPrivateDeclGuards, &wsloopOriginalDecls))) {
                    res = mlir::failure();
                    return mlir::failure();
                  }
                  remapOpenMPReductionDecls(*this, wsloopOp.getOperation(),
                                            wsloopClauseInfo,
                                            reductionDeclGuards);

                  mlir::Value logicalIVValue = loopBlock->getArgument(0);
                  if (auto ompIVTy = mlir::dyn_cast<mlir::IntegerType>(
                          logicalIVValue.getType())) {
                    auto cirLoopTy = cir::IntType::get(
                        &getMLIRContext(), ompIVTy.getWidth(), logicalIVSigned);
                    logicalIVValue = mlir::UnrealizedConversionCastOp::create(
                                         builder,
                                         getLoc(generatedLoopInfo->logicalIV
                                                    ->getLocation()),
                                         cirLoopTy, logicalIVValue)
                                         .getResult(0);
                    if (cirLoopTy != logicalIVType) {
                      logicalIVValue = builder.createIntCast(
                          logicalIVValue,
                          mlir::cast<cir::IntType>(logicalIVType));
                    }
                  } else {
                    logicalIVValue = castOpenMPValueToCIRType(
                        *this,
                        getLoc(generatedLoopInfo->logicalIV->getLocation()),
                        logicalIVValue, logicalIVType);
                  }
                  builder.createStore(
                      getLoc(generatedLoopInfo->logicalIV->getLocation()),
                      logicalIVValue, logicalIVRawAlloca);
                  for (const Expr *update : generatedLoopInfo->updates)
                    emitIgnoredExpr(update);

                  bool hasConditionalLastprivate =
                      pushOpenMPLastprivateConditional(
                          wsloopClauses, logicalIVRawAlloca, s.getBeginLoc());
                  OpenMPScanParentDirectiveRAII scanDirectiveScope(
                      *this, s, collectOpenMPReductionRegionArgs(wsloopOp));
                  bodyRes = emitOMPLoopBody(*this, builder, begin,
                                            generatedLoopInfo->body);
                  if (!wsloopClauseInfo.lastprivateIndices.empty()) {
                    assert(wsloopClauseInfo.lastprivateIndices.size() ==
                               wsloopClauseInfo.lastprivateVars.size() &&
                           "mismatched OpenMP wsloop lastprivate metadata");
                    assert(wsloopClauseInfo.lastprivateIndices.size() ==
                               wsloopClauseInfo.lastprivateConditionals.size() &&
                           "mismatched OpenMP wsloop conditional-lastprivate "
                           "metadata");
                    mlir::Location ivLoc =
                        getLoc(generatedLoopInfo->logicalIV->getLocation());
                    auto cirIVTy = mlir::dyn_cast<cir::IntType>(logicalIVType);
                    assert(cirIVTy &&
                           "expected CIR integer type for generated logical "
                           "IV");
                    mlir::Value upperBoundValue = castOpenMPValueToCIRType(
                        *this, ivLoc, upperBound, logicalIVType);
                    mlir::Value stepValue =
                        builder.getConstInt(ivLoc, cirIVTy, 1);
                    mlir::Value nextIV =
                        builder.createAdd(ivLoc, logicalIVValue, stepValue);
                    mlir::Value isLastIteration = builder.createCompare(
                        ivLoc, cir::CmpOpKind::gt, nextIV, upperBoundValue);

                    cir::IfOp::create(
                        builder, ivLoc, isLastIteration,
                        /*withElseRegion=*/false,
                        [&](mlir::OpBuilder &thenBuilder,
                            mlir::Location thenLoc) {
                          mlir::OpBuilder::InsertionGuard guard(builder);
                          builder.setInsertionPointToEnd(
                              thenBuilder.getInsertionBlock());
                          for (auto [index, vd, srcVD, destVD, assignOp,
                                     isConditional] :
                               llvm::zip_equal(
                                   wsloopClauseInfo.lastprivateIndices,
                                   wsloopClauseInfo.lastprivateVars,
                                   wsloopClauseInfo.lastprivateSourceVDs,
                                   wsloopClauseInfo.lastprivateDestVDs,
                                   wsloopClauseInfo.lastprivateAssignmentOps,
                                   wsloopClauseInfo.lastprivateConditionals)) {
                            assert(index < wsloopPrivateArgs.size() &&
                                   "unexpected OpenMP wsloop lastprivate "
                                   "block arg index");
                            Address srcAddr =
                                getOpenMPPrivateStorageAddress(*this, vd);
                            const VarDecl *canonicalVD = vd->getCanonicalDecl();
                            Address destAddr = Address::invalid();
                            if (auto parallelIt =
                                    parallelOriginalDecls.find(canonicalVD);
                                parallelIt != parallelOriginalDecls.end()) {
                              destAddr = parallelIt->second;
                            } else if (auto wsloopIt =
                                           wsloopOriginalDecls.find(
                                               canonicalVD);
                                       wsloopIt != wsloopOriginalDecls.end()) {
                              destAddr = wsloopIt->second;
                            }
                            assert(destAddr.isValid() &&
                                   "missing lastprivate destination address");
                            if (isConditional)
                              emitOpenMPLastprivateConditionalFinalUpdate(
                                  vd, srcAddr, vd->getLocation());
                            emitOpenMPLastprivateCopy(
                                *this, vd, srcAddr, destAddr, srcVD, destVD,
                                assignOp);
                          }
                          cir::YieldOp::create(thenBuilder, thenLoc);
                        });
                  }
                  if (hasConditionalLastprivate)
                    popOpenMPLastprivateConditional();
                }
                mlir::omp::YieldOp::create(builder, end);
                builder.setInsertionPointAfter(wsloopOp);

                reductionDeclGuards.clear();
                parallelPrivateDeclGuards.clear();

                for (auto [idx, final] :
                     llvm::enumerate(generatedLoopInfo->finals))
                  if (emitFinals[idx] && final)
                    emitIgnoredExpr(final);

                return bodyRes;
              };

              if (generatedLoopInfo->preCond->EvaluateAsBooleanCondition(
                      preCondConstant, getContext())) {
                res = emitGeneratedWsloop();
                return;
              }

              mlir::Value preCond =
                  emitOpOnBoolExpr(begin, generatedLoopInfo->preCond);
              cir::IfOp::create(
                  builder, begin, preCond, /*withElseRegion=*/false,
                  [&](mlir::OpBuilder &thenBuilder, mlir::Location) {
                    mlir::OpBuilder::InsertionGuard guard(builder);
                    builder.setInsertionPointToEnd(
                        thenBuilder.getInsertionBlock());
                    res = emitGeneratedWsloop();
                    builder.createYield(begin);
                  });
              return;
            }

            DeclMapRevertingRAII ivMapGuard(*this, loopInfo->iv);

            mlir::Value lowerBound =
                emitOpenMPIntegerExpr(*this, loopInfo->lowerBound);
            mlir::Value upperBound =
                emitOpenMPIntegerExpr(*this, loopInfo->upperBound);
            mlir::Type loopTy = lowerBound.getType();
            if (loopTy != upperBound.getType()) {
              getCIRGenModule().errorNYI(
                  s.getSourceRange(),
                  "OpenMP OMPTargetParallelForSimdDirective with mismatched "
                  "loop bound types");
              return;
            }
            mlir::Value step =
                createOpenMPIntegerConstant(*this, begin, loopTy, 1);

            auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
            wsloopOp.setNowait(true);
            wsloopOp.setComposite(true);
            OpenMPClauseEmissionInfo wsloopClauseInfo =
                emitOpenMPClauses(wsloopOp, wsloopClauses,
                                  parallelClauseInfo.privateVars,
                                  /*allowWsloopConditionalLastprivate=*/true);
            llvm::SmallVector<mlir::Type, 4> wsloopPrivateArgTypes;
            llvm::SmallVector<mlir::Location, 4> wsloopPrivateArgLocs;
            for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
              wsloopPrivateArgTypes.push_back(privateVar.getType());
              wsloopPrivateArgLocs.push_back(privateVar.getLoc());
            }
            llvm::SmallVector<mlir::Type, 4> wsloopReductionArgTypes;
            llvm::SmallVector<mlir::Location, 4> wsloopReductionArgLocs;
            for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
              wsloopReductionArgTypes.push_back(reductionVar.getType());
              wsloopReductionArgLocs.push_back(reductionVar.getLoc());
            }
            wsloopPrivateArgTypes.append(wsloopReductionArgTypes);
            wsloopPrivateArgLocs.append(wsloopReductionArgLocs);
            mlir::Block *wsloopBlock = builder.createBlock(
                &wsloopOp.getRegion(), {}, wsloopPrivateArgTypes,
                wsloopPrivateArgLocs);
            builder.setInsertionPointToEnd(wsloopBlock);

            mlir::omp::SimdOperands simdClauseOps;
            auto simdOp =
                mlir::omp::SimdOp::create(builder, begin, simdClauseOps);
            simdOp.setComposite(true);
            {
              mlir::OpBuilder::InsertionGuard guard(builder);
              builder.setInsertionPoint(wsloopOp->getBlock(),
                                        mlir::Block::iterator(wsloopOp));
              if (mlir::failed(
                      emitOpenMPSimdClausesForComposite(*this, simdOp,
                                                        simdClauses))) {
                res = mlir::failure();
                return;
              }
            }
            if (mlir::failed(res))
              return;

            mlir::Block *simdBlock = builder.createBlock(&simdOp.getRegion());
            builder.setInsertionPointToEnd(simdBlock);

            mlir::omp::LoopNestOperands loopClauseOps;
            loopClauseOps.loopLowerBounds.push_back(lowerBound);
            loopClauseOps.loopUpperBounds.push_back(upperBound);
            loopClauseOps.loopSteps.push_back(step);
            if (loopInfo->upperInclusive)
              loopClauseOps.loopInclusive = builder.getUnitAttr();

            auto loopNestOp =
                mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
            loopNestOp->setAttr(
                "omp.frontend.iv_signed",
                builder.getDenseBoolArrayAttr(
                    {loopInfo->iv->getType()
                         ->hasSignedIntegerRepresentation()}));
            mlir::Block *loopBlock = builder.createBlock(
                &loopNestOp.getRegion(), {}, {loopTy},
                {getLoc(loopInfo->iv->getLocation())});
            builder.setInsertionPointToEnd(loopBlock);
            llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                wsloopPrivateDeclGuards;
            auto wsloopPrivateArgs =
                mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                    wsloopOp.getOperation())
                    .getPrivateBlockArgs();
            if (mlir::failed(remapOpenMPPrivateDecls(
                    *this, wsloopOp.getOperation(), wsloopClauseInfo,
                    wsloopPrivateDeclGuards, &wsloopOriginalDecls))) {
              res = mlir::failure();
              return;
            }
            remapOpenMPReductionDecls(*this, wsloopOp.getOperation(),
                                      wsloopClauseInfo, reductionDeclGuards);

            mlir::Value ivValue = castOpenMPValueToCIRType(
                *this, getLoc(loopInfo->iv->getLocation()),
                loopBlock->getArgument(0), ivType);
            auto ivIt = localDeclMap.find(loopInfo->iv);
            assert(ivIt != localDeclMap.end() &&
                   "expected loop IV address to be mapped");
            builder.createStore(getLoc(loopInfo->iv->getLocation()), ivValue,
                                ivIt->second);

            bool hasConditionalLastprivate = pushOpenMPLastprivateConditional(
                wsloopClauses, ivIt->second, s.getBeginLoc());
            OpenMPScanParentDirectiveRAII scanDirectiveScope(
                *this, s, collectOpenMPReductionRegionArgs(wsloopOp));
            res = emitOMPLoopBody(*this, builder, begin,
                                  loopInfo->forStmt->getBody());
            if (!wsloopClauseInfo.lastprivateIndices.empty()) {
              assert(wsloopClauseInfo.lastprivateIndices.size() ==
                         wsloopClauseInfo.lastprivateVars.size() &&
                     "mismatched OpenMP wsloop lastprivate metadata");
              assert(wsloopClauseInfo.lastprivateIndices.size() ==
                         wsloopClauseInfo.lastprivateConditionals.size() &&
                     "mismatched OpenMP wsloop conditional-lastprivate "
                     "metadata");
              mlir::Location ivLoc = getLoc(loopInfo->iv->getLocation());
              auto cirIVTy = mlir::dyn_cast<cir::IntType>(ivType);
              assert(cirIVTy && "expected CIR integer type for loop IV");
              mlir::Value upperBoundValue =
                  castOpenMPValueToCIRType(*this, ivLoc, upperBound, ivType);
              mlir::Value stepValue = builder.getConstInt(ivLoc, cirIVTy, 1);
              mlir::Value nextIV = builder.createAdd(ivLoc, ivValue, stepValue);
              mlir::Value isLastIteration = builder.createCompare(
                  ivLoc, loopInfo->upperInclusive ? cir::CmpOpKind::gt
                                                  : cir::CmpOpKind::ge,
                  nextIV, upperBoundValue);

              cir::IfOp::create(
                  builder, ivLoc, isLastIteration, /*withElseRegion=*/false,
                  [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
                    mlir::OpBuilder::InsertionGuard guard(builder);
                    builder.setInsertionPointToEnd(
                        thenBuilder.getInsertionBlock());
                    for (auto [index, vd, srcVD, destVD, assignOp,
                               isConditional] :
                         llvm::zip_equal(
                             wsloopClauseInfo.lastprivateIndices,
                             wsloopClauseInfo.lastprivateVars,
                             wsloopClauseInfo.lastprivateSourceVDs,
                             wsloopClauseInfo.lastprivateDestVDs,
                             wsloopClauseInfo.lastprivateAssignmentOps,
                             wsloopClauseInfo.lastprivateConditionals)) {
                      assert(index < wsloopPrivateArgs.size() &&
                             "unexpected OpenMP wsloop lastprivate block arg "
                             "index");
                      Address srcAddr =
                          getOpenMPPrivateStorageAddress(*this, vd);
                      const VarDecl *canonicalVD = vd->getCanonicalDecl();
                      Address destAddr = Address::invalid();
                      if (auto parallelIt =
                              parallelOriginalDecls.find(canonicalVD);
                          parallelIt != parallelOriginalDecls.end()) {
                        destAddr = parallelIt->second;
                      } else if (auto wsloopIt =
                                     wsloopOriginalDecls.find(canonicalVD);
                                 wsloopIt != wsloopOriginalDecls.end()) {
                        destAddr = wsloopIt->second;
                      }
                      assert(destAddr.isValid() &&
                             "missing lastprivate destination address");
                      if (isConditional)
                        emitOpenMPLastprivateConditionalFinalUpdate(
                            vd, srcAddr, vd->getLocation());
                      emitOpenMPLastprivateCopy(*this, vd, srcAddr, destAddr,
                                                srcVD, destVD, assignOp);
                    }
                    cir::YieldOp::create(thenBuilder, thenLoc);
                  });
            }
            if (hasConditionalLastprivate)
              popOpenMPLastprivateConditional();
            mlir::omp::YieldOp::create(builder, end);
            builder.setInsertionPointAfter(wsloopOp);

            reductionDeclGuards.clear();
          });

      mlir::omp::TerminatorOp::create(builder, end);
    }
    return res;
  };

  llvm::SmallPtrSet<const VarDecl *, 4> materializedClauseVars;
  llvm::SmallVector<mlir::Value, 2> hostEvalVars;
  if (!useTargetOffload) {
    for (const OMPClause *clause : s.clauses()) {
      if (clause->getClauseKind() != llvm::omp::OMPC_device)
        continue;
      materializeOpenMPCapturedClauseExpr(
          *this, cast<OMPDeviceClause>(clause)->getDevice(),
          materializedClauseVars);
    }
    for (const Expr *expr : hostOnlyTargetSideEffectExprs)
      materializeOpenMPCapturedClauseExpr(*this, expr, materializedClauseVars);
    if (targetClauses.empty())
      return emitParallelForSimdBody(/*insideTargetRegion=*/false);

    materializeOMPTargetClauseExprs(*this, parallelClauses,
                                    materializedClauseVars);
    materializeOMPTargetClauseExprs(*this, wsloopClauses,
                                    materializedClauseVars);
    materializeOMPTargetClauseExprs(*this, simdClauses, materializedClauseVars);
    if (loopInfo) {
      materializeOpenMPCapturedClauseExpr(*this, loopInfo->lowerBound,
                                          materializedClauseVars);
      materializeOpenMPCapturedClauseExpr(*this, loopInfo->upperBound,
                                          materializedClauseVars);
    }
  } else {
    materializeOMPTargetClauseExprs(*this, targetClauses,
                                    materializedClauseVars);
    materializeOpenMPTargetParallelHostEvalClauseExprs(
        *this, parallelHostEvalClauses, materializedClauseVars);
    hostEvalVars =
        emitOpenMPTargetParallelHostEvalVars(*this, parallelHostEvalClauses);
  }

  targetOp = mlir::omp::TargetOp::create(
      builder, begin,
      /*allocate_vars=*/mlir::ValueRange{},
      /*allocator_vars=*/mlir::ValueRange{},
      /*bare=*/false,
      /*depend_kinds=*/mlir::ArrayAttr{},
      /*depend_vars=*/mlir::ValueRange{},
      /*depend_sizes=*/mlir::ValueRange{},
      /*device=*/mlir::Value{},
      /*has_device_addr_vars=*/mlir::ValueRange{},
      /*host_eval_vars=*/hostEvalVars,
      /*if_expr=*/mlir::Value{},
      /*in_reduction_vars=*/mlir::ValueRange{},
      /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
      /*in_reduction_syms=*/mlir::ArrayAttr{},
      /*is_device_ptr_vars=*/mlir::ValueRange{},
      /*map_vars=*/mlir::ValueRange{},
      /*nowait=*/false,
      /*private_vars=*/mlir::ValueRange{},
      /*private_syms=*/mlir::ArrayAttr{},
      /*private_needs_barrier=*/false,
      /*thread_limit=*/mlir::Value{},
      /*private_maps=*/mlir::DenseI64ArrayAttr{});
  annotateOpenMPTargetParentName(*this, targetOp);
  OpenMPClauseEmissionInfo targetClauseInfo =
      emitOpenMPClauses(targetOp, targetClauses, targetFirstprivateVars);
  targetSyntheticFirstprivateVars = filterOpenMPSyntheticFirstprivateVars(
      targetSyntheticFirstprivateVars, targetClauseInfo);
  if (mlir::failed(appendOpenMPSyntheticFirstprivatePrivates(
          *this, targetOp, targetSyntheticFirstprivateVars, targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(appendOpenMPTargetCapturedVLASizePrivates(
          *this, targetOp,
          *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target),
          targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(maybeEmitOpenMPRuntimeDependPayload(
          *this, targetOp, s, targetClauseInfo)))
    return mlir::failure();
  if (!useTargetOffload && !isTargetDevice)
    if (mlir::failed(appendHostOnlyOMPTargetCapturedMappings(
            *this, targetOp, targetClauseInfo,
            *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target))))
      return mlir::failure();
  if (mlir::failed(finalizeOMPTargetPrivateMappings(*this, targetOp,
                                                    targetClauseInfo)))
    return mlir::failure();
  if (useTargetOffload)
    promoteOMPTargetPrivateMapsForOffload(targetOp, targetClauseInfo);

  return emitOMPTargetRegionBody(
      *this, targetOp, targetClauseInfo, begin, end,
      [&]() -> mlir::LogicalResult {
        return emitParallelForSimdBody(/*insideTargetRegion=*/true);
      });
}
mlir::LogicalResult
CIRGenFunction::emitOMPTargetSimdDirective(const OMPTargetSimdDirective &s) {
  const bool isTargetDevice =
      getLangOpts().OpenMPIsTargetDevice && !forceOpenMPTargetOpEmission;
  const bool hasOffloadTargets = !getLangOpts().OMPTargetTriples.empty();
  const bool useTargetOffload = hasOffloadTargets && !isTargetDevice;
  const bool hasTargetAllocatorClauses = llvm::any_of(
      s.clauses(), [](const OMPClause *clause) {
        return clause->getClauseKind() == llvm::omp::OMPC_uses_allocators ||
               clause->getClauseKind() == llvm::omp::OMPC_allocate;
      });

  llvm::SmallVector<const OMPClause *, 8> targetClauses;
  llvm::SmallVector<const OMPClause *, 4> targetReductionClauses;
  llvm::SmallVector<const OMPClause *, 8> simdClauses;
  llvm::SmallVector<const VarDecl *, 8> targetFirstprivateVars;
  llvm::SmallVector<const VarDecl *, 8> targetSyntheticFirstprivateVars;
  llvm::SmallVector<const Expr *, 2> hostOnlyTargetSideEffectExprs;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_depend:
    case llvm::omp::OMPC_nowait:
    case llvm::omp::OMPC_uses_allocators:
    case llvm::omp::OMPC_allocate:
    case llvm::omp::OMPC_thread_limit:
      targetClauses.push_back(clause);
      if (clause->getClauseKind() == llvm::omp::OMPC_thread_limit &&
          !useTargetOffload)
        hostOnlyTargetSideEffectExprs.push_back(
            cast<OMPThreadLimitClause>(clause)->getThreadLimit().front());
      break;
    case llvm::omp::OMPC_firstprivate:
      if (useTargetOffload || hasTargetAllocatorClauses) {
        targetClauses.push_back(clause);
        for (const Expr *varExpr : cast<OMPFirstprivateClause>(clause)->varlist())
          if (const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr))
            targetFirstprivateVars.push_back(vd);
      }
      break;
    case llvm::omp::OMPC_private:
      if (useTargetOffload || hasTargetAllocatorClauses)
        targetClauses.push_back(clause);
      simdClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_if: {
      const auto *ifClause = cast<OMPIfClause>(clause);
      if (ifClause->getNameModifier() == llvm::omp::OMPD_unknown ||
          ifClause->getNameModifier() == llvm::omp::OMPD_target) {
        if (useTargetOffload)
          targetClauses.push_back(clause);
        break;
      }
      if (ifClause->getNameModifier() == llvm::omp::OMPD_simd) {
        simdClauses.push_back(clause);
        break;
      }
      getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                 "OpenMP target simd if clause modifier");
      break;
    }
    case llvm::omp::OMPC_device:
    case llvm::omp::OMPC_map:
    case llvm::omp::OMPC_is_device_ptr:
    case llvm::omp::OMPC_has_device_addr:
      if (useTargetOffload)
        targetClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_linear:
      LLVM_FALLTHROUGH;
    case llvm::omp::OMPC_aligned:
    case llvm::omp::OMPC_order:
    case llvm::omp::OMPC_nontemporal:
    case llvm::omp::OMPC_collapse:
    case llvm::omp::OMPC_safelen:
    case llvm::omp::OMPC_simdlen:
      simdClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_reduction:
      simdClauses.push_back(clause);
      targetReductionClauses.push_back(clause);
      break;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), "OpenMP target simd clause",
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      break;
    }
  }

  if (useTargetOffload || !targetClauses.empty())
    targetClauses.append(targetReductionClauses.begin(),
                         targetReductionClauses.end());

  if (useTargetOffload || hasTargetAllocatorClauses)
    collectOpenMPSimdSyntheticFirstprivateVars(simdClauses,
                                               targetSyntheticFirstprivateVars);

  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  auto emitSimdBody = [&](bool insideTargetRegion) -> mlir::LogicalResult {
    llvm::SmallPtrSet<const VarDecl *, 4> rematerializedClauseVars;
    llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 8>
        rematerializedClauseGuards;
    if (insideTargetRegion) {
      rematerializeOpenMPClauseExprs(*this, simdClauses,
                                     rematerializedClauseVars,
                                     rematerializedClauseGuards);
      if (auto loopInfo = getOpenMPSimpleCanonicalForInfo(s)) {
        rematerializeOpenMPCapturedExpr(*this, loopInfo->lowerBound,
                                        rematerializedClauseVars,
                                        rematerializedClauseGuards);
        rematerializeOpenMPCapturedExpr(*this, loopInfo->upperBound,
                                        rematerializedClauseVars,
                                        rematerializedClauseGuards);
      }
    }

    return emitSimpleOpenMPSimdDirective(s, simdClauses,
                                         "OpenMP OMPTargetSimdDirective",
                                         "OpenMP target simd clause");
  };

  llvm::SmallPtrSet<const VarDecl *, 4> materializedClauseVars;
  if (!useTargetOffload) {
    for (const OMPClause *clause : s.clauses()) {
      if (clause->getClauseKind() != llvm::omp::OMPC_device)
        continue;
      materializeOpenMPCapturedClauseExpr(
          *this, cast<OMPDeviceClause>(clause)->getDevice(),
          materializedClauseVars);
    }
    for (const Expr *expr : hostOnlyTargetSideEffectExprs)
      materializeOpenMPCapturedClauseExpr(*this, expr, materializedClauseVars);
    if (targetClauses.empty())
      return emitSimdBody(/*insideTargetRegion=*/false);

    materializeOMPTargetClauseExprs(*this, simdClauses, materializedClauseVars);
    if (auto loopInfo = getOpenMPSimpleCanonicalForInfo(s)) {
      materializeOpenMPCapturedClauseExpr(*this, loopInfo->lowerBound,
                                          materializedClauseVars);
      materializeOpenMPCapturedClauseExpr(*this, loopInfo->upperBound,
                                          materializedClauseVars);
    }
  } else {
    materializeOMPTargetClauseExprs(*this, targetClauses,
                                    materializedClauseVars);
    materializeOMPTargetClauseExprs(*this, simdClauses,
                                    materializedClauseVars);
  }

  auto targetOp = mlir::omp::TargetOp::create(
      builder, begin,
      /*allocate_vars=*/mlir::ValueRange{},
      /*allocator_vars=*/mlir::ValueRange{},
      /*bare=*/false,
      /*depend_kinds=*/mlir::ArrayAttr{},
      /*depend_vars=*/mlir::ValueRange{},
      /*depend_sizes=*/mlir::ValueRange{},
      /*device=*/mlir::Value{},
      /*has_device_addr_vars=*/mlir::ValueRange{},
      /*host_eval_vars=*/mlir::ValueRange{},
      /*if_expr=*/mlir::Value{},
      /*in_reduction_vars=*/mlir::ValueRange{},
      /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
      /*in_reduction_syms=*/mlir::ArrayAttr{},
      /*is_device_ptr_vars=*/mlir::ValueRange{},
      /*map_vars=*/mlir::ValueRange{},
      /*nowait=*/false,
      /*private_vars=*/mlir::ValueRange{},
      /*private_syms=*/mlir::ArrayAttr{},
      /*private_needs_barrier=*/false,
      /*thread_limit=*/mlir::Value{},
      /*private_maps=*/mlir::DenseI64ArrayAttr{});
  annotateOpenMPTargetParentName(*this, targetOp);
  OpenMPClauseEmissionInfo targetClauseInfo =
      emitOpenMPClauses(targetOp, targetClauses, targetFirstprivateVars);
  targetSyntheticFirstprivateVars = filterOpenMPSyntheticFirstprivateVars(
      targetSyntheticFirstprivateVars, targetClauseInfo);
  if (mlir::failed(appendOpenMPSyntheticFirstprivatePrivates(
          *this, targetOp, targetSyntheticFirstprivateVars, targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(appendOpenMPTargetCapturedVLASizePrivates(
          *this, targetOp,
          *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target),
          targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(maybeEmitOpenMPRuntimeDependPayload(
          *this, targetOp, s, targetClauseInfo)))
    return mlir::failure();
  if (!useTargetOffload && !isTargetDevice)
    if (mlir::failed(appendHostOnlyOMPTargetCapturedMappings(
            *this, targetOp, targetClauseInfo,
            *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target))))
      return mlir::failure();
  if (mlir::failed(finalizeOMPTargetPrivateMappings(*this, targetOp,
                                                    targetClauseInfo)))
    return mlir::failure();
  if (useTargetOffload)
    promoteOMPTargetPrivateMapsForOffload(targetOp, targetClauseInfo);

  return emitOMPTargetRegionBody(
      *this, targetOp, targetClauseInfo, begin, end,
      [&]() -> mlir::LogicalResult {
        return emitSimdBody(/*insideTargetRegion=*/true);
      });
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetTeamsGenericLoopDirective(
    const OMPTargetTeamsGenericLoopDirective &s) {
  constexpr llvm::StringLiteral directiveName =
      "OpenMP OMPTargetTeamsGenericLoopDirective";
  if (s.canBeParallelFor())
    return emitOMPTargetTeamsDistributeParallelForDirectiveImpl(
        s, directiveName,
        /*allowBind=*/true,
        /*hasCancel=*/false);

  return emitOMPTargetTeamsDistributeDirectiveImpl(
      s, directiveName,
      /*allowBind=*/true);
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetUpdateDirective(
    const OMPTargetUpdateDirective &s) {
  // Match classic codegen: without target devices, target update is a no-op
  // and none of its clause expressions are evaluated.
  if (getLangOpts().OMPTargetTriples.empty())
    return mlir::success();

  llvm::SmallPtrSet<const VarDecl *, 4> materializedClauseVars;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if:
      materializeOpenMPCapturedClauseExpr(
          *this, cast<OMPIfClause>(clause)->getCondition(),
          materializedClauseVars);
      break;
    case llvm::omp::OMPC_device:
      materializeOpenMPCapturedClauseExpr(
          *this, cast<OMPDeviceClause>(clause)->getDevice(),
          materializedClauseVars);
      break;
    case llvm::omp::OMPC_to:
      for (const Expr *varExpr : cast<OMPToClause>(clause)->varlist())
        materializeOpenMPMapClauseExprs(*this, varExpr, materializedClauseVars);
      break;
    case llvm::omp::OMPC_from:
      for (const Expr *varExpr : cast<OMPFromClause>(clause)->varlist())
        materializeOpenMPMapClauseExprs(*this, varExpr, materializedClauseVars);
      break;
    default:
      break;
    }
  }

  auto targetUpdateOp = mlir::omp::TargetUpdateOp::create(
      builder, getLoc(s.getSourceRange()),
      /*depend_kinds=*/mlir::ArrayAttr{},
      /*depend_vars=*/mlir::ValueRange{},
      /*depend_sizes=*/mlir::ValueRange{},
      /*device=*/mlir::Value{},
      /*if_expr=*/mlir::Value{},
      /*map_vars=*/mlir::ValueRange{},
      /*nowait=*/false);
  (void)emitOpenMPClauses(targetUpdateOp, s.clauses());
  return mlir::success();
}
mlir::LogicalResult CIRGenFunction::emitOMPTeamsDistributeDirective(
    const OMPTeamsDistributeDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTeamsDistributeDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTeamsDistributeSimdDirective(
    const OMPTeamsDistributeSimdDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTeamsDistributeSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPTeamsDistributeParallelForSimdDirective(
    const OMPTeamsDistributeParallelForSimdDirective &s) {
  getCIRGenModule().errorNYI(
      s.getSourceRange(), "OpenMP OMPTeamsDistributeParallelForSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTeamsDistributeParallelForDirective(
    const OMPTeamsDistributeParallelForDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTeamsDistributeParallelForDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTeamsGenericLoopDirective(
    const OMPTeamsGenericLoopDirective &s) {
  constexpr llvm::StringLiteral directiveName =
      "OpenMP OMPTeamsGenericLoopDirective";

  llvm::SmallVector<const OMPClause *, 8> teamsClauses;
  llvm::SmallVector<const OMPClause *, 8> distributeClauses;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_if: {
      const auto *ifClause = cast<OMPIfClause>(clause);
      if (ifClause->getNameModifier() == llvm::omp::OMPD_unknown ||
          ifClause->getNameModifier() == llvm::omp::OMPD_teams) {
        teamsClauses.push_back(clause);
        break;
      }
      getCIRGenModule().errorNYI(clause->getBeginLoc(),
                                 "OpenMP teams loop if clause modifier");
      return mlir::failure();
    }
    case llvm::omp::OMPC_allocate:
    case llvm::omp::OMPC_num_teams:
    case llvm::omp::OMPC_thread_limit:
    case llvm::omp::OMPC_firstprivate:
    case llvm::omp::OMPC_private:
    case llvm::omp::OMPC_reduction:
      teamsClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_order:
    case llvm::omp::OMPC_collapse:
    case llvm::omp::OMPC_lastprivate:
      distributeClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_bind:
      // Match OGCG: teams loop lowers as teams + distribute regardless of the
      // valid bind kind.
      break;
    case llvm::omp::OMPC_default:
    case llvm::omp::OMPC_shared:
      break;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), (directiveName + " clause").str(),
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      return mlir::failure();
    }
  }

  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  auto teamsOp = mlir::omp::TeamsOp::create(
      builder, begin,
      /*allocate_vars=*/mlir::ValueRange{},
      /*allocator_vars=*/mlir::ValueRange{},
      /*if_expr=*/mlir::Value{},
      /*num_teams_lower=*/mlir::Value{},
      /*num_teams_upper=*/mlir::Value{},
      /*private_vars=*/mlir::ValueRange{},
      /*private_syms=*/mlir::ArrayAttr{},
      /*private_needs_barrier=*/false,
      /*reduction_mod=*/mlir::omp::ReductionModifierAttr{},
      /*reduction_vars=*/mlir::ValueRange{},
      /*reduction_byref=*/mlir::DenseBoolArrayAttr{},
      /*reduction_syms=*/mlir::ArrayAttr{},
      /*thread_limit=*/mlir::Value{});
  OpenMPClauseEmissionInfo teamsClauseInfo =
      emitOpenMPClauses(teamsOp, teamsClauses);

  return emitOMPTeamsRegionBody(
      *this, teamsOp, teamsClauseInfo,
      [&]() -> mlir::LogicalResult {
        return emitSimpleOMPDistributeDirective(*this, s, distributeClauses,
                                                directiveName,
                                                teamsClauseInfo.privateVars);
      },
      begin, end, /*wrapInScope=*/false);
}
mlir::LogicalResult
CIRGenFunction::emitOMPTargetTeamsDirective(const OMPTargetTeamsDirective &s) {
  const bool isTargetDevice =
      getLangOpts().OpenMPIsTargetDevice && !forceOpenMPTargetOpEmission;
  const bool hasOffloadTargets = !getLangOpts().OMPTargetTriples.empty();
  const bool useTargetOffload = hasOffloadTargets && !isTargetDevice;
  const Stmt *teamsBody =
      s.getCapturedStmt(llvm::omp::OMPD_teams)->getCapturedStmt();
  bool wrapTeamsBodyInScope = true;
  while (teamsBody) {
    if (const auto *captured = dyn_cast<CapturedStmt>(teamsBody)) {
      teamsBody = captured->getCapturedStmt();
      continue;
    }
    if (const auto *compound = dyn_cast<CompoundStmt>(teamsBody)) {
      if (compound->size() == 1 && compound->body_front()) {
        teamsBody = compound->body_front();
        continue;
      }
    }
    break;
  }
  if (isa_and_nonnull<OMPExecutableDirective>(teamsBody))
    wrapTeamsBodyInScope = false;

  llvm::SmallVector<const OMPClause *, 8> targetClauses;
  llvm::SmallVector<const OMPClause *, 4> targetReductionClauses;
  llvm::SmallVector<const OMPClause *, 8> teamsClauses;
  llvm::SmallVector<const VarDecl *, 4> teamsDevicePointerVars;
  llvm::SmallPtrSet<const VarDecl *, 4> seenTeamsDevicePointerVars;
  const OMPNumTeamsClause *numTeamsClause = nullptr;
  const OMPThreadLimitClause *teamsThreadLimitClause = nullptr;
  auto recordTeamsDevicePointerVar =
      [&](const Expr *varExpr, bool allowArraySections) {
        const Expr *baseExpr = varExpr->IgnoreParenImpCasts();
        if (allowArraySections) {
          while (const auto *section = dyn_cast<ArraySectionExpr>(baseExpr))
            baseExpr = section->getBase()->IgnoreParenImpCasts();
          while (const auto *subscript =
                     dyn_cast<ArraySubscriptExpr>(baseExpr))
            baseExpr = subscript->getBase()->IgnoreParenImpCasts();
        }

        const auto *dre = dyn_cast<DeclRefExpr>(baseExpr);
        const auto *vd = dre ? dyn_cast<VarDecl>(dre->getDecl()) : nullptr;
        if (!vd)
          return;
        vd = vd->getCanonicalDecl();
        if (!vd->isLocalVarDeclOrParm() && !vd->hasGlobalStorage())
          return;
        if (seenTeamsDevicePointerVars.insert(vd->getCanonicalDecl()).second)
          teamsDevicePointerVars.push_back(vd);
      };
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_depend:
    case llvm::omp::OMPC_nowait:
      targetClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_firstprivate:
    case llvm::omp::OMPC_private:
      if (useTargetOffload)
        targetClauses.push_back(clause);
      teamsClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_num_teams:
      if (useTargetOffload) {
        numTeamsClause = cast<OMPNumTeamsClause>(clause);
        break;
      }
      teamsClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_reduction:
      teamsClauses.push_back(clause);
      targetReductionClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_thread_limit:
      if (useTargetOffload) {
        targetClauses.push_back(clause);
        teamsThreadLimitClause = cast<OMPThreadLimitClause>(clause);
        break;
      }
      teamsClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_if: {
      const auto *ifClause = cast<OMPIfClause>(clause);
      if (ifClause->getNameModifier() == llvm::omp::OMPD_unknown ||
          ifClause->getNameModifier() == llvm::omp::OMPD_target) {
        if (useTargetOffload)
          targetClauses.push_back(clause);
        break;
      }
      if (ifClause->getNameModifier() == llvm::omp::OMPD_teams) {
        teamsClauses.push_back(clause);
        break;
      }
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(),
          "OpenMP target teams target-level if clause under direct CIR");
      break;
    }
    case llvm::omp::OMPC_default:
      if (useTargetOffload)
        break;
      [[fallthrough]];
    case llvm::omp::OMPC_device:
    case llvm::omp::OMPC_map:
    case llvm::omp::OMPC_shared:
      // In the host-only fallback path there is no target data motion to
      // materialize. Match OGCG by accepting target-level map clauses and
      // lowering the teams region exactly like the no-map case.
      if (useTargetOffload &&
          (clause->getClauseKind() == llvm::omp::OMPC_device ||
           clause->getClauseKind() == llvm::omp::OMPC_map))
        targetClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_is_device_ptr:
    case llvm::omp::OMPC_has_device_addr:
      if (useTargetOffload) {
        targetClauses.push_back(clause);
        auto recordVars = [&]() {
          if (clause->getClauseKind() == llvm::omp::OMPC_is_device_ptr) {
            for (const Expr *varExpr :
                 cast<OMPIsDevicePtrClause>(clause)->varlist())
              recordTeamsDevicePointerVar(varExpr,
                                          /*allowArraySections=*/false);
          } else {
            for (const Expr *varExpr :
                 cast<OMPHasDeviceAddrClause>(clause)->varlist())
              recordTeamsDevicePointerVar(varExpr,
                                          /*allowArraySections=*/true);
          }
        };
        recordVars();
      }
      break;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), "OpenMP target teams clause",
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      break;
    }
  }

  if (useTargetOffload || !targetClauses.empty())
    targetClauses.append(targetReductionClauses.begin(),
                         targetReductionClauses.end());

  llvm::SmallPtrSet<const VarDecl *, 4> materializedClauseVars;
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  if (!useTargetOffload) {
    for (const OMPClause *clause : s.clauses()) {
      if (clause->getClauseKind() != llvm::omp::OMPC_device)
        continue;
      // Host-only target teams still falls back to a plain teams region, but
      // target-level device(expr) must be evaluated for side effects.
      materializeOpenMPCapturedClauseExpr(
          *this, cast<OMPDeviceClause>(clause)->getDevice(),
          materializedClauseVars);
    }

    for (const OMPClause *clause : teamsClauses) {
      switch (clause->getClauseKind()) {
      case llvm::omp::OMPC_if:
        materializeOpenMPCapturedClauseExpr(
            *this, cast<OMPIfClause>(clause)->getCondition(),
            materializedClauseVars);
        break;
      case llvm::omp::OMPC_num_teams:
        materializeOpenMPCapturedClauseExpr(
            *this, cast<OMPNumTeamsClause>(clause)->getNumTeams().front(),
            materializedClauseVars);
        break;
      case llvm::omp::OMPC_thread_limit:
        materializeOpenMPCapturedClauseExpr(
            *this, cast<OMPThreadLimitClause>(clause)->getThreadLimit().front(),
            materializedClauseVars);
        break;
      default:
        break;
      }
    }

    if (!targetClauses.empty()) {
      auto targetOp = mlir::omp::TargetOp::create(
          builder, begin,
          /*allocate_vars=*/mlir::ValueRange{},
          /*allocator_vars=*/mlir::ValueRange{},
          /*bare=*/false,
          /*depend_kinds=*/mlir::ArrayAttr{},
          /*depend_vars=*/mlir::ValueRange{},
          /*depend_sizes=*/mlir::ValueRange{},
          /*device=*/mlir::Value{},
          /*has_device_addr_vars=*/mlir::ValueRange{},
          /*host_eval_vars=*/mlir::ValueRange{},
          /*if_expr=*/mlir::Value{},
          /*in_reduction_vars=*/mlir::ValueRange{},
          /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
          /*in_reduction_syms=*/mlir::ArrayAttr{},
          /*is_device_ptr_vars=*/mlir::ValueRange{},
          /*map_vars=*/mlir::ValueRange{},
          /*nowait=*/false,
          /*private_vars=*/mlir::ValueRange{},
          /*private_syms=*/mlir::ArrayAttr{},
          /*private_needs_barrier=*/false,
          /*thread_limit=*/mlir::Value{},
          /*private_maps=*/mlir::DenseI64ArrayAttr{});
      annotateOpenMPTargetParentName(*this, targetOp);
      OpenMPClauseEmissionInfo targetClauseInfo =
          emitOpenMPClauses(targetOp, targetClauses);
      if (mlir::failed(appendOpenMPTargetCapturedVLASizePrivates(
              *this, targetOp,
              *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target),
              targetClauseInfo)))
        return mlir::failure();
      if (mlir::failed(maybeEmitOpenMPRuntimeDependPayload(
              *this, targetOp, s, targetClauseInfo)))
        return mlir::failure();
      if (mlir::failed(appendHostOnlyOMPTargetCapturedMappings(
              *this, targetOp, targetClauseInfo,
              *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target))))
        return mlir::failure();
      if (mlir::failed(finalizeOMPTargetPrivateMappings(*this, targetOp,
                                                        targetClauseInfo)))
        return mlir::failure();

      return emitOMPTargetRegionBody(
          *this, targetOp, targetClauseInfo, begin, end,
          [&]() -> mlir::LogicalResult {
            auto teamsOp = mlir::omp::TeamsOp::create(
                builder, begin,
                /*allocate_vars=*/mlir::ValueRange{},
                /*allocator_vars=*/mlir::ValueRange{},
                /*if_expr=*/mlir::Value{},
                /*num_teams_lower=*/mlir::Value{},
                /*num_teams_upper=*/mlir::Value{},
                /*private_vars=*/mlir::ValueRange{},
                /*private_syms=*/mlir::ArrayAttr{},
                /*private_needs_barrier=*/false,
                /*reduction_mod=*/mlir::omp::ReductionModifierAttr{},
                /*reduction_vars=*/mlir::ValueRange{},
                /*reduction_byref=*/mlir::DenseBoolArrayAttr{},
                /*reduction_syms=*/mlir::ArrayAttr{},
                /*thread_limit=*/mlir::Value{});
            OpenMPClauseEmissionInfo clauseInfo =
                emitOpenMPClauses(teamsOp, teamsClauses);
            return emitOMPTeamsRegionBody(
                *this, teamsOp, clauseInfo,
                s.getCapturedStmt(llvm::omp::OMPD_teams)->getCapturedStmt(),
                begin, end, wrapTeamsBodyInScope);
          },
          /*wrapInScope=*/false);
    }

    auto teamsOp = mlir::omp::TeamsOp::create(
        builder, begin,
        /*allocate_vars=*/mlir::ValueRange{},
        /*allocator_vars=*/mlir::ValueRange{},
        /*if_expr=*/mlir::Value{},
        /*num_teams_lower=*/mlir::Value{},
        /*num_teams_upper=*/mlir::Value{},
        /*private_vars=*/mlir::ValueRange{},
        /*private_syms=*/mlir::ArrayAttr{},
        /*private_needs_barrier=*/false,
        /*reduction_mod=*/mlir::omp::ReductionModifierAttr{},
        /*reduction_vars=*/mlir::ValueRange{},
        /*reduction_byref=*/mlir::DenseBoolArrayAttr{},
        /*reduction_syms=*/mlir::ArrayAttr{},
        /*thread_limit=*/mlir::Value{});
    OpenMPClauseEmissionInfo clauseInfo =
        emitOpenMPClauses(teamsOp, teamsClauses);

    return emitOMPTeamsRegionBody(
        *this, teamsOp, clauseInfo,
        s.getCapturedStmt(llvm::omp::OMPD_teams)->getCapturedStmt(), begin,
        end, wrapTeamsBodyInScope);
  }

  materializeOMPTargetClauseExprs(*this, targetClauses, materializedClauseVars);
  if (numTeamsClause) {
    materializeOpenMPCapturedClauseExpr(
        *this, numTeamsClause->getNumTeams().front(), materializedClauseVars);
  }

  llvm::SmallVector<mlir::Value, 2> hostEvalVars;
  if (numTeamsClause) {
    hostEvalVars.push_back(
        emitOpenMPIntegerExpr(*this, numTeamsClause->getNumTeams().front()));
  }
  if (teamsThreadLimitClause) {
    hostEvalVars.push_back(emitOpenMPIntegerExpr(
        *this, teamsThreadLimitClause->getThreadLimit().front()));
  }

  auto targetOp = mlir::omp::TargetOp::create(
      builder, begin,
      /*allocate_vars=*/mlir::ValueRange{},
      /*allocator_vars=*/mlir::ValueRange{},
      /*bare=*/false,
      /*depend_kinds=*/mlir::ArrayAttr{},
      /*depend_vars=*/mlir::ValueRange{},
      /*depend_sizes=*/mlir::ValueRange{},
      /*device=*/mlir::Value{},
      /*has_device_addr_vars=*/mlir::ValueRange{},
      /*host_eval_vars=*/hostEvalVars,
      /*if_expr=*/mlir::Value{},
      /*in_reduction_vars=*/mlir::ValueRange{},
      /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
      /*in_reduction_syms=*/mlir::ArrayAttr{},
      /*is_device_ptr_vars=*/mlir::ValueRange{},
      /*map_vars=*/mlir::ValueRange{},
      /*nowait=*/false,
      /*private_vars=*/mlir::ValueRange{},
      /*private_syms=*/mlir::ArrayAttr{},
      /*private_needs_barrier=*/false,
      /*thread_limit=*/mlir::Value{},
      /*private_maps=*/mlir::DenseI64ArrayAttr{});
  annotateOpenMPTargetParentName(*this, targetOp);
  OpenMPClauseEmissionInfo targetClauseInfo =
      emitOpenMPClauses(targetOp, targetClauses);
  if (mlir::failed(appendOpenMPTargetCapturedVLASizePrivates(
          *this, targetOp,
          *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target),
          targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(maybeEmitOpenMPRuntimeDependPayload(
          *this, targetOp, s, targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(finalizeOMPTargetPrivateMappings(*this, targetOp,
                                                    targetClauseInfo)))
    return mlir::failure();
  if (useTargetOffload)
    promoteOMPTargetPrivateMapsForOffload(targetOp, targetClauseInfo);

  return emitOMPTargetRegionBody(
      *this, targetOp, targetClauseInfo, begin, end,
      [&]() -> mlir::LogicalResult {
        auto teamsOp = mlir::omp::TeamsOp::create(
            builder, begin,
            /*allocate_vars=*/mlir::ValueRange{},
            /*allocator_vars=*/mlir::ValueRange{},
            /*if_expr=*/mlir::Value{},
            /*num_teams_lower=*/mlir::Value{},
            /*num_teams_upper=*/mlir::Value{},
            /*private_vars=*/mlir::ValueRange{},
            /*private_syms=*/mlir::ArrayAttr{},
            /*private_needs_barrier=*/false,
            /*reduction_mod=*/mlir::omp::ReductionModifierAttr{},
            /*reduction_vars=*/mlir::ValueRange{},
            /*reduction_byref=*/mlir::DenseBoolArrayAttr{},
            /*reduction_syms=*/mlir::ArrayAttr{},
            /*thread_limit=*/mlir::Value{});
        OpenMPClauseEmissionInfo teamsClauseInfo =
            emitOpenMPClauses(teamsOp, teamsClauses);

        auto targetArgIface =
            mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                targetOp.getOperation());
        auto hostEvalArgs = targetArgIface.getHostEvalBlockArgs();
        unsigned nextHostEvalArg = 0;
        if (numTeamsClause) {
          assert(nextHostEvalArg < hostEvalArgs.size() &&
                 "missing OpenMP target host_eval block arg for num_teams");
          teamsOp.getNumTeamsUpperMutable().assign(
              hostEvalArgs[nextHostEvalArg++]);
        }
        if (teamsThreadLimitClause) {
          assert(nextHostEvalArg < hostEvalArgs.size() &&
                 "missing OpenMP target host_eval block arg for thread_limit");
          teamsOp.getThreadLimitMutable().assign(
              hostEvalArgs[nextHostEvalArg++]);
        }
        if (mlir::failed(appendOpenMPSyntheticFirstprivatePrivates(
                *this, teamsOp, teamsDevicePointerVars, teamsClauseInfo)))
          return mlir::failure();

        return emitOMPTeamsRegionBody(
            *this, teamsOp, teamsClauseInfo,
            s.getCapturedStmt(llvm::omp::OMPD_teams)->getCapturedStmt(), begin,
            end, wrapTeamsBodyInScope);
      },
      /*wrapInScope=*/false);
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetTeamsDistributeDirectiveImpl(
    const OMPLoopDirective &s, llvm::StringRef directiveName,
    bool allowBind) {
  CIRGenFunction &cgf = *this;
  const bool isTargetDevice =
      cgf.getLangOpts().OpenMPIsTargetDevice &&
      !cgf.shouldEmitOpenMPTargetAsOperation();
  const bool hasOffloadTargets = !cgf.getLangOpts().OMPTargetTriples.empty();
  const bool useTargetOffload = hasOffloadTargets && !isTargetDevice;
  CIRGenBuilderTy &builder = cgf.getBuilder();

  llvm::SmallVector<const OMPClause *, 8> targetClauses;
  llvm::SmallVector<const OMPClause *, 4> targetReductionClauses;
  llvm::SmallVector<const OMPClause *, 8> teamsClauses;
  llvm::SmallVector<const OMPClause *, 8> distributeClauses;
  llvm::SmallVector<const VarDecl *, 4> teamsDevicePointerVars;
  llvm::SmallPtrSet<const VarDecl *, 4> seenTeamsDevicePointerVars;
  llvm::SmallVector<const VarDecl *, 4> targetFirstprivateVars;
  llvm::SmallVector<const VarDecl *, 4> targetSyntheticFirstprivateVars;
  const Expr *distScheduleChunkExpr = nullptr;
  const OMPNumTeamsClause *numTeamsClause = nullptr;
  const OMPThreadLimitClause *teamsThreadLimitClause = nullptr;
  auto recordTeamsDevicePointerVar =
      [&](const Expr *varExpr, bool allowArraySections) {
        const Expr *baseExpr = varExpr->IgnoreParenImpCasts();
        if (allowArraySections) {
          while (const auto *section = dyn_cast<ArraySectionExpr>(baseExpr))
            baseExpr = section->getBase()->IgnoreParenImpCasts();
          while (const auto *subscript =
                     dyn_cast<ArraySubscriptExpr>(baseExpr))
            baseExpr = subscript->getBase()->IgnoreParenImpCasts();
        }

        const auto *dre = dyn_cast<DeclRefExpr>(baseExpr);
        const auto *vd = dre ? dyn_cast<VarDecl>(dre->getDecl()) : nullptr;
        if (!vd)
          return;
        vd = vd->getCanonicalDecl();
        if (!vd->isLocalVarDeclOrParm() && !vd->hasGlobalStorage())
          return;
        if (seenTeamsDevicePointerVars.insert(vd->getCanonicalDecl()).second)
          teamsDevicePointerVars.push_back(vd);
      };
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_depend:
    case llvm::omp::OMPC_nowait:
      targetClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_firstprivate:
      if (useTargetOffload) {
        targetClauses.push_back(clause);
        for (const Expr *varExpr : cast<OMPFirstprivateClause>(clause)->varlist())
          if (const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr))
            targetFirstprivateVars.push_back(vd);
      }
      teamsClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_private:
      if (useTargetOffload)
        targetClauses.push_back(clause);
      teamsClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_num_teams:
      if (useTargetOffload) {
        numTeamsClause = cast<OMPNumTeamsClause>(clause);
        break;
      }
      teamsClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_thread_limit:
      if (useTargetOffload) {
        targetClauses.push_back(clause);
        teamsThreadLimitClause = cast<OMPThreadLimitClause>(clause);
        break;
      }
      teamsClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_if: {
      const auto *ifClause = cast<OMPIfClause>(clause);
      if (ifClause->getNameModifier() == llvm::omp::OMPD_unknown ||
          ifClause->getNameModifier() == llvm::omp::OMPD_target) {
        if (useTargetOffload)
          targetClauses.push_back(clause);
        break;
      }
      if (ifClause->getNameModifier() == llvm::omp::OMPD_teams) {
        teamsClauses.push_back(clause);
        break;
      }
      cgf.getCIRGenModule().errorNYI(
          clause->getBeginLoc(),
          "OpenMP target teams distribute if clause modifier");
      return mlir::failure();
    }
    case llvm::omp::OMPC_dist_schedule:
      if (const Expr *chunk =
              cast<OMPDistScheduleClause>(clause)->getChunkSize()) {
        distScheduleChunkExpr = chunk;
        if (const VarDecl *vd = getOpenMPVarDeclFromExpr(chunk))
          targetSyntheticFirstprivateVars.push_back(vd);
      }
      [[fallthrough]];
    case llvm::omp::OMPC_order:
    case llvm::omp::OMPC_collapse:
      distributeClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_default:
      if (useTargetOffload)
        break;
      [[fallthrough]];
    case llvm::omp::OMPC_device:
    case llvm::omp::OMPC_map:
    case llvm::omp::OMPC_shared:
      if (useTargetOffload &&
          (clause->getClauseKind() == llvm::omp::OMPC_device ||
           clause->getClauseKind() == llvm::omp::OMPC_map))
        targetClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_is_device_ptr:
    case llvm::omp::OMPC_has_device_addr:
      if (useTargetOffload) {
        targetClauses.push_back(clause);
        if (clause->getClauseKind() == llvm::omp::OMPC_is_device_ptr) {
          for (const Expr *varExpr :
               cast<OMPIsDevicePtrClause>(clause)->varlist())
            recordTeamsDevicePointerVar(varExpr, /*allowArraySections=*/false);
        } else {
          for (const Expr *varExpr :
               cast<OMPHasDeviceAddrClause>(clause)->varlist())
            recordTeamsDevicePointerVar(varExpr, /*allowArraySections=*/true);
        }
      }
      break;
    case llvm::omp::OMPC_bind:
      if (allowBind)
        break;
      LLVM_FALLTHROUGH;
    case llvm::omp::OMPC_lastprivate:
      cgf.getCIRGenModule().errorNYI(
          clause->getBeginLoc(), (directiveName + " clause").str(),
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      return mlir::failure();
    case llvm::omp::OMPC_reduction:
      teamsClauses.push_back(clause);
      targetReductionClauses.push_back(clause);
      break;
    default:
      cgf.getCIRGenModule().errorNYI(
          clause->getBeginLoc(), "OpenMP target teams distribute clause",
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      return mlir::failure();
    }
  }

  if (useTargetOffload || !targetClauses.empty())
    targetClauses.append(targetReductionClauses.begin(),
                         targetReductionClauses.end());

  auto loopInfo = getOpenMPSimpleCanonicalForInfo(s);
  if ((useTargetOffload || !targetClauses.empty()) && !loopInfo) {
    std::string feature = directiveName.str();
    feature += " for non-canonical/non-unit-stride loop";
    cgf.getCIRGenModule().errorNYI(s.getSourceRange(), feature);
    return mlir::failure();
  }

  mlir::Location begin = cgf.getLoc(s.getBeginLoc());
  mlir::Location end = cgf.getLoc(s.getEndLoc());
  auto emitDistributeLoopHostEvalVars =
      [&]() -> llvm::FailureOr<llvm::SmallVector<mlir::Value, 3>> {
    assert(loopInfo && "expected canonical loop info for target host_eval");
    mlir::Value lowerBound =
        emitOpenMPIntegerExpr(cgf, loopInfo->lowerBound);
    mlir::Value upperBound =
        emitOpenMPIntegerExpr(cgf, loopInfo->upperBound);
    if (lowerBound.getType() != upperBound.getType()) {
      std::string feature = directiveName.str();
      feature += " with mismatched loop bound types";
      cgf.getCIRGenModule().errorNYI(s.getSourceRange(), feature);
      return llvm::failure();
    }

    llvm::SmallVector<mlir::Value, 3> loopHostEvalVars;
    loopHostEvalVars.push_back(lowerBound);
    loopHostEvalVars.push_back(upperBound);
    loopHostEvalVars.push_back(
        createOpenMPIntegerConstant(cgf, begin, lowerBound.getType(), 1));
    return loopHostEvalVars;
  };
  auto emitTeamsDistributeBody =
      [&](bool insideTargetRegion,
          mlir::ValueRange hostEvalArgs = mlir::ValueRange{})
      -> mlir::LogicalResult {
    mlir::Value distScheduleChunkValue;
    mlir::Value lowerBoundValue;
    mlir::Value upperBoundValue;
    mlir::Value stepValue;
    if (insideTargetRegion && distScheduleChunkExpr)
      distScheduleChunkValue = emitOpenMPIntegerExpr(cgf, distScheduleChunkExpr);

    auto teamsOp = mlir::omp::TeamsOp::create(
        builder, begin,
        /*allocate_vars=*/mlir::ValueRange{},
        /*allocator_vars=*/mlir::ValueRange{},
        /*if_expr=*/mlir::Value{},
        /*num_teams_lower=*/mlir::Value{},
        /*num_teams_upper=*/mlir::Value{},
        /*private_vars=*/mlir::ValueRange{},
        /*private_syms=*/mlir::ArrayAttr{},
        /*private_needs_barrier=*/false,
        /*reduction_mod=*/mlir::omp::ReductionModifierAttr{},
        /*reduction_vars=*/mlir::ValueRange{},
        /*reduction_byref=*/mlir::DenseBoolArrayAttr{},
        /*reduction_syms=*/mlir::ArrayAttr{},
        /*thread_limit=*/mlir::Value{});
    CIRGenFunction::OpenMPClauseEmissionInfo teamsClauseInfo =
        cgf.emitOpenMPClauses(teamsOp, teamsClauses);

    unsigned nextHostEvalArg = 0;
    if (insideTargetRegion && useTargetOffload) {
      if (numTeamsClause) {
        assert(nextHostEvalArg < hostEvalArgs.size() &&
               "missing OpenMP target host_eval block arg for num_teams");
        teamsOp.getNumTeamsUpperMutable().assign(
            hostEvalArgs[nextHostEvalArg++]);
      }
      if (teamsThreadLimitClause) {
        assert(nextHostEvalArg < hostEvalArgs.size() &&
               "missing OpenMP target host_eval block arg for thread_limit");
        teamsOp.getThreadLimitMutable().assign(hostEvalArgs[nextHostEvalArg++]);
      }
      if (mlir::failed(appendOpenMPSyntheticFirstprivatePrivates(
              cgf, teamsOp, teamsDevicePointerVars, teamsClauseInfo)))
        return mlir::failure();
    }
    if (insideTargetRegion) {
      assert(nextHostEvalArg + 2 < hostEvalArgs.size() &&
             "missing OpenMP target host_eval block args for loop bounds");
      lowerBoundValue = hostEvalArgs[nextHostEvalArg++];
      upperBoundValue = hostEvalArgs[nextHostEvalArg++];
      stepValue = hostEvalArgs[nextHostEvalArg++];
    }

    return emitOMPTeamsRegionBody(
        cgf, teamsOp, teamsClauseInfo,
        [&]() -> mlir::LogicalResult {
          return emitOMPDistributeLoop(cgf, s, distributeClauses, directiveName,
                                       begin, end, insideTargetRegion,
                                       distScheduleChunkValue, lowerBoundValue,
                                       upperBoundValue, stepValue);
        },
        begin, end, /*wrapInScope=*/false);
  };

  llvm::SmallPtrSet<const VarDecl *, 4> materializedClauseVars;
  if (!useTargetOffload) {
    for (const OMPClause *clause : s.clauses()) {
      if (clause->getClauseKind() != llvm::omp::OMPC_device)
        continue;
      materializeOpenMPCapturedClauseExpr(
          cgf, cast<OMPDeviceClause>(clause)->getDevice(),
          materializedClauseVars);
    }

    for (const OMPClause *clause : teamsClauses) {
      switch (clause->getClauseKind()) {
      case llvm::omp::OMPC_if:
        materializeOpenMPCapturedClauseExpr(
            cgf, cast<OMPIfClause>(clause)->getCondition(),
            materializedClauseVars);
        break;
      case llvm::omp::OMPC_num_teams:
        materializeOpenMPCapturedClauseExpr(
            cgf, cast<OMPNumTeamsClause>(clause)->getNumTeams().front(),
            materializedClauseVars);
        break;
      case llvm::omp::OMPC_thread_limit:
        materializeOpenMPCapturedClauseExpr(
            cgf, cast<OMPThreadLimitClause>(clause)->getThreadLimit().front(),
            materializedClauseVars);
        break;
      default:
        break;
      }
    }
    if (distScheduleChunkExpr)
      materializeOpenMPCapturedClauseExpr(cgf, distScheduleChunkExpr,
                                          materializedClauseVars);

    if (!targetClauses.empty()) {
      materializeOpenMPCapturedClauseExpr(cgf, loopInfo->lowerBound,
                                          materializedClauseVars);
      materializeOpenMPCapturedClauseExpr(cgf, loopInfo->upperBound,
                                          materializedClauseVars);
      auto loopHostEvalVars = emitDistributeLoopHostEvalVars();
      if (llvm::failed(loopHostEvalVars))
        return mlir::failure();
      auto targetOp = mlir::omp::TargetOp::create(
          builder, begin,
          /*allocate_vars=*/mlir::ValueRange{},
          /*allocator_vars=*/mlir::ValueRange{},
          /*bare=*/false,
          /*depend_kinds=*/mlir::ArrayAttr{},
          /*depend_vars=*/mlir::ValueRange{},
          /*depend_sizes=*/mlir::ValueRange{},
          /*device=*/mlir::Value{},
          /*has_device_addr_vars=*/mlir::ValueRange{},
          /*host_eval_vars=*/*loopHostEvalVars,
          /*if_expr=*/mlir::Value{},
          /*in_reduction_vars=*/mlir::ValueRange{},
          /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
          /*in_reduction_syms=*/mlir::ArrayAttr{},
          /*is_device_ptr_vars=*/mlir::ValueRange{},
          /*map_vars=*/mlir::ValueRange{},
          /*nowait=*/false,
          /*private_vars=*/mlir::ValueRange{},
          /*private_syms=*/mlir::ArrayAttr{},
          /*private_needs_barrier=*/false,
          /*thread_limit=*/mlir::Value{},
          /*private_maps=*/mlir::DenseI64ArrayAttr{});
      annotateOpenMPTargetParentName(cgf, targetOp);
      CIRGenFunction::OpenMPClauseEmissionInfo targetClauseInfo =
          cgf.emitOpenMPClauses(targetOp, targetClauses, targetFirstprivateVars);
      targetSyntheticFirstprivateVars = filterOpenMPSyntheticFirstprivateVars(
          targetSyntheticFirstprivateVars, targetClauseInfo);
      if (mlir::failed(appendOpenMPSyntheticFirstprivatePrivates(
              cgf, targetOp, targetSyntheticFirstprivateVars,
              targetClauseInfo)))
        return mlir::failure();
      if (mlir::failed(appendOpenMPTargetCapturedVLASizePrivates(
              cgf, targetOp,
              *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target),
              targetClauseInfo)))
        return mlir::failure();
      if (mlir::failed(maybeEmitOpenMPRuntimeDependPayload(
              cgf, targetOp, s, targetClauseInfo)))
        return mlir::failure();
      if (mlir::failed(appendHostOnlyOMPTargetCapturedMappings(
              cgf, targetOp, targetClauseInfo,
              *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target))))
        return mlir::failure();
      if (mlir::failed(finalizeOMPTargetPrivateMappings(cgf, targetOp,
                                                        targetClauseInfo)))
        return mlir::failure();

      return emitOMPTargetRegionBody(
          cgf, targetOp, targetClauseInfo, begin, end,
          [&]() -> mlir::LogicalResult {
            auto targetArgIface =
                mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                    targetOp.getOperation());
            return emitTeamsDistributeBody(
                /*insideTargetRegion=*/true,
                targetArgIface.getHostEvalBlockArgs());
          },
          /*wrapInScope=*/false);
    }

    return emitTeamsDistributeBody(/*insideTargetRegion=*/false);
  }

  materializeOMPTargetClauseExprs(cgf, targetClauses, materializedClauseVars);
  if (numTeamsClause) {
    materializeOpenMPCapturedClauseExpr(
        cgf, numTeamsClause->getNumTeams().front(), materializedClauseVars);
  }
  if (distScheduleChunkExpr)
    materializeOpenMPCapturedClauseExpr(cgf, distScheduleChunkExpr,
                                        materializedClauseVars);
  materializeOpenMPCapturedClauseExpr(cgf, loopInfo->lowerBound,
                                      materializedClauseVars);
  materializeOpenMPCapturedClauseExpr(cgf, loopInfo->upperBound,
                                      materializedClauseVars);

  llvm::SmallVector<mlir::Value, 5> hostEvalVars;
  if (numTeamsClause) {
    hostEvalVars.push_back(
        emitOpenMPIntegerExpr(cgf, numTeamsClause->getNumTeams().front()));
  }
  if (teamsThreadLimitClause) {
    hostEvalVars.push_back(emitOpenMPIntegerExpr(
        cgf, teamsThreadLimitClause->getThreadLimit().front()));
  }
  if (auto loopHostEvalVars = emitDistributeLoopHostEvalVars();
      llvm::succeeded(loopHostEvalVars)) {
    hostEvalVars.append(loopHostEvalVars->begin(), loopHostEvalVars->end());
  } else {
    return mlir::failure();
  }

  auto targetOp = mlir::omp::TargetOp::create(
      builder, begin,
      /*allocate_vars=*/mlir::ValueRange{},
      /*allocator_vars=*/mlir::ValueRange{},
      /*bare=*/false,
      /*depend_kinds=*/mlir::ArrayAttr{},
      /*depend_vars=*/mlir::ValueRange{},
      /*depend_sizes=*/mlir::ValueRange{},
      /*device=*/mlir::Value{},
      /*has_device_addr_vars=*/mlir::ValueRange{},
      /*host_eval_vars=*/hostEvalVars,
      /*if_expr=*/mlir::Value{},
      /*in_reduction_vars=*/mlir::ValueRange{},
      /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
      /*in_reduction_syms=*/mlir::ArrayAttr{},
      /*is_device_ptr_vars=*/mlir::ValueRange{},
      /*map_vars=*/mlir::ValueRange{},
      /*nowait=*/false,
      /*private_vars=*/mlir::ValueRange{},
      /*private_syms=*/mlir::ArrayAttr{},
      /*private_needs_barrier=*/false,
      /*thread_limit=*/mlir::Value{},
      /*private_maps=*/mlir::DenseI64ArrayAttr{});
  annotateOpenMPTargetParentName(cgf, targetOp);
  CIRGenFunction::OpenMPClauseEmissionInfo targetClauseInfo =
      cgf.emitOpenMPClauses(targetOp, targetClauses, targetFirstprivateVars);
  targetSyntheticFirstprivateVars = filterOpenMPSyntheticFirstprivateVars(
      targetSyntheticFirstprivateVars, targetClauseInfo);
  if (mlir::failed(appendOpenMPSyntheticFirstprivatePrivates(
          cgf, targetOp, targetSyntheticFirstprivateVars, targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(appendOpenMPTargetCapturedVLASizePrivates(
          cgf, targetOp,
          *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target),
          targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(maybeEmitOpenMPRuntimeDependPayload(
          cgf, targetOp, s, targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(finalizeOMPTargetPrivateMappings(cgf, targetOp,
                                                    targetClauseInfo)))
    return mlir::failure();
  if (useTargetOffload)
    promoteOMPTargetPrivateMapsForOffload(targetOp, targetClauseInfo);

  return emitOMPTargetRegionBody(
      cgf, targetOp, targetClauseInfo, begin, end,
      [&]() -> mlir::LogicalResult {
        auto targetArgIface =
            mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                targetOp.getOperation());
        return emitTeamsDistributeBody(
            /*insideTargetRegion=*/true,
            targetArgIface.getHostEvalBlockArgs());
      },
      /*wrapInScope=*/false);
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetTeamsDistributeDirective(
    const OMPTargetTeamsDistributeDirective &s) {
  constexpr llvm::StringLiteral directiveName =
      "OpenMP OMPTargetTeamsDistributeDirective";
  return emitOMPTargetTeamsDistributeDirectiveImpl(
      s, directiveName,
      /*allowBind=*/false);
}
mlir::LogicalResult
CIRGenFunction::emitOMPTargetTeamsDistributeParallelForDirectiveImpl(
    const OMPLoopDirective &s, llvm::StringRef directiveName, bool allowBind,
    bool hasCancel) {
  if (hasCancel) {
    getCIRGenModule().errorNYI(
        s.getBeginLoc(),
        "OpenMP TargetTeamsDistributeParallelFor with Cancel");
    return mlir::failure();
  }

  auto loopInfo = getOpenMPSimpleCanonicalForInfo(s);
  auto generatedLoopInfo = getOpenMPSimpleGeneratedLoopInfo(s);
  bool useGeneratedLoopInfo = !loopInfo;
  if (loopInfo && generatedLoopInfo &&
      loopInfo->lowerBound->getType().getCanonicalType() !=
          loopInfo->upperBound->getType().getCanonicalType()) {
    useGeneratedLoopInfo = true;
  }
  if (!loopInfo && !generatedLoopInfo) {
    std::string feature = directiveName.str();
    feature += " for non-canonical/non-unit-stride loop";
    getCIRGenModule().errorNYI(s.getSourceRange(), feature);
    return mlir::failure();
  }

  const bool isTargetDevice =
      getLangOpts().OpenMPIsTargetDevice && !forceOpenMPTargetOpEmission;
  const bool hasOffloadTargets = !getLangOpts().OMPTargetTriples.empty();
  const bool useTargetOffload = hasOffloadTargets && !isTargetDevice;

  // When this combined construct is offloaded, the loop bounds reach the device
  // region as omp.target host_eval block arguments. A lastprivate clause needs
  // the upper bound for its last-iteration check, but a host_eval argument may
  // only be used as a loop bound in omp.loop_nest; reusing it for the check
  // produces IR the verifier rejects ("host_eval argument illegal use"). A
  // correct implementation must materialize a separate bound value, which is
  // not yet supported, so diagnose instead of emitting invalid IR. The
  // non-offload path materializes the bound directly and is unaffected.
  if (useTargetOffload &&
      llvm::any_of(s.clauses(), [](const OMPClause *c) {
        return c->getClauseKind() == llvm::omp::OMPC_lastprivate;
      })) {
    std::string feature = directiveName.str();
    feature += " lastprivate with host-evaluated loop bounds";
    getCIRGenModule().errorNYI(s.getSourceRange(), feature);
    return mlir::failure();
  }
  const bool hasTargetAllocatorClauses = llvm::any_of(
      s.clauses(), [](const OMPClause *clause) {
        return clause->getClauseKind() == llvm::omp::OMPC_uses_allocators ||
               clause->getClauseKind() == llvm::omp::OMPC_allocate;
      });

  llvm::SmallVector<const OMPClause *, 8> targetClauses;
  llvm::SmallVector<const OMPClause *, 4> targetReductionClauses;
  llvm::SmallVector<const OMPClause *, 8> teamsClauses;
  llvm::SmallVector<const OMPClause *, 8> distributeClauses;
  llvm::SmallVector<const OMPClause *, 8> parallelClauses;
  llvm::SmallVector<const OMPClause *, 8> wsloopClauses;
  llvm::SmallVector<const VarDecl *, 4> teamsDevicePointerVars;
  llvm::SmallPtrSet<const VarDecl *, 4> seenTeamsDevicePointerVars;
  llvm::SmallVector<const VarDecl *, 8> targetFirstprivateVars;
  llvm::SmallVector<const VarDecl *, 4> targetSyntheticFirstprivateVars;
  llvm::SmallVector<const Expr *, 2> hostOnlyTargetSideEffectExprs;
  const Expr *distScheduleChunkExpr = nullptr;
  const OMPNumTeamsClause *numTeamsClause = nullptr;
  const OMPThreadLimitClause *teamsThreadLimitClause = nullptr;
  OpenMPTargetParallelHostEvalClauseInfo parallelHostEvalClauses;

  auto recordTeamsDevicePointerVar =
      [&](const Expr *varExpr, bool allowArraySections) {
        const Expr *baseExpr = varExpr->IgnoreParenImpCasts();
        if (allowArraySections) {
          while (const auto *section = dyn_cast<ArraySectionExpr>(baseExpr))
            baseExpr = section->getBase()->IgnoreParenImpCasts();
          while (const auto *subscript = dyn_cast<ArraySubscriptExpr>(baseExpr))
            baseExpr = subscript->getBase()->IgnoreParenImpCasts();
        }

        const auto *dre = dyn_cast<DeclRefExpr>(baseExpr);
        const auto *vd = dre ? dyn_cast<VarDecl>(dre->getDecl()) : nullptr;
        if (!vd)
          return;
        vd = vd->getCanonicalDecl();
        if (!vd->isLocalVarDeclOrParm() && !vd->hasGlobalStorage())
          return;
        if (seenTeamsDevicePointerVars.insert(vd).second)
          teamsDevicePointerVars.push_back(vd);
      };

  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_depend:
    case llvm::omp::OMPC_nowait:
    case llvm::omp::OMPC_uses_allocators:
    case llvm::omp::OMPC_allocate:
      targetClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_thread_limit:
      targetClauses.push_back(clause);
      if (useTargetOffload) {
        teamsThreadLimitClause = cast<OMPThreadLimitClause>(clause);
      } else {
        hostOnlyTargetSideEffectExprs.push_back(
            cast<OMPThreadLimitClause>(clause)->getThreadLimit().front());
      }
      break;
    case llvm::omp::OMPC_firstprivate:
      if (useTargetOffload || hasTargetAllocatorClauses) {
        targetClauses.push_back(clause);
        for (const Expr *varExpr : cast<OMPFirstprivateClause>(clause)->varlist())
          if (const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr))
            targetFirstprivateVars.push_back(vd);
      }
      teamsClauses.push_back(clause);
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_private:
      if (useTargetOffload || hasTargetAllocatorClauses)
        targetClauses.push_back(clause);
      teamsClauses.push_back(clause);
      wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_num_teams:
      if (useTargetOffload) {
        numTeamsClause = cast<OMPNumTeamsClause>(clause);
        break;
      }
      teamsClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_if: {
      const auto *ifClause = cast<OMPIfClause>(clause);
      if (ifClause->getNameModifier() == llvm::omp::OMPD_unknown ||
          ifClause->getNameModifier() == llvm::omp::OMPD_target) {
        if (useTargetOffload)
          targetClauses.push_back(clause);
        break;
      }
      if (ifClause->getNameModifier() == llvm::omp::OMPD_teams) {
        teamsClauses.push_back(clause);
        break;
      }
      if (ifClause->getNameModifier() == llvm::omp::OMPD_parallel) {
        if (useTargetOffload)
          parallelHostEvalClauses.IfClause = ifClause;
        else
          parallelClauses.push_back(clause);
        break;
      }
      std::string feature = directiveName.str();
      feature += " if clause modifier";
      getCIRGenModule().errorNYI(clause->getBeginLoc(), feature);
      return mlir::failure();
    }
    case llvm::omp::OMPC_dist_schedule:
      if (const Expr *chunk =
              cast<OMPDistScheduleClause>(clause)->getChunkSize()) {
        distScheduleChunkExpr = chunk;
        if (const VarDecl *vd = getOpenMPVarDeclFromExpr(chunk))
          targetSyntheticFirstprivateVars.push_back(vd);
      }
      distributeClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_num_threads:
      if (useTargetOffload) {
        parallelHostEvalClauses.NumThreadsClause =
            cast<OMPNumThreadsClause>(clause);
        break;
      }
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_severity:
    case llvm::omp::OMPC_message:
    case llvm::omp::OMPC_proc_bind:
    case llvm::omp::OMPC_copyin:
    case llvm::omp::OMPC_default:
    case llvm::omp::OMPC_shared:
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_lastprivate:
      if (useTargetOffload)
        targetClauses.push_back(clause);
      wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_reduction:
      teamsClauses.push_back(clause);
      wsloopClauses.push_back(clause);
      targetReductionClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_schedule:
    case llvm::omp::OMPC_order:
    case llvm::omp::OMPC_ordered:
      wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_collapse:
      distributeClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_device:
    case llvm::omp::OMPC_map:
      if (useTargetOffload)
        targetClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_is_device_ptr:
    case llvm::omp::OMPC_has_device_addr:
      if (useTargetOffload) {
        targetClauses.push_back(clause);
        if (clause->getClauseKind() == llvm::omp::OMPC_is_device_ptr) {
          for (const Expr *varExpr :
               cast<OMPIsDevicePtrClause>(clause)->varlist())
            recordTeamsDevicePointerVar(varExpr, /*allowArraySections=*/false);
        } else {
          for (const Expr *varExpr :
               cast<OMPHasDeviceAddrClause>(clause)->varlist())
            recordTeamsDevicePointerVar(varExpr, /*allowArraySections=*/true);
        }
      }
      break;
    case llvm::omp::OMPC_bind:
      if (allowBind)
        break;
      LLVM_FALLTHROUGH;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), (directiveName + " clause").str(),
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      return mlir::failure();
    }
  }

  if (useTargetOffload || !targetClauses.empty())
    targetClauses.append(targetReductionClauses.begin(),
                         targetReductionClauses.end());

  if (useTargetOffload || !targetClauses.empty()) {
    collectOpenMPClauseSyntheticFirstprivateVars(parallelClauses,
                                                 targetSyntheticFirstprivateVars);
    collectOpenMPClauseSyntheticFirstprivateVars(wsloopClauses,
                                                 targetSyntheticFirstprivateVars);
  }

  bool distScheduleStatic = false;
  for (const OMPClause *clause : distributeClauses) {
    if (clause->getClauseKind() != llvm::omp::OMPC_dist_schedule)
      continue;
    const auto *distScheduleClause = cast<OMPDistScheduleClause>(clause);
    if (distScheduleClause->getDistScheduleKind() !=
        OMPC_DIST_SCHEDULE_static) {
      std::string feature = directiveName.str();
      feature += " dist_schedule kind";
      getCIRGenModule().errorNYI(clause->getBeginLoc(), feature);
      return mlir::failure();
    }
    distScheduleStatic = true;
  }

  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  auto emitDistributeLoopHostEvalVars =
      [&]() -> llvm::FailureOr<llvm::SmallVector<mlir::Value, 3>> {
    assert(!useGeneratedLoopInfo &&
           "generated loop info does not use canonical host-eval bounds");
    mlir::Value lowerBound =
        emitOpenMPIntegerExpr(*this, loopInfo->lowerBound);
    mlir::Value upperBound =
        emitOpenMPIntegerExpr(*this, loopInfo->upperBound);
    if (lowerBound.getType() != upperBound.getType()) {
      std::string feature = directiveName.str();
      feature += " with mismatched loop bound types";
      getCIRGenModule().errorNYI(s.getSourceRange(), feature);
      return llvm::failure();
    }

    llvm::SmallVector<mlir::Value, 3> loopHostEvalVars;
    loopHostEvalVars.push_back(lowerBound);
    loopHostEvalVars.push_back(upperBound);
    loopHostEvalVars.push_back(
        createOpenMPIntegerConstant(*this, begin, lowerBound.getType(), 1));
    return loopHostEvalVars;
  };

  auto emitTeamsDistributeParallelForBody =
      [&](bool insideTargetRegion,
          mlir::ValueRange hostEvalArgs = mlir::ValueRange{})
      -> mlir::LogicalResult {
    mlir::Value distScheduleChunkValue;
    if (distScheduleChunkExpr)
      distScheduleChunkValue = emitOpenMPIntegerExpr(*this, distScheduleChunkExpr);

    mlir::Value lowerBoundValue;
    mlir::Value upperBoundValue;
    mlir::Value stepValue;
    if (!insideTargetRegion && !useGeneratedLoopInfo) {
      lowerBoundValue = emitOpenMPIntegerExpr(*this, loopInfo->lowerBound);
      upperBoundValue = emitOpenMPIntegerExpr(*this, loopInfo->upperBound);
      if (lowerBoundValue.getType() != upperBoundValue.getType()) {
        std::string feature = directiveName.str();
        feature += " with mismatched loop bound types";
        getCIRGenModule().errorNYI(s.getSourceRange(), feature);
        return mlir::failure();
      }
      stepValue =
          createOpenMPIntegerConstant(*this, begin, lowerBoundValue.getType(), 1);
    }

    auto teamsOp = mlir::omp::TeamsOp::create(
        builder, begin,
        /*allocate_vars=*/mlir::ValueRange{},
        /*allocator_vars=*/mlir::ValueRange{},
        /*if_expr=*/mlir::Value{},
        /*num_teams_lower=*/mlir::Value{},
        /*num_teams_upper=*/mlir::Value{},
        /*private_vars=*/mlir::ValueRange{},
        /*private_syms=*/mlir::ArrayAttr{},
        /*private_needs_barrier=*/false,
        /*reduction_mod=*/mlir::omp::ReductionModifierAttr{},
        /*reduction_vars=*/mlir::ValueRange{},
        /*reduction_byref=*/mlir::DenseBoolArrayAttr{},
        /*reduction_syms=*/mlir::ArrayAttr{},
        /*thread_limit=*/mlir::Value{});
    OpenMPClauseEmissionInfo teamsClauseInfo =
        emitOpenMPClauses(teamsOp, teamsClauses);

    unsigned nextHostEvalArg = 0;
    if (insideTargetRegion && useTargetOffload) {
      if (numTeamsClause) {
        assert(nextHostEvalArg < hostEvalArgs.size() &&
               "missing OpenMP target host_eval block arg for num_teams");
        teamsOp.getNumTeamsUpperMutable().assign(hostEvalArgs[nextHostEvalArg++]);
      }
      if (teamsThreadLimitClause) {
        assert(nextHostEvalArg < hostEvalArgs.size() &&
               "missing OpenMP target host_eval block arg for thread_limit");
        teamsOp.getThreadLimitMutable().assign(hostEvalArgs[nextHostEvalArg++]);
      }
      if (mlir::failed(appendOpenMPSyntheticFirstprivatePrivates(
              *this, teamsOp, teamsDevicePointerVars, teamsClauseInfo)))
        return mlir::failure();
    }
    if (insideTargetRegion) {
      if (!useGeneratedLoopInfo) {
        assert(nextHostEvalArg + 2 < hostEvalArgs.size() &&
               "missing OpenMP target host_eval block args for loop bounds");
        lowerBoundValue = hostEvalArgs[nextHostEvalArg++];
        upperBoundValue = hostEvalArgs[nextHostEvalArg++];
        stepValue = hostEvalArgs[nextHostEvalArg++];
      }
    }
    unsigned parallelHostEvalArgStart = nextHostEvalArg;

    return emitOMPTeamsRegionBody(
        *this, teamsOp, teamsClauseInfo,
        [&]() -> mlir::LogicalResult {
          llvm::SmallPtrSet<const VarDecl *, 4> rematerializedClauseVars;
          llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 8>
              rematerializedClauseGuards;
          if (insideTargetRegion) {
            rematerializeOpenMPClauseExprs(*this, parallelClauses,
                                           rematerializedClauseVars,
                                           rematerializedClauseGuards);
            rematerializeOpenMPClauseExprs(*this, wsloopClauses,
                                           rematerializedClauseVars,
                                           rematerializedClauseGuards);
          }
          mlir::Value wsloopScheduleChunkValue;
          if (insideTargetRegion)
            if (const Expr *wsloopScheduleChunkExpr =
                    getOpenMPScheduleChunkExpr(wsloopClauses))
              wsloopScheduleChunkValue =
                  emitOpenMPIntegerExpr(*this,
                                        unwrapOpenMPCapturedExprInit(
                                            wsloopScheduleChunkExpr));

          llvm::SmallVector<OpenMPCopyinMasterAddr, 4> copyinMasterAddrs =
              collectOpenMPCopyinMasterAddresses(*this, s);

          llvm::SmallVector<mlir::Type> retTy;
          llvm::SmallVector<mlir::Value> operands;
          auto parallelOp =
              mlir::omp::ParallelOp::create(builder, begin, retTy, operands);
          parallelOp.setComposite(true);
          OpenMPClauseEmissionInfo parallelClauseInfo =
              emitOpenMPClauses(parallelOp, parallelClauses);
          if (insideTargetRegion && useTargetOffload) {
            unsigned parallelHostEvalArg = parallelHostEvalArgStart;
            if (parallelHostEvalClauses.IfClause) {
              assert(parallelHostEvalArg < hostEvalArgs.size() &&
                     "missing OpenMP target host_eval block arg for "
                     "parallel if");
              parallelOp.getIfExprMutable().assign(
                  hostEvalArgs[parallelHostEvalArg++]);
            }
            if (parallelHostEvalClauses.NumThreadsClause) {
              assert(parallelHostEvalArg < hostEvalArgs.size() &&
                     "missing OpenMP target host_eval block arg for "
                     "num_threads");
              parallelOp.getNumThreadsMutable().assign(
                  hostEvalArgs[parallelHostEvalArg++]);
              if (parallelHostEvalClauses.NumThreadsClause->getModifier() ==
                  OMPC_NUMTHREADS_strict) {
                // Mirror the
                // modifier into the CIR-namespaced discardable attribute
                // consumed by the OpenMP-to-LLVM translator (see
                // VisitOMPNumThreadsClause for full rationale).
                parallelOp.setNumThreadsModifier(
                    mlir::omp::NumThreadsModifier::strict);
                parallelOp->setAttr(
                    "cir.omp.num_threads_modifier",
                    mlir::omp::NumThreadsModifierAttr::get(
                        parallelOp.getContext(),
                        mlir::omp::NumThreadsModifier::strict));
              }
            }
          }

          const CapturedStmt *parallelCaptured =
              s.getCapturedStmt(OpenMPDirectiveKind::OMPD_parallel);
          assert(parallelCaptured && "expected OpenMP parallel captured stmt");
          parallelOp->setAttr(
              "cir.instrprof_func_name",
              cgm.getStringAttrCached(
                  getOpenMPHelperPGOName(*parallelCaptured)));
          parallelOp->setAttr(
              "cir.instrprof_func_linkage",
              builder.getI32IntegerAttr(static_cast<int32_t>(
                  llvm::GlobalValue::InternalLinkage)));

          Address ivSeedRawAlloca = Address::invalid();
          mlir::Type ivType;
          llvm::SmallVector<mlir::Attribute, 4> parallelPrivateSyms;
          if (auto existingSyms = parallelOp.getPrivateSyms())
            llvm::append_range(parallelPrivateSyms, *existingSyms);
          if (!useGeneratedLoopInfo) {
            CharUnits ivAlign = getContext().getDeclAlign(loopInfo->iv);
            ivType = convertTypeForMem(loopInfo->iv->getType());
            Address ivSeedAddr = createTempAlloca(
                ivType, ivAlign, getLoc(loopInfo->iv->getLocation()),
                loopInfo->iv->getName(), /*arraySize=*/nullptr,
                &ivSeedRawAlloca);
            if (!localDeclMap.count(loopInfo->iv))
              declare(ivSeedRawAlloca.getPointer(), loopInfo->iv,
                      loopInfo->iv->getType(),
                      getLoc(loopInfo->iv->getSourceRange()), ivAlign);
            unsigned ivParallelPrivateIndex = parallelOp.getPrivateVars().size();
            auto ivPrivatizer = getOrCreateOpenMPPrivatePrivatizer(
                *this, builder, getLoc(loopInfo->iv->getLocation()),
                loopInfo->iv, ivType);
            parallelOp.getPrivateVarsMutable().append(ivSeedAddr.getPointer());
            parallelPrivateSyms.push_back(mlir::SymbolRefAttr::get(ivPrivatizer));
            parallelClauseInfo.privateIndices.push_back(ivParallelPrivateIndex);
            parallelClauseInfo.privateVars.push_back(loopInfo->iv);
          }
          parallelOp.setPrivateSymsAttr(
              mlir::ArrayAttr::get(builder.getContext(), parallelPrivateSyms));
          if (insideTargetRegion &&
              mlir::failed(
                  appendOpenMPParallelNestedSyntheticFirstprivatePrivates(
                      *this, parallelOp, wsloopClauses,
                      parallelClauseInfo)))
            return mlir::failure();

          mlir::LogicalResult res = mlir::success();
          {
            mlir::OpBuilder::InsertionGuard guardCase(builder);
            llvm::SmallVector<mlir::Type, 4> privateArgTypes;
            llvm::SmallVector<mlir::Location, 4> privateArgLocs;
            for (mlir::Value privateVar : parallelOp.getPrivateVars()) {
              privateArgTypes.push_back(privateVar.getType());
              privateArgLocs.push_back(privateVar.getLoc());
            }

            mlir::Block *block = builder.createBlock(
                &parallelOp.getRegion(), {}, privateArgTypes, privateArgLocs);
            builder.setInsertionPointToEnd(block);

            auto pgoScope = enterOpenMPHelperRegionCounters(*parallelCaptured);
            LexicalScope ls{*this, begin, builder.getInsertionBlock()};
            llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                parallelPrivateDeclGuards;
            llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                wsloopPrivateDeclGuards;
            llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                reductionDeclGuards;
            llvm::DenseMap<const VarDecl *, Address> parallelOriginalDecls;
            llvm::DenseMap<const VarDecl *, Address> wsloopOriginalDecls;
            if (mlir::failed(remapOpenMPPrivateDecls(
                    *this, parallelOp.getOperation(), parallelClauseInfo,
                    parallelPrivateDeclGuards, &parallelOriginalDecls)))
              return mlir::failure();

            OpenMPTLSCopyinDeclMapScope tlsCopyinDecls(*this, s);
            (void)emitOpenMPCopyinClause(*this, s, copyinMasterAddrs);
            bool hasDisabledLastprivateConditional =
                pushDisabledOpenMPLastprivateConditional(s);
            auto popDisabledLastprivateConditional = llvm::scope_exit([&] {
              if (hasDisabledLastprivateConditional)
                popOpenMPLastprivateConditional();
            });
            incrementProfileCounter(
                parallelCaptured->getCapturedDecl()->getBody());

            if (useGeneratedLoopInfo) {
              auto emitGeneratedParallelBody = [&]() -> mlir::LogicalResult {
              assert(generatedLoopInfo &&
                     "expected generated loop info for target teams "
                     "distribute parallel for");

              if (mlir::failed(
                      emitOpenMPTransformPreInits(*this, s.getPreInits()))) {
                res = mlir::failure();
                return mlir::failure();
              }

              bool preCondConstant = false;
              if (generatedLoopInfo->preCond->EvaluateAsBooleanCondition(
                      preCondConstant, getContext()) &&
                  !preCondConstant)
                return mlir::success();

              llvm::SmallVector<bool, 4> emitFinals;
              emitFinals.reserve(generatedLoopInfo->sourceIVs.size());
              for (const VarDecl *sourceIV : generatedLoopInfo->sourceIVs) {
                emitFinals.push_back(localDeclMap.count(sourceIV) ||
                                     sourceIV->hasGlobalStorage() ||
                                     (capturedStmtInfo &&
                                      capturedStmtInfo->lookup(sourceIV)));
              }

              auto emitGeneratedWsloop = [&]() -> mlir::LogicalResult {
                mlir::Value upperBound =
                    emitOpenMPIntegerExpr(*this,
                                          generatedLoopInfo->lastIteration);
                mlir::Type loopTy = upperBound.getType();
                mlir::Value lowerBound =
                    createOpenMPIntegerConstant(*this, begin, loopTy, 0);
                mlir::Value step =
                    createOpenMPIntegerConstant(*this, begin, loopTy, 1);

                mlir::omp::DistributeOperands distributeClauseOps;
                auto distributeOp = mlir::omp::DistributeOp::create(
                    builder, begin, distributeClauseOps);
                distributeOp.setComposite(true);
                if (distScheduleStatic)
                  distributeOp.setDistScheduleStatic(true);
                if (distScheduleChunkValue)
                  distributeOp.getDistScheduleChunkSizeMutable().assign(
                      distScheduleChunkValue);

                {
                  mlir::OpBuilder::InsertionGuard guard(builder);
                  mlir::Block *distributeBlock =
                      builder.createBlock(&distributeOp.getRegion());
                  builder.setInsertionPoint(distributeOp);

                  auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
                  wsloopOp.setNowait(true);
                  wsloopOp.setComposite(true);
                  if (wsloopScheduleChunkValue)
                    wsloopOp.getScheduleChunkMutable().assign(
                        wsloopScheduleChunkValue);
                  OpenMPClauseEmissionInfo wsloopClauseInfo =
                      emitOpenMPClauses(
                          wsloopOp, wsloopClauses,
                          parallelClauseInfo.privateVars,
                          /*allowWsloopConditionalLastprivate=*/true);
                  wsloopOp->moveBefore(distributeBlock,
                                       distributeBlock->end());

                  llvm::SmallVector<mlir::Type, 4> wsloopPrivateArgTypes;
                  llvm::SmallVector<mlir::Location, 4> wsloopPrivateArgLocs;
                  for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
                    wsloopPrivateArgTypes.push_back(privateVar.getType());
                    wsloopPrivateArgLocs.push_back(privateVar.getLoc());
                  }
                  llvm::SmallVector<mlir::Type, 4> wsloopReductionArgTypes;
                  llvm::SmallVector<mlir::Location, 4> wsloopReductionArgLocs;
                  for (mlir::Value reductionVar :
                       wsloopOp.getReductionVars()) {
                    wsloopReductionArgTypes.push_back(reductionVar.getType());
                    wsloopReductionArgLocs.push_back(reductionVar.getLoc());
                  }
                  wsloopPrivateArgTypes.append(wsloopReductionArgTypes);
                  wsloopPrivateArgLocs.append(wsloopReductionArgLocs);
                  mlir::Block *wsloopBlock = builder.createBlock(
                      &wsloopOp.getRegion(), {}, wsloopPrivateArgTypes,
                      wsloopPrivateArgLocs);
                  builder.setInsertionPointToEnd(wsloopBlock);

                  mlir::omp::LoopNestOperands loopClauseOps;
                  loopClauseOps.loopLowerBounds.push_back(lowerBound);
                  loopClauseOps.loopUpperBounds.push_back(upperBound);
                  loopClauseOps.loopSteps.push_back(step);

                  auto loopNestOp = mlir::omp::LoopNestOp::create(
                      builder, begin, loopClauseOps);
                  bool logicalIVSigned =
                      generatedLoopInfo->logicalIV->getType()
                          ->hasSignedIntegerRepresentation();
                  loopNestOp->setAttr(
                      "omp.frontend.iv_signed",
                      builder.getDenseBoolArrayAttr({logicalIVSigned}));
                  mlir::Block *loopBlock = builder.createBlock(
                      &loopNestOp.getRegion(), {}, {loopTy},
                      {getLoc(generatedLoopInfo->logicalIV->getLocation())});
                  builder.setInsertionPointToEnd(loopBlock);

                  auto createLoopVarStorage = [&](const VarDecl *vd) {
                    CharUnits vdAlign = getContext().getDeclAlign(vd);
                    mlir::Type vdType = convertTypeForMem(vd->getType());
                    Address rawAlloca = Address::invalid();
                    createTempAlloca(vdType, vdAlign, getLoc(vd->getLocation()),
                                     vd->getName(), /*arraySize=*/nullptr,
                                     &rawAlloca);
                    return std::pair{rawAlloca, vdType};
                  };

                  auto [logicalIVRawAlloca, logicalIVType] =
                      createLoopVarStorage(generatedLoopInfo->logicalIV);
                  llvm::SmallVector<Address, 4> sourceIVRawAllocas;
                  sourceIVRawAllocas.reserve(
                      generatedLoopInfo->sourceIVs.size());
                  for (const VarDecl *sourceIV :
                       generatedLoopInfo->sourceIVs) {
                    auto [sourceIVRawAlloca, sourceIVType] =
                        createLoopVarStorage(sourceIV);
                    (void)sourceIVType;
                    sourceIVRawAllocas.push_back(sourceIVRawAlloca);
                  }

                  mlir::LogicalResult bodyRes = mlir::success();
                  {
                    llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                        generatedWsloopPrivateDeclGuards;
                    llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                        sourceIVMapGuards;
                    sourceIVMapGuards.reserve(
                        generatedLoopInfo->sourceIVs.size());
                    for (auto [sourceIV, sourceIVRawAlloca] :
                         llvm::zip_equal(generatedLoopInfo->sourceIVs,
                                         sourceIVRawAllocas)) {
                      sourceIVMapGuards.push_back(
                          std::make_unique<DeclMapRevertingRAII>(*this,
                                                                 sourceIV));
                      replaceAddrOfLocalVar(sourceIV, sourceIVRawAlloca);
                    }
                    DeclMapRevertingRAII logicalIVMapGuard(
                        *this, generatedLoopInfo->logicalIV);
                    replaceAddrOfLocalVar(generatedLoopInfo->logicalIV,
                                          logicalIVRawAlloca);

                    auto wsloopPrivateArgs =
                        mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                            wsloopOp.getOperation())
                            .getPrivateBlockArgs();
                    if (mlir::failed(remapOpenMPPrivateDecls(
                            *this, wsloopOp.getOperation(), wsloopClauseInfo,
                            generatedWsloopPrivateDeclGuards,
                            &wsloopOriginalDecls))) {
                      res = mlir::failure();
                      return mlir::failure();
                    }
                    remapOpenMPReductionDecls(*this, wsloopOp.getOperation(),
                                              wsloopClauseInfo,
                                              reductionDeclGuards);

                    mlir::Value logicalIVValue = loopBlock->getArgument(0);
                    if (auto ompIVTy = mlir::dyn_cast<mlir::IntegerType>(
                            logicalIVValue.getType())) {
                      auto cirLoopTy = cir::IntType::get(
                          &getMLIRContext(), ompIVTy.getWidth(),
                          logicalIVSigned);
                      logicalIVValue =
                          mlir::UnrealizedConversionCastOp::create(
                              builder,
                              getLoc(generatedLoopInfo->logicalIV
                                         ->getLocation()),
                              cirLoopTy, logicalIVValue)
                              .getResult(0);
                      if (cirLoopTy != logicalIVType) {
                        logicalIVValue = builder.createIntCast(
                            logicalIVValue,
                            mlir::cast<cir::IntType>(logicalIVType));
                      }
                    } else {
                      logicalIVValue = castOpenMPValueToCIRType(
                          *this,
                          getLoc(generatedLoopInfo->logicalIV->getLocation()),
                          logicalIVValue, logicalIVType);
                    }
                    builder.createStore(
                        getLoc(generatedLoopInfo->logicalIV->getLocation()),
                        logicalIVValue, logicalIVRawAlloca);
                    for (const Expr *update : generatedLoopInfo->updates)
                      emitIgnoredExpr(update);

                    bool hasConditionalLastprivate =
                        pushOpenMPLastprivateConditional(
                            wsloopClauses, logicalIVRawAlloca,
                            s.getBeginLoc());
                    OpenMPScanParentDirectiveRAII scanDirectiveScope(
                        *this, s, collectOpenMPReductionRegionArgs(wsloopOp));
                    bodyRes = emitOMPLoopBody(*this, builder, begin,
                                              generatedLoopInfo->body);
                    if (!wsloopClauseInfo.lastprivateIndices.empty()) {
                      assert(wsloopClauseInfo.lastprivateIndices.size() ==
                                 wsloopClauseInfo.lastprivateVars.size() &&
                             "mismatched OpenMP wsloop lastprivate metadata");
                      assert(wsloopClauseInfo.lastprivateIndices.size() ==
                                 wsloopClauseInfo.lastprivateConditionals
                                     .size() &&
                             "mismatched OpenMP wsloop conditional-lastprivate "
                             "metadata");
                      mlir::Location ivLoc =
                          getLoc(generatedLoopInfo->logicalIV->getLocation());
                      auto cirIVTy =
                          mlir::dyn_cast<cir::IntType>(logicalIVType);
                      assert(cirIVTy &&
                             "expected CIR integer type for generated "
                             "logical IV");
                      mlir::Value upperBoundValue = castOpenMPValueToCIRType(
                          *this, ivLoc, upperBound, logicalIVType);
                      mlir::Value stepValue =
                          builder.getConstInt(ivLoc, cirIVTy, 1);
                      mlir::Value nextIV =
                          builder.createAdd(ivLoc, logicalIVValue, stepValue);
                      mlir::Value isLastIteration = builder.createCompare(
                          ivLoc, cir::CmpOpKind::gt, nextIV, upperBoundValue);

                      cir::IfOp::create(
                          builder, ivLoc, isLastIteration,
                          /*withElseRegion=*/false,
                          [&](mlir::OpBuilder &thenBuilder,
                              mlir::Location thenLoc) {
                            mlir::OpBuilder::InsertionGuard guard(builder);
                            builder.setInsertionPointToEnd(
                                thenBuilder.getInsertionBlock());
                            for (auto [index, vd, srcVD, destVD, assignOp,
                                       isConditional] :
                                 llvm::zip_equal(
                                     wsloopClauseInfo.lastprivateIndices,
                                     wsloopClauseInfo.lastprivateVars,
                                     wsloopClauseInfo.lastprivateSourceVDs,
                                     wsloopClauseInfo.lastprivateDestVDs,
                                     wsloopClauseInfo.lastprivateAssignmentOps,
                                     wsloopClauseInfo
                                         .lastprivateConditionals)) {
                              assert(index < wsloopPrivateArgs.size() &&
                                     "unexpected OpenMP wsloop lastprivate "
                                     "block arg index");
                              Address srcAddr =
                                  getOpenMPPrivateStorageAddress(*this, vd);
                              const VarDecl *canonicalVD =
                                  vd->getCanonicalDecl();
                              Address destAddr = Address::invalid();
                              if (auto parallelIt =
                                      parallelOriginalDecls.find(canonicalVD);
                                  parallelIt != parallelOriginalDecls.end()) {
                                destAddr = parallelIt->second;
                              } else if (auto wsloopIt =
                                             wsloopOriginalDecls.find(
                                                 canonicalVD);
                                         wsloopIt !=
                                         wsloopOriginalDecls.end()) {
                                destAddr = wsloopIt->second;
                              }
                              assert(destAddr.isValid() &&
                                     "missing lastprivate destination "
                                     "address");
                              if (isConditional)
                                emitOpenMPLastprivateConditionalFinalUpdate(
                                    vd, srcAddr, vd->getLocation());
                              emitOpenMPLastprivateCopy(
                                  *this, vd, srcAddr, destAddr, srcVD,
                                  destVD, assignOp);
                            }
                            cir::YieldOp::create(thenBuilder, thenLoc);
                          });
                    }
                    if (hasConditionalLastprivate)
                      popOpenMPLastprivateConditional();
                  }
                  mlir::omp::YieldOp::create(builder, end);
                  builder.setInsertionPointAfter(distributeOp);

                  reductionDeclGuards.clear();
                  wsloopPrivateDeclGuards.clear();
                  parallelPrivateDeclGuards.clear();

                  for (auto [idx, final] :
                       llvm::enumerate(generatedLoopInfo->finals)) {
                    if (emitFinals[idx] && final)
                      emitIgnoredExpr(final);
                  }

                  return bodyRes;
                }
              };

              // Unlike plain parallel-loop generated lowering, distribute ->
              // wsloop must remain a direct composite chain under omp.parallel.
              // A dynamic zero-trip case is therefore modeled by the
              // synthesized lastIteration expression instead of wrapping the
              // loop wrappers in an outer cir.if.
              res = emitGeneratedWsloop();
              return res;
            };
              res = emitGeneratedParallelBody();
            } else {

            DeclMapRevertingRAII ivMapGuard(*this, loopInfo->iv);

            mlir::Type loopTy = lowerBoundValue.getType();
            if (loopTy != upperBoundValue.getType()) {
              std::string feature = directiveName.str();
              feature += " with mismatched loop bound types";
              getCIRGenModule().errorNYI(s.getSourceRange(), feature);
              return mlir::failure();
            }

            mlir::omp::DistributeOperands distributeClauseOps;
            auto distributeOp =
                mlir::omp::DistributeOp::create(builder, begin,
                                               distributeClauseOps);
            distributeOp.setComposite(true);
            if (distScheduleStatic)
              distributeOp.setDistScheduleStatic(true);
            if (distScheduleChunkValue)
              distributeOp.getDistScheduleChunkSizeMutable().assign(
                  distScheduleChunkValue);

            {
              mlir::OpBuilder::InsertionGuard guard(builder);
              mlir::Block *distributeBlock =
                  builder.createBlock(&distributeOp.getRegion());
              builder.setInsertionPoint(distributeOp);
              auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
              wsloopOp.setNowait(true);
              wsloopOp.setComposite(true);
              if (wsloopScheduleChunkValue)
                wsloopOp.getScheduleChunkMutable().assign(
                    wsloopScheduleChunkValue);
              OpenMPClauseEmissionInfo wsloopClauseInfo = emitOpenMPClauses(
                  wsloopOp, wsloopClauses, parallelClauseInfo.privateVars,
                  /*allowWsloopConditionalLastprivate=*/true);
              wsloopOp->moveBefore(distributeBlock, distributeBlock->end());

              llvm::SmallVector<mlir::Type, 4> wsloopPrivateArgTypes;
              llvm::SmallVector<mlir::Location, 4> wsloopPrivateArgLocs;
              for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
                wsloopPrivateArgTypes.push_back(privateVar.getType());
                wsloopPrivateArgLocs.push_back(privateVar.getLoc());
              }
              llvm::SmallVector<mlir::Type, 4> wsloopReductionArgTypes;
              llvm::SmallVector<mlir::Location, 4> wsloopReductionArgLocs;
              for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
                wsloopReductionArgTypes.push_back(reductionVar.getType());
                wsloopReductionArgLocs.push_back(reductionVar.getLoc());
              }
              wsloopPrivateArgTypes.append(wsloopReductionArgTypes);
              wsloopPrivateArgLocs.append(wsloopReductionArgLocs);
              mlir::Block *wsloopBlock = builder.createBlock(
                  &wsloopOp.getRegion(), {}, wsloopPrivateArgTypes,
                  wsloopPrivateArgLocs);
              builder.setInsertionPointToEnd(wsloopBlock);

              mlir::omp::LoopNestOperands loopClauseOps;
              loopClauseOps.loopLowerBounds.push_back(lowerBoundValue);
              loopClauseOps.loopUpperBounds.push_back(upperBoundValue);
              loopClauseOps.loopSteps.push_back(stepValue);
              if (loopInfo->upperInclusive)
                loopClauseOps.loopInclusive = builder.getUnitAttr();

              auto loopNestOp =
                  mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
              loopNestOp->setAttr(
                  "omp.frontend.iv_signed",
                  builder.getDenseBoolArrayAttr(
                      {loopInfo->iv->getType()
                           ->hasSignedIntegerRepresentation()}));
              mlir::Block *loopBlock = builder.createBlock(
                  &loopNestOp.getRegion(), {}, {loopTy},
                  {getLoc(loopInfo->iv->getLocation())});
              builder.setInsertionPointToEnd(loopBlock);
              auto wsloopPrivateArgs =
                  mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                      wsloopOp.getOperation())
                      .getPrivateBlockArgs();
              if (mlir::failed(remapOpenMPPrivateDecls(
                      *this, wsloopOp.getOperation(), wsloopClauseInfo,
                      wsloopPrivateDeclGuards, &wsloopOriginalDecls)))
                return mlir::failure();
              remapOpenMPReductionDecls(*this, wsloopOp.getOperation(),
                                        wsloopClauseInfo,
                                        reductionDeclGuards);

              mlir::Value ivValue = castOpenMPValueToCIRType(
                  *this, getLoc(loopInfo->iv->getLocation()),
                  loopBlock->getArgument(0), ivType);
              auto ivIt = localDeclMap.find(loopInfo->iv);
              assert(ivIt != localDeclMap.end() &&
                     "expected loop IV address to be mapped");
              builder.createStore(getLoc(loopInfo->iv->getLocation()), ivValue,
                                  ivIt->second);

              bool hasConditionalLastprivate = pushOpenMPLastprivateConditional(
                  wsloopClauses, ivIt->second, s.getBeginLoc());
              OpenMPScanParentDirectiveRAII scanDirectiveScope(
                  *this, s, collectOpenMPReductionRegionArgs(wsloopOp));
              res = emitOMPLoopBody(*this, builder, begin,
                                    loopInfo->forStmt->getBody());
              if (!wsloopClauseInfo.lastprivateIndices.empty()) {
                assert(wsloopClauseInfo.lastprivateIndices.size() ==
                           wsloopClauseInfo.lastprivateVars.size() &&
                       "mismatched OpenMP wsloop lastprivate metadata");
                assert(wsloopClauseInfo.lastprivateIndices.size() ==
                           wsloopClauseInfo.lastprivateConditionals.size() &&
                       "mismatched OpenMP wsloop conditional-lastprivate "
                       "metadata");
                mlir::Location ivLoc = getLoc(loopInfo->iv->getLocation());
                auto cirIVTy = mlir::dyn_cast<cir::IntType>(ivType);
                assert(cirIVTy && "expected CIR integer type for loop IV");
                mlir::Value upperBoundValueCIR = castOpenMPValueToCIRType(
                    *this, ivLoc, upperBoundValue, ivType);
                mlir::Value stepValueCIR =
                    builder.getConstInt(ivLoc, cirIVTy, 1);
                mlir::Value nextIV =
                    builder.createAdd(ivLoc, ivValue, stepValueCIR);
                mlir::Value isLastIteration = builder.createCompare(
                    ivLoc, loopInfo->upperInclusive ? cir::CmpOpKind::gt
                                                    : cir::CmpOpKind::ge,
                    nextIV, upperBoundValueCIR);

                cir::IfOp::create(
                    builder, ivLoc, isLastIteration,
                    /*withElseRegion=*/false,
                    [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
                      mlir::OpBuilder::InsertionGuard guard(builder);
                      builder.setInsertionPointToEnd(
                          thenBuilder.getInsertionBlock());
                      for (auto [index, vd, srcVD, destVD, assignOp,
                                 isConditional] :
                           llvm::zip_equal(
                               wsloopClauseInfo.lastprivateIndices,
                               wsloopClauseInfo.lastprivateVars,
                               wsloopClauseInfo.lastprivateSourceVDs,
                               wsloopClauseInfo.lastprivateDestVDs,
                               wsloopClauseInfo.lastprivateAssignmentOps,
                               wsloopClauseInfo.lastprivateConditionals)) {
                        assert(index < wsloopPrivateArgs.size() &&
                               "unexpected OpenMP wsloop lastprivate block "
                               "arg index");
                        Address srcAddr = getOpenMPPrivateStorageAddress(*this,
                                                                        vd);
                        const VarDecl *canonicalVD = vd->getCanonicalDecl();
                        Address destAddr = Address::invalid();
                        if (auto parallelIt =
                                parallelOriginalDecls.find(canonicalVD);
                            parallelIt != parallelOriginalDecls.end()) {
                          destAddr = parallelIt->second;
                        } else if (auto wsloopIt =
                                       wsloopOriginalDecls.find(canonicalVD);
                                   wsloopIt != wsloopOriginalDecls.end()) {
                          destAddr = wsloopIt->second;
                        }
                        assert(destAddr.isValid() &&
                               "missing lastprivate destination address");
                        if (isConditional)
                          emitOpenMPLastprivateConditionalFinalUpdate(
                              vd, srcAddr, vd->getLocation());
                        emitOpenMPLastprivateCopy(*this, vd, srcAddr, destAddr,
                                                 srcVD, destVD, assignOp);
                      }
                      cir::YieldOp::create(thenBuilder, thenLoc);
                    });
              }
              if (hasConditionalLastprivate)
                popOpenMPLastprivateConditional();
              mlir::omp::YieldOp::create(builder, end);
              builder.setInsertionPointAfter(distributeOp);
              reductionDeclGuards.clear();
            }
            }

            mlir::omp::TerminatorOp::create(builder, end);
          }
          if (mlir::failed(res))
            return res;

          return mlir::success();
        },
        begin, end, /*wrapInScope=*/false);
  };

  llvm::SmallPtrSet<const VarDecl *, 4> materializedClauseVars;
  if (!useTargetOffload) {
    for (const OMPClause *clause : s.clauses()) {
      if (clause->getClauseKind() != llvm::omp::OMPC_device)
        continue;
      materializeOpenMPCapturedClauseExpr(
          *this, cast<OMPDeviceClause>(clause)->getDevice(),
          materializedClauseVars);
    }
    for (const Expr *expr : hostOnlyTargetSideEffectExprs)
      materializeOpenMPCapturedClauseExpr(*this, expr, materializedClauseVars);
    for (const OMPClause *clause : teamsClauses) {
      switch (clause->getClauseKind()) {
      case llvm::omp::OMPC_if:
        materializeOpenMPCapturedClauseExpr(
            *this, cast<OMPIfClause>(clause)->getCondition(),
            materializedClauseVars);
        break;
      case llvm::omp::OMPC_num_teams:
        materializeOpenMPCapturedClauseExpr(
            *this, cast<OMPNumTeamsClause>(clause)->getNumTeams().front(),
            materializedClauseVars);
        break;
      case llvm::omp::OMPC_thread_limit:
        materializeOpenMPCapturedClauseExpr(
            *this, cast<OMPThreadLimitClause>(clause)->getThreadLimit().front(),
            materializedClauseVars);
        break;
      default:
        break;
      }
    }
    if (distScheduleChunkExpr)
      materializeOpenMPCapturedClauseExpr(*this, distScheduleChunkExpr,
                                          materializedClauseVars);

    if (targetClauses.empty())
      return emitTeamsDistributeParallelForBody(/*insideTargetRegion=*/false);

    materializeOMPTargetClauseExprs(*this, parallelClauses,
                                    materializedClauseVars);
    materializeOMPTargetClauseExprs(*this, wsloopClauses,
                                    materializedClauseVars);
    llvm::SmallVector<mlir::Value, 3> hostEvalVars;
    if (!useGeneratedLoopInfo) {
      materializeOpenMPCapturedClauseExpr(*this, loopInfo->lowerBound,
                                          materializedClauseVars);
      materializeOpenMPCapturedClauseExpr(*this, loopInfo->upperBound,
                                          materializedClauseVars);
      auto loopHostEvalVars = emitDistributeLoopHostEvalVars();
      if (llvm::failed(loopHostEvalVars))
        return mlir::failure();
      hostEvalVars.append(loopHostEvalVars->begin(), loopHostEvalVars->end());
    }

    auto targetOp = mlir::omp::TargetOp::create(
        builder, begin,
        /*allocate_vars=*/mlir::ValueRange{},
        /*allocator_vars=*/mlir::ValueRange{},
        /*bare=*/false,
        /*depend_kinds=*/mlir::ArrayAttr{},
        /*depend_vars=*/mlir::ValueRange{},
        /*depend_sizes=*/mlir::ValueRange{},
        /*device=*/mlir::Value{},
        /*has_device_addr_vars=*/mlir::ValueRange{},
        /*host_eval_vars=*/hostEvalVars,
        /*if_expr=*/mlir::Value{},
        /*in_reduction_vars=*/mlir::ValueRange{},
        /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
        /*in_reduction_syms=*/mlir::ArrayAttr{},
        /*is_device_ptr_vars=*/mlir::ValueRange{},
        /*map_vars=*/mlir::ValueRange{},
        /*nowait=*/false,
        /*private_vars=*/mlir::ValueRange{},
        /*private_syms=*/mlir::ArrayAttr{},
        /*private_needs_barrier=*/false,
        /*thread_limit=*/mlir::Value{},
        /*private_maps=*/mlir::DenseI64ArrayAttr{});
    annotateOpenMPTargetParentName(*this, targetOp);
    OpenMPClauseEmissionInfo targetClauseInfo =
        emitOpenMPClauses(targetOp, targetClauses, targetFirstprivateVars);
    targetSyntheticFirstprivateVars = filterOpenMPSyntheticFirstprivateVars(
        targetSyntheticFirstprivateVars, targetClauseInfo);
    if (mlir::failed(appendOpenMPSyntheticFirstprivatePrivates(
            *this, targetOp, targetSyntheticFirstprivateVars,
            targetClauseInfo)))
      return mlir::failure();
    if (mlir::failed(appendOpenMPTargetCapturedVLASizePrivates(
            *this, targetOp,
            *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target),
            targetClauseInfo)))
      return mlir::failure();
    if (mlir::failed(maybeEmitOpenMPRuntimeDependPayload(
            *this, targetOp, s, targetClauseInfo)))
      return mlir::failure();
    if (mlir::failed(appendHostOnlyOMPTargetCapturedMappings(
            *this, targetOp, targetClauseInfo,
            *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target))))
      return mlir::failure();
    if (mlir::failed(finalizeOMPTargetPrivateMappings(*this, targetOp,
                                                      targetClauseInfo)))
      return mlir::failure();

    mlir::LogicalResult res = emitOMPTargetRegionBody(
        *this, targetOp, targetClauseInfo, begin, end,
        [&]() -> mlir::LogicalResult {
          auto targetArgIface =
              mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                  targetOp.getOperation());
          return emitTeamsDistributeParallelForBody(
              /*insideTargetRegion=*/true,
              targetArgIface.getHostEvalBlockArgs());
        },
        /*wrapInScope=*/false);
    if (mlir::succeeded(res))
      checkForOpenMPLastprivateConditionalUpdate(*this, s);
    return res;
  }

  materializeOMPTargetClauseExprs(*this, targetClauses, materializedClauseVars);
  if (numTeamsClause) {
    materializeOpenMPCapturedClauseExpr(
        *this, numTeamsClause->getNumTeams().front(), materializedClauseVars);
  }
  if (distScheduleChunkExpr)
    materializeOpenMPCapturedClauseExpr(*this, distScheduleChunkExpr,
                                        materializedClauseVars);
  materializeOpenMPTargetParallelHostEvalClauseExprs(
      *this, parallelHostEvalClauses, materializedClauseVars);
  if (!useGeneratedLoopInfo) {
    materializeOpenMPCapturedClauseExpr(*this, loopInfo->lowerBound,
                                        materializedClauseVars);
    materializeOpenMPCapturedClauseExpr(*this, loopInfo->upperBound,
                                        materializedClauseVars);
  }

  llvm::SmallVector<mlir::Value, 7> hostEvalVars;
  if (numTeamsClause) {
    hostEvalVars.push_back(
        emitOpenMPIntegerExpr(*this, numTeamsClause->getNumTeams().front()));
  }
  if (teamsThreadLimitClause) {
    hostEvalVars.push_back(emitOpenMPIntegerExpr(
        *this, teamsThreadLimitClause->getThreadLimit().front()));
  }
  if (!useGeneratedLoopInfo) {
    if (auto loopHostEvalVars = emitDistributeLoopHostEvalVars();
        llvm::succeeded(loopHostEvalVars)) {
      hostEvalVars.append(loopHostEvalVars->begin(), loopHostEvalVars->end());
    } else {
      return mlir::failure();
    }
  }
  auto parallelHostEvalVars =
      emitOpenMPTargetParallelHostEvalVars(*this, parallelHostEvalClauses);
  hostEvalVars.append(parallelHostEvalVars.begin(), parallelHostEvalVars.end());

  auto targetOp = mlir::omp::TargetOp::create(
      builder, begin,
      /*allocate_vars=*/mlir::ValueRange{},
      /*allocator_vars=*/mlir::ValueRange{},
      /*bare=*/false,
      /*depend_kinds=*/mlir::ArrayAttr{},
      /*depend_vars=*/mlir::ValueRange{},
      /*depend_sizes=*/mlir::ValueRange{},
      /*device=*/mlir::Value{},
      /*has_device_addr_vars=*/mlir::ValueRange{},
      /*host_eval_vars=*/hostEvalVars,
      /*if_expr=*/mlir::Value{},
      /*in_reduction_vars=*/mlir::ValueRange{},
      /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
      /*in_reduction_syms=*/mlir::ArrayAttr{},
      /*is_device_ptr_vars=*/mlir::ValueRange{},
      /*map_vars=*/mlir::ValueRange{},
      /*nowait=*/false,
      /*private_vars=*/mlir::ValueRange{},
      /*private_syms=*/mlir::ArrayAttr{},
      /*private_needs_barrier=*/false,
      /*thread_limit=*/mlir::Value{},
      /*private_maps=*/mlir::DenseI64ArrayAttr{});
  annotateOpenMPTargetParentName(*this, targetOp);
  OpenMPClauseEmissionInfo targetClauseInfo =
      emitOpenMPClauses(targetOp, targetClauses, targetFirstprivateVars);
  targetSyntheticFirstprivateVars = filterOpenMPSyntheticFirstprivateVars(
      targetSyntheticFirstprivateVars, targetClauseInfo);
  if (mlir::failed(appendOpenMPSyntheticFirstprivatePrivates(
          *this, targetOp, targetSyntheticFirstprivateVars, targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(appendOpenMPTargetCapturedVLASizePrivates(
          *this, targetOp,
          *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target),
          targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(maybeEmitOpenMPRuntimeDependPayload(
          *this, targetOp, s, targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(finalizeOMPTargetPrivateMappings(*this, targetOp,
                                                    targetClauseInfo)))
    return mlir::failure();
  if (useTargetOffload)
    promoteOMPTargetPrivateMapsForOffload(targetOp, targetClauseInfo);

  mlir::LogicalResult res = emitOMPTargetRegionBody(
      *this, targetOp, targetClauseInfo, begin, end,
      [&]() -> mlir::LogicalResult {
        auto targetArgIface =
            mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                targetOp.getOperation());
        return emitTeamsDistributeParallelForBody(
            /*insideTargetRegion=*/true,
            targetArgIface.getHostEvalBlockArgs());
      },
      /*wrapInScope=*/false);
  if (mlir::succeeded(res))
    checkForOpenMPLastprivateConditionalUpdate(*this, s);
  return res;
}
mlir::LogicalResult
CIRGenFunction::emitOMPTargetTeamsDistributeParallelForDirective(
    const OMPTargetTeamsDistributeParallelForDirective &s) {
  constexpr llvm::StringLiteral directiveName =
      "OpenMP OMPTargetTeamsDistributeParallelForDirective";
  return emitOMPTargetTeamsDistributeParallelForDirectiveImpl(
      s, directiveName,
      /*allowBind=*/false,
      /*hasCancel=*/s.hasCancel());
}
mlir::LogicalResult
CIRGenFunction::emitOMPTargetTeamsDistributeParallelForSimdDirective(
    const OMPTargetTeamsDistributeParallelForSimdDirective &s) {
  constexpr llvm::StringLiteral directiveName =
      "OpenMP OMPTargetTeamsDistributeParallelForSimdDirective";

  auto loopInfo = getOpenMPSimpleCanonicalForInfo(s);
  if (!loopInfo) {
    std::string feature = directiveName.str();
    feature += " for non-canonical/non-unit-stride loop";
    getCIRGenModule().errorNYI(s.getSourceRange(), feature);
    return mlir::failure();
  }

  const bool isTargetDevice =
      getLangOpts().OpenMPIsTargetDevice && !forceOpenMPTargetOpEmission;
  const bool hasOffloadTargets = !getLangOpts().OMPTargetTriples.empty();
  const bool useTargetOffload = hasOffloadTargets && !isTargetDevice;
  const bool hasTargetAllocatorClauses = llvm::any_of(
      s.clauses(), [](const OMPClause *clause) {
        return clause->getClauseKind() == llvm::omp::OMPC_uses_allocators ||
               clause->getClauseKind() == llvm::omp::OMPC_allocate;
      });

  llvm::SmallVector<const OMPClause *, 8> targetClauses;
  llvm::SmallVector<const OMPClause *, 4> targetReductionClauses;
  llvm::SmallVector<const OMPClause *, 8> teamsClauses;
  llvm::SmallVector<const OMPClause *, 8> distributeClauses;
  llvm::SmallVector<const OMPClause *, 8> parallelClauses;
  llvm::SmallVector<const OMPClause *, 8> wsloopClauses;
  llvm::SmallVector<const OMPClause *, 8> simdClauses;
  llvm::SmallVector<const VarDecl *, 4> teamsDevicePointerVars;
  llvm::SmallPtrSet<const VarDecl *, 4> seenTeamsDevicePointerVars;
  llvm::SmallVector<const VarDecl *, 8> targetFirstprivateVars;
  llvm::SmallVector<const VarDecl *, 8> targetSyntheticFirstprivateVars;
  llvm::SmallVector<const Expr *, 2> hostOnlyTargetSideEffectExprs;
  const Expr *distScheduleChunkExpr = nullptr;
  const OMPNumTeamsClause *numTeamsClause = nullptr;
  const OMPThreadLimitClause *teamsThreadLimitClause = nullptr;
  OpenMPTargetParallelHostEvalClauseInfo parallelHostEvalClauses;

  auto recordTeamsDevicePointerVar =
      [&](const Expr *varExpr, bool allowArraySections) {
        const Expr *baseExpr = varExpr->IgnoreParenImpCasts();
        if (allowArraySections) {
          while (const auto *section = dyn_cast<ArraySectionExpr>(baseExpr))
            baseExpr = section->getBase()->IgnoreParenImpCasts();
          while (const auto *subscript = dyn_cast<ArraySubscriptExpr>(baseExpr))
            baseExpr = subscript->getBase()->IgnoreParenImpCasts();
        }

        const auto *dre = dyn_cast<DeclRefExpr>(baseExpr);
        const auto *vd = dre ? dyn_cast<VarDecl>(dre->getDecl()) : nullptr;
        if (!vd)
          return;
        vd = vd->getCanonicalDecl();
        if (!vd->isLocalVarDeclOrParm() && !vd->hasGlobalStorage())
          return;
        if (seenTeamsDevicePointerVars.insert(vd).second)
          teamsDevicePointerVars.push_back(vd);
      };

  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_depend:
    case llvm::omp::OMPC_nowait:
    case llvm::omp::OMPC_uses_allocators:
    case llvm::omp::OMPC_allocate:
      targetClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_thread_limit:
      targetClauses.push_back(clause);
      if (useTargetOffload) {
        teamsThreadLimitClause = cast<OMPThreadLimitClause>(clause);
      } else {
        hostOnlyTargetSideEffectExprs.push_back(
            cast<OMPThreadLimitClause>(clause)->getThreadLimit().front());
      }
      break;
    case llvm::omp::OMPC_firstprivate:
      if (useTargetOffload || hasTargetAllocatorClauses) {
        targetClauses.push_back(clause);
        for (const Expr *varExpr : cast<OMPFirstprivateClause>(clause)->varlist())
          if (const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr))
            targetFirstprivateVars.push_back(vd);
      }
      teamsClauses.push_back(clause);
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_private:
      if (useTargetOffload || hasTargetAllocatorClauses)
        targetClauses.push_back(clause);
      teamsClauses.push_back(clause);
      wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_num_teams:
      if (useTargetOffload) {
        numTeamsClause = cast<OMPNumTeamsClause>(clause);
        break;
      }
      teamsClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_if: {
      const auto *ifClause = cast<OMPIfClause>(clause);
      if (ifClause->getNameModifier() == llvm::omp::OMPD_unknown ||
          ifClause->getNameModifier() == llvm::omp::OMPD_target) {
        if (useTargetOffload)
          targetClauses.push_back(clause);
        break;
      }
      if (ifClause->getNameModifier() == llvm::omp::OMPD_teams) {
        teamsClauses.push_back(clause);
        break;
      }
      if (ifClause->getNameModifier() == llvm::omp::OMPD_parallel) {
        if (useTargetOffload)
          parallelHostEvalClauses.IfClause = ifClause;
        else
          parallelClauses.push_back(clause);
        break;
      }
      if (ifClause->getNameModifier() == llvm::omp::OMPD_simd) {
        simdClauses.push_back(clause);
        break;
      }
      std::string feature = directiveName.str();
      feature += " if clause modifier";
      getCIRGenModule().errorNYI(clause->getBeginLoc(), feature);
      return mlir::failure();
    }
    case llvm::omp::OMPC_dist_schedule:
      if (const Expr *chunk =
              cast<OMPDistScheduleClause>(clause)->getChunkSize()) {
        distScheduleChunkExpr = chunk;
        if (const VarDecl *vd = getOpenMPVarDeclFromExpr(chunk))
          targetSyntheticFirstprivateVars.push_back(vd);
      }
      distributeClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_num_threads:
      if (useTargetOffload) {
        parallelHostEvalClauses.NumThreadsClause =
            cast<OMPNumThreadsClause>(clause);
        break;
      }
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_severity:
    case llvm::omp::OMPC_message:
    case llvm::omp::OMPC_proc_bind:
    case llvm::omp::OMPC_copyin:
    case llvm::omp::OMPC_default:
    case llvm::omp::OMPC_shared:
      parallelClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_lastprivate:
      if (useTargetOffload)
        targetClauses.push_back(clause);
      wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_reduction:
      teamsClauses.push_back(clause);
      wsloopClauses.push_back(clause);
      targetReductionClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_schedule:
    case llvm::omp::OMPC_ordered:
    case llvm::omp::OMPC_linear:
      wsloopClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_order:
    case llvm::omp::OMPC_aligned:
    case llvm::omp::OMPC_nontemporal:
    case llvm::omp::OMPC_safelen:
    case llvm::omp::OMPC_simdlen:
      simdClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_collapse:
      distributeClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_device:
    case llvm::omp::OMPC_map:
      if (useTargetOffload)
        targetClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_is_device_ptr:
    case llvm::omp::OMPC_has_device_addr:
      if (useTargetOffload) {
        targetClauses.push_back(clause);
        if (clause->getClauseKind() == llvm::omp::OMPC_is_device_ptr) {
          for (const Expr *varExpr :
               cast<OMPIsDevicePtrClause>(clause)->varlist())
            recordTeamsDevicePointerVar(varExpr, /*allowArraySections=*/false);
        } else {
          for (const Expr *varExpr :
               cast<OMPHasDeviceAddrClause>(clause)->varlist())
            recordTeamsDevicePointerVar(varExpr, /*allowArraySections=*/true);
        }
      }
      break;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), (directiveName + " clause").str(),
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      return mlir::failure();
    }
  }

  if (useTargetOffload || !targetClauses.empty())
    targetClauses.append(targetReductionClauses.begin(),
                         targetReductionClauses.end());

  if (useTargetOffload || hasTargetAllocatorClauses) {
    llvm::SmallVector<const OMPClause *, 16> targetNestedClauses;
    targetNestedClauses.append(wsloopClauses.begin(), wsloopClauses.end());
    targetNestedClauses.append(simdClauses.begin(), simdClauses.end());
    collectOpenMPSimdSyntheticFirstprivateVars(targetNestedClauses,
                                               targetSyntheticFirstprivateVars);
  }

  bool distScheduleStatic = false;
  for (const OMPClause *clause : distributeClauses) {
    if (clause->getClauseKind() != llvm::omp::OMPC_dist_schedule)
      continue;
    const auto *distScheduleClause = cast<OMPDistScheduleClause>(clause);
    if (distScheduleClause->getDistScheduleKind() !=
        OMPC_DIST_SCHEDULE_static) {
      std::string feature = directiveName.str();
      feature += " dist_schedule kind";
      getCIRGenModule().errorNYI(clause->getBeginLoc(), feature);
      return mlir::failure();
    }
    distScheduleStatic = true;
  }

  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  auto emitDistributeLoopHostEvalVars =
      [&]() -> llvm::FailureOr<llvm::SmallVector<mlir::Value, 3>> {
    mlir::Value lowerBound = emitOpenMPIntegerExpr(*this, loopInfo->lowerBound);
    mlir::Value upperBound = emitOpenMPIntegerExpr(*this, loopInfo->upperBound);
    if (lowerBound.getType() != upperBound.getType()) {
      std::string feature = directiveName.str();
      feature += " with mismatched loop bound types";
      getCIRGenModule().errorNYI(s.getSourceRange(), feature);
      return llvm::failure();
    }

    llvm::SmallVector<mlir::Value, 3> loopHostEvalVars;
    loopHostEvalVars.push_back(lowerBound);
    loopHostEvalVars.push_back(upperBound);
    loopHostEvalVars.push_back(
        createOpenMPIntegerConstant(*this, begin, lowerBound.getType(), 1));
    return loopHostEvalVars;
  };

  auto emitTeamsDistributeParallelForSimdBody =
      [&](bool insideTargetRegion,
          mlir::ValueRange hostEvalArgs = mlir::ValueRange{})
      -> mlir::LogicalResult {
    OpenMPNontemporalDeclsRAII nontemporalDecls(*this, s);
    llvm::SmallPtrSet<const VarDecl *, 4> rematerializedClauseVars;
    llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 8>
        rematerializedClauseGuards;
    if (insideTargetRegion) {
      rematerializeOpenMPClauseExprs(*this, parallelClauses,
                                     rematerializedClauseVars,
                                     rematerializedClauseGuards);
      rematerializeOpenMPClauseExprs(*this, wsloopClauses,
                                     rematerializedClauseVars,
                                     rematerializedClauseGuards);
      rematerializeOpenMPClauseExprs(*this, simdClauses,
                                     rematerializedClauseVars,
                                     rematerializedClauseGuards);
    }
    mlir::Value wsloopScheduleChunkValue;
    if (insideTargetRegion)
      if (const Expr *wsloopScheduleChunkExpr =
              getOpenMPScheduleChunkExpr(wsloopClauses))
        wsloopScheduleChunkValue =
            emitOpenMPIntegerExpr(*this,
                                  unwrapOpenMPCapturedExprInit(
                                      wsloopScheduleChunkExpr));

    mlir::Value distScheduleChunkValue;
    if (distScheduleChunkExpr)
      distScheduleChunkValue = emitOpenMPIntegerExpr(*this, distScheduleChunkExpr);

    // On the host path the composite loop bounds feed the omp.loop_nest nested
    // deep inside the teams/parallel/distribute/wsloop/simd region tree, so
    // their defining ops must dominate that use. Materialize them before the
    // omp.teams op: emitting them afterwards places the defs after omp.teams in
    // the enclosing block, which the verifier rejects with "operand does not
    // dominate this use". In the target-offload path the bounds instead arrive
    // as host_eval block args, which already dominate.
    mlir::Value lowerBoundValue;
    mlir::Value upperBoundValue;
    mlir::Value stepValue;
    if (!insideTargetRegion) {
      lowerBoundValue = emitOpenMPIntegerExpr(*this, loopInfo->lowerBound);
      upperBoundValue = emitOpenMPIntegerExpr(*this, loopInfo->upperBound);
      if (lowerBoundValue.getType() != upperBoundValue.getType()) {
        std::string feature = directiveName.str();
        feature += " with mismatched loop bound types";
        getCIRGenModule().errorNYI(s.getSourceRange(), feature);
        return mlir::failure();
      }
      stepValue = createOpenMPIntegerConstant(*this, begin,
                                              lowerBoundValue.getType(), 1);
    }

    auto teamsOp = mlir::omp::TeamsOp::create(
        builder, begin,
        /*allocate_vars=*/mlir::ValueRange{},
        /*allocator_vars=*/mlir::ValueRange{},
        /*if_expr=*/mlir::Value{},
        /*num_teams_lower=*/mlir::Value{},
        /*num_teams_upper=*/mlir::Value{},
        /*private_vars=*/mlir::ValueRange{},
        /*private_syms=*/mlir::ArrayAttr{},
        /*private_needs_barrier=*/false,
        /*reduction_mod=*/mlir::omp::ReductionModifierAttr{},
        /*reduction_vars=*/mlir::ValueRange{},
        /*reduction_byref=*/mlir::DenseBoolArrayAttr{},
        /*reduction_syms=*/mlir::ArrayAttr{},
        /*thread_limit=*/mlir::Value{});
    OpenMPClauseEmissionInfo teamsClauseInfo =
        emitOpenMPClauses(teamsOp, teamsClauses);

    unsigned nextHostEvalArg = 0;
    if (insideTargetRegion && useTargetOffload) {
      if (numTeamsClause) {
        assert(nextHostEvalArg < hostEvalArgs.size() &&
               "missing OpenMP target host_eval block arg for num_teams");
        teamsOp.getNumTeamsUpperMutable().assign(hostEvalArgs[nextHostEvalArg++]);
      }
      if (teamsThreadLimitClause) {
        assert(nextHostEvalArg < hostEvalArgs.size() &&
               "missing OpenMP target host_eval block arg for thread_limit");
        teamsOp.getThreadLimitMutable().assign(hostEvalArgs[nextHostEvalArg++]);
      }
      if (mlir::failed(appendOpenMPSyntheticFirstprivatePrivates(
              *this, teamsOp, teamsDevicePointerVars, teamsClauseInfo)))
        return mlir::failure();
    }
    if (insideTargetRegion) {
      assert(nextHostEvalArg + 2 < hostEvalArgs.size() &&
             "missing OpenMP target host_eval block args for loop bounds");
      lowerBoundValue = hostEvalArgs[nextHostEvalArg++];
      upperBoundValue = hostEvalArgs[nextHostEvalArg++];
      stepValue = hostEvalArgs[nextHostEvalArg++];
    }
    unsigned parallelHostEvalArgStart = nextHostEvalArg;

    return emitOMPTeamsRegionBody(
        *this, teamsOp, teamsClauseInfo,
        [&]() -> mlir::LogicalResult {
          llvm::SmallVector<OpenMPCopyinMasterAddr, 4> copyinMasterAddrs =
              collectOpenMPCopyinMasterAddresses(*this, s);

          llvm::SmallVector<mlir::Type> retTy;
          llvm::SmallVector<mlir::Value> operands;
          auto parallelOp =
              mlir::omp::ParallelOp::create(builder, begin, retTy, operands);
          parallelOp.setComposite(true);
          OpenMPClauseEmissionInfo parallelClauseInfo =
              emitOpenMPClauses(parallelOp, parallelClauses);
          if (insideTargetRegion && useTargetOffload) {
            unsigned parallelHostEvalArg = parallelHostEvalArgStart;
            if (parallelHostEvalClauses.IfClause) {
              assert(parallelHostEvalArg < hostEvalArgs.size() &&
                     "missing OpenMP target host_eval block arg for "
                     "parallel if");
              parallelOp.getIfExprMutable().assign(
                  hostEvalArgs[parallelHostEvalArg++]);
            }
            if (parallelHostEvalClauses.NumThreadsClause) {
              assert(parallelHostEvalArg < hostEvalArgs.size() &&
                     "missing OpenMP target host_eval block arg for "
                     "num_threads");
              parallelOp.getNumThreadsMutable().assign(
                  hostEvalArgs[parallelHostEvalArg++]);
              if (parallelHostEvalClauses.NumThreadsClause->getModifier() ==
                  OMPC_NUMTHREADS_strict) {
                // Mirror the
                // modifier into the CIR-namespaced discardable attribute
                // consumed by the OpenMP-to-LLVM translator (see
                // VisitOMPNumThreadsClause for full rationale).
                parallelOp.setNumThreadsModifier(
                    mlir::omp::NumThreadsModifier::strict);
                parallelOp->setAttr(
                    "cir.omp.num_threads_modifier",
                    mlir::omp::NumThreadsModifierAttr::get(
                        parallelOp.getContext(),
                        mlir::omp::NumThreadsModifier::strict));
              }
            }
          }

          const CapturedStmt *parallelCaptured =
              s.getCapturedStmt(OpenMPDirectiveKind::OMPD_parallel);
          assert(parallelCaptured && "expected OpenMP parallel captured stmt");
          parallelOp->setAttr(
              "cir.instrprof_func_name",
              cgm.getStringAttrCached(
                  getOpenMPHelperPGOName(*parallelCaptured)));
          parallelOp->setAttr(
              "cir.instrprof_func_linkage",
              builder.getI32IntegerAttr(static_cast<int32_t>(
                  llvm::GlobalValue::InternalLinkage)));

          CharUnits ivAlign = getContext().getDeclAlign(loopInfo->iv);
          mlir::Type ivType = convertTypeForMem(loopInfo->iv->getType());
          Address ivSeedRawAlloca = Address::invalid();
          Address ivSeedAddr = createTempAlloca(
              ivType, ivAlign, getLoc(loopInfo->iv->getLocation()),
              loopInfo->iv->getName(), /*arraySize=*/nullptr, &ivSeedRawAlloca);
          if (!localDeclMap.count(loopInfo->iv))
            declare(ivSeedRawAlloca.getPointer(), loopInfo->iv,
                    loopInfo->iv->getType(),
                    getLoc(loopInfo->iv->getSourceRange()), ivAlign);
          unsigned ivParallelPrivateIndex = parallelOp.getPrivateVars().size();
          auto ivPrivatizer = getOrCreateOpenMPPrivatePrivatizer(
              *this, builder, getLoc(loopInfo->iv->getLocation()), loopInfo->iv,
              ivType);
          parallelOp.getPrivateVarsMutable().append(ivSeedAddr.getPointer());
          llvm::SmallVector<mlir::Attribute, 4> parallelPrivateSyms;
          if (auto existingSyms = parallelOp.getPrivateSyms())
            llvm::append_range(parallelPrivateSyms, *existingSyms);
          parallelPrivateSyms.push_back(mlir::SymbolRefAttr::get(ivPrivatizer));
          parallelOp.setPrivateSymsAttr(
              mlir::ArrayAttr::get(builder.getContext(), parallelPrivateSyms));
          parallelClauseInfo.privateIndices.push_back(ivParallelPrivateIndex);
          parallelClauseInfo.privateVars.push_back(loopInfo->iv);
          if (insideTargetRegion &&
              mlir::failed(
                  appendOpenMPParallelNestedSyntheticFirstprivatePrivates(
                      *this, parallelOp, wsloopClauses,
                      parallelClauseInfo)))
            return mlir::failure();

          mlir::LogicalResult res = mlir::success();
          {
            mlir::OpBuilder::InsertionGuard guardCase(builder);
            llvm::SmallVector<mlir::Type, 4> privateArgTypes;
            llvm::SmallVector<mlir::Location, 4> privateArgLocs;
            for (mlir::Value privateVar : parallelOp.getPrivateVars()) {
              privateArgTypes.push_back(privateVar.getType());
              privateArgLocs.push_back(privateVar.getLoc());
            }

            mlir::Block *block = builder.createBlock(
                &parallelOp.getRegion(), {}, privateArgTypes, privateArgLocs);
            builder.setInsertionPointToEnd(block);

            auto pgoScope = enterOpenMPHelperRegionCounters(*parallelCaptured);
            LexicalScope ls{*this, begin, builder.getInsertionBlock()};
            llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                parallelPrivateDeclGuards;
            llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                wsloopPrivateDeclGuards;
            llvm::SmallVector<std::unique_ptr<DeclMapRevertingRAII>, 4>
                reductionDeclGuards;
            llvm::DenseMap<const VarDecl *, Address> parallelOriginalDecls;
            llvm::DenseMap<const VarDecl *, Address> wsloopOriginalDecls;
            if (mlir::failed(remapOpenMPPrivateDecls(
                    *this, parallelOp.getOperation(), parallelClauseInfo,
                    parallelPrivateDeclGuards, &parallelOriginalDecls)))
              return mlir::failure();

            OpenMPTLSCopyinDeclMapScope tlsCopyinDecls(*this, s);
            (void)emitOpenMPCopyinClause(*this, s, copyinMasterAddrs);
            bool hasDisabledLastprivateConditional =
                pushDisabledOpenMPLastprivateConditional(s);
            auto popDisabledLastprivateConditional = llvm::scope_exit([&] {
              if (hasDisabledLastprivateConditional)
                popOpenMPLastprivateConditional();
            });
            incrementProfileCounter(
                parallelCaptured->getCapturedDecl()->getBody());

            DeclMapRevertingRAII ivMapGuard(*this, loopInfo->iv);

            mlir::Type loopTy = lowerBoundValue.getType();
            if (loopTy != upperBoundValue.getType()) {
              std::string feature = directiveName.str();
              feature += " with mismatched loop bound types";
              getCIRGenModule().errorNYI(s.getSourceRange(), feature);
              return mlir::failure();
            }

            OpenMPSimdCompositeClauseData simdClauseData;
            if (mlir::failed(collectOpenMPSimdClauseDataForComposite(
                    *this, simdClauses, simdClauseData)))
              return mlir::failure();

            mlir::omp::DistributeOperands distributeClauseOps;
            auto distributeOp =
                mlir::omp::DistributeOp::create(builder, begin,
                                               distributeClauseOps);
            distributeOp.setComposite(true);
            if (distScheduleStatic)
              distributeOp.setDistScheduleStatic(true);
            if (distScheduleChunkValue)
              distributeOp.getDistScheduleChunkSizeMutable().assign(
                  distScheduleChunkValue);

            {
              mlir::OpBuilder::InsertionGuard guard(builder);
              mlir::Block *distributeBlock =
                  builder.createBlock(&distributeOp.getRegion());
              builder.setInsertionPoint(distributeOp);
              auto wsloopOp = mlir::omp::WsloopOp::create(builder, begin);
              wsloopOp.setNowait(true);
              wsloopOp.setComposite(true);
              if (wsloopScheduleChunkValue)
                wsloopOp.getScheduleChunkMutable().assign(
                    wsloopScheduleChunkValue);
              OpenMPClauseEmissionInfo wsloopClauseInfo = emitOpenMPClauses(
                  wsloopOp, wsloopClauses, parallelClauseInfo.privateVars,
                  /*allowWsloopConditionalLastprivate=*/true);
              wsloopOp->moveBefore(distributeBlock, distributeBlock->end());

              llvm::SmallVector<mlir::Type, 4> wsloopPrivateArgTypes;
              llvm::SmallVector<mlir::Location, 4> wsloopPrivateArgLocs;
              for (mlir::Value privateVar : wsloopOp.getPrivateVars()) {
                wsloopPrivateArgTypes.push_back(privateVar.getType());
                wsloopPrivateArgLocs.push_back(privateVar.getLoc());
              }
              llvm::SmallVector<mlir::Type, 4> wsloopReductionArgTypes;
              llvm::SmallVector<mlir::Location, 4> wsloopReductionArgLocs;
              for (mlir::Value reductionVar : wsloopOp.getReductionVars()) {
                wsloopReductionArgTypes.push_back(reductionVar.getType());
                wsloopReductionArgLocs.push_back(reductionVar.getLoc());
              }
              wsloopPrivateArgTypes.append(wsloopReductionArgTypes);
              wsloopPrivateArgLocs.append(wsloopReductionArgLocs);
              mlir::Block *wsloopBlock = builder.createBlock(
                  &wsloopOp.getRegion(), {}, wsloopPrivateArgTypes,
                  wsloopPrivateArgLocs);
              builder.setInsertionPointToEnd(wsloopBlock);

              mlir::ArrayAttr alignments =
                  simdClauseData.alignments.empty()
                      ? mlir::ArrayAttr{}
                      : mlir::ArrayAttr::get(builder.getContext(),
                                             simdClauseData.alignments);
              mlir::ArrayAttr linearVarTypes =
                  simdClauseData.linearVarTypes.empty()
                      ? mlir::ArrayAttr{}
                      : mlir::ArrayAttr::get(builder.getContext(),
                                             simdClauseData.linearVarTypes);
              auto simdOp = mlir::omp::SimdOp::create(
                  builder, begin, simdClauseData.alignedVars, alignments,
                  simdClauseData.ifExpr, simdClauseData.linearVars,
                  simdClauseData.linearStepVars, linearVarTypes,
                  simdClauseData.nontemporalVars, simdClauseData.order,
                  simdClauseData.orderMod,
                  /*private_vars=*/mlir::ValueRange{},
                  /*private_syms=*/mlir::ArrayAttr{},
                  /*private_needs_barrier=*/false,
                  /*reduction_mod=*/mlir::omp::ReductionModifierAttr{},
                  /*reduction_vars=*/mlir::ValueRange{},
                  /*reduction_byref=*/mlir::DenseBoolArrayAttr{},
                  /*reduction_syms=*/mlir::ArrayAttr{},
                  simdClauseData.safelen, simdClauseData.simdlen);
              simdOp.setComposite(true);
              mlir::Block *simdBlock = builder.createBlock(&simdOp.getRegion());
              builder.setInsertionPointToEnd(simdBlock);

              mlir::omp::LoopNestOperands loopClauseOps;
              loopClauseOps.loopLowerBounds.push_back(lowerBoundValue);
              loopClauseOps.loopUpperBounds.push_back(upperBoundValue);
              loopClauseOps.loopSteps.push_back(stepValue);
              if (loopInfo->upperInclusive)
                loopClauseOps.loopInclusive = builder.getUnitAttr();

              auto loopNestOp =
                  mlir::omp::LoopNestOp::create(builder, begin, loopClauseOps);
              loopNestOp->setAttr(
                  "omp.frontend.iv_signed",
                  builder.getDenseBoolArrayAttr(
                      {loopInfo->iv->getType()
                           ->hasSignedIntegerRepresentation()}));
              mlir::Block *loopBlock = builder.createBlock(
                  &loopNestOp.getRegion(), {}, {loopTy},
                  {getLoc(loopInfo->iv->getLocation())});
              builder.setInsertionPointToEnd(loopBlock);
              auto wsloopPrivateArgs =
                  mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                      wsloopOp.getOperation())
                      .getPrivateBlockArgs();
              if (mlir::failed(remapOpenMPPrivateDecls(
                      *this, wsloopOp.getOperation(), wsloopClauseInfo,
                      wsloopPrivateDeclGuards, &wsloopOriginalDecls)))
                return mlir::failure();
              remapOpenMPReductionDecls(*this, wsloopOp.getOperation(),
                                        wsloopClauseInfo,
                                        reductionDeclGuards);

              mlir::Value ivValue = castOpenMPValueToCIRType(
                  *this, getLoc(loopInfo->iv->getLocation()),
                  loopBlock->getArgument(0), ivType);
              auto ivIt = localDeclMap.find(loopInfo->iv);
              assert(ivIt != localDeclMap.end() &&
                     "expected loop IV address to be mapped");
              builder.createStore(getLoc(loopInfo->iv->getLocation()), ivValue,
                                  ivIt->second);

              bool hasConditionalLastprivate = pushOpenMPLastprivateConditional(
                  wsloopClauses, ivIt->second, s.getBeginLoc());
              OpenMPScanParentDirectiveRAII scanDirectiveScope(
                  *this, s, collectOpenMPReductionRegionArgs(wsloopOp));
              res = emitOMPLoopBody(*this, builder, begin,
                                    loopInfo->forStmt->getBody());
              if (!wsloopClauseInfo.lastprivateIndices.empty()) {
                assert(wsloopClauseInfo.lastprivateIndices.size() ==
                           wsloopClauseInfo.lastprivateVars.size() &&
                       "mismatched OpenMP wsloop lastprivate metadata");
                assert(wsloopClauseInfo.lastprivateIndices.size() ==
                           wsloopClauseInfo.lastprivateConditionals.size() &&
                       "mismatched OpenMP wsloop conditional-lastprivate "
                       "metadata");
                mlir::Location ivLoc = getLoc(loopInfo->iv->getLocation());
                auto cirIVTy = mlir::dyn_cast<cir::IntType>(ivType);
                assert(cirIVTy && "expected CIR integer type for loop IV");
                mlir::Value upperBoundValueCIR = castOpenMPValueToCIRType(
                    *this, ivLoc, upperBoundValue, ivType);
                mlir::Value stepValueCIR =
                    builder.getConstInt(ivLoc, cirIVTy, 1);
                mlir::Value nextIV =
                    builder.createAdd(ivLoc, ivValue, stepValueCIR);
                mlir::Value isLastIteration = builder.createCompare(
                    ivLoc, loopInfo->upperInclusive ? cir::CmpOpKind::gt
                                                    : cir::CmpOpKind::ge,
                    nextIV, upperBoundValueCIR);

                cir::IfOp::create(
                    builder, ivLoc, isLastIteration,
                    /*withElseRegion=*/false,
                    [&](mlir::OpBuilder &thenBuilder, mlir::Location thenLoc) {
                      mlir::OpBuilder::InsertionGuard guard(builder);
                      builder.setInsertionPointToEnd(
                          thenBuilder.getInsertionBlock());
                      for (auto [index, vd, srcVD, destVD, assignOp,
                                 isConditional] :
                           llvm::zip_equal(
                               wsloopClauseInfo.lastprivateIndices,
                               wsloopClauseInfo.lastprivateVars,
                               wsloopClauseInfo.lastprivateSourceVDs,
                               wsloopClauseInfo.lastprivateDestVDs,
                               wsloopClauseInfo.lastprivateAssignmentOps,
                               wsloopClauseInfo.lastprivateConditionals)) {
                        assert(index < wsloopPrivateArgs.size() &&
                               "unexpected OpenMP wsloop lastprivate block "
                               "arg index");
                        Address srcAddr = getOpenMPPrivateStorageAddress(*this,
                                                                        vd);
                        const VarDecl *canonicalVD = vd->getCanonicalDecl();
                        Address destAddr = Address::invalid();
                        if (auto parallelIt =
                                parallelOriginalDecls.find(canonicalVD);
                            parallelIt != parallelOriginalDecls.end()) {
                          destAddr = parallelIt->second;
                        } else if (auto wsloopIt =
                                       wsloopOriginalDecls.find(canonicalVD);
                                   wsloopIt != wsloopOriginalDecls.end()) {
                          destAddr = wsloopIt->second;
                        }
                        assert(destAddr.isValid() &&
                               "missing lastprivate destination address");
                        if (isConditional)
                          emitOpenMPLastprivateConditionalFinalUpdate(
                              vd, srcAddr, vd->getLocation());
                        emitOpenMPLastprivateCopy(*this, vd, srcAddr, destAddr,
                                                 srcVD, destVD, assignOp);
                      }
                      cir::YieldOp::create(thenBuilder, thenLoc);
                    });
              }
              if (hasConditionalLastprivate)
                popOpenMPLastprivateConditional();
              mlir::omp::YieldOp::create(builder, end);
              builder.setInsertionPointAfter(distributeOp);
              reductionDeclGuards.clear();
            }

            mlir::omp::TerminatorOp::create(builder, end);
          }
          if (mlir::failed(res))
            return res;

          return mlir::success();
        },
        begin, end, /*wrapInScope=*/false);
  };

  llvm::SmallPtrSet<const VarDecl *, 4> materializedClauseVars;
  if (!useTargetOffload) {
    for (const OMPClause *clause : s.clauses()) {
      if (clause->getClauseKind() != llvm::omp::OMPC_device)
        continue;
      materializeOpenMPCapturedClauseExpr(
          *this, cast<OMPDeviceClause>(clause)->getDevice(),
          materializedClauseVars);
    }
    for (const Expr *expr : hostOnlyTargetSideEffectExprs)
      materializeOpenMPCapturedClauseExpr(*this, expr, materializedClauseVars);
    for (const OMPClause *clause : teamsClauses) {
      switch (clause->getClauseKind()) {
      case llvm::omp::OMPC_if:
        materializeOpenMPCapturedClauseExpr(
            *this, cast<OMPIfClause>(clause)->getCondition(),
            materializedClauseVars);
        break;
      case llvm::omp::OMPC_num_teams:
        materializeOpenMPCapturedClauseExpr(
            *this, cast<OMPNumTeamsClause>(clause)->getNumTeams().front(),
            materializedClauseVars);
        break;
      case llvm::omp::OMPC_thread_limit:
        materializeOpenMPCapturedClauseExpr(
            *this, cast<OMPThreadLimitClause>(clause)->getThreadLimit().front(),
            materializedClauseVars);
        break;
      default:
        break;
      }
    }
    if (distScheduleChunkExpr)
      materializeOpenMPCapturedClauseExpr(*this, distScheduleChunkExpr,
                                          materializedClauseVars);

    if (targetClauses.empty()) {
      mlir::LogicalResult res =
          emitTeamsDistributeParallelForSimdBody(/*insideTargetRegion=*/false);
      if (mlir::succeeded(res))
        checkForOpenMPLastprivateConditionalUpdate(*this, s);
      return res;
    }

    materializeOMPTargetClauseExprs(*this, parallelClauses,
                                    materializedClauseVars);
    materializeOMPTargetClauseExprs(*this, wsloopClauses,
                                    materializedClauseVars);
    materializeOpenMPCapturedClauseExpr(*this, loopInfo->lowerBound,
                                        materializedClauseVars);
    materializeOpenMPCapturedClauseExpr(*this, loopInfo->upperBound,
                                        materializedClauseVars);
    auto loopHostEvalVars = emitDistributeLoopHostEvalVars();
    if (llvm::failed(loopHostEvalVars))
      return mlir::failure();

    auto targetOp = mlir::omp::TargetOp::create(
        builder, begin,
        /*allocate_vars=*/mlir::ValueRange{},
        /*allocator_vars=*/mlir::ValueRange{},
        /*bare=*/false,
        /*depend_kinds=*/mlir::ArrayAttr{},
        /*depend_vars=*/mlir::ValueRange{},
        /*depend_sizes=*/mlir::ValueRange{},
        /*device=*/mlir::Value{},
        /*has_device_addr_vars=*/mlir::ValueRange{},
        /*host_eval_vars=*/*loopHostEvalVars,
        /*if_expr=*/mlir::Value{},
        /*in_reduction_vars=*/mlir::ValueRange{},
        /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
        /*in_reduction_syms=*/mlir::ArrayAttr{},
        /*is_device_ptr_vars=*/mlir::ValueRange{},
        /*map_vars=*/mlir::ValueRange{},
        /*nowait=*/false,
        /*private_vars=*/mlir::ValueRange{},
        /*private_syms=*/mlir::ArrayAttr{},
        /*private_needs_barrier=*/false,
        /*thread_limit=*/mlir::Value{},
        /*private_maps=*/mlir::DenseI64ArrayAttr{});
    annotateOpenMPTargetParentName(*this, targetOp);
    OpenMPClauseEmissionInfo targetClauseInfo =
        emitOpenMPClauses(targetOp, targetClauses, targetFirstprivateVars);
    targetSyntheticFirstprivateVars = filterOpenMPSyntheticFirstprivateVars(
        targetSyntheticFirstprivateVars, targetClauseInfo);
    if (mlir::failed(appendOpenMPSyntheticFirstprivatePrivates(
            *this, targetOp, targetSyntheticFirstprivateVars,
            targetClauseInfo)))
      return mlir::failure();
    if (mlir::failed(appendOpenMPTargetCapturedVLASizePrivates(
            *this, targetOp,
            *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target),
            targetClauseInfo)))
      return mlir::failure();
    if (mlir::failed(maybeEmitOpenMPRuntimeDependPayload(
            *this, targetOp, s, targetClauseInfo)))
      return mlir::failure();
    if (mlir::failed(appendHostOnlyOMPTargetCapturedMappings(
            *this, targetOp, targetClauseInfo,
            *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target))))
      return mlir::failure();
    if (mlir::failed(finalizeOMPTargetPrivateMappings(*this, targetOp,
                                                      targetClauseInfo)))
      return mlir::failure();

    mlir::LogicalResult res = emitOMPTargetRegionBody(
        *this, targetOp, targetClauseInfo, begin, end,
        [&]() -> mlir::LogicalResult {
          auto targetArgIface =
              mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                  targetOp.getOperation());
          return emitTeamsDistributeParallelForSimdBody(
              /*insideTargetRegion=*/true,
              targetArgIface.getHostEvalBlockArgs());
        },
        /*wrapInScope=*/false);
    if (mlir::succeeded(res))
      checkForOpenMPLastprivateConditionalUpdate(*this, s);
    return res;
  }

  materializeOMPTargetClauseExprs(*this, targetClauses, materializedClauseVars);
  if (numTeamsClause) {
    materializeOpenMPCapturedClauseExpr(
        *this, numTeamsClause->getNumTeams().front(), materializedClauseVars);
  }
  if (distScheduleChunkExpr)
    materializeOpenMPCapturedClauseExpr(*this, distScheduleChunkExpr,
                                        materializedClauseVars);
  materializeOpenMPTargetParallelHostEvalClauseExprs(
      *this, parallelHostEvalClauses, materializedClauseVars);
  materializeOpenMPCapturedClauseExpr(*this, loopInfo->lowerBound,
                                      materializedClauseVars);
  materializeOpenMPCapturedClauseExpr(*this, loopInfo->upperBound,
                                      materializedClauseVars);

  llvm::SmallVector<mlir::Value, 7> hostEvalVars;
  if (numTeamsClause) {
    hostEvalVars.push_back(
        emitOpenMPIntegerExpr(*this, numTeamsClause->getNumTeams().front()));
  }
  if (teamsThreadLimitClause) {
    hostEvalVars.push_back(emitOpenMPIntegerExpr(
        *this, teamsThreadLimitClause->getThreadLimit().front()));
  }
  if (auto loopHostEvalVars = emitDistributeLoopHostEvalVars();
      llvm::succeeded(loopHostEvalVars)) {
    hostEvalVars.append(loopHostEvalVars->begin(), loopHostEvalVars->end());
  } else {
    return mlir::failure();
  }
  auto parallelHostEvalVars =
      emitOpenMPTargetParallelHostEvalVars(*this, parallelHostEvalClauses);
  hostEvalVars.append(parallelHostEvalVars.begin(), parallelHostEvalVars.end());

  auto targetOp = mlir::omp::TargetOp::create(
      builder, begin,
      /*allocate_vars=*/mlir::ValueRange{},
      /*allocator_vars=*/mlir::ValueRange{},
      /*bare=*/false,
      /*depend_kinds=*/mlir::ArrayAttr{},
      /*depend_vars=*/mlir::ValueRange{},
      /*depend_sizes=*/mlir::ValueRange{},
      /*device=*/mlir::Value{},
      /*has_device_addr_vars=*/mlir::ValueRange{},
      /*host_eval_vars=*/hostEvalVars,
      /*if_expr=*/mlir::Value{},
      /*in_reduction_vars=*/mlir::ValueRange{},
      /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
      /*in_reduction_syms=*/mlir::ArrayAttr{},
      /*is_device_ptr_vars=*/mlir::ValueRange{},
      /*map_vars=*/mlir::ValueRange{},
      /*nowait=*/false,
      /*private_vars=*/mlir::ValueRange{},
      /*private_syms=*/mlir::ArrayAttr{},
      /*private_needs_barrier=*/false,
      /*thread_limit=*/mlir::Value{},
      /*private_maps=*/mlir::DenseI64ArrayAttr{});
  annotateOpenMPTargetParentName(*this, targetOp);
  OpenMPClauseEmissionInfo targetClauseInfo =
      emitOpenMPClauses(targetOp, targetClauses, targetFirstprivateVars);
  targetSyntheticFirstprivateVars = filterOpenMPSyntheticFirstprivateVars(
      targetSyntheticFirstprivateVars, targetClauseInfo);
  if (mlir::failed(appendOpenMPSyntheticFirstprivatePrivates(
          *this, targetOp, targetSyntheticFirstprivateVars, targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(appendOpenMPTargetCapturedVLASizePrivates(
          *this, targetOp,
          *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target),
          targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(maybeEmitOpenMPRuntimeDependPayload(
          *this, targetOp, s, targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(finalizeOMPTargetPrivateMappings(*this, targetOp,
                                                    targetClauseInfo)))
    return mlir::failure();
  if (useTargetOffload)
    promoteOMPTargetPrivateMapsForOffload(targetOp, targetClauseInfo);

  mlir::LogicalResult res = emitOMPTargetRegionBody(
      *this, targetOp, targetClauseInfo, begin, end,
      [&]() -> mlir::LogicalResult {
        auto targetArgIface =
            mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                targetOp.getOperation());
        return emitTeamsDistributeParallelForSimdBody(
            /*insideTargetRegion=*/true,
            targetArgIface.getHostEvalBlockArgs());
      },
      /*wrapInScope=*/false);
  if (mlir::succeeded(res))
    checkForOpenMPLastprivateConditionalUpdate(*this, s);
  return res;
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetTeamsDistributeSimdDirective(
    const OMPTargetTeamsDistributeSimdDirective &s) {
  constexpr llvm::StringLiteral directiveName =
      "OpenMP OMPTargetTeamsDistributeSimdDirective";

  const bool isTargetDevice =
      getLangOpts().OpenMPIsTargetDevice && !forceOpenMPTargetOpEmission;
  const bool hasOffloadTargets = !getLangOpts().OMPTargetTriples.empty();
  const bool useTargetOffload = hasOffloadTargets && !isTargetDevice;
  const bool hasTargetAllocatorClauses = llvm::any_of(
      s.clauses(), [](const OMPClause *clause) {
        return clause->getClauseKind() == llvm::omp::OMPC_uses_allocators ||
               clause->getClauseKind() == llvm::omp::OMPC_allocate;
      });

  llvm::SmallVector<const OMPClause *, 8> targetClauses;
  llvm::SmallVector<const OMPClause *, 8> teamsClauses;
  llvm::SmallVector<const OMPClause *, 4> distributeClauses;
  llvm::SmallVector<const OMPClause *, 8> simdClauses;
  llvm::SmallVector<const VarDecl *, 4> teamsDevicePointerVars;
  llvm::SmallPtrSet<const VarDecl *, 4> seenTeamsDevicePointerVars;
  llvm::SmallVector<const VarDecl *, 8> targetFirstprivateVars;
  llvm::SmallVector<const VarDecl *, 8> targetSyntheticFirstprivateVars;
  const Expr *distScheduleChunkExpr = nullptr;
  const OMPNumTeamsClause *numTeamsClause = nullptr;
  const OMPThreadLimitClause *teamsThreadLimitClause = nullptr;

  auto recordTeamsDevicePointerVar =
      [&](const Expr *varExpr, bool allowArraySections) {
        const Expr *baseExpr = varExpr->IgnoreParenImpCasts();
        if (allowArraySections) {
          while (const auto *section = dyn_cast<ArraySectionExpr>(baseExpr))
            baseExpr = section->getBase()->IgnoreParenImpCasts();
          while (const auto *subscript =
                     dyn_cast<ArraySubscriptExpr>(baseExpr))
            baseExpr = subscript->getBase()->IgnoreParenImpCasts();
        }

        const auto *dre = dyn_cast<DeclRefExpr>(baseExpr);
        const auto *vd = dre ? dyn_cast<VarDecl>(dre->getDecl()) : nullptr;
        if (!vd)
          return;
        vd = vd->getCanonicalDecl();
        if (!vd->isLocalVarDeclOrParm() && !vd->hasGlobalStorage())
          return;
        if (seenTeamsDevicePointerVars.insert(vd).second)
          teamsDevicePointerVars.push_back(vd);
      };

  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_depend:
    case llvm::omp::OMPC_nowait:
    case llvm::omp::OMPC_uses_allocators:
    case llvm::omp::OMPC_allocate:
      targetClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_firstprivate:
      if (useTargetOffload || hasTargetAllocatorClauses) {
        targetClauses.push_back(clause);
        for (const Expr *varExpr : cast<OMPFirstprivateClause>(clause)->varlist())
          if (const VarDecl *vd = getOpenMPVarDeclFromExpr(varExpr))
            targetFirstprivateVars.push_back(vd);
      }
      teamsClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_private:
      if (useTargetOffload || hasTargetAllocatorClauses)
        targetClauses.push_back(clause);
      teamsClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_num_teams:
      if (useTargetOffload) {
        numTeamsClause = cast<OMPNumTeamsClause>(clause);
        break;
      }
      teamsClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_thread_limit:
      if (useTargetOffload) {
        targetClauses.push_back(clause);
        teamsThreadLimitClause = cast<OMPThreadLimitClause>(clause);
        break;
      }
      teamsClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_if: {
      const auto *ifClause = cast<OMPIfClause>(clause);
      if (ifClause->getNameModifier() == llvm::omp::OMPD_unknown ||
          ifClause->getNameModifier() == llvm::omp::OMPD_target) {
        if (useTargetOffload)
          targetClauses.push_back(clause);
        break;
      }
      if (ifClause->getNameModifier() == llvm::omp::OMPD_teams) {
        teamsClauses.push_back(clause);
        break;
      }
      if (ifClause->getNameModifier() == llvm::omp::OMPD_simd) {
        simdClauses.push_back(clause);
        break;
      }
      std::string feature = directiveName.str();
      feature += " if clause modifier";
      getCIRGenModule().errorNYI(clause->getBeginLoc(), feature);
      return mlir::failure();
    }
    case llvm::omp::OMPC_dist_schedule:
      if (const Expr *chunk =
              cast<OMPDistScheduleClause>(clause)->getChunkSize()) {
        distScheduleChunkExpr = chunk;
        if (const VarDecl *vd = getOpenMPVarDeclFromExpr(chunk))
          targetSyntheticFirstprivateVars.push_back(vd);
      }
      distributeClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_aligned:
    case llvm::omp::OMPC_nontemporal:
    case llvm::omp::OMPC_order:
    case llvm::omp::OMPC_safelen:
    case llvm::omp::OMPC_simdlen:
      simdClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_linear:
      // Match plain distribute simd: loop-IV linear clauses stay on the loop
      // IV path instead of lowering as generic simd linear private/final ops.
      break;
    case llvm::omp::OMPC_collapse:
      distributeClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_default:
      if (useTargetOffload)
        break;
      [[fallthrough]];
    case llvm::omp::OMPC_device:
    case llvm::omp::OMPC_map:
    case llvm::omp::OMPC_shared:
      if (useTargetOffload &&
          (clause->getClauseKind() == llvm::omp::OMPC_device ||
           clause->getClauseKind() == llvm::omp::OMPC_map))
        targetClauses.push_back(clause);
      break;
    case llvm::omp::OMPC_is_device_ptr:
    case llvm::omp::OMPC_has_device_addr:
      if (useTargetOffload) {
        targetClauses.push_back(clause);
        if (clause->getClauseKind() == llvm::omp::OMPC_is_device_ptr) {
          for (const Expr *varExpr :
               cast<OMPIsDevicePtrClause>(clause)->varlist())
            recordTeamsDevicePointerVar(varExpr, /*allowArraySections=*/false);
        } else {
          for (const Expr *varExpr :
               cast<OMPHasDeviceAddrClause>(clause)->varlist())
            recordTeamsDevicePointerVar(varExpr, /*allowArraySections=*/true);
        }
      }
      break;
    case llvm::omp::OMPC_lastprivate:
    case llvm::omp::OMPC_reduction:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), (directiveName + " clause").str(),
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      return mlir::failure();
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), (directiveName + " clause").str(),
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      return mlir::failure();
    }
  }

  if (useTargetOffload || hasTargetAllocatorClauses)
    collectOpenMPSimdSyntheticFirstprivateVars(simdClauses,
                                               targetSyntheticFirstprivateVars);

  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  auto loopInfo = getOpenMPSimpleCanonicalForInfo(s);
  auto emitDistributeLoopHostEvalVars =
      [&]() -> std::optional<llvm::SmallVector<mlir::Value, 3>> {
    if (!loopInfo)
      return std::nullopt;

    mlir::Value lowerBound =
        emitOpenMPIntegerExpr(*this, loopInfo->lowerBound);
    mlir::Value upperBound =
        emitOpenMPIntegerExpr(*this, loopInfo->upperBound);
    if (!getOpenMPWorksharingLoopType(*this, lowerBound, upperBound))
      return std::nullopt;

    llvm::SmallVector<mlir::Value, 3> loopHostEvalVars;
    loopHostEvalVars.push_back(lowerBound);
    loopHostEvalVars.push_back(upperBound);
    loopHostEvalVars.push_back(
        createOpenMPIntegerConstant(*this, begin, lowerBound.getType(), 1));
    return loopHostEvalVars;
  };
  auto emitTeamsDistributeSimdBody =
      [&](bool insideTargetRegion, bool useLoopHostEvalArgs,
          mlir::ValueRange hostEvalArgs = mlir::ValueRange{})
      -> mlir::LogicalResult {
    mlir::Value distScheduleChunkValue;
    if (insideTargetRegion && distScheduleChunkExpr)
      distScheduleChunkValue = emitOpenMPIntegerExpr(*this, distScheduleChunkExpr);

    auto teamsOp = mlir::omp::TeamsOp::create(
        builder, begin,
        /*allocate_vars=*/mlir::ValueRange{},
        /*allocator_vars=*/mlir::ValueRange{},
        /*if_expr=*/mlir::Value{},
        /*num_teams_lower=*/mlir::Value{},
        /*num_teams_upper=*/mlir::Value{},
        /*private_vars=*/mlir::ValueRange{},
        /*private_syms=*/mlir::ArrayAttr{},
        /*private_needs_barrier=*/false,
        /*reduction_mod=*/mlir::omp::ReductionModifierAttr{},
        /*reduction_vars=*/mlir::ValueRange{},
        /*reduction_byref=*/mlir::DenseBoolArrayAttr{},
        /*reduction_syms=*/mlir::ArrayAttr{},
        /*thread_limit=*/mlir::Value{});
    OpenMPClauseEmissionInfo teamsClauseInfo =
        emitOpenMPClauses(teamsOp, teamsClauses);

    mlir::Value lowerBoundValue;
    mlir::Value upperBoundValue;
    mlir::Value stepValue;
    unsigned nextHostEvalArg = 0;
    if (insideTargetRegion && useTargetOffload) {
      if (numTeamsClause) {
        assert(nextHostEvalArg < hostEvalArgs.size() &&
               "missing OpenMP target host_eval block arg for num_teams");
        teamsOp.getNumTeamsUpperMutable().assign(hostEvalArgs[nextHostEvalArg++]);
      }
      if (teamsThreadLimitClause) {
        assert(nextHostEvalArg < hostEvalArgs.size() &&
               "missing OpenMP target host_eval block arg for thread_limit");
        teamsOp.getThreadLimitMutable().assign(hostEvalArgs[nextHostEvalArg++]);
      }
      if (mlir::failed(appendOpenMPSyntheticFirstprivatePrivates(
              *this, teamsOp, teamsDevicePointerVars, teamsClauseInfo)))
        return mlir::failure();
    }
    if (useLoopHostEvalArgs) {
      assert(nextHostEvalArg + 2 < hostEvalArgs.size() &&
             "missing OpenMP target host_eval block args for loop bounds");
      lowerBoundValue = hostEvalArgs[nextHostEvalArg++];
      upperBoundValue = hostEvalArgs[nextHostEvalArg++];
      stepValue = hostEvalArgs[nextHostEvalArg++];
    }

    return emitOMPTeamsRegionBody(
        *this, teamsOp, teamsClauseInfo,
        [&]() -> mlir::LogicalResult {
          return emitOMPDistributeSimdLoop(
              *this, s, distributeClauses, simdClauses, directiveName, begin,
              end, /*rematerializeClauseExprs=*/insideTargetRegion,
              distScheduleChunkValue, lowerBoundValue,
              upperBoundValue, stepValue);
        },
        begin, end, /*wrapInScope=*/false);
  };

  llvm::SmallPtrSet<const VarDecl *, 4> materializedClauseVars;
  if (!useTargetOffload) {
    for (const OMPClause *clause : s.clauses()) {
      if (clause->getClauseKind() != llvm::omp::OMPC_device)
        continue;
      materializeOpenMPCapturedClauseExpr(
          *this, cast<OMPDeviceClause>(clause)->getDevice(),
          materializedClauseVars);
    }
    for (const OMPClause *clause : teamsClauses) {
      switch (clause->getClauseKind()) {
      case llvm::omp::OMPC_if:
        materializeOpenMPCapturedClauseExpr(
            *this, cast<OMPIfClause>(clause)->getCondition(),
            materializedClauseVars);
        break;
      case llvm::omp::OMPC_num_teams:
        materializeOpenMPCapturedClauseExpr(
            *this, cast<OMPNumTeamsClause>(clause)->getNumTeams().front(),
            materializedClauseVars);
        break;
      case llvm::omp::OMPC_thread_limit:
        materializeOpenMPCapturedClauseExpr(
            *this, cast<OMPThreadLimitClause>(clause)->getThreadLimit().front(),
            materializedClauseVars);
        break;
      default:
        break;
      }
    }
    if (distScheduleChunkExpr)
      materializeOpenMPCapturedClauseExpr(*this, distScheduleChunkExpr,
                                          materializedClauseVars);

    if (targetClauses.empty())
      return emitTeamsDistributeSimdBody(/*insideTargetRegion=*/false,
                                         /*useLoopHostEvalArgs=*/false);

    materializeOMPTargetClauseExprs(*this, simdClauses, materializedClauseVars);
    if (loopInfo) {
      materializeOpenMPCapturedClauseExpr(*this, loopInfo->lowerBound,
                                          materializedClauseVars);
      materializeOpenMPCapturedClauseExpr(*this, loopInfo->upperBound,
                                          materializedClauseVars);
    }

    auto targetOp = mlir::omp::TargetOp::create(
        builder, begin,
        /*allocate_vars=*/mlir::ValueRange{},
        /*allocator_vars=*/mlir::ValueRange{},
        /*bare=*/false,
        /*depend_kinds=*/mlir::ArrayAttr{},
        /*depend_vars=*/mlir::ValueRange{},
        /*depend_sizes=*/mlir::ValueRange{},
        /*device=*/mlir::Value{},
        /*has_device_addr_vars=*/mlir::ValueRange{},
        /*host_eval_vars=*/mlir::ValueRange{},
        /*if_expr=*/mlir::Value{},
        /*in_reduction_vars=*/mlir::ValueRange{},
        /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
        /*in_reduction_syms=*/mlir::ArrayAttr{},
        /*is_device_ptr_vars=*/mlir::ValueRange{},
        /*map_vars=*/mlir::ValueRange{},
        /*nowait=*/false,
        /*private_vars=*/mlir::ValueRange{},
        /*private_syms=*/mlir::ArrayAttr{},
        /*private_needs_barrier=*/false,
        /*thread_limit=*/mlir::Value{},
        /*private_maps=*/mlir::DenseI64ArrayAttr{});
    annotateOpenMPTargetParentName(*this, targetOp);
    OpenMPClauseEmissionInfo targetClauseInfo =
        emitOpenMPClauses(targetOp, targetClauses, targetFirstprivateVars);
    targetSyntheticFirstprivateVars = filterOpenMPSyntheticFirstprivateVars(
        targetSyntheticFirstprivateVars, targetClauseInfo);
    if (mlir::failed(appendOpenMPSyntheticFirstprivatePrivates(
            *this, targetOp, targetSyntheticFirstprivateVars,
            targetClauseInfo)))
      return mlir::failure();
    if (mlir::failed(appendOpenMPTargetCapturedVLASizePrivates(
            *this, targetOp,
            *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target),
            targetClauseInfo)))
      return mlir::failure();
    if (mlir::failed(maybeEmitOpenMPRuntimeDependPayload(
            *this, targetOp, s, targetClauseInfo)))
      return mlir::failure();
    if (mlir::failed(appendHostOnlyOMPTargetCapturedMappings(
            *this, targetOp, targetClauseInfo,
            *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target))))
      return mlir::failure();
    if (mlir::failed(finalizeOMPTargetPrivateMappings(*this, targetOp,
                                                      targetClauseInfo)))
      return mlir::failure();

    return emitOMPTargetRegionBody(
        *this, targetOp, targetClauseInfo, begin, end,
        [&]() -> mlir::LogicalResult {
          return emitTeamsDistributeSimdBody(/*insideTargetRegion=*/true,
                                             /*useLoopHostEvalArgs=*/false);
        },
        /*wrapInScope=*/false);
  }

  materializeOMPTargetClauseExprs(*this, targetClauses, materializedClauseVars);
  materializeOMPTargetClauseExprs(*this, simdClauses, materializedClauseVars);
  if (numTeamsClause) {
    materializeOpenMPCapturedClauseExpr(
        *this, numTeamsClause->getNumTeams().front(), materializedClauseVars);
  }
  if (distScheduleChunkExpr)
    materializeOpenMPCapturedClauseExpr(*this, distScheduleChunkExpr,
                                        materializedClauseVars);

  llvm::SmallVector<mlir::Value, 5> hostEvalVars;
  if (numTeamsClause) {
    hostEvalVars.push_back(
        emitOpenMPIntegerExpr(*this, numTeamsClause->getNumTeams().front()));
  }
  if (teamsThreadLimitClause) {
    hostEvalVars.push_back(emitOpenMPIntegerExpr(
        *this, teamsThreadLimitClause->getThreadLimit().front()));
  }
  bool hasLoopHostEvalArgs = false;
  if (auto loopHostEvalVars = emitDistributeLoopHostEvalVars()) {
    hostEvalVars.append(loopHostEvalVars->begin(), loopHostEvalVars->end());
    hasLoopHostEvalArgs = true;
  }

  auto targetOp = mlir::omp::TargetOp::create(
      builder, begin,
      /*allocate_vars=*/mlir::ValueRange{},
      /*allocator_vars=*/mlir::ValueRange{},
      /*bare=*/false,
      /*depend_kinds=*/mlir::ArrayAttr{},
      /*depend_vars=*/mlir::ValueRange{},
      /*depend_sizes=*/mlir::ValueRange{},
      /*device=*/mlir::Value{},
      /*has_device_addr_vars=*/mlir::ValueRange{},
      /*host_eval_vars=*/hostEvalVars,
      /*if_expr=*/mlir::Value{},
      /*in_reduction_vars=*/mlir::ValueRange{},
      /*in_reduction_byref=*/mlir::DenseBoolArrayAttr{},
      /*in_reduction_syms=*/mlir::ArrayAttr{},
      /*is_device_ptr_vars=*/mlir::ValueRange{},
      /*map_vars=*/mlir::ValueRange{},
      /*nowait=*/false,
      /*private_vars=*/mlir::ValueRange{},
      /*private_syms=*/mlir::ArrayAttr{},
      /*private_needs_barrier=*/false,
      /*thread_limit=*/mlir::Value{},
      /*private_maps=*/mlir::DenseI64ArrayAttr{});
  annotateOpenMPTargetParentName(*this, targetOp);
  OpenMPClauseEmissionInfo targetClauseInfo =
      emitOpenMPClauses(targetOp, targetClauses, targetFirstprivateVars);
  targetSyntheticFirstprivateVars = filterOpenMPSyntheticFirstprivateVars(
      targetSyntheticFirstprivateVars, targetClauseInfo);
  if (mlir::failed(appendOpenMPSyntheticFirstprivatePrivates(
          *this, targetOp, targetSyntheticFirstprivateVars, targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(appendOpenMPTargetCapturedVLASizePrivates(
          *this, targetOp,
          *s.getCapturedStmt(OpenMPDirectiveKind::OMPD_target),
          targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(maybeEmitOpenMPRuntimeDependPayload(
          *this, targetOp, s, targetClauseInfo)))
    return mlir::failure();
  if (mlir::failed(finalizeOMPTargetPrivateMappings(*this, targetOp,
                                                    targetClauseInfo)))
    return mlir::failure();
  if (useTargetOffload)
    promoteOMPTargetPrivateMapsForOffload(targetOp, targetClauseInfo);

  return emitOMPTargetRegionBody(
      *this, targetOp, targetClauseInfo, begin, end,
      [&]() -> mlir::LogicalResult {
        auto targetArgIface =
            mlir::cast<mlir::omp::BlockArgOpenMPOpInterface>(
                targetOp.getOperation());
        return emitTeamsDistributeSimdBody(
            /*insideTargetRegion=*/true, hasLoopHostEvalArgs,
            targetArgIface.getHostEvalBlockArgs());
      },
      /*wrapInScope=*/false);
}
mlir::LogicalResult
CIRGenFunction::emitOMPInteropDirective(const OMPInteropDirective &s) {
  mlir::Location loc = getLoc(s.getBeginLoc());
  mlir::Value device;
  if (const auto *deviceClause = s.getSingleClause<OMPDeviceClause>())
    device = emitScalarExpr(deviceClause->getDevice());

  auto dependInfo = emitOpenMPInteropDependInfo(*this, s, loc);
  if (!dependInfo)
    return mlir::failure();

  bool hasNowaitClause = s.hasClausesOfKind<OMPNowaitClause>();
  assert(!(hasNowaitClause && !(s.getSingleClause<OMPInitClause>() ||
                                s.getSingleClause<OMPDestroyClause>() ||
                                s.getSingleClause<OMPUseClause>())) &&
         "OMPNowaitClause clause is used separately in OMPInteropDirective.");

  for (const auto *clause : s.getClausesOfKind<OMPInitClause>()) {
    emitOpenMPInteropRuntimeCall(
        *this, s.getBeginLoc(), "__tgt_interop_init",
        emitLValue(clause->getInteropVar()).getPointer(),
        clause->getIsTarget() ? llvm::omp::OMPInteropType::Target
                              : llvm::omp::OMPInteropType::TargetSync,
        device, *dependInfo, hasNowaitClause);
  }

  for (const auto *clause : s.getClausesOfKind<OMPDestroyClause>()) {
    emitOpenMPInteropRuntimeCall(
        *this, s.getBeginLoc(), "__tgt_interop_destroy",
        emitLValue(clause->getInteropVar()).getPointer(), std::nullopt, device,
        *dependInfo, hasNowaitClause);
  }

  for (const auto *clause : s.getClausesOfKind<OMPUseClause>()) {
    emitOpenMPInteropRuntimeCall(
        *this, s.getBeginLoc(), "__tgt_interop_use",
        emitLValue(clause->getInteropVar()).getPointer(), std::nullopt, device,
        *dependInfo, hasNowaitClause);
  }

  return mlir::success();
}
mlir::LogicalResult
CIRGenFunction::emitOMPDispatchDirective(const OMPDispatchDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPDispatchDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPGenericLoopDirective(const OMPGenericLoopDirective &s) {
  OpenMPBindClauseKind bindKind = OMPC_BIND_unknown;
  if (const auto *clause = s.getSingleClause<OMPBindClause>())
    bindKind = clause->getBindKind();

  llvm::SmallVector<const OMPClause *, 8> loweredClauses;
  loweredClauses.reserve(s.clauses().size());
  for (const OMPClause *clause : s.clauses())
    if (clause->getClauseKind() != llvm::omp::OMPC_bind)
      loweredClauses.push_back(clause);

  switch (bindKind) {
  case OMPC_BIND_parallel:
    return emitSimpleOMPForDirective(s, loweredClauses,
                                     "OpenMP OMPGenericLoopDirective",
                                     /*hasCancel=*/false);
  case OMPC_BIND_thread:
    return emitSimpleOpenMPSimdDirective(
        s, loweredClauses, "OpenMP OMPGenericLoopDirective",
        "OpenMP OMPGenericLoopDirective clause");
  case OMPC_BIND_teams:
    return emitSimpleOMPDistributeDirective(
        *this, s, loweredClauses,
        "OpenMP OMPGenericLoopDirective bind(teams)");
  case OMPC_BIND_unknown:
    getCIRGenModule().errorNYI(s.getSourceRange(),
                               "OpenMP OMPGenericLoopDirective bind kind");
    return mlir::failure();
  }

  llvm_unreachable("unexpected OpenMP bind kind");
}
mlir::LogicalResult
CIRGenFunction::emitOMPReverseDirective(const OMPReverseDirective &s) {
  if (mlir::failed(emitOpenMPTransformPreInits(*this, s.getPreInits())))
    return mlir::failure();

  Stmt *transformedStmt = s.getTransformedStmt();
  if (!transformedStmt) {
    getCIRGenModule().errorNYI(
        s.getSourceRange(), "OpenMP OMPReverseDirective dependent transform");
    return mlir::failure();
  }

  return emitStmt(transformedStmt, /*useCurrentScope=*/true);
}
mlir::LogicalResult
CIRGenFunction::emitOMPInterchangeDirective(const OMPInterchangeDirective &s) {
  if (mlir::failed(emitOpenMPTransformPreInits(*this, s.getPreInits())))
    return mlir::failure();

  Stmt *transformedStmt = s.getTransformedStmt();
  if (!transformedStmt) {
    getCIRGenModule().errorNYI(
        s.getSourceRange(),
        "OpenMP OMPInterchangeDirective dependent transform");
    return mlir::failure();
  }

  return emitStmt(transformedStmt, /*useCurrentScope=*/true);
}
mlir::LogicalResult
CIRGenFunction::emitOMPAssumeDirective(const OMPAssumeDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPAssumeDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPMaskedDirective(const OMPMaskedDirective &s) {
  const Expr *filterExpr = nullptr;
  for (const OMPClause *clause : s.clauses()) {
    switch (clause->getClauseKind()) {
    case llvm::omp::OMPC_filter:
      filterExpr = cast<OMPFilterClause>(clause)->getThreadID();
      break;
    default:
      getCIRGenModule().errorNYI(
          clause->getBeginLoc(), "OpenMP masked clause",
          llvm::omp::getOpenMPClauseName(clause->getClauseKind()));
      break;
    }
  }

  llvm::SmallPtrSet<const VarDecl *, 1> materializedClauseVars;
  if (filterExpr)
    materializeOpenMPCapturedClauseExpr(*this, filterExpr,
                                        materializedClauseVars);

  mlir::LogicalResult res = mlir::success();
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());
  auto maskedOp = mlir::omp::MaskedOp::create(
      builder, begin,
      filterExpr ? emitOpenMPIntegerExpr(*this, filterExpr) : mlir::Value{});

  {
    mlir::OpBuilder::InsertionGuard guard(builder);
    mlir::Block *block = builder.createBlock(&maskedOp.getRegion());
    builder.setInsertionPointToEnd(block);
    res = emitStmt(s.getAssociatedStmt(), /*useCurrentScope=*/true);
    mlir::omp::TerminatorOp::create(builder, end);
  }

  return res;
}
mlir::LogicalResult
CIRGenFunction::emitOMPStripeDirective(const OMPStripeDirective &s) {
  if (mlir::failed(emitOpenMPTransformPreInits(*this, s.getPreInits())))
    return mlir::failure();

  Stmt *transformedStmt = s.getTransformedStmt();
  if (!transformedStmt) {
    getCIRGenModule().errorNYI(
        s.getSourceRange(), "OpenMP OMPStripeDirective dependent transform");
    return mlir::failure();
  }

  return emitStmt(transformedStmt, /*useCurrentScope=*/true);
}
