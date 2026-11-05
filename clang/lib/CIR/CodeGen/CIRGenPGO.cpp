//===--- CIRGenPGO.cpp - PGO Instrumentation for CIR CodeGen --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Instrumentation-based profile-guided optimization for CIR.
//
//===----------------------------------------------------------------------===//

#include "CIRGenPGO.h"
#include "CIRGenFunction.h"
#include "CIRGenModule.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtObjC.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MD5.h"

// Defined in clang's CoverageMappingGen (CodeGen library): when set via
// -system-headers-coverage, profiling instruments functions in system headers.
// Re-declared here to mirror classic CodeGen's system-header instrumentation
// gate without pulling in a private CodeGen header.
namespace llvm::coverage {
extern llvm::cl::opt<bool> SystemHeadersCoverage;
} // namespace llvm::coverage

using namespace clang;
using namespace CIRGen;

/// The version of the PGO hash algorithm.
enum PGOHashVersion : unsigned {
  PGO_HASH_V1,
  PGO_HASH_V2,
  PGO_HASH_V3,
  PGO_HASH_V4,

  PGO_HASH_LATEST = PGO_HASH_V4
};

namespace {

/// Stable hasher for PGO region counters.
///
/// Copied from OGCG CodeGenPGO.cpp. Changing the output of this hash will
/// invalidate all previously generated profiles.
class PGOHash {
  uint64_t Working;
  unsigned Count;
  PGOHashVersion HashVersion;
  llvm::MD5 MD5;

  static const int NumBitsPerType = 6;
  static const unsigned NumTypesPerWord =
      sizeof(uint64_t) * 8 / NumBitsPerType;
  static const unsigned TooBig = 1u << NumBitsPerType;

public:
  enum HashType : unsigned char {
    None = 0,
    LabelStmt = 1,
    WhileStmt,
    DoStmt,
    ForStmt,
    CXXForRangeStmt,
    ObjCForCollectionStmt,
    SwitchStmt,
    CaseStmt,
    DefaultStmt,
    IfStmt,
    CXXTryStmt,
    CXXCatchStmt,
    ConditionalOperator,
    BinaryOperatorLAnd,
    BinaryOperatorLOr,
    BinaryConditionalOperator,
    // The preceding values are available with PGO_HASH_V1.

    EndOfScope,
    IfThenBranch,
    IfElseBranch,
    GotoStmt,
    IndirectGotoStmt,
    BreakStmt,
    ContinueStmt,
    ReturnStmt,
    ThrowExpr,
    UnaryOperatorLNot,
    BinaryOperatorLT,
    BinaryOperatorGT,
    BinaryOperatorLE,
    BinaryOperatorGE,
    BinaryOperatorEQ,
    BinaryOperatorNE,
    // The preceding values are available since PGO_HASH_V2.

    LastHashType
  };
  static_assert(LastHashType <= TooBig, "Too many types in HashType");

  PGOHash(PGOHashVersion HashVersion)
      : Working(0), Count(0), HashVersion(HashVersion) {}
  void combine(HashType Type);
  uint64_t finalize();
  PGOHashVersion getHashVersion() const { return HashVersion; }
};

const int PGOHash::NumBitsPerType;
const unsigned PGOHash::NumTypesPerWord;
const unsigned PGOHash::TooBig;

void PGOHash::combine(HashType Type) {
  assert(Type && "Hash is invalid: unexpected type 0");
  assert(unsigned(Type) < TooBig && "Hash is invalid: too many types");

  if (Count && Count % NumTypesPerWord == 0) {
    using namespace llvm::support;
    uint64_t Swapped =
        endian::byte_swap<uint64_t>(Working, llvm::endianness::little);
    MD5.update(llvm::ArrayRef((uint8_t *)&Swapped, sizeof(Swapped)));
    Working = 0;
  }

  ++Count;
  Working = Working << NumBitsPerType | Type;
}

uint64_t PGOHash::finalize() {
  if (Count <= NumTypesPerWord)
    return Working;

  if (Working) {
    if (HashVersion < PGO_HASH_V3) {
      MD5.update({(uint8_t)Working});
    } else {
      using namespace llvm::support;
      uint64_t Swapped =
          endian::byte_swap<uint64_t>(Working, llvm::endianness::little);
      MD5.update(llvm::ArrayRef((uint8_t *)&Swapped, sizeof(Swapped)));
    }
  }

  llvm::MD5::MD5Result Result;
  MD5.final(Result);
  return Result.low();
}

/// A RecursiveASTVisitor that fills a map of statements to PGO counters.
///
/// Ported from OGCG CodeGenPGO.cpp. MC/DC-specific paths are stripped
/// (MCDCMaxCond=0 always).
struct MapRegionCounters : public RecursiveASTVisitor<MapRegionCounters> {
  using Base = RecursiveASTVisitor<MapRegionCounters>;

