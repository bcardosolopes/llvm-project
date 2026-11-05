//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Decl nodes as CIR code.
//
//===----------------------------------------------------------------------===//

#include "CIRGenFunction.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/OffloadArch.h"
#include "clang/Basic/OpenMPKinds.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::CIRGen;

namespace {

static void setRuntimeFnNothrow(cir::FuncOp fn) {
  mlir::NamedAttrList attrs;
  if (auto existingAttrs = fn.getExtraAttrs())
    attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
  attrs.set("nothrow", mlir::UnitAttr::get(fn->getContext()));
  fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
      attrs.getDictionary(fn->getContext())));
}

static OffloadArch getOffloadArch(CIRGenModule &cgm) {
  if (!cgm.getTarget().hasFeature("ptx"))
    return OffloadArch::UNKNOWN;
  for (const auto &feature : cgm.getTarget().getTargetOpts().FeatureMap) {
    if (!feature.getValue())
      continue;
    OffloadArch arch = StringToOffloadArch(feature.getKey());
    if (arch != OffloadArch::UNKNOWN)
      return arch;
  }
  return OffloadArch::UNKNOWN;
}

static bool needsOpenMPThreadPrivateRuntimeRegistration(CIRGenModule &cgm,
                                                        const VarDecl *vd) {
  if (cgm.getLangOpts().OpenMPUseTLS &&
      cgm.getASTContext().getTargetInfo().isTLSSupported())
    return false;

  if (cgm.getLangOpts().OpenMPIsTargetDevice)
    return false;

  const Expr *init = vd->getAnyInitializer();
  bool performInit =
      init && !init->isConstantInitializer(cgm.getASTContext(), false);
  return performInit ||
         vd->getType().isDestructedType() != QualType::DK_none;
}

static cir::GlobalOp getOrCreateOpenMPRuntimeLocationString(CIRGenModule &cgm,
                                                            mlir::Location loc,
                                                            StringRef str) {
  return cgm.getGlobalForConstantCString(loc, str,
                                         "__omp_rtl_loc.str");
}

static cir::GlobalOp getOrCreateOpenMPRuntimeLocation(CIRGenModule &cgm,
                                                      mlir::Location loc,
                                                      unsigned flags,
                                                      StringRef srcLocStr) {
  cir::GlobalOp sourceGlobal =
      getOrCreateOpenMPRuntimeLocationString(cgm, loc, srcLocStr);
  std::string name =
      ("__omp_rtl_loc." + llvm::Twine(flags) + "." +
       sourceGlobal.getSymName().str())
          .str();
  if (auto *existing = cgm.getGlobalValue(name))
    return mlir::cast<cir::GlobalOp>(existing);

  CIRGenBuilderTy &builder = cgm.getBuilder();
  auto intTy = builder.getSInt32Ty();
  auto charPtrTy = builder.getPointerTo(builder.getSInt8Ty());
  auto identTy = builder.getAnonRecordTy(
      llvm::ArrayRef<mlir::Type>{intTy, intTy, intTy, intTy, charPtrTy});
  auto identInit = builder.getAnonConstRecord(mlir::ArrayAttr::get(
      &cgm.getMLIRContext(),
      {cir::IntAttr::get(intTy, 0), cir::IntAttr::get(intTy, flags),
       cir::IntAttr::get(intTy, 0), cir::IntAttr::get(intTy, 22),
       builder.getGlobalViewAttr(charPtrTy, sourceGlobal)}));

  cir::GlobalOp identGlobal =
      CIRGenModule::createGlobalOp(cgm, loc, name, identTy,
                                   /*isConstant=*/true);
  identGlobal.setLinkage(cir::GlobalLinkageKind::PrivateLinkage);
  identGlobal.setSymVisibility("private");
  identGlobal.setAlignment(cgm.getPointerAlign().getAsAlign().value());
  identGlobal->setAttr("unnamed_addr",
                       mlir::UnitAttr::get(&cgm.getMLIRContext()));
  CIRGenModule::setInitializer(identGlobal, identInit);
  return identGlobal;
}

static void emitOMPThreadPrivateDecl(CIRGenModule &cgm,
                                     const OMPThreadPrivateDecl &d) {
  // Do not emit threadprivates in simd-only mode.
  if (cgm.getLangOpts().OpenMP && cgm.getLangOpts().OpenMPSimd)
    return;

  // Match OGCG's EmitOMPThreadPrivateDecl for target-device builds: force the
  // threadprivate symbol into the module even though ordinary device-side
  // global emission skips host-only globals and functions.
  if (cgm.getLangOpts().OpenMPIsTargetDevice) {
    for (const Expr *refExpr : d.varlist()) {
      auto *vd = cast<VarDecl>(cast<DeclRefExpr>(refExpr)->getDecl());
      cgm.eraseUnusedGlobalVarAddr(cgm.getAddrOfGlobalVar(vd));
    }
    return;
  }

  // Runtime registration for non-TLS threadprivate variables is emitted from
  // the actual VarDecl initialization paths rather than the pragma node:
  // namespace-scope vars via emitCXXGlobalVarDeclInit() and static locals via
  // the guarded-init path. The pragma itself is a codegen no-op here.
  (void)d;
}

//===----------------------------------------------------------------------===//
// `#pragma omp declare simd` Vector Function ABI variant-name mangling.
//
// This mirrors classic CodeGen's CGOpenMPRuntime.cpp
// (evaluateCDTSize / mangleVectorParameters / emitX86DeclareSimdFunction /
// emitAArch64DeclareSimdFunction and emitDeclareSimdFunction). CIRGen does not
// link the classic CodeGen library, so the algorithm is replicated here; the
// only substantive difference is that instead of llvm::Function::addFnAttr the
// mangled names are handed to an `addFnAttr` callback so the caller can attach
// them to a cir::FuncOp. The mangled-name output is byte-identical to classic.
//===----------------------------------------------------------------------===//

