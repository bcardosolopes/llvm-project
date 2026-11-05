
#include "clang/CIR/Target/x86.h"
#include "ABIInfo.h"
#include "ABIInfoImpl.h"
#include "LowerModule.h"
#include "LowerTypes.h"
#include "SwiftCallingConv.h"
#include "TargetInfo.h"
#include "X86_64ABIInfo.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecordLayout.h"
#include "clang/CIR/ABIArgInfo.h"
#include "clang/CIR/Dialect/IR/CIRDataLayout.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ErrorHandling.h"
#include <memory>

using X86AVXABILevel = cir::X86AVXABILevel;
using ABIArgInfo = cir::ABIArgInfo;

namespace cir {

namespace {

static bool BitsContainNoUserData(clang::QualType Ty, unsigned StartBit,
                                  unsigned EndBit,
                                  clang::ASTContext &ASTContext) {
  unsigned TySize = (unsigned)ASTContext.getTypeSize(Ty);
  if (TySize <= StartBit)
    return true;

  if (const auto *AT = ASTContext.getAsConstantArrayType(Ty)) {
    unsigned EltSize = (unsigned)ASTContext.getTypeSize(AT->getElementType());
    unsigned NumElts = (unsigned)AT->getZExtSize();

    for (unsigned i = 0; i != NumElts; ++i) {
      unsigned EltOffset = i * EltSize;
      if (EltOffset >= EndBit)
        break;

      unsigned EltStart = EltOffset < StartBit ? StartBit - EltOffset : 0;
      if (!BitsContainNoUserData(AT->getElementType(), EltStart,
                                 EndBit - EltOffset, ASTContext))
        return false;
    }
    return true;
  }

  if (const auto *RD = Ty->getAsRecordDecl()) {
    const clang::ASTRecordLayout &Layout = ASTContext.getASTRecordLayout(RD);

    if (const auto *CXXRD = llvm::dyn_cast<clang::CXXRecordDecl>(RD)) {
      for (const auto &Base : CXXRD->bases()) {
        assert(!Base.isVirtual() && !Base.getType()->isDependentType() &&
               "Unexpected base class!");
        const auto *BaseRD = Base.getType()->getAsCXXRecordDecl();
        if (!BaseRD)
          continue;

        unsigned BaseOffset = ASTContext.toBits(Layout.getBaseClassOffset(BaseRD));
        if (BaseOffset >= EndBit)
          continue;

        unsigned BaseStart = BaseOffset < StartBit ? StartBit - BaseOffset : 0;
        if (!BitsContainNoUserData(Base.getType(), BaseStart,
                                   EndBit - BaseOffset, ASTContext))
          return false;
      }
    }

    unsigned Index = 0;
    for (auto It = RD->field_begin(), End = RD->field_end(); It != End;
         ++It, ++Index) {
      unsigned FieldOffset = (unsigned)Layout.getFieldOffset(Index);
      if (FieldOffset >= EndBit)
        break;

      unsigned FieldStart = FieldOffset < StartBit ? StartBit - FieldOffset : 0;
      if (!BitsContainNoUserData(It->getType(), FieldStart, EndBit - FieldOffset,
                                 ASTContext))
        return false;
    }
    return true;
  }

  return false;
}

// classifySwiftType is now handled by swiftcall::classifyType in
// SwiftCallingConv.cpp, which properly decomposes aggregates into
// CoerceAndExpand types.

/// \p returns the size in bits of the largest (native) vector for \p AVXLevel.
unsigned getNativeVectorSizeForAVXABI(X86AVXABILevel AVXLevel) {
  switch (AVXLevel) {
  case X86AVXABILevel::AVX512:
    return 512;
  case X86AVXABILevel::AVX:
    return 256;
  case X86AVXABILevel::None:
    return 128;
  }
  cir_cconv_unreachable("Unknown AVXLevel");
}

/// Returns true if this type can be passed in SSE/AVX registers with the
/// x86_64 Windows vectorcall calling convention.
static bool isX86VectorTypeForVectorCall(mlir::Type Ty,
                                         const CIRLowerContext &Context,
                                         const clang::TargetInfo &Target) {
  if (mlir::isa<SingleType, DoubleType, BF16Type, FP128Type>(Ty))
    return true;

  if (mlir::isa<FP16Type>(Ty))
    return false;

  if (mlir::isa<LongDoubleType>(Ty)) {
    if (&Target.getLongDoubleFormat() == &llvm::APFloat::x87DoubleExtended())
      return false;
    return true;
  }

  if (auto VecTy = mlir::dyn_cast<VectorType>(Ty)) {
    if (VecTy.getIsScalable() || VecTy.getIsSveFixedLength())
      return false;

    uint64_t VecSize = Context.getTypeSize(VecTy);
    return VecSize == 128 || VecSize == 256 || VecSize == 512;
  }

  return false;
}

static bool isX86VectorCallAggregateSmallEnough(uint64_t NumMembers) {
  return NumMembers <= 4;
}

/// Determine whether \p Ty is a homogeneous floating-point/vector aggregate
/// accepted by x86_64 Windows vectorcall. This mirrors the OGCG helper closely
/// enough for the vectorcall aggregate cases covered by clang/test/CodeGen.
static bool isHomogeneousAggregateForVectorCall(
    mlir::Type Ty, mlir::Type &Base, uint64_t &Members,
    const CIRLowerContext &Context, const clang::TargetInfo &Target) {
  if (auto ArrTy = mlir::dyn_cast<ArrayType>(Ty)) {
    uint64_t NumElements = ArrTy.getSize();
    if (NumElements == 0)
      return false;

    if (!isHomogeneousAggregateForVectorCall(ArrTy.getElementType(), Base,
                                             Members, Context, Target))
      return false;

    Members *= NumElements;
    return Members > 0 && isX86VectorCallAggregateSmallEnough(Members);
  }

  if (auto RT = mlir::dyn_cast<RecordType>(Ty)) {
    if (auto Ast = RT.getAst()) {
      if (const clang::RecordDecl *RD = Ast.getRawDecl();
          RD && RD->hasFlexibleArrayMember())
        return false;
    }

    auto MembersList = RT.getMembers();
    if (MembersList.empty())
      return false;

    Members = 0;
    bool IsUnion = RT.isUnion();
    for (mlir::Type Member : MembersList) {
      uint64_t FieldMembers = 0;
      if (!isHomogeneousAggregateForVectorCall(Member, Base, FieldMembers,
                                               Context, Target))
        return false;
      Members = IsUnion ? std::max(Members, FieldMembers)
                        : Members + FieldMembers;
    }

    if (!Base)
      return false;

    if (Context.getTypeSize(Base) * Members != Context.getTypeSize(Ty))
      return false;

    return Members > 0 && isX86VectorCallAggregateSmallEnough(Members);
  }

  Members = 1;
  if (auto CT = mlir::dyn_cast<ComplexType>(Ty)) {
    Members = 2;
    Ty = CT.getElementType();
  }

  if (!isX86VectorTypeForVectorCall(Ty, Context, Target))
    return false;

  if (!Base) {
    Base = Ty;
  } else if (mlir::isa<VectorType>(Base) != mlir::isa<VectorType>(Ty) ||
             Context.getTypeSize(Base) != Context.getTypeSize(Ty)) {
    return false;
  }

  return isX86VectorCallAggregateSmallEnough(Members);
}

static ABIArgInfo getDirectX86Hva(mlir::Type Ty) {
  auto AI = ABIArgInfo::getDirect(Ty, /*offset=*/0, /*padding=*/nullptr,
                                  /*canBeFlattened=*/false);
  AI.setInReg(true);
  return AI;
}

/// Return true if the specified [start,end) bit range is known to either be
/// off the end of the specified type or being in alignment padding.  The user
/// type specified is known to be at most 128 bits in size, and have passed
/// through X86_64ABIInfo::classify with a successful classification that put
/// one of the two halves in the INTEGER class.
///
/// It is conservatively correct to return false.
static bool BitsContainNoUserData(mlir::Type Ty, unsigned StartBit,
                                  unsigned EndBit, CIRLowerContext &Context) {
  // If the bytes being queried are off the end of the type, there is no user
  // data hiding here.  This handles analysis of builtins, vectors and other
  // types that don't contain interesting padding.
  unsigned TySize = (unsigned)Context.getTypeSize(Ty);
  if (TySize <= StartBit)
    return true;

  if (auto arrTy = llvm::dyn_cast<ArrayType>(Ty)) {
    unsigned EltSize = (unsigned)Context.getTypeSize(arrTy.getElementType());
    unsigned NumElts = (unsigned)arrTy.getSize();

    // Check each element to see if the element overlaps with the queried range.
    for (unsigned i = 0; i != NumElts; ++i) {
      // If the element is after the span we care about, then we're done.
      unsigned EltOffset = i * EltSize;
      if (EltOffset >= EndBit)
        break;

      unsigned EltStart = EltOffset < StartBit ? StartBit - EltOffset : 0;
      if (!BitsContainNoUserData(arrTy.getElementType(), EltStart,
                                 EndBit - EltOffset, Context))
        return false;
    }
    // If it overlaps no elements, then it is safe to process as padding.
    return true;
  }

  if (auto recordTy = llvm::dyn_cast<RecordType>(Ty)) {
    const CIRRecordLayout &Layout = Context.getCIRRecordLayout(Ty);

    // If this is a C++ record, check the bases first.
    if (auto ast = recordTy.getAst()) {
      const clang::RecordDecl *RD = ast.getRawDecl();
      if (const auto *CXXRD = llvm::dyn_cast<clang::CXXRecordDecl>(RD)) {
        const clang::ASTRecordLayout &ASTLayout =
            RD->getASTContext().getASTRecordLayout(RD);
        for (const auto &Base : CXXRD->bases()) {
          assert(!Base.isVirtual() && !Base.getType()->isDependentType() &&
                 "Unexpected base class!");

          const auto *BaseRD = Base.getType()->getAsCXXRecordDecl();
          if (!BaseRD)
            continue;

          // If the base is after the span we care about, ignore it.
          unsigned BaseOffset =
              ASTLayout.getBaseClassOffset(BaseRD).getQuantity() * 8;
          if (BaseOffset >= EndBit)
            continue;

          unsigned BaseStart =
              BaseOffset < StartBit ? StartBit - BaseOffset : 0;
          if (!BitsContainNoUserData(Base.getType(), BaseStart,
                                     EndBit - BaseOffset, RD->getASTContext()))
            return false;
        }
      }
    }

    // Verify that no field has data that overlaps the region of interest. Yes
    // this could be sped up a lot by being smarter about queried fields,
    // however we're only looking at records up to 16 bytes, so we don't care
    // much.
    //
    // When AST info is available, always use AST-based field iteration.
    // CIR RecordTypes may contain explicit padding members interleaved with
    // real fields, making index-based padding skip unreliable. Using AST
    // fields directly (like OGCG) avoids this issue entirely and also
    // handles bitfield records correctly — CIR stores bitfields using
    // minimal access units (e.g., i8 for "int d:3"), but the x86_64 ABI
    // needs the full backing type size (int = 32 bits).
    if (auto ast = recordTy.getAst()) {
      const clang::RecordDecl *RD = ast.getRawDecl();
      clang::ASTContext &ASTCtx = RD->getASTContext();
      const clang::ASTRecordLayout &ASTLayout =
          ASTCtx.getASTRecordLayout(RD);

      for (const auto *FD : RD->fields()) {
        if (FD->isUnnamedBitField())
          continue;

        unsigned FieldOffset =
            (unsigned)ASTLayout.getFieldOffset(FD->getFieldIndex());
        if (FieldOffset >= EndBit)
          break;

        unsigned FieldStart =
            FieldOffset < StartBit ? StartBit - FieldOffset : 0;
        if (!BitsContainNoUserData(FD->getType(), FieldStart,
                                   EndBit - FieldOffset, ASTCtx))
          return false;
      }

      return true;
    }

    // Fallback for records without AST info: use CIR member types directly.
    unsigned idx = 0;
    for (auto type : recordTy.getMembers()) {
      unsigned FieldOffset = (unsigned)Layout.getFieldOffset(idx);

      // If we found a field after the region we care about, then we're done.
      if (FieldOffset >= EndBit)
        break;

      unsigned FieldStart = FieldOffset < StartBit ? StartBit - FieldOffset : 0;
      if (!BitsContainNoUserData(type, FieldStart, EndBit - FieldOffset,
                                 Context))
        return false;

      ++idx;
    }

    // If nothing in this record overlapped the area of interest, we're good.
    return true;
  }

  return false;
}

/// Return a floating point type at the specified offset.
mlir::Type getFPTypeAtOffset(mlir::Type IRType, unsigned IROffset,
                             const cir::CIRDataLayout &TD) {
  if (IROffset == 0 &&
      mlir::isa<SingleType, DoubleType, FP16Type, BF16Type>(IRType))
    return IRType;

  // If this is a struct, recurse into the field at the specified offset.
  if (auto RT = mlir::dyn_cast<RecordType>(IRType)) {
    auto members = RT.getMembers();
    if (members.empty())
      return nullptr;

    // For unions, all members are at offset 0. OGCG operates on LLVM IR
    // types where unions are { largest_member, [padding] }, so recurse into
    // the largest member to match OGCG behavior.
    if (RT.isUnion()) {
      mlir::Type largest = RT.getLargestMember(TD.layout);
      return getFPTypeAtOffset(largest, IROffset, TD);
    }

    // Use element offsets from the record layout to correctly handle
    // alignment padding between members (e.g., {float, double} has
    // the double at offset 8, not 4). Use the next field's offset
    // (or the struct's total size) as the boundary.
    for (size_t i = 0; i < members.size(); ++i) {
      unsigned fieldOffset = (unsigned)RT.getElementOffset(TD.layout, i);
      unsigned nextBoundary;
      if (i + 1 < members.size())
        nextBoundary = (unsigned)RT.getElementOffset(TD.layout, i + 1);
      else
        nextBoundary = (unsigned)TD.getTypeAllocSize(RT);
      if (IROffset < nextBoundary)
        return getFPTypeAtOffset(members[i], IROffset - fieldOffset, TD);
    }
    return nullptr;
  }

  // If this is an array, recurse into the element at the specified offset.
  if (auto arrTy = mlir::dyn_cast<ArrayType>(IRType)) {
    mlir::Type EltTy = arrTy.getElementType();
    unsigned EltSize = TD.getTypeAllocSize(EltTy);
    if (EltSize == 0)
      return nullptr;
    IROffset -= IROffset / EltSize * EltSize;
    return getFPTypeAtOffset(EltTy, IROffset, TD);
  }

  // For complex types, extract the element type at the given offset.
  if (auto CT = mlir::dyn_cast<ComplexType>(IRType)) {
    mlir::Type EltTy = CT.getElementType();
    unsigned EltSize = TD.getTypeAllocSize(EltTy);
    if (IROffset < EltSize)
      return getFPTypeAtOffset(EltTy, IROffset, TD);
    return getFPTypeAtOffset(EltTy, IROffset - EltSize, TD);
  }

  return nullptr;
}

static unsigned getNumABIRelevantMembers(RecordType RT) {
  unsigned NumRealMembers = RT.getNumElements();
  if (auto ast = RT.getAst()) {
    const clang::RecordDecl *RD = ast.getRawDecl();
    if (RD) {
      unsigned NumFields = RD->getNumFields();
      unsigned NumBases = 0;
      if (const auto *CXXRD = llvm::dyn_cast<clang::CXXRecordDecl>(RD))
        NumBases = CXXRD->getNumBases();
      NumRealMembers = NumFields + NumBases;
    }
  }
  return NumRealMembers;
}

static bool is32Or64BitBasicType(mlir::Type Ty, CIRLowerContext &Context) {
  if (auto ComplexTy = mlir::dyn_cast<ComplexType>(Ty))
    Ty = ComplexTy.getElementType();

  if (!mlir::isa<IntType, BoolType, SingleType, DoubleType, PointerType>(Ty))
    return false;

  uint64_t Size = Context.getTypeSize(Ty);
  return Size == 32 || Size == 64;
}

static bool shouldReturnTypeInRegisterAST(clang::QualType Ty,
                                          clang::ASTContext &Context) {
  uint64_t Size = Context.getTypeSize(Ty);
  if (Size != 8 && Size != 16 && Size != 32 && Size != 64)
    return false;

  if (Ty->isVectorType()) {
    if (Size == 64 || Size == 128)
      return false;
    return true;
  }

  if (Ty->getAs<clang::BuiltinType>() || Ty->hasPointerRepresentation() ||
      Ty->isAnyComplexType() || Ty->isEnumeralType() ||
      Ty->isBlockPointerType() || Ty->isMemberPointerType())
    return true;

  if (const auto *AT = Context.getAsConstantArrayType(Ty))
    return shouldReturnTypeInRegisterAST(AT->getElementType(), Context);

  const auto *RD = Ty->getAsRecordDecl();
  if (!RD)
    return false;

  for (const auto *FD : RD->fields()) {
    if (FD->isUnnamedBitField())
      continue;
    if (!shouldReturnTypeInRegisterAST(FD->getType(), Context))
      return false;
  }

  return true;
}

} // namespace

class X86_32ABIInfo : public ABIInfo {
  enum class Class {
    Integer,
    Float,
  };