  unsigned NextCounter;
  PGOHash Hash;
  llvm::DenseMap<const Stmt *, unsigned> &CounterMap;
  uint64_t ProfileVersion;
  DiagnosticsEngine &Diag;

  MapRegionCounters(PGOHashVersion HashVersion, uint64_t ProfileVersion,
                    llvm::DenseMap<const Stmt *, unsigned> &CounterMap,
                    DiagnosticsEngine &Diag)
      : NextCounter(0), Hash(HashVersion), CounterMap(CounterMap),
        ProfileVersion(ProfileVersion), Diag(Diag) {}

  // Blocks and lambdas are handled as separate functions.
  bool TraverseBlockExpr(BlockExpr *BE) { return true; }
  bool TraverseLambdaExpr(LambdaExpr *LE) {
    for (auto C : zip(LE->captures(), LE->capture_inits()))
      TraverseLambdaCapture(LE, &std::get<0>(C), std::get<1>(C));
    return true;
  }
  bool TraverseCapturedStmt(CapturedStmt *CS) { return true; }

  bool VisitDecl(const Decl *D) {
    switch (D->getKind()) {
    default:
      break;
    case Decl::Function:
    case Decl::CXXMethod:
    case Decl::CXXConstructor:
    case Decl::CXXDestructor:
    case Decl::CXXConversion:
    case Decl::ObjCMethod:
    case Decl::Block:
    case Decl::Captured:
      CounterMap[D->getBody()] = NextCounter++;
      break;
    }
    return true;
  }

  /// If \p S gets a fresh counter, update the counter mappings. Return the
  /// V1 hash of \p S.
  PGOHash::HashType updateCounterMappings(Stmt *S) {
    auto Type = getHashType(PGO_HASH_V1, S);
    if (Type != PGOHash::None)
      CounterMap[S] = NextCounter++;
    return Type;
  }

  bool VisitStmt(Stmt *S) {
    auto Type = updateCounterMappings(S);
    if (Hash.getHashVersion() != PGO_HASH_V1)
      Type = getHashType(Hash.getHashVersion(), S);
    if (Type != PGOHash::None)
      Hash.combine(Type);
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator *S) {
    if (S->isLogicalOp() &&
        CIRGenFunction::isInstrumentedCondition(S->getRHS()) &&
        ProfileVersion >= llvm::IndexedInstrProf::Version7)
      CounterMap[S->getRHS()] = NextCounter++;
    return Base::VisitBinaryOperator(S);
  }

  bool TraverseIfStmt(IfStmt *If) {
    if (Hash.getHashVersion() == PGO_HASH_V1)
      return Base::TraverseIfStmt(If);

    VisitStmt(If);

    for (Stmt *CS : If->children()) {
      if (!CS)
        continue;
      if (CS == If->getThen())
        Hash.combine(PGOHash::IfThenBranch);
      else if (CS == If->getElse())
        Hash.combine(PGOHash::IfElseBranch);
      TraverseStmt(CS);
    }
    Hash.combine(PGOHash::EndOfScope);
    return true;
  }

  bool TraverseWhileStmt(WhileStmt *While) {
    Base::TraverseWhileStmt(While);
    if (Hash.getHashVersion() != PGO_HASH_V1)
      Hash.combine(PGOHash::EndOfScope);
    return true;
  }

  bool TraverseDoStmt(DoStmt *Do) {
    Base::TraverseDoStmt(Do);
    if (Hash.getHashVersion() != PGO_HASH_V1)
      Hash.combine(PGOHash::EndOfScope);
    return true;
  }