/// Kind of parameter in a function with 'declare simd' directive.
enum ParamKindTy {
  Linear,
  LinearRef,
  LinearUVal,
  LinearVal,
  Uniform,
  Vector,
};
/// Attribute set of the parameter.
struct ParamAttrTy {
  ParamKindTy Kind = Vector;
  llvm::APSInt StrideOrArg;
  llvm::APSInt Alignment;
  bool HasVarStride = false;
};

static unsigned evaluateCDTSize(const FunctionDecl *FD,
                                llvm::ArrayRef<ParamAttrTy> ParamAttrs) {
  // See classic evaluateCDTSize for the characteristic-data-type (CDT) rules.
  QualType RetType = FD->getReturnType();
  if (RetType.isNull())
    return 0;
  ASTContext &C = FD->getASTContext();
  QualType CDT;
  if (!RetType.isNull() && !RetType->isVoidType()) {
    CDT = RetType;
  } else {
    unsigned Offset = 0;
    if (const auto *MD = dyn_cast<CXXMethodDecl>(FD)) {
      if (ParamAttrs[Offset].Kind == Vector)
        CDT = C.getPointerType(C.getCanonicalTagType(MD->getParent()));
      ++Offset;
    }
    if (CDT.isNull()) {
      for (unsigned I = 0, E = FD->getNumParams(); I < E; ++I) {
        if (ParamAttrs[I + Offset].Kind == Vector) {
          CDT = FD->getParamDecl(I)->getType();
          break;
        }
      }
    }
  }
  if (CDT.isNull())
    CDT = C.IntTy;
  CDT = CDT->getCanonicalTypeUnqualified();
  if (CDT->isRecordType() || CDT->isUnionType())
    CDT = C.IntTy;
  return C.getTypeSize(CDT);
}

/// Mangle the parameter part of the vector function name according to
/// their OpenMP classification (section 4.5 of the AAVFABI 2021Q1).
static std::string
mangleVectorParameters(llvm::ArrayRef<ParamAttrTy> ParamAttrs) {
  llvm::SmallString<256> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  for (const auto &ParamAttr : ParamAttrs) {
    switch (ParamAttr.Kind) {
    case Linear:
      Out << 'l';
      break;
    case LinearRef:
      Out << 'R';
      break;
    case LinearUVal:
      Out << 'U';
      break;
    case LinearVal:
      Out << 'L';
      break;
    case Uniform:
      Out << 'u';
      break;
    case Vector:
      Out << 'v';
      break;
    }
    if (ParamAttr.HasVarStride)
      Out << "s" << ParamAttr.StrideOrArg;
    else if (ParamAttr.Kind == Linear || ParamAttr.Kind == LinearRef ||
             ParamAttr.Kind == LinearUVal || ParamAttr.Kind == LinearVal) {
      // Don't print the step value if it is not present or if it is
      // equal to 1.
      if (ParamAttr.StrideOrArg < 0)
        Out << 'n' << -ParamAttr.StrideOrArg;
      else if (ParamAttr.StrideOrArg != 1)
        Out << ParamAttr.StrideOrArg;
    }

    if (!!ParamAttr.Alignment)
      Out << 'a' << ParamAttr.Alignment;
  }

  return std::string(Out.str());
}

using AddFnAttrTy = llvm::function_ref<void(llvm::StringRef)>;

static void
emitX86DeclareSimdFunction(const FunctionDecl *FD, llvm::StringRef MangledName,
                           const llvm::APSInt &VLENVal,
                           llvm::ArrayRef<ParamAttrTy> ParamAttrs,
                           OMPDeclareSimdDeclAttr::BranchStateTy State,
                           AddFnAttrTy addFnAttr) {
  struct ISADataTy {
    char ISA;
    unsigned VecRegSize;
  };
  ISADataTy ISAData[] = {
      {'b', 128}, // SSE
      {'c', 256}, // AVX
      {'d', 256}, // AVX2
      {'e', 512}, // AVX512
  };
  llvm::SmallVector<char, 2> Masked;
  switch (State) {
  case OMPDeclareSimdDeclAttr::BS_Undefined:
    Masked.push_back('N');
    Masked.push_back('M');
    break;
  case OMPDeclareSimdDeclAttr::BS_Notinbranch:
    Masked.push_back('N');
    break;
  case OMPDeclareSimdDeclAttr::BS_Inbranch:
    Masked.push_back('M');
    break;
  }
  for (char Mask : Masked) {
    for (const ISADataTy &Data : ISAData) {
      llvm::SmallString<256> Buffer;
      llvm::raw_svector_ostream Out(Buffer);
      Out << "_ZGV" << Data.ISA << Mask;
      if (!VLENVal) {
        unsigned NumElts = evaluateCDTSize(FD, ParamAttrs);
        assert(NumElts && "Non-zero simdlen/cdtsize expected");
        Out << llvm::APSInt::getUnsigned(Data.VecRegSize / NumElts);
      } else {
        Out << VLENVal;
      }
      Out << mangleVectorParameters(ParamAttrs);
      Out << '_' << MangledName;
      addFnAttr(Out.str());
    }
  }
}

/// Maps To Vector (MTV), as defined in 4.1.1 of the AAVFABI (2021Q1).
static bool getAArch64MTV(QualType QT, ParamKindTy Kind) {
  QT = QT.getCanonicalType();

  if (QT->isVoidType())
    return false;

  if (Kind == ParamKindTy::Uniform)
    return false;

  if (Kind == ParamKindTy::LinearUVal || Kind == ParamKindTy::LinearRef)
    return false;

  if ((Kind == ParamKindTy::Linear || Kind == ParamKindTy::LinearVal) &&
      !QT->isReferenceType())
    return false;

  return true;
}