  static constexpr unsigned MinABIStackAlignInBytes = 4;

  bool IsDarwinVectorABI;
  bool IsRetSmallStructInRegABI;
  bool IsWin32StructABI;
  bool IsSoftFloatABI;
  bool IsLinuxABI;
  unsigned DefaultNumRegisterParameters;

  static bool isRegisterSize(unsigned Size) {
    return Size == 8 || Size == 16 || Size == 32 || Size == 64;
  }

  struct CCState {
    llvm::SmallBitVector IsPreassigned;
    llvm::CallingConv::ID CC = llvm::CallingConv::C;
    unsigned NumRequiredArgs = 0;
    unsigned FreeRegs = 0;
    unsigned FreeSSERegs = 0;
  };

  Class classify(mlir::Type Ty) const;
  bool shouldReturnTypeInRegister(mlir::Type Ty) const;
  bool canExpandIndirectArgument(RecordType RT) const;
  unsigned getTypeStackAlignInBytes(mlir::Type Ty, unsigned Align) const;
  ABIArgInfo getIndirectResult(mlir::Type Ty, bool ByVal,
                               CCState &State) const;
  ABIArgInfo getIndirectReturnResult(mlir::Type Ty, CCState &State) const;
  bool updateFreeRegs(mlir::Type Ty, CCState &State) const;
  bool shouldAggregateUseDirect(mlir::Type Ty, CCState &State, bool &InReg,
                                bool &NeedsPadding) const;
  bool shouldPrimitiveUseInReg(mlir::Type Ty, CCState &State) const;
  void runVectorCallFirstPass(LowerFunctionInfo &FI, CCState &State) const;
  ABIArgInfo classifyReturnType(mlir::Type RetTy, CCState &State) const;
  ABIArgInfo classifyArgumentType(mlir::Type Ty, CCState &State,
                                  unsigned ArgIndex) const;

public:
  X86_32ABIInfo(LowerTypes &LT, bool DarwinVectorABI,
                bool RetSmallStructInRegABI, bool Win32StructABI,
                unsigned NumRegisterParameters, bool SoftFloatABI)
      : ABIInfo(LT), IsDarwinVectorABI(DarwinVectorABI),
        IsRetSmallStructInRegABI(RetSmallStructInRegABI),
        IsWin32StructABI(Win32StructABI), IsSoftFloatABI(SoftFloatABI),
        IsLinuxABI(LT.getTarget().getTriple().isOSLinux() ||
                   LT.getTarget().getTriple().isOSCygMing()),
        DefaultNumRegisterParameters(NumRegisterParameters) {}

  void computeInfo(LowerFunctionInfo &FI) const override;
};

class X86_32TargetLoweringInfo : public TargetLoweringInfo {
public:
  X86_32TargetLoweringInfo(LowerTypes &LT, bool DarwinVectorABI,
                           bool RetSmallStructInRegABI, bool Win32StructABI,
                           unsigned NumRegisterParameters, bool SoftFloatABI)
      : TargetLoweringInfo(
            std::make_unique<X86_32ABIInfo>(
                LT, DarwinVectorABI, RetSmallStructInRegABI, Win32StructABI,
                NumRegisterParameters, SoftFloatABI)) {}

  unsigned getTargetAddrSpaceFromCIRAddrSpace(
      cir::LangAddressSpace addrSpace) const override {
    switch (addrSpace) {
    case cir::LangAddressSpace::OffloadPrivate:
    case cir::LangAddressSpace::OffloadLocal:
    case cir::LangAddressSpace::OffloadGlobal:
    case cir::LangAddressSpace::OffloadConstant:
    case cir::LangAddressSpace::OffloadGeneric:
      return 0;
    default:
      cir_cconv_unreachable("Unknown CIR address space for this target");
    }
  }
};

class X86_64TargetLoweringInfo : public TargetLoweringInfo {
public:
  X86_64TargetLoweringInfo(LowerTypes &LM, X86AVXABILevel AVXLevel)
      : TargetLoweringInfo(std::make_unique<X86_64ABIInfo>(LM, AVXLevel)) {}

  mlir::Type getOpaqueType(cir::OpaqueType type) const override {
    // The LLVM x86_amx tile-register type is carried through CIR as an opaque
    // type tagged "x86.amx" (it only ever appears in AMX intrinsic
    // signatures). Lower it to the primitive LLVM::LLVMX86AMXType.
    if (type.getTag() == cir::OpaqueType::getX86AMXTag())
      return mlir::LLVM::LLVMX86AMXType::get(type.getContext());
    return TargetLoweringInfo::getOpaqueType(type);
  }