  bool TraverseForStmt(ForStmt *For) {
    Base::TraverseForStmt(For);
    if (Hash.getHashVersion() != PGO_HASH_V1)
      Hash.combine(PGOHash::EndOfScope);
    return true;
  }

  bool TraverseCXXForRangeStmt(CXXForRangeStmt *ForRange) {
    Base::TraverseCXXForRangeStmt(ForRange);
    if (Hash.getHashVersion() != PGO_HASH_V1)
      Hash.combine(PGOHash::EndOfScope);
    return true;
  }

#define DEFINE_NESTABLE_TRAVERSAL(N)                                           \
  bool Traverse##N(N *S) {                                                     \
    Base::Traverse##N(S);                                                      \
    if (Hash.getHashVersion() != PGO_HASH_V1)                                 \
      Hash.combine(PGOHash::EndOfScope);                                       \
    return true;                                                               \
  }

  DEFINE_NESTABLE_TRAVERSAL(ObjCForCollectionStmt)
  DEFINE_NESTABLE_TRAVERSAL(CXXTryStmt)
  DEFINE_NESTABLE_TRAVERSAL(CXXCatchStmt)

  PGOHash::HashType getHashType(PGOHashVersion HashVersion, const Stmt *S) {
    switch (S->getStmtClass()) {
    default:
      break;
    case Stmt::LabelStmtClass:
      return PGOHash::LabelStmt;
    case Stmt::WhileStmtClass:
      return PGOHash::WhileStmt;
    case Stmt::DoStmtClass:
      return PGOHash::DoStmt;
    case Stmt::ForStmtClass:
      return PGOHash::ForStmt;
    case Stmt::CXXForRangeStmtClass:
      return PGOHash::CXXForRangeStmt;
    case Stmt::ObjCForCollectionStmtClass:
      return PGOHash::ObjCForCollectionStmt;
    case Stmt::SwitchStmtClass:
      return PGOHash::SwitchStmt;
    case Stmt::CaseStmtClass:
      return PGOHash::CaseStmt;
    case Stmt::DefaultStmtClass:
      return PGOHash::DefaultStmt;
    case Stmt::IfStmtClass:
      return PGOHash::IfStmt;
    case Stmt::CXXTryStmtClass:
      return PGOHash::CXXTryStmt;
    case Stmt::CXXCatchStmtClass:
      return PGOHash::CXXCatchStmt;
    case Stmt::ConditionalOperatorClass:
      return PGOHash::ConditionalOperator;
    case Stmt::BinaryConditionalOperatorClass:
      return PGOHash::BinaryConditionalOperator;
    case Stmt::BinaryOperatorClass: {
      const BinaryOperator *BO = cast<BinaryOperator>(S);
      if (BO->getOpcode() == BO_LAnd)
        return PGOHash::BinaryOperatorLAnd;
      if (BO->getOpcode() == BO_LOr)
        return PGOHash::BinaryOperatorLOr;
      if (HashVersion >= PGO_HASH_V2) {
        switch (BO->getOpcode()) {
        default:
          break;
        case BO_LT:
          return PGOHash::BinaryOperatorLT;
        case BO_GT:
          return PGOHash::BinaryOperatorGT;
        case BO_LE:
          return PGOHash::BinaryOperatorLE;
        case BO_GE:
          return PGOHash::BinaryOperatorGE;
        case BO_EQ:
          return PGOHash::BinaryOperatorEQ;
        case BO_NE:
          return PGOHash::BinaryOperatorNE;
        }
      }
      break;
    }
    }

    if (HashVersion >= PGO_HASH_V2) {
      switch (S->getStmtClass()) {
      default:
        break;
      case Stmt::GotoStmtClass:
        return PGOHash::GotoStmt;
      case Stmt::IndirectGotoStmtClass:
        return PGOHash::IndirectGotoStmt;
      case Stmt::BreakStmtClass:
        return PGOHash::BreakStmt;
      case Stmt::ContinueStmtClass:
        return PGOHash::ContinueStmt;
      case Stmt::ReturnStmtClass:
        return PGOHash::ReturnStmt;
      case Stmt::CXXThrowExprClass:
        return PGOHash::ThrowExpr;
      case Stmt::UnaryOperatorClass: {
        const UnaryOperator *UO = cast<UnaryOperator>(S);
        if (UO->getOpcode() == UO_LNot)
          return PGOHash::UnaryOperatorLNot;
        break;
      }
      }
    }

    return PGOHash::None;
  }
};

} // anonymous namespace

