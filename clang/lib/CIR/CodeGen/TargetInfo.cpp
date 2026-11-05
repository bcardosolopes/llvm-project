#include "TargetInfo.h"
#include "ABIInfo.h"
#include "CIRGenFunction.h"
#include "CIRGenModule.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Type.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"

#include <algorithm>

using namespace clang;
using namespace clang::CIRGen;

bool clang::CIRGen::isEmptyRecordForLayout(const ASTContext &context,
                                           QualType t) {
  const auto *rd = t->getAsRecordDecl();
  if (!rd)
    return false;

  // If this is a C++ record, check the bases first.
  if (const CXXRecordDecl *cxxrd = dyn_cast<CXXRecordDecl>(rd)) {
    if (cxxrd->isDynamicClass())
      return false;

    for (const auto &i : cxxrd->bases())
      if (!isEmptyRecordForLayout(context, i.getType()))
        return false;
  }

  for (const auto *i : rd->fields())
    if (!isEmptyFieldForLayout(context, i))
      return false;

  return true;
}

bool clang::CIRGen::isEmptyFieldForLayout(const ASTContext &context,
                                          const FieldDecl *fd) {
  if (fd->isZeroLengthBitField())
    return true;

  if (fd->isUnnamedBitField())
    return false;

  return isEmptyRecordForLayout(context, fd->getType());
}

namespace {

static bool isStreamingCompatible(const FunctionDecl *FD) {
  if (const auto *FPT = FD->getType()->getAs<FunctionProtoType>())
    return FPT->getAArch64SMEAttributes() &
           FunctionType::SME_PStateSMCompatibleMask;
  return false;
}

static bool isEmptyFieldForABI(const ASTContext &Context, const FieldDecl *FD,
                               bool AllowArrays);
static bool isEmptyRecordForABI(const ASTContext &Context, QualType Ty,
                                bool AllowArrays);

static bool isEmptyRecordForABI(const ASTContext &Context, QualType Ty,
                                bool AllowArrays) {
  const auto *RD = Ty->getAsRecordDecl();
  if (!RD || RD->hasFlexibleArrayMember())
    return false;

  if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(RD))
    for (const auto &Base : CXXRD->bases())
      if (!isEmptyRecordForABI(Context, Base.getType(), true))
        return false;

  for (const auto *FD : RD->fields())
    if (!isEmptyFieldForABI(Context, FD, AllowArrays))
      return false;

  return true;
}

static bool isEmptyFieldForABI(const ASTContext &Context, const FieldDecl *FD,
                               bool AllowArrays) {
  if (FD->isUnnamedBitField())
    return true;

  QualType FieldTy = FD->getType();
  bool WasArray = false;
  if (AllowArrays) {
    while (const auto *AT = Context.getAsConstantArrayType(FieldTy)) {
      if (AT->isZeroSize())
        return true;
      FieldTy = AT->getElementType();
      WasArray = true;
    }
  }

  const auto *RT = FieldTy->getAsCanonical<RecordType>();
  if (!RT)
    return false;

  if (isa<CXXRecordDecl>(RT->getDecl()) &&
      (WasArray || !FD->hasAttr<NoUniqueAddressAttr>()))
    return false;

  return isEmptyRecordForABI(Context, FieldTy, AllowArrays);
}

static bool isAArch64SoftFloatABI(const TargetInfo &TI) {
  return TI.getABI() == "aapcs-soft";
}

static bool isActualAArch64Target(const TargetInfo &TI) {
  const llvm::Triple &Triple = TI.getTriple();
  return Triple.getArch() == llvm::Triple::aarch64 ||
         Triple.getArch() == llvm::Triple::aarch64_be;
}

} // namespace

bool clang::CIRGen::shouldIgnoreAArch64VAArg(const ASTContext &Context,
                                             const TargetInfo &TI,
                                             QualType Ty) {
  const llvm::Triple &Triple = TI.getTriple();
  if (Triple.getArch() != llvm::Triple::aarch64 &&
      Triple.getArch() != llvm::Triple::aarch64_be)
    return false;

  if (!Ty->isRecordType() || Ty->isSVESizelessBuiltinType())
    return false;

  uint64_t Size = Context.getTypeSize(Ty);
  bool IsEmpty = isEmptyRecordForABI(Context, Ty, /*AllowArrays=*/true);
  if (TI.getABI() == "darwinpcs")
    return IsEmpty;

  if (!Context.getLangOpts().CPlusPlus)
    return IsEmpty || Size == 0;

  return Size == 0;
}

namespace {

static bool isAArch64HomogeneousAggregateBaseType(const ASTContext &Context,
                                                  const TargetInfo &TI,
                                                  QualType Ty) {
  if (isAArch64SoftFloatABI(TI))
    return false;

  if (const auto *BT = Ty->getAs<BuiltinType>())
    return BT->isFloatingPoint();

  const auto *VT = Ty->getAs<VectorType>();
  if (!VT)
    return false;

  if (auto Kind = VT->getVectorKind();
      Kind == VectorKind::SveFixedLengthData ||
      Kind == VectorKind::SveFixedLengthPredicate)
    return false;

  unsigned VecSize = Context.getTypeSize(VT);
  return VecSize == 64 || VecSize == 128;
}

static bool isAArch64HomogeneousAggregate(CIRGenTypes &CGT, QualType Ty,
                                          const Type *&Base,
                                          uint64_t &Members) {
  ASTContext &Context = CGT.getASTContext();
  const TargetInfo &TI = Context.getTargetInfo();

  if (const auto *AT = Context.getAsConstantArrayType(Ty)) {
    uint64_t NumElements = AT->getZExtSize();
    if (NumElements == 0)
      return false;
    if (!isAArch64HomogeneousAggregate(CGT, AT->getElementType(), Base,
                                       Members))
      return false;
    Members *= NumElements;
  } else if (const auto *RD = Ty->getAsRecordDecl()) {
    if (RD->hasFlexibleArrayMember())
      return false;

    Members = 0;

    if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
      for (const auto &BaseSpec : CXXRD->bases()) {
        if (isEmptyRecordForABI(Context, BaseSpec.getType(), true))
          continue;

        uint64_t FieldMembers;
        if (!isAArch64HomogeneousAggregate(CGT, BaseSpec.getType(), Base,
                                           FieldMembers))
          return false;

        Members += FieldMembers;
      }
    }

    for (const auto *FD : RD->fields()) {
      QualType FieldTy = FD->getType();
      while (const auto *AT = Context.getAsConstantArrayType(FieldTy)) {
        if (AT->isZeroSize())
          return false;
        FieldTy = AT->getElementType();
      }

      if (isEmptyRecordForABI(Context, FieldTy, true))
        continue;

      if (FD->isZeroLengthBitField())
        continue;

      uint64_t FieldMembers;
      if (!isAArch64HomogeneousAggregate(CGT, FD->getType(), Base,
                                         FieldMembers))
        return false;

      Members = RD->isUnion() ? std::max(Members, FieldMembers)
                              : Members + FieldMembers;
    }

    if (!Base)
      return false;

    if (Context.getTypeSize(Base) * Members != Context.getTypeSize(Ty))
      return false;
  } else {
    Members = 1;
    if (const auto *CT = Ty->getAs<ComplexType>()) {
      Members = 2;
      Ty = CT->getElementType();
    }

    if (!isAArch64HomogeneousAggregateBaseType(Context, TI, Ty))
      return false;

    const Type *TyPtr = Ty.getTypePtr();
    if (!Base) {
      Base = TyPtr;
      if (const auto *VT = Base->getAs<VectorType>()) {
        QualType ElementTy = VT->getElementType();
        unsigned NumElements =
            Context.getTypeSize(VT) / Context.getTypeSize(ElementTy);
        Base = Context
                   .getVectorType(ElementTy, NumElements, VT->getVectorKind())
                   .getTypePtr();
      }
    }

    if (Base->isVectorType() != TyPtr->isVectorType() ||
        Context.getTypeSize(Base) != Context.getTypeSize(TyPtr))
      return false;
  }

