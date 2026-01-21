//===- LoweringPrepareX86CXXABI.cpp - X86 ABI specific code ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides X86_64 C++ ABI specific code that is used during
// LLVMIR lowering prepare.
//
//===----------------------------------------------------------------------===//

#include "../LoweringPrepareCXXABI.h"
#include "ABIInfoImpl.h"
#include "LowerModule.h"
#include "Targets/X86_64ABIInfo.h"
#include "clang/CIR/Dialect/IR/CIRDataLayout.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/MissingFeatures.h"

using namespace mlir;
using namespace cir;

namespace {

class LoweringPrepareX86CXXABI : public cir::LoweringPrepareCXXABI {
  bool is64;

public:
  LoweringPrepareX86CXXABI(bool is64) : is64(is64) {}

  mlir::Value lowerDynamicCast(cir::CIRBaseBuilderTy &builder,
                               clang::ASTContext &astCtx,
                               cir::DynamicCastOp op) override;

  mlir::Value lowerVAArg(cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
                         const cir::CIRDataLayout &datalayout) override {
    if (is64)
      return lowerVAArgX86_64(builder, op, datalayout);

    llvm_unreachable("lowerVAArg for X86_32 not implemented yet");
  }

  mlir::Value lowerVAArgX86_64(cir::CIRBaseBuilderTy &builder,
                               cir::VAArgOp op,
                               const cir::CIRDataLayout &datalayout);
};

/// Helper to get a LowerModule from a VAArgOp (needed for ABI classification).
std::unique_ptr<cir::LowerModule> getLowerModule(cir::VAArgOp op) {
  mlir::ModuleOp mo = op->getParentOfType<mlir::ModuleOp>();
  if (!mo)
    return nullptr;
  mlir::PatternRewriter rewriter(mo.getContext());
  return cir::createLowerModule(mo, rewriter);
}

/// Helper to create a GetMemberOp with automatic result type derivation.
mlir::Value createGetMemberOp(cir::CIRBaseBuilderTy &builder,
                              mlir::Location loc, mlir::Value recordPtr,
                              const char *fldName, unsigned idx) {
  auto recordBaseTy =
      mlir::cast<cir::PointerType>(recordPtr.getType()).getPointee();
  auto fldTy = mlir::cast<cir::RecordType>(recordBaseTy).getMembers()[idx];
  auto fldPtrTy = cir::PointerType::get(fldTy);
  return builder.createGetMember(loc, fldPtrTy, recordPtr, fldName, idx);
}

/// Build va_arg lowering for arguments passed in memory on x86-64.
mlir::Value buildX86_64VAArgFromMemory(cir::CIRBaseBuilderTy &builder,
                                       const cir::CIRDataLayout &datalayout,
                                       mlir::Value valist, mlir::Type Ty,
                                       mlir::Location loc) {
  mlir::Value overflow_arg_area_p =
      createGetMemberOp(builder, loc, valist, "overflow_arg_area", 2);
  mlir::Value overflow_arg_area =
      builder.createLoad(loc, overflow_arg_area_p);

  // AMD64-ABI 3.5.7p5: Step 7. Align l->overflow_arg_area upwards to a 16
  // byte boundary if alignment needed by type exceeds 8 byte boundary.
  unsigned alignment = datalayout.getABITypeAlign(Ty).value();
  if (alignment > 8)
    overflow_arg_area =
        emitRoundPointerUpToAlignment(builder, overflow_arg_area, alignment);

  // AMD64-ABI 3.5.7p5: Step 8. Fetch type from l->overflow_arg_area.
  mlir::Value res = overflow_arg_area;

  // AMD64-ABI 3.5.7p5: Step 9. Set l->overflow_arg_area to:
  // l->overflow_arg_area + sizeof(type).
  // AMD64-ABI 3.5.7p5: Step 10. Align l->overflow_arg_area upwards to
  // an 8 byte boundary.
  uint64_t sizeInBytes = datalayout.getTypeStoreSize(Ty).getFixedValue();
  mlir::Value stride = builder.getSignedInt(loc, ((sizeInBytes + 7) & ~7), 32);
  mlir::Value castedPtr =
      builder.createPtrBitcast(overflow_arg_area, builder.getSIntNTy(8));
  overflow_arg_area = builder.createPtrStride(loc, castedPtr, stride);
  // Cast back to void* to match overflow_arg_area_p's pointee type.
  overflow_arg_area =
      builder.createBitcast(overflow_arg_area, builder.getVoidPtrTy());
  builder.createStore(loc, overflow_arg_area, overflow_arg_area_p);

  return res;
}

mlir::Value LoweringPrepareX86CXXABI::lowerVAArgX86_64(
    cir::CIRBaseBuilderTy &builder, cir::VAArgOp op,
    const cir::CIRDataLayout &datalayout) {
  // FIXME: return early since X86_64ABIInfo::classify can't handle these types.
  // Let's hope LLVM's va_arg instruction can take care of it.
  // Remove this when X86_64ABIInfo::classify can take care of every type.
  // NOTE: LongDoubleType is excluded because its lowering requires
  // PtrMaskOp (for 16-byte alignment of overflow area), which is NYI.
  // NOTE: RecordType is excluded because mixed register handling (both
  // GP and SSE registers) requires createMemCpy, which is NYI.
  if (!mlir::isa<VoidType, IntType, SingleType, DoubleType, BoolType,
                 cir::PointerType>(op.getType()))
    return nullptr;

  // Assume that va_list type is correct; should be pointer to LLVM type:
  // struct {
  //   i32 gp_offset;
  //   i32 fp_offset;
  //   i8* overflow_arg_area;
  //   i8* reg_save_area;
  // };
  unsigned neededInt, neededSSE;

  std::unique_ptr<cir::LowerModule> lowerModule = getLowerModule(op);
  if (!lowerModule)
    return nullptr;
  mlir::Type ty = op.getType();

  // FIXME: How should we access the X86AVXABILevel?
  X86_64ABIInfo abiInfo(lowerModule->getTypes(), X86AVXABILevel::None);
  ABIArgInfo ai = abiInfo.classifyArgumentType(
      ty, 0, neededInt, neededSSE, /*isNamedArg=*/false, /*IsRegCall=*/false);

  // Empty records are ignored for parameter passing purposes.
  if (ai.isIgnore())
    return nullptr;

  mlir::Location loc = op.getLoc();
  mlir::Value valist = op.getOperand();

  // AMD64-ABI 3.5.7p5: Step 1. Determine whether type may be passed
  // in the registers. If not go to step 7.
  if (!neededInt && !neededSSE)
    return builder.createLoad(
        loc, builder.createPtrBitcast(buildX86_64VAArgFromMemory(
                                          builder, datalayout, valist, ty, loc),
                                      ty));

  mlir::OpBuilder::InsertPoint scopeIP;
  auto scopeOp = cir::ScopeOp::create(
      builder, loc,
      [&](mlir::OpBuilder &opBuilder, mlir::Type &yieldTy,
          mlir::Location loc) {
        scopeIP = opBuilder.saveInsertionPoint();
        yieldTy = op.getType();
      });

  mlir::Block *contBlock = scopeIP.getBlock();

  mlir::Block *currentBlock = builder.createBlock(contBlock);
  builder.setInsertionPointToEnd(currentBlock);

  // AMD64-ABI 3.5.7p5: Step 2. Compute num_gp to hold the number of
  // general purpose registers needed to pass type and num_fp to hold
  // the number of floating point registers needed.

  // AMD64-ABI 3.5.7p5: Step 3. Verify whether arguments fit into
  // registers. In the case: l->gp_offset > 48 - num_gp * 8 or
  // l->fp_offset > 304 - num_fp * 16 go to step 7.
  //
  // NOTE: 304 is a typo, there are (6 * 8 + 8 * 16) = 176 bytes of
  // register save space).

  mlir::Value inRegs;
  mlir::Value gp_offset_p, fp_offset_p;
  mlir::Value gp_offset, fp_offset;

  if (neededInt) {
    gp_offset_p =
        createGetMemberOp(builder, loc, valist, "gp_offset", 0);
    gp_offset = builder.createLoad(loc, gp_offset_p);
    inRegs = builder.getUnsignedInt(loc, 48 - neededInt * 8, 32);
    inRegs =
        builder.createCompare(loc, cir::CmpOpKind::le, gp_offset, inRegs);
  }

  if (neededSSE) {
    fp_offset_p =
        createGetMemberOp(builder, loc, valist, "fp_offset", 1);
    fp_offset = builder.createLoad(loc, fp_offset_p);
    mlir::Value fitsInFP =
        builder.getUnsignedInt(loc, 176 - neededSSE * 16, 32);
    fitsInFP =
        builder.createCompare(loc, cir::CmpOpKind::le, fp_offset, fitsInFP);
    inRegs = inRegs ? builder.createAnd(inRegs.getLoc(), inRegs, fitsInFP)
                    : fitsInFP;
  }

  mlir::Block *inRegBlock = builder.createBlock(contBlock);
  mlir::Block *inMemBlock = builder.createBlock(contBlock);
  builder.setInsertionPointToEnd(currentBlock);
  cir::BrCondOp::create(builder, loc, inRegs, inRegBlock, inMemBlock);

  // Emit code to load the value if it was passed in registers.
  builder.setInsertionPointToStart(inRegBlock);

  // AMD64-ABI 3.5.7p5: Step 4. Fetch type from l->reg_save_area with
  // an offset of l->gp_offset and/or l->fp_offset. This may require
  // copying to a temporary location in case the parameter is passed
  // in different register classes or requires an alignment greater
  // than 8 for general purpose registers and 16 for XMM registers.
  mlir::Value regSaveArea = builder.createLoad(
      loc, createGetMemberOp(builder, loc, valist, "reg_save_area", 3));
  mlir::Value regAddr;

  uint64_t tyAlign = datalayout.getABITypeAlign(ty).value();
  // The alignment of result address.
  uint64_t alignment = 0;
  if (neededInt && neededSSE) {
    // FIXME: Cleanup.
    assert(ai.isDirect() && "Unexpected ABI info for mixed regs");
    auto recordTy = mlir::cast<cir::RecordType>(ai.getCoerceToType());
    cir::PointerType addrTy = builder.getPointerTo(ty);

    mlir::Value tmp = builder.createAlloca(
        loc, addrTy, ty, "tmp",
        builder.getAlignmentAttr(clang::CharUnits::fromQuantity(tyAlign)));
    tmp = builder.createPtrBitcast(tmp, recordTy);
    assert(recordTy.getNumElements() == 2 &&
           "Unexpected ABI info for mixed regs");
    mlir::Type tyLo = recordTy.getMembers()[0];
    mlir::Type tyHi = recordTy.getMembers()[1];
    assert((isFPOrVectorOfFPType(tyLo) ^ isFPOrVectorOfFPType(tyHi)) &&
           "Unexpected ABI info for mixed regs");
    mlir::Value gpAddr =
        builder.createPtrStride(loc, regSaveArea, gp_offset);
    mlir::Value fpAddr =
        builder.createPtrStride(loc, regSaveArea, fp_offset);
    mlir::Value regLoAddr = isFPOrVectorOfFPType(tyLo) ? fpAddr : gpAddr;
    mlir::Value regHiAddr = isFPOrVectorOfFPType(tyHi) ? gpAddr : fpAddr;

    // Copy the first element.
    mlir::Value v = builder.createAlignedLoad(
        loc, regLoAddr, datalayout.getABITypeAlign(tyLo).value());
    builder.createStore(
        loc, v,
        builder.createGetMember(
            loc, builder.getPointerTo(tyLo), tmp, "gp_offset", 0));

    // Copy the second element.
    v = builder.createAlignedLoad(
        loc, regHiAddr, datalayout.getABITypeAlign(tyHi).value());
    builder.createStore(
        loc, v,
        builder.createGetMember(
            loc, builder.getPointerTo(tyHi), tmp, "fp_offset", 1));

    tmp = builder.createPtrBitcast(tmp, ty);
    regAddr = tmp;
  } else if (neededInt || neededSSE == 1) {
    uint64_t tySize = datalayout.getTypeStoreSize(ty).getFixedValue();

    mlir::Type coTy;
    if (ai.isDirect())
      coTy = ai.getCoerceToType();

    mlir::Value gpOrFpOffset = neededInt ? gp_offset : fp_offset;
    alignment = neededInt ? 8 : 16;
    uint64_t regSize = neededInt ? neededInt * 8 : 16;
    if (coTy && (ai.getDirectOffset() == 8 || regSize < tySize)) {
      cir::PointerType addrTy = builder.getPointerTo(ty);
      mlir::Value tmp = builder.createAlloca(
          loc, addrTy, ty, "tmp",
          builder.getAlignmentAttr(clang::CharUnits::fromQuantity(tyAlign)));
      mlir::Value addr =
          builder.createPtrStride(loc, regSaveArea, gpOrFpOffset);
      mlir::Value src = builder.createAlignedLoad(
          loc, builder.createPtrBitcast(addr, coTy), tyAlign);
      mlir::Value ptrOffset =
          builder.getUnsignedInt(loc, ai.getDirectOffset(), 32);
      mlir::Value dst = builder.createPtrStride(loc, tmp, ptrOffset);
      builder.createStore(loc, src, dst);
      regAddr = tmp;
    } else {
      regAddr = builder.createPtrStride(loc, regSaveArea, gpOrFpOffset);

      // Copy into a temporary if the type is more aligned than the
      // register save area.
      if (neededInt && tyAlign > 8) {
        cir::PointerType addrTy = builder.getPointerTo(ty);
        mlir::Value tmp = builder.createAlloca(
            loc, addrTy, ty, "tmp",
            builder.getAlignmentAttr(clang::CharUnits::fromQuantity(tyAlign)));
        assert(!cir::MissingFeatures::createMemCpy());
        regAddr = tmp;
      }
    }

  } else {
    assert(neededSSE == 2 && "Invalid number of needed registers!");
    // SSE registers are spaced 16 bytes apart in the register save
    // area, we need to collect the two eightbytes together.
    mlir::Value regAddrLo =
        builder.createPtrStride(loc, regSaveArea, fp_offset);
    mlir::Value regAddrHi = builder.createPtrStride(
        loc, regAddrLo, builder.getUnsignedInt(loc, 16, /*numBits=*/32));

    mlir::MLIRContext *context = builder.getContext();
    cir::RecordType recordTy =
        ai.canHaveCoerceToType()
            ? cast<cir::RecordType>(ai.getCoerceToType())
            : cir::RecordType::get(
                  context,
                  {DoubleType::get(context), DoubleType::get(context)},
                  /*packed=*/false, /*padded=*/false,
                  cir::RecordType::Struct);
    cir::PointerType addrTy = builder.getPointerTo(ty);
    mlir::Value tmp = builder.createAlloca(
        loc, addrTy, ty, "tmp",
        builder.getAlignmentAttr(clang::CharUnits::fromQuantity(tyAlign)));
    tmp = builder.createPtrBitcast(tmp, recordTy);
    mlir::Value v = builder.createLoad(
        loc, builder.createPtrBitcast(regAddrLo, recordTy.getMembers()[0]));
    builder.createStore(
        loc, v,
        builder.createGetMember(
            loc, builder.getPointerTo(recordTy.getMembers()[0]),
            tmp, "", 0));
    v = builder.createLoad(
        loc, builder.createPtrBitcast(regAddrHi, recordTy.getMembers()[1]));
    builder.createStore(
        loc, v,
        builder.createGetMember(
            loc, builder.getPointerTo(recordTy.getMembers()[1]),
            tmp, "", 1));

    tmp = builder.createPtrBitcast(tmp, ty);
    regAddr = tmp;
  }

  // AMD64-ABI 3.5.7p5: Step 5. Set:
  // l->gp_offset = l->gp_offset + num_gp * 8
  // l->fp_offset = l->fp_offset + num_fp * 16.
  if (neededInt) {
    mlir::Value offset = builder.getUnsignedInt(loc, neededInt * 8, 32);
    builder.createStore(loc, builder.createAdd(loc, gp_offset, offset),
                        gp_offset_p);
  }

  if (neededSSE) {
    mlir::Value offset = builder.getUnsignedInt(loc, neededSSE * 8, 32);
    builder.createStore(loc, builder.createAdd(loc, fp_offset, offset),
                        fp_offset_p);
  }

  cir::BrOp::create(builder, loc, mlir::ValueRange{regAddr}, contBlock);

  // Emit code to load the value if it was passed in memory.
  builder.setInsertionPointToStart(inMemBlock);
  mlir::Value memAddr =
      buildX86_64VAArgFromMemory(builder, datalayout, valist, ty, loc);
  cir::BrOp::create(builder, loc, mlir::ValueRange{memAddr}, contBlock);

  // Yield the appropriate result.
  builder.setInsertionPointToStart(contBlock);
  mlir::Value res_addr = contBlock->addArgument(regAddr.getType(), loc);

  mlir::Value result =
      alignment
          ? builder.createAlignedLoad(
                loc, builder.createPtrBitcast(res_addr, ty), alignment)
          : builder.createLoad(loc, builder.createPtrBitcast(res_addr, ty));

  cir::YieldOp::create(builder, loc, result);

  return scopeOp.getResult(0);
}

// Delegate dynamic cast to the Itanium ABI implementation (which is
// shared across Itanium-derived ABIs including x86).
mlir::Value LoweringPrepareX86CXXABI::lowerDynamicCast(
    cir::CIRBaseBuilderTy &builder, clang::ASTContext &astCtx,
    cir::DynamicCastOp op) {
  // Create a temporary Itanium ABI instance and delegate.
  std::unique_ptr<cir::LoweringPrepareCXXABI> itaniumABI(
      cir::LoweringPrepareCXXABI::createItaniumABI());
  return itaniumABI->lowerDynamicCast(builder, astCtx, op);
}

} // namespace

cir::LoweringPrepareCXXABI *cir::LoweringPrepareCXXABI::createX86ABI(
    bool is64Bit) {
  return new LoweringPrepareX86CXXABI(is64Bit);
}
