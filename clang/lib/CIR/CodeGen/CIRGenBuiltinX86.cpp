//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit x86/x86_64 Builtin calls as CIR or a function
// call to be later resolved.
//
//===----------------------------------------------------------------------===//

#include "CIRGenBuilder.h"
#include "CIRGenFunction.h"
#include "CIRGenModule.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/Types.h"
#include "mlir/IR/ValueRange.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/MissingFeatures.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/X86TargetParser.h"
#include <string>

using namespace clang;
using namespace clang::CIRGen;

// Some x86 compare families historically mapped inverse predicates through
// `shouldInvert`. CIR now carries the direct unordered/ordered FP predicates
// needed by x86 AVX/SSE compare builtins, but keeping the helper generic keeps
// the remaining call sites simple.
static mlir::Value emitVectorFCmp(CIRGenFunction &cgf,
                                  llvm::SmallVector<mlir::Value> &ops,
                                  mlir::Location loc, cir::CmpOpKind pred,
                                  bool shouldInvert, bool isSignaling,
                                  FPOptions fpFeatures = FPOptions()) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  // TODO(cir): Add isSignaling boolean once constrained FP is implemented
  auto cmp = builder.createVecCompare(loc, pred, ops[0], ops[1]);
  cgf.maybeAttachFPConstraints(cmp.getOperation(), fpFeatures);
  cgf.maybeMarkSignalingFPCompare(cmp.getOperation(), isSignaling);
  mlir::Value bitCast = builder.createBitcast(
      shouldInvert ? builder.createNot(cmp) : cmp, ops[0].getType());
  return bitCast;
}

// Emit a scalar SSE/SSE2 comparison intrinsic call (e.g. x86.sse.cmp.ss).
// Appends an i8 immediate for the comparison condition code.
static mlir::Value
emitX86CmpIntrinsicCall(CIRGenBuilderTy &builder, CIRGenModule &cgm,
                        llvm::SmallVectorImpl<mlir::Value> &ops,
                        mlir::Location loc,
                        llvm::StringRef intrinsicName, unsigned imm) {
  auto resultTy = ops[0].getType();
  mlir::Value immVal = builder.getConstInt(loc, builder.getUIntNTy(8), imm);
  SmallVector<mlir::Value> args(ops.begin(), ops.end());
  args.push_back(immVal);
  return cir::LLVMIntrinsicCallOp::create(
             builder, loc, cgm.getStringAttrCached(intrinsicName), resultTy,
             args)
      .getResult();
}

static void maybeSetCallStrictFPAttr(CIRGenBuilderTy &builder,
                                     mlir::Operation *op,
                                     bool isFPConstrained) {
  if (isFPConstrained)
    op->setAttr("cir.call_strictfp", mlir::UnitAttr::get(builder.getContext()));
}

static mlir::Value emitX86PSLLDQIByteShift(CIRGenFunction &cgf,
                                           const CallExpr *e,
                                           ArrayRef<mlir::Value> ops) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  unsigned shiftVal =
      ops[1].getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue() &
      0xff;
  mlir::Location loc = cgf.getLoc(e->getExprLoc());
  auto byteVecType = cast<cir::VectorType>(ops[0].getType());

  // Get the original return type from the expression
  auto resultType = cast<cir::VectorType>(cgf.convertType(e->getType()));

  // If pslldq is shifting the vector more than 15 bytes, emit zero.
  // This matches the hardware behavior where shifting by 16+ bytes
  // clears the entire 128-bit lane.
  if (shiftVal >= 16) {
    mlir::Value zero = builder.getZero(loc, byteVecType);
    if (byteVecType != resultType)
      return builder.createBitcast(zero, resultType);
    return zero;
  }

  // Builtin type is vXi8 (already in bytes)
  unsigned numElts = byteVecType.getSize();
  assert(numElts % 16 == 0 && "Vector size must be multiple of 16 bytes");

  llvm::SmallVector<int64_t, 64> indices;

  // 256/512-bit pslldq operates on 128-bit lanes so we need to handle that
  for (unsigned l = 0; l < numElts; l += 16) {
    for (unsigned i = 0; i != 16; ++i) {
      unsigned idx = numElts + i - shiftVal;
      if (idx < numElts)
        idx -= numElts - 16; // end of lane, switch operand.
      indices.push_back(idx + l);
    }
  }

  mlir::Value zero = builder.getZero(loc, byteVecType);

  // Perform the shuffle (left shift by inserting zeros)
  mlir::Value shuffleResult =
      builder.createVecShuffle(loc, zero, ops[0], indices);

  // Cast back to original type if necessary
  if (byteVecType != resultType)
    return builder.createBitcast(shuffleResult, resultType);
  return shuffleResult;
}

static mlir::Value emitX86PSRLDQIByteShift(CIRGenFunction &cgf,
                                           const CallExpr *e,
                                           ArrayRef<mlir::Value> ops) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  auto byteVecType = cast<cir::VectorType>(ops[0].getType());
  mlir::Location loc = cgf.getLoc(e->getExprLoc());
  unsigned shiftVal =
      ops[1].getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue() &
      0xff;

  // Get the original return type from the expression
  auto resultType = cast<cir::VectorType>(cgf.convertType(e->getType()));

  // If psrldq is shifting the vector more than 15 bytes, emit zero.
  if (shiftVal >= 16) {
    mlir::Value zero = builder.getZero(loc, byteVecType);
    if (byteVecType != resultType)
      return builder.createBitcast(zero, resultType);
    return zero;
  }

  // Builtin type is vXi8 (already in bytes)
  uint64_t numElts = byteVecType.getSize();
  assert(numElts % 16 == 0 && "Expected a multiple of 16");

  llvm::SmallVector<int64_t, 64> indices;

  // 256/512-bit psrldq operates on 128-bit lanes.
  for (unsigned l = 0; l < numElts; l += 16) {
    for (unsigned i = 0; i < 16; ++i) {
      unsigned idx = i + shiftVal;
      if (idx >= 16)
        idx += numElts - 16;
      indices.push_back(idx + l);
    }
  }

  mlir::Value zero = builder.getZero(loc, byteVecType);

  // Perform the shuffle (right shift by inserting zeros from the left)
  mlir::Value shuffleResult =
      builder.createVecShuffle(loc, ops[0], zero, indices);

  // Cast back to original type if necessary
  if (byteVecType != resultType)
    return builder.createBitcast(shuffleResult, resultType);
  return shuffleResult;
}

static mlir::Value getMaskVecValue(CIRGenBuilderTy &builder, mlir::Location loc,
                                   mlir::Value mask, unsigned numElems) {
  auto maskTy = cir::VectorType::get(
      builder.getSIntNTy(1), cast<cir::IntType>(mask.getType()).getWidth());
  mlir::Value maskVec = builder.createBitcast(mask, maskTy);

  // If we have less than 8 elements, then the starting mask was an i8 and
  // we need to extract down to the right number of elements.
  if (numElems < 8) {
    SmallVector<mlir::Attribute, 4> indices;
    for (auto i : llvm::seq<unsigned>(0, numElems))
      indices.push_back(builder.getSInt32AttrLocalCached(i));

    maskVec = builder.createVecShuffle(loc, maskVec, maskVec, indices);
  }
  return maskVec;
}

static mlir::Value emitX86MaskedStore(CIRGenFunction &cgf,
                                      ArrayRef<mlir::Value> ops,
                                      llvm::Align alignment,
                                      mlir::Location loc) {
  mlir::Value ptr = ops[0];

  mlir::Value maskVec =
      getMaskVecValue(cgf.getBuilder(), loc, ops[2],
                      cast<cir::VectorType>(ops[1].getType()).getSize());

  return cgf.getBuilder().createMaskedStore(loc, ops[1], ptr, alignment,
                                            maskVec);
}

static mlir::Value emitX86MaskedLoad(CIRGenFunction &cgf,
                                     ArrayRef<mlir::Value> ops,
                                     llvm::Align alignment,
                                     mlir::Location loc) {
  mlir::Type ty = ops[1].getType();
  mlir::Value ptr = ops[0];
  mlir::Value maskVec = getMaskVecValue(cgf.getBuilder(), loc, ops[2],
                                        cast<cir::VectorType>(ty).getSize());

  return cgf.getBuilder().createMaskedLoad(loc, ty, ptr, alignment, maskVec,
                                           ops[1]);
}

static mlir::Value emitX86ExpandLoad(CIRGenFunction &cgf,
                                     ArrayRef<mlir::Value> ops,
                                     mlir::Location loc) {
  auto resultTy = cast<cir::VectorType>(ops[1].getType());
  mlir::Value ptr = ops[0];

  mlir::Value maskVec =
      getMaskVecValue(cgf.getBuilder(), loc, ops[2], resultTy.getSize());

  return cir::LLVMIntrinsicCallOp::create(
             cgf.getBuilder(), loc,
             cgf.getCIRGenModule().getStringAttrCached("masked.expandload"),
             resultTy,
             mlir::ValueRange{ptr, maskVec, ops[1]})
      .getResult();
}

static mlir::Value emitX86CompressStore(CIRGenFunction &cgf,
                                        ArrayRef<mlir::Value> ops,
                                        mlir::Location loc) {
  auto resultTy = cast<cir::VectorType>(ops[1].getType());
  mlir::Value ptr = ops[0];

  mlir::Value maskVec =
      getMaskVecValue(cgf.getBuilder(), loc, ops[2], resultTy.getSize());

  return cir::LLVMIntrinsicCallOp::create(
             cgf.getBuilder(), loc,
             cgf.getCIRGenModule().getStringAttrCached("masked.compressstore"),
             cgf.getBuilder().getVoidTy(),
             mlir::ValueRange{ops[1], ptr, maskVec})
      .getResult();
}

// Builds the VecShuffleOp for pshuflw and pshufhw x86 builtins.
//
// The vector is split into lanes of 8 word elements (16 bits). The lower or
// upper half of each lane, controlled by `isLow`, is shuffled in the following
// way: The immediate is truncated to 8 bits, separated into 4 2-bit fields. The
// i-th field's value represents the resulting index of the i-th element in the
// half lane after shuffling. The other half of the lane remains unchanged.
static cir::VecShuffleOp emitPshufWord(CIRGenBuilderTy &builder,
                                       const mlir::Value vec,
                                       const mlir::Value immediate,
                                       const mlir::Location loc,
                                       const bool isLow) {
  uint32_t imm = CIRGenFunction::getZExtIntValueFromConstOp(immediate);

  auto vecTy = cast<cir::VectorType>(vec.getType());
  unsigned numElts = vecTy.getSize();

  unsigned firstHalfStart = isLow ? 0 : 4;
  unsigned secondHalfStart = 4 - firstHalfStart;

  // Splat the 8-bits of immediate 4 times to help the loop wrap around.
  imm = (imm & 0xff) * 0x01010101;

  int64_t indices[32];
  for (unsigned l = 0; l != numElts; l += 8) {
    for (unsigned i = firstHalfStart; i != firstHalfStart + 4; ++i) {
      indices[l + i] = l + (imm & 3) + firstHalfStart;
      imm >>= 2;
    }
    for (unsigned i = secondHalfStart; i != secondHalfStart + 4; ++i)
      indices[l + i] = l + i;
  }

  return builder.createVecShuffle(loc, vec, ArrayRef(indices, numElts));
}

// Builds the shuffle mask for pshufd and shufpd/shufps x86 builtins.
// The shuffle mask is written to outIndices.
static void
computeFullLaneShuffleMask(CIRGenFunction &cgf, const mlir::Value vec,
                           uint32_t imm, const bool isShufP,
                           llvm::SmallVectorImpl<int64_t> &outIndices) {
  auto vecTy = cast<cir::VectorType>(vec.getType());
  unsigned numElts = vecTy.getSize();
  unsigned numLanes = cgf.cgm.getDataLayout().getTypeSizeInBits(vecTy) / 128;
  unsigned numLaneElts = numElts / numLanes;

  // Splat the 8-bits of immediate 4 times to help the loop wrap around.
  imm = (imm & 0xff) * 0x01010101;

  for (unsigned l = 0; l != numElts; l += numLaneElts) {
    for (unsigned i = 0; i != numLaneElts; ++i) {
      uint32_t idx = imm % numLaneElts;
      imm /= numLaneElts;
      if (isShufP && i >= (numLaneElts / 2))
        idx += numElts;
      outIndices[l + i] = l + idx;
    }
  }

  outIndices.resize(numElts);
}

static mlir::Value emitPrefetch(CIRGenFunction &cgf, unsigned builtinID,
                                const CallExpr *e,
                                const SmallVector<mlir::Value> &ops) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location location = cgf.getLoc(e->getExprLoc());
  mlir::Type voidTy = builder.getVoidTy();
  mlir::Value address = builder.createPtrBitcast(ops[0], voidTy);
  bool isWrite{};
  int locality{};

  assert((builtinID == X86::BI_mm_prefetch ||
          builtinID == X86::BI_m_prefetchw ||
          builtinID == X86::BI_m_prefetch) &&
         "Expected prefetch builtin");

  if (builtinID == X86::BI_mm_prefetch) {
    int hint = cgf.getSExtIntValueFromConstOp(ops[1]);
    isWrite = (hint >> 2) & 0x1;
    locality = hint & 0x3;
  } else {
    isWrite = (builtinID == X86::BI_m_prefetchw);
    locality = 0x3;
  }

  cir::PrefetchOp::create(builder, location, address, locality, isWrite);
  return {};
}

static mlir::Value emitX86CompressExpand(CIRGenBuilderTy &builder,
                                         mlir::Location loc, mlir::Value source,
                                         mlir::Value mask,
                                         mlir::Value inputVector,
                                         CIRGenModule &cgm,
                                         llvm::StringRef id) {
  auto resultTy = cast<cir::VectorType>(mask.getType());
  mlir::Value maskValue = getMaskVecValue(
      builder, loc, inputVector, cast<cir::VectorType>(resultTy).getSize());
  return cir::LLVMIntrinsicCallOp::create(
             builder, loc, cgm.getStringAttrCached(id), resultTy,
             mlir::ValueRange{source, mask, maskValue})
      .getResult();
}

static mlir::Value emitX86Select(CIRGenBuilderTy &builder, mlir::Location loc,
                                 mlir::Value mask, mlir::Value op0,
                                 mlir::Value op1) {
  auto constOp = mlir::dyn_cast_or_null<cir::ConstantOp>(mask.getDefiningOp());
  // If the mask is all ones just return first argument.
  if (constOp && constOp.isAllOnesValue())
    return op0;

  mask = getMaskVecValue(builder, loc, mask,
                         cast<cir::VectorType>(op0.getType()).getSize());

  return cir::VecTernaryOp::create(builder, loc, mask, op0, op1);
}

// Scalar select: extract bit 0 from mask, select between two scalar values,
// then insert the result back into the first vector operand at position 0.
// Matches OGCG's EmitX86ScalarSelect pattern.
static mlir::Value emitX86ScalarSelect(CIRGenBuilderTy &builder,
                                       mlir::Location loc, mlir::Value mask,
                                       mlir::Value op0, mlir::Value op1) {
  auto constOp = mlir::dyn_cast_or_null<cir::ConstantOp>(mask.getDefiningOp());
  if (constOp && constOp.isAllOnesValue())
    return op0;

  auto maskTy = cir::VectorType::get(
      builder.getSIntNTy(1), cast<cir::IntType>(mask.getType()).getWidth());
  mlir::Value maskVec = builder.createBitcast(mask, maskTy);
  mlir::Value idx = builder.getConstInt(loc, builder.getSInt64Ty(), 0);
  mlir::Value bit = cir::VecExtractOp::create(builder, loc, maskVec, idx);
  mlir::Value cond = builder.createCast(cir::CastKind::int_to_bool, bit,
                                        cir::BoolType::get(builder.getContext()));
  return cir::SelectOp::create(builder, loc, cond, op0, op1);
}

// Emit a packed 512-bit FMA expression with masking.
// Matches OGCG's EmitX86FMAExpr: determines intrinsic/negation from builtinID,
// computes fma(A, B, C) or fma(A, B, -C) for sub variants, then applies
// mask/maskz/mask3 via emitX86Select.
static mlir::Value emitX86FMAExpr(CIRGenBuilderTy &builder, CIRGenModule &cgm,
                                  mlir::Location loc,
                                  SmallVectorImpl<mlir::Value> &ops,
                                  unsigned builtinID, bool isAddSub) {
  bool subtract = false;
  llvm::StringRef intrinsicName;

  switch (builtinID) {
  default:
    break;
  case X86::BI__builtin_ia32_vfmsubph512_mask3:
    subtract = true;
    [[fallthrough]];
  case X86::BI__builtin_ia32_vfmaddph512_mask:
  case X86::BI__builtin_ia32_vfmaddph512_maskz:
  case X86::BI__builtin_ia32_vfmaddph512_mask3:
    intrinsicName = "x86.avx512fp16.vfmadd.ph.512";
    break;
  case X86::BI__builtin_ia32_vfmsubaddph512_mask3:
    subtract = true;
    [[fallthrough]];
  case X86::BI__builtin_ia32_vfmaddsubph512_mask:
  case X86::BI__builtin_ia32_vfmaddsubph512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubph512_mask3:
    intrinsicName = "x86.avx512fp16.vfmaddsub.ph.512";
    break;
  case X86::BI__builtin_ia32_vfmsubps512_mask3:
    subtract = true;
    [[fallthrough]];
  case X86::BI__builtin_ia32_vfmaddps512_mask:
  case X86::BI__builtin_ia32_vfmaddps512_maskz:
  case X86::BI__builtin_ia32_vfmaddps512_mask3:
    intrinsicName = "x86.avx512.vfmadd.ps.512";
    break;
  case X86::BI__builtin_ia32_vfmsubpd512_mask3:
    subtract = true;
    [[fallthrough]];
  case X86::BI__builtin_ia32_vfmaddpd512_mask:
  case X86::BI__builtin_ia32_vfmaddpd512_maskz:
  case X86::BI__builtin_ia32_vfmaddpd512_mask3:
    intrinsicName = "x86.avx512.vfmadd.pd.512";
    break;
  case X86::BI__builtin_ia32_vfmsubaddps512_mask3:
    subtract = true;
    [[fallthrough]];
  case X86::BI__builtin_ia32_vfmaddsubps512_mask:
  case X86::BI__builtin_ia32_vfmaddsubps512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubps512_mask3:
    intrinsicName = "x86.avx512.vfmaddsub.ps.512";
    break;
  case X86::BI__builtin_ia32_vfmsubaddpd512_mask3:
    subtract = true;
    [[fallthrough]];
  case X86::BI__builtin_ia32_vfmaddsubpd512_mask:
  case X86::BI__builtin_ia32_vfmaddsubpd512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubpd512_mask3:
    intrinsicName = "x86.avx512.vfmaddsub.pd.512";
    break;
  }

  mlir::Value a = ops[0];
  mlir::Value b = ops[1];
  mlir::Value c = ops[2];

  if (subtract)
    c = cir::UnaryOp::create(builder, loc, c.getType(),
                             cir::UnaryOpKind::Minus, c);

  mlir::Value res;
  // Only use generic fma when rounding is _MM_FROUND_CUR_DIRECTION (4)
  // and not an addsub variant.
  bool useGenericFma = false;
  if (!intrinsicName.empty()) {
    unsigned rnd =
        ops.back().getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue();
    if (rnd == 4 && !isAddSub)
      useGenericFma = true;
  } else {
    useGenericFma = true;
  }

  if (useGenericFma) {
    // TODO(cir): handle constrained FP via experimental_constrained_fma
    res = cir::LLVMIntrinsicCallOp::create(
              builder, loc, cgm.getStringAttrCached("fma"), a.getType(),
              mlir::ValueRange{a, b, c})
              .getResult();
  } else {
    res = cir::LLVMIntrinsicCallOp::create(
              builder, loc, cgm.getStringAttrCached(intrinsicName), a.getType(),
              mlir::ValueRange{a, b, c, ops.back()})
              .getResult();
  }

  // Handle masking: mask keeps ops[0], maskz keeps zero, mask3 keeps ops[2].
  mlir::Value maskFalseVal;
  switch (builtinID) {
  case X86::BI__builtin_ia32_vfmaddph512_mask:
  case X86::BI__builtin_ia32_vfmaddps512_mask:
  case X86::BI__builtin_ia32_vfmaddpd512_mask:
  case X86::BI__builtin_ia32_vfmaddsubph512_mask:
  case X86::BI__builtin_ia32_vfmaddsubps512_mask:
  case X86::BI__builtin_ia32_vfmaddsubpd512_mask:
    maskFalseVal = ops[0];
    break;
  case X86::BI__builtin_ia32_vfmaddph512_maskz:
  case X86::BI__builtin_ia32_vfmaddps512_maskz:
  case X86::BI__builtin_ia32_vfmaddpd512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubph512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubps512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubpd512_maskz:
    maskFalseVal = builder.getNullValue(ops[0].getType(), loc);
    break;
  case X86::BI__builtin_ia32_vfmsubph512_mask3:
  case X86::BI__builtin_ia32_vfmaddph512_mask3:
  case X86::BI__builtin_ia32_vfmsubps512_mask3:
  case X86::BI__builtin_ia32_vfmaddps512_mask3:
  case X86::BI__builtin_ia32_vfmsubpd512_mask3:
  case X86::BI__builtin_ia32_vfmaddpd512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddph512_mask3:
  case X86::BI__builtin_ia32_vfmaddsubph512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddps512_mask3:
  case X86::BI__builtin_ia32_vfmaddsubps512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddpd512_mask3:
  case X86::BI__builtin_ia32_vfmaddsubpd512_mask3:
    maskFalseVal = ops[2];
    break;
  default:
    break;
  }

  if (maskFalseVal)
    return emitX86Select(builder, loc, ops[3], res, maskFalseVal);

  return res;
}

// Emit a scalar FMA expression with masking.
// Matches OGCG's EmitScalarFMAExpr: extracts element 0 from vector operands,
// performs scalar fma, applies scalar select masking, and inserts result back
// into the upper (passthrough) vector.
static mlir::Value emitScalarFMAExpr(CIRGenBuilderTy &builder,
                                     CIRGenModule &cgm, mlir::Location loc,
                                     SmallVectorImpl<mlir::Value> &ops,
                                     mlir::Value upper, bool zeroMask = false,
                                     unsigned ptIdx = 0, bool negAcc = false) {
  unsigned rnd = 4;
  if (ops.size() > 4)
    rnd =
        ops[4].getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue();

  if (negAcc)
    ops[2] = cir::UnaryOp::create(builder, loc, ops[2].getType(),
                                  cir::UnaryOpKind::Minus, ops[2]);

  mlir::Value idx = builder.getConstInt(loc, builder.getSInt64Ty(), 0);
  mlir::Value a = cir::VecExtractOp::create(builder, loc, ops[0], idx);
  mlir::Value b = cir::VecExtractOp::create(builder, loc, ops[1], idx);
  mlir::Value c = cir::VecExtractOp::create(builder, loc, ops[2], idx);

  mlir::Value res;
  if (rnd != 4) {
    llvm::StringRef intrinsicName;
    unsigned bitWidth = builder.getCIRIntOrFloatBitWidth(a.getType());
    switch (bitWidth) {
    case 16:
      intrinsicName = "x86.avx512fp16.vfmadd.f16";
      break;
    case 32:
      intrinsicName = "x86.avx512.vfmadd.f32";
      break;
    case 64:
      intrinsicName = "x86.avx512.vfmadd.f64";
      break;
    default:
      llvm_unreachable("Unexpected size");
    }
    res = cir::LLVMIntrinsicCallOp::create(
              builder, loc, cgm.getStringAttrCached(intrinsicName), a.getType(),
              mlir::ValueRange{a, b, c, ops[4]})
              .getResult();
  } else {
    // TODO(cir): handle constrained FP via experimental_constrained_fma
    res = cir::LLVMIntrinsicCallOp::create(
              builder, loc, cgm.getStringAttrCached("fma"), a.getType(),
              mlir::ValueRange{a, b, c})
              .getResult();
  }

  // Apply scalar masking if we have more than 3 arguments.
  if (ops.size() > 3) {
    mlir::Value passThru;
    if (zeroMask) {
      passThru = builder.getNullValue(res.getType(), loc);
    } else {
      passThru = ops[ptIdx];
      // If we negated the accumulator and it's the passthrough, bypass the
      // negate by extracting from the original upper vector.
      if (negAcc && ptIdx == 2)
        passThru = cir::VecExtractOp::create(builder, loc, upper, idx);
      else
        passThru = cir::VecExtractOp::create(builder, loc, passThru, idx);
    }
    res = emitX86ScalarSelect(builder, loc, ops[3], res, passThru);
  }

  return cir::VecInsertOp::create(builder, loc, upper, res, idx);
}