/// Pass By Value (PBV), as defined in 3.1.2 of the AAVFABI.
static bool getAArch64PBV(QualType QT, ASTContext &C) {
  QT = QT.getCanonicalType();
  unsigned Size = C.getTypeSize(QT);

  // Only scalars and complex within 16 bytes wide set PVB to true.
  if (Size != 8 && Size != 16 && Size != 32 && Size != 64 && Size != 128)
    return false;

  if (QT->isFloatingType())
    return true;

  if (QT->isIntegerType())
    return true;

  if (QT->isPointerType())
    return true;

  // TODO: Add support for complex types (section 3.1.2, item 2).

  return false;
}

/// Computes the lane size (LS) of a return type or of an input parameter,
/// as defined by `LS(P)` in 3.2.1 of the AAVFABI.
static unsigned getAArch64LS(QualType QT, ParamKindTy Kind, ASTContext &C) {
  if (!getAArch64MTV(QT, Kind) && QT.getCanonicalType()->isPointerType()) {
    QualType PTy = QT.getCanonicalType()->getPointeeType();
    if (getAArch64PBV(PTy, C))
      return C.getTypeSize(PTy);
  }
  if (getAArch64PBV(QT, C))
    return C.getTypeSize(QT);

  return C.getTypeSize(C.getUIntPtrType());
}

// Get Narrowest Data Size (NDS) and Widest Data Size (WDS) from the
// signature of the scalar function, as defined in 3.2.2 of the AAVFABI.
static std::tuple<unsigned, unsigned, bool>
getNDSWDS(const FunctionDecl *FD, llvm::ArrayRef<ParamAttrTy> ParamAttrs) {
  QualType RetType = FD->getReturnType().getCanonicalType();

  ASTContext &C = FD->getASTContext();

  bool OutputBecomesInput = false;

  llvm::SmallVector<unsigned, 8> Sizes;
  if (!RetType->isVoidType()) {
    Sizes.push_back(getAArch64LS(RetType, ParamKindTy::Vector, C));
    if (!getAArch64PBV(RetType, C) && getAArch64MTV(RetType, {}))
      OutputBecomesInput = true;
  }
  for (unsigned I = 0, E = FD->getNumParams(); I < E; ++I) {
    QualType QT = FD->getParamDecl(I)->getType().getCanonicalType();
    Sizes.push_back(getAArch64LS(QT, ParamAttrs[I].Kind, C));
  }

  assert(!Sizes.empty() && "Unable to determine NDS and WDS.");
  // The LS of a function parameter / return value can only be a power
  // of 2, starting from 8 bits, up to 128.
  assert(llvm::all_of(Sizes,
                      [](unsigned Size) {
                        return Size == 8 || Size == 16 || Size == 32 ||
                               Size == 64 || Size == 128;
                      }) &&
         "Invalid size");

  return std::make_tuple(*llvm::min_element(Sizes), *llvm::max_element(Sizes),
                         OutputBecomesInput);
}

// Function used to add the attribute. The parameter `VLEN` is templated to
// allow the use of "x" when targeting scalable functions for SVE.
template <typename T>
static void addAArch64VectorName(T VLEN, llvm::StringRef LMask,
                                 llvm::StringRef Prefix, char ISA,
                                 llvm::StringRef ParSeq,
                                 llvm::StringRef MangledName,
                                 bool OutputBecomesInput, AddFnAttrTy addFnAttr) {
  llvm::SmallString<256> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  Out << Prefix << ISA << LMask << VLEN;
  if (OutputBecomesInput)
    Out << "v";
  Out << ParSeq << "_" << MangledName;
  addFnAttr(Out.str());
}

// Helper function to generate the Advanced SIMD names depending on the value
// of the NDS when simdlen is not present.
static void addAArch64AdvSIMDNDSNames(unsigned NDS, llvm::StringRef Mask,
                                      llvm::StringRef Prefix, char ISA,
                                      llvm::StringRef ParSeq,
                                      llvm::StringRef MangledName,
                                      bool OutputBecomesInput,
                                      AddFnAttrTy addFnAttr) {
  switch (NDS) {
  case 8:
    addAArch64VectorName(8, Mask, Prefix, ISA, ParSeq, MangledName,
                         OutputBecomesInput, addFnAttr);
    addAArch64VectorName(16, Mask, Prefix, ISA, ParSeq, MangledName,
                         OutputBecomesInput, addFnAttr);
    break;
  case 16:
    addAArch64VectorName(4, Mask, Prefix, ISA, ParSeq, MangledName,
                         OutputBecomesInput, addFnAttr);
    addAArch64VectorName(8, Mask, Prefix, ISA, ParSeq, MangledName,
                         OutputBecomesInput, addFnAttr);
    break;
  case 32:
    addAArch64VectorName(2, Mask, Prefix, ISA, ParSeq, MangledName,
                         OutputBecomesInput, addFnAttr);
    addAArch64VectorName(4, Mask, Prefix, ISA, ParSeq, MangledName,
                         OutputBecomesInput, addFnAttr);
    break;
  case 64:
  case 128:
    addAArch64VectorName(2, Mask, Prefix, ISA, ParSeq, MangledName,
                         OutputBecomesInput, addFnAttr);
    break;
  default:
    llvm_unreachable("Scalar type is too wide.");
  }
}

