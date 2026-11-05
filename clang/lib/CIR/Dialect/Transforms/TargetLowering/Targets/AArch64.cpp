//===- AArch64.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "clang/CIR/Target/AArch64.h"
#include "ABIInfoImpl.h"
#include "CIRRecordLayout.h"
#include "LowerFunctionInfo.h"
#include "LowerTypes.h"
#include "SwiftCallingConv.h"
#include "TargetInfo.h"
#include "TargetLoweringInfo.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecordLayout.h"
#include "clang/CIR/ABIArgInfo.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/Triple.h"

using AArch64ABIKind = cir::AArch64ABIKind;
using ABIArgInfo = cir::ABIArgInfo;
using MissingFeatures = cir::MissingFeatures;

namespace cir {

//===----------------------------------------------------------------------===//
// AArch64 ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

static bool isFixedSVEPredicateStorageVector(VectorType VT);
static mlir::Type convertFixedToScalableVectorType(VectorType VT,
                                                   const CIRLowerContext &Ctx);
static bool isAnonymousSVETupleRecord(mlir::Type Ty, unsigned &NVec,
                                      unsigned &NPred);

/// Check if a CIR type is a Homogeneous Floating-point Aggregate (HFA) or
/// Homogeneous Vector Aggregate (HVA). An HFA/HVA is a struct where all leaf
/// members are the same floating-point or short-vector type, with 1 to 4 total
/// leaf members. Handles nested structs recursively.
///
/// For vector types, only fixed-length vectors of 64 or 128 bits are valid
/// HVA base types (matching OGCG's isHomogeneousAggregateBaseType). Two vector
/// bases are considered equivalent if they have the same total size.
///
/// Returns the base type and total leaf member count if true.
static bool isHomogeneousAggregate(mlir::Type Ty, mlir::Type &Base,
                                   uint64_t &Members,
                                   const CIRLowerContext &ctx) {
  // Complex types are treated as 2-element HFAs if their element type is a
  // valid HFA base type (floating-point). This matches OGCG's
  // ABIInfo::isHomogeneousAggregate which handles ComplexType as a special case.
  if (auto CT = mlir::dyn_cast<ComplexType>(Ty)) {
    mlir::Type EltTy = CT.getElementType();
    if (!mlir::isa<SingleType, DoubleType, LongDoubleType, FP16Type, BF16Type>(
            EltTy))
      return false;
    Members = 2;
    if (!Base)
      Base = EltTy;
    else if (Base != EltTy)
      return false;
    return true;
  }

  auto RT = mlir::dyn_cast<RecordType>(Ty);
  if (!RT)
    return false;

  auto members = RT.getMembers();
  if (members.empty())
    return false;

  bool IsUnion = RT.isUnion();
  Members = 0;
  for (auto member : members) {
    uint64_t FldMembers = 0;
    if (mlir::isa<SingleType, DoubleType, LongDoubleType, FP16Type, BF16Type>(
            member)) {
      // Leaf floating-point member.
      if (!Base)
        Base = member;
      else if (Base != member)
        return false; // Mixed FP types.
      FldMembers = 1;
    } else if (auto vecTy = mlir::dyn_cast<VectorType>(member)) {
      // Vector member — accept 64-bit or 128-bit fixed-length vectors.
      if (vecTy.getIsScalable() || vecTy.getIsSveFixedLength())
        return false; // SVE vectors (scalable or fixed-length) are not HVA bases.
      uint64_t vecSize = ctx.getTypeSize(vecTy);
      if (vecSize != 64 && vecSize != 128)
        return false;
      if (!Base) {
        Base = member;
      } else if (auto baseVec = mlir::dyn_cast<VectorType>(Base)) {
        // Two vector bases are equivalent if they have the same size.
        if (ctx.getTypeSize(baseVec) != vecSize)
          return false;
      } else {
        // Mixed vector and FP base types.
        return false;
      }
      FldMembers = 1;
    } else if (mlir::isa<RecordType, ComplexType>(member)) {
      // Nested struct or complex member — recurse.
      mlir::Type nestedBase = {};
      uint64_t nestedMembers = 0;
      if (!isHomogeneousAggregate(member, nestedBase, nestedMembers, ctx))
        return false;
      if (!Base)
        Base = nestedBase;
      else if (mlir::isa<VectorType>(Base) && mlir::isa<VectorType>(nestedBase)) {
        // Vector base equivalence: same size.
        if (ctx.getTypeSize(Base) != ctx.getTypeSize(nestedBase))
          return false;
      } else if (Base != nestedBase)
        return false; // Mixed types across nesting.
      FldMembers = nestedMembers;
    } else if (auto arrTy = mlir::dyn_cast<ArrayType>(member)) {
      // Array member — flatten nested arrays and check if leaf type is valid.
      mlir::Type eltTy = arrTy.getElementType();
      uint64_t totalElts = arrTy.getSize();
      while (auto nestedArr = mlir::dyn_cast<ArrayType>(eltTy)) {
        totalElts *= nestedArr.getSize();
        eltTy = nestedArr.getElementType();
      }
      if (mlir::isa<SingleType, DoubleType, LongDoubleType, FP16Type,
                     BF16Type>(eltTy)) {
        if (!Base)
          Base = eltTy;
        else if (Base != eltTy)
          return false;
        FldMembers = totalElts;
      } else if (auto vecEltTy = mlir::dyn_cast<VectorType>(eltTy)) {
        if (vecEltTy.getIsScalable() || vecEltTy.getIsSveFixedLength())
          return false;
        uint64_t vecSize = ctx.getTypeSize(vecEltTy);
        if (vecSize != 64 && vecSize != 128)
          return false;
        if (!Base) {
          Base = eltTy;
        } else if (auto baseVec = mlir::dyn_cast<VectorType>(Base)) {
          if (ctx.getTypeSize(baseVec) != vecSize)
            return false;
        } else {
          return false;
        }
        FldMembers = totalElts;
      } else {
        return false;
      }
    } else {
      // Non-FP, non-vector, non-record, non-array member — not an HFA/HVA.
      return false;
    }
    // For unions, take the max member count (OGCG: RD->isUnion()).
    // For structs, sum all member counts.
    Members = IsUnion ? std::max(Members, FldMembers) : Members + FldMembers;
  }

  if (!Base)
    return false;

  // Ensure there is no padding (matches OGCG's padding check).
  if (ctx.getTypeSize(Base) * Members != ctx.getTypeSize(Ty))
    return false;

  return Members >= 1 && Members <= 4;
}

/// Recursively walk a CIR aggregate type to determine if it is a Pure Scalable
/// Type (PST). Collects the expanded SVE vector and `svcount_t` members into
/// CoerceToSeq. Returns true if the type contains only supported PST leaves
/// (possibly nested in arrays/records). Matches OGCG's
/// passAsPureScalableType.
///
/// Tracks both data vector register count (NVec) and predicate register count
/// (NPred) per AAPCS64.
static bool passAsPureScalableType(mlir::Type Ty, unsigned &NVec,
                                   unsigned &NPred,
                                   llvm::SmallVectorImpl<mlir::Type> &CoerceToSeq,
                                   const CIRLowerContext &context) {
  if (auto arrTy = mlir::dyn_cast<ArrayType>(Ty)) {
    uint64_t NElt = arrTy.getSize();
    if (NElt == 0)
      return false;

    unsigned NV = 0, NP = 0;
    llvm::SmallVector<mlir::Type> EltCoerceToSeq;
    if (!passAsPureScalableType(arrTy.getElementType(), NV, NP,
                                EltCoerceToSeq, context))
      return false;

    if (CoerceToSeq.size() + NElt * EltCoerceToSeq.size() > 12)
      return false;

    for (uint64_t I = 0; I < NElt; ++I)
      llvm::append_range(CoerceToSeq, EltCoerceToSeq);

    NVec += NElt * NV;
    NPred += NElt * NP;
    return true;
  }

  if (auto recTy = mlir::dyn_cast<RecordType>(Ty)) {
    // PSTs are never unions.
    if (recTy.isUnion())
      return false;

    // OGCG classifies a record's AST fields (passAsPureScalableType iterates
    // RD->fields()), which never include compiler-inserted padding. CIR, by
    // contrast, materializes tail/interstitial padding as explicit byte-array
    // members. A mixed data+predicate PST (e.g. `struct { vec d; fpred p; }`)
    // gets a trailing `[N x i8]` padding member to round the record up to the
    // SVE granule; naively walking that member makes classification fail (an
    // i8 array is not a scalable leaf) and demotes the aggregate to indirect,
    // whereas the homogeneous all-data/all-predicate cases pack with no
    // padding and classify fine. Skip the padding members by classifying only
    // the CIR members whose offset matches a real (non-empty) AST field,
    // mirroring the AST-driven view OGCG uses.
    llvm::SmallDenseSet<uint64_t, 8> realFieldOffsets;
    bool filterByOffset = false;
    if (recTy.getPadded()) {
      if (auto ast = recTy.getAst()) {
        const clang::RecordDecl *RD = ast.getRawDecl();
        const clang::ASTRecordLayout &ASTLayout =
            RD->getASTContext().getASTRecordLayout(RD);
        for (const auto *FD : RD->fields()) {
          if (FD->isUnnamedBitField())
            continue;
          realFieldOffsets.insert(
              ASTLayout.getFieldOffset(FD->getFieldIndex()));
        }
        filterByOffset = true;
      }
    }

    const CIRRecordLayout &Layout = context.getCIRRecordLayout(recTy);
    for (auto [idx, member] : llvm::enumerate(recTy.getMembers())) {
      if (filterByOffset &&
          !realFieldOffsets.contains(Layout.getFieldOffset(idx)))
        continue;
      if (!passAsPureScalableType(member, NVec, NPred, CoerceToSeq, context))
        return false;
    }
    return true;
  }

  if (auto vecTy = mlir::dyn_cast<VectorType>(Ty)) {
    if (vecTy.getIsSveFixedLength()) {
      if (isFixedSVEPredicateStorageVector(vecTy))
        ++NPred;
      else
        ++NVec;
      if (CoerceToSeq.size() + 1 > 12)
        return false;
      CoerceToSeq.push_back(convertFixedToScalableVectorType(vecTy, context));
      return true;
    }

    if (vecTy.getIsScalable()) {
      if (mlir::isa<BoolType>(vecTy.getElementType()))
        ++NPred;
      else
        ++NVec;
      if (CoerceToSeq.size() + 1 > 12)
        return false;
      CoerceToSeq.push_back(vecTy);
      return true;
    }

    return false;
  }

  if (auto opaqueTy = mlir::dyn_cast<OpaqueType>(Ty)) {
    if (opaqueTy.getTag() != OpaqueType::getAArch64SVCountTag())
      return false;
    ++NPred;
    if (CoerceToSeq.size() + 1 > 12)
      return false;
    CoerceToSeq.push_back(opaqueTy);
    return true;
  }

  return false;
}

/// Recursively flatten a CIR type into its leaf element types, preserving
/// padding arrays. Matches OGCG's AArch64ABIInfo::flattenType.
static void flattenCIRType(mlir::Type Ty,
                           llvm::SmallVectorImpl<mlir::Type> &Flattened) {
  if (ABIArgInfo::isPaddingForCoerceAndExpand(Ty)) {
    Flattened.push_back(Ty);
    return;
  }

  if (auto arrTy = mlir::dyn_cast<ArrayType>(Ty)) {
    uint64_t NElt = arrTy.getSize();
    if (NElt == 0)
      return;

    llvm::SmallVector<mlir::Type> EltFlattened;
    flattenCIRType(arrTy.getElementType(), EltFlattened);

    for (uint64_t I = 0; I < NElt; ++I)
      llvm::append_range(Flattened, EltFlattened);
    return;
  }

  if (auto recTy = mlir::dyn_cast<RecordType>(Ty)) {
    for (auto member : recTy.getMembers())
      flattenCIRType(member, Flattened);
    return;
  }

  Flattened.push_back(Ty);
}

/// CIR keeps a single `sve_fixed` bit but does not retain the original AST
/// SVE vector kind. Fixed-length SVE predicates therefore show up as compact
/// i8 storage vectors (for example, `svbool_t arm_sve_vector_bits(128)` maps
/// to `<2 x i8>`). Recognize the storage shape used by the current AAPCS64
/// parity tests so we can rebuild the scalable ABI surface type that OGCG
/// expects.
static bool isFixedSVEPredicateStorageVector(VectorType VT) {
  if (!VT.getIsSveFixedLength())
    return false;

  auto eltTy = mlir::dyn_cast<IntType>(VT.getElementType());
  if (!eltTy || eltTy.getWidth() != 8 || eltTy.isSigned())
    return false;

  return VT.getSize() < 16;
}

static mlir::Type convertFixedToScalableVectorType(VectorType VT,
                                                   const CIRLowerContext &Ctx) {
  cir_cconv_assert(VT.getIsSveFixedLength() &&
                   "expected SVE fixed-length vector");

  // A fixed-length predicate always maps to the scalable predicate surface
  // `<vscale x 16 x i1>`, independent of the fixed width (matches OGCG's
  // AArch64ABIInfo::convertFixedToScalableVectorType).
  if (isFixedSVEPredicateStorageVector(VT))
    return VectorType::get(BoolType::get(VT.getContext()), 16,
                           /*is_scalable=*/true);

  // The scalable minimum element count is normalized to the 128-bit SVE granule
  // (128 / element-bit-width), NOT the fixed-length element count. OGCG hardcodes
  // this per builtin element type (i8->16, i16->8, i32->4, i64->2, half/bf16->8,
  // float->4, double->2); using VT.getSize() would make the surface type scale
  // with `arm_sve_vector_bits(N)` and be N/128 times too wide.
  uint64_t EltBits = Ctx.getTypeSize(VT.getElementType());
  cir_cconv_assert(EltBits != 0 && "unexpected zero-width SVE element");
  unsigned ScalableMinElts = 128 / EltBits;
  return VectorType::get(VT.getElementType(), ScalableMinElts,
                         /*is_scalable=*/true);
}

/// CIRGen lowers SVE tuple builtins (for example `svfloat64x4_t` and
/// `svboolx4_t`) to anonymous records of homogeneous scalable vectors before
/// target ABI classification. OGCG still recognizes those source-level tuple
/// builtins on DarwinPCS and keeps them on the direct non-aggregate path,
/// while named PST structs remain aggregates. Reconstruct that distinction
/// structurally from CIR types so tuple builtins keep the Darwin direct ABI.
static bool isAnonymousSVETupleRecord(mlir::Type Ty, unsigned &NVec,
                                      unsigned &NPred) {
  NVec = 0;
  NPred = 0;

  auto recTy = mlir::dyn_cast<RecordType>(Ty);
  if (!recTy || recTy.getAst() || recTy.getName() || recTy.isUnion())
    return false;

  llvm::ArrayRef<mlir::Type> members = recTy.getMembers();
  if (members.size() < 2 || members.size() > 4)
    return false;

  mlir::Type firstTy = members.front();
  auto classifyElement = [](mlir::Type EltTy) -> int {
    if (auto vecTy = mlir::dyn_cast<VectorType>(EltTy)) {
      if (!vecTy.getIsScalable())
        return -1;
      return mlir::isa<BoolType>(vecTy.getElementType()) ? 1 : 0;
    }
    if (auto opaqueTy = mlir::dyn_cast<OpaqueType>(EltTy))
      return opaqueTy.getTag() == OpaqueType::getAArch64SVCountTag() ? 1 : -1;
    return -1;
  };

  int elementKind = classifyElement(firstTy);
  if (elementKind < 0)
    return false;

  for (mlir::Type memberTy : members)
    if (memberTy != firstTy)
      return false;

  if (elementKind == 1)
    NPred = members.size();
  else
    NVec = members.size();
  return true;
}

class AArch64ABIInfo : public ABIInfo {
  AArch64ABIKind Kind;

public:
  AArch64ABIInfo(LowerTypes &CGT, AArch64ABIKind Kind)
      : ABIInfo(CGT), Kind(Kind) {}

private:
  AArch64ABIKind getABIKind() const { return Kind; }
  bool isDarwinPCS() const { return Kind == AArch64ABIKind::DarwinPCS; }
  bool isSoftFloat() const { return Kind == AArch64ABIKind::AAPCSSoft; }

