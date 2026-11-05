//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for cir::MissingFeatures bookkeeping.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/MissingFeatures.h"
#include "gtest/gtest.h"

#include <type_traits>

namespace {

template <typename T, typename = void>
struct hasAggCopyDataSizeForOverlap : std::false_type {};

template <typename T>
struct hasAggCopyDataSizeForOverlap<
    T, std::void_t<decltype(&T::aggCopyDataSizeForOverlap)>> : std::true_type {
};

template <typename T, typename = void>
struct hasAggEmitFinalDestCopyRValue : std::false_type {};

template <typename T>
struct hasAggEmitFinalDestCopyRValue<
    T, std::void_t<decltype(&T::aggEmitFinalDestCopyRValue)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasABIArgCoerceAndExpand : std::false_type {};

template <typename T>
struct hasABIArgCoerceAndExpand<
    T, std::void_t<decltype(&T::ABIArgCoerceAndExpand)>> : std::true_type {};

template <typename T, typename = void>
struct hasAbiArgInfo : std::false_type {};

template <typename T>
struct hasAbiArgInfo<T, std::void_t<decltype(&T::abiArgInfo)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasABIClangTypeKind : std::false_type {};

template <typename T>
struct hasABIClangTypeKind<T, std::void_t<decltype(&T::ABIClangTypeKind)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasABIFuncPtr : std::false_type {};

template <typename T>
struct hasABIFuncPtr<T, std::void_t<decltype(&T::ABIFuncPtr)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasABIInRegAttribute : std::false_type {};

template <typename T>
struct hasABIInRegAttribute<
    T, std::void_t<decltype(&T::ABIInRegAttribute)>> : std::true_type {};

template <typename T, typename = void>
struct hasABINoAliasAttribute : std::false_type {};

template <typename T>
struct hasABINoAliasAttribute<
    T, std::void_t<decltype(&T::ABINoAliasAttribute)>> : std::true_type {};

template <typename T, typename = void>
struct hasABINoProtoFunctions : std::false_type {};

template <typename T>
struct hasABINoProtoFunctions<
    T, std::void_t<decltype(&T::ABINoProtoFunctions)>> : std::true_type {};

template <typename T, typename = void>
struct hasABIPotentialArgAccess : std::false_type {};

template <typename T>
struct hasABIPotentialArgAccess<
    T, std::void_t<decltype(&T::ABIPotentialArgAccess)>> : std::true_type {};

template <typename T, typename = void>
struct hasABITransparentUnionHandling : std::false_type {};

template <typename T>
struct hasABITransparentUnionHandling<
    T, std::void_t<decltype(&T::ABITransparentUnionHandling)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasAddHeapAllocSiteMetadata : std::false_type {};

template <typename T>
struct hasAddHeapAllocSiteMetadata<
    T, std::void_t<decltype(&T::addHeapAllocSiteMetadata)>> : std::true_type {
};

template <typename T, typename = void>
struct hasAddressPointerAuthInfo : std::false_type {};

template <typename T>
struct hasAddressPointerAuthInfo<
    T, std::void_t<decltype(&T::addressPointerAuthInfo)>> : std::true_type {};

template <typename T, typename = void>
struct hasAddressSpace : std::false_type {};

template <typename T>
struct hasAddressSpace<T, std::void_t<decltype(&T::addressSpace)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasArgumentPadding : std::false_type {};

template <typename T>
struct hasArgumentPadding<T, std::void_t<decltype(&T::argumentPadding)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasAggCtorSanitizerChecked : std::false_type {};

template <typename T>
struct hasAggCtorSanitizerChecked<
    T, std::void_t<decltype(&T::aggCtorSanitizerChecked)>> : std::true_type {};

template <typename T, typename = void>
struct hasAggValueSlotDestructedFlag : std::false_type {};

template <typename T>
struct hasAggValueSlotDestructedFlag<
    T, std::void_t<decltype(&T::aggValueSlotDestructedFlag)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasAggResultUnused : std::false_type {};

template <typename T, typename = void>
struct hasAtomicAttr : std::false_type {};

template <typename T>
struct hasAggResultUnused<T, std::void_t<decltype(&T::aggResultUnused)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasBuiltinCallF128 : std::false_type {};

template <typename T, typename = void>
struct hasAllocToken : std::false_type {};

template <typename T>
struct hasAllocToken<T, std::void_t<decltype(&T::allocToken)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasArm64ECDestructorAlias : std::false_type {};

template <typename T>
struct hasArm64ECDestructorAlias<
    T, std::void_t<decltype(&T::arm64ECDestructorAlias)>> : std::true_type {};

template <typename T, typename = void>
struct hasAsmMSReturnRegisterOutputs : std::false_type {};

template <typename T>
struct hasAsmMSReturnRegisterOutputs<
    T, std::void_t<decltype(&T::asmMSReturnRegisterOutputs)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasCxxabiRequiresArrayCookieForDelete : std::false_type {};

template <typename T>
struct hasCxxabiRequiresArrayCookieForDelete<
    T, std::void_t<decltype(&T::cxxabiRequiresArrayCookieForDelete)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasCxxabiUseARMGuardVarABI : std::false_type {};

template <typename T>
struct hasCxxabiUseARMGuardVarABI<
    T, std::void_t<decltype(&T::cxxabiUseARMGuardVarABI)>> : std::true_type {
};

template <typename T, typename = void>
struct hasCxxabiUseARMMethodPtrABI : std::false_type {};

template <typename T>
struct hasCxxabiUseARMMethodPtrABI<
    T, std::void_t<decltype(&T::cxxabiUseARMMethodPtrABI)>> : std::true_type {
};

template <typename T, typename = void>
struct hasDataLayoutPtrHandlingBasedOnLangAS : std::false_type {};

template <typename T>
struct hasDataLayoutPtrHandlingBasedOnLangAS<
    T, std::void_t<decltype(&T::dataLayoutPtrHandlingBasedOnLangAS)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasDebugInfoImportDecl : std::false_type {};

template <typename T>
struct hasDebugInfoImportDecl<T, std::void_t<decltype(&T::debugInfoImportDecl)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasDebugInfoLocalVarDecl : std::false_type {};

template <typename T>
struct hasDebugInfoLocalVarDecl<
    T, std::void_t<decltype(&T::debugInfoLocalVarDecl)>> : std::true_type {};

template <typename T, typename = void>
struct hasDebugInfoTypeCompletion : std::false_type {};

template <typename T>
struct hasDebugInfoTypeCompletion<
    T, std::void_t<decltype(&T::debugInfoTypeCompletion)>> : std::true_type {};

template <typename T, typename = void>
struct hasDeclHasAlignNaturalAttr : std::false_type {};

template <typename T>
struct hasDeclHasAlignNaturalAttr<
    T, std::void_t<decltype(&T::declHasAlignNaturalAttr)>> : std::true_type {};

template <typename T, typename = void>
struct hasDestroyingOperatorDelete : std::false_type {};

template <typename T>
struct hasDestroyingOperatorDelete<
    T, std::void_t<decltype(&T::destroyingOperatorDelete)>> : std::true_type {
};

template <typename T, typename = void>
struct hasDtorDeleteConditional : std::false_type {};

template <typename T>
struct hasDtorDeleteConditional<
    T, std::void_t<decltype(&T::dtorDeleteConditional)>> : std::true_type {};

template <typename T, typename = void>
struct hasEffectiveCallingConvention : std::false_type {};

template <typename T>
struct hasEffectiveCallingConvention<
    T, std::void_t<decltype(&T::effectiveCallingConvention)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasExactDynamicCast : std::false_type {};

template <typename T>
struct hasExactDynamicCast<T, std::void_t<decltype(&T::exactDynamicCast)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasExplicitCastExprType : std::false_type {};

template <typename T>
struct hasExplicitCastExprType<
    T, std::void_t<decltype(&T::explicitCastExprType)>> : std::true_type {};

template <typename T, typename = void>
struct hasExtVectorBoolType : std::false_type {};

template <typename T>
struct hasExtVectorBoolType<T, std::void_t<decltype(&T::extVectorBoolType)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasExtVectorIntegralCastShortcut : std::false_type {};

template <typename T>
struct hasExtVectorIntegralCastShortcut<
    T, std::void_t<decltype(&T::extVectorIntegralCastShortcut)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasFieldDeclAbstraction : std::false_type {};

template <typename T>
struct hasFieldDeclAbstraction<
    T, std::void_t<decltype(&T::fieldDeclAbstraction)>> : std::true_type {};

template <typename T, typename = void>
struct hasFieldDeclGetMaxFieldAlignment : std::false_type {};

template <typename T>
struct hasFieldDeclGetMaxFieldAlignment<
    T, std::void_t<decltype(&T::fieldDeclGetMaxFieldAlignment)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasFieldDeclIsBitfield : std::false_type {};

template <typename T>
struct hasFieldDeclIsBitfield<
    T, std::void_t<decltype(&T::fieldDeclIsBitfield)>> : std::true_type {};

template <typename T, typename = void>
struct hasFieldDeclIsPotentiallyOverlapping : std::false_type {};

template <typename T>
struct hasFieldDeclIsPotentiallyOverlapping<
    T, std::void_t<decltype(&T::fieldDeclIsPotentiallyOverlapping)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasFixedPointType : std::false_type {};

template <typename T>
struct hasFixedPointType<T, std::void_t<decltype(&T::fixedPointType)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasFixedWidthIntegers : std::false_type {};

template <typename T>
struct hasFixedWidthIntegers<
    T, std::void_t<decltype(&T::fixedWidthIntegers)>> : std::true_type {};

template <typename T, typename = void>
struct hasGetCXXRecordBases : std::false_type {};

template <typename T>
struct hasGetCXXRecordBases<T, std::void_t<decltype(&T::getCXXRecordBases)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasHip : std::false_type {};

template <typename T>
struct hasHip<T, std::void_t<decltype(&T::hip)>> : std::true_type {};

template <typename T, typename = void>
struct hasHipModuleCtor : std::false_type {};

template <typename T>
struct hasHipModuleCtor<T, std::void_t<decltype(&T::hipModuleCtor)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasHlsl : std::false_type {};

template <typename T>
struct hasHlsl<T, std::void_t<decltype(&T::hlsl)>> : std::true_type {};

template <typename T, typename = void>
struct hasHlslConstantBufferGlobalSkip : std::false_type {};

template <typename T>
struct hasHlslConstantBufferGlobalSkip<
    T, std::void_t<decltype(&T::hlslConstantBufferGlobalSkip)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasHlslControlFlowHintAttr : std::false_type {};

template <typename T>
struct hasHlslControlFlowHintAttr<
    T, std::void_t<decltype(&T::hlslControlFlowHintAttr)>> : std::true_type {};

template <typename T, typename = void>
struct hasHlslFRem : std::false_type {};

template <typename T>
struct hasHlslFRem<T, std::void_t<decltype(&T::hlslFRem)>> : std::true_type {};

template <typename T, typename = void>
struct hasHlslInlineSpirvType : std::false_type {};

template <typename T>
struct hasHlslInlineSpirvType<
    T, std::void_t<decltype(&T::hlslInlineSpirvType)>> : std::true_type {};

template <typename T, typename = void>
struct hasPackedVectorBoolType : std::false_type {};

template <typename T>
struct hasPackedVectorBoolType<
    T, std::void_t<decltype(&T::packedVectorBoolType)>> : std::true_type {};

template <typename T, typename = void>
struct hasPragmaClangSectionAttr : std::false_type {};

template <typename T>
struct hasPragmaClangSectionAttr<
    T, std::void_t<decltype(&T::pragmaClangSectionAttr)>> : std::true_type {};

template <typename T, typename = void>
struct hasRegisterGlobalDtorsWithAtExit : std::false_type {};

template <typename T>
struct hasRegisterGlobalDtorsWithAtExit<
    T, std::void_t<decltype(&T::registerGlobalDtorsWithAtExit)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasHlslShiftMasking : std::false_type {};

template <typename T>
struct hasHlslShiftMasking<T, std::void_t<decltype(&T::hlslShiftMasking)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasInallocaArgs : std::false_type {};

template <typename T>
struct hasInallocaArgs<T, std::void_t<decltype(&T::inallocaArgs)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasIntrinsics : std::false_type {};

template <typename T>
struct hasIntrinsics<T, std::void_t<decltype(&T::intrinsics)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasInstrumentation : std::false_type {};

template <typename T>
struct hasInstrumentation<T, std::void_t<decltype(&T::instrumentation)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasIncrementProfileCounter : std::false_type {};

template <typename T>
struct hasIncrementProfileCounter<
    T, std::void_t<decltype(&T::incrementProfileCounter)>> : std::true_type {};

template <typename T, typename = void>
struct hasInitSegAttr : std::false_type {};

template <typename T>
struct hasInitSegAttr<T, std::void_t<decltype(&T::initSegAttr)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasEhCleanupHasPrebranchedFallthrough : std::false_type {};

template <typename T>
struct hasEhCleanupHasPrebranchedFallthrough<
    T, std::void_t<decltype(&T::ehCleanupHasPrebranchedFallthrough)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasEhFilterScope : std::false_type {};

template <typename T>
struct hasEhFilterScope<T, std::void_t<decltype(&T::ehFilterScope)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasEhMayThrowVsHasEHBranches : std::false_type {};

template <typename T>
struct hasEhMayThrowVsHasEHBranches<
    T, std::void_t<decltype(&T::ehMayThrowVsHasEHBranches)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasElideOverflowSanitizerChecks : std::false_type {};

template <typename T>
struct hasElideOverflowSanitizerChecks<
    T, std::void_t<decltype(&T::elideOverflowSanitizerChecks)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasEmbedExprEmission : std::false_type {};

template <typename T>
struct hasEmbedExprEmission<T, std::void_t<decltype(&T::embedExprEmission)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasEmitCtorDtorLists : std::false_type {};

template <typename T>
struct hasEmitCtorDtorLists<T, std::void_t<decltype(&T::emitCtorDtorLists)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasEmitCXXGlobalInitFunc : std::false_type {};

template <typename T>
struct hasEmitCXXGlobalInitFunc<
    T, std::void_t<decltype(&T::emitCXXGlobalInitFunc)>> : std::true_type {};

template <typename T, typename = void>
struct hasEmitLValueAlignmentAssumption : std::false_type {};

template <typename T>
struct hasEmitLValueAlignmentAssumption<
    T, std::void_t<decltype(&T::emitLValueAlignmentAssumption)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasEmitModuleLinkOptions : std::false_type {};

template <typename T>
struct hasEmitModuleLinkOptions<
    T, std::void_t<decltype(&T::emitModuleLinkOptions)>> : std::true_type {};

template <typename T, typename = void>
struct hasEmitTypeMetadataCodeForVCall : std::false_type {};

template <typename T>
struct hasEmitTypeMetadataCodeForVCall<
    T, std::void_t<decltype(&T::emitTypeMetadataCodeForVCall)>>
    : std::true_type {};

} // namespace

