//===------- ItaniumCXXABI.cpp - Emit CIR code Itanium-specific code  -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides CIR lowering logic targeting the Itanium C++ ABI. The class in
// this file generates records that follow the Itanium C++ ABI, which is
// documented at:
//  https://itanium-cxx-abi.github.io/cxx-abi/abi.html
//  https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html
//
// It also supports the closely-related ARM ABI, documented at:
// https://developer.arm.com/documentation/ihi0041/g/
//
// This file partially mimics clang/lib/CodeGen/ItaniumCXXABI.cpp. The queries
// are adapted to operate on the CIR dialect, however.
//
//===----------------------------------------------------------------------===//

#include "../LoweringPrepareCXXABI.h"
#include "CIRCXXABI.h"
#include "LowerModule.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/CIR/ABIArgInfo.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/MissingFeatures.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/ErrorHandling.h"

namespace cir {

namespace {

// Ensure a member pointer value is a RecordType. ABI coercion in CIRGen may
// represent member pointers as !cir.array<!u64i x 2> instead of the expected
// record type {ptr, ptrdiff_t}. When this happens, bitcast through memory
// to convert back to the record type.
static mlir::Value ensureMemberPtrRecord(mlir::OpBuilder &builder,
                                         mlir::Location loc,
                                         mlir::Value val,
                                         cir::RecordType expectedTy) {
  if (mlir::isa<cir::RecordType>(val.getType()))
    return val;
  // The value is an array (ABI coerced). Bitcast to the expected record type
  // via alloca + store + load.
  auto ptrTy = cir::PointerType::get(val.getType());
  auto alloca = cir::AllocaOp::create(builder, loc, ptrTy, val.getType(),
                                      "memptr.coerce",
                                      builder.getI64IntegerAttr(8));
  cir::StoreOp::create(builder, loc, mlir::TypeRange{}, val, alloca,
                       /*isVolatile=*/false, /*isNonTemporal=*/false,
                       /*alignment=*/mlir::IntegerAttr{},
                       /*syncScope=*/cir::SyncScopeKindAttr{},
                       /*memOrder=*/cir::MemOrderAttr{},
                       /*tbaa=*/cir::TBAAAttr{});
  auto castPtr = cir::CastOp::create(builder, loc,
                                      cir::PointerType::get(expectedTy),
                                      cir::CastKind::bitcast, alloca);
  return cir::LoadOp::create(builder, loc, expectedTy, castPtr,
                              /*isDeref=*/false, /*isVolatile=*/false,
                              /*isNonTemporal=*/false,
                              /*alignment=*/mlir::IntegerAttr{},
                              /*syncScope=*/cir::SyncScopeKindAttr{},
                              /*memOrder=*/cir::MemOrderAttr{},
                              /*tbaa=*/cir::TBAAAttr{});
}

struct CIRVTablePointerAuthInfo {
  unsigned key;
  mlir::Value discriminator;
};

static cir::IntType getUIntPtrCIRTy(LowerModule &lowerMod) {
  const clang::TargetInfo &target = lowerMod.getTarget();
  return cir::IntType::get(lowerMod.getMLIRContext(),
                           target.getPointerWidth(clang::LangAS::Default),
                           /*isSigned=*/false);
}

static std::optional<clang::PointerAuthQualifier>
getVTablePointerAuthentication(LowerModule &LM,
                               const clang::CXXRecordDecl *record) {
  if (!record->getDefinition() || !record->isPolymorphic())
    return std::nullopt;

  const clang::PointerAuthSchema &defaultAuthentication =
      LM.getContext().getCodeGenOpts().PointerAuth.CXXVTablePointers;
  if (!defaultAuthentication)
    return std::nullopt;

  clang::ASTContext &context = record->getASTContext();
  const clang::CXXRecordDecl *primaryBase =
      context.baseForVTableAuthentication(record);

  unsigned key = defaultAuthentication.getKey();
  bool addressDiscriminated = defaultAuthentication.isAddressDiscriminated();
  auto defaultDiscrimination = defaultAuthentication.getOtherDiscrimination();
  unsigned typeBasedDiscriminator =
      context.getPointerAuthVTablePointerDiscriminator(primaryBase);

  unsigned discriminator = 0;
  switch (defaultDiscrimination) {
  case clang::PointerAuthSchema::Discrimination::None:
    break;
  case clang::PointerAuthSchema::Discrimination::Type:
    discriminator = typeBasedDiscriminator;
    break;
  case clang::PointerAuthSchema::Discrimination::Constant:
    discriminator = defaultAuthentication.getConstantDiscrimination();
    break;
  case clang::PointerAuthSchema::Discrimination::Decl:
    llvm_unreachable(
        "CXXVTablePointers schema must not use declaration discrimination");
  }

  if (auto explicitAuthentication =
          primaryBase->getAttr<clang::VTablePointerAuthenticationAttr>()) {
    auto explicitAddressDiscrimination =
        explicitAuthentication->getAddressDiscrimination();
    auto explicitDiscriminator = explicitAuthentication->getExtraDiscrimination();

    unsigned explicitKey = explicitAuthentication->getKey();
    if (explicitKey == clang::VTablePointerAuthenticationAttr::NoKey)
      return std::nullopt;

    if (explicitKey != clang::VTablePointerAuthenticationAttr::DefaultKey) {
      if (explicitKey ==
          clang::VTablePointerAuthenticationAttr::ProcessIndependent)
        key = static_cast<unsigned>(clang::PointerAuthSchema::ARM8_3Key::ASDA);
      else {
        assert(explicitKey ==
               clang::VTablePointerAuthenticationAttr::ProcessDependent);
        key = static_cast<unsigned>(clang::PointerAuthSchema::ARM8_3Key::ASDB);
      }
    }

    if (explicitAddressDiscrimination !=
        clang::VTablePointerAuthenticationAttr::DefaultAddressDiscrimination)
      addressDiscriminated =
          explicitAddressDiscrimination ==
          clang::VTablePointerAuthenticationAttr::AddressDiscrimination;

    if (explicitDiscriminator ==
        clang::VTablePointerAuthenticationAttr::TypeDiscrimination)
      discriminator = typeBasedDiscriminator;
    else if (explicitDiscriminator ==
             clang::VTablePointerAuthenticationAttr::CustomDiscrimination)
      discriminator = explicitAuthentication->getCustomDiscriminationValue();
    else if (explicitDiscriminator ==
             clang::VTablePointerAuthenticationAttr::NoExtraDiscrimination)
      discriminator = 0;
  }

  return clang::PointerAuthQualifier::Create(
      key, addressDiscriminated, discriminator,
      clang::PointerAuthenticationMode::SignAndAuth,
      /*IsIsaPointer=*/false, /*AuthenticatesNullValues=*/false);
}

static std::optional<CIRVTablePointerAuthInfo>
getVTablePointerAuthInfo(LowerModule &LM, mlir::Location loc,
                         const clang::CXXRecordDecl *record,
                         mlir::Value storageAddress,
                         mlir::OpBuilder &builder) {
  std::optional<clang::PointerAuthQualifier> authentication =
      getVTablePointerAuthentication(LM, record);
  if (!authentication)
    return std::nullopt;

  cir::IntType uintptrTy = getUIntPtrCIRTy(LM);
  cir::IntType keyTy =
      cir::IntType::get(builder.getContext(), /*width=*/32, /*isSigned=*/false);
  (void)keyTy;

  mlir::Value discriminator;
  if (unsigned extraDiscriminator = authentication->getExtraDiscriminator())
    discriminator = cir::ConstantOp::create(
        builder, loc, cir::IntAttr::get(uintptrTy, extraDiscriminator));

  if (authentication->isAddressDiscriminated()) {
    mlir::Value storageAddressInt = cir::CastOp::create(
        builder, loc, uintptrTy, cir::CastKind::ptr_to_int, storageAddress);
    if (discriminator) {
      discriminator = cir::LLVMIntrinsicCallOp::create(
                          builder, loc,
                          mlir::StringAttr::get(builder.getContext(),
                                               "ptrauth.blend"),
                          uintptrTy,
                          mlir::ValueRange{storageAddressInt, discriminator})
                          .getResult();
    } else {
      discriminator = storageAddressInt;
    }
  }

  if (!discriminator)
    discriminator = cir::ConstantOp::create(
        builder, loc, cir::IntAttr::get(uintptrTy, 0));

  return CIRVTablePointerAuthInfo{authentication->getKey(), discriminator};
}

static mlir::Value emitVTablePointerAuthAuth(LowerModule &LM,
                                             mlir::Location loc,
                                             const CIRVTablePointerAuthInfo &info,
                                             mlir::Value pointer,
                                             mlir::OpBuilder &builder) {
  cir::IntType uintptrTy = getUIntPtrCIRTy(LM);
  cir::IntType keyTy =
      cir::IntType::get(builder.getContext(), /*width=*/32, /*isSigned=*/false);
  mlir::Type originalType = pointer.getType();

  mlir::Value pointerAsInt = cir::CastOp::create(
      builder, loc, uintptrTy, cir::CastKind::ptr_to_int, pointer);
  mlir::Value keyValue = cir::ConstantOp::create(
      builder, loc, cir::IntAttr::get(keyTy, info.key));
  mlir::Value authedPointer = cir::LLVMIntrinsicCallOp::create(
                                  builder, loc,
                                  mlir::StringAttr::get(builder.getContext(),
                                                       "ptrauth.auth"),
                                  uintptrTy,
                                  mlir::ValueRange{pointerAsInt, keyValue,
                                                   info.discriminator})
                                  .getResult();
  return cir::CastOp::create(builder, loc, originalType,
                             cir::CastKind::int_to_ptr, authedPointer);
}

class ItaniumCXXABI : public CIRCXXABI {
public:
  enum class VTableComponentLayout {
    /// Components in the vtable are pointers to other records/functions.
    Pointer,