  ABIArgInfo classifyReturnType(mlir::Type RetTy, bool IsVariadic) const;
  ABIArgInfo classifyArm64ECVarArgType(mlir::Type Ty) const;
  ABIArgInfo classifyArgumentType(mlir::Type Ty, bool IsVariadic,
                                  bool IsNamedArg, unsigned CallingConvention,
                                  unsigned &NSRN, unsigned &NPRN) const;
  bool isIllegalVectorType(mlir::Type Ty) const;
  ABIArgInfo coerceIllegalVector(mlir::Type Ty, unsigned &NSRN,
                                 unsigned &NPRN) const;
  ABIArgInfo coerceAndExpandDarwinSVETuple(mlir::Type Ty) const;
  ABIArgInfo coerceAndExpandPureScalableAggregate(
      mlir::Type Ty, bool IsNamedArg, unsigned NVec, unsigned NPred,
      const llvm::SmallVectorImpl<mlir::Type> &UnpaddedCoerceToSeq,
      unsigned &NSRN, unsigned &NPRN) const;

  void computeInfo(LowerFunctionInfo &FI) const override {
    const unsigned CallingConv = FI.getCallingConvention();

    // Swift calling convention uses its own aggregate lowering.
    if (CallingConv == llvm::CallingConv::Swift ||
        CallingConv == llvm::CallingConv::SwiftTail) {
      if (!cir::classifyReturnType(getCXXABI(), FI, *this))
        FI.getReturnInfo() = swiftcall::classifyType(
            LT, FI.getReturnType(), /*forReturn=*/true);

      for (auto &arg : FI.arguments())
        arg.info =
            swiftcall::classifyType(LT, arg.type, /*forReturn=*/false);
      return;
    }

    if (!cir::classifyReturnType(getCXXABI(), FI, *this))
      FI.getReturnInfo() =
          classifyReturnType(FI.getReturnType(), FI.isVariadic());

    unsigned ArgNo = 0;
    unsigned NSRN = 0, NPRN = 0;
    for (auto &it : FI.arguments()) {
      const bool IsNamedArg = !FI.isVariadic() ||
                               ArgNo < FI.getNumRequiredArgs();
      ++ArgNo;
      it.info = classifyArgumentType(it.type, FI.isVariadic(), IsNamedArg,
                                     CallingConv, NSRN, NPRN);
    }
  }
};

class AArch64TargetLoweringInfo : public TargetLoweringInfo {
public:
  AArch64TargetLoweringInfo(LowerTypes &LT, AArch64ABIKind Kind)
      : TargetLoweringInfo(std::make_unique<AArch64ABIInfo>(LT, Kind)) {}