/// Convert CIR GlobalLinkageKind to LLVM GlobalValue::LinkageTypes.
static llvm::GlobalValue::LinkageTypes
convertCIRToLLVMLinkage(cir::GlobalLinkageKind linkage) {
  using CIR = cir::GlobalLinkageKind;
  using LLVM = llvm::GlobalValue::LinkageTypes;

  switch (linkage) {
  case CIR::AvailableExternallyLinkage:
    return LLVM::AvailableExternallyLinkage;
  case CIR::CommonLinkage:
    return LLVM::CommonLinkage;
  case CIR::ExternalLinkage:
    return LLVM::ExternalLinkage;
  case CIR::ExternalWeakLinkage:
    return LLVM::ExternalWeakLinkage;
  case CIR::InternalLinkage:
    return LLVM::InternalLinkage;
  case CIR::LinkOnceAnyLinkage:
    return LLVM::LinkOnceAnyLinkage;
  case CIR::LinkOnceODRLinkage:
    return LLVM::LinkOnceODRLinkage;
  case CIR::PrivateLinkage:
    return LLVM::PrivateLinkage;
  case CIR::WeakAnyLinkage:
    return LLVM::WeakAnyLinkage;
  case CIR::WeakODRLinkage:
    return LLVM::WeakODRLinkage;
  }
  llvm_unreachable("Unknown CIR linkage type");
}

void CIRGenPGO::setFuncName(llvm::StringRef RawName,
                            llvm::GlobalValue::LinkageTypes Linkage) {
  FuncName = llvm::getPGOFuncName(
      RawName, Linkage, CGM.getCodeGenOpts().MainFileName,
      llvm::IndexedInstrProf::Version);
  FuncLinkage = static_cast<unsigned>(Linkage);
}

void CIRGenPGO::setFuncName(cir::FuncOp Fn) {
  auto Linkage = convertCIRToLLVMLinkage(Fn.getLinkage());
  llvm::StringRef RawName = Fn.getName();
  if (auto Attr = Fn->getAttrOfType<mlir::StringAttr>("cir.frontend_pgo_name"))
    RawName = Attr.getValue();
  setFuncName(RawName, Linkage);
}

CIRGenPGO::SavedState CIRGenPGO::saveState() {
  SavedState State;
  State.FuncName = std::move(FuncName);
  State.NumRegionCounters = NumRegionCounters;
  State.FunctionHash = FunctionHash;
  State.RegionCounterMap = std::move(RegionCounterMap);
  State.FuncLinkage = FuncLinkage;
  return State;
}

void CIRGenPGO::restoreState(SavedState State) {
  FuncName = std::move(State.FuncName);
  NumRegionCounters = State.NumRegionCounters;
  FunctionHash = State.FunctionHash;
  RegionCounterMap = std::move(State.RegionCounterMap);
  FuncLinkage = State.FuncLinkage;
}

void CIRGenPGO::clearRegionCounters() {
  FuncName.clear();
  NumRegionCounters = 0;
  FunctionHash = 0;
  RegionCounterMap.reset();
  FuncLinkage = 0;
}