  unsigned getTargetAddrSpaceFromCIRAddrSpace(
      cir::LangAddressSpace addrSpace) const override {
    switch (addrSpace) {
    case cir::LangAddressSpace::OffloadPrivate:
    case cir::LangAddressSpace::OffloadLocal:
    case cir::LangAddressSpace::OffloadGlobal:
    case cir::LangAddressSpace::OffloadConstant:
    case cir::LangAddressSpace::OffloadGeneric:
      return 0;
    default:
      cir_cconv_unreachable("Unknown CIR address space for this target");
    }
  }
};

static bool isStructReturnInRegABI(LowerModule &LM) {
  const llvm::Triple &Triple = LM.getTarget().getTriple();
  const auto &CodeGenOpts = LM.getContext().getCodeGenOpts();

  switch (CodeGenOpts.getStructReturnConvention()) {
  case clang::CodeGenOptions::SRCK_Default:
    break;
  case clang::CodeGenOptions::SRCK_OnStack:
    return false;
  case clang::CodeGenOptions::SRCK_InRegs:
    return true;
  }

  if (Triple.isOSDarwin() || Triple.isOSIAMCU())
    return true;

  switch (Triple.getOS()) {
  case llvm::Triple::DragonFly:
  case llvm::Triple::FreeBSD:
  case llvm::Triple::OpenBSD:
  case llvm::Triple::Win32:
    return true;
  default:
    return false;
  }
}

bool X86_32ABIInfo::shouldReturnTypeInRegister(mlir::Type Ty) const {
  uint64_t Size = getContext().getTypeSize(Ty);
  if (!isRegisterSize(Size))
    return false;

  if (mlir::isa<IntType, BoolType, PointerType, ComplexType, SingleType,
                DoubleType>(Ty))
    return true;

  if (auto VecTy = mlir::dyn_cast<VectorType>(Ty)) {
    if (Size == 64 || Size == 128)
      return false;
    return true;
  }

  if (auto ArrTy = mlir::dyn_cast<ArrayType>(Ty)) {
    if (ArrTy.getSize() != 1)
      return false;
    return shouldReturnTypeInRegister(ArrTy.getElementType());
  }

  auto RT = mlir::dyn_cast<RecordType>(Ty);
  if (!RT || RT.isUnion())
    return false;

  if (auto ast = RT.getAst())
    if (const clang::RecordDecl *RD = ast.getRawDecl())
      return shouldReturnTypeInRegisterAST(
          RD->getASTContext().getTypeDeclType(llvm::cast<clang::TypeDecl>(RD)),
          RD->getASTContext());

  unsigned NumRealMembers = getNumABIRelevantMembers(RT);
  if (RT.getNumElements() < NumRealMembers)
    return false;

  auto Members = RT.getMembers();
  for (unsigned I = 0; I < NumRealMembers; ++I)
    if (!shouldReturnTypeInRegister(Members[I]))
      return false;

  return true;
}

bool X86_32ABIInfo::canExpandIndirectArgument(RecordType RT) const {
  if (RT.isUnion())
    return false;

  auto ast = RT.getAst();
  if (!ast)
    return false;

  const clang::RecordDecl *RD = ast.getRawDecl();
  if (!RD)
    return false;

  if (llvm::any_of(RD->fields(),
                   [](const clang::FieldDecl *FD) { return FD->isBitField(); }))
    return false;

  if (const auto *CXXRD = llvm::dyn_cast<clang::CXXRecordDecl>(RD))
    if (!CXXRD->isCLike())
      return false;

  unsigned NumRealMembers = getNumABIRelevantMembers(RT);
  if (RT.getNumElements() < NumRealMembers)
    return false;

  uint64_t Size = 0;
  auto Members = RT.getMembers();
  for (unsigned I = 0; I < NumRealMembers; ++I) {
    mlir::Type MemberTy = Members[I];
    if (!is32Or64BitBasicType(MemberTy, getContext()))
      return false;
    Size += getContext().getTypeSize(MemberTy);
  }

  return Size == getContext().getTypeSize(RT);
}

unsigned X86_32ABIInfo::getTypeStackAlignInBytes(mlir::Type Ty,
                                                 unsigned Align) const {
  if (Align <= MinABIStackAlignInBytes)
    return 0;

  if (IsLinuxABI) {
    if (mlir::isa<VectorType>(Ty) && (Align == 16 || Align == 32 || Align == 64))
      return Align;
  }

  if (!IsDarwinVectorABI)
    return MinABIStackAlignInBytes;

  if (Align >= 16 && mlir::isa<VectorType>(Ty))
    return 16;

  return MinABIStackAlignInBytes;
}

ABIArgInfo X86_32ABIInfo::getIndirectResult(mlir::Type Ty, bool ByVal,
                                            CCState &State) const {
  if (!ByVal) {
    ABIArgInfo AI = getNaturalAlignIndirect(Ty, /*ByVal=*/false);
    if (State.FreeRegs) {
      --State.FreeRegs;
      AI.setInReg(true);
    }
    return AI;
  }

  unsigned TypeAlign = getContext().getTypeAlign(Ty) / 8;
  unsigned StackAlign = getTypeStackAlignInBytes(Ty, TypeAlign);
  if (StackAlign == 0)
    return ABIArgInfo::getIndirect(MinABIStackAlignInBytes, /*ByVal=*/true);

  bool Realign = TypeAlign > StackAlign;
  return ABIArgInfo::getIndirect(StackAlign, /*ByVal=*/true, Realign);
}

ABIArgInfo X86_32ABIInfo::getIndirectReturnResult(mlir::Type RetTy,
                                                  CCState &State) const {
  ABIArgInfo AI = getNaturalAlignIndirect(RetTy, /*ByVal=*/false);
  if (State.CC != llvm::CallingConv::X86_FastCall &&
      State.CC != llvm::CallingConv::X86_VectorCall && State.FreeRegs) {
    --State.FreeRegs;
    AI.setInReg(true);
  }
  return AI;
}

ABIArgInfo X86_32ABIInfo::classifyReturnType(mlir::Type RetTy,
                                             CCState &State) const {
  if (mlir::isa<VoidType>(RetTy))
    return ABIArgInfo::getIgnore();

  mlir::Type Base = nullptr;
  uint64_t NumElts = 0;
  if (State.CC == llvm::CallingConv::X86_VectorCall &&
      isHomogeneousAggregateForVectorCall(RetTy, Base, NumElts, getContext(),
                                          getTarget()))
    return ABIArgInfo::getDirect();

  if (mlir::isa<VectorType>(RetTy))
    return ABIArgInfo::getDirect();

  if (isAggregateTypeForABI(RetTy)) {
    if (auto RT = mlir::dyn_cast<RecordType>(RetTy)) {
      if (auto ast = RT.getAst()) {
        const clang::RecordDecl *RD = ast.getRawDecl();
        if (RD && RD->hasFlexibleArrayMember())
          return getIndirectReturnResult(RetTy, State);
      }
    }

    if (!IsRetSmallStructInRegABI && !mlir::isa<ComplexType>(RetTy))
      return getIndirectReturnResult(RetTy, State);

    if (isEmptyRecord(RetTy, /*allowArrays=*/true) ||
        getContext().getTypeSize(RetTy) == 0)
      return ABIArgInfo::getIgnore();

    if (auto ComplexTy = mlir::dyn_cast<ComplexType>(RetTy))
      if (mlir::isa<FP16Type>(ComplexTy.getElementType()))
        return ABIArgInfo::getDirect(
            cir::VectorType::get(FP16Type::get(LT.getMLIRContext()), 2));

    if (shouldReturnTypeInRegister(RetTy)) {
      if (mlir::Type SingleEltTy = isSingleElementStruct(RetTy))
        if (((!IsWin32StructABI &&
              mlir::isa<SingleType, DoubleType>(SingleEltTy)) ||
             mlir::isa<PointerType>(SingleEltTy)))
          return ABIArgInfo::getDirect(SingleEltTy);

      return ABIArgInfo::getDirect(IntType::get(
          LT.getMLIRContext(), getContext().getTypeSize(RetTy),
          /*isSigned=*/false));
    }

    return getIndirectReturnResult(RetTy, State);
  }

  if (auto IntTy = mlir::dyn_cast<IntType>(RetTy))
    if (IntTy.getWidth() > 64)
      return getIndirectReturnResult(RetTy, State);

  return isPromotableIntegerTypeForABI(RetTy) ? ABIArgInfo::getExtend(RetTy)
                                              : ABIArgInfo::getDirect();
}

X86_32ABIInfo::Class X86_32ABIInfo::classify(mlir::Type Ty) const {
  if (mlir::Type SingleEltTy = isSingleElementStruct(Ty))
    Ty = SingleEltTy;

  if (mlir::isa<SingleType, DoubleType>(Ty))
    return Class::Float;

  return Class::Integer;
}

bool X86_32ABIInfo::updateFreeRegs(mlir::Type Ty, CCState &State) const {
  if (!IsSoftFloatABI && classify(Ty) == Class::Float)
    return false;

  unsigned Size = getContext().getTypeSize(Ty);
  unsigned SizeInRegs = (Size + 31) / 32;

  if (SizeInRegs == 0)
    return false;

  if (SizeInRegs > State.FreeRegs) {
    State.FreeRegs = 0;
    return false;
  }

  State.FreeRegs -= SizeInRegs;
  return true;
}

bool X86_32ABIInfo::shouldAggregateUseDirect(mlir::Type Ty, CCState &State,
                                             bool &InReg,
                                             bool &NeedsPadding) const {
  // On Windows, aggregates other than HFAs/HVAs are never passed in general
  // purpose registers and they do not consume those register slots.
  if (IsWin32StructABI && isAggregateTypeForABI(Ty))
    return false;

  NeedsPadding = false;
  InReg = true;

  if (!updateFreeRegs(Ty, State))
    return false;

  if (State.CC == llvm::CallingConv::X86_FastCall ||
      State.CC == llvm::CallingConv::X86_VectorCall ||
      State.CC == llvm::CallingConv::X86_RegCall) {
    if (getContext().getTypeSize(Ty) <= 32 && State.FreeRegs)
      NeedsPadding = true;
    return false;
  }

  return true;
}

bool X86_32ABIInfo::shouldPrimitiveUseInReg(mlir::Type Ty,
                                            CCState &State) const {
  bool IsPtrOrInt = getContext().getTypeSize(Ty) <= 32 &&
                    mlir::isa<IntType, BoolType, PointerType>(Ty);

  if (!IsPtrOrInt && (State.CC == llvm::CallingConv::X86_FastCall ||
                      State.CC == llvm::CallingConv::X86_VectorCall))
    return false;

  if (!updateFreeRegs(Ty, State))
    return false;

  if (!IsPtrOrInt && State.CC == llvm::CallingConv::X86_RegCall)
    return false;

  return true;
}

void X86_32ABIInfo::runVectorCallFirstPass(LowerFunctionInfo &FI,
                                           CCState &State) const {
  auto Args = FI.arguments();
  for (unsigned I = 0, E = Args.size(); I != E; ++I) {
    mlir::Type Ty = Args[I].type;
    if (!mlir::isa<VectorType, SingleType, DoubleType, FP16Type, BF16Type,
                   FP80Type, FP128Type, LongDoubleType>(Ty))
      continue;

    mlir::Type Base = nullptr;
    uint64_t NumElts = 0;
    if (!isHomogeneousAggregateForVectorCall(Ty, Base, NumElts, getContext(),
                                             getTarget()))
      continue;

    if (State.FreeSSERegs < NumElts)
      continue;

    State.FreeSSERegs -= NumElts;
    Args[I].info = getDirectX86Hva(Ty);
    State.IsPreassigned.set(I);
  }
}

ABIArgInfo X86_32ABIInfo::classifyArgumentType(mlir::Type Ty,
                                               CCState &State,
                                               unsigned ArgIndex) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (CIRCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
    return getIndirectResult(Ty, RAA == CIRCXXABI::RAA_DirectInMemory, State);

  mlir::Type Base = nullptr;
  uint64_t NumElts = 0;
  if (State.CC == llvm::CallingConv::X86_VectorCall &&
      isHomogeneousAggregateForVectorCall(Ty, Base, NumElts, getContext(),
                                          getTarget())) {
    if (State.FreeSSERegs >= NumElts) {
      State.FreeSSERegs -= NumElts;
      return getDirectX86Hva(Ty);
    }

    if (!isAggregateTypeForABI(Ty) && !mlir::isa<VectorType>(Ty))
      return ABIArgInfo::getDirect();

    return getIndirectResult(Ty, /*ByVal=*/false, State);
  }

  if (isAggregateTypeForABI(Ty)) {
    if (auto RT = mlir::dyn_cast<RecordType>(Ty)) {
      if (auto ast = RT.getAst()) {
        const clang::RecordDecl *RD = ast.getRawDecl();
        if (RD && RD->hasFlexibleArrayMember())
          return getIndirectResult(Ty, /*ByVal=*/true, State);
      }
    }

    if ((!IsWin32StructABI && isEmptyRecord(Ty, /*allowArrays=*/true)) ||
        getContext().getTypeSize(Ty) == 0)
      return ABIArgInfo::getIgnore();

    bool InReg = false;
    bool NeedsPadding = false;
    bool UseDirect = shouldAggregateUseDirect(Ty, State, InReg, NeedsPadding);

    if (IsWin32StructABI && ArgIndex < State.NumRequiredArgs) {
      unsigned AlignInBits = 0;
      if (auto RT = mlir::dyn_cast<RecordType>(Ty))
        if (auto ast = RT.getAst())
          if (const clang::RecordDecl *RD = ast.getRawDecl()) {
            const clang::ASTRecordLayout &Layout =
                RD->getASTContext().getASTRecordLayout(RD);
            AlignInBits = RD->getASTContext().toBits(Layout.getRequiredAlignment());
          }
      if (AlignInBits > 32)
        return getIndirectResult(Ty, /*ByVal=*/false, State);
    }

    if (auto RT = mlir::dyn_cast<RecordType>(Ty))
      if (getContext().getTypeSize(Ty) <= 128 && canExpandIndirectArgument(RT))
        return [&]() {
          ABIArgInfo AI = ABIArgInfo::getDirect(
            Ty, /*offset=*/0,
            NeedsPadding
                ? mlir::Type(IntType::get(LT.getMLIRContext(), 32,
                                          /*isSigned=*/false))
                : mlir::Type(),
            /*canBeFlattened=*/true);
          // Aggregates flattened into the argument registers (regparm/fastcall/
          // vectorcall/regcall) are marked inreg, mirroring classic's
          // getDirectInReg. NeedsPadding likewise consumes a register slot.
          if ((UseDirect && InReg) || NeedsPadding)
            AI.setInReg(true);
          return AI;
        }();

    return getIndirectResult(Ty, /*ByVal=*/true, State);
  }

  if (auto IntTy = mlir::dyn_cast<IntType>(Ty))
    if (IntTy.getWidth() > 64)
      return getIndirectResult(Ty, /*ByVal=*/false, State);

  if (auto VecTy = mlir::dyn_cast<VectorType>(Ty)) {
    // On Windows, vectors are passed directly if registers are available, or
    // indirectly if not. This avoids the need to align argument memory.
    if (IsWin32StructABI) {
      if (getContext().getTypeSize(Ty) <= 512 && State.FreeSSERegs > 0) {
        --State.FreeSSERegs;
        ABIArgInfo AI = ABIArgInfo::getDirect();
        AI.setInReg(true);
        return AI;
      }
      return getIndirectResult(Ty, /*ByVal=*/false, State);
    }

    if (IsDarwinVectorABI) {
      uint64_t Width = getContext().getTypeSize(Ty);
      if (Width == 8 || Width == 16 || Width == 32 ||
          (Width == 64 && VecTy.getSize() == 1))
        return ABIArgInfo::getDirect(IntType::get(
            LT.getMLIRContext(), Width, /*isSigned=*/false));
    }

    // An MMX-class 64-bit vector whose element is an integer narrower than 64
    // bits (__m64 / <2 x i32> / <4 x i16> / <8 x i8>) is coerced to i64 in
    // argument position, matching OGCG X86_32ABIInfo::classifyArgumentType's
    // IsX86_MMXType(...) -> getDirect(i64) case. <1 x i64> (element width 64)
    // and SSE vectors with a floating-point element (e.g. <2 x float>) are left
    // as getDirect(), as classic does.
    if (getContext().getTypeSize(VecTy) == 64)
      if (auto EltIntTy = mlir::dyn_cast<IntType>(VecTy.getElementType()))
        if (EltIntTy.getWidth() != 64)
          return ABIArgInfo::getDirect(
              IntType::get(LT.getMLIRContext(), 64, /*isSigned=*/false));

    return ABIArgInfo::getDirect();
  }

  bool useInReg = shouldPrimitiveUseInReg(Ty, State);
  ABIArgInfo AI = isPromotableIntegerTypeForABI(Ty) ? ABIArgInfo::getExtend(Ty)
                                                    : ABIArgInfo::getDirect();
  if (useInReg)
    AI.setInReg(true);
  return AI;
}

void X86_32ABIInfo::computeInfo(LowerFunctionInfo &FI) const {
  CCState State;
  State.CC = static_cast<llvm::CallingConv::ID>(FI.getCallingConvention());
  State.IsPreassigned = llvm::SmallBitVector(FI.arg_size());
  State.NumRequiredArgs = FI.getNumRequiredArgs();

  if (State.CC == llvm::CallingConv::X86_FastCall) {
    State.FreeRegs = 2;
    State.FreeSSERegs = 3;
  } else if (State.CC == llvm::CallingConv::X86_VectorCall) {
    State.FreeRegs = 2;
    State.FreeSSERegs = 6;
  } else if (FI.getHasRegParm()) {
    State.FreeRegs = FI.getRegParm();
  } else if (IsWin32StructABI) {
    State.FreeRegs = DefaultNumRegisterParameters;
    State.FreeSSERegs = 3;
  } else {
    State.FreeRegs = DefaultNumRegisterParameters;
  }

  if (!cir::classifyReturnType(getCXXABI(), FI, *this)) {
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType(), State);
  } else if (FI.getReturnInfo().isIndirect()) {
    // The C++ ABI is not aware of register usage, so we have to check if the
    // return value was sret and put it in a register ourselves if appropriate.
    if (State.FreeRegs) {
      --State.FreeRegs; // The sret parameter consumes a register.
      FI.getReturnInfo().setInReg(true);
    }
  }

  if (FI.isChainCall())
    ++State.FreeRegs;

  if (State.CC == llvm::CallingConv::X86_VectorCall)
    runVectorCallFirstPass(FI, State);

  for (auto &&[ArgIndex, Arg] : llvm::enumerate(FI.arguments())) {
    if (State.IsPreassigned.test(ArgIndex))
      continue;
    Arg.info = classifyArgumentType(Arg.type, State, ArgIndex);
  }
}

/// Check if the given QualType represents an empty record for ABI
/// classification purposes. Empty records contribute NoClass in the x86-64
/// SysV ABI. CIR record lowering skips empty fields, replacing them with
/// byte-array padding that would be incorrectly classified as Integer.
static bool isEmptyRecordForClassify(
    const clang::RecordDecl *RD,
    llvm::DenseMap<const clang::RecordDecl *, bool> &memo);

static bool isEmptyRecordForClassify(
    clang::QualType T, llvm::DenseMap<const clang::RecordDecl *, bool> &memo) {
  const auto *RD = T->getAsRecordDecl();
  if (!RD)
    return false;
  return isEmptyRecordForClassify(RD, memo);
}