  mlir::Type getOpaqueType(cir::OpaqueType type) const override {
    if (type.getTag() == cir::OpaqueType::getAArch64SVCountTag())
      return mlir::LLVM::LLVMTargetExtType::get(
          type.getContext(), "aarch64.svcount", /*typeParams=*/{},
          /*intParams=*/{});
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

} // namespace

/// Check if a CIR record type contains only pointer members (recursively).
/// If true, structs of pointers should be coerced to [N x ptr] rather than
/// [N x i64] to preserve alias analysis information through ABI boundaries.
/// This mirrors OGCG's ContainsOnlyPointers lambda in classifyArgumentType.
///
/// A record "contains only pointers" if:
/// - It is not empty
/// - All leaf members are 64-bit pointers with default address space, OR
///   nested records that recursively satisfy the same property
/// - Array members are checked by their base element type
static bool containsOnlyPointers(mlir::Type Ty, unsigned ptrWidth) {
  auto RT = mlir::dyn_cast<RecordType>(Ty);
  if (!RT)
    return false;

  auto members = RT.getMembers();
  if (members.empty())
    return false;

  for (auto member : members) {
    // Unwrap arrays to their base element type.
    mlir::Type elemTy = member;
    while (auto arrTy = mlir::dyn_cast<ArrayType>(elemTy))
      elemTy = arrTy.getElementType();

    if (auto ptrTy = mlir::dyn_cast<PointerType>(elemTy)) {
      // Must be a default-address-space pointer of the right width (64-bit).
      if (ptrTy.getAddrSpace() || ptrWidth != 64)
        return false;
    } else if (mlir::isa<RecordType>(elemTy)) {
      // Recursively check nested records (handles C++ bases which are flattened
      // as nested record members in CIR).
      if (!containsOnlyPointers(elemTy, ptrWidth))
        return false;
    } else {
      return false;
    }
  }

  return true;
}

/// Check whether a vector type is "illegal" for AArch64 ABI purposes.
/// Illegal vectors are coerced to integer or canonical vector types.
bool AArch64ABIInfo::isIllegalVectorType(mlir::Type Ty) const {
  if (auto VT = mlir::dyn_cast<VectorType>(Ty)) {
    // Pure scalable SVE vectors (svbool_t, svint32_t, etc.) are always legal
    // — they use the SVE calling convention directly. In OGCG these are
    // BuiltinType::SveBool/SveBoolxN etc. and never enter this function.
    if (VT.getIsScalable())
      return false;

    // SVE fixed-length vectors are always "illegal" (coerced at ABI boundary).
    if (VT.getIsSveFixedLength())
      return true;

    unsigned NumElements = VT.getSize();
    uint64_t Size = getContext().getTypeSize(VT);

    // Non-power-of-2 element counts are illegal.
    if (!llvm::isPowerOf2_32(NumElements))
      return true;

    // arm64_32 has to be compatible with the ARM logic here, which allows huge
    // vectors for some reason.
    llvm::Triple Triple = getTarget().getTriple();
    if (Triple.getArch() == llvm::Triple::aarch64_32 &&
        Triple.isOSBinFormatMachO())
      return Size <= 32;

    // Legal sizes are 64-bit and 128-bit (with >1 element).
    // Single-element 128-bit vectors are illegal.
    return Size != 64 && (Size != 128 || NumElements == 1);
  }
  return false;
}

/// Coerce an illegal vector type to a legal ABI type.
/// NSRN/NPRN track SIMD/predicate register consumption per AAPCS64.
ABIArgInfo AArch64ABIInfo::coerceIllegalVector(mlir::Type Ty, unsigned &NSRN,
                                               unsigned &NPRN) const {
  auto VT = mlir::cast<VectorType>(Ty);

  // SVE fixed-length vectors use the scalable ABI surface on AArch64. Data
  // vectors consume SIMD registers and predicate vectors consume predicate
  // registers, matching OGCG.
  if (VT.getIsSveFixedLength()) {
    if (isFixedSVEPredicateStorageVector(VT))
      NPRN = std::min(NPRN + 1, 4u);
    else
      NSRN = std::min(NSRN + 1, 8u);
    return ABIArgInfo::getDirect(
        convertFixedToScalableVectorType(VT, getContext()));
  }

  uint64_t Size = getContext().getTypeSize(Ty);

  // Android/OHOS promote <2 x i8> to i16 instead of i32, matching OGCG.
  llvm::Triple Triple = getTarget().getTriple();
  if ((Triple.isAndroid() || Triple.isOHOSFamily()) && Size <= 16) {
    auto resType = cir::IntType::get(LT.getMLIRContext(), 16, false);
    return ABIArgInfo::getDirect(resType);
  }
  // Small vectors (<=32 bits): coerce to integer.
  if (Size <= 32) {
    auto resType = cir::IntType::get(LT.getMLIRContext(), 32, false);
    return ABIArgInfo::getDirect(resType);
  }
  // 64-bit vectors: coerce to <2 x i32>, consumes one SIMD register.
  if (Size == 64) {
    NSRN = std::min(NSRN + 1, 8u);
    auto i32Ty = cir::IntType::get(LT.getMLIRContext(), 32, false);
    auto resType = cir::VectorType::get(i32Ty, 2);
    return ABIArgInfo::getDirect(resType);
  }
  // 128-bit vectors: coerce to <4 x i32>, consumes one SIMD register.
  if (Size == 128) {
    NSRN = std::min(NSRN + 1, 8u);
    auto i32Ty = cir::IntType::get(LT.getMLIRContext(), 32, false);
    auto resType = cir::VectorType::get(i32Ty, 4);
    return ABIArgInfo::getDirect(resType);
  }

  // Larger than 128 bits: pass via memory.
  return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
}

ABIArgInfo AArch64ABIInfo::classifyReturnType(mlir::Type RetTy,
                                              bool IsVariadic) const {
  if (mlir::isa<VoidType>(RetTy))
    return ABIArgInfo::getIgnore();

  // SVE fixed-length vectors are coerced in return position.
  if (auto VT = mlir::dyn_cast<VectorType>(RetTy)) {
    if (VT.getIsSveFixedLength()) {
      unsigned unusedNSRN = 0, unusedNPRN = 0;
      return coerceIllegalVector(RetTy, unusedNSRN, unusedNPRN);
    }
  }

  // Large vector types should be returned via memory.
  if (mlir::isa<VectorType>(RetTy) && getContext().getTypeSize(RetTy) > 128)
    return getNaturalAlignIndirect(RetTy);

  unsigned TupleNVec = 0, TupleNPred = 0;
  if (isDarwinPCS() &&
      isAnonymousSVETupleRecord(RetTy, TupleNVec, TupleNPred))
    return coerceAndExpandDarwinSVETuple(RetTy);

  if (!isAggregateTypeForABI(RetTy)) {
    // NOTE(cir): Skip enum handling.

    // _BitInt(N) > 128 bits is returned indirect on AArch64.
    if (auto intTy = mlir::dyn_cast<IntType>(RetTy))
      if (intTy.getWidth() > 128)
        return getNaturalAlignIndirect(RetTy);

    return (isPromotableIntegerTypeForABI(RetTy) && isDarwinPCS()
                ? ABIArgInfo::getExtend(RetTy)
                : ABIArgInfo::getDirect());
  }

  uint64_t Size = getContext().getTypeSize(RetTy);

  // Empty records are ignored as return types.
  if (isEmptyRecord(RetTy, /*allowArrays=*/true) || Size == 0)
    return ABIArgInfo::getIgnore();

  // Homogeneous Floating-point Aggregates (HFAs) are returned directly.
  mlir::Type HFABase = {};
  uint64_t HFAMembers = 0;
  if (!isSoftFloat() &&
      isHomogeneousAggregate(RetTy, HFABase, HFAMembers, getContext()))
    return ABIArgInfo::getDirect();

  // In AAPCS return values of a Pure Scalable type are treated as a single
  // named argument and passed expanded in registers, or indirectly if there are
  // not enough registers.
  if (Kind == AArch64ABIKind::AAPCS) {
    unsigned NVec = 0, NPred = 0;
    llvm::SmallVector<mlir::Type> UnpaddedCoerceToSeq;
    if (passAsPureScalableType(RetTy, NVec, NPred, UnpaddedCoerceToSeq,
                               getContext()) &&
        (NVec + NPred) > 0) {
      unsigned NSRN = 0, NPRN = 0;
      return coerceAndExpandPureScalableAggregate(
          RetTy, /*IsNamedArg=*/true, NVec, NPred, UnpaddedCoerceToSeq,
          NSRN, NPRN);
    }
    // If not a valid PST, fall through to standard aggregate handling.
  }

  // Aggregates <= 16 bytes are returned directly in registers or on the stack.
  if (Size <= 128) {
    if (Size <= 64 && !getDataLayout().isBigEndian()) {
      // Composite types are returned in lower bits of a 64-bit register for LE,
      // and in higher bits for BE. However, integer types are always returned
      // in lower bits for both LE and BE, and they are not rounded up to
      // 64-bits. We can skip rounding up of composite types for LE, but not for
      // BE, otherwise composite types will be indistinguishable from integer
      // types.
      return ABIArgInfo::getDirect(
          cir::IntType::get(LT.getMLIRContext(), Size, false));
    }

    unsigned Alignment = getContext().getTypeAlign(RetTy);
    Size = llvm::alignTo(Size, 64); // round up to multiple of 8 bytes

    // We use a pair of i64 for 16-byte aggregate with 8-byte alignment.
    // For aggregates with 16-byte alignment, we use i128.
    if (Alignment < 128 && Size == 128) {
      mlir::Type baseTy = cir::IntType::get(LT.getMLIRContext(), 64, false);
      return ABIArgInfo::getDirect(cir::ArrayType::get(baseTy, Size / 64));
    }

    return ABIArgInfo::getDirect(
        IntType::get(LT.getMLIRContext(), Size, false));
  }

  return getNaturalAlignIndirect(RetTy);
}

ABIArgInfo
AArch64ABIInfo::classifyArm64ECVarArgType(mlir::Type Ty) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (auto RT = mlir::dyn_cast<RecordType>(Ty)) {
    if (CIRCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
      return getNaturalAlignIndirect(Ty,
                                     RAA == CIRCXXABI::RAA_DirectInMemory);

    if (auto ast = RT.getAst()) {
      const clang::RecordDecl *RD = ast.getRawDecl();
      if (RD && RD->getDefinitionOrSelf()->hasFlexibleArrayMember())
        return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
    }
  }

  uint64_t Width = getContext().getTypeSize(Ty);

  // Arm64EC variadic calls use the Windows x86_64 argument classification
  // rules instead of the generic AArch64 ABI rules.
  if (mlir::isa<RecordType, ComplexType>(Ty)) {
    if (Width > 64 || !llvm::isPowerOf2_64(Width))
      return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
    return ABIArgInfo::getDirect(
        cir::IntType::get(LT.getMLIRContext(), Width, /*isSigned=*/false));
  }

  if (mlir::isa<BoolType>(Ty))
    return ABIArgInfo::getExtend(Ty);

  if (mlir::isa<FP128Type>(Ty))
    return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

  if (auto intTy = mlir::dyn_cast<IntType>(Ty)) {
    if (intTy.getWidth() > 64)
      return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
    return ABIArgInfo::getDirect();
  }

  return ABIArgInfo::getDirect();
}

ABIArgInfo
AArch64ABIInfo::classifyArgumentType(mlir::Type Ty, bool IsVariadic,
                                     bool IsNamedArg,
                                     unsigned CallingConvention,
                                     unsigned &NSRN, unsigned &NPRN) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (IsVariadic && getTarget().getTriple().isWindowsArm64EC())
    return classifyArm64ECVarArgType(Ty);