static mlir::Value emitX86MaskAddLogic(CIRGenBuilderTy &builder,
                                       mlir::Location loc,
                                       CIRGenModule &cgm,
                                       llvm::StringRef intrinsicName,
                                       SmallVectorImpl<mlir::Value> &ops) {

  auto intTy = cast<cir::IntType>(ops[0].getType());
  unsigned numElts = intTy.getWidth();
  mlir::Value lhsVec = getMaskVecValue(builder, loc, ops[0], numElts);
  mlir::Value rhsVec = getMaskVecValue(builder, loc, ops[1], numElts);
  mlir::Type vecTy = lhsVec.getType();
  mlir::Value resVec =
      cir::LLVMIntrinsicCallOp::create(
          builder, loc, cgm.getStringAttrCached(intrinsicName), vecTy,
          mlir::ValueRange{lhsVec, rhsVec})
          .getResult();
  return builder.createBitcast(resVec, ops[0].getType());
}

static mlir::Value emitX86MaskUnpack(CIRGenBuilderTy &builder,
                                     mlir::Location loc,
                                     const std::string &intrinsicName,
                                     SmallVectorImpl<mlir::Value> &ops) {
  unsigned numElems = cast<cir::IntType>(ops[0].getType()).getWidth();

  // Convert both operands to mask vectors.
  mlir::Value lhs = getMaskVecValue(builder, loc, ops[0], numElems);
  mlir::Value rhs = getMaskVecValue(builder, loc, ops[1], numElems);

  // Create indices for extracting the first half of each vector.
  SmallVector<mlir::Attribute, 32> halfIndices;
  for (auto i : llvm::seq<unsigned>(0, numElems / 2))
    halfIndices.push_back(builder.getSInt32AttrLocalCached(i));

  // Extract first half of each vector. This gives better codegen than
  // doing it in a single shuffle.
  mlir::Value lhsHalf = builder.createVecShuffle(loc, lhs, lhs, halfIndices);
  mlir::Value rhsHalf = builder.createVecShuffle(loc, rhs, rhs, halfIndices);

  // Create indices for concatenating the vectors.
  // NOTE: Operands are swapped to match the intrinsic definition.
  // After the half extraction, both vectors have numElems/2 elements.
  // In createVecShuffle(rhsHalf, lhsHalf, indices), indices [0..numElems/2-1]
  // select from rhsHalf, and indices [numElems/2..numElems-1] select from
  // lhsHalf.
  SmallVector<mlir::Attribute, 64> concatIndices;
  for (auto i : llvm::seq<unsigned>(0, numElems))
    concatIndices.push_back(builder.getSInt32AttrLocalCached(i));

  // Concat the vectors (RHS first, then LHS).
  mlir::Value res =
      builder.createVecShuffle(loc, rhsHalf, lhsHalf, concatIndices);
  return builder.createBitcast(res, ops[0].getType());
}

static mlir::Value emitX86MaskLogic(CIRGenBuilderTy &builder,
                                    mlir::Location loc,
                                    cir::BinOpKind binOpKind,
                                    SmallVectorImpl<mlir::Value> &ops,
                                    bool invertLHS = false) {
  unsigned numElts = cast<cir::IntType>(ops[0].getType()).getWidth();
  mlir::Value lhs = getMaskVecValue(builder, loc, ops[0], numElts);
  mlir::Value rhs = getMaskVecValue(builder, loc, ops[1], numElts);

  if (invertLHS)
    lhs = builder.createNot(lhs);
  return builder.createBitcast(builder.createBinop(loc, lhs, binOpKind, rhs),
                               ops[0].getType());
}

static mlir::Value emitX86MaskTest(CIRGenBuilderTy &builder, mlir::Location loc,
                                   CIRGenModule &cgm,
                                   llvm::StringRef intrinsicName,
                                   SmallVectorImpl<mlir::Value> &ops) {
  auto intTy = cast<cir::IntType>(ops[0].getType());
  unsigned numElts = intTy.getWidth();
  mlir::Value lhsVec = getMaskVecValue(builder, loc, ops[0], numElts);
  mlir::Value rhsVec = getMaskVecValue(builder, loc, ops[1], numElts);
  mlir::Type resTy = builder.getSInt32Ty();
  return cir::LLVMIntrinsicCallOp::create(
             builder, loc, cgm.getStringAttrCached(intrinsicName), resTy,
             mlir::ValueRange{lhsVec, rhsVec})
      .getResult();
}

static mlir::Value emitX86MaskedCompareResult(CIRGenBuilderTy &builder,
                                              mlir::Value cmp, unsigned numElts,
                                              mlir::Value maskIn,
                                              mlir::Location loc) {
  if (maskIn) {
    auto c = mlir::dyn_cast_or_null<cir::ConstantOp>(maskIn.getDefiningOp());
    if (!c || !c.isAllOnesValue())
      cmp = builder.createAnd(loc, cmp,
                              getMaskVecValue(builder, loc, maskIn, numElts));
  }
  if (numElts < 8) {
    llvm::SmallVector<mlir::Attribute> indices;
    for (unsigned i = 0; i != numElts; ++i)
      indices.push_back(builder.getSInt64AttrLocalCached(i));
    for (unsigned i = numElts; i != 8; ++i)
      indices.push_back(
          builder.getSInt64AttrLocalCached(i % numElts + numElts));

    // This should shuffle between cmp (first vector) and null (second vector)
    mlir::Value nullVec = builder.getNullValue(cmp.getType(), loc);
    cmp = builder.createVecShuffle(loc, cmp, nullVec, indices);
  }
  return builder.createBitcast(cmp, builder.getUIntNTy(std::max(numElts, 8U)));
}

static mlir::Value emitX86Vpmultishiftqb(CIRGenBuilderTy &builder,
                                         CIRGenModule &cgm, unsigned builtinID,
                                         ArrayRef<mlir::Value> ops,
                                         mlir::Location loc) {
  assert(ops.size() == 2 && "Expected vpmultishiftqb builtin arguments");

  llvm::StringRef intrinsicName;
  switch (builtinID) {
  default:
    llvm_unreachable("Unsupported intrinsic!");
  case X86::BI__builtin_ia32_vpmultishiftqb128:
    intrinsicName = "x86.avx512.pmultishift.qb.128";
    break;
  case X86::BI__builtin_ia32_vpmultishiftqb256:
    intrinsicName = "x86.avx512.pmultishift.qb.256";
    break;
  case X86::BI__builtin_ia32_vpmultishiftqb512:
    intrinsicName = "x86.avx512.pmultishift.qb.512";
    break;
  }

  return cir::LLVMIntrinsicCallOp::create(
             builder, loc, cgm.getStringAttrCached(intrinsicName),
             ops[0].getType(), ops)
      .getResult();
}

static mlir::Value emitX86Vpshufbitqmb(CIRGenBuilderTy &builder,
                                       CIRGenModule &cgm, unsigned builtinID,
                                       ArrayRef<mlir::Value> ops,
                                       mlir::Location loc) {
  assert(ops.size() == 3 && "Expected vpshufbitqmb builtin arguments");

  unsigned numElts = cast<cir::VectorType>(ops[0].getType()).getSize();
  llvm::StringRef intrinsicName;
  switch (builtinID) {
  default:
    llvm_unreachable("Unsupported intrinsic!");
  case X86::BI__builtin_ia32_vpshufbitqmb128_mask:
    intrinsicName = "x86.avx512.vpshufbitqmb.128";
    break;
  case X86::BI__builtin_ia32_vpshufbitqmb256_mask:
    intrinsicName = "x86.avx512.vpshufbitqmb.256";
    break;
  case X86::BI__builtin_ia32_vpshufbitqmb512_mask:
    intrinsicName = "x86.avx512.vpshufbitqmb.512";
    break;
  }

  auto resultTy = cir::VectorType::get(builder.getSIntNTy(1), numElts);
  mlir::Value shufbit = cir::LLVMIntrinsicCallOp::create(
                            builder, loc,
                            cgm.getStringAttrCached(intrinsicName), resultTy,
                            mlir::ValueRange{ops[0], ops[1]})
                            .getResult();
  return emitX86MaskedCompareResult(builder, shufbit, numElts, ops[2], loc);
}

static mlir::Value emitX86MaskedFCompareIntrinsic(CIRGenBuilderTy &builder,
                                                  CIRGenModule &cgm,
                                                  unsigned builtinID,
                                                  ArrayRef<mlir::Value> ops,
                                                  mlir::Location loc,
                                                  bool isFPConstrained) {
  assert((ops.size() == 4 || ops.size() == 5) &&
         "Unexpected number of arguments");

  unsigned numElts = cast<cir::VectorType>(ops[0].getType()).getSize();
  llvm::StringRef intrinsicName;
  switch (builtinID) {
  default:
    llvm_unreachable("Unsupported masked FP compare builtin");
  case X86::BI__builtin_ia32_cmpph128_mask:
    intrinsicName = "x86.avx512fp16.mask.cmp.ph.128";
    break;
  case X86::BI__builtin_ia32_cmpph256_mask:
    intrinsicName = "x86.avx512fp16.mask.cmp.ph.256";
    break;
  case X86::BI__builtin_ia32_cmpph512_mask:
    intrinsicName = "x86.avx512fp16.mask.cmp.ph.512";
    break;
  case X86::BI__builtin_ia32_cmpps128_mask:
    intrinsicName = "x86.avx512.mask.cmp.ps.128";
    break;
  case X86::BI__builtin_ia32_cmpps256_mask:
    intrinsicName = "x86.avx512.mask.cmp.ps.256";
    break;
  case X86::BI__builtin_ia32_cmpps512_mask:
    intrinsicName = "x86.avx512.mask.cmp.ps.512";
    break;
  case X86::BI__builtin_ia32_cmppd128_mask:
    intrinsicName = "x86.avx512.mask.cmp.pd.128";
    break;
  case X86::BI__builtin_ia32_cmppd256_mask:
    intrinsicName = "x86.avx512.mask.cmp.pd.256";
    break;
  case X86::BI__builtin_ia32_cmppd512_mask:
    intrinsicName = "x86.avx512.mask.cmp.pd.512";
    break;
  }

  auto resultTy = cir::VectorType::get(builder.getSIntNTy(1), numElts);
  SmallVector<mlir::Value, 5> args(ops.begin(), ops.end());
  args[3] = getMaskVecValue(builder, loc, args[3], numElts);

  auto call = cir::LLVMIntrinsicCallOp::create(
      builder, loc, cgm.getStringAttrCached(intrinsicName), resultTy, args);
  maybeSetCallStrictFPAttr(builder, call, isFPConstrained);
  return emitX86MaskedCompareResult(builder, call.getResult(), numElts,
                                    nullptr, loc);
}

static std::optional<mlir::Value>
emitX86MaskedCompare(CIRGenBuilderTy &builder, unsigned cc, bool isSigned,
                     ArrayRef<mlir::Value> ops, mlir::Location loc) {
  assert((ops.size() == 2 || ops.size() == 4) &&
         "Unexpected number of arguments");
  unsigned numElts = cast<cir::VectorType>(ops[0].getType()).getSize();
  mlir::Value cmp;

  if (cc == 3) {
    // cc==3: FALSE — no elements match. Return all-zero (false) vector.
    auto resultTy = cir::VectorType::get(builder.getSIntNTy(1), numElts);
    cmp = builder.getNullValue(resultTy, loc);
  } else if (cc == 7) {
    // cc==7: TRUE — all elements match. Return all-ones (true) vector.
    auto eltTy = builder.getSIntNTy(1);
    auto resultTy = cir::VectorType::get(eltTy, numElts);
    auto oneAttr = cir::IntAttr::get(eltTy, llvm::APInt::getAllOnes(1));
    llvm::SmallVector<mlir::Attribute> elems(numElts, oneAttr);
    auto vecAttr = cir::ConstVectorAttr::get(
        resultTy, mlir::ArrayAttr::get(builder.getContext(), elems));
    cmp = cir::ConstantOp::create(builder, loc, vecAttr);
  } else {
    cir::CmpOpKind pred;
    switch (cc) {
    default:
      llvm_unreachable("Unknown condition code");
    case 0:
      pred = cir::CmpOpKind::eq;
      break;
    case 1:
      pred = cir::CmpOpKind::lt;
      break;
    case 2:
      pred = cir::CmpOpKind::le;
      break;
    case 4:
      pred = cir::CmpOpKind::ne;
      break;
    case 5:
      pred = cir::CmpOpKind::ge;
      break;
    case 6:
      pred = cir::CmpOpKind::gt;
      break;
    }

    // CIR's VecCmpOp derives signed/unsigned comparison from the element
    // type's signedness. When isSigned=false, cast operands to unsigned
    // vector types so the lowering emits unsigned comparisons (ICMP_ULT
    // instead of ICMP_SLT, etc.).
    mlir::Value lhs = ops[0];
    mlir::Value rhs = ops[1];
    if (!isSigned) {
      auto vecTy = cast<cir::VectorType>(lhs.getType());
      auto eltTy = cast<cir::IntType>(vecTy.getElementType());
      if (eltTy.isSigned()) {
        auto unsignedEltTy =
            cir::IntType::get(builder.getContext(), eltTy.getWidth(), false);
        auto unsignedVecTy = cir::VectorType::get(unsignedEltTy, numElts);
        lhs = builder.createIntCast(lhs, unsignedVecTy);
        rhs = builder.createIntCast(rhs, unsignedVecTy);
      }
    }

    auto resultTy = cir::VectorType::get(builder.getSIntNTy(1), numElts);
    cmp = cir::VecCmpOp::create(builder, loc, resultTy, pred, lhs, rhs);
  }

  mlir::Value maskIn;
  if (ops.size() == 4)
    maskIn = ops[3];

  return emitX86MaskedCompareResult(builder, cmp, numElts, maskIn, loc);
}

static std::optional<mlir::Value> emitX86ConvertToMask(CIRGenBuilderTy &builder,
                                                       mlir::Value in,
                                                       mlir::Location loc) {
  cir::ConstantOp zero = builder.getNullValue(in.getType(), loc);
  return emitX86MaskedCompare(builder, 1, true, {in, zero}, loc);
}

static std::optional<mlir::Value> emitX86SExtMask(CIRGenBuilderTy &builder,
                                                  mlir::Value op,
                                                  mlir::Type dstTy,
                                                  mlir::Location loc) {
  unsigned numberOfElements = cast<cir::VectorType>(dstTy).getSize();
  mlir::Value mask = getMaskVecValue(builder, loc, op, numberOfElements);

  return builder.createCast(loc, cir::CastKind::integral, mask, dstTy);
}

static mlir::Value emitVecInsert(CIRGenBuilderTy &builder, mlir::Location loc,
                                 mlir::Value vec, mlir::Value value,
                                 mlir::Value indexOp) {
  unsigned numElts = cast<cir::VectorType>(vec.getType()).getSize();

  uint64_t index =
      indexOp.getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue();

  index &= numElts - 1;

  cir::ConstantOp indexVal = builder.getUInt64(index, loc);

  return cir::VecInsertOp::create(builder, loc, vec, value, indexVal);
}

static mlir::Value emitX86FunnelShift(CIRGenBuilderTy &builder,
                                      CIRGenModule &cgm,
                                      mlir::Location location, mlir::Value &op0,
                                      mlir::Value &op1, mlir::Value &amt,
                                      bool isRight) {
  mlir::Type op0Ty = op0.getType();

  // Amount may be scalar immediate, in which case create a splat vector.
  // Funnel shifts amounts are treated as modulo and types are all power-of-2
  // so we only care about the lowest log2 bits anyway.
  if (amt.getType() != op0Ty) {
    auto vecTy = mlir::cast<cir::VectorType>(op0Ty);
    uint64_t numElems = vecTy.getSize();

    auto amtTy = mlir::cast<cir::IntType>(amt.getType());
    auto vecElemTy = mlir::cast<cir::IntType>(vecTy.getElementType());

    // If signed, cast to the same width but unsigned first to
    // ensure zero-extension when casting to a bigger unsigned `vecElemeTy`.
    if (amtTy.isSigned()) {
      cir::IntType unsignedAmtTy = builder.getUIntNTy(amtTy.getWidth());
      amt = builder.createIntCast(amt, unsignedAmtTy);
    }
    cir::IntType unsignedVecElemType = builder.getUIntNTy(vecElemTy.getWidth());
    amt = builder.createIntCast(amt, unsignedVecElemType);
    amt = cir::VecSplatOp::create(
        builder, location, cir::VectorType::get(unsignedVecElemType, numElems),
        amt);
  }

  const StringRef intrinsicName = isRight ? "fshr" : "fshl";
  return cir::LLVMIntrinsicCallOp::create(
             builder, location, cgm.getStringAttrCached(intrinsicName), op0Ty,
             mlir::ValueRange{op0, op1, amt})
      .getResult();
}

// Emit a masked AVX-512 VPTERNLOG intrinsic. Mirrors OGCG's EmitX86Ternlog:
// selects the pternlog.{d,q}.{128,256,512} intrinsic from the vector/element
// width, calls it with (op0, op1, op2, imm), then applies the writemask against
// op0 (_mask merge) or a zero vector (_maskz).
static mlir::Value emitX86Ternlog(CIRGenBuilderTy &builder, CIRGenModule &cgm,
                                  mlir::Location loc, bool zeroMask,
                                  ArrayRef<mlir::Value> ops) {
  mlir::Type ty = ops[0].getType();
  auto vecTy = cast<cir::VectorType>(ty);
  unsigned vecWidth = cgm.getDataLayout().getTypeSizeInBits(ty);
  unsigned eltWidth =
      cast<cir::IntType>(vecTy.getElementType()).getWidth();

  StringRef intrinsicName;
  if (vecWidth == 128 && eltWidth == 32)
    intrinsicName = "x86.avx512.pternlog.d.128";
  else if (vecWidth == 256 && eltWidth == 32)
    intrinsicName = "x86.avx512.pternlog.d.256";
  else if (vecWidth == 512 && eltWidth == 32)
    intrinsicName = "x86.avx512.pternlog.d.512";
  else if (vecWidth == 128 && eltWidth == 64)
    intrinsicName = "x86.avx512.pternlog.q.128";
  else if (vecWidth == 256 && eltWidth == 64)
    intrinsicName = "x86.avx512.pternlog.q.256";
  else if (vecWidth == 512 && eltWidth == 64)
    intrinsicName = "x86.avx512.pternlog.q.512";
  else
    llvm_unreachable("Unexpected intrinsic");

  mlir::Value ternlog =
      cir::LLVMIntrinsicCallOp::create(
          builder, loc, cgm.getStringAttrCached(intrinsicName), ty,
          mlir::ValueRange{ops[0], ops[1], ops[2], ops[3]})
          .getResult();
  mlir::Value passThru = zeroMask ? builder.getNullValue(ty, loc) : ops[0];
  return emitX86Select(builder, loc, ops[4], ternlog, passThru);
}

static mlir::Value emitX86Muldq(CIRGenBuilderTy &builder, mlir::Location loc,
                                bool isSigned,
                                SmallVectorImpl<mlir::Value> &ops,
                                unsigned opTypePrimitiveSizeInBits) {
  mlir::Type ty = cir::VectorType::get(builder.getSInt64Ty(),
                                       opTypePrimitiveSizeInBits / 64);
  mlir::Value lhs = builder.createBitcast(loc, ops[0], ty);
  mlir::Value rhs = builder.createBitcast(loc, ops[1], ty);
  if (isSigned) {
    cir::ConstantOp shiftAmt =
        builder.getConstant(loc, builder.getSInt64AttrLocalCached(32));
    cir::VecSplatOp shiftSplatVecOp =
        cir::VecSplatOp::create(builder, loc, ty, shiftAmt.getResult());
    mlir::Value shiftSplatValue = shiftSplatVecOp.getResult();
    // In CIR, right-shift operations are automatically lowered to either an
    // arithmetic or logical shift depending on the operand type. The purpose
    // of the shifts here is to propagate the sign bit of the 32-bit input
    // into the upper bits of each vector lane.
    lhs = builder.createShift(loc, lhs, shiftSplatValue, true);
    lhs = builder.createShift(loc, lhs, shiftSplatValue, false);
    rhs = builder.createShift(loc, rhs, shiftSplatValue, true);
    rhs = builder.createShift(loc, rhs, shiftSplatValue, false);
  } else {
    cir::ConstantOp maskScalar = builder.getConstant(
        loc, builder.getSInt64AttrLocalCached(0xffffffff));
    cir::VecSplatOp mask =
        cir::VecSplatOp::create(builder, loc, ty, maskScalar.getResult());
    // Clear the upper bits
    lhs = builder.createAnd(loc, lhs, mask);
    rhs = builder.createAnd(loc, rhs, mask);
  }
  return builder.createMul(loc, lhs, rhs);
}

// Convert f16 half values to floats.
static mlir::Value emitX86CvtF16ToFloatExpr(CIRGenFunction &cgf,
                                            const CallExpr *expr,
                                            llvm::ArrayRef<mlir::Value> ops,
                                            mlir::Type dstTy) {
  assert((ops.size() == 1 || ops.size() == 3 || ops.size() == 4) &&
         "Unknown cvtph2ps intrinsic");

  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  // If the SAE intrinsic doesn't use default rounding then we can't upgrade.
  if (ops.size() == 4) {
    auto constOp = ops[3].getDefiningOp<cir::ConstantOp>();
    assert(constOp && "Expected constant operand");
    if (constOp.getIntValue().getZExtValue() != 4) {
      return cir::LLVMIntrinsicCallOp::create(
                 builder, loc,
                 cgm.getStringAttrCached("x86.avx512.mask.vcvtph2ps.512"),
                 dstTy, ops)
          .getResult();
    }
  }

  unsigned numElts = cast<cir::VectorType>(dstTy).getSize();
  mlir::Value src = ops[0];

  // Extract the subvector
  if (numElts != cast<cir::VectorType>(src.getType()).getSize()) {
    assert(numElts == 4 && "Unexpected vector size");
    src = builder.createVecShuffle(loc, src, {0, 1, 2, 3});
  }

  // Bitcast from vXi16 to vXf16.
  cir::VectorType halfTy =
      cir::VectorType::get(cir::FP16Type::get(builder.getContext()), numElts);

  src = builder.createCast(cir::CastKind::bitcast, src, halfTy);

  // Perform the fp-extension
  mlir::Value res = builder.createCast(cir::CastKind::floating, src, dstTy);
  cgf.maybeAttachFPConstraints(res.getDefiningOp(),
                               expr->getFPFeaturesInEffect(
                                   cgf.getLangOpts()));

  if (ops.size() >= 3)
    res = emitX86Select(builder, loc, ops[2], res, ops[1]);
  return res;
}