bool CIRGenPGO::shouldAssignRegionCounters(GlobalDecl GD) const {
  const Decl *D = GD.getDecl();
  if (!D->hasBody())
    return false;

  // Skip CUDA/HIP kernel launch stub functions.
  if (CGM.getLangOpts().CUDA && !CGM.getLangOpts().CUDAIsDevice &&
      D->hasAttr<CUDAGlobalAttr>())
    return false;

  if (!CGM.getCodeGenOpts().hasProfileClangInstr())
    return false;
  if (D->isImplicit())
    return false;

  // Instrument only base ctor/dtor variants; others delegate to base.
  if (CGM.getTarget().getCXXABI().hasConstructorVariants()) {
    if (const auto *CCD = dyn_cast<CXXConstructorDecl>(D))
      if (GD.getCtorType() != Ctor_Base &&
          CIRGenFunction::isConstructorDelegationValid(CCD))
        return false;
  }
  if (isa<CXXDestructorDecl>(D) && GD.getDtorType() != Dtor_Base)
    return false;

  // Skip functions with no-profile attributes.
  if (D->hasAttr<NoProfileFunctionAttr>())
    return false;

  // Match classic CodeGen: do not instrument functions defined in system
  // headers (e.g. inline STL template instantiations) unless coverage of
  // system headers was explicitly requested. Without this, profiling a
  // translation unit that uses the standard library instruments dozens of
  // STL helpers per container use, bloating the profile with counters classic
  // never emits.
  clang::SourceManager &sm = CGM.getASTContext().getSourceManager();
  if (!llvm::coverage::SystemHeadersCoverage &&
      sm.isInSystemHeader(D->getLocation()))
    return false;

  return true;
}

CIRGenPGO::RegionCountersScope::RegionCountersScope(CIRGenPGO &PGO)
    : PGO(&PGO), State(PGO.saveState()) {}

CIRGenPGO::RegionCountersScope::RegionCountersScope(
    RegionCountersScope &&Other) noexcept
    : PGO(Other.PGO), State(std::move(Other.State)) {
  Other.PGO = nullptr;
}

CIRGenPGO::RegionCountersScope &
CIRGenPGO::RegionCountersScope::operator=(RegionCountersScope &&Other) noexcept {
  if (this == &Other)
    return *this;
  if (PGO)
    PGO->restoreState(std::move(State));
  PGO = Other.PGO;
  State = std::move(Other.State);
  Other.PGO = nullptr;
  return *this;
}

CIRGenPGO::RegionCountersScope::~RegionCountersScope() {
  if (PGO)
    PGO->restoreState(std::move(State));
}

CIRGenPGO::RegionCountersScope
CIRGenPGO::enterRegionCounters(GlobalDecl GD, llvm::StringRef RawName,
                               llvm::GlobalValue::LinkageTypes Linkage) {
  RegionCountersScope Scope(*this);
  clearRegionCounters();
  if (!shouldAssignRegionCounters(GD))
    return Scope;

  setFuncName(RawName, Linkage);
  mapRegionCounters(GD.getDecl());
  return Scope;
}

void CIRGenPGO::mapRegionCounters(const Decl *D) {
  PGOHashVersion HashVersion = PGO_HASH_LATEST;
  uint64_t ProfileVersion = llvm::IndexedInstrProf::Version;

  RegionCounterMap.reset(new llvm::DenseMap<const Stmt *, unsigned>);
  MapRegionCounters Walker(HashVersion, ProfileVersion, *RegionCounterMap,
                           CGM.getDiags());
  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D))
    Walker.TraverseDecl(const_cast<FunctionDecl *>(FD));
  else if (const ObjCMethodDecl *MD = dyn_cast_or_null<ObjCMethodDecl>(D))
    Walker.TraverseDecl(const_cast<ObjCMethodDecl *>(MD));
  else if (const BlockDecl *BD = dyn_cast_or_null<BlockDecl>(D))
    Walker.TraverseDecl(const_cast<BlockDecl *>(BD));
  else if (const CapturedDecl *CD = dyn_cast_or_null<CapturedDecl>(D))
    Walker.TraverseDecl(const_cast<CapturedDecl *>(CD));
  assert(Walker.NextCounter > 0 && "no entry counter mapped for decl");
  NumRegionCounters = Walker.NextCounter;
  FunctionHash = Walker.Hash.finalize();
  if (HashVersion >= PGO_HASH_V4)
    FunctionHash &= llvm::NamedInstrProfRecord::FUNC_HASH_MASK;
}

void CIRGenPGO::assignRegionCounters(GlobalDecl GD, cir::FuncOp Fn) {
  clearRegionCounters();
  if (!shouldAssignRegionCounters(GD))
    return;

  switch (CGM.isFunctionBlockedFromProfileInstr(Fn.getSymName(),
                                                GD.getDecl()->getLocation())) {
  case ProfileList::Skip:
  case ProfileList::Forbid:
    return;
  case ProfileList::Allow:
    break;
  }

  setFuncName(Fn);
  mapRegionCounters(GD.getDecl());
}