  // Illegal vector types (non-power-of-2, single-element, SVE fixed-length)
  // are coerced to canonical types for ABI purposes.
  if (isIllegalVectorType(Ty))
    return coerceIllegalVector(Ty, NSRN, NPRN);

  unsigned TupleNVec = 0, TupleNPred = 0;
  if (isDarwinPCS() &&
      isAnonymousSVETupleRecord(Ty, TupleNVec, TupleNPred)) {
    NSRN = std::min(NSRN + TupleNVec, 8u);
    NPRN = std::min(NPRN + TupleNPred, 4u);
    return coerceAndExpandDarwinSVETuple(Ty);
  }

  if (!isAggregateTypeForABI(Ty)) {
    // NOTE(cir): Enum is IntType in CIR. Skip enum handling here.

    // _BitInt(N) > 128 bits is passed indirect on AArch64.
    if (auto intTy = mlir::dyn_cast<IntType>(Ty))
      if (intTy.getWidth() > 128)
        return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

    // Track register consumption for non-aggregate vector/float types.
    if (auto opaqueTy = mlir::dyn_cast<OpaqueType>(Ty)) {
      if (opaqueTy.getTag() == OpaqueType::getAArch64SVCountTag())
        NPRN = std::min(NPRN + 1, 4u);
    } else if (auto VT = mlir::dyn_cast<VectorType>(Ty)) {
      // SVE predicate types (svbool_t, etc.) consume predicate registers.
      if (VT.getIsScalable() && mlir::isa<BoolType>(VT.getElementType()))
        NPRN = std::min(NPRN + 1, 4u);
      else
        NSRN = std::min(NSRN + 1, 8u);
    } else if (mlir::isa<SingleType, DoubleType, LongDoubleType, FP16Type,
                          BF16Type>(Ty))
      NSRN = std::min(NSRN + 1, 8u);

    return (isPromotableIntegerTypeForABI(Ty) && isDarwinPCS()
                ? ABIArgInfo::getExtend(Ty)
                : ABIArgInfo::getDirect());
  }