static mlir::Value emitX86ConvertIntToFp(CIRGenFunction &cgf,
                                         const CallExpr *expr,
                                         llvm::ArrayRef<mlir::Value> ops,
                                         bool isSigned) {
  assert(ops.size() == 4 && "Expected source, passthru, mask, rounding mode");

  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());

  auto constOp = ops[3].getDefiningOp<cir::ConstantOp>();
  assert(constOp && "Expected constant rounding operand");

  mlir::Type dstTy = ops[1].getType();
  mlir::Value res;

  if (constOp.getIntValue().getZExtValue() != 4) {
    StringRef intrinsicName =
        isSigned ? "x86.avx512.sitofp.round" : "x86.avx512.uitofp.round";
    res = cir::LLVMIntrinsicCallOp::create(
              builder, loc, cgm.getStringAttrCached(intrinsicName), dstTy,
              mlir::ValueRange{ops[0], ops[3]})
              .getResult();
  } else {
    mlir::Value src = ops[0];

    if (!isSigned) {
      auto srcTy = cast<cir::VectorType>(src.getType());
      auto srcEltTy = cast<cir::IntType>(srcTy.getElementType());
      if (srcEltTy.isSigned()) {
        auto unsignedVecTy =
            cir::VectorType::get(builder.getUIntNTy(srcEltTy.getWidth()),
                                 srcTy.getSize());
        src = builder.createIntCast(src, unsignedVecTy);
      }
    }

    res = builder.createCast(loc, cir::CastKind::int_to_float, src, dstTy);
    cgf.maybeAttachFPConstraints(res.getDefiningOp(),
                                 expr->getFPFeaturesInEffect(
                                     cgf.getLangOpts()));
  }

  return emitX86Select(builder, loc, ops[2], res, ops[1]);
}

static mlir::Value emitX86vpcom(CIRGenBuilderTy &builder, mlir::Location loc,
                                llvm::SmallVector<mlir::Value> ops,
                                bool isSigned) {
  mlir::Value op0 = ops[0];
  mlir::Value op1 = ops[1];

  cir::VectorType ty = cast<cir::VectorType>(op0.getType());
  cir::IntType elementTy = cast<cir::IntType>(ty.getElementType());

  uint64_t imm = CIRGenFunction::getZExtIntValueFromConstOp(ops[2]) & 0x7;

  cir::CmpOpKind pred;
  switch (imm) {
  case 0x0:
    pred = cir::CmpOpKind::lt;
    break;
  case 0x1:
    pred = cir::CmpOpKind::le;
    break;
  case 0x2:
    pred = cir::CmpOpKind::gt;
    break;
  case 0x3:
    pred = cir::CmpOpKind::ge;
    break;
  case 0x4:
    pred = cir::CmpOpKind::eq;
    break;
  case 0x5:
    pred = cir::CmpOpKind::ne;
    break;
  case 0x6:
    return builder.getNullValue(ty, loc); // FALSE
  case 0x7: {
    llvm::APInt allOnes = llvm::APInt::getAllOnes(elementTy.getWidth());
    return cir::VecSplatOp::create(
        builder, loc, ty,
        builder.getConstAPInt(loc, elementTy, allOnes)); // TRUE
  }
  default:
    llvm_unreachable("Unexpected XOP vpcom/vpcomu predicate");
  }

  if ((!isSigned && elementTy.isSigned()) ||
      (isSigned && elementTy.isUnsigned())) {
    elementTy = elementTy.isSigned() ? builder.getUIntNTy(elementTy.getWidth())
                                     : builder.getSIntNTy(elementTy.getWidth());
    ty = cir::VectorType::get(elementTy, ty.getSize());
    op0 = builder.createBitcast(op0, ty);
    op1 = builder.createBitcast(op1, ty);
  }

  return builder.createVecCompare(loc, pred, op0, op1);
}

static mlir::Value emitX86Fpclass(CIRGenBuilderTy &builder, CIRGenModule &cgm,
                                  mlir::Location loc, unsigned builtinID,
                                  SmallVectorImpl<mlir::Value> &ops) {
  unsigned numElts = cast<cir::VectorType>(ops[0].getType()).getSize();
  mlir::Value maskIn = ops[2];
  ops.erase(ops.begin() + 2);

  StringRef intrinsicName;
  switch (builtinID) {
  default:
    llvm_unreachable("Unsupported fpclass builtin");
  case X86::BI__builtin_ia32_vfpclassbf16128_mask:
    intrinsicName = "x86.avx10.fpclass.bf16.128";
    break;
  case X86::BI__builtin_ia32_vfpclassbf16256_mask:
    intrinsicName = "x86.avx10.fpclass.bf16.256";
    break;
  case X86::BI__builtin_ia32_vfpclassbf16512_mask:
    intrinsicName = "x86.avx10.fpclass.bf16.512";
    break;
  case X86::BI__builtin_ia32_fpclassph128_mask:
    intrinsicName = "x86.avx512fp16.fpclass.ph.128";
    break;
  case X86::BI__builtin_ia32_fpclassph256_mask:
    intrinsicName = "x86.avx512fp16.fpclass.ph.256";
    break;
  case X86::BI__builtin_ia32_fpclassph512_mask:
    intrinsicName = "x86.avx512fp16.fpclass.ph.512";
    break;
  case X86::BI__builtin_ia32_fpclassps128_mask:
    intrinsicName = "x86.avx512.fpclass.ps.128";
    break;
  case X86::BI__builtin_ia32_fpclassps256_mask:
    intrinsicName = "x86.avx512.fpclass.ps.256";
    break;
  case X86::BI__builtin_ia32_fpclassps512_mask:
    intrinsicName = "x86.avx512.fpclass.ps.512";
    break;
  case X86::BI__builtin_ia32_fpclasspd128_mask:
    intrinsicName = "x86.avx512.fpclass.pd.128";
    break;
  case X86::BI__builtin_ia32_fpclasspd256_mask:
    intrinsicName = "x86.avx512.fpclass.pd.256";
    break;
  case X86::BI__builtin_ia32_fpclasspd512_mask:
    intrinsicName = "x86.avx512.fpclass.pd.512";
    break;
  }

  auto cmpResultTy = cir::VectorType::get(builder.getSIntNTy(1), numElts);
  mlir::Value fpclass =
      cir::LLVMIntrinsicCallOp::create(
          builder, loc, cgm.getStringAttrCached(intrinsicName), cmpResultTy,
          ops)
          .getResult();
  return emitX86MaskedCompareResult(builder, fpclass, numElts, maskIn, loc);
}

static mlir::Value emitX86Aes(CIRGenFunction &cgf, CIRGenBuilderTy &builder,
                              mlir::Location loc,
                              llvm::StringRef intrinsicName, mlir::Type retType,
                              llvm::ArrayRef<mlir::Value> ops) {
  // Create return struct type and call intrinsic function.
  mlir::Type vecType =
      mlir::cast<cir::PointerType>(ops[0].getType()).getPointee();
  cir::RecordType rstRecTy = builder.getAnonRecordTy({retType, vecType});
  mlir::Value rstValueRec =
      cir::LLVMIntrinsicCallOp::create(
          builder, loc, cgf.getCIRGenModule().getStringAttrCached(intrinsicName),
          rstRecTy, mlir::ValueRange{ops[1], ops[2]})
          .getResult();

  // Extract the first return value and truncate it to 1 bit, then cast result
  // to bool value.
  mlir::Value flag =
      cir::ExtractMemberOp::create(builder, loc, rstValueRec, /*index=*/0);
  mlir::Value flagBit0 = builder.createCast(loc, cir::CastKind::integral, flag,
                                            builder.getUIntNTy(1));
  mlir::Value succ = builder.createCast(loc, cir::CastKind::int_to_bool,
                                        flagBit0, builder.getBoolTy());

  // Extract the second return value, store it to output address if success.
  mlir::Value out =
      cir::ExtractMemberOp::create(builder, loc, rstValueRec, /*index=*/1);
  Address outAddr(ops[0], /*align=*/CharUnits::fromQuantity(16));
  cir::IfOp::create(
      builder, loc, succ, /*withElseRegion=*/true,
      /*thenBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location) {
        builder.createStore(loc, out, outAddr);
        builder.createYield(loc);
      },
      /*elseBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location) {
        mlir::Value zero = builder.getNullValue(vecType, loc);
        builder.createStore(loc, zero, outAddr);
        builder.createYield(loc);
      });

  return cir::ExtractMemberOp::create(builder, loc, rstValueRec, /*index=*/0);
}

static mlir::Value emitX86Aeswide(CIRGenFunction &cgf,
                                  CIRGenBuilderTy &builder, mlir::Location loc,
                                  llvm::StringRef intrinsicName,
                                  mlir::Type retType,
                                  llvm::ArrayRef<mlir::Value> ops) {
  mlir::Type vecType =
      mlir::cast<cir::PointerType>(ops[1].getType()).getPointee();

  // Create struct for return type and load input arguments, then call
  // intrinsic function.
  mlir::Type recTypes[9] = {retType, vecType, vecType, vecType, vecType,
                            vecType, vecType, vecType, vecType};
  mlir::Value arguments[9];
  arguments[0] = ops[2];
  for (int i = 0; i < 8; i++) {
    // Loading each vector argument from input address.
    cir::ConstantOp idx = builder.getUInt32(i, loc);
    mlir::Value nextInElePtr =
        builder.getArrayElement(loc, loc, ops[1], vecType, idx,
                                /*shouldDecay=*/false);
    arguments[i + 1] =
        builder.createAlignedLoad(loc, vecType, nextInElePtr,
                                  /*align=*/CharUnits::fromQuantity(16));
  }
  cir::RecordType rstRecTy = builder.getAnonRecordTy(recTypes);
  mlir::Value rstValueRec =
      cir::LLVMIntrinsicCallOp::create(
          builder, loc, cgf.getCIRGenModule().getStringAttrCached(intrinsicName),
          rstRecTy, arguments)
          .getResult();

  // Extract the first return value and truncate it to 1 bit, then cast result
  // to bool value.
  mlir::Value flag =
      cir::ExtractMemberOp::create(builder, loc, rstValueRec, /*index=*/0);
  mlir::Value flagBit0 = builder.createCast(loc, cir::CastKind::integral, flag,
                                            builder.getUIntNTy(1));
  mlir::Value succ = builder.createCast(loc, cir::CastKind::int_to_bool,
                                        flagBit0, builder.getBoolTy());

  // Extract other return values, store those to output address if success.
  cir::IfOp::create(
      builder, loc, succ, /*withElseRegion=*/true,
      /*thenBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location) {
        for (int i = 0; i < 8; i++) {
          mlir::Value out =
              cir::ExtractMemberOp::create(builder, loc, rstValueRec,
                                           /*index=*/i + 1);
          cir::ConstantOp idx = builder.getUInt32(i, loc);
          mlir::Value nextOutEleAddr =
              builder.getArrayElement(loc, loc, ops[0], vecType, idx,
                                      /*shouldDecay=*/false);
          Address outAddr(nextOutEleAddr,
                          /*align=*/CharUnits::fromQuantity(16));
          builder.createStore(loc, out, outAddr);
        }
        builder.createYield(loc);
      },
      /*elseBuilder=*/
      [&](mlir::OpBuilder &b, mlir::Location) {
        mlir::Value zero = builder.getNullValue(vecType, loc);
        for (int i = 0; i < 8; i++) {
          cir::ConstantOp idx = builder.getUInt32(i, loc);
          mlir::Value nextOutEleAddr =
              builder.getArrayElement(loc, loc, ops[0], vecType, idx,
                                      /*shouldDecay=*/false);
          Address outAddr(nextOutEleAddr,
                          /*align=*/CharUnits::fromQuantity(16));
          builder.createStore(loc, zero, outAddr);
        }
        builder.createYield(loc);
      });

  return cir::ExtractMemberOp::create(builder, loc, rstValueRec, /*index=*/0);
}

static std::optional<CIRGenFunction::MSVCIntrin>
translateX86ToMsvcIntrin(unsigned BuiltinID) {
  using MSVCIntrin = CIRGenFunction::MSVCIntrin;
  switch (BuiltinID) {
  default:
    return std::nullopt;
  case clang::X86::BI_BitScanForward:
  case clang::X86::BI_BitScanForward64:
    return MSVCIntrin::_BitScanForward;
  case clang::X86::BI_BitScanReverse:
  case clang::X86::BI_BitScanReverse64:
    return MSVCIntrin::_BitScanReverse;
  case clang::X86::BI_InterlockedAnd64:
    return MSVCIntrin::_InterlockedAnd;
  case clang::X86::BI_InterlockedCompareExchange128:
    return MSVCIntrin::_InterlockedCompareExchange128;
  case clang::X86::BI_InterlockedExchange64:
    return MSVCIntrin::_InterlockedExchange;
  case clang::X86::BI_InterlockedExchangeAdd64:
    return MSVCIntrin::_InterlockedExchangeAdd;
  case clang::X86::BI_InterlockedExchangeSub64:
    return MSVCIntrin::_InterlockedExchangeSub;
  case clang::X86::BI_InterlockedOr64:
    return MSVCIntrin::_InterlockedOr;
  case clang::X86::BI_InterlockedXor64:
    return MSVCIntrin::_InterlockedXor;
  case clang::X86::BI_InterlockedDecrement64:
    return MSVCIntrin::_InterlockedDecrement;
  case clang::X86::BI_InterlockedIncrement64:
    return MSVCIntrin::_InterlockedIncrement;
  }
  llvm_unreachable("must return from switch");
}