/// Emit vector function attributes for AArch64, as defined in the AAVFABI.
static void emitAArch64DeclareSimdFunction(
    CIRGenModule &cgm, const FunctionDecl *FD, unsigned UserVLEN,
    llvm::ArrayRef<ParamAttrTy> ParamAttrs,
    OMPDeclareSimdDeclAttr::BranchStateTy State, llvm::StringRef MangledName,
    char ISA, unsigned VecRegSize, SourceLocation SLoc, AddFnAttrTy addFnAttr) {
  // Get basic data for building the vector signature.
  const auto Data = getNDSWDS(FD, ParamAttrs);
  const unsigned NDS = std::get<0>(Data);
  const unsigned WDS = std::get<1>(Data);
  const bool OutputBecomesInput = std::get<2>(Data);

  // Check the values provided via `simdlen` by the user.
  // 1. A `simdlen(1)` doesn't produce vector signatures,
  if (UserVLEN == 1) {
    cgm.getDiags().Report(SLoc, diag::warn_simdlen_1_no_effect);
    return;
  }

  // 2. Section 3.3.1, item 1: user input must be a power of 2 for
  // Advanced SIMD output.
  if (ISA == 'n' && UserVLEN && !llvm::isPowerOf2_32(UserVLEN)) {
    cgm.getDiags().Report(SLoc, diag::warn_simdlen_requires_power_of_2);
    return;
  }

  // 3. Section 3.4.1. SVE fixed length must obey the architectural limits.
  if (ISA == 's' && UserVLEN != 0) {
    if ((UserVLEN * WDS > 2048) || (UserVLEN * WDS % 128 != 0)) {
      cgm.getDiags().Report(SLoc, diag::warn_simdlen_must_fit_lanes) << WDS;
      return;
    }
  }

  // Sort out parameter sequence.
  const std::string ParSeq = mangleVectorParameters(ParamAttrs);
  llvm::StringRef Prefix = "_ZGV";
  // Generate simdlen from user input (if any).
  if (UserVLEN) {
    if (ISA == 's') {
      // SVE generates only a masked function.
      addAArch64VectorName(UserVLEN, "M", Prefix, ISA, ParSeq, MangledName,
                           OutputBecomesInput, addFnAttr);
    } else {
      assert(ISA == 'n' && "Expected ISA either 's' or 'n'.");
      // Advanced SIMD generates one or two functions, depending on the
      // `[not]inbranch` clause.
      switch (State) {
      case OMPDeclareSimdDeclAttr::BS_Undefined:
        addAArch64VectorName(UserVLEN, "N", Prefix, ISA, ParSeq, MangledName,
                             OutputBecomesInput, addFnAttr);
        addAArch64VectorName(UserVLEN, "M", Prefix, ISA, ParSeq, MangledName,
                             OutputBecomesInput, addFnAttr);
        break;
      case OMPDeclareSimdDeclAttr::BS_Notinbranch:
        addAArch64VectorName(UserVLEN, "N", Prefix, ISA, ParSeq, MangledName,
                             OutputBecomesInput, addFnAttr);
        break;
      case OMPDeclareSimdDeclAttr::BS_Inbranch:
        addAArch64VectorName(UserVLEN, "M", Prefix, ISA, ParSeq, MangledName,
                             OutputBecomesInput, addFnAttr);
        break;
      }
    }
  } else {
    // If no user simdlen is provided, follow the AAVFABI rules for
    // generating the vector length.
    if (ISA == 's') {
      // SVE, section 3.4.1, item 1.
      addAArch64VectorName("x", "M", Prefix, ISA, ParSeq, MangledName,
                           OutputBecomesInput, addFnAttr);
    } else {
      assert(ISA == 'n' && "Expected ISA either 's' or 'n'.");
      // Advanced SIMD, Section 3.3.1 of the AAVFABI, generates one or two
      // vector names depending on the use of the clause `[not]inbranch`.
      switch (State) {
      case OMPDeclareSimdDeclAttr::BS_Undefined:
        addAArch64AdvSIMDNDSNames(NDS, "N", Prefix, ISA, ParSeq, MangledName,
                                  OutputBecomesInput, addFnAttr);
        addAArch64AdvSIMDNDSNames(NDS, "M", Prefix, ISA, ParSeq, MangledName,
                                  OutputBecomesInput, addFnAttr);
        break;
      case OMPDeclareSimdDeclAttr::BS_Notinbranch:
        addAArch64AdvSIMDNDSNames(NDS, "N", Prefix, ISA, ParSeq, MangledName,
                                  OutputBecomesInput, addFnAttr);
        break;
      case OMPDeclareSimdDeclAttr::BS_Inbranch:
        addAArch64AdvSIMDNDSNames(NDS, "M", Prefix, ISA, ParSeq, MangledName,
                                  OutputBecomesInput, addFnAttr);
        break;
      }
    }
  }
}

} // namespace