  return Members > 0 && Members <= 4;
}

static void diagnoseIfNeedsFPReg(DiagnosticsEngine &Diags,
                                 llvm::StringRef ABIName, CIRGenTypes &CGT,
                                 QualType Ty, const NamedDecl *D,
                                 SourceLocation Loc) {
  const Type *HABase = nullptr;
  uint64_t HAMembers = 0;
  if (Ty->isFloatingType() || Ty->isVectorType() ||
      isAArch64HomogeneousAggregate(CGT, Ty, HABase, HAMembers)) {
    Diags.Report(Loc, diag::err_target_unsupported_type_for_abi)
        << D->getDeclName() << Ty << ABIName;
  }
}

static bool wouldInliningViolateSMEFunctionCallABI(const FunctionDecl *Caller,
                                                   const FunctionDecl *Callee) {
  bool CallerIsStreaming =
      IsArmStreamingFunction(Caller, /*IncludeLocallyStreaming=*/true);
  bool CalleeIsStreaming =
      IsArmStreamingFunction(Callee, /*IncludeLocallyStreaming=*/true);
  bool CallerIsStreamingCompatible = isStreamingCompatible(Caller);
  bool CalleeIsStreamingCompatible = isStreamingCompatible(Callee);

  if (!CalleeIsStreamingCompatible &&
      (CallerIsStreaming != CalleeIsStreaming || CallerIsStreamingCompatible))
    return true;

  if (const auto *NewAttr = Callee->getAttr<ArmNewAttr>())
    return NewAttr->isNewZA() || NewAttr->isNewZT0();

  return false;
}

struct ArmSMEInlinability {
  bool errorCalleeRequiresNewZA = false;
  bool errorCalleeRequiresNewZT0 = false;
  bool warnIncompatibleStreamingModes = false;
  bool errorIncompatibleStreamingModes = false;

  bool hasAnyIssue() const {
    return errorCalleeRequiresNewZA || errorCalleeRequiresNewZT0 ||
           warnIncompatibleStreamingModes || errorIncompatibleStreamingModes;
  }

  bool hasIncompatibleStreamingModes() const {
    return warnIncompatibleStreamingModes || errorIncompatibleStreamingModes;
  }
};

static ArmSMEInlinability getArmSMEInlinability(const FunctionDecl *Caller,
                                                const FunctionDecl *Callee) {
  bool CallerIsStreaming =
      IsArmStreamingFunction(Caller, /*IncludeLocallyStreaming=*/true);
  bool CalleeIsStreaming =
      IsArmStreamingFunction(Callee, /*IncludeLocallyStreaming=*/true);
  bool CallerIsStreamingCompatible = isStreamingCompatible(Caller);
  bool CalleeIsStreamingCompatible = isStreamingCompatible(Callee);

  ArmSMEInlinability Inlinability;

  if (!CalleeIsStreamingCompatible &&
      (CallerIsStreaming != CalleeIsStreaming || CallerIsStreamingCompatible)) {
    if (CalleeIsStreaming)
      Inlinability.errorIncompatibleStreamingModes = true;
    else
      Inlinability.warnIncompatibleStreamingModes = true;
  }

  if (const auto *NewAttr = Callee->getAttr<ArmNewAttr>()) {
    Inlinability.errorCalleeRequiresNewZA = NewAttr->isNewZA();
    Inlinability.errorCalleeRequiresNewZT0 = NewAttr->isNewZT0();
  }

  return Inlinability;
}

// Apply x86 (32- and 64-bit shared) target attributes: interrupt calling
// convention and forced arg-pointer stack realignment. Mirrors classic
// X86_32/64TargetCodeGenInfo::setTargetAttributes for these two attrs.
static void setX86InterruptAndAlignAttrs(const clang::Decl *decl,
                                         mlir::Operation *global,
                                         CIRGenModule &cgm) {
  const auto *fd = clang::dyn_cast_or_null<clang::FunctionDecl>(decl);
  if (!fd)
    return;
  auto func = mlir::dyn_cast<cir::FuncOp>(global);
  if (!func || func.isDeclaration())
    return;

  if (fd->hasAttr<AnyX86InterruptAttr>()) {
    func.setCallingConv(cir::CallingConv::X86Intr);
    if (fd->getNumParams() == 0)
      return;
    auto ptrTy = cast<clang::PointerType>(fd->getParamDecl(0)->getType());
    mlir::Type byvalTy = cgm.getTypes().convertType(ptrTy->getPointeeType());
    func.setArgAttr(0, "cir.byval", mlir::TypeAttr::get(byvalTy));
  }

  // Match OGCG: X86ForceAlignArgPointerAttr adds "stackrealign".
  if (fd->hasAttr<X86ForceAlignArgPointerAttr>()) {
    mlir::NamedAttrList attrs;
    if (auto existingAttrs = func.getExtraAttrs())
      attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
    attrs.set("stackrealign", mlir::UnitAttr::get(func.getContext()));
    func.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(func.getContext())));
  }
}

class X8632ABIInfo : public ABIInfo {
public:
  X8632ABIInfo(CIRGenTypes &cgt) : ABIInfo(cgt) {}
};