/// Overload that takes a RecordDecl directly.
static bool isEmptyRecordForClassify(
    const clang::RecordDecl *RD,
    llvm::DenseMap<const clang::RecordDecl *, bool> &memo) {
  if (!RD)
    return false;

  if (const auto it = memo.find(RD); it != memo.end())
    return it->second;

  if (RD->hasFlexibleArrayMember()) {
    memo.try_emplace(RD, false);
    return false;
  }

  bool isEmpty = true;

  if (const auto *CXXRD = llvm::dyn_cast<clang::CXXRecordDecl>(RD)) {
    for (const auto &Base : CXXRD->bases()) {
      if (!isEmptyRecordForClassify(Base.getType(), memo)) {
        isEmpty = false;
        break;
      }
    }
  }

  if (isEmpty) {
    for (const auto *FD : RD->fields()) {
      if (FD->isUnnamedBitField())
        continue;
      if (!isEmptyRecordForClassify(FD->getType(), memo)) {
        isEmpty = false;
        break;
      }
    }
  }

  memo.try_emplace(RD, isEmpty);
  return isEmpty;
}

void X86_64ABIInfo::classify(mlir::Type Ty, uint64_t OffsetBase, Class &Lo,
                             Class &Hi, bool isNamedArg, bool IsRegCall) const {
  // FIXME: This code can be simplified by introducing a simple value class
  // for Class pairs with appropriate constructor methods for the various
  // situations.

  // FIXME: Some of the split computations are wrong; unaligned vectors
  // shouldn't be passed in registers for example, so there is no chance they
  // can straddle an eightbyte. Verify & simplify.

  Lo = Hi = Class::NoClass;

  Class &Current = OffsetBase < 64 ? Lo : Hi;
  Current = Class::Memory;

  // FIXME(cir): There's currently no direct way to identify if a type is a
  // builtin.
  if (/*isBuitinType=*/true) {
    if (mlir::isa<VoidType>(Ty)) {
      Current = Class::NoClass;
    } else if (auto IntTy = mlir::dyn_cast<IntType>(Ty)) {
      // The AMD64 SysV ABI classifies integers by eightbyte (8-byte) chunks.
      // IntTypes wider than 64 bits (e.g., __int128, _BitInt(127)) occupy two
      // eightbytes and need Lo=Integer, Hi=Integer classification.
      // IntTypes wider than 128 bits (e.g., _BitInt(160)) are too large for
      // registers and must be passed in memory. This matches OGCG's
      // classify() logic for BitIntType (X86.cpp:1988-1993).
      unsigned abySizeInBytes =
          getContext().getTypeSizeInChars(Ty).getQuantity();
      if (abySizeInBytes <= 8) {
        // An eightbyte-or-smaller integer normally occupies a single eightbyte,
        // but CIR lowers a run of bitfields to one integer storage member that
        // can sit at a non-natural offset (e.g. a 64-bit unit at byte 4) and so
        // straddle an eightbyte boundary. When it does, classify both eightbytes
        // as Integer, matching OGCG's per-bitfield classification (which sets
        // Hi=Integer for a bitfield whose bits land in the second eightbyte).
        uint64_t EB_Lo = OffsetBase / 64;
        uint64_t EB_Hi = (OffsetBase + abySizeInBytes * 8 - 1) / 64;
        if (EB_Lo == EB_Hi) {
          Current = Class::Integer;
        } else {
          Lo = Class::Integer;
          Hi = Class::Integer;
        }
      } else if (abySizeInBytes <= 16) {
        Lo = Class::Integer;
        Hi = Class::Integer;
      }
      // Larger values (> 16 bytes) stay as Memory (the default).
      return;

    } else if (mlir::isa<SingleType>(Ty) || mlir::isa<DoubleType>(Ty) ||
               mlir::isa<FP16Type>(Ty) || mlir::isa<BF16Type>(Ty)) {
      Current = Class::SSE;
      return;
    } else if (mlir::isa<FP128Type>(Ty)) {
      // Match OGCG x86-64 classification for __float128 / _Float128.
      Lo = Class::SSE;
      Hi = Class::SSEUp;
      return;

    } else if (mlir::isa<LongDoubleType>(Ty)) {
      const llvm::fltSemantics *LDF =
          &getContext().getTargetInfo().getLongDoubleFormat();
      if (LDF == &llvm::APFloat::IEEEquad()) {
        Lo = Class::SSE;
        Hi = Class::SSEUp;
      } else if (LDF == &llvm::APFloat::x87DoubleExtended()) {
        Lo = Class::X87;
        Hi = Class::X87Up;
      } else if (LDF == &llvm::APFloat::IEEEdouble()) {
        Current = Class::SSE;
      } else {
        llvm_unreachable("unexpected long double representation!");
      }
      return;
    } else if (mlir::isa<BoolType>(Ty)) {
      Current = Class::Integer;
    } else if (mlir::isa<PointerType, DataMemberType>(Ty)) {
      Current = Class::Integer;
    } else if (mlir::isa<MethodType>(Ty)) {
      // Member function pointers in Itanium ABI are {ptrdiff_t, ptrdiff_t},
      // classified as Lo=Integer, Hi=Integer.
      Lo = Hi = Class::Integer;
    } else if (auto mlirIntTy = mlir::dyn_cast<mlir::IntegerType>(Ty)) {
      // Handle MLIR builtin integer types (e.g., from exception handling
      // runtime function signatures).
      Current = Class::Integer;
    } else if (mlir::isa<mlir::FloatType>(Ty)) {
      // Handle MLIR builtin float types (e.g., from exception handling
      // runtime function signatures).
      Current = Class::SSE;
    } else if (const auto VT = mlir::dyn_cast<VectorType>(Ty)) {
      uint64_t Size = getContext().getTypeSize(VT);
      if (Size == 1 || Size == 8 || Size == 16 || Size == 32) {
        Current = Class::Integer;
        uint64_t EB_Lo = (OffsetBase) / 64;
        uint64_t EB_Hi = (OffsetBase + Size - 1) / 64;
        if (EB_Lo != EB_Hi)
          Hi = Lo;
      } else if (Size == 64) {
        mlir::Type ElementType = VT.getElementType();
        // gcc passes <1 x double> in memory.
        if (mlir::isa<DoubleType>(ElementType))
          return;

        // gcc passes <1 x long long> as SSE but clang used to unconditionally
        // pass them as integer. For platforms where clang is the de facto
        // platform compiler, we must continue to use integer.
        //
        // Match OGCG behavior: the Darwin/compat integer fallback is only for
        // one-lane 64-bit integer vectors (long/ulong/long long/ulong long),
        // not for all integer vectors of total size 64 bits (e.g. <8 x i8>).
        bool isOneLane64BitIntVec = false;
        if (auto intTy = mlir::dyn_cast<IntType>(ElementType))
          isOneLane64BitIntVec = (VT.getSize() == 1 && intTy.getWidth() == 64);

        if (!classifyIntegerMMXAsSSE() && isOneLane64BitIntVec)
          Current = Class::Integer;
        else
          Current = Class::SSE;

        if (OffsetBase && OffsetBase != 64)
          Hi = Lo;
      } else if (Size == 128 ||
                 (isNamedArg &&
                  Size <= getNativeVectorSizeForAVXABI(AVXLevel))) {
        mlir::Type EltTy = VT.getElementType();
        // gcc passes 256 and 512 bit <X x __int128> vectors in memory.
        if (passInt128VectorsInMem() && Size != 128) {
          if (auto intTy = mlir::dyn_cast<IntType>(EltTy))
            if (intTy.getWidth() == 128)
              return;
        }

        Lo = Class::SSE;
        Hi = Class::SSEUp;
      }
      return;
    } else if (const auto CT = mlir::dyn_cast<ComplexType>(Ty)) {
      mlir::Type ET = CT.getElementType();
      uint64_t Size = getContext().getTypeSize(Ty);
      if (mlir::isa<IntType>(ET)) {
        if (Size <= 64)
          Current = Class::Integer;
        else if (Size <= 128)
          Lo = Hi = Class::Integer;
      } else if (mlir::isa<SingleType, FP16Type, BF16Type>(ET)) {
        Current = Class::SSE;
      } else if (mlir::isa<DoubleType>(ET)) {
        Lo = Hi = Class::SSE;
      } else if (mlir::isa<LongDoubleType>(ET)) {
        const llvm::fltSemantics *LDF =
            &getContext().getTargetInfo().getLongDoubleFormat();
        if (LDF == &llvm::APFloat::IEEEquad())
          Current = Class::Memory;
        else if (LDF == &llvm::APFloat::x87DoubleExtended())
          Current = Class::ComplexX87;
        else if (LDF == &llvm::APFloat::IEEEdouble())
          Lo = Hi = Class::SSE;
        else
          llvm_unreachable("unexpected long double representation!");
      }

      // If this complex type crosses an eightbyte boundary then it
      // should be split.
      uint64_t EB_Real = (OffsetBase) / 64;
      uint64_t EB_Imag = (OffsetBase + getContext().getTypeSize(ET)) / 64;
      if (Hi == Class::NoClass && EB_Real != EB_Imag)
        Hi = Lo;

      return;
    } else if (const auto arrTy = mlir::dyn_cast<ArrayType>(Ty)) {
      // Arrays are treated like structures.
      uint64_t Size = getContext().getTypeSize(Ty);

      // AMD64-ABI 3.2.3p2: Rule 1. If the size of an object is larger
      // than eight eightbytes, ..., it has class MEMORY.
      if (!IsRegCall && Size > 512)
        return;

      // AMD64-ABI 3.2.3p2: Rule 1. If ..., or it contains unaligned
      // fields, it has class MEMORY.
      //
      // Only need to check alignment of array base.
      if (OffsetBase % getContext().getTypeAlign(arrTy.getElementType()))
        return;

      // Otherwise implement simplified merge. We could be smarter about
      // this, but it isn't worth it and would be harder to verify.
      Current = Class::NoClass;
      uint64_t EltSize = getContext().getTypeSize(arrTy.getElementType());
      uint64_t ArraySize = arrTy.getSize();

      // The only case a 256-bit wide vector could be used is when the array
      // contains a single 256-bit element. Since Lo and Hi logic isn't extended
      // to work for sizes wider than 128, early check and fallback to memory.
      if (Size > 128 &&
          (Size != EltSize || Size > getNativeVectorSizeForAVXABI(AVXLevel)))
        return;

      for (uint64_t i = 0, Offset = OffsetBase; i < ArraySize;
           ++i, Offset += EltSize) {
        Class FieldLo, FieldHi;
        classify(arrTy.getElementType(), Offset, FieldLo, FieldHi, isNamedArg);
        Lo = merge(Lo, FieldLo);
        Hi = merge(Hi, FieldHi);
        if (Lo == Class::Memory || Hi == Class::Memory)
          break;
      }

      postMerge(Size, Lo, Hi);
      cir_cconv_assert((Hi != Class::SSEUp || Lo == Class::SSE) &&
                       "Invalid SSEUp array classification.");
    } else if (const auto RT = mlir::dyn_cast<RecordType>(Ty)) {
      uint64_t Size = getContext().getTypeSize(Ty);

      // AMD64-ABI 3.2.3p2: Rule 1. If the size of an object is larger
      // than eight eightbytes, ..., it has class MEMORY.
      if (Size > 512)
        return;

      // AMD64-ABI 3.2.3p2: Rule 2. If a C++ object has either a non-trivial
      // copy constructor or a non-trivial destructor, it is passed by invisible
      // reference (class MEMORY).
      if (getRecordArgABI(RT, getCXXABI())) {
        Lo = Class::Memory;
        return;
      }

      // Assume variable sized types are passed in memory.
      if (auto ast = RT.getAst()) {
        if (const clang::RecordDecl *RD = ast.getRawDecl();
            RD && RD->hasFlexibleArrayMember()) {
          Lo = Class::Memory;
          return;
        }
      }

      // Incomplete record types (forward declarations) cannot be laid out.
      // Classify them as Memory (passed by pointer) since their size is
      // unknown. This can happen with member function pointers that reference
      // functions taking incomplete parameter types.
      if (auto recTy = mlir::dyn_cast<cir::RecordType>(Ty);
          recTy && !recTy.isComplete()) {
        Lo = Class::Memory;
        return;
      }

      const auto &Layout = getContext().getCIRRecordLayout(Ty);

      // Reset Lo class, this will be recomputed.
      Current = Class::NoClass;

      // Empty records (structs/unions with no real data members) classify as
      // NoClass. CIR pads empty C++ structs with an i8, so we consult the AST
      // to check for real (non-unnamed-bitfield) fields.
      //
      // Check if this record is truly empty (no data in fields or bases).
      // Use isEmptyRecordForClassify which recursively checks base classes,
      // so structs that inherit data from bases are correctly classified as
      // non-empty.
      llvm::DenseMap<const clang::RecordDecl *, bool> emptyRecordMemo;
      if (auto ast = RT.getAst()) {
        const clang::RecordDecl *RD = ast.getRawDecl();
        if (isEmptyRecordForClassify(RD, emptyRecordMemo))
          return; // Lo = Hi = NoClass
      }

      // Classify the fields one at a time, merging the results.
      bool UseClang11Compat = getContext().getLangOpts().getClangABICompat() <=
                                  clang::LangOptions::ClangABI::Ver11 ||
                              getContext().getTargetInfo().getTriple().isPS();
      bool IsUnion = RT.isUnion() && !UseClang11Compat;

      // CIR RecordTypes may contain explicit padding members (e.g., [12 x i8])
      // inserted by CIRGen for alignment. OGCG iterates only AST fields, so
      // tail padding is never classified. When the AST RecordDecl is available,
      // we compute the number of real (non-padding) members from the AST and
      // skip CIR members beyond that count. The CIR RecordType prepends base
      // class subobjects before fields, so we count both.
      //
      // Additionally, CIR record lowering skips empty struct fields entirely,
      // replacing them with byte-array padding in "padded" records. This
      // padding must not be classified — OGCG classifies AST field types
      // directly, where empty records naturally contribute NoClass. We use
      // AST field offsets to identify which CIR members correspond to real
      // non-empty fields and skip the rest.
      unsigned numRealMembers = RT.getNumElements();
      bool filterByOffset = false;
      const bool sizeExceeds128 = Size > 128;
      const bool returnCXXRecordGreaterThan128InMemFlag =
          returnCXXRecordGreaterThan128InMem();
      const uint64_t nativeVectorSizeForAVXABI =
          getNativeVectorSizeForAVXABI(AVXLevel);
      const bool sizeExceedsNativeVector = Size > nativeVectorSizeForAVXABI;
      llvm::SmallVector<uint64_t, 8> nonEmptyFieldOffsetsVec;
      llvm::SmallDenseSet<uint64_t, 8> nonEmptyFieldOffsets;
      // Track offsets of CIR members that are bitfield storage units.
      // OGCG skips the alignment check for bitfields (X86.cpp:2136:
      // "if (!BitField && IsInMemory)"), but CIR has already lowered
      // bitfields to integer storage types. We must identify which CIR
      // members correspond to bitfield storage to replicate the skip.
      llvm::SmallDenseSet<uint64_t, 8> bitfieldStorageOffsets;
      // Collect base class offsets to distinguish base vs field members in the
      // unified loop. OGCG applies different checks to bases vs fields
      // (X86.cpp:2070-2098 vs 2107-2175): base classes use the
      // returnCXXRecordGreaterThan128InMem() guard and skip alignment checks,
      // while fields use the unconditional >128 check with IsUnion and check
      // alignment. CIR's empty base optimization may remove base subobjects
      // entirely, so we use offsets instead of index ranges.
      llvm::SmallVector<uint64_t, 4> baseClassOffsets;
      if (auto ast = RT.getAst()) {
        const clang::RecordDecl *RD = ast.getRawDecl();
        unsigned numFields = RD->getNumFields();
        unsigned numBases = 0;
        bool hasEmptyMembers = false;
        const auto &ASTLayout =
            RD->getASTContext().getASTRecordLayout(RD);
        if (const auto *CXXRD = llvm::dyn_cast<clang::CXXRecordDecl>(RD)) {
          numBases = CXXRD->getNumBases();
          baseClassOffsets.reserve(numBases);
          nonEmptyFieldOffsetsVec.reserve(numBases + numFields);
          // Collect all non-empty base class offsets for identification.
          // Classes with virtual bases are always passed in memory (caught
          // by getRecordArgABI above), so we should never encounter them
          // here. This matches OGCG's assert at X86.cpp:2072.
          for (const auto &Base : CXXRD->bases()) {
            assert(!Base.isVirtual() &&
                   !Base.getType()->isDependentType() &&
                   "Unexpected base class!");
            bool isEmptyBase =
                isEmptyRecordForClassify(Base.getType(), emptyRecordMemo);
            hasEmptyMembers |= isEmptyBase;
            if (isEmptyBase)
              continue;

            const auto *BaseRD = Base.getType()->getAsCXXRecordDecl();
            uint64_t baseOffset =
                ASTLayout.getBaseClassOffset(BaseRD).getQuantity() * 8;
            baseClassOffsets.push_back(baseOffset);
            nonEmptyFieldOffsetsVec.push_back(baseOffset);
          }
        }
        if (!numBases)
          nonEmptyFieldOffsetsVec.reserve(numFields);
        numRealMembers = numBases + numFields;

        // Collect non-empty field offsets and detect empty members whose CIR
        // lowering introduces padding that must be ignored for classification.
        // Also track which field offsets correspond to bitfields so the
        // alignment check can be skipped for bitfield storage (matching
        // OGCG X86.cpp:2136).
        for (const auto *FD : RD->fields()) {
          bool isNamedBitField = !FD->isUnnamedBitField();
          bool isEmptyField =
              isNamedBitField &&
              isEmptyRecordForClassify(FD->getType(), emptyRecordMemo);
          hasEmptyMembers |= isEmptyField;
          if (isNamedBitField && !isEmptyField)
            nonEmptyFieldOffsetsVec.push_back(
                ASTLayout.getFieldOffset(FD->getFieldIndex()));
          // Only named bitfields contribute a storage offset. Unnamed
          // bitfields are padding: OGCG ignores them entirely
          // (X86.cpp:2112 "Ignore padding bit-fields"). Critically, a
          // zero-width unnamed bitfield's AST offset coincides with the
          // following real field's offset, so recording it here would
          // misclassify that field as bitfield storage and skip its
          // unaligned-field->MEMORY check.
          if (FD->isBitField() && isNamedBitField)
            bitfieldStorageOffsets.insert(
                ASTLayout.getFieldOffset(FD->getFieldIndex()));
        }
        // For padded records, CIR inserts explicit alignment padding members
        // (e.g., [4 x i8]) between real fields. These interstitial padding
        // members increase the CIR member count beyond numBases + numFields,
        // causing real fields after padding to be skipped by the
        // `idx >= numRealMembers` check. Fix: iterate ALL CIR members but
        // use offset-based filtering to classify only the real fields.
        // This also correctly skips trailing padding (e.g., alignas(16)
        // structs) since it won't match any AST field offset.
        if (RT.getPadded()) {
          numRealMembers = RT.getNumElements();
          filterByOffset = true;
        }
        if (filterByOffset) {
          nonEmptyFieldOffsets.reserve(nonEmptyFieldOffsetsVec.size());
          nonEmptyFieldOffsets.insert(nonEmptyFieldOffsetsVec.begin(),
                                      nonEmptyFieldOffsetsVec.end());
        }
      }

      for (auto [idx, FT] : llvm::enumerate(RT.getMembers())) {
        // Skip CIR-only padding members beyond the AST field count.
        if (idx >= numRealMembers)
          continue;
        uint64_t fieldOffset = Layout.getFieldOffset(idx);
        uint64_t fieldSize = getContext().getTypeSize(FT);

        // Skip CIR members that are padding for empty AST fields.
        // CIR record lowering replaces empty struct fields with byte-array
        // padding. Match CIR member offsets against non-empty AST field
        // offsets to determine which members should be classified.
        if (filterByOffset && !nonEmptyFieldOffsets.count(fieldOffset))
          continue;

        uint64_t Offset = OffsetBase + fieldOffset;
        bool isBaseClass = llvm::is_contained(baseClassOffsets, fieldOffset);

        // Check if this CIR member is a bitfield storage unit. In CIR,
        // bitfields are lowered to integer storage types by
        // CIRRecordLowering::accumulateBitFields. We detect bitfield storage
        // by checking if the member offset matches any AST bitfield offset.
        bool isBitFieldStorage =
            bitfieldStorageOffsets.count(fieldOffset) != 0;

        if (isBaseClass) {
          // Base class classification (OGCG X86.cpp:2070-2098).
          // Classify the base first, then apply the >128 check only when
          // returnCXXRecordGreaterThan128InMem() is true (Clang > 20, non-PS).
          // No alignment check for base class subobjects.
          Class FieldLo, FieldHi;
          classify(FT, Offset, FieldLo, FieldHi, isNamedArg);
          Lo = merge(Lo, FieldLo);
          Hi = merge(Hi, FieldHi);
          if (returnCXXRecordGreaterThan128InMemFlag &&
              (sizeExceeds128 && (Size != fieldSize || sizeExceedsNativeVector))) {
            Lo = Class::Memory;
          }
          if (Lo == Class::Memory || Hi == Class::Memory) {
            postMerge(Size, Lo, Hi);
            return;
          }
        } else {
          // Field classification (OGCG X86.cpp:2107-2175).
          // AMD64-ABI 3.2.3p2: Rule 1. If the size of an object is larger than
          // eight eightbytes, or it contains unaligned fields, it has class
          // MEMORY.
          //
          // The only case a 256-bit or a 512-bit wide vector could be used is
          // when the record contains a single 256-bit or 512-bit element. Early
          // check and fallback to memory.
          //
          // FIXME: Extended the Lo and Hi logic properly to work for size wider
          // than 128.
          if (sizeExceeds128 &&
              ((!IsUnion && Size != fieldSize) || sizeExceedsNativeVector)) {
            Lo = Class::Memory;
            postMerge(Size, Lo, Hi);
            return;
          }
          uint64_t fieldAlign = getContext().getTypeAlign(FT);
          // If the field is not aligned to its type's natural alignment, the
          // record must be passed in memory.
          // Skip this check for bitfield storage — bitfields do not force the
          // structure to be passed in memory even if unaligned, and can
          // straddle an eightbyte. OGCG has the equivalent check at
          // X86.cpp:2136: "if (!BitField && IsInMemory)".
          if (!isBitFieldStorage && Offset % fieldAlign) {
            Lo = Class::Memory;
            postMerge(Size, Lo, Hi);
            return;
          }

          // Classify this field.
          //
          // AMD64-ABI 3.2.3p2: Rule 3. If the size of the aggregate
          // exceeds a single eightbyte, each is classified
          // separately. Each eightbyte gets initialized to class
          // NO_CLASS.
          Class FieldLo, FieldHi;
          classify(FT, Offset, FieldLo, FieldHi, isNamedArg);
          Lo = merge(Lo, FieldLo);
          Hi = merge(Hi, FieldHi);
          if (Lo == Class::Memory || Hi == Class::Memory)
            break;
        }
      }

      postMerge(Size, Lo, Hi);
    } else {
      llvm::outs() << "Missing X86 classification for type " << Ty << "\n";
      cir_cconv_unreachable("NYI");
    }
    // FIXME: _Decimal32 and _Decimal64 are SSE.
    // FIXME: _float128 and _Decimal128 are (SSE, SSEUp).
    return;
  }

  llvm::outs() << "Missing X86 classification for non-builtin types\n";
  cir_cconv_unreachable("NYI");
}