std::optional<mlir::Value>
CIRGenFunction::emitX86BuiltinExpr(unsigned builtinID, const CallExpr *expr) {
  // Handle MSVC intrinsics before argument evaluation to prevent double
  // evaluation.
  if (std::optional<MSVCIntrin> MsvcIntId =
          translateX86ToMsvcIntrin(builtinID))
    return emitMSVCBuiltinExpr(*MsvcIntId, expr);

  if (builtinID == Builtin::BI__builtin_cpu_is) {
    auto loc = getLoc(expr->getSourceRange());
    const Expr *cpuExpr = expr->getArg(0)->IgnoreParenCasts();
    StringRef cpuStr = cast<clang::StringLiteral>(cpuExpr)->getString();

    auto int32Ty = cir::IntType::get(builder.getContext(), 32, true);
    auto cpuModelTy = getX86CpuModelType(getMLIRContext());

    // Get or create the __cpu_model extern global.
    getOrCreateExternGlobal(cgm, loc, "__cpu_model", cpuModelTy);

    // Map CPU name to {field_index, expected_value} using the same
    // StringSwitch as OGCG. The __cpu_model layout is:
    //   [0] = vendor, [1] = type, [2] = subtype, [3] = features[0]
    auto [index, value] =
        llvm::StringSwitch<std::pair<unsigned, unsigned>>(cpuStr)
#define X86_VENDOR(ENUM, STRING)                                               \
  .Case(STRING, {0u, static_cast<unsigned>(llvm::X86::ENUM)})
#define X86_CPU_TYPE_ALIAS(ENUM, ALIAS)                                        \
  .Case(ALIAS, {1u, static_cast<unsigned>(llvm::X86::ENUM)})
#define X86_CPU_TYPE(ENUM, STR)                                                \
  .Case(STR, {1u, static_cast<unsigned>(llvm::X86::ENUM)})
#define X86_CPU_SUBTYPE_ALIAS(ENUM, ALIAS)                                     \
  .Case(ALIAS, {2u, static_cast<unsigned>(llvm::X86::ENUM)})
#define X86_CPU_SUBTYPE(ENUM, STR)                                             \
  .Case(STR, {2u, static_cast<unsigned>(llvm::X86::ENUM)})
#include "llvm/TargetParser/X86TargetParser.def"
            .Default({0, 0});
    assert(value != 0 && "Invalid CPUStr passed to CpuIs");

    // GEP into __cpu_model[index], load, and compare with expected value.
    auto modelAddr = cir::GetGlobalOp::create(
        builder, loc, cir::PointerType::get(cpuModelTy), "__cpu_model");
    auto fieldPtr = cir::GetMemberOp::create(
        builder, loc, cir::PointerType::get(int32Ty), modelAddr,
        /*name=*/"", index);
    auto align4 = mlir::IntegerAttr::get(
        mlir::IntegerType::get(builder.getContext(), 64), 4);
    mlir::Value fieldVal = cir::LoadOp::create(
        builder, loc, fieldPtr, /*isDeref=*/false, /*isVolatile=*/false,
        /*is_nontemporal=*/false, /*alignment=*/align4,
        /*sync_scope=*/cir::SyncScopeKindAttr{},
        /*mem_order=*/cir::MemOrderAttr{}, /*tbaa=*/cir::TBAAAttr{});
    auto expectedVal = cir::ConstantOp::create(
        builder, loc, int32Ty, builder.getSInt32AttrLocalCached(value));
    mlir::Value result =
        cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq, fieldVal,
                           expectedVal);
    return result;
  }
  if (builtinID == Builtin::BI__builtin_cpu_supports) {
    auto loc = getLoc(expr->getSourceRange());
    const Expr *featureExpr = expr->getArg(0)->IgnoreParenCasts();
    StringRef featureStr =
        cast<clang::StringLiteral>(featureExpr)->getString();

    if (!getContext().getTargetInfo().validateCpuSupports(featureStr))
      return builder.getBool(false, loc);

    auto int32Ty = cir::IntType::get(builder.getContext(), 32, true);
    auto boolTy = cir::BoolType::get(builder.getContext());
    auto cpuFeatures1Ty = cir::ArrayType::get(int32Ty, 1);
    auto cpuModelTy = getX86CpuModelType(getMLIRContext());
    auto cpuFeatures2Ty = cir::ArrayType::get(int32Ty, 3);

    std::array<uint32_t, 4> featureMask =
        llvm::X86::getCpuSupportsMask({featureStr});
    auto align4 = mlir::IntegerAttr::get(
        mlir::IntegerType::get(builder.getContext(), 64), 4);

    mlir::Value result = builder.getBool(true, loc);

    // Check feature mask[0] from __cpu_model[3] (features[0]).
    if (featureMask[0] != 0) {
      getOrCreateExternGlobal(cgm, loc, "__cpu_model", cpuModelTy);
      auto modelAddr = cir::GetGlobalOp::create(
          builder, loc, cir::PointerType::get(cpuModelTy), "__cpu_model");
      auto featuresPtr = cir::GetMemberOp::create(
          builder, loc, cir::PointerType::get(cpuFeatures1Ty), modelAddr,
          /*name=*/"", /*index=*/3);
      auto idx0 = cir::ConstantOp::create(
          builder, loc, int32Ty, builder.getSInt32AttrLocalCached(0));
      auto featPtr = cir::GetElementOp::create(
          builder, loc, cir::PointerType::get(int32Ty), featuresPtr, idx0);
      mlir::Value featVal = cir::LoadOp::create(
          builder, loc, featPtr, /*isDeref=*/false, /*isVolatile=*/false,
          /*is_nontemporal=*/false, /*alignment=*/align4,
          /*sync_scope=*/cir::SyncScopeKindAttr{},
          /*mem_order=*/cir::MemOrderAttr{}, /*tbaa=*/cir::TBAAAttr{});
      auto maskVal = cir::ConstantOp::create(
          builder, loc, int32Ty,
          builder.getSInt32AttrLocalCached(featureMask[0]));
      mlir::Value andResult = cir::BinOp::create(
          builder, loc, int32Ty, cir::BinOpKind::And, featVal, maskVal, {},
          {}, {}, {}, {}, {});
      mlir::Value cmp = cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq,
                                           andResult, maskVal);
      result = cir::BinOp::create(builder, loc, boolTy, cir::BinOpKind::And,
                                  result, cmp, {}, {}, {}, {}, {}, {});
    }

    // Check feature mask[1..3] from __cpu_features2[0..2].
    bool needsFeatures2 =
        featureMask[1] != 0 || featureMask[2] != 0 || featureMask[3] != 0;
    if (needsFeatures2)
      getOrCreateExternGlobal(cgm, loc, "__cpu_features2", cpuFeatures2Ty);

    for (int i = 1; i < 4; ++i) {
      if (featureMask[i] == 0)
        continue;
      auto feat2Addr = cir::GetGlobalOp::create(
          builder, loc, cir::PointerType::get(cpuFeatures2Ty),
          "__cpu_features2");
      auto idxI = cir::ConstantOp::create(builder, loc, int32Ty,
                                          builder.getSInt32AttrLocalCached(i - 1));
      auto feat2Ptr = cir::GetElementOp::create(
          builder, loc, cir::PointerType::get(int32Ty), feat2Addr, idxI);
      mlir::Value feat2Val = cir::LoadOp::create(
          builder, loc, feat2Ptr, /*isDeref=*/false, /*isVolatile=*/false,
          /*is_nontemporal=*/false, /*alignment=*/align4,
          /*sync_scope=*/cir::SyncScopeKindAttr{},
          /*mem_order=*/cir::MemOrderAttr{}, /*tbaa=*/cir::TBAAAttr{});
      auto maskVal = cir::ConstantOp::create(
          builder, loc, int32Ty,
          builder.getSInt32AttrLocalCached(featureMask[i]));
      mlir::Value andResult = cir::BinOp::create(
          builder, loc, int32Ty, cir::BinOpKind::And, feat2Val, maskVal, {},
          {}, {}, {}, {}, {});
      mlir::Value cmp = cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq,
                                           andResult, maskVal);
      result = cir::BinOp::create(builder, loc, boolTy, cir::BinOpKind::And,
                                  result, cmp, {}, {}, {}, {}, {}, {});
    }

    return result;
  }
  if (builtinID == Builtin::BI__builtin_cpu_init) {
    auto loc = getLoc(expr->getSourceRange());
    auto voidTy = cir::VoidType::get(builder.getContext());
    auto cpuInitFnTy = cir::FuncType::get({}, voidTy, false);
    auto cpuInitFn = cgm.createRuntimeFunction(cpuInitFnTy,
                                               "__cpu_indicator_init", {},
                                               /*isLocal=*/true);
    // Match OGCG: explicitly force dso_local on __cpu_indicator_init.
    cpuInitFn.setDsoLocal(true);
    cir::CallOp::create(
        builder, loc,
        mlir::SymbolRefAttr::get(builder.getContext(),
                                 cpuInitFn.getSymName()),
        voidTy, mlir::ValueRange{});
    // Return an engaged optional with a null Value to signal "handled, void".
    return mlir::Value{};
  }

  // Handle MSVC intrinsics before argument evaluation to prevent double
  // evaluation.
  assert(!cir::MissingFeatures::msvcBuiltins());

  // The operands of the builtin call
  llvm::SmallVector<mlir::Value> ops;

  // `ICEArguments` is a bitmap indicating whether the argument at the i-th bit
  // is required to be a constant integer expression.
  unsigned iceArguments = 0;
  ASTContext::GetBuiltinTypeError error;
  getContext().GetBuiltinType(builtinID, error, &iceArguments);
  assert(error == ASTContext::GE_None && "Error while getting builtin type.");

  for (auto [idx, arg] : llvm::enumerate(expr->arguments()))
    ops.push_back(emitScalarOrConstFoldImmArg(iceArguments, idx, arg));

  CIRGenBuilderTy &builder = getBuilder();
  mlir::Type voidTy = builder.getVoidTy();

  switch (builtinID) {
  default:
    return std::nullopt;
  case X86::BI_mm_clflush:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("x86.sse2.clflush"), voidTy, ops[0])
        .getResult();
  case X86::BI_mm_lfence:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("x86.sse2.lfence"), voidTy)
        .getResult();
  case X86::BI_mm_pause:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("x86.sse2.pause"), voidTy)
        .getResult();
  case X86::BI_mm_mfence:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("x86.sse2.mfence"), voidTy)
        .getResult();
  case X86::BI_mm_sfence:
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("x86.sse.sfence"), voidTy)
        .getResult();
  case X86::BI_mm_prefetch:
  case X86::BI_m_prefetch:
  case X86::BI_m_prefetchw:
    return emitPrefetch(*this, builtinID, expr, ops);
  case X86::BI__builtin_ia32_roundps:
  case X86::BI__builtin_ia32_roundpd:
  case X86::BI__builtin_ia32_roundps256:
  case X86::BI__builtin_ia32_roundpd256: {
    auto fpExceptIgnore = cir::FPExceptionBehaviorAttr::get(
        &getMLIRContext(), cir::FPExceptionBehavior::Ignore);
    unsigned m =
        ops[1].getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue();
    // Bit 2 = use MXCSR, bit 3 = suppress exceptions.
    // If MXCSR is used or exceptions are not suppressed, use x86 intrinsic.
    if ((m & 0x4) || !(m & 0x8)) {
      StringRef name;
      switch (builtinID) {
      case X86::BI__builtin_ia32_roundps:
        name = "x86.sse41.round.ps";
        break;
      case X86::BI__builtin_ia32_roundpd:
        name = "x86.sse41.round.pd";
        break;
      case X86::BI__builtin_ia32_roundps256:
        name = "x86.avx.round.ps.256";
        break;
      case X86::BI__builtin_ia32_roundpd256:
        name = "x86.avx.round.pd.256";
        break;
      default:
        llvm_unreachable("must return from switch");
      }
      mlir::Location loc = getLoc(expr->getExprLoc());
      return cir::LLVMIntrinsicCallOp::create(
                 builder, loc, cgm.getStringAttrCached(name),
                 ops[0].getType(), ops)
          .getResult();
    }
    // When exceptions are suppressed and MXCSR is not used, lower to a
    // standard rounding intrinsic.
    StringRef name;
    switch (m & 0x3) {
    case 0:
      name = "roundeven";
      break;
    case 1:
      name = "floor";
      break;
    case 2:
      name = "ceil";
      break;
    case 3:
      name = "trunc";
      break;
    }
    mlir::Location loc = getLoc(expr->getExprLoc());
    auto roundOp =
        cir::LLVMIntrinsicCallOp::create(builder, loc,
                                         cgm.getStringAttrCached(name),
                                         ops[0].getType(), ops[0]);
    roundOp->setAttr("cir.fp_exception", fpExceptIgnore);
    return roundOp.getResult();
  }
  case X86::BI__builtin_ia32_roundss:
  case X86::BI__builtin_ia32_roundsd: {
    auto fpExceptIgnore = cir::FPExceptionBehaviorAttr::get(
        &getMLIRContext(), cir::FPExceptionBehavior::Ignore);
    unsigned m =
        ops[2].getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue();
    if ((m & 0x4) || !(m & 0x8)) {
      StringRef name = builtinID == X86::BI__builtin_ia32_roundss
                           ? "x86.sse41.round.ss"
                           : "x86.sse41.round.sd";
      mlir::Location loc = getLoc(expr->getExprLoc());
      return cir::LLVMIntrinsicCallOp::create(
                 builder, loc, cgm.getStringAttrCached(name),
                 ops[0].getType(), ops)
          .getResult();
    }
    // Extract element 0 from ops[1], round, and insert back into ops[0].
    mlir::Location loc = getLoc(expr->getExprLoc());
    auto vecTy = mlir::cast<cir::VectorType>(ops[0].getType());
    auto elemTy = vecTy.getElementType();
    auto idxTy = builder.getSInt64Ty();
    auto zeroIdx = builder.getConstInt(loc, idxTy, 0);
    mlir::Value elem = cir::VecExtractOp::create(builder, loc, elemTy,
                                                  ops[1], zeroIdx);
    StringRef name;
    switch (m & 0x3) {
    case 0:
      name = "roundeven";
      break;
    case 1:
      name = "floor";
      break;
    case 2:
      name = "ceil";
      break;
    case 3:
      name = "trunc";
      break;
    }
    auto roundOp = cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached(name), elemTy, elem);
    roundOp->setAttr("cir.fp_exception", fpExceptIgnore);
    return cir::VecInsertOp::create(builder, loc, vecTy, ops[0],
                                    roundOp.getResult(), zeroIdx);
  }
  case X86::BI__rdtsc: {
    // Return the 64-bit timestamp counter value
    mlir::Location loc = getLoc(expr->getExprLoc());
    mlir::Type u64Ty = builder.getUInt64Ty();
    return cir::LLVMIntrinsicCallOp::create(
               builder, loc, cgm.getStringAttrCached("x86.rdtsc"), u64Ty,
               mlir::ValueRange{})
        .getResult();
  }
  case X86::BI__builtin_ia32_rdtscp: {
    // Returns {i64 timestamp, i32 aux} and stores aux to the pointer argument
    mlir::Location loc = getLoc(expr->getExprLoc());
    mlir::Type u64Ty = builder.getUInt64Ty();
    mlir::Type u32Ty = builder.getUInt32Ty();

    // The intrinsic returns a struct {i64, i32}
    SmallVector<mlir::Type, 2> members = {u64Ty, u32Ty};
    auto structTy =
        cir::RecordType::get(&getMLIRContext(), members, false, false,
                             cir::RecordType::RecordKind::Struct);
    mlir::Value call =
        cir::LLVMIntrinsicCallOp::create(
            builder, loc, cgm.getStringAttrCached("x86.rdtscp"), structTy,
            mlir::ValueRange{})
            .getResult();

    // Extract and store the TSC_AUX value (second member) to the pointer
    mlir::Value tscAux =
        cir::ExtractMemberOp::create(builder, loc, u32Ty, call, /*index=*/1);
    builder.CIRBaseBuilderTy::createStore(loc, tscAux, ops[0]);

    // Return the timestamp counter (first member)
    return cir::ExtractMemberOp::create(builder, loc, u64Ty, call, /*index=*/0);
  }
  case X86::BI__builtin_ia32_lzcnt_u16:
  case X86::BI__builtin_ia32_lzcnt_u32:
  case X86::BI__builtin_ia32_lzcnt_u64: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    mlir::Value isZeroPoison = builder.getFalse(loc);
    return cir::LLVMIntrinsicCallOp::create(
               builder, loc, cgm.getStringAttrCached("ctlz"),
               ops[0].getType(), mlir::ValueRange{ops[0], isZeroPoison})
        .getResult();
  }
  case X86::BI__builtin_ia32_tzcnt_u16:
  case X86::BI__builtin_ia32_tzcnt_u32:
  case X86::BI__builtin_ia32_tzcnt_u64: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    mlir::Value isZeroPoison = builder.getFalse(loc);
    return cir::LLVMIntrinsicCallOp::create(
               builder, loc, cgm.getStringAttrCached("cttz"),
               ops[0].getType(), mlir::ValueRange{ops[0], isZeroPoison})
        .getResult();
  }
  case X86::BI__builtin_ia32_undef128:
  case X86::BI__builtin_ia32_undef256:
  case X86::BI__builtin_ia32_undef512:
    // The x86 definition of "undef" is not the same as the LLVM definition
    // (PR32176). We leave optimizing away an unnecessary zero constant to the
    // IR optimizer and backend.
    // TODO: If we had a "freeze" IR instruction to generate a fixed undef
    //  value, we should use that here instead of a zero.
    return builder.getNullValue(convertType(expr->getType()),
                                getLoc(expr->getExprLoc()));
  case X86::BI__builtin_ia32_vec_ext_v4hi:
  case X86::BI__builtin_ia32_vec_ext_v16qi:
  case X86::BI__builtin_ia32_vec_ext_v8hi:
  case X86::BI__builtin_ia32_vec_ext_v4si:
  case X86::BI__builtin_ia32_vec_ext_v4sf:
  case X86::BI__builtin_ia32_vec_ext_v2di:
  case X86::BI__builtin_ia32_vec_ext_v32qi:
  case X86::BI__builtin_ia32_vec_ext_v16hi:
  case X86::BI__builtin_ia32_vec_ext_v8si:
  case X86::BI__builtin_ia32_vec_ext_v4di: {
    unsigned numElts = cast<cir::VectorType>(ops[0].getType()).getSize();

    uint64_t index = getZExtIntValueFromConstOp(ops[1]);
    index &= numElts - 1;

    cir::ConstantOp indexVal =
        builder.getUInt64(index, getLoc(expr->getExprLoc()));

    // These builtins exist so we can ensure the index is an ICE and in range.
    // Otherwise we could just do this in the header file.
    return cir::VecExtractOp::create(builder, getLoc(expr->getExprLoc()),
                                     ops[0], indexVal);
  }
  case X86::BI__builtin_ia32_vec_set_v4hi:
  case X86::BI__builtin_ia32_vec_set_v16qi:
  case X86::BI__builtin_ia32_vec_set_v8hi:
  case X86::BI__builtin_ia32_vec_set_v4si:
  case X86::BI__builtin_ia32_vec_set_v2di:
  case X86::BI__builtin_ia32_vec_set_v32qi:
  case X86::BI__builtin_ia32_vec_set_v16hi:
  case X86::BI__builtin_ia32_vec_set_v8si:
  case X86::BI__builtin_ia32_vec_set_v4di: {
    return emitVecInsert(builder, getLoc(expr->getExprLoc()), ops[0], ops[1],
                         ops[2]);
  }
  case X86::BI__builtin_ia32_kunpckhi:
    return emitX86MaskUnpack(builder, getLoc(expr->getExprLoc()),
                             "x86.avx512.kunpackb", ops);
  case X86::BI__builtin_ia32_kunpcksi:
    return emitX86MaskUnpack(builder, getLoc(expr->getExprLoc()),
                             "x86.avx512.kunpackw", ops);
  case X86::BI__builtin_ia32_kunpckdi:
    return emitX86MaskUnpack(builder, getLoc(expr->getExprLoc()),
                             "x86.avx512.kunpackd", ops);
  case X86::BI_mm_setcsr:
  case X86::BI__builtin_ia32_ldmxcsr: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    Address tmp = createMemTemp(expr->getArg(0)->getType(), loc);
    builder.createStore(loc, ops[0], tmp);
    return cir::LLVMIntrinsicCallOp::create(
               builder, loc, cgm.getStringAttrCached("x86.sse.ldmxcsr"),
               builder.getVoidTy(), tmp.getPointer())
        .getResult();
  }
  case X86::BI_mm_getcsr:
  case X86::BI__builtin_ia32_stmxcsr: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    Address tmp = createMemTemp(expr->getType(), loc);
    cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("x86.sse.stmxcsr"),
        builder.getVoidTy(), tmp.getPointer());
    return builder.createLoad(loc, tmp);
  }
  case X86::BI__builtin_ia32_xsave:
  case X86::BI__builtin_ia32_xsave64:
  case X86::BI__builtin_ia32_xrstor:
  case X86::BI__builtin_ia32_xrstor64:
  case X86::BI__builtin_ia32_xsaveopt:
  case X86::BI__builtin_ia32_xsaveopt64:
  case X86::BI__builtin_ia32_xrstors:
  case X86::BI__builtin_ia32_xrstors64:
  case X86::BI__builtin_ia32_xsavec:
  case X86::BI__builtin_ia32_xsavec64:
  case X86::BI__builtin_ia32_xsaves:
  case X86::BI__builtin_ia32_xsaves64:
  case X86::BI__builtin_ia32_xsetbv:
  case X86::BI_xsetbv: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    StringRef intrinsicName;
    switch (builtinID) {
    default:
      llvm_unreachable("Unexpected builtin");
    case X86::BI__builtin_ia32_xsave:
      intrinsicName = "x86.xsave";
      break;
    case X86::BI__builtin_ia32_xsave64:
      intrinsicName = "x86.xsave64";
      break;
    case X86::BI__builtin_ia32_xrstor:
      intrinsicName = "x86.xrstor";
      break;
    case X86::BI__builtin_ia32_xrstor64:
      intrinsicName = "x86.xrstor64";
      break;
    case X86::BI__builtin_ia32_xsaveopt:
      intrinsicName = "x86.xsaveopt";
      break;
    case X86::BI__builtin_ia32_xsaveopt64:
      intrinsicName = "x86.xsaveopt64";
      break;
    case X86::BI__builtin_ia32_xrstors:
      intrinsicName = "x86.xrstors";
      break;
    case X86::BI__builtin_ia32_xrstors64:
      intrinsicName = "x86.xrstors64";
      break;
    case X86::BI__builtin_ia32_xsavec:
      intrinsicName = "x86.xsavec";
      break;
    case X86::BI__builtin_ia32_xsavec64:
      intrinsicName = "x86.xsavec64";
      break;
    case X86::BI__builtin_ia32_xsaves:
      intrinsicName = "x86.xsaves";
      break;
    case X86::BI__builtin_ia32_xsaves64:
      intrinsicName = "x86.xsaves64";
      break;
    case X86::BI__builtin_ia32_xsetbv:
    case X86::BI_xsetbv:
      intrinsicName = "x86.xsetbv";
      break;
    }

    // The xsave family of instructions take a 64-bit mask that specifies
    // which processor state components to save/restore. The hardware expects
    // this mask split into two 32-bit registers: EDX (high 32 bits) and
    // EAX (low 32 bits).
    mlir::Type i32Ty = builder.getSInt32Ty();

    // Mhi = (uint32_t)(ops[1] >> 32) - extract high 32 bits via right shift
    cir::ConstantOp shift32 = builder.getSInt64(32, loc);
    mlir::Value mhi = builder.createShift(loc, ops[1], shift32.getResult(),
                                          /*isShiftLeft=*/false);
    mhi = builder.createIntCast(mhi, i32Ty);

    // Mlo = (uint32_t)ops[1] - extract low 32 bits by truncation
    mlir::Value mlo = builder.createIntCast(ops[1], i32Ty);

    return cir::LLVMIntrinsicCallOp::create(
               builder, loc, cgm.getStringAttrCached(intrinsicName), voidTy,
               mlir::ValueRange{ops[0], mhi, mlo})
        .getResult();
  }
  case X86::BI__builtin_ia32_xgetbv:
  case X86::BI_xgetbv:
    // xgetbv reads the extended control register specified by ops[0] (ECX)
    // and returns the 64-bit value
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("x86.xgetbv"), builder.getUInt64Ty(),
               ops[0])
        .getResult();
  case X86::BI__builtin_ia32_storedqudi128_mask:
  case X86::BI__builtin_ia32_storedqusi128_mask:
  case X86::BI__builtin_ia32_storedquhi128_mask:
  case X86::BI__builtin_ia32_storedquqi128_mask:
  case X86::BI__builtin_ia32_storeupd128_mask:
  case X86::BI__builtin_ia32_storeups128_mask:
  case X86::BI__builtin_ia32_storedqudi256_mask:
  case X86::BI__builtin_ia32_storedqusi256_mask:
  case X86::BI__builtin_ia32_storedquhi256_mask:
  case X86::BI__builtin_ia32_storedquqi256_mask:
  case X86::BI__builtin_ia32_storeupd256_mask:
  case X86::BI__builtin_ia32_storeups256_mask:
  case X86::BI__builtin_ia32_storedqudi512_mask:
  case X86::BI__builtin_ia32_storedqusi512_mask:
  case X86::BI__builtin_ia32_storedquhi512_mask:
  case X86::BI__builtin_ia32_storedquqi512_mask:
  case X86::BI__builtin_ia32_storeupd512_mask:
  case X86::BI__builtin_ia32_storeups512_mask:
  case X86::BI__builtin_ia32_storesbf16128_mask:
  case X86::BI__builtin_ia32_storesh128_mask:
  case X86::BI__builtin_ia32_storess128_mask:
  case X86::BI__builtin_ia32_storesd128_mask:
    return emitX86MaskedStore(*this, ops, llvm::Align(1),
                              getLoc(expr->getExprLoc()));
  case X86::BI__builtin_ia32_cvtmask2b128:
  case X86::BI__builtin_ia32_cvtmask2b256:
  case X86::BI__builtin_ia32_cvtmask2b512:
  case X86::BI__builtin_ia32_cvtmask2w128:
  case X86::BI__builtin_ia32_cvtmask2w256:
  case X86::BI__builtin_ia32_cvtmask2w512:
  case X86::BI__builtin_ia32_cvtmask2d128:
  case X86::BI__builtin_ia32_cvtmask2d256:
  case X86::BI__builtin_ia32_cvtmask2d512:
  case X86::BI__builtin_ia32_cvtmask2q128:
  case X86::BI__builtin_ia32_cvtmask2q256:
  case X86::BI__builtin_ia32_cvtmask2q512:
    return emitX86SExtMask(this->getBuilder(), ops[0],
                           convertType(expr->getType()),
                           getLoc(expr->getExprLoc()));
  case X86::BI__builtin_ia32_cvtb2mask128:
  case X86::BI__builtin_ia32_cvtb2mask256:
  case X86::BI__builtin_ia32_cvtb2mask512:
  case X86::BI__builtin_ia32_cvtw2mask128:
  case X86::BI__builtin_ia32_cvtw2mask256:
  case X86::BI__builtin_ia32_cvtw2mask512:
  case X86::BI__builtin_ia32_cvtd2mask128:
  case X86::BI__builtin_ia32_cvtd2mask256:
  case X86::BI__builtin_ia32_cvtd2mask512:
  case X86::BI__builtin_ia32_cvtq2mask128:
  case X86::BI__builtin_ia32_cvtq2mask256:
  case X86::BI__builtin_ia32_cvtq2mask512:
    return emitX86ConvertToMask(this->getBuilder(), ops[0],
                                getLoc(expr->getExprLoc()));
  case X86::BI__builtin_ia32_cvtdq2ps512_mask:
  case X86::BI__builtin_ia32_cvtqq2ps512_mask:
  case X86::BI__builtin_ia32_cvtqq2pd512_mask:
  case X86::BI__builtin_ia32_vcvtw2ph512_mask:
  case X86::BI__builtin_ia32_vcvtdq2ph512_mask:
  case X86::BI__builtin_ia32_vcvtqq2ph512_mask:
    return emitX86ConvertIntToFp(*this, expr, ops, /*isSigned=*/true);
  case X86::BI__builtin_ia32_cvtudq2ps512_mask:
  case X86::BI__builtin_ia32_cvtuqq2ps512_mask:
  case X86::BI__builtin_ia32_cvtuqq2pd512_mask:
  case X86::BI__builtin_ia32_vcvtuw2ph512_mask:
  case X86::BI__builtin_ia32_vcvtudq2ph512_mask:
  case X86::BI__builtin_ia32_vcvtuqq2ph512_mask:
    return emitX86ConvertIntToFp(*this, expr, ops, /*isSigned=*/false);
  // Scalar FMA with masking
  case X86::BI__builtin_ia32_vfmaddsh3_mask:
  case X86::BI__builtin_ia32_vfmaddss3_mask:
  case X86::BI__builtin_ia32_vfmaddsd3_mask:
    return emitScalarFMAExpr(builder, cgm, getLoc(expr->getExprLoc()), ops,
                             ops[0]);
  case X86::BI__builtin_ia32_vfmaddsh3_maskz:
  case X86::BI__builtin_ia32_vfmaddss3_maskz:
  case X86::BI__builtin_ia32_vfmaddsd3_maskz:
    return emitScalarFMAExpr(builder, cgm, getLoc(expr->getExprLoc()), ops,
                             ops[0], /*zeroMask=*/true);
  case X86::BI__builtin_ia32_vfmaddsh3_mask3:
  case X86::BI__builtin_ia32_vfmaddss3_mask3:
  case X86::BI__builtin_ia32_vfmaddsd3_mask3:
    return emitScalarFMAExpr(builder, cgm, getLoc(expr->getExprLoc()), ops,
                             ops[2], /*zeroMask=*/false, /*ptIdx=*/2);
  case X86::BI__builtin_ia32_vfmsubsh3_mask3:
  case X86::BI__builtin_ia32_vfmsubss3_mask3:
  case X86::BI__builtin_ia32_vfmsubsd3_mask3:
    return emitScalarFMAExpr(builder, cgm, getLoc(expr->getExprLoc()), ops,
                             ops[2], /*zeroMask=*/false, /*ptIdx=*/2,
                             /*negAcc=*/true);
  // Packed 512-bit FMA with masking (non-addsub)
  case X86::BI__builtin_ia32_vfmaddph512_mask:
  case X86::BI__builtin_ia32_vfmaddph512_maskz:
  case X86::BI__builtin_ia32_vfmaddph512_mask3:
  case X86::BI__builtin_ia32_vfmsubph512_mask3:
  case X86::BI__builtin_ia32_vfmaddps512_mask:
  case X86::BI__builtin_ia32_vfmaddps512_maskz:
  case X86::BI__builtin_ia32_vfmaddps512_mask3:
  case X86::BI__builtin_ia32_vfmsubps512_mask3:
  case X86::BI__builtin_ia32_vfmaddpd512_mask:
  case X86::BI__builtin_ia32_vfmaddpd512_maskz:
  case X86::BI__builtin_ia32_vfmaddpd512_mask3:
  case X86::BI__builtin_ia32_vfmsubpd512_mask3:
    return emitX86FMAExpr(builder, cgm, getLoc(expr->getExprLoc()), ops,
                          builtinID, /*isAddSub=*/false);
  // Packed 512-bit FMA with masking (addsub/subadd)
  case X86::BI__builtin_ia32_vfmaddsubph512_mask:
  case X86::BI__builtin_ia32_vfmaddsubph512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubph512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddph512_mask3:
  case X86::BI__builtin_ia32_vfmaddsubps512_mask:
  case X86::BI__builtin_ia32_vfmaddsubps512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubps512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddps512_mask3:
  case X86::BI__builtin_ia32_vfmaddsubpd512_mask:
  case X86::BI__builtin_ia32_vfmaddsubpd512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubpd512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddpd512_mask3:
    return emitX86FMAExpr(builder, cgm, getLoc(expr->getExprLoc()), ops,
                          builtinID, /*isAddSub=*/true);
  case X86::BI__builtin_ia32_movdqa32store128_mask:
  case X86::BI__builtin_ia32_movdqa64store128_mask:
  case X86::BI__builtin_ia32_storeaps128_mask:
  case X86::BI__builtin_ia32_storeapd128_mask:
  case X86::BI__builtin_ia32_movdqa32store256_mask:
  case X86::BI__builtin_ia32_movdqa64store256_mask:
  case X86::BI__builtin_ia32_storeaps256_mask:
  case X86::BI__builtin_ia32_storeapd256_mask:
  case X86::BI__builtin_ia32_movdqa32store512_mask:
  case X86::BI__builtin_ia32_movdqa64store512_mask:
  case X86::BI__builtin_ia32_storeaps512_mask:
  case X86::BI__builtin_ia32_storeapd512_mask:
    return emitX86MaskedStore(
        *this, ops,
        getContext()
            .getTypeAlignInChars(expr->getArg(1)->getType())
            .getAsAlign(),
        getLoc(expr->getExprLoc()));
  case X86::BI__builtin_ia32_loadups128_mask:
  case X86::BI__builtin_ia32_loadups256_mask:
  case X86::BI__builtin_ia32_loadups512_mask:
  case X86::BI__builtin_ia32_loadupd128_mask:
  case X86::BI__builtin_ia32_loadupd256_mask:
  case X86::BI__builtin_ia32_loadupd512_mask:
  case X86::BI__builtin_ia32_loaddquqi128_mask:
  case X86::BI__builtin_ia32_loaddquqi256_mask:
  case X86::BI__builtin_ia32_loaddquqi512_mask:
  case X86::BI__builtin_ia32_loaddquhi128_mask:
  case X86::BI__builtin_ia32_loaddquhi256_mask:
  case X86::BI__builtin_ia32_loaddquhi512_mask:
  case X86::BI__builtin_ia32_loaddqusi128_mask:
  case X86::BI__builtin_ia32_loaddqusi256_mask:
  case X86::BI__builtin_ia32_loaddqusi512_mask:
  case X86::BI__builtin_ia32_loaddqudi128_mask:
  case X86::BI__builtin_ia32_loaddqudi256_mask:
  case X86::BI__builtin_ia32_loaddqudi512_mask:
    return emitX86MaskedLoad(*this, ops, llvm::Align(1),
                             getLoc(expr->getExprLoc()));
  case X86::BI__builtin_ia32_loadsbf16128_mask:
  case X86::BI__builtin_ia32_loadsh128_mask:
  case X86::BI__builtin_ia32_loadss128_mask:
  case X86::BI__builtin_ia32_loadsd128_mask:
    return emitX86MaskedLoad(*this, ops, llvm::Align(1),
                             getLoc(expr->getExprLoc()));
  case X86::BI__builtin_ia32_loadaps128_mask:
  case X86::BI__builtin_ia32_loadaps256_mask:
  case X86::BI__builtin_ia32_loadaps512_mask:
  case X86::BI__builtin_ia32_loadapd128_mask:
  case X86::BI__builtin_ia32_loadapd256_mask:
  case X86::BI__builtin_ia32_loadapd512_mask:
  case X86::BI__builtin_ia32_movdqa32load128_mask:
  case X86::BI__builtin_ia32_movdqa32load256_mask:
  case X86::BI__builtin_ia32_movdqa32load512_mask:
  case X86::BI__builtin_ia32_movdqa64load128_mask:
  case X86::BI__builtin_ia32_movdqa64load256_mask:
  case X86::BI__builtin_ia32_movdqa64load512_mask:
    return emitX86MaskedLoad(
        *this, ops,
        getContext()
            .getTypeAlignInChars(expr->getArg(1)->getType())
            .getAsAlign(),
        getLoc(expr->getExprLoc()));
  case X86::BI__builtin_ia32_expandloaddf128_mask:
  case X86::BI__builtin_ia32_expandloaddf256_mask:
  case X86::BI__builtin_ia32_expandloaddf512_mask:
  case X86::BI__builtin_ia32_expandloadsf128_mask:
  case X86::BI__builtin_ia32_expandloadsf256_mask:
  case X86::BI__builtin_ia32_expandloadsf512_mask:
  case X86::BI__builtin_ia32_expandloaddi128_mask:
  case X86::BI__builtin_ia32_expandloaddi256_mask:
  case X86::BI__builtin_ia32_expandloaddi512_mask:
  case X86::BI__builtin_ia32_expandloadsi128_mask:
  case X86::BI__builtin_ia32_expandloadsi256_mask:
  case X86::BI__builtin_ia32_expandloadsi512_mask:
  case X86::BI__builtin_ia32_expandloadhi128_mask:
  case X86::BI__builtin_ia32_expandloadhi256_mask:
  case X86::BI__builtin_ia32_expandloadhi512_mask:
  case X86::BI__builtin_ia32_expandloadqi128_mask:
  case X86::BI__builtin_ia32_expandloadqi256_mask:
  case X86::BI__builtin_ia32_expandloadqi512_mask:
    return emitX86ExpandLoad(*this, ops, getLoc(expr->getExprLoc()));
  case X86::BI__builtin_ia32_compressstoredf128_mask:
  case X86::BI__builtin_ia32_compressstoredf256_mask:
  case X86::BI__builtin_ia32_compressstoredf512_mask:
  case X86::BI__builtin_ia32_compressstoresf128_mask:
  case X86::BI__builtin_ia32_compressstoresf256_mask:
  case X86::BI__builtin_ia32_compressstoresf512_mask:
  case X86::BI__builtin_ia32_compressstoredi128_mask:
  case X86::BI__builtin_ia32_compressstoredi256_mask:
  case X86::BI__builtin_ia32_compressstoredi512_mask:
  case X86::BI__builtin_ia32_compressstoresi128_mask:
  case X86::BI__builtin_ia32_compressstoresi256_mask:
  case X86::BI__builtin_ia32_compressstoresi512_mask:
  case X86::BI__builtin_ia32_compressstorehi128_mask:
  case X86::BI__builtin_ia32_compressstorehi256_mask:
  case X86::BI__builtin_ia32_compressstorehi512_mask:
  case X86::BI__builtin_ia32_compressstoreqi128_mask:
  case X86::BI__builtin_ia32_compressstoreqi256_mask:
  case X86::BI__builtin_ia32_compressstoreqi512_mask:
    return emitX86CompressStore(*this, ops, getLoc(expr->getExprLoc()));
  case X86::BI__builtin_ia32_expanddf128_mask:
  case X86::BI__builtin_ia32_expanddf256_mask:
  case X86::BI__builtin_ia32_expanddf512_mask:
  case X86::BI__builtin_ia32_expandsf128_mask:
  case X86::BI__builtin_ia32_expandsf256_mask:
  case X86::BI__builtin_ia32_expandsf512_mask:
  case X86::BI__builtin_ia32_expanddi128_mask:
  case X86::BI__builtin_ia32_expanddi256_mask:
  case X86::BI__builtin_ia32_expanddi512_mask:
  case X86::BI__builtin_ia32_expandsi128_mask:
  case X86::BI__builtin_ia32_expandsi256_mask:
  case X86::BI__builtin_ia32_expandsi512_mask:
  case X86::BI__builtin_ia32_expandhi128_mask:
  case X86::BI__builtin_ia32_expandhi256_mask:
  case X86::BI__builtin_ia32_expandhi512_mask:
  case X86::BI__builtin_ia32_expandqi128_mask:
  case X86::BI__builtin_ia32_expandqi256_mask:
  case X86::BI__builtin_ia32_expandqi512_mask: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    return emitX86CompressExpand(builder, loc, ops[0], ops[1], ops[2], cgm,
                                 "x86.avx512.mask.expand");
  }
  case X86::BI__builtin_ia32_compressdf128_mask:
  case X86::BI__builtin_ia32_compressdf256_mask:
  case X86::BI__builtin_ia32_compressdf512_mask:
  case X86::BI__builtin_ia32_compresssf128_mask:
  case X86::BI__builtin_ia32_compresssf256_mask:
  case X86::BI__builtin_ia32_compresssf512_mask:
  case X86::BI__builtin_ia32_compressdi128_mask:
  case X86::BI__builtin_ia32_compressdi256_mask:
  case X86::BI__builtin_ia32_compressdi512_mask:
  case X86::BI__builtin_ia32_compresssi128_mask:
  case X86::BI__builtin_ia32_compresssi256_mask:
  case X86::BI__builtin_ia32_compresssi512_mask:
  case X86::BI__builtin_ia32_compresshi128_mask:
  case X86::BI__builtin_ia32_compresshi256_mask:
  case X86::BI__builtin_ia32_compresshi512_mask:
  case X86::BI__builtin_ia32_compressqi128_mask:
  case X86::BI__builtin_ia32_compressqi256_mask:
  case X86::BI__builtin_ia32_compressqi512_mask: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    return emitX86CompressExpand(builder, loc, ops[0], ops[1], ops[2], cgm,
                                 "x86.avx512.mask.compress");
  }
  case X86::BI__builtin_ia32_gather3div2df:
  case X86::BI__builtin_ia32_gather3div2di:
  case X86::BI__builtin_ia32_gather3div4df:
  case X86::BI__builtin_ia32_gather3div4di:
  case X86::BI__builtin_ia32_gather3div4sf:
  case X86::BI__builtin_ia32_gather3div4si:
  case X86::BI__builtin_ia32_gather3div8sf:
  case X86::BI__builtin_ia32_gather3div8si:
  case X86::BI__builtin_ia32_gather3siv2df:
  case X86::BI__builtin_ia32_gather3siv2di:
  case X86::BI__builtin_ia32_gather3siv4df:
  case X86::BI__builtin_ia32_gather3siv4di:
  case X86::BI__builtin_ia32_gather3siv4sf:
  case X86::BI__builtin_ia32_gather3siv4si:
  case X86::BI__builtin_ia32_gather3siv8sf:
  case X86::BI__builtin_ia32_gather3siv8si:
  case X86::BI__builtin_ia32_gathersiv8df:
  case X86::BI__builtin_ia32_gathersiv16sf:
  case X86::BI__builtin_ia32_gatherdiv8df:
  case X86::BI__builtin_ia32_gatherdiv16sf:
  case X86::BI__builtin_ia32_gathersiv8di:
  case X86::BI__builtin_ia32_gathersiv16si:
  case X86::BI__builtin_ia32_gatherdiv8di:
  case X86::BI__builtin_ia32_gatherdiv16si: {
    StringRef intrinsicName;
    switch (builtinID) {
    default:
      llvm_unreachable("Unexpected builtin");
    case X86::BI__builtin_ia32_gather3div2df:
      intrinsicName = "x86.avx512.mask.gather3div2.df";
      break;
    case X86::BI__builtin_ia32_gather3div2di:
      intrinsicName = "x86.avx512.mask.gather3div2.di";
      break;
    case X86::BI__builtin_ia32_gather3div4df:
      intrinsicName = "x86.avx512.mask.gather3div4.df";
      break;
    case X86::BI__builtin_ia32_gather3div4di:
      intrinsicName = "x86.avx512.mask.gather3div4.di";
      break;
    case X86::BI__builtin_ia32_gather3div4sf:
      intrinsicName = "x86.avx512.mask.gather3div4.sf";
      break;
    case X86::BI__builtin_ia32_gather3div4si:
      intrinsicName = "x86.avx512.mask.gather3div4.si";
      break;
    case X86::BI__builtin_ia32_gather3div8sf:
      intrinsicName = "x86.avx512.mask.gather3div8.sf";
      break;
    case X86::BI__builtin_ia32_gather3div8si:
      intrinsicName = "x86.avx512.mask.gather3div8.si";
      break;
    case X86::BI__builtin_ia32_gather3siv2df:
      intrinsicName = "x86.avx512.mask.gather3siv2.df";
      break;
    case X86::BI__builtin_ia32_gather3siv2di:
      intrinsicName = "x86.avx512.mask.gather3siv2.di";
      break;
    case X86::BI__builtin_ia32_gather3siv4df:
      intrinsicName = "x86.avx512.mask.gather3siv4.df";
      break;
    case X86::BI__builtin_ia32_gather3siv4di:
      intrinsicName = "x86.avx512.mask.gather3siv4.di";
      break;
    case X86::BI__builtin_ia32_gather3siv4sf:
      intrinsicName = "x86.avx512.mask.gather3siv4.sf";
      break;
    case X86::BI__builtin_ia32_gather3siv4si:
      intrinsicName = "x86.avx512.mask.gather3siv4.si";
      break;
    case X86::BI__builtin_ia32_gather3siv8sf:
      intrinsicName = "x86.avx512.mask.gather3siv8.sf";
      break;
    case X86::BI__builtin_ia32_gather3siv8si:
      intrinsicName = "x86.avx512.mask.gather3siv8.si";
      break;
    case X86::BI__builtin_ia32_gathersiv8df:
      intrinsicName = "x86.avx512.mask.gather.dpd.512";
      break;
    case X86::BI__builtin_ia32_gathersiv16sf:
      intrinsicName = "x86.avx512.mask.gather.dps.512";
      break;
    case X86::BI__builtin_ia32_gatherdiv8df:
      intrinsicName = "x86.avx512.mask.gather.qpd.512";
      break;
    case X86::BI__builtin_ia32_gatherdiv16sf:
      intrinsicName = "x86.avx512.mask.gather.qps.512";
      break;
    case X86::BI__builtin_ia32_gathersiv8di:
      intrinsicName = "x86.avx512.mask.gather.dpq.512";
      break;
    case X86::BI__builtin_ia32_gathersiv16si:
      intrinsicName = "x86.avx512.mask.gather.dpi.512";
      break;
    case X86::BI__builtin_ia32_gatherdiv8di:
      intrinsicName = "x86.avx512.mask.gather.qpq.512";
      break;
    case X86::BI__builtin_ia32_gatherdiv16si:
      intrinsicName = "x86.avx512.mask.gather.qpi.512";
      break;
    }

    mlir::Location loc = getLoc(expr->getExprLoc());
    unsigned minElts =
        std::min(cast<cir::VectorType>(ops[0].getType()).getSize(),
                 cast<cir::VectorType>(ops[2].getType()).getSize());
    ops[3] = getMaskVecValue(builder, loc, ops[3], minElts);
    return cir::LLVMIntrinsicCallOp::create(
               builder, loc, cgm.getStringAttrCached(intrinsicName),
               convertType(expr->getType()), ops)
        .getResult();
  }
  case X86::BI__builtin_ia32_scattersiv8df:
  case X86::BI__builtin_ia32_scattersiv16sf:
  case X86::BI__builtin_ia32_scatterdiv8df:
  case X86::BI__builtin_ia32_scatterdiv16sf:
  case X86::BI__builtin_ia32_scattersiv8di:
  case X86::BI__builtin_ia32_scattersiv16si:
  case X86::BI__builtin_ia32_scatterdiv8di:
  case X86::BI__builtin_ia32_scatterdiv16si:
  case X86::BI__builtin_ia32_scatterdiv2df:
  case X86::BI__builtin_ia32_scatterdiv2di:
  case X86::BI__builtin_ia32_scatterdiv4df:
  case X86::BI__builtin_ia32_scatterdiv4di:
  case X86::BI__builtin_ia32_scatterdiv4sf:
  case X86::BI__builtin_ia32_scatterdiv4si:
  case X86::BI__builtin_ia32_scatterdiv8sf:
  case X86::BI__builtin_ia32_scatterdiv8si:
  case X86::BI__builtin_ia32_scattersiv2df:
  case X86::BI__builtin_ia32_scattersiv2di:
  case X86::BI__builtin_ia32_scattersiv4df:
  case X86::BI__builtin_ia32_scattersiv4di:
  case X86::BI__builtin_ia32_scattersiv4sf:
  case X86::BI__builtin_ia32_scattersiv4si:
  case X86::BI__builtin_ia32_scattersiv8sf:
  case X86::BI__builtin_ia32_scattersiv8si: {
    llvm::StringRef intrinsicName;
    switch (builtinID) {
    default:
      llvm_unreachable("Unexpected builtin");
    case X86::BI__builtin_ia32_scattersiv8df:
      intrinsicName = "x86.avx512.mask.scatter.dpd.512";
      break;
    case X86::BI__builtin_ia32_scattersiv16sf:
      intrinsicName = "x86.avx512.mask.scatter.dps.512";
      break;
    case X86::BI__builtin_ia32_scatterdiv8df:
      intrinsicName = "x86.avx512.mask.scatter.qpd.512";
      break;
    case X86::BI__builtin_ia32_scatterdiv16sf:
      intrinsicName = "x86.avx512.mask.scatter.qps.512";
      break;
    case X86::BI__builtin_ia32_scattersiv8di:
      intrinsicName = "x86.avx512.mask.scatter.dpq.512";
      break;
    case X86::BI__builtin_ia32_scattersiv16si:
      intrinsicName = "x86.avx512.mask.scatter.dpi.512";
      break;
    case X86::BI__builtin_ia32_scatterdiv8di:
      intrinsicName = "x86.avx512.mask.scatter.qpq.512";
      break;
    case X86::BI__builtin_ia32_scatterdiv16si:
      intrinsicName = "x86.avx512.mask.scatter.qpi.512";
      break;
    case X86::BI__builtin_ia32_scatterdiv2df:
      intrinsicName = "x86.avx512.mask.scatterdiv2.df";
      break;
    case X86::BI__builtin_ia32_scatterdiv2di:
      intrinsicName = "x86.avx512.mask.scatterdiv2.di";
      break;
    case X86::BI__builtin_ia32_scatterdiv4df:
      intrinsicName = "x86.avx512.mask.scatterdiv4.df";
      break;
    case X86::BI__builtin_ia32_scatterdiv4di:
      intrinsicName = "x86.avx512.mask.scatterdiv4.di";
      break;
    case X86::BI__builtin_ia32_scatterdiv4sf:
      intrinsicName = "x86.avx512.mask.scatterdiv4.sf";
      break;
    case X86::BI__builtin_ia32_scatterdiv4si:
      intrinsicName = "x86.avx512.mask.scatterdiv4.si";
      break;
    case X86::BI__builtin_ia32_scatterdiv8sf:
      intrinsicName = "x86.avx512.mask.scatterdiv8.sf";
      break;
    case X86::BI__builtin_ia32_scatterdiv8si:
      intrinsicName = "x86.avx512.mask.scatterdiv8.si";
      break;
    case X86::BI__builtin_ia32_scattersiv2df:
      intrinsicName = "x86.avx512.mask.scattersiv2.df";
      break;
    case X86::BI__builtin_ia32_scattersiv2di:
      intrinsicName = "x86.avx512.mask.scattersiv2.di";
      break;
    case X86::BI__builtin_ia32_scattersiv4df:
      intrinsicName = "x86.avx512.mask.scattersiv4.df";
      break;
    case X86::BI__builtin_ia32_scattersiv4di:
      intrinsicName = "x86.avx512.mask.scattersiv4.di";
      break;
    case X86::BI__builtin_ia32_scattersiv4sf:
      intrinsicName = "x86.avx512.mask.scattersiv4.sf";
      break;
    case X86::BI__builtin_ia32_scattersiv4si:
      intrinsicName = "x86.avx512.mask.scattersiv4.si";
      break;
    case X86::BI__builtin_ia32_scattersiv8sf:
      intrinsicName = "x86.avx512.mask.scattersiv8.sf";
      break;
    case X86::BI__builtin_ia32_scattersiv8si:
      intrinsicName = "x86.avx512.mask.scattersiv8.si";
      break;
    }

    mlir::Location loc = getLoc(expr->getExprLoc());
    unsigned minElts =
        std::min(cast<cir::VectorType>(ops[2].getType()).getSize(),
                 cast<cir::VectorType>(ops[3].getType()).getSize());
    ops[1] = getMaskVecValue(builder, loc, ops[1], minElts);

    return cir::LLVMIntrinsicCallOp::create(
               builder, loc, cgm.getStringAttrCached(intrinsicName),
               convertType(expr->getType()), ops)
        .getResult();
  }
  case X86::BI__builtin_ia32_vextractf128_pd256:
  case X86::BI__builtin_ia32_vextractf128_ps256:
  case X86::BI__builtin_ia32_vextractf128_si256:
  case X86::BI__builtin_ia32_extract128i256:
  case X86::BI__builtin_ia32_extractf64x4_mask:
  case X86::BI__builtin_ia32_extractf32x4_mask:
  case X86::BI__builtin_ia32_extracti64x4_mask:
  case X86::BI__builtin_ia32_extracti32x4_mask:
  case X86::BI__builtin_ia32_extractf32x8_mask:
  case X86::BI__builtin_ia32_extracti32x8_mask:
  case X86::BI__builtin_ia32_extractf32x4_256_mask:
  case X86::BI__builtin_ia32_extracti32x4_256_mask:
  case X86::BI__builtin_ia32_extractf64x2_256_mask:
  case X86::BI__builtin_ia32_extracti64x2_256_mask:
  case X86::BI__builtin_ia32_extractf64x2_512_mask:
  case X86::BI__builtin_ia32_extracti64x2_512_mask: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    cir::VectorType dstTy = cast<cir::VectorType>(convertType(expr->getType()));
    unsigned numElts = dstTy.getSize();
    unsigned srcNumElts = cast<cir::VectorType>(ops[0].getType()).getSize();
    unsigned subVectors = srcNumElts / numElts;
    assert(llvm::isPowerOf2_32(subVectors) && "Expected power of 2 subvectors");
    unsigned index =
        ops[1].getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue();

    index &= subVectors - 1; // Remove any extra bits.
    index *= numElts;

    int64_t indices[16];
    std::iota(indices, indices + numElts, index);

    mlir::Value poison =
        builder.getConstant(loc, cir::PoisonAttr::get(ops[0].getType()));
    mlir::Value res = builder.createVecShuffle(loc, ops[0], poison,
                                               ArrayRef(indices, numElts));
    if (ops.size() == 4)
      res = emitX86Select(builder, loc, ops[3], res, ops[2]);

    return res;
  }
  case X86::BI__builtin_ia32_vinsertf128_pd256:
  case X86::BI__builtin_ia32_vinsertf128_ps256:
  case X86::BI__builtin_ia32_vinsertf128_si256:
  case X86::BI__builtin_ia32_insert128i256:
  case X86::BI__builtin_ia32_insertf64x4:
  case X86::BI__builtin_ia32_insertf32x4:
  case X86::BI__builtin_ia32_inserti64x4:
  case X86::BI__builtin_ia32_inserti32x4:
  case X86::BI__builtin_ia32_insertf32x8:
  case X86::BI__builtin_ia32_inserti32x8:
  case X86::BI__builtin_ia32_insertf32x4_256:
  case X86::BI__builtin_ia32_inserti32x4_256:
  case X86::BI__builtin_ia32_insertf64x2_256:
  case X86::BI__builtin_ia32_inserti64x2_256:
  case X86::BI__builtin_ia32_insertf64x2_512:
  case X86::BI__builtin_ia32_inserti64x2_512: {
    unsigned dstNumElts = cast<cir::VectorType>(ops[0].getType()).getSize();
    unsigned srcNumElts = cast<cir::VectorType>(ops[1].getType()).getSize();
    unsigned subVectors = dstNumElts / srcNumElts;
    assert(llvm::isPowerOf2_32(subVectors) && "Expected power of 2 subvectors");
    assert(dstNumElts <= 16);

    uint64_t index = getZExtIntValueFromConstOp(ops[2]);
    index &= subVectors - 1; // Remove any extra bits.
    index *= srcNumElts;

    llvm::SmallVector<int64_t, 16> mask(dstNumElts);
    for (unsigned i = 0; i != dstNumElts; ++i)
      mask[i] = (i >= srcNumElts) ? srcNumElts + (i % srcNumElts) : i;

    mlir::Value op1 =
        builder.createVecShuffle(getLoc(expr->getExprLoc()), ops[1], mask);

    for (unsigned i = 0; i != dstNumElts; ++i) {
      if (i >= index && i < (index + srcNumElts))
        mask[i] = (i - index) + dstNumElts;
      else
        mask[i] = i;
    }

    return builder.createVecShuffle(getLoc(expr->getExprLoc()), ops[0], op1,
                                    mask);
  }
  case X86::BI__builtin_ia32_pmovqd512_mask:
  case X86::BI__builtin_ia32_pmovwb512_mask: {
    mlir::Value Res =
        builder.createIntCast(ops[0], cast<cir::VectorType>(ops[1].getType()));
    return emitX86Select(builder, getLoc(expr->getExprLoc()), ops[2], Res,
                         ops[1]);
  }
  case X86::BI__builtin_ia32_pmovdb512_mask:
  case X86::BI__builtin_ia32_pmovdw512_mask:
  case X86::BI__builtin_ia32_pmovqw512_mask: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    cir::VectorType resTy = cast<cir::VectorType>(ops[1].getType());
    auto constOp = mlir::dyn_cast_or_null<cir::ConstantOp>(
        ops[2].getDefiningOp());
    if (constOp && constOp.isAllOnesValue())
      return builder.createIntCast(ops[0], resTy);

    llvm::StringRef intrinsicName;
    switch (builtinID) {
    default:
      llvm_unreachable("unsupported pmov*_mask builtin");
    case X86::BI__builtin_ia32_pmovdb512_mask:
      intrinsicName = "x86.avx512.mask.pmov.db.512";
      break;
    case X86::BI__builtin_ia32_pmovdw512_mask:
      intrinsicName = "x86.avx512.mask.pmov.dw.512";
      break;
    case X86::BI__builtin_ia32_pmovqw512_mask:
      intrinsicName = "x86.avx512.mask.pmov.qw.512";
      break;
    }

    return cir::LLVMIntrinsicCallOp::create(
               builder, loc, cgm.getStringAttrCached(intrinsicName), resTy,
               ops)
        .getResult();
  }
  case X86::BI__builtin_ia32_pblendw128:
  case X86::BI__builtin_ia32_blendpd:
  case X86::BI__builtin_ia32_blendps:
  case X86::BI__builtin_ia32_blendpd256:
  case X86::BI__builtin_ia32_blendps256:
  case X86::BI__builtin_ia32_pblendw256:
  case X86::BI__builtin_ia32_pblendd128:
  case X86::BI__builtin_ia32_pblendd256: {
    uint32_t imm = getZExtIntValueFromConstOp(ops[2]);
    unsigned numElts = cast<cir::VectorType>(ops[0].getType()).getSize();

    llvm::SmallVector<mlir::Attribute, 16> indices;
    // If there are more than 8 elements, the immediate is used twice so make
    // sure we handle that.
    for (unsigned i = 0; i != numElts; ++i)
      indices.push_back(
          builder.getSInt32AttrLocalCached(((imm >> (i % 8)) & 0x1)
                                               ? numElts + i
                                               : i));

    return builder.createVecShuffle(getLoc(expr->getExprLoc()), ops[0], ops[1],
                                    indices);
  }
  case X86::BI__builtin_ia32_pshuflw:
  case X86::BI__builtin_ia32_pshuflw256:
  case X86::BI__builtin_ia32_pshuflw512:
    return emitPshufWord(builder, ops[0], ops[1], getLoc(expr->getExprLoc()),
                         true);
  case X86::BI__builtin_ia32_pshufhw:
  case X86::BI__builtin_ia32_pshufhw256:
  case X86::BI__builtin_ia32_pshufhw512:
    return emitPshufWord(builder, ops[0], ops[1], getLoc(expr->getExprLoc()),
                         false);
  case X86::BI__builtin_ia32_pshufd:
  case X86::BI__builtin_ia32_pshufd256:
  case X86::BI__builtin_ia32_pshufd512:
  case X86::BI__builtin_ia32_vpermilpd:
  case X86::BI__builtin_ia32_vpermilps:
  case X86::BI__builtin_ia32_vpermilpd256:
  case X86::BI__builtin_ia32_vpermilps256:
  case X86::BI__builtin_ia32_vpermilpd512:
  case X86::BI__builtin_ia32_vpermilps512: {
    const uint32_t imm = getSExtIntValueFromConstOp(ops[1]);

    llvm::SmallVector<int64_t, 16> mask(16);
    computeFullLaneShuffleMask(*this, ops[0], imm, false, mask);

    return builder.createVecShuffle(getLoc(expr->getExprLoc()), ops[0], mask);
  }
  case X86::BI__builtin_ia32_shufpd:
  case X86::BI__builtin_ia32_shufpd256:
  case X86::BI__builtin_ia32_shufpd512:
  case X86::BI__builtin_ia32_shufps:
  case X86::BI__builtin_ia32_shufps256:
  case X86::BI__builtin_ia32_shufps512: {
    const uint32_t imm = getZExtIntValueFromConstOp(ops[2]);

    llvm::SmallVector<int64_t, 16> mask(16);
    computeFullLaneShuffleMask(*this, ops[0], imm, true, mask);

    return builder.createVecShuffle(getLoc(expr->getExprLoc()), ops[0], ops[1],
                                    mask);
  }
  case X86::BI__builtin_ia32_permdi256:
  case X86::BI__builtin_ia32_permdf256:
  case X86::BI__builtin_ia32_permdi512:
  case X86::BI__builtin_ia32_permdf512: {
    unsigned imm =
        ops[1].getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue();
    unsigned numElts = cast<cir::VectorType>(ops[0].getType()).getSize();

    // These intrinsics operate on 256-bit lanes of four 64-bit elements.
    int64_t Indices[8];

    for (unsigned l = 0; l != numElts; l += 4)
      for (unsigned i = 0; i != 4; ++i)
        Indices[l + i] = l + ((imm >> (2 * i)) & 0x3);

    return builder.createVecShuffle(getLoc(expr->getExprLoc()), ops[0],
                                    ArrayRef(Indices, numElts));
  }
  case X86::BI__builtin_ia32_palignr128:
  case X86::BI__builtin_ia32_palignr256:
  case X86::BI__builtin_ia32_palignr512: {
    uint32_t shiftVal = getZExtIntValueFromConstOp(ops[2]) & 0xff;

    unsigned numElts = cast<cir::VectorType>(ops[0].getType()).getSize();
    assert(numElts % 16 == 0);

    // If palignr is shifting the pair of vectors more than the size of two
    // lanes, emit zero.
    if (shiftVal >= 32)
      return builder.getNullValue(convertType(expr->getType()),
                                  getLoc(expr->getExprLoc()));

    // If palignr is shifting the pair of input vectors more than one lane,
    // but less than two lanes, convert to shifting in zeroes.
    if (shiftVal > 16) {
      shiftVal -= 16;
      ops[1] = ops[0];
      ops[0] =
          builder.getNullValue(ops[0].getType(), getLoc(expr->getExprLoc()));
    }

    int64_t indices[64];
    // 256-bit palignr operates on 128-bit lanes so we need to handle that
    for (unsigned l = 0; l != numElts; l += 16) {
      for (unsigned i = 0; i != 16; ++i) {
        uint32_t idx = shiftVal + i;
        if (idx >= 16)
          idx += numElts - 16; // End of lane, switch operand.
        indices[l + i] = l + idx;
      }
    }

    return builder.createVecShuffle(getLoc(expr->getExprLoc()), ops[1], ops[0],
                                    ArrayRef(indices, numElts));
  }
  case X86::BI__builtin_ia32_alignd128:
  case X86::BI__builtin_ia32_alignd256:
  case X86::BI__builtin_ia32_alignd512:
  case X86::BI__builtin_ia32_alignq128:
  case X86::BI__builtin_ia32_alignq256:
  case X86::BI__builtin_ia32_alignq512: {
    unsigned numElts = cast<cir::VectorType>(ops[0].getType()).getSize();
    unsigned shiftVal =
        ops[2].getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue() &
        0xff;

    // Mask the shift amount to width of a vector.
    shiftVal &= numElts - 1;

    SmallVector<mlir::Attribute, 16> indices;
    for (unsigned i = 0; i != numElts; ++i)
      indices.push_back(builder.getSInt32AttrLocalCached(i + shiftVal));

    return builder.createVecShuffle(getLoc(expr->getExprLoc()), ops[1], ops[0],
                                    indices);
  }
  case X86::BI__builtin_ia32_shuf_f32x4_256:
  case X86::BI__builtin_ia32_shuf_f64x2_256:
  case X86::BI__builtin_ia32_shuf_i32x4_256:
  case X86::BI__builtin_ia32_shuf_i64x2_256:
  case X86::BI__builtin_ia32_shuf_f32x4:
  case X86::BI__builtin_ia32_shuf_f64x2:
  case X86::BI__builtin_ia32_shuf_i32x4:
  case X86::BI__builtin_ia32_shuf_i64x2: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    unsigned imm = getZExtIntValueFromConstOp(ops[2]);
    auto vecTy = mlir::cast<cir::VectorType>(ops[0].getType());
    unsigned numElts = vecTy.getSize();
    unsigned numLanes =
        cgm.getDataLayout().getTypeSizeInBits(vecTy) == 512 ? 4 : 2;
    unsigned numLaneElts = numElts / numLanes;

    SmallVector<int64_t, 16> indices(numElts);
    for (unsigned l = 0; l != numElts; l += numLaneElts) {
      unsigned index = (imm % numLanes) * numLaneElts;
      imm /= numLanes;
      if (l >= (numElts / 2))
        index += numElts; // Switch to other source.
      for (unsigned i = 0; i != numLaneElts; ++i)
        indices[l + i] = index + i;
    }

    return builder.createVecShuffle(loc, ops[0], ops[1], indices);
  }
  case X86::BI__builtin_ia32_vperm2f128_pd256:
  case X86::BI__builtin_ia32_vperm2f128_ps256:
  case X86::BI__builtin_ia32_vperm2f128_si256:
  case X86::BI__builtin_ia32_permti256: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    unsigned imm = getZExtIntValueFromConstOp(ops[2]);
    auto vecTy = mlir::cast<cir::VectorType>(ops[0].getType());
    unsigned numElts = vecTy.getSize();

    // Two lanes, two inputs. Reserve first input for first lane, second for
    // second lane. May result in duplicate sources, handled in backend.
    mlir::Value outOps[2];
    SmallVector<int64_t, 8> indices(numElts);
    for (unsigned l = 0; l != 2; ++l) {
      if (imm & (1 << ((l * 4) + 3)))
        outOps[l] = builder.getNullValue(vecTy, loc);
      else if (imm & (1 << ((l * 4) + 1)))
        outOps[l] = ops[1];
      else
        outOps[l] = ops[0];

      for (unsigned i = 0; i != numElts / 2; ++i) {
        unsigned idx = (l * numElts) + i;
        if (imm & (1 << (l * 4)))
          idx += numElts / 2;
        indices[(l * numElts / 2) + i] = idx;
      }
    }

    return builder.createVecShuffle(loc, outOps[0], outOps[1], indices);
  }
  case X86::BI__builtin_ia32_pslldqi128_byteshift:
  case X86::BI__builtin_ia32_pslldqi256_byteshift:
  case X86::BI__builtin_ia32_pslldqi512_byteshift:
    return emitX86PSLLDQIByteShift(*this, expr, ops);
  case X86::BI__builtin_ia32_psrldqi128_byteshift:
  case X86::BI__builtin_ia32_psrldqi256_byteshift:
  case X86::BI__builtin_ia32_psrldqi512_byteshift:
    return emitX86PSRLDQIByteShift(*this, expr, ops);
  case X86::BI__builtin_ia32_kshiftliqi:
  case X86::BI__builtin_ia32_kshiftlihi:
  case X86::BI__builtin_ia32_kshiftlisi:
  case X86::BI__builtin_ia32_kshiftlidi: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    unsigned shiftVal =
        ops[1].getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue() &
        0xff;
    unsigned numElems = cast<cir::IntType>(ops[0].getType()).getWidth();

    if (shiftVal >= numElems)
      return builder.getNullValue(ops[0].getType(), loc);

    mlir::Value in = getMaskVecValue(builder, loc, ops[0], numElems);

    SmallVector<mlir::Attribute, 64> indices;
    for (auto i : llvm::seq<unsigned>(0, numElems))
      indices.push_back(
          builder.getSInt32AttrLocalCached(numElems + i - shiftVal));

    mlir::Value zero = builder.getNullValue(in.getType(), loc);
    mlir::Value sv = builder.createVecShuffle(loc, zero, in, indices);
    return builder.createBitcast(sv, ops[0].getType());
  }
  case X86::BI__builtin_ia32_kshiftriqi:
  case X86::BI__builtin_ia32_kshiftrihi:
  case X86::BI__builtin_ia32_kshiftrisi:
  case X86::BI__builtin_ia32_kshiftridi: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    unsigned shiftVal =
        ops[1].getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue() &
        0xff;
    unsigned numElems = cast<cir::IntType>(ops[0].getType()).getWidth();

    if (shiftVal >= numElems)
      return builder.getNullValue(ops[0].getType(), loc);

    mlir::Value in = getMaskVecValue(builder, loc, ops[0], numElems);

    SmallVector<mlir::Attribute, 64> indices;
    for (auto i : llvm::seq<unsigned>(0, numElems))
      indices.push_back(builder.getSInt32AttrLocalCached(i + shiftVal));

    mlir::Value zero = builder.getNullValue(in.getType(), loc);
    mlir::Value sv = builder.createVecShuffle(loc, in, zero, indices);
    return builder.createBitcast(sv, ops[0].getType());
  }
  case X86::BI__builtin_ia32_vprotbi:
  case X86::BI__builtin_ia32_vprotwi:
  case X86::BI__builtin_ia32_vprotdi:
  case X86::BI__builtin_ia32_vprotqi:
  case X86::BI__builtin_ia32_prold128:
  case X86::BI__builtin_ia32_prold256:
  case X86::BI__builtin_ia32_prold512:
  case X86::BI__builtin_ia32_prolq128:
  case X86::BI__builtin_ia32_prolq256:
  case X86::BI__builtin_ia32_prolq512:
    return emitX86FunnelShift(builder, cgm, getLoc(expr->getExprLoc()), ops[0],
                              ops[0], ops[1], false);
  case X86::BI__builtin_ia32_prord128:
  case X86::BI__builtin_ia32_prord256:
  case X86::BI__builtin_ia32_prord512:
  case X86::BI__builtin_ia32_prorq128:
  case X86::BI__builtin_ia32_prorq256:
  case X86::BI__builtin_ia32_prorq512:
    return emitX86FunnelShift(builder, cgm, getLoc(expr->getExprLoc()), ops[0],
                              ops[0], ops[1], true);
  case X86::BI__builtin_ia32_selectb_128:
  case X86::BI__builtin_ia32_selectb_256:
  case X86::BI__builtin_ia32_selectb_512:
  case X86::BI__builtin_ia32_selectw_128:
  case X86::BI__builtin_ia32_selectw_256:
  case X86::BI__builtin_ia32_selectw_512:
  case X86::BI__builtin_ia32_selectd_128:
  case X86::BI__builtin_ia32_selectd_256:
  case X86::BI__builtin_ia32_selectd_512:
  case X86::BI__builtin_ia32_selectq_128:
  case X86::BI__builtin_ia32_selectq_256:
  case X86::BI__builtin_ia32_selectq_512:
  case X86::BI__builtin_ia32_selectph_128:
  case X86::BI__builtin_ia32_selectph_256:
  case X86::BI__builtin_ia32_selectph_512:
  case X86::BI__builtin_ia32_selectpbf_128:
  case X86::BI__builtin_ia32_selectpbf_256:
  case X86::BI__builtin_ia32_selectpbf_512:
  case X86::BI__builtin_ia32_selectps_128:
  case X86::BI__builtin_ia32_selectps_256:
  case X86::BI__builtin_ia32_selectps_512:
  case X86::BI__builtin_ia32_selectpd_128:
  case X86::BI__builtin_ia32_selectpd_256:
  case X86::BI__builtin_ia32_selectpd_512:
    return emitX86Select(builder, getLoc(expr->getExprLoc()), ops[0], ops[1],
                         ops[2]);
  case X86::BI__builtin_ia32_selectsh_128:
  case X86::BI__builtin_ia32_selectsbf_128:
  case X86::BI__builtin_ia32_selectss_128:
  case X86::BI__builtin_ia32_selectsd_128: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    mlir::Value idx = builder.getConstInt(loc, builder.getSInt64Ty(), 0);
    mlir::Value a = cir::VecExtractOp::create(builder, loc, ops[1], idx);
    mlir::Value b = cir::VecExtractOp::create(builder, loc, ops[2], idx);
    a = emitX86ScalarSelect(builder, loc, ops[0], a, b);
    return cir::VecInsertOp::create(builder, loc, ops[1], a, idx);
  }
  case X86::BI__builtin_ia32_cmpb128_mask:
  case X86::BI__builtin_ia32_cmpb256_mask:
  case X86::BI__builtin_ia32_cmpb512_mask:
  case X86::BI__builtin_ia32_cmpw128_mask:
  case X86::BI__builtin_ia32_cmpw256_mask:
  case X86::BI__builtin_ia32_cmpw512_mask:
  case X86::BI__builtin_ia32_cmpd128_mask:
  case X86::BI__builtin_ia32_cmpd256_mask:
  case X86::BI__builtin_ia32_cmpd512_mask:
  case X86::BI__builtin_ia32_cmpq128_mask:
  case X86::BI__builtin_ia32_cmpq256_mask:
  case X86::BI__builtin_ia32_cmpq512_mask: {
    unsigned cc =
        ops[2].getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue() &
        0x7;
    return *emitX86MaskedCompare(builder, cc, /*isSigned=*/true, ops,
                                 getLoc(expr->getExprLoc()));
  }
  case X86::BI__builtin_ia32_ucmpb128_mask:
  case X86::BI__builtin_ia32_ucmpb256_mask:
  case X86::BI__builtin_ia32_ucmpb512_mask:
  case X86::BI__builtin_ia32_ucmpw128_mask:
  case X86::BI__builtin_ia32_ucmpw256_mask:
  case X86::BI__builtin_ia32_ucmpw512_mask:
  case X86::BI__builtin_ia32_ucmpd128_mask:
  case X86::BI__builtin_ia32_ucmpd256_mask:
  case X86::BI__builtin_ia32_ucmpd512_mask:
  case X86::BI__builtin_ia32_ucmpq128_mask:
  case X86::BI__builtin_ia32_ucmpq256_mask:
  case X86::BI__builtin_ia32_ucmpq512_mask: {
    unsigned cc =
        ops[2].getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue() &
        0x7;
    return *emitX86MaskedCompare(builder, cc, /*isSigned=*/false, ops,
                                 getLoc(expr->getExprLoc()));
  }
  case X86::BI__builtin_ia32_vpcomb:
  case X86::BI__builtin_ia32_vpcomw:
  case X86::BI__builtin_ia32_vpcomd:
  case X86::BI__builtin_ia32_vpcomq:
    return emitX86vpcom(builder, getLoc(expr->getExprLoc()), ops, true);
  case X86::BI__builtin_ia32_vpcomub:
  case X86::BI__builtin_ia32_vpcomuw:
  case X86::BI__builtin_ia32_vpcomud:
  case X86::BI__builtin_ia32_vpcomuq:
    return emitX86vpcom(builder, getLoc(expr->getExprLoc()), ops, false);
  case X86::BI__builtin_ia32_kortestcqi:
  case X86::BI__builtin_ia32_kortestchi:
  case X86::BI__builtin_ia32_kortestcsi:
  case X86::BI__builtin_ia32_kortestcdi: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    cir::IntType ty = cast<cir::IntType>(ops[0].getType());
    mlir::Value allOnesOp =
        builder.getConstAPInt(loc, ty, APInt::getAllOnes(ty.getWidth()));
    mlir::Value orOp = emitX86MaskLogic(builder, loc, cir::BinOpKind::Or, ops);
    mlir::Value cmp =
        cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq, orOp, allOnesOp);
    return builder.createCast(cir::CastKind::bool_to_int, cmp,
                              cgm.convertType(expr->getType()));
  }
  case X86::BI__builtin_ia32_kortestzqi:
  case X86::BI__builtin_ia32_kortestzhi:
  case X86::BI__builtin_ia32_kortestzsi:
  case X86::BI__builtin_ia32_kortestzdi: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    cir::IntType ty = cast<cir::IntType>(ops[0].getType());
    mlir::Value allZerosOp = builder.getNullValue(ty, loc).getResult();
    mlir::Value orOp = emitX86MaskLogic(builder, loc, cir::BinOpKind::Or, ops);
    mlir::Value cmp =
        cir::CmpOp::create(builder, loc, cir::CmpOpKind::eq, orOp, allZerosOp);
    return builder.createCast(cir::CastKind::bool_to_int, cmp,
                              cgm.convertType(expr->getType()));
  }
  case X86::BI__builtin_ia32_ktestcqi:
    return emitX86MaskTest(builder, getLoc(expr->getExprLoc()), cgm,
                           "x86.avx512.ktestc.b", ops);
  case X86::BI__builtin_ia32_ktestzqi:
    return emitX86MaskTest(builder, getLoc(expr->getExprLoc()), cgm,
                           "x86.avx512.ktestz.b", ops);
  case X86::BI__builtin_ia32_ktestchi:
    return emitX86MaskTest(builder, getLoc(expr->getExprLoc()), cgm,
                           "x86.avx512.ktestc.w", ops);
  case X86::BI__builtin_ia32_ktestzhi:
    return emitX86MaskTest(builder, getLoc(expr->getExprLoc()), cgm,
                           "x86.avx512.ktestz.w", ops);
  case X86::BI__builtin_ia32_ktestcsi:
    return emitX86MaskTest(builder, getLoc(expr->getExprLoc()), cgm,
                           "x86.avx512.ktestc.d", ops);
  case X86::BI__builtin_ia32_ktestzsi:
    return emitX86MaskTest(builder, getLoc(expr->getExprLoc()), cgm,
                           "x86.avx512.ktestz.d", ops);
  case X86::BI__builtin_ia32_ktestcdi:
    return emitX86MaskTest(builder, getLoc(expr->getExprLoc()), cgm,
                           "x86.avx512.ktestc.q", ops);
  case X86::BI__builtin_ia32_ktestzdi:
    return emitX86MaskTest(builder, getLoc(expr->getExprLoc()), cgm,
                           "x86.avx512.ktestz.q", ops);
  case X86::BI__builtin_ia32_kaddqi:
    return emitX86MaskAddLogic(builder, getLoc(expr->getExprLoc()), cgm,
                               "x86.avx512.kadd.b", ops);
  case X86::BI__builtin_ia32_kaddhi:
    return emitX86MaskAddLogic(builder, getLoc(expr->getExprLoc()), cgm,
                               "x86.avx512.kadd.w", ops);
  case X86::BI__builtin_ia32_kaddsi:
    return emitX86MaskAddLogic(builder, getLoc(expr->getExprLoc()), cgm,
                               "x86.avx512.kadd.d", ops);
  case X86::BI__builtin_ia32_kadddi:
    return emitX86MaskAddLogic(builder, getLoc(expr->getExprLoc()), cgm,
                               "x86.avx512.kadd.q", ops);
  case X86::BI__builtin_ia32_kandqi:
  case X86::BI__builtin_ia32_kandhi:
  case X86::BI__builtin_ia32_kandsi:
  case X86::BI__builtin_ia32_kanddi:
    return emitX86MaskLogic(builder, getLoc(expr->getExprLoc()),
                            cir::BinOpKind::And, ops);
  case X86::BI__builtin_ia32_kandnqi:
  case X86::BI__builtin_ia32_kandnhi:
  case X86::BI__builtin_ia32_kandnsi:
  case X86::BI__builtin_ia32_kandndi:
    return emitX86MaskLogic(builder, getLoc(expr->getExprLoc()),
                            cir::BinOpKind::And, ops, true);
  case X86::BI__builtin_ia32_korqi:
  case X86::BI__builtin_ia32_korhi:
  case X86::BI__builtin_ia32_korsi:
  case X86::BI__builtin_ia32_kordi:
    return emitX86MaskLogic(builder, getLoc(expr->getExprLoc()),
                            cir::BinOpKind::Or, ops);
  case X86::BI__builtin_ia32_kxnorqi:
  case X86::BI__builtin_ia32_kxnorhi:
  case X86::BI__builtin_ia32_kxnorsi:
  case X86::BI__builtin_ia32_kxnordi:
    return emitX86MaskLogic(builder, getLoc(expr->getExprLoc()),
                            cir::BinOpKind::Xor, ops, true);
  case X86::BI__builtin_ia32_kxorqi:
  case X86::BI__builtin_ia32_kxorhi:
  case X86::BI__builtin_ia32_kxorsi:
  case X86::BI__builtin_ia32_kxordi:
    return emitX86MaskLogic(builder, getLoc(expr->getExprLoc()),
                            cir::BinOpKind::Xor, ops);
  case X86::BI__builtin_ia32_knotqi:
  case X86::BI__builtin_ia32_knothi:
  case X86::BI__builtin_ia32_knotsi:
  case X86::BI__builtin_ia32_knotdi: {
    cir::IntType intTy = cast<cir::IntType>(ops[0].getType());
    unsigned numElts = intTy.getWidth();
    mlir::Value resVec =
        getMaskVecValue(builder, getLoc(expr->getExprLoc()), ops[0], numElts);
    return builder.createBitcast(builder.createNot(resVec), ops[0].getType());
  }
  case X86::BI__builtin_ia32_kmovb:
  case X86::BI__builtin_ia32_kmovw:
  case X86::BI__builtin_ia32_kmovd:
  case X86::BI__builtin_ia32_kmovq: {
    // Bitcast to vXi1 type and then back to integer. This gets the mask
    // register type into the IR, but might be optimized out depending on
    // what's around it.
    cir::IntType intTy = cast<cir::IntType>(ops[0].getType());
    unsigned numElts = intTy.getWidth();
    mlir::Value resVec =
        getMaskVecValue(builder, getLoc(expr->getExprLoc()), ops[0], numElts);
    return builder.createBitcast(resVec, ops[0].getType());
  }
  case X86::BI__builtin_ia32_sqrtsh_round_mask:
  case X86::BI__builtin_ia32_sqrtsd_round_mask:
  case X86::BI__builtin_ia32_sqrtss_round_mask: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    unsigned rnd =
        ops[4].getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue();
    // For non-CUR_DIRECTION rounding, use x86-specific intrinsics.
    if (rnd != 4) {
      llvm::StringRef intrinsicName;
      switch (builtinID) {
      default:
        llvm_unreachable("Unsupported intrinsic!");
      case X86::BI__builtin_ia32_sqrtsh_round_mask:
        intrinsicName = "x86.avx512fp16.mask.sqrt.sh";
        break;
      case X86::BI__builtin_ia32_sqrtsd_round_mask:
        intrinsicName = "x86.avx512.mask.sqrt.sd";
        break;
      case X86::BI__builtin_ia32_sqrtss_round_mask:
        intrinsicName = "x86.avx512.mask.sqrt.ss";
        break;
      }
      return cir::LLVMIntrinsicCallOp::create(
                 builder, loc, cgm.getStringAttrCached(intrinsicName),
                 ops[0].getType(), ops)
          .getResult();
    }
    // CUR_DIRECTION: extract element 0, call generic sqrt, apply scalar mask.
    // TODO(cir): handle constrained FP via experimental_constrained_sqrt.
    mlir::Value idx = builder.getConstInt(loc, builder.getSInt64Ty(), 0);
    mlir::Value a = cir::VecExtractOp::create(builder, loc, ops[1], idx);
    a = cir::LLVMIntrinsicCallOp::create(
            builder, loc, cgm.getStringAttrCached("sqrt"), a.getType(),
            mlir::ValueRange{a})
            .getResult();
    mlir::Value src = cir::VecExtractOp::create(builder, loc, ops[2], idx);
    a = emitX86ScalarSelect(builder, loc, ops[3], a, src);
    return cir::VecInsertOp::create(builder, loc, ops[0], a, idx);
  }
  case X86::BI__builtin_ia32_sqrtph512:
  case X86::BI__builtin_ia32_sqrtps512:
  case X86::BI__builtin_ia32_sqrtpd512: {
    uint64_t cc = CIRGenFunction::getZExtIntValueFromConstOp(ops[1]);
    // Match OGCG: only fold to generic sqrt when the rounding mode is
    // _MM_FROUND_CUR_DIRECTION (4). For explicit rounding modes, preserve
    // target-specific AVX-512 sqrt intrinsics and forward the immediate.
    if (cc != 4) {
      llvm::StringRef intrinsicName;
      switch (builtinID) {
      default:
        llvm_unreachable("Unsupported intrinsic!");
      case X86::BI__builtin_ia32_sqrtph512:
        intrinsicName = "x86.avx512fp16.sqrt.ph.512";
        break;
      case X86::BI__builtin_ia32_sqrtps512:
        intrinsicName = "x86.avx512.sqrt.ps.512";
        break;
      case X86::BI__builtin_ia32_sqrtpd512:
        intrinsicName = "x86.avx512.sqrt.pd.512";
        break;
      }
      return cir::LLVMIntrinsicCallOp::create(
                 builder, getLoc(expr->getExprLoc()),
                 cgm.getStringAttrCached(intrinsicName), ops[0].getType(),
                 ops)
          .getResult();
    }

    mlir::Location loc = getLoc(expr->getExprLoc());
    mlir::Value arg = ops[0];
    return cir::SqrtOp::create(builder, loc, arg.getType(), arg).getResult();
  }
  case X86::BI__builtin_ia32_pmuludq128:
  case X86::BI__builtin_ia32_pmuludq256:
  case X86::BI__builtin_ia32_pmuludq512: {
    unsigned opTypePrimitiveSizeInBits =
        cgm.getDataLayout().getTypeSizeInBits(ops[0].getType());
    return emitX86Muldq(builder, getLoc(expr->getExprLoc()), /*isSigned*/ false,
                        ops, opTypePrimitiveSizeInBits);
  }
  case X86::BI__builtin_ia32_pmuldq128:
  case X86::BI__builtin_ia32_pmuldq256:
  case X86::BI__builtin_ia32_pmuldq512: {
    unsigned opTypePrimitiveSizeInBits =
        cgm.getDataLayout().getTypeSizeInBits(ops[0].getType());
    return emitX86Muldq(builder, getLoc(expr->getExprLoc()), /*isSigned*/ true,
                        ops, opTypePrimitiveSizeInBits);
  }
  case X86::BI__builtin_ia32_pternlogd512_mask:
  case X86::BI__builtin_ia32_pternlogq512_mask:
  case X86::BI__builtin_ia32_pternlogd128_mask:
  case X86::BI__builtin_ia32_pternlogd256_mask:
  case X86::BI__builtin_ia32_pternlogq128_mask:
  case X86::BI__builtin_ia32_pternlogq256_mask:
    return emitX86Ternlog(builder, cgm, getLoc(expr->getExprLoc()),
                          /*zeroMask=*/false, ops);
  case X86::BI__builtin_ia32_pternlogd512_maskz:
  case X86::BI__builtin_ia32_pternlogq512_maskz:
  case X86::BI__builtin_ia32_pternlogd128_maskz:
  case X86::BI__builtin_ia32_pternlogd256_maskz:
  case X86::BI__builtin_ia32_pternlogq128_maskz:
  case X86::BI__builtin_ia32_pternlogq256_maskz:
    return emitX86Ternlog(builder, cgm, getLoc(expr->getExprLoc()),
                          /*zeroMask=*/true, ops);
  case X86::BI__builtin_ia32_vpshldd128:
  case X86::BI__builtin_ia32_vpshldd256:
  case X86::BI__builtin_ia32_vpshldd512:
  case X86::BI__builtin_ia32_vpshldq128:
  case X86::BI__builtin_ia32_vpshldq256:
  case X86::BI__builtin_ia32_vpshldq512:
  case X86::BI__builtin_ia32_vpshldw128:
  case X86::BI__builtin_ia32_vpshldw256:
  case X86::BI__builtin_ia32_vpshldw512:
    return emitX86FunnelShift(builder, cgm, getLoc(expr->getExprLoc()), ops[0],
                              ops[1], ops[2], false);
  case X86::BI__builtin_ia32_vpshrdd128:
  case X86::BI__builtin_ia32_vpshrdd256:
  case X86::BI__builtin_ia32_vpshrdd512:
  case X86::BI__builtin_ia32_vpshrdq128:
  case X86::BI__builtin_ia32_vpshrdq256:
  case X86::BI__builtin_ia32_vpshrdq512:
  case X86::BI__builtin_ia32_vpshrdw128:
  case X86::BI__builtin_ia32_vpshrdw256:
  case X86::BI__builtin_ia32_vpshrdw512:
    // Ops 0 and 1 are swapped for right shift.
    return emitX86FunnelShift(builder, cgm, getLoc(expr->getExprLoc()), ops[1],
                              ops[0], ops[2], true);
  case X86::BI__builtin_ia32_reduce_fadd_pd512:
  case X86::BI__builtin_ia32_reduce_fadd_ps512:
  case X86::BI__builtin_ia32_reduce_fadd_ph512:
  case X86::BI__builtin_ia32_reduce_fadd_ph256:
  case X86::BI__builtin_ia32_reduce_fadd_ph128: {
    // OGCG explicitly sets AllowReassoc for reduce_fadd builtins.
    cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(
        expr->getFPFeaturesInEffect(getLangOpts()));
    fmf = fmf | cir::FastMathFlags::reassoc;
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("vector.reduce.fadd"), ops[0].getType(),
               mlir::ValueRange{ops[0], ops[1]}, fmf)
        .getResult();
  }
  case X86::BI__builtin_ia32_reduce_fmul_pd512:
  case X86::BI__builtin_ia32_reduce_fmul_ps512:
  case X86::BI__builtin_ia32_reduce_fmul_ph512:
  case X86::BI__builtin_ia32_reduce_fmul_ph256:
  case X86::BI__builtin_ia32_reduce_fmul_ph128: {
    // OGCG explicitly sets AllowReassoc for reduce_fmul builtins.
    cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(
        expr->getFPFeaturesInEffect(getLangOpts()));
    fmf = fmf | cir::FastMathFlags::reassoc;
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("vector.reduce.fmul"), ops[0].getType(),
               mlir::ValueRange{ops[0], ops[1]}, fmf)
        .getResult();
  }
  case X86::BI__builtin_ia32_reduce_fmax_pd512:
  case X86::BI__builtin_ia32_reduce_fmax_ps512:
  case X86::BI__builtin_ia32_reduce_fmax_ph512:
  case X86::BI__builtin_ia32_reduce_fmax_ph256:
  case X86::BI__builtin_ia32_reduce_fmax_ph128: {
    // OGCG explicitly sets NoNaNs for reduce_fmax builtins.
    cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(
        expr->getFPFeaturesInEffect(getLangOpts()));
    fmf = fmf | cir::FastMathFlags::nnan;
    cir::VectorType vecTy = cast<cir::VectorType>(ops[0].getType());
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("vector.reduce.fmax"),
               vecTy.getElementType(), mlir::ValueRange{ops[0]}, fmf)
        .getResult();
  }
  case X86::BI__builtin_ia32_reduce_fmin_pd512:
  case X86::BI__builtin_ia32_reduce_fmin_ps512:
  case X86::BI__builtin_ia32_reduce_fmin_ph512:
  case X86::BI__builtin_ia32_reduce_fmin_ph256:
  case X86::BI__builtin_ia32_reduce_fmin_ph128: {
    // OGCG explicitly sets NoNaNs for reduce_fmin builtins.
    cir::FastMathFlags fmf = CIRGenFunction::getFastMathFlags(
        expr->getFPFeaturesInEffect(getLangOpts()));
    fmf = fmf | cir::FastMathFlags::nnan;
    cir::VectorType vecTy = cast<cir::VectorType>(ops[0].getType());
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached("vector.reduce.fmin"),
               vecTy.getElementType(), mlir::ValueRange{ops[0]}, fmf)
        .getResult();
  }
  case X86::BI__builtin_ia32_rdrand16_step:
  case X86::BI__builtin_ia32_rdrand32_step:
  case X86::BI__builtin_ia32_rdrand64_step:
  case X86::BI__builtin_ia32_rdseed16_step:
  case X86::BI__builtin_ia32_rdseed32_step:
  case X86::BI__builtin_ia32_rdseed64_step: {
    llvm::StringRef intrinsicName;
    switch (builtinID) {
    default:
      llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_rdrand16_step:
      intrinsicName = "x86.rdrand.16";
      break;
    case X86::BI__builtin_ia32_rdrand32_step:
      intrinsicName = "x86.rdrand.32";
      break;
    case X86::BI__builtin_ia32_rdrand64_step:
      intrinsicName = "x86.rdrand.64";
      break;
    case X86::BI__builtin_ia32_rdseed16_step:
      intrinsicName = "x86.rdseed.16";
      break;
    case X86::BI__builtin_ia32_rdseed32_step:
      intrinsicName = "x86.rdseed.32";
      break;
    case X86::BI__builtin_ia32_rdseed64_step:
      intrinsicName = "x86.rdseed.64";
      break;
    }

    mlir::Location loc = getLoc(expr->getExprLoc());
    mlir::Type randTy = cast<cir::PointerType>(ops[0].getType()).getPointee();
    llvm::SmallVector<mlir::Type, 2> resultTypes = {randTy,
                                                    builder.getUInt32Ty()};
    cir::RecordType resRecord =
        cir::RecordType::get(&getMLIRContext(), resultTypes, false, false,
                             cir::RecordType::RecordKind::Struct);

    mlir::Value call = cir::LLVMIntrinsicCallOp::create(
                           builder, loc, cgm.getStringAttrCached(intrinsicName),
                           resRecord)
                           .getResult();
    mlir::Value rand =
        cir::ExtractMemberOp::create(builder, loc, randTy, call, 0);
    builder.CIRBaseBuilderTy::createStore(loc, rand, ops[0]);

    return cir::ExtractMemberOp::create(builder, loc, builder.getUInt32Ty(),
                                        call, 1);
  }
  case X86::BI__builtin_ia32_addcarryx_u32:
  case X86::BI__builtin_ia32_addcarryx_u64:
  case X86::BI__builtin_ia32_subborrow_u32:
  case X86::BI__builtin_ia32_subborrow_u64: {
    llvm::StringRef intrinsicName;
    switch (builtinID) {
    default:
      llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_addcarryx_u32:
      intrinsicName = "x86.addcarry.32";
      break;
    case X86::BI__builtin_ia32_addcarryx_u64:
      intrinsicName = "x86.addcarry.64";
      break;
    case X86::BI__builtin_ia32_subborrow_u32:
      intrinsicName = "x86.subborrow.32";
      break;
    case X86::BI__builtin_ia32_subborrow_u64:
      intrinsicName = "x86.subborrow.64";
      break;
    }

    mlir::Location loc = getLoc(expr->getExprLoc());
    // The intrinsic returns {i8, i32/i64}. ops[1] determines the data width.
    mlir::Type dataTy = ops[1].getType();
    mlir::Type i8Ty = builder.getUInt8Ty();
    llvm::SmallVector<mlir::Type, 2> resultTypes = {i8Ty, dataTy};
    cir::RecordType resRecord =
        cir::RecordType::get(&getMLIRContext(), resultTypes, false, false,
                             cir::RecordType::RecordKind::Struct);

    SmallVector<mlir::Value> args = {ops[0], ops[1], ops[2]};
    mlir::Value call =
        cir::LLVMIntrinsicCallOp::create(
            builder, loc, cgm.getStringAttrCached(intrinsicName), resRecord,
            args)
            .getResult();

    // Extract field 1 (sum/difference) and store to output pointer (ops[3]).
    mlir::Value result =
        cir::ExtractMemberOp::create(builder, loc, dataTy, call, 1);
    builder.CIRBaseBuilderTy::createStore(loc, result, ops[3]);

    // Return field 0 (carry/borrow out).
    return cir::ExtractMemberOp::create(builder, loc, i8Ty, call, 0);
  }
  case X86::BI__builtin_ia32_fpclassps128_mask:
  case X86::BI__builtin_ia32_fpclassps256_mask:
  case X86::BI__builtin_ia32_fpclassps512_mask:
  case X86::BI__builtin_ia32_vfpclassbf16128_mask:
  case X86::BI__builtin_ia32_vfpclassbf16256_mask:
  case X86::BI__builtin_ia32_vfpclassbf16512_mask:
  case X86::BI__builtin_ia32_fpclassph128_mask:
  case X86::BI__builtin_ia32_fpclassph256_mask:
  case X86::BI__builtin_ia32_fpclassph512_mask:
  case X86::BI__builtin_ia32_fpclasspd128_mask:
  case X86::BI__builtin_ia32_fpclasspd256_mask:
  case X86::BI__builtin_ia32_fpclasspd512_mask:
    return emitX86Fpclass(builder, cgm, getLoc(expr->getExprLoc()), builtinID,
                          ops);
  case X86::BI__builtin_ia32_vp2intersect_q_512:
  case X86::BI__builtin_ia32_vp2intersect_q_256:
  case X86::BI__builtin_ia32_vp2intersect_q_128:
  case X86::BI__builtin_ia32_vp2intersect_d_512:
  case X86::BI__builtin_ia32_vp2intersect_d_256:
  case X86::BI__builtin_ia32_vp2intersect_d_128: {
    unsigned numElts = cast<cir::VectorType>(ops[0].getType()).getSize();
    mlir::Location loc = getLoc(expr->getExprLoc());
    StringRef intrinsicName;

    switch (builtinID) {
    default:
      llvm_unreachable("Unexpected builtin");
    case X86::BI__builtin_ia32_vp2intersect_q_512:
      intrinsicName = "x86.avx512.vp2intersect.q.512";
      break;
    case X86::BI__builtin_ia32_vp2intersect_q_256:
      intrinsicName = "x86.avx512.vp2intersect.q.256";
      break;
    case X86::BI__builtin_ia32_vp2intersect_q_128:
      intrinsicName = "x86.avx512.vp2intersect.q.128";
      break;
    case X86::BI__builtin_ia32_vp2intersect_d_512:
      intrinsicName = "x86.avx512.vp2intersect.d.512";
      break;
    case X86::BI__builtin_ia32_vp2intersect_d_256:
      intrinsicName = "x86.avx512.vp2intersect.d.256";
      break;
    case X86::BI__builtin_ia32_vp2intersect_d_128:
      intrinsicName = "x86.avx512.vp2intersect.d.128";
      break;
    }

    auto resVector = cir::VectorType::get(builder.getBoolTy(), numElts);

    cir::RecordType resRecord =
        cir::RecordType::get(&getMLIRContext(), {resVector, resVector}, false,
                             false, cir::RecordType::RecordKind::Struct);

    mlir::Value call =
        cir::LLVMIntrinsicCallOp::create(
            builder, getLoc(expr->getExprLoc()),
            cgm.getStringAttrCached(intrinsicName), resRecord,
            mlir::ValueRange{ops[0], ops[1]})
            .getResult();
    mlir::Value result =
        cir::ExtractMemberOp::create(builder, loc, resVector, call, 0);
    result = emitX86MaskedCompareResult(builder, result, numElts, nullptr, loc);
    Address addr = Address(
        ops[2], clang::CharUnits::fromQuantity(std::max(1U, numElts / 8)));
    builder.createStore(loc, result, addr);

    result = cir::ExtractMemberOp::create(builder, loc, resVector, call, 1);
    result = emitX86MaskedCompareResult(builder, result, numElts, nullptr, loc);
    addr = Address(ops[3],
                   clang::CharUnits::fromQuantity(std::max(1U, numElts / 8)));
    builder.createStore(loc, result, addr);
    return mlir::Value{};
  }
  case X86::BI__builtin_ia32_vpmultishiftqb128:
  case X86::BI__builtin_ia32_vpmultishiftqb256:
  case X86::BI__builtin_ia32_vpmultishiftqb512:
    return emitX86Vpmultishiftqb(builder, cgm, builtinID, ops,
                                 getLoc(expr->getExprLoc()));
  case X86::BI__builtin_ia32_cmpeqps:
  case X86::BI__builtin_ia32_cmpeqpd:
    return emitVectorFCmp(*this, ops, getLoc(expr->getExprLoc()),
                          cir::CmpOpKind::eq, /*shouldInvert=*/false,
                          /*isSignaling=*/false,
                          expr->getFPFeaturesInEffect(getLangOpts()));
  case X86::BI__builtin_ia32_vpshufbitqmb128_mask:
  case X86::BI__builtin_ia32_vpshufbitqmb256_mask:
  case X86::BI__builtin_ia32_vpshufbitqmb512_mask:
    return emitX86Vpshufbitqmb(builder, cgm, builtinID, ops,
                               getLoc(expr->getExprLoc()));
  case X86::BI__builtin_ia32_cmpltps:
  case X86::BI__builtin_ia32_cmpltpd:
    return emitVectorFCmp(*this, ops, getLoc(expr->getExprLoc()),
                          cir::CmpOpKind::lt, /*shouldInvert=*/false,
                          /*isSignaling=*/true,
                          expr->getFPFeaturesInEffect(getLangOpts()));
  case X86::BI__builtin_ia32_cmpleps:
  case X86::BI__builtin_ia32_cmplepd:
    return emitVectorFCmp(*this, ops, getLoc(expr->getExprLoc()),
                          cir::CmpOpKind::le, /*shouldInvert=*/false,
                          /*isSignaling=*/true,
                          expr->getFPFeaturesInEffect(getLangOpts()));
  case X86::BI__builtin_ia32_cmpunordps:
  case X86::BI__builtin_ia32_cmpunordpd:
    return emitVectorFCmp(*this, ops, getLoc(expr->getExprLoc()),
                          cir::CmpOpKind::funo, /*shouldInvert=*/false,
                          /*isSignaling=*/false,
                          expr->getFPFeaturesInEffect(getLangOpts()));
  case X86::BI__builtin_ia32_cmpneqps:
  case X86::BI__builtin_ia32_cmpneqpd:
    return emitVectorFCmp(*this, ops, getLoc(expr->getExprLoc()),
                          cir::CmpOpKind::ne, /*shouldInvert=*/false,
                          /*isSignaling=*/false,
                          expr->getFPFeaturesInEffect(getLangOpts()));
  case X86::BI__builtin_ia32_cmpnltps:
  case X86::BI__builtin_ia32_cmpnltpd:
    return emitVectorFCmp(*this, ops, getLoc(expr->getExprLoc()),
                          cir::CmpOpKind::fuge, /*shouldInvert=*/false,
                          /*isSignaling=*/true,
                          expr->getFPFeaturesInEffect(getLangOpts()));
  case X86::BI__builtin_ia32_cmpnleps:
  case X86::BI__builtin_ia32_cmpnlepd:
    return emitVectorFCmp(*this, ops, getLoc(expr->getExprLoc()),
                          cir::CmpOpKind::fugt, /*shouldInvert=*/false,
                          /*isSignaling=*/true,
                          expr->getFPFeaturesInEffect(getLangOpts()));
  case X86::BI__builtin_ia32_cmpordps:
  case X86::BI__builtin_ia32_cmpordpd:
    return emitVectorFCmp(*this, ops, getLoc(expr->getExprLoc()),
                          cir::CmpOpKind::ford, /*shouldInvert=*/false,
                          /*isSignaling=*/false,
                          expr->getFPFeaturesInEffect(getLangOpts()));
  case X86::BI__builtin_ia32_cmpps:
  case X86::BI__builtin_ia32_cmpps256:
  case X86::BI__builtin_ia32_cmppd:
  case X86::BI__builtin_ia32_cmppd256: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    // Generic packed FP compare with dynamic condition code (ops[2]).
    // Map the 5-bit CC to the same direct FP predicate OGCG uses.
    unsigned cc =
        ops[2].getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue() &
        0x1f;
    bool isFPConstrained =
        expr->getFPFeaturesInEffect(getLangOpts()).isFPConstrained() ||
        IsFPConstrained;
    if (isFPConstrained && ((cc & 0xf) == 0x0b || (cc & 0xf) == 0x0f)) {
      llvm::StringRef intrinsicName;
      switch (builtinID) {
      default:
        llvm_unreachable("unexpected dynamic packed compare builtin");
      case X86::BI__builtin_ia32_cmpps:
        intrinsicName = "x86.sse.cmp.ps";
        break;
      case X86::BI__builtin_ia32_cmpps256:
        intrinsicName = "x86.avx.cmp.ps.256";
        break;
      case X86::BI__builtin_ia32_cmppd:
        intrinsicName = "x86.sse2.cmp.pd";
        break;
      case X86::BI__builtin_ia32_cmppd256:
        intrinsicName = "x86.avx.cmp.pd.256";
        break;
      }
      llvm::SmallVector<mlir::Value, 2> cmpOps = {ops[0], ops[1]};
      return emitX86CmpIntrinsicCall(builder, cgm, cmpOps, loc, intrinsicName,
                                     cc);
    }
    cir::CmpOpKind pred;
    bool isSignaling = false;
    // Predicates for 16-31 repeat the 0-15 predicates. Only the signaling
    // behavior differs.
    switch (cc & 0xf) {
    case 0x00:
      pred = cir::CmpOpKind::eq;
      isSignaling = false;
      break; // OEQ
    case 0x01:
      pred = cir::CmpOpKind::lt;
      isSignaling = true;
      break; // OLT
    case 0x02:
      pred = cir::CmpOpKind::le;
      isSignaling = true;
      break; // OLE
    case 0x03:
      pred = cir::CmpOpKind::funo;
      isSignaling = false;
      break; // UNO
    case 0x04:
      pred = cir::CmpOpKind::ne;
      isSignaling = false;
      break; // UNE
    case 0x05:
      pred = cir::CmpOpKind::fuge;
      isSignaling = true;
      break; // UGE
    case 0x06:
      pred = cir::CmpOpKind::fugt;
      isSignaling = true;
      break; // UGT
    case 0x07:
      pred = cir::CmpOpKind::ford;
      isSignaling = false;
      break; // ORD
    case 0x08:
      pred = cir::CmpOpKind::fueq;
      isSignaling = false;
      break; // UEQ
    case 0x09:
      pred = cir::CmpOpKind::fult;
      isSignaling = true;
      break; // ULT
    case 0x0a:
      pred = cir::CmpOpKind::fule;
      isSignaling = true;
      break; // ULE
    case 0x0b: {
      // FALSE: return all-zero result
      mlir::Location loc = getLoc(expr->getExprLoc());
      return builder.getNullValue(ops[0].getType(), loc);
    }
    case 0x0c:
      pred = cir::CmpOpKind::fone;
      isSignaling = false;
      break; // ONE
    case 0x0d:
      pred = cir::CmpOpKind::ge;
      isSignaling = true;
      break; // OGE
    case 0x0e:
      pred = cir::CmpOpKind::gt;
      isSignaling = true;
      break; // OGT
    case 0x0f: {
      // TRUE: return all-ones result (all bits set).
      // Create integer all-ones vector and bitcast to the FP vector type.
      mlir::Location loc = getLoc(expr->getExprLoc());
      auto vecTy = cast<cir::VectorType>(ops[0].getType());
      unsigned numElts = vecTy.getSize();
      unsigned eltBitWidth = builder.getCIRIntOrFloatBitWidth(
          vecTy.getElementType());
      auto intEltTy = builder.getSIntNTy(eltBitWidth);
      auto intVecTy = cir::VectorType::get(intEltTy, numElts);
      auto oneAttr =
          cir::IntAttr::get(intEltTy, llvm::APInt::getAllOnes(eltBitWidth));
      llvm::SmallVector<mlir::Attribute> elems(numElts, oneAttr);
      auto vecAttr = cir::ConstVectorAttr::get(
          intVecTy, mlir::ArrayAttr::get(builder.getContext(), elems));
      mlir::Value allOnes = cir::ConstantOp::create(builder, loc, vecAttr);
      return builder.createBitcast(allOnes, vecTy);
    }
    default:
      llvm_unreachable("Unhandled CC");
    }
    if (cc & 0x10)
      isSignaling = !isSignaling;
    return emitVectorFCmp(*this, ops, loc, pred,
                          /*shouldInvert=*/false, isSignaling,
                          expr->getFPFeaturesInEffect(getLangOpts()));
  }
  case X86::BI__builtin_ia32_cmpeqss:
    return emitX86CmpIntrinsicCall(builder, cgm, ops,
                                   getLoc(expr->getExprLoc()),
                                   "x86.sse.cmp.ss", 0);
  case X86::BI__builtin_ia32_cmpltss:
    return emitX86CmpIntrinsicCall(builder, cgm, ops,
                                   getLoc(expr->getExprLoc()),
                                   "x86.sse.cmp.ss", 1);
  case X86::BI__builtin_ia32_cmpless:
    return emitX86CmpIntrinsicCall(builder, cgm, ops,
                                   getLoc(expr->getExprLoc()),
                                   "x86.sse.cmp.ss", 2);
  case X86::BI__builtin_ia32_cmpunordss:
    return emitX86CmpIntrinsicCall(builder, cgm, ops,
                                   getLoc(expr->getExprLoc()),
                                   "x86.sse.cmp.ss", 3);
  case X86::BI__builtin_ia32_cmpneqss:
    return emitX86CmpIntrinsicCall(builder, cgm, ops,
                                   getLoc(expr->getExprLoc()),
                                   "x86.sse.cmp.ss", 4);
  case X86::BI__builtin_ia32_cmpnltss:
    return emitX86CmpIntrinsicCall(builder, cgm, ops,
                                   getLoc(expr->getExprLoc()),
                                   "x86.sse.cmp.ss", 5);
  case X86::BI__builtin_ia32_cmpnless:
    return emitX86CmpIntrinsicCall(builder, cgm, ops,
                                   getLoc(expr->getExprLoc()),
                                   "x86.sse.cmp.ss", 6);
  case X86::BI__builtin_ia32_cmpordss:
    return emitX86CmpIntrinsicCall(builder, cgm, ops,
                                   getLoc(expr->getExprLoc()),
                                   "x86.sse.cmp.ss", 7);
  case X86::BI__builtin_ia32_cmpeqsd:
    return emitX86CmpIntrinsicCall(builder, cgm, ops,
                                   getLoc(expr->getExprLoc()),
                                   "x86.sse2.cmp.sd", 0);
  case X86::BI__builtin_ia32_cmpltsd:
    return emitX86CmpIntrinsicCall(builder, cgm, ops,
                                   getLoc(expr->getExprLoc()),
                                   "x86.sse2.cmp.sd", 1);
  case X86::BI__builtin_ia32_cmplesd:
    return emitX86CmpIntrinsicCall(builder, cgm, ops,
                                   getLoc(expr->getExprLoc()),
                                   "x86.sse2.cmp.sd", 2);
  case X86::BI__builtin_ia32_cmpunordsd:
    return emitX86CmpIntrinsicCall(builder, cgm, ops,
                                   getLoc(expr->getExprLoc()),
                                   "x86.sse2.cmp.sd", 3);
  case X86::BI__builtin_ia32_cmpneqsd:
    return emitX86CmpIntrinsicCall(builder, cgm, ops,
                                   getLoc(expr->getExprLoc()),
                                   "x86.sse2.cmp.sd", 4);
  case X86::BI__builtin_ia32_cmpnltsd:
    return emitX86CmpIntrinsicCall(builder, cgm, ops,
                                   getLoc(expr->getExprLoc()),
                                   "x86.sse2.cmp.sd", 5);
  case X86::BI__builtin_ia32_cmpnlesd:
    return emitX86CmpIntrinsicCall(builder, cgm, ops,
                                   getLoc(expr->getExprLoc()),
                                   "x86.sse2.cmp.sd", 6);
  case X86::BI__builtin_ia32_cmpordsd:
    return emitX86CmpIntrinsicCall(builder, cgm, ops,
                                   getLoc(expr->getExprLoc()),
                                   "x86.sse2.cmp.sd", 7);
  case X86::BI__builtin_ia32_cmpph128_mask:
  case X86::BI__builtin_ia32_cmpph256_mask:
  case X86::BI__builtin_ia32_cmpph512_mask:
  case X86::BI__builtin_ia32_cmpps128_mask:
  case X86::BI__builtin_ia32_cmpps256_mask:
  case X86::BI__builtin_ia32_cmpps512_mask:
  case X86::BI__builtin_ia32_cmppd128_mask:
  case X86::BI__builtin_ia32_cmppd256_mask:
  case X86::BI__builtin_ia32_cmppd512_mask:
  case X86::BI__builtin_ia32_vcmpbf16512_mask:
  case X86::BI__builtin_ia32_vcmpbf16256_mask:
  case X86::BI__builtin_ia32_vcmpbf16128_mask: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    bool isFPConstrained =
        expr->getFPFeaturesInEffect(getLangOpts()).isFPConstrained() ||
        IsFPConstrained;
    if (isFPConstrained) {
      switch (builtinID) {
      case X86::BI__builtin_ia32_cmpph128_mask:
      case X86::BI__builtin_ia32_cmpph256_mask:
      case X86::BI__builtin_ia32_cmpph512_mask:
      case X86::BI__builtin_ia32_cmpps128_mask:
      case X86::BI__builtin_ia32_cmpps256_mask:
      case X86::BI__builtin_ia32_cmpps512_mask:
      case X86::BI__builtin_ia32_cmppd128_mask:
      case X86::BI__builtin_ia32_cmppd256_mask:
      case X86::BI__builtin_ia32_cmppd512_mask:
        // Match OGCG's constrained lowering for the AVX-512 masked compare
        // families that already have legacy x86 compare intrinsics.
        return emitX86MaskedFCompareIntrinsic(builder, cgm, builtinID, ops,
                                              loc, isFPConstrained);
      default:
        break;
      }
    }

    // Masked FP comparison: extract CC, create VecCmpOp with <N x si1> result,
    // carry signaling when strict FP is enabled, and apply the input mask.
    unsigned cc =
        ops[2].getDefiningOp<cir::ConstantOp>().getIntValue().getZExtValue() &
        0x1f;
    auto vecTy = cast<cir::VectorType>(ops[0].getType());
    unsigned numElts = vecTy.getSize();

    cir::CmpOpKind pred;
    bool isSignaling = false;
    // Predicates for 16-31 repeat the 0-15 predicates. Only the signaling
    // behavior differs.
    switch (cc & 0xf) {
    case 0x00:
      pred = cir::CmpOpKind::eq;
      isSignaling = false;
      break; // OEQ
    case 0x01:
      pred = cir::CmpOpKind::lt;
      isSignaling = true;
      break; // OLT
    case 0x02:
      pred = cir::CmpOpKind::le;
      isSignaling = true;
      break; // OLE
    case 0x03:
      pred = cir::CmpOpKind::funo;
      isSignaling = false;
      break; // UNO
    case 0x04:
      pred = cir::CmpOpKind::ne;
      isSignaling = false;
      break; // UNE
    case 0x05:
      pred = cir::CmpOpKind::fuge;
      isSignaling = true;
      break; // UGE
    case 0x06:
      pred = cir::CmpOpKind::fugt;
      isSignaling = true;
      break; // UGT
    case 0x07:
      pred = cir::CmpOpKind::ford;
      isSignaling = false;
      break; // ORD
    case 0x08:
      pred = cir::CmpOpKind::fueq;
      isSignaling = false;
      break; // UEQ
    case 0x09:
      pred = cir::CmpOpKind::fult;
      isSignaling = true;
      break; // ULT
    case 0x0a:
      pred = cir::CmpOpKind::fule;
      isSignaling = true;
      break; // ULE
    case 0x0b: {
      // FALSE: return all-zero mask.
      auto resultTy = cir::VectorType::get(builder.getSIntNTy(1), numElts);
      mlir::Value cmp = builder.getNullValue(resultTy, loc);
      return emitX86MaskedCompareResult(builder, cmp, numElts, ops[3], loc);
    }
    case 0x0c:
      pred = cir::CmpOpKind::fone;
      isSignaling = false;
      break; // ONE
    case 0x0d:
      pred = cir::CmpOpKind::ge;
      isSignaling = true;
      break; // OGE
    case 0x0e:
      pred = cir::CmpOpKind::gt;
      isSignaling = true;
      break; // OGT
    case 0x0f: {
      // TRUE: return all-ones mask.
      auto boolTy = builder.getSIntNTy(1);
      auto resultTy = cir::VectorType::get(boolTy, numElts);
      auto oneAttr = cir::IntAttr::get(boolTy, llvm::APInt::getAllOnes(1));
      llvm::SmallVector<mlir::Attribute> elems(numElts, oneAttr);
      auto vecAttr = cir::ConstVectorAttr::get(
          resultTy, mlir::ArrayAttr::get(builder.getContext(), elems));
      mlir::Value cmp = cir::ConstantOp::create(builder, loc, vecAttr);
      return emitX86MaskedCompareResult(builder, cmp, numElts, ops[3], loc);
    }
    default:
      llvm_unreachable("Unhandled CC");
    }
    if (cc & 0x10)
      isSignaling = !isSignaling;

    auto resultTy = cir::VectorType::get(builder.getSIntNTy(1), numElts);
    auto cmpOp =
        cir::VecCmpOp::create(builder, loc, resultTy, pred, ops[0], ops[1]);
    maybeAttachFPConstraints(cmpOp.getOperation(),
                             expr->getFPFeaturesInEffect(getLangOpts()));
    maybeMarkSignalingFPCompare(cmpOp.getOperation(), isSignaling);
    return emitX86MaskedCompareResult(builder, cmpOp, numElts, ops[3], loc);
  }
  case X86::BI__builtin_ia32_vcvtph2ps_mask:
  case X86::BI__builtin_ia32_vcvtph2ps256_mask:
  case X86::BI__builtin_ia32_vcvtph2ps512_mask: {
    return emitX86CvtF16ToFloatExpr(*this, expr, ops, convertType(expr->getType()));
  }
  case X86::BI__builtin_ia32_cvtneps2bf16_128_mask: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    cir::VectorType resTy = cast<cir::VectorType>(convertType(expr->getType()));

    cir::VectorType inputTy = cast<cir::VectorType>(ops[0].getType());
    unsigned numElts = inputTy.getSize();

    mlir::Value mask = getMaskVecValue(builder, loc, ops[2], numElts);

    SmallVector<mlir::Value, 3> args;
    args.push_back(ops[0]);
    args.push_back(ops[1]);
    args.push_back(mask);

    return cir::LLVMIntrinsicCallOp::create(
               builder, loc,
               cgm.getStringAttrCached("x86.avx512bf16.mask.cvtneps2bf16.128"),
               resTy, args)
        .getResult();
  }
  case X86::BI__builtin_ia32_cvtneps2bf16_256_mask:
  case X86::BI__builtin_ia32_cvtneps2bf16_512_mask: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    cir::VectorType resTy = cast<cir::VectorType>(convertType(expr->getType()));
    StringRef intrinsicName;
    if (builtinID == X86::BI__builtin_ia32_cvtneps2bf16_256_mask) {
      intrinsicName = "x86.avx512bf16.cvtneps2bf16.256";
    } else {
      assert(builtinID == X86::BI__builtin_ia32_cvtneps2bf16_512_mask);
      intrinsicName = "x86.avx512bf16.cvtneps2bf16.512";
    }

    mlir::Value res = cir::LLVMIntrinsicCallOp::create(
                          builder, loc, cgm.getStringAttrCached(intrinsicName),
                          resTy, mlir::ValueRange{ops[0]})
                          .getResult();

    return emitX86Select(builder, loc, ops[2], res, ops[1]);
  }
  case X86::BI__cpuid:
  case X86::BI__cpuidex: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    mlir::Value subFuncId = builtinID == X86::BI__cpuidex
                                ? ops[2]
                                : builder.getConstInt(loc, sInt32Ty, 0);
    cir::CpuIdOp::create(builder, loc, /*cpuInfo=*/ops[0],
                         /*functionId=*/ops[1], /*subFunctionId=*/subFuncId);
    return mlir::Value{};
  }
  case X86::BI__emul:
  case X86::BI__emulu: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    bool isSigned = (builtinID == X86::BI__emul);
    cir::IntType i64Ty =
        isSigned ? builder.getSIntNTy(64) : builder.getUIntNTy(64);
    mlir::Value lhs = builder.createIntCast(ops[0], i64Ty);
    mlir::Value rhs = builder.createIntCast(ops[1], i64Ty);
    return isSigned ? builder.createNSWMul(loc, lhs, rhs)
                    : builder.createNUWAMul(loc, lhs, rhs);
  }
  case X86::BI__mulh:
  case X86::BI__umulh:
  case X86::BI_mul128:
  case X86::BI_umul128: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    bool isSigned =
        (builtinID == X86::BI__mulh || builtinID == X86::BI_mul128);
    cir::IntType i128Ty =
        isSigned ? builder.getSIntNTy(128) : builder.getUIntNTy(128);

    mlir::Value lhs = builder.createIntCast(ops[0], i128Ty);
    mlir::Value rhs = builder.createIntCast(ops[1], i128Ty);

    mlir::Value mulResult = isSigned ? builder.createNSWMul(loc, lhs, rhs)
                                     : builder.createNUWAMul(loc, lhs, rhs);

    // Shift right by 64 to extract the high bits.
    mlir::Value higherBits = builder.createShiftRight(loc, mulResult, 64);

    // Truncate back to the return type (i64).
    auto resTy = mlir::cast<cir::IntType>(convertType(expr->getType()));
    higherBits = builder.createIntCast(higherBits, resTy);

    if (builtinID == X86::BI__mulh || builtinID == X86::BI__umulh)
      return higherBits;

    // _mul128/_umul128: store high bits to output pointer and return low bits.
    builder.CIRBaseBuilderTy::createStore(loc, higherBits, ops[2]);
    return builder.createIntCast(mulResult, resTy);
  }
  case X86::BI__faststorefence: {
    cir::AtomicFenceOp::create(
        builder, getLoc(expr->getExprLoc()),
        cir::MemOrder::SequentiallyConsistent,
        cir::SyncScopeKindAttr::get(&getMLIRContext(),
                                    cir::SyncScopeKind::System));
    return mlir::Value{};
  }
  case X86::BI__shiftleft128:
  case X86::BI__shiftright128: {
    // Flip low/high ops and zero-extend amount to matching type.
    // shiftleft128(Low, High, Amt) -> fshl(High, Low, Amt)
    // shiftright128(Low, High, Amt) -> fshr(High, Low, Amt)
    std::swap(ops[0], ops[1]);

    // Zero-extend shift amount to i64 if needed
    auto amtTy = mlir::cast<cir::IntType>(ops[2].getType());
    cir::IntType i64Ty = builder.getUInt64Ty();

    if (amtTy != i64Ty)
      ops[2] = builder.createIntCast(ops[2], i64Ty);

    const StringRef intrinsicName =
        (builtinID == X86::BI__shiftleft128) ? "fshl" : "fshr";
    return cir::LLVMIntrinsicCallOp::create(
               builder, getLoc(expr->getExprLoc()),
               cgm.getStringAttrCached(intrinsicName), i64Ty,
               mlir::ValueRange{ops[0], ops[1], ops[2]})
        .getResult();
  }
  case X86::BI_ReadWriteBarrier:
  case X86::BI_ReadBarrier:
  case X86::BI_WriteBarrier: {
    cir::AtomicFenceOp::create(
        builder, getLoc(expr->getExprLoc()),
        cir::MemOrder::SequentiallyConsistent,
        cir::SyncScopeKindAttr::get(&getMLIRContext(),
                                    cir::SyncScopeKind::SingleThread));
    return mlir::Value{};
  }
  case X86::BI_AddressOfReturnAddress: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    mlir::Value addr =
        cir::AddrOfReturnAddrOp::create(builder, loc, allocaInt8PtrTy);
    return builder.createCast(loc, cir::CastKind::bitcast, addr, voidPtrTy);
  }
  case X86::BI__ud2:
    // llvm.trap makes a ud2a instruction on x86.
    cir::LLVMIntrinsicCallOp::create(builder, getLoc(expr->getExprLoc()),
                                     cgm.getStringAttrCached("trap"),
                                     mlir::Type{}, mlir::ValueRange{});
    return mlir::Value{};
  case X86::BI__stosb:
  case X86::BI__int2c:
  case X86::BI__readfsbyte:
  case X86::BI__readfsword:
  case X86::BI__readfsdword:
  case X86::BI__readfsqword:
  case X86::BI__readgsbyte:
  case X86::BI__readgsword:
  case X86::BI__readgsdword:
  case X86::BI__readgsqword:
  case X86::BI__builtin_ia32_encodekey128_u32:
  case X86::BI__builtin_ia32_encodekey256_u32: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented X86 builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinID));
    return mlir::Value{};
  }
  case X86::BI__builtin_ia32_aesenc128kl_u8:
  case X86::BI__builtin_ia32_aesdec128kl_u8:
  case X86::BI__builtin_ia32_aesenc256kl_u8:
  case X86::BI__builtin_ia32_aesdec256kl_u8: {
    llvm::StringRef intrinsicName;
    switch (builtinID) {
    default:
      llvm_unreachable("Unexpected builtin");
    case X86::BI__builtin_ia32_aesenc128kl_u8:
      intrinsicName = "x86.aesenc128kl";
      break;
    case X86::BI__builtin_ia32_aesdec128kl_u8:
      intrinsicName = "x86.aesdec128kl";
      break;
    case X86::BI__builtin_ia32_aesenc256kl_u8:
      intrinsicName = "x86.aesenc256kl";
      break;
    case X86::BI__builtin_ia32_aesdec256kl_u8:
      intrinsicName = "x86.aesdec256kl";
      break;
    }

    return emitX86Aes(*this, builder, getLoc(expr->getExprLoc()), intrinsicName,
                      convertType(expr->getType()), ops);
  }
  case X86::BI__builtin_ia32_aesencwide128kl_u8:
  case X86::BI__builtin_ia32_aesdecwide128kl_u8:
  case X86::BI__builtin_ia32_aesencwide256kl_u8:
  case X86::BI__builtin_ia32_aesdecwide256kl_u8: {
    llvm::StringRef intrinsicName;
    switch (builtinID) {
    default:
      llvm_unreachable("Unexpected builtin");
    case X86::BI__builtin_ia32_aesencwide128kl_u8:
      intrinsicName = "x86.aesencwide128kl";
      break;
    case X86::BI__builtin_ia32_aesdecwide128kl_u8:
      intrinsicName = "x86.aesdecwide128kl";
      break;
    case X86::BI__builtin_ia32_aesencwide256kl_u8:
      intrinsicName = "x86.aesencwide256kl";
      break;
    case X86::BI__builtin_ia32_aesdecwide256kl_u8:
      intrinsicName = "x86.aesdecwide256kl";
      break;
    }

    return emitX86Aeswide(*this, builder, getLoc(expr->getExprLoc()),
                          intrinsicName, convertType(expr->getType()), ops);
  }
  case X86::BI__builtin_ia32_vfcmaddcph512_mask:
  case X86::BI__builtin_ia32_vfmaddcph512_mask: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    bool isConj = builtinID == X86::BI__builtin_ia32_vfcmaddcph512_mask;
    llvm::StringRef intrinsicName =
        isConj ? "x86.avx512fp16.mask.vfcmadd.cph.512"
               : "x86.avx512fp16.mask.vfmadd.cph.512";
    mlir::Value call =
        cir::LLVMIntrinsicCallOp::create(builder, loc,
                                         cgm.getStringAttrCached(intrinsicName),
                                         ops[0].getType(), ops)
            .getResult();
    return emitX86Select(builder, loc, ops[3], call, ops[0]);
  }
  case X86::BI__builtin_ia32_vfcmaddcsh_round_mask:
  case X86::BI__builtin_ia32_vfmaddcsh_round_mask: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    bool isConj = builtinID == X86::BI__builtin_ia32_vfcmaddcsh_round_mask;
    llvm::StringRef intrinsicName =
        isConj ? "x86.avx512fp16.mask.vfcmadd.csh"
               : "x86.avx512fp16.mask.vfmadd.csh";
    mlir::Value call =
        cir::LLVMIntrinsicCallOp::create(builder, loc,
                                         cgm.getStringAttrCached(intrinsicName),
                                         ops[0].getType(), ops)
            .getResult();
    // Scalar mask: AND with 1 to extract the lowest bit.
    mlir::Value one = builder.getConstInt(loc, ops[3].getType(), 1);
    mlir::Value maskedBit = builder.createAnd(loc, ops[3], one);
    return emitX86Select(builder, loc, maskedBit, call, ops[0]);
  }
  case X86::BI__builtin_ia32_vfcmaddcsh_round_mask3:
  case X86::BI__builtin_ia32_vfmaddcsh_round_mask3: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    bool isConj = builtinID == X86::BI__builtin_ia32_vfcmaddcsh_round_mask3;
    llvm::StringRef intrinsicName =
        isConj ? "x86.avx512fp16.mask.vfcmadd.csh"
               : "x86.avx512fp16.mask.vfmadd.csh";
    mlir::Value call =
        cir::LLVMIntrinsicCallOp::create(builder, loc,
                                         cgm.getStringAttrCached(intrinsicName),
                                         ops[0].getType(), ops)
            .getResult();
    // mask3 variant: shuffle element 0 from call, elements 1-3 from ops[2].
    return builder.createVecShuffle(loc, call, ops[2],
                                    ArrayRef<int64_t>{0, 5, 6, 7});
  }
  case X86::BI__builtin_ia32_prefetchi: {
    mlir::Location loc = getLoc(expr->getExprLoc());
    // prefetchi maps to llvm.prefetch(ptr, rw=0, locality, cache_type=0).
    mlir::Value rw = builder.getConstInt(loc, builder.getSInt32Ty(), 0);
    mlir::Value cacheType = builder.getConstInt(loc, builder.getSInt32Ty(), 0);
    cir::LLVMIntrinsicCallOp::create(
        builder, loc, cgm.getStringAttrCached("prefetch"), mlir::Type{},
        mlir::ValueRange{ops[0], rw, ops[1], cacheType});
    return mlir::Value{};
  }
  }
}