// i386 target info. i386 previously fell through to the AArch64 CIRGen target
// class (whose methods no-op on x86), silently dropping the x86 interrupt
// calling convention (iret) and force_align_arg_pointer (stackrealign). This
// provides the x86 setTargetAttributes; it deliberately does NOT override
// checkFunctionCallABI (i386 passes vectors on the stack, so the x86_64
// AVX-ABI check must not fire) nor isNoProtoCallVariadic (i386 has no %al SSE
// convention).
class X8632TargetCIRGenInfo : public TargetCIRGenInfo {
public:
  X8632TargetCIRGenInfo(CIRGenTypes &cgt)
      : TargetCIRGenInfo(std::make_unique<X8632ABIInfo>(cgt)) {}

  void setTargetAttributes(const clang::Decl *decl, mlir::Operation *global,
                           CIRGenModule &cgm) const override {
    setX86InterruptAndAlignAttrs(decl, global, cgm);
  }
};

class X8664ABIInfo : public ABIInfo {
public:
  X8664ABIInfo(CIRGenTypes &cgt) : ABIInfo(cgt) {}
};

class X8664TargetCIRGenInfo : public TargetCIRGenInfo {
public:
  X8664TargetCIRGenInfo(CIRGenTypes &cgt)
      : TargetCIRGenInfo(std::make_unique<X8664ABIInfo>(cgt)) {}

  mlir::Type adjustInlineAsmType(CIRGenFunction &cgf,
                                 llvm::StringRef constraint,
                                 mlir::Type ty) const override {
    if (constraint == "k") {
      auto intTy = mlir::dyn_cast<cir::IntType>(ty);
      if (!intTy)
        return nullptr;
      return cir::VectorType::get(cgf.getBuilder().getSIntNTy(1),
                                  intTy.getWidth());
    }

    return TargetCIRGenInfo::adjustInlineAsmType(cgf, constraint, ty);
  }

  bool isNoProtoCallVariadic(
      const FunctionNoProtoType *fnType) const override {
    // On x86-64 with the C calling convention, unprototyped calls should
    // use the variadic convention so %al is set to the number of SSE args
    // (matching GCC/OGCG behavior).
    if (fnType->getCallConv() == CC_C)
      return true;
    return TargetCIRGenInfo::isNoProtoCallVariadic(fnType);
  }

  void setTargetAttributes(const clang::Decl *decl, mlir::Operation *global,
                           CIRGenModule &cgm) const override {
    const auto *fd = clang::dyn_cast_or_null<clang::FunctionDecl>(decl);
    if (!fd)
      return;
    auto func = mlir::dyn_cast<cir::FuncOp>(global);
    if (!func || func.isDeclaration())
      return;

    if (fd->hasAttr<AnyX86InterruptAttr>()) {
      func.setCallingConv(cir::CallingConv::X86Intr);
      if (fd->getNumParams() == 0)
        return;

      auto ptrTy =
          cast<clang::PointerType>(fd->getParamDecl(0)->getType());
      mlir::Type byvalTy =
          cgm.getTypes().convertType(ptrTy->getPointeeType());
      func.setArgAttr(0, "cir.byval", mlir::TypeAttr::get(byvalTy));
    }

    // Match OGCG: X86ForceAlignArgPointerAttr adds "stackrealign".
    if (fd->hasAttr<X86ForceAlignArgPointerAttr>()) {
      mlir::NamedAttrList attrs;
      if (auto existingAttrs = func.getExtraAttrs())
        attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
      attrs.set("stackrealign",
                mlir::UnitAttr::get(func.getContext()));
      func.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
          attrs.getDictionary(func.getContext())));
    }
  }

  // Diagnose calls that pass/return an AVX vector across a target-feature
  // boundary that changes the ABI (mirrors classic X86_64TargetCodeGenInfo::
  // checkFunctionCallABI). Without this, clangir silently miscompiles such
  // calls (e.g. a baseline caller passing a 256-bit vector as xmm-pairs to a
  // `target("avx2")` callee that reads it from ymm).
  void checkFunctionCallABI(CIRGenModule &cgm, clang::SourceLocation callLoc,
                            const clang::FunctionDecl *caller,
                            const clang::FunctionDecl *callee,
                            const CallArgList &args,
                            clang::QualType returnType) const override;
};

// Lazily populate caller/callee target-feature maps. Mirrors the classic
// initFeatureMaps in CodeGen/Targets/X86.cpp.
static void initX86FeatureMaps(const clang::ASTContext &ctx,
                               llvm::StringMap<bool> &callerMap,
                               const clang::FunctionDecl *caller,
                               llvm::StringMap<bool> &calleeMap,
                               const clang::FunctionDecl *callee) {
  if (calleeMap.empty() && callerMap.empty()) {
    // The caller may be null when the call isn't in a function; then
    // getFunctionFeatureMap yields the TU-level setting.
    ctx.getFunctionFeatureMap(callerMap, caller);
    ctx.getFunctionFeatureMap(calleeMap, callee);
  }
}

static bool checkAVXParamFeature(clang::DiagnosticsEngine &diag,
                                 clang::SourceLocation callLoc,
                                 const clang::FunctionDecl &callee,
                                 const llvm::StringMap<bool> &callerMap,
                                 const llvm::StringMap<bool> &calleeMap,
                                 clang::QualType ty, llvm::StringRef feature,
                                 bool isArgument) {
  bool callerHasFeat = callerMap.lookup(feature);
  bool calleeHasFeat = calleeMap.lookup(feature);
  // No explicit features and the callee is internal: be permissive.
  if (!callerHasFeat && !calleeHasFeat &&
      (!callee.isExternallyVisible() ||
       callee.hasAttr<clang::AlwaysInlineAttr>()))
    return false;

  if (!callerHasFeat && !calleeHasFeat)
    return diag.Report(callLoc, diag::warn_avx_calling_convention)
           << isArgument << ty << feature;

  // Exactly one side has the feature: a clear ABI mismatch.
  if (!callerHasFeat || !calleeHasFeat)
    return diag.Report(callLoc, diag::err_avx_calling_convention)
           << isArgument << ty << feature;

  return false;
}

static bool checkAVXParam(clang::DiagnosticsEngine &diag,
                          clang::ASTContext &ctx, clang::SourceLocation callLoc,
                          const clang::FunctionDecl &callee,
                          const llvm::StringMap<bool> &callerMap,
                          const llvm::StringMap<bool> &calleeMap,
                          clang::QualType ty, bool isArgument) {
  uint64_t size = ctx.getTypeSize(ty);
  if (size > 256)
    return checkAVXParamFeature(diag, callLoc, callee, callerMap, calleeMap, ty,
                                "avx512f", isArgument);
  if (size > 128)
    return checkAVXParamFeature(diag, callLoc, callee, callerMap, calleeMap, ty,
                                "avx", isArgument);
  return false;
}