ABIArgInfo X86_64ABIInfo::getIndirectReturnResult(mlir::Type ty,
                                                  bool isBitIntType) const {
  // If this is a scalar LLVM value then assume LLVM will pass it in the right
  // place naturally.
  if (!isAggregateTypeForABI(ty)) {
    if (isBitIntType)
      return ABIArgInfo::getIndirect(getIndirectBitIntAlign(ty, isBitIntType));

    return (isPromotableIntegerTypeForABI(ty) ? ABIArgInfo::getExtend(ty)
                                              : ABIArgInfo::getDirect());
  }

  return getNaturalAlignIndirect(ty);
}

bool X86_64ABIInfo::isIllegalVectorType(mlir::Type Ty) const {
  if (auto VT = mlir::dyn_cast<cir::VectorType>(Ty)) {
    uint64_t Size = getContext().getTypeSize(VT);
    unsigned LargestVector = getNativeVectorSizeForAVXABI(AVXLevel);
    if (Size <= 64 || Size > LargestVector)
      return true;
    mlir::Type EltTy = VT.getElementType();
    if (passInt128VectorsInMem()) {
      if (auto intTy = mlir::dyn_cast<IntType>(EltTy))
        if (intTy.getWidth() == 128)
          return true;
    }
  }

  return false;
}

unsigned X86_64ABIInfo::getIndirectBitIntAlign(mlir::Type ty,
                                               bool isBitIntType) const {
  if (isBitIntType) {
    if (auto intTy = mlir::dyn_cast<cir::IntType>(ty))
      return std::max(getTarget().getBitIntAlign(intTy.getWidth()) / 8, 8U);
  }
  return std::max(getContext().getTypeAlign(ty) / 8, 8U);
}

ABIArgInfo X86_64ABIInfo::getIndirectResult(mlir::Type ty, bool isBitIntType,
                                            bool isMatrixType,
                                            unsigned freeIntRegs) const {
  // If this is a scalar LLVM value then assume LLVM will pass it in the right
  // place naturally.
  //
  // This assumption is optimistic, as there could be free registers available
  // when we need to pass this argument in memory, and LLVM could try to pass
  // the argument in the free register. This does not seem to happen currently,
  // but this code would be much safer if we could mark the argument with
  // 'onstack'. See PR12193.
  //
  if (!isAggregateTypeForABI(ty) &&
      (!isIllegalVectorType(ty) || isMatrixType) && !isBitIntType) {
    // FIXME: Handling enum type?

    return (isPromotableIntegerTypeForABI(ty) ? ABIArgInfo::getExtend(ty)
                                              : ABIArgInfo::getDirect());
  }

  if (CIRCXXABI::RecordArgABI RAA = getRecordArgABI(ty, getCXXABI()))
    return getNaturalAlignIndirect(ty, RAA == CIRCXXABI::RAA_DirectInMemory);

  // Compute the byval alignment. We specify the alignment of the byval in all
  // cases so that the mid-level optimizer knows the alignment of the byval.
  unsigned align = getIndirectBitIntAlign(ty, isBitIntType);

  // Attempt to avoid passing indirect results using byval when possible. This
  // is important for good codegen.
  //
  // We do this by coercing the value into a scalar type which the backend can
  // handle naturally (i.e., without using byval).
  //
  // For simplicity, we currently only do this when we have exhausted all of the
  // free integer registers. Doing this when there are free integer registers
  // would require more care, as we would have to ensure that the coerced value
  // did not claim the unused register. That would require either reording the
  // arguments to the function (so that any subsequent inreg values came first),
  // or only doing this optimization when there were no following arguments that
  // might be inreg.
  //
  // We currently expect it to be rare (particularly in well written code) for
  // arguments to be passed on the stack when there are still free integer
  // registers available (this would typically imply large records being passed
  // by value), so this seems like a fair tradeoff for now.
  //
  // We can revisit this if the backend grows support for 'onstack' parameter
  // attributes. See PR12193.
  if (freeIntRegs == 0) {
    uint64_t size = getContext().getTypeSize(ty);

    // If this type fits in an eightbyte, coerce it into the matching integral
    // type, which will end up on the stack (with alignment 8).
    if (align == 8 && size <= 64)
      return ABIArgInfo::getDirect(
          cir::IntType::get(LT.getMLIRContext(), size, false));
  }

  return ABIArgInfo::getIndirect(align);
}