void CIRGenModule::emitOMPDeclareSimdFunction(const FunctionDecl *fd,
                                              cir::FuncOp fn) {
  ASTContext &C = getASTContext();
  llvm::StringRef mangledName = fn.getName();

  // Collect the mangled variant names, then attach them to the FuncOp's
  // extra-attributes dictionary as UnitAttr flags. The DirectToLLVM lowering
  // walks that dictionary and turns each UnitAttr entry into a value-less LLVM
  // function string attribute, matching classic CodeGen's Fn->addFnAttr(name).
  llvm::SmallVector<std::string, 8> variantNames;
  auto addFnAttr = [&](llvm::StringRef name) {
    variantNames.emplace_back(name.str());
  };

  const FunctionDecl *cur = fd->getMostRecentDecl();
  while (cur) {
    // Map params to their positions in function decl.
    llvm::DenseMap<const Decl *, unsigned> ParamPositions;
    if (isa<CXXMethodDecl>(cur))
      ParamPositions.try_emplace(cur, 0);
    unsigned ParamPos = ParamPositions.size();
    for (const ParmVarDecl *P : cur->parameters()) {
      ParamPositions.try_emplace(P->getCanonicalDecl(), ParamPos);
      ++ParamPos;
    }
    for (const auto *attr : cur->specific_attrs<OMPDeclareSimdDeclAttr>()) {
      llvm::SmallVector<ParamAttrTy, 8> ParamAttrs(ParamPositions.size());
      // Mark uniform parameters.
      for (const Expr *E : attr->uniforms()) {
        E = E->IgnoreParenImpCasts();
        unsigned Pos;
        if (isa<CXXThisExpr>(E)) {
          Pos = ParamPositions[cur];
        } else {
          const auto *PVD = cast<ParmVarDecl>(cast<DeclRefExpr>(E)->getDecl())
                                ->getCanonicalDecl();
          auto It = ParamPositions.find(PVD);
          assert(It != ParamPositions.end() && "Function parameter not found");
          Pos = It->second;
        }
        ParamAttrs[Pos].Kind = Uniform;
      }
      // Get alignment info.
      auto *NI = attr->alignments_begin();
      for (const Expr *E : attr->aligneds()) {
        E = E->IgnoreParenImpCasts();
        unsigned Pos;
        QualType ParmTy;
        if (isa<CXXThisExpr>(E)) {
          Pos = ParamPositions[cur];
          ParmTy = E->getType();
        } else {
          const auto *PVD = cast<ParmVarDecl>(cast<DeclRefExpr>(E)->getDecl())
                                ->getCanonicalDecl();
          auto It = ParamPositions.find(PVD);
          assert(It != ParamPositions.end() && "Function parameter not found");
          Pos = It->second;
          ParmTy = PVD->getType();
        }
        ParamAttrs[Pos].Alignment =
            (*NI) ? (*NI)->EvaluateKnownConstInt(C)
                  : llvm::APSInt::getUnsigned(
                        C.toCharUnitsFromBits(
                             C.getOpenMPDefaultSimdAlign(ParmTy))
                            .getQuantity());
        ++NI;
      }
      // Mark linear parameters.
      auto *SI = attr->steps_begin();
      auto *MI = attr->modifiers_begin();
      for (const Expr *E : attr->linears()) {
        E = E->IgnoreParenImpCasts();
        unsigned Pos;
        bool IsReferenceType = false;
        // Rescaling factor needed to compute the linear parameter value in the
        // mangled name.
        unsigned PtrRescalingFactor = 1;
        if (isa<CXXThisExpr>(E)) {
          Pos = ParamPositions[cur];
          auto *P = cast<PointerType>(E->getType());
          PtrRescalingFactor =
              C.getTypeSizeInChars(P->getPointeeType()).getQuantity();
        } else {
          const auto *PVD = cast<ParmVarDecl>(cast<DeclRefExpr>(E)->getDecl())
                                ->getCanonicalDecl();
          auto It = ParamPositions.find(PVD);
          assert(It != ParamPositions.end() && "Function parameter not found");
          Pos = It->second;
          if (auto *P = dyn_cast<PointerType>(PVD->getType()))
            PtrRescalingFactor =
                C.getTypeSizeInChars(P->getPointeeType()).getQuantity();
          else if (PVD->getType()->isReferenceType()) {
            IsReferenceType = true;
            PtrRescalingFactor =
                C.getTypeSizeInChars(PVD->getType().getNonReferenceType())
                    .getQuantity();
          }
        }
        ParamAttrTy &ParamAttr = ParamAttrs[Pos];
        if (*MI == OMPC_LINEAR_ref)
          ParamAttr.Kind = LinearRef;
        else if (*MI == OMPC_LINEAR_uval)
          ParamAttr.Kind = LinearUVal;
        else if (IsReferenceType)
          ParamAttr.Kind = LinearVal;
        else
          ParamAttr.Kind = Linear;
        // Assuming a stride of 1, for `linear` without modifiers.
        ParamAttr.StrideOrArg = llvm::APSInt::getUnsigned(1);
        if (*SI) {
          Expr::EvalResult Result;
          if (!(*SI)->EvaluateAsInt(Result, C, Expr::SE_AllowSideEffects)) {
            if (const auto *DRE =
                    cast<DeclRefExpr>((*SI)->IgnoreParenImpCasts())) {
              if (const auto *StridePVD =
                      dyn_cast<ParmVarDecl>(DRE->getDecl())) {
                ParamAttr.HasVarStride = true;
                auto It = ParamPositions.find(StridePVD->getCanonicalDecl());
                assert(It != ParamPositions.end() &&
                       "Function parameter not found");
                ParamAttr.StrideOrArg = llvm::APSInt::getUnsigned(It->second);
              }
            }
          } else {
            ParamAttr.StrideOrArg = Result.Val.getInt();
          }
        }
        // If we are using a linear clause on a pointer, we need to rescale the
        // value of linear_step with the byte size of the pointee type.
        if (!ParamAttr.HasVarStride &&
            (ParamAttr.Kind == Linear || ParamAttr.Kind == LinearRef))
          ParamAttr.StrideOrArg = ParamAttr.StrideOrArg * PtrRescalingFactor;
        ++SI;
        ++MI;
      }
      llvm::APSInt VLENVal;
      SourceLocation ExprLoc;
      const Expr *VLENExpr = attr->getSimdlen();
      if (VLENExpr) {
        VLENVal = VLENExpr->EvaluateKnownConstInt(C);
        ExprLoc = VLENExpr->getExprLoc();
      }
      OMPDeclareSimdDeclAttr::BranchStateTy State = attr->getBranchState();
      if (getTriple().isX86()) {
        emitX86DeclareSimdFunction(cur, mangledName, VLENVal, ParamAttrs, State,
                                   addFnAttr);
      } else if (getTriple().getArch() == llvm::Triple::aarch64) {
        unsigned VLEN = VLENVal.getExtValue();
        if (getTarget().hasFeature("sve"))
          emitAArch64DeclareSimdFunction(*this, cur, VLEN, ParamAttrs, State,
                                         mangledName, 's', 128, ExprLoc,
                                         addFnAttr);
        else if (getTarget().hasFeature("neon"))
          emitAArch64DeclareSimdFunction(*this, cur, VLEN, ParamAttrs, State,
                                         mangledName, 'n', 128, ExprLoc,
                                         addFnAttr);
      }
    }
    cur = cur->getPreviousDecl();
  }

  if (variantNames.empty())
    return;

  mlir::NamedAttrList attrs;
  if (auto existingAttrs = fn.getExtraAttrs())
    attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
  auto unit = mlir::UnitAttr::get(&getMLIRContext());
  for (const std::string &name : variantNames)
    attrs.set(name, unit);
  fn.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
      attrs.getDictionary(&getMLIRContext())));
}