  // Structures with either a non-trivial destructor or a non-trivial
  // copy constructor are always indirect.
  if (CIRCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
    return getNaturalAlignIndirect(Ty,
                                   RAA == CIRCXXABI::RAA_DirectInMemory);

  uint64_t Size = getContext().getTypeSize(Ty);

  // Empty records are ignored as arguments in C mode and C++ Darwin mode.
  // In C++ non-Darwin mode (e.g. Linux), empty records fall through to the
  // standard size-based handling path. See OGCG AArch64.cpp for details.
  bool IsEmpty = isEmptyRecord(Ty, /*allowArrays=*/true);
  if (IsEmpty || Size == 0) {
    if (!getContext().getLangOpts().CPlusPlus || isDarwinPCS())
      return ABIArgInfo::getIgnore();
    if (Size == 0)
      return ABIArgInfo::getIgnore();
  }

  // Homogeneous Floating-point Aggregates (HFAs) are passed directly
  // in SIMD registers.
  mlir::Type HFABase = {};
  uint64_t HFAMembers = 0;
  // In variadic functions on Windows, all composite types are treated alike,
  // no special handling of HFAs/HVAs. On other platforms (DarwinPCS, AAPCS),
  // HFAs are still passed directly even in variadic functions.
  bool IsWin64 = Kind == AArch64ABIKind::Win64 ||
                 CallingConvention == llvm::CallingConv::Win64;
  bool IsWinVariadic = IsWin64 && IsVariadic;
  if (!IsWinVariadic && !isSoftFloat() &&
      isHomogeneousAggregate(Ty, HFABase, HFAMembers, getContext())) {
    NSRN = std::min(NSRN + HFAMembers, uint64_t(8));
    // Widen non-power-of-2 vector bases to their padded ABI form.
    // E.g., <3 x float> (96-bit, padded to 128-bit) → <4 x float>.
    // This matches OGCG's isHomogeneousAggregate which widens the base type
    // using getTypeSize / elementSize to compute the padded element count.
    if (auto vecTy = mlir::dyn_cast<VectorType>(HFABase)) {
      uint64_t vecSize = getContext().getTypeSize(vecTy);
      uint64_t eltSize = getContext().getTypeSize(vecTy.getElementType());
      unsigned widenedCount = vecSize / eltSize;
      if (widenedCount != vecTy.getSize())
        HFABase = cir::VectorType::get(vecTy.getElementType(), widenedCount);
    }
    if (Kind != AArch64ABIKind::AAPCS)
      return ABIArgInfo::getDirect(
          cir::ArrayType::get(HFABase, HFAMembers));
    // For AAPCS, HFAs are passed directly with capped alignment:
    // cap to 16 if natural alignment >= 16, otherwise 8.
    unsigned Align = getContext().getTypeUnadjustedAlign(Ty) / 8;
    Align = (Align >= 16) ? 16 : 8;
    return ABIArgInfo::getDirect(
        cir::ArrayType::get(HFABase, HFAMembers), 0, nullptr, true, Align);
  }

  // In AAPCS named arguments of a Pure Scalable Type are passed expanded in
  // registers, or indirectly if there are not enough registers.
  if (Kind == AArch64ABIKind::AAPCS) {
    unsigned NVec = 0, NPred = 0;
    llvm::SmallVector<mlir::Type> UnpaddedCoerceToSeq;
    if (passAsPureScalableType(Ty, NVec, NPred, UnpaddedCoerceToSeq,
                               getContext()) &&
        (NVec + NPred) > 0)
      return coerceAndExpandPureScalableAggregate(
          Ty, IsNamedArg, NVec, NPred, UnpaddedCoerceToSeq, NSRN, NPRN);
  }

  // Aggregates <= 16 bytes are passed directly in registers or on the stack.
  if (Size <= 128) {
    unsigned Alignment;
    if (Kind == AArch64ABIKind::AAPCS) {
      Alignment = getContext().getTypeUnadjustedAlign(Ty);
      Alignment = Alignment < 128 ? 64 : 128;
    } else {
      Alignment = std::max(
          getContext().getTypeAlign(Ty),
          (unsigned)getTarget().getPointerWidth(clang::LangAS::Default));
    }
    Size = llvm::alignTo(Size, Alignment);

    // If the aggregate is made up of pointers, use an array of pointers for the
    // coerced type. This prevents having to convert ptr2int->int2ptr through
    // the call, allowing alias analysis to produce better code.
    //
    // We use a pair of i64 for 16-byte aggregate with 8-byte alignment.
    // For aggregates with 16-byte alignment, we use i128.
    unsigned ptrWidth = getTarget().getPointerWidth(clang::LangAS::Default);
    mlir::Type baseTy =
        cir::IntType::get(LT.getMLIRContext(), Alignment, false);
    if ((Size == 64 || Size == 128) && Alignment == 64 &&
        containsOnlyPointers(Ty, ptrWidth))
      baseTy = cir::PointerType::get(
          cir::VoidType::get(LT.getMLIRContext()));
    auto argTy = Size == Alignment
                     ? baseTy
                     : cir::ArrayType::get(baseTy, Size / Alignment);
    return ABIArgInfo::getDirect(argTy);
  }

  return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
}

ABIArgInfo AArch64ABIInfo::coerceAndExpandDarwinSVETuple(mlir::Type Ty) const {
  llvm::SmallVector<mlir::Type> UnpaddedCoerceToSeq;
  unsigned NVec = 0, NPred = 0;
  bool IsPST =
      passAsPureScalableType(Ty, NVec, NPred, UnpaddedCoerceToSeq,
                             getContext());
  cir_cconv_assert(IsPST && (NVec + NPred) > 0 &&
                   "expected Darwin SVE tuple to be a pure scalable type");

  mlir::Type UnpaddedCoerceToType;
  if (UnpaddedCoerceToSeq.size() == 1) {
    UnpaddedCoerceToType = UnpaddedCoerceToSeq[0];
  } else {
    UnpaddedCoerceToType = cir::RecordType::get(
        LT.getMLIRContext(),
        llvm::SmallVector<mlir::Type>(UnpaddedCoerceToSeq.begin(),
                                      UnpaddedCoerceToSeq.end()),
        /*packed=*/true, /*padded=*/false, cir::RecordType::RecordKind::Struct);
  }

  llvm::SmallVector<mlir::Type> CoerceToSeq;
  flattenCIRType(Ty, CoerceToSeq);
  auto CoerceToType = cir::RecordType::get(LT.getMLIRContext(), CoerceToSeq,
                                           /*packed=*/false,
                                           /*padded=*/false,
                                           cir::RecordType::RecordKind::Struct);

  return ABIArgInfo::getCoerceAndExpand(CoerceToType, UnpaddedCoerceToType);
}

ABIArgInfo AArch64ABIInfo::coerceAndExpandPureScalableAggregate(
    mlir::Type Ty, bool IsNamedArg, unsigned NVec, unsigned NPred,
    const llvm::SmallVectorImpl<mlir::Type> &UnpaddedCoerceToSeq,
    unsigned &NSRN, unsigned &NPRN) const {
  // Per AAPCS64: PSTs are only expanded in registers for named arguments,
  // and only if there are enough SIMD (8 max) and predicate (4 max) registers.
  if (!IsNamedArg || NSRN + NVec > 8 || NPRN + NPred > 4)
    return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
  NSRN += NVec;
  NPRN += NPred;

  mlir::Type UnpaddedCoerceToType;
  if (UnpaddedCoerceToSeq.size() == 1) {
    UnpaddedCoerceToType = UnpaddedCoerceToSeq[0];
  } else {
    // Create an anonymous packed record type for the unpadded sequence.
    UnpaddedCoerceToType = cir::RecordType::get(
        LT.getMLIRContext(),
        llvm::SmallVector<mlir::Type>(UnpaddedCoerceToSeq.begin(),
                                      UnpaddedCoerceToSeq.end()),
        /*packed=*/true, /*padded=*/false, cir::RecordType::RecordKind::Struct);
  }

  // Flatten the CIR type to get the padded layout.
  llvm::SmallVector<mlir::Type> CoerceToSeq;
  flattenCIRType(Ty, CoerceToSeq);
  auto CoerceToType = cir::RecordType::get(LT.getMLIRContext(), CoerceToSeq,
                                            /*packed=*/false, /*padded=*/false,
                                            cir::RecordType::RecordKind::Struct);

  return ABIArgInfo::getCoerceAndExpand(CoerceToType, UnpaddedCoerceToType);
}

std::unique_ptr<TargetLoweringInfo>
createAArch64TargetLoweringInfo(LowerModule &CGM, AArch64ABIKind Kind) {
  return std::make_unique<AArch64TargetLoweringInfo>(CGM.getTypes(), Kind);
}

} // namespace cir
