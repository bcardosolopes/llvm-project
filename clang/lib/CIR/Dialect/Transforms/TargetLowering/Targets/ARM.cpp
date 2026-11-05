//===- ARM.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "LowerFunctionInfo.h"
#include "LowerTypes.h"
#include "SwiftCallingConv.h"
#include "TargetInfo.h"
#include "TargetLoweringInfo.h"
#include "clang/CIR/ABIArgInfo.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/Support/MathExtras.h"

namespace cir {
namespace {

/// Check if a CIR type is a Homogeneous Floating-point Aggregate (HFA) or
/// Homogeneous Vector Aggregate (HVA). An HFA/HVA is a struct where all leaf
/// members are the same floating-point or short-vector type, with 1 to 4 total
/// leaf members. Handles nested structs and arrays recursively.
///
/// Only float/double/long double (long double is double on ARM) and 64- or
/// 128-bit fixed-length vectors are valid HFA/HVA base types under AAPCS-VFP,
/// matching OGCG ARMABIInfo::isHomogeneousAggregateBaseType. Unlike AAPCS64,
/// AAPCS does NOT admit __fp16 / __bf16 as a base type, so half/bfloat leaves
/// disqualify the aggregate and it falls back to the soft-float coercion path.
///
/// Returns the base type and total leaf member count if true. Adapted from the
/// AArch64 target-lowering helper of the same name.
static bool isHomogeneousAggregate(mlir::Type Ty, mlir::Type &Base,
                                   uint64_t &Members,
                                   const CIRLowerContext &ctx) {
  // Complex types are treated as 2-element HFAs if their element type is a valid
  // HFA base type. Matches OGCG ABIInfo::isHomogeneousAggregate's ComplexType
  // special case.
  if (auto CT = mlir::dyn_cast<ComplexType>(Ty)) {
    mlir::Type EltTy = CT.getElementType();
    if (!mlir::isa<SingleType, DoubleType, LongDoubleType>(EltTy))
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
    if (mlir::isa<SingleType, DoubleType, LongDoubleType>(member)) {
      // Leaf floating-point member.
      if (!Base)
        Base = member;
      else if (Base != member)
        return false; // Mixed FP types.
      FldMembers = 1;
    } else if (auto vecTy = mlir::dyn_cast<VectorType>(member)) {
      // Vector member — accept 64-bit or 128-bit fixed-length vectors.
      if (vecTy.getIsScalable() || vecTy.getIsSveFixedLength())
        return false;
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
        return false; // Mixed vector and FP base types.
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
      if (mlir::isa<SingleType, DoubleType, LongDoubleType>(eltTy)) {
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
    // For unions, take the max member count; for structs, sum all members.
    Members = IsUnion ? std::max(Members, FldMembers) : Members + FldMembers;
  }

  if (!Base)
    return false;

  // Ensure there is no padding (matches OGCG's padding check).
  if (ctx.getTypeSize(Base) * Members != ctx.getTypeSize(Ty))
    return false;

  return Members >= 1 && Members <= 4;
}

class ARMABIInfo : public ABIInfo {
public:
  explicit ARMABIInfo(LowerTypes &LT) : ABIInfo(LT) {}

  void computeInfo(LowerFunctionInfo &FI) const override {
    const unsigned CallingConv = FI.getCallingConvention();
    if (CallingConv == llvm::CallingConv::Swift ||
        CallingConv == llvm::CallingConv::SwiftTail) {
      if (!cir::classifyReturnType(getCXXABI(), FI, *this))
        FI.getReturnInfo() =
            swiftcall::classifyType(LT, FI.getReturnType(), /*forReturn=*/true);

      for (auto &arg : FI.arguments())
        arg.info = swiftcall::classifyType(LT, arg.type, /*forReturn=*/false);

      return;
    }

    // Hard-float AAPCS-VFP: homogeneous floating-point / vector aggregates (HFA
    // / HVA) with one to four leaf members are passed and returned directly in
    // VFP registers; everything else follows the same AAPCS coercion as the
    // soft-float path below (mirroring OGCG ARMABIInfo::classify{Return,Argument}
    // Type, which only special-case homogeneous aggregates when IsAAPCS_VFP).
    //
    // The float-abi option is not propagated into the reconstructed lowering
    // context, so VFP is detected from the target triple's hard-float ABI (the
    // driver bakes -mfloat-abi into the triple: gnueabi + -mfloat-abi=hard
    // becomes gnueabihf, gnueabihf + -mfloat-abi=soft becomes gnueabi), plus the
    // explicit AAPCS_VFP calling convention when one is present.
    if (CallingConv == llvm::CallingConv::ARM_AAPCS_VFP ||
        getTarget().getTriple().isHardFloatABI()) {
      if (!cir::classifyReturnType(getCXXABI(), FI, *this))
        FI.getReturnInfo() = classifyReturnTypeVFP(FI.getReturnType());

      for (auto &arg : FI.arguments())
        arg.info = classifyArgumentTypeVFP(arg.type);

      return;
    }

    if (!cir::classifyReturnType(getCXXABI(), FI, *this))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());

    for (auto &arg : FI.arguments())
      arg.info = classifyArgumentType(arg.type);
  }

  // AAPCS-VFP homogeneous-aggregate direct passing, matching OGCG
  // ARMABIInfo::classifyHomogeneousAggregate: the aggregate is passed in its
  // natural type (getDirect with no explicit coerce type) and the backend
  // splits it across the VFP register file.
  ABIArgInfo classifyHomogeneousAggregate(mlir::Type Ty, mlir::Type Base,
                                          uint64_t Members) const {
    return ABIArgInfo::getDirect(/*ty=*/nullptr, /*offset=*/0,
                                 /*padding=*/nullptr, /*canBeFlattened=*/false);
  }

  ABIArgInfo classifyReturnTypeVFP(mlir::Type RetTy) const {
    if (mlir::isa<VoidType>(RetTy))
      return ABIArgInfo::getIgnore();
    if (isAggregateTypeForABI(RetTy) &&
        !(isEmptyRecord(RetTy, /*allowArrays=*/true) ||
          getContext().getTypeSize(RetTy) == 0)) {
      mlir::Type Base = {};
      uint64_t Members = 0;
      if (isHomogeneousAggregate(RetTy, Base, Members, getContext()))
        return classifyHomogeneousAggregate(RetTy, Base, Members);
    }
    return classifyReturnType(RetTy);
  }

  ABIArgInfo classifyArgumentTypeVFP(mlir::Type Ty) const {
    mlir::Type resolved = useFirstFieldIfTransparentUnion(Ty);
    if (isAggregateTypeForABI(resolved) &&
        !getRecordArgABI(resolved, getCXXABI()) &&
        !(isEmptyRecord(resolved, /*allowArrays=*/true) ||
          getContext().getTypeSize(resolved) == 0)) {
      mlir::Type Base = {};
      uint64_t Members = 0;
      if (isHomogeneousAggregate(resolved, Base, Members, getContext()))
        return classifyHomogeneousAggregate(resolved, Base, Members);
    }
    return classifyArgumentType(Ty);
  }

private:
  // True when a vector's element is a half / __fp16 (and the target lacks a
  // fast half type) or a __bf16 under soft-float. On such targets these element
  // types are expanded rather than kept in hardware, so the ABI coerces the
  // vector to an integer vector to stay independent of half-type support.
  // Mirrors the FP16/BF16 clause shared by OGCG ARMABIInfo::isIllegalVectorType
  // and classifyReturnType.
  bool isFPVectorCoercedToInt(mlir::Type Ty) const {
    auto VT = mlir::dyn_cast<VectorType>(Ty);
    if (!VT)
      return false;
    mlir::Type eltTy = VT.getElementType();
    bool isSoftFloat = !getTarget().getTriple().isHardFloatABI();
    return (!getTarget().hasFastHalfType() && mlir::isa<cir::FP16Type>(eltTy)) ||
           (isSoftFloat && mlir::isa<cir::BF16Type>(eltTy));
  }

  // Whether a vector type is "illegal" for the AArch32 argument ABI and must be
  // coerced, mirroring OGCG ARMABIInfo::isIllegalVectorType: FP16/BF16 vectors
  // (see above), non-power-of-2 element counts, or vectors 32 bits or smaller.
  // Android's legacy vector ABI additionally admits 3-element and sub-32-bit
  // vectors.
  bool isIllegalVectorType(mlir::Type Ty) const {
    auto VT = mlir::dyn_cast<VectorType>(Ty);
    if (!VT)
      return false;
    if (isFPVectorCoercedToInt(Ty))
      return true;
    unsigned numElements = VT.getSize();
    if (getTarget().getTriple().isAndroid())
      return !llvm::isPowerOf2_32(numElements) && numElements != 3;
    if (!llvm::isPowerOf2_32(numElements))
      return true;
    return getContext().getTypeSize(VT) <= 32;
  }

  // Coerce an illegal AArch32 vector to an integer type, mirroring OGCG
  // ARMABIInfo::coerceIllegalVector: 32 bits or less -> i32; 64/128-bit ->
  // <Size/32 x i32>; anything larger is passed indirectly.
  ABIArgInfo coerceIllegalVector(mlir::Type Ty) const {
    uint64_t Size = getContext().getTypeSize(Ty);
    auto *ctx = LT.getMLIRContext();
    auto i32Ty = cir::IntType::get(ctx, 32, /*signed=*/false);
    if (Size <= 32)
      return ABIArgInfo::getDirect(i32Ty);
    if (Size == 64 || Size == 128)
      return ABIArgInfo::getDirect(cir::VectorType::get(i32Ty, Size / 32));
    return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
  }

  // Decide whether an empty record argument should be dropped or passed as a
  // 1-byte slot, mirroring OGCG ARMABIInfo::shouldIgnoreEmptyArg. In C an empty
  // record is size 0 and is always ignored; in C++ it is size 1 and is passed
  // (as i8) unless it is genuinely zero-sized or an older clang ABI (<= Ver19)
  // is being targeted. CIR ARM does not model the AAPCS16_VFP (WatchOS) kind.
  bool shouldIgnoreEmptyArg(mlir::Type Ty, uint64_t Size) const {
    if (!getContext().getLangOpts().CPlusPlus)
      return true;
    if (Size == 0)
      return true;
    if (getContext().getLangOpts().getClangABICompat() <=
        clang::LangOptions::ClangABI::Ver19)
      return true;
    return false;
  }

  // AAPCS (soft-float / softfp) return classification, mirroring OGCG
  // ARMABIInfo::classifyReturnType for the non-VFP, non-APCS path.
  ABIArgInfo classifyReturnType(mlir::Type RetTy) const {
    if (mlir::isa<VoidType>(RetTy))
      return ABIArgInfo::getIgnore();

    // FP16/BF16 vectors on targets without a legal half type are coerced to
    // integer vectors in return position, matching OGCG
    // ARMABIInfo::classifyReturnType.
    if (isFPVectorCoercedToInt(RetTy))
      return coerceIllegalVector(RetTy);

    if (!isAggregateTypeForABI(RetTy)) {
      // _BitInt(N) wider than 64 bits is returned indirectly.
      if (auto intTy = mlir::dyn_cast<IntType>(RetTy))
        if (intTy.getWidth() > 64)
          return getNaturalAlignIndirect(RetTy, /*ByVal=*/false);

      return isPromotableIntegerTypeForABI(RetTy) ? ABIArgInfo::getExtend(RetTy)
                                                  : ABIArgInfo::getDirect();
    }

    uint64_t Size = getContext().getTypeSize(RetTy);
    if (isEmptyRecord(RetTy, /*allowArrays=*/true) || Size == 0)
      return ABIArgInfo::getIgnore();

    // Aggregates <= 4 bytes are returned in r0 in the smallest viable integer
    // type; larger aggregates are returned indirectly (via sret).
    if (Size <= 32) {
      auto *ctx = LT.getMLIRContext();
      if (Size <= 8)
        return ABIArgInfo::getDirect(cir::IntType::get(ctx, 8, /*signed=*/false));
      if (Size <= 16)
        return ABIArgInfo::getDirect(
            cir::IntType::get(ctx, 16, /*signed=*/false));
      return ABIArgInfo::getDirect(cir::IntType::get(ctx, 32, /*signed=*/false));
    }

    return getNaturalAlignIndirect(RetTy);
  }

  // AAPCS (soft-float / softfp) argument classification, mirroring OGCG
  // ARMABIInfo::classifyArgumentType for the non-VFP, non-APCS path.
  ABIArgInfo classifyArgumentType(mlir::Type Ty) const {
    Ty = useFirstFieldIfTransparentUnion(Ty);

    // Handle illegal vector types (e.g. half/__fp16 vectors without a legal half
    // type) by coercing to integer vectors, matching OGCG
    // ARMABIInfo::classifyArgumentType.
    if (isIllegalVectorType(Ty))
      return coerceIllegalVector(Ty);

    if (!isAggregateTypeForABI(Ty)) {
      // _BitInt(N) wider than 64 bits is passed indirectly by value.
      if (auto intTy = mlir::dyn_cast<IntType>(Ty))
        if (intTy.getWidth() > 64)
          return getNaturalAlignIndirect(Ty, /*ByVal=*/true);

      return isPromotableIntegerTypeForABI(Ty) ? ABIArgInfo::getExtend(Ty)
                                               : ABIArgInfo::getDirect();
    }

    // Structures with a non-trivial destructor or copy constructor are passed
    // indirectly.
    if (CIRCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
      return getNaturalAlignIndirect(Ty,
                                     RAA == CIRCXXABI::RAA_DirectInMemory);

    uint64_t Size = getContext().getTypeSize(Ty);
    if (isEmptyRecord(Ty, /*allowArrays=*/true) || Size == 0) {
      // In C an empty record has size 0 and is dropped. In C++ an empty record
      // has size 1 and occupies a 1-byte argument slot (passed as i8), unless
      // the record is genuinely zero-sized (e.g. a GCC-style zero-length array
      // member) or an older clang ABI is being targeted for compatibility.
      // Mirrors OGCG ARMABIInfo::shouldIgnoreEmptyArg.
      if (shouldIgnoreEmptyArg(Ty, Size))
        return ABIArgInfo::getIgnore();
      return ABIArgInfo::getDirect(
          cir::IntType::get(LT.getMLIRContext(), 8, /*signed=*/false));
    }

    // AAPCS: the ABI alignment is at least 4 and at most 8 bytes. Aggregates
    // larger than 64 bytes are passed indirectly by value (byval); the rest are
    // coerced to an array of i32 (4-byte aligned) or i64 (8-byte aligned).
    //
    // Use the UNADJUSTED alignment (ignoring any __attribute__((aligned)) the
    // type carries), matching OGCG ARMABIInfo::classifyArgumentType, which reads
    // getTypeUnadjustedAlignInChars for the AAPCS/AAPCS-VFP kinds. The coercion
    // element type must follow the natural AAPCS word rule so that an
    // over-aligned aggregate still coerces to an i32-word array rather than an
    // i64 array (which would spuriously demand an even core-register pair).
    uint64_t TyAlign = getContext().getTypeUnadjustedAlign(Ty) / 8;
    if (Size > 64 * 8)
      return getNaturalAlignIndirect(Ty, /*ByVal=*/true);

    auto *ctx = LT.getMLIRContext();
    mlir::Type elemTy;
    unsigned sizeRegs;
    if (TyAlign <= 4) {
      elemTy = cir::IntType::get(ctx, 32, /*signed=*/false);
      sizeRegs = llvm::divideCeil(Size, 32);
    } else {
      elemTy = cir::IntType::get(ctx, 64, /*signed=*/false);
      sizeRegs = llvm::divideCeil(Size, 64);
    }

    return ABIArgInfo::getDirect(cir::ArrayType::get(elemTy, sizeRegs));
  }
};

class ARMTargetLoweringInfo : public TargetLoweringInfo {
public:
  explicit ARMTargetLoweringInfo(LowerTypes &LT)
      : TargetLoweringInfo(std::make_unique<ARMABIInfo>(LT)) {}

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

std::unique_ptr<TargetLoweringInfo>
createARMTargetLoweringInfo(LowerModule &LM) {
  return std::make_unique<ARMTargetLoweringInfo>(LM.getTypes());
}

} // namespace cir