void CIRGenModule::emitOMPThreadPrivateDecl(const OMPThreadPrivateDecl *d) {
  ::emitOMPThreadPrivateDecl(*this, *d);
}

void CIRGenFunction::emitOMPThreadPrivateDecl(const OMPThreadPrivateDecl &d) {
  ::emitOMPThreadPrivateDecl(getCIRGenModule(), d);
}

mlir::Value CIRGenFunction::emitOpenMPRuntimeLocation(SourceLocation loc,
                                                      unsigned flags) {
  mlir::Location runtimeLoc =
      loc.isValid() ? getLoc(loc) : builder.getUnknownLoc();
  std::string srcLocStr = ";unknown;unknown;0;0;;";
  if (loc.isValid()) {
    PresumedLoc ploc = getContext().getSourceManager().getPresumedLoc(loc);
    if (ploc.isValid()) {
      llvm::SmallString<128> buffer;
      llvm::raw_svector_ostream os(buffer);
      os << ';' << ploc.getFilename() << ';';
      if (const auto *fd = dyn_cast_or_null<FunctionDecl>(curFuncDecl))
        os << fd->getQualifiedNameAsString();
      else
        os << "unknown";
      os << ';' << ploc.getLine() << ';' << ploc.getColumn() << ";;";
      srcLocStr = std::string(buffer);
    }
  }

  cir::GlobalOp ompLocGlobal =
      getOrCreateOpenMPRuntimeLocation(cgm, runtimeLoc, flags, srcLocStr);
  auto ompLocPtrTy = builder.getPointerTo(ompLocGlobal.getSymType());
  return cir::GetGlobalOp::create(builder, runtimeLoc, ompLocPtrTy,
                                  ompLocGlobal.getSymNameAttr());
}

mlir::Value CIRGenFunction::emitOpenMPThreadID(SourceLocation loc,
                                               unsigned flags) {
  mlir::Location runtimeLoc =
      loc.isValid() ? getLoc(loc) : builder.getUnknownLoc();
  mlir::Value ompLoc = emitOpenMPRuntimeLocation(loc, flags);
  cir::FuncOp getThreadNumFn = cgm.createRuntimeFunction(
      builder.getFuncType({ompLoc.getType()}, builder.getSInt32Ty()),
      "__kmpc_global_thread_num");
  setRuntimeFnNothrow(getThreadNumFn);
  return emitRuntimeCall(runtimeLoc, getThreadNumFn, {ompLoc});
}