TEST(MissingFeaturesTest, AggCopyDataSizeForOverlapEntryRemoved) {
  static_assert(!hasAggCopyDataSizeForOverlap<cir::MissingFeatures>::value,
                "aggCopyDataSizeForOverlap should stay removed once "
                "AggregateExprEmitter overlap handling is implemented");
  EXPECT_FALSE(hasAggCopyDataSizeForOverlap<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, AggEmitFinalDestCopyRValueEntryRemoved) {
  static_assert(!hasAggEmitFinalDestCopyRValue<cir::MissingFeatures>::value,
                "aggEmitFinalDestCopyRValue should stay removed after stale "
                "emitFinalDestCopy guard deletion");
  EXPECT_FALSE(hasAggEmitFinalDestCopyRValue<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ABIArgCoerceAndExpandEntryRemoved) {
  static_assert(!hasABIArgCoerceAndExpand<cir::MissingFeatures>::value,
                "ABIArgCoerceAndExpand should stay removed while "
                "CoerceAndExpand remains NYI and guarded by unreachable paths");
  EXPECT_FALSE(hasABIArgCoerceAndExpand<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, AbiArgInfoEntryRemoved) {
  static_assert(!hasAbiArgInfo<cir::MissingFeatures>::value,
                "abiArgInfo should stay removed once stale CIRGen return-path "
                "guarding is deleted");
  EXPECT_FALSE(hasAbiArgInfo<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ABIClangTypeKindEntryRemoved) {
  static_assert(!hasABIClangTypeKind<cir::MissingFeatures>::value,
                "ABIClangTypeKind should stay removed after deleting stale "
                "type-kind guard in CIRLowerContext");
  EXPECT_FALSE(hasABIClangTypeKind<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ABIFuncPtrEntryRemoved) {
  static_assert(!hasABIFuncPtr<cir::MissingFeatures>::value,
                "ABIFuncPtr should stay removed after deleting stale "
                "CallConvLowering symbol-user guard");
  EXPECT_FALSE(hasABIFuncPtr<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ABIInRegAttributeEntryRemoved) {
  static_assert(!hasABIInRegAttribute<cir::MissingFeatures>::value,
                "ABIInRegAttribute should stay removed while InReg remains "
                "explicitly NYI in LowerCall");
  EXPECT_FALSE(hasABIInRegAttribute<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ABINoAliasAttributeEntryRemoved) {
  static_assert(!hasABINoAliasAttribute<cir::MissingFeatures>::value,
                "ABINoAliasAttribute should stay removed after deleting stale "
                "NoAlias call-attribute guard in CIRGenCall");
  EXPECT_FALSE(hasABINoAliasAttribute<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ABINoProtoFunctionsEntryRemoved) {
  static_assert(!hasABINoProtoFunctions<cir::MissingFeatures>::value,
                "ABINoProtoFunctions should stay removed after deleting stale "
                "no-prototype declaration guard in LowerCall");
  EXPECT_FALSE(hasABINoProtoFunctions<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ABIPotentialArgAccessEntryRemoved) {
  static_assert(!hasABIPotentialArgAccess<cir::MissingFeatures>::value,
                "ABIPotentialArgAccess should stay removed after deleting "
                "stale indirect-arg and return guards in LowerCall");
  EXPECT_FALSE(hasABIPotentialArgAccess<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ABITransparentUnionHandlingEntryRemoved) {
  static_assert(!hasABITransparentUnionHandling<cir::MissingFeatures>::value,
                "ABITransparentUnionHandling should stay removed once all "
                "call-conv lowering guards are deleted");
  EXPECT_FALSE(hasABITransparentUnionHandling<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, AddHeapAllocSiteMetadataEntryRemoved) {
  static_assert(!hasAddHeapAllocSiteMetadata<cir::MissingFeatures>::value,
                "addHeapAllocSiteMetadata should stay removed after deleting "
                "stale explicit-pointer-cast guard in CIRGenExprScalar");
  EXPECT_FALSE(hasAddHeapAllocSiteMetadata<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, AddressPointerAuthInfoEntryRemoved) {
  static_assert(!hasAddressPointerAuthInfo<cir::MissingFeatures>::value,
                "addressPointerAuthInfo should stay removed while CIR uses "
                "GEP-based addressing without ptr-auth offsets");
  EXPECT_FALSE(hasAddressPointerAuthInfo<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, AddressSpaceEntryRemoved) {
  static_assert(!hasAddressSpace<cir::MissingFeatures>::value,
                "addressSpace should stay removed once stale call-site "
                "guards are deleted");
  EXPECT_FALSE(hasAddressSpace<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ArgumentPaddingEntryRemoved) {
  static_assert(!hasArgumentPadding<cir::MissingFeatures>::value,
                "argumentPadding should stay removed after deleting stale "
                "ABI argument-mapping/type-lowering guard-sites");
  EXPECT_FALSE(hasArgumentPadding<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, AggCtorSanitizerCheckedEntryRemoved) {
  static_assert(!hasAggCtorSanitizerChecked<cir::MissingFeatures>::value,
                "aggCtorSanitizerChecked should stay removed while sanitizer "
                "handling remains tracked by focused per-call-site NYI work");
  EXPECT_FALSE(hasAggCtorSanitizerChecked<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, AggValueSlotDestructedFlagEntryRemoved) {
  static_assert(!hasAggValueSlotDestructedFlag<cir::MissingFeatures>::value,
                "aggValueSlotDestructedFlag should stay removed after deleting "
                "stale withReturnValueSlot bookkeeping guards");
  EXPECT_FALSE(hasAggValueSlotDestructedFlag<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, AggResultUnusedEntryRemoved) {
  static_assert(!hasAggResultUnused<cir::MissingFeatures>::value,
                "aggResultUnused should stay removed until CIR ReturnValueSlot "
                "gains explicit IsResultUnused semantics");
  EXPECT_FALSE(hasAggResultUnused<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, AtomicAttrEntryRemoved) {
  static_assert(!hasAtomicAttr<cir::MissingFeatures>::value,
                "atomicAttr should stay removed after deleting stale "
                "emitAttributedStmt guard");
  EXPECT_FALSE(hasAtomicAttr<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, BuiltinCallF128EntryRemoved) {
  static_assert(!hasBuiltinCallF128<cir::MissingFeatures>::value,
                "builtinCallF128 should stay removed after deleting stale "
                "emitBuiltinExpr guard");
  EXPECT_FALSE(hasBuiltinCallF128<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, AllocTokenEntryRemoved) {
  static_assert(!hasAllocToken<cir::MissingFeatures>::value,
                "allocToken should stay removed while sanitizer work remains "
                "tracked by focused per-call-site NYI handling");
  EXPECT_FALSE(hasAllocToken<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, Arm64ECDestructorAliasEntryRemoved) {
  static_assert(!hasArm64ECDestructorAlias<cir::MissingFeatures>::value,
                "arm64ECDestructorAlias should stay removed now that "
                "tryEmitBaseDestructorAsAlias handles Windows ARM64EC "
                "directly");
  EXPECT_FALSE(hasArm64ECDestructorAlias<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, AsmMSReturnRegisterOutputsEntryRemoved) {
  static_assert(!hasAsmMSReturnRegisterOutputs<cir::MissingFeatures>::value,
                "asmMSReturnRegisterOutputs should stay removed now that "
                "CIR marks sawAsmBlock for MS asm statements");
  EXPECT_FALSE(hasAsmMSReturnRegisterOutputs<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, CxxabiRequiresArrayCookieForDeleteEntryRemoved) {
  static_assert(
      !hasCxxabiRequiresArrayCookieForDelete<cir::MissingFeatures>::value,
      "cxxabiRequiresArrayCookieForDelete should stay removed after "
      "CIRGenCXXABI gained delete[] requiresArrayCookie overload parity");
  EXPECT_FALSE(
      hasCxxabiRequiresArrayCookieForDelete<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, CxxabiUseARMGuardVarABIEntryRemoved) {
  static_assert(!hasCxxabiUseARMGuardVarABI<cir::MissingFeatures>::value,
                "cxxabiUseARMGuardVarABI should stay removed once CIR "
                "uses ARM guard variable ABI semantics in guarded init");
  EXPECT_FALSE(hasCxxabiUseARMGuardVarABI<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, CxxabiUseARMMethodPtrABIEntryRemoved) {
  static_assert(!hasCxxabiUseARMMethodPtrABI<cir::MissingFeatures>::value,
                "cxxabiUseARMMethodPtrABI should stay removed once ARM method "
                "pointer ABI lowering is implemented");
  EXPECT_FALSE(hasCxxabiUseARMMethodPtrABI<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, DataLayoutPtrHandlingBasedOnLangASEntryRemoved) {
  static_assert(
      !hasDataLayoutPtrHandlingBasedOnLangAS<cir::MissingFeatures>::value,
      "dataLayoutPtrHandlingBasedOnLangAS should stay removed now that "
      "PointerType data layout handles address-space-specific pointer specs");
  EXPECT_FALSE(
      hasDataLayoutPtrHandlingBasedOnLangAS<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, DebugInfoImportDeclEntryRemoved) {
  static_assert(!hasDebugInfoImportDecl<cir::MissingFeatures>::value,
                "debugInfoImportDecl should stay removed because CIR handles "
                "direct imports without a dedicated debug-import operation");
  EXPECT_FALSE(hasDebugInfoImportDecl<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, DebugInfoLocalVarDeclEntryRemoved) {
  static_assert(!hasDebugInfoLocalVarDecl<cir::MissingFeatures>::value,
                "debugInfoLocalVarDecl should stay removed because CIR "
                "defers debug metadata materialization to lowering");
  EXPECT_FALSE(hasDebugInfoLocalVarDecl<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, DebugInfoTypeCompletionEntryRemoved) {
  static_assert(!hasDebugInfoTypeCompletion<cir::MissingFeatures>::value,
                "debugInfoTypeCompletion should stay removed because CIR "
                "defers DIType completion to lowering");
  EXPECT_FALSE(hasDebugInfoTypeCompletion<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, DeclHasAlignNaturalAttrEntryRemoved) {
  static_assert(!hasDeclHasAlignNaturalAttr<cir::MissingFeatures>::value,
                "declHasAlignNaturalAttr should stay removed once CIR record "
                "layout handles AlignNaturalAttr");
  EXPECT_FALSE(hasDeclHasAlignNaturalAttr<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, DestroyingOperatorDeleteEntryRemoved) {
  static_assert(!hasDestroyingOperatorDelete<cir::MissingFeatures>::value,
                "destroyingOperatorDelete should stay removed now that "
                "destroying delete handling is modeled directly in CIRGenExprCXX");
  EXPECT_FALSE(hasDestroyingOperatorDelete<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, DtorDeleteConditionalEntryRemoved) {
  static_assert(!hasDtorDeleteConditional<cir::MissingFeatures>::value,
                "dtorDeleteConditional should stay removed once conditional "
                "destructor delete behavior is modeled directly in CIR delete paths");
  EXPECT_FALSE(hasDtorDeleteConditional<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, EffectiveCallingConventionEntryRemoved) {
  static_assert(!hasEffectiveCallingConvention<cir::MissingFeatures>::value,
                "effectiveCallingConvention should stay removed now that "
                "effective CC is modeled in CIR-to-LLVM ABI lowering");
  EXPECT_FALSE(hasEffectiveCallingConvention<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ExactDynamicCastEntryRemoved) {
  static_assert(!hasExactDynamicCast<cir::MissingFeatures>::value,
                "exactDynamicCast should stay removed while CIR "
                "always uses ABI dynamic_cast emission for correctness");
  EXPECT_FALSE(hasExactDynamicCast<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ExplicitCastExprTypeEntryRemoved) {
  static_assert(!hasExplicitCastExprType<cir::MissingFeatures>::value,
                "explicitCastExprType should stay removed while "
                "CIR emits explicit-cast type side effects directly");
  EXPECT_FALSE(hasExplicitCastExprType<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ExtVectorBoolTypeEntryRemoved) {
  static_assert(!hasExtVectorBoolType<cir::MissingFeatures>::value,
                "extVectorBoolType should stay removed while "
                "CIR convertTypeForMem handles packed ext_vector_bool directly");
  EXPECT_FALSE(hasExtVectorBoolType<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ExtVectorIntegralCastShortcutEntryRemoved) {
  static_assert(!hasExtVectorIntegralCastShortcut<cir::MissingFeatures>::value,
                "extVectorIntegralCastShortcut should stay removed while "
                "emitScalarConversion handles vector integral casts directly");
  EXPECT_FALSE(
      hasExtVectorIntegralCastShortcut<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, FieldDeclAbstractionEntryRemoved) {
  static_assert(!hasFieldDeclAbstraction<cir::MissingFeatures>::value,
                "fieldDeclAbstraction should stay removed while target "
                "lowering uses record-layout builder context directly");
  EXPECT_FALSE(hasFieldDeclAbstraction<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, FieldDeclGetMaxFieldAlignmentEntryRemoved) {
  static_assert(
      !hasFieldDeclGetMaxFieldAlignment<cir::MissingFeatures>::value,
      "fieldDeclGetMaxFieldAlignment should stay removed while max alignment "
      "constraints are pre-applied from AST in initializeLayout");
  EXPECT_FALSE(
      hasFieldDeclGetMaxFieldAlignment<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, FieldDeclIsBitfieldEntryRemoved) {
  static_assert(!hasFieldDeclIsBitfield<cir::MissingFeatures>::value,
                "fieldDeclIsBitfield should stay removed while record-layout "
                "lowering works on bitfield storage members directly");
  EXPECT_FALSE(hasFieldDeclIsBitfield<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, FieldDeclIsPotentiallyOverlappingEntryRemoved) {
  static_assert(
      !hasFieldDeclIsPotentiallyOverlapping<cir::MissingFeatures>::value,
      "fieldDeclIsPotentiallyOverlapping should stay removed while "
      "record-layout lowering computes overlap handling directly");
  EXPECT_FALSE(
      hasFieldDeclIsPotentiallyOverlapping<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, FixedPointTypeEntryRemoved) {
  static_assert(!hasFixedPointType<cir::MissingFeatures>::value,
                "fixedPointType should stay removed while fixed-point paths "
                "are handled through direct NYI diagnostics");
  EXPECT_FALSE(hasFixedPointType<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, FixedWidthIntegersEntryRemoved) {
  static_assert(!hasFixedWidthIntegers<cir::MissingFeatures>::value,
                "fixedWidthIntegers should stay removed while ABI promotion "
                "queries rely on CIRLowerContext::isPromotableIntegerType");
  EXPECT_FALSE(hasFixedWidthIntegers<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, GetCXXRecordBasesEntryRemoved) {
  static_assert(
      !hasGetCXXRecordBases<cir::MissingFeatures>::value,
      "getCXXRecordBases should stay removed now that target lowering "
      "handles C++ base-subobject bookkeeping and x86 base classification");
  EXPECT_FALSE(hasGetCXXRecordBases<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, HipEntryRemoved) {
  static_assert(!hasHip<cir::MissingFeatures>::value,
                "hip should stay removed now that call emission and inline asm "
                "constraint validation no longer use a generic HIP guard");
  EXPECT_FALSE(hasHip<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, HipModuleCtorEntryRemoved) {
  static_assert(
      !hasHipModuleCtor<cir::MissingFeatures>::value,
      "hipModuleCtor should stay removed now that LoweringPrepare directly "
      "builds HIP/CUDA module ctors without a stale MissingFeatures gate");
  EXPECT_FALSE(hasHipModuleCtor<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, HlslEntryRemoved) {
  static_assert(
      !hasHlsl<cir::MissingFeatures>::value,
      "hlsl should stay removed now that generic non-HLSL code paths no longer "
      "carry stale MissingFeatures hlsl guard asserts");
  EXPECT_FALSE(hasHlsl<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, HlslConstantBufferGlobalSkipEntryRemoved) {
  static_assert(!hasHlslConstantBufferGlobalSkip<cir::MissingFeatures>::value,
                "hlslConstantBufferGlobalSkip should stay removed now that "
                "CIR emitGlobalVarDefinition directly mirrors OGCG's early "
                "return for hlsl_constant globals");
  EXPECT_FALSE(hasHlslConstantBufferGlobalSkip<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, HlslControlFlowHintAttrEntryRemoved) {
  static_assert(
      !hasHlslControlFlowHintAttr<cir::MissingFeatures>::value,
      "hlslControlFlowHintAttr should stay removed now that CIR threads "
      "HLSL [branch]/[flatten] statement hints through if/switch lowering");
  EXPECT_FALSE(hasHlslControlFlowHintAttr<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, HlslFRemEntryRemoved) {
  static_assert(
      !hasHlslFRem<cir::MissingFeatures>::value,
      "hlslFRem should stay removed now that CIR rem supports FP operands and "
      "lowers them to llvm.frem");
  EXPECT_FALSE(hasHlslFRem<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, HlslInlineSpirvTypeEntryRemoved) {
  static_assert(
      !hasHlslInlineSpirvType<cir::MissingFeatures>::value,
      "hlslInlineSpirvType should stay removed now that CIR mirrors OGCG by "
      "treating HLSLInlineSpirvType as non-zero-initializable");
  EXPECT_FALSE(hasHlslInlineSpirvType<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, PackedVectorBoolTypeEntryRemoved) {
  static_assert(
      !hasPackedVectorBoolType<cir::MissingFeatures>::value,
      "packedVectorBoolType should stay removed now that packed "
      "ext_vector_bool uses iP memory storage plus emitToMemory/"
      "emitFromMemory bitcast+shuffle conversion");
  EXPECT_FALSE(hasPackedVectorBoolType<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, PragmaClangSectionAttrEntryRemoved) {
  static_assert(
      !hasPragmaClangSectionAttr<cir::MissingFeatures>::value,
      "pragmaClangSectionAttr should stay removed now that CIR forwards "
      "#pragma clang section bss/data/rodata/relro attrs for globals/static "
      "locals and applies PragmaClangTextSectionAttr to functions");
  EXPECT_FALSE(hasPragmaClangSectionAttr<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, RegisterGlobalDtorsWithAtExitEntryRemoved) {
  static_assert(
      !hasRegisterGlobalDtorsWithAtExit<cir::MissingFeatures>::value,
      "registerGlobalDtorsWithAtExit should stay removed once CIR lowers "
      "-fregister-global-dtors-with-atexit through "
      "cir.register_global_dtor_with_atexit markers and synthesized "
      "__GLOBAL_init_<priority> helpers");
  EXPECT_FALSE(hasRegisterGlobalDtorsWithAtExit<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, HlslShiftMaskingEntryRemoved) {
  static_assert(
      !hasHlslShiftMasking<cir::MissingFeatures>::value,
      "hlslShiftMasking should stay removed now that CIR emitShl/emitShr "
      "apply OGCG-compatible HLSL shift-amount masking");
  EXPECT_FALSE(hasHlslShiftMasking<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, InallocaArgsEntryRemoved) {
  static_assert(
      !hasInallocaArgs<cir::MissingFeatures>::value,
      "inallocaArgs should stay removed: CIR now encodes inalloca NYI "
      "directly in ABIArgInfo::InAlloca switch branches");
  EXPECT_FALSE(hasInallocaArgs<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, InitSegAttrEntryRemoved) {
  static_assert(
      !hasInitSegAttr<cir::MissingFeatures>::value,
      "initSegAttr should stay removed now that CIR maps .CRT$XCC/.CRT$XCL "
      "to OGCG-compatible ctor priorities");
  EXPECT_FALSE(hasInitSegAttr<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, EhCleanupHasPrebranchedFallthroughEntryRemoved) {
  static_assert(
      !hasEhCleanupHasPrebranchedFallthrough<cir::MissingFeatures>::value,
      "ehCleanupHasPrebranchedFallthrough should stay removed because CIR "
      "cleanup routing does not use OGCG NormalCleanupDestSlot state");
  EXPECT_FALSE(
      hasEhCleanupHasPrebranchedFallthrough<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, EhFilterScopeEntryRemoved) {
  static_assert(!hasEhFilterScope<cir::MissingFeatures>::value,
                "ehFilterScope should stay removed while CIR models "
                "throw() as terminate-scope and diagnoses dynamic specs NYI");
  EXPECT_FALSE(hasEhFilterScope<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, EhMayThrowVsHasEHBranchesEntryRemoved) {
  static_assert(
      !hasEhMayThrowVsHasEHBranches<cir::MissingFeatures>::value,
      "ehMayThrowVsHasEHBranches should stay removed while CIR uses "
      "EHScope::scopeMayThrow for catch early-bailout logic");
  EXPECT_FALSE(hasEhMayThrowVsHasEHBranches<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ElideOverflowSanitizerChecksEntryRemoved) {
  static_assert(!hasElideOverflowSanitizerChecks<cir::MissingFeatures>::value,
                "elideOverflowSanitizerChecks should stay removed once stale "
                "guarding in canElideOverflowCheck is deleted");
  EXPECT_FALSE(hasElideOverflowSanitizerChecks<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, EmbedExprEmissionEntryRemoved) {
  static_assert(!hasEmbedExprEmission<cir::MissingFeatures>::value,
                "embedExprEmission should stay removed while CIR handles "
                "EmbedExpr in scalar/aggregate/constant init paths");
  EXPECT_FALSE(hasEmbedExprEmission<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, EmitCtorDtorListsEntryRemoved) {
  static_assert(!hasEmitCtorDtorLists<cir::MissingFeatures>::value,
                "emitCtorDtorLists should stay removed while CIR lowers "
                "GlobalOp ctor/dtor regions to LLVM ctor/dtor lists");
  EXPECT_FALSE(hasEmitCtorDtorLists<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, EmitCXXGlobalInitFuncEntryRemoved) {
  static_assert(!hasEmitCXXGlobalInitFunc<cir::MissingFeatures>::value,
                "emitCXXGlobalInitFunc should stay removed while CIR lowers "
                "global ctor/dtor attributes in LoweringPrepare");
  EXPECT_FALSE(hasEmitCXXGlobalInitFunc<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, EmitLValueAlignmentAssumptionEntryRemoved) {
  static_assert(!hasEmitLValueAlignmentAssumption<cir::MissingFeatures>::value,
                "emitLValueAlignmentAssumption should stay removed while "
                "ScalarExprEmitter emits lvalue alignment assumptions directly");
  EXPECT_FALSE(
      hasEmitLValueAlignmentAssumption<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, EmitModuleLinkOptionsEntryRemoved) {
  static_assert(!hasEmitModuleLinkOptions<cir::MissingFeatures>::value,
                "emitModuleLinkOptions should stay removed while CIR emits "
                "cir.linker_options in CIRGenerator::Initialize");
  EXPECT_FALSE(hasEmitModuleLinkOptions<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, EmitTypeMetadataCodeForVCallEntryRemoved) {
  static_assert(
      !hasEmitTypeMetadataCodeForVCall<cir::MissingFeatures>::value,
      "emitTypeMetadataCodeForVCall should stay removed while vcall metadata "
      "handling is tracked through createInvariantGroup/type-checked-load NYI");
  EXPECT_FALSE(hasEmitTypeMetadataCodeForVCall<cir::MissingFeatures>::value);
}

// --- Stale guard removal batch (27 guards with zero callers) ---

template <typename T, typename = void>
struct hasFoldRangeCase : std::false_type {};
template <typename T>
struct hasFoldRangeCase<T, std::void_t<decltype(&T::foldRangeCase)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasCoroOutsideFrameMD : std::false_type {};
template <typename T>
struct hasCoroOutsideFrameMD<T, std::void_t<decltype(&T::coroOutsideFrameMD)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasAtomicMisalignedOversizedDiag : std::false_type {};
template <typename T>
struct hasAtomicMisalignedOversizedDiag<
    T, std::void_t<decltype(&T::atomicMisalignedOversizedDiag)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasBuiltinCheckKind : std::false_type {};
template <typename T>
struct hasBuiltinCheckKind<T, std::void_t<decltype(&T::builtinCheckKind)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasConvergenceTokens : std::false_type {};
template <typename T>
struct hasConvergenceTokens<T, std::void_t<decltype(&T::convergenceTokens)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasConstructABIArgDirectExtend : std::false_type {};
template <typename T>
struct hasConstructABIArgDirectExtend<
    T, std::void_t<decltype(&T::constructABIArgDirectExtend)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasCreateProfileWeightsForLoop : std::false_type {};
template <typename T>
struct hasCreateProfileWeightsForLoop<
    T, std::void_t<decltype(&T::createProfileWeightsForLoop)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasCudaDeviceVarODRUsed : std::false_type {};
template <typename T>
struct hasCudaDeviceVarODRUsed<
    T, std::void_t<decltype(&T::cudaDeviceVarODRUsed)>> : std::true_type {};

template <typename T, typename = void>
struct hasDataLayoutTypeAllocSize : std::false_type {};
template <typename T>
struct hasDataLayoutTypeAllocSize<
    T, std::void_t<decltype(&T::dataLayoutTypeAllocSize)>> : std::true_type {};

template <typename T, typename = void>
struct hasEmitCheckedInBoundsGEP : std::false_type {};
template <typename T>
struct hasEmitCheckedInBoundsGEP<
    T, std::void_t<decltype(&T::emitCheckedInBoundsGEP)>> : std::true_type {};

template <typename T, typename = void>
struct hasEmitNullabilityCheck : std::false_type {};
template <typename T>
struct hasEmitNullabilityCheck<
    T, std::void_t<decltype(&T::emitNullabilityCheck)>> : std::true_type {};

template <typename T, typename = void>
struct hasDebugInfoVLADimensions : std::false_type {};
template <typename T>
struct hasDebugInfoVLADimensions<
    T, std::void_t<decltype(&T::debugInfoVLADimensions)>> : std::true_type {};

template <typename T, typename = void>
struct hasDebugInfoScopeCleanup : std::false_type {};
template <typename T>
struct hasDebugInfoScopeCleanup<
    T, std::void_t<decltype(&T::debugInfoScopeCleanup)>> : std::true_type {};

template <typename T, typename = void>
struct hasHandleBuiltinICEArguments : std::false_type {};
template <typename T>
struct hasHandleBuiltinICEArguments<
    T, std::void_t<decltype(&T::handleBuiltinICEArguments)>> : std::true_type {
};

template <typename T, typename = void>
struct hasCreateMemCpy : std::false_type {};
template <typename T>
struct hasCreateMemCpy<T, std::void_t<decltype(&T::createMemCpy)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasShouldCreateMemCpyFromGlobal : std::false_type {};
template <typename T>
struct hasShouldCreateMemCpyFromGlobal<
    T, std::void_t<decltype(&T::shouldCreateMemCpyFromGlobal)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasMergeAllConstants : std::false_type {};
template <typename T>
struct hasMergeAllConstants<T, std::void_t<decltype(&T::mergeAllConstants)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasNewArraySizeWiderThanSizeT : std::false_type {};
template <typename T>
struct hasNewArraySizeWiderThanSizeT<
    T, std::void_t<decltype(&T::newArraySizeWiderThanSizeT)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasNewExprConditionalEvaluation : std::false_type {};
template <typename T>
struct hasNewExprConditionalEvaluation<
    T, std::void_t<decltype(&T::newExprConditionalEvaluation)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasMustTailCall : std::false_type {};
template <typename T>
struct hasMustTailCall<T, std::void_t<decltype(&T::mustTailCall)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasTryEmitAsConstant : std::false_type {};
template <typename T>
struct hasTryEmitAsConstant<T, std::void_t<decltype(&T::tryEmitAsConstant)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasChainCall : std::false_type {};
template <typename T>
struct hasChainCall<T, std::void_t<decltype(&T::chainCall)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasNoEscapeAttr : std::false_type {};
template <typename T>
struct hasNoEscapeAttr<T, std::void_t<decltype(&T::noEscapeAttr)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasCtorMemcpyRecordTypes : std::false_type {};
template <typename T>
struct hasCtorMemcpyRecordTypes<
    T, std::void_t<decltype(&T::ctorMemcpyRecordTypes)>> : std::true_type {};

template <typename T, typename = void>
struct hasConditionalCleanup : std::false_type {};
template <typename T>
struct hasConditionalCleanup<T, std::void_t<decltype(&T::conditionalCleanup)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasCxx20NamedModuleVTableLinkage : std::false_type {};
template <typename T>
struct hasCxx20NamedModuleVTableLinkage<
    T, std::void_t<decltype(&T::cxx20NamedModuleVTableLinkage)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasEmitEmptyRecordCheck : std::false_type {};
template <typename T>
struct hasEmitEmptyRecordCheck<
    T, std::void_t<decltype(&T::emitEmptyRecordCheck)>> : std::true_type {};

template <typename T, typename = void>
struct hasEvaluationKind : std::false_type {};
template <typename T>
struct hasEvaluationKind<T, std::void_t<decltype(&T::evaluationKind)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasConstEmitterAbstractArrayFiller : std::false_type {};
template <typename T>
struct hasConstEmitterAbstractArrayFiller<
    T, std::void_t<decltype(&T::constEmitterAbstractArrayFiller)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasComplexPromotionForHigherPrecision : std::false_type {};
template <typename T>
struct hasComplexPromotionForHigherPrecision<
    T, std::void_t<decltype(&T::complexPromotionForHigherPrecision)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasCxxabiTerminateForUnexpectedException : std::false_type {};
template <typename T>
struct hasCxxabiTerminateForUnexpectedException<
    T, std::void_t<decltype(&T::cxxabiTerminateForUnexpectedException)>>
    : std::true_type {};

TEST(MissingFeaturesTest, FoldRangeCaseEntryRemoved) {
  static_assert(!hasFoldRangeCase<cir::MissingFeatures>::value,
                "foldRangeCase should stay removed");
  EXPECT_FALSE(hasFoldRangeCase<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, CoroOutsideFrameMDEntryRemoved) {
  static_assert(!hasCoroOutsideFrameMD<cir::MissingFeatures>::value,
                "coroOutsideFrameMD should stay removed");
  EXPECT_FALSE(hasCoroOutsideFrameMD<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, AtomicMisalignedOversizedDiagEntryRemoved) {
  static_assert(
      !hasAtomicMisalignedOversizedDiag<cir::MissingFeatures>::value,
      "atomicMisalignedOversizedDiag should stay removed");
  EXPECT_FALSE(
      hasAtomicMisalignedOversizedDiag<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, BuiltinCheckKindEntryRemoved) {
  static_assert(!hasBuiltinCheckKind<cir::MissingFeatures>::value,
                "builtinCheckKind should stay removed");
  EXPECT_FALSE(hasBuiltinCheckKind<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ConvergenceTokensEntryRemoved) {
  static_assert(!hasConvergenceTokens<cir::MissingFeatures>::value,
                "convergenceTokens should stay removed");
  EXPECT_FALSE(hasConvergenceTokens<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ConstructABIArgDirectExtendEntryRemoved) {
  static_assert(
      !hasConstructABIArgDirectExtend<cir::MissingFeatures>::value,
      "constructABIArgDirectExtend should stay removed");
  EXPECT_FALSE(
      hasConstructABIArgDirectExtend<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, CreateProfileWeightsForLoopEntryRemoved) {
  static_assert(
      !hasCreateProfileWeightsForLoop<cir::MissingFeatures>::value,
      "createProfileWeightsForLoop should stay removed");
  EXPECT_FALSE(
      hasCreateProfileWeightsForLoop<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, CudaDeviceVarODRUsedEntryRemoved) {
  static_assert(!hasCudaDeviceVarODRUsed<cir::MissingFeatures>::value,
                "cudaDeviceVarODRUsed should stay removed");
  EXPECT_FALSE(hasCudaDeviceVarODRUsed<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, DataLayoutTypeAllocSizeEntryRemoved) {
  static_assert(!hasDataLayoutTypeAllocSize<cir::MissingFeatures>::value,
                "dataLayoutTypeAllocSize should stay removed");
  EXPECT_FALSE(hasDataLayoutTypeAllocSize<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, EmitCheckedInBoundsGEPEntryRemoved) {
  static_assert(!hasEmitCheckedInBoundsGEP<cir::MissingFeatures>::value,
                "emitCheckedInBoundsGEP should stay removed");
  EXPECT_FALSE(hasEmitCheckedInBoundsGEP<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, EmitNullabilityCheckEntryRemoved) {
  static_assert(!hasEmitNullabilityCheck<cir::MissingFeatures>::value,
                "emitNullabilityCheck should stay removed");
  EXPECT_FALSE(hasEmitNullabilityCheck<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, DebugInfoVLADimensionsEntryRemoved) {
  static_assert(!hasDebugInfoVLADimensions<cir::MissingFeatures>::value,
                "debugInfoVLADimensions should stay removed");
  EXPECT_FALSE(hasDebugInfoVLADimensions<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, DebugInfoScopeCleanupEntryRemoved) {
  static_assert(!hasDebugInfoScopeCleanup<cir::MissingFeatures>::value,
                "debugInfoScopeCleanup should stay removed");
  EXPECT_FALSE(hasDebugInfoScopeCleanup<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, HandleBuiltinICEArgumentsEntryRemoved) {
  static_assert(!hasHandleBuiltinICEArguments<cir::MissingFeatures>::value,
                "handleBuiltinICEArguments should stay removed");
  EXPECT_FALSE(hasHandleBuiltinICEArguments<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, IncrementProfileCounterEntryRemoved) {
  static_assert(
      !hasIncrementProfileCounter<cir::MissingFeatures>::value,
      "incrementProfileCounter should stay removed while CIR tracks profile "
      "and coverage gaps via dedicated MissingFeatures guards");
  EXPECT_FALSE(hasIncrementProfileCounter<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, InstrumentationEntryRemoved) {
  static_assert(
      !hasInstrumentation<cir::MissingFeatures>::value,
      "instrumentation should stay removed now that CIR directly propagates "
      "-finstrument-functions attributes");
  EXPECT_FALSE(hasInstrumentation<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, IntrinsicsEntryRemoved) {
  static_assert(
      !hasIntrinsics<cir::MissingFeatures>::value,
      "intrinsics should stay removed since no MissingFeatures::intrinsics "
      "call sites remain");
  EXPECT_FALSE(hasIntrinsics<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, CreateMemCpyEntryRemoved) {
  static_assert(!hasCreateMemCpy<cir::MissingFeatures>::value,
                "createMemCpy should stay removed");
  EXPECT_FALSE(hasCreateMemCpy<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ShouldCreateMemCpyFromGlobalEntryRemoved) {
  static_assert(
      !hasShouldCreateMemCpyFromGlobal<cir::MissingFeatures>::value,
      "shouldCreateMemCpyFromGlobal should stay removed now that "
      "AggExprEmitter directly handles copy-from-global array-init lowering");
  EXPECT_FALSE(hasShouldCreateMemCpyFromGlobal<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, MergeAllConstantsEntryRemoved) {
  static_assert(!hasMergeAllConstants<cir::MissingFeatures>::value,
                "mergeAllConstants should stay removed");
  EXPECT_FALSE(hasMergeAllConstants<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, NewArraySizeWiderThanSizeTEntryRemoved) {
  static_assert(
      !hasNewArraySizeWiderThanSizeT<cir::MissingFeatures>::value,
      "newArraySizeWiderThanSizeT should stay removed now that CIR matches "
      "OGCG wide-count overflow handling in dynamic new[] size computation");
  EXPECT_FALSE(hasNewArraySizeWiderThanSizeT<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, NewExprConditionalEvaluationEntryRemoved) {
  static_assert(
      !hasNewExprConditionalEvaluation<cir::MissingFeatures>::value,
      "newExprConditionalEvaluation should stay removed now that CIR models "
      "null-checked new-expression cleanup scoping with ternary regions");
  EXPECT_FALSE(hasNewExprConditionalEvaluation<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, MustTailCallEntryRemoved) {
  static_assert(!hasMustTailCall<cir::MissingFeatures>::value,
                "mustTailCall should stay removed now that CIR emits "
                "musttail in call emission and thunk forwarding paths");
  EXPECT_FALSE(hasMustTailCall<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasNamedBreakContinue : std::false_type {};
template <typename T>
struct hasNamedBreakContinue<
    T, std::void_t<decltype(&T::namedBreakContinue)>> : std::true_type {};

TEST(MissingFeaturesTest, NamedBreakContinueEntryRemoved) {
  static_assert(!hasNamedBreakContinue<cir::MissingFeatures>::value,
                "namedBreakContinue should stay removed — CIR now emits "
                "explicit NYI diagnostics for named break/continue");
  EXPECT_FALSE(hasNamedBreakContinue<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasNewDeleteTypeIdentityArg : std::false_type {};
template <typename T>
struct hasNewDeleteTypeIdentityArg<
    T, std::void_t<decltype(&T::newDeleteTypeIdentityArg)>>
    : std::true_type {};

TEST(MissingFeaturesTest, NewDeleteTypeIdentityArgEntryRemoved) {
  static_assert(!hasNewDeleteTypeIdentityArg<cir::MissingFeatures>::value,
                "newDeleteTypeIdentityArg should stay removed now that CIR "
                "threads type_identity through new/delete cleanup emission");
  EXPECT_FALSE(hasNewDeleteTypeIdentityArg<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasNonTrivialCStructDestroy : std::false_type {};
template <typename T>
struct hasNonTrivialCStructDestroy<
    T, std::void_t<decltype(&T::nonTrivialCStructDestroy)>> : std::true_type {};

TEST(MissingFeaturesTest, NonTrivialCStructDestroyEntryRemoved) {
  static_assert(!hasNonTrivialCStructDestroy<cir::MissingFeatures>::value,
                "nonTrivialCStructDestroy should stay removed now that "
                "aggregate conditional emission pushes DK_nontrivial_c_struct "
                "cleanup like OGCG");
  EXPECT_FALSE(hasNonTrivialCStructDestroy<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasObjCBlocks : std::false_type {};
template <typename T>
struct hasObjCBlocks<T, std::void_t<decltype(&T::objCBlocks)>>
    : std::true_type {};

TEST(MissingFeaturesTest, ObjCBlocksEntryRemoved) {
  static_assert(!hasObjCBlocks<cir::MissingFeatures>::value,
                "objCBlocks should stay removed now that stale assert guards "
                "were dropped and block NYI paths use explicit diagnostics");
  EXPECT_FALSE(hasObjCBlocks<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasObjCIvarDecls : std::false_type {};
template <typename T>
struct hasObjCIvarDecls<T, std::void_t<decltype(&T::objCIvarDecls)>>
    : std::true_type {};

TEST(MissingFeaturesTest, ObjCIvarDeclsEntryRemoved) {
  static_assert(!hasObjCIvarDecls<cir::MissingFeatures>::value,
                "objCIvarDecls should stay removed now that stale record "
                "layout guard bookkeeping is gone");
  EXPECT_FALSE(hasObjCIvarDecls<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasObjCLifetime : std::false_type {};
template <typename T>
struct hasObjCLifetime<T, std::void_t<decltype(&T::objCLifetime)>>
    : std::true_type {};

TEST(MissingFeaturesTest, ObjCLifetimeEntryRemoved) {
  static_assert(!hasObjCLifetime<cir::MissingFeatures>::value,
                "objCLifetime should stay removed now that stale assert "
                "bookkeeping is replaced with explicit NYI behavior");
  EXPECT_FALSE(hasObjCLifetime<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasObjCTerminate : std::false_type {};
template <typename T>
struct hasObjCTerminate<T, std::void_t<decltype(&T::objCTerminate)>>
    : std::true_type {};

TEST(MissingFeaturesTest, ObjCTerminateEntryRemoved) {
  static_assert(!hasObjCTerminate<cir::MissingFeatures>::value,
                "objCTerminate should stay removed now that terminate handler "
                "selection supports objc_terminate when available");
  EXPECT_FALSE(hasObjCTerminate<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpAllocaCaptureByInit : std::false_type {};
template <typename T>
struct hasOpAllocaCaptureByInit<
    T, std::void_t<decltype(&T::opAllocaCaptureByInit)>> : std::true_type {};

TEST(MissingFeaturesTest, OpAllocaCaptureByInitEntryRemoved) {
  static_assert(
      !hasOpAllocaCaptureByInit<cir::MissingFeatures>::value,
      "opAllocaCaptureByInit should stay removed since CIR rejects escaping "
      "byref auto vars before this path");
  EXPECT_FALSE(hasOpAllocaCaptureByInit<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpAllocaEscapeByReference : std::false_type {};
template <typename T>
struct hasOpAllocaEscapeByReference<
    T, std::void_t<decltype(&T::opAllocaEscapeByReference)>>
    : std::true_type {};

TEST(MissingFeaturesTest, OpAllocaEscapeByReferenceEntryRemoved) {
  static_assert(!hasOpAllocaEscapeByReference<cir::MissingFeatures>::value,
                "opAllocaEscapeByReference should stay removed now that "
                "call-sites use explicit NYI diagnostics");
  EXPECT_FALSE(hasOpAllocaEscapeByReference<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpAllocaPreciseLifetime : std::false_type {};
template <typename T>
struct hasOpAllocaPreciseLifetime<
    T, std::void_t<decltype(&T::opAllocaPreciseLifetime)>> : std::true_type {};

TEST(MissingFeaturesTest, OpAllocaPreciseLifetimeEntryRemoved) {
  static_assert(
      !hasOpAllocaPreciseLifetime<cir::MissingFeatures>::value,
      "opAllocaPreciseLifetime should stay removed — ObjC GC is deprecated; "
      "ARC precise lifetime and LLVM lifetime markers are already handled");
  EXPECT_FALSE(hasOpAllocaPreciseLifetime<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpAllocaOpenMPThreadPrivate : std::false_type {};
template <typename T>
struct hasOpAllocaOpenMPThreadPrivate<
    T, std::void_t<decltype(&T::opAllocaOpenMPThreadPrivate)>>
    : std::true_type {};

TEST(MissingFeaturesTest, OpAllocaOpenMPThreadPrivateEntryRemoved) {
  static_assert(
      !hasOpAllocaOpenMPThreadPrivate<cir::MissingFeatures>::value,
      "opAllocaOpenMPThreadPrivate should stay removed because threadprivate "
      "declarations are currently diagnosed as NYI in CIR OpenMP decl "
      "emission");
  EXPECT_FALSE(hasOpAllocaOpenMPThreadPrivate<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpCallChain : std::false_type {};
template <typename T>
struct hasOpCallChain<T, std::void_t<decltype(&T::opCallChain)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasOpCallInAlloca : std::false_type {};
template <typename T>
struct hasOpCallInAlloca<T, std::void_t<decltype(&T::opCallInAlloca)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasOpCallObjCMethod : std::false_type {};
template <typename T>
struct hasOpCallObjCMethod<T, std::void_t<decltype(&T::opCallObjCMethod)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasOpCallCIRGenFuncInfoExtParamInfo : std::false_type {};
template <typename T>
struct hasOpCallCIRGenFuncInfoExtParamInfo<
    T, std::void_t<decltype(&T::opCallCIRGenFuncInfoExtParamInfo)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasOpenCLBuiltinTypes : std::false_type {};
template <typename T>
struct hasOpenCLBuiltinTypes<T, std::void_t<decltype(&T::openCLBuiltinTypes)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasOpenMPLastprivate : std::false_type {};
template <typename T>
struct hasOpenMPLastprivate<T, std::void_t<decltype(&T::openMPLastprivate)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasOpFuncExtraAttrs : std::false_type {};
template <typename T>
struct hasOpFuncExtraAttrs<T, std::void_t<decltype(&T::opFuncExtraAttrs)>>
    : std::true_type {};

TEST(MissingFeaturesTest, OpCallChainEntryRemoved) {
  static_assert(!hasOpCallChain<cir::MissingFeatures>::value,
                "opCallChain should stay removed now that no call-site guards "
                "remain");
  EXPECT_FALSE(hasOpCallChain<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, OpCallInAllocaEntryRemoved) {
  static_assert(!hasOpCallInAlloca<cir::MissingFeatures>::value,
                "opCallInAlloca should stay removed; InAlloca remains explicit "
                "NYI in call-lowering ABIArgInfo handling");
  EXPECT_FALSE(hasOpCallInAlloca<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, OpCallObjCMethodEntryRemoved) {
  static_assert(!hasOpCallObjCMethod<cir::MissingFeatures>::value,
                "opCallObjCMethod should stay removed now that emitCallArgs "
                "handles ObjCMethodDecl prototypes directly");
  EXPECT_FALSE(hasOpCallObjCMethod<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, OpenCLBuiltinTypesEntryRemoved) {
  static_assert(
      !hasOpenCLBuiltinTypes<cir::MissingFeatures>::value,
      "openCLBuiltinTypes should stay removed; image/pipe OpenCL kernel arg "
      "metadata gaps are handled by explicit NYI diagnostics in CIRGenModule");
  EXPECT_FALSE(hasOpenCLBuiltinTypes<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, OpenMPLastprivateEntryRemoved) {
  static_assert(
      !hasOpenMPLastprivate<cir::MissingFeatures>::value,
      "openMPLastprivate should stay removed; lastprivate(condition:) work is "
      "tracked via OpenMP runtime/lowering TODOs, not a global guard");
  EXPECT_FALSE(hasOpenMPLastprivate<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, OpFuncExtraAttrsEntryRemoved) {
  static_assert(
      !hasOpFuncExtraAttrs<cir::MissingFeatures>::value,
      "opFuncExtraAttrs should stay removed because extra_attrs are now "
      "explicitly handled in CIR lowering paths");
  EXPECT_FALSE(hasOpFuncExtraAttrs<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpCallCIRGenFuncInfoParamInfo : std::false_type {};
template <typename T>
struct hasOpCallCIRGenFuncInfoParamInfo<
    T, std::void_t<decltype(&T::opCallCIRGenFuncInfoParamInfo)>>
    : std::true_type {};

TEST(MissingFeaturesTest, OpCallCIRGenFuncInfoParamInfoEntryRemoved) {
  static_assert(!hasOpCallCIRGenFuncInfoParamInfo<cir::MissingFeatures>::value,
                "opCallCIRGenFuncInfoParamInfo should stay removed; "
                "CIRGenFunctionInfo stores canonical arg types only");
  EXPECT_FALSE(
      hasOpCallCIRGenFuncInfoParamInfo<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, OpCallCIRGenFuncInfoExtParamInfoEntryRemoved) {
  static_assert(
      !hasOpCallCIRGenFuncInfoExtParamInfo<cir::MissingFeatures>::value,
      "opCallCIRGenFuncInfoExtParamInfo should stay removed now that "
      "CIRGenFunctionInfo no longer tracks ext-parameter side-buffer state");
  EXPECT_FALSE(
      hasOpCallCIRGenFuncInfoExtParamInfo<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpCallExtParameterInfo : std::false_type {};
template <typename T>
struct hasOpCallExtParameterInfo<
    T, std::void_t<decltype(&T::opCallExtParameterInfo)>> : std::true_type {};

TEST(MissingFeaturesTest, OpCallExtParameterInfoEntryRemoved) {
  static_assert(!hasOpCallExtParameterInfo<cir::MissingFeatures>::value,
                "opCallExtParameterInfo should stay removed because CIR "
                "already consumes ext parameter info where needed");
  EXPECT_FALSE(hasOpCallExtParameterInfo<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpCallPaddingArgs : std::false_type {};
template <typename T>
struct hasOpCallPaddingArgs<T, std::void_t<decltype(&T::opCallPaddingArgs)>>
    : std::true_type {};

TEST(MissingFeaturesTest, OpCallPaddingArgsEntryRemoved) {
  static_assert(!hasOpCallPaddingArgs<cir::MissingFeatures>::value,
                "opCallPaddingArgs should stay removed; call coercion/padding "
                "is handled in CIR ABI lowering");
  EXPECT_FALSE(hasOpCallPaddingArgs<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpCallReturn : std::false_type {};
template <typename T>
struct hasOpCallReturn<T, std::void_t<decltype(&T::opCallReturn)>>
    : std::true_type {};

TEST(MissingFeaturesTest, OpCallReturnEntryRemoved) {
  static_assert(!hasOpCallReturn<cir::MissingFeatures>::value,
                "opCallReturn should stay removed; emitCall handles "
                "noreturn/musttail return control flow directly");
  EXPECT_FALSE(hasOpCallReturn<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpenCL : std::false_type {};
template <typename T>
struct hasOpenCL<T, std::void_t<decltype(&T::openCL)>> : std::true_type {};

template <typename T, typename = void>
struct hasOpenCLKernelStubCallingConv : std::false_type {};
template <typename T>
struct hasOpenCLKernelStubCallingConv<
    T, std::void_t<decltype(&T::openCLKernelStubCallingConv)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasOpenCLKernelStubDeferral : std::false_type {};
template <typename T>
struct hasOpenCLKernelStubDeferral<
    T, std::void_t<decltype(&T::openCLKernelStubDeferral)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasOpenMP : std::false_type {};
template <typename T>
struct hasOpenMP<T, std::void_t<decltype(&T::openMP)>> : std::true_type {};

TEST(MissingFeaturesTest, OpenCLEntryRemoved) {
  static_assert(!hasOpenCL<cir::MissingFeatures>::value,
                "openCL should stay removed; stale assert bookkeeping is "
                "replaced by specific OpenCL guards/NYI paths");
  EXPECT_FALSE(hasOpenCL<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, OpenCLKernelStubCallingConvEntryRemoved) {
  static_assert(
      !hasOpenCLKernelStubCallingConv<cir::MissingFeatures>::value,
      "openCLKernelStubCallingConv should stay removed; CIR does not yet "
      "materialize OpenCL kernel-stub call paths");
  EXPECT_FALSE(hasOpenCLKernelStubCallingConv<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, OpenCLKernelStubDeferralEntryRemoved) {
  static_assert(
      !hasOpenCLKernelStubDeferral<cir::MissingFeatures>::value,
      "openCLKernelStubDeferral should stay removed; CIR does not yet "
      "materialize OpenCL kernel-stub emission deferral paths");
  EXPECT_FALSE(hasOpenCLKernelStubDeferral<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, OpenMPEntryRemoved) {
  static_assert(!hasOpenMP<cir::MissingFeatures>::value,
                "openMP should stay removed; broad stale bookkeeping was "
                "replaced by focused OpenMP NYI guards");
  EXPECT_FALSE(hasOpenMP<cir::MissingFeatures>::value);
}
TEST(MissingFeaturesTest, TryEmitAsConstantEntryRemoved) {
  static_assert(!hasTryEmitAsConstant<cir::MissingFeatures>::value,
                "tryEmitAsConstant should stay removed");
  EXPECT_FALSE(hasTryEmitAsConstant<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ChainCallEntryRemoved) {
  static_assert(!hasChainCall<cir::MissingFeatures>::value,
                "chainCall should stay removed");
  EXPECT_FALSE(hasChainCall<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, NoEscapeAttrEntryRemoved) {
  static_assert(!hasNoEscapeAttr<cir::MissingFeatures>::value,
                "noEscapeAttr should stay removed");
  EXPECT_FALSE(hasNoEscapeAttr<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, CtorMemcpyRecordTypesEntryRemoved) {
  static_assert(!hasCtorMemcpyRecordTypes<cir::MissingFeatures>::value,
                "ctorMemcpyRecordTypes should stay removed");
  EXPECT_FALSE(hasCtorMemcpyRecordTypes<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ConditionalCleanupEntryRemoved) {
  static_assert(!hasConditionalCleanup<cir::MissingFeatures>::value,
                "conditionalCleanup should stay removed");
  EXPECT_FALSE(hasConditionalCleanup<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, Cxx20NamedModuleVTableLinkageEntryRemoved) {
  static_assert(
      !hasCxx20NamedModuleVTableLinkage<cir::MissingFeatures>::value,
      "cxx20NamedModuleVTableLinkage should stay removed");
  EXPECT_FALSE(
      hasCxx20NamedModuleVTableLinkage<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, EmitEmptyRecordCheckEntryRemoved) {
  static_assert(!hasEmitEmptyRecordCheck<cir::MissingFeatures>::value,
                "emitEmptyRecordCheck should stay removed");
  EXPECT_FALSE(hasEmitEmptyRecordCheck<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, EvaluationKindEntryRemoved) {
  static_assert(!hasEvaluationKind<cir::MissingFeatures>::value,
                "evaluationKind should stay removed");
  EXPECT_FALSE(hasEvaluationKind<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ConstEmitterAbstractArrayFillerEntryRemoved) {
  static_assert(
      !hasConstEmitterAbstractArrayFiller<cir::MissingFeatures>::value,
      "constEmitterAbstractArrayFiller should stay removed");
  EXPECT_FALSE(
      hasConstEmitterAbstractArrayFiller<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ComplexPromotionForHigherPrecisionEntryRemoved) {
  static_assert(
      !hasComplexPromotionForHigherPrecision<cir::MissingFeatures>::value,
      "complexPromotionForHigherPrecision should stay removed");
  EXPECT_FALSE(
      hasComplexPromotionForHigherPrecision<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest,
     CxxabiTerminateForUnexpectedExceptionEntryRemoved) {
  static_assert(
      !hasCxxabiTerminateForUnexpectedException<cir::MissingFeatures>::value,
      "cxxabiTerminateForUnexpectedException should stay removed");
  EXPECT_FALSE(
      hasCxxabiTerminateForUnexpectedException<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasCUDA : std::false_type {};
template <typename T>
struct hasCUDA<T, std::void_t<decltype(&T::CUDA)>> : std::true_type {};

TEST(MissingFeaturesTest, CUDAEntryRemoved) {
  static_assert(!hasCUDA<cir::MissingFeatures>::value,
                "CUDA should stay removed");
  EXPECT_FALSE(hasCUDA<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasNorecurseMainAttr : std::false_type {};
template <typename T>
struct hasNorecurseMainAttr<T, std::void_t<decltype(&T::norecurseMainAttr)>>
    : std::true_type {};

TEST(MissingFeaturesTest, NorecurseMainAttrEntryRemoved) {
  static_assert(!hasNorecurseMainAttr<cir::MissingFeatures>::value,
                "norecurseMainAttr should stay removed now that norecurse "
                "and stackrealign-on-main are in setNonAliasAttributes");
  EXPECT_FALSE(hasNorecurseMainAttr<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasSemanticInterposition : std::false_type {};
template <typename T>
struct hasSemanticInterposition<
    T, std::void_t<decltype(&T::semanticInterposition)>> : std::true_type {};

TEST(MissingFeaturesTest, SemanticInterpositionEntryRemoved) {
  static_assert(!hasSemanticInterposition<cir::MissingFeatures>::value,
                "semanticInterposition should stay removed — OGCG's "
                "TryMarkNoThrow does not effectively check this flag");
  EXPECT_FALSE(hasSemanticInterposition<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasIrGenFinishedGuard : std::false_type {};
template <typename T>
struct hasIrGenFinishedGuard<T,
                             std::void_t<decltype(&T::irGenFinishedGuard)>>
    : std::true_type {};

TEST(MissingFeaturesTest, IrGenFinishedGuardEntryRemoved) {
  static_assert(!hasIrGenFinishedGuard<cir::MissingFeatures>::value,
                "irGenFinishedGuard should stay removed — CIRGenerator now "
                "sets irGenFinished flag matching OGCG");
  EXPECT_FALSE(hasIrGenFinishedGuard<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasMultiversionResolver : std::false_type {};
template <typename T>
struct hasMultiversionResolver<
    T, std::void_t<decltype(&T::multiversionResolver)>> : std::true_type {};

TEST(MissingFeaturesTest, MultiversionResolverEntryRemoved) {
  static_assert(!hasMultiversionResolver<cir::MissingFeatures>::value,
                "multiversionResolver should stay removed now that "
                "multiversion paths use explicit target-kind checks");
  EXPECT_FALSE(hasMultiversionResolver<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasForbidGuardVariables : std::false_type {};
template <typename T>
struct hasForbidGuardVariables<T,
                               std::void_t<decltype(&T::forbidGuardVariables)>>
    : std::true_type {};

TEST(MissingFeaturesTest, ForbidGuardVariablesEntryRemoved) {
  static_assert(!hasForbidGuardVariables<cir::MissingFeatures>::value,
                "forbidGuardVariables should stay removed — CIR now checks "
                "CodeGenOpts.ForbidGuardVariables matching OGCG");
  EXPECT_FALSE(hasForbidGuardVariables<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasMcountFentryProfiling : std::false_type {};
template <typename T>
struct hasMcountFentryProfiling<
    T, std::void_t<decltype(&T::mcountFentryProfiling)>> : std::true_type {};

TEST(MissingFeaturesTest, McountFentryProfilingEntryRemoved) {
  static_assert(!hasMcountFentryProfiling<cir::MissingFeatures>::value,
                "mcountFentryProfiling should stay removed — CIR now handles "
                "-pg/-mfentry/-mnop-mcount/-mrecord-mcount profiling attrs");
  EXPECT_FALSE(hasMcountFentryProfiling<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasSetLLVMFunctionFEnvAttributes : std::false_type {};
template <typename T>
struct hasSetLLVMFunctionFEnvAttributes<
    T, std::void_t<decltype(&T::setLLVMFunctionFEnvAttributes)>>
    : std::true_type {};

TEST(MissingFeaturesTest, SetLLVMFunctionFEnvAttributesEntryRemoved) {
  static_assert(
      !hasSetLLVMFunctionFEnvAttributes<cir::MissingFeatures>::value,
      "setLLVMFunctionFEnvAttributes should stay removed — constrained FP "
      "is handled via strictfp attr and per-op isFPConstrained() checks");
  EXPECT_FALSE(hasSetLLVMFunctionFEnvAttributes<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasFinishFunctionCleanupAsserts : std::false_type {};
template <typename T>
struct hasFinishFunctionCleanupAsserts<
    T, std::void_t<decltype(&T::finishFunctionCleanupAsserts)>>
    : std::true_type {};

TEST(MissingFeaturesTest, FinishFunctionCleanupAssertsEntryRemoved) {
  static_assert(
      !hasFinishFunctionCleanupAsserts<cir::MissingFeatures>::value,
      "finishFunctionCleanupAsserts should stay removed — actual cleanup "
      "stack assertions are now in CIRGenFunction::finishFunction");
  EXPECT_FALSE(hasFinishFunctionCleanupAsserts<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasNullCheckClassCastValue : std::false_type {};
template <typename T>
struct hasNullCheckClassCastValue<
    T, std::void_t<decltype(&T::nullCheckClassCastValue)>> : std::true_type {};

TEST(MissingFeaturesTest, NullCheckClassCastValueEntryRemoved) {
  static_assert(!hasNullCheckClassCastValue<cir::MissingFeatures>::value,
                "nullCheckClassCastValue should stay removed — CIR now calls "
                "shouldNullCheckClassCastValue in all cast paths");
  EXPECT_FALSE(hasNullCheckClassCastValue<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasObjCGC : std::false_type {};
template <typename T>
struct hasObjCGC<T, std::void_t<decltype(&T::objCGC)>> : std::true_type {};

TEST(MissingFeaturesTest, ObjCGCEntryRemoved) {
  static_assert(!hasObjCGC<cir::MissingFeatures>::value,
                "objCGC should stay removed — ObjC GC was deprecated in 2012 "
                "and is not supported by CIR");
  EXPECT_FALSE(hasObjCGC<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasSetDLLStorageClass : std::false_type {};
template <typename T>
struct hasSetDLLStorageClass<T,
                             std::void_t<decltype(&T::setDLLStorageClass)>>
    : std::true_type {};

TEST(MissingFeaturesTest, SetDLLStorageClassEntryRemoved) {
  static_assert(!hasSetDLLStorageClass<cir::MissingFeatures>::value,
                "setDLLStorageClass should stay removed — DLL storage class "
                "is a Windows PE/COFF concept; Windows is not a CIR target");
  EXPECT_FALSE(hasSetDLLStorageClass<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasStaticVarSectionAttr : std::false_type {};
template <typename T>
struct hasStaticVarSectionAttr<
    T, std::void_t<decltype(&T::staticVarSectionAttr)>> : std::true_type {};

TEST(MissingFeaturesTest, StaticVarSectionAttrEntryRemoved) {
  static_assert(!hasStaticVarSectionAttr<cir::MissingFeatures>::value,
                "staticVarSectionAttr should stay removed — now implemented: "
                "SectionAttr forwarded via GlobalOp::setSectionAttr");
  EXPECT_FALSE(hasStaticVarSectionAttr<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasSetNonGC : std::false_type {};
template <typename T>
struct hasSetNonGC<T, std::void_t<decltype(&T::setNonGC)>> : std::true_type {};

TEST(MissingFeaturesTest, SetNonGCEntryRemoved) {
  static_assert(!hasSetNonGC<cir::MissingFeatures>::value,
                "setNonGC should stay removed — ObjC GC write barrier "
                "suppression; ObjC GC was deprecated in 2012");
  EXPECT_FALSE(hasSetNonGC<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasSetObjCGCLValueClass : std::false_type {};
template <typename T>
struct hasSetObjCGCLValueClass<T,
                               std::void_t<decltype(&T::setObjCGCLValueClass)>>
    : std::true_type {};

TEST(MissingFeaturesTest, SetObjCGCLValueClassEntryRemoved) {
  static_assert(!hasSetObjCGCLValueClass<cir::MissingFeatures>::value,
                "setObjCGCLValueClass should stay removed — ObjC GC LValue "
                "classification; ObjC GC was deprecated in 2012");
  EXPECT_FALSE(hasSetObjCGCLValueClass<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasIsRedundantBeforeReturn : std::false_type {};
template <typename T>
struct hasIsRedundantBeforeReturn<
    T, std::void_t<decltype(&T::isRedundantBeforeReturn)>> : std::true_type {};

TEST(MissingFeaturesTest, IsRedundantBeforeReturnEntryRemoved) {
  static_assert(
      !hasIsRedundantBeforeReturn<cir::MissingFeatures>::value,
      "isRedundantBeforeReturn should stay removed — CIR's Cleanup base "
      "class now has the virtual method, matching OGCG");
  EXPECT_FALSE(hasIsRedundantBeforeReturn<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasIsThisCompleteObject : std::false_type {};
template <typename T>
struct hasIsThisCompleteObject<
    T, std::void_t<decltype(&T::isThisCompleteObject)>> : std::true_type {};

TEST(MissingFeaturesTest, IsThisCompleteObjectEntryRemoved) {
  static_assert(
      !hasIsThisCompleteObject<cir::MissingFeatures>::value,
      "isThisCompleteObject should stay removed — CIR now implements "
      "isThisCompleteObject on CIRGenCXXABI matching OGCG");
  EXPECT_FALSE(hasIsThisCompleteObject<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpenMPNoAtExitDtor : std::false_type {};
template <typename T>
struct hasOpenMPNoAtExitDtor<
    T, std::void_t<decltype(&T::openMPNoAtExitDtor)>> : std::true_type {};

TEST(MissingFeaturesTest, OpenMPNoAtExitDtorEntryRemoved) {
  static_assert(
      !hasOpenMPNoAtExitDtor<cir::MissingFeatures>::value,
      "openMPNoAtExitDtor should stay removed now that CIR mirrors OGCG "
      "by lowering OpenMP target-device globals without hasAtExit() to "
      "llvm.global_dtors");
  EXPECT_FALSE(hasOpenMPNoAtExitDtor<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasTlsInitFunc : std::false_type {};
template <typename T>
struct hasTlsInitFunc<T, std::void_t<decltype(&T::tlsInitFunc)>>
    : std::true_type {};

TEST(MissingFeaturesTest, TlsInitFuncEntryRemoved) {
  static_assert(
      !hasTlsInitFunc<cir::MissingFeatures>::value,
      "tlsInitFunc should stay removed — CIR now emits TLS wrapper functions "
      "in CIRGen and lowers __tls_init/__tls_guard + _ZTH alias calls in "
      "LoweringPrepare");
  EXPECT_FALSE(hasTlsInitFunc<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasModuleTargetTriple : std::false_type {};
template <typename T>
struct hasModuleTargetTriple<T,
                             std::void_t<decltype(&T::moduleTargetTriple)>>
    : std::true_type {};

TEST(MissingFeaturesTest, ModuleTargetTripleEntryRemoved) {
  static_assert(
      !hasModuleTargetTriple<cir::MissingFeatures>::value,
      "moduleTargetTriple should stay removed — CIR sets cir.triple "
      "attribute on the MLIR module in CIRGenModule constructor");
  EXPECT_FALSE(hasModuleTargetTriple<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasVarDeclIsKNRPromoted : std::false_type {};
template <typename T>
struct hasVarDeclIsKNRPromoted<
    T, std::void_t<decltype(&T::varDeclIsKNRPromoted)>> : std::true_type {};

TEST(MissingFeaturesTest, VarDeclIsKNRPromotedEntryRemoved) {
  static_assert(!hasVarDeclIsKNRPromoted<cir::MissingFeatures>::value,
                "varDeclIsKNRPromoted should stay removed — CIR does not "
                "support K&R declarations");
  EXPECT_FALSE(hasVarDeclIsKNRPromoted<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasIsTrivialCtorOrDtor : std::false_type {};
template <typename T>
struct hasIsTrivialCtorOrDtor<T,
                              std::void_t<decltype(&T::isTrivialCtorOrDtor)>>
    : std::true_type {};

TEST(MissingFeaturesTest, IsTrivialCtorOrDtorEntryRemoved) {
  static_assert(!hasIsTrivialCtorOrDtor<cir::MissingFeatures>::value,
                "isTrivialCtorOrDtor should stay removed — was misnamed; "
                "replaced by focused sanitizer NYI handling in EmitTypeCheck");
  EXPECT_FALSE(hasIsTrivialCtorOrDtor<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasSimplifyCleanupEntry : std::false_type {};
template <typename T>
struct hasSimplifyCleanupEntry<T,
                               std::void_t<decltype(&T::simplifyCleanupEntry)>>
    : std::true_type {};

TEST(MissingFeaturesTest, SimplifyCleanupEntryRemoved) {
  static_assert(!hasSimplifyCleanupEntry<cir::MissingFeatures>::value,
                "simplifyCleanupEntry should stay removed — LLVM IR block "
                "merging not applicable to CIR structured control flow");
  EXPECT_FALSE(hasSimplifyCleanupEntry<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasEhstackBranches : std::false_type {};
template <typename T>
struct hasEhstackBranches<T, std::void_t<decltype(&T::ehstackBranches)>>
    : std::true_type {};

TEST(MissingFeaturesTest, EhstackBranchesRemoved) {
  static_assert(!hasEhstackBranches<cir::MissingFeatures>::value,
                "ehstackBranches should stay removed — CIR MLIR SSA handles "
                "value liveness without manual spilling");
  EXPECT_FALSE(hasEhstackBranches<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasCtorConstLvalueToRvalueConversion : std::false_type {};
template <typename T>
struct hasCtorConstLvalueToRvalueConversion<
    T, std::void_t<decltype(&T::ctorConstLvalueToRvalueConversion)>>
    : std::true_type {};

TEST(MissingFeaturesTest, CtorConstLvalueToRvalueConversionRemoved) {
  static_assert(
      !hasCtorConstLvalueToRvalueConversion<cir::MissingFeatures>::value,
      "ctorConstLvalueToRvalueConversion should stay removed — both CIR "
      "and OGCG intentionally return null for this case");
  EXPECT_FALSE(
      hasCtorConstLvalueToRvalueConversion<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasPeepholeProtection : std::false_type {};
template <typename T>
struct hasPeepholeProtection<T,
                             std::void_t<decltype(&T::peepholeProtection)>>
    : std::true_type {};

TEST(MissingFeaturesTest, PeepholeProtectionRemoved) {
  static_assert(!hasPeepholeProtection<cir::MissingFeatures>::value,
                "peepholeProtection should stay removed — MLIR does not do "
                "aggressive peephole optimization during IR construction");
  EXPECT_FALSE(hasPeepholeProtection<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasLongDoubleReferenced : std::false_type {};
template <typename T>
struct hasLongDoubleReferenced<T,
                               std::void_t<decltype(&T::longDoubleReferenced)>>
    : std::true_type {};

TEST(MissingFeaturesTest, LongDoubleReferencedEntryRemoved) {
  static_assert(
      !hasLongDoubleReferenced<cir::MissingFeatures>::value,
      "longDoubleReferenced should stay removed — redundant bookkeeping; "
      "PPC concerns tracked by ppcDoubleDoubleLongDouble and "
      "targetCodeGenInfoEmitTargetGlobals");
  EXPECT_FALSE(hasLongDoubleReferenced<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasUnrecoverableErrorCheckLevel : std::false_type {};
template <typename T>
struct hasUnrecoverableErrorCheckLevel<
    T, std::void_t<decltype(&T::unrecoverableErrorCheckLevel)>>
    : std::true_type {};

TEST(MissingFeaturesTest, UnrecoverableErrorCheckLevelEntryRemoved) {
  static_assert(
      !hasUnrecoverableErrorCheckLevel<cir::MissingFeatures>::value,
      "unrecoverableErrorCheckLevel should stay removed — CIR intentionally "
      "uses stricter hasErrorOccurred() instead of OGCG's "
      "hasUnrecoverableErrorOccurred()");
  EXPECT_FALSE(hasUnrecoverableErrorCheckLevel<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasZosPersonality : std::false_type {};
template <typename T>
struct hasZosPersonality<T, std::void_t<decltype(&T::zosPersonality)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasTerminateHandlerABIDispatch : std::false_type {};
template <typename T>
struct hasTerminateHandlerABIDispatch<
    T, std::void_t<decltype(&T::terminateHandlerABIDispatch)>>
    : std::true_type {};

TEST(MissingFeaturesTest, TerminateHandlerABIDispatchEntryRemoved) {
  static_assert(
      !hasTerminateHandlerABIDispatch<cir::MissingFeatures>::value,
      "terminateHandlerABIDispatch should stay removed — terminate handler "
      "ABI dispatch now happens directly in emitTerminateHandlerBody");
  EXPECT_FALSE(hasTerminateHandlerABIDispatch<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, ZosPersonalityEntryRemoved) {
  static_assert(!hasZosPersonality<cir::MissingFeatures>::value,
                "zosPersonality should stay removed — z/OS is not a "
                "supported CIR target");
  EXPECT_FALSE(hasZosPersonality<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasBpfPreserveStaticOffset : std::false_type {};
template <typename T>
struct hasBpfPreserveStaticOffset<
    T, std::void_t<decltype(&T::bpfPreserveStaticOffset)>> : std::true_type {};

template <typename T, typename = void>
struct hasTargetCodeGenInfoEmitTargetGlobals : std::false_type {};
template <typename T>
struct hasTargetCodeGenInfoEmitTargetGlobals<
    T, std::void_t<decltype(&T::targetCodeGenInfoEmitTargetGlobals)>>
    : std::true_type {};

TEST(MissingFeaturesTest, BpfPreserveStaticOffsetEntryRemoved) {
  static_assert(!hasBpfPreserveStaticOffset<cir::MissingFeatures>::value,
                "bpfPreserveStaticOffset should stay removed — BPF is not a "
                "supported CIR target");
  EXPECT_FALSE(hasBpfPreserveStaticOffset<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, TargetCodeGenInfoEmitTargetGlobalsEntryRemoved) {
  static_assert(
      !hasTargetCodeGenInfoEmitTargetGlobals<cir::MissingFeatures>::value,
      "targetCodeGenInfoEmitTargetGlobals should stay removed — release "
      "finalization no longer uses a dedicated MissingFeatures gate");
  EXPECT_FALSE(
      hasTargetCodeGenInfoEmitTargetGlobals<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasSimplifyPersonality : std::false_type {};
template <typename T>
struct hasSimplifyPersonality<T,
                              std::void_t<decltype(&T::simplifyPersonality)>>
    : std::true_type {};

TEST(MissingFeaturesTest, SimplifyPersonalityEntryRemoved) {
  static_assert(
      !hasSimplifyPersonality<cir::MissingFeatures>::value,
      "simplifyPersonality should stay removed — CIR per-function "
      "personality assignment avoids the ObjC++ personality issue");
  EXPECT_FALSE(hasSimplifyPersonality<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasPs4DLLImportExportVTable : std::false_type {};
template <typename T>
struct hasPs4DLLImportExportVTable<
    T, std::void_t<decltype(&T::ps4DLLImportExportVTable)>> : std::true_type {};

TEST(MissingFeaturesTest, Ps4DLLImportExportVTableEntryRemoved) {
  static_assert(!hasPs4DLLImportExportVTable<cir::MissingFeatures>::value,
                "ps4DLLImportExportVTable should stay removed — PS4/PS5 is "
                "not a supported CIR target");
  EXPECT_FALSE(hasPs4DLLImportExportVTable<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasCygMinGNUStepPersonality : std::false_type {};
template <typename T>
struct hasCygMinGNUStepPersonality<
    T, std::void_t<decltype(&T::cygMinGNUStepPersonality)>> : std::true_type {};

TEST(MissingFeaturesTest, CygMinGNUStepPersonalityEntryRemoved) {
  static_assert(!hasCygMinGNUStepPersonality<cir::MissingFeatures>::value,
                "cygMinGNUStepPersonality should stay removed — "
                "Cygwin/MinGW is not a supported CIR target");
  EXPECT_FALSE(hasCygMinGNUStepPersonality<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasUse32BitVTableOffsetABI : std::false_type {};
template <typename T>
struct hasUse32BitVTableOffsetABI<
    T, std::void_t<decltype(&T::use32BitVTableOffsetABI)>> : std::true_type {};

TEST(MissingFeaturesTest, Use32BitVTableOffsetABIEntryRemoved) {
  static_assert(!hasUse32BitVTableOffsetABI<cir::MissingFeatures>::value,
                "use32BitVTableOffsetABI should stay removed — Apple ARM64 "
                "vtable offset truncation is not needed for CIR targets");
  EXPECT_FALSE(hasUse32BitVTableOffsetABI<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasPreservedAccessIndexRegion : std::false_type {};
template <typename T>
struct hasPreservedAccessIndexRegion<
    T, std::void_t<decltype(&T::preservedAccessIndexRegion)>>
    : std::true_type {};

TEST(MissingFeaturesTest, PreservedAccessIndexRegionEntryRemoved) {
  static_assert(!hasPreservedAccessIndexRegion<cir::MissingFeatures>::value,
                "preservedAccessIndexRegion should stay removed — BPF "
                "preserve.access.index is not needed for CIR targets");
  EXPECT_FALSE(hasPreservedAccessIndexRegion<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasAppleArm64CXXABI : std::false_type {};
template <typename T>
struct hasAppleArm64CXXABI<T, std::void_t<decltype(&T::appleArm64CXXABI)>>
    : std::true_type {};

TEST(MissingFeaturesTest, AppleArm64CXXABIEntryRemoved) {
  static_assert(!hasAppleArm64CXXABI<cir::MissingFeatures>::value,
                "appleArm64CXXABI should stay removed — Apple ARM64 "
                "is not a supported CIR target");
  EXPECT_FALSE(hasAppleArm64CXXABI<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasAppleKext : std::false_type {};
template <typename T>
struct hasAppleKext<T, std::void_t<decltype(&T::appleKext)>>
    : std::true_type {};

TEST(MissingFeaturesTest, AppleKextEntryRemoved) {
  static_assert(!hasAppleKext<cir::MissingFeatures>::value,
                "appleKext should stay removed — Apple kernel extensions "
                "are not relevant for CIR targets");
  EXPECT_FALSE(hasAppleKext<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasShouldReverseUnaryCondOnBoolExpr : std::false_type {};
template <typename T>
struct hasShouldReverseUnaryCondOnBoolExpr<
    T, std::void_t<decltype(&T::shouldReverseUnaryCondOnBoolExpr)>>
    : std::true_type {};

TEST(MissingFeaturesTest, ShouldReverseUnaryCondOnBoolExprEntryRemoved) {
  static_assert(
      !hasShouldReverseUnaryCondOnBoolExpr<cir::MissingFeatures>::value,
      "shouldReverseUnaryCondOnBoolExpr should stay removed — CIR "
      "defers this optimization to LLVM lowering for diagnostic accuracy");
  EXPECT_FALSE(
      hasShouldReverseUnaryCondOnBoolExpr<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasWindowsHotPatching : std::false_type {};
template <typename T>
struct hasWindowsHotPatching<T, std::void_t<decltype(&T::windowsHotPatching)>>
    : std::true_type {};

TEST(MissingFeaturesTest, WindowsHotPatchingEntryRemoved) {
  static_assert(!hasWindowsHotPatching<cir::MissingFeatures>::value,
                "windowsHotPatching should stay removed — Windows "
                "is not a supported CIR target");
  EXPECT_FALSE(hasWindowsHotPatching<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasCurrentFuncletPad : std::false_type {};
template <typename T>
struct hasCurrentFuncletPad<T, std::void_t<decltype(&T::currentFuncletPad)>>
    : std::true_type {};

TEST(MissingFeaturesTest, CurrentFuncletPadEntryRemoved) {
  static_assert(!hasCurrentFuncletPad<cir::MissingFeatures>::value,
                "currentFuncletPad should stay removed — Windows SEH "
                "funclet EH is not used by CIR targets");
  EXPECT_FALSE(hasCurrentFuncletPad<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasMsvcCXXPersonality : std::false_type {};
template <typename T>
struct hasMsvcCXXPersonality<T, std::void_t<decltype(&T::msvcCXXPersonality)>>
    : std::true_type {};

TEST(MissingFeaturesTest, MsvcCXXPersonalityEntryRemoved) {
  static_assert(!hasMsvcCXXPersonality<cir::MissingFeatures>::value,
                "msvcCXXPersonality should stay removed — Windows/MSVC "
                "is not a supported CIR target");
  EXPECT_FALSE(hasMsvcCXXPersonality<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasCmseNSEntryAttr : std::false_type {};
template <typename T>
struct hasCmseNSEntryAttr<T, std::void_t<decltype(&T::cmseNSEntryAttr)>>
    : std::true_type {};

TEST(MissingFeaturesTest, CmseNSEntryAttrEntryRemoved) {
  static_assert(!hasCmseNSEntryAttr<cir::MissingFeatures>::value,
                "cmseNSEntryAttr should stay removed — CMSE is for ARM "
                "Cortex-M (32-bit), not AArch64 targets");
  EXPECT_FALSE(hasCmseNSEntryAttr<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasArmComputeVolatileBitfields : std::false_type {};
template <typename T>
struct hasArmComputeVolatileBitfields<
    T, std::void_t<decltype(&T::armComputeVolatileBitfields)>>
    : std::true_type {};

TEST(MissingFeaturesTest, ArmComputeVolatileBitfieldsEntryRemoved) {
  static_assert(
      !hasArmComputeVolatileBitfields<cir::MissingFeatures>::value,
      "armComputeVolatileBitfields should stay removed — ARM32 AAPCS "
      "volatile bitfield behavior is not relevant for AArch64 targets");
  EXPECT_FALSE(
      hasArmComputeVolatileBitfields<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasAsmLabelAttr : std::false_type {};
template <typename T>
struct hasAsmLabelAttr<T, std::void_t<decltype(&T::asmLabelAttr)>>
    : std::true_type {};

TEST(MissingFeaturesTest, AsmLabelAttrEntryRemoved) {
  static_assert(!hasAsmLabelAttr<cir::MissingFeatures>::value,
                "asmLabelAttr should stay removed — now implemented: builtins "
                "with __asm__ labels use mangled name");
  EXPECT_FALSE(hasAsmLabelAttr<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasAsmNoMergeAttr : std::false_type {};
template <typename T>
struct hasAsmNoMergeAttr<T, std::void_t<decltype(&T::asmNoMergeAttr)>>
    : std::true_type {};

TEST(MissingFeaturesTest, AsmNoMergeAttrEntryRemoved) {
  static_assert(!hasAsmNoMergeAttr<cir::MissingFeatures>::value,
                "asmNoMergeAttr should stay removed — now implemented: "
                "[[clang::nomerge]] forwarded to InlineAsmOp/InlineAsmGotoOp");
  EXPECT_FALSE(hasAsmNoMergeAttr<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasAsmConvergentAttr : std::false_type {};
template <typename T>
struct hasAsmConvergentAttr<T, std::void_t<decltype(&T::asmConvergentAttr)>>
    : std::true_type {};

TEST(MissingFeaturesTest, AsmConvergentAttrEntryRemoved) {
  static_assert(!hasAsmConvergentAttr<cir::MissingFeatures>::value,
                "asmConvergentAttr should stay removed — now implemented: "
                "convergent attr set on asm in CUDA/OpenCL modes");
  EXPECT_FALSE(hasAsmConvergentAttr<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasCleanupBranchThrough : std::false_type {};
template <typename T>
struct hasCleanupBranchThrough<
    T, std::void_t<decltype(&T::cleanupBranchThrough)>> : std::true_type {};

TEST(MissingFeaturesTest, CleanupBranchThroughEntryRemoved) {
  static_assert(!hasCleanupBranchThrough<cir::MissingFeatures>::value,
                "cleanupBranchThrough should stay removed — CIR uses "
                "structured control flow via LexicalScope::cleanup()");
  EXPECT_FALSE(hasCleanupBranchThrough<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasEhTerminateScopeSkipsCleanup : std::false_type {};
template <typename T>
struct hasEhTerminateScopeSkipsCleanup<
    T, std::void_t<decltype(&T::ehTerminateScopeSkipsCleanup)>>
    : std::true_type {};

TEST(MissingFeaturesTest, EhTerminateScopeSkipsCleanupEntryRemoved) {
  static_assert(
      !hasEhTerminateScopeSkipsCleanup<cir::MissingFeatures>::value,
      "ehTerminateScopeSkipsCleanup should stay removed — CIR uses "
      "conservative behavior of always running cleanups");
  EXPECT_FALSE(
      hasEhTerminateScopeSkipsCleanup<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpFuncArmNewAttr : std::false_type {};
template <typename T>
struct hasOpFuncArmNewAttr<T, std::void_t<decltype(&T::opFuncArmNewAttr)>>
    : std::true_type {};

TEST(MissingFeaturesTest, OpFuncArmNewAttrEntryRemoved) {
  static_assert(!hasOpFuncArmNewAttr<cir::MissingFeatures>::value,
                "opFuncArmNewAttr should stay removed — now implemented: "
                "ArmNewAttr forwarded in setNonAliasAttributes");
  EXPECT_FALSE(hasOpFuncArmNewAttr<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpFuncArmStreamingAttr : std::false_type {};
template <typename T>
struct hasOpFuncArmStreamingAttr<
    T, std::void_t<decltype(&T::opFuncArmStreamingAttr)>> : std::true_type {};

template <typename T, typename = void>
struct hasOpFuncMultipleReturnVals : std::false_type {};
template <typename T>
struct hasOpFuncMultipleReturnVals<
    T, std::void_t<decltype(&T::opFuncMultipleReturnVals)>>
    : std::true_type {};

TEST(MissingFeaturesTest, OpFuncArmStreamingAttrEntryRemoved) {
  static_assert(!hasOpFuncArmStreamingAttr<cir::MissingFeatures>::value,
                "opFuncArmStreamingAttr should stay removed — now implemented: "
                "ArmLocallyStreamingAttr forwarded in setNonAliasAttributes");
  EXPECT_FALSE(hasOpFuncArmStreamingAttr<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, OpFuncMultipleReturnValsEntryRemoved) {
  static_assert(
      !hasOpFuncMultipleReturnVals<cir::MissingFeatures>::value,
      "opFuncMultipleReturnVals should stay removed — CIR lowering creates "
      "single-return LLVM function signatures");
  EXPECT_FALSE(hasOpFuncMultipleReturnVals<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpFuncExceptions : std::false_type {};
template <typename T>
struct hasOpFuncExceptions<T, std::void_t<decltype(&T::opFuncExceptions)>>
    : std::true_type {};

TEST(MissingFeaturesTest, OpFuncExceptionsEntryRemoved) {
  static_assert(!hasOpFuncExceptions<cir::MissingFeatures>::value,
                "opFuncExceptions should stay removed — no-proto call "
                "reconciliation now preserves exception/cleanup semantics");
  EXPECT_FALSE(hasOpFuncExceptions<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpFuncOperandBundles : std::false_type {};
template <typename T>
struct hasOpFuncOperandBundles<
    T, std::void_t<decltype(&T::opFuncOperandBundles)>> : std::true_type {};

TEST(MissingFeaturesTest, OpFuncOperandBundlesEntryRemoved) {
  static_assert(!hasOpFuncOperandBundles<cir::MissingFeatures>::value,
                "opFuncOperandBundles should stay removed — operand bundles "
                "are an LLVM IR concept, not used in CIR's MLIR model");
  EXPECT_FALSE(hasOpFuncOperandBundles<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpFuncParameterAttributes : std::false_type {};
template <typename T>
struct hasOpFuncParameterAttributes<
    T, std::void_t<decltype(&T::opFuncParameterAttributes)>> : std::true_type {
};

TEST(MissingFeaturesTest, OpFuncParameterAttributesEntryRemoved) {
  static_assert(
      !hasOpFuncParameterAttributes<cir::MissingFeatures>::value,
      "opFuncParameterAttributes should stay removed — CIR no-proto call "
      "rewriting has no parameter-attribute lists to forward");
  EXPECT_FALSE(hasOpFuncParameterAttributes<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpGlobalDLLImportExport : std::false_type {};
template <typename T>
struct hasOpGlobalDLLImportExport<
    T, std::void_t<decltype(&T::opGlobalDLLImportExport)>> : std::true_type {};
TEST(MissingFeaturesTest, OpGlobalDLLImportExportEntryRemoved) {
  static_assert(!hasOpGlobalDLLImportExport<cir::MissingFeatures>::value,
                "opGlobalDLLImportExport should stay removed — DLL "
                "import/export is Windows-only, not a CIR target");
  EXPECT_FALSE(hasOpGlobalDLLImportExport<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpGlobalPartition : std::false_type {};
template <typename T>
struct hasOpGlobalPartition<T, std::void_t<decltype(&T::opGlobalPartition)>>
    : std::true_type {};

TEST(MissingFeaturesTest, OpGlobalPartitionEntryRemoved) {
  static_assert(!hasOpGlobalPartition<cir::MissingFeatures>::value,
                "opGlobalPartition should stay removed — CIR already applies "
                "-fsymbol-partition via module attr during CIR->LLVM lowering");
  EXPECT_FALSE(hasOpGlobalPartition<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOpGlobalWeakRef : std::false_type {};
template <typename T>
struct hasOpGlobalWeakRef<
    T, std::void_t<decltype(&T::opGlobalWeakRef)>> : std::true_type {};

template <typename T, typename = void>
struct hasOpLoadEmitScalarRangeCheck : std::false_type {};
template <typename T>
struct hasOpLoadEmitScalarRangeCheck<
    T, std::void_t<decltype(&T::opLoadEmitScalarRangeCheck)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasOpLoadStoreAtomic : std::false_type {};
template <typename T>
struct hasOpLoadStoreAtomic<
    T, std::void_t<decltype(&T::opLoadStoreAtomic)>> : std::true_type {};

template <typename T, typename = void>
struct hasOpLoadStoreObjC : std::false_type {};
template <typename T>
struct hasOpLoadStoreObjC<
    T, std::void_t<decltype(&T::opLoadStoreObjC)>> : std::true_type {};

TEST(MissingFeaturesTest, OpGlobalWeakRefEntryRemoved) {
  static_assert(!hasOpGlobalWeakRef<cir::MissingFeatures>::value,
                "opGlobalWeakRef should stay removed — weakrefs are handled "
                "directly by weakref alias emission paths");
  EXPECT_FALSE(hasOpGlobalWeakRef<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, OpLoadEmitScalarRangeCheckEntryRemoved) {
  static_assert(!hasOpLoadEmitScalarRangeCheck<cir::MissingFeatures>::value,
                "opLoadEmitScalarRangeCheck should stay removed — CIR load "
                "paths currently do not model scalar range checks");
  EXPECT_FALSE(hasOpLoadEmitScalarRangeCheck<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, OpLoadStoreAtomicEntryRemoved) {
  static_assert(!hasOpLoadStoreAtomic<cir::MissingFeatures>::value,
                "opLoadStoreAtomic should stay removed — aggregate atomic "
                "loads are handled by standard aggregate copy paths");
  EXPECT_FALSE(hasOpLoadStoreAtomic<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, OpLoadStoreObjCEntryRemoved) {
  static_assert(!hasOpLoadStoreObjC<cir::MissingFeatures>::value,
                "opLoadStoreObjC should stay removed — ObjC/ARC barriers are "
                "handled by dedicated ObjC paths, not generic load/store");
  EXPECT_FALSE(hasOpLoadStoreObjC<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOptInfoAttr : std::false_type {};
template <typename T>
struct hasOptInfoAttr<T, std::void_t<decltype(&T::optInfoAttr)>>
    : std::true_type {};

TEST(MissingFeaturesTest, OptInfoAttrEntryRemoved) {
  static_assert(!hasOptInfoAttr<cir::MissingFeatures>::value,
                "optInfoAttr should stay removed — cir.opt_info is already a "
                "first-class module attribute consumed by lowering passes");
  EXPECT_FALSE(hasOptInfoAttr<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasPgoInstrumentation : std::false_type {};
template <typename T>
struct hasPgoInstrumentation<
    T, std::void_t<decltype(&T::pgoInstrumentation)>> : std::true_type {};

TEST(MissingFeaturesTest, PgoInstrumentationEntryRemoved) {
  static_assert(!hasPgoInstrumentation<cir::MissingFeatures>::value,
                "pgoInstrumentation should stay removed — dedicated PGO "
                "gaps are tracked by more specific guards");
  EXPECT_FALSE(hasPgoInstrumentation<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasPointerAuthentication : std::false_type {};
template <typename T>
struct hasPointerAuthentication<
    T, std::void_t<decltype(&T::pointerAuthentication)>> : std::true_type {};

TEST(MissingFeaturesTest, PointerAuthenticationEntryRemoved) {
  static_assert(!hasPointerAuthentication<cir::MissingFeatures>::value,
                "pointerAuthentication should stay removed — pointer-auth "
                "gaps are tracked by explicit per-path NYI handling");
  EXPECT_FALSE(hasPointerAuthentication<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasPtrSubExactDiv : std::false_type {};
template <typename T>
struct hasPtrSubExactDiv<T, std::void_t<decltype(&T::ptrSubExactDiv)>>
    : std::true_type {};

TEST(MissingFeaturesTest, PtrSubExactDivEntryRemoved) {
  static_assert(!hasPtrSubExactDiv<cir::MissingFeatures>::value,
                "ptrSubExactDiv should stay removed — exactness is an "
                "LLVM IR division flag not modeled in CIR binop semantics");
  EXPECT_FALSE(hasPtrSubExactDiv<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasReadArrayCookieImpl : std::false_type {};
template <typename T>
struct hasReadArrayCookieImpl<
    T, std::void_t<decltype(&T::readArrayCookieImpl)>> : std::true_type {};

TEST(MissingFeaturesTest, ReadArrayCookieImplEntryRemoved) {
  static_assert(!hasReadArrayCookieImpl<cir::MissingFeatures>::value,
                "readArrayCookieImpl should stay removed — delete[] cookie "
                "reading is inlined in CIRGenExprCXX");
  EXPECT_FALSE(hasReadArrayCookieImpl<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasRecordDeclIsCXXDecl : std::false_type {};
template <typename T>
struct hasRecordDeclIsCXXDecl<
    T, std::void_t<decltype(&T::recordDeclIsCXXDecl)>> : std::true_type {};

TEST(MissingFeaturesTest, RecordDeclIsCXXDeclEntryRemoved) {
  static_assert(!hasRecordDeclIsCXXDecl<cir::MissingFeatures>::value,
                "recordDeclIsCXXDecl should stay removed — CIR RecordType "
                "already carries required AST declaration-kind information");
  EXPECT_FALSE(hasRecordDeclIsCXXDecl<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasRecordDeclIsMSStruct : std::false_type {};
template <typename T>
struct hasRecordDeclIsMSStruct<
    T, std::void_t<decltype(&T::recordDeclIsMSStruct)>> : std::true_type {};

TEST(MissingFeaturesTest, RecordDeclIsMSStructEntryRemoved) {
  static_assert(!hasRecordDeclIsMSStruct<cir::MissingFeatures>::value,
                "recordDeclIsMSStruct should stay removed — MS struct "
                "layout behavior is Windows/MSVC specific");
  EXPECT_FALSE(hasRecordDeclIsMSStruct<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasRecordLayoutVerification : std::false_type {};
template <typename T>
struct hasRecordLayoutVerification<
    T, std::void_t<decltype(&T::recordLayoutVerification)>>
    : std::true_type {};

TEST(MissingFeaturesTest, RecordLayoutVerificationEntryRemoved) {
  static_assert(!hasRecordLayoutVerification<cir::MissingFeatures>::value,
                "recordLayoutVerification should stay removed — CIR "
                "intentionally omits OGCG post-lowering layout checks");
  EXPECT_FALSE(hasRecordLayoutVerification<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasReturnValueValidCheck : std::false_type {};
template <typename T>
struct hasReturnValueValidCheck<
    T, std::void_t<decltype(&T::returnValueValidCheck)>> : std::true_type {};

TEST(MissingFeaturesTest, ReturnValueValidCheckEntryRemoved) {
  static_assert(!hasReturnValueValidCheck<cir::MissingFeatures>::value,
                "returnValueValidCheck should stay removed — CIR handles "
                "return validity with AST-level checks and skips OGCG hook");
  EXPECT_FALSE(hasReturnValueValidCheck<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasRunCleanupsScope : std::false_type {};
template <typename T>
struct hasRunCleanupsScope<T, std::void_t<decltype(&T::runCleanupsScope)>>
    : std::true_type {};

TEST(MissingFeaturesTest, RunCleanupsScopeEntryRemoved) {
  static_assert(!hasRunCleanupsScope<cir::MissingFeatures>::value,
                "runCleanupsScope should stay removed — CIR uses "
                "RunCleanupsScope directly at call-sites");
  EXPECT_FALSE(hasRunCleanupsScope<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasSanitizerFunctionAttrs : std::false_type {};
template <typename T>
struct hasSanitizerFunctionAttrs<
    T, std::void_t<decltype(&T::sanitizerFunctionAttrs)>> : std::true_type {};

template <typename T, typename = void>
struct hasSanitizers : std::false_type {};
template <typename T>
struct hasSanitizers<T, std::void_t<decltype(&T::sanitizers)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasSretArgs : std::false_type {};
template <typename T>
struct hasSretArgs<T, std::void_t<decltype(&T::sretArgs)>> : std::true_type {
};

template <typename T, typename = void>
struct hasStringTypeWithDifferentArraySize : std::false_type {};
template <typename T>
struct hasStringTypeWithDifferentArraySize<
    T, std::void_t<decltype(&T::stringTypeWithDifferentArraySize)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasUnsizedTypes : std::false_type {};
template <typename T>
struct hasUnsizedTypes<T, std::void_t<decltype(&T::unsizedTypes)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasSupportisHomogeneousAggregateQueryForAArch64 : std::false_type {};
template <typename T>
struct hasSupportisHomogeneousAggregateQueryForAArch64<
    T, std::void_t<decltype(&T::supportisHomogeneousAggregateQueryForAArch64)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasTagTypeClassAbstraction : std::false_type {};
template <typename T>
struct hasTagTypeClassAbstraction<
    T, std::void_t<decltype(&T::tagTypeClassAbstraction)>> : std::true_type {};

template <typename T, typename = void>
struct hasTypeGetAsEnumType : std::false_type {};
template <typename T>
struct hasTypeGetAsEnumType<T, std::void_t<decltype(&T::typeGetAsEnumType)>>
    : std::true_type {};

TEST(MissingFeaturesTest, SanitizerFunctionAttrsEntryRemoved) {
  static_assert(!hasSanitizerFunctionAttrs<cir::MissingFeatures>::value,
                "sanitizerFunctionAttrs should stay removed — CIR no longer "
                "uses a dedicated startFunction MissingFeatures gate");
  EXPECT_FALSE(hasSanitizerFunctionAttrs<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, SanitizersEntryRemoved) {
  static_assert(!hasSanitizers<cir::MissingFeatures>::value,
                "sanitizers should stay removed — sanitizer parity is tracked "
                "at explicit per-path NYI call-sites");
  EXPECT_FALSE(hasSanitizers<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, SretArgsEntryRemoved) {
  static_assert(!hasSretArgs<cir::MissingFeatures>::value,
                "sretArgs should stay removed — sret ABI behavior is tracked "
                "at explicit CIRGen/lowering call-sites");
  EXPECT_FALSE(hasSretArgs<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, StringTypeWithDifferentArraySizeEntryRemoved) {
  static_assert(
      !hasStringTypeWithDifferentArraySize<cir::MissingFeatures>::value,
      "stringTypeWithDifferentArraySize should stay removed — mismatched "
      "ConstArrayAttr string length is tracked by explicit lowering assert");
  EXPECT_FALSE(
      hasStringTypeWithDifferentArraySize<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, UnsizedTypesEntryRemoved) {
  static_assert(!hasUnsizedTypes<cir::MissingFeatures>::value,
                "unsizedTypes should stay removed — cir::isSized handles "
                "non-SizedTypeInterface cases directly without a global gate");
  EXPECT_FALSE(hasUnsizedTypes<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest,
     SupportisHomogeneousAggregateQueryForAArch64EntryRemoved) {
  static_assert(
      !hasSupportisHomogeneousAggregateQueryForAArch64<
          cir::MissingFeatures>::value,
      "supportisHomogeneousAggregateQueryForAArch64 should stay removed — "
      "AArch64 va_arg currently uses explicit local non-HFA handling");
  EXPECT_FALSE(hasSupportisHomogeneousAggregateQueryForAArch64<
               cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, TagTypeClassAbstractionEntryRemoved) {
  static_assert(!hasTagTypeClassAbstraction<cir::MissingFeatures>::value,
                "tagTypeClassAbstraction should stay removed — record type "
                "abstraction is handled directly in CIRLowerContext");
  EXPECT_FALSE(hasTagTypeClassAbstraction<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, TypeGetAsEnumTypeEntryRemoved) {
  static_assert(!hasTypeGetAsEnumType<cir::MissingFeatures>::value,
                "typeGetAsEnumType should stay removed — CIR type lowering "
                "uses explicit ABI promotable attributes instead of enum-type "
                "class queries");
  EXPECT_FALSE(hasTypeGetAsEnumType<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasSetTargetAttributes : std::false_type {};
template <typename T>
struct hasSetTargetAttributes<
    T, std::void_t<decltype(&T::setTargetAttributes)>> : std::true_type {};

template <typename T, typename = void>
struct hasTargetCIRGenInfoArch : std::false_type {};
template <typename T>
struct hasTargetCIRGenInfoArch<
    T, std::void_t<decltype(&T::targetCIRGenInfoArch)>> : std::true_type {};

template <typename T, typename = void>
struct hasTargetCodeGenInfoGetNullPointer : std::false_type {};
template <typename T>
struct hasTargetCodeGenInfoGetNullPointer<
    T, std::void_t<decltype(&T::targetCodeGenInfoGetNullPointer)>>
    : std::true_type {};

TEST(MissingFeaturesTest, SetTargetAttributesEntryRemoved) {
  static_assert(!hasSetTargetAttributes<cir::MissingFeatures>::value,
                "setTargetAttributes should stay removed — static-local and "
                "global emission paths both directly call TargetCIRGenInfo");
  EXPECT_FALSE(hasSetTargetAttributes<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, TargetCIRGenInfoArchEntryRemoved) {
  static_assert(!hasTargetCIRGenInfoArch<cir::MissingFeatures>::value,
                "targetCIRGenInfoArch should stay removed — unsupported-arch "
                "fallback in getTargetCIRGenInfo no longer uses a stale gate");
  EXPECT_FALSE(hasTargetCIRGenInfoArch<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, TargetCodeGenInfoGetNullPointerEntryRemoved) {
  static_assert(
      !hasTargetCodeGenInfoGetNullPointer<cir::MissingFeatures>::value,
      "targetCodeGenInfoGetNullPointer should stay removed — CIR preserves "
      "target-specific null-pointer semantics in DirectToLLVM lowering");
  EXPECT_FALSE(
      hasTargetCodeGenInfoGetNullPointer<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasSourceLanguageCases : std::false_type {};
template <typename T>
struct hasSourceLanguageCases<
    T, std::void_t<decltype(&T::sourceLanguageCases)>> : std::true_type {};

TEST(MissingFeaturesTest, SourceLanguageCasesEntryRemoved) {
  static_assert(!hasSourceLanguageCases<cir::MissingFeatures>::value,
                "sourceLanguageCases should stay removed — unsupported "
                "languages now produce a direct NYI diagnostic");
  EXPECT_FALSE(hasSourceLanguageCases<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasRecordDeclIsPacked : std::false_type {};
template <typename T>
struct hasRecordDeclIsPacked<
    T, std::void_t<decltype(&T::recordDeclIsPacked)>> : std::true_type {};

TEST(MissingFeaturesTest, RecordDeclIsPackedEntryRemoved) {
  static_assert(!hasRecordDeclIsPacked<cir::MissingFeatures>::value,
                "recordDeclIsPacked should stay removed — packedness is read "
                "from concrete RecordType state in record-layout lowering");
  EXPECT_FALSE(hasRecordDeclIsPacked<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasRecordDeclMayInsertExtraPadding : std::false_type {};
template <typename T>
struct hasRecordDeclMayInsertExtraPadding<
    T, std::void_t<decltype(&T::recordDeclMayInsertExtraPadding)>>
    : std::true_type {};

TEST(MissingFeaturesTest, RecordDeclMayInsertExtraPaddingEntryRemoved) {
  static_assert(
      !hasRecordDeclMayInsertExtraPadding<cir::MissingFeatures>::value,
      "recordDeclMayInsertExtraPadding should stay removed — extra field "
      "padding is tracked by explicit NYI paths in record layout lowering");
  EXPECT_FALSE(
      hasRecordDeclMayInsertExtraPadding<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasRecordLayoutIsEmptyRecordForLayout : std::false_type {};
template <typename T>
struct hasRecordLayoutIsEmptyRecordForLayout<
    T, std::void_t<decltype(&T::recordLayoutIsEmptyRecordForLayout)>>
    : std::true_type {};

TEST(MissingFeaturesTest, RecordLayoutIsEmptyRecordForLayoutEntryRemoved) {
  static_assert(
      !hasRecordLayoutIsEmptyRecordForLayout<cir::MissingFeatures>::value,
      "recordLayoutIsEmptyRecordForLayout should stay removed — record "
      "layout directly queries isEmptyRecordForLayout");
  EXPECT_FALSE(
      hasRecordLayoutIsEmptyRecordForLayout<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasReturnValueDominatingStoreOptmiization : std::false_type {};
template <typename T>
struct hasReturnValueDominatingStoreOptmiization<
    T, std::void_t<decltype(&T::returnValueDominatingStoreOptmiization)>>
    : std::true_type {};

TEST(MissingFeaturesTest,
     ReturnValueDominatingStoreOptmiizationEntryRemoved) {
  static_assert(
      !hasReturnValueDominatingStoreOptmiization<cir::MissingFeatures>::value,
      "returnValueDominatingStoreOptmiization should stay removed — CIR "
      "intentionally uses the simple non-optimized return-value epilog path");
  EXPECT_FALSE(
      hasReturnValueDominatingStoreOptmiization<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasTagDeclRequiredDefDeferredGuard : std::false_type {};
template <typename T>
struct hasTagDeclRequiredDefDeferredGuard<
    T, std::void_t<decltype(&T::tagDeclRequiredDefDeferredGuard)>>
    : std::true_type {};

TEST(MissingFeaturesTest, TagDeclRequiredDefDeferredGuardEntryRemoved) {
  static_assert(
      !hasTagDeclRequiredDefDeferredGuard<cir::MissingFeatures>::value,
      "tagDeclRequiredDefDeferredGuard should stay removed — CIR "
      "defers debug info to lowering, so no re-entrancy guard needed");
  EXPECT_FALSE(
      hasTagDeclRequiredDefDeferredGuard<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasAtomicMicrosoftVolatile : std::false_type {};
template <typename T>
struct hasAtomicMicrosoftVolatile<
    T, std::void_t<decltype(&T::atomicMicrosoftVolatile)>> : std::true_type {};

TEST(MissingFeaturesTest, AtomicMicrosoftVolatileEntryRemoved) {
  static_assert(!hasAtomicMicrosoftVolatile<cir::MissingFeatures>::value,
                "atomicMicrosoftVolatile should stay removed — MSVC volatile "
                "semantics are Windows-only");
  EXPECT_FALSE(hasAtomicMicrosoftVolatile<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasMsabi : std::false_type {};
template <typename T>
struct hasMsabi<T, std::void_t<decltype(&T::msabi)>> : std::true_type {};

TEST(MissingFeaturesTest, MsabiEntryRemoved) {
  static_assert(!hasMsabi<cir::MissingFeatures>::value,
                "msabi should stay removed — Microsoft C++ ABI is "
                "Windows-only; CIR uses Itanium ABI");
  EXPECT_FALSE(hasMsabi<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasIsOutlinedSEHHelper : std::false_type {};
template <typename T>
struct hasIsOutlinedSEHHelper<T,
                              std::void_t<decltype(&T::isOutlinedSEHHelper)>>
    : std::true_type {};

TEST(MissingFeaturesTest, IsOutlinedSEHHelperEntryRemoved) {
  static_assert(!hasIsOutlinedSEHHelper<cir::MissingFeatures>::value,
                "isOutlinedSEHHelper should stay removed — Windows SEH "
                "is not a supported CIR target");
  EXPECT_FALSE(hasIsOutlinedSEHHelper<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasCmseCall : std::false_type {};
template <typename T>
struct hasCmseCall<T, std::void_t<decltype(&T::cmseCall)>> : std::true_type {};

TEST(MissingFeaturesTest, CmseCallEntryRemoved) {
  static_assert(!hasCmseCall<cir::MissingFeatures>::value,
                "cmseCall should stay removed — CMSE is ARM Cortex-M "
                "only, not AArch64");
  EXPECT_FALSE(hasCmseCall<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasCmseNonSecureCallAttr : std::false_type {};
template <typename T>
struct hasCmseNonSecureCallAttr<
    T, std::void_t<decltype(&T::cmseNonSecureCallAttr)>> : std::true_type {};

TEST(MissingFeaturesTest, CmseNonSecureCallAttrEntryRemoved) {
  static_assert(!hasCmseNonSecureCallAttr<cir::MissingFeatures>::value,
                "cmseNonSecureCallAttr should stay removed — CMSE is ARM "
                "Cortex-M only, not AArch64");
  EXPECT_FALSE(hasCmseNonSecureCallAttr<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasDeclHasAlignMac68kAttr : std::false_type {};
template <typename T>
struct hasDeclHasAlignMac68kAttr<
    T, std::void_t<decltype(&T::declHasAlignMac68kAttr)>> : std::true_type {};

TEST(MissingFeaturesTest, DeclHasAlignMac68kAttrEntryRemoved) {
  static_assert(!hasDeclHasAlignMac68kAttr<cir::MissingFeatures>::value,
                "declHasAlignMac68kAttr should stay removed — Mac68k "
                "alignment is an old Apple feature, not relevant for CIR");
  EXPECT_FALSE(hasDeclHasAlignMac68kAttr<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasCodeSegAttr : std::false_type {};
template <typename T>
struct hasCodeSegAttr<T, std::void_t<decltype(&T::codeSegAttr)>>
    : std::true_type {};

TEST(MissingFeaturesTest, CodeSegAttrEntryRemoved) {
  static_assert(!hasCodeSegAttr<cir::MissingFeatures>::value,
                "codeSegAttr should stay removed — Windows PE/COFF section "
                "control; Windows is not a CIR target");
  EXPECT_FALSE(hasCodeSegAttr<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasPpcDoubleDoubleLongDouble : std::false_type {};
template <typename T>
struct hasPpcDoubleDoubleLongDouble<
    T, std::void_t<decltype(&T::ppcDoubleDoubleLongDouble)>>
    : std::true_type {};

TEST(MissingFeaturesTest, PpcDoubleDoubleLongDoubleEntryRemoved) {
  static_assert(!hasPpcDoubleDoubleLongDouble<cir::MissingFeatures>::value,
                "ppcDoubleDoubleLongDouble should stay removed — PPC is "
                "not a supported CIR target");
  EXPECT_FALSE(hasPpcDoubleDoubleLongDouble<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasMsCatchParamLifetime : std::false_type {};
template <typename T>
struct hasMsCatchParamLifetime<
    T, std::void_t<decltype(&T::msCatchParamLifetime)>> : std::true_type {};

TEST(MissingFeaturesTest, MsCatchParamLifetimeEntryRemoved) {
  static_assert(!hasMsCatchParamLifetime<cir::MissingFeatures>::value,
                "msCatchParamLifetime should stay removed — MSVC catch "
                "param lifetime is Windows-only");
  EXPECT_FALSE(hasMsCatchParamLifetime<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasIsInAllocaArgument : std::false_type {};
template <typename T>
struct hasIsInAllocaArgument<T,
                             std::void_t<decltype(&T::isInAllocaArgument)>>
    : std::true_type {};

TEST(MissingFeaturesTest, IsInAllocaArgumentEntryRemoved) {
  static_assert(!hasIsInAllocaArgument<cir::MissingFeatures>::value,
                "isInAllocaArgument should stay removed — inalloca is "
                "x86-32 MSVC ABI only, not applicable to CIR targets");
  EXPECT_FALSE(hasIsInAllocaArgument<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasLambdaInAllocaBody : std::false_type {};
template <typename T>
struct hasLambdaInAllocaBody<T,
                             std::void_t<decltype(&T::lambdaInAllocaBody)>>
    : std::true_type {};

TEST(MissingFeaturesTest, LambdaInAllocaBodyEntryRemoved) {
  static_assert(!hasLambdaInAllocaBody<cir::MissingFeatures>::value,
                "lambdaInAllocaBody should stay removed — lambda inalloca "
                "is x86-32 MSVC ABI only, not applicable to CIR targets");
  EXPECT_FALSE(hasLambdaInAllocaBody<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasStopAutoInit : std::false_type {};
template <typename T>
struct hasStopAutoInit<T, std::void_t<decltype(&T::stopAutoInit)>>
    : std::true_type {};

TEST(MissingFeaturesTest, StopAutoInitEntryRemoved) {
  static_assert(!hasStopAutoInit<cir::MissingFeatures>::value,
                "stopAutoInit should stay removed — now implemented: "
                "CIRGenModule::stopAutoInit() checks stop-after counter");
  EXPECT_FALSE(hasStopAutoInit<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasTrivialAutoVarInitMaxSize : std::false_type {};
template <typename T>
struct hasTrivialAutoVarInitMaxSize<
    T, std::void_t<decltype(&T::trivialAutoVarInitMaxSize)>>
    : std::true_type {};

TEST(MissingFeaturesTest, TrivialAutoVarInitMaxSizeEntryRemoved) {
  static_assert(!hasTrivialAutoVarInitMaxSize<cir::MissingFeatures>::value,
                "trivialAutoVarInitMaxSize should stay removed — now "
                "implemented: max-size check skips large variables");
  EXPECT_FALSE(hasTrivialAutoVarInitMaxSize<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasLikelihoodDiagOnInfiniteLoop : std::false_type {};
template <typename T>
struct hasLikelihoodDiagOnInfiniteLoop<
    T, std::void_t<decltype(&T::likelihoodDiagOnInfiniteLoop)>>
    : std::true_type {};

TEST(MissingFeaturesTest, LikelihoodDiagOnInfiniteLoopEntryRemoved) {
  static_assert(
      !hasLikelihoodDiagOnInfiniteLoop<cir::MissingFeatures>::value,
      "likelihoodDiagOnInfiniteLoop should stay removed — now implemented: "
      "CIR emits diagnostic when [[likely]]/[[unlikely]] annotates infinite "
      "while loop body");
  EXPECT_FALSE(
      hasLikelihoodDiagOnInfiniteLoop<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasOptimizeSizeDelete : std::false_type {};
template <typename T>
struct hasOptimizeSizeDelete<T,
                            std::void_t<decltype(&T::optimizeSizeDelete)>>
    : std::true_type {};

TEST(MissingFeaturesTest, OptimizeSizeDeleteEntryRemoved) {
  static_assert(!hasOptimizeSizeDelete<cir::MissingFeatures>::value,
                "optimizeSizeDelete should stay removed — OGCG reverted the "
                "-Oz null-check elision for delete expressions");
  EXPECT_FALSE(hasOptimizeSizeDelete<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasMayHaveIntegerOverflow : std::false_type {};
template <typename T>
struct hasMayHaveIntegerOverflow<
    T, std::void_t<decltype(&T::mayHaveIntegerOverflow)>> : std::true_type {};

TEST(MissingFeaturesTest, MayHaveIntegerOverflowEntryRemoved) {
  static_assert(!hasMayHaveIntegerOverflow<cir::MissingFeatures>::value,
                "mayHaveIntegerOverflow should stay removed — now implemented: "
                "BinOpInfo uses APInt overflow-checked operations");
  EXPECT_FALSE(hasMayHaveIntegerOverflow<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasSetDivFPAccuracy : std::false_type {};
template <typename T>
struct hasSetDivFPAccuracy<T, std::void_t<decltype(&T::setDivFPAccuracy)>>
    : std::true_type {};

TEST(MissingFeaturesTest, SetDivFPAccuracyEntryRemoved) {
  static_assert(!hasSetDivFPAccuracy<cir::MissingFeatures>::value,
                "setDivFPAccuracy should stay removed — OGCG no longer has "
                "setDivFPAccuracy; FP accuracy handled via FastMathFlags");
  EXPECT_FALSE(hasSetDivFPAccuracy<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasReferenceGlobalInvariantStart : std::false_type {};
template <typename T>
struct hasReferenceGlobalInvariantStart<
    T, std::void_t<decltype(&T::referenceGlobalInvariantStart)>>
    : std::true_type {};

TEST(MissingFeaturesTest, ReferenceGlobalInvariantStartEntryRemoved) {
  static_assert(!hasReferenceGlobalInvariantStart<cir::MissingFeatures>::value,
                "referenceGlobalInvariantStart should stay removed — OGCG does "
                "not emit invariant.start for reference globals");
  EXPECT_FALSE(
      hasReferenceGlobalInvariantStart<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasEmitDefinitionAsAlias : std::false_type {};
template <typename T>
struct hasEmitDefinitionAsAlias<
    T, std::void_t<decltype(&T::emitDefinitionAsAlias)>> : std::true_type {};

TEST(MissingFeaturesTest, EmitDefinitionAsAliasEntryRemoved) {
  static_assert(!hasEmitDefinitionAsAlias<cir::MissingFeatures>::value,
                "emitDefinitionAsAlias should stay removed — MSVC-only "
                "destructor aliasing; Itanium handled directly");
  EXPECT_FALSE(hasEmitDefinitionAsAlias<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasDynamicCastSourceNullCheck : std::false_type {};
template <typename T>
struct hasDynamicCastSourceNullCheck<
    T, std::void_t<decltype(&T::dynamicCastSourceNullCheck)>>
    : std::true_type {};

TEST(MissingFeaturesTest, DynamicCastSourceNullCheckEntryRemoved) {
  static_assert(!hasDynamicCastSourceNullCheck<cir::MissingFeatures>::value,
                "dynamicCastSourceNullCheck should stay removed — null check "
                "is implemented in LoweringPrepareItaniumCXXABI");
  EXPECT_FALSE(hasDynamicCastSourceNullCheck<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasTrivialAssignmentOptimization : std::false_type {};
template <typename T>
struct hasTrivialAssignmentOptimization<
    T, std::void_t<decltype(&T::trivialAssignmentOptimization)>>
    : std::true_type {};

TEST(MissingFeaturesTest, TrivialAssignmentOptimizationEntryRemoved) {
  static_assert(
      !hasTrivialAssignmentOptimization<cir::MissingFeatures>::value,
      "trivialAssignmentOptimization should stay removed — CIR preserves "
      "operator calls and defers optimization to later passes");
  EXPECT_FALSE(
      hasTrivialAssignmentOptimization<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasPgoRegionCounters : std::false_type {};
template <typename T>
struct hasPgoRegionCounters<T, std::void_t<decltype(&T::pgoRegionCounters)>>
    : std::true_type {};

TEST(MissingFeaturesTest, PgoRegionCountersEntryRemoved) {
  static_assert(!hasPgoRegionCounters<cir::MissingFeatures>::value,
                "pgoRegionCounters should stay removed — CIR does not track "
                "OGCG region-counter map bookkeeping in CIRGenFunction");
  EXPECT_FALSE(hasPgoRegionCounters<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasPgoUse : std::false_type {};
template <typename T>
struct hasPgoUse<T, std::void_t<decltype(&T::pgoUse)>> : std::true_type {};

TEST(MissingFeaturesTest, PgoUseEntryRemoved) {
  static_assert(!hasPgoUse<cir::MissingFeatures>::value,
                "pgoUse should stay removed — CIR does not thread OGCG's "
                "PGO state object through stmt/condition emission");
  EXPECT_FALSE(hasPgoUse<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasQualifiedTypes : std::false_type {};
template <typename T>
struct hasQualifiedTypes<T, std::void_t<decltype(&T::qualifiedTypes)>>
    : std::true_type {};

TEST(MissingFeaturesTest, QualifiedTypesEntryRemoved) {
  static_assert(!hasQualifiedTypes<cir::MissingFeatures>::value,
                "qualifiedTypes should stay removed — no remaining callers");
  EXPECT_FALSE(hasQualifiedTypes<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasRecordDeclHasFlexibleArrayMember : std::false_type {};
template <typename T>
struct hasRecordDeclHasFlexibleArrayMember<
    T, std::void_t<decltype(&T::recordDeclHasFlexibleArrayMember)>>
    : std::true_type {};

TEST(MissingFeaturesTest, RecordDeclHasFlexibleArrayMemberEntryRemoved) {
  static_assert(!hasRecordDeclHasFlexibleArrayMember<cir::MissingFeatures>::value,
                "recordDeclHasFlexibleArrayMember should stay removed — "
                "target lowering now checks AST RecordDecl directly");
  EXPECT_FALSE(
      hasRecordDeclHasFlexibleArrayMember<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasProfileInstrBlocking : std::false_type {};
template <typename T>
struct hasProfileInstrBlocking<
    T, std::void_t<decltype(&T::profileInstrBlocking)>> : std::true_type {};

TEST(MissingFeaturesTest, ProfileInstrBlockingEntryRemoved) {
  static_assert(!hasProfileInstrBlocking<cir::MissingFeatures>::value,
                "profileInstrBlocking should stay removed — CIR no longer "
                "tracks profile-instr attr handling via MissingFeatures guard");
  EXPECT_FALSE(hasProfileInstrBlocking<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasDevirtualizeDestructor : std::false_type {};
template <typename T>
struct hasDevirtualizeDestructor<
    T, std::void_t<decltype(&T::devirtualizeDestructor)>> : std::true_type {};

TEST(MissingFeaturesTest, DevirtualizeDestructorEntryRemoved) {
  static_assert(!hasDevirtualizeDestructor<cir::MissingFeatures>::value,
                "devirtualizeDestructor should stay removed — CIR preserves "
                "virtual destructor calls and defers to LLVM");
  EXPECT_FALSE(hasDevirtualizeDestructor<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasMemberPointerABIDispatch : std::false_type {};
template <typename T>
struct hasMemberPointerABIDispatch<
    T, std::void_t<decltype(&T::memberPointerABIDispatch)>>
    : std::true_type {};

TEST(MissingFeaturesTest, MemberPointerABIDispatchEntryRemoved) {
  static_assert(!hasMemberPointerABIDispatch<cir::MissingFeatures>::value,
                "memberPointerABIDispatch should stay removed — CIR uses "
                "high-level get_method op, ABI dispatch in target lowering");
  EXPECT_FALSE(hasMemberPointerABIDispatch<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasSwitchCaseLikelihood : std::false_type {};
template <typename T>
struct hasSwitchCaseLikelihood<
    T, std::void_t<decltype(&T::switchCaseLikelihood)>> : std::true_type {};

TEST(MissingFeaturesTest, SwitchCaseLikelihoodEntryRemoved) {
  static_assert(!hasSwitchCaseLikelihood<cir::MissingFeatures>::value,
                "switchCaseLikelihood should stay removed — now fully "
                "implemented via cir.branch_weights metadata");
  EXPECT_FALSE(hasSwitchCaseLikelihood<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasEmitStopPointInEmitStmt : std::false_type {};
template <typename T>
struct hasEmitStopPointInEmitStmt<
    T, std::void_t<decltype(&T::emitStopPointInEmitStmt)>>
    : std::true_type {};

TEST(MissingFeaturesTest, EmitStopPointInEmitStmtEntryRemoved) {
  static_assert(!hasEmitStopPointInEmitStmt<cir::MissingFeatures>::value,
                "emitStopPointInEmitStmt should stay removed — CIR uses "
                "MLIR locations on every op instead of explicit stop points");
  EXPECT_FALSE(hasEmitStopPointInEmitStmt<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasMsGuidDecl : std::false_type {};
template <typename T>
struct hasMsGuidDecl<T, std::void_t<decltype(&T::msGuidDecl)>>
    : std::true_type {};

TEST(MissingFeaturesTest, MsGuidDeclEntryRemoved) {
  static_assert(!hasMsGuidDecl<cir::MissingFeatures>::value,
                "msGuidDecl should stay removed — __declspec(uuid) is "
                "MSVC-only, Windows is not a supported CIR target");
  EXPECT_FALSE(hasMsGuidDecl<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasAtExitFallback : std::false_type {};
template <typename T>
struct hasAtExitFallback<T, std::void_t<decltype(&T::atExitFallback)>>
    : std::true_type {};

TEST(MissingFeaturesTest, AtExitFallbackEntryRemoved) {
  static_assert(!hasAtExitFallback<cir::MissingFeatures>::value,
                "atExitFallback should stay removed — __cxa_atexit is "
                "universally available on CIR's supported targets");
  EXPECT_FALSE(hasAtExitFallback<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasGuardedInitMaxInlineWidthCheck : std::false_type {};
template <typename T>
struct hasGuardedInitMaxInlineWidthCheck<
    T, std::void_t<decltype(&T::guardedInitMaxInlineWidthCheck)>>
    : std::true_type {};

TEST(MissingFeaturesTest, GuardedInitMaxInlineWidthCheckEntryRemoved) {
  static_assert(
      !hasGuardedInitMaxInlineWidthCheck<cir::MissingFeatures>::value,
      "guardedInitMaxInlineWidthCheck should stay removed — CIR targets "
      "(AArch64, x86_64) always have MaxAtomicInlineWidth > 0");
  EXPECT_FALSE(
      hasGuardedInitMaxInlineWidthCheck<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasLambdaCaptureNormalCleanup : std::false_type {};
template <typename T>
struct hasLambdaCaptureNormalCleanup<
    T, std::void_t<decltype(&T::lambdaCaptureNormalCleanup)>>
    : std::true_type {};

TEST(MissingFeaturesTest, LambdaCaptureNormalCleanupEntryRemoved) {
  static_assert(!hasLambdaCaptureNormalCleanup<cir::MissingFeatures>::value,
                "lambdaCaptureNormalCleanup should stay removed — CIR "
                "intentionally uses EHCleanup only for lambda captures");
  EXPECT_FALSE(hasLambdaCaptureNormalCleanup<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasThunkEmptyGlobalDecl : std::false_type {};
template <typename T>
struct hasThunkEmptyGlobalDecl<
    T, std::void_t<decltype(&T::thunkEmptyGlobalDecl)>> : std::true_type {};

TEST(MissingFeaturesTest, ThunkEmptyGlobalDeclEntryRemoved) {
  static_assert(!hasThunkEmptyGlobalDecl<cir::MissingFeatures>::value,
                "thunkEmptyGlobalDecl should stay removed — CIR intentionally "
                "passes real GlobalDecl to startFunction");
  EXPECT_FALSE(hasThunkEmptyGlobalDecl<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasFpVectorLogicalCmp : std::false_type {};
template <typename T>
struct hasFpVectorLogicalCmp<T, std::void_t<decltype(&T::fpVectorLogicalCmp)>>
    : std::true_type {};

TEST(MissingFeaturesTest, FpVectorLogicalCmpEntryRemoved) {
  static_assert(!hasFpVectorLogicalCmp<cir::MissingFeatures>::value,
                "fpVectorLogicalCmp should stay removed — VecCmpOp lowering "
                "correctly dispatches FP→FCmpOp(une) and int→ICmpOp(ne)");
  EXPECT_FALSE(hasFpVectorLogicalCmp<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasVectorLogicalOpSignExt : std::false_type {};
template <typename T>
struct hasVectorLogicalOpSignExt<
    T, std::void_t<decltype(&T::vectorLogicalOpSignExt)>> : std::true_type {};

TEST(MissingFeaturesTest, VectorLogicalOpSignExtEntryRemoved) {
  static_assert(
      !hasVectorLogicalOpSignExt<cir::MissingFeatures>::value,
      "vectorLogicalOpSignExt should stay removed — VecCmpOp lowering "
      "uses SExtOp for wider results and createBoolToInt uses SExtOp for "
      "signed types (Clang Sema guarantees signed result)");
  EXPECT_FALSE(hasVectorLogicalOpSignExt<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasWritebacks : std::false_type {};
template <typename T>
struct hasWritebacks<T, std::void_t<decltype(&T::writebacks)>>
    : std::true_type {};

TEST(MissingFeaturesTest, WritebacksEntryRemoved) {
  static_assert(!hasWritebacks<cir::MissingFeatures>::value,
                "writebacks should stay removed — ObjC ARC writebacks and "
                "HLSL out parameter writebacks are not applicable to CIR's "
                "supported C/C++/CUDA targets");
  EXPECT_FALSE(hasWritebacks<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasSwitchOp : std::false_type {};
template <typename T>
struct hasSwitchOp<T, std::void_t<decltype(&T::switchOp)>> : std::true_type {};

TEST(MissingFeaturesTest, SwitchOpEntryRemoved) {
  static_assert(!hasSwitchOp<cir::MissingFeatures>::value,
                "switchOp should stay removed — SwitchOp is fully implemented "
                "with CIRGen emission, LLVM lowering, and BreakOp verification");
  EXPECT_FALSE(hasSwitchOp<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasTryOp : std::false_type {};
template <typename T>
struct hasTryOp<T, std::void_t<decltype(&T::tryOp)>> : std::true_type {};

template <typename T, typename = void>
struct hasVlas : std::false_type {};
template <typename T>
struct hasVlas<T, std::void_t<decltype(&T::vlas)>> : std::true_type {};

template <typename T, typename = void>
struct hasVirtualDtorCallOutParam : std::false_type {};
template <typename T>
struct hasVirtualDtorCallOutParam<
    T, std::void_t<decltype(&T::virtualDtorCallOutParam)>> : std::true_type {};

template <typename T, typename = void>
struct hasX86AvxRoundingMode : std::false_type {};
template <typename T>
struct hasX86AvxRoundingMode<
    T, std::void_t<decltype(&T::x86AvxRoundingMode)>> : std::true_type {};

TEST(MissingFeaturesTest, TryOpEntryRemoved) {
  static_assert(
      !hasTryOp<cir::MissingFeatures>::value,
      "tryOp should stay removed — TryOp is emitted in CIRGen and handled "
      "through EH canonicalization/lowering without a MissingFeatures gate");
  EXPECT_FALSE(hasTryOp<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, VlasEntryRemoved) {
  static_assert(!hasVlas<cir::MissingFeatures>::value,
                "vlas should stay removed — VLA sizing and lowering are "
                "handled directly in dedicated CIRGen/lowering paths");
  EXPECT_FALSE(hasVlas<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, VirtualDtorCallOutParamEntryRemoved) {
  static_assert(
      !hasVirtualDtorCallOutParam<cir::MissingFeatures>::value,
      "virtualDtorCallOutParam should stay removed — Clang rejects "
      "[[clang::musttail]] on destructor calls, so virtual dtor emission "
      "has no valid post-emit call annotation site");
  EXPECT_FALSE(hasVirtualDtorCallOutParam<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, X86AvxRoundingModeEntryRemoved) {
  static_assert(
      !hasX86AvxRoundingMode<cir::MissingFeatures>::value,
      "x86AvxRoundingMode should stay removed — AVX-512 sqrt builtins now "
      "preserve non-default rounding modes via target intrinsics and lower "
      "CUR_DIRECTION through cir.sqrt");
  EXPECT_FALSE(hasX86AvxRoundingMode<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasTypeChecks : std::false_type {};
template <typename T>
struct hasTypeChecks<T, std::void_t<decltype(&T::typeChecks)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasVtableEmitMetadata : std::false_type {};
template <typename T>
struct hasVtableEmitMetadata<
    T, std::void_t<decltype(&T::vtableEmitMetadata)>> : std::true_type {};

template <typename T, typename = void>
struct hasWeakRefReference : std::false_type {};
template <typename T>
struct hasWeakRefReference<T, std::void_t<decltype(&T::weakRefReference)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasVectorConstants : std::false_type {};
template <typename T>
struct hasVectorConstants<T, std::void_t<decltype(&T::vectorConstants)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasVectorEltBitvectorBitcast : std::false_type {};
template <typename T>
struct hasVectorEltBitvectorBitcast<
    T, std::void_t<decltype(&T::vectorEltBitvectorBitcast)>>
    : std::true_type {};

template <typename T, typename = void>
struct hasVectorEltNarrowTypeZExt : std::false_type {};
template <typename T>
struct hasVectorEltNarrowTypeZExt<
    T, std::void_t<decltype(&T::vectorEltNarrowTypeZExt)>> : std::true_type {};

template <typename T, typename = void>
struct hasX86TypeClassification : std::false_type {};
template <typename T>
struct hasX86TypeClassification<
    T, std::void_t<decltype(&T::X86TypeClassification)>> : std::true_type {};

TEST(MissingFeaturesTest, TypeChecksEntryRemoved) {
  static_assert(!hasTypeChecks<cir::MissingFeatures>::value,
                "typeChecks should stay removed — sanitizer type-check parity "
                "is tracked directly in CIRGen call-sites");
  EXPECT_FALSE(hasTypeChecks<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, VtableEmitMetadataEntryRemoved) {
  static_assert(!hasVtableEmitMetadata<cir::MissingFeatures>::value,
                "vtableEmitMetadata should stay removed — vtable metadata "
                "is emitted directly by CIR vtable emission paths");
  EXPECT_FALSE(hasVtableEmitMetadata<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, WeakRefReferenceEntryRemoved) {
  static_assert(!hasWeakRefReference<cir::MissingFeatures>::value,
                "weakRefReference should stay removed — weak references are "
                "resolved directly through CIRGenModule::getWeakRefReference");
  EXPECT_FALSE(hasWeakRefReference<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, VectorConstantsEntryRemoved) {
  static_assert(!hasVectorConstants<cir::MissingFeatures>::value,
                "vectorConstants should stay removed — vector auto-var "
                "constant initialization is handled directly in CIRGenDecl");
  EXPECT_FALSE(hasVectorConstants<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, VectorEltBitvectorBitcastEntryRemoved) {
  static_assert(!hasVectorEltBitvectorBitcast<cir::MissingFeatures>::value,
                "vectorEltBitvectorBitcast should stay removed — CIR vector "
                "element stores operate on first-class vector types without "
                "OGCG's iN <-> <N x i1> bitcast path");
  EXPECT_FALSE(hasVectorEltBitvectorBitcast<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, VectorEltNarrowTypeZExtEntryRemoved) {
  static_assert(!hasVectorEltNarrowTypeZExt<cir::MissingFeatures>::value,
                "vectorEltNarrowTypeZExt should stay removed — CIR performs "
                "element-cast conversions before VecInsertOp and does not "
                "need OGCG's narrow-load zero-extension helper gate");
  EXPECT_FALSE(hasVectorEltNarrowTypeZExt<cir::MissingFeatures>::value);
}

TEST(MissingFeaturesTest, X86TypeClassificationEntryRemoved) {
  static_assert(!hasX86TypeClassification<cir::MissingFeatures>::value,
                "X86TypeClassification should stay removed — unsupported x86 "
                "classification paths are diagnosed locally in X86 target "
                "lowering, not tracked by a global MissingFeatures gate");
  EXPECT_FALSE(hasX86TypeClassification<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasStackBase : std::false_type {};
template <typename T>
struct hasStackBase<T, std::void_t<decltype(&T::stackBase)>>
    : std::true_type {};

TEST(MissingFeaturesTest, StackBaseEntryRemoved) {
  static_assert(!hasStackBase<cir::MissingFeatures>::value,
                "stackBase should stay removed — MSVC inalloca argument "
                "memory is not needed on CIR's supported targets");
  EXPECT_FALSE(hasStackBase<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasStackSaveOp : std::false_type {};
template <typename T>
struct hasStackSaveOp<T, std::void_t<decltype(&T::stackSaveOp)>>
    : std::true_type {};

TEST(MissingFeaturesTest, StackSaveOpEntryRemoved) {
  static_assert(!hasStackSaveOp<cir::MissingFeatures>::value,
                "stackSaveOp should stay removed — FlattenCFG no longer uses "
                "a stale MissingFeatures assert gate");
  EXPECT_FALSE(hasStackSaveOp<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasSupportIFuncAttr : std::false_type {};
template <typename T>
struct hasSupportIFuncAttr<T, std::void_t<decltype(&T::supportIFuncAttr)>>
    : std::true_type {};

TEST(MissingFeaturesTest, SupportIFuncAttrEntryRemoved) {
  static_assert(!hasSupportIFuncAttr<cir::MissingFeatures>::value,
                "supportIFuncAttr should stay removed — stale guard asserts "
                "were removed from declaration/local-alias queries");
  EXPECT_FALSE(hasSupportIFuncAttr<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasTargetCIRGenInfoOS : std::false_type {};
template <typename T>
struct hasTargetCIRGenInfoOS<
    T, std::void_t<decltype(&T::targetCIRGenInfoOS)>> : std::true_type {};

TEST(MissingFeaturesTest, TargetCIRGenInfoOSEntryRemoved) {
  static_assert(!hasTargetCIRGenInfoOS<cir::MissingFeatures>::value,
                "targetCIRGenInfoOS should stay removed — unsupported OS "
                "triples now use a direct Linux fallback path");
  EXPECT_FALSE(hasTargetCIRGenInfoOS<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasTargetMaxVectorAlign : std::false_type {};
template <typename T>
struct hasTargetMaxVectorAlign<
    T, std::void_t<decltype(&T::targetMaxVectorAlign)>> : std::true_type {};

TEST(MissingFeaturesTest, TargetMaxVectorAlignEntryRemoved) {
  static_assert(!hasTargetMaxVectorAlign<cir::MissingFeatures>::value,
                "targetMaxVectorAlign should stay removed — CIRLowerContext "
                "now caps vector alignment via TargetInfo::getMaxVectorAlign");
  EXPECT_FALSE(hasTargetMaxVectorAlign<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasTbaaMergeTBAAInfo : std::false_type {};
template <typename T>
struct hasTbaaMergeTBAAInfo<
    T, std::void_t<decltype(&T::tbaaMergeTBAAInfo)>> : std::true_type {};

TEST(MissingFeaturesTest, TbaaMergeTBAAInfoEntryRemoved) {
  static_assert(!hasTbaaMergeTBAAInfo<cir::MissingFeatures>::value,
                "tbaaMergeTBAAInfo should stay removed — CIRGenTBAA merge "
                "helpers now match OGCG behavior");
  EXPECT_FALSE(hasTbaaMergeTBAAInfo<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasTbaaNewStructPath : std::false_type {};
template <typename T>
struct hasTbaaNewStructPath<
    T, std::void_t<decltype(&T::tbaaNewStructPath)>> : std::true_type {};

TEST(MissingFeaturesTest, TbaaNewStructPathEntryRemoved) {
  static_assert(!hasTbaaNewStructPath<cir::MissingFeatures>::value,
                "tbaaNewStructPath should stay removed — CIR uses direct "
                "NewStructPathTBAA NYI handling at call-sites");
  EXPECT_FALSE(hasTbaaNewStructPath<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasTbaaStruct : std::false_type {};
template <typename T>
struct hasTbaaStruct<T, std::void_t<decltype(&T::tbaaStruct)>>
    : std::true_type {};

TEST(MissingFeaturesTest, TbaaStructEntryRemoved) {
  static_assert(!hasTbaaStruct<cir::MissingFeatures>::value,
                "tbaaStruct should stay removed — CIR uses direct NYI "
                "handling in getTBAAStructInfo");
  EXPECT_FALSE(hasTbaaStruct<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasVerboseTrapDebugMessage : std::false_type {};
template <typename T>
struct hasVerboseTrapDebugMessage<
    T, std::void_t<decltype(&T::verboseTrapDebugMessage)>> : std::true_type {};

TEST(MissingFeaturesTest, VerboseTrapDebugMessageEntryRemoved) {
  static_assert(!hasVerboseTrapDebugMessage<cir::MissingFeatures>::value,
                "verboseTrapDebugMessage should stay removed — CIR emits the "
                "trap intrinsic correctly without debug message decoration");
  EXPECT_FALSE(hasVerboseTrapDebugMessage<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasNoReturn : std::false_type {};
template <typename T>
struct hasNoReturn<T, std::void_t<decltype(&T::noReturn)>> : std::true_type {};

TEST(MissingFeaturesTest, NoReturnEntryRemoved) {
  static_assert(!hasNoReturn<cir::MissingFeatures>::value,
                "noReturn should stay removed — noreturn is handled "
                "ABI-agnostically in CIRGen and propagated during lowering");
  EXPECT_FALSE(hasNoReturn<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasZextOp : std::false_type {};
template <typename T>
struct hasZextOp<T, std::void_t<decltype(&T::zextOp)>> : std::true_type {};

TEST(MissingFeaturesTest, ZextOpEntryRemoved) {
  static_assert(!hasZextOp<cir::MissingFeatures>::value,
                "zextOp should stay removed — CIR models extension through "
                "cir.cast and BrCond lowering consumes i1 directly");
  EXPECT_FALSE(hasZextOp<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasIsCXXRecordDecl : std::false_type {};
template <typename T>
struct hasIsCXXRecordDecl<T, std::void_t<decltype(&T::isCXXRecordDecl)>>
    : std::true_type {};

TEST(MissingFeaturesTest, IsCXXRecordDeclEntryRemoved) {
  static_assert(!hasIsCXXRecordDecl<cir::MissingFeatures>::value,
                "isCXXRecordDecl should stay removed — target lowering now "
                "queries CXXRecordDecl properties from AST via "
                "RecordType::getAst()");
  EXPECT_FALSE(hasIsCXXRecordDecl<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasCXXRecordIsDynamicClass : std::false_type {};
template <typename T>
struct hasCXXRecordIsDynamicClass<
    T, std::void_t<decltype(&T::CXXRecordIsDynamicClass)>> : std::true_type {};

TEST(MissingFeaturesTest, CXXRecordIsDynamicClassEntryRemoved) {
  static_assert(!hasCXXRecordIsDynamicClass<cir::MissingFeatures>::value,
                "CXXRecordIsDynamicClass should stay removed — "
                "RecordLayoutBuilder queries isDynamicClass() from AST");
  EXPECT_FALSE(hasCXXRecordIsDynamicClass<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasCXXRecordDeclIsEmptyCXX11 : std::false_type {};
template <typename T>
struct hasCXXRecordDeclIsEmptyCXX11<
    T, std::void_t<decltype(&T::CXXRecordDeclIsEmptyCXX11)>> : std::true_type {
};

TEST(MissingFeaturesTest, CXXRecordDeclIsEmptyCXX11EntryRemoved) {
  static_assert(
      !hasCXXRecordDeclIsEmptyCXX11<cir::MissingFeatures>::value,
      "CXXRecordDeclIsEmptyCXX11 should stay removed — CIR's RecordType "
      "handles empty class placement in its flattened member list");
  EXPECT_FALSE(hasCXXRecordDeclIsEmptyCXX11<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasCXXRecordDeclIsPOD : std::false_type {};
template <typename T>
struct hasCXXRecordDeclIsPOD<T, std::void_t<decltype(&T::CXXRecordDeclIsPOD)>>
    : std::true_type {};

TEST(MissingFeaturesTest, CXXRecordDeclIsPODEntryRemoved) {
  static_assert(!hasCXXRecordDeclIsPOD<cir::MissingFeatures>::value,
                "CXXRecordDeclIsPOD should stay removed — "
                "RecordLayoutBuilder queries isPOD() from AST");
  EXPECT_FALSE(hasCXXRecordDeclIsPOD<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasBitFieldPaddingDiagnostics : std::false_type {};
template <typename T>
struct hasBitFieldPaddingDiagnostics<
    T, std::void_t<decltype(&T::bitFieldPaddingDiagnostics)>> : std::true_type {
};

TEST(MissingFeaturesTest, BitFieldPaddingDiagnosticsEntryRemoved) {
  static_assert(!hasBitFieldPaddingDiagnostics<cir::MissingFeatures>::value,
                "bitFieldPaddingDiagnostics should stay removed — CIR's "
                "target lowering doesn't emit clang diagnostics");
  EXPECT_FALSE(hasBitFieldPaddingDiagnostics<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasAstContextGetExternalSource : std::false_type {};
template <typename T>
struct hasAstContextGetExternalSource<
    T, std::void_t<decltype(&T::astContextGetExternalSource)>>
    : std::true_type {};

TEST(MissingFeaturesTest, AstContextGetExternalSourceEntryRemoved) {
  static_assert(
      !hasAstContextGetExternalSource<cir::MissingFeatures>::value,
      "astContextGetExternalSource should stay removed — CIR doesn't "
      "support external layout sources");
  EXPECT_FALSE(hasAstContextGetExternalSource<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasDuplicateMangledNameDiag : std::false_type {};
template <typename T>
struct hasDuplicateMangledNameDiag<
    T, std::void_t<decltype(&T::duplicateMangledNameDiag)>> : std::true_type {};

TEST(MissingFeaturesTest, DuplicateMangledNameDiagEntryRemoved) {
  static_assert(!hasDuplicateMangledNameDiag<cir::MissingFeatures>::value,
                "duplicateMangledNameDiag should stay removed — CIR's "
                "CIRGen doesn't emit clang diagnostics");
  EXPECT_FALSE(hasDuplicateMangledNameDiag<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasThunkReturnABIIndirect : std::false_type {};
template <typename T>
struct hasThunkReturnABIIndirect<
    T, std::void_t<decltype(&T::thunkReturnABIIndirect)>> : std::true_type {};

TEST(MissingFeaturesTest, ThunkReturnABIIndirectEntryRemoved) {
  static_assert(!hasThunkReturnABIIndirect<cir::MissingFeatures>::value,
                "thunkReturnABIIndirect should stay removed — CIR defers "
                "ABI-indirect return to CallConvLowering");
  EXPECT_FALSE(hasThunkReturnABIIndirect<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasThunkReturnSlotOpts : std::false_type {};
template <typename T>
struct hasThunkReturnSlotOpts<T,
                              std::void_t<decltype(&T::thunkReturnSlotOpts)>>
    : std::true_type {};

TEST(MissingFeaturesTest, ThunkReturnSlotOptsEntryRemoved) {
  static_assert(!hasThunkReturnSlotOpts<cir::MissingFeatures>::value,
                "thunkReturnSlotOpts should stay removed — CIR now passes "
                "volatile qualification to thunk ReturnValueSlot");
  EXPECT_FALSE(hasThunkReturnSlotOpts<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasUnprototypedThunkAttribute : std::false_type {};
template <typename T>
struct hasUnprototypedThunkAttribute<
    T, std::void_t<decltype(&T::unprototypedThunkAttribute)>>
    : std::true_type {};

TEST(MissingFeaturesTest, UnprototypedThunkAttributeEntryRemoved) {
  static_assert(!hasUnprototypedThunkAttribute<cir::MissingFeatures>::value,
                "unprototypedThunkAttribute should stay removed — CIR now "
                "propagates thunk as an LLVM passthrough function attribute");
  EXPECT_FALSE(
      hasUnprototypedThunkAttribute<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasAtomicStoreMemOrderDowngrade : std::false_type {};
template <typename T>
struct hasAtomicStoreMemOrderDowngrade<
    T, std::void_t<decltype(&T::atomicStoreMemOrderDowngrade)>>
    : std::true_type {};

TEST(MissingFeaturesTest, AtomicStoreMemOrderDowngradeEntryRemoved) {
  static_assert(
      !hasAtomicStoreMemOrderDowngrade<cir::MissingFeatures>::value,
      "atomicStoreMemOrderDowngrade should stay removed — CIR already "
      "downgrades Acquire/AcquireRelease for atomic stores");
  EXPECT_FALSE(
      hasAtomicStoreMemOrderDowngrade<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasLowerModeOptLevel : std::false_type {};
template <typename T>
struct hasLowerModeOptLevel<T,
                           std::void_t<decltype(&T::lowerModeOptLevel)>>
    : std::true_type {};

TEST(MissingFeaturesTest, LowerModeOptLevelEntryRemoved) {
  static_assert(!hasLowerModeOptLevel<cir::MissingFeatures>::value,
                "lowerModeOptLevel should stay removed — "
                "invariant group metadata for loads/stores is implemented");
  EXPECT_FALSE(hasLowerModeOptLevel<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasTbaaIncompleteType : std::false_type {};
template <typename T>
struct hasTbaaIncompleteType<T,
                            std::void_t<decltype(&T::tbaaIncompleteType)>>
    : std::true_type {};

TEST(MissingFeaturesTest, TbaaIncompleteTypeEntryRemoved) {
  static_assert(!hasTbaaIncompleteType<cir::MissingFeatures>::value,
                "tbaaIncompleteType should stay removed — "
                "getAccessInfo() returns getIncompleteInfo() matching OGCG");
  EXPECT_FALSE(hasTbaaIncompleteType<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasTbaaMayAlias : std::false_type {};
template <typename T>
struct hasTbaaMayAlias<T, std::void_t<decltype(&T::tbaaMayAlias)>>
    : std::true_type {};

TEST(MissingFeaturesTest, TbaaMayAliasEntryRemoved) {
  static_assert(!hasTbaaMayAlias<cir::MissingFeatures>::value,
                "tbaaMayAlias should stay removed — "
                "may_alias handling matches OGCG in all three call sites");
  EXPECT_FALSE(hasTbaaMayAlias<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasTbaaTagForStruct : std::false_type {};
template <typename T>
struct hasTbaaTagForStruct<T, std::void_t<decltype(&T::tbaaTagForStruct)>>
    : std::true_type {};

TEST(MissingFeaturesTest, TbaaTagForStructEntryRemoved) {
  static_assert(!hasTbaaTagForStruct<cir::MissingFeatures>::value,
                "tbaaTagForStruct should stay removed — "
                "getTypeInfo() returns getValidBaseTypeInfo() matching OGCG");
  EXPECT_FALSE(hasTbaaTagForStruct<cir::MissingFeatures>::value);
}

template <typename T, typename = void>
struct hasCoroutineExceptions : std::false_type {};
template <typename T>
struct hasCoroutineExceptions<T,
                             std::void_t<decltype(&T::coroutineExceptions)>>
    : std::true_type {};

TEST(MissingFeaturesTest, CoroutineExceptionsEntryRemoved) {
  static_assert(!hasCoroutineExceptions<cir::MissingFeatures>::value,
                "coroutineExceptions should stay removed — "
                "no assert callers, NYI documented inline");
  EXPECT_FALSE(hasCoroutineExceptions<cir::MissingFeatures>::value);
}