bool X86_64ABIInfo::isX86VectorTypeForVectorCall(mlir::Type Ty) const {
  return ::cir::isX86VectorTypeForVectorCall(Ty, getContext(), getTarget());
}

bool X86_64ABIInfo::isHomogeneousAggregateForVectorCall(
    mlir::Type Ty, mlir::Type &Base, uint64_t &Members) const {
  return ::cir::isHomogeneousAggregateForVectorCall(
      Ty, Base, Members, getContext(), getTarget());
}

ABIArgInfo X86_64ABIInfo::classifyWindowsType(mlir::Type Ty,
                                              unsigned &FreeSSERegs,
                                              bool IsReturnType,
                                              bool IsVectorCall) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (mlir::isa<VoidType>(Ty))
    return ABIArgInfo::getIgnore();

  if (mlir::isa<BoolType>(Ty))
    return ABIArgInfo::getExtend(Ty);

  if (auto RT = mlir::dyn_cast<RecordType>(Ty)) {
    if (!IsReturnType) {
      if (CIRCXXABI::RecordArgABI RAA = getRecordArgABI(RT, getCXXABI()))
        return getNaturalAlignIndirect(
            Ty, RAA == CIRCXXABI::RAA_DirectInMemory);
    }

    if (auto Ast = RT.getAst()) {
      if (const clang::RecordDecl *RD = Ast.getRawDecl();
          RD && RD->hasFlexibleArrayMember())
        return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
    }
  }

  mlir::Type Base = {};
  uint64_t NumElts = 0;
  if (IsVectorCall &&
      isHomogeneousAggregateForVectorCall(Ty, Base, NumElts)) {
    if (FreeSSERegs >= NumElts &&
        (IsReturnType || isX86VectorTypeForVectorCall(Ty))) {
      FreeSSERegs -= NumElts;
      return ABIArgInfo::getDirect(
          Ty, /*offset=*/0, /*padding=*/nullptr,
          /*canBeFlattened=*/!mlir::isa<RecordType>(Ty));
    }

    if (!IsReturnType && !isX86VectorTypeForVectorCall(Ty))
      return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
  }

  uint64_t Width = getContext().getTypeSize(Ty);
  if (Width == 0)
    return ABIArgInfo::getIgnore();

  if (mlir::isa<RecordType, ComplexType, MethodType, DataMemberType>(Ty)) {
    if (Width > 64 || !llvm::isPowerOf2_64(Width))
      return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

    return ABIArgInfo::getDirect(
        IntType::get(LT.getMLIRContext(), Width, /*isSigned=*/false));
  }

  if (mlir::isa<LongDoubleType>(Ty) &&
      getTarget().getTriple().isWindowsGNUEnvironment() &&
      &getTarget().getLongDoubleFormat() == &llvm::APFloat::x87DoubleExtended())
    return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

  if (auto IntTy = mlir::dyn_cast<IntType>(Ty)) {
    if (IntTy.getWidth() > 64)
      return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
  }

  return ABIArgInfo::getDirect(Ty);
}

ABIArgInfo X86_64ABIInfo::reclassifyHvaArgForVectorCall(
    mlir::Type Ty, unsigned &FreeSSERegs, const ABIArgInfo &Current) const {
  if (!mlir::isa<RecordType, ArrayType, ComplexType>(Ty))
    return Current;

  mlir::Type Base = {};
  uint64_t NumElts = 0;
  if (isHomogeneousAggregateForVectorCall(Ty, Base, NumElts) &&
      FreeSSERegs >= NumElts) {
    FreeSSERegs -= NumElts;
    return getDirectX86Hva(Ty);
  }

  return Current;
}

/// The ABI specifies that a value should be passed in a full vector XMM/YMM
/// register.  Pick a CIR type that will be passed as a vector register.
mlir::Type X86_64ABIInfo::GetByteVectorType(mlir::Type Ty) const {
  // Wrapper structs/arrays that only contain a single ABI-significant element
  // are passed just like that element; strip them off if present. Keep the
  // size equality check so tail padding disqualifies the wrapper, matching
  // OGCG's isSingleElementStruct behavior.
  if (auto RT = mlir::dyn_cast<RecordType>(Ty)) {
    auto members = RT.getMembers();
    if (members.size() == 1) {
      mlir::Type inner = members[0];
      // Recurse to handle nested wrappers.
      if (getContext().getTypeSize(Ty) == getContext().getTypeSize(inner))
        return GetByteVectorType(inner);
    }
  }
  if (auto AT = mlir::dyn_cast<ArrayType>(Ty)) {
    if (AT.getSize() == 1)
      return GetByteVectorType(AT.getElementType());
  }

  if (auto vecTy = mlir::dyn_cast<VectorType>(Ty)) {
    // Keep parity with OGCG's GetByteVectorType: on Linux/NetBSD with
    // Clang ABI > 9, 128-bit vectors of i128 are not passed in their native
    // one-lane shape. Coerce them to a vector of i64 lanes instead so the
    // backend sees the CLANG10ABI128-compatible register type.
    if (passInt128VectorsInMem()) {
      if (auto intTy = mlir::dyn_cast<IntType>(vecTy.getElementType())) {
        if (intTy.getWidth() == 128) {
          uint64_t size = getContext().getTypeSize(Ty);
          return cir::VectorType::get(
              IntType::get(LT.getMLIRContext(), 64, intTy.isSigned()),
              size / 64);
        }
      }
    }

    return Ty;
  }

  // Match OGCG x86_64 GetByteVectorType: __float128 / _Float128 and IEEEquad
  // long double already have the preferred ABI register type and must not be
  // coerced into a vector-of-double shape.
  if (mlir::isa<FP128Type>(Ty))
    return Ty;
  if (auto longDoubleTy = mlir::dyn_cast<LongDoubleType>(Ty))
    if (mlir::isa<FP128Type>(longDoubleTy.getUnderlying()))
      return Ty;

  // We couldn't find the preferred IR vector type for 'Ty'.
  uint64_t Size = getContext().getTypeSize(Ty);
  assert((Size == 128 || Size == 256 || Size == 512) && "Invalid type found!");

  // Return a CIR vector type based on the size of 'Ty'.
  return cir::VectorType::get(DoubleType::get(LT.getMLIRContext()), Size / 64);
}

/// Return a type that will be passed by the backend in the low 8 bytes of an
/// XMM register, corresponding to the SSE class.
mlir::Type X86_64ABIInfo::GetSSETypeAtOffset(mlir::Type IRType,
                                             unsigned IROffset,
                                             mlir::Type SourceTy,
                                             unsigned SourceOffset) const {
  const cir::CIRDataLayout &TD = getDataLayout();
  unsigned SourceSize =
      (unsigned)getContext().getTypeSize(SourceTy) / 8 - SourceOffset;
  mlir::Type T0 = getFPTypeAtOffset(IRType, IROffset, TD);
  if (!T0 || mlir::isa<DoubleType>(T0))
    return cir::DoubleType::get(LT.getMLIRContext());

  // Get the adjacent FP type.
  mlir::Type T1 = {};
  unsigned T0Size = TD.getTypeAllocSize(T0);
  if (SourceSize > T0Size)
    T1 = getFPTypeAtOffset(IRType, IROffset + T0Size, TD);
  if (T1 == nullptr) {
    // Check if IRType is a half/bfloat + float. float type will be in
    // IROffset+4 due to its alignment.
    if (mlir::isa<FP16Type, BF16Type>(T0) && SourceSize > 4)
      T1 = getFPTypeAtOffset(IRType, IROffset + 4, TD);
    // If we can't get a second FP type, return a simple half or float.
    // avx512fp16-abi.c:pr51813_2 shows it works to return float for
    // {float, i8} too.
    if (T1 == nullptr)
      return T0;
  }

  if (mlir::isa<SingleType>(T0) && mlir::isa<SingleType>(T1))
    return cir::VectorType::get(T0, 2);

  // Two 16-bit FP types → <2 x half> or <4 x half>.
  if (mlir::isa<FP16Type, BF16Type>(T0) && mlir::isa<FP16Type, BF16Type>(T1)) {
    // Check if there's a third/fourth 16-bit FP at offset +4.
    mlir::Type T2 = nullptr;
    if (SourceSize > 4)
      T2 = getFPTypeAtOffset(IRType, IROffset + 4, TD);
    if (!T2)
      return cir::VectorType::get(T0, 2);
    return cir::VectorType::get(T0, 4);
  }

  // Mixed half + other → <4 x half>.
  if (mlir::isa<FP16Type, BF16Type>(T0) || mlir::isa<FP16Type, BF16Type>(T1))
    return cir::VectorType::get(FP16Type::get(LT.getMLIRContext()), 4);

  return cir::DoubleType::get(LT.getMLIRContext());
}

/// The ABI specifies that a value should be passed in an 8-byte GPR.  This
/// means that we either have a scalar or we are talking about the high or low
/// part of an up-to-16-byte record.  This routine picks the best CIR type
/// to represent this, which may be i64 or may be anything else that the
/// backend will pass in a GPR that works better (e.g. i8, %foo*, etc).
///
/// PrefType is an CIR type that corresponds to (part of) the IR type for
/// the source type.  IROffset is an offset in bytes into the CIR type that
/// the 8-byte value references.  PrefType may be null.
///
/// SourceTy is the source-level type for the entire argument.  SourceOffset
/// is an offset into this that we're processing (which is always either 0 or
/// 8).
///
mlir::Type X86_64ABIInfo::GetINTEGERTypeAtOffset(mlir::Type DestTy,
                                                 unsigned IROffset,
                                                 mlir::Type SourceTy,
                                                 unsigned SourceOffset) const {
  // If we're dealing with an un-offset CIR type, then it means that we're
  // returning an 8-byte unit starting with it. See if we can safely use it.
  if (IROffset == 0) {
    // Pointers and int64's always fill the 8-byte unit.
    if (auto ptrTy = mlir::dyn_cast<PointerType>(DestTy)) {
      unsigned bitWidth =
          ptrTy.getTypeSizeInBits(getDataLayout().layout, {}).getFixedValue();
      if (bitWidth == 64)
        return DestTy;
      if (bitWidth == 32 &&
          BitsContainNoUserData(SourceTy, SourceOffset * 8 + bitWidth,
                                SourceOffset * 8 + 64, getContext()))
        return DestTy;
      cir_cconv_unreachable("NYI");
    }

    // If we have a 1/2/4-byte integer, we can use it only if the rest of the
    // goodness in the source type is just tail padding.  This is allowed to
    // kick in for struct {double,int} on the int, but not on
    // struct{double,int,int} because we wouldn't return the second int.  We
    // have to do this analysis on the source type because we can't depend on
    // unions being lowered a specific way etc.
    if (auto intTy = mlir::dyn_cast<IntType>(DestTy)) {
      // Pointers and int64's always fill the 8-byte unit.
      if (intTy.getWidth() == 64)
        return DestTy;

      if (intTy.getWidth() == 8 || intTy.getWidth() == 16 ||
          intTy.getWidth() == 32) {
        unsigned BitWidth = intTy.getWidth();
        if (BitsContainNoUserData(SourceTy, SourceOffset * 8 + BitWidth,
                                  SourceOffset * 8 + 64, getContext()))
          return DestTy;
      }
    }

    // CIR's BoolType is distinct from IntType. In OGCG, bool is already
    // lowered to LLVM i8 by this point, so isIntegerTy(8) catches it. We
    // must handle it explicitly here — bool occupies 1 byte in memory.
    // Return IntType(8) rather than BoolType to match OGCG's ABI coercion
    // behavior (the result is used as a coercion type, not a semantic type).
    if (mlir::isa<BoolType>(DestTy)) {
      if (BitsContainNoUserData(SourceTy, SourceOffset * 8 + 8,
                                SourceOffset * 8 + 64, getContext()))
        return IntType::get(LT.getMLIRContext(), 8, /*isSigned=*/false);
    }
  }

  if (auto RT = mlir::dyn_cast<RecordType>(DestTy)) {
    // For unions, all CIR members are at offset 0. OGCG operates on LLVM IR
    // types where unions are represented as { largest_member, [padding] }, so
    // its field iteration naturally picks the largest member. We must do this
    // explicitly for CIR since all original union members are preserved.
    if (RT.isUnion()) {
      mlir::Type largest = RT.getLargestMember(getDataLayout().layout);
      return GetINTEGERTypeAtOffset(largest, IROffset, SourceTy, SourceOffset);
    }

    // If this is a struct, recurse into the field that covers the requested
    // byte offset. Field offsets from CIRRecordLayout are in bits.
    // Like OGCG, we unconditionally recurse into the matching field so that
    // nested records (e.g., base class subobjects) and small integer fields
    // are narrowed properly for the ABI coercion type.
    const CIRRecordLayout &Layout = getContext().getCIRRecordLayout(RT);
    auto members = RT.getMembers();
    for (unsigned i = 0, e = members.size(); i < e; ++i) {
      uint64_t fieldOffsetBytes = Layout.getFieldOffset(i) / 8;
      uint64_t nextOffsetBytes =
          (i + 1 < e) ? Layout.getFieldOffset(i + 1) / 8
                      : (unsigned)getContext().getTypeSizeInChars(RT).getQuantity();
      if (fieldOffsetBytes <= IROffset && IROffset < nextOffsetBytes) {
        return GetINTEGERTypeAtOffset(members[i],
                                      IROffset - (unsigned)fieldOffsetBytes,
                                      SourceTy, SourceOffset);
      }
    }
  }

  if (auto AT = mlir::dyn_cast<ArrayType>(DestTy)) {
    mlir::Type EltTy = AT.getElementType();
    unsigned EltSize = getDataLayout().getTypeAllocSize(EltTy);
    unsigned EltOffset = IROffset / EltSize * EltSize;
    return GetINTEGERTypeAtOffset(EltTy, IROffset - EltOffset, SourceTy,
                                  SourceOffset);
  }

  // For complex types, recurse into the element at the given offset.
  if (auto CT = mlir::dyn_cast<ComplexType>(DestTy)) {
    mlir::Type EltTy = CT.getElementType();
    unsigned EltSize = getDataLayout().getTypeAllocSize(EltTy);
    if (IROffset < EltSize)
      return GetINTEGERTypeAtOffset(EltTy, IROffset, SourceTy, SourceOffset);
    return GetINTEGERTypeAtOffset(EltTy, IROffset - EltSize, SourceTy,
                                  SourceOffset);
  }

  // If we have a 128-bit integer, we can pass it safely using an i128
  // so we return that.
  if (auto intTy = mlir::dyn_cast<IntType>(DestTy)) {
    if (intTy.getWidth() == 128) {
      cir_cconv_assert(IROffset == 0);
      return DestTy;
    }
  }

  // Okay, we don't have any better idea of what to pass, so we pass this in
  // an integer register that isn't too big to fit the rest of the record.
  unsigned TySizeInBytes =
      (unsigned)getContext().getTypeSizeInChars(SourceTy).getQuantity();

  // FIXME(cir): Temporary workaround to make things non-blocking.
  if (!cirCConvAssertionMode)
    cir_cconv_assert(TySizeInBytes != SourceOffset && "Empty field?");

  // It is always safe to classify this as an integer type up to i64 that
  // isn't larger than the record.
  // FIXME(cir): Perhaps we should have the concept of singless integers in
  // CIR, mostly because coerced types should carry sign. On the other hand,
  // this might not make a difference in practice. For now, we just preserve the
  // sign as is to avoid unecessary bitcasts.
  bool isSigned = false;
  if (auto intTy = mlir::dyn_cast<IntType>(SourceTy))
    isSigned = intTy.isSigned();
  return IntType::get(LT.getMLIRContext(),
                      std::min(TySizeInBytes - SourceOffset, 8U) * 8, isSigned);
}

