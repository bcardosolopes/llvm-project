//===- CIRToABIType.cpp - CIR to LLVM ABI library type bridge -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CIRToABIType.h"

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;
using namespace cir;

/// Whether a record's declared argument-passing kind (from the module's
/// record-layout metadata) allows it to be passed in registers.  A record with
/// no layout entry (e.g. an anonymous struct) has no C++ non-trivial reason to
/// be forced to memory, so it defaults to can-pass-in-registers.
static bool recordCanPassInRegs(ModuleOp modOp, cir::RecordType recTy) {
  auto layout = cir::tryGetRecordLayout(modOp, recTy.getName());
  if (!layout)
    return true;
  return layout.getArgPassingKind() == cir::ArgPassingKind::CanPassInRegs;
}

/// A record's declared alignment, which the ABI uses for the byval and sret
/// alignment of an indirect argument.  DataLayout derives alignment from the
/// members, so it cannot see `__attribute__((aligned(N)))`.  The declared value
/// comes from the module's record-layout metadata instead.  CIRGen emits an
/// entry for every record it names, so the computed fallback only serves
/// hand-written CIR.
static llvm::Align recordDeclaredAlign(ModuleOp modOp, cir::RecordType recTy,
                                       const DataLayout &dl) {
  auto layout = cir::tryGetRecordLayout(modOp, recTy.getName());
  if (!layout)
    return llvm::Align(dl.getTypeABIAlignment(recTy));
  return llvm::Align(layout.getRecordAlign());
}

const llvm::abi::Type *cir::mapTypeToABIType(mlir::Type type,
                                             mlir::abi::ABITypeMapper &mapper,
                                             const DataLayout &dl,
                                             ModuleOp modOp) {
  llvm::abi::TypeBuilder &tb = mapper.getTypeBuilder();
  // Deferred: a void or incomplete type has no alignment to query.
  auto align = [&] { return llvm::Align(dl.getTypeABIAlignment(type)); };
  return llvm::TypeSwitch<mlir::Type, const llvm::abi::Type *>(type)
      .Case([&](cir::IntType intTy) {
        return tb.getIntegerType(intTy.getWidth(), align(), intTy.isSigned(),
                                 intTy.getIsBitInt());
      })
      .Case([&](cir::PointerType ptrTy) -> const llvm::abi::Type * {
        // A LangAddressSpaceAttr has to be lowered to a target address space
        // before the ABI can be asked about the pointer, so there is no
        // mapping for one.
        mlir::Attribute addrSpaceAttr = ptrTy.getAddrSpace();
        if (addrSpaceAttr && !isa<cir::TargetAddressSpaceAttr>(addrSpaceAttr))
          return nullptr;
        unsigned addrSpace = 0;
        if (auto targetAsAttr =
                dyn_cast_if_present<cir::TargetAddressSpaceAttr>(addrSpaceAttr))
          addrSpace = targetAsAttr.getValue();
        return tb.getPointerType(dl.getTypeSizeInBits(type), align(),
                                 addrSpace);
      })
      .Case([&](cir::BoolType) {
        return tb.getIntegerType(dl.getTypeSizeInBits(type), align(),
                                 /*Signed=*/false);
      })
      .Case([&](cir::VoidType) { return tb.getVoidType(); })
      .Case([&](cir::FPTypeInterface fpTy) {
        return tb.getFloatType(fpTy.getFloatSemantics(), align());
      })
      .Case([&](cir::ComplexType complexTy) -> const llvm::abi::Type * {
        const llvm::abi::Type *elemAbi =
            mapTypeToABIType(complexTy.getElementType(), mapper, dl, modOp);
        if (!elemAbi)
          return nullptr;
        return tb.getComplexType(elemAbi, align());
      })
      .Case([&](cir::ArrayType arrTy) -> const llvm::abi::Type * {
        const llvm::abi::Type *elemAbi =
            mapTypeToABIType(arrTy.getElementType(), mapper, dl, modOp);
        if (!elemAbi)
          return nullptr;
        return tb.getArrayType(elemAbi, arrTy.getSize(),
                               dl.getTypeSizeInBits(type).getFixedValue());
      })
      .Case([&](cir::RecordType recTy) -> const llvm::abi::Type * {
        // An incomplete record has no layout to classify, and a packed one
        // needs the pad-aware eightbyte classification this bridge does not
        // implement.
        if (!recTy.isComplete() || recTy.getPacked())
          return nullptr;
        llvm::abi::RecordFlags flags =
            recordCanPassInRegs(modOp, recTy)
                ? llvm::abi::RecordFlags::CanPassInRegisters
                : llvm::abi::RecordFlags::None;
        llvm::TypeSize sizeBits =
            llvm::TypeSize::getFixed(dl.getTypeSizeInBits(type).getFixedValue());
        llvm::Align recAlign = recordDeclaredAlign(modOp, recTy, dl);
        SmallVector<llvm::abi::FieldInfo> fields;
        fields.reserve(recTy.getMembers().size());

        // The size passed here spans the tail padding, so an eightbyte covers
        // the whole union rather than just the member the classifier reduces
        // it to.
        if (recTy.isUnion()) {
          for (mlir::Type fieldTy : recTy.getMembers()) {
            const llvm::abi::Type *mappedField =
                mapTypeToABIType(fieldTy, mapper, dl, modOp);
            if (!mappedField)
              return nullptr;
            fields.push_back(llvm::abi::FieldInfo(mappedField));
          }
          return tb.getUnionType(fields, sizeBits, recAlign,
                                 llvm::abi::StructPacking::Default, flags);
        }

        // A padded struct's explicit padding member would have to be
        // recognized as padding rather than data.  Only a struct whose fields
        // all sit at their naturally-aligned offsets maps.
        if (recTy.getPadded())
          return nullptr;
        uint64_t offsetBits = 0;
        for (mlir::Type fieldTy : recTy.getMembers()) {
          const llvm::abi::Type *mappedField =
              mapTypeToABIType(fieldTy, mapper, dl, modOp);
          if (!mappedField)
            return nullptr;
          offsetBits =
              llvm::alignTo(offsetBits, dl.getTypeABIAlignment(fieldTy) * 8);
          fields.push_back(llvm::abi::FieldInfo(mappedField, offsetBits));
          offsetBits += dl.getTypeSizeInBits(fieldTy).getFixedValue();
        }
        return tb.getRecordType(fields, sizeBits, recAlign,
                                llvm::abi::StructPacking::Default,
                                /*BaseClasses=*/{}, /*VirtualBaseClasses=*/{},
                                flags);
      })
      .Default([](mlir::Type) -> const llvm::abi::Type * { return nullptr; });
}