void CIRGenFunction::emitOMPThreadPrivateVarDefinition(const VarDecl *vd,
                                                       Address vdAddr,
                                                       bool performInit) {
  if (!vd->hasAttr<OMPThreadPrivateDeclAttr>() ||
      !needsOpenMPThreadPrivateRuntimeRegistration(cgm, vd))
    return;

  const auto *attr = vd->getAttr<OMPThreadPrivateDeclAttr>();
  mlir::Location loc = getLoc(attr->getLocation());
  CIRGenBuilderTy &builder = getBuilder();
  auto voidTy = builder.getVoidTy();
  auto voidPtrTy = builder.getVoidPtrTy();
  mlir::Type valueTy = convertTypeForMem(vd->getType());
  auto valuePtrTy = builder.getPointerTo(valueTy);

  auto createHelper = [&](llvm::StringRef name, cir::FuncType fnTy) {
    if (auto *existing = cgm.getGlobalValue(name))
      return mlir::cast<cir::FuncOp>(existing);

    cir::FuncOp fn = cgm.createCIRFunction(loc, name, fnTy, nullptr);
    fn.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
    fn.setSymVisibility("private");
    if (const char *section =
            getContext().getTargetInfo().getStaticInitSectionSpecifier())
      fn.setSectionAttr(builder.getStringAttr(section));
    cgm.setCIRFunctionAttributesForDefinition(GlobalDecl{}, fn);
    return fn;
  };

  auto emitCtorHelperBody = [&](cir::FuncOp fn) {
    if (!fn || !fn.getBody().empty())
      return;

    mlir::OpBuilder::InsertionGuard guard(builder);
    CIRGenFunction helperCGF(cgm, builder, /*suppressNewContext=*/true);
    helperCGF.curGD = GlobalDecl{};
    CIRGenFunction::SourceLocRAIIObject helperLoc(helperCGF, loc);
    mlir::Block *entry = fn.addEntryBlock();

    FunctionArgList args;
    ImplicitParamDecl dstParam(getContext(), /*DC=*/nullptr,
                               attr->getLocation(), /*Id=*/nullptr,
                               getContext().VoidPtrTy,
                               ImplicitParamKind::Other);
    args.push_back(&dstParam);

    CIRGenFunction::SymTableScopeTy varScope(helperCGF.symbolTable);
    {
      CIRGenFunction::LexicalScope fnScope(helperCGF, loc, entry);
      helperCGF.startFunction(GlobalDecl{}, getContext().VoidPtrTy, fn,
                              fn.getFunctionType(), args,
                              attr->getLocation(), attr->getLocation());

      mlir::Value dstAddr =
          builder.createLoad(loc, helperCGF.getAddrOfLocalVar(&dstParam));
      if (dstAddr.getType() != valuePtrTy)
        dstAddr = builder.createBitcast(loc, dstAddr, valuePtrTy);
      Address typedDst(dstAddr, valueTy, vdAddr.getAlignment());
      const Expr *init = vd->getAnyInitializer();
      assert(init && "threadprivate ctor helper requires an initializer");
      helperCGF.emitAnyExprToMem(init, typedDst,
                                 init->getType().getQualifiers(),
                                 /*isInit=*/true);

      mlir::Value retVal =
          builder.createLoad(loc, helperCGF.getAddrOfLocalVar(&dstParam));
      builder.createStore(loc, retVal, helperCGF.returnValue);
      helperCGF.finishFunction(attr->getLocation());
    }
  };

  auto emitDtorHelperBody = [&](cir::FuncOp fn) {
    if (!fn || !fn.getBody().empty())
      return;

    mlir::OpBuilder::InsertionGuard guard(builder);
    CIRGenFunction helperCGF(cgm, builder, /*suppressNewContext=*/true);
    helperCGF.curGD = GlobalDecl{};
    CIRGenFunction::SourceLocRAIIObject helperLoc(helperCGF, loc);
    mlir::Block *entry = fn.addEntryBlock();

    FunctionArgList args;
    ImplicitParamDecl dstParam(getContext(), /*DC=*/nullptr,
                               attr->getLocation(), /*Id=*/nullptr,
                               getContext().VoidPtrTy,
                               ImplicitParamKind::Other);
    args.push_back(&dstParam);

    CIRGenFunction::SymTableScopeTy varScope(helperCGF.symbolTable);
    {
      CIRGenFunction::LexicalScope fnScope(helperCGF, loc, entry);
      helperCGF.startFunction(GlobalDecl{}, getContext().VoidTy, fn,
                              fn.getFunctionType(), args,
                              attr->getLocation(), attr->getLocation());

      mlir::Value dstAddr =
          builder.createLoad(loc, helperCGF.getAddrOfLocalVar(&dstParam));
      if (dstAddr.getType() != valuePtrTy)
        dstAddr = builder.createBitcast(loc, dstAddr, valuePtrTy);
      Address typedDst(dstAddr, valueTy, vdAddr.getAlignment());
      QualType type = vd->getType();
      helperCGF.emitDestroy(typedDst, type,
                            helperCGF.getDestroyer(type.isDestructedType()),
                            helperCGF.needsEHCleanup(type.isDestructedType()));
      helperCGF.finishFunction(attr->getLocation());
    }
  };

  cir::FuncOp ctorHelper;
  cir::FuncOp dtorHelper;
  if (performInit) {
    std::string name =
        ("__kmpc_threadprivate_ctor." + cgm.getMangledName(vd)).str();
    ctorHelper =
        createHelper(name, builder.getFuncType({voidPtrTy}, voidPtrTy));
    emitCtorHelperBody(ctorHelper);
  }
  if (vd->getType().isDestructedType() != QualType::DK_none) {
    std::string name =
        ("__kmpc_threadprivate_dtor." + cgm.getMangledName(vd)).str();
    dtorHelper = createHelper(name, builder.getFuncType({voidPtrTy}, voidTy));
    emitDtorHelperBody(dtorHelper);
  }
  if (!ctorHelper && !dtorHelper)
    return;

  mlir::Value ompLoc = emitOpenMPRuntimeLocation(attr->getLocation(), 2);
  mlir::Value threadNum = emitOpenMPThreadID(attr->getLocation(), 2);
  (void)threadNum;

  cir::FuncOp registerFn = cgm.createRuntimeFunction(
      builder.getFuncType(
          {ompLoc.getType(), voidPtrTy, voidPtrTy, voidPtrTy, voidPtrTy},
          voidTy),
      "__kmpc_threadprivate_register");

  mlir::Value vdPtr = vdAddr.getPointer();
  if (vdPtr.getType() != voidPtrTy)
    vdPtr = builder.createBitcast(loc, vdPtr, voidPtrTy);

  mlir::Value ctorPtr = builder.getNullValue(voidPtrTy, loc);
  if (ctorHelper) {
    auto helperPtrTy = builder.getPointerTo(ctorHelper.getFunctionType());
    mlir::Value helperAddr = cir::GetGlobalOp::create(
        builder, loc, helperPtrTy, ctorHelper.getSymNameAttr());
    ctorPtr = builder.createBitcast(loc, helperAddr, voidPtrTy);
  }

  mlir::Value dtorPtr = builder.getNullValue(voidPtrTy, loc);
  if (dtorHelper) {
    auto helperPtrTy = builder.getPointerTo(dtorHelper.getFunctionType());
    mlir::Value helperAddr = cir::GetGlobalOp::create(
        builder, loc, helperPtrTy, dtorHelper.getSymNameAttr());
    dtorPtr = builder.createBitcast(loc, helperAddr, voidPtrTy);
  }

  mlir::Value copyCtorPtr = builder.getNullValue(voidPtrTy, loc);
  emitNounwindRuntimeCall(loc, registerFn,
                          {ompLoc, vdPtr, ctorPtr, copyCtorPtr, dtorPtr});
}

void CIRGenModule::emitOMPGroupPrivateDecl(const OMPGroupPrivateDecl *d) {
  // TODO(OpenMP): We don't properly differentiate between 'emitDecl' and
  // 'emitGlobal' and 'emitTopLevelDecl' in CIRGenDecl.cpp/CIRGenModule.cpp, so
  // if this decl requires we differentiate those, we probably need to split
  // this function into multiples.
  errorNYI(d->getSourceRange(), "OpenMP OMPGroupPrivateDecl");
  return;
}

void CIRGenFunction::emitOMPGroupPrivateDecl(const OMPGroupPrivateDecl &d) {
  // TODO(OpenMP): We don't properly differentiate between 'emitDecl' and
  // 'emitGlobal' and 'emitTopLevelDecl' in CIRGenDecl.cpp/CIRGenModule.cpp, so
  // if this decl requires we differentiate those, we probably need to split
  // this function into multiples.
  getCIRGenModule().errorNYI(d.getSourceRange(), "OpenMP OMPGroupPrivateDecl");
  return;
}

void CIRGenModule::emitOMPCapturedExpr(const OMPCapturedExprDecl *d) {
  // TODO(OpenMP): We don't properly differentiate between 'emitDecl' and
  // 'emitGlobal' and 'emitTopLevelDecl' in CIRGenDecl.cpp/CIRGenModule.cpp, so
  // if this decl requires we differentiate those, we probably need to split
  // this function into multiples.
  errorNYI(d->getSourceRange(), "OpenMP OMPCapturedExpr");
  return;
}