    /// Components in the vtable are relative offsets between the vtable and the
    /// other records/functions.
    Relative,
  };

protected:
  bool UseARMMethodPtrABI;
  bool UseARMGuardVarABI;
  bool Use32BitVTableOffsetABI;
  VTableComponentLayout VTComponentLayout;
  mutable llvm::DenseMap<mlir::Region *, mlir::Block *> sharedCFIMFTrapBlocks;

public:
  ItaniumCXXABI(
      LowerModule &LM, bool UseARMMethodPtrABI = false,
      bool UseARMGuardVarABI = false,
      VTableComponentLayout VTComponentLayout = VTableComponentLayout::Pointer)
      : CIRCXXABI(LM), UseARMMethodPtrABI(UseARMMethodPtrABI),
        UseARMGuardVarABI(UseARMGuardVarABI), Use32BitVTableOffsetABI(false),
        VTComponentLayout(VTComponentLayout) {}

  bool classifyReturnType(LowerFunctionInfo &FI) const override;

  // If C++ prohibits us from making a copy, pass by invisible reference.
  RecordArgABI getRecordArgABI(const RecordType RD) const override {
    if (LM.getRecordABIFacts(RD).passIndirectly)
      return RAA_Indirect;
    return RAA_Default;
  }

  mlir::Type getDataMemberABIType() const override;

  mlir::Type getMethodABIType() const override;

  mlir::TypedAttr lowerDataMemberConstant(
      cir::DataMemberAttr attr, const mlir::DataLayout &layout,
      const mlir::TypeConverter &typeConverter) const override;

  mlir::TypedAttr
  lowerMethodConstant(cir::MethodAttr attr, const mlir::DataLayout &layout,
                      const mlir::TypeConverter &typeConverter) const override;

  mlir::Operation *
  lowerGetRuntimeMember(cir::GetRuntimeMemberOp op, mlir::Type loweredResultTy,
                        mlir::Value loweredAddr, mlir::Value loweredMember,
                        mlir::OpBuilder &builder) const override;

  void lowerGetMethod(cir::GetMethodOp op, mlir::Value (&loweredResults)[3],
                      mlir::Value loweredMethod, mlir::Value loweredObjectPtr,
                      mlir::ConversionPatternRewriter &rewriter) const override;

  mlir::Value lowerBaseDataMember(cir::BaseDataMemberOp op,
                                  mlir::Value loweredSrc,
                                  mlir::OpBuilder &builder) const override;

  mlir::Value lowerDerivedDataMember(cir::DerivedDataMemberOp op,
                                     mlir::Value loweredSrc,
                                     mlir::OpBuilder &builder) const override;

  mlir::Value lowerBaseMethod(cir::BaseMethodOp op, mlir::Value loweredSrc,
                              mlir::OpBuilder &builder) const override;

  mlir::Value lowerDerivedMethod(cir::DerivedMethodOp op,
                                 mlir::Value loweredSrc,
                                 mlir::OpBuilder &builder) const override;

  mlir::Value lowerDataMemberCmp(cir::CmpOp op, mlir::Value loweredLhs,
                                 mlir::Value loweredRhs,
                                 mlir::OpBuilder &builder) const override;

  mlir::Value lowerMethodCmp(cir::CmpOp op, mlir::Value loweredLhs,
                             mlir::Value loweredRhs,
                             mlir::OpBuilder &builder) const override;

  mlir::Value lowerDataMemberBitcast(cir::CastOp op, mlir::Type loweredDstTy,
                                     mlir::Value loweredSrc,
                                     mlir::OpBuilder &builder) const override;

  mlir::Value
  lowerDataMemberToBoolCast(cir::CastOp op, mlir::Value loweredSrc,
                            mlir::OpBuilder &builder) const override;

  mlir::Value lowerMethodBitcast(cir::CastOp op, mlir::Type loweredDstTy,
                                 mlir::Value loweredSrc,
                                 mlir::OpBuilder &builder) const override;