void X8664TargetCIRGenInfo::checkFunctionCallABI(
    CIRGenModule &cgm, clang::SourceLocation callLoc,
    const clang::FunctionDecl *caller, const clang::FunctionDecl *callee,
    const CallArgList &args, clang::QualType returnType) const {
  if (!callee)
    return;

  clang::ASTContext &ctx = getABIInfo().cgt.getASTContext();
  llvm::StringMap<bool> callerMap;
  llvm::StringMap<bool> calleeMap;
  unsigned argIndex = 0;

  // Loop over the actual call arguments (not params) to handle variadics. The
  // "avx"/"avx512f" features change how >128/>256-bit vectors are passed.
  for (const CallArg &arg : args) {
    if (arg.ty->isVectorType() && ctx.getTypeSize(arg.ty) > 128) {
      initX86FeatureMaps(ctx, callerMap, caller, calleeMap, callee);
      clang::QualType ty = arg.ty;
      // Prefer the declared param type for a clearer diagnostic.
      if (argIndex < callee->getNumParams())
        ty = callee->getParamDecl(argIndex)->getType();

      if (checkAVXParam(cgm.getDiags(), ctx, callLoc, *callee, callerMap,
                        calleeMap, ty, /*isArgument=*/true))
        return;
    }
    ++argIndex;
  }

  // Always check the return type; codegen can't know whether it's used.
  if (callee->getReturnType()->isVectorType() &&
      ctx.getTypeSize(callee->getReturnType()) > 128) {
    initX86FeatureMaps(ctx, callerMap, caller, calleeMap, callee);
    checkAVXParam(cgm.getDiags(), ctx, callLoc, *callee, callerMap, calleeMap,
                  callee->getReturnType(), /*isArgument=*/false);
  }
}

} // namespace

std::unique_ptr<TargetCIRGenInfo>
clang::CIRGen::createX8632TargetCIRGenInfo(CIRGenTypes &cgt) {
  return std::make_unique<X8632TargetCIRGenInfo>(cgt);
}

std::unique_ptr<TargetCIRGenInfo>
clang::CIRGen::createX8664TargetCIRGenInfo(CIRGenTypes &cgt) {
  return std::make_unique<X8664TargetCIRGenInfo>(cgt);
}

//===----------------------------------------------------------------------===//
// ARM ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class ARMABIInfo : public ABIInfo {
public:
  ARMABIInfo(CIRGenTypes &cgt) : ABIInfo(cgt) {}
};

class ARMTargetCIRGenInfo : public TargetCIRGenInfo {
public:
  ARMTargetCIRGenInfo(CIRGenTypes &cgt)
      : TargetCIRGenInfo(std::make_unique<ARMABIInfo>(cgt)) {}

  cir::CallingConv getRuntimeCallingConv(CIRGenModule &cgm) const override {
    const llvm::Triple &triple = cgm.getTarget().getTriple();
    StringRef abi = cgm.getTarget().getABI();

    auto isEABI = [&]() {
      switch (triple.getEnvironment()) {
      case llvm::Triple::Android:
      case llvm::Triple::EABI:
      case llvm::Triple::EABIHF:
      case llvm::Triple::GNUEABI:
      case llvm::Triple::GNUEABIT64:
      case llvm::Triple::GNUEABIHF:
      case llvm::Triple::GNUEABIHFT64:
      case llvm::Triple::MuslEABI:
      case llvm::Triple::MuslEABIHF:
        return true;
      default:
        return triple.isOHOSFamily();
      }
    };
    auto isEABIHF = [&]() {
      switch (triple.getEnvironment()) {
      case llvm::Triple::EABIHF:
      case llvm::Triple::GNUEABIHF:
      case llvm::Triple::GNUEABIHFT64:
      case llvm::Triple::MuslEABIHF:
        return true;
      default:
        return false;
      }
    };

    // Match OGCG's ARM RuntimeCC behavior. APCS is not modeled in CIR yet, so
    // targets that want APCS stay at the default C calling convention.
    if (abi == "apcs-gnu")
      return cir::CallingConv::C;

    cir::CallingConv abiCC = cir::CallingConv::AAPCS;
    if (triple.getOS() == llvm::Triple::Win32 || abi == "aapcs16" ||
        cgm.getCodeGenOpts().FloatABI == "hard" ||
        (cgm.getCodeGenOpts().FloatABI != "soft" && triple.isHardFloatABI()))
      abiCC = cir::CallingConv::AAPCS_VFP;

    cir::CallingConv llvmDefaultCC = cir::CallingConv::C;
    if (isEABIHF() || triple.isWatchABI())
      llvmDefaultCC = cir::CallingConv::AAPCS_VFP;
    else if (isEABI())
      llvmDefaultCC = cir::CallingConv::AAPCS;

    if (abiCC != llvmDefaultCC)
      return abiCC;
    return cir::CallingConv::C;
  }

  // Apply ARM __attribute__((interrupt(...))) — emits the "interrupt"="<mode>"
  // function attribute (+ "save-fp") that the ARM backend uses to generate the
  // correct interrupt return sequence (subs pc, lr, #N) and register saves.
  // Mirrors classic ARMTargetCodeGenInfo::setTargetAttributes; without it
  // clangir silently dropped the attribute → a broken ARM interrupt handler
  // (ordinary function return instead of the interrupt return).
  void setTargetAttributes(const clang::Decl *decl, mlir::Operation *global,
                           CIRGenModule &cgm) const override {
    const auto *fd = clang::dyn_cast_or_null<clang::FunctionDecl>(decl);
    if (!fd)
      return;
    auto func = mlir::dyn_cast<cir::FuncOp>(global);
    if (!func || func.isDeclaration())
      return;
    const auto *attr = fd->getAttr<clang::ARMInterruptAttr>();
    if (!attr)
      return;

    const char *kind;
    switch (attr->getInterrupt()) {
    case clang::ARMInterruptAttr::Generic: kind = ""; break;
    case clang::ARMInterruptAttr::IRQ:     kind = "IRQ"; break;
    case clang::ARMInterruptAttr::FIQ:     kind = "FIQ"; break;
    case clang::ARMInterruptAttr::SWI:     kind = "SWI"; break;
    case clang::ARMInterruptAttr::ABORT:   kind = "ABORT"; break;
    case clang::ARMInterruptAttr::UNDEF:   kind = "UNDEF"; break;
    }

    mlir::NamedAttrList attrs;
    if (auto existingAttrs = func.getExtraAttrs())
      attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
    attrs.set("interrupt", mlir::StringAttr::get(func.getContext(), kind));
    // ARMSaveFPAttr can only exist alongside an interrupt attribute.
    if (fd->hasAttr<clang::ARMSaveFPAttr>())
      attrs.set("save-fp", mlir::UnitAttr::get(func.getContext()));
    func.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(func.getContext())));
  }
};

} // namespace