void CIRGenPGO::emitCounterIncrement(CIRGenBuilderTy &Builder, const Stmt *S,
                                      mlir::Location Loc, mlir::Value StepV) {
  if (!RegionCounterMap)
    return;

  auto It = RegionCounterMap->find(S);
  if (It == RegionCounterMap->end())
    return;

  unsigned Counter = It->second;

  if (!Builder.getInsertionBlock())
    return;

  auto cirModule = CGM.getModule();
  auto ptrTy = cir::PointerType::get(cir::VoidType::get(Builder.getContext()));
  auto i64Ty = cir::IntType::get(Builder.getContext(), 64, /*isSigned=*/false);
  auto i32Ty = cir::IntType::get(Builder.getContext(), 32, /*isSigned=*/false);
  auto voidTy = cir::VoidType::get(Builder.getContext());

  // A stepped increment (counter += StepV) lowers to llvm.instrprof.increment.step
  // and uses a distinct stub carrying an extra i64 step operand. The plain +1
  // form keeps the original stub so its lowering path is unchanged.
  const bool stepped = static_cast<bool>(StepV);
  llvm::StringRef stubName = stepped ? "__cir_instrprof_increment_step"
                                     : "__cir_instrprof_increment";

  // Get or create the stub function.
  auto stubFn = cirModule.lookupSymbol<cir::FuncOp>(stubName);
  if (!stubFn) {
    auto fnTy =
        stepped
            ? cir::FuncType::get({ptrTy, i64Ty, i32Ty, i32Ty, i64Ty}, voidTy)
            : cir::FuncType::get({ptrTy, i64Ty, i32Ty, i32Ty}, voidTy);

    mlir::OpBuilder::InsertionGuard guard(Builder);
    Builder.setInsertionPointToStart(cirModule.getBody());
    stubFn = cir::FuncOp::create(Builder, Loc, stubName, fnTy);
    mlir::SymbolTable::setSymbolVisibility(
        stubFn, mlir::SymbolTable::Visibility::Private);
  }

  // Build arguments: (null_ptr, hash, num_counters, index[, step]).
  mlir::Value nullPtr = cir::ConstantOp::create(
      Builder, Loc,
      cir::ConstPtrAttr::get(ptrTy, Builder.getI64IntegerAttr(0)));
  mlir::Value hashVal = cir::ConstantOp::create(
      Builder, Loc,
      cir::IntAttr::get(i64Ty, FunctionHash));
  mlir::Value numCountersVal = cir::ConstantOp::create(
      Builder, Loc,
      cir::IntAttr::get(i32Ty, NumRegionCounters));
  mlir::Value indexVal = cir::ConstantOp::create(
      Builder, Loc,
      cir::IntAttr::get(i32Ty, Counter));

  llvm::SmallVector<mlir::Value, 5> args{nullPtr, hashVal, numCountersVal,
                                         indexVal};
  if (stepped) {
    // instrprof.increment.step takes an i64 step. Widen a boolean condition
    // (the do-while case) or any narrower integer to i64.
    mlir::Value step = StepV;
    if (mlir::isa<cir::BoolType>(step.getType()))
      step = Builder.createCast(cir::CastKind::bool_to_int, step, i64Ty);
    else if (step.getType() != i64Ty)
      step = Builder.createIntCast(step, i64Ty);
    args.push_back(step);
  }

  auto callOp = cir::CallOp::create(
      Builder, Loc,
      mlir::SymbolRefAttr::get(Builder.getContext(), stubFn.getSymName()),
      cir::VoidType::get(Builder.getContext()), args);

  // Attach PGO metadata as discardable attributes.
  callOp->setAttr("cir.instrprof_func_name",
                   Builder.getStringAttr(FuncName));
  callOp->setAttr("cir.instrprof_func_linkage",
                   Builder.getI32IntegerAttr(FuncLinkage));
  callOp->setAttr("cir.nosanitize",
                   mlir::UnitAttr::get(Builder.getContext()));
}