void CIRGenFunction::emitOMPCapturedExpr(const OMPCapturedExprDecl &d) {
  emitVarDecl(d);
  maybeEmitDeferredVarDeclInit(&d);
}

void CIRGenModule::emitOMPAllocateDecl(const OMPAllocateDecl *d) {
  // TODO(OpenMP): We don't properly differentiate between 'emitDecl' and
  // 'emitGlobal' and 'emitTopLevelDecl' in CIRGenDecl.cpp/CIRGenModule.cpp, so
  // if this decl requires we differentiate those, we probably need to split
  // this function into multiples.
  errorNYI(d->getSourceRange(), "OpenMP OMPAllocateDecl");
  return;
}

void CIRGenFunction::emitOMPAllocateDecl(const OMPAllocateDecl &d) {
  // TODO(OpenMP): We don't properly differentiate between 'emitDecl' and
  // 'emitGlobal' and 'emitTopLevelDecl' in CIRGenDecl.cpp/CIRGenModule.cpp, so
  // if this decl requires we differentiate those, we probably need to split
  // this function into multiples.
  getCIRGenModule().errorNYI(d.getSourceRange(), "OpenMP OMPAllocateDecl");
  return;
}

void CIRGenModule::emitOMPDeclareReduction(const OMPDeclareReductionDecl *d) {
  // User-defined reducers are materialized lazily from the reduction clauses
  // that reference them, because those clauses determine the concrete storage
  // shape (scalar vs byref aggregate/array) needed by the MLIR OpenMP recipe.
  (void)d;
}

void CIRGenFunction::emitOMPDeclareReduction(const OMPDeclareReductionDecl &d) {
  // See CIRGenModule::emitOMPDeclareReduction(). Local declare-reduction
  // directives are likewise materialized on demand by the enclosing reduction
  // clause.
  (void)d;
}

void CIRGenModule::emitOMPDeclareMapper(const OMPDeclareMapperDecl *d) {
  if (!getLangOpts().OpenMP || getLangOpts().OpenMPSimd ||
      (!getLangOpts().EmitAllDecls && !d->isUsed()))
    return;

  (void)getOrCreateOpenMPDeclareMapper(
      d, d->getLocation().isValid() ? getLoc(d->getLocation())
                                    : builder.getUnknownLoc());
}

void CIRGenFunction::emitOMPDeclareMapper(const OMPDeclareMapperDecl &d) {
  if (!getLangOpts().OpenMP || getLangOpts().OpenMPSimd ||
      (!getLangOpts().EmitAllDecls && !d.isUsed()))
    return;

  (void)getCIRGenModule().getOrCreateOpenMPDeclareMapper(
      &d, d.getLocation().isValid() ? getLoc(d.getLocation())
                                    : builder.getUnknownLoc());
}

void CIRGenModule::processOMPRequiresDecl(const OMPRequiresDecl *d) {
  for (const OMPClause *clause : d->clauselists()) {
    if (clause->getClauseKind() == llvm::omp::OMPC_unified_shared_memory) {
      if (getLangOpts().OpenMPIsTargetDevice) {
        OffloadArch arch = getOffloadArch(*this);
        switch (arch) {
        case OffloadArch::SM_20:
        case OffloadArch::SM_21:
        case OffloadArch::SM_30:
        case OffloadArch::SM_32_:
        case OffloadArch::SM_35:
        case OffloadArch::SM_37:
        case OffloadArch::SM_50:
        case OffloadArch::SM_52:
        case OffloadArch::SM_53: {
          SmallString<256> buffer;
          llvm::raw_svector_ostream out(buffer);
          out << "Target architecture " << OffloadArchToString(arch)
              << " does not support unified addressing";
          error(clause->getBeginLoc(), out.str());
          return;
        }
        case OffloadArch::LAST:
          llvm_unreachable("Unexpected GPU arch.");
        default:
          break;
        }
      }
      hasOpenMPRequiresUnifiedSharedMemory = true;
      theModule->setAttr(
          "omp.requires",
          mlir::omp::ClauseRequiresAttr::get(
              &getMLIRContext(),
              mlir::omp::ClauseRequires::unified_shared_memory));
      continue;
    }

    if (const auto *atomicClause =
            dyn_cast<OMPAtomicDefaultMemOrderClause>(clause)) {
      switch (atomicClause->getAtomicDefaultMemOrderKind()) {
      case OMPC_ATOMIC_DEFAULT_MEM_ORDER_acq_rel:
        openMPRequiresAtomicOrdering = llvm::AtomicOrdering::AcquireRelease;
        break;
      case OMPC_ATOMIC_DEFAULT_MEM_ORDER_seq_cst:
        openMPRequiresAtomicOrdering =
            llvm::AtomicOrdering::SequentiallyConsistent;
        break;
      case OMPC_ATOMIC_DEFAULT_MEM_ORDER_relaxed:
        openMPRequiresAtomicOrdering = llvm::AtomicOrdering::Monotonic;
        break;
      case OMPC_ATOMIC_DEFAULT_MEM_ORDER_unknown:
        break;
      }
    }
  }
}

void CIRGenModule::emitOMPRequiresDecl(const OMPRequiresDecl *d) {
  // TODO(OpenMP): We don't properly differentiate between 'emitDecl' and
  // 'emitGlobal' and 'emitTopLevelDecl' in CIRGenDecl.cpp/CIRGenModule.cpp, so
  // if this decl requires we differentiate those, we probably need to split
  // this function into multiples.
  processOMPRequiresDecl(d);
}

void CIRGenFunction::emitOMPRequiresDecl(const OMPRequiresDecl &d) {
  // TODO(OpenMP): We don't properly differentiate between 'emitDecl' and
  // 'emitGlobal' and 'emitTopLevelDecl' in CIRGenDecl.cpp/CIRGenModule.cpp, so
  // if this decl requires we differentiate those, we probably need to split
  // this function into multiples.
  getCIRGenModule().processOMPRequiresDecl(&d);
}