std::unique_ptr<TargetCIRGenInfo>
clang::CIRGen::createARMTargetCIRGenInfo(CIRGenTypes &cgt) {
  return std::make_unique<ARMTargetCIRGenInfo>(cgt);
}

//===----------------------------------------------------------------------===//
// AArch64 ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class AArch64ABIInfo : public ABIInfo {
public:
  AArch64ABIInfo(CIRGenTypes &cgt) : ABIInfo(cgt) {}
};

class AArch64TargetCIRGenInfo : public TargetCIRGenInfo {
public:
  AArch64TargetCIRGenInfo(CIRGenTypes &cgt)
      : TargetCIRGenInfo(std::make_unique<AArch64ABIInfo>(cgt)) {}

  bool isScalarizableAsmOperand(CIRGenFunction &cgf,
                                mlir::Type ty) const override {
    if (cgf.getTarget().hasFeature("ls64")) {
      auto recordTy = mlir::dyn_cast<cir::RecordType>(ty);
      if (recordTy && recordTy.getNumElements() == 1) {
        auto arrayTy =
            mlir::dyn_cast<cir::ArrayType>(recordTy.getMembers()[0]);
        auto elemTy = arrayTy ? mlir::dyn_cast<cir::IntType>(
                                    arrayTy.getElementType())
                              : cir::IntType();
        if (arrayTy && elemTy && arrayTy.getSize() == 8 &&
            elemTy.getWidth() == 64)
          return true;
      }
    }
    return TargetCIRGenInfo::isScalarizableAsmOperand(cgf, ty);
  }

  llvm::StringRef
  getARCRetainAutoreleasedReturnValueMarker() const override {
    return "mov\tfp, fp\t\t// marker for objc_retainAutoreleaseReturnValue";
  }

  bool doesReturnSlotInterfereWithArgs() const override { return false; }

  bool wouldInliningViolateFunctionCallABI(
      const FunctionDecl *Caller, const FunctionDecl *Callee) const override {
    return Caller && Callee &&
           wouldInliningViolateSMEFunctionCallABI(Caller, Callee);
  }

  void checkFunctionABI(CIRGenModule &CGM,
                        const FunctionDecl *FD) const override;

  void checkFunctionCallABI(CIRGenModule &CGM, SourceLocation CallSiteLoc,
                            const FunctionDecl *Caller,
                            const FunctionDecl *Callee,
                            const CallArgList &Args,
                            QualType ReturnType) const override;

  void setTargetAttributes(const clang::Decl *decl, mlir::Operation *global,
                           CIRGenModule &cgm) const override {
    auto func = mlir::dyn_cast<cir::FuncOp>(global);
    if (!func)
      return;

    const auto *fd = clang::dyn_cast_or_null<clang::FunctionDecl>(decl);

    // Branch protection: start from global LangOpts defaults, then
    // override with per-function __attribute__((target("branch-protection=...")))
    // if present.  Mirrors OGCG AArch64TargetCodeGenInfo::setTargetAttributes.
    TargetInfo::BranchProtectionInfo BPI(cgm.getLangOpts());

    if (fd && fd->hasAttr<TargetAttr>()) {
      const auto *ta = fd->getAttr<TargetAttr>();
      ParsedTargetAttr attr =
          cgm.getTarget().parseTargetAttr(ta->getFeaturesStr());
      if (!attr.BranchProtection.empty()) {
        StringRef error;
        (void)cgm.getTarget().validateBranchProtection(
            attr.BranchProtection, attr.CPU, BPI, cgm.getLangOpts(), error);
        assert(error.empty());
      }
    }

    // Apply branch protection attrs — mirrors setBranchProtectionFnAttributes.
    // Attributes may need to be REMOVED (not just added) because defaults from
    // getDefaultFunctionAttributes may not match the per-function BPI.
    mlir::NamedAttrList attrs;
    if (auto existingAttrs = func.getExtraAttrs())
      attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
    auto *ctx = func.getContext();

    if (BPI.SignReturnAddr !=
        LangOptions::SignReturnAddressScopeKind::None) {
      attrs.set("sign-return-address",
                cgm.getStringAttrCached(BPI.getSignReturnAddrStr()));
      attrs.set("sign-return-address-key",
                cgm.getStringAttrCached(BPI.getSignKeyStr()));
    } else {
      attrs.erase("sign-return-address");
      attrs.erase("sign-return-address-key");
    }

    auto addRemoveAttr = [&](bool set, llvm::StringRef attrName) {
      if (set)
        attrs.set(attrName, mlir::UnitAttr::get(ctx));
      else
        attrs.erase(attrName);
    };
    addRemoveAttr(BPI.BranchTargetEnforcement, "branch-target-enforcement");
    addRemoveAttr(BPI.BranchProtectionPAuthLR, "branch-protection-pauth-lr");
    addRemoveAttr(BPI.GuardedControlStack, "guarded-control-stack");
    const auto &PAO = cgm.getCodeGenOpts().PointerAuth;
    addRemoveAttr(PAO.ReturnAddresses, "ptrauth-returns");
    addRemoveAttr(static_cast<bool>(PAO.FunctionPointers), "ptrauth-calls");
    addRemoveAttr(PAO.AuthTraps, "ptrauth-auth-traps");
    addRemoveAttr(PAO.IndirectGotos, "ptrauth-indirect-gotos");
    addRemoveAttr(PAO.AArch64JumpTableHardening,
                  "aarch64-jump-table-hardening");

    func.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(func.getContext())));
  }

private:
  void checkFunctionCallABIStreaming(CIRGenModule &CGM,
                                     SourceLocation CallLoc,
                                     const FunctionDecl *Caller,
                                     const FunctionDecl *Callee) const;

  void checkFunctionCallABISoftFloat(CIRGenModule &CGM,
                                     SourceLocation CallSiteLoc,
                                     const FunctionDecl *Caller,
                                     const FunctionDecl *Callee,
                                     const CallArgList &Args,
                                     QualType ReturnType) const;
};

void AArch64TargetCIRGenInfo::checkFunctionABI(CIRGenModule &CGM,
                                               const FunctionDecl *FD) const {
  CIRGenTypes &CGT = getABIInfo().cgt;
  const TargetInfo &TI = CGT.getASTContext().getTargetInfo();

  if (!isActualAArch64Target(TI) || TI.hasFeature("fp") ||
      isAArch64SoftFloatABI(TI))
    return;

  diagnoseIfNeedsFPReg(CGM.getDiags(), TI.getABI(), CGT, FD->getReturnType(),
                       FD, FD->getLocation());
  for (ParmVarDecl *PVD : FD->parameters())
    diagnoseIfNeedsFPReg(CGM.getDiags(), TI.getABI(), CGT, PVD->getType(), PVD,
                         FD->getLocation());
}