  mlir::Value lowerMethodToBoolCast(cir::CastOp op, mlir::Value loweredSrc,
                                    mlir::OpBuilder &builder) const override;
};

} // namespace

bool ItaniumCXXABI::classifyReturnType(LowerFunctionInfo &FI) const {
  const RecordType RD = mlir::dyn_cast<RecordType>(FI.getReturnType());
  if (!RD)
    return false;

  // If C++ prohibits us from making a copy (e.g. non-trivial destructor),
  // return by address via a hidden sret parameter.
  if (LM.getRecordABIFacts(RD).passIndirectly) {
    // getTypeAlign returns bits; convert to bytes to match OGCG's
    // CharUnits convention for ABIArgInfo::getIndirect.
    unsigned alignBytes = LM.getContext().getTypeAlign(FI.getReturnType()) / 8;
    FI.getReturnInfo() =
        cir::ABIArgInfo::getIndirect(alignBytes, /*byVal=*/false);
    return true;
  }

  return false;
}

static cir::IntType getPtrDiffCIRTy(LowerModule &lowerMod) {
  const clang::TargetInfo &target = lowerMod.getTarget();
  clang::TargetInfo::IntType ptrdiffTy =
      target.getPtrDiffType(clang::LangAS::Default);
  return cir::IntType::get(lowerMod.getMLIRContext(),
                           target.getTypeWidth(ptrdiffTy),
                           target.isTypeSigned(ptrdiffTy));
}

mlir::Type ItaniumCXXABI::getDataMemberABIType() const {
  // Itanium C++ ABI 2.3.1:
  //   A data member pointer is represented as the data member's offset in bytes
  //   from the address point of an object of the base type, as a ptrdiff_t.
  return getPtrDiffCIRTy(LM);
}

mlir::Type ItaniumCXXABI::getMethodABIType() const {
  // Itanium C++ ABI 2.3.2:
  //    In all representations, the basic ABI properties of member function
  //    pointer types are those of the following class, where fnptr_t is the
  //    appropriate function-pointer type for a member function of this type:
  //
  //    struct {
  //      fnptr_t ptr;
  //      ptrdiff_t adj;
  //    };

  cir::IntType ptrdiffCIRTy = getPtrDiffCIRTy(LM);

  // Note that clang CodeGen emits struct{ptrdiff_t, ptrdiff_t} for member
  // function pointers. Let's follow this approach.
  return cir::RecordType::get(
      ptrdiffCIRTy.getContext(), {ptrdiffCIRTy, ptrdiffCIRTy},
      /*packed=*/false, /*padded=*/false, cir::RecordType::Struct);
}

mlir::TypedAttr ItaniumCXXABI::lowerDataMemberConstant(
    cir::DataMemberAttr attr, const mlir::DataLayout &layout,
    const mlir::TypeConverter &typeConverter) const {
  uint64_t memberOffset;
  if (!attr || attr.isNullPtr()) {
    // Itanium C++ ABI 2.3:
    //   A NULL pointer is represented as -1.
    memberOffset = -1ull;
  } else if (attr.getOffsetBytes().has_value()) {
    // Pre-computed byte offset from cross-class member pointer conversion.
    memberOffset = attr.getOffsetBytes().value();
  } else {
    // Itanium C++ ABI 2.3:
    //   A pointer to data member is an offset from the base address of
    //   the class object containing it, represented as a ptrdiff_t
    auto memberIndex = attr.getMemberIndex().value();
    memberOffset =
        attr.getType().getClassTy().getElementOffset(layout, memberIndex);
  }

  mlir::Type abiTy = getDataMemberABIType();
  return cir::IntAttr::get(abiTy, memberOffset);
}

mlir::TypedAttr ItaniumCXXABI::lowerMethodConstant(
    cir::MethodAttr attr, const mlir::DataLayout &layout,
    const mlir::TypeConverter &typeConverter) const {
  cir::IntType ptrdiffCIRTy = getPtrDiffCIRTy(LM);
  auto loweredMethodTy = mlir::cast<cir::RecordType>(getMethodABIType());

  auto zero = cir::IntAttr::get(ptrdiffCIRTy, 0);
  auto thisAdjustment = attr.getThisAdjustment();

  // Itanium C++ ABI 2.3.2:
  //   In all representations, the basic ABI properties of member function
  //   pointer types are those of the following class, where fnptr_t is the
  //   appropriate function-pointer type for a member function of this type:
  //
  //   struct {
  //     fnptr_t ptr;
  //     ptrdiff_t adj;
  //   };

  if (attr.isNull()) {
    // Itanium C++ ABI 2.3.2:
    //
    //   In the standard representation, a null member function pointer is
    //   represented with ptr set to a null pointer. The value of adj is
    //   unspecified for null member function pointers.
    //
    // clang CodeGen emits struct{null, null} for null member function pointers.
    // Let's do the same here.
    return cir::ConstRecordAttr::get(
        loweredMethodTy, mlir::ArrayAttr::get(attr.getContext(), {zero, zero}));
  }

  if (attr.isVirtual()) {
    // Itanium C++ ABI 2.3.2:
    //
    //   In the standard representation, a member function pointer for a
    //   virtual function is represented with ptr set to 1 plus the function's
    //   v-table entry offset (in bytes), converted to a function pointer as if
    //   by reinterpret_cast<fnptr_t>(uintfnptr_t(1 + offset)), where
    //   uintfnptr_t is an unsigned integer of the same size as fnptr_t.
    auto ptr = cir::IntAttr::get(
        ptrdiffCIRTy,
        (UseARMMethodPtrABI ? 0 : 1) + attr.getVtableOffset().value());
    auto adj = cir::IntAttr::get(
        ptrdiffCIRTy,
        (UseARMMethodPtrABI ? 2 : 1) * thisAdjustment +
            (UseARMMethodPtrABI ? 1 : 0));
    return cir::ConstRecordAttr::get(
        loweredMethodTy, mlir::ArrayAttr::get(attr.getContext(), {ptr, adj}));
  }

  // Itanium C++ ABI 2.3.2:
  //
  //   A member function pointer for a non-virtual member function is
  //   represented with ptr set to a pointer to the function, using the base
  //   ABI's representation of function pointers.
  auto ptr = cir::GlobalViewAttr::get(ptrdiffCIRTy, attr.getSymbol().value(),
                                      /*indices=*/{}, /*inbounds=*/false,
                                      attr.getNoCFI());
  auto adj =
      cir::IntAttr::get(ptrdiffCIRTy, (UseARMMethodPtrABI ? 2 : 1) *
                                         thisAdjustment);
  return cir::ConstRecordAttr::get(
      loweredMethodTy, mlir::ArrayAttr::get(attr.getContext(), {ptr, adj}));
}

mlir::Operation *ItaniumCXXABI::lowerGetRuntimeMember(
    cir::GetRuntimeMemberOp op, mlir::Type loweredResultTy,
    mlir::Value loweredAddr, mlir::Value loweredMember,
    mlir::OpBuilder &builder) const {
  auto byteTy = IntType::get(op.getContext(), 8, true);
  auto bytePtrTy = PointerType::get(
      byteTy, mlir::cast<PointerType>(op.getAddr().getType()).getAddrSpace());
  auto objectBytesPtr = CastOp::create(builder, op.getLoc(), bytePtrTy,
                                       CastKind::bitcast, op.getAddr());
  auto memberBytesPtr = PtrStrideOp::create(builder, op.getLoc(), bytePtrTy,
                                            objectBytesPtr, loweredMember,
                                            op.getNoWrapFlags());
  return CastOp::create(builder, op.getLoc(), op.getType(), CastKind::bitcast,
                        memberBytesPtr);
}

void ItaniumCXXABI::lowerGetMethod(
    cir::GetMethodOp op, mlir::Value (&loweredResults)[3],
    mlir::Value loweredMethod, mlir::Value loweredObjectPtr,
    mlir::ConversionPatternRewriter &rewriter) const {
  // In the Itanium and ARM ABIs, method pointers have the form:
  //   struct { ptrdiff_t ptr; ptrdiff_t adj; } memptr;
  //
  // In the Itanium ABI:
  //  - method pointers are virtual if (memptr.ptr & 1) is nonzero
  //  - the this-adjustment is (memptr.adj)
  //  - the virtual offset is (memptr.ptr - 1)
  //
  // In the ARM ABI:
  //  - method pointers are virtual if (memptr.adj & 1) is nonzero
  //  - the this-adjustment is (memptr.adj >> 1)
  //  - the virtual offset is (memptr.ptr)
  // ARM uses 'adj' for the virtual flag because Thumb functions
  // may be only single-byte aligned.
  //
  // If the member is virtual, the adjusted 'this' pointer points
  // to a vtable pointer from which the virtual offset is applied.
  //
  // If the member is non-virtual, memptr.ptr is the address of
  // the function to call.

  mlir::Value &callee = loweredResults[0];
  mlir::Value &adjustedThis = loweredResults[1];
  mlir::Value &ptrauthDiscriminator = loweredResults[2];
  mlir::Type calleePtrTy = op.getCallee().getType();
  cir::IntType uintptrTy = getUIntPtrCIRTy(LM);

  cir::IntType ptrdiffCIRTy = getPtrDiffCIRTy(LM);
  mlir::Value ptrdiffOne = cir::ConstantOp::create(
      rewriter, op.getLoc(), cir::IntAttr::get(ptrdiffCIRTy, 1));

  // Ensure the member pointer is a RecordType (may be array from ABI coercion)
  auto methodRecTy = mlir::cast<cir::RecordType>(getMethodABIType());
  loweredMethod = ensureMemberPtrRecord(rewriter, op.getLoc(),
                                        loweredMethod, methodRecTy);

  mlir::Value adj = cir::ExtractMemberOp::create(
      rewriter, op.getLoc(), ptrdiffCIRTy, loweredMethod, 1);
  mlir::Value rawAdj = adj;

  // Compute the true adjustment.
  if (UseARMMethodPtrABI)
    adj = cir::ShiftOp::create(rewriter, op.getLoc(), adj.getType(), adj,
                               ptrdiffOne, /*isShiftleft=*/false);

  // Apply the adjustment to the 'this' pointer.
  mlir::Type thisVoidPtrTy =
      cir::PointerType::get(cir::VoidType::get(rewriter.getContext()),
                            op.getObject().getType().getAddrSpace());
  mlir::Value thisVoidPtr =
      cir::CastOp::create(rewriter, op.getLoc(), thisVoidPtrTy,
                          cir::CastKind::bitcast, loweredObjectPtr);
  adjustedThis = cir::PtrStrideOp::create(rewriter, op.getLoc(), thisVoidPtrTy,
                                          thisVoidPtr, adj,
      cir::GEPNoWrapFlags::inbounds);

  // Load the "ptr" field of the member function pointer and determine if it
  // points to a virtual function.
  mlir::Value methodPtrField = cir::ExtractMemberOp::create(
      rewriter, op.getLoc(), ptrdiffCIRTy, loweredMethod, 0);

  // If the LSB in the function pointer is 1, the function pointer points to
  // a virtual function. In the ARM ABI, the virtual flag is in adj instead.
  mlir::Value virtualBit;
  if (UseARMMethodPtrABI)
    virtualBit = cir::BinOp::create(rewriter, op.getLoc(), cir::BinOpKind::And,
                                    rawAdj, ptrdiffOne,
                                    {}, {}, {}, {}, {}, {});
  else
    virtualBit = cir::BinOp::create(
        rewriter, op.getLoc(), cir::BinOpKind::And, methodPtrField, ptrdiffOne,
        {}, {}, {}, {}, {}, {});
  mlir::Value isVirtual =
      cir::CmpOp::create(rewriter, op.getLoc(), cir::CmpOpKind::eq,
                          virtualBit, ptrdiffOne);

  cir_cconv_assert(!MissingFeatures::emitVFEInfo());
  cir_cconv_assert(!MissingFeatures::emitWPDInfo());

  // Read CFI attributes from the op, set by CIRGenExprCXX.
  auto virtualTypeIdAttr =
      op->getAttrOfType<mlir::StringAttr>("cir.cfi_mfcall_virtual_typeid");
  bool shouldEmitCFICheck = virtualTypeIdAttr != nullptr;
  bool cfiTrapMode = op->hasAttr("cir.cfi_mfcall_trap");
  bool cfiNoMerge = op->hasAttr("cir.cfi_mfcall_nomerge");
  bool cfiRecoverMode = op->hasAttr("cir.cfi_mfcall_recover");
  bool cfiMinimalRuntime = op->hasAttr("cir.cfi_mfcall_minimal_runtime");
  bool cfiPreserveAllRegs = op->hasAttr("cir.cfi_mfcall_preserve_all_regs");
  auto nvTypeIdsAttr =
      op->getAttrOfType<mlir::ArrayAttr>("cir.cfi_mfcall_nonvirtual_typeids");
  auto virtualCallTypeIdAttr =
      op->getAttrOfType<mlir::StringAttr>("cir.virtual.typeid");
  auto virtualCallTypeTestKindAttr =
      op->getAttrOfType<mlir::StringAttr>("cir.virtual.typetest_kind");
  bool virtualCallTypeCheckedLoad =
      op->hasAttr("cir.virtual.typecheckedload");
  auto vtablePtrAlignAttr =
      op->getAttrOfType<mlir::IntegerAttr>("cir.memptr.vtable_ptr_align");

  // Helper to get or create the __cir_type_test stub function in the module.
  // Signature: (ptr, ptr) -> bool. The second ptr arg is a placeholder that
  // amendOperation will replace with a metadata operand.
  auto getOrCreateTypeTestFn = [&]() -> cir::FuncOp {
    auto moduleOp = LM.getModule();
    constexpr llvm::StringLiteral typeTestName = "__cir_type_test";
    if (auto existing = moduleOp.lookupSymbol<cir::FuncOp>(typeTestName))
      return existing;

    auto voidPtrTy = cir::PointerType::get(
        cir::VoidType::get(rewriter.getContext()));
    auto boolTy = cir::BoolType::get(rewriter.getContext());
    auto fnTy = cir::FuncType::get({voidPtrTy, voidPtrTy}, boolTy);

    mlir::OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPointToStart(moduleOp.getBody());
    auto fn = cir::FuncOp::create(rewriter, op.getLoc(), typeTestName, fnTy);
    mlir::SymbolTable::setSymbolVisibility(
        fn, mlir::SymbolTable::Visibility::Private);
    // This helper is a temporary runtime stub whose second operand is rewritten
    // to metadata during LLVM IR translation. Skip proto-derived ABI attrs so
    // they do not leak onto the final llvm.type.test intrinsic operands.
    fn->setAttr("cir.no_proto_attrs",
                mlir::UnitAttr::get(rewriter.getContext()));
    return fn;
  };

  // Helper to emit a type test call with the given pointer and type ID string.
  // Returns the i1 result of the type test.
  auto emitTypeTestCall = [&](mlir::Value ptr,
                              llvm::StringRef typeIdStr) -> mlir::Value {
    auto typeTestFn = getOrCreateTypeTestFn();
    auto voidPtrTy = cir::PointerType::get(
        cir::VoidType::get(rewriter.getContext()));

    // Cast the pointer to void* if needed.
    mlir::Value castPtr = ptr;
    if (ptr.getType() != voidPtrTy)
      castPtr = cir::CastOp::create(rewriter, op.getLoc(), voidPtrTy,
                                     cir::CastKind::bitcast, ptr);

    // Null placeholder for the metadata operand.
    mlir::Value nullArg =
        cir::ConstantOp::create(rewriter, op.getLoc(),
                                cir::ConstPtrAttr::get(
                                    voidPtrTy,
                                    rewriter.getI64IntegerAttr(0)));

    auto callOp = cir::CallOp::create(
        rewriter, op.getLoc(),
        mlir::SymbolRefAttr::get(rewriter.getContext(),
                                 typeTestFn.getSymName()),
        cir::BoolType::get(rewriter.getContext()),
        mlir::ValueRange{castPtr, nullArg});

    // Attach the type ID string and nosanitize marker.
    callOp->setAttr("cir.type_test_id",
                     rewriter.getStringAttr(typeIdStr));
    callOp->setAttr("cir.nosanitize",
                     mlir::UnitAttr::get(rewriter.getContext()));

    return callOp.getResult();
  };

  auto getOrCreateVirtualMemPtrSlotLoadFn = [&]() -> cir::FuncOp {
    auto moduleOp = LM.getModule();
    constexpr llvm::StringLiteral slotLoadName =
        "__cir_virtual_memptr_slot_load";
    if (auto existing = moduleOp.lookupSymbol<cir::FuncOp>(slotLoadName))
      return existing;

    auto voidPtrTy = cir::PointerType::get(
        cir::VoidType::get(rewriter.getContext()));
    auto fnTy = cir::FuncType::get({voidPtrTy}, voidPtrTy);

    mlir::OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPointToStart(moduleOp.getBody());
    auto fn = cir::FuncOp::create(rewriter, op.getLoc(), slotLoadName, fnTy);
    mlir::SymbolTable::setSymbolVisibility(
        fn, mlir::SymbolTable::Visibility::Private);
    return fn;
  };

  auto getOrCreateCFICheckFailStubFn = [&]() -> cir::FuncOp {
    auto moduleOp = LM.getModule();
    constexpr llvm::StringLiteral stubName = "__cir_ubsan_cfi_check_fail";
    if (auto existing = moduleOp.lookupSymbol<cir::FuncOp>(stubName))
      return existing;

    auto fnTy =
        cir::FuncType::get({uintptrTy, uintptrTy},
                           cir::VoidType::get(rewriter.getContext()));

    mlir::OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPointToStart(moduleOp.getBody());
    auto fn = cir::FuncOp::create(rewriter, op.getLoc(), stubName, fnTy);
    mlir::SymbolTable::setSymbolVisibility(
        fn, mlir::SymbolTable::Visibility::Private);
    return fn;
  };

  auto setSanitizerBranchAttrs = [&](cir::BrCondOp brCondOp) {
    auto likelyWeight = rewriter.getI32IntegerAttr(1024 * 1024 - 1);
    auto unlikelyWeight = rewriter.getI32IntegerAttr(1);
    brCondOp->setAttr("cir.branch_weights",
                      rewriter.getArrayAttr({likelyWeight, unlikelyWeight}));
    brCondOp->setAttr("cir.nosanitize",
                      mlir::UnitAttr::get(rewriter.getContext()));
  };

  // Helper to emit a trap block with ubsantrap(2) for CFI mfcall.
  // Splits the current block: ops before the check stay in the current block,
  // a BrCondOp terminates it, and subsequent ops go into the new pass block.
  auto emitCFITrapBlock = [&](mlir::Value checkResult) {
    if (!cfiTrapMode)
      llvm_unreachable("NYI: non-trap CFI mfcall");

    bool noMergeTrap =
        !LM.getContext().getCodeGenOpts().OptimizationLevel || cfiNoMerge;

    // Split the current block at the current insertion point.
    mlir::Block *currentBlock = rewriter.getInsertionBlock();
    mlir::Block *passBlock =
        rewriter.splitBlock(currentBlock, rewriter.getInsertionPoint());

    mlir::Region *parentRegion = currentBlock->getParent();
    mlir::Block *trapBlock =
        noMergeTrap ? nullptr : sharedCFIMFTrapBlocks.lookup(parentRegion);
    if (noMergeTrap || !trapBlock) {
      // Create the trap block between current and pass blocks. When merging is
      // enabled, reuse a single trap block across all mfcall failures in the
      // current function/region, matching OGCG's per-handler trap BB reuse.
      trapBlock = rewriter.createBlock(passBlock);
      auto trapOp = cir::TrapOp::create(rewriter, op.getLoc());
      trapOp->setAttr("cir.ubsantrap_handler",
                       mlir::IntegerAttr::get(
                           mlir::IntegerType::get(rewriter.getContext(), 8),
                           2));
      trapOp->setAttr("cir.nosanitize",
                       mlir::UnitAttr::get(rewriter.getContext()));
      if (noMergeTrap) {
        trapOp->setAttr("cir.nomerge",
                        mlir::UnitAttr::get(rewriter.getContext()));
      } else {
        trapOp->setAttr("cir.ubsantrap_allow_merge",
                        mlir::UnitAttr::get(rewriter.getContext()));
        sharedCFIMFTrapBlocks[parentRegion] = trapBlock;
      }
    }

    // Terminate the current block with a conditional branch.
    rewriter.setInsertionPointToEnd(currentBlock);
    auto brCond = cir::BrCondOp::create(rewriter, op.getLoc(), checkResult,
                                        passBlock, trapBlock);
    setSanitizerBranchAttrs(brCond);

    // Continue inserting into the pass block.
    rewriter.setInsertionPointToStart(passBlock);
  };

  auto emitCFIFailBlock = [&](mlir::Value checkResult, mlir::Value checkedValue,
                              mlir::Value validVtable,
                              uint8_t typeCheckKind) {
    if (cfiTrapMode) {
      emitCFITrapBlock(checkResult);
      return;
    }

    llvm::StringRef handlerKind = cfiRecoverMode ? "recover" : "abort";
    bool noMergeHandler =
        LM.getContext().getCodeGenOpts().OptimizationLevel == 0 || cfiNoMerge;

    mlir::Block *currentBlock = rewriter.getInsertionBlock();
    mlir::Block *passBlock =
        rewriter.splitBlock(currentBlock, rewriter.getInsertionPoint());
    mlir::Block *failBlock = rewriter.createBlock(passBlock);

    rewriter.setInsertionPointToEnd(currentBlock);
    auto brCond = cir::BrCondOp::create(rewriter, op.getLoc(), checkResult,
                                        passBlock, failBlock);
    setSanitizerBranchAttrs(brCond);

    rewriter.setInsertionPointToStart(failBlock);
    auto stubFn = getOrCreateCFICheckFailStubFn();
    mlir::Value checkedValueInt = cir::CastOp::create(
        rewriter, op.getLoc(), uintptrTy, cir::CastKind::ptr_to_int,
        checkedValue);
    mlir::Value validVtableInt = cir::ConstantOp::create(
        rewriter, op.getLoc(), cir::IntAttr::get(uintptrTy, 0));
    if (validVtable)
      validVtableInt = cir::CastOp::create(
          rewriter, op.getLoc(), uintptrTy, cir::CastKind::bool_to_int,
          validVtable);

    auto callOp = cir::CallOp::create(
        rewriter, op.getLoc(),
        mlir::SymbolRefAttr::get(rewriter.getContext(), stubFn.getSymName()),
        cir::VoidType::get(rewriter.getContext()),
        mlir::ValueRange{checkedValueInt, validVtableInt});
    callOp->setAttr("cir.ubsan.handler_name",
                    rewriter.getStringAttr("cfi_check_fail"));
    callOp->setAttr("cir.ubsan.handler_kind",
                    rewriter.getStringAttr(handlerKind));
    callOp->setAttr("cir.ubsan.type_check_kind",
                    rewriter.getI8IntegerAttr(typeCheckKind));
    callOp->setAttr("cir.nosanitize",
                    mlir::UnitAttr::get(rewriter.getContext()));
    if (noMergeHandler)
      callOp->setAttr("cir.call_nomerge",
                      mlir::UnitAttr::get(rewriter.getContext()));
    if (cfiMinimalRuntime)
      callOp->setAttr("cir.cfi.mfcall.minimal_runtime",
                      mlir::UnitAttr::get(rewriter.getContext()));
    if (cfiPreserveAllRegs)
      callOp->setAttr("cir.cfi.mfcall.preserve_all_regs",
                      mlir::UnitAttr::get(rewriter.getContext()));

    for (llvm::StringRef attrName :
         {"cir.ubsan.source_file", "cir.ubsan.source_line",
          "cir.ubsan.source_column", "cir.ubsan.type_name",
          "cir.ubsan.type_kind", "cir.ubsan.type_info"}) {
      if (mlir::Attribute attr = op->getAttr(attrName))
        callOp->setAttr(attrName, attr);
    }

    if (handlerKind == "abort")
      cir::UnreachableOp::create(rewriter, op.getLoc());
    else
      cir::BrOp::create(rewriter, op.getLoc(), passBlock);

    rewriter.setInsertionPointToStart(passBlock);
  };

  mlir::Block *currBlock = rewriter.getInsertionBlock();
  mlir::Block *continueBlock =
      rewriter.splitBlock(currBlock, rewriter.getInsertionPoint());
  continueBlock->addArgument(calleePtrTy, op.getLoc());
  continueBlock->addArgument(uintptrTy, op.getLoc());

  mlir::Block *virtualBlock = rewriter.createBlock(continueBlock);
  mlir::Block *nonVirtualBlock = rewriter.createBlock(continueBlock);
  rewriter.setInsertionPointToEnd(currBlock);
  cir::BrCondOp::create(rewriter, op.getLoc(), isVirtual, virtualBlock,
                        nonVirtualBlock);

  auto buildVirtualBranch = [&] {
    mlir::OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPointToStart(virtualBlock);

    // Load vtable pointer.
    // Note that vtable pointer always point to the global address space.
    auto vtablePtrTy = cir::PointerType::get(
        cir::IntType::get(rewriter.getContext(), 8, true));
    auto vtablePtrPtrTy = cir::PointerType::get(
        vtablePtrTy, op.getObject().getType().getAddrSpace());
    auto vtablePtrPtr =
        cir::CastOp::create(rewriter, op.getLoc(), vtablePtrPtrTy,
                            cir::CastKind::bitcast, adjustedThis);
    auto vtablePtrLoad = cir::LoadOp::create(
        rewriter, op.getLoc(), vtablePtrPtr, /*isDeref=*/false,
        /*isVolatile=*/false,
        /*is_nontemporal=*/false,
        /*alignment=*/vtablePtrAlignAttr,
        /*sync_scope=*/cir::SyncScopeKindAttr{},
        /*mem_order=*/cir::MemOrderAttr(),
        /*tbaa=*/{});
    if (LM.getContext().getCodeGenOpts().OptimizationLevel > 0 &&
        !LM.getContext().getCodeGenOpts().RelaxedAliasing) {
      auto ptrSize = LM.getDataLayout().getPointerTypeSizeInBits(vtablePtrTy) / 8;
      auto tbaa = cir::TBAAVTablePointerAttr::get(rewriter.getContext(),
                                                  vtablePtrTy, ptrSize);
      vtablePtrLoad.setTbaaAttr(
          cir::TBAATagAttr::get(rewriter.getContext(), tbaa, tbaa, 0,
                                ptrSize));
    }
    mlir::Value vtablePtr = vtablePtrLoad;
    if (auto recordAst = op.getMethod().getType().getClassTy().getAst())
      if (auto authInfo = getVTablePointerAuthInfo(
              LM, op.getLoc(),
              llvm::cast<clang::CXXRecordDecl>(recordAst.getRawDecl()),
              adjustedThis, rewriter))
        vtablePtr = emitVTablePointerAuthAuth(LM, op.getLoc(), *authInfo,
                                              vtablePtr, rewriter);

    // Get the vtable offset.
    mlir::Value vtableOffset = methodPtrField;
    if (!UseARMMethodPtrABI)
      vtableOffset = cir::BinOp::create(
          rewriter, op.getLoc(), cir::BinOpKind::Sub, vtableOffset, ptrdiffOne,
          {}, {}, {}, {}, {}, {});
    if (Use32BitVTableOffsetABI)
      llvm_unreachable("NYI");

    // Apply the offset to the vtable pointer and get the pointer to the target
    // virtual function. Then load that pointer to get the callee.
    mlir::Value funcPtr;
    mlir::Value vfpAddr;
    auto getVFuncAddr = [&]() -> mlir::Value {
      if (vfpAddr)
        return vfpAddr;

      // Compute the virtual function pointer address for both the CFI
      // type test and the load.
      auto vfpStrideOp = cir::PtrStrideOp::create(
          rewriter, op.getLoc(), vtablePtrTy, vtablePtr, vtableOffset,
          cir::GEPNoWrapFlags::none);
      // OGCG wraps this entire block in a SanitizerDebugLocation scope
      // (ItaniumCXXABI.cpp:735) which causes all instructions to get
      // !nosanitize metadata via InsertHelper. Mark the GEP equivalent.
      vfpStrideOp->setAttr("cir.nosanitize",
                           mlir::UnitAttr::get(rewriter.getContext()));
      vfpAddr = vfpStrideOp;
      return vfpAddr;
    };

    // Emit CFI type test on the virtual function pointer address.
    if (shouldEmitCFICheck) {
      mlir::Value checkResult =
          emitTypeTestCall(getVFuncAddr(), virtualTypeIdAttr.getValue());
      mlir::Value validVtable;
      if (!cfiTrapMode)
        validVtable = emitTypeTestCall(vtablePtr, "all-vtables");
      emitCFIFailBlock(checkResult, vtablePtr, validVtable, 6);
    }

    if (VTComponentLayout == VTableComponentLayout::Relative) {
      auto loadRelativeOp = cir::LLVMIntrinsicCallOp::create(
          rewriter, op.getLoc(),
          mlir::StringAttr::get(rewriter.getContext(), "load.relative"),
          calleePtrTy, mlir::ValueRange{vtablePtr, vtableOffset});
      loadRelativeOp->setAttr("cir.nosanitize",
                              mlir::UnitAttr::get(rewriter.getContext()));
      loadRelativeOp->setAttr("tail",
                              mlir::UnitAttr::get(rewriter.getContext()));
      funcPtr = loadRelativeOp.getResult();
    } else {
      // OGCG uses CreateGEP (no inbounds) for the vtable virtual function
      // address GEP in member function pointer dispatch.
      if (virtualCallTypeIdAttr) {
        auto slotLoadFn = getOrCreateVirtualMemPtrSlotLoadFn();
        auto voidPtrTy = cir::PointerType::get(
            cir::VoidType::get(rewriter.getContext()));
        mlir::Value slotAddr = getVFuncAddr();
        if (slotAddr.getType() != voidPtrTy)
          slotAddr = cir::CastOp::create(rewriter, op.getLoc(), voidPtrTy,
                                         cir::CastKind::bitcast, slotAddr);
        auto callOp = cir::CallOp::create(
            rewriter, op.getLoc(),
            mlir::SymbolRefAttr::get(rewriter.getContext(),
                                     slotLoadFn.getSymName()),
            voidPtrTy, mlir::ValueRange{slotAddr});
        callOp->setAttr("cir.virtual.typeid", virtualCallTypeIdAttr);
        if (virtualCallTypeCheckedLoad)
          callOp->setAttr("cir.virtual.typecheckedload",
                          mlir::UnitAttr::get(rewriter.getContext()));
        if (virtualCallTypeTestKindAttr)
          callOp->setAttr("cir.virtual.typetest_kind",
                          virtualCallTypeTestKindAttr);
        funcPtr = cir::CastOp::create(rewriter, op.getLoc(), calleePtrTy,
                                      cir::CastKind::bitcast,
                                      callOp.getResult());
      } else {
        auto vfpPtrTy = cir::PointerType::get(calleePtrTy);
        mlir::Value vfpPtr = cir::CastOp::create(
            rewriter, op.getLoc(), vfpPtrTy, cir::CastKind::bitcast,
            getVFuncAddr());
        auto loadOp = cir::LoadOp::create(rewriter, op.getLoc(), vfpPtr,
                                      /*isDeref=*/false, /*isVolatile=*/false,
                                      /*is_nontemporal=*/false,
                                      /*alignment=*/mlir::IntegerAttr(),
                                      /*sync_scope=*/cir::SyncScopeKindAttr{},
                                      /*mem_order=*/cir::MemOrderAttr(),
                                      /*tbaa=*/{});
        // Mark the vtable function pointer load with nosanitize,
        // matching OGCG's SanitizerDebugLocation scope.
        loadOp->setAttr("cir.nosanitize",
                        mlir::UnitAttr::get(rewriter.getContext()));
        funcPtr = loadOp;
      }
    }

    mlir::Value zeroDiscriminator = cir::ConstantOp::create(
        rewriter, op.getLoc(), cir::IntAttr::get(uintptrTy, 0));
    cir::BrOp::create(rewriter, op.getLoc(), continueBlock,
                      mlir::ValueRange{funcPtr, zeroDiscriminator});
  };

  auto buildNonVirtualBranch = [&] {
    mlir::OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPointToStart(nonVirtualBlock);

    mlir::Value funcPtr =
        cir::CastOp::create(rewriter, op.getLoc(), calleePtrTy,
                            cir::CastKind::int_to_ptr, methodPtrField);
    mlir::Value nonVirtualDiscriminator = cir::ConstantOp::create(
        rewriter, op.getLoc(), cir::IntAttr::get(uintptrTy, 0));
    if (auto discAttr = op->getAttrOfType<mlir::IntegerAttr>(
            "cir.memptr.nonvirtual_ptrauth_discriminator"))
      nonVirtualDiscriminator = cir::ConstantOp::create(
          rewriter, op.getLoc(),
          cir::IntAttr::get(uintptrTy, discAttr.getValue().getZExtValue()));

    // Emit CFI type tests for non-virtual member function pointers.
    // Test against each most-base class's member pointer type and OR
    // the results. Matches OGCG ItaniumCXXABI.cpp:840-852.
    if (shouldEmitCFICheck && nvTypeIdsAttr) {
      auto boolTy = cir::BoolType::get(rewriter.getContext());
      mlir::Value orResult = cir::ConstantOp::create(
          rewriter, op.getLoc(),
          cir::BoolAttr::get(rewriter.getContext(), boolTy, false));

      for (auto attr : nvTypeIdsAttr) {
        auto typeIdStr = mlir::cast<mlir::StringAttr>(attr).getValue();
        mlir::Value typeTestResult = emitTypeTestCall(funcPtr, typeIdStr);
        orResult = cir::BinOp::create(rewriter, op.getLoc(),
                                       cir::BinOpKind::Or, orResult,
                                       typeTestResult,
                                       false, false, false, {}, {}, {});
      }

      emitCFIFailBlock(orResult, funcPtr, {}, 5);
    }

    cir::BrOp::create(rewriter, op.getLoc(), continueBlock,
                      mlir::ValueRange{funcPtr, nonVirtualDiscriminator});
  };

  buildVirtualBranch();
  buildNonVirtualBranch();

  rewriter.setInsertionPointToStart(continueBlock);
  callee = continueBlock->getArgument(0);
  ptrauthDiscriminator = continueBlock->getArgument(1);
}

static mlir::Value lowerDataMemberCast(mlir::Operation *op,
                                       mlir::Value loweredSrc,
                                       std::int64_t offset,
                                       bool isDerivedToBase,
                                       mlir::OpBuilder &builder) {
  if (offset == 0)
    return loweredSrc;

  auto nullValue = cir::ConstantOp::create(
      builder, op->getLoc(), cir::IntAttr::get(loweredSrc.getType(), -1));
  auto isNull = cir::CmpOp::create(builder, op->getLoc(), cir::CmpOpKind::eq,
                                   loweredSrc, nullValue);

  auto offsetValue = cir::ConstantOp::create(
      builder, op->getLoc(), cir::IntAttr::get(loweredSrc.getType(), offset));
  auto binOpKind = isDerivedToBase ? cir::BinOpKind::Sub : cir::BinOpKind::Add;
  // Matches OGCG's CreateNSWAdd/CreateNSWSub in EmitMemberPointerConversion.
  auto adjustedPtr =
      cir::BinOp::create(builder, op->getLoc(), loweredSrc.getType(), binOpKind,
                         loweredSrc, offsetValue, /*no_unsigned_wrap=*/false,
                         /*no_signed_wrap=*/true, /*saturated=*/false,
                         /*fastMathFlags=*/{}, /*fpExceptionBehavior=*/{},
                         /*roundingMode=*/{});

  return cir::SelectOp::create(builder, op->getLoc(), loweredSrc.getType(),
                               isNull, nullValue, adjustedPtr);
}

static mlir::Value lowerMethodCast(mlir::Operation *op, mlir::Value loweredSrc,
                                   std::int64_t offset, bool isDerivedToBase,
                                   LowerModule &lowerMod,
                                   bool useARMMethodPtrABI,
                                   mlir::OpBuilder &builder) {
  auto srcDiscAttr =
      op->getAttrOfType<mlir::IntegerAttr>("cir.memptr.ptrauth_src_discriminator");
  auto dstDiscAttr =
      op->getAttrOfType<mlir::IntegerAttr>("cir.memptr.ptrauth_dst_discriminator");
  if (offset == 0 && (!useARMMethodPtrABI || (!srcDiscAttr && !dstDiscAttr)))
    return loweredSrc;

  cir::IntType ptrdiffCIRTy = getPtrDiffCIRTy(lowerMod);
  // Build expected member pointer record type {ptrdiff_t, ptrdiff_t}
  auto methodRecTy = cir::RecordType::get(
      op->getContext(), {ptrdiffCIRTy, ptrdiffCIRTy}, /*packed=*/false,
      /*padded=*/false, cir::RecordType::Struct);
  loweredSrc = ensureMemberPtrRecord(builder, op->getLoc(),
                                     loweredSrc, methodRecTy);
  auto ptrField = cir::ExtractMemberOp::create(builder, op->getLoc(),
                                               ptrdiffCIRTy, loweredSrc, 0);
  auto adjField = cir::ExtractMemberOp::create(builder, op->getLoc(),
                                               ptrdiffCIRTy, loweredSrc, 1);
  mlir::Value result = loweredSrc;
  mlir::Value adjustedAdjField = adjField;

  // The this-adjustment is left-shifted by 1 under the ARM method-pointer ABI,
  // where the virtual flag is bit 0 of the adjustment (not the function
  // pointer). Only the magnitude is scaled; the direction is handled by the
  // Add-vs-Sub choice below. Matches classic
  // ItaniumCXXABI::EmitMemberPointerConversion.
  std::int64_t adjOffset = useARMMethodPtrABI ? offset << 1 : offset;

  auto buildConstantMethodRecord =
      [&](mlir::TypedAttr ptrAttr,
          llvm::APInt adjValue) -> cir::ConstantOp {
    auto recordAttr = cir::ConstRecordAttr::get(
        methodRecTy, mlir::ArrayAttr::get(op->getContext(),
                                          {ptrAttr, cir::IntAttr::get(
                                                        ptrdiffCIRTy,
                                                        adjValue)}));
    return cir::ConstantOp::create(builder, op->getLoc(), recordAttr);
  };

  auto rewritePtrauthPayload =
      [&](cir::ConstantOp constOp,
          mlir::IntegerAttr newDiscAttr) -> mlir::ArrayAttr {
    auto payload =
        constOp->getAttrOfType<mlir::ArrayAttr>("cir.member_func_ptr_ptrauth");
    if (!payload || !newDiscAttr)
      return payload;

    llvm::SmallVector<mlir::Attribute> rewrittenEntries;
    rewrittenEntries.reserve(payload.size());
    for (mlir::Attribute entryAttr : payload) {
      auto entry = mlir::dyn_cast<mlir::ArrayAttr>(entryAttr);
      if (!entry || entry.empty()) {
        rewrittenEntries.push_back(entryAttr);
        continue;
      }

      llvm::SmallVector<mlir::Attribute> fields(entry.begin(), entry.end());
      auto oldDiscAttr = mlir::dyn_cast<mlir::IntegerAttr>(fields.back());
      if (!oldDiscAttr) {
        rewrittenEntries.push_back(entryAttr);
        continue;
      }
      fields.back() = mlir::IntegerAttr::get(oldDiscAttr.getType(),
                                             newDiscAttr.getValue());
      rewrittenEntries.push_back(
          mlir::ArrayAttr::get(op->getContext(), fields));
    }
    return mlir::ArrayAttr::get(op->getContext(), rewrittenEntries);
  };

  auto maybeFoldConstantMethodCast = [&]() -> mlir::Value {
    auto constOp = loweredSrc.getDefiningOp<cir::ConstantOp>();
    if (!constOp)
      return {};

    auto recordAttr = mlir::dyn_cast<cir::ConstRecordAttr>(constOp.getValue());
    if (!recordAttr || recordAttr.getMembers().size() != 2)
      return {};

    auto ptrAttr = mlir::dyn_cast<mlir::TypedAttr>(recordAttr.getMembers()[0]);
    auto adjAttr = mlir::dyn_cast<cir::IntAttr>(recordAttr.getMembers()[1]);
    if (!ptrAttr || !adjAttr)
      return {};

    llvm::APInt adjValue = adjAttr.getValue();
    if (offset != 0) {
      llvm::APInt offsetValue(adjValue.getBitWidth(),
                              static_cast<uint64_t>(adjOffset),
                              /*isSigned=*/true);
      adjValue = isDerivedToBase ? adjValue - offsetValue : adjValue + offsetValue;
    }

    auto ptrIntAttr = mlir::dyn_cast<cir::IntAttr>(ptrAttr);
    bool isNullMemberPtr = ptrIntAttr && ptrIntAttr.getValue().isZero();
    // Under the ARM method-pointer ABI, bit 0 of the adjustment is the virtual
    // flag (not bit 0 of the function pointer). A virtual member whose vtable
    // offset is 0 therefore also has a zero function-pointer field, but it is
    // NOT the null member pointer -- do not fold it to null.
    if (useARMMethodPtrABI && isNullMemberPtr && adjAttr.getValue()[0])
      isNullMemberPtr = false;
    if (isNullMemberPtr) {
      return buildConstantMethodRecord(cir::IntAttr::get(ptrdiffCIRTy, 0),
                                       llvm::APInt(adjValue.getBitWidth(), 0,
                                                   /*isSigned=*/true));
    }

    if (!useARMMethodPtrABI || (!srcDiscAttr && !dstDiscAttr))
      return {};

    mlir::ArrayAttr payload =
        rewritePtrauthPayload(constOp, dstDiscAttr ? dstDiscAttr : srcDiscAttr);
    if (!payload)
      return {};

    auto newConst = buildConstantMethodRecord(ptrAttr, adjValue);
    newConst->setAttr("cir.member_func_ptr_ptrauth", payload);
    return newConst;
  };

  if (mlir::Value folded = maybeFoldConstantMethodCast())
    return folded;

  if (offset != 0) {
    auto offsetValue = cir::ConstantOp::create(
        builder, op->getLoc(), cir::IntAttr::get(ptrdiffCIRTy, adjOffset));
    auto binOpKind =
        isDerivedToBase ? cir::BinOpKind::Sub : cir::BinOpKind::Add;
    // Matches OGCG's CreateNSWAdd/CreateNSWSub in EmitMemberPointerConversion.
    adjustedAdjField = cir::BinOp::create(
        builder, op->getLoc(), ptrdiffCIRTy, binOpKind, adjField, offsetValue,
        /*no_unsigned_wrap=*/false, /*no_signed_wrap=*/true,
        /*saturated=*/false, /*fastMathFlags=*/{}, /*fpExceptionBehavior=*/{},
        /*roundingMode=*/{});
    result = cir::InsertMemberOp::create(builder, op->getLoc(), loweredSrc,
                                         builder.getI64IntegerAttr(1),
                                         adjustedAdjField);
  }

  if (!useARMMethodPtrABI)
    return result;

  if (!srcDiscAttr && !dstDiscAttr)
    return result;

  const auto &schema =
      lowerMod.getContext().getCodeGenOpts().PointerAuth.CXXMemberFunctionPointers;
  auto authMode = schema.getAuthenticationMode();
  if (authMode == clang::PointerAuthenticationMode::None)
    return result;

  mlir::Value ptrdiffOne = cir::ConstantOp::create(
      builder, op->getLoc(), cir::IntAttr::get(ptrdiffCIRTy, 1));
  mlir::Value virtualBit = cir::BinOp::create(builder, op->getLoc(),
                                              cir::BinOpKind::And,
                                              adjustedAdjField, ptrdiffOne, {},
                                              {}, {}, {}, {}, {});
  mlir::Value isNonVirtual = cir::CmpOp::create(
      builder, op->getLoc(), cir::CmpOpKind::ne, virtualBit, ptrdiffOne);

  mlir::Value ptrAsPtr = cir::CastOp::create(
      builder, op->getLoc(),
      cir::PointerType::get(cir::VoidType::get(builder.getContext())),
      cir::CastKind::int_to_ptr, ptrField);

  auto buildAuthInfo = [&](mlir::IntegerAttr discAttr) {
    CIRVTablePointerAuthInfo info{};
    info.key = schema.getKey();
    info.discriminator = cir::ConstantOp::create(
        builder, op->getLoc(),
        cir::IntAttr::get(getUIntPtrCIRTy(lowerMod),
                          discAttr ? discAttr.getValue().getZExtValue() : 0));
    return info;
  };

  std::optional<CIRVTablePointerAuthInfo> srcAuthInfo;
  std::optional<CIRVTablePointerAuthInfo> dstAuthInfo;
  if (srcDiscAttr)
    srcAuthInfo = buildAuthInfo(srcDiscAttr);
  if (dstDiscAttr)
    dstAuthInfo = buildAuthInfo(dstDiscAttr);

  auto emitPointerAuthSign = [&](const CIRVTablePointerAuthInfo &authInfo,
                                 mlir::Value pointer) -> mlir::Value {
    if (authMode == clang::PointerAuthenticationMode::None ||
        authMode == clang::PointerAuthenticationMode::Strip)
      return pointer;

    mlir::Value signedPointer = cir::LLVMIntrinsicCallOp::create(
                                    builder, op->getLoc(),
                                    mlir::StringAttr::get(builder.getContext(),
                                                         "ptrauth.sign"),
                                    getUIntPtrCIRTy(lowerMod),
                                    mlir::ValueRange{
                                        cir::CastOp::create(
                                            builder, op->getLoc(),
                                            getUIntPtrCIRTy(lowerMod),
                                            cir::CastKind::ptr_to_int, pointer),
                                        cir::ConstantOp::create(
                                            builder, op->getLoc(),
                                            cir::IntAttr::get(
                                                cir::IntType::get(
                                                    builder.getContext(), 32,
                                                    /*isSigned=*/false),
                                                authInfo.key)),
                                        authInfo.discriminator})
                                    .getResult();
    return cir::CastOp::create(builder, op->getLoc(), pointer.getType(),
                               cir::CastKind::int_to_ptr, signedPointer);
  };

  auto emitPointerAuthAuth = [&](const CIRVTablePointerAuthInfo &authInfo,
                                 mlir::Value pointer) -> mlir::Value {
    mlir::StringAttr intrinsicName;
    mlir::ValueRange intrinsicArgs;
    mlir::Value pointerAsInt = cir::CastOp::create(
        builder, op->getLoc(), getUIntPtrCIRTy(lowerMod),
        cir::CastKind::ptr_to_int, pointer);
    mlir::Value keyValue = cir::ConstantOp::create(
        builder, op->getLoc(),
        cir::IntAttr::get(
            cir::IntType::get(builder.getContext(), 32, /*isSigned=*/false),
            authInfo.key));

    if (authMode == clang::PointerAuthenticationMode::Strip) {
      intrinsicName = mlir::StringAttr::get(builder.getContext(), "ptrauth.strip");
      intrinsicArgs = mlir::ValueRange{pointerAsInt, keyValue};
    } else {
      if (authMode != clang::PointerAuthenticationMode::SignAndAuth)
        return pointer;
      intrinsicName = mlir::StringAttr::get(builder.getContext(), "ptrauth.auth");
      intrinsicArgs =
          mlir::ValueRange{pointerAsInt, keyValue, authInfo.discriminator};
    }

    mlir::Value authedPointer = cir::LLVMIntrinsicCallOp::create(
                                    builder, op->getLoc(), intrinsicName,
                                    getUIntPtrCIRTy(lowerMod), intrinsicArgs)
                                    .getResult();
    return cir::CastOp::create(builder, op->getLoc(), pointer.getType(),
                               cir::CastKind::int_to_ptr, authedPointer);
  };

  auto applyResign = [&]() -> mlir::Value {
    if (srcAuthInfo && dstAuthInfo) {
      if (authMode == clang::PointerAuthenticationMode::SignAndAuth) {
        mlir::Value resigned = cir::LLVMIntrinsicCallOp::create(
                                   builder, op->getLoc(),
                                   mlir::StringAttr::get(builder.getContext(),
                                                        "ptrauth.resign"),
                                   getUIntPtrCIRTy(lowerMod),
                                   mlir::ValueRange{
                                       cir::CastOp::create(
                                           builder, op->getLoc(),
                                           getUIntPtrCIRTy(lowerMod),
                                           cir::CastKind::ptr_to_int, ptrAsPtr),
                                       cir::ConstantOp::create(
                                           builder, op->getLoc(),
                                           cir::IntAttr::get(
                                               cir::IntType::get(
                                                   builder.getContext(), 32,
                                                   /*isSigned=*/false),
                                               srcAuthInfo->key)),
                                       srcAuthInfo->discriminator,
                                       cir::ConstantOp::create(
                                           builder, op->getLoc(),
                                           cir::IntAttr::get(
                                               cir::IntType::get(
                                                   builder.getContext(), 32,
                                                   /*isSigned=*/false),
                                               dstAuthInfo->key)),
                                       dstAuthInfo->discriminator})
                                   .getResult();
        return cir::CastOp::create(builder, op->getLoc(), ptrAsPtr.getType(),
                                   cir::CastKind::int_to_ptr, resigned);
      }
      return emitPointerAuthSign(*dstAuthInfo,
                                 emitPointerAuthAuth(*srcAuthInfo, ptrAsPtr));
    }
    if (dstAuthInfo)
      return emitPointerAuthSign(*dstAuthInfo, ptrAsPtr);
    assert(srcAuthInfo && "expected source auth info");
    return emitPointerAuthAuth(*srcAuthInfo, ptrAsPtr);
  };

  mlir::Value nullValue = cir::ConstantOp::create(
      builder, op->getLoc(), cir::IntAttr::get(ptrdiffCIRTy, 0));
  mlir::Value isNonNull = cir::CmpOp::create(builder, op->getLoc(),
                                             cir::CmpOpKind::ne, ptrField,
                                             nullValue);
  CIRBaseBuilderTy cirBuilder(builder);
  mlir::Value falseValue = cirBuilder.getFalse(op->getLoc());
  mlir::Value shouldResign = cir::SelectOp::create(
      builder, op->getLoc(), isNonVirtual, isNonNull, falseValue);
  mlir::Value resignedPtr = cir::TernaryOp::create(
                                builder, op->getLoc(), shouldResign,
                                [&](mlir::OpBuilder &b, mlir::Location loc) {
                                  mlir::Value resignedPtrValue = applyResign();
                                  mlir::Value resignedInt = cir::CastOp::create(
                                      b, loc, getUIntPtrCIRTy(lowerMod),
                                      cir::CastKind::ptr_to_int,
                                      resignedPtrValue);
                                  cir::YieldOp::create(
                                      b, loc,
                                      mlir::ValueRange{cir::CastOp::create(
                                          b, loc, ptrField.getType(),
                                          cir::CastKind::integral,
                                          resignedInt)});
                                },
                                [&](mlir::OpBuilder &b, mlir::Location loc) {
                                  cir::YieldOp::create(b, loc,
                                                       mlir::ValueRange{ptrField});
                                })
                                .getResult();
  return cir::InsertMemberOp::create(builder, op->getLoc(), result,
                                     builder.getI64IntegerAttr(0), resignedPtr);
}

mlir::Value ItaniumCXXABI::lowerBaseDataMember(cir::BaseDataMemberOp op,
                                               mlir::Value loweredSrc,
                                               mlir::OpBuilder &builder) const {
  return lowerDataMemberCast(op, loweredSrc, op.getOffset().getSExtValue(),
                             /*isDerivedToBase=*/true, builder);
}

mlir::Value
ItaniumCXXABI::lowerDerivedDataMember(cir::DerivedDataMemberOp op,
                                      mlir::Value loweredSrc,
                                      mlir::OpBuilder &builder) const {
  return lowerDataMemberCast(op, loweredSrc, op.getOffset().getSExtValue(),
                             /*isDerivedToBase=*/false, builder);
}

mlir::Value ItaniumCXXABI::lowerBaseMethod(cir::BaseMethodOp op,
                                           mlir::Value loweredSrc,
                                           mlir::OpBuilder &builder) const {
  return lowerMethodCast(op, loweredSrc, op.getOffset().getSExtValue(),
                         /*isDerivedToBase=*/true, LM, UseARMMethodPtrABI,
                         builder);
}

mlir::Value ItaniumCXXABI::lowerDerivedMethod(cir::DerivedMethodOp op,
                                              mlir::Value loweredSrc,
                                              mlir::OpBuilder &builder) const {
  return lowerMethodCast(op, loweredSrc, op.getOffset().getSExtValue(),
                         /*isDerivedToBase=*/false, LM, UseARMMethodPtrABI,
                         builder);
}

mlir::Value ItaniumCXXABI::lowerDataMemberCmp(cir::CmpOp op,
                                              mlir::Value loweredLhs,
                                              mlir::Value loweredRhs,
                                              mlir::OpBuilder &builder) const {
  return cir::CmpOp::create(builder, op.getLoc(), op.getKind(), loweredLhs,
                            loweredRhs);
}

mlir::Value ItaniumCXXABI::lowerMethodCmp(cir::CmpOp op, mlir::Value loweredLhs,
                                          mlir::Value loweredRhs,
                                          mlir::OpBuilder &builder) const {
  cir_cconv_assert(op.getKind() == cir::CmpOpKind::eq ||
                   op.getKind() == cir::CmpOpKind::ne);

  cir::IntType ptrdiffCIRTy = getPtrDiffCIRTy(LM);
  mlir::Value ptrdiffZero = cir::ConstantOp::create(
      builder, op.getLoc(), cir::IntAttr::get(ptrdiffCIRTy, 0));

  auto methodRecTy = mlir::cast<cir::RecordType>(getMethodABIType());
  loweredLhs = ensureMemberPtrRecord(builder, op.getLoc(),
                                     loweredLhs, methodRecTy);
  loweredRhs = ensureMemberPtrRecord(builder, op.getLoc(),
                                     loweredRhs, methodRecTy);

  mlir::Value lhsPtrField = cir::ExtractMemberOp::create(
      builder, op.getLoc(), ptrdiffCIRTy, loweredLhs, 0);
  mlir::Value rhsPtrField = cir::ExtractMemberOp::create(
      builder, op.getLoc(), ptrdiffCIRTy, loweredRhs, 0);
  mlir::Value ptrCmp = cir::CmpOp::create(builder, op.getLoc(), op.getKind(),
                                          lhsPtrField, rhsPtrField);
  mlir::Value ptrCmpToNull = cir::CmpOp::create(
      builder, op.getLoc(), op.getKind(), lhsPtrField, ptrdiffZero);

  mlir::Value lhsAdjField = cir::ExtractMemberOp::create(
      builder, op.getLoc(), ptrdiffCIRTy, loweredLhs, 1);
  mlir::Value rhsAdjField = cir::ExtractMemberOp::create(
      builder, op.getLoc(), ptrdiffCIRTy, loweredRhs, 1);
  mlir::Value adjCmp = cir::CmpOp::create(builder, op.getLoc(), op.getKind(),
                                          lhsAdjField, rhsAdjField);

  // We use cir.select to represent "||" and "&&" operations below:
  //   - cir.select if %a then %b else false => %a && %b
  //   - cir.select if %a then true else %b  => %a || %b
  // TODO: Do we need to invent dedicated "cir.logical_or" and "cir.logical_and"
  // operations for this?
  CIRBaseBuilderTy cirBuilder(builder);
  mlir::Value trueValue = cirBuilder.getTrue(op.getLoc());
  mlir::Value falseValue = cirBuilder.getFalse(op.getLoc());
  auto create_and = [&](mlir::Value lhs, mlir::Value rhs) {
    return cir::SelectOp::create(builder, op.getLoc(), lhs, rhs, falseValue);
  };
  auto create_or = [&](mlir::Value lhs, mlir::Value rhs) {
    return cir::SelectOp::create(builder, op.getLoc(), lhs, trueValue, rhs);
  };

  // On ARM, null member function pointers additionally require that the low
  // virtual bits of both adj fields are clear.
  if (UseARMMethodPtrABI) {
    mlir::Value ptrdiffOne = cir::ConstantOp::create(
        builder, op.getLoc(), cir::IntAttr::get(ptrdiffCIRTy, 1));
    mlir::Value orAdj = cir::BinOp::create(
        builder, op.getLoc(), cir::BinOpKind::Or, lhsAdjField, rhsAdjField,
        {}, {}, {}, {}, {}, {});
    mlir::Value orAdjAndOne = cir::BinOp::create(
        builder, op.getLoc(), cir::BinOpKind::And, orAdj, ptrdiffOne, {}, {},
        {}, {}, {}, {});
    mlir::Value orAdjCmpToZero = cir::CmpOp::create(
        builder, op.getLoc(), op.getKind(), orAdjAndOne, ptrdiffZero);
    ptrCmpToNull = op.getKind() == cir::CmpOpKind::eq
                       ? create_and(ptrCmpToNull, orAdjCmpToZero)
                       : create_or(ptrCmpToNull, orAdjCmpToZero);
  }

  mlir::Value result;
  if (op.getKind() == cir::CmpOpKind::eq) {
    // (lhs.ptr == null || lhs.adj == rhs.adj) && lhs.ptr == rhs.ptr
    result = create_and(create_or(ptrCmpToNull, adjCmp), ptrCmp);
  } else {
    // (lhs.ptr != null && lhs.adj != rhs.adj) || lhs.ptr != rhs.ptr
    result = create_or(create_and(ptrCmpToNull, adjCmp), ptrCmp);
  }

  return result;
}

mlir::Value
ItaniumCXXABI::lowerDataMemberBitcast(cir::CastOp op, mlir::Type loweredDstTy,
                                      mlir::Value loweredSrc,
                                      mlir::OpBuilder &builder) const {
  if (loweredSrc.getType() == loweredDstTy)
    return loweredSrc;

  return cir::CastOp::create(builder, op.getLoc(), loweredDstTy,
                             cir::CastKind::bitcast, loweredSrc);
}

mlir::Value
ItaniumCXXABI::lowerDataMemberToBoolCast(cir::CastOp op, mlir::Value loweredSrc,
                                         mlir::OpBuilder &builder) const {
  // Itanium C++ ABI 2.3:
  //   A NULL pointer is represented as -1.
  auto nullAttr = cir::IntAttr::get(getPtrDiffCIRTy(LM), -1);
  auto nullValue = cir::ConstantOp::create(builder, op.getLoc(), nullAttr);
  return cir::CmpOp::create(builder, op.getLoc(), cir::CmpOpKind::ne,
                            loweredSrc, nullValue);
}

mlir::Value ItaniumCXXABI::lowerMethodBitcast(cir::CastOp op,
                                              mlir::Type loweredDstTy,
                                              mlir::Value loweredSrc,
                                              mlir::OpBuilder &builder) const {
  (void)loweredDstTy;
  return lowerMethodCast(op, loweredSrc, /*offset=*/0,
                         /*isDerivedToBase=*/false, LM, UseARMMethodPtrABI,
                         builder);
}

mlir::Value
ItaniumCXXABI::lowerMethodToBoolCast(cir::CastOp op, mlir::Value loweredSrc,
                                     mlir::OpBuilder &builder) const {
  // Itanium C++ ABI 2.3.2:
  //
  //   In the standard representation, a null member function pointer is
  //   represented with ptr set to a null pointer. The value of adj is
  //   unspecified for null member function pointers.
  cir::IntType ptrdiffCIRTy = getPtrDiffCIRTy(LM);
  mlir::Value ptrdiffZero = cir::ConstantOp::create(
      builder, op.getLoc(), cir::IntAttr::get(ptrdiffCIRTy, 0));
  auto methodRecTy = mlir::cast<cir::RecordType>(getMethodABIType());
  loweredSrc = ensureMemberPtrRecord(builder, op.getLoc(),
                                     loweredSrc, methodRecTy);
  mlir::Value ptrField = cir::ExtractMemberOp::create(
      builder, op.getLoc(), ptrdiffCIRTy, loweredSrc, 0);
  mlir::Value result = cir::CmpOp::create(builder, op.getLoc(),
                                          cir::CmpOpKind::ne, ptrField,
                                          ptrdiffZero);

  // On ARM, member function pointers are also non-null when the low bit of the
  // adj field is set (virtual member function pointer, including vtable offset
  // 0 encoded as {ptr=0, adj=1}).
  if (UseARMMethodPtrABI) {
    mlir::Value adjField = cir::ExtractMemberOp::create(
        builder, op.getLoc(), ptrdiffCIRTy, loweredSrc, 1);
    mlir::Value ptrdiffOne = cir::ConstantOp::create(
        builder, op.getLoc(), cir::IntAttr::get(ptrdiffCIRTy, 1));
    mlir::Value virtualBit = cir::BinOp::create(
        builder, op.getLoc(), cir::BinOpKind::And, adjField, ptrdiffOne, {}, {},
        {}, {}, {}, {});
    mlir::Value isVirtual = cir::CmpOp::create(builder, op.getLoc(),
                                               cir::CmpOpKind::ne, virtualBit,
                                               ptrdiffZero);
    CIRBaseBuilderTy cirBuilder(builder);
    mlir::Value trueValue = cirBuilder.getTrue(op.getLoc());
    result =
        cir::SelectOp::create(builder, op.getLoc(), result, trueValue, isVirtual);
  }

  return result;
}

CIRCXXABI *CreateItaniumCXXABI(LowerModule &LM) {
  auto componentLayout =
      LM.getContext().getLangOpts().RelativeCXXABIVTables
          ? ItaniumCXXABI::VTableComponentLayout::Relative
          : ItaniumCXXABI::VTableComponentLayout::Pointer;

  switch (LM.getCXXABIKind()) {
  // Note that AArch64 uses the generic ItaniumCXXABI class since it doesn't
  // include the other 32-bit ARM oddities: constructor/destructor return values
  // and array cookies.
  case clang::TargetCXXABI::GenericAArch64:
  case clang::TargetCXXABI::AppleARM64:
    // OGCG uses AppleARM64CXXABI (inheriting from ARMCXXABI) with Apple-
    // specific vtable offset handling. Apple ARM64 is not a supported CIR
    // target; we use the generic ARM ABI settings here.
    return new ItaniumCXXABI(LM, /*UseARMMethodPtrABI=*/true,
                             /*UseARMGuardVarABI=*/true, componentLayout);

  case clang::TargetCXXABI::Fuchsia:
    return new ItaniumCXXABI(LM, /*UseARMMethodPtrABI=*/false,
                             /*UseARMGuardVarABI=*/false, componentLayout);

  case clang::TargetCXXABI::GenericItanium:
    return new ItaniumCXXABI(LM, /*UseARMMethodPtrABI=*/false,
                             /*UseARMGuardVarABI=*/false, componentLayout);

  case clang::TargetCXXABI::GenericARM:
  case clang::TargetCXXABI::iOS:
  case clang::TargetCXXABI::WatchOS:
  // WebAssembly's C++ ABI is Itanium-based and, like ARM, uses the ARM-style
  // method pointer and guard variable ABI (see OGCG's WebAssemblyCXXABI).
  case clang::TargetCXXABI::WebAssembly:
    return new ItaniumCXXABI(LM, /*UseARMMethodPtrABI=*/true,
                             /*UseARMGuardVarABI=*/true, componentLayout);

  case clang::TargetCXXABI::Microsoft:
    cir_cconv_unreachable("Microsoft ABI is not Itanium-based");
  default:
    cir_cconv_unreachable("NYI");
  }

  cir_cconv_unreachable("bad ABI kind");
}

} // namespace cir

// FIXME(cir): Merge this into the CIRCXXABI class above.
class LoweringPrepareItaniumCXXABI : public cir::LoweringPrepareCXXABI {
public:
  mlir::Value lowerDynamicCast(cir::CIRBaseBuilderTy &builder,
                               clang::ASTContext &astCtx,
                               cir::DynamicCastOp op) override;
  // TODO(cir): Add these back when VAArgOp and DeleteArrayOp lowering are
  // ported to upstream LoweringPrepareCXXABI.
  // mlir::Value lowerVAArg(cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
  //                        const cir::CIRDataLayout &datalayout) override;
  // mlir::Value lowerDeleteArray(cir::CIRBaseBuilderTy &builder,
  //                              cir::DeleteArrayOp op,
  //                              const cir::CIRDataLayout &datalayout)
  //                              override;
};