/// GetX86_64ByValArgumentPair - Given a high and low type that can ideally
/// be used as elements of a two register pair to pass or return, return a
/// first class aggregate to represent them.  For example, if the low part of
/// a by-value argument should be passed as i32* and the high part as float,
/// return {i32*, float}.
static mlir::Type GetX86_64ByValArgumentPair(mlir::Type lo, mlir::Type hi,
                                             const cir::CIRDataLayout &td) {
  // In order to correctly satisfy the ABI, we need to the high part to start
  // at offset 8.  If the high and low parts we inferred are both 4-byte types
  // (e.g. i32 and i32) then the resultant record type ({i32,i32}) won't have
  // the second element at offset 8.  Check for this:
  unsigned loSize = (unsigned)td.getTypeAllocSize(lo);
  llvm::Align highAlign = td.getABITypeAlign(hi);
  unsigned highStart = llvm::alignTo(loSize, highAlign);
  assert(highStart != 0 && highStart <= 8 && "Invalid x86-64 argument pair!");

  // To handle this, we have to increase the size of the low part so that the
  // second element will start at an 8 byte offset.  We can't increase the size
  // of the second element because it might make us access off the end of the
  // record.
  if (highStart != 8) {
    // There are usually two sorts of types the ABI generation code can produce
    // for the low part of a pair that aren't 8 bytes in size: half, float or
    // i8/i16/i32.  This can also include pointers when they are 32-bit (X32 and
    // NaCl).
    // Promote these to a larger type.
    if (mlir::isa<FP16Type, SingleType>(lo))
      lo = DoubleType::get(lo.getContext());
    else {
      assert((mlir::isa<IntType, PointerType>(lo)) &&
             "Invalid/unknown lo type");
      // TODO(cir): does the sign of the int64 type matter here?
      lo = IntType::get(lo.getContext(), 64, true);
    }
  }

  auto result = RecordType::get(lo.getContext(), {lo, hi}, /*packed=*/false,
                                /*padded=*/false, RecordType::Struct);

  // Verify that the second element is at an 8-byte offset.
  // TODO(cir): Port CIRRecordLayout to upstream to enable this check.
  // assert(td.getRecordLayout(result)->getElementOffset(1) == 8 &&
  //        "Invalid x86-64 argument pair!");
  return result;
}

cir::ABIArgInfo X86_64ABIInfo::classifyReturnType(mlir::Type RetTy,
                                                  bool isBitIntType) const {
  // AMD64-ABI 3.2.3p4: Rule 1. Classify the return type with the
  // classification algorithm.
  X86_64ABIInfo::Class Lo, Hi;
  classify(RetTy, 0, Lo, Hi, true);

  // Check some invariants.
  cir_cconv_assert((Hi != Class::Memory || Lo == Class::Memory) &&
                   "Invalid memory classification.");
  cir_cconv_assert((Hi != Class::SSEUp || Lo == Class::SSE) &&
                   "Invalid SSEUp classification.");

  mlir::Type resType = {};
  switch (Lo) {
  case Class::NoClass:
    if (Hi == Class::NoClass)
      return ABIArgInfo::getIgnore();
    break;

  case Class::Integer:
    resType = GetINTEGERTypeAtOffset(RetTy, 0, RetTy, 0);

    // If we have a sign or zero extended integer, make sure to return Extend
    // so that the parameter gets the right LLVM IR attributes.
    if (Hi == Class::NoClass && mlir::isa<IntType>(resType)) {
      // NOTE(cir): We skip enum types handling here since CIR represents
      // enums directly as their unerlying integer types. NOTE(cir): For some
      // reason, Clang does not set the coerce type here and delays it to
      // arrangeLLVMFunctionInfo. We do the same to keep parity.
      if (mlir::isa<IntType, BoolType>(RetTy) &&
          isPromotableIntegerTypeForABI(RetTy))
        return ABIArgInfo::getExtend(RetTy);
    }

    // i128 are passed directly.
    if (auto intTy = mlir::dyn_cast<IntType>(resType))
      if (intTy.getWidth() == 128) {
        cir_cconv_assert(Hi == Class::Integer);
        return ABIArgInfo::getDirect(resType);
      }
    break;
    // available SSE register of the sequence %xmm0, %xmm1 is used.
  case Class::SSE:
    resType = GetSSETypeAtOffset(RetTy, 0, RetTy, 0);
    break;

    // AMD64-ABI 3.2.3p4: Rule 6. If the class is X87, the value is
    // returned on the X87 stack in %st0 as 80-bit x87 number.
  case Class::X87:
    resType = LongDoubleType::get(LT.getMLIRContext(),
                                  FP80Type::get(LT.getMLIRContext()));
    break;

    // AMD64-ABI 3.2.3p4: Rule 8. If the class is COMPLEX_X87, the real
    // part of the value is returned in %st0 and the imaginary part in
    // %st1.
  case Class::ComplexX87: {
    cir_cconv_assert(Hi == Class::ComplexX87 &&
                     "Unexpected ComplexX87 classification.");
    // Complex x87 is returned as {x86_fp80, x86_fp80}. Extract the
    // element type from the CIR complex type.
    auto complexTy = mlir::cast<ComplexType>(RetTy);
    mlir::Type eltTy = complexTy.getElementType();
    resType = RecordType::get(LT.getMLIRContext(), {eltTy, eltTy},
                              /*packed=*/false, /*padded=*/false,
                              RecordType::Struct);
    break;
  }

    // AMD64-ABI 3.2.3p4: Rule 2. Types of class memory are returned via
    // hidden argument.
  case Class::Memory:
    return getIndirectReturnResult(RetTy, isBitIntType);

  case Class::SSEUp:
  case Class::X87Up:
    llvm_unreachable("Invalid classification for lo word.");
  }

  mlir::Type HighPart = {};
  switch (Hi) {

  case Class::ComplexX87: // Previously handled.
  case Class::NoClass:
    break;

  case Class::Integer:
    HighPart = GetINTEGERTypeAtOffset(RetTy, 8, RetTy, 8);
    if (Lo == Class::NoClass) // Return HighPart at offset 8 in memory.
      return ABIArgInfo::getDirect(HighPart, 8);
    break;

  case Class::SSE:
    HighPart = GetSSETypeAtOffset(RetTy, 8, RetTy, 8);
    if (Lo == Class::NoClass) // Return HighPart at offset 8 in memory.
      return ABIArgInfo::getDirect(HighPart, 8);
    break;

    // AMD64-ABI 3.2.3p4: Rule 7. If the class is X87UP, the value is
    // returned together with the previous X87 value in %st0.
  case Class::X87Up:
    // If X87Up is preceded by X87, we don't need to do
    // anything. However, in some cases with unions it may not be
    // preceded by X87. In such situations we follow gcc and pass the
    // extra bits in an SSE reg.
    if (Lo != Class::X87) {
      HighPart = GetSSETypeAtOffset(RetTy, 8, RetTy, 8);
      if (Lo == Class::NoClass) // Return HighPart at offset 8 in memory.
        return ABIArgInfo::getDirect(HighPart, 8);
    }
    break;

    // AMD64-ABI 3.2.3p4: Rule 5. If the class is SSEUP, the eightbyte
    // is passed in the next available eightbyte chunk if the last used
    // vector register.
    //
    // SSEUP should always be preceded by SSE, just widen.
  case Class::SSEUp:
    assert(Lo == Class::SSE && "Unexpected SSEUp classification.");
    resType = GetByteVectorType(RetTy);
    break;

  default:
    cir_cconv_unreachable("NYI");
  }

  // If a high part was specified, merge it together with the low part.  It is
  // known to pass in the high eightbyte of the result.  We do this by forming
  // a first class record aggregate with the high and low part: {low, high}
  if (HighPart)
    resType = GetX86_64ByValArgumentPair(resType, HighPart, getDataLayout());

  return ABIArgInfo::getDirect(resType);
}

ABIArgInfo X86_64ABIInfo::classifyArgumentType(
    mlir::Type Ty, bool isBitIntType, bool isMatrixType,
    unsigned freeIntRegs,
    unsigned &neededInt,
    unsigned &neededSSE, bool isNamedArg, bool IsRegCall = false) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  X86_64ABIInfo::Class Lo, Hi;
  classify(Ty, 0, Lo, Hi, isNamedArg, IsRegCall);

  // Check some invariants.
  // FIXME: Enforce these by construction.
  cir_cconv_assert((Hi != Class::Memory || Lo == Class::Memory) &&
                   "Invalid memory classification.");
  cir_cconv_assert((Hi != Class::SSEUp || Lo == Class::SSE) &&
                   "Invalid SSEUp classification.");

  neededInt = 0;
  neededSSE = 0;
  mlir::Type ResType = {};
  switch (Lo) {
  case Class::NoClass:
    if (Hi == Class::NoClass)
      return ABIArgInfo::getIgnore();
    // If the low part is just padding, it takes no register, leave ResType
    // null.
    cir_cconv_assert(
        (Hi == Class::SSE || Hi == Class::Integer || Hi == Class::X87Up) &&
        "Unknown missing lo part");
    break;

    // AMD64-ABI 3.2.3p3: Rule 2. If the class is INTEGER, the next
    // available register of the sequence %rdi, %rsi, %rdx, %rcx, %r8
    // and %r9 is used.
  case Class::Integer:
    ++neededInt;

    // Pick an 8-byte type based on the preferred type.
    ResType = GetINTEGERTypeAtOffset(Ty, 0, Ty, 0);

    // If we have a sign or zero extended integer, make sure to return Extend
    // so that the parameter gets the right LLVM IR attributes.
    if (Hi == Class::NoClass && mlir::isa<IntType>(ResType)) {
      // NOTE(cir): We skip enum types handling here since CIR represents
      // enums directly as their unerlying integer types. NOTE(cir): For some
      // reason, Clang does not set the coerce type here and delays it to
      // arrangeLLVMFunctionInfo. We do the same to keep parity.
      if (mlir::isa<IntType, BoolType>(Ty) && isPromotableIntegerTypeForABI(Ty))
        return ABIArgInfo::getExtend(Ty);
    }

    // i128 are passed directly.
    if (auto intTy = mlir::dyn_cast<IntType>(ResType))
      if (intTy.getWidth() == 128) {
        cir_cconv_assert(Hi == Class::Integer);
        ++neededInt;
        return ABIArgInfo::getDirect(ResType);
      }

    break;

    // AMD64-ABI 3.2.3p3: Rule 3. If the class is SSE, the next
    // available SSE register is used, the registers are taken in the
    // order from %xmm0 to %xmm7.
  case Class::SSE: {
    ResType = GetSSETypeAtOffset(Ty, 0, Ty, 0);
    ++neededSSE;
    break;
  }
  // AMD64-ABI 3.2.3p3: Rule 1. If the class is MEMORY, pass the argument
  // on the stack.
  case Class::Memory:

  // AMD64-ABI 3.2.3p3: Rule 5. If the class is X87, X87UP or
  // COMPLEX_X87, it is passed in memory.
  case Class::X87:
  case Class::ComplexX87:
    if (getRecordArgABI(Ty, getCXXABI()) == CIRCXXABI::RAA_Indirect)
      ++neededInt;
    return getIndirectResult(Ty, isBitIntType, isMatrixType, freeIntRegs);

  case Class::SSEUp:
  case Class::X87Up:
    llvm_unreachable("Invalid classification for lo word.");

  }

  mlir::Type HighPart = {};
  switch (Hi) {
  case Class::Memory:
  case Class::X87:
  case Class::ComplexX87:
    llvm_unreachable("Invalid classification for hi word.");

  case Class::NoClass:
    break;

  case Class::Integer:
    ++neededInt;
    // Pick an 8-byte type based on the preferred type.
    HighPart = GetINTEGERTypeAtOffset(Ty, 8, Ty, 8);

    if (Lo == Class::NoClass) // Pass HighPart at offset 8 in memory.
      return ABIArgInfo::getDirect(HighPart, 8);
    break;

  // X87Up generally doesn't occur here (long double is passed in
  // memory), except in situations involving unions.
  case Class::X87Up:
  case Class::SSE:
    ++neededSSE;
    HighPart = GetSSETypeAtOffset(Ty, 8, Ty, 8);

    if (Lo == Class::NoClass) // Pass HighPart at offset 8 in memory.
      return ABIArgInfo::getDirect(HighPart, 8);
    break;

  // AMD64-ABI 3.2.3p3: Rule 4. If the class is SSEUP, the
  // eightbyte is passed in the upper half of the last used SSE
  // register.  This only happens when 128-bit vectors are passed.
  case Class::SSEUp:
    assert(Lo == Class::SSE && "Unexpected SSEUp classification");
    ResType = GetByteVectorType(Ty);
    break;
  }

  // If a high part was specified, merge it together with the low part.  It is
  // known to pass in the high eightbyte of the result.  We do this by forming a
  // first class record aggregate with the high and low part: {low, high}
  if (HighPart)
    ResType = GetX86_64ByValArgumentPair(ResType, HighPart, getDataLayout());

  return ABIArgInfo::getDirect(ResType);
}