void AArch64TargetCIRGenInfo::checkFunctionCallABIStreaming(
    CIRGenModule &CGM, SourceLocation CallSiteLoc, const FunctionDecl *Caller,
    const FunctionDecl *Callee) const {
  if (!Caller || !Callee || !Callee->hasAttr<AlwaysInlineAttr>())
    return;

  ArmSMEInlinability Inlinability = getArmSMEInlinability(Caller, Callee);

  if (Inlinability.hasIncompatibleStreamingModes())
    CGM.getDiags().Report(
        CallSiteLoc, Inlinability.errorIncompatibleStreamingModes
                     ? diag::err_function_always_inline_attribute_mismatch
                     : diag::warn_function_always_inline_attribute_mismatch)
        << Caller->getDeclName() << Callee->getDeclName() << "streaming";

  if (Inlinability.errorCalleeRequiresNewZA)
    CGM.getDiags().Report(CallSiteLoc, diag::err_function_always_inline_new_za)
        << Callee->getDeclName();

  if (Inlinability.errorCalleeRequiresNewZT0)
    CGM.getDiags().Report(CallSiteLoc, diag::err_function_always_inline_new_zt0)
        << Callee->getDeclName();
}

void AArch64TargetCIRGenInfo::checkFunctionCallABISoftFloat(
    CIRGenModule &CGM, SourceLocation CallSiteLoc, const FunctionDecl *Caller,
    const FunctionDecl *Callee, const CallArgList &Args,
    QualType ReturnType) const {
  CIRGenTypes &CGT = getABIInfo().cgt;
  const TargetInfo &TI = CGT.getASTContext().getTargetInfo();

  if (!Caller || !isActualAArch64Target(TI) || TI.hasFeature("fp") ||
      isAArch64SoftFloatABI(TI))
    return;

  const NamedDecl *DiagDecl = Callee ? static_cast<const NamedDecl *>(Callee)
                                     : static_cast<const NamedDecl *>(Caller);
  diagnoseIfNeedsFPReg(CGM.getDiags(), TI.getABI(), CGT, ReturnType, DiagDecl,
                       CallSiteLoc);

  for (const CallArg &Arg : Args)
    diagnoseIfNeedsFPReg(CGM.getDiags(), TI.getABI(), CGT, Arg.ty,
                         DiagDecl, CallSiteLoc);
}

void AArch64TargetCIRGenInfo::checkFunctionCallABI(
    CIRGenModule &CGM, SourceLocation CallSiteLoc, const FunctionDecl *Caller,
    const FunctionDecl *Callee, const CallArgList &Args,
    QualType ReturnType) const {
  checkFunctionCallABIStreaming(CGM, CallSiteLoc, Caller, Callee);
  checkFunctionCallABISoftFloat(CGM, CallSiteLoc, Caller, Callee, Args,
                                ReturnType);
}

} // namespace

std::unique_ptr<TargetCIRGenInfo>
clang::CIRGen::createAArch64TargetCIRGenInfo(CIRGenTypes &cgt) {
  return std::make_unique<AArch64TargetCIRGenInfo>(cgt);
}

//===----------------------------------------------------------------------===//
// NVPTX ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class NVPTXABIInfo : public ABIInfo {
public:
  NVPTXABIInfo(CIRGenTypes &cgt) : ABIInfo(cgt) {}
};

class NVPTXTargetCIRGenInfo : public TargetCIRGenInfo {
public:
  NVPTXTargetCIRGenInfo(CIRGenTypes &cgt)
      : TargetCIRGenInfo(std::make_unique<NVPTXABIInfo>(cgt)) {}

  void setCUDAKernelCallingConvention(const FunctionType *&ft) const override {
    ft = getABIInfo().cgt.getASTContext().adjustFunctionType(
        ft, ft->getExtInfo().withCallingConv(CC_DeviceKernel));
  }

  cir::CallingConv getOpenCLKernelCallingConv() const override {
    return cir::CallingConv::PTXKernel;
  }

  mlir::Type getCUDADeviceBuiltinSurfaceDeviceType() const override {
    // On the device side, surface reference is represented as an object handle
    // in 64-bit integer.
    return cir::IntType::get(&getABIInfo().cgt.getMLIRContext(), 64,
                             /*isSigned=*/true);
  }

  mlir::Type getCUDADeviceBuiltinTextureDeviceType() const override {
    // On the device side, texture reference is represented as an object handle
    // in 64-bit integer.
    return cir::IntType::get(&getABIInfo().cgt.getMLIRContext(), 64,
                             /*isSigned=*/true);
  }

  void setTargetAttributes(const clang::Decl *decl, mlir::Operation *global,
                           CIRGenModule &cgm) const override {
    if (const auto *vd = clang::dyn_cast_or_null<clang::VarDecl>(decl)) {
      if (cgm.getLangOpts().CUDA &&
          (vd->getType()->isCUDADeviceBuiltinSurfaceType() ||
           vd->getType()->isCUDADeviceBuiltinTextureType())) {
        assert(!cir::MissingFeatures::cudaSurfaceTextureNVVMMetadata());
        return;
      }
    }
    if (const auto *fd = clang::dyn_cast_or_null<clang::FunctionDecl>(decl)) {
      cir::FuncOp func = mlir::dyn_cast<cir::FuncOp>(global);
      if (!func || func.isDeclaration())
        return;
      if (cgm.getLangOpts().CUDA) {
        if (fd->hasAttr<CUDAGlobalAttr>()) {
          func.setCallingConv(cir::CallingConv::PTXKernel);
          // __launch_bounds__ → nvvm.maxntid / minctasm / maxclusterrank fn
          // attrs (driver-enforced block-size limits + register-allocation
          // occupancy hints). Mirrors classic
          // CodeGenModule::handleCUDALaunchBoundsAttr; without this clangir
          // silently dropped the attribute (no .maxntid/.minnctapersm in PTX).
          if (const auto *lb = fd->getAttr<clang::CUDALaunchBoundsAttr>())
            emitNVVMLaunchBounds(func, lb, cgm);
        }
      }
    }
  }

private:
  static void emitNVVMLaunchBounds(cir::FuncOp func,
                                   const clang::CUDALaunchBoundsAttr *attr,
                                   CIRGenModule &cgm) {
    clang::ASTContext &ctx = cgm.getASTContext();
    mlir::NamedAttrList attrs;
    if (auto existingAttrs = func.getExtraAttrs())
      attrs = mlir::NamedAttrList{existingAttrs->getElements().getValue()};
    auto setStr = [&](llvm::StringRef name, int64_t v) {
      attrs.set(name, mlir::StringAttr::get(func.getContext(),
                                            llvm::utostr((uint64_t)v)));
    };

    llvm::APSInt maxThreads(32);
    maxThreads = attr->getMaxThreads()->EvaluateKnownConstInt(ctx);
    if (maxThreads > 0)
      setStr("nvvm.maxntid", maxThreads.getExtValue());

    if (attr->getMinBlocks()) {
      llvm::APSInt minBlocks(32);
      minBlocks = attr->getMinBlocks()->EvaluateKnownConstInt(ctx);
      if (minBlocks > 0)
        setStr("nvvm.minctasm", minBlocks.getExtValue());
    }
    if (attr->getMaxBlocks()) {
      llvm::APSInt maxBlocks(32);
      maxBlocks = attr->getMaxBlocks()->EvaluateKnownConstInt(ctx);
      if (maxBlocks > 0)
        setStr("nvvm.maxclusterrank", maxBlocks.getExtValue());
    }

    func.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
        attrs.getDictionary(func.getContext())));
  }
};

} // namespace

std::unique_ptr<TargetCIRGenInfo>
clang::CIRGen::createNVPTXTargetCIRGenInfo(CIRGenTypes &cgt) {
  return std::make_unique<NVPTXTargetCIRGenInfo>(cgt);
}

//===----------------------------------------------------------------------===//
// AMDGPU ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class AMDGPUABIInfo : public ABIInfo {
public:
  AMDGPUABIInfo(CIRGenTypes &cgt) : ABIInfo(cgt) {}
};

class AMDGPUTargetCIRGenInfo : public TargetCIRGenInfo {
public:
  AMDGPUTargetCIRGenInfo(CIRGenTypes &cgt)
      : TargetCIRGenInfo(std::make_unique<AMDGPUABIInfo>(cgt)) {}

  void setCUDAKernelCallingConvention(const FunctionType *&ft) const override {
    ft = getABIInfo().cgt.getASTContext().adjustFunctionType(
        ft, ft->getExtInfo().withCallingConv(CC_DeviceKernel));
  }

  cir::CallingConv getOpenCLKernelCallingConv() const override {
    return cir::CallingConv::AMDGPUKernel;
  }

  mlir::ptr::MemorySpaceAttrInterface
  getCIRAllocaAddressSpace() const override {
    return cir::LangAddressSpaceAttr::get(
        &getABIInfo().cgt.getMLIRContext(),
        cir::LangAddressSpace::OffloadPrivate);
  }

  LangAS getGlobalVarAddressSpace(CIRGenModule &cgm,
                                  const VarDecl *d) const override {
    assert(!cgm.getLangOpts().OpenCL &&
           !(cgm.getLangOpts().CUDA && cgm.getLangOpts().CUDAIsDevice) &&
           "Address space agnostic languages only");
    LangAS defaultGlobalAS = getLangASFromTargetAS(
        cgm.getASTContext().getTargetAddressSpace(LangAS::opencl_global));
    if (!d)
      return defaultGlobalAS;
    LangAS addrSpace = d->getType().getAddressSpace();
    if (addrSpace != LangAS::Default)
      return addrSpace;
    if (d->getType().isConstantStorage(cgm.getASTContext(), false, false) &&
        d->hasConstantInitialization()) {
      if (auto constAS = cgm.getTarget().getConstantAddressSpace())
        return *constAS;
    }
    return defaultGlobalAS;
  }

  void setTargetAttributes(const clang::Decl *decl, mlir::Operation *global,
                           CIRGenModule &cgm) const override {
    if (const auto *fd = clang::dyn_cast_or_null<clang::FunctionDecl>(decl)) {
      cir::FuncOp func = mlir::dyn_cast<cir::FuncOp>(global);
      if (!func || func.isDeclaration())
        return;
      if (cgm.getLangOpts().CUDA || cgm.getLangOpts().HIP) {
        if (fd->hasAttr<CUDAGlobalAttr>())
          func.setCallingConv(cir::CallingConv::AMDGPUKernel);
      }
      // OpenCL kernel CC is set via getOpenCLKernelCallingConv() through
      // the function info CC flow (constructAttributeList →
      // setCIRFunctionAttributes). Not overridden here so that stubs
      // (which adjust CC to C in arrangeFunctionDeclaration) keep C CC.

      const bool isOpenCLKernel =
          cgm.getLangOpts().OpenCL && fd->hasAttr<DeviceKernelAttr>();
      const bool isHIPKernel =
          cgm.getLangOpts().HIP && fd->hasAttr<CUDAGlobalAttr>();

      // Mirror OGCG AMDGPU kernel string attributes from
      // Targets/AMDGPU.cpp:setFunctionDeclAttributes.
      mlir::NamedAttrList attrs;
      if (auto extraAttrs = func.getExtraAttrs())
        attrs = mlir::NamedAttrList{extraAttrs->getElements().getValue()};
      auto setStrAttr = [&](llvm::StringRef key, const std::string &value) {
        attrs.set(key, cgm.getStringAttrCached(value));
      };
      auto evalConstExpr = [&](const Expr *e) -> uint32_t {
        return e->EvaluateKnownConstInt(cgm.getASTContext()).getExtValue();
      };

      const auto *reqdWGS = cgm.getLangOpts().OpenCL
                                ? fd->getAttr<ReqdWorkGroupSizeAttr>()
                                : nullptr;
      if (const auto *flatWGS = fd->getAttr<AMDGPUFlatWorkGroupSizeAttr>()) {
        uint32_t min = evalConstExpr(flatWGS->getMin());
        uint32_t max = evalConstExpr(flatWGS->getMax());
        if (min != 0)
          setStrAttr("amdgpu-flat-work-group-size",
                     std::to_string(min) + "," + std::to_string(max));
      } else if (reqdWGS) {
        // reqd_work_group_size sets exact size: min=max=product(x*y*z).
        unsigned size = evalConstExpr(reqdWGS->getXDim()) *
                        evalConstExpr(reqdWGS->getYDim()) *
                        evalConstExpr(reqdWGS->getZDim());
        if (size != 0)
          setStrAttr("amdgpu-flat-work-group-size",
                     std::to_string(size) + "," + std::to_string(size));
      } else if (isOpenCLKernel || isHIPKernel) {
        // Match OGCG defaults:
        // - OpenCL kernel: 1,256
        // - HIP kernel: 1,<--gpu-max-threads-per-block> (default 1024)
        const unsigned openCLDefaultMaxWGSize = 256;
        const unsigned defaultMaxWGSize =
            isOpenCLKernel ? openCLDefaultMaxWGSize
                           : cgm.getLangOpts().GPUMaxThreadsPerBlock;
        setStrAttr("amdgpu-flat-work-group-size",
                   "1," + std::to_string(defaultMaxWGSize));
      }

      if (const auto *waves = fd->getAttr<AMDGPUWavesPerEUAttr>()) {
        uint32_t min = evalConstExpr(waves->getMin());
        uint32_t max = waves->getMax() ? evalConstExpr(waves->getMax()) : 0;
        if (min != 0) {
          std::string value = std::to_string(min);
          if (max != 0)
            value += "," + std::to_string(max);
          setStrAttr("amdgpu-waves-per-eu", value);
        }
      }

      if (const auto *numSgpr = fd->getAttr<AMDGPUNumSGPRAttr>()) {
        unsigned value = numSgpr->getNumSGPR();
        if (value != 0)
          setStrAttr("amdgpu-num-sgpr", std::to_string(value));
      }

      if (const auto *numVgpr = fd->getAttr<AMDGPUNumVGPRAttr>()) {
        uint32_t value = numVgpr->getNumVGPR();
        if (value != 0)
          setStrAttr("amdgpu-num-vgpr", std::to_string(value));
      }

      if (const auto *maxWGs = fd->getAttr<AMDGPUMaxNumWorkGroupsAttr>()) {
        uint32_t x = evalConstExpr(maxWGs->getMaxNumWorkGroupsX());
        // Y and Z dimensions default to 1 when not specified.
        uint32_t y = maxWGs->getMaxNumWorkGroupsY()
                         ? evalConstExpr(maxWGs->getMaxNumWorkGroupsY())
                         : 1;
        uint32_t z = maxWGs->getMaxNumWorkGroupsZ()
                         ? evalConstExpr(maxWGs->getMaxNumWorkGroupsZ())
                         : 1;
        setStrAttr("amdgpu-max-num-workgroups", std::to_string(x) + "," +
                                                    std::to_string(y) + "," +
                                                    std::to_string(z));
      }

      // HIP kernels with offload-uniform-block use uniform workgroups.
      if (isHIPKernel && cgm.getLangOpts().OffloadUniformBlock)
        setStrAttr("uniform-work-group-size", "true");

      if (!attrs.empty())
        func.setExtraAttrsAttr(cir::ExtraFuncAttributesAttr::get(
            attrs.getDictionary(func.getContext())));

      // AMDGPU kernels with hidden visibility should use protected visibility
      // instead, so they can be referenced by the host.
      if (func.getGlobalVisibility() == cir::VisibilityKind::Hidden) {
        if (decl->hasAttr<CUDAGlobalAttr>() ||
            decl->hasAttr<DeviceKernelAttr>())
          func.setGlobalVisibility(cir::VisibilityKind::Protected);
      }
    }
  }
};

} // namespace