ABIArgInfo
X86_64ABIInfo::classifyRegCallStructTypeImpl(mlir::Type Ty, unsigned &neededInt,
                                             unsigned &neededSSE,
                                             unsigned &maxVectorWidth) const {
  auto RT = mlir::dyn_cast<RecordType>(Ty);
  if (!RT)
    return getIndirectReturnResult(Ty, /*isBitIntType=*/false);

  // Check for flexible array members via AST.
  if (auto ast = RT.getAst()) {
    const clang::RecordDecl *RD = ast.getRawDecl();
    if (RD->hasFlexibleArrayMember())
      return getIndirectReturnResult(Ty, /*isBitIntType=*/false);

    // Sum up bases for C++ records.
    if (const auto *CXXRD = llvm::dyn_cast<clang::CXXRecordDecl>(RD)) {
      if (CXXRD->isDynamicClass()) {
        neededInt = neededSSE = 0;
        return getIndirectReturnResult(Ty, /*isBitIntType=*/false);
      }

      // CIR flattens bases into the record's member list, so base
      // classification is handled implicitly by iterating members below.
      // This matches how CIR lowers class hierarchies.
    }
  }

  // Sum up members.
  for (auto memberTy : RT.getMembers()) {
    if (auto memberRT = mlir::dyn_cast<RecordType>(memberTy)) {
      if (classifyRegCallStructTypeImpl(memberTy, neededInt, neededSSE,
                                        maxVectorWidth)
              .isIndirect()) {
        neededInt = neededSSE = 0;
        return getIndirectReturnResult(Ty, /*isBitIntType=*/false);
      }
    } else {
      unsigned localNeededInt, localNeededSSE;
      if (classifyArgumentType(memberTy, /*isBitIntType=*/false,
                               /*isMatrixType=*/false, UINT_MAX,
                               localNeededInt,
                               localNeededSSE,
                               /*isNamedArg=*/true, /*IsRegCall=*/true)
              .isIndirect()) {
        neededInt = neededSSE = 0;
        return getIndirectReturnResult(Ty, /*isBitIntType=*/false);
      }
      // Track max vector width.
      if (auto vecTy = mlir::dyn_cast<cir::VectorType>(memberTy)) {
        unsigned width = getContext().getTypeSize(vecTy);
        if (width > maxVectorWidth)
          maxVectorWidth = width;
      }
      neededInt += localNeededInt;
      neededSSE += localNeededSSE;
    }
  }

  return ABIArgInfo::getDirect();
}

ABIArgInfo
X86_64ABIInfo::classifyRegCallStructType(mlir::Type Ty, unsigned &neededInt,
                                         unsigned &neededSSE,
                                         unsigned &maxVectorWidth) const {
  neededInt = 0;
  neededSSE = 0;
  maxVectorWidth = 0;

  return classifyRegCallStructTypeImpl(Ty, neededInt, neededSSE,
                                       maxVectorWidth);
}

void X86_64ABIInfo::computeInfo(LowerFunctionInfo &FI) const {
  const unsigned CallingConv = FI.getCallingConvention();

  if (CallingConv == llvm::CallingConv::Swift ||
      CallingConv == llvm::CallingConv::SwiftTail) {
    if (!cir::classifyReturnType(getCXXABI(), FI, *this))
      FI.getReturnInfo() = swiftcall::classifyType(LT, FI.getReturnType(),
                                                    /*forReturn=*/true);

    for (auto &arg : FI.arguments())
      arg.info = swiftcall::classifyType(LT, arg.type, /*forReturn=*/false);
    return;
  }

  if (CallingConv != llvm::CallingConv::X86_64_SysV &&
      CallingConv != llvm::CallingConv::X86_RegCall &&
      (CallingConv == llvm::CallingConv::Win64 ||
       getTarget().getTriple().isOSWindows())) {
    bool IsVectorCall = CallingConv == llvm::CallingConv::X86_VectorCall;

    unsigned FreeSSERegs = 0;
    if (IsVectorCall)
      FreeSSERegs = 4;

    if (!cir::classifyReturnType(getCXXABI(), FI, *this))
      FI.getReturnInfo() =
          classifyWindowsType(FI.getReturnType(), FreeSSERegs,
                              /*IsReturnType=*/true, IsVectorCall);

    if (IsVectorCall)
      FreeSSERegs = 6;

    unsigned ArgNum = 0;
    unsigned ZeroSSERegs = 0;
    for (auto &arg : FI.arguments()) {
      unsigned *MaybeFreeSSERegs =
          (IsVectorCall && ArgNum >= 6) ? &ZeroSSERegs : &FreeSSERegs;
      arg.info = classifyWindowsType(arg.type, *MaybeFreeSSERegs,
                                     /*IsReturnType=*/false, IsVectorCall);
      ++ArgNum;
    }

    if (IsVectorCall) {
      for (auto &arg : FI.arguments())
        arg.info = reclassifyHvaArgForVectorCall(arg.type, FreeSSERegs,
                                                 arg.info);
    }
    return;
  }

  bool IsRegCall = CallingConv == llvm::CallingConv::X86_RegCall;

  // Keep track of the number of assigned registers.
  unsigned FreeIntRegs = IsRegCall ? 11 : 6;
  unsigned FreeSSERegs = IsRegCall ? 16 : 8;
  unsigned NeededInt = 0, NeededSSE = 0, MaxVectorWidth = 0;

  if (!cir::classifyReturnType(getCXXABI(), FI, *this)) {
    if (IsRegCall && mlir::isa<RecordType>(FI.getReturnType()) &&
        !mlir::cast<RecordType>(FI.getReturnType()).isUnion()) {
      FI.getReturnInfo() = classifyRegCallStructType(
          FI.getReturnType(), NeededInt, NeededSSE, MaxVectorWidth);
      if (FreeIntRegs >= NeededInt && FreeSSERegs >= NeededSSE) {
        FreeIntRegs -= NeededInt;
        FreeSSERegs -= NeededSSE;
      } else {
        FI.getReturnInfo() =
            getIndirectReturnResult(FI.getReturnType(),
                                    FI.getReturnArgInfo().isBitIntType());
      }
    } else if (IsRegCall &&
               mlir::isa<ComplexType>(FI.getReturnType()) &&
               mlir::isa<LongDoubleType>(
                   mlir::cast<ComplexType>(FI.getReturnType())
                       .getElementType())) {
      // Complex Long Double Type is passed in Memory when Regcall
      // calling convention is used.
      FI.getReturnInfo() =
          getIndirectReturnResult(FI.getReturnType(),
                                  FI.getReturnArgInfo().isBitIntType());
    } else {
      FI.getReturnInfo() = classifyReturnType(
          FI.getReturnType(), FI.getReturnArgInfo().isBitIntType());
    }
  }

  // If the return value is indirect, then the hidden argument is consuming
  // one integer register.
  if (FI.getReturnInfo().isIndirect())
    --FreeIntRegs;
  else if (NeededSSE && MaxVectorWidth > 0)
    assert(!cir::MissingFeatures::maxVectorWidth());

  // The chain argument effectively gives us another free register.
  if (FI.isChainCall())
    ++FreeIntRegs;

  unsigned NumRequiredArgs = FI.getNumRequiredArgs();
  // AMD64-ABI 3.2.3p3: Once arguments are classified, the registers
  // get assigned (in left-to-right order) for passing as follows...
  unsigned ArgNo = 0;
  for (LowerFunctionInfo::arg_iterator it = FI.arg_begin(), ie = FI.arg_end();
       it != ie; ++it, ++ArgNo) {
    bool IsNamedArg = ArgNo < NumRequiredArgs;

    if (IsRegCall && mlir::isa<RecordType>(it->type) &&
        !mlir::cast<RecordType>(it->type).isUnion())
      it->info = classifyRegCallStructType(it->type, NeededInt, NeededSSE,
                                           MaxVectorWidth);
    else
      it->info =
          classifyArgumentType(it->type, it->isBitIntType(),
                               it->isMatrixType(), FreeIntRegs,
                               NeededInt, NeededSSE, IsNamedArg);

    // AMD64-ABI 3.2.3p3: If there are no registers available for any
    // eightbyte of an argument, the whole argument is passed on the
    // stack. If registers have already been assigned for some
    // eightbytes of such an argument, the assignments get reverted.
    if (FreeIntRegs >= NeededInt && FreeSSERegs >= NeededSSE) {
      FreeIntRegs -= NeededInt;
      FreeSSERegs -= NeededSSE;
    } else {
      it->info = getIndirectResult(it->type, it->isBitIntType(),
                                   it->isMatrixType(), FreeIntRegs);
    }
  }
}

X86_64ABIInfo::Class X86_64ABIInfo::merge(Class Accum, Class Field) {
  // AMD64-ABI 3.2.3p2: Rule 4. Each field of an object is
  // classified recursively so that always two fields are
  // considered. The resulting class is calculated according to
  // the classes of the fields in the eightbyte:
  //
  // (a) If both classes are equal, this is the resulting class.
  //
  // (b) If one of the classes is NO_CLASS, the resulting class is
  // the other class.
  //
  // (c) If one of the classes is MEMORY, the result is the MEMORY
  // class.
  //
  // (d) If one of the classes is INTEGER, the result is the
  // INTEGER.
  //
  // (e) If one of the classes is X87, X87UP, COMPLEX_X87 class,
  // MEMORY is used as class.
  //
  // (f) Otherwise class SSE is used.

  // Accum should never be memory (we should have returned) or
  // ComplexX87 (because this cannot be passed in a structure).
  cir_cconv_assert((Accum != Class::Memory && Accum != Class::ComplexX87) &&
                   "Invalid accumulated classification during merge.");
  if (Accum == Field || Field == Class::NoClass)
    return Accum;
  if (Field == Class::Memory)
    return Class::Memory;
  if (Accum == Class::NoClass)
    return Field;
  if (Accum == Class::Integer || Field == Class::Integer)
    return Class::Integer;
  if (Field == Class::X87 || Field == Class::X87Up ||
      Field == Class::ComplexX87 || Accum == Class::X87 ||
      Accum == Class::X87Up)
    return Class::Memory;
  return Class::SSE;
}

void X86_64ABIInfo::postMerge(unsigned AggregateSize, Class &Lo,
                              Class &Hi) const {
  // AMD64-ABI 3.2.3p2: Rule 5. Then a post merger cleanup is done:
  //
  // (a) If one of the classes is Memory, the whole argument is passed in
  //     memory.
  //
  // (b) If X87UP is not preceded by X87, the whole argument is passed in
  //     memory.
  //
  // (c) If the size of the aggregate exceeds two eightbytes and the first
  //     eightbyte isn't SSE or any other eightbyte isn't SSEUP, the whole
  //     argument is passed in memory. NOTE: This is necessary to keep the
  //     ABI working for processors that don't support the __m256 type.
  //
  // (d) If SSEUP is not preceded by SSE or SSEUP, it is converted to SSE.
  //
  // Some of these are enforced by the merging logic.  Others can arise
  // only with unions; for example:
  //   union { _Complex double; unsigned; }
  //
  // Note that clauses (b) and (c) were added in 0.98.
  //
  if (Hi == Class::Memory)
    Lo = Class::Memory;
  if (Hi == Class::X87Up && Lo != Class::X87 && honorsRevision0_98())
    Lo = Class::Memory;
  if (AggregateSize > 128 && (Lo != Class::SSE || Hi != Class::SSEUp))
    Lo = Class::Memory;
  if (Hi == Class::SSEUp && Lo != Class::SSE)
    Hi = Class::SSE;
}

std::unique_ptr<TargetLoweringInfo>
createX86_32TargetLoweringInfo(LowerModule &LM) {
  const llvm::Triple &Triple = LM.getTarget().getTriple();
  return std::make_unique<X86_32TargetLoweringInfo>(
      LM.getTypes(), Triple.isOSDarwin(), isStructReturnInRegABI(LM),
      Triple.isOSWindows() && !Triple.isOSCygMing(),
      LM.getContext().getCodeGenOpts().NumRegisterParameters,
      LM.getContext().getCodeGenOpts().FloatABI == "soft");
}

std::unique_ptr<TargetLoweringInfo>
createX86_64TargetLoweringInfo(LowerModule &LM, X86AVXABILevel AVXLevel) {
  return std::make_unique<X86_64TargetLoweringInfo>(LM.getTypes(), AVXLevel);
}

} // namespace cir