std::unique_ptr<TargetCIRGenInfo>
clang::CIRGen::createAMDGPUTargetCIRGenInfo(CIRGenTypes &cgt) {
  return std::make_unique<AMDGPUTargetCIRGenInfo>(cgt);
}

ABIInfo::~ABIInfo() noexcept = default;

bool TargetCIRGenInfo::isNoProtoCallVariadic(
    const FunctionNoProtoType *fnType) const {
  // The following conventions are known to require this to be false:
  //   x86_stdcall
  //   MIPS
  // For everything else, we just prefer false unless we opt out.
  return false;
}

mlir::Value TargetCIRGenInfo::performAddrSpaceCast(
    CIRGenFunction &cgf, mlir::Value v,
    mlir::ptr::MemorySpaceAttrInterface srcAddr, mlir::Type destTy,
    bool isNonNull) const {
  // Since target may map different address spaces in AST to the same address
  // space, an address space conversion may end up as a bitcast.
  if (cir::GlobalOp globalOp = v.getDefiningOp<cir::GlobalOp>()) {
    cgf.cgm.errorNYI("Global op addrspace cast");
    return v;
  }
  // Try to preserve the source's name to make IR more readable.
  return cgf.getBuilder().createAddrSpaceCast(v, destTy);
}

namespace {

class SPIRVABIInfo : public ABIInfo {
public:
  SPIRVABIInfo(CIRGenTypes &cgt) : ABIInfo(cgt) {}
};

class SPIRVTargetCIRGenInfo : public TargetCIRGenInfo {
public:
  SPIRVTargetCIRGenInfo(CIRGenTypes &cgt)
      : TargetCIRGenInfo(std::make_unique<SPIRVABIInfo>(cgt)) {}

  mlir::ptr::MemorySpaceAttrInterface
  getCIRAllocaAddressSpace() const override {
    // In OpenCL/SPIR-V, alloca is in the private address space.
    return cir::LangAddressSpaceAttr::get(
        &getABIInfo().cgt.getMLIRContext(),
        cir::LangAddressSpace::OffloadPrivate);
  }

  cir::CallingConv getOpenCLKernelCallingConv() const override {
    return cir::CallingConv::SpirKernel;
  }

  void
  setOCLKernelStubCallingConvention(const FunctionType *&ft) const override {
    ft = getABIInfo().cgt.getASTContext().adjustFunctionType(
        ft, ft->getExtInfo().withCallingConv(CC_SpirFunction));
  }

  LangAS getGlobalVarAddressSpace(CIRGenModule &cgm,
                                  const VarDecl *d) const override {
    assert(!cgm.getLangOpts().OpenCL &&
           !(cgm.getLangOpts().CUDA && cgm.getLangOpts().CUDAIsDevice) &&
           "Address space agnostic languages only");
    LangAS defaultGlobalAS = getLangASFromTargetAS(
        cgm.getASTContext().getTargetAddressSpace(LangAS::sycl_global));
    if (!d)
      return defaultGlobalAS;

    LangAS addrSpace = d->getType().getAddressSpace();
    if (addrSpace != LangAS::Default)
      return addrSpace;

    return defaultGlobalAS;
  }
};

} // namespace

std::unique_ptr<TargetCIRGenInfo>
clang::CIRGen::createSPIRVTargetCIRGenInfo(CIRGenTypes &cgt) {
  return std::make_unique<SPIRVTargetCIRGenInfo>(cgt);
}

LangAS TargetCIRGenInfo::getGlobalVarAddressSpace(CIRGenModule &cgm,
                                                  const VarDecl *d) const {
  assert(!cgm.getLangOpts().OpenCL &&
         !(cgm.getLangOpts().CUDA && cgm.getLangOpts().CUDAIsDevice) &&
         "Address space agnostic languages only");
  return d ? d->getType().getAddressSpace() : LangAS::Default;
}
